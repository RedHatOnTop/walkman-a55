# Phase 1 — 순정 UI 구조와 교체 방법

2026-10-10, 기기 없이 정적 분석과 호스트 실행으로 확인한 내용이다. 기기에서 아직 검증하지 않은 항목은 "기기 미검증"으로 표시했다.

## 요약

- 순정 UI는 `HgrmMediaPlayerApp` 실행 파일 안의 qrc 리소스다. QML 312개, JS 1개, PNG 807개가 38개 리소스 루트에 들어 있다.
  일부 이미지는 `/opt2/appresouce/`(option2 파티션)에서 읽는데 이 파티션은 아직 추출하지 않았다. 백업 UPG가 덤프한다.
- 화면 QML은 순수한 View다. C++ ViewModel과는 **이름**으로만 연결된다. 루트 프로퍼티 이름, 시그널 이름, 함수 이름, objectName이 그 연결 고리다.
  같은 이름만 유지하면 화면을 통째로 다시 써도 순정 C++ 로직(재생, DB, 설정)이 그대로 구동한다.
- 교체 수단은 **리소스 오버레이**다. `/contents/wmmod/overlay.rcc` 안의 파일이 같은 `qrc:/` 경로의 순정 파일을 대체하거나 새 파일을 추가한다.
  시스템 파티션에는 쓰지 않는다. eglfs 플러그인(`probe/mkspec/overlay.cpp`)이 `platformInit()`에서 적용한다.
- 개발은 호스트에서 반복한다. `mods/<이름>`에 패치를 쓰고 `tools/mkmod.sh`로 빌드한 뒤 `tools/uisim.sh --mod`로 화면을 PNG로 렌더한다.
  기기에는 같은 `overlay.rcc`를 복사한다.

## 1. 앱 UI 구조

| 항목 | 내용 | 근거 |
|---|---|---|
| 진입 | `QQmlApplicationEngine::load(qrc:/framework/FrameWork.qml)`. 루트는 `Window`다 | 앱 import, strings |
| 전역 컨텍스트 | `screenController`(`dmpapp::ScreenTransitionController`), `appFont`(`dmpapp::Font`), `dialogManager`(`DialogManager`), `holdToastManager`(`dmpapp::HoldToastManager`). 이 넷뿐이다 | QStringLiteral 0x407264–0x4072e4, QML 자유 식별자 스캔, moc 클래스 대조 |
| 화면 로딩 | `FrameWork.qml`의 `Repeater { model: screenController.allScreenIDList }` → `Loader { source: allViewLayouts[id]; active: allEarlyLoadList[id] }` | FrameWork.qml 978–1090행 |
| 화면 ↔ QML 경로 | C++가 `allViewLayouts`로 넘긴다. 정적으로는 확정하지 못했다. 기기에서 `json`으로 덤프한다 | — |
| 화면 정의 | `system/vendor/sony/bin/ScreenSettings.json`: window 10, screen 148(→ viewmodel 이름, window, layer, earlyload), transition 113, popup 57, view 17, convert, alias, style(글자 크기·색 토큰), effect | JSON |
| 값 전달 (VM → View) | VM 시그널 `updatedData(map)` / `updatedProperty(name)` / `updatedPropertyData(name, data)` → `FrameWork.qml`의 `Binding`(screenBinder)이 `item[name] = value`. 앱은 `QObject::setProperty`도 `QQmlProperty`도 import하지 않는다 | FrameWork.qml 1011–1090행, 앱 import |
| 이벤트 (View → VM) | 문자열 기반 connect. 예: View 시그널 `2shuffleClicked()` → VM 슬롯 `1OnShuffleClicked()`, `2released(int)` → `1OnScrubBarReleased(int)` | strings 0x3fa17d 주변 |
| 함수 호출 (VM → View) | VM 시그널을 QML 함수에 connect. 예: `2notifySeekError()` → `1onNotifySeekError()` | 같은 곳 |
| 자식 아이템 | objectName + `findChild`. 예: `swipeSelectorShuffle`, `swipeSelectorTimeScrubBar` | strings, `qt_qFindChild_helper` import |
| 동적 스코프 | 화면 QML은 `viewstyle.*`, `screen_info.*`(FrameWork 루트 프로퍼티)와 Loader의 `controller`, `viewModel`을 이름으로 참조한다 | QML 스캔 (`viewstyle.empty_string` 442회) |
| 번역 | 화면 문자열은 메시지 ID(`qsTrId`)다. `/system/vendor/sony/translations/HgrmMediaPlayerApp_<언어>.qm` 13개 언어 | 앱 import, strings |
| 폰트 | `vendor/sony/lib/fonts/`: SST, SST UI, SST Japanese Pro, Noto Sans KR/Thai, DF 계열. `QFont::insertSubstitutions`로 `SSTCJK` 대체 그룹을 만든다(추정) | strings |
| Sony Qt 차이 | `QQuickItemView`에 `modelReset()` 시그널이 추가돼 있다. `QQmlEngine::setUrlInterceptor`가 있다(5.4 기능의 백포트) | Sony Qt 5.3.2 소스 `qquickitemview_p.h`, libQt5Qml dynsym |

펌웨어는 A50 시리즈용 1.02(`ro.sony.version=1.02`, swid `03.01.E.1.02.00`)이다. 하지만 userland는 플랫폼 공용 빌드라서(`ro.product.model=HAGOROMO_MODEL`) A55 전용이 아니다.
`libDmpFeature.so`에 NW-A30/A40/A50Series, NW-ZX300/ZX300A, NW-WM1A/WM1Z, DMP-Z1 여덟 모델이 들어 있고, 앱의 QML도 이 모델들의 화면을 전부 담고 있다.
무엇이 보일지는 실행 중에 정해진다. 근거는 `IsModel`/`IsShip`(동영상은 판매 지역으로 갈린다, 역어셈블), 서비스의 하드웨어 능력(`NcAsmService::GetCapability` import)이다.
반면 `IsFeaturedNoiseCancel()`은 이 빌드에서 무조건 참을 돌려준다. 시뮬레이터 카탈로그는 이런 판단 없이 모든 화면을 렌더하므로 A55에 없는 화면(예: 4.4mm 밸런스 출력 설정)이 섞인다.
A55에서 실제로 쓰이는 화면은 기기 덤프로 가린다.

C++ 쪽은 moc 클래스 409개다(`tools/mocdump.py`). 그중 `StandardViewModel` 하위가 52개, `AbstractServiceModel` 하위가 40개다.
각 클래스의 프로퍼티(타입, NOTIFY), 시그널, 슬롯, invokable 시그니처를 인자 이름까지 얻을 수 있다.

## 2. 교체 계약

`tools/qmlcontract.py`가 QML 파일마다 C++이 의존하는 이름을 뽑는다. 계약 바깥은 자유롭게 바꿔도 된다.

- `props`: 루트 프로퍼티 중 어떤 moc 클래스에도 같은 이름이 있는 것. Binding이 이 이름으로 값을 쓴다.
- `signals`: 바이너리가 `SIGNAL("2이름(")`으로 connect하는 루트 시그널.
- `calls`: 바이너리가 `SLOT("1이름(")`으로 부르는 루트 함수.
- `objects`: 바이너리에 같은 문자열이 있는 objectName. `findChild` 대상일 수 있다.

이름을 화면별이 아니라 바이너리 전체에서 찾으므로 과대 추정이다. 남는 쪽이 안전하다.
218개 파일에 계약이 있고, 합계는 props 600, signals 577, calls 76, objects 51이다. 예:

```
sid_0201_player/MusicPlayerDefaultForSimpleMode.qml (ScreenBase)
  props    meta_data total_playback_time currently_playing_time currentry_playtrack_num total_playtrack_num repeat_mode is_shuffle output_device_connect entry_id update_db_counter is_activated
  signals  pressed released shuffleClicked repeatClicked
  calls    onNotifySeekError changeSimpleModeBlindStatus changeSeekBarBlindStatus forceBlind
  objects  swipeSelectorRepeat swipeSelectorShuffle swipeSelectorTimeScrubBar
```

## 3. 리소스 오버레이

Qt 5.3의 `QResourcePrivate::load()`는 등록된 루트를 순서대로 보고, 경로를 가진 첫 루트의 데이터를 쓴다(`qresource.cpp`). 앱의 루트는 static init에서 등록된다.
`overlayStart()`는 이 순서를 다음과 같이 바꾼다.

1. 실행 파일의 쓰기 불가 PT_LOAD에서 rcc 루트 노드를 찾는다. 이름 테이블은 qt_hash로, 데이터 테이블은 크기 체인으로 검증한다. 0 정렬 패딩도 허용한다.
2. 찾은 루트가 실제로 등록된 것인지 확인한다. `QResource`가 그 데이터 테이블의 정확한 포인터를 돌려주는 경로가 하나라도 있어야 한다. 아니면 건드리지 않는다.
3. 오버레이와 경로가 겹치는 루트만 내렸다가, 오버레이를 등록하고, 다시 올린다.
4. 결과(교체, 추가, 실패 수)를 stderr의 `[wmmod]` 줄과 probe `overlay` 명령으로 남긴다.

| 항목 | 값 |
|---|---|
| 오버레이 파일 | `/contents/wmmod/overlay.rcc` (환경변수 `WMMOD_OVERLAY`로 변경) |
| 끄기 | `/contents/wmmod/disable` 파일을 만든다(USB 저장장치로도 가능) |
| 읽는 시점 | `platformInit()`. QML 엔진이 어떤 파일도 읽기 전이므로 `FrameWork.qml`도 교체된다 |
| 메모리 | 파일을 시작할 때 한 번 메모리로 읽는다. 이후 `/contents`가 MSC로 내려가도 무관하다 |
| 실패 가드 | 오버레이를 적용할 때마다 `/contents/wmmod/badboots`를 1 올리고, 이벤트 루프가 60초 돌면 0으로 되돌린다. 3이 되면 오버레이를 건너뛴다. 깨진 오버레이가 watchdog 재부팅 루프를 만들어도 세 번째 부팅부터 순정 UI로 돌아온다 |
| probe | `/contents/wmmod/probe` 파일이 있거나 `WMPROBE=1`일 때만 켜진다 |

검증 범위:
- 호스트 하니스(`probe/hosttest/run.sh`, 54개 항목)에서 교체, 중첩 경로 교체, 추가, 오버레이 QML에서 앱 파일 참조, 실패 가드, probe 플래그를 확인했다. 스크린샷도 확인했다.
- C++ 파서를 기기 바이너리에 직접 돌린 결과가 `qrcdump.py`와 같다. 루트 38개, 파일 1123개이고 tree/names/data 오프셋까지 일치한다.
- 기기 미검증. Qt 5.3 + 실제 앱에서의 동작은 `docs/DEVICE-STEPS.md` 4단계에서 확인한다.

### 영구 설치 (설계, 미구현)

Wampy는 두 곳에 쓴다. `libqeglfs.so`를 `/system/vendor/sony/plugins/platforms/`에 복사하고(원본은 `libqeglfs.so_vendor`로 백업),
자기 데몬을 띄우려고 boot.img 램디스크의 `init.rc`를 고친다(`wampy/installer/run.sh`). 오버레이는 순정 앱이 로드하는 플러그인 안에서 돌므로 앞의 것만 있으면 된다.
**bootimg는 쓰지 않는다.** 설치 UPG는 플러그인 교체와 백업을, 제거 UPG는 백업 복원을 맡는다.
system 쓰기이므로 ROADMAP 규칙대로 복구 경로 판정 뒤에, 그리고 bind mount 시험(DEVICE-STEPS 3–4단계)으로 플러그인이 기기에서 검증된 뒤에 만든다.
그 시점의 복구 수단은 세 가지다. `disable` 파일, `badboots` 가드, 그리고 제거 UPG다. 제거 UPG는 W1 업데이터가 앱과 무관하게 실행하므로 UI가 깨져도 쓸 수 있다.

## 4. 모드 작업 흐름

```
mods/NAME/patches/*.patch   추출 qrc 트리에 대한 unified diff (a/경로, b/경로)
mods/NAME/files/...         새 파일이나 통째 교체 파일 (qrc 경로 그대로)

tools/mkmod.sh mods/NAME                     # patch + qmllint → work/mods/NAME/overlay.rcc
tools/uisim.sh --mod mods/NAME SPEC.json     # 호스트 렌더 → work/uisim/out/SPEC.png
tools/devmod.sh mods/NAME                    # 기기에서 앱 재시작 + 오버레이 (SD 카드가 빠진다)
tools/devmod.sh hot mods/NAME                # 실행 중인 앱에 바로 반영 (§7)
```

Sony 원본 QML은 저장소에 넣지 않는다. 원본은 `tools/qrcdump.py`가 기기 바이너리에서 `work/qrc`로 뽑는다. 저장소에는 우리 변경분만 남는다.
예제 모드는 두 개다. `mods/hello`는 화면 왼쪽 아래에 `wmmod` 표식을 띄운다(`FrameWork.qml` 패치). `mods/accent`는 플레이어 제목을 강조색으로 키운다.

## 5. 호스트 UI 시뮬레이터

`tools/uisim.sh`가 `probe/uisim`을 Debian Qt 5.15 + Mesa 컨테이너에서 빌드해 화면을 렌더한다.

- `Preview.qml`이 `FrameWork.qml` 루트의 `viewstyle`, `screen_info`와 화면 Loader의 `controller`를 흉내 낸다.
- 전역 컨텍스트 4개는 `tools/uisim_stubs.py`가 moc 데이터로 QML 스텁을 만든다. `styleProperties`는 기기의 ScreenSettings.json style 섹션이다.
- 기기 폰트와 번역(`--lang`, 기본 en_US)을 읽는다.
- `probe/uisim/hostfix`가 Sony Qt에만 있는 `modelReset` 핸들러를 호스트에서 지운다. 이 패치는 기기용 오버레이에 들어가지 않는다.
- `sid_*` 화면 94개가 모두 로드된다. 설정 목록, DAC 필터, NC, 날짜 피커, FM, 녹음 등이 영문으로 렌더된다.

한계:
- 기기와 그래픽 스택이 다르므로 레이아웃 미리보기 수준이다(기기: Qt 5.3 + Mali).
- `/opt2/appresouce` 이미지(재생 버튼 등)가 없다. 백업 뒤 해결된다.
- 헤더, 내비게이션 바, 화면 값은 손으로 쓴 값이다. 기기 probe의 `json` 덤프로 바꿀 예정이다.
- QML은 대문자로 시작하는 메서드를 선언할 수 없다. 그래서 `ReconfigureScreenByWindow` 같은 슬롯 스텁이 없고, 클릭 동작은 재현하지 않는다.
- `appFont.fontFamily = "SST UI"`는 추정이다.

## 6. 기기에서 확인한 것 (2026-10-10)

`docs/DEVICE-STEPS.md` 3·4단계, 도구는 `tools/devmod.sh`.

1. 기기 앱은 W1의 BBDMP2 빌드다(BuildID `a468f8c7…`, `ANALYSIS.md` §11). moc 덤프는 A50 앱과 같고 qrc 1121개 중 11개만 다르다.
   `mkmod.sh`는 이제 `work/qrc-device`를 기본 원본으로 쓴다. 시뮬레이터(`uisim.sh`)는 아직 `work/qrc`를 쓴다.
2. 오버레이가 기기에서 동작한다. `hello`: `1 files, 1 replaced, 0 added, 0 not served; 38 app roots, 1 moved behind`, 표식이 화면에 보인다.
3. `screenController` 덤프는 `work/device/probe/`에 있다. `allScreenIDList`는 148개로, 모델별로 거른 목록이 아니라 등록 전체로 보인다.
   A55에서 실제로 쓰이는 화면은 화면 전환 로그(`ScreenTransitionController`)나 probe로 따로 확인해야 한다.
4. `appFont.fontFamily`는 `SST`다(시뮬레이터 `globals.json`도 고쳤다). 모드의 `Text`는 반드시 `appFont.fontFamily` 같은 앱 폰트를 지정한다.
5. 남은 것: 영구 설치 때 `/contents` 마운트와 앱 시작의 순서. USB 연결 중 재시작하면 `/contents`가 내려가 있으므로 개발 중에는 `/tmp/wmmod`를 쓴다.

## 7. 핫 리로드 (2026-10-11)

`tools/devmod.sh hot mods/NAME`은 모드를 빌드해 `/tmp/wmmod/hot.rcc`로 올리고 probe의 `reload`를 부른다. 앱은 재시작하지 않는다.
`reload`는 오버레이를 갈아 끼운 뒤 `wmReloadToken`을 가진 QML만 다시 만든다. 화면과 창 아이템은 앱 C++가 포인터로 쥐고 있어서 밖에서 다시 만들 수 없기 때문이다.

### 모드 쪽 규약

- 화면 파일은 **껍데기**로 둔다. 교체 계약(루트 프로퍼티, 시그널, 함수, objectName 자식)과 C++가 찾는 아이템만 남기고,
  보이는 부분은 `components/WmHot.qml`(`Loader { property int wmReloadToken; property url file }`)이 읽는 `*Body.qml`로 옮긴다.
  예: `sid_0201_player/MusicPlayerDefault.qml` → `WmMusicPlayerBody.qml`, `components/WmPlayerControls.qml` → `WmPlayerControlsBody.qml`.
- 본체에서 껍데기를 부를 때는 id로 부른다. 본체의 컨텍스트 객체는 Loader라서, 한정하지 않은 이름은 Loader 프로퍼티가 먼저 잡는다.
- 껍데기가 본체 안 아이템을 써야 하면 본체가 프로퍼티로 내놓는다(`property Item scrubArea`). 껍데기 쪽 `findChild` 대상은 껍데기에 남긴다.
- 껍데기 자체를 바꾸면 앱을 재시작해야 한다(`devmod.sh mods/NAME`).

### reload 한 번에 일어나는 일

1. 오버레이 교체. 바뀐 경로(`changed`)와 앱에 처음 생기는 경로(`new`)를 응답에 적는다.
2. 모든 `wmReloadToken`을 -1로 둔다. `WmHot`은 `source`를 비우고, 옛 아이템과 컴포넌트는 deleteLater로 간다.
3. 50 ms 뒤 지연 삭제를 처리하고, 이미지 캐시를 비우고, `trimComponentCache()`로 살아 있는 인스턴스가 없는 컴파일 결과만 버린다.
4. 토큰을 이전 값+1로 둔다. Loader가 새 오버레이에서 파일을 다시 읽는다. 응답은 `refreshed N: 파일들`이다.

### clear가 아니라 trim인 이유

처음에는 `clearComponentCache()`를 썼다. 기기에서 reload 약 1분 뒤 watchdog 재부팅이 났다(`work/device/hot/freeze-1249/`의 core 덤프와 logc14).
원인은 Qt 5.3의 `QQmlBinding::createBinding`이다. 바인딩이 있는 `PropertyChanges`가 붙은 State가 적용될 때 컴파일 결과를 URL로 다시 찾는데,
clear 뒤에는 새로 컴파일됐지만 아직 링크되지 않은 결과가 나오고 `runtimeFunctions[...]`가 NULL이라 `createScriptFunction(NULL)`에서 SIGSEGV가 난다.
Qt 5.15는 이 조회를 하지 않아서 Debian Qt 하니스는 통과했다.

trim은 컴파일 결과의 참조 수가 1인 것만 버린다. 살아 있는 인스턴스는 자기 파일의 컴파일 결과를 붙잡고 있으므로(`qqmlobjectcreator.cpp` 235행),
살아 있는 객체의 파일이 두 번 컴파일되는 일이 없다. 다만 로딩 중인 파일은 의존 파일을 캐시 밖에서 붙잡고 있어서 이 보장이 깨진다.
그래서 Loader가 `Loading` 상태이면 reload를 거절한다(`not now, still loading: ...`, devmod는 1로 끝난다).

### 한계

- 토큰 밖에서 아직 쓰이는 파일은 옛 컴파일 그대로 남고, 다시 만든 본체도 그 버전을 쓴다. 응답의 `kept, still in use outside any wmReloadToken: 경로 (n objects)`가 그 목록이다.
  Qt 5.3은 루트에 프로퍼티, 시그널, 함수 선언이 있는 파일에만 그 파일 이름의 클래스를 만든다(`qqmltypecompiler.cpp` 489행).
  그래서 선언도 자식도 없는 잎 파일은 목록에 나오지 않는다. 나오지 않아도 남아 있는 것은 같다.
- 타입 로더는 디렉터리 목록을 캐시한다. 이미 읽힌 디렉터리에 새로 생긴 파일은 URL로는 찾지만 타입 이름으로는 앱을 재시작해야 찾는다.
- `.js` 스크립트 캐시는 trim 대상이 아니다.

### 검증

- 호스트 하니스(Debian Qt 5.15, `probe/hosttest/run.sh`) 84개 통과.
- ARM 하니스(`probe/build.sh hosttest` + `probe/hosttest/run-arm.sh`) 28개 통과. 기기 system 파티션의 Sony Qt 5.3 라이브러리로 하니스를 qemu-arm에서 돌린다.
  clear로 되돌리면 State 시험에서 앱이 죽는다(`FAIL app survives the reload checks`). 로딩 가드를 끄면 `FAIL no reload while a Loader is loading`.
- 기기: 재생 버튼 색과 폭을 바꿔 hot reload하자 같은 pid로 바뀌었고(`work/device/hot2/03-hot-green.png`), 되돌리는 reload도 반영됐다(`work/device/base2/c08-reverted.png`).
  reload 뒤 컨트롤 탭과 Sony 다이얼로그 열기/닫기를 반복하고 몇 분을 두어도 앱이 살아 있었고, 새 core 덤프나 logc 시그널 기록은 없었다.
- 플레이어 화면(`WmMusicPlayerBody.qml`)은 SD 카드가 빠진 상태라 열지 못했다. reload 응답의 `refreshed` 목록에는 들어 있다.

### 따로 발견한 것

probe `drag`로 홈 화면 페이지를 넘기면 페이지 애니메이션이 이벤트가 들어올 때만 한 칸씩 진행하고, 그동안 탭이 버튼에 닿지 않고 맨 아래의 `SwipeGrid` MouseArea로 간다.
reload 없이 재시작한 앱에서도 똑같이 재현된다(`work/device/base2/`). 원인은 아직 모른다.
