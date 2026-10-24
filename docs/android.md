# Android 이식 조사 (2026-10-11)

다음 세션에서 이어 갈 때는 `docs/android-handoff.md`부터 읽는다.

ROADMAP의 마지막 단계인 Android를 하기 전에 하드웨어와 커널 조건을 확인했다. 기기에서 직접 읽은 값만 적었다. 추정에는 따로 표시했다.

## SoC 구성

| 블록 | 이 기기 | 근거 |
|---|---|---|
| CPU | Cortex-A7 r0p3, 4코어 클러스터 중 **2코어**만 켜짐, 최대 1.3 GHz | `L2CTLR`, efuse, `[CORE] num:2`, `/sys/devices/system/cpu/possible` = `0-1` |
| GPU | Mali-450 **MP1**(GP 1 + PP 1), 커널 API v900 | efuse, `update_dev_info of MP1`, debugfs `pp/num_cores_total` = 1 |
| 보조 코어 | Cortex-M4(오디오), `cm4` 파티션 | MTK 헤더 이름 `CM4`, payload 22292 B, 벡터 SP `0x20015000`, reset `0x1051` |
| RAM | 512 MB 칩, 커널에 456 MB 보임 | `/proc/meminfo` MemTotal 467512 kB |

- **클러스터는 4코어로 만들어졌고, 2개를 퓨즈로 막았다.** `probe/kmod/coreprobe`가 두 코어에서 `L2CTLR`을 읽었다. 값은 `0x03800001`이고, bits[25:24] = 3이라서 4코어 클러스터다. 이 비트는 합성할 때 정해지므로 퓨즈로 바뀌지 않는다.
  - 커널(`mt-smp.c`)은 원래 이 레지스터로 코어 수를 셌다. 지금은 그 코드를 주석으로 막고 efuse를 읽는다. 그리고 2코어로 판정되면 cpu2와 cpu3의 MTCMOS 전원을 끈다.
  - 막힌 두 코어가 정상인데 등급 때문에 막은 것인지, 불량이라 막은 것인지는 모른다. 퓨즈가 하드웨어에서도 코어를 막는지도 모른다. 알아보려면 `NR_CPUS=4`이고 퓨즈 검사를 건너뛰는 커널이 필요하다. 그러려면 bootimg를 써야 하므로 복구 경로가 생긴 뒤에만 한다.
- 판정은 devinfo word 3으로 한다. 이 기기의 값은 `0x0c2e1000`이다.
  - bits[27:24]가 CPU 코어 수다. 0이면 4코어, 8이면 3코어, 그 밖이면 2코어다. 이 기기는 `0xc`라서 2코어다.
  - bits[18:17]이 GPU PP 수다. 0이면 MP4, 1이면 MP3, 2면 MP2, 3이면 MP1이다. 이 기기는 3이라서 MP1이다.
- 순정 커널 설정도 퓨즈와 같다. `NR_CPUS=2`이고, Mali 드라이버는 `MTK_CONFIG_FF_PP`로 빌드돼 MP1로 고정된다.
- CPU DVFS 단계는 1300 / 1196 / 1040 / 747.5 / 598 MHz다. governor는 `hotplug`다.
- 퓨즈 값은 `probe/devtools/devinfo`로 읽는다(`./build.sh tools`). word 12–13은 칩마다 다른 값으로 보여서 여기에 적지 않는다.

## 커널

- `CONFIG_ANDROID`, binder, ashmem, logger, LMK, ION, sync/sw_sync, SELinux, ZRAM이 이미 켜져 있다. `BLK_DEV_LOOP`, `EXT4_FS`, `FUSE_FS`, `PID_NS`도 켜져 있다. squashfs는 없다.
- `CONFIG_DRM`과 `DEVTMPFS`는 꺼져 있다. 디스플레이는 fbdev(`MTK_FB`)뿐이다.
- Android 5.0 CDD 7.6.1은 32비트 기기에 512 MB를 요구한다. 456 MB는 그보다 적다. low-RAM 설정과 zram으로 띄워 볼 수는 있다(미확인).

## 바인더 context manager

- 3.10 바인더는 시스템에 context manager를 하나만 둔다(`BINDER_SET_CONTEXT_MGR`, 이미 있으면 `EBUSY`).
- 지금 그 자리는 `hagodaemon`(`daemonNum=28`, pid 437)이 갖고 있다. Sony의 `pst::services::binder::ServiceManager`는 자체 프로토콜이다. `android.os.IServiceManager` 문자열이 없어서 Android 서비스가 같이 등록할 수 없다.
- 소유 프로세스가 죽으면 자리가 비워진다(`binder_deferred_release`). 그런데 커널은 첫 매니저의 euid를 부팅 내내 기억한다. 그래서 다음 매니저도 같은 euid여야 한다. Sony `system`은 **uid 100**이다(Android는 1000).
  - 따라서 Android `servicemanager`를 쓰려면 Sony 데몬을 내린 뒤 euid 100으로 띄워야 한다.

## 첫 GPU 시험: Android Mali 블롭이 그린다 (2026-10-11)

FireOS 5.1.1의 Mali 블롭(r4p0, API 401)으로 이 기기 GPU에서 셰이더를 그리고 결과를 읽었다. 순서는 이렇다(`tools/android-gpu.sh`):
1. `hagodaemons.sh stop`으로 Sony 스택을 내린다. 그러면 `/dev/mali`, `fb0`, `ion`을 쥔 프로세스가 없어지고, 바인더 프로세스도 0개가 된다.
2. 내장 r6p2 드라이버를 unbind한다(`/sys/bus/platform/drivers/mali-utgard/unbind`).
3. `probe/kmod/mali-r4p0`의 `mali_r4p0.ko`를 올린다. 이 모듈은 Fire 7 커널의 r4p0 드라이버를 MT8590용으로 옮긴 것이다(MP1 고정, MT8590 전원 경로, 이름 분리, 내장 장치 인계).
4. Fire `system.img`를 내장 저장소(`wmmod-android/fire-system.img`)에서 loop로 read-only 마운트한다. 그 위에 `/dev`, `/proc`, `/sys`를 bind한 chroot에서 `probe/android/egltest`(NDK r27c, API 21)를 실행한다.

결과:
- `EGL 1.4 Android META-EGL`, `GL ARM | Mali-450 MP | OpenGL ES 2.0`
- 셰이더로 그린 픽셀을 읽으면 `51 102 153 255`로 기대값과 같다. `glGetError 0x0`
- 커널: `mali_probe(): Successfully initialized driver for platform device mali-utgard-r4p0`, `1+0 PP cores initialized`

되돌리는 방법은 재부팅이다. 내장 드라이버의 장치를 내렸기 때문이다. Sony UI도 재부팅해야 돌아온다.

## SurfaceFlinger가 화면을 합성한다 (2026-10-11)

`tools/android-gpu.sh sf`(GPU 시험과 같은 chroot)에서 servicemanager, surfaceflinger, bootanimation이 돌았다. `screencap`(480×800)에 Fire 부트 애니메이션이 나왔고, 기기 패널에서도 같은 화면이 보였다(사용자 확인). 사본은 `work/android/sf-out/`에 있다.
- servicemanager는 정적 glibc `runas`(`probe/devtools/runas.c`)로 uid 100에서 띄운다. 바인더 매니저 자리를 잡은 뒤 `service list`에 `SurfaceFlinger: [android.ui.ISurfaceComposer]`가 나온다.
  - NDK r27c로 정적 빌드하면 bionic이 `TLS segment is underaligned`로 실행을 거부한다.
  - adb의 umask 0077 때문에 chroot 디렉터리를 755로 열어야 uid 100이 지나간다.
- chroot에는 프로퍼티 영역이 없다(이 init은 프로퍼티를 환경 변수로 넘긴다). 그래서 `/system/lib/hw`를 tmpfs로 덮고 `gralloc.default.so`와 `hwcomposer.default.so`를 `*.mt8127.so`로 연결한다.
- 커널 로그: MTK hwcomposer의 `HWC_Ovl`이 `MTKFB_POWERON`을 보내고, SF는 `MTKFB_QUEUE_OVERLAY_CONFIG`로 오버레이를 넣는다. fb/ddp/ovl ioctl은 MT8127과 MT8590이 같다. ion은 32비트에서 배치가 맞는다.
  - 다른 곳은 `mtkfb_vsync`뿐이다. Fire는 `_IOW(…, 1, vsync_src)`이고 Sony는 `_IO(…, 1)`이다. 아직 막지 않았다.
- logcat은 비어 있다. Android 5는 `logd` 소켓을 쓰는데, 아직 logd를 띄우지 않았다.

## 계획

1. Mali 블롭: Android 5.x에서 Mali-450을 쓰는 기기의 펌웨어에서 `libGLES_mali.so`와 gralloc을 구해 API 버전을 대조한다.
   - 후보는 Fire 7 5세대(`ford`, MT8127)의 FireOS 5.3.6.4(Android 5.1)다.
   - 아마존 S3 원본은 403(AccessDenied)이다. 그래서 Wayback 사본을 쓴다(2024-09-12 수집, 664,431,377 B, 원본 Last-Modified 2022-03-09).
   - 받는 위치는 `work/android/fire7-dl/`이다. 블록 OTA는 `tools/sdat2img.py`로 푼다.
   - **결과: 맞지 않는다.** 이 이미지는 Android 5.1.1(`LVY48F`, `37.6.2.6`, 커널 3.10.54)이다. Mali는 `r4p0-00rel0`이고 API **401**을 보낸다(`movw r3, #401` + `bfi` → `0x01910191`, ioctl `0xc0048203`).
     - 기기 커널은 `_MALI_API_VERSION 900`이고, 값이 정확히 같을 때만 `compatible = 1`을 준다. 기기의 `libMali_linux.so`(r6p2)는 `_mali_base_arch_open`에서 `0x03840384`를 V2 ioctl `0xc0108203`로 보낸다.
     - 번호만 고쳐도 안 된다. r4p0와 r6p2 사이에 ioctl 구조체가 바뀌었다(V2는 ctx가 u64).
     - 판정법: 블롭에서 GET_API_VERSION ioctl(`_IOWR(0x82, 3, …)`)을 부르는 곳을 찾고, 거기에 넣는 상수를 읽는다. 문자열이나 바이트 검색만으로는 우연히 걸리는 값이 많아서 판정하면 안 된다.
   - Fire 7 7세대(`austin`, MT8127)의 마지막 OTA `55.6.5.9`(2023-08, 아마존 S3에 아직 있음)도 같다. Android 5.1.1이고 `libMali.so`가 ford 것과 바이트까지 같다(r4p0, API 401). FireOS 5의 MT8127 계열에서는 API 900 블롭이 나오지 않는다.
2. 바인더 없이 시험한다. Android linker와 블롭으로 pbuffer EGL을 만들고 `glClear`와 `glReadPixels`를 돌린다. Mali는 여러 프로세스를 받으므로 Sony UI를 그대로 둘 수 있다.
   - 기기의 여유 RAM은 ~40 MB다. 그래서 Android system은 RAM에 올리지 않는다. ext4 이미지 파일을 loop로 read-only 마운트한다.
3. SurfaceFlinger: `hagodaemons.sh stop` 뒤에는 바인더 프로세스가 없다. servicemanager를 euid 100으로 띄우고, gralloc.mt8127과 hwcomposer.mt8127이 MT8590 커널(ion_mtk, 디스플레이 ioctl)에서 도는지 본다. (`hagoromo2` 하나만 내리면 재부팅되지만, 전체 stop은 재부팅 없이 된다.)

## Android init을 컨테이너에서 (2026-10-11, 이어서)

손으로 서비스를 띄우는 방식은 프로퍼티 서비스와 `logd`를 줄 수 없다. zygote와 system_server에는 둘 다 필요하므로, FireOS의 `init`을 컨테이너 안에서 직접 돌린다. Fire `init`은 정적 ARM이고, 커널 cmdline이 `androidboot.selinux=disabled`라서 정책을 올리지 않는다.

구성은 `tools/android-container.sh`가 `work/android/container.tgz`로 조립하고, `probe/android/android-boot.sh`가 기기에서 실행한다. 호스트 쪽 진입점은 `tools/android-boot.sh`다.

### 컨테이너 루트가 네임스페이스 루트여야 하는 이유

Amazon의 zygote는 system_server를 포크하기 전에 파일 디스크립터 테이블을 만들고, 화이트리스트에 없는 경로를 가진 디스크립터를 만나면 죽는다.

```
E/Zygote: Not whitelisted : /tmp/aroot/dev/__properties__
JNI FatalError ... com_android_internal_os_Zygote.cpp:482:
    Unable to construct file descriptor table.
```

두 가지가 동시에 성립해야 한다.

1. **상속된 디스크립터가 없어야 한다.** adbd는 Sony init의 프로퍼티 영역 디스크립터를 물려받아 자식에게 넘긴다. Sony의 `/dev/__properties__`는 unlink돼 있어서 경로가 `(deleted)`로 읽히고, 화이트리스트에 절대 걸릴 수 없다. `pivotinit`이 exec 전에 stderr 위의 모든 fd를 닫는다.
2. **프로퍼티 영역이 `/dev/__properties__`로 읽혀야 한다.** zygote는 init의 마운트 네임스페이스에 있지 않다(ART의 `Runtime::InitZygote`가 네임스페이스를 unshare한다. 앞서 본 `Failed to mount() rootfs as MS_SLAVE`가 그 경로다). 그래서 디스크립터가 얹힌 마운트가 zygote의 루트에서 닿지 않고, 이 커널의 `d_path`가 절대 경로로 물러선다. 컨테이너 루트를 네임스페이스 루트로 만들면 `/dev/__properties__`가 된다.

`pivot_root()`는 쓸 수 없다. Sony의 `/`는 부모가 없는 rootfs 마운트이고, 3.10의 `pivot_root`는 부모 없는 루트 마운트를 거부한다(`fs/namespace.c`의 `mnt_has_parent()`). `pivotinit`은 컨테이너 루트를 `MS_MOVE`로 `/` 위에 올리고 `chroot()`한다.

### init.rc가 stock과 달라야 했던 지점

- `load_all_props`를 직접 trigger한다. FireOS는 `load_all_props_action` 트리거에서 `/system/build.prop`을 읽고, 여기서 `ro.board.platform=mt8127`(MTK HAL 선택)과 `dalvik.vm.*`가 온다.
- `AMAZON_EXTRA_RESOURCE_LIST=framework/fireos-res/fireos-res.apk`를 export한다. 없으면 리소스 0x02070002를 못 찾아 `ActivityManagerService` 생성자가 던진다.
- `dalvik.vm.*`를 `on boot`에서 `setprop`한다. Fire 설정은 태블릿 값(heapsize 256m, dex2oat-Xmx 512m)이고, `ro.build.type=user`라서 `/data/local.prop` 덮어쓰기가 무시된다. ART는 `-Xmx`만큼 mmap하므로 456 MB에 안 맞는다.
- `vm.overcommit_memory=1`. 커널 휴리스틱이 ART의 힙 예약을 거부해 dex2oat가 `Failed anonymous mmap(0x40000000, ...) Out of memory`로 죽었다.
- Android의 `post-fs-data`가 만드는 디렉터리(`/data/dalvik-cache`, `/data/misc/user`, `/data/media/0/Android/{data,obb}`)를 만든다. installd가 스스로 만들다 실패하면(`media fs prepare failed`) 패키지마다 "Unable to create data dirs"가 난다.
- ueventd 규칙에 `/dev/ion 0666`, `/dev/mtk_disp|mtk_ovl|mtkfb_vsync 0660 system graphics`를 더한다. Sony의 `/dev`는 root 전용이라 `system`으로 도는 SurfaceFlinger가 열 수 없다.
- `devclean.sh`가 콜드부트가 만든 raw eMMC 노드를 지운다. vold와 `mount_all`은 쓰지 않는다.
- keystore는 띄우지 않는다. `keystore.mt8127.so`가 MTK TEE keymaster를 요구하는데 여기서는 닿지 않는다.

### 결과

init, ueventd 콜드부트, 프로퍼티 서비스, `logd`가 모두 살아 있다. servicemanager는 uid 100으로 binder 컨텍스트 매니저를 잡는다(커널이 부팅 내내 첫 매니저의 euid를 기억하고, 그게 Sony `hagodaemon`의 100이다). SurfaceFlinger는 `gralloc.mt8127`/`hwcomposer.mt8127`로 패널에 합성한다. zygote는 클래스·리소스 preload를 끝내고 system_server를 포크하며, system_server는 첫 부팅 패키지 스캔과 dexopt를 돌린다.

부팅 완료(`sys.boot_completed`)는 vold가 있어야 지나간다. `SystemServer.java:721`이 `mountService.waitForAsecScan()`을 메인 스레드에서 부르고, 그 latch는 `MountService.java:866-867`에서 vold가 접속할 때만 풀린다. vold는 `/fstab.icx` 없이만 띄운다(`vold main.cpp:154`의 `process_config`가 실패하고 `main.cpp:80-82`가 "continuing anyways"로 계속한다. 볼륨 0개, fstab 안 읽음, 소켓은 응답). vold가 뜨면 MountService가 스토리지를 세고 SurfaceFlinger가 `Boot is finished`를 찍는다. 그 다음 관문은 오디오다: system_server가 `media.audio_flinger` / `media.audio_policy`를 기다리며 멈춘다(mediaserver를 띄우지 않았다).

### 패널을 눈 없이 읽기 (`probe/devtools/fbcap.c`)

`screencap`은 SurfaceFlinger의 논리 합성이고, 이 SoC는 OVL 엔진으로 합성하므로 `/dev/fb0` 메모리도 LCD가 받는 그림이 아니다. `MTKFB_CAPTURE_FRAMEBUFFER`로 오버레이 엔진이 자기 출력을 사용자 버퍼에 쓰게 한다.

- `mtkfb.h:43`: `MTK_IOW(3, unsigned long)` = `_IOW('O',3,4)` = `0x40044f03`.
- `mtkfb.c:2680`이 4바이트 사용자 포인터를 `copy_from_user()`하고 `mtkfb_capture_framebuffer`(`mtkfb.c:2098`)로 넘긴다. 그 함수는 UI 레이어 전용이 아닐 때 `DISP_Capture_Framebuffer`를 부른다(`mtkfb.c:2152`).
- `disp_drv.c:1803`이 버퍼를 M4U로 매핑하고(`DISP_GetScreenWidth()*DISP_GetScreenHeight()*bpp/8`), 레이어가 secure이거나 early-suspend면 0으로 채운다(`disp_drv.c:1826-1830`). 크기는 `lcm_params->width/height`(`disp_drv.c:1528/1541`), 즉 패널 해상도다.

`fbcap`은 `/dev/graphics/fb0`을 열고 `FBIOGET_VSCREENINFO`로 크기를 얻어 페이지를 모두 만지고 `mlock`한 버퍼를 ioctl에 넘긴다. dmesg가 `cmd=0x40044f03(MTKFB_CAPTURE_FRAMEBUFFER)`로 이름을 찍어 주는지로 확인한다. `tools/fbcap2png.py`가 raw를 PNG로 바꾼다. 채널 순서는 B,G,R,A이고, 근거는 주황색 Fire 로고(`1b a6 fa ff`)다.

### 패널 방향: A/B와 기준 캡처

| 캡처 | 조건 | 결과 |
|---|---|---|
| `panel.png` | Fire의 `persist.panel.orientation=270` | 480x800 버퍼에 90도 돌아가 있음 |
| `panel2.png` | `persist.panel.orientation=0` | 똑바름 |
| `stock.png` | 재부팅한 순정 Sony UI | 똑바름 (기준) |

`stock.png`가 기준이다. 순정 Sony UI가 480x800 버퍼에서 똑바로 나오므로 버퍼 0행이 패널의 물리적 위쪽이고, 버퍼 방향 = 기기 방향이다. 따라서 `panel2.png`의 "똑바름"은 실제 패널에서도 똑바름이다. Fire의 `libsurfaceflinger.so`에 `persist.panel.orientation` 문자열이 있고 270을 0으로 바꾸면 캡처가 바뀐다는 것까지가 측정이다. 그 문자열을 읽는 코드 위치는 디스어셈블리에서 찾지 못했으므로 메커니즘은 `unverified`이고, 결론은 A/B 캡처에만 근거한다.
