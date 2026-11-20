// SPDX-License-Identifier: GPL-3.0-only
// The engine as Third Planet compiles it (C++17, engine_unit.cpp), started as upstream's host/en_render.c starts
// it, as raw float32 stereo. tests/null_test.sh compares this against ref_render (the same engine built as C99
// with upstream's flags): the C++ build must change nothing.
//   engine_render OUT.f32 SECS PIECE
#include <cstdio>
#include <cstdlib>
#include <vector>

extern "C" {
#include "nachtfahrt/dsp/amb.h"
}

int main(int argc, char** argv)
{
    if (argc < 4) return 2;
    const long frames = (long)(std::atof(argv[2]) * 44100.0);
    const int piece = std::atoi(argv[3]);
    int16_t par[P_NPARAMS];
    uint16_t pool;
    en_init();
    en_piece(piece, par, &pool);
    for (int i = 0; i < P_NPARAMS; i++)
        if (i != P_TUNE && i != P_MIDI) en_set(i, par[i]);
    en_pool(pool);
    en_regrow();
    en_play(1);
    static int32_t buf[512];
    std::vector<float> out;
    out.reserve((size_t)frames * 2);
    for (long done = 0; done < frames;)
    {
        long n = frames - done > 256 ? 256 : frames - done;
        en_render(buf, 256, 2048);
        for (long j = 0; j < 2 * n; j++) out.push_back((float)buf[j] * (1.0f / 8388608.0f));
        done += n;
    }
    FILE* f = std::fopen(argv[1], "wb");
    if (!f) return 1;
    std::fwrite(out.data(), sizeof(float), out.size(), f);
    std::fclose(f);
    return 0;
}
