#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Third Planet with or without Harmony Link, in one command: build, write the skin for that build's parameters
# (tools/gen_skin.py), build again so the folder carries that skin. The committed skins/ is the build without Link
# (the default; the link baseline's skin, byte for byte); "on" rewrites skins/ for the build with it, "off" writes it back.
#   plugins/ThirdPlanet/tools/build_variant.sh on|off   (from anywhere)
set -eu
case "${1:-}" in
    on) define=(--define TP_LINK=1) ;;
    off) define=() ;;
    *) echo "usage: build_variant.sh on|off" >&2; exit 2 ;;
esac
cd "$(dirname "$0")/../../.."
build() { python3 deps/mpc-plugin-sdk/tools/build.py plugins/ThirdPlanet ${define[@]+"${define[@]}"} "$@"; }
build --no-validate > /dev/null
python3 plugins/ThirdPlanet/tools/gen_skin.py
build
