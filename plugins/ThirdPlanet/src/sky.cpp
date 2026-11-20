// SPDX-License-Identifier: GPL-3.0-only
// Third Planet: the star-chart screen's model (sky.h). The particles follow the FM-1's orbits (ui.c orb_add,
// orbits_take, draw_orbits); nothing here draws. No heap, no locks, no I/O: it runs on the audio thread.
#include "sky.h"

#include <cmath>

extern "C" {
#include "nachtfahrt/dsp/amb.h"
}

namespace tp {
namespace sky {

using tp::app::Event;

namespace {

constexpr int kStepMs = 16;                  // ui.c's frame: the orbits move once a frame
constexpr float kSpin[2] = {0.06f, 0.045f};  // radians a step: the Sequence's (outer), Sequence 2's (inner)
constexpr float kFade = 0.016f;              // life lost a step
constexpr int kLifeSteps = 63;               // a hit's whole life (1 / 0.016, rounded up): any more steps park it
constexpr double kTwoPi = 6.283185307179586;
constexpr int kTextLen = kTextMax - 1;

// The log's short parameter names, without the layer (the line names it): "Seq Cutoff", "Atmos Tone". The ones a
// nudge moves (ui.c NUDGE_PAR) are 7 bytes at most, so "Evolve › Seq 2 <name> −100" fits whole.
const char* const kShort[P_NPARAMS] = {
    "Level", "Level", "Level", "Level",                       // P_CHORD .. P_SEQ2
    "Density", "Evolve", "Drift", "Repeat A",                 // P_DENSITY .. P_TAPA
    "Echo", "Time", "Reverb", "Fdbk",                         // P_ECHO .. P_FEEDBACK
    "Cutoff", "Decay", "Sweep", "Motion",                     // P_CUTOFF .. P_MOTION
    "Tune", "MIDI out", "Tempo", "Root", "Mode", "Octave",    // P_TUNE .. P_SEQOCT
    "Voice",                                                  // P_CVOICE
    "Octave", "Grid", "Transp",                               // P_SEQ2OCT .. P_TRANS
    "A time", "Repeat B",                                     // P_TAPAT, P_TAPB
    "Length", "Level", "B time", "Size",                      // P_SEQLEN .. P_SIZE
    "Cloud",                                                  // P_CLOUD
    "Voice", "Voice", "Voice",                                // P_SVOICE .. P_S2VOICE
    "Attack", "Colour", "Reso", "Release",                    // P_CATT .. P_DREL
    "Pedal", "Reverb", "Echo", "Reverb",                      // P_PEDAL .. P_LDREV
    "Octave", "Vibrato", "Echo", "Glide",                     // P_LOCT .. P_GLIDE
    "Time", "Reverb", "Echo", "Echo",                         // P_MT4 .. P_S2ECHO
    "Width", "Time", "Rate", "Intro",                         // P_MW0 .. P_MI0
    "Decay", "Time", "Rate", "Intro",                         // P_S2DEC .. P_MI1
    "Width", "Attack", "Time", "Rate",                        // P_MW2 .. P_MR2
    "Root 1", "Root 2", "Root 3", "Root 4", "Root 5", "Root 6", "Root 7", "Root 8",
    "Kind 1", "Kind 2", "Kind 3", "Kind 4", "Kind 5", "Kind 6", "Kind 7", "Kind 8",
    "Type 1", "Type 2", "Type 3", "Type 4", "Type 5", "Type 6", "Type 7", "Type 8",
    "Play",                                                   // P_PMODE
    "Steps", "Steps 2", "Octaves", "Octs 2", "Skips",         // P_SGATE0 .. P_SSKIP0
    "Accents", "Accs 2", "Intro", "Intro", "Width",           // P_SACC0 .. P_MW3
    "Chance", "Chance 2", "Lead in", "Time", "Rate",          // P_SCH0 .. P_MR3
    "Accent", "Seq play",                                     // P_SACCENT, P_SMODE
    "Tone", "Density", "Air", "Drift", "Noise",               // P_NCOL .. P_NCHAR
    "Phaser", "Cutoff", "Phrases", "Reverb",                  // P_PHASER .. P_S2REV
    "Tone", "Length", "Blur", "Tone", "Width", "Ring",        // P_LTONE .. P_NTUNE
    "Width", "Plate", "Echo", "Width",                        // P_NWIDTH .. P_MW4
    "LFO",                                                    // P_NLFO
    "Arc",                                                    // P_ARC
    "Rate", "Intro",                                          // P_MR4, P_MI4
    "Bridge",                                                 // P_BRIDGE
    "Memory",                                                 // P_LMEM
};
static_assert(sizeof kShort / sizeof kShort[0] == P_NPARAMS, "a short name for every parameter");

// The two layer numberings (app_api.h): the nudge layers (kEvParam, kEvVoice, kEvNudge, kEvShake, flashes) and the
// mute layers (kEvMute, kEvSolo, planets).
const char* const kNudgeName[tp::app::kNumNudge] = {"Drone", "Seq", "Lead", "Seq 2", "Space", "Atmos"};
const char* const kMuteName[tp::app::kNumLayers] = {"Drone", "Seq", "Lead", "Seq 2", "Atmos"};
const int kShakeAll[5] = {0, 1, 2, 3, 5};    // shakeAll(): every nudge layer but the Space (ruling R8)

const char* nudgeName(int l) { return l >= 0 && l < tp::app::kNumNudge ? kNudgeName[l] : ""; }
const char* muteName(int l) { return l >= 0 && l < tp::app::kNumLayers ? kMuteName[l] : ""; }

// A line under construction: at most kTextLen bytes, whole UTF-8 sequences only, words joined by one space.
class Text
{
public:
    explicit Text(char* out) : b_(out) { b_[0] = 0; }
    // s as it is, as far as it fits under `lim` bytes; a malformed byte or a control character becomes '?'.
    void raw(const char* s, int lim = kTextLen)
    {
        if (lim > kTextLen) lim = kTextLen;
        for (int i = 0; s[i];)
        {
            const unsigned c = (unsigned char)s[i];
            int len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
            for (int k = 1; k < len; k++)
                if (((unsigned char)s[i + k] & 0xC0) != 0x80)
                {
                    len = 0;   // (cut short or malformed; the loop stops at the NUL, never past it)
                    break;
                }
            const bool bad = len == 0 || c < 0x20 || c == 0x7F;
            const int out = bad ? 1 : len;
            if (n_ + out > lim) break;
            if (bad)
                b_[n_++] = '?';
            else
                for (int k = 0; k < len; k++)
                    b_[n_++] = s[i + k];
            i += bad ? 1 : len;
        }
        b_[n_] = 0;
    }
    // A word after the ones before (a space between), cut to leave `reserve` bytes after it; nothing if empty.
    void word(const char* s, int reserve = 0)
    {
        if (!s || !*s) return;
        const int start = n_, lim = kTextLen - reserve;
        if (words_) raw(" ", lim);
        const int body = n_;
        raw(s, lim);
        while (n_ > body && b_[n_ - 1] == ' ')
            n_--;   // (a name cut after its space)
        if (n_ == body)
            n_ = start;
        else
            words_++;
        b_[n_] = 0;
    }
    int length() const { return n_; }

private:
    char* b_;
    int n_ = 0, words_ = 0;
};

// "+6", "−12", "±0" (to − from, in int: ±65535 at the int16 limits).
int deltaText(int d, char* b)
{
    char digits[12];
    int n = 0, k = 0;
    unsigned u = d < 0 ? 0u - (unsigned)d : (unsigned)d;
    do
        digits[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    const char* sign = d > 0 ? "+" : d < 0 ? "\xE2\x88\x92" : "\xC2\xB1";
    while (*sign)
        b[k++] = *sign++;
    while (n)
        b[k++] = digits[--n];
    b[k] = 0;
    return k;
}

} // namespace

const char* shortName(int param) { return param >= 0 && param < P_NPARAMS ? kShort[param] : ""; }

int formatEvent(const Event& e, char* out, int piece)
{
    using namespace tp::app;
    Text t(out);
    const bool asPiece = e.what == kEvReset || e.what == kEvPiece;   // ruling R9: the piece's line, whoever did it
    const char* who = asPiece ? "Piece" : e.who == kWhoYou ? "You" : e.who == kWhoEvolve ? "Evolve" : e.who == kWhoPiece ? "Piece" : "?";
    t.raw(who);
    t.raw(" \xE2\x80\xBA ");   // " › "
    const bool on = e.to != 0;
    switch (e.what)
    {
    case kEvParam:
    {
        char d[16];
        const int dn = deltaText((int)e.to - (int)e.from, d);
        t.word(nudgeName(e.layer));
        t.word(shortName(e.param), 1 + dn);
        t.word(d);
        break;
    }
    case kEvVoice:
    {
        char v[16] = "";
        if (e.param >= 0 && e.param < P_NPARAMS) en_param_text(e.param, e.to, v);
        v[sizeof v - 1] = 0;
        t.word("Shake");
        t.word(nudgeName(e.layer));
        t.word(v);
        break;
    }
    case kEvHold: t.word(on ? "Hold on" : "Hold off"); break;
    case kEvLock: t.word(on ? "Lock on" : "Lock off"); break;
    case kEvPlay: t.word(on ? "Play" : "Stop"); break;
    case kEvForget: t.word("Forget Lead"); break;
    case kEvNextChord: t.word("Next chord"); break;
    case kEvNewChords: t.word("New chords"); break;
    case kEvShakeAll: t.word("Shake all"); break;
    case kEvReset: t.word(en_piece_name(piece)); break;
    case kEvMute:
        t.word(on ? "Mute" : "Unmute");
        t.word(muteName(e.layer));
        break;
    case kEvSolo:
        t.word(on ? "Solo" : "Unsolo");
        t.word(muteName(e.layer));
        break;
    case kEvNudge:
        t.word("Nudge");
        t.word(nudgeName(e.layer));
        break;
    case kEvShake:
        t.word("Shake");
        t.word(nudgeName(e.layer));
        break;
    case kEvPiece: t.word(en_piece_name(e.to)); break;
    default: t.word("?"); break;
    }
    return t.length();
}

void Sky::reset()
{
    for (Orb& o : orb_)
        o = Orb{0.0f, 0.0f, false};
    for (int s = 0; s < 2; s++)
        seen_hits_[s] = seen_reps_[s] = 0;
    tick_ms_ = 0;
    for (int p = 0; p < kPlanets; p++)
    {
        act_[p] = 0.0f;
        heard_[p] = true;
    }
    for (int& f : flash_)
        f = 0;
    for (int l = 0; l < kLogLines; l++)
    {
        text_[l][0] = 0;
        serial_[l] = nextSerial();   // (new: the plugin sends the empty line)
    }
}

int Sky::nextSerial()
{
    int s = (int)(serial_next_++ & 0x7FFFFFFFu);
    if (!s) s = (int)(serial_next_++ & 0x7FFFFFFFu);
    return s;
}

// ui.c orb_add: the orbit's first parked puck, else the one with the least life (of equals, the first).
void Sky::launch(int seq, uint32_t count, bool rep)
{
    const int base = seq ? kPucks / 2 : 0;
    int k = base;
    for (int i = base; i < base + kPucks / 2; i++)
    {
        if (!orb_[i].on)
        {
            k = i;
            break;
        }
        if (orb_[i].life < orb_[k].life) k = i;
    }
    // 0.39 x (hits + reps), Sequence 2's 3 rad on; kept in 0..2pi (the counts grow without end)
    const double a = std::fmod(0.39 * (double)count + (seq ? 3.0 : 0.0), kTwoPi);
    orb_[k] = Orb{(float)a, rep ? 0.55f : 1.0f, true};
}

void Sky::push(const Event& e, int piece)
{
    for (int l = kLogLines - 1; l > 0; l--)
    {
        for (int k = 0; k < kTextMax; k++)
            text_[l][k] = text_[l - 1][k];
        serial_[l] = serial_[l - 1];
    }
    formatEvent(e, text_[0], piece);
    serial_[0] = nextSerial();
}

void Sky::advance(const SkyInput& in, int ms)
{
    using namespace tp::app;
    if (ms < 0) ms = 0;
    for (int& f : flash_)
        f = f > ms ? f - ms : 0;
    for (int p = 0; p < kPlanets; p++)
    {
        act_[p] = in.act[p];
        heard_[p] = in.heard[p];
    }

    // the pucks move a step each 16 ms, only while playing (ui.c draw_orbits)
    if (in.playing)
    {
        tick_ms_ += ms;
        const int steps = tick_ms_ / kStepMs;
        tick_ms_ %= kStepMs;
        for (int i = 0; i < kPucks && steps; i++)
        {
            Orb& o = orb_[i];
            if (!o.on) continue;
            if (steps > kLifeSteps)
            {
                o.on = false;
                continue;
            }
            const float spin = kSpin[i < kPucks / 2 ? 0 : 1];
            for (int s = 0; s < steps && o.on; s++)
            {
                o.a += spin;
                if (o.a >= (float)kTwoPi) o.a -= (float)kTwoPi;
                o.life -= kFade;
                if (o.life <= 0.0f) o.on = false;
            }
        }
    }

    // the sequences' new notes and repeats (ui.c orbits_take): a puck each, 3 notes and 2 repeats a call at most,
    // none while stopped or the layer unheard; the counts are taken all the same
    for (int s = 0; s < 2; s++)
    {
        const bool launches = in.playing && in.heard[s ? 3 : 1];
        uint32_t dh = in.hits[s] - seen_hits_[s], dr = in.reps[s] - seen_reps_[s];
        if (dh > 0x7FFFFFFFu) dh = 0;   // (a count gone back, a new boot: taken as it is, nothing launched)
        if (dr > 0x7FFFFFFFu) dr = 0;
        for (uint32_t k = 0; k < dh && k < 3u && launches; k++)
            launch(s, seen_hits_[s] + k + 1u + seen_reps_[s], false);
        seen_hits_[s] = in.hits[s];
        for (uint32_t k = 0; k < dr && k < 2u && launches; k++)
            launch(s, seen_hits_[s] + seen_reps_[s] + k + 1u, true);
        seen_reps_[s] = in.reps[s];
    }

    // the events: every one's flash; the newest kLogLines into the log
    const int n = in.events && in.numEvents > 0 ? in.numEvents : 0;
    for (int i = 0; i < n; i++)
    {
        const Event& e = in.events[i];
        switch (e.what)
        {
        case kEvParam:
        case kEvVoice:
        case kEvNudge:
        case kEvShake:
            if (e.layer >= 0 && e.layer < kNumNudge) flash_[e.layer] = kFlashMs;
            break;
        case kEvShakeAll:
            for (int l : kShakeAll)
                flash_[l] = kFlashMs;
            break;
        default: break;
        }
    }
    for (int i = n > kLogLines ? n - kLogLines : 0; i < n; i++)
        push(in.events[i], in.piece);
}

Puck Sky::puck(int i) const
{
    if (i < 0 || i >= kPucks || !orb_[i].on) return Puck{0.5f, 0.5f};
    return Puck{0.5f + 0.5f * std::cos(orb_[i].a), 0.5f + 0.5f * std::sin(orb_[i].a)};
}

int Sky::planetFrame(int layer) const
{
    if (layer < 0 || layer >= kPlanets) return 0;
    const float a = act_[layer];
    int level = a > 0.0f ? (a >= 1.0f ? 7 : (int)(a * 8.0f)) : 0;   // (NaN: 0)
    if (level > 7) level = 7;
    const int nudge = layer < 4 ? layer : 5;   // planet 4, the Atmosphere, is nudge layer 5 (the Space has none)
    const int state = flash_[nudge] > 0 ? 2 : heard_[layer] ? 0 : 1;
    return level * 3 + state;
}

int Sky::logSerial(int line) const { return line >= 0 && line < kLogLines ? serial_[line] : 0; }

const char* Sky::logText(int line) const { return line >= 0 && line < kLogLines ? text_[line] : ""; }

int Sky::flash(int layer) const { return layer >= 0 && layer < tp::app::kNumNudge ? flash_[layer] : 0; }

} // namespace sky
} // namespace tp
