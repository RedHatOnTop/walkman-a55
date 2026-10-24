# Bluetooth in the CM container: operator checklist (DRAFT, nothing here has run on the device)

Goal: BT on in Settings -> HCI up (`hci reset` acknowledged, vendor init succeeded) -> scan sees a
nearby phone -> pair. Findings and the VERIFIED / UNVERIFIED split are in
`devlog/2026-10-15-bt-consys-groundwork.md`. Every command below is operator-run; the groundwork
workers never touched the device.

Conventions (run from `~/Projects/nw-a55`):

```bash
A() { adb -s <serial> "$@"; }          # the Walkman, Sony host side
C() { tools/android-boot.sh sh "$*"; }       # one command inside the running container
BB=/system/xbin/busybox                      # busybox on the Sony host
mkdir -p work/android/bt
```

Filter output on the workstation (`C '...' | grep ...`); the container has no reliable `grep`.

How to read the expected output. Lines marked **(code)** come from strings and disassembly of the
shipped binaries or from the kernel source, so the wording is exact. Lines marked **(unknown)**
are device facts nobody has read yet. Write down what you see; that is the data this checklist
exists to collect. Log lines of the CM vendor lib go to logcat tag `AMZ-[BT]` and have the form
`<function>: <message>` or `<function> #<source line>`.

## 0. Workstation (device not needed)

The three patches in this directory are already merged into `probe/android/android-boot.sh`,
`probe/android/container/init.aosp.rc` and `tools/android-boot.sh`, together with the WiFi ones
(`../wifi/`). The `.patch` files are kept as the record of the change; do not apply them again.
The init change travels in `container.tgz`, which `tools/android-boot.sh up` rebuilds/pushes; if
your flow caches that archive, rebuild it.

Build the A2DP-only Bluetooth.apk (user decision 2026-10-15: no HFP/HSP call mode, A2DP + AVRCP
only; GATT/BLE kept). `up` pushes it and `android-boot.sh` binds it over the container's
`/system/app/Bluetooth/Bluetooth.apk`. The earlier static overlay (`a2dp-only-overlay/`) is inert
on CM 12.1 (device run 2026-10-15: listed by PackageManager, never applied); do not rely on it.

```bash
probe/android/connectivity/bt/a2dp-only/build.sh   # ends with "built .../Bluetooth.apk (..., cert 27196e38...)"
```

Nothing else is needed for the first run. The libnvram shim and the address seed (step 8) are a
second-run option.

## 1. Host pre-flight, Sony stack still up (changes nothing)

```bash
tools/android-boot.sh btcheck
```

| Line | Expected | If it differs |
|---|---|---|
| ps list of `mtkbt`, `6620_launcherA`, `nvram_daemon` | all three present **(code: init.rc `start mtkbt`; Sony wmt_loader starts `66xx_launcherA`)** | no `6620_launcherA`: stop here and report. The kernel asks that process for the firmware patches on every chip power-on, so the container cannot bring BT up without it. |
| `ls -l /dev/stpbt /dev/stpwmtA /dev/wmtdetect` | three char devices **(code: all three are created by `class_create` in the shipped modules)** | `/dev/stpwmt` (no `A`) never exists on this kernel; that is normal and not an error. Missing `stpbt` means the module is not loaded. |
| `persist.mtk.wcn.combo.chipid = [...]` | `0x8590` **(unknown; the kernel is built for chip 0x8590)** | Empty or another value: report it. The container's chip-id property is set to `0x8127` on purpose either way, but the real value is evidence. |
| `service.nvram_init = [...]` | `Ready` **(unknown)** | informational |
| `pids holding /dev/stpbt: [...]` | empty if Sony BT is off, `mtkbt`'s pid if it was left on **(unknown)** | informational; step 2 stops `mtkbt` |
| `mtk_stp_bt_soc`, `mtk_stp_wmt_soc`, `mtk_wmt_detect` lines from `/proc/modules` | all three | missing: the Sony radio stack is not up |

Read the unit's real BD address while the Sony stack still owns the NVRAM tree:

```bash
A shell "ls -l /data/nvram/APCFG/APRDEB/BT_Addr"
A pull /data/nvram/APCFG/APRDEB/BT_Addr work/android/bt/sony_BT_Addr
python3 -I probe/android/connectivity/bt/gen-bt-nvram.py --check work/android/bt/sony_BT_Addr
```

- Expected **(code for the format, unknown for the content)**: 66 bytes, the first six are the
  address, the tail is `aa xx`, `--check` prints `OK` for the trailer. Note the address.
- `No such file`: fine. Sony creates the file lazily, or keeps the address elsewhere. Look at
  `A shell "logcat -d | $BB grep -iE 'BD address|BDAddr'"` and write down any address Sony logged.
- An all-zero or `00:00:46:...` address means Sony itself relies on the chip's eFUSE address.

## 2. Bring the container up

```bash
NW_DROP_SYS_BOOT=1 NW_SYS_IMG=work/android/aosp-ford/system.img \
  NW_CONTAINER_VARIANT=aosp tools/android-boot.sh up
```

New lines this adds to the usual output:

| Output | Meaning |
|---|---|
| `== release /dev/stpbt from the Sony stack` followed by no `!!` line | `mtkbt` stopped, `6620_launcherA` alive, node free. |
| `!! mtkbt is still running` | `stop mtkbt` did not work. `A shell stop mtkbt`, check with `btcheck`. Two openers would share one rx queue and either `close` powers BT off for both. |
| `!! 6620_launcherA is not running ...` | BT cannot power the chip up (see step 1). |
| `!! /dev/stpbt is still open in pid(s): N` | something else holds it: `A shell "ls -l /proc/N/fd; cat /proc/N/cmdline"`. |
| `!! refusing to start: ... runs MTK connectivity bring-up` | someone added `wmt_loader` / `6620_launcher` / `nvram_daemon` to the container init. Remove it. |
| `Bluetooth: .../system/app/Bluetooth/Bluetooth.apk <- /tmp/bt-apk/Bluetooth.apk (A2DP only, ro)` | the patched APK is in place. Missing line: step 0's build was skipped (the PC side printed `no work/android/bt/Bluetooth.apk`). |
| (end of output) `== /dev/stpbt in the container` then `crw-rw---- bluetooth radio 192, 0 ... /dev/stpbt` | **(code for the rule and the major; unknown that it happens)** ueventd made the node from `/sys/class/stpbt/stpbt` under the `bluetooth radio 0660` rule. |
| `ueventd did not create /dev/stpbt; mknod c 192 0` | the fallback ran. Not an error, but record it: the devlog's "ueventd creates it" claim was wrong. |

If `up` fails for a non-Bluetooth reason, that is the existing bring-up, not this work.

## 3. Container checks before touching Bluetooth

```bash
C 'getprop persist.mtk.wcn.combo.chipid; getprop service.nvram_init; ls -ld /data/misc/bluedroid; ls -l /dev/stpbt; ps' | grep -E '^0x|^$|bluedroid|stpbt|wmt|6620|nvram|mtkbt'
```

Expected: `0x8127`; an empty line (`service.nvram_init` is empty on purpose);
`drwxrwx--- bluetooth net_bt_stack ... /data/misc/bluedroid`; `crw-rw---- bluetooth radio ...
192, 0 ... /dev/stpbt`; and no `wmt`/`6620`/`nvram`/`mtkbt` process.

Failure: empty chip id or missing `/data/misc/bluedroid` means the `init.aosp.rc` patch is not in the
`container.tgz` that was pushed. Re-check step 0.

A2DP-only APK accepted:

```bash
C 'pm path com.android.bluetooth; logcat -d | grep -iE "bluetooth" | grep -iE "signature|mismatch|inconsistent" | tail -20'
```

Expected **(unknown that it happens)**: `package:/system/app/Bluetooth/Bluetooth.apk` and no
signature line. PackageManager rescans the APK because its timestamp changed; a signature complaint
means the image is not the test-keys build that the build script checked.

Baseline before enabling anything. The first CM boot recorded `com.android.bluetooth`
("Bluetooth Share") in a force-close loop (devlog 2026-10-14-cm-ford-first-boot) and nobody saved
the trace. It decides whether step 4 can work at all:

```bash
C 'logcat -d -v time' > work/android/bt/baseline-logcat.txt
grep -n -B2 -A25 'FATAL EXCEPTION' work/android/bt/baseline-logcat.txt | grep -B3 -A25 -i bluetooth | head -80
```

- Empty: the app is not crashing; continue.
- `UnsatisfiedLinkError` / `dlopen failed` naming `libbluetooth_jni.so`: a library problem, not the
  radio. Run `C 'ls -l /system/app/Bluetooth/lib/arm /system/lib/libbluetooth_jni.so'`, send the
  output, and stop. (Static check: the app's `lib/arm/libbluetooth_jni.so` is a symlink to
  `/system/lib/libbluetooth_jni.so`, and every dependency of the vendor chain resolves in `/system/lib`.)
- Any other trace: send `baseline-logcat.txt`; stop.

## 4. Bluetooth on, HCI up

Terminal 1, leave a marker in the host kernel log, then follow the container log:

```bash
A shell "echo '--- BT test start ---' > /dev/kmsg"
C 'logcat -v time' | grep --line-buffered -E 'AMZ-\[BT\]|btnvshim|BluetoothAdapterState|AdapterService|FATAL|Fatal signal|DEBUG'
```

Terminal 2 after the toggle: `A shell dmesg | sed -n '/--- BT test start ---/,$p' | grep -E 'MTK-BT|WMT'`.

On the Walkman screen: Settings -> Bluetooth -> On. (`svc bluetooth` is not in this image.
`C 'service call bluetooth_manager 6'` should be `enable()` with the 5.1 AIDL numbering,
**unverified on this image**.)

### Expected, in order (messages exact **(code)**; whether it happens **(unknown)**)

| # | Where | Line | Means |
|---|---|---|---|
| 1 | logcat | `mtk_bt_init #46` | bluedroid loaded `libbt-vendor.so` and called `mtk_bt_init` |
| 2 | logcat | `mtk_bt_op: BT_VND_OP_POWER_CTRL 1` | log-only in this library; the open below is the power-on |
| 3 | logcat | `mtk_bt_op: BT_VND_OP_USERIAL_OPEN` then `init_uart #107` | opening `/dev/stpbt` (flags `O_RDWR\|O_NOCTTY\|O_NONBLOCK`) |
| 4 | dmesg | `[MTK-BT] BT_open: BT_open: major 192 minor 0 (pid N)`, `BT_open: WMT register BT rst cb!`, `BT_open: Now it's in MTK Bluetooth Mode`, `BT_open: WMT turn on BT OK!`, `BT_open: STP is ready!` | the kernel powered the BT function on; the firmware patches were read by the kernel from the host's `/system/etc/firmware` via `6620_launcherA` |
| 5 | dmesg | `nvram_read: failed to open!!` (harmless), then `BT_open: Read NVRAM : BD address 000000000000 Cap 0x00 Codec 0x00`, `BT_open: finish` | the driver tries to log `/data/BT_Addr` from the container root as uid bluetooth; the value is never used. (Driver quirk: on that failure it forgets to restore `KERNEL_DS` for the calling thread; reads and writes still work.) |
| 6 | logcat | `mtk_bt_op: BT_VND_OP_FW_CFG`, `mtk_fw_cfg #250` | firmware config starts |
| 7 | logcat | `bt_get_combo_id: Get combo chip id retry 0`, `bt_get_combo_id: Combo chip id 8127` | the property override is read on the first try |
| 8 | logcat | `bt_read_nvram #192`, then **about 5 s of silence**, then `bt_read_nvram: Get NVRAM ready retry 10`, `bt_read_nvram: Get NVRAM restore ready fails!`, `mtk_fw_cfg: Read NVRAM data fails or NVRAM data all zero!!`, `mtk_fw_cfg: Use 8127 default value` | intended: `service.nvram_init` is unset, so the lib waits 10 x 0.5 s and uses its built-in record. `NVM_GetFileDesc` is **not** called on the read path in this case. |
| 9 | logcat | `idme_get_bt_mac_addr: Open /proc/idme/bt_mac_addr fails` | Amazon-only override, absent here |
| 10 | logcat | `mtk_fw_cfg: [BDAddr 00-00-46-81-27-01][Voice 60 00][Codec 23 10 00 00][Radio 07 80 00 06 05 07][Sleep 03 40 1f 40 1f 00 04][BtFTR 80 00][TxPWOffset ff ff ff][CoexAdjust 00 00 00 00 00 00]` | the built-in record, with the placeholder address (that value triggers the eFUSE query) |
| 11 | logcat | `BT_InitDevice: BT_InitDevice`, `GORM_FW_Init_Thread: FW init thread starts` | init script thread |
| 12 | logcat | `GORMcmd_HCC_Get_Local_BD_Addr: ...`, `GORMevt_HCE_Get_Local_BD_Addr: Retrieve eFUSE address: aa-bb-cc-dd-ee-ff`, `GORM_FW_Init_Thread: The event of command 0 success` | the controller answered an HCI command: **HCI is up**. Write the eFUSE address down. |
| 13 | logcat | `GORMcmd_HCC_Set_Local_BD_Addr: eFUSE address has valid value` (or `eFUSE address default value` if the chip's eFUSE is blank or the placeholder), then possibly `WriteBDAddrToNvram: Open BT NVRAM fails errno N` (real libnvram, no `/data/nvram`; **unknown**), then `GORMcmd_HCC_Set_Local_BD_Addr: Write BD address: aa-bb-...` and `The event of command 1 success` | the address the stack will use |
| 14 | logcat | `GORMcmd_HCC_Set_Radio`, `GORMcmd_HCC_Set_TX_Power_Offset`, `GORMcmd_HCC_Set_Sleep_Timeout`, each followed by `The event of command 2 / 3 / 4 success` | RF and sleep config |
| 15 | logcat | `GORMcmd_HCC_RESET` then `The event of command 5 success` | **hci reset** acknowledged |
| 16 | logcat | `GORMcmd_HCC_Set_FW_SysLog disable`, `The event of command 6 success`; then `mtk_bt_op: BT_VND_OP_SCO_CFG`, `BT_InitSCO` and possibly `GORM_SCO_Init_Thread: SCO init thread starts` | init script finished (the SCO script is empty in this library: **static reading**) |
| 17 | Settings, shell | the switch stays On; `C 'dumpsys bluetooth_manager' \| grep -iE 'enabled\|state\|address\|name'` shows `enabled: true`, `state: ON`, an address | stack up |

Timing budget: from `USER_TURN_ON`, `BluetoothAdapterState` allows 5 s for the profile services
(`START_TIMEOUT_DELAY`) and 12 s (`ENABLE_TIMEOUT_DELAY`) for `enableNative`; both values were read
from `Bluetooth.apk`. The 5 s NVRAM wait sits inside that 12 s together with the kernel's power-on and
patch download (**duration unknown**). If logcat shows `CURRENT_STATE=PENDING, MESSAGE = ENABLE_TIMEOUT`
from `BluetoothAdapterState`, cut the wait: `C 'setprop service.nvram_init Ready'` before the toggle
(this makes the library call the real libnvram on the read path as well; it should fail with
`Open BT NVRAM fails errno N` and fall back to the same defaults, **unverified**). With the step 8
shim this is the intended setting.

### What a failure line means

| Output | Meaning and next step |
|---|---|
| `init_uart: Can't open serial port` and dmesg `BT_open: WMT turn on BT fail!` | The kernel could not power the BT function on. `A shell dmesg \| grep -E 'WMT\|patch\|stp\|launcher'`. Likely the launcher RPC (`srh_patch`) or the patch read. Send the dmesg. |
| `Can't open serial port` and no `BT_open` line at all | the node: permission (`ls -l /dev/stpbt` in the container), missing, or held; run `tools/android-boot.sh btdev`. |
| `bt_get_combo_id: Invalid combo chip id!` / `mtk_fw_cfg: Get combo chip id fails` | property missing or `-1`: step 3 should have shown `0x8127`. |
| `mtk_fw_cfg: Unknown combo chip id` or `GORM_FW_Init_Thread: No matching init script` | the property is not `0x8127` in that process. |
| `GORM_FW_Init_Thread: Send command N fails`, `No HCI packet transmit callback`, `No libbt-hci callbacks!` | the write to `/dev/stpbt` failed or the stack's callbacks are not set. `ENOSPC` or STP errors in dmesg = STP window full. |
| `GORM_FW_Init_Thread: The event of command N error` | the firmware refused vendor command N (0 get address, 1 set address, 2 radio, 3 TX power, 4 sleep, 5 reset, 6 syslog). **This is the main risk**: CM's 0x8127 script may not match what this chip's firmware accepts. Send logcat and dmesg; it names the command. |
| `Write BD address: 00-00-00-00-00-00` | the chip's eFUSE address is blank. Run step 8 with Sony's address. |
| `Write BD address: 00-00-46-81-27-01` | fell back to the placeholder; every unit would share it. Run step 8. |
| last `AMZ-[BT]` line is `eFUSE address ... value`, then `Fatal signal 11` / tombstone in `com.android.bluetooth` | the real libnvram faulted in `WriteBDAddrToNvram`. Go to step 8 (the shim never touches `/data/nvram`). |
| 5 s stall, then the switch flips Off, no `command N` lines | `FW_CFG` failed before the script: look for the chip-id lines above. |
| `FATAL EXCEPTION` in `com.android.bluetooth` on the toggle | stack trace to the devlog; the vendor path may not have been reached. |

## 5. Scan

Keep a phone with Bluetooth visible nearby (and, for LE, a BLE advertiser app). Settings ->
Bluetooth -> Search for devices.

- Expect the phone within about 12 s.
- Log: `C 'logcat -d -v time' | grep -iE 'deviceFound|device_found|discovery|BluetoothRemoteDevices'`.
- Nothing found but step 4 reached `command 5 success`: the controller is alive, so check that the
  phone is discoverable and try the LE advertiser.

## 6. Pair

Tap the phone in the list and accept the 6-digit code on both. Evidence:

```bash
C 'dumpsys bluetooth_manager' | grep -iE 'bond|paired'
C 'ls -l /data/misc/bluedroid'
```

`bt_config.xml` appears in `/data/misc/bluedroid` and Settings lists the phone under paired devices.

A2DP-only check, with a Bluetooth headphone paired:

```bash
C 'dumpsys activity services com.android.bluetooth' | grep -oE 'com.android.bluetooth/[.a-zA-Z0-9]+Service' | sort -u
```

Expected: `.btservice.AdapterService`, `.a2dp.A2dpService`, `.gatt.GattService` and no
`HeadsetService`, `BluetoothOppService`, `PanService`, `HidService`, `BluetoothMapService`,
`BluetoothPbapService` or `HealthService`. In Settings → the headphone's gear icon, only "Media
audio" is offered (no "Phone audio"). A `HeadsetService` in the list means the patched APK was not
in place (see steps 2 and 3).

## 7. Save the evidence

```bash
TS=$(date +%Y%m%dT%H%M%S); D=work/android/bt-$TS; mkdir -p $D
A shell dmesg > $D/dmesg.txt
C 'logcat -d -v time' > $D/logcat.txt
C 'dumpsys bluetooth_manager' > $D/dumpsys-bluetooth_manager.txt
tools/android-boot.sh pull        # init.out, logcat, dumpsys -l -> work/android/boot-out/
```

## 8. Optional second run: choose the BD address (shim and seed)

Only if step 4 showed a blank or placeholder address, a libnvram fault, or an `ENABLE_TIMEOUT`, or
you want Sony's address and Sony's Walkman-tuned radio bytes instead of the library's built-in table.

```bash
python3 -I probe/android/connectivity/bt/gen-bt-nvram.py \
    --addr-from-file work/android/bt/sony_BT_Addr -o work/android/bt/bt_nvram.bin
probe/android/connectivity/bt/nvram-shim/build.sh     # -> nvram-shim/out/libnvram-btshim.so
tools/android-boot.sh down
NW_BT_SHIM=$PWD/probe/android/connectivity/bt/nvram-shim/out/libnvram-btshim.so \
NW_BT_SEED=$PWD/work/android/bt/bt_nvram.bin \
NW_DROP_SYS_BOOT=1 NW_SYS_IMG=work/android/aosp-ford/system.img NW_CONTAINER_VARIANT=aosp \
  tools/android-boot.sh up
C 'setprop service.nvram_init Ready'      # required, see below
```

(`down` needs a reboot before the next `up` for the Mali swap, as for any container restart.
Use `--addr-from-file` only if step 1 found a real address; for "let the chip's eFUSE decide" use
`--addr zero`.)

Expected extra `up` output: `libnvram shim: .../system/lib/libnvram.so <- /tmp/libnvram-btshim.so (ro)`
and `bt seed: .../data/nw/bt_nvram.bin (66 bytes)`; the tools script prints
`Bluetooth: libnvram shim and BD-address seed pushed`.

`service.nvram_init` **must** be `Ready` for the seed to be read at all: `libbluetooth_mtk` skips the
`NVM_GetFileDesc` read path entirely when the property is not `Ready` after its 5 s wait. Without the
`setprop`, the shim is only used for the address write-back (`/data/nw/bt_nvram.written`).

After enabling BT, expect in logcat `btnvshim: lid 1 read /data/nw/bt_nvram.bin -> fd N (errno 0)`
and `mtk_fw_cfg: [BDAddr aa-bb-...]` with your address, and no `Get NVRAM ready retry 10` stall.
If the address is non-default the eFUSE query is skipped: the first command is 1, there is no
`Retrieve eFUSE address` line, and you see `NVRAM BD address has valid value`. If it is zero, the
eFUSE is queried and the result is written through the shim:

```bash
C 'ls -l /data/nw; od -A x -t x1 /data/nw/bt_nvram.written'
```

`btnvshim: lid 1 read ... -> fd -1 (errno 13)` = permissions on `/data/nw` (the patched
`android-boot.sh` makes it `777`); `errno 2` = no seed pushed (both `NW_BT_SHIM` and `NW_BT_SEED`
must be set).

## 9. Teardown

```bash
tools/android-boot.sh down
```

The Mali swap still needs a reboot, and `mtkbt` stays stopped until then, so the Sony Bluetooth
stack is back only after the reboot (the same rule as for the rest of the Sony stack).
