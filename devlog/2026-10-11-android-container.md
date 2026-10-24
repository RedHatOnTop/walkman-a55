# 2026-10-11 — Android userspace under its own init (container bring-up)

Follows `devlog/2026-10-11-wifi-soc-android.md`, which ended with GPU and
SurfaceFlinger running from a chroot but no property area and no logs. The
blocker there was structural: starting services by hand cannot give zygote the
property service, `logd`, or init's `on property:` handling. This session runs
Android's own init instead, inside a container.

## What was built

| Path | Role |
|---|---|
| `tools/android-container.sh` | Assembles the container root into `work/android/container.tgz`. |
| `probe/android/container/init.rc` | A minimal init.rc, not FireOS's. |
| `probe/android/container/default.prop` | `ro.zygote=zygote32` and dex2oat sizing. |
| `probe/android/container/ueventd.extra.rc` | Node permissions Sony's own `/dev` does not grant. |
| `probe/android/container/devclean.sh` | Removes the raw eMMC nodes before services start. |
| `probe/android/android-boot.sh` | Device side: prepare, Mali swap, container build, start/stop. |
| `tools/android-boot.sh` | Host side driver, mirrors `tools/android-gpu.sh`. |
| `probe/devtools/pivotinit.c` | Mount namespace + container root over `/`; `--join` for debugging. |

## Why init in a container, and why not a plain chroot

FireOS's `init` is static ARM and SELinux is off by kernel cmdline
(`androidboot.selinux=disabled`), so it needs no policy. Running it under a
chroot gets the property service and `logd`, but Amazon's zygote refuses to fork
system_server: it builds a file descriptor table and aborts on any inherited
descriptor whose path is not on a hard-coded whitelist. Two things had to be
true, and neither is about SELinux:

1. **No stray descriptors.** `adbd` inherits Sony init's property-area
   descriptor and passes it down to everything it spawns. Sony's
   `/dev/__properties__` is unlinked, so the path reads back as
   `(deleted)` and can never be whitelisted. `pivotinit` closes everything above
   stderr before exec.
2. **The property area has to resolve as `/dev/__properties__`.** Zygote is not
   in init's mount namespace (ART's `Runtime::InitZygote` unshares one, which is
   where the earlier "Failed to mount() rootfs as MS_SLAVE" came from), so the
   descriptor's mount is unreachable from zygote's root and this kernel's
   `d_path` falls back to the absolute path `/tmp/aroot/dev/__properties__`.
   Making the container root the namespace root fixes it.

`pivot_root()` is not usable on this kernel: Sony's `/` is a bare rootfs mount,
and 3.10's `pivot_root` rejects a root mount with no parent
(`mnt_has_parent()` in `fs/namespace.c`). `pivotinit` moves the container root
over `/` with `MS_MOVE` and `chroot()`s into it instead.

## What each failure turned out to be

In the order they were hit, because several are non-obvious:

1. `binder: BINDER_SET_CONTEXT_MGR bad uid 1000 != 100` — the kernel remembers
   the euid of the first context manager since boot, which on this device was
   Sony's `hagodaemon` as uid 100. servicemanager runs as `user 100`.
2. `ro.board.platform` empty, so the MTK HALs were not found — FireOS loads
   `/system/build.prop` with the `load_all_props` command from a
   `load_all_props_action` trigger that a minimal init.rc never fires.
3. SurfaceFlinger could not open `/dev/ion` — Sony's `/dev/ion`, `/dev/mtk_disp`,
   `/dev/mtk_ovl` and `/dev/mtkfb_vsync` are root-only; Android's ueventd rules
   normally grant more. Added in `ueventd.extra.rc`.
4. `app_process: Error creating cache dir /data/dalvik-cache/arm` — the
   directories Android's `post-fs-data` creates are not optional.
5. `dex2oat ... Failed anonymous mmap(0x0, 402653184) Out of memory` — the
   kernel's heuristic overcommit refused ART's heap reservation on a 456 MB
   device. `vm.overcommit_memory=1` plus capped `dalvik.vm.*`. Fire's build is
   `ro.build.type=user`, so `/data/local.prop` overrides are ignored and the
   properties have to be set from init.rc before zygote starts.
6. `ART CreateJavaVM failed` after `Failed to mount() rootfs as MS_SLAVE` — that
   mount needs its target to be a mount point, so the container root is bound
   onto itself first.
7. `Resources$NotFoundException: Resource ID #0x2070002` in
   `ExcessivePowerUsageMonitorAndKillCallback.vendorInit` — FireOS carries a
   second resource package that is only loaded when `AMAZON_EXTRA_RESOURCE_LIST`
   is exported.
8. `installd: media fs prepare failed: /data/media/0/Android/data/` — with
   installd dead, PackageManager fails every package with "Unable to create data
   dirs". The media and misc trees are now declared in init.rc.

keystore is deliberately not started: `keystore.mt8127.so` wants the MTK TEE
keymaster, which is not reachable here, so it would only crash-loop.

## Verified on the device

- Android's init runs; ueventd coldboot populates a full `/dev`; the property
  service answers `getprop`; `logd` runs and `logcat` works.
- servicemanager holds the binder context-manager slot as uid 100;
  SurfaceFlinger starts against `gralloc.mt8127` / `hwcomposer.mt8127` and
  composes on the panel.
- zygote completes class and resource preload and forks system_server.
- **The framework boots to its own UI.** SurfaceFlinger logged
  `Boot is finished (81391 ms)` and `screencap` (800x480) shows the FireOS
  first-boot screen, "Optimizing system storage and applications…", on the
  Walkman's panel. The boot animation service starts and exits as it should.

## The rest of the path to that screen

Three more failures stood between the package scan and a visible framework, each
one a missing service or directory rather than anything subtle:

- `BatteryService: onStart threw` with
  `IBatteryPropertiesRegistrar ... on a null object reference` — healthd
  publishes that registrar and was not running. `/sbin/healthd` comes from the
  FireOS ramdisk and is static, so it just needed adding to the container.
- `PackageManagerService.deleteTempPackageFiles` NPE ("Attempt to get length of
  a null array") — `listFiles()` on a missing `/data/app-private`. Rather than
  add one directory at a time, init.rc now mirrors FireOS's own `post-fs-data`
  list (`work/android/fire-ramdisk/init.rc`, the `mkdir /data/...` block),
  ownership and modes included.
- netd is started so the framework's connectivity services have their sockets.

## Safety, revisited

- `devclean.sh` also removes `/dev/pmt` (the MTK partition-table ioctl device)
  and `/dev/trustzone`, alongside the raw eMMC nodes.
- ueventd keeps watching for uevents after the coldboot, and any new block
  uevent would recreate the nodes devclean just removed. It is now started
  without a class and explicitly `stop`ped right after `devclean.sh`, so
  `class_start core` cannot bring it back. Nothing in this userspace needs
  hotplug yet.
- vold is deliberately not started, per the project's safety rules. The
  consequence is visible in the log: `MountService` waits for it
  (`Thread MountService still waiting for MountService ready...`, then
  `VoldConnector: Communications error ... No such file or directory`). Whether
  the boot completes without it is the next thing to measure.

## Reading the panel without eyes (`probe/devtools/fbcap.c`)

`screencap` returns SurfaceFlinger's logical composition, and on this SoC the
composition goes out through the MTK overlay engine, so `/dev/fb0`'s memory is
not what the LCD receives either. `MTKFB_CAPTURE_FRAMEBUFFER` makes the overlay
engine write its own output into a user buffer:

- `mtkfb.h:43` defines it as `MTK_IOW(3, unsigned long)` = `_IOW('O',3,4)` =
  `0x40044f03`.
- `mtkfb.c:2680` `copy_from_user()`s a 4-byte user pointer and passes it to
  `mtkfb_capture_framebuffer` (`mtkfb.c:2098`), which for the non-UI-layer case
  calls `DISP_Capture_Framebuffer` (`mtkfb.c:2152`).
- `disp_drv.c:1803` maps the buffer through M4U for
  `DISP_GetScreenWidth() * DISP_GetScreenHeight() * bpp / 8` bytes and returns
  zeros when a layer is secure or the display is early-suspended
  (`disp_drv.c:1826-1830`). `DISP_GetScreenWidth/Height` are `lcm_params->width
  /height` (`disp_drv.c:1528/1541`), i.e. the panel's own resolution.

`fbcap` opens `/dev/graphics/fb0`, takes the geometry from `FBIOGET_VSCREENINFO`,
mlocks a resident buffer (M4U writes into it) and issues the ioctl. dmesg
confirms the decoded name: `cmd=0x40044f03(MTKFB_CAPTURE_FRAMEBUFFER)`.
`tools/fbcap2png.py` turns the raw capture into a PNG; the channel order is
B,G,R,A, measured from the orange Fire logo (`1b a6 fa ff`).

The first capture settled a question nobody could answer by looking: the panel
buffer is 480x800 (not 800x480 — the byte count is identical, and rendering it
as 800x480 is visibly garbled) and the composition was written into it **rotated
90 degrees**. Fire's `libsurfaceflinger.so` carries the string
`persist.panel.orientation`; setting it to 0 instead of Fire's tablet value 270
made the next capture upright. That A/B capture is the evidence — a call site in
the disassembly was not found, so the mechanism is measured, not read.

"Upright in the buffer" only means "upright on the device" if buffer row 0 is
the physical top, so the device was rebooted to stock and the same tool run
against Sony's own UI. It comes out upright (the USB mass-storage screen, with
its status bar along the top and nav bar along the bottom), so the buffer
orientation is the device orientation and the question is closed. The three
captures are `panel.png` (270, sideways), `panel2.png` (0, upright) and
`stock.png` (reference, upright).

## The gate after vold: audio

With vold running, boot proceeds until system_server waits for
`media.audio_flinger` and `media.audio_policy`:

```
I/ServiceManager: Waiting for service media.audio_flinger...
W/AudioSystem: AudioFlinger not published, waiting...
```

Those are `mediaserver`, which is not started: its audio HAL is Fire's
`audio.primary.mt8127.so` and this device's codec is Sony's CXD3778GF driven by
Sony's own stack, so the audio path is a decision to make rather than a service
to switch on. That is where the bring-up stands.

## vold, and what actually gated boot completion

`SystemServer.java:721` calls `mountService.waitForAsecScan()` on the main
thread unless `mOnlyCore` (which is only set for `vold.decrypt` encryption
states, `SystemServer.java:340-346`), and `MountService.java:866-867` counts that
latch down only from `onDaemonConnected()`. So without vold, `startOtherServices()`
never returns and `sys.boot_completed` is never set — the log showed
`Thread main still waiting for MountService ready...` and
`VoldConnector: Communications error ... No such file or directory`.

The project rules allow vold only with no `/fstab.icx` in the container. Read
from source: `vold/main.cpp:80-82` logs "Error reading configuration ...
continuing anyways" when `process_config` fails, and `process_config`
(`main.cpp:154`) builds `/fstab.<ro.hardware>` and returns -1 when it is
missing, before the volume loop. So vold manages no volumes, never reads an
fstab, and still answers the MountService socket. FireOS's `vold` binary carries
the same strings. The device script now refuses to start if any `$ROOT/fstab.*`
appears.

With vold running, MountService enumerates storage and SurfaceFlinger logged
`Boot is finished (54242 ms)`.

## The /dev cleanup was not actually running

The first version of the cleanup was a no-op and the check that "verified" it was
wrong twice over. Both are worth keeping:

- **`exec` does nothing in this init.** AOSP 5.1's `do_exec` is a stub that
  returns -1 (`init/builtins.c:257-260`), so `exec -- /system/bin/sh
  /devclean.sh` is accepted, logged, and skipped. Measured: with `exec`, the
  container's `/dev` still held 41 raw eMMC names after a full boot, while
  running `devclean.sh` by hand removed them. It now runs as a oneshot service
  started with `start` (which clears `SVC_DISABLED`, `init/init.c:180`), before
  `start vold`, and `devclean.sh` says in its header that it is asynchronous.
- **`grep -E '^(mmcblk|...)$'` never matched `mmcblk0`.** The first count of
  "0 forbidden names" was a false negative twice: the pattern only matched the
  literal name `mmcblk`, and adb's CR line endings broke the anchors. The check
  is now `ls -l /dev` and looking for the `b` type column, plus a name scan for
  `mmcblk`, which is what the claim actually needs.

## /dev/trustzone has to stay

Removing it (as first suggested) breaks the display:

```
E/uree.so: UREE_OpenFd open fd fail, err = No such file or directory
E/hwcomposer: [BLIT] UREE_CreateSession failed (Non-specific cause.), aborting
F/libc: Fatal signal 6 (SIGABRT) ... in tid (surfaceflinger)
```

`libtz_uree.so` opens `/dev/trustzone` and `/dev/tzmem`, and
`hwcomposer.mt8127.so`'s `BlitManager` creates a uREE session through it at
startup, so SurfaceFlinger aborts and crash-loops. It is a TEE interface rather
than storage and is not on the project's forbidden list. `/dev/pmt` is still
removed.

## Stale containers held /dev/alarm

With the cleanup finally working, boot still hung, this time with
`AlarmManagerService: Unable to wait on alarm: Device or resource busy` in a
tight loop. The cause was not the cleanup: **four `system_server` processes were
alive at once**, all holding the global `/dev/alarm`. `container_pids()` only
matched the *current* init's mount namespace, so processes left behind by
earlier container runs — each in its own namespace — survived `down()` and
accumulated.

`container_pids()` now matches "not in the namespace Sony's init is in", which
covers stale runs too, and `up()` kills anything left over before building a new
container. After that: one `system_server`, zero alarm errors, and
`Boot is finished (70352 ms)`.

## Verified after the fix

- `ls -l /dev | grep '^b'` in the container namespace is empty: no block devices
  at all, so no `mmcblk0*`, and no `pmt`/`mbr`/`ebr*`/`bootimg`/`nvp` names.
- `/dev/trustzone` is present, `UREE_CreateSession` aborts are zero, and the
  panel capture shows the boot screen upright.
- `Boot is finished (70352 ms)`, `AlarmManagerService` alarm errors zero.

## mediaserver, and the HAL that had to stay out

system_server waits for `media.audio_flinger` and `media.audio_policy` before it
finishes booting. The safe way to publish them was measured before anything was
started, because the obvious HAL is not safe:

- `audio.primary.mt8127.so` links `libnvram.so` and `libcustom_nvram.so`, whose
  strings include `/dev/block/platform/mtk-msdc.0/by-name/NVRAM`,
  `.../by-name/PRO_INFO` and `/data/nvram/APCFG/APRDCL/Audio_*`. Both partitions
  are on the project's forbidden list.
- So the HAL stays out of the container: `android-boot.sh` binds `/dev/null`
  over `audio.primary.mt8127.so`. `hw_get_module()` finds the path, `dlopen()`
  fails, `loadHwModule()` logs and returns NULL.
- That is enough because neither service needs a HAL to be published.
  `AudioFlinger::onFirstRef` loads none at all
  (`frameworks/av services/audioflinger/AudioFlinger.cpp:212-235`: standby time,
  a PatchPanel, the mode) and HALs load lazily through
  `findSuitableHwDev_l`. The policy service's failure path returns early
  (`AudioPolicyService.cpp:87-113`), and that happens *after* `addService`.
- `libaudiopolicymanagerdefault.so` reads `/system/etc/audio_policy.conf` or
  `/vendor/etc/audio_policy.conf`; neither exists in the Fire image, and the
  library has no fatal paths, so it initialises with defaults.

Measured result: mediaserver runs, and `service list` in the container shows

```
80   media.audio_policy: [android.media.IAudioPolicyService]
117  media.audio_flinger: [android.media.IAudioFlinger]
```

and the "Waiting for service media.audio_flinger..." lines drop to zero.

## The app fork needed emulated storage

With the audio wait gone the boot went further and then zygote aborted in a loop
with `JNI FatalError called: RuntimeAbort`, once every ten seconds, exactly when
system_server started `com.amazon.kindle.otter.oobe`. That is
`MountEmulatedStorage` in `com_android_internal_os_Zygote.cpp`: it returns false
when `EMULATED_STORAGE_SOURCE`, `EMULATED_STORAGE_TARGET` and
`EXTERNAL_STORAGE` are unset, and the caller turns that into "Cannot continue
without emulated storage" plus `RuntimeAbort`, which kills the zygote — so every
app start killed the framework.

FireOS sets those in `init.ssd.rc` (`work/android/fire-ramdisk/init.ssd.rc:29-41`),
along with the `/mnt/shell/emulated` and `/storage/emulated` tree and the
`/sdcard` links. init.rc now mirrors that. After it, zygote aborts are zero and
the app fork succeeds.

## The device resets before the first boot finishes

`sys.boot_completed` still has not been observed. The framework gets to the
first-boot dexopt of the system apps, and the device then hard-resets with no
kernel message at all: `/proc/last_kmsg` ends mid-stream, no panic, no OOM, no
`lowmemorykiller` kill. The boot reason is `wdt_by_pass_pwk`, the same value an
ordinary reboot produces, so it is not diagnostic on its own.

`tools/memwatch.sh` samples the device from the host every five seconds while a
run is in flight, because the device cannot tell us anything after the fact.
What the samples say, and what they rule out:

- **Memory is not the story.** At the stall: `MemFree` 26 MB with 259 MB in
  reclaimable page cache, `allocstall 0` (no direct-reclaim stalls),
  `pswpout 353` pages with 260 MB of zram free, and `D_user=[]` — no userspace
  process was ever in uninterruptible sleep. `D_kernel` sits at a constant 10
  because the MTK kernel threads (`wdtk-0/1`, `hang_detect`, `pmic_thread_kth`,
  `GCPU`, `entropy_thread`, ...) live in D on this device permanently, so that
  count carries no signal at all.
- **Power does not fit after all.** An earlier version of this entry blamed it.
  The samples show `VOLTAGE_NOW` holding at 3.951-3.974 V while load rose from 6
  to 10: a one-time step down from 4.07 V, not a collapse, and far above any
  Li-ion cutoff. The 95% -> 73% gauge move over the same window is probably
  voltage-derived rather than coulomb counting. Do not report power as the
  cause.
- **Heat is not ruled out.** The zones read 46-49 C at the stall (against 40 C
  measured five minutes after an earlier reset, which says nothing about the
  stall itself).

### The hang-detect path, and why it is not the answer either

The `hang_detect` kernel thread and the `RT_Monitor` node looked like a perfect
fit, so the source was read rather than guessed. `monitor_hang.c`:

- `/dev/RT_Monitor` takes `AEEIOCTL_RT_MON_Kick` (`monitor_hang.c:94`,
  `:128-133`), which calls `aee_kernel_RT_Monitor_api` (`:336`). A kick with a
  positive argument *arms* the monitor with a countdown; 0 disarms it.
- The thread (`:275-327`) only counts down when `hd_detect_enabled == 1` **and**
  a task named `system_server` exists. On reaching zero it prints
  `[Hang_Detect] we should triger HWT`, calls `aee_kernel_warning`, sleeps ten
  seconds, then does `local_irq_disable(); while (1);` — an IRQ-off spin that the
  hardware watchdog turns into a reset with nothing further logged.

That would explain both the stall and the silent `wdt_by_pass_pwk` reset, so it
was measured instead of assumed: `dmesg | grep -ci hang_detect` is **0**
throughout container runs (the three boot-time lines age out of the ring
buffer and no `counts down` or `enabled` line ever appears), and no process
holds `/dev/RT_Monitor` in either state. The monitor is not armed, so it cannot
be what fires. FireOS's own libs and framework jars carry no `RT_Monitor` or
`hang_detect` string either.

### Bisect by stage: all three stages survive, so the container itself is implicated

The first attempt measured nothing (withdrawn above). Two bugs were fixed before
the rerun:

- `tools/android-bisect.sh` did not push `mali_r4p0.ko`, so `swap()` died at
  `insmod` and `bisect` never reached `load_start`.
- `load_start`'s background children inherited the adb shell's stdout, so
  `adb shell sh android-boot.sh bisect N` never returned and the host sampler
  never started — the log simply stopped at the `load:` line. The three
  background commands now carry `</dev/null >/dev/null 2>&1`, as `cachecap_start`
  already did.

With both fixed, every stage carries the same load (two busy loops, one per
core, plus a loop streaming the whole 1.2 GB image) and the sampler actually
runs:

| Stage | Contents | Result |
|---|---|---|
| 1 | `prepare`: Sony stack stopped, stock Mali | survived 10 min |
| 2 | + Mali `r4p0` loaded | survived ~28 min |
| 3 | + zram (262 MB) | survived 10 min |

Stage 2's survival was read off the device rather than the log: the two busy
loops showed 27:45 and 28:17 of CPU time with `mali_r4p0` loaded and the Sony
stack stopped, and no reset had happened.

**So none of the setup steps is the cause — with two caveats that matter.**

Stopping the Sony stack, swapping Mali, turning zram on and the synthetic load
all run for ten minutes or more without a reset, while a container run dies at
4-6 minutes. What is left is the container itself: its init, the services it
starts, dexopt, SurfaceFlinger and system_server.

But two things were **present and not exercised**, so they are not cleared:

- **zram was idle.** Stage 3 had it on with `Swap used 0`, and the load adds no
  anonymous memory, so nothing pushed pages through it. A container run does
  reach `pswpout 353`. zram under real swap pressure is untested.
- **The load only reads.** It streams the eMMC image and spins the CPU; it
  writes nothing. Dexopt writes about 160 MB into the data image (ext4 on a
  loop file on a vfat file on eMMC — measured 40 MB to 199 MB in one run) while
  memory is tight. That write path is untested too.

If the container run with `dexopt-watch` does not name an entry or a fixed
time, the next stages are: stage 4 = stage 3 plus a loop writing a bounded
~200 MB scratch file on `/tmp/c`, deleted afterwards; stage 5 = a memory hog
that makes `pswpout` climb. Both need `Swap used` and `pswpout` in the samples.


`tools/android-bisect.sh` runs a stage and then samples uptime every ten
seconds, reporting the last uptime before the device stops answering. Every
stage is meant to carry the same synthetic load: two busy loops (one per core)
and a loop streaming the 1.2 GB system image.

An earlier version of this entry reported "stage 2 survived, stage 3 reset at
500 s" and drew conclusions from it. **That table was wrong and is withdrawn:**

- `swap()` ends in `die "insmod failed"` (`android-boot.sh:99`), which exits the
  script, so `bisect` never reached `load_start` in stages 2 and 3. Only stage 1
  printed `load: 3 busy loops + image reader`; stages 2 and 3 ran with **no load
  at all**, so their survival and reset times say nothing.
- The stage 3 "reset at uptime 1911.83" was not stage 3. `tools/android-bisect.sh`
  for stage 3 was still sampling while a second job (the cache-cap A/B) ran
  `down; up` on the same device at uptime ~1598. Stage 3's last uptime matches
  that container's uptime at the same moment, and stage 3's load figure jumped
  from 10.3 to 12.4 at uptime 1599. **Two jobs were driving one device.**

The lesson is procedural: one device job at a time, and a stage only counts as
having run if its own `load:` line appears.

The load itself was also wrong in a way worth keeping: `dd if=$SYSIMG bs=1M
count=64` with no `skip` re-reads the same first 64 MB, so after the first pass
it is all page-cache hits and no I/O. The load now streams the whole image.

### The cache-cap A/B

`cachecap` runs `echo 3 > /proc/sys/vm/drop_caches` every five seconds during a
full container run. The run reset at ~360 s, the same window as every other
container run. `Cached` during the run stayed between 165 and 258 MB, but
`Cached` is not the right field to judge the intervention by: `drop_caches` does
not free mapped or tmpfs pages. The same samples read Mapped 31-52 MB and Shmem
0.6-5.2 MB, with the rest file cache from the loop-mounted images. Whether the
cap took effect is therefore not settled by that number, and this A/B is
**inconclusive** rather than negative.

### The data image was too small to finish dexopt — and that is all it was

`tools/android-boot.sh` created the data image at 256 MB. Measured inside the
container with `df /data` (via `pivotinit --join`): **229.9M total, 180.8M used,
49.2M free** once dalvik-cache held ~50 entries. The oat output for the ~248
dexopt inputs cannot fit in that, and the effect was visible as a throttle:
entries crawled 41 -> 58 over minutes, against ~30 MB/min earlier.

The image is now 1900M. (2 GB does not fit: the push fails with `remote File too
large`. The likely cause is adbd's 32-bit file offset rather than FAT32's 4 GB
limit, but that is unverified — the partial file was gone before it could be
checked.)

**This is not the reset cause, and an earlier version of this entry said it
was.** The 1900 MB run reset at **16.1 minutes of container time** — with
`/data` only 635.8M used of 1.8G, MemFree at 175 MB, thermal 51 C, and every
sampler gap a clean 11 s. The user watched that run reboot with the progress bar
at about 95%. A nearly full filesystem, a nearly empty one, plenty of free
memory and a tight one have all now been seen at a reset. Capacity explains why
dexopt was slow; it does not explain why the device reboots.

What the bigger image did buy is speed: entries went 31 -> 141 in 16 minutes
against 41 -> 58 over minutes before.

### Second reset sample: 2 minutes in, warm cache

The CAP_SYS_BOOT run reset at about 22:54:50 KST, which is **1.5-2 minutes after
`up` began at 22:52:40** — with 143 dalvik entries already cached from the 1900M
run. Two streams and the sampler were running this time and survived on the
host:

- `last_kmsg` saved (`/tmp/nw-a55-last_kmsg-capsysboot.txt`, 7167 bytes):
  **zero** `Restarting system` / `Power down` / `System halted` / `Kernel panic` /
  `Oops` matches, so again no `reboot(2)` and no kernel restart path. Its last 60
  lines are **all** `mtkfb_ioctl` and `amazon_touch` does not appear in it at
  all, so this tail says nothing about what preceded the stop and is **not** the
  same signature as the 1900M one — an earlier version of this entry claimed it
  was.
- `logcat -v time` (4042 lines) ends on boot-phase lines, not a crash:
  `installd: Failed to remove /cache/dalvik-cache/PLACEHOLDER`,
  `memtrack_amz: generic_inspect_heap error to open
  /sys/kernel/debug/ion/heaps/ion_mm_heap`, and
  `PackageRecency: Matched blackListPattern for
  [com.amazon.firelauncher/...]`, at container clock 10:02:59.3.
- `kmsg` (3056 lines) ends on surfaceflinger `mtkfb_ioctl` lines, last at
  device uptime 1153.32 s.
- `container-watch` got two samples, 13:54:11 and 13:54:44 host time, uptime
  1137.69 and 1150.97 — 33 s of host time against **13.3 s** of device uptime,
  so the second call ran about **20 s late**. The 1900M rows had host and uptime
  deltas matching, so this is the 20-27 s stall seen in the earlier runs, back
  again.

Timing, from the files' own last-write times rather than from the sampler's
stamps:

| what | last write (KST) | = UTC |
|---|---|---|
| `/tmp/nw-a55-kmsg.txt` | 22:54:24.56 | 13:54:24Z |
| `/tmp/nw-a55-logcat.txt` | 22:54:24.39 | 13:54:24Z |
| `/tmp/nw-a55-watch-capsysboot.log` | 22:54:44.82 | 13:54:44Z |
| reset (new boot at 14:03:10Z minus uptime 500.80 s) | 22:54:49 | 13:54:49Z |

So the kernel stream and the logcat stream **stop together**, within 0.2 s of
each other at 13:54:24Z, and the device resets **25 s later**. The sampler's
13:54:44 stamp is not when its data was read: `container-watch.sh` reads `up=`
early in an iteration and stamps the row after four adb calls, so the row
carrying uptime 1150.97 was read around 13:54:24 — the same moment the streams
went quiet — and the call itself took about 20 s. That also re-reads the earlier
"20-27 s sampler stall": it is the same 25 s window, seen from the host side.

An earlier version of this entry said the kernel tail ended 22 s after the
logcat tail; that premise was unsound and is withdrawn. What is left unverified:
what happens in those 25 s, and whether the reset is the cause of the silence or
the silence is a symptom of whatever stops next.

The silence is real, not a transport artifact: the saved on-device ring
(`/tmp/nw-a55-last_kmsg-capsysboot.txt`, read from `/proc/last_kmsg` after the
reset) ends at uptime **1153.326728**, and the adb kmsg stream ends at
**1153.326728** as well — the same line. So the kernel stopped printing 25 s
before the reset while adb shell calls still answered. Since surfaceflinger's
`mtkfb_ioctl` lines were arriving every ~10 ms up to that point, the whole
logging path — kernel and userspace — went quiet together, and then the device
went down 25 s later.



What the two resets share is not a signature but a position: both land near the
end of dexopt — this one with a warm cache at 2 minutes, the 1900M one cold at
16 — while the 256M resets came at a low bar, 41-58 entries. That is a
correlation across three samples, not a mechanism, and "not a function of dexopt
progress" was too strong.

`sony=0` after such a reset is expected, not a fault: `prepare` stops the Sony
stack (`probe/android/android-boot.sh:77`) and nothing restarts it until a
reboot.


### The last 20 seconds: a process storm, collapsing memory, and page-fault waits

`tools/stream-25s.sh` ran at 1 s with the kmsg and logcat streams alongside, and
the device reset again. The last 30 rows (`/tmp/nw-a55-stream-run3.log`) show
what the earlier 25 s of silence actually contained — the polling sampler could
not see it because it read once every 10 s and stamped late:

| time | uptime | tasks (loadavg f4) | MemFree | Cached | D-state tasks |
|---|---|---|---|---|---|
| 10:40:51 | 2246.88 | 2/249 | 62,744 | 240,000 | kernel threads only |
| 10:40:53 | 2248.56 | 2/254 | 54,836 | 247,496 | kernel threads only |
| 10:40:55 | 2250.42 | 1/260 | 37,928 | 256,868 | kernel threads only |
| 10:40:58 | 2252.83 | 4/298 | **11,212** | 274,020 | + `ndroid.settings:SyS_epoll_wait` |
| 10:41:04 | 2257.98 | 10/385 | 26,868 | 235,684 | `ndroid.systemui:0`, `amazon.redstone:0`, `.service:remote:binder_thread_read` |
| 10:41:08 | 2261.17 | 3/475 | 99,504 | 137,064 | **`ndroid.systemui:sleep_on_page_killable`**, `amazon.platform:sleep_on_page_killable` |
| 10:41:11 | 2265.10 | 4/**504** | 89,792 | 132,376 | `ndroid.systemui:sleep_on_page_killable`, `amazon.platform:sleep_on_page_killable` |

then the stream ends.

Three things move in the last ~20 s, and the end state is **not** plain
exhaustion:

- **The task count roughly doubles**, 249 -> 504, and load goes 11.5 -> 14.87.
  Field 4 of loadavg is what shows it; the earlier sampler cut it off.
- **Free memory dips to 11,212 kB, but for about 2 s only** (one sample). It is
  45,160 kB at 10:41:00 and **89,792-99,504 kB at the end**, while the tasks in D
  state are waiting on pages. Memory was tight, not gone, when the device stopped
  answering.
- **Userspace tasks appear in D state** with `wchan` = `sleep_on_page_killable`,
  waiting on a page: `ndroid.systemui`, `amazon.platform`, `amazon.redstone`,
  `ndroid.settings`. Every D entry before this was an MTK kernel thread — but
  that "first time" is only first time in the **1 s** stream. The 10 s polling
  sampler would rarely catch a window this short, so it is not evidence that
  earlier runs lacked it.

`Cached` falls hard too: **274,020 kB at 10:40:58 to 132,376 kB at 10:41:11, a
142 MB drop**, of which about 99 MB (235,684 -> 137,064 kB) lands between
10:41:04 and :08. Something released that much cache while free memory was
recovering. The kmsg has 35 lowmemorykiller lines in this window, including
`lowmem_shrink return directly, due to 12067 (thumbs thread) is dying`, so LMK
kills are the obvious candidate, but **what released the cache is not
established** by these samples.

The `w` writes did land — six `SysRq : Show Blocked State` blocks are in
`/tmp/nw-a55-kmsg-run3.txt` — but they did **not** capture the app stacks:
`sleep_on_page_killable` appears zero times in that log, and the stacks the
blocks carry are mostly the sampler's own shell in `__down`. The blocked app
stacks are still unread.

Timing, corrected: the container init was up at 14:31:31Z, the last stream row
is up=2265.10, and the device booted at about **14:33:08Z** (uptime 290.72 read
in the 23:37:59 call, not the 23:38:12 one). So the kmsg stream's last write,
14:32:39.9Z, is **~28 s before the reset**, and the reset is **~97 s after
container init**. Run 2 measured 25 s for the same interval.

### What the reset probably is, marked as inference

The watchdog source in this tree says: the kicker threads run SCHED_FIFO
(`wd_common_drv.c:288-292`), kick every 20 s (`g_kinterval = 20`, `:209`), and
only restart the external watchdog once **every** CPU has set its kick bit
(`local_bit == wk_check_kick_bit()` -> `mtk_wdt_restart(WD_TYPE_NORMAL)`,
`:331-345`), with the hardware timeout reported at 30 s (`mtk_wdt.c:501`).

A silent reset 25-28 s after the kernel stops printing fits a **hardware
watchdog bite after a whole-kernel stall**: the FIFO kickers stop, and the
external watchdog fires 30 s later. That is an **inference, not a measurement** —
nothing in the logs names the watchdog, and no `Hang_Detect`, `WDK`, `mali`,
`kbase`, `mmc`, `EXT4` or `I/O error` line appears in the last 60 s of either
the stream or the ring.

It also means the memory story cannot be the whole cause on its own: **memory
thrash and D-state do not stop SCHED_FIFO kickers.** The `min_free_kbytes` A/B
was aimed at the wrong thing and is dropped as a next step.

What the ring does end on is MTK's own exception marker:
`ram console header, hw_status: 1, fiq step 0.` and `bin log 0.`, immediately
before the trailing ioctls and ring garbage. That is the ipanic/ram-console
header being written, which is consistent with the watchdog path dumping on its
way out — again an inference from one line, not proof.

Other corrections from the same read:

- The on-device ring's last timestamp is **2270.503197**, about **5 s after** the
  stream's last row (up=2265.10), so the ring outlives the adb stream slightly.
- `lowmemorykiller` lines are **4 real kills** (`Killing '`) and **25 `is dying`
  lines**, which are not kills (`lowmemorykiller.c:270-275`). The earlier "35 LMK
  lines" was a combined grep count.
- The tail greps used `^\[` while the lines begin `<6>[`, which is why the
  non-ioctl tail looked empty; with `grep -av mtkfb_ioctl` the tail is the ram
  console header above.




Both runs' sampler logs were re-read for `entries` and for the `dex=` field,
which is printed only when a live dex2oat is found:

| run | rows | first row | final rows | `dex=` seen |
|---|---|---|---|---|
| 1900M | 92 | 13:19:14, entries=31 | 13:53:29 -> 13:54:17, all entries=**143** | **never** (0 rows) |
| CAP_SYS_BOOT | 2 | 13:54:11, entries=**143** | 13:54:11 and 13:54:44, both 143 | **never** (0 rows) |

Two readings, and only one of them is evidence:

- **The `dex=` field proves nothing either way.** It appears in **zero** rows of
  both logs, so it never caught a dex2oat alive. A dex2oat run is about 1-4 s per
  entry while the sampler ticks every 10 s, so the sampler is too coarse to see
  one. Absence here is not absence of dexopt.
- **The entry count is the useful number.** In the 1900M run it grew
  136 -> 137 -> 140 -> 141 and then sat at **143 for eight consecutive samples**
  (about 80 s) before the reset, and the CAP_SYS_BOOT run started at 143 and
  never moved. So no dexopt output was being written when either device went
  down.

Consequence: a dexopt-**write** hypothesis — stage 4, the bounded scratch-write
loop in `android-bisect.sh` — loses its support. That is not the same as
excluding it: dex2oat could be reading and compiling without having written an
entry yet, and the sample before the flat stretch does move. But the write path
is no longer the first thing to test.

For the next sampler: the `dex=` field needs a much shorter interval to be worth
anything, or it should become a dex2oat start count read from `/proc` on every
tick.

### Two read-only checks that die at the next reboot

Both were taken from the current Sony boot and saved to host files
(`/tmp/nw-a55-dmesg-sonyboot.txt`, `/tmp/nw-a55-last_kmsg-1900m.txt`).

**FAT dirty bit: unresolved, and the check was not clean.** `dmesg` has **zero**
`FAT-fs` or `vfat` lines. The rule is that the warning
("Volume was not properly unmounted...") appears on a rw vfat mount when the
dirty bit is set, and `fat_remount()` clears it on a ro remount — so absence
would point at something having run Android's `android_reboot()` (which syncs
and writes `u` to `/proc/sysrq-trigger`, remounting block-backed filesystems
read-only). But `/tmp/c` is not mounted now, so I cannot show from this boot that
a rw vfat mount of `/contents` happened in it at all, and without that the
absence proves nothing. It needs a controlled mount: mount `/contents` rw once
and read the result. Unresolved.

**The data image may have been touched.** `dmesg` contains no `loop0` EXT4-fs
line, so no journal replay was logged. But the loop device was set up with
`losetup /dev/loop0 <image>` **without `-r`**, so it was read-write, and ext4 is
then free to replay a dirty journal before honouring the read-only mount. I
cannot prove the image was not written. Nothing outside that one file was
touched, and no partition, fstab or formatting was involved — but the honest
statement is that the ro inspection was not guaranteed read-only, and the next
one will use `losetup -r`.


`/proc/last_kmsg` from the 1900 MB reset was saved to
`/tmp/nw-a55-last_kmsg-1900m.txt` before the next run could overwrite it, and it
settles the userspace-reboot question:

- This tree prints `KERN_EMERG "Restarting system.\n"` in `kernel_restart()`
  immediately before `machine_restart()` (`kernel/sys.c:410-423`), and
  `"Power down.\n"` in `kernel_power_off()` (`:457-464`). Any `reboot(2)`, and
  any in-kernel restart, goes through those.
- The saved log contains **zero** matches for `Restarting system`, `Power down`,
  `System halted`, `Kernel panic` or `Oops`, and its last line is cut mid-stream
  (the raw tail is ring-buffer garbage after
  `[BLS]disp_bls_set_backlight: 172`).

So nothing called `reboot(2)`, and no kernel restart path ran. Dropping
`CAP_SYS_BOOT` from the container's bounding set cannot change the outcome, so
that experiment is void — the premise was wrong, not the measurement. The
opt-in stays in `pivotinit` (`PIVOTINIT_DROP_SYS_BOOT`, verified by
`CapBnd=0x1FFFBFFFFF` against Sony's `0x1FFFFFFFFF`), but it is not the answer.

What the last lines do show is a **monotonic backlight ramp-down**, 184 -> 172
in about 0.296 s, from a Java thread (`Thread-101`). That rate is about 40
units/s, which is AOSP 5.1.1's `BRIGHTNESS_RAMP_RATE_SLOW`
(`DisplayPowerController`), and that constant is used only for auto-brightness
adaptation while the screen is **on** (`slowChange`); turning the screen off
calls `animateScreenBrightness(brightness, 0)`, i.e. no ramp. Fire's framework is
modified, so this is a strong match rather than proof. What it supports is:
screen on, auto-brightness adapting, display ioctls still running, then silence.
An earlier version of this entry read it as the framework turning the screen off
and pointed at the suspend path; **that is withdrawn — the suspend hypothesis has
no evidence.** The user's "screen went dark, then the Walkman logo" is what any
reset looks like.

So the firm statement is narrow: a hard stop with no printk, **10 ms** after the
last backlight write (the 25 ms figure is the ramp period, not the gap),
observed once. After the next reset, `last_kmsg` gets saved again and its last
non-ioctl line and timing compared with this one.



### What the reset leaves behind, and what each check can and cannot show

After the 1900 MB reset the data image — a loop file on `/contents`, not a
partition — was mounted on the host side and inspected. What the checks support,
and what they do not:

- `/data/anr/traces.txt` is 0 bytes and still has the mtime init gave it, so no
  ANR was dumped. Supported.
- `/data/system/dropbox` is empty. That shows there is no dropbox entry. It does
  **not** show the reset bypassed the framework's crash reporting: the
  `BootReceiver` that writes the dropbox boot entries only runs on
  `BOOT_COMPLETED`, which this run never reached.
- `/data/tombstones` held five files of equal size. An earlier version of this
  entry said all five were my own sampler's `df` dying on `SIGPIPE`; that was
  written from reading **one** file (`tombstone_09`: `name: df`, `signal 13`)
  with `tail -5` hiding 00-04, and equal size is suggestive, not a read. **That
  claim is withdrawn as unverified** — re-reading them failed because
  `losetup -r` is unusable on this busybox, so 00-04 are still unread.

Nothing here shows what the reset wrote or did not write on the way out: a hard
reset drops unflushed writes, so absence proves nothing. Nothing here shows it
bypassed the framework's watchdog either, because the system_server watchdog
restarts zygote rather than resetting the device. The only firm statement about
the mechanism is the `last_kmsg` result above: no `reboot(2)`, no kernel restart
path.

The sampler needed fixing before the next run: its `df` output went into a pipe
that could close early, so `df` took `SIGPIPE` and debuggerd dumped a tombstone
roughly every sample. debuggerd keeps only ten slots, so that noise evicts real
crash evidence, and it costs ptrace and log work every 11 s on a 456 MB device.
`df` is now captured into a shell variable instead of a pipe.


- The reset happens only with the container running: stock controls ran 9+ and
  10+ minutes with no reset, and stage 1 (Sony stack stopped, full synthetic
  load, stock Mali) survived ten minutes.
- Memory *exhaustion* is ruled out (allocstall 0, no userspace D state,
  reclaimable cache, swap barely used). Page-cache growth and I/O pressure is a
  different claim and is still open.
- The hang-detect path is ruled out (not armed, no RT_Monitor holder).
- Power is not supported. Heat is open.


Two fixes came out of this that are worth keeping regardless:

- **lmkd was missing.** ActivityManagerService applies oom_adj *only* by writing
  `LMK_PROCPRIO` to the lmkd socket (`ProcessList.java:600-614`; `writeLmkd` at
  `:645-655` retries three times with one-second sleeps and gives up, with no
  fallback to `/proc/<pid>/oom_score_adj`). Without it every `setOomAdj` cost
  AMS three seconds ("SLOW OOM ADJ: 3003ms") and nothing was ever ranked. FireOS
  runs it as a critical class-core service (`work/android/fire-ramdisk/init.rc:634-637`);
  the container now does too, minus `critical`, because a critical service that
  exits makes init call `do_reboot()` and that would reset the device.
- FireOS's lowmemorykiller parameter permissions (`init.rc:419-424`) are applied
  too, and AMS/lmkd then sets the targets: the sampler caught
  `minfree` going from `1024,...` to `18432,23040,27648,32256,36864,46080`.

`tools/android-boot-cycles.sh` runs repeated boots and reports the dexopt
backlog, since the dexopt results land in `/data/dalvik-cache` on the persistent
loop image. That is the workaround for a boot that takes longer than the reset
window; it has not yet produced a completed boot.

Two changes were made in one run before the supervisor pointed out the
attribution problem: dex2oat's `-Xmx` came down from 128m to 96m and zram went
from 192 MB to 256 MB. Neither is the reset cause (memory is ruled out above),
but they are not independently verified either.



- `sys.boot_completed` is still not set: system_server now waits for
  `media.audio_flinger` / `media.audio_policy`, i.e. `mediaserver`, which is not
  started because the audio HAL is a decision (Fire's `audio.primary.mt8127.so`
  versus Sony's CXD3778GF path).
- Input and Bluetooth HALs are untouched.
- The container is non-persistent: the Mali driver swap and everything else go
  away on reboot, which is the only way back to the Sony stack.



