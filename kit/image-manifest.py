#!/usr/bin/env python3
"""Print an ext4 image's contents as a sorted manifest, one line per path:

  path  type  mode  uid:gid  size  sha256-or-link-target  xattrs

Two builds of the same tree compare equal here even when debugfs placed their
blocks and inodes differently, which byte equality of the images does not.
Timestamps, inode numbers and flags are left out for the same reason.

The files are dumped with debugfs rdump into a temporary directory next to
the image (on its filesystem, not /tmp) and gone when this exits.

Usage: image-manifest.py <image> > manifest.tsv
"""

import hashlib
import os
import re
import subprocess
import sys
import tempfile


def debugfs(img, cmds):
    with tempfile.NamedTemporaryFile("w", suffix=".cmds") as f:
        f.write("\n".join(cmds) + "\n")
        f.flush()
        r = subprocess.run(["debugfs", "-f", f.name, img], capture_output=True, text=True, errors="surrogateescape")
    return r.stdout


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    img = sys.argv[1]
    if not os.path.isfile(img):
        sys.exit(f"no {img}")
    with tempfile.TemporaryDirectory(prefix=".manifest.", dir=os.path.dirname(os.path.abspath(img))) as tmp:
        top = [
            line.split()[-1]
            for line in debugfs(img, ["ls -l /"]).splitlines()
            if not line.startswith("debugfs") and line.strip()
        ]
        top = [t for t in top if t not in (".", "..", "lost+found")]
        debugfs(img, [f"rdump /{t} {tmp}" for t in top])

        paths = {"/": None}
        for d, dirs, files in os.walk(tmp):
            for name in dirs + files:
                full = os.path.join(d, name)
                p = "/" + os.path.relpath(full, tmp)
                if re.search(r'[\s"\\]', p):
                    sys.exit(f"{img}: path debugfs cannot quote: {p!r}")
                paths[p] = full
            for name in dirs:
                if not os.path.islink(os.path.join(d, name)):
                    os.chmod(os.path.join(d, name), 0o700)

        out = debugfs(img, [f"stat {p}" for p in sorted(paths)])
        stats = {}
        for block in re.split(r"^debugfs: stat ", out, flags=re.M)[1:]:
            p, _, body = block.partition("\n")
            stats[p] = body

        for p in sorted(paths):
            body = stats.get(p)
            if body is None or "Inode:" not in body:
                sys.exit(f"{img}: no stat for {p}")
            kind = re.search(r"Type: (\S+)", body).group(1)
            mode = re.search(r"Mode:\s+(\d+)", body).group(1)
            uid = re.search(r"User:\s+(\d+)", body).group(1)
            gid = re.search(r"Group:\s+(\d+)", body).group(1)
            size = re.search(r"Size: (\d+)", body).group(1)
            xattrs = []
            if "Extended attributes:" in body:
                for line in body.split("Extended attributes:\n", 1)[1].splitlines():
                    if not line.startswith("  "):
                        break
                    xattrs.append(line.strip())
            full = paths[p]
            if kind == "regular":
                os.chmod(full, 0o600)
                data = sha256(full)
            elif kind == "symlink":
                data = "->" + os.readlink(full)
            else:
                data = "-"
            if kind == "directory":
                size = "-"
            print("\t".join([p, kind, mode, f"{uid}:{gid}", size, data, ";".join(sorted(xattrs))]))


main()
