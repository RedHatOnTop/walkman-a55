# HANDOFF 검증 결과 (2026-10-10)

HANDOFF.md를 번들 실물과 대조한 결과. HANDOFF와 다른 점은 여기가 우선한다.

## 디렉터리

| 경로 | 내용 |
|---|---|
| `nwa50-extracted.tar.zst` | 받은 원본 그대로 |
| `nwa50-extracted/` | 위 아카이브를 푼 것 |
| `system-rootfs/` | `upg-raw/file6-system.img`를 `debugfs rdump`로 푼 것 (마운트 없이, 소유권 정보는 없음) |
| `work/ramdisk-check/` | boot.img에서 직접 뽑은 램디스크 |
| `work/upg-verify/` | 백업 UPG를 독립 복호화한 결과 |
| `tools/nwupg.py` | Rockbox `upg.c`를 보고 새로 짠 UPG 검증·생성기 (`check` / `pack`). 번들 UPG를 upgtool과 바이트 단위로 똑같이 재현한다. `upgcheck.py`는 이전 버전 |
| `backup-upg/` | 백업 UPG 작업 사본 (번들은 원본 그대로 둠). §5의 두 가지를 반영해 다시 빌드했다 |
| `rockbox/` | Rockbox sparse clone (`utils/nwztools`, `utils/scsi`, `firmware/target/hosted/sonynwz`). `scsitool` 빌드됨 |

## 1. 트리거는 이미 풀려 있다 (HANDOFF §5 대체)

Rockbox `scsitool`의 `do_fw_upgrade`가 바로 그 트리거다. 벤더 SCSI 명령
`fc 00 04 'd' 'b' 'm' 'n' 00 <flags> 00 00 00`을 보낸다 (flags 0x80으로 먼저 시도하고, 실패하면 0으로 다시 보냄).

이 펌웨어에서 직접 확인한 것:
- 1.02 커널의 f_mass_storage 안에 `do_fw_upgrade`, `VDR_SC_FW_UPGRADE`, `"FW UPGRADE"`, `"dbmn"`,
  `"signature Error"` 문자열이 붙어 있다 (`vmlinux.bin` 0xa339b8 부근).
- Wampy INSTALL.md가 NW-A50용 절차로 문서화해 두었다.
  1. MSC 모드에서 `NW_WM_FW.UPG`를 루트(MUSIC 폴더가 있는 곳)에 복사
  2. `scsitool list_devices` → `sudo scsitool -d -s nw-a50 /dev/sgN do_fw_upgrade`
  3. 재부팅 → 부분 업그레이드 → 다시 재부팅 → 전체 업그레이드 (2단계라고 적혀 있다)
- adb가 열린 뒤에는 `nvpflag fup 0x70555766` + `reboot`으로도 된다.

따라서 exe 리패킹(B-1)이나 UART(C)를 쓸 필요가 없다. 순정 UI에도 경로가 있다:
`libUpdateService.so`에 `/contents/NW_WM_FW.UPG`, microSD `/contents_ext/*.UPG` → `NW_WM_FW.TMP` → rename,
`SetFupFlag`(`/dev/icx_nvp/003`), `Reboot`가 있다. 다만 메뉴에서 이걸 부를 수 있는지는 확인하지 않았다.

## 2. 실측으로 확인한 것

- `file6-system.img`: 838,860,800 B, md5 `1f697e43c6af5565589979f1fb9e4b69`. `file1-md5.txt`와 일치한다.
- `kernel/boot.img` 와 `upg-raw/file2-boot.img`: 바이트 단위로 같다.
- 백업 UPG (`tools/nwupg.py check`): MD5 OK, 헤더 서명이 A50 KAS와 일치, 파일 2개,
  file0은 `update.sh`와 바이트 단위로 같다, file1은 0바이트.
- `fwpchk`: 링크된 crypto 라이브러리가 없다. 문자열은 AES/MD5뿐이고, KAS는 `/dev/icx_nvp/011`에서 읽는다.
  공개키 검증이 없다는 HANDOFF 주장이 맞다.
- kconfig: `MODULE_SIG` 없음, `KEXEC` 없음, `DM_VERITY` 없음, `USB_USBNET` 없음, `TUN=y`, `ICX_RECOVERY_PARTITION` 없음.
- `cxd3778gf_dnc_core.ko`, `radio-si4708icx.ko` 존재. `asound.conf`는 card 0 / device 4.

## 3. HANDOFF와 다른 점

- **`ramdisk/update-initramfs/`는 업데이트 전용 램디스크가 아니다.** boot.img의 일반 부팅 램디스크와
  `diff -rq` 결과가 완전히 같다.
- **`icx_start_update.sh`를 호출하는 곳이 번들 어디에도 없다** (rc 파일, init 바이너리, 커널, system.img 모두 확인).
  게다가 이 스크립트는 `/lib/modules/icx_nvp_emmc.ko`, `/usr/local/bin/fwfb`를 참조하는데,
  일반 레이아웃에는 둘 다 없다. 순정 update.sh도 `/system`을 직접 마운트한다.
  → 실제 업데이터는 번들에 없는 별도 rootfs에서 돈다 (uboot/LK나 다른 파티션일 가능성).
  백업을 돌려 나오는 파티션 이미지에서 찾을 수 있을 것이다. "메인 bootimg initramfs에서 수행"이라는 §1.3/§1.4 서술은 근거가 없다.
- UPG 파일명은 고정값이 아니라 nvp `ufn`에서 읽는다 (공장 기본값은 `NW_WM_FW`).
- `sshd`는 init.rc에서 start 하지만 바이너리는 system에서 빠져 있다.

## 4. adb 단서 (다음 목표)

- `default.prop`: `ro.secure=0`, `ro.debuggable=1`, `persist.sys.usb.config=adb`.
- MSC 모드의 USB function은 `UsbMgrServiceFw::SetAdbEnabled`에 따라 `mass_storage`나 `mass_storage,adb`가 된다.
  adbd 쪽 게이트는 `/dev/android_adb_enable`.
- 진단 모드 `diag_main`의 `ADBC` = "ADB control".
- AdbEnabled가 어디에 저장되는지는 아직 못 찾았다. 이걸 찾으면 UPG의 file0 한 줄로 adb를 켤 수 있다.

## 5. 백업 UPG 리스크 메모

- update.sh는 읽기만 하고 항상 exit 0으로 끝난다. 확인했다.
- file1이 0바이트다. `fwpchk -1`이 빈 엔트리를 추출하지 못하면 업데이터는 fup을 리셋하고 재부팅한다
  (안전한 실패지만 시도 한 번이 날아간다). `md5.txt`에 한 줄이라도 넣고 다시 빌드하는 쪽을 권한다.
- 여유 공간 계산이 `busybox df | tail -1`에 의존한다. busybox df는 장치명이 20자를 넘으면 줄을 바꾸므로,
  그때는 STOP으로 빠져 서베이만 남는다. `/emmc@contents`(14자)로 마운트되면 문제없다. `df -P`로 바꾸면 이 걱정이 없어진다.
- 업데이터가 2단계로 돌면 두 번째 패스는 이름이 바뀐 UPG를 찾지 못하고 정상 부팅으로 빠진다 (안전).

## 출처

- Rockbox `utils/nwztools/scsitools/scsitool.c` (`do_fw_upgrade`), `upgtools/upg.c`
- github.com/unknown321/wampy `INSTALL.md`

## 6. 백업 UPG v2 (2026-10-10, `backup-upg/NW_WM_FW.UPG`)

- md5.txt에 한 줄을 넣었다. 이제 file1이 순정처럼 4096B 엔트리다.
- 여유 공간을 `awk '{print $(NF-2)}'`로 읽는다. df 줄바꿈 여부와 상관없이 Available 값을 잡는다 (호스트에서 두 형식으로 확인).
- 16,464 B, md5 `98c2f8e13625e92e402ebf915e6400ff`. `nwupg.py check`로 서명 OK, file0/file1 원본과 일치.
- 기기 실행은 아직 안 했다.

## 7. 이 기기는 Walkman One이다 (2026-10-10 확인)

- Walkman One 설치기는 기기의 `fwpchk`를 다른 키로 바꿔 넣는다. KAS를 푸는 passkey가 `9cc4419c8bef488e`,
  IV가 `6063ce1efa1d543b`(payload CBC에도 같은 IV)이고, KAS는 `nw-wm1a`의 것(`e8d171a5…`)이다.
  출처는 unknown321/nw-installer의 upgtool 패치와 `installer/Makefile`의 `walkmanOne` 타깃이다.
  → **순정 키로 만든 UPG는 이 기기에서 거부된다.** (fup 리셋 후 정상 부팅이라 안전하지만 아무 일도 안 일어난다.)
- `tools/nwupg.py -w`로 Wampy v1.14.6 `walkmanOne.uninstaller`의 UPG를 바이트 단위로 재현했다. 키 세트와 생성기 모두 검증됐다.
- W1 표식: `/opt2/stock/conf_bk`(오프셋 40960에 실제 모델명 24B). W1은 시스템 프로퍼티상 NW-WM1Z로 보인다.
- Wampy의 UPG도 file1(md5.txt)이 0바이트다. 빈 엔트리는 실제로 문제없다. §5의 걱정은 기우였다.
- nw-installer `install.sh`는 첫 줄에서 `nvpflag fup -1`을 실행하고, 끝에서 fup을 다시 세워 두 번째 UPG를 연쇄 실행한다.
  "설치 중 두 번 재부팅"은 이 연쇄 때문이다.

## 8. adb 스위치

- `vendor/sony/bin/AdbEnabler` / `AdbDisabler`는 `persist.sys.sony.icx.adb`를 `"1"` / `"0"`으로 설정한다.
  두 바이너리는 0x93c의 `movs r0, #1/#0` 하나만 다르다. 이 값을 읽는 곳은 `libDmpConfig.so`의 `pst::dmpconfig::AdbEnablerImpl`이다.
- init의 영구 프로퍼티는 `/data/property/<이름>` 파일로 저장된다. `/data`는 usrdata(ext4)다 (`mount_partition` 문자열).
- → 후보: UPG file0에서 usrdata를 마운트하고 `/data/property/persist.sys.sony.icx.adb`에 `1`을 쓴다.
  system/boot는 건드리지 않는다. W1에서도 이 경로가 유효한지는 백업 결과(W1 `boot_complete.sh`)로 확인할 것.

## 9. 백업 UPG v3

- 첫 줄에서 `nvpflag fup -1`을 실행한다 (크래시나 행에도 부팅 루프가 안 생긴다).
- survey에 `/proc/emmc`를 추가했다.
- 업데이터 램디스크를 `update-rootfs.tar`로 남긴다. 마운트된 경로는 빼고, 64MB를 넘으면 건너뛴다.
  `update-env.txt`에는 `ls -la`를 남긴다.
- `build.sh`가 `NW_WM_FW.UPG`(W1용, **이 기기에 쓸 것**)와 `stock/NW_WM_FW.UPG`를 함께 만든다.
  각자 자기 키로 왕복 검증하고, W1용은 순정 키로 거부되는 것까지 확인했다.

## 10. 트리거는 정상 유저랜드에 의존한다 (Sony GPL 소스로 확인)

- `drivers/usb/gadget/f_mass_storage.c`: `VDR_SC_FW_UPGRADE`(0xFC), 서브커맨드 0x04, LUN 0만 받는다.
  `do_fw_upgrade()`는 `dbmn` 서명을 검사하고 `fsg_fsync_all()` 후 `fsg_conn_to_msc_func_tbl.change_status(USB_FW_UPDATE)`만 호출한다. fup을 직접 쓰지 않는다.
- `icx_usbconn.c`(Sony OSS `icx_usbconn.tar.gz`): 상태를 큐에 넣고 `/dev/usbconn` poll 대기자를 깨운다.
  그 대기자는 `libUsbDeviceConnectionService.so`(hagodaemon)이고, fup을 세우고 재부팅하는 쪽은 `libUpdateService.so`("PC UPDATE!", `SetFupFlag`)다.
- → **SCSI 트리거는 정상 부팅한 유저랜드(hagodaemon)가 살아 있어야만 동작한다.** boot/system을 망가뜨려 유저랜드가 USB 연결 단계까지 못 가면 이 경로로는 복구할 수 없다.
  다른 진입 경로(부팅 시 키 조합, LK의 fup 처리, BROM)는 백업의 uboot/preloader 분석으로 확인해야 한다. ROADMAP의 "복구 경로 판정"이 이것이다.

## 11. 백업 결과 (2026-10-10, `work/device/backup-20261010/`)

실행
- `scsitool do_fw_upgrade`의 첫 형식(flags 0x80)은 ILLEGAL REQUEST(ASC 0x24)로 거부되고, 대체 형식(flags 0)이 받아들여졌다.
- 연결이 끊긴 뒤 다시 붙기까지 15분. 그중 약 12분은 아래 버그 1이다.
- `get_dev_info`는 `SONY NW-WM1Z 3.02`를 돌려준다(W1 위장). nvp: `fpi=NW-WM1Z`, `kas=e8d171a5…`(§7과 같음), `ufn=NW_WM_FW`, `mid=0x21000008`, `shp=6 0`, `fup`은 리셋돼 있다.

하드웨어 (`survey.txt`)
- MT8590, Cortex-A7 2코어, 최대 1.3 GHz. MemTotal 467,512 kB(512 MB DRAM).
- eMMC Samsung(manfid 0x15) `AJTD4R`, 15.6 GB. boot0/boot1 각 4 MB, RPMB 4 MB.
- cmdline: `lcm=1-hx8379c_vdo_cmd`, `androidboot.selinux=disabled`, `androidboot.console=ttyMT1`, `icx_modelid=0x21000008`, `icx_ship=0x6`.

파티션
- 표는 `dumchar_info.txt`에 있다. contents 12.3 GB, option3 1 GB, android(system) 800 MB, option2 128 MB(`/opt2`), var 110 MB, db 100 MB, usrdata 40 MB, recovery 16 MB.
- 이미지 26개가 dumchar 크기와 정확히 같다. PC 사본은 기기가 계산한 md5와 일치한다.
- preloader는 `mmcblk0boot0.img`다(`EMMC_BOOT` 헤더, `PRELOADER` 문자열).
- 빠진 것은 eMMC 앞 5 MB(mbr, ebr2–9)뿐이다. 파티션 표는 dumchar와 `/proc/emmc`로 재구성할 수 있다.

W1이 바꾼 것
- 커널: boot와 recovery 둘 다 순정 1.02 커널과 같다(md5 `583358ff…`). 램디스크는 둘 다 2021-09-21 빌드다. 즉 W1은 boot와 recovery의 램디스크를 바꿨다.
- 업데이트 환경은 recovery 파티션의 램디스크다. 공통 파일 122개 중 `bin/sh` 하나만 다르고, 나머지 차이는 실행 중에 생기는 파일(`update.sh`, `md5.txt.0`, `root/fwup_*`)이다.
- system은 `BBDMP2` 빌드다(`ro.sony.version=3.02`, swid `01.20.E.1.02.00`). A50 번들은 `BBDMP5`이고, 둘 다 2019-07-31 빌드다.
  - Sony 바이너리 21개가 다르다(앱, `libDmpFeature`, `libDmpConfig`, 서비스 라이브러리들).
  - Qt 라이브러리와 `libqeglfs.so`는 같다.
  - W1 데이터는 `/system/etc/.mod/`에 있다. 사운드 튜닝, `conf_a/b/c`(nvp 파티션 크기의 이미지), stockrevert.
- 앱 `HgrmMediaPlayerApp`(BuildID `a468f8c7…`):
  - moc 클래스 409개의 덤프가 A50 앱과 한 글자도 다르지 않다.
  - qrc 파일 1121개 중 11개만 다르다. StatusBar 높이 28→12, FooterArea 56→40, PlayIcon 22x18→15x12 같은 크기 편집이다. 바이트 길이를 맞추려고 주석에 공백을 넣은 흔적이 있어서 바이너리 직접 편집으로 보인다.
  - 기기 추출본은 `work/qrc-device`다.
- probe 플러그인 symcheck를 기기 라이브러리로 다시 돌렸다: 730 undefined, 0 missing.

W1 부팅 스크립트 (`/sbin/boot_complete.sh`, boot 램디스크)
- `/contents/CFW/settings.txt`를 읽는다. 문서에 없는 `ADB=` 키가 있다.
  - `ADB=1`이면 `setprop persist.sys.sony.icx.adb 1`(§8의 스위치), `ADB=2`면 끈다.
  - → adb는 UPG 없이 이 한 줄과 재부팅으로 켤 수 있다.
- `CFW/TESTM` 또는 `CFW/TESTM.txt`가 있으면 `nvpflag tst 0x4D545354`(UART 진단 모드)를 세우고 파일을 지운다.
- 사운드 시그니처를 바꿀 때 nvp 파티션(`mmcblk0p22`)을 `conf_a/b/c`로 덮어쓰고 재부팅한다.
- USB 구성(`init.usbcfg.rc`): `0B8B`=adb, `0B8C`=audio+adb, `0B8D`=mass_storage+adb. 부팅 직후 1초 동안 `0B8B`로 붙었다가 `0BC1`로 다시 붙는 것이 이것이다.

LK (`uboot.img`)
- MTK 표준 모드 문자열이 있다: fastboot, META, FACTORY, DOWNLOAD, misc의 `boot-recovery`.
- 이 기기에서 어떤 조건이나 키로 진입하는지는 아직 분석하지 않았다.

백업 스크립트 버그 (다시 돌리기 전에 고칠 것)
1. MapTo가 `/dev/block/mmcblk0`인 항목(mbr, ebr2–9)을 장치 전체로 읽는다. 각각 2 GB에서 FAT 한계로 실패했다(9 × 약 82초). 오프셋과 크기로 잘라 읽어야 한다.
2. 크기 확인이 busybox `ls -l`의 5번째 칸을 읽는데 그 자리가 날짜다. 그래서 모든 파티션이 `WARN size mismatch`로 찍혔다. 실제 크기는 PC에서 대조해 모두 맞았다.
