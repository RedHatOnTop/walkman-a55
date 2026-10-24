#!/usr/bin/env python3
"""Which key the owner's player takes a UPG with, read from their backup.

    upg_key.py BACKUP

Prints "w1" or "stock", the key set of tools/nwupg.py (-w or none). A UPG is
checked by bin/fwpchk in the recovery ramdisk, the slot the update trigger
boots; Walkman One's installer swaps it for one with its own passkey. So
BACKUP's recovery.img must be that updater: an image of anything else in the
recovery slot is refused, since a UPG trigger would boot it instead
(devlog/2026-10-21-picker-recovery.md).
"""
import gzip
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # python3 -I leaves it out
from fire_ramdisk import cpio  # noqa: E402

# tools/nwupg.py VARIANTS: the passkeys the two fwpchk builds carry.
PASSKEYS = {"w1": b"9cc4419c8bef488e", "stock": b"9cc4419c8bef488c"}
MTK_MAGIC = b"\x88\x16\x88\x58"


def ramdisk(boot):
    """The recovery ramdisk's cpio. MediaTek puts a 512-byte header, named
    RECOVERY here, in front of the gzip."""
    if boot[:8] != b"ANDROID!":
        sys.exit("recovery.img has no Android boot image header")
    kernel_size, _, ramdisk_size = struct.unpack("<3I", boot[8:20])
    page = struct.unpack("<I", boot[36:40])[0]
    start = page + -(-kernel_size // page) * page
    blob = boot[start:start + ramdisk_size]
    if blob[:4] != MTK_MAGIC or blob[8:40].split(b"\0")[0] != b"RECOVERY":
        sys.exit("recovery.img's ramdisk is not a RECOVERY image")
    return gzip.decompress(blob[512:])


def main(backup):
    with open(os.path.join(backup, "recovery.img"), "rb") as f:
        boot = f.read()
    fwpchk = next((data for name, _, data in cpio(ramdisk(boot)) if name == "bin/fwpchk"), None)
    if fwpchk is None:
        sys.exit(f"{backup}/recovery.img has no bin/fwpchk: the recovery slot holds something "
                 "other than the updater, and a UPG trigger would boot it")
    found = [key for key, passkey in PASSKEYS.items() if passkey in fwpchk]
    if len(found) != 1:
        sys.exit(f"{backup}/recovery.img: fwpchk carries {'both' if found else 'neither'} of the known keys")
    print(found[0])


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
