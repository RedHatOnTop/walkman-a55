#!/bin/bash
#
# Build the probe eglfs plugin (libqeglfs.so) for the NW-A50 series.
#
#   ./build.sh qt      pass 1: configure and build Sony's Qt 5.3.2 (qtbase and
#                      qtdeclarative) with the stock hooks; run once, slow
#   ./build.sh         pass 2: rebuild only the eglfs plugin with the probe
#   ./build.sh tools   build the device-side helpers in devtools/ into out/
#   ./build.sh hosttest   build the host harness for ARM against this Qt, for
#                      hosttest/run-arm.sh, into work/probe-hosttest-arm
#   ./build.sh shell CMD   run CMD in the build container (debugging)
#
# Runs inside nw-probe-builder:local (Dockerfile.builder), which adds python to
# the nw-installer crosstool image (gcc 4.9.4, glibc 2.23, hard-float) built
# from wampy/nw-installer/crosstool as nw-crosstool:local. Containers carry the
# label nw-a55=1: `docker container prune --filter label=nw-a55=1` clears them.
# The configure options follow unknown321/wampy's server/Makefile, which is
# known to produce a plugin the stock app loads.
#
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
IMAGE=nw-probe-builder:local
PLATFORM=armv5-unknown-linux-gnueabihf
SYSROOT=/x-tools/$PLATFORM/$PLATFORM/sysroot
SPEC=wmprobe-mt8590
QT_DIR="$ROOT/work/qt-src"
BUILD_DIR="$ROOT/work/qt-build"
OUT="$HERE/out"
JOBS=16

# The user's ~/.docker/config.json names a credential helper that is not
# installed; an empty config avoids it without touching theirs.
export DOCKER_CONFIG="$ROOT/work/docker-config"

if [ ! -f "$QT_DIR/configure" ]; then
    mkdir -p "$QT_DIR"
    tar -C "$QT_DIR" -xzf "$ROOT/oss/Qt-5.3.2.tar.gz"
fi
mkdir -p "$BUILD_DIR" "$OUT"

if ! docker image inspect "$IMAGE" > /dev/null 2>&1; then
    docker build -t "$IMAGE" -f "$HERE/Dockerfile.builder" "$HERE"
fi

# The stock Mali library provides both EGL and GLESv2 on the device.
MALI="$ROOT/system-rootfs/lib/libMali_linux.so"

EXTRA=()
in_container()
{
    # The image's entrypoint wraps the command in `sudo -s`, which turns the
    # newlines of a multi-line script into line continuations; run bash
    # directly as the same uid instead.
    docker run --label nw-a55=1 \
        -u "$(id -u):$(id -g)" --entrypoint /bin/bash \
        -v "$QT_DIR":/qt \
        -v "$HERE/mkspec":/qt/qtbase/mkspecs/devices/$SPEC \
        -v "$MALI":$SYSROOT/lib/libEGL.so:ro \
        -v "$MALI":$SYSROOT/lib/libGLESv2.so:ro \
        -v "$ROOT/wampy/libs":/libs:ro \
        -v "$ROOT/wampy/libs/glad/include":/gladinc:ro \
        -v "$BUILD_DIR":/build \
        "${EXTRA[@]}" \
        -w /qt \
        "$IMAGE" -c "$1"
}

if [ "${1:-}" = "shell" ]; then
    in_container "$2"
    exit
fi

# Device-side helpers in devtools/, static so they need nothing from the rootfs.
if [ "${1:-}" = "tools" ]; then
    docker run --rm --label nw-a55=1 -u "$(id -u):$(id -g)" --entrypoint /bin/bash \
        -v "$HERE/devtools":/src:ro -v "$OUT":/out "$IMAGE" -c "
            set -e
            for c in /src/*.c; do
                /x-tools/$PLATFORM/bin/$PLATFORM-gcc -static -Os -Wall -o /out/\$(basename \$c .c) \$c
            done"
    exit
fi

if [ "${1:-}" = "hosttest" ]; then
    mkdir -p "$ROOT/work/probe-hosttest-arm"
    EXTRA=(-v "$HERE":/probe:ro -v "$ROOT/work/probe-hosttest-arm":/out)
    in_container "set -e
        cd /out
        /build/bin/qmake /probe/hosttest/hosttest.pro > /dev/null
        make -j$JOBS > make.log 2>&1 || { cat make.log; exit 1; }
        grep -E 'warning|error' make.log || true
        ls -l hosttest"
    exit
fi

if [ "${1:-}" = "qt" ]; then
    in_container "set -e
        ./configure \
            -extprefix /build \
            -device $SPEC \
            -device-option CROSS_COMPILE=/x-tools/$PLATFORM/bin/$PLATFORM- \
            -sysroot $SYSROOT \
            -opensource -confirm-license -release -force-debug-info \
            -nomake examples -nomake tests \
            -qt-zlib -qt-freetype \
            -qpa eglfs -opengl es2 -eglfs -gui -widgets -evdev \
            -skip qtquickcontrols -skip qtserialport -skip qtx11extras \
            -skip qtgraphicaleffects -skip qtwebkit -skip qtsvg -skip qtwinextras \
            -skip qtsensors -skip qtmacextras -skip qtenginio -skip qttranslations \
            -skip qtandroidextras -skip qtmultimedia -skip qtactiveqt \
            -skip qtwebsockets -skip qtlocation -skip qtimageformats \
            -skip qtxmlpatterns -skip qtdoc -skip qtscript -skip qtquick1 \
            -skip qtconnectivity -skip qtwebkit-examples -skip qttools \
            -no-gif -no-libpng -no-libjpeg -no-openssl -no-glib -no-xinput2 \
            -no-pulseaudio -no-alsa -no-nis -no-cups -no-iconv -no-icu \
            -no-fontconfig -no-dbus -no-xcb -no-slog2 -no-pps -no-imf -no-pch \
            -no-lgmon
        make -j$JOBS
        make install"
fi

# Pass 2. qmake.conf picks the probe sources up from PROBE_CFLAGS at qmake
# time, so regenerate the plugin's Makefile before building it. The plugin
# needs QtNetwork, which the app does not load and which lives outside the
# loader's search path, so give it the stock plugin's RUNPATH in place of
# Qt's build-tree rpath.
in_container "set -e
    export PROBE_CFLAGS=-DWMPROBE
    cd qtbase/src/plugins/platforms/eglfs
    /qt/qtbase/bin/qmake -o Makefile eglfs.pro -after \
        'QMAKE_LFLAGS_RPATH=' \
        'QMAKE_LFLAGS+=-Wl,-rpath,/system/vendor/sony/lib -Wl,--enable-new-dtags'
    make clean
    make EGL_LIB='-L/qt/qtdeclarative/lib -lQt5Quick -lQt5Qml -L/qt/qtbase/lib -lQt5Network'"

cp "$QT_DIR/qtbase/plugins/platforms/libqeglfs.so" "$OUT/libqeglfs.so"
ls -l "$OUT/libqeglfs.so"
