#!/bin/sh
#
# NW-A50 series -- turn adb on (or off)
#
# Runs as file 0 of a custom UPG, executed as root by icx_start_update.sh.
# It writes exactly one file: the persistent property persist.sys.sony.icx.adb,
# set to the same value /system/vendor/sony/bin/AdbEnabler sets ("1"; the
# AdbDisabler binary sets "0"). init keeps persistent properties as one file
# per property under /data/property, and /data is the usrdata partition.
# Nothing in system, boot or nvp is touched. To undo, build with VALUE=0.
#
# Like the backup UPG, it clears the update flag before anything else and
# always exits 0, so no failure can turn into an update loop.
#

nvpflag fup -1

VALUE=1
PROP=persist.sys.sony.icx.adb
CONSOLE=/dev/console
BB=/xbin/busybox
LOG=/contents/ADBUPG.log
MNT=/tmp/usrdata

log()
{
    echo "$1" > $CONSOLE
    echo "$1" >> $LOG
}

log "====== ADB UPG START `$BB date 2>/dev/null` (value $VALUE) ======"

$BB mkdir -p $MNT
mount -t ext4 -o rw,noatime /emmc@usrdata $MNT 2>>$LOG
if [ "$?" != 0 ]; then
    log "mount /emmc@usrdata failed; nothing written"
    ls -l /emmc@* >> $LOG 2>&1
    cat /proc/mounts >> $LOG 2>&1
    exit 0
fi

DIR=$MNT/property
if [ ! -d $DIR ]; then
    log "no property directory on usrdata; nothing written. Top level:"
    ls -la $MNT >> $LOG 2>&1
    umount $MNT
    exit 0
fi

log "before: `ls -la $DIR/$PROP 2>&1` value='`cat $DIR/$PROP 2>/dev/null`'"

# Same shape as init's own writes: no trailing newline, root-owned, 0600,
# written to a temp name and renamed into place.
TMP=$DIR/.wmadb.tmp
printf '%s' "$VALUE" > $TMP && $BB chown 0:0 $TMP && $BB chmod 600 $TMP && mv $TMP $DIR/$PROP
if [ "$?" != 0 ]; then
    log "write FAILED"
    $BB rm -f $TMP 2>/dev/null
fi

log "after:  `ls -la $DIR/$PROP 2>&1` value='`cat $DIR/$PROP 2>/dev/null`'"
echo "--- $DIR" >> $LOG
ls -la $DIR >> $LOG 2>&1

sync
umount $MNT
sync
log "====== ADB UPG DONE ======"
exit 0
