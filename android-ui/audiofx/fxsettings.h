#ifndef WALKMANFX_SETTINGS_H
#define WALKMANFX_SETTINGS_H

#include <stdint.h>

// What the app sends the effect library: AudioEffect.setParameter with the
// 4-byte key below and this struct as the value, little endian on both sides.
#define WALKMANFX_PARAM_SETTINGS 0x31584d57 /* "WMX1" */

// The effect's type and implementation UUIDs, also in
// android/src/org/walkmanui/SoundEffects.java.
#define WALKMANFX_TYPE_UUID "f0463e98-f8c3-412b-891d-a188b922e2a0"
#define WALKMANFX_UUID "7c3e4afc-7189-4cf4-9ad2-5c37724cd6c7"

enum {
    WALKMANFX_NORMAL = 0,
    WALKMANFX_CLEAR_AUDIO = 1,      // the effects below are ignored for a fixed set
    WALKMANFX_SOURCE_DIRECT = 2,    // no processing at all
};

enum { WALKMANFX_BANDS = 10, WALKMANFX_DC_PHASE_TYPES = 6, WALKMANFX_VINYL_TYPES = 4 };

// The stock sound settings (SoundState in the app).
typedef struct {
    int32_t mode;
    int32_t toneActive;             // tone control instead of the equalizer
    float bands[WALKMANFX_BANDS];   // dB, -10..+10, at 31, 62, 125, 250, 500, 1k, 2k, 4k, 8k, 16k Hz
    float tone[3];                  // low, middle, high, -10..+10 dB
    int32_t dseeHx;
    int32_t dcPhase;
    int32_t dcPhaseType;            // A LOW, A STANDARD, A HIGH, B LOW, B STANDARD, B HIGH
    int32_t dynamicNormalizer;
    int32_t vinyl;
    int32_t vinylType;              // standard, turntable resonance, arm resonance, surface noise
    int32_t hiResSource;            // beyond CD quality: DSEE HX, ClearAudio+'s too, has nothing to restore
} WalkmanFxSettings;

#endif
