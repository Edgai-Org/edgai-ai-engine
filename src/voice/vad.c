/* vad.c — Energy-based Voice Activity Detection for EduOS libedgai
 *
 * Computes RMS energy per 20 ms frame and drives a simple four-state machine:
 *
 *   SILENCE → SPEECH (energy crosses threshold)
 *   SPEECH  → TRAILING_SILENCE (energy drops below threshold)
 *   TRAILING_SILENCE → SPEECH (energy rises again — student still talking)
 *   TRAILING_SILENCE → DONE (EDGAI_VAD_TRAIL_MS of continuous silence)
 *   any state → TIMEOUT (EDGAI_VAD_MAX_MS of total time elapsed)
 *
 * The 800 ms trailing-silence window is intentionally generous. Nigerian
 * secondary school students often pause mid-sentence while recalling a term
 * (e.g. "what is... erm... logarithm"). Cutting them off at 200-300 ms
 * would force repeated starts and break Vosk recognition accuracy.
 *
 * Author: Edgai Contributors
 * License: GPL v3
 */

/* 1. System includes */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* 2. Third-party includes — none */

/* 3. Local includes */
#include "vad.h"

/* 4. Module-private constants */
/* Frames elapsed per millisecond at EDGAI_CAPTURE_RATE with EDGAI_VAD_FRAME_MS */
#define FRAMES_PER_MS       (EDGAI_VAD_FRAME_MS)
#define TRAIL_FRAMES        (EDGAI_VAD_TRAIL_MS  / EDGAI_VAD_FRAME_MS)
#define MAX_FRAMES          (EDGAI_VAD_MAX_MS    / EDGAI_VAD_FRAME_MS)

/* 5. Module-private types */
struct EdgaiVad {
    EdgaiVadState state;
    int           frames_total;    /* total frames fed since last reset */
    int           silence_frames;  /* consecutive silent frames since last speech */
};

/* 6. Static function declarations */
static uint32_t compute_rms(const int16_t *frame, size_t n_samples);

/* ── Public API ─────────────────────────────────────────────────────────── */

EdgaiVad *edgai_vad_create(void)
{
    EdgaiVad *vad = calloc(1, sizeof(EdgaiVad));
    return vad; /* calloc zeroes all fields → state = VAD_STATE_SILENCE */
}

void edgai_vad_destroy(EdgaiVad *vad)
{
    free(vad);
}

EdgaiVadState edgai_vad_process(EdgaiVad *vad,
                                 const int16_t *frame,
                                 size_t frame_samples)
{
    if (!vad || !frame || frame_samples == 0)
        return VAD_STATE_SILENCE;

    /* Terminal states are sticky — stop processing once reached */
    if (vad->state == VAD_STATE_DONE || vad->state == VAD_STATE_TIMEOUT)
        return vad->state;

    vad->frames_total++;

    /* Hard timeout — force end regardless of energy level */
    if (vad->frames_total >= MAX_FRAMES) {
        vad->state = VAD_STATE_TIMEOUT;
        return vad->state;
    }

    uint32_t rms = compute_rms(frame, frame_samples);
    int is_speech = (rms >= (uint32_t)EDGAI_VAD_SPEECH_THRESHOLD);

    switch (vad->state) {
    case VAD_STATE_SILENCE:
        if (is_speech) {
            vad->state         = VAD_STATE_SPEECH;
            vad->silence_frames = 0;
        }
        break;

    case VAD_STATE_SPEECH:
        if (!is_speech) {
            vad->state          = VAD_STATE_TRAILING_SILENCE;
            vad->silence_frames = 1;
        }
        break;

    case VAD_STATE_TRAILING_SILENCE:
        if (is_speech) {
            /* Student resumed speaking — back to SPEECH */
            vad->state          = VAD_STATE_SPEECH;
            vad->silence_frames = 0;
        } else {
            vad->silence_frames++;
            if (vad->silence_frames >= TRAIL_FRAMES)
                vad->state = VAD_STATE_DONE;
        }
        break;

    case VAD_STATE_DONE:
    case VAD_STATE_TIMEOUT:
        break;
    }

    return vad->state;
}

void edgai_vad_reset(EdgaiVad *vad)
{
    if (!vad)
        return;
    memset(vad, 0, sizeof(*vad));
    /* state = VAD_STATE_SILENCE, counters = 0 */
}

/* ── Static helpers ─────────────────────────────────────────────────────── */

/* Compute RMS energy of a frame as an integer in [0, 32767].
 * Uses integer arithmetic only — no floating point, no math.h dependency. */
static uint32_t compute_rms(const int16_t *frame, size_t n_samples)
{
    if (n_samples == 0)
        return 0;

    uint64_t sum_sq = 0;
    for (size_t i = 0; i < n_samples; i++) {
        int32_t s = (int32_t)frame[i];
        sum_sq += (uint64_t)(s * s);
    }

    /* Integer square root via Newton's method */
    uint64_t mean_sq = sum_sq / n_samples;
    if (mean_sq == 0)
        return 0;

    uint64_t x = mean_sq;
    uint64_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + mean_sq / x) / 2;
    }
    return (uint32_t)x;
}
