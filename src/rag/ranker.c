/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 *
 * ranker.c — re-rank FTS5 results by combining BM25 score and question
 * text length. No ML, no external library — simple weighted sum, in-place sort.
 */

#include <string.h>
#include <math.h>

#include "edgai/edgai_rag.h"

/*
 * Compute a combined rank score.
 * BM25 from SQLite FTS5 returns negative values (lower = more relevant).
 * We negate so higher combined score = better.
 * Length penalty: shorter question_text = more specific = bonus.
 */
static float combined_score(const EdgaiRagResult *r)
{
    float bm25 = -r->score; /* negate: higher now means more relevant */
    float len_bonus = 0.0f;
    if (r->question_text)
        len_bonus = 1.0f / (1.0f + (float)strlen(r->question_text) / 80.0f);
    return 0.8f * bm25 + 0.2f * len_bonus;
}

/* Insertion sort — result counts are small (top_k ≤ 10 typically) */
void edgai_rag_rank(EdgaiRagResult *results, int count)
{
    if (!results || count <= 1)
        return;

    for (int i = 1; i < count; i++) {
        EdgaiRagResult key = results[i];
        float key_score = combined_score(&key);
        int j = i - 1;
        while (j >= 0 && combined_score(&results[j]) < key_score) {
            results[j + 1] = results[j];
            j--;
        }
        results[j + 1] = key;
    }
}
