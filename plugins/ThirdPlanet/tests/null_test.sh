#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# The engine null test: the engine as Third Planet compiles it (C++17, engine_unit.cpp) against the same
# engine built as upstream builds it (C99, -ffp-contract=off). Every piece, started as upstream's
# host/en_render.c starts it, must render identically, bit for bit. (tests/app_null.sh covers the app
# on top; tests/test_plugin.cpp the plugin's own glue.)
#   plugins/ThirdPlanet/tests/null_test.sh [SECONDS]   (from the repository root; default 60)
set -u
cd "$(dirname "$0")/../../.." || exit 1
secs="${1:-60}"
T=plugins/ThirdPlanet
B=$T/build/null
mkdir -p "$B"
cc -O2 -ffp-contract=off -std=c99 -D_GNU_SOURCE -DEN_HOST -I$T/src/nachtfahrt/dsp -o $B/ref_render \
    $T/tests/ref_render.c $T/src/nachtfahrt/dsp/amb.c -lm || exit 1
c++ -std=c++17 -O2 -I$T/src -o $B/engine_render $T/tests/engine_render.cpp $T/src/engine_unit.cpp || exit 1
cc -O2 -o $B/compare_f32 $T/tests/compare_f32.c -lm || exit 1
status=0
for piece in 0 1 2 3 4 5 6 7 8 9; do
    $B/ref_render $B/ref_$piece.f32 "$secs" $piece || { echo "ref_render failed"; exit 1; }
    $B/engine_render $B/eng.f32 "$secs" $piece || { echo "engine_render failed"; exit 1; }
    printf "piece %d: " $piece
    $B/compare_f32 $B/ref_$piece.f32 $B/eng.f32 || status=1
done
[ $status -eq 0 ] && echo "NULL TEST PASSED" || echo "NULL TEST FAILED"
exit $status
