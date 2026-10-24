#!/bin/bash
#
# Build the A2DP-only Bluetooth overlay into work/android/overlay/BluetoothA2dpOnly.apk.
# Superseded: CM 12.1 never applies an app-targeted static overlay (device run
# 2026-10-15), so ../a2dp-only/build.sh patches Bluetooth.apk itself instead.
# tools/android-boot.sh up pushes it and android-boot.sh places it in the container's
# /vendor/overlay. Android 5.1 verifies only v1 (JAR) signatures; /vendor/overlay packages
# are trusted overlays, so any key works.
#
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../../../.." && pwd)"
BT="${ANDROID_BUILD_TOOLS:-$HOME/Android/sdk/build-tools/28.0.3}"
JAR="${ANDROID_JAR:-$HOME/Android/sdk/platforms/android-28/android.jar}"
KS="${OVERLAY_KEYSTORE:-$HOME/.android/debug.keystore}"
OUT="$ROOT/work/android/overlay"
APK="$OUT/BluetoothA2dpOnly.apk"

mkdir -p "$OUT"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

"$BT/aapt" package -f -M "$HERE/AndroidManifest.xml" -S "$HERE/res" -I "$JAR" \
    --min-sdk-version 22 --target-sdk-version 22 -F "$tmp/unsigned.apk"
"$BT/zipalign" -f 4 "$tmp/unsigned.apk" "$tmp/aligned.apk"
"$BT/apksigner" sign --ks "$KS" --ks-pass pass:android --key-pass pass:android \
    --v1-signing-enabled true --v2-signing-enabled false --min-sdk-version 22 \
    --out "$APK" "$tmp/aligned.apk"
"$BT/apksigner" verify --min-sdk-version 22 "$APK"
echo "built $APK ($(stat -c %s "$APK") bytes)"
