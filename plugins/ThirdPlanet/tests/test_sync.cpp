// SPDX-License-Identifier: GPL-3.0-only
// Sync, through the plugin's real entry point (VSTPluginMain) and a fake host that answers audioMasterGetTime:
// Sync on with the MPC stopped rests the piece; the MPC's play starts it on the external clock; Sync off
// while playing keeps it playing on its own clock (and off while stopped starts it).
//   c++ -std=c++17 -O1 -I plugins/ThirdPlanet/src -I deps/mpc-plugin-sdk/framework plugins/ThirdPlanet/tests/test_sync.cpp \
//       plugins/ThirdPlanet/src/app_unit.cpp plugins/ThirdPlanet/src/engine_unit.cpp plugins/ThirdPlanet/src/power.cpp plugins/ThirdPlanet/src/sky.cpp \
//       plugins/ThirdPlanet/src/link_codec.cpp plugins/ThirdPlanet/src/link_capture.cpp \
//       plugins/ThirdPlanet/src/link_alsa.cpp plugins/ThirdPlanet/src/link_worker.cpp
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
float L[512], R[512];
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

// The MPC's sequence loop, 2 bars of 4/4 at 124 BPM from the top, for secs. How the position gets back to the loop's
// start is not known block by block (docs/harmony-link-device-facts.md 1), so every shape a host may give:
enum Wrap {
    kNoWrap,      // (the control: the position runs on)
    kCarry,       // the block that crosses the loop's end starts the next one that far past its start (7.997, 0.003)
    kOvershoot,   // the position runs 0.05 beats past the loop's end over several blocks, then jumps back 8 beats
    kSnap         // the block after the one that crosses the loop's end starts at 0 exactly
};
// Returns the engine's sixteenths since the start minus the host's (its position with the wraps undone, in whole
// 16ths): 0 is in step, +1 a sixteenth ahead. `burst`: the most sixteenths one block gave.
int loopDrift(double secs, int n, Wrap wrap, int& burst, bool& ext)
{
    float* outs[2] = {L, R};
    ti.flags &= ~kVstTransportPlaying;     // (stop first, so the only jumps back are the loop's wraps)
    run(1.0);
    ti.tempo = 124, ti.ppqPos = 0, ti.barStartPos = 0;
    ti.flags |= kVstTransportPlaying | kVstBarsValid;
    const double loopBeats = 8.0;
    long wraps = 0;
    uint32_t was = en_bars;
    burst = 0;
    for (long done = 0; done < (long)(secs * 44100); done += n)
    {
        fx->processReplacing(fx, nullptr, outs, n);
        // (the Start puts the engine's count back to 0, in this block or the next; from then on it only goes up)
        if (en_bars > was && (int)(en_bars - was) > burst) burst = (int)(en_bars - was);
        was = en_bars;
        ti.ppqPos += n * ti.tempo / 60.0 / 44100.0;
        if (wrap == kCarry && ti.ppqPos >= loopBeats) ti.ppqPos -= loopBeats, ++wraps;
        else if (wrap == kOvershoot && ti.ppqPos >= loopBeats + 0.05) ti.ppqPos -= loopBeats, ++wraps;
        else if (wrap == kSnap && ti.ppqPos >= loopBeats) ti.ppqPos = 0.0, ++wraps;
        ti.barStartPos = 4.0 * std::floor(ti.ppqPos / 4.0);   // (4/4)
    }
    ext = tp::app::playing() && tp::app::external();
    // (the engine's count since the Start includes the sixteenth at the host's position 0)
    return (int)en_bars - 1 - (int)std::floor(((double)wraps * loopBeats + ti.ppqPos) * 4.0 + 1e-6);
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

    // T-1757: a 2-bar MPC loop at 124 BPM, 8 minutes of it (124 wraps). The engine stays on the host's bars: within a
    // sixteenth of the host's position however the loop wraps, at the MPC's block and at a long one; never two
    // sixteenths in one block (no burst of clocks), and the external clock is never lost.
    {
        const char* const kWrapName[] = {"no wrap", "carry", "overshoot", "snap to 0"};
        const auto loops = [&](int n, Wrap w) {
            int burst = 0;
            bool ext = false;
            // (480 s and half a sixteenth: the count is read in the middle of a sixteenth, not on its edge)
            const int d = loopDrift(480.0 + 0.5 * 15.0 / 124.0, n, w, burst, ext);
            std::printf("T-1757: 480 s, blocks of %d, %s: the engine %+d sixteenths from the host\n", n, kWrapName[w], d);
            CHECK(ext, "T-1757: external clock lost (%s, blocks of %d)", kWrapName[w], n);
            CHECK(d >= -1 && d <= 1, "T-1757: 480 s of 2-bar loops (%s, blocks of %d): the engine is %+d sixteenths from the host",
                  kWrapName[w], n, d);
            CHECK(burst <= 1, "T-1757: %d sixteenths in one block (%s, blocks of %d)", burst, kWrapName[w], n);
        };
        for (int n : {128, 512})
            for (Wrap w : {kNoWrap, kCarry, kOvershoot, kSnap})
                loops(n, w);
        // (blocks of 508 at 124 BPM: the block that crosses the loop's end runs 0.54 of a clock past it, every wrap.
        // Taking the jump to the nearest clock would count it as 193 and send the loop's first clock twice)
        loops(508, kSnap);
    }

    // A locate back while playing (not a loop: 26.7 beats, no whole number of clocks): the clock goes on from there,
    // at the tempo, with no burst and no stall, and the engine keeps its external clock.
    {
        ti.flags &= ~kVstTransportPlaying;
        run(1.0);
        ti.tempo = 124, ti.ppqPos = 32.0, ti.barStartPos = 32.0;
        ti.flags |= kVstTransportPlaying;
        run(8.0 * 60.0 / 124.0);               // (to ppq 40)
        ti.ppqPos = 13.3, ti.barStartPos = 12.0;
        float* outs[2] = {L, R};
        const uint32_t bars0 = en_bars;
        uint32_t was = en_bars;
        int burst = 0, stall = 0, quiet = 0;
        const double secs = 10.0;              // 10 s at 124 BPM = 82.7 sixteenths
        for (long done = 0; done < (long)(secs * 44100); done += 128)
        {
            fx->processReplacing(fx, nullptr, outs, 128);
            if ((int)(en_bars - was) > burst) burst = (int)(en_bars - was);
            quiet = en_bars == was ? quiet + 1 : 0;
            if (quiet > stall) stall = quiet;
            was = en_bars;
            ti.ppqPos += 128 * ti.tempo / 60.0 / 44100.0;
            ti.barStartPos = 4.0 * std::floor(ti.ppqPos / 4.0);
        }
        const int got = (int)(en_bars - bars0);
        std::printf("locate back: %d sixteenths in 10 s, the longest gap %d blocks, at most %d in a block\n", got, stall, burst);
        CHECK(tp::app::playing() && tp::app::external(), "locate back: external clock lost");
        CHECK(got >= 82 && got <= 84, "locate back: %d sixteenths in the 10 s after it, not 83", got);
        CHECK(burst <= 1, "locate back: %d sixteenths in one block", burst);
        CHECK(stall <= 50, "locate back: no sixteenth for %d blocks (one is 42)", stall);
    }

    fx->dispatcher(fx, effClose, 0, 0, nullptr, 0.0f);
    std::printf(fails ? "FAILED (%d)\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
