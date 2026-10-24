#!/usr/bin/env bash
# Builds this repository's own binaries the install kit takes and collects them in <out-dir>
# with their SHA256SUMS. --pin also writes those to kit/prebuilt.sha256: that folder is then
# the repository's prebuilt release, which make-install.sh downloads or takes as a local copy
# (KIT_PREBUILT=<dir>). make-install.sh runs it without --pin for KIT_FROM_SOURCE=1.
#
# Each build needs what its script names: Qt 5.15.2, JDK 11 and NDK r21 under work/
# (android-ui/build.sh), the SDK's build-tools 35 and android-28 (android-browser/build.sh),
# the ford image as the audio HAL's link stub (probe/audiohal/build.sh), the crosstool-ng
# toolchain (probe/build.sh) and the Sony 3.10.26 kernel tree with arm-eabi-4.8 for the
# modules (probe/kmod/*/Makefile).
#
# Usage: kit/make-prebuilt.sh [--pin] <out-dir>

set -euo pipefail
export LC_ALL=C.UTF-8 TZ=UTC
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PIN=
[ "$1" != --pin ] || { PIN=1; shift; }
OUT="$1"
CROSS_COMPILE="${CROSS_COMPILE:-$HOME/.cache/arm-eabi-4.8/bin/arm-eabi-}"
# The audited build of the guard (tools/android-boot.sh GUARD_SHA256), stripped as below. Its
# debug info and build id hash the build directory; the code is the same from any checkout.
GUARD_SHA256=6080f7ce0b871dae63c7712a981c43950e0e6d158c2183280913236c5470dc18

[ ! -e "$OUT" ] || [ -z "$(ls -A "$OUT")" ] || { echo "$OUT is not empty" >&2; exit 1; }
mkdir -p "$OUT"

"$ROOT/android-ui/build.sh" release
"$ROOT/android-browser/build.sh" "$ROOT/work/browser/Browser.apk"
"$ROOT/probe/audiohal/build.sh"
"$ROOT/probe/build.sh" tools
make -C "$ROOT/probe/kmod/mali-r4p0"
make -C "$ROOT/probe/kmod/swap-accounting-guard"
make -C "$ROOT/probe/kmod/mtk-wmt-wifi-soc" verify
# Its own backports tree: the default one under work/ belongs to the development builds.
make -C "$ROOT/probe/kmod/wlan-mt" BUILD="$ROOT/work/kit-wlan-mt" verify

while read -r name from; do
	cp "$ROOT/$from" "$OUT/$name"
done <<EOF
WalkmanUI.apk android-ui/build/android-release/WalkmanUI.apk
Browser.apk work/browser/Browser.apk
libwalkmanfx.so android-ui/build/audiofx/armeabi-v7a/libwalkmanfx.so
audio.primary.mt8127.so probe/audiohal/out/audio.primary.mt8127.so
pivotinit probe/out/pivotinit
bootpick probe/out/bootpick
mali_r4p0.ko probe/kmod/mali-r4p0/mali_r4p0.ko
swap_accounting_guard.ko probe/kmod/swap-accounting-guard/swap_accounting_guard.ko
mtk_wmt_wifi_soc.ko probe/kmod/mtk-wmt-wifi-soc/mtk_wmt_wifi_soc.ko
compat_bp.ko work/kit-wlan-mt/compat_bp.ko
cfg80211_bp.ko work/kit-wlan-mt/cfg80211_bp.ko
wlan_mt_bp.ko work/kit-wlan-mt/wlan_mt_bp.ko
EOF

for ko in "$OUT"/*.ko; do
	"${CROSS_COMPILE}strip" --strip-debug --remove-section=.note.gnu.build-id "$ko"
done
echo "$GUARD_SHA256  $OUT/swap_accounting_guard.ko" | sha256sum --quiet -c ||
	{ echo "swap_accounting_guard.ko is not the audited build ($GUARD_SHA256)" >&2; exit 1; }
(cd "$OUT" && sha256sum -- *) >"$OUT/SHA256SUMS"
[ -z "$PIN" ] || cp "$OUT/SHA256SUMS" "$ROOT/kit/prebuilt.sha256"
cat "$OUT/SHA256SUMS"
