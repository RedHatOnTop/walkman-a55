# Dedicated wallpapers + embedding #1 (Hi-Res waveform) as the ford default

2026-10-15. User asked for a dedicated wallpaper as part of the image customization. The designs
were produced in a separate session (the user's Opus 5.5 session) under `probe/wallpaper/`; this
devlog covers the set and the **image embedding**, which is the part done here.

**Status: embedded + filesystem-verified, NOT boot-validated** (Walkman unplugged). On the next
re-flash + boot the default wallpaper should be the Hi-Res waveform.

## The set (all 480×800 RGB JPEG q88, in `probe/wallpaper/`)

1. **`hires-waveform-480x800.jpg`** (44,866 B) — gold ribbon/filament waveform band (y≈300–450),
   orange playhead "02:47", gold ruler + `NW-A55` / `24 bit / 192 kHz`, WALKMAN wordmark, Hi-Res
   badge; black top/bottom. Generator `gen_hires.py` (numpy float accumulation → gold tone-map).
   **This is the one embedded as the default** (user: "1번부터").
2. **`walkman-creature-480x800.jpg`** (56,908 B) — original cassette-creature mascot (reel eyes,
   orange over-ear headphones, stubby legs, waving arm, coiled-cable tail with a gold jack) on a
   dark speaker-grille glow with a floor reflection. `walkman-creature.html` → `render.py`
   (headless-Chromium SVG).
3. **`hires-ripple-480x800.jpg`** (37,639 B) — concentric gold ripples on black "liquid" as the
   waveform; very dark, excellent icon legibility. `gen_ripple.py`.
4. **`circuit-480x800.jpg`** (65,380 B) — black solder-mask macro with gold I2S/output traces,
   MediaTek SoC + S-Master (CXD3778GF) QFN + SD slot. **Stylised/created, not an accurate board**
   (the glowing routes, small passives, silk part numbers, board number and the Hi-Res silk are
   artistic; only CXD3778GF / MT8590 / dual clocks are real). `gen_circuit.py`.

All four were viewed (rendered) and read well against a Trebuchet grid mock-up; all are original
generated art.

## Embedding mechanism (image-only; my lane)

Android's default wallpaper is `com.android.internal.R.drawable.default_wallpaper` in
`framework-res.apk`. The 480×800 panel is mdpi, and resource resolution picks the density-agnostic
**`res/drawable-nodpi-v4/default_wallpaper.jpg`** (no mdpi variant exists; nodpi beats the xhdpi /
sw600 / sw720 variants for this device). Replacing that one file:
- Rebuilt `framework-res.apk`: `zip -0 -X` the new JPG in as **STORED** (so AssetManager can mmap
  it), then **`zipalign -f 4`**. `resources.arsc` is untouched (the resource ID still maps to the
  same path), so **no re-sign** — AssetManager does not verify the framework package signature at
  resource-load time. `unzip -t` clean; entry now 44,866 B.
- Written into the image with `debugfs` (same path as the HAL/build.prop edits; fuse2fs can't
  write here): `rm` the old apk, `cd /framework`, `write` the new one, `sif` mode 0644 uid/gid 0;
  then `e2fsck -fy` and `resize2fs -M`. Grew the image +20 MiB first for headroom (17.7 MiB apk),
  shrank back after. Embedded apk is byte-identical to the built one (`cmp`), `unzip -t` clean.
- No regressions: audio HAL (inode 1911), `ro.config.low_ram`, `/vendor` and `/bin/idmap` (the BT
  A2DP-only RRO overlay's dependency) all intact. Image 790,491,136 B.

## Swapping the default

To make a different one the default, rebuild `framework-res.apk` with that JPG at
`res/drawable-nodpi-v4/default_wallpaper.jpg` (STORED + zipalign) and `debugfs`-replace it — a
one-liner change of the source path in the recipe above. All four JPGs stay in `probe/wallpaper/`,
so the user can pick after seeing them on the real panel. Pristine revert: `system.img.cmstock`.

## Open / on-device

- Not boot-tested: the real default-wallpaper pick + legibility behind live Trebuchet icons is
  confirmed only against a mock-up. Verify on the next re-flash.
- A denser `lcd_density` for the 3.1" panel (separate UI-scale question) is still untouched.

## Boot animation bundled into the image (2026-10-15)

Per the user's "bundle now" call (it's a cosmetic that can't break boot, unlike the pending RRO
skin), `/media/bootanimation.zip` in the image was replaced via debugfs with the design session's
`probe/skin/bootanimation/bootanimation-wlogo.zip` (the Sony stock white W.logo → wave loop; commits
`9e23c5d`/`53c131e`). New 919,477 B < old 1,360,590 B, so the swap freed space; embedded file is
byte-identical (`cmp`), `unzip -t` clean, inode 86, mode 0644 root:root. Image now 790,007,808 B;
audio HAL / low_ram / wallpaper / `/vendor` / `/bin/idmap` all still intact.

Personal use on the user's own device; for distribution this is regenerated from the user's own
firmware via `gen_bootanim.py` (see [[nw-a55-distribution-policy]]). So the current image now carries
the full device-free layer: audio HAL + debloat2 + low_ram/battery + default wallpaper + boot
animation — all filesystem-verified, awaiting one on-device re-flash + boot to validate.

See [[nw-a55-aosp-ford-image]], `2026-10-15-ford-debloat2-lowram.md`.
