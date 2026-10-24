# G1-C: speech-cut + hide_error_dialogs = usable baseline; the container harness is validated (phase 1)

2026-10-14. Final Fire-OS-usability run before the AOSP pivot. Image = speech-cut only
(SpeechInteractionManager removed, msm RESTORED after the G1-B over-cut regression). Evidence
in `work/android/g1c`.

## Result
- `settings get global hide_error_dialogs` = **1** (persists in android-data.img).
- SystemUI over 150 s: **ANR 0, FATAL 0, restart 0** — stable (speech-cut keeps it ANR-free).
- `mFocusedActivity = com.amazon.firelauncher/.Launcher` — the launcher is the focused activity.
- `com.amazon.media.session.monitor` crashed **248×** in the window (it binds the absent speech
  SIM at startup and does not catch the failure), BUT with `hide_error_dialogs=1` **no
  "Application Error" dialog steals focus** — `mCurrentFocus=StatusBar`, not the
  `Application Error: com.amazon.media.session.monitor` window we saw in G1-A. The user's
  dialog-suppression instinct is validated: the crash loop is now a background/perf cost, not a
  usability wall.
- Remaining: the non-secure keyguard was showing again (`mShowingLockscreen=true`,
  `isStatusBarKeyguard=true`); the injected swipe did not dismiss it this run (input-injection
  timing is flaky), but a real finger-swipe dismisses it (confirmed earlier on the physical
  panel) and the launcher sits behind it, already focused.

## What this validates (and why it still matters after the AOSP pivot)
This is the roadmap's **phase 1 — "Fire OS validates a reusable base."** The container harness
(guard kmod → zram → mali_r4p0 → pivotinit → loopback system/data → SystemUI stable → launcher
focused, rendering on the physical Sony panel) now works end-to-end, and `hide_error_dialogs`
is a known lever for surviving a partially-broken system. **The AOSP pivot reuses this exact
harness** — Phase A swaps the Amazon `/system` for an AOSP 5.1.1 `ford` `/system` and boots it
the same way (`tools/android-boot.sh up`). The msm/avod crash churn is Amazon-specific and
disappears with an AOSP system, so it is not worth chasing on the Fire base.

Decision (user, 2026-10-14): stop polishing Fire OS, pivot to AOSP-ification. See
[[2026-10-14-aosp-feasibility]], [[nw-a55-systemui-anr-root-cause]].
