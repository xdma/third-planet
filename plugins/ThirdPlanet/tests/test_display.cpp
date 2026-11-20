// SPDX-License-Identifier: GPL-3.0-only
// Third Planet's status line (the display parameters appended after the controls):
//   1. the chord's number makes exactly the name the FM-1's own screen shows (ui.c chord_now), on every
//      piece, with the walk's characters, in Endless and in Loop;
//   2. the plugin sends the Pad, the chord, Hold and Lock as they change, from the pads and the switches;
//   3. a value the MPC writes over a status value is corrected on the next block;
//   4. a second instance shows that another one plays, and nothing else.
// And the star-chart screen's display parameters (d_section .. d_source) with their send scheduler:
//   5. each one follows the state it shows (the app's readouts, the Sky model), within its pace;
//   6. no block notifies the MPC of more than 24 parameters, and the controls still come back within a second;
//   7. at rest nothing is sent; a project load logs nothing; a second instance sends only its message;
//   8. every text fits the MPC's 31 bytes in the glyphs its fonts have; a value written over is corrected.
//   c++ -std=c++17 -O2 -I plugins/ThirdPlanet/src -I deps/mpc-plugin-sdk/framework \
//       plugins/ThirdPlanet/tests/test_display.cpp plugins/ThirdPlanet/src/app_unit.cpp \
//       plugins/ThirdPlanet/src/engine_unit.cpp plugins/ThirdPlanet/src/power.cpp plugins/ThirdPlanet/src/sky.cpp \
//       -o plugins/ThirdPlanet/build/tests/test_display
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "ThirdPlanet.cpp"

namespace {

int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); fails++; } } while (0)

struct Ev { long frame; int status, d1, d2; };

void runPlugin(ThirdPlanet& p, long frames, const std::vector<Ev>& evs = {})
{
    std::vector<float> l(512), r(512);
    float* outs[2] = {l.data(), r.data()};
    size_t e = 0;
    for (long done = 0; done < frames;)
    {
        int n = 128;
        if (n > frames - done) n = (int)(frames - done);
        uint8_t storage[sizeof(VstEvents) + 16 * sizeof(VstEvent*)];
        VstMidiEvent me[16];
        VstEvents* ve = reinterpret_cast<VstEvents*>(storage);
        ve->numEvents = 0;
        while (e < evs.size() && evs[e].frame < done + n && ve->numEvents < 16)
        {
            VstMidiEvent& m = me[ve->numEvents];
            std::memset(&m, 0, sizeof m);
            m.type = kVstMidiType, m.byteSize = sizeof m;
            m.deltaFrames = (VstInt32)(evs[e].frame > done ? evs[e].frame - done : 0);
            m.midiData[0] = (char)evs[e].status, m.midiData[1] = (char)evs[e].d1, m.midiData[2] = (char)evs[e].d2;
            ve->events[ve->numEvents++] = reinterpret_cast<VstEvent*>(&m);
            e++;
        }
        if (ve->numEvents) p.internalAddMidi(ve);
        p.internalProcess(nullptr, outs, n);
        done += n;
    }
}

// A host that notes which parameters the plugin notifies it of (audioMasterAutomate), block by block.
std::vector<int> notified;
VstIntPtr hostCb(AEffect*, VstInt32 opcode, VstInt32 index, VstIntPtr, void*, float)
{
    if (opcode == audioMasterAutomate) notified.push_back(index);
    return 0;
}
AEffect fakeFx;

std::unique_ptr<ThirdPlanet> make()
{
    auto p = std::make_unique<ThirdPlanet>();
    p->effect_ = &fakeFx;
    p->host_ = hostCb;
    p->internalSetSampleRate(44100.0);
    p->internalSetBlockSize(512);
    p->internalResume();
    return p;
}

std::string text(ThirdPlanet& p, int i)
{
    std::string s;
    if (p.formatValue(i, p.get(i), s)) return s;
    return p.param(i).choices[(size_t)p.getInt(i)];
}

int idx(const char* id)
{
    for (int i = 0; i < tp::kNumControls; ++i)
        if (!std::strcmp(tp::kParams[i].id, id)) return i;
    std::printf("no parameter %s\n", id);
    std::exit(2);
}

void chordNames()
{
    using namespace tp::app;
    int32_t buf[2 * tp::kChunk];
    long compared = 0;
    std::set<std::string> names;
    std::set<int> labelCodes;
    boot();
    for (int loop = 0; loop < 2; ++loop)
        for (int piece = 0; piece < EN_NPIECES; ++piece)
            for (int w = 0; w < EN_NWALK; w += 2)
            {
                setPiece(piece);
                knob(kKnobLoop, loop);
                walk(w);
                for (int ms = 0; ms < 40000; ++ms)
                {
                    if (!stepMs(buf)) continue;
                    char a[kChordTextMax], b[64];
                    const int code = chordCode();
                    chordText(code, a);
                    chordNow(b);
                    CHECK(code > 0 && code < kChordCodes, "code %d out of range", code);
                    if (std::strcmp(a, b) != 0)
                    {
                        CHECK(false, "piece %d walk %d loop %d: status '%s', FM-1 '%s' (code %d)", piece, w, loop, a, b, code);
                        return;
                    }
                    ++compared;
                    names.insert(a);
                    if (code >= kChordLabelBase) labelCodes.insert(code);
                }
            }
    std::printf("chords: %ld blocks compared, %zu names, %zu from written slots\n", compared, names.size(), labelCodes.size());
    CHECK(names.size() >= 12, "only %zu chord names seen", names.size());
    CHECK(!labelCodes.empty(), "no chord from a written slot was seen");
    char z[kChordTextMax];
    chordText(0, z);
    CHECK(z[0] == 0, "code 0 is not blank");
    chordText(kChordCodes, z);
    CHECK(z[0] == 0, "a code out of range is not blank");
    chordText(-5, z);
    CHECK(z[0] == 0, "a negative code is not blank");
}

// Every chord number survives the MPC's normalised value: the screen shows that one, and the
// write-over check compares against it.
void chordValues()
{
    auto p = make();
    const mpc::Param& d = p->param(kDispChord);
    int bad = 0;
    for (int c = 0; c < tp::app::kChordCodes; ++c)
        if ((int)std::lround(d.toPlain(d.toNormalised((float)c))) != c && bad++ < 3)
            CHECK(false, "chord code %d does not survive its normalised value", c);
    CHECK(bad == 0, "%d chord codes lost", bad);
}

void statusLine()
{
    auto p = make();
    runPlugin(*p, 44100);
    CHECK(text(*p, kDispPad) == " ", "Pad shows '%s' before any pad", text(*p, kDispPad).c_str());
    char now[64];
    tp::app::chordNow(now);
    CHECK(text(*p, kDispChord) == now, "chord shows '%s', the FM-1 '%s'", text(*p, kDispChord).c_str(), now);
    CHECK(text(*p, kDispHold) == " " && text(*p, kDispLock) == " ", "Hold or Lock shown at power-on");

    runPlugin(*p, 44100, {{4410, 0x90, 55, 100}, {8820, 0x80, 55, 0}});   // the Shimmer pad
    CHECK(text(*p, kDispPad) == "Shimmer", "Pad shows '%s' after the Shimmer pad", text(*p, kDispPad).c_str());
    p->set(idx("hold"), 1.0f), p->set(idx("lock"), 1.0f);
    runPlugin(*p, 4410);
    CHECK(text(*p, kDispHold) == "HOLD" && text(*p, kDispLock) == "LOCK", "Hold '%s' Lock '%s' after both on",
          text(*p, kDispHold).c_str(), text(*p, kDispLock).c_str());
    p->set(idx("hold"), 0.0f), p->set(idx("lock"), 0.0f);
    runPlugin(*p, 4410);
    CHECK(text(*p, kDispHold) == " " && text(*p, kDispLock) == " ", "Hold '%s' Lock '%s' after both off",
          text(*p, kDispHold).c_str(), text(*p, kDispLock).c_str());

    // The MPC writes over a status value (a load, automation): the true one comes back.
    const int pad = p->getInt(kDispPad);
    p->setNormalised(kDispPad, 0.0f);
    p->setNormalised(kDispHold, 1.0f);
    runPlugin(*p, 4410 + 256);
    CHECK(p->getInt(kDispPad) == pad && p->getInt(kDispHold) == 0, "status not corrected: pad %d hold %d",
          p->getInt(kDispPad), p->getInt(kDispHold));

    // A second instance: the message, nothing else.
    auto q = make();
    runPlugin(*q, 4410);
    CHECK(text(*q, kDispPad) == kBusyText, "second instance's Pad shows '%s'", text(*q, kDispPad).c_str());
    CHECK(text(*q, kDispChord) == " " && text(*q, kDispHold) == " " && text(*q, kDispLock) == " ",
          "second instance shows a status");
    p.reset();   // the owner removed: the second takes over and shows its own status
    runPlugin(*q, 44100);
    tp::app::chordNow(now);
    CHECK(text(*q, kDispPad) == " " && text(*q, kDispChord) == now, "after taking over, Pad '%s' chord '%s'",
          text(*q, kDispPad).c_str(), text(*q, kDispChord).c_str());
}

// ---- the star-chart screen's display parameters ----

// The display parameters in the order the skin's generator expects them after the controls.
std::vector<std::string> displayIds()
{
    std::vector<std::string> ids = {"d_pad", "d_chord", "d_hold", "d_lock", "d_section", "d_phrase", "d_evolve",
                                    "d_next", "d_next_in", "d_prog", "d_ring"};
    auto many = [&](const char* stem, int n) {
        for (int i = 1; i <= n; ++i) ids.push_back(stem + std::to_string(i));
    };
    many("d_slot", 8), many("d_slotname", 8), many("d_px", 8), many("d_py", 8), many("d_planet", 5), many("d_log", 3),
        many("d_drift", 5);
    ids.insert(ids.end(), {"d_density", "d_step", "d_ext", "d_source"});
    return ids;
}
enum { xPad, xChord, xHold, xLock, xSection, xPhrase, xEvolve, xNext, xNextIn, xProg, xRing, xSlot, xSlotName = xSlot + 8,
       xPx = xSlotName + 8, xPy = xPx + 8, xPlanet = xPy + 8, xLog = xPlanet + 5, xDrift = xLog + 3, xDensity = xDrift + 5,
       xStep, xExt, xSource, xCount };
constexpr int kD0 = tp::kNumControls;   // the first display parameter's index

int did(ThirdPlanet& p, const std::string& id)
{
    for (int i = 0; i < p.numParams(); ++i)
        if (p.param(i).id == id) return i;
    return -1;
}

void runBlocks(ThirdPlanet& p, long frames, int bs, const std::function<void(long, int)>& after = nullptr)
{
    std::vector<float> l(1024), r(1024);
    float* outs[2] = {l.data(), r.data()};
    for (long done = 0; done < frames;)
    {
        const int n = (int)(frames - done < bs ? frames - done : bs);
        notified.clear();
        p.internalProcess(nullptr, outs, n);
        done += n;
        if (after) after(done, n);
    }
}

// What each display parameter must show, from the app's readouts and the plugin's Sky as they are now.
void expected(ThirdPlanet& p, double* w)
{
    using namespace tp::app;
    const tp::sky::Sky& sky = p.sky();
    w[xPad] = p.get(kD0 + xPad);   // (the last pad: statusLine checks it)
    w[xChord] = chordCode(), w[xHold] = held(), w[xLock] = locked();
    w[xSection] = section() + 1;
    w[xPhrase] = phraseBar() + 1;
    w[xEvolve] = evolveChance();
    w[xNext] = nextChordCode();
    w[xNextIn] = barsToNextChord() > 64 ? 64 : barsToNextChord();
    if (!loopMode()) w[xProg] = 1 + walkCharacter() % 3;
    else w[xProg] = progPos() < 0 || progCount() < 1 ? 0 : 4 + progPos() * 8 + progCount() - 1;
    bool last = false;
    const int dots = ringDots(&last);
    w[xRing] = dots < 0 ? 0 : held() ? 67 + dots : last ? 34 + dots : 1 + dots;
    for (int i = 0; i < 8; ++i)
    {
        w[xSlot + i] = i == progPos() ? 3 : (int)slotState(i);
        w[xSlotName + i] = slotChordCode(i);
        w[xPx + i] = sky.puck(i).x, w[xPy + i] = sky.puck(i).y;
    }
    for (int l = 0; l < 5; ++l)
    {
        w[xPlanet + l] = sky.planetFrame(l);
        float d = filterDrift(l);
        d = d < -4.5f ? -4.5f : d > 4.5f ? 4.5f : d;
        w[xDrift + l] = std::lround(d * 10.0f);
    }
    for (int n = 0; n < 3; ++n) w[xLog + n] = sky.logSerial(n) % 1024;
    w[xDensity] = densityShift();
    const int lane = knobValue(kKnobLane);
    w[xStep] = playing() ? loopStep(lane == 1 || lane == 3 ? 1 : 0) : 16;
    w[xExt] = external();
    w[xSource] = 0;
}
bool xIsPuck(int k) { return k >= xPx && k < xPlanet; }
bool xIsPaced(int k) { return (k >= xPlanet && k < xLog) || (k >= xDrift && k <= xDensity); }   // (10 a second at most)
bool same(int k, double have, double want) { return xIsPuck(k) ? std::fabs(have - want) <= 0.0026 : have == want; }

// How long each display parameter has not shown its state, in frames, kept block by block.
struct Lag
{
    long now[xCount] = {}, worst[xCount] = {};
    std::set<long> seen[xCount];
    void block(ThirdPlanet& p, int n, bool count)
    {
        double w[xCount];
        expected(p, w);
        for (int k = 0; k < xCount; ++k)
        {
            const double have = p.get(kD0 + k);
            now[k] = same(k, have, w[k]) ? 0 : now[k] + n;
            if (count && now[k] > worst[k]) worst[k] = now[k];
            seen[k].insert(std::lround(have * 1000.0));
        }
    }
    // The longest a parameter may lag: a block or two for the budget; the paced ones a tenth of a second more; a
    // puck its half's turn (50 ms) as well.
    static long limit(int k, int bs) { return 3L * bs + (xIsPuck(k) ? 4410 + 2205 : xIsPaced(k) ? 4410 : 0); }
    void check(int bs, const char* what)
    {
        const std::vector<std::string> ids = displayIds();
        for (int k = 0; k < xCount; ++k)
            CHECK(worst[k] <= limit(k, bs), "%s: %s lagged its state by %ld frames (limit %ld)", what, ids[(size_t)k].c_str(),
                  worst[k], limit(k, bs));
    }
};

std::string upper(const char* s)
{
    std::string u(s);
    for (char& c : u) c = (char)std::toupper((unsigned char)c);
    return u;
}

void displayOrder()
{
    auto p = make();
    const std::vector<std::string> ids = displayIds();
    CHECK(p->numParams() == kD0 + xCount, "%d parameters, expected %d", p->numParams(), kD0 + xCount);
    for (int k = 0; k < xCount; ++k)
    {
        CHECK(did(*p, ids[(size_t)k]) == kD0 + k, "%s is parameter %d, expected %d", ids[(size_t)k].c_str(),
              did(*p, ids[(size_t)k]), kD0 + k);
        if (did(*p, ids[(size_t)k]) >= 0) CHECK(!p->param(did(*p, ids[(size_t)k])).automatable, "%s is automatable", ids[(size_t)k].c_str());
    }
}

// display_mirrors_state: 30 s of play with the hand's actions in it; every d_* follows what Tasks 1-3 give.
void displayMirrorsState()
{
    auto p = make();
    if (p->numParams() != kD0 + xCount) { CHECK(false, "display_mirrors_state: the display parameters are missing"); return; }
    Lag lag;
    const int bs = 128;
    auto at = [](long done, double secs) { return done >= (long)(secs * 44100) && done < (long)(secs * 44100) + 128; };
    std::string nudgeLine;
    runBlocks(*p, 30 * 44100L, bs, [&](long done, int n) {
        lag.block(*p, n, done > 44100);
        if (at(done, 5.0)) p->set(idx("nudge_seq"), 1.0f);
        if (at(done, 5.5)) nudgeLine = text(*p, kD0 + xLog);
        if (at(done, 9.0)) p->set(idx("hold"), 1.0f);
        if (at(done, 11.0))
        {
            CHECK(p->getInt(kD0 + xRing) >= 67, "held: the ring shows %d, not a held frame", p->getInt(kD0 + xRing));
            CHECK(text(*p, kD0 + xNextIn) == "ON HOLD", "held: NEXT shows '%s'", text(*p, kD0 + xNextIn).c_str());
            p->set(idx("hold"), 0.0f);
        }
        if (at(done, 14.0)) p->set(idx("chord_play"), 1.0f);   // Loop
        if (at(done, 17.0))
        {
            char t[32];
            std::snprintf(t, sizeof t, "LOOP %d/%d", tp::app::progPos() + 1, tp::app::progCount());
            CHECK(text(*p, kD0 + xProg) == t, "Loop: the progression shows '%s', expected '%s'", text(*p, kD0 + xProg).c_str(), t);
        }
        if (at(done, 20.0)) p->set(idx("mute_seq"), 1.0f);
        if (at(done, 22.0))
            CHECK(p->getInt(kD0 + xPlanet + 1) % 3 == 1, "the muted Seq's planet shows frame %d", p->getInt(kD0 + xPlanet + 1));
        if (at(done, 23.0)) p->set(idx("mute_seq"), 0.0f);
        if (done % (128 * 64) == 0)   // the texts, now and then
        {
            for (int l = 0; l < 3; ++l)
                if (p->getInt(kD0 + xLog + l) == p->sky().logSerial(l) % 1024)
                {
                    const char* t = p->sky().logText(l);
                    CHECK(text(*p, kD0 + xLog + l) == (t[0] ? t : " "), "log line %d shows '%s', the Sky '%s'", l + 1,
                          text(*p, kD0 + xLog + l).c_str(), t);
                }
            if (p->getInt(kD0 + xSection) == tp::app::section() + 1)
                CHECK(text(*p, kD0 + xSection) == upper(EN_SEC_NAME[tp::app::section()]), "section shows '%s'",
                      text(*p, kD0 + xSection).c_str());
            char c[tp::app::kChordTextMax];
            tp::app::chordText(p->getInt(kD0 + xNext), c);
            CHECK(text(*p, kD0 + xNext) == (c[0] ? c : " "), "next chord shows '%s', its code names '%s'",
                  text(*p, kD0 + xNext).c_str(), c);
        }
    });
    lag.check(bs, "display_mirrors_state");
    CHECK(nudgeLine.find("Nudge Seq") != std::string::npos, "after Nudge Seq the newest log line is '%s'", nudgeLine.c_str());
    // (the run was not an empty one: the things that move did)
    CHECK(lag.seen[xRing].size() >= 10, "the ring took only %zu values", lag.seen[xRing].size());
    CHECK(lag.seen[xStep].size() >= 8, "the step took only %zu values", lag.seen[xStep].size());
    CHECK(lag.seen[xPhrase].size() >= 4, "the phrase took only %zu values", lag.seen[xPhrase].size());
    CHECK(lag.seen[xPx].size() >= 20 && lag.seen[xPy + 4].size() >= 20, "the pucks hardly moved (%zu, %zu values)",
          lag.seen[xPx].size(), lag.seen[xPy + 4].size());
    CHECK(lag.seen[xPlanet + 1].size() >= 3, "the Seq's planet took only %zu frames", lag.seen[xPlanet + 1].size());
    CHECK(lag.seen[xLog].size() >= 4, "the log's first line took only %zu values", lag.seen[xLog].size());
    CHECK(lag.seen[xSlotName + 7].size() >= 2, "the last slot's name never changed (Endless)");
}

// The control's value as mirror() sends it back (the choice's index), or false for the plugin's own controls.
bool controlNow(ThirdPlanet& p, int i, int& v)
{
    using namespace tp;
    const ParamDef& d = kParams[i];
    switch (d.kind)
    {
        case Kind::Knob: v = app::knobValue(d.code); break;
        case Kind::Voice: v = app::par(d.code); break;
        case Kind::Piece: v = app::piece(); break;
        case Kind::Walk: v = app::walkValue(); break;
        case Kind::Switch:
            v = d.code == kSwHold ? app::held() : d.code == kSwLock ? app::locked() : d.code == kSwPlay ? app::playing()
                : d.code < kSwSolo0 ? app::muted(d.code - kSwMute0) : app::soloed(d.code - kSwSolo0);
            break;
        case Kind::Action: v = 0; break;
        case Kind::Step: v = app::step(d.code) ? 1 : 0; break;
        case Kind::Accent: v = app::accent(d.code) ? 1 : 0; break;
        case Kind::ChordRoot: v = app::chordRoot(); break;
        default: return false;
    }
    if (p.param(i).kind == mpc::Param::Kind::Choice) v -= rangeLo(d);
    return true;
}

// budget_per_block: 5 min of busy play at three block sizes. No block notifies more than 24 parameters, the
// display keeps its pace and every control the app changed is back on the MPC within a second.
void budgetPerBlock()
{
    for (int bs : {32, 128, 512})
    {
        auto p = make();
        if (p->numParams() != kD0 + xCount) { CHECK(false, "budget_per_block: the display parameters are missing"); return; }
        unsigned rng = 0xBEEF1u * (unsigned)bs;
        auto rnd = [&](unsigned n) { rng = rng * 1664525u + 1013904223u; return (rng >> 8) % n; };
        Lag lag;
        std::vector<long> stale((size_t)tp::kNumControls, 0);
        std::vector<long> sentAt((size_t)xCount, -1000000);
        long soonest = 1000000, bothHalves = 0;
        std::string soonestId;
        long worstCtl = 0, blocks = 0, total = 0, shown = 0;
        int most = 0, worstI = -1;
        long nextShake = 7 * 44100L, nextPiece = 30 * 44100L, nextKnob = 44100;
        runBlocks(*p, 300 * 44100L, bs, [&](long done, int n) {
            const int count = (int)notified.size();
            if (count > most) most = count;
            ++blocks, total += count;
            for (int i : notified) shown += i >= kD0;
            bool half[2] = {false, false};
            for (int i : notified)
            {
                const int k = i - kD0;
                if (k < 0 || !(xIsPuck(k) || xIsPaced(k))) continue;
                if (xIsPuck(k)) half[(k - xPx) % 8 / 4] = true;
                if (done - sentAt[(size_t)k] < soonest) soonest = done - sentAt[(size_t)k], soonestId = p->param(i).id;
                sentAt[(size_t)k] = done;
            }
            bothHalves += half[0] && half[1];
            lag.block(*p, n, done > 44100);
            for (int i = 0; i < tp::kNumControls; ++i)
            {
                int v;
                if (!controlNow(*p, i, v)) continue;
                stale[(size_t)i] = p->getInt(i) == v ? 0 : stale[(size_t)i] + n;
                if (stale[(size_t)i] > worstCtl) worstCtl = stale[(size_t)i], worstI = i;
            }
            if (done >= nextShake) nextShake += 7 * 44100L, p->set(idx("shake_all"), 1.0f);
            if (done >= nextPiece) nextPiece += 30 * 44100L, p->set(idx("piece"), (float)rnd(EN_NPIECES));
            if (done >= nextKnob)
            {
                nextKnob += 4410 + (long)rnd(22050);
                const int i = 1 + (int)rnd((unsigned)tp::kNumControls - 1);
                const tp::Kind k = tp::kParams[i].kind;
                if (k == tp::Kind::Knob || k == tp::Kind::Voice) p->setNormalised(i, (float)rnd(1001) / 1000.0f);
            }
        });
        std::printf("budget, blocks of %d: at most %d a block, %.2f a block on average (%.0f%% display), controls back in %.0f ms\n",
                    bs, most, (double)total / (double)blocks, total ? 100.0 * (double)shown / (double)total : 0.0,
                    (double)worstCtl / 44.1);
        CHECK(most <= 24, "blocks of %d: %d notifications in one block", bs, most);
        CHECK(worstCtl <= 44100, "blocks of %d: control %s was not sent back for %ld frames", bs,
              worstI >= 0 ? tp::kParams[worstI].id : "?", worstCtl);
        // (the pace: a particle, a planet, a drift 10 times a second at most; the particles a half at a time)
        CHECK(soonest >= 4410, "blocks of %d: %s was sent again after %ld frames", bs, soonestId.c_str(), soonest);
        CHECK(bothHalves == 0, "blocks of %d: %ld blocks sent particles of both halves", bs, bothHalves);
        char what[48];
        std::snprintf(what, sizeof what, "budget_per_block (%d)", bs);
        lag.check(bs, what);
        std::string still;   // (what this run never moved is checked by its texts and range only)
        const std::vector<std::string> ids = displayIds();
        for (int k = 0; k < xCount; ++k)
            if (lag.seen[k].size() < 2) still += " " + ids[(size_t)k];
        std::printf("  never changed:%s\n", still.empty() ? " none" : still.c_str());
    }
}

// rest_sends_nothing: Play off; once the particles alive have faded (their life, about 3 s: ruling R13) and the
// layers have died away, no display parameter is sent.
void restSendsNothing()
{
    auto p = make();
    if (p->numParams() != kD0 + xCount) { CHECK(false, "rest_sends_nothing: the display parameters are missing"); return; }
    runBlocks(*p, 5 * 44100L, 128);
    p->set(idx("play"), 0.0f);
    long lastSend = 0, late = 0;
    std::string lateId;
    runBlocks(*p, 10 * 44100L, 128, [&](long done, int) {
        for (int i : notified)
            if (i >= kD0)
            {
                lastSend = done;
                if (done > 4 * 44100L) ++late, lateId = p->param(i).id;
            }
    });
    std::printf("rest: the last display send %.2f s after Stop\n", (double)lastSend / 44100.0);
    CHECK(!tp::app::playing(), "rest_sends_nothing: still playing");
    CHECK(late == 0, "at rest %ld display sends after 4 s (the last: %s)", late, lateId.c_str());
    CHECK(p->getInt(kD0 + xStep) == 16, "at rest the step shows %d", p->getInt(kD0 + xStep));
    CHECK(text(*p, kD0 + xEvolve) == " ", "at rest Evolve shows '%s'", text(*p, kD0 + xEvolve).c_str());
    for (int i = 0; i < 8; ++i)
        CHECK(std::fabs(p->get(kD0 + xPx + i) - 0.5f) < 0.003f && std::fabs(p->get(kD0 + xPy + i) - 0.5f) < 0.003f,
              "at rest puck %d is not parked", i + 1);
}

// load_and_busy: a project load logs nothing (the switches it restores are not the hand's); a second instance
// sends its message and nothing else.
void loadAndBusy()
{
    auto p = make();
    if (p->numParams() != kD0 + xCount) { CHECK(false, "load_and_busy: the display parameters are missing"); return; }
    runBlocks(*p, 2 * 44100L, 128);
    p->set(idx("hold"), 1.0f), p->set(idx("lock"), 1.0f), p->set(idx("mute_seq"), 1.0f), p->set(idx("solo_lead"), 1.0f);
    runBlocks(*p, 22050, 128);
    p->set(idx("nudge_lead"), 1.0f);
    runBlocks(*p, 22050, 128);
    for (int l = 0; l < 3; ++l)
        CHECK(text(*p, kD0 + xLog + l) != " ", "before the load, log line %d is empty", l + 1);
    const std::vector<uint8_t> saved = p->internalSaveState();
    CHECK(p->internalLoadState(saved.data(), saved.size()), "the project did not load");
    std::vector<std::string> logged;
    runBlocks(*p, 44100, 128, [&](long, int) {
        for (int i : notified)
            if (i >= kD0 + xLog && i < kD0 + xLog + 3) logged.push_back(text(*p, i));
    });
    CHECK(tp::app::held() && tp::app::locked() && tp::app::muted(1) && tp::app::soloed(2), "the load did not restore the switches");
    for (int l = 0; l < 3; ++l)
        CHECK(text(*p, kD0 + xLog + l) == " ", "after the load, log line %d shows '%s'", l + 1, text(*p, kD0 + xLog + l).c_str());
    CHECK(!logged.empty(), "the load did not send the cleared log lines");
    for (const std::string& t : logged)
        CHECK(t == " ", "the load logged '%s'", t.c_str());
    CHECK(text(*p, kD0 + xEvolve) == " ", "locked and held, Evolve shows '%s'", text(*p, kD0 + xEvolve).c_str());

    auto q = make();
    std::vector<int> sent;
    runBlocks(*q, 22050, 128, [&](long, int) { sent.insert(sent.end(), notified.begin(), notified.end()); });
    CHECK(sent.size() == 1 && sent[0] == kDispPad, "a second instance sent %zu parameters (the first: %d)", sent.size(),
          sent.empty() ? -1 : sent[0]);
    CHECK(text(*q, kDispPad) == kBusyText, "second instance's Pad shows '%s'", text(*q, kDispPad).c_str());
    // (one that loaded a project's display values shows none of them)
    q->internalLoadState(saved.data(), saved.size());
    runBlocks(*q, 22050, 128);
    const std::vector<std::string> ids = displayIds();
    auto fresh = make();
    for (int k = 1; k < xCount; ++k)
        CHECK(q->get(kD0 + k) == fresh->get(kD0 + k), "a second instance with a project shows %s = %g", ids[(size_t)k].c_str(),
              (double)q->get(kD0 + k));
    CHECK(text(*q, kDispPad) == kBusyText, "second instance's Pad shows '%s' after a load", text(*q, kDispPad).c_str());
}

// A text the MPC shows whole: at most 31 bytes of well-formed UTF-8, in ASCII and the glyphs its fonts have.
bool fits(const std::string& s)
{
    static const char* const kGlyphs[] = {"\xE2\x88\x92", "\xC2\xB1", "\xC2\xB7", "\xE2\x80\xBA", "\xC2\xBB", "\xE2\x80\xA2"};   // − ± · › » •
    if (s.empty() || s.size() > 31) return false;
    for (size_t i = 0; i < s.size();)
    {
        const unsigned char c = (unsigned char)s[i];
        if (c >= 0x20 && c < 0x7F) { ++i; continue; }
        bool ok = false;
        for (const char* g : kGlyphs)
            if (s.compare(i, std::strlen(g), g) == 0) { i += std::strlen(g), ok = true; break; }
        if (!ok) return false;
    }
    return true;
}

// texts_fit: every d_* at every value of its range (the log: every serial, after a long busy play).
void textsFit()
{
    auto p = make();
    if (p->numParams() != kD0 + xCount) { CHECK(false, "texts_fit: the display parameters are missing"); return; }
    long nextAct = 22050;
    int act = 0;
    static const char* const kActs[] = {"shake_lead", "nudge_seq2", "hold", "shake_all", "reset_piece", "shake_atmos",
                                        "mute_seq2", "shake_space", "solo_atmos", "lock", "forget", "shake_seq2"};
    long checked = 0, bad = 0;
    auto check = [&](int i, float plain) {
        std::string s;
        if (!p->formatValue(i, plain, s)) s = p->param(i).display(plain);
        ++checked;
        if (!fits(s) && bad++ < 5) CHECK(false, "%s at %g shows '%s' (%zu bytes)", p->param(i).id.c_str(), (double)plain, s.c_str(), s.size());
    };
    runBlocks(*p, 60 * 44100L, 256, [&](long done, int) {
        if (done >= nextAct)
        {
            nextAct += 22050;
            const int i = idx(kActs[act++ % 12]);
            p->set(i, tp::kParams[i].kind == tp::Kind::Action ? 1.0f : p->getBool(i) ? 0.0f : 1.0f);
            if (act % 24 == 0) p->set(idx("piece"), (float)((act / 24) % EN_NPIECES));
        }
        for (int l = 0; l < 3; ++l) check(kD0 + xLog + l, p->get(kD0 + xLog + l));
    });
    for (int k = 0; k < xCount; ++k)
    {
        const mpc::Param& d = p->param(kD0 + k);
        if (xIsPuck(k))
            for (int v = 0; v <= 1000; ++v) check(kD0 + k, (float)v / 1000.0f);
        else
            for (int v = (int)d.min; v <= (int)d.max; ++v) check(kD0 + k, (float)v);
    }
    CHECK(bad == 0, "%ld of %ld texts do not fit", bad, checked);

    // the texts themselves
    auto shows = [&](int k, int v, const char* want) {
        std::string s;
        if (!p->formatValue(kD0 + k, (float)v, s)) s = p->param(kD0 + k).display((float)v);
        CHECK(s == want, "%s at %d shows '%s', expected '%s'", p->param(kD0 + k).id.c_str(), v, s.c_str(), want);
    };
    shows(xSection, 0, " "), shows(xSection, 1, "EMERGE"), shows(xSection, 2, "FLOW"), shows(xSection, 3, "SOLO"), shows(xSection, 4, "BRIDGE");
    shows(xEvolve, 0, " "), shows(xEvolve, 20, "EVOLVE 20%"), shows(xEvolve, 100, "EVOLVE 100%");
    shows(xNext, 0, " ");
    shows(xNextIn, -2, "ON HOLD"), shows(xNextIn, -1, " "), shows(xNextIn, 0, "ON THE BAR"), shows(xNextIn, 1, "ON THE BAR");
    shows(xNextIn, 2, "IN 2 BARS"), shows(xNextIn, 64, "IN 64 BARS");
    shows(xProg, 0, " "), shows(xProg, 1, "ENDLESS \xC2\xB7 HOME"), shows(xProg, 2, "ENDLESS \xC2\xB7 WANDER");
    shows(xProg, 3, "ENDLESS \xC2\xB7 FREE"), shows(xProg, 4 + 2 * 8 + 4, "LOOP 3/5"), shows(xProg, 67, "LOOP 8/8"), shows(xProg, 4, "LOOP 1/1");
    shows(xSlotName + 3, 0, " ");
    shows(xDrift, 0, " "), shows(xDrift, -12, "MOVING DARKER \xE2\x88\x92" "1.2 OCT"), shows(xDrift + 4, 7, "MOVING BRIGHTER +0.7 OCT");
    shows(xDrift + 2, -45, "MOVING DARKER \xE2\x88\x92" "4.5 OCT");
    shows(xDensity, 0, " "), shows(xDensity, 12, "MOVING DENSER +12"), shows(xDensity, -100, "MOVING SPARSER \xE2\x88\x92" "100");
    shows(xExt, 0, " "), shows(xExt, 1, "EXT"), shows(xSource, 0, " "), shows(xSource, 1, "PERFORMER");
    const mpc::Param& in = p->param(kD0 + xNextIn);
    CHECK(in.min == -2.0f && in.max == 64.0f, "d_next_in runs %g..%g", (double)in.min, (double)in.max);
    CHECK(p->param(kD0 + xProg).max == 67.0f && p->param(kD0 + xRing).max == 99.0f && p->param(kD0 + xLog).max == 1023.0f &&
              p->param(kD0 + xPlanet).max == 23.0f && p->param(kD0 + xStep).max == 16.0f && p->param(kD0 + xPhrase).max == 8.0f &&
              p->param(kD0 + xSlot).max == 3.0f && p->param(kD0 + xDrift).min == -45.0f && p->param(kD0 + xDensity).min == -100.0f,
          "a display parameter's range is not the skin's");
}

// overwrite_corrected: the MPC writes over every d_* (a load, automation); each true value comes back.
void overwriteCorrected()
{
    auto p = make();
    if (p->numParams() != kD0 + xCount) { CHECK(false, "overwrite_corrected: the display parameters are missing"); return; }
    runBlocks(*p, 3 * 44100L, 128);
    p->set(idx("play"), 0.0f);
    runBlocks(*p, 6 * 44100L, 128);   // at rest: what is shown stands still
    std::vector<float> was;
    for (int k = 0; k < xCount; ++k) was.push_back(p->get(kD0 + k));
    for (int k = 0; k < xCount; ++k) p->setNormalised(kD0 + k, p->getNormalised(kD0 + k) < 0.5f ? 1.0f : 0.0f);
    int most = 0;
    runBlocks(*p, 22050, 128, [&](long, int) { most = (int)notified.size() > most ? (int)notified.size() : most; });
    const std::vector<std::string> ids = displayIds();
    for (int k = 0; k < xCount; ++k)
        CHECK(std::fabs(p->get(kD0 + k) - was[(size_t)k]) < 0.003f, "%s written over: shows %g, was %g", ids[(size_t)k].c_str(),
              (double)p->get(kD0 + k), (double)was[(size_t)k]);
    CHECK(most <= 24, "the corrections took %d notifications in one block", most);
}

} // namespace

int main()
{
    chordNames();
    chordValues();
    statusLine();
    displayOrder();
    displayMirrorsState();
    budgetPerBlock();
    restSendsNothing();
    loadAndBusy();
    textsFit();
    overwriteCorrected();
    if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
    std::printf("test_display: all passed\n");
    return 0;
}
