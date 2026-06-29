/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 *
 * preprocessor.c — query preprocessing: lowercase, stop word removal,
 * synonym expansion, and Porter stemmer.
 * Zero external dependencies — everything implemented from scratch.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "edgai/edgai_types.h"

/* ── Stop words ─────────────────────────────────────────────────────────── */

static const char *STOP_WORDS[] = {
    "a", "an", "the", "is", "it", "in", "on", "at", "to", "for",
    "of", "and", "or", "but", "with", "what", "how", "why", "when",
    "do", "does", "did", "can", "could", "will", "would", "i", "me",
    "my", "we", "you", "he", "she", "they", "this", "that", "which",
    NULL
};

static int is_stop_word(const char *word)
{
    for (int i = 0; STOP_WORDS[i]; i++) {
        if (strcmp(word, STOP_WORDS[i]) == 0)
            return 1;
    }
    return 0;
}

/* ── Synonym expansion ──────────────────────────────────────────────────── */

typedef struct { const char *term; const char *canonical; } edgai_synonym_t;

static const edgai_synonym_t SYNONYMS[] = {
    {"log",       "logarithm"},
    {"logs",      "logarithm"},
    {"trig",      "trigonometry"},
    {"indices",   "index"},
    {"powers",    "index"},
    {"expo",      "exponential"},
    {"quadratic", "quadratic equation"},
    {"quad",      "quadratic equation"},
    {"seq",       "sequence"},
    {"ap",        "arithmetic progression"},
    {"gp",        "geometric progression"},
    {"diff",      "differentiation"},
    {"integ",     "integration"},
    {"prob",      "probability"},
    {"permu",     "permutation"},
    {"combo",     "combination"},
    {NULL, NULL}
};

static const char *expand_synonym(const char *word)
{
    for (int i = 0; SYNONYMS[i].term; i++) {
        if (strcmp(word, SYNONYMS[i].term) == 0)
            return SYNONYMS[i].canonical;
    }
    return NULL;
}

/* ── Porter Stemmer (Martin Porter, 1980) ───────────────────────────────── */

static int is_consonant(const char *word, int i)
{
    char c = word[i];
    if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u')
        return 0;
    if (c == 'y')
        return (i == 0) ? 1 : !is_consonant(word, i - 1);
    return 1;
}

/* Measure m: count of VC sequences in word[0..k-1] */
static int measure(const char *word, int k)
{
    int m = 0, i = 0;
    while (i <= k) {
        while (i <= k && is_consonant(word, i)) i++;
        if (i > k) break;
        while (i <= k && !is_consonant(word, i)) i++;
        m++;
    }
    return m;
}

static int vowel_in_stem(const char *word, int j)
{
    for (int i = 0; i <= j; i++)
        if (!is_consonant(word, i)) return 1;
    return 0;
}

static int double_consonant(const char *word, int k)
{
    if (k < 1) return 0;
    return word[k] == word[k - 1] && is_consonant(word, k);
}

/* cvc: ends consonant-vowel-consonant where last C != w, x, y */
static int cvc(const char *word, int k)
{
    if (k < 2) return 0;
    char c = word[k];
    return is_consonant(word, k) &&
           !is_consonant(word, k - 1) &&
           is_consonant(word, k - 2) &&
           c != 'w' && c != 'x' && c != 'y';
}

/* Check suffix and update k to end of stem if matched */
static int ends(char *word, int *k, const char *suffix)
{
    int slen = (int)strlen(suffix);
    int wlen = *k + 1;
    if (slen > wlen) return 0;
    if (memcmp(word + wlen - slen, suffix, (size_t)slen) != 0) return 0;
    *k = wlen - slen - 1;
    return 1;
}

/* Replace suffix on word[0..*k] with s, update *k to end of new word */
static void setto(char *word, int *k, int old_k, const char *s)
{
    int slen = (int)strlen(s);
    memcpy(word + *k + 1, s, (size_t)slen);
    *k = *k + slen;
    (void)old_k;
}

static void step1a(char *word, int *k)
{
    int j = *k;
    if (ends(word, &j, "sses"))      { *k = j + 2; }
    else if (ends(word, &j, "ies")) { word[j + 1] = 'i'; *k = j + 1; }
    else if (word[*k] == 's' && !ends(word, &j, "ss")) { (*k)--; }
}

static void step1b(char *word, int *k)
{
    int j = *k;
    if (ends(word, &j, "eed")) {
        if (measure(word, j) > 0)
            (*k)--;
        return;
    }
    int did_step = 0;
    if (ends(word, &j, "ed") && vowel_in_stem(word, j)) {
        *k = j; did_step = 1;
    } else {
        j = *k;
        if (ends(word, &j, "ing") && vowel_in_stem(word, j)) {
            *k = j; did_step = 1;
        }
    }
    if (did_step) {
        j = *k;
        int orig_k = *k;
        if (ends(word, &j, "at"))      { setto(word, &j, orig_k, "ate"); *k = j; }
        else if (ends(word, &j, "bl")) { setto(word, &j, orig_k, "ble"); *k = j; }
        else if (ends(word, &j, "iz")) { setto(word, &j, orig_k, "ize"); *k = j; }
        else if (double_consonant(word, *k)) {
            char c = word[*k];
            if (c != 'l' && c != 's' && c != 'z')
                (*k)--;
        } else if (measure(word, *k) == 1 && cvc(word, *k)) {
            word[++(*k)] = 'e';
        }
    }
}

static void step1c(char *word, int *k)
{
    int j = *k;
    if (ends(word, &j, "y") && vowel_in_stem(word, j))
        word[*k] = 'i';
}

static void step2(char *word, int *k)
{
    int j = *k;
    switch (word[*k - 1]) {
    case 'a':
        if (ends(word, &j, "ational")) { if (measure(word,j)>0) { int ok=j; setto(word,&j,ok,"ate"); *k=j; } break; }
        j = *k;
        if (ends(word, &j, "tional"))  { if (measure(word,j)>0) { int ok=j; setto(word,&j,ok,"tion"); *k=j; } } break;
    case 'c':
        if (ends(word, &j, "enci"))    { if (measure(word,j)>0) { int ok=j; setto(word,&j,ok,"ence"); *k=j; } break; }
        j = *k;
        if (ends(word, &j, "anci"))    { if (measure(word,j)>0) { int ok=j; setto(word,&j,ok,"ance"); *k=j; } } break;
    case 'e':
        if (ends(word, &j, "izer"))    { if (measure(word,j)>0) { int ok=j; setto(word,&j,ok,"ize");  *k=j; } } break;
    case 'l':
        if (ends(word, &j, "abli"))    { if (measure(word,j)>0) { int ok=j; setto(word,&j,ok,"able"); *k=j; } break; }
        j = *k;
        if (ends(word, &j, "alli"))    { if (measure(word,j)>0) { int ok=j; setto(word,&j,ok,"al");   *k=j; } break; }
        j = *k;
        if (ends(word, &j, "entli"))   { if (measure(word,j)>0) { int ok=j; setto(word,&j,ok,"ent");  *k=j; } break; }
        j = *k;
        if (ends(word, &j, "eli"))     { if (measure(word,j)>0) { int ok=j; setto(word,&j,ok,"e");    *k=j; } break; }
        j = *k;
        if (ends(word, &j, "ousli"))   { if (measure(word,j)>0) { int ok=j; setto(word,&j,ok,"ous");  *k=j; } } break;
    case 'o':
        if (ends(word, &j, "ization")) { if (measure(word,j)>0) { int ok=j; setto(word,&j,ok,"ize");  *k=j; } break; }
        j = *k;
        if (ends(word, &j, "ation"))   { if (measure(word,j)>0) { int ok=j; setto(word,&j,ok,"ate");  *k=j; } break; }
        j = *k;
        if (ends(word, &j, "ator"))    { if (measure(word,j)>0) { int ok=j; setto(word,&j,ok,"ate");  *k=j; } } break;
    case 's':
        if (ends(word, &j, "alism"))   { if (measure(word,j)>0) { int ok=j; setto(word,&j,ok,"al");   *k=j; } break; }
        j = *k;
        if (ends(word, &j, "iveness")) { if (measure(word,j)>0) { int ok=j; setto(word,&j,ok,"ive");  *k=j; } break; }
        j = *k;
        if (ends(word, &j, "fulness")) { if (measure(word,j)>0) { int ok=j; setto(word,&j,ok,"ful");  *k=j; } break; }
        j = *k;
        if (ends(word, &j, "ousness")) { if (measure(word,j)>0) { int ok=j; setto(word,&j,ok,"ous");  *k=j; } } break;
    case 't':
        if (ends(word, &j, "aliti"))   { if (measure(word,j)>0) { int ok=j; setto(word,&j,ok,"al");   *k=j; } break; }
        j = *k;
        if (ends(word, &j, "iviti"))   { if (measure(word,j)>0) { int ok=j; setto(word,&j,ok,"ive");  *k=j; } break; }
        j = *k;
        if (ends(word, &j, "biliti"))  { if (measure(word,j)>0) { int ok=j; setto(word,&j,ok,"ble");  *k=j; } } break;
    default: break;
    }
}

static void step3(char *word, int *k)
{
    int j = *k;
    switch (word[*k]) {
    case 'e':
        if (ends(word,&j,"icate")) { if (measure(word,j)>0) { int ok=j; setto(word,&j,ok,"ic"); *k=j; } break; }
        j = *k;
        if (ends(word,&j,"ative")) { if (measure(word,j)>0) { *k=j; } break; }
        j = *k;
        if (ends(word,&j,"alize")) { if (measure(word,j)>0) { int ok=j; setto(word,&j,ok,"al"); *k=j; } } break;
    case 'i':
        if (ends(word,&j,"iciti")) { if (measure(word,j)>0) { int ok=j; setto(word,&j,ok,"ic"); *k=j; } } break;
    case 'l':
        if (ends(word,&j,"ical"))  { if (measure(word,j)>0) { int ok=j; setto(word,&j,ok,"ic"); *k=j; } break; }
        j = *k;
        if (ends(word,&j,"ful"))   { if (measure(word,j)>0) { *k=j; } } break;
    case 's':
        if (ends(word,&j,"ness"))  { if (measure(word,j)>0) { *k=j; } } break;
    default: break;
    }
}

static void step4(char *word, int *k)
{
    int j = *k;
    switch (word[*k - 1]) {
    case 'a':
        ends(word,&j,"al"); break;
    case 'c':
        if (ends(word,&j,"ance")) break;
        j = *k; ends(word,&j,"ence"); break;
    case 'e':
        ends(word,&j,"er"); break;
    case 'i':
        ends(word,&j,"ic"); break;
    case 'l':
        if (ends(word,&j,"able")) break;
        j = *k; ends(word,&j,"ible"); break;
    case 'n':
        if (ends(word,&j,"ant")) break;
        j = *k; if (ends(word,&j,"ement")) break;
        j = *k; if (ends(word,&j,"ment"))  break;
        j = *k; ends(word,&j,"ent"); break;
    case 'o':
        j = *k;
        if (ends(word,&j,"ion")) {
            if (j >= 0 && (word[j] == 's' || word[j] == 't')) break;
        }
        j = *k; ends(word,&j,"ou"); break;
    case 's':
        ends(word,&j,"ism"); break;
    case 't':
        if (ends(word,&j,"ate")) break;
        j = *k; ends(word,&j,"iti"); break;
    case 'u':
        ends(word,&j,"ous"); break;
    case 'v':
        ends(word,&j,"ive"); break;
    case 'z':
        ends(word,&j,"ize"); break;
    default:
        return;
    }
    if (measure(word, j) > 1)
        *k = j;
}

static void step5a(char *word, int *k)
{
    int j = *k;
    if (word[*k] == 'e') {
        int m = measure(word, j - 1);
        if (m > 1 || (m == 1 && !cvc(word, j - 1)))
            (*k)--;
    }
}

static void step5b(char *word, int *k)
{
    if (measure(word, *k) > 1 && double_consonant(word, *k) && word[*k] == 'l')
        (*k)--;
}

/*
 * Stem word in-place. word[] must be NUL-terminated and large enough.
 * Returns new length of the stemmed word.
 * Not static — exported so test_preprocessor can call it directly.
 */
int edgai_porter_stem(char *word, int len)
{
    if (len <= 2) return len;

    int k = len - 1;
    step1a(word, &k);
    step1b(word, &k);
    step1c(word, &k);
    step2(word, &k);
    step3(word, &k);
    step4(word, &k);
    step5a(word, &k);
    step5b(word, &k);
    word[k + 1] = '\0';
    return k + 1;
}

/* ── Public interface ───────────────────────────────────────────────────── */

/*
 * Preprocess a student query:
 *   1. Lowercase
 *   2. Expand synonyms
 *   3. Strip stop words
 *   4. Apply Porter stemmer to each token
 *   5. Return FTS5-ready query string (space-separated stems)
 *
 * Returns heap-allocated string. Caller must free.
 * Returns NULL on allocation failure.
 */
char *edgai_preprocess_query(const char *raw_query)
{
    if (!raw_query) return NULL;

    size_t len = strlen(raw_query);
    char *lower = malloc(len + 1);
    if (!lower) return NULL;

    /* Lowercase + replace non-alpha with spaces */
    for (size_t i = 0; i < len; i++)
        lower[i] = isalpha((unsigned char)raw_query[i])
                   ? (char)tolower((unsigned char)raw_query[i])
                   : ' ';
    lower[len] = '\0';

    /* Output buffer — at most 4x input length (synonym expansion) */
    size_t out_cap = len * 4 + 256;
    char *out = malloc(out_cap);
    if (!out) { free(lower); return NULL; }
    out[0] = '\0';
    size_t out_len = 0;

    char token[256];
    char *p = lower;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;

        size_t tlen = 0;
        while (*p && *p != ' ' && tlen < sizeof(token) - 1)
            token[tlen++] = *p++;
        token[tlen] = '\0';
        if (tlen == 0) continue;

        /* Synonym expansion — may produce a phrase (with spaces) */
        const char *expanded = expand_synonym(token);
        const char *to_process = expanded ? expanded : token;

        /* Iterate over words in expanded string */
        char exp_copy[512];
        strncpy(exp_copy, to_process, sizeof(exp_copy) - 1);
        exp_copy[sizeof(exp_copy) - 1] = '\0';

        char *ep = exp_copy;
        while (*ep) {
            while (*ep == ' ') ep++;
            if (!*ep) break;

            char word[256];
            size_t wlen = 0;
            while (*ep && *ep != ' ' && wlen < sizeof(word) - 1)
                word[wlen++] = *ep++;
            word[wlen] = '\0';
            if (wlen == 0) continue;

            if (is_stop_word(word)) continue;

            int stemmed_len = edgai_porter_stem(word, (int)wlen);
            if (stemmed_len == 0) continue;

            if (out_len + (size_t)stemmed_len + 2 > out_cap) {
                out_cap *= 2;
                char *tmp = realloc(out, out_cap);
                if (!tmp) { free(out); free(lower); return NULL; }
                out = tmp;
            }
            if (out_len > 0) out[out_len++] = ' ';
            memcpy(out + out_len, word, (size_t)stemmed_len);
            out_len += (size_t)stemmed_len;
            out[out_len] = '\0';
        }
    }

    free(lower);
    return out;
}
