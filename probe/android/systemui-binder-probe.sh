#!/system/bin/sh
# Read-only SystemUI stall probe. The prior run got the kernel wchan
# (binder_thread_read) but never the binder PEER or the Java stack. This grabs
# the raw sources and pulls them to the host for parsing (the container has only
# toolbox: no busybox, no awk). Captures:
#   - /sys/kernel/debug/binder/proc/<pid>  (outgoing transaction names the target pid)
#   - /data/anr/traces.txt via SIGQUIT      (main-thread Java stack)
#   - debuggerd -b                          (native binder transact frames)
#   - dumpsys window / SurfaceFlinger        (focus / layers)
# No mounts, no writes outside its output dir (+ /data/anr, written by the system
# anyway). kill -3 is a diagnostic signal ART handles; it does NOT kill. Expects
# root (adbd root, as prior probes had). Push + run; then `adb pull` the OUTPUT_DIR.
set -u
TS=$(date -u +%Y%m%dT%H%M%SZ 2>/dev/null || echo manual)
OUT=/data/local/tmp/sysui-probe-$TS
mkdir -p "$OUT"
id > "$OUT/00-id.txt" 2>&1

# locate systemui pid without awk: mksh positional splitting of the ps line
LINE=$(ps 2>/dev/null | grep com.android.systemui | grep -v grep | head -1)
set -- $LINE
SUI=${2:-}
echo "systemui_pid=$SUI" > "$OUT/01-pid.txt"
echo "ps_line=$LINE" >> "$OUT/01-pid.txt"
if [ -z "$SUI" ]; then echo "NO_SYSTEMUI_PID - container not booted far enough"; cat "$OUT/01-pid.txt"; exit 2; fi

BINDER=/sys/kernel/debug/binder
[ -d "$BINDER" ] || BINDER=/d/binder

# sample 3x over ~75s to catch a TIME_TICK ANR window (main thread sits in
# binder_thread_read persistently, so one sample likely suffices; 3 is insurance)
i=0
while [ "$i" -lt 3 ]; do
  cat /proc/$SUI/task/$SUI/wchan > "$OUT/10-wchan-$i.txt" 2>/dev/null; echo >> "$OUT/10-wchan-$i.txt"
  cat "$BINDER/proc/$SUI" > "$OUT/11-binderproc-$i.txt" 2>/dev/null    # KEY: peer pid
  cat "$BINDER/transactions" > "$OUT/13-transactions-$i.txt" 2>/dev/null
  echo "sample $i wchan=$(cat $OUT/10-wchan-$i.txt 2>/dev/null)"
  i=$((i+1))
  [ "$i" -lt 3 ] && sleep 30
done

# Java thread dump via SIGQUIT -> accumulates in /data/anr/traces.txt
kill -3 "$SUI" 2>/dev/null
sleep 3
cat /data/anr/traces.txt > "$OUT/30-traces.txt" 2>/dev/null || echo "NO_TRACES" > "$OUT/30-traces.txt"

# native backtrace (binder transact frames across threads)
debuggerd -b "$SUI" > "$OUT/35-native-bt.txt" 2>&1 || echo "debuggerd_failed" >> "$OUT/35-native-bt.txt"

# UI state (raw; parse on host)
dumpsys window > "$OUT/40-window.txt" 2>/dev/null
dumpsys SurfaceFlinger > "$OUT/41-sf.txt" 2>/dev/null
logcat -d -t 3000 > "$OUT/50-logcat.txt" 2>/dev/null

echo "=== OUTPUT_DIR=$OUT ==="
echo "--- binderproc outgoing (sample 0) ---"; grep "outgoing transaction" "$OUT/11-binderproc-0.txt" 2>/dev/null
echo "--- wchan ---"; cat "$OUT"/10-wchan-*.txt 2>/dev/null
echo "pull: adb pull $OUT"
