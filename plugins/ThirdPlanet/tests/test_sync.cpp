// SPDX-License-Identifier: GPL-3.0-only
// Sync, through the plugin's real entry point (VSTPluginMain) and a fake host that answers audioMasterGetTime:
// Sync on with the MPC stopped rests the piece; the MPC's play starts it on the external clock; Sync off
// while playing keeps it playing on its own clock (and off while stopped starts it).
//   c++ -std=c++17 -O1 -I plugins/ThirdPlanet/src -I deps/mpc-plugin-sdk/framework plugins/ThirdPlanet/tests/test_sync.cpp \
//       plugins/ThirdPlanet/src/app_unit.cpp plugins/ThirdPlanet/src/engine_unit.cpp plugins/ThirdPlanet/src/power.cpp plugins/ThirdPlanet/src/sky.cpp
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

    fx->setParameter(fx, iSync, 1.0f);
    ti.flags &= ~kVstTransportPlaying;
    run(1.0);
    CHECK(!tp::app::playing(), "Sync on, MPC stopped (2): still playing");
    fx->setParameter(fx, iSync, 0.0f);     // Sync off while rested: the piece starts again
    run(1.0);
    CHECK(tp::app::playing(), "Sync off while rested: the piece did not start");

    fx->dispatcher(fx, effClose, 0, 0, nullptr, 0.0f);
    std::printf(fails ? "FAILED (%d)\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
