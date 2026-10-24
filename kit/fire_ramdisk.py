#!/usr/bin/env python3
"""The four files the container takes from Fire OS, out of the owner's update.

    fire_ramdisk.py UPDATE.bin OUT

UPDATE.bin is Amazon's Fire OS 5.3.6.4 update for the Fire 7 (ford). Only its
boot.img is read: the ramdisk's static init, healthd and ueventd rules go to
OUT under their ramdisk paths, for tools/android-container.sh
(NW_FIRE_RAMDISK=OUT). Each must be the build the container was brought up
with, byte for byte; android-container.sh patches healthd itself.
"""
import gzip
import hashlib
import os
import struct
import sys
import zipfile

TAKE = {
    "init": "f897d856dd8bc06951fe616117e6b27eee2673763f2d4b26a6d847b3fd5b734a",
    "sbin/healthd": "772b3518fc5cb4ac670340ded72bec7502013a1c19881e2d2ff435fecd67461f",
    "ueventd.rc": "01d9566b079861171210f1760b4ed49b0f1c7b84a6efcfb151e874a4adbf7d74",
    "ueventd.mt8127.rc": "dfd75ba182cd4e89f4ae14bf54bf7b1cd9ad9a160b6ebed2c47e0747af94f9d9",
}


def ramdisk(boot):
    """The gzip'd cpio of an Android boot image (header v0)."""
    if boot[:8] != b"ANDROID!":
        sys.exit("boot.img has no Android boot image header")
    kernel_size, _, ramdisk_size = struct.unpack("<3I", boot[8:20])
    page = struct.unpack("<I", boot[36:40])[0]
    start = page + -(-kernel_size // page) * page
    return gzip.decompress(boot[start:start + ramdisk_size])


def cpio(blob):
    """(name, mode, data) of each entry of a newc archive."""
    pos = 0
    while True:
        if blob[pos:pos + 6] != b"070701":
            sys.exit(f"ramdisk: no cpio header at {pos}")
        field = [int(blob[pos + 6 + 8 * i:pos + 14 + 8 * i], 16) for i in range(13)]
        mode, size, namesize = field[1], field[6], field[11]
        name = blob[pos + 110:pos + 110 + namesize - 1].decode()
        pos += -(-(110 + namesize) // 4) * 4
        if name == "TRAILER!!!":
            return
        yield name, mode, blob[pos:pos + size]
        pos += -(-size // 4) * 4


def main(update, out):
    if os.path.exists(out):
        sys.exit(f"{out} exists")
    with zipfile.ZipFile(update) as z:
        boot = z.read("boot.img")
    found = {name: (mode, data) for name, mode, data in cpio(ramdisk(boot)) if name in TAKE}
    for name, want in TAKE.items():
        if name not in found:
            sys.exit(f"{update}: no {name} in the boot image's ramdisk")
        if hashlib.sha256(found[name][1]).hexdigest() != want:
            sys.exit(f"{update}: {name} is not the build the container was brought up with")
    for name, (mode, data) in found.items():
        path = os.path.join(out, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(data)
        os.chmod(path, mode & 0o777)
    print(f"{out}: {', '.join(TAKE)}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(*sys.argv[1:])
