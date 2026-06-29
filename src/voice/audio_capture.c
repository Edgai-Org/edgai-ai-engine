/* audio_capture.c — ALSA microphone capture for EduOS libedgai
 *
 * Opens the ALSA capture device at 16 kHz S16_LE mono (required by Vosk).
 * Uses goto cleanup to avoid leaking ALSA handles on any error path.
 * Returns EDGAI_ERR_AUDIO_BUSY if the device is held by another process,
 * allowing the caller to degrade gracefully to text-only mode.
 *
 * Author: Edgai Contributors
 * License: GPL v3
 */

/* 1. System includes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 2. Third-party includes */
#ifdef EDGAI_VOICE_ENABLED
#include <alsa/asoundlib.h>
#endif

/* 3. Local includes */
#include "edgai/edgai.h"
#include "audio_capture.h"

/* 4. Module-private constants */
#ifdef EDGAI_VOICE_ENABLED
#define EDGAI_CAPTURE_FORMAT    SND_PCM_FORMAT_S16_LE
#define EDGAI_CAPTURE_ACCESS    SND_PCM_ACCESS_RW_INTERLEAVED
#define EDGAI_CAPTURE_PERIODS   4   /* number of periods in ring buffer */
#endif

/* 5. Module-private types */
#ifdef EDGAI_VOICE_ENABLED
struct EdgaiAudioCapture {
    snd_pcm_t *pcm;
};
#else
struct EdgaiAudioCapture {
    int unused;
};
#endif

/* ── Public API ─────────────────────────────────────────────────────────── */

EdgaiAudioCapture *edgai_audio_capture_open(const char *device)
{
#ifndef EDGAI_VOICE_ENABLED
    (void)device;
    return NULL;
#else
    /* Resolve device name: argument → env var → default */
    if (!device || device[0] == '\0') {
        device = getenv("EDGAI_ALSA_DEVICE");
        if (!device || device[0] == '\0')
            device = "default";
    }

    EdgaiAudioCapture *cap = calloc(1, sizeof(EdgaiAudioCapture));
    if (!cap)
        return NULL;

    snd_pcm_hw_params_t *hwparams = NULL;
    int err;

    err = snd_pcm_open(&cap->pcm, device, SND_PCM_STREAM_CAPTURE, 0);
    if (err == -EBUSY) {
        fprintf(stderr, "edgai: ALSA device '%s' is busy — degrading to text mode\n",
                device);
        goto cleanup;
    }
    if (err < 0) {
        fprintf(stderr, "edgai: cannot open capture device '%s': %s\n",
                device, snd_strerror(err));
        goto cleanup;
    }

    snd_pcm_hw_params_alloca(&hwparams);

    err = snd_pcm_hw_params_any(cap->pcm, hwparams);
    if (err < 0) {
        fprintf(stderr, "edgai: cannot get hw params: %s\n", snd_strerror(err));
        goto cleanup;
    }

    err = snd_pcm_hw_params_set_access(cap->pcm, hwparams, EDGAI_CAPTURE_ACCESS);
    if (err < 0) {
        fprintf(stderr, "edgai: cannot set interleaved access: %s\n", snd_strerror(err));
        goto cleanup;
    }

    err = snd_pcm_hw_params_set_format(cap->pcm, hwparams, EDGAI_CAPTURE_FORMAT);
    if (err < 0) {
        fprintf(stderr, "edgai: cannot set S16_LE format: %s\n", snd_strerror(err));
        goto cleanup;
    }

    err = snd_pcm_hw_params_set_channels(cap->pcm, hwparams, EDGAI_CAPTURE_CHANNELS);
    if (err < 0) {
        fprintf(stderr, "edgai: cannot set mono: %s\n", snd_strerror(err));
        goto cleanup;
    }

    unsigned int rate = EDGAI_CAPTURE_RATE;
    err = snd_pcm_hw_params_set_rate_near(cap->pcm, hwparams, &rate, 0);
    if (err < 0) {
        fprintf(stderr, "edgai: cannot set %d Hz: %s\n", EDGAI_CAPTURE_RATE,
                snd_strerror(err));
        goto cleanup;
    }
    if (rate != EDGAI_CAPTURE_RATE) {
        fprintf(stderr, "edgai: ALSA gave %u Hz instead of %d — Vosk requires exactly %d\n",
                rate, EDGAI_CAPTURE_RATE, EDGAI_CAPTURE_RATE);
        goto cleanup;
    }

    snd_pcm_uframes_t period = EDGAI_CAPTURE_PERIOD_FRAMES;
    err = snd_pcm_hw_params_set_period_size_near(cap->pcm, hwparams, &period, 0);
    if (err < 0) {
        fprintf(stderr, "edgai: cannot set period size: %s\n", snd_strerror(err));
        goto cleanup;
    }

    snd_pcm_uframes_t buffer_size = period * EDGAI_CAPTURE_PERIODS;
    err = snd_pcm_hw_params_set_buffer_size_near(cap->pcm, hwparams, &buffer_size);
    if (err < 0) {
        fprintf(stderr, "edgai: cannot set buffer size: %s\n", snd_strerror(err));
        goto cleanup;
    }

    err = snd_pcm_hw_params(cap->pcm, hwparams);
    if (err < 0) {
        fprintf(stderr, "edgai: cannot apply hw params: %s\n", snd_strerror(err));
        goto cleanup;
    }

    err = snd_pcm_prepare(cap->pcm);
    if (err < 0) {
        fprintf(stderr, "edgai: cannot prepare capture device: %s\n", snd_strerror(err));
        goto cleanup;
    }

    return cap;

cleanup:
    if (cap->pcm) snd_pcm_close(cap->pcm);
    free(cap);
    return NULL;
#endif /* EDGAI_VOICE_ENABLED */
}

int edgai_audio_capture_read(EdgaiAudioCapture *cap,
                              int16_t *buf, size_t buf_frames)
{
#ifndef EDGAI_VOICE_ENABLED
    (void)cap; (void)buf; (void)buf_frames;
    return EDGAI_ERR_VOICE_UNAVAILABLE;
#else
    if (!cap || !buf || buf_frames == 0)
        return EDGAI_ERR_AUDIO_BUSY;

    snd_pcm_sframes_t n = snd_pcm_readi(cap->pcm, buf, (snd_pcm_uframes_t)buf_frames);
    if (n == -EPIPE) {
        /* Buffer overrun — recover and report */
        snd_pcm_prepare(cap->pcm);
        fprintf(stderr, "edgai: ALSA capture overrun — recovered\n");
        return EDGAI_ERR_AUDIO_BUSY;
    }
    if (n < 0) {
        fprintf(stderr, "edgai: ALSA read error: %s\n", snd_strerror((int)n));
        return EDGAI_ERR_AUDIO_BUSY;
    }
    return (int)n;
#endif /* EDGAI_VOICE_ENABLED */
}

void edgai_audio_capture_close(EdgaiAudioCapture *cap)
{
#ifdef EDGAI_VOICE_ENABLED
    if (!cap)
        return;
    if (cap->pcm)
        snd_pcm_close(cap->pcm);
    free(cap);
#else
    (void)cap;
#endif
}
