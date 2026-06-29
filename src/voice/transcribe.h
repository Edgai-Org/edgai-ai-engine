/* transcribe.h — Vosk STT + ALSA microphone capture interface
 *
 * Manages the full STT pipeline: ALSA mic capture at 16 kHz → VAD gate →
 * Vosk recognition → NUL-terminated transcript string.
 *
 * Memory tiers:
 *   2 GB — Vosk model loaded/freed per call; recognizer rebuilt each time.
 *   4 GB+ — model stays resident in session->vosk_model; only the recognizer
 *             is created and freed per call (lightweight per Vosk docs).
 *
 * Thread safety: not reentrant. Only one edgai_transcribe_listen() may run
 * per session at a time.
 */

#ifndef EDGAI_TRANSCRIBE_H
#define EDGAI_TRANSCRIBE_H

#include "edgai/edgai.h"

/* Initialize Vosk STT.
 * On 4 GB+ tier: loads the model and stores it in session->vosk_model.
 * On 2 GB tier:  does nothing (model loaded per-call in edgai_transcribe_listen).
 * Model resolved via EDGAI_VOSK_MODEL env var or
 *   ~/.edgai/models/vosk/vosk-model-small-en-us-0.15/.
 * Returns EDGAI_OK on success.
 * Returns EDGAI_ERR_VOICE_UNAVAILABLE if model directory is not found —
 * engine continues in text-only mode, no crash. */
int edgai_transcribe_init(EdgaiSession *session);

/* Record from the microphone until silence (800 ms) or timeout (10 s),
 * feed audio to Vosk, and return the transcript.
 *
 * The returned string is heap-allocated and owned by the caller; free() it
 * when done. Returns NULL on error (no mic, OOM, or no speech detected).
 *
 * Sets session->voice_state to EDGAI_VOICE_STATE_LISTENING while capturing,
 * then EDGAI_VOICE_STATE_TRANSCRIBED on success. */
char *edgai_transcribe_listen(EdgaiSession *session);

/* Free Vosk model loaded by edgai_transcribe_init.
 * Called during edgai_destroy(). Safe to call with NULL. */
void edgai_transcribe_destroy(EdgaiSession *session);

#endif /* EDGAI_TRANSCRIBE_H */
