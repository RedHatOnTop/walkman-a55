#!/bin/bash
#
# Host side of the Android bring-up (docs/android.md): assembles the container,
# installs it with the modules and overlays as the boot set on the device's
# internal storage, makes sure the two loop images are there, and drives
# probe/android/android-boot.sh. Once up has installed the set, the device can
# start Android without this host.
#
#   tools/android-boot.sh up          start Android init in the container
#   tools/android-boot.sh install     put the boot set and images on the device
#                                     only, for the picker (picker-upg)
#   tools/android-boot.sh status      list the container's processes
#   tools/android-boot.sh sh CMD      run CMD inside the container
#   tools/android-boot.sh log         show the container's kernel log lines
#   tools/android-boot.sh down        stop the container
#   tools/android-boot.sh pull        copy the container logs into work/android
#   tools/android-boot.sh wifi        Wi-Fi modules / node / netdev state
#   tools/android-boot.sh wifi-unload host side Wi-Fi teardown, after down
#   tools/android-boot.sh btcheck     Bluetooth state on the Sony host (read-only)
#   tools/android-boot.sh btdev       /dev/stpbt inside the running container
#
# The device needs a reboot afterwards to get the stock Mali driver and the
# Sony UI back.
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/tools/serial.sh"; SERIAL="$(nw_serial)" || exit 1
A=(adb -s "$SERIAL")
IMG="${NW_SYS_IMG:-$ROOT/work/android/fire7-unpack/system.img}"
DATA="$ROOT/work/android/android-data.img"
DATA_NAME=wmmod-android/android-data.img
SYS_NAME=wmmod-android/fire-system.img
OUT="$ROOT/work/android/boot-out"
SETOUT="$ROOT/work/android/boot-set"
SET=/tmp/c/wmmod-android/boot
WIFI_CHRDEV="$ROOT/probe/kmod/mtk-wmt-wifi-soc/mtk_wmt_wifi_soc.ko"
WIFI_BP="$ROOT/work/wlan-mt"
GUARD="$ROOT/probe/kmod/swap-accounting-guard/swap_accounting_guard.ko"
GUARD_SHA256=cbcf6d6f9360eb4e91538cd6798178748132997a51873e018d20c08b92227571

CMD="${1:-}"
shift || true

for dev in /dev/disk/by-id/usb-SONY_WALKMAN_*; do
    [ -e "$dev" ] || continue
    src="$(readlink -f "$dev")"
    if findmnt -n -S "$src" > /dev/null; then
        udisksctl unmount -b "$src" > /dev/null
    fi
done

push_once()
{
    local local_file="$1" remote="$2" remote_size
    remote_size="$("${A[@]}" shell "/system/xbin/busybox stat -c %s '$remote' 2>/dev/null" | tr -d '\r')"
    if [ "$remote_size" != "$(stat -c %s "$local_file")" ]; then
        "${A[@]}" push "$local_file" "$remote" > /dev/null
        "${A[@]}" shell sync
    fi
}

remote_md5()
{
    "${A[@]}" shell "/system/xbin/busybox md5sum '$1' 2>/dev/null" | cut -d' ' -f1
}

# A rebuilt system image can keep the old one's size, so size alone would skip
# it. The data image is the container's live /data and must stay size-only.
push_image()
{
    local local_file="$1" remote="$2" sum
    sum="$(md5sum "$local_file" | cut -d' ' -f1)"
    if [ "$(remote_md5 "$remote")" != "$sum" ]; then
        "${A[@]}" push "$local_file" "$remote" > /dev/null
        "${A[@]}" shell sync
        if [ "$(remote_md5 "$remote")" != "$sum" ]; then
            echo "md5 mismatch after pushing $local_file to $remote" >&2
            exit 1
        fi
    fi
    echo "system image on device: md5 $sum"
}

set_add()
{
    "${A[@]}" push "$1" "$SET/$2" > /dev/null
    echo "$(md5sum < "$1" | cut -d' ' -f1)  $2" >> "$SETOUT/MANIFEST"
}

# Everything the device script's up needs besides the two images, under $SET
# with a MANIFEST of md5 sums, written last. The device stages only what the
# MANIFEST lists and refuses a set that no longer matches it, so a push that
# stopped half way leaves a set that will not start rather than a mixed one.
install_set()
{
    local f ko
    mkdir -p "$SETOUT"
    : > "$SETOUT/MANIFEST"
    "${A[@]}" shell "/system/xbin/busybox mkdir -p $SET/wifi $SET/idc-overlay $SET/kl-overlay $SET/vendor-overlay $SET/bt-apk"
    set_add "$ROOT/probe/android/android-boot.sh" android-boot.sh
    set_add "$ROOT/probe/out/pivotinit" pivotinit
    set_add "$ROOT/work/android/container.tgz" container.tgz
    set_add "$ROOT/probe/kmod/mali-r4p0/mali_r4p0.ko" mali_r4p0.ko
    echo "$GUARD_SHA256  $GUARD" | sha256sum --quiet -c || { echo "$GUARD is not the audited build" >&2; exit 1; }
    set_add "$GUARD" swap_accounting_guard.ko
    # Four Wi-Fi modules, all this repo's builds; the device script loads them
    # host-side before the container starts (devlog
    # 2026-10-15-wifi-container-groundwork.md). The chardev
    # (probe/kmod/mtk-wmt-wifi-soc) is station-only: the WIFITEST bundle's
    # registers P2P on netd's "STA" write, and the next wpa_supplicant request
    # then oopsed cfg80211. wlan_mt (probe/kmod/wlan-mt) gives its STA
    # testmode_cmd and mgmt_tx backports 3.17's arguments: the bundle's took
    # 3.10's, and CM's "DRIVER SETSUSPENDMODE" oopsed cfg80211 through
    # testmode_cmd. compat and cfg80211 come from the same backports build
    # and have the bundle's code byte for byte (make -C probe/kmod/wlan-mt verify).
    # Opt-in with NW_WIFI=1. Modules an earlier up in the same device boot
    # loaded stay loaded, so an A/B starts from a reboot.
    if [ -z "${NW_WIFI:-}" ] || [ -n "${NW_NO_WIFI:-}" ]; then
        echo "Wi-Fi off: NW_WIFI=1 loads the modules"
    else
        [ -f "$WIFI_CHRDEV" ] || { echo "missing $WIFI_CHRDEV: make -C probe/kmod/mtk-wmt-wifi-soc verify" >&2; exit 1; }
        set_add "$WIFI_CHRDEV" wifi/mtk_wmt_wifi_soc.ko
        for ko in compat_bp cfg80211_bp wlan_mt_bp; do
            [ -f "$WIFI_BP/$ko.ko" ] || { echo "missing $WIFI_BP/$ko.ko: make -C probe/kmod/wlan-mt verify" >&2; exit 1; }
            set_add "$WIFI_BP/$ko.ko" "wifi/$ko.ko"
        done
    fi
    for f in "$ROOT"/probe/android/idc/*.idc; do
        set_add "$f" "idc-overlay/${f##*/}"
    done
    for f in "$ROOT"/probe/android/keylayout/*.kl; do
        set_add "$f" "kl-overlay/${f##*/}"
    done
    # Resource overlays for the container's /vendor/overlay. CM 12.1 applies a
    # static overlay only to framework-res; an app-targeted one stays inert, so
    # the A2DP-only Bluetooth change is the patched APK below instead.
    for f in "$ROOT"/work/android/overlay/*.apk; do
        [ -f "$f" ] || break
        set_add "$f" "vendor-overlay/${f##*/}"
    done
    # A2DP-only Bluetooth.apk (probe/android/connectivity/bt/a2dp-only/build.sh),
    # bound over the image's copy by android-boot.sh.
    if [ -f "$ROOT/work/android/bt/Bluetooth.apk" ]; then
        set_add "$ROOT/work/android/bt/Bluetooth.apk" bt-apk/Bluetooth.apk
    else
        echo "no work/android/bt/Bluetooth.apk: Bluetooth keeps HFP/HSP and the other profiles" >&2
    fi
    # Optional Bluetooth BD-address control (probe/android/connectivity/bt):
    # both NW_BT_SHIM (the built libnvram-btshim.so) and NW_BT_SEED (the file
    # gen-bt-nvram.py wrote) must be set; otherwise the container uses
    # libbluetooth_mtk's built-in default record.
    if [ -n "${NW_BT_SHIM:-}" ] && [ -n "${NW_BT_SEED:-}" ]; then
        set_add "$NW_BT_SHIM" libnvram-btshim.so
        set_add "$NW_BT_SEED" bt_nvram.bin
        echo "Bluetooth: libnvram shim and BD-address seed in the boot set"
    fi
    # NW_DROP_SYS_BOOT=1 arms the CAP_SYS_BOOT experiment: the container's init
    # runs with that capability dropped from its bounding set, so nothing inside
    # can reboot the shared kernel.
    if [ -n "${NW_DROP_SYS_BOOT:-}" ]; then
        : > "$SETOUT/drop-sys-boot"
        set_add "$SETOUT/drop-sys-boot" drop-sys-boot
        echo "NW_DROP_SYS_BOOT set: the container will run without CAP_SYS_BOOT"
    fi
    # The picker's own files (picker-upg's bootswitcher.sh hook runs them at
    # boot). Not in MANIFEST: up never stages them.
    [ -x "$ROOT/probe/out/bootpick" ] || { echo "run ./probe/build.sh tools first" >&2; exit 1; }
    python3 "$ROOT/tools/bootpick_frames.py" "$SETOUT/frames.raw"
    "${A[@]}" shell "/system/xbin/busybox mkdir -p $SET/picker"
    for f in "$ROOT/probe/out/bootpick" "$SETOUT/frames.raw"; do
        "${A[@]}" push "$f" "$SET/picker/${f##*/}" > /dev/null
        [ "$(remote_md5 "$SET/picker/${f##*/}")" = "$(md5sum < "$f" | cut -d' ' -f1)" ] || \
            { echo "md5 mismatch after pushing $f" >&2; exit 1; }
    done
    "${A[@]}" push "$SETOUT/MANIFEST" "$SET/MANIFEST" > /dev/null
    "${A[@]}" shell sync
    f="$("${A[@]}" shell "cd $SET && /system/xbin/busybox md5sum -c MANIFEST 2>&1" | tr -d '\r' | grep -v ': OK$' || true)"
    [ -z "$f" ] || { echo "the boot set on the device does not match its MANIFEST: $f" >&2; exit 1; }
    echo "boot set: $(wc -l < "$SETOUT/MANIFEST") files in $SET"
}

push_helpers()
{
    "${A[@]}" push "$ROOT/probe/android/android-boot.sh" /tmp/android-boot.sh > /dev/null
    "${A[@]}" push "$ROOT/probe/out/pivotinit" /tmp/pivotinit > /dev/null
    "${A[@]}" shell "chmod 755 /tmp/pivotinit /tmp/android-boot.sh"
}

case "$CMD" in
up)
    "$ROOT/tools/android-container.sh"
    if [ ! -f "$DATA" ]; then
        # 256 MB was too small. Measured in the container: `df /data` showed
        # 229.9M total with 180.8M used and 49.2M free once dalvik-cache held
        # ~50 entries, and entries average about 5 MB, so the output for the
        # ~248 dexopt inputs on this image cannot fit. The image lives on
        # /contents (vfat, ~10.5 GB free), so there is room for this.
        # Kept just under 2 GiB: the device adbd opens the push target without
        # O_LARGEFILE, so a file of 2^31 bytes or more is rejected with EFBIG
        # ("remote File too large"). 2048M hit that exactly; 2000M clears it.
        truncate -s 2000M "$DATA"
        if ! mke2fs -q -t ext4 -F -L android-data -O ^metadata_csum,^64bit,^orphan_file "$DATA" 2>/dev/null; then
            mke2fs -q -t ext4 -F -L android-data -O ^metadata_csum,^64bit "$DATA"
        fi
    fi
    [ -f "$IMG" ] || { echo "missing $IMG" >&2; exit 1; }
    [ -x "$ROOT/probe/out/pivotinit" ] || { echo "run ./probe/build.sh tools first" >&2; exit 1; }
    push_helpers
    "${A[@]}" shell "sh /tmp/android-boot.sh prepare"
    install_set
    push_image "$IMG" "/tmp/c/$SYS_NAME"
    push_once "$DATA" "/tmp/c/$DATA_NAME"
    # The set's own copy, so every up runs what the device runs on its own.
    "${A[@]}" shell "sh $SET/android-boot.sh up"
    ;;
install)
    # The boot set and the images only, for the picker to start at boot.
    "$ROOT/tools/android-container.sh"
    [ -f "$DATA" ] || { echo "no $DATA yet: run up once" >&2; exit 1; }
    [ -f "$IMG" ] || { echo "missing $IMG" >&2; exit 1; }
    push_helpers
    "${A[@]}" shell "sh /tmp/android-boot.sh prepare"
    install_set
    push_image "$IMG" "/tmp/c/$SYS_NAME"
    push_once "$DATA" "/tmp/c/$DATA_NAME"
    ;;
status|log|down|prepare|wifi|wifi-unload|btcheck|btdev)
    push_helpers
    "${A[@]}" shell "sh /tmp/android-boot.sh $CMD"
    ;;
sh)
    push_helpers
    "${A[@]}" shell "sh /tmp/android-boot.sh sh $(printf '%q' "$*")"
    ;;
pull)
    mkdir -p "$OUT"
    for f in init.out; do
        "${A[@]}" pull "/tmp/aroot/$f" "$OUT/$f" > /dev/null 2>&1 || echo "no $f"
    done
    "${A[@]}" shell "sh /tmp/android-boot.sh sh 'logcat -d -v time'" > "$OUT/logcat.txt" 2>&1 || true
    "${A[@]}" shell "sh /tmp/android-boot.sh sh 'dumpsys -l'" > "$OUT/dumpsys-l.txt" 2>&1 || true
    ls -l "$OUT"
    ;;
*)
    echo "usage: tools/android-boot.sh up | install | status | sh CMD | log | wifi | wifi-unload | down | pull | btcheck | btdev" >&2
    exit 2
    ;;
esac
