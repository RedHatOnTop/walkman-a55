#!/bin/bash
#
# Download the third-party APKs tools/cm_customize.py embeds into the ford
# image, each pinned to the build that booted on the A55, into
# work/android/aosp-ford/apps/ (gitignored). WalkmanUI.apk comes from
# android-ui/build.sh instead.
#
#   fetch_apps.sh [APPS_DIR]
#
# A file already there with the pinned sum is kept; a different one is an
# error, so a hand-placed build is never replaced silently.
#
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APPS="${1:-$ROOT/work/android/aosp-ford/apps}"

# name, sha256, url. Fenix 143.0.4 is the armeabi-v7a build from Mozilla's
# release archive (devlog 2026-10-14-cm-ford-customize.md).
APKS=(
    "Firefox.apk af560738627d4efc9ace75e708fc48aad6a24c78ef43a89daca27e16ff3829f8 https://archive.mozilla.org/pub/fenix/releases/143.0.4/android/fenix-143.0.4-android-armeabi-v7a/fenix-143.0.4.multi.android-armeabi-v7a.apk"
)

mkdir -p "$APPS"
for entry in "${APKS[@]}"; do
    read -r name sum url <<< "$entry"
    out="$APPS/$name"
    if [ -f "$out" ]; then
        have="$(sha256sum "$out" | cut -d' ' -f1)"
        if [ "$have" = "$sum" ]; then
            echo "$name: already there"
            continue
        fi
        echo "$out is another build (sha256 $have); move it away first" >&2
        exit 1
    fi
    echo "$name: $url"
    curl -fsSL --retry 2 -o "$out.part" "$url"
    have="$(sha256sum "$out.part" | cut -d' ' -f1)"
    if [ "$have" != "$sum" ]; then
        echo "$name: sha256 $have, want $sum" >&2
        exit 1
    fi
    mv "$out.part" "$out"
    echo "$name: ok"
done
