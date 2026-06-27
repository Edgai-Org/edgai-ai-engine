/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 *
 * test_retriever.c — Phase 4: FTS5 retrieval test.
 * Uses demo_curriculum.db (EDUOS_DB_PATH env or repo-relative path).
 * Asserts: at least 1 result returned; question_id starts with "demo-".
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <sqlite3.h>
#include "eduos/eduos.h"
#include "eduos/eduos_rag.h"

int main(void)
{
    printf("test_retriever: FTS5 retrieval from demo_curriculum.db\n\n");

    const char *db_path = getenv("EDUOS_DB_PATH");
    if (!db_path) db_path = "db/demo/demo_curriculum.db";

    sqlite3 *db = NULL;
    int rc = sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READONLY, NULL);
    if (rc != SQLITE_OK || !db) {
        printf("  WARNING: cannot open '%s'\n", db_path);
        printf("  Run from repo root or set EDUOS_DB_PATH.\n");
        printf("test_retriever: SKIPPED\n");
        return 0;
    }

    /* Test 1: query 'logarithm' */
    int count = 0;
    eduos_rag_result_t *results = eduos_rag_retrieve(
        db, "logarithm", EDUOS_MODE_LAUNCHPAD, 5, &count);

    printf("  query='logarithm': %d result(s)\n", count);
    assert(count >= 1);
    assert(results != NULL);
    assert(strncmp(results[0].question_id, "demo-", 5) == 0);

    printf("  top result: id=%s\n", results[0].question_id);
    printf("              text=%.80s\n",
           results[0].question_text ? results[0].question_text : "(null)");
    printf("              hook=%s\n",
           results[0].hook ? "present" : "(null)");
    printf("              steps=%s\n",
           results[0].steps_json ? "present" : "(null)");

    eduos_rag_results_free(results, count);

    /* Test 2: query 'quadratic' */
    count = 0;
    results = eduos_rag_retrieve(db, "quadratic", EDUOS_MODE_EXPLORER, 3, &count);
    printf("\n  query='quadratic': %d result(s)\n", count);
    if (results) eduos_rag_results_free(results, count);

    sqlite3_close(db);
    printf("\ntest_retriever: PASS\n");
    return 0;
}
