#!/usr/bin/env python3
"""Generates Third Planet's MPC skin into plugins/ThirdPlanet/skins/.

    python3 plugins/ThirdPlanet/tools/gen_skin.py

The eight pages come from ui.json through the SDK's own generator (tools/gen_ui.py). This script then
adds the status line across the top of every page and moves the 4 x 4 grid down to make room for it.
The status line is live text: `Label` widgets of type Value bound to the plugin's display parameters
(src/ThirdPlanet.cpp, kDispPage..kDispHold) and to Piece, with `Name` labels as captions. (A `Label` of
type Text did not render on the MPC in Kolo's device check, so there is no fixed text.)

With skins/ present the SDK build copies it and no longer runs gen_ui.py, so rerun this script after a
change to ui.json or to the parameters, and commit skins/. It needs build/params.json, which every
build writes. Standard library only.
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PLUGIN = os.path.normpath(os.path.join(HERE, ".."))
ROOT = os.path.normpath(os.path.join(PLUGIN, "..", ".."))
GEN_UI = os.path.join(ROOT, "deps", "mpc-plugin-sdk", "tools", "gen_ui.py")
PARAMS = os.path.join(PLUGIN, "build", "params.json")
OUT = os.path.join(PLUGIN, "skins")

# Colours (AARRGGBB): the cyan kit's accent, white values, the generator's grey captions.
ACCENT, TEXT, MUTED = "ff19c6e0", "fff1efe8", "ff8a8a8c"

# The grid: gen_ui puts its rows at these tops; they move down under the status line. The Q-Link
# columns start below the line too, and their 38 px "Q" badge (drawn in the top-left corner of the
# focused column) stays clear of the first row.
OLD_ROWS = (45, 196, 346, 497)
NEW_ROWS = (104, 234, 364, 494)
QLINK_Y, QLINK_H = 64, 560
SLOT_H = 107   # gen_ui's slot height

# The status line, by parameter name (indices come from params.json).
CAPTION_Y, CAPTION_H, VALUE_Y, VALUE_H = 2, 24, 24, 38
STATUS = [
    # (parameter, caption?, bounds x, width, colour)
    ("Piece", True, 23, 300, TEXT),
    ("Chord", True, 343, 300, ACCENT),
    ("Page", True, 663, 600, TEXT),     # (wide: it also says when another instance plays)
    ("Perform", False, 983, 170, ACCENT),
    ("Hold", False, 1160, 100, ACCENT),
]


def text_style(height, style, colour, case="Original"):
    return {"version": 1, "font": {"version": 1, "name": "Titillium Web", "style": style, "height": float(height)},
            "colour": colour, "justification": "left verticallyCentred", "case": case}


def label(name, kind, index, bounds, style):
    return {
        "version": 2,
        "componentData": {"version": 1, "name": name, "type": "Label",
                          "data": {"version": 1, "textStyle": style, "type": kind,
                                   "handleName": "Parameter %d" % index}},
        "handle remapping": {"version": 1, "map": []},
        "bounds": {"version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show",
                   "whenVisible": "Always", "boundsType": "Absolute", "bounds": bounds},
    }


def status_line(index_of):
    out = []
    for name, caption, x, w, colour in STATUS:
        i = index_of[name]
        if caption:
            out.append(label("status caption " + name, "Name", i, "%d %d %d %d" % (x, CAPTION_Y, w, CAPTION_H),
                             text_style(18, "Regular", MUTED, "Upper Case")))
        out.append(label("status " + name, "Value", i, "%d %d %d %d" % (x, VALUE_Y, w, VALUE_H),
                         text_style(32, "Bold", colour)))
    return out


def move_row(bounds):
    """A control, or a part of one (a switch's segments sit inside its slot), moves with its row."""
    x, y, w, h = (int(v) for v in bounds.split())
    for old, new in zip(OLD_ROWS, NEW_ROWS):
        if old <= y and y + h <= old + SLOT_H:
            return "%d %d %d %d" % (x, y + new - old, w, h)
    sys.exit("gen_skin: a control at y %d (h %d) is in no row of gen_ui's grid %s; update OLD_ROWS" % (y, h, OLD_ROWS))


def main():
    if not os.path.isfile(PARAMS):
        sys.exit("gen_skin: build the plugin first (no %s)" % PARAMS)
    params = json.load(open(PARAMS))
    plist = params["params"]
    index_of = {p["name"]: p["index"] for p in plist}
    for name, *_ in STATUS:
        if name not in index_of:
            sys.exit("gen_skin: no parameter named %r in params.json" % name)

    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([sys.executable, GEN_UI, "--params", PARAMS, "--ui", os.path.join(PLUGIN, "ui.json"),
                        "--out", tmp, "--kit-cache", os.path.join(PLUGIN, "build"), "--quiet"], check=True)
        tui = json.load(open(os.path.join(tmp, "TUI.json")))
        for tab in tui["pageData"]["tabs"]:
            comps = tab["componentDefinition"]["componentsData"]
            for c in comps:
                c["bounds"]["bounds"] = move_row(c["bounds"]["bounds"])
            comps.extend(status_line(index_of))
            tab["qlinkBoundsData"] = [re.sub(r"^(\d+) \d+ (\d+) \d+$", r"\1 %d \2 %d" % (QLINK_Y, QLINK_H), b)
                                      for b in tab["qlinkBoundsData"]]
        if os.path.isdir(OUT):
            shutil.rmtree(OUT)
        shutil.copytree(tmp, OUT)
        with open(os.path.join(OUT, "TUI.json"), "w") as f:
            json.dump(tui, f, indent=2)
            f.write("\n")
    print("gen_skin: wrote %s (%d pages)" % (OUT, len(tui["pageData"]["tabs"])))


if __name__ == "__main__":
    main()
