# Phase A acquisition + integration plan: community AOSP 5.1.1 `ford` /system in the container

2026-10-14. READ-ONLY desk study (repo files, one read-only `debugfs` dump of our own
`system.img.orig`, web). No adb, no device, no build, no download of any ROM zip, no sub-agents.
Nothing was committed; this file is the only write. Evidence tags:

- **[E]** established: a file/line/URL is cited and I read it.
- **[I]** inference from established facts (reasoning shown).
- **[U]** unverified / speculation.

## Recommended first action

Have the operator download **`lineage-12.1-20200505-UNOFFICIAL-ford.zip`** (238.3 MB, MD5
`5fa7b3fdc29ae0693afe80f439c69631`, AndroidFileHost fid `4349826312261799008`,
https://androidfilehost.com/?fid=4349826312261799008) in a browser into a fresh git-ignored
directory (`work/android/lineage12-dl/`, `/work/` is ignored by `.gitignore:8`), check the MD5, and run
only **listing-level triage** before anything is extracted: `unzip -l`, read
`META-INF/com/google/android/updater-script`, read `system/build.prop`, and answer three go/no-go
questions (section 6, gates G1-G2): is `/system` dexpreopted, is the bundled `libMali.so` still
API 401 like ours, and what is `ro.build.type`. The zip is a *file-tree* flashable zip, not an
image, so the next artifact to build is a small `zip -> system.img` converter (section 3). That is
days of work and needs no AOSP build environment, which shortens the "weeks, needs a 5.1 build
env" estimate in `devlog/2026-10-14-aosp-feasibility.md:303`. Booting it needs the device, which
another worker owns, so everything up to and including the image build and the init.rc port is
host-only and can start now.

## 0. Corrections to the brief and to the feasibility devlog

| # | Statement | Finding |
|---|---|---|
| 1 | Brief: "ford = Amazon Fire HD 7 (2014, 4th gen, MT8127)" | **Wrong.** `ford` is the **Fire 7 (2015), 5th generation**, MT8127, Mali-450. See section 1. The 2014 Fire HD 7 is a different machine (MT8135, PowerVR G6200, Fire OS 4 / Android 4.4 [U: web-search summary only, the hothardware/gsmarena pages were not opened]). The repo itself already says 5th gen: `docs/android.md:68` ("Fire 7 5세대(`ford`, MT8127)"). |
| 2 | Brief: `android-boot.sh` "binds the MT8127 graphics blobs we already provide" | **Not what the scripts do.** `probe/android/android-boot.sh:149` loop-mounts the *whole* Fire `system.img` ro, so the graphics blobs come from inside that image. The only binds are the stub audio HAL (`:162-170`), an empty UI-sounds dir (`:185-190`) and the idc overlay (`:194-203`). `work/android/fire7-unpack/mali/` (22 files: `gralloc.mt8127.so`, `hwcomposer.mt8127.so`, `libMali.so`, ...) is a leftover of the older chroot flow (`tools/android-gpu.sh`). After a swap the blobs come from the ROM unless we add an overlay step (risk R3). |
| 3 | Feasibility devlog: "chaosmaster/android_device_amazon_ford (branch cm-12.1) ... kernel chaosmaster/android_kernel_amazon_ford" are the cm-12.1 trees (`:129-131`) | They exist [E] but are not what the community builds were made from. chaosmaster's device repo has default branch **`twrp`** (branches `cm-12.1` f765607, `master`, `twrp`), its kernel repo has **2 commits** on `cm-12.1`, last push 2016-01-30. The 2017-2020 builds come from the `cm12-amami` source tree plus `NaphatrB/android_{device_amazon_ford,device_amazon_mt8127-common,kernel_amazon_mt8127-common}` (branch `cm-12.1`, pushed 2019-04/05), kernel upstreamed to 3.10.108. Section 2b. |
| 4 | Feasibility devlog: "a one-function shim" for `lab126_log_write` (`:179`) | For the blobs the ROM keeps it is **two** symbols (`lab126_log_write`, `__vitals_log_print`), and with a prebuilt Lineage zip **no shim is needed at all**: the ROM's own liblog already defines both under `-DAMAZON_LOG`. A shim only matters for a from-source build without that flag. Section 3.3. |
| 5 | Feasibility devlog open question 3: "check whether an old Lineage 12.1 ford `system.img` is still downloadable" (`:319-322`) | **Answered.** Nine Lineage 12.1 ford zips are still listed on AndroidFileHost; none is an image, all are file-tree flashable zips (`BLOCK_BASED_OTA := false`). Section 2a. |
| 6 | Feasibility devlog: "cm-12.1 ford ... ships a `selinux_permissive.ko`" (`:212-213`) | That module is in chaosmaster's **`twrp`**-branch tree (`modules/selinux_permissive`). The 2019 ford `BoardConfig.mk` uses the cmdline `androidboot.selinux=permissive` instead. Neither applies to us, but the init-side SELinux gate matters (R1), and the feasibility devlog did not mention it. |

What the feasibility devlog got right and this study confirms: cm-12.1 and cm-14.1 ford trees exist
(`:18-20`, `:127-137`); the board config carries `-DAMAZON_LOG -DADD_LEGACY_ACQUIRE_BUFFER_SYMBOL`
(`:179-180`); Lineage 12.1 ford has an XDA thread dated 05 May 2020 (`:140`).

## 1. ford = Fire 7 (2015), 5th generation, MT8127 [E]

- postmarketOS package `device-amazon-ford`: "Amazon Fire 7 (2015)", depends on
  `linux-postmarketos-mediatek-mt8127`, armv7 (https://pkgs.nura.eco/package/main/postmarketos/armv7/device-amazon-ford,
  the old `pkgs.postmarketos.org` URL 301-redirects there).
- XDA unlock thread "[UNLOCK][ROOT][TWRP][UNBRICK][DOWNGRADE] Fire 7 (ford and austin)": "This is for the
  5th gen Fire and 7th gen Fire ... 5th gen: amonet-ford-v1.4.1.zip, 7th gen: amonet-austin-v1.4.1.zip"
  (https://xdaforums.com/t/unlock-root-twrp-unbrick-downgrade-fire-7-ford-and-austin.3899860/page-105).
- https://gist.github.com/Juul/ac03f4e8c7f8e7cc2d67d40bf2174f4f : "Amazon Fire 2015 7" ... also known as the 5th
  generation 7" Amazon Fire or under the internal codename ford".
- https://www.alexgoldcheidt.com/amazon-fire-7-5th-gen-tablet-twrp/ : "5th-Gen tablet (codename: ford)",
  released September 2015, last FireOS 5.3.6.4 (matches our `ro.build.version.fireos=5.3.6.4`,
  `devlog/2026-10-14-aosp-feasibility.md:44-46`).
- Mainline: LKML series "[PATCH v3 0/4] ARM: Basic support for Amazon ford tablet (MT8127)", Zakariya
  Hadrami, 2026-09-08 (https://lkml.rescloud.iu.edu/2609.1/02670.html), new `mt8127-amazon-ford.dts`. It
  does not name the marketing model; relevant only to the Phase D research branch, not to this plan.
- Our Mali-450 userland (`libMali.so` r4p0, API 401, `docs/android.md:71`) is consistent with the 2015
  Fire 7; the 2014 Fire HD 7 used a PowerVR GPU.

## 2. Artifacts

### 2a. Prebuilt ROM zips (the thing to extract `/system` from)

AndroidFileHost ford folder "Amazon Fire 2015 Tablet / ford": https://androidfilehost.com/?flid=293089&w=files
(read through a third-party fetch cache; the AFH page itself was not scripted). All files are by `ggow`
(XDA Recognized Developer; AFH profile https://androidfilehost.com/?uid=23487008491966649&w=profile).
All zips are **UNOFFICIAL** community builds.

| File | Size (AFH) | Uploaded | Notes |
|---|---|---|---|
| **lineage-12.1-20200505-UNOFFICIAL-ford.zip** | **238.3 MB** | 2020-05-06 | **Recommended.** MD5 `5fa7b3fdc29ae0693afe80f439c69631`. https://androidfilehost.com/?fid=4349826312261799008 . Latest 12.1; XDA changelog "05 May 2020: Fixed video recording" |
| lineage-12.1-20200423-UNOFFICIAL-ford.zip | 238 MB | 2020-04-23 | MD5 `21e5efe179e966d7c01b713fc4ee58ca`, fid `4349826312261780056` |
| lineage-12.1-20190823-UNOFFICIAL-ford.zip | 236.3 MB | 2019-08-23 | MD5 `c8a3934867ab70d8bca3a07103414ec4`, fid `6006931924117942835` |
| lineage-12.1-20190731 / 20190524 / 20190422 / 20190418 / 20190416 / 20181218 | 237.8 / 236.2 / 240.7 / 240.8 / 240.8 / 238.7 MB | 2018-12 .. 2019-07 | 20190731 MD5 `bf5b38fb7e0548e6e833c4b5c2b12ff8`, 20190524 MD5 `208b3f5c4f2abd3fe89544654795a86e` |
| cm-12.1-20161122-UNOFFICIAL-ford.zip, cm-12.1-20151116-UNOFFICIAL-ford.zip | n/a (not opened) | 2016, 2015 | Earlier CM 12.1 builds (23K / 6.8K downloads on AFH "popular files"). Older sources, not preferred. |
| lineage-14.1-{20190828, 20210117, 20220201}-UNOFFICIAL-ford.zip | 272.6 / 278.4 / 283.2 MB | 2019-2022 | Android 7.1. Out of scope for Phase A (feasibility `:253-257`: needs a loadable policy). A 20230621 build moved to Google Drive per https://www.alexgoldcheidt.com/amazon-fire-7-5th-gen-tablet-twrp/ and XDA. |

Alternative with fewer CM extras: **Fire Nexus ROM** ("Pure AOSP Experience", AOSP `android-5.1.1_r34`..`r37`,
base `LMY49M`, last updated 2019-05-26; XDA thread
https://xdaforums.com/t/discontinued-rom-unlocked-ford-austin-fire-nexus-rom-26-may-2019.3300714/ ;
AFH folder https://androidfilehost.com/?w=files&flid=48493 with `ford` = 4 files, sizes not seen [U];
historical names `lp-fire-nexus-rom-ford-20171217.zip`, `...-standalone-20171002.zip`). Its changelog says it "switches
to using FireOS 5.x binaries **and boot.img**", so it ran on real ford with Amazon's boot image and ramdisk, which is
our container's arrangement [E for the changelog, I for the conclusion]. Keep as the B-candidate if CM's
extras (CMTheme/LiveDisplay/Privacy Guard services) cost too much RAM.

Lineage 12.1 thread (title says "ford,austin"): https://xdaforums.com/t/discontinued-rom-unlocked-ford-austin-lineage-12-1-05-may-2020.3639447/
. Its OP names the sources (below) and says "this ROM only works on devices using the bootloader hack"
(irrelevant to us, we never flash). The OP asks "Please do NOT create any mirrors": download for local
research, do not redistribute, never commit the zip or the derived image (`/work/` is ignored).

### 2b. Source trees

| What | Where | State |
|---|---|---|
| ROM source manifest | https://github.com/cm12-amami/android (`cm-12.1`, 402 commits, `default.xml`, README asks OpenJDK 1.7) | **archived 2022-09-18**, read-only. It is MSe1969's CM 12.1 tree for Sony `amami`; ggow rebased onto it on 2018-01-28 (XDA changelog). `cm12-amami/android_system_core` etc. are archived too. |
| Device tree, ford | https://github.com/NaphatrB/android_device_amazon_ford (`cm-12.1`, pushed 2019-04-24, 13 files, `ro.sf.hwrotation=270` in `system.prop`) | live. Owner identity vs XDA `ggow` is [I]: the XDA OP links `github.com/ggow/android_device_amazon_ford`, which now returns 404, and the NaphatrB repos carry the same names, branch and dates as the changelog entries (2019-04-22 kernel rebase, 2019-05-24 build). |
| Device tree, common | https://github.com/NaphatrB/android_device_amazon_mt8127-common (`cm-12.1`, pushed 2019-04-28, 27 MB; `BoardConfigCommon.mk`, `proprietary-files.txt` ~250 entries, `rootdir/` with `init.mt8127.rc`, `ueventd.mt8127.rc`, `fstab.mt8127`, ...) | live |
| Kernel | https://github.com/NaphatrB/android_kernel_amazon_mt8127-common (`cm-12.1`, pushed 2019-05-24, 1 GB) | live, "upstreamed to 3.10.108", merged ford+austin kernel (XDA changelog 2019-04-18/22). Not used by us (we keep Sony's 3.10.26). |
| Vendor blobs | `TeamRegular/android_vendor_amazon_ford` (`cm-12.1`, 2015-12-03) and `TeamRegular/android_device_amazon_ford` (`cm-12.1`, 2015-12-03). A `vendor_amazon_mt8127-common` for 12.1 was **not located** [U]; `extract-files.sh` in the device trees pulls blobs from a FireOS device/image. The ROM zip is itself the practical blob source. | stale (2015) |
| Older per-device trees | https://github.com/chaosmaster/android_device_amazon_ford (default `twrp`, also `cm-12.1`, `master`), https://github.com/chaosmaster/android_kernel_amazon_ford (`cm-12.1`, 2 commits, pushed 2016-01-30) | live but not the source of the 2017-2020 builds |
| Lineage 14.1 | org `cm14-mt8127`: `device_amazon_{ford,austin,mt8127-common}`, `kernel_amazon_mt8127-common`, `vendor_amazon_{ford,austin,mt8127-common}` (all `cm-14.1`, pushed 2022-02 .. 2023-06-26, not archived) | Android 7.1, out of scope here |

`proprietary-files.txt` (mt8127-common, read) lists the blob set the ROM carries, taken from FireOS:
`lib/hw/{gralloc,hwcomposer,audio.primary,camera,lights,sensors,keystore,thermal,memtrack,power}.*`,
`lib/egl/lib{EGL,GLESv1_CM,GLESv2}_mali.so`, `libMali.so`, `libmtk_mali_user.so`, `libbwc`, `libdpframework`,
`libgralloc_extra`, `libgui_ext`, `libion_mtk`, `libm4u`, `libtz_uree`, `libMtkOmx*`, `libnvram*`, `bin/{idme,kisd,
devicetype_service,nvram_daemon,thermald,wpa_supplicant,...}`. So the ROM is **CM 12.1 framework + FireOS
blobs**, exactly the shape of our current "Fire framework + Fire blobs" minus the Amazon framework layer.

### 2c. Build-from-source fallback (only if the zip is unusable)

`repo init -u https://github.com/cm12-amami/android -b cm-12.1` plus a ford local manifest (the
`NaphatrB/android` repo, branch `cm-12.1`, pushed 2019-02-25, is probably that manifest; its `default.xml` was **not read** [U]),
OpenJDK 1.7, `brunch ford`. Per repo rule the heavy build goes to `aws-burst`. A source build is the only way to
get dexpreopt on the host if the zip turns out not to be preopted (R2), and to set `BOARD_SYSTEMIMAGE_PARTITION_SIZE`
yourself. `BoardConfigCommon.mk` already sets 1258291200 B, which equals the byte size of our
`work/android/fire7-unpack/system.img.orig` (1258291200) [E].

### 2d. What I could not reach

- XDA pages return **403** to `WebFetch` (both `forum.xda-developers.com` and `xdaforums.com`). I read OP and page 2 of the
  12.1 thread and the 14.1 / Fire Nexus OPs through a third-party fetch cache; later pages (known issues, reports on
  RAM use) were not read.
- `wiki.nura.eco` (postmarketOS wiki) is behind an Anubis bot wall; the package page worked.
- GitHub code search needs auth (401); I used targeted raw-file reads instead.
- AFH downloads need an interactive browser session [U: scripted download is fragile]. Not attempted (rules).
- `web.archive.org` was not tried this session. `github.com/ggow/*` is 404.
- The ROM zip's own contents (file list, `updater-script`, `build.prop`) are unread by design. Everything about the
  zip layout below is inferred from the build system, not from the zip.

## 3. Extraction chain: zip -> raw ext4 `system.img`

### 3.1 Packaging is a file tree, not an image [E for the build system, U for this specific zip]

- `BoardConfigCommon.mk` (mt8127-common, `cm-12.1`): `BLOCK_BASED_OTA := false`,
  `BOARD_SYSTEMIMAGE_PARTITION_SIZE := 1258291200`, `TARGET_USERIMAGES_USE_EXT4 := true`,
  `BOARD_BOOTIMAGE_PARTITION_SIZE := 16777216`.
- cm-12.1 `build/tools/releasetools/ota_from_target_files` (cm12-amami/android_build): `OPTIONS.block_based = False` by
  default, set True only by `--block`; `block_based = OPTIONS.block_based and has_recovery_patch`; otherwise the
  file branch runs `script.UnpackPackageDir("system", "/system")`, `CopyPartitionFiles`, `script.MakeSymlinks(symlinks)`
  and later `SetPermissions`.
- cm-12.1 `edify_generator.py` emits `package_extract_dir("system", "/system");`,
  `symlink("<target>", "<link1>", "<link2>", ...);`, `set_metadata("<path>", "uid", U, "gid", G, "mode", 0M
  [, "capabilities", C][, "selabel", "S"]);`, `set_metadata_recursive("<dir>", "uid", U, "gid", G, "dmode", 0D, "fmode", 0F
  [, "capabilities", "C"][, "selabel", "S"]);` (and the older `set_perm` / `set_perm_recursive`).
- Therefore **expect**: `system/` directory tree of regular files, `boot.img`, `META-INF/com/google/android/{update-binary,
  updater-script}`. **No** `system.new.dat` / `system.transfer.list` (that is the Amazon OTA form behind our
  `tools/sdat2img.py`, which does not apply here) and no `system.img`. Symlinks, ownership, modes and capabilities
  are **not** in the zip entries; they live only in `updater-script`. [U until the zip is listed; if it does contain
  `system.new.dat`, `tools/sdat2img.py` handles it directly, `tools/sdat2img.py:1-40`.]

### 3.2 Chain (all host-side, as the unprivileged user)

1. `unzip` `system/*`, `boot.img` and `META-INF/com/google/android/updater-script` into a **fresh empty
   directory** (`work/android/lineage12-unpack/`); keep any converter script in `tools/`, not in that directory.
2. Replay the script (a ~100-line Python tool, `tools/zip2sysimg.py` to be written; edify subset is tiny):
   - every `symlink(target, links...)` becomes a symlink in the staging tree;
   - build a permission table from `set_metadata_recursive` / `set_metadata` (and `set_perm*`);
   - ignore `mount`, `format`, `ui_print`, `assert(getprop("ro.product.device") == "ford" ...)`, `package_extract_file("boot.img", ...)`.
   Skipping the permission replay is not an option: dirs must be 0755 and files readable by `system`/`graphics`/`media`
   or system_server and SurfaceFlinger fail to open their own libraries.
3. Build the image with **`fakeroot`** so uid/gid survive without root: `chown/chmod` from the table, then
   `mke2fs -t ext4 -L system -b 4096 -O ^metadata_csum,^64bit,^orphan_file,^has_journal -d <staging> <img> <size>`.
   Feature flags follow `tools/android-boot.sh:66-68` (the device kernel is 3.10.26 and cannot mount `metadata_csum`
   / `orphan_file`). Size: used bytes plus ~15 percent; the partition limit is 1258291200 B but smaller is fine for a
   read-only loop image and pushes faster. Capabilities (`security.capability` xattrs) and `selabel` can be skipped
   because SELinux stays disabled [I]; `run-as` etc. are not needed in a root container.
4. Check on the host before pushing: `e2fsck -fn`, `dumpe2fs -h` (no `metadata_csum`, `64bit`, `orphan_file`),
   `debugfs -R "stat /bin/surfaceflinger"` for mode/owner, and the ELF/link check in step 6.
5. `boot.img` -> ramdisk (the container takes `init`, `sbin/healthd`, `ueventd.rc`, `ueventd.mt8127.rc` from the ramdisk,
   `tools/android-container.sh:26-45`): split the Android boot image (page size 2048, `BoardConfigCommon.mk`:
   `--ramdisk_offset 0x04000000 --tags_offset 0x00000100`), drop the 512-byte MTK ramdisk header if present [U whether
   it is], `gunzip | cpio -idm` (non-root, ignore device-node errors) into `work/android/lineage-ramdisk/`.
   The repo holds the *result* for Fire (`work/android/fire7-unpack/ramdisk.bin` is plain gzip, 1,624,064 B
   expanded) but no script that produced it [E], so this step is new tooling.
6. `tools/symcheck.py ELF LIBDIR...` (`tools/symcheck.py:1-9`) over every ELF the container runs (`bin/*`, `lib/*.so`,
   `lib/hw/*.so`, `lib/egl/*.so`) against the new image's own `lib/`: any "missing" symbol is a boot blocker found
   without the device.

### 3.3 What the `lab126_log_write` shim is for

- **Measured here [E]** (read-only `debugfs` dump of `system.img.orig` `/lib` and `/bin` to the session scratchpad, then
  `readelf --dyn-syms`): `lab126_log_write` is *imported* by **193 files under `/system/lib` (incl. `hw/`) and 34 under
  `/system/bin`**, and *defined* by both Fire `libcutils.so` and `liblog.so`, together with `__vitals_log_print`,
  `lab126_blocking_log_buf_write` and `lab126_log_level`. Importers among the blobs the ROM keeps include
  `gralloc.mt8127`, `hwcomposer.mt8127`, `libgralloc_extra`, `libgui_ext`, `libion_mtk`, `libm4u`, `libbwc`, `libtz_uree`,
  `audio.primary.mt8127`, `camera.mt8127`, `libMtkOmx{Vdec,Venc,Mp3Dec,AIVPlayer}`, `libnvram`. `libMali`, `libEGL_mali`,
  `libGLESv*_mali` and `libmtk_mali_user` do **not** import it. `__vitals_log_print` is imported only by
  `libMtkOmxAIVPlayer.so` (and Fire's own `logd`); `lab126_blocking_log_buf_write` only by Fire's `libandroid_runtime.so`
  (framework, replaced by the ROM's).
- So Amazon's logging wrapper leaked into the MTK/Amazon-built blobs; a stock AOSP `libcutils`/`liblog` does not define
  it and the blob fails to resolve at load.
- **How cm-12.1 solves it [E]:** `COMMON_GLOBAL_CFLAGS += -DAMAZON_LOG -DADD_LEGACY_ACQUIRE_BUFFER_SYMBOL` in
  `BoardConfigCommon.mk`, and `liblog/logd_write.c` (cm12-amami/android_system_core, `cm-12.1`) contains an
  `#ifdef AMAZON_LOG` block defining `int lab126_log_write(int bufID, int prio, const char *tag, const char *fmt, ...)`
  (drops tags starting "Sensors"/"qcom_se", prefixes "AMZ-", forwards to `__android_log_buf_write`) and a stubbed
  `__vitals_log_print`. liblog is statically linked into libcutils in 5.1, which is how Fire's libcutils exports it too [I].
  cm-14.1 and chaosmaster's later trees use a separate `libshims` instead (suez:
  https://github.com/chaosmaster/android_device_amazon_suez/blob/lineage-16.0/libshims/lab126_shim.cpp , signature
  `(int loglevel, uint32_t *unknown, const char *logtag, const char *fmt, ...)`).
- **Consequence for us [I]:** with the prebuilt zip nothing needs shimming; confirm by `readelf` that the ROM's `libcutils.so`
  (or `liblog.so`) defines both symbols (gate G2). A shim is needed only for a from-source build without `-DAMAZON_LOG`.
- `ADD_LEGACY_ACQUIRE_BUFFER_SYMBOL`: I searched cm12-amami `frameworks/native/libs/gui/{BufferQueueConsumer.cpp,Android.mk}`
  and did not find it; its meaning stays [U]. It is part of the same board flags the ROM was built with, so it is carried
  by the prebuilt libs.

## 4. Integration into the container

What is **kept** unchanged: Sony 3.10.26 kernel, `mali_r4p0.ko` swap (`probe/android/android-boot.sh:90-102`), zram
(`:212-223`), `pivotinit` container root and mount-namespace trick (`docs/android.md:86-101`), the data image, idc overlay,
the swap-accounting guard kmod (`probe/kmod/swap-accounting-guard/`; **`tools/android-boot.sh up` does not load it**, grep of
`tools/*.sh`, `probe/android/*.sh`, `docs/*.md` finds no reference, so it is a separate pre-step per
`devlog/2026-10-13-optimization-handoff.md:24-34`). Per that handoff's rule at `:42` ("bend the software to the kernel"), the swap
is entirely a userland-layer change.

What is **replaced / changed**:

| Item | File:line | Change |
|---|---|---|
| System image | `tools/android-boot.sh:22` (`IMG=.../fire7-unpack/system.img`), `:25` (`SYS_NAME=wmmod-android/fire-system.img`), `probe/android/android-boot.sh:27` | Point at the new image and give it a **new remote name** (e.g. `wmmod-android/aosp-system.img`). `push_once` (`tools/android-boot.sh:39-47`) skips the push when the remote *size* equals the local size, so reusing the old name risks silently booting a stale image; a separate name also keeps the Fire image for A/B. |
| Container ramdisk source | `tools/android-container.sh:20,26-28` (`work/android/fire-ramdisk`) | Parametrize; keep `tools/tests/test_android_container.py` green. See "which init" below. |
| BOOTCLASSPATH / SYSTEMSERVERCLASSPATH | `probe/android/container/init.rc:54-55` | Contain Amazon jars (`fosframework.jar`, `webviewext.jar`, `amazon-remoteviews.jar`, `fosinit.jar`, `fosservices.jar`, `AWSRemoteConfigurationAndroidClient.jar`). Replace with the ROM's list. Since 5.0 it is generated into the ramdisk `init.environ.rc` from `PRODUCT_BOOT_JARS`; **read it from `work/android/lineage-ramdisk/init.environ.rc`, do not guess** (CM adds `org.cyanogenmod.*` jars [U]). |
| Amazon resource package | `init.rc:61` (`export AMAZON_EXTRA_RESOURCE_LIST ...`) | Delete. This was only needed because Amazon's `ActivityManagerService` read `fireos-res.apk` (`docs/android.md:107`). |
| Amazon group | `init.rc:237` (`group ... amz_group media`) | Remove `amz_group`; AOSP's AID name table (`android_filesystem_config.h`) has no such group, so init would reject it for that service [I]. |
| Panel orientation | `init.rc:167-173` (`persist.panel.orientation 0`) | That prop is read by Fire's patched SurfaceFlinger. AOSP's equivalent is **`ro.sf.hwrotation`**, and the ford `system.prop` sets `ro.sf.hwrotation=270` and `ro.sf.lcd_density=160` [E]. Put `ro.sf.hwrotation=0` (what `docs/android.md:130-138` measured as upright) and a density in the container `default.prop`; `ro.*` is first-writer-wins and `/default.prop` loads before `load_all_props` reads `/system/build.prop` [I, from init property semantics, not re-read]. |
| Fire-only prop | `probe/android/container/default.prop:8` (`ro.product.config.type=AIQC11IVNRMBB`) | Fire imp workaround (`devlog/2026-10-13-imp-stage-a.md`); meaningless for AOSP, harmless; drop. |
| Stub audio HAL | `probe/android/android-boot.sh:162-170` | Still applies: the ROM's `proprietary-files.txt` lists both `lib/hw/audio.primary.mt8127.so` and `lib/hw/audio.primary.default.so` (FireOS stub). The script `die`s if the stub is missing, so check it exists after the first image build. |
| UI sounds dir, idc | `:185-190`, `:194-203` | AOSP has `system/media/audio/ui` and `system/usr/idc`; scripts keep working [I]. |
| Services started by our init.rc | `init.rc:151-300` | Keep ueventd/vold/healthd/netd/mediaserver/logd/lmkd/debuggerd/installd/servicemanager/surfaceflinger/bootanim/zygote. Still omit keystore (needs the MTK TEE keymaster), `nvram_daemon`, `thermald`, `kisd`, `idme`, `devicetype_service`, `wmt_*`, `msensord`, `akmd*`. The ROM's `init.mt8127.rc` (23,753 B) starts several of these on real ford; we do not import it. |
| First-run UI | n/a | Lineage ships SetupWizard (cm12-amami `android_packages_apps_SetupWizard`). To reach the launcher: `settings put global device_provisioned 1; settings put secure user_setup_complete 1` or `pm disable` the wizard. |

### Which init, ramdisk binaries [E facts, I decision]

- Our container runs the **Amazon** static `init` (256,340 B) and static `healthd` (259,288 B) from `work/android/fire-ramdisk/`
  (`file`: ELF 32-bit ARM, statically linked), so they do not care which `/system` is mounted.
- AOSP 5.1.1 `init.c`: `selinux_is_disabled()` honours `ro.boot.selinux=disabled` **only inside `#ifdef ALLOW_DISABLE_SELINUX`**
  (`init.c` ~lines 825-851), and `init/Android.mk` defines `-DALLOW_DISABLE_SELINUX=1` only under
  `ifneq (,$(filter userdebug eng,$(TARGET_BUILD_VARIANT)))`
  (https://github.com/aosp-mirror/platform_system_core/blob/android-5.1.1_r38/init/init.c , `.../init/Android.mk`). If the
  init in the ROM's boot.img comes from a **user** build, it would call `selinux_android_load_policy()`, fail (no `/sepolicy`
  in the container), log "SELinux: Failed to load policy; rebooting into recovery mode" and **reboot the Walkman**.
  Build type of the ROM is [U] (Lineage unofficial builds are usually `userdebug`, unverified).
- **Decision for the first boot: keep the Amazon `init`, `healthd`, `ueventd` from `work/android/fire-ramdisk`**, take only
  `init.environ.rc` (for the classpaths), `ueventd.mt8127.rc` and `default.prop` content from the ROM ramdisk as reference.
  This is the same pairing the Fire Nexus changelog describes on real ford (AOSP framework + FireOS boot.img) [I], and it
  is already proven in our container. Swap to the ROM's init later, behind `NW_DROP_SYS_BOOT=1`
  (`tools/android-boot.sh:85-93`) which strips CAP_SYS_BOOT so a failed policy load cannot reboot the device.

## 5. Compatibility risks (ranked)

| # | Risk | Evidence / reasoning | Mitigation |
|---|---|---|---|
| R1 | **Init SELinux gate**, device reboot if the ROM's init is a user build | Section 4. [E code paths, U ROM build type] | Keep Amazon init first; read `ro.build.type` in `system/build.prop` and `default.prop` in the ROM ramdisk at gate G1; use `NW_DROP_SYS_BOOT=1` when trying the ROM's init. |
| R2 | **Not dexpreopted -> first-boot `boot.art` generation + dexopt of ~all apps on 2 A7 cores / 456 MB, with the swap/zram path under load** | Fire's image is odexed (`framework/arm/boot.art` 11 MB + `boot.oat` 67 MB, feasibility `:156-158`). Our history: first-boot dexopt of ~248 inputs coincided with hard resets (`devlog/2026-10-11-android-container.md:308-314`), later traced to two stock swap bugs fixed by the guard kmod (`devlog/2026-10-13-optimization-handoff.md:24-34`). Whether cm-12.1 ford zips are preopted: `WITH_DEXPREOPT` is not set in `BoardConfigCommon.mk` and `build/core/dex_preopt.mk` only disables it when no `DALVIK_VM_LIB`, so the default comes from elsewhere [U]. | G1: `unzip -l` for `system/framework/arm/boot.oat` and `*.odex`. If absent: load the guard kmod first, `dalvik.vm.image-dex2oat-Xmx` stays 96m (`init.rc:164-165`), expect a long first boot, persist `/data` dalvik-cache across runs (already on the loop image), or fall back to a source build with dexpreopt (2c). |
| R3 | **Blob vintage differs from our Fire image.** The ROM's blobs come from FireOS 5.x of its own date (changelog: "FireOS 5.4.0.0 update-kindle-37.5.7.9 binaries and boot.img", 2017-12-02) vs our `37.6.2.6` / 5.3.6.4. Our `mali_r4p0.ko` and the display ioctls were validated against *our* blobs only. | `docs/android.md:71-75`: ford 5.3.6.4 and austin 55.6.5.9 ship a byte-identical `libMali.so`, which makes a change within FireOS 5 unlikely but not excluded [I]. | G2: sha256 the 12 graphics blobs against the `system.img.orig` copies; for any difference re-run the API check from `docs/android.md:74` (find the `GET_API_VERSION` ioctl `_IOWR(0x82,3,...)` and read the constant: must stay 401); `tools/symcheck.py`. Worst case, bind our known-good files over the ROM's (same pattern as the audio stub, `:162-170`). |
| R4 | **Panel rotation / density.** ford `system.prop`: `ro.sf.hwrotation=270`, `ro.sf.lcd_density=160`. Walkman panel is 480x800 upright. | Section 4 table; `docs/android.md:130-138`. | Override in container `default.prop` (not in the image). |
| R5 | **Framework-level kernel expectations.** | I see no new kernel feature the CM framework needs beyond what the Fire framework already used in this container: same binder v7 + single context manager (uid 100), ashmem, `/dev/alarm`, logd, ION, `sw_sync`. The ROM boots on real ford with `androidboot.selinux=permissive` (ford `BoardConfig.mk`) and kernel 3.10.108; we run 3.10.26 with SELinux effectively off. CM's own additions (LiveDisplay, Profiles, ThemeManager, `org.cyanogenmod.hardware`) call sysfs paths that may not exist and should degrade, but are [U] at runtime. | Watch logcat for `cmhw`, `LiveDisplay`, `Theme` stack traces on the first boot; disable via `pm disable` if they loop. |
| R6 | **Vendor blobs "we do not have".** | None missing for the ROM's own boot: its `proprietary-files.txt` carries ~250 FireOS files (graphics, codecs, audio, BT, wifi firmware, nvram). Blobs that need absent hardware or forbidden partitions behave as in the Fire container: `audio.primary.mt8127` -> libnvram (forbidden NVRAM/PRO_INFO) stays masked by the stub bind (`android-boot.sh:151-170`); `/dev/trustzone` must stay (`docs/android-handoff.md:57-59`); keystore stays off; camera/sensors/thermal HALs load but find no hardware. | G2 plus first-boot logcat. |
| R7 | **SELinux policy differences vs our Fire system.** | Our container never loads a policy (`androidboot.selinux=disabled` + Amazon init, `docs/android.md:82`). The ROM's policy files (`sepolicy`, `file_contexts`, `property_contexts`, `seapp_contexts`, `service_contexts`) live in the ramdisk, not in `/system`, and are simply not used if SELinux stays off; `/system/etc/security/mac_permissions.xml` is read by PackageManager regardless and needs no kernel support. Policy compile/versions only matter for Android 7.1+ (feasibility `:246`). | None for Phase A beyond R1. |
| R8 | **zygote fd whitelist vs static-liblog copies.** The earlier container work hit exactly this class of failure (`Not whitelisted : /tmp/aroot/dev/__properties__`, `docs/android.md:88-100`), and LineageOS cm-13 hit it for legacy blobs that call the static liblog inside libcutils (https://review.lineageos.org/c/LineageOS/android_system_core/+/142832). `lab126_log_write` forwards into liblog from libcutils [I]. | Fire's libcutils has the same arrangement and works here. The ROM runs on real ford. | Treat any "Not whitelisted: socket:" in the first zygote log as this. |
| R9 | **Memory.** Unknown whether CM 12.1 plus the ROM's persistent apps sits below Fire's trimmed footprint on 456 MB. | No data. Fire's census (`devlog/2026-10-13-fireos-trim-plan.md`) had ~15 resident Amazon processes that disappear. | Measure with `dumpsys meminfo` and `/proc/meminfo` from the same samplers as the Fire runs; compare with Fire Nexus if it is heavy. |
| R10 | **Acquisition fragility.** AFH needs a browser; the XDA author forbids mirroring; the source orgs are archived; Amazon blobs are proprietary. | 2d. | Download once, record MD5 in a devlog, keep the zip only under `/work/` (ignored), never commit it or any derived image. |

## 6. Validation plan

### Host-only gates (no device needed, can run now once the zip is downloaded)

| Gate | Check | Go / no-go |
|---|---|---|
| G0 | MD5 equals `5fa7b3fdc29ae0693afe80f439c69631`; `unzip -t` | stop on mismatch |
| G1 | `unzip -l`: confirms file-tree layout (3.1), absence/presence of `framework/arm/boot.oat` and `*.odex` (R2), size of `system/` (fits 1,258,291,200 B); read `updater-script` (symlink / `set_metadata*` forms, device assert); read `system/build.prop` (`ro.build.type`, `ro.board.platform=mt8127`, `ro.product.device=ford`, `ro.sf.*`); inspect the ramdisk `default.prop` and `init.environ.rc` | `ro.board.platform` must be `mt8127` (it is how the MTK HALs are found, `probe/android/container/default.prop:3-5`). A `user` build type triggers the R1 mitigation, not a stop. |
| G2 | After extraction: hash and `readelf` the graphics blobs vs ours; `tools/symcheck.py` on `bin/` and `lib/hw/`; `readelf --dyn-syms libcutils.so liblog.so` shows `lab126_log_write` and `__vitals_log_print` defined; `libMali.so` API constant 401 | any undefined symbol or API != 401 stops the plan until understood |
| G3 | Build `system.img`, `e2fsck -fn`, feature-flag check, stat spot checks (section 3.2 step 4) | clean fsck |

### Device run (needs the device; another worker owns it, so coordinate first)

Preconditions per `docs/android-handoff.md:49-55`: ask whether another session is using the device, no reboots without the
user, no bootimg/system/nvp writes (the container writes nothing to device partitions). Load the swap guard kmod first.

1. Port `init.rc` / `default.prop` / `android-container.sh` as in section 4. Keep the Fire path working by selecting the image
   and ramdisk by variable, so a Fire boot remains the negative control.
2. `tools/android-boot.sh up` (it builds `container.tgz`, pushes the Mali module, container and image, runs `prepare`, zram,
   `swap`, `container`, `start_init`; `tools/android-boot.sh:57-95`). Then `status`, `log`, `pull`.
3. Expected order of failures [I], with the evidence that identifies each:

   | Order | Symptom | Cause / next step |
   |---|---|---|
   | 1 | `!! no stub audio HAL`, `!! no .../media/audio/ui` from `android-boot.sh` | image missing a path the script requires (`:164-165`, `:186`); fix the image build |
   | 2 | init reboots the device / "Failed to load policy" | R1; rerun with Amazon init or `NW_DROP_SYS_BOOT=1` |
   | 3 | `init: ... invalid group 'amz_group'`, services not starting | init.rc port incomplete |
   | 4 | zygote `ClassNotFoundException` / `fosframework.jar` not found | BOOTCLASSPATH not replaced |
   | 5 | `dex2oat` for boot image / long dexopt, resets | R2; guard kmod, dalvik-cache persistence |
   | 6 | `Not whitelisted : ...` in zygote | R8 |
   | 7 | SurfaceFlinger crash loop (`UREE_CreateSession failed`, missing `/dev/trustzone`) or link errors | R3 / devclean must keep `/dev/trustzone` (`docs/android-handoff.md:57-59`) |
   | 8 | `media.audio_flinger` wait | mediaserver or the stub bind (as before, `docs/android-handoff.md:74`) |
   | 9 | SetupWizard instead of home | provisioning settings (section 4) |

4. **Pass criteria**, in increasing strength:
   - `getprop ro.build.fingerprint` is the Lineage/ford build, not `Amazon/full_ford/...`.
   - `ls /system/framework | grep -Ei 'fos|amazon'` is empty; `pm list packages | grep -ci amazon` is 0 (leftover FireOS *binaries* such as `bin/kisd`,
     `bin/idme`, `lib/*amzn*` are expected and not started).
   - SurfaceFlinger prints `Boot is finished` (same marker as `docs/android.md:118`, `docs/android-handoff.md:13`), `sys.boot_completed=1`.
   - Panel capture via `probe/devtools/fbcap.c` + `tools/fbcap2png.py` shows the Lineage launcher (Trebuchet) **upright**
     (`docs/android.md:120-139`). Looking at the panel is the verification bar (CLAUDE.md "visual projects").
   - No repeating SystemUI/launcher ANR over a stable window, `dumpsys meminfo` budget recorded next to the Fire numbers.
5. Reboot is the recovery for every step, as before.

## 7. Open unknowns

1. Actual contents of the zip: layout, `updater-script`, dexpreopt, `build.prop`, ramdisk `init`'s build type, `libMali` constant
   (all gated on download; G0-G2).
2. Whether the 2020 builds still use FireOS 5.4.0.0 blobs/boot image or something later (changelog only says so for 2017-12-02).
3. Identity of `ggow` vs `NaphatrB` on GitHub [I], and which repos a ford build actually pulls (`NaphatrB/android` `default.xml` unread; a
   `vendor_amazon_mt8127-common` for 12.1 not found).
4. `ADD_LEGACY_ACQUIRE_BUFFER_SYMBOL` semantics.
5. Runtime cost of CM extras on 456 MB; whether Fire Nexus is materially lighter (its zip list/sizes unseen).
6. Whether a scripted AFH download is feasible and acceptable; XDA known-issue pages unread (403).
7. Bluetooth, camera, sensors, battery/charge nodes: not different from the Fire container (still open there, see
   `devlog/2026-10-13-optimization-handoff.md` "Open compatibility problems").

## Sources

Repo (all under `~/Projects/nw-a55/`): `devlog/2026-10-14-aosp-feasibility.md`, `docs/android.md`,
`docs/android-handoff.md`, `devlog/2026-10-11-android-container.md`, `devlog/2026-10-13-optimization-handoff.md`,
`devlog/2026-10-12-swap-accounting-guard.md`, `tools/android-boot.sh`, `probe/android/android-boot.sh`,
`tools/android-container.sh`, `tools/sdat2img.py`, `tools/symcheck.py`, `probe/android/container/{init.rc,default.prop,ueventd.extra.rc}`,
`work/android/fire7-unpack/{system.img.orig,system.transfer.list,boot.img,ramdisk.bin,mali/}`,
`work/android/fire-ramdisk/{init,sbin/healthd}`, `.gitignore:8`.

Web (fetched 2026-10-14):
- AFH ford folder https://androidfilehost.com/?flid=293089&w=files ; austin/all https://androidfilehost.com/?flid=293088&w=files ; file pages
  https://androidfilehost.com/?fid=4349826312261799008 (20200505), `?fid=4349826312261780056` (20200423), `?fid=6006931924117942835` (20190823),
  `?fid=6006931924117927619` (20190731), `?fid=3556969557455275779` (20190524) ; Fire Nexus folder https://androidfilehost.com/?w=files&flid=48493 ; ggow profile https://androidfilehost.com/?uid=23487008491966649&w=profile
- XDA (via cache): Lineage 12.1 https://xdaforums.com/t/discontinued-rom-unlocked-ford-austin-lineage-12-1-05-may-2020.3639447/ (+ `/page-2`);
  Lineage 14.1 https://xdaforums.com/t/rom-unlocked-ford-austin-lineage-14-1-21-jun-2023.3962457/ ; Fire Nexus
  https://xdaforums.com/t/discontinued-rom-unlocked-ford-austin-fire-nexus-rom-26-may-2019.3300714/ ; unlock/amonet
  https://xdaforums.com/t/unlock-root-twrp-unbrick-downgrade-fire-7-ford-and-austin.3899860/page-105
- Device identity: https://pkgs.nura.eco/package/main/postmarketos/armv7/device-amazon-ford , https://gist.github.com/Juul/ac03f4e8c7f8e7cc2d67d40bf2174f4f ,
  https://www.alexgoldcheidt.com/amazon-fire-7-5th-gen-tablet-twrp/ , https://blog.jonandtina.net/2025/amazon-fire-7-and-lineageos/ , https://lkml.rescloud.iu.edu/2609.1/02670.html
- GitHub: https://github.com/NaphatrB/android_device_amazon_ford , https://github.com/NaphatrB/android_device_amazon_mt8127-common
  (`BoardConfigCommon.mk`, `proprietary-files.txt`, `mt8127-common.mk`, `rootdir/`, `configs/`), https://github.com/NaphatrB/android_kernel_amazon_mt8127-common ,
  https://github.com/chaosmaster/android_device_amazon_ford , https://github.com/chaosmaster/android_kernel_amazon_ford ,
  https://github.com/cm12-amami/android , https://github.com/cm12-amami/android_system_core (`liblog/logd_write.c`, `cm-12.1`),
  https://github.com/cm12-amami/android_build (`tools/releasetools/{ota_from_target_files,edify_generator.py}`, `core/dex_preopt.mk`),
  https://github.com/cm14-mt8127/device_amazon_ford , https://github.com/TeamRegular/android_vendor_amazon_ford ,
  https://github.com/chaosmaster/android_device_amazon_suez/blob/lineage-16.0/libshims/lab126_shim.cpp ,
  GitHub API listings for `users/{chaosmaster,NaphatrB,ggow}`, `orgs/{cm12-amami,cm14-mt8127}`, `search/repositories`
- AOSP: https://github.com/aosp-mirror/platform_system_core/blob/android-5.1.1_r38/init/init.c , `.../init/Android.mk` ;
  LineageOS libcutils static-liblog change https://review.lineageos.org/c/LineageOS/android_system_core/+/142832
