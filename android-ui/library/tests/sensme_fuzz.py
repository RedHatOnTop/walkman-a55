"""SMFMF variants for checking the SensMe port against the device's library.

    sensme_fuzz.py gen SAMPLE MODEL OUT [COUNT] [SEED]   length-prefixed blobs
    sensme_fuzz.py compare HARNESS PORT                 smuharness lines vs tst_sensme lines

Most variants pull the 16 cluster words toward a random centroid and scatter
the rule words around their thresholds; the rest damage the chunk structure
(sizes, order, duplicates, truncation). SAMPLE is a real analysis blob,
MODEL the sensme.bin stage.py writes.
"""
import random
import struct
import sys

CLUSTER_WORDS = (81, 79, 55, 86, 88, 63, 76, 84, 56, 61, 87, 77, 58, 75, 89, 60)
STHF_SLOT = (15, 16, 12, 1, 17, 4, 0, 13, 5, 7, 18, 10, 9, 19, 6, 8, 20, 21, 22, 2,
             23, 24, 25, 26, 27, 3, 28, 29, 30, 31, 32, 14, 33, 34, 35, 36, 11, 37, 38, 39)
MAGIC = b"STAEMMLW"
VERSION = bytes.fromhex("01008000")


def chunks(blob):
    out, pos = [], 0
    while pos + 20 <= len(blob):
        n = struct.unpack(">I", blob[pos + 16:pos + 20])[0]
        out.append([blob[pos:pos + 4], blob[pos + 4:pos + 16], bytearray(blob[pos + 20:pos + 20 + n]), None])
        pos += 20 + n
    return out


def join(cs):
    return b"".join(tag + head + struct.pack(">I", len(p) if n is None else n) + bytes(p) for tag, head, p, n in cs)


def model(path):
    data = open(path, "rb").read()
    scale = struct.unpack_from("<16d", data, 0)
    centroids = [struct.unpack_from("<16d", data, 128 + c * 128) for c in range(48)]
    return scale, centroids


def word_value(r):
    pick = r.random()
    if pick < 0.03:
        return 0xff
    if pick < 0.06:
        return 0
    if pick < 0.75:
        return r.randrange(0x18, 0x5a)
    return r.randrange(0, 0x100)


def features(r, cs, scale, centroids):
    words = {}
    c = r.randrange(48)
    for i, w in enumerate(CLUSTER_WORDS):
        v = round(centroids[c][i] / scale[i] + r.gauss(0, r.choice((1, 4, 12))))
        words[w] = 0xff if r.random() < 0.01 else min(max(v, 0), 0xfe)
    for w in range(4, 90):
        if w not in words and r.random() < 0.9:
            words[w] = word_value(r)
    if r.random() < 0.05:
        words[78] = 0
    for chunk in cs:
        tag, p = chunk[0], chunk[2]
        if tag == b"STBF":
            for w in range(54, 90):
                if w in words:
                    p[w - 54] = words[w]
        elif tag == b"STMO":
            for i in range(0, len(p) - 3, 4):
                t = p[i] << 8 | p[i + 1]
                if p[i + 2] == 0 and 0x100 <= t <= 0x109 and 4 + t - 0x100 in words:
                    p[i + 3] = words[4 + t - 0x100]
                if r.random() < 0.02:
                    p[i + 2] = r.choice((0, 1, 2))
                if r.random() < 0.01:
                    p[i + 1] = r.randrange(0, 12)
        elif tag == b"STHF":
            for i in range(0, len(p) - 3, 4):
                n = p[i + 1]
                if p[i] == 8 and n < 40 and 14 + STHF_SLOT[n] in words:
                    p[i + 2] = words[14 + STHF_SLOT[n]]
                if r.random() < 0.01:
                    p[i + 1] = r.randrange(0, 44)
                if r.random() < 0.01:
                    p[i + 3] = r.randrange(0, 4)
        elif tag == b"STSA" and r.random() < 0.3:
            p[0:4] = struct.pack(">I", r.choice((0, 1, r.randrange(1 << 20), r.randrange(1 << 32))))


def damage(r, cs, blob):
    kind = r.randrange(9)
    if kind == 0:
        r.shuffle(cs)
    elif kind == 1 and cs:
        i = r.randrange(len(cs))
        copy = [cs[i][0], cs[i][1], bytearray(cs[i][2]), None]
        if copy[2]:
            copy[2][r.randrange(len(copy[2]))] = r.randrange(256)
        cs.insert(r.randrange(len(cs) + 1), copy)
    elif kind == 2 and cs:
        del cs[r.randrange(len(cs))]
    elif kind == 3 and cs:
        c = cs[r.randrange(len(cs))]
        c[3] = max(0, len(c[2]) + r.choice((-16, -4, -1, 1, 4, 16, 1 << 20, 0x7fffffff, 0x80000000)))
    elif kind == 4 and cs:
        c = cs[r.randrange(len(cs))]
        del c[2][r.randrange(len(c[2]) + 1):]
        if r.random() < 0.5:
            c[2] += bytes(r.randrange(256) for _ in range(r.choice((1, 3, 4, 16))))
    elif kind == 5:
        payload = bytearray(r.randrange(256) for _ in range(r.choice((0, 4, 8, 12, 236, 240, 244))))
        cs.insert(r.randrange(len(cs) + 1), [b"STSA", MAGIC + VERSION, payload, None])
    elif kind == 6:
        size = r.choice((0, 11, 12, 16, 24, 40))
        payload = bytearray(r.randrange(256) for _ in range(size))
        if size >= 12 and r.random() < 0.7:
            payload[0:4] = struct.pack(">I", r.choice((0, 4, size - 12, size, 0xfffffff4, 0xfffffffb)))
        cs.insert(r.randrange(len(cs) + 1), [b"ELYR", MAGIC + VERSION, payload, None])
    elif kind == 7:
        tag = r.choice((b"\0\0\0\0", b"ABCD", b"GVNM", b"STNM", b"VNDM", b"STMM"))
        payload = bytearray(r.randrange(256) for _ in range(r.choice((0, 6, 8, 10))))
        cs.insert(r.randrange(len(cs) + 1), [tag, MAGIC + VERSION, payload, None])
    else:
        head = bytearray(MAGIC + VERSION)
        head[r.randrange(12)] ^= 0x20
        if cs:
            cs[r.randrange(len(cs))][1] = bytes(head)
    out = join(cs)
    if r.random() < 0.15:
        out = out[:r.randrange(len(out) + 1)]
    return out


def gen(sample, model_path, out, count=3000, seed=1):
    base = open(sample, "rb").read()
    scale, centroids = model(model_path)
    r = random.Random(seed)
    with open(out, "wb") as f:
        for i in range(count):
            cs = chunks(base)
            features(r, cs, scale, centroids)
            blob = damage(r, cs, base) if r.random() < 0.3 else join(cs)
            f.write(struct.pack("<I", len(blob)) + blob)


def harness_line(line):
    fields = dict(part.split("=", 1) for part in line.split() if "=" in part)
    feat = next(v for k, v in fields.items() if k.startswith("feat")).split(",")[4:]
    return "ch=%s extra=%s sabi=%s feat=%s" % (fields["ch"], fields["extra"], fields["sabi"], ",".join(feat))


def compare(harness, port):
    a = [harness_line(l) for l in open(harness) if l.startswith("ch=")]
    b = [l.strip() for l in open(port) if l.startswith("ch=")]
    if len(a) != len(b):
        sys.exit("line counts differ: %d vs %d" % (len(a), len(b)))
    bad = [i for i in range(len(a)) if a[i] != b[i]]
    for i in bad[:10]:
        print("blob %d\n  sony %s\n  port %s" % (i, a[i], b[i]))
    seen = {}
    for line in a:
        mask = line.split()[0]
        seen[mask] = seen.get(mask, 0) + 1
    clusters = {line.split()[1] for line in a}
    print("%d blobs, %d differ; %d channel sets, %d clusters" % (len(a), len(bad), len(seen), len(clusters)))
    return not bad


if __name__ == "__main__":
    if sys.argv[1] == "gen":
        gen(sys.argv[2], sys.argv[3], sys.argv[4], *map(int, sys.argv[5:7]))
    elif sys.argv[1] == "compare":
        sys.exit(0 if compare(sys.argv[2], sys.argv[3]) else 1)
    else:
        sys.exit(__doc__)
