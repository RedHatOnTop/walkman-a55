#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
. ./tests/adb-guard.sh
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fail=0
check() {
    if [ "$3" = "$2" ]; then printf 'PASS %-52s (%s)\n' "$1" "$3"
    else printf 'FAIL %-52s got=%s want=%s\n' "$1" "$3" "$2"; fail=1; fi
}
count() { awk -v pattern="$2" '$0 ~ pattern {n++} END {print n+0}' "$1"; }
stat_file() {
    awk -v pid="$2" -v comm="$3" -v tick="$4" '
        { sub(/^.*\) /, ""); n=split($0,f," "); f[20]=tick
          printf "%s (%s)",pid,comm
          for(i=1;i<=n;i++) printf " %s",f[i]
          printf "\n" }' tests/fixtures/proc-stat.txt > "$1"
}
fixture() {
    PROC="$T/$1/proc"; TMP="$T/$1/tmp"; STATE="$T/$1/state"
    mkdir -p "$PROC/sys/kernel/random" "$PROC/465" "$PROC/900" "$TMP"
    printf '100.50 200.00\n' > "$PROC/uptime"
    printf '11111111-2222-3333-4444-555555555555\n' > "$PROC/sys/kernel/random/boot_id"
    printf 'MemFree: 13304 kB\nSwapTotal: 262140 kB\nSwapFree: 262012 kB\n' > "$PROC/meminfo"
    printf 'Filename Type Size Used Priority\n/dev/block/zram0 partition 262140 128 -1\n' > "$PROC/swaps"
    printf 'mode interval timeout enable\n2 10 30 1\n' > "$PROC/wdk"
    printf 'tmpfs %s tmpfs rw 0 0\n' "$TMP" > "$PROC/mounts"
    stat_file "$PROC/465/stat" 465 'system server (test)' 4242
    stat_file "$PROC/900/stat" 900 pss_only 5555
    printf 'system server (test)\n' > "$PROC/465/comm"
    printf 'pss_only\n' > "$PROC/900/comm"
    printf 'Size: 4096 kB\nSwap: 128 kB\nSwapPss: 7 kB\n' > "$PROC/465/smaps"
    printf 'Size: 1024 kB\nSwap: 0 kB\nSwapPss: 999 kB\n' > "$PROC/900/smaps"
    stat_file "$PROC/465/after-stat" 465 reused 7777
    : > "$PROC/pivot.log"
}
cat > "$T/bb" <<'BB'
#!/bin/sh
set -eu
op=$1; shift
last=''; for argument in "$@"; do last=$argument; done
case "${FAULT:-}:$op:$last" in
    summary:awk:*/current|copy:cp:*|raw:cat:*/max|cleanup:rm:*) exit 91 ;;
    bad_summary:awk:*/current) printf ": :\n"; exit 0 ;;
esac
if [ "$op" = awk ]; then
    case "$*" in *'function scan('*)
        case "${FAULT:-}" in
            summary) exit 91 ;;
            bad_summary) printf ': :\n'; exit 0 ;;
            signal) kill -TERM "$PPID"; exit 91 ;;
            copy)
                for argument in "$@"; do case "$argument" in maximum=*) ln -s /dev/full "${argument#maximum=}" ;; esac; done ;;
        esac ;;
    esac
fi
if [ "${FAULT:-}" = duration ] && [ "$op" = awk ]; then
    case "$*" in *'b - a'*) exit 91;; esac
fi
if [ "$op" = cut ]; then
    n=0; [ ! -f "$TEST_STATE" ] || n=$(cat "$TEST_STATE")
    n=$((n+1)); printf '%s\n' "$n" > "$TEST_STATE"
    if [ "${FAULT:-}" = uptime_end ] && [ "$n" -eq 2 ]; then exit 91; fi
fi
if [ "$op" = cat ] && [ "$last" = "$PROC_ROOT/465/smaps" ]; then
    case "${FAULT:-}" in
        reuse) cat "$last"; cp "$PROC_ROOT/465/after-stat" "$PROC_ROOT/465/stat"; exit 0 ;;
        signal) kill -TERM "$PPID"; exit 91 ;;
    esac
fi
exec "$op" "$@"
BB
chmod 755 "$T/bb"
cat > "$T/numeric-sh" <<'COMPAT'
#!/bin/sh
trap() {
    action=$1; shift
    for signal in "$@"; do
        case "$signal" in ''|*[!0-9]*) printf 'numeric trap required\n' >&2; return 1 ;; esac
    done
    command trap "$action" "$@"
}
. ./collect/device-collect.sh
COMPAT
cat > "$T/pivotinit" <<'PIVOT'
#!/bin/sh
printf 'called\n' >> "$PIVOT_LOG"
exit 42
PIVOT
chmod 755 "$T/pivotinit"
run() {
    OUT="$T/$1.out"; ERR="$T/$1.err"
    if BB="$T/bb" PROC_ROOT="$PROC" TMP_ROOT="$TMP" TEST_STATE="$STATE" FAULT="${2:-}" INIT_PID_FILE="$PROC/init.pid" PIVOTINIT="$T/pivotinit" PIVOT_LOG="$PROC/pivot.log" \
        "${COLLECT_SHELL:-sh}" "${COLLECT_ENTRY:-collect/device-collect.sh}" ${COLLECT_ARGUMENT:-} > "$OUT" 2> "$ERR"; then RC=0; else RC=$?; fi
}
no_success() {
    [ "$RC" -ne 0 ] && nonzero=1 || nonzero=0
    check "$1 exits nonzero" 1 "$nonzero"
    check "$1 has no success marker" 0 "$(count "$OUT" '^DEVICE_RC=0$|^END_SAMPLE$')"
}
fixture happy; run happy
check 'collector happy path' 0 "$RC"
check 'two summaries, no invented PID' 2 "$(count "$OUT" '^PID_SUMMARY=')"
check 'native field22, spaces and parentheses' 1 "$(count "$OUT" '^PID_SUMMARY=465 start_before=4242 start_after=4242 ')"
check 'ordinary Swap sum' 1 "$(count "$OUT" '^PID_SUMMARY=465 .*swap_kb=128 lines=3 ')"
check 'SwapPss is excluded' 1 "$(count "$OUT" '^PID_SUMMARY=900 .*swap_kb=0 lines=3 ')"
check 'maximum PID' 1 "$(count "$OUT" '^MAX_SMAPS_PID=465$')"
check 'only one raw frame' 2 "$(count "$OUT" '^BEGIN_MAX_SMAPS$|^END_MAX_SMAPS$')"
check 'raw maximum matches summary' 1 "$(count "$OUT" '^Swap: 128 kB$')"
check 'duration uses device uptime' 1 "$(count "$OUT" '^SAMPLE_DURATION_SECONDS=0.00$')"
check 'normal exit removes buffers' 0 "$(find "$TMP" -mindepth 1 | wc -l | tr -d ' ')"
fixture property; printf '123\n' > "$PROC/init.pid"; run property
check 'property query failure does not end sample' 0 "$RC"
check 'property query was actually invoked' 1 "$(count "$PROC/pivot.log" '^called$')"
check 'property failure status is recorded' 1 "$(count "$OUT" '^BOOT_COMPLETED_RC=42$')"
check 'property failure retains completed sample' 2 "$(count "$OUT" '^DEVICE_RC=0$|^END_SAMPLE$')"
fixture light; COLLECT_ARGUMENT=--no-smaps; run light
check 'light sample has no smaps reads' 0 "$(count "$OUT" '^PID_SUMMARY=')"
check 'light scan marker is explicit' 1 "$(count "$OUT" '^SMAPS_SCAN=0$')"
check 'light sample remains complete' 2 "$(count "$OUT" '^DEVICE_RC=0$|^END_SAMPLE$')"
unset COLLECT_ARGUMENT
fixture numeric; COLLECT_ENTRY="$T/numeric-sh"; COLLECT_SHELL=bash; run numeric
check 'device numeric-only trap compatibility' 0 "$RC"
unset COLLECT_ENTRY COLLECT_SHELL
fixture empty; : > "$PROC/465/smaps"; : > "$PROC/900/smaps"; run empty
check 'empty smaps retains zero nonnegative sums' 2 "$(count "$OUT" 'smaps_rc=0 swap_kb=0 lines=0 ')"
check 'empty smaps has no raw maximum' 1 "$(count "$OUT" '^MAX_SMAPS_PID=-$')"
fixture reuse; mv "$PROC/465/smaps" "$PROC/465/smaps.saved"; mkfifo "$PROC/465/smaps"
(cat "$PROC/465/smaps.saved"; cp "$PROC/465/after-stat" "$PROC/465/stat") > "$PROC/465/smaps" &
writer=$!
run reuse; wait "$writer"
check 'PID reuse has actual before/after mismatch' 1 "$(count "$OUT" '^PID_SUMMARY=465 start_before=4242 start_after=7777 ')"
check 'reused PID excluded, eligible zero PID retained' 1 "$(count "$OUT" '^MAX_SMAPS_PID=900$')"
fixture dead; rm "$PROC/465/stat" "$PROC/465/smaps" "$PROC/465/comm"; run dead
check 'expected PID exit has no stray diagnostic' 0 "$(wc -l < "$ERR" | tr -d ' ')"
check 'exited PID identity recorded as unknown' 1 "$(count "$OUT" '^PID_SUMMARY=465 start_before=- start_after=- ')"
check 'failed read excluded from maximum' 1 "$(count "$OUT" '^MAX_SMAPS_PID=900$')"
fixture wrongpid; stat_file "$PROC/465/stat" 466 wrong 4242; run wrongpid
check 'stat PID mismatch cannot establish identity' 1 "$(count "$OUT" '^PID_SUMMARY=465 start_before=- start_after=- ')"
fixture missing; rm "$PROC/meminfo"; run missing; no_success 'mandatory read failure'
check 'mandatory error visible on stderr' 1 "$(count "$ERR" 'COLLECT_ERROR=meminfo')"
fixture disk; printf 'ext4 %s ext4 rw 0 0\n' "$TMP" > "$PROC/mounts"; run disk
no_success 'non-tmpfs buffer'; check 'no writes on non-tmpfs refusal' 0 "$(find "$TMP" -mindepth 1 | wc -l | tr -d ' ')"
for fault in summary bad_summary copy raw duration uptime_end cleanup signal; do
    fixture "$fault"; run "$fault" "$fault"; no_success "$fault failure"
done
[ "$fail" -eq 0 ] && echo 'ALL PASS' || echo 'FAILURES PRESENT'
exit "$fail"
