# SystemUI TIME_TICK ANR root cause: a main-thread MediaMetadataRetriever call in Amazon SpeechUi

2026-10-14. Ground truth from a forced SIGQUIT dump of the live container
(`work/android/sysui-sigquit-run`, captured this session: guard loaded, `up`, SystemUI
up, `kill -3` the systemui pid). This is the Java-level cause that
[[2026-10-14-systemui-anr-io-storm]] could not see without a thread dump. The I/O storm in
that devlog is real but is the **amplifier**, not the proximate cause.

## The main thread is blocked in a synchronous media binder call
From `30-traces.txt`, both captured SystemUI instances (sysTid 6996, then the SIGQUIT
target 7951) show the identical main-thread stack, state=S:

```
native: __ioctl -> IPCThreadState::talkWithDriver -> waitForResponse -> transact -> BpBinder::transact
native: libmedia.so  MediaMetadataRetriever::setDataSource
  at android.media.MediaMetadataRetriever.setDataSource(MediaMetadataRetriever.java:190)
  at com.amazon.knight.speechui.SpeechUiPresenter.getSoundFileLength(SpeechUiPresenter.java:603)
  at com.amazon.knight.speechui.SpeechUiPresenter.<init>(SpeechUiPresenter.java:491 / :384)
  at com.amazon.knight.speechui.SpeechUiService$SimClientConnectionListener.onConnect(SpeechUiService.java:106)
  at amazon.speech.simclient.SimClient.onConnectedToService(SimClient.java:251)
  at android.app.LoadedApk$ServiceDispatcher.doConnected(LoadedApk.java:1244)
  at android.os.Handler.handleCallback -> Looper.loop -> ActivityThread.main
```

`11-binderproc.txt` corroborates it: the main thread (`from 7951:7951`) has an outgoing
binder transaction `to 1589` — and pid 1589 is **mediaserver** (the pre-N MTK process that
also hosts CameraService/`camera.mt8127.so`/DrmManagerClient and serves
`media.metadata_retriever`). `10-wchan.txt` = `binder_thread_read`.

## The full chain, and why the period is ~60 s
`50-logcat.txt`:
- `SPCH-SIM_SpeechInteractionService(3798): Account Registered Condition Check account (false)
  available (false) ... Restarting in (60000) ms` — this is a Walkman with **no Amazon
  account**, so `amazon.speech.sim` (pid 3798) runs its SIM service in a 60 s reconnect loop.
- On each (re)connect: `SPCH-SIM_SimClientConnectionManager: register client:
  android.uid.systemui` → `SPCH-TABLET_SPUI_SpeechUiService(6996): Connected to SIM`.
- That connection fires `SimClientConnectionListener.onConnect` **on the SystemUI main
  thread**, which constructs `SpeechUiPresenter`; its constructor calls `getSoundFileLength()`
  → `MediaMetadataRetriever.setDataSource(fd, offset, length)` — a **synchronous binder call
  to mediaserver, on the main thread**.
- While that call is outstanding the Looper cannot dispatch the TIME_TICK broadcast → ANR at
  the 10 s timeout → `am_kill -12 bg anr` → `am_proc_start ... restart` → the new instance
  re-binds the speech service on the next 60 s cycle → same block. `40-window.txt` shows
  `mFocusedWindow=Window{KeyguardScrim}`: the keyguard scrim keeps focus because SystemUI
  never finishes init between restarts, so the launcher stays covered and the UI is unusable.

## Why the I/O storm is the amplifier, not the cause
`MediaMetadataRetriever.setDataSource` reads/probes a sound file through mediaserver; both the
service and the file are on the loopback-backed images, and during the wedge the system is in
the major-fault I/O storm ([[2026-10-14-systemui-anr-io-storm]]: 49% kernel + 18% iowait,
loop0/loop1/mmcqd/kswapd). That makes a call that would be fast on real storage take seconds,
which is what pushes it past the 10 s ANR timeout. But the defect is **a blocking media call
on the UI thread inside a service-connect callback** — it would stutter even without the
storm; the storm just guarantees the timeout.

## The fix (highest leverage, directly testable, reversible)
Cut the trigger: stop `amazon.speech.sim` (pid 3798) from running. With no speech SIM service,
the SystemUI SimClient never reaches `onConnect` with a live service → `SpeechUiPresenter` is
never constructed → no main-thread `setDataSource` → the TIME_TICK ANR cannot occur,
independent of memory/IO. This is the debloat STUB/REMOVE candidate `amazon.speech.sim`
(18,616 kB in the RSS snapshot), now promoted from a RAM line item to the **direct cause of
the ANR**. The G1 test is a single reversible mask — `pm disable amazon.speech.sim` (revert
with `pm enable`) — watched for: TIME_TICK ANR = 0, SystemUI restart = 0, keyguard dismissed,
launcher usable. One-at-a-time, as the debloat plan prescribes.

This reconciles the debloat/Codex review (which kept speech.sim as a STUB "until B3"): it is
not merely resident weight, it is what wedges SystemUI. IO/vm tuning
(`probe/android/io-vm-tuning-probe.sh`) and broader debloat remain useful for general
responsiveness, but they are secondary to removing this trigger.

See [[2026-10-14-systemui-anr-io-storm]], [[2026-10-14-debloat-codex-review]],
[[nw-a55-imp-prop-fix]].
