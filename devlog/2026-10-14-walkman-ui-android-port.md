# Porting the Sony stock UI to an Android app (Qt 5.15 for Android)

2026-10-14. Goal (user): port the Walkman stock UI to an Android app. The stock UI is the
`HgrmMediaPlayerApp` QML (Sony Qt 5.3.2); the host `uisim` already renders its screens on Qt 5.15
with QML stand-ins for the C++ global context. This ports that exact rendering stack to **Qt 5.15
for Android**, so the real Sony QML runs in an APK. Device-free throughout (host build + Waydroid).

## Result so far

- **Host build renders the real player screen** as a standalone app (`android-ui/`, not the uisim
  harness). `build.sh desktop` + `WALKMANUI_GRAB` → the Sony `MusicPlayerDefault.qml` screen: cover
  area, scrub bar (01:01 / 3/12 / 04:05), "Test Track Title"/"Test Artist" in SST — identical to the
  uisim baseline. Validates `main.cpp` + bundling everything in the app's own qrc.
- **APK builds for x86_64**: `build.sh android x86_64` → `apk-root-debug.apk` (46 MB, BUILD
  SUCCESSFUL). Native `libwalkmanui_x86_64.so` cross-compiled, packaged by androiddeployqt+gradle.
- **Waydroid run**: install + launch verification (adb to the running Waydroid at
  192.168.240.112:5555). [status updated below]

## How it works (reuses the uisim pipeline)

`android-ui/main.cpp` is uisim's loader made live (no PNG-grab-and-exit): register the Sony
resources (`hostfix.rcc` then `stock.rcc`, copied out of the app qrc so QResource can mmap them),
add the fonts, load the translation, create each global-context stub as a context property
(`appFont`/`screenController`/`dialogManager`/`holdToastManager`), set `previewSpec`, and show
`qrc:/uisim/Preview.qml` (the FrameWork.qml stand-in) in a QQuickView. A `WALKMANUI_GRAB` env hook
grabs one frame for headless validation; the on-device run leaves it unset and stays live.

Sony resources are **never committed** (docs/phase1-ui.md §4). `build.sh` stages them from `work/`
(the extracted `stock.rcc`, the `hostfix` overlay, the stubs, the device fonts/translations) into
`build/stage/` and generates `app.qrc` at build time. Prerequisite: `tools/uisim.sh` run once so
those artifacts exist.

## Toolchain (the version-matching that had to be solved)

- Qt 5.15.2 (`android` kit incl. x86_64 + `gcc_64` host tools) via aqtinstall → `work/Qt`.
  NDK r21e (`ndk;21.4.7075529`) via sdkmanager. Existing SDK at `~/Android/sdk`.
- Qt 5.15.2 generates **gradle 5.6.4 + AGP 3.6.0 + buildTools 28.0.3**, which need **JDK <=12** and
  SDK platform/build-tools 28 — not the host's JDK 25 / SDK 35. Fix: bundled **Temurin JDK 11**
  (`work/jdk11`, used as JAVA_HOME for the gradle step), installed `build-tools;28.0.3` +
  `platforms;android-28`, and `androiddeployqt --android-platform android-28`. With the host JDK 25
  gradle dies in `org.codehaus.groovy.vmplugin.v7.Java7.<clinit>` (old Groovy vs new JVM).

## Build / run

```
tools/uisim.sh probe/uisim/values/sid_0201_player.json    # once: extract Sony resources to work/
android-ui/build.sh desktop                                # host; run with WALKMANUI_GRAB=out.png
android-ui/build.sh android x86_64                         # -> apk-root-debug.apk
adb -s 192.168.240.112:5555 install -r -g <apk> && waydroid app launch org.qtproject.example.walkmanui
```

## Known gap (polish, not core)

The bottom transport buttons (play/prev/next) render empty: `components/StateButton.qml` reads its
images from a `properties` object the stubs don't populate (the navbar/window ViewModel data, a
hand-written value per docs/phase1-ui.md §5) and the image files come from `/opt2/appresouce/`
(present at `work/device/opt2/`, 78 files, not yet bundled + path-redirected). Fixing it needs the
navbar `properties` in the stub/spec plus an `/opt2` → bundled-qrc url interception. The core screen
(cover, scrub bar, metadata, fonts) is faithful.

## Next

- Finish the Waydroid run evidence (launch + logcat + a screenshot via the app's grab hook if
  Wayland capture is unavailable here).
- Transport buttons: bundle `/opt2/appresouce` + populate the navbar `properties`.
- More screens / real FrameWork navigation (screenController transitions) beyond the single player
  screen. See [[nw-a55-walkman-re]], docs/phase1-ui.md.
