"""WalkmanUI's launcher icon as the legacy PNGs Android 5.1 shows, one per density.

    legacy_icons(art)    {bucket: PNG}: art masked to a circle at the 44 dp keyline of
                         a 48 dp icon, over a soft key shadow
    placeholder()        {bucket: PNG}: this repository's own icon, what a release
                         APK carries
    walkman_mark(logo)   art: the W. mark of the player's own shutdown logo
                         (sys_shutdown_logo.png), white on its orange

stage.py draws the development APK's icon from the ZX507's Walkman app; the install
kit draws the owner's from walkman_mark() and puts it over the placeholder in the
APK it writes into the system image. The PNGs land in the APK as
res/drawable-<bucket>-v4/ic_launcher.png.
"""
import io

from PIL import Image, ImageDraw, ImageFilter

DENSITIES = {"mdpi": 1, "hdpi": 1.5, "xhdpi": 2, "xxhdpi": 3, "xxxhdpi": 4}
SUPERSAMPLE = 4


def png(image):
    out = io.BytesIO()
    image.save(out, "PNG", optimize=True)
    return out.getvalue()


def legacy_icons(art):
    icons = {}
    for bucket, scale in DENSITIES.items():
        size = round(48 * scale)
        n = size * SUPERSAMPLE
        dp = n / 48
        d = round(44 * dp)
        left, top = (n - d) // 2, round(23.5 * dp - d / 2)
        icon = Image.new("RGBA", (n, n), (0, 0, 0, 0))
        shadow = Image.new("L", (n, n), 0)
        ImageDraw.Draw(shadow).ellipse((left, top + dp, left + d, top + dp + d), fill=72)
        icon.putalpha(shadow.filter(ImageFilter.GaussianBlur(1.2 * dp)))
        mask = Image.new("L", (d, d), 0)
        ImageDraw.Draw(mask).ellipse((0, 0, d - 1, d - 1), fill=255)
        icon.paste(art.resize((d, d), Image.LANCZOS), (left, top), mask)
        icons[bucket] = png(icon.resize((size, size), Image.LANCZOS))
    return icons


def placeholder():
    """The rings of res/drawable/ic_launcher.xml, the icon before there were PNGs."""
    icons = {}
    for bucket, scale in DENSITIES.items():
        size = round(48 * scale)
        n = size * SUPERSAMPLE
        dp = n / 48
        icon = Image.new("RGBA", (n, n), (0x19, 0x1C, 0x21, 255))
        draw = ImageDraw.Draw(icon)
        for r in (16, 5):
            draw.ellipse(((24 - r - 1) * dp, (24 - r - 1) * dp, (24 + r + 1) * dp, (24 + r + 1) * dp),
                         outline=(0xF5, 0x9E, 0x0B, 255), width=round(2 * dp))
        icons[bucket] = png(icon.resize((size, size), Image.LANCZOS))
    return icons


def walkman_mark(logo):
    """The W. above the WALKMAN letters, orange on black in the logo, as white on that
    orange: the first band of orange rows, cropped, its red channel as the coverage."""
    logo = logo.convert("RGB")
    width, height = logo.size
    pixels = logo.load()

    def orange(x, y):
        r, _, b = pixels[x, y]
        return r > 150 and b < 80

    rows = [y for y in range(height) if any(orange(x, y) for x in range(width))]
    if not rows:
        raise ValueError("no orange mark in the logo")
    end = rows[0]
    while end + 1 in rows:
        end += 1
    band = logo.crop((0, rows[0], width, end + 1))
    coverage = band.getchannel("R").point(lambda v: v if v > 24 else 0)
    box = coverage.getbbox()
    coverage = coverage.crop(box)
    colour = max((c for c in band.getcolors(width * (end + 1 - rows[0])) if c[1][0] > 150 and c[1][2] < 80),
                 key=lambda c: c[0])[1]

    side = 512
    art = Image.new("RGBA", (side, side), colour + (255,))
    w = round(side * 0.6)
    h = round(coverage.height * w / coverage.width)
    mark = Image.new("RGBA", (w, h), (255, 255, 255, 255))
    mark.putalpha(coverage.resize((w, h), Image.LANCZOS))
    art.alpha_composite(mark, ((side - w) // 2, (side - h) // 2))
    return art
