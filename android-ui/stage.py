"""Stage the Sony UI resources and this app's QML into build/res.

    python3 -I android-ui/stage.py [--android] [--sony DIR] [--out DIR]
    python3 -I android-ui/stage.py --android --release

The Sony QML, images, fonts, translations and screen tables are proprietary
and never committed (docs/phase1-ui.md section 4). They are read from the
repo's gitignored work/ tree, or from the folder kit/sony.py made from the
owner's backup (--sony), edited only in the staged copy, and packed into
stock.rcc next to the fonts, translations and the screen tables the C++
screen controller reads. With --android the same tree is copied into the
package source directory as APK assets. --release leaves them out: that
package is the one the install kit ships, and the kit writes the resources
to /system/etc/walkmanui/res, which the app reads first.

Edits to the staged Sony tree, in order:
  1. Qt 5.15 compatibility (the device ran Sony's Qt 5.3.2), see COMPAT below.
  2. patches/*.patch, unified diffs against work/qrc-device (our changes:
     the navigation bar moves to the top of each window).
  3. Pictures that draw the stock bottom navigation bar, redrawn with this
     app's top action row (needs Pillow).
  4. qml/, this app's own QML, added under qrc:/app/.
"""
import argparse
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

APP = Path(__file__).resolve().parent
ROOT = APP.parent
WORK = ROOT / "work"
# Development reads the Sony files from work/. The install kit passes --sony,
# the folder kit/sony.py made from the owner's own backup.
QRC_DEVICE = WORK / "qrc-device"
OPT2 = WORK / "device/opt2/appresouce"
SONY = WORK / "device/system/vendor/sony"
# Which Sony view each screen id loads, as the stock app's ScreenController
# reported it on the device (its allViewLayouts property).
SCREENS = APP / "screens.json"
MKRCC = ROOT / "tools/mkrcc.py"
BUILD = APP / "build"

# Latin (SST, SST UI) and Korean. The Japanese, Chinese and Thai families add
# 30 MB to a 456 MiB device; WALKMANUI_ALL_FONTS=1 stages them too.
FONT_PREFIXES = ("SST-", "SSTUI-", "NotoSansKR-")


def replace_once(text: str, old: str, new: str, where: str) -> str:
    if text.count(old) != 1:
        sys.exit(f"{where}: expected one {old!r}, found {text.count(old)}")
    return text.replace(old, new)


def edit(stage: Path, alias: str, change) -> None:
    path = stage / alias
    path.write_text(change(path.read_text(encoding="utf-8")), encoding="utf-8")


def compat(stage: Path) -> None:
    # Qt 5.3's QQuickItemView has a Sony-added modelReset signal; a handler
    # for a signal that does not exist is a load error in 5.15.
    edit(stage, "components/EdgeDetectListView.qml", lambda t: replace_once(
        t, "  onModelReset: modelChangedOrReset()", "", "EdgeDetectListView.qml"))

    # A Repeater with a QtObject delegate crashes Qt 5.15.2 in
    # QQmlIncubatorPrivate::incubate once its model is non-empty (5.15.8 only
    # warns). Instantiator is the 5.15 type for a model of non-visual objects.
    for alias in ("components/StatusVolumeBar.qml", "components/VolumeArea.qml"):
        def instantiator(text, alias=alias):
            text, n = re.subn(r"\bRepeater(\s*\{\s*model\s*:\s*volumeOperateInfo\b)", r"Instantiator\1", text)
            if n != 1:
                sys.exit(f"{alias}: expected one Repeater over volumeOperateInfo, found {n}")
            return re.sub(r"^(import QtQuick [0-9.]+\n)", r"\1import QtQml 2.2\n", text, count=1, flags=re.M)
        edit(stage, alias, instantiator)

    # LibraryIcon's dragged and paged icons switch between two states whose
    # when-clauses read one property. Qt 5.15 passes through the "" state on
    # the way, so transitions from a named state never ran: a long press
    # left the grid flickable and the dragged icon hidden.
    def any_state(text):
        text, n = re.subn(r'from: "(?:active|inactive)"(\s*\n\s*to: "(?:active|inactive)")', r'from: "*"\1', text)
        if n != 4:
            sys.exit(f"LibraryIcon.qml: expected four named-state transitions, found {n}")
        return text
    edit(stage, "components/LibraryIcon.qml", any_state)

    # Swipe selectors read properties["objectName"] before any value arrives.
    for alias in ("components/SwipeSelector.qml", "components/SwipeSelectorViewJudge.qml"):
        edit(stage, alias, lambda t, a=alias: replace_once(
            t, "property var properties\n", "property var properties: ({objectName: \"\"})\n", a))

    # HeaderArea forwards its values to the volume bar through two Binding
    # binders that swap their property per value. Qt 5.15's Binding restores
    # the previous property's original binding when retargeted, so all but
    # the last pushed value revert; assign directly instead. The second
    # Loader's onLoaded also calls the first binder, which 5.3 tolerated.
    def header(text):
        prefix, rest = text.split("id:statusVolumeBar", 1)
        rest = replace_once(rest, "volumeAreaBinder.readValues()", "statusVolumeBarBinder.readValues()",
                            "HeaderArea.qml")
        text = prefix + "id:statusVolumeBar" + rest
        pattern = re.compile(r'property = ""\s*\n\s*value = properties\[prop\]\s*\n\s*property = prop')
        text, n = pattern.subn("if (target && prop in target) target[prop] = properties[prop]", text)
        if n != 2:
            sys.exit(f"HeaderArea.qml: expected two binders, found {n}")
        text = text.replace("Binding {", "QtObject {\n            property var target")
        return text
    edit(stage, "components/HeaderArea.qml", header)

    # Images the device reads from its option2 partition ride in the resource.
    shutil.copytree(OPT2, stage / "opt2/appresouce")
    for path in stage.rglob("*"):
        if path.suffix in (".qml", ".js"):
            text = path.read_text(encoding="utf-8")
            if "file:/opt2/appresouce/" in text:
                path.write_text(text.replace("file:/opt2/appresouce/", "qrc:/opt2/appresouce/"), encoding="utf-8")


def apply_patches(stage: Path) -> None:
    # git apply outside any repository (the stage sits inside this one, so
    # discovery is cut off at its parent) behaves like patch -p1.
    env = {**os.environ, "GIT_CEILING_DIRECTORIES": str(stage.parent)}
    for patch in sorted((APP / "patches").glob("*.patch")):
        result = subprocess.run(["git", "apply", "-p1", "--whitespace=nowarn", str(patch)], cwd=stage, env=env,
                                capture_output=True, text=True)
        if result.returncode != 0:
            sys.exit(f"{patch.name} does not apply:\n{result.stdout}{result.stderr}")


def bookmark_guide(stage: Path) -> None:
    """The empty bookmark list's picture (the empty playlist shows the same
    one under another name) of two screens, each with the stock
    four-button bar at the bottom and a ring on the button to press. The
    bar's buttons move to a header band as this app has them (Now Playing,
    Library and Option at the right end; Back is Android's), the
    content moves down under it, and the rings follow the Option button and
    the list's first menu button."""
    from PIL import Image, ImageDraw

    path = stage / "bookmark/ic_audio_favorite_quick_userguide.png"
    picture = Image.open(path).convert("RGBA")
    width, height = picture.size
    if (width, height) != (480, 310):
        sys.exit(f"{path.name}: expected 480x310, found {width}x{height}")
    pixels = picture.load()
    frame = pixels[81, 150]
    # Each screen's inside is 136x228 at (left, 43); its bar is rows 252-270.
    screens = ((82, False), (259, True))
    old_rings = ((201.5, 260.5), (384.5, 88.5))
    for y in range(height):
        for x in range(width):
            p = pixels[x, y]
            tinted = p[2] - p[0] > 10 or (p[0] == 0 and p[2] > 0)
            if tinted and any(15.5 <= ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5 <= 21 for cx, cy in old_rings):
                inside = any(left <= x < left + 136 for left, _ in screens) and 43 <= y < 271
                pixels[x, y] = (0, 0, 0, 255) if inside else (0, 0, 0, 0)
    out = picture.copy()
    for left, list_screen in screens:
        bar = Image.new("RGBA", (136, 19), (0, 0, 0, 255))
        # Now Playing, Library and Option, at their places in the 480 px header.
        for start, end, target in ((44, 58, 90), (81, 91, 108), (113, 127, 122)):
            bar.paste(picture.crop((left + start, 252, left + end, 271)), (target, 0))
        out.paste(bar, (left, 43))
        if list_screen:
            out.paste(picture.crop((left, 251, left + 136, 252)), (left, 62))
            out.paste(picture.crop((left, 43, left + 136, 251)), (left, 63))
        else:
            out.paste(picture.crop((left, 43, left + 136, 252)), (left, 62))
    draw = ImageDraw.Draw(out)
    for left, _ in screens:
        draw.rectangle((left - 1, 42, left + 136, 271), outline=frame)
    scale = 4
    rings = Image.new("RGBA", (width * scale, height * scale), (0, 0, 0, 0))
    ring_draw = ImageDraw.Draw(rings)
    for cx, cy in ((210.5, 52.0), (384.5, 108.5)):
        r = 18.5
        ring_draw.ellipse([(cx - r) * scale, (cy - r) * scale, (cx + r) * scale, (cy + r) * scale],
                          outline=(0, 204, 255, 255), width=int(2.5 * scale))
    out.alpha_composite(rings.resize((width, height), Image.LANCZOS))
    out.save(path)
    shutil.copy(path, stage / "ic_playlists_tutorial_on_no_register.png")


def guard_repeaters(stage: Path) -> None:
    bad = []
    pattern = re.compile(r"\bRepeater\s*\{\s*(?:\w[\w.]*\s*:[^\n{}]*\n\s*)*(?:QtObject|Binding|Connections|Timer)\s*\{")
    for path in stage.rglob("*.qml"):
        if pattern.search(path.read_text(encoding="utf-8")):
            bad.append(str(path.relative_to(stage)))
    if bad:
        sys.exit("Repeater with a non-Item delegate (crashes Qt 5.15.2): " + ", ".join(sorted(bad)))


def write_rcc(stage: Path, output: Path) -> int:
    # tools/mkrcc.py, not Qt's rcc: the install kit runs this on the owner's
    # computer, which has no Qt. Its format v1 loads in 5.15 as in 5.3.
    subprocess.run([sys.executable, "-I", str(MKRCC), str(stage), str(output)], check=True,
                   stdout=subprocess.DEVNULL)
    return sum(1 for p in stage.rglob("*") if p.is_file())


def stage_data(res: Path) -> None:
    fonts = res / "fonts"
    fonts.mkdir()
    everything = os.environ.get("WALKMANUI_ALL_FONTS") == "1"
    for font in sorted((SONY / "lib/fonts").iterdir()):
        if everything or font.name.startswith(FONT_PREFIXES):
            shutil.copy(font, fonts / font.name)
    shutil.copytree(SONY / "translations", res / "tr")
    shutil.copy(SONY / "bin/ScreenSettings.json", res / "ScreenSettings.json")
    shutil.copy(SCREENS, res / "allViewLayouts.json")
    stage_sensme(res / "sensme.bin")


# The SensMe classifier's numbers in the device's libMediaStoreService.so
# (2019-07-31 build), read by library/sensme.cpp: per-dimension scales (1.0 where
# the library uses the word as is), 48x16 centroids, the cluster's time channel
# and its fallback mood (index 0 unused).
SENSME_MD5 = "d3268ccc423d8369043a378ba26ebe0b"
SENSME_SCALES = (None, 0x47ed8, 0x47ed0, 0x47ec8, 0x47ec0, None, 0x47e98, 0x47e88,
                 0x47e80, None, 0x47e90, 0x47ea0, 0x47ea8, None, 0x47eb0, 0x47eb8)
SENSME_TABLES = ((0x1a69d8, 48 * 16 * 8), (0x1a81d8, 48 * 4), (0x1a8434, 49 * 4))
# Then the immediates of its channel tests (the Thumb-2 function at 0x48dac), in
# the order sensme.cpp's rule() reads them.
SENSME_TESTS = 0x48dac
SENSME_LIMITS = (0x3c, 0x52, 0x56, 0x5a, 0x70, 0x78, 0x7c,
                 0x56a, 0x56e, 0x572, 0x576, 0x594, 0x586, 0x57c, 0x5d0, 0x5c2, 0x5b8,
                 0x346, 0x33c, 0x354, 0x1da, 0x1cc, 0x1c2, 0x2aa, 0x29e,
                 0xe8, 0xde, 0xf6, 0x104,
                 0x532, 0x51a, 0x524, 0x540, 0x54e, 0x47c, 0x422, 0x418, 0x428, 0x430, 0x43e, 0x44c, 0x45a,
                 0x46a,
                 0x122, 0x124, 0x236, 0x228, 0x21e, 0x244,
                 0x12c, 0x4d6, 0x4e0, 0x4f2, 0x4f8, 0x4fe, 0x502, 0x5e0, 0x644, 0x61a, 0x610, 0x628, 0x636,
                 0x26c, 0x302, 0x310, 0x306,
                 0x166, 0x15a)


def thumb_immediate(lib: bytes, at: int) -> int:
    """The immediate of the cmp or sub at `at`."""
    h, = struct.unpack_from("<H", lib, at)
    if (h & 0xf800) in (0x2800, 0x3800):  # cmp, subs Rdn, #imm8
        return h & 0xff
    h2, = struct.unpack_from("<H", lib, at + 2)
    if not ((h & 0xfbf0) == 0xf1b0 and (h2 & 0x8f00) == 0x0f00  # cmp.w Rn, #const
            or (h & 0xfbe0) == 0xf1a0 and not h2 & 0x8000):  # sub.w Rd, Rn, #const
        sys.exit(f"libMediaStoreService.so: no cmp or sub at {at:#x}")
    imm12 = (h & 0x400) << 1 | (h2 & 0x7000) >> 4 | h2 & 0xff
    imm8 = imm12 & 0xff
    if imm12 >> 10 == 0:
        return (imm8, imm8 * 0x00010001, imm8 * 0x01000100, imm8 * 0x01010101)[imm12 >> 8 & 3]
    v, rot = 0x80 | imm12 & 0x7f, imm12 >> 7
    return (v >> rot | v << (32 - rot)) & 0xffffffff


def stage_sensme(output: Path) -> None:
    lib = (SONY / "lib/libMediaStoreService.so").read_bytes()
    if hashlib.md5(lib).hexdigest() != SENSME_MD5:
        sys.exit("libMediaStoreService.so is not the build the SensMe offsets were read from")
    data = b"".join(lib[a:a + 8] if a else struct.pack("<d", 1.0) for a in SENSME_SCALES)
    data += b"".join(lib[a:a + n] for a, n in SENSME_TABLES)
    limits = [thumb_immediate(lib, SENSME_TESTS + a) for a in SENSME_LIMITS]
    if max(limits) > 0xff:
        sys.exit("libMediaStoreService.so: a SensMe limit does not fit a byte")
    data += bytes(limits)
    output.write_bytes(data)


# Sony's own Walkman app icon, from the ZX507's HighResMediaPlayerApp
# (com.sony.walkman.highresmediaplayer, res/mipmap), copied into work/. It is
# an adaptive icon, which Android 5.1 cannot show, so its two 108 dp layers are
# flattened here: the inner 72 dp the ZX507's launcher masks to a circle, drawn
# as launcher_icon.py's legacy PNGs. Without the layers the committed
# placeholder vector stays the icon. A release package gets the placeholder as
# PNGs, which the install kit draws over with the owner's own Walkman mark.
SONY_ICON = WORK / "zx507-res/HighResMediaPlayerApp"
SONY_ICON_MD5 = {
    "ic_launcher_background.png": "ce2419d52f14ff75d6aae212d9fefe67",
    "ic_launcher_foreground.png": "326a42a2075580c4d758c8bf39f18293",
}


def write_icons(res: Path, icons: dict) -> None:
    for bucket, data in icons.items():
        out = res / f"drawable-{bucket}/ic_launcher.png"
        out.parent.mkdir(exist_ok=True)
        out.write_bytes(data)
    (res / "drawable/ic_launcher.xml").unlink()


def launcher_icon(res: Path) -> bool:
    from PIL import Image

    sys.path.insert(0, str(APP))
    from launcher_icon import legacy_icons

    layers = []
    for name, md5 in SONY_ICON_MD5.items():
        path = SONY_ICON / name
        if not path.exists():
            return False
        if hashlib.md5(path.read_bytes()).hexdigest() != md5:
            sys.exit(f"{path}: not the ZX507 build the launcher icon was drawn from")
        layers.append(Image.open(path).convert("RGBA"))
    art = Image.alpha_composite(*layers)
    side = art.size[0]
    art = art.crop((side // 6, side // 6, side - side // 6, side - side // 6))
    write_icons(res, legacy_icons(art))
    return True


def main() -> None:
    global QRC_DEVICE, OPT2, SONY
    parser = argparse.ArgumentParser()
    parser.add_argument("--android", action="store_true", help="also fill build/android-pkg")
    parser.add_argument("--release", action="store_true",
                        help="with --android: only the package, without Sony's resources and icon; "
                             "the install kit puts the resources into the system image")
    parser.add_argument("--sony", type=Path, help="kit/sony.py's folder, instead of work/")
    parser.add_argument("--out", type=Path, default=BUILD / "res", help="where the resources go")
    args = parser.parse_args()
    if args.release and not args.android:
        sys.exit("--release goes with --android")
    pkg = BUILD / "android-pkg"
    if args.release:
        # The placeholder as PNGs, so the install kit can draw the owner's
        # icon over the same entries in the APK it puts into the image.
        sys.path.insert(0, str(APP))
        from launcher_icon import placeholder

        shutil.rmtree(pkg, ignore_errors=True)
        shutil.copytree(APP / "android", pkg)
        write_icons(pkg / "res", placeholder())
        print(f"android package source: {pkg} (no Sony resources, placeholder icon)")
        return

    if args.sony:
        QRC_DEVICE, OPT2, SONY = args.sony / "qrc", args.sony / "opt2/appresouce", args.sony / "system/vendor/sony"
    for path in (QRC_DEVICE, OPT2, SONY / "bin/ScreenSettings.json"):
        if not path.exists():
            sys.exit(f"missing {path} -- see tools/uisim.sh and docs/phase1-ui.md")

    out = args.out.resolve()
    work = out.with_name(out.name + ".staging")
    shutil.rmtree(work, ignore_errors=True)
    stage = work / "qrc"
    shutil.copytree(QRC_DEVICE, stage)
    compat(stage)
    apply_patches(stage)
    bookmark_guide(stage)
    shutil.copytree(APP / "qml", stage / "app")
    guard_repeaters(stage)

    res = out.with_name(out.name + ".new")
    shutil.rmtree(res, ignore_errors=True)
    res.mkdir(parents=True)
    count = write_rcc(stage, res / "stock.rcc")
    stage_data(res)
    shutil.rmtree(out, ignore_errors=True)
    res.rename(out)
    shutil.rmtree(work)
    print(f"stock.rcc: {count} files, {(out / 'stock.rcc').stat().st_size} bytes")

    if args.android:
        shutil.rmtree(pkg, ignore_errors=True)
        shutil.copytree(APP / "android", pkg)
        shutil.copytree(out, pkg / "assets/res")
        if launcher_icon(pkg / "res"):
            print("launcher icon: Sony Walkman app icon")
        else:
            print(f"launcher icon: placeholder (no {SONY_ICON})")
        print(f"android package source: {pkg}")


if __name__ == "__main__":
    main()
