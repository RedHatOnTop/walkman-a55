# Bluetooth on the MT8590 CONSYS from the CM container — device-free groundwork (DRAFT)

2026-10-15. Second target of the audio/BT/WiFi track (spec: `docs/mt6620-connectivity-hal-brief.md`,
section A). Goal: make the CM 12.1 image's **own** MTK Bluetooth stack (`libbt-vendor.so`,
`libbluetooth_mtk.so`, `bluetooth.default.so`) work on the Sony 3.10.26 kernel inside the container,
using the radio the Sony host already brought up.

**Status: nothing here has run on the device.** The device was unplugged for this pass. Everything
below is static analysis (strings, ELF/disassembly, kernel and ramdisk source, a host-built unit
test) plus a set of patches and an operator checklist. Each claim is marked VERIFIED (read straight
from a file, compiled, or tested on the workstation) or UNVERIFIED (inferred, needs the device). The
split is summarised near the end. Deliverables are in `probe/android/connectivity/bt/`.

## Result in five lines

1. CM's Bluetooth userspace is thin. `libbt-vendor.so` (5240 B) does one thing that matters:
   `open("/dev/stpbt", O_RDWR|O_NOCTTY|O_NONBLOCK)`. There are **no ioctls**, no rfkill, no
   `/dev/stpwmt`; power-on and the firmware-patch download happen in the kernel's `BT_open`.
2. CM's HCI init script runs **only if `persist.mtk.wcn.combo.chipid` is `0x8127`**. This kernel is
   chip 0x8590. The container therefore sets that property to `0x8127` (init patch). Sony's own
   `libbtdrv.so` sends the same six vendor commands for 0x8590 (UNVERIFIED that the firmware accepts
   CM's script at runtime; this is the main risk).
3. The CM image's connectivity services (`wmt_loader`, `6620_launcher`, `nvram_daemon`, `autobt`)
   are **not in the container init at all**, so there is nothing to disable. The real conflict is
   Sony's `mtkbt`, which `hagodaemons.sh stop` leaves running and which can hold `/dev/stpbt`.
   `6620_launcherA` must stay running. The patch stops `mtkbt` and adds a guard.
4. Firmware is kernel-only. The kernel reads `ROMv2_lm_patch_*` from the **host's**
   `/system/etc/firmware`; the container needs no firmware files. CM's `ROMv2_patch_*` are never read.
5. The BD address comes from a 64-byte NVRAM record. Without NVRAM the library uses a built-in
   record and asks the chip for its eFUSE address. A tiny `libnvram` shim plus a generator script let
   us pick the address instead of fighting the real libnvram.

## Q1. What CM's libraries expect

Extraction and analysis (all offline): `debugfs -R "dump /lib/libbt-vendor.so ..."` on
`work/android/aosp-ford/system.img.cmstock` (the customised `system.img` has identical BT libs),
`strings -a`, `readelf -dsW`, and a capstone/pyelftools annotated Thumb disassembler for the
literal-pool strings. Line numbers in the log format (`%s #%d`) were read out of the code.

**libbt-vendor.so** (exports `BLUETOOTH_VENDOR_LIB_INTERFACE`, imports `lab126_log_write`, which
Amazon's `liblog` provides and which writes logcat tag `AMZ-[BT]`). `mtk_bt_op` dispatches:
`POWER_CTRL` and the LPM ops only log; `USERIAL_OPEN` calls `init_uart` and returns 1 fd;
`USERIAL_CLOSE` calls `close_uart`; `FW_CFG` calls `mtk_fw_cfg`; `SCO_CFG` calls `mtk_sco_cfg`;
`GET_LPM_IDLE_TIMEOUT` returns 5000; `EPILOG` calls `mtk_prepare_off`. (VERIFIED, disassembly.)

**libbluetooth_mtk.so** (17 564 B; NEEDED `libnvram.so`, `libcutils.so`, `liblog.so`):

| What | Detail |
|---|---|
| device node | `init_uart`: `open("/dev/stpbt", 0x902)` = `O_RDWR\|O_NOCTTY\|O_NONBLOCK`. Fail log: `Can't open serial port`. **No ioctl imports at all.** (VERIFIED) |
| chip id | `property_get("persist.mtk.wcn.combo.chipid")`, 10 tries x 0.5 s, rejects empty and `"-1"`, `strtoul(...,16)`. Then `Get combo chip id retry N`, `Combo chip id %x`. (VERIFIED) |
| chip-id gate | `GORM_FW_Init_Thread` runs a script only if id == `0x8127` (`bt_init_preload_script_consys`, 7 steps); other ids give `Unknown combo chip id` / `No matching init script` and the callback is called with failure. `GORM_SCO_Init_Thread` likewise; `bt_init_postload_script_consys` is a 4-byte `.bss` object, i.e. an empty SCO script. `bt_read_nvram` has built-in default records for 0x6630, 0x8127 and 0x6628 only. (VERIFIED static; SCO emptiness at runtime UNVERIFIED) |
| NVRAM wait | `service.nvram_init` must read `Ready`, 10 x 0.5 s; on timeout `Get NVRAM restore ready fails!` and the lib **skips `NVM_GetFileDesc` entirely** and uses the built-in record (not an error). (VERIFIED, disassembly 0x10b8-0x111a) |
| NVRAM read | `NVM_GetFileDesc(lid=1, isRead=1)` -> needs `recNum==1`, `recSize==0x40`, reads 64 bytes; all-zero or any failure falls back to the defaults. (VERIFIED) |
| BD address override | `/proc/idme/bt_mac_addr` (Amazon). Open fails harmlessly here. (VERIFIED) |
| eFUSE address | script step 0 `Get_Local_BD_Addr` (HCI 0x1009) runs only when the NVRAM address is all-zero or the 0x8127 placeholder `00:00:46:81:27:01`; the eFUSE address then replaces it. Step 1 `Set_Local_BD_Addr` (0xfc1a) logs which of four cases applied (`eFUSE address default value` / `eFUSE address has valid value` / `NVRAM BD address default value` / `NVRAM BD address has valid value`) and calls `WriteBDAddrToNvram` unless the NVRAM address was already valid. (VERIFIED, disassembly 0x1f60-0x20b8, 0x21c4-0x23fa) |
| script | 0 Get_Local_BD_Addr 0x1009, 1 Set_Local_BD_Addr 0xfc1a, 2 Set_Radio 0xfc79, 3 Set_TX_Power_Offset 0xfc93, 4 Set_Sleep_Timeout 0xfc7a, 5 HCI Reset 0x0c03, 6 Set_FW_SysLog 0xfcbe. Each logs `The event of command N success\|error`. (VERIFIED) |
| other properties | `persist.bt.syslog.enable` only. |

**bluetooth.default.so** (bluedroid, 1.3 MB): `dlopen("libbt-vendor.so")` +
`dlsym(BLUETOOTH_VENDOR_LIB_INTERFACE)`; reads `/etc/bluetooth/bt_stack.conf` (BtSnoop false, trace
levels 2), `bt_did.conf` (no active entries), `ble_stack.conf`, `auto_pair_devlist.conf`; writes
`/data/misc/bluedroid/bt_config.xml`; uses `/dev/uhid`, `/dev/uinput`, `/dev/tun`. Its HCI layer has
only an epilog timer (no preload timer string). `Bluetooth.apk`'s `AdapterState` has
`START_TIMEOUT_DELAY=5000` and `ENABLE_TIMEOUT_DELAY=12000` (read with `dexdump`). (VERIFIED)

`libbluetooth_mtk_pure.so` is the engineering-test variant (used by `autobt` / `libbluetooth_hw_test`),
not bluedroid. The image also carries `bdt`, `sdptool`, `gatt_testtool`, `idme`. `svc bluetooth` does
not exist in this image's `svc.jar`; toggle through Settings. `dumpsys bluetooth_manager` exists.
`tablet_core_hardware.xml` declares `android.hardware.bluetooth`; `bluetooth_le.xml` is present.

Library loading is side-effect free (no `INIT_ARRAY` in `libnvram`, `libcustom_nvram`,
`libnvram_platform`, `libnvram_sec`, `libbluetooth_mtk`, `libbt-vendor`), and the transitive NEEDED
closure of `libbt-vendor.so` (12 libraries) resolves entirely in `/system/lib`. (VERIFIED)

## Q2. Who brings the chip up (CM services versus the container)

Search of the container init (`probe/android/container/init.aosp.rc`, `init.rc`): **none** of
`wmt_loader`, `6620_launcher`, `autobt`, `nvram_daemon`, `NvRAMAgent` or any bluetooth service is
started. (VERIFIED, grep.) The Fire ramdisk's `init.mt8127.rc` / `init.project.rc` do start them,
but those files are not in the container. What they would have done, for the record:

- CM `wmt_loader`: `insmod` from `/system/lib/modules`, probes `/dev/wmtdetect`, **sets
  `persist.mtk.wcn.combo.chipid`** (`0x%04x`). It would overwrite our `0x8127`.
- CM `6620_launcher`: opens `/dev/stpwmt`, which does not exist on this kernel (the node is
  `/dev/stpwmtA`) and would fight Sony's launcher for the patch RPC.
- `nvram_daemon`: restores the NVRAM tree from the eMMC `NVRAM` partition (forbidden by project
  rules) and sets `service.nvram_init=Ready`.
- `autobt`: an engineering BT test tool (`HW_TEST_BT_*`), not a daemon.

So "disable" means "do not add". The android-boot patch makes that a hard guard: `container()` dies
if any `$ROOT/init*.rc` contains a `service` line for those names. The init patch only adds the
chip-id property, `/data/misc/bluedroid`, and a comment listing what must never be added.

## Q3. Sony host side

From the Sony ramdisk (`work/ramdisk-check/root/init.project.rc`, `init.rc`):

- `wmtLoader` is started in `post-fs-data` (oneshot); it brings up the modules and does
  `ctl.start 66xx_launcherA` (`/system/bin/6620_launcherA -p /system/etc/firmware/`, class core,
  disabled). `init.project.rc` also pre-creates `mknod /dev/stpbt c 192 0` (and a stale
  `/dev/stpwmt c 190`).
- `init.rc` boot section: `start nvram_daemon`, `chmod 777 /dev/stpbt`, `start mtkbt`
  (`service mtkbt /bin/mtkbt`, user system, oneshot): Sony's BlueAngel stack.
- `hagodaemons.sh stop` stops only the `hagoromo*` services of `/init.hagoromo.rc`
  (`BtCommonService` and friends, which talk to `mtkbt` over `/dev/socket/bt.int.adp`). `mtkbt` and
  `6620_launcherA` are **not** hagodaemons. The ps snapshot in
  `debloat/REPORT-recovered-20261014.md` (lines 359-362, 652-653) shows both alive as children of pid
  1 while the container ran. (VERIFIED from the files; that they still hold the node on a live boot
  is UNVERIFIED.)
- Only Sony's `libbtdrv.so` mentions `/dev/stpbt`. Its users are `mtkbt`, `bt_drv`, `hci_cmd`,
  `cmd_bridge` (the last three look like factory tools using `/tmp/hcicmd_socket`; their use on a
  normal boot is UNVERIFIED).
- Kernel `stp_chrdev_bt.c` does not refuse a second opener: `open` calls
  `mtk_wcn_wmt_func_on(WMTDRV_TYPE_BT)`, `close` calls `func_off`, and both openers would share one
  rx queue. `mtkbt` opens the node on a BT-on request (`mtk_bt_enable` strings), so it only holds it
  if Sony BT was left on.

Decision, implemented in `android-boot.sh.patch` (`bt_host_release`, called by `up()` after
`prepare`): `stop mtkbt`, then warn (never die) if `mtkbt` is still running, if `6620_launcherA` is
not running, if `/dev/stpbt` is missing, or if any pid still has it open. `6620_launcherA` is left
alone on purpose (Q5). After `down` the Sony stack is not restored without a reboot anyway, because
of the Mali swap.

**Why no bind.** The brief and the audio devlog suggest binding the node into the container. For
`/dev/stpbt` that cannot work: Android init in the container mounts a fresh tmpfs on its `/dev`
after `container()` returns (comment at the top of `probe/android/android-boot.sh`), so a prior bind
is covered, and ueventd's cold boot creates nodes from `/sys/class` exactly as it does for
`/dev/snd/*` (comment near line 156). The container's `ueventd.icx.rc` (built by
`tools/android-container.sh` from the Fire `ueventd.mt8127.rc`) has `/dev/stpbt 0660 bluetooth
radio`, and the kernel registers class `stpbt` (`WMT_CREATE_NODE_DYNAMIC=1`, major 192). The patch
adds `btdev` instead (report, `mknod c 192 0` fallback from `/sys/class/stpbt/stpbt/dev`). The same
argument applies to `/dev/snd`: worth checking against the audio session's plan.

## Q4. NVRAM, the BD address, and the stub

The record (`ap_nvram_btradio_struct`, 64 bytes) from the log format string and Sony's own default:

| off | len | field | | off | len | field |
|---|---|---|---|---|---|---|
| 0 | 6 | BD address (MSB first) | | 25 | 2 | BtFTR |
| 6 | 2 | Voice | | 27 | 3 | TxPWOffset |
| 8 | 4 | Codec | | 30 | 6 | CoexAdjust |
| 12 | 6 | Radio | | 36 | 28 | reserved |
| 18 | 7 | Sleep | | 64 | 2 | `0xAA` + check number (file is 66 bytes) |

The check number is `NVM_ComputeCheckNo`: alternating add/xor over the 64 bytes. It reproduces the
trailer `aa e9` of Sony's `/system/etc/BT_Addr_ForWM_Default` (VERIFIED, `gen-bt-nvram.py
--self-test`). CM's built-in 0x8127 record equals Sony's `libbtdrv` `stBtDefault_8590` except the
placeholder address (`00:00:46:85:90:01` on Sony, `00:00:46:81:27:01` on CM). Sony's
`BT_Addr_ForWM_Default` is the Walkman-tuned variant (radio `07 80 00 06 07 07`, BtFTR `00 00`,
TxPWOffset `00 00 00`) and is what the generator uses by default; `--template cm8127` gives CM's.

Where the real address lives (UNVERIFIED): probably the Sony host's
`/data/nvram/APCFG/APRDEB/BT_Addr` (written at the factory through the `nvram_rw` USB-SCSI path;
Sony `diag_main` initialises it from the ForWM default). Step 1 of the checklist pulls it and
`gen-bt-nvram.py --addr-from-file` builds the seed. `/system/etc/BT_Addr_ForWM_Default` has a zero
address, so it is not a source.

What the real `libnvram` needs and why it is a poor fit (VERIFIED by reading the library and
`libcustom_nvram`): `NVM_GetFileDesc(lid=1)` maps to `/data/nvram/APCFG/APRDEB/BT_Addr` (recSize
0x40, recNum 1, version 2) and needs `FILE_VER` (`/data/nvram/APCFG/APRDCL/FILE_VER`, 0x4a-byte
records, header `NVRAM_VER_INFO`), a protect/check pass, write access for uid bluetooth (dirs
`root:system 2771`), and, **if `/data/nvram/RestoreFlag` exists and the file is missing, a restore
from the eMMC `NVRAM` partition**. With no `/data/nvram` it should fail safely and the library
falls back to its defaults (UNVERIFIED).

Plan:
- **Phase 1 (default)**: no NVRAM. Built-in record, placeholder address triggers the eFUSE query, the
  address the stack uses is the chip's eFUSE address if valid. `service.nvram_init` stays unset, so
  the read path is skipped (a 5 s wait per enable). The write-back after the eFUSE query still calls
  the real libnvram (UNVERIFIED that it fails harmlessly).
- **Phase 2 (optional)**: `nvram-shim/btnvshim.c` (two functions, `NVM_GetFileDesc` /
  `NVM_CloseFileDesc`, soname `libnvram.so`) is bound read-only over the container's
  `/system/lib/libnvram.so`. Reads serve `/data/nw/bt_nvram.bin`; the address write-back goes to
  `/data/nw/bt_nvram.written` and never touches `/data/nvram` or any partition. Requires
  `setprop service.nvram_init Ready` to be consumed. The shim is compiled with the NDK for
  armv7a API 22 (exports only the two symbols, NEEDED `liblog`, `libdl`, `libc`); ABI recovered from
  the ARM disassembly of `libnvram.so` and the caller (hidden F_ID return pointer in r0, lid in r1,
  `&recSize` r2, `&recNum` r3, `isRead` on the stack); the host logic test passes. Nothing in the
  container other than `libbluetooth_mtk*.so` imports it (the CM stock audio HAL that does is
  replaced by ours).

## Q5. Firmware

Kernel-only. In `conn_soc` (`common/core/wmt_conf.c`, `wmt_ctrl.c`, `common/linux/pub/osal.c`,
`wmt_dev.h`): `CUST_CFG_WMT_PREFIX "/system/etc/firmware/"`; `wmt_dev_patch_get` reads the patch and
`WMT_SOC.cfg` with `filp_open` from WMT kthreads created by `osal.c` (`kthread_create`), i.e. in the
**host** filesystem context, regardless of the container's `pivot_root`. Sony's `6620_launcherA`
(`-p /system/etc/firmware/`) additionally opens the patch headers and hands names/addresses to the
kernel through ioctls on `/dev/stpwmtA`; the kernel RPCs it with `srh_patch`. The host has
`ROMv2_lm_patch_1_0_hdr.bin`, `ROMv2_lm_patch_1_1_hdr.bin`, `WMT_SOC.cfg` and `WIFI_RAM_CODE_8590`
(`work/device/system/etc/firmware/`); CM's `ROMv2_patch_*` and `WIFI_RAM_CODE_8127` are never opened
by anything that runs here, and CM's 80-byte `WMT_SOC.cfg` is identical to Sony's. The container's
BT code never opens a firmware file. (VERIFIED from source and strings; the actual download on
`BT_open` is UNVERIFIED.)

## Driver-node map

| Node | Kernel module | Major | Who opens it | In the container |
|---|---|---|---|---|
| `/dev/stpbt` | `mtk_stp_bt_soc` (class `stpbt`, `stp_chrdev_bt.c`) | 192 (module param `BT_major`) | Sony `mtkbt` (stopped by the patch); in the container `libbluetooth_mtk` | created by ueventd (`bluetooth radio 0660`), `btdev` mknod fallback |
| `/dev/stpwmtA` | `mtk_stp_wmt_soc` (class `stpwmtA`) | 200 | Sony `6620_launcherA` only | **not needed, not present**; do not stop the launcher |
| `/dev/wmtdetect` | `mtk_wmt_detect` | dynamic | Sony `wmt_loader` | not needed |
| `/dev/stpwmt` | none | 190 (stale `mknod` in Sony init) | CM `6620_launcher` | does not exist; CM's launcher must not run |

Kernel config has `CONFIG_MTK_COMBO_CHIP_CONSYS_8590=y` (the 8127 variant is not set). The kernel
also tries `filp_open("/data/BT_Addr", O_WRONLY|O_CREAT)` in the opener's root on `BT_open` only to
log it (`nvram_read: failed to open!!` for uid bluetooth is expected and harmless; the failure
path forgets `set_fs(old_fs)`, a driver quirk that leaves the calling thread on `KERNEL_DS`).

## Components used

| Piece | Where | From |
|---|---|---|
| `libbt-vendor.so`, `libbluetooth_mtk.so`, `bluetooth.default.so`, `libbluetooth_jni.so` | CM image `/system/lib`, `/system/lib/hw` | CM 12.1 `system.img` (stock, not modified) |
| `libnvram.so` and friends (`libcustom_nvram`, `libnvram_platform`, `libnvram_sec`) | CM image `/system/lib` | CM image; optionally shadowed by the shim |
| `bt_stack.conf`, `bt_did.conf`, `ble_stack.conf`, `auto_pair_devlist.conf` | CM image `/etc/bluetooth` | CM image |
| `Bluetooth.apk`, `Bluetooth` app JNI symlink | CM image `/system/app/Bluetooth` | CM image |
| kernel BT / WMT drivers, firmware patches | host kernel, host `/system/etc/firmware` | Sony |
| `6620_launcherA`, `wmt_loader` | host `/system/bin` | Sony; **left running/untouched** |
| `mtkbt`, `libbtdrv.so`, `BT_Addr_ForWM_Default` | host | Sony; `mtkbt` stopped, rest reference only |
| `gen-bt-nvram.py`, `nvram-shim/` | `probe/android/connectivity/bt/` | this work |

## Deliverables (`probe/android/connectivity/bt/`)

- `android-boot.sh.patch`: `bt_host_release`, `bt_holders`, `btcheck`, `btdev`; `up()` calls
  `bt_host_release` and then `btdev`; `container()` refuses connectivity services in the container
  init and optionally binds the shim and copies the seed; `down()` unmounts the shim.
- `init.aosp.rc.patch`: `/data/misc/bluedroid`, `setprop persist.mtk.wcn.combo.chipid 0x8127`,
  comment on `service.nvram_init` and the forbidden services.
- `tools-android-boot.sh.patch`: `btcheck`, `btdev` verbs; `NW_BT_SHIM` + `NW_BT_SEED` env push.
- `gen-bt-nvram.py`, `nvram-shim/` (`btnvshim.c`, `test_btnvshim.c`, `build.sh`).
- `operator-checklist.md`: the ordered, operator-run adb procedure with expected output and a
  failure-meaning table.

All three patches were generated with `diff -u` against the live files at HEAD 2f2b98c, apply with
`git apply --check` against the working tree (other sessions have uncommitted changes elsewhere,
not in these three files), and the patched shell files pass `bash -n`.

## VERIFIED versus UNVERIFIED

VERIFIED (static or workstation):
- Library behaviour listed in Q1 (strings, symbol tables, disassembly, `dexdump` of `Bluetooth.apk`).
- Record layout and check number (reproduces Sony's `aa e9`); the generator's self-test.
- The shim compiles with the NDK (armv7a, API 22), exports only the two symbols, host logic test
  passes, ARM ABI inferred from disassembly.
- Container init starts none of the MTK services; Sony init starts `mtkbt`, `nvram_daemon`,
  `wmtLoader` and `66xx_launcherA` as described; `hagodaemons.sh` does not stop them.
- Kernel source paths for patch loading, node creation and the `BT_open` log lines.
- Patches apply, shell parses.

UNVERIFIED (needs the device; most of these are what the checklist collects):
- That ueventd creates `/dev/stpbt` in the container (fallback exists).
- That the 8590 firmware accepts CM's 0x8127 script, and what the eFUSE address is (it may be blank).
- The host's `persist.mtk.wcn.combo.chipid` value, and whether `mtkbt` holds the node.
- Where Sony's real BD address lives.
- That the real `libnvram` fails safely on the write-back in phase 1 (otherwise the shim is the fix).
- That a 5 s NVRAM wait plus kernel power-on fits in `ENABLE_TIMEOUT_DELAY` (12 s).
- The `com.android.bluetooth` ("Bluetooth Share") force-close loop recorded in
  `2026-10-14-cm-ford-first-boot.md`: cause unknown, trace never saved; step 3 captures it first.
  Static checks (the app's JNI symlink, dependency closure) found nothing wrong. The loop predates
  the debloat pass 2 image (9228d7b, which removed `TeleService`/`Telecom` and kept the telephony
  providers), so that pass did not cause it. The HFP/HSP audio gateway would bind to the now-absent
  `com.android.phone`, but it is switched off (next section), so `HeadsetService` does not start.
- Whether A2DP works (no audio route exists yet), `service call
  bluetooth_manager 6` as enable().

## A2DP-only profile set (user decision 2026-10-15)

The Walkman has no calls, and an advertised HFP/HSP gateway lets a headset fall into narrowband SCO
call mode. Bluetooth is therefore A2DP + AVRCP only. A static resource overlay
(`a2dp-only-overlay/`, built into `work/android/overlay/BluetoothA2dpOnly.apk`) sets
`profile_supported_{hs_hfp,pbap,map,opp,pan,hid,hidd,hdp}` to false in `com.android.bluetooth`.
`Config.init()` starts only the profile services whose flag is true, so these services never start
and their SDP records are never registered. AVRCP is part of `A2dpService`. GATT/BLE stays on: a
headset does not connect to it as a classic profile. Flags CM already ships off (`a2dp_sink`,
`hfpclient`, `avrcp_controller`) are untouched.

Why an overlay: `Bluetooth.apk` is untouched, so it needs no re-sign and no image edit; the image is
shared with the audio session. `/system/bin/idmap` is in the image, and PMS treats `/vendor/overlay`
packages as trusted overlays, so they skip the signature check. The container's `/vendor` was a
symlink into read-only `/system/vendor`. `android-boot.sh` now makes it a real directory: one
symlink per `/system/vendor` entry, plus `overlay/`. VERIFIED offline: the APK's manifest
(`<overlay targetPackage="com.android.bluetooth" priority="1">`), its eight bools, and its v1
signature (`apksigner verify --min-sdk-version 22`). UNVERIFIED: that this CM 12.1 PMS creates the
idmap and applies it to the Bluetooth app (checklist steps 3 and 6).

## Corrections to the brief

1. `/dev/stpwmt` is not missing because a module is absent: the node is `/dev/stpwmtA` by design,
   and only Sony's launcher uses it. Sony's `mknod /dev/stpwmt c 190 0` is stale.
2. Nothing needs to be disabled in the container: the CM connectivity services are not in its init.
   The conflicts are Sony's `mtkbt` (not stopped by `hagodaemons.sh`) and the chip-id gate.
3. A host-side bind of `/dev/stpbt` is the wrong tool; ueventd makes the node.
4. Firmware names (`ROMv2_lm_patch` vs `ROMv2_patch`) are irrelevant to userspace; the kernel and the
   host launcher own them.
5. `libnvram` is not the chip-id source; it is a property (`persist.mtk.wcn.combo.chipid`).

## Open questions for the first device run

1. Does `BT_open` succeed from the container (kernel log lines in checklist step 4)?
2. Does every command 0-6 return `success` with chipid faked to 0x8127? If one errors, which?
3. What is the chip's eFUSE address, and what does Sony have in `BT_Addr`?
4. Does the enable finish inside 12 s, or is `service.nvram_init Ready` needed?
5. Does `com.android.bluetooth` crash before the radio is touched, and why?
6. Does scanning find a phone and does pairing write `bt_config.xml`?

## The A2DP-only overlay is inert on CM 12.1; patched Bluetooth.apk instead (2026-10-15 night)

Device run by distribution-project-30 on the `NW_NO_WIFI` home boot. `/vendor/overlay/BluetoothA2dpOnly.apk`
was present and PackageManager listed it (`org.nwa55.overlay.bluetooth`). But `dumpsys
bluetooth_manager` still had `HeadsetService`, `A2dpService` and AVRCP all enabled, and
`/data/resource-cache` had only a 0-byte `overlays.list`.

Why, from CM's own code (`services.jar` / `framework.jar` of the ford image, dexdump):
- `PackageManagerService` scans `/vendor/overlay` with `SCAN_TRUSTED_OVERLAY`, and
  `createIdmapForPackagePairLI` runs `installd idmap`. The output goes to
  `ThemeUtils.getTargetCacheDir(target, overlay)`, CM's theme cache, not the AOSP
  `vendor@overlay@...@idmap` name. Then the method returns. The AOSP tail that sets the target's
  `applicationInfo.resourceDirs` is gone: services.dex never writes `resourceDirs`.
- `ResourcesManager.attachThemeAssets` adds overlay paths only for the package that `ThemeConfig`
  names as the applied theme for that app (`getOverlayPkgNameForApp`, `mOverlayTargets`). An
  ordinary RRO is never that.
- `overlays.list` is native AssetManager's list of framework-res overlays; that path still works,
  but it never touches an app's own resources.

So on CM 12.1 a static overlay can change framework-res only.

Fix: `probe/android/connectivity/bt/a2dp-only/build.sh`. It reads `/app/Bluetooth/Bluetooth.apk` from
the user's own image, and `flip_bools.py` rewrites the eight `profile_supported_*` bools in
`resources.arsc` in place: hs_hfp, pbap, map, opp, pan, hid, hidd and hdp, each true -> false. The
table keeps its size and layout. The script re-signs v1 with the AOSP test platform key, which is
the key the CM build used (stock cert SHA-1 `27196e386b875e76adf700e7ea84e4c6eee33dfa`; the script
refuses any other). That keeps the platform signature and the `android.uid.bluetooth` shared user.
Host result: built, cert SHA-1 identical to stock, `aapt dump resources` shows the eight false
with a2dp and gatt still true. Only 32 bytes of `resources.arsc` differ, and every other entry's
CRC is unchanged except `META-INF`. `tools/android-boot.sh up` pushes `work/android/bt/Bluetooth.apk`,
`container()` binds it read-only over `/system/app/Bluetooth/Bluetooth.apk`, and `down` unbinds it.

Device result (distribution-project-30, same night, `NW_NO_WIFI` home boot):
- `up` printed the `Bluetooth: ... <- /tmp/bt-apk/Bluetooth.apk (A2DP only, ro)` line.
- `pm path com.android.bluetooth` = `package:/system/app/Bluetooth/Bluetooth.apk`, and logcat had
  no signature, mismatch or INSTALL_FAILED line.
- Bluetooth came up: `bluetooth_on=1`, adapter state 11 (ON).
- `dumpsys activity services com.android.bluetooth` listed only `.btservice.AdapterService`,
  `.a2dp.A2dpService` and `.gatt.GattService`. HeadsetService and the Opp/Pan/Hid/Map/Pbap/Health
  services were gone, and `bluetooth_manager` no longer shows a `HeadsetStateMachine`.

Pairing a real A2DP headphone was still running when this was written.
