#!/system/bin/sh
# Reversible IO/vm-tuning experiment for the SystemUI TIME_TICK ANR
# (devlog/2026-10-14-systemui-anr-io-storm.md). The ANR window is dominated by
# major faults routed loop0/loop1 -> mmcqd/0 (the container's loopback-mounted
# system/data images), not swap thrash. This measures whether raising the loop
# readahead cuts the major-fault RATE, with zero app removal and zero partition
# write. Everything it changes is a host-kernel sysfs/sysctl value that it saves
# first and restores at the end; a reboot would also restore it.
#
# Runs in the STOCK adb shell (host kernel namespace), NOT inside the container:
# /proc/vmstat, /sys/block/*, /proc/sys/vm/* here are the real host kernel. ANR
# counting is a SEPARATE container-logcat capture (logd lives in the container);
# this script measures the kernel-global fault/IO rate, which is the direct test.
#
# Usage: push to /tmp, run in the stock shell with the container already up:
#   adb push probe/android/io-vm-tuning-probe.sh /tmp/ ; adb shell sh /tmp/io-vm-tuning-probe.sh [WINDOW_S] [TARGET_KB]
# Then `adb pull` the OUTPUT_DIR. Parse the rates on the host.
set -u
WINDOW="${1:-120}"
TARGET="${2:-512}"
TS=$(date -u +%Y%m%dT%H%M%SZ 2>/dev/null || echo manual)
OUT=/data/local/tmp/io-vm-tuning-$TS
mkdir -p "$OUT"
BB=/system/xbin/busybox   # exists but is not on PATH; full path only

RA="read_ahead_kb"
LOOPS="/sys/block/loop0/queue/$RA /sys/block/loop1/queue/$RA /sys/block/mmcblk0/queue/$RA"

# --- context snapshot (once) ---
cat /proc/meminfo  > "$OUT/00-meminfo.txt"   2>&1
cat /proc/swaps    > "$OUT/00-swaps.txt"     2>&1
for z in orig_data_size compr_data_size mem_used_total; do
  echo "$z=$(cat /sys/block/zram0/$z 2>/dev/null)" >> "$OUT/00-zram.txt"
done
echo "min_free_kbytes=$(cat /proc/sys/vm/min_free_kbytes 2>/dev/null)"       > "$OUT/00-vm.txt"
echo "vfs_cache_pressure=$(cat /proc/sys/vm/vfs_cache_pressure 2>/dev/null)" >> "$OUT/00-vm.txt"
echo "swappiness=$(cat /proc/sys/vm/swappiness 2>/dev/null)"                 >> "$OUT/00-vm.txt"

# --- save originals, verify every knob is writable before touching anything ---
: > "$OUT/01-ra-original.txt"
ok=1
for f in $LOOPS; do
  if [ -w "$f" ]; then
    echo "$f=$(cat "$f" 2>/dev/null)" >> "$OUT/01-ra-original.txt"
  else
    echo "$f=NOT_WRITABLE" >> "$OUT/01-ra-original.txt"; ok=0
  fi
done
if [ "$ok" != 1 ]; then echo "ABORT: a readahead knob is not writable; nothing changed"; cat "$OUT/01-ra-original.txt"; exit 2; fi

vmstat_keys() { grep -E '^(pgmajfault|pgfault|pgpgin|pgpgout|pswpin|pswpout) ' /proc/vmstat; }

# --- baseline window (current readahead) ---
echo "phase=baseline window_s=$WINDOW" > "$OUT/10-baseline.txt"
vmstat_keys > "$OUT/10-baseline-t0.txt"
date -u +%s > "$OUT/10-baseline-t0.epoch" 2>/dev/null
sleep "$WINDOW"
vmstat_keys > "$OUT/10-baseline-t1.txt"
date -u +%s > "$OUT/10-baseline-t1.epoch" 2>/dev/null

# --- apply the tuning (reversible) ---
for f in $LOOPS; do echo "$TARGET" > "$f" 2>>"$OUT/20-apply.err"; done
: > "$OUT/20-ra-applied.txt"
for f in $LOOPS; do echo "$f=$(cat "$f" 2>/dev/null)" >> "$OUT/20-ra-applied.txt"; done

# --- treatment window (raised readahead) ---
echo "phase=treatment window_s=$WINDOW target_kb=$TARGET" > "$OUT/30-treatment.txt"
vmstat_keys > "$OUT/30-treatment-t0.txt"
date -u +%s > "$OUT/30-treatment-t0.epoch" 2>/dev/null
sleep "$WINDOW"
vmstat_keys > "$OUT/30-treatment-t1.txt"
date -u +%s > "$OUT/30-treatment-t1.epoch" 2>/dev/null

# --- restore originals unconditionally ---
: > "$OUT/40-restore.txt"
while IFS='=' read -r f v; do
  case "$v" in ''|NOT_WRITABLE) continue;; esac
  echo "$v" > "$f" 2>>"$OUT/40-restore.err"
  echo "$f=$(cat "$f" 2>/dev/null) (wanted $v)" >> "$OUT/40-restore.txt"
done < "$OUT/01-ra-original.txt"

echo "=== OUTPUT_DIR=$OUT ==="
echo "--- baseline t0 ---"; cat "$OUT/10-baseline-t0.txt"
echo "--- baseline t1 ---"; cat "$OUT/10-baseline-t1.txt"
echo "--- treatment t0 ---"; cat "$OUT/30-treatment-t0.txt"
echo "--- treatment t1 ---"; cat "$OUT/30-treatment-t1.txt"
echo "--- readahead restored ---"; cat "$OUT/40-restore.txt"
echo "pull: adb pull $OUT"
