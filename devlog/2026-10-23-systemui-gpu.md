# The status bar stuck on the panel header: SystemUI in software

The bug from devlog/2026-10-19-android-opt-image.md ("Status bar stuck on the panel header",
first seen 10-21) came back on 2026-10-23 after the owner joined Wi-Fi: the 31 px status bar
showed the top of the expanded panel header (blue-grey 56,66,72 across row 10, cut-off
battery, gear and avatar) on every screen.

## While it was stuck

- SystemUI reported the panel closed (`mExpandedVisible=false`, `expandedHeight=0`,
  `NotificationPanelView` GONE), but kept queuing StatusBar frames (SurfaceFlinger's
  latency history: 1957, 2000, 2018 s uptime), each with the strip in it. So it was not a
  window that stopped drawing; the strip was what it drew.
- One swipe from the top opened the panel straight into full Quick Settings, and the
  header's clock read 11:26 PM, the time it got stuck, ten minutes later.
- Killing `com.android.systemui` cleared it, as on 10-21.

## Reproducing it

`qsprobe` counts row-10 pixels of the header colour in an `fbcap` capture (460 when stuck, 0
otherwise). From a freshly started SystemUI each time:

| path | software (as shipped) | GPU (`persist.sys.force_highendgfx=true`) |
| --- | --- | --- |
| unlocked: open Quick Settings, tap the header's gear | 0 of 3 | 0 of 3 |
| lock screen: open Quick Settings, tap the gear | 3 of 3 stuck | 0 of 3 |

Settings opened every time in both. The trigger is Quick Settings starting an activity while
the lock screen shows, which is also where an owner joins Wi-Fi after a boot (the lock screen
shows on every wake).

## Why software, and the fix

`ro.config.low_ram=true` (since 9228d7b) makes `ActivityManager.isHighEndGfx()` false, and
then Android 5.1 turns hardware rendering off for persistent processes: SystemUI and
system_server draw in software, the bug's side. `persist.sys.force_highendgfx=true` (in this
build's framework: `boot.oat` has the string) makes `isHighEndGfx()` true again without
undoing the rest of `low_ram`. Measured with it on the unit:

- SystemUI gets a RenderThread. Its GL memory in the Mali driver
  (`/sys/kernel/debug/mali_r4p0/gpu_memory`): 8.4 to 9.4 MB, 14.7 MB at most, of a
  506 MB limit.
- system_server shows only the screen-off fade's 2.6 MB there until it shows a dialog; the
  power menu draws.
- The shade: four open-expand-close cycles from a fresh SystemUI each, frame gaps under
  100 ms from the StatusBar layer's history:

  | | intervals | over 20 ms |
  | --- | --- | --- |
  | software | 61, 63, 77, 65 | 45, 19, 3, 11 |
  | GPU | 100, 81, 81, 81 | 13, 1, 6, 6 |

- Lock screen, Quick Settings, recents and the power menu drew as before.

`kit/build_image.py` appends the property to `/build.prop`; a set built with it (only
`/build.prop` differs in the image manifest) booted on the unit (`boot completed` at 70.7 s),
and the lock-screen path gave no strip twice. The unit also has the property in `/data` from
the tests, so a first boot on an empty `/data` has not been seen with it.

Unrelated, once in some seventy shell commands: an `app_process` tool (`input` or `svc`)
crashed on exit in its Binder_1 thread (SIGSEGV at an unmapped pc, tombstone_00).
