"""Tests for gen_art.py: python3 -m unittest plugins/ThirdPlanet/tools/test_gen_art.py"""
import math
import os
import struct
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_art  # noqa: E402
import layout  # noqa: E402


def read_png(path):
    """(width, height, rows of RGBA bytes) for the 8-bit RGBA, filter-0 PNGs gen_art writes."""
    with open(path, "rb") as f:
        data = f.read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    pos, idat = 8, b""
    w = h = None
    while pos < len(data):
        n, tag = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if tag == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
            assert (depth, ctype) == (8, 6), path
        elif tag == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = 4 * w + 1
    rows = []
    for y in range(h):
        line = raw[y * stride:(y + 1) * stride]
        assert line[0] == 0, "%s: row %d uses filter %d" % (path, y, line[0])
        rows.append(line[1:])
    return w, h, rows


def pixel(rows, x, y):
    i = 4 * int(x)
    return tuple(rows[int(y)][i:i + 4])


class GenArtTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.out = cls.tmp.name
        cls.result = gen_art.generate(cls.out)
        cls.cache = {}

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def png(self, name):
        if name not in self.cache:
            self.cache[name] = read_png(os.path.join(self.out, name))
        return self.cache[name]

    def frame_rows(self, name, f):
        w, h, frames = layout.ASSETS[name]
        return self.png(name)[2][f * h:(f + 1) * h]

    def test_sizes(self):
        self.assertEqual(set(self.result), set(layout.ASSETS))
        for name, (w, h, frames) in layout.ASSETS.items():
            self.assertEqual(self.result[name], (w, h, frames), name)
            pw, ph, _ = self.png(name)
            self.assertEqual((pw, ph), (w, h * frames), name)

    def test_black_orbit(self):
        _, _, rows = self.png("bg_sky.png")
        n = 0
        for y in range(layout.SCREEN_H):
            for x in range(layout.SCREEN_W):
                if layout.in_black(x + 0.5, y + 0.5):
                    n += 1
                    self.assertEqual(pixel(rows, x, y), layout.BG, "bg_sky (%d, %d)" % (x, y))
        rx, ry = layout.ORBIT_RX + 8, layout.ORBIT_RY + 8
        self.assertAlmostEqual(n, math.pi * rx * ry, delta=0.01 * math.pi * rx * ry)

    def test_backgrounds_opaque(self):
        for name in layout.BACKGROUNDS:
            _, _, rows = self.png(name)
            for y, r in enumerate(rows):
                self.assertEqual(r[3::4].count(255), layout.SCREEN_W, "%s row %d has a see-through pixel" % (name, y))

    def test_backgrounds_carry_art(self):
        """Each background has something on it besides black (a hairline at least)."""
        for name in layout.BACKGROUNDS:
            _, _, rows = self.png(name)
            bg = bytes(layout.BG)
            self.assertTrue(any(r != bg * layout.SCREEN_W for r in rows), name)

    def ring_dot(self, k):
        a = 2 * math.pi * k / layout.RING_DOTS - math.pi / 2
        c = layout.ASSETS["ring.png"][0] / 2
        return c + layout.RING_R * math.cos(a), c + layout.RING_R * math.sin(a)

    def test_ring_frames(self):
        rows0 = self.frame_rows("ring.png", 0)
        self.assertTrue(all(not any(r[3::4]) for r in rows0), "frame 0 (no ring) must be empty")
        for f in range(1, 100):
            state, k = divmod(f - 1, layout.RING_FRAMES_PER_STATE)
            rows = self.frame_rows("ring.png", f)
            lit = layout.CREAM if state == 0 else layout.ACCENT
            unlit = layout.DIM_ACCENT if state == 2 else layout.DIM
            for j in range(layout.RING_DOTS):
                x, y = self.ring_dot(j)
                self.assertEqual(pixel(rows, x, y), lit if j < k else unlit, "ring frame %d dot %d" % (f, j))

    def test_ring_starts_at_twelve_clockwise(self):
        x0, y0 = self.ring_dot(0)
        x1, y1 = self.ring_dot(1)
        self.assertAlmostEqual(x0, 130)
        self.assertLess(y0, 130)
        self.assertGreater(x1, x0)
        # between the disc and the inner orbit, less a puck
        self.assertGreater(layout.RING_R - 4, layout.DISC_R)
        self.assertLess(layout.RING_R + 4, layout.ORBIT_INNER[3] / 2 - layout.PUCK / 2)

    def test_phrase_frames(self):
        w, h, _ = layout.ASSETS["phrase.png"]
        for f in range(layout.PHRASE_BARS + 1):
            rows = self.frame_rows("phrase.png", f)
            for b in range(layout.PHRASE_BARS):
                x, y = gen_art.phrase_dot(b)
                want = layout.CREAM if b == f - 1 else layout.DIM
                self.assertEqual(pixel(rows, x, y), want, "phrase frame %d bar %d" % (f, b + 1))

    def test_star_frames(self):
        c = layout.ASSETS["star.png"][0] // 2
        centres = [pixel(self.frame_rows("star.png", f), c, c) for f in range(4)]
        self.assertEqual(centres[3], layout.ACCENT)        # playing
        self.assertEqual(centres[2], layout.CREAM)         # written
        self.assertEqual(centres[1], layout.DIM)           # walk
        self.assertEqual(centres[0][3], 0)                 # a gap is a hollow ring
        for f in range(3):
            rows = self.frame_rows("star.png", f)
            self.assertFalse(any(pixel(rows, x, y)[:3] == layout.ACCENT[:3] and pixel(rows, x, y)[3]
                                 for y in range(len(rows)) for x in range(c * 2)), "accent only when playing")

    def test_planet_frames(self):
        frames = [self.frame_rows("planet.png", f) for f in range(24)]
        self.assertEqual(len({b"".join(r) for r in frames}), 24, "every planet frame differs")
        for f, rows in enumerate(frames):
            for r in rows:
                for x in range(0, len(r), 4):
                    if r[x + 3]:
                        self.assertNotEqual(tuple(r[x:x + 3]), layout.ACCENT[:3], "no accent on planets")
        # the fill grows with the level (heard)
        lit = [sum(r[3::4].count(255) for r in frames[lv * 3]) for lv in range(8)]
        self.assertEqual(lit, sorted(lit))
        self.assertLess(lit[0], lit[7])

    def test_puck_disc_cursor(self):
        c = pixel(self.frame_rows("puck.png", 0), 6, 6)
        self.assertEqual(c, layout.CREAM)
        disc = self.frame_rows("disc.png", 0)
        self.assertEqual(pixel(disc, 75, 75), layout.BG)
        self.assertEqual(pixel(disc, 0, 0)[3], 0)
        self.assertTrue(all(not any(r[3::4]) for r in self.frame_rows("cursor.png", 0)))
        self.assertTrue(any(any(r[3::4]) for r in self.frame_rows("cursor.png", 1)))

    def test_total_size(self):
        total = sum(os.path.getsize(os.path.join(self.out, n)) for n in layout.ASSETS)
        self.assertLess(total, 1200 * 1000)

    def test_font(self):
        self.assertRaises(KeyError, gen_art.text_width, "~", 1)
        for text in ["ARC", "PHRASE", "NEXT", "LOG", layout.PAD_MAP_CAPTION, "SEQ 2"]:
            gen_art.text_width(text, 2)
        for letter, title, x, w, cells in layout.PAD_BANKS:
            gen_art.text_width(letter + "  " + title, 2)
            for name in cells:
                self.assertLessEqual(gen_art.text_width(name, 2), w - 6, name)

    def assert_plain(self, name, rects):
        _, _, rows = self.png(name)
        bg = bytes(layout.BG)
        for (x, y, w, h) in rects:
            for yy in range(y, y + h):
                self.assertEqual(rows[yy][4 * x:4 * (x + w)], bg * w, "%s: art under the widget at %s, row %d"
                                 % (name, (x, y, w, h), yy))

    def test_nothing_under_live_widgets(self):
        """No field star, line or caption under a SKY or CHORDS widget (text and strips must read clean)."""
        self.assert_plain("bg_sky.png", layout.SKY_LIVE_RECTS)
        self.assert_plain("bg_chords.png", layout.CHORDS_STARS + layout.CHORDS_LABELS)

    def test_layout_clear_of_black(self):
        """The slot stars, their labels and lines are outside the black area by layout, not by clipping."""
        for (x, y, w, h) in layout.SLOT_STARS + layout.SLOT_LABELS:
            for px, py in ((x, y), (x + w, y), (x, y + h), (x + w, y + h)):
                self.assertFalse(layout.in_black(px, py), (x, y, w, h))
        for (x, y, w, h) in layout.PLANETS + [layout.ARC_VALUE, layout.NEXT_CHORD] + layout.LOG_LINES:
            for px, py in ((x, y), (x + w, y), (x, y + h), (x + w, y + h)):
                self.assertFalse(layout.in_black(px, py), (x, y, w, h))


if __name__ == "__main__":
    unittest.main()
