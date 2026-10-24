# WalkmanUI missed a card that vold mounted after it started

The owner saw WalkmanUI on Android list no music from the microSD card (10,010 tracks there).

## Cause

`main.cpp` took the storage list once, at start: every folder under `/storage` that is
readable and not empty. vold mounts the card on its own time. On the 2026-10-23 boot its
`Checking` state (fsck.exfat) took 4.4 s and the card was mounted 16 s before `boot completed`;
a check of an exFAT volume left dirty takes longer, and the unit had two forced power-offs
that day. A WalkmanUI started before that saw `/storage/sdcard1` as an empty folder and left
the card out until the app was started again.

Reproduced on the unit with the shipped APK: `vdc volume unmount /storage/sdcard1`, start
WalkmanUI (Folder: System Storage only), `vdc volume mount /storage/sdcard1`. The card was
mounted and readable, and Folder still had no SD Card 15 s later. The scan without the card
had also rewritten `library.cache` from 8,296,312 bytes to 12: every card track's tags were
gone, to be read again when the card came back.

## Fix

- `WalkmanActivity` takes `MEDIA_MOUNTED`, `MEDIA_UNMOUNTED`, `MEDIA_REMOVED` and
  `MEDIA_BAD_REMOVAL` (not `MEDIA_EJECT`: a scan started then would hold the card's files while
  vold unmounts it) and calls the library to take the list again.
- `MEDIA_MOUNTED` comes once vold has mounted `/mnt/media_rw/sdcard1`, while the sdcard
  daemon's view in `/storage` is still empty: the first build of the fix got the broadcast
  (`dumpsys activity broadcasts` lists the receiver) and saw no change. The list is taken at
  0, 1, 3, 7 and 15 s after each broadcast; `setStorages` does nothing while the paths are the
  same.
- The cache keeps the tracks of a storage that is not there for a scan.

On the unit (release APK `50357cdc…` installed over the system copy), WalkmanUI running on
Folder each time:

| step | Folder |
| --- | --- |
| card unmounted, app started | System Storage only; `library.cache` stays 8,296,312 bytes |
| `vdc volume mount` | SD Card 10010 on the open screen within 25 s |
| unmounted while open | SD Card gone |
| mounted again | SD Card 10010 47 s after `MEDIA_MOUNTED` (the media scanner read the card too) |

Not seen yet: the case itself, a boot where the card mounts after the app starts.

## In the kit

`prebuilt-2` is `prebuilt-1` with this `WalkmanUI.apk` (`50357cdc…`); the other eleven files
are the same bytes. The kit built from it (`install14`) differs from `install13` in two
image entries only, `/app/WalkmanUI/WalkmanUI.apk` and its `libwalkmanui_armeabi-v7a.so`; the
boot set and the UPG are the same bytes. Its system image on the unit: `boot completed` at
66.06 s, and the image's WalkmanUI, started with the card unmounted, listed SD Card 10010
after `vdc volume mount`.
