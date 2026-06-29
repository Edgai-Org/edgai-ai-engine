/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 */

#ifndef EDGAI_H
#define EDGAI_H

#include "edgai_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * edgai_init — initialise a new session
 *
 * Detects RAM tier from /proc/meminfo.
 * Reads age mode from profile_path (JSON).
 * Opens Unix socket connection to engine.py.
 * Returns NULL on failure.
 */
EdgaiSession *edgai_init(const char *profile_path);

/*
 * edgai_query — send a student message, get a teaching response
 *
 * Proxies over Unix socket to engine.py (Phase 3).
 * Will call llama.cpp directly in Phase 4.
 * Caller must free response with edgai_response_free().
 */
EdgaiResponse *edgai_query(EdgaiSession *session, const char *text);

/*
 * edgai_transcribe — voice input (Phase 5)
 *
 * Passes raw audio to whisper.cpp, returns transcribed text.
 * Caller must free returned string.
 * STUB in Phase 3 — returns NULL.
 */
char *edgai_transcribe(EdgaiSession *session, const uint8_t *audio, size_t len);

/*
 * edgai_speak — voice output (Phase 5)
 *
 * Passes text to Piper TTS, returns raw audio bytes.
 * Caller must free returned buffer.
 * STUB in Phase 3 — returns NULL.
 */
uint8_t *edgai_speak(EdgaiSession *session, const char *text, size_t *out_len);

/*
 * edgai_voice_turn — one complete voice interaction cycle (Phase 5)
 *
 * Blocks for up to 10 seconds of mic capture, then generates and speaks
 * the response. Returns the EdgaiResponse* from the LLM, or NULL if no
 * speech was detected or voice is disabled.
 * Caller must free with edgai_response_free().
 * Only available when compiled with EDGAI_VOICE=ON.
 */
EdgaiResponse *edgai_voice_turn(EdgaiSession *session);

/*
 * edgai_set_mode — switch age mode mid-session
 *
 * Fires D-Bus signal to compositor (Phase 6).
 * Updates session age_mode immediately in all phases.
 */
void edgai_set_mode(EdgaiSession *session, EdgaiAgeMode mode);

/*
 * edgai_destroy — clean up session
 *
 * Closes socket, frees all session memory.
 */
void edgai_destroy(EdgaiSession *session);

/*
 * edgai_response_free — free a response returned by edgai_query
 */
void edgai_response_free(EdgaiResponse *response);

#ifdef __cplusplus
}
#endif

#endif /* EDGAI_H */
