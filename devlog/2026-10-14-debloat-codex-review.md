# Debloat plan — Codex review deltas + a location-subsystem lead

2026-10-14. The recovered Sonnet debloat report (`debloat/REPORT-recovered-20261014.md`) was
reviewed by a read-only Codex consult. Codex did real investigation (MinervaApi DEX disassembly,
AOSP source, stored-log mining), not just comments. Key corrections, each verified by me against
raw data:

## Verified corrections to the plan
- **"K0 = safe" is wrong.** The manifest graph misses system_server's *runtime* binds. Verified
  counter-examples in `work/android/imp-systemui-anr-20261013T140135Z/50-logcat.out`:
  - `com.amazon.platform` runs `com.amazon.geo.platform.GeoDeviceService`, bound by
    `<=Proc{29886:system/1000}` (system_server) — L443-444, 573. Not an inert leaf.
  - `com.here.odnp.service` runs `.LocationService`, also bound by system_server — L438-439,
    574-575.
  → platform and odnp are **system_server-bound location services**, reclassified from
  "low-risk K0" to "framework-connected, individually mask-tested."
- **persistent is 7→4, not 7→3.** The 5 K0 REMOVEs include only 3 persistent (tcomm, msm,
  assetsync); speech.sim stays until B3's STUB resolves. (My earlier "7→3" was wrong.)
- **~64,620 kB is a loose estimate**, neither an upper bound nor guaranteed: per-process shared
  pages differ (can't uniformly subtract zygote RSS) and it assumes the 3 STUBs fully vanish.
- **thrash is the working hypothesis, not a confirmed root cause** — major fault/IO load is real
  but the SystemUI Java stack is UNVERIFIED; `binder_thread_read` alone doesn't fix the peer.
- **G1 was too weak.** "No ANR increase" passes even with the existing failure intact. Real
  success gate: TIME_TICK ANR = 0 AND SystemUI restart = 0 AND actual unlock/home/settings/
  keyboard usable.
- **client.metrics: mask-first beats an incomplete stub.** Minerva client (disassembled) handles
  the bind-false branch and buffers on null service (50-item queue) — a service-absent path
  exists. `bindService` is boolean, not null; service-absence != permission-denial. The services
  already declare no `android:permission` (defined perm 0x3), so "drop signature permission" was
  an inaccurate model. A stub would need the full Binder descriptor/transaction/callback contract,
  not an empty onBind.
- Sequencing: **one-at-a-time, not batched** (attribution). assetsync → msm first; then odnp,
  tcomm, platform individually; STUB 3 individually.
- Mask-release does not restore deleted `/data`; keep an identical snapshot for A/B. "Rebuild" =
  container image only, distinct from a device system-partition write.
- Debloat-first is right over kernel tuning: LMK `minfree` can't selectively kill Amazon
  persistent (lowmemorykiller.c picks by free/file + oom_score_adj), zram calls lzo directly.
  Keep kernel/zram levers as the fallback if debloat doesn't clear the ANR.

## Lead worth chasing (device-up)
The **location subsystem is itself unstable**: GeoDeviceService ANRs as a foreground service
(bind delay 20,102 ms → crash → restart, L691-692/765-766/831) and odnp LocationService
crash-loops (proc died → 13,976 ms reschedule, L936-939), both bound by system_server. So
removing platform/odnp may cut wedged fg-service churn, not just RAM — and this churn may be
entangled with the SystemUI ANR. `location.fused` is KEEP, so the relationship needs care.
Investigate next time the container is up.

See [[nw-a55-imp-prop-fix]], `debloat/REPORT-recovered-20261014.md`.
