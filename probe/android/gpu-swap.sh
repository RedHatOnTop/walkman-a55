#!/system/bin/sh
#
# Runs on the device as root: stops the Sony stack, swaps the built-in r6p2
# Mali driver for mali_r4p0.ko, and runs egltest in a chroot of the FireOS 5
# /system. Nothing here survives a reboot, and a reboot is the only way back:
# the stock driver's device is unregistered when mali_r4p0 takes over.
#
#   sh gpu-swap.sh prepare      stop the Sony stack, mount the internal storage
#                               read-write on /tmp/c for the image push
#   sh gpu-swap.sh run IMAGE    swap the driver and run egltest; IMAGE is the
#                               Fire system.img relative to /tmp/c
#   sh gpu-swap.sh sf IMAGE     swap the driver, start servicemanager and
#                               surfaceflinger, then bootanimation; leaves
#                               screencaps and logs in /tmp/aroot/data
#
# Expects /tmp/mali_r4p0.ko, /tmp/egltest and /tmp/runas. The host side is
# tools/android-gpu.sh.
#
BB=/system/xbin/busybox
GADGET=/sys/devices/platform/mt_usb/musb-hdrc.0/gadget
ROOT=/tmp/aroot

say() { echo "== $*"; }
die() { echo "!! $*"; exit 1; }

holders()
{
    for p in /proc/[0-9]*; do
        $BB ls -l $p/fd 2>/dev/null | $BB grep -q "$1" && echo "${p#/proc/} $(cat $p/comm)"
    done
}

prepare()
{
say "stop the Sony stack"
if $BB ps | $BB grep -q '[h]agodaemon'; then
    hagodaemons.sh stop | $BB tail -1
fi
for i in $($BB seq 60); do
    $BB ps | $BB grep -q '[H]grmMediaPlayer' || break
    $BB sleep 1
done
$BB ps | $BB grep -q '[H]grmMediaPlayer' && die "the app did not stop"
for f in $GADGET/lun*/file; do echo > $f; done
left="$(holders /dev/mali)"
[ -z "$left" ] || die "/dev/mali still open by: $left"
$BB mkdir -p /tmp/c
$BB grep -q ' /tmp/c ' /proc/mounts || $BB mount -t vfat /emmc@contents /tmp/c || die "contents mount failed"
$BB mkdir -p /tmp/c/wmmod-android
$BB df -h /tmp/c | $BB tail -1
}

swap()
{
left="$(holders /dev/mali)"
[ -z "$left" ] || die "/dev/mali still open by: $left"

say "unbind the stock driver"
if [ -e /sys/bus/platform/drivers/mali-utgard/mali-utgard.0 ]; then
    echo mali-utgard.0 > /sys/bus/platform/drivers/mali-utgard/unbind
fi
[ -e /sys/bus/platform/drivers/mali-utgard/mali-utgard.0 ] && die "unbind failed"

say "load mali_r4p0"
$BB grep -q '^mali_r4p0 ' /proc/modules || insmod /tmp/mali_r4p0.ko || die "insmod failed"
dmesg | $BB grep -E 'Mali|mali' | $BB tail -12
dev="$(cat /sys/class/misc/mali/dev 2>/dev/null)"
[ -n "$dev" ] || die "no mali misc device"
$BB rm -f /dev/mali
$BB mknod /dev/mali c ${dev%%:*} ${dev##*:}
$BB chmod 666 /dev/mali
ls -l /dev/mali
}

chroot_up()
{
IMAGE="$1"
say "mount the Fire /system"
$BB mkdir -p $ROOT/system $ROOT/dev $ROOT/proc $ROOT/sys
$BB grep -q ' /tmp/c ' /proc/mounts || die "run prepare first"
[ -f "/tmp/c/$IMAGE" ] || die "no /tmp/c/$IMAGE"
$BB grep -q " $ROOT/system " /proc/mounts || $BB mount -o loop,ro -t ext4 "/tmp/c/$IMAGE" $ROOT/system || die "loop mount failed"
for d in dev proc sys; do
    $BB grep -q " $ROOT/$d " /proc/mounts || $BB mount -o bind /$d $ROOT/$d
done
}

run()
{
IMAGE="${1:?usage: gpu-swap.sh run IMAGE}"
swap
chroot_up "$IMAGE"
$BB cp /tmp/egltest $ROOT/egltest && $BB chmod 755 $ROOT/egltest
say "egltest"
$BB chroot $ROOT /egltest
echo "egltest exit $?"
dmesg | $BB grep -E 'Mali|mali' | $BB tail -8
}

# The chroot has no property area (this init hands properties over in the
# environment, not /dev/__properties__), so hw_get_module asks for the
# "default" variants. The MTK HALs are linked in under those names: Mali can
# only render into buffers from the MTK gralloc.
hw_overlay()
{
$BB mkdir -p $ROOT/fire
$BB grep -q " $ROOT/fire " /proc/mounts || $BB mount -o loop,ro -t ext4 "/tmp/c/$1" $ROOT/fire || die "second loop mount failed"
if ! $BB grep -q " $ROOT/system/lib/hw " /proc/mounts; then
    $BB mount -t tmpfs tmpfs $ROOT/system/lib/hw
    for f in $ROOT/fire/lib/hw/*; do
        $BB ln -s /fire/lib/hw/${f##*/} $ROOT/system/lib/hw/${f##*/}
    done
    $BB ln -sf /fire/lib/hw/gralloc.mt8127.so $ROOT/system/lib/hw/gralloc.default.so
    $BB ln -sf /fire/lib/hw/hwcomposer.mt8127.so $ROOT/system/lib/hw/hwcomposer.default.so
fi
}

alive() { $BB pidof "$1" > /dev/null && echo "$1 running" || echo "$1 NOT running"; }

sf()
{
IMAGE="${1:?usage: gpu-swap.sh sf IMAGE}"
swap
chroot_up "$IMAGE"
hw_overlay "$IMAGE"
$BB mkdir -p $ROOT/data $ROOT/cache
# adb shell's umask is 0077; servicemanager, as uid 100, has to get through.
$BB chmod 755 $ROOT $ROOT/data $ROOT/cache $ROOT/fire $ROOT/dev $ROOT/proc $ROOT/sys
$BB cp /tmp/runas $ROOT/runas && $BB chmod 755 $ROOT/runas
export ANDROID_ROOT=/system ANDROID_DATA=/data PATH=/system/bin:/system/xbin

say "servicemanager as uid 100"
$BB pidof servicemanager > /dev/null || \
    $BB setsid $BB chroot $ROOT /runas 100 100 /system/bin/servicemanager > $ROOT/data/sm.log 2>&1 &
$BB sleep 1
alive servicemanager

say "surfaceflinger"
$BB pidof surfaceflinger > /dev/null || \
    $BB setsid $BB chroot $ROOT /system/bin/surfaceflinger > $ROOT/data/sf.log 2>&1 &
$BB sleep 6
alive servicemanager
alive surfaceflinger
# Both block forever while servicemanager is missing.
$BB timeout -t 10 $BB chroot $ROOT /system/bin/service list 2>&1 | $BB head -8
$BB timeout -t 20 $BB chroot $ROOT /system/bin/screencap -p /data/sf0.png 2>&1 | $BB tail -3

say "bootanimation"
$BB pidof bootanimation > /dev/null || \
    $BB setsid $BB chroot $ROOT /system/bin/bootanimation > $ROOT/data/ba.log 2>&1 &
$BB sleep 6
alive bootanimation
$BB timeout -t 20 $BB chroot $ROOT /system/bin/screencap -p /data/sf1.png 2>&1 | $BB tail -3
ls -l $ROOT/data
dmesg | $BB grep -E 'Mali|mali' | $BB tail -6
}

case "$1" in
prepare) prepare ;;
run) shift; run "$@" ;;
sf) shift; sf "$@" ;;
*) die "usage: gpu-swap.sh prepare | run IMAGE | sf IMAGE" ;;
esac
