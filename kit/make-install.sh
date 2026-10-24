#!/usr/bin/env bash
# Everything the NW-A50 series Android install takes, from the owner's own files: the backup
# backup-upg/ wrote to their player (WMBACKUP), LineageOS 12.1 for the Fire 7 (ford) and
# Amazon's Fire OS 5.3.6.4 update for the Fire 7. The hand steps of tools/android-boot.sh
# install and picker-upg in one run, and the entry point walkman-manager's Firmware Studio
# drives.
#
# <out-dir> gets what goes onto the player's internal storage, under the same paths:
#   wmmod-android/boot/              the boot set: android-boot.sh, the container, the modules
#                                    and the input overlays with their MANIFEST, and the picker
#   wmmod-android/fire-system.img    the container's /system (build_image.py)
#   wmmod-android/android-data.img   an empty /data
#   NW_WM_FW.UPG                     the picker hook, with the key the player's updater takes
# and beside them
#   manifest.tsv   image-manifest.py of fire-system.img
#   install.json   what is copied in which order, the hashes, the UPG and what it logs
#
# Our own binaries, the browser among them, come from this repository's prebuilt release,
# pinned by kit/prebuilt.sha256; KIT_PREBUILT=<dir> takes a local copy of it, and
# KIT_FROM_SOURCE=1 builds them with make-prebuilt.sh instead.
#
# Progress: "== total <n>" first, then one "== <i> <step>" line as each step starts. Every
# other line is a step's own output.
#
# Usage: [KIT_PREBUILT=<dir> | KIT_FROM_SOURCE=1] \
#            kit/make-install.sh <WMBACKUP> <lineage-ford.zip> <update-ford-5.3.6.4.bin> <out-dir>
# <WMBACKUP> may also name the backup's manifest.txt, as walkman-manager's vault keeps it, and
# <lineage-ford.zip> a folder with the zip's system.transfer.list and system.new.dat.

set -euo pipefail
# Glob and sort order decide the boot set's MANIFEST; one locale, same bytes.
export LC_ALL=C.UTF-8 TZ=UTC
BACKUP="$1"
ROM="$2"
FIREOS="$3"
OUT="$4"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PY=(env -u PYTHONHOME -u PYTHONPATH /usr/bin/python3 -I)
PREBUILT_URL=https://github.com/RedHatOnTop/walkman-a55/releases/download/prebuilt-3
# The data image's UUID and hash seed, fixed so the same kit gives the same bytes.
DATA_UUID=a0319e29-5878-5c80-b9a3-75e964da80c4

[ ! -f "$BACKUP" ] || BACKUP="$(dirname "$BACKUP")"
for f in "$BACKUP/android.img" "$BACKUP/option2.img" "$BACKUP/recovery.img" "$FIREOS"; do
	[ -f "$f" ] || { echo "no $f" >&2; exit 1; }
done
[ -e "$ROM" ] || { echo "no $ROM" >&2; exit 1; }
[ ! -e "$OUT" ] || [ -z "$(ls -A "$OUT")" ] || { echo "$OUT is not empty" >&2; exit 1; }
mkdir -p "$OUT/wmmod-android"
OUT="$(cd "$OUT" && pwd)"
WORK="$OUT/.work"
SET="$OUT/wmmod-android/boot"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK"

image_steps="$("${PY[@]}" "$ROOT/kit/build_image.py" --count)"
total=$((6 + image_steps + 7))
echo "== total $total"
i=0
top() {
	i=$((i + 1))
	echo "== $i $1"
}

top prebuilt
P="$WORK/prebuilt"
if [ -n "${KIT_FROM_SOURCE:-}" ]; then
	"$ROOT/kit/make-prebuilt.sh" "$P"
else
	mkdir -p "$P"
	while read -r _ name; do
		if [ -n "${KIT_PREBUILT:-}" ]; then
			cp "$KIT_PREBUILT/$name" "$P/$name"
		else
			curl -sSfL -o "$P/$name" "$PREBUILT_URL/$name"
		fi
	done <"$ROOT/kit/prebuilt.sha256"
	(cd "$P" && sha256sum --quiet -c "$ROOT/kit/prebuilt.sha256")
	echo "$(wc -l <"$ROOT/kit/prebuilt.sha256") files"
fi

top sony
line="$("${PY[@]}" "$ROOT/kit/sony.py" "$BACKUP" "$WORK/sony")"
echo "$line"
FIRMWARE="${line%%:*}"
FIRMWARE="${FIRMWARE#sony-}"
KEY="$("${PY[@]}" "$ROOT/kit/upg_key.py" "$BACKUP")"
echo "the player's updater takes the $KEY key"

top walkmanui
"${PY[@]}" "$ROOT/android-ui/stage.py" --sony "$WORK/sony" --out "$WORK/walkmanui-res"

top bootanimation
"${PY[@]}" "$ROOT/probe/skin/bootanimation/gen_bootanim.py" \
	"$WORK/sony/system/media/bootanimation.zip" "$WORK/bootanimation.zip"

top wallpaper
"${PY[@]}" "$ROOT/probe/wallpaper/gen_hires.py" "$WORK/wallpaper.jpg" \
	--fonts "$WORK/sony/system/vendor/sony/lib/fonts"

top rom
mkdir -p "$WORK/rom"
if [ -d "$ROM" ]; then
	cp --reflink=auto "$ROM/system.transfer.list" "$ROM/system.new.dat" "$WORK/rom/"
else
	unzip -q -o "$ROM" system.transfer.list system.new.dat -d "$WORK/rom"
fi
(cd "$WORK/rom" && sha256sum -c "$ROOT/kit/ford.sha256")

# build_image.py numbers its own steps from 1; they follow on here.
"${PY[@]}" "$ROOT/kit/build_image.py" "$OUT/wmmod-android/fire-system.img" \
	--sdat "$WORK/rom" --walkmanui "$P/WalkmanUI.apk" --walkmanui-res "$WORK/walkmanui-res" \
	--walkmanui-logo "$WORK/sony/qrc/sys_shutdown_logo.png" \
	--browser "$P/Browser.apk" --audio-hal "$P/audio.primary.mt8127.so" \
	--soundfx "$P/libwalkmanfx.so" --wallpaper "$WORK/wallpaper.jpg" \
	--bootanimation "$WORK/bootanimation.zip" |
	while IFS= read -r line; do
		if [[ "$line" =~ ^==\ ([0-9]+)\ (.*)$ ]]; then
			echo "== $((i + BASH_REMATCH[1])) ${BASH_REMATCH[2]}"
		else
			printf '%s\n' "$line"
		fi
	done
i=$((i + image_steps))

top container
"${PY[@]}" "$ROOT/kit/fire_ramdisk.py" "$FIREOS" "$WORK/fire-ramdisk"
NW_CONTAINER_VARIANT=aosp NW_FIRE_RAMDISK="$WORK/fire-ramdisk" NW_CONTAINER_TGZ="$WORK/container.tgz" \
	"$ROOT/tools/android-container.sh"

top picker
"${PY[@]}" "$ROOT/tools/bootpick_frames.py" "$WORK/frames.raw" --sony "$WORK/sony"

# The set tools/android-boot.sh install pushes over adb, Wi-Fi included. The picker's own
# files are not in the MANIFEST: android-boot.sh never stages them.
top set
: >"$WORK/MANIFEST"
add() {
	install -D -m 0644 "$1" "$SET/$2"
	echo "$(md5sum <"$1" | cut -d' ' -f1)  $2" >>"$WORK/MANIFEST"
}
add "$ROOT/probe/android/android-boot.sh" android-boot.sh
add "$P/pivotinit" pivotinit
add "$WORK/container.tgz" container.tgz
add "$P/mali_r4p0.ko" mali_r4p0.ko
add "$P/swap_accounting_guard.ko" swap_accounting_guard.ko
add "$P/mtk_wmt_wifi_soc.ko" wifi/mtk_wmt_wifi_soc.ko
for ko in compat_bp cfg80211_bp wlan_mt_bp; do
	add "$P/$ko.ko" "wifi/$ko.ko"
done
for f in "$ROOT"/probe/android/idc/*.idc; do
	add "$f" "idc-overlay/${f##*/}"
done
for f in "$ROOT"/probe/android/keylayout/*.kl; do
	add "$f" "kl-overlay/${f##*/}"
done
# The container's init runs without CAP_SYS_BOOT, as in every ford bring-up since 10-14:
# a power-off or reboot from Android reaches the device through sys.powerctl, which
# android-boot.sh's powerwatch carries out, and never resets the shared kernel itself.
: >"$WORK/drop-sys-boot"
add "$WORK/drop-sys-boot" drop-sys-boot
install -D -m 0644 "$P/bootpick" "$SET/picker/bootpick"
install -D -m 0644 "$WORK/frames.raw" "$SET/picker/frames.raw"
cp "$WORK/MANIFEST" "$SET/MANIFEST"
echo "$(wc -l <"$SET/MANIFEST") files in the MANIFEST"

# 2000M: the first boot's dexopt filled 256M; adbd refuses a push of 2 GiB or more
# (tools/android-boot.sh up).
top data
truncate -s 2000M "$WORK/android-data.img"
if ! E2FSPROGS_FAKE_TIME=1 mke2fs -q -t ext4 -F -L android-data -U "$DATA_UUID" -E hash_seed="$DATA_UUID" \
	-O ^metadata_csum,^64bit,^orphan_file "$WORK/android-data.img" 2>/dev/null; then
	E2FSPROGS_FAKE_TIME=1 mke2fs -q -t ext4 -F -L android-data -U "$DATA_UUID" -E hash_seed="$DATA_UUID" \
		-O ^metadata_csum,^64bit "$WORK/android-data.img"
fi
mv "$WORK/android-data.img" "$OUT/wmmod-android/android-data.img"

top upg
mkdir -p "$WORK/picker-upg"
cp "$ROOT/picker-upg/update.sh" "$WORK/picker-upg/"
"$ROOT/tools/build_upg.sh" "$WORK/picker-upg"
case "$KEY" in
w1) cp "$WORK/picker-upg/NW_WM_FW.UPG" "$OUT/NW_WM_FW.UPG" ;;
stock) cp "$WORK/picker-upg/stock/NW_WM_FW.UPG" "$OUT/NW_WM_FW.UPG" ;;
esac

top manifest
"${PY[@]}" "$ROOT/kit/image-manifest.py" "$OUT/wmmod-android/fire-system.img" >"$OUT/manifest.tsv"
wc -l <"$OUT/manifest.tsv"

top install
"${PY[@]}" "$ROOT/kit/install_json.py" "$OUT" "$BACKUP" "$KEY" "$FIRMWARE" >"$OUT/install.json"
cat "$OUT/install.json"
