"""List every QObject class moc compiled into a stripped 32-bit ARM ELF, with
its properties, signals, slots and invokables (Qt 5.3, meta revision 7).

    mocdump.py ELF [--json OUT]

Each class has a staticMetaObject {superdata, stringdata, data, metacall, ...}
in .data.rel.ro. With REL relocations the link-time addresses sit in place,
so a staticMetaObject is any pair of words where the first points at a
QByteArrayData table (ref -1, alloc 0) and the next at a uint table starting
with revision 7. superdata is either another staticMetaObject in the binary
or a relocation against an imported one, which names the base class.
"""
import json
import re
import struct
import sys

from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

TYPES = {
    43: "void", 1: "bool", 2: "int", 3: "uint", 4: "qlonglong", 5: "qulonglong",
    6: "double", 32: "long", 33: "short", 34: "char", 35: "ulong", 36: "ushort",
    37: "uchar", 38: "float", 40: "signed char", 31: "void*", 7: "QChar",
    10: "QString", 11: "QStringList", 12: "QByteArray", 13: "QBitArray",
    14: "QDate", 15: "QTime", 16: "QDateTime", 17: "QUrl", 18: "QLocale",
    19: "QRect", 20: "QRectF", 21: "QSize", 22: "QSizeF", 23: "QLine",
    24: "QLineF", 25: "QPoint", 26: "QPointF", 27: "QRegExp", 29: "QEasingCurve",
    30: "QUuid", 41: "QVariant", 42: "QModelIndex", 44: "QRegularExpression",
    45: "QJsonValue", 46: "QJsonObject", 47: "QJsonArray", 48: "QJsonDocument",
    39: "QObject*", 8: "QVariantMap", 9: "QVariantList", 28: "QVariantHash",
    64: "QFont", 65: "QPixmap", 66: "QBrush", 67: "QColor", 68: "QPalette",
    69: "QIcon", 70: "QImage", 71: "QPolygon", 72: "QRegion", 73: "QBitmap",
    74: "QCursor", 75: "QKeySequence", 76: "QPen", 77: "QTextLength",
    78: "QTextFormat", 79: "QMatrix", 80: "QTransform", 81: "QMatrix4x4",
    82: "QVector2D", 83: "QVector3D", 84: "QVector4D", 85: "QQuaternion",
    86: "QPolygonF", 121: "QSizePolicy",
}
UNRESOLVED = 0x80000000
KIND = {0x00: "invokable", 0x04: "signal", 0x08: "slot", 0x0c: "constructor"}
ACCESS = {0: "private", 1: "protected", 2: "public"}
PROP_FLAGS = [(0x1, "R"), (0x2, "W"), (0x4, "reset"), (0x8, "enum"),
              (0x400, "const"), (0x800, "final"), (0x400000, "notify")]


class Image:
    def __init__(self, path):
        self.f = open(path, "rb")
        self.elf = ELFFile(self.f)
        self.segments = [(s["p_vaddr"], s["p_offset"], s["p_filesz"])
                         for s in self.elf.iter_segments() if s["p_type"] == "PT_LOAD"]
        self.blob = open(path, "rb").read()
        self.reloc_syms = {}
        for sec in self.elf.iter_sections():
            if isinstance(sec, RelocationSection):
                symtab = self.elf.get_section(sec["sh_link"])
                for r in sec.iter_relocations():
                    if r["r_info_sym"]:
                        self.reloc_syms[r["r_offset"]] = symtab.get_symbol(r["r_info_sym"]).name

    def off(self, addr):
        for va, fo, size in self.segments:
            if va <= addr < va + size:
                return fo + addr - va
        return None

    def u32(self, addr):
        o = self.off(addr)
        return None if o is None or o + 4 > len(self.blob) else struct.unpack_from("<I", self.blob, o)[0]

    def cstr(self, addr, limit=4096):
        o = self.off(addr)
        end = self.blob.index(b"\0", o, o + limit)
        return self.blob[o:end].decode("utf-8", "replace")


def string_table(img, addr):
    """Decode a moc QByteArrayData table, or None."""
    out = []
    for i in range(100000):
        a = addr + 16 * i
        ref, size, alloc, offset = (img.u32(a + k) for k in (0, 4, 8, 12))
        if ref != 0xffffffff or alloc != 0 or offset is None:
            break
        offset = offset - (1 << 32) if offset & 0x80000000 else offset
        o = img.off(a + offset)
        if o is None or img.blob[o + size:o + size + 1] != b"\0":
            break
        out.append(img.blob[o:o + size].decode("utf-8", "replace"))
    return out or None


def meta_data(img, addr):
    head = [img.u32(addr + 4 * i) for i in range(14)]
    if None in head or head[0] != 7:
        return None
    return head


def type_name(t, strings):
    if t & UNRESOLVED:
        return strings[t & 0x7fffffff]
    return TYPES.get(t, f"<metatype {t}>")


def decode(img, strings, data_addr):
    d = lambda i: img.u32(data_addr + 4 * i)
    _, cls, ci_count, ci_idx, m_count, m_idx, p_count, p_idx, e_count, e_idx, \
        c_count, c_idx, flags, sig_count = (d(i) for i in range(14))
    out = {"class": strings[cls], "classinfo": {}, "methods": [], "properties": [], "enums": []}
    for i in range(ci_count):
        out["classinfo"][strings[d(ci_idx + 2 * i)]] = strings[d(ci_idx + 2 * i + 1)]

    def methods(count, idx, ctor=False):
        for i in range(count):
            name, argc, params, tag, mflags = (d(idx + 5 * i + k) for k in range(5))
            ret = None if ctor else type_name(d(params), strings)
            base = params + (0 if ctor else 1)
            if ctor:
                base = params + 1
            types = [type_name(d(base + a), strings) for a in range(argc)]
            names = [strings[d(base + argc + a)] for a in range(argc)]
            yield {
                "kind": KIND[mflags & 0x0c], "access": ACCESS.get(mflags & 3, "?"),
                "name": strings[name], "return": ret,
                "args": [f"{t} {n}".strip() for t, n in zip(types, names)],
                "cloned": bool(mflags & 0x20),
            }

    out["methods"] = list(methods(m_count, m_idx))
    if c_count:
        out["methods"] += list(methods(c_count, c_idx, ctor=True))
    notify_base = p_idx + 3 * p_count
    has_notify = any(d(p_idx + 3 * i + 2) & 0x400000 for i in range(p_count))
    for i in range(p_count):
        name, ptype, pflags = (d(p_idx + 3 * i + k) for k in range(3))
        prop = {"name": strings[name], "type": type_name(ptype, strings),
                "flags": [s for bit, s in PROP_FLAGS if pflags & bit]}
        if has_notify and pflags & 0x400000:
            sig = d(notify_base + i)
            prop["notify"] = out["methods"][sig]["name"] if sig < len(out["methods"]) else sig
        out["properties"].append(prop)
    for i in range(e_count):
        name, eflags, count, edata = (d(e_idx + 4 * i + k) for k in range(4))
        keys = {strings[d(edata + 2 * k)]: d(edata + 2 * k + 1) for k in range(count)}
        out["enums"].append({"name": strings[name], "flag": bool(eflags & 1), "keys": keys})
    return out


def demangle_smo(sym):
    m = re.fullmatch(r"_ZN(.+)16staticMetaObjectE", sym or "")
    if not m:
        return sym
    s, parts = m.group(1), []
    while s:
        n = re.match(r"\d+", s)
        if not n:
            return sym
        k = int(n.group())
        parts.append(s[n.end():n.end() + k])
        s = s[n.end() + k:]
    return "::".join(parts)


def find_classes(img):
    found = {}
    for sec in img.elf.iter_sections():
        if sec.name not in (".data.rel.ro", ".data"):
            continue
        start, size = sec["sh_addr"], sec["sh_size"]
        for a in range(start, start + size - 12, 4):
            sd, md = img.u32(a + 4), img.u32(a + 8)
            if not md or meta_data(img, md) is None:
                continue
            strings = string_table(img, sd)
            if not strings:
                continue
            try:
                found[a] = decode(img, strings, md)
            except (IndexError, KeyError, TypeError):
                continue
    for a, c in found.items():
        sup = img.u32(a)
        if sup in found:
            c["super"] = found[sup]["class"]
        elif a in img.reloc_syms:
            c["super"] = demangle_smo(img.reloc_syms[a])
        else:
            c["super"] = None
    return sorted(found.values(), key=lambda c: c["class"])


def render(c):
    lines = [f"class {c['class']}" + (f" : {c['super']}" if c["super"] else "")]
    for k, v in c["classinfo"].items():
        lines.append(f"  classinfo {k} = {v}")
    for e in c["enums"]:
        lines.append(f"  enum {e['name']} {{ " + ", ".join(f"{k}={v}" for k, v in e["keys"].items()) + " }")
    for p in c["properties"]:
        extra = f" notify {p['notify']}" if "notify" in p else ""
        lines.append(f"  prop {p['type']} {p['name']} [{' '.join(p['flags'])}]{extra}")
    for m in c["methods"]:
        if m["cloned"]:
            continue
        ret = f"{m['return']} " if m["return"] else ""
        acc = "" if m["access"] == "public" else f" ({m['access']})"
        lines.append(f"  {m['kind']} {ret}{m['name']}({', '.join(m['args'])}){acc}")
    return "\n".join(lines)


def main(argv):
    path = argv[0]
    img = Image(path)
    classes = find_classes(img)
    if "--json" in argv:
        with open(argv[argv.index("--json") + 1], "w") as f:
            json.dump(classes, f, indent=1, ensure_ascii=False)
    for c in classes:
        print(render(c))
        print()
    print(f"# {len(classes)} classes", file=sys.stderr)


if __name__ == "__main__":
    main(sys.argv[1:])
