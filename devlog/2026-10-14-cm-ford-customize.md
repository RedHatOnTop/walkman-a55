# Customizing the cm-12.1 ford image: debloat + embed WalkmanUI/F-Droid/Firefox

2026-10-14. Goal (user): pre-package the Walkman UI app into the CyanogenMod image and debloat
it, toward an ultra-minimal set — no Google apps, a lightweight app store (F-Droid), a modern-
engine browser, all on the NW-A55's 456 MiB RAM. This is a filesystem edit of the already
acquired/verified `work/android/aosp-ford/system.img` (cm-12.1, 1200 MiB ext4). Device-free; the
first CM boot that would actually exercise it is still pending on the physical Walkman.

## Result (filesystem-verified, NOT boot-verified)

`tools/cm_customize.py <mountpoint> <apps_dir>` run against the loop-mounted (rw) image:

- **Debloated 30 dirs from `/system/app`** — 11 wallpaper/dream packages, plus Email, Exchange2,
  Calendar, Camera2, Calculator, DeskClock, SoundRecorder, Terminal, PrintSpooler, PicoTts,
  HTMLViewer, Gallery2, CMFileManager, Profiles, LockClock, Development, WAPPushManager,
  privacy-browser, Eleven. `/system/app` went **41 → 15** dirs. (Full list: the `REMOVE_APP`
  array in `tools/cm_customize.py`.)
- **Embedded 3 system apps** at `/system/app/<Name>/<Name>.apk` (dir 0755, apk 0644):
  - `WalkmanUI.apk` — 46 MiB, armeabi-v7a (the Qt 5.15 Sony-UI port, `android-ui/`)
  - `FDroid.apk` — 11 MiB
  - `Firefox.apk` — 109 MiB, Fenix 143.0.4 armeabi-v7a
- `/system` used: **425 → 532 MiB** (bloat removed is small; the three APKs add ~166 MiB).
- `e2fsck -fy` clean, then `resize2fs -M` shrank the **image file 1.2 GiB → 562 MiB** (the stock
  CM image had ~775 MiB of slack). It is loop-mounted read-only at boot, and `push_once`
  re-pushes on a size change, so a smaller image is fine.
- Pristine CM backup kept: `work/android/aosp-ford/system.img.cmstock` (1.2 GiB reflink). To
  boot stock CM as a baseline, point `NW_SYS_IMG` at that file instead.

Verified by re-mounting read-only and listing `/system/app`: all three embedded APKs present, none
of the debloated dirs present, count 15. This is a **filesystem** check — nothing here has been
booted yet.

## Decisions taken (reversible; adjust on your word)

- **Stayed on cm-12.1 (Android 5.1), did not pivot to 7.1.** Firefox 143 (Sept 2025, current
  Gecko) still ships for "Android 7 and earlier"; Mozilla only raises the floor to 8.0 at FF 144
  (blog.mozilla.org/futurereleases/2025/09/15). 5.1 is lighter for 456 MiB, and Chrome is frozen
  ~2022 on 5.1, so Firefox is the way to a current engine here.
- **MicroG skipped for now.** F-Droid and Firefox need no GMS; GmsCore is a standing RAM cost and
  wants a signature-spoofing framework patch. The image is deodexed, so NanoDroid-patcher can add
  sig-spoof + MicroG later if a GMS-dependent app ever shows up.
- **Debloat is a boot-safe first pass.** Kept every content provider, SystemUI, Settings, the
  launcher (Trebuchet), Bluetooth, LatinIME, the webview, PackageInstaller and the whole telephony
  cluster until a CM first boot is confirmed. A more aggressive second pass (telephony, the CM
  theme engine, SetupWizard) waits for that boot.

## Caveats to carry into the first boot

- Not boot-tested. Recommend booting the pristine `system.img.cmstock` first as a baseline, then
  the customized image.
- The three embedded APKs are large system apps on a **not-dexpreopted** ROM: their dex compiles
  and native libs extract at first boot, on top of the full-ROM dex2oat storm. The swap-accounting
  guard kmod is the mitigation; expect a slow first boot regardless.
- Firefox on 456 MiB RAM is the real constraint, not disk. It may be unusable under memory
  pressure even once it installs — that is the open question the first boot answers.
- WalkmanUI is installed **alongside** Trebuchet, not yet as the home launcher. Making it the
  launcher (or dropping Trebuchet) is a later step, after the UI is confirmed to run on-device.

## Reproduce

```
# image already at work/android/aosp-ford/system.img (see 2026-10-14-aosp-ford-acquired-verified.md)
# APKs staged in work/android/aosp-ford/apps/ (gitignored)
sudo mount -o loop work/android/aosp-ford/system.img /mnt/x
sudo python3 -I tools/cm_customize.py /mnt/x work/android/aosp-ford/apps
sudo umount /mnt/x
e2fsck -fy work/android/aosp-ford/system.img && resize2fs -M work/android/aosp-ford/system.img
```

See [[nw-a55-aosp-ford-image]], docs/phase1-ui.md, and the android-ui port devlog
(2026-10-14-walkman-ui-android-port.md).
