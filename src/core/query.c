/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 *
 * query.c — edgai_query() Phase 4 implementation.
 *
 * Default path: direct llama.cpp + RAG pipeline.
 * Compile with -DEDGAI_SOCKET_FALLBACK to use Phase 3 engine.py socket bridge.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <sqlite3.h>
#include "edgai/edgai.h"
#include "edgai/edgai_rag.h"

#ifdef EDGAI_VOICE_ENABLED
#include "../voice/transcribe.h"
#include "../voice/speak.h"
#endif

/* ── Forward declarations ───────────────────────────────────────────────── */

/* state.c */
EdgaiSequenceState edgai_state_next(EdgaiSession *session,
                                         EdgaiIntent intent);
char *edgai_state_get_db_content(EdgaiSession *session, sqlite3 *db);

/* llama_backend.c */
char *edgai_llm_explain_concept(EdgaiSession *session,
                                const char *question_text);
char *edgai_llm_rephrase(EdgaiSession *session,
                          const char *prev_explanation,
                          const char *student_message);

/* formatter.c */
char *edgai_format_response(const char *raw, EdgaiAgeMode mode);


/* ── Common helpers ─────────────────────────────────────────────────────── */

static EdgaiResponse *make_error_response(const char *msg)
{
    EdgaiResponse *r = calloc(1, sizeof(EdgaiResponse));
    if (!r) return NULL;
    r->error = strdup(msg);
    return r;
}

/* ── Intent detection — keyword matching (instant, zero cost) ───────────── */

/*
 * Keywords intentionally excluded:
 *   "what"    — starts nearly every new-topic query ("what is logarithm")
 *   "explain" — ambiguous: "explain logarithms" is a new topic, not RE_EXPLAIN
 * Both caused false positives that bypassed new-topic detection entirely.
 */
static const char *ADVANCE_KEYWORDS[] = {
    "ok", "got it", "next", "yes", "yep", "sure", "i see",
    "continue", "understand", "makes sense", "go on", "proceed", NULL
};
static const char *RE_EXPLAIN_KEYWORDS[] = {
    "confused", "don't understand", "dont understand", "huh",
    "again", "unclear", "lost", "rephrase", "simpler", NULL
};
static const char *SKIP_KEYWORDS[] = {
    "skip", "boring", "already know", "move on", NULL
};
static const char *DECLINE_KEYWORDS[] = {
    "stop", "quit", "exit", "bye", "no thanks", "not now", NULL
};

/*
 * Whole-word substring search — prevents "ok" matching inside "look" or "book",
 * "yes" inside "yesterday", etc.
 * A match is valid only when the keyword is preceded and followed by a
 * non-alphanumeric character (or the start/end of the string).
 */
static const char *word_match(const char *text, const char *kw)
{
    size_t klen = strlen(kw);
    const char *p = text;
    while ((p = strstr(p, kw)) != NULL) {
        int pre_ok  = (p == text) || !isalnum((unsigned char)p[-1]);
        int post_ok = !isalnum((unsigned char)p[klen]);
        if (pre_ok && post_ok)
            return kw;
        p++;
    }
    return NULL;
}

/* Returns the first matching keyword string, or NULL. Operates on lowercase. */
static const char *find_keyword(const char *lower_text, const char **keywords)
{
    for (int i = 0; keywords[i]; i++) {
        if (word_match(lower_text, keywords[i]))
            return keywords[i];
    }
    return NULL;
}

static EdgaiIntent edgai_detect_intent_keywords(const char *text)
{
    char lower[1024];
    size_t len = strlen(text);
    if (len >= sizeof(lower)) len = sizeof(lower) - 1;
    for (size_t i = 0; i < len; i++)
        lower[i] = (char)tolower((unsigned char)text[i]);
    lower[len] = '\0';

    const char *kw;

    if ((kw = find_keyword(lower, DECLINE_KEYWORDS))) {
        fprintf(stderr, "edgai: intent keyword DECLINE matched '%s'\n", kw);
        return EDGAI_INTENT_DECLINE;
    }
    if ((kw = find_keyword(lower, SKIP_KEYWORDS))) {
        fprintf(stderr, "edgai: intent keyword SKIP matched '%s'\n", kw);
        return EDGAI_INTENT_SKIP;
    }
    if ((kw = find_keyword(lower, RE_EXPLAIN_KEYWORDS))) {
        fprintf(stderr, "edgai: intent keyword RE_EXPLAIN matched '%s'\n", kw);
        return EDGAI_INTENT_RE_EXPLAIN;
    }
    if ((kw = find_keyword(lower, ADVANCE_KEYWORDS))) {
        fprintf(stderr, "edgai: intent keyword ADVANCE matched '%s'\n", kw);
        return EDGAI_INTENT_ADVANCE;
    }
    return EDGAI_INTENT_UNKNOWN;
}

/* ── Socket fallback (Phase 3 emergency path) ───────────────────────────── */

#ifdef EDGAI_SOCKET_FALLBACK

/* These JSON helpers are only needed by the socket path — inside the ifdef. */

static void json_escape(const char *src, char *dst, size_t dst_size)
{
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j < dst_size - 3; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"' || c == '\\') { dst[j++] = '\\'; dst[j++] = (char)c; }
        else if (c == '\n') { dst[j++] = '\\'; dst[j++] = 'n'; }
        else if (c == '\r') { dst[j++] = '\\'; dst[j++] = 'r'; }
        else if (c == '\t') { dst[j++] = '\\'; dst[j++] = 't'; }
        else dst[j++] = (char)c;
    }
    dst[j] = '\0';
}

static char *json_extract_string(const char *json, const char *key)
{
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    char *pos = strstr(json, search);
    if (!pos) return NULL;
    pos += strlen(search);
    while (*pos == ' ' || *pos == ':' || *pos == '\t') pos++;
    if (*pos == 'n') return NULL;
    if (*pos != '"') return NULL;
    pos++;
    const char *p = pos;
    while (*p) {
        if (*p == '\\') { p += 2; continue; }
        if (*p == '"')  break;
        p++;
    }
    if (!*p) return NULL;
    size_t len = (size_t)(p - pos);
    char *result = malloc(len + 1);
    if (!result) return NULL;
    memcpy(result, pos, len);
    result[len] = '\0';
    return result;
}

static int json_extract_int(const char *json, const char *key, int def)
{
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    char *pos = strstr(json, search);
    if (!pos) return def;
    pos += strlen(search);
    while (*pos == ' ' || *pos == ':' || *pos == '\t') pos++;
    if (*pos == '-' || (*pos >= '0' && *pos <= '9'))
        return (int)strtol(pos, NULL, 10);
    return def;
}

static int json_extract_bool(const char *json, const char *key, int def)
{
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    char *pos = strstr(json, search);
    if (!pos) return def;
    pos += strlen(search);
    while (*pos == ' ' || *pos == ':' || *pos == '\t') pos++;
    if (strncmp(pos, "true",  4) == 0) return 1;
    if (strncmp(pos, "false", 5) == 0) return 0;
    return def;
}

static EdgaiSequenceState parse_sequence_state(const char *s)
{
    if (!s) return EDGAI_STATE_CONCEPT;
    if (strcmp(s, "CONCEPT")  == 0 || strcmp(s, "concept")  == 0) return EDGAI_STATE_CONCEPT;
    if (strcmp(s, "HOOK")     == 0 || strcmp(s, "hook")     == 0) return EDGAI_STATE_HOOK;
    if (strcmp(s, "PREDICT")  == 0 || strcmp(s, "predict")  == 0) return EDGAI_STATE_PREDICT;
    if (strcmp(s, "STEPS")    == 0 || strcmp(s, "steps")    == 0) return EDGAI_STATE_STEPS;
    if (strcmp(s, "VERIFY")   == 0 || strcmp(s, "verify")   == 0) return EDGAI_STATE_VERIFY;
    if (strcmp(s, "PRACTICE") == 0 || strcmp(s, "practice") == 0) return EDGAI_STATE_PRACTICE;
    if (strcmp(s, "CLOSE")    == 0 || strcmp(s, "close")    == 0) return EDGAI_STATE_CLOSE;
    if (strcmp(s, "DONE")     == 0 || strcmp(s, "done")     == 0) return EDGAI_STATE_DONE;
    long v = strtol(s, NULL, 10);
    if (v >= 0 && v <= (long)EDGAI_STATE_DONE) return (EdgaiSequenceState)v;
    return EDGAI_STATE_CONCEPT;
}

#include <sys/socket.h>
#include <sys/un.h>

#define RAG_SOCK_PATH  "/tmp/edgai_rag.sock"
#define READ_BUF_SIZE  65536

static const char *age_mode_str(EdgaiAgeMode mode)
{
    switch (mode) {
    case EDGAI_MODE_PLAYGROUND:   return "playground";
    case EDGAI_MODE_EXPLORER:     return "explorer";
    case EDGAI_MODE_LAUNCHPAD:    return "launchpad";
    case EDGAI_MODE_PROFESSIONAL: return "professional";
    default:                      return "launchpad";
    }
}

static int socket_reconnect(EdgaiSession *session)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, RAG_SOCK_PATH, sizeof(addr.sun_path) - 1);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    session->socket_fd = fd;
    return fd;
}

static EdgaiResponse *query_via_socket(EdgaiSession *session,
                                            const char *text)
{
    if (session->socket_fd < 0 && socket_reconnect(session) < 0)
        return make_error_response("engine.py not reachable — run: python3 src/rag/engine.py");

    char escaped[4096];
    json_escape(text, escaped, sizeof(escaped));

    char payload[8192];
    int n = snprintf(payload, sizeof(payload),
        "{\"session_id\":\"%s\",\"text\":\"%s\",\"age_mode\":\"%s\"}\n",
        session->session_id, escaped, age_mode_str(session->age_mode));
    if (n < 0 || (size_t)n >= sizeof(payload))
        return make_error_response("query text too long");

    if (write(session->socket_fd, payload, (size_t)n) != (ssize_t)n) {
        close(session->socket_fd);
        session->socket_fd = -1;
        return make_error_response("socket write failed");
    }

    char *buf = malloc(READ_BUF_SIZE);
    if (!buf) return make_error_response("out of memory");

    size_t total = 0;
    while (total < (size_t)READ_BUF_SIZE - 1) {
        ssize_t r = read(session->socket_fd, buf + total,
                         (size_t)READ_BUF_SIZE - 1 - total);
        if (r <= 0) {
            free(buf);
            close(session->socket_fd);
            session->socket_fd = -1;
            return make_error_response("socket read failed");
        }
        total += (size_t)r;
        if (memchr(buf, '\n', total)) break;
    }
    buf[total] = '\0';

    EdgaiResponse *resp = calloc(1, sizeof(EdgaiResponse));
    if (!resp) { free(buf); return make_error_response("out of memory"); }

    resp->text       = json_extract_string(buf, "text");
    resp->step_index = json_extract_int(buf, "step_index", 0);
    resp->can_skip   = json_extract_bool(buf, "can_skip", 0);
    resp->error      = json_extract_string(buf, "error");

    char *state_str = json_extract_string(buf, "sequence_state");
    resp->sequence_state = parse_sequence_state(state_str);
    free(state_str);

    char *qid = json_extract_string(buf, "question_id");
    if (qid) { strncpy(resp->question_id, qid, sizeof(resp->question_id) - 1); free(qid); }

    free(buf);
    return resp;
}

#endif /* EDGAI_SOCKET_FALLBACK */

/* ── Phase 4 — direct llama.cpp + RAG pipeline ──────────────────────────── */

#ifndef EDGAI_SOCKET_FALLBACK

static EdgaiResponse *query_direct(EdgaiSession *session,
                                       const char *text)
{
    sqlite3 *db = session->db;

    /* Short-circuit: very short messages (≤3 chars) with no topic loaded are
     * greetings or noise — return a prompt without touching state or the LLM. */
    if (session->current_question_id[0] == '\0' && strlen(text) <= 3) {
        EdgaiResponse *resp = calloc(1, sizeof(EdgaiResponse));
        if (!resp) return make_error_response("out of memory");
        resp->text = strdup(
            "Hello! Tell me what topic you'd like to study — "
            "for example: 'what is logarithm' or 'explain quadratic equations'.");
        resp->sequence_state = EDGAI_STATE_CONCEPT;
        resp->can_skip       = 0;
        return resp;
    }

    /* a. Keyword-only intent detection */
    EdgaiIntent intent = edgai_detect_intent_keywords(text);

    /* b. UNKNOWN → FTS5 topic lookup.
     * Found:    load the top result, keep intent=UNKNOWN so state stays at CONCEPT.
     * Not found: tell the student and return immediately. */
    if (intent == EDGAI_INTENT_UNKNOWN) {
        if (!db) {
            /* No DB available — nothing to look up, fall through */
        } else {
            int count = 0;
            EdgaiRagResult *results =
                edgai_rag_retrieve(db, text, session->age_mode, 1, &count);

            if (results && count > 0) {
                strncpy(session->current_question_id, results[0].question_id,
                        sizeof(session->current_question_id) - 1);
                session->current_state = EDGAI_STATE_CONCEPT;
                session->step_index    = 0;
                session->total_steps   = 0;
                fprintf(stderr, "edgai: topic loaded: %s\n",
                        session->current_question_id);
                edgai_rag_results_free(results, count);
                /* intent stays UNKNOWN — state machine holds at CONCEPT */
            } else {
                if (results) edgai_rag_results_free(results, count);
                EdgaiResponse *resp = calloc(1, sizeof(EdgaiResponse));
                if (!resp) return make_error_response("out of memory");
                resp->text = strdup(
                    "I don't know that topic yet. "
                    "Try asking about logarithms, quadratic equations, "
                    "or arithmetic progression.");
                resp->sequence_state = session->current_state;
                resp->can_skip       = 0;
                return resp;
            }
        }
    }

    /* c. Advance state machine */
    edgai_state_next(session, intent);

    /* d. Get DB content for new state */
    char *raw_content = db ? edgai_state_get_db_content(session, db) : NULL;

    /* e. LLM calls for CONCEPT and STEPS+RE_EXPLAIN */
    char *llm_text = NULL;

    if (session->current_state == EDGAI_STATE_CONCEPT) {
        /* raw_content is the question_text; pass it to LLM */
        llm_text = edgai_llm_explain_concept(session,
                    raw_content ? raw_content : "this question");
    } else if (session->current_state == EDGAI_STATE_STEPS &&
               intent == EDGAI_INTENT_RE_EXPLAIN) {
        /* Rephrase the current step */
        llm_text = edgai_llm_rephrase(session,
                    raw_content ? raw_content : "", text);
    }

    /* f. Choose the text source (LLM wins over DB for CONCEPT/rephrase) */
    const char *source = llm_text ? llm_text : raw_content;
    if (!source) source = "I could not find content for this step.";

    /* g. Format for age mode */
    char *formatted = edgai_format_response(source, session->age_mode);

    /* h. Build response */
    EdgaiResponse *resp = calloc(1, sizeof(EdgaiResponse));
    if (!resp) {
        free(llm_text);
        free(raw_content);
        free(formatted);
        return make_error_response("out of memory");
    }

    resp->text           = formatted;
    resp->sequence_state = session->current_state;
    resp->step_index     = session->step_index;
    resp->can_skip       = (session->current_state != EDGAI_STATE_DONE) ? 1 : 0;
    strncpy(resp->question_id, session->current_question_id,
            sizeof(resp->question_id) - 1);

    free(llm_text);
    free(raw_content);
    return resp;
}

#endif /* !EDGAI_SOCKET_FALLBACK */

/* ── Public API ─────────────────────────────────────────────────────────── */

EdgaiResponse *edgai_query(EdgaiSession *session, const char *text)
{
    if (!session || !text)
        return make_error_response("invalid arguments");

#ifdef EDGAI_SOCKET_FALLBACK
    return query_via_socket(session, text);
#else
    return query_direct(session, text);
#endif
}

void edgai_response_free(EdgaiResponse *response)
{
    if (!response) return;
    free(response->text);
    free(response->error);
    free(response);
}

/*
 * edgai_voice_turn — one full voice interaction cycle:
 *   1. Capture mic audio via VAD-gated Vosk STT (blocks until end of speech)
 *   2. Feed transcript to edgai_query() to get a teaching response
 *   3. Speak the response text via Piper+ALSA (blocks until playback completes
 *      or session->speak_interrupt is set by caller)
 *
 * Returns the EdgaiResponse* that resulted from the transcript, or NULL if
 * no speech was detected, voice is disabled, or an error occurred.
 * Caller must free the returned response with edgai_response_free().
 *
 * State transitions set on session->voice_state by the sub-functions:
 *   IDLE → LISTENING (in edgai_transcribe_listen)
 *   LISTENING → TRANSCRIBED (on speech detected)
 *   TRANSCRIBED → GENERATING (set here before edgai_query)
 *   GENERATING → SPEAKING (set here before edgai_speak)
 *   SPEAKING → IDLE (set here on completion)
 */
EdgaiResponse *edgai_voice_turn(EdgaiSession *session)
{
    if (!session)
        return NULL;

#ifndef EDGAI_VOICE_ENABLED
    return NULL;
#else
    if (!session->voice_enabled)
        return NULL;

    /* ── Step 1: Transcribe ─────────────────────────────────────────── */
    char *transcript = edgai_transcribe_listen(session);
    if (!transcript) {
        session->voice_state = EDGAI_VOICE_STATE_IDLE;
        return NULL;
    }

    fprintf(stderr, "edgai: voice transcript: %s\n", transcript);

    /* ── Step 2: Generate response ──────────────────────────────────── */
    session->voice_state = EDGAI_VOICE_STATE_GENERATING;

    EdgaiResponse *resp = edgai_query(session, transcript);
    free(transcript);

    if (!resp || !resp->text) {
        session->voice_state = EDGAI_VOICE_STATE_IDLE;
        return resp;
    }

    /* ── Step 3: Speak response ─────────────────────────────────────── */
    session->voice_state = EDGAI_VOICE_STATE_SPEAKING;

    edgai_speak(session, resp->text);

    session->voice_state = EDGAI_VOICE_STATE_IDLE;
    return resp;
#endif
}
