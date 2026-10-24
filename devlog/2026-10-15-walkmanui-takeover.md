# WalkmanUI takeover: the stock app as an Android app

2026-10-15. The Codex continuation (`devlog/2026-10-15-walkmanui-playback.md`) was replaced. The
user's requirements: W.music-smooth animation, every screen and swipe gesture, and the stock
bottom navigation bar replaced by buttons elsewhere, because a bar above Android's own bar looks
wrong. Earlier direction for the same app still holds: port the Sony QML as it is and do not
invent UI. Current architecture and status: `docs/walkmanui-implementation-plan.md`.

## Approach

- Sony's views bind to ViewModels by name only, so the app keeps every Sony QML file and supplies
  a C++ `ScreenModel` per view plus a `ScreenController` that reads the stock
  `ScreenSettings.json` transition table. Codex's hand-written player spec and stub loader were
  dropped; their sources are kept outside the repo in `work/codex-walkmanui-leftovers/`.
- The navigation bar first became a 40 px action row under the 66 px header. The user then
  pointed out that screen space is short (the device stage is 480x728 once Android's bars are
  taken), so its buttons moved into the header's volume row, into the slot the A55 never uses
  and the row's right end, and the empty status icon row folds: 52 px back on every screen
  (patch 0001). Back is Android's key. Everything that drew or pointed at the old bar followed:
  the library help guide (patch 0003, its picture cut into bands), the text entry popup (patch
  0004), the details and lyrics popups, and the empty bookmark/playlist guide picture (redrawn
  at stage time).
- The user then asked for the volume bar section to go and pointed at the A55's own UI: the
  probed tables came from a Walkman One unit, which reports NW-WM1Z, so the header was the WM1
  series' (a 54 px volume area for its taller screen). The header now runs the stock QML's
  other branch, `useVolumeArea: false`: the A55's info row (playback state at the left, the
  volume where the NW-A50 Help Guide draws it) with the three buttons at its right end, where
  the A55's battery sat. The row is 40 px, the stock bar's height, so screens start 40 px down
  (54 before). The volume popup and text entry popup start under it (patch 0004, renamed
  `0004-popups-under-header.patch`). The sound pages keep the WM1 series' ten-band equalizer
  and tone control on purpose (user: the A55's fewer bands were software discrimination).
- Settings then lost what cannot work in an Android app (user): brightness, screen timeout,
  USB connection, auto power off, clock, keyboard choice, high gain output, DSD playback
  settings, storage format and factory reset. Stock flags where they exist, patch 0006 for
  the rest; the screens stay in the table, unlinked.
- 2026-10-16, Settings kept going: the items that stayed but only stored a value now act.
  AVLS and Playback Range moved into the player beside shuffle and repeat (`playback/avls`,
  `playback/allRange`); All Range follows the stock text ("continue with the next list after
  finishing the list that includes the selected song"), taking "the next list" from the level
  the list was picked on. Artist List Display picks the album artist or song artist grouping;
  Keep Screen On sets Android's keep-screen-on flag while the lyrics popup is open.
- Transitions: a finger-driven change always slides with the finger (W.music ViewPager slop,
  fling distance and velocity, settle capped at 600 ms). Button changes with a direction slide too;
  the stock effect table only offered fades for most of them.
- The stock table marks the player's swipe neighbourhood `earlyload`; the port now builds those
  views in the background after the first screen, as the stock app did at startup.
- Settings and sound pages came from a Sonnet worker and were reviewed before landing. Its own
  icon chooser duplicated the library one already in place (and used another prefs key), so it
  was dropped; its preset flow parsed dialog text to work around unmapped message ids, so those
  ids got their stock dialog types and the view's own questions run instead.
- Sound page swipes: the table lists every model's pages under numbered conditions; the port
  picks the A50 series' order. The "which page was shown last" number is only for the player's
  swipe up.
- Lyrics: no stock ViewModel survived, so `LyricDispPopup` is driven from what the view asks for
  (three empty rows before and after, `lyricDisp*` calls, `indexJump`, `scrolled`).

## Qt 5.3 to 5.15 differences found on the way

- A state machine whose two states' `when` clauses read one property passes through the `""`
  state in 5.15, so `Transition { from: "inactive" }` never matched. Library icon drag-and-drop
  hid the held icon without lifting it and the pages kept flicking. Fixed in `stage.py` by
  matching any source state (only `LibraryIcon.qml` depends on it; `BasicPlayerControls.qml`
  only loses a no-animation first transition).
- `Image.sourceClipRect` needs `import QtQuick 2.15`; the stock files import 2.3.
- A Qt 5 `MouseArea` has its own `enabled`, separate from the item's. The swipe recogniser's hit
  test took the frame's disabled transition guard as the pressed item, so no list or slider ever
  blocked a screen swipe; a drag on the lyrics list swiped the player to the equalizer.
- The stock `TimeOutDialog` runs its `Timer` with the interval it is given, and its comment says
  0 means no timer. Qt 5.15 fires a 0 ms Timer at once, so the "Saved." notice closed as it
  opened; the dialog manager now gives it 2 s.
- The software scene graph keeps a stale `layer.enabled` texture: a dropped library icon kept its
  pressed background on the host. The OpenGL backend and Android draw it correctly.

## Android and Waydroid notes

- Waydroid here is Android 13 x86_64 in Samsung M1 mode (`gts9`). `screencap` is forbidden in it
  (it killed system_server before), so the app grabs its own window: the test driver reads a
  script from the app's external files directory and logs through `qInfo` (logcat).
- `run-as` fails in the container (`setegid(AID_PACKAGE_INFO) failed`). Files pushed by `adb`
  into `Android/data/org.walkmanui/files` are owned by `shell` with mode 0770 dirs, so the app
  cannot read them until `chmod -R a+rwX`. The app also cannot list `Android/data` on Android 13,
  so music pushed there is not scanned; shared storage files are.
- A force-stop during the storage prompt leaves the permission activity in the app's task; a
  relaunch only brings that task forward. Answering it (`rig/uitap.py tap '^Allow$'`) clears it.

## Evidence

- Waydroid: tap a song, position 00:02 to 00:06, media session pushed to PLAYING; swipe down
  58 frames (median 16.6 ms, worst 32.5 ms); horizontal swipes 54 and 55 frames (median 16.5 ms,
  worst 17.2 ms, none over 25 ms); Back returns through history.
- Waydroid, after measuring frames with the window drawing continuously (the first numbers
  timed idle gaps between gestures as 340 ms frames): player to sound page plus five sound
  swipes, 425 frames, median 16.7 ms, 3 over 25 ms (worst 37 ms) on the first run after install;
  two right swipes and a swipe down plus left, 141 frames each, worst 17.6 ms, none over 25 ms;
  lyrics drag, 111 frames, worst 17.5 ms, none over 25 ms.
- Host tour of every screen: 111 of 111 grabbed, no refusal, no crash.
- Host at the device's stage height (728), with the buttons in the header: tour 111 of 111;
  the player, library, queue, bookmarks, language list and every sound page from the player fit
  above the player controls, and on Waydroid's shorter window (674) the sound pages fit too.
- `tst_lyrics` 21/21, `tst_tagreader` 49 fixtures.

## Open

- Nothing has run on the NW-A55 itself (API 22, armeabi-v7a). The device APK is built
  (`build.sh android armeabi-v7a`); flashing it is the operator's step in the implementation doc.
- Sound effects are applied on the NW-A55 image from 2026-10-16
  (`devlog/2026-10-16-walkman-sound-effects.md`), not yet heard on the device.
