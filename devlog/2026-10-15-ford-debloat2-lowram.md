# ford image: debloat pass 2 + low-RAM / battery tuning

2026-10-15. User asked to further customize the Android image: more optimization + battery. This
is the aggressive second debloat pass the customize devlog (`2026-10-14-cm-ford-customize.md`)
queued for "after a CM first boot is confirmed" — first boot *is* confirmed (booted to Trebuchet,
`nw-a55-aosp-ford-image`). Filesystem edit of `work/android/aosp-ford/system.img`.

**Status: filesystem-verified, NOT boot-validated** (Walkman unplugged). Backups kept:
`system.img.pre-debloat2` (reflink, this pass's starting point) and `system.img.cmstock`
(pristine CM). Both deletable after an on-device boot confirms the pass.

## Removed (12 dirs, via debugfs — see mechanism below)

`/app/PacProcessor`, and from `/priv-app`: `AudioFX`, `BackupRestoreConfirmation`, `Contacts`,
`ManagedProvisioning`, `MmsService`, `SharedStorageBackup`, `Telecom`, `TeleService`,
`ThemeChooser`, `ThemesProvider`, `VpnDialogs`.

Rationale:
- **Telephony apps** — `TeleService` (com.android.phone, 4.5 MiB, a resident process), `Telecom`,
  `MmsService`. The Walkman is `ro.carrier=wifi-only` with no modem, so these are pure RAM. The
  **providers were kept** (`TelephonyProvider`) so anything that queries them does not hit a
  missing-provider crash. This is the one item to watch on device (SystemUI/Settings referencing
  the phone app) — CM handles absent telephony on wifi-only builds, but it is unvalidated here.
- **CM theme engine** — `ThemeChooser` + `ThemesProvider`. A standing RAM cost we do not use.
- **AudioFX** — CM's audio-effects app; redundant on a Walkman whose own CXD3778GF path is the
  point (see the audio HAL track).
- Enterprise/backup/proxy leftovers — `ManagedProvisioning`, `SharedStorageBackup`,
  `BackupRestoreConfirmation`, `PacProcessor`, `VpnDialogs`. `Contacts` (the app; provider kept).

## Kept deliberately (boot safety)

All providers (`ContactsProvider`, `TelephonyProvider`, `CalendarProvider`, `MediaProvider`,
`SettingsProvider`, `CMSettingsProvider`, `DownloadProvider`, `ExternalStorageProvider`),
`DefaultContainerService` (app installs), `FusedLocation` (location requests shouldn't hang; GPS
may come later), `InputDevices`, `Shell`, `SystemUI`, `Settings`, `Trebuchet`, `WallpaperCropper`.
**The setup-wizard cluster (`CyanogenSetupWizard`, `OneTimeInitializer`, `Provision`) was kept** —
the container init carries no `device_provisioned` / `user_setup_complete` write that I could find,
so removing the wizard risks a setup loop or no-launcher on a device I cannot validate; it runs
once and is not resident, so keeping it costs ~nothing at steady state.

Remaining: `/priv-app` 30 → 19 dirs.

## build.prop — low-RAM + battery (the high-leverage change)

Appended to `/build.prop` (replaced in place; inode 84, mode 0644, uid/gid 0):
```
ro.config.low_ram=true          # 456 MiB < Google's low-ram threshold; smaller heaps,
                                 # no recents thumbnails, trimmer graphics caches
persist.sys.purgeable_assets=1  # let the system drop decoded assets under pressure
wifi.supplicant_scan_interval=180  # fewer scan wakeups when disconnected (battery)
```
`ro.config.low_ram=true` is the one that matters on this device; the runtime reads it on 5.1.
Watch on device that Firefox (already the RAM-marginal app on 456 MiB) does not OOM harder under
the smaller heaps. All three are reversible one-liners. Dalvik heap sizes were left as-is on
purpose (low_ram already caps per-app heaps; hand-lowering risked app OOM with no upside).

## Mechanism (why debugfs, not a mount)

`fuse2fs -o rw[,fakeroot]` **cannot write this image here**: file writes `ENOSPC` even with 32 MiB
free (block-allocation path is broken under fuse2fs on this host), and with the image at
`resize2fs -M` even `mkdir` fails for lack of a free block. A kernel loop mount needs root. So all
edits go through **`debugfs`**, which writes the ext4 directly and ignores the host uid:
- removals: `tools/cm_debloat_debugfs.py <image> <dir>...` — debugfs has no `rrm`, so it walks each
  dir with `ls -l` and emits `rm` (files) then `rmdir` (deepest first).
- build.prop: `rm` + `write` + `sif mode/uid/gid` from a `debugfs -w -f` script.
- then `e2fsck -fy` (debugfs edits leave link counts to repair) and `resize2fs -M`.

Freed ~11 MiB; image 801,812,480 → 790,474,752 bytes. `e2fsck -fn` clean. The audio HAL
(`/lib/hw/audio.primary.mt8127.so`, inode 1911) is intact.

## Reproduce

```
cp --reflink=auto work/android/aosp-ford/system.img work/android/aosp-ford/system.img.pre-debloat2
python3 -I tools/cm_debloat_debugfs.py work/android/aosp-ford/system.img \
  /app/PacProcessor /priv-app/AudioFX /priv-app/BackupRestoreConfirmation /priv-app/Contacts \
  /priv-app/ManagedProvisioning /priv-app/MmsService /priv-app/SharedStorageBackup \
  /priv-app/Telecom /priv-app/TeleService /priv-app/ThemeChooser /priv-app/ThemesProvider \
  /priv-app/VpnDialogs
# then append the build.prop block above (debugfs rm+write) and:
e2fsck -fy work/android/aosp-ford/system.img && resize2fs -M work/android/aosp-ford/system.img
```

## Still open (this request)

- **Dedicated wallpaper** — being designed in a separate Opus 5.5 session (user-driven), starting
  with the Hi-Res-waveform direction (#1). Delivery here is replacing
  `res/drawable-nodpi-v4/default_wallpaper.jpg` inside `/framework/framework-res.apk` (the 480×800
  panel is mdpi→nodpi; resources.arsc unchanged, so no re-sign; STORED + zipalign, verify with
  `unzip -t`). Not done yet.
- A denser `ro.sf.lcd_density` for the small 3.1" panel is a separate UI-scale question, not
  touched here (the container `default.prop` owns rotation/density per the init-rc devlog).

See [[nw-a55-aosp-ford-image]], `2026-10-14-cm-ford-customize.md`, and the audio HAL devlog
`2026-10-15-walkman-audio-hal-built.md`.
