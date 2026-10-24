# Audio stub HAL fix — full staged plan

Status: Stage C finished 2026-10-13T13:43:12Z. Evidence `work/android/imp-audio-stub-up-20261013T134312Z`. One up, exit 0. New init pid 29013, comm init, starttime 2863629. Boot id stayed `10415f50-9f70-4b81-acb9-5a782bf11c71`. `READS-DONE`, no `STOP.txt`, no `last_kmsg`. `99-verdict.txt`: stub_bind PASS, ui_bind PASS, fresh_boot PASS, watchdog_window PASS (288.713s, WATCHDOG KILLING 0, setParameters 0), ss_stayed PASS (pid 29886 starttime 2864314), window PASS with focus KeyguardScrim and mStatusBarLayer=151000. getprop FAIL is the container shell missing printf, so the key prefixes were not written; the seven values are present in order. hal_load FAIL counts two `dlopen failed` hits that are both libemoji.so; the primary HAL load line is present and the audio-primary failure strings are 0. sf_layers FAIL: firelauncher layer present at alpha 0, no StatusBar layer, KeyguardScrim visible. tap_test NOT_PERFORMED. Outbox `grok-audio-stub-up-20261013T134136Z.txt` sha256 `c436b3649e330be607c7247621bde7b146a72188a4ef6093d264d23e3ade74b0`. RPC `022bac20-7bbc-49c3-b1b0-ae44fd1520b5`, mode queue.

This is the gate text for the helper already edited at `probe/android/android-boot.sh`. Host sha256 `d02c41072d27745f06a85bd2b0b91295e1a5b7e98f1b81d97ccb9889c899adc4`. `bash -n` passed when that digest was recorded. The diff against the deployed helper (stage-1 pull `work/android/imp-audio-ui-down-20261013T114803Z/12-helper-pulled.sh`, sha256 `16009c1fab9573772e5903dd509e3eb22039783fce0ad2edc2ed4118cfd84225`) is only the `container()` audio block: bind `audio.primary.default.so` over the `audio.primary.mt8127.so` path, then `mount -o remount,bind,ro`. ui-sounds, idc, and `down()` are unchanged from that deployed file.

The drm bundle is dropped. Do not start drmserver, keystore, kisd, or nvram_daemon. Do not edit init.rc. Do not remove the bind and expose the real MTK HAL. The real `audio.primary.mt8127.so` stays on the system image underneath the bind. `down()` umounts the path. The image bytes are not written.

No new `.bak`. Do not open, copy, or hash `android-data.img.focused-20261013T094941Z.bak`. Do not hash `fire-system.img`. `up()` loop-mounts the existing `android-data.img` the same way every previous `up` did.

`container()` does `grep /proc/mounts || mount`. The echo `stub audio HAL bound:` runs after that test, so the echo means the block was reached. It does not prove the source changed. A leftover `/dev/null` mount is kept and only remounted. The Stage C proof is mountinfo plus `ls -l`. Stage C does not call `down`. If Stage B left the mt8127 path mounted, Stage C stops before `up`.

`down()` exits 1 only when the ui-sounds bind is still mounted. A leftover mt8127 bind, idc bind, `/tmp/aroot` mount, `/tmp/avol` mount, or surviving container pid is printed (`!! mounts still under`, `!! still alive`) and the function can still exit 0. Stage B records the exit code and does not use it as the verdict.

## Rules for every stage

Serial `<serial>` only. Every adb command is `adb -s <serial>` and is redirected `< /dev/null`. The script never addresses the SM-X716N at `192.168.240.112:5555`. The serial must be in state `device`. Another attached device does not fail that gate.

Never call host `tools/android-boot.sh`. Device commands that run inside the container are:

```
adb -s <serial> shell "sh /tmp/android-boot.sh sh '<cmd>'"
```

`<cmd>` contains no single quote. Host-side helper commands are `adb -s <serial> shell sh /tmp/android-boot.sh up` and the same with `down` or `status`.

Host python is `env -u PYTHONHOME -u PYTHONPATH python3`. adb status 0 is not success when stdout contains `can't open` or `No such file`.

Boot id that must hold: `10415f50-9f70-4b81-acb9-5a782bf11c71`.

Dump-first: `/proc/last_kmsg` is read only when the serial is not in state `device`, or a boot-id read is not exactly that value. The happy path does not read it. After `last_kmsg`, the script stops. It does not push, down, up, logcat, or dumpsys after that.

A failed identity gate stops before the stage's mutation and before later reads. The mutations are: Stage A push, Stage B the one `down`, Stage C the one `up`.

No second `down`. No second `up`. No hand umount. No reboot. No `logcat -c`. No crash buffer. No comma-joined `-b`. No SIGQUIT, no `kill` except the one `down` the helper itself performs. No `input`, `wm`, `am`, `pm`, `settings`, or `setprop`. No `dumpsys media.audio_flinger`. No `dumpsys media.audio_policy`. One `Unable to open log device` is not retried.

Logcat, only in Stage C, and only inside the container:

```
logcat -d -b main -b system -b events -v threadtime
```

Current container identity, required before Stage A and again before Stage B: init pid `16081`, comm `init`, stat field 22 starttime `2200997`. system_server's pid is not a gate. Guard line contains `swap_accounting_guard` and `[permanent]`. `/sys/block/zram0/disksize` is `268435456`. `/proc/swaps` contains `zram0`. `/proc/modules` contains a line starting `mali_r4p0 `.

Dispatch, later and only after a gate, is `dsh-send.py send` mode `queue`, and only when projcache `openTurnStartSeq` is null. The first send is the Stage A script alone.

## Stage A — push the helper, verify the sha, stop

Evidence directory: `work/android/imp-audio-stub-push-<UTC>/`.

This stage does not call `down` or `up`. It does not logcat.

Steps, in order:

1. `adb devices -l`. Stop if `<serial>` is not in state `device`. That branch reads `last_kmsg` and then stops. No push.
2. `cat /proc/sys/kernel/random/boot_id`. The trimmed value must be `10415f50-9f70-4b81-acb9-5a782bf11c71`. Any other value reads `last_kmsg` and stops. No push. A matching value does not read `last_kmsg`.
3. `busybox grep swap_accounting_guard /proc/modules`. The line must contain `swap_accounting_guard` and `[permanent]`.
4. `busybox cat /sys/block/zram0/disksize` must be `268435456`. `busybox cat /proc/swaps` must contain `zram0`.
5. `busybox grep mali /proc/modules` must contain a line matching `^mali_r4p0 `.
6. `busybox cat /tmp/aroot/init.pid` must be `16081`. `busybox cat /proc/16081/comm` must be exactly `init`. `busybox cat /proc/16081/stat`, field 22, must be `2200997`. Stdout containing `can't open` or `No such file` fails the gate even when the adb status file is 0.
7. Host `sha256sum probe/android/android-boot.sh` must equal `d02c41072d27745f06a85bd2b0b91295e1a5b7e98f1b81d97ccb9889c899adc4`. Any other digest stops before the push.
8. `adb push probe/android/android-boot.sh /tmp/android-boot.sh` with a 30s host timeout.
9. `adb shell chmod 755 /tmp/android-boot.sh`.
10. `busybox ls -l /tmp/android-boot.sh` saved.
11. On-device digest, two readings:
    - `adb pull /tmp/android-boot.sh` into the evidence directory, then host `sha256sum` of that pulled file. This is the gate. It must equal `d02c41072d27745f06a85bd2b0b91295e1a5b7e98f1b81d97ccb9889c899adc4`.
    - `busybox sha256sum /tmp/android-boot.sh` on the device. If the first field is 64 hex digits, it must equal the same digest. If the applet prints an error instead of a digest, that error is saved and the pull-back remains the gate.
12. Re-read boot id. A change reads `last_kmsg` and stops. There is still no `down`.

The script then writes `READS-DONE` and `90-stage-a.txt` containing `PUSH_SHA_OK`, and exits. Hand back the pull sha and the boot id.

```bash
#!/bin/bash
# Stage A. Push the helper and verify the on-device sha. No down. No up.
set -u
SERIAL=<serial>
HELPER=~/Projects/nw-a55/probe/android/android-boot.sh
EXPECTED=d02c41072d27745f06a85bd2b0b91295e1a5b7e98f1b81d97ccb9889c899adc4
BOOT_EXPECTED=10415f50-9f70-4b81-acb9-5a782bf11c71
export UTC=$(date -u +%Y%m%dT%H%M%SZ)
export EVID=~/Projects/nw-a55/work/android/imp-audio-stub-push-$UTC
mkdir -p "$EVID"
printf 'argv=audio-stub-push-stage-a\nutc=%s\nserial=%s\nhelper=%s\nexpected_sha256=%s\nboot_expected=%s\ninit_pid_expected=16081\ninit_starttime_expected=2200997\nno_down=1\nno_up=1\n' \
  "$UTC" "$SERIAL" "$HELPER" "$EXPECTED" "$BOOT_EXPECTED" > "$EVID/00-meta.txt"

stop() {
  printf 'STOP at %s before any later device command\n' "$1" > "$EVID/STOP.txt"
  shift
  printf '%s\n' "$@" >> "$EVID/STOP.txt"
  printf 'READS_DONE\n' > "$EVID/READS-DONE.txt"
  printf '%s\n' "$EVID"
  exit 0
}

adb_s() {
  local out="$1"
  shift
  adb -s "$SERIAL" "$@" < /dev/null > "$EVID/$out.out" 2> "$EVID/$out.err"
  echo $? > "$EVID/$out.status"
  return 0
}

need0() {
  if [ "$(cat "$EVID/$1.status")" != "0" ]; then
    stop "$2" "status $(cat "$EVID/$1.status")" "---- stderr ----" "$(cat "$EVID/$1.err")"
  fi
}

bad_open() {
  grep -E -q 'can.t open|No such file|Not a directory' "$EVID/$1.out" "$EVID/$1.err" 2>/dev/null
}

read_boot() {
  local tag="$1"
  local why="$2"
  adb_s "$tag" shell "cat /proc/sys/kernel/random/boot_id"
  if [ "$(cat "$EVID/$tag.status")" != "0" ]; then
    adb_s "${tag}-last_kmsg" shell "/system/xbin/busybox cat /proc/last_kmsg"
    stop "$tag" "$why boot_id read failed" "status $(cat "$EVID/$tag.status")"
  fi
  local b
  b=$(tr -d '\r\n ' < "$EVID/$tag.out")
  printf '%s\n' "$b" > "$EVID/$tag.value"
  if [ "$b" != "$BOOT_EXPECTED" ]; then
    adb_s "${tag}-last_kmsg" shell "/system/xbin/busybox cat /proc/last_kmsg"
    stop "$tag" "$why boot_id=[$b]"
  fi
}

adb -s "$SERIAL" devices -l < /dev/null > "$EVID/01-adb-devices.out" 2> "$EVID/01-adb-devices.err"
echo $? > "$EVID/01-adb-devices.status"
need0 01-adb-devices "gate serial transport"
if ! grep -E '^<serial>[[:space:]]+device([[:space:]]|$)' "$EVID/01-adb-devices.out" >/dev/null; then
  adb_s 02-last_kmsg shell "/system/xbin/busybox cat /proc/last_kmsg"
  stop "gate serial" "<serial> is not in state device" "$(cat "$EVID/01-adb-devices.out")"
fi

read_boot 03-boot_id "before push."

adb_s 04-guard shell "/system/xbin/busybox grep swap_accounting_guard /proc/modules"
need0 04-guard "gate guard"
if ! grep -q 'swap_accounting_guard' "$EVID/04-guard.out" || ! grep -q '\[permanent\]' "$EVID/04-guard.out"; then
  stop "gate guard" "guard is not permanent" "$(cat "$EVID/04-guard.out")"
fi
adb_s 05-zram-disksize shell "/system/xbin/busybox cat /sys/block/zram0/disksize"
adb_s 05-swaps shell "/system/xbin/busybox cat /proc/swaps"
need0 05-zram-disksize "gate zram disksize"
need0 05-swaps "gate swaps"
zdisk=$(tr -d '\r\n ' < "$EVID/05-zram-disksize.out")
if [ "$zdisk" != "268435456" ] || ! grep -q 'zram0' "$EVID/05-swaps.out"; then
  stop "gate zram" "disksize=[$zdisk]" "$(cat "$EVID/05-swaps.out")"
fi
adb_s 06-mali shell "/system/xbin/busybox grep mali /proc/modules"
need0 06-mali "gate mali"
if ! grep -q '^mali_r4p0 ' "$EVID/06-mali.out"; then
  stop "gate mali" "mali_r4p0 is not loaded" "$(cat "$EVID/06-mali.out")"
fi

adb_s 08-init-pid shell "/system/xbin/busybox cat /tmp/aroot/init.pid"
need0 08-init-pid "gate init.pid"
if bad_open 08-init-pid; then
  stop "gate init.pid" "stdout says the pid file cannot be opened" "$(cat "$EVID/08-init-pid.out")"
fi
pid=$(tr -d '\r\n ' < "$EVID/08-init-pid.out")
if [ "$pid" != "16081" ]; then
  stop "gate init.pid" "init_pid=$pid expected 16081"
fi
adb_s 09-init-comm shell "/system/xbin/busybox cat /proc/16081/comm"
adb_s 13-init-stat shell "/system/xbin/busybox cat /proc/16081/stat"
need0 09-init-comm "gate init comm"
need0 13-init-stat "gate init stat"
if bad_open 09-init-comm || [ "$(tr -d '\r\n' < "$EVID/09-init-comm.out")" != "init" ]; then
  stop "gate init comm" "comm=[$(cat "$EVID/09-init-comm.out")]"
fi
export PRIOR_STARTTIME=2200997
env -u PYTHONHOME -u PYTHONPATH python3 - << 'PY'
import os
from pathlib import Path
evid = Path(os.environ["EVID"])
stat = (evid / "13-init-stat.out").read_text(errors="replace").replace("\r", "").strip()
rest = stat[stat.rfind(")") + 2:].split()
start = rest[19] if len(rest) >= 20 else "UNVERIFIED"
(evid / "14-init-starttime.out").write_text(start + "\n")
if start != os.environ["PRIOR_STARTTIME"]:
    (evid / "14-starttime-mismatch.txt").write_text(start + "\n")
PY
if [ $? -ne 0 ]; then
  stop "gate init starttime" "host python failed"
fi
if [ -f "$EVID/14-starttime-mismatch.txt" ]; then
  stop "gate init starttime" "start=$(cat "$EVID/14-init-starttime.out") expected 2200997"
fi

host_sha=$(sha256sum "$HELPER" | awk '{print $1}')
printf '%s\n' "$host_sha" > "$EVID/10-host-helper.sha"
if [ "$host_sha" != "$EXPECTED" ]; then
  stop "host sha" "on-disk helper [$host_sha] != expected [$EXPECTED]. No push."
fi

timeout 30 adb -s "$SERIAL" push "$HELPER" /tmp/android-boot.sh < /dev/null > "$EVID/11-push.out" 2> "$EVID/11-push.err"
echo $? > "$EVID/11-push.status"
need0 11-push "push"
timeout 15 adb -s "$SERIAL" shell chmod 755 /tmp/android-boot.sh < /dev/null > "$EVID/11-chmod.out" 2> "$EVID/11-chmod.err"
echo $? > "$EVID/11-chmod.status"
need0 11-chmod "chmod"
adb_s 11-helper-ls shell "/system/xbin/busybox ls -l /tmp/android-boot.sh"
need0 11-helper-ls "helper ls"

timeout 30 adb -s "$SERIAL" pull /tmp/android-boot.sh "$EVID/12-helper-pulled.sh" < /dev/null > "$EVID/12-pull.out" 2> "$EVID/12-pull.err"
echo $? > "$EVID/12-pull.status"
need0 12-pull "pull"
pulled=$(sha256sum "$EVID/12-helper-pulled.sh" | awk '{print $1}')
printf '%s\n' "$pulled" > "$EVID/12-pulled.sha"
if [ "$pulled" != "$EXPECTED" ]; then
  stop "pull sha" "pulled [$pulled] != expected [$EXPECTED]. No down was run."
fi

adb_s 13-device-sha shell "/system/xbin/busybox sha256sum /tmp/android-boot.sh"
devsha=$(awk '{print $1; exit}' "$EVID/13-device-sha.out" | tr -d '\r')
printf '%s\n' "$devsha" > "$EVID/13-device-sha.value"
if printf '%s' "$devsha" | grep -E -q '^[0-9a-f]{64}$'; then
  if [ "$devsha" != "$EXPECTED" ]; then
    stop "device sha" "busybox sha256sum [$devsha] != expected [$EXPECTED]. No down was run."
  fi
else
  printf 'device sha256sum did not print a digest. pull-back is the gate.\n' > "$EVID/13-device-sha.note"
fi

read_boot 14-boot_id "after push."
printf 'PUSH_SHA_OK\npulled=%s\nboot=%s\n' "$pulled" "$(cat "$EVID/14-boot_id.value")" > "$EVID/90-stage-a.txt"
printf 'READS_DONE\n' > "$EVID/READS-DONE.txt"
printf '%s\n' "$EVID"
```

## Stage B — one down, prove teardown, stop

Evidence directory: `work/android/imp-audio-stub-down-<UTC>/`.

This stage does not call `up`. It does not push. It does not umount by hand. It calls `down` once.

Steps, in order:

1. Same serial, boot-id, guard, zram, mali, and init `16081` / `init` / `2200997` gates as Stage A. A serial miss or a boot-id miss reads `last_kmsg` and stops before `down`.
2. `adb pull /tmp/android-boot.sh` and host `sha256sum`. The digest must be `d02c41072d27745f06a85bd2b0b91295e1a5b7e98f1b81d97ccb9889c899adc4`. A mismatch stops before `down`.
3. One command, host timeout 120s: `adb shell sh /tmp/android-boot.sh down`. If the timeout fires, do not run `down` again.
4. Immediately `cat /proc/sys/kernel/random/boot_id`. A missing serial or a changed boot id reads `last_kmsg` and stops. A matching boot id does not read `last_kmsg`.
5. Teardown reads, all of them, on the matching boot id:
    - `busybox cat /proc/mounts`
    - `busybox grep swap_accounting_guard /proc/modules`
    - `busybox cat /sys/block/zram0/disksize`
    - `busybox cat /proc/swaps`
    - `busybox grep mali /proc/modules`
    - `busybox cat /proc/16081/comm`
    - `busybox cat /tmp/aroot/init.pid` (a leftover file is recorded; it is not a process)
    - `adb shell sh /tmp/android-boot.sh status`

Verdict, written to `90-verdict.txt`. `down`'s exit code is recorded in `60-down.status` and is not a verdict row.

| Proof | PASS |
|---|---|
| container procs | `status` has no pid line between `container processes` and `devices in the container`, and down stdout does not contain `!! still alive` |
| mt8127 bind | `/proc/mounts` has no `audio.primary.mt8127.so` |
| ui bind | `/proc/mounts` has no `media/audio/ui`, and down stdout contains `ui sound bind: gone` and does not contain `!! ui sound bind` |
| idc bind | `/proc/mounts` has no `/system/usr/idc` |
| data bind | `/proc/mounts` has no `/tmp/avol` and no `/tmp/aroot/data` |
| system bind | `/proc/mounts` has no `/tmp/aroot` (that covers `/tmp/aroot/system`, the container self-bind, dev, proc, and sys) and down stdout does not contain `!! mounts still under` |
| boot id | still `10415f50-9f70-4b81-acb9-5a782bf11c71` |
| guard | `swap_accounting_guard` and `[permanent]` |
| zram | disksize `268435456` and `zram0` in `/proc/swaps` |
| Mali | a `/proc/modules` line starts with `mali_r4p0 ` |
| old init | `/proc/16081/comm` is not `init`. `can't open` / `No such file` is the expected text |

`/tmp/idc-overlay` and `/tmp/android-ui-sounds-empty` may remain as directories. They are not mounts. Do not delete them. The script writes `READS-DONE` and stops. No `up`.

```bash
#!/bin/bash
# Stage B. One down, then teardown proofs. No up. No second down.
set -u
SERIAL=<serial>
EXPECTED=d02c41072d27745f06a85bd2b0b91295e1a5b7e98f1b81d97ccb9889c899adc4
BOOT_EXPECTED=10415f50-9f70-4b81-acb9-5a782bf11c71
export UTC=$(date -u +%Y%m%dT%H%M%SZ)
export EVID=~/Projects/nw-a55/work/android/imp-audio-stub-down-$UTC
mkdir -p "$EVID"
printf 'argv=audio-stub-down-stage-b\nutc=%s\nserial=%s\nexpected_sha256=%s\nboot_expected=%s\ninit_pid_expected=16081\ninit_starttime_expected=2200997\nno_up=1\nno_second_down=1\n' \
  "$UTC" "$SERIAL" "$EXPECTED" "$BOOT_EXPECTED" > "$EVID/00-meta.txt"

stop() {
  printf 'STOP at %s before any later device command\n' "$1" > "$EVID/STOP.txt"
  shift
  printf '%s\n' "$@" >> "$EVID/STOP.txt"
  printf 'READS_DONE\n' > "$EVID/READS-DONE.txt"
  printf '%s\n' "$EVID"
  exit 0
}

adb_s() {
  local out="$1"
  shift
  adb -s "$SERIAL" "$@" < /dev/null > "$EVID/$out.out" 2> "$EVID/$out.err"
  echo $? > "$EVID/$out.status"
  return 0
}

need0() {
  if [ "$(cat "$EVID/$1.status")" != "0" ]; then
    stop "$2" "status $(cat "$EVID/$1.status")" "---- stderr ----" "$(cat "$EVID/$1.err")"
  fi
}

bad_open() {
  grep -E -q 'can.t open|No such file|Not a directory' "$EVID/$1.out" "$EVID/$1.err" 2>/dev/null
}

read_boot() {
  local tag="$1"
  local why="$2"
  adb_s "$tag" shell "cat /proc/sys/kernel/random/boot_id"
  if [ "$(cat "$EVID/$tag.status")" != "0" ]; then
    adb_s "${tag}-last_kmsg" shell "/system/xbin/busybox cat /proc/last_kmsg"
    stop "$tag" "$why boot_id read failed"
  fi
  local b
  b=$(tr -d '\r\n ' < "$EVID/$tag.out")
  printf '%s\n' "$b" > "$EVID/$tag.value"
  if [ "$b" != "$BOOT_EXPECTED" ]; then
    adb_s "${tag}-last_kmsg" shell "/system/xbin/busybox cat /proc/last_kmsg"
    stop "$tag" "$why boot_id=[$b]"
  fi
}

adb -s "$SERIAL" devices -l < /dev/null > "$EVID/01-adb-devices.out" 2> "$EVID/01-adb-devices.err"
echo $? > "$EVID/01-adb-devices.status"
need0 01-adb-devices "gate serial transport"
if ! grep -E '^<serial>[[:space:]]+device([[:space:]]|$)' "$EVID/01-adb-devices.out" >/dev/null; then
  adb_s 02-last_kmsg shell "/system/xbin/busybox cat /proc/last_kmsg"
  stop "gate serial" "<serial> is not in state device" "$(cat "$EVID/01-adb-devices.out")"
fi

read_boot 03-boot_id "before down."

adb_s 04-guard shell "/system/xbin/busybox grep swap_accounting_guard /proc/modules"
need0 04-guard "gate guard"
if ! grep -q 'swap_accounting_guard' "$EVID/04-guard.out" || ! grep -q '\[permanent\]' "$EVID/04-guard.out"; then
  stop "gate guard" "guard is not permanent" "$(cat "$EVID/04-guard.out")"
fi
adb_s 05-zram-disksize shell "/system/xbin/busybox cat /sys/block/zram0/disksize"
adb_s 05-swaps shell "/system/xbin/busybox cat /proc/swaps"
need0 05-zram-disksize "gate zram disksize"
need0 05-swaps "gate swaps"
zdisk=$(tr -d '\r\n ' < "$EVID/05-zram-disksize.out")
if [ "$zdisk" != "268435456" ] || ! grep -q 'zram0' "$EVID/05-swaps.out"; then
  stop "gate zram" "disksize=[$zdisk]" "$(cat "$EVID/05-swaps.out")"
fi
adb_s 06-mali shell "/system/xbin/busybox grep mali /proc/modules"
need0 06-mali "gate mali"
if ! grep -q '^mali_r4p0 ' "$EVID/06-mali.out"; then
  stop "gate mali" "mali_r4p0 is not loaded" "$(cat "$EVID/06-mali.out")"
fi

adb_s 08-init-pid shell "/system/xbin/busybox cat /tmp/aroot/init.pid"
need0 08-init-pid "gate init.pid"
if bad_open 08-init-pid; then
  stop "gate init.pid" "$(cat "$EVID/08-init-pid.out")"
fi
pid=$(tr -d '\r\n ' < "$EVID/08-init-pid.out")
if [ "$pid" != "16081" ]; then
  stop "gate init.pid" "init_pid=$pid expected 16081"
fi
adb_s 09-init-comm shell "/system/xbin/busybox cat /proc/16081/comm"
adb_s 13-init-stat shell "/system/xbin/busybox cat /proc/16081/stat"
need0 09-init-comm "gate init comm"
need0 13-init-stat "gate init stat"
if bad_open 09-init-comm || [ "$(tr -d '\r\n' < "$EVID/09-init-comm.out")" != "init" ]; then
  stop "gate init comm" "comm=[$(cat "$EVID/09-init-comm.out")]"
fi
export PRIOR_STARTTIME=2200997
env -u PYTHONHOME -u PYTHONPATH python3 - << 'PY'
import os
from pathlib import Path
evid = Path(os.environ["EVID"])
stat = (evid / "13-init-stat.out").read_text(errors="replace").replace("\r", "").strip()
rest = stat[stat.rfind(")") + 2:].split()
start = rest[19] if len(rest) >= 20 else "UNVERIFIED"
(evid / "14-init-starttime.out").write_text(start + "\n")
if start != os.environ["PRIOR_STARTTIME"]:
    (evid / "14-starttime-mismatch.txt").write_text(start + "\n")
PY
if [ $? -ne 0 ]; then
  stop "gate init starttime" "host python failed"
fi
if [ -f "$EVID/14-starttime-mismatch.txt" ]; then
  stop "gate init starttime" "start=$(cat "$EVID/14-init-starttime.out")"
fi

timeout 30 adb -s "$SERIAL" pull /tmp/android-boot.sh "$EVID/07-helper-pulled.sh" < /dev/null > "$EVID/07-pull.out" 2> "$EVID/07-pull.err"
echo $? > "$EVID/07-pull.status"
need0 07-pull "helper pull"
pulled=$(sha256sum "$EVID/07-helper-pulled.sh" | awk '{print $1}')
printf '%s\n' "$pulled" > "$EVID/07-pulled.sha"
if [ "$pulled" != "$EXPECTED" ]; then
  stop "helper sha" "on-device helper [$pulled] != expected [$EXPECTED]. No down."
fi

timeout 120 adb -s "$SERIAL" shell sh /tmp/android-boot.sh down < /dev/null > "$EVID/60-down.out" 2> "$EVID/60-down.err"
echo $? > "$EVID/60-down.status"

read_boot 61-boot_id "after down."

adb_s 70-mounts shell "/system/xbin/busybox cat /proc/mounts"
adb_s 72-guard shell "/system/xbin/busybox grep swap_accounting_guard /proc/modules"
adb_s 73-zram-disksize shell "/system/xbin/busybox cat /sys/block/zram0/disksize"
adb_s 73-swaps shell "/system/xbin/busybox cat /proc/swaps"
adb_s 74-mali shell "/system/xbin/busybox grep mali /proc/modules"
adb_s 75-oldinit-comm shell "/system/xbin/busybox cat /proc/16081/comm"
adb_s 76-init-pid shell "/system/xbin/busybox cat /tmp/aroot/init.pid"
timeout 30 adb -s "$SERIAL" shell sh /tmp/android-boot.sh status < /dev/null > "$EVID/77-status.out" 2> "$EVID/77-status.err"
echo $? > "$EVID/77-status.status"

env -u PYTHONHOME -u PYTHONPATH python3 - << 'PY'
import os
from pathlib import Path
evid = Path(os.environ["EVID"])

def read(name):
    p = evid / name
    if not p.exists():
        return ""
    return p.read_text(errors="replace").replace("\r", "")

def row(key, ok, detail):
    state = "PASS" if ok else "FAIL"
    return "%s=%s %s" % (key, state, detail)

down = read("60-down.out")
mounts = read("70-mounts.out")
status = read("77-status.out")
guard = read("72-guard.out")
zdisk = read("73-zram-disksize.out").strip()
swaps = read("73-swaps.out")
mali = read("74-mali.out")
comm = read("75-oldinit-comm.out").strip()
boot = read("61-boot_id.value").strip()
down_status = read("60-down.status").strip()

lines = status.splitlines()
procs = []
seen = False
for line in lines:
    if "container processes" in line:
        seen = True
        continue
    if seen and "devices in the container" in line:
        break
    if seen and line.strip():
        procs.append(line.strip())

leftovers = []
for needle in (
    "audio.primary.mt8127.so",
    "media/audio/ui",
    "/system/usr/idc",
    "/tmp/avol",
    "/tmp/aroot",
):
    if needle in mounts:
        leftovers.append(needle)

text = []
text.append("down_exit=%s" % (down_status if down_status else "UNVERIFIED"))
text.append(row("container_procs", seen and not procs and "!! still alive" not in down,
                 "pids=[%s]" % " | ".join(procs)))
text.append(row("mt8127_bind_gone", "audio.primary.mt8127.so" not in mounts, ""))
text.append(row("ui_bind_gone",
                 "media/audio/ui" not in mounts and "ui sound bind: gone" in down and "!! ui sound bind" not in down,
                 ""))
text.append(row("idc_bind_gone", "/system/usr/idc" not in mounts, ""))
text.append(row("data_bind_gone", "/tmp/avol" not in mounts and "/tmp/aroot/data" not in mounts, ""))
text.append(row("system_bind_gone", "/tmp/aroot" not in mounts and "!! mounts still under" not in down, ""))
text.append(row("boot_id", boot == "10415f50-9f70-4b81-acb9-5a782bf11c71", "boot=%s" % boot))
text.append(row("guard", "swap_accounting_guard" in guard and "[permanent]" in guard, guard.strip()))
text.append(row("zram", zdisk == "268435456" and "zram0" in swaps, "disksize=%s" % zdisk))
text.append(row("mali", any(line.startswith("mali_r4p0 ") for line in mali.splitlines()), mali.strip()))
text.append(row("old_init_gone", comm != "init", "comm=[%s]" % comm))
if leftovers:
    text.append("leftover_mount_needles=%s" % ",".join(leftovers))
(evid / "90-verdict.txt").write_text("\n".join(text) + "\n")
PY

printf 'READS_DONE\n' > "$EVID/READS-DONE.txt"
printf '%s\n' "$EVID"
```

## Stage C — one up, then the fresh-boot checks, stop

Evidence directory: `work/android/imp-audio-stub-up-<UTC>/`.

This stage does not call `down`. It does not push. It calls `up` once. `up` itself runs `prepare` (stops the Sony player stack and mounts `/tmp/c`), `zram`, `swap`, `container`, and `start_init`. That is the existing helper, not an added step.

Pre-up gates:

1. Serial and boot id, with `last_kmsg` only on a miss. Then guard, zram disksize `268435456`, `zram0` in swaps, and `mali_r4p0`.
2. Pull `/tmp/android-boot.sh` and require sha256 `d02c41072d27745f06a85bd2b0b91295e1a5b7e98f1b81d97ccb9889c899adc4`.
3. The container is down. `/proc/mounts` contains none of `audio.primary.mt8127.so`, `media/audio/ui`, `/system/usr/idc`, `/tmp/avol`, `/tmp/aroot`. `status` lists no container pid. `/proc/16081/comm` is not `init`. Any miss stops before `up`. This stage does not repair it with `down`.

One `up`, host timeout 240s. That is the timeout the last successful `up` used. `prepare` can wait up to 60s for the Sony player to exit.

If the adb status is non-zero, stdout contains `!!`, or the timeout fires: read boot id. A change reads `last_kmsg` and stops. The same boot id saves `/proc/mounts` and `/tmp/aroot/init.pid` and stops. No logcat, no dumpsys, no second `up`. A host timeout means the device-side `up` may still be running. Do not kill it by hand.

On a clean return:

1. Boot id. A change reads `last_kmsg` and stops.
2. `/tmp/aroot/init.pid`, comm, and stat field 22. Comm must be `init`. The pid must not be `16081` and the starttime must not be `2200997`. Otherwise stop before the wait. Save those files. Do not read `last_kmsg` when the boot id matched.
3. Record whether `up` stdout contains `stub audio HAL bound:` and whether it contains `masked /tmp/aroot/system/lib/hw/audio.primary.mt8127.so`. Continue either way. The echo is not the bind proof.
4. Host `/proc/mounts`, and `/proc/<new-init-pid>/mountinfo`.
5. Container `ls -l` of `/system/lib/hw/audio.primary.mt8127.so` and `/system/lib/hw/audio.primary.default.so`.
6. Container getprop, one command, printed as `key=value` for `ro.product.config.type`, `ro.hardware`, `ro.board.platform`, `ro.product.device`, `ro.product.board`, `ro.build.version.release`, `ro.build.version.sdk`.
7. Host `sleep 90`. No adb during the sleep. Then boot id and the same init pid/comm/starttime. A boot-id change reads `last_kmsg` and stops. Init comm no longer `init`, or the starttime changed, stops before logcat. No `last_kmsg` on a matching boot id.
8. Container `ps`, then `/proc/<pid>/comm` and `/proc/<pid>/stat` for each `system_server`, `zygote`, and `zygote64` pid in that `ps` (stat field 22 is the starttime). Then one logcat of the form above, host timeout 120s.
9. Host `sleep 180`. No adb during the sleep. The same boot-id and init check as step 7.
10. Container `ps`, the same per-pid comm/stat/starttime reads as step 8, and a second logcat, unless the first log contained `Unable to open log device`. That failure is not retried.
11. Container `dumpsys window` and `dumpsys SurfaceFlinger`, each with a host timeout of 60s. A timeout or an empty file makes that row UNVERIFIED. Do not retry. Do not dumpsys audio.
12. Boot id and init pid/comm/starttime once more. A boot-id change reads `last_kmsg` and stops before the extract.
13. Host python writes `99-verdict.txt`. The script then stops.

The 4-minute span is device time in the logs: the later log's last timestamp minus the fresh boot's `boot_progress_system_run` (or `boot_progress_start` if `system_run` is absent). Host sleep is not the measurement. If that span is under 4 minutes, the watchdog row is UNVERIFIED. This stage does not send `SCREEN_OFF`. The extract counts `SCREEN_OFF` lines and reports them. A zero means that broadcast was not in the captured logs.

Death lines are the bracket payload of `am_proc_died`, `am_kill`, and `am_crash`: `[user,pid,name,...]`. A line counts when `name` is `system_server`, or when `pid` equals the new system_server pid from `ps`. A line whose logger pid is system_server does not count. `SystemServer: Init Watchdog` is counted separately and is not a kill.

Grep strings for the HAL row, searched in both log files:

- PASS requires at least one line containing `loadHwModule() Loaded primary audio interface from`
- PASS requires zero lines containing `dlopen failed`
- PASS requires zero lines containing `couldn't load audio hw module audio.primary`
- PASS requires zero lines containing both `loadHwModule() error` and `module primary`
- Every matching line is copied to `71-hal-lines.txt`. A non-empty log with no Loaded line is FAIL. An empty log, a timeout, or `Unable to open log device` is UNVERIFIED.

Fresh-boot marker strings:

- `boot_progress_ams_ready`
- `boot_progress_enable_screen`
- `am_on_resume_called` whose message contains `com.amazon.firelauncher.Launcher`

The logger pid (threadtime field 3) of the two `boot_progress_*` lines must equal the system_server pid from `ps`. A marker logged by pid `16953` or pid `26383` is FAIL for this fresh boot.

Other rows:

| Row | PASS | UNVERIFIED when |
|---|---|---|
| getprop | `ro.product.config.type=AIQC11IVNRMBB`, `ro.hardware=icx`, `ro.board.platform=mt8127`, `ro.product.device=ford`, `ro.product.board=ford`, `ro.build.version.release=5.1.1`, `ro.build.version.sdk=22` | file empty or the container command failed |
| stub bind | in `/proc/<init>/mountinfo`, the mt8127 mountpoint has root containing `audio.primary.default.so`, root not containing `/null`, and option token `ro`; container `ls -l` of the mt8127 path starts with `-` and has a field `5316` | mountinfo or `ls` missing. Quote the raw line |
| ui bind | mountinfo root `/android-ui-sounds-empty` on the `media/audio/ui` mountpoint, option token `ro` | line absent |
| ss stayed up | payload death count 0, and the early and late `ps` system_server pid and starttime are the same pair | `ps` or stat missing |
| SF | the `Visible layers` section contains `firelauncher` and `StatusBar` | dumpsys empty, timed out, or no `Visible layers` line. A real section without those names is FAIL |
| window | an `mCurrentFocus=` line whose value is not `null`, and an `mStatusBarLayer=` line whose value is not `-1` | the string is absent or dumpsys timed out. Quote `mFocusedApp` and `mLockScreenShown` when present |

`tap_test` stays `NOT_PERFORMED`. The physical panel is not claimed from dumpsys.

A content row that fails does not skip the later reads. Identity and boot-id failures are the only stops.

```bash
#!/bin/bash
# Stage C. One up, then the fresh-boot checks. No down. No second up.
set -u
SERIAL=<serial>
EXPECTED=d02c41072d27745f06a85bd2b0b91295e1a5b7e98f1b81d97ccb9889c899adc4
BOOT_EXPECTED=10415f50-9f70-4b81-acb9-5a782bf11c71
export UTC=$(date -u +%Y%m%dT%H%M%SZ)
export EVID=~/Projects/nw-a55/work/android/imp-audio-stub-up-$UTC
mkdir -p "$EVID"
printf 'argv=audio-stub-up-stage-c\nutc=%s\nserial=%s\nexpected_sha256=%s\nboot_expected=%s\nold_init_pid=16081\nold_starttime=2200997\nno_down=1\nno_second_up=1\nno_drmserver=1\n' \
  "$UTC" "$SERIAL" "$EXPECTED" "$BOOT_EXPECTED" > "$EVID/00-meta.txt"

stop() {
  printf 'STOP at %s before any later device command\n' "$1" > "$EVID/STOP.txt"
  shift
  printf '%s\n' "$@" >> "$EVID/STOP.txt"
  printf 'READS_DONE\n' > "$EVID/READS-DONE.txt"
  printf '%s\n' "$EVID"
  exit 0
}

adb_s() {
  local out="$1"
  shift
  adb -s "$SERIAL" "$@" < /dev/null > "$EVID/$out.out" 2> "$EVID/$out.err"
  echo $? > "$EVID/$out.status"
  return 0
}

need0() {
  if [ "$(cat "$EVID/$1.status")" != "0" ]; then
    stop "$2" "status $(cat "$EVID/$1.status")" "---- stderr ----" "$(cat "$EVID/$1.err")"
  fi
}

bad_open() {
  grep -E -q 'can.t open|No such file|Not a directory' "$EVID/$1.out" "$EVID/$1.err" 2>/dev/null
}

read_boot() {
  local tag="$1"
  local why="$2"
  adb_s "$tag" shell "cat /proc/sys/kernel/random/boot_id"
  if [ "$(cat "$EVID/$tag.status")" != "0" ]; then
    adb_s "${tag}-last_kmsg" shell "/system/xbin/busybox cat /proc/last_kmsg"
    stop "$tag" "$why boot_id read failed"
  fi
  local b
  b=$(tr -d '\r\n ' < "$EVID/$tag.out")
  printf '%s\n' "$b" > "$EVID/$tag.value"
  if [ "$b" != "$BOOT_EXPECTED" ]; then
    adb_s "${tag}-last_kmsg" shell "/system/xbin/busybox cat /proc/last_kmsg"
    stop "$tag" "$why boot_id=[$b]"
  fi
}

in_ct() {
  local out="$1"
  local secs="$2"
  local cmd="$3"
  timeout "$secs" adb -s "$SERIAL" shell "sh /tmp/android-boot.sh sh '$cmd'" < /dev/null > "$EVID/$out.out" 2> "$EVID/$out.err"
  echo $? > "$EVID/$out.status"
  return 0
}

log_failed() {
  grep -q 'Unable to open log device' "$EVID/$1.out" "$EVID/$1.err" 2>/dev/null
}

starttime_of() {
  local src="$1"
  local dst="$2"
  SRC_NAME="$src" DST_NAME="$dst" env -u PYTHONHOME -u PYTHONPATH python3 - << 'PY'
import os
from pathlib import Path
evid = Path(os.environ["EVID"])
raw = (evid / os.environ["SRC_NAME"]).read_text(errors="replace").replace("\r", "").strip()
rest = raw[raw.rfind(")") + 2:].split()
start = rest[19] if len(rest) >= 20 else "UNVERIFIED"
(evid / os.environ["DST_NAME"]).write_text(start + "\n")
PY
}

capture_ps_stat() {
  local psfile="$1"
  local prefix="$2"
  PS_FILE="$psfile" PREFIX="$prefix" env -u PYTHONHOME -u PYTHONPATH python3 - << 'PY'
import os
from pathlib import Path
evid = Path(os.environ["EVID"])
text = (evid / os.environ["PS_FILE"]).read_bytes().replace(b"\r", b"").decode("utf-8", "replace")
found = {}
for line in text.splitlines():
    parts = line.split()
    if len(parts) < 2:
        continue
    name, pid = parts[-1], parts[1]
    if name in ("system_server", "zygote", "zygote64") and pid.isdigit():
        found.setdefault(name, pid)
pref = os.environ["PREFIX"]
for name, pid in found.items():
    (evid / ("%s-%s.pid" % (pref, name))).write_text(pid + "\n")
PY
  local role pid
  for role in system_server zygote zygote64; do
    if [ -f "$EVID/${prefix}-${role}.pid" ]; then
      pid=$(tr -d '\r\n ' < "$EVID/${prefix}-${role}.pid")
      case "$pid" in
        ''|*[!0-9]*) ;;
        *)
          adb_s "${prefix}-${role}-comm" shell "/system/xbin/busybox cat /proc/$pid/comm"
          adb_s "${prefix}-${role}-stat" shell "/system/xbin/busybox cat /proc/$pid/stat"
          starttime_of "${prefix}-${role}-stat.out" "${prefix}-${role}-starttime.out"
          ;;
      esac
    fi
  done
}

adb -s "$SERIAL" devices -l < /dev/null > "$EVID/01-adb-devices.out" 2> "$EVID/01-adb-devices.err"
echo $? > "$EVID/01-adb-devices.status"
need0 01-adb-devices "gate serial transport"
if ! grep -E '^<serial>[[:space:]]+device([[:space:]]|$)' "$EVID/01-adb-devices.out" >/dev/null; then
  adb_s 02-last_kmsg shell "/system/xbin/busybox cat /proc/last_kmsg"
  stop "gate serial" "<serial> is not in state device" "$(cat "$EVID/01-adb-devices.out")"
fi

read_boot 03-boot_id "before up."

adb_s 04-guard shell "/system/xbin/busybox grep swap_accounting_guard /proc/modules"
need0 04-guard "gate guard"
if ! grep -q 'swap_accounting_guard' "$EVID/04-guard.out" || ! grep -q '\[permanent\]' "$EVID/04-guard.out"; then
  stop "gate guard" "guard is not permanent" "$(cat "$EVID/04-guard.out")"
fi
adb_s 05-zram-disksize shell "/system/xbin/busybox cat /sys/block/zram0/disksize"
adb_s 05-swaps shell "/system/xbin/busybox cat /proc/swaps"
need0 05-zram-disksize "gate zram disksize"
need0 05-swaps "gate swaps"
zdisk=$(tr -d '\r\n ' < "$EVID/05-zram-disksize.out")
if [ "$zdisk" != "268435456" ] || ! grep -q 'zram0' "$EVID/05-swaps.out"; then
  stop "gate zram" "disksize=[$zdisk]" "$(cat "$EVID/05-swaps.out")"
fi
adb_s 06-mali shell "/system/xbin/busybox grep mali /proc/modules"
need0 06-mali "gate mali"
if ! grep -q '^mali_r4p0 ' "$EVID/06-mali.out"; then
  stop "gate mali" "mali_r4p0 is not loaded" "$(cat "$EVID/06-mali.out")"
fi

timeout 30 adb -s "$SERIAL" pull /tmp/android-boot.sh "$EVID/07-helper-pulled.sh" < /dev/null > "$EVID/07-pull.out" 2> "$EVID/07-pull.err"
echo $? > "$EVID/07-pull.status"
need0 07-pull "helper pull"
pulled=$(sha256sum "$EVID/07-helper-pulled.sh" | awk '{print $1}')
printf '%s\n' "$pulled" > "$EVID/07-pulled.sha"
if [ "$pulled" != "$EXPECTED" ]; then
  stop "helper sha" "on-device helper [$pulled] != expected [$EXPECTED]. No up."
fi

adb_s 08-old-comm shell "/system/xbin/busybox cat /proc/16081/comm"
if [ "$(cat "$EVID/08-old-comm.status")" = "0" ] && [ "$(tr -d '\r\n' < "$EVID/08-old-comm.out")" = "init" ]; then
  stop "container still up" "pid 16081 is still init. No up and no down."
fi
adb_s 09-pre-mounts shell "/system/xbin/busybox cat /proc/mounts"
need0 09-pre-mounts "pre mounts"
if grep -E -q 'audio\.primary\.mt8127\.so|media/audio/ui|/system/usr/idc|/tmp/avol|/tmp/aroot' "$EVID/09-pre-mounts.out"; then
  stop "mounts remain" "Stage B leftovers are still mounted. No up and no down." "$(grep -E 'audio\.primary\.mt8127\.so|media/audio/ui|/system/usr/idc|/tmp/avol|/tmp/aroot' "$EVID/09-pre-mounts.out")"
fi
timeout 30 adb -s "$SERIAL" shell sh /tmp/android-boot.sh status < /dev/null > "$EVID/09-pre-status.out" 2> "$EVID/09-pre-status.err"
echo $? > "$EVID/09-pre-status.status"
if grep -E -q '^[0-9]+' "$EVID/09-pre-status.out"; then
  stop "container pid remains" "status still lists a pid. No up and no down." "$(cat "$EVID/09-pre-status.out")"
fi

timeout 240 adb -s "$SERIAL" shell sh /tmp/android-boot.sh up < /dev/null > "$EVID/60-up.out" 2> "$EVID/60-up.err"
echo $? > "$EVID/60-up.status"
up_bad=0
if [ "$(cat "$EVID/60-up.status")" != "0" ]; then
  up_bad=1
fi
if grep -q '^!!' "$EVID/60-up.out" "$EVID/60-up.err"; then
  up_bad=1
fi
if [ "$up_bad" = "1" ]; then
  read_boot 61-boot_id "up failed."
  adb_s 61-mounts shell "/system/xbin/busybox cat /proc/mounts"
  adb_s 61-init-pid shell "/system/xbin/busybox cat /tmp/aroot/init.pid"
  stop "up failed" "status $(cat "$EVID/60-up.status")" "no retry and no last_kmsg on the matching boot id"
fi

read_boot 62-boot_id "after up."
adb_s 62-init-pid shell "/system/xbin/busybox cat /tmp/aroot/init.pid"
if [ "$(cat "$EVID/62-init-pid.status")" != "0" ] || bad_open 62-init-pid; then
  stop "init pid" "init.pid missing after up" "$(cat "$EVID/62-init-pid.out")"
fi
NEW_PID=$(tr -d '\r\n ' < "$EVID/62-init-pid.out")
case "$NEW_PID" in
  ''|*[!0-9]*) stop "init pid" "init.pid=[$NEW_PID]" ;;
esac
if [ "$NEW_PID" = "16081" ]; then
  stop "init pid" "init is still 16081"
fi
adb_s 62-init-comm shell "/system/xbin/busybox cat /proc/$NEW_PID/comm"
adb_s 62-init-stat shell "/system/xbin/busybox cat /proc/$NEW_PID/stat"
if [ "$(cat "$EVID/62-init-comm.status")" != "0" ] || bad_open 62-init-comm || [ "$(tr -d '\r\n' < "$EVID/62-init-comm.out")" != "init" ]; then
  stop "init comm" "comm=[$(cat "$EVID/62-init-comm.out")]"
fi
starttime_of 62-init-stat.out 62-init-starttime.out
NEW_START=$(tr -d '\r\n ' < "$EVID/62-init-starttime.out")
if [ "$NEW_START" = "2200997" ] || [ "$NEW_START" = "UNVERIFIED" ]; then
  stop "init starttime" "start=$NEW_START"
fi
printf '%s\n' "$NEW_PID" > "$EVID/62-new-init-pid.txt"
printf '%s\n' "$NEW_START" > "$EVID/62-new-init-starttime.txt"

adb_s 64-mounts shell "/system/xbin/busybox cat /proc/mounts"
adb_s 65-mountinfo shell "/system/xbin/busybox cat /proc/$NEW_PID/mountinfo"
in_ct 73-hal-ls 20 'ls -l /system/lib/hw/audio.primary.mt8127.so /system/lib/hw/audio.primary.default.so'
in_ct 75-prop 20 'printf ro.product.config.type=; getprop ro.product.config.type; printf ro.hardware=; getprop ro.hardware; printf ro.board.platform=; getprop ro.board.platform; printf ro.product.device=; getprop ro.product.device; printf ro.product.board=; getprop ro.product.board; printf ro.build.version.release=; getprop ro.build.version.release; printf ro.build.version.sdk=; getprop ro.build.version.sdk'

sleep 90
read_boot 16-boot_id "after sleep 90."
adb_s 16-init-comm shell "/system/xbin/busybox cat /proc/$NEW_PID/comm"
adb_s 16-init-stat shell "/system/xbin/busybox cat /proc/$NEW_PID/stat"
if bad_open 16-init-comm || [ "$(tr -d '\r\n' < "$EVID/16-init-comm.out")" != "init" ]; then
  stop "init during first wait" "comm=[$(cat "$EVID/16-init-comm.out")]"
fi
starttime_of 16-init-stat.out 16-init-starttime.out
if [ "$(tr -d '\r\n ' < "$EVID/16-init-starttime.out")" != "$NEW_START" ]; then
  stop "init starttime during first wait" "start=$(cat "$EVID/16-init-starttime.out")"
fi
adb_s 16-uptime shell "/system/xbin/busybox cat /proc/uptime"
adb_s 16-date shell date
in_ct 20-ps-t0 20 'ps'
capture_ps_stat 20-ps-t0.out t0
in_ct 40-logcat-t0 120 'logcat -d -b main -b system -b events -v threadtime'
if log_failed 40-logcat-t0; then
  printf 'Unable to open log device at t0. No logcat retry.\n' > "$EVID/40-logcat-open-failed.txt"
fi

sleep 180
read_boot 17-boot_id "after sleep 180."
adb_s 17-init-comm shell "/system/xbin/busybox cat /proc/$NEW_PID/comm"
adb_s 17-init-stat shell "/system/xbin/busybox cat /proc/$NEW_PID/stat"
if bad_open 17-init-comm || [ "$(tr -d '\r\n' < "$EVID/17-init-comm.out")" != "init" ]; then
  stop "init during second wait" "comm=[$(cat "$EVID/17-init-comm.out")]"
fi
starttime_of 17-init-stat.out 17-init-starttime.out
if [ "$(tr -d '\r\n ' < "$EVID/17-init-starttime.out")" != "$NEW_START" ]; then
  stop "init starttime during second wait" "start=$(cat "$EVID/17-init-starttime.out")"
fi
adb_s 17-uptime shell "/system/xbin/busybox cat /proc/uptime"
adb_s 17-date shell date
in_ct 21-ps-t270 20 'ps'
capture_ps_stat 21-ps-t270.out t270
if [ ! -f "$EVID/40-logcat-open-failed.txt" ]; then
  in_ct 41-logcat-t270 120 'logcat -d -b main -b system -b events -v threadtime'
  if log_failed 41-logcat-t270; then
    printf 'Unable to open log device at t270. No logcat retry.\n' > "$EVID/41-logcat-open-failed.txt"
  fi
fi
in_ct 81-window 60 'dumpsys window'
in_ct 82-sf 60 'dumpsys SurfaceFlinger'

read_boot 90-boot_id "before extract."
adb_s 90-init-comm shell "/system/xbin/busybox cat /proc/$NEW_PID/comm"
adb_s 90-init-stat shell "/system/xbin/busybox cat /proc/$NEW_PID/stat"

env -u PYTHONHOME -u PYTHONPATH python3 - << 'PY'
import os, re
from datetime import datetime
from pathlib import Path
evid = Path(os.environ["EVID"])

def read(name):
    p = evid / name
    if not p.exists():
        return ""
    return p.read_bytes().replace(b"\r", b"").decode("utf-8", "replace")

def status_ok(name):
    s = read(name + ".status").strip()
    return s == "0"

def row(key, state, detail):
    return "%s=%s %s" % (key, state, detail)

def ps_pids(text):
    found = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) < 2:
            continue
        name = parts[-1]
        pid = parts[1]
        if name in ("system_server", "zygote", "zygote64") and pid.isdigit():
            found.setdefault(name, pid)
    return found

def starttime(text):
    raw = text.strip()
    if not raw or "can't open" in raw or "No such file" in raw:
        return "UNVERIFIED"
    rest = raw[raw.rfind(")") + 2:].split()
    return rest[19] if len(rest) >= 20 else "UNVERIFIED"

def parse_ts(s):
    return datetime.strptime(s, "%m-%d %H:%M:%S.%f")

def thread_lines(text):
    out = []
    rx = re.compile(r"^(\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}) (\d+) (\d+) ([A-Z]) ([^:]*): (.*)$")
    for line in text.splitlines():
        m = rx.match(line)
        if m:
            out.append(m)
    return out

rows = []
up = read("60-up.out")
rows.append(row("up_echo",
                "NOTE",
                "stub_echo=%s masked_echo=%s" % (
                    "yes" if "stub audio HAL bound:" in up else "no",
                    "yes" if "masked /tmp/aroot/system/lib/hw/audio.primary.mt8127.so" in up else "no")))

prop = read("75-prop.out")
expect = {
    "ro.product.config.type": "AIQC11IVNRMBB",
    "ro.hardware": "icx",
    "ro.board.platform": "mt8127",
    "ro.product.device": "ford",
    "ro.product.board": "ford",
    "ro.build.version.release": "5.1.1",
    "ro.build.version.sdk": "22",
}
got = {}
for line in prop.splitlines():
    if "=" in line:
        k, v = line.split("=", 1)
        got[k] = v.strip()
if not prop.strip() or not status_ok("75-prop"):
    rows.append(row("getprop", "UNVERIFIED", "empty or command failed"))
else:
    bad = ["%s=%s" % (k, got.get(k, "MISSING")) for k, v in expect.items() if got.get(k) != v]
    rows.append(row("getprop", "PASS" if not bad else "FAIL", " ".join(bad)))

def mount_fields(line):
    if " - " not in line:
        return None
    left = line.split(" - ", 1)[0].split()
    if len(left) < 6:
        return None
    return {"root": left[3], "mountpoint": left[4], "options": left[5].split(","), "raw": line}

info = read("65-mountinfo.out")
parsed = [mount_fields(line) for line in info.splitlines()]
parsed = [p for p in parsed if p]
hal = [p for p in parsed if "audio.primary.mt8127.so" in p["mountpoint"]]
ui = [p for p in parsed if p["mountpoint"].endswith("media/audio/ui") or p["mountpoint"].endswith("/system/media/audio/ui")]
ls = read("73-hal-ls.out")
mt_lines = [line for line in ls.splitlines() if "audio.primary.mt8127.so" in line]
if not info.strip() or not status_ok("65-mountinfo") or not ls.strip() or not status_ok("73-hal-ls"):
    rows.append(row("stub_bind", "UNVERIFIED", "mountinfo or ls missing"))
elif not hal or not mt_lines:
    rows.append(row("stub_bind", "FAIL", "hal_mounts=%d ls_lines=%d" % (len(hal), len(mt_lines))))
else:
    h = hal[0]
    ls_ok = False
    for line in mt_lines:
        fields = line.split()
        if line.startswith("-") and "5316" in fields:
            ls_ok = True
    ok = ("audio.primary.default.so" in h["root"] and "/null" not in h["root"]
          and "ro" in h["options"] and ls_ok)
    rows.append(row("stub_bind", "PASS" if ok else "FAIL", h["raw"]))
    (evid / "65-hal-mount.txt").write_text(h["raw"] + "\n")
if not info.strip() or not status_ok("65-mountinfo"):
    rows.append(row("ui_bind", "UNVERIFIED", "mountinfo missing"))
elif not ui:
    rows.append(row("ui_bind", "FAIL", "no ui mountpoint"))
else:
    u = ui[0]
    ok = u["root"] == "/android-ui-sounds-empty" and "ro" in u["options"]
    rows.append(row("ui_bind", "PASS" if ok else "FAIL", u["raw"]))

logs = []
for name in ("40-logcat-t0.out", "41-logcat-t270.out"):
    if (evid / name).exists() and status_ok(name.replace(".out", "")):
        logs.append((name, read(name)))
open_fail = (evid / "40-logcat-open-failed.txt").exists() or (evid / "41-logcat-open-failed.txt").exists()
hal_hits = []
fail_hits = []
for name, text in logs:
    for i, line in enumerate(text.splitlines(), 1):
        if ("loadHwModule() Loaded primary audio interface from" in line
                or "dlopen failed" in line
                or "couldn't load audio hw module audio.primary" in line
                or ("loadHwModule() error" in line and "module primary" in line)
                or "load: module=/system/lib/hw/audio.primary.mt8127.so" in line):
            hal_hits.append("%s:%d:%s" % (name, i, line))
        if ("dlopen failed" in line
                or "couldn't load audio hw module audio.primary" in line
                or ("loadHwModule() error" in line and "module primary" in line)):
            fail_hits.append(line)
loaded = [h for h in hal_hits if "loadHwModule() Loaded primary audio interface from" in h]
if open_fail or not logs:
    rows.append(row("hal_load", "UNVERIFIED", "log missing or log device failed"))
elif loaded and not fail_hits:
    rows.append(row("hal_load", "PASS", "loaded_lines=%d" % len(loaded)))
else:
    rows.append(row("hal_load", "FAIL", "loaded_lines=%d fail_lines=%d" % (len(loaded), len(fail_hits))))
(evid / "71-hal-lines.txt").write_text("\n".join(hal_hits) + ("\n" if hal_hits else ""))

early = ps_pids(read("20-ps-t0.out"))
late = ps_pids(read("21-ps-t270.out"))
ss = early.get("system_server", "")
rows.append("ps_t0=%s" % early)
rows.append("ps_t270=%s" % late)

marker_lines = []
boot_pid = {"boot_progress_ams_ready": set(), "boot_progress_enable_screen": set()}
resume = []
system_run = None
boot_start = None
watch = []
setparam = []
init_watch = []
screen_off = 0
death = []
stamps_late = []
for name, text in logs:
    for m in thread_lines(text):
        ts, pid, tid, pri, tag, msg = m.group(1), m.group(2), m.group(3), m.group(4), m.group(5), m.group(6)
        if name.startswith("41-"):
            stamps_late.append(ts)
        full = m.group(0)
        if tag == "boot_progress_system_run" and system_run is None:
            system_run = ts
        if tag == "boot_progress_start" and boot_start is None:
            boot_start = ts
        if tag in boot_pid:
            boot_pid[tag].add(pid)
            marker_lines.append(full)
        if tag == "am_on_resume_called" and "com.amazon.firelauncher.Launcher" in msg:
            resume.append(full)
        if "WATCHDOG KILLING" in full:
            watch.append(full)
        if "at android.media.AudioSystem.setParameters" in full:
            setparam.append(full)
        if "Init Watchdog" in full:
            init_watch.append(full)
        em = re.search(r"(am_proc_died|am_kill|am_crash)\s*:\s*\[([^\]]*)\]", full)
        if em:
            parts = [p.strip() for p in em.group(2).split(",")]
            payload_pid = parts[1] if len(parts) > 1 else ""
            payload_name = parts[2] if len(parts) > 2 else ""
            if payload_name == "system_server" or (ss and payload_pid == ss):
                death.append(full)
watch = [line for _, text in logs for line in text.splitlines() if "WATCHDOG KILLING" in line]
setparam = [line for _, text in logs for line in text.splitlines() if "at android.media.AudioSystem.setParameters" in line]
init_watch = [line for _, text in logs for line in text.splitlines() if "Init Watchdog" in line]
screen_off = sum(1 for _, text in logs for line in text.splitlines() if "SCREEN_OFF" in line)
(evid / "72-markers.txt").write_text("\n".join(marker_lines + resume + watch + setparam + init_watch + death) + "\n")

def fresh_ok(tag):
    pids = boot_pid[tag]
    if not pids:
        return False
    if pids & {"16953", "26383"}:
        return False
    return bool(ss) and pids == {ss}

if not logs:
    rows.append(row("fresh_boot", "UNVERIFIED", "log missing"))
else:
    ok = fresh_ok("boot_progress_ams_ready") and fresh_ok("boot_progress_enable_screen") and bool(resume)
    detail = "ams=%s enable=%s resume=%d ss=%s" % (
        ",".join(sorted(boot_pid["boot_progress_ams_ready"])) or "absent",
        ",".join(sorted(boot_pid["boot_progress_enable_screen"])) or "absent",
        len(resume), ss or "MISSING")
    if not ss and (boot_pid["boot_progress_ams_ready"] or boot_pid["boot_progress_enable_screen"] or resume):
        rows.append(row("fresh_boot", "UNVERIFIED", detail))
    else:
        rows.append(row("fresh_boot", "PASS" if ok else "FAIL", detail))

origin = system_run or boot_start
span_ok = False
span_detail = "origin=%s late_last=%s" % (origin or "absent", stamps_late[-1] if stamps_late else "absent")
if origin and stamps_late:
    try:
        delta = (parse_ts(stamps_late[-1]) - parse_ts(origin)).total_seconds()
        span_ok = delta >= 240
        span_detail += " delta_s=%.3f" % delta
    except ValueError:
        span_detail += " timestamp_parse_failed"
if not logs or not origin or not stamps_late:
    rows.append(row("watchdog_window", "UNVERIFIED", span_detail))
elif not span_ok:
    rows.append(row("watchdog_window", "UNVERIFIED", span_detail + " watchdog_lines=%d setParameters_lines=%d" % (len(watch), len(setparam))))
elif watch or setparam:
    rows.append(row("watchdog_window", "FAIL", span_detail + " watchdog_lines=%d setParameters_lines=%d" % (len(watch), len(setparam))))
else:
    rows.append(row("watchdog_window", "PASS", span_detail + " init_watchdog_lines=%d screen_off_lines=%d" % (len(init_watch), screen_off)))
rows.append("screen_off_lines=%d" % screen_off)
rows.append("init_watchdog_lines=%d" % len(init_watch))

st0 = read("t0-system_server-starttime.out").strip()
st1 = read("t270-system_server-starttime.out").strip()
same_pid = bool(ss) and early.get("system_server") == late.get("system_server")
if not early or not late:
    rows.append(row("ss_stayed", "UNVERIFIED", "ps missing death_lines=%d" % len(death)))
elif death or not same_pid:
    rows.append(row("ss_stayed", "FAIL", "death_lines=%d early=%s late=%s start0=%s start1=%s" % (
        len(death), early.get("system_server"), late.get("system_server"), st0 or "MISSING", st1 or "MISSING")))
elif not st0 or not st1 or st0 == "UNVERIFIED" or st1 == "UNVERIFIED":
    rows.append(row("ss_stayed", "UNVERIFIED", "start0=%s start1=%s" % (st0 or "MISSING", st1 or "MISSING")))
elif st0 != st1:
    rows.append(row("ss_stayed", "FAIL", "start0=%s start1=%s" % (st0, st1)))
else:
    rows.append(row("ss_stayed", "PASS", "ss=%s starttime=%s death_lines=0" % (ss, st0)))

win = read("81-window.out")
if not win.strip() or not status_ok("81-window"):
    rows.append(row("window", "UNVERIFIED", "empty or timeout"))
else:
    focus = [line.strip() for line in win.splitlines() if "mCurrentFocus=" in line]
    bar = [line.strip() for line in win.splitlines() if "mStatusBarLayer=" in line]
    (evid / "83-window-focus.txt").write_text("\n".join(
        [line.strip() for line in win.splitlines()
         if any(s in line for s in ("mCurrentFocus=", "mFocusedApp=", "mStatusBarLayer=", "mLockScreenShown", "mShowingLockscreen="))]
    ) + "\n")
    if not focus or not bar:
        rows.append(row("window", "UNVERIFIED", "focus_lines=%d bar_lines=%d" % (len(focus), len(bar))))
    else:
        def value_of(line, key):
            i = line.find(key)
            return line[i + len(key):].strip()
        focus_ok = all(value_of(line, "mCurrentFocus=") != "null" for line in focus)
        bar_ok = all(value_of(line, "mStatusBarLayer=") != "-1" for line in bar)
        rows.append(row("window", "PASS" if focus_ok and bar_ok else "FAIL", " | ".join(focus + bar)))

sf = read("82-sf.out")
if not sf.strip() or not status_ok("82-sf"):
    rows.append(row("sf_layers", "UNVERIFIED", "empty or timeout"))
else:
    lines = sf.splitlines()
    section = []
    on = False
    for line in lines:
        if "Visible layers" in line:
            on = True
        if on:
            if line.startswith("Displays") or "h/w composer" in line.lower() or "Hardware Composer" in line:
                break
            section.append(line)
            if line.strip() == "" and len(section) > 1:
                break
    (evid / "84-sf-visible.txt").write_text("\n".join(section) + "\n")
    if not any("Visible layers" in line for line in section):
        rows.append(row("sf_layers", "UNVERIFIED", "no Visible layers line"))
    else:
        blob = "\n".join(section)
        ok = "firelauncher" in blob and "StatusBar" in blob
        rows.append(row("sf_layers", "PASS" if ok else "FAIL", "firelauncher=%s StatusBar=%s" % (
            "yes" if "firelauncher" in blob else "no",
            "yes" if "StatusBar" in blob else "no")))

(evid / "99-verdict.txt").write_text("\n".join(rows) + "\n")
PY

printf 'READS_DONE\n' > "$EVID/READS-DONE.txt"
printf '%s\n' "$EVID"
```

## After Stage C

Report every `99-verdict.txt` row with the quoted lines from `71-hal-lines.txt`, `72-markers.txt`, `83-window-focus.txt`, and `84-sf-visible.txt`. Do not promote a PASS from a missing file. Do not dispatch a follow-up from this stage. The operator looks at the physical panel after the report.
