# Reflash recovery path — what exists for NW-A55 (MT8590), and the gap

2026-10-14. Goal: before any risky `bootimg`-level work (the `NR_CPUS=4` kernel for core
revival), establish a reflash recovery net so a bad flash is survivable. Web + community
survey, read-only, no device touched.

## Two recovery layers, only the first is established

**Layer 1 — Sony UPG / firmware (ESTABLISHED; it is our existing path).** Every community
tool operates here, not below it:
- **WLKMN Studio** (github Dexxzy/wlkmnstudio): app-level only. Its "Bootloop Recovery"
  re-installs a known-good *player app*. Explicitly: "Deeper trouble, like a bad partition or
  full-firmware flash, is outside its scope and needs re-flashing firmware." Needs the device
  reachable over USB (so it must still boot). mtk-tools/mtkimg are cited only as image *format*
  references, not flashing.
- **SonyWalkmanFirmwarePatcher** (github roobscoob): unpacks → modify → repacks a UPG, then
  injects via Sony's `SoftwareUpdateTool.exe`. UPG pack/unpack is portable (NW-A50 listed), but
  **injection only works on NW-ZX300**, not A50/A55. So for the A55 the UPG-application path is
  Sony's own mechanism — which for us is `scsitool do_fw_upgrade` + `nwupg.py` (see
  `docs/DEVICE-STEPS.md`), the equivalent injection trigger.
- **Known-good restore baseline:** Sony official firmware **1.02** (Aug 2019; NW-A55/A56HN/A57).
  Cannot revert below it. This is our stock restore image; we hold `backup-upg/NW_WM_FW.UPG`.

Layer 1 recovers bad *content* flashed through the UPG mechanism — **as long as the device
still boots enough to trigger `do_fw_upgrade` (or enter Sony update mode).**

**Layer 2 — MediaTek BROM / preloader download mode (NOT established for MT8590).** This is the
layer that would survive a bad *bootimg* (lk/kernel) where the device no longer boots to reach
the UPG trigger. The generic MTK method exists — enter BROM (hardware button combo, or mtkclient
"crash preloader to BROM"), then SP Flash Tool with the model's **scatter + DA (download agent)**,
optionally mtkclient to bypass auth — but **nothing MT8590-specific turned up**: no MT8590 in
mtkclient context, no NW-A55 scatter/DA, and MT8590 is 2014-era/obscure. All found guides are for
newer Xiaomi/etc. parts with auth files that will not apply.

## Consequence — the core-revival gate is NOT yet satisfied
"bootimg only after a recovery path exists" is not met by Layer 1 alone: Layer 1 does not protect
against a bad bootimg (if the kernel/lk is broken the device can't reach the UPG trigger). Before
any bootimg flash we need one of:
1. **Confirm Layer 2 exists for MT8590** — does the device expose a BROM/preloader USB download
   mode, and can we obtain/build a scatter + DA for MT8590? (biggest unknown), OR
2. **Confirm the UPG itself reflashes boot/kernel** — if a stock Sony UPG (`do_fw_upgrade`)
   rewrites the boot/lk/kernel partitions, then Layer 1 *is* a bootimg recovery, provided the
   device still boots to trigger it. A corrupt kernel that still lets lk+recovery reach the UPG
   trigger would then be recoverable.

## Next (read-only, no device)
Analyze our existing `backup-upg/NW_WM_FW.UPG` with `nwupg.py` to list **which partitions the UPG
covers**. If it includes boot/lk/kernel → Layer 1 doubles as bootimg recovery (with the
boots-enough caveat), materially lowering the gate. If it is userland-only → we genuinely need
Layer 2 (BROM) before touching bootimg, and that becomes the prerequisite research.

Sources: github Dexxzy/wlkmnstudio, github roobscoob/SonyWalkmanFirmwarePatcher, Sony FW 1.02
(support id 00016314/00016315), generic MTK BROM/SP-Flash-Tool unbrick guides (hovatek, xdaforums).
See also [[docs/DEVICE-STEPS.md]], the `scsitool do_fw_upgrade` trigger, `nwupg.py`.

## UPDATE (2026-10-14, later) — unpacked the UPGs: it IS a bootimg flasher

Corrects the implication above that the UPG layer is userland-only. Unpacked our UPGs with
`nwupg.py check` (they are AES V2 containers; a UPG = a short file list, `file0` an installer
shell script run as root, then payload files — no partition-name metadata in the container
itself).

- **`backup-upg/stock/NW_WM_FW.UPG`** (16 KB, standard Sony key — `-w` sig MISMATCH) is **our own**
  read-only "NW-A50 backup + device survey" script (comment: "NEVER writes to any partition"),
  not Sony firmware. So the small "stock" UPGs here are our tooling, not a full image.
- **`work/testvec-w1/x/NW_WM_FW.UPG`** (3 MB, Walkman One key) `file0` **IS the real flasher**:
  `UBOOT_PARTITION_NAME=uboot`, `COMMON_KERNEL_PARTITION_NAME=bootimg`,
  `RECOVERY_KERNEL_PARTITION_NAME=recovery`, `SYSTEM_PARTITION_NAME=android`; it `dd`s the
  bootimg block device out, runs `abootimg -x/-u` to rebuild the boot image (kernel+initrd), and
  writes it back (`updateBootImg()`), gated by `fwpchk`. Payloads are ARM ELFs + a tar.

**So the UPG mechanism DELIVERS a kernel/bootimg** (via abootimg+dd, the unknown321/nw-installer
path) — but from a **booted** device, so it is a delivery path, not a bricked-device recovery.

**Partition map captured:** `work/device/backup-20261010/dumchar_info.txt` + the full name list in
`docs/phase0-architecture.md:532` (uboot, bootimg, recovery, sec_ro, logo, tee1, tee2, android, …;
`/proc/dumchar_info` format `%-10s 0x%016llx 0x%016llx 2 /dev/block/mmcblk0p%d`).

**Recovery-net candidate — the `recovery` partition.** `docs/ROADMAP.md:19` already notes the
updater ramdisk lives in the `recovery` partition. So if we ever flash **only `bootimg`** (never
`recovery`/`uboot`/`tee`), a bad kernel could be recoverable by entering recovery/update mode and
re-running a stock UPG. This converges with the EXISTING open ROADMAP items, which are now the
critical path for core revival:
- `docs/ROADMAP.md:20` — **LK (uboot) analysis: update-mode entry conditions, key combo, and
  whether LK verifies the kernel signature.** The signature check is decisive: if LK verifies the
  bootimg signature, a custom `NR_CPUS=4` kernel is rejected outright → core revival via bootimg
  is blocked without a signature bypass or unlocked LK.
- `docs/phase0-architecture.md:745` Q30 — where the updater rootfs is and how to enter recovery.

**Refined gate status:** kernel DELIVERY = established (UPG+abootimg, booted device); RECOVERY net
= promising (recovery partition + UPG reinstall) but GATED on LK analysis; deeper MTK-BROM net =
still unverified (last resort if recovery is also compromised). **Next recovery/core step = extract
and analyze the `uboot` (LK) partition** (update-mode entry + signature verification) — the
existing ROADMAP item, now blocking core revival. See [[2026-10-14-core-revival-tee-recheck]].

## CORRECTION (2026-10-14, later) — MT8590 BROM recovery looks MORE viable than concluded above

The "Layer 2 (BROM) — nothing MT8590-specific turned up" conclusion was from an under-search.
The AOSP feasibility desk study ([[2026-10-14-aosp-feasibility]]) found MT8590-specific tooling:
mtkclient carries an `0x8590` config, and unknown321's `wbrt` / `mediatek_flash_tool` target
MT8590 Walkmans incl. the NW-A50 series; a Wampy `BACKUP.md` shows a real A50 **BROM session**
(entry = Vol-Down + Play + USB with the device off) with SBC/SLA/DAA all False and a full-eMMC
dump. That log is a *sibling* unit, not ours — so BROM recovery on OUR unit is still unconfirmed
and needs the on-device entry test (gated). **Locally VERIFIED corroboration:** our own
`work/device/backup-20261010/seccfg.img` is 131072 bytes of all zero → secure-boot config
(SBC/SLA/DAA) is disabled on THIS unit too, which is consistent with LK not enforcing a kernel
signature. Net: the recovery-net + custom-kernel path is more promising than this devlog first
concluded; the remaining unknowns are the on-device BROM entry and an offline `uboot.img`
disassembly to confirm LK does not verify the kernel.
