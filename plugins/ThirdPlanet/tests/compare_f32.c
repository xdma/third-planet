/* SPDX-License-Identifier: GPL-3.0-only */
/* compare_f32 A.f32 B.f32: the largest sample difference, in the engine's units (1 = one step of 2^-23),
 * the first frame that differs, and exit 1 if the files differ at all (or in length). */
#include <stdio.h>
#include <math.h>
int main(int argc, char **argv)
{
    FILE *a, *b;
    float x, y;
    long i = 0, first = -1;
    double worst = 0.0, peak = 0.0;
    if (argc < 3 || !(a = fopen(argv[1], "rb")) || !(b = fopen(argv[2], "rb")))
        return 2;
    for (;;) {
        size_t na = fread(&x, 4, 1, a), nb = fread(&y, 4, 1, b);
        if (na != nb) { printf("length differs at sample %ld\n", i); return 1; }
        if (!na) break;
        if (fabs(x) > peak) peak = fabs(x);
        if (x != y) {
            double d = fabs((double)x - (double)y) * 8388608.0;
            if (first < 0) first = i / 2;
            if (d > worst) worst = d;
        }
        i++;
    }
    printf("%ld frames, peak %.1f dBFS, ", i / 2, peak > 0 ? 20.0 * log10(peak) : -999.0);
    if (first < 0) { printf("identical\n"); return 0; }
    printf("DIFFER from frame %ld, worst %.0f steps\n", first, worst);
    return 1;
}
