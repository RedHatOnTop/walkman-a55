# SensMe's channel limits come from the owner's firmware

The owner, 2026-10-24: the values that decide SensMe channels must not be public; counts and
structure may be. `android-ui/library/sensme.cpp` had the media store's channel tests with
their limits written in, and walkman-a55 had carried that file since its first tree. The
centroids, scales and the two cluster tables were already read from the owner's
`libMediaStoreService.so` by `android-ui/stage.py`.

walkman-a55 went private at 14:04:28 KST the same day (anonymous downloads then answered
404) until a tree without the limits replaced its history.

## What moved

- `stage.py` reads the 68 immediates of the media store's channel tests (the Thumb-2 function
  at file offset 0x48dac of the 2019-07-31 build, md5-pinned as before) and appends them to
  `res/sensme.bin`, one byte each, in the order `rule()` reads them: 6,660 bytes become 6,728.
  A 16-bit `cmp`/`subs` carries the value in its low byte, a 32-bit `cmp.w`/`sub.w` as a
  Thumb-2 modified immediate; any other instruction at an offset stops the staging.
- `sensme.cpp` keeps which word each test reads and how it compares, and takes the number
  from the model. Where the library compares the other way round (`>= n` for `> n - 1`) the
  test now reads like the library, so the limit is the immediate as it stands. Two tests are
  ranges in the library, `(v - lo) < n` unsigned, and read that way (`within()`); three
  compare the cluster number.
- `loadModel()` takes only the new size, so an image's `sensme.bin` and its WalkmanUI change
  together; the kit writes both.

The offsets came from walkman-manager's session, which had mapped the same function for its
own copy of the classifier (both builds of the library it checked agree), and each was matched
here against the test it belongs to before it was used.

## Checks

- The old `rule()` and `channels()` against the new ones, built together on the host with the
  old 6,660-byte model and the new 6,728-byte one: 400,000 feature sets (uniform bytes, the
  fuzz range, a 3 % missing rate), every test 0..7 and 9 for every cluster 0..49 and the
  whole classification: 180,000,000 rule results and 400,000 channel masks, 0 differences.
  With one limit raised by one in the model, 1,900 differences in the first 20,000 sets.
- `android-ui/build.sh release`: `BUILD_EXIT=0`, `WalkmanUI.apk` `da1d9d10…`.
- `prebuilt-3` is `prebuilt-2` with that APK; the other eleven files are the same bytes.
- The kit with it (`install16`) against `install14`: three image entries differ,
  `/app/WalkmanUI/WalkmanUI.apk`, its `libwalkmanui_armeabi-v7a.so` and
  `/etc/walkmanui/res/sensme.bin` (6,728 bytes); the boot set and the UPG are the same bytes.

## On the unit

`install16`'s system image pushed in place of `install14`'s (md5 checked on the unit),
`boot completed` at 65.47 s. The image's `/system/etc/walkmanui/res/sensme.bin` is 6,728
bytes, the same bytes the host check loaded. WalkmanUI, driven by its script:

- `go:SensMePlayer` 6 s after launch, before the library had loaded, showed an empty strip
  (`---- / ----`).
- 30 s after launch: Sony's notice that some songs have not been analyzed (020194), then the
  current time slot, Midnight, at 1 / 3, and one swipe on, Energetic (Active) at 1 / 140.
  Both are the channel sizes the cache gave with the old code on 2026-10-19 (Midnight 3,
  Active 140).
