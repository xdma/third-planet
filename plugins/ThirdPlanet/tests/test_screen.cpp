// SPDX-License-Identifier: GPL-3.0-only
// Third Planet's readouts for the star-chart screen (app_api.h, "Readouts for the star-chart screen"):
//   1. each readout equals the engine global it reads, while a piece plays;
//   2. Evolve's chance is ui.c nudge_tick's, and 0 while locked, held or resting;
//   3. a slot's chord number names exactly what ui.c's slot_label() does, on every piece and walk character;
//   4. the next chord: Endless, slot 1's (what the next change brings); Loop, the next slot that isn't a gap;
//   5. the ring is ui.c ring_dots' at random moments, stays put while held, and is absent at Drift Hold.
//   c++ -std=c++17 -O2 -I plugins/ThirdPlanet/src -I deps/mpc-plugin-sdk/framework \
//       plugins/ThirdPlanet/tests/test_screen.cpp plugins/ThirdPlanet/src/app_unit.cpp \
//       plugins/ThirdPlanet/src/engine_unit.cpp plugins/ThirdPlanet/src/power.cpp \
//       -o plugins/ThirdPlanet/build/tests/test_screen
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

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

} // namespace

int main()
{
    readouts_follow_engine();
    evolve_chance();
    slot_codes_name_like_ui();
    next_chord();
    ring_matches_ui();
    if (fails)
    {
        std::printf("test_screen: %d failures\n", fails);
        return 1;
    }
    std::printf("test_screen: all passed\n");
    return 0;
}
