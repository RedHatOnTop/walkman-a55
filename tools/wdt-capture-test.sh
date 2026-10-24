#!/bin/bash
# Mock-adb regression tests for tools/wdt-capture.sh.
#
#   tools/wdt-capture-test.sh
#
# A capture helper that reports success on a failed adb call, or that lets a
# second run overwrite the first, would silently destroy the baseline last_kmsg,
# which the next reset makes unrecoverable. A fake adb on PATH drives the cases;
# no device is touched. Setup failures abort the run; the intentional nonzero
# exits under test must not, so this script does not use `set -e`.
set -u

cd "$(dirname "$0")/.." || { echo "cannot cd to repo root"; exit 1; }
TMP="$(mktemp -d)" || { echo "mktemp failed"; exit 1; }
trap 'rm -rf "$TMP"' EXIT
FAIL=0

mkdir -p "$TMP/bin" || { echo "mkdir failed"; exit 1; }
cat > "$TMP/bin/adb" <<'EOF'
#!/bin/bash
case "${MOCK_MODE:-ok}" in
    ok)   echo "mock stdout"; exit 0 ;;
    err)  echo "error: device offline" >&2; exit 42 ;;
    hang) sleep 60; exit 0 ;;
esac
EOF
chmod +x "$TMP/bin/adb"
[ -x "$TMP/bin/adb" ] || { echo "mock setup failed"; exit 1; }
PATH="$TMP/bin:$PATH" adb --version >/dev/null 2>&1 || true
case "$(PATH="$TMP/bin:$PATH" command -v adb)" in
    "$TMP/bin/adb") ;;
    *) echo "mock adb is not first on PATH, aborting"; exit 1 ;;
esac

check()
{
    local name="$1" rel="$2" actual="$3" expected="$4" ok=no
    case "$rel" in
        eq) [ "$actual" = "$expected" ] && ok=yes ;;
        ne) [ "$actual" != "$expected" ] && ok=yes ;;
    esac
    if [ "$ok" = yes ]; then
        printf 'PASS %-38s (%s)\n' "$name" "$actual"
    else
        printf 'FAIL %-38s got=%s want %s %s\n' "$name" "$actual" "$rel" "$expected"
        FAIL=1
    fi
}

run()
{
    MOCK_MODE="$1" NW_CAP_TIMEOUT="${2:-30}" PATH="$TMP/bin:$PATH" \
        ./tools/wdt-capture.sh "$3" "$4" ${5:+"$5"} >/dev/null 2>&1
    return $?
}

echo "== 1. success =="
run ok 30 "$TMP/c1" one 'echo hi'; check "success exit 0" eq "$?" 0
check "success .exit=0" eq "$(cat "$TMP/c1/one/one.exit" 2>/dev/null)" 0
check "success stdout nonempty" eq "$([ -s "$TMP/c1/one/one.out" ] && echo yes)" yes

echo "== 2. adb error must not be success =="
run err 30 "$TMP/c2" two 'echo hi'; check "adb error exit 42" eq "$?" 42
check "adb error .exit=42" eq "$(cat "$TMP/c2/two/two.exit" 2>/dev/null)" 42

echo "== 3. timeout must not be success =="
run hang 2 "$TMP/c3" three 'echo hi'; check "timeout exit 124" eq "$?" 124
check "timeout .exit=124" eq "$(cat "$TMP/c3/three/three.exit" 2>/dev/null)" 124

echo "== 4. a later capture must not overwrite an earlier one =="
run ok 30 "$TMP/c4" baseline >/dev/null 2>&1
printf 'SENTINEL\n' > "$TMP/c4/baseline/last_kmsg.out"
run ok 30 "$TMP/c4" postreset >/dev/null 2>&1
check "baseline preserved" eq "$(cat "$TMP/c4/baseline/last_kmsg.out" 2>/dev/null)" SENTINEL
check "postreset written" eq "$([ -s "$TMP/c4/postreset/last_kmsg.out" ] && echo yes)" yes

echo "== 5. a repeated label is refused =="
run ok 30 "$TMP/c4" baseline >/dev/null 2>&1; check "repeat exit 3" eq "$?" 3
check "repeat preserved baseline" eq "$(cat "$TMP/c4/baseline/last_kmsg.out" 2>/dev/null)" SENTINEL

echo "== 6. unsafe labels are refused =="
run ok 30 "$TMP/c6" '../escape' 'echo hi'; check "path label exit 2" eq "$?" 2
run ok 30 "$TMP/c6" 'a b' 'echo hi'; check "space label exit 2" eq "$?" 2

echo "== 7. the standard set must fail if a capture fails =="
run err 30 "$TMP/c7" baseline >/dev/null 2>&1; check "baseline set exit 1" eq "$?" 1
check "baseline set wrote last_kmsg .exit" eq "$(cat "$TMP/c7/baseline/last_kmsg.exit" 2>/dev/null)" 42

echo "== 8. an existing empty label is refused =="
mkdir -p "$TMP/c8/empty"
run ok 30 "$TMP/c8" empty 'echo hi'; check "empty label exit 3" eq "$?" 3

echo "== 9. an invalid or zero timeout is refused =="
run ok 0 "$TMP/c9" zero 'echo hi'; check "zero timeout exit 2" eq "$?" 2
run ok abc "$TMP/c9" text 'echo hi'; check "text timeout exit 2" eq "$?" 2

echo "== 10. an unwritable destination is refused =="
mkdir -p "$TMP/c10"; chmod 500 "$TMP/c10"
run ok 30 "$TMP/c10/sub" label 'echo hi'; check "unwritable exit 4" eq "$?" 4
chmod 700 "$TMP/c10"

# A full filesystem can let adb succeed and still lose the exit record. The
# failing printf exists only in this copied context (BASH_ENV), never real adb.
# The pattern matches the exit-record call shape (`printf '%s\n' <rc>`); the
# filename lives in the redirect, not in printf's arguments.
echo "== 11. a lost exit record is a capture failure =="
cat > "$TMP/bashenv-exit" <<'EOF'
printf() { case "$*" in '%s\n '*) return 1 ;; esac; command printf "$@"; }
EOF
MOCK_MODE=ok PATH="$TMP/bin:$PATH" BASH_ENV="$TMP/bashenv-exit" \
    bash ./tools/wdt-capture.sh "$TMP/c11" one 'echo hi' >/dev/null 2>&1
check "exit-record failure propagates" ne "$?" 0

echo "== 12. a lost hosttime write is a capture failure =="
cat > "$TMP/bashenv-host" <<'EOF'
printf() { case "$*" in *"host_start"*) return 1 ;; esac; command printf "$@"; }
EOF
MOCK_MODE=ok PATH="$TMP/bin:$PATH" BASH_ENV="$TMP/bashenv-host" \
    bash ./tools/wdt-capture.sh "$TMP/c12" baseline >/dev/null 2>&1
check "hosttime failure propagates" ne "$?" 0

echo
if [ "$FAIL" = 0 ]; then echo "ALL PASS"; else echo "FAILURES PRESENT"; fi
exit "$FAIL"
