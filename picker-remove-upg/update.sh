#!/bin/sh
#
# NW-A50 series -- remove the dual-boot picker hook
#
# Runs as file 0 of a custom UPG, executed as root by icx_start_update.sh.
# Undoes ../picker-upg: renames the kept bootswitcher.sony.sh back over the
# hook, so system holds Sony's script as it was. Only when bootswitcher.sh is
# the hook and Sony's copy is there; anything else is logged and left alone.
# /contents (wmmod-android) is not touched.
#
# Like the other UPGs here it clears the update flag before anything else and
# always exits 0, so no failure can turn into an update loop.
#

nvpflag fup -1

CONSOLE=/dev/console
BB=/xbin/busybox
LOG=/contents/PICKERUPG.log
MNT=/tmp/system
BIN=$MNT/bin
MARK="nw-a55 dual-boot picker hook"

log()
{
    echo "$1" > $CONSOLE
    echo "$1" >> $LOG
}

done_upg()
{
    UFN=`nvpstr ufn 2>/dev/null`
    if [ -n "$UFN" ] && [ -f /contents/$UFN.UPG ]; then
        mv /contents/$UFN.UPG /contents/$UFN.UPG.done && log "renamed /contents/$UFN.UPG -> .done"
    fi
}

log "====== PICKER REMOVE UPG START `$BB date 2>/dev/null` ======"

$BB mkdir -p $MNT
mount -t ext4 -o rw,noatime /emmc@android $MNT 2>>$LOG
if [ "$?" != 0 ]; then
    log "mount /emmc@android failed; nothing written"
    cat /proc/mounts >> $LOG 2>&1
    done_upg
    exit 0
fi

log "before: `ls -l $BIN/bootswitcher.sh $BIN/bootswitcher.sony.sh 2>&1`"
if ! $BB grep -q "$MARK" $BIN/bootswitcher.sh 2>/dev/null; then
    log "bootswitcher.sh is not the hook; nothing written"
elif [ ! -f $BIN/bootswitcher.sony.sh ]; then
    log "no bootswitcher.sony.sh to put back; nothing written"
elif mv $BIN/bootswitcher.sony.sh $BIN/bootswitcher.sh; then
    log "Sony's bootswitcher.sh is back: md5 `$BB md5sum $BIN/bootswitcher.sh | $BB cut -d' ' -f1`"
else
    log "could not rename bootswitcher.sony.sh back"
fi

sync
umount $MNT
sync
done_upg
log "====== PICKER REMOVE UPG DONE ======"
exit 0
