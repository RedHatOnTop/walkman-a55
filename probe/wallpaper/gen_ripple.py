#!/usr/bin/env python3
"""Hi-Res Ripple wallpaper for the NW-A55 launcher (480x800, mdpi).

A black liquid surface seen through a pitched perspective camera. Concentric
ripples carry an audio-like signal outward from a drop; a gold line light and
a soft gold box above the far side are reflected off the ripple slopes. The
surface is ray-cast per pixel at 3x, shaded with Schlick Fresnel, tone-mapped
through one gold ramp and Lanczos-downsampled.
"""
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy.ndimage import gaussian_filter

W, H = 480, 800
S = 3
OUT = Path(__file__).with_name("hires-ripple-480x800.jpg")

MONTSERRAT = "/usr/share/fonts/julietaula-montserrat-fonts/Montserrat-{}.otf"
MONO = str(Path.home() / ".local/share/fonts/jetbrains-mono/JetBrainsMono-{}.ttf")

CAM_H = 10.0
PITCH = np.radians(36)
FOV = np.radians(52)
CENTER_Y = 418
SCALE = 2.9

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
BG = "#030304"
EXPOSURE = 2.4


def hexf(h):
    return np.array([int(h[i:i + 2], 16) for i in (1, 3, 5)], np.float32) / 255


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def window(x, a, b, soft):
    return smoothstep(a - soft, a + soft, x) * smoothstep(b + soft, b - soft, x)


def rays(rows, sub):
    """View rays for the given 3x rows, offset by sub-pixel fractions in y."""
    yy, xx = np.meshgrid(rows + sub, np.arange(W * S, dtype=np.float64), indexing="ij")
    th = np.tan(FOV / 2)
    xc = (xx + 0.5 - W * S / 2) / (H * S / 2) * th
    yc = (H * S / 2 - yy - 0.5) / (H * S / 2) * th
    sp, cp = np.sin(PITCH), np.cos(PITCH)
    d = np.stack([xc, yc * sp + cp, yc * cp - sp], -1)
    d /= np.linalg.norm(d, axis=-1, keepdims=True)
    return d


def ground_point(d):
    t = CAM_H / -d[..., 2]
    return d[..., 0] * t, d[..., 1] * t, t


def signal(r):
    """Ripple height along the radius: a decaying train that reads like a waveform.

    Packets nearer the drop are tighter and steeper; the outer ones left earlier
    and have spread and softened.
    """
    packets = [
        # (radius, width, wavelength, amplitude)
        (0.95, 0.30, 0.46, 0.020),
        (1.85, 0.42, 0.52, 0.024),
        (2.90, 0.40, 0.58, 0.018),
        (3.90, 0.55, 0.66, 0.017),
        (5.10, 0.60, 0.76, 0.012),
    ]
    h = np.zeros_like(r)
    for r0, w, lam, a in packets:
        env = np.exp(-0.5 * ((r - r0) / w) ** 2)
        h += a * env * np.cos(2 * np.pi * (r - r0) / lam)
    crown = 0.035 * np.exp(-0.5 * (r / 0.06) ** 2) - 0.012 * np.exp(-0.5 * ((r - 0.16) / 0.06) ** 2)
    return (h + crown) * smoothstep(6.6, 5.6, r)


def environment(R):
    """Backlight: a gold line light just above the far horizon, a wide warm glow
    around it, and a faint overhead fill. Flat water never sees any of them."""
    elev = np.degrees(np.arcsin(np.clip(R[..., 2], -1, 1)))
    azim = np.degrees(np.arctan2(R[..., 0], R[..., 1]))
    line = window(elev, 2.75, 3.25, 0.3) * window(azim, -48, 48, 14) * 7.5
    glow = window(elev, 0.0, 6.0, 2.5) * np.exp(-(azim / 40) ** 2) * 0.55
    fill = window(elev, 52, 80, 6) * np.exp(-(azim / 70) ** 2) * 0.2
    return line + glow + fill


def shade(d, cx, cy):
    X, Y, t = ground_point(d)
    dx, dy = X - cx, Y - cy
    r = np.hypot(dx, dy) + 1e-6
    eps = 1e-3
    dhdr = (signal((r + eps) / SCALE) - signal((r - eps) / SCALE)) * SCALE / (2 * eps)
    nx, ny = -dhdr * dx / r, -dhdr * dy / r
    N = np.stack([nx, ny, np.ones_like(nx)], -1)
    N /= np.linalg.norm(N, axis=-1, keepdims=True)
    cos_i = np.clip(-(d * N).sum(-1), 0, 1)
    R = d + 2 * cos_i[..., None] * N
    fresnel = 0.035 + 0.965 * (1 - cos_i) ** 5
    return fresnel * environment(R)


def surface(vsub=4, chunk=160):
    """Shade the surface with vsub extra samples per 3x row; the reflected line
    light is thinner than a pixel on the steep crests and beads without them."""
    cx, cy, _ = ground_point(rays_at(W * S / 2, CENTER_Y * S))
    lum = np.zeros((H * S, W * S))
    for y0 in range(0, H * S, chunk):
        rows = np.arange(y0, min(y0 + chunk, H * S), dtype=np.float64)
        acc = np.zeros((len(rows), W * S))
        for k in range(vsub):
            acc += shade(rays(rows, (k + 0.5) / vsub - 0.5), cx, cy)
        lum[y0:y0 + len(rows)] = acc / vsub
    return lum


def rays_at(px, py):
    th = np.tan(FOV / 2)
    xc = (px + 0.5 - W * S / 2) / (H * S / 2) * th
    yc = (H * S / 2 - py - 0.5) / (H * S / 2) * th
    sp, cp = np.sin(PITCH), np.cos(PITCH)
    d = np.array([xc, yc * sp + cp, yc * cp - sp])
    return d / np.linalg.norm(d)


def ramp_lut(n=1024):
    pos = np.array([p for p, _ in RAMP])
    cols = np.stack([hexf(c) for _, c in RAMP])
    t = np.linspace(0, 1, n)
    return np.stack([np.interp(t, pos, cols[:, k]) for k in range(3)], 1).astype(np.float32)


def bloom(buf, passes):
    out = buf * passes[0]
    for sigma, weight in passes[1:]:
        out = out + gaussian_filter(buf, sigma * S) * weight
    return out


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
    s = size * S * 4
    yy, xx = np.mgrid[0:s, 0:s].astype(np.float32) / s
    t = np.clip(yy * 0.85 + xx * 0.25, 0, 1)[..., None]
    a, b, c = hexf("#f4dea4"), hexf("#d4ad5e"), hexf("#a87d33")
    field = np.where(t < 0.55, a + (b - a) * (t / 0.55), b + (c - b) * ((t - 0.55) / 0.45))
    rgba = np.concatenate([field * 255, np.full((s, s, 1), 255, np.float32)], 2).astype(np.uint8)
    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, s - 1, s - 1], radius=int(s * 0.1), fill=255)
    badge = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    badge.paste(Image.fromarray(rgba, "RGBA"), (0, 0), mask)
    d = ImageDraw.Draw(badge)
    inset, strip = s * 0.085, s * 0.34
    d.rectangle([inset, s - inset - strip, s - inset, s - inset], fill=(11, 9, 7, 255))
    f1 = fit_font(d, "Hi-Res", MONTSERRAT.format("Bold"), (s - 2 * inset) * 0.86)
    d.text((s / 2, inset + (s - strip - 2 * inset) / 2 + s * 0.015), "Hi-Res", font=f1,
           fill=(21, 16, 10, 255), anchor="mm")
    f2 = ImageFont.truetype(MONTSERRAT.format("SemiBold"), int(s * 0.15))
    tracked(d, (s / 2, s - inset - strip / 2 + s * 0.01), "AUDIO", f2, s * 0.035, (236, 211, 148, 255), "m")
    return badge.resize((size * S, size * S), Image.LANCZOS)


def typography(img):
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    word = ImageFont.truetype(MONTSERRAT.format("SemiBold"), 14 * S)
    tracked(d, (W / 2 * S, 646 * S), "WALKMAN", word, 8.5 * S, (228, 220, 204, 230))
    mono = ImageFont.truetype(MONO.format("Regular"), int(8.5 * S))
    tracked(d, (W / 2 * S, 664 * S), "HIGH-RESOLUTION AUDIO", mono, 2.2 * S, (200, 162, 75, 120))
    layer.alpha_composite(hires_mark(26), (int((W / 2 - 13) * S), int(588 * S)))
    img.alpha_composite(layer)
    return img


def main():
    lum = surface()
    lit = bloom(lum, [1.0, (0.8, 0.35), (5, 0.22), (22, 0.18)])
    t = 1 - np.exp(-EXPOSURE * lit)
    for y in (60, 200, 300, 392, 480, 600, 720):
        row = t[y * S]
        print(f"y={y:3d}  max={row.max():.3f}  mean={row.mean():.3f}")
    lut = ramp_lut()
    rgb = lut[np.clip((t * (len(lut) - 1)).astype(int), 0, len(lut) - 1)]
    rgb = hexf(BG) + rgb * (1 - hexf(BG))
    img = Image.fromarray((np.clip(rgb, 0, 1) * 255 + 0.5).astype(np.uint8), "RGB").convert("RGBA")
    img = typography(img).convert("RGB").resize((W, H), Image.LANCZOS)

    arr = np.asarray(img).astype(np.float32)
    grain = np.random.default_rng(7).normal(0, 1.3, (H, W, 1))
    arr = np.clip(arr + grain, 0, 255).astype(np.uint8)
    Image.fromarray(arr, "RGB").save(OUT, "JPEG", quality=88, subsampling=0, optimize=True)
    print(OUT)


if __name__ == "__main__":
    main()
