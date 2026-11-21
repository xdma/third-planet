// SPDX-License-Identifier: GPL-3.0-only
// Third Planet's MPC controls against the FM-1's own gestures (spec 2026-11-20, section 5). Two boots, bit-exact
// alike: one plays the gesture on the virtual panel (PERFORM's keys, REC held with a key, a black key, HOME or GLO
// held on, the Pattern and Chords pages' keys), the other calls the setter the MPC control calls, at the
// millisecond the panel's input acts. The audio and the saved project must be identical.
//   c++ -std=c++17 -O2 -I plugins/ThirdPlanet/src -I deps/mpc-plugin-sdk/framework \
//       plugins/ThirdPlanet/tests/test_equiv.cpp plugins/ThirdPlanet/src/app_unit.cpp \
//       plugins/ThirdPlanet/src/engine_unit.cpp plugins/ThirdPlanet/src/power.cpp plugins/ThirdPlanet/src/sky.cpp \
//       plugins/ThirdPlanet/src/link_codec.cpp plugins/ThirdPlanet/src/link_capture.cpp \
//       plugins/ThirdPlanet/src/link_alsa.cpp plugins/ThirdPlanet/src/link_worker.cpp \
//       -o plugins/ThirdPlanet/build/tests/test_equiv
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "app_api.h"
#include "engine.h"

using namespace tp::app;

namespace {

int fails = 0, compared = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); fails++; } } while (0)

constexpr int kEnd = 5000;   // ms run (the actions within the first 2 s)

// The panel from a millisecond on: the buttons and keys held (bit = Button / key index).
struct Panel { int ms; uint32_t btn, keys; };
// A setter at a millisecond: called after that millisecond's step, where the panel's input acted.
struct Call { int ms; std::function<void()> f; };

struct Run { std::vector<int32_t> audio; std::vector<uint8_t> project; };

Run play(const std::vector<Panel>& panel, const std::vector<Call>& calls)
{
    storeWrite(nullptr, -1);
    boot();
    Run r;
    int32_t buf[2 * tp::kChunk];
    size_t p = 0, c = 0;
    for (int t = 1; t <= kEnd; ++t)
    {
        while (p < panel.size() && panel[p].ms == t) { setButtons(panel[p].btn), setKeys(panel[p].keys); ++p; }
        if (stepMs(buf)) r.audio.insert(r.audio.end(), buf, buf + 2 * tp::kChunk);
        while (c < calls.size() && calls[c].ms == t) { calls[c].f(); ++c; }
    }
    r.project.resize((size_t)kStoreMax);
    r.project.resize((size_t)snapshot(r.project.data(), kStoreMax));
    return r;
}

// `held`: a state the gesture leaves (read after it ran), for a gesture the music may not show in the time run
void same(const char* what, const std::vector<Panel>& panel, const std::vector<Call>& calls,
          const std::function<bool()>& held = nullptr)
{
    const Run none = play({}, {});
    const Run b = play({}, calls);
    const Run a = play(panel, {});   // (last: the app is left as the gesture left it)
    ++compared;
    long d = -1;
    for (size_t i = 0; i < a.audio.size() && i < b.audio.size(); ++i)
        if (a.audio[i] != b.audio[i]) { d = (long)(i / 2); break; }
    CHECK(a.audio.size() == b.audio.size() && d < 0, "%s: the audio differs from frame %ld", what, d);
    CHECK(a.project == b.project, "%s: the project differs", what);
    if (held) CHECK(held(), "%s: the gesture did not leave its state", what);
    else CHECK(a.audio != none.audio || a.project != none.project, "%s: the gesture changed nothing (test is blind)", what);
}

// key indices (ui.c): PERFORM's keys, the effects grid's, the black keys', the white keys (steps, degrees)
const int kPerfLayer[5] = {0, 2, 4, 6, 7}, kPerfSolo[5] = {1, 3, 5, 8, 10}, kPerfMove[5] = {11, 12, 14, 16, 18};
const int kWhite[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
const int kBlackMove[3] = {20, 22, 25};   // Rise, Close, Transpose
uint32_t B(int b) { return 1u << b; }
uint32_t K(int k) { return 1u << k; }

std::vector<Panel> perform(std::vector<Panel> then)   // SEL tapped (PERFORM), then the rest
{
    std::vector<Panel> g = {{100, B(kSEL), 0}, {140, 0, 0}};
    g.insert(g.end(), then.begin(), then.end());
    return g;
}

} // namespace

int main()
{
    char what[64];
    for (int l = 0; l < kNumLayers; ++l)
    {
        std::snprintf(what, sizeof what, "mute %d on and off", l);
        same(what, perform({{300, 0, K(kPerfLayer[l])}, {400, 0, 0}, {900, 0, K(kPerfLayer[l])}, {1000, 0, 0}}),
             {{300, [l] { setMute(l, true); }}, {900, [l] { setMute(l, false); }}}, [l] { return !muted(l); });
        std::snprintf(what, sizeof what, "mute %d on", l);
        same(what, perform({{300, 0, K(kPerfLayer[l])}, {400, 0, 0}}), {{300, [l] { setMute(l, true); }}},
             [l] { return muted(l); });
        std::snprintf(what, sizeof what, "solo %d on and off", l);
        same(what, perform({{300, 0, K(kPerfSolo[l])}, {400, 0, 0}, {900, 0, K(kPerfSolo[l])}, {1000, 0, 0}}),
             {{300, [l] { setSolo(l, true); }}, {900, [l] { setSolo(l, false); }}});
    }
    for (int e = 0; e < kNumFx; ++e)
    {
        std::snprintf(what, sizeof what, "effect %d (REC held, its key)", e);
        same(what, {{100, B(kREC), 0}, {200, B(kREC), K(kWhite[e])}, {700, B(kREC), 0}, {800, 0, 0}},
             {{200, [e] { padFx(e, true); }}, {700, [e] { padFx(e, false); }}});
    }
    for (int m = 0; m < 3; ++m)
    {
        std::snprintf(what, sizeof what, "move %d (a black key)", m);
        same(what, {{200, 0, K(kBlackMove[m])}, {900, 0, 0}},
             {{200, [m] { padMove(m, true); }}, {900, [m] { padMove(m, false); }}});
    }
    for (int m = 3; m < kNumMoves; ++m)
    {
        std::snprintf(what, sizeof what, "move %d (PERFORM's key)", m);
        same(what, perform({{300, 0, K(kPerfMove[m - 3])}, {1000, 0, 0}}),
             {{300, [m] { padMove(m, true); }}, {1000, [m] { padMove(m, false); }}});
    }
    for (int d : {0, 4, 9, 15})
    {
        std::snprintf(what, sizeof what, "Lead degree %d (a white key)", d);
        same(what, {{200, 0, K(kWhite[d])}, {900, 0, 0}},
             {{200, [d] { padLead(d, true); }}, {900, [d] { padLead(d, false); }}});
    }
    same("Shake all (HOME held on)", {{100, B(kHOME), 0}, {1800, 0, 0}}, {{1600, [] { shakeAll(); }}});
    same("Reset piece (GLO held on)", {{100, B(kGLO), 0}, {1800, 0, 0}}, {{1601, [] { resetPiece(); }}});
    for (int n = 0; n < 4; ++n)
    {
        static const int kButton[4] = {kENV, kARP, kLFO, kSEQ};
        std::snprintf(what, sizeof what, "nudge %d (its button held)", n);
        same(what, {{100, B(kButton[n]), 0}, {700, 0, 0}}, {{600, [n] { nudgeLayer(n, false); }}});
        std::snprintf(what, sizeof what, "shake %d (its button held on)", n);
        same(what, {{100, B(kButton[n]), 0}, {1700, 0, 0}},
             {{600, [n] { nudgeLayer(n, false); }}, {1600, [n] { nudgeLayer(n, true); }}});
    }

    // the Pattern page: SEQ tapped till it shows (Sequence 2, its sound, the Pattern), then the keys
    const std::vector<Panel> toPattern = {{100, B(kSEQ), 0}, {140, 0, 0}, {200, B(kSEQ), 0}, {240, 0, 0},
                                          {300, B(kSEQ), 0}, {340, 0, 0}};
    {
        std::vector<Panel> g = toPattern;
        g.push_back({500, 0, 0});
        storeWrite(nullptr, -1);
        boot();
        int32_t buf[2 * tp::kChunk];
        size_t p = 0;
        for (int t = 1; t <= 500; ++t)
        {
            while (p < g.size() && g[p].ms == t) { setButtons(g[p].btn), setKeys(g[p].keys); ++p; }
            stepMs(buf);
        }
        CHECK(std::string(viewName(view())) == "Pattern", "three SEQ taps show '%s', not the Pattern page", viewName(view()));
    }
    for (int st : {0, 3, 7, 15})
    {
        std::vector<Panel> g = toPattern;
        g.insert(g.end(), {{500, 0, K(kWhite[st])}, {600, 0, 0}, {900, 0, K(kWhite[st])}, {1000, 0, 0}});
        std::snprintf(what, sizeof what, "step %d on the Pattern page, twice", st);
        same(what, g, {{500, [st] { setStep(st, !step(st)); }}, {900, [st] { setStep(st, !step(st)); }}});
        g = toPattern;
        g.insert(g.end(), {{500, 0, K(1) | K(kWhite[st])}, {600, 0, 0}});
        std::snprintf(what, sizeof what, "accent %d (a black key with the white)", st);
        same(what, g, {{500, [st] { setAccent(st, !accent(st)); }}});
    }

    // the Chords page: EDIT tapped, then a key: the slot's root
    for (int k : {0, 5, 11})
    {
        const int root = (5 + k) % 12;
        std::snprintf(what, sizeof what, "chord root %d (key %d on the Chords page)", root, k);
        same(what, {{100, B(kEDIT), 0}, {140, 0, 0}, {300, 0, K(k)}, {400, 0, 0}},
             {{300, [root] { setChordRoot(root); }}});
    }

    std::printf(fails ? "FAILED (%d of %d)\n" : "test_equiv: %d controls match their gestures\n", fails ? fails : compared, compared);
    return fails ? 1 : 0;
}
