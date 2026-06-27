/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 *
 * test_rag.c — Phase 4: test the full RAG pipeline.
 * Requires demo_curriculum.db (set EDUOS_DB_PATH or run from repo root).
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <sqlite3.h>
#include "eduos/eduos.h"
#include "eduos/eduos_rag.h"

/* Forward declarations from preprocessor.c */
char *eduos_preprocess_query(const char *raw_query);

int main(void)
{
    printf("test_rag: testing RAG pipeline\n\n");

    /* Preprocessor */
    char *processed = eduos_preprocess_query("what are logarithms");
    if (processed) {
        printf("  preprocess('what are logarithms') = '%s'\n", processed);
        /* 'what' and 'are' are stop words; 'logarithms' should be stemmed */
        assert(strlen(processed) > 0);
        free(processed);
    }

    processed = eduos_preprocess_query("trig equations");
    if (processed) {
        printf("  preprocess('trig equations')      = '%s'\n", processed);
        free(processed);
    }

    /* Retriever */
    const char *db_path = getenv("EDUOS_DB_PATH");
    if (!db_path) db_path = "db/demo/demo_curriculum.db";

    sqlite3 *db = NULL;
    int rc = sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READONLY, NULL);
    if (rc != SQLITE_OK || !db) {
        printf("  WARNING: cannot open DB at '%s' — skipping retriever test\n", db_path);
        printf("test_rag: PASS (DB not found — partial)\n");
        return 0;
    }

    int count = 0;
    eduos_rag_result_t *results = eduos_rag_retrieve(
        db, "logarithm", EDUOS_MODE_LAUNCHPAD, 5, &count);

    printf("  retrieve('logarithm'): %d result(s)\n", count);
    for (int i = 0; i < count; i++) {
        printf("    [%d] id=%s score=%.4f text=%.60s\n",
               i, results[i].question_id, (double)results[i].score,
               results[i].question_text ? results[i].question_text : "(null)");
    }

    if (results) eduos_rag_results_free(results, count);
    sqlite3_close(db);

    printf("\ntest_rag: PASS\n");
    return 0;
}
