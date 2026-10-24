# A WebView browser against Firefox on the unit

Firefox (Fenix 143.0.4, the last build for Android 5 to 7) raised ANRs on two starts and was
running when the kernel reset under swap (devlog/2026-10-22-kit-first-device-run.md). This
measures a one-tab browser on the image's system WebView (81.0.4044.117) against it.

## Setup

- `android-browser/`: one activity, an address bar, a progress line, back, a VIEW filter
  for http and https. `build.sh` builds it without Gradle (javac, d8, aapt2) and signs it
  with the SDK debug key. 12.7 KB.
- The unit had no network (Wi-Fi not joined), and the host adbd has no `adb reverse`. Three
  pages saved with their requisites (`wget -p -k -H`, an Android UA, Windows-safe names, no
  video): en.m.wikipedia.org/wiki/Walkman, MDN's JavaScript page, The Guardian's
  international front page. 24 MB on `/contents/browser-test`, served from the host side
  by `probe/devtools/servedir` on 127.0.0.1:8080, which the container shares. Wikipedia's
  main stylesheet link (`load.php?…|…`) was not saved, so that page renders unstyled in both.
- Each run: both browsers force-stopped, the panel woken and unlocked, the page sent as a
  VIEW intent to the package, 45 s, then `dumpsys meminfo`, `VmSwap` of each of the
  browser's processes, the panel captured. Runs and captures are in
  `work/browser-test/run-2251/` (and earlier runs beside it).

## Result (run-2251, panel on throughout)

| | PSS + VmSwap, kB | processes | drawn after 45 s |
| --- | --- | --- | --- |
| WebView, Wikipedia | 31769 + 7208 | 1 | yes |
| WebView, MDN | 34008 + 4320 | 1 | white (drawn in run-2241) |
| WebView, Guardian | 68348 + 4116 | 1 | yes |
| Firefox, Wikipedia | 114511 + 49564 | 3 (main, tab, crashhelper) | no, white |
| Firefox, MDN | 122323 + 20544 | 3 | no, white |
| Firefox, Guardian | 139044 + 31296 | 3 | no, black |

The WebView browser held 39 to 72 MB; Firefox 143 to 170 MB, of which 21 to 50 MB had gone
to swap. No ANR in this run. In run-2241, where Firefox was started through its launcher
activity and did not get the URL, it showed its home page once and stayed white twice.

## In the image

The owner chose it over Firefox. What the browser became:

- A dark bar: the address in a rounded field, reload as a drawn open circle that turns into
  a cross while a page loads, and the load as an orange line under the bar.
- Downloads go to DownloadManager (Download folder, a notification when done), and
  `<input type=file>` opens the system picker.
- Strings in English and Korean (`res/values-ko`). The icon is an orange line globe on
  WalkmanUI's dark ground, in WalkmanUI's 44 dp circle and key shadow (`icon.py`, through
  `android-ui/launcher_icon.py`).
- `build.sh` dates `classes.dex` 1981-01-01 UTC before zipping: two builds a minute apart
  gave the same APK.

The kit no longer downloads Firefox. `make-prebuilt.sh` builds the browser, and
`prebuilt.sha256` pins it as `Browser.apk`, which `build_image.py` puts at
`/system/app/Browser` where the image's Privacy Browser was. The kit has 24 steps, and
`fire-system.img` went from 724 MB to 463 MB.

On the unit with the kit's set (install10, prebuilt6), keeping the earlier /data:

- `System package org.mozilla.firefox no longer exists; wiping its data`; the browser's
  dexopt took 0.68 s; `boot_completed` at 66 s uptime on this first boot of the new image;
- the drawer shows Browser with the globe beside WalkmanUI;
- a VIEW intent with no package goes straight to it (`am start -W`: Status ok, TotalTime
  6746 ms on its first start), and reload loads the page again.

## Open

- WebView 81 is from 2020 and does not update, like Fenix 143 did not.
- `/contents/browser-test` is left on the unit's internal storage for further runs.
