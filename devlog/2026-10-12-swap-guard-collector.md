# swap guard: collector/loop safety, and the unapproved reboot incident (2026-10-12)

Continues `devlog/2026-10-12-swap-accounting-guard.md`, which is kept intact and
is near its size limit. Kernel guard source is unchanged and cleared
(`e3bcecd`); this entry covers the collector, the capture loop and one incident.

## The unapproved device reboot, and its basis

During a `make verify` at 10:38:07.738Z -> 10:38:34.641Z, a host test invoked the
rewritten all-in-one runner with `--dry-run`. That rewrite does not implement
`--dry-run` and ignores extra argv, so the call fell through to its real path and
its `adb ... reboot` **rebooted the user's Walkman**. This was **one unapproved
device reboot issued by a host test**. It was not spontaneous, not a watchdog
bite, and not evidence of kernel-patch instability; no such inference is made.

Raw evidence, `work/android/guard-load-20261012T102333Z/postreset-104446Z/last_kmsg.out`:

```
ram console header, hw_status: 2, fiq step 0.
[  857.820380]SysRq : Emergency Remount R/O
[  859.110971] (0)[1117:adbd]Restarting system with command ''.
[  859.113082]-(0)[1117:adbd]arch_reset: cmd =
[  859.113475]-(0)[1117:adbd]wdt_arch_reset called@Kernel mode =
```

That is the ordinary adbd reboot path with no FIQ stack. Basis for no lasting
impact, stated as basis rather than as an absolute claim: the device came back on
a stock boot (`1ff226ae-37ec-4f5b-9498-d0acc2345c72`), the guard is not loaded,
WDK is the stock `1 10 30 1`, `/proc/swaps` is empty and no container runs; the
RAM-only guard leaves nothing persistent, and no workload, swapon or firmware
write had happened. Nothing indicates lasting change; that is not the same as a
proof that no state anywhere changed.

## Guards in place

`tests/adb-guard.sh` is sourced by every host suite. Unless a test supplies an
explicit fixture transport, a failing `adb` is placed first on `PATH` so any
device access exits 127 and is logged. Proven: the exact offending command
(`adb -s <serial> reboot`) is refused with rc=127 and logged. `make verify`
is green (6 suites, **210 PASS / 0 FAIL**) with **zero** refusal-guard lines, so
no suite tried to reach a device.

Still to do for the reviewer's point 1: every device-capable test should also
assert that `command -v adb` resolves to the fake, set `NW_SERIAL` to a
deliberately nonexistent serial, fail when the fake is missing, not executable or
shadowed, and add negative tests for those cases. Not implemented yet.

## Collector and loop: fixed contracts

`collect/device-collect.sh`: the earlier `need()` helper never shifted its name
argument, so `need uptime $BB cut ...` executed `uptime ...` and the failure
could not abort the parent — a sample could have carried `DEVICE_RC=0` and
`END_SAMPLE` with empty mandatory fields. Replaced with `mread`, which echoes
`COLLECT_ERROR=...` and exits 1, and every call site is a **checked assignment**
(`X=$(mread ...) || exit 1`), so a mandatory failure aborts before any success
marker. Simulated on the host: rc=1 with no `DEVICE_RC=0` and no `END_SAMPLE`.
Per-PID `cat`/`stat` failures remain expected, are recorded with their own status
and never abort the sample.

`collect/capture-samples.sh`: rejects more than four arguments and any flag in any
position (there is no `--dry-run`) **before any adb runs**; requires a new output
directory so a run cannot be overwritten; validates the seconds/interval as
positive integers and the sample uptime as a real decimal; checks every status
and time file write; requires each sample's boot id to equal the expected loaded
boot and the uptime to be monotonic; and stops nonzero on transport failure,
missing end marker, missing `DEVICE_RC=0`, `COLLECT_ERROR`, boot change, uptime
regression or STOP. It never runs up/load/swapon/down/reboot and makes no PASS
claim.

Host tests: `tests/test-capture.sh` (16 assertions) covers completion at the
elapsed target, the immediate first sample, a missing end marker, a device error
marker, a transport timeout, a changed boot id, a decreasing uptime, STOP, an
unknown flag and a non-numeric argument (both with **zero** adb invocations), and
refusal to overwrite an existing output directory.

## Not done, and therefore not cleared

The **lean** collector the reviewer asked for — one summary line per pid (pid,
comm, stat starttime before/after, saved smaps rc, ordinary `Swap:` sum, line
count) with raw smaps retained only for the single largest-`Swap` pid, plus
regression tests written first against real native stat/smaps shapes — is **not
implemented**. The current collector still emits full smaps for every pid, which
the reviewer measured at 3.7 MB and 9.7 s for 164 pids with no container; with
Android running that cost is much larger and would itself perturb the window.

So: no new reboot, no load, no FIQ re-application, no native sample and no
workload in this entry. The device stays on the stock boot with the guard
unloaded, and the next step is the lean collector plus its regression tests,
followed by root clearance of one lean native sample.

## Subphase 1: host-test isolation strengthened

The guard now enforces, in order: an explicit `TEST_ADB` (which is **not** a
bypass) must exist, be executable and be an absolute path; its directory is
prepended to `PATH`; and then `command -v adb` must equal that exact path, with
the canonical `readlink -f` comparison as an additional check. With `TEST_ADB`
unset, a refusal fake is installed via `mktemp -d` and its executable is verified
before `PATH` is set. `NW_SERIAL` is forced to `NW-TEST-NO-SUCH-SERIAL`, so no
test can address a real device by serial even if the guard were broken.

`tests/test-adb-guard.sh` (10 assertions, all passing) runs every case in a
subprocess, never sourcing the guard into this shell. Two of its cases were
fixed after review:

- The first case used the **real serial with `reboot`**, which would have
  re-run the exact incident if the guard ever failed to install its refusal fake.
  It now uses `NW-TEST-NO-SUCH-SERIAL` and the harmless `get-state`, so a broken
  guard still cannot reach the device.
- Case 5 expected the guard to *fail* on a shadowing `adb` earlier on `PATH`.
  That contradicted the contract: the guard prepends and asserts, so the fake
  wins and the guard exits 0. The expectation now matches, and a genuine
  wrong-resolution case (a shell **function** named `adb`, which outranks `PATH`)
  is the one asserted to fail.

Counting in that selftest used `grep -c ... || echo 0`, which prints `0` and
exits 1, producing `0\n0`; it now uses an `awk` counter. The transient failure
was quoted in the journal: `FAIL the shadow itself was never used got=1 want=0`
-- the `1` was the fake's own log line containing the word "shadow". A distinct
token (`SHADOW_WAS_USED`) removed the self-match.

`tests/test-no-absolute-adb.sh` (8 assertions) is a static gate over the
device-capable scripts: none may reach adb by an absolute path or through an
`ADB*` variable that could hold one, which keeps the `PATH` guard sufficient.
`tests/test-capture.sh` sources the guard **after** creating its fake and asserts
inside `run()` that `adb` resolves to that exact file immediately before the
script executes; it uses tiny intervals, so no test sleeps for real seconds.

Gates, unpiped: `make verify` rc=0, **eight ALL PASS suites, 228 PASS / 0 FAIL**,
**zero** refusal-guard lines (no suite tried to reach a device), and all 11
imports satisfied by the decoded stock tables.

Still outstanding from the review, not claimed as done: the changed-boot fixture
in `test-capture.sh` should be otherwise-valid so it reaches the boot-id branch
rather than failing earlier, and the STOP case should exercise a **live** STOP
(the fake touching `STOP` after the first sample) rather than only the
existing-directory refusal. The lean collector is the next subphase. No device
action in this subphase.

## Lean collector: implemented as a draft, NOT cleared (six blocker groups)

`collect/device-collect.sh` (106 lines) now emits one `PID_SUMMARY` line per pid
plus a single `MAX_SMAPS_PID` / `BEGIN_MAX_SMAPS` / `END_MAX_SMAPS` block, with
`SAMPLE_DURATION_SECONDS`, and it is parameterized (`BB`, `PROC_ROOT`, `TMP_ROOT`)
so the actual collector can run against a fixture proc tree. `tests/test-lean-collect.sh`
runs it that way (21 assertions currently pass).

It is **not** cleared, and it was not run on the device. The root's review found
six blocker groups, recorded here rather than papered over:

1. `swap_kb=-1` is emitted for an rc0-but-empty smaps (kernel threads) and for
   failed reads, which violates the agreed non-negative sum protocol and would
   make every native sample fail a strict parser. Empty/failed must emit
   `swap_kb=0 lines=0` while preserving the real rc.
2. The `sb`/`sa` captures are unused and `starttime_of` re-reads the stat a
   second time; each read must happen once. The leading pid must be validated
   against the directory pid, and the greedy last `") "` split must yield a
   numeric field 20, otherwise `-`. The existing "reused" test only rewrites the
   stat **before** sampling, so it never simulates a change between BEFORE and
   AFTER at all; that needs a wrapper that mutates the stat after the smaps read.
   The fixture also creates `"$r/$5"`, mistaking the pss kB for a pid.
3. The smaps buffer uses a predictable `/tmp/collect.$$.smaps` path (symlink
   risk) instead of a private `mktemp -d` on the asserted tmpfs; cleanup must
   remove both buffers **and** the directory on every exit, and a signal handler
   must exit nonzero rather than clean up and continue.
4. The awk aggregate is read through an unchecked heredoc; the actual cat rc must
   be preserved immediately, the two numbers validated as non-negative, the max
   copy and the final max read checked before `END_SAMPLE`, and the duration
   computed in a checked assignment rather than inside the final `echo`. None of
   those failures may emit `DEVICE_RC=0` or `END_SAMPLE`.
5. An unreadable largest pid still leaves the eligible zero-Swap pid as the max:
   the expected selection is that zero-Swap pid, not `-`. The "ties go to the
   lower pid" comment is false for a lexical glob (`100` sorts before `20`);
   either implement a numeric tie-break or document first-in-traversal.
6. TDD honesty: the source was written before `tests/test-lean-collect.sh`, so
   that is not tests-first. The regressions for the above must be added and
   proven red first, then fixed.

Also still outstanding: `test-capture.sh` needs an otherwise-valid changed-boot
fixture, a **live** STOP case (the fake touching STOP after the first sample),
and fake-sleep instead of real sleeps. The root's independent parser
(`work/android/swap-guard-supervisor-20261012/lean-verify/verify_lean.py`) must
be run over actual collector fixture outputs; its default requires FIQ `2 10 30 1`
and must not be weakened — a stock sample uses the separate `expected_wdk 1` path.

Nothing here is committed: the Makefile wiring was reverted so the committed gate
is unchanged at `4517345`, and the collector, its tests and the pending loop
tests stay as uncommitted drafts. No device action.

## Root implementation: lean collector and capture gate

DeepSeek stopped host edits after the six-blocker draft. Root added failing
regressions before replacing the draft, then reran the actual collector against
fixture proc trees. The fixture stat is the full native PID465 record captured
in `native-prezram-sample.out`, not a host process record.

The initial regression run exited 1, including these literal failures:

```
FAIL empty smaps retains zero nonnegative sums            got=0 want=2
FAIL exited PID identity recorded as unknown              got=0 want=1
FAIL stat PID mismatch cannot establish identity          got=0 want=1
FAIL raw failure exits nonzero                            got=0 want=1
FAIL raw failure has no success marker                    got=2 want=0
FAIL signal failure exits nonzero                         got=0 want=1
```

The replacement emits one `PID_SUMMARY` per device PID, containing starttime
before/after, actual smaps read status, ordinary Swap sum, line count and comm.
Missing/exited/reused identities cannot count as hits. Empty reads report zero,
not negative sums. The raw snapshot is retained only for the largest Swap sum
among complete, nonempty, stable reads; numeric PID resolves ties. It is copied
from the already-read buffer, never reread after selection. Private buffers use
`mktemp -d` on an explicitly verified tmpfs. Aggregation, copying, final raw
read, duration computation and cleanup failures abort without success markers;
signal handling also exits nonzero. Both buffers are removed before success.

The host capture loop checks transport and remote chmod separately, requires
unique sample/status markers and matching duration/end uptime, checks continuity
against the previous sample's end, and reports actual device duration per sample.
Its cadence target is 30 seconds start-to-start; collection overruns are logged
and receive a full rest interval rather than overlapping or continuous walks.
It remains capture-only: no up, swapon, kernel load, down or reboot. Timeout/reset
recovery remains dump-first and operator-controlled.

Capture tests now actually reach the changed-boot and live-STOP branches, check
that chmod failure prevents collection, reject malformed/reversed end times,
and verify cadence/overrun reporting. A fixture fake validates the deliberately
nonexistent serial on every invocation. `sleep` is explicitly faked.
**Correction to Subphase 1:** `4517345` still requested five real `sleep 30`
calls. Root's scratch verification intercepted them; the earlier statement that
that revision used tiny intervals was incorrect. The current tests install their
own fake sleep and never reach root's outer sleep refusal fence.

Verification, with adb and sleep refusal fences outside the fixture transports:

```
python3 work/android/swap-guard-supervisor-20261012/verify-collector.py
clean exit=0
make verify exit=0
290 PASS; 0 FAIL; build diagnostics=0
outer_fence_calls=0; unexpected_guard_calls=0
```

The full gate builds the ARM module, runs nine suites and decodes all 11 actual
module imports against stock exports. Source hashes and full output are in
`work/android/swap-guard-supervisor-20261012/collector-final/`.
`sh -n` passed for every `*.sh` file in the scratch module directory.

Independent protocol verification:

```
python3 work/android/swap-guard-supervisor-20261012/verify-actual-fixture.py
actual collector fixture exit=0; independent parser exit=0
processes=2; successful_reads=2; positive_hits=1; swap_sum_kB=128
```

This is an actual script run against a fixture, **not a device observation**.
The independent parser's default still requires WDK `2 10 30 1` and validates
max-raw identity, line count and Swap sum against summaries. Its 27 unit tests
pass; branch-aware coverage reports 84%. A stock sample may use an explicit
WDK1 format-only override, never the experimental gate.

No device command was issued by root during this implementation. The next step
is one lean stock-native sample after confirming zero current swap, no historical
Adding-swap, no container and unchanged boot ID. A fresh Phase A must then repeat
before loading the previously verified `pulled.ko`, SHA256
`f73c9981237c76469a60961789fa65a44041d728492bd69fab05fadcbe40ca9e`, source
`e3bcecd`. No new module binary is substituted.

The experiment remains unstarted. Its predeclared acceptance criteria remain:
at least 1800 seconds of continuous loaded-boot observation, actual SwapTotal>0
and maximum SwapTotal-SwapFree>0, and repeated rc0/stable-identity smaps reads
with Swap>0. Record swapon's actual kmsg uptime, workload timing, MemFree and LMK
kills. Compare with the previous 102.05-second post-zram freeze; one passing run
supports this mitigation but is not proof against every failure. Preserve reset
evidence before recovery, and verify a new boot ID before post-recovery state.

The preexisting dirty `tools/observe-lib.sh`, untracked `coreup/`, work artifacts
and graph files are not staged with this slice. Kernel patch source is unchanged.

## Native failure and measured shell compatibility

`b93abe0` did **not** work on the stock device. The single approved sample in
`work/android/lean-native-20261012T114659Z/` returned only 21 bytes:

```
trap: INT: bad trap
```

Transport rc was 0, but there were no sample/success markers. This is a failed
collector run, not a valid sample. Before it, boot ID was
`1ff226ae-37ec-4f5b-9498-d0acc2345c72`, SwapTotal 0, swaps rows 0, Adding-swap
history 0, no container and no guard. After it the boot ID was unchanged. `/tmp`
was tmpfs and all eight required BusyBox applets were present (the initial zero
applet counts were CRLF matching, not missing commands). No reboot, FIQ change,
module load or swapon occurred.

Read-only probes in `work/android/lean-trap-probe-20261012T115209Z/` established
that `/system/bin/sh` resolves to `/system/bin/ash`. Numeric trap setup and
clearing work. The dedicated TERM probe was executed directly from a pushed
script with literal `$$`; its complete output was:

```
PROBE_PID=2177
TERM_HANDLED
remote_rc=7
```

The outer adb shell survived. This avoids relying on host/outer-shell expansion
of `$$`. Numeric exit trap 0, individual signal traps 2/15, and `trap - 0` were
also measured successfully. The collector now uses those exact forms, with
separate signal registrations. The host numeric-only regression failed before
this correction (`FAIL device numeric-only trap compatibility got=1 want=0`)
and passes afterward. It supplements, rather than replaces, normal `sh` tests.

Another regression removed comm as well as stat/smaps during PID exit. It failed
before fixing redirection order:

```
FAIL expected PID exit has no stray diagnostic            got=1 want=0
```

stderr is now redirected before opening comm, so an expected PID exit cannot
inject a shell diagnostic into an old adb transport's merged sample stream.

`verify-collector.py`: clean rc0, full `make verify` rc0, **292 PASS / 0 FAIL**,
build diagnostics 0, outer adb/sleep fence calls 0, unexpected guard calls 0.
Kernel module source is unchanged. A new lean native sample is still required;
no fresh Phase A/load or zram run is cleared by this host-only result.

## Single-process scan and separate reset cadence

The `328d9c8` stock-native sample was complete but too expensive:
`work/android/lean-native-20261012T120107Z/` recorded uptime 4979.94 to
5018.49, **38.55 seconds**, 163 summaries (163 rc0), 66 nonempty reads and
47,515 output bytes. Root's independent format-only WDK1 check accepted the
original bytes. SwapTotal and hits were zero, boot ID unchanged, guard absent,
no container and stock WDK. This was not a patched-workload observation.

The replacement uses one awk process for the whole device proc traversal.
It reads stat-before, comm, smaps and stat-after directly from the device proc
paths, emits the same summaries, and saves the largest eligible raw snapshot
from the already-read private buffer. Failed/exited/reused identities still
cannot count. Copy/read/cleanup failures still prevent success markers.

The capture command now explicitly invokes `/system/xbin/busybox sh`, rather
than relying on the stock shebang interpreter. `BOOT_COMPLETED_RC` records join
or getprop failure without discarding the core sample. Boot completion is
metadata only. Every sample records cheap uptime/boot ID/meminfo/swaps/WDK;
only samples 1, 6, 11, etc. traverse smaps (`SMAPS_SCAN=1`). The other samples
are marked `SMAPS_SCAN=0` and contain no PID summaries or raw smaps. This keeps
reset sampling at the nominal 30-second cadence without walking every PID on
every sample. Actual duration and overruns remain recorded, not inferred.

Host verification (no real adb or device sleeps):

```
python3 work/android/swap-guard-supervisor-20261012/verify-collector.py
clean exit=0; make verify exit=0
300 PASS; 0 FAIL; build diagnostics=[]
outer_fence_calls=0; unexpected_guard_calls=0
python3 work/android/swap-guard-supervisor-20261012/verify-actual-fixture.py
actual collector fixture exit=0; independent parser exit=0
processes=2; successful_reads=2; positive_hits=1; swap_sum_kB=128
python3 -m unittest discover -s work/android/swap-guard-supervisor-20261012/lean-verify -p 'test_lean.py'
Ran 30 tests; OK
```

The independent parser now accepts the explicitly marked light samples and
requires the scan-mode and property-status fields. A getprop failure is not a
verdict failure. Positive path hits still require ordinary Swap>0, smaps rc0,
nonempty output and identical device starttime before/after.

A native BusyBox-ash sample of this revision is still required. The ARM QEMU
BusyBox fixture run was not performed; the reviewer explicitly permits the
actual-device sample as the shell-compatibility gate. Once that sample passes,
proceed directly to fresh Phase A and the unchanged verified `pulled.ko` rather
than extending instrumentation work. Start both capture and kmsg before `up`.
The acceptance window is **1800 actual kernel-uptime seconds from the dmesg
Adding-swap timestamp**, not from `up` completion or nominal sample count;
require swap use and at least two distinct positive heavy-scan samples. Give
the bounded capture sufficient setup margin. On timeout/reset, preserve
last_kmsg before any recovery. No kernel source or binary changes in this slice.

## BusyBox-native verification passed; fresh Phase A requested

`a2be610` was executed once as `/system/xbin/busybox sh
/tmp/device-collect.sh` in the unchanged clean stock boot. Evidence:
`work/android/lean-busybox-20261012T123321Z/`.

- Complete sample: device rc0, outer remote rc0, transport rc0, empty stderr.
- Uptime 6916.89 to 6920.12: **3.23 seconds**, versus the prior 38.55 seconds.
- 162 summaries, all rc0; 66 nonempty/stable reads, zero Swap/hits as expected.
- SwapTotal0, no historical Adding-swap, no container/guard; boot unchanged.
- Explicit BusyBox ash and `SMAPS_SCAN=1`; property status `not_attempted`.
- Original `sample.out` is 47,300 bytes including the trailing outer-status
  marker. Root retained its original CRLF payload through END_SAMPLE (47,288
  bytes), rather than asserting that the worker's normalized 46,098-byte
  `sample.payload` was byte-identical. The independent WDK1 format-only parser
  accepts the original payload; `root-format-proof.json` records its evidence.

Root's first surrounding assertion mistakenly expected `sample.status` to be a
bare `0`; its actual format is `transport_rc=0` plus host timestamps. It raised
`AssertionError` after the protocol parse had succeeded. The corrected check
reads that status format and all checks passed. This was a host verification
mistake, not a collector/device failure. No actual-device failure was hidden.

The user-prioritized instrumentation gate is now met. Root requested the one
approved fresh reboot, no-swap-history preflight, exact symbols, unchanged
`f73c9981...` artifact identity, FIQ and single RAM-only load. The worker stops
at the load for root's raw-log inspection; no zram or workload yet. Kernel
source remains unchanged and no permanent writes are authorized.

## Fresh Phase A and second RAM-only load passed

Evidence: `work/android/guard-load-20261012T123543Z/` and root's independent
`root-load-proof.json`. One approved reboot replaced boot `1ff226ae...` with
`4b598319-1683-4f38-991a-aa4ef202a694` (uptime 39.10 at detection). Complete
fresh dmesg had no prior Adding-swap; swap was absent, no container/guard, and
all five expected symbol addresses matched.

The unchanged reviewed artifact was pulled back byte-identical: SHA256
`f73c9981237c76469a60961789fa65a44041d728492bd69fab05fadcbe40ca9e`, source
`e3bcecd`. FIQ final row `2 10 30 1` and actual register `mode=1d` were recorded.
The single insmod returned device `insmod_rc=0`, transport0, empty stderr. Both
sites logged exact stock words, replacement/readback `e3a00000`, `patched 1`,
`patch_readback_ok 1` and `ro_verified 1`. Literal summary:

```
writes 2/2 rollback 0/0 failed 0 pages_rw 2 ro_restored 2 ro_failed 0 stopped 1 ran 1
```

The guard is Live. No swap/workload had started at the load checkpoint. Root
clears the original zram-enabled workload with observer and kmsg active first,
1800 seconds measured from the actual Adding-swap uptime, positive swap use and
at least two distinct valid heavy-scan hits. Timeout/reset recovery stays
dump-first. This load milestone does not itself pass the workload gate.

## Workload outcome: watchdog/FIQ reset, not a passing gate

The approved patched+zram run started with kmsg and capture before up. It failed
on incomplete sample011; reconnect showed a new stock boot and last_kmsg was
captured first, before any recovery. Ten complete samples covered240.01seconds
from zram, with one positive heavy scan. The kernel's last normal traffic was
zram+287.528943seconds and its FIQ event zram+315.211585seconds. The dump confirms
another sleeping-under-spinlock path through `zram_slot_free_notify`; the exact
wait cycle remains unobserved. Full error, unelided traces, source/disassembly
and verified stock poststate are in `2026-10-12-patched-zram-reset.md`.
No extra recovery reboot, retry or additional patch was issued. The two-site
module is not cleared for deployment: the device gate is FAILED, regardless
of the green host build and successful load. Further RAM-only work requires
source/host verification and reviewer clearance again; permanent writes remain
prohibited.
