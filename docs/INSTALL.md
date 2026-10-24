# Installing Android on an NW-A50 series player

This puts Android 5.1 (LineageOS 12.1's userland in a container, with WalkmanUI) next to
Walkman OS on an NW-A55, NW-A56HN or NW-A57, and a picker at power-on that starts either
one. Everything is built on your computer from your own player's backup and two files
from the web; nothing here carries Sony's or Amazon's files.

On the system partition the install changes one file, Sony's `/system/bin/bootswitcher.sh`,
and keeps the original beside it. Android and the picker live in one folder on the
player's internal storage (`wmmod-android`).

[walkman-manager](https://github.com/RedHatOnTop/walkman-manager)'s Firmware Studio does
the steps below for you. This page is the same thing by hand.

**Alpha firmware, not a stable release.** It can crash, lack things Walkman OS has, and
change between versions. Keep the backup of step 1 on your computer: it is what gets the
player back if something below goes wrong.

**State.** On one NW-A55 (`devlog/`): the backup, the picker package through
`do_fw_upgrade`, the picker, both starts and the fallback to Walkman, a set this kit built
(copied over adb, and on 2026-10-23 through the drive by walkman-manager, steps 3 and 4),
and putting a recovery slot back over the boot ROM. Not run yet: removal, and this guide
start to end on another player.

## What you need

- The player, on **Walkman One** over Sony's build BBDMP2 of 2019-07-31. The kit reads the
  Sony files it uses from your backup and refuses any other build
  (`kit/sony-bbdmp2-20190731.sha256`).
- About 2.6 GB free on its internal storage, besides the backup.
- A Linux computer (x86_64) with bash, Python 3 with Pillow, numpy and cryptography,
  e2fsprogs (`debugfs`, `dumpe2fs`, `e2fsck`, `resize2fs`, `mke2fs`), `unzip`, `curl`, `tar`,
  `gzip`, and about 6 GB free.
- Rockbox's `scsitool` (`utils/nwztools/scsitools` in the Rockbox source), which sends the
  player the command that starts its updater.
- LineageOS 12.1 for the Fire 7 (`ford`): `lineage-12.1-20200505-UNOFFICIAL-ford.zip`
  ([AndroidFileHost](https://androidfilehost.com/?fid=4349826312261799008)).
- Amazon's Fire OS 5.3.6.4 update for the same tablet,
  `update-kindle-Fire_7_LC_5th_Gen-37.6.2.6_user_626549020.bin` (664,431,377 bytes). Amazon's
  address now answers 403; the Internet Archive has a capture of it. Only its boot image is
  used (the container's `init`, `healthd`, `ueventd` rules).

## 1. Back up the player

The backup is a package for Sony's own updater that only reads: it writes every partition
of the player as an image, with a manifest of MD5s, into `WMBACKUP` on the internal storage
(about 2.4 GB). The kit takes WalkmanUI's screens, images and fonts, the boot animation and
the updater's key from it.

1. `backup-upg/build.sh` writes `backup-upg/NW_WM_FW.UPG`, the package for Walkman One.
   (`backup-upg/stock/NW_WM_FW.UPG` beside it is for Sony's own firmware; each firmware
   ignores the other's.)
2. Copy `backup-upg/NW_WM_FW.UPG` to the root of the WALKMAN drive and eject the drive in
   your file manager. Leave the cable in.
3. `scsitool -d /dev/sgN do_fw_upgrade`, where `/dev/sgN` is the WALKMAN drive's first unit
   (`lsscsi -g` lists it).
4. The player restarts into the updater, writes `WMBACKUP` for a few minutes and starts as
   usual.
5. Copy the whole `WMBACKUP` folder to your computer.

The backup holds what is your player's alone: serial number, Wi-Fi and Bluetooth addresses,
calibration. Keep it to yourself.

## 2. Build the install set

```sh
kit/make-install.sh WMBACKUP lineage-12.1-20200505-UNOFFICIAL-ford.zip \
    update-kindle-Fire_7_LC_5th_Gen-37.6.2.6_user_626549020.bin out
```

It prints `== total <n>` and one `== <i> <step>` line per step. Our own binaries (the
container's helpers, the kernel modules, WalkmanUI, the browser) come from this
repository's `prebuilt-3` release, checked against `kit/prebuilt.sha256`;
`KIT_FROM_SOURCE=1` builds them instead with `kit/make-prebuilt.sh`, which names what each
build needs.

The kit refuses a backup whose recovery slot does not hold Sony's updater with one of the
two known keys (`kit/upg_key.py`): the update trigger boots that slot, and a player whose
recovery is something else stays on the logo (see When something goes wrong).

`out/` then holds:

| path | what |
| --- | --- |
| `wmmod-android/boot/` | the boot set: the start script, the container, the modules, the input overlays, the picker, and `MANIFEST` |
| `wmmod-android/fire-system.img` | Android's `/system` (about 460 MB) |
| `wmmod-android/android-data.img` | an empty `/data` (2000 MB) |
| `NW_WM_FW.UPG` | the package that installs the picker hook, with your player's updater key |
| `install.json` | every file with its size and SHA-256, the copy order, what the package logs |

## 3. Copy it onto the player

With the player on Walkman and its drive mounted, copy in `install.json`'s `copy` order:
the boot set with `MANIFEST` last, then the two images, then `NW_WM_FW.UPG` to the root of
the drive. A player that already has `wmmod-android/android-data.img` keeps it: it is
Android's `/data`. Unmount and mount the drive again, and compare each file's SHA-256 with
`install.json`.

## 4. Install the picker

Eject the drive, leave the cable in, and run `scsitool -d /dev/sgN do_fw_upgrade` again.
The updater runs the picker package for a few seconds, renames `NW_WM_FW.UPG` to
`NW_WM_FW.UPG.done`, and restarts. `PICKERUPG.log` on the drive must end with the `hook md5
…` line `install.json` names and `====== PICKER UPG DONE ======`.

## 5. Starting

At power-on the picker shows Walkman and Android in black and white. The volume keys and
previous/next move the selection, power or play takes it. With no key pressed it takes the
remembered entry after 15 seconds; the first key stops that countdown, and a minute without
a key takes whatever is selected.

The first Android start on an empty `/data` takes about 15 minutes (every app is compiled
once) and ends in LineageOS's setup wizard; set the time zone there. Later starts reach the
home screen in about a minute.

The picker remembers Android only once Android has finished starting. If a start does not
finish (it hangs, or the player resets on the way), the next power-on defaults to Walkman;
Android can still be chosen with the keys. `wmmod-android/boot-android.log` holds the last
start's log.

## Updating

Build a new set with the new kit and copy the boot set and `fire-system.img` over the old
ones, keeping `android-data.img`. The picker package needs running again only when
`picker-upg/update.sh` changed; running it on a player that has the hook rewrites the hook
and leaves Sony's kept copy alone.

## Removing

1. `picker-remove-upg/build.sh`, then copy `picker-remove-upg/NW_WM_FW.UPG` to the root of
   the drive, eject, and `scsitool -d /dev/sgN do_fw_upgrade`. It moves Sony's kept
   `bootswitcher.sh` back over the hook, and only when both are what it expects;
   `PICKERUPG.log` says what it did.
2. Delete `wmmod-android` from the drive.

The player is then what it was before step 3.

## When something goes wrong

- **Walkman starts with no picker.** The hook runs Sony's script whenever the picker's
  files are missing. Check `wmmod-android/boot` against its `MANIFEST`.
- **Android does not come up.** If the container does not start, the player restarts into
  Walkman by itself; if it starts and never finishes, the next power-on defaults to
  Walkman. Read `boot-android.log`.
- **The player stays on the logo after `do_fw_upgrade`.** The recovery slot does not hold
  Sony's updater. It happened on the NW-A55 this was made on, where a test image had been
  left in that slot (`devlog/2026-10-21-picker-recovery.md`). Nothing in this install writes
  the recovery slot; the kit checks it in your backup, and `install.json` records its
  SHA-256 so an installer can compare it with the player's before the trigger. Getting out
  of it takes the boot ROM path below.
- **The boot ROM path.** [mtkclient](https://github.com/bkerler/mtkclient) knows the
  MT8590 (hwcode `0x8590`). Unknown321's Wampy notes enter it on the A50 series with the
  player off, Volume down and Play held, and USB plugged in. On the NW-A55 here it reached
  the preloader with SBC, SLA and DAA all off, read the system and var partitions in full
  (`mtk ro <offset> <length> <file> --parttype user`), and one bounded write put the backup's
  `recovery.img` back into the recovery slot (`mtk wo`), which read back equal byte for
  byte. Writing other partitions this way has not been tried. Take offsets and lengths from
  your backup's `dumchar_info.txt`, write only images from your own backup, and read every
  write back.
