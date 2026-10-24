# Patched zram gate failed: sleeping free notification under a swap spinlock

Date: 2026-10-12. The approved RAM-only guard loaded correctly, but the
patched+zram **30-minute gate failed with a watchdog/FIQ reset**. No retry or
additional patch was executed. The reset itself restored stock state; no extra
recovery reboot was issued.

## Inputs and predeclared gate

- Collector/capture: `a2be610`, independently host-tested (`make verify` rc0,
  300 PASS / 0 FAIL, no build diagnostics or real adb/sleep calls).
- Native BusyBox-ash full scan: 3.23 seconds, versus the previous 38.55 seconds.
  Cheap samples every 30 seconds; full smaps samples 1, 6, 11, etc.
- Guard artifact: unchanged source `e3bcecd`, SHA256
  `f73c9981237c76469a60961789fa65a44041d728492bd69fab05fadcbe40ca9e`.
- Fresh load evidence: `work/android/guard-load-20261012T123543Z/`, independently
  accepted by root and Claude reviewer. Exact five symbols, no swap history,
  FIQ `2 10 30 1` / actual `mode=1d`, both stock words matched, patched/readback
  `e3a00000`, both RO verified, writes2/2 and no rollback/failure.
- Loaded boot: `4b598319-1683-4f38-991a-aa4ef202a694`.
- Gate: continuous loaded boot for at least 1800 actual uptime seconds from
  Adding-swap, nonzero swap use and at least two distinct complete heavy-scan
  samples with ordinary Swap>0, rc0 and stable device starttime.

Evidence root: `work/android/patched-zram-20261012T123915Z/`. The original helper
was unchanged and ran once; Mali driver replacement remained. Existing warm
data/cache loop images were not reset. No kernel/firmware/raw-partition write
command was issued. The host wrapper returned `host_rc=0`; it does not expose
an authoritative remote up status, so none is invented. Its resulting live
container, guard, swap and boot state were captured separately.

Kmsg and capture started **before up**. Sample001 finished at uptime212.26;
up began at uptime236.49; swap was added at uptime238.790397; up completed at
267.77. The unique actual kernel line is:

```
<6>[  238.790397] (1)[1231:busybox]Adding 262140k swap on /dev/block/zram0. Priority:-1 extents:1 across:262140k SS
```

`12:40:32Z` in the initial worker summary was the later host capture wall time,
not swapon's wall event time. The kernel uptime is the observation clock.

## What ran and what failed

Root's independent parser accepted ten consecutive complete samples in the same
boot with FIQ unchanged. Sample006, uptime359.18, was a complete heavy scan:
150 PID summaries, 36 valid positive ordinary-Swap reads, total46328kB across
eligible reads, duration12.50seconds. Its retained maximum snapshot was PID3897
`.storagemanager`, starttime35210 before/after, rc0, 12733 lines, Swap3408kB and
SwapPss0kB. Ordinary swap was retained while the vendor field was zero in that
observed snapshot. This is an exercised swapped-PTE path, not merely swapon.
It is not proof about every CPU/caller or future cache state.

Across complete samples, maximum SwapUsed was46860kB, minimum MemFree13560kB.
The kmsg stream contains63 LMK `Killing '` lines after swapon. Boot completion
became1, but observation correctly continued. Only one heavy sample was positive
before failure; the predeclared repeated-hit criterion was not met.

Sample011 began at uptime509.33 (270.539603seconds after zram). Its original
1447-byte output ended after `SMAPS_SCAN=1`, with no END_SAMPLE or DEVICE_RC.
Transport rc was0 and stderr empty; neither makes this a successful sample.
The capture loop failed closed with the literal error:

```
2026-10-12T12:45:10Z STOP: sample011 missing/duplicate marker END_SAMPLE
```

The host-only supervisor also exited1 on that failure. Last complete sample010
started at478.80 (240.009603seconds after zram); **240seconds is not the freeze
or reset time**. Kernel timestamps establish:

- scheduling-while-atomic warning:526.296293, zram+287.505896seconds;
- last recorded normal traffic:526.319340, zram+287.528943seconds;
- recorded FIQ event:554.001982, zram+315.211585seconds.

The earlier diagnostic's last-normal-traffic freeze was zram+102.05seconds.
This run reached later normal traffic but still failed; it did not establish a
reset-free 30-minute window. Exact freeze onset is not observed between the last
normal traffic and FIQ. A temporal overlap with the smaps scan does not prove the
collector caused the deadlock.

## Dump-first capture and exact traces

On failure the worker did **not** send down/reboot. Bounded reconnect showed a
new boot `614d990e-c25d-4c05-877e-67774f380c2c`; `/proc/last_kmsg` was captured
first, then dmesg/WDK/memory/swaps/modules/container. Evidence:
`failure-20261012T124604Z/last_kmsg.out`, 7433 bytes, SHA256
`5ce3331316d6150fb5e3ee2115334310681e88d277e23d24edd2665a5f544ab3`.
Header: `ram console header, hw_status: 1, fiq step 17.` The full original
97-line dump is retained, including its truncated binary tail. The warning
backtrace is reproduced without eliding frames:

```
[  526.296293] (1)[5647:Binder_2]BUG: scheduling while atomic: Binder_2/5647/0x00000003

[  526.296321] (1)[5647:Binder_2]Modules linked in: mali_r4p0 bf0e4000 (O) swap_accounting_guard bf0e0000 (O) icx_usbscsi bf0d1000 (PO) icx_usbconn bf0cc000 (O) icx_carddet bf0c7000 (O) wm_key bf0c2000 (O) mtk_stp_bt_soc bf0b3000  mtk_stp_wmt_soc bf015000  mtk_wmt_detect bf00d000  icx_nvp_emmc bf000000 (PO)
[  526.296382]Backtrace: 
[  526.296416] (1)[5647:Binder_2][<c0013ae4>] (dump_backtrace+0x0/0x150) from [<c07b8778>] (dump_stack+0x20/0x24)
[  526.296424] r6:c72eda80 r5:00000001 r4:c0b27c80 r3:600d0113
[  526.296453] (1)[5647:Binder_2][<c07b8758>] (dump_stack+0x0/0x24) from [<c07b93cc>] (__schedule_bug+0x5c/0x6c)
[  526.296470] (1)[5647:Binder_2][<c07b9370>] (__schedule_bug+0x0/0x6c) from [<c07c8d2c>] (__schedule+0x790/0x800)
[  526.296485] (1)[5647:Binder_2][<c07c859c>] (__schedule+0x0/0x800) from [<c07c8ddc>] (schedule+0x40/0x80)
[  526.296499] (1)[5647:Binder_2][<c07c8d9c>] (schedule+0x0/0x80) from [<c07c9b70>] (__down_write_nested+0x80/0xd8)
[  526.296513] (1)[5647:Binder_2][<c07c9af0>] (__down_write_nested+0x0/0xd8) from [<c07c9bdc>] (__down_write+0x14/0x18)
[  526.296519] r7:d80f4480 r6:0000d2ce r5:dcd359c8 r4:dcd359c0
[  526.296547] (1)[5647:Binder_2][<c07c9bc8>] (__down_write+0x0/0x18) from [<c07c7e78>] (down_write+0x18/0x1c)
[  526.296567] (1)[5647:Binder_2][<c07c7e60>] (down_write+0x0/0x1c) from [<c0348204>] (zram_slot_free_notify+0x2c/0x50)
[  526.296586] (1)[5647:Binder_2][<c03481d8>] (zram_slot_free_notify+0x0/0x50) from [<c0150250>] (swap_entry_free+0x13c/0x178)
[  526.296593] r6:ffffffff r5:ffffffff r4:c0b5e734 r3:c03481d8
[  526.296621] (1)[5647:Binder_2][<c0150114>] (swap_entry_free+0x0/0x178) from [<c0150934>] (swapcache_free+0x30/0x3c)
[  526.296628] r9:d00b9e00 r8:d38da4c4 r7:d00b9e3c r6:c0b5da20 r5:d80f4480
r4:0000d2ce
[  526.296663] (1)[5647:Binder_2][<c0150904>] (swapcache_free+0x0/0x3c) from [<c014f520>] (delete_from_swap_cache+0x58/0x64)
[  526.296669] r5:0000d2ce r4:c0ff1b40
[  526.296691] (1)[5647:Binder_2][<c014f4c8>] (delete_from_swap_cache+0x0/0x64) from [<c0150a08>] (reuse_swap_page+0x7c/0x90)
[  526.296698] r6:b8b31000 r5:00000001 r4:c0ff1b40 r3:00090039
[  526.296728] (1)[5647:Binder_2][<c015098c>] (reuse_swap_page+0x0/0x90) from [<c013e86c>] (do_wp_page+0x2b0/0x6fc)
[  526.296735] r5:ca95ccb8 r4:c0ff1b40
[  526.296757] (1)[5647:Binder_2][<c013e5bc>] (do_wp_page+0x0/0x6fc) from [<c013fdc8>] (handle_pte_fault+0x338/0x6b0)
[  526.296773] (1)[5647:Binder_2][<c013fa90>] (handle_pte_fault+0x0/0x6b0) from [<c01401e8>] (handle_mm_fault+0xa8/0xe0)
[  526.296790] (1)[5647:Binder_2][<c0140140>] (handle_mm_fault+0x0/0xe0) from [<c07cc680>] (do_page_fault+0x28c/0x430)
[  526.296805] (1)[5647:Binder_2][<c07cc3f4>] (do_page_fault+0x0/0x430) from [<c00083f8>] (do_DataAbort+0x88/0x108)
[  526.296819] (1)[5647:Binder_2][<c0008370>] (do_DataAbort+0x0/0x108) from [<c07caa58>] (__dabt_usr+0x38/0x40)
[  526.296830] (1)[5647:Binder_2]Exception stack(0xcb359fb0 to 0xcb359ff8)
[  526.296842] (1)[5647:Binder_2]9fa0:                                     b8b31080 0000000c b8b204a0 00000000
[  526.296855] (1)[5647:Binder_2]9fc0: b8b31080 8004720f 80047210 80047200 8004720f b6e94008 b8b31080 b8b20458
[  526.296867] (1)[5647:Binder_2]9fe0: b6eceb88 a47debe8 b6ebe131 b6eba00e 400d0030 ffffffff
```

Saved FIQ CPU0 context and nested-panic context (original text, invalid tail
bytes displayed as replacement characters):

```
cpu 0 preempt=2, softirq=0, hardirq=0 
cpu 0 r0->r10 fp ip sp lr pc cpsr orig_r0
00000002 00000000 0000b4de 0000b4dc d80f44dc d80f4480 000200da d80f44dc cab62000 00000040 0000a0b1 cab63d14 cab63d00 cab63d00 c07c9c00 c07c9c28 800b0113 800b0113 
cpu 0 stack [cab63d00 cab63d80]
00000000 0000a0b0 cab63d3c cab63d18 c01502dc c07c9bec c1154b80 0000a0b0 000200da c0b5da10 cab62000 00000000 cab63d4c cab63d40 c0152e34 c0150298 cab63d7c cab63d50 c014f710 c0152e24 b6d5f000 0000a0b1 000200da c4a441b8 b6d5f000 0000a0b7 00000000 0000a0b1 cab63dc4 cab63d80 c014f8e8 c014f6a8 
cpu 0 backtrace : c07c9c00, c01502dc, c0152e34, c014f710, c014f8e8, c013fde4, c01401e8, c07cc680, c00083f8, c07caa58, 


 kick=0x00000000,check=0x00000003
WDT: failed to stop other CPUs in FIQ

Qwdt at [  554.001982] 
 CPU 0, nested_panic_time=0
wd_api[0xc0b9ac94]
wd_restart[0xc03e87a8]
mt8590 only hw reboot mode test
mt6582 only hw reboot mode --
Nested panic
Previous reg:
pc: c038b7cc lr: c038b7cc psr: a00b01d3
sp: cab63cd8 ip: cab63cd8 fp: cab63cfc
r10: 00000000 r9: 00000078 r8: cab63d00
r7: 00000080 r6: 001e401d r5: 00000045 r4: 40103d97
r3: f900dc00 r2: 00000000 r1: 00000000 r0: 00000011
Current reg:
pc: c0441e44 lr: c0442d64 psr: 000b01d3
sp: cab634e0 ip: cab63518 fp: cab63514
r10: 00000000 r9: 00027804 r8: 00000008
r7: c0d20858 r6: dc6c7000 r5: 00000000 r4: 00000000
r3: cab634f8 r2: dc6c7000 r1: 00027804 r0: 00027804
,�
```

## Source and compiled-word interpretation

The addresses were resolved against the loaded boot's stock kallsyms and the
hash-verified stock `vmlinux.bin`, not inferred from names. Disassembly evidence:
`root-analysis/stock-locking-disassembly.txt`.

- Saved CPU0 PC `c07c9c28` is `_raw_spin_lock+0x48`; its return chain is
  `__swap_duplicate+0x50 -> swapcache_prepare+0x1c ->
  read_swap_cache_async+0x74 -> swapin_readahead+0x7c ->
  handle_pte_fault -> handle_mm_fault -> do_page_fault -> do_DataAbort`.
  `__swap_duplicate` at `c01502d0` derives `p->lock` as `p+0x5c`; the saved lock
  address is `d80f44dc`, hence p=`d80f4480`. The saved ticket is `0xb4de`, owner
  `0xb4dc`. Their distance is2; this differs from the earlier single-predecessor
  SwapPss dump. Do not infer the current thread identities from these tickets.
- CPU1's earlier warning shows `zram_slot_free_notify` sleeping in `down_write`
  while reached from `swap_entry_free -> swapcache_free`. Compiled
  `swapcache_free` calls `swap_info_get` at `c015091c`, `swap_entry_free` at
  `c0150930`, and only unlocks p+0x5c at `c0150938`. The callback is therefore
  called **while p->lock remains held**. This is not the vendor
  `swap_info_unlock` mismatch: this caller eventually uses the correct lock.
- Source `mm/swapfile.c:695-705` matches that ordering; callback invocation is
  at665-671. `drivers/staging/zram/zram_drv.c:586-595` takes the sleeping
  `zram->lock` rwsem before freeing a slot. Stock instruction `c0348200` calls
  `down_write`; its saved return is `c0348204`, matching the warning.
  The invalid sleeping-under-spinlock path is confirmed by source, compiled
  code and the actual scheduling warning.
- CPU1 did not provide a simultaneous FIQ register snapshot (`WDT: failed to
  stop other CPUs in FIQ`). Its warning is27.705689seconds before the FIQ.
  Its exact FIQ ownership, the rwsem holder and a complete wait cycle remain
  unobserved. Treat this path as the next blocker candidate, not a proven
  complete ABBA graph.
- Nested-panic **current** PC `c0441e44` is
  `simp_mmc_single_write+0x28`, LR `c0442d64` is
  `card_dump_func_write+0x17c`. These are the kernel panic-dump-writing context,
  not the original hang site. They do not establish an MMC or Mali root cause.

## Verified post-reset state and next gate

Root independently checked `failure-20261012T124604Z/poststate/`: boot614d990e,
SwapTotal0, swaps header only, no guard/Mali-r4 module, no container, WDK stock
`1 10 30 1`, HgrmMediaPlayer present. All nine read transports returned0 with
empty stderr. Experimental host streams were gone; no extra down/reboot, no
retry and no new patch were issued. This verifies stock process state, not a
visual screenshot of the panel.

The existing two-site module is **not sufficient for the 30-minute gate**.
Keep no-zram as the already measured avoidance. The next RAM-only experiment
needs an atomic-safe/deferred zram free-notification design, including slot
reuse and swapoff lifetime, source/host verification and reviewer clearance
before another load. Simply suppressing the callback would retain compressed
pages until overwrite/reset (`zram_bvec_write` frees old slot contents at
`zram_drv.c:260-262`); it is not a production-quality fix and was not executed.
Permanent kernel/boot writes remain prohibited. No module source was changed
in this failed-run analysis; the previous full host gate remains300/0, while the
actual device gate is FAILED.

## Reviewer follow-up: all five atomic-sleep warnings, two entry paths

The review message saying the run was still reset-free refers to an earlier
point in this same run. By the time it reached root, the watchdog/FIQ reset and
dump-first capture above had already occurred. Continuing that loaded boot is
impossible without a new run; no new run, manual stop, reboot or patch was
issued in response. The failed gate and the two independent failure modes stay
separate: observed vendor-accounting mitigation is not zram stability.

Root counted the original `kmsg.out`, not the short last_kmsg tail. All five
literal `BUG: scheduling while atomic` events have preempt_count `0x00000003`:

| Kernel uptime (s) | Since zram (s) | CPU | PID | Thread | Swap caller |
|---|---|---|---|---|---|
| 413.044165 | 174.253768 | 1 | 4915 | Binder_4 | swap_free |
| 436.608163 | 197.817766 | 0 | 3879 | pool-6-thread-1 | swap_free |
| 474.309138 | 235.518741 | 1 | 3400 | om.amazon.tcomm | swapcache_free |
| 474.390617 | 235.600220 | 1 | 3434 | Binder_1 | swapcache_free |
| 526.296293 | 287.505896 | 1 | 5647 | Binder_2 | swapcache_free |

They share `swap_entry_free -> zram_slot_free_notify -> down_write -> schedule`,
but their **complete backtraces are not identical**. The first two come directly
through `swap_free` from `handle_pte_fault`; the other three come through
`do_wp_page -> reuse_swap_page -> delete_from_swap_cache -> swapcache_free`.
All five complete warning/exception-stack blocks, without removed frames, are
preserved in `root-analysis/atomic-sleep-traces.txt` (161 normalized text lines);
`atomic-sleep-events.json` records source line numbers and complete frame chains.
The original kmsg bytes remain unchanged, SHA256
`fe7cd3d608399a987c1a0c75507d6bf0459faec117c6186a29d786d185ee1c8f`.

Both paths retain the swap spinlock across the callback: `swap_free` source
`mm/swapfile.c:681-689`, compiled call `c015056c`, unlock `c0150574`; the
`swapcache_free` ordering was verified above. The page-fault paths also retain
PTE locking: `mm/memory.c:3104,3145,3172` for swap-in, and `3732-3739,2656-2669`
for write-protect/reuse. `swap-free-disassembly.txt` preserves the added stock
instruction evidence. This confirms the separate sleep-in-atomic bug, not the
missing rwsem-holder identity or a complete simultaneous wait graph.

Reproducible host-only evidence check:

```
python3 work/android/patched-zram-20261012T123915Z/root-analysis/check-atomic-sleep.py
PASS: 5 warnings; preempt_count=0x3 in all 5; swap_free=2; swapcache_free=3; complete exception stacks=5
check_exit=0
```

A future reset-free run with those warnings must still be reported as
"accounting-path mitigation observed; slot_free_notify atomic-sleep bug
unresolved", never "zram stable". This run instead has the stronger negative
outcome: **30-minute gate FAILED with a reset**. Notify-pointer suppression,
trylock substitution or deferred freeing remain separate RAM-only design/review
work after the gate; none was executed. Only this journal was changed. No new
build/test gate was run for unchanged module code; the command above validates
the requested log enumeration, not a kernel fix.

## Reviewer mapping agreement and next design gate

Root rechecked the raw saved registers and loaded-boot kallsyms. CPU0's saved
PC `c07c9c28` is `_raw_spin_lock+0x48`; the backtrace entry `c07c9c00` is the
same function+0x20. `Previous reg` PC/LR `c038b7cc` is
`aee_wdt_irq_info+0x11c`, the FIQ handler itself, not an earlier application
hang site. CPU0 r4=`d80f44dc` and r5=`d80f4480` match p+0x5c and p; the earlier
CPU1 Binder_2 warning carries this same p in r5/r7. Its normal `swapcache_free`
unlock pair is unrelated to the vendor accounting mismatch. `kick=0x0,
check=0x3` and Qwdt554.001982 are recorded; the warning-to-FIQ interval
27.705689seconds is consistent with the configured30-second watchdog timeout,
not an exact measurement of the lockup onset.

Reviewer/Codex attribution for this reset is the second failure mode:
**zram free notification sleeping under the swap spinlock**. The common lock
address, atomic-sleep warning and subsequent CPU0 lock wait directly support
that attribution. Simultaneous CPU1 FIQ state, the rwsem holder and the exact
reason the full system could not progress remain unobserved. No complete ABBA
cycle or owner-thread identity is asserted. The accounting guard's intended
behavior was observed in the positive smaps snapshot, and none of the five BUG
traces goes through the smaps accounting path; this does not turn the failed
30-minute gate into a pass.

The next proposal is `probe/kmod/swap-accounting-guard/NOTIFY-GUARD-DESIGN.md`:
one combined three-site transaction, preserving the two accounting guards and
replacing the zram callback entry with `bx lr`. Root's offline check verifies
unique stock symbol `c03481d8`, word `e1a0c00d` (`mov ip, sp`) and proposed word
`e12fff1e` (`bx lr`):

```
.venv/bin/python work/android/patched-zram-20261012T123915Z/root-analysis/verify-notify-proposal.py
PASS: exact stock callback symbol/word; proposed word decodes bx lr; same swap_info_struct in both traces; Previous reg maps to FIQ handler
NOT RUN: proposed ARM instruction execution, three-site module build/tests, device load or gate
verify_exit=0
```

The proposal deliberately retains unused compressed slot data until overwrite
or reset. Its memory cost must be measured, and it is a temporary diagnostic
bypass, not a production repair. The design, source/host/ARM-execution and
artifact review gates precede any device execution. No module code, collector,
binary or device state was changed in this phase. The two-site DESIGN status
was corrected to the actual reviewed-load/failed-run state; old "not yet
loaded" and pending-C/D claims were stale.

## Next mitigation revised to synchronous trylock/skip

Claude's subsequent review favors an in-place trylock, retaining synchronous
free on success and returning immediately on contention. The proposal in
`NOTIFY-GUARD-DESIGN.md` now uses that approach instead of the earlier whole
callback `bx lr`. No deferred queue is introduced: freeing the current handle
by index after reuse could delete newly written data. No new module source,
upload/load, reboot or device experiment occurred in this revision.

Root independently read the matching public/generic rwsem source and decoded
the stock wrapper/helper. `down_write_trylock c007bc4c` calls
`__down_write_trylock c02c55ec`, whose actual words return1 and store activity=-1
when previously0, otherwise return0 without changing activity. The proposed
calls and conditional epilogue were generated and decoded. Ten tail words are
checked; nine change, the original c0348224 epilogue remains validation-only.
The design additionally guards the original prologue; no stores or rollback
stores to validation-only words are allowed. Module implementation and its
full isolated make verify remain open.

Root also executed the candidate ARM words under the existing QEMU Docker
image, with no network/device mounts or capabilities. Exact stock public/
inner rwsem words execute after one common relocation. IRQ primitives and
mcount are stubbed; free is a held-lock/argument-checking stub; up_write executes
its empty-waiter stock path. This is control-flow/ABI evidence, not real kernel
concurrency, zsmalloc, cache/permission or watchdog proof.

The first assembler invocation failed, not silently passed:

```
notify-test.S: Assembler messages:
notify-test.S:61: Error: constant expression expected -- `ldr r2,=pass_end-pass_text'
notify-test.S:126: Error: constant expression expected -- `ldr r2,=fail_end-fail_text'
```

The ARM4.8 assembler could not use those forward label differences in the LDR
pseudo-immediate. Named literal words now hold the differences; no suppression
was used. Assembly/link then returned0 with empty diagnostics. Reproducible
command and results:

```
bash work/android/patched-zram-20261012T123915Z/root-analysis/trylock-arm/run-checks.sh
stock/encoding exit=0
stock as_exit=0; ld_exit=0; run_exit=1
FAIL: ARM callback/return/callee argument/state check
trylock as_exit=0; ld_exit=0; run_exit=0
PASS: ARM stock trylock 1/0; callback 5 cases; ABI preserved; free/unlock only on success; no blocking down_write or stats call
PASS: 3 real-instruction trylock return scenarios + 5 callback scenarios; negative stock control failed as expected
```

The stock callback is an intentional negative control: the harness fails entry
into blocking down_write. All8 positive ARM scenarios ran; this is not a
zero-test or disassembly-only result. Full sources, words, binaries and outputs
are in `root-analysis/trylock-arm/`. No live kernel instructions were written.

The warning count5 cannot estimate all contention/skip events or retained RAM.
The proposed gate records signed orig_data_size-minus-active-Swap bytes with
its initial baseline, zero-page context, actual pool memory, MemFree and LMK.
Zero pages, the header and snapshot skew prevent claiming that difference is
exact leakage. The lost notify_free statistic is not a skip counter. A future
30-minute run requires zero atomic-sleep warnings as well as the existing real
swap/smaps/continuity criteria. Design and source/artifact review remain gates
before device action; the failed run is not retried here.
