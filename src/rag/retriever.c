/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 *
 * retriever.c — FTS5 full-text search against the curriculum DB via sqlite3.h.
 * No Python, no subprocess, no external search library.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sqlite3.h>
#include "edgai/edgai_rag.h"

/* Forward declaration from preprocessor.c */
char *edgai_preprocess_query(const char *raw_query);

/* Forward declaration from ranker.c */
void edgai_rag_rank(EdgaiRagResult *results, int count);

static char *dup_column(sqlite3_stmt *stmt, int col)
{
    const unsigned char *val = sqlite3_column_text(stmt, col);
    if (!val) return NULL;
    return strdup((const char *)val);
}

/*
 * FTS5 query — bm25() returns negative values (lower = more relevant).
 * ORDER BY score gives most relevant first in SQLite FTS5.
 */
static const char *FTS_SQL =
    "SELECT q.id, q.question_text, q.hook, q.predict, q.steps, q.how_to_ace, "
    "       bm25(questions_fts) AS score "
    "FROM questions_fts "
    "JOIN questions q ON questions_fts.rowid = q.rowid "
    "WHERE questions_fts MATCH ? "
    "ORDER BY score "
    "LIMIT ?;";

EdgaiRagResult *edgai_rag_retrieve(
    struct sqlite3   *db,
    const char       *raw_query,
    EdgaiAgeMode  mode,
    int               top_k,
    int              *out_count)
{
    (void)mode; /* age-mode adaptation happens in formatter.c */

    *out_count = 0;
    if (!db || !raw_query || top_k <= 0)
        return NULL;

    char *processed = edgai_preprocess_query(raw_query);
    if (!processed || processed[0] == '\0') {
        free(processed);
        return NULL;
    }

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, FTS_SQL, -1, &stmt, NULL) != SQLITE_OK) {
        free(processed);
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, processed, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt,  2, top_k);
    free(processed);

    /* Collect results into a dynamically grown array */
    int capacity = top_k;
    EdgaiRagResult *results = calloc((size_t)capacity, sizeof(EdgaiRagResult));
    if (!results) {
        sqlite3_finalize(stmt);
        return NULL;
    }

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < top_k) {
        EdgaiRagResult *r = &results[count];

        const unsigned char *id = sqlite3_column_text(stmt, 0);
        if (id)
            strncpy(r->question_id, (const char *)id, sizeof(r->question_id) - 1);

        r->question_text = dup_column(stmt, 1);
        r->hook          = dup_column(stmt, 2);
        r->predict       = dup_column(stmt, 3);
        r->steps_json    = dup_column(stmt, 4);
        r->how_to_ace    = dup_column(stmt, 5);
        r->score         = (float)sqlite3_column_double(stmt, 6);
        count++;
    }
    sqlite3_finalize(stmt);

    if (count == 0) {
        free(results);
        return NULL;
    }

    /* Re-rank: BM25 + length weighting */
    edgai_rag_rank(results, count);

    *out_count = count;
    return results;
}

void edgai_rag_results_free(EdgaiRagResult *results, int count)
{
    if (!results) return;
    for (int i = 0; i < count; i++) {
        free(results[i].question_text);
        free(results[i].hook);
        free(results[i].predict);
        free(results[i].steps_json);
        free(results[i].how_to_ace);
    }
    free(results);
}
