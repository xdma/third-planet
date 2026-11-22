// SPDX-License-Identifier: GPL-3.0-only
// Third Planet's controls for the MPC:
//   1. the pads: each bank sets the engine state it must, while held; a release ends what the press began,
//      whatever Keys or Pad base say by then; all notes off, a change of Keys or of Pad base let go of all;
//   2. Keys: Lead, and notes outside the banks: the Lead by MIDI;
//   3. the status line's Pad text names what the last pad did;
//   4. every switch both ways: set from the MPC, and mirrored when the app changes it;
//   5. every one-shot acts once and goes back to Off, and acts again on the next press;
//   6. the Pattern's steps and accents, and the Chords slot's root;
//   7. a project load: the piece's values come back as saved, and no one-shot fires from it.
//   c++ -std=c++17 -O2 -I plugins/ThirdPlanet/src -I deps/mpc-plugin-sdk/framework \
//       plugins/ThirdPlanet/tests/test_controls.cpp plugins/ThirdPlanet/src/app_unit.cpp \
//       plugins/ThirdPlanet/src/engine_unit.cpp plugins/ThirdPlanet/src/power.cpp plugins/ThirdPlanet/src/sky.cpp plugins/ThirdPlanet/src/link_codec.cpp \
//       plugins/ThirdPlanet/src/link_capture.cpp \
//       plugins/ThirdPlanet/src/link_alsa.cpp plugins/ThirdPlanet/src/link_worker.cpp \
//       -o plugins/ThirdPlanet/build/tests/test_controls
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "ThirdPlanet.cpp"

namespace {

int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); fails++; } } while (0)

int idx(const char* id)
{
    for (int i = 0; i < tp::kNumControls; ++i)
        if (!std::strcmp(tp::kParams[i].id, id)) return i;
    std::printf("no parameter %s\n", id);
    std::exit(2);
}

struct Ev { int status, d1, d2; };

// `frames` rendered in blocks of 128, the events at the first block's start.
void run(ThirdPlanet& p, long frames, const std::vector<Ev>& evs = {})
{
    std::vector<float> l(128), r(128);
    float* outs[2] = {l.data(), r.data()};
    bool first = true;
    for (long done = 0; done < frames; done += 128)
    {
        if (first && !evs.empty())
        {
            uint8_t storage[sizeof(VstEvents) + 16 * sizeof(VstEvent*)];
            VstMidiEvent me[16];
            VstEvents* ve = reinterpret_cast<VstEvents*>(storage);
            ve->numEvents = 0;
            for (const Ev& e : evs)
            {
                VstMidiEvent& m = me[ve->numEvents];
                std::memset(&m, 0, sizeof m);
                m.type = kVstMidiType, m.byteSize = sizeof m;
                m.midiData[0] = (char)e.status, m.midiData[1] = (char)e.d1, m.midiData[2] = (char)e.d2;
                ve->events[ve->numEvents++] = reinterpret_cast<VstEvent*>(&m);
            }
            p.internalAddMidi(ve);
        }
        first = false;
        p.internalProcess(nullptr, outs, 128);
    }
}

std::unique_ptr<ThirdPlanet> make()
{
    auto p = std::make_unique<ThirdPlanet>();
    p->internalSetSampleRate(44100.0);
    p->internalSetBlockSize(128);
    p->internalResume();
    return p;
}

std::string padText(ThirdPlanet& p)
{
    std::string s;
    p.formatValue(kDispPad, p.get(kDispPad), s);
    return s;
}

void down(ThirdPlanet& p, int note) { run(p, 2048, {{0x90, note, 100}}); }
void up(ThirdPlanet& p, int note) { run(p, 2048, {{0x80, note, 0}}); }

void pads()
{
    using namespace tp::app;
    auto p = make();
    run(*p, 44100);
    CHECK(p->getInt(idx("keymode")) == 0 && p->getInt(idx("pad_base")) == 36, "Keys %d, Pad base %d at start",
          p->getInt(idx("keymode")), p->getInt(idx("pad_base")));

    std::printf("1. pads\n");
    for (int d : {0, 5, 15})
    {
        down(*p, 36 + d);
        CHECK(leadDegree() == d, "Lead pad %d: the Lead sounds degree %d", d, leadDegree());
        CHECK(padText(*p) == "Lead " + std::to_string(d + 1), "Lead pad %d shows '%s'", d, padText(*p).c_str());
        up(*p, 36 + d);
    }
    for (int e = 0; e < kNumFx; ++e)
    {
        down(*p, 52 + e);
        CHECK(effectsHeld() == 1u << e, "effect pad %d held: effects %#x", e, effectsHeld());
        CHECK(padText(*p) == fxName(e), "effect pad %d shows '%s', not '%s'", e, padText(*p).c_str(), fxName(e));
        up(*p, 52 + e);
        CHECK(effectsHeld() == 0, "effect pad %d released: effects %#x", e, effectsHeld());
    }
    for (int m = 0; m < kNumMoves; ++m)
    {
        down(*p, 68 + m);
        const uint32_t mv = effectsHeld() >> 16, pf = perfMoves();
        CHECK(m < 3 ? mv == 1u << m && pf == 0 : pf == 1u << (m - 3) && mv == 0, "move pad %d: moves %#x, PERFORM's %#x", m, mv, pf);
        CHECK(padText(*p) == moveName(m), "move pad %d shows '%s'", m, padText(*p).c_str());
        up(*p, 68 + m);
        CHECK(effectsHeld() == 0 && perfMoves() == 0, "move pad %d released: %#x %#x", m, effectsHeld(), perfMoves());
    }
    down(*p, 52), down(*p, 55);   // two at once, one let go
    CHECK(effectsHeld() == 0x9u, "two effects: %#x", effectsHeld());
    up(*p, 52);
    CHECK(effectsHeld() == 0x8u, "one let go: %#x", effectsHeld());
    up(*p, 55);

    // a release ends what its press began, whatever Pad base says by then; a change of Pad base lets go of all
    down(*p, 53);
    p->set(idx("pad_base"), 40.0f);
    run(*p, 2048);
    CHECK(effectsHeld() == 0, "Pad base changed with an effect held: %#x", effectsHeld());
    up(*p, 53);   // (now Lead degree 13 in the new banks: its release must not reach anything)
    down(*p, 56);   // (base 40: effect 0)
    CHECK(effectsHeld() == 1u, "Pad base 40, note 56: effects %#x", effectsHeld());
    run(*p, 2048, {{0xB0, 123, 0}});   // all notes off
    CHECK(effectsHeld() == 0, "all notes off left effects %#x", effectsHeld());
    p->set(idx("pad_base"), 36.0f);
    run(*p, 2048);

    std::printf("2. notes outside the banks, Keys: Lead\n");
    down(*p, 90);
    CHECK(padText(*p) == "Note 90", "note 90 shows '%s'", padText(*p).c_str());
    CHECK(effectsHeld() == 0, "note 90 held effects %#x", effectsHeld());
    up(*p, 90);
    p->set(idx("keymode"), 1.0f);
    run(*p, 2048);
    down(*p, 52);
    CHECK(effectsHeld() == 0 && padText(*p) == "Note 52", "Keys Lead: note 52 effects %#x, shows '%s'", effectsHeld(),
          padText(*p).c_str());
    up(*p, 52);
    // a pad held while Keys changes: let go
    p->set(idx("keymode"), 0.0f);
    run(*p, 2048);
    down(*p, 52);
    p->set(idx("keymode"), 1.0f);
    run(*p, 2048);
    CHECK(effectsHeld() == 0, "Keys changed with an effect held: %#x", effectsHeld());
    up(*p, 52);
    p->set(idx("keymode"), 0.0f);
    run(*p, 2048);
}

// A switch: set from the MPC, then from the app (mirrored back).
void switchBothWays(ThirdPlanet& p, const char* id, bool (*now)(), void (*appSet)(bool))
{
    const int i = idx(id);
    for (bool v : {true, false})
    {
        p.set(i, v ? 1.0f : 0.0f);
        run(p, 4410);
        CHECK(now() == v, "%s set %d from the MPC: the app has %d", id, v, now());
        CHECK(p.getBool(i) == v, "%s set %d from the MPC: shows %d", id, v, p.getBool(i));
    }
    for (bool v : {true, false})
    {
        appSet(v);
        run(p, 4410);
        CHECK(p.getBool(i) == v, "%s set %d by the app: shows %d", id, v, p.getBool(i));
    }
}

void switches()
{
    using namespace tp::app;
    std::printf("4. switches both ways\n");
    auto p = make();
    run(*p, 44100);
    switchBothWays(*p, "hold", [] { return held(); }, [](bool v) { setHold(v); });
    switchBothWays(*p, "lock", [] { return locked(); }, [](bool v) { setLock(v); });
    switchBothWays(*p, "play", [] { return playing(); }, [](bool v) { play(v); });
    play(true);
    run(*p, 4410);
    static int layer;
    const char* const mute[] = {"mute_drone", "mute_seq", "mute_lead", "mute_seq2", "mute_atmos"};
    const char* const solo[] = {"solo_drone", "solo_seq", "solo_lead", "solo_seq2", "solo_atmos"};
    for (layer = 0; layer < kNumLayers; ++layer)
    {
        switchBothWays(*p, mute[layer], [] { return muted(layer); }, [](bool v) { setMute(layer, v); });
        switchBothWays(*p, solo[layer], [] { return soloed(layer); }, [](bool v) { setSolo(layer, v); });
    }
    // the status line follows
    p->set(idx("hold"), 1.0f), p->set(idx("lock"), 1.0f);
    run(*p, 4410);
    CHECK(p->getInt(kDispHold) == 1 && p->getInt(kDispLock) == 1, "status Hold %d Lock %d", p->getInt(kDispHold),
          p->getInt(kDispLock));
    p->set(idx("hold"), 0.0f), p->set(idx("lock"), 0.0f);
    run(*p, 4410);
    CHECK(p->getInt(kDispHold) == 0 && p->getInt(kDispLock) == 0, "status Hold %d Lock %d after off",
          p->getInt(kDispHold), p->getInt(kDispLock));
}

std::vector<uint8_t> project()
{
    std::vector<uint8_t> b(tp::app::kStoreMax);
    b.resize((size_t)tp::app::snapshot(b.data(), (int)b.size()));
    return b;
}

// A one-shot pressed: it goes back to Off; `changed` says whether it acted.
void press(ThirdPlanet& p, const char* id)
{
    p.set(idx(id), 1.0f);
    run(p, 2048);
    CHECK(!p.getBool(idx(id)), "%s stayed On", id);
}

void oneShots()
{
    using namespace tp::app;
    std::printf("5. one-shots\n");
    auto p = make();
    run(*p, 44100);
    // Reset piece: a value changed comes back to the recipe, once a press
    for (int k = 0; k < 2; ++k)
    {
        p->set(idx("echo"), 77.0f);
        run(*p, 2048);
        CHECK(par(P_ECHO) == 77, "echo %d", par(P_ECHO));
        int16_t rec[P_NPARAMS];
        uint16_t pool;
        en_piece(piece(), rec, &pool);
        press(*p, "reset_piece");
        CHECK(par(P_ECHO) == rec[P_ECHO], "press %d: Reset piece left echo %d (recipe %d)", k, par(P_ECHO), rec[P_ECHO]);
        CHECK(p->getInt(idx("echo")) == rec[P_ECHO], "echo shows %d after the reset", p->getInt(idx("echo")));
    }
    // each nudge, shake, New chords and Shake all changes the project; again on the next press
    const char* const acts[] = {"nudge_drone", "nudge_seq", "nudge_lead", "nudge_seq2", "nudge_space", "nudge_atmos",
                                "shake_drone", "shake_seq", "shake_lead", "shake_seq2", "shake_space", "shake_atmos",
                                "shake_all", "new_chords"};
    p->set(idx("evolve"), 0.0f);   // (Evolve's own nudges at rest)
    run(*p, 2048);
    for (const char* a : acts)
        for (int k = 0; k < 2; ++k)
        {
            const std::vector<uint8_t> before = project();
            press(*p, a);
            CHECK(project() != before, "%s (press %d) changed nothing", a, k);
        }
    // Forget and Next chord act on the engine only: back to Off is what shows
    press(*p, "forget");
    press(*p, "next_chord");
    // nothing fires on Off, nor on an Off the MPC writes again
    const std::vector<uint8_t> before = project();
    p->set(idx("shake_all"), 0.0f);
    run(*p, 2048);
    CHECK(project() == before, "Shake all fired on Off");
}

void patternAndChords()
{
    using namespace tp::app;
    std::printf("6. steps, accents, the chord root\n");
    auto p = make();
    run(*p, 44100);
    char id[16];
    for (int lane = 0; lane < 4; ++lane)
    {
        p->set(idx("lane"), (float)lane);
        run(*p, 4410);
        for (int st = 0; st < kNumSteps; ++st)
        {
            std::snprintf(id, sizeof id, "step%d", st + 1);
            CHECK(p->getBool(idx(id)) == step(st), "lane %d: %s shows %d, plays %d", lane, id, p->getBool(idx(id)), step(st));
        }
    }
    p->set(idx("lane"), 0.0f);
    run(*p, 4410);
    const bool was = step(3);
    p->set(idx("step4"), was ? 0.0f : 1.0f);
    run(*p, 4410);
    CHECK(step(3) == !was && par(P_SMODE) == 1, "step 4: %d (was %d), Pattern %d", step(3), was, par(P_SMODE));
    CHECK(p->getBool(idx("step4")) == !was, "step4 shows %d", p->getBool(idx("step4")));
    p->set(idx("step6"), 0.0f);
    run(*p, 4410);
    p->set(idx("accent6"), accent(5) ? 0.0f : 1.0f);   // an accent on: the step on too
    run(*p, 4410);
    if (accent(5))
        CHECK(step(5) && p->getBool(idx("step6")), "accent 6 on, step 6 %d shows %d", step(5), p->getBool(idx("step6")));

    // the root: of a written slot, a gap, and the walk's
    p->set(idx("chord_slot"), 0.0f);
    run(*p, 4410);
    const int iRoot = idx("chord_root");
    p->set(iRoot, 4.0f + 1.0f);   // E
    run(*p, 4410);
    CHECK(chordRoot() == 4 && p->getInt(iRoot) == 5, "root E: the slot has %d, shows %d", chordRoot(), p->getInt(iRoot));
    p->set(iRoot, 0.0f);   // "-": a gap
    run(*p, 4410);
    CHECK(chordRoot() == -1 && p->getInt(iRoot) == 0, "root '-': the slot has %d, shows %d", chordRoot(), p->getInt(iRoot));
    for (int s = 1; s < 8; ++s)
    {
        p->set(idx("chord_slot"), (float)s);
        run(*p, 4410);
        CHECK(p->getInt(iRoot) == chordRoot() + 1, "slot %d: root shows %d, the slot's is %d", s + 1, p->getInt(iRoot) - 1,
              chordRoot());
    }
}

void load()
{
    using namespace tp::app;
    std::printf("7. a project load\n");
    auto p = make();
    run(*p, 44100);
    p->set(idx("step2"), step(1) ? 0.0f : 1.0f);
    p->set(idx("echo"), 66.0f);
    p->set(idx("mute_lead"), 1.0f);
    run(*p, 4410);
    std::vector<int> savedPar(P_NPARAMS);
    for (int k = 0; k < P_NPARAMS; ++k) savedPar[(size_t)k] = par(k);
    std::vector<uint8_t> state = p->internalSaveState();
    p.reset();

    auto q = make();
    CHECK(q->internalLoadState(state.data(), state.size()), "state did not load");
    q->set(idx("reset_piece"), 1.0f);   // (a one-shot On from the MPC before the first block: dropped)
    q->set(idx("step2"), 0.5f);
    run(*q, 4410);
    CHECK(par(P_ECHO) == 66, "echo %d after the load (Reset piece fired?)", par(P_ECHO));
    CHECK(!q->getBool(idx("reset_piece")), "Reset piece shows On after the load");
    // (every parameter of the piece as saved: the Pattern's mode, gates and accents, the chord slots; the Lead's
    // memory moves on as it plays, so the rest of the project is not compared)
    int differ = 0;
    for (int k = 0; k < P_NPARAMS; ++k)
        if (par(k) != savedPar[(size_t)k] && differ++ < 5)
            std::printf("  par %d: saved %d, now %d\n", k, savedPar[(size_t)k], par(k));
    CHECK(differ == 0, "%d parameters differ after the load", differ);
    CHECK(q->getBool(idx("step2")) == step(1), "step2 shows %d, plays %d", q->getBool(idx("step2")), step(1));
    CHECK(muted(2) && q->getBool(idx("mute_lead")), "the Lead's mute did not come back from the load");
}

// A project saved with the Pattern on Auto: the MPC's step, accent and root values (stale, or written before the
// first block) must not edit the pattern (which would switch it to Pattern) or write a walk slot.
void loadAuto()
{
    using namespace tp::app;
    std::printf("7b. a project load, the Pattern on Auto\n");
    auto p = make();
    run(*p, 44100);
    CHECK(par(P_SMODE) == 0, "the Pattern is not on Auto at start");
    std::vector<int> savedPar(P_NPARAMS);
    for (int k = 0; k < P_NPARAMS; ++k) savedPar[(size_t)k] = par(k);
    std::vector<bool> steps(16), accents(16);
    for (int st = 0; st < 16; ++st) steps[(size_t)st] = step(st), accents[(size_t)st] = accent(st);
    const int root = chordRoot();
    std::vector<uint8_t> state = p->internalSaveState();
    p.reset();

    auto q = make();
    CHECK(q->internalLoadState(state.data(), state.size()), "state did not load");
    char id[16];
    for (int st = 0; st < 16; ++st)
    {
        std::snprintf(id, sizeof id, "step%d", st + 1);
        q->set(idx(id), steps[(size_t)st] ? 0.0f : 1.0f);
        std::snprintf(id, sizeof id, "accent%d", st + 1);
        q->set(idx(id), accents[(size_t)st] ? 0.0f : 1.0f);
    }
    q->set(idx("chord_root"), (float)((root + 6) % 12 + 1));
    run(*q, 4410);
    CHECK(par(P_SMODE) == 0, "the load switched the Pattern to %d", par(P_SMODE));
    int differ = 0;
    for (int k = 0; k < P_NPARAMS; ++k)
        if (par(k) != savedPar[(size_t)k] && differ++ < 5)
            std::printf("  par %d: saved %d, now %d\n", k, savedPar[(size_t)k], par(k));
    CHECK(differ == 0, "%d parameters differ after the load (a slot written, a step edited?)", differ);
}

} // namespace

int main()
{
    pads();
    switches();
    oneShots();
    patternAndChords();
    load();
    loadAuto();
    std::printf(fails ? "FAILED (%d)\n" : "test_controls: all passed\n", fails);
    return fails ? 1 : 0;
}
