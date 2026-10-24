# Android container: system-wide slowness, full screen, media keys, storage

2026-10-19, branch `android-opt`. User: WalkmanUI's proportions were off and "그냥 전체적으로
안드로이드 시스템이 느려". Choices: WalkmanUI full screen (immersive), then "DPI 좀만 더 올려봐라"
(UI elements too small). Targets set the same day: no lag in the base UI animations, at least 70% of
the stock UI's battery life with Wi-Fi/Bluetooth off, a dual-boot picker (volume moves, power or play
selects), and play/next/previous working in Android.

## Slowness: a Bluetooth crash loop

Idle, with the screen on and nothing in use, the device was busy about half the time. One 60 s window
by per-process ticks (`/proc/*/stat`): `gzip` 717 ticks, `debuggerd` 203, `surfaceflinger` 168,
`system_server` 116, `com.android.bluetooth` 83. The `gzip` came from the host.

`com.android.bluetooth` restarted 42 times in 20 minutes:

- `libbluetooth_mtk.so` waits up to 5 s (`bt_read_nvram`, 10 x 0.5 s) for `service.nvram_init` to
  read `Ready`. The container never set it.
- bluedroid's own preload wait is also 5 s. It fired first (`preload_wait_timeout (retried:0/max-retry:2)`),
  tore the vendor init down (`RX termination`) and retried. This happened twice.
- The 12 s `ENABLE_TIMEOUT` then expired, and `bte_ssr_cleanup` crashed:
  `Fatal signal 11 (SIGSEGV), code 1, fault addr 0x2`.
- With `bluetooth_on=1` the cycle repeated about every 45 s.

Each crash also went through the host. The kernel's `core_pattern` is Sony's
`|/bin/sh /system/vendor/sony/etc/hcoredump.sh /var/log`. That script runs `hdumpstate` (a 600 KB
log) and gzips the whole core to `/var/log/core......gz` (4.4 MB), overwriting it every time.

Fixes:

- `init.aosp.rc` sets `service.nvram_init Ready`. After `setprop` on the running container, the next
  enable ran all seven init commands (`The event of command 0..6 success`) and reached state 12 in
  0.4 s. libnvram has no `/data/nvram`, so the write-back logs `WriteBDAddrToNvram: Open BT NVRAM
  fails errno 13` and goes on. The address is the chip's eFUSE value `00-00-46-85-90-01`, which looks
  like a per-model placeholder.
- `pivotinit` sets `RLIMIT_CORE` to 1/1 before it execs init. Kernel 3.10 skips a piped core when
  the soft limit is exactly 1. Verified with SIGABRT to `com.android.bluetooth`: the kernel printed
  `Process 2700(droid.bluetooth) has RLIMIT_CORE set to 1` / `Aborting core`, and no new file
  appeared in `/var/log`. App processes keep 1/1. `system_server` holds CAP_SYS_RESOURCE, so ART
  raises its own limit to 0/unlimited, and a native crash there would still reach the host handler.

With the loop gone, a 90 s idle window with nothing else running measured 42 busy ticks out of
17,925 (0.2%). The two "before" figures (52% in 60 s, 21.5% in 10 s) included the per-process
sampler or a short window, so they are not a clean pair; the per-process ticks above are the cost
that went away.

Load average stays around 9-10 at idle. That is about ten MTK kernel threads in D state (`GCPU`,
`entropy_thread`, `pmic_thread_kth`, `hang_detect`, `wdtk-*`, ...), which count toward the load
average without using the CPU.

The recovery path after an abnormal Bluetooth death is broken in CM 12.1 itself. AMS restarts the
profile services before AdapterService (`onCreate, null mAdapterService`), the enable hits
`START_TIMEOUT`, and Bluetooth stays off until the container restarts. A normal boot comes up at
state 12.

## Other measurements

- **Settings launch.** Cold start 1.3-2.0 s, warm 1.0-1.3 s. During the cold start the two cores
  were 78% busy with 2 iowait ticks, 12.8 MB read and 48 major faults, so it is bound by CPU, not by
  I/O. The governor is MTK `hotplug` (up_threshold 85, 598 MHz-1.3 GHz) with both cores online.
  Compilation is fine: there is no dex2oat filter, so everything is compiled at "speed".
- **Settings scroll.** 128 frames, average 12.9 ms (Draw+Prepare+Process+Execute), 1 frame over
  16.7 ms. HWC composes all four layers on the Mali-450.
- **mtkfb log.** `mtkfb_ioctl, info=...` is printed twice per composed frame. It is an unconditional
  `pr_info` (`drivers/misc/mediatek/videox/mt8590/mtkfb.c:2420`), so `mtkfblog:off` in
  `/d/mtkfb` does not stop it. The only console is the RAM console. Its cost was inside the noise of
  four scroll runs (SurfaceFlinger 642-748 ticks each). It does push older lines out of dmesg (the
  guard's `writes` line was gone by the time `up` grepped for it). Left as is.

## Density

`ro.sf.lcd_density=200` in `default.aosp.prop`, up from the ford build's 160. Ramdisk `ro.*` values
win over `build.prop`. Changing it at runtime (`wm density 200`) made CM soft-reboot the framework
(`Shutting down activity manager... restarting`), which crash-looped three times before it settled;
the device did not reset. The status bar is now 30 px and the navigation bar 60 px.

## WalkmanUI full screen

`window->showFullScreen()` on Android replaces `showMaximized()`. Qt 5.15 applies immersive sticky
itself and reapplies it on focus: `mLastSystemUiFlags=0x1706`, stage 480x800. CM shows its one-time
"Swipe down from the top to exit full screen" hint on the first entry.

The library top showed a 150 px empty band between the page dots and the player controls. The stock
layout already has a roomier icon geometry (row pitch 126: height 504, top margin 24) for screens
without BasicPlayerControls. Patch 0007 uses it whenever the function icon row is empty and the
screen area has the height: 24 + 504 + 31 + 20 + 10 = 589. The screen area already ends at the
controls (`ScreenController::ReconfigureScreenByWindow`). The first version also subtracted the
controls' 157 px, so it never applied; this was caught on the capture. On the 800 stage the rows sit
at 122/248/374/500 and the dots at 607 (`work/android-opt/walkmanui-full-3.png`). If the navigation
bar's slot is given back (stage 740), the check fails and the compact layout returns by itself.

## Shared storage

Every app's `/storage/emulated/0` was unusable. The container had `init.ssd.rc`'s directories and
links but not its `sdcard` service, so zygote bound a root-only, 0700 directory from the container
root into each app. WalkmanUI therefore found no music. `init.aosp.rc` now runs
`/system/bin/sdcard -u 1023 -g 1023 -l /data/media /mnt/shell/emulated` (class main). After restart:
`init.svc.sdcard=running`, `/dev/fuse /mnt/shell/emulated fuse`.

WalkmanUI then listed every track twice. FireOS's init links `/storage/sdcard0` to the emulated
storage, and `musicStorages()` took that link for an SD card. It now skips symlinks under
`/storage`. A queue saved before this fix still holds the `/storage/sdcard0/...` copies. When
playback reaches one of them, `loadCurrent` drops the track (`m_track = -1`), which ends the session
until the next play.

Test files, still in the data image: `/data/media/0/Music/tone-440.flac` and `tone-660.flac` (40 s
sine tones made with ffmpeg).

The Walkman's own music is not visible to Android. `/contents/MUSIC` is empty on this unit and the
microSD card is not mounted.

## Media keys

- **Codes.** `icx_key` reports the side buttons through ADC ladders. Sony's GPL `wm_key.c` (generic
  table) maps AD0 to `KEY_ENTER`, `KEY_RIGHT` and `KEY_LEFT` (28/106/105) and AD1 to the volume keys.
  Without a layout file, `Generic.kl` turned these into ENTER and D-pad keys.
  `probe/android/keylayout/icx_key.kl` maps 28/106/105 to `MEDIA_PLAY_PAUSE`, `MEDIA_NEXT` and
  `MEDIA_PREVIOUS`; 114/115/116 are volume and power; HOLD (35) is left out.
- **Overlay.** `up` pushes the file, and the device script binds a copy of `/system/usr/keylayout`
  plus this file over it, like the idc overlay. `dumpsys input` shows
  `KeyLayoutFile: /system/usr/keylayout/icx_key.kl`.
- **Qt mapping.** Qt maps `KEYCODE_MEDIA_PLAY_PAUSE` to `Qt::Key_MediaPlay` (`androidjniinput.cpp`).
  A Qt-side key filter could therefore start playback but never pause it. `WalkmanActivity` now sends
  the media key codes straight to the playback commands before Qt sees them.
- **Session lifetime.** Loading another track passes through `StoppedState`, so next-while-paused
  ended the session and the service. With the screen off the keys then went nowhere. Now only
  `stop()` ends the session, and a track restored at launch has an active (paused) session from
  the start.
- **Session updates.** The session was republished on every 250 ms position tick. AVRCP logged
  metadata updates at roughly 4 Hz while playing. It is now republished on state and track changes
  and on a position jump of more than 1 s; in 10 s of playback there were 0 AVRCP position updates.
- **Lock screen.** CM 12.1's keyguard passed play/pause to the session but not next/previous.
  `input keyevent 87` failed on the lock screen and worked with the screen off. The stock Walkman has
  no lock screen (HOLD does that job), so the lock screen is now off:
  `sqlite3 /data/system/locksettings.db "update locksettings set value=1 where
  name='lockscreen.disabled' and user=0"`, then a framework restart, because LockSettings caches
  the value. Settings' own chooser cannot do this: it crashes with an NPE in
  `upgradeQualityForKeyStore`, because the container runs no `keystore` service.

Verified by writing the codes to `/dev/input/event4` with `sendevent` (`keytest5.sh`). With
WalkmanUI in front, with the screen off, and after the screen came back on: play toggles 3/2/3,
previous twice steps back to the 440 Hz track, and next goes to 660 Hz. Not yet verified: that the
physical buttons send 28/106/105.

## State left on the device

- `svc power stayon usb`. Undo with `svc power stayon false`.
- Lock screen disabled in `locksettings.db`.
- `/data/local/tmp/{scrollbench,keytest*}.sh` and the two test tones.
- `settings global display_density_forced` was deleted, so the density comes from the property.
