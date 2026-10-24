# NW-A55 Android optimization — handoff for a fresh Codex session

You (Codex) are the **main worker**: you own the design and drive the implementation.
**Claude reviews, gives feedback, and watches the device runs for safety**; it no longer leads
design. **DeepSeek is the worker** that runs every device command — you steer it the way Claude
did (write `codex-*.txt` to `~/.cache/dswatch/outbox/` and
`dsh-send.py send <session> <file> queue|steer`). **No one types adb against the device except
DeepSeek.** Everything on the device is **RAM-only** — no bootimg/flash/raw writes, because no
brick-recovery path is verified. Reboot is the recovery.

**First objective (user, 2026-10-13): give Fire's own UI a chance (plan A).** Revive Fire OS's
SystemUI (top status bar) and FireLauncher (icons) by cutting the Amazon background bloat and
breaking the `com.amazon.imp` crash-loop, rather than swapping in a lighter launcher. Only fall
back to a lighter launcher if SystemUI still ANRs after the Amazon stack is trimmed and imp is
quieted. Success = no repeating SystemUI/launcher ANR, the status bar draws, and launcher icons
populate. Claude has a baseline census running now; its numbers pick the first cut list.

Hardware reality: ~457 MB usable RAM, stock Fire OS 5 userland that ANRs constantly on it.
Work like a 1990s console dev: measure first, set a RAM budget per subsystem, cut what does
not earn its place, report before/after numbers.

## What already works (verified on device)

- **Kernel guard** `probe/kmod/swap-accounting-guard/` — a RAM-only kmod, loaded via insmod.
  Two stock swap bugs reached through our zram; both patched:
  1. vendor `swap_info_unlock` unlocks the wrong lock (accounting path).
  2. `zram_slot_free_notify` sleeps on an rwsem under the swap spinlock → the trylock-or-skip
     rewrite (9 of its 20 words).
  One stopped transaction over 22 checked words; the mcount word at +0x10 accepts the ftrace
  NOP. Artifact sha256 `cbcf6d6f9360eb4e91538cd6798178748132997a51873e018d20c08b92227571`,
  source commit `e730dd7`. On device: `writes 11/11 … guards 22/22`, clean. With it, zram ran
  **775 s with zero scheduling-while-atomic** (unpatched dies ~102 s; notify-unfixed reset
  ~315 s). The 30-min gate was never completed — adb/USB dropped on screen-off, not a crash.
  Full story: `devlog/2026-10-12-patched-zram-reset.md`,
  `probe/kmod/swap-accounting-guard/NOTIFY-GUARD-DESIGN.md`. **Still owed: the old Codex
  session's source/binary audit of e730dd7.**
- **Touch** — the Himax panel had no `INPUT_PROP_DIRECT`, so Android drove it as a touchpad
  (mouse cursor). Fixed by an idc overlay (`touch.deviceType = touchScreen`) bound RAM-only
  over the read-only `/system/usr/idc`. Commits `99e8a78`, `abe1855` (busybox rm), `f409219`
  (chmod so system_server can read it). User confirmed touch now works without a cursor.

## Governing principle: fix disagreements on the userland side

Kernel and Fire OS userland disagree all over (device identity, charge/backlight,
input, USB) because a Fire OS userland is running on a Sony/MTK Walkman kernel. The rule:
**bend the software to the kernel, not the kernel to the software.** Kernel changes are
RAM-only, risky, and have no brick-recovery path; userland changes (props, idc, overlays,
bind-mounts, `pm disable`, stub providers) are cheap, reversible, and vanish on reboot.
Order of preference: **prop/setting → overlay/bind-mount → container shim → (last) kernel kmod.**
- Userland asks the kernel for a FACT in the wrong shape (device type, charge state, input
  prop) → answer it at the userland boundary (prop, idc, a faked sysfs via bind-mount, a stub
  provider). Do not rebuild the kernel to match Amazon's expectations.
- Userland needs a CAPABILITY the kernel exposes differently (USB gadget, a HAL path) → point
  userland at the kernel's real interface, or a thin container shim. Kernel only if neither works.
- The kernel is touched only for true defects no userland config can avoid (the swap-lock and
  zram-notify bugs, both needed once zram is on).

## Open compatibility problems (next, thinnest layer first)

1. **Power / backlight / charge** — screen-off leaves a black frame while the LCD backlight
   stays on; `stay_on_while_plugged_in` had no effect because Android does not know it is
   charging. Suspect the power/backlight HAL and charge-state plumbing.
2. **adb drops mid-session** — USB vanishes on screen-off/suspend. Container init.rc claims it
   never touches USB, yet Fire OS logged `UsbDeviceManager: resetting config to … adb`.
   Possibly an adbd/USB-function conflict.
3. **UI unresponsive even with touch working** — `com.android.systemui` ANRs on a loop; input
   is probably stuck on a dead window. Probe with `am start` / `input tap` / `screencap`.
4. **`com.amazon.imp` crash-loop** — `RuntimeException: No central device type found`. Needs a
   device-type config or the service removed (Amazon account stack is a non-goal on a Walkman).

## Optimization track (Claude is running this now)

Baseline census in progress (`work/android/census-*`, `bringup-*`): top-PSS processes, total
PSS, package counts, ANR/tombstone counts. Then: strip unneeded Amazon packages at the overlay
level so they never dexopt or start; consider a lighter launcher; report before/after PSS.

## Mechanics

- Repo `~/Projects/nw-a55`, local git, **never push**. Device serial `<serial>` only.
- Host wrapper `tools/android-boot.sh` → device `probe/android/android-boot.sh`
  (`up` / `status` / `sh CMD` / `down`). It loads nothing persistent.
- DeepSeek is `session-8f79c1e3…`, driven via
  `python3 ~/.cache/dswatch/dsh-send.py send <session> <file> queue|steer`; outbox
  `~/.cache/dswatch/outbox/`. **You write the device instructions now**; Claude reviews them
  before they run and judges the evidence after. Hand Claude each device plan before sending it,
  and each run's evidence after, for the safety review.
- Never the quarantined `patched-zram-observe.sh`. On any adb drop or reset: dump-first,
  `/proc/last_kmsg` before any reboot.
