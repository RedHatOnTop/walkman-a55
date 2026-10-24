#!/system/bin/sh
#
# NW-A55 /dev/stpgps function-on test. DRAFT, not yet run on the device.
#
# Host side, Sony stack as it boots (the same state as the 2026-10-11 Wi-Fi run).
# Writes nothing to any partition: the module lives in RAM, the log goes to /tmp.
#
#   adb push probe/kmod/mtk-stp-gps-soc/mtk_stp_gps_soc.ko /tmp/gps/
#   adb push probe/android/connectivity/gps/gps-open-test.sh /tmp/gps/
#   adb shell sh /tmp/gps/gps-open-test.sh [hold-seconds]
#
# What it proves, in this order:
#   1  the module loads (stock exports resolve, vermagic accepted)
#   2  the character device is registered and /dev/stpgps exists
#   3  open(/dev/stpgps) makes WMT switch the GPS function on
#      (mtk_wcn_wmt_func_on type 2) and close switches it off again
# It does NOT prove a fix: no antenna, no mnld, no NMEA is involved.

BB=/system/xbin/busybox
D=/tmp/gps
KO=$D/mtk_stp_gps_soc.ko
HOLD="${1:-5}"
TS=$($BB date +%Y%m%d_%H%M%S)
LOG=$D/gpstest_$TS.log
TAG="GPSTEST-$TS"

mkdir -p $D
say() { echo "$*"; echo "$*" >> $LOG; }
klog() { $BB dmesg | $BB sed -n "/$TAG/,\$p"; }
keylog() { klog | $BB grep -E 'GPS|WMT|STP|CONSYS|mtk_stp|chrdev|stpgps' | $BB tail -n 60; }
verdict() { say ""; say ">>> VERDICT: $*"; keylog >> $LOG; keylog; say "log: $LOG"; }
loaded() { $BB grep -q "^$1 " /proc/modules; }

say "== [0] environment"
[ "$($BB id -u)" = "0" ] || { verdict "not root"; exit 1; }
[ "$($BB uname -r)" = "3.10.26" ] || { verdict "unexpected kernel release $($BB uname -r)"; exit 1; }
loaded mtk_stp_wmt_soc || { verdict "mtk_stp_wmt_soc is not loaded (Sony WMT core down)"; exit 1; }
[ -f $KO ] || { verdict "$KO missing"; exit 1; }
echo "$TAG start" > /dev/kmsg
$BB ps | $BB grep -E '[6]620_launcher|[m]tkbt' >> $LOG 2>&1
$BB grep -E '^(mtk_|compat|cfg80211|wlan_mt)' /proc/modules >> $LOG

say "== [1] insmod"
if loaded mtk_stp_gps_soc; then
    say "already loaded: mtk_stp_gps_soc"
else
    insmod $KO >> $LOG 2>&1
    rc=$?
    say "insmod rc=$rc"
    [ $rc -eq 0 ] || { verdict "insmod failed. 'Unknown symbol' = stock export missing; 'Device or resource busy' = major 191 taken (retry: insmod $KO GPS_major=<free major from /proc/devices>)"; exit 1; }
fi

say "== [2] character device"
maj=$($BB awk '$2 == "mtk_stp_GPS_chrdev_soc" {print $1}' /proc/devices)
say "char major: ${maj:-none}"
[ -n "$maj" ] || { verdict "GPS char device not registered"; exit 1; }
say "class dev: $($BB cat /sys/class/stpgps/stpgps/dev 2>/dev/null)"
if [ ! -c /dev/stpgps ]; then
    # /dev is a plain tmpfs and the Sony ueventd may not have made the node.
    $BB mknod /dev/stpgps c "$maj" 0 || { verdict "mknod /dev/stpgps failed"; exit 1; }
    say "made /dev/stpgps by hand"
elif [ "$($BB stat -c %t /dev/stpgps)" != "$($BB printf %x "$maj")" ]; then
    verdict "/dev/stpgps exists with another major; not opening it"
    exit 1
fi
$BB ls -l /dev/stpgps >> $LOG 2>&1
$BB ls -l /dev/stpgps

say "== [3] open for $HOLD s (WMT turns the GPS function on)"
# The shell keeps fd 3 open for the hold time and closes it afterwards. read() is
# never called, so nothing blocks on an empty STP queue. Background + poll so a
# hung open() cannot freeze this script. The subshell leaves its own status in a
# file: by the time the poll below ends, the device shell may already have reaped
# it, and wait then says 127 (seen 2026-10-15 on a run that passed).
RCF=$D/open-rc.$TS
( exec 3</dev/stpgps || { echo 7 > $RCF; exit 7; }; $BB sleep "$HOLD"; exec 3<&-; echo 0 > $RCF ) &
pid=$!
i=0
while [ $i -lt $((HOLD + 25)) ] && kill -0 $pid 2>/dev/null; do
    $BB sleep 1
    i=$((i + 1))
done
if kill -0 $pid 2>/dev/null; then
    verdict "open/close still running after $i s: WMT function switch is hung. Pull dmesg, then reboot."
    exit 3
fi
wait $pid 2>/dev/null
# No file: the open redirect failed and took the subshell down before the echo.
orc=$($BB cat $RCF 2>/dev/null || echo "none (open failed)")
say "open+hold+close rc=$orc"

if klog | $BB grep -q 'WMT turn on GPS OK'; then
    if klog | $BB grep -q 'WMT turn off GPS OK'; then
        verdict "GPS FUNCTION ON/OFF OK (WMT accepted type 2). Nothing about a satellite fix is implied."
        exit 0
    fi
    verdict "turned on but no 'turn off GPS OK' line: check close() in the log above"
    exit 5
fi
if klog | $BB grep -q 'WMT turn on GPS fail'; then
    verdict "WMT refused the GPS function (CONSYS/efuse/firmware side). Share the log."
    exit 2
fi
if klog | $BB grep -q 'STP is not ready'; then
    verdict "open() reached the driver but STP is not ready"
    exit 4
fi
if klog | $BB grep -q 'whole chip resetting'; then
    verdict "a whole-chip reset was in progress; retry in a minute"
    exit 6
fi
verdict "no GPS_open line at all (open rc=$orc): the node does not reach mtk_stp_gps_soc"
exit 8
