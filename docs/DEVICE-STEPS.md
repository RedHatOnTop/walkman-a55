# 기기 작업 순서

## Recovery prerequisite for every UPG

Do not trigger an UPG while p9 (`recovery`) contains a kernel-test image. The
UPG updater boots that slot, not the normal p8 (`bootimg`) slot. A working Sony
home screen therefore does not prove the updater can boot.

On 2026-10-21 the picker installation stopped before its script ran because p9
still contained `control-reason.img` from an earlier experiment. The operator
restored the original 16 MiB recovery backup and read it back byte-for-byte.
See `devlog/2026-10-21-picker-recovery.md` for the evidence and status. Restore
and verify the known-good recovery before any further UPG; partition writes
remain the operator's step.

기기가 충전되고 PC에 연결되면 이 순서로 진행한다. 각 단계는 앞 단계의 결과를 확인한 뒤에 시작한다.
기기는 Walkman One이므로 **각 폴더 최상위의 `NW_WM_FW.UPG`**(W1 키)를 쓴다. `stock/` 안의 것은 순정 펌웨어용이다.

공통 준비
- 노트북에서 완전 방전 상태로 충전하면 전력 부족으로 부팅 루프가 생긴다 (사용자 경험). 충전기로 50% 이상 채운 뒤 연결한다.
- USB 모드는 대용량 저장장치(MSC)로 한다. PC에 `WALKMAN` 드라이브가 보여야 한다.
- 트리거 도구: `~/Projects/nw-a55/rockbox/utils/nwztools/scsitools/scsitool`. `/dev/sgN` 접근에 root가 필요하다.
- 내장 저장공간(`WALKMAN`, 12.3 GB vfat)에 3 GB 이상 비워 둔다. 백업 스크립트는 파티션마다 그 크기 + 256 MB가
  남아 있어야 덤프하고, `android`(system) 하나가 800 MB다. 2026-10-10 연결 때 여유는 163 MB였다.
  모자라면 survey, 업데이터 램디스크, preloader만 남고 파티션 이미지는 전부 `STOP`으로 건너뛴다.
- `WALKMAN/CFW/`는 Walkman One의 설정(`settings.txt`), 부팅 로그, StockRevert 패키지(순정 1.02 복귀용 Windows exe)다.
  `work/device/cfw/`에 복사해 두었다.

## 1. 백업 (읽기 전용) — 2026-10-10 완료

결과는 `work/device/backup-20261010/`, 분석은 `ANALYSIS.md` §11.

1. `backup-upg/NW_WM_FW.UPG`를 WALKMAN 루트에 복사하고 sync, 언마운트한다 (전원은 끄지 않는다).
   파일 관리자가 폴더를 열고 있으면 언마운트가 `target is busy`로 실패한다.
2. `pkexec .../scsitool /dev/sg0 get_dev_info` — 읽기 전용 사전 확인. W1에서는 `NW-WM1Z` / `3.02`로 나온다.
   Claude Code의 `!` 셸은 TTY가 없어 sudo 비밀번호를 못 받으므로 `pkexec`를 쓴다. 트리거는 LUN 0(`sg0`)만 받는다.
3. `pkexec .../scsitool -d /dev/sg0 do_fw_upgrade`. 시리즈(`-s`)는 이 명령과 무관하다.
   첫 형식은 `illegal request`로 거부되고 `Trying alternative firmware upgrade command...` 뒤의 형식이 통한다.
4. 기기가 재부팅하며 덤프를 만든다(15분, 스크립트 버그를 고치면 3분 남짓). 다시 정상 부팅하면 `WALKMAN/WMBACKUP/`을 PC로 통째로 복사한다.
5. UPG는 `NW_WM_FW.UPG.done`으로 이름이 바뀌어 있다. 지워도 된다.

## 2. adb 켜기 (W1 설정 한 줄)

W1 부팅 스크립트가 `CFW/settings.txt`의 문서에 없는 `ADB=` 키를 읽는다 (`ANALYSIS.md` §11).

1. `WALKMAN/CFW/settings.txt` 끝에 `ADB=1` 한 줄을 넣는다 (뒤에 공백 없이). 언마운트 후 기기를 재부팅한다.
2. `WALKMAN/CFW/boot_log.txt` 끝에 `The [ADB] setting was found in the settings file (1).`과 `ADB is turned on.`이 있는지 본다.
3. PC에서 `adb devices` → 기기가 보이면 `adb shell id`로 root인지 확인한다.
4. 끌 때는 `ADB=2`로 바꾸고 재부팅한다 (`0`은 아무것도 하지 않는다).

이 경로가 안 되면 `adb-upg/NW_WM_FW.UPG`(usrdata의 프로퍼티 파일을 쓰는 UPG)를 1단계와 같은 방법으로 쓴다.

## 3·4. probe와 UI 오버레이 (비영구) — 2026-10-10 기기에서 확인

`tools/devmod.sh`가 아래 과정을 한 번에 한다. 파티션에 쓰지 않고, 모든 것이 `/tmp`와 bind mount에 있어 재부팅하면 순정이다.

- `tools/devmod.sh mods/NAME` — 모드를 `work/qrc-device` 기준으로 빌드해 띄운다. 마지막 줄이 오버레이 상태다.
- `tools/devmod.sh probe` — 오버레이 없이 probe만.
- `tools/devmod.sh stock` — bind mount를 걷고 순정 앱으로 재시작한다. 끝에 앱과 플러그인의 순정 md5를 찍는다.
- 그 뒤 `tools/probe.py`로 접속한다 (`adb forward tcp:7777 tcp:7777`는 스크립트가 해 둔다).

기기에서 확인한 결과
- `hello`: `1 files, 1 replaced, 0 added, 0 not served; 38 app roots, 1 moved behind`, 화면 왼쪽 아래 표식 (`work/device/probe/shot-devmod.png`).
- 덤프: `work/device/probe/`의 `allScreenIDList`(148개), `allViewLayouts`(174개, QML 102개), `allEarlyLoadList`, `properties`, `windowProperties`.
- `appFont.fontFamily`는 `SST`다. 폰트를 지정하지 않은 `Text`는 기기에서 아무것도 그리지 않는다 (Qt 기본 `Helvetica`가 없다).
- 저장장치 모드는 probe로 끌 수 있다: MSC 화면 객체에 `call ID enableExportAsMsc false`.

스크립트가 처리하는 기기 쪽 함정 (손으로 할 때도 같다)
1. `adb shell`의 umask가 `0077`이다. push한 파일은 root 전용이 되고, `system`으로 도는 앱이 플러그인을 못 읽어
   `could not find or load the Qt platform plugin "eglfs"`로 죽는다. chmod 644, `/tmp/wmmod`는 `chown system.system`.
2. 이 adbd는 `adb shell` 명령의 종료 코드를 돌려주지 않는다. 성공 여부는 출력으로 판단한다.
3. `hagodaemons.sh restart`는 USB를 다시 붙여 저장장치 모드로 들어가고, 그동안 기기의 `/contents`가 내려가 있다.
   그래서 앱 바이너리 위에 래퍼를 bind mount해 `WMMOD_OVERLAY=/tmp/wmmod/overlay.rcc`를 준다. probe 플래그와 badboots도 그 폴더를 따른다.
4. 앱이 bind된 파일을 매핑하고 있어서, 앱이 도는 중에는 언마운트가 `busy`로 실패하고 플러그인을 덮어쓰면 위험하다.
   `hagodaemons.sh stop` → 교체 → `start` 순서로 한다. stop 뒤 앱이 끝나기까지 15초 넘게 걸린다.
5. 이미 걸린 bind 위에 다시 걸면 래퍼가 자기 자신을 exec해 앱이 뜨지 않고, 20초쯤 뒤 감시가 기기를 재부팅한다(한 번 겪음).
   그래서 스크립트는 매번 bind를 전부 풀고 새로 건 뒤, 시작 전에 세 파일의 md5를 확인한다.
   플러그인을 못 읽어 앱이 죽었을 때는 재부팅 없이 UI 없는 상태로 남았다. 어느 쪽이든 `devmod.sh stock`이나 재부팅으로 순정이 된다.
6. 데몬을 재시작할 때마다 기기의 데이터베이스 재구축이 처음부터 다시 시작된다. 음악을 옮긴 직후에는 재구축이 끝난 뒤에 작업한다.

영구 설치(부팅 때 `/contents/wmmod/overlay.rcc`)는 `docs/phase1-ui.md`의 설계대로이며 아직 하지 않았다.
