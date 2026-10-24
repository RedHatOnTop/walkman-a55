#!/bin/sh
# Capture only. On failure the operator must collect reset evidence before recovery.
set -u
DIR=''
die() {
    message="$(date -u +%Y-%m-%dT%H:%M:%SZ) STOP: $*"
    printf '%s\n' "$message" >&2
    if [ -n "$DIR" ] && [ -f "$DIR/capture.log" ]; then
        printf '%s\n' "$message" >> "$DIR/capture.log" || printf 'CAPTURE_LOG_WRITE_FAILED\n' >&2
    fi
    exit 1
}
log() {
    message="$(date -u +%Y-%m-%dT%H:%M:%SZ) $*"
    printf '%s\n' "$message" >> "$DIR/capture.log" || die 'cannot append capture log'
    printf '%s\n' "$message" || die 'cannot write capture output'
}
field() {
    awk -v prefix="$2" 'index($0,prefix)==1 {n++;value=substr($0,length(prefix)+1)}
        END {if(n!=1) exit 1; print value}' "$1"
}

[ $# -ge 2 ] && [ $# -le 4 ] || die 'usage: capture-samples.sh <new-out-dir> <expected-boot-id> [seconds] [interval]'
# Unsupported dry-run must never fall through to the device path.
for argument in "$@"; do case "$argument" in -*) die "unknown flag '$argument'" ;; esac; done
DIR=$1; EXPECT_BOOT=$2; SECONDS_TARGET=${3:-1860}; INTERVAL=${4:-30}
printf '%s\n' "$EXPECT_BOOT" | grep -Eq '^[0-9a-fA-F]{8}(-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}$' \
    || die 'expected boot ID is not a UUID'
for value in "$SECONDS_TARGET" "$INTERVAL"; do
    case "$value" in ''|*[!0-9]*) die 'seconds/interval must be positive integers' ;; esac
    [ "$value" -gt 0 ] || die 'seconds/interval must be positive integers'
done
[ ! -e "$DIR" ] || die "output directory already exists, refusing to overwrite: $DIR"
HERE=$(cd "$(dirname "$0")/.." && pwd) || die 'cannot resolve collector directory'
SER="${NW_SERIAL:-$(sed -n '1{s/[[:space:]]//g;p}' "$HERE/../../../work/serial" 2>/dev/null)}"
[ -n "$SER" ] || die 'no Walkman serial: set NW_SERIAL or put it in work/serial'
mkdir -p "$DIR/samples" || die 'cannot create sample directory'
: > "$DIR/capture.log" || die 'cannot create capture log'
timeout 30 adb -s "$SER" push "$HERE/collect/device-collect.sh" /tmp/device-collect.sh \
    > "$DIR/push.out" 2> "$DIR/push.err" || die 'cannot push collector'
timeout 30 adb -s "$SER" shell 'chmod 755 /tmp/device-collect.sh; rc=$?; echo COLLECT_CHMOD_RC=$rc; exit $rc' \
    > "$DIR/chmod.out" 2> "$DIR/chmod.err" || die 'chmod transport failure'
tr -d '\r' < "$DIR/chmod.out" > "$DIR/chmod.lf" || die 'cannot normalize chmod output'
[ "$(field "$DIR/chmod.lf" COLLECT_CHMOD_RC=)" = 0 ] || die 'chmod remote failure'

SMAPS_EVERY=5
START_UP=''; previous_end=''; n=0
while :; do
    [ ! -e "$DIR/STOP" ] || die "STOP file present after $n samples"
    n=$((n+1)); tag=$(printf 'sample%03d' "$n")
    iso_start=$(date -u +%Y-%m-%dT%H:%M:%SZ)
    host_start=$(date +%s)
    scan=0; scan_argument=--no-smaps
    if [ "$(((n-1)%SMAPS_EVERY))" -eq 0 ]; then scan=1; scan_argument=''; fi
    timeout 120 adb -s "$SER" shell /system/xbin/busybox sh /tmp/device-collect.sh $scan_argument > "$DIR/samples/$tag.out" 2> "$DIR/samples/$tag.err"
    rc=$?
    printf '%s\n' "$rc" > "$DIR/samples/$tag.transport-exit" || die "$tag cannot write status"
    printf 'host_start=%s\nhost_end=%s\n' "$iso_start" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
        > "$DIR/samples/$tag.times" || die "$tag cannot write times"
    [ "$rc" -eq 0 ] || die "$tag transport exit $rc"
    F="$DIR/samples/$tag.lf"
    tr -d '\r' < "$DIR/samples/$tag.out" > "$F" || die "$tag cannot normalize output"
    for marker in BEGIN_SAMPLE END_SAMPLE BEGIN_MEMINFO END_MEMINFO BEGIN_SWAPS END_SWAPS BEGIN_WDK END_WDK BEGIN_MAX_SMAPS END_MAX_SMAPS; do
        [ "$(grep -c "^$marker$" "$F")" = 1 ] || die "$tag missing/duplicate marker $marker"
    done
    [ "$(head -n1 "$F")" = BEGIN_SAMPLE ] && [ "$(tail -n1 "$F")" = END_SAMPLE ] || die "$tag incomplete sample"
    [ "$(field "$F" DEVICE_RC=)" = 0 ] || die "$tag device sample failure"
    ! grep -q '^COLLECT_ERROR=' "$F" || die "$tag collector error"
    [ "$(field "$F" SMAPS_SCAN=)" = "$scan" ] || die "$tag wrong scan mode"
    up=$(field "$F" UPTIME_START=) || die "$tag missing uptime start"
    end=$(field "$F" UPTIME_END=) || die "$tag missing uptime end"
    duration=$(field "$F" SAMPLE_DURATION_SECONDS=) || die "$tag missing duration"
    bid=$(field "$F" BOOT_ID=) || die "$tag missing boot ID"
    awk -v a="$up" -v b="$end" -v d="$duration" 'BEGIN {
        pattern="^[0-9]+([.][0-9]+)?$"
        diff=d-(b-a); if(diff<0) diff=-diff
        exit (a~pattern && b~pattern && d~pattern && b>=a && diff<=0.011) ? 0 : 1
    }' || die "$tag invalid time fields"
    [ "$bid" = "$EXPECT_BOOT" ] || die "$tag boot id '$bid' != expected '$EXPECT_BOOT'"
    if [ -n "$previous_end" ]; then
        awk -v a="$previous_end" -v b="$up" 'BEGIN {exit b>=a ? 0 : 1}' || die "$tag uptime went backwards"
    fi
    [ -n "$START_UP" ] || START_UP=$up
    previous_end=$end
    counts=$(awk '/^PID_SUMMARY=/ {n++; if($4=="smaps_rc=0") ok++} END {printf "pids=%d reads_ok=%d",n,ok}' "$F") \
        || die "$tag cannot summarize counts"
    log "$tag captured up=$up end=$end duration_s=$duration $counts"
    awk -v a="$START_UP" -v b="$up" -v target="$SECONDS_TARGET" 'BEGIN {exit b-a>=target ? 0 : 1}' && break
    host_elapsed=$(($(date +%s)-host_start))
    cadence=$(awk -v host="$host_elapsed" -v device="$duration" -v interval="$INTERVAL" 'BEGIN {
        elapsed=host>device ? host : device
        if(elapsed>interval) printf "%s %.2f",interval,elapsed-interval
        else printf "%.2f 0",interval-elapsed
    }') || die "$tag cannot compute cadence"
    set -- $cadence
    [ "$#" -eq 2 ] || die "$tag invalid cadence"
    # An overrun gets a full rest interval, never overlapping or continuous walks.
    log "$tag cadence sleep_s=$1 overrun_s=$2"
    sleep "$1" || die 'sleep interrupted'
done
log "capture complete: $n samples from device uptime $START_UP (no verdict)"
