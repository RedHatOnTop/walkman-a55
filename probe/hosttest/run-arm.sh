#!/bin/bash
#
# Run hosttest.py on the device's own Qt 5.3: the harness from
# `probe/build.sh hosttest` under qemu-arm, with the libraries, QML imports
# and fonts of the device's system partition (work/device/system) and
# ramdisk, and the minimal platform plugin from this Qt build. run.sh tests
# against Debian's Qt 5.15, which is no evidence for what 5.3 does: a reload
# that passed there crashed the app on the device. With no OpenGL here the
# harness window is never shown, so only the groups named run, reload by
# default.
#
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
IMAGE=nw-qemu-arm:1
BUILD="$ROOT/work/probe-hosttest-arm"
export DOCKER_CONFIG="$ROOT/work/docker-config"

[ -x "$BUILD/hosttest" ] || { echo "no ARM harness; run probe/build.sh hosttest" >&2; exit 1; }
if ! docker image inspect "$IMAGE" > /dev/null 2>&1; then
    docker build -t "$IMAGE" -f "$HERE/Dockerfile.qemu" "$HERE"
fi

GUEST="qemu-arm-static -L /sysroot
    -E LD_LIBRARY_PATH=/system/vendor/sony/lib:/system/lib
    -E QT_QPA_PLATFORM=minimal -E QT_PLUGIN_PATH=/plugins
    -E QML2_IMPORT_PATH=/system/vendor/sony/qml
    -E QT_QPA_FONTDIR=/system/vendor/sony/lib/fonts
    -E HOSTTEST_NOSHOW=1 ./hosttest"
docker run --init --label nw-a55=1 -u "$(id -u):$(id -g)" \
    -v "$HERE/..":/probe:ro -v "$ROOT/tools":/tools:ro -v "$BUILD":/build -w /build \
    -v "$ROOT/work/device/system":/sysroot/system:ro \
    -v "$ROOT/work/ramdisk-check/root/lib":/sysroot/lib:ro \
    -v "$ROOT/work/qt-build/plugins/platforms/libqminimal.so":/plugins/platforms/libqminimal.so:ro \
    -e HOME=/build -e PYTHONUNBUFFERED=1 -e HOSTTEST_CMD="$(echo $GUEST)" \
    "$IMAGE" /bin/bash -c "set -e
        python3 /tools/mkrcc.py /probe/hosttest/overlay overlay.rcc
        python3 /tools/mkrcc.py /probe/hosttest/overlay /probe/hosttest/overlay2 overlay2.rcc
        python3 /probe/hosttest/hosttest.py ${*:-reload}"
