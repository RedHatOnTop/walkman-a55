"""Pack a directory into a binary Qt resource file in rcc format v1, which
Qt 5.3's QResource::registerResource() and the wmprobe overlay read.

    mkrcc.py DIR [DIR ...] OUT.rcc

DIR/a/b.qml is served as qrc:/a/b.qml. With several DIRs a file in a later
one replaces the same path in an earlier one. Files are stored uncompressed.
Layout follows rcc -binary: a 20-byte header ("qres", version, tree, data
and name offsets, big-endian), then the data, name and tree tables.
"""
import os
import struct
import sys

DIRECTORY = 2
LANG_C, ANY_COUNTRY = 1, 0


def qt_hash(name):
    h = 0
    for c in name:
        h = ((h << 4) + ord(c)) & 0xffffffff
        h ^= (h & 0xf0000000) >> 23
        h &= 0x0fffffff
    return h


def scan(path):
    entries = {}
    for name in sorted(os.listdir(path)):
        full = os.path.join(path, name)
        entries[name] = scan(full) if os.path.isdir(full) else full
    return entries


def merge(base, top):
    for name, value in top.items():
        if isinstance(value, dict) and isinstance(base.get(name), dict):
            merge(base[name], value)
        else:
            base[name] = value
    return base


def pack(root_dirs):
    data, names = bytearray(), bytearray()
    name_offsets = {}

    def name_off(name):
        if name not in name_offsets:
            name_offsets[name] = len(names)
            utf16 = name.encode("utf-16-be")
            names.extend(struct.pack(">HI", len(name), qt_hash(name)) + utf16)
        return name_offsets[name]

    # Breadth first: a directory's children are contiguous and sorted by hash,
    # which QResourceRoot::findNode() binary-searches.
    nodes = [None]
    merged = {}
    for root_dir in root_dirs:
        merge(merged, scan(root_dir))
    queue = [(0, merged)]
    root_children = None
    while queue:
        index, entries = queue.pop(0)
        children = sorted(entries.items(), key=lambda kv: qt_hash(kv[0]))
        first = len(nodes)
        if index == 0:
            root_children = (len(children), first)
        else:
            nodes[index] = nodes[index][:2] + (len(children), first)
        for name, value in children:
            child = len(nodes)
            if isinstance(value, dict):
                nodes.append((name_off(name), DIRECTORY, 0, 0))
                queue.append((child, value))
            else:
                with open(value, "rb") as f:
                    payload = f.read()
                nodes.append((name_off(name), 0, len(data)))
                data.extend(struct.pack(">I", len(payload)) + payload)

    tree = bytearray(struct.pack(">IHII", 0, DIRECTORY, *root_children))
    for node in nodes[1:]:
        if node[1] & DIRECTORY:
            tree += struct.pack(">IHII", *node)
        else:
            tree += struct.pack(">IHHHI", node[0], node[1], ANY_COUNTRY, LANG_C, node[2])

    data_off = 20
    names_off = data_off + len(data)
    tree_off = names_off + len(names)
    header = b"qres" + struct.pack(">IIII", 1, tree_off, data_off, names_off)
    return header + bytes(data) + bytes(names) + bytes(tree)


def main(*args):
    *srcs, out = args
    blob = pack(srcs)
    with open(out, "wb") as f:
        f.write(blob)
    print(f"{out}: {len(blob)} bytes")


if __name__ == "__main__":
    main(*sys.argv[1:])
