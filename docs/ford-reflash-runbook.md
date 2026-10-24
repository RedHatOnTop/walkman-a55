# ford re-flash + boot-validation runbook (operator)

The fully customized `work/android/aosp-ford/system.img` is staged and filesystem-verified (audio
HAL + debloat2/low_ram + Hi-Res-waveform wallpaper + wlogo bootanim + the 2026-10-15 pre-flash
fixes, commit `73dac18`). It is **not boot-validated**. This is the operator's device pass — Claude
does not run adb.

```
image : work/android/aosp-ford/system.img
size  : 789270528 bytes (752.7 MiB, under the 2 GiB adbd push cap)
md5   : 54260441904e9ea98310235659225013
serial: <serial>   (override with NW_SERIAL=)
```

## 0. Device present

```sh
adb devices          # expect <serial>  device
adb -s <serial> root 2>/dev/null; adb -s <serial> wait-for-device
```

## 1. Swap-accounting guard (DO NOT SKIP)

The stock kernel's zram swap bugs reset the container at ~315 s under memory pressure; dexopt and
low_ram+Firefox both apply pressure. The guard must be resident and `[permanent]`.

```sh
adb -s <serial> shell "lsmod | grep -q swap_accounting_guard && echo loaded || echo absent"
# if absent:
adb -s <serial> push probe/kmod/swap-accounting-guard/swap_accounting_guard.ko /tmp/
adb -s <serial> shell "insmod /tmp/swap_accounting_guard.ko; cat /proc/modules | grep swap_accounting_guard"
# the line must end in  ... - Live 0x...  [permanent]
```

## 2. Pick the path

```sh
adb -s <serial> shell "ls -l /tmp/c/wmmod-android/ 2>/dev/null"
```

- **Container state intact** (the dir lists `fire-system.img` ~804M + `android-data.img` 2000M):
  lighter path — only the system image re-pushes, `/data` is kept (dalvik-cache survives, no full
  dexopt storm). Go to §3A.
- **Empty / host rebooted** (`/tmp` is tmpfs, wiped on reboot): everything re-pushes including the
  2000M `/data`. The in-`up` push of a 2000M file can race `prepare`'s USB-gadget jiggle and EOF.
  Push `/data` standalone first. Go to §3B.

## 3A. Re-flash keeping /data (container intact)

```sh
NW_SYS_IMG=work/android/aosp-ford/system.img NW_CONTAINER_VARIANT=aosp tools/android-boot.sh down
NW_SYS_IMG=work/android/aosp-ford/system.img NW_CONTAINER_VARIANT=aosp NW_DROP_SYS_BOOT=1 tools/android-boot.sh up
```

`push_once` compares device-side size; 789270528 ≠ the old ~804M, so the new image pushes and
`/data` (unchanged size) is skipped.

## 3B. Fresh container (host rebooted)

```sh
# bring the container skeleton up to the point /tmp/c exists, then push the big /data on a stable USB
NW_SYS_IMG=work/android/aosp-ford/system.img NW_CONTAINER_VARIANT=aosp tools/android-boot.sh prepare
adb -s <serial> push work/android/android-data.img /tmp/c/wmmod-android/android-data.img
# then the normal up (push_once now skips /data, pushes system.img)
NW_SYS_IMG=work/android/aosp-ford/system.img NW_CONTAINER_VARIANT=aosp NW_DROP_SYS_BOOT=1 tools/android-boot.sh up
```

If you want a clean-state validation instead (full dexopt storm, slower boot), remove the host
`work/android/android-data.img` first so `up` makes a fresh 2000M /data, and after boot bypass
SetupWizard: `settings put global device_provisioned 1; settings put secure user_setup_complete 1`.

## 4. Verify the pushed image (push_once only checked size)

```sh
adb -s <serial> shell "md5sum /tmp/c/wmmod-android/fire-system.img"
# must be 54260441904e9ea98310235659225013
```

## 5. Boot validation checklist

Watch the boot and drive the flows — a green container is not a rendered UI.

```sh
NW_SYS_IMG=work/android/aosp-ford/system.img NW_CONTAINER_VARIANT=aosp tools/android-boot.sh pull
#   -> work/android/boot-out/{init.out,logcat.txt,dumpsys-l.txt}
```

| Check | How | Pass |
|-------|-----|------|
| no ~315 s reset | watch the container for 6+ min through dexopt | guard held, still up |
| Trebuchet home upright | look at the Sony panel | home renders, not rotated |
| **default wallpaper** | home background | Hi-Res gold waveform, NOT stock CM |
| **boot animation** | watch the boot | white W.logo → wave (wlogo) |
| **audio HAL loads** | `logcat.txt` grep `nwa55_audio_hw` / `audio.primary` | `pcm_open(0,0)` succeeds, no SIGSEGV in mediaserver |
| `/dev/snd` present | `tools/android-boot.sh sh "ls -l /dev/snd"` | pcmC0D0p etc. exist |
| **sound at the jack** | play audio in Android, headphones in | audible (HAL applies the CXD3778GF unmute itself) |
| no system_server crash on wallpaper | change wallpaper in Settings | no FC (ThemesProvider restored) |
| telephony pruning safe | boot + open Settings | no Phone/Mms FC (providers kept) |
| low_ram vs Firefox | open Firefox, wait out the cold start | renders, not OOM-killed |
| A2DP-only (if BT reachable) | pair a BT audio device | A2DP only, no HFP/HSP — BT itself is -26's unvalidated layer |

Report back what each row did (quote any logcat failure verbatim); Claude verifies against the
image and the fixes. A failing audio row with the exact `logcat` line is the most useful signal.
