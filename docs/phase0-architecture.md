# NW-A55 v1.02 스톡 펌웨어 아키텍처 지도 (Phase 0)

정적 분석만으로 만든 지도이다. 펌웨어 바이너리는 하나도 실행하지 않았고, `readelf`/`nm`/`strings`/디스어셈블(읽기 전용)과 rc·스크립트 읽기만 사용했다. 추출물 안의 문자열은 모두 데이터로만 취급했다. 비밀값(KAS/UBP 키, `/etc/rsa_host_key`, `/etc/passwd` 해시)은 위치만 언급하고 값은 옮기지 않았다.

**경로 약어**

| 약어 | 실제 경로 (저장소 루트 기준) | 런타임 위치 |
|---|---|---|
| RD | `nwa50-extracted/ramdisk/update-initramfs/` | `/` (루트 램디스크) |
| SYS | `system-rootfs/` (UPG `file6-system.img` 추출본) | `/system` |
| VS | `system-rootfs/vendor/sony/` | `/system/vendor/sony` |
| K | `nwa50-extracted/kernel/` (`kconfig.txt`, `vmlinux.bin`, `boot.img`) | - |
| UPG | `nwa50-extracted/upg-raw/` (`file0-update.sh`, `file7-cm4.bin` 등) | - |
| ANA | `ANALYSIS.md` (프로젝트 루트) | - |

**증거 규칙**: 사실 문장 끝의 괄호가 근거 파일이다. 추론은 `(추정)`, 근거가 없으면 "미확인"이라고 적었다. 근거 방식은 필요한 곳에만 `strings`, `nm`, `readelf`, `디스어셈블`로 표기했다. 번호 매긴 절은 정확히 7개이고, 그 위의 이 머리말은 절이 아니다.

**한 장 요약**

```
LK(uboot) -> boot.img [kernel 3.10.26 + ramdisk(RD, init 55KB)]
               |  root=/dev/ram, /system = /emmc@android (ext4 ro)
               v
        init.rc + init.hagoromo.rc  --(sys.sony.bootmode=1)--> class_start hagoromo
               v
  hagoromo1 = hagodaemon 마스터 (BinderDriver::BecomeContextManager, /dev/binder)
  hagoromo2..28 = hagodaemon sub_sm x27  (서비스 38개를 dlopen("lib<Name>.so")로 적재)
               ^  표준 Android binder ioctl, 자체 ServiceManager (Android servicemanager 없음)
               |
  appmgrservice --fork/execvp--> HgrmMediaPlayerApp (type Home, Qt 5.3.2 QML, eglfs, Mali-450)
```

---

## 1. 부팅과 프로세스 모델

### 1.1 부트 체인

1. 부트로더(LK, `uboot` 파티션)가 커널 명령행을 공급한다. `boot.img` 헤더의 cmdline은 비어 있고(K boot.img), `CONFIG_CMDLINE_FROM_BOOTLOADER=y`이다(K kconfig.txt). kconfig의 기본 문자열은 `console=tty0 console=ttyMT3,921600n1 root=/dev/ram vmalloc=496M ...`이다(K kconfig.txt). 실제 LK가 넘기는 값은 미확인이다.
2. 커널은 Linux 3.10.26, ARMv7, `MT8590_ICX` 보드, `ARCH_MTK_PROJECT=BBDMP5_linux`이다(K kconfig.txt). 루트는 램디스크이다(`root=/dev/ram`, 위 기본 문자열 기준).
3. 램디스크 `/init`은 55KB짜리 Android 스타일 init이다(RD init). 문자열에 `early-init`, `init`, `early-fs`, `post-fs`, `post-fs-data`, `early-boot`, `boot`, `charger`, `seclabel`, `restorecon`, `class_start`, `socket`, `setenv`가 있고(strings), `late-init`은 없다. 따라서 구형 트리거 체계로 보인다(추정). `fs`는 `strings -n 4`로 잡히지 않았지만 `init.rc`가 `on fs`를 쓴다(RD init.rc:74).
4. 내장 property service가 `/data/property`에 영속 속성을 쓰고 `ro.boot.*`를 cmdline에서 만든다(RD init strings). 펌웨어는 `/etc/firmware`에서 로드한다(RD init strings).
5. 램디스크에는 `sepolicy`가 없다(RD 트리). 커널에는 `SELINUX`가 켜져 있다(K kconfig.txt). 서비스 정의에 `seclabel u:r:adbd:s0`이 있으나(RD init.rc:336) 정책 파일이 없으므로 실질적으로 비활성이다(추정).
6. `RD default.prop`: `ro.secure=0`, `ro.debuggable=1`, `persist.sys.usb.config=adb`. `SYS build.prop`: `full_BBDMP5_linux-userdebug 5.0 LRX21M`, `ro.board.platform=mt8590`, `ro.sony.version=1.02`, `ro.sf.lcd_density=213`, `ro.opengles.version=131072`.
7. `/system`은 `mount ext4 /emmc@android /system noatime ro`로 마운트된다(RD init.rc:75).
8. ANA의 확인 사항: 이 램디스크는 일반 부팅 램디스크와 동일하고, `icx_start_update.sh`는 번들 어디서도 호출되지 않으며, 실제 업데이터 rootfs의 위치는 미확인이다.

**툴체인/ABI** (뒤 절들의 전제): 모든 Sony 바이너리는 Chromium OS clang 3.9.0(일부 gcc 4.9.2)로 `armv7a-cros-linux-gnueabi`, hard-float, Thumb, glibc 2.23용으로 빌드됐다(VS bin/·lib/ `.comment`/`readelf`). C++ 런타임은 libc++(`std::__1`) + libcxxrt이다. 램디스크 `/lib`에 `libc++.so.1`, `libcxxrt.so.1`, `libglibc_bridge.so`, `libc_stubs.so`가 있고, `/etc/ld.so.preload`는 `/lib/libc_stubs.so`이다(SYS etc/ld.so.preload, RD lib/). 인터페이스가 `std::__1::string/function/shared_ptr`을 쓰므로 교체 코드도 같은 clang+libc++ ABI여야 한다. Qt는 mkspec `devices/linux-mt8590-clang-libc++`로 빌드됐다(VS plugins/platforms/libqeglfs.so strings).
Android 라이브러리 일부(libcutils, liblog, libhardware, libnetutils, libutils)가 glibc 프로세스에서 쓰이고, libbinder는 `service` 도구만 쓴다(SYS bin/service `readelf`). `libglibc_bridge.so`는 `/dev/socket/property_service`와 통신한다(RD lib/ strings).

### 1.2 init 단계별 동작

| 단계 | 정의 위치 | 동작 |
|---|---|---|
| early-init | RD init.rc:7-30 | `start ueventd`; `min_free_kbytes` 8192 (주석: USB DAC용 4096KB 초과 연속 메모리 필요); debugfs 마운트; tmpfs `/tmp` 32m; `/dev` remount 4m; core_pattern을 `/sbin/coredump.sh /data/coredump ...`로 설정 |
| init | RD init.rc:32-72, RD init.hagoromo.rc:1-8 | `PATH`, `LD_LIBRARY_PATH`, `QT_IM_MODULE=inputcontextproxy`; `/data` 생성; `/dev/shm` tmpfs 64m; `/dev/snd/timer` -> `/dev/timer`; Cast 잔재 속성(`ro.eureka.*`); `/sys/autok` 권한; hagoromo용 Qt 환경변수 7개 export (3.3절) |
| fs | RD init.rc:74-122 | `/system` ro 마운트; `start bootanimation`; `load_sony_driver_early`; `linear_contents`; `format_contents_after_install`; `fsck_msdos -y /emmc@contents`; `mount_partition cache usrdata var db option1 option2 option3 contents`; `setprop sys.usb.msc1 /emmc@contents`; `/data/share`, `/data/factory`(`/factory` 링크), `/data/nvram`(2771) 생성 |
| post-fs | RD init.rc:124-176 | `start icx_syslog`; `wdt_ctrl 30`; `ifconfig lo up`; `/data/property`(0700); TCP 버퍼; `link_timezone.sh`; `buildinfo.sh`; `start set_iserial`; USB `iManufacturer=Sony`, `iProduct=WALKMAN` |
| post-fs-data | RD init.project.rc | `/dev/ttyMT2` 권한; `insmod /system/lib/modules/mtk_wmt_detect.ko`; `mknod /dev/stpwmt c 190 0`, `/dev/stpgps c 191 0`, `/dev/stpbt c 192 0`; `/dev/stpbt`는 `bluetooth radio` 소유; `start wmtLoader`; `start conn_launcher`(해당 service 정의 없음) |
| early-boot | RD init.hagoromo.rc:10-38 | rlimit; core_pattern을 `/system/vendor/sony/etc/hcoredump.sh /var/log`로 교체; `/var/log`, `/var/run`; 아래 sysfs 노드를 0666으로 개방: `/sys/power/{state,wake_lock,wake_unlock,dpidle_state}`, `icx_pm_helper/resume_lock_cancel`, `mt_sleep/parameters/slp_mode`, `leds/lcd-backlight/brightness`, `android_usb/android0/{enable,functions,idProduct,idVendor,MaxPower}`, `f_mass_storage/lun/file`, `mt_usb/mode`, `mt-rtc/.../wakealarm`, cpufreq hotplug `cpu_num_base`, `scaling_min_freq`, `snd_soc_cxd3778gf/parameters/timed_mute_ms`; `/contents_ext`, `/contents_ext1` 생성(0777) |
| boot | RD init.rc:178-261 | `start load_sony_driver`, `adbd`, `sshd`, `nvram_daemon`, `dpfd`, `mtkbt`(BlueAngel 라이브러리 chown/chmod 후); CPU hotplug 파라미터; `wait /dev/icx_nvp/000`; `bootswitcher.sh`; `optproc_for_boot.sh`(주변기기 펌웨어 갱신, 1.4절) |
| property 트리거 | RD init.rc:264-281 | `sys.sony.bootmode=1` -> `class_start hagoromo`; `=2` -> `prepare_contentroot.sh`, `stop bootanimation`, `start icx_diag`; `sys.sony.console=0/1` -> `stop/start icx_console` |

비고
- `class_start`는 `hagoromo`에만 쓰인다(RD init.rc grep). `class main`(wmtLoader, ffu)과 `class core`(autokd, 66xx_launcher*)를 일괄 기동하는 코드는 없다. 이들은 명시적 `start`가 있을 때만 뜬다.
- `late_start` 클래스의 `mount_msc1`/`unmount_msc1`/`fuse_usbotg`는 `disabled`이며 USB 대용량 저장 모드 전환 때 쓰이는 것으로 보인다(추정, 시작 지점은 미확인).
- `sshd`는 `start sshd`가 있으나 `/usr/bin/sshd`가 RD와 SYS 어디에도 없다(존재 검사). 기동은 실패한다(추정: 조용히 무시).
- `PATH`에 `/system/vendor/sony/bin`이 포함된다(RD init.rc:34). `LD_LIBRARY_PATH`에는 `/system/vendor/sony/lib`가 없고, Sony 바이너리는 RPATH/RUNPATH로 찾는다(RD init.rc:35, VS bin/HgrmMediaPlayerApp `readelf -d`).

### 1.3 부트 모드 분기 (`bootswitcher.sh`)

`SYS bin/bootswitcher.sh`는 NVP 플래그 `tst`/`gty`/`prk`와 `icx_bid3`를 읽어 속성 `sys.sony.{bootmode,console,gettyport,wmp}`를 정한다.
- `tst`가 0x4D545346 또는 0x4D545354이면 `bootmode=2`(진단)이다. 아니면 `bootmode=1`(일반)이다.
- `gty`가 0x67747479이면 콘솔을 켠다(`sys.sony.console=1`, `icx_console`이 `ro.boot.console` 포트에서 getty 실행).
- 콘솔/printk가 꺼져 있으면 `sys.sony.wmp=1`이다. UART `/dev/ttyMT1`이 콘솔과 WM-PORT(크래들) 겸용이기 때문이다.
- 진단 모드는 `dodiag.sh`가 `diag_main`을 `/dev/ttyMT1` 921600bps로 띄운다(SYS bin/dodiag.sh, SYS xbin/diag_main).

### 1.4 `load_sony_driver` (boot 단계, 이후 hagoromo보다 먼저)

`SYS bin/load_sony_driver`(스크립트)가 수행하는 일:
- `init_nvp.sh` 실행(RD install_update_script/init_nvp.sh 계열).
- `insmod`: `wm_key.ko`(`eint_num_key_wake=6`), `cxd3778gf_dnc_core.ko`, `icx_carddet.ko`, `icx_usbconn.ko`, `icx_usbscsi.ko`. `radio-si4708icx.ko`는 모델 코드(`shp` 0x1/0x306)에 따라 `deemphasis=750`.
- `nvram_rw &` 실행; NVP `mid`의 용량 바이트를 eMMC 크기에서 계산해 기록.
- `dacdat auto $PRODDEV ...` 후 `/system/usr/share/audio_dac`의 튜닝 테이블(cnw500n/750n/cnc31n, idata, ainc, limiter, u_ncgain, ambgain)을 `/proc/icx_audio_cxd3778gf_data`와 `/proc/icx_audio_dnc_data`에 기록한다.
- `load_sony_driver_early`(fs 단계)는 `icx_nvp_emmc.ko`를 올리고 `nvpnode`로 `/dev/icx_nvp/*`를 만든다.
- 두 `load_sony_driver*`는 sh 스크립트이고 `dacdat`는 ELF이다(SYS bin/ `file`).

**부팅마다 도는 주변기기 펌웨어 갱신** (`boot` 단계 마지막, bootswitcher 직후): `SYS usr/bin/optproc_for_boot.sh`가 `icx_bid2`를 읽고 어느 쪽이든 `optproc_for_boot_1291.sh`를 호출한다(SYS usr/bin/optproc_for_boot.sh, 두 분기가 같다). 이 스크립트가 순서대로 실행한다(SYS usr/bin/optproc_for_boot_1291.sh).
1. `sony_tovis_updater.sh`: NVP `syi` 플래그의 하위 3비트가 3이면 `/system/etc/firmware/Sony_Tovis_ICX1291_*.bin` 중 마지막 파일명을 골라 `echo "t <파일>" > /proc/himax_touch/debug`로 터치 IC 갱신을 요청한다. 같은 버전이면 건너뛰는지는 이 스크립트에 없다(드라이버 쪽 동작 미확인).
2. `cxd3778gf_ucom_fwupdate.sh`: `/sys/class/i2c-dev/i2c-1/device/1-004e/fw_version`과 파일명(`cxd3778gf_ucom_fw_00_16.bin`의 `0016`)을 비교해 더 새로우면 `/sys/class/i2c-dev/i2c-0/device/0-0010/fwupdate`에 파일명을 써서 최대 5회 재시도한다.
3. `nfc_cxd225x_updater.sh`: NVP `tst`가 0x4D545346이 아니면 `cxd225x_fwupdater /system/etc/firmware/cxd225x_firmware.bin`을 실행한다. 버전 비교는 이 스크립트에 없다.
이 동작은 UI 교체 실험 중에도 매 부팅마다 일어난다. 이 이미지의 펌웨어 파일은 `SYS etc/firmware/`에 있다.

### 1.5 init.rc / init.project.rc 의 기타 서비스

| service | 정의 | 상태/비고 |
|---|---|---|
| ueventd | `/sbin/ueventd`, class onboot, critical (RD init.rc:293) | `early-init`에서 start |
| bootanimation | `/bin/logwrapper /system/xbin/icx_bootanimation`, oneshot | `fs`에서 start; app의 `StopBootAnimation`이 정지(3.10절) |
| console | `/bin/sh -I`, console | start 주석 처리 |
| icx_console | `/system/bin/icx_console`, class service | `sys.sony.console`로 제어 |
| nvram_daemon | `/system/bin/nvram_daemon`, user root/group system, oneshot | boot에서 start |
| autokd | `/system/bin/autokd`, class core | start 트리거 미발견; `/sys/autok` 권한만 설정 |
| adbd | `/sbin/adbd --root_seclabel=u:r:su:s0`, `socket adbd`, disabled | boot에서 start + USB 속성 트리거에서 재기동 |
| boot_complete | `/bin/sh /sbin/boot_complete.sh`, disabled | start 주석 처리 |
| load_sony_driver / set_iserial | oneshot | boot / post-fs |
| mount_msc1 / unmount_msc1 | `mount_partition contents` / `umount /contents`, late_start, disabled | 대용량 저장 모드용(추정) |
| dpfd | `system/bin/dpfd`, user root/group system, oneshot | boot에서 start |
| mtkbt | `/bin/mtkbt > /tmp/mtkbt.log`, user system, oneshot | boot에서 start |
| icx_syslog | `/system/xbin/icx_syslog -n 32 -l 6 -d "/emmc@var"` | post-fs |
| icx_hdump_log | `/system/xbin/icx_hdump_log -c`, oneshot | RD init.rc:429 |
| icx_diag | `/system/bin/dodiag.sh`, oneshot | bootmode=2 |
| wmtLoader | `/system/bin/logwrapper /system/bin/wmt_loader`, class main | post-fs-data에서 start (RD init.project.rc) |
| ffu | `/system/bin/ffu`, class main | start 트리거 미발견. 용도 미확인 |
| 66xx_launcherA / C | `6620_launcherA -p /system/etc/firmware/`, `6620_launcherC -m 4 ...`, class core, disabled | 시작 지점 미발견 |
| fuse_usbotg | `/system/bin/sdcard -u 1023 ... /storage/usbotg`, late_start, disabled | `SYS bin/sdcard` 없음 |
| wpa_supplicant, stp_dump | `wpa_supplicant`, `stp_dump3` | 바이너리 없음 (SYS bin) |
| sntpd, dhcpcd, cast_receiver, cast_installer, bdpprog_shell, imdb_shell, net_mgr, watchdog | Chromecast 이식 잔재 | 바이너리 없음 또는 start 주석 처리. `cast_receiver`는 `/chrome/cast_cli`를 가리키나 `chrome` 파티션 내용은 미확인 |

`sshd`와 `wpa_supplicant`는 기동 시도가 있어도 바이너리가 없다(존재 검사). Wi-Fi는 이 이미지에서 쓰이지 않는다(wlan 모듈 없음, SYS lib/modules).

### 1.6 init.hagoromo.rc 서비스 (28개 전부)

모든 서비스는 `/bin/logwrapper /system/vendor/sony/bin/hagodaemon <인자>`로 시작하고 `class hagoromo`, `oneshot`이다(RD init.hagoromo.rc:40-226). 표의 인자는 `hagodaemon` 뒤의 것이다. "시작 uid"는 rc의 `user`이고, `userAndGroup=system,system`이 있으면 서비스가 시작 후 root 권한을 버린다(디스어셈블, 1.7절).

| service | hagodaemon 인자 | class | 시작 uid / 권한 | 호스팅 서비스와 역할 |
|---|---|---|---|---|
| hagoromo1 | `userAndGroup=system,system groups=log,chrome capabilities=5,22 daemonNum=28` | hagoromo | root -> system; 보조 그룹 `log`, `chrome`; cap 5(KILL), 22(SYS_BOOT) | 마스터. 서비스를 호스팅하지 않음. binder context manager, 서비스 등록·감독·재부팅 |
| hagoromo2 | `appmgrservice sub_sm` | hagoromo | system, 그룹 `system inet` | 앱 관리자. `*.appcfg` 스캔, 앱 fork/exec, 라이프사이클 (VS lib/libappmgrservice.so) |
| hagoromo3 | `PowerService sub_sm` | hagoromo | root, 드롭 없음 | `/sys/power/state`(mem), wake_lock, `icx_pm_helper`, 전원 상태, 종료/재부팅, 부분 기상 |
| hagoromo4 | `EventRouter sub_sm` | hagoromo | system | 이름 기반 이벤트 pub/sub (`SendEvent`/`AddListener`) |
| hagoromo5 | `TimerService capabilities=1,3,25,35 userAndGroup=system,system sub_sm` | hagoromo | root -> system; cap 1,3,25(SYS_TIME),35(WAKE_ALARM) | `/dev/rtc0`, wakealarm, 알람, 시간대(`/data/share/chrome/localtime`) |
| hagoromo6 | `WMPortService KeyService ConfigurationService DisplayService userAndGroup=system,system capabilities=1,12 sub_sm` | hagoromo | root -> system; cap 1, 12(NET_ADMIN) | 4개 동거. WM-PORT/크래들(`/dev/icx_cradle`, `/dev/ttyMT1`, uinput); 키(`/dev/input/event*`, wm_key 파라미터); 설정(NVP); LCD 백라이트·LED·터치 유효화 |
| hagoromo7 | `UpdateService sub_sm` | hagoromo | system, 그룹 `system inet` | 펌웨어 업데이트 (`/contents/NW_WM_FW.UPG`, microSD `*.UPG`, `SetFupFlag`) |
| hagoromo8 | `UsbHostConnectionService UsbDeviceConnectionService UsbDeviceAudioPlayerService userAndGroup=system,system capabilities=1,12 nice=-10 sub_sm`; env `PST_LOG_UDAP=8 PST_LOG_UHCS=6 PST_LOG_UDCS=6` | hagoromo | root -> system; cap 1,12; nice -10 | USB 호스트/디바이스 연결 감시, USB DAC(UAC) 재생 |
| hagoromo9 | `MediaStoreService PlayerService userAndGroup=system,system capabilities=13 sub_sm`; env `PST_LOG_PLYRSRVC=6 PST_LOG_MDSTRSRV=6` | hagoromo | root -> system; cap 13(NET_RAW) | 미디어 DB(SQLite 3.7.6.3 + 병음 정렬)와 재생 엔진. 같은 프로세스에 동거하며 PlayerService가 libMediaStoreService.so를 NEEDED로 가짐 |
| hagoromo10 | `OneTrackPlayerService userAndGroup=system,system sub_sm`; env `PST_LOG_ONEP=6` | hagoromo | root -> system | 단일 트랙 재생(효과음/경량 재생, 추정) |
| hagoromo11 | `SoundServiceFw capabilities=23 userAndGroup=system,system nice=-15 sub_sm`; env `PST_LOG_SS=8 PST_LOG_AHAL=8` | hagoromo | root -> system; cap 23(SYS_NICE); nice -15 | 오디오 출력 엔진. 소프트웨어 DSP 체인과 audiohal-* 플러그인, ALSA 출력 |
| hagoromo12 | `NcServiceFw userAndGroup=system,system capabilities=12 sub_sm`; env `PST_LOG_NC=8` | hagoromo | root -> system; cap 12 | 노이즈 캔슬링(`/sys/class/switch/cxd3778gf_ucom/state`) |
| hagoromo13 | `VolumeServiceFw userAndGroup=system,system sub_sm`; env `PST_LOG_VOL=8` | hagoromo | root -> system | 마스터 볼륨, AVLS, SE/BTL 게인, 리미터 경고 |
| hagoromo14 | `VolumeGlue userAndGroup=system,system sub_sm`; env `PST_LOG_VOL=8` | hagoromo | root -> system | 볼륨 서비스 접착 프로세스 |
| hagoromo15 | `RecorderService userAndGroup=system,system sub_sm` | hagoromo | root -> system | 녹음 |
| hagoromo16 | `AudioAnalyzerService userAndGroup=system,system sub_sm` | hagoromo | root -> system | 오디오 분석(레벨/스펙트럼, `audioanalyzer_params`) |
| hagoromo17 | `ConnMgrServiceFw userAndGroup=system,system sub_sm`; env `PST_LOG_CMGR=8 PST_LOG_RDAE=8` | hagoromo | root -> system | 연결 장치 상태 집계(유선 헤드폰, USB 호스트/디바이스, BT 송신, WM-PORT, 카드 감지) |
| hagoromo18 | `FuncMgrServiceFw userAndGroup=system,system sub_sm`; env `PST_LOG_FMGR=8` | hagoromo | root -> system | 기능 모드(`FuncMode`, `EnterFuncMode`, `SetBootFuncMode`) |
| hagoromo19 | `PathMgrServiceFw userAndGroup=system,system sub_sm`; env `PST_LOG_PMGR=8 PST_LOG_VOL=8` | hagoromo | root -> system | 오디오 경로(amixer 제어 "analog input device", "output device", "headphone amp") |
| hagoromo20 | `PowerMgrServiceFw userAndGroup=system,system capabilities=12 sub_sm`; env `PST_LOG_PWMG=8` | hagoromo | root -> system; cap 12 | 전원 정책, 배터리/충전, 부팅·재개 요인, 공장 초기화 |
| hagoromo21 | `RemoteCmdInjCtrlDaemon userAndGroup=system,system sub_sm` | hagoromo | root -> system | 원격 명령 주입 제어(이름으로 추정, 용도 미확인) |
| hagoromo22 | `UsbMgrServiceFw userAndGroup=system,system capabilities=12 sub_sm`; env `PST_LOG_UMGR=8` | hagoromo | root -> system; cap 12 | USB 기능 전환(android_usb), OTG 모드, `SetAdbEnabled` |
| hagoromo23 | `StorageMgrServiceFw sub_sm`; env `PST_LOG_SMGR=8` | hagoromo | root, 드롭 없음 | microSD/내장 스토리지 마운트, 포맷, 대용량 저장 LUN |
| hagoromo24 | `WiredHpServiceFw userAndGroup=system,system capabilities=12 sub_sm` | hagoromo | root -> system; cap 12 | 헤드폰 잭 감지(uevent) |
| hagoromo25 | `NfcService sub_sm` | hagoromo | system | NFC (`libnfc-nci`, `/dev/cxd224x-i2c`) |
| hagoromo26 | `MemmonService sub_sm` | hagoromo | root, 드롭 없음 | 메모리 감시(procrank, `/var/log/memmon*.csv`) |
| hagoromo27 | `BtCommonService BtTransmitterService BtBleCommonService BtBleRemoteService BtPlayerService capabilities=23 sub_sm`; env `PST_LOG_BT=6` | hagoromo | system; cap 23 | 블루투스 5개 동거 (mtkbt 소켓, LDAC/aptX/AAC/SBC) |
| hagoromo28 | `AudioInPlayerService TunerPlayerService userAndGroup=system,system nice=-10 sub_sm` | hagoromo | root -> system; nice -10 | 외부 입력 재생, FM 튜너 재생(`/dev/radio`) |

capability 번호는 Linux `capability.h` 값이다(1 DAC_OVERRIDE, 3 FOWNER, 5 KILL, 12 NET_ADMIN, 13 NET_RAW, 22 SYS_BOOT, 23 SYS_NICE, 25 SYS_TIME, 35 WAKE_ALARM). 서비스 역할은 라이브러리의 `strings`/`nm`에서 읽은 것이고(VS lib/lib<Name>.so), 동작 검증은 하지 않았다.

### 1.7 hagodaemon 인스턴스와 서비스의 매핑 (근거: 디스어셈블)

`VS bin/hagodaemon`은 11KB 스텁이다. `main`이 `pst::core::Framework::StartForDaemon(int, const char**, std::function<void()>)`(VS lib/libpstcore.so)를 호출한다. `libpstcore.so`의 RUNPATH는 `/system/vendor/sony/lib:$ORIGIN/../lib`이다(readelf).

`StartForDaemon`을 디스어셈블해 확인한 인자 문법:
- 옵션 인자: `nice=`, `schedparam=`, `groups=`, `capabilities=`, `userAndGroup=`, `daemonNum=`, 그리고 키워드 `sub_sm`.
- 옵션이 아닌 인자는 서비스 이름이다. 각각 `ServiceLibrary::CreateInstance(name)`으로 `dlopen("lib%s.so")`한 뒤 심볼 `_ZN3pst8services%d%sFactory14CreateInstanceEv`로 서비스를, `..%sClientFactory14CreateInstanceEv`로 클라이언트를 만든다. 로그 "Register <name> object" 뒤 `ServiceManager::AddService`를 호출한다.
- `daemonNum=N`은 `ServiceManager::SetDaemonSubCount(N)`을 호출한다. hagoromo1만 가진다. sub_sm이 27개이므로 28은 마스터를 포함한 인스턴스 총수일 가능성이 크다(추정).
- `sub_sm`이 없으면 마스터이다. `BinderDriver::BecomeContextManager()`를 호출하고 `NotifyReady(SMKind)`를 호출한다. `sub_sm`이면 마스터에 `RegisterSub`로 등록한다.
- `userAndGroup=`와 `capabilities=`는 `StartForDaemon`이 파싱한다(디스어셈블). rc에서 root로 시작한 서비스가 이 값으로 root 권한을 버리고 지정 capability만 남기는 것으로 보이지만(추정), 드롭 시점과 순서는 확인하지 않았다.
- init의 `user system`은 init이 쓰는 이름 표로 해석되며 uid 값은 `/etc/passwd`의 100(SYS etc/passwd)과 다를 수 있다. 런타임 uid는 미확인이다.

핵심 귀결:
- 서비스 38개가 프로세스 28개에 분산된다. 프로세스를 나누는 기준은 권한(capability, nice)과 장애 격리이다(추정).
- 서비스 구현은 공유 라이브러리이므로 동일 프로세스 안에 여러 서비스가 있으면 서로 binder 없이 직접 호출할 수 있다(추정). 예: hagoromo9의 PlayerService -> MediaStoreService.
- 프로세스 이름이 전부 `hagodaemon`이므로 `ps`에서 구분하려면 cmdline이 필요하다. `hagodaemons.sh`는 `init.hagoromo.rc`를 grep해 전체 데몬을 start/stop한다(VS bin/hagodaemons.sh).

### 1.8 감독과 재부팅 정책 (근거: libpstcore.so strings, 디스어셈블)

- 마스터가 `fr_hang` 스레드로 hang 검사를, binder death notification으로 사망 검사를 한다. 스레드 이름: `fr_binder`, `fr_eloop`, `fr_job`, `fr_hang`.
- 문자열: "hagodaemon death detected!", "hagodaemon hang detected!", "Application(pid=%s) main thread is freezed, reboot the system...", "Power state transition is stopped by this program, it will be aborted...".
- 재부팅 경로: `kill(pid, SIGTERM)` -> `sync` 두 번 -> `android_reboot(0xDEAD....)` (libpstcore.so 0x3fc00-0x3fcf4 디스어셈블).
- 앱도 `Framework::StartForApplication`으로 `[app]` sub-SM으로 등록되고 `ServiceManager::NotifyCrash app=%d`가 있다. 앱 크래시가 재부팅을 일으키는지는 미확인이다. 그러나 앱 메인 스레드 freeze는 시스템 재부팅이다(문자열).
- 앱은 `pst::core::Framework::UpdateCurrentContextHangTimeout(unsigned long long)`과 `GetReference()`, `GetBinderLastError()`를 import한다(VS bin/HgrmMediaPlayerApp strings). hang 검사 시간을 앱이 늘릴 수 있다는 뜻이다(추정). 기본 시간과 단위는 미확인이다.
- 수명주기 훅: `OnInit`, `OnRun`, `OnSuspend`, `OnResume`, `OnPostResume`, `OnEarlySuspend`, `OnLateResume`, `OnPreShutdown`, `OnShutdown`, `OnPreResetSetting`, `OnPostResetSetting`, `ReadyToSuspend`, `ReadyToShutdown`, `ReadyToEarlySuspend`(libpstcore.so, libeaselcore.so nm).

### 1.9 런타임 프로세스 구성 (정상 부팅 후, 추정)

`init`, `ueventd`, `icx_syslog`, `nvram_daemon`, `nvram_rw`(load_sony_driver 파생), `dpfd`, `mtkbt`, `adbd`, `hagodaemon` 28개(`logwrapper` 경유), 그리고 appmgrservice가 fork한 `HgrmMediaPlayerApp` 1개이다. `logwrapper`가 각 hagodaemon의 부모로 남는지, exec로 대체되는지는 미확인이다.

디버그 도구: `hagomon`, `hagomach`(libpstmach_.so), `hdumpstate`, `fillheap`, `ihagomon.sh`, `memmon.sh`(VS bin/, VS etc/). `libLogupStub.so`는 `/tmp/loguploader-socket.path`를 쓴다(strings).

---

## 2. IPC 프레임워크

### 2.1 핵심 라이브러리와 역할

| 라이브러리 | 역할 | 근거 |
|---|---|---|
| `libpstcore.so` | Framework 전부. `pst::core::Framework`, `ServiceManager`, `BinderDriver`, `ServiceLibrary`, `TransactionParam`. 모든 서비스·클라이언트·앱이 NEEDED로 링크 | VS lib/libpstcore.so (nm, 디스어셈블; NEEDED 역방향 표) |
| `libeaselcore.so` | 앱 프레임워크. `easel::ApplicationBase`의 `OnInitialize/PostInitialize/Activate/Foreground/Background/Inactivate/Finalize/Suspend/Resume`, `StopBootAnimation`, `StartResumeAnimation` | VS lib/libeaselcore.so (nm) |
| `libeaselqt.so` | `easel::QtApplication` (Qt 이벤트 루프 결합) | VS lib/libeaselqt.so (nm) |
| `libeaselcui.so` | `CuiAppModule` (CUI 앱 모듈) | VS lib/libeaselcui.so (nm) |
| `libappmgrservice.so` | `AppManagerService` + `LifeCycleManager`. libeaselcore가 NEEDED | VS lib/libappmgrservice.so |
| `libDmpConfig.so` | DMP 설정 키 정의와 `AdbEnablerImpl` 등 | VS lib/libDmpConfig.so |
| `libDmpFeature.so` | 모델/출하지/헤드폰 기능 플래그, USB VID/PID, 초기 볼륨, 게인 임계값 | VS lib/libDmpFeature.so |
| `libFuncArchUtils.so` | funcarch(Fw 서비스군) 공용 유틸. 거의 모든 Fw 서비스·audiohal이 NEEDED | readelf 역방향 표 |
| `libInitialObject.so` | `hgrmdmp_utils::DeviceInfo`(DirectRecAvailable, XApplicationAvailable 등), `InitialFile`(기본 capability 파일 복사). StorageMgrServiceFw만 NEEDED | VS lib/libInitialObject.so (nm) |
| `libKeyListener.so` | `KeyListener`: `IKeyService::key_packet_t`를 Qt `QEvent::Type`/`Qt::Key`로 변환해 전송. Qt Widgets 링크. 이미지 내 참조자 없음 | VS lib/libKeyListener.so (nm, 전체 grep) |
| `libjson.so` | 앱의 JSON 파서 | HgrmMediaPlayerApp NEEDED |

Android 쪽 `servicemanager`는 사용되지 않는다. 마스터 hagodaemon이 직접 context manager가 된다.

### 2.2 binder 프로토콜

- `libpstcore.so`가 `/dev/binder`를 직접 열고 표준 Android binder ioctl을 쓴다. 디스어셈블로 확인한 상수: `BINDER_WRITE_READ` 0xc0186201, `BINDER_VERSION` 0xc0046209, `BINDER_SET_CONTEXT_MGR` 0x40046207.
- 커널 쪽: `ANDROID_BINDER_IPC`(K kconfig.txt). 1.7절의 마스터가 `SET_CONTEXT_MGR`를 잡으므로 Android `servicemanager`와 공존할 수 없다(커널 context manager는 하나, 일반 지식).
- 페이로드 포맷은 Sony 고유이다: `TransactionParam`과 서비스별 `ReqMsg_X`/`RspMsg_X` 구조체, `XFwBase::OnTransactImpl(code, TransactionParam&, TransactionParam*)`(VS lib/lib*Fw.so nm). 트랜잭션 코드 번호와 직렬화 바이트 배치는 미확인이다.
- 리스너/콜백은 binder 객체(`ServiceListenerProxy`)로 서버에 등록된다(libpstcore.so nm).
- 오디오 PCM은 binder가 아닌 POSIX 공유 메모리로 옮긴다: `/MappedShmHolder`, `shm_open`, `IpcAudioBuffer`(`/dev/shm` tmpfs 64MB, RD init.rc:42), 분석기·AudioIn은 `pst::ipcmw::ipcsocket`도 쓴다(libSoundService.so, libAudioAnalyzerService.so strings).

### 2.3 서버 / 클라이언트 / Fw 명명 규칙

- `lib<Name>.so`: 서버 구현 + binder 프록시. 모든 서버 라이브러리 38개가 `<Name>Factory`와 `<Name>ClientFactory`를 모두 export한다. 따라서 같은 .so를 서버 프로세스는 서버로, 소비자는 클라이언트로 쓴다.
- `lib<Name>Fw.so`: "funcarch" 계층 서비스(ConnMgr, FuncMgr, PathMgr, PowerMgr, UsbMgr, StorageMgr, WiredHp, Nc, Volume, Sound). 생성된 `XFwClient`/`XFwBase` 클래스를 가진다.
- `lib<X>Service.so`(약 55-63KB): Fw 서비스의 클라이언트 파사드. 예: `VolumeService::SetVolume`, `FuncMgrService::EnterFuncMode`. `Get<X>ServiceFwRef`를 가진다. 대상: VolumeService, FuncMgrService, ConnMgrService, PathMgrService, UsbMgrService, StorageMgrService, WiredHpService, NcAsmService, SoundServiceSettingsDmp.
- `lib<X>ServiceClient.so`: 고수준 래퍼.
  - `libPlayerServiceClient.so`: `PlayController`, `PlayerService::GetController(name)`, `SetTrackSequence`, `Alloc/AllocNext/AllocPrev`, `OnNextTrack`, `SeekTime`, `PlayEventListener`. `libPlayerServiceClientUtil.so`: `Node`, `UriInfo`, `NodeJsonUtil`.
  - `libMediaStoreServiceClient.so`: `Config(string, string, vector<DicPath>, vector<string>, vector<StorageInfo>)`, `MediaStoreService::SetConfig`, `MediaStoreClient::Search`.
  - `libOneTrackPlayerServiceClient.so`, `libPowerMgrServiceClient.so`, `libRecorderServiceClient.so`, `libAudioInRecorderServiceClient.so`, `libAudioAnalyzerServiceClient.so`.
- 인터페이스 정의가 헤더에만 있는 서비스(export된 Client/Base 메서드 없음): BtCommonService, BtBleCommonService, BtBleRemoteService, BtTransmitterService, MediaStoreService, NfcService, OneTrackPlayerService, PlayerService, PowerService, RecorderService, WMPortService. 이들의 `I*Service` 가상 함수 표는 바이너리에서 vtable/심볼로 복원해야 한다(nm).
- 반대로 `libConfigurationService`(타입별 Get/Set(GroupId, KeyId), `*WithPassword`, `GetSystemNvp`), `libEventRouter`, `libDisplayService`(`SetLCDBacklightBrightness`, `SetLedBlink`, `SetTouchPanelValidate`), `libKeyService`(`GetKeyStatusAll`, `SetSingleKeyRepeat`), `libTimerService`(`SetClock`, `SetAlarmTimer`, `SetTimeZone`), `libUpdateService`, `libMemmonService`, `libTunerPlayerService`, `libAudioInPlayerService`, `libBtPlayerService`는 Client 메서드를 export한다(nm).

### 2.4 서비스 표 (38개)

"정적 소비자 없음"은 다른 Sony 바이너리의 NEEDED에 안 잡힌다는 뜻이다. 이 경우 `GetServiceClient(name)`의 `dlopen` 경로로만 쓰일 것으로 본다(추정). 앱 문자열에 서비스 이름이 있으면 "앱 문자열"로 적었다(HgrmMediaPlayerApp strings).

| 서비스 | 서버 lib | 클라이언트 lib | 주요 소비자 |
|---|---|---|---|
| appmgrservice | libappmgrservice.so | 동일 | libeaselcore.so가 NEEDED -> 모든 easel 앱 |
| PowerService | libPowerService.so | 동일 | 정적 소비자 없음 |
| EventRouter | libEventRouter.so | 동일 | 정적 소비자 없음 |
| TimerService | libTimerService.so | 동일 | 앱 문자열 |
| WMPortService | libWMPortService.so | 동일 | 정적 소비자 없음 |
| KeyService | libKeyService.so | 동일 | 앱 문자열; libKeyListener.so가 타입 사용 |
| ConfigurationService | libConfigurationService.so | 동일 | 앱, libDmpConfig, libDmpFeature, libInitialObject, PowerService, TimerService, DisplayService, UpdateService, TunerPlayerService, Bt* |
| DisplayService | libDisplayService.so | 동일 | 앱 문자열 |
| UpdateService | libUpdateService.so | 동일 | 정적 소비자 없음 |
| UsbHostConnectionService | libUsbHostConnectionService.so | 동일 | 정적 소비자 없음 |
| UsbDeviceConnectionService | libUsbDeviceConnectionService.so | 동일 | 정적 소비자 없음 |
| UsbDeviceAudioPlayerService | libUsbDeviceAudioPlayerService.so | 동일 | 앱 문자열 |
| MediaStoreService | libMediaStoreService.so | libMediaStoreServiceClient.so | 앱(클라이언트), PlayerService(서버 lib 직접 링크) |
| PlayerService | libPlayerService.so | libPlayerServiceClient.so, libPlayerServiceClientUtil.so | 앱 |
| OneTrackPlayerService | libOneTrackPlayerService.so | libOneTrackPlayerServiceClient.so | 앱 |
| SoundServiceFw | libSoundServiceFw.so | libSoundService.so, libSoundServiceSettingsDmp.so, libNcAsmService.so | libSoundService: PlayerService, OneTrackPlayerService, AudioAnalyzerService, AudioInPlayerService, BtPlayerService, UsbDeviceAudioPlayerService. 앱: SoundServiceSettingsDmp, NcAsmService |
| NcServiceFw | libNcServiceFw.so | libNcService.so, libNcAsmService.so | libNcAsmService: 앱, SoundServiceFw. libNcService.so는 정적 소비자 없음 |
| VolumeServiceFw | libVolumeServiceFw.so | libVolumeService.so | 앱, PlayerService, OneTrackPlayerService, SoundServiceFw |
| VolumeGlue | libVolumeGlue.so | 동일 | 정적 소비자 없음 |
| RecorderService | libRecorderService.so | libRecorderServiceClient.so, libAudioInRecorderServiceClient.so | 앱 (AudioInRecorderServiceClient 경유) |
| AudioAnalyzerService | libAudioAnalyzerService.so | libAudioAnalyzerServiceClient.so | 앱 |
| ConnMgrServiceFw | libConnMgrServiceFw.so | libConnMgrService.so | 앱, FuncMgrServiceFw, NcServiceFw, PathMgrServiceFw, StorageMgrServiceFw, UsbMgrServiceFw, VolumeServiceFw, UsbDeviceAudioPlayerService, audiohal-uacalsasingletrack |
| FuncMgrServiceFw | libFuncMgrServiceFw.so | libFuncMgrService.so | 앱, ConnMgrServiceFw, NcServiceFw, AudioAnalyzerService |
| PathMgrServiceFw | libPathMgrServiceFw.so | libPathMgrService.so | 앱, FuncMgrServiceFw, NcServiceFw, SoundServiceFw, VolumeServiceFw, RemoteCmdInjCtrlDaemon |
| PowerMgrServiceFw | libPowerMgrServiceFw.so | libPowerMgrServiceClient.so | 앱, audiohal-adleralsa, audiohal-uacalsasingletrack |
| RemoteCmdInjCtrlDaemon | libRemoteCmdInjCtrlDaemon.so | 동일 | 정적 소비자 없음 |
| UsbMgrServiceFw | libUsbMgrServiceFw.so | libUsbMgrService.so | 앱, FuncMgrServiceFw |
| StorageMgrServiceFw | libStorageMgrServiceFw.so | libStorageMgrService.so | 앱 |
| WiredHpServiceFw | libWiredHpServiceFw.so | libWiredHpService.so | ConnMgrServiceFw |
| NfcService | libNfcService.so | 동일 | 앱 문자열 |
| MemmonService | libMemmonService.so | 동일 | 정적 소비자 없음 |
| BtCommonService | libBtCommonService.so | 동일 (하위: libBtCompIf, libBtMw) | 앱 문자열 |
| BtTransmitterService | libBtTransmitterService.so | 동일 | 정적 소비자 없음 |
| BtBleCommonService | libBtBleCommonService.so | 동일 (하위: libBleFunctionHandler) | 정적 소비자 없음 |
| BtBleRemoteService | libBtBleRemoteService.so | 동일 | 정적 소비자 없음 |
| BtPlayerService | libBtPlayerService.so | 동일 | 앱 문자열 |
| AudioInPlayerService | libAudioInPlayerService.so | 동일 | 정적 소비자 없음 |
| TunerPlayerService | libTunerPlayerService.so | 동일 | 앱 문자열 |

HgrmMediaPlayerApp가 정적으로 NEEDED하는 Sony 라이브러리: PlayerServiceClient(+Util), MediaStoreServiceClient, VolumeService, AudioInRecorderServiceClient, OneTrackPlayerServiceClient, PowerMgrServiceClient, ConnMgrService, PathMgrService, StorageMgrService, ConfigurationService, EffectCtrlDmp, NcAsmService, SoundServiceSettingsDmp, FuncMgrService, AudioAnalyzerServiceClient, json, ScreenshotShared, UsbMgrService, DmpConfig, RecFileEditUtil, FuncArchUtils, easelcore, easelqt, pstcore (VS bin/HgrmMediaPlayerApp `readelf -d`).

### 2.5 클라이언트가 서비스를 찾는 방법

1. 호출자가 `pst::core::Framework::GetServiceClient(const std::string& name)`를 부른다(앱의 import 심볼, 앱 `nm`). `name`은 라이브러리 기본 이름에서 `lib`와 `.so`를 뗀 것이다(`StartForDaemon`의 `lib%s.so` 규칙과 동일).
2. `Framework`는 이미 로드된 라이브러리를 먼저 확인하고, 없으면 `FindServiceLibrary` -> `ServiceLibrary::CreateInstance`로 `dlopen`한다. `dlopen`이 libpstcore.so 안에서 일어나므로 그 RUNPATH(`/system/vendor/sony/lib`)가 검색 경로이다(glibc 규칙, 일반 지식).
3. `...%sClientFactory14CreateInstanceEv` 심볼로 클라이언트 객체를 만든다. 서버 binder 핸들을 얻는 방식(마스터 ServiceManager에 이름으로 조회)이 뒤따른다. 핸들 조회의 트랜잭션 코드는 미확인이다.
4. 정적 NEEDED로 링크한 고수준 래퍼(`PlayerServiceClient` 등)도 내부에서 같은 `GetServiceClient`를 호출한다(추정).
5. 서버 쪽은 `Framework::StartForDaemon` -> `ServiceManager::AddService`로 등록하고, sub 데몬은 `RegisterSub`로 마스터에 자신을 알린다.

일반 프로세스(앱이 아닌 임의 도구)가 Framework에 등록 없이 클라이언트만 얻을 수 있는지는 미확인이다. 앱은 `StartForApplication`으로 `[app]` sub-SM이 된다.

### 2.6 계약의 세 가지 유형 (교체 난이도 관점)

1. 생성된 Fw 서비스: 클라이언트 클래스(`XFwClient`)와 `ReqMsg/RspMsg`가 export돼 있어 심볼에서 시그니처를 복원할 수 있다. 가장 쉽다.
2. 헤더 전용 `I*Service`: export 메서드가 없다(2.3절의 11개). vtable 배치를 바이너리에서 복원해야 한다. 가장 어렵다.
3. 고수준 래퍼: 서버 상태를 클라이언트가 소유한다. 예: PlayerService의 트랙 시퀀스는 클라이언트가 구현하고 서버가 콜백으로 요청하는 것으로 보인다(`SetTrackSequence`, `OnNextTrack`; 추정). MediaStoreService는 앱이 `SetConfig`로 DB 경로와 스토리지 정의를 주입한다(3.6절).

---

## 3. UI 스택

### 3.1 HgrmMediaPlayerApp

- `VS bin/HgrmMediaPlayerApp`, 9MB, stripped. RPATH `$ORIGIN/../lib:/system/vendor/sony/lib:<빌드 서버 경로>`(readelf -d).
- 소스 경로 흔적: `dmp/app/HgrmMediaPlayer/src/HagoromoApp.cpp`. 진입 QML: `qrc:/framework/FrameWork.qml`(strings).
- NEEDED: 2.4절의 Sony 라이브러리 + Qt5 Quick/Qml/Widgets/Gui/Core(+Network, QuickWidgets, QuickParticles는 라이브러리로 존재).
- `easel::QtApplication` 위에서 동작하고, 앱 수명주기는 appmgrservice가 `Foreground/Background/Terminate`로 지시한다(VS lib/libeaselcore.so, libappmgrservice.so).

### 3.2 Qt 구성

| 항목 | 내용 | 근거 |
|---|---|---|
| 버전/빌드 | Qt 5.3.2, mkspec `devices/linux-mt8590-clang-libc++` | VS lib/libQt5Core.so.5, plugins/platforms/libqeglfs.so |
| prefix | `/system/vendor/sony` (plugins/, qml/, translations/) | VS 트리 |
| 모듈 | Core, Gui, Network, Qml, Quick, QuickParticles, QuickWidgets, Widgets; icu 61, harfbuzz | VS lib/ |
| 플랫폼 플러그인 | `plugins/platforms/libqeglfs.so`만 존재 | VS plugins/platforms/ |
| 이미지 포맷 | `plugins/imageformats/libqjpeg.so` (PNG는 Qt 내장) | VS plugins/imageformats/ |
| 입력기 | `platforminputcontexts/{libinputcontextproxyplugin,libsw12keyboardplugin,libPinyinSwkb,libCangjieSwkb}.so` | VS plugins/platforminputcontexts/ |
| QML import | QtQuick.2, Controls, Dialogs, Layouts, Particles.2, Window.2, PrivateWidgets, QtGraphicalEffects, QtQml/Models.2 | VS qml/, 앱 strings |
| 번역 | `HgrmMediaPlayerApp_*.qm` 13종 (de_DE, en_US, es_ES, fr_FR, it_IT, ja_JP, ko_KR, pl_PL, pr_PR, ru_RU, tr_TR, zh_CN, zh_HK) | VS translations/ |
| 사전/DB | `etc/dic/`, `db/ImePinYin.db`, `db/PinyinSort.db` | VS etc/, VS db/ |

### 3.3 플랫폼 플러그인과 환경변수

- `libQt5Gui.so.5`는 기본 플랫폼을 `eglfs`로 하드코딩한다(strings). rc에는 `QT_QPA_PLATFORM`이 없다. 앱이 코드에서 지정하는지는 미확인이다.
- `libqeglfs.so`에 `QEglFSmt8590Hooks`가 있다(nm). EGL fbdev 방식이고 evdev 터치/키보드/마우스 핸들러가 내장돼 있다.
- init 단계에서 export되는 값(RD init.hagoromo.rc:1-8, RD init.rc:36):

| 변수 | 값 | 의도 |
|---|---|---|
| `QML_USE_GLYPHCACHE_WORKAROUND` | 1 | 글리프 캐시 우회 (이름 기준) |
| `QT_QPA_EGLFS_HIDECURSOR` | 1 | 커서 숨김 |
| `QT_QPA_EVDEV_MOUSE_PARAMETERS` | `/dev/input/piyo` | 존재하지 않는 경로 -> 마우스 비활성 (추정) |
| `QT_QPA_EVDEV_KEYBOARD_PARAMETERS` | `/dev/input/hoge` | 존재하지 않는 경로 -> 키보드 비활성 (추정) |
| `QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS` | `/dev/input/event1` | 터치 장치 |
| `QT_JPEG_DISABLE_DECODE_PROGRESSIVE` | 1 | 프로그레시브 JPEG 디코드 끔 |
| `QT_PNG_DISABLE_DECODE_INTERLACE` | 1 | 인터레이스 PNG 디코드 끔 |
| `QT_IM_MODULE` | `inputcontextproxy` | 입력기 프록시 |
| `QT_PLUGIN_PATH` | 앱이 설정하는 것으로 보임 (rc에 없음) | 플러그인 경로 (추정) |

이 환경은 init에서 export돼 hagodaemon을 거쳐 appmgrservice가 fork하는 자식에게 상속된다(추정: init 환경은 자식에게 상속되는 표준 동작). 환경을 바꾸려면 램디스크 rc(boot.img)를 건드려야 한다.

### 3.4 그래픽 스택

- GPU 라이브러리: `SYS lib/libMali_linux.so`("mali450-r0p0-gles20-gles11-linux" 문자열). `libEGL.so`, `libGLESv1_CM.so`, `libGLESv2.so`는 이 파일의 심볼릭 링크이다(SYS lib/ 링크 목록). 장치 `/dev/mali`; 보조 `libion`, `libion_mtk`, `libm4u`.
- `/system/lib/hw`가 없다. 즉 gralloc/hwcomposer가 없는 fbdev "linux" 플레이버이다(SYS lib/ 디렉터리 목록).
- 디스플레이는 800x480이다(VS etc/default-capability_*.xml, model `NW-A50Series`, firmwareversion 1.02). `/dev/graphics/fb0`의 실제 해상도·bpp·회전은 미확인이다.
- 부트 애니메이션 `icx_bootanimation`이 fb0에 직접 그린다(RGB565 BMP 프레임을 BGRA32로 변환; 800x480). 앱이 뜨면 `easel::ApplicationBase::StopBootAnimation`이 `/xbin/killall`과 `/system/xbin/icx_bootanimation`을 이용해 끈다(strings 참조). 이 시점의 fb 소유권 인계 방식은 미확인이다.
- `/system/media/resumeanimation.zip`을 `StartResumeAnimation`이 참조하나 SYS media에는 `bootanimation.zip`(29MB)만 있다. 누락 시 동작은 미확인이다.

### 3.5 리소스 위치

| 종류 | 위치 | 근거 |
|---|---|---|
| QML 소스 (~300개) | 앱 바이너리의 qrc. `.rodata`에 평문(`import QtQuick` 244회). `qrc:/framework/FrameWork.qml` | VS bin/HgrmMediaPlayerApp strings |
| 내장 이미지 | `qrc:/assets/images/...` | 앱 strings |
| 외장 이미지 | `/opt2/appresouce/` (option2 파티션; 철자는 원문 그대로) | 앱 strings |
| 시스템 사운드 | `/opt2/systemsound/WM_BEEP_*.wav` | 앱 strings |
| 번역 | `/system/vendor/sony/translations/*.qm` | VS translations/ |
| 폰트 | `/system/vendor/sony/lib/fonts/` | VS lib/fonts |
| 기본 capability | `/system/vendor/sony/etc/default-capability_{j,w}_{internal,external0}.xml`, `capability_00.xml` | VS etc/ |
| 오디오 효과 파라미터 | `/system/vendor/sony/etc/effector_params`, `audioanalyzer_params` | VS etc/ |

`/opt2` 내용물은 이 추출본에 없다(option2 파티션 미추출). QML은 평문이므로 문자열 추출로 구조를 볼 수 있다. 파일 경계(qrc 디렉터리 트리)의 복원 방법은 아직 시험하지 않았다.

### 3.6 폰트

- 파일: SST(Bold/Light/Roman), SSTUI(Bold/Light/Roman), SSTJpPro(Bold/Light/Regular), NotoSansKR(Bold/Light/Regular), NotoSansThai(Bold/Regular), DF 계열(`DFGothicPW5-BIG5HK-SONY-20131004part.ttf`, `DFHeiW5-A-SONY-CTC.ttf`, `DFPGothicPW5-BIG5HK-SONY-20140613.ttf`)(VS lib/fonts).
- 앱이 쓰는 패밀리 이름: "SST UI", "SST Japanese Pro", "SSTCJK", "DFGothicPW5-BIG5HK-SONY"(앱 strings).
- Qt는 `QPlatformFontDatabase::fontDir`와 `QT_QPA_FONTDIR`을 참조한다(libQt5Gui strings). 폰트 디렉터리를 어떻게 지정하는지(앱 코드 vs 기본값 `<prefix>/lib/fonts`)는 확인하지 못했다. prefix가 `/system/vendor/sony`이므로 기본값이 `VS lib/fonts`일 가능성이 높다(추정).
- `SYS etc/fonts/fonts.conf`는 `/usr/share/fonts`와 `/data/chrome/fontconfig`를 가리키지만 `SYS usr/share`에는 `alsa`, `audio_dac`, `diagnosis`, `zoneinfo`뿐이어서 Chromecast 잔재로 보인다(추정).

### 3.7 입력

- 터치: Qt evdev 핸들러가 `/dev/input/event1`을 읽는다(위 환경변수). 터치 IC는 4.2절.
- 하드웨어 키: Qt evdev 키보드는 존재하지 않는 경로로 꺼져 있으므로 키는 `KeyService`(hagoromo6)가 `/dev/input/event*`를 읽어 제공한다(추정). `libKeyListener.so`는 `key_packet_t`를 Qt 키 이벤트로 바꾸는 코드이나 이미지 안에 참조자가 없다. 앱이 동등한 코드를 내장하는 것으로 본다(추정). 앱의 키 수신 경로는 미확인이다.
- 소프트웨어 키보드: `QT_IM_MODULE=inputcontextproxy` + `libsw12keyboardplugin`, `libPinyinSwkb`, `libCangjieSwkb`.

### 3.8 기동과 설정 (appcfg, ScreenSettings.json)

- `VS bin/HgrmMediaPlayerApp.appcfg`: `name: HgrmMediaPlayerApp`, `command: HgrmMediaPlayerApp`, `type: Home`, `hidden: false`.
- `appmgrservice`는 `/system/vendor/sony/bin`의 `*.appcfg`를 스캔한다(필드: name, command, type, hidden). `fork`/`execvp`로 띄우고 `LifeCycleManager`로 Foreground/Background/Terminate를 관리한다. 오류 문자열: "Home app not found.", "TIMEOUT: Wait for '%s' to be foreground", "Failed to make %s foreground ... Abort..", "ApplicationBase::OnSuspend timeout! Abort the process..."(libappmgrservice.so strings).
- `VS bin/ScreenSettings.json`(127KB, 루트 키 `TestModel`): window(10), screen(148), startup, transition(113), view(17), popup(57), convert, alias, style, effect 섹션. 앱에 ViewModel 클래스 이름이 약 63개 있다(앱 strings). 화면 ↔ 뷰모델 ↔ QML 연결 표로 보인다(추정).
- 앱은 `VolumeSettings.json`, `AudioAnalyzerSettings.json`도 참조한다. 위치는 미확인이다(앱 strings).
- 앱은 MediaStoreService에 `SetConfig`를 호출한다. 인자에 `/db/MTPDB.dat`, `/data/mnt/`(internal, external0, external1, limited), 디렉터리 MUSIC, LEARNING, NWWM_REC, WALKMAN, SD_CARD가 있다(앱 strings). `/system/vendor/sony/bin/prepare_contentroot.sh`도 참조한다(앱 strings). 이 스크립트가 `/data/mnt` 링크를 만든다(VS bin/prepare_contentroot.sh).
- 모델 구분은 `libDmpFeature.so`/`libInitialObject.so`의 DeviceInfo(출하지 J/W 등)와 capability XML이 맡는다(nm).

### 3.9 스크린샷 도구

`VS bin/screenshot`(11KB)와 `libScreenshotShared.so`는 `/dev/graphics/fb0`를 mmap(`FBIOGET_FSCREENINFO`/`FBIOGET_VSCREENINFO`)하고 `QImage::save`로 `/contents/screenshot_YYYYMMDD_hhmmssmmm.png`를 쓴다. 환경변수 `SCREENSHOT_DEBUG`가 있다(strings). 앱이 `libScreenshotShared.so`를 NEEDED로 가지므로 앱 내부에서도 호출된다. 호출 조건(키 조합 등)은 미확인이다. fb0를 읽기 때문에 GL 합성 결과가 fb0에 그대로 있는지는 장비에서 확인해야 한다.

### 3.10 부트 애니메이션 -> 앱 인계

`icx_bootanimation`은 `/system/media/bootanimation.zip`(`desc.txt`: `1 0 0`/`1 0 59`/`0 0 89`, `part0`/`part1`의 BMP)을 fb0에 그린다. 이어서 앱이 `ApplicationBase::StopBootAnimation`으로 정지시킨다. 정지 신호 방식과 애니메이션-앱 사이 화면 깜박임은 미확인이다.

---

## 4. 하드웨어 인터페이스

공통: 커널 모듈은 `SYS lib/modules/`의 12개이다: `cxd3778gf_dnc_core`, `gspca_main`, `icx_carddet`, `icx_nvp_emmc`, `icx_usbconn`, `icx_usbscsi`, `lcd`, `mtk_stp_bt_soc`, `mtk_stp_wmt_soc`, `mtk_wmt_detect`, `radio-si4708icx`, `wm_key`. 나머지는 vmlinux 내장이다(K kconfig.txt). `MODULE_SIG`, `KEXEC`, `DM_VERITY`는 없다(ANA).

### 4.1 디스플레이 / fb / GPU

| 항목 | 내용 | 근거 |
|---|---|---|
| fb 장치 | `/dev/graphics/fb0` (mtkfb) | VS bin/screenshot, SYS xbin/icx_bootanimation, K kconfig.txt `MTK_FB` |
| 해상도 | 800x480 (capability XML). fb0 실측은 미확인 | VS etc/default-capability_*.xml |
| 백라이트 | `/sys/class/leds/lcd-backlight/brightness`(0666), 드라이버 `BACKLIGHT_ICX_LM3630`. 점진 점등 `hgrm_setLcdValidateGradually.sh` | RD init.hagoromo.rc:21, K kconfig.txt, VS etc/ |
| LED | `/sys/class/leds/{red,green,blue,dtb:gpio:LED_*}` (DisplayService `SetLedBlink`) | VS lib/libDisplayService.so strings |
| GPU | Mali-450 (`/dev/mali`), ion(`libion`, `libion_mtk`), m4u | SYS lib/libMali_linux.so, K kconfig.txt `ION_MTK` |
| 표시 처리 | `dpfd`가 `/dev/mtk_disp`, `libdpframework`(`/proc/mtk_mdp_cmdq`)를 사용. MTK 디스플레이 처리 보조 데몬(추정) | SYS bin/dpfd strings |
| 모듈 | `lcd.ko` - 역할 미확인 | SYS lib/modules/ |
| 보조 도구 | `fwfb`, `test_fb`, `egl_test`, `vvt`(`/dev/video0` 접근) | SYS bin/, SYS xbin/ |
| 장치 노드 | `/dev/ebc`, `/dev/vsw`, `/dev/hdmitx`, `/dev/rcs730`, `/dev/video0`는 ueventd에 정의됐으나 사용처 불명 | RD ueventd*.rc |

### 4.2 터치 / 키 / HOLD 스위치

| 항목 | 내용 | 근거 |
|---|---|---|
| 터치 입력 | `/dev/input/event1` | RD init.hagoromo.rc:6 |
| 터치 IC | 커널에 `TOUCHSCREEN_HIMAX_HX8526_ICX`와 `SYNAPTICS_ICX` 두 드라이버가 설정돼 있다. NVP `syi` 하위 3비트=3(ICX1291)일 때 Himax 경로(`/proc/himax_touch/debug`)로 펌웨어를 올리므로 이 변형은 Himax이다. Synaptics는 다른 `syi` 값용으로 보인다(추정). DisplayService는 I2C `mt-i2c.1/i2c-1/1-0020/sleep`과 `1-0048/sleep`을 쓴다(어느 쪽이 어느 IC인지 미확인) | K kconfig.txt, SYS usr/bin/sony_tovis_updater.sh, VS lib/libDisplayService.so strings |
| 터치 디버그/펌웨어 | `/proc/himax_touch/debug`; 펌웨어 `SYS etc/firmware/Sony_Tovis_ICX1291_C21_2017-02-09.bin`, 갱신 스크립트 `SYS usr/bin/sony_tovis_updater.sh` (1.4절) | SYS etc/firmware, SYS usr/bin |
| 터치 캘리브레이션 | `tpd_calmat` (nvram_daemon) | SYS bin/nvram_daemon strings |
| 터치 유효화 | `DisplayService::SetTouchPanelValidate` | VS lib/libDisplayService.so nm |
| 키 드라이버 | `wm_key.ko`, `eint_num_key_wake=6` | SYS bin/load_sony_driver |
| 키 상태 | `/sys/module/wm_key/parameters/{hold_key_value,nc_key_value}` | VS lib/libKeyService.so strings |
| 키 입력 | `/dev/input/event*` (KeyService), `SetSingleKeyRepeat` | libKeyService.so |
| HOLD | `hold_key_value` 파라미터로 노출 (HOLD가 wm_key 소관이라는 건 추정) | 위 근거 |
| 키 도구 | `icx_key`, `ad_key_ioctl`, `getevent`, `sendevent`, `test_ts` | SYS xbin/, SYS bin/ |

### 4.3 오디오

| 항목 | 내용 | 근거 |
|---|---|---|
| ALSA 기본 | `pcm.!default` = card 0 device 4, `ctl.!default` = card 0 (확정 사항) | SYS etc/asound.conf |
| HAL이 여는 장치 | `hw:0,0` ~ `hw:0,4` | VS lib/libaudiohal-*.so strings |
| 오디오 HAL 플러그인 | `adleralsa`, `dualtrackmixalsa`, `genericalsa`, `uacalsasingletrack`, `a2dpsnksingletrack`, `analyzer`, `listener` | VS lib/libaudiohal-*.so |
| ALSA 라이브러리 | `libasound.so`; 도구 `aplay`, `amixer`, `alsactl`, `speaker-test`, `iecset` | SYS lib/, SYS bin/ |
| 코덱 | CXD3778GF (K `SND_SOC_CXD3778GF`, `SND_SOC_ICX_AUDIO_MOBILE_NEXT`) | K kconfig.txt |
| 코덱 펌웨어 | `SYS etc/firmware/cxd3778gf_ucom_fw_00_16.bin`, 갱신 `SYS usr/bin/cxd3778gf_ucom_fwupdate.sh`(+`cxd3778gf_ucom_fwerase.sh`); I2C 버전 `i2c-1/1-004e/fw_version`, 갱신 `i2c-0/0-0010/fwupdate` (1.4절) | SYS etc/firmware, SYS usr/bin |
| 코덱 sysfs/param | `/sys/module/snd_soc_cxd3778gf/parameters/timed_mute_ms`(0666); NC 스위치 `/sys/class/switch/cxd3778gf_ucom/state`; ALSA 믹서에 마스터 볼륨, 헤드폰 앰프, 잭 모드, `deep_early_suspend` 등 | RD init.hagoromo.rc:36, VS lib/libNcServiceFw.so, libPathMgrServiceFw.so |
| SoC 오디오 | `mt8590-i2s-out*`, DSD 인코더(`dsdenc`), ASRC `/dev/asrc1..5`, `/dev/audrtc`, `/dev/audacc`; 파라미터 `mt8590_audio` `interrupt_delay_us`; 옵션 `MTK_CM4_USE_HIGH_FREQ_ASRC` | K vmlinux.bin strings, K kconfig.txt, libAudioInPlayerService.so |
| 튜닝 테이블 | `dacdat`가 `/system/usr/share/audio_dac`의 테이블을 `/proc/icx_audio_cxd3778gf_data`, `/proc/icx_audio_dnc_data`에 기록 | SYS bin/load_sony_driver, SYS bin/dacdat |
| CM4 | 파티션 `cm4`, `UPG/file7-cm4.bin`(24KB, "[CM4]DMA Module" 문자열). 적재 경로 미확인 | UPG file0-update.sh, file7-cm4.bin |
| 경로 전환 | PathMgrServiceFw가 amixer 제어를 건드린다: "analog input device", "output device", "headphone amp". 장치 이름: LineIn, BtlHeadphone, SeHeadphone, LineOut, UacDevice, UacHost | VS lib/libPathMgrServiceFw.so strings |
| 대기 전환 | `PaSleepPrimaryAudioDevice`/`PaWakePrimaryAudioDevice`가 믹서 "standby" 제어를 조작. 호출자 미확인 | VS bin/Pa*PrimaryAudioDevice |
| ASRC 유틸 | `libmtkmemasrc`가 `/dev/asrc*` 사용, 도구 `memasrc_wavconv` | SYS lib/, SYS xbin/ |
| 재생 엔진 | SoundServiceFw(3.3MB)의 소프트웨어 DSP: DSEE AI/ClearAudio, ClearPhase, EQ, 바이닐, MQA, DSD->PCM | VS lib/libSoundServiceFw.so strings |
| 볼륨 | VolumeServiceFw: 마스터 볼륨, AVLS, SE/BTL 게인, 리미터 | libVolumeServiceFw.so strings |
| 효과 파라미터 | `/system/vendor/sony/etc/effector_params` | VS etc/ |

오디오 출력 경로는 "앱 -> PlayerService -> SoundService(공유 메모리) -> SoundServiceFw -> audiohal-adleralsa(`hw:0,x`) -> CXD3778GF"로 이어진다(추정, 라이브러리 NEEDED 관계에서 도출).

### 4.4 전원 / 배터리 / 서스펜드

| 항목 | 내용 | 근거 |
|---|---|---|
| 서스펜드 | `echo mem > /sys/power/state`, `wake_lock`/`wake_unlock`, `dpidle_state` (모두 0666) | RD init.hagoromo.rc:16-33, VS lib/libPowerService.so strings |
| 커널 기능 | `EARLYSUSPEND`, `WAKELOCK` | K kconfig.txt |
| PM 도우미 | `/sys/devices/platform/icx_pm_helper/{boot_powerkey,eint_sta,resume_lock_cancel,spm_r12}` 와 모듈 파라미터 `/sys/module/icx_pm_helper/parameters/{icx_bid2,icx_bid3,icx_modelid,icx_ship,icx_sysinfo,icx_boot_option,icx_boot_reason}` | libPowerService.so strings, K kconfig.txt `ICX_PM_HELPER` |
| 슬립 모드 | `/sys/module/mt_sleep/parameters/slp_mode` | RD init.hagoromo.rc:20 |
| 배터리/충전 | `power_supply` battery/dc/usb 노드; 충전 IC I2C `mt-i2c.2/i2c-2/2-006b/power_supply/usb`; "Itawari charging" 문자열(`libPowerService.so`, `libPowerMgrServiceFw.so`, `libPowerMgrServiceClient.so`; 배터리를 아끼는 충전 모드로 보이나 추정, 의미 미확인) | libPowerService.so, libPowerMgrServiceFw.so strings |
| 전원 요인 | PowerStatus, BootFactor, ResumeFactor, FactoryReset, 부분 기상(PartialWakeup), CPU 개수 기준 잠금 | libPowerMgrServiceFw.so strings |
| RTC 기상 | `/dev/rtc0`, `/sys/devices/platform/mt-rtc/rtc/rtc0/wakealarm`, `mt-rtc/power/wakeup`; 도구 `rtcwake` | RD init.hagoromo.rc:31-32, libTimerService.so |
| CPU | cpufreq hotplug: `cpu_up_avg_times`, `cpu_up_threshold`, `cpu_num_base`, `scaling_min_freq` | RD init.rc:251-253, init.hagoromo.rc:34-35 |
| 워치독 | `wdt_ctrl 30`, `/proc/wdk` | RD init.rc:129 |
| NVRAM 데몬 | `nvram_daemon` + `/data/nvram/*`, `/dev/mtk-adc-cali` | SYS bin/nvram_daemon strings |
| autok | `autokd` + `/sys/autok/*` (MTK 메모리 카드 인터페이스 튜닝 계열로 보임, 추정) | SYS bin/autokd, RD init.rc:62-72 |
| EEPROM | `/sys/bus/i2c/devices/2-0050/eeprom` | SYS strings |
| 종료 경로 | 서비스 `Poweroff`/`Reboot` 호출 + `android_reboot` (1.8절) | libpstcore.so |

배터리 용량·게이지 IC·충전 제어의 세부는 미확인이다.

### 4.5 스토리지

| 항목 | 내용 | 근거 |
|---|---|---|
| 내장 eMMC | `/dev/block/mmcblk0p*`, `/emmc@<name>` 심볼릭 링크는 커널 dumchar가 만든다 | UPG file0-update.sh, K vmlinux.bin strings(`/emmc@%s`, `emmc_create_sys_symlink`) |
| microSD | 카드 감지 `icx_carddet.ko`, `/dev/icx_carddet`; 장치 `mmcblk1`/`mmcblk2`; 마운트 `/contents_ext`, `/contents_ext1` | SYS bin/load_sony_driver, RD init.hagoromo.rc:27-30, libStorageMgrServiceFw.so |
| 내장 콘텐츠 | `md0` 선형 MD(`mkmdlinear`, `linear_contents`), vfat `/contents` | SYS bin/linear_contents, mount_partition |
| exFAT | `exfatfuse`, `mount.exfat`, `mkexfatfs`, `exfatfsck`; FUSE 커널 지원 | SYS bin/, SYS etc/exfatfuse.conf, K kconfig.txt |
| 파일시스템 | EXT4, VFAT, MD_LINEAR, DM_CRYPT, ZRAM | K kconfig.txt |
| 포맷 도구 | `StorageFormatterDmp`, `format_partition`, `format_contents*`, `make_ext4fs`, `mkfs.vfat`, `newfs_msdos` | VS bin/, SYS bin/ |
| LUN | `f_mass_storage/lun*/file` (USB 대용량 저장) | RD init.hagoromo.rc:26, libStorageMgrServiceFw.so |
| 공장 초기화 | `VS bin/factoryresetdmp.sh` (5.5절) | VS bin/ |

### 4.6 USB 모드

| 항목 | 내용 | 근거 |
|---|---|---|
| 가젯 | `USB_G_ANDROID` (`android_usb`), MTK USB HDRC OTG | K kconfig.txt |
| VID/PID | VID `054C`; adb `0B8B`, uac(`audio_func,adb`) `0B8C`, msc(`mass_storage,adb`) `0B8D` | RD init.usbcfg.rc |
| 전환 속성 | `sys.sony.config` = root / unroot / adb / uac / msc. 전환 때마다 adbd를 stop/start | RD init.usbcfg.rc, init.usbcfgdmp.rc |
| UAC 피드백 | `f_audio_func/{f_allow,f_minus,f_plus,f_start,f_thresh,f_valid}` | VS lib/libUsbDeviceAudioPlayerService.so strings |
| OTG 모드 | `/sys/devices/platform/mt_usb/mode` (0666) | RD init.hagoromo.rc:38 |
| 연결 장치 | `/dev/usbconn`(`libUsbDeviceConnectionService.so`, `SYS xbin/usbconn_test`), `/sys/class/switch/usb0_suspend_state`(`libUsbDeviceConnectionService.so`), `usb_overcurrent`(`libUsbHostConnectionService.so`), `/dev/icx_usbscsi`(벤더 SCSI; `dbmn` 문자열이 `K vmlinux.bin`에 1건 있어 펌웨어 업그레이드 명령으로 보이나(추정) 의미 미확인) | 각 라이브러리 strings, K vmlinux.bin strings |
| USB 라이브러리 | `libusbaudio`, `libusbhost` | SYS lib/ |
| adb 스위치 (확정 사항) | `persist.sys.sony.icx.adb` = "1"/"0". `AdbEnabler`/`AdbDisabler`(VS bin/), `libDmpConfig.so`의 `AdbEnablerImpl`, `UsbMgrServiceFw::SetAdbEnabled/GetAdbEnabled`. adbd 게이트는 `/dev/android_adb_enable` | VS bin/, ANA |
| 기본 속성 | `persist.sys.usb.config=adb`, `ro.secure=0`, `ro.debuggable=1` | RD default.prop |
| 문자열 | iManufacturer "Sony", iProduct "WALKMAN", iSerial = `set_iserial.sh` | RD init.rc:175-176, SYS bin/set_iserial.sh |

### 4.7 블루투스

- 스택은 MTK BlueAngel이다: `mtkbt` 데몬(SYS bin/mtkbt)과 `libbluetooth.blueangel.so`. BlueDroid가 아니다.
- 연결 칩: 커널 설정은 SoC 내장 `MTK_COMBO` `CONSYS_8590`(K kconfig.txt). 모듈 `mtk_wmt_detect.ko`, `mtk_stp_wmt_soc.ko`, `mtk_stp_bt_soc.ko`. 로더 `wmt_loader`, `6620_launcherA/C`. 펌웨어 `ROMv2_lm_patch_1_0_hdr.bin`, `ROMv2_lm_patch_1_1_hdr.bin`, `WMT_SOC.cfg`(SYS etc/firmware). 다만 `SYS etc/MT6625L_SingleSKU_00{0,1}.dat`가 있어 MT6625L 계열 이름이 보이나, 실제 칩 구성은 미확인이다.
- 장치 노드: `/dev/stpwmt`(c 190), `/dev/stpgps`(191), `/dev/stpbt`(192), `/dev/wmtdetect`, `/dev/ttyMT2`(BT/FM/GPS 공용)(RD init.project.rc).
- 소켓: `/dev/socket/bt.int.adp`, `/tmp/bt.*`(libBtMw.so strings). 주소 `/data/nvram/APCFG/APRDEB/BT_Addr`(+기본값 `SYS etc/BT_Addr_ForWM_Default`). DB `/data/Bluetooth/devdb`. uinput 사용.
- 코덱: SBC, Fraunhofer AAC, LDAC(`libldacBTBC.so`), aptX, aptX HD가 `libbluetooth.blueangel.so`의 NEEDED이고, BtMw에 `btmwAvBothSetLdacEnable` 등이 있다(strings/readelf).
- 상위 계층: `libBtMw.so`, `libBtCompIf.so`, `libBleFunctionHandler.so` -> hagoromo27의 Bt* 5개 서비스.
- 도구: `btut`, `bt_drv`, `hci_cmd`(SYS bin/).
- Wi-Fi는 이 이미지에서 쓰이지 않는다(`WIFI_RAM_CODE_8590` 펌웨어는 있으나 wlan 모듈 없음).

### 4.8 NFC

`NFC_CXD22XX`(K kconfig.txt), 장치 `/dev/cxd224x-i2c`, `libnfc-nci`(SYS etc/libnfc-nci.conf), 서비스 NfcService(hagoromo25, user system). 펌웨어 `SYS etc/firmware/cxd225x_firmware.bin`, 설정 `cxd225x_config.txt`, 부팅 시 갱신 `SYS usr/bin/nfc_cxd225x_updater.sh`(1.4절). 도구: `cxd225x_fwupdater`, `cxd224x_test`, `test_libnfc`, `test_nfc_sequencer`(SYS bin/).

### 4.9 FM 라디오

`RADIO_SI470X` + `radio-si4708icx.ko`(deemphasis 750은 `shp` 0x1/0x306일 때), 장치 `/dev/radio`(TunerPlayerService), 도구 `fm_tuner`(SYS xbin/). 안테나는 헤드폰 케이블일 것이다(추정).

### 4.10 기타

- NVP: `icx_nvp_emmc.ko`가 eMMC 원시 영역을 `/dev/icx_nvp/000`~`092`로 노출한다. 도구 `nvp`, `nvpflag`, `nvpstr`, `nvpinfo`, `nvpnode`, `nvptest`. 플래그 이름: syi, fup(0x70555766 업데이트), prk, hld, mid, tst, gty, bml, fni, sid, mso, ctr, sku, dgs, mac, nvr, ins(0x494E5354), fur, shp, sps, clv. 문자열 키: ubp, ufn, kas, ums, ups, fvi, fpi, ser, se2, bpr, lyr, pwd, clg, pcd. 노드 번호와 이름의 대응은 거의 미확인이다. 알려진 것은 `000`을 init이 기다린다는 점(RD init.rc:257)과, UpdateService가 FUP 플래그 때 `003`/`026`을 쓴다는 점(`libUpdateService.so` strings)뿐이다.
- UART: `/dev/ttyMT1`은 진단 콘솔과 WM-PORT/크래들(`/dev/icx_cradle`)이 공유한다(1.3절).
- I2C 테스트 도구: `i2cdetect`, `i2cdump`, `i2cget`, `i2cset`, `devmem`, `regmon`, `mt_at24_test`(SYS xbin/, SYS bin/). 라이브 기기에서 읽기 전용 조사에 쓸 수 있다.
- 부트로더/플래시 도구: `flash_bootloader`, `flash_image`, `update_fu_image.sh`(SYS bin/)는 쓰기 도구이므로 조사 단계에서 쓰지 않는다.

---

## 5. 데이터와 저장소

### 5.1 파티션 이름

`UPG/file0-update.sh`가 쓰는 이름: uboot, bootimg, recovery, sec_ro, logo, tee1, tee2, android, chrome, cm4, option1, option2, option3, nvp, usrdata, cache, var, db, contents. `/proc/dumchar_info` 형식은 `%-10s 0x%016llx 0x%016llx 2 /dev/block/mmcblk0p%d`이다(UPG file0-update.sh). 이름별 오프셋·크기·번호는 미확인이다(장비의 `/proc/dumchar_info`가 필요). 업데이트 번들 구성: `file2-boot.img`, `file3-secro.bin`, `file4-logo.bin`, `file5-tz.img`, `file6-system.img`, `file7-cm4.bin`, `file8-bgimage.bin`(UPG).

### 5.2 마운트 표 (`mount_partition`, 디스어셈블)

| 마운트 지점 | 소스 | 파일시스템 | 옵션 / 비고 |
|---|---|---|---|
| `/` | `/dev/ram` | 램디스크 (RD) | 부팅마다 새로 풀림 |
| `/system` | `/emmc@android` | ext4 | `noatime ro` (RD init.rc:75) |
| `/data` | `/emmc@usrdata` | ext4 | `discard`; 플래그 0x40c = NOATIME\|NODEV\|NOEXEC로 보임(추정) |
| `/cache` | `/emmc@cache` | (미확인) | 같은 도구 |
| `/var` | `/emmc@var` | (미확인) | icx_syslog가 사용 |
| `/db` | `/emmc@db` | (미확인) | `MTPDB.dat` |
| `/opt1`, `/opt2`, `/opt3` | `/emmc@option1~3` | (미확인) | 마운트 후 읽기 전용으로 remount, root 소유 |
| `/contents` | `/emmc@contents` | vfat | `iocharset=iso8859-1,utf8,umask=000,discard`, 플래그 0x408 |
| `/contents_ext`, `/contents_ext1` | microSD | vfat/exfat (추정) | StorageMgrService가 마운트 (추정) |
| `/tmp` | tmpfs 32m | | RD init.rc:20 |
| `/dev` | tmpfs 4m | | RD init.rc:23 |
| `/dev/shm` | tmpfs 64m | | RD init.rc:42 |
| `/data/mnt/{internal,external,external1,limited}` | 링크/바인드 | | `prepare_contentroot.sh`가 생성 (VS bin/prepare_contentroot.sh) |

`NOEXEC`가 맞다면 `/data`와 `/contents`에서 직접 실행할 수 없다. 임시 테스트용 바이너리 배치는 `/tmp`(noexec 여부 미확인)나 remount가 필요하다. `/system/*` 바이너리 교체는 `/system`을 rw로 remount해야 한다. 이 플래그 해석은 미확인으로 두고 7절에서 검증한다.

### 5.3 DB와 설정 파일

| 경로 | 내용 | 근거 |
|---|---|---|
| `/db/MTPDB.dat` | MediaStoreService DB 경로(앱이 `SetConfig`로 전달) | 앱 strings |
| `/data/genie/willberemoved` | Sony "genie/genesys-db"("loco" 객체)의 삭제 예정 표식 | libMediaStoreService.so strings |
| MediaStore 내부 | SQLite 3.7.6.3 (libcnpysort.so에 내장), 병음 정렬, cue sheet, M3U8 | VS lib/libcnpysort.so, libMediaStoreService.so |
| NVP `/dev/icx_nvp/NNN` | ConfigurationService의 설정 저장 영역. 키는 숫자 (GroupId, KeyId). `StaticIdWithStr`로 문자열 해시 ID가 쓰일 수 있음(추정) | libConfigurationService.so strings |
| DmpConfig 키 이름 | `FNC_BOOT_FUNC`, `FNC_UACHOST_POWER`, `FNC_MSC_AUTOEXPORT`, `SS_*`(DSEE, VPT, EQ6/EQ10, TONE, DCLINEAR, CAPLUS, SRCDIRECT, BTEFFECT 각각 P1~P3 프로파일), `VOL_*`, `NC_*`, `ABD_ONOFF`, `BEEP_ONOFF`, `ASM_*`, `LPCM_PBMODE`, `DSEEAI_ONOFF`, `VINYL_*`, `AULOS_*` | VS lib/libDmpConfig.so strings |
| `ScreenSettings.json` | UI 화면/전환/팝업 정의 | VS bin/ScreenSettings.json |
| capability XML | 모델/재생 가능 포맷 | VS etc/default-capability_*.xml |
| `effector_params`, `audioanalyzer_params` | DSP 효과·분석기 파라미터 (오버라이드 `/contents/audioanalyzer_params/`) | VS etc/, libAudioAnalyzerService.so strings |
| `/data/share/chrome/localtime` | 시간대 (TimerService) | libTimerService.so strings |
| `/data/Bluetooth/devdb` | BT 페어링 DB | libBtMw.so strings |
| `/data/nvram/*` | MTK NVRAM (`APCFG`, `AllFile`, `AllMap`, `BT_Addr`, ...) | nvram_daemon strings |
| `/data/{nfc,key_provisioning,misc/touchpanel,coredump,bootid}` | 각각 NFC, 키 프로비저닝, 터치 패널, 코어 덤프, 부팅 ID | 바이너리 strings (사용처 개별 확인은 안 함) |
| `/var/log/{chagomon,hhagomon,logc,memmon*.csv}`, `SYSTEMLOG`, `HDUMP_LOG`, `buildinfo.txt`, `shagomon` | 로그/덤프 | strings |
| `/tmp` | BT 소켓 `bt.*`, `hosts`, `resolv.conf` | strings |
| `/contents` 디버그 훅 | `FUNC`, `SS`, `PKT_FILE`, `UDAP`, `audioanalyzer_params`, `SONYWM/CACHE`, `usba_cap`, `boot_logo` - 의미 미확인 | strings |
| 업데이트 | `/contents/NW_WM_FW.UPG`, microSD `/contents_ext/*.UPG` | libUpdateService.so strings |
| 기본 콘텐츠 | `/opt3/prein_contents`, `/opt3/special_contents`, `/opt3/prein_data` | VS bin/prepare_contentroot.sh |
| 번인 UI | `/opt2/appresouce/`, `/opt2/systemsound/WM_BEEP_*.wav` | 앱 strings |

### 5.4 영속 속성

- 구현: `persist.*`는 `/data/property/<name>` 파일이다(확정 사항, init의 property service). `/data`가 usrdata ext4이다.
- 관찰된 이름: `persist.sys.sony.{icx.adb, sleepsec, icx.anls.tune, icx.mqarender, icx.sehp.dsd, icx.cmpsepkt, icx.hardvinyl, icx.showselog, icx.tuning, seoff, seon, showamixer, showfile, showsysfs, showuevent}`, `persist.sys.usb.config`; 런타임 `sys.sony.{bootmode,console,gettyport,wmp,config}`, `sys.usb.{config,state,vid,msc1}`; 읽기 전용 `ro.sony.*`, `ro.model.*`, `ro.boot.*`(SYS bin/, VS lib/ strings). 이 중 `persist.sys.sony.show*`/`icx.tuning` 계열은 디버그 표시·튜닝 스위치로 보인다(추정).
- 속성 접근 도구: `getprop`, `setprop`, `watchprops`(SYS bin/).

### 5.5 공장 초기화

`VS bin/factoryresetdmp.sh`: `data`, `cache`, `db`, `contents`를 포맷하고 `StorageFormatterDmp`, `prepare_contentroot.sh`를 실행한 뒤 `nvp`, `var`를 포맷한다. NVP까지 지우므로 되돌릴 수 없는 작업이다. 조사 단계에서 호출하지 않는다.

---

## 6. 단계별 시사점

위험 순위는 "실패했을 때 장비를 재부팅 루프/벽돌로 만들거나 되돌리기 어려운 정도 x 불확실성"으로 매겼다. ROADMAP의 원칙(복구 경로 확인 전에는 bootimg/system 파티션에 쓰지 않는다)을 전제로 한다.

### 6.1 (a) UI만 교체, 데몬은 전부 유지

**할 일의 정의**: `HgrmMediaPlayerApp`을 대체하는 새 프로세스(또는 같은 이름의 바이너리)를 만든다. 데몬 28개와 `libpstcore` 기반 IPC는 그대로 둔다.

**통신해야 하는 대상** (모두 `Framework::GetServiceClient`/NEEDED 경로)

| 영역 | 라이브러리 / 인터페이스 | 비고 |
|---|---|---|
| 재생 | `libPlayerServiceClient.so` (`PlayController`, `SetTrackSequence`, `AllocNext/Prev`, `SeekTime`, `PlayEventListener`), `libPlayerServiceClientUtil.so` | 트랙 시퀀스는 클라이언트 소유 (추정) |
| 라이브러리 | `libMediaStoreServiceClient.so` (`Config`, `SetConfig`, `MediaStoreClient::Search`) | 앱이 DB 경로·스토리지 정의를 주입 |
| 설정 | `libConfigurationService.so`, `libDmpConfig.so` 키 | (GroupId, KeyId) 숫자 ID |
| 오디오 설정 | `libSoundServiceSettingsDmp.so`, `libVolumeService.so`, `libNcAsmService.so`, `libEffectCtrlDmp.so`, `libPathMgrService.so` | EQ/DSEE/볼륨/NC |
| 장치 관리 | `libFuncMgrService.so`, `libConnMgrService.so`, `libUsbMgrService.so`, `libStorageMgrService.so`, `libPowerMgrServiceClient.so` | 모드 전환·연결·USB·스토리지·전원 |
| 입력/표시/시간 | `KeyService`, `DisplayService`, `TimerService` (GetServiceClient 이름) | 키 이벤트, 백라이트, 알람 |
| 부가 | `NfcService`, `BtCommonService`, `BtPlayerService`, `TunerPlayerService`, `UsbDeviceAudioPlayerService`, 녹음·분석기 클라이언트 | 기능별 |
| 앱 수명주기 | `libeaselcore.so`/`libeaselqt.so`의 `easel::ApplicationBase`/`QtApplication`, `libappmgrservice.so` | 필수 |

**앱이 실제로 import하는 Framework/easel 표면** (VS bin/HgrmMediaPlayerApp `nm -D --undefined-only`): 이것이 새 UI가 맞춰야 할 최소 계약이다.
- `pst::core::Framework`: `GetServiceClient(const std::string&)`, `GetReference()`, `GetBinderLastError()`, `UpdateCurrentContextHangTimeout(unsigned long long)`. (`StartForApplication`은 import 목록에 없으므로 `easel::ApplicationBase::run` 안에서 불리는 것으로 보인다. 추정)
- `easel::ApplicationBase`: 생성자/소멸자, `run(int, char**, const char*, std::unique_ptr<easel::ModuleBaseInterface>)`, `Exit()`, `SetPumpTriggerHandler(std::function<void()>)`, `GetChangeLifeCycleTimeout()`, `GetPostInitializeTimeout()`, `OnSuspend(bool&)`, `OnPreResetSetting()`, `OnResetSetting()`, `ReadyToShutdown()`, `StopBootAnimation()`, `StartResumeAnimation()`, `StopResumeAnimation()`. 나머지 `OnInitialize/Activate/...` 훅은 가상 함수라서 import가 아니라 vtable로 연결된다(추정).
- `easel::QtApplication::QtApplication(int&, char**)`. 앱 클래스는 이것을 상속하고, 화면 모듈은 `ModuleBaseInterface` 구현체로 `run`에 넘긴다(추정).
- `SetPumpTriggerHandler`는 Framework의 이벤트 루프(`fr_eloop`)를 Qt 이벤트 루프에서 구동하기 위한 훅으로 보인다(추정). 새 UI가 Qt를 쓰지 않는다면 이 부분을 직접 구현해야 한다.
- `pst::services` 심볼은 앱이 189개를 import한다(같은 `nm`). 서비스 클라이언트 클래스와 메서드 전부가 이 목록에서 나오므로, 새 UI의 필요 인터페이스 목록은 이 189개로 시작하면 된다.

앱이 import하는 `pst::services` 심볼 189개의 분포 (네임스페이스 기준, VS bin/HgrmMediaPlayerApp `nm -D --undefined-only`):

| 네임스페이스::클래스 | 심볼 수 | 눈에 띄는 진입점 |
|---|---|---|
| `sound::EffectCtrlDmp` | 57 | 오디오 효과 제어 (DSEE/EQ/바이닐 등, 메서드 이름은 libEffectCtrlDmp.so nm) |
| `ncasm::NcAsmService` | 23 | NC/주변음 |
| `volume::VolumeService` | 19 | 볼륨, AVLS |
| `sound::SoundServiceSettingsDmp` | 14 | 사운드 설정 |
| `funcarch::storagemgr` | 12 | 스토리지 상태/포맷 |
| `configuration::Configuration` | 12 | `GetInt/SetInt/GetBool/SetBool/GetString/SetString/GetBinary/SetBinary(GroupId, KeyId, ...)`, `Flush()`, `Reset()`, `SetSystemNvp(unsigned int const&, std::vector<unsigned char> const&)` |
| `audioanalyzerservice::AudioAnalyzerService` | 7 | 분석기 |
| `recorderservice::AudioInRecorderService` | 6 | 녹음 |
| `funcarch::connmgr` | 6 | 연결 장치 상태 |
| `funcarch::usbmgr`, `pathmgr`, `funcmgr` | 각 5 | USB 기능, 오디오 경로, 기능 모드 |
| `onetrackplayer::OneTrackPlayerService` | 4 | 단일 트랙 재생 |
| `mediastore::MediaStoreService` | 4 | `GetInstance()`, `GetMediaScanner()`, `GetMediaStoreClient()`, `SetConfig(mediastore::Config const&)` |
| `playerservice::PlayerService` | 2 | `GetInstance()`, `getPlayController(char const*)` |
| `binder::ServiceManager` / `BinderDriver` | 3 / 1 | `ServiceManager::AddLog`, `TimeMeasureHolder`, `BinderDriver::GetReference()` (로그/계측용으로 보임, 추정) |
| `funcarch::SysPropUtils`, `funcarch::powermgr` | 1 / 1 | `SysPropUtils::Get(string const&)`, `GetPowerMgrServiceClient()` |
| `playerservice::util`, `recorderservice::util` | 1 / 1 | `NodeCount::s_instance_` 등 |

즉 앱이 쓰는 인터페이스의 약 60%(113/189: 57+23+19+14)가 오디오 효과·볼륨·NC 설정이다. 설정(`Configuration`)과 재생(`PlayerService`)의 표면은 의외로 작다.

**재구현해야 하는 것**: `ScreenSettings.json`이 묶는 ViewModel 약 63개와 QML 약 300개(C++ ViewModel은 앱 실행 파일 안에 있고 별도 .so가 없어 링크로 재사용할 수 없다. 앱은 ELF `DYN`(PIE)이고 stripped이다(VS bin/HgrmMediaPlayerApp `readelf -h`). glibc 2.23의 `dlopen`으로 앱 자체를 적재해 내부 심볼을 쓰는 방법은 이론상 있으나 export된 동적 심볼이 거의 없을 것이므로 비현실적이다(추정, 미검증)). 팝업·저전력·USB 연결·업데이트 진행 같은 시스템 UX도 앱이 맡는다(앱 strings의 `ResStartLowBattery`, `ResStartUpdateDB`, `ResStartInitialSetting`). 데몬이 앱 쪽 UX를 전제하는 범위는 미확인이다.

**그대로 둘 것**: 28개 데몬, 커널 모듈, 오디오 튜닝 테이블, 부트 체인, `/system` 이미지의 나머지.

**위험 순위**

1. 수명주기·hang 프로토콜: 앱 메인 스레드가 멈추면 시스템이 재부팅된다(1.8절). appmgrservice는 앱이 Foreground가 될 때까지 기다리다 타임아웃이면 Abort한다. 새 UI는 `ApplicationBase` 훅(OnInitialize ... OnSuspend)과 hang 응답을 정확히 구현해야 한다. Qt 이벤트 루프를 막는 작업은 금지다. 실패 시 증상이 재부팅 루프이다.
2. ABI·툴체인·인터페이스 복원: clang 3.9 + libc++(`std::__1`)로 빌드해야 한다. 헤더 전용 `I*Service` 11개는 vtable과 `ReqMsg/RspMsg` 배치를 바이너리에서 복원해야 한다. 틀린 배치를 데몬에 보내면 데몬이 죽고 그것이 재부팅이다.
3. 클라이언트 소유 상태와 초기화 책임: MediaStore `SetConfig`(스토리지·DB 경로), 트랙 시퀀스, 설정 초기값, 볼륨·효과 복원 등 앱이 시작 시 하는 일을 빠뜨리면 음악이 안 나오거나 DB가 비어 보인다. 앱의 시작 순서(`startup` 섹션 in ScreenSettings.json)를 분석해야 한다.
4. 화면 소유권과 입력: fb0/Mali를 eglfs로 쓰는 경우 `QEglFSmt8590Hooks`가 필요하다. 부트 애니메이션을 정지해야 한다(`StopBootAnimation`). 터치는 `/dev/input/event1`, 키는 KeyService 경유(추정). 표준 Qt eglfs 빌드는 이 하드웨어 훅이 없어서 그대로 안 될 수 있다(추정). 순정 `libqeglfs.so`를 재사용하면 가장 안전하다.
5. 배포 방식: `/system`이 ro이고 appcfg가 `/system/vendor/sony/bin`에 있다. 영구 배포는 `system.img`를 UPG로 구워야 하므로 복구 경로 확인 전에는 금지다. 비영구 시험은 adb root에서 `/system` rw remount 또는 bind mount로 한다(추정: 재부팅하면 원복). 램디스크 환경을 바꾸는 것은 boot.img 쓰기이므로 같은 이유로 금지다.
6. 폰트·번역·아이콘 자산: 글꼴(SST/NotoSansKR 등)은 `VS lib/fonts`에서 가져다 쓰면 되고, 이미지는 `/opt2/appresouce/`(미추출)가 필요하다. 위험은 낮다.

**우회 경로 후보** (모두 미검증)
- `HgrmMediaPlayerApp.appcfg`에 `type: Home`인 appcfg를 하나 더 두고 새 앱을 등록한다. 복수 Home 앱 처리 규칙은 미확인이다.
- 평문 QML이므로 qrc 대체 방식(`QResource::registerResource` 선후 우선순위, `/etc/ld.so.preload` 훅)을 연구할 가치가 있으나 이 방식은 C++ ViewModel을 그대로 쓰는 장점이 있다(추정, 미검증).

### 6.2 (b) 유저랜드 전체 교체

**정의**: 커널(3.10.26, 벤더 모듈 포함)은 유지하고 init 이후 전부 새로 만든다. Sony 데몬·`libpstcore`를 버리는 경우를 기본으로 본다.

**잃는 것**: SoundServiceFw의 DSP(DSEE AI/ClearAudio, ClearPhase, EQ, 바이닐, MQA, DSD->PCM), PlayerService의 디코더·MediaStore, BT 미들웨어, NC, 전원/USB/스토리지 관리 서비스 전부.

**재구현 또는 바이너리 재사용 목록**

| 영역 | 재현해야 하는 것 | 재사용 가능 후보 |
|---|---|---|
| 부팅 | `load_sony_driver` 전 과정: `icx_nvp_emmc.ko` + `nvpnode`, wm_key/cxd3778gf_dnc_core/icx_carddet/icx_usbconn/icx_usbscsi insmod, `dacdat` 테이블 적재(`/proc/icx_audio_*_data`), ucom 펌웨어 | `SYS bin/dacdat`, `nvp*`, `load_sony_driver` 스크립트를 그대로 호출 (바이너리 의존) |
| 오디오 | ALSA card 0 `hw:0,4` + CXD3778GF 믹서 시퀀스, ASRC, DSD, 헤드폰 앰프·리미터·NC 설정 | libasound, `audiohal-*`는 Sony 프레임워크 의존이므로 직접 재사용 어려움 |
| 전원 | `/sys/power/state` mem, wakelock, `icx_pm_helper`, RTC wake, 배터리·충전 정책, 종료 순서 | 없음. 정책을 새로 작성해야 한다 |
| USB | `android_usb` 모드 전환(adb/uac/msc), LUN 연결, OTG(`mt_usb/mode`), UAC 피드백 | `init.usbcfg.rc` 로직 재현 |
| 스토리지 | `mount_partition` 로직, md0 선형, exfat(FUSE), 카드 감지 | `mount_partition`, `exfatfuse` 그대로 |
| BT | mtkbt + `libbluetooth.blueangel.so` 구동, LDAC/aptX/AAC/SBC | 두 바이너리를 블랙박스로 구동 (IPC 규약 미확인) |
| NFC/FM | `libnfc-nci` + `/dev/cxd224x-i2c`, `/dev/radio` | 비교적 표준 |
| 미디어 | 디코더(MP3/AAC/FLAC/ALAC/DSD/APE/WMA...), 태그·cue·M3U8·정렬 | 오픈소스 코덱으로 대체하는 것이 현실적 |
| 설정/NVP | `/dev/icx_nvp/NNN` 레이아웃, 모델 코드(`shp`), 용량 바이트(`mid`) | 읽기 전용 접근부터 |

**위험 순위**
1. 오디오 경로와 하드웨어 보호: 리미터·게인·ucom 설정을 틀리면 소리가 안 나거나 이어폰에 과대 출력이 갈 수 있다(위험 추정, 실제 보호 회로는 미확인). `dacdat` 테이블과 `timed_mute_ms`, 믹서 시퀀스를 그대로 모사하는 것이 필수다.
2. 전원·충전·서스펜드: 배터리 보호, 충전 제어(`2-006b`), 부팅 요인·재개 요인, `wdt_ctrl` 관리. 잘못 만들면 방전·재부팅 루프.
3. 부팅 체인 전체(램디스크 init, rc): boot.img를 써야 하므로 복구 경로가 선행 조건이다.
4. BlueAngel(mtkbt) 블랙박스 구동과 코덱 라이선스.
5. USB 모드·스토리지(대용량 저장 시 LUN 해제·재마운트 순서).
6. 미디어 DB·디코더 품질(DSD/ALAC/APE 등)과 기능 격차.

(b)에서 `libpstcore`와 Sony 데몬을 일부 유지하는 절충안은 가능하다. 단, 마스터 hagodaemon이 binder context manager이고 감독/재부팅을 쥐고 있어 부분 유지는 곧 (a)와 같은 제약(수명주기·hang 프로토콜)을 가진다.

### 6.3 (c) 이 커널 위에서 Android 유저랜드 실행

**근거**: 커널에 `ANDROID_BINDER_IPC`, `ASHMEM`, `ION`/`ION_MTK`, `ANDROID_LOGGER`, `LMK`, `SYNC`/`SW_SYNC`, `EARLYSUSPEND`/`WAKELOCK`, `USB_G_ANDROID`가 있고(K kconfig.txt), 빌드 트리는 `LRX21M`(Android 5.0)이다(SYS build.prop).

**장애물** (위험 순위)
1. 그래픽: Mali-450 userspace가 fbdev "linux" 플레이버이다(gralloc/hwcomposer 없음, SYS lib/). Android 5 SurfaceFlinger는 gralloc/HWC를 요구한다. 커널 Mali 드라이버 버전과 userspace(r0p0)의 ABI가 맞는지 미확인이다. 소프트웨어 GLES 폴백은 가능하나 800x480에서도 느리다(추정).
2. 오디오 HAL 신규 작성: CXD3778GF 초기화(dacdat, ucom 펌웨어)와 믹서 시퀀스, `hw:0,4` 경로, CM4/ASRC. tinyalsa로 card 0을 다루고 `load_sony_driver` 로직을 init에서 재현해야 한다.
3. binder context manager 충돌: Android `servicemanager`와 Sony 마스터 hagodaemon은 공존할 수 없다(2.2절). 둘을 함께 쓰려면 하나를 포기해야 한다.
4. 메모리: 램 용량·CPU 코어 수가 미확인이다. Android 5 런타임(ART/zygote)의 메모리 요구가 맞는지 판단할 수 없다.
5. BT: BlueAngel이지 BlueDroid가 아니다. Android BT 프레임워크와 맞는 HAL을 새로 써야 한다. LDAC/aptX는 BlueAngel 코덱 라이브러리 의존.
6. 빠진 구성: `/system/bin/sdcard`가 없고(SYS bin/), Wi-Fi 모듈이 없다. 터치·키는 evdev로 표준 입력이 되지만, 전원/서스펜드는 `icx_pm_helper` 연동이 필요하다.
7. 일부 Android 구성요소는 이미 glibc 위에서 쓰인다(libcutils, liblog, libhardware 등). 하지만 이것이 Android 호환 Bionic 사용자 공간이라는 뜻은 아니다(glibc 2.23 환경).

(c)는 (a)(b)보다 위험과 작업량이 크다. 그래픽 스택(Mali 사용자 공간과 커널 일치)과 오디오 HAL이 먼저 해결돼야 의미가 있다.

### 6.4 선행 실험 우선순위 (단계 공통)

1. 읽기 전용 조사로 7절의 항목 확인 (uid, `/proc/dumchar_info`, 마운트 옵션, 메모리).
2. 비영구 환경에서 (a)의 가장 위험한 부분(수명주기·hang 응답)을 작은 테스트 클라이언트로 먼저 검증 (예: `Framework::StartForApplication`만 호출하는 빈 Qt 앱).
3. 복구 경로(recovery 파티션, 부트로더 모드, UPG 재설치)가 확인된 뒤에만 영구 배포를 고려.

---

## 7. 미확인 사항 (라이브 기기, adb root 셸에서 확인)

모든 확인 명령은 읽기 전용이다. 쓰기/플래시/포맷 도구(`flash_*`, `update_fu_image.sh`, `format*`, `factoryresetdmp.sh`, `nvp`의 write 모드)는 쓰지 않는다.

| # | 질문 | 확인 방법 (읽기 전용) | 영향 |
|---|---|---|---|
| 1 | 실제 파티션 맵(이름, 오프셋, 크기)은? | `cat /proc/dumchar_info`, `ls -l /dev/block/`, `ls -l /emmc@*` | 5.1절, 복구 계획 |
| 2 | 커널 명령행(LK가 넘긴 값)은? | `cat /proc/cmdline` | 1.1절 |
| 3 | 각 마운트의 파일시스템과 옵션(특히 noexec)은? | `cat /proc/mounts` | 5.2절, 6.1 배포 |
| 4 | `system` 이름의 런타임 uid/gid, hagodaemon 드롭 후 uid는? | `ps`, `cat /proc/<pid>/status`(Uid, CapEff) | 1.6절, 6.1 |
| 5 | 28개 프로세스의 실제 cmdline과 부모(logwrapper 유지 여부)는? | `ps -ef`, `cat /proc/<pid>/cmdline` | 1.9절 |
| 6 | RAM 용량과 CPU 코어 수, 스왑(zram)은? | `cat /proc/meminfo /proc/cpuinfo`, `cat /proc/swaps` | 6.3 (Android 가능성) |
| 7 | fb0의 해상도, bpp, 회전, 스트라이드는? | `cat /sys/class/graphics/fb0/{virtual_size,bits_per_pixel,stride,rotate}`, `fbset` 또는 `FBIOGET_*` 읽기 | 3.4절 |
| 8 | Mali 커널 드라이버 버전과 userspace(r0p0) 일치는? | `dmesg \| grep -i mali`, `/sys/module/mali*/version` | 6.3 |
| 9 | 앱 프로세스의 실제 환경변수(`QT_QPA_PLATFORM`, `QT_PLUGIN_PATH`, `QT_QPA_FONTDIR`)는? | `cat /proc/<앱pid>/environ` | 3.3절 |
| 10 | 앱이 열고 있는 장치·파일·소켓은 (입력 경로 포함)? | `ls -l /proc/<앱pid>/fd`, `cat /proc/<앱pid>/maps` | 3.7절, 6.1 |
| 11 | 키 이벤트는 `/dev/input/event*` 중 어디서 오고, HOLD 스위치는 어떤 장치인가? | `getevent -lp`, `cat /proc/bus/input/devices` | 4.2절 |
| 12 | 앱이 죽으면(SIGKILL) 시스템은 재부팅하는가, 앱만 재시작하는가? | 로그를 보며 앱 pid에 신호 (재부팅을 감수하는 시험이므로 마지막에, 복구 경로 확인 후) | 6.1 위험 1 |
| 13 | 앱 메인 스레드 hang 판정 시간과 응답 프로토콜은? | `libpstcore.so`의 `UpdateCurrentContextHangTimeout` 호출처 디스어셈블, 런타임 trace(`strace -f`는 없다면 `/proc/<pid>/stack`) | 6.1 위험 1 |
| 14 | 부팅 중 `StopBootAnimation`과 fb 인계 방식은? | 부팅 로그 `logcat`/`/var/log/SYSTEMLOG`, `ps`에서 `icx_bootanimation` 소멸 시점 | 3.4절 |
| 15 | `resumeanimation.zip`이 없을 때 동작은? | `ls -l /system/media/`, 서스펜드 복귀 시 로그 | 3.4절 |
| 16 | binder 트랜잭션 코드와 `TransactionParam` 직렬화 배치는? | `/sys/kernel/debug/binder/{transactions,transaction_log,state}` 읽기(debugfs 마운트됨, RD init.rc:14), `libpstcore.so` 디스어셈블 | 2.2절, 6.1 위험 2 |
| 17 | 임의 프로세스가 `Framework`에 앱 등록 없이 클라이언트만 얻는가? | 작은 테스트 클라이언트로 `GetServiceClient` 시험(읽기 전용 서비스) | 2.5절 |
| 18 | `ConfigurationService`의 (GroupId, KeyId) 숫자 ID 표와 NVP 노드 대응은? | `libConfigurationService.so`/`libDmpConfig.so` 테이블 디스어셈블, `nvpinfo` 출력 | 5.3절 |
| 19 | `/dev/icx_nvp/000~092` 노드와 NVP 이름의 대응은? | `nvpinfo`, `nvpstr` 읽기 모드, `ls -l /dev/icx_nvp/` | 4.10절 |
| 20 | `VolumeSettings.json`, `AudioAnalyzerSettings.json`의 위치는? | `lsof`/`/proc/<앱pid>/fd` 추적, `find / -name '*Settings.json'` | 3.8절 |
| 21 | `/opt1`, `/opt2`, `/opt3` 내용(특히 `appresouce`, `systemsound`, `prein_*`)은? | `ls -lR /opt2 /opt3`, `df` | 3.5절 |
| 22 | CM4 코어 펌웨어는 어떻게 적재되는가? | `dmesg \| grep -i cm4`, `/proc/iomem`, `/sys/devices/platform/*cm4*` | 4.3절 |
| 23 | `PaSleep/PaWakePrimaryAudioDevice`의 호출자와 시점은? | `grep -r PaSleep /system /sbin /etc`, 서스펜드 시 `ps`/로그 | 4.3절 |
| 24 | adbd를 막는/여는 추가 게이트는 있는가(`/dev/android_adb_enable` 외)? | `ls -l /dev/android_adb_enable`, `getprop persist.sys.sony.icx.adb`, `cat /sys/class/android_usb/android0/state` | 4.6절 |
| 25 | 실제 연결 칩(MT6625L? CONSYS_8590?)과 BT/FM 펌웨어 로딩 경로는? | `dmesg \| grep -i -E 'wmt\|stp\|conn'`, `cat /proc/net/dev`, `ls /sys/class/net` | 4.7절 |
| 26 | `mtkbt` 소켓 프로토콜(`/dev/socket/bt.int.adp`)은? | `ls -l /dev/socket /tmp`, BtMw 디스어셈블 | 6.2 (BT 블랙박스) |
| 27 | `ffu` 서비스와 `66xx_launcher*`는 언제 쓰이는가? | `grep -r ffu /system /sbin /etc`, `getprop init.svc.ffu` | 1.5절 |
| 28 | `/contents`의 디버그 훅(`FUNC`, `SS`, `PKT_FILE`, `UDAP`, `SONYWM/CACHE`, `usba_cap`, `boot_logo`)의 의미는? | 파일 생성 없이 바이너리 strings 주변 디스어셈블 | 5.3절 |
| 29 | `chrome` 파티션의 내용과 `cast_cli` 존재 여부는? | `ls -l /emmc@chrome`, 파티션 헤더 읽기(dd 읽기 전용) | 1.5절 |
| 30 | 실제 업데이터 rootfs는 어디에 있고 복구 모드 진입은? | `recovery` 파티션 헤더 읽기, 키 조합 문서 확인 | 복구 계획 |
| 31 | `/data`·`/contents`·`/tmp`에서 실행이 막혀 있는가(noexec)? | `/proc/mounts`, 테스트 스크립트를 `/tmp`에 두고 실행 시도 | 5.2절, 6.1 배포 |
| 32 | 배터리 게이지·충전 IC 노드와 단위는? | `ls /sys/class/power_supply/*`, 각 노드 읽기 | 4.4절 |
| 33 | 터치 IC 두 I2C 주소(`1-0020`, `1-0048`) 중 실제 장착된 것은 무엇이고 `syi`/펌웨어 버전은? (ICX1291 변형은 Himax 경로) | `ls /sys/bus/i2c/devices/`, `/proc/himax_touch/*` 읽기(`debug`에 쓰기는 하지 않는다), `nvpflag -x syi` | 4.2절 |
| 34 | `libKeyListener.so`는 누가 쓰는가, 앱의 키 수신 경로는? | 앱 `maps`/`fd`, `strace` 불가 시 `lsof` | 3.7절 |
| 35 | 한 개의 Home 앱 이외에 appcfg를 하나 더 등록하면 어떻게 동작하는가? | 비영구 시험(bind mount된 디렉터리에서) | 6.1 우회 경로 |
| 36 | `sys.sony.bootmode`, `ro.boot.console` 등 현재 값과 `icx_pm_helper` 파라미터 값은? | `getprop`, `cat /sys/module/icx_pm_helper/parameters/*` | 1.3절, 4.4절 |
| 37 | `/etc/passwd`의 root와 system이 같은 해시를 갖는다. 실제 로그인/su 경로가 열려 있는가? | `ls -l /bin/su /system/bin/su`, `getprop ro.secure`, 직접 로그인 시도는 하지 않고 설정만 확인 | 보안 메모, 접근 방식 |
| 38 | 매 부팅의 터치/코덱/NFC 펌웨어 갱신(1.4절)은 같은 버전일 때 실제로 쓰기를 건너뛰는가? | 부팅 로그의 `Touchpanel FW update:`, `cxd3778gf ucom FW update:`, `NFC(cxd225x) FW update:` 줄(`/var/log/SYSTEMLOG`, `dmesg`) | 1.4절, 실험 중 부팅 반복의 안전성 |
