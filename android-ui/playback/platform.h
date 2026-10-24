#ifndef PLAYBACK_PLATFORM_H
#define PLAYBACK_PLATFORM_H

#include <QtCore/QByteArray>
#include <QtCore/QObject>

#include <functional>

// Runs ready once the library may read shared storage: at once, or when
// the first run's permission prompt has been answered either way.
void withMediaAccess(QObject *context, std::function<void()> ready);
// Runs changed on context's thread whenever Android mounts a storage volume or
// loses one; never where there are none.
void onStoragesChanged(QObject *context, std::function<void()> changed);
class PlayerController;
bool requestPlaybackFocus();
void releasePlaybackFocus();
void attachPlaybackPlatform(PlayerController *player);
void detachPlaybackPlatform();
void backgroundPlaybackApp();
// Keeps the display on while the app's window is shown.
void keepScreenOn(bool on);
// Hands a WalkmanFxSettings (audiofx/fxsettings.h) to the system's sound
// effects; does nothing where there are none.
void applySoundEffects(const QByteArray &settings);

#endif
