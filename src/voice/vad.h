/* vad.h — Voice Activity Detection
 *
 * Energy-based VAD. Detects when a student has finished speaking vs.
 * pausing mid-sentence. The 800 ms trailing-silence window (EDGAI_VAD_TRAIL_MS)
 * gives students time to think without restarting the recognition cycle.
 *
 * Feed 20 ms audio frames via edgai_vad_process(). State advances from
 * SILENCE → SPEECH → TRAILING_SILENCE → DONE (or TIMEOUT on long utterances).
 */

#ifndef EDGAI_VAD_H
#define EDGAI_VAD_H

#include <stdint.h>
#include <stddef.h>

/* ── Tunable parameters ─────────────────────────────────────────────────── */
#define EDGAI_VAD_SPEECH_THRESHOLD  300   /* RMS energy units — speech vs silence */
#define EDGAI_VAD_TRAIL_MS          800   /* ms of silence after speech = done    */
#define EDGAI_VAD_MAX_MS          10000   /* ms max utterance before forced end   */
#define EDGAI_VAD_FRAME_MS           20   /* ms per input frame                   */

typedef enum {
    VAD_STATE_SILENCE          = 0,  /* no speech detected yet        */
    VAD_STATE_SPEECH           = 1,  /* student is speaking           */
    VAD_STATE_TRAILING_SILENCE = 2,  /* speech ended, waiting for gap */
    VAD_STATE_DONE             = 3,  /* utterance complete            */
    VAD_STATE_TIMEOUT          = 4   /* max duration exceeded         */
} EdgaiVadState;

typedef struct EdgaiVad EdgaiVad;

/* Allocate and initialise a new VAD instance.
 * Returns NULL on allocation failure. */
EdgaiVad *edgai_vad_create(void);

/* Free a VAD instance created with edgai_vad_create. Safe to call with NULL. */
void edgai_vad_destroy(EdgaiVad *vad);

/* Feed one frame of PCM audio (exactly EDGAI_VAD_FRAME_MS worth of samples).
 *   frame:         S16_LE samples
 *   frame_samples: number of samples in the frame
 * Returns the updated VAD state. Poll this after each frame; stop feeding
 * when state is VAD_STATE_DONE or VAD_STATE_TIMEOUT. */
EdgaiVadState edgai_vad_process(EdgaiVad *vad,
                                 const int16_t *frame,
                                 size_t frame_samples);

/* Reset VAD state to SILENCE without freeing the handle.
 * Use between utterances to reuse the same VAD instance. */
void edgai_vad_reset(EdgaiVad *vad);

#endif /* EDGAI_VAD_H */
