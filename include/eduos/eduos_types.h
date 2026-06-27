/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 */

#ifndef EDUOS_TYPES_H
#define EDUOS_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum {
    EDUOS_MODE_PLAYGROUND   = 0,  /* ages 3–10  */
    EDUOS_MODE_EXPLORER     = 1,  /* ages 10–15 */
    EDUOS_MODE_LAUNCHPAD    = 2,  /* ages 15–19 */
    EDUOS_MODE_PROFESSIONAL = 3   /* ages 20+   */
} eduos_age_mode_t;

typedef enum {
    EDUOS_RAM_TIER_LOW  = 0,  /* <2 GB — Qwen 1.5B    */
    EDUOS_RAM_TIER_MID  = 1,  /* <4 GB — Phi-3 Mini   */
    EDUOS_RAM_TIER_HIGH = 2   /* 4 GB+ — Llama 3 8B   */
} eduos_ram_tier_t;

typedef enum {
    EDUOS_STATE_CONCEPT  = 0,
    EDUOS_STATE_HOOK     = 1,
    EDUOS_STATE_PREDICT  = 2,
    EDUOS_STATE_STEPS    = 3,
    EDUOS_STATE_VERIFY   = 4,
    EDUOS_STATE_PRACTICE = 5,
    EDUOS_STATE_CLOSE    = 6,
    EDUOS_STATE_DONE     = 7
} eduos_sequence_state_t;

typedef enum {
    EDUOS_INTENT_ADVANCE    = 0,
    EDUOS_INTENT_RE_EXPLAIN = 1,
    EDUOS_INTENT_SKIP       = 2,
    EDUOS_INTENT_DECLINE    = 3,
    EDUOS_INTENT_UNKNOWN    = 4,
} eduos_intent_t;

/* Forward-declare opaque llama.cpp types — defined in llama.h at compile time */
struct llama_model;
struct llama_context;
struct llama_sampler;

/* Forward-declare opaque SQLite type — defined in sqlite3.h at compile time */
struct sqlite3;

typedef struct {
    char              session_id[64];
    eduos_age_mode_t  age_mode;
    eduos_ram_tier_t  ram_tier;

    /* Phase 3 socket bridge — only compiled when EDUOS_SOCKET_FALLBACK is set */
    int               socket_fd;

    /* Phase 4 — llama.cpp inference */
    struct llama_model   *llm_model;
    struct llama_context *llm_ctx;
    struct llama_sampler *llm_sampler;
    bool                  is_mobile;

    /* Phase 4 — curriculum DB (opened once per session) */
    struct sqlite3       *db;

    /* Phase 4 — teaching state machine */
    eduos_sequence_state_t current_state;
    char                   current_question_id[64];
    int                    step_index;
    int                    total_steps;
} eduos_session_t;

typedef struct {
    char                   *text;
    eduos_sequence_state_t  sequence_state;
    int                     step_index;
    char                    question_id[64];
    int                     can_skip;
    char                   *error;
} eduos_response_t;

#endif /* EDUOS_TYPES_H */
