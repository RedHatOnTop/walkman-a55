#!/usr/bin/env python3
# Debloat a mounted cm-12.1 /system and embed our system apps.
# Usage: cm_customize.py <mountpoint> <apps_dir>
# Destructive dir removals go through shutil.rmtree here (not a shell `rm`).
import os
import shutil
import sys
import zipfile

# /system/app leaf apps safe to drop (wallpapers/dreams, non-music bloat,
# browsers/players we replace). Providers, SystemUI, Settings, launcher, BT,
# keyboard, webview, installer and telephony are kept for a first boot-safe pass.
REMOVE_APP = [
    # live/static wallpapers + dreams
    "BasicDreams", "CMWallpapers", "Galaxy4", "HoloSpiralWallpaper",
    "LiveWallpapers", "LiveWallpapersPicker", "NoiseField", "PhaseBeam",
    "PhotoPhase", "PhotoTable", "VisualizationWallpapers",
    # non-music leaf apps
    "Email", "Exchange2", "Calendar", "Camera2", "Calculator", "DeskClock",
    "SoundRecorder", "Terminal", "PrintSpooler", "PicoTts", "HTMLViewer",
    "Gallery2", "Profiles", "LockClock", "Development",
    "WAPPushManager",
    # replaced by what we add
    "privacy-browser", "Eleven",
]
REMOVE_PRIVAPP = []  # conservative: keep all priv-app for the first pass

# (dir name in /system/app, source apk filename)
EMBED = [
    ("WalkmanUI", "WalkmanUI.apk"),
    ("Firefox", "Firefox.apk"),
]

# APKs ship native libs under lib/armeabi-v7a/. /system is mounted read-only at
# boot, so an app with extractNativeLibs=true cannot unpack them at runtime and
# dies with UnsatisfiedLinkError. AOSP pre-extracts a system app's libs into
# <codePath>/lib/<arch>/ instead (observed on this image: webview, Bluetooth,
# LatinIME all carry lib/arm/*.so), and PackageManager points the app there. For
# armeabi-v7a the on-device arch dir is "arm".
APK_ABI = "armeabi-v7a"
SYS_ABI = "arm"


def extract_libs(apk, app_dir):
    members = [n for n in zipfile.ZipFile(apk).namelist()
               if n.startswith(f"lib/{APK_ABI}/") and n.endswith(".so")]
    if not members:
        return 0
    lib_dir = os.path.join(app_dir, "lib")
    arch_dir = os.path.join(lib_dir, SYS_ABI)
    os.makedirs(arch_dir, exist_ok=True)
    with zipfile.ZipFile(apk) as z:
        for n in members:
            dst = os.path.join(arch_dir, os.path.basename(n))
            with z.open(n) as fsrc, open(dst, "wb") as fdst:
                shutil.copyfileobj(fsrc, fdst)
            os.chmod(dst, 0o644)
    os.chmod(lib_dir, 0o755)
    os.chmod(arch_dir, 0o755)
    return len(members)


def main(mnt, apps):
    removed, missing = [], []
    for sub, names in (("app", REMOVE_APP), ("priv-app", REMOVE_PRIVAPP)):
        for n in names:
            p = os.path.join(mnt, sub, n)
            if os.path.isdir(p):
                shutil.rmtree(p)
                removed.append(f"{sub}/{n}")
            else:
                missing.append(f"{sub}/{n}")

    embedded = []
    for name, apk in EMBED:
        src = os.path.join(apps, apk)
        if not os.path.isfile(src):
            sys.exit(f"missing source apk {src}")
        d = os.path.join(mnt, "app", name)
        os.makedirs(d, exist_ok=True)
        dst = os.path.join(d, name + ".apk")
        shutil.copyfile(src, dst)
        os.chmod(d, 0o755)
        os.chmod(dst, 0o644)
        libs = extract_libs(src, d)
        embedded.append(
            f"app/{name}/{name}.apk ({os.path.getsize(dst)//1024//1024} MiB, "
            f"{libs} native libs -> lib/{SYS_ABI})")

    print(f"removed {len(removed)} dirs:")
    for r in removed:
        print(f"  - {r}")
    if missing:
        print(f"not present (skipped): {', '.join(missing)}")
    print(f"embedded {len(embedded)} apps:")
    for e in embedded:
        print(f"  + {e}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: cm_customize.py <mountpoint> <apps_dir>")
    main(sys.argv[1], sys.argv[2])
