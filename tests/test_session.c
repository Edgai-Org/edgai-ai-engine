/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 *
 * test_session.c — Phase 4: test edgai_init() and edgai_destroy().
 * Verifies new session fields; does not require a model file.
 */

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "edgai/edgai.h"

int main(void)
{
    printf("test_session: testing edgai_init and edgai_destroy\n");

    EdgaiSession *session = edgai_init(NULL);
    assert(session != NULL);

    printf("  session_id:     %s\n", session->session_id);
    printf("  ram_tier:       %d\n", (int)session->ram_tier);
    printf("  age_mode:       %d\n", (int)session->age_mode);
    printf("  is_mobile:      %d\n", (int)session->is_mobile);
    printf("  current_state:  %d (expect CONCEPT=0)\n", (int)session->current_state);
    printf("  step_index:     %d\n", session->step_index);
    printf("  llm_model:      %s\n", session->llm_model ? "loaded" : "NULL (no model file — OK)");
    printf("  db:             %s\n", session->db       ? "open"   : "NULL (no demo DB in path — OK)");

    assert(session->session_id[0] != '\0');
    assert(session->current_state == EDGAI_STATE_CONCEPT);
    assert(session->step_index == 0);

    edgai_destroy(session);
    printf("test_session: PASS\n");
    return 0;
}
