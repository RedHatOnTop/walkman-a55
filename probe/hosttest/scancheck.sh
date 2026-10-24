#!/bin/bash
#
# Compare the overlay's C++ resource parser (overlayscan) with
# tools/qrcdump.py on a binary, normally the device's HgrmMediaPlayerApp.
# Runs inside the hosttest container from the build directory.
#
set -euo pipefail

BIN=$1
mkdir -p scan
(cd scan && qmake /probe/hosttest/overlayscan.pro > /dev/null && make > /dev/null)
scan/overlayscan "$BIN" > scan/cpp.txt
python3 /tools/qrcdump.py "$BIN" scan/qrc > scan/qrcdump.txt
{
    sed -n 's/^# \(tree .* data [^,]*\),.*/\1/p' scan/qrcdump.txt
    echo "$(grep -vc '^#' scan/qrcdump.txt) files"
} > scan/py.txt
if diff scan/cpp.txt scan/py.txt; then
    echo "PASS overlay parser matches qrcdump on $BIN: $(grep -c '^tree' scan/cpp.txt) roots, $(tail -1 scan/cpp.txt)"
else
    echo "FAIL overlay parser differs from qrcdump on $BIN"
    exit 1
fi
