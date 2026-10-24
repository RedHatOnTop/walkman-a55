#!/bin/bash
# Sample dexopt progress so a reset can be reported as entries/total and as the
# package it landed on.
#
#   tools/dexopt-watch.sh [SECONDS] [OUTFILE]
#
#   total    the dexopt inputs on this image: 76 framework jars plus 172
#            /system/app and /system/priv-app packages, counted on the host from
#            the Fire image with debugfs (an upper bound: jars outside
#            BOOTCLASSPATH and apks without dex get no entry).
#   entries  the oat files written so far, counted with shell builtins because
#            the container's sh has neither wc nor cut.
#   dexfile  the --dex-file of each running dex2oat, pulled out of the NUL-joined
#            cmdline with parameter expansion, because entries are not equal in
#            cost and a reset may sit at a fixed entry rather than a fixed count.
#
# The counts must be taken inside the container's mount namespace: a plain
# `adb shell` sees Sony's /data and reads 0 entries for the whole run
# (measured: 41 in the container while the sampler said 0).
#
# Run it while the container is up; it reads only.
set -u

cd "$(dirname "$0")/.."
INTERVAL="${1:-5}"
OUT="${2:-/tmp/nw-a55-dexopt.log}"
. tools/serial.sh; SERIAL="$(nw_serial)" || exit 1
A=(adb -s "$SERIAL")
TOTAL=248

echo "# dexopt-watch start $(date -u +%Y-%m-%dT%H:%M:%SZ) interval=${INTERVAL}s total=${TOTAL}" | tee -a "$OUT"
while :; do
    pid="$("${A[@]}" shell '/system/xbin/busybox cat /tmp/aroot/init.pid 2>/dev/null' | tr -d '\r')"
    if [ -z "$pid" ]; then
        printf '%s %s\n' "$(date -u +%H:%M:%S)" "<no container>" | tee -a "$OUT"
        sleep "$INTERVAL"
        continue
    fi
    sample="$("${A[@]}" shell "ANDROID_ROOT=/system /tmp/pivotinit --join $pid /system/bin/sh -c '
        up=\$(cat /proc/uptime); up=\${up%% *}
        n=0; for f in /data/dalvik-cache/arm/*; do [ -e \"\$f\" ] && n=\$((n+1)); done
        echo up=\$up entries=\$n
        for p in /proc/[0-9]*; do
            [ \"\$(cat \$p/comm 2>/dev/null)\" = dex2oat ] || continue
            c=\$(cat \$p/cmdline 2>/dev/null)
            case \"\$c\" in
                *--dex-file=*)
                    r=\${c#*--dex-file=}; r=\${r%%--*}
                    echo \"dex2oat pid=\${p#/proc/} \${r}\" ;;
                *) echo \"dex2oat pid=\${p#/proc/} (no dex-file arg)\" ;;
            esac
        done'" 2>/dev/null | tr -d '\r' | tr '\n' '|')"
    printf '%s %s\n' "$(date -u +%H:%M:%S)" "${sample:-<no reply>}" | tee -a "$OUT"
    sleep "$INTERVAL"
done
