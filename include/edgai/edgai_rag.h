/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 */

#ifndef EDGAI_RAG_H
#define EDGAI_RAG_H

#include "edgai_types.h"

struct sqlite3;

typedef struct {
    char   question_id[64];
    char  *question_text;   /* heap-allocated */
    char  *hook;            /* heap-allocated */
    char  *predict;         /* heap-allocated */
    char  *steps_json;      /* heap-allocated — raw JSON array string */
    char  *how_to_ace;      /* heap-allocated */
    float  score;
} EdgaiRagResult;

/*
 * Search the curriculum DB via FTS5.
 * Preprocesses raw_query before searching.
 * Returns array of top_k results; *out_count set to actual count.
 * Caller frees with edgai_rag_results_free().
 */
EdgaiRagResult *edgai_rag_retrieve(
    struct sqlite3   *db,
    const char       *raw_query,
    EdgaiAgeMode  mode,
    int               top_k,
    int              *out_count
);

void edgai_rag_results_free(EdgaiRagResult *results, int count);

#endif /* EDGAI_RAG_H */
