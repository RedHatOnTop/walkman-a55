# observe-lib.sh - predicates for the patched+zram observation run.
#
# Sourced by tools/patched-zram-observe.sh and driven by tests/test-observe.sh
# with real fixtures. Every predicate FAILS CLOSED: a missing, empty, malformed
# or non-numeric input is a failure, never a pass.

OL_MIN_SECONDS=1800

# The whole dmesg of this boot must exist and contain no historical swap add.
# totalswap==0 cannot see a lock leaked by an earlier swapon/swapoff.
ol_no_prior_swap() {
    [ -s "${1:-}" ] || return 1
    ! grep -qE 'Adding [0-9]+k swap' "$1"
}

# /proc/swaps has no device rows AND SwapTotal parses as exactly 0.
ol_swap_absent() {
    [ -s "${1:-}" ] || return 1
    [ -s "${2:-}" ] || return 1
    [ "$(awk 'NR>1 && NF' "$1" | wc -l)" -eq 0 ] || return 1
    awk '/^SwapTotal:/ { found = 1; exit ($2 == 0) ? 0 : 1 } END { if (!found) exit 1 }' "$2"
}

# A numeric, finite uptime.
ol_numeric() {
    case "${1:-}" in
        ''|*[!0-9.]*) return 1 ;;
        *.*.*) return 1 ;;
    esac
    awk -v v="$1" 'BEGIN { exit (v >= 0) ? 0 : 1 }'
}

# >= OL_MIN_SECONDS of actual device monotonic time since the workload launch.
ol_elapsed_ok() {
    ol_numeric "$1" && ol_numeric "$2" || return 1
    awk -v a="$1" -v b="$2" -v m="$OL_MIN_SECONDS" 'BEGIN { exit (b - a >= m) ? 0 : 1 }'
}

# Uptime must not go backwards: a reset invalidates the run.
ol_boot_continuous() {
    ol_numeric "$1" && ol_numeric "$2" || return 1
    awk -v a="$1" -v b="$2" 'BEGIN { exit (b >= a) ? 0 : 1 }'
}

# A kallsyms row with exactly this address and name. kallsyms rows are
# "c0150448 T swap_info_get" (optionally " [module]"), with no leading space
# before the address, so the whole row is parsed rather than substring-matched.
ol_symbol_at() {
    awk -v want="$2" -v name="$3" '
        $1 == want && $3 == name { found = 1 }
        END { exit found ? 0 : 1 }' "$1" 2>/dev/null
}

# The watchdog mode file's final row is exactly "2 10 30 1" for `2 10 30 0 1`.
ol_wdk_ok() {
    [ -s "${1:-}" ] || return 1
    awk 'NF { last = $1" "$2" "$3" "$4 } END { exit (last == "2 10 30 1") ? 0 : 1 }' "$1"
}

# The mode register actually printed for that request: 0x1d.
ol_wdt_mode_ok() {
    [ -s "${1:-}" ] || return 1
    grep -qE 'mtk_wdt_mode_config  mode=1d' "$1"
}

# A successful complete smaps read whose ordinary Swap: fields sum above zero.
ol_smaps_hit() {
    [ -s "${1:-}" ] || return 1
    grep -q '^Swap:' "$1" || return 1
    awk '/^Swap:/ { s += $2 } END { exit (s > 0) ? 0 : 1 }' "$1"
}

# The module's own log contract: two UNIQUE site lines (by symbol and address)
# plus the summary. The module prints no "armed" line, so requiring one would
# reject the real module. Duplicated words are not counted as two sites.
ol_module_ok() {
    d=${1:-}
    [ -s "$d" ] || return 1

    # exactly one line per site, each with the full verified field set
    for site in "0 smaps_pte_range" "1 kpageswapn_read"; do
        n=$(grep -c "swap-accounting-guard: site $site" "$d")
        [ "$n" -eq 1 ] || return 1
    done
    grep -q 'site 0 smaps_pte_range+0xd4 addr c01acb90 old ebfe8e2c' "$d" || return 1
    grep -q 'site 1 kpageswapn_read+0x38 addr c01b99a4 old ebfe5aa7' "$d" || return 1
    [ "$(grep -c 'new e3a00000 write_rc 0' "$d")" -eq 2 ] || return 1
    [ "$(grep -c 'patch_readback e3a00000 patch_readback_ok 1 patched 1' "$d")" -eq 2 ] || return 1
    [ "$(grep -c 'rw_attempted 1 ro_attempted 1 ro_verified 1' "$d")" -eq 2 ] || return 1
    [ "$(grep -c 'rollback_write_rc 0' "$d")" -eq 2 ] || return 1

    # the summary: both writes, no rollback, no failures, both pages restored
    grep -qE 'writes 2/2 rollback 0/0 failed 0 ' "$d" || return 1
    grep -qE 'ro_failed 0 stopped 1 ran 1' "$d" || return 1
    ! grep -q 'refused (' "$d"
}
