# 내장 Wi-Fi 수신 테스트 (2026-10-11)

외부 번들 `nwa50-wifi-test.tar.gz`(WIFITEST)를 Walkman One 기기에서 돌렸다. 번들은 MT8590 내장 Wi-Fi를 깨우는 모듈 5개와 정적 `iw`/`wpa_supplicant`를 담고 있다. 실행은 RAM에서만 했고 파티션에는 쓰지 않았다.

## 결과

- **수신 경로가 동작한다.** 모듈 4개를 적재하고 `echo 1 > /dev/wmtWifi`를 쓰자 `wlan0`가 0초 만에 생겼다. 스캔에서 BSS 12개가 잡혔다.
  - 신호 −87…−33 dBm
  - 2.4 GHz 채널 1/2/3/6/11, 그리고 5240 MHz에서 1개
- 커널 로그는 `WMT turn on WIFI success!`, `wiphy_register`, 스캔 FSM 전이까지 정상이다. 테스트 이후 oops나 오류는 없다.
- 테스트 뒤에도 앱, SD 마운트, BT 모듈은 그대로 살아 있었다.
- MAC은 `00:08:22:40:53:3f`다. NVRAM이 없어 MediaTek 기본값이 쓰였고, 번들 README의 예상과 같다.
- regdom은 world 기본값이다. 그래서 5 GHz 채널은 `no IR`(수동 수신만)이다.
- **송신과 연결도 동작한다** (같은 날, `wifi.conf` 추가 후).
  - WPA2-PSK/CCMP로 2422 MHz AP에 연결했고, 4-way handshake와 group key 교환이 끝났다.
  - DHCP로 `192.168.0.17/24`를 받았다.
  - 게이트웨이와 `1.1.1.1` ping 모두 손실 0%다(평균 8 ms / 18 ms).
  - 링크는 −65 dBm, tx bitrate 65 Mbit/s다(11n 20 MHz).
  - DNS는 `/etc/resolv.conf`가 read-only system에 있어서 아직 설정하지 않았다.
- 연결 상태에서 기기가 네트워크 쪽에 여는 포트는 없다. `127.0.0.1`의 5037과 probe 7777뿐이다.

로그는 `work/wifitest/run-1011-1223/`에 있다(gitignore):
- `stdout2.txt`, `wifitest_*.log`, `dmesg_*.txt`
- `scan_*.txt`, `iw_phy_*.txt`
- `kmsg-live.txt`: 호스트에서 실시간으로 받은 커널 로그

## 정적 대조 (우리 덤프 기준)

번들 README의 주장은 기기 커널(boot 이미지, 순정 1.02와 같은 md5)과 맞았다:
- 커널: `3.10.26`, gcc 4.8
- IKCONFIG: `MODVERSIONS`, `MODULE_SIG` 꺼짐. `CFG80211`/`WLAN`/`MTK_CONNSYS_WIFI`/WEXT도 꺼짐. 즉 칩이 아니라 빌드 설정에서 뺀 것이다.
- vermagic: 번들 모듈과 기기 모듈이 같다.
- 커널에 `mtk_wcn_wmt_func_on/off`, `assert`, `msgcb_reg` 이름이 있다.
- `/etc/firmware/WIFI_RAM_CODE_8590`가 있다.
- `wmt_loader`가 `*_bp.ko` 파일명을 찾는다.

기기의 `mtk_stp_wmt_soc.ko`는 `system-rootfs` 순정본과 바이트가 다르다. 하지만 export 목록(4개)은 같다.

## 번들 스크립트를 고친 곳

`/bin`의 명령은 Android `toolbox`를 가리킨다(`/bin/id`, `/bin/mknod` 등). 그래서 GNU/busybox 옵션이 안 먹는다. 1–3은 스크립트를 고친 곳이고(diff는 로그 폴더의 `wifi_test.sh.diff`), 4는 스크립트는 그대로 두고 손으로 우회한 문제다:

1. `uname -r` → `$BB uname -r`: 기기에 `uname` 명령 자체가 없어서 0단계에서 멈췄다.
2. `id -u` → `$BB id -u`: toolbox `id`는 `-u`를 무시해서 root를 root가 아니라고 판정했다.
3. `/dev/wmtWifi` 메이저를 `/proc/devices`의 `mtk_wmt_WIFI_chrdev_soc`에서 읽고, mknod는 busybox로 한다. `/dev`가 devtmpfs가 아니라 일반 tmpfs라서다.
   - 이번 실행에서는 노드가 이미 있었고 메이저는 153이었다.
4. 5단계의 `wpa_supplicant -B ... -f wpa.log`: `-f`를 주면 데몬이 시작 직후 사라진다. 로그 파일도 생기지 않았다. `-f`를 빼면 `-B`가 정상 동작한다. 원인은 확인하지 않았다(빌드에 `CONFIG_DEBUG_FILE`이 빠졌을 가능성).

## 남은 것

- DNS: `resolv.conf`를 `/tmp`에 두고 bind mount하는 방법 등. 그리고 무엇을 네트워크로 할지 정해야 한다.
- 번들 모듈의 출처 검증: BUILD.md로 다시 빌드해서 비교한다.
- 상주화(`/system/lib/modules`에 넣기): ROADMAP상 system 쓰기는 복구 경로가 확인된 뒤에만 한다.
