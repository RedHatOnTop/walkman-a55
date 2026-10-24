# Release checklist: Android on the NW-A55

What still stands between the current tree and someone else installing the ford container +
WalkmanUI from their own firmware. Status as of 2026-10-24; a done item names its commit or the
check that showed it. "Device" items wait for the A55.

Releases are alpha, not stable, and the README and `docs/INSTALL.md` say so (the owner,
2026-10-24): what is left below goes out as fixes after a release, not before it.

## 1. Blocking

### Booting without a PC

- Boot set on `/contents` (`wmmod-android/boot`, md5 `MANIFEST`), so Android starts without
  the host: done, 9197e15.
- Dual-boot picker: `bootpick` + screens + install/remove UPGs done (221f18e, df8e003).
  The install UPG was flashed on 2026-10-21 and the unit stuck on the logo. The UPG
  trigger boots the recovery slot, and p9 still held the control-reason test image from the
  core-revival work, so the updater never ran the picker script (`/bin/bootswitcher.sh` read
  back as Sony's, no `bootswitcher.sony.sh`). The operator wrote the 10-10 recovery back and
  it read back byte-equal (devlog/2026-10-21-picker-recovery.md). The UPG then ran twice on
  10-21 (`PICKERUPG.log`), and on 2026-10-22 the picker started the remembered Android on
  its own on four boots (devlog/2026-10-22-kit-first-device-run.md). **Device:** choosing
  with the keys and the Walkman side were not watched.
- Recovery before a UPG: the kit refuses a backup whose recovery slot is not Sony's updater
  with a known key (`kit/upg_key.py`), and `install.json` records that image's SHA-256 so an
  installer can compare the player's slot before the trigger. walkman-manager does not read
  the slot back yet.
- Hook hardening (4390676, devlog/2026-10-23-picker-fallback.md):
  - Walkman is the picker's default until Android reports `sys.boot_completed`: on the unit
    a start cut short at 30 s left `1`, and the next boot took Walkman by itself. In the
    boot set, so no UPG needed.
  - `bootpick` runs under busybox `timeout -t 180`; tested on the host only. It is in the
    hook, so it reaches the unit with the next picker UPG.

### Building from the user's own firmware

`kit/make-install.sh` is the one entry point (573139f, d4f0675): from the owner's backup
(`WMBACKUP`), the ford zip and the Fire OS 5.3.6.4 update it writes the boot set, both
images, the picker UPG with the key read from the owner's recovery, and `install.json`.
Our own binaries come from the pinned prebuilt release (`kit/prebuilt.sha256`) or
`KIT_FROM_SOURCE=1`. walkman-manager's Firmware Studio drives it. A set it wrote booted on
the unit on 2026-10-22 with a fresh /data: Wi-Fi and Bluetooth scans, WalkmanUI from
`/system/etc/walkmanui/res` (devlog/2026-10-22-kit-first-device-run.md). The app's install
path (WALKMAN drive copy, SCSI trigger) ran on 2026-10-23: the Studio's set copied and read
back, the picker UPG's log checked, `boot completed` at 66 s (walkman-manager's devlog of
that day). **Device:** only as an update; a clean install from the app has not run.

What `KIT_FROM_SOURCE=1` and the hand steps need that a clean clone does not have, all
gitignored:

| input | from |
| --- | --- |
| A55 firmware dump (QML, fonts, images, SensMe tables, boot animation) | the user's own unit |
| ford CM 12.1 `system.img`, Fire OS 5 boot image (init, healthd) | the user |
| `oss/` (Sony GPL kernel, Qt 5.3.2) | Sony's OSS site |
| `wampy/` (nw-installer crosstool for `nw-probe-builder:local`) | unknown321/wampy |
| `~/.cache/arm-eabi-4.8`, `~/.cache/nw-kernel/linux-kernel-3.10.26` prepared with the stock config | AOSP prebuilts, `oss/` |

### Assets that must not ship

- Wi-Fi: the WIFITEST bundle is no longer needed. `make -C probe/kmod/wlan-mt verify` builds
  compat/cfg80211/wlan_mt and checks compat and cfg80211 against the bundle's code byte for
  byte: 2caab3f. Loaded on the unit from the kit's set on 2026-10-22; the setup wizard's
  Wi-Fi page listed the networks around.
- Browser: this repository's own (`android-browser/`, on the image's WebView 81), built into
  the prebuilt set. Firefox (Fenix 143, the last build for Android 5 to 7) is out of the
  image: it held 143 to 170 MB with swap where this one holds 39 to 72 MB
  (devlog/2026-10-22-webview-browser.md).
- Boot animation zips (made from Sony's): no longer tracked, generator only: 6dfcff8.
  History before it still has them.
- Wallpapers: no longer tracked, generators only. The kit draws the default
  (`probe/wallpaper/gen_hires.py`) on the owner's computer in the SST faces from their backup,
  with numpy and Pillow alone. History before it still has the four JPEGs.
- Launcher icon: the release APK carries this repository's placeholder as PNGs, and the kit
  draws the owner's icon over them in the image, from the W. of their player's shutdown logo
  (`android-ui/launcher_icon.py`); on the unit 2026-10-22. Development builds still draw it
  from the ZX507's HighResMediaPlayerApp when `work/` has it.
- No tracked file is a byte copy of a firmware file: every tracked file over 200 bytes was
  hashed against `system-rootfs/`, `nwa50-extracted/`, `work/device/`, `work/qrc/`, the Fire
  ramdisk/unpack, `work/android/aosp-ford/`, `work/zx507-res/` and `oss/` (4836 files). The
  one match is `HANDOFF.md`, our own document.

### Install and recovery docs

`docs/INSTALL.md`: backup, kit, copy, picker package, starting, updating, removing, and the
boot ROM path. No root adb is needed for any of it. The BROM path (mtkclient, MT8590,
SBC/SLA/DAA off) read `android` and `var` in full and wrote the recovery slot back once,
read back equal (2026-10-21); no other partition has been written that way. Not run:
`picker-remove-upg` and the guide on another player. The copy through the WALKMAN drive ran
from walkman-manager on 2026-10-23.

## 2. Goals (device)

| goal | measured so far |
| --- | --- |
| no UI lag | Settings scroll only (12.9 ms mean frame); launcher, SystemUI, WalkmanUI not yet |
| battery ≥ 70 % of stock, Wi-Fi/BT off | no stock baseline, no container run since the healthd patch |
| dual-boot picker | see 1 |
| hardware play/next/prev | `sendevent` only, no real button press |

## 3. Stability (device)

PC link lost twice (logging + auto-reset since 6a072ca; read `usb-log.txt` next time),
one `adb reboot` about 70 s after a boot completed that never came back until the power
key (2026-10-23, no kernel log; devlog/2026-10-23-picker-fallback.md),
PlaybackService ANR, Bluetooth gaps, power menu "Power off" never tried, slow first minutes
after boot. Decide whether Wi-Fi is on by default (today `NW_WIFI=1`).

The status bar no longer keeps the top of the panel after Quick Settings starts an activity
over the lock screen: SystemUI draws on the GPU (`persist.sys.force_highendgfx`), at 8 to 15 MB
of Mali memory (devlog/2026-10-23-systemui-gpu.md).

keystore runs on AOSP's software keymaster since b09d38c: Settings no longer crashes at the
end of the setup wizard, and the screen-lock page opens.

Boot time, picker to launcher (devlog/2026-10-22-kit-first-device-run.md): the boot
animation starts about 6 s after Android is chosen since 24771ea (was 77 s: a 60 s wait
for a service Walkman One does not have, then busybox forks). Firefox's dexopt hit
dex2oat's 6-minute watchdog and ran again on every boot; with `--no-watch-dog` it is
compiled once (384 s) and the screen comes on at 55 s uptime.

A fresh /data (what a clean install gives) starts in CM's setup wizard, then: time zone
Midway Island (GMT-11) unless the owner picks one there, English, the swipe lock screen on
every wake although the A55 has a HOLD switch, and a first boot of about 14 minutes
(dexopt). Decide the defaults.

## 4. Making the repo public

- Licence: GPL-3.0-or-later (the owner, 2026-10-22), `probe/kmod/` GPL-2.0, in `public/`.
  History: `tools/export-public.sh` makes a fresh root on the `public` branch; `-n DIR`
  writes the tree and runs its checks without committing (82b7ac7).
- Device serial: out of the code (`tools/serial.sh`, from `NW_SERIAL` or `work/serial`):
  a1ce279. The docs' and devlogs' copies are replaced by `<serial>` on export, and paths
  under the home directory are written from `~`.
- Sony-headed files:
  - `probe/mkspec/qeglfshooks_mt8590.cpp`: keeps Sony's header and now names the licence
    its Qt 5.3.2 release ships under; this copy is GPL 3.0 (82b7ac7).
  - `android-ui/patches/*.patch`: 518 lines of Sony's QML as context or removed lines, the
    patches' anchors in the owner's own files; `MusicPlayerDefault.qml` keeps 23
    property/signal lines so the ViewModel binds it. Both named in the public README.
  - Other projects' files (AOSP and tinyalsa headers, Qt's `qplatformdefs.h`) keep their
    notices; the README names them.
- Mali r4p0 driver: carries the GPL notices of ARM's own r4p0-00rel0 release instead of the
  DDK notice of the Fire 7 copy, and goes out whole; the rebuilt module's code equals the
  tested one and ran on the unit (e69d9da, devlog/2026-10-23-picker-fallback.md).
- SensMe: the limits of the channel tests come from the owner's `libMediaStoreService.so`
  like the centroids (devlog/2026-10-24-sensme-limits-from-firmware.md). The trees published
  before carried them in `sensme.cpp`; a fresh root replaced that history.
- Other personal data: no SSID or passphrase anywhere. `docs/wifi-test.md` has only a DHCP
  LAN address and MediaTek's default MAC. `mcp-server/TEST-OUTPUT.txt` has a Tailscale
  address and a LAN address; devlogs name the build hosts.
- `debloat/manifests.json` is component metadata pulled from Fire OS APKs, from the Fire OS
  phase; the ford build does not use it.
