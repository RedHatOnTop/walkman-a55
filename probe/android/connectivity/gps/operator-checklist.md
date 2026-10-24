# GPS function-on test: operator checklist (DRAFT, nothing here has been run)

Goal: show that the stock WMT core accepts the GPS function when a ported
`stp_chrdev_gps.c` opens `/dev/stpgps`. This is the same kind of proof as the
2026-10-11 Wi-Fi run, and it is a function-on test only: no antenna, no `mnld`,
no NMEA, no fix. Context and evidence: `devlog/2026-10-15-wifi-container-groundwork.md`.

Rules: all `adb` is operator-run (serial `<serial>`). Everything is RAM-only;
a reboot undoes it. Run it on the host as the Sony stack boots it (no container),
which is the state the Wi-Fi run was done in. `mtkbt` / `6620_launcherA` should be
left alone.

## 0. Build (any time, on the PC)

```
cd probe/kmod/mtk-stp-gps-soc && make verify
```

Expected: `EXIT=0` for the make, `vermagic: 3.10.26 SMP preempt mod_unload ARMv7`,
`unresolved: 0`. Do not push a module that fails this.

## 1. Push

```
adb -s <serial> shell "/system/xbin/busybox mkdir -p /tmp/gps"
adb -s <serial> push probe/kmod/mtk-stp-gps-soc/mtk_stp_gps_soc.ko /tmp/gps/
adb -s <serial> push probe/android/connectivity/gps/gps-open-test.sh /tmp/gps/
```

## 2. Preconditions

```
adb -s <serial> shell "/system/xbin/busybox grep -E '^(mtk_stp_wmt_soc|mtk_stp_bt_soc|mtk_wmt_detect|mtk_stp_gps_soc) ' /proc/modules"
adb -s <serial> shell "/system/xbin/busybox awk '\$1 >= 150' /proc/devices"
```

Expected: the first three modules listed, `mtk_stp_gps_soc` absent. In
`/proc/devices` no line for major `191` (the driver registers a fixed major 191;
the Wi-Fi chardev uses 153, BT 192, WMT 200 in the source).

If 191 is taken: `insmod` will fail with `Device or resource busy` and dmesg shows
`fail to register chrdev`. Retry with `insmod /tmp/gps/mtk_stp_gps_soc.ko GPS_major=<free>`
(the module has a `GPS_major` parameter), and mknod `/dev/stpgps` with that major.

## 3. Run

```
adb -s <serial> shell "sh /tmp/gps/gps-open-test.sh 5"
```

The script loads the module, finds the major in `/proc/devices`, makes sure
`/dev/stpgps` exists, opens it from a subshell for 5 s, closes it, and reads the
kernel log after its own marker line.

### Expected on success (exit 0)

```
== [1] insmod
insmod rc=0
== [2] character device
char major: 191
class dev: 191:0
crw-rw----  ... 191,   0 ... /dev/stpgps
== [3] open for 5 s (WMT turns the GPS function on)
open+hold+close rc=0
>>> VERDICT: GPS FUNCTION ON/OFF OK ...
```

and in the kernel log part, in this order (from the source: `GPS_open` prints the
first line without a prefix, the rest carry `[GPS] <function>: `; the WMT line has
the same shape as the `type(3)` Wi-Fi line of the 2026-10-11 run, GPS is type 2):

```
mtk_stp_GPS_chrdev_soc driver(major 191) installed.
GPS_open: major 191 minor 0 (pid NNNN)
[WMT-STP-EXP][I]mtk_wcn_wmt_func_on: mtk_wcn_wmt_func_on_f type(2)
[GPS] GPS_open: WMT turn on GPS OK!
GPS_close: major 191 minor 0 (pid NNNN)
[GPS] GPS_close: WMT turn off GPS OK!
```

Extra WMT-FUNC lines about `GPS_SYNC`, `GPS_LNA` or `LDO VCN28 has been turn on by GPS`
may appear between them (they are strings in the shipped `mtk_stp_wmt_soc.ko`);
they are fine.

### Exit codes and what a failure means

| exit | verdict / dmesg | meaning | next |
|---|---|---|---|
| 1 | `Unknown symbol` | a stock export is missing; the PC-side `make verify` should have caught it | send the log, do not retry |
| 1 | `Device or resource busy`, `fail to register chrdev` | major 191 is taken | `GPS_major=` as above |
| 2 | `[GPS] GPS_open: WMT turn on GPS fail!` | WMT / CONSYS refused the function: GPS hardware, efuse or firmware is not available on this unit | dead end for GPS on this unit; send the log |
| 4 | `STP is not ready, Cannot open GPS Devices` | the function switched on but STP is down. Upstream `GPS_open` then returns without switching the function off again, so GPS stays on in WMT | reboot before the next try, and do not `rmmod` first: the reset callback registered by `GPS_open` still points into the module |
| 6 | `whole chip resetting...` | CONSYS reset in progress | wait a minute, retry |
| 3 | script still waiting | the WMT function switch hung | `adb shell dmesg`, then reboot |
| 8 | no `GPS_open` line | the node does not reach this module (wrong major, or `/dev/stpgps` came from something else) | `ls -l /dev/stpgps` and the major in `/proc/devices` |

## 4. Teardown

```
adb -s <serial> shell "rmmod mtk_stp_gps_soc"
```

Expected: no output. `rmmod` succeeds only when no fd is open. Or reboot.

## 5. What a pass unlocks, and what it does not

A pass means the stock WMT core switches the GPS function on and off through the
dispatcher with `CHIP_TYPE_SOC`. It says nothing about reception. The Walkman has
no known GPS antenna and the CM image has no GPS userspace (`mnld`, `gps.*.so`);
the Fire 7 has no GPS either. Before any `mnld` work:

- Amazon's static init (the container's `/init`) has no `gps` user or group in its
  name table (`strings init | grep -i gps` finds nothing). A `service mnld` with
  `user gps` would not parse; use the numeric id (`user 1021`, `group 1021 system`).
  `ueventd.mt8127.rc` already lists `/dev/stpgps 0660 gps gps`; the unknown names
  fall through to root-owned. Both are UNVERIFIED until a container run.
- The container's `/dev/stpgps` would come from ueventd coldboot exactly like
  `/dev/wmtWifi`, so the module has to be loaded on the host before the container
  starts.
