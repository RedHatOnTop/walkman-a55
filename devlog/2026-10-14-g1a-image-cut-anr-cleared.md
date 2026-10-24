# G1-A: the clean image cut clears the ANR AND the crash — SystemUI stable, launcher up

2026-10-14. Option A (user-approved): remove the speech SIM service at the image level instead
of the runtime `pm hide` that crashed system_server ([[2026-10-14-g1-speechsim-cut-confirms-anr]]).
Evidence in `work/android/g1a`.

## Host image edit (reversible)
`work/android/fire7-unpack/system.img` is a raw ext4 (read-only loop-mounted by the container,
so size is free to change). Procedure, no root:
- backup `system.img.orig` (reflink);
- `fuse2fs -o rw,fakeroot` mount, delete `/priv-app/SpeechInteractionManager` (the
  `amazon.speech.sim` host: APK + arm/ odex + lib/, 16 MB) via a Python `shutil.rmtree`,
  keeping sibling `SpeechUiTablet`;
- `e2fsck -fy` clean, then `resize2fs -M` → file size 1258291200 → 1163644928, which makes
  `tools/android-boot.sh` `push_once` re-push it (it compares byte size).
Revert = copy `system.img.orig` back.

## Result — booted the trimmed container (fresh reboot → guard → up), 150 s window
- `ANR in com.android.systemui` = **0**; `TIME_TICK ... timed out` = 0; SystemUI restart = 0.
- `FATAL EXCEPTION IN SYSTEM PROCESS` = **0**, `reportMemUsage` NPE = 0 — the pm-hide artifact
  did NOT recur; on boot PackageManager drops the now-orphaned `amazon.speech.sim` entry cleanly.
- `Connected to SIM` / `SpeechUiPresenter` / `SpeechInteractionService` = 0 — the service is
  gone, so the main-thread `MediaMetadataRetriever.setDataSource` path never runs.
- SystemUI still *attempts* the bind (keyguard/VolumePanel → `amazon.speech.simclient`) but it
  now fails with a **caught, non-fatal** `IllegalStateException: Binding was unsuccessful`
  instead of wedging the main thread. This is the clean-degradation behaviour we wanted.
- `mFocusedApp` / `mFocusedActivity` = `com.amazon.firelauncher/.Launcher`, `Hist #0 ... Launcher
  ... visible=true`, `ACTIVITY com.amazon.firelauncher/.Launcher pid=4160` — **the home activity
  is up, focused, and visible.** `screen_on=true`.

## What this means
The TIME_TICK ANR loop was the plan-A hard blocker (SystemUI never finished a frame, keyguard
scrim kept focus, UI unusable). With the speech SIM service removed at the image level, **that
blocker is gone and the launcher loads and holds focus** — cleanly, with no ANR and no
system_server crash. This is the first time the Fire UI stack comes up stable on this device.

## Remaining
- `mShowingLockscreen=true mDreamingLockscreen=true isStatusBarKeyguard=true` — the non-secure
  keyguard (fresh-boot default, `isSecure=false`; the known item in [[nw-a55-imp-prop-fix]]) sits
  in front of the launcher, which reads `sleeping=true`. Dismissing it (wake + MENU/swipe, or
  `wm dismiss-keyguard`) is the last step to an interactive home — tested next.
- The `uiautomator dump` under the keyguard was near-empty (306 B); a real hierarchy is expected
  only after the keyguard is dismissed.
- Still not shown: whether the container renders to the Walkman's physical panel (a
  `screencap` risks hanging SurfaceFlinger, so it is deferred to a safer method).

See [[2026-10-14-systemui-anr-root-cause-speechui]], [[2026-10-14-g1-speechsim-cut-confirms-anr]],
[[nw-a55-systemui-anr-root-cause]].
