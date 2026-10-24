# Driving the Walkman CXD3778GF DAC from userspace — audio output proven

2026-10-15. First target of the audio/BT/WiFi track. Goal: get real sound out of the NW-A55 under
our control, as the foundation for a CM-container `audio.primary` HAL (and for the WalkmanUI audio
settings Sol is building). **Done: a 440 Hz tone played out the headphone jack, audibly, confirmed
by the operator.**

## What the hardware is

The Walkman does not use a standard Android audio HAL. Sony replaced the whole audioflinger HAL
layer with their own ALSA-based stack (`/system/vendor/sony/lib/libaudiohal-*alsa.so`, driven by the
Qt app via `AudioInPlayerService` etc.). Underneath is a normal ALSA card:

- `/proc/asound/cards`: card 0 = **`sonysoccard`** (sony-soc-card). The codec is the Sony
  **CXD3778GF** (S-Master HX digital amp; `cxd3778gf_dnc_core.ko` is a module, the ASoC codec driver
  is built into the kernel so the card is already up).
- PCM devices: `pcmC0D0p`..`D5p` (playback), `pcmC0D1c` (capture). `libaudiohal-genericalsa.so`
  opens **`hw:0,0`** — the generic playback path. `libaudiohal-adleralsa.so` is the DSD/hi-res path
  (full of `a_coef_dcf_dsd064/128/256` tables).
- Userspace ALSA is present on the device: `libasound.so`, `amixer`, `aplay`, `speaker-test`,
  `alsaucm`, `/system/etc/asound.conf`. So no tinyalsa build was needed to prove the path.

## The winning sequence (verified: produced audible output)

Device running the CM container, so Sony's audio stack is stopped and nothing holds the card. From
that state, the CXD3778GF mixer (`amixer -c 0`) had two controls blocking output; everything else was
already playable:

- **`Master Switch` (numid=43): was `off` → set `on`** — blocker.
- **`analog playback mute` (numid=16): was `on` → set `off`** — blocker.
- Already OK (HAL should still assert them): `output device` (25) = `headphone`, `headphone amp`
  (27) = `smaster-se`, `standby` (35) = off, `playback mute` (14) = off, `analog stream mute` (17) =
  off. DSD/timed mutes irrelevant for PCM.
- Volume/gain stages: `master volume` (10, 0–120), `master gain` (13, 0–30), `Master Volume`
  (42, 0–100), `headphone smaster se gain mode` (29, normal/high). For the test: gain 10 / vol 45 /
  Master 55 / se-gain normal was comfortably audible. These are the loudness knobs — keep se-gain on
  `normal` and start low (the mapping to dB is not yet characterised).
- Jack sense is not a gate here: `jack status se` (33) already read `3pin`, so the driver sees a
  headphone and does not mute. `nc ignore jack state` (8) can override it if needed.

Proof command (operator-run, Sony host adbd):
```
amixer -c0 cset numid=43 1 ; amixer -c0 cset numid=16 0           # the two blockers
amixer -c0 cset numid=13 10 ; amixer -c0 cset numid=10 45 ; amixer -c0 cset numid=42 55
amixer -c0 cset numid=29 0
speaker-test -Dplughw:0,0 -c 2 -t sine -f 440 -l 2                 # 48000 Hz S16_LE stereo
```
All mixer writes are runtime only (reset on reboot or when Sony's stack restarts); nothing persisted.

## Next: the container audio.primary HAL

To give CM/audioflinger (and thus WalkmanUI) sound:
1. Write a standard `audio.primary.<hw>.so` that, on init, applies the enable sequence above via a
   mixer (tinyalsa `mixer_*` or libasound), and on an output stream opens `hw:0,0` (pcmC0D0p),
   S16_LE, resampling to 48 kHz as needed (the plug layer handles format/rate).
2. Container plumbing: Android init gives the container its own tmpfs `/dev`, so `/dev/snd` is not
   visible inside it — bind `/dev/snd` into the container (like the existing mali/idc binds in
   `probe/android/android-boot.sh`), and drop the stub-HAL bind for `audio.primary`.
3. Start low-volume and characterise the gain mapping before wiring it to the UI volume.

See [[nw-a55-audio-dac-alsa]]. BT is next (`/dev/stpbt` is already live; needs an MT6620
`libbt-vendor` for bluedroid); WiFi after (the MT6620 WiFi function is currently off — no
`/dev/wmtWifi`, no `wlan0`).
