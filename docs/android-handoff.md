# Android 브링업 핸드오프 (2026-10-11, 컨테이너 세션)

다음 세션은 이 문서부터 읽는다. 자세한 근거는 `docs/android.md`에, 그날의 시도와 실패는
`devlog/2026-10-11-wifi-soc-android.md`와 `devlog/2026-10-11-android-container.md`에 있다.

## 지금 되는 것

- **Android init이 컨테이너에서 돈다.** FireOS 5.1.1의 `init`을 자체 마운트 네임스페이스에서
  실행한다. 프로퍼티 서비스, `logd`, ueventd 콜드부트가 모두 살아 있다.
- **servicemanager가 binder 컨텍스트 매니저를 잡는다.** uid 100(Sony system)이어야 한다.
- **SurfaceFlinger가 패널에 합성한다.** `gralloc.mt8127` / `hwcomposer.mt8127`을 쓴다.
- **zygote가 preload를 끝내고 system_server를 포크한다.**
- **프레임워크가 화면에 뜬다.** SurfaceFlinger가 `Boot is finished`를 찍고, 패널 캡처에
  FireOS 첫 부팅 화면이 **똑바로** 찍힌다.
- **패널을 눈 없이 읽는다.** `probe/devtools/fbcap.c`가 MTK 오버레이 엔진의 출력
  (`MTKFB_CAPTURE_FRAMEBUFFER`)을 받아 raw로 떨군다. 호스트에서
  `tools/fbcap2png.py`로 PNG로 바꾼다. `screencap`은 SurfaceFlinger의 논리 합성이고,
  이 SoC는 OVL로 합성하므로 `/dev/fb0` 메모리도 정답이 아니다. fbcap이 정답이다.
- **패널 방향이 맞다.** Fire의 `persist.panel.orientation=270`을 0으로 바꿨다. 270에서
  캡처가 90도 돌아가 있었고 0에서 똑바로 나온다(둘 다 캡처로 확인).
- **vold가 돈다.** `/fstab.icx` 없이만. 이게 없으면 `MountService`가 메인 스레드를
  붙잡아 `sys.boot_completed`가 서지 않는다.
- **기기 상태:** 순정 Walkman OS다. 시험은 모두 비영구적이고, 되돌리는 방법은 재부팅이다.

## 다시 띄우기

```sh
probe/build.sh tools               # pivotinit 등 devtools를 다시 빌드할 때만
tools/android-boot.sh up           # 컨테이너 빌드 + Mali 교체 + init 시작
tools/android-boot.sh sh "getprop ro.board.platform"
tools/android-boot.sh sh "logcat -d -v time -b all > /data/lc.txt"
tools/android-boot.sh status       # 컨테이너 프로세스와 /dev 목록
tools/android-boot.sh down         # 컨테이너만 정지(되돌리려면 재부팅)
```

- 컨테이너 루트는 `work/android/container.tgz`이고, 조립은 `tools/android-container.sh`가 한다.
- 컨테이너 안에서 명령을 돌릴 때는 `pivotinit --join`을 쓴다. 컨테이너는 자체 마운트
  네임스페이스에 있고 루트가 `/`로 옮겨져 있어서, 호스트에서 `chroot`로는 들어갈 수 없다.
- 컨테이너의 init pid는 `$ROOT/init.pid`에 있고, 프로세스는 마운트 네임스페이스로 찾는다.
- 호스트 쪽에서 미리 있어야 하는 것:
  - `work/android/fire7-unpack/system.img` (없으면 `work/android/fire7-dl/`의 OTA에서
    `tools/sdat2img.py`로 만든다)
  - `probe/kmod/mali-r4p0/mali_r4p0.ko` (`make`)
  - `probe/out/pivotinit` (`probe/build.sh tools`)
  - `work/android/android-data.img` (없으면 `tools/android-boot.sh up`이 만든다)

## 지켜야 할 것

- **기기를 다른 세션도 쓴다.** 쓰기 전에 다른 세션이 기기를 쓰고 있는지 확인한다
  (`work/devmod-*.log` 시각). Sony 스택을 내리거나 재부팅하면 그쪽에서 올린 mod도 내려간다.
  사용자에게 먼저 묻는다.
- **재부팅은 사용자가 원할 때 한다.** 2026-10-11에 화면을 확인한 직후 바로 재부팅했다가
  혼란을 줬다.
- **bootimg, system, nvp 쓰기는 하지 않는다.** ROADMAP 규칙상 복구 경로가 생긴 뒤에만 된다.
  컨테이너는 기기 파티션에 쓰지 않는다. ueventd가 만든 raw eMMC 노드는 `devclean.sh`가
  지우고, ueventd를 먼저 멈춰서 다시 생기지 않게 한다.
- **`/dev/trustzone`은 지우면 안 된다.** `libtz_uree.so`가 열고 hwcomposer의 BlitManager가
  uREE 세션을 만든다. 지우면 SurfaceFlinger가 `UREE_CreateSession failed`로 abort하며
  크래시 루프를 돈다. `pmt`는 계속 지운다.
- **init의 `exec`는 동작하지 않는다.** AOSP 5.1의 `do_exec`가 -1을 돌려주는 스텁이다
  (`init/builtins.c:257-260`). 셸 명령은 oneshot 서비스 + `start`로 돌린다.
- **컨테이너 프로세스는 "Sony init의 마운트 ns가 아닌 것"으로 찾는다.** 현재 init의 ns만
  보면 이전 실행이 남긴 프로세스가 살아남아 `/dev/alarm` 같은 전역 장치를 쥐고 부팅을 막는다.
- **keystore는 띄우지 않는다.** MTK TEE keymaster에 닿을 수 없어서 크래시 루프만 돈다.
- **기기 셸의 함정:**
  - adb shell의 umask는 0077이다. uid 100 프로세스가 들어갈 디렉터리는 755로 연다.
  - `/proc/kmsg`를 읽으면 블록된다.
  - `pkill -f`나 `pgrep -f`는 자기 셸도 잡는다. comm으로 고른다.
  - MTK에서는 `reboot`도 `wdt_by_pass_pwk`로 찍힌다.
  - 컨테이너 안에는 busybox가 없다. `ls`, `rm`, `sh`는 Fire 이미지의 것을 쓴다.

## 남은 일

1. **오디오.** system_server가 `media.audio_flinger` / `media.audio_policy`를 기다리며 멈춘다.
   `mediaserver`를 안 띄웠기 때문이다. Fire의 `audio.primary.mt8127.so`를 쓸지, Sony
   CXD3778GF 경로를 살릴지 정해야 한다. 여기서 `sys.boot_completed`가 막혀 있다.
2. RAM. 456 MB에 system_server와 dex2oat가 같이 돈다. zram 192 MB를 켜 두었다.
3. 입력, BT HAL. 터치는 앱이 grab한다.
4. `mtkfb_vsync` ioctl 차이(Fire `_IOW`, Sony `_IO`). 아직 막지 않았다.
5. 프레임워크가 도는 것을 확인한 뒤에 컨테이너를 영구화할지 결정한다(ROADMAP 게이트).

## 그 밖의 진행 상황

- 하단 바 3버튼 mod: 설계만 있다(`docs/navbar-3button.md`).
- 핫리로드: 다른 세션이 만들었다(`docs/phase1-ui.md`, `tools/devmod.sh hot`).
- Wi-Fi: 연결과 ping까지 된다. DNS와 상주화는 남았다(`docs/wifi-test.md`).
