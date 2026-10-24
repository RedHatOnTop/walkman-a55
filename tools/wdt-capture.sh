#!/bin/bash
# Read-only capture of device state for the watchdog/FIQ diagnostic.
#
#   tools/wdt-capture.sh <capture-dir> <label> [device-shell-command]
#
# Without a command, runs the standard set. Raw stdout goes to
# <dir>/<label>/<name>.out, stderr to <name>.err, and the command's own exit
# status to <name>.exit immediately, before any filtering: a pipeline would
# report the last element's status, and a timeout must not look like success.
# The script exits nonzero if any capture failed.
#
# The label directory is reserved with a checked mkdir, so any existing label —
# empty or not — is refused and a repeated baseline cannot truncate the previous
# capture. The baseline last_kmsg is destroyed by the next reset and cannot be
# re-taken.
#
# /proc/kmsg is not read here: it blocks and only one reader may hold it.
set -u

. "$(dirname "$0")/serial.sh"; SER="$(nw_serial)" || exit 2
TIMEOUT="${NW_CAP_TIMEOUT:-30}"

case "$TIMEOUT" in
    ""|*[!0-9]*)
        echo "invalid NW_CAP_TIMEOUT: '$TIMEOUT'" >&2
        exit 2
        ;;
esac
if [ "$TIMEOUT" -le 0 ]; then
    echo "NW_CAP_TIMEOUT must be > 0 (timeout 0 disables the bound)" >&2
    exit 2
fi

if [ $# -lt 2 ]; then
    echo "usage: wdt-capture.sh <capture-dir> <label> [device-shell-command]" >&2
    exit 2
fi

DIR="$1"
LABEL="$2"
shift 2
CMD="${*:-}"

case "$LABEL" in
    ""|*/*|*..*|.*|*[!A-Za-z0-9._-]*)
        echo "unsafe label: '$LABEL'" >&2
        exit 2
        ;;
esac

cd "$(dirname "$0")/.." || { echo "cannot cd to the repo root" >&2; exit 2; }

mkdir -p "$DIR" || { echo "cannot create capture dir: $DIR" >&2; exit 4; }
OUTDIR="$DIR/$LABEL"
if ! mkdir "$OUTDIR" 2>/dev/null; then
    echo "label already exists (or cannot be created), refusing: $OUTDIR" >&2
    exit 3
fi
if ! : > "$OUTDIR/.writable" 2>/dev/null; then
    echo "capture dir is not writable: $OUTDIR" >&2
    exit 4
fi
rm -f "$OUTDIR/.writable"

FAILED=0
CAP_RC=0

cap()
{
    local name="$1" cmd="$2"
    timeout "$TIMEOUT" adb -s "$SER" shell "$cmd" > "$OUTDIR/$name.out" 2> "$OUTDIR/$name.err"
    CAP_RC=$?
    # A full filesystem can lose the exit record while adb itself succeeded, so
    # the write is checked and a failure is a capture failure.
    if ! printf '%s\n' "$CAP_RC" > "$OUTDIR/$name.exit"; then
        echo "cannot write exit record: $OUTDIR/$name.exit" >&2
        FAILED=1
    fi
    [ "$CAP_RC" -ne 0 ] && FAILED=1
    printf '%-16s exit=%-3s out=%sB err=%sB\n' \
        "$name" "$CAP_RC" "$(wc -c < "$OUTDIR/$name.out")" "$(wc -c < "$OUTDIR/$name.err")"
}

if [ -n "$CMD" ]; then
    cap "$LABEL" "$CMD"
    echo "capture dir: $OUTDIR"
    [ "$CAP_RC" -ne 0 ] && exit "$CAP_RC"
    exit "$FAILED"
fi

if ! printf 'host_start %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$OUTDIR/$LABEL.hosttime"; then
    echo "cannot write hosttime: $OUTDIR/$LABEL.hosttime" >&2
    FAILED=1
fi
cap "$LABEL" 'echo "uptime=$(cut -d" " -f1 /proc/uptime)"; echo "hosttime=$(date -u +%Y-%m-%dT%H:%M:%SZ)"'
if ! printf 'host_end %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" >> "$OUTDIR/$LABEL.hosttime"; then
    echo "cannot append hosttime: $OUTDIR/$LABEL.hosttime" >&2
    FAILED=1
fi

cap last_kmsg  'cat /proc/last_kmsg'
cap dmesg      'dmesg'
cap wdk        'cat /proc/wdk'
cap uptime     'cat /proc/uptime'
cap swaps      'cat /proc/swaps'
cap meminfo    'cat /proc/meminfo'
cap loadavg    'cat /proc/loadavg'
cap version    'cat /proc/version'
cap kallsyms   'cat /proc/kallsyms'
cap ps         '/system/xbin/busybox ps'
cap mounts     'cat /proc/mounts'

echo "capture dir: $OUTDIR"
exit "$FAILED"
