#!/usr/bin/env python3
"""Convert a raw fbcap capture into a PNG.

    tools/fbcap2png.py panel.raw panel.png [--width 480] [--height 800]
                                        [--order bgra|rgba]

probe/devtools/fbcap.c writes the MTK overlay engine's composed output as raw
32bpp pixels. The channel order is B,G,R,A in memory: the FireOS boot logo is
orange, and reading the capture that way gives R=0xfa G=0xa6 B=0x1b. Use
--order to check that against another guess.

Only stdlib is used, so this runs without a virtualenv.
"""

import argparse
import struct
import sys
import zlib


def chunk(tag: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + tag
        + data
        + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("raw")
    ap.add_argument("png")
    ap.add_argument("--width", type=int, default=480)
    ap.add_argument("--height", type=int, default=800)
    ap.add_argument("--order", choices=("bgra", "rgba"), default="bgra")
    args = ap.parse_args()

    data = open(args.raw, "rb").read()
    need = args.width * args.height * 4
    if len(data) < need:
        print(f"{args.raw}: {len(data)} bytes, need {need}", file=sys.stderr)
        return 1

    rows = bytearray()
    for y in range(args.height):
        rows.append(0)  # PNG filter: none
        base = y * args.width * 4
        for x in range(args.width):
            p = data[base + x * 4 : base + x * 4 + 4]
            if args.order == "bgra":
                b, g, r = p[0], p[1], p[2]
            else:
                r, g, b = p[0], p[1], p[2]
            rows += bytes((r, g, b))

    out = b"\x89PNG\r\n\x1a\n"
    out += chunk(b"IHDR", struct.pack(">IIBBBBB", args.width, args.height, 8, 2, 0, 0, 0))
    out += chunk(b"IDAT", zlib.compress(bytes(rows), 9))
    out += chunk(b"IEND", b"")
    open(args.png, "wb").write(out)
    print(f"{args.png}: {args.width}x{args.height} from {len(data)} bytes, order {args.order}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
