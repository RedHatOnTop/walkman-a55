import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "probe/android/container"
DEVICE_TYPE_KEY = "ro.product.config.type"
DEVICE_TYPE = "AIQC11IVNRMBB"


class AndroidContainerTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="nw-container-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.src = self.root / "probe/android/container"
        shutil.copytree(CONFIG, self.src)
        (self.root / "tools").mkdir()
        shutil.copy2(ROOT / "tools/android-container.sh", self.root / "tools")
        self.ramdisk = self.root / "work/android/fire-ramdisk"
        (self.ramdisk / "sbin").mkdir(parents=True)
        for name, content in (
            ("init", b"fixture-init\n"),
            ("sbin/healthd", b"fixture-healthd\n"),
            ("ueventd.rc", b"# fixture common rules\n"),
            ("ueventd.mt8127.rc", b"# fixture platform rules\n"),
        ):
            path = self.ramdisk / name
            path.write_bytes(content)
            path.chmod(0o755 if name in ("init", "sbin/healthd") else 0o644)
        self.tarball = self.root / "work/android/container.tgz"
        self.fence = self.root / "fence"
        self.fence.mkdir()
        adb = self.fence / "adb"
        adb.write_text('#!/bin/sh\necho "unexpected adb invocation" >&2\nexit 97\n')
        adb.chmod(0o755)

    def assemble(self, variant=None):
        env = {**os.environ, "PATH": f"{self.fence}:{os.environ['PATH']}",
               "NW_SERIAL": "HOST_TEST_NO_DEVICE"}
        if variant is not None:
            env["NW_CONTAINER_VARIANT"] = variant
        return subprocess.run(
            ["bash", str(self.root / "tools/android-container.sh")],
            cwd=self.root,
            env=env,
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        )

    def test_device_type_is_present_once(self):
        assignments = [line.strip() for line in (CONFIG / "default.prop").read_text().splitlines()
                       if line.strip() and not line.lstrip().startswith("#")]
        matches = [line for line in assignments if line.split("=", 1)[0] == DEVICE_TYPE_KEY]
        self.assertEqual(matches, [f"{DEVICE_TYPE_KEY}={DEVICE_TYPE}"])
        keys = [line.split("=", 1)[0] for line in assignments]
        self.assertEqual(len(keys), len(set(keys)))

    def test_tarball_preserves_payload(self):
        result = self.assemble()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        with tarfile.open(self.tarball) as archive:
            for name in ("init.rc", "default.prop", "devclean.sh"):
                self.assertEqual(archive.extractfile(f"./{name}").read(),
                                 (CONFIG / name).read_bytes())
            for name in ("init", "sbin/healthd", "ueventd.rc"):
                self.assertEqual(archive.extractfile(f"./{name}").read(),
                                 (self.ramdisk / name).read_bytes())
            rules = (self.ramdisk / "ueventd.mt8127.rc").read_bytes()
            self.assertEqual(archive.extractfile("./ueventd.icx.rc").read(),
                             rules + (CONFIG / "ueventd.extra.rc").read_bytes())
            self.assertEqual(archive.getmember("./sbin/ueventd").linkname, "../init")
            for name in ("init", "sbin/healthd", "devclean.sh"):
                self.assertEqual(archive.getmember(f"./{name}").mode, 0o755)
            self.assertEqual(archive.getmember("./default.prop").mode, 0o644)

    def test_packaged_device_type_is_present_once(self):
        result = self.assemble()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        with tarfile.open(self.tarball) as archive:
            lines = archive.extractfile("./default.prop").read().decode().splitlines()
            matches = [line for line in lines if line.startswith(f"{DEVICE_TYPE_KEY}=")]
            self.assertEqual(matches, [f"{DEVICE_TYPE_KEY}={DEVICE_TYPE}"])
            names = archive.getnames()
            self.assertFalse(any(Path(name).name.startswith("fstab.") for name in names))
            init = archive.extractfile("./init.rc").read().decode()
            self.assertNotRegex(init, r"(?m)^service\s+devicetype\s")

    def test_aosp_variant_packages_aosp_files(self):
        result = self.assemble(variant="aosp")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        with tarfile.open(self.tarball) as archive:
            self.assertEqual(archive.extractfile("./init.rc").read(),
                             (CONFIG / "init.aosp.rc").read_bytes())
            self.assertEqual(archive.extractfile("./default.prop").read(),
                             (CONFIG / "default.aosp.prop").read_bytes())
            prop = archive.extractfile("./default.prop").read().decode()
            prop_keys = [line.split("=", 1)[0].strip() for line in prop.splitlines()
                         if line.strip() and not line.lstrip().startswith("#")]
            self.assertNotIn(DEVICE_TYPE_KEY, prop_keys)
            init = archive.extractfile("./init.rc").read().decode()
            for token in ("fosframework.jar", "amazon-remoteviews.jar",
                          "export AMAZON_EXTRA_RESOURCE_LIST", "amz_group"):
                self.assertNotIn(token, init)
            self.assertIn("org.cyanogenmod.platform.jar", init)
            self.assertIn("export LD_PRELOAD libsigchain.so", init)

    def test_unknown_variant_fails(self):
        result = self.assemble(variant="nope")
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("NW_CONTAINER_VARIANT", result.stderr)
        self.assertFalse(self.tarball.exists())

    def test_rebuild_removes_stale_container_files(self):
        result = self.assemble()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        (self.root / "work/android/container/stale").write_text("stale\n")
        result = self.assemble()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        with tarfile.open(self.tarball) as archive:
            self.assertNotIn("./stale", archive.getnames())

    def test_missing_inputs_fail_without_tarball(self):
        for path in (self.src / "init.rc", self.src / "default.prop",
                     self.src / "devclean.sh", self.src / "ueventd.extra.rc",
                     self.ramdisk / "init", self.ramdisk / "sbin/healthd",
                     self.ramdisk / "ueventd.rc", self.ramdisk / "ueventd.mt8127.rc"):
            with self.subTest(path=path.relative_to(self.root)):
                content, mode = path.read_bytes(), path.stat().st_mode
                path.unlink()
                try:
                    result = self.assemble()
                    self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn(str(path), result.stderr)
                    self.assertFalse(self.tarball.exists())
                finally:
                    path.write_bytes(content)
                    path.chmod(mode)

    def test_nonexecutable_init_fails(self):
        (self.ramdisk / "init").chmod(0o644)
        result = self.assemble()
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("unpack the Fire boot image first", result.stderr)
        self.assertFalse(self.tarball.exists())


if __name__ == "__main__":
    unittest.main()
