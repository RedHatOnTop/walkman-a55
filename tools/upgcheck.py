"""Independent NW-A50 UPG (v2, AES) reader, written from Rockbox upg.c."""
import hashlib
import struct
import sys

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

KAS = "dd49de9dab2bce5a59090c01049576d537af6a313e5c0a2c24353937a87352d6"
PASSKEY = b"9cc4419c8bef488c"
IV = b"6063ce1efa1d543a"


def cbc_dec(key, data):
    d = Cipher(algorithms.AES(key), modes.CBC(IV)).decryptor()
    return d.update(data) + d.finalize()


def ecb_dec(key, data):
    d = Cipher(algorithms.AES(key), modes.ECB()).decryptor()
    return d.update(data) + d.finalize()


def main(path, outdir=None):
    keysig = cbc_dec(PASSKEY, bytes.fromhex(KAS))
    key, sig = keysig[:16], keysig[16:]
    print(f"key={key!r} sig={sig!r}")

    buf = open(path, "rb").read()
    stored, actual = buf[:16], hashlib.md5(buf[16:]).digest()
    print(f"size={len(buf)} md5 stored={stored.hex()} actual={actual.hex()} "
          f"{'OK' if stored == actual else 'MISMATCH'}")

    hdr = ecb_dec(key, buf[16:48])
    hsig, nr = hdr[:16], struct.unpack_from("<I", hdr, 16)[0]
    pad = hdr[20:32]
    print(f"header sig {'OK' if hsig == sig else 'MISMATCH'} nr_files={nr} pad_zero={pad == bytes(12)}")

    tbl = ecb_dec(key, buf[48:48 + 16 * nr])
    for i in range(nr):
        off, size, p0, p1 = struct.unpack_from("<4I", tbl, 16 * i)
        n = (size + 15) // 16 * 16
        data = cbc_dec(key, buf[off:off + n])
        print(f"file{i}: offset={off:#x} size={size} pad_zero={p0 == p1 == 0} "
              f"md5={hashlib.md5(data).hexdigest()} stripped_md5="
              f"{hashlib.md5(data.rstrip(bytes(1))).hexdigest()}")
        if outdir:
            open(f"{outdir}/file{i}.bin", "wb").write(data)


if __name__ == "__main__":
    main(*sys.argv[1:])
