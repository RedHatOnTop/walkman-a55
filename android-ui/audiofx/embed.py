#!/usr/bin/env python3
"""Put libwalkmanfx.so and its audio_effects.conf entries into a cm-12.1 system.img.

    python3 -I android-ui/audiofx/embed.py <system.img> [libwalkmanfx.so]

The library defaults to build/audiofx/armeabi-v7a/libwalkmanfx.so (make android).
debugfs -w writes the image without a mount or root, as tools/cm_debloat_debugfs.py
does; e2fsck -fy follows. Nothing else may write the image meanwhile.

mediaserver reads /vendor/etc/audio_effects.conf when there is one (this image has
it) and /etc/audio_effects.conf otherwise, so both get the entries. New files take
the SELinux label of the file they replace or sit beside. Run again, it replaces
the library and leaves configs that already name it as they are.
"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_SO = os.path.join(HERE, "..", "build", "audiofx", "armeabi-v7a", "libwalkmanfx.so")
SO_PATH = "/lib/soundfx/libwalkmanfx.so"
CONFS = ["/vendor/etc/audio_effects.conf", "/etc/audio_effects.conf"]
UUID = "7c3e4afc-7189-4cf4-9ad2-5c37724cd6c7"   # WALKMANFX_UUID in fxsettings.h
BLOCK = 4096

LIBRARY = """  walkmanfx {
    path /system/lib/soundfx/libwalkmanfx.so
  }
"""
EFFECT = f"""  walkmanfx {{
    library walkmanfx
    uuid {UUID}
  }}
"""
# Every music session gets the effect when it starts; the policy enables it.
SESSION = """
# WalkmanUI sound effects (android-ui/audiofx) on every music session.
output_session_processing {
  music {
    walkmanfx {
    }
  }
}
"""


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


def exists(image, path):
    return "Inode:" in debugfs(image, f"stat {path}", quiet=True).stdout


def dump(image, path, dest):
    debugfs(image, f"dump {path} {dest}")
    with open(dest, "rb") as f:
        return f.read()


def patched(text):
    if "libwalkmanfx.so" in text:
        return None
    if re.search(r"^output_session_processing\s*\{", text, re.M):
        sys.exit("this audio_effects.conf already has an output_session_processing block; "
                 "add walkmanfx to its music stream by hand")
    for section, entry in (("libraries", LIBRARY), ("effects", EFFECT)):
        match = re.search(rf"^{section}\s*\{{\n", text, re.M)
        if not match:
            sys.exit(f"no {section} block in audio_effects.conf")
        text = text[:match.end()] + entry + text[match.end():]
    return text + SESSION


def free_blocks(image):
    out = subprocess.run(["dumpe2fs", "-h", image], capture_output=True, text=True, check=True).stdout
    return int(re.search(r"^Free blocks:\s+(\d+)", out, re.M).group(1))


def main(image, so):
    if not os.path.isfile(so):
        sys.exit(f"missing {so}: run make -C android-ui/audiofx android")
    with tempfile.TemporaryDirectory() as tmp:
        label = os.path.join(tmp, "label")
        debugfs(image, f"ea_get -f {label} /lib/soundfx security.selinux")
        if not os.path.getsize(label):
            sys.exit("cannot read the SELinux label of /lib/soundfx")

        files = [(so, SO_PATH)]
        for conf in CONFS:
            if not exists(image, conf):
                continue
            text = patched(dump(image, conf, os.path.join(tmp, "old")).decode())
            if text is None:
                print(f"{conf}: already names libwalkmanfx.so")
                continue
            local = os.path.join(tmp, conf.strip("/").replace("/", "_"))
            with open(local, "w") as f:
                f.write(text)
            files.append((local, conf))

        # The image may sit at resize2fs -M with no blocks to spare.
        needed = sum(-(-os.path.getsize(local) // BLOCK) for local, _ in files) + 4
        if free_blocks(image) < needed:
            size = os.path.getsize(image) // BLOCK + needed
            subprocess.run(["e2fsck", "-fy", image], capture_output=True)
            os.truncate(image, size * BLOCK)
            subprocess.run(["resize2fs", image, str(size)], check=True, capture_output=True)
            print(f"grew the image to {size} blocks")

        commands = []
        for local, path in files:
            directory, name = path.rsplit("/", 1)
            if exists(image, path):
                commands.append(f"rm {path}")
            commands += [
                f"cd {directory}",
                f"write {local} {name}",
                f"sif {path} mode 0100644",
                f"sif {path} uid 0",
                f"sif {path} gid 0",
                f"ea_set -f {label} {path} security.selinux",
            ]
        debugfs(image, *commands, write=True)

        check = subprocess.run(["e2fsck", "-fy", image], capture_output=True, text=True)
        if check.returncode > 1:
            sys.exit(f"e2fsck {check.returncode}:\n{check.stdout}{check.stderr}")

        for local, path in files:
            with open(local, "rb") as f:
                if dump(image, path, os.path.join(tmp, "back")) != f.read():
                    sys.exit(f"{path} does not read back as written")
            print(f"{path}: {os.path.getsize(local)} bytes")


if __name__ == "__main__":
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2] if len(sys.argv) == 3 else DEFAULT_SO)
