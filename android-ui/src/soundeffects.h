#ifndef SOUNDEFFECTS_H
#define SOUNDEFFECTS_H

class PlayerController;
class SoundState;

// Keeps the system's sound effects (audiofx/, on the NW-A55 image) on the
// settings in sound: when they change, when the track does (DSEE HX does
// nothing for a hi-res one) and when playback starts.
void attachSoundEffects(SoundState *sound, PlayerController *player);

#endif
