#!/bin/bash
# Watch the last seconds before a container run goes silent, by streaming from
# one long-lived adb shell instead of polling it.
#
#   tools/stream-25s.sh [OUTFILE]
#
# Why streaming: polling costs four adb round trips per row and a stamp is not
# when its data was read, which made the earlier timing read wrong. One shell
# loop on the device at 1 s intervals removes both problems, and its output is
# appended to a host file so the last lines survive a reset.
#
# Each tick prints:
#   uptime, loadavg (all five fields, so field 4 running/total and field 5 last
#   pid show a fork storm or a mass kill), MemFree/Cached, every task in D state
#   as comm:wchan, and the pids of surfaceflinger, system_server, logd and zygote
#   (GONE means userland died rather than froze).
#
# The D-state list uses a single `grep -l` over /proc/[0-9]*/stat and takes the
# pid from the path. A `sed` per process would fork hundreds of times a second
# on two cores, in exactly the condition being studied.
#
# Every fifth tick it also writes 'w' to /proc/sysrq-trigger (only if that node
# exists), which dumps blocked-task stacks into the kernel log -- key 'w' only,
# no other sysrq key is ever written.
#
# The device-side loop runs as root in Sony's namespace; it reads /proc and
# writes only that one sysrq key.
set -u

cd "$(dirname "$0")/.."
OUT="${1:-/tmp/nw-a55-stream.log}"
. tools/serial.sh; SERIAL="$(nw_serial)" || exit 1
A=(adb -s "$SERIAL")

echo "# stream-25s start $(date -u +%Y-%m-%dT%H:%M:%SZ)" | tee -a "$OUT"
"${A[@]}" shell 'BB=/system/xbin/busybox
i=0
while :; do
    up=$(cut -d" " -f1 /proc/uptime)
    load=$($BB cat /proc/loadavg)
    mem=$($BB grep -E "^(MemFree|Cached):" /proc/meminfo | $BB tr -s " " | $BB tr "\n" " ")
    d=""
    for f in $($BB grep -l -E "^[0-9]+ \(.*\) D " /proc/[0-9]*/stat 2>/dev/null); do
        p=${f#/proc/}; p=${p%/stat}
        d="$d$($BB cat /proc/$p/comm 2>/dev/null):$($BB cat /proc/$p/wchan 2>/dev/null) "
    done
    alive=""
    for n in surfaceflinger system_server logd zygote; do
        p=$($BB pidof $n 2>/dev/null)
        alive="$alive$n=${p:-GONE} "
    done
    echo "$(date -u +%H:%M:%S) up=$up load=$load $mem D=[$d] pids=[$alive]"
    i=$((i+1))
    if [ $((i % 5)) -eq 0 ] && [ -e /proc/sysrq-trigger ]; then
        echo w > /proc/sysrq-trigger
    fi
    sleep 1
done' >> "$OUT" 2>&1
