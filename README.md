# walkman-a55

Android 5.1 on the Sony NW-A50 series (NW-A55, NW-A56, NW-A57), next to Walkman OS on the
same player. A picker at power-on starts either one: volume keys move, power or play
selects. Android runs as a container on Sony's own kernel, which stays as Sony signed it;
nothing but Sony's one-line boot hook is changed on the player.

This repository ships no system image. LineageOS 12.1 for the Fire 7 (`ford`) and the
Walkman's own firmware come from each owner, are put together on that owner's computer, and
are never published here.

**Alpha: not a stable release.** It has run on one NW-A55, its author's, and nowhere else.
Expect crashes, missing pieces and changes from one version to the next, and do not install
it on a player you cannot do without. If an install goes wrong, getting the player back can
take the boot ROM path in [`docs/INSTALL.md`](docs/INSTALL.md#when-something-goes-wrong).
It comes with no warranty (sections 15 and 16 of the [GPL](LICENSE)).

[`docs/INSTALL.md`](docs/INSTALL.md) has the steps and what has not been run yet;
[walkman-manager](https://github.com/RedHatOnTop/walkman-manager)'s Firmware Studio drives
the same kit.

## What it does

| | |
|---|---|
| Kernel | Sony's 3.10.26, untouched. What Android needs beyond it comes as modules built from Sony's GPL tree: Mali r4p0, the MT8590 Wi-Fi driver on backports 3.17, and a guard for a swap bug in the stock kernel. |
| Container | LineageOS 12.1's userland in a container of its own (`probe/android/`), started from the player itself once its files are on internal storage. |
| Picker | `bootpick`, drawn straight to the panel in the black and white of the stock settings list, run by a hook in place of Sony's `bootswitcher.sh` (`picker-upg/`; `picker-remove-upg/` puts Sony's back). |
| Music | WalkmanUI: the stock Walkman player's screens from the owner's firmware, on Qt 5.15 for Android with our own C++ behind them (`android-ui/`). Audio through the CXD3778GF DAC with our audio HAL (`probe/audiohal/`). |
| Browser | A one-tab browser on the image's system WebView (`android-browser/`), in place of the image's own. |

## Layout

| Path | Contents |
|---|---|
| `probe/android/` | The container's start, stop and power handling on the player, `pivotinit`, overlays. |
| `probe/kmod/` | Kernel modules and the patches they are built with. |
| `probe/devtools/` | Static device helpers, `bootpick` among them. |
| `android-ui/` | WalkmanUI and the patches it applies to the stock screens. |
| `android-browser/` | The browser. |
| `kit/` | The install kit: the set from the owner's backup, the ford zip and Fire OS (`make-install.sh`), and our binaries for the prebuilt release (`make-prebuilt.sh`). |
| `probe/wallpaper/`, `android-ui/launcher_icon.py` | The default wallpaper and WalkmanUI's icon, drawn on the owner's computer from their backup. |
| `adb-upg/`, `backup-upg/`, `picker-upg/`, `picker-remove-upg/` | Script-only firmware updates for Sony's own updater (`tools/build_upg.sh`, `tools/nwupg.py`). |
| `probe/build.sh`, `probe/mkspec/`, `mods/` | A plugin and UI mods for the stock Walkman app. |
| `tools/` | Host side: the container's assembly and start, image steps, UPG packing. |
| `docs/`, `devlog/` | Notes, plans and what was tried, by date. |

## Licensing

Our code and documentation are licensed under the **GNU General Public License v3.0 or
later** ([`LICENSE`](LICENSE)).

The kernel modules in `probe/kmod/` are built from, and patch, GPL-2.0 kernel code (Sony's
and MediaTek's), and are licensed like it: **GPL-2.0** ([`probe/kmod/COPYING`](probe/kmod/COPYING)).
`probe/mkspec/qeglfshooks_mt8590.cpp` is Sony's file from its Qt 5.3.2 source release with our
changes; it keeps Sony's notice and is under GPL 3.0, one of the two licences that release
offers. The patches in `android-ui/patches/` apply to the screens of the owner's own
firmware and carry the lines around each change as diff context, and
`mods/expressive/files/sid_0201_player/MusicPlayerDefault.qml` keeps the property and signal
names of Sony's screen so the music app binds it. Beyond these and the GPL kernel code in
`probe/kmod/`, this repository holds none of Sony's or Amazon's files.

Files from other projects keep their own notices: the Android headers in
`probe/audiohal/aosp-include/` and `android-ui/audiofx/aosp/` (Apache 2.0; tinyalsa's under
its BSD licence) and Qt's `probe/mkspec/qplatformdefs.h` (LGPL 2.1 or GPL 3.0).

Sony, Walkman and the Walkman logo are trademarks of Sony Group Corporation. This project is
not affiliated with or endorsed by Sony or Amazon.
