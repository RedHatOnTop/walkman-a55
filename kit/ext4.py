"""An ext4 image file edited with debugfs, without a mount or root.

fuse2fs cannot write the ford image and a loop mount needs root, so every image
step of the kit goes through debugfs, as tools/cm_debloat_debugfs.py and
tools/cm_replace_app.py do. debugfs -f carries on past a failed command and
exits 0; its errors are the stderr lines after the version banner, and any of
them stops the kit.
"""
import os
import re
import subprocess
import sys
import tempfile

BLOCK = 4096
LABEL = "security.selinux"
S_IFMT, S_IFDIR = 0o170000, 0o040000


class Ext4:
    def __init__(self, path):
        self.path = path

    def run(self, *commands, write=False, quiet=False):
        with tempfile.NamedTemporaryFile("w", suffix=".cmd", delete=False) as script:
            script.write("\n".join(commands) + "\n")
        try:
            args = ["debugfs"] + (["-w"] if write else []) + ["-f", script.name, self.path]
            out = subprocess.run(args, capture_output=True, text=True, errors="surrogateescape", check=True)
        finally:
            os.unlink(script.name)
        errors = [line for line in out.stderr.splitlines() if line and not line.startswith("debugfs ")]
        if errors and not quiet:
            sys.exit(f"debugfs {self.path}: " + "\n".join(errors))
        return out.stdout

    def exists(self, path):
        return "Inode:" in self.run(f"stat {path}", quiet=True)

    def is_dir(self, path):
        return "Type: directory" in self.run(f"stat {path}", quiet=True)

    def ls(self, path):
        """(name, mode, uid, gid) of each entry of the directory at path."""
        found = []
        for line in self.run(f"ls -l {path}", quiet=True).splitlines():
            f = line.split()
            if len(f) < 9 or f[-1] in (".", ".."):
                continue
            try:
                found.append((f[-1], int(f[1], 8), int(f[3]), int(f[4])))
            except ValueError:
                continue
        return found

    def label(self, path):
        """The SELinux label's raw bytes, or None when the file has none."""
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "label")
            self.run(f"ea_get -f {out} {path} {LABEL}", quiet=True)
            if not os.path.exists(out) or not os.path.getsize(out):
                return None
            with open(out, "rb") as f:
                return f.read()

    def get(self, path, local):
        self.run(f"dump {path} {local}")
        return local

    def read(self, path):
        with tempfile.TemporaryDirectory() as tmp:
            with open(self.get(path, os.path.join(tmp, "f")), "rb") as f:
                return f.read()

    def rm_tree(self, path):
        """Remove a file, or a directory and everything under it."""
        commands = []

        def walk(p):
            for name, mode, _, _ in self.ls(p):
                child = f"{p}/{name}"
                if mode & S_IFMT == S_IFDIR:
                    walk(child)
                    commands.append(f"rmdir {child}")
                else:
                    commands.append(f"rm {child}")

        if not self.exists(path):
            sys.exit(f"{path} is not in {self.path}")
        if self.is_dir(path):
            walk(path)
            commands.append(f"rmdir {path}")
        else:
            commands.append(f"rm {path}")
        self.run(*commands, write=True)

    def _attrs(self, path, mode, uid, gid, label, labels):
        commands = [f"sif {path} mode 0{mode:o}", f"sif {path} uid {uid}", f"sif {path} gid {gid}"]
        if label is not None:
            local = os.path.join(labels, f"{len(os.listdir(labels))}")
            with open(local, "wb") as f:
                f.write(label)
            commands.append(f"ea_set -f {local} {path} {LABEL}")
        return commands

    def mkdir(self, path, mode=0o755, uid=0, gid=0, label=None):
        with tempfile.TemporaryDirectory() as labels:
            self.run(f"mkdir {path}", *self._attrs(path, S_IFDIR | mode, uid, gid, label, labels), write=True)

    def put(self, files):
        """Write [(local, path, mode, uid, gid, label)], replacing what is there.
        A label of None keeps the replaced file's label, if it had one."""
        self.reserve(sum(-(-os.path.getsize(local) // BLOCK) for local, *_ in files) + 16)
        commands = []
        with tempfile.TemporaryDirectory() as labels:
            for local, path, mode, uid, gid, label in files:
                if self.exists(path):
                    if label is None:
                        label = self.label(path)
                    commands.append(f"rm {path}")
                directory, name = path.rsplit("/", 1)
                commands += [f"cd {directory or '/'}", f"write {local} {name}"]
                commands += self._attrs(path, 0o100000 | mode, uid, gid, label, labels)
            self.run(*commands, write=True)
        for local, path, *_ in files:
            with open(local, "rb") as f:
                if self.read(path) != f.read():
                    sys.exit(f"{path} does not read back as written")

    def free_blocks(self):
        out = subprocess.run(["dumpe2fs", "-h", self.path], capture_output=True, text=True, check=True).stdout
        return int(re.search(r"^Free blocks:\s+(\d+)", out, re.M).group(1))

    def reserve(self, blocks):
        """Grow the image when fewer than this many blocks are free."""
        if self.free_blocks() >= blocks:
            return
        self.fsck()
        size = os.path.getsize(self.path) // BLOCK + blocks
        os.truncate(self.path, size * BLOCK)
        subprocess.run(["resize2fs", self.path, str(size)], check=True, capture_output=True)

    def fsck(self):
        check = subprocess.run(["e2fsck", "-fy", self.path], capture_output=True, text=True)
        if check.returncode > 1:
            sys.exit(f"e2fsck {check.returncode}:\n{check.stdout}{check.stderr}")

    def shrink(self):
        self.fsck()
        subprocess.run(["resize2fs", "-M", self.path], check=True, capture_output=True)
        # resize2fs -M leaves the resize inode's i_size short; e2fsck sets it.
        self.fsck()
        check = subprocess.run(["e2fsck", "-fn", self.path], capture_output=True, text=True)
        if check.returncode:
            sys.exit(f"e2fsck -fn {check.returncode} after the shrink:\n{check.stdout}{check.stderr}")
