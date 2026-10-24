# Wi-Fi in the CM container, and a GPS kmod: groundwork (draft)

2026-10-15. Worker B of the connectivity track (`docs/mt6620-connectivity-hal-brief.md`). The device
was unplugged the whole time: every claim below comes from a file, a binary, a disassembly or a build,
never from the Walkman. **Nothing here has run on the device and nothing is claimed to work.** What
exists is a patch set plus an operator checklist, and a GPS module that builds.

Deliverables (all uncommitted, no existing file edited):

- `probe/android/connectivity/wifi/` : `android-boot.sh.patch`, `init.aosp.rc.patch`,
  `tools-android-boot.sh.patch`, `operator-checklist.md`
- `probe/android/connectivity/gps/` : `operator-checklist.md`, `gps-open-test.sh`
- `probe/kmod/mtk-stp-gps-soc/` : ported source, `Kbuild`, `Makefile`, `PORT.diff`, built `.ko` (ignored by git)

## Answers

| # | question | answer | basis |
|---|---|---|---|
| 1 | does the container share the host netns? | **yes**, `wlan0` is visible inside with no plumbing | `pivotinit.c` only `unshare(CLONE_NEWNS)` |
| 2 | who powers the radio? | modules: **host script, before `start_init`**. The write of `1` to `/dev/wmtWifi`: **the container's init**, on `wlan.driver.status=ok`. CM's `libhardware_legacy` does **not** do it | disassembly, below |
| 3 | `/dev/wmtWifi` bind? | **no bind needed**: ueventd's coldboot in the container creates it, provided the modules were loaded before the container starts | class device in the driver source; `ueventd.mt8127.rc` line 7 |
| 4 | supplicant / dhcpcd / DNS fit | CM's binaries fit as they are; init services copied from CM's own `init.mt8127.rc`; Wi-Fi Direct must be hidden (no `p2p0`); DNS goes through netd, no `resolv.conf` | CM ramdisk, binaries |
| 5 | MAC / NVRAM stub? | **no stub**. The driver reads `/data/nvram/APCFG/APRDEB/WIFI` itself from a kernel thread, i.e. on the *host* filesystem; nothing in CM's Wi-Fi chain links `libnvram`. MAC stays `00:08:22:40:53:3f` | driver source, `readelf` |

## 1. Network namespace

```
$ grep -n "CLONE_NEW\|unshare(\|SYS_setns," probe/devtools/pivotinit.c
85:    if (syscall(SYS_setns, fd, CLONE_NEWNS) != 0) {
114:    if (unshare(CLONE_NEWNS) != 0) {
$ grep -n "setsid /tmp/pivotinit" probe/android/android-boot.sh
248:    $BB setsid /tmp/pivotinit $ROOT /init
```

The container is a mount namespace plus a `MS_MOVE`/`chroot` of `$ROOT`; no `CLONE_NEWNET`, no
`CLONE_NEWPID` (which is why `container_pids()` has to tell the container apart by its mount namespace).
The kernel could do more (`CONFIG_NET_NS=y`, `PID_NS=y`, `# USER_NS is not set`) but nothing asks for it.
So `wlan0`, created in `init_net` by `wlan_mt`'s `register_netdev`, is there for netd, the supplicant and
`ConnectivityService` the moment it exists. The checklist makes this falsifiable
(`net ns host:` and `net ns init:` lines of `android-boot.sh wifi` must be identical).

Consequences worth knowing: the container's netd edits the **kernel-wide** iptables/ip rules, and they
survive `down`; `wlan0` and its address survive `down` as well until the modules are removed
(`android-boot.sh wifi-unload`). The supplicant's `-g@android:wpa_wlan0` is, as far as the strings show, not an abstract socket: the supplicant
is handed the init-created socket `/dev/socket/wpa_wlan0` (it carries the string `Failed to open Android control
socket`), so it lives in the container's own `/dev` and the shared netns does not matter for it.

## 2. Who powers the radio

What the kernel needs (verified in `wmt_chrdev_wifi.c` and in the 2026-10-11 log): `wlan0` only appears
after a write to `/dev/wmtWifi`. `'1'` runs `mtk_wcn_wmt_func_on(WMTDRV_TYPE_WIFI, CHIP_TYPE_SOC)`, which
calls the probe that `wlan_mt` registered with `mtk_wcn_wmt_wlan_reg`; `'0'` is `func_off`; `'S'`/`'P'`/`'A'`
switch STA+P2P / AP mode (and power on first when needed). `wlan_mt_bp.ko` itself loads with the radio off.

What CM's userspace does (`system.img.cmstock`, extracted with `debugfs -R "dump ..."`):

```
$ arm-eabi-nm -D lib/libhardware_legacy.so | grep -c "init_module\|delete_module\|finit_module"
0
00001cc8 <wifi_load_driver>:                       # whole function
    ldr r0,[pc,#16] ; ldr r1,[pc,#20] ; push {r3,lr} ; add r1,pc ; add r0,pc
    blx property_set                               # r0 = "wlan.driver.status"  r1 = "ok"
    movs r0,#0 ; pop {r3,pc}
00001ce4 <wifi_unload_driver>:                     # usleep(200000); property_set("wlan.driver.status","unloaded")
$ strings -a bin/netd | grep fw_path
wifi_get_fw_path
wifi_change_fw_path
$ arm-eabi-nm -D lib/libwifi-service.so | grep " U wifi_"       # the JNI side of WifiNative
   is_wifi_driver_loaded wifi_close_supplicant_connection wifi_command wifi_connect_to_supplicant
   wifi_load_driver wifi_set_mode wifi_start_supplicant wifi_stop_supplicant wifi_unload_driver wifi_wait_for_event
```

- `wifi_load_driver()` is exactly `property_set("wlan.driver.status","ok")`. It loads nothing and writes
  nothing. `wifi_set_mode()` stores an int and returns.
- `/dev/wmtWifi` is a string only in `wifi_change_fw_path()` (open `O_WRONLY`, write `strlen+1` bytes of
  `"P2P"`/`"AP"`, from `wifi_get_fw_path()`). A raw search of the entire image for `wmtWifi` hits exactly two
  files, `lib/libhardware_legacy.so` and `bin/meta_tst` (factory test). The only importer of
  `wifi_change_fw_path` is `bin/netd` (soft-AP firmware reload); `libwifi-service.so` does not import it.
  The Java jars (`wifi-service.jar`, `services.jar`, `framework.jar`, ...) contain neither `wmtWifi` nor
  `wlan.driver`.
- Both ramdisks tell the same story. The Fire ramdisk's `init.mt8127.rc:558-563` has
  `on property:wlan.driver.status=ok` / `write /dev/wmtWifi "1"` and the `unloaded` / `"0"` twin, **commented
  out**. The CM ramdisk (`rom/boot.img`, unpacked into the scratchpad) has neither; it only does
  `mknod /dev/wmtWifi c 153 0` (`init.mt8127.rc:249`). So on a real ford the stock Fire kernel powers the radio
  some other way. On the Sony kernel with the WIFITEST modules nobody does, so **we must**.

**Decision.** Modules are loaded on the host by `android-boot.sh` before `start_init` (`wifi_modules`, order
`mtk_wmt_wifi_soc`, `compat_bp`, `cfg80211_bp`, `wlan_mt_bp`). The power write is the two commented-out MTK
triggers, enabled in the container's `init.aosp.rc`:

```
on property:wlan.driver.status=ok
    write /dev/wmtWifi "1"
on property:wlan.driver.status=unloaded
    write /dev/wmtWifi "0"
```

Why this side: the framework's own toggle then controls the radio (Settings off really powers the CONSYS
function down; a host-side power-on would leave it on and the `unloaded` trigger would undo it at framework
start anyway), and nothing in the container needs a new binary. `mac80211_bp.ko` is **not** loaded:
`modinfo wlan_mt_bp.ko` says `depends: cfg80211,mtk_wmt_wifi_soc,compat,mtk_stp_wmt_soc`, and the proven run
loaded four modules.

Ordering argument (reasoned from AOSP 5.1 init, not seen): `WifiNative.loadDriver()` sets the property, then
`startSupplicant()` sets `ctl.start`. init handles one property message per main-loop pass and runs queued
action commands at the top of the next pass, so the `write` (about 1.2 s of firmware download, from the
2026-10-11 timestamps) completes before `ctl.start wpa_supplicant` is read, and `wlan0` exists when the
supplicant starts.

Hazard found while reading: init's `write` opens with `O_CREAT` (AOSP 5.1 `write_file`, recalled, not checked in
Amazon's binary). With no `/dev/wmtWifi` node it would silently create a **regular file** and the radio never starts. The checklist's node check (step 3) comes before
any enable attempt for that reason. Also (recalled from AOSP 5.1, not seen): the `unloaded` trigger should fire at framework start (`WifiStateMachine`'s
`InitialState` calls `unloadDriver()`), writing `0` to a radio that is already off, which the driver answers with
`WIFI is already power off!`.

Not exercised before: the **power-off** path. The 2026-10-11 session never wrote `0` (it ended in a reboot).
Settings -> Wi-Fi off will be its first run on this unit. The checklist puts it last.

## 3. `/dev/wmtWifi`

- The driver is built with `-DWMT_CREATE_NODE_DYNAMIC=1` (the bundle's `Makefile`), so `WIFI_init_soc()`
  does `register_chrdev_region(MKDEV(153,0), ...)` (static major 153, `/proc/devices` name
  `mtk_wmt_WIFI_chrdev_soc`), then `class_create("wmtWifi")` and `device_create(..., "wmtWifi")`
  (`wmt_chrdev_wifi.c:585-605`). That makes `/sys/class/wmtWifi/wmtWifi/dev` = `153:0` and a uevent.
- The 2026-10-11 log shows the host's ueventd reacting to exactly this: stage 2 of the run lists
  `crw-rw---- 1 system 100 153, 0 ... /dev/wmtWifi` with **no** `$ mknod` line before it. The node existed
  right after the `insmod`s.
- The container's own ueventd does the same at its coldboot. `devclean.sh` exists because that coldboot
  creates nodes for every device in sysfs; the container's `ueventd.icx.rc` is `ueventd.mt8127.rc` plus
  `ueventd.extra.rc` (`tools/android-container.sh`), and `ueventd.mt8127.rc:7` is
  `/dev/wmtWifi 0660 system system`. `init.aosp.rc` stops ueventd at `on boot`, so the module has to be
  loaded before the container's init starts. That ordering is the whole reason `wifi_modules` runs in
  `up()` ahead of `container()`/`start_init`.
- A bind from the host is therefore unnecessary, and a plain bind of the host node would be covered
  anyway because init mounts a fresh tmpfs on `/dev`. Fallback if the coldboot theory is wrong: `mknod`
  from the host through `/proc/<init pid>/root/dev/wmtWifi`, or `busybox mknod` via `sh_in` (the CM image
  has `/system/xbin/busybox`). Both are in the checklist.

## 4. Supplicant, DHCP, DNS

**wpa_supplicant** (`bin/wpa_supplicant` of the CM image): nl80211 only. It has `Linux nl80211/cfg80211`
and no wext driver string; it has `android_priv_cmd` (the Android `DRIVER ...` passthrough); `NEEDED`:
`libc libcrypto libcutils libkeystore_binder liblog libm libnl libssl libstdc++`. The usage text has **no
`-f`**, and neither does the bundle's static 2.10 build (the usage dump in
`work/wifitest/run-1011-1223/wpa_fg.log` lists `-b -B -c -C -d -D -e -g -G -h -i -I -K -L -N -o -O -p -P -q -t
-v -W`). That explains the "daemon vanishes with `-f`" quirk of `docs/wifi-test.md` fix #4: `-f` is an unknown
option, the usage text is printed and the process exits. CM's init line does not use `-f`.

**init services**, copied verbatim from the CM ramdisk (`init.mt8127.rc:607-640, 527-560`) into the
`init.aosp.rc.patch`: `wpa_supplicant` (`-iwlan0 -Dnl80211 -c/data/misc/wifi/wpa_supplicant.conf
-e/data/misc/wifi/entropy.bin -I/system/etc/wifi/wpa_supplicant_overlay.conf -O/data/misc/wifi/sockets
-g@android:wpa_wlan0`, `socket wpa_wlan0 dgram 660 wifi wifi`, `disabled`, `oneshot`), `dhcpcd_wlan0`
(`dhcpcd -ABK -dd`, `user dhcp`, `group net_admin net_raw`) and `iprenew_wlan0`. The Amazon init supports
`ctl.start name:arg` for oneshot services (it contains `service '%s' must be one-shot to use dynamic args`)
and knows the user and group names `dhcp wifi net_admin net_raw inet`. Directories and owners already exist
in `init.aosp.rc` (`/data/misc/wifi 0770 wifi wifi`, `sockets`, `dhcp 0770 dhcp wifi`). Config template:
`/system/etc/wifi/wpa_supplicant.conf` (`ctrl_interface=wlan0`, `update_config=1`, `wowlan_triggers=disconnect`, ...)
is copied to `/data/misc/wifi/` by `wifi_start_supplicant()` (it contains `ensure_config_file_exists`, `entropy.bin`).

**Wi-Fi Direct must go.** `/system/etc/permissions/android.hardware.wifi.direct.xml` is in the image and
`build.prop` has `ro.mediatek.wlan.p2p=1`, so the framework would start `p2p_supplicant`, whose CM definition
brings up `-ip2p0` as a second interface. The proven driver offers only `* IBSS * managed` interface modes
(`iw_phy_20180101_225931.txt`) and registered one netdev, `wlan0`; wpa_supplicant exits when one of its
interfaces fails to initialise. Two layers in the patch set: `android-boot.sh` binds an overlay of
`/system/etc/permissions` without the direct feature (same pattern as the idc overlay; `down()` peels it), and
`init.aosp.rc` defines a single-interface `p2p_supplicant` as a safety net.

**DHCP and DNS.** `libnetutils` (`dhcp_do_request`) starts `dhcpcd_wlan0` and reads `dhcp.wlan0.*`
properties (`.ipaddress .gateway .mask .server .leasetime .mtu .dns%d .domain .result`), which the
`95-configured` and `20-dns.conf` hooks of `/system/etc/dhcpcd/dhcpcd-hooks/` set with `setprop`. DNS
reaches apps through netd's resolver (the framework pushes the lease's servers; `netd` already has
`socket dnsproxyd` in our rc). There is no `resolv.conf` to place; the read-only-`/system` problem of the
busybox test does not exist here. `/system/etc/dhcpcd/dhcpcd.conf` is absent from the image; dhcpcd's
`-ABK` run without one is UNVERIFIED but is the stock arrangement of this image.

**Kernel prerequisites** for Android 5.1 networking, checked in the tree's `.config` (the stock one):
`IP_ADVANCED_ROUTER`, `IP_MULTIPLE_TABLES`, `NETFILTER_XT_MARK`/`TARGET_MARK`/`MATCH_MARK`,
`NETFILTER_XT_CONNMARK`, `QTAGUID`, `QUOTA2`, `NF_NAT`, `ANDROID_PARANOID_NETWORK=y`. Absent:
`NETFILTER_XT_MATCH_OWNER`, `MATCH_CONNTRACK`, `MATCH_PKTTYPE`, `TARGET_TCPMSS`, `RFKILL`. The last is harmless
(the 2026-10-11 supplicant logged `rfkill: Cannot open RFKILL control device` and carried on); the others may
make some netd iptables calls fail. Whether any of that blocks a station connection is UNVERIFIED.

## 5. MAC and NVRAM

- The MediaTek driver reads Wi-Fi NVRAM with `filp_open("/data/nvram/APCFG/APRDEB/WIFI")`
  (`platform.c:144,667`, `nvram_read` at `:510`), plus `/etc/MT6625L_SingleSKU_000.dat` and
  `/etc/firmware/WIFI_RAM_CODE_8590` the same way. In the proven run `wlanProbe` ran in the kernel thread
  `mtk_wmtd_a` (`[143:mtk_wmtd_a][MT6582]: HifAhbProbe()`), and a kernel thread resolves paths against the
  **host's** root, not the container's. So an NVRAM stub in the container's `/data` would never be seen,
  and does not need to be: the file is simply missing on the host as well, which is the `NVRAM WARNING:
  Err = 0x01` path of the log, and the MediaTek default MAC `00:08:22:40:53:3f` results.
- Nothing in CM's Wi-Fi chain needs NVRAM: `readelf -d` on `wpa_supplicant`, `dhcpcd`, `netd`,
  `libwifi-service.so`, `libhardware_legacy.so`, `libnetutils.so` shows no `libnvram*` (0 hits each).
  `nvram_daemon`/`libnvram` belong to the BT/audio HAL discussions, not here.
- Consequence: every unit with this driver uses the same default MAC. Two Walkmans on one network would
  clash. If that matters, set the address before the supplicant starts (`ip link set wlan0 address ...`,
  whether `wlan_mt` accepts it is UNVERIFIED), or place a `WIFI` file on the host's `/data/nvram`, which is a
  write to a partition the ROADMAP gates and is not proposed here.
- The host has a `/data/nvram` (Sony's init creates it, `docs/phase0-architecture.md`; `mtkbt` reads a BD address from
  it in the 2026-10-11 kmsg: `BT_open: Read NVRAM : BD address cc988b3a3d61`). That is worker A's side.

## Patch set

All three apply cleanly with `git apply --check` against the tree as of this note; `sh -n`/`bash -n` pass.
Base files (sha256): `probe/android/android-boot.sh` `f83e1ce3...ec67`, `tools/android-boot.sh`
`ffd3ba22...ada`, `probe/android/container/init.aosp.rc` `a74f2b29...b47`. Apply from the repo root with
`git apply probe/android/connectivity/wifi/<name>.patch` (not done).

- `android-boot.sh.patch`: `WIFIKO`; `wifi_modules()` (checks the Sony WMT core and the firmware file, loads the
  four modules, prints the launcher/mtkbt state), `wifi_status()`, `wifi_unload()`; the permissions overlay in
  `container()` right after the idc block; `wifi_modules || echo ...` in `up()` between `swap` and `container`
  (non-fatal); the overlay umount in `down()`; `wifi` and `wifi-unload` subcommands.
- `init.aosp.rc.patch`: the two triggers, `wpa_supplicant`, a single-interface `p2p_supplicant`, `dhcpcd_wlan0`,
  `iprenew_wlan0`, inserted after the `debuggerd` service.
- `tools-android-boot.sh.patch`: checks the bundle against its own `SHA256SUMS` (passes here), pushes the four
  `.ko` to `/tmp/wifi`, forwards `wifi`/`wifi-unload`. Without the bundle the container still starts, with a
  message.
- Overlaps to resolve by hand with the audio session and worker A (same anchors): the header comment of
  `android-boot.sh`, the line after `DATAIMG=...`, the `idc` block in `container()`, `up()`, the umount
  loop in `down()`, and the final `case`; in `init.aosp.rc` the region after `debuggerd`.
- Running it needs `NW_CONTAINER_VARIANT=aosp` and `NW_SYS_IMG=work/android/aosp-ford/system.img` on every
  `up`, because `init.aosp.rc` is baked into `container.tgz` and the tools script defaults to the Fire image.

## Components used

| component | where from | licence |
|---|---|---|
| WIFITEST bundle (`mtk_wmt_wifi_soc`, `compat_bp`, `cfg80211_bp`, `wlan_mt_bp`, `mac80211_bp` modules; static `iw`, `wpa_supplicant`, `wpa_cli`; `BUILD.md`) | `~/Downloads/nwa50-wifi-test.tar.gz`, sha256 `fcbab2d7...3893`, copied to `work/wifitest/bundle/` (gitignored); its own `SHA256SUMS` verify. Built by a third party from Sony's GPL tarball plus backports 3.17.1. **Origin of the modules still not reproduced** (`docs/wifi-test.md` open item) | modules GPL / Dual BSD-GPL; wpa_supplicant 2.10 BSD; iw ISC; libnl LGPL |
| CM 12.1 "ford" image userspace: `libhardware_legacy`, `libwifi-service`, `libnetutils`, `netd`, `wpa_supplicant`, `wpa_cli`, `dhcpcd` + hooks, `etc/wifi/*`, `etc/permissions/*`, `xbin/busybox` | `work/android/aosp-ford/system.img.cmstock`, read with `debugfs` only | Apache-2.0 / BSD |
| CM ramdisk (`init.mt8127.rc`, `ueventd.mt8127.rc`) | unpacked from `work/android/aosp-ford/rom/boot.img` into the scratchpad, read only | Apache-2.0 / GPL mix |
| Fire ramdisk (`init.mt8127.rc`, Amazon `init` binary strings) | `work/android/fire-ramdisk/`, read only | Amazon, not redistributed |
| Sony kernel tree 3.10.26 incl. `.config`, `Module.symvers` | `~/.cache/nw-kernel/linux-kernel-3.10.26` (upstream `prodgpl.blob.core.windows.net/download/Audio/NW-A57/linux-kernel-3.10.26.tar.gz`), read only, untouched by the GPS build (checked with `find -newer`) | GPL-2.0 |
| `stp_chrdev_gps.c` (MediaTek, 2011-2015), ported | that tree, `drivers/misc/mediatek/conn_soc/common/linux/pub/` | GPL-2.0 (`MODULE_LICENSE("GPL")`) |

## Verified and unverified

**Verified** (seen in a file, binary, log or build output):

- `pivotinit` unshares only the mount namespace.
- `wifi_load_driver`/`wifi_unload_driver` are bare `property_set`s; `libhardware_legacy` has no module-loading
  import; `/dev/wmtWifi` appears only in `wifi_change_fw_path` and in a factory test binary; netd is the only
  other importer of that function; the Java jars have no `wmtWifi`.
- The Fire ramdisk has the two power-on triggers commented out; the CM ramdisk has none; `ueventd.mt8127.rc`
  lists `/dev/wmtWifi 0660 system system`.
- Kernel side of the node: static major 153, class device `wmtWifi`, write protocol `0/1/S/P/A`; host ueventd made
  the node in the 2026-10-11 run.
- `wlan_mt_bp.ko` does not depend on `mac80211`; four modules were enough on 2026-10-11 (WPA2, DHCP, ping).
- CM's supplicant: nl80211 only, no `-f`; the bundle's has no `-f` either (explains fix #4).
- Driver NVRAM/firmware paths are opened with `filp_open`; no `libnvram` in CM's Wi-Fi chain.
- `p2p0` is not offered by the proven build; `android.hardware.wifi.direct.xml` is in the image.
- Patches apply (`git apply --check`), scripts parse.
- GPS section below: build, vermagic, symbol check, dispatcher signatures.

**Unverified** (everything that needs the device or the Android framework running):

- that the container's ueventd creates `/dev/wmtWifi` at coldboot, and with the modes expected;
- the init sequencing argument (write completes before `ctl.start`), and Amazon init's handling of `write`/triggers;
- the Settings toggle path end to end: `WifiStateMachine` states, the `DRIVER ...` private commands through
  `android_priv_cmd` against `wlan_mt` (they may return `FAIL`; the framework tolerates most), `wowlan_triggers`;
- `dhcpcd` without `dhcpcd.conf`, the `dhcp`/`net_raw` privileges under `ANDROID_PARANOID_NETWORK`, netd's
  iptables/ip-rule calls on this kernel, name resolution;
- that the host-side `mtk_wmt_wifi_soc` etc. survive a container `up`/`down` cycle and a second `up`;
- the power-**off** path (`func_off(WIFI)` and the remove callback), never run on this unit;
- whether Wi-Fi function-on still works with the Sony launcher stopped (`prepare()` stops the hagodaemons; the
  2026-10-11 run had the whole Sony stack, and `mtkbt` power-cycles BT on every resume);
- the permissions overlay actually hiding Wi-Fi Direct; the MAC reported to the framework.

## Things that did not match earlier notes

- The brief says CM's `libhardware_legacy` "may do the power-on itself". It does not (disassembled).
- `docs/wifi-test.md` counts 4 exports of `mtk_stp_wmt_soc.ko`; the on-device module (`work/device/system/lib/modules/`)
  has **3** (`__ksymtab` is 0x18 bytes): `mtk_wcn_wmt_co_clock_flag_get`, `mtk_wcn_wmt_wlan_reg`,
  `mtk_wcn_wmt_wlan_unreg`. The brief's list ("`mtk_wcn_wmt_func_on/off` ... and `mtk_wcn_soc_gps_sync_ctrl`") is
  off: `func_on/off` are *vmlinux* exports (the chip-type dispatchers), and `mtk_wcn_soc_gps_sync_ctrl` is only a
  string in the module.
- The bundle README's "expected log" `driver(major 153) installed` is not printed (it is `INFO` level, the driver
  runs at `MAJOR`), and is absent from the proven kmsg too. Not an error.
- The brief says the GPS chardev is built "the way `src-mtk_wmt_wifi_soc/Makefile` builds the WiFi chardev".
  That Makefile alone is not enough: the GPS source is not buildable unchanged (next section).
- The brief's "bind `/dev/wmtWifi` in" is replaced by "load the modules first"; the container's `/dev` is a
  fresh tmpfs, but ueventd refills it from sysfs.

## GPS: `mtk_stp_gps_soc.ko`

Source `.../conn_soc/common/linux/pub/stp_chrdev_gps.c` in Sony's tree is the **pre-abstraction combo-chip**
version. It calls `mtk_wcn_wmt_func_on(WMTDRV_TYPE_GPS)`, `mtk_wcn_stp_send_data(buf, n, GPS_TASK_INDX)`, ... with
no chip-type argument, while the stock kernel exports the dispatchers (the shipped BT module, built from
`stp_chrdev_bt.c`, passes `CHIP_TYPE_SOC` everywhere). Built unchanged with the tree's flags
(`-DMTK_WCN_WMT_STP_EXP_SYMBOL_ABSTRACT`, as `drivers/misc/mediatek/Makefile:138` and the Wi-Fi bundle do):

```
$ make -C $KDIR M=<scratch with the original file> ...  ; echo EXIT=$?
EXIT=2
stp_chrdev_gps_soc.c:119:9: error: too few arguments to function 'mtk_wcn_stp_send_data'
... (10 call sites: stp_receive_data x2, wmt_hwver_get, wmt_func_on, wmt_msgcb_reg, stp_is_ready,
     stp_register_event_cb x2, wmt_msgcb_unreg, wmt_func_off)
```

The stock dispatcher confirms the signature from the binary (`vmlinux.bin`, `kallsyms.out`): `mtk_wcn_wmt_func_on`
at `0xc04091cc` indexes its callback table with `r1 << 2` (the second argument), `mtk_wcn_wmt_hwver_get` at
`0xc0409278` with `r0`. `CHIP_TYPE_SOC` is `0` (`arch/arm/mach-mt8590/include/mach/mtk_wcn_cmb_stub.h:99`).

The port (`probe/kmod/mtk-stp-gps-soc/PORT.diff`: 13 changed lines plus an 11-line header note) adds `CHIP_TYPE_SOC` to
those call sites and renames the driver `mtk_stp_GPS_chrdev_soc`; nothing else changed. Registered major stays 191
(`GPS_major` module parameter to override), class and device name `stpgps`, so the node is `/dev/stpgps`
(`ueventd.mt8127.rc:5`, `0660 gps gps`).

Build (`probe/kmod/mtk-stp-gps-soc/`, `Kbuild` + `Makefile`, `KBUILD_EXTRA_SYMBOLS` for the three exports of the
shipped WMT module in `mtk_stp_wmt_soc.symvers`, CRC 0 because `MODVERSIONS` is off):

```
$ make -j 2 > build.log 2>&1 ; echo "EXIT=$?"
EXIT=0                                   # only warning: "WMT_IDC_SUPPORT" is not defined [-Wundef]
$ make verify > verify.log 2>&1 ; echo "EXIT=$?"
EXIT=0
vermagic: 3.10.26 SMP preempt mod_unload ARMv7
undefined symbols: 34 / from vmlinux exports: 33 / from mtk_stp_wmt_soc: 1 / unresolved: 0
$ modinfo mtk_stp_gps_soc.ko
license: GPL   depends: mtk_stp_wmt_soc   vermagic: 3.10.26 SMP preempt mod_unload ARMv7   parm: GPS_major:uint
```

- vermagic is identical to `mtk_stp_wmt_soc.ko`, `mtk_stp_bt_soc.ko` and `mtk_wmt_detect.ko` of the device.
- The 34 undefined symbols: 33 are in the stock `Module.symvers` (6510 vmlinux exports), and the repo's independent
  decoder `probe/kmod/swap-accounting-guard/tools/verify-stock-exports.py` run against the real `vmlinux.bin` and
  the live `kallsyms.out` agrees: `decoded 4037 EXPORT_SYMBOL and 2473 EXPORT_SYMBOL_GPL ... all 33 imports are
  satisfied by the stock kernel`. The 34th, `mtk_wcn_wmt_co_clock_flag_get`, is exported by the shipped
  `mtk_stp_wmt_soc.ko` (and the same run lists its marker `__ksymtab_mtk_wcn_wmt_co_clock_flag_get@0xbf035fb0`
  from the live device). `rtc_low_power_detected` is a vmlinux export.
- The kernel tree was only read: `find $KDIR -newer <marker before the build> -type f` printed nothing after both
  builds. The module is 121 KB unstripped.

Not claimed: that the CONSYS die has working GPS (no Walkman advertises it; the Fire 7 has none), that WMT accepts
type 2 on this unit, or anything about reception. `gps-open-test.sh` and its checklist test only the function-on
and function-off through WMT. Known upstream wart handled in the checklist: if `GPS_open` switches the function on
and then finds STP not ready, it returns `-ENODEV` without switching it off and leaves a reset callback registered.
GPS userspace (`mnld`, `gps.*.so`) stays out until the function-on test passes. When it comes: Amazon's init has no
`gps` user or group name, so a `service mnld` needs numeric ids.

## Open questions only a device run settles

1. Does the container's ueventd create `/dev/wmtWifi` from the host-loaded class device (checklist step 3)?
2. Does `func_on(WIFI)` work with the Sony stack stopped by `prepare()` (is `6620_launcherA` still running)?
3. Is the init-trigger sequencing as argued, or does the first supplicant start race the `write`?
4. What does the CM framework's `DRIVER ...` command traffic do against `wlan_mt`?
5. Does netd's setup (fwmark rules, iptables) go through on this kernel, and does DNS resolve?
6. Is the Wi-Fi **off** path (`write 0`, remove callback) clean on this unit?
7. Does `down` followed by `up` leave `wlan0` and the host modules in a usable state?
8. GPS: does WMT accept `type(2)`; is major 191 free in `/proc/devices`?

## Device result (2026-10-15 evening): radio on during boot -> HW watchdog reset

First device run of the merged glue, by the image session (distribution-project-30, operator-approved
adb), on the full customized ford image with `NW_DROP_SYS_BOOT=1` and the e730dd7 swap guard loaded
(sha256 `cbcf6d6f9360...`, both swap bugs patched).

- Boot was healthy up to the radio: zygote 162 s, boot animation on Mali 163 s, then
  `wmt_wcn_wmt_func_on` type 3, `WMT turn on WIFI success`, cfg80211 `wiphy_register`,
  `write /dev/wmtWifi 1` (174-178 s), init starts `wpa_supplicant` 179.340 s, wlan0
  `NETDEV_UP ... link is not ready` 179.699 s.
- `/proc/last_kmsg`, read before any other reboot (archived as
  `work/android/wifi-bisect-20261015T224828Z/last_kmsg.wifi-on-crash.out`):
  `ram console header, hw_status: 1, fiq step 0.` The streamed kmsg showed no oops, but the ram
  console does: at `[180.144132]` `[2574:wpa_supplicant] Unable to handle kernel paging request at
  virtual address 00a08b20`, `Oops: 5 [#1]`, a nested NULL oops `[#2]`, then `Nested panic` and the
  hardware reboot. Previous `pc: bf1741cc lr: bf174198`, `r4: 00a08000`. First read of this file
  stopped at an earlier surfaceflinger line and reported no oops; that was wrong.
- Same image with the Wi-Fi modules left off: boots to Trebuchet (`sys.boot_completed=1`,
  `mFocusedApp=com.cyanogenmod.trebuchet/Launcher`), 674 s+ without a reset. The image is not the
  cause; the audio HAL had been ruled out separately (stock MTK HAL still reset with Wi-Fi on).

Not a userspace stall: the watchdog is kicked by the per-CPU `wdtk-N` kthreads (SCHED_FIFO,
`drivers/misc/mediatek/wdk/wd_common_drv.c:401`) and Sony's `.config` has no
`CONFIG_MTK_HANG_DETECT`, so only a CPU stuck in the kernel, an RT hog starving `wdtk` or a bus hang
can bite. A brownout is the other candidate for a silent end, but `hw_status: 1` says watchdog.

What differs from the 2026-10-11 proof, which ran scan/WPA2/DHCP with the same four modules: that run
was an idle Walkman One host with the bundle's static `wpa_supplicant`/`iw`, no zram, no Mali r4p0,
no container. Here the radio came up during the boot animation, under boot memory pressure, and CM's
Fire-built `wpa_supplicant` opened the interface.

Wi-Fi is opt-in from here (`NW_WIFI=1` on `tools/android-boot.sh up`), so other sessions' boots do
not take the reset. Next cut, all from a fresh device boot, a live `/proc/kmsg` stream running, and
`/proc/last_kmsg` pulled to a timestamped `work/android/` dir after any reset:

| step | how | isolates |
|------|-----|----------|
| W1 | before the run, in a no-Wi-Fi container: `settings put global wifi_on 0` and `wifi_scan_always_enabled 0`; then fresh boot, `NW_WIFI=1 up` | modules loaded at boot, radio never powered (`wlan.driver.status` stays unset) |
| W2 | W1 at home, idle 2 min: `sh 'setprop wlan.driver.status ok'` (init writes 1) | radio power-on at idle |
| W3 | host `busybox ifconfig wlan0 up` (shared netns) | interface open, no supplicant |
| W4 | host bundle `bin/iw dev wlan0 scan` | driver scan path in this configuration, without CM userspace |
| W5 | `sh 'svc wifi enable'` | the CM framework + `wpa_supplicant` path at idle |

All five passing at idle points at boot-time load or timing, and the fix is then to power the radio
only after `sys.boot_completed`. A reset at W2-W4 is a driver/firmware matter for a `/proc/wdk` FIQ
capture (`devlog/2026-10-12-watchdog-fiq.md`; that write needs the operator's approval each time).

## W1-W5 result and the fix (2026-10-15 night)

Run by distribution-project-30 from one `NW_WIFI=1` boot that reached home, all steps at idle
(evidence `work/android/wifi-bisect-20261015T224828Z/`: `kmsg-live.log`, `last_kmsg.W5.out`):

| step | result |
|------|--------|
| W1 modules, radio off | clean: `wlan.driver.status=unloaded`, no wlan0, `/dev/wmtWifi` 153:0 |
| W2 `setprop wlan.driver.status ok` | clean: wlan0 appears |
| W3 `ifconfig wlan0 up` | clean |
| W4 bundle `iw dev wlan0 scan` | clean, 72 BSS |
| W5 `svc wifi enable` | oops ~0.4 s after `wpa_supplicant` started, `hw_status: 1` |

What W5 adds over W4 is visible in `kmsg-live.log`, before the supplicant starts:

```
[  370.255177] (0)[1903:init][MTK-WIFI] WIFI_write: WIFI is already power on!
[  370.264508] (1)[2227:netd]PRIV_CMD_P2P_MODE=1 0
[  370.264590] (1)[3437:soc_tx_thread]glRegisterP2P
[  370.277948] (0)[2227:netd][MTK-WIFI] WIFI_write: Set wlan mode 0 --> 2
[  370.321870] (1)[1903:init]init: starting 'wpa_supplicant'
[  370.716224] (1)[4150:wpa_supplicant]Unable to handle kernel paging request at virtual address 00406b20
```

netd writes the firmware-path string into `/dev/wmtWifi` (CM `libhardware_legacy.so` has
`/dev/wmtWifi` next to the `P2P`/`AP` fw-path strings; WifiStateMachine calls
`wifiFirmwareReload(wlan0, "STA")` on enable). In Sony's `wmt_chrdev_wifi.c` an `S` or `P` write calls
wlan_mt's `set_p2p_mode(enable=1, mode=0)`, which registers the P2P interface, and sets
`WLAN_MODE_STA_P2P` (mode 2).

The oops, symbolized against `cfg80211_bp.ko` (load base 0xbf16d000 in that boot, so pc
`bf1791cc` = `.text+0xc1cc`): `__cfg80211_rdev_from_attrs+0xfc`, called from `nl80211_pre_doit`. That is
the `NL80211_ATTR_IFINDEX` branch: `netdev->ieee80211_ptr` (+0x180) -> `wdev->wiphy` -> `rdev = wiphy -
0x160`, then `ldr r3, [r4, #0xb20]` reads `wiphy_net(&rdev->wiphy)`. `r4` was `0x00406000` (W5) and
`0x00a08000` (boot crash, same instruction), so the wireless_dev that the looked-up netdev points at
holds a garbage `wiphy`. Not established at the time: which netdev, and why. (The claim first written
here, that W4 did the same lookup and so P2P registration caused the fault, was wrong; see V1
below.)

Fix: `probe/kmod/mtk-wmt-wifi-soc`, a rebuild of the bundle's chardev from Sony's source with a
station-only patch (`PORT.diff`). `S` powers the function and returns success without P2P, `P` and `A`
fail with `-EOPNOTSUPP` (netd's soft-AP reload would take the same path), `wlan_mode` stays HALT,
and the power-off, `X` and chip-reset paths no longer ask wlan_mt to turn off a P2P/AP interface that
was never registered. Host checks: the unpatched rebuild matches the bundle's `mtk_wmt_wifi_soc.ko`
in every section, and only the 20-byte build ID differs. The patched one builds with the same three
warnings as the unpatched source (`make verify`: vermagic matches, 21 imports all vmlinux exports).
It exports the same 5 symbols, and those are the 5 that `wlan_mt_bp` imports.
`tools/android-boot.sh` pushes it in place of the bundle's chardev. Not yet run on the device;
Wi-Fi stays opt-in until W5 passes with it.

## V1: the station-only chardev does not stop the oops (2026-10-15 night)

`NW_WIFI=1` boot with 409a0d6, at home, `svc wifi enable` (evidence `work/android/wifi-v1-*/`,
`last_kmsg.V1.out`, `kmsg-live.log`). The patch engaged: netd's write was a `P`
(`WIFI_write: P2P/AP mode 'P' is not supported on this device`). No `PRIV_CMD_P2P_MODE` or
`glRegisterP2P` appeared. `wpa_supplicant` started at 124.437 s, NETDEV_UP came at 124.626 s, and at
126.530 s the same oops followed: pc `bf17a1cc` = `.text+0xc1cc`, `r4: 0011a000`, then a nested
panic and `hw_status: 1`. So P2P registration is not the cause. The chardev change stays: it stops
a P2P/AP request from reaching wlan_mt's untested P2P code, and with it netd's fw reload fails
cleanly. It does not fix this oops.

Correction to the W4 reasoning: `iw dev wlan0 scan` is an `NL80211_FLAG_NEED_WDEV` op. It resolves
the wdev by walking `rdev->wdev_list`, so it never reads `netdev->ieee80211_ptr`. The oops is in the
`NEED_WIPHY` path (`cfg80211_get_dev_from_info` -> `__cfg80211_rdev_from_attrs`, IFINDEX branch),
which W1-W4 never exercised.

Ruled out on the host:
- net_device layout: the stock kernel has `ieee80211_ptr` at 0x180, `cfg80211_bp` reads 0x180, and
  `wlanProbe` stores its wdev at 0x180 right after `alloc_netdev_mqs`.
- wiphy tail: wlan_mt's `vendor_commands` / `n_vendor_commands` (=5) writes at +0x9cc / +0x9d4 fit
  cfg80211's layout, where `_net` is at +0x9c0.
- WEXT: `cfg80211_bp.ko` has no wext symbols, although Sony's in-tree backports `.config` sets
  `CPTCFG_CFG80211_WEXT=y`. So the bundle's cfg80211 was configured differently from that file.
  wlan_mt allocates its `wireless_dev` with `kmalloc(0xc8)`.

The junk `wiphy` values all end in 0x160 (`0x00406160`, `0x00a08160`, `0x0011a160`), which is
`offsetof(cfg80211_registered_device, wiphy)`. Next: a hand-run supplicant config bisect (minimal
conf, then `wowlan_triggers`, then the stock conf) with its logcat streamed, to name the crashing
command.

## GPS function-on: passed on the device (2026-10-15 night)

Run by distribution-project-30 on the bare Sony firmware (no container), as the GPS checklist says.
Preconditions were clean: wmt, bt and detect loaded, gps absent, major 191 free. Kernel log:
`mtk_stp_GPS_chrdev_soc driver(major 191) installed`, `GPS_open: major 191`,
`[WMT-STP-EXP] mtk_wcn_wmt_func_on type(2)`, `[GPS] GPS_open: WMT turn on GPS OK!`, then after the
5 s hold `GPS_close: WMT turn off GPS OK!`. Verdict `GPS FUNCTION ON/OFF OK`. The device stayed up
and the module unloaded cleanly. So WMT accepts type 2 on this unit; the GPS block is not fused
off. Still not claimed: reception or a fix. There is no antenna path known and no GPS userspace
(`mnld`, `gps.*.so`) in the image.

The script printed `open+hold+close rc=127` on that passing run: by the time the poll loop ended,
the device shell had already reaped the background subshell, so `wait` had nothing to report.
`gps-open-test.sh` now has the subshell write its own status to a file.

## E1-E6: wlan_mt's testmode_cmd has the 3.10 signature (2026-10-21)

Evidence: `work/android/wifi-bisect-20261021T063149Z/` (cmds.E4-E6.log, kmsg-live.E4-E6.log, logcat-live.E5/E6.log,
last_kmsg.E4/E5.out). All runs from one `NW_WIFI=1` boot at home, `wifi_on` 0. The radio was
powered with `setprop wlan.driver.status ok`, and CM's own `/system/bin/wpa_supplicant` was run
by hand with `-dd -t`. Android's supplicant logs through logcat, not stdout. A config under
`/data/local/tmp` gets EACCES; one under `/data/misc/wifi` (system:wifi 0660) works.

| run | what | result |
|-----|------|--------|
| E1 | minimal conf (ctrl_interface only), 45 s | clean, init and teardown |
| E2 | E1 + `wowlan_triggers=disconnect` | clean, `nl80211: Setting wowlan` at init |
| E3 | the stock conf + overlay + `-e`/`-O`, 45 s | clean |
| E4 | E3, then wpa_cli `DRIVER SETSUSPENDMODE 1`, `... 0` | the second command hung, oops, `hw_status: 1` |
| E5 | E3, then `DRIVER SETSUSPENDMODE 0` twice | the same: the first answers, the second oopses |

The oops is the known one: pc `bf1751cc` = `__cfg80211_rdev_from_attrs+0xfc`, r1 = r2 = 7
(wlan0's ifindex), r4 = `0x00cf0000` / `0x00538000`. The supplicant log of the first command
shows the path: `iface wlan0 recv cmd SETSUSPENDMODE 0`, then `nl80211: Test Mode buflen = 12,
ifindex=7`, then `ret = -22`. A private ioctl 0x89f1 goes out too, and `wlanDoIOCTL` only
warns about it and returns 0.

Cause: backports 3.17's cfg80211 calls `testmode_cmd(wiphy, wdev, data, len)`, but wlan_mt
defines `mtk_cfg80211_testmode_cmd(wiphy, data, len)` (gl_cfg80211.c). It treats the
`wireless_dev` as its parameter block, and its first statement,
`prParams->index &= ~BITS(24,31)`, clears the top byte of `wdev->wiphy`. The index is garbage,
so the call returns -EINVAL. The next NEED_WIPHY request on ifindex 7 computes
`rdev = wiphy - 0x160` from the damaged pointer. That is why every junk `wiphy` value ended in
0x160 with a zero top byte.

Fix (`tools/android-boot.sh` `wlan_mt_patched`): five ARM instructions in the bundle's
`wlan_mt_bp.ko`, md5-checked. The entry `subs r3,r0,#0; mov ip,r1; mov r4,r2` becomes
`mov r1,r2; mov r2,r3; subs r3,r0,#0`, and `ldr/str r4,[ip]` becomes `[r1]`. The
sub-handlers already get (wiphy, data, len) in r0-r2. None of the five words has a relocation.
The patched copy goes to `work/android/boot-out/` and is pushed in place of the bundle's.

| run | what | result |
|-----|------|--------|
| E6 | patched module, E3, then 18 framework-style commands: SETSUSPENDMODE 0/0/1/0, RXFILTER-*, BTCOEX*, SETBAND, POWERMODE, `SET ps`, MACADDR, COUNTRY, SCAN, SCAN_RESULTS | no oops. POWERMODE and COUNTRY answer FAIL, the rest OK |
| E7 | `svc wifi enable` (framework + init's supplicant), 25 s, then 3 screen off/on cycles | `Wi-Fi is enabled`, ScanState, 17 BSS, no oops |
| E8 | user joins a WPA2-PSK AP on 2.4 GHz (ch 3) from Settings | COMPLETED, DHCP lease, `everValidated{true}`, ping 8.8.8.8 3/3, DNS resolves, 1 MB HTTP download in 5 s, no WARNING/oops in dmesg after 3 h 18 m uptime |

Still open: `mtk_cfg80211_mgmt_tx` has the same kind of mismatch. 3.17 passes
`(wiphy, wdev, params, cookie)`, while the driver takes the 3.10 list (channel, offchan, wait, buf,
len, no_cck, dont_wait_for_ack, cookie). Any action-frame TX (802.11v BTM response, GAS/ANQP,
SA Query, P2P) would get garbage arguments. It needs a source rebuild, which first needs the
bundle's backports config reproduced: Sony's tree ships `CPTCFG_CFG80211_WEXT`, and the
bundle's cfg80211 has none. E8 did not trip it.

## wlan_mt rebuilt from source (2026-10-21)

`probe/kmod/wlan-mt` (152b042) rebuilds the driver the way the bundle's BUILD.md describes:
backports 3.17.1-1 copied out of the kernel tree into `work/wlan-mt/bp`, its Kconfig regenerated
from the kernel's `.config` (no WIRELESS_EXT, so CFG80211_WEXT drops out as in the bundle), then
wlan_mt with the bundle's patch 0001 and our 0002. The bundle's compat_bp and cfg80211_bp stay in
use; `make verify` checks that the cfg80211 built here has their section sizes.

gcc's incompatible-pointer warnings list every op whose type differs from 3.17's. Eight show up on
the ops tables: the STA `testmode_cmd` and `mgmt_tx`, and six station ops that differ only in
`const u8 *mac`. The P2P ops already have `CONFIG_MTK_BENTEN_SUPPORT` variants with the 3.17
lists; 0002 adds the same for the two STA ones. The genl_register_ops warning in gl_rst.c is
harmless: backport_genl_family starts with the native genl_family.

The first build oopsed on the device: NULL dereference in `mtk_wmtd_a` right after
`HifAhbProbe()`, at the first `wlan.driver.status=ok` (last_kmsg). A 0001-only build differed from
the bundle's module in 5 instructions (`mov r0, #4` vs `#2`, `ldr [r2, #0x34c]` vs `#0x33c`). The
shared kernel tree `~/.cache/nw-kernel/linux-kernel-3.10.26` had `CONFIG_NR_CPUS=4` since
2026-10-16 15:40; the stock IKCONFIG has 2, and nothing else differed. The kernel tree is now prepared from
the stock config again (old one kept as `.config.nr4-20261016`). After that, a 0001-only build's `.text` is byte-identical to the
bundle's, and every differing `.data`/`.rodata` word is a relocated pointer into the string
sections (build paths). With 0002, only `mtk_cfg80211_mgmt_tx` and `mtk_cfg80211_testmode_cmd`
change size. The Makefile now refuses a kernel tree whose `include/config/auto.conf` differs from
the stock config. All kmods deployed so far were built before 10-16 15:40.

| run | what | result |
|-----|------|--------|
| R1 | first build (NR_CPUS=4 tree), `up` | oops in HifAhbProbe, watchdog reset |
| R2 | stock-config build (md5 40cce6ea…), `up`, auto-connect to the saved AP | no oops; ASSOC-REJECT status 1 at RSSI -85 (E8 joined at -63; the bundle module was also rejected at -79 on 10-20) |
| R2 | 3 screen off/on cycles, 8x `SETSUSPENDMODE` | no oops, supplicant alive |
| R3 | user joins a second WPA2 AP (2.4 GHz, -47 dBm) from Settings | COMPLETED, DHCP, `everValidated{true}`, ping OK |
| R3 | `wpa_cli wnm_bss_query 0` twice (BTM Query action frame) | `--> mtk_cfg80211_mgmt_tx()` x2, `NL80211_CMD_FRAME_TX_STATUS`, `Action TX status: cookie=01 (match) (ack=1)`, no oops, still COMPLETED |

mgmt_tx now sends the frame the supplicant gave it and returns a cookie that its TX status matches.
