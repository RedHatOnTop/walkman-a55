#!/bin/bash
#
# Build a single-script UPG for the Sony NW-A50 series from DIR/update.sh.
#
#   DIR/NW_WM_FW.UPG        for a device running Walkman One (the user's)
#   DIR/stock/NW_WM_FW.UPG  for a device on Sony firmware
#
# Walkman One swaps the device's fwpchk for one with a different key, so a
# package built for one firmware is rejected by the other. A rejected package
# is harmless (the updater clears the flag and boots normally) but does nothing.
#
# Uses nwupg.py next to this script, which reproduces upgtool output byte for
# byte for both key sets.  Usage:  build_upg.sh DIR
#
set -e

TOOLS="$(cd "$(dirname "$0")" && pwd)"
NWUPG="$TOOLS/nwupg.py"
DIR="$(cd "$1" && pwd)"
NAME="$(basename "$DIR")"

cd "$DIR"

# File 1 is the md5 manifest; the host script extracts it as md5.txt and
# renames it to md5.txt.0. These packages flash nothing, so it lists no images.
echo "# $NAME: no images to flash" > md5.txt

# The device's /bin/sh chokes on CRs.
sed -i 's/\r$//' update.sh
sh -n update.sh

mkdir -p stock
python3 -I "$NWUPG" -w pack NW_WM_FW.UPG update.sh md5.txt
python3 -I "$NWUPG" pack stock/NW_WM_FW.UPG update.sh md5.txt

# Round-trip: unpack each with its own key set and compare against the
# sources, so a wrong key or a packing bug fails here and not on the device.
verify()
{
    local flag="$1" upg="$2" out="$TOOLS/../work/verify-$NAME-$3"
    mkdir -p "$out"
    python3 -I "$NWUPG" $flag check "$upg" "$out"
    tr -d '\000' < "$out/file0.bin" | cmp - update.sh
    tr -d '\000' < "$out/file1.bin" | cmp - md5.txt
    echo "OK: $NAME/$upg round-trips"
}
echo
verify -w NW_WM_FW.UPG w1
verify "" stock/NW_WM_FW.UPG stock
if python3 -I "$NWUPG" check NW_WM_FW.UPG > /dev/null; then
    echo "ERROR: the Walkman One package also passes with the stock key" >&2
    exit 1
fi
md5sum NW_WM_FW.UPG stock/NW_WM_FW.UPG
