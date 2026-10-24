# G1: cutting the speech SIM service eliminates the TIME_TICK ANR — but pm hide is the wrong cut

2026-10-14. Live intervention test in the same container session that captured the SIGQUIT
root cause ([[2026-10-14-systemui-anr-root-cause-speechui]]). Evidence in
`work/android/g1-speechsim` (v1) and `work/android/g1v2` (v2).

## Result — the root cause is confirmed by intervention
With the Amazon speech SIM service (`amazon.speech.sim`) actually down, over a 150 s window:
- `ANR in com.android.systemui` = **0**, `am_anr ... systemui` = 0, `TIME_TICK ... timed out`
  = 0, `am_proc_died ... systemui` = 0, SystemUI restart starts = 0.
- `Connected to SIM` = 0 and `getSoundFileLength`/`SpeechUiPresenter` = 0 — the main-thread
  media call never happens because the bind never connects.
- `41-ps-speech-final.txt` empty — the service stayed down for the whole window.

Baseline (v1, 90 s, service up) had 2 TIME_TICK ANRs; the historical loop was ~17/run. So
removing the speech trigger removes the ANR. The root-cause chain is proven, not just observed.

## Stopping a persistent, bound service took more than `pm disable`
- `pm disable amazon.speech.sim` → "new state: disabled" (succeeds), but the process
  **respawned** after kill (pid 3798 → 13345): it is `persistent`, so zygote/AMS restarts it
  even disabled.
- `am force-stop` (rc=0) did not keep it down either.
- `pm hide amazon.speech.sim` ("new hidden state: true") **+** component-disable of
  `amazon.speech.sim/amazon.speech.sim.service.SpeechInteractionService` finally kept it down
  (post-kill ps empty).

## The catch — `pm hide` crashed system_server (new, not pre-existing)
After the hide, a crash appeared that is in **none** of the pre-cut logs and 5× in the
post-cut log:
```
FATAL EXCEPTION IN SYSTEM PROCESS: Thread-239
java.lang.NullPointerException: Attempt to read from field 'int android.content.pm.ApplicationInfo.uid' on a null object reference
  at com.android.server.am.ProcessRecord.toShortString(ProcessRecord.java:588)
  at ActivityManagerService.dumpProcessOomList / dumpProcessesLocked / reportMemUsage
```
Hiding the package left a `ProcessRecord` with a null `ApplicationInfo`; AMS's low-memory
`reportMemUsage` path then NPE'd dumping the OOM list → **system_server crashed**. That
cascaded: SystemUI (pid 14805) died with `Unable to create service KeyguardService: Package
manager has died`, reached through `AmazonVolumePanel.<init> → SimClient.getBindIntent`
(SystemUI's VolumePanel also binds the SIM client); the restarting system_server then NPE'd on
`Display.getMetrics`. Net: the TIME_TICK ANR loop is gone, but `pm hide` traded it for a
system_server crash, so the UI is still not clean (`mCurrentFocus=null`, keyguard
`mShowing=true mOccluded=false isSecure=false` — the fresh-boot default).

## Conclusion and next step
The **fix direction is right** (remove the speech trigger) and is now evidence-backed. The
**runtime `pm hide` method is unsafe** here because it nulls AMS state. The clean cut is at the
container-image level: remove or stub `SpeechInteractionManager.apk` from the system image
`/priv-app` so the service never starts at boot — no persistent respawn, no ProcessRecord, no
null-ApplicationInfo hazard. This is the debloat REMOVE/STUB for `amazon.speech.sim` done
properly, and it needs a container-image rebuild (reversible: rebuild without the change), not
runtime pm. It is the first real image cut, so it is a decision point, not an autonomous step.

Still unproven after a clean cut: whether a fully usable home/keyguard-dismissed UI appears
(this run never got there because of the induced crash). That is the real G1 gate and needs
the clean cut plus a focus/interaction check.

See [[2026-10-14-systemui-anr-root-cause-speechui]], [[2026-10-14-debloat-codex-review]],
[[nw-a55-systemui-anr-root-cause]].
