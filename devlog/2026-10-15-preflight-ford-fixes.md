# 2026-10-15 — pre-flash static review of the ford image + fixes

Before the first on-device re-flash of the fully customized `work/android/aosp-ford/system.img`
(audio HAL + debloat2/low_ram + default wallpaper + bootanim), a static review pass looked for
boot-blocking failure points. It found **no confirmed boot-blocker**, but two confirmed crash
paths and two guard/correctness gaps. All four are device-free fixes and were applied + verified
at the filesystem level (NOT yet boot-validated — that needs the operator + device).

## 1. HAL SIGSEGV in `out_write` (confirmed crash) — FIXED

Two compounding bugs in `probe/audiohal/audio_hw.c` / the vendored tinyalsa header:

- `pcm_open(..., (struct pcm_config *)&pcm_config_out)` handed the lib a cast pointer to the
  shared `static const pcm_config_out`, which lives in `.rodata`. `pcm_open` writes the
  negotiated period back into `*config` → write to read-only page → fault in mediaserver.
- The vendored `struct pcm_config` was 8 fields / 32 B, but Android 5.1 libtinyalsa carries a
  9th field `avail_min` at offset 0x20 and reads/writes it → the lib touches memory past the
  caller's shorter struct.

Fix: `out_write` now copies into a writable local `struct pcm_config config = pcm_config_out;`
and passes `&config`. `probe/audiohal/aosp-include/tinyalsa/pcm.h` gains the `avail_min` field
so the struct layout matches the image's lib. Rebuilt HAL is 22224 B, HMI symbol exported,
sentinel `"NW-A55 CXD3778GF"` present, byte-identical copy embedded at
`/lib/hw/audio.primary.mt8127.so`.

## 2. ThemesProvider removal → system_server crash (confirmed) — FIXED

debloat pass 2 removed the whole CM theme engine, but `ThemeService` in `services.jar` (part of
system_server) calls `ContentResolver.update` on the MixnMatch provider URI when the wallpaper
or notification sound changes, and the call is uncaught. With the provider gone the resolver
throws and takes system_server with it — exactly the path our default-wallpaper swap would hit.

Fix: restored `/priv-app/ThemesProvider/ThemesProvider.apk` (59444 B, 0644 0:0) from the
pre-debloat2 image; `ThemeChooser` (the user-facing settings UI) stays removed. The provider is
the only piece system_server reaches.

## 3. `init_check` wedge guard — FIXED

`adev_init_check` returned `-ENODEV` when the mixer failed to open. AudioFlinger treats a primary
HAL that fails init as unusable and unloads it, leaving the system with no primary → system_server
audio init wedges. Fix: `adev_init_check` always returns 0; `out_write` lazily retries
`mixer_open(PCM_CARD)` and re-asserts the route if `adev_open` missed it. `pcm_write` failure now
closes + standbys + paces (sleeps one buffer-duration) and returns `bytes` rather than a negative
error, so AudioFlinger's MixerThread does not spin a retry loop flooding the log.

## 4. Default wallpaper lost to a higher-density variant — FIXED

Android 5.1 `ResourceTypes` density matching: `nodpi` (0xffff) *loses* to an `xhdpi` variant for
an mdpi device, so swapping only `drawable-nodpi-v4/default_wallpaper.jpg` would have left the
stock CM wallpaper showing. Fix: all four `default_wallpaper.jpg` variants in framework-res.apk
(`nodpi-v4`, `xhdpi-v4`, `sw600dp-nodpi-v4`, `sw720dp-nodpi-v4`) now carry the Hi-Res waveform
(44866 B each, STORED, `zipalign -f 4`, resources.arsc untouched → no re-sign). Rebuilt apk is
byte-identical to the embedded one, `unzip -t` clean.

## Verified OK by the review (no change needed)

e2fsck clean, `/vendor` + `/bin/idmap` preserved (A2DP-only RRO intact), the 8 RRO bools exist in
Bluetooth.apk, `/media/bootanimation.zip` == wlogo, HAL probe order correct.

## Operational note for the flash (tell the operator)

The customized image is NOT the default. The flash command must set
`NW_SYS_IMG=work/android/aosp-ford/system.img NW_CONTAINER_VARIANT=aosp` (defaults are
fire7/fire). `push_once` only checks size — recommend an md5 check after the upload. Do not
manually push `container.tgz`.

## Not fixed (informational)

- `purgeable_assets` is a no-op on 5.1 (harmless).
- SELinux is not loaded (kernel cmdline disables it), so the earlier `/dev/snd` SELinux-label
  worry is moot; the lazy-mixer retry covers the case where `/dev/snd` is not yet present at
  `adev_open`.

Post-flash watch list on the boot: telephony-app pruning, low_ram (Firefox OOM), the
default-wallpaper + boot-animation, A2DP-only, and the audio HAL actually producing sound.
