# kernel source provenance (GPL)

The quad-core rebuild is built from Sony's **official** NW-A50-series GPL release, not from the
device dump. Sony distributes the sources for the NW-A55/56/57 under the NW-A57 product on its
open-source portal.

- Portal: <https://oss.sony.net/Products/Linux/Audio/NW-A57.html>
  (the page is a JS SPA; the file list links straight to Azure blob tarballs)
- Direct tarballs: `https://prodgpl.blob.core.windows.net/download/Audio/NW-A57/<name>.tar.gz`
- Fetched 2026-10-16 into `~/.cache/nw-kernel/sony-oss-nwa57/` (one dir per the untrusted-archive rule).

## sha256

```
3aef2bdcc4ecd43ab9fed0f038fa384e2515c58851573908dc8b1743febb1609  linux-kernel-3.10.26.tar.gz
da297aa03a71e67c206050b01d192e3816284f16b077a001233a7ed35f6091eb  icx_carddet.tar.gz
477e7e8cdfab2694b1613f5c2456a64ad89ee16277d80f03a67a746ddf262eba  icx_key.tar.gz
04aaf6ca75408faebf376e7086caabd44581baa3a2c5fadf13303f9a37e01397  icx_radio.tar.gz
a6f33fbaf027cf5a714809b9e97897047077acc7f579c94cd8aff724e212e9ff  icx_usbconn.tar.gz
936e17f668b3ddc9f2183606f56c1798282061bdc6ac8abc76443ce91edbc747  bluez-4.101.tar.gz
d4fe90e813d3b4ff674a33660e4b59082dc2f1ea27773ff77cd375ecb2d44f5e  alsa-lib-1.0.29.tar.gz
42f8888086b9cb41dcfe621478f120d7f543e8339b04308f4b01ceadad346add  alsa-utils-1.0.27.2.tar.gz
```

`linux-kernel-3.10.26` is the kernel (matches the device's own version exactly). `icx_*` are Sony's
out-of-tree Walkman drivers (SD card detect, keys, FM radio, USB connector). `bluez`/`alsa-*` are
pulled for the BT/audio work, not the kernel build.

## what this release ships vs. the earlier module-build tree

This full source carries the complete `arch/arm/mach-mt8590/BBDMP5_linux/` board tree (LCM, sensors,
kpd, headset, sound, touchpanel, power, rtc, …) that the earlier `~/.cache/nw-kernel/linux-kernel-3.10.26`
extraction was missing — so the LCM/sensor header cascade documented in `README.md` is gone.

The one thing **not** shipped is `BBDMP5_linux/common/` (`partition_define*.h`, `pmt.h`). MTK
generates those at build time from a partition table in the proprietary `mediatek/` tree, and Sony
ships only the reference boards' generated copies (bx8590*, evb8590, fpga8590), not the production
board's. There is no `ProjectConfig.mk`, scatter file, or ptgen script for it in the GPL drop. We
regenerate it from the live device (`gen_partition.py` against `/proc/dumchar_info`); see `README.md`.

## GPL compliance

Upstream copyright/license headers are kept intact. Our changes are the two recorded in `README.md`
(`mt-smp.c` L2CTLR read, `.config` NR_CPUS=4); they are GPL derivatives and distributed as patches
only. Per the project distribution policy, Sony's source and any firmware-derived assets stay
personal-use; only our original patches/tools are shareable.
