/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 *
 * test_inference.c — Phase 4: end-to-end LLM inference test.
 *
 * Requires a GGUF model in EDUOS_MODELS_DIR, /usr/share/eduos/models/, or ~/.eduos/models/.
 * If no model is found, prints a warning and exits 0 (not a build failure).
 * Asserts: response is non-empty and contains no LaTeX (no $ or \frac).
 */

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "eduos/eduos.h"

int main(void)
{
    printf("test_inference: end-to-end LLM inference\n");

    eduos_session_t *session = eduos_init(NULL);
    assert(session != NULL);

    if (!session->llm_model) {
        printf("  WARNING: no model file found in search paths.\n");
        printf("  Download a GGUF model and set EDUOS_MODELS_DIR to run this test.\n");
        printf("  Suggested: qwen2.5-1.5b-instruct-q4_k_m.gguf\n");
        printf("  (bartowski/Qwen2.5-1.5B-Instruct-GGUF on HuggingFace)\n");
        eduos_destroy(session);
        printf("test_inference: SKIPPED (no model)\n");
        return 0;
    }

    printf("  model loaded — running inference\n");

    /* Send a question that triggers CONCEPT state + LLM explain */
    eduos_response_t *resp = eduos_query(session, "what is logarithm");
    assert(resp != NULL);
    assert(resp->error == NULL);
    assert(resp->text != NULL);
    assert(strlen(resp->text) > 0);

    printf("  response length: %zu chars\n", strlen(resp->text));
    printf("  first 200 chars: %.200s\n", resp->text);

    /* Assert no LaTeX — the prompt explicitly forbids $ and \frac */
    assert(strchr(resp->text, '$') == NULL);
    assert(strstr(resp->text, "\\frac") == NULL);

    eduos_response_free(resp);
    eduos_destroy(session);
    printf("test_inference: PASS\n");
    return 0;
}
