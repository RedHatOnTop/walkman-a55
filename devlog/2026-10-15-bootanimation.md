# Walkman boot animation for the ford (CM12.1) image

2026-10-15. First piece of the Walkman skin: replace CM's boot animation with Sony's own.
Device-free; nothing flashed. `probe/skin/bootanimation/gen_bootanim.py` builds three zips.

## Sources

| build | file | content |
|---|---|---|
| 2019 stock | `system-rootfs/media/bootanimation.zip` | W.logo + WALKMAN still → logo unravels → wave loop |
| 2020 device dump | `work/device/system/media/bootanimation.zip` | "for and by Music Lovers" sparkle → line → wave loop |
| ZX507 4.06.00 | `walkman-zx507/.../exact-4.06.00-images/system.img:/system/media/bootanimation.zip` | same art as the 2020 build at 720x1280 |

The A55 files are the native-resolution originals, so the ZX507 copy is not used.

Sony's format (`icx_bootanimation`, not Android's): `desc.txt` lines are `<count> <first> <last>`
per part, frames are 16-bit BMPs, 480x200 strips drawn centred at y=300. The 2019 part0 is one
full 480x800 frame whose y=300..500 crop is pixel-identical (MAE 0) to part1's first frame. Both
builds share a byte-identical part2 (90-frame wave loop); part1→part2 and the loop seam differ by
one frame of motion, so the joins are clean.

## Output

Same shape CM12.1 already ships (its zip: 600x200@60, `c` parts, palette PNGs, STORED):
`480 200 30`, palette PNGs (96 colours), STORED zip, intro parts `c`, wave `p 0 0` (loops until
boot completes).

| zip | desc | size |
|---|---|---|
| `bootanimation-wlogo.zip` | `c 1 24 part0` / `c 1 0 part1` / `p 0 0 part2` | 919,477 B |
| `bootanimation-wlogo-gold.zip` | same, luminance mapped onto the wallpapers' Hi-Res gold ramp | 919,477 B |
| `bootanimation-musiclovers.zip` | `c 2 0 part0` / `c 1 0 part1` / `p 0 0 part2` | 923,210 B |

Texture budget: BootAnimation caches every frame of a looping part. The current CM zip loops
120 frames at 600x200; ours loops 90 at 480x200, so it is smaller than what already booted.

## Not done

Not installed into `work/android/aosp-ford/system.img` and not booted. Target is
`/media/bootanimation.zip` in the image (CM's is 1,360,590 B there). Apply after the pending
layers (debloat2, audio HAL, wallpaper) are boot-validated, so a boot failure stays attributable.

## Decision

2026-10-15, user: **`bootanimation-wlogo.zip`** (white W.logo, Sony's original colours) — it fits
the black & white Sony-stock skin direction; the gold variant stays committed but is not used.
Installing it into the image belongs to the image-integration lane (debugfs swap of
`/media/bootanimation.zip`), after the pending layers boot.

## Regenerating from your own firmware (distribution policy)

Per the project distribution policy (ship only our own patches/tools/assets; users extract
firmware files from their own device), the zips above are firmware-derived and are not for
distribution — the generator is. `gen_bootanim.py` now takes the device's own file:

```
gen_bootanim.py <your /system/media/bootanimation.zip> bootanimation.zip [--gold]
```

Part counts come from Sony's `desc.txt` (`count 0` → looping `p`, otherwise `c`; a single-frame
part is held 24 frames), so it does not assume one firmware build. Entries carry a fixed
timestamp, so output is byte-stable. Verified 2026-10-15: regenerating from the 2019 and 2020
stock zips reproduces all three committed zips entry-for-entry (desc.txt included), and two runs
on the same input are byte-identical.

2026-10-21: the three zips are no longer tracked. They are firmware-derived, so they moved to
`work/skin/bootanimation/` (gitignored) and only `gen_bootanim.py` stays in the repo. Git history
before that still holds them.
