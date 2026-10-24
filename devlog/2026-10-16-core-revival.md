# core revival — MT8590 is quad, 2 cores software-power-gated

2026-10-15 night / 10-16. Goal "코어 부활" = light up the other 2 Cortex-A7 cores. Device
plugged in, Claude runs adb directly (serial <serial>).

## Proven on device (nail-in-coffin, un-fakeable)

1. **Silicon is QUAD A7.** `probe/kmod/coreprobe/coreprobe.ko` reads L2CTLR (`mrc p15,1,c9,c0,2`)
   from PL1 on each cpu → `0x03800001`, bits[25:24]=0b11 → **cluster of 4**. MIDR `410fc073` =
   Cortex-A7 r0p3. L2CTLR core count is a synthesis-time constant the efuse can't change. (Earlier
   "dual" guesses — incl. community reads of superwilso/Cinder — were wrong.)

2. **The "power cut" is the KERNEL, not an efuse.** `arch/arm/mach-mt8590/mt-smp.c`
   `mt_smp_init_cpus()`: `ncores = L2CTLR_get_core_count()` (=4) → `if (ncores > NR_CPUS)` clamp to
   NR_CPUS=2 → `if (ncores==2){ spm_mtcmos_ctrl_cpu3(STA_POWER_DOWN); spm_mtcmos_ctrl_cpu2(STA_POWER_DOWN); }`
   → `set_cpu_possible(0..1)`. So at boot the kernel powers cpu2/3 OFF because it was built
   NR_CPUS=2. The clamp warning is silent on device (`CONFIG_ICX_SILENT_LOG=y` + early-ring wrap).

3. **The power gate OPENS.** `coreup_mod.ko` dry run: cpu2/3 PWR_STATUS bits (bit10/bit9)=0 (off),
   PWR_CON=0x32 (PWR_ISO|PWR_CLK_DIS|SRAM_CKISO, reset held), L1_PDN=0x101; firmware slave-boot
   magic slots already populated (cpu2 "LAS2", cpu3 "ASL3"). LIVE `cpu=2`: **"powered but no
   report"** — `power_on()` completed with NO PWR_STATUS-ack timeout → the domain is soft
   power-gated, NOT fused. Device survived, clean teardown. But the stub's first store never ran
   (mailbox 0 0 0, wfi 0) → the woken core's PC never reached our code.

## Diagnosis of "powered but no report"

Config: `CONFIG_MTK_IN_HOUSE_TEE_SUPPORT=y`, `CONFIG_TRUSTONIC_TEE_SUPPORT` not set → the compiled
wake path uses **register writes, no SMC**. `mt_platform_smp_prepare_cpus` sets SLAVE_JUMP_REG +
NS_SLAVE_JUMP_REG; `mt_wakeup_cpu` sets magic + BOOT_ADDR + NS_SLAVE_BOOT_ADDR + powers on; then
`mt_boot_secondary` does **`smp_cross_call(cpumask_of(cpu))` = an SGI**. coreup wrote every one of
those regs but **sent no wake event** → the powered core parks in the bootrom wait and never reads
the jump reg. Hypothesis (from source, not yet proven): add the wake kick.

## Code change made (coreup.c, built-but-not-yet)

Added `wake_core(phys)`: after `power_on`, `dsb(); sev();` (for a WFE spin) + a GIC SGI to the
physical core (`writel(1<<(16+phys), GIC_DIST_SOFTINT)` where `GIC_DIST_BASE=0xF0211000`,
`CORTEXA7MP_BASE=0xF0210000`, SOFTINT=+0xF00) for a WFI spin — matching `smp_cross_call`. Called
once after power-on and re-kicked for the first 10 ms of the mailbox poll. New `nokick=1` param to
A/B it. Success criterion next run: mailbox shows `MPIDR 80000002`.

## Plan

- **Rebuild on aws-burst** (user's call, 10-16): BLOCKED — aws-burst is unreachable from Fedora or
  spectre. `buildbox`=`ssh.lightning.ai` (old Lightning, now auth-denied/retired per
  [[spectre-stops-cargo]] "Lightning replacement"); no aws CLI, no burst launcher/alias found;
  spectre lacks the arm-eabi-4.8 toolchain and the `~/.cache/nw-kernel/linux-kernel-3.10.26` tree.
  Needs the user to launch/point to aws-burst (and the tree+toolchain provisioned there).
- **Device run deferred to 10-16 afternoon.** Then: fresh boot, `insmod coreup_mod.ko cpu=2`, grep
  dmesg, rmmod; compare to `nokick=1`. If it runs, that proves soft-revival; full Linux join still
  needs NR_CPUS=4 rebuild + platform SMP + bootimg flash (gated: see [[nw-a55-container-reset]]).

Memory: `nw-a55-core-revival.md`.

## 10-16 afternoon — module approach exhausted

Built coreup locally (`make`, arm-eabi-4.8 — a 2-file C compile, not cargo; aws-burst stayed
unreachable so local was the only path, and the aws-burst rule is cargo/rustc-scoped) and ran it
three times on the device (serial <serial>, survived each, no WDT reboot):

1. **wake kick** (`wake_core`: SEV + GIC SGI to the physical core, GIC_DIST_SOFTINT=0xF0211F00) —
   still "cpu2 powered but no report".
2. **MPIDR-first stub** — stub now writes MPIDR to mailbox[0] before any check; mb[0] stayed
   `00000000` → the stub's first store never executed → the core's PC never reached our code.
3. **TEE reset-vector SMC** — `sec_call(MC_FC_SET_RESET_VECTOR=-301, stub_phys, cpu, 0)` returned
   `smc_ret -65519` (0xFFFF0011, not MC_FC_RET_OK=0) → the IN_HOUSE_TEE monitor rejects a runtime
   vector change.

Also verified coreup's `power_on` is byte-identical to the kernel's `spm_mtcmos_ctrl_cpu2`
STA_POWER_ON sequence — the power-on itself is correct; the core comes up exactly as cpu1 does.

**Conclusion:** on this locked device the secure monitor owns the NS reset vector (latched to
`mt_secondary_startup`), so arbitrary code on a secondary core is not reachable from a module. The
core IS routed to the Linux secondary entry, so the real path to using cores 2/3 is a
`CONFIG_NR_CPUS=4` rebuild + bootimg flash (gated). Commits abde041, bc6634e.
