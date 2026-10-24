"""Exercise the packaged hook without touching a device or host services."""

import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[2]


class PickerHookTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="nw-picker-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.data = self.root / "contents/wmmod-android"
        self.picker = self.data / "boot/picker"
        self.picker.mkdir(parents=True)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.sony = self.bin / "sony.sh"
        self.write_script(self.sony, 'echo SONY_CALLED\n')
        self.write_script(self.bin / "nvpflag", 'echo "$TEST_NVP_TST"\n')
        self.write_script(self.bin / "setprop", 'echo "SETPROP:$*"\n')
        self.write_script(
            self.bin / "busybox",
            'case "$1" in\n'
            'killall|sync) exit 0 ;;\n'
            'timeout) [ "$2" = -t ] || exit 97; t=$3; shift 3; exec timeout "$t" "$@" ;;\n'
            'setsid) shift; exec "$@" ;;\n'
            '*) exec "$@" ;;\n'
            'esac\n',
        )
        self.write_script(
            self.picker / "bootpick",
            'printf "PICKER:%s:%s:%s\\n" "$1" "$2" "$3"\n'
            '[ "$TEST_PICKER_CHOICE" != hang ] || exec sleep 30\n'
            'exit "$TEST_PICKER_CHOICE"\n',
        )
        (self.picker / "frames.raw").write_bytes(b"fixture frames")
        self.write_script(
            self.data / "boot/android-boot.sh", 'echo "ANDROID:$1"\n'
        )
        source = (ROOT / "picker-upg/update.sh").read_text()
        hook = source.split("<<'HOOK'\n", 1)[1].split("\nHOOK\n", 1)[0]
        replacements = (
            ("SONY=/system/bin/bootswitcher.sony.sh", f"SONY={self.sony}"),
            ("BB=/system/xbin/busybox", f"BB={self.bin / 'busybox'}"),
            ("DIR=/contents/wmmod-android", f"DIR={self.data}"),
            ("/bin/nvpflag", str(self.bin / "nvpflag")),
            ("/bin/setprop", str(self.bin / "setprop")),
            ("/tmp/bootpick", str(self.root / "bootpick")),
            ("SECONDS_AT_MOST=180", "SECONDS_AT_MOST=1"),
        )
        self.hook = self.root / "hook.sh"
        for old, new in replacements:
            hook = hook.replace(old, new)
        self.hook.write_text(hook + "\n")

    @staticmethod
    def write_script(path, body):
        path.write_text("#!/bin/sh\n" + body)
        path.chmod(0o755)

    def run_hook(self, choice=1, diagnostic="0xFFFFFFFF"):
        env = {
            **os.environ,
            "TEST_PICKER_CHOICE": str(choice),
            "TEST_NVP_TST": diagnostic,
        }
        result = subprocess.run(
            ["sh", str(self.hook)], env=env, capture_output=True,
            text=True, timeout=5, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def test_walkman_waits_fifteen_seconds(self):
        result = self.run_hook()
        self.assertIn(f"PICKER:{self.picker / 'frames.raw'}:1:15\n", result.stdout)
        self.assertIn("SONY_CALLED", result.stdout)
        self.assertEqual((self.data / "boot-choice").read_text(), "1\n")

    def test_saved_android_choice_is_passed_to_picker(self):
        (self.data / "boot-choice").write_text("2\n")
        result = self.run_hook()
        self.assertIn(f"PICKER:{self.picker / 'frames.raw'}:2:15\n", result.stdout)

    def test_android_starts_detached_and_saves_choice(self):
        result = self.run_hook(choice=2)
        self.assertNotIn("SONY_CALLED", result.stdout)
        self.assertEqual((self.data / "boot-choice").read_text(), "2\n")
        log = self.data / "boot-android.log"
        deadline = time.monotonic() + 1
        while time.monotonic() < deadline:
            if log.exists() and log.read_text() == "ANDROID:boot\n":
                break
            time.sleep(0.005)
        self.assertEqual(log.read_text(), "ANDROID:boot\n")

    def test_picker_failure_falls_back_without_saving_error(self):
        (self.data / "boot-choice").write_text("2\n")
        result = self.run_hook(choice=70)
        self.assertIn("SONY_CALLED", result.stdout)
        self.assertEqual((self.data / "boot-choice").read_text(), "2\n")

    def test_hung_picker_is_ended_and_falls_back(self):
        (self.data / "boot-choice").write_text("2\n")
        result = self.run_hook(choice="hang")
        self.assertIn("SONY_CALLED", result.stdout)
        self.assertEqual((self.data / "boot-choice").read_text(), "2\n")

    def test_missing_picker_skips_to_sony(self):
        (self.picker / "bootpick").unlink()
        result = self.run_hook()
        self.assertEqual(result.stdout, "SONY_CALLED\n")
        self.assertFalse((self.data / "boot-choice").exists())

    def test_diagnostic_boot_skips_picker(self):
        for flag in ("0x4D545346", "0x4D545354"):
            with self.subTest(flag=flag):
                self.assertEqual(self.run_hook(diagnostic=flag).stdout, "SONY_CALLED\n")

    def test_missing_sony_copy_starts_normal_bootmode(self):
        self.sony.unlink()
        self.assertEqual(self.run_hook().stdout, "SETPROP:sys.sony.bootmode 1\n")


if __name__ == "__main__":
    unittest.main()
