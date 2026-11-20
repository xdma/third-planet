"""Shared geometry and palette of Third Planet's star-chart screen.

gen_art.py draws the images from these numbers and gen_skin.py places the widgets with them, so a widget
and the art under it never drift apart. Rects are (x, y, w, h) in screen pixels; colours are RGBA tuples
(`argb()` gives the skin's "ffRRGGBB" form). Standard library only.
"""
import math

SCREEN_W, SCREEN_H = 1280, 628

# Style C (spec 7): black, cream lines and text, dim secondary, one red accent.
BG = (5, 5, 5, 255)
CREAM = (239, 230, 210, 255)
DIM = (128, 121, 107, 255)
ACCENT = (226, 85, 63, 255)
CLEAR = (0, 0, 0, 0)


def mix(a, b, t):
    """Opaque colour t of the way from a to b."""
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3)) + (255,)


DIM_ACCENT = mix(BG, ACCENT, 0.45)   # the held ring's unlit dots: the whole ring reads red
FAINT = mix(BG, DIM, 0.55)           # hairlines and constellation lines


def argb(c):
    return "ff%02x%02x%02x" % c[:3]


def rect_str(r):
    return "%d %d %d %d" % tuple(int(v) for v in r)


def centred(cx, cy, w, h):
    return (int(round(cx - w / 2)), int(round(cy - h / 2)), w, h)


# ---- Images (gen_art.generate writes them; film-strip frames are stacked top to bottom) ----

ASSETS = {
    # name: (frame w, frame h, frames)
    "ring.png": (260, 260, 100),     # d_ring: 0 none; 1+k k dots; 34+k last bar (accent); 67+k held
    "planet.png": (64, 64, 24),      # Sky::planetFrame: level (0..7) x 3 + state (0 heard, 1 muted, 2 flash)
    "phrase.png": (240, 20, 9),      # d_phrase: 0 none; n bar n lit; bar 1 marked
    "star.png": (28, 28, 4),         # d_slot: 0 gap, 1 walk, 2 written, 3 playing (accent)
    "puck.png": (12, 12, 1),
    "disc.png": (150, 150, 1),
    "btn_on.png": (120, 44, 1),
    "btn_off.png": (120, 44, 1),
    "cursor.png": (58, 6, 2),        # PATTERN step cursor: off, on
}
BACKGROUNDS = ("bg_sky.png", "bg_deck.png", "bg_chords.png", "bg_setup.png", "bg_plain.png")
for _bg in BACKGROUNDS:
    ASSETS[_bg] = (SCREEN_W, SCREEN_H, 1)
# Which background each tab uses (PATTERN and the layer pages share the plain one).
TAB_BACKGROUND = {
    "SKY": "bg_sky.png", "DECK": "bg_deck.png", "CHORDS": "bg_chords.png", "SETUP": "bg_setup.png",
    "PATTERN": "bg_plain.png", "DRONE": "bg_plain.png", "SEQ": "bg_plain.png", "SEQ 2": "bg_plain.png",
    "LEAD": "bg_plain.png", "ATMOS": "bg_plain.png", "SPACE": "bg_plain.png",
}

RING_FRAMES_PER_STATE = 33           # 0..32 dots
RING_DOTS = 32
PLANET_LEVELS, PLANET_STATES = 8, 3
PHRASE_BARS = 8

# ---- The status line of every tab but SKY (gen_skin.STATUS sits in y 2..62) ----

STATUS_RULE_Y = 66                   # a dim hairline under it
RULE_X0, RULE_X1 = 23, 1257

# ---- SKY (spec 8.1) ----

HEADER_H = 56
HEADER_RULE_Y = 57

SKY_CX, SKY_CY = 660, 380
# R12: the Sequence on the INNER orbit, Sequence 2 on the OUTER (as the FM-1). Pucks 0..3 are the Sequence
# (inner pad), 4..7 Sequence 2 (outer pad). A puck's x, y (0..1) span its pad.
ORBIT_INNER = centred(SKY_CX, SKY_CY, 340, 220)    # Sequence
ORBIT_OUTER = centred(SKY_CX, SKY_CY, 460, 300)    # Sequence 2
ORBIT_RX, ORBIT_RY = 230, 150                      # the outer orbit
BLACK_MARGIN = 8
BLACK_RX, BLACK_RY = ORBIT_RX + BLACK_MARGIN, ORBIT_RY + BLACK_MARGIN   # bg_sky is pure BG inside this


def in_black(x, y):
    """Is the screen point inside the area bg_sky keeps pure black (the outer orbit ellipse + 8 px)?"""
    return ((x - SKY_CX) / BLACK_RX) ** 2 + ((y - SKY_CY) / BLACK_RY) ** 2 <= 1.0


# The cover: a blank Name label over the orbits so a finger moves nothing.
COVER = ORBIT_OUTER
DISC = centred(SKY_CX, SKY_CY, 150, 150)
DISC_R = 74
CHORD_TEXT = centred(SKY_CX, SKY_CY, 140, 60)
RING = centred(SKY_CX, SKY_CY, 260, 260)
# The ring's dots sit between the disc (r 74) and the inner orbit (ry 110, less the 6 px puck), so no puck
# ever crosses a dot.
RING_R = 92
PUCK = 12

# The constellation: the 8 chord slots on an arc above the orbits, evenly spaced in x.
SLOT_STAR = 28
SLOT_LABEL_W, SLOT_LABEL_H = 80, 26


def _arc(n, x0, dx, cx, a, base, b):
    pts = []
    for i in range(n):
        x = x0 + dx * i
        y = base - b * math.sqrt(max(0.0, 1.0 - ((x - cx) / a) ** 2))
        pts.append((x, int(round(y))))
    return pts


SLOT_CENTRES = _arc(8, 380, 80, SKY_CX, 330.0, 270.0, 160.0)
SLOT_STARS = [centred(x, y, SLOT_STAR, SLOT_STAR) for x, y in SLOT_CENTRES]
SLOT_LABELS = [(x - SLOT_LABEL_W // 2, y - SLOT_STAR // 2 - SLOT_LABEL_H - 2, SLOT_LABEL_W, SLOT_LABEL_H)
               for x, y in SLOT_CENTRES]
CONSTELLATION_GAP = 16               # the lines stop this far from a star's centre

# Left column.
COL_X, COL_W = 24, 290
LOG_W = 380                          # the log lines may run to the black area's edge
SKY_CAPTIONS = [                     # static captions baked into bg_sky: (text, x, top y)
    ("ARC", COL_X, 76),
    ("PHRASE", COL_X, 150),
    ("NEXT", COL_X, 246),
    ("LOG", COL_X, 384),
]
CAPTION_SCALE, CAPTION_TRACK = 2, 2  # the bitmap font: 2x, 2 font pixels between letters
CAPTION_RULE_X1 = 320                # each caption is followed by a hairline to here
ARC_VALUE = (COL_X, 94, COL_W, 38)
PHRASE_STRIP = (COL_X, 172, 240, 20)
EVOLVE_TEXT = (COL_X, 198, COL_W, 28)
NEXT_CHORD = (COL_X, 264, COL_W, 40)
NEXT_IN = (COL_X, 304, COL_W, 28)
NEXT_PROG = (COL_X, 332, COL_W, 28)
LOG_LINES = [(COL_X, 404 + 36 * i, LOG_W, 32) for i in range(3)]   # newest on top

# Right column: Drone, Seq, Lead, Seq 2, Atmos (the planet order of d_planet1..5).
PLANET_NAMES = ("DRONE", "SEQ", "LEAD", "SEQ 2", "ATMOS")
PLANETS = [(1052, 72 + 94 * i, 64, 64) for i in range(5)]
PLANET_LABELS = [(1124, 72 + 94 * i + 20, 130, 24) for i in range(5)]

# Bottom strip: MAIN's 16 values in 4 groups of 4 (one per Q-Link bank).
STRIP_RULE_Y = 552
CELL_W, GROUP_GAP, STRIP_X0 = 72, 32, 16
STRIP_NAME_Y, STRIP_NAME_H = 556, 20
STRIP_VALUE_Y, STRIP_VALUE_H = 576, 44


def strip_cell(i):
    """x of value cell i (0..15)."""
    g, k = divmod(i, 4)
    return STRIP_X0 + g * (4 * CELL_W + GROUP_GAP) + k * CELL_W


STRIP_NAMES = [(strip_cell(i), STRIP_NAME_Y, CELL_W, STRIP_NAME_H) for i in range(16)]
STRIP_VALUES = [(strip_cell(i), STRIP_VALUE_Y, CELL_W, STRIP_VALUE_H) for i in range(16)]
STRIP_GROUPS = [(strip_cell(4 * g), STRIP_NAME_Y, 4 * CELL_W, STRIP_VALUE_Y + STRIP_VALUE_H - STRIP_NAME_Y)
                for g in range(4)]

# The faint field stars of bg_sky: only in this band, never in the black area, never near a slot star,
# a label, a constellation line or text.
SKY_STAR_FIELD = (334, 64, 1016 - 334, STRIP_RULE_Y - 8 - 64)
SKY_STAR_COUNT = 70

# ---- DECK (spec 8.2; Task 7 places the widgets) ----

DECK_BUTTON_W, DECK_BUTTON_H = 120, 44
DECK_ACTION_COLS = (40, 660)         # Play, Hold, Lock, Forget | Next chord, New chords, Shake all, Reset piece
DECK_ACTION_ROWS = (80, 132, 184, 236)
DECK_RULE_Y = 294                    # a hairline between the actions and the layer matrix

# ---- CHORDS (spec 8.3; Task 8 places the widgets) ----

CHORDS_STAR = 40
CHORDS_LABEL_W, CHORDS_LABEL_H = 110, 30
CHORDS_CENTRES = _arc(8, 100, 125, 537.5, 520.0, 300.0, 170.0)
CHORDS_STARS = [centred(x, y, CHORDS_STAR, CHORDS_STAR) for x, y in CHORDS_CENTRES]
CHORDS_LABELS = [(int(x - CHORDS_LABEL_W / 2), y - CHORDS_STAR // 2 - CHORDS_LABEL_H - 2, CHORDS_LABEL_W,
                  CHORDS_LABEL_H) for x, y in CHORDS_CENTRES]
CHORDS_GAP = 22
CHORDS_STAR_FIELD = (24, 72, 1000, 270)
CHORDS_STAR_COUNT = 60

# ---- SETUP (spec 8.6): the pad map in the lower part, as text and one thin frame a bank ----

PAD_MAP_CAPTION = "FROM PAD BASE"
PAD_ROW_H = 50
PAD_TITLE_Y = 360                    # bank titles
PAD_GRID_Y = 384                     # top of the 4 x 4 grid (pads 13..16 on top, 1..4 at the bottom, as on the MPC)
PAD_CAPTION_Y = PAD_GRID_Y + 4 * PAD_ROW_H + 10
PAD_BANKS = [
    # (letter, title, x, cell w, the 16 cells by pad number 1..16 ("" = no function))
    ("A", "LEAD 1–16", 24, 52, [str(n) for n in range(1, 17)]),
    ("B", "EFFECTS", 268, 120, ["REPEAT", "TIME", "TAPE STOP", "SHIMMER",
                                "LOW-PASS", "HIGH-PASS", "SWEEP", "PHASER",
                                "REVERB", "TAKE-OFF", "FREEZE", "WASH",
                                "OCTAVE", "RETURN", "TWITTER", "SUPERNOVA"]),
    ("C", "MOVES", 776, 120, ["RISE", "CLOSE", "TRANSPOSE", "SWEEP",
                              "OPEN", "SHUT", "DROP", "ECHO"] + [""] * 8),
]


def pad_cell(bank, pad):
    """Rect of pad (1..16) in bank (an entry of PAD_BANKS): pad 1 bottom-left, pad 16 top-right."""
    _, _, x, w, _ = bank
    row, col = divmod(pad - 1, 4)
    return (x + col * w, PAD_GRID_Y + (3 - row) * PAD_ROW_H, w, PAD_ROW_H)
