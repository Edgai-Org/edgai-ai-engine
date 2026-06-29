/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 *
 * formatter.c — age-mode response formatting.
 * Phase 4: Playground/Explorer truncate to first 2 sentences.
 * Full vocabulary adaptation comes in Phase 7 (fine-tuning).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "edgai/edgai_types.h"

/*
 * Return a heap-allocated copy of raw truncated to the first n_sentences.
 * Sentence boundaries: '. ', '! ', '? ', or end of string.
 */
static char *first_n_sentences(const char *raw, int n)
{
    if (!raw) return NULL;

    const char *p = raw;
    int found = 0;
    const char *end = raw + strlen(raw);

    while (*p && found < n) {
        if ((*p == '.' || *p == '!' || *p == '?') &&
            (*(p + 1) == ' ' || *(p + 1) == '\0')) {
            found++;
            if (found == n) {
                p++; /* include the punctuation */
                break;
            }
        }
        p++;
    }

    /* If we didn't find enough sentence boundaries, use the whole string */
    if (found < n)
        p = end;

    size_t len = (size_t)(p - raw);
    char *out = malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, raw, len);
    out[len] = '\0';
    return out;
}

/*
 * Format a DB response string for the student's age mode.
 *
 * PLAYGROUND / EXPLORER  — truncate to first 2 sentences (Phase 4).
 * LAUNCHPAD / PROFESSIONAL — return as-is.
 *
 * Returns heap-allocated string. Caller must free.
 */
char *edgai_format_response(const char *raw, EdgaiAgeMode mode)
{
    if (!raw) return strdup("");

    switch (mode) {
    case EDGAI_MODE_PLAYGROUND:
    case EDGAI_MODE_EXPLORER:
        return first_n_sentences(raw, 2);

    case EDGAI_MODE_LAUNCHPAD:
    case EDGAI_MODE_PROFESSIONAL:
    default:
        return strdup(raw);
    }
}
