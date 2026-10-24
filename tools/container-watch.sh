#!/bin/bash
# One sampler for a container run, started right after `up` and left running
# until the device resets or sys.boot_completed turns 1. It combines the two
# views that matter:
#
#   host side    the memwatch fields (MemFree, Cached, Dirty, Writeback, Mapped,
#                Shmem, Active/Inactive(file), SwapCached, SwapFree, vmstat,
#                thermal, battery, D counts) so a reset has a last sample
#   container    df /data Used/Free, the dalvik-cache entry count and the
#                --dex-file of each running dex2oat, read through
#                pivotinit --join because a plain adb shell sees Sony's /data
#
# The decisive case for the capacity hypothesis: /data Used passes 230 MB
# without a reset. The decisive case against: a reset while Free is still large.
#
#   tools/container-watch.sh [SECONDS] [OUTFILE]
#
# Host side only; it reads.
set -u

cd "$(dirname "$0")/.."
INTERVAL="${1:-5}"
OUT="${2:-/tmp/nw-a55-container-watch.log}"
. tools/serial.sh; SERIAL="$(nw_serial)" || exit 1
A=(adb -s "$SERIAL")

echo "# container-watch start $(date -u +%Y-%m-%dT%H:%M:%SZ) interval=${INTERVAL}s" | tee -a "$OUT"
while :; do
    pid="$("${A[@]}" shell '/system/xbin/busybox cat /tmp/aroot/init.pid 2>/dev/null' | tr -d '\r')"

    host="$("${A[@]}" shell 'BB=/system/xbin/busybox
        echo "up=$(cut -d" " -f1 /proc/uptime)"
        $BB grep -E "^(MemFree|Cached|Dirty|Writeback|Mapped|Shmem|Active\(file\)|Inactive\(file\)|SwapCached|SwapFree):" /proc/meminfo | tr -s " " | tr "\n" " "
        echo
        $BB grep -E "^(pgmajfault|allocstall|pswpin|pswpout|pgpgin|pgpgout) " /proc/vmstat | tr "\n" " "
        echo
        echo "swap=$($BB tail -n +2 /proc/swaps 2>/dev/null | $BB awk "{print \$4}")"
        echo "thermal=$($BB cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null)/$($BB cat /sys/class/thermal/thermal_zone1/temp 2>/dev/null)"
        echo "bat=$($BB grep -E "^POWER_SUPPLY_(VOLTAGE_NOW|CAPACITY)=" /sys/class/power_supply/battery/uevent 2>/dev/null | tr "\n" " ")"' 2>/dev/null | tr -d '\r' | tr '\n' '|')"

    if [ -n "$pid" ]; then
        cont="$("${A[@]}" shell "ANDROID_ROOT=/system /tmp/pivotinit --join $pid /system/bin/sh -c '
            n=0; for f in /data/dalvik-cache/arm/*; do [ -e \"\$f\" ] && n=\$((n+1)); done
            echo -n \"entries=\$n \"
            o=\$(df /data); echo \"\$o\"
            for p in /proc/[0-9]*; do
                [ \"\$(cat \$p/comm 2>/dev/null)\" = dex2oat ] || continue
                c=\$(cat \$p/cmdline 2>/dev/null)
                case \"\$c\" in *--dex-file=*) r=\${c#*--dex-file=}; echo \"dex=\${r%%--*}\";; esac
            done'" 2>/dev/null | tr -d '\r' | tr '\n' '|')"
        bc="$("${A[@]}" shell "ANDROID_ROOT=/system /tmp/pivotinit --join $pid /system/bin/getprop sys.boot_completed" 2>/dev/null | tr -d '\r')"
    else
        cont="<no container>"
        bc=""
    fi

    printf '%s %s %s boot_completed=[%s]\n' "$(date -u +%H:%M:%S)" "${host:-<no host reply>}" "${cont}" "$bc" | tee -a "$OUT"

    if [ "$bc" = "1" ]; then
        echo "BOOT_COMPLETED" | tee -a "$OUT"
        exit 0
    fi
    sleep "$INTERVAL"
done
