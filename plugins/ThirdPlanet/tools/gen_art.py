#!/usr/bin/env python3
"""Draws the PNGs of Third Planet's star-chart screen (spec 7, 8): one native 1280x628 background per tab
and the small film strips (frames stacked top to bottom) the display parameters drive.

    python3 plugins/ThirdPlanet/tools/gen_art.py [out_dir]     (default: plugins/ThirdPlanet/build/art)

gen_skin.py calls generate(out_dir). Geometry and colours come from layout.py. Static captions in the
backgrounds use a built-in 5x7 pixel font scaled by whole numbers; live text is drawn by the MPC's fonts.
Standard library only (no PIL).
"""
import math
import os
import random
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import layout as L  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))

# ---- PNG and drawing ----


def write_png(path, width, rows):
    """rows: byte strings of width * 4 RGBA bytes. Returns the file size."""
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + bytes(r) for r in rows)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, len(rows), 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)
    return len(png)


def _clamp(v):
    return 0.0 if v < 0.0 else 1.0 if v > 1.0 else v


class Canvas:
    def __init__(self, w, h, fill=L.CLEAR):
        self.w, self.h = w, h
        self.px = [bytearray(bytes(fill) * w) for _ in range(h)]

    def blend(self, x, y, colour, a):
        """Composites colour over pixel (x, y) at coverage a (0..1, times the colour's own alpha)."""
        a = _clamp(a * colour[3] / 255.0)
        if a <= 0.0 or not (0 <= x < self.w and 0 <= y < self.h):
            return
        row, i = self.px[y], 4 * x
        oa = row[i + 3] / 255.0
        na = a + oa * (1.0 - a)
        for k in range(3):
            row[i + k] = int(round((colour[k] * a + row[i + k] * oa * (1.0 - a)) / na))
        row[i + 3] = int(round(na * 255))

    def cover(self, x0, y0, x1, y1, coverage, colour, alpha=1.0):
        """Blends colour over the box [x0, x1) x [y0, y1) with coverage(px, py) at each pixel centre."""
        for y in range(max(0, int(math.floor(y0))), min(self.h, int(math.ceil(y1)))):
            for x in range(max(0, int(math.floor(x0))), min(self.w, int(math.ceil(x1)))):
                a = coverage(x + 0.5, y + 0.5)
                if a > 0.0:
                    self.blend(x, y, colour, a * alpha)

    def dot(self, cx, cy, r, colour, alpha=1.0):
        """An anti-aliased disc."""
        self.cover(cx - r - 1, cy - r - 1, cx + r + 2, cy + r + 2,
                   lambda x, y: _clamp(r + 0.5 - math.hypot(x - cx, y - cy)), colour, alpha)

    def ring(self, cx, cy, r, width, colour, alpha=1.0):
        """An anti-aliased circle line of the given width, centred on radius r."""
        o = r + width / 2 + 1
        self.cover(cx - o, cy - o, cx + o + 1, cy + o + 1,
                   lambda x, y: _clamp(width / 2 + 0.5 - abs(math.hypot(x - cx, y - cy) - r)), colour, alpha)

    def line(self, x0, y0, x1, y1, colour, width=1.0, alpha=1.0):
        dx, dy = x1 - x0, y1 - y0
        ll = dx * dx + dy * dy or 1e-9

        def cov(x, y):
            t = max(0.0, min(1.0, ((x - x0) * dx + (y - y0) * dy) / ll))
            return _clamp(width / 2 + 0.5 - math.hypot(x - x0 - t * dx, y - y0 - t * dy))
        o = width / 2 + 1
        self.cover(min(x0, x1) - o, min(y0, y1) - o, max(x0, x1) + o + 1, max(y0, y1) + o + 1, cov, colour, alpha)

    def fill_rect(self, x, y, w, h, colour):
        """Sets a pixel-aligned rect (no blending)."""
        c = bytes(colour)
        for yy in range(max(0, y), min(self.h, y + h)):
            x0, x1 = max(0, x), min(self.w, x + w)
            if x1 > x0:
                self.px[yy][4 * x0:4 * x1] = c * (x1 - x0)

    def hline(self, x0, x1, y, colour):
        self.fill_rect(x0, y, x1 - x0, 1, colour)

    def frame(self, x, y, w, h, colour):
        """A 1 px outline just inside the rect."""
        self.fill_rect(x, y, w, 1, colour)
        self.fill_rect(x, y + h - 1, w, 1, colour)
        self.fill_rect(x, y, 1, h, colour)
        self.fill_rect(x + w - 1, y, 1, h, colour)

    def round_rect(self, x, y, w, h, r, colour, stroke=None, alpha=1.0):
        """A rounded rect: filled, or (stroke = width) its outline, drawn inside the rect."""
        cx, cy, hx, hy = x + w / 2, y + h / 2, w / 2 - r, h / 2 - r

        def sd(px, py):   # signed distance to the edge, negative inside
            qx, qy = abs(px - cx) - hx, abs(py - cy) - hy
            return math.hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - r
        if stroke is None:
            cov = lambda px, py: _clamp(0.5 - sd(px, py))
        else:
            cov = lambda px, py: _clamp(stroke / 2 + 0.5 - abs(sd(px, py) + stroke / 2))
        self.cover(x, y, x + w, y + h, cov, colour, alpha)

    def text(self, s, x, y, scale, colour, track=1):
        """Crisp bitmap text, top-left at (x, y); returns its width."""
        for ch in s.upper():
            for gy, line in enumerate(FONT[ch]):
                for gx, bit in enumerate(line):
                    if bit == "#":
                        self.fill_rect(x + gx * scale, y + gy * scale, scale, scale, colour)
            x += (GLYPH_W + track) * scale
        return text_width(s, scale, track)

    def rows(self):
        return [bytes(r) for r in self.px]


# ---- A 5x7 pixel font for the static captions (uppercase, digits, a little punctuation) ----

GLYPH_W, GLYPH_H = 5, 7
_F = {
    "A": ".###. #...# #...# ##### #...# #...# #...#",
    "B": "####. #...# #...# ####. #...# #...# ####.",
    "C": ".###. #...# #.... #.... #.... #...# .###.",
    "D": "####. #...# #...# #...# #...# #...# ####.",
    "E": "##### #.... #.... ####. #.... #.... #####",
    "F": "##### #.... #.... ####. #.... #.... #....",
    "G": ".###. #...# #.... #.### #...# #...# .####",
    "H": "#...# #...# #...# ##### #...# #...# #...#",
    "I": ".###. ..#.. ..#.. ..#.. ..#.. ..#.. .###.",
    "J": "..### ...#. ...#. ...#. ...#. #..#. .##..",
    "K": "#...# #..#. #.#.. ##... #.#.. #..#. #...#",
    "L": "#.... #.... #.... #.... #.... #.... #####",
    "M": "#...# ##.## #.#.# #.#.# #...# #...# #...#",
    "N": "#...# #...# ##..# #.#.# #..## #...# #...#",
    "O": ".###. #...# #...# #...# #...# #...# .###.",
    "P": "####. #...# #...# ####. #.... #.... #....",
    "Q": ".###. #...# #...# #...# #.#.# #..#. .##.#",
    "R": "####. #...# #...# ####. #.#.. #..#. #...#",
    "S": ".#### #.... #.... .###. ....# ....# ####.",
    "T": "##### ..#.. ..#.. ..#.. ..#.. ..#.. ..#..",
    "U": "#...# #...# #...# #...# #...# #...# .###.",
    "V": "#...# #...# #...# #...# #...# .#.#. ..#..",
    "W": "#...# #...# #...# #.#.# #.#.# #.#.# .#.#.",
    "X": "#...# #...# .#.#. ..#.. .#.#. #...# #...#",
    "Y": "#...# #...# .#.#. ..#.. ..#.. ..#.. ..#..",
    "Z": "##### ....# ...#. ..#.. .#... #.... #####",
    "0": ".###. #...# #..## #.#.# ##..# #...# .###.",
    "1": "..#.. .##.. ..#.. ..#.. ..#.. ..#.. .###.",
    "2": ".###. #...# ....# ...#. ..#.. .#... #####",
    "3": "##### ...#. ..#.. ...#. ....# #...# .###.",
    "4": "...#. ..##. .#.#. #..#. ##### ...#. ...#.",
    "5": "##### #.... ####. ....# ....# #...# .###.",
    "6": "..##. .#... #.... ####. #...# #...# .###.",
    "7": "##### ....# ...#. ..#.. .#... .#... .#...",
    "8": ".###. #...# #...# .###. #...# #...# .###.",
    "9": ".###. #...# #...# .#### ....# ...#. .##..",
    " ": "..... ..... ..... ..... ..... ..... .....",
    "-": "..... ..... ..... .###. ..... ..... .....",
    "–": "..... ..... ..... ##### ..... ..... .....",
    "·": "..... ..... ..... ..#.. ..... ..... .....",
    ".": "..... ..... ..... ..... ..... ..... ..#..",
    ",": "..... ..... ..... ..... ..... ..#.. .#...",
    ":": "..... ..... ..#.. ..... ..... ..#.. .....",
    "/": "....# ....# ...#. ..#.. .#... #.... #....",
    "%": "##..# ##..# ...#. ..#.. .#... #..## #..##",
    "'": "..#.. ..#.. .#... ..... ..... ..... .....",
    "(": "...#. ..#.. .#... .#... .#... ..#.. ...#.",
    ")": ".#... ..#.. ...#. ...#. ...#. ..#.. .#...",
    "+": "..... ..#.. ..#.. ##### ..#.. ..#.. .....",
    "!": "..#.. ..#.. ..#.. ..#.. ..#.. ..... ..#..",
    "?": ".###. #...# ....# ...#. ..#.. ..... ..#..",
}
FONT = {ch: rows.split() for ch, rows in _F.items()}
assert all(len(g) == GLYPH_H and all(len(r) == GLYPH_W for r in g) for g in FONT.values())


def text_width(s, scale, track=1):
    """Width in pixels of s in the bitmap font; KeyError on a character the font lacks."""
    for ch in s.upper():
        FONT[ch]
    n = len(s)
    return 0 if n == 0 else (n * (GLYPH_W + track) - track) * scale


def text_height(scale):
    return GLYPH_H * scale


# ---- Film strips ----


def strip(out_dir, name, draw):
    w, h, frames = L.ASSETS[name]
    rows = []
    for f in range(frames):
        c = Canvas(w, h)
        draw(c, f)
        rows += c.rows()
    write_png(os.path.join(out_dir, name), w, rows)


def ring_frame(c, f):
    """d_ring: 0 none; 1+k: k of 32 dots lit (cream); 34+k: the chord's last bar (accent); 67+k: held (accent,
    and the unlit dots dark red, so the frozen ring reads red)."""
    if f == 0:
        return
    state, k = divmod(f - 1, L.RING_FRAMES_PER_STATE)
    lit = L.CREAM if state == 0 else L.ACCENT
    unlit = L.DIM_ACCENT if state == 2 else L.DIM
    cx, cy = c.w / 2, c.h / 2
    for j in range(L.RING_DOTS):
        a = 2 * math.pi * j / L.RING_DOTS - math.pi / 2     # from 12 o'clock, clockwise
        x, y = cx + L.RING_R * math.cos(a), cy + L.RING_R * math.sin(a)
        if j < k:
            c.dot(x, y, 3.2, lit)
        else:
            c.dot(x, y, 1.8, unlit)


def planet_frame(c, f):
    """level (0..7) x 3 + state (0 heard, 1 muted, 2 flash): a thin rim, a body that grows with the level;
    dim when muted; a halo on a flash (cream: the accent is not the planets')."""
    level, state = divmod(f, L.PLANET_STATES)
    cx, cy = c.w / 2, c.h / 2
    muted = state == 1
    rim = L.DIM if muted else L.mix(L.BG, L.CREAM, 0.7)
    body = L.DIM if muted else L.CREAM
    if state == 2:
        c.ring(cx, cy, 28, 1.6, L.CREAM, 0.85)
    c.ring(cx, cy, 22, 1.2, rim)
    if level > 0:
        c.dot(cx, cy, 4.0 + 2.3 * level, body)


def phrase_dot(bar):
    """Centre of bar (0..7) in a phrase.png frame."""
    w, h, _ = L.ASSETS["phrase.png"]
    step = w / L.PHRASE_BARS
    return step * bar + step / 2, h / 2


def phrase_frame(c, f):
    """d_phrase: 0 none; n: bar n lit. Bar 1 is marked by a thin ring (where Evolve may act)."""
    x0, y0 = phrase_dot(0)
    c.ring(x0, y0, 7.5, 1.0, L.DIM)
    for b in range(L.PHRASE_BARS):
        x, y = phrase_dot(b)
        if b == f - 1:
            c.dot(x, y, 4.5, L.CREAM)
        else:
            c.dot(x, y, 2.2, L.DIM)


def star_frame(c, f):
    """d_slot: 0 gap (a hollow ring), 1 walk (dim), 2 written (cream, short rays), 3 playing (accent, longer)."""
    cx, cy = c.w / 2, c.h / 2
    if f == 0:
        c.ring(cx, cy, 4.0, 1.0, L.DIM)
    elif f == 1:
        c.dot(cx, cy, 4.0, L.DIM)
    else:
        colour, r, ray = (L.CREAM, 5.0, 9.0) if f == 2 else (L.ACCENT, 6.0, 12.5)
        c.line(cx - ray, cy, cx + ray, cy, colour, 1.0, 0.7)
        c.line(cx, cy - ray, cx, cy + ray, colour, 1.0, 0.7)
        c.dot(cx, cy, r, colour)


def puck(c, f):
    c.dot(c.w / 2, c.h / 2, c.w / 2 - 0.8, L.CREAM)


def disc(c, f):
    """Opaque black, a thin cream rim; it hides parked pucks under the chord name."""
    c.dot(c.w / 2, c.h / 2, L.DISC_R, L.BG)
    c.ring(c.w / 2, c.h / 2, L.DISC_R - 0.8, 1.5, L.CREAM)


def btn_off(c, f):
    c.round_rect(0, 0, c.w, c.h, 8, L.BG)
    c.round_rect(0, 0, c.w, c.h, 8, L.DIM, stroke=1.5)


def btn_on(c, f):
    c.round_rect(0, 0, c.w, c.h, 8, L.mix(L.BG, L.CREAM, 0.22))
    c.round_rect(0, 0, c.w, c.h, 8, L.CREAM, stroke=2.0)


def cursor(c, f):
    if f == 1:
        c.round_rect(0, 0, c.w, c.h, c.h / 2, L.CREAM)


# ---- Backgrounds ----


def caption(c, text, x, y, colour=L.DIM, rule_to=None):
    """A static caption in the bitmap font, optionally followed by a hairline to x = rule_to."""
    w = c.text(text, x, y, L.CAPTION_SCALE, colour, L.CAPTION_TRACK)
    if rule_to is not None:
        c.hline(x + w + 10, rule_to, y + text_height(L.CAPTION_SCALE) // 2, L.FAINT)


def constellation(c, centres, gap, labels):
    """The static lines between neighbouring slot stars, stopping short of each star and of every label
    rect (the longest stretch of each line clear of them is drawn)."""
    segs = []
    for (x0, y0), (x1, y1) in zip(centres, centres[1:]):
        d = math.hypot(x1 - x0, y1 - y0)
        ux, uy = (x1 - x0) / d, (y1 - y0) / d
        best, run = None, None
        n = int(d * 2)
        for i in range(n + 1):
            t = gap + (d - 2 * gap) * i / n
            x, y = x0 + ux * t, y0 + uy * t
            if any(_near_rect(x, y, r, 4) for r in labels):
                run = None
                continue
            run = (run[0], t) if run else (t, t)
            if best is None or run[1] - run[0] > best[1] - best[0]:
                best = run
        if best is None or best[1] - best[0] < 4:
            continue
        seg = (x0 + ux * best[0], y0 + uy * best[0], x0 + ux * best[1], y0 + uy * best[1])
        c.line(*seg, colour=L.DIM, width=1.0, alpha=0.55)
        segs.append(seg)
    return segs


def _seg_dist(px, py, seg):
    x0, y0, x1, y1 = seg
    dx, dy = x1 - x0, y1 - y0
    t = max(0.0, min(1.0, ((px - x0) * dx + (py - y0) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - x0 - t * dx, py - y0 - t * dy)


def field_stars(c, field, count, avoid, seed):
    """Faint, irregular stars in field (x, y, w, h): at most 1 px, mostly dim, never in rows, at least 18 px
    apart, and nowhere avoid(x, y) is true."""
    rng = random.Random(seed)
    fx, fy, fw, fh = field
    placed = []
    tries = 0
    while len(placed) < count and tries < count * 200:
        tries += 1
        x, y = fx + rng.random() * fw, fy + rng.random() * fh
        if avoid(x, y) or any(math.hypot(x - a, y - b) < 18 for a, b in placed):
            continue
        placed.append((x, y))
        if rng.random() < 0.12:
            c.dot(x, y, 0.9, L.CREAM, rng.uniform(0.45, 0.7))
        else:
            c.dot(x, y, rng.uniform(0.55, 0.9), L.DIM, rng.uniform(0.45, 0.9))
    return placed


def _near_rect(x, y, r, pad):
    rx, ry, rw, rh = r
    return rx - pad <= x <= rx + rw + pad and ry - pad <= y <= ry + rh + pad


def bg_sky():
    c = Canvas(L.SCREEN_W, L.SCREEN_H, L.BG)
    c.hline(L.RULE_X0, L.RULE_X1, L.HEADER_RULE_Y, L.FAINT)
    for text, x, y in L.SKY_CAPTIONS:
        caption(c, text, x, y, rule_to=L.CAPTION_RULE_X1)
    c.hline(L.RULE_X0, L.RULE_X1, L.STRIP_RULE_Y, L.FAINT)
    segs = constellation(c, L.SLOT_CENTRES, L.CONSTELLATION_GAP, L.SLOT_STARS + L.SLOT_LABELS)
    star_rx, star_ry = L.BLACK_RX + 10, L.BLACK_RY + 10

    def avoid(x, y):
        return (((x - L.SKY_CX) / star_rx) ** 2 + ((y - L.SKY_CY) / star_ry) ** 2 <= 1.0
                or any(math.hypot(x - sx, y - sy) < 24 for sx, sy in L.SLOT_CENTRES)
                or any(_near_rect(x, y, r, 6) for r in L.SKY_LIVE_RECTS)
                or any(_seg_dist(x, y, s) < 7 for s in segs))
    field_stars(c, L.SKY_STAR_FIELD, L.SKY_STAR_COUNT, avoid, seed=1729)
    # The orbit area stays pure black whatever was drawn above (spike run 4: pucks over black cost +2.7,
    # over a picture +12).
    for y in range(L.SCREEN_H):
        for x in range(L.SCREEN_W):
            if L.in_black(x + 0.5, y + 0.5):
                c.px[y][4 * x:4 * x + 4] = bytes(L.BG)
    return c


def bg_plain():
    c = Canvas(L.SCREEN_W, L.SCREEN_H, L.BG)
    c.hline(L.RULE_X0, L.RULE_X1, L.STATUS_RULE_Y, L.FAINT)
    return c


def bg_deck():
    c = bg_plain()
    c.hline(L.RULE_X0, L.RULE_X1, L.DECK_RULE_Y, L.FAINT)
    return c


def bg_chords():
    c = bg_plain()
    segs = constellation(c, L.CHORDS_CENTRES, L.CHORDS_GAP, L.CHORDS_STARS + L.CHORDS_LABELS)

    def avoid(x, y):
        return (any(math.hypot(x - sx, y - sy) < 30 for sx, sy in L.CHORDS_CENTRES)
                or any(_near_rect(x, y, r, 4) for r in L.CHORDS_LABELS)
                or any(_seg_dist(x, y, s) < 8 for s in segs))
    field_stars(c, L.CHORDS_STAR_FIELD, L.CHORDS_STAR_COUNT, avoid, seed=36)
    return c


def bg_setup():
    """The pad map (spec 8.6): text and one thin frame a bank, never pad squares, so it cannot read as
    touchable. Pads as on the MPC: 1..4 the bottom row, 13..16 the top."""
    c = bg_plain()
    th2 = text_height(2)
    for bank in L.PAD_BANKS:
        letter, title, x, w, cells = bank
        lw = c.text(letter, x, L.PAD_TITLE_Y, 2, L.CREAM, 2)
        c.text(title, x + lw + 16, L.PAD_TITLE_Y, 2, L.DIM, 2)
        c.frame(x - 6, L.PAD_GRID_Y - 4, 4 * w + 12, 4 * L.PAD_ROW_H + 8, L.FAINT)
        for pad in range(1, 17):
            cx, cy, cw, ch = L.pad_cell(bank, pad)
            name = cells[pad - 1]
            if letter == "A":   # the cell is the scale degree, which is the pad number
                c.text(name, cx + (cw - text_width(name, 2)) // 2, cy + (ch - th2) // 2, 2, L.CREAM)
                continue
            c.text(str(pad), cx + 4, cy + 5, 1, L.DIM)
            if name:
                c.text(name, cx + (cw - text_width(name, 2)) // 2, cy + 20, 2, L.CREAM)
    caption(c, L.PAD_MAP_CAPTION, L.PAD_BANKS[0][2], L.PAD_CAPTION_Y)
    return c


BACKGROUND_DRAW = {"bg_sky.png": bg_sky, "bg_deck.png": bg_deck, "bg_chords.png": bg_chords,
                   "bg_setup.png": bg_setup, "bg_plain.png": bg_plain}
STRIP_DRAW = {"ring.png": ring_frame, "planet.png": planet_frame, "phrase.png": phrase_frame,
              "star.png": star_frame, "puck.png": puck, "disc.png": disc, "btn_on.png": btn_on,
              "btn_off.png": btn_off, "cursor.png": cursor}


def generate(out_dir):
    """Writes every image of layout.ASSETS into out_dir; returns {name: (w, h, frames)}."""
    os.makedirs(out_dir, exist_ok=True)
    for name, draw in STRIP_DRAW.items():
        strip(out_dir, name, draw)
    for name, draw in BACKGROUND_DRAW.items():
        c = draw()
        write_png(os.path.join(out_dir, name), c.w, c.rows())
    assert set(STRIP_DRAW) | set(BACKGROUND_DRAW) == set(L.ASSETS)
    return dict(L.ASSETS)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "build", "art")
    sizes = generate(out)
    total = 0
    for name in sorted(sizes):
        n = os.path.getsize(os.path.join(out, name))
        total += n
        w, h, frames = sizes[name]
        print("%-14s %4dx%-4d x%-3d %7d bytes" % (name, w, h, frames, n))
    print("gen_art: wrote %d images, %d bytes, to %s" % (len(sizes), total, os.path.normpath(out)))


if __name__ == "__main__":
    main()
