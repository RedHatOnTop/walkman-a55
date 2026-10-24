#!/usr/bin/env python3
"""S-Master HX circuit wallpaper for the NW-A55 launcher (480x800, mdpi).

A macro "product shot" of the NW-A50-series main board, re-rendered in black
solder mask. Part placement follows Sony's published board photo: the SoC BGA
beside the microSD slot, memory to its lower right, and in the lower-left
corner the CXD3778GF S-Master HX with its 44.1k/48k dual clock on the left,
two round power inductors with the POSCAP pair-of-pairs between them, and the
row of four test pads below. The I2S path from the SoC into S-Master and the
output path out of it are lit.

Everything is laid out in 1x pixels, rasterised at 3x into material maps
(albedo, height, gloss, metal, emission), lit in one pass, given a tilt-shift
depth of field and Lanczos-downsampled.
"""
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy.ndimage import distance_transform_edt, gaussian_filter

W, H = 480, 800
S = 3
OUT = Path(__file__).with_name("circuit-480x800.jpg")

MONTSERRAT = "/usr/share/fonts/julietaula-montserrat-fonts/Montserrat-{}.otf"
MONO = str(Path.home() / ".local/share/fonts/jetbrains-mono/JetBrainsMono-{}.ttf")

# Placement is measured in pixels on Sony's photo of the NW-A50 main board
# (sony.jp NW-A50_series/feature_7, original_NW-A50_013.jpg, 460x400) and
# mapped onto the screen, so every part keeps its real neighbours and size.
PHOTO_SCALE = 2.25


def M(x, y):
    return ((x - 245) * PHOTO_SCALE + 240, (y - 70) * PHOTO_SCALE + 40)


def Ms(v):
    return v * PHOTO_SCALE


BOARD_EDGE_X = M(147, 0)[0]
SMASTER = M(200, 295)
SMASTER_HALF = Ms(28.5) / 2
XTALS = [M(168, 280), M(169.5, 300)]
RINDS = [M(173, 333), M(234, 333)]
POSCAPS = [M(196, 331), M(213, 331), M(196, 347), M(213, 347)]
TESTPADS = [M(195, 362), M(203, 362), M(211, 362), M(219, 362)]
SOT6 = [M(176, 369.5), M(221, 369.5)]
KF_MARKS = [M(249, 310), M(249, 333)]
SD_RECT = (*M(147, 142), *M(237, 222))
SOC = M(304.5, 161)
SOC_HALF = Ms(71) / 2
MEM = M(355, 270)
MEM_HALF = (Ms(70) / 2, Ms(80) / 2)
QFN_MID = M(300, 257)
QFN_BR = M(326, 361)
QFN_TOP = M(300, 80)
PMIC = M(187.5, 90)
SMALL_IND = M(265, 247)
GOLD_DOTS = [M(180, 235), M(193, 235), M(207, 235)]

MASK = np.array([0.020, 0.022, 0.025], np.float32)
COPPER = np.array([0.034, 0.030, 0.026], np.float32)
GOLD = np.array([0.80, 0.60, 0.30], np.float32)
TIN = np.array([0.66, 0.67, 0.70], np.float32)
SILK = np.array([0.62, 0.62, 0.60], np.float32)
VOID = np.array([0.006, 0.006, 0.007], np.float32)

RAMP = [
    (0.00, "#000000"),
    (0.07, "#1d0e04"),
    (0.20, "#55300c"),
    (0.38, "#a26c24"),
    (0.56, "#d4a24c"),
    (0.72, "#ecc679"),
    (0.86, "#f8e2ae"),
    (1.00, "#fffaf0"),
]

rng = np.random.default_rng(55)


def hexf(h):
    return np.array([int(h[i:i + 2], 16) for i in (1, 3, 5)], np.float32) / 255


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def sc(pts):
    return [(x * S, y * S) for x, y in pts]


# ---------------------------------------------------------------- routing

def offset_polyline(pts, d):
    """Offset an open polyline sideways by d with mitred corners (left of travel is +)."""
    pts = np.asarray(pts, float)
    dirs = pts[1:] - pts[:-1]
    dirs /= np.linalg.norm(dirs, axis=1, keepdims=True)
    normals = np.stack([dirs[:, 1], -dirs[:, 0]], 1)
    out = [pts[0] + normals[0] * d]
    for k in range(1, len(pts) - 1):
        n = normals[k - 1] + normals[k]
        n /= np.linalg.norm(n)
        out.append(pts[k] + n * d / np.dot(n, normals[k]))
    out.append(pts[-1] + normals[-1] * d)
    return [tuple(p) for p in out]


def dedupe(pts):
    out = [pts[0]]
    for p in pts[1:]:
        if np.hypot(p[0] - out[-1][0], p[1] - out[-1][1]) > 1e-6:
            out.append(p)
    return out


# ---------------------------------------------------------------- board

class Board:
    def __init__(self):
        size = (W * S, H * S)
        self.cu = Image.new("L", size)
        self.pad = Image.new("L", size)
        self.hole = Image.new("L", size)
        self.silk = Image.new("L", size)
        self.dimple = Image.new("L", size)
        self.emit = Image.new("F", size)
        self.d_cu = ImageDraw.Draw(self.cu)
        self.d_pad = ImageDraw.Draw(self.pad)
        self.d_hole = ImageDraw.Draw(self.hole)
        self.d_silk = ImageDraw.Draw(self.silk)
        self.d_dimple = ImageDraw.Draw(self.dimple)
        self.d_emit = ImageDraw.Draw(self.emit)
        self.lit = []
        self.parts = []
        self.busy = np.zeros((H, W), bool)

    def claim(self, x0, y0, x1, y1, margin=2.0):
        xa, xb = int(max(0, np.floor(x0 - margin))), int(min(W, np.ceil(x1 + margin)))
        ya, yb = int(max(0, np.floor(y0 - margin))), int(min(H, np.ceil(y1 + margin)))
        if xb > xa and yb > ya:
            self.busy[ya:yb, xa:xb] = True

    def free(self, x0, y0, x1, y1, margin=1.5):
        if x0 - margin < BOARD_EDGE_X + 3 or x1 + margin > W - 1 or y0 - margin < 1 or y1 + margin > H - 1:
            return False
        xa, xb = int(np.floor(x0 - margin)), int(np.ceil(x1 + margin))
        ya, yb = int(np.floor(y0 - margin)), int(np.ceil(y1 + margin))
        return not self.busy[ya:yb, xa:xb].any()

    def trace(self, pts, w=1.2, lit=False, phase=0.0):
        self.d_cu.line(sc(pts), fill=255, width=max(1, round(w * S)), joint="curve")
        for p in (pts[0], pts[-1]):
            self.circle(self.d_cu, p, w / 2)
        for a, b in zip(pts[:-1], pts[1:]):
            self.claim(min(a[0], b[0]), min(a[1], b[1]), max(a[0], b[0]), max(a[1], b[1]), w / 2 + 1)
        if lit:
            self.lit.append((pts, w, phase))

    def circle(self, draw, c, r, fill=255):
        draw.ellipse([(c[0] - r) * S, (c[1] - r) * S, (c[0] + r) * S, (c[1] + r) * S], fill=fill)

    def via(self, c, exposed=False, r_ring=1.6, r_drill=0.7):
        self.circle(self.d_cu, c, r_ring)
        if exposed:
            self.circle(self.d_pad, c, r_ring)
            self.circle(self.d_hole, c, r_drill)
        else:
            self.circle(self.d_dimple, c, r_drill + 0.2)
        self.claim(c[0] - r_ring, c[1] - r_ring, c[0] + r_ring, c[1] + r_ring, 0.8)

    def smd_pad(self, c, w, h, radius=0.4):
        x, y = c
        box = [(x - w / 2) * S, (y - h / 2) * S, (x + w / 2) * S, (y + h / 2) * S]
        self.d_pad.rounded_rectangle(box, radius=radius * S, fill=255)
        self.d_cu.rounded_rectangle(box, radius=radius * S, fill=255)
        self.claim(x - w / 2, y - h / 2, x + w / 2, y + h / 2, 0.8)

    def silk_text(self, xy, text, size, anchor="la", font="mono", tracking=0.0):
        path = MONO.format("Medium") if font == "mono" else MONTSERRAT.format("SemiBold")
        f = ImageFont.truetype(path, int(size * S))
        x, y = xy
        widths = [self.d_silk.textlength(ch, font=f) / S for ch in text]
        total = sum(widths) + tracking * (len(text) - 1)
        if anchor[0] == "m":
            x -= total / 2
        elif anchor[0] == "r":
            x -= total
        for ch, w in zip(text, widths):
            self.d_silk.text((x * S, y * S), ch, font=f, fill=255, anchor="l" + anchor[1])
            x += w + tracking


def footprint(b, kind, c, **kw):
    """Register a part, lay its copper pads and reserve its area."""
    x, y = c
    if kind in ("cap", "res"):
        w, h, horizontal = kw["w"], kw["h"], kw["horizontal"]
        if horizontal:
            for dx in (-w / 2 + 1.4, w / 2 - 1.4):
                b.smd_pad((x + dx, y), 3.2, h + 1.2)
            b.claim(x - w / 2 - 1, y - h / 2 - 1, x + w / 2 + 1, y + h / 2 + 1)
        else:
            for dy in (-h / 2 + 1.4, h / 2 - 1.4):
                b.smd_pad((x, y + dy), w + 1.2, 3.2)
            b.claim(x - w / 2 - 1, y - h / 2 - 1, x + w / 2 + 1, y + h / 2 + 1)
    elif kind == "qfn":
        half, n = kw["half"], kw["n"]
        pitch = (2 * half - 6) / max(n - 1, 1)
        for i in range(n):
            o = -half + 3 + i * pitch
            for px, py, pw, ph in ((x + o, y - half, 1.0, 3.0), (x + o, y + half, 1.0, 3.0),
                                   (x - half, y + o, 3.0, 1.0), (x + half, y + o, 3.0, 1.0)):
                b.smd_pad((px, py), pw, ph, 0.2)
        b.claim(x - half - 2, y - half - 2, x + half + 2, y + half + 2)
    elif kind == "box":
        hw, hh = kw["hw"], kw["hh"]
        b.claim(x - hw, y - hh, x + hw, y + hh, kw.get("margin", 2.0))
    b.parts.append((kind, c, kw))


def place_parts(b):
    footprint(b, "qfn", SMASTER, half=SMASTER_HALF, n=14, body="smaster")
    footprint(b, "qfn", PMIC, half=Ms(32) / 2, n=12, body="dark", label=("", ""))
    footprint(b, "qfn", QFN_TOP, half=Ms(28) / 2, n=10, body="dark", label=("", ""))
    footprint(b, "qfn", QFN_MID, half=Ms(22) / 2, n=8, body="dark", label=("", ""))
    footprint(b, "qfn", QFN_BR, half=Ms(18) / 2, n=7, body="dark", label=("", ""))
    footprint(b, "box", SOC, hw=SOC_HALF, hh=SOC_HALF, body="soc")
    footprint(b, "box", MEM, hw=MEM_HALF[0], hh=MEM_HALF[1], body="mem")
    x0, y0, x1, y1 = SD_RECT
    footprint(b, "box", ((x0 + x1) / 2, (y0 + y1) / 2), hw=(x1 - x0) / 2, hh=(y1 - y0) / 2, body="sd")
    for c in XTALS:
        for dx in (-Ms(15) / 2 + 3, Ms(15) / 2 - 3):
            for dy in (-Ms(14) / 2 + 3, Ms(14) / 2 - 3):
                b.smd_pad((c[0] + dx, c[1] + dy), 6, 5, 0.6)
        footprint(b, "box", c, hw=Ms(15) / 2, hh=Ms(14) / 2, body="xtal")
    for c in RINDS:
        for dx in (-Ms(18.5) / 2 + 4, Ms(18.5) / 2 - 4):
            b.smd_pad((c[0] + dx, c[1]), 9, Ms(18.5) * 0.7, 1.0)
        footprint(b, "box", c, hw=Ms(18.5) / 2, hh=Ms(18.5) / 2, body="rind")
    for c in POSCAPS:
        for dx in (-Ms(14) / 2 + 3, Ms(14) / 2 - 3):
            b.smd_pad((c[0] + dx, c[1]), 7, Ms(9.5) * 0.8, 0.6)
        footprint(b, "box", c, hw=Ms(14) / 2, hh=Ms(9.5) / 2, body="poscap", margin=1.0)
    for c in SOT6:
        for dx in (-3.6, 0, 3.6):
            for dy in (-6.2, 6.2):
                b.smd_pad((c[0] + dx, c[1] + dy), 1.8, 3.2, 0.3)
        footprint(b, "box", c, hw=Ms(9) / 2, hh=Ms(7) / 2, body="sot6")
    for c in TESTPADS:
        b.circle(b.d_pad, c, 4.6)
        b.circle(b.d_cu, c, 4.6)
        b.claim(c[0] - 5, c[1] - 5, c[0] + 5, c[1] + 5)
    for c in GOLD_DOTS:
        b.circle(b.d_pad, c, 3.6)
        b.circle(b.d_cu, c, 3.6)
        b.claim(c[0] - 4, c[1] - 4, c[0] + 4, c[1] + 4)
    for c, letter in zip(KF_MARKS, "KF"):
        x, y = c
        b.d_pad.rounded_rectangle([(x - 8) * S, (y - 10) * S, (x + 8) * S, (y + 10) * S], radius=2.5 * S,
                                  outline=255, width=round(1.4 * S))
        f = ImageFont.truetype(MONTSERRAT.format("Bold"), int(12 * S))
        b.d_pad.text((x * S, (y + 0.5) * S), letter, font=f, fill=255, anchor="mm")
        b.claim(x - 9, y - 11, x + 9, y + 11)
    footprint(b, "box", SMALL_IND, hw=Ms(10) / 2, hh=Ms(8) / 2, body="smallind")
    for dx in (-Ms(10) / 2 + 2, Ms(10) / 2 - 2):
        b.smd_pad((SMALL_IND[0] + dx, SMALL_IND[1]), 5, Ms(8) * 0.8, 0.5)


def passive(b, c, horizontal, size=(9, 4.6), kind="cap"):
    w, h = size if horizontal else (size[1], size[0])
    footprint(b, kind, c, w=w, h=h, horizontal=horizontal, size=size)


def route(b):
    sx, sy = SMASTER
    hs = SMASTER_HALF

    # I2S from under the SoC's lower-left corner into the S-Master's right side.
    soc_bottom = SOC[1] + SOC_HALF
    centre = [(313, soc_bottom - 6), (313, 360), (243, 430), (243, 500), (213, 530), (sx + hs + 1, 530)]
    for k in range(4):
        p = offset_polyline(centre, (k - 1.5) * 5.2)
        b.trace(dedupe(p), 1.15, lit=True, phase=0.27 * k)
    for k, d in enumerate((-13, 13)):
        p = offset_polyline(centre[:4] + [(243, 520)], d)
        b.trace(dedupe(p), 1.0)

    # Output: four nets leave the bottom edge, thread the gaps between the
    # inductors and the POSCAPs, and land on the four test pads.
    bottom = sy + hs + 1
    gap_l = (RINDS[0][0] + Ms(18.5) / 2 + POSCAPS[0][0] - Ms(14) / 2) / 2
    gap_c = (POSCAPS[0][0] + POSCAPS[1][0]) / 2
    gap_r = (POSCAPS[1][0] + Ms(14) / 2 + RINDS[1][0] - Ms(18.5) / 2) / 2
    starts = [sx - 20, sx - 6, sx + 21, sx + 27]
    lanes = [gap_l, gap_c, gap_r - 2.3, gap_r + 2.3]
    tp_y = TESTPADS[0][1]
    for k, (x0, lane, tp) in enumerate(zip(starts, lanes, TESTPADS)):
        d1 = abs(lane - x0)
        d2 = abs(tp[0] - lane)
        pts = [(x0, bottom), (x0, bottom + 3), (lane, bottom + 3 + d1), (lane, tp_y - d2), tp]
        b.trace(dedupe(pts), 1.3, lit=True, phase=0.31 * k + 0.4)
    for k, tp in enumerate(TESTPADS[:3]):
        b.trace([tp, (tp[0], H + 12)], 1.3, lit=True, phase=0.31 * k + 0.9)

    # Clocks into the left side, inductors into the bottom corners.
    for k, c in enumerate(XTALS):
        y = sy - 9 + k * 18
        b.trace(dedupe([(c[0] + Ms(15) / 2 - 3, c[1]), (sx - hs - 8, c[1]), (sx - hs - 8, y), (sx - hs - 1, y)]), 1.0)
    for c, sgn in zip(RINDS, (-1, 1)):
        x_in = c[0] - sgn * (Ms(18.5) / 2 - 4)
        b.trace(dedupe([(x_in, c[1] - 8), (x_in, sy + hs - 4), (sx + sgn * (hs + 1), sy + hs - 4)]), 2.4)

    # Power IC down the left edge toward S-Master (Sony's "power pattern").
    px, py = PMIC
    b.trace(dedupe([(px - 20, py + Ms(16) + 1), (px - 20, SD_RECT[1] - 10), (BOARD_EDGE_X + 10, SD_RECT[1] - 10)]), 3.4)
    b.trace(dedupe([(BOARD_EDGE_X + 10, SD_RECT[3] + 8), (BOARD_EDGE_X + 10, 470), (sx - hs - 18, 470),
                    (sx - hs - 18, sy - hs + 6), (sx - hs - 1, sy - hs + 6)]), 3.0)

    # SoC escape: rows of short stubs and vias along its left and bottom edges.
    for i in range(14):
        y = SOC[1] - SOC_HALF + 10 + i * (2 * SOC_HALF - 20) / 13
        x = SOC[0] - SOC_HALF - 1
        L = 5 if i % 2 else 10
        b.trace([(x, y), (x - L, y)], 0.9)
        b.via((x - L - 1.4, y))
    for i in range(10):
        x = SOC[0] - SOC_HALF + 52 + i * 9.5
        y = SOC[1] + SOC_HALF + 1
        L = 6 if i % 2 else 11
        b.trace([(x, y), (x, y + L)], 0.9)
        b.via((x, y + L + 1.4))


def decoupling(b):
    """Hand-placed passives where the photo shows them, then a dense scatter."""
    sx, sy = SMASTER
    hs = SMASTER_HALF
    for k in range(4):
        passive(b, (sx + hs + 9, sy - hs + 6 + k * 9), True, (8, 4.2))
    for k in range(3):
        passive(b, (sx - 14 + k * 14, sy - hs - 9), True, (8, 4.2))
    passive(b, (sx + hs + 22, sy - hs - 2), False, (10, 5.2))
    passive(b, (sx - hs - 20, sy + 2), False, (8, 4.2))
    for k in range(3):
        passive(b, (QFN_MID[0] - 30, QFN_MID[1] - 14 + k * 10), True, (8, 4.2), "res" if k == 1 else "cap")
    for k in range(4):
        passive(b, (SOC[0] - SOC_HALF + 12 + k * 13, SOC[1] + SOC_HALF + 24), True, (8, 4.2))

    clusters = [
        ((255, 385), 26), ((300, 525), 22), ((62, 455), 10), ((215, 470), 8),
        ((350, 690), 14), ((45, 700), 10), ((240, 700), 12), ((420, 625), 10),
        ((215, 200), 8),
    ]
    for (cx, cy), n in clusters:
        tries = 0
        placed = 0
        while placed < n and tries < n * 30:
            tries += 1
            horizontal = rng.random() < 0.55
            big = rng.random() < 0.2
            size = (10, 5.2) if big else (8, 4.2)
            w, h = size if horizontal else (size[1], size[0])
            x = cx + rng.normal(0, 20)
            y = cy + rng.normal(0, 16)
            x = round(x * 2) / 2
            y = round(y * 2) / 2
            if not b.free(x - w / 2, y - h / 2, x + w / 2, y + h / 2, 1.6):
                continue
            kind = "res" if rng.random() < 0.3 else "cap"
            passive(b, (x, y), horizontal, size, kind)
            placed += 1
            if rng.random() < 0.6:
                if horizontal:
                    vx, vy = x + (w / 2 + 3.2) * (1 if rng.random() < 0.5 else -1), y
                else:
                    vx, vy = x, y + (h / 2 + 3.2) * (1 if rng.random() < 0.5 else -1)
                if b.free(vx - 1.6, vy - 1.6, vx + 1.6, vy + 1.6, 0.4):
                    b.via((vx, vy))


def stitch_vias(b):
    for _ in range(260):
        x, y = rng.uniform(BOARD_EDGE_X + 6, W - 4), rng.uniform(4, H - 4)
        if b.free(x - 1.6, y - 1.6, x + 1.6, y + 1.6, 2.5):
            b.via((x, y))


def silkscreen(b):
    sx, sy = SMASTER
    hs = SMASTER_HALF
    b.silk_text((sx - hs, sy + hs + 7.5), "IC1201", 5.0, "lt")
    b.silk_text((XTALS[0][0], XTALS[0][1] - Ms(7) - 2.5), "X1201", 4.6, "ms")
    b.silk_text((XTALS[1][0], XTALS[1][1] + Ms(7) + 7), "X1202", 4.6, "ms")
    b.silk_text((RINDS[0][0], RINDS[0][1] + Ms(9.25) + 7), "L1201", 4.6, "ms")
    b.silk_text((RINDS[1][0], RINDS[1][1] + Ms(9.25) + 7), "L1202", 4.6, "ms")
    for k, tp in enumerate(TESTPADS):
        b.silk_text((tp[0], tp[1] - 7.5), f"{k + 1}", 4.2, "ms")
    b.silk_text((SOC[0] - SOC_HALF, SOC[1] - SOC_HALF - 4), "IC1001", 5.0, "ls")
    b.silk_text((BOARD_EDGE_X + 8, 772), "SONY", 6.4, "ls", font="sans", tracking=1.2)
    b.silk_text((BOARD_EDGE_X + 8, 781), "1-983-155-11", 4.8, "ls", tracking=0.3)


def hires_copper(b, x, y, size):
    """Hi-Res Audio mark etched in exposed ENIG copper; lettering is solder mask."""
    s = size
    box = [x * S, y * S, (x + s) * S, (y + s) * S]
    b.d_pad.rounded_rectangle(box, radius=s * 0.1 * S, fill=255)
    b.d_cu.rounded_rectangle(box, radius=s * 0.1 * S, fill=255)
    b.claim(x, y, x + s, y + s, 3)
    inset, strip = s * 0.085, s * 0.34
    b.d_pad.rectangle([(x + inset) * S, (y + s - inset - strip) * S, (x + s - inset) * S, (y + s - inset) * S], fill=0)
    f1 = None
    for fs in np.arange(s * 0.5, 4, -0.2):
        f1 = ImageFont.truetype(MONTSERRAT.format("Bold"), int(fs * S))
        if b.d_pad.textlength("Hi-Res", font=f1) <= (s - 2 * inset) * 0.86 * S:
            break
    b.d_pad.text(((x + s / 2) * S, (y + inset + (s - strip - 2 * inset) / 2 + s * 0.015) * S), "Hi-Res",
                 font=f1, fill=0, anchor="mm")
    f2 = ImageFont.truetype(MONTSERRAT.format("SemiBold"), int(s * 0.15 * S))
    text = "AUDIO"
    widths = [b.d_pad.textlength(ch, font=f2) for ch in text]
    track = s * 0.035 * S
    total = sum(widths) + track * (len(text) - 1)
    cx = (x + s / 2) * S - total / 2
    for ch, w in zip(text, widths):
        b.d_pad.text((cx, (y + s - inset - strip / 2 + s * 0.01) * S), ch, font=f2, fill=255, anchor="lm")
        cx += w + track


# ---------------------------------------------------------------- materials

class Maps:
    def __init__(self):
        n = (H * S, W * S)
        self.albedo = np.empty(n + (3,), np.float32)
        self.albedo[:] = MASK
        self.height = np.zeros(n, np.float32)
        self.gloss = np.full(n, 1.0, np.float32)
        self.metal = np.zeros(n, np.float32)
        self.shadow = np.zeros(n, np.float32)

    def paint(self, mask, albedo=None, height=None, gloss=None, metal=None):
        m = mask[..., None]
        if albedo is not None:
            self.albedo = self.albedo * (1 - m) + np.asarray(albedo, np.float32) * m
        if height is not None:
            self.height = self.height * (1 - mask) + height * mask
        if gloss is not None:
            self.gloss = self.gloss * (1 - mask) + gloss * mask
        if metal is not None:
            self.metal = self.metal * (1 - mask) + metal * mask


def arr(img):
    return np.asarray(img, np.float32) / 255


def rounded_box_sdf(xx, yy, cx, cy, hw, hh, r):
    qx = np.abs(xx - cx) - (hw - r)
    qy = np.abs(yy - cy) - (hh - r)
    return np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - r


def patch(cx, cy, hw, hh, pad=4):
    x0, x1 = max(0, int((cx - hw - pad) * S)), min(W * S, int((cx + hw + pad) * S))
    y0, y1 = max(0, int((cy - hh - pad) * S)), min(H * S, int((cy + hh + pad) * S))
    yy, xx = np.mgrid[y0:y1, x0:x1].astype(np.float32)
    return (slice(y0, y1), slice(x0, x1)), (xx + 0.5) / S, (yy + 0.5) / S


def put(maps, sl, mask, albedo, height, gloss, metal):
    m = mask[..., None]
    maps.albedo[sl] = maps.albedo[sl] * (1 - m) + albedo * m
    maps.height[sl] = np.maximum(maps.height[sl], height * mask)
    maps.gloss[sl] = maps.gloss[sl] * (1 - mask) + gloss * mask
    maps.metal[sl] = maps.metal[sl] * (1 - mask) + metal * mask
    maps.shadow[sl] = np.maximum(maps.shadow[sl], mask * np.clip(height / 6, 0, 1.4))


def text_mask(sl, text, xy, size, font, anchor="mm", tracking=0.0, angle=0):
    h = sl[0].stop - sl[0].start
    w = sl[1].stop - sl[1].start
    side = int(max(w, h) * 1.5)
    img = Image.new("L", (side, side))
    d = ImageDraw.Draw(img)
    f = ImageFont.truetype(font, int(size * S))
    x, y = side / 2, side / 2
    widths = [d.textlength(ch, font=f) for ch in text]
    total = sum(widths) + tracking * S * (len(text) - 1)
    x -= total / 2
    for ch, wd in zip(text, widths):
        d.text((x, y), ch, font=f, fill=255, anchor="lm")
        x += wd + tracking * S
    if angle:
        img = img.rotate(angle, resample=Image.BICUBIC)
    cx, cy = xy[0] * S - sl[1].start, xy[1] * S - sl[0].start
    out = Image.new("L", (w, h))
    out.paste(img, (int(round(cx - side / 2)), int(round(cy - side / 2))))
    return arr(out)


def engrave(maps, sl, mask, rgb, amount=0.85, depth=0.12):
    maps.albedo[sl] = maps.albedo[sl] * (1 - mask[..., None] * amount) + np.asarray(rgb) * mask[..., None] * amount
    maps.height[sl] += mask * depth


def qfn_part(maps, c, half, body, label=None):
    sl, xx, yy = patch(c[0], c[1], half, half, 3)
    sdf = rounded_box_sdf(xx, yy, c[0], c[1], half - 0.4, half - 0.4, 0.8)
    m = np.clip(0.5 - sdf * S, 0, 1)
    tex = 1 + 0.07 * gaussian_filter(rng.standard_normal(m.shape), 1.0)
    base = np.array([0.075, 0.085, 0.115], np.float32) if body == "smaster" else np.array([0.05, 0.051, 0.055], np.float32)
    put(maps, sl, m, base * tex[..., None], 4.2 * smoothstep(0, 0.9, -sdf), 0.3, 0.0)
    r1 = max(1.2, half * 0.09)
    dot = np.clip(r1 - np.hypot(xx - (c[0] - half + r1 + 2.2), yy - (c[1] - half + r1 + 2.2)), 0, 1) * m
    maps.height[sl] -= dot * 0.8
    engrave(maps, sl, dot, (0.16, 0.17, 0.2), 0.7, 0)
    if body == "smaster":
        lines = [("SONY", 6.6, MONTSERRAT.format("Bold"), 0.8), ("CXD3778GF", 5.6, MONO.format("Medium"), 0.2),
                 ("8B4K701", 4.8, MONO.format("Regular"), 0.2), ("JAPAN", 4.2, MONO.format("Regular"), 0.6)]
        offs = [-15, -5, 4.5, 13]
        for (text, size, font, tr), o in zip(lines, offs):
            t = text_mask(sl, text, (c[0] + o, c[1]), size, font, "mm", tr, angle=90) * m
            engrave(maps, sl, t, (0.30, 0.32, 0.38))
    elif label and label[0]:
        t = text_mask(sl, label[0], (c[0], c[1] - half * 0.18), half * 0.32, MONO.format("Medium"), "mm", 0.2) * m
        engrave(maps, sl, t, (0.20, 0.20, 0.21), 0.7)
        if label[1]:
            t = text_mask(sl, label[1], (c[0], c[1] + half * 0.28), half * 0.26, MONO.format("Regular"), "mm", 0.3) * m
            engrave(maps, sl, t, (0.20, 0.20, 0.21), 0.7)


def bga_part(maps, c, hw, hh, marks):
    sl, xx, yy = patch(c[0], c[1], hw, hh, 3)
    sub = rounded_box_sdf(xx, yy, c[0], c[1], hw, hh, 1.2)
    ms = np.clip(0.5 - sub * S, 0, 1)
    put(maps, sl, ms, np.array([0.10, 0.085, 0.06], np.float32), 2.4 * smoothstep(0, 0.6, -sub), 0.6, 0.0)
    cap = rounded_box_sdf(xx, yy, c[0], c[1], hw - 2.6, hh - 2.6, 1.6)
    mc = np.clip(0.5 - cap * S, 0, 1)
    tex = 1 + 0.06 * gaussian_filter(rng.standard_normal(mc.shape), 1.4)
    put(maps, sl, mc, np.array([0.046, 0.047, 0.05], np.float32) * tex[..., None], 7.5 * smoothstep(0, 2.0, -cap) + 2.4, 0.22, 0.0)
    for text, dx, dy, size, font, tr in marks:
        t = text_mask(sl, text, (c[0] + dx, c[1] + dy), size, font, "mm", tr) * mc
        engrave(maps, sl, t, (0.15, 0.15, 0.16), 0.8)


def sd_slot_part(maps, rect):
    x0, y0, x1, y1 = rect
    cx, cy, hw, hh = (x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2, (y1 - y0) / 2
    sl, xx, yy = patch(cx, cy, hw, hh, 6)
    sdf = rounded_box_sdf(xx, yy, cx, cy, hw, hh, 2.0)
    cage = np.clip(0.5 - sdf * S, 0, 1)
    brushed = 1 + 0.06 * gaussian_filter(rng.standard_normal(cage.shape), (8, 0.5))
    put(maps, sl, cage, np.array([0.25, 0.255, 0.27], np.float32) * brushed[..., None], 13.0 + 1.5 * smoothstep(0, 2, -sdf), 0.75, 1.0)
    for tx in (x0 + hw * 0.55, x0 + hw * 1.45):
        for ty in (y0 - 2.5, y1 + 2.5):
            tab = (np.abs(xx - tx) < 7) & (np.abs(yy - ty) < 3.2)
            put(maps, sl, tab.astype(np.float32), TIN, 2.0, 0.85, 1.0)
    wx0, wx1 = x0 + (x1 - x0) * 0.2, x0 + (x1 - x0) * 0.86
    wy0, wy1 = y0 + (y1 - y0) * 0.18, y0 + (y1 - y0) * 0.88
    win = rounded_box_sdf(xx, yy, (wx0 + wx1) / 2, (wy0 + wy1) / 2, (wx1 - wx0) / 2, (wy1 - wy0) / 2, 2.5)
    mw = np.clip(0.5 - win * S, 0, 1)
    maps.albedo[sl] = maps.albedo[sl] * (1 - mw[..., None]) + np.array([0.035, 0.035, 0.04]) * mw[..., None]
    maps.height[sl] = maps.height[sl] * (1 - mw) + (9.0 + 0.8 * smoothstep(0, 2, -win)) * mw
    maps.metal[sl] *= (1 - mw)
    maps.gloss[sl] = maps.gloss[sl] * (1 - mw) + 0.4 * mw
    plastic = 1 + 0.18 * gaussian_filter(rng.standard_normal(mw.shape), 0.8)
    maps.albedo[sl] *= (1 - mw[..., None] * (1 - plastic[..., None]))
    lengths = [0.56, 0.52, 0.56, 0.50, 0.56, 0.52, 0.56, 0.47]
    for k, frac in enumerate(lengths):
        fy = wy0 + 7 + k * (wy1 - wy0 - 14) / 7
        tip = wx0 + (wx1 - wx0) * frac
        finger = ((np.abs(yy - fy) < 2.5) & (xx > wx0 + 6) & (xx < tip)).astype(np.float32)
        finger *= np.clip((tip - xx) * S * 0.5, 0, 1)
        bend = smoothstep(tip - 16, tip - 4, xx) * (1 - smoothstep(tip - 4, tip, xx))
        put(maps, sl, finger, GOLD * (0.85 + 0.15 * np.cos(np.clip((xx - wx0) / 9, 0, 6.3)))[..., None],
            9.6 + 2.2 * bend, 0.9, 1.0)
        root = ((np.abs(yy - fy) < 3.4) & (xx > wx0 + 2) & (xx < wx0 + 7)).astype(np.float32)
        put(maps, sl, root, np.array([0.05, 0.05, 0.055], np.float32), 10.4, 0.3, 0.0)
    for ry in (wy0 - 4.5, wy1 + 4.5):
        rib = np.exp(-0.5 * ((yy - ry) / 1.1) ** 2) * ((xx > wx0 - 2) & (xx < wx1 + 2)) * cage
        maps.height[sl] += rib * 1.6
    tx = (wx1 + x1) / 2
    for ty in (y0 + (y1 - y0) * 0.30, y0 + (y1 - y0) * 0.62):
        box = rounded_box_sdf(xx, yy, tx, ty, 4.5, 11, 4.5)
        cut = ((np.abs(box) < 0.55) & (yy < ty + 10)).astype(np.float32) * cage
        maps.albedo[sl] = maps.albedo[sl] * (1 - cut[..., None]) + np.array([0.03, 0.03, 0.035]) * cut[..., None]
        maps.height[sl] -= cut * 1.5
        tongue = ((box < -0.55) & (yy < ty + 11)).astype(np.float32) * cage
        maps.height[sl] += tongue * smoothstep(ty + 11, ty - 11, yy) * 1.2
    for k in range(6):
        sy_ = y0 + 14 + k * (y1 - y0 - 28) / 5
        slit = ((np.abs(yy - sy_) < 5) & (np.abs(xx - (x0 + 10)) < 3.2)).astype(np.float32)
        maps.albedo[sl] = maps.albedo[sl] * (1 - slit[..., None]) + np.array([0.04, 0.04, 0.045]) * slit[..., None]
        maps.height[sl] -= slit * 2.0
    for hx, hy in ((x0 + hw * 0.38, y0 + hh * 0.42), (x0 + hw * 0.38, y1 - hh * 0.42)):
        hole = np.clip(4.2 - np.hypot(xx - hx, yy - hy), 0, 1)
        maps.albedo[sl] = maps.albedo[sl] * (1 - hole[..., None]) + np.array([0.02, 0.02, 0.022]) * hole[..., None]
        maps.height[sl] -= hole * 3.0
    hook = ((np.abs(xx - (x1 - 12)) < 1.4) & (yy > y0 + 6) & (yy < y0 + 22)) | \
           ((np.abs(yy - (y0 + 22)) < 1.4) & (xx > x1 - 22) & (xx < x1 - 10.6))
    put(maps, sl, hook.astype(np.float32), GOLD, 15.5, 0.9, 1.0)


def round_inductor_part(maps, c, size):
    half = size / 2
    sl, xx, yy = patch(c[0], c[1], half, half, 3)
    sdf = rounded_box_sdf(xx, yy, c[0], c[1], half, half, 3.0)
    m = np.clip(0.5 - sdf * S, 0, 1)
    put(maps, sl, m, np.array([0.10, 0.10, 0.11], np.float32), 6.0 * smoothstep(0, 1.2, -sdf), 0.4, 0.0)
    r = np.hypot(xx - c[0], yy - c[1])
    dome = np.clip(half * 0.88 - r, 0, 1)
    shade = 1 + 0.25 * (-(xx - c[0]) - (yy - c[1])) / half
    tex = 1 + 0.08 * gaussian_filter(rng.standard_normal(dome.shape), 0.7)
    put(maps, sl, dome, np.array([0.30, 0.30, 0.31], np.float32) * (shade * tex)[..., None],
        6.0 + 3.0 * np.sqrt(np.clip(1 - (r / (half * 0.88)) ** 2, 0, 1)), 0.25, 0.0)
    dot = np.clip(2.6 - r, 0, 1)
    engrave(maps, sl, dot, (0.86, 0.86, 0.84), 1.0, 0.1)


def poscap_part(maps, c, hw, hh):
    sl, xx, yy = patch(c[0], c[1], hw, hh, 3)
    sdf = rounded_box_sdf(xx, yy, c[0], c[1], hw, hh, 0.8)
    m = np.clip(0.5 - sdf * S, 0, 1)
    along = np.abs(xx - c[0])
    term = (along > hw - 3.2).astype(np.float32)
    body = np.array([0.07, 0.07, 0.075], np.float32)
    band = ((xx - (c[0] - hw + 4.8)) > 0) & ((xx - (c[0] - hw + 4.8)) < 2.2)
    alb = body[None, None] * np.ones_like(m)[..., None]
    alb = np.where(band[..., None], np.array([0.30, 0.30, 0.31]), alb)
    alb = alb * (1 - term[..., None]) + TIN * term[..., None]
    put(maps, sl, m, alb.astype(np.float32), 5.0 * smoothstep(0, 1.0, -sdf), 0.35 + 0.5 * term, term)
    t = text_mask(sl, "47", (c[0] + 1.5, c[1]), hh * 1.0, MONO.format("Medium"), "mm", 0.2) * m * (1 - term)
    engrave(maps, sl, t, (0.32, 0.32, 0.33), 0.8)


def passive_part(maps, c, horizontal, size, kind):
    w, h = size
    hw, hh = (w / 2, h / 2) if horizontal else (h / 2, w / 2)
    sl, xx, yy = patch(c[0], c[1], hw, hh, 3)
    sdf = rounded_box_sdf(xx, yy, c[0], c[1], hw, hh, 0.6)
    m = np.clip(0.5 - sdf * S, 0, 1)
    ht = 3.2 * smoothstep(0, 0.8, -sdf)
    along = (xx - c[0]) if horizontal else (yy - c[1])
    half = hw if horizontal else hh
    term = (np.abs(along) > half - min(2.0, half * 0.4)).astype(np.float32)
    body = np.array([0.50, 0.40, 0.27], np.float32) if kind == "cap" else np.array([0.05, 0.05, 0.05], np.float32)
    tone = 1 + 0.12 * (rng.random() - 0.5)
    alb = body[None, None, :] * tone * (1 - term[..., None]) + TIN * term[..., None]
    put(maps, sl, m, alb, ht, 0.55 + 0.35 * term, term)
    if kind == "res":
        edge = ((np.abs((yy - c[1]) if horizontal else (xx - c[0])) > (hh if horizontal else hw) - 0.7)
                & (term < 0.5)).astype(np.float32) * m
        engrave(maps, sl, edge, (0.55, 0.55, 0.53), 0.8, 0)
    fillet = (np.abs(along) > half - 0.2) & (np.abs(along) < half + 1.3)
    lat = np.abs(yy - c[1]) if horizontal else np.abs(xx - c[0])
    lat_half = hh if horizontal else hw
    fm = (fillet & (lat < lat_half + 0.3)).astype(np.float32)
    put(maps, sl, fm, TIN, 1.1 * (1 - np.clip((np.abs(along) - half) / 1.3, 0, 1)), 0.9, 1.0)


def crystal_part(maps, c, hw, hh, label):
    sl, xx, yy = patch(c[0], c[1], hw, hh, 3)
    sdf = rounded_box_sdf(xx, yy, c[0], c[1], hw, hh, 1.4)
    base = np.clip(0.5 - sdf * S, 0, 1)
    put(maps, sl, base, np.array([0.46, 0.38, 0.28], np.float32), 3.6 * smoothstep(0, 0.8, -sdf), 0.4, 0.0)
    # Seam-welded lid with the notched corners seen on the board photo.
    lid = rounded_box_sdf(xx, yy, c[0], c[1], hw - 1.6, hh - 1.6, 1.2)
    notch = np.minimum.reduce([np.hypot(xx - (c[0] + sx * (hw - 1.6)), yy - (c[1] + sy * (hh - 1.6))) - 3.6
                               for sx in (-1, 1) for sy in (-1, 1)])
    mlid = np.clip(0.5 - np.maximum(lid, -notch) * S, 0, 1)
    brushed = 1 + 0.07 * gaussian_filter(rng.standard_normal(mlid.shape), (0.4, 7))
    put(maps, sl, mlid, np.array([0.70, 0.71, 0.73], np.float32) * brushed[..., None],
        4.4 + 0.8 * smoothstep(0, 1.0, -lid), 0.9, 1.0)
    t = text_mask(sl, label, (c[0], c[1]), 5.0, MONO.format("Medium"), "mm", 0.1) * mlid
    engrave(maps, sl, t, (0.30, 0.30, 0.31), 0.7)


def sot6_part(maps, c, hw, hh):
    sl, xx, yy = patch(c[0], c[1], hw, hh, 4)
    for dx in (-3.6, 0, 3.6):
        for sy_ in (-1, 1):
            lead = ((np.abs(xx - (c[0] + dx)) < 0.8) & (np.abs(yy - (c[1] + sy_ * (hh + 1.0))) < 1.8)).astype(np.float32)
            put(maps, sl, lead, TIN, 1.6, 0.85, 1.0)
    sdf = rounded_box_sdf(xx, yy, c[0], c[1], hw, hh, 0.6)
    m = np.clip(0.5 - sdf * S, 0, 1)
    put(maps, sl, m, np.array([0.05, 0.05, 0.055], np.float32), 3.4 * smoothstep(0, 0.7, -sdf), 0.3, 0.0)
    dot = np.clip(1.0 - np.hypot(xx - (c[0] - hw + 2), yy - (c[1] - hh + 2)), 0, 1) * m
    engrave(maps, sl, dot, (0.3, 0.3, 0.3), 0.8, 0)


def small_ind_part(maps, c, hw, hh):
    sl, xx, yy = patch(c[0], c[1], hw, hh, 3)
    sdf = rounded_box_sdf(xx, yy, c[0], c[1], hw, hh, 1.2)
    m = np.clip(0.5 - sdf * S, 0, 1)
    tex = 1 + 0.08 * gaussian_filter(rng.standard_normal(m.shape), 0.6)
    put(maps, sl, m, np.array([0.34, 0.34, 0.35], np.float32) * tex[..., None], 4.2 * smoothstep(0, 1.0, -sdf), 0.3, 0.0)


def parts(maps, b):
    for kind, c, kw in b.parts:
        body = kw.get("body")
        if kind == "qfn":
            qfn_part(maps, c, kw["half"], body, kw.get("label"))
        elif kind in ("cap", "res"):
            passive_part(maps, c, kw["horizontal"], kw["size"], kind)
        elif body == "soc":
            bga_part(maps, c, SOC_HALF, SOC_HALF, [
                ("MEDIATEK", 0, -16, 12, MONTSERRAT.format("SemiBold"), 1.4),
                ("MT8590AAAA", 0, 2, 8.6, MONO.format("Medium"), 0.6),
                ("1813-BCDHL", 0, 15, 7.0, MONO.format("Regular"), 0.4),
                ("TAIWAN", 0, 27, 5.8, MONO.format("Regular"), 1.0),
            ])
            pin1 = np.array([SOC[0] + SOC_HALF - 9, SOC[1] - SOC_HALF + 9])
            sl, xx, yy = patch(*pin1, 3, 3, 1)
            dot = np.clip(2.8 - np.hypot(xx - pin1[0], yy - pin1[1]), 0, 1)
            engrave(maps, sl, dot, (0.85, 0.85, 0.83), 1.0, 0)
        elif body == "mem":
            bga_part(maps, c, MEM_HALF[0], MEM_HALF[1], [])
        elif body == "sd":
            sd_slot_part(maps, SD_RECT)
        elif body == "xtal":
            crystal_part(maps, c, Ms(15) / 2, Ms(14) / 2, "22.5" if c is XTALS[0] else "24.5")
        elif body == "rind":
            round_inductor_part(maps, c, Ms(18.5))
        elif body == "poscap":
            poscap_part(maps, c, Ms(14) / 2, Ms(9.5) / 2)
        elif body == "sot6":
            sot6_part(maps, c, Ms(9) / 2 * 0.62, Ms(7) / 2 * 0.62)
        elif body == "smallind":
            small_ind_part(maps, c, Ms(10) / 2, Ms(8) / 2)


# ---------------------------------------------------------------- light

def emission(b):
    """Lit audio nets: a steady glow plus pulses travelling toward the jack."""
    for pts, w, phase in b.lit:
        pts = np.asarray(pts, float)
        seg = np.linalg.norm(pts[1:] - pts[:-1], axis=1)
        total = 0.0
        for (a, bb), L in zip(zip(pts[:-1], pts[1:]), seg):
            n = max(1, int(L / 0.8))
            for k in range(n):
                s0 = total + L * k / n
                pa = a + (bb - a) * k / n
                pb = a + (bb - a) * (k + 1) / n
                u = s0 / 64.0 - phase
                pulse = np.exp(-0.5 * (((u % 1.0) - 0.5) / 0.09) ** 2)
                b.d_emit.line([tuple(pa * S), tuple(pb * S)], fill=float(0.45 + 0.9 * pulse),
                              width=max(1, round(w * 0.7 * S)))
            total += L


def pour(b, maps):
    cu, pad = arr(b.cu), arr(b.pad)
    features = (cu > 0.5) | (pad > 0.5) | (maps.height > 0.5)
    dist = distance_transform_edt(~features) / S
    return (dist > 1.7).astype(np.float32)


def ramp_lut(n=1024):
    pos = np.array([p for p, _ in RAMP])
    cols = np.stack([hexf(c) for _, c in RAMP])
    t = np.linspace(0, 1, n)
    return np.stack([np.interp(t, pos, cols[:, k]) for k in range(3)], 1).astype(np.float32)


def light(maps, emit):
    h = gaussian_filter(maps.height, 0.7 * S)
    gy, gx = np.gradient(h * S)
    N = np.stack([-gx, -gy, np.ones_like(h)], -1)
    N /= np.linalg.norm(N, axis=-1, keepdims=True)
    Lk = np.array([-0.52, -0.62, 0.58], np.float32)
    Lk /= np.linalg.norm(Lk)
    yy, xx = np.mgrid[0:H * S, 0:W * S].astype(np.float32) / S
    V = np.stack([240 - xx, 300 - yy, np.full_like(xx, 1400.0)], -1)
    V /= np.linalg.norm(V, axis=-1, keepdims=True)
    Hv = Lk + V
    Hv /= np.linalg.norm(Hv, axis=-1, keepdims=True)
    ndl = np.clip((N * Lk).sum(-1), 0, 1)
    ndh = np.clip((N * Hv).sum(-1), 0, 1)
    ndv = np.clip((N * V).sum(-1), 0, 1)

    shadow = gaussian_filter(np.roll(np.roll(maps.shadow, 4 * S, 0), 3 * S, 1), 2.4 * S)
    occl = 1 - 0.75 * np.clip(shadow, 0, 1) * (1 - np.clip(maps.shadow * 4, 0, 1))

    a, g, m = maps.albedo, maps.gloss[..., None], maps.metal[..., None]
    diffuse = a * (0.22 + 0.95 * ndl[..., None]) * (1 - m)
    fres = 0.04 + 0.96 * (1 - ndv) ** 5
    spec_d = (g * (ndh ** 140)[..., None] * 0.22 + (ndh ** 9)[..., None] * 0.05 + fres[..., None] * 0.25) * (1 - m)
    sheen = 0.030 * np.exp(-(((xx - 120) / 260) ** 2 + ((yy - 250) / 300) ** 2)) * maps.gloss
    spec_d = spec_d + (sheen * (0.4 + 0.6 * N[..., 2] ** 40))[..., None] * (1 - m) * np.array([1.0, 0.96, 0.9])
    env = 0.30 + 0.70 * smoothstep(0.55, 1.0, N[..., 2])[..., None] * (0.55 + 0.45 * ndl[..., None])
    spec_m = m * a * (env * 0.72 + (ndh ** 30)[..., None] * 1.8)
    flat = Lk[2]
    edge = np.clip(ndl - flat, 0, 1) * (1 - m[..., 0]) * 0.55
    rgb = (diffuse + spec_d + spec_m + edge[..., None] * np.array([0.95, 0.90, 0.82])) * occl[..., None]

    lit = gaussian_filter(emit, 0.35 * S)
    for sigma, wgt in ((1.0, 0.45), (5, 0.32), (18, 0.26), (60, 0.18)):
        lit = lit + gaussian_filter(emit, sigma * S) * wgt
    t = 1 - np.exp(-1.25 * lit)
    lut = ramp_lut()
    g_rgb = lut[np.clip((t * (len(lut) - 1)).astype(int), 0, len(lut) - 1)]
    rgb = 1 - (1 - rgb) * (1 - g_rgb)
    spill = gaussian_filter(emit, 10 * S)[..., None] * hexf("#d9a24c") * 0.35
    return rgb + maps.albedo * spill * 3.0


def depth_of_field(rgb):
    """Focus sits on the S-Master corner; the SoC softens, the top falls away."""
    yy = (np.arange(H * S) + 0.5) / S
    coc = np.maximum(smoothstep(440, 60, yy) * 2.8, smoothstep(712, 800, yy) * 1.6)
    levels = [0.0, 0.7, 1.4, 2.1, 2.8]
    blurred = [rgb] + [gaussian_filter(rgb, (s * S, s * S, 0)) for s in levels[1:]]
    out = np.zeros_like(rgb)
    for k in range(len(levels)):
        lo = levels[k - 1] if k > 0 else -1
        hi = levels[k + 1] if k + 1 < len(levels) else 99
        wgt = np.where(coc < levels[k], np.clip((coc - lo) / (levels[k] - lo), 0, 1),
                       np.clip((hi - coc) / (hi - levels[k]), 0, 1))
        out += blurred[k] * wgt[:, None, None]
    return out


def vignette(rgb):
    yy, xx = np.mgrid[0:H * S, 0:W * S].astype(np.float32) / S
    r = np.hypot((xx - 200) / 340, (yy - 520) / 480)
    v = 1 - 0.55 * smoothstep(0.55, 1.25, r)
    top = 1 - 0.62 * smoothstep(210, 0, yy)
    return rgb * (v * top)[..., None]


def main():
    b = Board()
    place_parts(b)
    route(b)
    hires_copper(b, 300, 600, 30)
    b.silk_text((315, 646), "WALKMAN", 7.4, "ms", font="sans", tracking=3.2)
    b.claim(268, 590, 362, 654, 4)
    decoupling(b)
    stitch_vias(b)
    silkscreen(b)
    emission(b)

    maps = Maps()
    parts(maps, b)
    plane = pour(b, maps)
    cu = np.maximum(arr(b.cu), plane)
    maps.paint(cu, albedo=COPPER, height=1.0)
    maps.height -= arr(b.dimple) * 0.6
    pad = arr(b.pad)
    maps.paint(pad, albedo=GOLD, height=1.6, gloss=0.9, metal=1.0)
    silk = arr(b.silk)
    maps.paint(silk, albedo=SILK, height=1.4, gloss=0.15, metal=0.0)
    parts(maps, b)
    hole = arr(b.hole)
    maps.paint(hole, albedo=np.zeros(3), height=-2.0, gloss=0.0, metal=0.0)

    xs = (np.arange(W * S) + 0.5) / S
    off = (xs < BOARD_EDGE_X).astype(np.float32)[None, :] * np.ones((H * S, 1), np.float32)
    maps.paint(off, albedo=VOID, height=-4.0, gloss=0.2, metal=0.0)

    yy = (np.arange(H * S) + 0.5) / S
    fade = smoothstep(812, 690, yy)[:, None]
    rgb = light(maps, np.asarray(b.emit, np.float32) * fade)
    rgb = vignette(depth_of_field(rgb))
    img = Image.fromarray((np.clip(rgb, 0, 1) * 255 + 0.5).astype(np.uint8), "RGB").resize((W, H), Image.LANCZOS)
    arr8 = np.asarray(img).astype(np.float32)
    grain = np.random.default_rng(7).normal(0, 1.4, (H, W, 1))
    arr8 = np.clip(arr8 + grain, 0, 255).astype(np.uint8)
    Image.fromarray(arr8, "RGB").save(OUT, "JPEG", quality=88, subsampling=0, optimize=True)
    print(OUT)


if __name__ == "__main__":
    main()
