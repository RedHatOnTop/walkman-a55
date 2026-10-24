#!/bin/sh
# Static gate: no device-capable script may reach adb by an absolute path or
# through a variable that could hold one, so the PATH guard stays sufficient.
set -u
cd "$(dirname "$0")/.."
fail=0
check() { if [ "$3" = "$2" ]; then printf 'PASS %-50s (%s)\n' "$1" "$3"
          else printf 'FAIL %-50s got=%s want=%s\n' "$1" "$3" "$2"; fail=1; fi; }
# device-capable scripts: the collector, the capture loop, and the suites that
# drive them. tools/wdt-capture-test.sh has its own mock and is out of scope.
FILES="collect/device-collect.sh collect/capture-samples.sh tests/test-capture.sh tests/adb-guard.sh tests/run-tests.sh tests/test-symvers.sh tests/test-observe.sh tests/test-lean-collect.sh tests/test-notify-arm.sh"
n=0
for f in $FILES; do
    [ -f "$f" ] || { echo "FAIL missing $f"; fail=1; continue; }
    # an absolute path to adb, or an ADB/ADB_BIN-style variable holding one
    hits=$(awk '
        /\/usr\/bin\/adb|\/bin\/adb|\/usr\/local\/bin\/adb/ { n++ }
        /\$\{?ADB[A-Z_]*\}?[[:space:]]/ && /adb/ { n++ }
        END { print n + 0 }' "$f")
    check "no absolute/variable adb in $f" 0 "$hits"
    n=$((n + 1))
done
check "device-capable files scanned" 9 "$n"
[ "$fail" = 0 ] && echo "ALL PASS" || echo "FAILURES PRESENT"
exit "$fail"
