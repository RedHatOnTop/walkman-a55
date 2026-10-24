#!/bin/bash
#
# Assemble the Android container root that tools/android-boot.sh pushes to the
# device. The tree is built in work/android/container/ (git-ignored) from two
# sources:
#
#   probe/android/container/       our init.rc and default.prop, tracked in git
#   work/android/fire-ramdisk/     Android init and the ueventd rules, unpacked
#                                  from the FireOS 5 boot image
#
# The result is work/android/container.tgz. Amazon's init and ueventd.rc are
# not copied into the repository; they stay in the extracted ramdisk.
#
#   tools/android-container.sh
#
# The install kit sets NW_FIRE_RAMDISK to what kit/fire_ramdisk.py took from the
# owner's Fire OS update, and NW_CONTAINER_TGZ to its own output.
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/probe/android/container"
RAMDISK="${NW_FIRE_RAMDISK:-$ROOT/work/android/fire-ramdisk}"
TGZ="${NW_CONTAINER_TGZ:-$ROOT/work/android/container.tgz}"
OUT="${TGZ%.tgz}"

# NW_CONTAINER_VARIANT selects the init.rc/default.prop pair: fire (default,
# the FireOS-userland container) or aosp (the AOSP/CM 12.1 system). Both are
# always installed into the tarball as /init.rc and /default.prop, the names the
# Amazon static init reads.
VARIANT="${NW_CONTAINER_VARIANT:-fire}"
FSTAB=""
case "$VARIANT" in
    fire) INIT_RC="init.rc"; DEF_PROP="default.prop" ;;
    aosp) INIT_RC="init.aosp.rc"; DEF_PROP="default.aosp.prop"; FSTAB="fstab.icx" ;;
    *) echo "unknown NW_CONTAINER_VARIANT=$VARIANT (want fire|aosp)" >&2; exit 1 ;;
esac

[ -f "$SRC/$INIT_RC" ] || { echo "missing $SRC/$INIT_RC" >&2; exit 1; }
[ -f "$SRC/$DEF_PROP" ] || { echo "missing $SRC/$DEF_PROP" >&2; exit 1; }
[ -x "$RAMDISK/init" ] || { echo "missing $RAMDISK/init; unpack the Fire boot image first" >&2; exit 1; }
[ -f "$RAMDISK/ueventd.rc" ] || { echo "missing $RAMDISK/ueventd.rc" >&2; exit 1; }
[ -f "$RAMDISK/ueventd.mt8127.rc" ] || { echo "missing $RAMDISK/ueventd.mt8127.rc" >&2; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT/sbin" "$OUT/dev" "$OUT/proc" "$OUT/sys" "$OUT/mnt" "$OUT/storage"

install -m 0755 "$RAMDISK/init" "$OUT/init"
# healthd registers the batteryproperties service that system_server's
# BatteryService binds to; without it BatteryService throws a null registrar.
install -m 0755 "$RAMDISK/sbin/healthd" "$OUT/sbin/healthd"
# healthd_register_event (0x9170 in this build) adds the uevent socket, the
# wake alarm and binder to its epoll set with EPOLLIN | EPOLLWAKEUP, a literal
# at file offset 0x11b4. On this kernel every suspend attempt's
# musb_bus_suspend flips otg_state and sends a uevent; with EPOLLWAKEUP that
# uevent aborts the attempt, and the abort's bus resume flips it back, so the
# device loops instead of sleeping (devlog 2026-10-19-android-opt-image.md).
# Clearing the flag leaves healthd reading those events after the resume.
# The charger-mode input copy of the literal (0x11a5c) is not used here.
HEALTHD_MD5=0861d268a64ce3868936120981a40977
if [ "$(md5sum < "$OUT/sbin/healthd" | cut -d' ' -f1)" = "$HEALTHD_MD5" ]; then
    printf '\0' | dd of="$OUT/sbin/healthd" bs=1 seek=$((0x11b7)) conv=notrunc status=none
    [ "$(od -An -tx1 -j $((0x11b4)) -N4 "$OUT/sbin/healthd" | tr -d ' ')" = 01000000 ] \
        || { echo "healthd: EPOLLWAKEUP patch did not apply" >&2; exit 1; }
else
    echo "healthd: unknown build, EPOLLWAKEUP left in (suspend may loop)" >&2
fi
install -m 0644 "$SRC/$INIT_RC" "$OUT/init.rc"
install -m 0644 "$SRC/$DEF_PROP" "$OUT/default.prop"
install -m 0755 "$SRC/devclean.sh" "$OUT/devclean.sh"
# The microSD slot, for CM 12.1's vold (which mounts exFAT through its FUSE
# helper). The fire variant's vold keeps managing nothing.
[ -z "$FSTAB" ] || install -m 0644 "$SRC/$FSTAB" "$OUT/$FSTAB"
# ueventd reads /ueventd.rc and then /ueventd.${ro.hardware}.rc. ro.hardware is
# icx on this device, so the MT8127 rules go in under that name.
install -m 0644 "$RAMDISK/ueventd.rc" "$OUT/ueventd.rc"
cat "$RAMDISK/ueventd.mt8127.rc" "$SRC/ueventd.extra.rc" > "$OUT/ueventd.icx.rc"
chmod 0644 "$OUT/ueventd.icx.rc"
ln -s ../init "$OUT/sbin/ueventd"

rm -f "$TGZ"
# Root-owned, as a boot ramdisk is: the device's busybox tar, running as root,
# would otherwise give the tree to whichever uid built it. Fixed times and
# order, so the same inputs give the same bytes.
tar -C "$OUT" --owner=0 --group=0 --numeric-owner --mtime=@0 --sort=name -cf - . | gzip -9n > "$TGZ"

echo "variant:   $VARIANT ($INIT_RC, $DEF_PROP)"
echo "container: $OUT"
echo "tarball:   $TGZ ($(stat -c %s "$TGZ") bytes)"
