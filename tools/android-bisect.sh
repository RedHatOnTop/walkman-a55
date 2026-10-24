#!/bin/bash
# Bisect the device reset by stage, with the same synthetic load in every stage.
#
#   tools/android-bisect.sh STAGE [MINUTES]
#
#   1  prepare only                (Sony stack stopped, stock Mali driver)
#   2  prepare + Mali swap
#   3  prepare + Mali swap + zram
#
# The device script runs the stage and starts the load (two busy loops, one per
# core, plus a loop re-reading the 1.2 GB system image). This side then samples
# uptime every 10 s and reports the last uptime seen before the device stops
# answering, which is the reset time. The stage that first resets names the
# cause without any container logic in the picture.
#
# A reset is detected two ways: adb failing, or uptime going backwards. Host
# side only; it writes nothing to the device beyond the stage itself.
set -u

cd "$(dirname "$0")/.."
STAGE="${1:?usage: android-bisect.sh STAGE [MINUTES]}"
MINUTES="${2:-10}"
. tools/serial.sh; SERIAL="$(nw_serial)" || exit 1
A=(adb -s "$SERIAL")
OUT="/tmp/nw-a55-bisect-stage${STAGE}.log"

echo "# stage $STAGE, ${MINUTES} min, $(date -u +%H:%M:%S)" | tee -a "$OUT"

"${A[@]}" wait-for-device 2>/dev/null
"${A[@]}" push probe/android/android-boot.sh /tmp/android-boot.sh > /dev/null
"${A[@]}" push probe/kmod/mali-r4p0/mali_r4p0.ko /tmp/mali_r4p0.ko > /dev/null
"${A[@]}" shell "chmod 755 /tmp/android-boot.sh" > /dev/null

echo "== starting stage $STAGE" | tee -a "$OUT"
"${A[@]}" shell "sh /tmp/android-boot.sh bisect $STAGE" 2>&1 | tee -a "$OUT"

start_uptime="$("${A[@]}" shell 'cut -d" " -f1 /proc/uptime' 2>/dev/null | tr -d '\r')"
echo "== uptime at start: ${start_uptime:-?}" | tee -a "$OUT"

last=""
reset=no
deadline=$(( MINUTES * 60 / 10 ))
for i in $(seq 1 "$deadline"); do
    sleep 10
    u="$("${A[@]}" shell 'cut -d" " -f1 /proc/uptime' 2>/dev/null | tr -d '\r')"
    if [ -z "$u" ]; then
        echo "reset: adb stopped answering after uptime ${last:-?} ($((i * 10))s in)" | tee -a "$OUT"
        reset=yes
        break
    fi
    case "$u" in
        ''|*[!0-9.]*) echo "unexpected uptime '$u'" | tee -a "$OUT"; continue ;;
    esac
    if [ -n "$last" ] && awk "BEGIN{exit !($u < $last)}"; then
        echo "reset: uptime went backwards $last -> $u ($((i * 10))s in)" | tee -a "$OUT"
        reset=yes
        break
    fi
    last="$u"
    [ $((i % 6)) -eq 0 ] && echo "  uptime=$u  load=$("${A[@]}" shell 'cut -d" " -f1 /proc/loadavg' 2>/dev/null | tr -d '\r')" | tee -a "$OUT"
done

if [ "$reset" = no ]; then
    echo "SURVIVED stage $STAGE for ${MINUTES} min (last uptime $last)" | tee -a "$OUT"
fi
echo "== stage $STAGE done $(date -u +%H:%M:%S)" | tee -a "$OUT"
