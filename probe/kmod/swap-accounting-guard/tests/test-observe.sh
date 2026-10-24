#!/bin/sh
# Host tests for the observation predicates (tools/observe-lib.sh) and for the
# runner's dry mode. Fixtures are real files with real contents; each case
# asserts the predicate's decision, not just an exit code.
set -u
cd "$(dirname "$0")/.."
. ./tests/adb-guard.sh
. ./tools/observe-lib.sh
fail=0
D=$(mktemp -d); trap 'rm -rf "$D"' EXIT INT TERM

check() { # name want got
    if [ "$3" = "$2" ]; then printf 'PASS %-48s (%s)\n' "$1" "$3"
    else printf 'FAIL %-48s got=%s want=%s\n' "$1" "$3" "$2"; fail=1; fi
}
yn() { if "$@" >/dev/null 2>&1; then echo 0; else echo 1; fi; }

# --- prior swap: the real fresh-boot gate --------------------------------
printf 'Booting Linux\nzram: Initialization done!\n' > "$D/dm_clean"
printf 'Booting Linux\nAdding 262140k swap on /dev/block/zram0.\n' > "$D/dm_swap"
check "clean dmesg has no prior swap" 0 "$(yn ol_no_prior_swap "$D/dm_clean")"
check "historical Adding-swap is caught" 1 "$(yn ol_no_prior_swap "$D/dm_swap")"

# --- swap state ----------------------------------------------------------
printf 'Filename\t\t\t\tType\t\tSize\tUsed\tPriority\n' > "$D/sw_empty"
printf 'Filename\t\t\t\tType\t\tSize\tUsed\tPriority\n/dev/block/zram0 partition 262140 144 -1\n' > "$D/sw_one"
printf 'SwapTotal:             0 kB\n' > "$D/mi_0"
printf 'SwapTotal:        262140 kB\n' > "$D/mi_262"
check "empty swaps + SwapTotal 0" 0 "$(yn ol_swap_absent "$D/sw_empty" "$D/mi_0")"
check "one swap row refused" 1 "$(yn ol_swap_absent "$D/sw_one" "$D/mi_0")"
check "nonzero SwapTotal refused" 1 "$(yn ol_swap_absent "$D/sw_empty" "$D/mi_262")"

# --- module logs ---------------------------------------------------------
cat > "$D/mod_ok" <<'EOF'
swap-accounting-guard: site 0 smaps_pte_range+0xd4 addr c01acb90 old ebfe8e2c old_read_ok 1 new e3a00000 write_rc 0 patch_readback e3a00000 patch_readback_ok 1 patched 1 rw_attempted 1 ro_attempted 1 ro_verified 1 rollback_write_rc 0 rollback_readback 0 rollback_read_ok 0 rolled_back 0
swap-accounting-guard: site 1 kpageswapn_read+0x38 addr c01b99a4 old ebfe5aa7 old_read_ok 1 new e3a00000 write_rc 0 patch_readback e3a00000 patch_readback_ok 1 patched 1 rw_attempted 1 ro_attempted 1 ro_verified 1 rollback_write_rc 0 rollback_readback 0 rollback_read_ok 0 rolled_back 0
swap-accounting-guard: writes 2/2 rollback 0/0 failed 0 pages_rw 2 ro_restored 2 ro_failed 0 stopped 1 ran 1
swap-accounting-guard: armed, 2 sites, writes 2/2, ro 2
EOF
sed 's/ro_verified 1/ro_verified 0/' "$D/mod_ok" > "$D/mod_partial"
sed 's/writes 2\/2/writes 1\/2/' "$D/mod_ok" > "$D/mod_short"
printf 'swap-accounting-guard: loading\n' > "$D/mod_empty"
sed 's/rollback 0\/0 failed 0/rollback 1\/1 failed 1/' "$D/mod_ok" > "$D/mod_rollback"
check "complete verified module log" 0 "$(yn ol_module_ok "$D/mod_ok")"
check "partial log (ro not verified) refused" 1 "$(yn ol_module_ok "$D/mod_partial")"
check "incomplete write count refused" 1 "$(yn ol_module_ok "$D/mod_short")"
check "empty log refused" 1 "$(yn ol_module_ok "$D/mod_empty")"
check "rollback failure refused" 1 "$(yn ol_module_ok "$D/mod_rollback")"

# --- smaps evidence ------------------------------------------------------
printf 'Size: 4096 kB\nSwap: 0 kB\nSwapPss: 0 kB\n' > "$D/smaps_zero"
printf 'Size: 8192 kB\nSwap: 128 kB\nSwapPss: 64 kB\n' > "$D/smaps_hit"
: > "$D/smaps_empty"
printf 'cat: /proc/1234/smaps: No such file or directory\n' > "$D/smaps_gone"
check "swap 0 is not a path hit" 1 "$(yn ol_smaps_hit "$D/smaps_zero")"
check "swap >0 is a path hit" 0 "$(yn ol_smaps_hit "$D/smaps_hit")"
check "empty read is not a hit" 1 "$(yn ol_smaps_hit "$D/smaps_empty")"
check "PID exit is not a hit" 1 "$(yn ol_smaps_hit "$D/smaps_gone")"

# --- elapsed and continuity ---------------------------------------------
check "1799 s is short" 1 "$(yn ol_elapsed_ok 1000 2799)"
check "1800 s passes" 0 "$(yn ol_elapsed_ok 1000 2800)"
check "1801 s passes" 0 "$(yn ol_elapsed_ok 1000 2801)"
check "monotonic uptime ok" 0 "$(yn ol_boot_continuous 1000 1900)"
check "uptime regression is a reset" 1 "$(yn ol_boot_continuous 1900 1000)"


[ "$fail" = 0 ] && echo "ALL PASS" || echo "FAILURES PRESENT"
exit "$fail"
