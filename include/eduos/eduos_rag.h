/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 */

#ifndef EDUOS_RAG_H
#define EDUOS_RAG_H

#include "eduos_types.h"

struct sqlite3;

typedef struct {
    char   question_id[64];
    char  *question_text;   /* heap-allocated */
    char  *hook;            /* heap-allocated */
    char  *predict;         /* heap-allocated */
    char  *steps_json;      /* heap-allocated — raw JSON array string */
    char  *how_to_ace;      /* heap-allocated */
    float  score;
} eduos_rag_result_t;

/*
 * Search the curriculum DB via FTS5.
 * Preprocesses raw_query before searching.
 * Returns array of top_k results; *out_count set to actual count.
 * Caller frees with eduos_rag_results_free().
 */
eduos_rag_result_t *eduos_rag_retrieve(
    struct sqlite3   *db,
    const char       *raw_query,
    eduos_age_mode_t  mode,
    int               top_k,
    int              *out_count
);

void eduos_rag_results_free(eduos_rag_result_t *results, int count);

#endif /* EDUOS_RAG_H */
