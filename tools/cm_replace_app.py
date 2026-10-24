#!/usr/bin/env python3
# Replace one embedded /system/app with a newer build of the same APK, in a
# cm-12.1 ext4 image, using debugfs -- no mount (see cm_debloat_debugfs.py for
# why). The APK and its armeabi-v7a libs pre-extracted to lib/arm/ are swapped
# the way cm_customize.py first laid them out; owner and modes are kept from the
# files being replaced.
#
# Usage: cm_replace_app.py <image> <Name> <apk>
#   e.g. cm_replace_app.py work/android/aosp-ford/system.img WalkmanUI \
#            android-ui/build/android/WalkmanUI.apk
#
# The image may sit at resize2fs -M; it is grown only by what the new files
# need beyond the blocks the old ones free. Nothing else may write the image
# meanwhile. e2fsck -fy runs at the end and every written file is read back.
import os
import re
import subprocess
import sys
import tempfile
import zipfile

APK_ABI = "armeabi-v7a"
SYS_ABI = "arm"
BLOCK = 4096


# debugfs -f carries on past a failed command and exits 0, so its errors are
# the lines on stderr after the version banner.
def debugfs(image, *commands, write=False, quiet=False):
    with tempfile.NamedTemporaryFile("w", suffix=".cmd", delete=False) as script:
        script.write("\n".join(commands) + "\n")
    try:
        args = ["debugfs"] + (["-w"] if write else []) + ["-f", script.name, image]
        out = subprocess.run(args, capture_output=True, text=True, check=True)
    finally:
        os.unlink(script.name)
    errors = [line for line in out.stderr.splitlines() if line and not line.startswith("debugfs ")]
    if errors and not quiet:
        sys.exit("debugfs: " + "\n".join(errors))
    return out


def entries(image, path):
    for line in debugfs(image, f"ls -l {path}", quiet=True).stdout.splitlines():
        f = line.split()
        if len(f) < 9 or f[-1] in (".", ".."):
            continue
        try:
            mode = int(f[1], 8)
        except ValueError:
            continue
        yield f[-1], mode, int(f[3]), int(f[4])


def free_blocks(image):
    out = subprocess.run(["dumpe2fs", "-h", image], capture_output=True, text=True, check=True).stdout
    return int(re.search(r"^Free blocks:\s+(\d+)", out, re.M).group(1))


def main(image, name, apk):
    app = f"/app/{name}"
    lib = f"{app}/lib/{SYS_ABI}"
    old = {n: (m, u, g) for n, m, u, g in entries(image, app)}
    if f"{name}.apk" not in old:
        sys.exit(f"{app}/{name}.apk is not in {image}")
    _, uid, gid = old[f"{name}.apk"]
    old_libs = [n for n, _, _, _ in entries(image, lib)]

    with tempfile.TemporaryDirectory() as tmp:
        files = [(apk, f"{app}/{name}.apk")]
        with zipfile.ZipFile(apk) as z:
            for member in z.namelist():
                if member.startswith(f"lib/{APK_ABI}/") and member.endswith(".so"):
                    local = os.path.join(tmp, os.path.basename(member))
                    with z.open(member) as src, open(local, "wb") as dst:
                        dst.write(src.read())
                    files.append((local, f"{lib}/{os.path.basename(member)}"))
        if len(files) > 1 and "lib" not in old:
            sys.exit(f"{app} has no lib/ dir; embed it with cm_customize.py first")

        removals = [f"rm {app}/{name}.apk"] + [f"rm {lib}/{n}" for n in old_libs]
        debugfs(image, *removals, write=True)

        needed = sum(-(-os.path.getsize(local) // BLOCK) for local, _ in files) + 16
        if free_blocks(image) < needed:
            size = os.path.getsize(image) // BLOCK + needed
            subprocess.run(["e2fsck", "-fy", image], capture_output=True)
            os.truncate(image, size * BLOCK)
            subprocess.run(["resize2fs", image, str(size)], check=True, capture_output=True)
            print(f"grew the image to {size} blocks")

        commands = []
        for local, path in files:
            directory, base = path.rsplit("/", 1)
            commands += [
                f"cd {directory}",
                f"write {local} {base}",
                f"sif {path} mode 0100644",
                f"sif {path} uid {uid}",
                f"sif {path} gid {gid}",
            ]
        debugfs(image, *commands, write=True)

        check = subprocess.run(["e2fsck", "-fy", image], capture_output=True, text=True)
        if check.returncode > 1:
            sys.exit(f"e2fsck {check.returncode}:\n{check.stdout}{check.stderr}")

        back = os.path.join(tmp, "back")
        for local, path in files:
            debugfs(image, f"dump {path} {back}")
            with open(local, "rb") as a, open(back, "rb") as b:
                if a.read() != b.read():
                    sys.exit(f"{path} does not read back as written")

    print(f"{app}/{name}.apk: {os.path.getsize(apk)} bytes, {len(files) - 1} libs in lib/{SYS_ABI} "
          f"(was {len(old_libs)}), uid {uid} gid {gid}")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit("usage: cm_replace_app.py <image> <Name> <apk>")
    main(*sys.argv[1:])
