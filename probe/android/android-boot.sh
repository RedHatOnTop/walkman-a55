#!/system/bin/sh
#
# Runs on the device as root: brings an Android 5.1 userspace (the FireOS 5
# system image) up inside a chroot, under Android's own init, so that the
# property service, logd, servicemanager, SurfaceFlinger and zygote run.
#
#   sh android-boot.sh prepare     stop the Sony stack, mount /contents on /tmp/c
#   sh android-boot.sh up          prepare + swap Mali + build the container
#                                  + start Android init
#   sh android-boot.sh boot        up from the picker at boot, before any Sony
#                                  service; reboots into Walkman if it fails
#   sh android-boot.sh status      list the container's processes
#   sh android-boot.sh sh CMD      run CMD inside the container
#   sh android-boot.sh log         show the container's kernel log lines
#   sh android-boot.sh wifi        Wi-Fi modules / node / netdev state (host + container)
#   sh android-boot.sh wifi-unload host side Wi-Fi teardown, after down
#   sh android-boot.sh down        kill the container and unmount everything
#   sh android-boot.sh powerwatch  act on the container's power-off / reboot
#                                  requests, put the USB port back after a
#                                  resume (up starts it)
#
# up needs the boot set and the two images in wmmod-android on the internal
# storage (/emmc@contents, mounted on /tmp/c). tools/android-boot.sh installs
# them, so after that the device can run this script from there on its own:
#   sh /tmp/c/wmmod-android/boot/android-boot.sh up
#
# Android init mounts tmpfs on /dev and mounts proc and sysfs at startup. Those
# paths are under $ROOT, so the host /dev, /proc and /sys are not touched and no
# mount namespace is needed. Nothing here survives a reboot.

BB=/system/xbin/busybox
GADGET=/sys/devices/platform/mt_usb/musb-hdrc.0/gadget
PMH=/sys/devices/platform/icx_pm_helper
ROOT=/tmp/aroot
VOL=/tmp/avol
C=/tmp/c
SYSIMG=wmmod-android/fire-system.img
DATAIMG=wmmod-android/android-data.img
SET=$C/wmmod-android/boot
WIFIKO=/tmp/wifi

# The picker's boot-android.log has no clock of its own; the uptime times the steps.
say() { echo "== $($BB cut -d' ' -f1 /proc/uptime) $*"; }
die() { echo "!! $*"; exit 1; }

# pivotinit writes its pid to $ROOT/init.pid. The container's processes are
# found by mount namespace: after pivot_root their root reads back as "/", not
# as $ROOT, so a root comparison does not identify them.
container_init_pid()
{
    pid="$($BB cat $ROOT/init.pid 2>/dev/null | $BB head -1)"
    [ -n "$pid" ] || return
    [ "$($BB cat /proc/$pid/comm 2>/dev/null)" = "init" ] || return
    echo "$pid"
}

# Anything not in the mount namespace Sony's init is in belongs to a container,
# current or stale. Matching only the *current* init's namespace leaked: a
# process left behind by an earlier run sits in its own namespace, survives
# down(), and keeps holding global devices. Measured: four system_server
# processes alive at once, all holding /dev/alarm, so the live one's
# AlarmManagerService looped on "Unable to wait on alarm: Device or resource
# busy" and the boot never finished.
#
# One ls for every process, not a readlink each: a busybox call costs this CPU
# some 50 ms while the unit boots, and the loop took 7 s over 136 processes.
container_pids()
{
    $BB ls -l /proc/[0-9]*/ns/mnt 2>/dev/null | $BB awk -v base="$($BB readlink /proc/1/ns/mnt 2>/dev/null)" '
        base != "" && $(NF - 1) == "->" && $NF != base { split($(NF - 2), p, "/"); print p[3] }'
}

# Processes from earlier runs, i.e. container processes that are not the current
# init's. up() clears these before building a new container.
stale_pids()
{
    init_pid="$(container_init_pid)"
    ns=""
    [ -n "$init_pid" ] && ns="$($BB readlink /proc/$init_pid/ns/mnt 2>/dev/null)"
    for p in $(container_pids); do
        [ "$($BB readlink /proc/$p/ns/mnt 2>/dev/null)" = "$ns" ] || echo "$p"
    done
}

# --- Bluetooth: see devlog/2026-10-15-bt-consys-groundwork.md ----------------
#
# Sony's BlueAngel daemon, mtkbt, is an init service (init.rc "start mtkbt"),
# not a hagodaemon, so hagodaemons.sh stop leaves it running. It is the only
# host process that opens /dev/stpbt (through libbtdrv.so's mtk_bt_enable, on a
# BT-on request from BtCommonService). The kernel node (stp_chrdev_bt.c) does
# not refuse a second opener: both would share one rx queue, and either close
# turns the BT function off for both. So mtkbt is stopped before the container
# can open the node.
#
# 6620_launcherA is deliberately left running. The WMT driver asks it for the
# firmware patches over /dev/stpwmtA on every chip power-on ("srh_patch"), so
# Bluetooth in the container cannot power the chip up without it. The patches
# themselves are read by the kernel from the host's /system/etc/firmware, so the
# container needs no firmware files.
bt_holders()
{
    $BB ls -l /proc/[0-9]*/fd 2>/dev/null | $BB awk '
        /^\/proc\/[0-9]+\/fd:$/ { split($0, p, "/"); pid = p[3] }
        $(NF - 1) == "->" && $NF == "/dev/stpbt" && pid != last { print pid; last = pid }'
}

bt_host_release()
{
    say "release /dev/stpbt from the Sony stack"
    if $BB ps | $BB grep -q '[m]tkbt'; then
        stop mtkbt
        for i in $($BB seq 100); do
            $BB ps | $BB grep -q '[m]tkbt' || break
            $BB sleep 0.1
        done
    fi
    # Warnings, not die: a Bluetooth problem must not stop the graphics bring-up.
    $BB ps | $BB grep -q '[m]tkbt' && echo "!! mtkbt is still running"
    $BB ps | $BB grep -q '[6]620_launcherA' || echo "!! 6620_launcherA is not running: Bluetooth cannot power the chip up"
    [ -c /dev/stpbt ] || echo "!! no /dev/stpbt on the host"
    held="$(bt_holders)"
    [ -z "$held" ] || echo "!! /dev/stpbt is still open in pid(s): $held"
}

# Read-only report; safe at any time, container up or not.
btcheck()
{
    say "Bluetooth, host side"
    $BB ps | $BB grep -E '[m]tkbt|[6]620_launcher|[n]vram_daemon' || echo "(mtkbt, 6620_launcherA, nvram_daemon: none running)"
    $BB ls -l /dev/stpbt /dev/stpwmtA /dev/wmtdetect 2>&1
    echo "persist.mtk.wcn.combo.chipid = [$(getprop persist.mtk.wcn.combo.chipid)]"
    echo "service.nvram_init = [$(getprop service.nvram_init)]"
    echo "pids holding /dev/stpbt: [$(bt_holders)]"
    $BB grep -E '^(mtk_stp_bt_soc|mtk_stp_wmt_soc|mtk_wmt_detect) ' /proc/modules
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
    # icx_pm_helper takes a wakelock for resume_lock_ms (20 s) on every
    # resume, and Sony's PowerService cancels it once it has handled the wake.
    # powerwatch takes over the cancel. The lock starts before the ~0.9 s of
    # device resume, so 3 s leaves powerwatch's 0.5 s poll room to put the
    # USB port back first, and caps the cost of a wake if powerwatch is gone.
    # Android holds its own wakelocks for what it handles (alarm, KeyEvents,
    # eventpoll).
    echo 3000 > $PMH/resume_lock_ms
    for f in $GADGET/lun*/file; do echo > $f; done
    # Sony's storage manager mounts the card here with exfatfuse; the
    # container's vold takes it over.
    for m in /contents_ext /contents_ext1; do
        $BB grep -q " $m " /proc/mounts || continue
        fusermount -u $m || die "could not unmount $m"
    done
    $BB mkdir -p $C
    $BB grep -q " $C " /proc/mounts || $BB mount -t vfat /emmc@contents $C || die "contents mount failed"
    $BB df -h $C | $BB tail -1
}

# The boot set goes into /tmp where the rest of this script expects it, but
# only what its MANIFEST lists, so a file an earlier install left behind (the
# Wi-Fi modules once NW_WIFI is dropped) is never used. Modules and the
# container tarball are only read, so they are links into the set and cost no
# tmpfs RAM. The rest is copied: the overlays are chmodded and bound into the
# container, and the vfat mount gives root-only modes. A copy is renamed into
# place, so this very script can be replaced while it runs.
STAGED="android-boot.sh pivotinit container.tgz mali_r4p0.ko swap_accounting_guard.ko wifi
    idc-overlay kl-overlay vendor-overlay bt-apk libnvram-btshim.so bt_nvram.bin drop-sys-boot"

stage()
{
    say "stage the boot set"
    [ -z "$(container_init_pid)" ] || die "the container is already up; run down first"
    [ -f $SET/MANIFEST ] || die "no $SET/MANIFEST; install the set with tools/android-boot.sh up"
    bad="$(cd $SET && $BB md5sum -c MANIFEST 2>&1 | $BB grep -v ': OK$')"
    [ -z "$bad" ] || die "the boot set does not match its MANIFEST: $bad"
    for f in $STAGED; do
        [ "$f" = android-boot.sh ] || $BB rm -rf /tmp/$f
    done
    while read sum rel; do
        case "$rel" in
            */*) $BB mkdir -p "/tmp/${rel%/*}" ;;
        esac
        case "$rel" in
            *.ko|container.tgz) $BB ln -s "$SET/$rel" "/tmp/$rel" ;;
            *) $BB cp "$SET/$rel" "/tmp/$rel.new" && $BB mv "/tmp/$rel.new" "/tmp/$rel" ;;
        esac || die "could not stage $rel"
    done < $SET/MANIFEST
    $BB chmod 755 /tmp/pivotinit /tmp/android-boot.sh
    echo "$($BB wc -l < $SET/MANIFEST) files from $SET"
}

# zram (below) resets the stock kernel without this guard (devlog
# 2026-10-12-patched-zram-reset.md). It is RAM-only, so every device boot needs
# it again. It cannot be unloaded, and a refused load leaves that boot in an
# unknown state: reboot the device before trying again. The host installs only
# the audited build.
guard()
{
    say "swap accounting guard"
    $BB grep -q '^swap_accounting_guard ' /proc/modules || insmod /tmp/swap_accounting_guard.ko
    case "$($BB grep '^swap_accounting_guard ' /proc/modules)" in
        *permanent*) ;;
        *) die "swap guard not resident; reboot the device before trying again" ;;
    esac
    $BB dmesg | $BB grep 'swap-accounting-guard: writes' | $BB tail -1
}

swap()
{
    say "unbind the stock driver"
    if [ -e /sys/bus/platform/drivers/mali-utgard/mali-utgard.0 ]; then
        echo mali-utgard.0 > /sys/bus/platform/drivers/mali-utgard/unbind
    fi
    [ -e /sys/bus/platform/drivers/mali-utgard/mali-utgard.0 ] && die "unbind failed"

    say "load mali_r4p0"
    $BB grep -q '^mali_r4p0 ' /proc/modules || insmod /tmp/mali_r4p0.ko || die "insmod failed"
    [ -e /sys/class/misc/mali/dev ] || die "no mali misc device"
    echo "mali dev $($BB cat /sys/class/misc/mali/dev)"
}

# Wi-Fi, host side. The stock kernel has no cfg80211 and no WLAN driver, so four
# RAM-only modules are loaded here, in the WIFITEST bundle's insmod order, BEFORE
# start_init: the repo's station-only rebuild of the bundle's chardev
# (probe/kmod/mtk-wmt-wifi-soc), then compat, cfg80211 and wlan_mt from
# probe/kmod/wlan-mt (the bundle's first run: docs/wifi-test.md). That ordering is
# load-bearing: mtk_wmt_wifi_soc creates the class device wmtWifi
# (WMT_CREATE_NODE_DYNAMIC=1), and the container's ueventd makes /dev/wmtWifi
# (0660 system system, from ueventd.icx.rc) only while it runs its coldboot at
# early-init. A module loaded later leaves the container without the node.
# mac80211_bp is not loaded: wlan_mt_bp does not depend on it.
# Nothing here powers the radio. The container's init writes 1 to /dev/wmtWifi
# when the framework sets wlan.driver.status=ok (init.aosp.rc), which is what
# creates wlan0. Non-fatal: up() carries on without Wi-Fi when this fails.
wifi_loaded() { $BB grep -q "^$1 " /proc/modules; }

wifi_modules()
{
    say "wifi kernel modules (host side)"
    [ -f $WIFIKO/mtk_wmt_wifi_soc.ko ] || { echo "no $WIFIKO/*.ko in the boot set"; return 1; }
    wifi_loaded mtk_stp_wmt_soc || { echo "mtk_stp_wmt_soc (Sony WMT core) is not loaded"; return 1; }
    [ -r /etc/firmware/WIFI_RAM_CODE_8590 ] || { echo "no /etc/firmware/WIFI_RAM_CODE_8590 on the host"; return 1; }
    for ko in mtk_wmt_wifi_soc compat_bp cfg80211_bp wlan_mt_bp; do
        m="${ko%_bp}"
        if wifi_loaded "$m"; then
            echo "already loaded: $m"
            continue
        fi
        insmod $WIFIKO/$ko.ko || { echo "insmod $ko failed"; return 1; }
        echo "loaded: $m"
    done
    [ -e /sys/class/wmtWifi/wmtWifi/dev ] || { echo "no /sys/class/wmtWifi/wmtWifi/dev"; return 1; }
    echo "wmtWifi dev $($BB cat /sys/class/wmtWifi/wmtWifi/dev)"
    echo "WMT launcher state (kept by the Sony side):"
    $BB ps | $BB grep -E '[6]620_launcher|[w]mt_loader|[m]tkbt' || echo "  none of 6620_launcher*, wmt_loader, mtkbt is running"
}

wifi_status()
{
    say "wifi modules"
    $BB grep -E '^(mtk_stp_wmt_soc|mtk_wmt_wifi_soc|compat|cfg80211|wlan_mt) ' /proc/modules
    say "wifi char device"
    $BB awk '$2 ~ /WIFI/' /proc/devices
    echo "class dev: $($BB cat /sys/class/wmtWifi/wmtWifi/dev 2>/dev/null)"
    say "host view"
    $BB ls -l /dev/wmtWifi 2>&1
    if [ -e /sys/class/net/wlan0 ]; then
        echo "wlan0 present, address $($BB cat /sys/class/net/wlan0/address)"
    else
        echo "no wlan0 (radio is off, or modules are not loaded)"
    fi
    pid="$(container_init_pid)"
    if [ -n "$pid" ]; then
        say "container view (init pid $pid)"
        $BB ls -l /proc/$pid/root/dev/wmtWifi 2>&1
        echo "net ns host: $($BB readlink /proc/1/ns/net)"
        echo "net ns init: $($BB readlink /proc/$pid/ns/net)"
    fi
}

# Host side teardown for after down. Powers the radio off first, then removes the
# modules in reverse dependency order (the order of the bundle's wifi_unload.sh).
wifi_unload()
{
    say "wifi teardown (host side)"
    [ -z "$(container_pids)" ] || die "the container is still up; run down first"
    [ -c /dev/wmtWifi ] && echo 0 > /dev/wmtWifi
    $BB sleep 1
    for m in wlan_mt cfg80211 compat mtk_wmt_wifi_soc; do
        if wifi_loaded "$m"; then
            rmmod "$m" && echo "rmmod $m ok" || echo "rmmod $m FAILED"
        fi
    done
}

# overlay_fill DIR STAGED: DIR's files from the image with the boot set's
# staged files on top, in STAGED, to bind over DIR. Two copies, not one per
# file: the per-file loop took 3.7 s for the 35 key layouts while booting.
overlay_fill()
{
    $BB rm -rf "$2.set"
    $BB mv "$2" "$2.set" && $BB mkdir "$2" || die "could not set $2 aside"
    $BB cp "$1"/* "$2"/
    $BB cp "$2.set"/* "$2"/ || die "could not copy the boot set's files into $2"
}

container()
{
    say "build the container root"
    if [ -n "$(container_init_pid)" ]; then
        die "the container is already up; run down first"
    fi
    # Processes left behind by an earlier run hold the loop mounts and global
    # devices, so clear them before touching $ROOT.
    stale="$(stale_pids)"
    if [ -n "$stale" ]; then
        say "kill processes left by an earlier container run"
        for p in $stale; do kill -9 "$p" 2>/dev/null; done
        $BB sleep 1
        stale="$(stale_pids)"
        [ -z "$stale" ] || die "stale container processes survived: $stale"
    fi
    # A leftover mount under $ROOT would make rm -rf fail part-way and leave the
    # data image mounted and about to be overwritten, so refuse instead.
    if $BB grep -q " $ROOT" /proc/mounts; then
        die "$ROOT still has mounts; run down first"
    fi
    $BB rm -rf $ROOT
    $BB mkdir -p $ROOT
    $BB tar -xzf /tmp/container.tgz -C $ROOT || die "container extract failed"
    [ -x $ROOT/init ] || die "no init in the container"
    [ -f $ROOT/init.rc ] || die "no init.rc in the container"

    # vold owns whatever the fstab names. The only thing it may have is the
    # microSD slot (msdc1); the eMMC holds the partitions the project rules
    # forbid writing, so refuse any other line, and refuse if msdc1 ever
    # carries something that is not an SD card.
    for f in $ROOT/fstab.*; do
        [ -e "$f" ] || continue
        [ "$f" = "$ROOT/fstab.icx" ] || die "refusing to start: unexpected $f"
        bad="$($BB awk '!/^[[:space:]]*(#|$)/ && ($1 !~ "^/devices/platform/mtk-msdc[.]1/" || $5 !~ /voldmanaged=/)' "$f")"
        [ -z "$bad" ] || die "refusing to start: $f names more than the microSD slot: $bad"
    done
    for b in /sys/block/mmcblk*; do
        case "$($BB readlink -f $b)" in
            */mtk-msdc.1/*)
                [ "$($BB cat $b/device/type 2>/dev/null)" = SD ] || \
                    die "refusing to start: $b is on msdc1 but is not an SD card" ;;
        esac
    done
    # vold mounts the card read-write; a Sony-side mount as well would be two
    # writers on one exFAT volume.
    $BB grep -qE 'exfatfuse|^/dev/block/mmcblk1' /proc/mounts && \
        die "refusing to start: the microSD card is still mounted on the host"

    # The CM image carries MTK's own connectivity bring-up. None of it may run in
    # the container: Sony's 6620_launcherA on the host already owns the chip (and
    # the node is /dev/stpwmtA, CM's 6620_launcher opens /dev/stpwmt), wmt_loader
    # would re-probe the chip and overwrite persist.mtk.wcn.combo.chipid, and
    # nvram_daemon would restore from the NVRAM partition. Fail loudly if one of
    # them is ever added to the container init.
    for f in $ROOT/init*.rc; do
        [ -e "$f" ] || continue
        $BB grep -qE '^service[[:space:]]+(wmt_loader|wmtLoader|6620_launcher|conn_launcher|66xx_launcher[AC]?|nvram_daemon|NvRAMAgent)[[:space:]]' "$f" && \
            die "refusing to start: $f runs MTK connectivity bring-up or nvram_daemon in the container"
    done

    # ART's Runtime::Init runs mount("rootfs", "/", NULL, MS_SLAVE|MS_REC, NULL)
    # and gives up ("CreateJavaVM failed") when that fails. Changing mount
    # propagation needs the target to be a mount point, so bind the container
    # root onto itself first. pivotinit then moves this mount over /.
    $BB mount -o bind $ROOT $ROOT || die "container self bind failed"

    $BB grep -q " $C " /proc/mounts || die "run prepare first"
    [ -f "$C/$SYSIMG" ] || die "no $C/$SYSIMG"
    [ -f "$C/$DATAIMG" ] || die "no $C/$DATAIMG"

    $BB mkdir -p $ROOT/system $ROOT/data $ROOT/cache $ROOT/dev $ROOT/proc $ROOT/sys
    $BB mount -o loop,ro -t ext4 "$C/$SYSIMG" $ROOT/system || die "system loop mount failed"

    # The stock MTK primary HAL (audio.primary.mt8127.so) links libnvram.so and
    # libcustom_nvram.so, which reach for the NVRAM and PRO_INFO partitions on
    # the project's forbidden list, so it cannot run here. We replace it in the
    # system image with our own CXD3778GF tinyalsa HAL (probe/audiohal/, built
    # under the same mt8127 name), which drives the live sonysoccard DAC;
    # ueventd creates its /dev/snd/* nodes (ueventd.rc: "/dev/snd/* 0660 system
    # audio"). When the in-image HAL is ours (it carries the "NW-A55 CXD3778GF"
    # marker), AudioFlinger's hw_get_module() loads it directly, no bind.
    #
    # If the image still has the stock MTK HAL, fall back to the old masking:
    # bind the AOSP no-op stub (audio.primary.default.so, returns 0 and touches
    # no hardware) over the mt8127 name, so AudioFlinger still gets a primary and
    # AudioSystem calls return at once. A missing primary wedged system_server
    # before -- AudioService.onReceive's setParameters() blocked in the native
    # call until Watchdog killed the process and forced a runtime restart.
    audio_hal="$ROOT/system/lib/hw/audio.primary.mt8127.so"
    audio_stub="$ROOT/system/lib/hw/audio.primary.default.so"
    if [ -e "$audio_hal" ]; then
        if $BB grep -q "NW-A55 CXD3778GF" "$audio_hal"; then
            echo "audio HAL: in-image CXD3778GF HAL, loaded directly (no bind)"
        else
            [ -e "$audio_stub" ] || die "no stub audio HAL $audio_stub on the system image"
            $BB grep -q " $audio_hal " /proc/mounts || \
                $BB mount -o bind "$audio_stub" "$audio_hal" || die "could not bind the stub audio HAL"
            $BB mount -o remount,bind,ro "$audio_hal" || die "could not remount stub audio HAL read-only"
            echo "stub audio HAL bound: $audio_hal <- $audio_stub (ro)"
        fi
    fi

    # keystore the same way. The image's keystore.mt8127.so wants the MTK TEE
    # keymaster, which this container cannot reach, and hw_get_module() takes
    # the board's name before the default, so keystore exited at start. Without
    # it CM's ProfileManagerService NPEs on KeyStore and takes the caller down:
    # Settings crashed as the setup wizard finished on a fresh /data (10-22).
    # keystore.default.so is AOSP's software keymaster under the same module id.
    ks_hal="$ROOT/system/lib/hw/keystore.mt8127.so"
    ks_soft="$ROOT/system/lib/hw/keystore.default.so"
    if [ -e "$ks_hal" ] && [ -e "$ks_soft" ]; then
        $BB grep -q " $ks_hal " /proc/mounts || \
            $BB mount -o bind "$ks_soft" "$ks_hal" || die "could not bind the software keymaster"
        $BB mount -o remount,bind,ro "$ks_hal" || die "could not remount the software keymaster read-only"
        echo "software keymaster bound: $ks_hal <- $ks_soft (ro)"
    fi

    # Empty RAM directory over the Fire UI sounds, after the read-only system
    # mount and before container() returns. up() calls start_init only after
    # that. SoundPool.load of a missing Effect_Tick.ogg / Keypress*.ogg returns
    # sample 0 and does not decode. The system image is not written.
    # One mount -o bind,ro is not used: on this 3.10 kernel the ro flag is
    # applied to a bind by a following remount,bind,ro.
    ui_src=/tmp/android-ui-sounds-empty
    $BB rm -rf $ui_src
    $BB mkdir -p $ui_src || die "could not create $ui_src"
    $BB chown 0:0 $ui_src || die "could not chown $ui_src"
    $BB chmod 755 $ui_src || die "could not chmod $ui_src"
    [ -z "$($BB ls -A $ui_src)" ] || die "$ui_src is not empty"
    ui_dst="$ROOT/system/media/audio/ui"
    [ -d "$ui_dst" ] || die "no $ui_dst on the system image"
    $BB grep -q " $ui_dst " /proc/mounts || \
        $BB mount -o bind $ui_src $ui_dst || die "could not bind empty ui sounds"
    $BB mount -o remount,bind,ro $ui_dst || die "could not remount ui sounds read-only"
    echo "ui sounds hidden: $ui_dst <- $ui_src (ro)"

    # The system image is read-only, so extra input device configuration rides
    # in as a copy of /system/usr/idc plus the host's files, bound over it.
    if [ -d /tmp/idc-overlay ]; then
        overlay_fill $ROOT/system/usr/idc /tmp/idc-overlay
        # system_server reads these as uid system; a root umask must not hide them
        $BB chmod 755 /tmp/idc-overlay
        $BB chmod 644 /tmp/idc-overlay/*
        $BB mount -o bind /tmp/idc-overlay $ROOT/system/usr/idc || die "idc overlay bind failed"
        echo "idc overlay: $($BB ls /tmp/idc-overlay | $BB tr '\n' ' ')"
    fi
    # Key layouts the same way (probe/android/keylayout).
    if [ -d /tmp/kl-overlay ]; then
        overlay_fill $ROOT/system/usr/keylayout /tmp/kl-overlay
        $BB chmod 755 /tmp/kl-overlay
        $BB chmod 644 /tmp/kl-overlay/*
        $BB mount -o bind /tmp/kl-overlay $ROOT/system/usr/keylayout || die "keylayout overlay bind failed"
        echo "keylayout overlay: $($BB ls /tmp/kl-overlay | $BB grep -v '^Vendor_' | $BB tr '\n' ' ')"
    fi

    # Wi-Fi Direct is hidden from the framework. The proven wlan_mt build lists
    # only IBSS and managed interface modes (docs/wifi-test.md run log, iw phy)
    # and has no p2p0, so FEATURE_WIFI_DIRECT would make WifiStateMachine start
    # the p2p_supplicant service. The same overlay trick as for the idc files:
    # a copy of /system/etc/permissions minus android.hardware.wifi.direct.xml,
    # bound over the read-only original.
    perm_src=/tmp/perm-overlay
    perm_dst="$ROOT/system/etc/permissions"
    if [ -d "$perm_dst" ]; then
        $BB rm -rf $perm_src
        $BB mkdir -p $perm_src || die "could not create $perm_src"
        $BB cp "$perm_dst"/* $perm_src/
        $BB rm -f $perm_src/android.hardware.wifi.direct.xml
        $BB chmod 755 $perm_src
        $BB chmod 644 $perm_src/*
        $BB mount -o bind $perm_src "$perm_dst" || die "permissions overlay bind failed"
        $BB mount -o remount,bind,ro "$perm_dst" || die "could not remount permissions overlay read-only"
        echo "permissions overlay: wifi.direct hidden ($($BB ls $perm_src | $BB wc -l) files)"
    fi

    # Static resource overlays (RRO). PackageManagerService scans /vendor/overlay
    # before any app; in this container /vendor is init.aosp.rc's symlink to the
    # read-only /system/vendor. When overlays were pushed, /vendor becomes a real
    # directory instead: one symlink per /system/vendor entry plus overlay/, and
    # init's own symlink then fails with EEXIST. Carries the A2DP-only Bluetooth
    # overlay (probe/android/connectivity/bt/a2dp-only-overlay).
    if [ -n "$($BB ls /tmp/vendor-overlay/*.apk 2>/dev/null)" ]; then
        $BB mkdir -p $ROOT/vendor/overlay || die "could not create $ROOT/vendor/overlay"
        for e in $ROOT/system/vendor/*; do
            [ -e "$e" ] || continue
            $BB ln -s "/system/vendor/${e##*/}" "$ROOT/vendor/${e##*/}" || die "vendor symlink ${e##*/} failed"
        done
        $BB cp /tmp/vendor-overlay/*.apk $ROOT/vendor/overlay/ || die "could not copy the vendor overlays"
        $BB chmod 755 $ROOT/vendor $ROOT/vendor/overlay
        $BB chmod 644 $ROOT/vendor/overlay/*.apk
        echo "vendor overlays: $($BB ls $ROOT/vendor/overlay | $BB tr '\n' ' ')"
    fi
    # Optional, off unless /tmp/libnvram-btshim.so was pushed: lets the container
    # pick the Bluetooth BD address. libbluetooth_mtk.so imports only
    # NVM_GetFileDesc / NVM_CloseFileDesc from libnvram.so; the shim serves its
    # 64-byte record from /data/nw/bt_nvram.bin (below) instead of the NVRAM
    # tree and partition. Nothing else in the container loads libnvram.so (the
    # stock audio HAL that did is replaced). Built by
    # probe/android/connectivity/bt/nvram-shim/build.sh.
    if [ -f /tmp/libnvram-btshim.so ]; then
        nv="$ROOT/system/lib/libnvram.so"
        [ -e "$nv" ] || die "no libnvram.so on the system image to shadow"
        $BB chmod 644 /tmp/libnvram-btshim.so
        $BB grep -q " $nv " /proc/mounts || \
            $BB mount -o bind /tmp/libnvram-btshim.so "$nv" || die "libnvram shim bind failed"
        $BB mount -o remount,bind,ro "$nv" || die "libnvram shim remount ro failed"
        echo "libnvram shim: $nv <- /tmp/libnvram-btshim.so (ro)"
    fi
    # A2DP-only Bluetooth: the image's Bluetooth.apk with the other profile bools
    # set false and re-signed with the same AOSP test platform key, so the
    # platform signature and the android.uid.bluetooth shared user still match.
    # Built by probe/android/connectivity/bt/a2dp-only/build.sh.
    if [ -f /tmp/bt-apk/Bluetooth.apk ]; then
        bt="$ROOT/system/app/Bluetooth/Bluetooth.apk"
        [ -e "$bt" ] || die "no Bluetooth.apk on the system image to shadow"
        $BB chmod 644 /tmp/bt-apk/Bluetooth.apk
        $BB grep -q " $bt " /proc/mounts || \
            $BB mount -o bind /tmp/bt-apk/Bluetooth.apk "$bt" || die "Bluetooth.apk bind failed"
        $BB mount -o remount,bind,ro "$bt" || die "Bluetooth.apk remount ro failed"
        echo "Bluetooth: $bt <- /tmp/bt-apk/Bluetooth.apk (A2DP only, ro)"
    fi
    $BB mkdir -p $VOL
    $BB mount -o loop,rw -t ext4 "$C/$DATAIMG" $VOL || die "data loop mount failed"
    $BB mkdir -p $VOL/data $VOL/cache
    # BD-address seed for the libnvram shim: gen-bt-nvram.py -> /tmp/bt_nvram.bin.
    # The directory is world-writable so the bluetooth uid can leave
    # bt_nvram.written (the address the stack settled on) next to it.
    if [ -f /tmp/bt_nvram.bin ]; then
        $BB mkdir -p $VOL/data/nw
        $BB chmod 777 $VOL/data/nw
        $BB cp /tmp/bt_nvram.bin $VOL/data/nw/bt_nvram.bin
        $BB chmod 644 $VOL/data/nw/bt_nvram.bin
        echo "bt seed: $VOL/data/nw/bt_nvram.bin ($($BB wc -c < /tmp/bt_nvram.bin) bytes)"
    fi
    $BB mount -o bind $VOL/data $ROOT/data || die "data bind failed"
    $BB mount -o bind $VOL/cache $ROOT/cache || die "cache bind failed"
    $BB df -h $ROOT/system $ROOT/data | $BB tail -2
}

zram()
{
    say "zram swap"
    if $BB grep -q zram /proc/swaps 2>/dev/null; then
        echo "already on"
        return
    fi
    echo 268435456 > /sys/block/zram0/disksize
    $BB mkswap /dev/block/zram0 > /dev/null || die "mkswap failed"
    $BB swapon /dev/block/zram0 || die "swapon failed"
    $BB free | $BB tail -2
}

# The container gets its own mount namespace with $ROOT mounted over /. Amazon's
# zygote aborts on any inherited descriptor whose path is not on its whitelist,
# and the property area only resolves as /dev/__properties__ when the container
# root is the namespace root. pivotinit also closes the descriptors adbd hands
# down (Sony init's unlinked property area). See probe/devtools/pivotinit.c.
start_init()
{
    # Opt-in experiment (host: NW_DROP_SYS_BOOT=1): hand init a container with
    # no CAP_SYS_BOOT in its bounding set, so nothing inside can reboot the
    # shared kernel. pivotinit reads this from the environment.
    if [ -f /tmp/drop-sys-boot ]; then
        PIVOTINIT_DROP_SYS_BOOT=1
        export PIVOTINIT_DROP_SYS_BOOT
        echo "CAP_SYS_BOOT will be dropped from the container's bounding set"
    fi
    $BB setsid /tmp/pivotinit $ROOT /init
}

# Synthetic load for the reset bisect. Every stage carries the same load so the
# only difference between stages is what the container setup adds: two busy
# loops (one per core) plus a loop streaming the whole 1.2 GB system image, so
# the page cache really fills and has to keep evicting. An earlier version read
# only the first 64 MB with no skip, which is all cache hits after the first
# pass and proved nothing about cache growth or I/O pressure.
load_start()
{
    $BB grep -q " $C " /proc/mounts || die "run a stage that mounts /contents first"
    # The redirects matter: a background child that inherits the adb shell's
    # stdout keeps that pipe open, so "adb shell sh android-boot.sh bisect N"
    # never returns and the host-side sampler never starts. cachecap_start
    # already did this; load_start did not.
    for i in 1 2; do
        $BB setsid $BB sh -c 'while :; do :; done' </dev/null >/dev/null 2>&1 &
    done
    $BB setsid $BB sh -c 'while :; do dd if='"$C/$SYSIMG"' of=/dev/null bs=1M 2>/dev/null; done' </dev/null >/dev/null 2>&1 &
    $BB sleep 1
    echo "load: $($BB ps | $BB grep -c '[w]hile :') busy loops + image reader"
}

load_stop()
{
    for p in $($BB ps | $BB grep -E '[w]hile :|[d]d if=' | $BB awk '{print $1}'); do
        kill -9 "$p" 2>/dev/null
    done
    echo "load stopped"
}

# A/B aid for the cache-growth hypothesis: drop the page cache every five
# seconds so it cannot grow. A kernel knob, non-persistent, nothing written.
cachecap_start()
{
    $BB setsid $BB sh -c 'while :; do echo 3 > /proc/sys/vm/drop_caches; sleep 5; done' > /dev/null 2>&1 &
    echo "cache cap: drop_caches every 5s"
}

cachecap_stop()
{
    for p in $($BB ps | $BB grep -E '[d]rop_caches|[s]leep 5' | $BB awk '{print $1}'); do
        kill -9 "$p" 2>/dev/null
    done
    echo "cache cap stopped"
}

# Reset bisect: name the cause of the reset without any container logic.
#   1  prepare only            (Sony stack stopped, stock Mali driver)
#   2  prepare + Mali swap
#   3  prepare + Mali swap + zram
# Each stage then runs the same synthetic load for the caller's window.
bisect()
{
    load_stop > /dev/null
    case "$1" in
        1) prepare ;;
        2) prepare; swap ;;
        3) prepare; zram; swap ;;
        *) die "usage: android-boot.sh bisect 1|2|3" ;;
    esac
    load_start
}

up()
{
    prepare
    stage
    bt_host_release
    guard
    zram
    swap
    wifi_modules || echo "Wi-Fi: modules not loaded, continuing without Wi-Fi"
    container
    say "start Android init"
    start_init > $ROOT/init.out 2>&1 &
    $BB sleep 12
    status
    log
    if [ -n "$(container_init_pid)" ]; then
        btdev
        # Ignored before the fork: adb's shell exits as soon as up returns,
        # and its hangup reached the child before setsid had run. This ash
        # takes the signal by number only ("trap: HUP: bad trap").
        trap '' 1
        $BB setsid sh /tmp/android-boot.sh powerwatch < /dev/null > /tmp/powerwatch.log 2>&1 &
        echo $DOWNLOCK > /sys/power/wake_unlock 2>/dev/null
    fi
}

# The picker's Android path. The bootswitcher.sh wrapper (picker-upg) starts
# this in the background and returns without setting sys.sony.bootmode, so no
# Sony service ever runs. Stock init has started load_sony_driver (wm_key, the
# DAC tables) before the hook and execs optproc_for_boot.sh (peripheral
# firmware) once the hook returns; both are waited for. Walkman One loads the
# same drivers in boot_complete.sh, which init runs to its end before the hook,
# and has no optproc_for_boot.sh: there the wait for a load_sony_driver service
# that never exists took the full minute on every start.
#
# The picker's default is Walkman until Android reports sys.boot_completed, so
# a start that hangs, loops or takes the kernel down is not taken again by
# itself; the owner can still choose Android with the keys. If the container
# does not come up at all, the device reboots into Walkman: by then the stock
# Mali driver may be unbound, so starting the Sony stack in place is no
# fallback.
CHOICE=/contents/wmmod-android/boot-choice

boot()
{
    echo 1 > $CHOICE
    $BB sync
    if $BB grep -q '^[[:space:]]*exec .*optproc_for_boot' /init.rc; then
        for i in $($BB seq 60); do
            [ "$(getprop init.svc.load_sony_driver)" != running ] && \
                ! $BB ps | $BB grep -qE '[b]ootswitcher[.]sh|[o]ptproc_for_boot' && break
            $BB sleep 1
        done
    fi
    echo "<5>android-boot: picker chose Android, starting it" > /dev/kmsg
    (up)
    if [ -z "$(container_init_pid)" ]; then
        echo "<3>android-boot: Android did not start; rebooting into Walkman" > /dev/kmsg
        /system/bin/reboot
        return
    fi
    while [ -n "$(container_init_pid)" ]; do
        if [ "$(sh_in 'getprop sys.boot_completed')" = 1 ]; then
            echo 2 > $CHOICE
            $BB sync
            say "boot completed; Android is the picker's default again"
            return
        fi
        $BB sleep 10
    done
}

# init.aosp.rc writes sys.powerctl here when Android shuts down or reboots.
# Polled: busybox on the host has no inotifyd, and a sleep does not keep the
# device from suspending. A reboot reason (recovery, bootloader) is dropped,
# since on the Sony host those name the firmware's own boot modes.
#
# powerwatch also does what Sony's USB services did after every resume. This
# kernel's musb_bus_resume (USB_SW_WITCH_MODE) leaves the port in host mode
# with VBUS on: the PC link is gone, a replug does not bring it back
# (mt_usb_connect returns while is_host), and the charger stops charging.
# Sony's service writes mt_usb/mode back: 0 (to idle), then 1 (device mode,
# connected if a cable is in).
#
# It has to happen before the next suspend attempt. That attempt's
# musb_bus_suspend switches the port back to device mode itself, the
# otg_state uevent lands on healthd's EPOLLWAKEUP socket (wakeup source
# "NETLINK"), the attempt aborts with -EBUSY, the abort resumes the bus into
# host mode again, and so on: a loop that kept a resumed device awake for good
# while nothing reset the port. The container's healthd is patched to watch
# uevents without EPOLLWAKEUP (tools/android-container.sh), which breaks the
# loop. As PowerService did, powerwatch still cancels the icx_pm_helper resume
# wakelock only after a resume has been seen with the port in device mode.
# The poll holds no lock, so a suspend can fall anywhere inside it, and
# resume_count also counts the aborted attempts, which take no lock. The count
# is read again after the port check: if it moved, a cancel would hit the
# lock of a resume this poll has not looked at.
#
# While a USB host has the gadget configured, a wakelock keeps the device out
# of suspend, as on a phone: adb and the storage stay up. On a cable the
# charger's wakeup source does that anyway until the battery is full.
#
# Twice the PC link died with the cable in and never came back (10-19 22:49,
# 10-21 20:25), and both ended in a forced power-off, a cold boot with no
# last_kmsg. So every gadget state change goes to $USBLOG on /contents, synced:
# the controller's role and VBUS, otg_state, the charger's view and the
# kernel's last USB lines. When the charger still sees a PC (usb online) but
# the gadget has not been configured for 20 polls (about 15 s), the port gets
# what Sony's service gives it after a resume, mode 0 then 1, up to three
# times per loss.
POWERCTL=$ROOT/data/misc/powerctl
OTGSTATE=/sys/class/switch/otg_state/state
MTMODE=/sys/devices/platform/mt_usb/mode
MUSB=/sys/devices/platform/mt_usb/musb-hdrc.0
USBSTATE=/sys/class/android_usb/android0/state
USBONLINE=/sys/class/power_supply/usb/online
USBLOCK=nw_usb_host
DOWNLOCK=nw_down
USBLOG=$C/usb-log.txt

usblog()
{
    {
        echo "$($BB date '+%F %T') boot=$($BB cut -d' ' -f1 /proc/uptime) $1:" \
            "usb=$usb role=$($BB cat $MUSB/mode) $($BB cat $MUSB/vbus) otg=$otg pc=$pc" \
            "$($BB grep '^POWER_SUPPLY_STATUS=' /sys/class/power_supply/battery/uevent)"
        $BB dmesg | $BB grep -iE 'musb|usb|vbus|otg|charg' | $BB tail -25 | $BB sed 's/^/    /'
    } >> $USBLOG
    $BB sync
}

powerwatch()
{
    pid="$(container_init_pid)"
    [ -n "$pid" ] || die "the container is not up"
    $BB rm -f $POWERCTL
    held=0
    seen=
    lastusb=
    dead=0
    resets=0
    while kill -0 "$pid" 2>/dev/null; do
        # read, not cat: no fork per poll.
        rc=; read rc < $PMH/resume_count
        otg=0; read otg < $OTGSTATE
        if [ "$otg" = 1 ]; then
            echo 0 > $MTMODE
            echo 1 > $MTMODE
            echo "<5>android-boot: usb port back to device mode" > /dev/kmsg
        fi
        now=; read now < $PMH/resume_count
        if [ "$now" = "$rc" ] && [ "$rc" != "$seen" ]; then
            echo 1 > $PMH/resume_lock_cancel
            seen=$rc
        fi
        usb=; read usb < $USBSTATE
        case "$usb" in
            CONFIGURED) want=1 ;;
            *) want=0 ;;
        esac
        if [ $want != $held ]; then
            if [ $want = 1 ]; then echo $USBLOCK > /sys/power/wake_lock
            else echo $USBLOCK > /sys/power/wake_unlock; fi
            held=$want
        fi
        pc=0; read pc < $USBONLINE
        if [ "$usb" != "$lastusb" ]; then
            usblog "${lastusb:-start} -> $usb"
            lastusb=$usb
        fi
        if [ $want = 1 ] || [ "$pc" != 1 ]; then
            dead=0
            [ $want = 1 ] && resets=0
        else
            dead=$((dead + 1))
            if [ $dead -ge 20 ] && [ $resets -lt 3 ]; then
                echo 0 > $MTMODE
                echo 1 > $MTMODE
                resets=$((resets + 1))
                dead=0
                echo "<5>android-boot: usb link reset $resets" > /dev/kmsg
                usblog "reset $resets"
            fi
        fi
        if [ -s $POWERCTL ]; then
            req="$($BB cat $POWERCTL)"
            echo "<5>android-boot: sys.powerctl=$req" > /dev/kmsg
            (down)
            $BB sync
            case "$req" in
                reboot*) /system/bin/reboot ;;
                *) /system/bin/reboot -p ;;
            esac
            return
        fi
        $BB sleep 0.5
    done
    [ $held = 1 ] && echo $USBLOCK > /sys/power/wake_unlock
}

status()
{
    say "container processes"
    for p in $(container_pids); do
        echo "$p $($BB cat $p/comm 2>/dev/null)"
    done
    say "devices in the container /dev"
    $BB ls $ROOT/dev 2>/dev/null | $BB tr '\n' ' '
    echo
}

log()
{
    say "kernel log (container)"
    dmesg | $BB grep -E 'init:|logd|servicemanager|surfaceflinger|zygote|ueventd|audit' | $BB tail -40
}

sh_in()
{
    pid="$(container_init_pid)"
    [ -n "$pid" ] || die "the container is not up"
    ANDROID_ROOT=/system ANDROID_DATA=/data PATH=/system/bin:/system/xbin \
        /tmp/pivotinit --join "$pid" /system/bin/sh -c "$1"
}

# Inside the container: is /dev/stpbt there, and who owns it? ueventd's coldboot
# makes it from /sys/class/stpbt/stpbt (class device of mtk_stp_bt_soc, major
# 192) under the "bluetooth radio 0660" rule in ueventd.icx.rc, the same way it
# makes /dev/snd/*. A bind from the host cannot be used: Android init mounts a
# fresh tmpfs on the container's /dev after container() returns, which would
# cover it. So report, and mknod only if ueventd did not.
btdev()
{
    say "/dev/stpbt in the container"
    sh_in '
        if [ ! -c /dev/stpbt ]; then
            devno="$(cat /sys/class/stpbt/stpbt/dev 2>/dev/null)"
            [ -n "$devno" ] || devno=192:0
            echo "ueventd did not create /dev/stpbt; mknod c ${devno%%:*} ${devno##*:}"
            /system/xbin/mknod /dev/stpbt c ${devno%%:*} ${devno##*:}
            /system/xbin/chown 1002:1001 /dev/stpbt
            /system/xbin/chmod 0660 /dev/stpbt
        fi
        ls -l /dev/stpbt
    '
}

down()
{
    say "stop the container"
    # Once init is gone powerwatch lets go of the USB lock, and after a picker
    # boot no Sony service holds one either: the device suspends, and without
    # powerwatch to reset the port the PC link does not come back (10-22).
    # Held until the next up has powerwatch running, or a reboot.
    echo $DOWNLOCK > /sys/power/wake_lock
    # Killed, the card's FUSE daemon would leave the exFAT volume marked dirty.
    if [ -n "$(container_init_pid)" ]; then
        sh_in 'vdc volume unmount /storage/sdcard1 force' 2>&1 | $BB tail -1
    fi
    for p in $(container_pids); do kill -9 "$p" 2>/dev/null; done
    $BB sleep 1
    for p in $(container_pids); do kill -9 "$p" 2>/dev/null; done
    # The container root is bind mounted onto itself and init stacks its own
    # mounts on top, so peel the stack rather than assuming one level. The
    # masked audio HAL, the software keymaster, the ui sound bind, the idc and keylayout overlays, the permissions
    # overlay, the libnvram shim and the A2DP-only Bluetooth.apk sit on top of
    # $ROOT/system and have to come
    # off before the system loop mount can be released. The ui bind is first so
    # it is gone before $ROOT/system.
    for i in 1 2 3 4 5; do
        $BB umount "$ROOT/system/media/audio/ui" 2>/dev/null
        $BB umount "$ROOT/system/lib/hw/audio.primary.mt8127.so" 2>/dev/null
        $BB umount "$ROOT/system/lib/hw/keystore.mt8127.so" 2>/dev/null
        $BB umount "$ROOT/system/lib/libnvram.so" 2>/dev/null
        $BB umount "$ROOT/system/app/Bluetooth/Bluetooth.apk" 2>/dev/null
        $BB umount "$ROOT/system/usr/idc" 2>/dev/null
        $BB umount "$ROOT/system/usr/keylayout" 2>/dev/null
        $BB umount "$ROOT/system/etc/permissions" 2>/dev/null
        $BB umount $ROOT/dev/pts $ROOT/dev $ROOT/proc $ROOT/sys \
            $ROOT/data $ROOT/cache $ROOT/system $ROOT $VOL 2>/dev/null
    done
    left="$(container_pids)"
    [ -z "$left" ] || echo "!! still alive: $left"
    if $BB grep -q " $ROOT" /proc/mounts; then
        echo "!! mounts still under $ROOT:"
        $BB grep " $ROOT" /proc/mounts
    fi
    # Independent of umount's status, which is discarded above.
    ui_dst="$ROOT/system/media/audio/ui"
    if $BB grep -q " $ui_dst " /proc/mounts; then
        echo "!! ui sound bind still mounted:"
        $BB grep " $ui_dst " /proc/mounts
        exit 1
    fi
    echo "ui sound bind: gone"
    echo "container stopped; the Mali driver swap still needs a reboot"
}

case "$1" in
prepare) prepare ;;
bisect) shift; bisect "$@" ;;
loadstop) load_stop ;;
cachecap) cachecap_start ;;
cachecapstop) cachecap_stop ;;
up) up ;;
boot) boot ;;
status) status ;;
log) log ;;
wifi) wifi_status ;;
wifi-unload) wifi_unload ;;
down) down ;;
powerwatch) powerwatch ;;
btcheck) btcheck ;;
btdev) btdev ;;
sh) shift; sh_in "$*" ;;
*) die "usage: android-boot.sh prepare | up | boot | status | sh CMD | log | wifi | wifi-unload | down | btcheck | btdev" ;;
esac
