#!/bin/bash
#
# Host side of the first Android GPU test (docs/android.md): releases the
# Walkman's USB mass storage, pushes mali_r4p0.ko and egltest, stops the Sony
# stack, pushes the FireOS 5 system image onto the internal storage unless it is
# there already, and runs probe/android/gpu-swap.sh on the device. The device
# needs a reboot to get the stock driver and the Sony UI back.
#
#   tools/android-gpu.sh [egl|sf] [system.img]
#       egl  run egltest (default)
#       sf   start servicemanager, surfaceflinger and bootanimation; their
#            logs and screencaps land in work/android/sf-out/
#   system.img defaults to work/android/fire7-unpack/system.img
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MODE="${1:-egl}"
IMG="${2:-$ROOT/work/android/fire7-unpack/system.img}"
NAME=wmmod-android/fire-system.img
. "$ROOT/tools/serial.sh"; SERIAL="$(nw_serial)" || exit 1
A=(adb -s "$SERIAL")

for dev in /dev/disk/by-id/usb-SONY_WALKMAN_*; do
    [ -e "$dev" ] || continue
    src="$(readlink -f "$dev")"
    if findmnt -n -S "$src" > /dev/null; then
        udisksctl unmount -b "$src" > /dev/null
    fi
done

"${A[@]}" push "$ROOT/probe/kmod/mali-r4p0/mali_r4p0.ko" /tmp/mali_r4p0.ko > /dev/null
"${A[@]}" push "$ROOT/probe/android/egltest" /tmp/egltest > /dev/null
"${A[@]}" push "$ROOT/probe/out/runas" /tmp/runas > /dev/null
"${A[@]}" push "$ROOT/probe/android/gpu-swap.sh" /tmp/gpu-swap.sh > /dev/null
"${A[@]}" shell "sync; sh /tmp/gpu-swap.sh prepare"
size="$("${A[@]}" shell "/system/xbin/busybox stat -c %s /tmp/c/$NAME 2>/dev/null" | tr -d '\r')"
if [ "$size" != "$(stat -c %s "$IMG")" ]; then
    "${A[@]}" push "$IMG" "/tmp/c/$NAME"
    "${A[@]}" shell sync
fi
case "$MODE" in
egl) "${A[@]}" shell "sh /tmp/gpu-swap.sh run $NAME" ;;
sf)
    "${A[@]}" shell "sh /tmp/gpu-swap.sh sf $NAME"
    mkdir -p "$ROOT/work/android/sf-out"
    for f in sm.log sf.log ba.log sf0.png sf1.png; do
        "${A[@]}" pull "/tmp/aroot/data/$f" "$ROOT/work/android/sf-out/$f" > /dev/null 2>&1 || echo "no $f"
    done
    ;;
*) echo "usage: tools/android-gpu.sh [egl|sf] [system.img]" >&2; exit 2 ;;
esac
