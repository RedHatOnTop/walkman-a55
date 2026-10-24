#include "walkmanfx.h"

#include <math.h>
#include <string.h>

namespace walkmanfx {

namespace {

const double kPi = 3.14159265358979323846;
const double kBandHz[WALKMANFX_BANDS] = {31.25, 62.5, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
const double kBandQ = 1.41;     // one octave
const double kDcPhaseHz[3] = {40, 80, 160};
const double kLimiterKnee = 0.891;  // -1 dBFS
const int kBlock = 64;
const double kTiny = 1e-15;

// ClearAudio+ replaces the user's effects with one setting of its own: a
// light loudness curve, DSEE HX and the standard DC phase linearizer.
const float kClearAudioBands[WALKMANFX_BANDS] = {2, 2.5f, 1.5f, 0, -0.5f, 0, 0.5f, 1, 1.5f, 1.5f};

float clampDb(float value)
{
    if (!(value == value))
        return 0;
    return value < -10 ? -10 : value > 10 ? 10 : value;
}

int32_t clampIndex(int32_t value, int32_t count)
{
    return value < 0 ? 0 : value >= count ? count - 1 : value;
}

double flushed(double x)
{
    return fabs(x) < kTiny ? 0 : x;
}

double soften(double x)
{
    const double a = fabs(x);
    if (a <= kLimiterKnee)
        return x;
    const double y = kLimiterKnee + (1 - kLimiterKnee) * tanh((a - kLimiterKnee) / (1 - kLimiterKnee));
    return x < 0 ? -y : y;
}

void setCoefficients(Biquad *target, const Biquad &source)
{
    target->b0 = source.b0;
    target->b1 = source.b1;
    target->b2 = source.b2;
    target->a1 = source.a1;
    target->a2 = source.a2;
}

Biquad normalized(double b0, double b1, double b2, double a0, double a1, double a2)
{
    Biquad q;
    q.b0 = b0 / a0;
    q.b1 = b1 / a0;
    q.b2 = b2 / a0;
    q.a1 = a1 / a0;
    q.a2 = a2 / a0;
    return q;
}

}

void Biquad::clear()
{
    z1[0] = z1[1] = z2[0] = z2[1] = 0;
}

void Biquad::flush()
{
    for (int c = 0; c < 2; ++c) {
        z1[c] = flushed(z1[c]);
        z2[c] = flushed(z2[c]);
    }
}

double Biquad::gainDb(double rate, double f) const
{
    const double w = 2 * kPi * f / rate;
    const double c1 = cos(w), s1 = sin(w), c2 = cos(2 * w), s2 = sin(2 * w);
    const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
    const double dr = 1 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
    return 10 * log10((nr * nr + ni * ni) / (dr * dr + di * di));
}

Biquad Biquad::peaking(double rate, double f, double q, double db)
{
    const double a = pow(10, db / 40), w = 2 * kPi * f / rate, alpha = sin(w) / (2 * q);
    return normalized(1 + alpha * a, -2 * cos(w), 1 - alpha * a, 1 + alpha / a, -2 * cos(w), 1 - alpha / a);
}

Biquad Biquad::lowShelf(double rate, double f, double q, double db)
{
    const double a = pow(10, db / 40), w = 2 * kPi * f / rate, alpha = sin(w) / (2 * q), c = cos(w);
    const double s = 2 * sqrt(a) * alpha;
    return normalized(a * ((a + 1) - (a - 1) * c + s), 2 * a * ((a - 1) - (a + 1) * c), a * ((a + 1) - (a - 1) * c - s),
                      (a + 1) + (a - 1) * c + s, -2 * ((a - 1) + (a + 1) * c), (a + 1) + (a - 1) * c - s);
}

Biquad Biquad::highShelf(double rate, double f, double q, double db)
{
    const double a = pow(10, db / 40), w = 2 * kPi * f / rate, alpha = sin(w) / (2 * q), c = cos(w);
    const double s = 2 * sqrt(a) * alpha;
    return normalized(a * ((a + 1) + (a - 1) * c + s), -2 * a * ((a - 1) + (a + 1) * c), a * ((a + 1) + (a - 1) * c - s),
                      (a + 1) - (a - 1) * c + s, 2 * ((a - 1) - (a + 1) * c), (a + 1) - (a - 1) * c - s);
}

Biquad Biquad::highPass(double rate, double f, double q)
{
    const double w = 2 * kPi * f / rate, alpha = sin(w) / (2 * q), c = cos(w);
    return normalized((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
}

Biquad Biquad::lowPass(double rate, double f, double q)
{
    const double w = 2 * kPi * f / rate, alpha = sin(w) / (2 * q), c = cos(w);
    return normalized((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
}

Biquad Biquad::bandPass(double rate, double f, double q)
{
    const double w = 2 * kPi * f / rate, alpha = sin(w) / (2 * q), c = cos(w);
    return normalized(alpha, 0, -alpha, 1 + alpha, -2 * c, 1 - alpha);
}

Biquad Biquad::allPass(double rate, double f, double q)
{
    const double w = 2 * kPi * f / rate, alpha = sin(w) / (2 * q), c = cos(w);
    return normalized(1 - alpha, -2 * c, 1 + alpha, 1 + alpha, -2 * c, 1 - alpha);
}

Biquad Biquad::allPassFirstOrder(double rate, double f)
{
    const double t = tan(kPi * f / rate), k = (t - 1) / (t + 1);
    Biquad q;
    q.b0 = k;
    q.b1 = 1;
    q.b2 = 0;
    q.a1 = k;
    q.a2 = 0;
    return q;
}

Processor::Processor()
{
    memset(&m_settings, 0, sizeof m_settings);
    m_settings.dcPhaseType = 1;
}

void Processor::configure(double rate)
{
    m_rate = rate > 0 ? rate : 44100;
    rebuild();
    clearState();
}

void Processor::apply(const WalkmanFxSettings &settings)
{
    WalkmanFxSettings s = settings;
    if (s.mode != WALKMANFX_CLEAR_AUDIO && s.mode != WALKMANFX_SOURCE_DIRECT)
        s.mode = WALKMANFX_NORMAL;
    for (float &band : s.bands)
        band = clampDb(band);
    for (float &value : s.tone)
        value = clampDb(value);
    s.dcPhaseType = clampIndex(s.dcPhaseType, WALKMANFX_DC_PHASE_TYPES);
    s.vinylType = clampIndex(s.vinylType, WALKMANFX_VINYL_TYPES);
    m_settings = s;
    const bool wasIdle = m_idle;
    rebuild();
    if (wasIdle && !m_idle)
        clearState();
}

// Coefficients change in place, so a filter that stays keeps its state and
// a change of settings does not click.
void Processor::rebuild()
{
    WalkmanFxSettings s = m_settings;
    if (s.mode == WALKMANFX_SOURCE_DIRECT) {
        m_idle = true;
        return;
    }
    if (s.mode == WALKMANFX_CLEAR_AUDIO) {
        memcpy(s.bands, kClearAudioBands, sizeof s.bands);
        s.toneActive = 0;
        s.dseeHx = 1;
        s.dcPhase = 1;
        s.dcPhaseType = 1;
        s.dynamicNormalizer = 0;
        s.vinyl = 0;
    }
    const double top = 0.45 * m_rate;
    const bool dsee = m_dsee, phase = m_phaseCount != 0, arm = m_arm, turntable = m_turntable, surface = m_surface;

    Biquad eq[WALKMANFX_BANDS];
    int count = 0;
    if (s.toneActive) {
        if (s.tone[0] != 0 || s.tone[1] != 0 || s.tone[2] != 0) {
            eq[count++] = Biquad::lowShelf(m_rate, 120, 0.707, s.tone[0]);
            eq[count++] = Biquad::peaking(m_rate, 1000, 0.7, s.tone[1]);
            eq[count++] = Biquad::highShelf(m_rate, 8000 < top ? 8000 : top, 0.707, s.tone[2]);
        }
    } else {
        bool any = false;
        for (float band : s.bands)
            any |= band != 0;
        for (int i = 0; any && i < WALKMANFX_BANDS && kBandHz[i] < top; ++i)
            eq[count++] = Biquad::peaking(m_rate, kBandHz[i], kBandQ, s.bands[i]);
    }
    if (count != m_eqCount) {
        for (int i = 0; i < count; ++i)
            m_eq[i] = eq[i];
    } else {
        for (int i = 0; i < count; ++i)
            setCoefficients(&m_eq[i], eq[i]);
    }
    m_eqCount = count;
    // Boosts get their headroom first: the gain drops by the response's peak.
    double peak = 0;
    for (int i = 0; count > 0 && i < 96; ++i) {
        const double f = 20 * pow(top / 20, i / 95.0);
        double db = 0;
        for (int j = 0; j < count; ++j)
            db += m_eq[j].gainDb(m_rate, f);
        peak = db > peak ? db : peak;
    }
    m_preGain = pow(10, -peak / 20);

    // DSEE HX: the top octave lossy codecs cut is made again from the octave
    // below it: the 8 kHz region, full-wave rectified (its second harmonic),
    // keeps only what lands above 14 kHz.
    m_dsee = s.dseeHx && !s.hiResSource && m_rate >= 44100;
    if (m_dsee) {
        setCoefficients(&m_dseeBand, Biquad::bandPass(m_rate, 8000, 1.0));
        setCoefficients(&m_dseeHigh1, Biquad::highPass(m_rate, 14000, 0.707));
        setCoefficients(&m_dseeHigh2, Biquad::highPass(m_rate, 14000, 0.707));
        m_dseeEnvCoef = 1 - exp(-1 / (0.005 * m_rate));
    }

    // DC phase linearizer: the low-frequency phase lag of an analog
    // amplifier, without touching the level. Type A is first order, type B
    // second order; LOW, STANDARD and HIGH move the corner.
    m_phaseCount = s.dcPhase ? 1 : 0;
    if (m_phaseCount) {
        const double corner = kDcPhaseHz[s.dcPhaseType % 3];
        setCoefficients(&m_phase, s.dcPhaseType < 3 ? Biquad::allPassFirstOrder(m_rate, corner)
                                                    : Biquad::allPass(m_rate, corner, 0.7));
    }

    // Vinyl processor: STANDARD is all of the record player at once.
    m_arm = s.vinyl && (s.vinylType == 0 || s.vinylType == 2);
    m_turntable = s.vinyl && (s.vinylType == 0 || s.vinylType == 1);
    m_surface = s.vinyl && (s.vinylType == 0 || s.vinylType == 3);
    m_crosstalk = s.vinyl && s.vinylType == 0;
    if (m_arm) {
        // The tonearm and cartridge resonance near 10 Hz, the stylus's high
        // resonance and the cartridge's roll-off.
        setCoefficients(&m_armLow, Biquad::peaking(m_rate, 10, 1.2, 6));
        setCoefficients(&m_armHigh, Biquad::peaking(m_rate, 12000 < top ? 12000 : top, 2.0, 1.5));
        setCoefficients(&m_armRolloff, Biquad::lowPass(m_rate, 18000 < top ? 18000 : top, 0.707));
    }
    if (m_turntable) {
        // The platter and plinth ringing with the music.
        setCoefficients(&m_bodyLow, Biquad::bandPass(m_rate, 45, 5));
        setCoefficients(&m_bodyHigh, Biquad::bandPass(m_rate, 95, 5));
    }
    if (m_surface) {
        // Hiss around 60 dB down and about three clicks a second.
        setCoefficients(&m_hiss, Biquad::bandPass(m_rate, 3500, 0.5));
        setCoefficients(&m_click, Biquad::highPass(m_rate, 1000, 0.707));
        m_clickDecay = exp(-1 / (0.0004 * m_rate));
        m_clickChance = 3.0 / m_rate;
    }

    m_normalizer = s.dynamicNormalizer != 0;
    m_idle = !(m_eqCount || m_dsee || m_phaseCount || m_arm || m_turntable || m_surface || m_normalizer);

    // An effect switched on starts from silence, not from where it stopped.
    if (m_dsee && !dsee) {
        m_dseeBand.clear();
        m_dseeHigh1.clear();
        m_dseeHigh2.clear();
        m_dseeEnv[0] = m_dseeEnv[1] = 0;
    }
    if (m_phaseCount && !phase)
        m_phase.clear();
    if (m_arm && !arm) {
        m_armLow.clear();
        m_armHigh.clear();
        m_armRolloff.clear();
    }
    if (m_turntable && !turntable) {
        m_bodyLow.clear();
        m_bodyHigh.clear();
    }
    if (m_surface && !surface) {
        m_hiss.clear();
        m_click.clear();
        m_clickLevel[0] = m_clickLevel[1] = 0;
    }
}

void Processor::clearState()
{
    for (Biquad &q : m_eq)
        q.clear();
    Biquad *const filters[] = {&m_dseeBand, &m_dseeHigh1, &m_dseeHigh2, &m_phase, &m_armLow, &m_armHigh,
                               &m_armRolloff, &m_bodyLow, &m_bodyHigh, &m_hiss, &m_click};
    for (Biquad *q : filters)
        q->clear();
    m_dseeEnv[0] = m_dseeEnv[1] = 0;
    m_clickLevel[0] = m_clickLevel[1] = 0;
    m_level = 0;
    m_gainDb = 0;
    m_gain = m_targetGain = 1;
    m_blockSum = 0;
    m_blockFrames = 0;
}

void Processor::flush()
{
    for (Biquad &q : m_eq)
        q.flush();
    Biquad *const filters[] = {&m_dseeBand, &m_dseeHigh1, &m_dseeHigh2, &m_phase, &m_armLow, &m_armHigh,
                               &m_armRolloff, &m_bodyLow, &m_bodyHigh, &m_hiss, &m_click};
    for (Biquad *q : filters)
        q->flush();
    for (int c = 0; c < 2; ++c) {
        m_dseeEnv[c] = flushed(m_dseeEnv[c]);
        m_clickLevel[c] = flushed(m_clickLevel[c]);
    }
    m_level = flushed(m_level);
}

float Processor::noise()
{
    m_random ^= m_random << 13;
    m_random ^= m_random >> 17;
    m_random ^= m_random << 5;
    return float(int32_t(m_random) * (1.0 / 2147483648.0));
}

void Processor::process(float *frames, size_t count)
{
    if (m_idle)
        return;
    // Dynamic normalizer: the level over about three seconds, brought towards
    // -20 dBFS by up to 9 dB; turned down quickly, up slowly, and held through
    // passages too quiet to measure.
    const double levelCoef = 1 - exp(-kBlock / (3.0 * m_rate));
    const double downCoef = 1 - exp(-kBlock / (0.5 * m_rate));
    const double upCoef = 1 - exp(-kBlock / (4.0 * m_rate));
    const double gainStep = 1.0 / kBlock;

    for (size_t i = 0; i < count; ++i) {
        double out[2];
        for (int c = 0; c < 2; ++c) {
            double y = frames[2 * i + c];
            if (m_dsee) {
                const double band = m_dseeBand.run(y, c);
                m_dseeEnv[c] += (fabs(band) - m_dseeEnv[c]) * m_dseeEnvCoef;
                y += 0.5 * m_dseeHigh2.run(m_dseeHigh1.run(fabs(band) - m_dseeEnv[c], c), c);
            }
            if (m_eqCount) {
                y *= m_preGain;
                for (int s = 0; s < m_eqCount; ++s)
                    y = m_eq[s].run(y, c);
            }
            if (m_phaseCount)
                y = m_phase.run(y, c);
            if (m_arm)
                y = m_armRolloff.run(m_armHigh.run(m_armLow.run(y, c), c), c);
            out[c] = y;
        }
        if (m_turntable) {
            const double mono = 0.5 * (out[0] + out[1]);
            const double body = 0.06 * (m_bodyLow.run(mono, 0) + m_bodyHigh.run(mono, 0));
            out[0] += body;
            out[1] += body;
        }
        if (m_crosstalk) {
            const double left = out[0];
            out[0] += 0.03 * out[1];
            out[1] += 0.03 * left;
        }
        if (m_surface) {
            if (noise() * 0.5f + 0.5f < m_clickChance) {
                const double level = (0.01 + 0.02 * (noise() * 0.5 + 0.5)) * (noise() < 0 ? -1 : 1);
                const float side = noise();
                m_clickLevel[0] += side < 0.3f ? level : 0;
                m_clickLevel[1] += side > -0.3f ? level : 0;
            }
            for (int c = 0; c < 2; ++c) {
                out[c] += 0.0012 * m_hiss.run(noise(), c) + m_click.run(m_clickLevel[c], c);
                m_clickLevel[c] *= m_clickDecay;
            }
        }
        if (m_normalizer) {
            const double mono = 0.5 * (out[0] + out[1]);
            m_blockSum += mono * mono;
            if (++m_blockFrames == kBlock) {
                m_level += (m_blockSum / kBlock - m_level) * levelCoef;
                m_blockSum = 0;
                m_blockFrames = 0;
                const double levelDb = 10 * log10(m_level + 1e-12);
                if (levelDb > -50) {
                    double target = -20 - levelDb;
                    target = target < -9 ? -9 : target > 9 ? 9 : target;
                    m_gainDb += (target - m_gainDb) * (target < m_gainDb ? downCoef : upCoef);
                }
                m_targetGain = pow(10, m_gainDb / 20);
            }
            m_gain += (m_targetGain - m_gain) * gainStep;
            out[0] *= m_gain;
            out[1] *= m_gain;
        }
        frames[2 * i] = float(soften(out[0]));
        frames[2 * i + 1] = float(soften(out[1]));
    }
    flush();
}

}
