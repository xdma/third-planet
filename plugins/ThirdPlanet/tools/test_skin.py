"""Structural tests of the generated skin (skins/TUI.json, skins/Q-Links.json):

    python3 -m unittest plugins/ThirdPlanet/tools/test_skin.py

They read the committed skins/ and build/params.json (build first), so run gen_skin.py before them.
"""
import json
import os
import re
import struct
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_skin  # noqa: E402
import layout as L  # noqa: E402

SKINS = os.path.join(HERE, "..", "skins")
PARAMS = os.path.join(HERE, "..", "build", "params.json")
HANDLE = re.compile(r"^(?:WithName\[[^\]]*\]/)?Parameter (\d+)$")


def rect(c):
    return tuple(int(v) for v in c["bounds"]["bounds"].split())


def overlap(a, b):
    ax, ay, aw, ah = a
    bx, by, bw, bh = b
    return ax < bx + bw and bx < ax + aw and ay < by + bh and by < ay + ah


def inside(a, b):
    """Is rect a inside rect b?"""
    return b[0] <= a[0] and b[1] <= a[1] and a[0] + a[2] <= b[0] + b[2] and a[1] + a[3] <= b[1] + b[3]


def png_size(path):
    with open(path, "rb") as f:
        head = f.read(24)
    return struct.unpack(">II", head[16:24])


@unittest.skipUnless(os.path.isfile(PARAMS), "build the plugin first (no build/params.json)")
class SkinTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.pd = json.load(open(os.path.join(SKINS, "TUI.json")))["pageData"]
        cls.ql = json.load(open(os.path.join(SKINS, "Q-Links.json")))
        cls.plist = json.load(open(PARAMS))["params"]
        cls.controls, cls.disp = gen_skin.parameter_maps(cls.plist)
        cls.tabs = {t["tabName"]: t for t in cls.pd["tabs"]}
        cls.sky = cls.tabs["SKY"]["componentDefinition"]["componentsData"]
        cls.defs = {d["key"]: d["value"] for d in cls.pd["componentDefinitions"]["localComponentDefinitions"]}

    def of_type(self, kind):
        return [c for c in self.sky if c["componentData"]["type"] == kind]

    def test_tab_order(self):
        names = [t["tabName"] for t in self.pd["tabs"]]
        self.assertEqual(names[:3], ["SKY", "PLAY", "LAYERS"])
        self.assertNotIn("MAIN", names)
        self.assertEqual([t["fnKeyIndex"] for t in self.pd["tabs"]], list(range(len(names))))
        self.assertEqual([e["Tab"] for e in self.ql["Screen Mode Q-Links"]["map"]], list(range(1, len(names) + 1)))

    def test_definition_keys_unique(self):
        keys = [d["key"] for d in self.pd["componentDefinitions"]["localComponentDefinitions"]]
        self.assertEqual(len(keys), len(set(keys)))

    def test_pucks_on_their_orbits(self):
        """R12: pucks 1-4 (the Sequence) on the inner pad, 5-8 (Sequence 2) on the outer."""
        pads = self.of_type("XYPad")
        self.assertEqual(len(pads), 8)
        for i, c in enumerate(pads, 1):
            data = c["componentData"]["data"]
            self.assertEqual(data["xPositionHandle"], "Parameter %d" % self.disp["d_px%d" % i])
            self.assertEqual(data["yPositionHandle"], "Parameter %d" % self.disp["d_py%d" % i])
            self.assertEqual(data["image"], "puck.png")
            self.assertEqual(rect(c), L.ORBIT_INNER if i <= 4 else L.ORBIT_OUTER)

    def test_z_order(self):
        """The background first; the disc after every puck (parked pucks hide behind it); the cover last, a blank
        Name label over both pads."""
        names = [c["componentData"]["name"] for c in self.sky]
        self.assertEqual(names[0], "background")
        last_puck = max(i for i, c in enumerate(self.sky) if c["componentData"]["type"] == "XYPad")
        self.assertGreater(names.index("disc"), last_puck)
        cover = self.sky[-1]
        self.assertEqual(cover["componentData"]["name"], "cover")
        data = cover["componentData"]["data"]
        self.assertEqual(data["type"], "Name")
        self.assertTrue(data["handleName"].startswith("WithName[ ]/"))
        self.assertEqual(data["textStyle"]["colour"][:2], "00")
        for pad in self.of_type("XYPad"):
            self.assertTrue(inside(rect(pad), rect(cover)))

    def test_handles_resolve(self):
        """Every SKY handle names an existing parameter; component instances remap Data to one."""
        n = len(self.plist)
        for c in self.sky:
            data = c["componentData"]["data"]
            handles = [data[k] for k in ("handleName", "xPositionHandle", "yPositionHandle") if k in data]
            if c["componentData"]["type"] in self.defs:
                self.assertEqual(handles, ["Data"])
                handles = [e["value"] for e in c["handle remapping"]["map"] if e["key"] == "Data"]
                self.assertEqual(len(handles), 1, c["componentData"]["name"])
            for h in handles:
                m = HANDLE.match(h)
                self.assertIsNotNone(m, (c["componentData"]["name"], h))
                self.assertLess(int(m.group(1)), n)

    def test_images(self):
        """Every image exists; a film strip's frames match layout.ASSETS; a Regular image (not scaled on the MPC)
        sits at its native size."""
        for c in self.of_type("Image"):
            data = c["componentData"]["data"]
            path = os.path.join(SKINS, data["image"])
            self.assertTrue(os.path.isfile(path), data["image"])
            w, h = png_size(path)
            fw, fh, frames = L.ASSETS[data["image"]]
            self.assertEqual((w, h), (fw, fh * frames), data["image"])
            if data["imageType"] == "FilmStrip":
                self.assertEqual(data["numFrames"], frames, data["image"])
            self.assertEqual(rect(c)[2:], (fw, fh), data["image"])

    def test_no_label_over_a_control(self):
        """A label drawn over a control takes its touches: no Label after a strip cell overlaps it, and SKY has no
        Button. (The cover over the pucks is the one wanted case.)"""
        self.assertEqual(self.of_type("Button"), [])
        for i, c in enumerate(self.sky):
            if c["componentData"]["type"] not in self.defs:
                continue
            for later in self.sky[i + 1:]:
                if later["componentData"]["type"] == "Label":
                    self.assertFalse(overlap(rect(c), rect(later)),
                                     (c["componentData"]["name"], later["componentData"]["name"]))

    def test_strip_and_qlinks(self):
        """The 16 cells are MAIN's 16 controls in the brief's order, each in its group's Q-Link rectangle, on the
        knob row of its group (RowTopDown: bank 1 = Q-Links 13-16 = the first group)."""
        cells = [c for c in self.sky if c["componentData"]["type"] in self.defs]
        want = ["Filter", "Evolve", "Motion", "Drift", "Energy", "Wet", "Sweep", "Volume",
                "Drone", "Sequence", "Lead", "Sequence 2", "Piece", "Tempo", "Root", "Mode"]
        self.assertEqual(len(cells), 16)
        bounds = [tuple(int(v) for v in b.split()) for b in self.tabs["SKY"]["qlinkBoundsData"]]
        self.assertEqual(len(bounds), 4)
        entry = self.ql["Screen Mode Q-Links"]["map"][0]
        self.assertEqual(entry["Bank Direction"], "RowTopDown")
        for i, (c, name) in enumerate(zip(cells, want)):
            idx = self.controls[name]
            self.assertEqual(c["handle remapping"]["map"], [{"key": "Data", "value": "Parameter %d" % idx}])
            self.assertEqual(c["bounds"]["acceptsHWFocus"], "Yes")
            self.assertTrue(inside(rect(c), bounds[i // 4]), name)
            self.assertEqual(entry["Q-Links"]["Q-Link %d" % (13 - 4 * (i // 4) + i % 4)], idx, name)
            overlay = self.defs[c["componentData"]["type"]]["actions"][1]["additionalData"]
            self.assertIn(overlay, self.defs, name)
            kind = next(p["kind"] for p in self.plist if p["index"] == idx)
            self.assertEqual(overlay == "menu overlay", kind == "choice", name)
            if kind == "choice":
                self.assertIn("Parameter %d" % idx, {e["key"] for e in self.pd["replacementStrings"]}, name)

    def test_header_and_hot_log(self):
        """R17, R18, R20: the header binds d_tempo (not Tempo or d_ext) and d_solo; the hot log label sits on line 1,
        in the accent, after the cream one."""
        by = {c["componentData"]["name"]: c for c in self.sky}
        handle = lambda n: by[n]["componentData"]["data"]["handleName"]
        self.assertEqual(handle("tempo"), "Parameter %d" % self.disp["d_tempo"])
        self.assertEqual(handle("solo"), "Parameter %d" % self.disp["d_solo"])
        used = {c["componentData"]["data"].get("handleName") for c in self.sky}
        self.assertNotIn("Parameter %d" % self.disp["d_ext"], used)
        self.assertNotIn("Parameter %d" % self.controls["Tempo"], used)
        names = [c["componentData"]["name"] for c in self.sky]
        self.assertGreater(names.index("log 1 hot"), names.index("log 1"))
        self.assertEqual(rect(by["log 1 hot"]), rect(by["log 1"]))
        self.assertEqual(handle("log 1 hot"), "Parameter %d" % self.disp["d_log1hot"])
        self.assertEqual(by["log 1 hot"]["componentData"]["data"]["textStyle"]["colour"], L.argb(L.ACCENT))
        self.assertEqual(by["log 1"]["componentData"]["data"]["textStyle"]["colour"], L.argb(L.CREAM))

    def test_names_unique(self):
        """R19: no two parameters share a name (d_next is "Next", the one-shot "Next chord")."""
        names = [p["name"] for p in self.plist]
        self.assertEqual(len(names), len(set(names)))

    def test_no_one_shot_on_a_qlink(self):
        """The one-shots (and the PLAY page's Next chord, 115, not d_next) stay off the knobs."""
        play = self.tabs["PLAY"]["componentDefinition"]["componentsData"]
        nxt = next(c for c in play if c["componentData"]["name"] == "Next chord")
        self.assertEqual(nxt["handle remapping"]["map"][0]["value"], "Parameter %d" % self.controls["Next chord"])
        self.assertLess(self.controls["Next chord"], self.disp["d_pad"])
        one_shot = {i for n, i in self.controls.items() if gen_skin.ONE_SHOT.match(n)}
        for e in self.ql["Screen Mode Q-Links"]["map"]:
            self.assertFalse(one_shot & set(e["Q-Links"].values()), e["Tab"])

    def test_encoded_values_as_text_only(self):
        """d_drift* and d_density are encoded numbers: anywhere in the skin they are bound only as Value text."""
        encoded = {"Parameter %d" % self.disp[k] for k in self.disp if k.startswith("d_drift") or k == "d_density"}
        for t in self.pd["tabs"]:
            for c in t["componentDefinition"]["componentsData"]:
                data = c["componentData"]["data"]
                hs = {data.get(k) for k in ("handleName", "xPositionHandle", "yPositionHandle")}
                hs |= {e["value"] for e in c["handle remapping"]["map"]}
                if hs & encoded:
                    self.assertEqual((c["componentData"]["type"], data.get("type")), ("Label", "Value"))

    def test_palette_and_fonts(self):
        """SKY's text uses the three MPC fonts and the palette's cream, dim and one accent (or transparent)."""
        colours = {L.argb(L.CREAM), L.argb(L.DIM), L.argb(L.ACCENT)}
        texts = [c["componentData"]["data"]["textStyle"] for c in self.of_type("Label")]
        for k in set(gen_skin.SKY_CELL.values()):
            texts += [w["componentData"]["data"]["textStyle"] for w in self.defs[k]["componentsData"]
                      if w["componentData"]["type"] == "Label"]
        for st in texts:
            self.assertIn(st["font"]["name"], ("Titillium Web", "Nexa", "Roboto"))
            self.assertTrue(st["colour"] in colours or st["colour"].startswith("00"), st["colour"])


if __name__ == "__main__":
    unittest.main()
