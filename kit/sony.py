#!/usr/bin/env python3
"""The Sony files the kit uses, from the owner's own player.

    sony.py BACKUP OUT
    sony.py --pin NAME BACKUP OUT    write kit/sony-NAME.sha256 from a backup
                                     the patches were made against

BACKUP is the WMBACKUP folder backup-upg/ leaves on the player's internal
storage: android.img is the system partition, option2.img the partition with
the music app's button images. They are only read, with debugfs. OUT gets the
files under their paths on the player (system/..., opt2/...), then the Qt
resources compiled into the music app (tools/qrcdump.py) under qrc/, and must
match one of the kit's pins, kit/sony-*.sha256, file for file. The pin's name
is printed: it is the build every patch in android-ui/patches is made against.
"""
import hashlib
import os
import subprocess
import sys

KIT = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(KIT)

# (image, path on it, where it goes under OUT)
TAKE = [
    ("android.img", "/vendor/sony/bin/HgrmMediaPlayerApp", "system/vendor/sony/bin"),
    ("android.img", "/vendor/sony/bin/ScreenSettings.json", "system/vendor/sony/bin"),
    ("android.img", "/vendor/sony/lib/libMediaStoreService.so", "system/vendor/sony/lib"),
    ("android.img", "/vendor/sony/lib/fonts", "system/vendor/sony/lib"),
    ("android.img", "/vendor/sony/translations", "system/vendor/sony"),
    ("android.img", "/media/bootanimation.zip", "system/media"),
    ("option2.img", "/appresouce", "opt2"),
]
APP = "system/vendor/sony/bin/HgrmMediaPlayerApp"


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def manifest(out):
    lines = []
    for d, _, files in os.walk(out):
        for name in files:
            p = os.path.join(d, name)
            rel = os.path.relpath(p, out)
            if not rel.startswith("qrc/"):
                lines.append(f"{sha256(p)}  {rel}")
    return sorted(lines, key=lambda line: line[66:])


def main(backup, out, pin_as=None):
    for image in {image for image, _, _ in TAKE}:
        if not os.path.isfile(os.path.join(backup, image)):
            sys.exit(f"no {image} in {backup}: the backup UPG writes it to WMBACKUP")
    if os.path.exists(out):
        sys.exit(f"{out} exists")
    for image, path, into in TAKE:
        dest = os.path.join(out, into)
        os.makedirs(dest, exist_ok=True)
        r = subprocess.run(["debugfs", "-R", f"rdump {path} {dest}", os.path.join(backup, image)],
                           capture_output=True, text=True)
        target = os.path.join(dest, os.path.basename(path))
        if not os.path.exists(target):
            sys.exit(f"{image}:{path} could not be read: {r.stderr.strip()}")

    have = manifest(out)
    if pin_as:
        with open(os.path.join(KIT, f"sony-{pin_as}.sha256"), "w") as f:
            f.write("".join(line + "\n" for line in have))
    pins = sorted(p for p in os.listdir(KIT) if p.startswith("sony-") and p.endswith(".sha256"))
    for pin in pins:
        with open(os.path.join(KIT, pin)) as f:
            want = sorted((line.strip() for line in f if line.strip() and not line.startswith("#")),
                          key=lambda line: line[66:])
        if have == want:
            break
    else:
        # Against the closest pin: the files that differ or are missing.
        def lines(pin):
            return {line.strip() for line in open(os.path.join(KIT, pin)) if line.strip()}
        pin = max(pins, key=lambda p: len(lines(p) & set(have)))
        off = sorted({line[66:] for line in lines(pin) - set(have)})
        sys.exit(f"{backup} is not a build the kit knows: {len(off)} of {len(lines(pin))} files differ "
                 f"from {pin}, such as {', '.join(off[:4])}")

    subprocess.run([sys.executable, "-I", os.path.join(ROOT, "tools/qrcdump.py"),
                    os.path.join(out, APP), os.path.join(out, "qrc")], check=True, stdout=subprocess.DEVNULL)
    print(f"{pin[:-len('.sha256')]}: {len(have)} files, "
          f"{sum(len(f) for _, _, f in os.walk(os.path.join(out, 'qrc')))} resources from the music app")


if __name__ == "__main__":
    if len(sys.argv) == 5 and sys.argv[1] == "--pin":
        main(sys.argv[3], sys.argv[4], pin_as=sys.argv[2])
    elif len(sys.argv) == 3:
        main(*sys.argv[1:])
    else:
        sys.exit(__doc__)
