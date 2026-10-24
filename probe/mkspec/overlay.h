// Resource overlay: files in an external .rcc (tools/mkrcc.py) replace the
// app's own qrc:/ files of the same path, QML, JS and images alike, and new
// paths are added, without writing to the system partition.
//
// Qt 5.3 serves a path from the first registered resource root that has it,
// and the app registers its roots during static initialization. overlayStart()
// finds those roots in the executable's read-only segments, unregisters the
// ones that share a path with the overlay, registers the overlay, and
// registers them again behind it. It has to run before the QML engine loads
// anything, so the eglfs hooks call it from platformInit().

#ifndef OVERLAY_H
#define OVERLAY_H

#include <QtCore/QStringList>

// The overlay is OVERLAY_DIR/overlay.rcc, or WMMOD_OVERLAY from the
// environment. Files beside it, which USB mass storage can reach:
//   disable   turns the overlay off.
//   badboots  counts starts that did not stay up for OVERLAY_HEALTHY_MS
//             (WMMOD_HEALTHY_MS overrides it). At OVERLAY_MAX_BAD_BOOTS the
//             overlay is skipped until the file is deleted, so a broken
//             overlay cannot hold the device in a watchdog reboot loop.
//   probe     starts the probe server (probe.h).
#define OVERLAY_DIR "/contents/wmmod"
#define OVERLAY_MAX_BAD_BOOTS 3
#define OVERLAY_HEALTHY_MS 60000

void overlayStart();
QStringList overlayStatus();

// Swaps the overlay of a running app for the one at path: the overlay in
// effect is unregistered and the new one goes in front of the app roots it
// shares a path with, as at start-up. The QML engine still holds what it
// compiled from the old files; the probe's `reload` refreshes that (probe.h).
// changed receives every path whose content differs, added and removed ones
// included; added the paths that Qt did not serve at all before.
bool overlayReload(const QString &path, QStringList &changed, QStringList &added, QStringList &out);
QString overlayDir();

#endif
