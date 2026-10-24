#!/usr/bin/env python3
"""The container's /system: LineageOS 12.1 for the Fire 7 (ford), made into the
NW-A55's image.

    build_image.py --count
    build_image.py OUT.img --sdat DIR --walkmanui APK --walkmanui-res DIR \\
        --walkmanui-logo PNG --browser APK --audio-hal SO --soundfx SO --wallpaper JPG \\
        --bootanimation ZIP

--sdat names the folder with the ford zip's system.transfer.list and
system.new.dat; --walkmanui-res the one android-ui/stage.py --sony filled from
the owner's own firmware, which goes to /etc/walkmanui/res, where the release
APK looks for it. Each step
prints "== <n> <step>" as it starts; make-install.sh renumbers them into its
own count. Every step writes through debugfs (ext4.py) and reads back what it
wrote; the image ends checked and shrunk to its contents.
"""
import argparse
import io
import os
import struct
import subprocess
import sys
import tempfile
import zipfile
import zlib

KIT = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(KIT)
sys.path.insert(0, KIT)  # python3 -I leaves the script's directory out
sys.path.insert(0, os.path.join(ROOT, "probe/android/connectivity/bt/a2dp-only"))
sys.path.insert(0, os.path.join(ROOT, "android-ui"))
from ext4 import Ext4  # noqa: E402
from flip_bools import flip  # noqa: E402
from launcher_icon import legacy_icons, walkman_mark  # noqa: E402
STEPS = ["unpack", "debloat", "apps", "walkmanui-res", "bluetooth", "audio", "soundfx", "props", "wallpaper", "bootanimation", "finish"]

# System apps go where cm_customize.py put them, with their armeabi-v7a
# libraries pre-extracted: /system is read-only, so PackageManager cannot unpack
# them at install time (devlog 2026-10-14-cm-ford-first-boot.md).
APK_ABI, SYS_ABI = "armeabi-v7a", "arm"

# Appended to /build.prop (devlog 2026-10-15-ford-debloat2-lowram.md).
PROPS = """
# --- nw-a55 low-RAM + battery tuning (456 MiB device; 2026-10-15) ---
ro.config.low_ram=true
persist.sys.purgeable_assets=1
wifi.supplicant_scan_interval=180
# SystemUI and system_server on the GPU despite low_ram: drawn in software, the
# status bar kept the top of the open panel after Quick Settings started an
# activity over the lock screen (devlog 2026-10-23-systemui-gpu.md).
persist.sys.force_highendgfx=true
"""

# Every Bluetooth profile a headset or phone could use besides A2DP (with
# AVRCP) and GATT, as probe/android/connectivity/bt/a2dp-only/build.sh turns
# them off. That script re-signs the APK it binds over a stock image; in the
# image it keeps its signature, as Android 5.1 verifies only a system app's
# AndroidManifest.xml.
BLUETOOTH = "/app/Bluetooth/Bluetooth.apk"
BLUETOOTH_OFF = ["profile_supported_hs_hfp", "profile_supported_pbap", "profile_supported_map",
                 "profile_supported_opp", "profile_supported_pan", "profile_supported_hid",
                 "profile_supported_hidd", "profile_supported_hdp"]

FRAMEWORK_RES = "/framework/framework-res.apk"
WALKMANUI_RES = "/etc/walkmanui/res"
WALLPAPER_ENTRY = "default_wallpaper.jpg"


def step(n, name):
    print(f"== {n} {name}", flush=True)


def add_app(img, name, apk):
    app = f"/app/{name}"
    if img.exists(app):
        sys.exit(f"{app} is already in the image")
    label = img.label("/app")
    img.mkdir(app, label=label)
    files = [(apk, f"{app}/{name}.apk", 0o644, 0, 0, label)]
    tmp = tempfile.mkdtemp(dir=os.path.dirname(img.path))
    with zipfile.ZipFile(apk) as z:
        for member in z.namelist():
            if member.startswith(f"lib/{APK_ABI}/") and member.endswith(".so"):
                local = os.path.join(tmp, os.path.basename(member))
                with z.open(member) as src, open(local, "wb") as dst:
                    dst.write(src.read())
                files.append((local, f"{app}/lib/{SYS_ABI}/{os.path.basename(member)}", 0o644, 0, 0, label))
    if len(files) > 1:
        img.mkdir(f"{app}/lib", label=label)
        img.mkdir(f"{app}/lib/{SYS_ABI}", label=label)
    img.put(files)
    for local, *_ in files[1:]:
        os.unlink(local)
    os.rmdir(tmp)
    print(f"{app}: {os.path.getsize(apk)} bytes, {len(files) - 1} libs")


def put_tree(img, local, path, label):
    """local's directories and files under path, root-owned, labelled as label."""
    img.mkdir(path, label=label)
    files = []
    for d, dirs, names in os.walk(local):
        rel = os.path.relpath(d, local)
        here = path if rel == "." else f"{path}/{rel}"
        for name in sorted(dirs):
            img.mkdir(f"{here}/{name}", label=label)
        files += [(os.path.join(d, name), f"{here}/{name}", 0o644, 0, 0, label) for name in sorted(names)]
    img.put(files)
    return len(files)


def with_entries(apk, replace):
    """apk's bytes with the named entries' contents replaced, stored, and every
    stored entry 4-byte aligned as zipalign 4 leaves it. Entry order, times and
    attributes stay as they were."""
    data = open(apk, "rb").read()
    out, central = bytearray(), bytearray()
    infos = zipfile.ZipFile(io.BytesIO(data)).infolist()
    for info in infos:
        off = info.header_offset
        (_, version, flag, method, time, date, crc, csize, usize, nlen, elen) = \
            struct.unpack("<IHHHHHIIIHH", data[off:off + 30])
        name = data[off + 30:off + 30 + nlen]
        payload = data[off + 30 + nlen + elen:off + 30 + nlen + elen + info.compress_size]
        crc, csize, usize = info.CRC, info.compress_size, info.file_size
        if info.filename in replace:
            payload = replace[info.filename]
            method, crc, csize, usize = 0, zlib.crc32(payload), len(payload), len(payload)
        flag &= ~0x08
        local = len(out)
        pad = b"\0" * ((-(local + 30 + nlen)) % 4) if method == 0 else b""
        out += struct.pack("<IHHHHHIIIHH", 0x04034B50, version, flag, method, time, date,
                           crc, csize, usize, nlen, len(pad)) + name + pad + payload
        central += struct.pack("<IHHHHHHIIIHHHHHII", 0x02014B50,
                               info.create_system << 8 | info.create_version, version, flag, method,
                               time, date, crc, csize, usize, nlen, 0, 0, 0,
                               info.internal_attr, info.external_attr, local) + name
    start = len(out)
    out += central
    out += struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, len(infos), len(infos), len(central), start, 0)
    return bytes(out)


def main():
    if sys.argv[1:] == ["--count"]:
        print(len(STEPS))
        return
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    for opt in ("sdat", "walkmanui", "walkmanui_res", "walkmanui_logo", "browser", "audio_hal", "soundfx",
                "wallpaper", "bootanimation"):
        ap.add_argument("--" + opt.replace("_", "-"), required=True)
    a = ap.parse_args()
    if os.path.exists(a.out):
        sys.exit(f"{a.out} exists")
    img = Ext4(a.out)
    tmp = tempfile.mkdtemp(dir=os.path.dirname(os.path.abspath(a.out)))

    step(1, "unpack")
    subprocess.run([sys.executable, "-I", os.path.join(ROOT, "tools/sdat2img.py"),
                    os.path.join(a.sdat, "system.transfer.list"), os.path.join(a.sdat, "system.new.dat"),
                    a.out], check=True, stdout=subprocess.DEVNULL)

    step(2, "debloat")
    dirs = [line.strip() for line in open(os.path.join(KIT, "debloat.txt"))
            if line.strip() and not line.startswith("#")]
    for d in dirs:
        img.rm_tree(d)
    print(f"{len(dirs)} directories removed")

    step(3, "apps")
    # The release APK carries placeholder PNGs; the owner's icon goes over them
    # in place, drawn from the W. of their player's shutdown logo.
    from PIL import Image
    icons = {f"res/drawable-{bucket}-v4/ic_launcher.png": data
             for bucket, data in legacy_icons(walkman_mark(Image.open(a.walkmanui_logo))).items()}
    with zipfile.ZipFile(a.walkmanui) as z:
        missing = sorted(set(icons) - set(z.namelist()))
    if missing:
        sys.exit(f"{a.walkmanui} has no {missing[0]}: a release APK from before the PNG icon")
    walkmanui = os.path.join(tmp, "WalkmanUI.apk")
    with open(walkmanui, "wb") as f:
        f.write(with_entries(a.walkmanui, icons))
    with zipfile.ZipFile(walkmanui) as z:
        if z.testzip() is not None or any(z.read(name) != data for name, data in icons.items()):
            sys.exit("the WalkmanUI.apk with the icon does not read back")
    print(f"WalkmanUI icon: the W. of {os.path.basename(a.walkmanui_logo)}")
    add_app(img, "WalkmanUI", walkmanui)
    os.unlink(walkmanui)
    add_app(img, "Browser", a.browser)

    step(4, "walkmanui-res")
    if not os.path.isfile(os.path.join(a.walkmanui_res, "stock.rcc")):
        sys.exit(f"no stock.rcc in {a.walkmanui_res}: android-ui/stage.py --sony makes it")
    label = img.label("/etc")
    img.mkdir(os.path.dirname(WALKMANUI_RES), label=label)
    print(f"{WALKMANUI_RES}: {put_tree(img, a.walkmanui_res, WALKMANUI_RES, label)} files")

    step(5, "bluetooth")
    apk = img.get(BLUETOOTH, os.path.join(tmp, "Bluetooth.apk"))
    with zipfile.ZipFile(apk) as z:
        arsc = flip(z.read("resources.arsc"), BLUETOOTH_OFF)
    local = os.path.join(tmp, "Bluetooth.new.apk")
    with open(local, "wb") as f:
        f.write(with_entries(apk, {"resources.arsc": arsc}))
    with zipfile.ZipFile(local) as z:
        if z.testzip() is not None or z.read("resources.arsc") != arsc:
            sys.exit("the rewritten Bluetooth.apk does not read back")
    img.put([(local, BLUETOOTH, 0o644, 0, 0, None)])

    step(6, "audio")
    img.put([(a.audio_hal, "/lib/hw/audio.primary.mt8127.so", 0o644, 0, 0, None)])

    step(7, "soundfx")
    subprocess.run([sys.executable, "-I", os.path.join(ROOT, "android-ui/audiofx/embed.py"),
                    a.out, a.soundfx], check=True)

    step(8, "props")
    prop = img.read("/build.prop").decode()
    if PROPS.strip() not in prop:
        local = os.path.join(tmp, "build.prop")
        with open(local, "w") as f:
            f.write(prop + PROPS)
        img.put([(local, "/build.prop", 0o644, 0, 0, None)])

    step(9, "wallpaper")
    res = img.get(FRAMEWORK_RES, os.path.join(tmp, "framework-res.apk"))
    jpg = open(a.wallpaper, "rb").read()
    names = [n for n in zipfile.ZipFile(res).namelist() if n.endswith("/" + WALLPAPER_ENTRY)]
    local = os.path.join(tmp, "framework-res.new.apk")
    with open(local, "wb") as f:
        f.write(with_entries(res, {n: jpg for n in names}))
    with zipfile.ZipFile(local) as z:
        if z.testzip() is not None or any(z.read(n) != jpg for n in names):
            sys.exit("the rewritten framework-res.apk does not read back")
    img.put([(local, FRAMEWORK_RES, 0o644, 0, 0, None)])
    print(f"{len(names)} wallpaper entries")

    step(10, "bootanimation")
    img.put([(a.bootanimation, "/media/bootanimation.zip", 0o644, 0, 0, None)])

    step(11, "finish")
    for name in os.listdir(tmp):
        os.unlink(os.path.join(tmp, name))
    os.rmdir(tmp)
    img.shrink()
    print(f"{a.out}: {os.path.getsize(a.out)} bytes")


if __name__ == "__main__":
    main()
