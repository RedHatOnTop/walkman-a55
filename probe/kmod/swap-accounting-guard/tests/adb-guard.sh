# adb-guard.sh - sourced by every host suite. It guarantees that no host test
# can reach a real device.
#
# INCIDENT 2026-10-12: a host test invoked the rewritten all-in-one runner with
# an argument it no longer supported (--dry-run). The runner ignored the extra
# argv, fell through to its real path, and its `adb ... reboot` REBOOTED THE
# USER'S WALKMAN at ~10:38:13Z inside the test window (10:38:07.738 ->
# 10:38:34.641). The postreset last_kmsg shows the ordinary adbd path, not a
# watchdog bite.
#
# Contract:
#   TEST_ADB  the test must create an executable fake and export its absolute
#             path. It is NOT a bypass: the guard prepends its directory and
#             then asserts that `command -v adb` resolves to exactly that file.
#   unset TEST_ADB -> a refusal fake is installed, so any device access fails
#             loudly and is logged.
#   ADB_GUARD_LOG     ordinary attempts (must stay empty in a green run).
#   ADB_SELFTEST_LOG  expected-refusal calls made by the guard's own selftest,
#             kept separate so the ordinary log stays a clean zero.
#   NW_SERIAL is forced to a deliberately nonexistent serial.
# A failure here exits the sourcing script before it can run anything.

_adb_guard_install_refusal() {
    _ag_dir=$(mktemp -d "${TMPDIR:-/tmp}/adb-guard.XXXXXX") || return 1
    cat > "$_ag_dir/adb" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >> "${ADB_GUARD_LOG:-/tmp/adb-guard.log}"
echo "adb-guard: REFUSING real adb from a host test: $*" >&2
exit 127
EOF
    chmod 755 "$_ag_dir/adb" || return 1
    [ -x "$_ag_dir/adb" ] || return 1
    PATH="$_ag_dir:$PATH"
    export PATH
    _ag_want="$_ag_dir/adb"
    return 0
}

if [ -n "${TEST_ADB:-}" ]; then
    case "$TEST_ADB" in
        /*) ;;
        *) echo "adb-guard: TEST_ADB must be an absolute path: $TEST_ADB" >&2; exit 1 ;;
    esac
    if [ ! -f "$TEST_ADB" ]; then
        echo "adb-guard: TEST_ADB does not exist: $TEST_ADB" >&2; exit 1
    fi
    if [ ! -x "$TEST_ADB" ]; then
        echo "adb-guard: TEST_ADB is not executable: $TEST_ADB" >&2; exit 1
    fi
    PATH="$(dirname "$TEST_ADB"):$PATH"
    export PATH
    _ag_want="$TEST_ADB"
else
    _adb_guard_install_refusal || { echo "adb-guard: cannot install the refusal fake" >&2; exit 1; }
fi

# Resolution must be exactly the intended fake: quoting PATH is not proof.
_ag_resolved=$(command -v adb 2>/dev/null) || {
    echo "adb-guard: adb does not resolve on PATH" >&2; exit 1; }
[ "$_ag_resolved" = "$_ag_want" ] || {
    echo "adb-guard: command -v adb is '$_ag_resolved', expected '$_ag_want'" >&2; exit 1; }
_ag_got=$(readlink -f "$_ag_resolved" 2>/dev/null)
_ag_expect=$(readlink -f "$_ag_want" 2>/dev/null)
if [ -z "$_ag_got" ] || [ "$_ag_got" != "$_ag_expect" ]; then
    echo "adb-guard: adb resolves to '${_ag_got:-nothing}', expected '$_ag_expect'" >&2
    exit 1
fi

# A real serial must never be reachable from a test.
NW_SERIAL="NW-TEST-NO-SUCH-SERIAL"
export NW_SERIAL
