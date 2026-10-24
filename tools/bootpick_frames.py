#!/usr/bin/env python3
"""Render the dual-boot picker's screens for probe/devtools/bootpick.c.

    bootpick_frames.py OUT [--png DIR] [--sony DIR]

OUT holds four 480x800 frames after a small header (little-endian):

    "NWPK" u16 width, u16 height, u16 frames, u16 0
    per frame: u16 countdown area x, y, w, h; u8 r, g, b of its line, u8 line
    height; 4 bytes 0
    then each frame's pixels, B G R A, row by row (the panel's own order)

Frames: 0 Walkman selected, 1 Android selected, 2 starting Walkman,
3 starting Android. The countdown area holds the "Starts automatically" label
and, along its bottom, the line bootpick drains while no key has been pressed;
the first key clears the area. The starting frames have none (w = 0).

The screen is the stock UI's radio settings list (SettingItemRadio: 84 px
rows, title at x 20, ScreenSettings.json's sizes and greys), black and white
only. It is drawn with the owner's own firmware where the repo's work/ has it,
or the folder kit/sony.py made from their backup (--sony): the SST UI font and
the radio and pressed-row images, staged at build time and never committed.
Without them the same shapes are drawn with Noto Sans.
"""
import argparse
import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
SONY_FONTS = ROOT / "work/device/system/vendor/sony/lib/fonts"
SONY_QRC = ROOT / "work/qrc-device"
FREE_FONT = "/usr/share/fonts/google-noto-vf/NotoSans[wght].ttf"

W, H = 480, 800
# Drawn at K times the size and box-filtered down: text gets Qt-like
# unhinted anti-aliasing (FreeType's hinting at 26 px breaks SST Light's
# "m"), while the stock images, scaled up by pixel repetition, come back
# exactly as they are.
K = 4

# ScreenSettings.json, style "normal": bgcolor D1, textcolor L1 / L2 / L3,
# textsize L 26 / S 22; the header rule and row separators as the stock
# list draws them.
BG = (0, 0, 0)
L1 = (255, 255, 255)
L2 = (152, 152, 152)
L3 = (102, 102, 102)
RULE = (153, 153, 153)
SEPARATOR = (51, 51, 51)
PRESSED = (51, 51, 51)

HEADER_H = 88
ROW_H = 84
ROW_Y = (HEADER_H + 2, HEADER_H + 2 + ROW_H + 1)
BAR = (20, 660, W - 40, 2)
COUNTDOWN = (20, 620, W - 40, BAR[1] + BAR[3] - 620)

OPTIONS = [("Walkman", "Sony firmware"), ("Android", "Android 5.1")]


def sony_font(weight):
    path = SONY_FONTS / f"SSTUI-{weight}.ttf"
    return path if path.is_file() else None


def font(weight, size):
    path = sony_font(weight)
    if path:
        return ImageFont.truetype(str(path), size * K)
    fnt = ImageFont.truetype(FREE_FONT, size * K)
    fnt.set_variation_by_axes([{"Light": 300, "Roman": 400, "Bold": 700}[weight]])
    return fnt


def sony_image(name):
    path = SONY_QRC / f"{name}.png"
    if not path.is_file():
        return None
    img = Image.open(path).convert("RGBA")
    return img.resize((img.width * K, img.height * K), Image.NEAREST)


def k(*v):
    return tuple(round(x * K) for x in v)


def rect(d, x0, y0, x1, y1, fill):
    """Fill [x0, x1) x [y0, y1); PIL's rectangle includes its end corner."""
    d.rectangle((round(x0 * K), round(y0 * K), round(x1 * K) - 1, round(y1 * K) - 1), fill=fill)


def radio(on):
    img = sony_image(f"btn_radio_{'on' if on else 'off'}_normal_88")
    if img:
        return img
    img = Image.new("RGBA", k(88, 88), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse(k(29, 29, 59, 59), outline=(200, 200, 200, 255), width=round(1.5 * K))
    if on:
        d.ellipse(k(35, 35, 53, 53), fill=L1 + (255,))
    return img


def play_pause(d, x, y, color):
    d.polygon(k(x, y - 15, x + 12, y - 8, x, y - 1), fill=color)
    rect(d, x + 16, y - 15, x + 17.5, y - 1, color)
    rect(d, x + 21, y - 15, x + 22.5, y - 1, color)


def frame(selected, starting):
    img = Image.new("RGBA", k(W, H), BG + (255,))
    d = ImageDraw.Draw(img)
    title, label, note = font("Light", 26), font("Light", 26), font("Light", 22)

    d.text(k(20, HEADER_H / 2), "Select System", font=title, fill=L1, anchor="lm")
    rect(d, 0, HEADER_H, W, HEADER_H + 2, RULE)

    pressed = sony_image("list_full_pressed")
    for i, (name, desc) in enumerate(OPTIONS):
        y = ROW_Y[i]
        on = i == selected
        if on:
            if pressed:
                img.alpha_composite(pressed, k(0, y))
            else:
                rect(d, 0, y, W, y + ROW_H, PRESSED)
        dim = starting and not on
        d.text(k(20, y + 12), name, font=label, fill=L3 if dim else L1)
        d.text(k(20, y + 47), desc, font=note, fill=L3 if dim else L2)
        img.alpha_composite(radio(on), k(W - 88, y + (ROW_H - 88) // 2))
        rect(d, 0, y + ROW_H, W, y + ROW_H + 1, SEPARATOR)

    hint = font("Light", 22)
    if starting:
        d.text(k(20, BAR[1] - 8), f"Starting {OPTIONS[selected][0]}…", font=hint, fill=L2, anchor="ls")
    else:
        d.text(k(20, BAR[1] - 12), "Starts automatically", font=hint, fill=L2, anchor="ls")
        rect(d, BAR[0], BAR[1], BAR[0] + BAR[2], BAR[1] + BAR[3], SEPARATOR)
        d.text(k(20, 712), "VOL  + / −", font=hint, fill=L1, anchor="ls")
        d.text(k(240, 712), "Move", font=hint, fill=L2, anchor="ls")
        play_pause(d, 21, 748, L1)
        d.text(k(57, 748), "/  POWER", font=hint, fill=L1, anchor="ls")
        d.text(k(240, 748), "Start", font=hint, fill=L2, anchor="ls")

    area = (0, 0, 0, 0) if starting else COUNTDOWN
    return img.resize((W, H), Image.BOX), area


def main():
    global SONY_FONTS, SONY_QRC
    parser = argparse.ArgumentParser()
    parser.add_argument("out")
    parser.add_argument("--png", type=Path, help="also save each frame as a PNG here")
    parser.add_argument("--sony", type=Path, help="kit/sony.py's folder, instead of work/")
    args = parser.parse_args()
    if args.sony:
        SONY_FONTS, SONY_QRC = args.sony / "system/vendor/sony/lib/fonts", args.sony / "qrc"
    frames = [frame(0, False), frame(1, False), frame(0, True), frame(1, True)]
    data = bytearray(b"NWPK" + struct.pack("<HHHH", W, H, len(frames), 0))
    for _, area in frames:
        data += struct.pack("<HHHHBBBB4x", *area, *L1, BAR[3])
    for img, _ in frames:
        data += img.tobytes("raw", "BGRA")
    Path(args.out).write_bytes(data)
    if args.png:
        args.png.mkdir(parents=True, exist_ok=True)
        for i, (img, _) in enumerate(frames):
            img.save(args.png / f"bootpick-{i}.png")
    source = "SST UI and the stock images" if sony_font("Light") else "Noto Sans, drawn radios"
    print(f"{args.out}: {len(frames)} frames, {len(data)} bytes ({source})")


if __name__ == "__main__":
    main()
