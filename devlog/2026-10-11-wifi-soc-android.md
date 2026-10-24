# 2026-10-11 — 내장 Wi-Fi, SoC 구성, Android 그래픽 브링업

같은 날 다른 세션이 핫리로드를 만들었다(`1920241`…`d5e018a`, `docs/phase1-ui.md` 참고). 이 기록은 그 세션 것이 아니다.

## 내장 Wi-Fi (`docs/wifi-test.md`)
- 외부 WIFITEST 번들은 backport cfg80211/mac80211과 wlan_mt 모듈로 이루어져 있다. 이것을 순정 3.10.26 커널에 RAM으로만 올렸다. 스캔(BSS 12개), WPA2 연결, DHCP, ping이 모두 됐다(−65 dBm, 65 Mbit/s). Wi-Fi가 꺼져 있던 이유는 하드웨어가 아니라 kconfig였다.
- 번들 스크립트는 그대로 돌지 않았다. 기기에 `uname`이 없고, toolbox `id`는 `-u`를 무시하고, `/dev`는 tmpfs다. 그래서 busybox로 바꿨다. `wpa_supplicant -B -f`는 바로 죽는다. `-f`를 빼면 된다(원인은 확인하지 않았다).

## 하단 바 구상 (`docs/navbar-3button.md`)
- Android 3버튼식으로 배치한다: ◁ 뒤로, ○ 라이브러리 홈, 지금 재생 중 칩, ⋮ 메뉴. 플레이어로 돌아가는 버튼은 □ 대신 커버 칩으로 하기로 사용자가 정했다. 구현은 아직 하지 않았다.

## SoC 구성 (`docs/android.md`)
- efuse의 devinfo word 3은 `0x0c2e1000`이다. 이 값은 A7 2코어와 Mali-450 MP1을 뜻한다. 순정 커널도 같다(`NR_CPUS=2`, `MTK_CONFIG_FF_PP`).
- `L2CTLR`을 읽는 모듈(`probe/kmod/coreprobe`)로 확인하니 클러스터는 4코어로 만들어졌다. 나머지 둘은 퓨즈로 막혀 있다.
- cpu2 전원 켜기 시험(`probe/kmod/coreup`, 커밋 안 함)에서는 전원 도메인 ACK는 왔지만 코어가 코드를 실행하지 않았다. 기동 경로가 TEE를 거쳐서 이 방향은 멈췄다. 이어 가지 않는다.
- 보조 코어는 Cortex-M4(`cm4` 파티션, 오디오)다.

## Android 블롭 찾기
- Fire 7 5세대(ford)와 7세대(austin, 마지막 OTA 55.6.5.9) 모두 Mali r4p0을 쓰고, API 401을 보낸다. 기기 커널은 900만 받는다.
  - 판정은 디스어셈블로 했다. GET_API_VERSION ioctl을 부르는 곳의 상수를 읽었다(`movw #401` + `bfi`). 바이트 검색은 우연히 걸리는 값이 많아서 판정에 쓰지 않았다.
- 버전 숫자만 패치하는 방법은 버렸다. r4p0에서 r6p2 사이에 ioctl 구조체가 바뀌었기 때문이다(V2 ctx가 u64).
- 결정: 블롭 대신 커널 쪽을 맞춘다. 아마존 GPL 커널(`chaosmaster/android_kernel_amazon_ford@5b71ce8`)의 r4p0 드라이버를 모듈로 옮긴다.

## mali_r4p0 (`probe/kmod/mali-r4p0`)
- vendor 원본을 먼저 커밋하고(`04fc4b1`, `c73062d`), 그 위에 이식 diff를 올렸다(`d390145`).
- 바꾼 것:
  - MP1 고정
  - MT8590 전원 경로(UNIVPLL, init에서 PLL을 켜지 않음, 커널 전역 gpu-loading 포인터를 건드리지 않음)
  - 장치·드라이버 이름 `mali-utgard-r4p0`, 모듈 이름 `mali_r4p0`, debugfs `mali_r4p0`
  - 내장 드라이버가 이미 export하는 이름 5개를 다시 export하지 않음
  - profiling 끔(trace event 이름이 겹침)
  - unbind된 `mali-utgard.0`의 리소스를 넘겨받음
- 외부 심볼 141개가 모두 기기 커널에서 export돼 있다. 첫 빌드에 적재까지 됐다.

## GPU와 SurfaceFlinger (`tools/android-gpu.sh`)
- `hagodaemons.sh stop`이면 Sony 스택이 재부팅 없이 내려간다. 그러면 binder, mali, fb0, ion을 쥔 프로세스가 0개가 된다.
- `egltest`를 Fire `system.img` chroot에서 돌렸다. 셰이더로 그린 픽셀을 읽으니 `51 102 153 255`로 기대값과 같았다.
- servicemanager는 uid 100으로 띄운다. 바인더가 첫 매니저의 euid(Sony system=100)를 기억하기 때문이다.
  - NDK로 정적 빌드한 runas는 TLS 정렬 때문에 거부당했다. 그래서 정적 glibc로 바꿨다.
  - adb의 umask 0077 때문에 chroot 디렉터리를 755로 열어야 했다.
- chroot에는 프로퍼티 영역이 없다. 그래서 MTK gralloc과 hwcomposer를 `default` 이름으로 연결했다.
- 결과: SurfaceFlinger와 Fire bootanimation이 패널에 출력됐다(사용자 확인). 재부팅 전까지 약 3분 반 동안 안정적으로 돌았다.
- 헤더를 대조하니 fb, ddp, ovl ioctl은 같다. ion은 32비트에서 배치가 맞는다. `mtkfb_vsync`만 ioctl 번호가 다르다.

## 알게 된 것
- MTK에서는 일반 `reboot`도 WDT 리셋이다. 그래서 부팅 사유가 `wdt_by_pass_pwk`(boot_reason=4)로 찍힌다. 이 값만으로 진짜 워치독 리셋과 구분할 수 없다.
- 같은 기기를 다른 세션도 쓴다. devmod 실행 로그(`work/devmod-*.log`)와 Sony 스택 재시작 시각으로 그 사실을 알아냈다. 기기를 쓰기 전에 확인할 것.
- 이 세션에서 안전 분류기가 응답을 여러 번 막았다. 그래서 Android 작업은 다음 세션으로 넘긴다.
