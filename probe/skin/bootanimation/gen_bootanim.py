#!/usr/bin/env python3
"""Convert a stock NW-A55 boot animation into an Android 5.1 bootanimation.zip.

Input is the device's own `/system/media/bootanimation.zip` (pull it from your
Walkman or your firmware dump; this project does not ship Sony's assets).

Sony's icx_bootanimation format: desc.txt lines are "<count> <first> <last>"
per part (count 0 = loop), frames are 16-bit BMPs, 480x200 strips drawn
centred (y=300) on the 480x800 panel. Some builds open with one full 480x800
frame whose centre strip is pixel-identical to the next part's first frame.
Android's BootAnimation also centres a WxH animation, so strips carry over.

Output matches what CM12.1 ships: palette PNGs in a STORED zip, finite parts
as "c" (always play to the end), the looping part as "p". Entries carry a
fixed timestamp, so the same input always gives the same bytes.

usage: gen_bootanim.py SONY_BOOTANIMATION_ZIP OUT_ZIP [--gold]
"""
import argparse
import io
import zipfile

import numpy as np
from PIL import Image

W, H, FPS = 480, 200, 30
STRIP_Y = 300
COLORS = 96
HOLD_STILL = 24
STAMP = (1980, 1, 1, 0, 0, 0)

# Luminance -> Hi-Res gold, the ramp the wallpapers use.
GOLD_RAMP = [
    (0.00, "#000000"),
    (0.20, "#55300c"),
    (0.45, "#b07a2c"),
    (0.70, "#e2b765"),
    (0.88, "#f6dca3"),
    (1.00, "#fff8ea"),
]


def hexf(h):
    return np.array([int(h[i:i + 2], 16) for i in (1, 3, 5)], np.float32) / 255


def gold(img):
    lum = np.asarray(img.convert("L"), np.float32) / 255
    pos = np.array([p for p, _ in GOLD_RAMP])
    cols = np.stack([hexf(c) for _, c in GOLD_RAMP])
    rgb = np.stack([np.interp(lum, pos, cols[:, k]) for k in range(3)], -1)
    return Image.fromarray((rgb * 255 + 0.5).astype(np.uint8), "RGB")


def sony_parts(zf):
    """[(count, frame_names)] in play order, from Sony's desc.txt."""
    parts = []
    for k, line in enumerate(l for l in zf.read("desc.txt").decode().splitlines() if l.strip()):
        count, first, last = (int(v) for v in line.split())
        names = [f"part{k}/boot_{i:04d}.bmp" for i in range(first, last + 1)]
        missing = [n for n in names if n not in zf.namelist()]
        if missing:
            raise SystemExit(f"desc.txt line {k + 1} names frames the zip lacks: {missing[:3]}")
        parts.append((count, names))
    return parts


def frame(zf, name):
    im = Image.open(io.BytesIO(zf.read(name))).convert("RGB")
    if im.size == (480, 800):
        im = im.crop((0, STRIP_Y, W, STRIP_Y + H))
    if im.size != (W, H):
        raise SystemExit(f"{name}: unexpected frame size {im.size}")
    return im


def png(im):
    buf = io.BytesIO()
    im.quantize(COLORS, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).save(buf, "PNG", optimize=True)
    return buf.getvalue()


def write(z, name, data):
    info = zipfile.ZipInfo(name, STAMP)
    info.compress_type = zipfile.ZIP_STORED
    info.external_attr = 0o644 << 16
    z.writestr(info, data)


def convert(src, out, tint=None):
    zf = zipfile.ZipFile(src)
    lines = [f"{W} {H} {FPS}"]
    with zipfile.ZipFile(out, "w", zipfile.ZIP_STORED) as z:
        for k, (count, names) in enumerate(sony_parts(zf)):
            kind = "p" if count == 0 else "c"
            pause = HOLD_STILL if len(names) == 1 and count > 0 else 0
            lines.append(f"{kind} {count} {pause} part{k}")
            for i, n in enumerate(names):
                im = frame(zf, n)
                write(z, f"part{k}/{i:04d}.png", png(tint(im) if tint else im))
        write(z, "desc.txt", "\n".join(lines) + "\n")
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("src", help="stock /system/media/bootanimation.zip from your own NW-A55")
    ap.add_argument("out", help="Android bootanimation.zip to write")
    ap.add_argument("--gold", action="store_true", help="map luminance onto the Hi-Res gold ramp")
    args = ap.parse_args()
    lines = convert(args.src, args.out, gold if args.gold else None)
    print(args.out, "|", " / ".join(lines))


if __name__ == "__main__":
    main()
