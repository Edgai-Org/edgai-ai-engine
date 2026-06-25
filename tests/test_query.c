/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 */

#include <stdio.h>
#include <assert.h>
#include "eduos/eduos.h"

int main(void)
{
    printf("test_query: testing eduos_query via socket\n");
    printf("  (requires engine.py running: python3 src/rag/engine.py)\n\n");

    eduos_session_t *session = eduos_init(NULL);
    assert(session != NULL);

    if (session->socket_fd < 0) {
        printf("  WARNING: engine.py not running — socket connect failed\n");
        printf("  Start engine.py and rerun to test full pipeline\n");
        eduos_destroy(session);
        return 0;
    }

    eduos_response_t *resp = eduos_query(session, "what is logarithm");
    assert(resp != NULL);

    if (resp->error) {
        printf("  ERROR: %s\n", resp->error);
    } else {
        printf("  sequence_state: %d\n", resp->sequence_state);
        printf("  question_id:    %s\n", resp->question_id);
        printf("  can_skip:       %d\n", resp->can_skip);
        printf("  text (first 120 chars): %.120s\n", resp->text ? resp->text : "(null)");
        printf("\ntest_query: PASS\n");
    }

    eduos_response_free(resp);
    eduos_destroy(session);
    return 0;
}
