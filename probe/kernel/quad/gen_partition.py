#!/usr/bin/env python3
"""Generate BBDMP5_linux/common/{partition_define.h,partition_define_private.h,pmt.h}
from the device's live /proc/dumchar_info, using an MTK reference board's common/ as the
format template. The stock extraction ships the reference boards (bx8590*) but not the
Sony BBDMP5_linux board's generated common/, so we rebuild it from device ground truth.

  python3 -I gen_partition.py <dumchar_info.txt> <reference_common_dir> <out_common_dir>

Names/sizes/offsets are exact (from dumchar_info). partition_idx comes from the MapTo
mmcblk0pN column. MBR_EBR_px is best-effort for the build; validate against the real
source before any flash.
"""
import os
import re
import sys


def parse_dumchar(path):
    parts = []
    for line in open(path):
        f = line.split()
        # rows look like: name  0xSIZE  0xSTART  TYPE  MapTo
        if len(f) < 5 or not f[1].startswith("0x"):
            continue
        name, size, start, _type, mapto = f[0], f[1], f[2], f[3], f[4]
        m = re.search(r"mmcblk0p(\d+)$", mapto)
        idx = int(m.group(1)) if m else 0
        region = "EMMC_PART_BOOT1" if name == "preloader" else "EMMC_PART_USER"
        parts.append(dict(name=name, size=int(size, 16), start=start, idx=idx, region=region))
    return parts


def gen_private(parts):
    rows = []
    for p in parts:
        rows.append('\t\t\t{"%s", %d, %s, EMMC, %d, %s},'
                    % (p["name"], p["size"], p["start"], p["idx"], p["region"]))
    # mbr + ebrN carry the logical partitions; map each ebr to the p-indices that
    # follow it on disk. Best-effort grouping of the 29 logical indices; the block
    # layer parses the real on-disk EBR chain, so this feeds MTK tools, not boot.
    logical = [p["idx"] for p in parts if p["idx"]]
    ebrs = ["mbr"] + [p["name"] for p in parts if p["name"].startswith("ebr")]
    groups, per = [], max(1, -(-len(logical) // len(ebrs)))
    for i, name in enumerate(ebrs):
        chunk = logical[i * per:(i + 1) * per]
        groups.append('\t{"%s", {%s}},' % (name, ", ".join(str(x) for x in chunk) + ("," if chunk else "")))
    body = "\n".join(rows)
    mbr = "\n".join(groups)
    return ('#include "partition_define.h"\n'
            "static const struct excel_info PartInfo_Private[PART_NUM]={\n"
            "%s\n };\n\n"
            "#ifdef  CONFIG_MTK_EMMC_SUPPORT\n"
            "struct MBR_EBR_struct MBR_EBR_px[MBR_COUNT]={\n%s\n};\n\n"
            "EXPORT_SYMBOL(MBR_EBR_px);\n"
            "#endif\n" % (body, mbr))


def patch_define(ref_text, n):
    # the array is sized [PART_NUM]; make it match our generated count
    return re.sub(r"#define\s+PART_NUM\s+\d+", "#define PART_NUM\t\t\t%d" % n, ref_text)


def main():
    dumchar, ref_dir, out_dir = sys.argv[1], sys.argv[2], sys.argv[3]
    parts = parse_dumchar(dumchar)
    os.makedirs(out_dir, exist_ok=True)
    ref_def = open(os.path.join(ref_dir, "partition_define.h")).read()
    open(os.path.join(out_dir, "partition_define.h"), "w").write(patch_define(ref_def, len(parts)))
    open(os.path.join(out_dir, "partition_define_private.h"), "w").write(gen_private(parts))
    # pmt.h is generic across boards
    open(os.path.join(out_dir, "pmt.h"), "w").write(open(os.path.join(ref_dir, "pmt.h")).read())
    print("wrote %d partitions, PART_NUM=%d -> %s" % (len(parts), len(parts), out_dir))
    for p in parts:
        print("  %-10s size=%-11d start=%-12s idx=%d" % (p["name"], p["size"], p["start"], p["idx"]))


if __name__ == "__main__":
    main()
