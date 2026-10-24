# Patched + zram observation run: design (not yet authorized)

Host scripts only. Nothing here has been run on the device, and the stub
`tools/patched-zram-observe.sh` exits without touching it.

## Why a fresh boot is required

`totalswap == 0` cannot detect a lock leaked by an earlier `swapon`/`swapoff` in
the same boot: the guard's zero-swap check only proves that no swap is active
*now*. The run therefore starts from a **new boot in which no swapon has ever
happened**, and the preflight proves it from the complete dmesg (no
`Adding ... swap` line anywhere), not from `/proc/swaps` alone.

## Preflight (nothing loaded)

- fresh-boot identity: device uptime, `/proc/version`, and the boot timestamp;
- complete `dmesg` captured and checked for `Adding .* swap`: any occurrence
  aborts the run;
- `/proc/swaps` empty and `SwapTotal: 0`;
- stock symbol addresses match `swap_guard_spec.h` (`swap_info_get`,
  `smaps_pte_range`, `kpageswapn_read`, `init_mm`, `v7_coherent_kern_range`);
- no container running.

## Load

FIQ mode `2 10 30 0 1` verified by `/proc/wdk` **and** the actual
`mtk_wdt_mode_config` register, then `insmod` the guard **before any zram is
enabled**. No force-insmod and no `rmmod`; the module has no exit path and
reboot is the recovery.

## Workload and the swap requirement

The **original helper order is preserved**: `prepare; zram; swap; container;
start_init` — `swap` is the **Mali driver replacement**, not memory swap. The run
is **invalid** unless `SwapTotal > 0` **and** `SwapUsed > 0` are observed: with
no swap used there are no swapped PTEs, the smaps walk never reaches
`swap_info_get`, and the run proves nothing.

A **long-lived PID with measured swapped PTEs** is pinned and its
`/proc/<pid>/smaps` is read on a bounded cadence; a container process such as
`system_server` or `zygote` is preferred because it is long-lived. What matters
is the **measurement**: the read must succeed and its ordinary `Swap:` fields
must sum above zero. A stock process whose read measures `Swap: 0` is not a path
hit, but neither is it ruled out in advance — a stock process with a measured
`Swap:` sum above zero counts. PID and start time are recorded, and if the PID
exits another is chosen with the transition logged.

## Observation: 30 minutes from the workload launch

- No break on `boot_completed`; it is recorded, not a stop condition.
- Continuous: uptime and boot continuity, actual wall and device elapsed,
  complete `/proc/swaps`, `SwapTotal`/`SwapFree`, `MemFree`, genuine
  `lowmemorykiller "Killing '"` lines and their count.
- Each smaps read records its **exit status and byte count**, so a successful
  path read is distinguishable from a PID that exited or an `EFAULT`. Failures
  are classified, never counted as successes.
- Raw kmsg and logcat are captured throughout, and on a reset the postreset
  `last_kmsg` is saved before anything else.

## Abort policy

Any `STOPPED`, `WRITE`, `ROLLBACK`, `RO_RESTORE`, `PERM`, unknown or incomplete
evidence aborts the workload and forces a **reboot before any retry**. A rollback
that reports success does not license reusing the boot: the refusal was
uncertain, so the state is not trusted. No automatic re-up or retry after a
reset.

At the end the workload is stopped and stock recovered with one reboot.

## What a pass means

The earlier post-zram freeze was **~102.05 s** after the swap add. One
reset-free 30-minute run with real swap use supports the mitigation hypothesis.
It is not proof that every caller of the faulty pair, or every failure mode, is
fixed.

## Deployment and load evidence (raw files, real statuses)

Every step writes raw stdout, stderr and its **own exit status** to separate
files. The insmod result must be the **device-side** status, not the status of
the last `adb echo`/`grep` in a pipeline. Recorded alongside: the host `.ko`
SHA-256, the device copy's hash (or a pull-back comparison when the device has no
hashing tool), the source commit, and the `.ko` vermagic.

## Module validation before any swapon

The original zram-enabled `up` runs only after the module logs show **all** of:

- both sites patched with the exact word `e3a00000` and `patch_readback_ok=1`;
- `writes 2/2`;
- `ro_verified=1` for each site;
- no rollback and no failure counter set.

An empty or missing log line is never counted as success. The latest live
kallsyms is recorded, including the fixed `init_mm` address
(`SG_STOCK_INIT_MM`). If any backend cannot confirm its PMD/PTE/RO/cache
behaviour, **no swapon happens**.

## Helper order

The existing unpatched helper order is `prepare; zram; swap; container;
start_init`, and it is preserved once the guard is verified — `swap` here is the
**Mali driver replacement**, not memory swap. Nothing alters the image or the
data content to force a result: the data and cache are warmed by earlier runs,
and that limitation stays in the comparison rather than being reset away.

## Proving the swapped-PTE path was exercised

Global `SwapUsed > 0` plus a stock PID is not enough: a stock PID has `Swap: 0`,
so its smaps walk never reaches the faulty pair. The run therefore requires a
**successful, complete smaps read whose sum of ordinary `Swap:` fields is > 0**
on at least one monitored PID — preferably the container's `system_server` or
`zygote`, with its PID and start time recorded. If that PID exits, another is
chosen and the transition is logged; an empty or failed read never counts as a
path hit. Each read records bytes, exit status, the `Swap:` sum, timestamp and
PID/start time. One read per sample, bounded cadence, no fork storm, and no
`/proc/kpageswapn` reads (it predictably returns `EFAULT` and logs).

## Observation window

At least **1800 actual device monotonic seconds** after the single workload
start, with wall timestamps alongside; elapsed time is never inferred as
`i * 30`, and `boot_completed` is not a stop condition. Samples are never
silently dropped: each is a uniquely numbered raw file, with boot-continuity
evidence recorded. Ordinary LMK `Killing '` lines and `MemFree` are tracked
alongside the hang/reset/OOM distinction.

## Failure handling

The workload and swapon are **never retried automatically** after an invalid or
failed run. Any reset captures a fresh `last_kmsg` before anything else, then
stock recovery. Every named forbidden partition and node stays inaccessible to
the experiment, exactly as in the existing helper.
