// SPDX-License-Identifier: GPL-3.0-only
// Third Planet's plugin glue (ThirdPlanet.cpp) over the FM-1 app:
//   1. its output is the app's own, bit for bit, at any block size (so the glue sends the app nothing by itself:
//      no parameter echo, no stray input);
//   2. pads and notes act the same at any block size (tests/test_controls.cpp: what each one does);
//   3. parameters reach the app, and what the app changes comes back (a piece's values);
//   4. the project survives a save and a load;
//   5. a second instance stays silent while the first owns the engine;
//   0. (first) a boot after the FM-1 has played is a true power-on: the same music as the first boot.
//   c++ -std=c++17 -O2 -I plugins/ThirdPlanet/src -I deps/mpc-plugin-sdk/framework \
//       plugins/ThirdPlanet/tests/test_plugin.cpp plugins/ThirdPlanet/src/app_unit.cpp \
//       plugins/ThirdPlanet/src/engine_unit.cpp plugins/ThirdPlanet/src/power.cpp plugins/ThirdPlanet/src/sky.cpp \
//       -o plugins/ThirdPlanet/build/tests/test_plugin
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include "ThirdPlanet.cpp"
#include "power.h"

namespace {

int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); fails++; } } while (0)

int paramIndex(const char* id)
{
    for (int i = 0; i < tp::kNumControls; ++i)
        if (!std::strcmp(tp::kParams[i].id, id)) return i;
    std::printf("no parameter %s\n", id);
    std::exit(2);
}

struct Ev { long frame; int status, d1, d2; };

// The plugin rendered for `frames` at a block size (0: random 1..512), with MIDI events at given frames.
std::vector<float> runPlugin(ThirdPlanet& p, long frames, int block, const std::vector<Ev>& evs = {})
{
    std::vector<float> l(1024), r(1024), out;
    float* outs[2] = {l.data(), r.data()};
    unsigned rng = 777u;
    size_t e = 0;
    for (long done = 0; done < frames;)
    {
        int n = block;
        if (n <= 0) { rng = rng * 1664525u + 1013904223u; n = 1 + (int)((rng >> 8) % 512u); }
        if (n > frames - done) n = (int)(frames - done);
        if (e < evs.size() && evs[e].frame < done + n && evs[e].frame > done) n = (int)(evs[e].frame - done);
        uint8_t storage[sizeof(VstEvents) + 16 * sizeof(VstEvent*)];
        VstMidiEvent me[16];
        VstEvents* ve = reinterpret_cast<VstEvents*>(storage);
        ve->numEvents = 0;
        while (e < evs.size() && evs[e].frame == done && ve->numEvents < 16)
        {
            VstMidiEvent& m = me[ve->numEvents];
            std::memset(&m, 0, sizeof m);
            m.type = kVstMidiType, m.byteSize = sizeof m;
            m.midiData[0] = (char)evs[e].status, m.midiData[1] = (char)evs[e].d1, m.midiData[2] = (char)evs[e].d2;
            ve->events[ve->numEvents++] = reinterpret_cast<VstEvent*>(&m);
            e++;
        }
        if (ve->numEvents) p.internalAddMidi(ve);
        p.internalProcess(nullptr, outs, n);
        for (int i = 0; i < n; ++i) out.push_back(l[(size_t)i]), out.push_back(r[(size_t)i]);
        done += n;
    }
    return out;
}

std::unique_ptr<ThirdPlanet> make()
{
    auto p = std::make_unique<ThirdPlanet>();
    p->internalSetSampleRate(44100.0);
    p->internalSetBlockSize(1024);
    p->internalResume();
    return p;
}

// The app alone, from power-on, as upstream's simulator runs it.
std::vector<float> runApp(long frames)
{
    tp::app::storeWrite(nullptr, -1);
    tp::app::boot();
    std::vector<float> out;
    static int32_t buf[512];
    while ((long)out.size() < frames * 2)
        if (tp::app::stepMs(buf))
            for (int k = 0; k < 512; ++k) out.push_back((float)buf[k] * tp::kOutScale);
    out.resize((size_t)frames * 2);
    return out;
}

long firstDiff(const std::vector<float>& a, const std::vector<float>& b)
{
    if (a.size() != b.size()) return -2;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) return (long)(i / 2);
    return -1;
}

} // namespace

int main()
{
    const long frames = 15 * 44100;
    const std::vector<float> ref = runApp(frames);

    std::printf("0. a boot again is a power-on (variables %ld + %ld bytes)\n", tp::power_data_bytes(), tp::power_bss_bytes());
    {
        tp::app::setKeys(1u << 7);   // (leave the FM-1 in some state: a key held, PERFORM on, then boot)
        static int32_t buf[512];
        for (int i = 0; i < 3000; ++i) tp::app::stepMs(buf);
        tp::app::setButtons(1u << tp::app::kSEL);
        for (int i = 0; i < 100; ++i) tp::app::stepMs(buf);
        tp::app::setButtons(0), tp::app::setKeys(0);
        for (int i = 0; i < 100; ++i) tp::app::stepMs(buf);
        long d = firstDiff(ref, runApp(frames));
        CHECK(d == -1, "the second boot differs from the first at frame %ld", d);
    }

    std::printf("1. the app's own output at any block size\n");
    for (int block : {64, 128, 333, 512, 1000, 0})
    {
        auto p = make();
        long d = firstDiff(ref, runPlugin(*p, frames, block));
        CHECK(d == -1, "block %d differs from the app alone at frame %ld", block, d);
    }

    std::printf("2. pads and notes, the same at any block size\n");
    {
        // (Keys: Pads, Pad base 36) a Lead degree, an effect held, a move held, a note outside the banks (MIDI in)
        const std::vector<Ev> evs = {{2048 * 40, 0x90, 40, 100}, {2048 * 60, 0x80, 40, 0}, {2048 * 80, 0x90, 55, 100},
                                     {2048 * 130, 0x80, 55, 0}, {2048 * 150, 0x90, 71, 100}, {2048 * 170, 0x80, 71, 0},
                                     {2048 * 230, 0x90, 84, 90}, {2048 * 250, 0x80, 84, 0}};
        std::vector<float> first;
        for (int block : {512, 64, 128, 256})
        {
            auto p = make();
            std::vector<float> got = runPlugin(*p, frames, block, evs);
            if (first.empty())
            {
                first = got;
                CHECK(firstDiff(ref, got) >= 2048 * 40 - 256, "the notes changed nothing (or too early): %ld", firstDiff(ref, got));
            }
            else
            {
                long d = firstDiff(first, got);
                CHECK(d == -1, "block %d with notes differs from block 512 at frame %ld", block, d);
            }
        }
    }

    std::printf("3. parameters to the app and back\n");
    {
        auto p = make();
        runPlugin(*p, 4410, 128);
        const int iPiece = paramIndex("piece"), iTempo = paramIndex("tempo"), iEcho = paramIndex("echo");
        CHECK(p->getInt(iTempo) == tp::app::par(P_TEMPO), "tempo shows %d, the app has %d", p->getInt(iTempo), tp::app::par(P_TEMPO));
        p->set(iPiece, 3.0f);
        runPlugin(*p, 44100, 128);
        CHECK(tp::app::piece() == 3, "piece %d after asking for 3", tp::app::piece());
        int16_t rec[P_NPARAMS];
        uint16_t pool;
        en_piece(3, rec, &pool);
        CHECK(p->getInt(iTempo) == rec[P_TEMPO], "tempo shows %d, piece 3's is %d", p->getInt(iTempo), rec[P_TEMPO]);
        p->set(iEcho, 77.0f);
        runPlugin(*p, 4410, 128);
        CHECK(tp::app::par(P_ECHO) == 77, "echo %d after asking for 77", tp::app::par(P_ECHO));
        CHECK(p->getInt(iEcho) == 77, "echo shows %d", p->getInt(iEcho));

        std::printf("4. the project through a save and a load\n");
        std::vector<uint8_t> state = p->internalSaveState();
        p.reset();
        auto q = make();
        CHECK(q->internalLoadState(state.data(), state.size()), "state did not load");
        runPlugin(*q, 44100, 128);
        CHECK(tp::app::piece() == 3, "piece %d after the load", tp::app::piece());
        CHECK(tp::app::par(P_ECHO) == 77, "echo %d after the load", tp::app::par(P_ECHO));
        CHECK(q->getInt(iEcho) == 77, "echo shows %d after the load", q->getInt(iEcho));

        std::printf("5. a second instance stays silent\n");
        auto other = make();
        std::vector<float> o = runPlugin(*other, 44100, 128);
        float peak = 0;
        for (float v : o) peak = std::fmax(peak, std::fabs(v));
        CHECK(peak == 0.0f, "the second instance sounds (peak %f)", (double)peak);
        std::vector<float> still = runPlugin(*q, 44100, 128);
        peak = 0;
        for (float v : still) peak = std::fmax(peak, std::fabs(v));
        CHECK(peak > 0.001f, "the owner went quiet (peak %f)", (double)peak);
        q.reset();
        o = runPlugin(*other, 44100, 128);
        peak = 0;
        for (float v : o) peak = std::fmax(peak, std::fabs(v));
        CHECK(peak > 0.001f, "the second instance did not take over when the first went (peak %f)", (double)peak);
    }

    std::printf(fails ? "FAILED (%d)\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
