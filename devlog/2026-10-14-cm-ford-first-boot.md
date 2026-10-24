# First device boot of the customized cm-12.1 ford image on the NW-A55

2026-10-14. Booted the customized CM 12.1 image (debloat + WalkmanUI/F-Droid/Firefox, see
2026-10-14-cm-ford-customize.md) on the physical Walkman via the container path
(`tools/android-boot.sh up`, `NW_CONTAINER_VARIANT=aosp`). Operator present; adb driven from the
host, verification from the screen.

## Result: CM boots to the home screen. The three embedded apps all failed; fixed below.

**Track-A core is proven on-device.** The image booted all the way through: LineageOS boot
animation (smooth — Mali r4p0 + SurfaceFlinger GPU compositing working, `libEGL_mali.so` /
`libGLESv2_mali.so` loaded), zygote preload, `system_server` up, the full dex2oat storm (framework
jars then "Optimizing app X of 44" — debloat cut the stock ~74 to 44), SetupWizard passed, and the
Trebuchet home screen. The swap-accounting guard held: no ~315 s swap reset through the whole
dexopt storm. `/dev/loop0` = our 532 MiB system (ro), `/dev/loop1` = the fresh 2000 MiB /data (rw).

**The embedded apps failed — root causes confirmed from the APK manifests (aapt), not guessed:**

| app | minSdk | extractNativeLibs | observed | cause |
|-----|--------|-------------------|----------|-------|
| WalkmanUI | 21 | **true** | fatal error on launch | 52 Qt `.so` not unpacked: extractNativeLibs=true + ro /system |
| Firefox 143 | 21 | **true** | crash on launch | 19 Gecko `.so`, same ro-extraction failure |
| F-Droid 2.0.1 | **24** | false | not in launcher | minSdk 24 (Android 7.0) > our API 22 → install refused |

The customization defect is mine: `cm_customize.py` copied each `<Name>.apk` into `/system/app` but
did not pre-extract its native libs. On a read-only /system an `extractNativeLibs=true` app cannot
unpack `lib/armeabi-v7a/*.so` at runtime and dies with UnsatisfiedLinkError. AOSP pre-extracts a
system app's libs to `<codePath>/lib/<arch>/`; confirmed on this very image — `webview`,
`Bluetooth`, `LatinIME` et al. all ship `lib/arm/*.so` (the arch dir for armeabi-v7a is **arm**).

## Fixes applied (image rebuilt from the pristine `system.img.cmstock`, filesystem-verified)

- `cm_customize.py` now extracts each APK's `lib/armeabi-v7a/*.so` to `/system/app/<Name>/lib/arm/`
  (0644, world-readable). Rebuilt image carries WalkmanUI 52 libs + Firefox 19 libs in `lib/arm`.
- F-Droid swapped to **1.15.6** (versionCode 1015056), the last client that supports Android 5.1
  (minSdk 22, per F-Droid's own "Running on old Android versions" doc; API 23 would be 1.23.2).
- Image is now 767 MiB (libs added ~205 MiB over the 562 MiB libs-less build). `/system/app` 15.
- Separately, `tools/android-boot.sh` fresh-/data size cut 2048M → 2000M: the device adbd opens the
  push target without O_LARGEFILE, so a 2^31-byte file is rejected with EFBIG ("remote File too
  large"). The first push hit that exactly; 2000M clears it. Proven: the 2000M image pushed and
  mounted rw as loop1.

## Also observed (expected, not a regression)

WiFi and Bluetooth do not work; `com.android.bluetooth` (Bluetooth Share) FC-loops. This is the
radio side of the Fire-7/MT8127-userland-on-Walkman/MT8590 mismatch ([[nw-a55-android-roadmap]]):
the CM image's radio HALs are for the Fire's hardware. (Earlier I wrongly claimed the NW-A55 has no
WiFi radio; it does — the chip just isn't brought up by this image's stack.) The ford image is a
proof that boot + display + GPU + dexopt + app install work; connectivity needs MT8590-specific
HALs, which belong to the from-source AOSP track.

## Open question for the re-flash

Whether Firefox 143 (a 2025 browser, targetSdk 36) actually *runs* on Android 5.1 + old Mali once
its libs are present is unverified — only re-pushing the 767 MiB image and relaunching answers it.
Re-flash keeps the existing /data so only the 3 changed packages re-dexopt. WalkmanUI ran on x86_64
Waydroid, so the lib fix is expected to carry it; Firefox is the real unknown. Pending the adb
channel (another session was using it) + operator.

## Re-flash result (2026-10-15): the lib fix lands all three apps

The CM container survived overnight on 456 MiB (still at the home screen). Re-flashed over the Sony
host adbd without a device reboot: `down`, standalone-push the 767 MiB system.img (803307520 B, no
EOF), `up`. `/data` preserved (loop1 203.6 MiB used, not fresh → SetupWizard skipped); loop0 now
735 MiB (the libs-carrying image). Second `up` needs the screen woken (backlight blanked from the
overnight idle; SF was compositing the whole time).

- **F-Droid 1.15.6** — opens normally. The minSdk swap fixed it.
- **Firefox 143** — runs and renders. The "white screen" was just a very long cold start on 456 MiB,
  not a compositor failure. So Fenix 143 does work on Android 5.1 + Mali r4p0; RAM is the cost, as
  predicted. The lib pre-extraction fixed the crash.
- **WalkmanUI** — the core player screen renders on-device (cover, Test Track Title / Test Artist in
  SST, draggable scrub bar), confirming the Qt port runs on armv7 hardware, not just x86_64 Waydroid.
  Transport controls still dead and an opaque bar covers the area below the artist line — the known
  gap from 2026-10-14-walkman-ui-android-port.md (StateButton.qml images from `/opt2/appresouce` not
  bundled + the navbar `properties` not populated), now manifesting on-device.

Net: the embedded-apps goal is met — all three are installed and launch; the one remaining defect is
the WalkmanUI transport bar (Track-B polish), not the image or the lib handling.
