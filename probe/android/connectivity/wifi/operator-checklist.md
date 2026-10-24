# Wi-Fi in the CM container: operator checklist (DRAFT, nothing here has been run)

Validation goal: **Settings -> Wi-Fi on -> scan -> connect -> DHCP -> ping**, inside the
CM 12.1 container. Why each piece is the way it is, with evidence:
`devlog/2026-10-15-wifi-container-groundwork.md`.

Each expected-output block says where it comes from:

- **seen** = taken from the 2026-10-11 host-side run (`work/wifitest/run-1011-1223/`),
- **expected** = derived from source or disassembly, never seen on this device.

All `adb` is operator-run. `A` below means `adb -s <serial>`. `tools/android-boot.sh sh "CMD"`
runs CMD inside the container (as root, `PATH=/system/bin:/system/xbin`). Everything is RAM-only
or inside the container's own `/data` image; a reboot restores the Sony stack and removes all
modules. The device is shared with other sessions: check nobody is using it first.

## 0. Before the operator starts (PC side, orchestrator)

1. The three patches in this directory (`android-boot.sh.patch`, `init.aosp.rc.patch`,
   `tools-android-boot.sh.patch`) are already merged into the scripts, together with the BT ones
   (`../bt/`). The `.patch` files are the record of the change; do not apply them again.
2. The bundle is where the tools patch expects it, and its own sums hold:
   `cd work/wifitest/bundle/WIFITEST && sha256sum --quiet -c SHA256SUMS; echo EXIT=$?` -> `EXIT=0`.
   The chardev is not the bundle's: build the station-only one (it replaces the bundle's, which
   registers P2P and crashed cfg80211 on 2026-10-15):
   `make -C probe/kmod/mtk-wmt-wifi-soc verify; echo EXIT=$?` -> `unresolved: 0`, `EXIT=0`.
3. The container is the CM one, and `init.aosp.rc` is baked into `container.tgz` by the build
   script, so the variant and the image must be named on every `up`:
   `NW_WIFI=1 NW_CONTAINER_VARIANT=aosp NW_SYS_IMG=$PWD/work/android/aosp-ford/system.img tools/android-boot.sh up`
   (Wi-Fi is opt-in since the 2026-10-15 boot reset: without `NW_WIFI=1` no module is pushed)

## 1. Control run on the host (optional, skip if the 2026-10-11 result is trusted)

Only to separate "the device no longer powers Wi-Fi" from "the integration is wrong". Do it on a
normal boot, before any container `up`: the script's stage 0 needs the WMT core (`mtk_stp_wmt_soc`)
loaded and refuses without it.

```
$A push work/wifitest/bundle/WIFITEST /tmp/WIFITEST
$A push work/wifitest/run-1011-1223/wifi_test.patched.sh /tmp/WIFITEST/wifi_test.sh
$A shell "sh /tmp/WIFITEST/wifi_test.sh"
```

Expected (**seen**): `wlan0 appeared after 0s`, `BSS found: 10..12`, `VERDICT: RX PATH CONFIRMED`.
Then `$A shell "sh /tmp/WIFITEST/wifi_unload.sh"` for a clean slate (the power-off write is **not**
exercised before: see step 9).
`exit 2` with `turn on WIFI fail` / `exit 1` with `Unknown symbol`: the table in the bundle README applies.

## 2. Bring the container up

```
NW_WIFI=1 NW_CONTAINER_VARIANT=aosp NW_SYS_IMG=$PWD/work/android/aosp-ford/system.img tools/android-boot.sh up
```

Expected in the output (**expected**, text from the patch):

```
== wifi kernel modules (host side)
loaded: mtk_wmt_wifi_soc
loaded: compat
loaded: cfg80211
loaded: wlan_mt
wmtWifi dev 153:0
WMT launcher state (kept by the Sony side):
  ... (ps lines for 6620_launcher*, wmt_loader, mtkbt; or the "none of ..." line)
...
permissions overlay: wifi.direct hidden (NN files)
```

and, in `$A shell dmesg` around the insmods (**seen**):

```
cfg80211: Calling CRDA to update world regulatory domain
HifAhbPltmProbe
mtk_wcn_wmt_wlan_reg
initWlan...Co-exist
```

| output | meaning |
|---|---|
| `Wi-Fi: modules not loaded, continuing without Wi-Fi`, preceded by `no /tmp/wifi/*.ko pushed` | the tools patch did not run (old `tools/android-boot.sh`) |
| `mtk_stp_wmt_soc (Sony WMT core) is not loaded` | the Sony side did not bring up WMT; nothing to do with this change |
| `insmod ... failed` and dmesg `Unknown symbol` | the bundle build does not match this kernel; stop and report |
| `already loaded: ...` for all four | normal after a previous run in the same boot |
| `WMT launcher state ... none of ...` | `6620_launcherA` is gone (the Sony stop script killed it). Not known to matter for Wi-Fi; if step 4 fails with a WMT error, this is suspect number one |

Wait for the framework: `tools/android-boot.sh sh "getprop sys.boot_completed"` -> `1`
(the first boot on a fresh /data image runs dex2oat for a long time).

## 3. Node and namespace

```
tools/android-boot.sh wifi
```

Expected (**expected**):

```
== wifi modules
mtk_stp_wmt_soc ... Live ...
mtk_wmt_wifi_soc ... Live ...
compat ... Live ...
cfg80211 ... Live ...
wlan_mt ... Live ...
== wifi char device
153 mtk_wmt_WIFI_chrdev_soc
class dev: 153:0
== host view
crw-rw---- 1 system 100 153, 0 ... /dev/wmtWifi        <- seen on 2026-10-11 (Sony ueventd made it)
no wlan0 (radio is off, or modules are not loaded)
== container view (init pid NNN)
crw-rw---- 1 system system 153, 0 ... /proc/NNN/root/dev/wmtWifi
net ns host: net:[4026531956]
net ns init: net:[4026531956]
```

(The namespace numbers differ; what matters is that the two lines are **identical**.)

| output | meaning / next |
|---|---|
| container view: `No such file or directory` | ueventd's coldboot missed the node. Check the order in step 2 (modules must be loaded before `start Android init`). Fallback, and report that the coldboot theory failed: `tools/android-boot.sh sh "/system/xbin/busybox mknod /dev/wmtWifi c 153 0; /system/xbin/busybox chown 1000:1000 /dev/wmtWifi; /system/xbin/busybox chmod 660 /dev/wmtWifi"` |
| container view starts with `-rw` | a regular file: init's `write` created it because the node was missing at an enable attempt. Delete it inside the container and use the fallback above |
| the two `net ns` lines differ | the container does have its own network namespace after all. Stop; the integration needs the `wlan0` move (`ip link set wlan0 netns`) |
| `153` missing from `/proc/devices` | the chardev module is not loaded |

## 4. Radio power-on by hand (bisect; skip when step 5 works first time)

Does the chip come up from inside the container at all, independent of the framework?
The node must be a character device (step 3) or this writes a regular file.

```
tools/android-boot.sh sh "[ -c /dev/wmtWifi ] && echo 1 > /dev/wmtWifi && echo written"
$A shell "ls /sys/class/net"
$A shell "dmesg | /system/xbin/busybox grep -E 'HifAhbProbe|wiphy_register|func_on|MTK-WIFI'"
```

Expected (**seen** 2026-10-11, host-side; same kernel path):

```
written
...wlan0...
[MT6582]: HifAhbProbe()
Reday to call CFG80211 wiphy_register ...
[WMT-STP-EXP][I]mtk_wcn_wmt_func_on: mtk_wcn_wmt_func_on_f type(3)
[MTK-WIFI] WIFI_write: WMT turn on WIFI success!
```

about 1.2 s between `HifAhbProbe` and the last line (firmware download). A second write prints
`WIFI is already power on!`.

| output | meaning |
|---|---|
| `written` but no `wlan0`, dmesg `WMT turn on WIFI fail!` | WMT refused. With a container running this points at the missing `6620_launcherA` or a stopped Sony service; compare with step 1 on a normal boot |
| dmesg `request irq ... fail` | GDMA2 / IRQ 90 is held by someone else |
| dmesg `FW image` / `Open FW image ... failed` | `/etc/firmware/WIFI_RAM_CODE_8590` is read from the **host** filesystem by the kernel thread; check it on the host, not in the container |
| the command hangs | WMT is stuck; `$A shell dmesg`, then reboot |

To return to the initial state before step 5: `tools/android-boot.sh sh "echo 0 > /dev/wmtWifi"`.
**This power-off path has never been run on this unit** (the 2026-10-11 session ended with a reboot).
Expected (**expected**): `[MTK-WIFI] WIFI_write: WMT turn off WIFI OK!` and `wlan0` disappears.
If it oopses, that is a finding about `wlan_mt`'s remove path, not about the integration.

## 5. Settings -> Wi-Fi on

```
tools/android-boot.sh sh "am start -a android.settings.WIFI_SETTINGS"
```

Toggle Wi-Fi **on** on the touch screen. (Equivalent without the UI: `tools/android-boot.sh sh "svc wifi enable"`.)

Within a few seconds (**expected**):

```
tools/android-boot.sh sh "getprop wlan.driver.status"          -> ok
tools/android-boot.sh sh "getprop init.svc.wpa_supplicant"     -> running
tools/android-boot.sh sh "dumpsys wifi | /system/xbin/busybox head -3"   -> Wi-Fi is enabled
$A shell "ls /sys/class/net"                                   -> contains wlan0
tools/android-boot.sh sh "ls -l /data/misc/wifi"               -> wpa_supplicant.conf, entropy.bin, sockets/
tools/android-boot.sh log                                      -> ... init: starting 'wpa_supplicant'
```

and in dmesg the same Wi-Fi lines as step 4 (init did the write for you).

| output | meaning |
|---|---|
| `wlan.driver.status` stays empty | the framework never asked: `logcat -d -v time -s WifiStateMachine WifiService WifiHW` |
| `ok`, but no `wlan0`, dmesg has no `MTK-WIFI` line | init's `write` did not fire: the rc patch is not in `container.tgz` (re-run `up` with `NW_CONTAINER_VARIANT=aosp`), or `/dev/wmtWifi` became a regular file (step 3) |
| `wlan0` appears, `init.svc.wpa_supplicant` is `stopped`, logcat `WifiHW: Supplicant not running, cannot connect` | the supplicant died at start. Run it by hand to see why: `tools/android-boot.sh sh "wpa_supplicant -iwlan0 -Dnl80211 -c/data/misc/wifi/wpa_supplicant.conf -e/data/misc/wifi/entropy.bin -O/data/misc/wifi/sockets -dd"` (no `-f`: neither supplicant build has it) |
| logcat `WifiHW: Wi-Fi will not be enabled` / `entropy file was not created` | `/data/misc/wifi` ownership is wrong (`wifi:wifi 0770` expected) |
| `init.svc.p2p_supplicant` instead of `wpa_supplicant` | the permissions overlay is missing (step 2 line) — the single-interface fallback service should still work |
| Settings shows an error toggle | `dumpsys wifi` says `Wi-Fi is failed` or `disabled`; collect logcat |

## 6. Scan

The Settings list should fill within about 5 s. Supplicant-level cross-check:

```
tools/android-boot.sh sh "wpa_cli -p /data/misc/wifi/sockets -i wlan0 scan_results"
```

Expected (**seen** on the host, same driver): a table `bssid / frequency / signal level / flags / ssid` with
10-12 BSS indoors, 2.4 GHz channels 1-11 (`2412`..`2462`) and perhaps one 5 GHz entry (`5240`).
dmesg shows `[wlan] aisFsmSteps: (AIS STATE) [13] TRANSITION: [0] -> [2]` followed by
`scnFsmSteps ... [0] -> [1]` and `[1] -> [0]` per scan (**seen**).
The regulatory domain is the world default, so most 5 GHz channels are receive-only (`no IR`).

| output | meaning |
|---|---|
| empty `scan_results`, Settings list empty | `wpa_cli status` says `wpa_state=INTERFACE_DISABLED`? then `ip link set wlan0 up` was not issued: logcat `netd` / `ConnectivityService`. If the interface is up and the scan is empty, move next to an AP (RF path) |
| `FAIL` / `Failed to connect to non-global ctrl_ifname` | wrong ctrl dir: the socket is `/data/misc/wifi/sockets/wlan0` |

## 7. Connect and DHCP

WPA2-PSK (CCMP) is what was proven on this driver; use a WPA2-PSK (or open) network, not WPA3-only.
Pick an SSID in the Settings list, enter the passphrase.

Expected (**expected** for the Android side, **seen** for the kernel lines), within about 10 s:

```
dmesg:  [wlan] saaFsmSteps: (SAA STATE) [20] TRANSITION: [0] -> [1] ... [6] -> [0]
        [wlan] aisFsmRunEventJoinComplete: (AIS STATE) AISOK
        IPv6: ADDRCONF(NETDEV_CHANGE): wlan0: link becomes ready
tools/android-boot.sh sh "wpa_cli -p /data/misc/wifi/sockets -i wlan0 status"
        wpa_state=COMPLETED, ssid=<yours>, pairwise_cipher=CCMP, key_mgmt=WPA2-PSK
tools/android-boot.sh sh "getprop dhcp.wlan0.result"           -> ok
tools/android-boot.sh sh "getprop dhcp.wlan0.ipaddress"        -> 192.168.x.y
tools/android-boot.sh sh "getprop dhcp.wlan0.gateway"          -> 192.168.x.1
tools/android-boot.sh sh "getprop dhcp.wlan0.dns1"             -> a resolver address
tools/android-boot.sh sh "ip addr show wlan0"                  -> inet 192.168.x.y/24
tools/android-boot.sh sh "dumpsys wifi | /system/xbin/busybox grep mNetworkInfo"   -> ... state: CONNECTED/CONNECTED ...
```

(The container's toolbox may lack `head` and `grep`; its `/system/xbin/busybox` has both.)

| output | meaning |
|---|---|
| `wpa_state=SCANNING` / `ASSOCIATING` forever, dmesg `saaFsmSteps` stops early | auth/assoc failure: wrong PSK shows as `4WAY_HANDSHAKE` then `DISCONNECTED` |
| `COMPLETED` but `dhcp.wlan0.result` empty or `failed` | dhcpcd: `getprop init.svc.dhcpcd_wlan0`, `logcat -d -v time -s dhcpcd`. `stopped` right after start = the `dhcp` user cannot write `/data/misc/dhcp` (`dhcp:wifi 0770` expected) or `net_raw` group missing |
| `dhcp.wlan0.result=ok` but Settings says "Obtaining IP" forever | the framework could not apply the lease: `logcat -d -v time -s ConnectivityService netd NetworkManagement`; `ndc` errors point at kernel netfilter/route options (the stock config has `IP_MULTIPLE_TABLES`, `XT_MARK`, `CONNMARK`; it lacks `XT_MATCH_OWNER` and `XT_MATCH_CONNTRACK`) |
| `Failed to open wlan fw path param` (tag `WifiHW`) | only from netd's soft-AP reload; it is not part of this path |

## 8. Ping and DNS

```
tools/android-boot.sh sh "ping -c 3 <gateway from step 7>"
tools/android-boot.sh sh "ping -c 3 1.1.1.1"
tools/android-boot.sh sh "ping -c 3 example.com"
```

Expected (**seen** for the first two on 2026-10-11 with the busybox stack): `3 packets transmitted, 3 received, 0% packet loss`,
round trip around 3-18 ms. The third (**expected**) resolves the name through netd's resolver;
there is no `resolv.conf` to configure on Android 5.1.

| output | meaning |
|---|---|
| gateway ping fails, `wpa_state=COMPLETED` | routing: `ip route`, `ip rule` (fwmark rules from netd) |
| IP ping works, name fails | `getprop dhcp.wlan0.dns1` empty -> the DHCP server gave no DNS; present -> netd resolver not fed: `logcat -d -v time -s netd` |
| `ping: sendto: Operation not permitted` | `ANDROID_PARANOID_NETWORK=y` in the kernel: the caller needs group `inet`/`net_raw`; root has it |

## 9. Wi-Fi off and teardown

Toggle Wi-Fi **off** in Settings. Expected (**expected**): `init.svc.wpa_supplicant` -> `stopped`,
`wlan.driver.status` -> `unloaded`, dmesg `WMT turn off WIFI OK!`, `wlan0` gone from `/sys/class/net`.
This is the first ever exercise of `func_off(WIFI)` on this unit: save `dmesg` and `logcat` beforehand
(`tools/android-boot.sh pull`).

Then:

```
tools/android-boot.sh down
tools/android-boot.sh wifi-unload
```

Expected (**expected**): `rmmod wlan_mt ok`, `rmmod cfg80211 ok`, `rmmod compat ok`, `rmmod mtk_wmt_wifi_soc ok`.
A `FAILED` line means something still holds the module (a leftover wpa_supplicant, or wlan0 still up);
a reboot clears everything.

## What to send back

`tools/android-boot.sh pull` (init.out, logcat, dumpsys list), `$A shell dmesg`, and the output of
`tools/android-boot.sh wifi` and of the first command that did not match its expected output.
