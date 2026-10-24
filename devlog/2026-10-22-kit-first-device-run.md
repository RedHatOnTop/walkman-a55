# The install kit's set on the unit, and keystore

The first run of what `kit/make-install.sh` writes on the A55 itself. The set came from
walkman-manager's Firmware Studio (build `2026-10-22-154109`, kit 44fb2be content, the
same files as a hand run of the kit) and went onto `/contents` over adb, not through the
WALKMAN drive and the UPG trigger the app uses; that path waits for the owner. The picker
hook on the unit is the one the kit's UPG installs (`hook md5 9e86b977…` in
`PICKERUPG.log` since 10-21), so no UPG ran.

## Setup

- The unit came back on adb at 18:25 KST and the picker started Android from the old set
  on its own (`boot-choice` 2). `tools/android-boot.sh down` from this branch took
  `nw_down` (d0b2199) and the unit stayed on USB for the rest of the session; that is the
  first device check of d0b2199.
- The old set was moved aside, not removed: `boot.pre-kit`, `fire-system.img.pre-kit`,
  `android-data.img.pre-kit` under `wmmod-android/`. The owner's Android data is the
  last of these.
- 15 boot files and both images pushed: 9.5 MB/s over adb (724 MB in 72.8 s, 2000 MB in
  210 s). md5 on the unit matched for all 17.

## First boot from the kit's set, fresh /data

- `adb reboot`; the picker started Android on its own, from the new set.
- `sys.boot_completed` 823 s after adb came back: the first boot's dexopt.
- Host modules: `mali_r4p0`, `swap_accounting_guard`, `mtk_wmt_wifi_soc`, `compat`,
  `cfg80211`, `wlan_mt`. The container's own files are owned by uid 0.
- CM's setup wizard comes first on a fresh /data:
  - Welcome, then Wi-Fi (Wi-Fi turned on and listed the networks around), skipped.
  - LineageOS features, location, date and time.
  - The time zone page offers Midway Island (GMT-11): nothing sets
    `persist.sys.timezone`.
  - Start, then **"Unfortunately, Settings has stopped."**
- The crash: `SetupDefaultProfileReceiver` (on `com.cyanogenmod.setupwizard.SETUP_FINISHED`)
  calls `ProfileManager.updateProfile`, and system_server's side throws "Attempt to invoke
  interface method 'int android.security.IKeystoreService.zero()' on a null object
  reference" back into Settings. The boot also logged `F/SystemServer` NPEs in
  `KeyStore.contains`. keystore had been left out of `init.aosp.rc` on purpose: the image's
  `keystore.mt8127.so` wants the MTK TEE keymaster, and `hw_get_module()` tries the board
  name (`mt8127`) before `default`, so keystore exited at start. RELEASE.md already listed
  "Settings dies on the lock-screen page (no keystore)".
- WalkmanUI (`org.walkmanui`, not a home app; Trebuchet is home): Displayed in 3.8 s, the
  library screen with Sony's first-run help over it, the resources from
  `/system/etc/walkmanui/res`. Its first frame is a blank gradient while it loads. The log
  has 14 QML `TypeError`s at start.
- Bluetooth: on from Settings, the scan filled the screen with nearby devices (8 shown);
  turned off again.

## keystore

Tried live first: a tmpfs copy of `/system/lib/hw` without `keystore.mt8127.so`, bound over
the directory, and keystore started by hand. keystore registered and the wizard's broadcast,
sent again, completed with no crash. But every app started afterwards segfaulted in
`GraphicBufferMapper::registerBuffer` (fault addr 0x80, the gralloc module NULL), WalkmanUI
twice until ActivityManager gave up, and the bind could not be taken off ("Device or
resource busy"). The copy had the same 23 files with the same sizes. After a clean reboot
WalkmanUI started again.

So only the one file is bound (b09d38c): `android-boot.sh` binds `keystore.default.so`,
AOSP's software keymaster under the same module id, over `keystore.mt8127.so` read-only,
as it masks the stock audio HAL, and `down` takes it off. `init.aosp.rc` makes
`/data/misc/keystore` and runs keystore as in AOSP.

Run again with the kit rebuilt at b09d38c (only `android-boot.sh`, `container.tgz` and
`MANIFEST` differ) and another fresh data image:

- `sys.boot_completed` after 852 s.
- `software keymaster bound: …/keystore.mt8127.so <- …/keystore.default.so (ro)` in
  `boot-android.log`; keystore runs as `keystore`, `android.security.keystore` is found,
  `/data/misc/keystore` is `keystore keystore 0700`; no `KeyStore` NPEs at boot.
- The wizard ends on the launcher, no crash, `device_provisioned` and `user_setup_complete`
  1, no `FATAL EXCEPTION` in the log.
- Settings' screen-lock page (`ChooseLockGeneric`) opens with its five choices. No PIN was
  set.
- WalkmanUI Displayed in 3.8 s, no `F/libc`.

## From the picker to the boot animation

The owner saw a long wait after choosing Android. Sampled once a second from adb:
`android-boot.sh boot` sat in its `sleep 1` loop until 87 s, Android init started at
102.5 s and `bootanimation` ran from 103.8 s. The picker's "starting Android" frame stayed
on the panel the whole time.

- The loop waited for `init.svc.load_sony_driver=stopped`, which this unit never sets:
  Walkman One's `/init.rc` has no `load_sony_driver` service and no `optproc_for_boot.sh`.
  Its `boot_complete.sh` does the same insmods and `dacdat` loads, and init `exec`s it to the
  end before the hook. So the loop ran its full 60 s on every start, as
  devlog/2026-10-21-picker-recovery.md had measured. It now runs only when `/init.rc`
  `exec`s `optproc_for_boot.sh` (stock), and waits there while `load_sony_driver` is
  running, the hook is alive, or `optproc_for_boot.sh` runs. Busybox on the unit: the
  device's `/init.rc` skips the wait, a copy of the stock one takes it.
- The rest was busybox forks, about 50 ms each while the unit boots. Measured on the unit
  with 136 processes, one call vs. one per item: the mount-namespace scan 7.05 s vs. 0.16 s,
  the `/dev/stpbt` holder scan 13.9 s vs. 0.27 s, and 35 key layouts copied 3.66 s vs.
  0.15 s. `container_pids` and `bt_holders` are now one `ls -l` each through awk, the same
  pids as the loops on the live unit (16 container pids; holders of `/dev/stpbt`,
  `/dev/alarm`, `/dev/binder`). The idc, key layout and permission overlays are two
  copies each. `mtkbt` is polled every 0.1 s.
- `say` stamps the uptime, so `boot-android.log` times the steps.

| | chose Android | init starts | `bootanimation` |
| --- | --- | --- | --- |
| before | ≈ 27 s | 102.5 s | 103.8 s |
| without the wait | 25.6 s | 41.5 s | 43.2 s |
| and fewer forks | 26.0 s | 31.0 s | 31.7–32.7 s |

The overlays the container got are the same files, modes and md5s as before (71 lines
compared). Both runs are the kit's set (install7, install8).

## Six minutes of dexopt on every boot

That boot then sat 357 s with no system or events log line between `ProcessCpuTracker` at
00:45:04 and `LockSettingsService` at 00:51:01, `performBootDexOpt()` in SystemServer's
order, although the system image had not changed since the boot before. The main buffer
had wrapped. Sampled in the next boot: `dex2oat` on `/system/app/Firefox/Firefox.apk`,
started 00:54:52.09, ended 01:00:52.42 with `DexInv: --- END '/system/app/Firefox/Firefox.apk'
--- status=0x0100, process failed`. That is dex2oat's watchdog ("dex2oat did not finish
after %d seconds" in the image's binary, exit 1 at 360 s). The oat never got written, so
every boot compiled Firefox again for six minutes, and an owner opening Firefox would have
waited the same.

The image's `installd` appends `dalvik.vm.dex2oat-flags` to each dex2oat command line and its
`dex2oat` takes `--no-watch-dog`; `init.aosp.rc` sets it beside the other `dalvik.vm.*`
caps. On the unit with the kit's set (install9):

- the boot after: `dex2oat took 384.266s (threads: 2) … swap=48MB`, no failure line;
  `enable_screen` at 447 s;
- the boot after that: no "Running dexopt" line, `pms_ready` 42.6 s, `ams_ready` 47.5 s,
  `enable_screen` 55.4 s (the boot before the fix: 423.8 s).

Firefox then starts from that oat (no dex2oat at launch). Its first window stayed white for
about a minute while the Gecko processes loaded `libxul.so`, then showed the "Welcome to
Firefox" onboarding under an ANR dialog: `executing service
org.mozilla.firefox/androidx.work.impl.background.systemalarm.SystemAlarmService` timed out
during that first start (WAIT dismissed it). Open.

About three minutes later, at 417.68 s uptime with Firefox still running, the kernel oopsed
and the hardware watchdog reset the unit (`hw_status: 1`, nested panic): `Binder_1` (pid
1175) faulted on user address b6ed99f4 ("Unable to handle kernel paging request") while
kswapd0 on CPU1 was in `swap_writepage` ← `shrink_page_list`. The guard was loaded, and this
is neither of the two swap bugs it covers. `last_kmsg` is in
`work/android/reset-20261022T1220Z/`. A second Firefox start, after the reset, timed out
two more services (`crashhelper.CrashHelper`, a `GeckoChildProcessServices$tab`) and raised
an ANR for `org.mozilla.firefox:crashhelper`, so the ANRs are not only the first run's.

## Open

- Fresh-/data defaults: time zone GMT-11 until the owner picks one in the wizard, English,
  and the swipe lock screen on every wake although the A55 has a HOLD switch.
- What the panel shows during the 14-minute first boot was not captured.
- The app's own path: copying onto the WALKMAN drive, the read-back, and the SCSI trigger.
  In Android mode the unit shows no USB disk (lsblk), as the app's drive gate says.
- The unit is left on the kit's set with this session's test data. Going back: `down`,
  then move the three `.pre-kit` names back.
