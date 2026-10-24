// libwalkmanfx.so, an Android 5.1 audio effect library around
// walkmanfx::Processor. audio_effects.conf attaches it to every music output
// session (output_session_processing), where the audio policy enables it.
// The app reaches all of those through one more instance of its own on the
// output mix: it never enables that one, it only sends settings through it,
// and every instance reads them from this library's shared copy.

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <new>

#include <hardware/audio_effect.h>

#include "fxsettings.h"
#include "walkmanfx.h"

namespace {

const effect_descriptor_t kDescriptor = {
    {0xf0463e98, 0xf8c3, 0x412b, 0x891d, {0xa1, 0x88, 0xb9, 0x22, 0xe2, 0xa0}},
    {0x7c3e4afc, 0x7189, 0x4cf4, 0x9ad2, {0x5c, 0x37, 0x72, 0x4c, 0xd6, 0xc7}},
    EFFECT_CONTROL_API_VERSION,
    EFFECT_FLAG_TYPE_INSERT | EFFECT_FLAG_INSERT_LAST,
    120,    // cpuLoad, 0.1 MIPS
    8,      // memoryUsage, KB
    "Walkman sound effects",
    "WalkmanUI",
};

pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
WalkmanFxSettings g_settings;   // all off until the app says otherwise
uint32_t g_generation = 1;

struct Context {
    const struct effect_interface_s *itfe;  // effect_handle_t points here
    walkmanfx::Processor processor;
    effect_config_t config;
    bool enabled;
    bool control;
    uint32_t generation;
    float *buffer;
    size_t capacity;
};

int16_t clip16(float value)
{
    const float scaled = value * 32768.0f;
    if (scaled >= 32767.0f)
        return 32767;
    if (scaled <= -32768.0f)
        return -32768;
    return int16_t(scaled < 0 ? scaled - 0.5f : scaled + 0.5f);
}

int16_t add16(int32_t a, int32_t b)
{
    const int32_t sum = a + b;
    return int16_t(sum > 32767 ? 32767 : sum < -32768 ? -32768 : sum);
}

void syncSettings(Context *context)
{
    if (__atomic_load_n(&g_generation, __ATOMIC_ACQUIRE) == context->generation)
        return;
    pthread_mutex_lock(&g_lock);
    const WalkmanFxSettings settings = g_settings;
    context->generation = g_generation;
    pthread_mutex_unlock(&g_lock);
    context->processor.apply(settings);
}

bool reserve(Context *context, size_t samples)
{
    if (samples <= context->capacity)
        return true;
    float *buffer = static_cast<float *>(realloc(context->buffer, samples * sizeof(float)));
    if (!buffer)
        return false;
    context->buffer = buffer;
    context->capacity = samples;
    return true;
}

bool validConfig(const effect_config_t &config)
{
    const buffer_config_t &in = config.inputCfg, &out = config.outputCfg;
    return in.samplingRate > 0 && in.samplingRate == out.samplingRate && in.channels == AUDIO_CHANNEL_OUT_STEREO
           && out.channels == AUDIO_CHANNEL_OUT_STEREO && in.format == AUDIO_FORMAT_PCM_16_BIT
           && out.format == AUDIO_FORMAT_PCM_16_BIT
           && (out.accessMode == EFFECT_BUFFER_ACCESS_WRITE || out.accessMode == EFFECT_BUFFER_ACCESS_ACCUMULATE);
}

int32_t process(effect_handle_t self, audio_buffer_t *in, audio_buffer_t *out)
{
    Context *context = reinterpret_cast<Context *>(self);
    if (!context || !in || !out || !in->raw || !out->raw || in->frameCount != out->frameCount)
        return -EINVAL;
    const bool active = context->enabled && !context->control;
    if (active)
        syncSettings(context);
    const size_t samples = in->frameCount * 2;
    const bool accumulate = context->config.outputCfg.accessMode == EFFECT_BUFFER_ACCESS_ACCUMULATE;
    // AudioFlinger keeps calling a disabled effect until it answers -ENODATA,
    // and that buffer still has to reach the output.
    if (!active || context->processor.idle()) {
        if (accumulate) {
            for (size_t i = 0; i < samples; ++i)
                out->s16[i] = add16(out->s16[i], in->s16[i]);
        } else if (in->raw != out->raw) {
            memcpy(out->raw, in->raw, samples * sizeof(int16_t));
        }
        return context->enabled ? 0 : -ENODATA;
    }
    if (!reserve(context, samples))
        return -ENOMEM;
    for (size_t i = 0; i < samples; ++i)
        context->buffer[i] = in->s16[i] * (1.0f / 32768.0f);
    context->processor.process(context->buffer, in->frameCount);
    for (size_t i = 0; i < samples; ++i) {
        const int16_t sample = clip16(context->buffer[i]);
        out->s16[i] = accumulate ? add16(out->s16[i], sample) : sample;
    }
    return 0;
}

int32_t reply(void *data, uint32_t *size, int32_t status)
{
    if (!data || !size || *size < sizeof(int32_t))
        return -EINVAL;
    *static_cast<int32_t *>(data) = status;
    return 0;
}

int32_t command(effect_handle_t self, uint32_t code, uint32_t size, void *data, uint32_t *replySize, void *replyData)
{
    Context *context = reinterpret_cast<Context *>(self);
    if (!context)
        return -EINVAL;
    switch (code) {
    case EFFECT_CMD_INIT:
        return reply(replyData, replySize, 0);
    case EFFECT_CMD_SET_CONFIG: {
        if (!data || size != sizeof(effect_config_t))
            return -EINVAL;
        const effect_config_t &config = *static_cast<const effect_config_t *>(data);
        if (!validConfig(config))
            return reply(replyData, replySize, -EINVAL);
        context->config = config;
        context->processor.configure(config.inputCfg.samplingRate);
        // So the audio thread does not allocate on its first buffer.
        reserve(context, config.inputCfg.buffer.frameCount * 2);
        return reply(replyData, replySize, 0);
    }
    case EFFECT_CMD_GET_CONFIG:
        if (!replyData || !replySize || *replySize != sizeof(effect_config_t))
            return -EINVAL;
        memcpy(replyData, &context->config, sizeof(effect_config_t));
        return 0;
    case EFFECT_CMD_RESET:
        context->processor.configure(context->config.inputCfg.samplingRate);
        return 0;
    case EFFECT_CMD_ENABLE:
    case EFFECT_CMD_DISABLE:
        context->enabled = code == EFFECT_CMD_ENABLE;
        return reply(replyData, replySize, 0);
    case EFFECT_CMD_SET_PARAM: {
        const effect_param_t *param = static_cast<const effect_param_t *>(data);
        if (!param || size < sizeof(effect_param_t) + sizeof(int32_t)
            || size < sizeof(effect_param_t) + param->psize + param->vsize)
            return -EINVAL;
        int32_t key;
        memcpy(&key, param->data, sizeof key);
        if (param->psize != sizeof(int32_t) || key != WALKMANFX_PARAM_SETTINGS
            || param->vsize != sizeof(WalkmanFxSettings))
            return reply(replyData, replySize, -EINVAL);
        pthread_mutex_lock(&g_lock);
        memcpy(&g_settings, param->data + sizeof(int32_t), sizeof(WalkmanFxSettings));
        __atomic_add_fetch(&g_generation, 1, __ATOMIC_RELEASE);
        pthread_mutex_unlock(&g_lock);
        return reply(replyData, replySize, 0);
    }
    case EFFECT_CMD_GET_PARAM: {
        const uint32_t needed = sizeof(effect_param_t) + sizeof(int32_t) + sizeof(WalkmanFxSettings);
        if (!data || size < sizeof(effect_param_t) + sizeof(int32_t) || !replyData || !replySize
            || *replySize < needed)
            return -EINVAL;
        memcpy(replyData, data, sizeof(effect_param_t) + sizeof(int32_t));
        effect_param_t *answer = static_cast<effect_param_t *>(replyData);
        int32_t key;
        memcpy(&key, answer->data, sizeof key);
        if (answer->psize != sizeof(int32_t) || key != WALKMANFX_PARAM_SETTINGS) {
            answer->status = -EINVAL;
            answer->vsize = 0;
            *replySize = sizeof(effect_param_t) + sizeof(int32_t);
            return 0;
        }
        pthread_mutex_lock(&g_lock);
        memcpy(answer->data + sizeof(int32_t), &g_settings, sizeof(WalkmanFxSettings));
        pthread_mutex_unlock(&g_lock);
        answer->status = 0;
        answer->vsize = sizeof(WalkmanFxSettings);
        *replySize = needed;
        return 0;
    }
    case EFFECT_CMD_SET_DEVICE:
    case EFFECT_CMD_SET_VOLUME:
    case EFFECT_CMD_SET_AUDIO_MODE:
        return 0;
    default:
        return -EINVAL;
    }
}

int32_t getDescriptor(effect_handle_t self, effect_descriptor_t *descriptor)
{
    if (!self || !descriptor)
        return -EINVAL;
    *descriptor = kDescriptor;
    return 0;
}

const struct effect_interface_s kInterface = {process, command, getDescriptor, nullptr};

bool sameUuid(const effect_uuid_t *uuid)
{
    return uuid && memcmp(uuid, &kDescriptor.uuid, sizeof(effect_uuid_t)) == 0;
}

int32_t createEffect(const effect_uuid_t *uuid, int32_t sessionId, int32_t, effect_handle_t *handle)
{
    if (!handle || !sameUuid(uuid))
        return -EINVAL;
    void *memory = calloc(1, sizeof(Context));
    if (!memory)
        return -ENOMEM;
    Context *context = new (memory) Context;
    context->itfe = &kInterface;
    memset(&context->config, 0, sizeof context->config);
    buffer_config_t *configs[] = {&context->config.inputCfg, &context->config.outputCfg};
    for (buffer_config_t *config : configs) {
        config->samplingRate = 44100;
        config->channels = AUDIO_CHANNEL_OUT_STEREO;
        config->format = AUDIO_FORMAT_PCM_16_BIT;
        config->accessMode = EFFECT_BUFFER_ACCESS_WRITE;
        config->mask = EFFECT_CONFIG_ALL;
    }
    context->config.inputCfg.accessMode = EFFECT_BUFFER_ACCESS_READ;
    context->processor.configure(44100);
    context->enabled = false;
    context->control = sessionId == AUDIO_SESSION_OUTPUT_MIX;
    context->generation = 0;
    context->buffer = nullptr;
    context->capacity = 0;
    *handle = reinterpret_cast<effect_handle_t>(context);
    return 0;
}

int32_t releaseEffect(effect_handle_t handle)
{
    Context *context = reinterpret_cast<Context *>(handle);
    if (!context)
        return -EINVAL;
    free(context->buffer);
    context->~Context();
    free(context);
    return 0;
}

int32_t libraryDescriptor(const effect_uuid_t *uuid, effect_descriptor_t *descriptor)
{
    if (!descriptor || !sameUuid(uuid))
        return -EINVAL;
    *descriptor = kDescriptor;
    return 0;
}

}

extern "C" {
__attribute__((visibility("default"))) audio_effect_library_t AUDIO_EFFECT_LIBRARY_INFO_SYM = {
    AUDIO_EFFECT_LIBRARY_TAG,
    EFFECT_LIBRARY_API_VERSION,
    "Walkman sound effects",
    "WalkmanUI",
    createEffect,
    releaseEffect,
    libraryDescriptor,
};
}
