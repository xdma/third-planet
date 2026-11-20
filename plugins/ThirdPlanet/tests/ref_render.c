/* SPDX-License-Identifier: GPL-3.0-only */
/* The reference: upstream's engine built as upstream builds it (C99, -O2 -ffp-contract=off, EN_HOST),
 * started as upstream's host/en_render.c starts it, written as raw float32 stereo (the engine's int32
 * sample / 2^23, every bit kept). tests/null_test.sh compares Third Planet's output against this.
 *   ref_render OUT.f32 SECS PIECE */
#define EN_HOST 1
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "amb.h"

void en_midi_out(uint32_t st, uint32_t d1, uint32_t d2) { (void)st, (void)d1, (void)d2; }

int main(int argc, char **argv)
{
    static int32_t buf[2 * 256];
    static float out[2 * 256];
    int16_t par[P_NPARAMS];
    uint16_t pool;
    uint32_t frames, done = 0, j;
    int i, piece;
    FILE *f;
    if (argc < 4)
        return 2;
    frames = (uint32_t)(atof(argv[2]) * 44100.0);
    piece = atoi(argv[3]);
    en_init();
    en_piece(piece, par, &pool);
    for (i = 0; i < P_NPARAMS; i++)
        if (i != P_TUNE && i != P_MIDI)
            en_set(i, par[i]);
    en_pool(pool);
    en_regrow();
    en_play(1);
    f = fopen(argv[1], "wb");
    if (!f)
        return 1;
    while (done < frames) {
        uint32_t n = frames - done > 256 ? 256 : frames - done;
        en_render(buf, 256, 2048);               /* (always whole chunks, as the plugin renders them) */
        for (j = 0; j < 2 * n; j++)
            out[j] = (float)buf[j] * (1.0f / 8388608.0f);
        fwrite(out, sizeof(float), 2 * n, f);
        done += n;
    }
    fclose(f);
    return 0;
}
