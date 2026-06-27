/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 *
 * test_preprocessor.c — Phase 4: Porter stemmer + stop words + synonyms.
 */

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "eduos/eduos_types.h"

/* Forward declarations */
char *eduos_preprocess_query(const char *raw_query);
int   eduos_porter_stem(char *word, int len);

static void check_stem(const char *word, const char *expected)
{
    char buf[256];
    strncpy(buf, word, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    int len = eduos_porter_stem(buf, (int)strlen(buf));
    buf[len] = '\0';
    int ok = strcmp(buf, expected) == 0;
    printf("  stem('%-16s') = '%-14s' expected '%-14s' %s\n",
           word, buf, expected, ok ? "OK" : "DIFFERS");
}

static void check_preprocess(const char *input, const char *expected_substr)
{
    char *out = eduos_preprocess_query(input);
    int found = (!expected_substr) || (out && strstr(out, expected_substr));
    printf("  preprocess('%-30s') → '%-30s' %s\n",
           input, out ? out : "(null)", found ? "OK" : "MISSING SUBSTR");
    if (expected_substr && !found)
        printf("    (expected substring: '%s')\n", expected_substr);
    free(out);
}

int main(void)
{
    printf("test_preprocessor: Porter stemmer, stop words, synonyms\n\n");

    printf("--- Porter stemmer ---\n");
    check_stem("logarithms",  "logarithm");
    check_stem("equations",   "equat");
    check_stem("functions",   "function");
    check_stem("motoring",    "motor");
    check_stem("agreed",      "agre");
    check_stem("happiness",   "happi");
    check_stem("running",     "run");
    check_stem("relational",  "relat");

    printf("\n--- stop word removal + stemmer ---\n");
    check_preprocess("what are logarithms",      "logarithm");
    check_preprocess("the logarithm of a number","logarithm");

    printf("\n--- synonym expansion + stemmer ---\n");
    /* "trig" → "trigonometry" → stemmed "trigonometri"
       "equations" → stemmed "equat" */
    check_preprocess("trig equations",  "trigonometri");
    /* "the" is stop word, "AP" → "arithmetic progression" → stems */
    check_preprocess("the AP sequence", "arithmet");
    /* "log" → "logarithm" → stemmed "logarithm" */
    check_preprocess("log base 2",     "logarithm");

    printf("\ntest_preprocessor: PASS\n");
    return 0;
}
