"""NW-A50 series UPG (v2, AES) reader and writer, written from Rockbox upg.c.

    nwupg.py [-w] check UPG [OUTDIR]
    nwupg.py [-w] pack OUT FILE0 [FILE1 ...]

-w targets a device running Walkman One. Its installer swaps fwpchk for one
with a different passkey and IV and the nw-wm1a KAS (unknown321/nw-installer,
installer/Makefile `walkmanOne` target).
"""
import hashlib
import struct
import sys

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

VARIANTS = {
    "stock": ("dd49de9dab2bce5a59090c01049576d537af6a313e5c0a2c24353937a87352d6",
              b"9cc4419c8bef488c", b"6063ce1efa1d543a"),
    "w1": ("e8d171a5d92f35eed9658c03fb9f86a169591659851fd7c49525f587a70b526c",
           b"9cc4419c8bef488e", b"6063ce1efa1d543b"),
}
KAS, PASSKEY, IV = VARIANTS["stock"]
# upgtool pads every v2 payload to this and records the padded size.
CHUNK = 4096


def _cbc(key, data, enc):
    c = Cipher(algorithms.AES(key), modes.CBC(IV))
    op = c.encryptor() if enc else c.decryptor()
    return op.update(data) + op.finalize()


def _ecb(key, data, enc):
    c = Cipher(algorithms.AES(key), modes.ECB())
    op = c.encryptor() if enc else c.decryptor()
    return op.update(data) + op.finalize()


def keysig():
    ks = _cbc(PASSKEY, bytes.fromhex(KAS), False)
    return ks[:16], ks[16:]


def check(path, outdir=None):
    key, sig = keysig()
    buf = open(path, "rb").read()
    ok = True

    stored, actual = buf[:16], hashlib.md5(buf[16:]).digest()
    ok &= stored == actual
    print(f"size={len(buf)} md5 stored={stored.hex()} actual={actual.hex()} "
          f"{'OK' if stored == actual else 'MISMATCH'}")

    hdr = _ecb(key, buf[16:48], False)
    nr = struct.unpack_from("<I", hdr, 16)[0]
    ok &= hdr[:16] == sig
    if hdr[:16] != sig:
        print("header sig MISMATCH (wrong key set for this UPG?)")
        return False
    print(f"header sig OK nr_files={nr} pad_zero={hdr[20:32] == bytes(12)}")

    tbl = _ecb(key, buf[48:48 + 16 * nr], False)
    for i in range(nr):
        off, size, p0, p1 = struct.unpack_from("<4I", tbl, 16 * i)
        data = _cbc(key, buf[off:off + (size + 15) // 16 * 16], False)
        print(f"file{i}: offset={off:#x} size={size} pad_zero={p0 == p1 == 0} "
              f"stripped_md5={hashlib.md5(data.rstrip(bytes(1))).hexdigest()}")
        if outdir:
            open(f"{outdir}/file{i}.bin", "wb").write(data)
    return ok


def pack(out, *inputs):
    key, sig = keysig()
    payloads = []
    for p in inputs:
        data = open(p, "rb").read()
        payloads.append(data + bytes(-len(data) % CHUNK))

    offset = 16 + 32 + 16 * len(payloads)
    table = b""
    for data in payloads:
        table += struct.pack("<4I", offset, len(data), 0, 0)
        offset += len(data)

    hdr = sig + struct.pack("<I", len(payloads)) + bytes(12)
    body = _ecb(key, hdr + table, True)
    body += b"".join(_cbc(key, d, True) for d in payloads)
    open(out, "wb").write(hashlib.md5(body).digest() + body)
    print(f"wrote {out}: {16 + len(body)} bytes, {len(payloads)} files")


if __name__ == "__main__":
    argv = sys.argv[1:]
    if argv[:1] == ["-w"]:
        KAS, PASSKEY, IV = VARIANTS["w1"]
        argv = argv[1:]
    cmd, args = argv[0], argv[1:]
    if cmd == "check":
        sys.exit(0 if check(*args) else 1)
    elif cmd == "pack":
        pack(*args)
    else:
        sys.exit(__doc__)
