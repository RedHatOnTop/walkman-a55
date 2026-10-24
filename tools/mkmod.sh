#!/bin/bash
#
# Build a UI mod into an overlay .rcc for the wmprobe plugin.
#
#   tools/mkmod.sh mods/NAME [QRC_DIR]
#
# A mod is patches/*.patch, unified diffs against the app's qrc tree (paths
# a/framework/FrameWork.qml), plus optional files/, new or wholesale
# replacement files laid out as qrc paths. The originals come from QRC_DIR
# (default work/qrc-device, from tools/qrcdump.py on the HgrmMediaPlayerApp in
# the device backup, else work/qrc from the A50 bundle),
# so the repository holds only our changes. Every resulting .qml is checked
# with qmllint. Output: work/mods/NAME/overlay.rcc, and the patched tree in a
# fresh work/mods/NAME/tree.* directory for review.
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MOD="$(cd "$1" && pwd)"
NAME="$(basename "$MOD")"
DEFAULT_QRC="$ROOT/work/qrc-device"
[ -d "$DEFAULT_QRC" ] || DEFAULT_QRC="$ROOT/work/qrc"
QRC="$(cd "${2:-$DEFAULT_QRC}" && pwd)"
OUT="$ROOT/work/mods/$NAME"
IMAGE=nw-probe-hosttest:3
export DOCKER_CONFIG="$ROOT/work/docker-config"

mkdir -p "$OUT"
STAGE="$(mktemp -d "$OUT/tree.XXXXXX")"

shopt -s nullglob
for p in "$MOD"/patches/*.patch; do
    for f in $(sed -n 's|^+++ b/\([^[:space:]]*\).*|\1|p' "$p"); do
        if [ -f "$QRC/$f" ] && [ ! -f "$STAGE/$f" ]; then
            mkdir -p "$(dirname "$STAGE/$f")"
            cp "$QRC/$f" "$STAGE/$f"
        fi
    done
done
if [ -d "$MOD/files" ]; then
    cp -r "$MOD/files/." "$STAGE/"
fi

if ! docker image inspect "$IMAGE" > /dev/null 2>&1; then
    docker build -t "$IMAGE" "$ROOT/probe/hosttest"
fi
docker run --label nw-a55=1 -u "$(id -u):$(id -g)" \
    -v "$STAGE":/stage -v "$MOD":/mod:ro -w /stage "$IMAGE" /bin/bash -c '
        set -euo pipefail
        shopt -s nullglob
        for p in /mod/patches/*.patch; do
            patch -p1 -s --no-backup-if-mismatch -r - < "$p"
        done
        mapfile -t QML < <(find . -name "*.qml" | sort)
        if [ ${#QML[@]} -gt 0 ]; then
            /usr/lib/qt5/bin/qmllint "${QML[@]}"
        fi'

python3 "$ROOT/tools/mkrcc.py" "$STAGE" "$OUT/overlay.rcc"
echo "tree: $STAGE"
(cd "$STAGE" && find . -type f | sort)
