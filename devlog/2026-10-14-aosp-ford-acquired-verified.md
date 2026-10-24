# Phase A: the ford CM-12.1 ROM is downloaded, extracted to a raw ext4 system.img, and verified

2026-10-14. Executes the plan in [[2026-10-14-aosp-ford-acquisition-plan]] (host-only, no device
touched). The AFH download that the plan left to the operator turned out to be scriptable, so the
acquisition + extraction + host gates G0-G2 are done. The zip and the derived image live under
`work/android/aosp-ford/` (gitignored, never committed). Serial-nothing: no adb this run.

## What ran

1. **Download.** AFH's mirror API answered a plain POST
   (`libs/otf/mirrors.otf.php`, `action=getdownloadmirrors&fid=4349826312261799008`) with a direct
   mirror URL; `curl` pulled the 249,854,976-byte zip. **MD5 = `5fa7b3fdc29ae0693afe80f439c69631`,
   matches the plan (G0 pass).**
2. **Extract.** `sdat2img` on `system.transfer.list` (v3, 113977 new blocks) + `system.new.dat`
   produced `work/android/aosp-ford/system.img`, **1,258,291,200 B (1200 MiB) valid ext4** (`file`
   confirms `Linux rev 1.0 ext4 filesystem data (extents) (large files)`), exactly the ford
   `system` partition size.
3. **Inspect** via rootless `fuse2fs -o ro`, `readelf`/`nm`/`strings` on the extracted files (read
   as data; nothing from the image was executed).

## Verified deltas vs the acquisition plan (what the plan had to leave [U])

- **CORRECTION — packaging is BLOCK-BASED, not file-tree.** The plan's section 3.1 predicted a
  `system/` file tree and said `sdat2img` "does not apply here". The zip actually contains
  `system.new.dat` (466 MB) + `system.transfer.list` + `system.patch.dat` (0 B), and
  `updater-script` calls `block_image_update(...)`. So the whole section-3.2 chain (unzip tree ->
  replay `symlink`/`set_metadata` -> `fakeroot mke2fs -d`) is **not needed**; `sdat2img` yields a
  ready ext4 image with symlinks, modes and selabels already baked in. This is simpler and more
  faithful than the file-tree rebuild. (The repo already ships `tools/sdat2img.py`; a scratchpad
  copy was used this run — the runbook should call the repo tool.)
- **CONFIRMED — `ro.build.type=userdebug`** (fingerprint `google/cm_ford/ford:5.1.1/LMY49M/
  484994cb82:userdebug/test-keys`). The plan's R1 top risk (a `user`-build init rebooting the
  Walkman on `selinux=disabled`) is **largely defused**: userdebug compiles `ALLOW_DISABLE_SELINUX`,
  so the ROM's own init would honour `androidboot.selinux=disabled/permissive`. First boot still
  keeps the Amazon static init (plan section 4) as the proven path; R1 is now low either way.
- **CONFIRMED — `lab126_log_write` defined in the ROM's `liblog.so` AND `libcutils.so`** (both `T`,
  no undefined refs). G2 for the log shim passes: no shim needed, matches the plan's -DAMAZON_LOG
  reasoning. (`__vitals_log_print` not re-checked this run; the plan found it imported only by
  `libMtkOmxAIVPlayer.so` + logd, so it is not a boot blocker.)
- **CONFIRMED — graphics blobs are MediaTek Mali of our DDK.** `lib/libMali.so` strings
  `Mali online shader compiler r4p0-00rel0 [Revision 96995]` and carries the device-driver API
  version-check code; the set is `libMali.so`, `libmtk_mali_user.so`, `libgralloc_extra.so`,
  `lib/egl/{libEGL,libGLESv1_CM,libGLESv2}_mali.so`. r4p0-00rel0 is the DDK our ported
  `mali_r4p0.ko` is built for, and the real Fire 7 runs this exact blob on mt8127 mali r4p0. R3 is
  low. RESIDUAL: the exact API constant 401 was not independently read from the ioctl
  (`docs/android.md:74`); r4p0-00rel0 is strong proxy evidence. sha256 cross-check vs
  `system.img.orig`'s blobs is deferred to pre-boot.
- **CONFIRMED — R2 is real: the image is NOT dexpreopted.** No `framework/arm/boot.oat`/`boot.art`,
  zero `.odex` under app/priv-app/framework, 72 APKs. First boot therefore runs full `dex2oat`
  (boot image + 72 apps) on 2x A7 / 456 MB — the exact load that preceded the earlier hard resets
  (`devlog/2026-10-11-android-container.md:308-314`), now mitigated by the swap-accounting guard
  kmod. **Plan accordingly: guard kmod loaded first, zram up, a FRESH `/data` image, persistent
  dalvik-cache, and expect a long first boot.**
- **CONFIRMED — clean de-Amazon'd system.** No `fos*`/`amazon`/`awsremote` jars in `/framework`;
  `app_process -> app_process32` (32-bit, correct for API 22). SetupWizard is present
  (`Provision`, `CyanogenSetupWizard`, `ManagedProvisioning`) + `Trebuchet` launcher, so reaching
  home needs the provisioning bypass (`settings put global device_provisioned 1; settings put
  secure user_setup_complete 1`, plan section 4).
- **CONFIRMED — device identity.** `ro.product.device=ford`, `ro.board.platform=mt8127`,
  `ro.product.board=ford`, API 22 / 5.1.1 — exact ABI match to our `Amazon/full_ford/ford:5.1.1`
  userland. (Corrects an earlier verbal claim of mine that ford = "Fire HD 7 2014"; ford is the
  **Fire 7 2015, 5th gen, MT8127, Mali-450**, as `docs/android.md:68` and the plan already state.)

## Host change made

`tools/android-boot.sh:22` now reads `IMG="${NW_SYS_IMG:-$ROOT/.../fire7-unpack/system.img}"` — an
env override so a boot can select the AOSP image without editing the script or clobbering the Fire
image, and to dodge the `push_once` size-only trap (the AOSP image is the same 1200 MiB as the
pristine Fire image). Default behaviour unchanged.

## Next (host-side, then the device milestone)

1. **init.rc / container port** (plan section 4, reversible host work): extract the ROM `boot.img`
   ramdisk, read the real `BOOTCLASSPATH`/`SYSTEMSERVERCLASSPATH` from its `init.environ.rc`,
   replace the Amazon jars in `probe/android/container/init.rc:54-55`, drop `amz_group` (`:237`)
   and `AMAZON_EXTRA_RESOURCE_LIST` (`:61`), set `ro.sf.hwrotation`/density in the container
   `default.prop`, parametrize the ramdisk source in `tools/android-container.sh` and keep
   `tools/tests/test_android_container.py` green.
2. **Device first boot** (the milestone to watch on the panel): reboot -> root -> guard kmod ->
   fresh `/data` -> `NW_DROP_SYS_BOOT=1 NW_SYS_IMG=.../aosp-ford/system.img tools/android-boot.sh
   up`. Expect the long dex2oat; pass criteria per plan section 6.4 (Trebuchet upright, no Amazon
   fingerprint, `Boot is finished`).

See [[nw-a55-aosp-feasibility]], [[nw-a55-android-roadmap]].
