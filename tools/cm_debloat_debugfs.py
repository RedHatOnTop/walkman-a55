#!/usr/bin/env python3
# Recursively remove /system app directories from a cm-12.1 ext4 image using
# debugfs -- no mount. fuse2fs cannot write (even metadata) on this image here,
# and a kernel loop mount needs root, so debugfs is the only working path (the
# same one that embedded the audio HAL). debugfs has no `rrm`, so this walks each
# target with `ls -l` and emits file-removes then dir-removes, deepest first.
#
# Usage: cm_debloat_debugfs.py <image> <dir> [<dir> ...]
#   <dir> is an absolute in-image path, e.g. /priv-app/ThemeChooser
#
# Run removals before adding anything: removes free blocks, and this image sits
# at resize2fs -M (no slack), so additions must wait until space is freed.
# Follow with: e2fsck -fy <image>   (debugfs edits leave the fs needing a check)
#
# DO NOT target /vendor or /bin/idmap: the connectivity session's BT A2DP-only
# RRO overlay (910f1b4) is placed into /vendor/overlay at boot and relies on
# idmap. Removing either breaks it. Only prune /app and /priv-app leaves here.
import os
import subprocess
import sys


def ls(image, path):
    out = subprocess.run(["debugfs", "-R", f"ls -l {path}", image],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        f = line.split()
        if len(f) < 9:
            continue
        try:
            mode = int(f[1], 8)
        except ValueError:
            continue
        name = f[-1]
        if name in (".", ".."):
            continue
        yield name, mode


def walk(image, path, files, dirs):
    for name, mode in ls(image, path):
        child = f"{path}/{name}"
        if mode & 0o170000 == 0o040000:      # directory
            walk(image, child, files, dirs)
            dirs.append(child)               # children already appended -> deepest first
        else:                                # regular file (or link)
            files.append(child)


def main(image, targets):
    cmds, planned = [], []
    for t in targets:
        entries = list(ls(image, t))
        if not entries and not path_exists(image, t):
            print(f"  skip (absent): {t}")
            continue
        files, dirs = [], []
        walk(image, t, files, dirs)
        for fpath in files:
            cmds.append(f"rm {fpath}")
        for dpath in dirs:
            cmds.append(f"rmdir {dpath}")
        cmds.append(f"rmdir {t}")
        planned.append((t, len(files), len(dirs) + 1))

    if not cmds:
        print("nothing to remove")
        return
    script = "\n".join(cmds) + "\nquit\n"
    r = subprocess.run(["debugfs", "-w", image], input=script,
                       capture_output=True, text=True)
    # debugfs prints each command back; surface only real errors
    for line in r.stdout.splitlines():
        low = line.lower()
        if any(k in low for k in ("error", "not found", "file exists", "directory not empty")):
            print("  debugfs:", line)
    for t, nf, nd in planned:
        print(f"removed {t}  ({nf} files, {nd} dirs)")


def path_exists(image, path):
    parent, _, leaf = path.rpartition("/")
    parent = parent or "/"
    return any(n == leaf for n, _ in ls(image, parent))


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit("usage: cm_debloat_debugfs.py <image> <dir> [<dir> ...]")
    main(sys.argv[1], sys.argv[2:])
