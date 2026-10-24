# swap-accounting-guard: design

RAM-only runtime kmod guarding the stock swap-lock mismatch at two accounting
call sites. The reviewed two-site artifact was loaded and exercised on the
device, but the patched+zram gate failed with a separate zram free-notification
atomic-sleep bug. This document describes the two-site code, not a stable-zram
result. See `devlog/2026-10-12-patched-zram-reset.md` and the unimplemented,
review-gated extension in `NOTIFY-GUARD-DESIGN.md`.

## The defect

`swap_info_get()` takes `p->lock` (`mm/swapfile.c:576`) and
`swap_info_unlock()` releases the global `swap_lock` (`mm/swapfile.c:594-597`).
Two callers take the first and release the second:

- `fs/proc/task_mmu.c:539-546`, the smaps walk behind `/proc/<pid>/smaps`;
- `fs/proc/page.c:107-114`, `kpageswapn_read`, behind `/proc/kpageswapn`.

The per-device lock therefore leaks on the first swapped PTE and the next taker
spins. The FIQ dump caught exactly that: `pc = _raw_spin_lock+0x48` reached from
`swap_info_get` through `smaps_pte_range`, with the lock's owner ticket `0x32d1`
one BEHIND the waiter's `0x32d2` — one outstanding acquisition, no intervening
waiter. The owning *thread* is not captured; the owner *ticket* is.

With no swap configured the path is never reached, which is why the stock device
is unaffected and why the no-zram control run survived its 30-minute window.

## Method

Two narrow call-site guards, not a change to the shared unlock: `p` is not a
parameter of `swap_info_unlock()`, and only these two callers are
compiled-verified. Each site's `bl swap_info_get` becomes `mov r0, #0`, so the
null check already after the call takes the branch it already had.

```
smaps_pte_range   c01acabc + 0xd4 = c01acb90: ebfe8e2c  bl 0xc0150448
                  c01acb98: e3500000  cmp r0, #0
                  c01acb9c: 0a00000b  beq 0xc01acbd0   -> next PTE
kpageswapn_read   c01b996c + 0x38 = c01b99a4: ebfe5aa7  bl 0xc0150448
                  c01b99a8: e3500000  cmp r0, #0
                  c01b99ac: 0a000020  beq 0xc01b9a34   -> printk; mvn r0,#13
replacement       mov r0, #0 = e3a00000
```

Consequences: smaps keeps accumulating `mss->swap` (before the call) and only
loses the vendor `SwapPss` figure; `/proc/kpageswapn` returns `-EFAULT`, because
the branch it now takes is the existing error path.

`remove_proc_entry()` on the two files was rejected: it must cope with
already-open files and with a partial removal, and it removes the interfaces
instead of degrading them.

## Load-time gates

The first four gates run before any page is made writable. The final gate
revalidates the whole set inside the stopped context, after permissions are
changed but before the first text write. These refusals write no text.

| gate | failure |
|---|---|
| `swap_info_get` resolves to `c0150448`, `smaps_pte_range` to `c01acabc`, `kpageswapn_read` to `c01b996c`, `v7_coherent_kern_range` to `c001ec80`, `init_mm` at `c0b5d840` | load refused, `-ENOENT` |
| CPU is the expected Cortex-A7: `(read_cpuid_id() & 0xfffffff0) == 0x410fc070` | `-ENODEV` |
| `si_swapinfo()` reports `totalswap == 0` | `SG_TXN_SWAP_ACTIVE` |
| both sites hold exactly the stock words `ebfe8e2c` and `ebfe5aa7` | plan rejected, no writes |
| after the pages are made writable, the whole set is re-read and re-planned | `SG_TXN_REVALIDATE`, no writes |

The planner (`swap_guard_plan.h`) has no "already patched" success path: an
altered word is a rejection, because the guard is defined against the original
kernel.

## Write path

`patch_text()` is not called. It would call
`stop_machine(patch_text_stop_machine, &patch, cpu_online_mask)` if
`cache_ops_need_broadcast()` were true, and that callback writes on every
selected CPU. On this kernel the condition is a compile-time zero
(`arch/arm/include/asm/smp_plat.h:37-38`, ARMv7), but the module does not depend
on that. It performs the steps itself:

1. `set_memory_rw(page, 1)` — `EXPORT_SYMBOL`, `arch/arm/mm/rodata.c:109-119` —
   outside any stopped context, because `set_page_attributes()` (`:80-104`) ends
   in `flush_tlb_kernel_range()`, a cross-CPU TLB shootdown.
2. `stop_machine(fn, data, NULL)` for a single writer:
   `kernel/stop_machine.c:418-419` runs the callback on the first online CPU
   with the others stopped.
3. Inside the callback: revalidate the whole set, then `probe_kernel_write()`
   (`EXPORT_SYMBOL_GPL`, `mm/maccess.c:49-62` — the store `__patch_text()`
   performs) and `v7_coherent_kern_range()` (`c001ec80`), which is the V7
   implementation of `cpu_cache.coherent_kern_range`, i.e. what
   `flush_icache_range()` calls on this CPU, and which issues no IPI. The
   backend is the concrete exported symbol because `flush_icache_range` is a
   macro and `cpu_cache` does not appear in this kernel's symbol table, so the
   header route cannot be import-validated.
4. Read every word back; a write that reported failure is rejected even if the
   store appears to have landed, because a backend that returns before its
   cache maintenance is not trustworthy.
5. Roll back in place, still inside the single writer and still writable, for
   every attempted site, verifying each rollback by reading it back.
6. `set_memory_ro(page, 1)` outside the stopped context, on every path.

No printk, allocation or locking happens inside the stopped callback; the
evidence is logged after it returns.

## Permission verification

`set_memory_rw()` returning 0 is not proof: `set_page_attributes()` skips a
non-table PMD and still returns 0. Permissions are therefore checked by walking
the page tables (`swap_guard_perm.h`): the PMD must be a coarse table, the PTE
must be present, and the write bit is read from it. Never by storing a value
into the text — that could write the old word back and undo the guard.

## Evidence recorded

Each site records its address, the word found, the word written, what was read
back, and whether each step succeeded (`struct sg_site_result`), plus counters
for writes, rollbacks and page-permission changes. The module logs all of it
after the transaction returns, so an unread or unverified word is never
described as verified.

## Recovery and limits

There is no `module_exit`: the guard cannot be unloaded, so the vulnerable
accounting cannot be restored under a live swap. **Reboot is the recovery.**

- `/proc/kpageswapn` fails while armed; smaps loses `SwapPss`.
- The guard must be loaded before swap; loading after swap is refused.
- It patches by exact stock word, so any other build fails the plan.
- It does not repair the lock mismatch, so untraced callers of the same pair
  remain reachable in principle.
- Host tests exercise the planner, the transaction and the permission walk
  through mocks and a shim; they are not device proof. The actual two-site run
  exercised real swap and smaps but failed the 30-minute watchdog gate.

## Stock export metadata

The warning-free build rests on a `Module.symvers` derived from the stock
kernel, so that metadata is gated too:

- `tools/gen-stock-symvers.sh` validates the input (present, readable,
  non-empty, well-formed 32-bit bounds, 8-byte aligned, ordered,
  non-overlapping, complete pairs) and every marker address before classifying
  anything; it writes a temporary file and renames it only after all checks
  pass, so a failure preserves the previous output.
- GPL categories come from the section a marker's entry address falls in:
  4037 `EXPORT_SYMBOL` and 2473 `EXPORT_SYMBOL_GPL` in this stock kernel.
- `tools/verify-stock-exports.py` decodes the real `__ksymtab` arrays from
  `vmlinux.bin` and checks each decoded value against the kallsyms address for
  that name, so a zeroed or retargeted value is rejected. Three `mtk_wcn`
  markers sit outside the standard kernel tables; they are reported, not
  trusted.

## Build

```
make          # KDIR=$HOME/.cache/nw-kernel/linux-kernel-3.10.26, ARM 4.8
make check    # unit suites plus the metadata suite
make verify   # build, then the suites, then import verification of the .ko
```

`KBUILD_MODPOST_WARN` is deliberately not set. Generated metadata and binaries
are not committed.

## Adapter results versus load result

`sg_run` returns the `SG_TXN_*` codes above. `guard_init` maps any
non-`SG_TXN_OK` result **from `sg_run`** to `-EINVAL` for the module loader;
symbol and CPU resolution errors happen before `sg_run` and retain their own
errno (`-ENOENT`, `-ENODEV`). So for the transaction itself the errno is not a
per-gate value: the specific reason is in the transaction result and in the
logged counters, not in the errno. The gates split into two phases, and the split is deliberate:

- **Pre-write phase** (before any page is made writable): symbol resolution,
  CPU check, the swap snapshot, and the initial plan. Nothing can have been
  modified when these fail.
- **Stopped phase** (pages already writable, single writer): the whole set is
  re-read and re-planned before the first write, so a site that changed after the
  initial plan is refused with `SG_TXN_REVALIDATE` and nothing is written.

## Device gate outcome

The Claude reviewer and Codex checks passed; the reviewed e3bcecd artifact
(SHA256 f73c9981237c76469a60961789fa65a44041d728492bd69fab05fadcbe40ca9e)
loaded in two fresh, zero-swap-history boots. The second load preceded the
original zram workload with capture and kmsg already active. The run had real
swap use and a complete positive smaps scan but reset before 1800 seconds from
Adding-swap. Its last_kmsg was captured first. The device gate is **FAILED**;
there is no authorization to deploy this as a stable zram solution.

The original ~102-second accounting-lock freeze and the later zram notification
atomic-sleep failure are distinct. The next RAM-only proposal is review-gated
in `NOTIFY-GUARD-DESIGN.md`; no three-site module has been implemented or run.
Permanent writes to boot, system, recovery or raw storage remain prohibited
until a separately confirmed brick-recovery path exists. Reboot remains the
RAM patch's recovery, not proof of recovery from a damaged flash image.
