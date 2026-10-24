#!/bin/sh
#
# NW-A50 series -- install the dual-boot picker hook
#
# Runs as file 0 of a custom UPG, executed as root by icx_start_update.sh.
# It changes one file on system: /system/bin/bootswitcher.sh becomes the
# wrapper below, and Sony's script is kept beside it as bootswitcher.sony.sh.
# A second install leaves the kept original alone. The picker and the Android
# start live on /contents (wmmod-android/boot, put there by
# tools/android-boot.sh up), so they change without another UPG.
# ../picker-remove-upg puts Sony's script back.
#
# bootswitcher.sh is never missing: the wrapper and the copy of the original
# are written under temporary names and renamed into place, original first.
# Both carry the original's root:shell 0755.
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

log "====== PICKER UPG START `$BB date 2>/dev/null` ======"

$BB mkdir -p $MNT
mount -t ext4 -o rw,noatime /emmc@android $MNT 2>>$LOG
if [ "$?" != 0 ]; then
    log "mount /emmc@android failed; nothing written"
    cat /proc/mounts >> $LOG 2>&1
    done_upg
    exit 0
fi

if [ ! -f $BIN/bootswitcher.sh ]; then
    log "no $BIN/bootswitcher.sh; nothing written"
    umount $MNT
    done_upg
    exit 0
fi
log "before: `ls -l $BIN/bootswitcher.sh` md5 `$BB md5sum $BIN/bootswitcher.sh | $BB cut -d' ' -f1`"

if [ ! -f $BIN/bootswitcher.sony.sh ]; then
    if $BB grep -q "$MARK" $BIN/bootswitcher.sh; then
        log "bootswitcher.sh is already the hook but Sony's copy is gone; nothing written"
        umount $MNT
        done_upg
        exit 0
    fi
    $BB cp -p $BIN/bootswitcher.sh $BIN/.bootswitcher.sony.new && \
        mv $BIN/.bootswitcher.sony.new $BIN/bootswitcher.sony.sh
    if [ "$?" != 0 ]; then
        log "could not keep Sony's bootswitcher.sh; nothing else written"
        $BB rm -f $BIN/.bootswitcher.sony.new
        umount $MNT
        done_upg
        exit 0
    fi
    log "kept Sony's script as bootswitcher.sony.sh"
else
    log "bootswitcher.sony.sh already there: `$BB md5sum $BIN/bootswitcher.sony.sh | $BB cut -d' ' -f1`"
fi

cat > $BIN/.bootswitcher.new <<'HOOK'
#!/bin/sh
#
# nw-a55 dual-boot picker hook, in place of Sony's bootswitcher.sh (kept as
# bootswitcher.sony.sh). init runs this with exec in its boot section, before
# any Sony service. Walkman runs Sony's script, which sets sys.sony.bootmode
# and so starts the stock stack. Android sets no bootmode and starts the
# container from /contents in the background. Whatever goes wrong here ends
# in Sony's script. Installed by picker-upg; see the nw-a55 repo's
# devlog/2026-10-21-boot-picker.md.

SONY=/system/bin/bootswitcher.sony.sh
BB=/system/xbin/busybox
DIR=/contents/wmmod-android
SET=$DIR/boot
SECONDS_TO_WAIT=15
# bootpick takes the selection itself after a minute without a key; this ends
# a picker that hangs, which would hold init's exec and the panel for good.
SECONDS_AT_MOST=180

if [ ! -f $SONY ]; then
    /bin/setprop sys.sony.bootmode 1
    exit 0
fi

# A diagnostic boot (tst flag) stays Sony's.
case "`/bin/nvpflag -x tst`" in
    0x4D545346|0x4D545354) exec /bin/sh $SONY ;;
esac

choice=0
if [ -f $SET/picker/bootpick ] && [ -f $SET/picker/frames.raw ] && [ -f $SET/android-boot.sh ] && \
    $BB cp $SET/picker/bootpick /tmp/bootpick && $BB chmod 755 /tmp/bootpick; then
    last="`$BB cat $DIR/boot-choice 2>/dev/null`"
    $BB killall icx_bootanimation 2>/dev/null
    $BB timeout -t $SECONDS_AT_MOST /tmp/bootpick $SET/picker/frames.raw "${last:-1}" $SECONDS_TO_WAIT
    choice=$?
fi
case $choice in
    1|2)
        echo $choice > $DIR/boot-choice
        $BB sync
        ;;
esac
if [ $choice = 2 ]; then
    $BB setsid /bin/sh $SET/android-boot.sh boot < /dev/null > $DIR/boot-android.log 2>&1 &
    exit 0
fi
exec /bin/sh $SONY
HOOK
if [ "$?" != 0 ] || ! $BB grep -q "$MARK" $BIN/.bootswitcher.new; then
    log "could not write the hook; Sony's bootswitcher.sh stays"
    $BB rm -f $BIN/.bootswitcher.new
    umount $MNT
    done_upg
    exit 0
fi
$BB chown 0:2000 $BIN/.bootswitcher.new && $BB chmod 755 $BIN/.bootswitcher.new && \
    mv $BIN/.bootswitcher.new $BIN/bootswitcher.sh
if [ "$?" != 0 ]; then
    log "could not rename the hook into place; Sony's bootswitcher.sh stays"
    $BB rm -f $BIN/.bootswitcher.new
fi

log "after:  `ls -l $BIN/bootswitcher.sh $BIN/bootswitcher.sony.sh 2>&1`"
log "hook md5 `$BB md5sum $BIN/bootswitcher.sh | $BB cut -d' ' -f1`"

sync
umount $MNT
sync
done_upg
log "====== PICKER UPG DONE ======"
exit 0
