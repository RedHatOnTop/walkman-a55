#!/bin/bash
#
# Build btnvshim for the CM container (armeabi-v7a, API 22) with the NDK, or run
# its host-side logic test.  Nothing here touches the device.
#
#   ./build.sh            -> out/libnvram-btshim.so   (soname libnvram.so)
#   ./build.sh test       -> builds a host binary and runs it against a seed
#
# After the build, push as /tmp/libnvram-btshim.so (see operator-checklist.md).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
NDK="${NDK:-$HOME/Android/sdk/ndk/21.4.7075529}"
BIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
OUT="${OUT:-$HERE/out}"
mkdir -p "$OUT"

case "${1:-build}" in
build)
    "$BIN/armv7a-linux-androideabi22-clang" -shared -fPIC -O2 -Wall -Wextra \
        -fvisibility=hidden -Wl,--no-undefined -Wl,-soname,libnvram.so \
        -o "$OUT/libnvram-btshim.so" "$HERE/btnvshim.c" -llog
    echo "EXIT=$?"
    "$BIN/llvm-readelf" --dyn-syms --dynamic "$OUT/libnvram-btshim.so" | grep -E "NVM_|SONAME|NEEDED"
    ;;
test)
    SEED="$OUT/test_seed.bin"
    WRITTEN="$OUT/test_written.bin"
    rm -f "$WRITTEN"
    python3 -I "$HERE/../gen-bt-nvram.py" --addr 00:11:22:33:44:55 -o "$SEED" > /dev/null
    gcc -O0 -g -Wall -Wextra -DBTNV_HOST_TEST \
        -DBTNV_SEED="\"$SEED\"" -DBTNV_WRITTEN="\"$WRITTEN\"" \
        -o "$OUT/test_btnvshim" "$HERE/btnvshim.c" "$HERE/test_btnvshim.c"
    "$OUT/test_btnvshim" "$SEED"
    echo "EXIT=$?"
    ;;
*)
    echo "usage: $0 [build|test]" >&2
    exit 2
    ;;
esac
