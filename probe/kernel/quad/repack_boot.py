#!/usr/bin/env python3
"""Repack an MTK ANDROID! boot.img, swapping only the kernel payload.

  repack_boot.py <stock_boot.img> <new_zImage> <out_boot.img>

Keeps the stock ramdisk blob, addresses, page size and cmdline byte-identical; only the
kernel's MTK payload is replaced (the 512-byte MTK "KERNEL" header is copied verbatim from
the stock image with just its size field updated). Recomputes the mkbootimg SHA1 id.
"""
import hashlib
import math
import struct
import sys

stock, newk, out = sys.argv[1], sys.argv[2], sys.argv[3]
d = bytearray(open(stock, "rb").read())
z = open(newk, "rb").read()
assert d[:8] == b"ANDROID!"

ks, ka, rs, ra, ss, sa, ta, ps, f9 = struct.unpack_from("<9I", d, 8)
pages = lambda n: math.ceil(n / ps)

# stock kernel MTK header (512 B at page 1) — reuse verbatim, patch size field only
khdr = bytearray(d[ps:ps + 512])
assert struct.unpack_from("<I", khdr, 0)[0] == 0x58881688, "no MTK magic on kernel"
assert khdr[8:14] == b"KERNEL", khdr[8:14]
struct.pack_into("<I", khdr, 4, len(z))            # MTK payload size = new zImage size
new_kernel_blob = bytes(khdr) + z
new_ks = len(new_kernel_blob)

# stock ramdisk blob (full ramdisk_size bytes, MTK-wrapped) — keep exactly
roff = ps + pages(ks) * ps
ramdisk_blob = bytes(d[roff:roff + rs])
assert struct.unpack_from("<I", ramdisk_blob, 0)[0] == 0x58881688, "no MTK magic on ramdisk"
assert ramdisk_blob[8:14] == b"ROOTFS", ramdisk_blob[8:14]

# second / dt (this image: none)
soff = roff + pages(rs) * ps
second = bytes(d[soff:soff + ss])

# new ANDROID! header: copy stock, patch kernel_size, recompute id
hdr = bytearray(d[:ps])
struct.pack_into("<I", hdr, 8, new_ks)             # kernel_size
sha = hashlib.sha1()
for blob, size in ((new_kernel_blob, new_ks), (ramdisk_blob, rs), (second, ss)):
    sha.update(blob)
    sha.update(struct.pack("<I", size))
hdr[576:576 + 20] = sha.digest()
hdr[576 + 20:608] = b"\0" * 12

pad = lambda b: b + b"\0" * ((-len(b)) % ps)
img = pad(bytes(hdr[:ps])) + pad(new_kernel_blob) + pad(ramdisk_blob) + pad(second)
open(out, "wb").write(img)

print(f"stock kernel payload = {struct.unpack_from('<I', d, ps + 4)[0]}  new = {len(z)}")
print(f"kernel_size {ks} -> {new_ks}   ramdisk_size {rs} (unchanged)")
print(f"page_size={ps} kernel_addr=0x{ka:08x} ramdisk_addr=0x{ra:08x} tags_addr=0x{ta:08x}")
print(f"wrote {out}  ({len(img)} bytes, stock {len(d)})")
print(f"new id sha1 = {sha.hexdigest()}")
