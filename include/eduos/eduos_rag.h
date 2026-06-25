/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 */

#ifndef EDUOS_RAG_H
#define EDUOS_RAG_H

#include "eduos_types.h"

/*
 * Internal interface — not exposed to consumers of libeduos.
 * Phase 4 will implement these in src/rag/.
 * Phase 3 stubs return NULL / 0.
 */

typedef struct {
    char  *question_id;
    char  *question_text;
    char  *hook;
    char  *predict;
    char  *steps_json;
    char  *how_to_ace;
    float  score;
} eduos_rag_result_t;

eduos_rag_result_t *eduos_rag_retrieve(const char *query, eduos_age_mode_t mode, int top_k);
void                eduos_rag_result_free(eduos_rag_result_t *result);

#endif /* EDUOS_RAG_H */
