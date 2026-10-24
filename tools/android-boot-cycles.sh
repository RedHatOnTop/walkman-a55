#!/bin/bash
# Drive the container through repeated boots until sys.boot_completed=1.
#
# The device hard-resets after a few minutes of container time (no kernel
# message: the MTK watchdog, with Sony's stack stopped). The FireOS first-boot
# dexopt of the system apps takes longer than that, but the results land in
# /data/dalvik-cache on the persistent loop image, so each boot should have less
# left to do. This script runs cycles and reports the dexopt backlog each time.
#
# Host side only; it never touches the device beyond the normal up/down path.
set -u

cd "$(dirname "$0")/.."
. tools/serial.sh; SERIAL="$(nw_serial)" || exit 1
A=(adb -s "$SERIAL")
CYCLES="${1:-4}"

for cycle in $(seq 1 "$CYCLES"); do
    echo "=== cycle $cycle $(date -u +%H:%M:%S) ==="
    "${A[@]}" wait-for-device 2>/dev/null
    ./tools/android-boot.sh down > /dev/null 2>&1
    ./tools/android-boot.sh up > /dev/null 2>&1
    pid="$("${A[@]}" shell '/system/xbin/busybox cat /tmp/aroot/init.pid' 2>/dev/null | tr -d '\r')"
    echo "container init=$pid"
    [ -n "$pid" ] || { echo "  container did not start"; continue; }

    for i in $(seq 1 9); do
        sleep 40
        v="$("${A[@]}" shell "ANDROID_ROOT=/system /tmp/pivotinit --join $pid /system/bin/getprop sys.boot_completed" 2>/dev/null | tr -d '\r')"
        d="$("${A[@]}" shell '/system/xbin/busybox ps 2>/dev/null | /system/xbin/busybox grep -c "[d]ex2oat"' 2>/dev/null | tr -d '\r')"
        c="$("${A[@]}" shell '/system/xbin/busybox du -sm /tmp/aroot/data/dalvik-cache 2>/dev/null | /system/xbin/busybox cut -f1' 2>/dev/null | tr -d '\r')"
        echo "  t=$((i * 40))s boot_completed=[$v] dex2oat=$d cache=${c}MB"
        if [ "$v" = "1" ]; then
            echo "BOOT_COMPLETED cycle=$cycle"
            exit 0
        fi
        case "$d" in
            ''|*[!0-9]*) echo "  device reset mid-cycle"; break ;;
        esac
    done
done
echo "NO_BOOT_COMPLETED after $CYCLES cycles"
