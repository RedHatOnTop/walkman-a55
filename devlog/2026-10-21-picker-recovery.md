# Picker installation failure investigation

## Scope and gates

The owner asked this session to finish the NW-A55 boot-picker issue. The previous
session's device reads are in `work/rescue-20261021/`. Partition writes and UPG
execution remain operator steps; this session does not authorize them.

1. Establish the failure stage, read-only.
   - Compare the live system dump with the saved Sony script.
   - Inspect the updater log and current boot-selection metadata, without
     printing personal NVP records.
   - Compare boot/recovery images if the metadata does not explain the failure.
   - Gate: distinguish measured facts from hypotheses; no device flash writes.
2. Implement the smallest justified fix on the host.
   - Add failing regression tests before changing the affected code.
   - Preserve the Sony path and bound failure handling.
   - Gate: relevant tests, script syntax checks, and package round-trip checks.
3. Rehearse and verify on the device after recovery.
   - Operator executes any required persistent recovery/install operation.
   - Verify both selections, timeout, and error fallback, with panel captures.
   - Gate: do not declare the picker complete without actual cold-boot evidence.

## Initial evidence

- `adb devices -l` lists only the ZX507. It must not be used as a fallback target.
  USB inventory includes `0e8d:2000` (MediaTek preloader).
- `debugfs -R 'cat /bin/bootswitcher.sh'
  work/rescue-20261021/android-live.orig.img` shows the original Sony script.
  The directory listing contains no `bootswitcher.sony.sh`.
- `debugfs -R 'cat /update.log' work/rescue-20261021/var-live.img` ends with
  `NW_WM_FW.UPG not found`. This is not evidence that the picker ran.
- The recovered updater checks the UPG's existence before extraction and
  `/update.sh` execution (`install_update_script/icx_start_update.sh:141-151`).
  That failure branch requests an update-flag reset and reboot. Whether that
  request completed is unverified.
- The previous session completed system and var reads (`EXIT=0` and exact dump
  lengths); no recovery write was made there.

## Read safety

The additional diagnostic command is mtkclient `ro`, not `wo`, `w`, erase, or
format. Its dispatch calls `da_ro`, which calls `readflash` in the local checkout
(`work/mtkclient/mtkclient/Library/DA/mtk_da_handler.py`). DA upload and watchdog
setup affect volatile state; output images and session state are host files.
Read size and the explicit `Dumped offset` result must also be checked because
the CLI dispatch does not reliably turn a failed read into a nonzero exit code.

## Failure isolated

- Additional `ro` reads completed at the recorded partition-map offsets:
  NVP (15 MiB), LK through the end of sec_ro (0x2660000 bytes), and misc
  (512 KiB). Explicit `Dumped offset` lines and output lengths matched.
- LK and bootimg are byte-identical to the 2026-10-10 backup. Misc is also
  identical, with an empty boot command. Recovery is different: its first
  7,176,192 bytes exactly match `work/control-reason/control-reason.img`.
  The earlier core-revival handoff explicitly records that this test image
  was left in p9 and that booting recovery would hang. The UPG trigger selects
  that slot; the picker installation never reached its script.
- The live system image is not byte-identical to the 10-10 backup; do not claim
  the entire system partition is unchanged. The Sony bootswitcher itself is
  unchanged and the picker backup filename is absent.

## Operator recovery and readback

The operator ran the bounded recovery-only `wo` command at `0x3180000`, length
`0x1000000`, using `work/device/backup-20261010/recovery.img`. The source hash
was checked before writing. The operator then read the entire slot back and
compared it, ending with `RECOVERY_READBACK_OK` and exit code 0.

This session independently ran:

```text
cmp work/device/backup-20261010/recovery.img work/rescue-20261021/recovery-restored.img
RECOVERY_CMP_EXIT=0
sha256sum work/rescue-20261021/recovery-restored.img
1093dbee3723ad6f145d3ffa28b76679b5ac8cd957bf891c070eafdc3690c7e2
```

No boot/system/NVP write was performed by this session.

## Pending UPG checked without mounting storage

Read the FAT32 boot sector at `0x92f80000` (512-byte sectors, 64 sectors per
cluster, 58 reserved sectors, two FATs of 3139 sectors, root cluster 2).
The root at `0x93298000` contains `NW_WM_FW.UPG`, size 12,368, cluster 26056.
It fits in one cluster. Reading its bytes at `0xc60c8000` gives an exact match
to `picker-upg/NW_WM_FW.UPG`, SHA256
`7db2946dee491fd47d51095451abef7a653f240536fa048565d6113ac22a5a44`.

An initial extraction failed because the output directory did not exist:

```text
FileNotFoundError: [Errno 2] No such file or directory: 'work/rescue-20261021/pending-unpacked/file0.bin'
```

After creating that host directory, `python3 -I tools/nwupg.py -w check
work/rescue-20261021/pending.UPG work/rescue-20261021/pending-unpacked`
reported MD5 `OK`, `header sig OK nr_files=2 pad_zero=True`, and both files.
The extracted first script compared exactly to `picker-upg/update.sh`
(`PENDING_SCRIPT_CMP_EXIT=0`).

The operator was asked to power-cycle normally and select Walkman. Actual
updater completion and cold-boot picker operation are still unverified.

## Host verification while waiting for the operator

- `sh -n picker-upg/update.sh picker-remove-upg/update.sh probe/android/android-boot.sh`
  and `bash -n tools/android-boot.sh tools/build_upg.sh picker-upg/build.sh
  picker-remove-upg/build.sh`: exit 0.
- `python3 -m unittest discover -s tools/tests -v`: `Ran 8 tests in 0.288s`,
  `OK`. These test container assembly, not a physical boot.
- `probe/build.sh tools`: exit 0, static device helpers rebuilt.
- `picker-upg/build.sh` and `picker-remove-upg/build.sh`: exit 0; all four
  W1/stock package round trips passed. Combined command ended with
  `PICKER_HOST_GATE_OK`.
- No picker code was changed: the evidence identifies an occupied recovery
  slot, not a demonstrated defect in the picker.

`timeout 180 adb -s <A55 serial> wait-for-device` ended with
`A55_ADB_WAIT_EXIT=124`. At that observation USB still listed the preloader,
and ADB listed only the ZX507. This does not establish a new boot failure:
the operator's normal power-cycle result has not been received yet. No new
write or automatic reset was attempted.

## First real picker boot after recovery

The operator reported a brief update screen, a reboot, the boot animation,
and a normally displayed picker. ADB then returned on the A55, not the ZX507:

- `getprop sys.sony.bootmode`: `1`; `HgrmMediaPlayerApp` and Sony services run.
- Installed hook MD5 `176d5f53d4736e200778abe4ab466a87`; its text exactly
  matches the packaged hook.
- Preserved Sony script MD5 `80b7e3b99901b831d47740bcf4663d6d`; its bytes
  exactly match the original device copy.
- `/bin/nvpflag -x fup`: `0xFFFFFFFF`.
- The install log records `kept Sony's script as bootswitcher.sony.sh`,
  the expected permissions, UPG rename to `.done`, and `PICKER UPG DONE`.

The USB-exported contents volume was initially unmounted on both sides.
Reading the expected PC mount path returned
`cat: /run/media/person/WALKMAN/PICKERUPG.log: No such file or directory`.
After `udisksctl mount -b /dev/sda -o ro`, the log and boot choice (`1`) were
read successfully. No concurrent device-side contents mount was created.

This verifies the recovery repair, updater execution, picker appearance,
and Sony boot path. Android cold boot remained to test.

## Longer automatic selection, requested by the operator

The operator authorized the Android boot test and requested more selection
time. Change only `SECONDS_TO_WAIT=5` to `SECONDS_TO_WAIT=15` in the packaged
hook. The existing first-key cancellation and 60-second inactivity behavior
stay unchanged. This requires the operator to run the installer UPG again;
the installed hook cannot take a new constant from the host without it.

Added isolated shell-flow tests in `tools/tests/test_picker_hook.py`. They
extract the actual packaged hook, replace hardware/absolute paths with
temporary fixtures, and exercise both selections, saved default, diagnostic
bypass, missing payload, missing Sony copy, and picker failure fallback.

The first test run also found a fixture race: the detached Android stub had
not finished writing its log when the assertion read it:

```text
AssertionError: '' != 'ANDROID:boot\n'
```

The fixture now waits with a bounded deadline, without changing production
behavior. Before changing the constant, the corrected test suite reported
`Ran 7 tests`, `FAILED (failures=2)`: both timeout assertions received `:5`
instead of `:15` (`work/rescue-20261021/picker-hook-red.log`).

After the one-line change:

- `python3 -m unittest discover -s tools/tests -v`: `Ran 15 tests in 0.393s`,
  `OK`.
- Shell syntax checks and both `build.sh` commands: exit 0; all four package
  round trips passed, ending with `PICKER_TIMEOUT_GATE_OK`.
- `python3 -m py_compile tools/tests/test_picker_hook.py`: exit 0.
- Updated shell function names have no duplicates; edited region re-read.
- On-device recovery SHA256 is still the verified backup's hash; battery
  reports `100` / `Full`.
- PC-side `md5sum -c MANIFEST`: all 14 boot-set files `OK`.

The new W1 UPG was copied through the USB-exported internal contents volume,
compared byte-for-byte, synced, and unmounted. Its SHA256 is
`33beed17de71bfb5f841cca0428ea5706fb3be218d9e659316180d5d5dae8a74`.
Boot choice is `2` for the authorized automatic Android test. No image or
boot-set payload was replaced. The operator was given the SCSI trigger;
the assistant did not run it.

`graphify update .` completed with `GRAPHIFY_UPDATE_EXIT=0` (5977 nodes), but
reported this existing parser limitation:

```text
warning: 36 file(s) had syntax errors and may be partially extracted: android-ui/audiofx/aosp/hardware/audio_effect.h (first error at line 49, no symbols extracted), android-ui/library/lyrics.h (first error at line 12, no symbols extracted), android-ui/playback/queue.h (first error at line 7, no symbols extracted), android-ui/src/models/settingsmodels.h (first error at line 12, no symbols extracted), android-ui/src/models/soundmodels.h (first error at line 11, no symbols extracted) (+31 more)
```

These unrelated source files were not modified. Graph extraction is not a
compiler or runtime gate.

## Fifteen-second picker and Android cold boot verified

The operator ran the new UPG trigger. Its first SCSI variant reported:

```text
Current sense: illegal request
An error occured during request
Trying alternative firmware upgrade command...
```

The A55 returned on its own ADB transport. At kernel uptime 27.18 seconds:

- `/system/bin/bootswitcher.sh` contains `SECONDS_TO_WAIT=15`.
- `sys.sony.bootmode` is unset, so the Sony service class was not selected.
- `/bin/sh /contents/wmmod-android/boot/android-boot.sh boot` is running.

The container then started without a host `up` command. The boot log records
14 staged files, swap-guard `writes 11/11`, Wi-Fi modules loaded, in-image
CXD3778GF HAL selected, and `start Android init`. At uptime 132.94 seconds:

```text
/tmp/pivotinit --join $(cat /tmp/aroot/init.pid) /system/bin/sh -c 'getprop sys.boot_completed'
1
```

The resumed activity was
`com.cyanogenmod.trebuchet/com.android.launcher3.Launcher`. The operator also
confirmed Android works. This is runtime evidence for the automatic Android
selection, not just a package-build result. The previous boot already verified
the Sony path. Current default selection is Android (`2`).

An attempted audio-service excerpt used an unavailable container command:

```text
/system/bin/sh: head: not found
```

No audio-playback result is claimed. No new cold-boot failure injection or
physical key-cancellation run was performed in this session; isolated hook
tests cover fallback branches, and the unchanged picker binary's earlier
device input runs remain documented in `2026-10-21-boot-picker.md`.

## Slow startup deferred at the operator's request

The operator reports a long delay even before the first boot animation and
explicitly deferred that issue until tomorrow. No further device operation
or startup-code change was performed after that request. Leave the running
Android instance and its default selection alone.

Separate measured observation: the Android boot script waited for the full
60-iteration driver loop after the picker. `getprop
init.svc.load_sony_driver` is empty. The actual device `/init.rc`, saved as
`work/rescue-20261021/device-init.rc`, has neither the `load_sony_driver`
service definition nor the `optproc_for_boot.sh` call used by the stock
`work/ramdisk-check/root/init.rc`. Its bootswitcher call is at line 174.
This explains why that script's readiness condition cannot succeed on this
W1 ramdisk; it does not explain the operator's earlier, pre-animation delay.
Do not conflate those two intervals or implement a speculative fix tonight.

Next session: read the actual W1 init/boot-complete paths, measure power-on
through animation, picker and Android as separate intervals, and obtain the
operator's agreement before another reboot. Preserve the restored original
recovery slot; do not resume the deferred quad-core experiment.
