#!/usr/bin/env python3
"""Build / inspect the MTK BT NVRAM record (the "BT_Addr" file) for the NW-A55.

The CM image's libbluetooth_mtk.so reads one 64-byte record,
`ap_nvram_btradio_struct`, through libnvram (`NVM_GetFileDesc(lid=1)` ->
/data/nvram/APCFG/APRDEB/BT_Addr).  Layout, recovered from the log format
string in libbluetooth_mtk.so and cross-checked against Sony's own
/system/etc/BT_Addr_ForWM_Default (66 bytes, checksum verified below):

    off  len  field
      0    6  BD address, human order (MSB first)
      6    2  Voice    (SCO voice setting)
      8    4  Codec    (PCM codec setting)
     12    6  Radio    (RF configuration)
     18    7  Sleep    (sleep mode configuration)
     25    2  BtFTR    (feature bits)
     27    3  TxPWOffset
     30    6  CoexAdjust
     36   28  reserved (zero)
     --  ---
     64    1  0xAA  (libnvram "check flag")
     65    1  check number: byte-wise fold over bytes 0..63, alternating
               "r = (r + b) & 0xff" (even index) and "r ^= b" (odd index),
               which is NVM_ComputeCheckNo() in libnvram.so.

How the record gets used and what is and is not verified is in
devlog/2026-10-15-bt-consys-groundwork.md.  Nothing here touches a device.

Usage:
  gen-bt-nvram.py --self-test
  gen-bt-nvram.py --addr 12:34:56:78:9a:bc -o bt_nvram.bin
  gen-bt-nvram.py --addr-from-file pulled/BT_Addr -o bt_nvram.bin
  gen-bt-nvram.py --addr zero -o bt_nvram.bin     # chip eFUSE address wins
  gen-bt-nvram.py --check some/BT_Addr            # decode + validate
"""
import argparse
import sys

REC = 64
FIELDS = [
    ("addr", 0, 6), ("voice", 6, 2), ("codec", 8, 4), ("radio", 12, 6),
    ("sleep", 18, 7), ("btftr", 25, 2), ("txpwoffset", 27, 3),
    ("coexadjust", 30, 6), ("reserved", 36, 28),
]

# Sony /system/etc/BT_Addr_ForWM_Default, bytes 0..63 (address left at zero).
# Source: work/device/system/etc/BT_Addr_ForWM_Default (66 bytes, tail "aa e9").
SONY_FORWM = bytes.fromhex(
    "000000000000" "6000" "23100000" "078000060707"
    "03401f401f0004" "0000" "000000" "000000000000" + "00" * 28)

# CM libbluetooth_mtk.so built-in 0x8127 table (.rodata 0x313c) == Sony
# libbtdrv.so stBtDefault_8590 apart from the placeholder address.
CM_8127 = bytes.fromhex(
    "000046812701" "6000" "23100000" "078000060507"
    "03401f401f0004" "8000" "ffffff" "000000000000" + "00" * 28)

# Addresses libbluetooth_mtk.so treats as "NVRAM address is a default, ask the
# chip's eFUSE instead": all zero, and the 0x8127 placeholder.
DEFAULT_ADDRS = (bytes(6), bytes.fromhex("000046812701"))


def check_number(data):
    """NVM_ComputeCheckNo(): add / xor alternately over the data bytes."""
    r = 0
    add = True
    for b in data:
        r = (r + b) & 0xFF if add else r ^ b
        add = not add
    return r


def build(record):
    assert len(record) == REC
    return bytes(record) + bytes([0xAA, check_number(record)])


def parse_addr(text):
    parts = text.replace("-", ":").split(":")
    if len(parts) != 6:
        raise SystemExit(f"bad BD address {text!r}: want AA:BB:CC:DD:EE:FF")
    try:
        b = bytes(int(p, 16) for p in parts)
    except ValueError:
        raise SystemExit(f"bad BD address {text!r}")
    return b


def decode(blob, label):
    if len(blob) not in (REC, REC + 2):
        raise SystemExit(f"{label}: {len(blob)} bytes, want 64 or 66")
    rec = blob[:REC]
    print(f"{label}: {len(blob)} bytes")
    for name, off, n in FIELDS:
        print(f"  {name:11s} {rec[off:off + n].hex(' ')}")
    addr = rec[:6]
    print("  BD address  " + ":".join(f"{x:02x}" for x in addr))
    if addr in DEFAULT_ADDRS:
        print("  note: libbluetooth_mtk treats this as a default and asks the chip eFUSE")
    if len(blob) == REC + 2:
        want = check_number(rec)
        ok = blob[REC] == 0xAA and blob[REC + 1] == want
        print(f"  trailer     {blob[REC]:02x} {blob[REC + 1]:02x}  "
              f"(expected aa {want:02x}) {'OK' if ok else 'BAD'}")
        return ok
    return True


def self_test():
    # The one real file we have: Sony's default. Its trailer must reproduce.
    sony = build(SONY_FORWM)
    assert sony[-2:] == bytes.fromhex("aae9"), sony[-2:].hex()
    assert check_number(SONY_FORWM) == 0xE9
    assert len(build(CM_8127)) == 66
    # field table must tile the record exactly
    pos = 0
    for _, off, n in FIELDS:
        assert off == pos
        pos += n
    assert pos == REC
    print("self-test OK: Sony ForWM default reproduces trailer aa e9")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--check", metavar="FILE", help="decode and validate a BT_Addr file")
    ap.add_argument("--addr", help="AA:BB:CC:DD:EE:FF, or 'zero' (let the chip eFUSE decide)")
    ap.add_argument("--addr-from-file", metavar="FILE",
                    help="take bytes 0..5 from a BT_Addr file pulled off the Sony host")
    ap.add_argument("--template", choices=("sony", "cm8127"), default="sony",
                    help="radio/sleep/feature bytes: Sony's Walkman default (default) "
                         "or CM's built-in 0x8127 table")
    ap.add_argument("-o", "--output", help="write the 66-byte file here")
    a = ap.parse_args()

    if a.self_test:
        self_test()
        return
    if a.check:
        sys.exit(0 if decode(open(a.check, "rb").read(), a.check) else 1)

    if bool(a.addr) == bool(a.addr_from_file):
        ap.error("give exactly one of --addr / --addr-from-file (or --self-test / --check)")
    if a.addr_from_file:
        raw = open(a.addr_from_file, "rb").read()
        if len(raw) < 6:
            raise SystemExit("file shorter than a BD address")
        addr = raw[:6]
    elif a.addr.lower() == "zero":
        addr = bytes(6)
    else:
        addr = parse_addr(a.addr)
    if addr[0] & 1:
        raise SystemExit("refusing a multicast address (bit 0 of the first byte is set)")

    rec = bytearray(SONY_FORWM if a.template == "sony" else CM_8127)
    rec[0:6] = addr
    blob = build(rec)
    decode(blob, a.output or "(stdout)")
    if a.output:
        with open(a.output, "wb") as f:
            f.write(blob)
        print(f"wrote {a.output}")


if __name__ == "__main__":
    main()
