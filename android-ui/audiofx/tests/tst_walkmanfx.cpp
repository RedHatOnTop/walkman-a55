// Checks walkmanfx::Processor on synthetic signals, then loads the built
// libwalkmanfx.so the way the Android effects factory does and drives it the
// way AudioFlinger and the app do.
//
//   tst_walkmanfx path/to/libwalkmanfx.so

#include <dlfcn.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <vector>

#include <hardware/audio_effect.h>

#include "../fxsettings.h"
#include "../walkmanfx.h"

namespace {

int g_failures = 0;

#define CHECK(condition, ...)                                                                                          \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            ++g_failures;                                                                                              \
            printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #condition);                                                \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)

const double kPi = 3.14159265358979323846;

WalkmanFxSettings off()
{
    WalkmanFxSettings s;
    memset(&s, 0, sizeof s);
    s.dcPhaseType = 1;
    return s;
}

std::vector<float> sine(double rate, double f, double amplitude, double seconds)
{
    std::vector<float> frames(size_t(rate * seconds) * 2);
    for (size_t i = 0; i < frames.size() / 2; ++i)
        frames[2 * i] = frames[2 * i + 1] = float(amplitude * sin(2 * kPi * f * i / rate));
    return frames;
}

double rmsDb(const std::vector<float> &frames, size_t fromFrame)
{
    double sum = 0;
    for (size_t i = fromFrame * 2; i < frames.size(); ++i)
        sum += double(frames[i]) * frames[i];
    return 10 * log10(sum / (frames.size() - fromFrame * 2) + 1e-30);
}

// The level of one frequency in the left channel, in dB.
double toneDb(const std::vector<float> &frames, size_t fromFrame, double rate, double f)
{
    double re = 0, im = 0;
    const size_t count = frames.size() / 2 - fromFrame;
    for (size_t i = 0; i < count; ++i) {
        const double x = frames[2 * (fromFrame + i)];
        re += x * cos(2 * kPi * f * i / rate);
        im -= x * sin(2 * kPi * f * i / rate);
    }
    return 20 * log10(2 * sqrt(re * re + im * im) / count + 1e-30);
}

std::vector<float> run(const WalkmanFxSettings &settings, std::vector<float> frames, double rate = 44100)
{
    walkmanfx::Processor processor;
    processor.configure(rate);
    processor.apply(settings);
    // In chunks, as AudioFlinger hands them over.
    for (size_t at = 0; at < frames.size() / 2; at += 941) {
        const size_t count = frames.size() / 2 - at < 941 ? frames.size() / 2 - at : 941;
        processor.process(&frames[2 * at], count);
    }
    return frames;
}

// The steady gain of a sine through the settings, in dB.
double gainDb(const WalkmanFxSettings &settings, double f, double rate = 44100)
{
    const std::vector<float> in = sine(rate, f, 0.1, 1.0);
    const std::vector<float> out = run(settings, in, rate);
    const size_t from = size_t(rate * 0.5);
    return rmsDb(out, from) - rmsDb(in, from);
}

void testBypass()
{
    walkmanfx::Processor processor;
    processor.configure(44100);
    processor.apply(off());
    CHECK(processor.idle(), "all effects off is not idle");
    const std::vector<float> in = sine(44100, 997, 0.9, 0.2);
    CHECK(run(off(), in) == in, "all effects off changed the stream");

    WalkmanFxSettings direct = off();
    direct.mode = WALKMANFX_SOURCE_DIRECT;
    for (float &band : direct.bands)
        band = 6;
    direct.dseeHx = direct.dcPhase = direct.dynamicNormalizer = direct.vinyl = 1;
    CHECK(run(direct, in) == in, "Source Direct changed the stream");

    // A flat equalizer is no equalizer.
    WalkmanFxSettings flat = off();
    flat.toneActive = 1;
    processor.apply(flat);
    CHECK(processor.idle(), "flat tone control is not idle");
}

void testEqualizer()
{
    WalkmanFxSettings s = off();
    s.bands[5] = 6;    // 1 kHz
    const double at1k = gainDb(s, 1000), at125 = gainDb(s, 125), at8k = gainDb(s, 8000);
    CHECK(fabs(at1k - at125 - 6) < 0.5, "1 kHz +6 dB band: 1k %.2f dB, 125 Hz %.2f dB", at1k, at125);
    CHECK(fabs(at125 - at8k) < 0.3, "bands away from 1 kHz: 125 Hz %.2f dB, 8 kHz %.2f dB", at125, at8k);
    CHECK(at1k < 0.2, "the boost has no headroom: 1 kHz %.2f dB", at1k);

    WalkmanFxSettings cut = off();
    cut.bands[0] = -10;
    const double at31 = gainDb(cut, 31.25), at1kCut = gainDb(cut, 1000);
    CHECK(fabs(at31 - at1kCut + 10) < 1.0, "31 Hz -10 dB band: 31 Hz %.2f dB, 1 kHz %.2f dB", at31, at1kCut);
    CHECK(fabs(at1kCut) < 0.2, "a cut lowered 1 kHz: %.2f dB", at1kCut);

    // Out-of-range and broken values are clamped, never passed on.
    WalkmanFxSettings wild = off();
    wild.bands[3] = 1e9f;
    wild.bands[4] = nanf("");
    const std::vector<float> out = run(wild, sine(44100, 250, 0.1, 0.5));
    bool finite = true;
    for (float x : out)
        finite &= isfinite(x) != 0;
    CHECK(finite, "wild band values gave a non-finite output");
    const double at250 = gainDb(wild, 250), at4k = gainDb(wild, 4000);
    CHECK(fabs(at250 - at4k - 10) < 1.0, "a 1e9 dB band is not +10 dB: 250 Hz %.2f dB, 4 kHz %.2f dB", at250, at4k);
}

void testTone()
{
    WalkmanFxSettings s = off();
    s.toneActive = 1;
    s.tone[0] = 6;
    s.bands[5] = 10;   // ignored under tone control
    const double low = gainDb(s, 50), mid = gainDb(s, 1000), high = gainDb(s, 12000);
    CHECK(low - mid > 4.5, "bass +6 dB: 50 Hz %.2f dB, 1 kHz %.2f dB", low, mid);
    CHECK(fabs(mid - high) < 0.5, "bass touched 1k or 12k: %.2f / %.2f dB", mid, high);

    WalkmanFxSettings treble = off();
    treble.toneActive = 1;
    treble.tone[2] = -6;
    const double top = gainDb(treble, 14000), centre = gainDb(treble, 1000);
    CHECK(centre - top > 4.5, "treble -6 dB: 14 kHz %.2f dB, 1 kHz %.2f dB", top, centre);
}

void testDcPhase()
{
    for (int type = 0; type < WALKMANFX_DC_PHASE_TYPES; ++type) {
        WalkmanFxSettings s = off();
        s.dcPhase = 1;
        s.dcPhaseType = type;
        for (double f : {40.0, 80.0, 1000.0, 15000.0}) {
            const double g = gainDb(s, f);
            CHECK(fabs(g) < 0.05, "DC phase type %d changed the level at %.0f Hz: %.3f dB", type, f, g);
        }
        // The low end moves in time against the rest.
        const std::vector<float> in = sine(44100, 80, 0.1, 0.5);
        const std::vector<float> out = run(s, in);
        double difference = 0;
        for (size_t i = 11025 * 2; i < in.size(); ++i)
            difference = fmax(difference, fabs(out[i] - in[i]));
        CHECK(difference > 0.02, "DC phase type %d left 80 Hz in phase: %.4f", type, difference);
    }
}

void testDsee()
{
    WalkmanFxSettings s = off();
    s.dseeHx = 1;
    const std::vector<float> in = sine(44100, 8000, 0.3, 1.0);
    const std::vector<float> out = run(s, in);
    const double before = toneDb(in, 22050, 44100, 16000), after = toneDb(out, 22050, 44100, 16000);
    const double fundamental = toneDb(out, 22050, 44100, 8000);
    CHECK(after - before > 40, "DSEE HX made no 16 kHz from 8 kHz: %.1f -> %.1f dB", before, after);
    CHECK(fundamental - after > 6 && fundamental - after < 40, "the made top octave is %.1f dB under 8 kHz",
          fundamental - after);
    // Low content makes nothing above 14 kHz.
    const std::vector<float> low = run(s, sine(44100, 500, 0.3, 1.0));
    CHECK(toneDb(low, 22050, 44100, 15000) < -90, "500 Hz made top-octave content");

    // A 32 kHz stream has no top octave to make.
    walkmanfx::Processor processor;
    processor.configure(32000);
    processor.apply(s);
    CHECK(processor.idle(), "DSEE HX runs at 32 kHz");

    // Nor does a hi-res track, under ClearAudio+ either.
    s.hiResSource = 1;
    processor.configure(44100);
    processor.apply(s);
    CHECK(processor.idle(), "DSEE HX runs on a hi-res track");
    WalkmanFxSettings clear = off();
    clear.mode = WALKMANFX_CLEAR_AUDIO;
    clear.hiResSource = 1;
    const std::vector<float> hiRes = run(clear, sine(44100, 8000, 0.3, 1.0));
    CHECK(toneDb(hiRes, 22050, 44100, 16000) < -90, "ClearAudio+ made a top octave for a hi-res track: %.1f dB",
          toneDb(hiRes, 22050, 44100, 16000));
}

void testNormalizer()
{
    WalkmanFxSettings s = off();
    s.dynamicNormalizer = 1;
    const size_t last = size_t(44100 * 18);
    {
        const std::vector<float> in = sine(44100, 1000, 0.01, 20);    // -43 dBFS RMS
        const std::vector<float> out = run(s, in);
        const double gain = rmsDb(out, last) - rmsDb(in, last);
        CHECK(fabs(gain - 9) < 1, "a quiet track is lifted %.2f dB, not 9", gain);
    }
    {
        const std::vector<float> in = sine(44100, 1000, 0.2, 20);     // -17 dBFS RMS
        const std::vector<float> out = run(s, in);
        const double gain = rmsDb(out, last) - rmsDb(in, last);
        CHECK(fabs(gain + 3) < 1, "a -17 dBFS track is moved %.2f dB, not -3", gain);
    }
    {
        // Silence is held, not pumped up.
        const std::vector<float> in = sine(44100, 1000, 0.0001, 20);  // -83 dBFS
        const std::vector<float> out = run(s, in);
        const double gain = rmsDb(out, last) - rmsDb(in, last);
        CHECK(fabs(gain) < 0.5, "near-silence is moved %.2f dB", gain);
    }
}

void testVinyl()
{
    const std::vector<float> silence(44100 * 2 * 2, 0.0f);
    for (int type = 0; type < WALKMANFX_VINYL_TYPES; ++type) {
        WalkmanFxSettings s = off();
        s.vinyl = 1;
        s.vinylType = type;
        const double noise = rmsDb(run(s, silence), 0);
        if (type == 0 || type == 3)
            CHECK(noise > -80 && noise < -45, "vinyl type %d surface noise is %.1f dBFS", type, noise);
        else
            CHECK(noise < -200, "vinyl type %d is not silent on silence: %.1f dBFS", type, noise);
    }
    WalkmanFxSettings arm = off();
    arm.vinyl = 1;
    arm.vinylType = 2;
    CHECK(gainDb(arm, 20000) < -2, "the arm type does not roll off 20 kHz: %.2f dB", gainDb(arm, 20000));

    // STANDARD bleeds a little of each channel into the other.
    WalkmanFxSettings standard = off();
    standard.vinyl = 1;
    std::vector<float> left = sine(44100, 1000, 0.1, 0.5);
    for (size_t i = 1; i < left.size(); i += 2)
        left[i] = 0;
    const std::vector<float> out = run(standard, left);
    double right = 0;
    for (size_t i = 11025 * 2 + 1; i < out.size(); i += 2)
        right = fmax(right, fabs(out[i]));
    CHECK(right > 0.001 && right < 0.01, "crosstalk into the silent channel peaks at %.4f", right);
}

void testLimiterAndClearAudio()
{
    WalkmanFxSettings s = off();
    for (float &band : s.bands)
        band = 10;
    s.vinyl = 1;
    std::vector<float> in = sine(44100, 60, 1.0, 0.5);
    double peak = 0;
    for (float x : run(s, in))
        peak = fmax(peak, fabs(x));
    CHECK(peak <= 1.0, "the output peaks at %.4f", peak);

    WalkmanFxSettings clear = off();
    clear.mode = WALKMANFX_CLEAR_AUDIO;
    clear.vinyl = 1;   // ClearAudio+ ignores the user's effects
    walkmanfx::Processor processor;
    processor.configure(44100);
    processor.apply(clear);
    CHECK(!processor.idle(), "ClearAudio+ is idle");
    const double lift = gainDb(clear, 62.5) - gainDb(clear, 500);
    CHECK(fabs(lift - 3) < 1, "ClearAudio+ lifts 62 Hz by %.2f dB against 500 Hz", lift);
    const std::vector<float> silence(44100 * 2, 0.0f);
    CHECK(rmsDb(run(clear, silence), 0) < -200, "ClearAudio+ added vinyl noise");
}

void testSilenceSettles()
{
    // After the music stops the filters ring down to exact zeros, not to
    // denormals that would stall the CPU.
    WalkmanFxSettings s = off();
    for (float &band : s.bands)
        band = 3;
    s.dcPhase = 1;
    s.dseeHx = 1;
    s.vinyl = 1;
    s.vinylType = 2;    // the arm: no noise of its own
    std::vector<float> frames = sine(44100, 50, 0.5, 4.0);
    for (size_t i = 44100 * 2; i < frames.size(); ++i)
        frames[i] = 0;
    const std::vector<float> out = run(s, frames);
    size_t nonzero = 0;
    for (size_t i = size_t(44100 * 3.5) * 2; i < out.size(); ++i)
        nonzero += out[i] != 0.0f;
    CHECK(nonzero == 0, "%zu samples still nonzero 1.5 s into silence", nonzero);
}

// The effect library, as the effects factory and AudioFlinger see it.

const effect_uuid_t kUuid = {0x7c3e4afc, 0x7189, 0x4cf4, 0x9ad2, {0x5c, 0x37, 0x72, 0x4c, 0xd6, 0xc7}};
const effect_uuid_t kTypeUuid = {0xf0463e98, 0xf8c3, 0x412b, 0x891d, {0xa1, 0x88, 0xb9, 0x22, 0xe2, 0xa0}};

int32_t sendCommand(effect_handle_t handle, uint32_t code, uint32_t size = 0, void *data = nullptr)
{
    int32_t status = 12345;
    uint32_t replySize = sizeof status;
    const int32_t result = (*handle)->command(handle, code, size, data, &replySize, &status);
    return result != 0 ? result : status == 12345 ? 0 : status;
}

effect_config_t stereoConfig(uint32_t rate, int16_t *in, int16_t *out, size_t frames)
{
    effect_config_t config;
    memset(&config, 0, sizeof config);
    config.inputCfg.samplingRate = config.outputCfg.samplingRate = rate;
    config.inputCfg.channels = config.outputCfg.channels = AUDIO_CHANNEL_OUT_STEREO;
    config.inputCfg.format = config.outputCfg.format = AUDIO_FORMAT_PCM_16_BIT;
    config.inputCfg.accessMode = EFFECT_BUFFER_ACCESS_READ;
    config.outputCfg.accessMode = in == out ? EFFECT_BUFFER_ACCESS_WRITE : EFFECT_BUFFER_ACCESS_ACCUMULATE;
    config.inputCfg.buffer.frameCount = config.outputCfg.buffer.frameCount = frames;
    config.inputCfg.buffer.s16 = in;
    config.outputCfg.buffer.s16 = out;
    config.inputCfg.mask = config.outputCfg.mask = EFFECT_CONFIG_ALL;
    return config;
}

int32_t sendSettings(effect_handle_t handle, const WalkmanFxSettings &settings)
{
    // effect_param_t as AudioEffect.setParameter(byte[], byte[]) builds it.
    std::vector<char> buffer(sizeof(effect_param_t) + sizeof(int32_t) + sizeof settings);
    effect_param_t *param = reinterpret_cast<effect_param_t *>(buffer.data());
    param->psize = sizeof(int32_t);
    param->vsize = sizeof settings;
    const int32_t key = WALKMANFX_PARAM_SETTINGS;
    memcpy(param->data, &key, sizeof key);
    memcpy(param->data + sizeof key, &settings, sizeof settings);
    return sendCommand(handle, EFFECT_CMD_SET_PARAM, buffer.size(), buffer.data());
}

void testLibrary(const char *path)
{
    void *library = dlopen(path, RTLD_NOW);
    CHECK(library, "dlopen %s: %s", path, dlerror());
    if (!library)
        return;
    const audio_effect_library_t *info
        = static_cast<const audio_effect_library_t *>(dlsym(library, AUDIO_EFFECT_LIBRARY_INFO_SYM_AS_STR));
    CHECK(info, "no %s symbol", AUDIO_EFFECT_LIBRARY_INFO_SYM_AS_STR);
    if (!info)
        return;
    CHECK(info->tag == AUDIO_EFFECT_LIBRARY_TAG, "library tag %08x", info->tag);
    CHECK(EFFECT_API_VERSION_MAJOR(info->version) == EFFECT_API_VERSION_MAJOR(EFFECT_LIBRARY_API_VERSION),
          "library version %08x", info->version);

    effect_descriptor_t descriptor;
    CHECK(info->get_descriptor(&kUuid, &descriptor) == 0, "no descriptor for the effect UUID");
    CHECK(memcmp(&descriptor.type, &kTypeUuid, sizeof kTypeUuid) == 0, "wrong type UUID");
    CHECK(EFFECT_API_VERSION_MAJOR(descriptor.apiVersion) == EFFECT_API_VERSION_MAJOR(EFFECT_CONTROL_API_VERSION),
          "control version %08x", descriptor.apiVersion);
    CHECK((descriptor.flags & EFFECT_FLAG_TYPE_MASK) == EFFECT_FLAG_TYPE_INSERT, "not an insert effect");
    CHECK(info->get_descriptor(&kTypeUuid, &descriptor) == -EINVAL, "a descriptor for the type UUID");

    effect_handle_t control = nullptr, music = nullptr, other = nullptr;
    CHECK(info->create_effect(&kTypeUuid, 9, 0, &other) == -EINVAL, "created from the type UUID");
    CHECK(info->create_effect(&kUuid, AUDIO_SESSION_OUTPUT_MIX, 0, &control) == 0, "no control instance");
    CHECK(info->create_effect(&kUuid, 9, 0, &music) == 0, "no music instance");
    if (!control || !music)
        return;

    const size_t frames = 512;
    std::vector<int16_t> in(frames * 2), out(frames * 2), mix(frames * 2);
    for (size_t i = 0; i < frames; ++i) {
        in[2 * i] = in[2 * i + 1] = int16_t(8000 * sin(2 * kPi * 1000 * i / 44100.0));
        mix[2 * i] = mix[2 * i + 1] = 100;
    }
    audio_buffer_t inBuffer, outBuffer;
    inBuffer.frameCount = outBuffer.frameCount = frames;
    inBuffer.s16 = in.data();
    outBuffer.s16 = out.data();

    effect_config_t config = stereoConfig(44100, in.data(), out.data(), frames);
    CHECK(sendCommand(music, EFFECT_CMD_INIT) == 0, "INIT");
    CHECK(sendCommand(music, EFFECT_CMD_SET_CONFIG, sizeof config, &config) == 0, "SET_CONFIG");
    effect_config_t mono = config;
    mono.inputCfg.channels = mono.outputCfg.channels = AUDIO_CHANNEL_OUT_MONO;
    CHECK(sendCommand(music, EFFECT_CMD_SET_CONFIG, sizeof mono, &mono) == -EINVAL, "mono accepted");
    effect_config_t back;
    uint32_t backSize = sizeof back;
    CHECK((*music)->command(music, EFFECT_CMD_GET_CONFIG, 0, nullptr, &backSize, &back) == 0
              && back.inputCfg.samplingRate == 44100 && back.outputCfg.accessMode == EFFECT_BUFFER_ACCESS_ACCUMULATE,
          "GET_CONFIG after a refused SET_CONFIG");

    // Disabled, the session still reaches the mix, and AudioFlinger is told to stop.
    out = mix;
    CHECK((*music)->process(music, &inBuffer, &outBuffer) == -ENODATA, "a disabled effect has data");
    CHECK(out[10] == mix[10] + in[10], "a disabled effect dropped its buffer: %d", out[10]);

    CHECK(sendCommand(music, EFFECT_CMD_ENABLE) == 0, "ENABLE");
    out = mix;
    CHECK((*music)->process(music, &inBuffer, &outBuffer) == 0, "process");
    bool passed = true;
    for (size_t i = 0; i < out.size(); ++i)
        passed &= out[i] == int16_t(mix[i] + in[i]);
    CHECK(passed, "with the effects off the session is not mixed unchanged");

    // The app sets the effects through its own instance on the output mix.
    WalkmanFxSettings settings = off();
    settings.bands[5] = 10;
    settings.bands[0] = -10;
    CHECK(sendSettings(control, settings) == 0, "SET_PARAM on the control instance");
    std::vector<char> query(sizeof(effect_param_t) + sizeof(int32_t) + sizeof settings);
    effect_param_t *param = reinterpret_cast<effect_param_t *>(query.data());
    param->psize = sizeof(int32_t);
    param->vsize = sizeof settings;
    const int32_t key = WALKMANFX_PARAM_SETTINGS;
    memcpy(param->data, &key, sizeof key);
    uint32_t querySize = query.size();
    CHECK((*music)->command(music, EFFECT_CMD_GET_PARAM, sizeof(effect_param_t) + sizeof key, query.data(), &querySize,
                            query.data())
                  == 0
              && param->status == 0 && memcmp(param->data + sizeof key, &settings, sizeof settings) == 0,
          "GET_PARAM does not return what SET_PARAM gave");
    WalkmanFxSettings shortSettings[2];
    CHECK(sendCommand(control, EFFECT_CMD_SET_PARAM, sizeof(effect_param_t) + 2, shortSettings) == -EINVAL,
          "a short SET_PARAM accepted");

    // The music session now runs the equalizer, the control instance does not.
    double changed = 0;
    for (int round = 0; round < 20; ++round) {
        out = mix;
        CHECK((*music)->process(music, &inBuffer, &outBuffer) == 0, "process");
    }
    for (size_t i = 0; i < out.size(); ++i)
        changed = fmax(changed, fabs(double(out[i]) - (mix[i] + in[i])));
    CHECK(changed > 500, "the equalizer did not reach the music session: %.0f", changed);

    std::vector<int16_t> global = in;
    audio_buffer_t globalBuffer = inBuffer;
    globalBuffer.s16 = global.data();
    effect_config_t inPlace = stereoConfig(44100, global.data(), global.data(), frames);
    CHECK(sendCommand(control, EFFECT_CMD_SET_CONFIG, sizeof inPlace, &inPlace) == 0, "control SET_CONFIG");
    CHECK(sendCommand(control, EFFECT_CMD_ENABLE) == 0, "control ENABLE");
    CHECK((*control)->process(control, &globalBuffer, &globalBuffer) == 0, "control process");
    CHECK(global == in, "the control instance processed the output mix");

    // Source Direct: the music session goes back to the plain stream.
    settings.mode = WALKMANFX_SOURCE_DIRECT;
    CHECK(sendSettings(control, settings) == 0, "SET_PARAM Source Direct");
    out = mix;
    (*music)->process(music, &inBuffer, &outBuffer);
    passed = true;
    for (size_t i = 0; i < out.size(); ++i)
        passed &= out[i] == int16_t(mix[i] + in[i]);
    CHECK(passed, "Source Direct is not bit exact");

    CHECK(sendCommand(music, EFFECT_CMD_DISABLE) == 0, "DISABLE");
    CHECK(info->release_effect(music) == 0 && info->release_effect(control) == 0, "release");
    dlclose(library);
}

}

int main(int argc, char **argv)
{
    testBypass();
    testEqualizer();
    testTone();
    testDcPhase();
    testDsee();
    testNormalizer();
    testVinyl();
    testLimiterAndClearAudio();
    testSilenceSettles();
    if (argc > 1)
        testLibrary(argv[1]);
    else
        printf("SKIP the library checks: no path to libwalkmanfx.so\n");
    printf("%s, %d failure(s)\n", g_failures ? "FAILED" : "PASSED", g_failures);
    return g_failures ? 1 : 0;
}
