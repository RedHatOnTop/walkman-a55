# A CXD3778GF primary audio HAL for the CM container — built and embedded

2026-10-15, same day as the DAC proof (`2026-10-15-walkman-dac-audio-proven.md`). Step 2 of the
audio track: turn the proven userspace mixer+PCM sequence into a standard Android
`audio.primary` HAL so the CM-container AudioFlinger (and thus WalkmanUI) reaches the DAC.

**Status: built, cross-checked, and embedded in the image. NOT device-validated** — the Walkman
is unplugged (charging the laptop). On-device playback is the open step, queued for the operator.

## What was built

`probe/audiohal/audio_hw.c` — a minimal, output-only primary HAL:
- **Device API 3.0** (the 5.1 current; MIN is 2.0). The vendored header is 3.0, so the struct
  layout and the 3.0 signatures (`open_output_stream` with `address`, the `*_audio_patch` /
  `*_audio_port` methods) match what AudioFlinger calls for a `version = 3_0` device. The 3.0
  patch/port methods return `-ENOSYS`, which makes the framework fall back to `set_parameters`
  routing.
- On `adev_open`: `mixer_open(0)` and apply the proven enable sequence (the two blockers
  `Master Switch`→on and `analog playback mute`→off, plus asserting `output device`=headphone,
  `headphone amp`=smaster-se, standby/mutes off, conservative volume/gain). `set_master_volume`
  maps 0..1 onto `master volume` (0–120).
- On `out_write`: lazily `pcm_open(card 0, device 0, PCM_OUT, S16_LE/48k/stereo)` then
  `pcm_write`. `out_standby` closes it. A failed `pcm_open` paces the caller and returns the byte
  count, so a dead card never spins AudioFlinger.
- Input/record is a stub (`open_input_stream` → `-ENOSYS`).

`probe/audiohal/build.sh` — reproducible cross-compile (NDK r21e, `armv7a...andioideabi22-clang`,
ARM EABI5 / API 22). Links against the image's own `libtinyalsa.so` (pulled read-only with
`debugfs dump`, no mount) and the NDK's `liblog`; `libtinyalsa`'s own deps
(`libcutils`/`libutils`/`libstdc++`) resolve on the device. Output (gitignored):
`probe/audiohal/out/audio.primary.mt8127.so`, 22 KiB stripped.

Vendored headers added under `probe/audiohal/aosp-include/` (android-5.1.1_r38): real
`cutils/bitops.h` (`popcount`), `cutils/native_handle.h`, `system/graphics.h`; plus a documented
build shim `cutils/log.h` mapping `ALOGx` onto the NDK's `__android_log_print` (the real
`cutils/log.h` drags in the whole `log/` tree the NDK does not ship — same liblog ABI at runtime).

### Why it replaces `audio.primary.mt8127.so`

AudioFlinger loads the primary by the name `ro.board.platform` dictates; this CM "ford" image has
`mt8127`, so the name is `audio.primary.mt8127.so`. The image shipped the **real MTK MT8127 HAL**
there (679 KiB; `readelf -d` shows `libaudiocompensationfilter`, `libaudiomtkdcremoval`,
`libcvsd_mtk`, `libcustom_nvram`, `libnvram`, AudioMTKStreamManager/HiFiDAC strings) — it reaches
for NVRAM/PRO_INFO and the wrong (MT8127) mixer topology, so it is useless on the MT8590
sonysoccard. We build under the same name and replace it.

## Cross-check (what is actually verified, host-side)

- ELF: `ELF 32-bit LSB shared object, ARM, EABI5, for Android 22`.
- `HMI` (= `HAL_MODULE_INFO_SYM`) is a `GLOBAL DEFAULT` dynamic symbol → AudioFlinger's
  `dlsym("HMI")` will find it.
- `DT_NEEDED` = `libtinyalsa.so`, `liblog.so`, `libdl.so`, `libc.so` — all present in the image's
  `/lib`. Undefined symbols are only `pcm_*`/`mixer_*` (→ libtinyalsa), `__android_log_print`
  (→ liblog) and libc/compiler-runtime, all resolvable on device. `-Wl,--no-undefined` passed, so
  every symbol my code references is satisfied at link time.

## Embedded in the image

`work/android/aosp-ford/system.img` now carries the HAL at `/lib/hw/audio.primary.mt8127.so`
(inode 1911, mode 0100644, uid/gid 0, 22224 bytes, byte-identical to the build output; verified by
`debugfs dump` + `cmp`). The AOSP no-op stub `audio.primary.default.so` is kept as a fallback.

Image write mechanics (learned the hard way): **`fuse2fs -o rw` (even `-o rw,fakeroot`) could not
write the file** — `cp` hit `ENOSPC` even with 32 MiB free, i.e. its block-allocating write path
fails here, not a real space problem. The working non-root path is **`debugfs -w`**: `unlink` the
old entry, `write` the new blob, `ln` it into `/lib/hw`, fix mode/uid/gid with `sif`, then
`e2fsck -fy` to repair the link counts `debugfs unlink` leaves behind, and `resize2fs -M` back to
minimum. `debugfs` ignores the host uid, so no fakeroot and no `sudo` mount is needed.

`probe/android/android-boot.sh` now auto-detects the HAL: if the in-image
`audio.primary.mt8127.so` carries the `NW-A55 CXD3778GF` marker, AudioFlinger loads it directly
(no bind); if the image still has the stock MTK HAL, it falls back to the old stub-bind so a boot
never wedges on a missing primary. `/dev/snd/*` needs no bind — the container's ueventd creates it
from the live card (`ueventd.rc`: `/dev/snd/* 0660 system audio`), the same way mali appears.

## Open risks for the on-device validation (do not report as working until seen)

1. **`hw:0,0` vs `plughw:0,0`.** The tone was proven through `plughw:0,0`, where ALSA's plug layer
   converts format/rate. tinyalsa opens `hw:0,0` **directly**. If the card does not natively
   accept S16_LE/48k/stereo at `hw:0,0`, `pcm_open` fails (logged `nwa55_audio_hw: pcm_open ...`).
   First thing to check in logcat. Fallback: match the card's native hw params, or convert in
   `out_write`.
2. **SELinux / `/dev/snd` access** from the mediaserver domain. The embedded apps load from
   unlabeled `/system` and work, which suggests the container tolerates it, but audio device
   access is a distinct rule — watch for `avc: denied` on `/dev/snd`.
3. Volume/gain dB mapping is uncharacterised; it starts conservative (`master_volume` 0.35).

## Validate on device (operator-run; device currently unplugged)

```
NW_DROP_SYS_BOOT=1 NW_SYS_IMG=work/android/aosp-ford/system.img NW_CONTAINER_VARIANT=aosp \
  tools/android-boot.sh up          # expect "audio HAL: in-image CXD3778GF HAL, loaded directly"
tools/android-boot.sh sh 'logcat -d | grep -iE "nwa55_audio_hw|AudioFlinger|audio.primary|avc: denied.*snd"'
# then play something through WalkmanUI / a tone and listen on the headphone jack
```

See [[nw-a55-audio-dac-alsa]], [[nw-a55-aosp-ford-image]]. Next on the track: BT
(`docs/mt6620-connectivity-hal-brief.md`).
