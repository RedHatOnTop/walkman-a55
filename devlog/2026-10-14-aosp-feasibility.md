# AOSP-ifying the NW-A55 — feasibility assessment (GSI, AOSP 5.1, newer Android)

2026-10-14. READ-ONLY desk study: repo docs, host-side files already on disk (the 2026-10-10 backup
images, the Fire `system.img.orig`, Sony's GPL kernel tarball), and web research. No adb, no device,
no build, no sub-agents. Nothing was committed. Evidence tags used below:

- **[E]** established: a file/line/URL is cited and I read it.
- **[I]** inference from established facts (reasoning shown).
- **[U]** unverified / speculation.

## Bottom line

A GSI cannot boot on this device and cannot be made to without writing the whole Android vendor half
(HIDL HALs, vendor image, multi-context binder, new SELinux policy) yourself, so GSI is the wrong
first target. The cheap real win is **a from-source AOSP/LineageOS 5.1.1 system in the existing
container**, replacing the *whole* Fire OS `system` (not just debloating it) while reusing the Sony
kernel, the ported `mali_r4p0` module and the Fire MT8127 graphics blobs. It touches no gated
partition (no bootimg, LK or kernel), the blobs' only Amazon coupling I could find is one libcutils
symbol that every community Fire-7 tree already shims, and community cm-12.1 / cm-14.1 trees for this
exact Fire-7 userland exist. Anything newer than 7.1 needs kernel changes (multi-context binder,
SELinux policy v30, ~40 config options), which means a bootimg write, which is the recovery gate. Two
findings make that gate look better than the repo currently assumes: (1) community tools for MT8590
Walkmans (wbrt, mediatek_flash_tool, mtkclient 1.63) already give a preloader-mode full-eMMC
backup/restore and report SBC/SLA/DAA disabled on an A50, and (2) the Sony kernel's SoC matches the
MT2701/MT7623 family that **mainline Linux supports**. Recommended order: A) AOSP 5.1 in the container
(no gate, start now); B) confirm Layer-2 recovery on this unit and analyze LK offline (**the first
blocker for everything past A**); C) kernel backports for Android 7.1 / 8.1 Go; D) mainline kernel only
as a research branch. Android 9+ and GSI: very hard to likely infeasible without a modern BSP.

## Findings that contradict or sharpen existing repo assumptions

| # | Repo says | What I found |
|---|---|---|
| 1 | `devlog/2026-10-14-recovery-path-mtk.md`: Layer-2 (MTK BROM/preloader) "NOT established for MT8590 ... no MT8590 in mtkclient context, no NW-A55 scatter/DA" | mtkclient's `brom_config.py` has a full `0x8590` entry (`mt8590_payload.bin`, `blacklist`, `send_ptr`, `cmd_handler`); the payload file exists. unknown321's `wbrt` and `mediatek_flash_tool` target "MT8590 Walkmans - NW-A30/40/50, ZX300, WM1A, WM1Z, DMP-Z1", and Wampy's `BACKUP.md` shows a real A50 preloader session: `SBC/SLA/DAA enabled: False`, `Device is unprotected`, full-eMMC dump and restore. [E] (section 4) |
| 2 | `docs/android-roadmap.md`: newer kernel for MT8590 "may not exist" | Sony's `mach-mt8590` PLL tree and peripheral base addresses match mainline's MT2701/MT7623 support register-for-register on everything I compared. [E] for the match, [I] for "same family" (section 2) |
| 3 | `docs/android-roadmap.md`: "Android 8+ expects kernel ~3.18+" | AOSP's own fragments for 8.x start at 3.18, but Google's `android-msm-bullhead-3.10-oreo-mr1` kernel is 3.10.73, and Google's docs (search snippet only) say upgraded devices may keep their base kernel. The real gaps are specific and countable (section 4). |
| 4 | Task brief: `walkman-zx507` "reached" GSI Android 13 | It did not. The ZX507 repo is stage 1 only: an AOSP-rules *ceiling* computed from firmware files; bootloader still locked, no unlock, no flash, device not touched (`walkman-zx507/README.md`, `devlog/2026-10-14.md`). |
| 5 | `ROADMAP.md:20`: LK may verify the kernel signature | LK contains `sec_boot_check` / `sec_img_verify.c` code, but every enforcement indicator I could find says "off" for this device family (section 4). One residual unknown: kernel-image replacement was never demonstrated by anyone I found. |

## 1. GSI / Treble viability

**Verdict: not bootable. Not "hard", structurally absent.**

- Android 5.1.1 is API 22. `ro.build.version.sdk=22`, `ro.build.version.release=5.1.1`,
  `ro.build.version.fireos=5.3.6.4`, `ro.product.device=ford`, `ro.board.platform=mt8127` [E]
  (`work/android/fire7-unpack/system.img.orig`, `/build.prop`, read with `debugfs`). Treble arrived in
  8.0 (API 26).
- AOSP's GSI page says a GSI is for devices running Android 9+ that use "AIDL/HIDL vendor interfaces",
  flashing "requires the device to be 'Treblized'", and needs a way to unlock/fastboot [E]
  (https://source.android.com/docs/setup/build/gsi). TrebleDroid's FAQ repeats that only
  treble-enabled devices are in scope [E].
- Why this device fails each Treble leg [E unless noted]:
  1. **No vendor interface.** HALs are legacy `libhardware` modules inside `/system/lib/hw`
     (`gralloc.mt8127.so`, `hwcomposer.mt8127.so`, `audio.primary.mt8127.so`, ... in the Fire image).
     No `android.hardware.*` HIDL services, no vendor mount, no VINTF manifest.
  2. **Binder.** Treble needs three contexts (`/dev/binder`, `/dev/hwbinder`, `/dev/vndbinder`), the
     latter only if `CONFIG_ANDROID_BINDER_DEVICES="binder,hwbinder,vndbinder"`
     (https://source.android.com/docs/core/architecture/hidl/binder-ipc). Sony's 3.10.26 `binder.c`
     has one global `binder_context_mgr_node` (`oss/linux-kernel-3.10.26.tar.gz`
     `drivers/staging/android/binder.c:64`) and no devices option; the kernel config has only
     `CONFIG_ANDROID_BINDER_IPC=y` (`nwa50-extracted/kernel/kconfig.txt:2369`). Sony's own
     `hagodaemon` already owns the single context (`docs/android.md`, "바인더 context manager").
  3. **SELinux.** Kernel has SELinux built in but the cmdline is `androidboot.selinux=disabled`
     (`work/device/backup-20261010/survey.txt`); no policy exists (section 4).
  4. **Kernel config.** 38 of 154 options in AOSP's Android 8.1 `android-base.cfg` are missing
     (section 4).
- Even a "synthetic Treble" (loop-mounted vendor image, hand-written VINTF, HIDL `-impl` wrappers
  around the Fire blobs, a GSI as `system`) is just Phase C of this document with extra steps. [I]
  Treblizing legacy devices is a known community practice (e.g. `erfanoabdi/Capire-Le-Treble`,
  described as "Treble without Vendor Partition"; I could not read its README, [U] on how it works).
- **What does NOT transfer from `walkman-zx507`:** ZX507 is arm64 i.MX 8M Mini, 4 GB, Android 9 vendor
  with `ro.treble.enabled=true`, `ro.vndk.version=28`, a 200 MiB vendor partition, and Sony's audio
  HAL ships *as an Android HAL* in vendor (`audio.primary.icx1295.so`)
  (`walkman-zx507/docs/android-upgrade-feasibility.md` s1-2). Here there is no Android vendor at all and
  Sony's A55 firmware is not an Android userland (`docs/phase0-architecture.md` s1: hagodaemon +
  Qt/eglfs, no Android servicemanager). What *does* transfer is the method: ceilings are read from
  AOSP's own `kernel/configs` and compatibility matrices, not from forum lore.

## 2. What exists: trees, kernels, blobs

### Sony / MTK side (MT8590)

- **Kernel source: yes, full tree.** `oss/linux-kernel-3.10.26.tar.gz` (Sony GPL drop, 156 MB) holds
  `arch/arm/mach-mt8590/`, MTK drivers, the CXD3778GF codec driver
  (`sound/soc/codecs/cxd3778gf/cxd3778gf.c`) and Sony `icx_*` drivers [E]. This is the only MT8590
  kernel source that exists publicly as far as I found.
- **Android device tree for MT8590 / any Sony Walkman: none found.** GitHub repo searches for
  `mt8590`, `android_device_sony mt8590`, "walkman kernel", "nw-a50 kernel" returned only
  `unknown321/mediatek_flash_tool`, `superwilso/Cinder` (a Rust player UI over Sony's stack) and
  Wampy-family tools [E, search results; absence is not proof].
- **MT8590 Android blobs: none.** The Walkman firmware's Mali userspace is the Linux/eglfs flavor,
  not Android (`docs/phase0-architecture.md` s6.3 item 1). Working blobs are Fire 7 (MT8127) ones:
  Mali r4p0 (API 401) needs the repo's own `probe/kmod/mali-r4p0` port because the Sony kernel only
  accepts API 900 (`docs/android.md`, "Android 블롭 찾기") [E].
- **Relationship to MT8127:** the Fire blobs run on MT8590 hardware here (SurfaceFlinger composites
  via `gralloc.mt8127`/`hwcomposer.mt8127`, `docs/android.md`). The preloader image in our backup
  carries the string `MT8127` at 0xd10 and `preloader_bbdmp5_linux.bin` (`strings -t x
  work/device/backup-20261010/mmcblk0boot0.img`), meaning unverified [U]; LK's source paths say
  `platform/mt8590/` [E].

### Mainline Linux: same family as MT2701 / MT7623 (surprise)

I compared Sony's `mach-mt8590` headers against mainline files [E for each match]:

| Item | Sony 3.10.26 (`mach-mt8590`) | Mainline |
|---|---|---|
| APMIXEDSYS PLL regs | ARMPLL 0x200, MAINPLL 0x210, UNIVPLL 0x220, MMPLL 0x230, MSDCPLL 0x240, TVDPLL 0x250, AUD1PLL 0x270, TRGPLL 0x280, ETHPLL 0x290 (`mt_clkmgr.h`) | `clk-mt2701.c`: armpll 0x200, mainpll 0x210, univpll 0x220, mmpll 0x230, msdcpll 0x240, tvdpll 0x250, aud1pll 0x270, trgpll 0x280, ethpll 0x290 |
| PLL names | adds VDECPLL, HADDS2PLL, AUD2PLL, TVD2PLL (enum in `mt_clkmgr.h`) | identical set (`CLK_APMIXED_*`) |
| UART/PWM/I2C/SPI/NFI | 0xF1002000 / F1006000 / F1007000 / F100A000 / F100D000 (virt, MTK maps phys 0x1xxxxxxx -> 0xFxxxxxxx) | `mt7623.dtsi`: serial@11002000, pwm@11006000, i2c@11007000, spi@1100a000, nfi@1100d000 |
| USB / audio AFE / MSDC | F1200000 / F1220000 / F1230000, F1240000 | usb@11200000 (musb), audsys@11220000, mmc@11230000, mmc@11240000 |
| PWRAP, SPM, EFUSE, VDEC | F000D000, F0006000, F0206000, F6000000 | pwrap@1000d000, power-controller@10006000, efuse@10206000, vdecsys@16000000 |
| TZ SMP mailbox | `NS_SLAVE_JUMP_REG = BOOT_SHARE_BASE+1020`, `NS_SLAVE_MAGIC_REG = +1016` (`mt-smp.c:68-69`) | `arch/arm/mach-mediatek/platsmp.c`: `mediatek,mt2701` -> `mtk_mt8135_tz_boot = {0x80002000, 0x3fc, {0x534c4131, ...}, {0x3f8,...}}` |

- [I] The MT8590 die is in the MT2701/MT7623 family. Corroboration: mtkclient's hwcode alias
  `MT8590/MT7683/MT8521/MT7623`, and the A50 preloader log in Wampy's `BACKUP.md` reports exactly
  that string with `HW code: 0x8590`.
- Mainline also has an `arm,mali-450` binding with `mediatek,mt7623-mali`
  (`Documentation/devicetree/bindings/gpu/arm,mali-utgard.yaml`) and the `lima` DRM driver
  ("DRM driver for ARM Mali 400/450 GPUs"); `mt7623.dtsi` itself has no GPU node [E].
- **What this does NOT mean:** no one has booted mainline on a Walkman that I found [U]. Missing for a
  mainline port: CXD3778GF codec (Sony GPL driver exists for 3.10 only), HX8379C DSI panel, Himax
  touch, Sony power/charger glue, the Cortex-M4 `cm4` audio DSP load path, MT66xx connectivity, and
  the closed-TEE interplay already documented in `devlog/2026-10-14-core-revival-tee-recheck.md`
  (the mainline `mt2701` SMP path uses the same `0x80002000` mailbox, which fits the measured
  TEE-side jump vector but does not resolve the fuse question).

### MT8127 (Fire 7, `ford` / `austin`): the relevant community ecosystem [E]

- LineageOS 12.1 (Android 5.1): `chaosmaster/android_device_amazon_ford` (branch `cm-12.1`),
  `NaphatrB/android_device_amazon_mt8127-common` (`cm-12.1`), kernel
  `chaosmaster/android_kernel_amazon_ford` (`cm-12.1`).
- LineageOS 14.1 (Android 7.1): GitHub org `cm14-mt8127` (`device_amazon_mt8127-common`, branch
  `cm-14.1`, last commit 2023-06-26; `kernel_amazon_mt8127-common`). Its tree has `libshims/`
  (`shim_binder`, `shim_camera`, `shim_gui`, `shim_liblog`, `shim_libwvm`, `shim_skia`),
  `LINKER_FORCED_SHIM_LIBS`, `patches/frameworks/av/*`, a `sepolicy/` directory and `zram: setup as
  0.5 GB`. That it builds and has camera/wifi/codec fixes in its log suggests it boots [I]; I found
  no status report [U].
- Sources for the frameworks exist: LineageOS `cm-12.1`, `cm-14.1`, `lineage-15.1` branches of
  `android_frameworks_base`; AOSP tag `android-5.1.1_r38`.
- XDA thread "[DISCONTINUED][ROM][UNLOCKED] ford/austin Lineage 12.1 (05 May 2020)" exists (title only
  via search; the page returned 403, so downloads/known-issues are [U]).
- Other MT-family Fire trees (MT8163, MT8173) reuse the same shim idea
  (`lab126_log_write` appears in `mt8163-dev/...`, `mt8173-dev/...`, `LOSMantis/...`, etc., GitHub code
  search).

## 3. Near-term path: de-Amazon to AOSP 5.1.1, keep kernel and vendor HALs

**Verdict: feasible, with a known recipe, no gated writes. This is the right next project.**

### It is not "debloat taken to the end"

Debloat removes apps and services; it cannot remove Amazon's framework. In the Fire image
`/system/framework/framework.jar` and `services.jar` are two of **49 jars that are exactly 310 bytes**
(stubs); the code lives in `framework/arm/` (37 `.odex` files plus `boot.art` 11 MB and `boot.oat`
67 MB) together with Amazon-only `fosframework.jar`, `fosinit.jar`, `fosservices.jar`,
`com.amazon.*.jar` and `framework/fireos-res/` [E, `debugfs ls` on `system.img.orig`]. Mixing an AOSP
`framework.jar` with Amazon odex/boot images is a trap (ART boot-image checksums); the swap has to be a
**whole-`system` replacement** built from source, ideally with dexpreopt on the build host so the two
A7 cores do not dex2oat everything at first boot. The debloat track's own evidence shows why this
matters: the SystemUI ANR was caused by Amazon's SpeechUi inside SystemUI plus system_server-bound
Amazon services (`devlog/2026-10-14-systemui-anr-root-cause-speechui.md`,
`devlog/2026-10-14-debloat-codex-review.md`). Those are framework-level couplings.

### What I measured about the vendor blobs (host-side, from `system.img.orig`) [E]

- Graphics stack: `hwcomposer.mt8127.so` needs `libbwc, libdpframework, libgralloc_extra, libion_mtk,
  libm4u, libtz_uree, libui, libutils, libcutils, libhardware, libion, libsync`. Its undefined C++
  symbols into AOSP libs are only `RefBase`, `Thread`, `VectorImpl`, `StopWatch` (libutils). The rest
  are MTK-private (`gralloc_extra_*`, `DpBlitStream`, `MTKM4UDrv`, `UREE_*`, `BWC::`). `libMali.so`
  needs `libdpframework, libmtk_mali_user, libbinder, libsync, ...`.
- No `mtk`/`gralloc_extra`/`GuiExt` symbols are exported by `libui`, `libgui`, `libsurfaceflinger`,
  `libcutils`; `gui_ext` is only referenced inside `libgui_ext.so` itself.
- **The one Amazon coupling:** `libcutils.so` *defines* `lab126_log_write` (also
  `lab126_log_level`, `lab126_blocking_log_buf_write`), and 11 of the 14 libs I extracted import it:
  `audio.primary.mt8127`, `gralloc.mt8127`, `hwcomposer.mt8127`, `libgralloc_extra`, `libgui_ext`,
  `libaudioflinger`, `libgui`, `libhardware`, `libmedia`, `libsurfaceflinger`, `libui`. A stock AOSP
  `libcutils` will not provide it, so the blobs fail to link (the 5.1 bionic linker rejects unresolved
  symbols at load, expected but not tested here [U]). The community fix is a one-function shim;
  the cm-12.1 board config sets `-DAMAZON_LOG -DADD_LEGACY_ACQUIRE_BUFFER_SYMBOL`, and cm-14.1
  force-loads `libshim_liblog.so` into `libcutils`/`liblog` via `LINKER_FORCED_SHIM_LIBS`. Meaning of
  `ADD_LEGACY_ACQUIRE_BUFFER_SYMBOL` is [U].
- `audio.primary.default.so` (5316 B) in the Fire image is AOSP's stub HAL, and the repo already binds
  it over `audio.primary.mt8127.so` and gets `Loaded primary audio interface`
  (`devlog/2026-10-13-audio-hal-stub-plan.md`). So audio is already "AOSP-shaped"; it is silent, not
  broken.
- AOSP 5.1.1's `init` honors `androidboot.selinux=disabled` (`selinux_is_disabled()`,
  `platform/system/core` tag `android-5.1.1_r38`, `init/init.c:889,935,984`) [E], which is what the
  container relies on today. `ro.config.low_ram` exists in this tree
  (`ActivityManager.isLowRamDeviceStatic`, `ActivityManager.java:474-475`) [E].

### Swap-in vs stays

| Layer | Today (Fire 5.1.1 `ford` in container) | After AOSP-ification (5.1.1) |
|---|---|---|
| Kernel, RAM-only kmods (swap guard, `mali_r4p0`, Wi-Fi backports), zram recipe | Sony 3.10.26 | **unchanged** (this is the "reusable base" in `docs/android-roadmap.md`) |
| `pivotinit` container, loop-mounted system image | yes | unchanged; `fire-system.img` replaced by the new image |
| init / ueventd / servicemanager / vold / netd / installd | Amazon-built | AOSP-built; reuse the container's current init tweaks |
| zygote, ART, framework, services, SystemUI, Settings, Launcher | Amazon (odexed, `fos*`) | AOSP / Lineage cm-12.1 (+ Launcher3/Trebuchet; AOSP Music or the custom player) |
| libcutils/libui/libgui/SurfaceFlinger/libhwui | Amazon build | AOSP build + `lab126_log_write` shim |
| gralloc/hwcomposer/Mali/MDP/ION/M4U/uREE blobs | MT8127 Fire blobs | **keep** (keep `/dev/trustzone`, per `docs/android-handoff.md`) |
| Audio | AOSP stub bound over MT8127 HAL | write a real CXD3778GF tinyalsa HAL; same work Fire needs |
| Wi-Fi | RAM-only backports + MTK wpa_supplicant | same; take driver glue from the cm trees |
| Bluetooth | unresolved | [U] Fire uses MTK `bluetooth.default`; Sony uses `mtkbt` BlueAngel. Not investigated. |
| Keystore, drmserver, kisd, devicetype_service, idme | Amazon/MTK | dropped (TEE not reachable; per `docs/android-handoff.md` keystore is already not started) |
| Apps | 154 `priv-app` + 18 `app` directories in the image (`debugfs`) | a few dozen; the Amazon census (~15 resident Amazon processes, `devlog/2026-10-13-fireos-trim-plan.md`) disappears |

### Costs and caveats

- Needs a 5.1-era AOSP/Lineage build environment (old JDK etc.; cm-12.1 build notes ask for OpenJDK
  1.7, from the `cm12-amami` README seen in search results). The repo's rule is that heavy builds go
  to `aws-burst`; same applies here.
- The cm-12.1 ford tree targets the Amazon kernel 3.10.54 (`kernel/amazon/ford`) and ships an
  `selinux_permissive.ko`; neither applies, we keep Sony's kernel and AOSP 5.1.1's `init` path.
- Open items that are not new: `mtkfb_vsync` ioctl mismatch (`_IOW` vs Sony `_IO`,
  `docs/android.md`), power/backlight/charge-state, adb drop on screen-off
  (`devlog/2026-10-13-optimization-handoff.md`). AOSP-ifying does not fix them but removes the
  Amazon noise that currently hides them.
- Product trade-off: in Android mode Sony's DSP chain (DSEE HX, ClearAudio, LDAC, `dacdat` tables in
  `load_sony_driver`) is not available unless re-implemented. That is the same trade-off as
  `docs/phase0-architecture.md` s6.2/s6.3, and it is the opposite of Cinder's "keep Sony's DSP"
  strategy.
- Expected RAM win vs Fire is plausible but unmeasured [U]; `ro.config.low_ram=true` is available.

## 4. Hard path: a newer Android

### What AOSP itself says (not forum lore)

- `kernel/configs` at `pie-release`: `o/` and `o-mr1/` contain `android-3.18`, `android-4.4`,
  `android-4.9`; `p/` contains `android-4.14`, `android-4.4`, `android-4.9`. No 3.10 fragment for 8.x
  [E] (https://android.googlesource.com/kernel/configs/+/refs/heads/pie-release/).
- Google's `core-kernel-reqs` page (seen only in a search snippet, page not retrievable here): "Older
  Android devices upgrading to Android 8.0 or higher can continue to use their original base kernel
  version" [E-ish, secondary]. The same snippet notes new SoC launches must be 3.18+/4.4+.
- Counter-example that matters: Google's own `android-msm-bullhead-3.10-oreo-mr1` kernel is
  `VERSION = 3, PATCHLEVEL = 10, SUBLEVEL = 73`, with `binder_devices`/`CONFIG_ANDROID_BINDER_DEVICES`
  in `drivers/staging/android/binder.c` and SELinux policydb version 30 (`POLICYDB_VERSION_XPERMS_IOCTL`)
  in `security.h` [E]. Android 8.1 *can* sit on a backported 3.10; and the delta is a concrete diff
  against a kernel of the same major version.

### Gap list: Sony 3.10.26 vs what 7.x / 8.x need

| Requirement | Evidence | Sony kernel state | Fix class |
|---|---|---|---|
| Multi-context binder (8.0+ HIDL/Treble) | binder-ipc doc; bullhead `binder.c` | one global context manager, protocol v7 (`binder.c:64`, `binder.h:85`) | kernel backport. Binder is **built-in** (`=y`), so a bootimg write; or a renamed out-of-tree binder module like `mali_r4p0` [U, many unexported symbols] |
| Binder protocol 32 vs 64-bit | AOSP 8.1 bionic `binder.h` keeps `#ifdef BINDER_IPC_32BIT -> PROTOCOL 7`; `build/core/config.mk` (pie) forces 64-bit only when `PRODUCT_SHIPPING_API_LEVEL >= 28` | v7 (32-bit) | **not a blocker** for an upgrade-style 8.x build [E] |
| SELinux policy | 7.1.2 `init.cpp:selinux_initialize` calls `selinux_android_load_policy()` and `security_failure()` on error, no `selinux_is_disabled` path (5.1.1 had one); `sepolicy` default `POLICYVERS ?= 30` for 7.1.2 and 8.1.0 | built-in SELinux, no policy, `androidboot.selinux=disabled`; `POLICYDB_VERSION_MAX = POLICYDB_VERSION_DEFAULT_TYPE = 28` (`security/selinux/include/security.h:35,42`; `CONFIG_SECURITY_SELINUX_POLICYDB_VERSION_MAX` not set) | backport v29/v30 (bullhead has v30), or compile policy at 28 ([U] whether 7.x/8.x policy compiles at 28) |
| Config fragment | AOSP `o-mr1/android-3.18/android-base.cfg` (154 options) vs `kconfig.txt` | **38 missing**: 32 are netfilter/xfrm/ematch (conntrack, NAT, XT_*), 6 are `ANDROID_BINDER_DEVICES`, `CGROUP_FREEZER`, `HARDENED_USERCOPY`, `MODVERSIONS`, `SECURITY_PERF_EVENTS_RESTRICT`, `UID_SYS_STATS`; 4 hardening mismatches (`DEVMEM`, `DEVKMEM`, `OABI_COMPAT`, `SYSVIPC` are `y`, fragment wants `n`). Also `CPUSETS=n`, `CMA=n`, `DRM=n` | kernel rebuild; most netfilter items only matter for netd firewall features |
| Android 7.0+ init | see SELinux | needs a loadable policy even in permissive | as above |
| Graphics/HAL for 8.x | N/A | legacy gralloc/HWC1 blobs from Android 5.1 | [I] wrap with HIDL `-impl` passthrough + HWC1->HWC2 adapter; unproven for these MTK blobs |
| RAM | developer.android.com Go page: min RAM 512 MB for 8.1, 9, 10; 1 GB 11/12; 2 GB 13 | MemTotal 467,512 kB (`survey.txt`) | 8.1 Go is the only plausible "new" target, and it is marginal [I] |
| CPU | | 2 usable A7 cores (fuse) | not an AOSP-compat gate; perf only |

So: **Android 7.1 is the ceiling that needs no kernel change** *if* sepolicy can be built at an
accepted version or run permissive with a loadable policy [I]; community cm-14.1 proves 7.1 on a 3.10
MTK kernel with these blob families [E for the tree's existence, [U] for "boots"]. **Android 8.x needs
kernel work** (binder devices, SELinux v30, cgroup freezer, config fragment). **Android 9+** also
needs a vendor stack that no one has for MT8590 (and Mali-450 userspace for those releases is not
researched [U]).

### How this ties to the repo's existing gates (cited, not re-derived)

| Gate | Source | Status after this study |
|---|---|---|
| Recovery net before any bootimg write | `ROADMAP.md`, `devlog/2026-10-14-recovery-path-mtk.md` | Layer-1 (UPG) is a delivery path only; **Layer-2 now has MT8590-specific community tooling** (below). Not yet confirmed on this unit. |
| LK signature verification | `ROADMAP.md:20`, `devlog/2026-10-14-recovery-path-mtk.md` ("decisive"), `devlog/2026-10-14-core-revival-tee-recheck.md` | See evidence list below. Leaning "not enforced"; kernel replacement is the one thing nobody has demonstrated. |
| `NR_CPUS=2` | `devlog/2026-10-14-core-revival-tee-recheck.md` | Independent of AOSP feasibility (fuse leaves 2 cores either way). A kernel rebuild for AOSP needs could carry `NR_CPUS=4` for free, but that is not a requirement. |

**Layer-2 evidence [E unless noted]:**
- Wampy `BACKUP.md` (real A50 log, mtkclient 1.63): `HW code: 0x8590`, `SBC enabled: False`,
  `SLA enabled: False`, `DAA enabled: False`, `Mem read/write auth: False`, `Device is unprotected`,
  `Device is in Preloader-Mode`. Entry: SD card out, device off, hold `volume down` + `play`, plug USB;
  host sees `idVendor=0e8d idProduct=2000 "MT65xx Preloader"`. Full dump `mtk rf`, restore `mtk wf`.
  Linux fallback `unknown321/mediatek_flash_tool` because "mtkclient 2.0.x ... USB Overflow" on A40/A50.
- `unknown321/wbrt` (Windows): backup/restore for NW-A30/40/50, ZX300, WM1A/Z, DMP-Z1; FAQ: "Can this
  tool pull my device out of bootloop? Yes, if you have made a backup beforehand"; never restore
  another unit's backup (serial/factory data).
- Cinder README: "Back up the player with wbrt. There is no other recovery path for this device."
- Our unit: complete partition set except MBR/EBR in `work/device/backup-20261010/` (matching
  device-computed md5s, `ANALYSIS.md` s11), including the preloader (`mmcblk0boot0.img`).
- Caveat: that log is a different A50 unit [U for ours]. Preloader-mode flashing is by nature a raw
  write path, so only the read/backup direction fits `AGENTS.md` until the operator says otherwise.

**LK evidence (static, host-side strings of our `uboot.img`, 384 KiB; preliminary, not a verdict):**
- Code exists: `[LK] sec_boot_check (boot_mode=%d)`, `app/mt_boot/sec/sec_boot.c`,
  `app/mt_boot/sec/sec_img_verify.c`, `[%s] Signature Fail...`, `Image sign format is v3/v4 version`.
- Modes exist: `[LK] boot mode is DOWNLOAD_BOOT`, ` => Not authenticated tool, download stop...`,
  ` DA verification disabled...`, `FASTBOOT mode...` with `getvar: flash: erase: download: continue
  reboot`, a `[Recovery Mode]` / `[Fastboot Mode]` menu (`[VOLUME_UP to select. VOLUME_DOWN is OK.]`),
  `MT65XX_RECOVERY_KEY`. Entry conditions not analyzed; this is a second possible flash/recovery route
  [U].
- Enforcement indicators against: SBC/SLA/DAA false (above); `seccfg.img` in our backup is 131072
  bytes of zeros (measured; no `0x4D4D4D4D` header, so a blank config); and
  Wampy / nw-installer / Walkman One all ship boot images whose **ramdisk was rebuilt** (cpio +
  re-added MTK headers, `MAKING_OF.md` "Booting") and boot, including the W1 ramdisk on this very
  unit (`ANALYSIS.md` s11, "W1이 바꾼 것"). Unknown: whether a different *kernel* would be accepted;
  no public custom-kernel precedent for MT8590 Walkmans found (llusbdac builds a *module* against
  Sony's tree).

## 5. Recommended phased path

| Phase | What | Gated writes? | Effort (my estimate, no data behind it) | Risk | Notes |
|---|---|---|---|---|---|
| **A** | AOSP/Lineage **5.1.1** in the existing container: whole-`system` replacement, `lab126_log_write` shim, dexpreopt, keep Mali kmod + MT8127 blobs, stub audio first | **No** | weeks (build env + bring-up) | Low-Med | Removes the Amazon layer entirely. First milestone: AOSP Launcher + SystemUI up with a measured RAM budget, no ANR loop. |
| **B** | **Confirm Layer-2 on this unit** (read-only preloader handshake + full-eMMC read with wbrt / mediatek_flash_tool / mtkclient 1.63) and **analyze LK offline** (Ghidra on `uboot.img`: `sec_boot_check` policy, boot-menu key paths) | Reads only | days of analysis + one operator session | Med (flash tools are raw) | **Single biggest blocker for everything past A.** Needs the device, which another worker currently owns. |
| **A+** | Real audio HAL (CXD3778GF tinyalsa, `dacdat` tables) and persistence (start the container from the boot ramdisk, Wampy-style via UPG + abootimg + mtkheader) | Persistence = bootimg write | weeks-months | Med-High | Persistence is gated on B. Audio HAL is needed by Fire as well. |
| **C** | Kernel backports on 3.10.26: multi-device binder, SELinux v30, cgroup freezer, config fragment; target Android 7.1 first (no kernel needed if sepolicy cooperates), then 8.1 Go | Bootimg write | months | High | Use Google's `android-msm-bullhead-3.10-oreo-mr1` as the reference diff for binder/SELinux. 457 MiB RAM vs Go's 512 MB nominal is marginal. |
| **D** | Mainline Linux (MT2701/MT7623 family, `lima` for Mali-450) as the only way to a modern kernel | Bootimg write | many months | Very high | Research branch only: no codec/panel/touch/M4/connectivity drivers exist for mainline; likely infeasible without a lot of RE. Only worth it if C proves insufficient. |
| **E** | GSI / Android 9+ | Bootimg write | - | Very high | Not recommended: needs C + D-class kernel *and* a Treble vendor stack nobody has for MT8590. |

**Biggest blocker to resolve first (programme-wide): kernel replaceability = Layer-2 recovery on this
unit + whether LK accepts a replaced kernel (phase B).** Phase A does not depend on it and should
start in parallel.

## Not done / open questions (all cheap, mostly host-only)

1. Extend my `lab126_log_write` check to *all* undefined symbols of the vendor blob set against a
   built AOSP 5.1.1 `libcutils/libutils/libui/libgui/libhardware` (host-only, needs an AOSP build).
2. Disassemble `uboot.img` for the `sec_boot_check` policy path (what gates it: seccfg lock state,
   SBC fuse, or neither).
3. Pull `proprietary-files.txt` from the cm-12.1 ford tree and diff against Fire 5.3.6.4 to confirm
   the blob set; check whether an old Lineage 12.1 ford `system.img` is still downloadable as a
   no-build shortcut for Phase A [U].
4. Size the binder/SELinux backport: `diff` `drivers/staging/android/binder.c` and
   `security/selinux` between our tarball and the bullhead 3.10.73 branch.
5. Bluetooth path under Android (MTK Bluedroid blob vs Sony `mtkbt`) is uninvestigated.
6. Does Sony's MT8590 differ from MT7623 in IP revisions that matter to mainline (MSDC, musb)? The
   register-base match is necessary, not sufficient.
7. Device, operator-only: preloader-mode handshake on this A55 (Vol-Down + Play + USB, SD out).

## Sources

Repo (all under `~/Projects/nw-a55/`):
`docs/ROADMAP.md`, `docs/phase0-architecture.md` (s1, s5.1-5.2, s6.2-6.3, Q30 at :745),
`docs/android-roadmap.md`, `docs/android.md`, `docs/android-handoff.md`, `ANALYSIS.md` (s10-11),
`AGENTS.md`, `devlog/2026-10-14-recovery-path-mtk.md`, `devlog/2026-10-14-core-revival-tee-recheck.md`,
`devlog/2026-10-14-systemui-anr-root-cause-speechui.md`, `devlog/2026-10-14-debloat-codex-review.md`,
`devlog/2026-10-13-fireos-trim-plan.md`, `devlog/2026-10-13-audio-hal-stub-plan.md`,
`devlog/2026-10-13-optimization-handoff.md`, `nwa50-extracted/kernel/kconfig.txt`,
`oss/linux-kernel-3.10.26.tar.gz` (`mach-mt8590/mt-smp.c`, `mt_clkmgr.h`, `mt_reg_base.h`,
`drivers/staging/android/binder.{c,h}`, `security/selinux/include/security.h`),
`work/device/backup-20261010/` (`dumchar_info.txt`, `survey.txt`, `uboot.img`, `seccfg.img`,
`mmcblk0boot0.img`, `bootimg.img`), `work/android/fire7-unpack/system.img.orig` (read-only
`debugfs`; libs copied to the session scratchpad for `readelf`).
Sibling: `~/Projects/distribution-project/walkman-zx507/` (`README.md`,
`devlog/2026-10-14.md`, `docs/android-upgrade-feasibility.md`). Note: it lives under
`distribution-project/`, not `~/Projects/`.

Web (fetched 2026-10-14):
- GSI: https://source.android.com/docs/setup/build/gsi
- Binder domains: https://source.android.com/docs/core/architecture/hidl/binder-ipc
- Core kernel requirements (search snippet only): https://source.android.com/devices/architecture/kernel/core-kernel-reqs
- AOSP kernel configs: https://android.googlesource.com/kernel/configs/+/refs/heads/pie-release/ (`o/`, `o-mr1/`, `p/`; `o-mr1/android-3.18/android-base.cfg`)
- Google 3.10 Oreo-MR1 kernel: https://android.googlesource.com/kernel/msm/+/refs/heads/android-msm-bullhead-3.10-oreo-mr1/
- sepolicy `POLICYVERS`: https://android.googlesource.com/platform/system/sepolicy/+/refs/tags/android-7.1.2_r39/Android.mk (and `android-8.1.0_r81`)
- init SELinux handling: https://github.com/aosp-mirror/platform_system_core/blob/android-5.1.1_r38/init/init.c , https://github.com/aosp-mirror/platform_system_core/blob/android-7.1.2_r39/init/init.cpp
- Binder 32/64: https://android.googlesource.com/platform/bionic/+/refs/heads/oreo-release/libc/kernel/uapi/linux/binder.h , https://android.googlesource.com/platform/build/+/refs/heads/pie-release/core/config.mk
- Low RAM: https://github.com/aosp-mirror/platform_frameworks_base/blob/android-5.1.1_r38/core/java/android/app/ActivityManager.java
- Android Go RAM table: https://developer.android.com/guide/topics/androidgo
- TrebleDroid FAQ: https://github.com/trebleDroid/treble_experimentations/wiki/Frequently-Asked-Questions-(FAQ)
- mtkclient config / payloads: https://github.com/bkerler/mtkclient (`mtkclient/config/brom_config.py`, `mtkclient/payloads/`)
- Wampy backup + boot-image notes: https://github.com/unknown321/wampy/blob/master/BACKUP.md , https://github.com/unknown321/wampy/blob/master/MAKING_OF.md
- Recovery tooling: https://github.com/unknown321/wbrt , https://github.com/unknown321/mediatek_flash_tool , https://github.com/unknown321/nw-installer , https://github.com/superwilso/Cinder
- Fire 7 trees: https://github.com/chaosmaster/android_device_amazon_ford , https://github.com/NaphatrB/android_device_amazon_mt8127-common , https://github.com/cm14-mt8127/device_amazon_mt8127-common , https://github.com/cm14-mt8127/kernel_amazon_mt8127-common , https://github.com/chaosmaster/android_kernel_amazon_ford
- Mainline: https://github.com/torvalds/linux/blob/master/arch/arm/boot/dts/mediatek/mt7623.dtsi , `.../mt2701.dtsi`, `.../mt8127.dtsi`, https://github.com/torvalds/linux/blob/master/drivers/clk/mediatek/clk-mt2701.c , https://github.com/torvalds/linux/blob/master/arch/arm/mach-mediatek/platsmp.c , https://github.com/torvalds/linux/blob/master/Documentation/devicetree/bindings/gpu/arm,mali-utgard.yaml , https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/lima/Kconfig
- XDA ford/austin Lineage 12.1 thread (title only; page 403): https://xdaforums.com/t/discontinued-rom-unlocked-ford-austin-lineage-12-1-05-may-2020.3639447/

Fetch failures, so the related claims are weaker: web.archive.org blocked; XDA 403; the current
source.android.com restructure hid the old kernel-requirements page.
