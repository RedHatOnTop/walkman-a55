#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
. ./tests/adb-guard.sh
HERE=$(pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/notify-arm.XXXXXX")
trap 'rm -rf "$TMP"' 0
trap 'exit 1' 2 15
CC=${CC:-gcc}
CROSS_COMPILE=${CROSS_COMPILE:-$HOME/.cache/arm-eabi-4.8/bin/arm-eabi-}
VMLINUX=${NW_VMLINUX:-$HERE/../../../nwa50-extracted/kernel/vmlinux.bin}
KALLSYMS=${NW_KALLSYMS:-$HERE/../../../work/android/wdt-nozram-20261012T062210Z/baseline/kallsyms.out}
if ! env -u PYTHONHOME -u PYTHONPATH NW_VMLINUX="$VMLINUX" NW_KALLSYMS="$KALLSYMS" \
    python3 -m unittest discover -s tests/notify-arm -p 'test_generate.py' > "$TMP/generate-tests.log" 2>&1; then
    cat "$TMP/generate-tests.log"; exit 1
fi
cat "$TMP/generate-tests.log"
grep -Eq '^Ran [1-9][0-9]* tests' "$TMP/generate-tests.log" || { echo 'FAIL zero generator tests'; exit 1; }
$CC -Wall -Wextra -Werror -O1 tests/dump_spec.c -o "$TMP/dump-spec"
"$TMP/dump-spec" > "$TMP/spec.txt"
env -u PYTHONHOME -u PYTHONPATH python3 tests/notify-arm/generate.py \
    --spec "$TMP/spec.txt" --vmlinux "$VMLINUX" --kallsyms "$KALLSYMS" --out "$TMP"
cp tests/notify-arm/notify-test.S tests/notify-arm/notify-test.ld "$TMP/"
cd "$TMP"
for case in stock stock-nop trylock trylock-nop; do
    set --
    case "$case" in stock*) set -- "$@" --defsym STOCK=1 ;; esac
    case "$case" in *-nop) set -- "$@" --defsym FTRACE_NOP=1 ;; esac
    "${CROSS_COMPILE}as" -mcpu=cortex-a7 "$@" notify-test.S -o "$case.o"
    "${CROSS_COMPILE}ld" -T notify-test.ld "$case.o" -o "$case.elf"
    rc=0
    timeout 30 docker run --rm --network none --cap-drop ALL --security-opt no-new-privileges \
        --read-only --pids-limit 64 -u "$(id -u):$(id -g)" -v "$TMP":/t:ro \
        nw-qemu-arm:1 qemu-arm-static "/t/$case.elf" > "$case.log" 2>&1 || rc=$?
    if [ "$case" = stock ] || [ "$case" = stock-nop ]; then
        [ "$rc" -eq 1 ] && grep -q '^FAIL: ARM callback/return/callee argument/state check$' "$case.log" || {
            cat "$case.log"; echo "FAIL stock negative control exit=$rc"; exit 1; }
        echo "PASS ARM $case negative control: forbidden blocking call rejected"
    else
        cat "$case.log"
        [ "$rc" -eq 0 ] && grep -q '^PASS: ARM stock trylock 1/0; callback 5 cases;' "$case.log" || {
            echo "FAIL ARM $case candidate exit=$rc"; exit 1; }
        echo "PASS ARM variant $case: BL/NOP callback and public wrappers"
    fi
done
