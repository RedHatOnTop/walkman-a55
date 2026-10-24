#!/bin/sh
#
# NW-A50 series -- READ-ONLY backup + device survey
#
# Runs as file 0 of a custom UPG, executed as root by icx_start_update.sh.
# It NEVER writes to any partition. It only reads block devices and writes
# plain files into /contents (the user-visible storage), which the host
# script has already mounted for us.
#
# CRITICAL: this script must always exit 0. The host script does NOT reset
# the nvp "fup" flag when /update.sh fails, which would make the device try
# to update again on every boot (update loop). Every failure here is logged
# and swallowed. As a second guard the flag is cleared before anything else
# runs, the same way unknown321/nw-installer's install.sh does it, so even a
# crash or a hang ends in a normal boot.
#
# Only commands that the stock Sony update.sh itself uses are assumed to
# exist: /xbin/busybox, /bin/dd, cat, echo, ls, awk, grep, expr, mv, mkdir,
# sync, and /proc/dumchar_info.
#

CONSOLE=/dev/console
BB=/xbin/busybox
OUT=/contents/WMBACKUP
LOG=$OUT/backup.log

# Partitions never dumped:
#   contents  - this is where we are writing; it is huge
#   bmtpool   - NAND bad-block pool, meaningless on eMMC
#   usrdata   - user data, large and not needed to restore a brick
#   cache     - scratch
SKIP="contents bmtpool usrdata cache"

# Leave this much free on /contents (KB) so we never fill the user's storage.
RESERVE_KB=262144

# Rename the UPG when done so the device does not re-detect it as an update.
RENAME_UPG=1

log()
{
    echo "$1" > $CONSOLE
    echo "$1" >> $LOG
}

# ---------------------------------------------------------------- prepare
nvpflag fup -1
echo "====== WMBACKUP START ======" > $CONSOLE

$BB mkdir -p $OUT 2>/dev/null
if [ ! -d $OUT ]; then
    echo "WMBACKUP: cannot create $OUT -- is /contents mounted rw?" > $CONSOLE
    exit 0
fi

echo "" > $LOG
log "====== WMBACKUP START ======"
log "date: `$BB date 2>/dev/null`"

# ---------------------------------------------------------- device survey
# This part answers the open hardware questions (RAM size, eMMC vendor and
# size, DRAM type, partition map) and is worth more than the images.
log "------ DEVICE SURVEY ------"

survey()
{
    echo "" >> $OUT/survey.txt
    echo "===== $1 =====" >> $OUT/survey.txt
    if [ -e "$1" ]; then
        cat "$1" >> $OUT/survey.txt 2>/dev/null
    else
        echo "(absent)" >> $OUT/survey.txt
    fi
}

echo "WMBACKUP survey" > $OUT/survey.txt

survey /proc/meminfo
survey /proc/cpuinfo
survey /proc/cmdline
survey /proc/version
survey /proc/partitions
survey /proc/dumchar_info
survey /proc/emmc
survey /proc/mtd
survey /sys/block/mmcblk0/size
survey /sys/block/mmcblk0/device/name
survey /sys/block/mmcblk0/device/type
survey /sys/block/mmcblk0/device/manfid
survey /sys/block/mmcblk0/device/oemid
survey /sys/block/mmcblk0/device/cid
survey /sys/block/mmcblk0/device/csd
survey /sys/block/mmcblk0/device/fwrev
survey /sys/block/mmcblk0/device/hwrev
survey /sys/block/mmcblk0/device/serial
survey /sys/block/mmcblk0/device/date
survey /sys/block/mmcblk0/device/preferred_erase_size
survey /sys/block/mmcblk0boot0/size
survey /sys/block/mmcblk0boot1/size
survey /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies
survey /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
survey /sys/module/icx_pm_helper/parameters/icx_bid2
survey /sys/module/icx_pm_helper/parameters/icx_bid3

# DRAM type is exported by mach-mt8590 under a name we are not sure of,
# so look for anything that smells like it.
echo "" >> $OUT/survey.txt
echo "===== dram sysfs candidates =====" >> $OUT/survey.txt
for f in /sys/bus/platform/drivers/emi_mpu/DRAM_TYPE \
         /sys/devices/platform/emi_mpu/DRAM_TYPE \
         /proc/dram_type; do
    if [ -e "$f" ]; then
        echo "--- $f" >> $OUT/survey.txt
        cat "$f" >> $OUT/survey.txt 2>/dev/null
    fi
done
$BB find /sys -maxdepth 6 -iname "*DRAM*" >> $OUT/survey.txt 2>/dev/null

echo "" >> $OUT/survey.txt
echo "===== mount =====" >> $OUT/survey.txt
cat /proc/mounts >> $OUT/survey.txt 2>/dev/null

echo "" >> $OUT/survey.txt
echo "===== block devices =====" >> $OUT/survey.txt
ls -l /dev/block >> $OUT/survey.txt 2>/dev/null
ls -l /emmc@* >> $OUT/survey.txt 2>/dev/null

echo "" >> $OUT/survey.txt
echo "===== nvp =====" >> $OUT/survey.txt
for k in kas ubp ufn ums ups fvi fpi lyr ser pcd clg bpr pwd; do
    echo "nvpstr $k = `nvpstr $k 2>/dev/null`" >> $OUT/survey.txt
done
for k in mid syi tst fni sid mac clv shp sps gty prk fup fur ins; do
    echo "nvpflag $k = `nvpflag -x $k 2>/dev/null`" >> $OUT/survey.txt
done

log "survey written to survey.txt"

# ------------------------------------------------- update environment
# This script runs inside the updater's own small ramdisk. It is in neither
# boot.img nor system.img, and Walkman One swaps fwpchk inside it, so archive
# its files to find out where it lives. Mounted filesystems are left out;
# the partitions behind them are dumped below anyway.
log "------ UPDATE ENVIRONMENT ------"

ls -la / /bin /sbin /xbin /lib /lib/modules /opt2 /opt2/stock > $OUT/update-env.txt 2>&1

TARLIST=""
for d in /*; do
    case "$d" in
        /proc|/sys|/dev|/contents) continue ;;
    esac
    if $BB grep -q " $d " /proc/mounts; then
        log "update env: skip $d (mounted)"
        continue
    fi
    TARLIST="$TARLIST $d"
done
# A ramdisk is a few MB. Anything far bigger means / is not what we think it
# is, and the partition dumps cover it better than a tar would.
ENV_KB=""
if [ -n "$TARLIST" ]; then
    ENV_KB=`$BB du -sk $TARLIST 2>/dev/null | $BB awk '{s += $1} END {print s}'`
fi
log "update env: $ENV_KB KB in:$TARLIST"
case "$ENV_KB" in
    ""|*[!0-9]*)
        log "update env: cannot size it, not archiving"
        ;;
    *)
        if [ "$ENV_KB" -gt 65536 ]; then
            log "update env: over 64MB, not archiving"
        else
            $BB tar -cf $OUT/update-rootfs.tar $TARLIST 2>>$LOG
            if [ "$?" = 0 ]; then
                log "update env: archived to update-rootfs.tar"
            else
                log "update env: tar FAILED (partial archive may remain)"
            fi
        fi
        ;;
esac
sync

# ---------------------------------------------------- partition dump loop
# /proc/dumchar_info lines look like:
#   android 0x0000000032000000 0x0000000006200000 2 /dev/block/mmcblk0p19
#   name    size                 offset             type device
log "------ PARTITION DUMP ------"

if [ ! -e /proc/dumchar_info ]; then
    log "no /proc/dumchar_info -- cannot enumerate partitions, survey only"
    sync
    log "====== WMBACKUP DONE (survey only) ======"
    exit 0
fi

cat /proc/dumchar_info > $OUT/dumchar_info.txt 2>/dev/null

echo "# name size_bytes md5 file" > $OUT/manifest.txt

cat /proc/dumchar_info | $BB grep "^[a-z]" | while read NAME SIZE OFFSET TYPE DEV REST
do
    # skip header / malformed lines
    case "$SIZE" in
        0x*) ;;
        *) continue ;;
    esac

    SKIPIT=0
    for s in $SKIP; do
        if [ "$NAME" = "$s" ]; then SKIPIT=1; fi
    done
    if [ "$SKIPIT" = "1" ]; then
        log "skip $NAME (excluded)"
        continue
    fi

    BYTES=`echo $(($SIZE))`
    KB=`$BB expr $BYTES / 1024`

    if [ "$BYTES" = "0" ]; then
        log "skip $NAME (zero size)"
        continue
    fi

    # free space guard; if df misbehaves, be conservative and stop.
    # busybox df wraps device names over 20 chars onto their own line, so
    # count "Available" from the end of the line instead of the start.
    FREE=`$BB df /contents 2>/dev/null | $BB tail -1 | $BB awk '{print $(NF-2)}'`
    case "$FREE" in
        ""|*[!0-9]*)
            log "STOP: cannot read free space on /contents (got '$FREE')"
            break
            ;;
    esac
    NEED=`$BB expr $KB + $RESERVE_KB`
    if [ "$FREE" -lt "$NEED" ]; then
        log "STOP: $NAME needs ${KB}KB, only ${FREE}KB free (reserve ${RESERVE_KB}KB)"
        break
    fi

    # prefer the named char device the updater itself uses, fall back to the
    # block device from dumchar_info
    SRC=/emmc@$NAME
    if [ ! -e $SRC ]; then
        SRC=$DEV
    fi
    if [ ! -e "$SRC" ]; then
        log "skip $NAME (no device node: /emmc@$NAME or $DEV)"
        continue
    fi

    DST=$OUT/$NAME.img
    log "dump $NAME: expect $BYTES bytes from $SRC"

    # No count= on purpose: reading to EOF gives the exact partition size and
    # cannot truncate a partition whose size is not a multiple of the blocksize.
    /bin/dd if=$SRC of=$DST bs=32768 2>>$LOG
    if [ "$?" != 0 ]; then
        log "  dd FAILED for $NAME"
        $BB rm -f $DST 2>/dev/null
        continue
    fi

    GOT=`ls -l $DST | $BB awk '{print $5}'`
    MD5=`$BB md5sum $DST 2>/dev/null | $BB awk '{print $1}'`
    echo "$NAME $GOT $MD5 $NAME.img" >> $OUT/manifest.txt
    if [ "$GOT" = "$BYTES" ]; then
        log "  ok  $GOT bytes  md5=$MD5"
    else
        log "  WARN size mismatch: got $GOT, dumchar says $BYTES  md5=$MD5"
    fi
    sync
done

# ------------------------------------------------------- boot area (eMMC)
# The preloader lives in the eMMC boot partition, not in the user area.
# It is the single most valuable file for SP Flash Tool recovery.
for B in mmcblk0boot0 mmcblk0boot1; do
    if [ -e /dev/block/$B ]; then
        log "dump $B"
        /bin/dd if=/dev/block/$B of=$OUT/$B.img bs=32768 2>>$LOG
        if [ "$?" = 0 ]; then
            MD5=`$BB md5sum $OUT/$B.img 2>/dev/null | $BB awk '{print $1}'`
            SZ=`ls -l $OUT/$B.img | $BB awk '{print $5}'`
            echo "$B $SZ $MD5 $B.img" >> $OUT/manifest.txt
            log "  ok  md5=$MD5"
        else
            log "  dd FAILED for $B"
        fi
        sync
    fi
done

# --------------------------------------------------------------- finish up
if [ "$RENAME_UPG" = "1" ]; then
    UFN=`nvpstr ufn 2>/dev/null`
    if [ -n "$UFN" ]; then
        if [ -f /contents/$UFN.UPG ]; then
            mv /contents/$UFN.UPG /contents/$UFN.UPG.done 2>/dev/null
            log "renamed /contents/$UFN.UPG -> .done"
        fi
    fi
fi

echo "" >> $OUT/survey.txt
echo "===== free space after =====" >> $OUT/survey.txt
$BB df >> $OUT/survey.txt 2>/dev/null

sync
sync
log "====== WMBACKUP DONE ======"

# ALWAYS succeed -- see header comment about the update loop.
exit 0
