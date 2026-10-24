#!/bin/bash
#
# Build the host harness against Debian's Qt 5, compare the overlay parser
# with qrcdump.py on the device binary (scancheck.sh), pack hosttest/overlay,
# and that with hosttest/overlay2 laid over it, with tools/mkrcc.py, and run
# hosttest.py under Xvfb with Mesa, in a labeled container (prune with
# `docker container prune --filter label=nw-a55=1`).
#
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
IMAGE=nw-probe-hosttest:3
export DOCKER_CONFIG="$ROOT/work/docker-config"

if ! docker image inspect "$IMAGE" > /dev/null 2>&1; then
    docker build -t "$IMAGE" "$HERE"
fi

BUILD="$ROOT/work/probe-hosttest"
mkdir -p "$BUILD"
# xvfb-run hangs as PID 1 (it never sees Xvfb's SIGUSR1), so start Xvfb
# directly under --init.
docker run --init --label nw-a55=1 -u "$(id -u):$(id -g)" \
    -v "$HERE/..":/probe:ro -v "$ROOT/tools":/tools:ro -v "$BUILD":/build -w /build \
    -v "$ROOT/system-rootfs/vendor/sony/bin/HgrmMediaPlayerApp":/device/HgrmMediaPlayerApp:ro \
    -e HOME=/build -e DISPLAY=:99 -e PYTHONUNBUFFERED=1 \
    "$IMAGE" /bin/bash -c "set -e
        qmake /probe/hosttest/hosttest.pro > /dev/null
        make -j8 > make.log 2>&1 || { cat make.log; exit 1; }
        grep -E 'warning|error' make.log || true
        test -x hosttest
        /probe/hosttest/scancheck.sh /device/HgrmMediaPlayerApp
        python3 /tools/mkrcc.py /probe/hosttest/overlay overlay.rcc
        python3 /tools/mkrcc.py /probe/hosttest/overlay /probe/hosttest/overlay2 overlay2.rcc
        Xvfb :99 -screen 0 1024x768x24 -nolisten tcp 2> /dev/null &
        for i in \$(seq 50); do test -e /tmp/.X11-unix/X99 && break; sleep 0.1; done
        python3 /probe/hosttest/hosttest.py"
