# Core revival — the "TEE route" was an inference, not a measurement

2026-10-14. Deep read-only recheck (kernel disassembly from the device bootimg +
source + the coreup dry-run values from the original session transcript). This
CORRECTS the overconfident `devlog/2026-10-11-wifi-soc-android.md:15` line
("기동 경로가 TEE를 거쳐서 … 멈췄다") and the memory that repeated it.

## What is actually measured vs inferred
- **Measured:** `SLAVE_JUMP_REG` (SRAMROM+0x34) reads **0x9e300168** — a TEE-side
  address in the top ~36 MB the bootloader reserves (System RAM is
  0x80000000-0x9dbfffff per /proc/iomem), **not** the `0x807b5ecc`
  (`mt_secondary_startup`) the kernel wrote at `mt-smp.c:364`. `BOOT_ADDR`
  (INFRACFG+0x800) reads 0 despite the kernel writing it at `mt-smp.c:200`. So
  non-secure writes to the jump/boot vector **do not stick** (dropped, hidden, or
  overwritten).
- **Inferred (not proven):** that a TEE owns/validates that vector. Plausible —
  the preloader loads `tee1/2.img` (encrypted, 8.0 bits/byte, `AGENTS.md:36`
  forbids touching it) and the boot-share mailbox (`NS_SLAVE_*` at phys
  0x80002000) exists for secure-side consumption — but the secure body can't be
  inspected statically, so "the TEE blocked it" remains an inference.

## The REAL hard blocker is NR_CPUS, not (only) the TEE
A RAM-only route to a **fully online Linux cpu2/cpu3 is impossible**: there is no
logical slot. `CONFIG_NR_CPUS=2` is compile-time (`.config:381`) with fixed-size
arrays (`setup.c:131,462`, `irq.c:830`); a module cannot create a third logical
CPU. **A kernel rebuild is a hard requirement** — necessary, but NOT shown
sufficient (TEE/fuse may still block).

## The efuse gate (kernel-side, ignorable)
`L2CTLR_get_core_count()` (`mt-smp.c:109-139`): the `L2CTLR` read is commented out
(111-121); the active path reads `get_devinfo_with_index(3)` → nibble [27:24] of
word 3 (0x0c2e1000) = 0xC → 2 cores. The kernel never reads the efuse hardware
itself — the value comes from the bootloader's `ATAG_DEVINFO_DATA`. So a rebuilt
kernel can **ignore it** (return 4, or restore the L2CTLR block, which yields
3+1=4 on this device's 0x03800001). If the fuse *also* gates the silicon, the
kernel can't change that — unknown.

## coreup's negative result had unexcluded confounds
The original "core did not run our stub" was real (first stub store is the 7th
insn, `stub.S:18`; mailbox stayed `0 0 0`), but the CAUSE was not isolated — the
session itself called it "판정 불가" (indeterminate). Confounds:
- coreup **never sent the bring-up SGI** that `mt_boot_secondary` does
  (`mt-smp.c:268`) — a *secure* SGI that wakes a TEE-parked core; may be required.
- Its debug-monitor readback used the **wrong register pair** (0xF020021c/0x14 =
  the Trustonic branch; this build is non-Trustonic, using 0xF0200080 + select
  cpu+8, `mt-smp.c:301-302`). Its `0x0000000f` is uninterpreted.
- **No positive control on cpu1** (never showed the method works on a live core).
- **Never sampled `MCUSYS_DBG_CORE2_PC`** (MCUSYS+0x320, `mt_mcu.h:74-76`) — the
  one read that directly shows whether/where the core fetched.

## Cheapest read-mostly tests that would settle it (device, gated; coreup was
## stopped on purpose per `AGENTS.md:50`)
1. Read back each vector register **right after** writing it → does a non-secure
   write stick at all?
2. **cpu1 positive control:** offline cpu1 via sysfs, stub-wake it with coreup's
   flow → if the stub does not run on a *known-good* core, the method is flawed,
   not the fuse.
3. Sample `CORE2_PC` at a few delays after power-on: PC ≈ 0x9e30xxxx → the core
   reached the TEE; PC stuck at 0 → it never fetched → fuse/reset gate.

## Rebuild recipe (for when the recovery gate is met — see
## [[2026-10-14-recovery-path-mtk]])
`CONFIG_NR_CPUS=4`; `L2CTLR_get_core_count` → 4; the power-down at
`mt-smp.c:327-337` is then skipped; set `is_secondary_cpu_first_boot` (343);
adjust `irq_total_secondary_cpus` (`irq.c:413-418`); rebuild ALL out-of-tree
modules (modversions off). Delivery = bootimg flash (gated), or a kexec-style
no-flash jump (CONFIG_KEXEC off → very invasive). Note LK may verify the boot
image (`boot_chk_bootimg`/`boot_chk_tee`, unanalyzed) — relevant to the recovery
net before any flash.

(Subagent also flagged delete-queue id 324 as a false positive — nothing was
deleted; it can be rejected.)
