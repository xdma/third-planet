// SPDX-License-Identifier: GPL-3.0-only
// Third Planet's readouts for the star-chart screen (app_api.h, "Readouts for the star-chart screen"):
//   1. each readout equals the engine global it reads, while a piece plays;
//   2. Evolve's chance is ui.c nudge_tick's, and 0 while locked, held or resting;
//   3. a slot's chord number names exactly what ui.c's slot_label() does, on every piece and walk character;
//   4. the next chord: Endless, the first slot from 1 that isn't a gap; Loop, the next slot that isn't a gap; and
//      against the engine, with gaps between: the chord the engine actually brings, after barsToNextChord() bars;
//   5. the ring is ui.c ring_dots' at random moments, stays put while held, and is absent at Drift Hold;
//   6. the event ring (takeEvents): a hand nudge and a shake are "You" with the parameters they moved, Evolve's
//      nudges are "Evolve" with exactly the parameters nudge_tick moved on a phrase edge (also in the frame of a hand
//      nudge, or of a panel knob turned), a piece change is one "Piece", and knobs, pads, boot, a loaded store and
//      Motion log nothing; 100 events leave the newest 64, in order;
//   7. the Sky model (sky.h): particles launch, move and park as ui.c's orbits do; planet frames follow activity,
//      mutes and nudge flashes; the log keeps the newest three lines; every event formats within 31 bytes of valid
//      UTF-8, the realistic ones whole; reset() clears it all.
//   c++ -std=c++17 -O2 -I plugins/ThirdPlanet/src -I deps/mpc-plugin-sdk/framework \
//       plugins/ThirdPlanet/tests/test_screen.cpp plugins/ThirdPlanet/src/sky.cpp plugins/ThirdPlanet/src/app_unit.cpp \
//       plugins/ThirdPlanet/src/engine_unit.cpp plugins/ThirdPlanet/src/power.cpp \
//       -o plugins/ThirdPlanet/build/tests/test_screen
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "app_api.h"
#include "engine.h"
#include "sky.h"

using namespace tp::app;

namespace {

int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); fails++; } } while (0)

int32_t chunk[2 * tp::kChunk];
void run(int ms)
{
    for (int i = 0; i < ms; i++)
        stepMs(chunk);
}

void start(int pc)
{
    boot();
    setPiece(pc);
    play(true);
    run(50);
}

std::string name(int code)
{
    char b[kChordTextMax];
    chordText(code, b);
    return b;
}

// ui.c ring_dots, from its inputs (unfrozen).
int ringModel(bool* last)
{
    const int db = en_drift_bars(par(P_DRIFT));
    *last = false;
    if (!db) return -1;
    const int k = (int)(en_drift * (float)db + 0.5f);
    float p = ((float)k + (float)(en_drum_pos[0] + 1u) * (1.0f / 16.0f)) / (float)db;
    p = p < 0.0f ? 0.0f : p > 1.0f ? 1.0f : p;
    *last = k >= db - 1;
    return (int)(p * 32.0f);
}

void readouts_follow_engine()
{
    start(0);
    std::set<int> bars;
    for (int t = 0; t < 20000; t++)
    {
        stepMs(chunk);
        bars.insert(phraseBar());
        if (t % 97) continue;
        CHECK(section() == en_section, "section %d vs %d", section(), en_section);
        CHECK(section() >= 0 && section() < EN_NSEC, "section %d", section());
        CHECK(phraseBar() == en_phrase_bar, "phraseBar");
        for (int l = 0; l < kNumLayers; l++)
        {
            CHECK(layerActivity(l) == en_layer_act[l], "layerActivity %d", l);
            CHECK(filterDrift(l) == en_fo[l], "filterDrift %d", l);
            CHECK(layerHeard(l) == (!muted(l) && !(soloed() && !soloed(l))), "layerHeard %d", l);
        }
        CHECK(densityShift() == en_dd_shift, "densityShift");
        for (int s = 0; s < 2; s++)
        {
            CHECK(seqHits(s) == en_seq_hits[s], "seqHits %d", s);
            CHECK(seqReps(s) == en_seq_reps[s], "seqReps %d", s);
            CHECK(loopStep(s) == en_loop_pos[s], "loopStep %d", s);
        }
        CHECK(progPos() == en_prog_pos, "progPos");
        CHECK(loopMode() == ((par(P_PMODE) & EN_PLOOP) != 0), "loopMode");
        CHECK(walkCharacter() == (par(P_PMODE) & 3), "walkCharacter");
        int pc = 0;
        for (int i = 0; i < EN_NPROG; i++)
            if (par(P_CQ0 + i) || par(P_CR0 + i)) pc = i + 1;
        CHECK(progCount() == pc, "progCount %d vs %d", progCount(), pc);
        bool last;
        const int rd = ringDots(&last);
        const int btc = barsToChange();
        if (rd >= 0)
        {
            const int db = en_drift_bars(par(P_DRIFT));
            CHECK(btc >= 0 && btc <= db, "barsToChange %d of %d", btc, db);
            CHECK(last == (btc <= 1), "lastBar %d, barsToChange %d", (int)last, btc);
        }
        else
            CHECK(btc == -1, "barsToChange %d at Drift Hold", btc);
    }
    for (int b = 0; b < 8; b++)
        CHECK(bars.count(b), "phraseBar never %d", b);
    // a layer muted, a layer soloed
    setMute(2, true);
    CHECK(!layerHeard(2) && layerHeard(0), "muted layer heard");
    setMute(2, false);
    setSolo(1, true);
    CHECK(layerHeard(1) && !layerHeard(0) && !layerHeard(4), "solo");
    setSolo(1, false);
    // out of range
    CHECK(layerActivity(-1) == 0.0f && layerActivity(kNumLayers) == 0.0f, "layerActivity range");
    CHECK(!layerHeard(-1) && !layerHeard(kNumLayers), "layerHeard range");
    CHECK(filterDrift(9) == 0.0f && seqHits(2) == 0 && seqReps(-1) == 0 && loopStep(2) == 0, "ranges");
    CHECK(slotState(-1) == kSlotGap && slotState(EN_NPROG) == kSlotGap && slotChordCode(EN_NPROG) == 0, "slot range");
}

void evolve_chance()
{
    start(0);
    struct { int ev, want; } cases[] = {{100, 40}, {70, 20}, {40, 0}, {0, 0}, {41, 0}, {43, 2}};
    for (auto c : cases)
    {
        knob(P_EVOLVE, c.ev);
        run(2);
        CHECK(evolveChance() == c.want, "Evolve %d: %d, want %d", c.ev, evolveChance(), c.want);
    }
    knob(P_EVOLVE, 100);
    setLock(true);
    run(20);
    CHECK(evolveChance() == 0, "Lock: %d", evolveChance());
    setLock(false);
    run(20);
    CHECK(evolveChance() == 40, "Lock off: %d", evolveChance());
    setHold(true);
    run(20);
    CHECK(held() && evolveChance() == 0, "Hold: %d", evolveChance());
    setHold(false);
    run(20);
    CHECK(evolveChance() == 40, "Hold off: %d", evolveChance());
    play(false);
    run(20);
    CHECK(evolveChance() == 0, "resting: %d", evolveChance());
}

int seenKind[3];
void checkSlots(const char* where)
{
    char b[kChordTextMax];
    for (int i = 0; i < EN_NPROG; i++)
    {
        const bool gap = !par(P_CQ0 + i) && !par(P_CR0 + i), walkSlot = !par(P_CQ0 + i) && par(P_CR0 + i);
        const SlotState want = gap ? kSlotGap : walkSlot ? kSlotWalk : kSlotWritten;
        CHECK(slotState(i) == want, "%s slot %d state %d want %d", where, i, slotState(i), want);
        const std::string got = name(slotChordCode(i));
        seenKind[want]++;
        if (gap)
        {
            CHECK(slotChordCode(i) == 0 && got.empty(), "%s slot %d gap code %d", where, i, slotChordCode(i));
            continue;
        }
        CHECK(slotChordCode(i) > 0 && slotChordCode(i) < kChordCodes, "%s slot %d code %d", where, i, slotChordCode(i));
        slotLabel(i, b);
        CHECK(got == b, "%s slot %d (%s): \"%s\" vs ui.c \"%s\"", where, i, walkSlot ? "walk" : "written", got.c_str(), b);
    }
}

void slot_codes_name_like_ui()
{
    char where[64];
    for (int pc = 0; pc < EN_NPIECES; pc++)
        for (int w = 0; w < EN_NWALK; w++)
        {
            start(pc);
            walk(w);
            run(100);
            std::snprintf(where, sizeof where, "piece %d walk %d", pc, w);
            checkSlots(where);
            run(15000);   // (Endless moves the slots up; the walk writes slot 8)
            std::snprintf(where, sizeof where, "piece %d walk %d later", pc, w);
            checkSlots(where);
            // written slots and a gap
            knob(kKnobSlot, 2);
            setChordRoot(4);
            knob(kKnobType, 3);
            knob(kKnobSlot, 5);
            setChordRoot(-1);
            run(20);
            std::snprintf(where, sizeof where, "piece %d walk %d written", pc, w);
            checkSlots(where);
        }
    CHECK(seenKind[kSlotGap] > 0 && seenKind[kSlotWalk] > 100 && seenKind[kSlotWritten] > 0, "slots seen: %d gaps, %d walk, %d written",
          seenKind[kSlotGap], seenKind[kSlotWalk], seenKind[kSlotWritten]);
}

void next_chord()
{
    start(1);
    knob(kKnobLoop, 1);
    for (int i = 0; i < EN_NPROG; i++)
    {
        knob(kKnobSlot, i);
        setChordRoot(i < 3 ? (i * 5) % 12 : -1);
    }
    knob(P_DRIFT, 0);   // (2 bars a chord)
    run(50);
    CHECK(loopMode() && progCount() == 3, "Loop with 3 slots: %d %d", (int)loopMode(), progCount());
    CHECK(slotState(3) == kSlotGap, "slot 4 a gap");
    std::set<int> seen;
    int pos = progPos(), changes = 0;
    for (int t = 0; t < 120000 && changes < 7; t++)
    {
        const int want = slotChordCode(pos < 0 ? 0 : (pos + 1) % 3);
        CHECK(nextChordCode() == want, "Loop at %d: next %d want %d", pos, nextChordCode(), want);
        if (nextChordCode() != want) break;
        const int predicted = nextChordCode();
        stepMs(chunk);
        if (progPos() != pos)
        {
            CHECK(progPos() == (pos + 1) % 3, "Loop went %d -> %d", pos, progPos());
            CHECK(chordCode() == predicted, "Loop: the change brought %s, predicted %s", name(chordCode()).c_str(),
                  name(predicted).c_str());
            pos = progPos();
            seen.insert(pos);
            changes++;
        }
    }
    CHECK(changes >= 6 && seen.size() == 3, "Loop changes %d, positions %zu", changes, seen.size());
    // a gap in the middle is skipped by the readout
    knob(kKnobSlot, 3);
    setChordRoot(9);
    knob(kKnobSlot, 1);
    setChordRoot(-1);
    run(20);
    CHECK(progCount() == 4 && slotState(1) == kSlotGap, "progCount %d", progCount());
    int seenSkip = 0, seenWrap = 0, seenGap = 0;
    for (int t = 0; t < 60000; t++)
    {
        stepMs(chunk);
        const int p = progPos();
        if (p == 0) { CHECK(nextChordCode() == slotChordCode(2), "skip the gap"); seenSkip++; }
        if (p == 1) { CHECK(nextChordCode() == slotChordCode(2), "from the gap"); seenGap++; }
        if (p == 3) { CHECK(nextChordCode() == slotChordCode(0), "wrap"); seenWrap++; }
    }
    CHECK(seenSkip && seenGap && seenWrap, "positions seen %d %d %d", seenSkip, seenGap, seenWrap);

    // Endless: slot 1's code, and the change brings it
    start(2);
    knob(P_DRIFT, 0);
    run(100);
    CHECK(!loopMode(), "Endless");
    changes = 0;
    for (int t = 0; t < 120000 && changes < 4; t++)
    {
        const int predicted = nextChordCode();
        CHECK(predicted == slotChordCode(1), "Endless: next %d, slot 1 %d", predicted, slotChordCode(1));
        if (predicted != slotChordCode(1)) break;
        const uint32_t g = en_prog_gen;
        stepMs(chunk);
        if (en_prog_gen != g)
        {
            run(20);   // (the UI takes the moved slots at its next frame)
            CHECK(name(slotChordCode(0)) == name(predicted), "Endless: slot 1 moved to 0 as %s, predicted %s",
                  name(slotChordCode(0)).c_str(), name(predicted).c_str());
            CHECK(name(chordCode()) == name(predicted), "Endless: the change brought %s, predicted %s",
                  name(chordCode()).c_str(), name(predicted).c_str());
            changes++;
        }
    }
    CHECK(changes >= 3, "Endless: %d changes seen", changes);
}

// The engine's own account of the next chord: at sampled moments, nextChordCode() and barsToNextChord() are
// recorded; the engine then runs, and the first change that lands on a slot that isn't a gap (a gap holds the
// chord) must come after exactly that many bar lines, and sound that chord. Bars remaining = the bar lines from the
// moment to the change, the change's own counted (in a chord's last bar: 1), as barsToChange() counts.
struct Pending { std::string name; int bars, barAt, t, pb, pos; };
void checkAgainstEngine(const char* where, int ms, int* resolved)
{
    std::vector<Pending> pend;
    int barCount = 0, quiet = 0, landed = -1;
    int lastBar = phraseBar(), lastPos = en_prog_pos;
    uint32_t lastGen = en_prog_gen;
    for (int t = 0; t < ms; t++)
    {
        stepMs(chunk);
        if (phraseBar() != lastBar) { lastBar = phraseBar(); barCount++; }
        bool change = false, onGap = false;
        if (loopMode())
        {
            if (en_prog_pos != lastPos)
            {
                change = true;
                const int q = en_prog_pos;
                onGap = q >= 0 && !en_prog_slot[q][0] && !en_prog_slot[q][1];
            }
        }
        else if (en_prog_gen != lastGen)
        {
            change = true;
            onGap = !en_prog_slot[0][0] && !en_prog_slot[0][1];
        }
        lastPos = en_prog_pos, lastGen = en_prog_gen;
        if (change)
        {
            quiet = 20;   // (the UI takes the engine's slots at its next frame)
            if (!onGap) landed = barCount;
        }
        if (landed >= 0 && quiet == 1)   // the chord landed: every moment sampled before it is resolved
        {
            const std::string now = name(chordCode());
            for (const Pending& p : pend)
            {
                CHECK(p.bars == landed - p.barAt, "%s: predicted %d bars, the chord came after %d (at %d ms, bar %d, slot %d)",
                      where, p.bars, landed - p.barAt, p.t, p.pb, p.pos);
                CHECK(p.name == now, "%s: predicted %s, the change brought %s", where, p.name.c_str(), now.c_str());
                (*resolved)++;
            }
            pend.clear();
            landed = -1;
        }
        if (quiet) { quiet--; continue; }
        if (t % 211 == 0 && barCount >= 1)   // (an edit of the slot playing restarts its bars mid-bar, amb.c
                                             // slot_play; en_drift shows it from the next bar line)
        {
            const int b = barsToNextChord();
            CHECK(b >= 1, "%s: barsToNextChord %d", where, b);
            pend.push_back({name(nextChordCode()), b, barCount, t, phraseBar(), progPos()});
        }
    }
}

void next_chord_engine()
{
    // Loop, slots 1, 2 written, 3 a gap, 4 written
    start(4);
    knob(kKnobLoop, 1);
    knob(P_DRIFT, 0);   // (2 bars a chord)
    const int roots[EN_NPROG] = {0, 5, -1, 9, -1, -1, -1, -1};
    for (int i = 0; i < EN_NPROG; i++)
    {
        knob(kKnobSlot, i);
        setChordRoot(roots[i]);
    }
    run(50);
    CHECK(progCount() == 4 && slotState(2) == kSlotGap, "Loop setup: %d", progCount());
    int resolved = 0;
    checkAgainstEngine("Loop, gap at 3", 90000, &resolved);
    CHECK(resolved > 50, "Loop: %d moments resolved", resolved);
    // Drift 4 bars, the same
    knob(P_DRIFT, 1);
    run(50);
    resolved = 0;
    checkAgainstEngine("Loop, 4 bars", 90000, &resolved);
    CHECK(resolved > 30, "Loop 4 bars: %d moments resolved", resolved);

    // Endless with slot 1 a gap (and, after the change, slot 0: the chord held a change longer)
    for (int pc : {0, 5})
    {
        start(pc);
        knob(P_DRIFT, 0);
        knob(kKnobSlot, 1);
        setChordRoot(-1);
        knob(kKnobSlot, 4);
        setChordRoot(-1);
        run(50);
        CHECK(!loopMode() && slotState(1) == kSlotGap, "Endless setup");
        CHECK(nextChordCode() == slotChordCode(2) && barsToNextChord() == barsToChange() + 2,
              "Endless, slot 1 a gap: next %d (slot 2 %d), bars %d (to change %d)", nextChordCode(), slotChordCode(2),
              barsToNextChord(), barsToChange());
        resolved = 0;
        checkAgainstEngine("Endless, gaps at 1 and 4", 60000, &resolved);
        CHECK(resolved > 50, "Endless: %d moments resolved", resolved);
    }

    // Hold: changes paused
    setHold(true);
    run(30);
    CHECK(held() && barsToNextChord() == -2, "Hold: barsToNextChord %d", barsToNextChord());
    run(20000);
    CHECK(barsToNextChord() == -2 && barsToChange() == 0, "Hold, later: %d, barsToChange %d", barsToNextChord(),
          barsToChange());
    setHold(false);
    run(30);
    // (the chord's bars ran out while held: it changes at the next bar line)
    CHECK(barsToChange() == 1 && barsToNextChord() >= 1, "Hold off: %d, barsToChange %d", barsToNextChord(), barsToChange());
    resolved = 0;
    checkAgainstEngine("Endless, after Hold", 20000, &resolved);
    CHECK(resolved > 10, "after Hold: %d moments resolved", resolved);
    // Drift Hold
    knob(P_DRIFT, 6);
    run(30);
    CHECK(barsToNextChord() == -1, "Drift Hold: %d", barsToNextChord());
}

void ring_matches_ui()
{
    start(3);
    std::srand(7);
    int checked = 0, held_checks = 0;
    for (int n = 0; n < 100; n++)
    {
        run(1 + std::rand() % 3000);
        bool last, mlast;
        const int got = ringDots(&last), want = ringModel(&mlast);
        CHECK(got == want && last == mlast, "ring %d/%d vs ui.c %d/%d", got, (int)last, want, (int)mlast);
        checked++;
        if (n % 10 == 9)   // a stretch of Hold: the ring stays as it was
        {
            setHold(true);
            int before = -2;
            bool blast = false;
            for (int t = 0; t < 30 && !held(); t++)
            {
                before = ringDots(&blast);   // (the last unfrozen reading)
                stepMs(chunk);
            }
            CHECK(held(), "Hold not taken");
            for (int t = 0; t < 4000; t += 500)
            {
                run(500);
                bool hl;
                const int h = ringDots(&hl);
                CHECK(h == before && hl == blast, "held ring %d/%d, was %d/%d", h, (int)hl, before, (int)blast);
                held_checks++;
            }
            setHold(false);
            run(30);
        }
    }
    CHECK(checked == 100 && held_checks > 0, "ring checks");
    // Drift Hold: no ring
    knob(P_DRIFT, 6);
    run(30);
    CHECK(en_drift_bars(par(P_DRIFT)) == 0, "Drift 6 should be Hold");
    bool last = true;
    CHECK(ringDots(&last) == -1 && !last, "Drift Hold: ring %d", ringDots(&last));
    CHECK(barsToChange() == -1, "Drift Hold: barsToChange %d", barsToChange());
}

// ------------------------------------------------------------------ the event ring ---
struct Pars { int16_t v[P_NPARAMS]; };
Pars pars()
{
    Pars s;
    for (int p = 0; p < P_NPARAMS; p++)
        s.v[p] = (int16_t)par(p);
    return s;
}
std::vector<Event> drain()
{
    Event e[64];
    const int n = takeEvents(e, 64);
    return std::vector<Event>(e, e + n);
}
bool slotParam(int p) { return (p >= P_CR0 && p <= P_CX7) || p == P_PMODE || p == P_SEQ2OCT; }
// What a nudge may move: ui.c NUDGE_PAR, by nudge layer; a shake's voice besides.
const std::set<int> kNudgePar[kNumNudge] = {
    {P_CATT, P_CCOL, P_CRES, P_DREL, P_PHASER, P_PEDAL}, {P_CUTOFF, P_DECAY, P_SACCENT}, {P_LTONE, P_LATT, P_VIB, P_GLIDE},
    {P_S2CUT, P_S2DEC}, {P_ECHO, P_FEEDBACK, P_REVERB, P_SIZE, P_CLOUD}, {P_NCOL, P_NGRAIN, P_NTUNE, P_NROOM}};
const int kShakeVoice[kNumNudge] = {P_CVOICE, P_SVOICE, P_LVOICE, P_S2VOICE, P_ETIME, P_NCHAR};

// The events of who `who` are exactly the parameters that differ between a and b (but the slots'), each once, with
// its values; `voice`: the parameter that must be a kEvVoice (-1 none).
void checkDiff(const std::vector<Event>& ev, Who who, const Pars& a, const Pars& b, int layer, int voice, const char* what)
{
    std::set<int> seen;
    for (const Event& e : ev)
    {
        if (e.who != who || (e.what != kEvParam && e.what != kEvVoice)) continue;
        CHECK(e.param >= 0 && e.param < P_NPARAMS, "%s: param %d", what, e.param);
        if (e.param < 0 || e.param >= P_NPARAMS) continue;
        CHECK(seen.insert(e.param).second, "%s: param %d twice", what, e.param);
        CHECK(e.from != e.to, "%s: param %d did not move (%d)", what, e.param, e.from);
        CHECK(e.from == a.v[e.param] && e.to == b.v[e.param], "%s: param %d %d>%d, was %d>%d", what, e.param, e.from, e.to,
              a.v[e.param], b.v[e.param]);
        CHECK(e.layer == layer, "%s: param %d layer %d, want %d", what, e.param, e.layer, layer);
        CHECK((e.what == kEvVoice) == (e.param == voice), "%s: param %d what %d", what, e.param, (int)e.what);
        CHECK(!slotParam(e.param), "%s: slot param %d", what, e.param);
    }
    for (int p = 0; p < P_NPARAMS; p++)
        if (a.v[p] != b.v[p] && !slotParam(p)) CHECK(seen.count(p), "%s: param %d moved %d>%d, no event", what, p, a.v[p], b.v[p]);
}
int count(const std::vector<Event>& ev, Who who)
{
    int n = 0;
    for (const Event& e : ev)
        n += e.who == who;
    return n;
}

void hand_nudge_is_you()
{
    start(0);
    knob(P_EVOLVE, 100);
    run(300);
    drain();
    int moved_total = 0;
    for (int l = 0; l < kNumNudge; l++)
        for (int shake = 0; shake < 2; shake++)
        {
            char what[32];
            std::snprintf(what, sizeof what, "%s %d", shake ? "shake" : "nudge", l);
            const Pars a = pars();
            nudgeLayer(l, shake != 0);
            const Pars b = pars();
            const std::vector<Event> ev = drain();
            CHECK(!ev.empty() && ev[0].who == kWhoYou && ev[0].what == (shake ? kEvShake : kEvNudge) && ev[0].layer == l,
                  "%s: first event", what);
            CHECK(count(ev, kWhoYou) == (int)ev.size(), "%s: not all You", what);
            for (size_t i = 1; i < ev.size(); i++)
            {
                CHECK(ev[i].what == kEvParam || ev[i].what == kEvVoice, "%s: event %d what %d", what, (int)i, (int)ev[i].what);
                CHECK(kNudgePar[l].count(ev[i].param) || (shake && ev[i].param == kShakeVoice[l]), "%s: param %d not the layer's",
                      what, ev[i].param);
            }
            checkDiff(ev, kWhoYou, a, b, l, shake ? kShakeVoice[l] : -1, what);
            if (shake) CHECK(a.v[kShakeVoice[l]] != b.v[kShakeVoice[l]], "%s: no new voice", what);
            if (!shake)
            {
                // (3-4 values, but a layer with fewer has them all, and a value at its end may stay)
                const int most = (int)kNudgePar[l].size() < 4 ? (int)kNudgePar[l].size() : 4;
                CHECK((int)ev.size() - 1 >= 1 && (int)ev.size() - 1 <= most, "%s: %d params", what, (int)ev.size() - 1);
                moved_total += (int)ev.size() - 1;
            }
            // nothing of it is Evolve's in the frames after (bars 1.. of the phrase: no nudge of Evolve's own is due)
            if (phraseBar() != 0 && phraseBar() != 7)
            {
                run(40);
                CHECK(drain().empty(), "%s: events in the frames after", what);
            }
        }
    CHECK(moved_total >= 10, "hand nudges moved %d params", moved_total);
    // the brief's case: the Sequence nudged, by hand
    start(2);
    drain();
    nudgeLayer(1, false);
    std::vector<Event> ev = drain();
    CHECK(ev.size() >= 4 && ev.size() <= 5 && ev[0].what == kEvNudge && ev[0].who == kWhoYou && ev[0].layer == 1, "nudge 1");
    for (size_t i = 1; i < ev.size(); i++)
        CHECK(ev[i].who == kWhoYou && ev[i].what == kEvParam && ev[i].from != ev[i].to, "nudge 1 line %d", (int)i);
    run(33);
    CHECK(count(drain(), kWhoEvolve) == 0, "Evolve after a hand nudge");
    // the other actions: one line each, and none for what already is
    struct { What what; int layer, from, to; } want[] = {
        {kEvHold, -1, 0, 1}, {kEvHold, -1, 1, 0}, {kEvLock, -1, 0, 1}, {kEvLock, -1, 1, 0}, {kEvPlay, -1, 1, 0}, {kEvPlay, -1, 0, 1},
        {kEvForget, -1, 0, 0}, {kEvNextChord, -1, 0, 0}, {kEvNewChords, -1, 0, 0}, {kEvMute, 3, 0, 1}, {kEvMute, 3, 1, 0},
        {kEvSolo, 4, 0, 1}, {kEvSolo, 4, 1, 0}, {kEvShakeAll, -1, 0, 0}, {kEvReset, -1, 0, 0}};
    // (Hold and Play are the engine's, taken at its next block: the same again counts as nothing only after it)
    setHold(true), run(10), setHold(true), setHold(false), run(10), setHold(false);
    setLock(true), setLock(true), setLock(false);
    play(false), run(10), play(false), play(true), run(10), play(true);
    forget(), nextChord(), newChords();
    setMute(3, true), setMute(3, true), setMute(9, true), setMute(3, false);
    setSolo(4, true), setSolo(4, false), setSolo(-1, true);
    nudgeLayer(-1, false), nudgeLayer(kNumNudge, true);
    shakeAll(), resetPiece();
    ev = drain();
    const int nwant = (int)(sizeof want / sizeof want[0]);
    CHECK((int)ev.size() == nwant, "actions: %d events, want %d", (int)ev.size(), nwant);
    for (int i = 0; i < nwant && i < (int)ev.size(); i++)
        CHECK(ev[i].who == kWhoYou && ev[i].what == want[i].what && ev[i].layer == want[i].layer && ev[i].from == want[i].from &&
                  ev[i].to == want[i].to && ev[i].param == -1,
              "action %d: what %d layer %d %d>%d param %d", i, (int)ev[i].what, ev[i].layer, ev[i].from, ev[i].to, ev[i].param);
}

// One ms, checked: Evolve's events are exactly what moved (but the slots'), on a phrase's first bar, just come.
// Returns Evolve's events.
struct Edge { int since0 = 1000; };   // ms since the phrase's bar was last seen not 0
std::vector<Event> evolveMs(Edge& g, const char* what)
{
    const Pars a = pars();
    stepMs(chunk);
    const Pars b = pars();
    const std::vector<Event> ev = drain();
    CHECK(count(ev, kWhoEvolve) == (int)ev.size(), "%s: an event not Evolve's", what);
    checkDiff(ev, kWhoEvolve, a, b, ev.empty() ? -1 : ev[0].layer, -1, what);
    if (!ev.empty())
    {
        CHECK(phraseBar() == 0 && g.since0 <= 17, "%s: not at a phrase edge (bar %d, %d ms)", what, phraseBar(), g.since0);
        CHECK(ev.size() <= 2, "%s: %d values", what, (int)ev.size());
        for (const Event& e : ev)
            CHECK(e.layer >= 0 && e.layer < kNumNudge && kNudgePar[e.layer].count(e.param), "%s: param %d layer %d", what, e.param, e.layer);
    }
    g.since0 = phraseBar() == 0 ? g.since0 + 1 : 0;
    return ev;
}

void evolve_is_evolve()
{
    start(4);
    knob(P_EVOLVE, 100);
    knob(P_TEMPO, 140);
    knob(P_MOTION, 100);   // (Motion moving its macros and Sequence 2's octave meanwhile: none of it Evolve's)
    drain();
    Edge g;
    int nudges = 0, phrases = 0, lastBar = phraseBar();
    for (int t = 0; phrases < 64 && t < 2000000; t++)
    {
        nudges += !evolveMs(g, "evolve").empty();
        if (phraseBar() == 0 && lastBar != 0) phrases++;
        lastBar = phraseBar();
    }
    CHECK(phrases == 64, "%d phrases", phrases);
    CHECK(nudges >= 10 && nudges <= 45, "Evolve nudged %d of 64 phrases (40%% of them)", nudges);
    // Lock, Hold, Evolve 40: it rests
    knob(P_EVOLVE, 40);
    for (int t = 0; t < 60000; t++)
        CHECK(evolveMs(g, "evolve 40").empty(), "Evolve 40 nudged");
}

// The ms of Evolve's first `want` nudges from a fixed start (the same every run: boot() clears the seeds).
void sameStart()
{
    start(1);
    knob(P_EVOLVE, 100);
    knob(P_TEMPO, 140);
    drain();
}
void same_frame()
{
    std::vector<int> at;
    std::vector<std::vector<Event>> base;
    {
        sameStart();
        Edge g;
        for (int t = 0; at.size() < 6 && t < 2000000; t++)
        {
            const std::vector<Event> ev = evolveMs(g, "same_frame base");
            if (!ev.empty()) at.push_back(t), base.push_back(ev);
        }
        CHECK(at.size() == 6, "only %d Evolve nudges", (int)at.size());
    }
    // a hand nudge in the ms of an Evolve nudge (the hand's moves the seed Evolve draws from: try each edge and layer
    // until Evolve nudges too, and once the same parameter)
    int both = 0, chained = 0;
    for (size_t k = 0; k < at.size(); k++)
        for (int l = 0; l < kNumNudge; l++)
        {
            sameStart();
            run(at[k]);
            drain();   // (Evolve's nudges before this one)
            const Pars a = pars();
            nudgeLayer(l, false);
            const Pars m = pars();
            stepMs(chunk);
            const Pars b = pars();
            const std::vector<Event> ev = drain();   // (drained once: the ring keeps who did what)
            std::vector<Event> you, evo;
            bool order = true;
            for (const Event& e : ev)
            {
                if (e.who == kWhoYou) order = order && evo.empty(), you.push_back(e);
                else evo.push_back(e);
            }
            CHECK(order, "same_frame: Evolve before You");
            CHECK(!you.empty() && you[0].what == kEvNudge && you[0].layer == l, "same_frame: the hand's nudge");
            CHECK(count(evo, kWhoEvolve) == (int)evo.size(), "same_frame: a third who");
            checkDiff(you, kWhoYou, a, m, l, -1, "same_frame you");
            checkDiff(evo, kWhoEvolve, m, b, evo.empty() ? -1 : evo[0].layer, -1, "same_frame evolve");
            both += !evo.empty();
            for (const Event& e : evo)
                for (const Event& y : you)
                    if (y.what == kEvParam && y.param == e.param)
                    {
                        CHECK(y.to == e.from, "same_frame: param %d %d>%d then %d>%d", e.param, y.from, y.to, e.from, e.to);
                        chained++;
                    }
        }
    CHECK(both >= 5, "same_frame: both in %d runs", both);
    CHECK(chained >= 1, "same_frame: no parameter moved by both");
    // a panel knob turned in Evolve's frame (ui.c input(), after nudge_tick): Evolve's events are what they were
    // without it, the knob's move nobody's
    static const int kViewBtn[] = {kENV, kARP, kLFO, kSEQ, kFX};
    int turned = 0;
    for (size_t k = 0; k < at.size(); k++)
    {
        sameStart();
        setButtons(1u << kViewBtn[k % 5]);   // a tap: the layer's page, its knobs the layer's sound
        run(60);
        setButtons(0);
        run(at[k] - 60);
        drain();
        const Pars a = pars();
        setAccelOff(true);
        for (int e = kK1; e <= kK4; e++)
            turn(e, 3);
        stepMs(chunk);
        const Pars b = pars();
        const std::vector<Event> ev = drain();
        CHECK(ev.size() == base[k].size(), "panel %d: %d events, %d without the knobs", (int)k, (int)ev.size(), (int)base[k].size());
        for (size_t i = 0; i < ev.size() && i < base[k].size(); i++)
            CHECK(ev[i].who == kWhoEvolve && ev[i].what == base[k][i].what && ev[i].param == base[k][i].param &&
                      ev[i].from == base[k][i].from && ev[i].to == base[k][i].to && ev[i].layer == base[k][i].layer,
                  "panel %d: event %d differs", (int)k, (int)i);
        int knobs = 0;
        for (int p = 0; p < P_NPARAMS; p++)
        {
            bool evolves = false;
            for (const Event& e : ev)
                evolves |= e.param == p;
            knobs += a.v[p] != b.v[p] && !evolves && !slotParam(p);
        }
        turned += knobs;
    }
    CHECK(turned >= 6, "panel: the knobs moved %d values in Evolve's frames", turned);
}

void not_logged()
{
    start(3);
    knob(P_EVOLVE, 0);
    drain();
    knob(P_CUTOFF, par(P_CUTOFF) > 50 ? 20 : 90);
    knob(P_TEMPO, 133);
    knob(kKnobFilter, 30);
    voice(P_SVOICE, par(P_SVOICE) + 1);
    walk(walkValue() + 1);
    run(40);
    CHECK(drain().empty(), "knobs logged");
    for (int d = 0; d < 16; d++)
        padLead(d, true), run(5), padLead(d, false);
    for (int e = 0; e < kNumFx; e++)
        padFx(e, true), run(5), padFx(e, false);
    for (int m = 0; m < kNumMoves; m++)
        padMove(m, true), run(5), padMove(m, false);
    setStep(3, !step(3));
    setChordRoot((chordRoot() + 13) % 12);
    run(40);
    CHECK(drain().empty(), "pads logged");
    // Motion 100, 30 s: its macros, Sequence 2's octave and the walk's slots move, and nothing is logged
    knob(P_MOTION, 100);
    knob(P_TEMPO, 140);
    run(30000);
    CHECK(drain().empty(), "Motion logged");
    // boot() clears the ring; a loaded store logs nothing
    setHold(true);
    nudgeLayer(0, false);
    static uint8_t blob[kStoreMax];
    const int n = snapshot(blob, kStoreMax);
    CHECK(n > 0, "snapshot");
    boot();
    CHECK(drain().empty(), "boot() left events");
    setMute(0, true);
    storeWrite(blob, n);
    boot();
    CHECK(drain().empty(), "a loaded store logged");
    run(200);
    CHECK(drain().empty(), "a loaded store logged, running");
    storeWrite(nullptr, -1);
}

void piece_events()
{
    start(0);
    knob(P_EVOLVE, 100);
    std::vector<Event> ev = drain();   // (start()'s own: Play)
    CHECK(count(ev, kWhoPiece) == 0, "start(0): a Piece");   // (boot's piece is 0: setPiece(0) changes nothing)
    const int was = piece();
    setPiece(3);
    run(17);   // the call and the frame after it
    ev = drain();
    CHECK(ev.size() == 1 && ev[0].who == kWhoPiece && ev[0].what == kEvPiece && ev[0].from == was && ev[0].to == 3 && ev[0].layer == -1 &&
              ev[0].param == -1,
          "setPiece(3): %d events", (int)ev.size());
    setPiece(3);
    run(17);
    CHECK(drain().empty(), "setPiece to the piece playing logged");
    // every piece change, on any ms of the 16: one Piece, nothing of Evolve's with it
    for (int k = 0; k < 40; k++)
    {
        run(1 + k * 7 % 23);
        drain();
        const int to = (piece() + 1 + k % 3) % 10;
        setPiece(to);
        run(17);
        ev = drain();
        CHECK(ev.size() == 1 && ev[0].what == kEvPiece && ev[0].who == kWhoPiece && ev[0].to == to, "setPiece %d: %d events", to,
              (int)ev.size());
    }
    // PRESET turned on the panel: the app's own piece change, one Piece
    setAccelOff(true);
    for (int k = 0; k < 20; k++)
    {
        run(3 + k * 5 % 29);
        drain();
        const int from = piece();
        turn(kPRESET, 1);
        run(17);
        ev = drain();
        CHECK(piece() != from, "PRESET did not change the piece");
        CHECK(ev.size() == 1 && ev[0].what == kEvPiece && ev[0].who == kWhoPiece && ev[0].from == from && ev[0].to == piece(),
              "PRESET: %d events", (int)ev.size());
    }
    // PRESET turned in the frame of an Evolve nudge: the Piece, and Evolve's dropped
    start(1);
    knob(P_EVOLVE, 100);
    knob(P_TEMPO, 140);
    drain();
    int t = 0;
    for (; t < 2000000; t++)
    {
        stepMs(chunk);
        if (!drain().empty()) break;
    }
    start(1);
    knob(P_EVOLVE, 100);
    knob(P_TEMPO, 140);
    setAccelOff(true);
    run(t);
    drain();
    turn(kPRESET, 1);
    stepMs(chunk);
    ev = drain();
    CHECK(ev.size() == 1 && ev[0].what == kEvPiece && ev[0].from == 1 && ev[0].to == piece(), "PRESET in Evolve's frame: %d events",
          (int)ev.size());
}

void overflow()
{
    boot();
    drain();
    bool on[kNumLayers] = {};
    for (int i = 0; i < 100; i++)
        setMute(i % kNumLayers, on[i % kNumLayers] = !on[i % kNumLayers]);
    Event e[80];
    const int n = takeEvents(e, 80);
    CHECK(n == 64, "overflow: %d events", n);
    for (int k = 0; k < n; k++)
    {
        const int i = 36 + k;
        CHECK(e[k].who == kWhoYou && e[k].what == kEvMute && e[k].layer == i % kNumLayers && e[k].to == (i / kNumLayers % 2 == 0),
              "overflow: event %d", k);
    }
    CHECK(takeEvents(e, 80) == 0, "overflow: not drained");
    // taken a few at a time: the oldest first, the rest kept
    for (int i = 0; i < 10; i++)
        setSolo(i % kNumLayers, i < kNumLayers);
    CHECK(takeEvents(e, 4) == 4 && e[0].layer == 0 && e[3].layer == 3 && e[0].to == 1, "take 4");
    CHECK(takeEvents(e, 0) == 0, "take 0");
    CHECK(takeEvents(e, 64) == 6 && e[0].layer == 4 && e[1].layer == 0 && e[1].to == 0, "take the rest");
}

// --- 7. the Sky model ---------------------------------------------------------------------------------------------

using tp::sky::Puck;
using tp::sky::Sky;
using tp::sky::SkyInput;

SkyInput skyIn()
{
    SkyInput in{};
    in.playing = true;
    for (int l = 0; l < 5; l++)
        in.heard[l] = true;
    return in;
}
bool parked(Puck p) { return p.x == 0.5f && p.y == 0.5f; }
bool at(Puck p, double a)
{
    return std::fabs(p.x - (0.5 + 0.5 * std::cos(a))) < 1e-3 && std::fabs(p.y - (0.5 + 0.5 * std::sin(a))) < 1e-3;
}
int unparked(const Sky& s, int from, int to)
{
    int n = 0;
    for (int i = from; i < to; i++)
        n += !parked(s.puck(i));
    return n;
}
Event ev(Who who, What what, int layer = -1, int param = -1, int from = 0, int to = 0)
{
    return Event{who, what, (int8_t)layer, (int16_t)param, (int16_t)from, (int16_t)to};
}
void feed(Sky& s, SkyInput in, std::vector<Event> e, int ms = 0)
{
    in.events = e.data();
    in.numEvents = (int)e.size();
    s.advance(in, ms);
}
std::string fmt(const Event& e, int piece = 0)
{
    char b[tp::sky::kTextMax];
    const int n = tp::sky::formatEvent(e, b, piece);
    CHECK(n == (int)std::strlen(b), "formatEvent's length %d vs %d", n, (int)std::strlen(b));
    return b;
}

void particles_launch_and_park()
{
    Sky s;
    SkyInput in = skyIn();
    s.advance(in, 0);
    CHECK(unparked(s, 0, 8) == 0, "parked at start");
    // 4 hits on the Sequence, one a call: 4 outer pucks at 0.39 x (hits + reps)
    for (uint32_t h = 1; h <= 4; h++)
    {
        in.hits[0] = h;
        s.advance(in, 0);
    }
    CHECK(unparked(s, 0, 4) == 4 && unparked(s, 4, 8) == 0, "4 outer pucks: %d, inner %d", unparked(s, 0, 4), unparked(s, 4, 8));
    for (int k = 0; k < 4; k++)
        CHECK(at(s.puck(k), 0.39 * (k + 1)), "puck %d at %.3f: %.4f %.4f", k, 0.39 * (k + 1), s.puck(k).x, s.puck(k).y);
    // a 16 ms step: each 0.06 on (the outer orbit)
    s.advance(in, 16);
    for (int k = 0; k < 4; k++)
        CHECK(at(s.puck(k), 0.39 * (k + 1) + 0.06), "puck %d one step on", k);
    // 1100 ms with no hits, a little at a time: all parked
    for (int t = 0; t < 110; t++)
        s.advance(in, 10);
    CHECK(unparked(s, 0, 8) == 0, "parked after 1100 ms: %d", unparked(s, 0, 8));

    // 10 hits in one call: 3 launched (the counts 5, 6, 7), the rest still counted for the next angle
    in.hits[0] = 14;
    s.advance(in, 0);
    CHECK(unparked(s, 0, 4) == 3, "3 a call: %d", unparked(s, 0, 4));
    for (int k = 0; k < 3; k++)
        CHECK(at(s.puck(k), 0.39 * (5 + k)), "capped puck %d", k);
    s.advance(in, 16);
    in.hits[0] = 15;
    s.advance(in, 0);
    CHECK(at(s.puck(3), 0.39 * 15), "the skipped hits count: puck 3 at 0.39 x 15");
    // full orbit: the oldest (least life; of equals, the first) is taken, not the newest
    in.hits[0] = 16;
    s.advance(in, 0);
    CHECK(at(s.puck(0), 0.39 * 16), "the oldest puck reused");
    CHECK(at(s.puck(1), 0.39 * 6 + 0.06) && at(s.puck(2), 0.39 * 7 + 0.06) && at(s.puck(3), 0.39 * 15), "the others kept");

    // Sequence 2: the inner orbit, 3 rad on, 0.045 a step
    Sky s2;
    SkyInput i2 = skyIn();
    i2.hits[1] = 1;
    s2.advance(i2, 0);
    CHECK(unparked(s2, 0, 4) == 0 && unparked(s2, 4, 8) == 1 && at(s2.puck(4), 0.39 + 3.0), "inner launch");
    s2.advance(i2, 32);
    CHECK(at(s2.puck(4), 0.39 + 3.0 + 2 * 0.045), "inner: 0.045 a step");
    // a repeat: life 0.55 (34 steps on, parked at the 35th), at 0.39 x (hits + reps)
    i2.reps[1] = 1;
    s2.advance(i2, 0);
    CHECK(unparked(s2, 4, 8) == 2 && at(s2.puck(5), 0.39 * 2 + 3.0), "repeat launched at 0.39 x 2 + 3");
    s2.advance(i2, 34 * 16);
    CHECK(!parked(s2.puck(5)), "a repeat lives 34 steps");
    s2.advance(i2, 16);
    CHECK(parked(s2.puck(5)) && !parked(s2.puck(4)), "a repeat parks at 35 steps, the hit lives on");
    // repeats: 2 a call
    i2.reps[1] = 11;
    s2.advance(i2, 0);
    CHECK(unparked(s2, 4, 8) == 3, "2 repeats a call: %d", unparked(s2, 4, 8));

    // a muted layer launches none (its counts still taken); the other sequence does
    Sky s3;
    SkyInput i3 = skyIn();
    i3.heard[1] = false;
    i3.hits[0] = 3;
    i3.hits[1] = 1;
    s3.advance(i3, 0);
    CHECK(unparked(s3, 0, 4) == 0 && unparked(s3, 4, 8) == 1, "muted Sequence: none (%d)", unparked(s3, 0, 4));
    i3.heard[1] = true;
    i3.heard[3] = false;
    i3.hits[0] = 4;
    i3.hits[1] = 2;
    s3.advance(i3, 0);
    CHECK(unparked(s3, 0, 4) == 1 && at(s3.puck(0), 0.39 * 4) && unparked(s3, 4, 8) == 1, "heard again: from its count");

    // not playing: nothing moves, nothing launches
    i3.playing = false;
    const Puck before = s3.puck(0);
    i3.hits[0] = 6;
    i3.heard[3] = true;
    i3.hits[1] = 3;
    s3.advance(i3, 500);
    CHECK(s3.puck(0).x == before.x && s3.puck(0).y == before.y, "stopped: still");
    CHECK(unparked(s3, 0, 4) == 1 && unparked(s3, 4, 8) == 1, "stopped: no launches");
    // a counter that goes back (a new boot) launches nothing and does not spin
    i3.playing = true;
    i3.hits[0] = 0;
    s3.advance(i3, 0);
    CHECK(unparked(s3, 0, 4) == 1, "counter back: no launches");
    i3.hits[0] = 1;
    s3.advance(i3, 0);
    CHECK(unparked(s3, 0, 4) == 2, "counter back: then from there");
}

void planet_frames()
{
    Sky s;
    SkyInput in = skyIn();
    in.act[1] = 0.5f;
    s.advance(in, 0);
    CHECK(s.planetFrame(1) == 12, "heard, 0.5: %d", s.planetFrame(1));
    in.heard[1] = false;
    s.advance(in, 16);
    CHECK(s.planetFrame(1) == 13, "muted: %d", s.planetFrame(1));
    in.heard[1] = true;
    feed(s, in, {ev(kWhoYou, kEvNudge, 1)});
    CHECK(s.planetFrame(1) == 14 && s.flash(1) == 600, "nudge: %d (%d ms)", s.planetFrame(1), s.flash(1));
    s.advance(in, 599);
    CHECK(s.planetFrame(1) == 14, "599 ms on: still the flash");
    s.advance(in, 1);
    CHECK(s.planetFrame(1) == 12 && s.flash(1) == 0, "after 600 ms: %d", s.planetFrame(1));
    // a flash shows over a mute
    in.heard[1] = false;
    feed(s, in, {ev(kWhoYou, kEvShake, 1)});
    CHECK(s.planetFrame(1) == 14, "shake over a mute: %d", s.planetFrame(1));
    s.advance(in, 600);
    CHECK(s.planetFrame(1) == 13, "then muted");
    // Evolve's nudges are kEvParam only; nudge layer 5 is planet 4 (Atmos); the Space (4) has no planet
    in.heard[1] = true;
    feed(s, in, {ev(kWhoEvolve, kEvParam, 5, P_NCOL, 0, 3)});
    CHECK(s.flash(5) == 600 && s.planetFrame(4) == 2, "Evolve on the Atmosphere: planet 4 frame %d", s.planetFrame(4));
    s.advance(in, 600);
    feed(s, in, {ev(kWhoEvolve, kEvParam, 4, P_ECHO, 50, 52)});
    CHECK(s.flash(4) == 600, "the Space's flash");
    for (int p = 0; p < 5; p++)
        CHECK(s.planetFrame(p) % 3 != 2, "the Space flashes no planet (%d)", p);
    s.advance(in, 600);
    // shake all: Drone, Seq, Lead, Seq 2, Atmos, not the Space
    feed(s, in, {ev(kWhoYou, kEvShakeAll)});
    for (int l = 0; l < 6; l++)
        CHECK((s.flash(l) == 600) == (l != 4), "shake all: layer %d %d ms", l, s.flash(l));
    for (int p = 0; p < 5; p++)
        CHECK(s.planetFrame(p) % 3 == 2, "shake all: planet %d", p);
    s.advance(in, 600);
    // mutes, solos and the rest flash nothing
    feed(s, in, {ev(kWhoYou, kEvMute, 1, -1, 0, 1), ev(kWhoYou, kEvSolo, 2, -1, 0, 1), ev(kWhoYou, kEvNextChord),
                 ev(kWhoYou, kEvReset), ev(kWhoPiece, kEvPiece, -1, -1, 0, 3)});
    for (int l = 0; l < 6; l++)
        CHECK(s.flash(l) == 0, "no flash on layer %d", l);
    // a burst: every event's flash, not only the three the log keeps
    std::vector<Event> burst = {ev(kWhoYou, kEvNudge, 0)};
    for (int i = 0; i < 5; i++)
        burst.push_back(ev(kWhoYou, kEvParam, 3, P_S2CUT, i, i + 1));
    burst[3] = ev(kWhoEvolve, kEvParam, 2, P_VIB, 3, 4);
    feed(s, in, burst);
    CHECK(s.flash(0) == 600 && s.flash(2) == 600 && s.flash(3) == 600 && s.flash(1) == 0, "burst flashes");
    // levels: floor(act x 8), at most 7
    const float acts[] = {0.0f, 0.124f, 0.125f, 0.99f, 1.0f, 2.0f, -1.0f, NAN};
    const int levels[] = {0, 0, 1, 7, 7, 7, 0, 0};
    for (int i = 0; i < 8; i++)
    {
        in.act[0] = acts[i];
        s.advance(in, 600);
        CHECK(s.planetFrame(0) == levels[i] * 3, "act %g: frame %d", acts[i], s.planetFrame(0));
    }
    CHECK(s.planetFrame(-1) == 0 && s.planetFrame(5) == 0 && s.flash(-1) == 0 && s.flash(6) == 0, "out of range");
}

void log_order_and_fit()
{
    Sky s;
    SkyInput in = skyIn();
    s.advance(in, 0);
    for (int l = 0; l < 3; l++)
        CHECK(std::strcmp(s.logText(l), "") == 0, "empty line %d", l);
    const std::vector<Event> five = {ev(kWhoYou, kEvNextChord), ev(kWhoYou, kEvNewChords), ev(kWhoYou, kEvForget),
                                     ev(kWhoYou, kEvHold, -1, -1, 0, 1), ev(kWhoYou, kEvLock, -1, -1, 0, 1)};
    feed(s, in, five);
    CHECK(std::string(s.logText(0)) == "You › Lock on", "line 0: %s", s.logText(0));
    CHECK(std::string(s.logText(1)) == "You › Hold on", "line 1: %s", s.logText(1));
    CHECK(std::string(s.logText(2)) == "You › Forget Lead", "line 2: %s", s.logText(2));
    CHECK(s.logSerial(0) != s.logSerial(1) && s.logSerial(1) != s.logSerial(2) && s.logSerial(0) != s.logSerial(2), "serials");
    // the same five in two calls: the same lines; each shift changes the serials
    Sky t;
    feed(t, in, {five[0], five[1], five[2]});
    CHECK(std::string(t.logText(0)) == "You › Forget Lead" && std::string(t.logText(2)) == "You › Next chord", "3 in");
    int ser[3];
    for (int l = 0; l < 3; l++)
        ser[l] = t.logSerial(l);
    feed(t, in, {five[3]});
    CHECK(std::string(t.logText(1)) == "You › Forget Lead" && std::string(t.logText(2)) == "You › New chords", "shifted");
    for (int l = 0; l < 3; l++)
        CHECK(t.logSerial(l) != ser[l], "line %d's serial changed with its text", l);
    for (int l = 0; l < 3; l++)
        ser[l] = t.logSerial(l);
    t.advance(in, 100);
    for (int l = 0; l < 3; l++)
        CHECK(t.logSerial(l) == ser[l], "no event: serial %d kept", l);
    feed(t, in, {five[4]});
    for (int l = 0; l < 3; l++)
        CHECK(std::string(t.logText(l)) == s.logText(l), "two calls, line %d: %s", l, t.logText(l));
    CHECK(std::strcmp(t.logText(3), "") == 0 && std::strcmp(t.logText(-1), "") == 0, "out of range");

    // the wording (spec section 10, rulings R8, R9)
    const int reed = 6;   // EN_BV_NAME: "FM Reed"
    CHECK(fmt(ev(kWhoEvolve, kEvParam, 1, P_CUTOFF, 40, 46)) == "Evolve › Seq Cutoff +6", "%s", fmt(ev(kWhoEvolve, kEvParam, 1, P_CUTOFF, 40, 46)).c_str());
    CHECK(fmt(ev(kWhoYou, kEvParam, 5, P_NCOL, 3, -9)) == "You › Atmos Tone −12", "%s", fmt(ev(kWhoYou, kEvParam, 5, P_NCOL, 3, -9)).c_str());
    CHECK(fmt(ev(kWhoYou, kEvVoice, 2, P_LVOICE, 0, reed)) == "You › Shake Lead FM Reed", "%s", fmt(ev(kWhoYou, kEvVoice, 2, P_LVOICE, 0, reed)).c_str());
    CHECK(fmt(ev(kWhoYou, kEvVoice, 4, P_ETIME, 2, 3)) == "You › Shake Space 1/4.", "%s", fmt(ev(kWhoYou, kEvVoice, 4, P_ETIME, 2, 3)).c_str());
    CHECK(fmt(ev(kWhoPiece, kEvPiece, -1, -1, 0, 2)) == "Piece › Phaedra", "%s", fmt(ev(kWhoPiece, kEvPiece, -1, -1, 0, 2)).c_str());
    CHECK(fmt(ev(kWhoYou, kEvReset), 8) == "Piece › Sakura Rain", "reset: %s", fmt(ev(kWhoYou, kEvReset), 8).c_str());
    CHECK(fmt(ev(kWhoYou, kEvNextChord)) == "You › Next chord", "next chord");
    CHECK(fmt(ev(kWhoYou, kEvNewChords)) == "You › New chords", "new chords");
    CHECK(fmt(ev(kWhoYou, kEvShakeAll)) == "You › Shake all", "shake all");
    CHECK(fmt(ev(kWhoYou, kEvHold, -1, -1, 1, 0)) == "You › Hold off", "hold off");
    CHECK(fmt(ev(kWhoYou, kEvLock, -1, -1, 1, 0)) == "You › Lock off", "lock off");
    CHECK(fmt(ev(kWhoYou, kEvPlay, -1, -1, 0, 1)) == "You › Play", "play");
    CHECK(fmt(ev(kWhoYou, kEvPlay, -1, -1, 1, 0)) == "You › Stop", "stop");
    CHECK(fmt(ev(kWhoYou, kEvMute, 4, -1, 0, 1)) == "You › Mute Atmos", "%s", fmt(ev(kWhoYou, kEvMute, 4, -1, 0, 1)).c_str());
    CHECK(fmt(ev(kWhoYou, kEvMute, 1, -1, 1, 0)) == "You › Unmute Seq", "unmute");
    CHECK(fmt(ev(kWhoYou, kEvSolo, 3, -1, 0, 1)) == "You › Solo Seq 2", "solo");
    CHECK(fmt(ev(kWhoYou, kEvSolo, 0, -1, 1, 0)) == "You › Unsolo Drone", "unsolo");
    CHECK(fmt(ev(kWhoYou, kEvNudge, 4)) == "You › Nudge Space", "%s", fmt(ev(kWhoYou, kEvNudge, 4)).c_str());
    CHECK(fmt(ev(kWhoYou, kEvShake, 5)) == "You › Shake Atmos", "shake");
    // the log uses the piece the input names
    Sky r;
    SkyInput ri = skyIn();
    ri.piece = 9;
    feed(r, ri, {ev(kWhoYou, kEvReset)});
    CHECK(std::string(r.logText(0)) == "Piece › Umeda", "reset line: %s", r.logText(0));
}

// Valid UTF-8, printable, and only ASCII or the screen's glyphs (− ± · › » •).
bool cleanText(const std::string& t)
{
    for (size_t i = 0; i < t.size();)
    {
        const unsigned char c = (unsigned char)t[i];
        if (c < 0x80)
        {
            if (c < 0x20 || c == 0x7f) return false;
            i++;
            continue;
        }
        int n = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 0;
        if (!n || i + n > t.size()) return false;
        uint32_t cp = c & (n == 2 ? 0x1F : 0x0F);
        for (int k = 1; k < n; k++)
        {
            const unsigned char d = (unsigned char)t[i + k];
            if ((d & 0xC0) != 0x80) return false;
            cp = cp << 6 | (d & 0x3F);
        }
        if (cp != 0x2212 && cp != 0xB1 && cp != 0xB7 && cp != 0x203A && cp != 0xBB && cp != 0x2022) return false;
        i += n;
    }
    return true;
}

void every_format_fits()
{
    const int lims[] = {-32768, -1, 0, 1, 100, 32767};
    const int layers[] = {-128, -1, 0, 1, 2, 3, 4, 5, 6, 127};
    long n = 0, bad = 0;
    for (int who = 0; who <= 3; who++)
        for (int what = 0; what <= 15; what++)
            for (int layer : layers)
                for (int param = -2; param <= P_NPARAMS + 1; param++)
                {
                    // kEvParam and kEvVoice use the parameter; the rest are the same for every one: a few suffice
                    if (what != kEvParam && what != kEvVoice && param > 2) continue;
                    std::vector<std::pair<int, int>> ft;
                    for (int f : lims)
                        for (int t : lims)
                            ft.push_back({f, t});
                    if (param >= 0 && param < P_NPARAMS)
                        for (int v = en_param_info(param)->lo; v <= en_param_info(param)->hi && v - en_param_info(param)->lo < 40; v++)
                            ft.push_back({en_param_info(param)->lo, v});
                    for (int pc = 0; pc < EN_NPIECES; pc++)
                        ft.push_back({0, pc});
                    for (auto [f, t] : ft)
                        for (int piece : {-1, 0, 4, 8, EN_NPIECES})
                        {
                            if (what != kEvReset && piece != 0) continue;
                            char b[tp::sky::kTextMax + 8];
                            std::memset(b, 'X', sizeof b);
                            const Event e = ev((Who)who, (What)what, layer, param, f, t);
                            const int len = tp::sky::formatEvent(e, b, piece);
                            n++;
                            const bool ok = len >= 0 && len <= 31 && (int)std::strlen(b) == len && cleanText(b) &&
                                            b[tp::sky::kTextMax] == 'X';
                            if (!ok && bad++ < 10)
                                CHECK(false, "who %d what %d layer %d param %d %d->%d piece %d: %d \"%s\"", who, what, layer,
                                      param, f, t, piece, len, b);
                        }
                }
    CHECK(bad == 0 && n > 100000, "%ld of %ld formats bad", bad, n);

    // the realistic lines come out whole: each nudge layer's values (ui.c NUDGE_PAR) moved by up to 100, by either
    static const char* const LAYER[6] = {"Drone", "Seq", "Lead", "Seq 2", "Space", "Atmos"};
    static const int PAR[6][4] = {{P_CATT, P_CCOL, P_CRES, P_DREL},   {P_CUTOFF, P_DECAY, P_SACCENT, P_SACCENT},
                                  {P_LTONE, P_LATT, P_VIB, P_GLIDE},  {P_S2CUT, P_S2DEC, P_S2CUT, P_S2DEC},
                                  {P_ECHO, P_FEEDBACK, P_REVERB, P_SIZE}, {P_NCOL, P_NGRAIN, P_NTUNE, P_NROOM}};
    static const int MORE[3][2] = {{0, P_PHASER}, {0, P_PEDAL}, {4, P_CLOUD}};
    std::vector<std::pair<int, int>> lp;
    for (int l = 0; l < 6; l++)
        for (int j = 0; j < 4; j++)
            lp.push_back({l, PAR[l][j]});
    for (auto& m : MORE)
        lp.push_back({m[0], m[1]});
    for (auto [l, p] : lp)
    {
        CHECK(std::strlen(tp::sky::shortName(p)) >= 3, "short name of %d: \"%s\"", p, tp::sky::shortName(p));
        for (Who who : {kWhoYou, kWhoEvolve})
        {
            const std::string head = std::string(who == kWhoYou ? "You" : "Evolve") + " › " + LAYER[l] + " " + tp::sky::shortName(p);
            CHECK(fmt(ev(who, kEvParam, l, p, 0, 100)) == head + " +100", "whole: %s", fmt(ev(who, kEvParam, l, p, 0, 100)).c_str());
            CHECK(fmt(ev(who, kEvParam, l, p, 100, 0)) == head + " −100", "whole: %s", fmt(ev(who, kEvParam, l, p, 100, 0)).c_str());
        }
    }
    // every voice a shake can pick, and every piece, whole
    static const int VOICE[6] = {P_CVOICE, P_SVOICE, P_LVOICE, P_S2VOICE, P_ETIME, P_NCHAR};
    for (int l = 0; l < 6; l++)
        for (int v = en_param_info(VOICE[l])->lo; v <= en_param_info(VOICE[l])->hi; v++)
        {
            char t[16];
            en_param_text(VOICE[l], v, t);
            const std::string want = std::string("You › Shake ") + LAYER[l] + " " + t;
            CHECK(fmt(ev(kWhoYou, kEvVoice, l, VOICE[l], 0, v)) == want, "voice whole: %s", want.c_str());
        }
    for (int pc = 0; pc < EN_NPIECES; pc++)
    {
        CHECK(fmt(ev(kWhoPiece, kEvPiece, -1, -1, 0, pc)) == std::string("Piece › ") + en_piece_name(pc), "piece %d", pc);
        CHECK(fmt(ev(kWhoYou, kEvReset), pc) == std::string("Piece › ") + en_piece_name(pc), "reset %d", pc);
    }
    for (int p = 0; p < P_NPARAMS; p++)
        CHECK(std::strlen(tp::sky::shortName(p)) >= 2 && cleanText(tp::sky::shortName(p)), "short name %d", p);
    CHECK(std::strcmp(tp::sky::shortName(-1), "") == 0 && std::strcmp(tp::sky::shortName(P_NPARAMS), "") == 0, "short name range");
}

void reset_clears()
{
    Sky s;
    SkyInput in = skyIn();
    in.act[2] = 1.0f;
    in.hits[0] = 2;
    in.hits[1] = 1;
    std::vector<Event> e = {ev(kWhoYou, kEvShakeAll), ev(kWhoYou, kEvNextChord), ev(kWhoYou, kEvForget)};
    feed(s, in, e);
    CHECK(unparked(s, 0, 8) == 3 && s.flash(0) == 600 && std::strlen(s.logText(2)) > 0, "set up");
    std::set<int> seen;
    for (int l = 0; l < 3; l++)
        seen.insert(s.logSerial(l));
    s.reset();
    CHECK(unparked(s, 0, 8) == 0, "reset: parked");
    for (int l = 0; l < 6; l++)
        CHECK(s.flash(l) == 0, "reset: flash %d", l);
    for (int p = 0; p < 5; p++)
        CHECK(s.planetFrame(p) == 0, "reset: planet %d frame %d", p, s.planetFrame(p));
    for (int l = 0; l < 3; l++)
    {
        CHECK(std::strcmp(s.logText(l), "") == 0, "reset: line %d empty", l);
        CHECK(!seen.count(s.logSerial(l)), "reset: line %d's serial new", l);
        seen.insert(s.logSerial(l));
    }
    // and it goes on from there: the counts start again from 0
    in.hits[0] = 1;
    in.hits[1] = 0;
    feed(s, in, {ev(kWhoYou, kEvNextChord)});
    CHECK(unparked(s, 0, 4) == 1 && at(s.puck(0), 0.39), "after reset: a launch");
    CHECK(std::string(s.logText(0)) == "You › Next chord" && !seen.count(s.logSerial(0)), "after reset: a line");
}

} // namespace

int main()
{
    readouts_follow_engine();
    evolve_chance();
    slot_codes_name_like_ui();
    next_chord();
    next_chord_engine();
    ring_matches_ui();
    hand_nudge_is_you();
    evolve_is_evolve();
    same_frame();
    not_logged();
    piece_events();
    overflow();
    particles_launch_and_park();
    planet_frames();
    log_order_and_fit();
    every_format_fits();
    reset_clears();
    if (fails)
    {
        std::printf("test_screen: %d failures\n", fails);
        return 1;
    }
    std::printf("test_screen: all passed\n");
    return 0;
}
