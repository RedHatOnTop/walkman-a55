#!/system/bin/sh
#
# Runs on the device as root: logs what idle costs while the Walkman is
# unplugged, for comparing the container against the stock Sony UI. USB keeps
# the charger's wakeup source active, so an idle test means no cable, and the
# log has to live on the device until the cable comes back.
#
#   sh idle-log.sh start [SECONDS]   one line per sample (default 60) to $LOG
#   sh idle-log.sh stop
#
# The loop holds no wakelock. It sleeps 2 s at a time on the monotonic clock,
# which stops in suspend, and samples once the boot clock has moved SECONDS
# past the last sample. A suspended device so gets a sample within 2 s of
# awake time after a long sleep, which catches the state when the screen
# comes on before a replug. Suspended time shows as boot advancing past mono
# (mono stops in suspend, boot does not). Each line carries:
#
#   wall boot mono   date +%s, /proc/uptime, timer_list "now at"
#   bat              capacity, voltage, status from the battery uevent
#   bl               lcd-backlight brightness (0 = panel dark)
#   cpu              the /proc/stat cpu line, for busy time between samples
#   wl               active userspace wakelocks (/sys/power/wake_lock)
#   ws               kernel wakeup sources active right now (debugfs)
#   ss               suspend_stats success/fail counts (debugfs)
#   lf               suspend_stats last failed device, errno and step
#   wake             icx_pm_helper resume_count, then SPM r12 and EINT
#                    status from the last resume (what woke the SoC)
#
# Not /sys/power/wakeup_count: a read blocks while any wakeup event is in
# progress, and the charger's is in progress the whole time a cable is in.
#
# $LOG is on /contents (vfat), so it survives a power-off. Started with
# SIGHUP ignored so it outlives the adb shell, as android-boot.sh does for
# powerwatch.

BB=/system/xbin/busybox
# android-boot.sh prepare mounts /contents a second time on /tmp/c.
if $BB grep -q ' /contents ' /proc/mounts; then
    LOG=/contents/idle-log.txt
elif $BB grep -q ' /tmp/c ' /proc/mounts; then
    LOG=/tmp/c/idle-log.txt
else
    LOG=/tmp/idle-log.txt
fi
PIDFILE=/tmp/idle-log.pid
PMH=/sys/devices/platform/icx_pm_helper

sample()
{
    mono=$($BB awk '/^now at/ { printf "%.1f", $3 / 1e9; exit }' /proc/timer_list)
    bat=$($BB grep -E '^POWER_SUPPLY_(CAPACITY|VOLTAGE_NOW|STATUS)=' /sys/class/power_supply/battery/uevent \
        | $BB sed 's/^POWER_SUPPLY_//' | $BB tr '\n' ' ')
    # Names can hold spaces ("EINT wakelock"), so count columns from the end.
    ws=$($BB awk 'NR > 1 && $(NF - 4) != 0 { n = $1; for (i = 2; i <= NF - 9; i++) n = n "_" $i; printf "%s,", n }' \
        /sys/kernel/debug/wakeup_sources 2>/dev/null)
    echo "wall=$($BB date +%s) boot=$($BB cut -d' ' -f1 /proc/uptime) mono=$mono" \
        "bat=[$bat] bl=$($BB cat /sys/class/leds/lcd-backlight/brightness 2>/dev/null)" \
        "cpu=[$($BB head -1 /proc/stat | $BB cut -d' ' -f3-)]" \
        "wl=[$($BB cat /sys/power/wake_lock 2>/dev/null)] ws=[$ws]" \
        "ss=$($BB awk '/^(success|fail):/ { printf "%s%s", $2, ($1 == "success:" ? "/" : "") }' /sys/kernel/debug/suspend_stats 2>/dev/null)" \
        "wake=$($BB cat $PMH/resume_count $PMH/spm_r12 $PMH/eint_sta 2>/dev/null | $BB tr '\n' ' ')" \
        "lf=[$($BB awk '/last_failed_(dev|errno|step):/ { printf "%s ", $2 }' /sys/kernel/debug/suspend_stats 2>/dev/null)]"
}

loop()
{
    echo "# idle-log start $($BB date) every ${1}s" >> $LOG
    next=0
    while :; do
        read boot idle < /proc/uptime
        boot=${boot%.*}
        if [ $boot -ge $next ]; then
            sample >> $LOG
            $BB sync
            next=$((boot + $1))
        fi
        $BB sleep 2
    done
}

case "$1" in
start)
    [ -f $PIDFILE ] && kill -0 "$($BB cat $PIDFILE)" 2>/dev/null && { echo "already running"; exit 0; }
    trap '' 1
    $BB setsid sh "$0" loop "${2:-60}" < /dev/null > /dev/null 2>&1 &
    echo $! > $PIDFILE
    echo "logging to $LOG"
    ;;
loop) loop "$2" ;;
stop)
    [ -f $PIDFILE ] && kill "$($BB cat $PIDFILE)" 2>/dev/null
    $BB rm -f $PIDFILE
    echo "# idle-log stop $($BB date)" >> $LOG
    ;;
*) echo "usage: idle-log.sh start [SECONDS] | stop" ;;
esac
