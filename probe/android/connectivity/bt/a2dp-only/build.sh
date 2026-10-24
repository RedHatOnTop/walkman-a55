#!/bin/bash
#
# Build the A2DP-only Bluetooth.apk into work/android/bt/Bluetooth.apk from the
# user's own CM image. tools/android-boot.sh up pushes it and android-boot.sh
# binds it over the container's /system/app/Bluetooth/Bluetooth.apk.
#
# A static overlay cannot do this on CM 12.1: its PackageManagerService writes
# the idmap into the theme cache and never sets the target's resourceDirs, and
# ResourcesManager attaches overlays only for the applied theme. So the profile
# bools are flipped in the APK itself. The CM build is signed with the public
# AOSP test platform key, and re-signing with that same key keeps the platform
# signature and the android.uid.bluetooth shared user intact. The script
# refuses an image whose Bluetooth.apk carries any other certificate.
#
#   NW_SYS_IMG         system image to read (default: the ford image)
#   NW_PLATFORM_KEYS   directory with the AOSP test platform.pk8 / platform.x509.pem
#
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../../../.." && pwd)"
BT="${ANDROID_BUILD_TOOLS:-$HOME/Android/sdk/build-tools/28.0.3}"
IMG="${NW_SYS_IMG:-$ROOT/work/android/aosp-ford/system.img}"
KEYS="${NW_PLATFORM_KEYS:-$HOME/Projects/distribution-project/walkman-zx507/tools/aosp-testkeys}"
OUT="$ROOT/work/android/bt"
APK="$OUT/Bluetooth.apk"
# Same profiles the abandoned overlay turned off: everything a headset or phone
# could use except A2DP (+ AVRCP, which lives inside A2dpService) and GATT.
OFF=(profile_supported_hs_hfp profile_supported_pbap profile_supported_map
     profile_supported_opp profile_supported_pan profile_supported_hid
     profile_supported_hidd profile_supported_hdp)

[ -f "$IMG" ] || { echo "missing $IMG" >&2; exit 1; }
[ -f "$KEYS/platform.pk8" ] && [ -f "$KEYS/platform.x509.pem" ] || { echo "no platform key in $KEYS" >&2; exit 1; }
mkdir -p "$OUT"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

cert_sha1() { "$BT/apksigner" verify --print-certs "$1" | awk '/certificate SHA-1 digest/ {print $NF}'; }

debugfs -R "dump /app/Bluetooth/Bluetooth.apk $tmp/stock.apk" "$IMG" 2>/dev/null
[ -s "$tmp/stock.apk" ] || { echo "no /app/Bluetooth/Bluetooth.apk in $IMG" >&2; exit 1; }
want="$(cert_sha1 "$tmp/stock.apk")"
key="$(openssl x509 -in "$KEYS/platform.x509.pem" -noout -fingerprint -sha1 | sed 's/.*=//; s/://g' | tr 'A-F' 'a-f')"
[ -n "$want" ] && [ "$want" = "$key" ] || { echo "stock Bluetooth.apk cert $want is not the key in $KEYS ($key)" >&2; exit 1; }

python3 -I "$HERE/flip_bools.py" "$tmp/stock.apk" "$tmp/unsigned.apk" "${OFF[@]}"
"$BT/zipalign" -f 4 "$tmp/unsigned.apk" "$tmp/aligned.apk"
"$BT/apksigner" sign --key "$KEYS/platform.pk8" --cert "$KEYS/platform.x509.pem" \
    --v1-signing-enabled true --v2-signing-enabled false --min-sdk-version 22 \
    --out "$APK" "$tmp/aligned.apk"
got="$(cert_sha1 "$APK")"
[ "$got" = "$want" ] || { echo "signed with $got, want $want" >&2; exit 1; }
"$BT/aapt" dump resources "$APK" | awk '/^ +resource 0x.*:bool\/profile_supported_.* t=0x12 / {sub(/.*:bool\//, "", $3); print $3, $5}'
echo "built $APK ($(stat -c %s "$APK") bytes, cert $got)"
