// SPDX-License-Identifier: GPL-3.0-only
// Harmony Link's default-path proof (tests/link_null.sh): one deterministic script played into the plugin through
// its VST entry only, recorded to one file. The same file compiles unchanged against the link baseline (e58a6f3, no
// Link) and against this tree with TP_LINK=0 and 1; with Link left Off, all three must write the same audio, saved
// project and final values (link_null.sh says what it asks of the notifications). It uses
// only base-era API: VSTPluginMain, processReplacing, dispatcher, setParameter, getParameter, tp::kParams (indices
// found by id) and the host callback. (Ported from the Conductor's conductor_null.cpp.)
//
// Link is a parameter inserted after Sync, not appended, so the harness works in the base's index space: the base's
// parameter i is the build's i, or i + 1 from Link on. Link is looked up by its id "link" (absent in the base and
// with TP_LINK=0); the harness never writes it.
//
// The script, about 90 s at 44100 Hz at random block sizes 1..512 (fixed-seed LCG):
//   - pads on channels 1 and 16, notes on all 16 channels (stacks of 3-4 on channel 16, notes on channel 15);
//   - CCs 0..127 with random values on all channels, except 120 and 123 (only at set times, on all 16 channels:
//     Performer's panic) and 102/103 on channel 16 (only the set near-miss handshakes below);
//   - near-miss handshakes on channel 16, and one exact handshake on channel 15; the key CCs 104..106 on 16;
//   - program changes, pitch bend and channel pressure now and then;
//   - every base parameter written once by the host (display values too), then Volume, Piece, Hold, Mute Seq,
//     Next chord and Sync written as parameters; a fake transport that plays and stops (sometimes mid-bar), with
//     ppqPos, tempo and barStartPos advancing;
//   - a save and load of the project (effGetChunk, effSetChunk) in the middle.
//
// Output (argv[1]), in this order:
//   audio          float32 stereo interleaved, every frame
//   state          the mid-run save, then the final save, each normalised to the base's parameters: the
//                  framework's chunk ("MPCP", version, count, per parameter id/plain/normalised, then the extra
//                  blob, Third Planet's "TP1" project) re-written without the entry "link", then the extra
//   notifications  every audioMasterAutomate for a base parameter: int32 block, int32 base index, float32 value
//                  (12 bytes each)
//   values         getParameter of every base parameter at the end (float32 each)
// stdout: "sections audio=A state=S notify=N values=V state_mid=M" (byte counts; M: the mid-run save's share of S),
// "midbar frame=F block=K" (the first start mid-bar, -1 none: the script must exercise T-1745), "base_controls C"
// (the base's control count: base indices from C on are display values), then a few facts for the log. Beside the output, <out>.blocks: every block's size (uint32), for the report only.
// stderr: notifications filtered out (about Link, or past the base's count; with Link Off there should be none).
//   c++ -std=c++17 -O2 -I <src> -I deps/mpc-plugin-sdk/framework plugins/ThirdPlanet/tests/link_null.cpp \
//       <src>/app_unit.cpp <src>/engine_unit.cpp <src>/power.cpp <src>/sky.cpp
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ThirdPlanet.cpp"

namespace {

constexpr double kRate = 44100.0;
constexpr long kTotal = 90L * 44100L;
constexpr int kMaxBlock = 512;

uint32_t seed = 0x7E3D1A5Bu;
uint32_t rnd() { seed = seed * 1664525u + 1013904223u; return seed >> 8; }   // 24 bits
int pick(int n) { return (int)(rnd() % (uint32_t)n); }                       // 0..n-1
float rfloat() { return (float)(rnd() & 0xFFFFFF) / (float)0xFFFFFF; }       // 0..1

// ---- the fake host ----
VstTimeInfo ti;
long blockNo = 0;
struct Note { int32_t block, index; float value; };
std::vector<Note> notes;

VstIntPtr host(AEffect*, VstInt32 opcode, VstInt32 index, VstIntPtr, void*, float opt)
{
    if (opcode == audioMasterVersion) return 2400;
    if (opcode == audioMasterGetTime) return (VstIntPtr)&ti;
    if (opcode == audioMasterAutomate) notes.push_back(Note{(int32_t)blockNo, (int32_t)index, opt});
    return 0;
}

int paramIndex(const char* id)
{
    for (int i = 0; i < tp::kNumControls; ++i)
        if (!std::strcmp(tp::kParams[i].id, id)) return i;
    std::fprintf(stderr, "link_null: no parameter %s\n", id);
    std::exit(2);
}

// Link's index in this build (-1: none), and the base's parameter i in this build's index space and back
int gLink = -1;
void findLink()
{
    for (int i = 0; i < tp::kNumControls; ++i)
        if (!std::strcmp(tp::kParams[i].id, "link")) gLink = i;
}
int toBuild(int i) { return gLink >= 0 && i >= gLink ? i + 1 : i; }
int toBase(int j) { return gLink < 0 || j < gLink ? j : j == gLink ? -1 : j - 1; }

// ---- the MIDI script ----
struct Ev { long frame; uint8_t s, d1, d2; };
std::vector<Ev> evs;     // every event, sorted by frame at the end (stable: same-frame events keep their order)
void ev(long f, int status, int d1, int d2)
{
    if (f >= 0 && f < kTotal) evs.push_back(Ev{f, (uint8_t)status, (uint8_t)(d1 & 127), (uint8_t)(d2 & 127)});
}
void cc(long f, int ch, int n, int v) { ev(f, 0xB0 | ch, n, v); }
long sec(double s) { return (long)std::lround(s * kRate); }

// a note held for a while; note-offs as 0x80 or as a note-on of velocity 0
void note(long f, int ch, int n, long len)
{
    const int vel = 1 + pick(127);
    ev(f, 0x90 | ch, n, vel);
    if (pick(2)) { const int off = pick(128); ev(f + len, 0x80 | ch, n, off); }
    else ev(f + len, 0x90 | ch, n, 0);
}

// windows (frames) where channel 16's CC 102 and 103 stay out of the random stream: around each set handshake
std::vector<std::pair<long, long>> quiet;
bool inQuiet(long f)
{
    for (const auto& q : quiet)
        if (f >= q.first && f < q.second) return true;
    return false;
}

void buildMidi()
{
    // the set handshakes first (so the random stream can keep clear of them); channel 16 is index 15
    const int h = 15;
    auto shake = [&](double t, double gap, int ch, int v102, int v103, bool send102, bool send103) {
        const long f = sec(t);
        quiet.push_back({f - sec(1.0), f + sec(gap) + sec(1.0)});
        if (send102) cc(f, ch, 102, v102);
        if (send103) cc(f + sec(gap), ch, 103, v103);
    };
    shake(9.0, 0.10, h, 84, 79, true, true);     // the wrong letter
    shake(17.0, 0.0, h, 0, 80, false, true);     // 'P' alone
    shake(27.0, 0.65, h, 84, 80, true, true);    // 'T' then 'P' too late (> 500 ms)
    shake(39.0, 0.05, h, 83, 80, true, true);    // 'S' then 'P'
    shake(52.0, 0.05, 14, 84, 80, true, true);   // the right handshake on channel 15: not the harmony channel
    shake(63.0, 0.0, h, 84, 0, true, false);     // 'T' alone
    shake(64.0, 0.05, h, 84, 0, true, true);     // the release, in Own
    shake(77.0, 0.0, h, 80, 84, true, false);    // the letters the wrong way round...
    cc(sec(77.02), h, 103, 84);                  // ...('P' as 102, 'T' as 103)

    // Performer's panic: CC 123 then 120 on all 16 channels, at set times
    for (double t : {14.0, 44.5, 71.0, 86.0})
        for (int ch = 0; ch < 16; ++ch) cc(sec(t), ch, 123, 0), cc(sec(t), ch, 120, 0);

    // the key CCs on channel 16 at set times (a root, a mask's two halves)
    for (double t : {11.0, 33.0, 58.0, 81.0})
    {
        const int root = pick(12), lo = pick(128), hi = pick(32);
        cc(sec(t), h, 104, root), cc(sec(t) + 1, h, 105, lo), cc(sec(t) + 2, h, 106, hi);
    }

    // the random stream, second by second
    for (long s = 0; s < 90; ++s)
    {
        const long f0 = s * 44100L;
        auto at = [&]() { return f0 + pick(44100); };
        // (each random draw in its own statement: the order of a call's arguments is unspecified)
        // pads on channels 1 and 16 (notes 36..75)
        for (int k = pick(4); k-- > 0;)
        {
            const long f = at();
            const int ch = pick(2) ? 0 : 15, n = 36 + pick(40);
            const long len = sec(0.02) + pick((int)sec(1.5));
            note(f, ch, n, len);
        }
        // notes on any channel, any note
        for (int k = pick(6); k-- > 0;)
        {
            const long f = at();
            const int ch = pick(16), n = pick(128);
            const long len = 1 + pick((int)sec(2.0));
            note(f, ch, n, len);
        }
        // notes on channel 15 (a conductor's Lead channel)
        for (int k = pick(3); k-- > 0;)
        {
            const long f = at();
            const int n = 48 + pick(36);
            const long len = sec(0.05) + pick((int)sec(1.0));
            note(f, 14, n, len);
        }
        // stacks of 3-4 notes on channel 16, at one frame or spread over a few ms, held together
        if (pick(3) == 0)
        {
            const long f = at();
            const long len = sec(0.3) + pick((int)sec(3.0));
            const int root = 36 + pick(36);
            const int n = 3 + pick(2);
            const int iv[4] = {0, 3 + pick(2), 7, 10 + pick(2)};   // (braced: in order)
            const bool spread = pick(2);
            for (int k = 0; k < n; ++k)
            {
                const long fk = f + (spread ? pick((int)sec(0.03)) : 0);
                const int vel = 40 + pick(80);
                ev(fk, 0x90 | 15, root + iv[k], vel);
                ev(f + len, 0x80 | 15, root + iv[k], 0);
            }
        }
        // CCs: any number, any value, any channel (but not 120/123; not 102/103 on channel 16 near a set
        // handshake, and never 102 = 84 or 103 = 80 there, so the stream cannot complete or start one)
        for (int k = 4 + pick(24); k-- > 0;)
        {
            const long f = at();
            const int ch = pick(16);
            int n = pick(128), v = pick(128);
            if (n == 120 || n == 123) continue;
            if (ch == h && (n == 102 || n == 103))
            {
                if (inQuiet(f)) continue;
                if (n == 102 && v == 84) v = 85;
                if (n == 103 && v == 80) v = 81;
            }
            cc(f, ch, n, v);
        }
        // other channel messages now and then
        if (pick(4) == 0) { const long f = at(); const int ch = pick(16), v = pick(128); ev(f, 0xC0 | ch, v, 0); }
        if (pick(3) == 0)
        {
            const long f = at();
            const int ch = pick(16), lo = pick(128), hi = pick(128);
            ev(f, 0xE0 | ch, lo, hi);
        }
        if (pick(4) == 0) { const long f = at(); const int ch = pick(16), v = pick(128); ev(f, 0xD0 | ch, v, 0); }
    }
    for (size_t i = 1; i < evs.size(); ++i)   // (insertion sort: stable)
    {
        const Ev e = evs[i];
        size_t j = i;
        while (j > 0 && evs[j - 1].frame > e.frame) evs[j] = evs[j - 1], --j;
        evs[j] = e;
    }
}

// ---- parameter writes and the transport, at set times ----
enum Act { kWrite, kPlay, kStop, kTempo, kSave, kLoad };
struct Cue { long frame; Act act; int index; float value; };
std::vector<Cue> cues;

void buildCues(int base)
{
    // the host writes every one of the base's parameters once, display values included (a review finding: a host
    // write to a display value must be answered as the base answers it), and the display values again at the small
    // and the large end of their range (base indices, written to this build's; a cue holds this build's index)
    const int baseControls = tp::kNumControls - (gLink >= 0 ? 1 : 0);
    for (int i = 0; i < base; ++i) cues.push_back({sec(2.0 + 85.0 * i / base + 0.3 * rfloat()), kWrite, toBuild(i), rfloat()});
    for (int i = baseControls; i < base; ++i)
    {
        cues.push_back({sec(3.0 + 80.0 * rfloat()), kWrite, toBuild(i), 0.013f});
        cues.push_back({sec(3.0 + 80.0 * rfloat()), kWrite, toBuild(i), 1.0f});
    }
    const int iVol = paramIndex("volume"), iPiece = paramIndex("piece"), iHold = paramIndex("hold");
    const int iMute = paramIndex("mute_seq"), iNext = paramIndex("next_chord"), iSync = paramIndex("sync");
    for (double t = 1.5; t < 90.0; t += 2.0 + 2.0 * rfloat()) cues.push_back({sec(t), kWrite, iVol, rfloat()});
    for (double t : {13.0, 31.0, 47.0, 69.0}) cues.push_back({sec(t), kWrite, iPiece, rfloat()});
    cues.push_back({sec(15.0), kWrite, iHold, 1.0f});
    cues.push_back({sec(22.5), kWrite, iHold, 0.0f});
    cues.push_back({sec(57.0), kWrite, iHold, 1.0f});
    cues.push_back({sec(60.0), kWrite, iHold, 0.0f});
    cues.push_back({sec(35.0), kWrite, iMute, 1.0f});
    cues.push_back({sec(42.0), kWrite, iMute, 0.0f});
    for (double t : {12.0, 34.0, 55.5, 66.0, 83.0}) cues.push_back({sec(t), kWrite, iNext, 1.0f});
    cues.push_back({sec(66.5), kWrite, iNext, 0.0f});
    // Sync and the transport: on with the MPC stopped (rest); play (mid-bar, after the ppq moved); stop; play;
    // a tempo change; Sync off while playing; on again; stop; play mid-bar; off while stopped
    cues.push_back({sec(5.0), kWrite, iSync, 1.0f});
    cues.push_back({sec(7.3), kPlay, 0, 0.0f});
    cues.push_back({sec(18.0), kStop, 0, 0.0f});
    cues.push_back({sec(20.7), kPlay, 0, 0.0f});
    cues.push_back({sec(24.0), kTempo, 0, 97.0f});
    cues.push_back({sec(36.0), kWrite, iSync, 0.0f});
    cues.push_back({sec(41.0), kWrite, iSync, 1.0f});
    cues.push_back({sec(46.0), kSave, 0, 0.0f});
    cues.push_back({sec(48.0), kLoad, 0, 0.0f});
    cues.push_back({sec(50.0), kStop, 0, 0.0f});
    cues.push_back({sec(53.9), kPlay, 0, 0.0f});
    cues.push_back({sec(62.0), kTempo, 0, 133.0f});
    cues.push_back({sec(72.0), kStop, 0, 0.0f});
    cues.push_back({sec(74.0), kWrite, iSync, 0.0f});
    cues.push_back({sec(79.0), kWrite, iSync, 1.0f});
    cues.push_back({sec(80.4), kPlay, 0, 0.0f});
    cues.push_back({sec(88.0), kWrite, iSync, 0.0f});
    for (size_t i = 1; i < cues.size(); ++i)   // (stable)
    {
        const Cue c = cues[i];
        size_t j = i;
        while (j > 0 && cues[j - 1].frame > c.frame) cues[j] = cues[j - 1], --j;
        cues[j] = c;
    }
}

// The framework's chunk, re-written over the base's parameters (see the top); false if it does not parse.
size_t lastExtra = 0;   // (the extra blob's size in the last chunk: "TP1", 0, then the FM-1's project)
bool normaliseState(const uint8_t* d, size_t size, int base, std::vector<uint8_t>& out)
{
    auto u32 = [&](size_t p) { uint32_t v = 0; for (int i = 0; i < 4; ++i) v |= (uint32_t)d[p + i] << (8 * i); return v; };
    auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back((uint8_t)(v >> (8 * i))); };
    if (size < 12 || std::memcmp(d, "MPCP", 4) != 0) return false;
    const uint32_t n = u32(8);
    std::vector<uint8_t> entries;
    int kept = 0;
    size_t p = 12;
    for (uint32_t k = 0; k < n; ++k)
    {
        if (p >= size) return false;
        const size_t len = 1 + d[p] + 8;
        if (p + len > size) return false;
        const bool link = d[p] == 4 && std::memcmp(d + p + 1, "link", 4) == 0;
        if (!link) entries.insert(entries.end(), d + p, d + p + len), ++kept;
        p += len;
    }
    if (kept != base) return false;
    if (p + 4 > size) return false;
    const uint32_t extra = u32(p);
    p += 4;
    if (p + extra != size) return false;
    lastExtra = extra;
    out.insert(out.end(), d, d + 8);   // "MPCP", version
    put32((uint32_t)base);
    out.insert(out.end(), entries.begin(), entries.end());
    put32(extra);
    out.insert(out.end(), d + p, d + size);
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) { std::fprintf(stderr, "usage: link_null <out.bin>\n"); return 2; }
    long midFrame = -1, midBlock = -1;   // (the first play mid-bar, or Sync on mid-bar while the MPC plays)
    const int iSyncParam = paramIndex("sync");

    std::memset(&ti, 0, sizeof ti);
    ti.sampleRate = kRate, ti.tempo = 120, ti.timeSigNumerator = 4, ti.timeSigDenominator = 4;
    ti.flags = kVstTempoValid | kVstPpqPosValid | kVstBarsValid | kVstTimeSigValid;

    AEffect* fx = VSTPluginMain(host);
    if (fx == nullptr) { std::fprintf(stderr, "link_null: no plugin\n"); return 2; }
    fx->dispatcher(fx, effOpen, 0, 0, nullptr, 0.0f);
    fx->dispatcher(fx, effSetSampleRate, 0, 0, nullptr, (float)kRate);
    fx->dispatcher(fx, effSetBlockSize, 0, kMaxBlock, nullptr, 0.0f);
    fx->dispatcher(fx, effMainsChanged, 0, 1, nullptr, 0.0f);

    // the base's parameter count: with Link compiled in, it is one more (Link, after Sync)
    findLink();
    const int base = fx->numParams - (gLink >= 0 ? 1 : 0);

    buildMidi();
    buildCues(base);

    std::vector<float> audio;
    audio.reserve((size_t)kTotal * 2);
    std::vector<uint8_t> state, saved;
    std::vector<uint32_t> blockSizes;   // (written beside the output, for the report: not compared)
    size_t stateMid = 0;
    float L[kMaxBlock], R[kMaxBlock];
    float* outs[2] = {L, R};
    // VstEvents with room for a block's events (declared with 2; hosts allocate it longer)
    constexpr int kMaxEv = 1000;
    std::vector<uint8_t> evMem(sizeof(VstEvents) + kMaxEv * sizeof(VstEvent*));
    VstEvents* vev = reinterpret_cast<VstEvents*>(evMem.data());
    std::vector<VstMidiEvent> mev(kMaxEv);
    size_t ei = 0, ci = 0;
    long maxEvents = 0;
    double peak = 0.0;

    for (long done = 0; done < kTotal;)
    {
        int n = 1 + pick(kMaxBlock);
        if (n > kTotal - done) n = (int)(kTotal - done);
        // the cues due by this block's start
        while (ci < cues.size() && cues[ci].frame <= done)
        {
            const Cue& c = cues[ci++];
            switch (c.act)
            {
                case kWrite:
                    if (c.index == iSyncParam && c.value >= 0.5f && (ti.flags & kVstTransportPlaying) &&
                        fx->getParameter(fx, c.index) < 0.5f)
                    {   // (Sync on while the MPC plays: the piece starts now, on the MPC's clock, as a play would)
                        const double bar = std::floor(ti.ppqPos / 4.0) * 4.0;
                        if (ti.ppqPos - bar > 0.01 && midFrame < 0) midFrame = done, midBlock = blockNo;
                    }
                    fx->setParameter(fx, c.index, c.value);
                    break;
                case kPlay:
                {
                    const double bar = std::floor(ti.ppqPos / 4.0) * 4.0;
                    if (ti.ppqPos - bar > 0.01 && midFrame < 0) midFrame = done, midBlock = blockNo;
                    ti.flags |= kVstTransportPlaying | kVstTransportChanged;
                    break;
                }
                case kStop: ti.flags = (ti.flags & ~kVstTransportPlaying) | kVstTransportChanged; break;
                case kTempo: ti.tempo = c.value; break;
                case kSave:
                {
                    void* p = nullptr;
                    const VstIntPtr len = fx->dispatcher(fx, effGetChunk, 0, 0, &p, 0.0f);
                    saved.assign((const uint8_t*)p, (const uint8_t*)p + (len > 0 ? len : 0));
                    if (!normaliseState(saved.data(), saved.size(), base, state))
                    { std::fprintf(stderr, "link_null: the mid-run chunk does not parse\n"); return 2; }
                    stateMid = state.size();
                    break;
                }
                case kLoad:
                    if ((ti.flags & kVstTransportPlaying) && fx->getParameter(fx, iSyncParam) >= 0.5f)
                    {   // (a load boots the FM-1: on the MPC's clock while it plays, the piece starts again, as a play)
                        const double bar = std::floor(ti.ppqPos / 4.0) * 4.0;
                        if (ti.ppqPos - bar > 0.01 && midFrame < 0) midFrame = done, midBlock = blockNo;
                    }
                    fx->dispatcher(fx, effSetChunk, 0, (VstIntPtr)saved.size(), saved.data(), 0.0f);
                    break;
            }
        }
        // this block's MIDI
        int k = 0;
        while (ei < evs.size() && evs[ei].frame < done + n && k < kMaxEv)
        {
            const Ev& e = evs[ei++];
            VstMidiEvent& m = mev[(size_t)k];
            std::memset(&m, 0, sizeof m);
            m.type = kVstMidiType, m.byteSize = (VstInt32)sizeof m, m.deltaFrames = (VstInt32)(e.frame - done);
            m.midiData[0] = (char)e.s, m.midiData[1] = (char)e.d1, m.midiData[2] = (char)e.d2;
            vev->events[k] = reinterpret_cast<VstEvent*>(&m);
            ++k;
        }
        if (k > maxEvents) maxEvents = k;
        if (k > 0)
        {
            vev->numEvents = k, vev->reserved = 0;
            fx->dispatcher(fx, effProcessEvents, 0, 0, vev, 0.0f);
        }
        fx->processReplacing(fx, nullptr, outs, n);
        for (int i = 0; i < n; ++i)
        {
            audio.push_back(L[i]), audio.push_back(R[i]);
            if (std::fabs(L[i]) > peak) peak = std::fabs(L[i]);
            if (std::fabs(R[i]) > peak) peak = std::fabs(R[i]);
        }
        blockSizes.push_back((uint32_t)n);
        ++blockNo;
        done += n;
        ti.flags &= ~kVstTransportChanged;
        ti.samplePos += n;
        if (ti.flags & kVstTransportPlaying)
        {
            ti.ppqPos += n * ti.tempo / 60.0 / kRate;
            ti.barStartPos = std::floor(ti.ppqPos / 4.0) * 4.0;
        }
    }
    if (ei != evs.size()) { std::fprintf(stderr, "link_null: %zu events left over\n", evs.size() - ei); return 2; }

    {
        void* p = nullptr;
        const VstIntPtr len = fx->dispatcher(fx, effGetChunk, 0, 0, &p, 0.0f);
        std::vector<uint8_t> fin((const uint8_t*)p, (const uint8_t*)p + (len > 0 ? len : 0));
        if (!normaliseState(fin.data(), fin.size(), base, state))
        { std::fprintf(stderr, "link_null: the final chunk does not parse\n"); return 2; }
    }
    const size_t tp1 = lastExtra;
    std::vector<uint8_t> notify;
    long filtered = 0;
    for (const Note& x : notes)
    {
        const int bi = x.index < 0 ? -1 : toBase(x.index);
        if (bi < 0 || bi >= base) { ++filtered; continue; }
        const Note y{x.block, (int32_t)bi, x.value};
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&y);
        notify.insert(notify.end(), b, b + sizeof y);
    }
    std::vector<float> values;
    for (int i = 0; i < base; ++i) values.push_back(fx->getParameter(fx, toBuild(i)));

    FILE* f = std::fopen(argv[1], "wb");
    if (f == nullptr) { std::fprintf(stderr, "link_null: cannot write %s\n", argv[1]); return 2; }
    std::fwrite(audio.data(), sizeof(float), audio.size(), f);
    std::fwrite(state.data(), 1, state.size(), f);
    std::fwrite(notify.data(), 1, notify.size(), f);
    std::fwrite(values.data(), sizeof(float), values.size(), f);
    std::fclose(f);
    const std::string side = std::string(argv[1]) + ".blocks";
    if (FILE* g = std::fopen(side.c_str(), "wb"))
    {
        std::fwrite(blockSizes.data(), sizeof(uint32_t), blockSizes.size(), g);
        std::fclose(g);
    }

    std::printf("midbar frame=%ld block=%ld\n", midFrame, midBlock);
    std::printf("base_controls %d\n", tp::kNumControls - (gLink >= 0 ? 1 : 0));
    std::printf("sections audio=%zu state=%zu notify=%zu values=%zu state_mid=%zu\n", audio.size() * sizeof(float),
                state.size(), notify.size(), values.size() * sizeof(float), stateMid);
    std::printf("facts blocks=%ld events=%zu max_events_per_block=%ld peak=%.4f params=%d base_params=%d "
                "notifications=%zu chunk_bytes=%zu tp1_bytes=%zu\n",
                blockNo, evs.size(), maxEvents, peak, fx->numParams, base, notes.size() - (size_t)filtered,
                saved.size(), tp1);
    std::fprintf(stderr, "filtered_notifications=%ld\n", filtered);
    fx->dispatcher(fx, effClose, 0, 0, nullptr, 0.0f);
    return 0;
}
