/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 *
 * state.c — EduOS teaching sequence state machine.
 * Ports the state logic from engine.py to C.
 *
 * States: CONCEPT → HOOK → PREDICT → STEPS → VERIFY → PRACTICE → CLOSE → DONE
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sqlite3.h>
#include "edgai/edgai.h"
#include "edgai/edgai_rag.h"

/* Forward declaration from preprocessor.c */
char *edgai_preprocess_query(const char *raw_query);

/* ── State machine transitions ──────────────────────────────────────────── */

/*
 * Advance the state machine based on intent.
 * Mutates session->step_index when appropriate.
 * Returns the new state (also stored in session->current_state).
 */
EdgaiSequenceState edgai_state_next(EdgaiSession *session,
                                         EdgaiIntent intent)
{
    EdgaiSequenceState cur = session->current_state;

    if (intent == EDGAI_INTENT_DECLINE) {
        session->current_state = EDGAI_STATE_CLOSE;
        return EDGAI_STATE_CLOSE;
    }

    if (intent == EDGAI_INTENT_UNKNOWN) {
        /* Stay in current state — ask for clarification */
        return cur;
    }

    if (intent == EDGAI_INTENT_SKIP) {
        /* Skip to next logical state */
        switch (cur) {
        case EDGAI_STATE_CONCEPT:  session->current_state = EDGAI_STATE_STEPS;    break;
        case EDGAI_STATE_HOOK:     session->current_state = EDGAI_STATE_STEPS;    break;
        case EDGAI_STATE_PREDICT:  session->current_state = EDGAI_STATE_STEPS;    break;
        case EDGAI_STATE_STEPS:    session->current_state = EDGAI_STATE_VERIFY;   break;
        case EDGAI_STATE_VERIFY:   session->current_state = EDGAI_STATE_PRACTICE; break;
        case EDGAI_STATE_PRACTICE: session->current_state = EDGAI_STATE_CLOSE;    break;
        case EDGAI_STATE_CLOSE:    session->current_state = EDGAI_STATE_DONE;     break;
        default:                   break;
        }
        return session->current_state;
    }

    /* ADVANCE and RE_EXPLAIN */
    switch (cur) {
    case EDGAI_STATE_CONCEPT:
        session->current_state = EDGAI_STATE_HOOK;
        break;

    case EDGAI_STATE_HOOK:
        session->current_state = EDGAI_STATE_PREDICT;
        break;

    case EDGAI_STATE_PREDICT:
        session->current_state = EDGAI_STATE_STEPS;
        session->step_index    = 0;
        break;

    case EDGAI_STATE_STEPS:
        if (intent == EDGAI_INTENT_RE_EXPLAIN) {
            /* Stay in STEPS — caller will invoke LLM rephrase */
            break;
        }
        session->step_index++;
        if (session->step_index >= session->total_steps) {
            session->current_state = EDGAI_STATE_VERIFY;
        }
        /* else: stay STEPS, next step_index will be served */
        break;

    case EDGAI_STATE_VERIFY:
        session->current_state = EDGAI_STATE_PRACTICE;
        break;

    case EDGAI_STATE_PRACTICE:
        session->current_state = EDGAI_STATE_CLOSE;
        break;

    case EDGAI_STATE_CLOSE:
        session->current_state = EDGAI_STATE_DONE;
        break;

    case EDGAI_STATE_DONE:
        /* Terminal — no transition */
        break;
    }

    return session->current_state;
}

/* ── JSON array element extractor ────────────────────────────────────────── */

/*
 * Extract the string at position `index` from a JSON array string.
 * e.g. ["Step 1", "Step 2"] with index=1 → "Step 2"
 * Returns heap-allocated string. Caller must free. NULL on error.
 */
static char *json_array_get(const char *json, int index)
{
    if (!json) return NULL;

    const char *p = json;
    while (*p && *p != '[') p++;
    if (!*p) return NULL;
    p++; /* skip '[' */

    /* Skip forward through `index` elements */
    for (int i = 0; i < index; i++) {
        int depth = 0;
        int in_str = 0;
        while (*p) {
            if (!in_str && *p == '"') {
                in_str = 1; p++; continue;
            }
            if (in_str) {
                if (*p == '\\') { p += 2; continue; }
                if (*p == '"')  { in_str = 0; p++; continue; }
                p++; continue;
            }
            if (*p == '[' || *p == '{') { depth++; p++; continue; }
            if (*p == ']' || *p == '}') { depth--; p++; continue; }
            if (*p == ',' && depth == 0) { p++; break; }
            p++;
        }
        if (!*p) return NULL;
    }

    /* Skip whitespace */
    while (*p == ' ' || *p == '\n' || *p == '\t') p++;

    if (*p == '"') {
        p++;
        const char *start = p;
        while (*p && !(*p == '"' && *(p - 1) != '\\')) p++;
        size_t len = (size_t)(p - start);
        char *out = malloc(len + 1);
        if (!out) return NULL;
        memcpy(out, start, len);
        out[len] = '\0';
        return out;
    }
    return NULL;
}

/* Count elements in a JSON array string */
static int json_array_count(const char *json)
{
    if (!json) return 0;

    const char *p = json;
    while (*p && *p != '[') p++;
    if (!*p) return 0;
    p++;

    while (*p == ' ') p++;
    if (*p == ']') return 0; /* empty array */

    int count = 1;
    int depth = 0;
    int in_str = 0;

    while (*p && !(*p == ']' && depth == 0 && !in_str)) {
        if (!in_str && *p == '"') { in_str = 1; p++; continue; }
        if (in_str) {
            if (*p == '\\') { p += 2; continue; }
            if (*p == '"')  { in_str = 0; p++; continue; }
            p++; continue;
        }
        if (*p == '[' || *p == '{') { depth++; }
        else if (*p == '}' || *p == ']') { depth--; }
        else if (*p == ',' && depth == 0) { count++; }
        p++;
    }
    return count;
}

/* ── DB content resolver ─────────────────────────────────────────────────── */

/*
 * Get the DB-driven response content for the session's current state.
 * For CONCEPT state, returns the question_text so the caller can pass it to
 * edgai_llm_explain_concept(). All other states return DB content directly.
 *
 * Returns heap-allocated string. Caller must free. NULL on DB error.
 */
char *edgai_state_get_db_content(EdgaiSession *session, sqlite3 *db)
{
    if (!session || !db || session->current_question_id[0] == '\0')
        return NULL;

    /* Fixed strings for terminal states */
    if (session->current_state == EDGAI_STATE_CLOSE)
        return strdup("Great work. Ready for the next question?");
    if (session->current_state == EDGAI_STATE_DONE)
        return strdup("Session complete.");

    /* Fetch row for the current question */
    const char *sql =
        "SELECT question_text, hook, predict, steps, how_to_ace "
        "FROM questions WHERE id = ? LIMIT 1;";

    fprintf(stderr, "edgai: DB lookup — sql='%s' question_id='%s' state=%d\n",
            sql, session->current_question_id, (int)session->current_state);

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "edgai: DB prepare failed: %s\n", sqlite3_errmsg(db));
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, session->current_question_id, -1, SQLITE_STATIC);

    char *result = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *question_text = sqlite3_column_text(stmt, 0);
        const unsigned char *hook          = sqlite3_column_text(stmt, 1);
        const unsigned char *predict       = sqlite3_column_text(stmt, 2);
        const unsigned char *steps_json    = sqlite3_column_text(stmt, 3);
        const unsigned char *how_to_ace    = sqlite3_column_text(stmt, 4);

        switch (session->current_state) {
        case EDGAI_STATE_CONCEPT:
            /* Return question_text — caller passes this to edgai_llm_explain_concept() */
            result = question_text ? strdup((const char *)question_text) : NULL;
            break;

        case EDGAI_STATE_HOOK:
            result = hook ? strdup((const char *)hook) : NULL;
            break;

        case EDGAI_STATE_PREDICT:
            result = predict ? strdup((const char *)predict) : NULL;
            break;

        case EDGAI_STATE_STEPS: {
            const char *sj = steps_json ? (const char *)steps_json : NULL;
            if (session->total_steps == 0 && sj)
                session->total_steps = json_array_count(sj);
            result = sj ? json_array_get(sj, session->step_index) : NULL;
            break;
        }

        case EDGAI_STATE_VERIFY: {
            if (question_text) {
                char buf[1024];
                snprintf(buf, sizeof(buf),
                    "Is that clear? Can you tell me: %s",
                    (const char *)question_text);
                result = strdup(buf);
            }
            break;
        }

        case EDGAI_STATE_PRACTICE:
            result = how_to_ace ? strdup((const char *)how_to_ace) : NULL;
            break;

        default:
            break;
        }
    } else {
        fprintf(stderr, "edgai: DB lookup returned no row — question_id='%s' errmsg='%s'\n",
                session->current_question_id, sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
    return result;
}

/* ── New-topic detection ─────────────────────────────────────────────────── */

/*
 * Detect if the student's message contains 2+ curriculum keywords via FTS5.
 * Returns 1 if 2+ words individually hit the FTS5 index, 0 otherwise.
 */
int edgai_state_is_new_topic(const char *student_message, sqlite3 *db)
{
    if (!student_message || !db)
        return 0;

    char *lower = strdup(student_message);
    if (!lower) return 0;

    for (char *p = lower; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + 32);
        else if (!(*p >= 'a' && *p <= 'z')) *p = ' ';
    }

    const char *count_sql =
        "SELECT count(*) FROM questions_fts WHERE questions_fts MATCH ?;";

    int hits = 0;
    char *tok = strtok(lower, " ");
    while (tok && hits < 2) {
        if (strlen(tok) < 3) { tok = strtok(NULL, " "); continue; }

        sqlite3_stmt *stmt = NULL;
        if (sqlite3_prepare_v2(db, count_sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, tok, -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                if (sqlite3_column_int(stmt, 0) > 0)
                    hits++;
            }
            sqlite3_finalize(stmt);
        }
        tok = strtok(NULL, " ");
    }

    free(lower);
    return hits >= 2 ? 1 : 0;
}
