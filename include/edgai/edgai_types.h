/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 */

#ifndef EDGAI_TYPES_H
#define EDGAI_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum {
    EDGAI_MODE_PLAYGROUND   = 0,  /* ages 3–10  */
    EDGAI_MODE_EXPLORER     = 1,  /* ages 10–15 */
    EDGAI_MODE_LAUNCHPAD    = 2,  /* ages 15–19 */
    EDGAI_MODE_PROFESSIONAL = 3   /* ages 20+   */
} EdgaiAgeMode;

typedef enum {
    EDGAI_RAM_TIER_LOW  = 0,  /* <2 GB — Qwen 1.5B    */
    EDGAI_RAM_TIER_MID  = 1,  /* <4 GB — Phi-3 Mini   */
    EDGAI_RAM_TIER_HIGH = 2   /* 4 GB+ — Llama 3 8B   */
} EdgaiRamTier;

typedef enum {
    EDGAI_STATE_CONCEPT  = 0,
    EDGAI_STATE_HOOK     = 1,
    EDGAI_STATE_PREDICT  = 2,
    EDGAI_STATE_STEPS    = 3,
    EDGAI_STATE_VERIFY   = 4,
    EDGAI_STATE_PRACTICE = 5,
    EDGAI_STATE_CLOSE    = 6,
    EDGAI_STATE_DONE     = 7
} EdgaiSequenceState;

typedef enum {
    EDGAI_INTENT_ADVANCE    = 0,
    EDGAI_INTENT_RE_EXPLAIN = 1,
    EDGAI_INTENT_SKIP       = 2,
    EDGAI_INTENT_DECLINE    = 3,
    EDGAI_INTENT_UNKNOWN    = 4,
} EdgaiIntent;

/* ── Return codes ────────────────────────────────────────────────────────── */

#define EDGAI_OK                       0
#define EDGAI_ERR_AUDIO_BUSY          -1   /* ALSA device held by another process */
#define EDGAI_ERR_VOICE_UNAVAILABLE   -2   /* model missing or voice disabled     */
#define EDGAI_ERR_OOM                 -3   /* out of memory                       */
#define EDGAI_ERR_INTERNAL            -4   /* unexpected internal failure         */

/* Phase 5 — voice mode state machine */
typedef enum {
    EDGAI_VOICE_STATE_IDLE        = 0,
    EDGAI_VOICE_STATE_LISTENING   = 1,  /* Vosk loaded, capturing */
    EDGAI_VOICE_STATE_TRANSCRIBED = 2,  /* Vosk freed, text ready */
    EDGAI_VOICE_STATE_GENERATING  = 3,  /* LLM loaded, generating */
    EDGAI_VOICE_STATE_SPEAKING    = 4,  /* LLM freed, Piper+ALSA active */
} EdgaiVoiceState;

/* Forward-declare opaque llama.cpp types — defined in llama.h at compile time */
struct llama_model;
struct llama_context;
struct llama_sampler;

/* Forward-declare opaque SQLite type — defined in sqlite3.h at compile time */
struct sqlite3;

typedef struct {
    char              session_id[64];
    EdgaiAgeMode  age_mode;
    EdgaiRamTier  ram_tier;

    /* Phase 3 socket bridge — only compiled when EDGAI_SOCKET_FALLBACK is set */
    int               socket_fd;

    /* Phase 4 — llama.cpp inference */
    struct llama_model   *llm_model;
    struct llama_context *llm_ctx;
    struct llama_sampler *llm_sampler;
    bool                  is_mobile;

    /* Phase 4 — curriculum DB (opened once per session) */
    struct sqlite3       *db;

    /* Phase 4 — teaching state machine */
    EdgaiSequenceState current_state;
    char                   current_question_id[64];
    int                    step_index;
    int                    total_steps;

    /* Phase 5 — voice pipeline (opaque pointers; defined in voice/*.c) */
    void          *vosk_model;    /* VoskModel*   — NULL when voice disabled  */
    void          *vosk_rec;      /* VoskRecognizer* — per-turn lifecycle      */
    void          *piper_ctx;     /* PiperContext* — NULL when voice disabled  */
    EdgaiVoiceState voice_state;
    int             voice_enabled; /* 0 if EDGAI_DISABLE_VOICE=1 or no model   */
    int             speak_interrupt; /* atomic flag: 1 = cancel active playback */
} EdgaiSession;

typedef struct {
    char                   *text;
    EdgaiSequenceState  sequence_state;
    int                     step_index;
    char                    question_id[64];
    int                     can_skip;
    char                   *error;
} EdgaiResponse;

#endif /* EDGAI_TYPES_H */
