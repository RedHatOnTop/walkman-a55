# swap-accounting-guard: RAM-only kmod for the stock swap lock mismatch (2026-10-12)

Goal: prepare a RAM-only runtime kmod that closes the two faulty swap accounting
call sites, with source-confirmed design, host tests before implementation, and
a build validated against the stock kernel. Recovery is a reboot. Permanent
bootimg/system/recovery/raw-storage writes stay forbidden until a separately
confirmed brick-recovery path.

**Status: source + host tests + build complete, committed for review. Nothing
was uploaded, loaded, or exercised on the device in this turn.**

## Phase A: source-confirmed design

Confirmed the defect and the exact patch points from source and from the stock
binary (base `_text = 0xc0008000`):

- `mm/swapfile.c:576` `swap_info_get()` ends `spin_lock(&p->lock)`;
  `mm/swapfile.c:594-597` `swap_info_unlock()` is `spin_unlock(&swap_lock)`.
- `fs/proc/task_mmu.c:539-546` (smaps) and `fs/proc/page.c:107-114`
  (`kpageswapn_read`) take the first and release the second.
- Stock words, disassembled from `nwa50-extracted/kernel/vmlinux.bin`:
  `c01acb90 = ebfe8e2c` (`bl 0xc0150448`, smaps) and
  `c01b99a4 = ebfe5aa7` (`bl 0xc0150448`, kpageswapn); both followed by
  `cmp r0,#0` and a `beq`. Replacement `mov r0,#0 = e3a00000`.
- Branch targets: smaps' `beq` lands at `c01acbd0` (`add sl, sl, #4096`, next
  PTE) so both the get and the mismatched unlock are skipped; kpageswapn's
  `beq` lands at `c01b9a34` (`printk`, then `mvn r0,#13` = `-EFAULT`).

Method: two narrow call-site guards. A change to the shared
`swap_info_unlock()` was rejected because `p` is not a parameter there and only
these two callers are compiled-verified. `remove_proc_entry()` was rejected
because of already-open files and partial-removal rollback. Degradations are
recorded: smaps loses only `SwapPss` (it accumulates `mss->swap` before the
call), and `/proc/kpageswapn` returns `-EFAULT`.

Write path, source-justified: `set_memory_rw()` (exported,
`arch/arm/mm/rodata.c:109-119`) outside any stopped context because
`set_page_attributes()` (`:80-104`) ends in a cross-CPU
`flush_tlb_kernel_range()`; then `probe_kernel_write()` (GPL-exported,
`mm/maccess.c:49-62`), which is the store `__patch_text()` performs; then
`flush_icache_range()` (`arch/arm/include/asm/cacheflush.h:279` =
`__cpuc_coherent_kern_range`, `arch/arm/mm/cache-v7.S:255-299`); then readback;
then `set_memory_ro()`. `patch_text()` itself was not used: it calls
`stop_machine(..., cpu_online_mask)` when `cache_ops_need_broadcast()` is true,
and that callback writes on every selected CPU. On this kernel the condition is
a compile-time zero (`arch/arm/include/asm/smp_plat.h:37-38`, ARMv7), but the
module does not rely on it and contains no `stop_machine` at all.

## Phase B: host tests before implementation

`tests/test_plan.c` compiles the module's real planner (`swap_guard_plan.h`) for
the host. The test was written first and observed to fail (planner absent:
`fatal error: ../swap_guard_plan.h: No such file or directory`), then the planner
was added.

```
$ ./tests/run-tests.sh          # unpiped
rc=0, ALL PASS, 17 PASS / 0 FAIL
```

Cases: stock/stock -> PATCH (need 2); first-site wrong word -> BAD_WORD;
second-site wrong word -> BAD_WORD; both already patched -> ALREADY (need 0);
half patched -> BAD_WORD; read failure at either site -> BAD_READ; null/short/
missing arguments -> BAD_ARG; plus the site table's own offsets and words.
Every non-PATCH, non-ALREADY result means zero writes.

## Phase C: build

```
$ make                          # unpiped
build rc=0
  WARNING: Symbol version dump .../Module.symvers is missing;
           modules will have no dependencies and modversions.
  CC [M]  swap_accounting_guard.o
  MODPOST 1 modules
  LD [M]  swap_accounting_guard.ko
$ ls -la swap_accounting_guard.ko
-rw-r--r-- 1 person person 99760 ... swap_accounting_guard.ko
```

That warning is the known absent `Module.symvers` (`CONFIG_MODVERSIONS` is off);
`KBUILD_MODPOST_WARN` was deliberately not set, so an unresolved import would
fail the build rather than warn.

Imports validated against the stock kernel's own export markers rather than
against a symvers file. This kernel carries per-symbol `__ksymtab_<name>`
entries in `/proc/kallsyms` (6521 of them), which is a definitive export check:

```
__gnu_mcount_nc      EXPORT_SYMBOL
kallsyms_lookup_name EXPORT_SYMBOL
printk               EXPORT_SYMBOL
probe_kernel_read    EXPORT_SYMBOL
probe_kernel_write   EXPORT_SYMBOL
set_memory_ro        EXPORT_SYMBOL
set_memory_rw        EXPORT_SYMBOL
si_swapinfo          EXPORT_SYMBOL
__stack_chk_fail     EXPORT_SYMBOL
__stack_chk_guard    EXPORT_SYMBOL
```

The three instrumentation symbols (`__gnu_mcount_nc`, `__stack_chk_fail`,
`__stack_chk_guard`) are also imported by the known-good Mali module that loads
on this device. No `stop_machine` import is present. Artifact check:
`ELF 32-bit LSB relocatable, ARM`, vermagic `3.10.26 SMP preempt mod_unload
ARMv7`, matching the running `Linux version 3.10.26 ... #1 SMP PREEMPT`. The
`.rodata` dump shows the site table as `0xd4 / ebfe8e2c / e3a00000` and
`0x38 / ebfe5aa7 / e3a00000`.

Load-time gates, all before any writable page: symbol resolution
(`-ENOENT`), `si_swapinfo().totalswap == 0` (`-EBUSY`), the plan
(`-EINVAL`), revalidation of the whole set after the pages are made writable
(`-EAGAIN`), write + readback with rollback (`-EIO`). Pages are restored to
read-only on every path. There is no `module_exit`, so the guard cannot be
unloaded; reboot is the recovery.

## Limits, stated plainly

- This is a shell/kmod artefact prepared for review, not a verified fix: it has
  never run on the device.
- It patches by exact stock word, so any other build fails the plan and writes
  nothing.
- It does not repair the lock mismatch itself; untraced callers of the same pair
  would remain reachable in principle.
- `/proc/kpageswapn` fails while armed, and smaps loses `SwapPss`.
- The guard must be loaded before swap; loading after swap is refused.

## Remaining gates before device use (not authorized)

User review first. Then a device run with the guard loaded **before** zram, and
the run is only valid if `SwapTotal > 0` **and** `SwapUsed > 0` are observed and
repeated successful `/proc/<pid>/smaps` reads are documented, over the same
30-minute window, compared against the ~102 s post-zram freeze. Memory/LMK
failures must be distinguished from a kernel/WDT reset, and raw evidence kept.
No upload, insmod, zram enable, or patched-device experiment before that.

## Files

- `probe/kmod/swap-accounting-guard/DESIGN.md` — the design note above
- `swap_accounting_guard.c`, `swap_guard_plan.h` — module and shared planner
- `tests/test_plan.c`, `tests/run-tests.sh` — host tests
- `Kbuild`, `Makefile`, `.gitignore` — build; artifacts are not committed

## Revision after review (superseded by later commits; kept as history)

Seven review points were addressed before this commit:

1. `flush_icache_range` is a macro, not a runtime symbol. The header route
   resolves to `cpu_cache.coherent_kern_range` under `MULTI_CACHE`, and
   `cpu_cache` is absent from this kernel's `/proc/kallsyms`, so it cannot be
   import-validated. The module uses the concrete exported backend
   `v7_coherent_kern_range` (stock `c001ec80`) and additionally refuses to load
   unless the CPU is the expected Cortex-A7.
2. Exact addresses are now required: `swap_info_get` `c0150448`,
   `smaps_pte_range` `c01acabc`, `kpageswapn_read` `c01b996c`. Already-patched
   words are rejected: the guard is defined against the ORIGINAL words.
3. Rollback now covers the site whose write landed but whose readback failed
   (attempted is set before the write), and the rollback is read back and
   verified; an unverifiable rollback is reported, never claimed as restored.
4. Permission changes are verified behaviourally, because `set_memory_rw()`
   returning 0 is not proof (`set_page_attributes()` skips a non-table PMD and
   still returns 0). After `set_rw` a probe write of the existing value must
   succeed; after `set_ro` it must fail.
5. `stop_machine(fn, data, NULL)` gives one writer
   (`kernel/stop_machine.c:418-419`). Permission/TLB changes and the
   `si_swapinfo()` snapshot stay outside the stopped context; inside it there is
   no printk, allocation or locking, only bounded reads, writes, readbacks and
   the non-IPI cache maintenance.
6. Tests now cover the module's actual transaction through a mock `sg_ops`:
   second-site mismatch with zero write attempts, a site changed during
   revalidation, write and readback failure on each site, failed rollback,
   permission failure and restoration, active swap, already-patched, and bad
   arguments. Both suites build with `-Werror`.

```
$ ./tests/run-tests.sh          # unpiped
rc=0, ALL PASS, 64 PASS / 0 FAIL
```

7. The missing-`Module.symvers` warning is resolved with real stock metadata:
   `tools/gen-stock-symvers.sh` derives 6513 exports from the stock kernel's own
   `__ksymtab_<name>` markers (CRC 0, `CONFIG_MODVERSIONS` off). With that
   installed the build is warning-free:

```
$ make                          # unpiped
build rc=0, 0 WARNING/error lines
  MODPOST 1 modules
  LD [M]  swap_accounting_guard.ko
vermagic=3.10.26 SMP preempt mod_unload ARMv7
```

Every import was then re-checked against those markers:
`__gnu_mcount_nc`, `kallsyms_lookup_name`, `printk`, `probe_kernel_read`,
`probe_kernel_write`, `set_memory_ro`, `set_memory_rw`, `si_swapinfo`,
`__stack_chk_fail`, `__stack_chk_guard`, `stop_machine` — all EXPORTED.
`v7_coherent_kern_range` is resolved at runtime, so it is not a static import.
The `.rodata` dump shows the site table as `c01acabc/0xd4/ebfe8e2c/e3a00000` and
`c01b996c/0x38/ebfe5aa7/e3a00000`.

No device action was taken: nothing was uploaded, loaded, or exercised, and no
zram was enabled. Status: the Claude reviewer passed this source; the Codex C/D
host check is pending, after which the device gate is authorized. RAM-patch
approval is the user's and the source review gate is the Claude reviewer's, not
a further user-review request.

## Third revision (superseded by later commits; kept as history): permission walker, verdict precedence

Blockers found in review of the second revision, all fixed before this commit:

- **Permission proof no longer writes text.** `set_ro` was called with the
  pre-patch word and its "probe" could write that word back if the page was
  still writable, undoing the guard outside synchronization. The walk now lives
  in `swap_guard_perm.h` and is host-tested (absent PMD, non-table PMD, absent
  PTE, reader errors, both write states); the module supplies real pmd/pte
  readers with `pte_unmap` paired on every path.
- **The masked CPUID comparison was unsatisfiable**: mask `0xfffffff0` with
  required `0x410fc073` can never match. Required is now `0x410fc070`, and a
  host test asserts the measured MIDR `0x410fc073` masks to it.
- **A text page left writable now outranks a refused permission change**
  (`SG_TXN_RO_RESTORE`, not a benign `SG_TXN_PERM`), with a test for that pair.
- **`sg_run` no longer ignores the stopped call**: a failed or non-running
  `stop_machine` callback is `SG_TXN_STOPPED`, and stage 2's execution is
  recorded rather than assumed.
- **Rollback runs inside the single writer while pages are writable**, does not
  stop at the first failed site, and every rollback is read back and verified.
- **Every attempted page is restored, including the failing one.**
- **Callback pointers are validated**, with tests for each missing one.
- **The spec is shared** (`swap_guard_spec.h`) by the module and all tests.
- **Stock export metadata is validated**: `gen-stock-symvers.sh` rejects bad
  inputs and preserves prior output on failure, classifies GPL by entry-address
  section (4037 plain / 2473 GPL), and `verify-stock-exports.py` decodes the real
  export arrays from `vmlinux.bin` and validates imports and the GPL
  requirement. Error paths and the decoder are covered by
  `tests/test-symvers.sh`.

Gates, unpiped: unit rc=0 85 PASS / 0 FAIL; symvers/decoder rc=0 13 PASS /
0 FAIL; `make` rc=0 with 0 warning/error lines; all 11 imports satisfied by the
decoded stock export tables. Still no device upload or load.

## Fourth revision: per-site evidence, validated metadata, consistent docs

Review blockers fixed in this commit:

- **Per-site evidence.** The transaction now records, per site, the address, the
  word found, the word written, the word read back and whether each step
  succeeded, and the module logs all of it after the stopped context returns.
  `sg_ops.write_word` is documented as store + cache maintenance only; the
  transaction owns the readback, so a write that took effect but returned
  failure is observed — and rejected, because a backend that returns before its
  cache maintenance is not trustworthy. Tests cover the records, a readback
  failure after a successful write, and a write that lands but reports failure.
- **Generator false success fixed.** An input with `ZZZZZZZZ`/`ffffffff` bounds
  and one marker used to return 0 and overwrite the output. Bounds are now
  validated (well-formed 32-bit, 8-byte aligned, ordered, non-overlapping,
  complete pairs) and marker addresses must parse, before anything is
  classified; the output is renamed only after every check passes. Malformed
  bounds and marker addresses return 4 and 5 with the previous output intact,
  and an output path that is a directory returns 3.
- **Decoder false success fixed.** A `vmlinux.bin` whose `__ksymtab_printk`
  value was zeroed used to pass. Each decoded entry's value is now checked
  against the kallsyms address for that name (zero and mismatched values are
  rejected), with helpers instead of one long main, clean errors for missing
  files, and the marker-to-entry cross-check retained.
- **Docs.** `DESIGN.md` is rewritten as one current document; the layered
  revision notes it replaced live here. The devlog's inverted ticket direction
  is corrected in place: the owner ticket `0x32d1` is one BEHIND the waiter's
  `0x32d2`.
- **Makefile.** `clean` now propagates `CROSS_COMPILE` (it previously printed
  `arm-eabi-gcc: No such file or directory` while exiting 0) and removes every
  test binary; `make check` runs both suites and `make verify` adds a fresh
  module build.
- **Function size.** `sg_run` and `guard_init` are factored under 50 lines each.

Gates, unpiped: `make verify` rc=0 with four ALL PASS suites (unit 101 PASS /
0 FAIL; metadata 13 PASS / 0 FAIL) and a clean module build. Coverage measured
earlier was plan 100%, txn ~98%, perm 100% with gcc --coverage at O0; that is
host coverage, not device proof. Known gap recorded in DESIGN.md: the decoder
accepts any kallsyms address for a name rather than separating the module
column, and the "every decoded entry has its marker" direction was dropped in
the split.

No device upload, insmod, zram enable or device command was run.

## Fifth revision: tests that exercise the named paths, decoder gaps closed

- **Metadata tests were vacuous and are now real.** The malformed/order fixtures
  used literal `\n`, so they were a single line and failed for "bounds missing"
  rather than the bound check; the output-directory case passed `/dev/null` as
  the *input*, so it failed on the input before reaching the output check. All
  fixtures now use heredocs (real newlines), every case asserts the exit code,
  the message and that the previous output survived, and the output-directory
  and unwritable-directory cases use valid input. A missing stock capture now
  fails the gate instead of skipping to ALL PASS. 31 metadata assertions.
- **Decoder gaps closed.** `/proc/kallsyms` module rows (`[name]`) are parsed
  into a separate set, so a module address can never validate a built-in export
  — a retargeted `printk` value at the module address `0xbf037578` is now
  rejected against the built-in `0xc07b8d8c`. Valid local duplicates are kept as
  sets; `_text` and the section bounds must be unambiguous. The decoded-entry to
  marker direction is restored alongside marker to entry.
- **Build metadata regenerated and compared.** The `Module.symvers` consumed by
  the build still held 6513 lines from the earlier generator while the validated
  generator produces 6510 (4037 plain, 2473 GPL); the 3 extra were the
  module-space markers. Regenerated with
  `tools/gen-stock-symvers.sh <captured kallsyms> <validated.symvers>`, copied
  into KDIR, and compared byte for byte: `EXACT_MATCH True` afterwards. A fresh
  clean build is warning-free, all 11 imports are satisfied by the decoded stock
  tables, and `v7_coherent_kern_range` is confirmed in the validated export set.

Gates, unpiped: `make verify` rc=0, four ALL PASS suites, **132 PASS / 0 FAIL**;
fresh `make` rc=0 with 0 warning/error lines; decoder rc=0 on all 11 imports.

Still open from the last review, recorded rather than claimed: the per-site
evidence records patch readback but not a separate rollback readback value, nor
per-site RW-attempted / RO-verified flags (the attempted edit corrupted the
header and was reverted to the committed state rather than committed broken).
`DESIGN.md` now documents the adapter-versus-errno mapping, the pre-write versus
stopped gate split, and restores the device gate section.

## Sixth revision: module column, per-site rollback evidence, real fixtures

- **Parser bug fixed (reproduced).** `load_syms` tested `p[2]` (the symbol name)
  for brackets, but the module is the FOURTH field: stock lines are
  `c081a7f0 R crc16_table` and `bf037578 r crc16_table [mtk_stp_wmt_soc]`. The
  decoder now parses `len(p) >= 4` with a bracketed `p[3]` as module metadata and
  keeps `p[2]` as the name. The exact case is now a test: retargeting the binary
  `__ksymtab_crc16_table` value to its same-name module address `0xbf037578`
  returns 1 with "module addresses 0xbf037578" in the message, where it
  previously returned 0.
- **Ambiguity and classification.** `builtin()` no longer silently takes
  `min(set)`: a name with more than one built-in address is treated as
  ambiguous, so `_text` and the section bounds cannot be guessed. Legitimate
  kernel static duplicates (`dev_open`, `unuse_mm`) stay as sets. Unknown
  built-in markers outside the tables are still rejected; module markers are
  reported separately (3 in this stock kernel) and never used.
- **Entry to marker direction kept as a permanent test.** Removing
  `__ksymtab_printk` by its name field (not by a CRLF-sensitive grep) and
  restoring its value makes the decoder return 1 with "has no marker".
- **Per-site evidence completed.** Each site now records patch readback and,
  separately, the rollback readback value, its read success and verified flag,
  plus `rw_attempted`, `ro_attempted` and `ro_verified`. The refusal paths record
  the observed word and read status instead of zeros. All logging stays after
  the stopped context, and no text store happens outside it.
- **Metadata tests no longer depend on a scratch file**: the imports are taken
  from the built `.ko`, and `make verify` runs the decoder import check after the
  build, including the `v7_coherent_kern_range` runtime dependency.
- **DESIGN** had orphan table rows before its heading and a stale
  "known decoder gap" paragraph; both are removed rather than appended to, and
  the load table now matches the code (`SG_TXN_SWAP_ACTIVE`, pre-write versus
  stopped revalidation).

Gates, unpiped: `make verify` rc=0, four ALL PASS suites, **149 PASS / 0 FAIL**;
fresh clean `make` rc=0 with 0 warning/error lines; stock words and branch
targets re-checked against `vmlinux.bin`; all 11 imports satisfied by the decoded
stock tables. Historical devlog sections are labelled superseded. No device
action.

## Seventh revision: initialised reads, split rollback status, strict gates

1. **Uninitialised read fixed.** `sg_read_all` now sets `st[i].word = 0` before
   calling the backend, so the refusal paths that record the observed word can
   never copy an uninitialised kernel-stack value; `read_ok` stays 0 and the word
   is never presented as verified. Test: a backend that fails without writing
   `*out` yields `old_word == 0`, `old_read_ok == 0` and zero writes.
2. **Rollback status is not conflated.** `rollback_ok` is replaced by a distinct
   `rollback_write_rc` and `rollback_read_ok`, so a failed write followed by a
   successful read is reported as exactly that. `guard_report` now logs the new
   fields after the stopped context: `old_read_ok`, `write_rc`, `patch_readback`
   and its ok flag, `patched`, per-site `rw_attempted` / `ro_attempted` /
   `ro_verified`, and `rollback_write_rc` / `rollback_readback` /
   `rollback_read_ok` / `rolled_back`.
3. **Gate strictness.** `make verify` no longer uses a masking pipeline: `nm`
   output goes to its own file, `nm`'s exit status is checked directly, and an
   empty import list is refused. The order is explicit (build, then the host
   suites, then import verification of the `.ko` that was just built) instead of
   unordered prerequisites. The decoder refuses an imports file with no symbols
   and now **fails** on built-in markers outside the tables, while the three true
   module markers remain report-only. Tests cover the empty imports file, the
   module-shadow retarget, the removed marker, and the read-without-write case.

Gates, unpiped: `make verify` rc=0, four ALL PASS suites, **156 PASS / 0 FAIL**,
with `all 11 imports are satisfied by the stock kernel`. No device action.

## Eighth revision: initial reads recorded on every preflight outcome

`sg_prepare` filled the per-site record only when the plan failed, so a
permission or swap refusal after a *successful* initial read reported
`addr=00000000`, `old=00000000`, `old_read_ok=0` alongside `rw_attempted=1` and
`ro_verified=1` — self-contradictory evidence. The initial read's address, word
and read status are now recorded before any preflight result is returned, for
success and refusal alike, and the stopped reread values are kept as before.

Regression: with stock words and `set_rw` refused at site 0, `sg_run` returns
`SG_TXN_PERM` and the record shows the real address `0xc01acb90`, the real word
`0xebfe8e2c`, `old_read_ok=1`, `rw_attempted=1`, `ro_verified=1` and zero write
attempts; a swap refusal likewise keeps the observed word and read status.

Gates, unpiped: `make verify` rc=0, four ALL PASS suites, **166 PASS / 0 FAIL**;
fresh clean `make` rc=0 with 0 warning/error lines. No device action.

## Ninth revision: the generator is locale-reproducible

The exact-byte comparison of a freshly generated `Module.symvers` against the one
installed in KDIR failed with an AssertionError under the supervisor host's
`C.UTF-8` locale while passing under `en_US.UTF-8` (the daemon's locale differed
from root's, which is what made the discrepancy visible). The rows were identical in content, count (6510)
and multiplicity; only the **sort order** differed, because `sort` collates by
locale. The generator now pins `LC_ALL=C` and `LANG=C` before it runs `awk` and
`sort`, so its output does not depend on the caller's environment.

Verified across the locale matrix: `C` vs `C.UTF-8` and `C` vs `en_US.UTF-8`
both produce **BYTE_MATCH True**, and KDIR regenerated with the fixed generator
compares **EXACT_BYTE_MATCH True** (6510 rows: 4037 `EXPORT_SYMBOL`, 2473
`EXPORT_SYMBOL_GPL`).

Gates, unpiped: `make verify` rc=0, four ALL PASS suites, 166 PASS / 0 FAIL,
`all 11 imports are satisfied by the stock kernel`, and the exact-byte metadata
gate holds. No device action.

## Independent verification (host, no device)

The committed source at `4ec8a22` was verified independently on the host, with
raw proof kept outside the repository at
`work/android/swap-guard-supervisor-20261012/verification.json` and its sibling
logs:

- clean build plus full `make verify`: **166 PASS / 0 FAIL**, no compile
  diagnostics;
- **12 dependencies** validated: the 11 actual `.ko` imports plus the runtime
  cache backend `v7_coherent_kern_range`;
- original words, BL/BEQ targets and symbol addresses re-checked against the
  stock `vmlinux.bin`;
- negative cases refused with rc=1: module-shadow retarget, removed marker, and
  an empty imports file;
- metadata: 6510 rows, **EXACT BYTE MATCH** against the installed `Module.symvers`;
- updated gcov on the shared core: planner **100%**, transaction **98.72%**,
  permission **100%**.

The device backend (`g_write`, `g_set_rw`, `g_set_ro`, `g_stopped`, `g_swap` and
the real page-table readers) remains **unverified**: host coverage says nothing
about it. No device execution, upload, insmod or zram was performed, and
permanent writes stay prohibited.

## Phase 1 follow-ups (C, D, B) and the observer design

Implemented on top of `fc164f4`, host-only:

- **C — compile-time binding.** `guard_resolve()` carries `BUILD_BUG_ON` checks
  tying `SG_PTE_PRESENT`, `SG_PTE_RDONLY`, `SG_PMD_TYPE_TABLE` and
  `SG_PMD_TYPE_MASK` to this prepared kernel's `L_PTE_PRESENT`,
  `L_PTE_RDONLY`, `PMD_TYPE_TABLE` and `PMD_TYPE_MASK`. They are statements, so
  they live in a function; the build fails if a future tree changes the bits,
  with no warning suppression.
- **D — exact `init_mm`.** `SG_STOCK_INIT_MM = 0xc0b5d840` is in the shared
  spec, the module resolves `init_mm` against that expectation instead of 0, and
  a host test asserts the shared constant plus the other spec addresses.
- **B — verdict ordering.** A failed permission preparation still reports a page
  left writable as `SG_TXN_RO_RESTORE` rather than a benign `PERM`, but the
  normal post-stopped path now goes through `sg_txn_verdict`, so a failed
  rollback outranks both. Regression: rollback write failure plus an RO restore
  failure yields `SG_TXN_ROLLBACK` with `rollback_failed >= 1` and
  `pages_ro_failed >= 1`; the permission-preparation pair yields
  `SG_TXN_RO_RESTORE` with zero writes.

Gates, unpiped: `make verify` rc=0, four ALL PASS suites, **175 PASS / 0 FAIL**;
clean build rc=0 with 0 warning/error lines, which is also the proof that the
`BUILD_BUG_ON` checks compile against the real headers.

The device-run procedure and the 30-minute patched+zram observer are written up
in `probe/kmod/swap-accounting-guard/OBSERVER-DESIGN.md`, with the stub
`tools/patched-zram-observe.sh` exiting without touching the device. Nothing was
uploaded, inserted, enabled or run on the device.

## Phase 2: the real observation runner and its host tests

`tools/patched-zram-observe.sh` replaces the design stub with the executable
procedure, and `tools/observe-lib.sh` holds the decision predicates so the host
tests drive the real logic against fixture files:

- preflight: one deliberate reboot, then a clean-boot check that the **whole**
  dmesg has no historical `Adding ... swap` (totalswap 0 cannot see a leaked
  lock), `/proc/swaps` empty, `SwapTotal` 0, no container, and the live exact
  addresses of `swap_info_get`, `smaps_pte_range`, `kpageswapn_read` and
  `init_mm`;
- FIQ `2 10 30 0 1` verified, then the guard pushed and `insmod`-ed with the
  **device-side** status in its own file, before any zram;
- the module log must show both sites at `e3a00000` with `patch_readback_ok 1`,
  `writes 2/2` and `ro_verified 1` each, with no rollback or failure counter —
  otherwise the run aborts with no swapon;
- the original helper order `prepare; zram; swap; container; start_init` is used
  unchanged (`swap` is the Mali driver replacement);
- observation runs until **1800 actual device monotonic seconds** after the
  workload launch, sampling uptime, complete `/proc/swaps`, `SwapTotal`,
  `SwapFree`, `MemFree`, genuine LMK `Killing '` lines and a smaps read of a
  pinned long-lived PID, with each sample in uniquely numbered raw files;
- a `STOP` file aborts between samples, a reset or any unknown state aborts, and
  there is no automatic re-up or swapon retry; recovery is `down` plus one
  reboot.

`tests/test-observe.sh` covers the predicates with real fixtures (prior swap,
swap state, complete/partial/empty/rollback module logs, smaps zero vs positive
vs empty vs PID-exit, 1799/1800/1801 seconds, uptime regression) and the runner's
dry mode (abort on a running container, refusal before any workload, exactly one
reboot, at most one workload attempt).

Gates, unpiped: `make verify` rc=0, five ALL PASS suites, **203 PASS / 0 FAIL**.
Nothing was uploaded, inserted, enabled or run on the device.

## Device-verified: Phase A preflight and the RAM-only guard load (2026-10-12)

The device gate ran with explicit operator commands, not the unfinished
all-in-one runner. Capture directory:
`work/android/guard-load-20261012T102333Z/` (with `codex-preflight-proof.json`
and `codex-load-proof.json` written by the independent reviewer).

- **Pre-reboot read-only capture**: uptime 12256.78, boot id
  `51f2f15b-9856-434c-8585-1b597ded01d6`, no container, `/proc/swaps` empty,
  `SwapTotal 0`, guard not loaded, `last_kmsg` 7178 B.
- **One approved reboot** produced a genuinely new boot:
  boot id `1d0f3aa6-f5a6-4228-9a02-ef79d9b147dd`, uptime 34.12 s at detection.
- **Pre-load proof**: zero `Adding ... swap` lines anywhere in the new boot's
  dmesg, zero `/proc/swaps` rows, `SwapTotal 0`, no container, guard not loaded,
  and the five live addresses exact — `swap_info_get c0150448`,
  `smaps_pte_range c01acabc`, `kpageswapn_read c01b996c`, `init_mm c0b5d840`,
  `v7_coherent_kern_range c001ec80`.
- **Upload and identity**: `/tmp` only; host sha256 `f73c9981…` equals the
  pull-back hash (`cmp` identical), device copy 113243 B, vermagic
  `3.10.26 SMP preempt mod_unload ARMv7`, source unmodified since the cleared
  commit.
- **FIQ**: `/proc/wdk` parsed final row `2 10 30 1` and the actual register
  `mode=1d`.
- **Load**: `insmod` exactly once, device-side `insmod_rc=0`, transport rc 0,
  empty stderr, no force and no `rmmod`. `/proc/modules` shows
  `swap_accounting_guard 3925 0 [permanent], Live 0xbf0e0000 (O)`: no exit
  function, so it cannot be unloaded and reboot is the recovery.
- **Module log contract**: two unique site lines with the exact addresses, the
  original words `ebfe8e2c`/`ebfe5aa7` at `old_read_ok 1`, `new e3a00000`,
  `write_rc 0`, `patch_readback e3a00000` with `patch_readback_ok 1`,
  `patched 1`, `rw_attempted 1 ro_attempted 1 ro_verified 1` each, and the
  summary `writes 2/2 rollback 0/0 failed 0 pages_rw 2 ro_restored 2 ro_failed 0
  stopped 1 ran 1`. No rollback and no failure. The module prints no `armed`
  line and none was required.
- **State after load**: `/proc/swaps` empty, `SwapTotal 0`, `SwapFree 0`,
  `MemFree 76712 kB`, no container. No zram was enabled and no workload started.

Corrections recorded rather than papered over: the `LOAD-SUMMARY.txt` site lines
are an **excerpt** (the hand-copied site 1 line had dropped `patched 1`; the raw
bytes are in `load_dmesg.out`), the `insmod.exit` file was written after the echo
so it records the echo pipeline and the load rests on the literal transport rc 0,
the device-side `insmod_rc=0` and the Live module entry, and the earlier "56 Sony
processes" was a matching-line count, not a proven process count.

The swapped-PTE execution and cache-effect gate is **not** verified: it needs a
workload with real swap use and a successful smaps read whose ordinary `Swap:`
fields sum above zero. The read-only observer for that is still being host-tested
and is **not cleared**.

## Follow-up: guard_resolve naming, quarantine, and the artifact delta

- **Readability fix (cosmetic).** `guard_resolve()` reused `icache` to receive
  `swap_info_get`; it now has its own `unsigned long swap_info_get`. No
  behaviour change, no re-insmod: the loaded boot keeps the reviewed artifact.
- **Honest artifact comparison.** The rebuilt `.ko` is **not** byte-identical to
  the loaded one — loaded `f73c9981…` (reviewed, still what runs) versus the
  new build `3fae613aabf17e7774878674f5f809bc0c01298eb4e0c5acbf90c9f67074f748`. The code-section diff is a stack-frame shift from the
  added local (`sub r2, fp, #204` -> `#252`), i.e. layout, not logic. The
  loaded artifact's provenance is unchanged and remains the reviewed evidence;
  the new source would need its own load if it is ever to run.
- **Quarantine.** `tools/patched-zram-observe.sh` is moved to
  `quarantine/patched-zram-observe.sh.QUARANTINED` with a README: it is the
  all-in-one reboot+load+workload orchestrator, it is **not authorized to
  execute** (it would reboot the loaded boot, it was never host-tested end to
  end, and its dry-run fixtures did not reproduce its own call sequence). Its
  dry-run assertions are removed from `tests/test-observe.sh`, which now covers
  only the predicates. The flawed single-pid observer draft was deleted rather
  than left as a misleading artifact.

Gates, unpiped: `make verify` rc=0, five ALL PASS suites, **194 PASS / 0 FAIL**,
all 11 imports satisfied by the decoded stock tables. No device action: no
reboot, no load, no `up`, no swapon, no permanent write.

**Observer status: not cleared.** The agreed design is one read-only DEVICE
collection script plus a host parser/loop: every sample reads all
`/proc/[0-9]*/smaps` on the device once, framing each pid's stat-before,
smaps bytes and stat-after between explicit BEGIN/END markers with
`smaps_rc`/`device_rc`; the host never touches `/proc/<device-pid>`. Streams
stay operator-managed (one kmsg reader before the original `android-boot.sh up`,
logcat once the init pid exists). PASS requires >=1800 true device seconds,
`max(SwapTotal-SwapFree) > 0`, repeated valid swapped-PTE hits and an
uninterrupted boot; STOP is aborted/nonzero. Reset evidence policy applies to
both the up phase and the loop: on failure or timeout, do **not** down or reboot
first — wait bounded for the Walkman, compare `boot_id`, and if it changed
capture `/proc/last_kmsg` first, then dmesg/wdk/meminfo with immediate statuses,
keeping that dump before any manual recovery reboot.

## Read-only device collector + bounded host capture loop

Deliverable: `collect/device-collect.sh` (runs on the device, emits one sample as
explicit markers) and `collect/capture-samples.sh` (host loop that only stores
samples; it computes no verdict, and never runs up/load/swapon/down/reboot).

Protocol as specified: BEGIN_SAMPLE, UPTIME_START, BOOT_ID, full
meminfo/swaps/wdk between BEGIN_/END_ pairs, BOOT_COMPLETED via the join (empty
before boot, never a stop condition), then for **every** `/proc/[0-9]*` on the
device a BEGIN_PID / STAT_BEFORE / BEGIN_SMAPS / cat smaps / END_SMAPS /
SMAPS_RC / STAT_AFTER / END_PID frame, then UPTIME_END, DEVICE_RC=0, END_SAMPLE.
All reads use device BusyBox; no host `/proc`, no `pick_pid`, no synthetic
fields, one sequential cat/stat pair per pid.

**Validated on a real pre-zram sample** (`work/android/guard-load-20261012T102333Z/prezram-sample/`,
3.73 MB): all markers present and closed, **164 pid frames**, **164/164 smaps
reads with `SMAPS_RC=0`**, `STAT_BEFORE` non-empty for all 164, swaps header-only,
`SwapTotal 0`, and **0 pids with a positive `Swap:` sum** — exactly what a
no-swap boot should show. Host smoke test: 9 assertions, all pass.

The real sample also exposed a defect in my own loop: the device emits CRLF, so
the marker checks (`^END_SAMPLE$`) never matched. The loop now normalises with
`tr -d '\r'` into `<sample>.lf` before checking, and aborts nonzero on a
transport failure, a missing end marker, a missing `DEVICE_RC=0` or any missing
mandatory section.

## The loaded boot was lost to an adbd-initiated reset, and the mechanism is named

While the guard was loaded and **idle** — no workload, no swapon — the device
reset on its own. Per the evidence policy, the postreset dump was captured
**before** anything else, read-only: `/proc/last_kmsg` first (7186 B), then
dmesg, wdk, meminfo, swaps, boot id, uptime, modules, container
(`postreset-104446Z/`).

The `last_kmsg` tail names the mechanism:

```
[  859.113082]-(0)[1117:adbd]arch_reset: cmd =
[  859.113475]-(0)[1117:adbd]wdt_arch_reset called@Kernel mode =
[  859.114175]-(0)[1117:adbd]wdt_arch_reset called MTK_WDT_MODE =50
[  859.114696]-(0)[1117:adbd]wdt_arch_reset called end  MTK_WDT_MODE =22000014
```

`adbd` initiated a userspace `arch_reset`/`wdt_arch_reset` at uptime 859 s. That
is a **different mechanism** from the resets investigated earlier, whose
`last_kmsg` carried no restart marker at all. It is also not the guard: the guard
only patches two call sites and prints nothing here, and the new boot's dmesg has
zero `swap-accounting-guard` and zero `Adding ... swap` traces.

State after the reset: boot id `1ff226ae-37ec-4f5b-9498-d0acc2345c72`, guard
**not** loaded (a reboot clears it), `/proc/swaps` empty, no container, no
workload, no swapon, no permanent write. The reviewed load artifact
`f73c9981…` remains valid as an artifact but is no longer running, so a future
zram run needs a fresh load.

The collector and loop are cleared for the next step; the kernel mitigation
itself is not.

## The real native sample, and why the independent verifier rejects it

`collect/device-collect.sh` was run once against the current (post-reset) Walkman
and captured to the requested path
`work/android/guard-load-20261012T102333Z/native-prezram-sample.out` with its own
`.err` (0 bytes) and `.transport-exit` (0), plus `.times`. Nothing was loaded,
enabled or rebooted for it; the read took 9 s.

```
transport rc=0   bytes=3733304   stderr=0
UPTIME_START=464.30  UPTIME_END=473.99
BOOT_ID=1ff226ae-37ec-4f5b-9498-d0acc2345c72
BOOT_COMPLETED=      (no container, so empty)
DEVICE_RC=0
frames: BEGIN_PID=163 END_PID=163, smaps rc=0 163/163, STAT_BEFORE non-empty 163
SwapTotal: 0     positive Swap-sum hits: 0
```

That is exactly the expected pre-zram shape: every read succeeds, the frames are
complete and balanced, and there are **no positive hits because there is no
swap**. Host smoke test over the same file: 9/9 assertions.

The reviewer's independent verifier then rejected it with
`ValueError: watchdog mode changed`. That rejection is **correct and useful**: the
verifier requires the WDK row `2 10 30 1`, while the captured (and live) value is
the stock `1 10 30 1`:

```
BEGIN_WDK
mode interval timeout enable
1    10        30       1
END_WDK
```

The reason is the adbd-initiated reset: it cleared the guard **and** the FIQ
mode, so the current boot is stock. A sample the verifier accepts therefore
requires a fresh boot with the FIQ mode re-applied and the guard re-loaded, which
is the next authorized step and has not been taken.

Collector status: **cleared** (validated on a real sample and by the smoke test).
Kernel mitigation status: unchanged, still unverified for swapped-PTE execution.
The quarantined orchestration remains quarantined and is not claimed to work.
No up, no swapon, no reboot, no permanent write.

## INCIDENT 2026-10-12: a host test rebooted the device. Earlier "spontaneous reset" claims are WRONG.

**What actually happened.** My host test invoked the rewritten 131-line
`patched-zram-observe.sh` with `--dry-run`. That rewrite **does not implement
`--dry-run` and ignores extra argv**, so the call fell through to its real code
path and its `raw reboot adb -s <serial> reboot` **rebooted the user's
Walkman**. Transcript correlation: the test call began at UTC 10:38:07.738 and
returned at 10:38:34.641; the captured uptime places the restart at ~10:38:13,
inside exactly that window. The seven failing old dry-run assertions are the same
event seen from the test side.

**Raw evidence** (`postreset-104446Z/last_kmsg.out`, verbatim, CR-stripped):

```
hw_status: 2, fiq step 0
[  857.820380] SysRq : Emergency Remount R/O
[  859.110971] (0)[1117:adbd] Restarting system with command ''.
[  859.113082] (0)[1117:adbd]arch_reset: cmd =
[  859.113475] (0)[1117:adbd]wdt_arch_reset called@Kernel mode =
```

That is the **ordinary adbd reboot path**: a userspace emergency remount then
`Restarting system`, with `hw_status: 2, fiq step 0` and **no FIQ stack** — not a
watchdog bite and not an FIQ. **Correction:** the "spontaneous reset", "reset on
its own" and "adbd initiated" wording in the two entries above is withdrawn. The
reset was **test-issued**. No inference about patch instability or a WDT is
supported, and none is made.

**Guards added so it cannot recur.**
`tests/adb-guard.sh` is sourced by every host suite: unless a test explicitly
supplies a fixture transport via `FAKE_ROOT`, a failing `adb` is put first on
`PATH`, so any attempt to reach a device exits 127 and is logged. Proven:
`. ./tests/adb-guard.sh; adb -s <serial> reboot` -> rc=127 with the attempt
logged, i.e. the exact command that caused the incident is now refused.
`make verify` is green (194 PASS / 0 FAIL) with **zero** refusal log lines, so no
test tried to reach a device. The quarantine is hardened:
`quarantine/patched-zram-observe.sh` is now an explicit refusal (rc=127) and the
real file beside it is non-executable (mode 644); neither is part of `make
verify`. Withdrawing the unsafe, unsupported orchestration tests does **not**
mean that orchestration works — it remains quarantined and unclaimed.

**Current state (unchanged by this commit):** fresh boot
`1ff226ae-37ec-4f5b-9498-d0acc2345c72`, guard not loaded, WDK stock `1 10 30 1`,
swap 0, no container. The verifier's `watchdog mode changed` rejection is correct
and its expected WDK was **not** edited. The native sample's format was read
successfully (164 full `/proc` PID frames, 164 `cat` rc 0, positives 0 as
expected without swap).

## Recovery / reset protocol (explicit)

Applies to the `up` phase and to any capture loop:

1. On a failure or timeout, do **not** run `down` or `reboot` first.
2. Wait bounded for the Walkman serial to enumerate again.
3. Read the boot id and compare it with the one recorded before the failure.
4. If it changed, capture `/proc/last_kmsg` **first**, then dmesg, `/proc/wdk`
   and its register, meminfo and swaps, each with its own immediate status.
5. Only then perform a normal recovery reboot.
6. A post-recovery state capture is taken **only after a genuinely new boot id**
   is verified, never during the reboot.
7. No automatic new workload and no same-boot retry.

No `up` and no swapon until the collector, the native sample and the host capture
loop are independently cleared, and then only after a deliberate new boot with a
no-`Adding-swap` preflight, a real byte comparison of the `.ko` and the FIQ mode
re-applied.
