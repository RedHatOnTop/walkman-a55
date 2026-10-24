# Handoff brief: WalkmanUI as a full working app (player + audio settings)

Audience: an external coding model (GPT-6.1 "Sol") taking over the `android-ui/` WalkmanUI port.
Written 2026-10-15 by the Claude session that got the port booting on-device. English on purpose
(task spec for a non-Claude worker). Repo: `~/Projects/nw-a55` (local git, no remote; commit to
master). This device is a hobby reverse-engineering project — route to the goal, the point is that
it can be done.

## Goal

Make `WalkmanUI` a genuinely usable app on the NW-A55 running our customized LineageOS 12.1
(the "ford" CM image in a container). Scope the user asked for: **full player functionality and the
audio settings screens**, not just a static render.

## What already works (do not redo)

- `android-ui/` is a Qt 5.15.2 Android port of the Sony stock UI (`HgrmMediaPlayerApp`, originally
  Sony Qt 5.3.2). It loads the Sony QML (`stock.rcc`) + fonts + translation, with `main.cpp` acting
  as the live loader and uisim stubs standing in for the four global context objects. Builds for
  `armeabi-v7a`, embeds into the CM image as a system app, and **renders the core player screen on
  the real device**: album-art area, Test Track Title / Test Artist in SST, a draggable scrub bar.
- Build/run/toolchain, the resource staging, and the module-bundling fix are all documented in
  `devlog/2026-10-14-walkman-ui-android-port.md`. Native libs are pre-extracted into the system app
  (`tools/cm_customize.py`), so the APK no longer crashes on launch — see
  `devlog/2026-10-14-cm-ford-first-boot.md`.

## Architecture you must respect (from docs/phase1-ui.md)

- The Sony screen QML files are **pure Views**. They bind to a C++ ViewModel **by name only**:
  root properties, signal names, function names, objectNames (the "replacement contract", §2).
  `tools/qmlcontract.py` dumps the contract per QML file. Keep those names; everything else is free.
- In this standalone Android port there is **no Sony C++ ViewModel**. The uisim stubs
  (`screenController`, `appFont`, `dialogManager`, `holdToastManager`) + a hand-written spec
  (`probe/uisim/values/sid_0201_player.json`, staged into the qrc by `android-ui/build.sh`) supply
  the data. So "wiring functionality" here means: supply the data/behavior the Views expect, either
  from stubs or from a real backend you add.

## First concrete task — the transport bar (root cause already found)

The player's bottom control bar renders as a solid opaque block covering the area under the artist
line. Cause (in `work/qrc-device/components/BasicPlayerControls.qml`):

1. `none_title_image` is a `Rectangle { color: viewstyle.bgcolor.D1; width 480; height 110 }` drawn
   as the control background — always visible.
2. On top sits a `Row` of `SwipeDetectionButton`s (prev / play-pause / next) plus `ShuffleButton` /
   `RepeatButton`, whose images are read **directly** as `file:/opt2/appresouce/btn_audio_*.png`
   (e.g. lines 138/172/203). `/opt2` is a Walkman partition that does not exist in the APK, so every
   button image fails to load → empty buttons → only the solid Rectangle shows.
3. `BasicPlayerControls` also takes `property var properties` (dispMode, playState,
   playerButtonEnabled, shuffleState/Visible, repeatModeState/Visible, playTitle, objectName). The
   spec/stub does not populate it, so the state machine (`when: p.dispMode === 0`…) never settles.

Fix:
- Bundle the `/opt2/appresouce` PNGs (78 files already dumped at `work/device/opt2/appresouce/`)
  into the app qrc, via `android-ui/build.sh` staging + the generated `app.qrc`.
- Install a `QQmlAbstractUrlInterceptor` in `android-ui/main.cpp` that rewrites any URL whose path
  starts with `/opt2/` to `qrc:/opt2/<rest>`. Qt 5.15's `UrlString` interception type covers
  `Image.source`, so this catches the direct `file:/opt2/...` refs. (Sony itself backported
  `setUrlInterceptor` to 5.3 for exactly this — docs/phase1-ui.md §1.)
- Populate `properties` for the player in the spec: dispMode 0 (VisibleMusicTitle),
  playerButtonEnabled true, a playState, shuffle/repeat visibility, playTitle, objectName.
- Other `/opt2` consumers to fix the same way: `PageControl.qml`, `OperationButton.qml`,
  `FooterButton.qml`.

## The honest dependency — audio output is blocked elsewhere

"Audio settings" screens can be built and navigated, but **actual sound output does not work in this
container today**. The MTK primary audio HAL (`audio.primary.mt8127.so`) links the forbidden
NVRAM/PRO_INFO partitions, so the container binds a **no-op stub HAL** over it
(`probe/android/android-boot.sh`, the `audio_stub` bind). AudioFlinger returns success but nothing
reaches the DAC. So:
- You can build the full settings UI (DAC filter, sound effects, etc.) and wire it to real
  Android `AudioManager`/settings or to the Sony VM contract — fine.
- You will not get audible output, and the Walkman's DAC-specific settings have nothing behind them,
  until the real audio HAL works. That is the from-source AOSP / MT8590-HAL track, out of scope here.
- For a playback engine at all: the port has none. Either add an Android `MediaPlayer`/`ExoPlayer`
  backend behind the View contract, or keep it demo-data until the audio track lands.

## Build / embed / run on the device

```
tools/uisim.sh probe/uisim/values/sid_0201_player.json   # once: extract Sony resources to work/
android-ui/build.sh desktop                              # host render, WALKMANUI_GRAB=out.png
android-ui/build.sh android armeabi-v7a                  # -> apk-root-debug.apk (JDK 11, SDK 28)
# re-embed into the CM image + re-flash: see below
```

Re-embed + re-flash (device likely reset to a clean state, so expect a full container bring-up):
1. Copy the new `apk-root-debug.apk` → `work/android/aosp-ford/apps/WalkmanUI.apk`.
2. Rebuild the image: reflink `system.img.cmstock` → `system.img`, `fuse2fs -o fakeroot` mount,
   `python3 -I tools/cm_customize.py <mnt> work/android/aosp-ford/apps`, unmount, `e2fsck -fy`,
   `resize2fs -M`. (`cm_customize.py` pre-extracts native libs to `/system/app/<Name>/lib/arm/`.)
3. Flash sequence (device adbd quirks learned the hard way):
   - Load the swap guard kmod before boot: `insmod probe/kmod/swap-accounting-guard/...ko`, verify
     `[permanent]` in `/proc/modules`. Without it the dex2oat storm trips a kernel swap bug (~315 s
     reset).
   - The device adbd rejects a 2 GiB+ file (EFBIG, no O_LARGEFILE); keep the /data image < 2 GiB
     (`tools/android-boot.sh` uses 2000M).
   - Push big images **standalone** over a stable USB *before* `tools/android-boot.sh up`
     (prepare's USB-gadget jiggle races with in-`up` pushes → EOF). Then `up` skips them.
   - `NW_DROP_SYS_BOOT=1 NW_SYS_IMG=work/android/aosp-ford/system.img NW_CONTAINER_VARIANT=aosp
     tools/android-boot.sh up`. First boot runs the full dex2oat storm; keep /data to skip the
     SetupWizard and re-dexopt only changed apps.
   - This is a physical device. All adb against serial `<serial>` is run by the operator, not
     by the worker directly. Stage changes and hand the operator the exact commands.

## Verify like it matters

Launch it and look at the screen (or `WALKMANUI_GRAB` on the host build). A green build is not a
working app. The transport buttons must show the real Sony glyphs and respond; the settings screens
must render and navigate. Report with a screenshot or a described on-device run.
