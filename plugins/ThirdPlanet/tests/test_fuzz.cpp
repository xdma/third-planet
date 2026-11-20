// SPDX-License-Identifier: GPL-3.0-only
// A random-input probe of the whole plugin: random notes over the full range (keys, buttons, MIDI in), held and
// released at random, all-notes-off, random parameter values, saves and loads mid-stream, random block sizes.
// The output must stay finite and bounded, and nothing may crash (run under ASan and UBSan).
//   TP_FUZZ_SECONDS=N (default 120) of audio per seed; seeds 1..TP_FUZZ_SEEDS (default 3).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include "ThirdPlanet.cpp"

int main()
{
    const char* e = std::getenv("TP_FUZZ_SECONDS");
    const double secs = e ? std::atof(e) : 120.0;
    e = std::getenv("TP_FUZZ_SEEDS");
    const int seeds = e ? std::atoi(e) : 3;
    int fails = 0;
    for (int seed = 1; seed <= seeds; ++seed)
    {
        unsigned rng = 0x1234567u * (unsigned)seed;
        auto rnd = [&](unsigned n) { rng = rng * 1664525u + 1013904223u; return (rng >> 8) % n; };
        auto p = std::make_unique<ThirdPlanet>();
        p->internalSetSampleRate(44100.0), p->internalSetBlockSize(1024), p->internalResume();
        std::vector<float> l(1024), r(1024);
        float* outs[2] = {l.data(), r.data()};
        std::vector<uint8_t> saved;
        float peak = 0;
        long frames = 0, bad = 0;
        const long total = (long)(secs * 44100);
        while (frames < total)
        {
            const int n = 1 + (int)rnd(1000);
            uint8_t storage[sizeof(VstEvents) + 8 * sizeof(VstEvent*)];
            VstMidiEvent me[8];
            VstEvents* ve = reinterpret_cast<VstEvents*>(storage);
            ve->numEvents = 0;
            const int nev = rnd(4) == 0 ? (int)rnd(8) : 0;
            for (int k = 0; k < nev; ++k)
            {
                VstMidiEvent& m = me[k];
                std::memset(&m, 0, sizeof m);
                m.type = kVstMidiType, m.byteSize = sizeof m, m.deltaFrames = (int)rnd((unsigned)n);
                const unsigned what = rnd(20);
                if (what == 0) { m.midiData[0] = (char)0xB0, m.midiData[1] = 123, m.midiData[2] = 0; }
                else
                {
                    const int note = rnd(3) ? 36 + (int)rnd(44) : (int)rnd(128);   // mostly the panel
                    m.midiData[0] = (char)(rnd(2) ? 0x90 : 0x80), m.midiData[1] = (char)note, m.midiData[2] = (char)(1 + rnd(127));
                }
                ve->events[ve->numEvents++] = reinterpret_cast<VstEvent*>(&m);
            }
            if (ve->numEvents) p->internalAddMidi(ve);
            if (rnd(50) == 0)
            {
                const int i = (int)rnd((unsigned)p->numParams());
                if (i != 0) p->setNormalised(i, (float)rnd(1001) / 1000.0f);   // (Volume left alone: loudness)
            }
            if (rnd(4000) == 0) saved = p->internalSaveState();
            if (rnd(6000) == 0 && !saved.empty()) p->internalLoadState(saved.data(), saved.size());
            p->internalProcess(nullptr, outs, n);
            for (int i = 0; i < n; ++i)
            {
                if (!std::isfinite(l[(size_t)i]) || !std::isfinite(r[(size_t)i])) bad++;
                peak = std::fmax(peak, std::fmax(std::fabs(l[(size_t)i]), std::fabs(r[(size_t)i])));
            }
            frames += n;
        }
        std::printf("seed %d: %.0f s, peak %.2f (%.1f dBFS), %ld non-finite\n", seed, secs, (double)peak,
                    20.0 * std::log10(std::fmax(peak, 1e-9f)), bad);
        if (bad || peak > 8.0f) fails++;
    }
    std::printf(fails ? "FUZZ FAILED\n" : "FUZZ PASSED\n");
    return fails ? 1 : 0;
}
