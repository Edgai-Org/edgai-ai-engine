/* transcribe.c — Vosk STT + ALSA capture + VAD integration for libedgai
 *
 * Pipeline per edgai_transcribe_listen() call:
 *   1. Open ALSA mic capture at 16 kHz S16_LE mono
 *   2. Feed 20 ms frames into both the VAD and the Vosk recognizer
 *   3. Stop when VAD reports DONE (800 ms silence) or TIMEOUT (10 s)
 *   4. Extract transcript text from Vosk's JSON result
 *   5. Return a heap-allocated NUL-terminated string (caller must free())
 *
 * 2 GB tier: load model → create recognizer → transcribe → free model.
 * 4 GB+ tier: model stays in session->vosk_model; only recognizer is
 *              created and freed per call.
 *
 * Author: Edgai Contributors
 * License: GPL v3
 */

/* 1. System includes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* 2. Third-party includes */
#ifdef EDGAI_VOICE_ENABLED
#include "../../vendor/vosk/vosk_api.h"
#endif

/* 3. Local includes */
#include "edgai/edgai.h"
#include "transcribe.h"
#include "audio_capture.h"
#include "vad.h"

/* 4. Module-private constants */
/* One VAD frame = 20 ms at 16 kHz = 320 samples */
#define EDGAI_VAD_FRAME_SAMPLES \
    ((EDGAI_CAPTURE_RATE * EDGAI_VAD_FRAME_MS) / 1000)

/* ALSA reads PERIOD_FRAMES (4096) at once; VAD processes 320-sample chunks */
#define EDGAI_CAPTURE_PERIOD_SAMPLES EDGAI_CAPTURE_PERIOD_FRAMES

/* Maximum size of the Vosk JSON result we'll ever copy from */
#define EDGAI_VOSK_JSON_MAX 2048

/* 5. Module-private types — none */

/* 6. Static function declarations */
#ifdef EDGAI_VOICE_ENABLED
static const char *resolve_vosk_model(void);
static char       *extract_text_from_json(const char *json);
static char       *run_recognition(EdgaiSession *session,
                                    VoskModel    *model);
#endif

/* ── Public API ─────────────────────────────────────────────────────────── */

int edgai_transcribe_init(EdgaiSession *session)
{
    if (!session)
        return EDGAI_ERR_INTERNAL;

#ifndef EDGAI_VOICE_ENABLED
    session->voice_enabled = 0;
    return EDGAI_OK;
#else
    vosk_set_log_level(-1); /* suppress Vosk diagnostic output */

    /* On 2 GB tier, skip upfront load — model loaded per-call */
    if (session->is_mobile) {
        const char *path = resolve_vosk_model();
        if (!path) {
            fprintf(stderr,
                    "edgai: Vosk model not found — text-only mode\n"
                    "       Set EDGAI_VOSK_MODEL or download to "
                    "~/.edgai/models/vosk/vosk-model-small-en-us-0.15/\n");
            session->voice_enabled = 0;
            return EDGAI_ERR_VOICE_UNAVAILABLE;
        }
        /* model path is valid; will load on first call */
        return EDGAI_OK;
    }

    /* 4 GB+ tier: load model once */
    const char *path = resolve_vosk_model();
    if (!path) {
        fprintf(stderr,
                "edgai: Vosk model not found — text-only mode\n"
                "       Set EDGAI_VOSK_MODEL or download to "
                "~/.edgai/models/vosk/vosk-model-small-en-us-0.15/\n");
        session->voice_enabled = 0;
        return EDGAI_ERR_VOICE_UNAVAILABLE;
    }

    session->vosk_model = vosk_model_new(path);
    if (!session->vosk_model) {
        fprintf(stderr, "edgai: failed to load Vosk model from: %s\n", path);
        session->voice_enabled = 0;
        return EDGAI_ERR_VOICE_UNAVAILABLE;
    }

    return EDGAI_OK;
#endif
}

char *edgai_transcribe_listen(EdgaiSession *session)
{
    if (!session || !session->voice_enabled)
        return NULL;

#ifndef EDGAI_VOICE_ENABLED
    return NULL;
#else
    session->voice_state = EDGAI_VOICE_STATE_LISTENING;

    /* ── 2 GB tier: load model for this call ────────────────────────── */
    int         loaded_for_call = 0;
    VoskModel  *model           = (VoskModel *)session->vosk_model;

    if (session->is_mobile && !model) {
        const char *path = resolve_vosk_model();
        if (!path) {
            session->voice_state = EDGAI_VOICE_STATE_IDLE;
            return NULL;
        }
        model = vosk_model_new(path);
        if (!model) {
            session->voice_state = EDGAI_VOICE_STATE_IDLE;
            return NULL;
        }
        loaded_for_call = 1;
    }

    char *transcript = run_recognition(session, model);

    /* ── 2 GB tier: free model immediately ──────────────────────────── */
    if (loaded_for_call && model) {
        vosk_model_free(model);
        /* Do NOT write back to session->vosk_model — 4 GB keeps it resident */
    }

    if (transcript)
        session->voice_state = EDGAI_VOICE_STATE_TRANSCRIBED;
    else
        session->voice_state = EDGAI_VOICE_STATE_IDLE;

    return transcript;
#endif
}

void edgai_transcribe_destroy(EdgaiSession *session)
{
    if (!session)
        return;
#ifdef EDGAI_VOICE_ENABLED
    if (session->vosk_model) {
        vosk_model_free((VoskModel *)session->vosk_model);
        session->vosk_model = NULL;
    }
    if (session->vosk_rec) {
        vosk_recognizer_free((VoskRecognizer *)session->vosk_rec);
        session->vosk_rec = NULL;
    }
#endif
}

/* ── Static helpers ─────────────────────────────────────────────────────── */

#ifdef EDGAI_VOICE_ENABLED

/* Resolve the Vosk model directory path.
 * Search order: EDGAI_VOSK_MODEL env var → ~/.edgai/models/vosk/ default. */
static const char *resolve_vosk_model(void)
{
    const char *env = getenv("EDGAI_VOSK_MODEL");
    if (env && env[0] != '\0') {
        /* Minimal check: directory must be stat-able */
        FILE *probe = fopen(env, "r");
        if (!probe) {
            /* Vosk model paths are directories, not files — check via am/final.mdl */
            static char probe_path[4096];
            snprintf(probe_path, sizeof(probe_path), "%s/am/final.mdl", env);
            probe = fopen(probe_path, "rb");
        }
        if (probe) {
            fclose(probe);
            return env;
        }
        fprintf(stderr, "edgai: EDGAI_VOSK_MODEL='%s' not found or invalid\n", env);
        return NULL;
    }

    static char path[4096];
    const char *home = getenv("HOME");
    if (home) {
        snprintf(path, sizeof(path),
                 "%s/.edgai/models/vosk/vosk-model-small-en-us-0.15", home);
        char probe_path[4096];
        snprintf(probe_path, sizeof(probe_path), "%s/am/final.mdl", path);
        FILE *f = fopen(probe_path, "rb");
        if (f) {
            fclose(f);
            return path;
        }
    }
    return NULL;
}

/*
 * Extract the "text" value from a Vosk JSON result string.
 * Input: '{"text": "hello world"}' or '{"text": ""}'
 * Returns a heap-allocated copy of the text value (may be empty string).
 * Returns NULL on parse failure or if text is empty.
 * Caller must free() the returned string.
 */
static char *extract_text_from_json(const char *json)
{
    if (!json)
        return NULL;

    /* Find "text" key */
    const char *key = strstr(json, "\"text\"");
    if (!key)
        return NULL;

    /* Skip past "text" : " */
    const char *colon = strchr(key + 6, ':');
    if (!colon)
        return NULL;

    const char *quote = strchr(colon + 1, '"');
    if (!quote)
        return NULL;

    const char *text_start = quote + 1;
    const char *text_end   = strchr(text_start, '"');
    if (!text_end)
        return NULL;

    size_t len = (size_t)(text_end - text_start);
    if (len == 0)
        return NULL; /* Vosk returned empty transcript — no speech detected */

    char *result = malloc(len + 1);
    if (!result)
        return NULL;

    memcpy(result, text_start, len);
    result[len] = '\0';
    return result;
}

/*
 * Core recognition loop:
 *   - Opens ALSA mic at 16 kHz
 *   - Reads PERIOD_FRAMES frames per ALSA call
 *   - Feeds 320-sample (20 ms) VAD frames from the ALSA buffer
 *   - Feeds the full ALSA buffer to Vosk
 *   - Stops when VAD reports DONE or TIMEOUT
 *   - Returns heap-allocated transcript string or NULL
 */
static char *run_recognition(EdgaiSession *session, VoskModel *model)
{
    if (!model)
        return NULL;

    /* Create per-call recognizer */
    VoskRecognizer *rec = vosk_recognizer_new(model,
                                               (float)EDGAI_CAPTURE_RATE);
    if (!rec) {
        fprintf(stderr, "edgai: failed to create Vosk recognizer\n");
        return NULL;
    }

    /* Align Vosk's built-in endpointer with our VAD settings */
    vosk_recognizer_set_endpointer_delays(rec,
        5.0f,   /* t_start_max: 5 s silence at start before giving up */
        0.8f,   /* t_end: 800 ms trailing silence = end of utterance   */
        10.0f   /* t_max: 10 s absolute maximum                        */
    );

    /* Open ALSA mic */
    EdgaiAudioCapture *cap = edgai_audio_capture_open(NULL);
    if (!cap) {
        fprintf(stderr, "edgai: cannot open microphone\n");
        vosk_recognizer_free(rec);
        return NULL;
    }

    /* Create VAD */
    EdgaiVad *vad = edgai_vad_create();
    if (!vad) {
        edgai_audio_capture_close(cap);
        vosk_recognizer_free(rec);
        return NULL;
    }

    int16_t period_buf[EDGAI_CAPTURE_PERIOD_SAMPLES];
    char   *transcript       = NULL;
    int     vosk_final_seen  = 0;

    while (!vosk_final_seen) {
        /* Read one ALSA period */
        int frames_read = edgai_audio_capture_read(cap, period_buf,
                                                    EDGAI_CAPTURE_PERIOD_SAMPLES);
        if (frames_read <= 0)
            break;

        /* Feed ALSA buffer to Vosk first (full period, byte count) */
        int vosk_done = vosk_recognizer_accept_waveform(rec,
                            (const char *)period_buf,
                            frames_read * (int)sizeof(int16_t));

        /* Run VAD over the period in 20 ms sub-frames */
        int16_t     *vad_ptr     = period_buf;
        int          frames_left = frames_read;
        EdgaiVadState vad_state  = VAD_STATE_SILENCE;

        while (frames_left >= EDGAI_VAD_FRAME_SAMPLES) {
            vad_state = edgai_vad_process(vad, vad_ptr,
                                           EDGAI_VAD_FRAME_SAMPLES);
            vad_ptr    += EDGAI_VAD_FRAME_SAMPLES;
            frames_left -= EDGAI_VAD_FRAME_SAMPLES;

            if (vad_state == VAD_STATE_DONE || vad_state == VAD_STATE_TIMEOUT) {
                vosk_final_seen = 1;
                break;
            }
        }

        /* Vosk signaled complete utterance */
        if (vosk_done) {
            vosk_final_seen = 1;
        }
    }

    /* Extract transcript from Vosk */
    const char *json = vosk_recognizer_result(rec);
    if (json)
        transcript = extract_text_from_json(json);

    /* Cleanup */
    edgai_vad_destroy(vad);
    edgai_audio_capture_close(cap);
    vosk_recognizer_free(rec);

    (void)session; /* session->voice_state updated by caller */
    return transcript;
}

#endif /* EDGAI_VOICE_ENABLED */
