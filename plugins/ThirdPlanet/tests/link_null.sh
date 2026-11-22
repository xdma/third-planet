#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Harmony Link's default-path proof: with Link Off (the default), Third Planet is the link baseline exactly.
#
# tests/link_null.cpp (this tree's file, unchanged) plays one deterministic 90 s script into the plugin through its
# VST entry and records the audio, the saved project, the host notifications and the final parameter values, over
# the base's parameters (Link, inserted after Sync, is left out by its id). It is built three ways:
#   (a) the link baseline at d31f2a8 (git archive): tp-screen, the Sync start on the bar line (T-1745) and Sync
#       across a loop jump (T-1757);
#   (b) this tree with TP_LINK=0 (compiled out);
#   (c) this tree with TP_LINK=1 (compiled in, Link Off).
# The base already has T-1745, so there is no exception for plays mid-bar (some of the script's plays are):
#   - (a) = (b) byte for byte: audio, saved project, notifications and final values;
#   - (a) = (c) byte for byte on audio, saved project and final values, and on the display values' notifications.
#     The controls' notifications carry the same values in the same order for every control, but not always in the
#     same block: the plugin's mirror walk (ThirdPlanet.cpp mirror()) steps through the controls by a growing stride
#     from where it last sent, so one more control (Link, never mirrored) shifts which control's change goes out in
#     which block. The script reports the largest shift, in blocks;
#   - (a) run twice gives the same bytes (the harness is deterministic);
#   - no build notifies the host about a parameter outside the base's (with Link Off, Link stays silent);
#   - the real plugin built three ways (SDK build.py, native): params.json (b) = (a), and (c) = (a) plus exactly the
#     parameter "Link", right after "Sync", every other parameter identical (its index one higher after Link);
#   - gen_skin.py run on (b)'s params.json, in a copy of the project, writes the base's skins/ byte for byte.
# (No self-test that the proof can see Link on: Link does nothing yet. The later tasks add one.)
# The plugin's own build/ is left with the default build (Link compiled out, = (b)), rebuilt last; the committed skins/ is
# never touched.
#   plugins/ThirdPlanet/tests/link_null.sh   (from the repository root; about 15 s)
set -u
cd "$(dirname "$0")/../../.." || exit 1
ROOT=$PWD
T=plugins/ThirdPlanet
B=$T/build/lnull
BASE_REV=d31f2a8
FW=deps/mpc-plugin-sdk/framework
SDK_BUILD=deps/mpc-plugin-sdk/tools/build.py
BT=$B/base/$T   # the base project, extracted
SECONDS=0
status=0
fail() { echo "FAIL: $*"; status=1; }

rm -rf "$B" && mkdir -p "$B/base" || exit 1
git archive "$BASE_REV" "$T" | tar -x -C "$B/base" || { echo "cannot extract $BASE_REV"; exit 1; }

# ---- the harness, three ways ----
harness() {   # name src [defines...]
    local name=$1 src=$2 link=
    shift 2
    [ -f "$src/link_codec.cpp" ] && link="$src/link_codec.cpp $src/link_capture.cpp"   # (not in the base; #if TP_LINK inside)
    [ -f "$src/link_worker.cpp" ] && link="$link $src/link_alsa.cpp $src/link_worker.cpp"
    c++ -std=c++17 -O2 "$@" -I"$src" -I"$FW" -o "$B/$name" "$T/tests/link_null.cpp" \
        "$src/app_unit.cpp" "$src/engine_unit.cpp" "$src/power.cpp" "$src/sky.cpp" $link
}
harness a "$BT/src" & pa=$!
harness b "$T/src" -DTP_LINK=0 & pb=$!
harness c "$T/src" -DTP_LINK=1 & pc=$!
wait $pa || { echo "the base harness (a) does not build"; exit 1; }
wait $pb || { echo "the harness with TP_LINK=0 (b) does not build"; exit 1; }
wait $pc || { echo "the harness with TP_LINK=1 (c) does not build"; exit 1; }

run() {   # name out
    "$B/$1" "$B/$2.bin" > "$B/$2.txt" 2> "$B/$2.err" || { cat "$B/$2.err"; echo "harness $1 failed"; exit 1; }
}
run a a; run a a2; run b b; run c c
echo "(a) $(grep '^facts' "$B/a.txt" | cut -d' ' -f2-)"
grep -q '^facts .* params=\([0-9]*\) base_params=\1 ' "$B/c.txt" && fail "(c) has no Link parameter: TP_LINK=1 did not compile it in"
grep -q '^midbar frame=-1 ' "$B/a.txt" && fail "the script has no play mid-bar: T-1745 is not exercised"

# the first difference between two outputs, by section (the sizes come from the harness's own report)
explain() {   # ref other label
    python3 -I - "$B/$1" "$B/$2" "$3" <<'EOF'
import struct, sys
ra, rb, label = sys.argv[1], sys.argv[2], sys.argv[3]
def sections(stem):
    for line in open(stem + ".txt"):
        if line.startswith("sections "):
            return {k: int(v) for k, v in (w.split("=") for w in line.split()[1:])}
A, Bs = sections(ra), sections(rb)
a, b = open(ra + ".bin", "rb").read(), open(rb + ".bin", "rb").read()
blocks = list(struct.unpack("<%dI" % (len(open(ra + ".bin.blocks", "rb").read()) // 4), open(ra + ".bin.blocks", "rb").read()))
for k in ("audio", "state", "notify", "values"):
    if A[k] != Bs[k]:
        print("  %s: the %s section is %d bytes in the reference, %d here" % (label, k, A[k], Bs[k]))
i = next((j for j in range(min(len(a), len(b))) if a[j] != b[j]), None)
if i is None:
    print("  %s: identical up to the shorter file (%d vs %d bytes)" % (label, len(a), len(b)))
    sys.exit(0)
bounds = [("audio", 0, A["audio"]), ("state", A["audio"], A["audio"] + A["state"]),
          ("notifications", A["audio"] + A["state"], A["audio"] + A["state"] + A["notify"]),
          ("values", A["audio"] + A["state"] + A["notify"], len(a))]
sec, lo = next((n, s) for n, s, e in bounds if s <= i < e)
off = i - lo
msg = "  %s: first difference at byte %d, in %s (offset %d)" % (label, i, sec, off)
if sec == "audio":
    frame, ch = off // 8, "LR"[(off // 4) % 2]
    blk, acc = 0, 0
    while blk < len(blocks) and acc + blocks[blk] <= frame:
        acc += blocks[blk]; blk += 1
    va = struct.unpack_from("<f", a, lo + off // 4 * 4)[0]; vb = struct.unpack_from("<f", b, lo + off // 4 * 4)[0]
    msg += ": frame %d (%.3f s), channel %s, block %d; %r vs %r" % (frame, frame / 44100.0, ch, blk, va, vb)
elif sec == "state":
    which = "the mid-run save" if off < A["state_mid"] else "the final save"
    o = off if off < A["state_mid"] else off - A["state_mid"]
    base = lo if off < A["state_mid"] else lo + A["state_mid"]
    if o < 12:
        msg += ": %s, its header" % which
    else:
        p, k = base + 12, 0
        n = struct.unpack_from("<I", a, base + 8)[0]
        while k < n and p + 1 + a[p] + 8 <= base + o:
            p += 1 + a[p] + 8; k += 1
        if k < n:
            msg += ": %s, parameter %d (%s)" % (which, k, a[p + 1:p + 1 + a[p]].decode())
        else:
            msg += ": %s, the extra (TP1) blob at byte %d of it" % (which, base + o - p - 4)
elif sec == "notifications":
    r = off // 12
    da = struct.unpack_from("<iif", a, lo + r * 12) if lo + r * 12 + 12 <= len(a) else None
    db = struct.unpack_from("<iif", b, lo + r * 12) if lo + r * 12 + 12 <= len(b) else None
    msg += ": notification %d, (block, index, value) %r vs %r" % (r, da, db)
else:
    k = off // 4
    msg += ": getParameter(%d) %r vs %r" % (k, struct.unpack_from("<f", a, lo + k * 4)[0],
                                            struct.unpack_from("<f", b, lo + k * 4)[0] if lo + k * 4 + 4 <= len(b) else None)
print(msg)
EOF
}
same() {   # ref other label
    if cmp -s "$B/$1.bin" "$B/$2.bin"; then echo "$3: identical ($(wc -c < "$B/$1.bin" | tr -d ' ') bytes)"
    else fail "$3: DIFFER"; cmp "$B/$1.bin" "$B/$2.bin" | head -1; explain "$1" "$2" "$3"; fi
}
same a a2 "(a) twice"
same a b "(a) base vs (b) TP_LINK=0"
# (a) vs (c): audio, saved project and final values byte for byte; notifications as described at the top
python3 -I - "$B/a" "$B/c" <<'PYEOF' || status=1
import struct, sys
from collections import defaultdict
ra, rc = sys.argv[1], sys.argv[2]
def load(stem):
    txt = open(stem + ".txt").read().splitlines()
    S = next({k: int(v) for k, v in (w.split("=") for w in l.split()[1:])} for l in txt if l.startswith("sections "))
    nc = next(int(l.split()[1]) for l in txt if l.startswith("base_controls "))
    d = open(stem + ".bin", "rb").read()
    a, st = S["audio"], S["audio"] + S["state"]
    nt = st + S["notify"]
    notes = [struct.unpack_from("<iif", d, st + 12 * r) for r in range(S["notify"] // 12)]
    return nc, d[:a], d[a:st], notes, d[nt:]
NA, aa, sa, na, va = load(ra)
NC, ac, sc, nc, vc = load(rc)
ok = True
for label, x, y in (("controls (base count)", NA, NC), ("audio", aa, ac), ("saved project", sa, sc), ("final values", va, vc)):
    if x != y:
        print("FAIL: (a) base vs (c) TP_LINK=1, Link Off: the %s differ" % label); ok = False
if ok: print("(a) base vs (c) TP_LINK=1, Link Off: audio, saved project, final values identical (%d bytes)" % (len(aa) + len(sa) + len(va)))
da, dc = [n for n in na if n[1] >= NA], [n for n in nc if n[1] >= NA]
if da != dc:
    k = next((j for j in range(min(len(da), len(dc))) if da[j] != dc[j]), min(len(da), len(dc)))
    print("FAIL: (a) vs (c): display notification %d differs: %r vs %r" % (k, da[k] if k < len(da) else None, dc[k] if k < len(dc) else None)); ok = False
else: print("(a) vs (c): display notifications identical (%d)" % len(da))
def per(l):
    m = defaultdict(list)
    for b, i, v in l:
        if i < NA: m[i].append((b, v))
    return m
pa, pc = per(na), per(nc)
bad = [i for i in sorted(set(pa) | set(pc)) if [v for _, v in pa.get(i, [])] != [v for _, v in pc.get(i, [])]]
if bad:
    print("FAIL: (a) vs (c): control %d's notified values differ: %r vs %r" % (bad[0], pa.get(bad[0]), pc.get(bad[0]))); ok = False
else:
    n = sum(len(v) for v in pa.values())
    lag = max([abs(x[0] - y[0]) for i in pa for x, y in zip(pa[i], pc[i])] or [0])
    print("(a) vs (c): control notifications: the same values in the same order for every control (%d), "
          "up to %d blocks apart (the mirror walk's order, see the top)" % (n, lag))
sys.exit(0 if ok else 1)
PYEOF
for r in a b c; do
    f=$(sed -n 's/^filtered_notifications=//p' "$B/$r.err")
    [ "$f" = 0 ] || fail "($r) notified the host $f times about Link or parameters past the base's"
done

# ---- params.json, the real plugin three ways ----
build() {   # project out.json [build.py args...]
    local proj=$1 out=$2
    shift 2
    python3 "$SDK_BUILD" "$proj" --target native "$@" > "$out.log" 2>&1 && cp "$proj/build/params.json" "$out" \
        || { fail "build.py $proj $*: see $out.log"; tail -5 "$out.log"; }
}
build "$BT" "$B/params_a.json"
build "$T" "$B/params_b.json" --define TP_LINK=0

# ---- skins from (b): gen_skin.py in a copy of the project (it writes beside its own tools/) ----
S=$B/skin/$T
mkdir -p "$S/build" "$B/skin/deps"
cp -R "$T/tools" "$T/ui.json" "$T/plugin.json" "$S/"
ln -s "$ROOT/deps/mpc-plugin-sdk" "$B/skin/deps/mpc-plugin-sdk"
if [ -f "$B/params_b.json" ]; then
    cp "$B/params_b.json" "$S/build/params.json"
    if python3 "$S/tools/gen_skin.py" > "$B/gen_skin.log" 2>&1; then
        if diff -r "$BT/skins" "$S/skins" > "$B/skins.diff"; then echo "skins: gen_skin.py on (b) = the base's skins/"
        else fail "skins: gen_skin.py on (b) differs from the base's skins/:"; head -20 "$B/skins.diff"; fi
    else fail "gen_skin.py on (b) failed: see $B/gen_skin.log"; tail -5 "$B/gen_skin.log"; fi
fi

build "$T" "$B/params_c.json" --define TP_LINK=1
build "$T" "$B/params_d.json"   # (last: this tree's build/ is left with the default build, Link compiled out)
if [ -f "$B/params_b.json" ] && [ -f "$B/params_d.json" ]; then
    cmp -s "$B/params_b.json" "$B/params_d.json" && echo "params.json: the default build = (b), TP_LINK=0" \
        || fail "params.json: the default build differs from (b), TP_LINK=0"
fi

if [ -f "$B/params_a.json" ] && [ -f "$B/params_b.json" ] && [ -f "$B/params_c.json" ]; then
    python3 -I - "$B/params_a.json" "$B/params_b.json" "$B/params_c.json" <<'EOF' || status=1
import json, sys
a, b, c = (json.load(open(p)) for p in sys.argv[1:4])
ok = True
def diff(x, y, label):
    """x, y: params.json dicts; the first difference, or None."""
    for k in sorted(set(x) | set(y)):
        if k != "params" and x.get(k) != y.get(k):
            return "%r is %r in (a), %r here" % (k, x.get(k), y.get(k))
    px, py = x["params"], y["params"]
    if len(px) != len(py):
        return "%d parameters in (a), %d here" % (len(px), len(py))
    for i, (p, q) in enumerate(zip(px, py)):
        if p != q:
            fields = [f for f in sorted(set(p) | set(q)) if p.get(f) != q.get(f)]
            return "parameter %d (%s) differs in %s: (a) %s, here %s" % (
                i, p.get("name"), ", ".join(fields), {f: p.get(f) for f in fields}, {f: q.get(f) for f in fields})
    return None
d = diff(a, b, "(b)")
if d: print("FAIL: params.json (b) vs (a): " + d); ok = False
else: print("params.json (b) = (a): %d parameters" % len(a["params"]))
names = [p.get("name") for p in c["params"]]
if names.count("Link") != 1:
    print("FAIL: params.json (c) has %d parameters named Link, not exactly 1 (%d parameters, (a) %d)" % (
        names.count("Link"), len(names), len(a["params"]))); ok = False
else:
    k = names.index("Link")
    if k == 0 or names[k - 1] != "Sync":
        print("FAIL: params.json (c): Link is parameter %d, after %r, not right after Sync" % (k, names[k - 1] if k else None)); ok = False
    if c["params"][k].get("index") != k:
        print("FAIL: params.json (c): Link's index is %r, not %d" % (c["params"][k].get("index"), k)); ok = False
    rest = [dict(p, index=p["index"] - 1) if i > k and isinstance(p.get("index"), int) else p
            for i, p in enumerate(c["params"]) if i != k]
    d = diff(a, dict(c, params=rest), "(c)")
    if d: print("FAIL: params.json (c) without Link vs (a): " + d); ok = False
    elif ok:
        print("params.json (c) = (a) + Link at %d (after Sync): %r" % (k, {f: v for f, v in c["params"][k].items() if f != "index"}))
sys.exit(0 if ok else 1)
EOF
fi

echo "($SECONDS s)"
if [ $status -eq 0 ]; then echo "LINK NULL PASSED"; else echo "LINK NULL FAILED"; fi
exit $status
