#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# The app null test: Third Planet's FM-1 app (app_unit.cpp: ui.c and project.c on the virtual panel, with the
# engine) against upstream's own host simulator, host/amb_host.c, built from the unpatched upstream sources the
# way upstream builds it (C99, -ffp-contract=off). Both run the same scripts; every WAV must match byte for byte.
# Both draw with the blank font (src/felucca_font.h): the screen never reaches the sound.
#   plugins/ThirdPlanet/tests/app_null.sh   (from the repository root)
set -u
cd "$(dirname "$0")/../../.." || exit 1
T=$PWD/plugins/ThirdPlanet
B=$T/build/appnull
R=$B/ref
rm -rf "$R" && mkdir -p "$R/host" "$R/firmware/src/app" "$R/gen" "$R/words"
ln -s "$T/tests/upstream/amb_host.c" "$R/host/amb_host.c"
ln -s "$T/src/nachtfahrt/gfx.c" "$R/firmware/src/gfx.c"
ln -s "$T/tests/upstream/announce.c" "$R/firmware/src/announce.c"
ln -s "$T/src/nachtfahrt/dsp" "$R/firmware/src/dsp"
for f in app.h plat.h project.c; do ln -s "$T/src/nachtfahrt/app/$f" "$R/firmware/src/app/$f"; done
ln -s "$T/tests/upstream/orig/ui.c" "$R/firmware/src/app/ui.c"   # (upstream's, without Third Planet's two C++ patches)
python3 -I "$T/tests/upstream/gen_words.py" "$T/tests/upstream/words.tsv" "$R/words" "$R/gen/ann_words.h" >/dev/null || exit 1
cc -O2 -ffp-contract=off -std=c99 -D_GNU_SOURCE -w -DEN_HOST -I"$R/firmware/src" -I"$R/firmware/src/dsp" \
    -I"$R/gen" -I"$T/src" -o "$B/amb_host" "$R/host/amb_host.c" "$R/firmware/src/dsp/amb.c" -lm || exit 1
c++ -std=c++17 -O2 -I"$T/src" -o "$B/app_render" "$T/tests/app_render.cpp" "$T/src/app_unit.cpp" \
    "$T/src/engine_unit.cpp" "$T/src/power.cpp" || exit 1
# upstream's own scenarios too, those that drive the device only with commands app_render runs (the checks in
# them, expect and said, are skipped by app_render; a screen reader that listens, reboots, saved files and bar
# cues are left out), each recorded whole
mkdir -p "$B/upstream_scen"
for u in "$T"/tests/upstream/scenarios/*.amb; do
    grep -qE '^(listen|reboot|storefile|orphans|bar|at|splash|splashrun|film|where|wav)( |$)' "$u" && continue
    { echo "wav out.wav"; cat "$u"; } > "$B/upstream_scen/up-$(basename "$u")"
done
status=0
for s in "$T"/tests/app_scenarios/*.amb "$B"/upstream_scen/*.amb; do
    n=$(basename "$s" .amb)
    mkdir -p "$B/$n/ref" "$B/$n/plug"
    "$B/amb_host" "$s" "$B/$n/ref" >/dev/null   # (its expect checks may fail without a screen reader: not ours)
    [ -f "$B/$n/ref/out.wav" ] || { echo "$n: amb_host wrote no audio"; status=1; continue; }
    "$B/app_render" "$s" "$B/$n/plug" || { echo "$n: app_render failed"; status=1; continue; }
    a="$B/$n/ref/out.wav" p="$B/$n/plug/out.wav"
    if cmp -s <(tail -c +45 "$a") <(tail -c +45 "$p"); then
        echo "$n: identical ($(( ($(wc -c < "$a") - 44) / 4 )) frames)"
    else
        echo "$n: DIFFER at byte $(cmp <(tail -c +45 "$a") <(tail -c +45 "$p") | awk '{print $5}')"
        status=1
    fi
done
[ $status -eq 0 ] && echo "APP NULL TEST PASSED" || echo "APP NULL TEST FAILED"
exit $status
