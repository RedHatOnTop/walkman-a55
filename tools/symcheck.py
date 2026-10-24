"""Check that an ARM ELF would link on the device: every undefined dynamic
symbol (with its version) must be exported by one of the libraries it NEEDs,
looked up in the device's own library directories.

    symcheck.py ELF LIBDIR [LIBDIR ...]

Exit status 0 when nothing is missing. Weak undefined symbols are reported
but do not count as missing.
"""
import os
import sys

from elftools.elf.dynamic import DynamicSection
from elftools.elf.elffile import ELFFile
from elftools.elf.gnuversions import GNUVerDefSection, GNUVerNeedSection, GNUVerSymSection


def find_lib(name, dirs):
    for d in dirs:
        p = os.path.join(d, name)
        if os.path.exists(p):
            return os.path.realpath(p)
    return None


def needed(elf):
    for s in elf.iter_sections():
        if isinstance(s, DynamicSection):
            return [t.needed for t in s.iter_tags() if t.entry.d_tag == "DT_NEEDED"]
    return []


def versions(elf):
    """Map versym index -> version name, from verdef and verneed."""
    names = {}
    for s in elf.iter_sections():
        if isinstance(s, GNUVerDefSection):
            for vd, auxs in s.iter_versions():
                names[vd["vd_ndx"]] = next(auxs).name
        elif isinstance(s, GNUVerNeedSection):
            for _vn, auxs in s.iter_versions():
                for a in auxs:
                    names[a["vna_other"]] = a.name
    return names


def dynsyms(elf):
    dynsym = elf.get_section_by_name(".dynsym")
    versym = next((s for s in elf.iter_sections() if isinstance(s, GNUVerSymSection)), None)
    vnames = versions(elf)
    for i, sym in enumerate(dynsym.iter_symbols()):
        if not sym.name:
            continue
        ver = None
        if versym is not None:
            ndx = versym.get_symbol(i)["ndx"]
            if isinstance(ndx, int) and ndx > 1:
                ver = vnames.get(ndx & 0x7FFF)
        yield sym, ver


def exports(path):
    out = set()
    with open(path, "rb") as f:
        elf = ELFFile(f)
        for sym, ver in dynsyms(elf):
            if sym["st_shndx"] != "SHN_UNDEF":
                out.add((sym.name, ver))
                out.add((sym.name, None))
    return out


def main(target, *dirs):
    with open(target, "rb") as f:
        elf = ELFFile(f)
        libs = needed(elf)
        undef = [(s.name, v, s["st_info"]["bind"]) for s, v in dynsyms(elf) if s["st_shndx"] == "SHN_UNDEF"]

    provided = set()
    missing_libs = []
    for name in libs:
        path = find_lib(name, dirs)
        if path is None:
            missing_libs.append(name)
            continue
        print(f"NEEDED {name} -> {path}")
        provided |= exports(path)

    missing = [(n, v) for n, v, b in undef if b != "STB_WEAK" and (n, v) not in provided]
    weak = [(n, v) for n, v, b in undef if b == "STB_WEAK" and (n, v) not in provided]
    for name in missing_libs:
        print(f"MISSING LIBRARY {name}")
    for n, v in missing:
        print(f"MISSING {n}{'@' + v if v else ''}")
    print(f"{len(undef)} undefined, {len(missing)} missing, {len(weak)} weak unresolved, "
          f"{len(missing_libs)} libraries not found")
    sys.exit(1 if missing or missing_libs else 0)


if __name__ == "__main__":
    main(*sys.argv[1:])
