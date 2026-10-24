# 2026-10-16 Walkman sound effects on the NW-A55 image

User (2026-10-16): "음향 효과는 특정 하드웨어의 안드로이드 기기와 호환되게만 만들면 됨". Asked which
hardware, they picked the NW-A55 Android image (CM-12.1, Android 5.1.1, API 22, armeabi-v7a),
not the Sony izm-HAL Walkmans. Until now the sound pages only kept their settings.

## What there is

- `android-ui/audiofx/`: `libwalkmanfx.so`, an Android 5.1 effect library (library API 3.0,
  effect control API 2.0, insert effect). `walkmanfx.cpp` is the processing, `effect.cpp` the
  effect-library glue, `aosp/` the android-5.1.1_r38 ABI headers, unmodified.
- How it reaches the audio: `audio_effects.conf` gets a library, an effect and an
  `output_session_processing { music { walkmanfx { } } }` block. AudioPolicyEffects then creates
  it on every music session when the track starts, and enables it. QMediaPlayer on Android is a
  MediaPlayer on STREAM_MUSIC, so the app needs no audio session id.
- How the settings reach it: the app makes one more instance of the same effect on session 0
  (the output mix), never enables it, and sends a `WalkmanFxSettings` (`fxsettings.h`) through
  `setParameter(byte[], byte[])`. The library keeps one copy behind a mutex and a generation
  counter, and each session instance picks the new settings up on its next buffer. The
  `AudioEffect(type, uuid, priority, session)` constructor and `setParameter(byte[], byte[])` are
  hidden API on 5.1, so `SoundEffects.java` reaches them by reflection. Session 0 needs
  `MODIFY_AUDIO_SETTINGS`.
- `src/soundeffects.cpp` sends the settings when SoundState changes, when the track changes (a
  hi-res track gets no DSEE HX, under ClearAudio+ too) and when playback starts (a restarted
  mediaserver comes back with every effect off). Where the effect is absent, as on Waydroid,
  `AudioEffect.queryEffects()` lacks its UUID and the app logs once and keeps the settings.

## The processing

Sony never published these, so they are ours, built to do what the stock help describes.
Order: DSEE HX, the equalizer or tone control, DC phase linearizer, vinyl processor, dynamic
normalizer, a soft limiter (knee at -1 dBFS). Source Direct, and everything off, pass the
stream bit for bit.

- Equalizer: ten one-octave peaking filters (Q 1.41, 31.25 Hz to 16 kHz), with the gain
  lowered by the response's peak so a boost does not clip. Tone control: low shelf 120 Hz,
  peak 1 kHz, high shelf 8 kHz.
- DSEE HX: the 8 kHz region, full-wave rectified less its envelope, high-passed twice at
  14 kHz and mixed in at half level; it makes the top octave a lossy codec cut from the octave
  under it. Only at 44.1 kHz and up.
- DC phase linearizer: an all-pass, so the level stays flat; type A first order, type B second
  order, LOW/STANDARD/HIGH corners at 40/80/160 Hz.
- Vinyl: tonearm resonance (10 Hz and 12 kHz peaks, 18 kHz roll-off), platter resonance (45 and
  95 Hz), surface noise (hiss about 66 dB down, about three clicks a second), and in STANDARD
  all of those plus -30 dB crosstalk.
- Dynamic normalizer: the level over 3 s brought towards -20 dBFS by up to 9 dB, down in 0.5 s,
  up in 4 s, held below -50 dB.
- ClearAudio+: a fixed set of its own (a light loudness curve, DSEE HX, DC phase A STANDARD).

Denormals: the surface-noise clicks decayed into denormal doubles and made the vinyl
processor ten times slower than the rest (host: 620 ms per minute of audio, 35 ms with
flush-to-zero). Every filter state under -300 dB is now zeroed after each buffer, which also
covers the filters ringing down after the music stops.

## Image facts found on the way

- The image has `/system/vendor/etc/audio_effects.conf`, and 5.1's EffectsFactory and
  AudioPolicyEffects read that one, not `/system/etc/audio_effects.conf`, when it exists. The
  `/system/etc` copy also names Dolby's `ds` (`/system/vendor/lib/soundfx/libswdap.so`).
  `embed.py` patches both.
- `audio_policy.conf`: the primary output is PCM 16-bit, 44.1 kHz, stereo; A2DP, USB and remote
  submix are PCM too. No offload or deep-buffer output, so nothing bypasses the effect.
- The image sits at `resize2fs -M` with 20 free blocks; the library and the config growth need
  8. `embed.py` grows the image when it has to.
- Files in `/system/lib/soundfx` carry `u:object_r:system_file:s0`, and `embed.py` gives the new
  ones the same label. In the current image `/lib/hw/audio.primary.mt8127.so` (the HAL track's)
  has no `security.selinux` attribute at all. That matters only if the device enforces
  SELinux, which nothing here has checked.

## Evidence (host only)

- `make -C android-ui/audiofx test`: PASSED, 0 failures. Measured: a +6 dB band at 1 kHz is
  +5.95 dB against 125 Hz; bass +6 is +5.8 dB at 50 Hz against 1 kHz; DSEE HX takes 16 kHz from
  -195 dB to -25.9 dB on an 8 kHz tone at -10.5 dB, and adds nothing to 500 Hz; the normalizer
  moves a -43 dBFS tone +8.9 dB, a -17 dBFS tone -3.0 dB and near-silence 0.0 dB; surface noise
  -66.6 dBFS on silence; Source Direct and all-off are bit exact; 1.5 s into silence the output
  is exact zeros (the same check without the flush fails with 44100 nonzero samples).
- The same test loads the built `.so` with `dlopen`/`dlsym("AELI")` as the effects factory
  does and drives it as AudioFlinger and the app do: descriptor, create on session 0 and on a
  music session, INIT/SET_CONFIG (mono refused), a disabled instance still accumulating and
  answering -ENODATA, settings sent to the session 0 instance changing the music session's
  output, GET_PARAM round trip, the control instance passing the output mix untouched.
- Every effect on costs 185 ms per minute of audio on the host (Core Ultra 7 155H), 0.31% of
  real time. Not measured on the A7.
- ARM build: 22012 bytes, `DT_NEEDED` libm, libdl, libc; `AELI` is its only export; all 17
  imports are exported by the image's own libc/libm.
- Wiring harness (the app's objects with a recording `applySoundEffects`): the defaults on
  start, each change once and no repeats, tone control, every effect, Source Direct, ClearAudio+,
  `hiResSource` 1 for a 96 kHz/24-bit FLAC and 0 for a 44.1 kHz/16-bit one. 9 of 9.
- `embed.py` on a reflink copy of `work/android/aosp-ford/system.img`: the library reads back
  byte-identical; the three files are 0644 root:root with the label; the vendor config's diff is
  exactly the three entries; `e2fsck -fn` clean; a second run leaves the configs alone; the
  grow path (a 300 KB stand-in) grows the image to 192775 blocks and checks clean.
  `work/wui-takeover/fximg/system.img` is that copy, ready for the operator.
- Host tour 111 of 111, the same 65 Sony QML TypeErrors as the tour before.

## Not verified

- Nothing has run on the NW-A55. Nobody has heard the effects; whether this CM-12.1 build's
  AudioPolicyEffects attaches them, whether mediaserver may load the library under the
  device's SELinux mode, the reflection on API 22 and the CPU load on the Cortex-A7 are open.

## Operator steps

    make -C android-ui/audiofx test android
    python3 -I android-ui/audiofx/embed.py work/android/aosp-ford/system.img
    # flash as usual, play a track, then:
    adb logcat -d | grep -iE "walkman|EffectsFactory|AudioPolicyEffects"
    adb shell dumpsys media.audio_flinger | grep -B2 -A6 "Walkman sound effects"

The dump should list the effect in the playing track's session chain, enabled; turning on the
equalizer in the app should be audible at once.
