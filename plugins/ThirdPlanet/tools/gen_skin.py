#!/usr/bin/env python3
"""Generates Third Planet's MPC skin into plugins/ThirdPlanet/skins/.

    python3 plugins/ThirdPlanet/tools/gen_skin.py

The pages come from ui.json through the SDK's own generator (tools/gen_ui.py). This script then adds the
status line across the top of every page and moves the 4 x 4 grid down to make room for it, adds the PATTERN
page's steps and accents under its four knobs, and builds the LAYERS page (Mute, Solo, Nudge, Shake a layer),
second after PLAY.
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
    ("Pad", True, 663, 420, TEXT),      # (wide: it also says when another instance plays)
    ("Hold status", False, 1093, 90, ACCENT),
    ("Lock status", False, 1183, 90, ACCENT),
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


def entry(name, ctype, data, bounds, focus="No"):
    return {
        "version": 2,
        "componentData": {"version": 1, "name": name, "type": ctype, "data": data},
        "handle remapping": {"version": 1, "map": []},
        "bounds": {"version": 1, "acceptsHWFocus": focus, "showWhenDataModelInvalid": "Show",
                   "whenVisible": "Always", "boundsType": "Absolute", "bounds": bounds},
    }


def png_copy(out, *names):
    kit = os.path.join(PLUGIN, "build", "ui-kit-cyan")
    for f in names:
        shutil.copy(os.path.join(kit, f), os.path.join(out, f))


def button(name, handle, x, y, on="button_on.png", off="button_off.png", w=120, h=44):
    """A Button straight on a parameter. Nothing may lie over it: a label over a button takes its touches (seen
    on the MPC, 3.10.1), so its caption goes beside it."""
    return entry(name, "Button", {"version": 2, "onImage": on, "offImage": off, "buttonId": 1, "numButtonsInGroup": 1,
                                  "handleName": handle, "gestureBehaviour": "Instant"},
                 "%d %d %d %d" % (x, y, w, h), "Yes")


def caption(name, text, handle, bounds, colour=TEXT, height=20, centred=True):
    """A caption: a `Name` label showing `text` (WithName), since a `Label` of type Text does not render."""
    st = text_style(height, "SemiBold", colour, "Upper Case")
    if centred:
        st["justification"] = "horizontallyCentred verticallyCentred"
    return entry(name, "Label", {"version": 1, "textStyle": st, "type": "Name",
                                 "handleName": "WithName[%s]/%s" % (text, handle)}, bounds)


ONE_SHOT = re.compile(r"^(Forget|Next chord|New chords|Shake all|Reset piece|Nudge .+|Shake .+)$")


# LAYERS: a column a layer, as PERFORM's keys and the layers' buttons held: Mute and Solo (switches), Nudge and
# Shake (one-shots). Space has no mute or solo on the FM-1.
LAYERS = [("DRONE", "Drone"), ("SEQ", "Seq"), ("LEAD", "Lead"), ("SEQ 2", "Seq 2"), ("ATMOS", "Atmos"),
          ("SPACE", "Space")]
LAYER_X0, LAYER_DX, LAYER_Y0, LAYER_DY = 46, 206, 112, 116


def layers_tab(p, template):
    comps = []
    for c, (title, pname) in enumerate(LAYERS):
        x = LAYER_X0 + c * LAYER_DX
        rows = ["Mute", "Solo", "Nudge", "Shake"] if pname != "Space" else ["Nudge", "Shake"]
        first = p("%s %s" % (rows[0], pname))
        comps.append(caption("layer " + pname, title, first, "%d %d 120 30" % (x, LAYER_Y0 - 40), ACCENT, 24))
        for r, what in enumerate(rows if pname != "Space" else [None, None] + rows):
            if what is None:
                continue
            h = p("%s %s" % (what, pname))
            y = LAYER_Y0 + r * LAYER_DY
            comps.append(button("%s %s" % (what, pname), h, x, y))
            comps.append(caption("%s %s cap" % (what, pname), what, h, "%d %d 120 30" % (x, y + 50)))
    return {"version": 1, "tabName": "LAYERS", "fnKeyIndex": 0, "fnKeySubIndex": 0,
            "componentDefinition": dict(template, componentsData=comps), "qlinkBoundsData": []}


# PATTERN: below the four knobs from ui.json, the lane's 16 steps and their 16 accents (the white keys, and a black
# key held with one, on the FM-1's Pattern page).
STEP_X0, STEP_DX, STEP_W, STEP_H = 46, 74, 58, 40
STEP_ROWS = (("STEPS", "Step", 262), ("ACCENTS", "Accent", 412))


def pattern_steps(p):
    comps = []
    for title, pname, y in STEP_ROWS:
        comps.append(caption("pattern " + pname, title, p(pname + " 1"), "%d %d 400 30" % (STEP_X0, y - 40), MUTED, 20,
                             False))
        for st in range(16):
            x = STEP_X0 + st * STEP_DX
            h = p("%s %d" % (pname, st + 1))
            comps.append(button("%s %d" % (pname, st + 1), h, x, y, "seg4_on.png", "seg4_off.png", STEP_W, STEP_H))
            comps.append(caption("%s %d cap" % (pname, st + 1), str(st + 1), h, "%d %d %d 26" % (x, y + 44, STEP_W),
                                 MUTED, 18))
    return comps


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
        p = lambda n: "Parameter %d" % index_of[n]
        tabs = tui["pageData"]["tabs"]
        names = [t["tabName"] for t in tabs]
        pat = tabs[names.index("PATTERN")]["componentDefinition"]["componentsData"]
        pat[:] = [c for c in pat if not c["componentData"]["name"].startswith("status")] + pattern_steps(p) + status_line(index_of)
        layers = layers_tab(p, tabs[0]["componentDefinition"])
        layers["componentDefinition"]["componentsData"].extend(status_line(index_of))
        tabs.insert(1, layers)
        for i, t in enumerate(tabs):
            t["fnKeyIndex"] = i
        png_copy(tmp, "button_on.png", "button_off.png", "seg4_on.png", "seg4_off.png")
        ql_path = os.path.join(tmp, "Q-Links.json")
        ql = json.load(open(ql_path))
        m = ql["Screen Mode Q-Links"]["map"]
        for e in m:
            if e["Tab"] >= 2:
                e["Tab"] += 1
        m.insert(1, dict(m[0], Tab=2, **{"Q-Links": {"Q-Link %d" % (i + 1): -1 for i in range(16)}}))
        # (no one-shot on a Q-Link: a knob turned would fire it again and again as it goes back to Off)
        one_shot = {index_of[n] for n in index_of if ONE_SHOT.match(n)}
        for e in m:
            for k, v in e["Q-Links"].items():
                if v in one_shot:
                    e["Q-Links"][k] = -1
        with open(ql_path, "w") as f:
            json.dump(ql, f, indent=4)
        if os.path.isdir(OUT):
            shutil.rmtree(OUT)
        shutil.copytree(tmp, OUT)
        with open(os.path.join(OUT, "TUI.json"), "w") as f:
            json.dump(tui, f, indent=2)
            f.write("\n")
    print("gen_skin: wrote %s (%d pages)" % (OUT, len(tui["pageData"]["tabs"])))


if __name__ == "__main__":
    main()
