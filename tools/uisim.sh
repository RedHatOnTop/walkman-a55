#!/bin/bash
#
# Render app screens on the host with probe/uisim.
#
#   tools/uisim.sh [--mod mods/NAME] [--lang ko_KR] SPEC.json [SPEC.json ...]
#
# Each SPEC (see probe/uisim/values/) names a qrc:/ screen, the values its
# ViewModel would push, and optionally a window. PNGs land in
# work/uisim/out/<spec name>.png. --mod builds that mod with tools/mkmod.sh
# and renders with its overlay in front of the stock resources. --lang picks
# the app's translation (default en_US). A spec's "cover" is a file in
# probe/uisim/covers that stands in for the playing track's cover art.
#
# Resources, fonts and translations come from the device's own system
# partition (work/device/system, from the backup in docs/DEVICE-STEPS.md)
# when it is there, else from the A50 bundle in system-rootfs.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE=nw-probe-hosttest:3
WORK="$ROOT/work/uisim"
export DOCKER_CONFIG="$ROOT/work/docker-config"

MOD=
LANG_ID=en_US
while [ $# -gt 0 ]; do
    case "$1" in
        --mod) MOD="$2"; shift 2 ;;
        --lang) LANG_ID="$2"; shift 2 ;;
        *) break ;;
    esac
done
[ $# -gt 0 ] || { echo "usage: $0 [--mod mods/NAME] SPEC.json..." >&2; exit 2; }

if [ -f "$ROOT/work/device/system/vendor/sony/bin/HgrmMediaPlayerApp" ]; then
    SYS="$ROOT/work/device/system"
    QRC="$ROOT/work/qrc-device"
    STOCK=stock-device.rcc
    MOC="$ROOT/work/moc-app-device.json"
else
    SYS="$ROOT/system-rootfs"
    QRC="$ROOT/work/qrc"
    STOCK=stock.rcc
    MOC="$ROOT/work/moc-app.json"
fi
APPBIN="$SYS/vendor/sony/bin/HgrmMediaPlayerApp"
# The transport button images live on the option2 partition (/opt2), not in
# the app; work/device/opt2 is extracted from the backup's option2.img.
OPT2=
[ -d "$ROOT/work/device/opt2/appresouce" ] && OPT2="-v $ROOT/work/device/opt2:/opt2:ro"

mkdir -p "$WORK/out" "$WORK/build" "$WORK/specs"
if [ ! -d "$QRC" ]; then
    "$ROOT/.venv/bin/python" -I "$ROOT/tools/qrcdump.py" "$APPBIN" "$QRC" > /dev/null
fi
[ -f "$WORK/$STOCK" ] || python3 "$ROOT/tools/mkrcc.py" "$QRC" "$WORK/$STOCK"
[ -f "$MOC" ] || "$ROOT/.venv/bin/python" -I "$ROOT/tools/mocdump.py" \
    "$APPBIN" --json "$MOC" > /dev/null
python3 "$ROOT/tools/uisim_stubs.py" "$MOC" \
    "$SYS/vendor/sony/bin/ScreenSettings.json" "$ROOT/probe/uisim/globals.json" \
    "$WORK/stubs" > /dev/null

# probe/uisim/hostfix patches what only runs on Sony's Qt 5.3 (see its
# patches); it sits between a mod and the stock resources.
"$ROOT/tools/mkmod.sh" "$ROOT/probe/uisim/hostfix" "$QRC" > /dev/null
RCCS="--rcc /hostfix/overlay.rcc --rcc /work/$STOCK"
if [ -n "$MOD" ]; then
    "$ROOT/tools/mkmod.sh" "$MOD" "$QRC" > /dev/null
    RCCS="--rcc /mod/overlay.rcc $RCCS"
fi

NAMES=()
for spec in "$@"; do
    name="$(basename "$spec" .json)"
    cp "$spec" "$WORK/specs/$name.json"
    NAMES+=("$name")
done

if ! docker image inspect "$IMAGE" > /dev/null 2>&1; then
    docker build -t "$IMAGE" "$ROOT/probe/hosttest"
fi
docker run --init --label nw-a55=1 -u "$(id -u):$(id -g)" \
    -v "$ROOT/probe/uisim":/uisim:ro -v "$WORK":/work \
    -v "$SYS/vendor/sony/lib/fonts":/fonts:ro \
    -v "$SYS/vendor/sony/translations":/translations:ro \
    -v "$ROOT/probe/uisim/covers":/covers:ro \
    $OPT2 \
    -v "$ROOT/work/mods/hostfix":/hostfix:ro \
    -v "$ROOT/work/mods/$(basename "${MOD:-none}")":/mod:ro \
    -e HOME=/work -e DISPLAY=:99 -w /work/build "$IMAGE" /bin/bash -c "
        set -e
        qmake /uisim/uisim.pro > /dev/null
        make -j8 > make.log 2>&1 || { cat make.log; exit 1; }
        Xvfb :99 -screen 0 1024x1024x24 -nolisten tcp 2> /dev/null &
        for i in \$(seq 50); do test -e /tmp/.X11-unix/X99 && break; sleep 0.1; done
        status=0
        for name in ${NAMES[*]}; do
            ./uisim $RCCS --stubs /work/stubs --fonts /fonts --covers /covers \
                --qm /translations/HgrmMediaPlayerApp_$LANG_ID.qm \
                --spec /work/specs/\$name.json --out /work/out/\$name.png \
                2> /work/out/\$name.log || status=1
        done
        exit \$status"
