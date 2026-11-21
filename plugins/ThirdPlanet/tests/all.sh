#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Every C++ test of README.md's table, built and run twice: with Harmony Link compiled out (TP_LINK=0) and in
# (TP_LINK=1). The null tests (null_test.sh, app_null.sh, link_null.sh) and host_sim are separate.
#   plugins/ThirdPlanet/tests/all.sh [0|1]   (from the repository root; default both)
set -u
cd "$(dirname "$0")/../../.." || exit 1
T=plugins/ThirdPlanet
B=$T/build/tests
mkdir -p "$B"
SRC="$T/src/app_unit.cpp $T/src/engine_unit.cpp $T/src/power.cpp $T/src/sky.cpp"
status=0
for c in ${1:-0 1}; do
    for t in test_plugin test_controls test_equiv test_display test_screen test_sync; do
        c++ -std=c++17 -O2 -DTP_LINK=$c -I$T/src -Ideps/mpc-plugin-sdk/framework $T/tests/$t.cpp $SRC \
            -o $B/${t}_$c 2>&1 | grep -E "error" && { echo "$t (TP_LINK=$c): BUILD FAILED"; status=1; continue; }
        if $B/${t}_$c > $B/${t}_$c.log 2>&1; then
            echo "$t (TP_LINK=$c): passed"
        else
            echo "$t (TP_LINK=$c): FAILED"; grep -m 10 FAIL $B/${t}_$c.log; status=1
        fi
    done
    c++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize=shift-base -fsanitize-recover=address \
        -DTP_LINK=$c -I$T/src -Ideps/mpc-plugin-sdk/framework $T/tests/test_fuzz.cpp $SRC -o $B/test_fuzz_$c 2>&1 \
        | grep -E "error" && { echo "test_fuzz (TP_LINK=$c): BUILD FAILED"; status=1; continue; }
    # (T-1723: upstream's wd_read reads one sample past its buffer; reported, not fatal, until it is decided)
    if ASAN_OPTIONS=halt_on_error=0 $B/test_fuzz_$c > $B/test_fuzz_$c.log 2>&1 && \
        [ "$(grep -c 'runtime error\|ERROR: AddressSanitizer' $B/test_fuzz_$c.log)" = "$(grep -c "wd_buf" $B/test_fuzz_$c.log)" ]; then
        echo "test_fuzz (TP_LINK=$c): passed"
    else
        echo "test_fuzz (TP_LINK=$c): FAILED"; tail -5 $B/test_fuzz_$c.log; status=1
    fi
done
[ $status -eq 0 ] && echo "ALL TESTS PASSED" || echo "TESTS FAILED"
exit $status
