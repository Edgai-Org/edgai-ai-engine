/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 */

#ifndef EDUOS_TYPES_H
#define EDUOS_TYPES_H

#include <stdint.h>
#include <stddef.h>

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

typedef struct {
    char              session_id[64];
    eduos_age_mode_t  age_mode;
    eduos_ram_tier_t  ram_tier;
    int               socket_fd;   /* Unix socket to engine.py — Phase 3 only */
    void             *llm_ctx;     /* llama.cpp context — Phase 4, NULL for now */
} eduos_session_t;

typedef struct {
    char                   *text;           /* heap-allocated, caller frees */
    eduos_sequence_state_t  sequence_state;
    int                     step_index;
    char                    question_id[64];
    int                     can_skip;
    char                   *error;          /* NULL on success, heap-allocated on error */
} eduos_response_t;

#endif /* EDUOS_TYPES_H */
