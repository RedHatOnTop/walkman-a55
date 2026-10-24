# 하단 바를 안드로이드 3버튼식으로 (구상, 2026-10-11)

구현은 아직 하지 않았다. 대상은 `components/NavigationBar.qml` 한 파일이다. 30곳이 이 파일을 쓰고, `framework/Dialog.qml`의 다이얼로그 하단 바도 그중 하나다. 그래서 파일 하나를 바꾸면 모든 화면에 적용된다.

## 대응

| 자리 | 모양 | 기존 버튼 (objectName) | 시그널 |
|---|---|---|---|
| 왼쪽 | ◁ 뒤로 | `BackButton` | `back()` |
| 가운데 | ○ 홈 | `FunctionTopButton` (라이브러리 탑) | `library()` |
| 오른쪽 | 지금 재생 중 칩 (아래) | `NowPlayingButton` | `play()` |
| 오른쪽 끝 | ⋮ 메뉴 | `OptionButton` | 옵션 메뉴 다이얼로그 → `option(index)` |

⋮는 안드로이드 2.x–4.x에서 옛 앱에 붙던 메뉴 키 자리다. 화면에 옵션이 있을 때(`properties["OptionButton"]`의 `Enabled`/`Visible`)만 보인다. 머리말로 옮기지 않는 이유는 옵션 다이얼로그를 이 파일이 소유하기 때문이다(`DialogWrapper`, `optionMenuModel`).

○는 라이브러리 맨 위로 간다. 안드로이드 홈과 같은 역할이다. 플레이어로 돌아가는 버튼은 최근 앱 자리에 두지만 □ 글리프는 쓰지 않고 따로 디자인한다(사용자 결정, 2026-10-11).

## 지금 재생 중 칩

- 32px 둥근 사각형(반경 10) 안에 지금 곡의 커버를 넣는다. 소스는 `viewstyle.wm.coverSource`다. 플레이어 화면이 다른 화면 뒤에 있어도 현재 곡을 따라간다.
- 재생 중(`viewstyle.wm.playing`)이면 칩 둘레에 2px `primary` 링을 두르고, 오른쪽 아래 모서리에 막대 3개짜리 레벨 배지(`primary`/`primaryInk`)를 움직인다. 일시정지면 링과 배지 움직임이 멈춘다.
- 커버가 없으면 `surfaceContainerHigh` 바탕에 디스크 글리프를 그린다. 곡이 없으면(`coverSource` 비어 있음) 칩을 숨긴다.
- 플레이어 화면에서는 C++이 이 버튼을 끄거나 숨길 것이다(`Enabled`/`Visible`). 그대로 따르되 자리는 비워 둔다.
- `wm`이 없는 스톡 FrameWork에서는 디스크 글리프만 보인다.

## 배치 (480 × 높이 유지)

- 아이콘 중심은 x = 144 / 240 / 336, ⋮는 444다.
- 터치 영역은 높이 전체를 쓰고 서로 붙인다. 왼쪽 빈자리(0–96)는 뒤로가기 영역에 포함한다.
- 버튼이 숨겨져도 다른 버튼 자리는 그대로 둔다. 안드로이드처럼 위치가 고정된다.
- 높이는 지금 값을 유지한다. 줄이면 창마다 화면 영역 계산(`footerArea` → `reconfigureScreen`)이 바뀌어서 위험이 커진다.

## 모양 (Expressive 플레이어와 같은 언어)

- 글리프는 `WmIcon`으로 그린다. 22–24px, 선 2px, 둥근 이음매.
  - ◁: 왼쪽을 향한 외곽선 삼각형
  - ○: 지름 20px 원
  - ⋮: 4px 점 세 개
- 색은 `viewstyle.wm.surfaceInkVariant`다. 비활성(`Enabled: false`)이면 불투명도 38%다.
- 눌림 표시: 지름 48px 원형 state layer(잉크색 12%)가 100ms에 나타나고, 아이콘이 0.92배로 줄었다 돌아온다.
- 배경: 틴트가 켜진 화면(`viewstyle.wm.tint`)에서는 투명, 나머지에서는 `surface`다. `wm`이 없으면(스톡 FrameWork) 지금처럼 검정이다.
- C++이 넘기는 `normalImage`/`pressedImage`/`disableImage`는 쓰지 않는다. `Enabled`/`Visible`만 따른다.

## 지켜야 할 API

- 루트 타입 `Rectangle`과 `color` (PlayerWindow 패치가 `color: "transparent"`를 준다)
- 시그널 `back()`, `play()`, `library()`, `option(int)`
- `properties`, `optionMenuModel`, `optionMenuOpened`
- alias `backButtonEnabledState` (Dialog.qml이 쓴다), alias `optionMenuDialog` (ContentDetailedInfo, SyncLyricDisp가 `.close()`를 부른다)
- `objectName: properties["objectName"]`
- 네 버튼의 objectName. 이 이름들은 앱 바이너리에 문자열로 들어 있다. 버튼 항목은 `enabled: properties["Enabled"]`, `visible: properties["Visible"]`를 가진 Item으로 남긴다.
- `DialogWrapper`, `onOptionMenuModelChanged`, `onVisibleChanged` 로직은 그대로 둔다.

## 작업 순서

1. `mods/expressive/files/components/NavigationBar.qml`에 전체를 새로 쓴다. mkmod는 mod를 하나만 올리므로 expressive에 넣는다.
2. uisim으로 확인한다. 플레이어 창, 라이브러리 탑, 옵션 메뉴가 열린 다이얼로그를 보고, 프리뷰 JSON으로 `Enabled`/`Visible`을 바꿔 보며 비활성·숨김 상태도 본다. 칩은 재생 중, 일시정지, 커버 없음, 곡 없음 네 상태를 본다.
3. 기기 확인: 하단 바는 창 안에 있어서 핫리로드 계획(화면 본문 Loader)에 들어가지 않는다. 그래서 devmod 재시작이 필요하고, 그때 SD를 재삽입해야 한다.

## 정할 것

- 칩에 진행률을 보여줄지. 하단 바는 재생 위치를 모른다. 넣으려면 플레이어 화면이 `viewstyle.wm`에 진행률을 올려 줘야 한다.
- 길게 누르기에 동작을 붙일지. 쓸 수 있는 시그널은 위 넷뿐이다.
