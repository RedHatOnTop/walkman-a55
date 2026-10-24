#!/usr/bin/env python3
"""Decode the stock kernel's real export tables and check a module against them.

    verify-stock-exports.py <vmlinux.bin> <kallsyms> [imports-file] [--gpl]

__ksymtab and __ksymtab_gpl are arrays of (value, name pointer) pairs. Each
entry's name is resolved from the binary and its value must equal a BUILT-IN
kallsyms address for that name: /proc/kallsyms also lists module definitions as
"[name]", and a module address must never validate a built-in export. Every
decoded entry must also have its marker at that entry address, so the two
directions are both checked.
"""
import sys


def die(msg):
    print("verify-stock-exports: " + msg, file=sys.stderr)
    sys.exit(1)


def load_syms(path):
    """{name: {'builtin': {addr,..}, 'module': {addr,..}}} from a kallsyms dump.

    A module row is "[name]" in the last column; those addresses are kept apart
    so they can never satisfy a built-in export check.
    """
    syms = {}
    try:
        with open(path, errors="replace") as fh:
            for line in fh:
                p = line.replace("\r", "").split()
                if len(p) < 3:
                    continue
                try:
                    addr = int(p[0], 16)
                except ValueError:
                    continue
                # kallsyms columns: addr type name [module]. The module, when
                # present, is the FOURTH field; p[2] stays the symbol name.
                name, is_mod = p[2], len(p) >= 4 and p[3].startswith("[")
                rec = syms.setdefault(name, {"builtin": set(), "module": set()})
                rec["module" if is_mod else "builtin"].add(addr)
    except OSError as exc:
        die("cannot read %s: %s" % (path, exc))
    if not syms:
        die("no symbols parsed from %s" % path)
    return syms


def read_blob(path):
    try:
        with open(path, "rb") as fh:
            return fh.read()
    except OSError as exc:
        die("cannot read %s: %s" % (path, exc))


def builtin(syms, name):
    """The built-in address for a name, or None when absent or ambiguous."""
    rec = syms.get(name)
    if not rec or not rec["builtin"]:
        return None
    return None if len(rec["builtin"]) > 1 else next(iter(rec["builtin"]))


def check_bounds(syms, start_name, stop_name):
    s, e = builtin(syms, start_name), builtin(syms, stop_name)
    if s is None or e is None:
        die("kallsyms has no unambiguous %s/%s" % (start_name, stop_name))
    if e <= s:
        die("%s is not before %s" % (start_name, stop_name))
    if (e - s) % 8:
        die("%s..%s is not a whole number of entries" % (start_name, stop_name))
    return s, e


def decode_table(blob, base, syms, start_name, stop_name):
    s, e = check_bounds(syms, start_name, stop_name)
    out, by_addr = {}, {}
    for a in range(s, e, 8):
        off = a - base
        if off < 0 or off + 8 > len(blob):
            die("entry at %#x is outside the image" % a)
        val = int.from_bytes(blob[off:off + 4], "little")
        namep = int.from_bytes(blob[off + 4:off + 8], "little")
        off2 = namep - base
        if off2 < 0 or off2 >= len(blob):
            die("name pointer %#x at %#x is outside the image" % (namep, a))
        end = blob.find(b"\0", off2, off2 + 256)
        if end < 0:
            die("unterminated name at %#x" % namep)
        try:
            name = blob[off2:end].decode("ascii")
        except UnicodeDecodeError:
            die("non-ascii name at %#x" % namep)
        if not name:
            die("empty name at %#x" % a)
        if name in out and out[name] != val:
            die("contradictory duplicate %s: %#x vs %#x" % (name, out[name], val))
        out[name] = val
        by_addr[a] = name
    return out, by_addr


def check_values(syms, plain, gpl):
    for table in (plain, gpl):
        for name, val in table.items():
            want = syms.get(name, {}).get("builtin")
            if not want:
                die("decoded export %s has no unambiguous built-in kallsyms definition"
                    % name)
            if val == 0:
                die("decoded export %s has a zero value" % name)
            if val not in want:
                mods = syms.get(name, {}).get("module", set())
                extra = " (module addresses %s)" % ",".join(
                    "%#x" % m for m in sorted(mods)) if mods else ""
                die("decoded export %s has value %#x, built-in kallsyms says %s%s"
                    % (name, val, "/".join("%#x" % w for w in sorted(want)), extra))


def check_markers(syms, plain_addr, gpl_addr):
    """Both directions: every marker names its entry, every entry has a marker."""
    entry2name = dict(plain_addr)
    entry2name.update(gpl_addr)
    outside, module_markers = [], []  # outside = built-in, must be empty
    for name, rec in syms.items():
        if not name.startswith("__ksymtab_"):
            continue
        short = name
        for p in ("__ksymtab_gpl_future_", "__ksymtab_gpl_", "__ksymtab_"):
            if short.startswith(p):
                short = short[len(p):]
                break
        for addr in rec["builtin"]:
            got = entry2name.get(addr)
            if got is None:
                outside.append("%s@%#x" % (name, addr))
            elif got != short:
                die("marker %s at %#x decodes to %s" % (name, addr, got))
        for addr in rec["module"]:
            module_markers.append("%s@%#x" % (name, addr))
    for addr, nm in entry2name.items():
        rec = syms.get("__ksymtab_" + nm) or syms.get("__ksymtab_gpl_" + nm)
        if not rec or addr not in rec["builtin"]:
            die("decoded export %s at %#x has no marker" % (nm, addr))
    return outside, module_markers


def check_imports(imports, plain, gpl, gpl_ok):
    missing = []
    for sym in imports:
        if sym in plain:
            continue
        if sym in gpl and gpl_ok:
            continue
        if sym in gpl:
            missing.append(sym + " (GPL-only, module is not GPL)")
        else:
            missing.append(sym + " (not exported by the stock kernel)")
    if missing:
        for m in missing:
            print("MISSING " + m, file=sys.stderr)
        die("%d of %d imports are not satisfied" % (len(missing), len(imports)))


def main(argv):
    if len(argv) < 3:
        die("usage: verify-stock-exports.py <vmlinux.bin> <kallsyms> [imports] [--gpl]")
    blob, syms = read_blob(argv[1]), load_syms(argv[2])
    base = builtin(syms, "_text")
    if base is None:
        die("kallsyms has no unambiguous _text")

    plain, plain_addr = decode_table(blob, base, syms,
                                     "__start___ksymtab", "__stop___ksymtab")
    gpl, gpl_addr = decode_table(blob, base, syms,
                                 "__start___ksymtab_gpl", "__stop___ksymtab_gpl")
    if not plain and not gpl:
        die("no exports decoded")

    check_values(syms, plain, gpl)
    outside, module_markers = check_markers(syms, plain_addr, gpl_addr)
    if outside:
        for o in outside:
            print("built-in marker outside the tables: " + o, file=sys.stderr)
        die("%d built-in markers are outside the export tables" % len(outside))
    print("decoded %d EXPORT_SYMBOL and %d EXPORT_SYMBOL_GPL, %d built-in markers "
          "outside the tables, %d module markers"
          % (len(plain), len(gpl), len(outside), len(module_markers)))
    for o in outside:
        print("built-in marker outside the tables: " + o, file=sys.stderr)
    for m in module_markers[:5]:
        print("module marker (reported, not used): " + m, file=sys.stderr)

    imports, gpl_ok = [], "--gpl" in argv
    for a in argv[3:]:
        if a == "--gpl":
            continue
        try:
            with open(a) as fh:
                rows = [l.strip() for l in fh if l.strip()]
        except OSError as exc:
            die("cannot read imports file %s: %s" % (a, exc))
        if not rows:
            die("imports file %s has no symbols" % a)
        imports += rows
    check_imports(imports, plain, gpl, gpl_ok)
    if imports:
        print("all %d imports are satisfied by the stock kernel" % len(imports))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
