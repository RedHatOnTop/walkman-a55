#!/usr/bin/env bash
#
# Cross-compile the NW-A55 CXD3778GF primary audio HAL (audio_hw.c) into
# audio.primary.mt8127.so for the CM-container AudioFlinger.
#
# AudioFlinger loads the HAL by the name ro.board.platform dictates, which on
# this CM "ford" image is mt8127 -> audio.primary.mt8127.so. We build under that
# name and replace the stock MTK HAL (it links libnvram/libcustom_nvram, which
# reach forbidden partitions and cannot run here). Links against the image's own
# libtinyalsa.so (pulled read-only with debugfs, no mount) and the NDK's
# liblog/libc; libtinyalsa's own deps (libcutils/libutils/...) resolve on device.
#
# Output: probe/audiohal/out/audio.primary.mt8127.so (ARM EABI5, Android API 22).
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
ndk="${NDK:-$HOME/Android/sdk/ndk/21.4.7075529}"
img="${NW_SYS_IMG:-$root/work/android/aosp-ford/system.img}"
cc="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi22-clang"
strip="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip"
out="$here/out"

[ -x "$cc" ] || { echo "no NDK clang at $cc (set NDK=)" >&2; exit 1; }
[ -f "$img" ] || { echo "no system image at $img (set NW_SYS_IMG=)" >&2; exit 1; }

mkdir -p "$out"

# Link stub: the device's own libtinyalsa.so, extracted without mounting.
debugfs -R "dump /lib/libtinyalsa.so $out/libtinyalsa.so" "$img" 2>/dev/null || true
[ -s "$out/libtinyalsa.so" ] || { echo "could not pull /lib/libtinyalsa.so from $img" >&2; exit 1; }

"$cc" -shared -fPIC -O2 -g0 -Wall -Wextra \
    -Wno-gnu-designator -Wno-unused-function \
    -I "$here/aosp-include" \
    -o "$out/audio.primary.mt8127.so" "$here/audio_hw.c" \
    -L"$out" -ltinyalsa -llog \
    -Wl,--no-undefined -Wl,-soname,audio.primary.mt8127.so

# Drop debug/local symbols; keep the dynamic HMI export AudioFlinger dlsym()s.
"$strip" --strip-unneeded "$out/audio.primary.mt8127.so"

echo "built: $out/audio.primary.mt8127.so ($(stat -c%s "$out/audio.primary.mt8127.so") bytes)"
