/*
 * Primary audio HAL for the NW-A55 (Sony CXD3778GF on ALSA card 0 "sonysoccard").
 *
 * The Walkman ships no standard Android audio HAL (Sony drives the DAC with their
 * own libaudiohal-*alsa from the Qt app). This is a minimal tinyalsa-based
 * audio.primary that lets the CM-container audioflinger reach the DAC: on open it
 * asserts the mixer route proved in devlog/2026-10-15-walkman-dac-audio-proven.md,
 * and an output stream writes S16_LE/48k PCM to hw:0,0 (pcmC0D0p).
 *
 * Built out-of-tree (NDK r21e) against vendored AOSP 5.1 headers; audio device API
 * 3.0 (the 5.1 current; MIN is 2.0). Output-focused — input is a stub.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#define LOG_TAG "nwa55_audio_hw"
#include <cutils/log.h>

#include <hardware/hardware.h>
#include <system/audio.h>
#include <hardware/audio.h>

#include <tinyalsa/asoundlib.h>

#define PCM_CARD   0
#define PCM_DEVICE 0
#define OUT_RATE   48000

static const struct pcm_config pcm_config_out = {
    .channels = 2,
    .rate = OUT_RATE,
    .period_size = 1024,
    .period_count = 4,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .stop_threshold = 0,
    .silence_threshold = 0,
};

struct nwa55_audio_device {
    struct audio_hw_device device;
    pthread_mutex_t lock;
    struct mixer *mixer;
    float master_volume;
};

struct nwa55_stream_out {
    struct audio_stream_out stream;
    pthread_mutex_t lock;
    struct pcm *pcm;
    bool standby;
    struct nwa55_audio_device *adev;
};

/* ---- mixer: the CXD3778GF enable sequence (names/types from amixer -c 0) ---- */

static void mx_bool(struct mixer *m, const char *name, int v)
{
    struct mixer_ctl *c = mixer_get_ctl_by_name(m, name);
    if (c) mixer_ctl_set_value(c, 0, v);
    else ALOGW("mixer: no bool ctl '%s'", name);
}

static void mx_int(struct mixer *m, const char *name, int v)
{
    struct mixer_ctl *c = mixer_get_ctl_by_name(m, name);
    if (c) mixer_ctl_set_value(c, 0, v);
    else ALOGW("mixer: no int ctl '%s'", name);
}

static void mx_enum(struct mixer *m, const char *name, const char *s)
{
    struct mixer_ctl *c = mixer_get_ctl_by_name(m, name);
    if (c) mixer_ctl_set_enum_by_string(c, s);
    else ALOGW("mixer: no enum ctl '%s'", name);
}

/* Routes the headphone output and lifts the two mutes that block it. Volume
 * stages start conservative; master volume also tracks set_master_volume(). */
static void apply_output_route(struct nwa55_audio_device *adev)
{
    struct mixer *m = adev->mixer;
    if (!m) return;
    mx_bool(m, "standby", 0);
    mx_enum(m, "output device", "headphone");
    mx_enum(m, "headphone amp", "smaster-se");
    mx_enum(m, "headphone smaster se gain mode", "normal");
    mx_bool(m, "playback mute", 0);
    mx_bool(m, "analog stream mute", 0);
    mx_bool(m, "analog playback mute", 0);   /* was the blocker */
    mx_int(m, "master gain", 8);
    mx_int(m, "master volume", (int)(adev->master_volume * 120.0f));
    mx_int(m, "Master Volume", 60);
    mx_bool(m, "Master Switch", 1);          /* was the blocker */
}

/* ------------------------------ output stream ------------------------------ */

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    (void)stream; return OUT_RATE;
}
static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    (void)stream; (void)rate; return 0;
}
static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    (void)stream;
    return pcm_config_out.period_size * pcm_config_out.channels * sizeof(int16_t);
}
static audio_channel_mask_t out_get_channels(const struct audio_stream *stream)
{
    (void)stream; return AUDIO_CHANNEL_OUT_STEREO;
}
static audio_format_t out_get_format(const struct audio_stream *stream)
{
    (void)stream; return AUDIO_FORMAT_PCM_16_BIT;
}
static int out_set_format(struct audio_stream *stream, audio_format_t format)
{
    (void)stream; (void)format; return 0;
}

static int out_standby(struct audio_stream *stream)
{
    struct nwa55_stream_out *out = (struct nwa55_stream_out *)stream;
    pthread_mutex_lock(&out->lock);
    if (!out->standby && out->pcm) {
        pcm_close(out->pcm);
        out->pcm = NULL;
    }
    out->standby = true;
    pthread_mutex_unlock(&out->lock);
    return 0;
}

static int out_dump(const struct audio_stream *stream, int fd)
{
    (void)stream; (void)fd; return 0;
}
static audio_devices_t out_get_device(const struct audio_stream *stream)
{
    (void)stream; return AUDIO_DEVICE_OUT_WIRED_HEADPHONE;
}
static int out_set_device(struct audio_stream *stream, audio_devices_t device)
{
    (void)stream; (void)device; return 0;
}
static int out_set_parameters(struct audio_stream *stream, const char *kv_pairs)
{
    (void)stream; (void)kv_pairs; return 0;
}
static char *out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    (void)stream; (void)keys; return strdup("");
}
static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    (void)stream; (void)effect; return 0;
}
static int out_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    (void)stream; (void)effect; return 0;
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    (void)stream;
    return (pcm_config_out.period_size * pcm_config_out.period_count * 1000) / OUT_RATE;
}
static int out_set_volume(struct audio_stream_out *stream, float left, float right)
{
    (void)stream; (void)left; (void)right; return -ENOSYS;   /* use master volume */
}

static ssize_t out_write(struct audio_stream_out *stream, const void *buffer, size_t bytes)
{
    struct nwa55_stream_out *out = (struct nwa55_stream_out *)stream;
    int ret;

    pthread_mutex_lock(&out->lock);
    if (out->standby) {
        /* pcm_open writes the negotiated period back into *config, so hand it a
         * writable local copy -- never the shared const template, which lives in
         * .rodata and faulted mediaserver on the device. The copy also has to
         * match the image libtinyalsa's struct layout (incl. avail_min) or the
         * lib touches memory past it. */
        struct pcm_config config = pcm_config_out;
        out->pcm = pcm_open(PCM_CARD, PCM_DEVICE, PCM_OUT, &config);
        if (!out->pcm || !pcm_is_ready(out->pcm)) {
            ALOGE("pcm_open(%u,%u) failed: %s", PCM_CARD, PCM_DEVICE,
                  out->pcm ? pcm_get_error(out->pcm) : "no pcm");
            if (out->pcm) { pcm_close(out->pcm); out->pcm = NULL; }
            pthread_mutex_unlock(&out->lock);
            /* pace the caller so a dead device does not spin audioflinger */
            usleep((int64_t)bytes * 1000000
                   / (pcm_config_out.channels * sizeof(int16_t)) / OUT_RATE);
            return bytes;
        }
        pthread_mutex_lock(&out->adev->lock);
        if (!out->adev->mixer)
            out->adev->mixer = mixer_open(PCM_CARD);   /* lazy retry if adev_open missed it */
        apply_output_route(out->adev);
        pthread_mutex_unlock(&out->adev->lock);
        out->standby = false;
    }

    ret = pcm_write(out->pcm, buffer, bytes);
    if (ret != 0) {
        ALOGE("pcm_write failed: %d (%s)", ret, pcm_get_error(out->pcm));
        /* drop the stream and pace, rather than returning an error that spins
         * AudioFlinger's MixerThread into a retry loop flooding the log */
        pcm_close(out->pcm);
        out->pcm = NULL;
        out->standby = true;
        pthread_mutex_unlock(&out->lock);
        usleep((int64_t)bytes * 1000000
               / (pcm_config_out.channels * sizeof(int16_t)) / OUT_RATE);
        return bytes;
    }
    pthread_mutex_unlock(&out->lock);
    return bytes;
}

static int out_get_render_position(const struct audio_stream_out *stream, uint32_t *dsp_frames)
{
    (void)stream; (void)dsp_frames; return -EINVAL;
}
static int out_get_next_write_timestamp(const struct audio_stream_out *stream, int64_t *timestamp)
{
    (void)stream; (void)timestamp; return -EINVAL;
}
static int out_set_callback(struct audio_stream_out *stream, stream_callback_t cb, void *cookie)
{
    (void)stream; (void)cb; (void)cookie; return -ENOSYS;
}
static int out_pause(struct audio_stream_out *stream) { (void)stream; return -ENOSYS; }
static int out_resume(struct audio_stream_out *stream) { (void)stream; return -ENOSYS; }
static int out_drain(struct audio_stream_out *stream, audio_drain_type_t type)
{
    (void)stream; (void)type; return -ENOSYS;
}
static int out_flush(struct audio_stream_out *stream) { (void)stream; return -ENOSYS; }
static int out_get_presentation_position(const struct audio_stream_out *stream,
                                         uint64_t *frames, struct timespec *timestamp)
{
    (void)stream; (void)frames; (void)timestamp; return -EINVAL;
}

/* ------------------------------ device ------------------------------------- */

static int adev_open_output_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out,
                                   const char *address)
{
    struct nwa55_audio_device *adev = (struct nwa55_audio_device *)dev;
    struct nwa55_stream_out *out;
    (void)handle; (void)devices; (void)flags; (void)address;

    out = calloc(1, sizeof(*out));
    if (!out) return -ENOMEM;

    pthread_mutex_init(&out->lock, NULL);
    out->adev = adev;
    out->standby = true;
    out->pcm = NULL;

    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.standby = out_standby;
    out->stream.common.dump = out_dump;
    out->stream.common.get_device = out_get_device;
    out->stream.common.set_device = out_set_device;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;
    out->stream.get_latency = out_get_latency;
    out->stream.set_volume = out_set_volume;
    out->stream.write = out_write;
    out->stream.get_render_position = out_get_render_position;
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;
    out->stream.set_callback = out_set_callback;
    out->stream.pause = out_pause;
    out->stream.resume = out_resume;
    out->stream.drain = out_drain;
    out->stream.flush = out_flush;
    out->stream.get_presentation_position = out_get_presentation_position;

    /* We only play S16_LE/48k/stereo; report that back to the framework, which
     * resamples/mixes to match. */
    config->sample_rate = OUT_RATE;
    config->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    config->format = AUDIO_FORMAT_PCM_16_BIT;

    *stream_out = &out->stream;
    ALOGI("opened output stream (hw:%u,%u S16_LE %u stereo)", PCM_CARD, PCM_DEVICE, OUT_RATE);
    return 0;
}

static void adev_close_output_stream(struct audio_hw_device *dev,
                                     struct audio_stream_out *stream)
{
    (void)dev;
    out_standby(&stream->common);
    free(stream);
}

static int adev_init_check(const struct audio_hw_device *dev)
{
    /* Always OK: a missing mixer must not fail the primary (that would leave
     * AudioFlinger without one and wedge system_server); out_write retries
     * mixer_open lazily and the route is re-asserted there. */
    (void)dev;
    return 0;
}
static int adev_set_voice_volume(struct audio_hw_device *dev, float volume)
{
    (void)dev; (void)volume; return 0;
}
static int adev_set_master_volume(struct audio_hw_device *dev, float volume)
{
    struct nwa55_audio_device *adev = (struct nwa55_audio_device *)dev;
    pthread_mutex_lock(&adev->lock);
    adev->master_volume = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
    if (adev->mixer) mx_int(adev->mixer, "master volume", (int)(adev->master_volume * 120.0f));
    pthread_mutex_unlock(&adev->lock);
    return 0;
}
static int adev_get_master_volume(struct audio_hw_device *dev, float *volume)
{
    struct nwa55_audio_device *adev = (struct nwa55_audio_device *)dev;
    *volume = adev->master_volume;
    return 0;
}
static int adev_set_mode(struct audio_hw_device *dev, audio_mode_t mode)
{
    (void)dev; (void)mode; return 0;
}
static int adev_set_mic_mute(struct audio_hw_device *dev, bool state)
{
    (void)dev; (void)state; return 0;
}
static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state)
{
    (void)dev; *state = false; return 0;
}
static int adev_set_parameters(struct audio_hw_device *dev, const char *kv_pairs)
{
    (void)dev; (void)kv_pairs; return 0;
}
static char *adev_get_parameters(const struct audio_hw_device *dev, const char *keys)
{
    (void)dev; (void)keys; return strdup("");
}
static size_t adev_get_input_buffer_size(const struct audio_hw_device *dev,
                                         const struct audio_config *config)
{
    (void)dev; (void)config; return 320 * sizeof(int16_t) * 2;
}
static int adev_open_input_stream(struct audio_hw_device *dev, audio_io_handle_t handle,
                                  audio_devices_t devices, struct audio_config *config,
                                  struct audio_stream_in **stream_in,
                                  audio_input_flags_t flags, const char *address,
                                  audio_source_t source)
{
    (void)dev; (void)handle; (void)devices; (void)config; (void)stream_in;
    (void)flags; (void)address; (void)source;
    return -ENOSYS;   /* output-only HAL */
}
static void adev_close_input_stream(struct audio_hw_device *dev, struct audio_stream_in *stream)
{
    (void)dev; (void)stream;
}
static int adev_dump(const struct audio_hw_device *dev, int fd)
{
    (void)dev; (void)fd; return 0;
}
static int adev_set_master_mute(struct audio_hw_device *dev, bool mute)
{
    struct nwa55_audio_device *adev = (struct nwa55_audio_device *)dev;
    if (adev->mixer) mx_bool(adev->mixer, "playback mute", mute ? 1 : 0);
    return 0;
}
static int adev_get_master_mute(struct audio_hw_device *dev, bool *mute)
{
    (void)dev; *mute = false; return 0;
}
static int adev_create_audio_patch(struct audio_hw_device *dev, unsigned int num_sources,
                                   const struct audio_port_config *sources,
                                   unsigned int num_sinks,
                                   const struct audio_port_config *sinks,
                                   audio_patch_handle_t *handle)
{
    (void)dev; (void)num_sources; (void)sources; (void)num_sinks; (void)sinks; (void)handle;
    return -ENOSYS;   /* framework falls back to set_parameters routing */
}
static int adev_release_audio_patch(struct audio_hw_device *dev, audio_patch_handle_t handle)
{
    (void)dev; (void)handle; return -ENOSYS;
}
static int adev_get_audio_port(struct audio_hw_device *dev, struct audio_port *port)
{
    (void)dev; (void)port; return -ENOSYS;
}
static int adev_set_audio_port_config(struct audio_hw_device *dev,
                                      const struct audio_port_config *config)
{
    (void)dev; (void)config; return -ENOSYS;
}

static int adev_close(hw_device_t *device)
{
    struct nwa55_audio_device *adev = (struct nwa55_audio_device *)device;
    if (adev->mixer) mixer_close(adev->mixer);
    free(adev);
    return 0;
}

static int adev_open(const hw_module_t *module, const char *name, hw_device_t **device)
{
    struct nwa55_audio_device *adev;

    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0)
        return -EINVAL;

    adev = calloc(1, sizeof(*adev));
    if (!adev) return -ENOMEM;

    pthread_mutex_init(&adev->lock, NULL);
    adev->master_volume = 0.35f;
    adev->mixer = mixer_open(PCM_CARD);
    if (!adev->mixer)
        ALOGE("mixer_open(%u) failed — output route will not be asserted", PCM_CARD);
    else
        apply_output_route(adev);

    adev->device.common.tag = HARDWARE_DEVICE_TAG;
    adev->device.common.version = AUDIO_DEVICE_API_VERSION_3_0;
    adev->device.common.module = (struct hw_module_t *)module;
    adev->device.common.close = adev_close;

    adev->device.get_supported_devices = NULL;   /* >= 2.0 uses audio_policy.conf */
    adev->device.init_check = adev_init_check;
    adev->device.set_voice_volume = adev_set_voice_volume;
    adev->device.set_master_volume = adev_set_master_volume;
    adev->device.get_master_volume = adev_get_master_volume;
    adev->device.set_mode = adev_set_mode;
    adev->device.set_mic_mute = adev_set_mic_mute;
    adev->device.get_mic_mute = adev_get_mic_mute;
    adev->device.set_parameters = adev_set_parameters;
    adev->device.get_parameters = adev_get_parameters;
    adev->device.get_input_buffer_size = adev_get_input_buffer_size;
    adev->device.open_output_stream = adev_open_output_stream;
    adev->device.close_output_stream = adev_close_output_stream;
    adev->device.open_input_stream = adev_open_input_stream;
    adev->device.close_input_stream = adev_close_input_stream;
    adev->device.dump = adev_dump;
    adev->device.set_master_mute = adev_set_master_mute;
    adev->device.get_master_mute = adev_get_master_mute;
    adev->device.create_audio_patch = adev_create_audio_patch;
    adev->device.release_audio_patch = adev_release_audio_patch;
    adev->device.get_audio_port = adev_get_audio_port;
    adev->device.set_audio_port_config = adev_set_audio_port_config;

    *device = &adev->device.common;
    ALOGI("NW-A55 CXD3778GF primary HAL opened (mixer %s)", adev->mixer ? "ok" : "MISSING");
    return 0;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "NW-A55 CXD3778GF primary audio HAL",
        .author = "nw-a55",
        .methods = &hal_module_methods,
    },
};
