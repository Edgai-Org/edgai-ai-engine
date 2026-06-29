/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 *
 * tools/chat.c — interactive CLI for libedgai.
 *
 * Usage:
 *   ./edgai-chat
 *   EDGAI_MODELS_DIR=~/.edgai/models ./edgai-chat
 *
 * Type 'quit' or 'exit' to end the session.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "edgai/edgai.h"

#define LINE_MAX_LEN 1024

static const char *state_name(EdgaiSequenceState s)
{
    switch (s) {
        case EDGAI_STATE_CONCEPT:  return "CONCEPT";
        case EDGAI_STATE_HOOK:     return "HOOK";
        case EDGAI_STATE_PREDICT:  return "PREDICT";
        case EDGAI_STATE_STEPS:    return "STEPS";
        case EDGAI_STATE_VERIFY:   return "VERIFY";
        case EDGAI_STATE_PRACTICE: return "PRACTICE";
        case EDGAI_STATE_CLOSE:    return "CLOSE";
        case EDGAI_STATE_DONE:     return "DONE";
        default:                   return "?";
    }
}

static const char *ram_tier_name(EdgaiRamTier t)
{
    switch (t) {
        case EDGAI_RAM_TIER_LOW:  return "LOW (<2 GB)";
        case EDGAI_RAM_TIER_MID:  return "MID (<4 GB)";
        case EDGAI_RAM_TIER_HIGH: return "HIGH (4 GB+)";
        default:                  return "?";
    }
}

static const char *age_mode_name(EdgaiAgeMode m)
{
    switch (m) {
        case EDGAI_MODE_PLAYGROUND:   return "PLAYGROUND (3-10)";
        case EDGAI_MODE_EXPLORER:     return "EXPLORER (10-15)";
        case EDGAI_MODE_LAUNCHPAD:    return "LAUNCHPAD (15-19)";
        case EDGAI_MODE_PROFESSIONAL: return "PROFESSIONAL (20+)";
        default:                      return "?";
    }
}

int main(void)
{
    printf("edgai-chat — EduOS interactive session\n");
    printf("---------------------------------------\n");

    EdgaiSession *session = edgai_init(NULL);
    if (!session) {
        fprintf(stderr, "ERROR: edgai_init() returned NULL\n");
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

        EdgaiResponse *resp = edgai_query(session, line);
        if (!resp) {
            fprintf(stderr, "ERROR: edgai_query() returned NULL\n");
            continue;
        }

        if (resp->error) {
            fprintf(stderr, "ERROR: %s\n", resp->error);
        } else {
            printf("[%s] %s\n", state_name(resp->sequence_state),
                   resp->text ? resp->text : "(no text)");
        }

        edgai_response_free(resp);
    }

    edgai_destroy(session);
    return 0;
}
