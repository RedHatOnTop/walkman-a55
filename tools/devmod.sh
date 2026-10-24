#!/bin/bash
#
# Run the stock app on the device with the probe plugin and, optionally, a UI
# mod, without writing to any partition. Everything lives in /tmp and in bind
# mounts, so a reboot returns the device to stock.
#
#   tools/devmod.sh mods/NAME       build the mod against work/qrc-device and run it
#   tools/devmod.sh hot mods/NAME   build it and swap it into the running app
#   tools/devmod.sh probe           probe only, no overlay
#   tools/devmod.sh stock           drop the bind mounts and restart the stock app
#
# Every mode but hot restarts the app, which loses the SD card until it is
# reinserted (probe/devtools/carddet.c). hot goes through the probe's reload
# (probe/mkspec/probe.h), which only reaches QML under a wmReloadToken, such
# as a mod's components/WmHot.qml, and lists what it could not reach. It
# pushes /tmp/wmmod/hot.rcc and leaves overlay.rcc, so an app that restarts
# comes back with the last full run's build. An app started by a devmod from
# before reload existed has to be restarted once.
#
# Needs adb root (W1 `ADB=1`, docs/DEVICE-STEPS.md step 2). Set NW_SERIAL when
# more than one USB adb device is attached. This adbd does not return the exit
# status of `adb shell` commands, so results are read from their output.
#
# Device-side constraints this handles:
# - adb shell runs with umask 0077, and the app runs as `system`, so every
#   file it reads or writes is chmod/chown'ed explicitly.
# - hagodaemons.sh restart re-enters USB mass storage mode, which unmounts
#   /contents. A wrapper over the app binary sets WMMOD_OVERLAY so the overlay,
#   probe flag and badboots live in /tmp/wmmod instead, and mass storage is
#   turned off afterwards through the probe (MSC screen, enableExportAsMsc).
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP=/system/vendor/sony/bin/HgrmMediaPlayerApp
PLUGIN=/system/vendor/sony/plugins/platforms/libqeglfs.so
GADGET=/sys/devices/platform/mt_usb/musb-hdrc.0/gadget
MODE="${1:?usage: tools/devmod.sh mods/NAME | hot mods/NAME | probe | stock}"

if [ -z "${NW_SERIAL:-}" ]; then
    mapfile -t serials < <(adb devices -l | awk '$2 == "device" && / usb:/ {print $1}')
    [ "${#serials[@]}" -eq 1 ] || { echo "need exactly one USB adb device, got ${#serials[@]}; set NW_SERIAL" >&2; exit 1; }
    NW_SERIAL="${serials[0]}"
fi
A=(adb -s "$NW_SERIAL")
probe() { python3 "$ROOT/tools/probe.py" "$@"; }

"${A[@]}" shell id | grep -q "^uid=0(" || { echo "adb is not root" >&2; exit 1; }

if [ "$MODE" = hot ]; then
    MOD="${2:?usage: tools/devmod.sh hot mods/NAME}"
    "${A[@]}" shell "[ -d /tmp/wmmod ] && echo ok" | grep -q ok \
        || { echo "no devmod run since boot; run tools/devmod.sh $MOD" >&2; exit 1; }
    "$ROOT/tools/mkmod.sh" "$MOD" "$ROOT/work/qrc-device" | grep "overlay.rcc: "
    "${A[@]}" push "$ROOT/work/mods/$(basename "$MOD")/overlay.rcc" /tmp/wmmod/hot.rcc > /dev/null
    "${A[@]}" shell "chmod 644 /tmp/wmmod/hot.rcc"
    "${A[@]}" forward tcp:7777 tcp:7777 > /dev/null
    if ! out="$(probe "reload /tmp/wmmod/hot.rcc" 2> /dev/null)"; then
        echo "no probe answers on 127.0.0.1:7777; run tools/devmod.sh $MOD" >&2
        exit 1
    fi
    if grep -q "^unknown command" <<< "$out"; then
        echo "the running probe predates reload; run tools/devmod.sh $MOD once" >&2
        exit 1
    fi
    sed 1d <<< "$out"
    ! grep -q "^not now" <<< "$out"
    exit
fi

# The PC must not hold the storage while the device takes it back.
release_host()
{
    local dev src
    for dev in /dev/disk/by-id/usb-SONY_WALKMAN_*; do
        [ -e "$dev" ] || continue
        src="$(readlink -f "$dev")"
        if findmnt -n -S "$src" > /dev/null; then
            udisksctl unmount -b "$src" > /dev/null
        fi
    done
}

# The app maps the bound files, so they are swapped only while it is stopped.
stop_app()
{
    release_host
    "${A[@]}" shell hagodaemons.sh stop | tail -1
    for _ in $(seq 60); do
        if ! "${A[@]}" shell ps | grep -q HgrmMediaPlayerApp; then
            # Stopped in mass storage mode, the daemons leave the SD card
            # bound to the USB gadget; the next start then cannot mount it
            # and the library comes up empty.
            "${A[@]}" shell "for f in $GADGET/lun*/file; do echo > \$f; done"
            return 0
        fi
        sleep 1
    done
    echo "the app did not stop" >&2
    exit 1
}

# Starting the daemons re-enumerates USB, so adb drops for a few seconds.
start_app()
{
    "${A[@]}" logcat -c
    "${A[@]}" shell hagodaemons.sh start | tail -1
    sleep 5
    timeout 120 "${A[@]}" wait-for-device
    for _ in $(seq 30); do
        "${A[@]}" shell ps 2> /dev/null | grep -q HgrmMediaPlayerApp && return 0
        sleep 2
    done
    echo "the app did not come back" >&2
    exit 1
}

# Binding over an existing bind makes the wrapper exec itself, the app never
# comes up and the watchdog reboots the device, so every run starts unbound.
unbind_all()
{
    local left
    left="$("${A[@]}" shell "for m in $APP /tmp/HgrmMediaPlayerApp.real $PLUGIN; do
                                 grep -q \" \$m \" /proc/mounts && umount \$m
                             done
                             grep -cE 'HgrmMediaPlayerApp|libqeglfs' /proc/mounts" | tr -d '\r' | tail -1)"
    [ "$left" = 0 ] || { echo "bind mounts left: $left" >&2; exit 1; }
}

remote_md5() { "${A[@]}" shell "md5sum $1" | awk '{print $1}'; }

if [ "$MODE" = stock ]; then
    stop_app
    unbind_all
    start_app
    "${A[@]}" shell "md5sum $APP $PLUGIN"
    exit 0
fi

if [ "$MODE" != probe ]; then
    "$ROOT/tools/mkmod.sh" "$MODE" "$ROOT/work/qrc-device" | grep "overlay.rcc: "
fi
# The app's stderr, where its own log goes, is a pty that nobody reads. The
# wrapper keeps its first MiB in /tmp/wmmod/app.log and drains the rest, so
# neither the 32 MB tmpfs fills nor the app blocks on a full pipe.
cat > "$ROOT/work/wrap.sh" << 'EOF'
#!/bin/sh
export WMMOD_OVERLAY=/tmp/wmmod/overlay.rcc
rm -f /tmp/wmmod/stderr
if /system/xbin/mkfifo /tmp/wmmod/stderr; then
    { /system/xbin/head -c 1048576 > /tmp/wmmod/app.log; cat > /dev/null; } < /tmp/wmmod/stderr &
    exec /tmp/HgrmMediaPlayerApp.real "$@" 2> /tmp/wmmod/stderr
fi
exec /tmp/HgrmMediaPlayerApp.real "$@"
EOF

stop_app
unbind_all
app_md5="$(remote_md5 $APP)"
"${A[@]}" push "$ROOT/probe/out/libqeglfs.so" /tmp/libqeglfs.so > /dev/null
"${A[@]}" push "$ROOT/work/wrap.sh" /tmp/wrap.sh > /dev/null
"${A[@]}" shell "mkdir -p /tmp/wmmod && chown system.system /tmp/wmmod && chmod 755 /tmp/wmmod
                 chmod 644 /tmp/libqeglfs.so && chmod 755 /tmp/wrap.sh
                 touch /tmp/wmmod/probe && chmod 644 /tmp/wmmod/probe
                 [ -f /tmp/wmmod/overlay.rcc ] && mv /tmp/wmmod/overlay.rcc /tmp/wmmod/overlay.rcc.off || true"
if [ "$MODE" != probe ]; then
    "${A[@]}" push "$ROOT/work/mods/$(basename "$MODE")/overlay.rcc" /tmp/wmmod/overlay.rcc > /dev/null
    "${A[@]}" shell "chmod 644 /tmp/wmmod/overlay.rcc"
fi
"${A[@]}" shell "touch /tmp/HgrmMediaPlayerApp.real
                 mount -o bind $APP /tmp/HgrmMediaPlayerApp.real
                 mount -o bind /tmp/wrap.sh $APP
                 mount -o bind /tmp/libqeglfs.so $PLUGIN"
if [ "$(remote_md5 /tmp/HgrmMediaPlayerApp.real)" != "$app_md5" ] \
    || [ "$(remote_md5 $APP)" != "$(md5sum < "$ROOT/work/wrap.sh" | awk '{print $1}')" ] \
    || [ "$(remote_md5 $PLUGIN)" != "$(md5sum < "$ROOT/probe/out/libqeglfs.so" | awk '{print $1}')" ]; then
    echo "bind mounts are not what they should be; back to stock" >&2
    unbind_all
    start_app
    exit 1
fi
start_app

"${A[@]}" forward tcp:7777 tcp:7777 > /dev/null
for _ in $(seq 15); do probe overlay > /dev/null 2>&1 && break; sleep 2; done
probe overlay 2>&1 | tail -1

release_host
msc="$(probe "tree 30" | grep -oE '#[0-9]+ MSC_QMLTYPE_[0-9]+' | head -1 | cut -d' ' -f1 | tr -d '#')"
if [ -n "$msc" ]; then
    probe "call $msc enableExportAsMsc false" | tail -1
fi

# With mass storage off the app mounts the SD card and rescans the library,
# but only if it knows the card is there. The connection manager learns that
# from the card-detect driver's insert event, which the driver gives once,
# to the first reader after boot; a restarted one never hears of a card that
# was already in the slot (CARD_DET_REQ_DETECT and slot power do not help, see
# probe/devtools/carddet.c).
if "${A[@]}" shell "[ -b /dev/block/mmcblk1p1 ] && echo present" | grep -q present; then
    mounted=
    for _ in $(seq 30); do
        if "${A[@]}" shell "grep ' /contents_ext ' /proc/mounts" | grep -q contents_ext; then
            mounted=1
            break
        fi
        sleep 1
    done
    [ -n "$mounted" ] && echo "SD card mounted" \
        || echo "warning: the SD card is not mounted; reinsert it or reboot" >&2
fi
