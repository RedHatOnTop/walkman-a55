# Watchdog/FIQ capture during an Android-container reset (2026-10-12)

Objective: obtain one watchdog/FIQ diagnostic capture during the Android-container
reset, not to change bring-up. The volatile `/proc/wdk` mode change was
explicitly approved by the user, who was at the computer and able to reset the
Walkman manually. The device reset on its own; no manual reset was needed.

## Phase 1: preflight

- No other session was driving the device: `pgrep -af 'adb|android-boot|stream-25s|...'`
  returned only the adb server, no DSH background jobs were running, and
  `work/devmod-*` was last written 2026-10-11 12:45-13:26, ~16 h earlier.
- Device in known stock state at host 2026-10-12T05:43:53Z: uptime 54643.51 s,
  no container (`/tmp/aroot/init.pid` absent), `mali_r4p0` not loaded, Sony
  stack present. `hagodaemon` is **one process** (comm `hagodaemon`), not 58; the
  earlier "58" counted thread rows.
- Baseline capture into `work/android/wdt-20261012T054505Z/` (flat files, taken
  before the helper gained per-label subdirectories), every capture exit 0:
  `last_kmsg.out` 7167 B, `dmesg.out` 128751 B, `wdk.out`, `uptime.out`,
  `swaps.out`, `meminfo.out`, `loadavg.out`, `version.out`, `kallsyms.out`
  2582544 B, `ps.out`, `mounts.out`, plus `baseline.hosttime`.
- Host 2026-10-12T05:45:05Z, device uptime 54716.21, device clock
  2022-02-20T01:53:40Z (the device RTC is years off; both clocks recorded).
- Kernel: `Linux version 3.10.26 (slave@azslave5q) (gcc version 4.8) #1 SMP
  PREEMPT Wed Jul 31 16:22:50 JST 2019`.
- Stock `/proc/wdk`: `mode interval timeout enable` / `1 10 30 1`. The measured
  kick interval is **10 s**, not the source default of 20, so the interval was
  preserved and only the mode changed.
- Baseline `last_kmsg` still held the previous reset's tail: 0 matches for
  `Restarting system|Power down|System halted|Kernel panic|Oops`, ending
  `ram console header, hw_status: 1, fiq step 0.` / `bin log 0.`

## Phase 2: source, then one run

Read from `~/.cache/dswatch/ksrc/` (same tree as
`oss/linux-kernel-3.10.26.tar.gz`):

- `wdk/wd_common_drv.c:103-200`: `sscanf(cmd_buf, "%d %d %d %d %d", &mode,
  &kinterval, &timeout, &debug_sleep, &en)`; mode 2 calls
  `my_wd_api->wd_set_mode(WDT_IRQ_ONLY_MODE)` (:190-191) and is the only mode
  that skips `g_need_config` (:196-197). Interval bounds are 1..30
  (`:56-57`), so 10/30 is valid.
- `wdk/wd_api.c:160-176`: `WDT_IRQ_ONLY_MODE` calls
  `mtk_wdt_mode_config(FALSE, TRUE, TRUE, FALSE, TRUE)`.
- `wdt/mt8590/mtk_wdt.c:103-157`: bit 0 ENABLE, bit 3 IRQ, bit 6 DUAL, bit 4
  AUTO_RESTART always set; the function prints the **actual** register and `tmp`
  unconditionally (:153-154).
- `wdt/mt8590/mtk_wdt.c:447-460`: `wdt_fiq` prints
  `kick=0x%08x,check=0x%08x` then calls `aee_wdt_fiq_info`; `:490-491`
  registers it with `request_fiq` under `CONFIG_FIQ_GLUE`.
- `aee/common/wdt-handler.c:444-481`: `aee_wdt_fiq_info` records
  `get_HW_cpuid()`, prints `CPU %d FIQ: Watchdog time out` and the preempt
  counts, dumps the interrupted CPU registers and stack, and loops forever if
  another CPU already entered. The `BUG()` at `:439` is inside
  `aee_wdt_irq_info` (`:388`), which `:481` calls — not in the FIQ function
  itself.

Applied the approved volatile setting and verified it two ways:

```
echo "2 10 30 0 1" > /proc/wdk          -> exit 0
cat /proc/wdk                            -> "2    10        30       1"
dmesg | grep mtk_wdt_mode_config         -> "[WDK] mode=2 interval=10 timeout=30 enable =1"
                                            "[WDK] enable wdt"
                                            "mtk_wdt_mode_config  mode=1d, tmp:2200001d, pid=7087"
```

`mode=0x1d` = ENABLE(0x1) + EXTEN(0x4) + IRQ(0x8) + AUTO_RESTART(0x10), DUAL
(0x40) clear: IRQ set, ENABLE set, DUAL clear, as required. The same dmesg shows
the Sony stack toggling the mode from `kworker/u4:4` (`mode=15` / `mode=5c`), so
the mode register is not stable on its own; the captured kmsg contains no mode
change later than `0x1d`.

Run: `work/android/wdt-20261012T054505Z/run-capture.sh`, one `up`, one
`/proc/kmsg` reader (`kmsg-stream.txt`, 684045 B) and one logcat stream through
`pivotinit --join` (`logcat-stream.txt`, 678786 B). `up.exit` = 0.

Timeline: container `up` at 05:49:05Z, init pid 7597, `boot_completed` empty at
t=15/30/45/60 s, then at t=75 s the bounded `uptime` call returned empty and the
runner logged `DEVICE_STOPPED_ANSWERING`. Both streams stopped writing at
14:50:49.94 KST. The device reconnected 70 s later, i.e. it rebooted by itself.

## Phase 3: the capture

Postreset capture into `work/android/wdt-20261012T054505Z/postreset/` (new
subdirectory, all exits 0): `last_kmsg.out` **7470 B**, `dmesg.out`, `wdk.out`
(back to `1 10 30 1` — the mode change is volatile and did not survive), plus
`uptime.out` 33.94 s at capture time. Resolved symbols in
`postreset/resolved-symbols.txt`.

The fresh `last_kmsg` contains the dump. Verbatim excerpt (the register, stack
and backtrace rows are complete; the surrounding ioctl traffic is omitted):

```
[15157.588310] (0)[7605:surfaceflinger]mtkfb_ioctl ... (last line of normal traffic)

cpu 0 preempt=2, softirq=0, hardirq=0
cpu 0 r0->r10 fp ip sp lr pc cpsr orig_r0
00000002 00000000 000032d2 000032d1 d591b85c 00000013 cc369224 d50b1e58 b528a000 d50b1e78 b5289000 d50b1d8c d50b1d78 d50b1d78 c07c9c00 c07c9c28 80070013 80070013
cpu 0 stack [d50b1d78 d50b1df8]
00000000 d591b800 d50b1da4 d50b1d90 c01504b8 c07c9bec 00755000 0000000d d50b1de4 d50b1da8 c01acb94 c0150454 ff0a0000 00007550 cb4bb8fc c0c744c0 c09a964c b5289000 d50b1e58 b528a000 cb03ad48 b528a000 cc36a160 00000000 d50b1e1c d50b1de8 c014c44c c01acac8 d50b1e10 b5289fff c017bec0 cc36a160
cpu 0 backtrace : c07c9c00, c01504b8, c01acb94, c014c44c, c01ac7f8, c01aca0c, c017b884, c015a378, c015aa30, c000eda0,

 kick=0x00000000,check=0x00000003
WDT: failed to stop other CPUs in FIQ

Qwdt at [15188.622235]
 CPU 0, nested_panic_time=0
wd_api[0xc0b9ac94]
wd_restart[0xc03e87a8]
mt8590 only hw reboot mode test
mt6582 only hw reboot mode --
Nested panic
Previous reg:
pc: c038b7cc lr: c038b7cc psr: a00701d3
...
Current reg:
pc: c0441e44 lr: c0442d64 psr: 000701d3
...
```

Resolved against the captured `/proc/kallsyms` (same kernel build, no KASLR):

```
c07c9c28  _raw_spin_lock+0x48          <- CPU 0 pc at the FIQ (ticket spin wait)
c07c9c00  _raw_spin_lock+0x20          <- CPU 0 lr at the FIQ
c01504b8  swap_info_get+0x70           <- return address of the call
c01504b8  swap_info_get+0x70
c01acb94  smaps_pte_range+0xd8
c014c44c  walk_page_range+0x144
c01ac7f8  show_smap+0x8c
c01aca0c  show_pid_smap+0x1c
c017b884  seq_read+0x2ec
c015a378  vfs_read+0xa8
c015aa30  sys_read+0x4c
c000eda0  ret_fast_syscall+0x0

c038b7cc  aee_wdt_irq_info+0x11c        <- panic "Previous reg"
c0441e44  simp_mmc_single_write.isra.31.constprop.40+0x28   <- panic "Current reg"
c0442d64  card_dump_func_write+0x17c
c0b9ac94  g_wd_api_obj+0x0
c03e87a8  wd_restart+0x0
```

Context around it, from the streams:

- Last non-ioctl kernel line is `M4Ualloc_mva_dynamic: id=DISP_OVL_0` at
  15157.282. Just before it, lowmemorykiller kills at 15147.24:
  `Killing 'id.printspooler' (9425), adj 9, score_adj 529` and
  `Killing 'FinalizerWatchd' (9436), adj 9` — 2 real kills in this window.
- The logcat tail is the framework still booting:
  `BluetoothManagerService: Trying to bind to profile: 1`,
  `W/art: Verification of com.amazon.kindle.otter.oobe.modules.oobeend.OOBEEndModule$UserRole`,
  and `E/memtrack_amz: generic_inspect_heap error to open
  /sys/kernel/debug/ion/heaps/ion_mm_heap`.
- Gap: last log line 15157.588, `Qwdt` 15188.622 — **31.03 s** of no output.

## Verified: the swap lock is taken and released mismatched

The captured PC is `c07c9c28` = `_raw_spin_lock+0x48` with LR `c07c9c00` =
`_raw_spin_lock+0x20` (an earlier reading of this entry took the LR as the PC).

Source, read from `oss/linux-kernel-3.10.26.tar.gz`:

- `mm/swapfile.c:558-590` `swap_info_get()` ends with `spin_lock(&p->lock)`
  (`:576`) and returns `p`.
- `mm/swapfile.c:594-597` `swap_info_unlock(void)` is
  `spin_unlock(&swap_lock)` — the global, not `p->lock`.
- `fs/proc/task_mmu.c:539-546`, the smaps path, does
  `p = swap_info_get(entry); if (p) { ...; swap_info_unlock(); }`.

So the smaps walk takes `p->lock` and releases `swap_lock`: the per-device lock
is leaked on the first swapped PTE, and the next `swap_info_get` on that device
spins forever on it.

Compiled proof, not just OSS. Disassembled the stock `vmlinux.bin`
(`nwa50-extracted/kernel/vmlinux.bin`, version string identical to the running
kernel) with the ARM toolchain, base `_text = 0xc0008000` from the pre-run
`/proc/kallsyms`:

```
swap_info_get @c0150448:
  c01504b0: e284005c  add  r0, r4, #92   @ 0x5c   ; r4 = p, +0x5c = p->lock
  c01504b4: eb19e5c9  bl   0xc07c9be0            ; _raw_spin_lock
  c01504b8: e1a00004  mov  r0, r4                ; return p
swap_info_unlock @c01508e0:
  c01508f4: ldr r0, [pc, #4]
  c01508f8: bl  _raw_spin_unlock
  c0150900: .word 0xc0c74e34                     ; the runtime global swap_lock
```

Cross-check against the dump: the FIQ recorded `r4 = d591b85c`, and the caller's
`p = d591b800`, so `r4 = p + 0x5c` — the CPU was waiting on the lock **inside the
swap_info_struct**, at exactly the offset `swap_info_get` locks. Independent
Capstone disassembly reached the same result and is saved at
`work/android/wdt-20261012T054505Z/stock-lock-mismatch.txt`. The mismatched
unlock is therefore in the compiled stock binary.

## Ticket state, the second caller, and the trigger

Observed ticket state in the dump, not an inference: CPU 0 `r2 = 0x32d2` is its
allocated ticket and `r3 = 0x32d1` is the ticket currently owning the lock, so
there was exactly **one** outstanding predecessor and no intervening queued
ticket.

Known: the **owner ticket is `0x32d1`**, one BEHIND CPU 0's `0x32d2`, with exactly one outstanding acquisition and no intervening waiter.
Unknown: the **thread that owns it**, and the other CPU's registers. A prior
smaps swap hit leaking its acquisition explains the ticket being held — possibly
the same walk self-deadlocking — but the owning thread is not captured, so that
remains an explanation rather than an observation.

The mismatched unlock also corrupts the global lock: `swap_info_unlock` releases
`swap_lock` without holding it, which advances its owner past its next, so a
later acquirer can spin and later tickets may wrongly enter. `mm/swapfile.c:424`
`get_swap_page` takes `swap_lock` and then the per-device lock for allocation, so
kswapd or direct reclaim on the other CPU is a **candidate** for what was
spinning there — not an observed CPU 1 stack.

A second caller has the same mismatch. `fs/proc/page.c:107-114`
`kpageswapn_read` does `swap_info_get`, then `put_user` while holding the device
lock, then `swap_info_unlock`. Compiled confirmation in the stock binary:

```
kpageswapn_read @c01b996c (kallsyms: "t kpageswapn_read")
  c01b99a4: bl 0xc0150448   ; swap_info_get
  c01b99ec: bl 0xc01508e0   ; swap_info_unlock
  c01b9a28: bl 0xc01508e0   ; swap_info_unlock, error path
```

So `/proc/kpageswapn` (libpagemap/procrank) is reachable independently of
smaps, and a smaps-only mitigation would leave it.

Trigger evidence. The baseline capture shows `swaps.out` empty and SwapTotal 0,
so the stock device never exercised this path. `probe/android/android-boot.sh`
`zram()` writes `268435456` to `/sys/block/zram0/disksize`, then `mkswap` and
`swapon`, and the run's kmsg stream contains:

```
[15055.503708] (1)[7185:sh]zram: Initialization done!
[15055.536787] (1)[7199:busybox]Adding 262140k swap on /dev/block/zram0.  Priority:-1 extents:1 across:262140k
```

The watchdog fired at 15188.622, about **133 s** after swap was added, and the
last kernel line (15157.588) is **102.05 s** after it. This is not a proven zram
driver defect: enabling swap makes the faulty accounting path reachable, and the
stock no-swap state left it dormant.

## Proposals (analysis only, nothing run or written)

- Same container run with zram left off, everything else unchanged. Prediction
  to test: no WDT reset, with more LMK pressure instead. **Not run** — no device
  run is authorized for this.
- For smaps, replace the call at `c01acb90` with `mov r0, #0` so the existing
  `beq` skips the lock/unlock pair while keeping `mss->swap` and losing the
  vendor `SwapPss` figure. `kpageswapn` would need its own mitigation. Analysis
  only.
- Any persistent boot, system or recovery patching stays gated on a confirmed
  recovery path and explicit user approval. No write is authorized, and none was
  made.

## Facts, hypotheses, and what is still unknown

Device- and source-supported facts:

1. The watchdog interrupt fired in IRQ-only mode and the FIQ handler ran: the
   dump above is its output, and `mode=1d` was verified before the run.
2. `kick=0x00000000,check=0x00000003`: at the moment the FIQ sampled, neither
   CPU's kick bit was set while both were expected. That is one sample, not
   proof that neither kicker ran at any point during the preceding 30 s.
3. The FIQ could not stop the other CPU (`WDT: failed to stop other CPUs in
   FIQ`).
4. The interrupted CPU 0 context is a userspace `read()` of `/proc/<pid>/smaps`
   (`show_pid_smap` -> `smaps_pte_range`), spinning in `_raw_spin_lock` reached
   through `swap_info_get`, on the lock at `p + 0x5c`.
5. The compiled stock kernel takes `p->lock` there and releases `swap_lock`
   elsewhere, so the spin is explained by the mismatched pair above.
6. The panic path contains `card_dump_func_write` and `simp_mmc_single_write`.
   That is the dump-writing code running, not evidence that the write completed
   or that the eMMC was usable: whether the expdb dump persisted is unverified.

Hypotheses, not established:

- The identity of the thread owning ticket `0x32d1`, and the other CPU's
  registers. The owner ticket is known from the dump; the owning thread is not
  captured, and `failed to stop other CPUs in FIQ` is evidence that the second
  CPU was not captured, not proof of a bus hang.
- Whether other callers leak the lock too. The two accounting callers traced here
  — smaps and `kpageswapn` — are both compiled-verified faulty; the remaining
  `swap_info_get` call sites were not fully audited.
- Whether the OOBE/memtrack activity at the end of logcat is related. The
  `memtrack_amz` line is a failure to open an ion heap, not a smaps read, and the
  exact reader of `/proc/<pid>/smaps` in this run was not identified.

Explicitly not concluded: absent FIQ lines in the streamed kmsg mean nothing (the
stream ends before the dump, which exists only in `last_kmsg`), and the earlier
logging silence does not by itself prove both CPUs were frozen.

## No-zram control run: predeclared gate (2026-10-12)

Approved by the user ("대조실험 진행", 06:17:02Z). Hypothesis under test: with no
swap, the faulty accounting path is unreachable, so the watchdog reset should
not occur.

Gate, declared before the run:

- **One continuous 30-minute observation** after the container launches, with no
  reboot and no hang, and with swap remaining absent throughout. The window is
  30 minutes because prior reset latencies include ~2 min, ~6 min and 16.1 min
  (`devlog/2026-10-11-android-container.md:437,459,471`); a 15- or 20-minute
  bound would not have covered the 16.1-minute case.
- `boot_completed` is **not** the stop condition. It is a separate observed
  result; the run continues past it if it is set.
- **Swap invalidation**: complete `/proc/swaps` and `SwapTotal` are recorded
  periodically, including after Android init starts, because Android's
  `swapon_all` could enable swap independently. Any swap entry, any positive
  `SwapTotal`, or any new kernel `Adding ... swap` line during the control
  **invalidates** it: stop and report the invalidity rather than silently
  swapping off and continuing.
- The watchdog mode is the same as the diagnostic run: `/proc/wdk` set to
  `2 10 30 0 1`, with the actual register verified at `0x1d`.
- The only difference from the failing run is the omitted `zram()` call. `up()`
  keeps `prepare; swap; container; start_init` — `swap()` replaces the **Mali
  driver**, it is not memory swap. No kernel code patch, no persistent setting.
- Memory is watched as `MemFree` plus genuine `lowmemorykiller "Killing '"`
  lines, so an LMK/OOM or process-restart failure is distinguishable from a
  kernel/WDT reset. Kill counts are compared as rates over the window, since the
  runs differ in length.

What one reset-free 30-minute run would support: the hypothesis that the swap
path is what the resets depend on. It would not prove sole causality, and it
would not be a complete fix. It also does not assert that LMK pressure increased
unless the measured rate exceeds the first run's.

Identity limitation in this comparison. Fixed code and module identities are
recorded and comparable (`container.tgz`, `mali_r4p0.ko`, `pivotinit`, the git
head, all hashed into `identities.txt`). The **data image is not**: the host
`android-data.img` hash does not prove what the reused on-device loop image
contains, because `push_once` skips by size and Android writes persistent
dalvik and `/data` state during every run. The first diagnostic run's pre-run
device-image checksum was never captured, so byte-for-byte data and cache
identity between the two runs is **unverified**, and the second run may start
from a warmer cache than the first. The image is reused as approved rather than
reformatted or restored, since that would hide the difference instead of
recording it.

### First launch stopped on a tooling bug, before any sample

The first runner was stopped before its first sample. Its count logic was

```sh
kills=$(grep -c "Killing '" "$DIR/kmsg-stream.txt" 2>/dev/null || echo 0)
adding=$(grep -c 'Adding .* swap' "$DIR/kmsg-stream.txt" 2>/dev/null || echo 0)
```

`grep -c` prints `0` **and exits 1** when nothing matches, so `|| echo 0`
appended a second `0`: the value became `0\n0`, `[ "$adding" != "0" ]` was true,
and the loop would have logged INVALID and stopped at t=30 s with no swap
present. Reproduced on the host: old logic `adding=[0\n0]` (3 bytes); fixed
logic `adding=[0]` on zero matches and `[1]` on one `Adding 1k swap` line.

What was kept: the launch itself was correct — the copied no-zram helper ran
`up` with rc=0 at **2026-10-12T06:26:09Z** and started container init **1635**,
and both streams were already writing. A second `up` was not run: another
`start_init` in a live container is outside the approval. The run is the same
one, observed by a corrected loop.

The corrected observer (`observe-nozram.sh`, byte copy saved at run time as
`observer-as-run.sh`) is the code **actually used**: it does no `wdk` write, no
`up` and starts no second kmsg reader. It anchors the window at the logged
container start and runs until **06:56:09Z or later**, sampling every 30 s. The
original buggy runner is preserved unmodified as `run-nozram.sh`, with the copy
taken before the correction as `runner-as-run.sh`, so the pre-launch bug stays
visible in the evidence rather than being tidied away. `EVIDENCE-INDEX.txt` in
the capture directory maps every file to its role.
 Gap proof at observer start, covering 06:26:09Z to the first sample:

```
init=[1635] (same container) uptime=[2192.43] kmsg_last_ts=[2187.945003] adding_swap=0
```

i.e. the container is the one that launched, the device uptime and the kmsg
timestamps agree to 4.5 s so there was no reboot in the gap, and no
`Adding ... swap` line had appeared.

## No-zram control run: result

Gate **passed**. 55 samples from 06:28:48Z to 06:56:16Z, uptime strictly
increasing, every sample rc=0, `SwapTotal` 0 and zero swap lines throughout,
watchdog mode `2 10 30 1` in every sample, and the kmsg stream contains no
`Adding ... swap` line and no watchdog timeout. The window from the logged
launch at 06:26:09Z to the last sample is **1807 s (30 min 7 s)**.

Timing, with the two gaps labelled separately: the largest **host timestamp**
gap is **33 s** and the largest **device uptime delta** is **32.1 s**. Those are
different measurements and are not the same number.

Coverage arithmetic, which is three different intervals:

- launch 06:26:09Z to observer start 06:28:18Z = **129 s**;
- launch 06:26:09Z to the first sample 06:28:48Z = **159 s**;
- the continuity check itself, WDK setup 06:25:39Z to observer start 06:28:18Z,
  is **159 s of wall time against 158.98 s of device uptime**, with init 1635
  still alive.

So the pre-observer interval is covered by the continuous kmsg stream plus that
uptime continuity check, not by the sample loop.

The kmsg stream begins at uptime 0.000000, so it contains boot-time lines:
`zram: Created 1 device(s)` at 0.779616 and the FIQ registrations at 0.39/0.81 s.
Those are **driver initialisation**, not swap activation and not a watchdog
event; they are not invalidation and not a WDT hit.

`sys.boot_completed` was first observed at **06:41:27Z**, 15 min 18 s after
launch (uptime 2981.02). That is the first `boot_completed=1` in these
diagnostic runs: every earlier mention in `devlog/` and `docs/` says it was
never set.

LMK timing, stated only as far as the samples support it. **A clean phase split
at `boot_completed` is not established**, because the transition falls inside a
sampling interval:

- last sample with `boot=[]`: 06:40:56Z, uptime 2950.39, **kills=0**;
- first sample with `boot=[1]`: 06:41:27Z, uptime 2981.02, **kills=9**;
- the first actual `Killing '` line in the kmsg stream is at uptime
  **2965.631591**, i.e. between those two samples.

So: zero kills up to the last sampled incomplete state, nine by the first
sampled completed state, **164 in total**, and where exactly the kills sit
relative to the property transition is unresolved inside that 31 s sampling
interval. It is not correct to call all 164 kills "after `boot_completed`", and
not correct to say there were no kills before the exact transition.

Kill window: first `Killing '` at uptime 2965.631591, last at 3386.700236
(about 06:41:11Z to 06:48:12Z), then none for the remaining ~8 min of the
window. MemFree bottomed at **13,304 kB** at 06:45:37Z.

No raw-count causal comparison is made with the short failed run: the first run
had 5 kills and reset without ever reaching `boot_completed`, this one ran 1807 s
with 164 kills, and the runs differ in length and in what they reached. The
pre-run prediction that removing swap would *increase* LMK pressure is therefore
not asserted in either direction.

What this supports: avoiding swap keeps the faulty accounting branch
unreachable, and this run survived 30 continuous minutes where the earlier runs
reset at ~2 min, ~6 min and 16.1 min. What it does not do: prove sole causality,
prove no other trigger exists, or constitute a fix. A single reset-free run is
evidence for the hypothesis, not a verdict.

## Recovery state and open mitigation

Two recoveries, distinct:

**Control run (approved, deliberate).** After the 30-minute window the observer
stopped the kmsg and logcat streams and the container with the copied host driver
(`down` rc=0, "the Mali driver swap still needs a reboot"), the final logs were
copied to `final-observer-samples.log` / `final-observer.log`, and one normal
reboot was issued as the approved recovery. Verified afterwards in
`postrecovery/`: uptime 68 s, `hagodaemon` back as one process, no container,
`mali_r4p0` unloaded, no swap, and `/proc/wdk` back to the stock `1 10 30 1` —
the volatile mode change does not survive.

**First FIQ diagnostic (spontaneous).** The device rebooted on its own 70 s after
adb stopped answering, with no manual reset and no flashing. The postreset
capture shows stock state (`/proc/wdk` back to `1 10 30 1`, mode change volatile
and gone). Raw evidence preserved in `work/android/wdt-20261012T054505Z/`
(baseline flat files,
`postreset/` subdirectory, `stock-lock-mismatch.txt`).

Mitigation is a separate question and nothing was implemented. Two categories,
gated differently:

- **Permanent writes** — boot, system or recovery partitions — stay gated on a
  confirmed recovery path **and** explicit user approval. None was requested or
  made here.
- **A volatile kmod or runtime kernel patch** is a different category: it does
  not touch persistent storage and disappears on reboot. It is also **not
  approved** for this work, and none was applied.

Avoiding the trigger has two forms, and both belong in scope: the two faulty
accounting callers (smaps and `kpageswapn`) and the simpler no-swap/no-zram
condition, which keeps the path unreachable by never having swapped PTEs to walk.
The control run above tests the second form.

## Tooling gates

`tools/wdt-capture.sh` captures raw stdout, stderr and the command's own exit
status per capture, reserves the label directory with a checked `mkdir` so any
existing label (empty or not) is refused, and exits nonzero when any capture
fails — including when the `.exit` or hosttime record cannot be written, since a
full filesystem can lose the record while adb itself succeeded.
`tools/wdt-capture-test.sh` drives it with a fake `adb` on PATH; the mock setup
aborts if the fake is not first on PATH, so a case can never fall through to the
real device.

Literal result of the final gates, run unpiped:

```
bash -n tools/wdt-capture.sh        -> rc=0
bash -n tools/wdt-capture-test.sh   -> rc=0
./tools/wdt-capture-test.sh         -> rc=0, ALL PASS, 21 PASS / 0 FAIL
```

Cases covered: success; adb error (42) not reported as success; timeout (124)
not reported as success; a later capture not overwriting an earlier one;
repeated label refused (3); unsafe labels refused (2); the standard set failing
when a capture fails; an existing **empty** label refused (3); zero and
non-numeric timeouts refused (2, since `timeout 0` disables the bound); an
unwritable destination refused (4); a lost exit record treated as a capture
failure; a lost hosttime write treated as a capture failure. The two write-loss
cases use a failing `printf` defined only in a copied `BASH_ENV` context, never
real adb.

This is a shell helper and mock tests, not a kernel build and not an end-to-end
fix. No C or Rust application code changed in this slice, and the device run is
evidence for the diagnosis, not a repaired kernel.

## Evidence paths

- `work/android/wdt-20261012T054505Z/` — baseline capture (flat, pre-helper)
- `work/android/wdt-20261012T054505Z/postreset/` — postreset capture, all exits 0
- `work/android/wdt-20261012T054505Z/postreset/resolved-symbols.txt` — resolved dump
- `work/android/wdt-20261012T054505Z/stock-lock-mismatch.txt` — Capstone disassembly
- `work/android/wdt-20261012T054505Z/kpageswapn-lock-mismatch.txt` — second caller
- `work/android/wdt-20261012T054505Z/codex-tests.out` — independent gate re-run
- `work/android/wdt-20261012T054505Z/kmsg-stream.txt`, `logcat-stream.txt` — streams
- `work/android/wdt-20261012T054505Z/run.log`, `up.exit` — run bookkeeping
- `work/android/wdt-20261012T054505Z/wdk-after.out`, `wdk-dmesg.out` — mode proof
- `tools/wdt-capture.sh`, `tools/wdt-capture-test.sh` — capture helper and tests
- `work/android/wdt-nozram-20261012T062210Z/` — control run evidence, indexed by
  `EVIDENCE-INDEX.txt` (baseline, streams, 55 samples, pre-reboot, postrecovery,
  the copied helpers and their diffs, and the preserved buggy runner)
- `work/android/wdt-nozram-20261012T062210Z/codex-control-verification.json` —
  independent parse of all 55 rows and the raw kmsg
- `work/android/wdt-nozram-20261012T062210Z/codex-tests.out` — independent
  mock-gate run

