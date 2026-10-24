# NW-A55 개조 로드맵

순서는 사용자가 정했다 (2026-10-10): **순정 상태에서 최대한 파악 → UI 교체 → 유저랜드 교체 → 안드로이드 구동.**
기기는 현재 Walkman One 펌웨어다 (ANALYSIS.md §7).

각 단계에는 "넘어가기 전에 갖춰야 할 것"이 있다. 공통 원칙은 하나다.
**복구 경로가 확인되기 전에는 bootimg/system 파티션에 쓰지 않는다.**

## Phase 0 — 파악

정적 분석 (기기 없이)
- [x] 업데이트 체인, 키 (순정 + W1), 트리거 (`scsitool do_fw_upgrade`)
- [x] adb 스위치 = `persist.sys.sony.icx.adb`
- [x] 유저랜드 구조 지도 → `docs/phase0-architecture.md` (서비스 38개, binder context manager, hang 감시 후 재부팅, 미확인 38항목은 §7)
- [ ] Wampy 소스에서 IPC 사용법 정리 (binder 서비스 호출, 상태가 클라이언트별로 유지되는 문제)

기기에서 (백업 UPG v3 실행 후)
- [x] `survey.txt`: RAM, eMMC CID, 실제 파티션 맵, nvp 전체 (DRAM 타입은 sysfs에 없음) → ANALYSIS §11
- [x] 업데이터 램디스크는 recovery 파티션의 것 (ANALYSIS §11). **복구 경로 판정**은 LK 분석 뒤에
- [ ] uboot(LK) 분석: fup 처리, 업데이트 모드 진입 조건, 키 조합 여부, 커널 서명 검사 여부
- [x] preloader(`mmcblk0boot0.img`) 확보. BROM/SP Flash Tool 동작은 미확인
- [x] W1이 바꾼 것: boot/recovery 램디스크, system 전체가 BBDMP2 빌드 (ANALYSIS §11)
- [x] adb 활성화 (W1 `settings.txt`에 `ADB=1`, root 셸). 실행 중인 시스템 관찰은 진행 중 (`ps`, 로그 확인함)

넘어가기 전에 갖출 것: adb root 셸, 복구 경로에 대한 판정, 파티션 전체 백업 PC 보관.

## Phase 1 — UI 교체 (데몬 유지)

- 순정 데몬(재생, DSEE/음장, BT, NC, 전원)은 그대로 두고 `HgrmMediaPlayerApp`만 우리 UI로 바꾼다.
- **1단계는 기생 방식이다** (docs/phase0-architecture.md §6.1 검토 결과). 순정 앱을 살려 둔 채 그 프로세스 안에 들어가서 창만 숨기고 우리 화면을 띄운다.
  진입점은 앱이 로드하는 `vendor/sony/plugins/platforms/libqeglfs.so`이고, Wampy가 같은 방식을 쓴다.
  이렇게 하면 문서가 위험 1~3위로 꼽은 항목을 순정 앱이 계속 맡는다. 앱 freeze는 곧 시스템 재부팅인데 그 수명주기·hang 프로토콜이 그대로 유지되고,
  clang 3.9/libc++ ABI와 `I*Service` vtable을 복원할 필요가 없고, MediaStore `SetConfig`나 트랙 시퀀스 같은 클라이언트 초기화도 앱이 한다.
  같은 프로세스 안이라 QML 엔진과 ViewModel에 직접 접근할 수 있다.
- 2단계(선택)로 앱을 완전히 대체한다. `easel::ApplicationBase` 계약과 앱이 import하는 `pst::services` 189개 심볼이 그 범위다.
- 비영구 시험 방법: adb root에서 `libqeglfs.so`에 bind mount를 걸고 앱을 재시작한다. 파티션에는 쓰지 않는다.
  전역 주입 지점으로는 `/etc/ld.so.preload`(→ `/lib/libc_stubs.so`)도 있지만, 이건 boot/system에 써야 한다.
- 배포는 init 서비스를 추가하는 방식이다. Wampy처럼 boot.img 램디스크를 수정한다. 이건 bootimg 쓰기이므로 Phase 0의 복구 판정이 필요하다.
  대안으로 system 쪽 스크립트 훅만 쓰면 bootimg를 건드리지 않아도 된다. 이 길을 먼저 검토할 것.

- 진행 (2026-10-10, 기기 없이): 순정 화면은 C++ ViewModel과 이름으로만 묶인 순수 View라서, 창을 숨기고 덮는 대신 **화면 QML을 같은 qrc 경로에서 바꿔 끼우는** 쪽이 낫다.
  리소스 오버레이(`/contents/wmmod/overlay.rcc`, `probe/mkspec/overlay.cpp`)와 호스트 시뮬레이터(`tools/uisim.sh`), 모드 빌더(`tools/mkmod.sh`)를 만들었다.
  상세와 검증 범위는 `docs/phase1-ui.md`, 기기 확인 순서는 `docs/DEVICE-STEPS.md` 4단계.
- 진행 (2026-10-11): 앱을 재시작하지 않는 핫 리로드(`tools/devmod.sh hot`)가 기기에서 동작한다. 모드 화면은 껍데기와 `WmHot` 본체로 나눈다(`docs/phase1-ui.md` §7).

넘어가기 전에 갖출 것: 우리 UI로 재생/정지/곡 이동/볼륨/전원 관리가 순정과 같은 수준으로 동작.

## Phase 2 — 유저랜드 교체

- glibc 2.23 armhf 그대로 크로스 컴파일한다 (Sony GPL 커널 3.10.26 + 동봉 모듈 유지).
- 다시 구현해야 하는 것: 오디오 경로(ALSA hw:0,4, CXD3778GF 설정, `dacdat` 테이블, CM4 DSP), 전원/서스펜드, 배터리, 입력, USB 모드, 스토리지 마운트.
  순정 데몬을 잃으면 DSEE/ClearAudio/NC/LDAC가 사라진다. 각각을 블롭째 재사용할 수 있는지가 핵심 질문이다.
- 초기 형태는 system 파티션 교체가 아니라, 기존 system 옆에서 chroot나 별도 init 서비스로 띄우는 것이다. 그래야 되돌리기 쉽다.

## Phase 3 — 안드로이드 구동

- 커널에 binder/ashmem/ION/logger/sw_sync가 이미 있다. Android 5.x(L) 유저랜드가 후보다.
- 걸리는 부분은 GPU다. Mali 드라이버가 Linux(eglfs)용이라 gralloc/HWC 블롭이 필요하다. 블롭이 없으면 소프트웨어 렌더링(SwiftShader 류)으로 간다.
  오디오 HAL은 직접 만들어야 하고, BT는 MTK BlueAngel이라 별도 작업이다.
- 처음에는 chroot 안에서 `servicemanager`/`surfaceflinger`를 띄우는 실험부터 한다. 부팅 교체는 그 다음이다.
- 조사 결과는 `docs/android.md`에 있다. SoC는 퓨즈상 A7 2코어 + Mali-450 MP1이다. 바인더 context manager는 Sony `hagodaemon`(uid 100)이 갖고 있다.
