#!/bin/sh
# Selftest for tests/adb-guard.sh. Every case runs the guard in a SUBPROCESS
# (never sourced into this shell).
#
# SAFETY: no case uses a real serial or a state-changing subcommand. The serial
# is always the guard's forced NW-TEST-NO-SUCH-SERIAL and the subcommand is
# harmless (get-state), so even a completely broken guard cannot reboot or
# otherwise touch a device.
set -u
cd "$(dirname "$0")/.."
fail=0
check() { if [ "$3" = "$2" ]; then printf 'PASS %-46s (%s)\n' "$1" "$3"
          else printf 'FAIL %-46s got=%s want=%s\n' "$1" "$3" "$2"; fail=1; fi; }
# count <file> <pattern>: never `grep -c || echo 0`, which yields "0\n0"
count() { awk -v p="$2" 'index($0, p) { n++ } END { print n + 0 }' "$1" 2>/dev/null || echo 0; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT INT TERM
export ADB_GUARD_LOG="$T/attempts.log"; : > "$ADB_GUARD_LOG"
export ADB_SELFTEST_LOG="$T/selftest.log"; : > "$ADB_SELFTEST_LOG"

# 1. no TEST_ADB -> refusal fake installed, and it refuses a harmless call
sh -c '. ./tests/adb-guard.sh; adb -s NW-TEST-NO-SUCH-SERIAL get-state' >/dev/null 2>&1
check "no fixture: harmless call refused (127)" 127 "$?"
check "refusal was logged" 1 "$(count "$ADB_GUARD_LOG" get-state)"

# 2. missing fake -> the guard fails before anything runs
sh -c 'TEST_ADB=/nonexistent/adb . ./tests/adb-guard.sh' >/dev/null 2>&1
check "missing TEST_ADB fails" 1 "$?"

# 3. non-executable fake -> fails
: > "$T/nonexec"; chmod 644 "$T/nonexec"
sh -c "TEST_ADB=$T/nonexec . ./tests/adb-guard.sh" >/dev/null 2>&1
check "non-executable TEST_ADB fails" 1 "$?"

# 4. a good fake resolves and is used
cat > "$T/adb" <<'EOF'
#!/bin/sh
echo "fake-adb $*" >> "$FAKE_LOG"
exit 0
EOF
chmod 755 "$T/adb"
: > "$T/fake.log"
sh -c "TEST_ADB=$T/adb FAKE_LOG=$T/fake.log . ./tests/adb-guard.sh; adb -s NW-TEST-NO-SUCH-SERIAL get-state" >/dev/null 2>&1
check "good fake is used" 1 "$(count "$T/fake.log" get-state)"

# 5. a shadow earlier on PATH must NOT win: the fake is prepended and resolution
#    is asserted, so the guard exits 0 and the fake is the one that runs
mkdir -p "$T/shadow"; printf '#!/bin/sh\necho SHADOW_WAS_USED >> "$FAKE_LOG"\nexit 0\n' > "$T/shadow/adb"; chmod 755 "$T/shadow/adb"
: > "$T/fake2.log"
sh -c "PATH=$T/shadow:\$PATH TEST_ADB=$T/adb FAKE_LOG=$T/fake2.log . ./tests/adb-guard.sh; adb -s NW-TEST-NO-SUCH-SERIAL get-state" >/dev/null 2>&1
check "shadow is overridden: guard succeeds" 0 "$?"
check "the fake ran under the shadow" 1 "$(count "$T/fake2.log" get-state)"
check "the shadow itself was never used" 0 "$(count "$T/fake2.log" SHADOW_WAS_USED)"

# 6. genuine wrong resolution: a shell FUNCTION named adb outranks PATH, so the
#    assertion must fail before anything runs
sh -c "adb() { :; }; TEST_ADB=$T/adb . ./tests/adb-guard.sh" >/dev/null 2>&1
check "function shadowing is rejected" 1 "$?"

# 7. the ordinary attempt log holds only the deliberate case 1 call
check "ordinary log holds only the selftest call" 1 "$(count "$ADB_GUARD_LOG" get-state)"

[ "$fail" = 0 ] && echo "ALL PASS" || echo "FAILURES PRESENT"
exit "$fail"
