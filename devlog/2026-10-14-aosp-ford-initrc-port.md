# Phase A: container init.rc/default.prop ported to the AOSP/CM ford system (host-only)

2026-10-14. Follows [[2026-10-14-aosp-ford-acquired-verified]]. All host-side, no device. This is
the integration step that section 4 of [[2026-10-14-aosp-ford-acquisition-plan]] called for; after
it the only thing between us and a first AOSP boot is the device run itself.

## Inputs taken from the ROM

Unpacked the ROM `boot.img` ramdisk (MTK boot image; carved the ramdisk past the page-aligned
kernel, no MTK sub-header on the ramdisk, gzip+cpio). The authoritative classpaths come from its
`init.environ.rc`:

- `BOOTCLASSPATH` = core-libart..apache-xml (13 jars, NO Amazon `fos*`/icu/webviewext).
- `SYSTEMSERVERCLASSPATH` = `org.cyanogenmod.platform.jar:org.cyanogenmod.hardware.jar:services.jar:
  ethernet-service.jar:wifi-service.jar` (NO `fosinit`/`fosservices`/`AWSRemote*`).
- `export LD_PRELOAD libsigchain.so` (ART signal-chain shim), `ro.zygote=zygote32`.

## What changed (variant, not clobber — the Fire path stays the control)

New files:
- `probe/android/container/init.aosp.rc` — a copy of `init.rc` with exactly these edits (a string
  patch that asserts each hit once): BOOTCLASSPATH truncated at `apache-xml.jar`;
  SYSTEMSERVERCLASSPATH replaced with CM's; the Amazon `AMAZON_EXTRA_RESOURCE_LIST` export dropped
  and `export LD_PRELOAD libsigchain.so` added in its place; `amz_group` removed from mediaserver's
  group list; `setprop persist.panel.orientation 0` removed (AOSP SF reads `ro.sf.hwrotation`
  instead). Everything else (the generic Android-container plumbing: emulated-storage env, the
  post-fs-data dir tree, lmkd socket, servicemanager uid 100, vold with no fstab, dex2oat RAM caps)
  is kept verbatim — it is Android-5.1-on-this-container glue, not Fire-specific.
- `probe/android/container/default.aosp.prop` — like `default.prop` but drops the Fire imp
  `ro.product.config.type` and adds `ro.sf.hwrotation=0` (first-writer-wins over the ford
  build.prop's 270; the upright value measured on this 480x800 panel, to confirm on first capture).

Changed:
- `tools/android-container.sh` — `NW_CONTAINER_VARIANT` env (default `fire`) selects which
  init.rc/default.prop pair to bundle; both are always installed as `/init.rc` and `/default.prop`.
  The Amazon static `init`/`healthd`/`ueventd` are shared by both variants (no ramdisk swap).
- `tools/tests/test_android_container.py` — `assemble(variant=)` plus two tests (aosp variant
  packages the aosp files with no Amazon jars/group and the CM classpath; unknown variant fails).

## Verification (host)

- BOOTCLASSPATH and SYSTEMSERVERCLASSPATH in `init.aosp.rc` are byte-identical to the ROM's
  `init.environ.rc` (diffed).
- `bash -n` clean; full container test suite 8/8 green.
- Real `NW_CONTAINER_VARIANT=aosp tools/android-container.sh` builds a 347,486-byte tarball whose
  `/init.rc` carries the CM SYSTEMSERVERCLASSPATH and `/default.prop` carries `ro.sf.hwrotation=0`
  with no `ro.product.config.type`. Rebuilt the default (fire) tarball afterwards so the on-disk
  `container.tgz` still matches the device's current Fire state.
- Earlier blob/symbol gate (sonnet worker, see acquired-verified devlog): GO — 10/12 graphics
  blobs incl. `libMali.so` are sha256-identical to our Fire `system.img.orig`, Mali API 401
  confirmed in disassembly, no boot-blocking undefined symbol in any core binary.

## First boot (device, when the operator is present)

```
# reboot -> adb root -> push+insmod swap_accounting_guard.ko (verify [permanent]) -> fresh /data
NW_DROP_SYS_BOOT=1 NW_SYS_IMG=work/android/aosp-ford/system.img \
  NW_CONTAINER_VARIANT=aosp tools/android-boot.sh up
```

Expect a long first boot (full dex2oat, R2). Watch for: init SELinux/reboot (R1, userdebug should
be fine + CAP_SYS_BOOT dropped), zygote classpath errors (should be none now), SetupWizard instead
of home (bypass with `settings put global device_provisioned 1; settings put secure
user_setup_complete 1`). Pass = Trebuchet upright on the Sony panel, no `Amazon/full_ford`
fingerprint, `Boot is finished`. See [[nw-a55-aosp-ford-image]].
