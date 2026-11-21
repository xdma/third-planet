// SPDX-License-Identifier: GPL-3.0-only
// Third Planet - an MPC instrument plugin by Shostak.
//
// A port of NACHTFAHRT by Cranky Man (https://github.com/FartyPants/fm1-nachtfahrt), the generative
// 1970s Berlin School firmware for the M-VAVE FM-1. Both the sound engine and the FM-1's whole app
// (its pages, buttons, keys, PERFORM, pieces, Evolve) are NACHTFAHRT's own code, unchanged
// (src/nachtfahrt/, docs/VENDOR.md). This file gives the FM-1 the MPC's controls: pads and keys only play,
// the screen switches and acts (docs/superpowers/specs/2026-11-20-third-planet-controls-design.md):
//
//   Keys: Pads, notes from Pad base   +0..15 the Lead's scale degrees 1..16, +16..31 the 16 effects of the
//                                     FM-1's grid, +32..39 the moves (Rise, Close, Transpose, Sweep, Open,
//                                     Shut, Drop, Echo), each while held
//   other notes, or Keys: Lead        the FM-1's MIDI in: the Lead, to the nearest note of the scale
//   parameters                        every FM-1 knob, absolute; Hold, Lock, Play, the mutes and solos, the
//                                     Pattern's steps and accents, the Chords slot's root as switches; Forget,
//                                     Next chord, New chords, Shake all, Reset, the nudges and shakes as
//                                     one-shots (src/params.h)
//   Sync                              the MPC's tempo and transport, sent to the FM-1 as MIDI clock
//   display parameters                outputs only, after the controls: the status line (Pad, Chord, Hold, Lock)
//                                     and the star-chart screen's (the arc, the ring, the slots, the particles,
//                                     the planets, the log; src/sky.h), sent as they change, 24 a block at most
//
// Signal flow: the panel -> the FM-1 app (ui.c) -> engine commands -> en_render (Drone, Sequence,
// Sequence 2, Lead, Atmosphere, echo, plate) in 256-frame blocks -> stereo out. Time inside is the
// FM-1's, counted in rendered audio (app_unit.cpp), so nothing depends on the MPC's block size. The
// instrument plays by itself from the moment it is loaded, as the FM-1 does from power-on.
//
// One engine per process (its state is C globals): the first instance to process owns it; any other
// stays silent until the owner is removed. The project (every piece as it was left) is saved with the
// MPC project.

#include "mpc_plugin.h"

#include <atomic>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "app_api.h"
#include "engine.h"
#include "params.h"
#include "sky.h"

namespace {

// The pad banks, from Pad base: the Lead's degrees, the effects, the moves.
constexpr int kPadLeadN = 16, kPadFxN = tp::app::kNumFx, kPadMoveN = tp::app::kNumMoves;
constexpr int kPadSpan = kPadLeadN + kPadFxN + kPadMoveN;
constexpr int kMaxSends = 24;      // parameters sent back to the MPC per block (Kolo's device finding)
constexpr int kSettleChunks = 64;  // ~0.37 s after a boot, every value has been sent back to the MPC

// Display parameters: what the screen's status line shows, appended after the controls. Outputs only:
// not automatable, never read by the sound. A blank value is " " (an empty string shows the number).
constexpr int kDispPad = tp::kNumControls;   // what the last pad or note did (kPad*), or that another instance plays
constexpr int kDispChord = kDispPad + 1;     // the chord sounding (tp::app::chordCode)
constexpr int kDispHold = kDispPad + 2;      // Hold
constexpr int kDispLock = kDispPad + 3;      // Lock
// All of them by display index k (parameter kDispPad + k): the status line's four, then the star-chart screen's
// (docs/superpowers/specs/2026-11-20-third-planet-screen-design.md, section 10). tools/gen_skin.py finds them by id
// (d_section .. d_tempo), in this order, so it is free to grow at the end only.
constexpr int kSlots = EN_NPROG, kPucks = tp::sky::kPucks, kPlanets = tp::sky::kPlanets, kLogLines = tp::sky::kLogLines;
enum Disp : int
{
    kDPad, kDChord, kDHold, kDLock,
    kDSection,      // the arc's section, 1..4 (0 none)
    kDPhrase,       // the bar of the phrase, 1..8 (0 none)
    kDEvolve,       // percent of phrases Evolve nudges now
    kDNext,         // the next chord that will sound (a chord code)
    kDNextIn,       // bars until it does: -2 on hold, -1 none
    kDProg,         // 0 none; 1..3 Endless and the walk's character; 4 + position x 8 + (count - 1) in Loop
    kDRing,         // 0 no ring; 1..33 the chord's dots 0..32; 34..66 in its last bar; 67..99 held
    kDSlot,         // x 8: 0 a gap, 1 the walk's, 2 written, 3 the one playing
    kDSlotName = kDSlot + kSlots,       // x 8: the slot's chord code
    kDPx = kDSlotName + kSlots,         // x 4, then kDPy x 4: the particles, 0..1 in their orbit's pad
    kDPy = kDPx + kPucks,
    kDPlanet = kDPy + kPucks,           // x 5: the layer's planet frame (Sky::planetFrame)
    kDLog = kDPlanet + kPlanets,        // x 3: the log line's serial; its text is the line
    kDDrift = kDLog + kLogLines,        // x 5: the layer's filter drift, tenths of an octave: as it is while it moves
                                        // further out, kDriftStill further from 0 while it stands or eases back
    kDDensity = kDDrift + kPlanets,     // Density's slow drift, in the same way with kDensityStill
    kDStep,         // the step the Pattern page's lane plays, 16 none
    kDExt,          // on the MPC's clock
    kDSource,       // (reserved: the harmony's source, milestone B)
    kDSolo,         // a layer is soloed
    kDLog1Hot,      // the newest log line while it is new (kLogHotFrames): its serial, -1 none; meanwhile kDLog is -1
    kDTempo,        // the header's tempo: the BPM, or -1 on the MPC's clock (EXT)
    kNumDisplay
};
static_assert(kDispChord == kDispPad + kDChord && kDispHold == kDispPad + kDHold && kDispLock == kDispPad + kDLock,
              "the status line's four come first");
static_assert(EN_NPROG == 8 && EN_NSEC == 4 && EN_NWALK == 3 && tp::app::kNumLayers == kPlanets, "the screen's counts");
constexpr int kMirrorMin = 4;         // of a block's kMaxSends, the controls always get this many
constexpr int kPuckUnit = 1000;       // a particle's 0..1, in the thousandths the scheduler compares
constexpr int kPuckMove = 2;          // ... sent when it moved by more than this
constexpr int kPuckFrames = 8820;     // all the particles together every 200 ms: 5 Hz (ruling R23)
constexpr int kPaceFrames = 4410;     // the planets and the drifts: 10 a second at most
constexpr int kDriftMax = 45, kDriftStill = 100;       // (tenths of an octave: ui.c shows 4.5 at most)
constexpr int kDensityMax = 100, kDensityStill = 200;
constexpr int kLoadHoldFrames = 22050;   // after a project load the MPC keeps the display values it loaded this long
constexpr int kLogSlots = 1024;       // a log line's value: its serial modulo this
constexpr int kLogWords = tp::sky::kTextMax / 4;
constexpr int kLogHotFrames = 88200;  // the newest log line is shown in the accent this long (2 s of audio)
constexpr int kTempoMax = 300;        // (above any BPM the FM-1's Tempo reaches)

constexpr bool isPuck(int k) { return k >= kDPx && k < kDPlanet; }
constexpr bool isPaced(int k) { return (k >= kDPlanet && k < kDLog) || (k >= kDDrift && k <= kDDensity); }
constexpr bool isLog(int k) { return k >= kDLog && k < kDLog + kLogLines; }
constexpr bool isChordCode(int k) { return k == kDChord || k == kDNext || (k >= kDSlotName && k < kDPx); }

// The Pad field's values: 0 nothing yet; a note n (Lead mode, or outside the banks) n + 1; then the banks.
constexpr int kPadBusy = 129, kPadLead0 = 130, kPadFx0 = kPadLead0 + kPadLeadN, kPadMove0 = kPadFx0 + kPadFxN,
              kPadCodes = kPadMove0 + kPadMoveN;
// A display value's range, and `rest`: what it is with nothing to show (the parameter's default).
struct DispRange { int lo, hi, rest; };
constexpr DispRange dispRange(int k)
{
    return k == kDPad ? DispRange{0, kPadCodes - 1, 0}
         : isChordCode(k) ? DispRange{0, tp::app::kChordCodes - 1, 0}
         : k == kDSection ? DispRange{0, EN_NSEC, 0}
         : k == kDPhrase ? DispRange{0, 8, 0}
         : k == kDEvolve ? DispRange{0, 100, 0}
         : k == kDNextIn ? DispRange{-2, 64, -1}
         : k == kDProg ? DispRange{0, 3 + 8 * 8, 0}
         : k == kDRing ? DispRange{0, 99, 0}
         : k >= kDSlot && k < kDSlotName ? DispRange{0, 3, 0}
         : isPuck(k) ? DispRange{0, kPuckUnit, kPuckUnit / 2}
         : k >= kDPlanet && k < kDLog ? DispRange{0, 23, 0}
         : isLog(k) ? DispRange{-1, kLogSlots - 1, 0}   // (-1: blank, line 1 while it is hot)
         : k == kDLog1Hot ? DispRange{-1, kLogSlots - 1, -1}
         : k == kDTempo ? DispRange{-1, kTempoMax, 0}
         : k >= kDDrift && k < kDDensity ? DispRange{-kDriftStill - kDriftMax, kDriftStill + kDriftMax, 0}
         : k == kDDensity ? DispRange{-kDensityStill - kDensityMax, kDensityStill + kDensityMax, 0}
         : k == kDStep ? DispRange{0, 16, 16}
         : DispRange{0, 1, 0};   // Hold, Lock, EXT, the source, Solo
}
const char* const kHoldText[] = {" ", "HOLD"};
const char* const kLockText[] = {" ", "LOCK"};
const char* const kBusyText = "Another Third Planet plays";
const char* const kSectionText[] = {" ", "EMERGE", "FLOW", "SOLO", "BRIDGE"};   // (EN_SEC_NAME, as the screen's caps)
const char* const kWalkText[] = {"HOME", "WANDER", "FREE"};                     // (EN_WALK_NAME)
const char* const kExtText[] = {" ", "EXT"};
const char* const kSourceText[] = {" ", "PERFORMER"};
const char* const kSoloText[] = {" ", "SOLO"};
#define TP_MINUS "\xE2\x88\x92"   // U+2212, as the log writes a fall
#define TP_DOT "\xC2\xB7"         // U+00B7
constexpr int kFixFrames = 4410;   // a status value the MPC wrote over is corrected at most every 100 ms
constexpr uint32_t kSwitchSettle = 2;   // chunks a switch keeps the MPC's value after a write (Hold, Play: a block late)

const char* const kKeyModes[] = {"Pads", "Lead"};
const char* const kOffOn[] = {"Off", "On"};
const char* const kNoteName[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

bool isBool(tp::Kind k)
{
    return k == tp::Kind::Sync || k == tp::Kind::Switch || k == tp::Kind::Action || k == tp::Kind::Step ||
           k == tp::Kind::Accent;
}

// A value as the FM-1's screen shows it.
void label(const tp::ParamDef& d, int v, char* buf, size_t n)
{
    using namespace tp::app;
    switch (d.kind)
    {
        case tp::Kind::Master: std::snprintf(buf, n, "%d%%", v); return;
        case tp::Kind::Piece: std::snprintf(buf, n, "%s", en_piece_name(v)); return;
        case tp::Kind::Walk: std::snprintf(buf, n, "%s", EN_WALK_NAME[v % EN_NWALK]); return;
        case tp::Kind::Sync: std::snprintf(buf, n, "%s", kOffOn[v ? 1 : 0]); return;
        case tp::Kind::KeyMode: std::snprintf(buf, n, "%s", kKeyModes[v ? 1 : 0]); return;
        case tp::Kind::PadBase: std::snprintf(buf, n, "%d", v); return;
        case tp::Kind::Switch:
        case tp::Kind::Action:
        case tp::Kind::Step:
        case tp::Kind::Accent: std::snprintf(buf, n, "%s", kOffOn[v ? 1 : 0]); return;
        case tp::Kind::ChordRoot: std::snprintf(buf, n, "%s", v >= 0 && v < 12 ? kNoteName[v] : "-"); return;
        case tp::Kind::Voice: break;
        case tp::Kind::Knob: break;
    }
    const int c = d.code;
    if (c < P_NPARAMS)
    {
        char t[16];
        en_param_text(c, v, t);
        std::snprintf(buf, n, "%s", t[0] == '+' ? t + 1 : t);   // (the FM-1 drops a leading '+')
        return;
    }
    switch (c)
    {
        case kKnobFilter:
            if (v == 0) std::snprintf(buf, n, "Off");
            else std::snprintf(buf, n, "%s %d", v < 0 ? "Shut" : "Open", std::abs(v) * 2);
            return;
        case kKnobWet: std::snprintf(buf, n, "%+d", v * 2); return;
        case kKnobEnergy: std::snprintf(buf, n, "%+d", v); return;
        case kKnobMode: std::snprintf(buf, n, "%s", EN_MODE_NAME[modeAt(v)]); return;
        case kKnobLoop: std::snprintf(buf, n, "%s", v ? "Loop" : "Endless"); return;
        case kKnobSlot: std::snprintf(buf, n, "%d", v + 1); return;
        case kKnobChord: std::snprintf(buf, n, "%s", EN_CQ_NAME[v % EN_NCQ]); return;
        case kKnobType: std::snprintf(buf, n, "%s", EN_CX_NAME[v % EN_NCX]); return;
        case kKnobLane: std::snprintf(buf, n, "%s", laneName(v)); return;
        default: std::snprintf(buf, n, "%d", v); return;
    }
}

int rangeLo(const tp::ParamDef& d)
{
    using namespace tp::app;
    if (d.kind == tp::Kind::ChordRoot) return -1;
    if (d.kind != tp::Kind::Knob && d.kind != tp::Kind::Voice) return 0;
    if (d.code < P_NPARAMS) return en_param_info(d.code)->lo;
    return d.code >= kKnobFilter && d.code <= kKnobSpace ? -50 : 0;
}

int rangeHi(const tp::ParamDef& d)
{
    using namespace tp::app;
    switch (d.kind)
    {
        case tp::Kind::Master: return 200;
        case tp::Kind::Piece: return EN_NPIECES - 1;
        case tp::Kind::Walk: return EN_NWALK - 1;
        case tp::Kind::Sync: return 1;
        case tp::Kind::KeyMode: return 1;
        case tp::Kind::PadBase: return 128 - kPadSpan;
        case tp::Kind::ChordRoot: return 11;
        case tp::Kind::Switch:
        case tp::Kind::Action:
        case tp::Kind::Step:
        case tp::Kind::Accent: return 1;
        case tp::Kind::Voice:
        case tp::Kind::Knob: break;
    }
    if (d.code < P_NPARAMS) return en_param_info(d.code)->hi;
    switch (d.code)
    {
        case kKnobFilter: case kKnobWet: case kKnobEnergy: case kKnobSpace: return 50;
        case kKnobMode: return EN_NMODES - 1;
        case kKnobLoop: return 1;
        case kKnobSlot: return EN_NPROG - 1;
        case kKnobChord: return EN_NCQ - 1;
        case kKnobType: return EN_NCX - 1;
        case kKnobLane: return EN_NDRUM - 1;
        case kKnobLaneChance: return 100;
        default: return 0;
    }
}

int rangeDef(const tp::ParamDef& d)
{
    if (d.kind == tp::Kind::Master) return 100;
    if (d.kind == tp::Kind::PadBase) return 36;   // (C1: the MPC's bank A pad 1, Pad Perform chromatic from C1)
    if ((d.kind == tp::Kind::Knob || d.kind == tp::Kind::Voice) && d.code < P_NPARAMS) return en_param_info(d.code)->def;
    return 0;
}

// The MPC's parameter for an FM-1 control: a list when it has few named values, else a number.
mpc::Param makeParam(const tp::ParamDef& d)
{
    const char* name = d.name ? d.name : en_param_info(d.code)->name;
    const int lo = rangeLo(d), hi = rangeHi(d), def = rangeDef(d);
    if (isBool(d.kind))
    {
        mpc::Param p = mpc::boolParam(d.id, name, false);
        // (one-shots, and what belongs to the lane or slot on show, would replay wrongly as automation)
        p.automatable = d.kind == tp::Kind::Sync || d.kind == tp::Kind::Switch;
        return p;
    }
    if (d.kind == tp::Kind::PadBase) return mpc::intParam(d.id, name, lo, hi, def, "");
    if (hi - lo < 25 && d.kind != tp::Kind::Master)
    {
        std::vector<std::string> names;
        char buf[32];
        for (int v = lo; v <= hi; ++v)
        {
            label(d, v, buf, sizeof buf);
            names.emplace_back(buf);
        }
        mpc::Param p = mpc::choiceParam(d.id, name, names, def - lo);
        p.automatable = d.kind != tp::Kind::ChordRoot;
        return p;
    }
    return mpc::intParam(d.id, name, lo, hi, def, "");
}

// A screen value's text, 31 bytes at most, in ASCII and the glyphs the MPC's fonts have; " " is blank. False: the
// value shows as it is (a film strip's frame, a choice, a particle's place). The log's lines are the plugin's.
bool screenText(int k, int v, char* t, size_t n)
{
    const int a = v < 0 ? -v : v;
    if (isChordCode(k))
    {
        char c[tp::app::kChordTextMax];
        tp::app::chordText(v, c);
        std::snprintf(t, n, "%s", c[0] != 0 ? c : " ");
    }
    else if (k == kDEvolve)
    {
        if (v > 0) std::snprintf(t, n, "EVOLVE %d%%", v);
        else std::snprintf(t, n, " ");
    }
    else if (k == kDTempo)
    {
        if (v < 0) std::snprintf(t, n, "EXT");
        else if (v > 0) std::snprintf(t, n, "%d BPM", v);
        else std::snprintf(t, n, " ");
    }
    else if (k == kDNextIn)   // (app_api.h barsToNextChord)
    {
        if (v == -2) std::snprintf(t, n, "ON HOLD");
        else if (v < 0) std::snprintf(t, n, " ");
        else if (v <= 1) std::snprintf(t, n, "ON THE BAR");
        else std::snprintf(t, n, "IN %d BARS", v);
    }
    else if (k == kDProg)
    {
        if (v >= 1 && v <= EN_NWALK) std::snprintf(t, n, "ENDLESS " TP_DOT " %s", kWalkText[v - 1]);
        else if (v > EN_NWALK && v <= dispRange(kDProg).hi) std::snprintf(t, n, "LOOP %d/%d", (v - 4) / 8 + 1, (v - 4) % 8 + 1);
        else std::snprintf(t, n, " ");
    }
    else if (k >= kDDrift && k < kDDensity)   // (ui.c's sound pages, from a tenth of an octave: "MOVING" while it
    {                                         // goes further out)
        const bool still = a >= kDriftStill;
        const int d = still ? a - kDriftStill : a;
        if (d >= 1)
            std::snprintf(t, n, "%s%s %s%d.%d OCT", still ? "" : "MOVING ", v < 0 ? "DARKER" : "BRIGHTER", v < 0 ? TP_MINUS : "+",
                          d / 10, d % 10);
        else std::snprintf(t, n, " ");
    }
    else if (k == kDDensity)   // (ui.c's Sequence page, from 3 of Density's units)
    {
        const bool still = a > kDensityStill;
        const int d = still ? a - kDensityStill : a;
        if (d >= 3) std::snprintf(t, n, "%s%s %s%d", still ? "" : "MOVING ", v < 0 ? "SPARSER" : "DENSER", v < 0 ? TP_MINUS : "+", d);
        else std::snprintf(t, n, " ");
    }
    else
        return false;
    return true;
}

// The screen's display parameter k (from kDSection on).
mpc::Param makeDisplay(int k)
{
    const DispRange r = dispRange(k);
    char id[24], name[24];
    auto numbered = [&](const char* i, const char* nm, int first) {
        std::snprintf(id, sizeof id, "%s%d", i, k - first + 1);
        std::snprintf(name, sizeof name, "%s %d", nm, k - first + 1);
        return mpc::intParam(id, name, r.lo, r.hi, r.rest, "");
    };
    switch (k)
    {
        case kDSection: return mpc::choiceParam("d_section", "Section", std::vector<std::string>(kSectionText, kSectionText + 5), 0);
        case kDPhrase: return mpc::intParam("d_phrase", "Phrase bar", r.lo, r.hi, r.rest, "");
        case kDEvolve: return mpc::intParam("d_evolve", "Evolve chance", r.lo, r.hi, r.rest, "");
        case kDNext: return mpc::intParam("d_next", "Next", r.lo, r.hi, r.rest, "");   // (not "Next chord": the one-shot's)
        case kDNextIn: return mpc::intParam("d_next_in", "Next chord in", r.lo, r.hi, r.rest, "");
        case kDProg: return mpc::intParam("d_prog", "Progression", r.lo, r.hi, r.rest, "");
        case kDRing: return mpc::intParam("d_ring", "Chord ring", r.lo, r.hi, r.rest, "");
        case kDDensity: return mpc::intParam("d_density", "Density drift", r.lo, r.hi, r.rest, "");
        case kDStep: return mpc::intParam("d_step", "Step playing", r.lo, r.hi, r.rest, "");
        case kDExt: return mpc::choiceParam("d_ext", "Clock", std::vector<std::string>(kExtText, kExtText + 2), 0);
        case kDSource: return mpc::choiceParam("d_source", "Harmony source", std::vector<std::string>(kSourceText, kSourceText + 2), 0);
        case kDSolo: return mpc::choiceParam("d_solo", "Solo status", std::vector<std::string>(kSoloText, kSoloText + 2), 0);
        case kDLog1Hot: return mpc::intParam("d_log1hot", "Log line 1 hot", r.lo, r.hi, r.rest, "");
        case kDTempo: return mpc::intParam("d_tempo", "Tempo text", r.lo, r.hi, r.rest, "");
        default: break;
    }
    if (k < kDSlotName) return numbered("d_slot", "Slot", kDSlot);
    if (k < kDPx) return numbered("d_slotname", "Slot chord", kDSlotName);
    if (isPuck(k))
    {
        const bool y = k >= kDPy;
        std::snprintf(id, sizeof id, "d_p%c%d", y ? 'y' : 'x', k - (y ? kDPy : kDPx) + 1);
        std::snprintf(name, sizeof name, "Particle %d %c", k - (y ? kDPy : kDPx) + 1, y ? 'Y' : 'X');
        return mpc::floatParam(id, name, 0.0f, 1.0f, (float)r.rest / (float)kPuckUnit, "", 3);
    }
    if (k < kDLog) return numbered("d_planet", "Planet", kDPlanet);
    if (k < kDDrift) return numbered("d_log", "Log line", kDLog);
    return numbered("d_drift", "Filter drift", kDDrift);
}

} // namespace

class ThirdPlanet : public mpc::Plugin
{
public:
    ThirdPlanet() : mpc::Plugin(makeInfo())
    {
        for (int i = 0; i < tp::kNumControls; ++i)
        {
            addParam(makeParam(tp::kParams[i]));
            lo_[i] = rangeLo(tp::kParams[i]);
            if (tp::kParams[i].kind == tp::Kind::Sync) idxSync_ = i;
            if (tp::kParams[i].kind == tp::Kind::KeyMode) idxKeyMode_ = i;
            if (tp::kParams[i].kind == tp::Kind::PadBase) idxPadBase_ = i;
            appVal_[i].store(INT_MIN, std::memory_order_relaxed);
            dirty_[i].store(false, std::memory_order_relaxed);
        }
        addDisplay(mpc::intParam("d_pad", "Pad", 0, kPadCodes - 1, 0, ""));
        addDisplay(mpc::intParam("d_chord", "Chord", 0, tp::app::kChordCodes - 1, 0, ""));
        addDisplay(mpc::choiceParam("d_hold", "Hold status", std::vector<std::string>(kHoldText, kHoldText + 2), 0));
        addDisplay(mpc::choiceParam("d_lock", "Lock status", std::vector<std::string>(kLockText, kLockText + 2), 0));
        for (int k = kDSection; k < kNumDisplay; ++k) addDisplay(makeDisplay(k));
        for (int k = 0; k < kNumDisplay; ++k)
        {
            displaySent_[k].store(INT_MIN, std::memory_order_relaxed);   // (INT_MIN: never sent)
            fixOne_[k].store(false, std::memory_order_relaxed);
        }
        for (auto& line : logText_)
            for (std::atomic<uint32_t>& w : line) w.store(0, std::memory_order_relaxed);
        forgetHeld();
        displayReset_.store(true, std::memory_order_relaxed);
    }

    ~ThirdPlanet() override { lease_.release(this); }

    void prepare(double sampleRate, int maxBlockSize) override
    {
        (void)sampleRate;   // the engine runs at 44100 Hz, the MPC's fixed rate
        (void)maxBlockSize;
    }

    // A value from the MPC (a Q-Link, the screen, automation, a project load). Applied on the audio
    // thread before the next block, in parameter order. A value the app itself just sent is not sent back.
    void parameterChanged(int index, float plainValue) override
    {
        if (index >= kDispPad && index < kDispPad + kNumDisplay)
        {   // (the MPC wrote a display value itself, a load or automation: the true one goes out again)
            const int k = index - kDispPad, sent = displaySent_[k].load(std::memory_order_acquire);
            const bool same = isPuck(k) ? sent != INT_MIN && std::fabs(plainValue * (float)kPuckUnit - (float)sent) <= 1.0f
                                        : (int)std::lround(plainValue) == sent;
            if (!same)
            {
                fixOne_[k].store(true, std::memory_order_release);
                displayFix_.store(true, std::memory_order_release);
            }
            return;
        }
        if (index < 0 || index >= tp::kNumControls) return;
        const int v = (int)std::lround(plainValue) + (isChoice(index) ? lo_[index] : 0);
        if (v == appVal_[index].load(std::memory_order_acquire)) return;
        wanted_[index].store(v, std::memory_order_relaxed);
        dirty_[index].store(true, std::memory_order_release);
    }

    bool formatValue(int index, float plainValue, std::string& out) override
    {
        if (index == kDispPad)
        {
            out = padText(std::isfinite(plainValue) ? (int)std::lround(plainValue) : 0);
            return true;
        }
        if (index > kDispPad && index < kDispPad + kNumDisplay)
        {
            const int k = index - kDispPad;
            const DispRange r = dispRange(k);
            int v = std::isfinite(plainValue) ? (int)std::lround(plainValue) : r.rest;
            v = v < r.lo ? r.lo : v > r.hi ? r.hi : v;
            if (isLog(k) || k == kDLog1Hot)
            {
                out = v < 0 ? std::string(" ") : logLine(v);
                return true;
            }
            char t[tp::sky::kTextMax];
            if (!screenText(k, v, t, sizeof t)) return false;
            out = t;
            return true;
        }
        if (index < 0 || index >= tp::kNumControls || isChoice(index)) return false;
        char buf[32];
        label(tp::kParams[index], (int)std::lround(plainValue), buf, sizeof buf);
        out = buf;
        return true;
    }

    // The project: the FM-1's own save (every piece as it was left), as Third Planet last copied it.
    void saveExtraState(std::vector<uint8_t>& out) override
    {
        uint8_t buf[tp::app::kStoreMax];
        int len = -1;
        if (lease_.owns(this) && booted_.load(std::memory_order_acquire))
        {
            for (int tries = 0; tries < 8; ++tries)
            {
                const uint32_t s0 = snapSeq_.load(std::memory_order_acquire);
                if (s0 & 1u) continue;
                const int k = (int)(s0 >> 1) & 1;
                len = snapLen_[k];
                if (len > 0) std::memcpy(buf, snap_[k], (size_t)len);
                if (snapSeq_.load(std::memory_order_acquire) == s0) break;
                len = -1;
            }
        }
        if (len <= 0)
        {
            len = ownLen_;
            if (len > 0) std::memcpy(buf, ownBlob_, (size_t)len);
        }
        out.clear();
        out.push_back('T'), out.push_back('P'), out.push_back('1'), out.push_back(0);
        if (len > 0) out.insert(out.end(), buf, buf + len);
    }

    void loadExtraState(const uint8_t* data, size_t size) override
    {
        if (size < 4 || std::memcmp(data, "TP1", 4) != 0) return;
        const size_t n = size - 4 <= (size_t)tp::app::kStoreMax ? size - 4 : 0;
        std::memcpy(pendingBlob_, data + 4, n);
        pendingLen_ = n > 0 ? (int)n : -1;
        hostKnows_.store(true, std::memory_order_release);
        displayReset_.store(true, std::memory_order_release);   // (the load brought the saved status values)
        loadHold_.store(true, std::memory_order_release);
        loadPending_.store(true, std::memory_order_release);
    }

    void process(mpc::ProcessContext& ctx) override
    {
        // A project just loaded: the MPC shows the display values it loaded for half a second, then the true ones
        // all at once (nothing is sent or corrected meanwhile, so a load reads back as it was saved).
        if (loadHold_.exchange(false, std::memory_order_acq_rel)) hold_ = kLoadHoldFrames;
        const bool holding = hold_ > 0;
        if (holding && (hold_ -= ctx.numFrames) <= 0)
        {
            hold_ = 0;
            displayReset_.store(true, std::memory_order_release);   // (what was written over meanwhile goes with it)
            displayFix_.store(false, std::memory_order_release);
            for (std::atomic<bool>& f : fixOne_) f.store(false, std::memory_order_release);
        }
        bool fresh = false;
        if (!lease_.claim(this, fresh))   // another instance owns the engine: silence, and the screen says so
        {
            forgetHeld();   // (what this instance held went with the engine; the owner boots its own)
            if (!holding) sendBusy();
            return;
        }
        actionSends_ = 0;
        if (loadPending_.exchange(false, std::memory_order_acq_rel))
        {
            ownLen_ = pendingLen_;
            if (ownLen_ > 0) std::memcpy(ownBlob_, pendingBlob_, (size_t)ownLen_);
            fresh = true;
        }
        const bool booting = fresh || !booted_.load(std::memory_order_relaxed);
        if (booting) boot();

        applyParams();
        if (booting)
        {   // (the switches a load brought back were set through the hand's setters: not the hand's, not logged)
            tp::app::Event drop[tp::app::kNumEvents];
            while (tp::app::takeEvents(drop, tp::app::kNumEvents) > 0) {}
            sky_.reset();
            skyCarry_ = 0;
        }
        tp::app::setMaster((uint32_t)getInt(0) * 2048u / 100u);
        midiIn(ctx);
        clock(ctx);
        render(ctx);
        watch(ctx.numFrames);
        // A block's notifications, kMaxSends in all: the one-shots' way back to Off (applyParams), then the display,
        // then the controls with what is left, never fewer than kMirrorMin.
        const int left = kMaxSends - actionSends_;
        mirror(left - (holding ? 0 : sendDisplay(status(), left - kMirrorMin, ctx.numFrames)));
        if (chunks_ > kSettleChunks) hostKnows_.store(true, std::memory_order_release);   // (every value sent back by now)
    }

    const tp::sky::Sky& sky() const { return sky_; }   // (tests: the screen's model, as the audio thread left it)
    bool logHot() const { return logHot_ > 0; }          // (tests: the newest log line is in the accent)

private:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name = "Third Planet";
        info.vendor = "Shostak";
        info.uniqueId = MPC_FOURCC('S', 's', 'n', 'i');
        info.version = 1000;
        info.isInstrument = true;
        info.numInputs = 0;
        info.numOutputs = 2;
        return info;
    }

    bool isChoice(int i) const { return param(i).kind == mpc::Param::Kind::Choice; }

    // The FM-1 from power-on, with this instance's project (or the recipes).
    void boot()
    {
        tp::app::storeWrite(ownLen_ > 0 ? ownBlob_ : nullptr, ownLen_ > 0 ? ownLen_ : -1);
        tp::app::boot();
        pos_ = tp::kChunk;
        chunks_ = 0;
        forgetHeld();   // (power_on let go of everything)
        clockRunning_ = false;
        lastTick_ = LLONG_MIN;
        displayReset_.store(true, std::memory_order_relaxed);
        // The FM-1's project is the truth for everything it saves: values the MPC loaded alongside it are
        // dropped (as shown, they may carry Motion's octave shift, or a walk slot as if written). What the
        // FM-1 never saves comes back from the MPC once it holds this instance's values (a project
        // loaded, or values sent back earlier): the three macros, the Pattern lane, the chord slot, and the
        // switches (Hold, Lock, Play, the mutes and solos).
        const bool restore = hostKnows_.load(std::memory_order_acquire);
        for (int i = 0; i < tp::kNumControls; ++i)
        {
            appVal_[i].store(INT_MIN, std::memory_order_relaxed);
            settle_[i] = 0;
            const tp::ParamDef& d = tp::kParams[i];
            if (d.kind == tp::Kind::Master || d.kind == tp::Kind::Sync || d.kind == tp::Kind::KeyMode ||
                d.kind == tp::Kind::PadBase)
                continue;   // (Volume, Sync, Keys, Pad base: the plugin's own, kept as they are)
            // (the steps, accents and the root are the app's, saved in its project; the one-shots go back to Off and
            // never fire from a load: mirror() sends every one of them from the app. The switches are the FM-1's
            // state it never saves, so they come back from the MPC with the macros)
            const bool unsaved = d.kind == tp::Kind::Switch ||
                                 (d.kind == tp::Kind::Knob &&
                                  (d.code == tp::app::kKnobFilter || d.code == tp::app::kKnobWet ||
                                   d.code == tp::app::kKnobEnergy || d.code == tp::app::kKnobLane ||
                                   d.code == tp::app::kKnobSlot));
            if (restore && unsaved)
            {
                wanted_[i].store((int)std::lround(get(i)) + (isChoice(i) ? lo_[i] : 0), std::memory_order_relaxed);
                dirty_[i].store(true, std::memory_order_release);
            }
            else
                dirty_[i].store(false, std::memory_order_release);
        }
        booted_.store(true, std::memory_order_release);
    }

    void applyParams()
    {
        using namespace tp;
        for (int i = 0; i < kNumControls; ++i)
        {
            if (!dirty_[i].exchange(false, std::memory_order_acq_rel)) continue;
            const int v = wanted_[i].load(std::memory_order_relaxed);
            const ParamDef& d = kParams[i];
            switch (d.kind)
            {
                case Kind::Knob: app::knob(d.code, v); break;
                case Kind::Voice: app::voice(d.code, v); break;
                case Kind::Piece: app::setPiece(v); break;
                case Kind::Walk: app::walk(v); break;
                case Kind::Sync:
                    if (!v && syncWas_)   // back to the FM-1's own clock: the clock stops coming (the engine
                    {                     // drops it after 100 blocks) and the piece plays on, or starts if it rested
                        clockRunning_ = false;
                        unsyncPending_ = true;
                    }
                    syncWas_ = v != 0;
                    break;
                case Kind::KeyMode:
                case Kind::PadBase: releaseHeld(); break;   // (read where used; a pad held now meant something else)
                case Kind::Switch:
                    setSwitch(d.code, v != 0);
                    settle(i, v);
                    break;
                case Kind::Action:
                    appVal_[i].store(0, std::memory_order_release);   // (before the write: it comes back here)
                    setNormalised(i, 0.0f);
                    notifyHostParameterChanged(i);
                    ++actionSends_;
                    if (v) act(d.code);
                    break;
                case Kind::Step: app::setStep(d.code, v != 0); settle(i, v); break;
                case Kind::Accent: app::setAccent(d.code, v != 0); settle(i, v); break;
                case Kind::ChordRoot: app::setChordRoot(v); settle(i, v); break;
                case Kind::Master: break;   // read where used
            }
        }
    }

    // A switch set from the MPC: the app's own setter (absolute).
    static void setSwitch(int code, bool on)
    {
        using namespace tp;
        if (code == kSwHold) app::setHold(on);
        else if (code == kSwLock) app::setLock(on);
        else if (code == kSwPlay) app::play(on);
        else if (code >= kSwMute0 && code < kSwMute0 + app::kNumLayers) app::setMute(code - kSwMute0, on);
        else if (code >= kSwSolo0 && code < kSwSolo0 + app::kNumLayers) app::setSolo(code - kSwSolo0, on);
    }
    static bool switchValue(int code)
    {
        using namespace tp;
        if (code == kSwHold) return app::held();
        if (code == kSwLock) return app::locked();
        if (code == kSwPlay) return app::playing();
        if (code >= kSwMute0 && code < kSwMute0 + app::kNumLayers) return app::muted(code - kSwMute0);
        if (code >= kSwSolo0 && code < kSwSolo0 + app::kNumLayers) return app::soloed(code - kSwSolo0);
        return false;
    }
    // A one-shot pressed.
    static void act(int code)
    {
        using namespace tp;
        switch (code)
        {
            case kActForget: app::forget(); return;
            case kActNext: app::nextChord(); return;
            case kActNewChords: app::newChords(); return;
            case kActShakeAll: app::shakeAll(); return;
            case kActReset: app::resetPiece(); return;
            default:
                if (code >= kActNudge0 && code < kActNudge0 + app::kNumNudge) app::nudgeLayer(code - kActNudge0, false);
                else if (code >= kActShake0 && code < kActShake0 + app::kNumNudge) app::nudgeLayer(code - kActShake0, true);
                return;
        }
    }
    // The MPC's value stays as written for a few blocks, while the app takes it (Hold, Play: a block late).
    void settle(int i, int v)
    {
        appVal_[i].store(v, std::memory_order_release);
        settle_[i] = chunks_ + kSwitchSettle;
    }

    // MIDI notes: the pads' banks (Keys: Pads), or the FM-1's MIDI in. What a note does is fixed when it goes down
    // (padAct_), so its release ends that, whatever Keys or Pad base say by then.
    enum : int8_t { kNoAct = -2, kLeadMidi = -1 };   // padAct_: nothing held, the Lead by MIDI, else a bank's pad
    void midiIn(const mpc::ProcessContext& ctx)
    {
        const bool pads = getInt(idxKeyMode_) == 0;
        const int base = getInt(idxPadBase_);
        for (int i = 0; i < ctx.numMidiEvents; ++i)
        {
            const mpc::MidiEvent& e = ctx.midi[i];
            if (e.isAllNotesOff()) { releaseHeld(); continue; }
            if (!e.isNoteOn() && !e.isNoteOff()) continue;
            const int n = e.note() & 127;
            if (e.isNoteOn())
            {
                if (noteCount_[n] < 255 && noteCount_[n]++ > 0) continue;   // (already down: the first press acts)
                const int k = n - base;
                padAct_[n] = pads && k >= 0 && k < kPadSpan ? (int8_t)k : kLeadMidi;
                press(n, padAct_[n], true, e.velocity());
                lastPad_ = padAct_[n] >= 0 ? padCode(padAct_[n]) : n + 1;
            }
            else if (noteCount_[n] > 0 && --noteCount_[n] == 0)
            {
                press(n, padAct_[n], false, 0);
                padAct_[n] = kNoAct;
            }
        }
    }
    static int padCode(int k)
    {
        return k < kPadLeadN ? kPadLead0 + k : k < kPadLeadN + kPadFxN ? kPadFx0 + k - kPadLeadN
                                                                      : kPadMove0 + k - kPadLeadN - kPadFxN;
    }
    static void press(int note, int act, bool down, int velocity)
    {
        using namespace tp;
        if (act == kLeadMidi)
            app::midiIn(down ? 0x09u | 0x90u << 8 | (uint32_t)note << 16 | (uint32_t)velocity << 24
                             : 0x08u | 0x80u << 8 | (uint32_t)note << 16);
        else if (act >= 0 && act < kPadLeadN) app::padLead(act, down);
        else if (act >= kPadLeadN && act < kPadLeadN + kPadFxN) app::padFx(act - kPadLeadN, down);
        else if (act >= kPadLeadN + kPadFxN && act < kPadSpan) app::padMove(act - kPadLeadN - kPadFxN, down);
    }
    // Everything held let go (all notes off, a change of Keys or Pad base).
    void releaseHeld()
    {
        for (int n = 0; n < 128; ++n)
            if (noteCount_[n] && padAct_[n] != kNoAct) press(n, padAct_[n], false, 0);
        forgetHeld();
    }
    // Everything held forgotten without telling the app (it was reset, or belongs to another instance now).
    void forgetHeld()
    {
        std::memset(noteCount_, 0, sizeof noteCount_);
        std::memset(padAct_, kNoAct, sizeof padAct_);
    }
    // The Pad field's text.
    static std::string padText(int v)
    {
        char t[48];
        if (v == kPadBusy) return kBusyText;
        if (v >= 1 && v <= 128) std::snprintf(t, sizeof t, "Note %d", v - 1);
        else if (v >= kPadLead0 && v < kPadFx0) std::snprintf(t, sizeof t, "Lead %d", v - kPadLead0 + 1);
        else if (v >= kPadFx0 && v < kPadMove0) std::snprintf(t, sizeof t, "%s", tp::app::fxName(v - kPadFx0));
        else if (v >= kPadMove0 && v < kPadCodes) std::snprintf(t, sizeof t, "%s", tp::app::moveName(v - kPadMove0));
        else return " ";
        return t;
    }

    // Sync: a start in the middle of a bar (a play from mid-song, after a locate) waits for the MPC's next bar line, so
    // the piece's bar 1 is a bar of the song (T-1745, Dima's yes, 2026-11-21; a conductor's bar 1 is then the MPC's).
    // True: start now, `first` the tick before the first clock. A start on a bar line, or with no bars from the host,
    // starts at once, as before.
    static bool barReady(const mpc::Transport& t, int frames, long long& first)
    {
        const double barLen = t.timeSigDenominator > 0 ? t.timeSigNumerator * 4.0 / t.timeSigDenominator : 4.0;
        const double into = t.ppqPos - t.barStartPpq;
        if (!(barLen >= 0.25 && barLen <= 64.0) || !(into > 0.01 && into < barLen - 0.01)) return true;   // (on a bar line,
        const double next = t.barStartPpq + barLen;                                                        // or no bars)
        if (t.ppqPos + (double)frames * t.bpm / 60.0 / 44100.0 < next) return false;
        first = (long long)std::floor(next * 24.0) - 1;
        return true;
    }

    // Sync: the MPC's transport as the FM-1's MIDI clock in (24 a beat, start, stop).
    void clock(const mpc::ProcessContext& ctx)
    {
        if (unsyncPending_)
        {
            unsyncPending_ = false;
            tp::app::play(true);
        }
        if (!getBool(idxSync_)) { stoppedSent_ = false; return; }
        const mpc::Transport& t = ctx.transport;
        const bool run = t.playing && t.ppqValid;
        if (run && !clockRunning_)
        {
            long long first = (long long)std::floor(t.ppqPos * 24.0) - 1;
            if (!barReady(t, ctx.numFrames, first)) return;   // (a start mid-bar: Start on the bar line)
            tp::app::midiIn(0x0F | 0xFAu << 8);   // Start: the piece from the top, on the MPC's play
            clockRunning_ = true;
            lastTick_ = first;
        }
        else if (!run && clockRunning_)
        {
            tp::app::midiIn(0x0F | 0xFCu << 8);
            clockRunning_ = false;
        }
        else if (!run && !stoppedSent_)
        {
            tp::app::midiIn(0x0F | 0xFCu << 8);   // Sync on with the MPC stopped: rest until it plays
        }
        stoppedSent_ = !run;
        if (!run) return;
        const double end = t.ppqPos + (double)ctx.numFrames * t.bpm / 60.0 / 44100.0;
        long long last = (long long)std::floor(end * 24.0);
        if (last - lastTick_ > 48) lastTick_ = last - 48;   // (a jump in the song: no burst of clocks)
        for (; lastTick_ < last; ++lastTick_) tp::app::midiIn(0x0F | 0xF8u << 8);
    }

    // The FM-1's time runs on as audio is needed: each simulated millisecond renders a 256-frame block
    // when one falls due, then runs the app.
    void render(mpc::ProcessContext& ctx)
    {
        float* outL = ctx.outputs[0];
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : nullptr;
        int done = 0;
        while (done < ctx.numFrames)
        {
            if (pos_ >= tp::kChunk)
            {
                while (!tp::app::stepMs(buf_)) {}
                pos_ = 0;
                ++chunks_;
                snapshot();   // (every block: a save is never behind what plays)
            }
            int n = ctx.numFrames - done;
            if (n > tp::kChunk - pos_) n = tp::kChunk - pos_;
            for (int i = 0; i < n; ++i)
            {
                outL[done + i] = (float)buf_[2 * (pos_ + i)] * tp::kOutScale;
                if (outR != nullptr) outR[done + i] = (float)buf_[2 * (pos_ + i) + 1] * tp::kOutScale;
            }
            pos_ += n;
            done += n;
        }
    }

    // The screen's model, once a block: the app's readouts and what it logged since the last block (all of it in
    // one call, so a hand's action and the values it moved stay together), over the block's time.
    void watch(int frames)
    {
        using namespace tp::app;
        Event events[kNumEvents];
        tp::sky::SkyInput in;
        in.playing = playing();
        in.held = held();
        for (int q = 0; q < 2; ++q) in.hits[q] = seqHits(q), in.reps[q] = seqReps(q);
        for (int l = 0; l < kPlanets; ++l) in.heard[l] = layerHeard(l), in.act[l] = layerActivity(l);
        in.numEvents = takeEvents(events, kNumEvents);
        in.events = events;
        in.piece = piece();
        skyCarry_ += (long long)frames * 1000;   // (in milliseconds, the part of one carried on)
        const int ms = (int)(skyCarry_ / 44100);
        skyCarry_ %= 44100;
        sky_.advance(in, ms);
        logHot_ = logHot_ > frames ? logHot_ - frames : 0;
        for (int l = 0; l < kLogLines; ++l)   // a line's new text, where formatValue() finds it, before its serial is sent
            if (sky_.logSerial(l) != logSeen_[l])
            {
                logSeen_[l] = sky_.logSerial(l);
                publishLog(logSeen_[l] % kLogSlots, sky_.logText(l));
                if (l == 0) logHot_ = sky_.logText(0)[0] != 0 ? kLogHotFrames : 0;   // (a new event, or the lines cleared)
            }
    }

    // The log's texts by serial (modulo kLogSlots), written on the audio thread and read by formatValue() on the
    // MPC's: words under a sequence count (odd while a text is being written; a reader retries if it moved).
    void publishLog(int slot, const char* text)
    {
        uint32_t w[kLogWords] = {};
        std::memcpy(w, text, strnlen(text, tp::sky::kTextMax - 1));
        const uint32_t s0 = logSeq_.load();
        logSeq_.store(s0 + 1);
        for (int i = 0; i < kLogWords; ++i) logText_[slot][i].store(w[i]);
        logSeq_.store(s0 + 2);
    }
    std::string logLine(int slot) const
    {
        char t[tp::sky::kTextMax + 1] = {};
        for (int tries = 0; tries < 8; ++tries)
        {
            const uint32_t s0 = logSeq_.load();
            if (s0 & 1u) continue;
            uint32_t w[kLogWords];
            for (int i = 0; i < kLogWords; ++i) w[i] = logText_[slot][i].load();
            if (logSeq_.load() != s0) continue;
            std::memcpy(t, w, sizeof w);
            break;
        }
        return t[0] != 0 ? t : " ";
    }

    // The display values now: the status line's, then the screen's, each within its range.
    struct Status { int v[kNumDisplay]; operator const int*() const { return v; } };
    Status status() const
    {
        using namespace tp::app;
        Status s;
        int* v = s.v;
        v[kDPad] = lastPad_, v[kDChord] = chordCode(), v[kDHold] = held() ? 1 : 0, v[kDLock] = locked() ? 1 : 0;
        v[kDSection] = section() + 1;
        v[kDPhrase] = phraseBar() + 1;
        v[kDEvolve] = evolveChance();
        v[kDNext] = nextChordCode();
        v[kDNextIn] = barsToNextChord();
        const int pos = progPos(), count = progCount();
        if (!loopMode()) v[kDProg] = 1 + walkCharacter() % EN_NWALK;
        else v[kDProg] = pos < 0 || count < 1 ? 0 : 4 + pos * 8 + count - 1;
        bool last = false;
        const int dots = ringDots(&last);
        v[kDRing] = dots < 0 ? 0 : (held() ? 67 : last ? 34 : 1) + (dots > 32 ? 32 : dots);
        for (int i = 0; i < kSlots; ++i)
        {
            v[kDSlot + i] = i == pos ? 3 : (int)slotState(i);
            v[kDSlotName + i] = slotChordCode(i);
        }
        for (int i = 0; i < kPucks; ++i)
        {
            const tp::sky::Puck p = sky_.puck(i);
            v[kDPx + i] = (int)std::lround(p.x * (float)kPuckUnit);
            v[kDPy + i] = (int)std::lround(p.y * (float)kPuckUnit);
        }
        for (int l = 0; l < kPlanets; ++l)
        {
            v[kDPlanet + l] = sky_.planetFrame(l);
            const float d = filterDrift(l);   // (octaves; not a number: none)
            const int tenths = d >= 4.5f ? kDriftMax : d <= -4.5f ? -kDriftMax : d == d ? (int)std::lround(d * 10.0f) : 0;
            const int way = filterHeading(l);
            const bool out = way != 0 && (d > 0.0f) == (way > 0);   // (ui.c move_state: further out, "MOVING")
            v[kDDrift + l] = out || tenths == 0 ? tenths : tenths + (tenths > 0 ? kDriftStill : -kDriftStill);
        }
        for (int l = 0; l < kLogLines; ++l) v[kDLog + l] = sky_.logSerial(l) % kLogSlots;
        // (the newest line while it is new: on the accent's label, and the cream one blank, so one of them shows)
        v[kDLog1Hot] = logHot_ > 0 ? v[kDLog] : -1;
        if (logHot_ > 0) v[kDLog] = -1;
        int dens = densityShift();
        dens = dens > kDensityMax ? kDensityMax : dens < -kDensityMax ? -kDensityMax : dens;
        v[kDDensity] = densityHeading() > 0 || dens == 0 ? dens : dens + (dens > 0 ? kDensityStill : -kDensityStill);
        const int lane = knobValue(kKnobLane);   // (ui.c lane_seq: lanes 1 and 3 are Sequence 2's)
        v[kDStep] = playing() ? loopStep(lane == 1 || lane == 3 ? 1 : 0) : 16;
        v[kDExt] = external() ? 1 : 0;
        v[kDSource] = 0;
        v[kDSolo] = soloed() ? 1 : 0;
        v[kDTempo] = external() ? -1 : par(P_TEMPO);
        for (int k = 0; k < kNumDisplay; ++k)
        {
            const DispRange r = dispRange(k);
            v[k] = v[k] < r.lo ? r.lo : v[k] > r.hi ? r.hi : v[k];
        }
        return s;
    }

    // Display value k as the MPC holds it now.
    int shown(int k) const
    {
        const float plain = get(kDispPad + k);
        return (int)std::lround(isPuck(k) ? plain * (float)kPuckUnit : plain);
    }
    // Display value k to the MPC (it redraws only what it is told of).
    void put(int k, int v)
    {
        displaySent_[k].store(v, std::memory_order_release);   // (before the write: it comes back to parameterChanged)
        sinceSent_[k] = 0;
        const int i = kDispPad + k;
        setNormalised(i, param(i).toNormalised(isPuck(k) ? (float)v / (float)kPuckUnit : (float)v));
        notifyHostParameterChanged(i);
    }

    // The display values that changed since they were last sent, to the MPC, `budget` of them at most; returns how
    // many. In this order: a value the MPC wrote over and the status line, the screen's texts and states, then the
    // particles. The planets and the drifts go 10 times a second at most; the particles that moved every 200 ms, all
    // in one block (R23: the MPC repaints their pads once). What does not fit waits for the next block.
    // `frames`: the block's length, which is the clock for all of this.
    int sendDisplay(const int* now, int budget, int frames)
    {
        sinceFix_ = sinceFix_ < kFixFrames ? sinceFix_ + frames : kFixFrames;
        if (displayReset_.exchange(false, std::memory_order_acq_rel))   // (a boot, a load: every value again)
            for (std::atomic<int>& v : displaySent_) v.store(INT_MIN, std::memory_order_relaxed);
        if (sinceFix_ >= kFixFrames && displayFix_.exchange(false, std::memory_order_acq_rel))
        {   // (paced: should the MPC ever write back a rounded value, this must not run every block)
            sinceFix_ = 0;
            for (int k = 0; k < kNumDisplay; ++k)
                if (fixOne_[k].exchange(false, std::memory_order_acq_rel)) displaySent_[k].store(INT_MIN, std::memory_order_relaxed);
        }
        for (int& f : sinceSent_) f = f < kPaceFrames ? f + frames : kPaceFrames;
        int sent = 0;
        for (int k = 0; k < kNumDisplay && sent < budget; ++k)
        {
            const int was = displaySent_[k].load(std::memory_order_relaxed);
            if (isPuck(k) || now[k] == was) continue;
            if (isPaced(k) && was != INT_MIN && sinceSent_[k] < kPaceFrames) continue;
            put(k, now[k]);
            ++sent;
        }
        puckClock_ = puckClock_ < kPuckFrames ? puckClock_ + frames : kPuckFrames;
        if (puckClock_ >= kPuckFrames)
        {
            int moved = 0;   // (all of them or none: what does not fit whole keeps its turn for the next block)
            for (int k = kDPx; k < kDPlanet; ++k) moved += puckMoved(now, k) ? 1 : 0;
            if (sent + moved <= budget)
            {
                for (int k = kDPx; k < kDPlanet; ++k)
                    if (puckMoved(now, k)) put(k, now[k]), ++sent;
                puckClock_ = 0;
            }
        }
        return sent;
    }
    bool puckMoved(const int* now, int k) const
    {
        const int was = displaySent_[k].load(std::memory_order_relaxed);
        return was == INT_MIN || std::abs(now[k] - was) > kPuckMove;
    }

    // Another instance owns the engine: the Pad field says so, and every other display value rests. Only what the
    // MPC does not hold already is sent: a new instance's one message, or a loaded project's values put to rest.
    void sendBusy()
    {
        int sent = 0;
        for (int k = 0; k < kNumDisplay && sent < kMaxSends; ++k)
        {
            const int want = k == kDPad ? kPadBusy : dispRange(k).rest;
            if (shown(k) == want) continue;
            put(k, want);
            ++sent;
        }
    }

    void addDisplay(mpc::Param p)
    {
        p.automatable = false;
        addParam(p);
    }

    // What the app changed by itself (a piece, Evolve, a shake, the engine's macros) back to the MPC.
    void mirror(int budget)
    {
        using namespace tp;
        int sent = 0;
        for (int k = 0; k < kNumControls && sent < budget; ++k)
        {
            const int i = (mirrorFrom_ + k) % kNumControls;
            const ParamDef& d = kParams[i];
            int v;
            switch (d.kind)
            {
                case Kind::Knob: v = app::knobValue(d.code); break;
                case Kind::Voice: v = app::par(d.code); break;
                case Kind::Piece: v = app::piece(); break;
                case Kind::Walk: v = app::walkValue(); break;
                case Kind::Switch: v = switchValue(d.code) ? 1 : 0; break;
                case Kind::Action: v = 0; break;
                case Kind::Step: v = app::step(d.code) ? 1 : 0; break;
                case Kind::Accent: v = app::accent(d.code) ? 1 : 0; break;
                case Kind::ChordRoot: v = app::chordRoot(); break;
                default: continue;
            }
            if (v == appVal_[i].load(std::memory_order_relaxed) || dirty_[i].load(std::memory_order_acquire) ||
                (int32_t)(chunks_ - settle_[i]) < 0)
                continue;
            appVal_[i].store(v, std::memory_order_release);
            setNormalised(i, param(i).toNormalised((float)(isChoice(i) ? v - lo_[i] : v)));
            notifyHostParameterChanged(i);
            ++sent;
            mirrorFrom_ = (i + 1) % kNumControls;
        }
    }

    void snapshot()
    {
        // A sequence count: even when stable, its half's parity names the latest copy. The writer fills the
        // other copy while the count is odd; a reader retries if the count moved while it copied.
        const uint32_t s0 = snapSeq_.load(std::memory_order_relaxed);
        const int k = 1 - ((int)(s0 >> 1) & 1);
        snapSeq_.store(s0 + 1, std::memory_order_release);
        snapLen_[k] = tp::app::snapshot(snap_[k], tp::app::kStoreMax);
        snapSeq_.store(s0 + 2, std::memory_order_release);
    }

    tp::sky::Sky sky_;
    tp::EngineLease lease_;
    std::atomic<bool> booted_{false};
    std::atomic<bool> hostKnows_{false};   // the MPC's values are this instance's (not a new instance's defaults)

    // parameters: what the app last showed (sent back), and what the MPC asked for
    int lo_[tp::kNumControls] = {};
    std::atomic<int> appVal_[tp::kNumControls];
    std::atomic<int> wanted_[tp::kNumControls];
    std::atomic<bool> dirty_[tp::kNumControls];
    int mirrorFrom_ = 0;
    uint32_t settle_[tp::kNumControls] = {};   // (a switch keeps the MPC's value till this chunk)
    int lastPad_ = 0;     // (the status line's Pad: kPad*)
    std::atomic<int> displaySent_[kNumDisplay];   // each display value as last sent (INT_MIN: never, or to send again)
    std::atomic<bool> displayReset_{true};   // send every display value again (a boot, a load)
    std::atomic<bool> displayFix_{false};    // the MPC wrote over a display value: one of fixOne_
    std::atomic<bool> fixOne_[kNumDisplay];
    int sinceFix_ = kFixFrames;
    int sinceSent_[kNumDisplay] = {};        // frames since each was sent, up to kPaceFrames
    int puckClock_ = 0;                      // frames since the particles went
    int actionSends_ = 0;                    // one-shots sent back to Off in this block
    std::atomic<bool> loadHold_{false};      // a project loaded: hold the display
    int hold_ = 0;                           // ... for this many frames more

    // the star-chart screen's model, and the log's texts for formatValue()
    long long skyCarry_ = 0;                 // the part of a millisecond not yet given to the Sky, in frames x 1000
    int logSeen_[kLogLines] = {};            // each line's serial as published
    int logHot_ = 0;                         // frames the newest line stays hot (kDLog1Hot)
    std::atomic<uint32_t> logSeq_{0};
    std::atomic<uint32_t> logText_[kLogSlots][kLogWords];
    int idxSync_ = 0, idxKeyMode_ = 0, idxPadBase_ = 0;

    // the notes held, and what each one's press did
    uint8_t noteCount_[128] = {};
    int8_t padAct_[128];

    // Sync
    bool syncWas_ = false, unsyncPending_ = false, clockRunning_ = false, stoppedSent_ = false;
    long long lastTick_ = 0;

    // audio
    int32_t buf_[2 * tp::kChunk] = {};
    int pos_ = tp::kChunk;
    uint32_t chunks_ = 0;

    // the project: this instance's (loaded, or none), one waiting to load, and the copies for saving
    uint8_t ownBlob_[tp::app::kStoreMax] = {};
    int ownLen_ = -1;
    uint8_t pendingBlob_[tp::app::kStoreMax] = {};
    int pendingLen_ = -1;
    std::atomic<bool> loadPending_{false};
    uint8_t snap_[2][tp::app::kStoreMax] = {};
    int snapLen_[2] = {-1, -1};
    std::atomic<uint32_t> snapSeq_{0};
};

// This macro creates the exported VSTPluginMain entry point. Keep it last.
MPC_PLUGIN_ENTRY(ThirdPlanet)
