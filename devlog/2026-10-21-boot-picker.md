# Dual-boot picker (goal 3), 2026-10-21

Goal 3 of `nw-a55-android-goals-2026-10-19`: a boot screen where the volume keys move the
selection and power or play selects Sony or Android. Until it exists, Android comes back only
with `tools/android-boot.sh up` from the host.

## Where it hooks in

- The Sony ramdisk's `init.rc` boot stage runs `exec /bin/sh /system/bin/bootswitcher.sh`
  (`work/ramdisk-check/root/init.rc:258`), after `wait /dev/icx_nvp/000`. The script sets
  `sys.sony.bootmode`; `=1` is what runs `class_start hagoromo` (the 28 hagodaemons and the UI).
  It lives on `/system`, not in the ramdisk.
- A picker in front of it: Sony → the original script, unchanged. Android → no bootmode, so no
  Sony service ever starts, and the container starts from the device (below).
- `exec` blocks init. While the picker runs, `setprop ctl.*` (and so toolbox `stop`/`start`) is
  not served; the Android start has to run after the script returns, in the background.
  `icx_bootanimation` is drawing on fb0 at that point and has to be killed directly (`killall`,
  as the Sony app's `StopBootAnimation` does).
- Keys, read on the running device (`/proc/bus/input/devices`, `KEY=` bitmaps, 32-bit words):
  - event0 `mtk-kpd`, built into the kernel: 102, 107, 114 (vol down), 115 (vol up),
    116 (power). Present from kernel boot.
  - event4 `icx_key`, `/sys/devices/virtual/input/input4`, created by `wm_key.ko`
    (`icx_key_probe`): 28 (play), 105/106 (prev/next) and the rest. `load_sony_driver`
    loads that module as a separate service started just before the `exec`, so the picker
    has to wait for the node; volume and power alone are enough to drive it.
- Open: `wdt_ctrl 30` (post-fs) arms a 30 s watchdog. Who kicks it while init is blocked is
  not known yet, so the picker's wait stays short until that is measured.
- Install: a script-only UPG (`tools/build_upg.sh`, `nwupg.py -w`) that puts a wrapper in place
  of `bootswitcher.sh` and keeps the original, plus an uninstall UPG. Walkman One already boots
  this unit with a changed boot/recovery ramdisk and a whole changed system (ANALYSIS §11).
  Flashing is the user's step.

## Step 1: the boot set on the device

`tools/android-boot.sh up` used to push everything to `/tmp` on every device boot. Now it
installs a boot set in `/emmc@contents` (`wmmod-android/boot/`, next to the two images) with a
`MANIFEST` of md5 sums, pushed last, and starts the set's own copy of the device script.
`android-boot.sh up` stages the set before anything else:

- only what `MANIFEST` lists, so stale files (Wi-Fi modules after `NW_WIFI` is dropped) are
  never used; a set that no longer matches its sums is refused;
- modules and `container.tgz` as links into the set, the rest (overlays, `pivotinit`, the
  script) copied and renamed into place;
- the swap guard load moved from the host into `up`, right before zram.

In the Sony state with the PC cable in, `/contents` is not mounted (the storage is handed to
the PC). The device start therefore clears the gadget LUNs and mounts `/emmc@contents` on
`/tmp/c` itself; at the picker's point in boot no USB storage mode is active yet.

| Run | How | Result |
|---|---|---|
| S1 | host `up` (NW_WIFI=1 NW_DROP_SYS_BOOT=1, aosp-ford image) after a reboot | set 14 files, md5 OK; guard 11/11; Wi-Fi modules loaded; `boot_completed=1`. `/tmp` tmpfs 3.9M used, was 10.1M with the old pushes (both with Android up, different boots) |
| S2 | reboot, then only `adb shell` LUN clear + `mount -t vfat /emmc@contents /tmp/c` + `sh /tmp/c/wmmod-android/boot/android-boot.sh up`; nothing pushed | 14 files staged; guard 11/11 at 56 s; `boot_completed=1`; powerwatch running; `CapBnd 0000001fffbfffff` (CAP_SYS_BOOT dropped); Wi-Fi on THENETWORK, -39 dBm, 65 Mbps; home screen in the screencap |

## Step 2: the picker

`probe/devtools/bootpick.c` (static, `probe/build.sh tools`) and `tools/bootpick_frames.py`,
which renders its four 480x800 screens at build time from the host's Montserrat and Noto Sans
(OFL), so the repo holds no images: Walkman selected, Android selected, and a "Starting" screen
for each. `bootpick FRAMES DEFAULT SECONDS` exits 1 (Walkman) or 2 (Android); any other status
means the caller boots Walkman.

- fb0 (`bootpick -i`): 480x800, virtual 480x2400, 32 bpp, line 1920, r16 g8 b0 a24, i.e.
  B G R A in memory. The frames are stored in that order and copied row by row.
- Updates follow `icx_bootanimation` (disassembled: `FBIOGET_VSCREENINFO`, `yoffset = 0`,
  `FBIOPUT_VSCREENINFO`, `FBIOGET_FSCREENINFO`, then per frame `activate |= FB_ACTIVATE_FORCE`
  and `FBIOPUT_VSCREENINFO`). Measured: a forced put or a pan takes one vsync, 15.4-16.5 ms;
  a plain put 15 µs.
- The watchdog armed by `wdt_ctrl 30` is kicked by the kernel threads `wdtk-0`/`wdtk-1`
  (`/proc/wdk`: mode 1, interval 10, timeout 30; `drivers/misc/mediatek/wdk/wd_common_drv.c`),
  not by userspace, so the picker may wait for a key for as long as it likes. It does: the first
  key stops the countdown.
- Input device names come from `/sys/class/input/event*/device/name`. Opening every node to ask
  `EVIOCGNAME` (touch, `hwmdata`, `m_batch_input`) made start-up 0.66 s; by name it is 0.28 s.

Device runs, Sony stack stopped with `hagodaemons.sh stop`, keys injected with `sendevent` into
the same evdev nodes, panel read with `fbcap` (what the overlay engine sends to the panel):

| Run | Input | Result |
|---|---|---|
| P1 | default 1, 8 s; VOL down (event0 114) at 3 s; POWER (116) at 13 s | at 3 s Walkman card with the bar about 60 % full; after VOL down Android selected, bar stopped empty; still running at 13 s; after POWER the "Starting Android" screen, rc 2 |
| P2 | default 2, 3 s, no key | rc 2 after 3.8 s (before the sysfs change) |
| P3 | default 2, no countdown; prev (event4 105), VOL up (115), play (event4 28) | rc 1 |
| P4 | default 2, 30 s, capture at 2 s | Android card in mint, bar draining: colours right after the switch to B G R A |

Redesign the same day (user: "소니 순정 펌 느낌 살려서 철저게 블랙 앤 화이트로", the first cut's
glows, colour accents and icon cards read as generated). The screens are now the stock radio
settings list (`components/SettingItemRadio.qml`): 84 px rows, title at x 20, the header rule and
separators as the stock list draws them (sampled from a device capture: `#999999` 2 px, `#333333`
1 px), `ScreenSettings.json` style `normal` (bg `#000000`, text L1 `#ffffff` / L2 `#989898` /
L3 `#666666`, sizes L 26 / S 22). The owner's own SST UI font and `btn_radio_*_normal_88` /
`list_full_pressed` images are read from `work/` at build time and never committed; without them
the generator draws the same shapes with Noto Sans. Rendered at 4x and box-filtered down: at 26 px
FreeType's hinting broke SST Light's "m", and the stock images come back pixel-exact. SST has no
`▶`, so the play mark is drawn. The countdown area (its "Starts automatically" label and 2 px
line) is cleared once a key stops the countdown; a minute without a key takes the selection.

Panel runs of the final build (Sony stack stopped, `sendevent`, `fbcap`): countdown line half
drained at 3 s of 6 s, rc 1 on timeout; after VOL up the label and line are gone; VOL down moves
to Android; POWER shows "Starting Android…", rc 2. No key, no countdown: rc 2 after 60.4 s.

## Step 3: the hook and its UPGs

- `picker-upg/update.sh` (built by `picker-upg/build.sh`, W1 and stock keys, round-trip checked)
  mounts `/emmc@android` read-write in the updater, keeps Sony's `bootswitcher.sh` as
  `bootswitcher.sony.sh` (`cp -p`: root:shell 0755 and its 2019 mtime), and renames the hook into
  place. A second install keeps the first copy. `picker-remove-upg` renames the copy back.
- The hook: a tst-flag (diagnostic) boot and a missing set go straight to Sony's script. Otherwise
  it kills `icx_bootanimation`, runs `bootpick` from `/contents/wmmod-android/boot/picker` with the
  last choice (`/contents/wmmod-android/boot-choice`) as the default and 5 s, saves the choice.
  Walkman: `exec` Sony's script. Android: `setsid android-boot.sh boot` in the background, no
  bootmode, so no Sony service starts.
- `android-boot.sh boot` waits for `load_sony_driver` and `optproc_for_boot.sh`, then `up`. If no
  container init is left, it writes Walkman as the next default and reboots.
- Checked for the never-started Sony stack: `6620_launcherA` is started by `wmt_loader` through
  `ctl.start` at post-fs-data (pid 142, `strings` of `wmt_loader`), not by a hagodaemon. The touch
  driver comes up awake (`sleep_state` is zeroed at probe; only suspend sets it). USB defaults to
  adb (`init.usbcfg.rc`: `sys.sony.config adb`). Not checked: anything PowerService or
  SoundServiceFw set once at start-up that Android now never gets (battery and audio to watch).
- `tools/android-boot.sh install`: the boot set, the picker's files and the images, without
  starting Android.

Rehearsals, nothing persistent:

| Run | How | Result |
|---|---|---|
| H1 | hook text from update.sh with only `SONY` pointed at a fake, `/emmc@contents` on `/contents`, no key | after 5.6 s the fake Sony script ran, hook rc 0, `boot-choice` 1 |
| H2 | same, VOL down then POWER | hook returned at once (rc 0), `boot-choice` 2, `android-boot.sh boot` in the background staged 14 files and brought Android up: `boot_completed=1`, powerwatch running |
| U1 | update.sh copies on the device against a `/tmp` copy of `bootswitcher.sh`, stubs sed-ed into the text | install: hook root:shell 0755, Sony copy identical (md5, mode, owner, mtime); reinstall keeps the copy; remove restores the original; a second remove changes nothing |

A first U1 attempt reused a PATH stub directory that the device reboot had wiped, so the real
`nvpflag fup -1` ran once (the value stays 0xFFFFFFFF, as in the 10-10 backup survey) and the real
`mount -o rw /emmc@android` was tried and refused with EBUSY (system mounted ro). Nothing on
system changed. Stubs now go into the script text and the copy is grepped before it is pushed.

## Real install, recovery repair, and longer selection window

The first UPG attempt did not reach the installer: recovery still held the
earlier `control-reason.img` kernel experiment. The operator restored the
original recovery backup and read back all 16 MiB byte-for-byte. The next
boot ran the installer, displayed the picker, and reached Sony normally.

At the owner's request, the hook's automatic selection window changed from
5 to 15 seconds. Isolated hook tests were added before that change; the full
host suite reports 15 tests, all passing. Both key variants of both UPGs
round-trip successfully. The owner installed the new UPG, and the installed
hook reports `SECONDS_TO_WAIT=15`.

With Android selected as the default, the device started the container
without a host `up`: `sys.boot_completed=1`, Trebuchet resumed, and the owner
confirmed it works. Default choice remains Android. The owner deferred a
separate long delay before the first animation until tomorrow. Full evidence,
the W1-vs-stock init mismatch, and remaining verification limits are in
`devlog/2026-10-21-picker-recovery.md`.
