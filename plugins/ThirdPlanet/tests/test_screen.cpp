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
//      Motion log nothing; 100 events leave the newest 64, in order.
//   c++ -std=c++17 -O2 -I plugins/ThirdPlanet/src -I deps/mpc-plugin-sdk/framework \
//       plugins/ThirdPlanet/tests/test_screen.cpp plugins/ThirdPlanet/src/app_unit.cpp \
//       plugins/ThirdPlanet/src/engine_unit.cpp plugins/ThirdPlanet/src/power.cpp \
//       -o plugins/ThirdPlanet/build/tests/test_screen
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "app_api.h"
#include "engine.h"

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
    if (fails)
    {
        std::printf("test_screen: %d failures\n", fails);
        return 1;
    }
    std::printf("test_screen: all passed\n");
    return 0;
}
