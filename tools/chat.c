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
 * Special commands:
 *   /voice     — toggle voice mode (listen + speak a full turn)
 *   /voiceon   — enable voice for all subsequent turns
 *   /voiceoff  — disable voice, revert to text input
 *   quit / exit — end the session
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

static void print_response(EdgaiResponse *resp)
{
    if (!resp) {
        fprintf(stderr, "ERROR: NULL response\n");
        return;
    }
    if (resp->error)
        fprintf(stderr, "ERROR: %s\n", resp->error);
    else
        printf("[%s] %s\n", state_name(resp->sequence_state),
               resp->text ? resp->text : "(no text)");
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
    printf("model      : %s\n",
           session->llm_model ? "loaded" : "not loaded (LLM disabled)");
    printf("voice      : %s\n",
           session->voice_enabled ? "enabled" : "disabled (model not found)");
    printf("---------------------------------------\n");
    printf("Type 'quit' or 'exit' to end.\n");
    printf("Type '/voice' for one voice turn, '/voiceon' to stay in voice mode.\n\n");

    char line[LINE_MAX_LEN];
    int  voice_mode = 0; /* 0 = text loop; 1 = every turn is a voice turn */

    for (;;) {
        if (voice_mode) {
            printf("[voice] Listening... (speak now)\n");
            fflush(stdout);

            EdgaiResponse *resp = edgai_voice_turn(session);
            if (!resp) {
                printf("[voice] No speech detected or voice unavailable.\n");
            } else {
                print_response(resp);
                edgai_response_free(resp);
            }
            /* In voice_mode, loop back and listen again */
            continue;
        }

        /* ── Text mode prompt ──────────────────────────────────────── */
        printf("> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }

        /* Strip trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        if (len == 0) continue;

        if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0)
            break;

        /* ── Voice commands ─────────────────────────────────────────── */
        if (strcmp(line, "/voice") == 0) {
            if (!session->voice_enabled) {
                printf("[voice] Voice unavailable — "
                       "install Vosk and Piper models first.\n");
                continue;
            }
            printf("[voice] Listening... (speak now)\n");
            fflush(stdout);

            EdgaiResponse *resp = edgai_voice_turn(session);
            if (!resp)
                printf("[voice] No speech detected.\n");
            else {
                print_response(resp);
                edgai_response_free(resp);
            }
            continue;
        }

        if (strcmp(line, "/voiceon") == 0) {
            if (!session->voice_enabled) {
                printf("[voice] Voice unavailable — "
                       "install Vosk and Piper models first.\n");
                continue;
            }
            voice_mode = 1;
            printf("[voice] Voice mode ON — speak each turn. "
                   "Type /voiceoff in text mode to exit.\n");
            continue;
        }

        if (strcmp(line, "/voiceoff") == 0) {
            voice_mode = 0;
            printf("[voice] Voice mode OFF — back to text input.\n");
            continue;
        }

        /* ── Normal text query ──────────────────────────────────────── */
        EdgaiResponse *resp = edgai_query(session, line);
        print_response(resp);
        edgai_response_free(resp);
    }

    edgai_destroy(session);
    return 0;
}
