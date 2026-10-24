#!/usr/bin/env python3
"""Set boolean resources of an APK to false in place in its resources.arsc.

    flip_bools.py IN.apk OUT.apk NAME [NAME...]

Every config's value of each named <bool> is rewritten from true to false; the
table keeps its size and layout, so nothing else in the APK changes. OUT.apk is
unsigned: the old v1 signature files are dropped, the caller signs it again.
Fails if a name has no bool entry at all, so a renamed resource cannot slip by.
"""
import struct
import sys
import zipfile

RES_STRING_POOL = 0x0001
RES_TABLE = 0x0002
RES_TABLE_PACKAGE = 0x0200
RES_TABLE_TYPE = 0x0201
TYPE_INT_BOOLEAN = 0x12
NO_ENTRY = 0xFFFFFFFF
FLAG_COMPLEX = 0x0001


def string_pool(buf, off):
    typ, hsz, _size, count, _styles, flags, start = struct.unpack_from("<HHIIIII", buf, off)
    if typ != RES_STRING_POOL:
        raise ValueError("no string pool at 0x%x" % off)
    utf8 = flags & 0x100
    offsets = struct.unpack_from("<%dI" % count, buf, off + hsz)
    out = []
    for o in offsets:
        p = off + start + o
        if utf8:
            p += 2 if buf[p] & 0x80 else 1
            n = buf[p]
            p += 1
            if n & 0x80:
                n = ((n & 0x7F) << 8) | buf[p]
                p += 1
            out.append(buf[p:p + n].decode("utf-8"))
        else:
            n, = struct.unpack_from("<H", buf, p)
            p += 2
            if n & 0x8000:
                n = ((n & 0x7FFF) << 16) | struct.unpack_from("<H", buf, p)[0]
                p += 2
            out.append(buf[p:p + 2 * n].decode("utf-16le"))
    return out


def flip(arsc, names):
    buf = bytearray(arsc)
    typ, hsz, size, npkg = struct.unpack_from("<HHII", buf, 0)
    if typ != RES_TABLE or size != len(buf):
        raise ValueError("not a resource table")
    found = {n: 0 for n in names}
    off = hsz
    while off < size:
        ctype, chsz, csize = struct.unpack_from("<HHI", buf, off)
        if ctype == RES_TABLE_PACKAGE:
            type_strings_off, = struct.unpack_from("<I", buf, off + 268)
            key_strings_off, = struct.unpack_from("<I", buf, off + 276)
            type_names = string_pool(buf, off + type_strings_off)
            key_names = string_pool(buf, off + key_strings_off)
            sub = off + chsz
            while sub < off + csize:
                stype, shsz, ssize = struct.unpack_from("<HHI", buf, sub)
                if stype == RES_TABLE_TYPE and type_names[buf[sub + 8] - 1] == "bool":
                    count, entries = struct.unpack_from("<II", buf, sub + 12)
                    for i in range(count):
                        eo, = struct.unpack_from("<I", buf, sub + shsz + 4 * i)
                        if eo == NO_ENTRY:
                            continue
                        e = sub + entries + eo
                        esize, eflags, key = struct.unpack_from("<HHI", buf, e)
                        name = key_names[key]
                        if name not in found or eflags & FLAG_COMPLEX:
                            continue
                        v = e + esize
                        dtype, data = struct.unpack_from("<xxxBI", buf, v)
                        if dtype != TYPE_INT_BOOLEAN:
                            raise ValueError("%s is not a boolean (type 0x%x)" % (name, dtype))
                        struct.pack_into("<I", buf, v + 4, 0)
                        found[name] += 1
                        print("%-36s %s -> false" % (name, "true" if data else "false"))
                sub += ssize
        off += csize
    missing = [n for n, c in found.items() if c == 0]
    if missing:
        raise ValueError("no bool entry for: " + ", ".join(missing))
    return bytes(buf)


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__.strip())
    src, dst, names = sys.argv[1], sys.argv[2], sys.argv[3:]
    with zipfile.ZipFile(src) as zin, zipfile.ZipFile(dst, "w") as zout:
        for info in zin.infolist():
            n = info.filename
            if n.startswith("META-INF/") and (n == "META-INF/MANIFEST.MF" or n.endswith((".SF", ".RSA", ".DSA", ".EC"))):
                continue
            data = zin.read(info)
            if info.filename == "resources.arsc":
                data = flip(data, names)
            zout.writestr(info, data, compress_type=info.compress_type)


if __name__ == "__main__":
    main()
