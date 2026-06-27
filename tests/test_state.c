/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 *
 * test_state.c — Phase 4: state machine transition test.
 * Creates a fake session, advances ADVANCE through all states, asserts DONE.
 */

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "eduos/eduos.h"

/* Forward declaration from state.c */
eduos_sequence_state_t eduos_state_next(eduos_session_t *session,
                                         eduos_intent_t intent);

int main(void)
{
    printf("test_state: teaching sequence state machine transitions\n\n");

    /* Build a minimal fake session — no LLM, no DB needed */
    eduos_session_t *session = calloc(1, sizeof(eduos_session_t));
    assert(session != NULL);
    session->current_state = EDUOS_STATE_CONCEPT;
    session->step_index    = 0;
    session->total_steps   = 3; /* pretend 3 steps so STEPS→VERIFY fires */

    /* Expected state after each ADVANCE call */
    static const struct {
        eduos_intent_t          intent;
        eduos_sequence_state_t  expected;
        const char             *label;
    } TRANSITIONS[] = {
        { EDUOS_INTENT_ADVANCE, EDUOS_STATE_HOOK,     "CONCEPT  + ADVANCE → HOOK"     },
        { EDUOS_INTENT_ADVANCE, EDUOS_STATE_PREDICT,  "HOOK     + ADVANCE → PREDICT"  },
        { EDUOS_INTENT_ADVANCE, EDUOS_STATE_STEPS,    "PREDICT  + ADVANCE → STEPS"    },
        { EDUOS_INTENT_ADVANCE, EDUOS_STATE_STEPS,    "STEPS+0  + ADVANCE → STEPS+1"  },
        { EDUOS_INTENT_ADVANCE, EDUOS_STATE_STEPS,    "STEPS+1  + ADVANCE → STEPS+2"  },
        { EDUOS_INTENT_ADVANCE, EDUOS_STATE_VERIFY,   "STEPS+2  + ADVANCE → VERIFY"   },
        { EDUOS_INTENT_ADVANCE, EDUOS_STATE_PRACTICE, "VERIFY   + ADVANCE → PRACTICE" },
        { EDUOS_INTENT_ADVANCE, EDUOS_STATE_CLOSE,    "PRACTICE + ADVANCE → CLOSE"    },
        { EDUOS_INTENT_ADVANCE, EDUOS_STATE_DONE,     "CLOSE    + ADVANCE → DONE"     },
    };

    int n = (int)(sizeof(TRANSITIONS) / sizeof(TRANSITIONS[0]));
    for (int i = 0; i < n; i++) {
        eduos_sequence_state_t got = eduos_state_next(session, TRANSITIONS[i].intent);
        int ok = (got == TRANSITIONS[i].expected);
        printf("  %-40s state=%d %s\n",
               TRANSITIONS[i].label, (int)got, ok ? "OK" : "FAIL");
        assert(ok);
    }

    assert(session->current_state == EDUOS_STATE_DONE);
    printf("\n  Final state: DONE — correct.\n");

    /* Test DECLINE from any state → CLOSE */
    session->current_state = EDUOS_STATE_PREDICT;
    assert(eduos_state_next(session, EDUOS_INTENT_DECLINE) == EDUOS_STATE_CLOSE);
    printf("  DECLINE from PREDICT → CLOSE: OK\n");

    /* Test UNKNOWN stays in current state */
    session->current_state = EDUOS_STATE_HOOK;
    assert(eduos_state_next(session, EDUOS_INTENT_UNKNOWN) == EDUOS_STATE_HOOK);
    printf("  UNKNOWN from HOOK → HOOK (no change): OK\n");

    /* Test SKIP skips to next logical state */
    session->current_state = EDUOS_STATE_HOOK;
    assert(eduos_state_next(session, EDUOS_INTENT_SKIP) == EDUOS_STATE_STEPS);
    printf("  SKIP from HOOK → STEPS: OK\n");

    free(session);
    printf("\ntest_state: PASS\n");
    return 0;
}
