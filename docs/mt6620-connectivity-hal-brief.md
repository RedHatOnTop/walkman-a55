# Orchestration brief: BT / WiFi / GPS / FM HALs for the NW-A55

For a **separate Claude Code session** that runs **two subagents plus one external research job**
(layout in "Workers" below). Written 2026-10-15 by the main session that proved the audio path;
revised the same day after the kernel-source and CM-image checks below. Repo: `~/Projects/nw-a55`
(local git, no remote; commit to master). Hobby RE project — route to the goal.

The filename says MT6620 for history only: the hardware is the MT8590's integrated CONSYS (see below).

## Read first (context the workers need)

- Device: Sony NW-A55 Walkman, MediaTek **MT8590**, Sony kernel Linux 3.10.26, 456 MiB RAM. It runs
  a **CyanogenMod 12.1 (Android 5.1 / API 22, armeabi-v7a) container** on the Sony kernel via
  `tools/android-boot.sh` (`NW_CONTAINER_VARIANT=aosp`). The CM "ford" image is in
  `work/android/aosp-ford/` (gitignored). See [[nw-a55-aosp-ford-image]].
- **Connectivity is the MT8590's integrated CONSYS, not an external MT6620 combo.** Sony's kernel
  config has `CONFIG_MTK_COMBO_CHIP="CONSYS_8590"`, the shipped modules carry the `_soc` suffix and
  are built from `drivers/misc/mediatek/conn_soc/`, and the transport is BTIF
  (`mtk_wcn_consys_stp_btif_*`). The 2026-10-11 WiFi run logged
  `stp_dbg_set_version_info: chipid(0x8590),romver(E1),patchver(20180628)`: the radios are on the
  MT8590 die, with no separate connectivity chip. The `6620_*` binary names are MTK's legacy naming
  for every chip.
  The CM ford image is a Fire 7 (MT8127) build, and the MT8127 is the same CONSYS family.
- **The CM image already ships an MTK CONSYS radio userspace** (verified with `debugfs` on
  `system.img.cmstock`): `lib/libbt-vendor.so`, `lib/libbluetooth_mtk.so`,
  `lib/libbluetooth_mtk_pure.so`, `lib/hw/bluetooth.default.so`, `bin/autobt`, `bin/wmt_loader`,
  `bin/6620_launcher`, `bin/wpa_supplicant`, `lib/libhardware_legacy.so`, `lib/libwifi-service.so`,
  `lib/libnetutils.so`, `bin/dhcpcd`, `bin/nvram_daemon` + `lib/libnvram*.so`, and firmware
  `WMT_SOC.cfg`, `ROMv2_patch_1_{0,1}_hdr.bin`, `WIFI_RAM_CODE_8127`; `build.prop` has
  `wifi.interface=wlan0`. There is **no GPS userspace** (no `mnld`, no `gps.*.so`; the Fire 7 has no
  GPS). So BT and WiFi start from the CM image's own MTK stack, **not** from a BSP hunt. The Sony
  dump (`work/device/system/`) is still useless for HALs: Sony replaced the standard userspace with
  their own (`libBt*Service`, the Qt app).
- **WiFi hardware is proven on this unit** (`docs/wifi-test.md`, 2026-10-11): the external WIFITEST
  bundle, loaded RAM-only on the stock 3.10.26 kernel, gave `wlan0`, a 12-BSS scan, WPA2-PSK
  association, DHCP and 0% loss ping. Sony left WiFi out by kconfig, not by hardware, even though the
  product is sold as BT 4.2 + NFC. GPS is untested (no Walkman advertises it). FM is a *separate*
  chip (Silicon Labs **Si4708**, `radio-si4708icx.ko`); Sony's help guide lists FM on the NW-A50
  series except the NW-A55L, so FM depends on this unit's region variant.
- **Device-work rule (hard):** all `adb` against the physical Walkman (serial `<serial>`) is
  **operator-run**. Workers stage exact commands for the operator; they never drive the device
  themselves. The device is **currently unplugged** (charging the laptop), so *all work now is
  device-free* — sourcing, porting, drafting, building. On-device validation is queued for the
  operator. Subagents must **not spawn further agents** (depth 1).
- **Reference template — the audio track** (already done): probe the live kernel nodes → find the
  enable sequence → prove it with a stock tool → write the HAL → bind the kernel `/dev` node into the
  container → replace the stub. See `devlog/2026-10-15-walkman-dac-audio-proven.md` and
  [[nw-a55-audio-dac-alsa]]. The main session is writing the `audio.primary` HAL now; do not touch
  audio.

## Live kernel state (probed 2026-10-15, Sony host adbd)

- Modules Live: `mtk_stp_bt_soc`, `mtk_stp_wmt_soc`, `mtk_wmt_detect` (the WMT/STP transport is up;
  Sony's `6620_launcherA` brought the chip up).
- `/dev`: **`/dev/stpbt` present**, `/dev/wmtdetect` present. **`/dev/stpwmt` and `/dev/wmtWifi`
  absent**, and `/sys/class/net` has no `wlan0` → the **WiFi function is OFF** because stock loads no
  WiFi modules (the WIFITEST bundle supplies them). `/dev/stpgps` was not
  probed, but no GPS chardev module exists to create it (next bullet).
- `work/device/system/lib/modules/` holds exactly four radio modules: `mtk_stp_bt_soc.ko`,
  `mtk_stp_wmt_soc.ko`, `mtk_wmt_detect.ko`, `radio-si4708icx.ko`. **No `mtk_wmt_wifi_soc`, no
  `wlan_mt`, no `mtk_stp_gps_soc`** — Sony built with `CONFIG_MTK_COMBO_WIFI`, `CONFIG_MTK_COMBO_GPS`,
  `CONFIG_MTK_CONNSYS_WIFI` unset. The shipped WMT core still has `mtk_wcn_wmt_func_on/off`,
  `mtk_wcn_wmt_wlan_reg/unreg` and `mtk_wcn_soc_gps_sync_ctrl`, so the function-control path exists;
  only the WiFi/GPS client modules are missing.
- Firmware on the Sony system: `/system/etc/firmware/WMT_SOC.cfg`, `ROMv2_lm_patch_1_0_hdr.bin`,
  `ROMv2_lm_patch_1_1_hdr.bin`, `/system/etc/Patch_02210011_*.bin`, and `/etc/firmware/WIFI_RAM_CODE_8590`
  (what the proven WiFi run used; the CM image's `WIFI_RAM_CODE_8127` is for the wrong chip). The
  kernel loads firmware from the host filesystem, so the container does not need it. Binaries:
  `6620_launcherA/C`, `6620_wmt_concurrency`, `6620_wmt_lpbk`.

## Toolchain / build

- NDK **r21e** at `~/Android/sdk/ndk/21.4.7075529`, target `armeabi-v7a`, API 22.
- Vendor AOSP **android-5.1.1_r38** headers only where a HAL is actually written, the way the audio
  HAL did (`probe/audiohal/aosp-include/`). `curl` has internet here; split URLs carefully (the
  `https://` colon broke a loop once).
- Link against the CM image's libs, not bundled. Inspect it without mounting:
  `debugfs -R "ls /lib/hw" work/android/aosp-ford/system.img.cmstock` (or `fuse2fs -o ro`).
- **Kernel modules:** Sony's GPL tree with its `.config` is at
  `~/.cache/nw-kernel/linux-kernel-3.10.26`, toolchain `~/.cache/arm-eabi-4.8/bin/arm-eabi-`. Build
  out-of-tree the way `probe/kmod/swap-accounting-guard/Makefile` does (`-C $(KDIR)`). Shipped
  vermagic is `3.10.26 SMP preempt mod_unload ARMv7` with **no modversions**, so a module that
  resolves its symbols will load even if a struct layout differs — confirm the tree's `conn_soc`
  source matches the shipped `mtk_stp_wmt_soc.ko` (export list, strings) before trusting a build.
  Kernel modules are C, not cargo, so they build locally.
- **WIFITEST bundle:** `~/Downloads/nwa50-wifi-test.tar.gz` (not in the repo; copy it to
  `work/wifitest/bundle/`). It holds the proven modules `mtk_wmt_wifi_soc.ko`, `compat_bp.ko`,
  `cfg80211_bp.ko`, `mac80211_bp.ko`, `wlan_mt_bp.ko` (insmod order in `wifi_test.sh`), the
  `wlan_mt` patch `0001-wlan_mt-a55-wext-less-kernel-and-assert-abi.patch`, and `BUILD.md`, the
  out-of-tree recipe against Sony's tree (Module.symvers from stock kallsyms + the stock WMT exports,
  CRC 0; **never define `CONFIG_WIRELESS_EXT`**, it changes the `net_device` layout).
  `src-mtk_wmt_wifi_soc/Makefile` is the template for any other CONSYS client module.
  Our fixes to its scripts are in `work/wifitest/run-1011-1223/wifi_test.sh.diff`.
- Container integration pattern: the container's `/dev` is a fresh tmpfs from Android init, so kernel
  nodes are not visible inside. Bind the needed node in, following the existing mali/idc/audio-stub
  binds in `probe/android/android-boot.sh` (the `container()` function): `/dev/stpbt` for BT,
  `/dev/wmtWifi` + the wlan netdev plumbing for WiFi once it exists.

## Workers (each produces groundwork, NOT a claimed-working HAL)

Split by shared layer, not one per HAL: GPS needs the same CONSYS client-module build setup the WiFi
bundle already proves (`BUILD.md`), so it rides with WiFi instead of rebuilding that setup in a
separate worker. At most **two workers at once**: A and B in parallel, C when A finishes.

A and B both touch `probe/android/android-boot.sh` (`container()` binds) and the container init.
They do **not** edit those files directly: each delivers its change as a patch file next to its
devlog, and the orchestrating session applies both and resolves overlaps.

Each worker delivers to the repo: a devlog with the components used (where from, licence), the
driver-node map, a draft container integration (binds + file placement + build recipe), and an
explicit **"operator must validate on device"** checklist.

**A — BT (Sonnet subagent).** Make the CM image's own MTK BT stack run on the Sony kernel. Work out:
which side owns chip bring-up (Sony's host `6620_launcherA` already did it, so the CM init services
`wmt_loader` / `6620_launcher` / `autobt` must not re-initialise it; stage disabling them in the
container init); whether a Sony host BT daemon (`libBt*Service` users) holds `/dev/stpbt` open and
must be stopped first; what CM's `libbt-vendor.so` expects (chip-id probe, NVRAM BD-address via
`libnvram` / `nvram_daemon`, `bt_stack.conf`) and how to satisfy it on the Fire-shaped NVRAM layout;
the firmware-name difference (Sony `ROMv2_lm_patch_*` vs CM `ROMv2_patch_*`, if anything userspace
reads it). Deliver: the `/dev/stpbt` bind, container init changes, any NVRAM stub, and the HCI-up
validation steps (BT on in Settings → hci reset → scan → pair).

**B — WiFi in the container, then GPS (Sonnet subagent).** The kernel side of WiFi is already
proven by the WIFITEST bundle; B's WiFi job is Android integration. Work out: where the modules load
(host side before the container starts, via the bundle's insmod order) and who writes
`echo 1 > /dev/wmtWifi`. CM's MTK `libhardware_legacy` may do the power-on itself when Settings
enables WiFi, so check its strings for `/dev/wmtWifi` and `wifi.*` driver-path properties, and
decide which side owns it. Also work out the `/dev/wmtWifi` bind, whether the container shares the
host network namespace (if not, how `wlan0` gets in), the CM `wpa_supplicant` (nl80211) +
`wpa_supplicant.conf` / init service fit, `dhcpcd` and DNS. Deliver: the integration patch, the
operator steps (Settings → WiFi on → scan → connect), and expected dmesg / logcat lines. Rebuilding
the bundle's modules from `BUILD.md` to verify where they came from (`docs/wifi-test.md` lists it as
open) is optional, after integration works.
Then GPS: build `mtk_stp_gps_soc` (`conn_soc/common/linux/pub/stp_chrdev_gps.c`, gives
`/dev/stpgps`) the way `src-mtk_wmt_wifi_soc/Makefile` builds the WiFi chardev, and stage a
function-on test (`insmod`, open `/dev/stpgps`, the `WMT turn on GPS` dmesg line). GPS userspace
(`mnld` + `gps.*.so`, which CM lacks) waits until that test passes.

**C — FM, Si4708 research (DeepSeek via `dsh-task`, not a Claude subagent).** RE the
`radio-si4708icx.ko` interface from `modinfo` / strings / disassembly (V4L2 radio `/dev/radioX`, or
custom sysfs/ioctl) and Sony's FM userspace in the dump. Android has no standard FM HAL — it is an
app + a JNI shim to the driver. Deliver a research note on the interface, the region caveat
(NW-A55L), and what an Android FM path would take. Research only; Claude reviews the note.

## What "done" means

A subsystem is done the way audio will be: the HAL/driver loads in the CM container, the kernel node
is bound in, and the **operator confirms the real function on the device** (BT scan/pair, WiFi
scan/connect, a GPS fix, FM tune). Groundwork that compiles but is unvalidated is a draft, labelled
as such — never reported as working.
