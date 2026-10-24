#ifndef WALKMANFX_H
#define WALKMANFX_H

#include <stddef.h>
#include <stdint.h>

#include "fxsettings.h"

namespace walkmanfx {

// A second-order section from the RBJ audio EQ cookbook, transposed direct
// form II, with one state per stereo channel.
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] = {0, 0}, z2[2] = {0, 0};

    double run(double x, int channel)
    {
        const double y = b0 * x + z1[channel];
        z1[channel] = b1 * x - a1 * y + z2[channel];
        z2[channel] = b2 * x - a2 * y;
        return y;
    }
    void clear();
    // Zeroes states below -300 dB before they decay into denormals, which
    // cost a hundred times a normal multiply.
    void flush();
    // The magnitude response at f, in dB.
    double gainDb(double rate, double f) const;

    static Biquad peaking(double rate, double f, double q, double db);
    static Biquad lowShelf(double rate, double f, double q, double db);
    static Biquad highShelf(double rate, double f, double q, double db);
    static Biquad highPass(double rate, double f, double q);
    static Biquad lowPass(double rate, double f, double q);
    static Biquad bandPass(double rate, double f, double q);
    static Biquad allPass(double rate, double f, double q);
    static Biquad allPassFirstOrder(double rate, double f);
};

// The sound effects of the stock player on one interleaved stereo stream:
// DSEE HX, the equalizer or tone control, DC phase linearizer, vinyl
// processor and dynamic normalizer, in that order, then a soft limiter.
// Sony did not publish its algorithms; DSEE HX, the DC phase linearizer and
// the vinyl processor here do what the stock help describes, their own way.
class Processor {
public:
    Processor();
    void configure(double rate);
    void apply(const WalkmanFxSettings &settings);
    // With nothing to do the stream passes bit for bit.
    bool idle() const { return m_idle; }
    void process(float *frames, size_t count);

private:
    void rebuild();
    void clearState();
    void flush();
    float noise();

    double m_rate = 44100;
    WalkmanFxSettings m_settings;
    bool m_idle = true;

    Biquad m_eq[WALKMANFX_BANDS];
    int m_eqCount = 0;
    double m_preGain = 1;

    bool m_dsee = false;
    Biquad m_dseeBand, m_dseeHigh1, m_dseeHigh2;
    double m_dseeEnv[2] = {0, 0};
    double m_dseeEnvCoef = 0;

    int m_phaseCount = 0;
    Biquad m_phase;

    bool m_arm = false, m_turntable = false, m_surface = false, m_crosstalk = false;
    Biquad m_armLow, m_armHigh, m_armRolloff;
    Biquad m_bodyLow, m_bodyHigh;
    Biquad m_hiss, m_click;
    double m_clickLevel[2] = {0, 0};
    double m_clickDecay = 0;
    double m_clickChance = 0;
    uint32_t m_random = 0x9e3779b9u;

    bool m_normalizer = false;
    double m_level = 0;         // mean square, smoothed
    double m_gainDb = 0;
    double m_gain = 1;
    double m_targetGain = 1;
    double m_blockSum = 0;
    int m_blockFrames = 0;
};

}

#endif
