// SPDX-License-Identifier: GPL-3.0-only
// Sync, through the plugin's real entry point (VSTPluginMain) and a fake host that answers audioMasterGetTime:
// Sync on with the MPC stopped rests the piece; the MPC's play starts it on the external clock; Sync off
// while playing keeps it playing on its own clock (and off while stopped starts it).
//   c++ -std=c++17 -O1 -I plugins/ThirdPlanet/src -I deps/mpc-plugin-sdk/framework plugins/ThirdPlanet/tests/test_sync.cpp \
//       plugins/ThirdPlanet/src/app_unit.cpp plugins/ThirdPlanet/src/engine_unit.cpp plugins/ThirdPlanet/src/power.cpp plugins/ThirdPlanet/src/sky.cpp
#include <cmath>
#include <cstdio>
#include <cstring>

#include "ThirdPlanet.cpp"

namespace {

int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); fails++; } } while (0)

VstTimeInfo ti;
VstIntPtr host(AEffect*, VstInt32 opcode, VstInt32, VstIntPtr, void*, float)
{
    if (opcode == audioMasterVersion) return 2400;
    if (opcode == audioMasterGetTime) return (VstIntPtr)&ti;
    return 0;
}

AEffect* fx = nullptr;
float L[256], R[256];
void run(double secs)
{
    float* outs[2] = {L, R};
    const int n = 128;
    for (long done = 0; done < (long)(secs * 44100); done += n)
    {
        fx->processReplacing(fx, nullptr, outs, n);
        if (ti.flags & kVstTransportPlaying) ti.ppqPos += n * ti.tempo / 60.0 / 44100.0;
        if (ti.flags & kVstBarsValid) ti.barStartPos = 4.0 * std::floor(ti.ppqPos / 4.0);   // (4/4)
    }
}

// The MPC's sequence loop: ppqPos wraps back by loopBeats once it reaches loopBeats + overshoot (on the device: 0..8,
// then 0, on a 2-bar loop; docs/harmony-link-device-facts.md 1).
void runLoop(double secs, double loopBeats, double overshoot)
{
    float* outs[2] = {L, R};
    const int n = 128;
    ti.flags |= kVstBarsValid;
    for (long done = 0; done < (long)(secs * 44100); done += n)
    {
        fx->processReplacing(fx, nullptr, outs, n);
        if (ti.flags & kVstTransportPlaying) ti.ppqPos += n * ti.tempo / 60.0 / 44100.0;
        if (ti.ppqPos >= loopBeats + overshoot) ti.ppqPos -= loopBeats;
        ti.barStartPos = 4.0 * std::floor(ti.ppqPos / 4.0);   // (4/4)
    }
}

} // namespace

int main()
{
    std::memset(&ti, 0, sizeof ti);
    ti.sampleRate = 44100, ti.tempo = 120, ti.flags = kVstTempoValid | kVstPpqPosValid;
    fx = VSTPluginMain(host);
    fx->dispatcher(fx, effSetSampleRate, 0, 0, nullptr, 44100.0f);
    fx->dispatcher(fx, effSetBlockSize, 0, 128, nullptr, 0.0f);
    fx->dispatcher(fx, effMainsChanged, 0, 1, nullptr, 0.0f);
    int iSync = -1;
    for (int i = 0; i < tp::kNumControls; ++i)
        if (tp::kParams[i].kind == tp::Kind::Sync) iSync = i;

    run(1.0);
    CHECK(tp::app::playing() && !tp::app::external(), "not playing on its own clock at start");

    fx->setParameter(fx, iSync, 1.0f);   // Sync on, MPC stopped: rest
    run(1.0);
    CHECK(!tp::app::playing(), "Sync on with the MPC stopped: still playing");

    ti.flags |= kVstTransportPlaying;     // the MPC plays: start on its clock
    run(2.0);
    CHECK(tp::app::playing(), "the MPC plays: the piece did not start");
    CHECK(tp::app::external(), "the MPC plays: not on the external clock");
    CHECK(fx->getParameter(fx, kDispPad + kDExt) == 1.0f, "the MPC plays: the screen does not show EXT");
    const uint32_t bars0 = en_bars;
    run(2.0);                              // 2 s at 120 BPM = 4 beats = 16 sixteenths
    CHECK(en_bars - bars0 >= 15 && en_bars - bars0 <= 17, "2 s at 120 BPM gave %u sixteenths, not 16", en_bars - bars0);
    ti.tempo = 90;
    const uint32_t bars1 = en_bars;
    run(2.0);                              // 90 BPM: 12 sixteenths
    CHECK(en_bars - bars1 >= 11 && en_bars - bars1 <= 13, "2 s at 90 BPM gave %u sixteenths, not 12", en_bars - bars1);

    ti.flags &= ~kVstTransportPlaying;     // the MPC stops: rest
    run(1.0);
    CHECK(!tp::app::playing(), "the MPC stopped: still playing");

    ti.flags |= kVstTransportPlaying;
    run(1.0);
    fx->setParameter(fx, iSync, 0.0f);     // Sync off while playing: plays on, on its own clock
    run(2.0);
    CHECK(tp::app::playing(), "Sync off while playing: the piece stopped");
    CHECK(!tp::app::external(), "Sync off: still on the external clock");
    CHECK(fx->getParameter(fx, kDispPad + kDExt) == 0.0f, "Sync off: the screen still shows EXT");

    fx->setParameter(fx, iSync, 1.0f);
    ti.flags &= ~kVstTransportPlaying;
    run(1.0);
    CHECK(!tp::app::playing(), "Sync on, MPC stopped (2): still playing");
    fx->setParameter(fx, iSync, 0.0f);     // Sync off while rested: the piece starts again
    run(1.0);
    CHECK(tp::app::playing(), "Sync off while rested: the piece did not start");

    // T-1745: a start in the middle of a bar waits for the MPC's next bar line; a start on a bar line starts at once
    fx->setParameter(fx, iSync, 1.0f);
    ti.flags |= kVstBarsValid | kVstTimeSigValid;
    ti.timeSigNumerator = 4, ti.timeSigDenominator = 4, ti.tempo = 120;
    run(1.0);
    CHECK(!tp::app::playing(), "bar line: Sync on with the MPC stopped: still playing");
    ti.ppqPos = 9.5, ti.barStartPos = 8.0;  // (a play from the middle of bar 3)
    ti.flags |= kVstTransportPlaying;
    run(0.5);                              // (to ppq 10.5)
    CHECK(!tp::app::playing(), "bar line: a start mid-bar started at once");
    run(1.5);                              // (to ppq 13.5: past the bar line at 12)
    CHECK(tp::app::playing() && tp::app::external(), "bar line: not playing on the MPC's clock after the bar line");
    CHECK(en_bars >= 5 && en_bars <= 7, "bar line: %u sixteenths 1.5 beats after the bar line, not 6", en_bars);
    ti.flags &= ~kVstTransportPlaying;
    run(1.0);
    ti.ppqPos = 16.0, ti.barStartPos = 16.0;   // (a play from a bar line)
    ti.flags |= kVstTransportPlaying;
    run(0.1);
    CHECK(tp::app::playing(), "bar line: a start on a bar line did not start at once");

    // T-1757: a 2-bar MPC loop at 124 BPM; the clock must survive the wrap
    ti.tempo = 124; ti.ppqPos = 0; ti.flags |= kVstTransportPlaying | kVstBarsValid;
    runLoop(20.0, 8.0, 0.0);
    CHECK(tp::app::playing() && tp::app::external(), "T-1757: external clock lost after the loop wrapped (exact wrap)");
    const uint32_t bars2 = en_bars;
    runLoop(20.0, 8.0, 0.05);      // the host overshoots the loop end before jumping back
    CHECK(tp::app::playing() && tp::app::external(), "T-1757: external clock lost after the loop wrapped (overshoot)");
    // 20 s at 124 BPM = 41.3 beats = 165 sixteenths: one tick per 24th across each wrap, no burst, none lost
    CHECK(en_bars - bars2 >= 162 && en_bars - bars2 <= 168, "T-1757: 20 s of loops at 124 BPM gave %u sixteenths, not 165",
          en_bars - bars2);

    fx->dispatcher(fx, effClose, 0, 0, nullptr, 0.0f);
    std::printf(fails ? "FAILED (%d)\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
