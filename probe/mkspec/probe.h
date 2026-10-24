// In-process inspector for HgrmMediaPlayerApp, loaded as part of the eglfs
// platform plugin. Listens on 127.0.0.1:PROBE_PORT (reach it with
// `adb forward tcp:7777 tcp:7777`) and answers line-based text commands about
// the app's windows, QML item tree and QObjects. Type `help` for the list.
// It only starts when overlayDir()/probe exists (overlay.h) or WMPROBE=1.
//
// Everything runs on the GUI thread, which the system watchdog monitors: a
// main thread that stops answering reboots the device, so every command must
// finish quickly and output is capped.
//
// `reload PATH` swaps the resource overlay (overlay.h) without restarting the
// app. Objects the app's C++ holds, screens and windows among them, cannot be
// recreated from outside, so a reload only reaches QML that asks for it with
//   property int wmReloadToken
// The probe sets every such property to -1, lets the old objects go, drops
// what the engines compiled from files that nothing uses any more, and the
// cached images nothing shows, and then sets it to a new positive value. A
// Loader whose source is empty while the token is negative therefore loads
// its file again from the new overlay; components/WmHot.qml in the mods is
// that Loader. A changed file that something outside the tokens still uses
// stays as compiled, for the reloaded objects too, and the reply says so
// for the files it can tell are in use (see cmdReload for the gap).
// The type loader also keeps its directory listings, so a type name only
// finds a file new to such a directory after the app restarts.

#ifndef PROBE_H
#define PROBE_H

#define PROBE_PORT 7777
#define PROBE_MAX_LINES 20000
#define PROBE_RELOAD_TOKEN "wmReloadToken"
// Time for the old objects' deferred deletes before the caches are dropped.
#define PROBE_RELOAD_SETTLE_MS 50

void probeStart();

#endif
