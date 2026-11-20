// SPDX-License-Identifier: GPL-3.0-only
// Third Planet's status line (the display parameters appended after the controls):
//   1. the chord's number makes exactly the name the FM-1's own screen shows (ui.c chord_now), on every
//      piece, with the walk's characters, in Endless and in Loop;
//   2. the plugin sends the page, the chord, PERFORM and Hold as they change, from the panel's notes;
//   3. a value the MPC writes over a status value is corrected on the next block;
//   4. a second instance shows that another one plays, and nothing else.
//   c++ -std=c++17 -O2 -I plugins/ThirdPlanet/src -I deps/mpc-plugin-sdk/framework \
//       plugins/ThirdPlanet/tests/test_display.cpp plugins/ThirdPlanet/src/app_unit.cpp \
//       plugins/ThirdPlanet/src/engine_unit.cpp plugins/ThirdPlanet/src/power.cpp \
//       -o plugins/ThirdPlanet/build/tests/test_display
#include <cstdio>
#include <cstdlib>
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

std::unique_ptr<ThirdPlanet> make()
{
    auto p = std::make_unique<ThirdPlanet>();
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

// A button tapped (held 100 ms) at `t` seconds.
void tap(std::vector<Ev>& evs, double t, int button)
{
    const long f = (long)(t * 44100.0);
    evs.push_back({f, 0x90, kButtonNote0 + button, 100});
    evs.push_back({f + 4410, 0x80, kButtonNote0 + button, 0});
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
    CHECK(p->getInt(kDispPage) == tp::app::view() + 1, "page %d, view %d", p->getInt(kDispPage), tp::app::view());
    CHECK(text(*p, kDispPage) == "Nachtfahrt", "page shows '%s'", text(*p, kDispPage).c_str());
    char now[64];
    tp::app::chordNow(now);
    CHECK(text(*p, kDispChord) == now, "chord shows '%s', the FM-1 '%s'", text(*p, kDispChord).c_str(), now);
    CHECK(text(*p, kDispPerform) == " " && text(*p, kDispHold) == " ", "PERFORM or Hold shown at power-on");

    std::vector<Ev> evs;
    tap(evs, 0.2, tp::app::kSEL);   // PERFORM
    tap(evs, 0.6, tp::app::kREC);   // Hold
    runPlugin(*p, 44100, evs);
    CHECK(tp::app::perform() && tp::app::held(), "the taps did not reach the app");
    CHECK(text(*p, kDispPerform) == "PERFORM", "perform shows '%s'", text(*p, kDispPerform).c_str());
    CHECK(text(*p, kDispHold) == "HOLD", "hold shows '%s'", text(*p, kDispHold).c_str());
    CHECK(p->getInt(kDispPage) == tp::app::view() + 1, "page %d after PERFORM, view %d", p->getInt(kDispPage), tp::app::view());

    evs.clear();
    tap(evs, 0.2, tp::app::kSEL);
    tap(evs, 0.6, tp::app::kREC);
    tap(evs, 1.0, tp::app::kENV);   // the Drone's page
    runPlugin(*p, 66150, evs);
    CHECK(text(*p, kDispPerform) == " " && text(*p, kDispHold) == " ", "PERFORM '%s' Hold '%s' after both off",
          text(*p, kDispPerform).c_str(), text(*p, kDispHold).c_str());
    CHECK(text(*p, kDispPage) == tp::app::viewName(tp::app::view()) && tp::app::view() != 0, "page shows '%s', view %d",
          text(*p, kDispPage).c_str(), tp::app::view());

    // The MPC writes over a status value (a load, automation): the true one comes back.
    const int page = p->getInt(kDispPage);
    p->setNormalised(kDispPage, 0.0f);
    p->setNormalised(kDispHold, 1.0f);
    runPlugin(*p, 4410 + 256);
    CHECK(p->getInt(kDispPage) == page && p->getInt(kDispHold) == 0, "status not corrected: page %d hold %d",
          p->getInt(kDispPage), p->getInt(kDispHold));

    // A second instance: the message, nothing else.
    auto q = make();
    runPlugin(*q, 4410);
    CHECK(text(*q, kDispPage) == kBusyText, "second instance's page shows '%s'", text(*q, kDispPage).c_str());
    CHECK(text(*q, kDispChord) == " " && text(*q, kDispPerform) == " " && text(*q, kDispHold) == " ",
          "second instance shows a status");
    p.reset();   // the owner removed: the second takes over and shows its own status
    runPlugin(*q, 44100);
    CHECK(q->getInt(kDispPage) == tp::app::view() + 1, "after taking over, page %d view %d", q->getInt(kDispPage), tp::app::view());
}

} // namespace

int main()
{
    chordNames();
    chordValues();
    statusLine();
    if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
    std::printf("test_display: all passed\n");
    return 0;
}
