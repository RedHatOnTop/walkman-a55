#!/usr/bin/env python3
"""install.json for what make-install.sh left in OUT.

    install_json.py OUT BACKUP KEY FIRMWARE > OUT/install.json

The install copies files onto the player's internal storage (the WALKMAN drive) under the
paths they have in OUT, reads each back, then triggers the UPG, which installs the picker
hook and logs to the drive. So install.json carries:

  files     every file to copy, by its path: bytes and sha256
  copy      the order: the boot set with its MANIFEST last, so a copy that stops half way
            leaves a set android-boot.sh refuses, then the images, then the UPG
  keep      files a player that has one keeps: its Android /data
  upg       the UPG, the key it is built with and the recovery image the trigger boots, as
            the backup the kit read holds it; a player whose recovery reads otherwise now
            is not the one the kit was made for
  after     the log the UPG writes and the lines it must hold once it ran
"""
import hashlib
import json
import os
import re
import subprocess
import sys

KIT = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(KIT)
UPG = "NW_WM_FW.UPG"
DATA = "wmmod-android/android-data.img"
IMAGES = ["wmmod-android/fire-system.img", DATA]
SET = "wmmod-android/boot"


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(4 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def hook_md5():
    """md5 of the bootswitcher.sh the UPG writes: update.sh's HOOK here-document."""
    with open(os.path.join(ROOT, "picker-upg/update.sh")) as f:
        m = re.search(r"<<'HOOK'\n(.*?\n)HOOK\n", f.read(), re.S)
    return hashlib.md5(m.group(1).encode()).hexdigest()


def git(*args):
    r = subprocess.run(["git", "-C", ROOT, *args], capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else None


def commit():
    """The commit git archive stamped into kit/COMMIT, or a checkout's HEAD and whether its
    tracked files changed."""
    with open(os.path.join(KIT, "COMMIT")) as f:
        stamp = f.read().strip()
    if re.fullmatch(r"[0-9a-f]{40}", stamp):
        return stamp, False
    head = git("rev-parse", "HEAD")
    return head, bool(head and git("status", "--porcelain", "--untracked-files=no"))


def main(out, backup, key, firmware):
    found = []
    for d, _, names in os.walk(out):
        found += [os.path.relpath(os.path.join(d, n), out) for n in names]
    in_set = sorted(p for p in found if p.startswith(SET + "/") and p != f"{SET}/MANIFEST")
    copy = in_set + [f"{SET}/MANIFEST"] + IMAGES + [UPG]
    missing = [p for p in copy if p not in found]
    if missing:
        sys.exit(f"{out} has no {', '.join(missing)}")
    rev, dirty = commit()
    print(json.dumps({
        "kit": "nw-a50-android5",
        "commit": rev,
        "dirty": dirty,
        "firmware": firmware,
        "files": {p: {"bytes": os.path.getsize(os.path.join(out, p)), "sha256": sha256(os.path.join(out, p))}
                  for p in copy},
        "copy": copy,
        "keep": [DATA],
        "upg": {"file": UPG, "key": key, "recovery": sha256(os.path.join(backup, "recovery.img"))},
        "after": {"log": "PICKERUPG.log",
                  "expect": [f"hook md5 {hook_md5()}", "====== PICKER UPG DONE ======"]},
    }, indent=2))


if __name__ == "__main__":
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    main(*sys.argv[1:])
