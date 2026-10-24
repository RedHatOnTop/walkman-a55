> **2026-10-10 갱신.** 빌드는 `./build.sh`(→ `../tools/build_upg.sh`, upgtool 불필요)이고, 실행 절차는 `../docs/DEVICE-STEPS.md`를 따른다.
> 이 기기는 Walkman One이라 최상위 `NW_WM_FW.UPG`(W1 키)를 쓴다. 아래 "미해결: 어떻게 실행시킬 것인가"와 "다음 단계"는
> `../ANALYSIS.md` §1·§7·§10으로 대체됐다 (트리거 = `scsitool do_fw_upgrade`). 스크립트는 v3다: 첫 줄에서 fup을 리셋하고,
> 업데이터 램디스크를 `update-rootfs.tar`로 남긴다.

# NW-A50 백업 UPG (읽기 전용)

NW-A55 / A56HN / A57 의 **전 파티션 백업 + 하드웨어 서베이**를 수행하는 커스텀 펌웨어 패키지.
어떤 파티션에도 쓰지 않는다. 오로지 읽어서 `/contents/WMBACKUP/` 에 파일로 떨군다.

## 파일

| 파일 | 역할 |
|---|---|
| `update.sh` | UPG의 0번 파일. 업데이터가 root로 실행한다. |
| `md5.txt` | UPG의 1번 파일. 빈 파일이지만 슬롯이 반드시 있어야 한다. |
| `build.sh` | upgtool로 서명된 `NW_WM_FW.UPG` 생성 + round-trip 검증 |
| `NW_WM_FW.UPG` | 빌드 결과물 |

## 빌드

```sh
git clone --depth 1 https://github.com/Rockbox/rockbox.git
cd rockbox/utils/nwztools/upgtools && make      # libcrypto++-dev, zlib 필요
cd -
./build.sh /path/to/upgtool
```

빌드 스크립트는 만든 UPG를 다시 풀어서 0번 파일이 바이트 단위로 일치하는지 확인한다.
서명이나 키가 틀렸으면 기기가 아니라 여기서 실패한다.

## 동작 원리

업데이트 체인은 이렇게 생겼다.

```
Sony 업데이터 exe
  └─ CAB (바이트 반전) ─ 7z로 추출 후 NOT 하면 열린다
       └─ NW_WM_FW.UPG ─ AES, KAS = dd49de9d... (Rockbox upgtool에 포함)
            ├─ file 0  update.sh   ← 기기가 root로 실행
            ├─ file 1  md5.txt
            ├─ file 2  boot.img
            └─ ...
```

기기 쪽에서는 `icx_start_update.sh`가 이 순서로 돈다.

1. `nvpflag fup` 가 `0x70555766` 인지 확인. 아니면 즉시 재부팅.
2. `/contents` 마운트
3. `fwpchk -c` 로 **MD5와 대칭키 서명만** 검증 (RSA 없음)
4. 0번 파일을 `/update.sh` 로 추출, `chmod 755`, 실행
5. 성공하면 `fup` 플래그 리셋 후 재부팅

핵심은 3번이다. 공개키 검증이 없어서 upgtool로 만든 패키지가 정품과 구분되지 않는다.

## 수집되는 것

`/contents/WMBACKUP/` 아래에 생긴다.

- `survey.txt` — meminfo, cpuinfo, cmdline, eMMC CID/manfid/CSD, DRAM 타입, 파티션 맵, nvp 전체 덤프
- `dumchar_info.txt` — 원본 파티션 테이블
- `<파티션>.img` — 각 파티션 이미지
- `mmcblk0boot0.img` — **preloader. SP Flash Tool 복구에 가장 중요한 파일**
- `manifest.txt` — 이름 / 크기 / md5
- `backup.log` — 실행 로그

제외: `contents`(여기에 쓰는 중), `bmtpool`, `usrdata`, `cache`

`survey.txt` 하나로 지금까지 못 풀던 질문이 다 풀린다. RAM 실장량, eMMC 제조사와 모델, DRAM 타입,
실제 파티션 레이아웃, 그리고 `recovery` 파티션이 존재하는지 여부.

## 설계상 중요한 제약 두 가지

**1. 무조건 `exit 0` 한다.**
호스트 스크립트는 `/update.sh` 가 0이 아닌 값을 반환하면 `fup` 플래그를 **리셋하지 않고** 재부팅한다.
그러면 다음 부팅에서 또 업데이트를 시도한다 → 부팅 루프. 그래서 모든 실패는 로그만 남기고 삼킨다.

**2. 순정 `update.sh`가 쓰는 명령만 쓴다.**
업데이트 중에는 `/system`이 마운트되어 있지 않은 것으로 보인다(순정 스크립트가 직접 마운트한다).
따라서 존재가 보장되는 것은 순정 스크립트가 실제로 호출하는 것들뿐이다:
`/xbin/busybox`, `/bin/dd`, `cat`, `echo`, `ls`, `awk`, `grep`, `expr`, `mv`, `mkdir`, `sync`,
`nvp` / `nvpflag` / `nvpstr`, 그리고 `/proc/dumchar_info`.

## 미해결: 어떻게 실행시킬 것인가

**이 부분은 아직 검증되지 않았다.** UPG를 만드는 것과 기기가 그걸 집어먹게 하는 것은 별개다.
`fup` 플래그를 세우는 주체를 아직 특정하지 못했다. 후보는 셋.

1. **순정 업데이터 exe 재활용 (유력)** — 113MB exe 안의 CAB을 열어 `NW_WM_FW.UPG`만 바꿔치기하고
   다시 반전해 붙인다. `Settings.ini`의 `TotalFileSize` 도 같이 고쳐야 한다. MrWalkman 계열 커스텀
   펌웨어가 쓰는 방식으로 추정된다.
2. **진단 모드** — `nvpflag tst` 를 `0x4D545346` 또는 `0x4D545354` 로 세우면 UART(`/dev/ttyMT1`)로
   진단 셸이 뜨고 거기에 ADB 제어 명령이 있다. 단 WM-PORT 핀에 물리적으로 붙어야 한다.
3. **기기 설정의 업데이트 메뉴** — UPG를 `/contents` 루트에 두고 호출되는지 확인 필요.

1번부터 시도하는 게 맞다. 진행 전에 **반드시 순정 1.02 업데이터 exe 원본을 따로 보관**해 둘 것.

## 리스크

- 스크립트 자체는 읽기 전용이라 파티션을 망가뜨릴 수 없다.
- 실제 위험은 "실행시키는 단계"에 있다. 업데이터 exe를 잘못 손대면 기기가 아니라 PC 쪽에서 실패하므로
  비교적 안전하다.
- 최악의 경우 대비책은 MT8590의 BROM 모드(SP Flash Tool)인데, 이 기기에서 동작하는지 미확인이다.
  그래서 `mmcblk0boot0.img`(preloader) 확보가 이 백업의 가장 큰 목적이다.
- `/contents`는 FAT32다. 파일당 4GB 제한이 있지만 가장 큰 파티션이 800MB라 문제없다.

## 다음 단계

1. 순정 업데이터 exe 리패킹 (CAB 교체 + `TotalFileSize` 수정)
2. 백업 실행, `survey.txt` 확보
3. `survey.txt` 로 복구 경로 판단 (recovery 파티션 유무, BROM 접근성)
4. 그 다음에야 adb 활성화 등 쓰기 작업으로
