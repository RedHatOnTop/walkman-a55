# Walkman Manager desktop app — rough scoping brief

High-level only (user: "대략적으로만"). For a **separate Opus session** to pick up and build. Working
name only; it's an original third-party tool for the user's own Walkman(s), not a Sony product and
not affiliated with Sony — naming/branding TBD, and any distribution avoids implying affiliation
(see [[nw-a55-distribution-policy]]).

## Idea

The "ultimate" Walkman manager — the desktop app Sony never made (Media Go / Music Center for PC
were closed, clunky, format-limited, and obviously did no custom-firmware or hardware tuning). One
app that turns a stock Walkman into our custom-Android player **and** manages music + audio +
device, end to end. This is the endpoint of the "user only has to supply firmware; everything after
is as easy as possible" principle — the GUI over the nw-a55 pipeline.

## Stack

Rust + Tauri v2 (frontend TS + React; Three.js only if a device/visual flourish earns it) — same
family as the user's other desktop apps. Shape it as **Rust core library + Tauri shell**: the core
wraps every nw-a55 operation so a CLI and the GUI share it. Keep the existing nw-a55 tools
composable as that backend rather than reimplementing them.

## Scope pillars (rough, priority order)

1. **Custom-firmware builder (highest leverage — reuses the most).** User points it at (a) their own
   Walkman stock firmware and (b) the public LineageOS "ford" ROM; the app builds the customized
   image (debloat, audio HAL, default wallpaper, boot animation, B&W skin) and drives the flash. It
   is a GUI over what we do by hand now: `cm_debloat_debugfs.py`, `probe/audiohal/build.sh`,
   `cm_customize.py`, the `gen_*` generators, and the debugfs recipes for framework-res / wallpaper /
   bootanim. Include the distribution "no-marks" toggle. Flashing stays **operator-gated** per the
   project's device rules.
2. **DAC / audio control.** Expose the CXD3778GF mixer we mapped (S-Master route, the gain stages,
   the proven unmute/route sequence from [[nw-a55-audio-dac-alsa]]) as live hardware tuning the
   Walkman never offered on desktop.
3. **Library / music manager.** hi-res / DSD / FLAC, playlists, gapless, ReplayGain, cover art,
   drag-drop sync — the parts Sony's software did badly.
4. **Device manager.** Backup / restore, flash, patch management, live skin/wallpaper apply, logs.
5. **(stretch) Multi-device.** Extend to the ZX507 (the A13 Walkman project) → one unified manager.

## Principles / guardrails

- Distribution policy holds: ship tools + our originals, **never firmware/vendor blobs**; the user
  self-sources (their Walkman firmware + the public ford ROM); distribution builds strip brand marks.
- Device writes stay behind the existing gate (operator-run flashing; the app stages + verifies, the
  operator confirms the actual write).
- Backend = the nw-a55 tooling, kept composable; the app is the shell, not a rewrite.

## Start where

Pillar 1 (the builder) first — it's the most leverage and reuses the existing pipeline directly.
Everything else layers on once a stock Walkman can be turned into the custom player from the GUI.
This brief is intentionally rough; the separate session should flesh out architecture + milestones.
