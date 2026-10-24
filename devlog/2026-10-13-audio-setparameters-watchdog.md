# Audio HAL is the root of the unusable UI — `setParameters` wedges system_server

2026-10-13, after the user reported the panel "딱히 작동을 안함" following the ui-sounds overlay.

## What the evidence settled

The earlier "audio boot wedge CLEARED" was only half true. The ui-sounds overlay
(empty RAM dir over `/system/media/audio/ui`) fixed the `loadSoundEffects` ->
`SoundPool.load` -> decode path, and the framework did finish booting once. But the
boot is **not stable**, and the real root is the same masked audio HAL reached by a
different call.

Two read-only probes (gated, no device writes):

- `work/android/imp-drm-systemui-20261013T125643Z/` — the drm+SystemUI bundle. Its
  identity gate STOPPED before any write because the pinned `system_server` pid 16953
  was gone (`/proc/16953/comm: No such file`). boot_id / init pid 16081 / starttime
  2200997 all unchanged, so no kernel reset — only the userspace `system_server` had
  been replaced. Nothing mutated. The gate did its job and kept us off the wrong (drm)
  target.
- `work/android/imp-ss-stability-20261013T130437Z/` — the read-only ss re-probe.

### `system_server` restarts, it is not a tight loop

- t0 and t+65s both: `zygote` pid 26372 starttime 2474422, `system_server` pid 26383
  starttime 2474786 (ppid 26372). Same pid AND starttime at both samples -> the current
  generation is stable across one TIME_TICK; zygote stable beside it; init still 16081.
- ss 26383 was created at 08:47:12 and logs through 09:13:25 — alive ~26 min, not a 60 s
  loop. The restart from 16953 -> 26383 happened once, triggered by an event, not on a timer.

### The watchdog kill names the cause (`70-logcat.out`)

```
*** WATCHDOG KILLING SYSTEM PROCESS: Blocked in handler on main thread (main)
main thread stack trace:
    at android.media.AudioSystem.setParameters(Native Method)
    at android.media.AudioService$AudioServiceBroadcastReceiver.onReceive(AudioService.java:5860)
    at android.app.LoadedApk$ReceiverDispatcher$Args.run(LoadedApk.java:902)
    at android.os.Handler.handleCallback(Handler.java:739)
    ... Looper.loop ... com.android.server.SystemServer.run(SystemServer.java:289)
```

A `SCREEN_OFF` broadcast reached `AudioService.onReceive`, which called
`AudioSystem.setParameters` (native). That call never returned -> ss main thread blocked
> 60 s -> Watchdog killed ss 16953 -> runtime restart (zygote 26372, ss 26383). The
`BroadcastQueue: Timeout of broadcast ... SCREEN_OFF ... started 10970ms ago` line sits
right beside it.

### The restarted generation is stuck in its own boot

ss 26383 emits **no** `boot_progress_ams_ready` / `boot_progress_enable_screen` in the 27
min after it starts. `dumpsys window`: `mCurrentFocus=null mFocusedApp=null
mStatusBarLayer=-1`. `dumpsys SurfaceFlinger`: 3 visible layers — `FocusedStackFrame` + 2
`DimLayer`, no firelauncher layer, no StatusBar layer. So 26383 is alive but hung in early
`SystemServer` init (below the Watchdog arm point) on the same audio path. That is the
user's "작동 안함".

The `drm.drmManager` ServiceManager spin is `mediaserver` (16093 tid 18073) only and is
NOT the watchdog cause — keeping drmserver unstarted was correct.

Caveat on `71-counts.txt`: the `ss_16953_am_kill=226 / am_proc_died=147` counts are lines
that merely *contain* "16953"; 16953 is the AMS **subject** killing other apps, not ss
dying 226 times. Not a death count.

## Root mechanism and the fix

`probe/android/android-boot.sh` `container()` masked `audio.primary.mt8127.so` by binding
`/dev/null` over it (the MTK HAL links `libnvram.so` and needs NVRAM/PRO_INFO, which are on
the forbidden list). The intent was "dlopen fails, AudioFlinger publishes anyway". But with
**no** primary HAL loaded, `AudioSystem.setParameters` blocks forever instead of returning.

The image already ships the AOSP **stub** primary HAL at
`/system/lib/hw/audio.primary.default.so` (5316 bytes, a no-op that returns 0 and touches no
hardware — confirmed by `debugfs -R "ls -l /lib/hw" system.img`). Fix: bind that stub over
the `mt8127` name (the name `ro.board.platform=mt8127` makes `hw_get_module` look for first)
instead of `/dev/null`. dlopen then succeeds, AudioFlinger gets a working primary output, and
every `AudioSystem.*` call returns at once.

Applied to `probe/android/android-boot.sh` (host source), `bash -n` clean,
sha256 `d02c41072d27745f06a85bd2b0b91295e1a5b7e98f1b81d97ccb9889c899adc4`. ui-sounds and idc
overlays unchanged; `down()` already umounts the `mt8127` path so it needs no change. The fix
is a bind-mount at `container()` time — RAM-only, reversible, and it does not write
`android-data.img`, so the existing focused `.bak` stays the rollback.

AudioFlinger only dlopens the HAL at startup, so this needs a gated `down` -> `up` cycle to
take effect; a live bind on the running container would not reload it.

## Verify bar for the up pass

- AudioFlinger log shows it loaded a primary HAL with no dlopen failure.
- A fresh boot reaches `boot_progress_ams_ready` + `boot_progress_enable_screen` +
  `am_on_resume_called` launcher.
- Over >= 4 min: no `WATCHDOG KILLING`, no ss `am_proc_died`, no main-thread block in
  `AudioSystem.setParameters`.
- `dumpsys SurfaceFlinger`: firelauncher + StatusBar layers visible; `dumpsys window`
  `mCurrentFocus` non-null, `mStatusBarLayer != -1`.
- Then the physical panel, checked by the user.

## Result — gated down/up applied, audio fix VERIFIED (2026-10-13 later)

Stages A/B/C ran gated (rundirs `work/android/imp-audio-stub-{push,down,up}-20261013T13*`). Stage A
pushed the edited helper and the pull-back sha matched `d02c4107…`. Stage B's one `down` tore the
container down clean (all binds gone, boot_id/guard/zram/mali unchanged, old init 16081 gone). Stage
C's one `up` brought it back with the stub bound, boot_id still `10415f50`.

Verified from `imp-audio-stub-up-20261013T134312Z/99-verdict.txt` + raw:

- `stub_bind=PASS` — `/proc/<init>/mountinfo`: the `audio.primary.mt8127.so` mountpoint's root is
  `/lib/hw/audio.primary.default.so`, option `ro`, not `/null`.
- AudioFlinger: `loadHwModule() Loaded primary audio interface from Default audio HW HAL (audio)
  handle 1` — the stub loaded as the primary interface. (The verdict's `hal_load=FAIL` is a false
  positive: its two "dlopen failed" lines are `libemoji.so`, not audio.)
- `fresh_boot=PASS` — `boot_progress_ams_ready` + `boot_progress_enable_screen` both logged by the
  NEW `system_server` 29886, plus `am_on_resume_called com.amazon.firelauncher.Launcher`.
- `ss_stayed=PASS` — ss 29886 same pid+starttime at t0 and t+270 s, `death_lines=0`.
- `watchdog_window=PASS` over 288 s — `WATCHDOG KILLING`=0 and `AudioSystem.setParameters`=0 in both
  logs **even though a real `SCREEN_OFF` landed** (`screen_off_lines=1`): the exact trigger that
  killed ss 16953 was survived. The `getprop=FAIL` row is a capture glitch (boot could not complete
  without `ro.product.config.type`).

So the masked-audio-HAL / system_server-watchdog wall is cleared: the container now boots to a stable
`system_server` with the launcher drawn.

### Remaining wall — SystemUI, independent of audio

The later window still has `ANR in com.android.systemui` x3 and `TIME_TICK` timeouts x6 (WATCHDOG=0,
so ss survives — SystemUI alone is killed/restarted; ps shows two systemui pids, 32605 S and 30521
D). `mCurrentFocus=KeyguardScrim`, `mFocusedApp` is the firelauncher Launcher behind it;
`mStatusBarLayer=151000` exists but no StatusBar layer is visible in SurfaceFlinger (firelauncher IS
visible). SystemUI is ANR-ing so it cannot dismiss the keyguard scrim. Next: a read-only diagnosis of
the SystemUI TIME_TICK ANR main-thread block (the same method used on system_server).
