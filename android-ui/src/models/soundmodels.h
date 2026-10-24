#ifndef SOUNDMODELS_H
#define SOUNDMODELS_H

#include "screenmodel.h"

// The ViewModels of the sound-effect pages, in their player (...FromPlayer),
// settings and DAC (...FromDAC) variants: equalizer, tone control, DSEE HX, DC
// phase linearizer, dynamic normalizer, vinyl processor, saved sound settings
// and L/R balance. Null for any other id. All variants of a page share
// SoundState.
ScreenModel *createSoundModel(const QString &id, AppContext *app);

// The names of the DC phase linearizer and vinyl processor types, as the
// sound-enhancement list shows the selected one.
QString dcPhaseTypeLabel(int type);
QString vinylTypeLabel(int type);

#endif
