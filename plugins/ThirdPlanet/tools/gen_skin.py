#!/usr/bin/env python3
"""Generates Third Planet's MPC skin into plugins/ThirdPlanet/skins/.

    python3 plugins/ThirdPlanet/tools/gen_skin.py

The pages come from ui.json through the SDK's own generator (tools/gen_ui.py). This script then adds the
status line across the top of every page and moves the 4 x 4 grid down to make room for it, adds the PATTERN
page's steps and accents under its four knobs, builds the LAYERS page (Mute, Solo, Nudge, Shake a layer),
and builds SKY, the star-chart page (spec 8.1), as the first tab: SKY, PLAY, LAYERS, then ui.json's pages.
SKY's art comes from gen_art.py and its geometry from layout.py; its Q-Links are MAIN's 16 controls.
The status line is live text: `Label` widgets of type Value bound to the plugin's display parameters
(src/ThirdPlanet.cpp, kDispPad on) and to Piece, with `Name` labels as captions. (A `Label` of
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
sys.path.insert(0, HERE)
import gen_art  # noqa: E402
import layout as L  # noqa: E402

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


# ---- Parameters by id ----
# params.json carries no ids (host_sim writes "id": null), and names are not unique ("Next chord" is both the
# PLAY one-shot and d_next), so the display parameters are found by their fixed order from d_pad on
# (src/ThirdPlanet.cpp, enum Disp) and each one's name is checked. Controls are looked up by name below d_pad.
DISPLAY = ([("d_pad", "Pad"), ("d_chord", "Chord"), ("d_hold", "Hold status"), ("d_lock", "Lock status"),
            ("d_section", "Section"), ("d_phrase", "Phrase bar"), ("d_evolve", "Evolve chance"),
            ("d_next", "Next chord"), ("d_next_in", "Next chord in"), ("d_prog", "Progression"),
            ("d_ring", "Chord ring")]
           + [("d_slot%d" % i, "Slot %d" % i) for i in range(1, 9)]
           + [("d_slotname%d" % i, "Slot chord %d" % i) for i in range(1, 9)]
           + [("d_px%d" % i, "Particle %d X" % i) for i in range(1, 9)]
           + [("d_py%d" % i, "Particle %d Y" % i) for i in range(1, 9)]
           + [("d_planet%d" % i, "Planet %d" % i) for i in range(1, 6)]
           + [("d_log%d" % i, "Log line %d" % i) for i in range(1, 4)]
           + [("d_drift%d" % i, "Filter drift %d" % i) for i in range(1, 6)]
           + [("d_density", "Density drift"), ("d_step", "Step playing"), ("d_ext", "Clock"),
              ("d_source", "Harmony source")])


def parameter_maps(plist):
    """(controls by name, display parameters by id), both as indices."""
    by_index = {p["index"]: p["name"] for p in plist}
    pad = min(p["index"] for p in plist if p["name"] == DISPLAY[0][1])
    controls = {}
    for p in plist:
        if p["index"] < pad:
            if p["name"] in controls:
                sys.exit("gen_skin: two controls are named %r" % p["name"])
            controls[p["name"]] = p["index"]
    display = {}
    for k, (pid, name) in enumerate(DISPLAY):
        if by_index.get(pad + k) != name:
            sys.exit("gen_skin: parameter %d should be %s (%r) but is %r; update DISPLAY"
                     % (pad + k, pid, name, by_index.get(pad + k)))
        display[pid] = pad + k
    return controls, display


# ---- SKY (spec 8.1) ----

CREAM, DIM, RED = L.argb(L.CREAM), L.argb(L.DIM), L.argb(L.ACCENT)
CENTRED = "horizontallyCentred verticallyCentred"

# The bottom strip: MAIN's 16 controls in 4 groups of 4, group g on the g-th knob row from the top (Q-Links 13-16,
# 9-12, 5-8, 1-4), the same knobs as on the old MAIN page. (strip name, parameter, overlay)
SKY_STRIP = [
    ("FILTER", "Filter", "knob overlay bipolar"), ("EVOLVE", "Evolve", "knob overlay"),
    ("MOTION", "Motion", "knob overlay"), ("DRIFT", "Drift", "menu overlay"),
    ("ENERGY", "Energy", "knob overlay bipolar"), ("WET", "Wet", "knob overlay bipolar"),
    ("SWEEP", "Sweep", "knob overlay"), ("VOLUME", "Volume", "knob overlay"),
    ("DRONE", "Drone", "knob overlay"), ("SEQ", "Sequence", "knob overlay"),
    ("LEAD", "Lead", "knob overlay"), ("SEQ 2", "Sequence 2", "knob overlay"),
    ("PIECE", "Piece", "menu overlay"), ("TEMPO", "Tempo", "knob overlay"),
    ("ROOT", "Root", "menu overlay"), ("MODE", "Mode", "menu overlay"),
]
SKY_OVERLAYS = ("knob overlay", "knob overlay bipolar", "menu overlay")
SKY_CELL = {"knob overlay": "sky knob", "knob overlay bipolar": "sky knob bipolar", "menu overlay": "sky menu"}


def sky_qlink(i):
    """The Q-Link (1..16) of strip cell i: group g on knob row g from the top."""
    g, k = divmod(i, 4)
    return 13 - 4 * g + k


def sky_qlink_bounds():
    """One rectangle per group, in bank order (RowTopDown: bank 1 is Q-Links 13-16, the first group)."""
    m = 4
    return [L.rect_str((x - m, y - m, w + 2 * m, h + 2 * m)) for x, y, w, h in L.STRIP_GROUPS]


def sky_cell_rect(i):
    """A strip cell's touch area: its name and its value."""
    x, y, w, _ = L.STRIP_NAMES[i]
    vx, vy, vw, vh = L.STRIP_VALUES[i]
    return (x, y, w, vy + vh - y)


def style(height, weight, colour, just="left verticallyCentred", case="Original"):
    st = text_style(height, weight, colour, case)
    st["justification"] = just
    return st


def text(name, kind, handle, rect, st):
    """A Label: kind "Value" shows the handle's display text, "Name" its name (WithName[...] for fixed text)."""
    return entry(name, "Label", {"version": 1, "textStyle": st, "type": kind, "handleName": handle}, L.rect_str(rect))


def image(name, file, rect, frames=None, handle=None):
    """A Regular image at its native size, or a film strip whose frame follows handle."""
    if frames is None:
        data = {"version": 2, "imageType": "Regular", "colour": "0", "image": file}
    else:
        data = {"version": 2, "imageType": "FilmStrip", "colour": "0", "image": file, "numFrames": frames,
                "handleName": handle}
    return entry(name, "Image", data, L.rect_str(rect))


def sky_cell_definitions():
    """The strip's value cell (Kolo's value label): a tap focuses its Q-Link bank, a double tap or Enter opens the
    overlay, the data wheel changes it. The name label lies under it, outside the component."""
    defs = []
    w, h = L.CELL_W, L.STRIP_VALUE_Y + L.STRIP_VALUE_H - L.STRIP_NAME_Y
    vy = L.STRIP_VALUE_Y - L.STRIP_NAME_Y
    for overlay in SKY_OVERLAYS:
        actions = [{"version": 1, "onAction": on, "handler": handler, "handleName": "Data", "additionalData": extra}
                   for on, handler, extra in (("Mouse Down", "Q-Link", ""), ("Double Click", "Show Overlay", overlay),
                                              ("Enter Pressed", "Show Overlay", overlay))]
        focus = entry("focus", "Focus", {"version": 1, "backgroundColour": "14" + CREAM[2:], "outlineColour": RED,
                                         "backgroundInset": 2.0, "outlineThickness": 1.0}, "0 0 %d %d" % (w, h))
        focus["bounds"]["whenVisible"] = "WhenFocussed"
        value = entry("value", "Label", {"version": 1, "textStyle": style(22, "SemiBold", CREAM, CENTRED),
                                         "type": "Value", "handleName": "Data"},
                      "0 %d %d %d" % (vy, w, L.STRIP_VALUE_H))
        clear = {"version": 1, "colour": "00000000", "image": ""}
        defs.append({"key": SKY_CELL[overlay], "value": {
            "version": 2, "actions": actions, "backgroundData": {"version": 1, "focussed": clear, "unfocussed": clear},
            "ignoreMousePresses": False, "disableCoarseDataWheel": False, "componentsData": [focus, value]}})
    return defs


def sky_tab(c, d):
    """SKY, bottom of the z-order first. c: control indices by name; d: display parameter indices by id."""
    P = lambda i: "Parameter %d" % i
    comps = [image("background", "bg_sky.png", (0, 0, L.SCREEN_W, L.SCREEN_H))]
    # The particles (R12): pucks 1-4 are the Sequence on the inner orbit, 5-8 Sequence 2 on the outer. Parked
    # pucks sit at (0.5, 0.5), behind the disc.
    for i in range(1, 9):
        pad = L.ORBIT_INNER if i <= 4 else L.ORBIT_OUTER
        comps.append(entry("puck %d" % i, "XYPad", {"version": 1, "xPositionHandle": P(d["d_px%d" % i]),
                                                     "yPositionHandle": P(d["d_py%d" % i]), "image": "puck.png"},
                           L.rect_str(pad)))
    comps.append(image("disc", "disc.png", L.DISC))
    comps.append(image("ring", "ring.png", L.RING, L.ASSETS["ring.png"][2], P(d["d_ring"])))
    comps.append(text("chord", "Value", P(d["d_chord"]), L.CHORD_TEXT, style(36, "Bold", CREAM, CENTRED)))
    # The constellation.
    for i in range(8):
        comps.append(image("slot %d" % (i + 1), "star.png", L.SLOT_STARS[i], L.ASSETS["star.png"][2],
                           P(d["d_slot%d" % (i + 1)])))
        comps.append(text("slot chord %d" % (i + 1), "Value", P(d["d_slotname%d" % (i + 1)]), L.SLOT_LABELS[i],
                          style(18, "SemiBold", CREAM, CENTRED)))
    # The left column (its captions ARC, PHRASE, NEXT, LOG are in bg_sky).
    comps += [
        text("section", "Value", P(d["d_section"]), L.ARC_VALUE, style(30, "Bold", CREAM)),
        image("phrase", "phrase.png", L.PHRASE_STRIP, L.ASSETS["phrase.png"][2], P(d["d_phrase"])),
        text("evolve", "Value", P(d["d_evolve"]), L.EVOLVE_TEXT, style(20, "Regular", CREAM)),
        text("next chord", "Value", P(d["d_next"]), L.NEXT_CHORD, style(32, "Bold", CREAM)),
        text("next in", "Value", P(d["d_next_in"]), L.NEXT_IN, style(20, "Regular", CREAM)),
        text("progression", "Value", P(d["d_prog"]), L.NEXT_PROG, style(20, "Regular", DIM)),
    ]
    for i in range(3):
        comps.append(text("log %d" % (i + 1), "Value", P(d["d_log%d" % (i + 1)]), L.LOG_LINES[i],
                          style(20, "Regular", CREAM)))
    # The right column: the five planets and their names.
    for i, name in enumerate(L.PLANET_NAMES):
        h = P(d["d_planet%d" % (i + 1)])
        comps.append(image("planet %d" % (i + 1), "planet.png", L.PLANETS[i], L.ASSETS["planet.png"][2], h))
        comps.append(text("planet %d name" % (i + 1), "Name", "WithName[%s]/%s" % (name, h), L.PLANET_LABELS[i],
                          style(18, "SemiBold", DIM)))
    # The bottom strip: each name under its cell (a label drawn over a control takes its touches).
    for i, (title, pname, overlay) in enumerate(SKY_STRIP):
        comps.append(text("strip %s name" % title.lower(), "Name", "WithName[%s]/%s" % (title, P(c[pname])),
                          L.STRIP_NAMES[i], style(15, "SemiBold", DIM, CENTRED)))
    for i, (title, pname, overlay) in enumerate(SKY_STRIP):
        cell = entry("strip " + title.lower(), SKY_CELL[overlay], {"version": 1, "handleName": "Data"},
                     L.rect_str(sky_cell_rect(i)), "Yes")
        cell["handle remapping"]["map"] = [{"key": "Data", "value": P(c[pname])}]
        cell["bounds"]["showWhenDataModelInvalid"] = "Hide"
        comps.append(cell)
    # The header.
    hy, hh = 8, 40
    comps += [
        text("title shostak", "Name", "WithName[SHOSTAK]/%s" % P(c["Piece"]), (24, hy, 110, hh),
             style(24, "Bold", RED)),
        text("title third planet", "Name", "WithName[· THIRD PLANET]/%s" % P(c["Piece"]), (132, hy, 220, hh),
             style(24, "Regular", CREAM)),
        text("piece", "Value", P(c["Piece"]), (372, hy, 210, hh), style(26, "SemiBold", CREAM)),
        text("tempo", "Value", P(c["Tempo"]), (590, hy, 60, hh), style(26, "SemiBold", CREAM, "right verticallyCentred")),
        text("clock", "Value", P(d["d_ext"]), (658, hy, 60, hh), style(26, "SemiBold", CREAM)),
        text("root", "Value", P(c["Root"]), (726, hy, 50, hh), style(26, "SemiBold", CREAM, "right verticallyCentred")),
        text("mode", "Value", P(c["Mode"]), (784, hy, 140, hh), style(26, "SemiBold", CREAM)),
        text("hold", "Value", P(d["d_hold"]), (936, hy, 72, hh), style(22, "Bold", RED)),
        text("lock", "Value", P(d["d_lock"]), (1010, hy, 72, hh), style(22, "Bold", RED)),
        text("source", "Value", P(d["d_source"]), (1090, hy, 166, hh), style(20, "SemiBold", DIM, "right verticallyCentred")),
    ]
    # Last, the cover: a blank Name label over the orbits, so a finger moves no puck (spike run 4).
    comps.append(text("cover", "Name", "WithName[ ]/%s" % P(d["d_source"]), L.COVER,
                      style(10, "Regular", "00000000")))
    clear = {"version": 1, "colour": L.argb(L.BG), "image": ""}
    return {"version": 1, "tabName": "SKY", "fnKeyIndex": 0, "fnKeySubIndex": 0,
            "componentDefinition": {"version": 2, "actions": [],
                                    "backgroundData": {"version": 1, "focussed": clear, "unfocussed": clear},
                                    "ignoreMousePresses": False, "disableCoarseDataWheel": False,
                                    "componentsData": comps},
            "qlinkBoundsData": sky_qlink_bounds()}


def sky_qlinks(c):
    m = {"Q-Link %d" % n: -1 for n in range(1, 17)}
    for i, (_, pname, _) in enumerate(SKY_STRIP):
        m["Q-Link %d" % sky_qlink(i)] = c[pname]
    return {"Tab": 1, "SubTab": 1, "Bank Direction": "RowTopDown", "Q-Links": m}


def main():
    if not os.path.isfile(PARAMS):
        sys.exit("gen_skin: build the plugin first (no %s)" % PARAMS)
    params = json.load(open(PARAMS))
    plist = params["params"]
    index_of, disp = parameter_maps(plist)
    index_of.update({name: disp[pid] for pid, name in DISPLAY[:4]})   # the status line's own four
    for name, *_ in STATUS:
        if name not in index_of:
            sys.exit("gen_skin: no parameter named %r in params.json" % name)
    for _, pname, _ in SKY_STRIP:
        if pname not in index_of:
            sys.exit("gen_skin: no control named %r in params.json" % pname)

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
        # SKY uses gen_ui's overlays and the choice texts of MAIN's menus, which other pages must still bring.
        pd = tui["pageData"]
        defs = pd["componentDefinitions"]["localComponentDefinitions"]
        missing = set(SKY_OVERLAYS) - {e["key"] for e in defs}
        if missing:
            sys.exit("gen_skin: gen_ui no longer writes %s; SKY's strip needs them" % sorted(missing))
        defs.extend(sky_cell_definitions())
        repl = {e["key"] for e in pd["replacementStrings"]}
        for _, pname, overlay in SKY_STRIP:
            key = "Parameter %d" % index_of[pname]
            choices = next(q["choices"] for q in plist if q["index"] == index_of[pname])
            if overlay == "menu overlay" and key not in repl:
                pd["replacementStrings"].append({"key": key, "value": choices})
        tabs[:] = [sky_tab(index_of, disp), tabs[0], layers] + tabs[1:]
        for i, t in enumerate(tabs):
            t["fnKeyIndex"] = i
        png_copy(tmp, "button_on.png", "button_off.png", "seg4_on.png", "seg4_off.png")
        gen_art.generate(tmp)
        ql_path = os.path.join(tmp, "Q-Links.json")
        ql = json.load(open(ql_path))
        # gen_ui numbered its map by its own tab order (names); renumber it by the final order.
        by_name = {names[e["Tab"] - 1]: e for e in ql["Screen Mode Q-Links"]["map"]}
        blank = {"Q-Link %d" % (i + 1): -1 for i in range(16)}
        m = []
        for i, t in enumerate(tabs):
            if t["tabName"] == "SKY":
                e = sky_qlinks(index_of)
            elif t["tabName"] in by_name:
                e = by_name[t["tabName"]]
            else:
                e = {"Tab": 0, "SubTab": 1, "Bank Direction": "Column", "Q-Links": dict(blank)}
            e["Tab"] = i + 1
            m.append(e)
        ql["Screen Mode Q-Links"]["map"] = m
        # (no one-shot on a Q-Link: a knob turned would fire it again and again as it goes back to Off)
        one_shot = {index_of[n] for n in index_of if ONE_SHOT.match(n) and index_of[n] < disp["d_pad"]}
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
