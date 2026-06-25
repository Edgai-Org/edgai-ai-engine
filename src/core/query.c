/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "eduos/eduos.h"

#define RAG_SOCK_PATH  "/tmp/eduos_rag.sock"
#define READ_BUF_SIZE  65536

/* engine.py expects string age_mode values, not integers */
static const char *age_mode_str(eduos_age_mode_t mode)
{
    switch (mode) {
        case EDUOS_MODE_PLAYGROUND:   return "playground";
        case EDUOS_MODE_EXPLORER:     return "explorer";
        case EDUOS_MODE_LAUNCHPAD:    return "launchpad";
        case EDUOS_MODE_PROFESSIONAL: return "professional";
        default:                      return "launchpad";
    }
}

/* map uppercase sequence_state strings returned by engine.py to the C enum */
static eduos_sequence_state_t parse_sequence_state(const char *s)
{
    if (!s) return EDUOS_STATE_CONCEPT;
    if (strcmp(s, "CONCEPT")  == 0) return EDUOS_STATE_CONCEPT;
    if (strcmp(s, "HOOK")     == 0) return EDUOS_STATE_HOOK;
    if (strcmp(s, "PREDICT")  == 0) return EDUOS_STATE_PREDICT;
    if (strcmp(s, "STEPS")    == 0) return EDUOS_STATE_STEPS;
    if (strcmp(s, "VERIFY")   == 0) return EDUOS_STATE_VERIFY;
    if (strcmp(s, "PRACTICE") == 0) return EDUOS_STATE_PRACTICE;
    if (strcmp(s, "CLOSE")    == 0) return EDUOS_STATE_CLOSE;
    if (strcmp(s, "DONE")     == 0) return EDUOS_STATE_DONE;
    /* lowercase fallback */
    if (strcmp(s, "concept")  == 0) return EDUOS_STATE_CONCEPT;
    if (strcmp(s, "hook")     == 0) return EDUOS_STATE_HOOK;
    if (strcmp(s, "predict")  == 0) return EDUOS_STATE_PREDICT;
    if (strcmp(s, "steps")    == 0) return EDUOS_STATE_STEPS;
    if (strcmp(s, "verify")   == 0) return EDUOS_STATE_VERIFY;
    if (strcmp(s, "practice") == 0) return EDUOS_STATE_PRACTICE;
    if (strcmp(s, "close")    == 0) return EDUOS_STATE_CLOSE;
    if (strcmp(s, "done")     == 0) return EDUOS_STATE_DONE;
    long v = strtol(s, NULL, 10);
    if (v >= 0 && v <= (long)EDUOS_STATE_DONE)
        return (eduos_sequence_state_t)v;
    return EDUOS_STATE_CONCEPT;
}

/* escape JSON special characters in src into dst */
static void json_escape(const char *src, char *dst, size_t dst_size)
{
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j < dst_size - 3; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"' || c == '\\') {
            dst[j++] = '\\';
            dst[j++] = (char)c;
        } else if (c == '\n') {
            dst[j++] = '\\'; dst[j++] = 'n';
        } else if (c == '\r') {
            dst[j++] = '\\'; dst[j++] = 'r';
        } else if (c == '\t') {
            dst[j++] = '\\'; dst[j++] = 't';
        } else {
            dst[j++] = (char)c;
        }
    }
    dst[j] = '\0';
}

/*
 * extract a JSON string value for the given key.
 * returns heap-allocated string on success, NULL if key missing or value is null.
 */
static char *json_extract_string(const char *json, const char *key)
{
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);

    char *pos = strstr(json, search);
    if (!pos) return NULL;

    pos += strlen(search);
    while (*pos == ' ' || *pos == ':' || *pos == '\t') pos++;

    if (*pos == 'n') return NULL; /* JSON null */
    if (*pos != '"') return NULL;
    pos++;

    /* find closing quote, skip escaped quotes */
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

/* extract a JSON integer value for the given key */
static int json_extract_int(const char *json, const char *key, int default_val)
{
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);

    char *pos = strstr(json, search);
    if (!pos) return default_val;

    pos += strlen(search);
    while (*pos == ' ' || *pos == ':' || *pos == '\t') pos++;

    if (*pos == '-' || (*pos >= '0' && *pos <= '9'))
        return (int)strtol(pos, NULL, 10);
    return default_val;
}

/* extract a JSON boolean value for the given key */
static int json_extract_bool(const char *json, const char *key, int default_val)
{
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);

    char *pos = strstr(json, search);
    if (!pos) return default_val;

    pos += strlen(search);
    while (*pos == ' ' || *pos == ':' || *pos == '\t') pos++;

    if (strncmp(pos, "true",  4) == 0) return 1;
    if (strncmp(pos, "false", 5) == 0) return 0;
    return default_val;
}

static eduos_response_t *make_error_response(const char *msg)
{
    eduos_response_t *r = calloc(1, sizeof(eduos_response_t));
    if (!r) return NULL;
    r->error = strdup(msg);
    return r;
}

static int reconnect(eduos_session_t *session)
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

eduos_response_t *eduos_query(eduos_session_t *session, const char *text)
{
    if (!session || !text)
        return make_error_response("invalid arguments");

    if (session->socket_fd < 0 && reconnect(session) < 0)
        return make_error_response("engine.py not reachable — run: python3 src/rag/engine.py");

    char escaped[4096];
    json_escape(text, escaped, sizeof(escaped));

    char payload[8192];
    int n = snprintf(payload, sizeof(payload),
        "{\"session_id\":\"%s\",\"text\":\"%s\",\"age_mode\":\"%s\"}\n",
        session->session_id, escaped, age_mode_str(session->age_mode));

    if (n < 0 || (size_t)n >= sizeof(payload))
        return make_error_response("query text too long");

    ssize_t written = write(session->socket_fd, payload, (size_t)n);
    if (written != (ssize_t)n) {
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

    eduos_response_t *resp = calloc(1, sizeof(eduos_response_t));
    if (!resp) { free(buf); return make_error_response("out of memory"); }

    resp->text       = json_extract_string(buf, "text");
    resp->step_index = json_extract_int(buf, "step_index", 0);
    resp->can_skip   = json_extract_bool(buf, "can_skip", 0);
    resp->error      = json_extract_string(buf, "error");

    char *state_str = json_extract_string(buf, "sequence_state");
    resp->sequence_state = parse_sequence_state(state_str);
    free(state_str);

    char *qid = json_extract_string(buf, "question_id");
    if (qid) {
        strncpy(resp->question_id, qid, sizeof(resp->question_id) - 1);
        free(qid);
    }

    free(buf);
    return resp;
}

void eduos_response_free(eduos_response_t *response)
{
    if (!response) return;
    free(response->text);
    free(response->error);
    free(response);
}
