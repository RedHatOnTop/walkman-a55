#!/bin/sh
# Host tests for collect/capture-samples.sh using a fake adb that serves one
# fixture per call. Never touches a device: tests/adb-guard.sh is sourced and
# FAKE_ROOT replaces the transport, and every fake invocation is logged so the
# zero-adb cases can be asserted.
set -u
cd "$(dirname "$0")/.."
fail=0
check() { if [ "$3" = "$2" ]; then printf 'PASS %-48s (%s)\n' "$1" "$3"
          else printf 'FAIL %-48s got=%s want=%s\n' "$1" "$3" "$2"; fail=1; fi; }
BOOT="1ff226ae-37ec-4f5b-9498-d0acc2345c72"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT INT TERM
BIN="$T/bin"; mkdir -p "$BIN"
cat > "$BIN/adb" <<'EOF'
#!/bin/sh
[ "${1:-}" = -s ] && [ "${2:-}" = NW-TEST-NO-SUCH-SERIAL ] || exit 98
printf '%s\n' "$*" >> "$FAKE_ROOT/calls.log"
n=$(cat "$FAKE_ROOT/seq.n" 2>/dev/null || echo 0); n=$((n + 1)); printf '%s' "$n" > "$FAKE_ROOT/seq.n"
k="$FAKE_ROOT/seq/$n"
rc=0; [ -f "$k.exit" ] && rc=$(cat "$k.exit")
if [ -f "$k.out" ]; then
    case "$*" in *--no-smaps*) sed '/^PID_SUMMARY=/d;s/^SMAPS_SCAN=1$/SMAPS_SCAN=0/' "$k.out" ;; *) cat "$k.out" ;; esac
fi
[ -f "$k.err" ] && cat "$k.err" >&2
if [ "$n" -eq 3 ] && [ -f "$FAKE_ROOT/stop-path" ]; then touch "$(cat "$FAKE_ROOT/stop-path")"; fi
exit "$rc"
EOF
chmod +x "$BIN/adb"
# a fake sleep in the same directory: the guard prepends it to PATH, so the
# loop's `sleep` resolves here and no test waits for real seconds
cat > "$BIN/sleep" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "$FAKE_ROOT/sleeps.log"
exit 0
EOF
chmod +x "$BIN/sleep"
# the guard now runs with the fake it must assert; it also forces NW_SERIAL
TEST_ADB="$BIN/adb" . ./tests/adb-guard.sh
fx() { mkdir -p "$1/seq"; printf '%s' "$2" > "$1/seq/$3.out"; }
fxe() { mkdir -p "$1/seq"; printf '%s' "$2" > "$1/seq/$3.exit"; }
sample() { # uptime [endmarker] [devicerc]
    end=$(awk -v value="$1" 'BEGIN {printf "%.2f",value+2}')
    printf 'BEGIN_SAMPLE\nUPTIME_START=%s\nBOOT_ID=%s\nBEGIN_MEMINFO\nMemFree: 1 kB\nSwapTotal: 0 kB\nSwapFree: 0 kB\nEND_MEMINFO\nBEGIN_SWAPS\nFilename\nEND_SWAPS\nBEGIN_WDK\nmode interval timeout enable\n2 10 30 1\nEND_WDK\nBOOT_COMPLETED=1\nBOOT_COMPLETED_RC=0\nSMAPS_SCAN=1\nPID_SUMMARY=1 start_before=1 start_after=1 smaps_rc=0 swap_kb=0 lines=0 comm=init\nMAX_SMAPS_PID=-\nBEGIN_MAX_SMAPS\nEND_MAX_SMAPS\nUPTIME_END=%s\nSAMPLE_DURATION_SECONDS=2.00\n%s\n%s\n' \
        "$1" "$BOOT" "$end" "${3:-DEVICE_RC=0}" "${2:-END_SAMPLE}"
}
# call 1 push, 2 chmod, then one per sample
build() { d=$1; shift; mkdir -p "$d/seq"; rm -f "$d/seq.n" "$d/calls.log"; fx "$d" "" 1; fx "$d" "COLLECT_CHMOD_RC=0" 2; c=2
    for up in "$@"; do c=$((c + 1)); fx "$d" "$(sample "$up")" "$c"; done; }
run() { # dir out [args...]
    d=$1; o=$2; shift 2
    # assert the fake resolves to this exact executable just before running the
    # script, so a shadow or a missing fake cannot silently reach a device
    TEST_ADB="$BIN/adb"
    [ -x "$TEST_ADB" ] || { echo "test-capture: fake adb missing or not executable" >&2; return 1; }
    [ "$(readlink -f "$(command -v adb 2>/dev/null)" 2>/dev/null)" = "$(readlink -f "$TEST_ADB")" ] \
        || { echo "test-capture: adb does not resolve to the fake" >&2; return 1; }
    FAKE_ROOT="$d" TEST_ADB="$TEST_ADB" NW_SERIAL=NW-TEST-NO-SUCH-SERIAL NW_OBS_INTERVAL=0 \
        ./collect/capture-samples.sh "$@" >"$o" 2>&1
    echo $?
}

# 0. the loop must not really sleep: the fake sleep is the only sleep used
d0="$T/r0"; build "$d0" 100 1900; : > "$d0/sleeps.log"
run "$d0" "$T/o0" "$T/out0" "$BOOT" 1800 30 >/dev/null
check "fake sleep was used, not a real one" 1 "$([ -s "$d0/sleeps.log" ] && echo 1 || echo 0)"

# 1. completes at the elapsed target, first sample immediate
d="$T/r1"; build "$d" 100 500 900 1300 1900
check "sequence completes at target" 0 "$(run "$d" "$T/o1" "$T/out1" "$BOOT" 1800 30)"
check "all five samples stored" 5 "$(ls "$T/out1/samples" | grep -c '\.out$')"
check "first sample up=100" 1 "$(grep -c '^UPTIME_START=100$' "$T/out1/samples/sample001.lf")"
check "no PASS claim in the log" 0 "$(grep -c 'PASS' "$T/out1/capture.log")"

# 2. missing end marker
d="$T/r2"; build "$d" 100; c=3; fx "$d" "$(sample 100 NOMARKER)" "$c"
check "missing END_SAMPLE stops nonzero" 1 "$(run "$d" "$T/o2" "$T/out2" "$BOOT" 1800 30)"

# 3. device error marker
d="$T/r3"; build "$d" 100; c=3; fx "$d" "$(printf 'BEGIN_SAMPLE\nUPTIME_START=100\nCOLLECT_ERROR=mandatory_swaps\n')" "$c"
check "COLLECT_ERROR stops nonzero" 1 "$(run "$d" "$T/o3" "$T/out3" "$BOOT" 1800 30)"

# 4. transport timeout
d="$T/r4"; build "$d" 100; fxe "$d" 124 3
check "transport timeout stops nonzero" 1 "$(run "$d" "$T/o4" "$T/out4" "$BOOT" 1800 30)"

# 5. changed boot id
d="$T/r5"; build "$d" 100; c=3; fx "$d" "$(sample 100 | sed "s/$BOOT/deadbeef-0000-0000-0000-000000000000/")" "$c"
check "boot id change stops nonzero" 1 "$(run "$d" "$T/o5" "$T/out5" "$BOOT" 1800 30)"
check "boot fixture reaches boot-id rejection" 1 "$(grep -c '!= expected' "$T/o5")"

# 6. decreasing uptime
d="$T/r6"; build "$d" 500 100
check "uptime regression stops nonzero" 1 "$(run "$d" "$T/o6" "$T/out6" "$BOOT" 1800 30)"

# 7. STOP file
d="$T/r7"; build "$d" 100 200; printf '%s\n' "$T/out7/STOP" > "$d/stop-path"
check "STOP stops nonzero" 1 "$(run "$d" "$T/o7" "$T/out7" "$BOOT" 1800 30)"
check "live STOP branch reached after first sample" 1 "$(grep -c 'STOP file present after 1 samples' "$T/o7")"
check "STOP prevents a second collection" 3 "$(wc -l < "$d/calls.log" | tr -d ' ')"

# 8. unknown flag / non-numeric arg -> ZERO adb invocations
d="$T/r8"; mkdir -p "$d"; : > "$d/calls.log"
check "unknown --dry-run refused" 1 "$(run "$d" "$T/o8" "$T/out8" "$BOOT" 1800 30 --dry-run)"
check "zero adb calls for the flag case" 0 "$(grep -c . "$d/calls.log")"
d="$T/r9"; mkdir -p "$d"; : > "$d/calls.log"
check "non-numeric seconds refused" 1 "$(run "$d" "$T/o9" "$T/out9" "$BOOT" abc 30)"
check "zero adb calls for the numeric case" 0 "$(grep -c . "$d/calls.log")"

# 9. existing output directory refused
d="$T/r10"; build "$d" 100 1900; mkdir -p "$T/out10"; : > "$d/calls.log"
check "existing output dir refused" 1 "$(run "$d" "$T/o10" "$T/out10" "$BOOT" 1800 30)"
check "zero adb calls when refusing overwrite" 0 "$(grep -c . "$d/calls.log")"

d="$T/r11"; build "$d" 100; fxe "$d" 91 2
check "chmod transport failure aborts" 1 "$(run "$d" "$T/o11" "$T/out11" "$BOOT" 1800 30)"
check "chmod failure prevents collection" 2 "$(wc -l < "$d/calls.log" | tr -d ' ')"
d="$T/r12"; build "$d" 100; fx "$d" "COLLECT_CHMOD_RC=91" 2
check "chmod remote failure aborts" 1 "$(run "$d" "$T/o12" "$T/out12" "$BOOT" 1800 30)"
check "remote chmod failure prevents collection" 2 "$(wc -l < "$d/calls.log" | tr -d ' ')"
for bad in '.' '1..2' '.5' '99.0'; do
    key=$(printf '%s' "$bad" | tr '.' '_'); d="$T/end-$key"; build "$d" 100
    fx "$d" "$(sample 100 | sed "s/^UPTIME_END=.*/UPTIME_END=$bad/")" 3
    check "malformed/reversed end $bad refused" 1 "$(run "$d" "$T/o-end-$key" "$T/out-end-$key" "$BOOT" 1800 30)"
    check "end $bad reaches time-field rejection" 1 "$(grep -c 'invalid time fields' "$T/o-end-$key")"
done
d="$T/r13"; build "$d" 100 1900
check "capture cadence completion" 0 "$(run "$d" "$T/o13" "$T/out13" "$BOOT" 1800 30)"
check "cadence subtracts collection duration" 1 "$(grep -c '^28' "$d/sleeps.log")"
check "device runtime is explicit busybox ash" 2 "$(grep -c 'shell /system/xbin/busybox sh /tmp/device-collect.sh' "$d/calls.log")"
check "duration is reported per sample" 2 "$(grep -c 'duration_s=2.00' "$T/out13/capture.log")"

d="$T/r14"; build "$d" 100 1900
fx "$d" "$(sample 100 | sed 's/UPTIME_END=102.00/UPTIME_END=150.00/;s/SAMPLE_DURATION_SECONDS=2.00/SAMPLE_DURATION_SECONDS=50.00/')" 3
check "cadence overrun still completes" 0 "$(run "$d" "$T/o14" "$T/out14" "$BOOT" 1800 30)"
check "overrun grants a full rest interval" 1 "$(grep -c '^30' "$d/sleeps.log")"
check "overrun is explicitly reported" 1 "$(grep -c 'overrun_s=20.00' "$T/out14/capture.log")"

[ "$fail" = 0 ] && echo "ALL PASS" || echo "FAILURES PRESENT"
exit "$fail"
