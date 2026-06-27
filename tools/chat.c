/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 *
 * tools/chat.c — interactive CLI for libeduos.
 *
 * Usage:
 *   ./eduos-chat
 *   EDUOS_MODELS_DIR=~/.eduos/models ./eduos-chat
 *
 * Type 'quit' or 'exit' to end the session.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eduos/eduos.h"

#define LINE_MAX_LEN 1024

static const char *state_name(eduos_sequence_state_t s)
{
    switch (s) {
        case EDUOS_STATE_CONCEPT:  return "CONCEPT";
        case EDUOS_STATE_HOOK:     return "HOOK";
        case EDUOS_STATE_PREDICT:  return "PREDICT";
        case EDUOS_STATE_STEPS:    return "STEPS";
        case EDUOS_STATE_VERIFY:   return "VERIFY";
        case EDUOS_STATE_PRACTICE: return "PRACTICE";
        case EDUOS_STATE_CLOSE:    return "CLOSE";
        case EDUOS_STATE_DONE:     return "DONE";
        default:                   return "?";
    }
}

static const char *ram_tier_name(eduos_ram_tier_t t)
{
    switch (t) {
        case EDUOS_RAM_TIER_LOW:  return "LOW (<2 GB)";
        case EDUOS_RAM_TIER_MID:  return "MID (<4 GB)";
        case EDUOS_RAM_TIER_HIGH: return "HIGH (4 GB+)";
        default:                  return "?";
    }
}

static const char *age_mode_name(eduos_age_mode_t m)
{
    switch (m) {
        case EDUOS_MODE_PLAYGROUND:   return "PLAYGROUND (3-10)";
        case EDUOS_MODE_EXPLORER:     return "EXPLORER (10-15)";
        case EDUOS_MODE_LAUNCHPAD:    return "LAUNCHPAD (15-19)";
        case EDUOS_MODE_PROFESSIONAL: return "PROFESSIONAL (20+)";
        default:                      return "?";
    }
}

int main(void)
{
    printf("eduos-chat — EduOS interactive session\n");
    printf("---------------------------------------\n");

    eduos_session_t *session = eduos_init(NULL);
    if (!session) {
        fprintf(stderr, "ERROR: eduos_init() returned NULL\n");
        return 1;
    }

    printf("session_id : %s\n", session->session_id);
    printf("ram_tier   : %d (%s)\n", (int)session->ram_tier,
           ram_tier_name(session->ram_tier));
    printf("age_mode   : %d (%s)\n", (int)session->age_mode,
           age_mode_name(session->age_mode));
    printf("model      : %s\n", session->llm_model ? "loaded" : "not loaded (LLM disabled)");
    printf("---------------------------------------\n");
    printf("Type 'quit' or 'exit' to end.\n\n");

    char line[LINE_MAX_LEN];

    for (;;) {
        printf("> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            /* EOF — Ctrl-D */
            printf("\n");
            break;
        }

        /* Strip trailing newline (and any \r on Windows) */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        if (len == 0) continue;

        if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0)
            break;

        eduos_response_t *resp = eduos_query(session, line);
        if (!resp) {
            fprintf(stderr, "ERROR: eduos_query() returned NULL\n");
            continue;
        }

        if (resp->error) {
            fprintf(stderr, "ERROR: %s\n", resp->error);
        } else {
            printf("[%s] %s\n", state_name(resp->sequence_state),
                   resp->text ? resp->text : "(no text)");
        }

        eduos_response_free(resp);
    }

    eduos_destroy(session);
    return 0;
}
