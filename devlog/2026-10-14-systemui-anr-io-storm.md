# SystemUI TIME_TICK ANR is a major-fault I/O storm, not swap thrash

> **UPDATE (same day, superseded as the *proximate* cause):** a forced SIGQUIT dump later
> showed the SystemUI main thread blocked in a synchronous `MediaMetadataRetriever.setDataSource`
> call from Amazon SpeechUi. The I/O storm below is the **amplifier** that makes that call time
> out; the proximate cause and the fix are in [[2026-10-14-systemui-anr-root-cause-speechui]].

2026-10-14. On-disk analysis only, no device touched. Re-read the two live-wedge
snapshots captured 2026-10-13 (`work/android/imp-systemui-anr-20261013T140135Z`,
`work/android/imp-systemui-traces-mem-20261013T141343Z`). They settle the root-cause
question the debloat review left open ("thrash is the working hypothesis, not a confirmed
root cause", `2026-10-14-debloat-codex-review.md:22`).

## The thrash/OOM hypothesis is DISPROVEN by the memory snapshot
From `imp-systemui-traces-mem-.../81-meminfo-keys.txt`, `83-zram.txt`, `84-swaps.out`,
`82-vmstat-keys.txt` (one sample, during the wedge):

- MemTotal 467,512 kB; **MemFree 29,280 kB**; **Cached 165,460 kB**; Buffers 1,836 kB.
- SwapTotal 262,140 kB; **SwapFree 213,440 kB → only 48,700 kB swap in use (18.6%)**;
  SwapCached 20,848 kB.
- zram0: orig_data_size 27,561,984 B (26.3 MB) → compr 9,958,913 B (9.5 MB),
  mem_used_total 13,058,048 B (12.5 MB); ratio 2.77x.
- vmstat (cumulative since boot): pgmajfault 352,002; pswpin 106,681; pswpout 147,249.

Swap is barely touched and zram's whole footprint is 12.5 MB. There are 165 MB of
reclaimable page cache. This is **not** an out-of-memory / swap-thrash wedge. The
`pswpin/out` are modest cumulative totals, not a runaway rate; the pressure shows up as
**file-backed major faults**, not anonymous swap churn.

## What the ANR actually is — the CPU breakdown (the smoking gun)
From `imp-systemui-anr-.../71-anr-blocks.txt`, the first TIME_TICK ANR, 7,422 ms window:

```
89% TOTAL: 22% user + 49% kernel + 18% iowait
35% 29886/system_server: 14% user + 21% kernel / faults: 9735 minor 294 major
21% 64/mmcqd/0:          0% user + 21% kernel
12% 30521/com.android.systemui: 6.4% user + 6% kernel / faults: 9321 minor 350 major
10% 28971/loop0: 0% user + 10% kernel
10% 32/kswapd0:  0% user + 10% kernel
9.8% 29002/loop1: 0% user + 9.8% kernel
```

Second ANR, 5,113 ms window: `51% TOTAL: 15% user + 32% kernel + 3.9% iowait`, with
`55% system_server: 20% user + 34% kernel / faults: 7118 minor 105 major`.

49% kernel + 18% iowait, and the top kernel consumers are **`mmcqd/0`** (the eMMC request
queue), **`loop0` + `loop1`** (the container's loopback-mounted `fire-system.img` and
`android-data.img`, pushed to `/tmp/c/wmmod-android/*.img` by `tools/android-boot.sh:83-84`
and loop-mounted by `probe/android/android-boot.sh`), and **`kswapd0`** (reclaim). Both
system_server and SystemUI take hundreds of major faults inside a single ANR window.

## Causal chain (evidence-backed)
- `imp-systemui-anr-.../30-sweep.txt`: SystemUI main thread (tid 2735) and its Binder_1 /
  Binder_2 threads are **all** parked in `wchan=binder_thread_read`. system_server's main
  thread is also `binder_thread_read` (`imp-display-threads-.../32-wchans.txt`).
- `imp-systemui-anr-.../73-lifecycle.txt`: `am_anr` (TIME_TICK) → `am_kill ... -12 bg anr`
  → `am_proc_died` → `am_proc_start ... restart` → `am_anr` again, on a ~60 s cycle.

So: SystemUI's main thread blocks in binder waiting for a system_server reply; system_server
is itself saturated at 21–34% kernel with hundreds of major faults; the container's /system
and /data are **loopback files on eMMC**, so every major fault is a demand page-in routed
loop → backing file → single eMMC queue (`mmcqd/0`); with only 29 MB free, `kswapd0` keeps
evicting file cache, so hot code pages are re-faulted repeatedly. The TIME_TICK broadcast
cannot complete within the 10 s timeout → ANR → kill → restart → repeat. The keyguard scrim
never yields focus because SystemUI never finishes a frame between restarts.

## What this means for the plan
- **Debloat stays directionally right, but for a different reason.** Removing Amazon apps
  shrinks the *resident working set* → fewer code pages competing for the 165 MB cache →
  fewer major faults → less loop/eMMC queue pressure. It is NOT "free RAM for headroom"
  (there is no swap exhaustion to relieve). This also means app removal helps only insofar
  as it cuts the fault rate — a weaker, slower lever than it looked.
- **A cheaper, fully reversible lever is now in view: the loopback I/O path.** None of it
  removes an app or writes a partition:
  - bump `loop0`/`loop1` (and `mmcblk0`) `read_ahead_kb` so each major fault pulls more per
    I/O — RAM-only, self-restoring;
  - `vm.vfs_cache_pressure` / `vm.min_free_kbytes` to change when kswapd evicts;
  - a `drop_caches` A/B purely to get a clean baseline.
  If a readahead bump alone measurably cuts the TIME_TICK ANR rate, that is a zero-risk,
  zero-removal partial fix and a direct confirmation of this hypothesis. Probe:
  `probe/android/io-vm-tuning-probe.sh` (reversible, measures ANR count before/after).
- **Still unverified:** the exact SystemUI main-thread Java frame (which binder transaction
  it waits on). It needs a forced SIGQUIT dump — `probe/android/systemui-binder-probe.sh`
  does this — but the I/O-storm root cause above does not depend on it. The prior
  traces-mem run did not signal (`00-meta.txt: no_sigquit=1`), so its traces.txt held no
  SystemUI stack (`80-systemui-main-stack.txt: no com.android.systemui main thread`).

## Next device session (one container-up, bundled)
Rather than a cold bring-up only for the Java stack, the next up should collect in one
session: (1) the reversible IO/vm-tuning experiment with before/after TIME_TICK ANR counts,
(2) the SIGQUIT Java stack + binder-peer dump. App removal (debloat) touches the /data image
and stays gated behind that evidence and the user's direction call.

See [[2026-10-14-debloat-codex-review]], [[nw-a55-imp-prop-fix]].
