#ifndef SETTINGSMODELS_H
#define SETTINGSMODELS_H

#include "screenmodel.h"

// The settings and sound-effect screens' ViewModels; null for other ids.
//
// Settings live in prefs under "settings/" and the sound effects under
// "sound/" (SoundState). The play settings act on the player. Screens of
// hardware this build does not have (Bluetooth, BT receiver, remote control,
// NC/ASM, USB DAC, mass storage) get default values so they render.
ScreenModel *createSettingsModel(const QString &id, AppContext *app);

#endif
