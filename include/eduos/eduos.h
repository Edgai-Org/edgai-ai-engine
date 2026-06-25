/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 */

#ifndef EDUOS_H
#define EDUOS_H

#include "eduos_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * eduos_init — initialise a new session
 *
 * Detects RAM tier from /proc/meminfo.
 * Reads age mode from profile_path (JSON).
 * Opens Unix socket connection to engine.py.
 * Returns NULL on failure.
 */
eduos_session_t *eduos_init(const char *profile_path);

/*
 * eduos_query — send a student message, get a teaching response
 *
 * Proxies over Unix socket to engine.py (Phase 3).
 * Will call llama.cpp directly in Phase 4.
 * Caller must free response with eduos_response_free().
 */
eduos_response_t *eduos_query(eduos_session_t *session, const char *text);

/*
 * eduos_transcribe — voice input (Phase 5)
 *
 * Passes raw audio to whisper.cpp, returns transcribed text.
 * Caller must free returned string.
 * STUB in Phase 3 — returns NULL.
 */
char *eduos_transcribe(eduos_session_t *session, const uint8_t *audio, size_t len);

/*
 * eduos_speak — voice output (Phase 5)
 *
 * Passes text to Piper TTS, returns raw audio bytes.
 * Caller must free returned buffer.
 * STUB in Phase 3 — returns NULL.
 */
uint8_t *eduos_speak(eduos_session_t *session, const char *text, size_t *out_len);

/*
 * eduos_set_mode — switch age mode mid-session
 *
 * Fires D-Bus signal to compositor (Phase 6).
 * Updates session age_mode immediately in all phases.
 */
void eduos_set_mode(eduos_session_t *session, eduos_age_mode_t mode);

/*
 * eduos_destroy — clean up session
 *
 * Closes socket, frees all session memory.
 */
void eduos_destroy(eduos_session_t *session);

/*
 * eduos_response_free — free a response returned by eduos_query
 */
void eduos_response_free(eduos_response_t *response);

#ifdef __cplusplus
}
#endif

#endif /* EDUOS_H */
