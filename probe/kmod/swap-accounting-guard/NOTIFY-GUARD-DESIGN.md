# Non-blocking zram notification: RAM-only diagnostic proposal

Status: **design awaiting Claude reviewer clearance**. This revision adopts
synchronous trylock/skip in place of the earlier whole-callback return.
No new kmod implementation, upload, load or device run has occurred. Permanent
kernel/boot writes remain prohibited; user approval remains RAM-only.

## Separate defect and chosen mitigation

The accounting guard loaded and exercised its intended smaps behavior, but the
30-minute gate reset. Five atomic-sleep warnings share
`swap_entry_free -> zram_slot_free_notify -> down_write -> schedule`. CPU0's
saved lock wait and the preceding CPU1 warning involve the same
swap_info_struct d80f4480. See `devlog/2026-10-12-patched-zram-reset.md` for full
traces, time mapping and the unobserved simultaneous CPU1/rwsem-holder state.

Use `down_write_trylock`: if it succeeds, free synchronously and release the
rwsem; if it fails, return without freeing or releasing an unacquired rwsem.
No queue, delayed work or later free is introduced. `zram_free_page` reads the
handle currently stored at an index; an index-only delayed free could otherwise
run after reuse and delete replacement data. The immediate skip leaves old
contents until the next write's existing synchronous old-handle cleanup.

The five warnings count recorded schedules in atomic context, **not every
rwsem contention or the proposed trylock's future failures**. They cannot bound
skip count, retained bytes or future contention frequency.

## Stock facts and proposed instruction words

Stock image base c0008000, SHA256
`71f050c3d24067ad86ad3e8ea4d4712fd723e22147e002d71107faa8cefef2ed`;
loaded-boot symbols from `guard-load-20261012T123543Z/boot_kallsyms.out`:

- zram_slot_free_notify c03481d8;
- down_write_trylock c007bc4c, __down_write_trylock c02c55ec;
- zram_free_page c03480e8;
- up_write c007bc84, __up_write c02c568c.

`down_write_trylock`'s stock wrapper calls `__down_write_trylock` and returns
its result. The compiled helper returns 1 and stores activity=-1 when activity
was0, otherwise returns 0 without changing activity. It takes/restores the
internal IRQ-save spinlock, never calls schedule. Source agrees:
`kernel/rwsem.c:55-67`, `lib/rwsem-spinlock.c:231-249`.

Keep the two existing accounting patches. Preserve the callback's original
prologue and r4=zram, r5=&zram->lock, r6=index setup. Rewrite its tail in place:

| Address | Exact stock word | Proposed word | Proposed instruction |
|---|---|---|---|
| c0348200 | eb11ff16 | ebf4ce91 | bl c007bc4c (down_write_trylock) |
| c0348204 | e1a01006 | e3500000 | cmp r0, #0 |
| c0348208 | e1a00004 | 089da878 | ldmeq sp, {r3,r4,r5,r6,fp,sp,pc} |
| c034820c | ebffffb5 | e1a01006 | mov r1, r6 |
| c0348210 | e1a00005 | e1a00004 | mov r0, r4 |
| c0348214 | ebf4ce9a | ebffffb3 | bl c03480e8 (zram_free_page) |
| c0348218 | e1a00004 | e1a00005 | mov r0, r5 |
| c034821c | e2841070 | ebf4ce98 | bl c007bc84 (up_write) |
| c0348220 | ebffffa0 | e89da878 | ldm sp, {r3,r4,r5,r6,fp,sp,pc} |
| c0348224 | e89da878 | e89da878 | retained original epilogue, validation only |

This is **ten checked tail words, nine changed words**. The original final
word remains unreachable in both new paths and must not be dropped from the
stock check. Both epilogues use the stock saved-register/stack layout. The
caller's normal p->lock release remains unchanged; the rewrite removes only
the callback's `notify_free` increment, not free-page accounting on success.
No I/O-path change or global unlock change is proposed.

## Host ARM execution evidence and limits

Artifacts: `work/android/patched-zram-20261012T123915Z/root-analysis/trylock-arm/`.
Stock-word comparison and Capstone decode confirm all ten tail words, the nine
changes and BL targets. A bare ARM ELF then runs under existing
`nw-qemu-arm:1` (image e95c52d5...), without network, capabilities or USB/device
mounts. All kernel code is relocated by one common bias, preserving the exact
PC-relative instruction words.

The harness executes the real stock trylock wrapper/helper and the real
up_write wrapper/helper (empty waiter list). IRQ-save/restore and mcount are
stubbed, zram_free_page is an argument/held-lock checking stub, and entry into
the old blocking down_write or removed statistic call fails the test.
Three direct trylock scenarios verify success1/count=-1 and reader/writer
contention0/unchanged count. Five callback scenarios cover free, read-held,
write-held, multiple readers and another free at a different index. They check
callee-saved registers, SP, index/zram arguments and rwsem release only after
success. The stock callback is a negative control and fails the forbidden-call
check; the proposed callback passes.

```
bash work/android/patched-zram-20261012T123915Z/root-analysis/trylock-arm/run-checks.sh
stock/encoding exit=0
stock as_exit=0; ld_exit=0; run_exit=1 (expected negative control)
trylock as_exit=0; ld_exit=0; run_exit=0
PASS: 3 real-instruction trylock return scenarios + 5 callback scenarios; negative stock control failed as expected
```

This verifies ARM control flow/ABI, **not real IRQ/spinlock concurrency,
nonempty-waiter wakeup, zsmalloc freeing, text permission/cache behavior or a
working kmod**. These remain source/device gates, not inferred from emulation.

## Retention cost and measurement

Only skipped notifications retain objects or zero flags. The next write to
that index already frees old contents (`zram_drv.c:260-262`), and reset frees
remaining contents. Retention is limited by the fixed slot table; do not call
this a 256-MiB total-physical-memory bound. Fragmentation and metadata add costs.
This is a temporary diagnostic mitigation, not a production repair; no
persistence or autoload.

Record signed `orig_data_size - SwapUsed_kB*1024` as requested, plus its baseline
just after swapon. It is a **retained-slot proxy, not exact leaked bytes**:
`orig_data_size` counts nonzero stored objects, whereas SwapUsed includes active
zero-filled slots; the swap header and sequential-read timing also affect it.
Record `zero_pages` and the corresponding
`orig_data_size + zero_pages*4096 - SwapUsed_kB*1024` context, without hiding
negative values. Record `compr_data_size`, `mem_used_total`, MemFree and LMK.
`mem_used_total` is zsmalloc pool bytes, not every metadata allocation. Source:
`zram_drv.c:105-130`, `zram_sysfs.c:182-214`.

The removed `notify_free` counter can no longer count successful notifications
or skips. Do not use it to estimate either. Runtime sysfs availability/formats
have not been measured in this phase; capture statuses and never invent zeros.

## Fail-closed transaction and review gates

1. Review this design before implementation/deployment. Source/host/artifact
   review must precede **all device action** for the new module.
2. Validate the entire 20-word stock callback, including its unchanged ten-word
   prologue, plus the two accounting words, before writable permissions and
   again inside stop_machine before the first write. All exact addresses and
   words must match; already-patched or unreadable input rejects all writes.
3. Write only the 11 changed words (nine callback, two accounting). Unchanged
   prologue/final words are explicit validation-only guards: no store or
   rollback store, no misleading patched1. They remain in both validation
   phases. Their mutation/failure must be covered by negative host tests.
4. Keep fault-safe access, a single stopped writer, readback, cache maintenance,
   rollback of every attempted changed word, and RO verification on every
   path. All write words occupy three distinct pages. Report permission
   counters truthfully if the engine counts per-word operations rather than
   distinct pages. Require writes11/11, exact readbacks, all three pages RO,
   no rollback/failure, stopped1/ran1 and explicit validation-only evidence.
5. Require fresh boot and whole dmesg with no historical swapon, SwapTotal 0,
   no old guard/container. No patching a task sleeping in old down_write:
   its return address lies inside the rewritten sequence. Never sanitize a
   previously swapped boot with swapoff. Uncertain load states require reboot
   before retry; no same-boot retry, force load or unload.
6. Resolve/validate the existing five symbols and callback/trylock/free/up_write
   targets against the stock addresses above. The new artifact needs its own
   source commit/hash, pullback comparison, vermagic and stock-import checks;
   f73c9981 is the old two-site artifact, not this proposal.
7. Implement negative cases first, including every guarded-word mismatch,
   stopped revalidation, partial writes/readbacks and rollback/RO failures.
   Run isolated full make verify, warning-free ARM build and frame/stack-size
   checks, then commit source/tests/journal for reviewer clearance. Existing
   two-site transaction code does **not** yet support these new write/guard
   semantics; no full module build or host gate has run for this proposal.

## Device gate after source/artifact review

Same workload, warmed images, FIQ settings and 1800 actual seconds from Adding
swap. Start capture and kmsg before up; cheap samples every 30 seconds and smaps
on every fifth sample. Require real swap use, two distinct stable positive
smaps scans and **zero scheduling-while-atomic warnings** in the full observed
kmsg. Any warning fails safety classification even without a reset, but no
automatic mid-run patch/reboot is introduced. Count time/thread/path and retain
reset last_kmsg before recovery. No automatic up/swapon retry.

Add only small status-checked zram memory readouts alongside existing capture,
not another collector redesign or synthetic load. Distinguish retention/LMK/OOM
from hangs. A passing run would support this synchronous temporary mitigation,
not establish zram generally stable or justify permanent deployment. Restore
and verify stock state afterward. No firmware/raw-partition writes in any phase.
