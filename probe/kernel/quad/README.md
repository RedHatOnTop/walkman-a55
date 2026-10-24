# quad-core kernel (CONFIG_NR_CPUS=4) — builds from Sony GPL source

Bring up all four Cortex-A7 cores. The MT8590 silicon is quad (L2CTLR `0x03800001` →
cluster of 4, proven by `probe/kmod/coreprobe`), but the stock Walkman kernel runs only
cpu0/1. Module-level revival is a dead end — the IN_HOUSE_TEE secure monitor owns the NS
reset vector (see `devlog/2026-10-16-core-revival.md`), so the only path is to rebuild the
kernel so cpu2/3 become first-class Linux cores, then flash it.

**Status (2026-10-16): the quad `zImage` builds clean** from the full Sony NW-A50-series GPL
source. Flash is still the gated hard blocker (see Deploy).

## Source

Build from the complete Sony GPL kernel, not the earlier module-build extraction. See
`SOURCE.md` for provenance/sha256. Fetched into `~/.cache/nw-kernel/sony-oss-nwa57/ksrc`
(3.10.26, MT8590, `CONFIG_ARCH_MTK_PROJECT="BBDMP5_linux"`). Cross: `~/.cache/arm-eabi-4.8`.

The full source ships the complete `arch/arm/mach-mt8590/BBDMP5_linux/` board tree (LCM,
sensors, kpd, headset, sound, …) that the old extraction lacked. It does **not** ship
`BBDMP5_linux/common/` (`partition_define*.h`); MTK generates that at build time and Sony
shipped only the reference boards' copies. We regenerate it from the live device (below).

## The three changes

### 1. `arch/arm/mach-mt8590/mt-smp.c` — `L2CTLR_get_core_count()`
`0001-mt-smp-L2CTLR-core-count.patch`. Stock reads devinfo **efuse** word 3 bits[27:24]
(`0x0→4, 0x8→3, else→2`); our unit is fused as a 2-core SKU, so `mt_smp_init_cpus` then
`spm_mtcmos_ctrl_cpu2/3(STA_POWER_DOWN)` and `set_cpu_possible(0..1)`. Patched to read the
real synthesized count from L2CTLR (the MRC upstream left commented out) → returns 4.

### 2. `.config` — `CONFIG_NR_CPUS` 2 → 4
With `ncores==4` and `NR_CPUS==4`: `4 > 4` is false (no clamp), the `ncores==2/3` power-down
branches are skipped, `set_cpu_possible(0..3)` runs. Does not change vermagic
(`3.10.26 SMP preempt mod_unload ARMv7`), so out-of-tree modules still load.

### 3. `arch/arm/mach-mt8590/mt_clkmgr.c` — define `md_power_on`/`md_power_off`
`0002-mt_clkmgr-define-md_power-stubs.patch`. This board's header hardcodes
`#define CLKMGR_8127 1`, so the stock `#if !CLKMGR_8127` drops these no-op stubs. But
`mtk_ccci_helper.c:internal_md_power_down()` calls them even in the no-modem (dummy CCCI)
build — its call sites are outside the `__USING_DUMMY_CCCI_API__` guard — so the vmlinux link
fails on undefined `md_power_on/off`. Define the stubs unconditionally (no modem on the
Walkman; the stock non-8127 stub is itself `return 0`). Unrelated to the quad work, but the
full-source link needs it.

## Build — OUT OF TREE (this is the key)

The MTK kernel must be built out-of-tree (`O=`). In-tree, kbuild does **not** add
`-I$(obj)` (`scripts/Makefile.lib`: `-I$(obj)` is only added when `KBUILD_SRC` is set), so
every `#include <same_dir_header>` — e.g. `mt65xx_lcm_list.c`'s `<lcm_drv_mt8127.h>` — fails.
Sony's `build.sh` builds with `O=out`; so do we. Build in-tree and it dies at the first
same-dir include.

```sh
KSRC=~/.cache/nw-kernel/sony-oss-nwa57/ksrc
OBJ=~/.cache/nw-kernel/sony-oss-nwa57/obj
CROSS=~/.cache/arm-eabi-4.8/bin/arm-eabi-

# mt_spm_sleep.c has a literal #include "../../../../kernel/drivers/.../mtk_ir_core.h",
# which assumes the tree is named "kernel" (Sony builds it as a dir called kernel):
ln -sfn ksrc ~/.cache/nw-kernel/sony-oss-nwa57/kernel

# regenerate BBDMP5_linux/common/ from the live device (see gen_partition.py):
python3 -I gen_partition.py <(adb -s <serial> shell cat /proc/dumchar_info) \
    $KSRC/arch/arm/mach-mt8590/bx8590m2_linux/common \
    $KSRC/arch/arm/mach-mt8590/BBDMP5_linux/common

# apply the patches:
patch -p1 -d $KSRC < 0001-mt-smp-L2CTLR-core-count.patch
patch -p1 -d $KSRC < 0002-mt_clkmgr-define-md_power-stubs.patch

make -C $KSRC O=$OBJ ARCH=arm CROSS_COMPILE=$CROSS BBDMP5_linux_debug_defconfig
$KSRC/scripts/config --file $OBJ/.config --set-val NR_CPUS 4
make -C $KSRC O=$OBJ ARCH=arm CROSS_COMPILE=$CROSS olddefconfig
make -C $KSRC O=$OBJ ARCH=arm CROSS_COMPILE=$CROSS -j2 zImage
# -> $OBJ/arch/arm/boot/zImage  (3.8 MB; verified built 2026-10-16, KBUILD_EXIT=0)
```

Build with `make` + gcc (not cargo), so Fedora is fine. End every build with
`; rc=$?; echo KBUILD_EXIT=$rc` and read that line — a backgrounded "exit 0" has masked a
make failure before. Build the tree you extracted fresh; an in-tree build pollutes it and
`make mrproper` can't clean it (the stock `gdma/mt8135` clean rule references a dir MT8590
doesn't ship), so out-of-tree from a pristine extraction is the clean path.

`BBDMP5_linux/common/` is generated from the device, not from Sony (not shipped). Offsets are
device ground truth (`/proc/dumchar_info`: bootimg @ `0x2180000`, 16 MB, `mmcblk0p8`);
`gen_partition.py` cross-checks them and `bx8590m2_linux/common/` is the format template.

## Deploy — GATED

Flashing a new kernel (repacked boot.img) is the project's gated hard blocker
([[nw-a55-container-reset]] "bootimg flash no"; [[nw-a55-aosp-feasibility]] kernel/BROM
replacement is the real open question). Do NOT flash without the operator's go.

The repacked image is ready: `repack_boot.py <stock_boot.img> <new_zImage> <out>` swaps only
the kernel payload, keeping the stock ramdisk blob byte-identical. The stock boot.img is
ANDROID! format (page 2048, kernel@0x10008000, ramdisk@0x11000000) with each payload
MTK-wrapped (magic `0x58881688` + "KERNEL"/"ROOTFS"); the tool re-wraps the new zImage, keeps
the ramdisk/addrs, and recomputes the mkbootimg SHA1 id. The header cmdline is empty on this
device (LK supplies `console=tty0 console=ttyMT3,921600n1 root=/dev/ram vmalloc=496M
slub_max_order=0 slub_debug=O`).

```sh
python3 -I repack_boot.py nwa50-extracted/kernel/boot.img \
    ~/.cache/nw-kernel/sony-oss-nwa57/obj/arch/arm/boot/zImage \
    ~/.cache/nw-kernel/sony-oss-nwa57/boot-quad.img
# built 2026-10-16: 7.17 MB (< 16 MB bootimg partition), round-trip verified
# (kernel payload == new zImage, ramdisk blob == stock).
```

Before flashing (`mmcblk0p8`, @0x2180000, 16 MB), verify the preloader/LK bootimg signature
check; `seccfg` (mmcblk0p6) read all-zero earlier ([[nw-a55-aosp-feasibility]]), suggesting
loose secure boot — confirm before flashing. The quad kernel only matters once flashed; the
container path runs the stock Sony kernel.
