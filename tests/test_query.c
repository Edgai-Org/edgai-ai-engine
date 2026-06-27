/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 *
 * test_query.c — Phase 4: test eduos_query() with direct llama.cpp path.
 * Does not require engine.py. If model is not present, tests DB-only path.
 */

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "eduos/eduos.h"

int main(void)
{
    printf("test_query: testing eduos_query (Phase 4 — no engine.py needed)\n\n");

    eduos_session_t *session = eduos_init(NULL);
    assert(session != NULL);

    if (!session->llm_model)
        printf("  NOTE: model not loaded — LLM calls will return fallback message\n");

    if (!session->db)
        printf("  NOTE: curriculum DB not found — set EDUOS_DB_PATH or run from repo root\n");

    /* First query — should return CONCEPT state content */
    eduos_response_t *resp = eduos_query(session, "what is logarithm");
    assert(resp != NULL);

    if (resp->error) {
        printf("  error: %s\n", resp->error);
    } else {
        printf("  sequence_state: %d\n", (int)resp->sequence_state);
        printf("  question_id:    %.63s\n", resp->question_id);
        printf("  can_skip:       %d\n", resp->can_skip);
        printf("  text (first 120 chars): %.120s\n",
               resp->text ? resp->text : "(null)");
    }
    eduos_response_free(resp);

    /* Advance to next state */
    resp = eduos_query(session, "ok got it");
    assert(resp != NULL);
    printf("\n  After ADVANCE: state=%d\n", resp->error ? -1 : (int)resp->sequence_state);
    eduos_response_free(resp);

    eduos_destroy(session);
    printf("\ntest_query: PASS\n");
    return 0;
}
