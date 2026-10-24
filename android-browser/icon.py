"""The browser's launcher icon: an orange line globe on WalkmanUI's dark ground, in the
same 44 dp circle and key shadow as WalkmanUI's own (android-ui/launcher_icon.py).

    icon.py RES_DIR    writes RES_DIR/mipmap-<bucket>/ic_launcher.png
"""
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "android-ui"))
from launcher_icon import legacy_icons  # noqa: E402

GROUND = (0x19, 0x1C, 0x21, 255)
ORANGE = (0xFF, 0x66, 0x00, 255)


def globe(side=1024):
    art = Image.new("RGBA", (side, side), GROUND)
    draw = ImageDraw.Draw(art)
    c, r, w = side / 2, side * 0.29, round(side * 0.036)
    box = (c - r, c - r, c + r, c + r)
    draw.ellipse(box, outline=ORANGE, width=w)
    draw.ellipse((c - r * 0.42, c - r, c + r * 0.42, c + r), outline=ORANGE, width=w)
    draw.line((c, c - r, c, c + r), fill=ORANGE, width=w)
    for y in (-0.5, 0.0, 0.5):
        half = r * (1 - y * y) ** 0.5 - w / 2
        draw.line((c - half, c + y * r, c + half, c + y * r), fill=ORANGE, width=w)
    return art


def main():
    res = sys.argv[1]
    for bucket, data in legacy_icons(globe()).items():
        os.makedirs(os.path.join(res, f"mipmap-{bucket}"), exist_ok=True)
        with open(os.path.join(res, f"mipmap-{bucket}", "ic_launcher.png"), "wb") as f:
            f.write(data)


if __name__ == "__main__":
    main()
