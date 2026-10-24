"""Quick ARM/Thumb disassembly of an ELF section or address range.

    disas.py ELF [--arm] [SECTION | START:END]

Calls into the PLT are annotated with the imported symbol name.
"""
import sys

import capstone
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection


def plt_names(elf):
    """Map PLT stub addresses to symbol names (ARM EABI: 12-byte stubs after a 20-byte header)."""
    plt = elf.get_section_by_name(".plt")
    rel = elf.get_section_by_name(".rel.plt")
    if not plt or not isinstance(rel, RelocationSection):
        return {}
    syms = elf.get_section(rel["sh_link"])
    names = {}
    for i, r in enumerate(rel.iter_relocations()):
        names[plt["sh_addr"] + 20 + 12 * i] = syms.get_symbol(r["r_info_sym"]).name
    return names


def main(path, *args):
    arm = "--arm" in args
    args = [a for a in args if a != "--arm"]
    elf = ELFFile(open(path, "rb"))
    target = args[0] if args else ".text"
    if ":" in target:
        start, end = (int(x, 16) for x in target.split(":"))
    else:
        s = elf.get_section_by_name(target)
        start, end = s["sh_addr"], s["sh_addr"] + s["sh_size"]

    code = b""
    for seg in elf.iter_segments():
        if seg["p_type"] == "PT_LOAD" and seg["p_vaddr"] <= start < seg["p_vaddr"] + seg["p_filesz"]:
            off = start - seg["p_vaddr"]
            code = seg.data()[off:off + (end - start)]
            break

    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM if arm else capstone.CS_MODE_THUMB)
    md.skipdata = True
    names = plt_names(elf)
    for ins in md.disasm(code, start):
        note = ""
        if ins.mnemonic in ("bl", "blx", "b") and ins.op_str.startswith("#"):
            note = names.get(int(ins.op_str[1:], 16), "")
        print(f"{ins.address:08x}  {ins.mnemonic:8s} {ins.op_str}" + (f"   ; {note}" if note else ""))


if __name__ == "__main__":
    main(*sys.argv[1:])
