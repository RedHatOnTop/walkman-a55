#!/bin/bash
#
# Build WalkmanUI for the host (checks under Xvfb) or for Android.
#
#   android-ui/build.sh host                   # build/host/walkmanui
#   android-ui/build.sh android [ABI ...]      # one APK, default armeabi-v7a x86_64
#                                              # (the NW-A55 and Waydroid), and the
#                                              # image's sound effects (audiofx/embed.py)
#   android-ui/build.sh release [ABI ...]      # the APK the install kit ships, default
#                                              # armeabi-v7a, without Sony's resources
#
# stage.py first packs the Sony resources from the gitignored work/ tree into
# build/res (and the APK assets); they are never committed. A release APK
# carries none: kit/build_image.py writes them, made from the owner's own
# firmware, to /system/etc/walkmanui/res.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
TARGET="${1:-host}"
shift || true

QT="$ROOT/work/Qt/5.15.2"
SDK="${ANDROID_SDK_ROOT:-$HOME/Android/sdk}"
NDK="$SDK/ndk/21.4.7075529"

case "$TARGET" in
host)
    python3 -I "$HERE/stage.py"
    BUILD="$HERE/build/host"; mkdir -p "$BUILD"; cd "$BUILD"
    "$QT/gcc_64/bin/qmake" "$HERE/walkmanui.pro"
    make -j"$(nproc)"
    echo "BINARY=$BUILD/walkmanui"
    ;;
android|release)
    if [ "$TARGET" = release ]; then
        ABIS="${*:-armeabi-v7a}"
        python3 -I "$HERE/stage.py" --android --release
        # Its own tree: androiddeployqt never removes the assets an earlier
        # development build left in apk-root.
        BUILD="$HERE/build/android-release"
    else
        ABIS="${*:-armeabi-v7a x86_64}"
        python3 -I "$HERE/stage.py" --android
        BUILD="$HERE/build/android"
    fi
    # Qt 5.15.2 generates gradle 5.6.4 + AGP 3.6.0 + build-tools 28.0.3, which
    # need JDK 11 and SDK platform 28, not the host's newer ones.
    export ANDROID_SDK_ROOT="$SDK" ANDROID_NDK_ROOT="$NDK"
    export JAVA_HOME="$ROOT/work/jdk11" PATH="$ROOT/work/jdk11/bin:$PATH"
    mkdir -p "$BUILD"; cd "$BUILD"
    "$QT/android/bin/qmake" "$HERE/walkmanui.pro" -spec android-clang CONFIG+=release "ANDROID_ABIS=$ABIS"
    make -j"$(nproc)"
    rm -rf "$BUILD/apk-root/libs"
    make INSTALL_ROOT="$BUILD/apk-root" install
    "$QT/android/bin/androiddeployqt" \
        --input "$BUILD/android-walkmanui-deployment-settings.json" \
        --output "$BUILD/apk-root" --android-platform android-28 --gradle
    APK="$BUILD/apk-root/build/outputs/apk/debug/apk-root-debug.apk"
    cp "$APK" "$BUILD/WalkmanUI.apk"
    if [ "$TARGET" = release ] && unzip -l "$BUILD/WalkmanUI.apk" | grep -q ' assets/res/'; then
        echo "$BUILD/WalkmanUI.apk carries Sony resources" >&2; exit 1
    fi
    echo "APK=$BUILD/WalkmanUI.apk"
    make -C "$HERE/audiofx" NDK="$NDK" test android
    echo "SOUNDFX=$HERE/build/audiofx/armeabi-v7a/libwalkmanfx.so"
    ;;
*)
    echo "usage: $0 host | android [abi ...] | release [abi ...]" >&2; exit 2 ;;
esac
