# NW-A55 Android roadmap

Long-term plan for running Android on the Sony NW-A55 Walkman. Recorded
2026-10-13. Strategy, not a schedule.

## The three-way mismatch we are bridging

What is actually running is a **Fire 7 (`ford`, built for MediaTek MT8127)
userland on the Sony Walkman kernel (3.10.26, `bx8590` / MT8590, `ro.hardware=icx`)**,
with Amazon-specific expectations on top that neither SoC satisfies. So every
disagreement falls into one of three layers:

1. **Kernel ↔ hardware** — the Walkman's own kernel and silicon. Fixed, expensive,
   RAM-only patching, no brick-recovery for persistent writes.
2. **Userland ↔ kernel/SoC** — a MT8127-targeted Android expecting MT8590 to look
   like MT8127: audio HAL, GPU (Mali), display, power/backlight, charger, USB gadget,
   input. This is the deepest and most reusable body of work.
3. **Amazon-specific** — Fire OS brittleness (imp "No central device type" fatal
   crash), the Amazon service weight, FireLauncher, account/OTA/registration. This
   layer disappears entirely if we change the OS.

Guiding rule (see `devlog/2026-10-13-optimization-handoff.md`): **bend the software
to the kernel, not the kernel to the software** — prop → overlay/bind-mount → container
shim → (last) kernel kmod.

## The reusable base (carries to every later Android)

Everything in layers 1–2 is an asset we build once and reuse:

- Kernel guard: swap-accounting lock fix + non-blocking zram notify (RAM-only kmod).
- zram tuning that lets ~457 MB usable RAM hold a full Android userland.
- Touch classified as a touchscreen (idc overlay).
- Power / backlight / charge-state bridging (open).
- USB gadget / adb stability on MT8590 musb (open).
- Container boot recipe: pivotinit namespace, mounts, Mali swap, masked MT8127 HALs.

Only layer 3 is thrown away when the OS changes.

## Phase 1 — Fire OS 5.1 (current)

We run Fire OS first because a device image for this hardware class **already exists
and boots today**; nothing else is ready to start on. The goal is not Fire OS for its
own sake — it is to **validate the reusable base** against a real, complete userland.

Exit criteria: SystemUI draws the status bar, FireLauncher shows icons (keyguard
clears — currently stuck on `KeyguardScrim` because SystemUI ANRs on imp's providers),
and touch / power / charge / adb are stable. Current cut: supply
`ro.product.config.type` to quiet imp, then trim non-UI Amazon services.

## Phase 2 — an AOSP-family build (Lineage / AOSP)

Once the base is validated, move to a lighter, AOSP-closer userland. Main win: layer 3
vanishes — no imp brittleness, no Amazon service storm, a lighter SystemUI/launcher that
is far more likely to come up cleanly on 457 MB. Caveat: on pre-Treble Android (5.1/7.1)
HALs are still board-specific, so the layer-2 MT8590 bridging is still required — but it
is already solved from Phase 1, so this phase is markedly easier. Lower difficulty is the
whole point of doing Fire OS first.

## Phase 3 — Android Go edition (8.1 / 9 / 10 Go), long-term

Android **Go** is purpose-built for exactly this memory budget (~512 MB and below):
tuned LMK, zram on by default, lighter system apps and SystemUI, smaller dex footprint.
On paper it is the ideal long-term fit for 457 MB. Known barriers, recorded honestly:

- **Kernel gap.** Android 8+ expects kernel ~3.18+; this is 3.10.26. Expect missing
  syscalls, an older SELinux policy version, an older binder (no binderfs), and no GKI.
  Either a newer kernel for MT8590 (may not exist) or real backporting.
- **Treble / HALs.** Treble + GSI from 8.0 could let a Generic System Image boot, but it
  needs a vendor partition of **MT8590 HALs for the new Android**, which we do not have and
  would have to build or port.
- **Memory.** Higher Android is heavier, so zram/swap becomes mandatory — which is exactly
  what the Phase 1 kernel guard enables. The swap-stability work pays its biggest dividend
  here.

Treat Phase 3 as an aspiration gated on the kernel/HAL barrier, not a near-term step.

## Cross-cutting later task: robustness hardening

Brittle "crash fatally on a missing non-essential value" paths (imp's device-type throw is
the first; others likely) should be made to degrade gracefully rather than kill the process —
*after* the prop/config workarounds are in. Lower priority than the workarounds, but it is the
difference between a device that limps and one that is actually robust.
