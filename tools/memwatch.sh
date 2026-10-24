#!/bin/bash
# Sample the device every few seconds while a container run is in flight, so the
# last samples before an unexplained reset show what the kernel was doing. The
# device hard-resets during long runs and /proc/last_kmsg ends mid-stream with
# no panic or OOM text, so this is the only outside view.
#
#   tools/memwatch.sh [SECONDS] [OUTFILE]
#
# Collected, and why:
#   MemFree/Buffers/Cached      headline numbers
#   Dirty/Writeback             a flush storm can block every writer
#   Mapped/Shmem                page cache the kernel cannot simply drop
#   Active(file)/Inactive(file) reclaimable vs working set
#   SwapCached/SwapFree         whether zram is absorbing anything
#   swappiness                  why swap may not be taking anonymous pages
#   pgmajfault/allocstall       direct reclaim stalls
#   pswpin/pswpout              swap actually moving
#   thermal_zone*/temp          heat at the moment of the stall, not after
#   battery VOLTAGE/CURRENT/TEMP  a voltage dip means power, not memory
#   D_kernel / D_user           processes in uninterruptible sleep. The kernel
#                               threads (wdtk-*, hang_detect, pmic_thread_kth,
#                               GCPU, ...) sit in D on this device all the time,
#                               so only D_user moves.
#
# Interpretation: a high zone temperature or a voltage dip means memory is not
# the story; flat temperature with MemFree near zero and D_user growing means it
# is.
#
# Host side only; it never writes to the device.
set -u

cd "$(dirname "$0")/.."
INTERVAL="${1:-5}"
OUT="${2:-/tmp/nw-a55-memwatch.log}"
. tools/serial.sh; SERIAL="$(nw_serial)" || exit 1
A=(adb -s "$SERIAL")

echo "# memwatch start $(date -u +%Y-%m-%dT%H:%M:%SZ) interval=${INTERVAL}s" | tee -a "$OUT"
while :; do
    sample="$("${A[@]}" shell 'BB=/system/xbin/busybox
        echo "up=$(cut -d" " -f1 /proc/uptime)"
        $BB grep -E "^(MemFree|Buffers|Cached|Dirty|Writeback|Mapped|Shmem|Active\(file\)|Inactive\(file\)|SwapCached|SwapFree):" /proc/meminfo | tr -s " " | tr "\n" " "
        echo
        echo "swappiness=$($BB cat /proc/sys/vm/swappiness)"
        $BB grep -E "^(pgmajfault|allocstall|pswpin|pswpout|pgpgin|pgpgout) " /proc/vmstat | tr "\n" " "
        echo
        th=""; for z in /sys/class/thermal/thermal_zone*/temp; do th="$th${z%/temp}=$(cat $z 2>/dev/null) "; done
        echo "thermal=$th"
        echo "battery=$($BB grep -E "^POWER_SUPPLY_(VOLTAGE_NOW|CURRENT_NOW|TEMP|CAPACITY|STATUS)=" /sys/class/power_supply/battery/uevent 2>/dev/null | tr "\n" " ")"
        echo "swaps=$($BB tail -n +2 /proc/swaps 2>/dev/null | tr "\n" " ")"
        dk=0; du=""
        for p in /proc/[0-9]*; do
            s=$($BB sed -n "s/^[0-9]* (.*) \([A-Z]\).*/\1/p" $p/stat 2>/dev/null)
            if [ "$s" = "D" ]; then
                c=$($BB cat $p/cmdline 2>/dev/null | $BB tr -d "\000")
                if [ -n "$c" ]; then du="$du${p#/proc/} "; else dk=$((dk+1)); fi
            fi
        done
        echo "D_kernel=$dk D_user=[$du]"
        echo "load=$($BB cat /proc/loadavg)"' 2>/dev/null | tr -d '\r' | tr '\n' '|')"
    printf '%s %s\n' "$(date -u +%H:%M:%S)" "${sample:-<no reply>}" | tee -a "$OUT"
    sleep "$INTERVAL"
done
