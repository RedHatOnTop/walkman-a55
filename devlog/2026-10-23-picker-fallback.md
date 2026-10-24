# The picker's default after an Android start that did not finish

`docs/RELEASE.md` asked for two changes to the boot path before the next picker flash: a
picker that hangs must not hold init for good, and an Android start that never completes
must not be taken again by the picker's countdown.

## What changed

- `android-boot.sh boot` writes `1` (Walkman) to `boot-choice` before anything else, and
  `2` once the container reports `sys.boot_completed`; it polls through `sh_in` every 10 s
  while the container's init lives. The picker's countdown takes whatever `boot-choice`
  holds, so after a start that hangs, loops or resets the kernel the next boot goes to
  Walkman unless the owner chooses Android with the keys. This lives in the boot set on
  `/contents`, so it needs no UPG.
- The hook runs `bootpick` under busybox `timeout -t 180`. The unit's busybox 1.23.1 has
  the applet (`Usage: timeout [-t SECS] [-s SIG] PROG ARGS`); a child it ends returns 143,
  which the hook treats like any other picker failure and runs Sony's script.
  `tools/tests/test_picker_hook.py` covers a picker that never returns. The hook is on the
  system partition, so this one waits for the next picker UPG.

## On the unit

With the new `android-boot.sh` in the kit's boot set (MANIFEST line updated, `md5sum -c`
clean):

| boot | what | `boot-choice` |
| --- | --- | --- |
| from Android, uptime 904 s | picker's default Android | `1` at 28.1 s, `2` at 67.9 s (`boot completed` logged at 67.48 s) |
| from Android, steady | picker's default Android, rebooted at 30.35 s uptime, after the loop mounts and before Android's init | stays `1` |
| the next | the picker took Walkman by itself: `sys.sony.bootmode=1`, no container | `1` (read on the WALKMAN drive) |
| after `boot-choice` set back to `2` | Android | `2` at 67.5 s |

The data image needed an ext4 journal recovery after the cut-short start (`EXT4-fs (loop1):
recovery complete`) and mounted.

## A reboot that did not come back

Between those runs, on 2026-10-23 at 00:01:42 KST, an `adb reboot` issued about 70 s after a
boot completed dropped the USB link and the unit did not come back on USB. It had not
reached Android's `powerwatch`: `usb-log.txt` has no line for that boot. The next boot was
started with the power key at 00:30 (cmdline `androidboot.bootreason=power_key`) and has no
`/proc/last_kmsg`, so the stuck one left no kernel log. The reboot before it (from Android
at 904 s) and the three after it (from Android at 63313 s, during a start at 30 s, from
Walkman) all came back. Open; the panel was not seen while it was stuck.

## The Mali driver under ARM's GPL release

The prebuilt release carries `mali_r4p0.ko`, built from the Fire 7 kernel's copy of the
r4p0 driver, whose files (139 of 165) carry ARM's notice for DDK licensees; the public tree
had left that source out. ARM published the same driver under GPL v2:
`DX910-SW-99002-r4p0-00rel1.tgz` is still on developer.arm.com, and
`DX910-SW-99002-r4p0-00rel0.tgz` is in the Internet Archive's capture of
malideveloper.arm.com (2015-03-23; the file's sha1 is the one the capture records). Both
define `_MALI_API_VERSION 401`. Against the Fire 7 copy, with the first comment block of
every file set aside:

| | files in common | same code | changed lines |
| --- | --- | --- | --- |
| r4p0-00rel1 | 139 | 37 | 4627 in 102 files |
| r4p0-00rel0 | 139 | 109 | 849 in 30 files (MediaTek's) |

So the Fire 7 copy is rel0 with MediaTek's changes. Each of the 135 files with the DDK notice
that rel0 also has now carries rel0's GPL notice for that file; both notices are nine lines,
so no line moved. The three `.orig` files and `linux/license/proprietary`, which the GPL build
never includes, are gone (e69d9da). Built from that tree and stripped as `make-prebuilt.sh`
does, the module's `.text`, `.rodata`, `.data`, `.bss`, `.init.text` and `.exit.text` equal
the pinned build's; the build date and the revision string differ. In the boot set on the
unit it loaded (`mali_r4p0 382067 15`), Android finished at 66.8 s, SurfaceFlinger reports
`GLES: ARM, Mali-450 MP, OpenGL ES 2.0`, and the panel showed the launcher.

The export now ships the driver whole; `fire7.patch` is gone, and the check for restrictive
notices covers the driver like everything else. `kit/prebuilt.sha256` pins the rebuilt module.
