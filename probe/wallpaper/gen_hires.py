#!/usr/bin/env python3
"""Hi-Res waveform wallpaper for the NW-A55 launcher (480x800, mdpi).

    gen_hires.py OUT.jpg --fonts DIR

Light is accumulated in a float buffer (filaments splatted bilinearly), bloomed,
then tone-mapped through a single gold ramp so dim light stays amber instead of
going olive. Type is drawn on top at 3x and everything is Lanczos-downsampled.

Only this script is in the repository: the install kit draws the wallpaper on the
owner's computer, in the player's own type. DIR holds Sony's SST faces
(/vendor/sony/lib/fonts, which kit/sony.py takes from the owner's backup).
Needs numpy and Pillow, nothing the kit does not check for already.
"""
import argparse
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

W, H = 480, 800
S = 3

# SST for the words, SST UI for the figures; filled from --fonts.
FONT = {}
FACES = {"bold": "SST-Bold.otf", "roman": "SST-Roman.otf", "figures": "SSTUI-Roman.ttf"}

BAND_Y = 372
PLAYHEAD = 0.618

# Luminance -> colour. Every lit pixel in the band comes from this ramp.
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
BG = "#040405"
ORANGE = "#ff6a00"
EXPOSURE = 2.6

rng = np.random.default_rng(55)


def hexf(h):
    return np.array([int(h[i:i + 2], 16) for i in (1, 3, 5)], np.float32) / 255


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def gauss(x, mu, sd):
    return np.exp(-0.5 * ((x - mu) / sd) ** 2)


def envelope(u):
    """Song-shaped loudness: intro, build, chorus, breakdown under the playhead, chorus, fade."""
    body = (0.26 * gauss(u, 0.15, 0.05)
            + 0.72 * smoothstep(0.17, 0.36, u) * smoothstep(0.60, 0.54, u)
            + 0.30 * gauss(u, 0.45, 0.05)
            + 0.88 * smoothstep(0.645, 0.70, u) * smoothstep(0.93, 0.80, u))
    tail = smoothstep(0.0, 0.12, u) * smoothstep(1.0, 0.90, u)
    return (0.10 + 0.90 * body) * (0.26 + 0.74 * tail)


class Light:
    def __init__(self):
        self.buf = np.zeros((H * S, W * S), np.float64)

    def splat(self, x, y, w):
        """Bilinear splat of samples given in 1x coordinates."""
        px, py = x * S - 0.5, y * S - 0.5
        x0, y0 = np.floor(px).astype(int), np.floor(py).astype(int)
        fx, fy = px - x0, py - y0
        for dx, dy, k in ((0, 0, (1 - fx) * (1 - fy)), (1, 0, fx * (1 - fy)),
                          (0, 1, (1 - fx) * fy), (1, 1, fx * fy)):
            xi, yi = x0 + dx, y0 + dy
            ok = (xi >= 0) & (xi < W * S) & (yi >= 0) & (yi < H * S)
            np.add.at(self.buf, (yi[ok], xi[ok]), (w * k)[ok])


def ribbon(light, strands, amp, turns, phase, fan, lift, gain, played_gain):
    """A flat ribbon of filaments twisting about the band axis.

    Edge-on, the filaments converge into hot knots; face-on they open into a
    sheet. Front filaments and the ribbon's rims carry most of the light.
    """
    xs = np.arange(-8, W + 8, 1 / (S * 4), dtype=np.float64)
    u = xs / W
    env = envelope(u)
    played = np.where(u < PLAYHEAD, 1.0, played_gain)
    yc = BAND_Y + lift * np.sin(2 * np.pi * 0.75 * u + 1.1)
    shimmer = 1 + 0.045 * np.sin(2 * np.pi * 23 * u)
    for j in range(strands):
        s = j / (strands - 1) * 2 - 1
        omega = 2 * np.pi * turns * u + phase + fan * s * np.sin(2 * np.pi * 0.9 * u + 0.4)
        R = amp * env * shimmer
        y = yc + s * R * np.cos(omega)
        z = s * np.sin(omega)
        depth = (z + 1) / 2
        rim = 0.45 + 0.55 * abs(s) ** 4
        slope = np.gradient(y, xs)
        ds = np.sqrt(1 + slope ** 2)
        w = gain * rim * (0.22 + 0.78 * depth ** 2) * played * (0.4 + 0.6 * np.minimum(1, env * 1.6)) * ds
        light.splat(xs, y, w / (S * 4))


def stems(light):
    """Fine vertical sample stems behind the ribbon: the grain of a real waveform."""
    ys = (np.arange(H * S) + 0.5) / S
    for x in range(1, W, 3):
        u = x / W
        peak = 78 * envelope(u) * (0.45 + 0.55 * rng.random())
        played = 1.0 if u < PLAYHEAD else 0.3
        prof = np.clip(1 - np.abs(ys - BAND_Y) / max(peak, 1e-3), 0, 1) ** 1.4
        light.buf[:, x * S:x * S + S] += (prof * 0.075 * played)[:, None]


def dust(buf):
    """Sparse gold motes and a few out-of-focus discs drifting around the band."""
    yy, xx = np.mgrid[0:H * S, 0:W * S].astype(np.float32) / S
    for _ in range(46):
        x = rng.uniform(8, W - 8)
        y = BAND_Y + rng.normal(0, 70)
        r = rng.uniform(0.5, 1.1)
        a = rng.uniform(0.15, 0.9) * np.exp(-((y - BAND_Y) / 160) ** 2)
        x0, x1 = int((x - 4) * S), int((x + 4) * S)
        y0, y1 = int((y - 4) * S), int((y + 4) * S)
        d = np.hypot(xx[y0:y1, x0:x1] - x, yy[y0:y1, x0:x1] - y)
        buf[y0:y1, x0:x1] += np.clip(r - d, 0, 1) * a * 0.9
    for x in (62, 158, 352, 431):
        y = BAND_Y - rng.uniform(75, 200) if rng.random() < 0.75 else BAND_Y + rng.uniform(55, 80)
        r = rng.uniform(9, 16)
        x0, x1 = int((x - r - 2) * S), int((x + r + 2) * S)
        y0, y1 = int((y - r - 2) * S), int((y + r + 2) * S)
        d = np.hypot(xx[y0:y1, x0:x1] - x, yy[y0:y1, x0:x1] - y)
        disc = np.clip((r - d) * 0.8, 0, 1) * (0.55 + 0.45 * smoothstep(r * 0.55, r, d))
        buf[y0:y1, x0:x1] += disc * rng.uniform(0.018, 0.03)


def axis(light):
    xs = (np.arange(W * S) + 0.5) / S
    fade = smoothstep(0, 140, xs) * smoothstep(W, W - 140, xs)
    light.buf[BAND_Y * S - 1:BAND_Y * S + 1, :] += fade * 0.15


def gaussian_filter(a, sigma):
    """scipy.ndimage.gaussian_filter (truncate 4, mode "reflect") in numpy alone, equal to it
    to rounding, so the owner's computer needs no scipy."""
    r = int(4.0 * sigma + 0.5)
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2)
    k /= k.sum()
    out = a
    for axis in range(a.ndim):
        pad = [(0, 0)] * a.ndim
        pad[axis] = (r, r)
        padded = np.pad(out, pad, mode="symmetric")
        n = out.shape[axis]
        acc = np.zeros(out.shape, np.float64)
        for i, w in enumerate(k):
            acc += w * np.take(padded, np.arange(i, i + n), axis=axis)
        out = acc
    return out


def bloom(buf, passes):
    out = buf.copy() * passes[0]
    for sigma, weight in passes[1:]:
        if sigma * S <= 12:
            out += gaussian_filter(buf, sigma * S) * weight
        else:
            small = buf.reshape(H, S, W, S).mean(axis=(1, 3))
            blurred = gaussian_filter(small, sigma)
            up = np.asarray(Image.fromarray(blurred.astype(np.float32), "F").resize((W * S, H * S), Image.BICUBIC))
            out += up * weight
    return out


def ramp_lut(n=1024):
    pos = np.array([p for p, _ in RAMP])
    cols = np.stack([hexf(c) for _, c in RAMP])
    t = np.linspace(0, 1, n)
    return np.stack([np.interp(t, pos, cols[:, k]) for k in range(3)], 1).astype(np.float32)


def halo():
    y, x = np.mgrid[0:H * S, 0:W * S].astype(np.float32) / S
    return 0.035 * np.exp(-(((x - W / 2) / 300) ** 2 + ((y - BAND_Y) / 135) ** 2))


def playhead_layer():
    buf = np.zeros((H * S, W * S), np.float32)
    x = int(round(PLAYHEAD * W)) * S
    ys = (np.arange(H * S) + 0.5) / S
    span = smoothstep(BAND_Y - 120, BAND_Y - 96, ys) * smoothstep(BAND_Y + 122, BAND_Y + 92, ys)
    buf[:, x:x + S] += span[:, None] * 0.75
    yy, xx = np.mgrid[0:H * S, 0:W * S].astype(np.float32) / S
    dot = np.clip(3.4 - np.hypot(xx - (x / S + 0.5), yy - (BAND_Y - 106)), 0, 1)
    return buf + dot * 1.4


def tracked(draw, center, text, font, tracking, fill, anchor_y="s"):
    widths = [draw.textlength(c, font=font) for c in text]
    total = sum(widths) + tracking * (len(text) - 1)
    x = center[0] - total / 2
    for c, w in zip(text, widths):
        draw.text((x, center[1]), c, font=font, fill=fill, anchor="l" + anchor_y)
        x += w + tracking


def fit_font(draw, text, path, width):
    size = 400
    while size > 4:
        font = ImageFont.truetype(path, size)
        if draw.textlength(text, font=font) <= width:
            return font
        size -= 2
    return ImageFont.truetype(path, size)


def hires_mark(size):
    """Hi-Res Audio style badge: gold field above a black AUDIO strip."""
    k = 4
    s = size * S * k
    yy, xx = np.mgrid[0:s, 0:s].astype(np.float32) / s
    t = np.clip(yy * 0.85 + xx * 0.25, 0, 1)[..., None]
    stops = [(0.0, hexf("#f4dea4")), (0.55, hexf("#d4ad5e")), (1.0, hexf("#a87d33"))]
    field = np.where(t < 0.55, stops[0][1] + (stops[1][1] - stops[0][1]) * (t / 0.55),
                     stops[1][1] + (stops[2][1] - stops[1][1]) * ((t - 0.55) / 0.45))
    rgba = np.concatenate([field * 255, np.full((s, s, 1), 255, np.float32)], 2).astype(np.uint8)
    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, s - 1, s - 1], radius=int(s * 0.1), fill=255)
    badge = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    badge.paste(Image.fromarray(rgba, "RGBA"), (0, 0), mask)
    d = ImageDraw.Draw(badge)
    inset, strip = s * 0.085, s * 0.34
    d.rectangle([inset, s - inset - strip, s - inset, s - inset], fill=(11, 9, 7, 255))
    f1 = fit_font(d, "Hi-Res", FONT["bold"], (s - 2 * inset) * 0.86)
    d.text((s / 2, inset + (s - strip - 2 * inset) / 2 + s * 0.015), "Hi-Res", font=f1,
           fill=(21, 16, 10, 255), anchor="mm")
    f2 = ImageFont.truetype(FONT["bold"], int(s * 0.15))
    tracked(d, (s / 2, s - inset - strip / 2 + s * 0.01), "AUDIO", f2, s * 0.035, (236, 211, 148, 255), "m")
    return badge.resize((size * S, size * S), Image.LANCZOS)


def typography(img):
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    gold = (200, 162, 75)

    for i, x in enumerate(range(40, 441, 5)):
        major, mid = i % 16 == 0, i % 4 == 0
        h = 7 if major else 4 if mid else 2
        a = 128 if major else 76 if mid else 40
        d.rectangle([x * S, (BAND_Y + 100) * S, x * S + S - 1, (BAND_Y + 100 + h) * S], fill=gold + (a,))

    mono = ImageFont.truetype(FONT["figures"], int(9.5 * S))
    d.text((40 * S, (BAND_Y + 124) * S), "NW-A55", font=mono, fill=gold + (140,), anchor="ls")
    d.text((440 * S, (BAND_Y + 124) * S), "24 bit / 192 kHz", font=mono, fill=gold + (140,), anchor="rs")
    d.text(((round(PLAYHEAD * W) + 0.5) * S, (BAND_Y - 116) * S), "02:47", font=mono,
           fill=(255, 140, 60, 230), anchor="ms")

    word = ImageFont.truetype(FONT["roman"], 14 * S)
    tracked(d, (W / 2 * S, (BAND_Y + 158) * S), "WALKMAN", word, 8.5 * S, (228, 220, 204, 236))

    layer.alpha_composite(hires_mark(30), (int((W / 2 - 15) * S), int((BAND_Y + 176) * S)))
    img.alpha_composite(layer)
    return img


def main():
    ap = argparse.ArgumentParser(description="the Hi-Res waveform wallpaper, 480x800 JPEG")
    ap.add_argument("out", type=Path)
    ap.add_argument("--fonts", type=Path, required=True, help="the player's /vendor/sony/lib/fonts")
    a = ap.parse_args()
    for role, name in FACES.items():
        FONT[role] = str(a.fonts / name)
        if not Path(FONT[role]).is_file():
            raise SystemExit(f"no {name} in {a.fonts}")

    light = Light()
    stems(light)
    ribbon(light, strands=170, amp=94, turns=1.3, phase=0.95, fan=0.5, lift=14, gain=0.16, played_gain=0.3)
    ribbon(light, strands=56, amp=52, turns=-0.85, phase=2.3, fan=0.8, lift=-9, gain=0.11, played_gain=0.3)
    axis(light)

    core = light.buf / np.percentile(light.buf, 99.9)
    dust(core)
    lit = bloom(core, [1.0, (0.6, 0.5), (4, 0.30), (16, 0.16), (55, 0.10)]) + halo()
    t = 1 - np.exp(-EXPOSURE * lit)
    lut = ramp_lut()
    rgb = lut[np.clip((t * (len(lut) - 1)).astype(int), 0, len(lut) - 1)]
    rgb = hexf(BG) + rgb * (1 - hexf(BG))

    ph = bloom(playhead_layer(), [1.0, (1.0, 0.35), (5, 0.22), (18, 0.16)])
    lit_ph = np.clip(ph, 0, 1)[..., None]
    rgb = rgb * (1 - lit_ph * 0.85) + hexf(ORANGE) * lit_ph

    img = Image.fromarray((np.clip(rgb, 0, 1) * 255 + 0.5).astype(np.uint8), "RGB").convert("RGBA")
    img = typography(img).convert("RGB").resize((W, H), Image.LANCZOS)

    arr = np.asarray(img).astype(np.float32)
    grain = np.random.default_rng(7).normal(0, 1.3, (H, W, 1))
    arr = np.clip(arr + grain, 0, 255).astype(np.uint8)
    Image.fromarray(arr, "RGB").save(a.out, "JPEG", quality=88, subsampling=0, optimize=True)
    print(f"{a.out.name}: {a.out.stat().st_size} bytes")


if __name__ == "__main__":
    main()
