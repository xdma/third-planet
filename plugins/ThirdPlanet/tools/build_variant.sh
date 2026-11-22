#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Third Planet with or without Harmony Link, in one command: build, write the skin for that build's parameters
# (tools/gen_skin.py), build again so the folder carries that skin.
#   off: the default build; regenerates the committed skins/ (the link baseline's skin, byte for byte).
#   on:  the build with Harmony Link. Its skin goes only into build/: the committed skins/ is put back as it was,
#        so the working tree stays clean.
#   plugins/ThirdPlanet/tools/build_variant.sh on|off   (from anywhere)
set -eu
case "${1:-}" in
    on) define=(--define TP_LINK=1) ;;
    off) define=() ;;
    *) echo "usage: build_variant.sh on|off" >&2; exit 2 ;;
esac
cd "$(dirname "$0")/../../.."
P=plugins/ThirdPlanet
build() { python3 deps/mpc-plugin-sdk/tools/build.py $P ${define[@]+"${define[@]}"} "$@"; }
if [ "$1" = on ]; then
    keep=$(mktemp -d)
    cp -R $P/skins "$keep/skins"
    trap 'rm -rf $P/skins && cp -R "$keep/skins" $P/skins && rm -rf "$keep"' EXIT
fi
build --no-validate > /dev/null
python3 $P/tools/gen_skin.py
build
