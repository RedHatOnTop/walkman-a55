# Android optimization resumes: current ford image

2026-10-19. User: "이쯤되면 그냥 안드로이드쪽 최적화 계속하자 쿼드코어 살리는것도 일단은 연기".
The quad-core work stops at the CPU0-only panel preflight (`quad-hotplug-cpu2-cpu3`,
devlogs 2026-10-17..19). Android work continues on `android-opt`, branched from `master`
(`5625b49`).

The customized ford image (`2026-10-15-preflight-ford-fixes.md`) has not been on the device
since 10-15. It also lacked the 10-16 WalkmanUI changes and the sound effects.

## Image

- WalkmanUI rebuilt with `android-ui/build.sh android armeabi-v7a`, `BUILD_EXIT=0`. The
  `android-ui/` tree is identical to `5625b49` (empty diff against the checkout it was built
  from, `270b4f8`). APK 44,988,603 bytes, 59 armeabi-v7a libs. The audiofx host test printed
  `PASSED, 0 failure(s)`. `libwalkmanfx.so` was already current (`make: Nothing to be done`).
- `work/android/aosp-ford/system.img.pre-1019` is a reflink of the image before this change.
- `tools/cm_replace_app.py` (new) swapped `/app/WalkmanUI/WalkmanUI.apk` and
  `lib/arm/*.so` with debugfs: 59 libs where there were 52, owner kept at uid/gid 1000. Every
  written file read back equal.
- `android-ui/audiofx/embed.py` added `/lib/soundfx/libwalkmanfx.so` (22,012 bytes) and the
  walkmanfx entries in `/vendor/etc/audio_effects.conf` and `/etc/audio_effects.conf`.
- `e2fsck -fn`: exit 0, 1894/46080 files, 176876/192693 blocks. Size unchanged at
  789,270,528 bytes. md5 `5aff14b7ffde99058ccbcae314914b6c`.

Pushed 2026-10-19 by the operator with the command below: `work/android/aosp-ford/system.img: 1 file pushed ... (789270528 bytes in 85.485s)`, then `system image on device: md5 5aff14b7ffde99058ccbcae314914b6c`, so the md5 path pushed and verified on the device. `up` exited 0; `/tmp/aroot/system` mounted (722.6M, 660.8M used), Android init started, and the container kernel log shows surfaceflinger queueing overlay configs at 736 s. What the screen shows is not yet reported.

## Push

`push_once` compared sizes only. This image is the same size as before the swap, so an
older same-size image on the device would have been kept. `up` now pushes the system image
through `push_image`, which compares md5 with busybox on the device, pushes on a mismatch
and fails if the md5 still differs afterwards. The data image stays size-only; it is the
container's live `/data`. `bash -n` passes and the 8 container tests pass. The new path has
not run against the device.

Operator command:

    cd ~/Projects/nw-a55
    NW_SYS_IMG=work/android/aosp-ford/system.img NW_CONTAINER_VARIANT=aosp tools/android-boot.sh up

## After the first boot

From the 10-15 watch list: telephony-app pruning, `low_ram` (Firefox OOM), default wallpaper
and boot animation, A2DP-only Bluetooth, the audio HAL producing sound. Added: WalkmanUI
transport buttons, and the sound effects in the playing session
(`dumpsys media.audio_flinger`, see `2026-10-16-walkman-sound-effects.md`).

Then a RAM baseline before any further trimming: `/proc/meminfo` on the host side, and
`dumpsys meminfo` inside the container, idle at the home screen and during playback. The
per-process numbers set the next cuts.

## First run: hardware watchdog reset during dexopt

The operator saw the "optimizing apps" screen, then the stock Sony UI. The device had reset:
uptime 70 s afterwards, and `/proc/last_kmsg` (`work/android-opt/last_kmsg-1019a.txt`) starts
`ram console header, hw_status: 1, fiq step 0.` with no panic text. Its last lines, at
808.0-808.5 s, are surfaceflinger overlay ioctls and the backlight ramping 59 -> 10.

The operator command in this devlog skipped step 1 of `docs/ford-reflash-runbook.md`: the
swap-accounting guard was not loaded, and the device script turns zram on. That is the setup
that reset at ~102 s and ~315 s after swapon on 2026-10-12. The swap bugs are the likely
cause; this tail does not show them.

`up` now loads the guard itself before anything else reaches the device: it checks the module
against the audited sha256 `cbcf6d6f…`, pushes and insmods it when `/proc/modules` lacks it,
and stops unless the line reads `[permanent]`. A refused load ends the run with "reboot the
device before trying again", as the guard design requires.

## Second run: guard loaded, boot and memory

`up` with the guard and `NW_DROP_SYS_BOOT=1` (`work/android-opt/up-1019b.log`): the guard logged
`writes 11/11 rollback 0/0 failed 0 ... guards 22/22 required 11`. `sys.boot_completed=1` at
uptime 259 s, and the container was still up 600 s later (uptime 864 s) with zram in use. Audio:
`NW-A55 CXD3778GF primary HAL opened (mixer ok)`, `opened output stream (hw:0,0 S16_LE 48000
stereo)`, and mediaserver loaded `libwalkmanfx.so`. Bluetooth failed to enable
(`preload_wait_timeout` -> `Error enabling Bluetooth`), and `com.android.bluetooth` then died with
`Fatal signal 11 (SIGSEGV), code 1, fault addr 0x2 in tid 4078 (BluetoothAdapte)`. Not followed
up yet.

Largest PSS 25 s after boot was Firefox, which nobody had opened: `org.mozilla.firefox` 70,855 kB
+ `:crashhelper` 9,722 kB + `:tab37` 7,200 kB, started `for broadcast
org.mozilla.firefox/androidx.work.impl.background.systemalarm.RescheduleReceiver`. F-Droid (8,496
kB) started for `ConstraintProxy$BatteryChargingProxy`. `tools/android-tune.sh apply` disables
those WorkManager receivers (13 components). F-Droid's own `PackageManagerReceiver` had to go
first: every `pm disable` sent it a PACKAGE_CHANGED, it started F-Droid, and F-Droid's WorkManager
re-enabled `RescheduleReceiver` within a second.

After `down`/`up` (`work/android-opt/boot-1019c/`), neither app was running at boot. Same point,
25 s after `boot_completed`, `dumpsys meminfo` totals:

| | Free RAM | Used PSS | AnonPages (host) |
|---|---|---|---|
| Firefox and F-Droid at boot | 149,756 kB | 149,601 kB | 124,548 kB |
| receivers disabled | 177,166 kB | 129,758 kB | 77,772 kB |

The first row is the first boot of this image after an interrupted dexopt; the second booted in
6 s on a warm dalvik-cache. The difference is not only Firefox.

## WalkmanUI on the device

Starting the activity reports `TotalTime: 1554` ms, but that is the Java window (a grey
gradient). Qt loads only once the activity is visible: the cold start then runs 7-8 s at 25-45%
CPU while RSS climbs 63 -> 92 MB, and every player variant's QML loads
(`MusicPlayerDefault`, `...DigitalLevelMeter`, `...LevelMeter`, `...Spectrum`) whichever one is
selected. `dumpsys meminfo org.walkmanui` afterwards: TOTAL 70,448 kB PSS, Native Heap 50,924 kB
allocated, Unknown 40,023 kB. That is now the largest app on the device.

The window is 480x752 (`mShownFrame=[0.0,0.0][480.0,752.0]`): no Android status bar over it, a
48 px navigation bar under it. The stage is 24 px taller than the 480x728 the layouts were budgeted
for; in the library screen the extra height shows as an empty band between the page dots and the
transport row (`work/android-opt/walkmanui-6.png`).

For the captures the container's screen was set to stay on over USB (`svc power stayon usb`);
`svc power stayon false` undoes it.

## SensMe Channels

User: "그 혹시 Sensme는 어디감?", then "어 그래 그렇게하자". The library had no SensMe icon:
the port had left the screen out. Restored on `android-opt` in `153e386`, `1b133ab`, `c11b604`.

- `android-ui/library/sensme.cpp` reads the SMFMF blob (Music Center's 12 TONE ANALYSIS) and
  classifies it the way the A55's `libMediaStoreService.so` does: 86 feature bytes, the nearest
  of 48 centroids by weighted L1 over 16 STBF words, the cluster's time slot and fallback mood,
  eight mood rules, and the highlight start (word 0 of the last STSA).
- The centroids and tables are staged by `stage.py` from the device library into
  `res/sensme.bin` (6,660 bytes, md5 checked). They are firmware-derived and not committed.
- The tag reader takes the blob from MP3 ID3 `GEOB` (`application/smfmf`, description
  `USR_SMFMF` preferred), FLAC `APPLICATION` `SMFM`, and MP4 `moov/meta/ID32`. The library cache
  is now version 4, so every device rescans once.
- The library top page has the SensMe icon (page 2). The player screen is
  `SensMePlayerModel`: 14 slots in the stock order, starts the current time slot, plays each
  track from its highlight, and shows message 020194 once per session when tracks are
  unanalyzed. `playerCondition()` 4 routes the stock SwipeUp/SwipeDown transitions to it.

### Matching the device library

`work/sensme/harness/smuharness` calls the device's own classification in
`libMediaStoreService.so` on the A55 host. `tests/sensme_fuzz.py` generated blobs from a real
analysis with random edits (lengths, duplicates, truncation, feature values) and compared
channels, cluster, highlight and features with the port. The first run of 3,000 differed in
1,527. Three causes, all fixed:

- the STMO and STHF entry switches fall through: an entry also fills the slots of every later
  type until a later entry overwrites them;
- a missing cluster word gives no channels and cluster 0, not an error;
- the highlight takes the last STSA by a header-only count, then fetches that one.

After the fixes 0 of 3,000 and 0 of 6,000 differ.

The A55 host `/tmp` is a 32 MB tmpfs. The 28 MB second fuzz set filled it and the next `up`
failed (`adb: error: failed to copy ... mali_r4p0.ko ... No space left on device`).

### On the device

The library on the microSD card is 10,012 tracks. With `153e386`/`1b133ab` (cache version 3)
413 were analyzed, all MP3. SensMe opened with message 020194. Night held 84 tracks and started
at 01:48, the highlight. Midnight held one track. Shuffle All showed `1 / ----`.

With `c11b604` (cache version 4, image md5 `361028ddd87743cd6a1f116ed3625e9a`, backup
`system.img.pre-1019n`) 573 of 10,014 tracks are analyzed: 414 MP3 and 159 FLAC. Version 3 had
ignored 158 FLACs on the card that already carried an `SMFM` block. By folder: `K-Pop Hits` 60,
`Favorites` 48, `New` 31, `The Beatles` 17, the card root 2. Channel sizes from the pulled cache,
classified by the port:

| channel | v3 | v4 |
|---|---:|---:|
| Morning | 59 | 85 |
| Daytime | 174 | 219 |
| Evening | 94 | 155 |
| Night | 84 | 110 |
| Midnight | 1 | 3 |
| Active | 119 | 140 |
| Relax | 14 | 26 |
| Mellow | 76 | 100 |
| Upbeat | 90 | 153 |
| Emotional | 103 | 144 |
| Lounge | 10 | 13 |
| Dance | 51 | 67 |
| Extreme | 4 | 4 |

One analyzed track fits no channel in both. On the device, Night shows `1 / 110` and starts at
03:00 (`work/android-opt/sm-n3.png`).

After the reboot the rescan shared the card with Android's own media scanner. `top` showed
`mediaserver` (FFmpeg extractor) at 37%, `mount.exfat` at 13%, and 22% I/O wait while WalkmanUI's
low-priority scan thread waited. Both index the same 10,000 files on every boot. This is a
battery cost for goal 2.

### Walkman Manager interop

`walkman-manager` (`out/uwm-b46`) `sensme-copy` copied the analysis of `work/sensme/sm1.mp3` into
a bare FLAC (`flac_nocover_notags.flac`) and an MP3 (`mp3_cbr320_v24.mp3`) fixture. The manager
writes an `APPLICATION` `SMFM` block and an ID3 `GEOB` respectively.

- WalkmanUI's reader on the host gives mask 648 (Night, Mellow, Emotional), cluster 36, and
  highlight 141,258 ms for both.
- On the device, as `Music/uwm/uwm-sensme.{flac,mp3}` in internal storage, the cache holds the
  same 86 features and highlight 141,258 for both.

The manager does not write MP4 (`tags.rs` `write_smfmf` handles MP3 and FLAC). WalkmanUI reads
MP4 `ID32` from the fixture only.

Left on the device: `/tmp/smu/` (the harness), `/tmp/fbcap`, `/tmp/fb.raw` on the host;
`/data/media/0/Music/uwm/` and `/data/local/tmp/scan-marker-m` in the container. Deleted from
the host `/tmp` with `adb shell rm`: `/tmp/smu/fuzz1.blobs`, `fuzz1.out`, `fuzz2.blobs`,
`fuzz2.out`.

## System bars

User: "안드로이드 앱에서 네비게이션 바는 고정, 상단 상태바는 자동 숨기고 스와이프해서 소환 가능함?".
Qt 5.15's `QtActivityDelegate.setFullScreen(true)` (disassembled from `QtAndroid.jar`) sets
`HIDE_NAVIGATION | LAYOUT_* | FULLSCREEN | IMMERSIVE_STICKY` and re-applies it on focus. Since
`37bb527` it had hidden the navigation bar too. `0b20ee9` shows the window maximized again.
`setFullScreen(false)` then never runs, so Qt leaves the flags alone. `WalkmanActivity` sets
`LAYOUT_STABLE | FULLSCREEN | IMMERSIVE_STICKY` on create and on every focus gain.

On the device (image md5 `20476df4244f740a5748a3565e100b63`, backup `system.img.pre-1019o`):

- `mLastSystemUiFlags=0x1104`. The navigation bar is `WINDOW_STATE_SHOWING` and drawn black under
  the transport row (`work/android-opt/bar-0.png`). The stage is 480x752.
- A swipe down from the top edge gives `mState=WINDOW_STATE_SHOWING`,
  `mTransientBar=TRANSIENT_BAR_SHOWING`, and the status bar over the Sony header
  (`bar-u1.png`).
- A few seconds later it reads `WINDOW_STATE_HIDDEN` / `TRANSIENT_BAR_NONE` and the header is
  back (`bar-u3.png`).

The first four `input swipe` tries left the bar at `TRANSIENT_BAR_NONE`. They ran while the app
was stuck in the ANR below, or just after it. Once the app responded again, five of six
`input swipe 240 2 240 300 <150-300 ms>` tries showed the bar. A real touch path works too.
Protocol A on `/dev/input/event1` (`himax-hx8526-icx`, 960x1600, twice the screen) uses
`sendevent` `1 330 1`, then per point `3 53 X`, `3 54 Y`, `3 48 8`, `0 2 0`, `0 0 0`, then
`1 330 0`.

### ANR after the reboot

`ANR in org.walkmanui`, `Reason: executing service org.walkmanui/.PlaybackService`. The activity
started at 21:22:55 (device clock). `Timeout executing service` came at 21:24:08, with
`PlaybackService.onCreate` -> `initializeSession` -> `applySession` on the main thread. The
process logged 533 major faults in the ANR window. Firefox hit an ANR on its `BOOT_COMPLETED`
receiver at 21:25:03. Android's media scanner was reading the card at the same time
(`mediaserver` 37%). While the app window did not consume input, the dispatcher held every
touch, the system gesture listener included. Traces: `work/android-opt/anr-walkmanui-1019o.txt`,
`anr-traces-1019o.txt`. Not fixed. This belongs to goal 1: the boot storm (media scanner,
Firefox and F-Droid WorkManager at boot) and the service's main-thread work.

## AudioFX tile in Quick Settings

User: "QS에 아직 AUdioFX가 남아있길래 말이야". The AudioFX app went in debloat pass 2. It is absent
from the image (`/priv-app/AudioFX`: `File not found by ext2_lookup`) and `pm path
org.cyanogenmod.audiofx` prints nothing. The tile is SystemUI's `visualizer` tile
(`quick_settings_visualizer_label` = "AudioFX"). CM puts it in
`android:string/config_defaultQuickSettingsTiles` =
`wifi,bt,cell,airplane,rotation,flashlight,location,cast,visualizer,hotspot,live_display`.
`QSUtils.filterTiles` drops cell, hotspot, flashlight and the like on this device, but never
`visualizer`. The device had that whole list written into `sysui_qs_tiles`.

Fixed in the settings database, not in the signed `framework-res.apk`:

    tools/android-boot.sh sh 'settings put secure sysui_qs_tiles wifi,bt,cell,airplane,rotation,flashlight,location,cast,hotspot,live_display'

Quick Settings now shows Wi-Fi, Bluetooth, Airplane mode, Portrait, Location, Cast screen and
the LiveDisplay tile ("Off") (`work/android-opt/qs-1.png`). It lives in the container's `/data`.
A fresh data image would bring the tile back.

Before this, at 22:49:24 (host clock) the A55 dropped off USB (`usb 3-4: USB disconnect`) and
did not enumerate again. The user forced it off; the next boot reports
`androidboot.bootreason=power_key`. A cold power-off keeps no `/proc/last_kmsg`, so the cause is
unknown.

## Power off and restart from Android

User: "참고로 전원버튼 꾹눌러서 나오는 버튼으로 정상종료는 안 먹힘". Android's power menu ends in
`sys.powerctl` (`shutdown` or `reboot,<reason>`). AOSP's `init.rc` hands that to `powerctl`.
`init.aosp.rc` had no such trigger, and init runs without `CAP_SYS_BOOT` (`NW_DROP_SYS_BOOT=1`).
The host's own power services (`hagodaemon PowerService`, `PowerMgrServiceFw`) are stopped by
`prepare`. So Android shut its services down and stopped there.

`7c67f71`:

- `init.aosp.rc`: `on property:sys.powerctl=*` writes the value to `/data/misc/powerctl`.
- `android-boot.sh powerwatch` runs on the host and polls that file once a second. On a request
  it logs `android-boot: sys.powerctl=<value>` to `/dev/kmsg` and runs `down`. Then it runs
  `sync`, and the host's toolbox `reboot` for `reboot*` or `reboot -p` otherwise. A reboot reason
  is dropped. The watcher exits when the container's init does.
- `up` starts the watcher with `setsid` after `btdev`. The first device run lost it: adb's
  shell exits as soon as `up` returns, and the hangup reached the child before `setsid` had run.
  Reproduced: started that way from a shell that exits at once, the watcher was gone 3 s later.
  With `trap '' 1` first it stayed. This ash rejects `trap '' HUP` (`trap: HUP: bad trap`).

Device:

- `svc power reboot` in the container at 23:19:00 (host clock). The joined shell came back
  `Killed`: `down` killed the container's processes. The A55 was back on adb at 23:20:24
  with uptime 12.9 s and `androidboot.bootreason=wdt_by_pass_pwk`, the MTK software reboot.
  `/proc/last_kmsg` held 42 KB of garbage this time, so the kmsg line could not be read back.
- After the next `up` (log `work/android-opt/up-1019r.log`), the watcher (pid 3156) is alive
  after `up` returned and after `sys.boot_completed`. `sysui_qs_tiles` kept its new value
  through the reboot.
- Power off from the power menu needs the operator: the device cannot be switched back on from
  here. Not yet run.

Either way the device next boots into the stock Sony UI. Android comes back only with `up` from
the host until the dual-boot picker (goal 3) exists.

## Slow after boot: the boot storm

User: "근데 기계 왤케 느리냐". Measured 3 to 4 minutes after the container's
`sys.boot_completed`:

- `/proc/loadavg` 21.09 on two online cores (cpu2/3 power-gated). MemFree 19 MB, 155 MB in zram
  swap, 310 swap-ins and 294 major faults per 10 s. `top` over 2 s: 38% idle, 7% I/O wait.
- Android's media scanner was indexing the whole card again: `mediaserver` (FFmpeg extractor)
  39%, `mount.exfat` and `sdcard` (FUSE) busy, `MediaScannerService` running. WalkmanUI keeps its
  own library, so all of this is duplicate work.
- Firefox started on `BOOT_COMPLETED` (WorkManager `RescheduleReceiver`), spawned
  `crashhelper` and a tab process, then hit an ANR. The broadcast timed out after 81 s
  (`Timeout of broadcast ... BOOT_COMPLETED ... started 81270ms ago`), which held back every
  later boot receiver.
- F-Droid started from `nearby.UsbDeviceMediaMountedReceiver` when the card mounted.

Changes, in the container's `/data` (they survive container restarts, not a new data image):

    appops set org.mozilla.firefox BOOT_COMPLETED ignore     # CM Privacy Guard op
    appops set org.fdroid.fdroid BOOT_COMPLETED ignore
    pm disable com.android.providers.media/.MediaScannerReceiver
    pm disable org.fdroid.fdroid/.nearby.UsbDeviceMediaMountedReceiver

`pm disable` alone would not hold for the WorkManager receivers. WorkManager re-enables its own
components when the app schedules work, and the app-op cannot be changed by the app. With the
scanner's receiver disabled, nothing indexes media into MediaStore automatically. WalkmanUI does
not use MediaStore.

Same point after a container restart (3 min after boot): load 9.44, CPU 94% idle, 0% I/O wait,
17 MB in swap, 0 swap-ins and 0 major faults per 10 s. No Firefox, F-Droid or scanner process, and no
broadcast timeout or ANR. The 1-minute load stays high with the CPU idle, which suggests tasks
in uninterruptible sleep; not examined.

## F-Droid out, File Manager in

User: "그냥 F-Droid 제거하고 파일 탐색기(매니저)나 넣자". CM 12.1's own File Manager
(`com.cyanogenmod.filemanager` 3.0.0, `/app/CMFileManager/CMFileManager.apk`, 2,710,685 bytes,
targetSdk 21, no native libraries) comes from `system.img.cmstock`. The first debloat had
removed it. Built on a reflink copy while a container run was still using the old image, then
swapped in:

- `tools/cm_debloat_debugfs.py` removed `/app/FDroid`. debugfs added `/app/CMFileManager`
  (0755 dir, 0644 apk, root:root). `e2fsck -fy` exit 0, then `e2fsck -fn` exit 0:
  1894/46080 files, 175377/192693 blocks. The APK read back equal.
- md5 `7f3b5f61c592b5edcd5a8dcd85b4d21a`, backup `system.img.pre-1019t`.
- `tools/cm_customize.py` keeps CMFileManager and no longer embeds F-Droid, so a rebuild from
  stock gives the same set.

On the device: `System package org.fdroid.fdroid no longer exists; wiping its data`, and
`/data/data/org.fdroid.fdroid` is gone. `pm list packages` has `com.cyanogenmod.filemanager`.
It starts in 2.2 s (`TotalTime: 2207`) with CM's welcome dialog (safe mode). It lists internal
storage, the SD card (`/storage/sdcard1`) and secure storage, and opens the card's folders
(`work/android-opt/fm-1.png` .. `fm-3.png`). F-Droid's two settings from the section above
went with the package.

## SensMe player under the navigation bar

User: "지금 스크린샷 봐봐 뭔가 좀 잘못됨". `0b20ee9` made the stage 480x752. Sony's
`SensMePlayer.qml` stacks fixed heights: `y: 72`, the 240 px channel strip, 36 px, the 18 px dots,
40 px, then `PlayerMetaDataArea` (179 px of its own). That is 585 px, against about 555 between
the header and the bottom bar at 752. The bottom bar's line ran through the artist and hid the
album (`work/android-opt/now-1.png`). The host capture `shots/sensme2-ch1.png` from 20:38 already
showed it at 752, and I missed it. `0002-fit-shorter-screens.patch` had fitted the other player
screens; SensMe came back after that patch.

`3b1a0fb`, `patches/0008-sensme-player-fits-shorter-screens.patch`: the three empty gaps (72, 36
and 40, 148 px) shrink in their stock proportions by
`gapFit = clamp(1 - (585 - height) / 148)`. The strip and the metadata keep their size. At 800
`gapFit` is 1. At 752 the gaps are 57/29/32. The gaps alone fit down to a 437 px player area,
which covers Waydroid's 674 stage.

Host (`work/wui-takeover/run.sh fit800|fit752|fit728`): title, artist and album clear the
bottom bar at 752 and 728, and 800 is unchanged. Device (image md5
`21c17528b256806fa6b885e7f02bd155`, backup `system.img.pre-1019u`): Night `1 / 110` shows
title, artist and album above the line (`work/android-opt/fit-dev-3.png`).

## Idle drain: the 20 s resume wakelock

User: "Idle 상태로 방치시 소니 UI에 비해 배터리가 지나치게 빨리 닳음". The A55 had been left in
container mode and its battery died; it came back at 9 %, charging, with `bootreason=power_key`.

Screen off on USB, the Android side looks right. `dumpsys power` shows `mWakefulness=Asleep` and
`Wake Locks: size=0`, `/sys/power/wake_lock` is empty, and the backlight is 0. libsuspend picked
early suspend, the ES handlers run, and the kernel logs `pm_autosleep_set_state() with
parameter: 3`. The only active kernel wakeup source is `bq24262_charging`, which the charger holds
for as long as the cable is in. So no suspend can be measured on a cable (`suspend_stats`
success 0, fail 0). Charging works in the container: 24 → 33 % in about seven minutes.

Sony's `libPowerService.so` writes `/sys/devices/platform/icx_pm_helper/resume_lock_cancel`.
In the kernel source (`drivers/power/icx_pm_helper.c`), `icx_pm_helper_resume` takes a
wakelock with no name for `resume_lock_ms`, `CONFIG_ICX_PM_HELPER_RESUME_LOCK_MS` = 20 000, on
every resume. Writing 1 cancels it. PowerService does that once it has handled the wake.
`prepare` stops PowerService, so in the container nothing cancels the lock. Each wake (Android's
`alarm` source fired 11 times in the first ten minutes) would then keep the SoC up for 20 s.
Android holds its own wakelocks for the work it does after a wake (`alarm`, `KeyEvents`,
`eventpoll`).

`2f25982`: `prepare` writes 1000 to `resume_lock_ms`. One second still covers the hand-off from
the waking driver. The power key must still wake the screen. That, the share of time spent
suspended, wakes per hour and the drain are for the unplugged run.

`probe/android/idle-log.sh` (`a6ef09d`, fixed in `d33aca0` and `e95432a`, extended in
`2f25982`) logs one line a minute to `/contents/idle-log.txt`. The `/contents` file survives a
power-off. Each line has boot vs monotonic clock (the gap is time suspended), the battery
uevent, the backlight, the cpu line, the userspace wakelocks, the active kernel wakeup sources,
the `suspend_stats` success/fail counts, and icx_pm_helper's `resume_count`, `spm_r12` and
`eint_sta`. It does not read `/sys/power/wakeup_count`: that read blocks while a wakeup event is
in progress, and the charger keeps one in progress. "EINT wakelock" has a space in its name,
which shifted the debugfs columns, so names are now parsed from the end.

## Bluetooth name

The adapter showed up as `Amazon Fire`. That is `BTM_DEF_LOCAL_NAME`, compiled into
`/system/lib/hw/bluetooth.default.so`. The stack uses it when `bt_config.xml` has no `Name`.
The strings around it are packed (`%s: event=%s\0Amazon Fire\0%s, %d\0`), so `NW-A50Series` (12
characters, as the user asked) cannot replace it in place. The name is set in /data instead,
under `Local/Adapter` in `/data/misc/bluedroid/bt_config.xml`:

    <N8 Tag="Name" Type="string">NW-A50Series</N8>

`dumpsys bluetooth_manager` shows `name: NW-A50Series`. A fresh /data image needs the line again.

Do not kill `com.android.bluetooth` to make the stack reread its config. The ActivityManager
brings the bound profile services back before AdapterService
(`BluetoothProfileService: onCreate, null mAdapterService`). `STARTED` then never arrives, and
the adapter loops 10 → 11 → `START_TIMEOUT` → 10 every 8.5 s. `am force-stop
com.android.bluetooth` ends the loop with BT off. `service call bluetooth_manager 8` brings it
back: on CM 12.1 code 8 is `enable()`, not `disable`. `bluedroid: enable`, `mtk_bt_init`,
`BT_open: finish`, state 12. The file was rewritten in place (`cat > file`), so it kept
`bluetooth:net_bt_stack` 0660.

## USB link lost for good, and charging stopped

User: "그냥 랜덤으로 안드로이드 쪽에서 연결 끊은거임 전기는 여전히 먹고 있었어서", then "화면 켜는것만으로
안 잡힌다니까?" and "경험상 강제 재부팅 말고 방법이 없음". On a PC cable in container mode, adb went
away (host: `usb 3-4: USB disconnect` at 17:19:33). Neither the screen nor a replug brought it
back. Only a forced reboot did.

`idle-log` had the whole sequence (233 lines, copied from the Walkman volume the PC mounts once
Sony exports `/contents` as mass storage). Charging, `bq24262_charging` was the only active
source and nothing tried to suspend. When the status turned `Full`, the charger let go. From
then on suspend failed about 240 times per 90 s (`ss=0/6789` at the end) and never succeeded.
The last line, with the cable still in, read `STATUS=Not charging CAPACITY=87`. The
forced reboot cleared `suspend_stats`, and there is no `/proc/last_kmsg` on this boot. So which
device refused is not known yet: `lf=` in the log is for the next run.

Why the link stays dead (kernel source, `drivers/usb/host/usb20`). The A55 build defines
`USB_SW_WITCH_MODE` and `WITH_USB_ENTER_SUSPEND`. `musb_bus_suspend` disconnects the port, and
`musb_bus_resume` switches it to host mode (`switch_int_to_host`, `musb_id_pin_sw_work(true)`),
which calls `musb_platform_set_vbus(1)`. In host mode `mt_usb_connect()` returns at once
(`if (... mtk_musb->is_host ...) return;`), so a replug cannot reconnect, and a VBUS output
explains the stopped charging. Sony's `libUsbDeviceConnectionService.so` and
`libUsbMgrServiceFw.so` write `/sys/devices/platform/mt_usb/mode`. Its store handler goes to
idle on 0 and to device mode, connected if a cable is in, on 1. `prepare` stops those services.
Stock reads `b_peripheral` and `otg_state` 0.

`c3aee21`, powerwatch:

- Every 0.5 s (`read`, no fork), when `/sys/class/switch/otg_state/state` is 1, it writes 0
  then 1 to `mt_usb/mode`, and logs `android-boot: usb port back to device mode`. The poll fits
  inside the 1 s resume wakelock.
- While `/sys/class/android_usb/android0/state` is `CONFIGURED` it holds the `nw_usb_host`
  wakelock, as a phone does on a PC.

Verified in the container (image md5 `21c17528b256806fa6b885e7f02bd155`). `nw_usb_host` was
held at `CONFIGURED`. Host mode was forced with `mode` 0, 2 at uptime 699.81. powerwatch logged
at 699.877, the PC saw a disconnect at 18:16:47 and a new enumeration at 18:16:48, and adb kept
answering. Afterwards: `b_peripheral`, `CONFIGURED`. Not verified yet: the same recovery after a
real suspend and resume, and a replug after an unplugged run.

## Unplugged run: suspend works, and the NETLINK abort loop

Run on 10-20, screen off, Wi-Fi off. The battery was `Full` on the cable at 18:19. It was
unplugged at 18:35:45 and plugged back in at 19:29:48 (host time), so about 54 minutes. adb came
back on the replug without a reboot. Afterwards the port read `b_peripheral` with `otg_state` 0,
and `CONFIGURED`. This settles the replug check left open above.

The logger got one line at the unplug (boot 1847.7, mono 1847.6: `Not charging`, 4045 mV,
90 %). The next line came at boot 5077.4, mono 1908.0. Its 60 s sleep runs on the monotonic
clock, which stops in suspend, so the device was awake about 60 s out of 3230 s, roughly 2 %.
`suspend_stats` read 15 successes and 22 failures (`failed_freeze` 5, `failed_suspend` 17, last
errno -16). The plugged-in part of the run had 0/0. So suspend works on battery. That line
was taken after the replug, so the run has no off-cable battery reading at the end. The logger
now samples on the boot clock instead (below).

The 100 → 90 at the unplug is how the driver reports charge. `bq24262_wmport` on this build
has no gauge (`CONFIG_BQ24262_EXTERNAL_GAUGE` is not set). It maps battery voltage through a
six-point table (`BTT_1_*` / `BTT_2_*` by model id: 3.35 V 0 %, ..., about 3.83 V 50 %,
4.10 V 100 %), linear between points, so 50–100 % is about 5.4 mV per percent. Off the
charger, `bq24262_wmport_status_read` never lets `battery_uv` (and so the capacity) rise. A
voltage dip under load therefore stays in the reading until the next charge. 4045 mV
is 90 % on that table.

The dmesg ring buffer reached back to 06:24:41 device time only. Wakes in that window came 51 s,
8.7 min and 3.3 min apart. All were SPM `wake up by EINT`, followed by the `alarm` or
`pmicAuxadc irq wakelock` source. The failures came in bursts. One example: resume finished
at 06:34:15.876, and the next attempt started 115 ms later. `icx_pm_helper_resume` takes its
lock during device resume, about 0.87 s before `suspend exit`, so a 1000 ms lock left only
~0.13 s. powerwatch had not polled yet and `otg_state` was 1. Seven attempts then aborted in a
row, each with `[WAKEUP][pm_wakeup_pending] ... active wakeup source: NETLINK` and
`PM: Some devices failed to suspend`, about 0.4 s per cycle. The loop ended when powerwatch
ran at 1886.95. The attempt right after succeeded (3.3 min).

The loop runs like this:

1. Each attempt's `musb_bus_suspend` switches the port from host to device mode
   (`musb_id_pin_sw_work(false)`).
2. The `otg_state` change sends a uevent.
3. A netlink socket carries the uevent. Sockets are named after their protocol, so an
   EPOLLWAKEUP watch on one shows up as wakeup source `NETLINK`. Most likely it is healthd's
   uevent socket, which Lollipop's `healthd_register_event` adds with `EPOLLIN | EPOLLWAKEUP`.
4. The attempt aborts with -EBUSY.
5. The abort resumes the bus, `musb_bus_resume` puts the port back into host mode, and the
   next attempt starts again at step 1.

Nothing breaks the loop until userspace resets the port while awake. The plugged-in `Full`
run above failed about 240 times per 90 s, about 0.38 s per cycle. That run had no port reset,
so it matches this loop. Before `c3aee21` nothing reset the port on battery either, and the
20 s resume lock was taken again on every aborted resume. After the first wake the device
would not have slept again. That fits the original idle-drain report. It was not measured on
the old setup.

Fix, as Sony's PowerService did: release the resume lock only after the port is back.

- `prepare` sets `resume_lock_ms` to 3000. The lock starts ~0.9 s before the resume
  finishes, so 3 s leaves room for powerwatch's 0.5 s poll and caps a wake if powerwatch is
  gone.
- powerwatch reads `icx_pm_helper/resume_count`, then `otg_state`, resets the port if needed,
  and writes 1 to `resume_lock_cancel` when the count moved. Reading the count first means a
  resume between the two reads leaves the lock to the next poll, not cancelled with the port
  still in host mode.
- `idle-log.sh` sleeps 2 s at a time and samples when the boot clock has moved `SECONDS` past
  the last sample. A long suspend so produces a line within 2 s of awake time, which covers
  the screen-on before a replug.

Deployed live on 10-20 (powerwatch and the logger restarted, `resume_lock_ms` 3000). The
logger samples 60 s apart on the boot clock while plugged in. The cancel itself has not been
verified: no resume has happened since, because `nw_usb_host` is held while `CONFIGURED`.

## Overnight run: healthd's EPOLLWAKEUP keeps the loop alive

Run from 10-20 19:55:07 to 10-21 14:48:45 (host time), 18 h 53 min off the cable. The screen
was off, and Bluetooth and Wi-Fi were off (`bluetooth_on` 0, no Wi-Fi modules). The
`883e416` powerwatch and logger were live. adb came back on the replug without a reboot.

- Awake: mono moved 1544 s while boot moved 68030 s, so 2.3 % of the time.
- `suspend_stats` over the run: 192 successes and 3203 failures (`failed_freeze` 232,
  `failed_suspend` 2971, errno -16). `resume_count` moved 3395: aborted attempts count as
  well.
- Real wakes came about every 5.9 min, followed by about 17 aborted attempts each. At ~0.4 s
  per abort that is roughly 1280 s, most of the awake time.
- Battery (the driver's held-down voltage):
  - 88 %, 4033.9 mV ten minutes after the unplug.
  - 83 %, 4006.2 mV at the last screen-off sample.
  - 79 %, 3983.5 mV in the sample taken with the screen on before the replug: the load
    dip stays in the reading.

The cancel in `883e416` did not stop the loop. One case, at device time 01:53:29: a real wake
took the 3000 ms lock, and the next attempt still came 30 ms after the resume. Before that
sleep, powerwatch had reset the port inside a burst of aborts. It was frozen between the port
reset and the cancel. The count it held had been read before that sleep, and the aborts had
already moved it, so after the thaw it cancelled the new resume's lock before polling the
port. The port was in host mode again, and the loop restarted. Aborted attempts take no
lock, so powerwatch only ran when a freeze happened to abort (`eventpoll`) and gave it a
slice. The race is fixed: the count is now read again after the port check.

The abort itself comes from one watcher. Only one process in the container has an
EPOLLWAKEUP watch: `/sbin/healthd`, the Fire ramdisk's static binary. Its epoll fd 3 holds
the wake alarm timerfd, binder, and a `NETLINK_KOBJECT_UEVENT` socket (`/proc/net/netlink`
protocol 15, groups `ffffffff`), all with events `0x20000019`. Every uevent therefore takes a
wakeup event, and the musb `otg_state` flip during each attempt's device suspend aborts it.
The stock Sony userland has no such watcher.

`tools/android-container.sh` now clears the flag in the container copy. In
`healthd_register_event` (0x9170) the events word is a literal at file offset 0x11b4,
`0x20000001`, which becomes `0x00000001`. The step checks the original md5
`0861d268a64ce3868936120981a40977` and the bytes after the patch. The charger-mode input copy
of the literal (0x11a5c) stays. The wake alarm still wakes the SoC, since it is
`CLOCK_BOOTTIME_ALARM`. healthd reads it after the resume, and the alarm re-arms on that read.

Verified after a reboot and `up` (container healthd md5
`fec13c1bc92bcf35a918e405cfe31471`):

- healthd's three epoll entries read `events: 19`.
- `dumpsys batterymanager` (this build's name for the battery service) shows level 98,
  4087 mV, USB powered: healthd still feeds BatteryService.
- powerwatch is running and `resume_lock_ms` is 3000.

Not verified yet: an unplugged run with the patch.

## Status bar stuck on the panel header (open)

10-21, after the reboot for the healthd patch. User: "지금 QS가 좀 꼬인듯". With Quick
Settings closed, the 31 px status bar showed the top of the expanded header: grey background,
the cut-off battery, gear and avatar icons, and no clock. It stayed that way across a screen
off/on and a `service call statusbar 1` / `2` (expand, collapse). The re-expanded header still
showed 3:27 PM six minutes later. SystemUI itself reported the panel closed
(`mExpandedVisible=false`, `expandedHeight=0`, `NotificationPanelView` GONE).
`screencap` (SurfaceFlinger's composition) showed the same strip as `fbcap`, and the StatusBar
layer held a 480x31 buffer. So the strip was SystemUI's own buffer, not a stale overlay layer.

SystemUI draws in software here. It has no RenderThread, because `ro.config.low_ram=true`
(since `9228d7b`) turns hardware rendering off for persistent processes. Its main thread sat
idle in `epoll_wait`, and logcat had no error from it. Killing `com.android.systemui` (it
restarts as a persistent app) brought back the normal bar. Four scripted expand/collapse
cycles after that did not bring the strip back (row 10 has no header-grey pixels, against 460
in the stuck capture). Not reproduced, cause unknown. The user's own close gesture may matter.
Found 2026-10-23: Quick Settings starting an activity over the lock screen, with SystemUI in
software; fixed by drawing it on the GPU (devlog/2026-10-23-systemui-gpu.md).

## PC link lost with the cable in, again (2026-10-21)

At 20:25:07 (host clock) the A55 dropped off USB (`usb 3-4: USB disconnect`) while the container
ran WalkmanUI and a scripted swipe/tap was in progress, and did not enumerate again; the user
saw it charging but not recognized. It came back at 20:31 after a power-key boot
(`androidboot.bootreason=power_key`, no `/proc/last_kmsg`), the same shape as 10-19 22:49.
The cause is not known.

powerwatch now writes every gadget state change to `/contents/usb-log.txt` and syncs: gadget
state, `musb-hdrc.0/mode` and `vbus`, `otg_state`, `power_supply/usb/online` (`pc`), battery
status and the last 25 USB/charger lines of dmesg. When `pc` is 1 but the gadget has not been
`CONFIGURED` for 20 polls, it writes `mt_usb/mode` 0 then 1, up to three times per loss.

Check: `echo 0 > mt_usb/mode` dropped the link (`role=b_idle`, `pc=1`, PC disconnect at
20:38:15); powerwatch logged `reset 1` at boot 456.62, 15.5 s later; the gadget was
`CONFIGURED` at 457.47 and adb came back at 20:38:31. Whether the real loss looks like this is
for the next `usb-log.txt` to show.
