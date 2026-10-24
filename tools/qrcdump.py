"""Find the Qt resources (rcc format v1, Qt 5.3) compiled into a binary, or
stored in a binary .rcc file, and dump them as files.

    qrcdump.py BINARY OUTDIR

rcc emits three tables per .qrc, all big-endian: a tree of 14-byte nodes,
then the name table, then the data table, back to back. A tree is found by
its root node (name offset 0, directory, first child 1); the name table must
then start right after the last node and every name must match its
qt_hash(), and the data table must start right after the last name with
payloads stored back to back as (u32 size, bytes). probe/mkspec/overlay.cpp
finds the app's resources in memory the same way. Prints one line per file.
"""
import os
import re
import struct
import sys
import zlib

COMPRESSED, DIRECTORY = 1, 2


def qt_hash(name):
    h = 0
    for c in name:
        h = ((h << 4) + ord(c)) & 0xffffffff
        h ^= (h & 0xf0000000) >> 23
        h &= 0x0fffffff
    return h


def name_at(blob, pos):
    """(name, end) of a valid name entry at pos, else None."""
    if pos + 6 > len(blob):
        return None
    n, h = struct.unpack_from(">HI", blob, pos)
    end = pos + 6 + 2 * n
    if n == 0 or end > len(blob):
        return None
    try:
        name = blob[pos + 6:end].decode("utf-16-be")
    except UnicodeDecodeError:
        return None
    return (name, end) if qt_hash(name) == h else None


def read_tree(blob, tree):
    """{index: (flags, a, b, name_off)} for every node reachable from the
    root, or None if it does not look like a tree."""
    nodes, todo = {}, [0]
    while todo:
        i = todo.pop()
        if i in nodes or len(nodes) > 100000:
            return None
        pos = tree + 14 * i
        if pos + 14 > len(blob):
            return None
        name_off, flags = struct.unpack_from(">IH", blob, pos)
        if flags & ~(COMPRESSED | DIRECTORY):
            return None
        if flags & DIRECTORY:
            count, child = struct.unpack_from(">II", blob, pos + 6)
            if count > 10000 or child == 0:
                return None
            todo.extend(range(child, child + count))
            nodes[i] = (flags, count, child, name_off)
        else:
            country, lang, data_off = struct.unpack_from(">HHI", blob, pos + 6)
            nodes[i] = (flags, country, data_off, name_off)
    return nodes


MAX_PADDING = 64


def names_end(blob, names, nodes):
    end = names
    for i, n in nodes.items():
        if i == 0:
            continue
        entry = name_at(blob, names + n[3])
        if entry is None:
            return None
        end = max(end, entry[1])
    return end


def fits_data(blob, data, nodes):
    offsets = sorted({n[2] for n in nodes.values() if not n[0] & DIRECTORY})
    for a, b in zip(offsets, offsets[1:]):
        if data + a + 4 > len(blob) or struct.unpack_from(">I", blob, data + a)[0] != b - a - 4:
            return False
    if offsets:
        last = data + offsets[-1]
        if last + 4 > len(blob) or last + 4 + struct.unpack_from(">I", blob, last)[0] > len(blob):
            return False
    return True


def padded(blob, start):
    """start, then each later position reached over zero bytes only."""
    for pos in range(start, min(start + MAX_PADDING, len(blob))):
        yield pos
        if blob[pos]:
            return


def check(blob, tree, nodes, names=None, data=None):
    """Locate (names, data) for a parsed tree, or None if they do not fit.
    Without offsets, the tables may sit behind zero alignment padding; the
    nearest fit wins, which is a guess only for a single-file data table."""
    for n in [names] if names is not None else padded(blob, tree + 14 * (max(nodes) + 1)):
        end = names_end(blob, n, nodes)
        if end is not None:
            break
    else:
        return None
    for d in [data] if data is not None else padded(blob, end):
        if fits_data(blob, d, nodes):
            return n, d
    return None


def find_resources(blob):
    if blob[:4] == b"qres":
        version, tree, data, names = struct.unpack_from(">IIII", blob, 4)
        nodes = read_tree(blob, tree)
        if version == 1 and nodes and check(blob, tree, nodes, names, data):
            yield tree, names, data, nodes
        return
    for m in re.finditer(rb"\x00\x00\x00\x00\x00\x02(?=.{4}\x00\x00\x00\x01)", blob, re.S):
        tree = m.start()
        nodes = read_tree(blob, tree)
        if not nodes or len(nodes) < 2:
            continue
        found = check(blob, tree, nodes)
        if found:
            yield (tree,) + found + (nodes,)


def dump(blob, tree, names, data, nodes, outdir):
    def walk(i, path):
        flags, a, b, name_off = nodes[i]
        name = "" if i == 0 else name_at(blob, names + name_off)[0]
        here = os.path.join(path, name) if name else path
        if flags & DIRECTORY:
            for c in range(b, b + a):
                walk(c, here)
            return
        size = struct.unpack_from(">I", blob, data + b)[0]
        payload = blob[data + b + 4:data + b + 4 + size]
        if flags & COMPRESSED:
            payload = zlib.decompress(payload[4:])
        rel = os.path.relpath(here, outdir)
        os.makedirs(os.path.dirname(here), exist_ok=True)
        with open(here, "wb") as f:
            f.write(payload)
        print(f"{len(payload):8d} {'z' if flags & COMPRESSED else '-'} {rel}")

    walk(0, outdir)


def main(binary, outdir):
    blob = open(binary, "rb").read()
    count = 0
    for tree, names, data, nodes in find_resources(blob):
        print(f"# tree {tree:#x} names {names:#x} data {data:#x}, {len(nodes)} nodes")
        dump(blob, tree, names, data, nodes, outdir)
        count += 1
    if not count:
        sys.exit("no resource tree found")


if __name__ == "__main__":
    main(*sys.argv[1:])
