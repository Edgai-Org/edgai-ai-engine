/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 *
 * llama_backend.c — llama.cpp wrapper.
 * Pinned to commit 0ed235ea2c17a19fc8238668653946721ed136fd.
 * API note: tokenize/piece functions take llama_vocab*, not llama_model*.
 *           EOG check: llama_vocab_is_eog(vocab, token).
 *           KV reset: llama_memory_clear(llama_get_memory(ctx), false).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "llama.h"
#include "eduos/eduos.h"
#include "eduos_tier.h"

#define MODEL_NOT_LOADED_MSG "Model not loaded. Please download the model file."

/*
 * Post-process the raw generation buffer in place:
 *   1. Truncate at the first <| — catches any special token that leaked
 *      through as partial characters (e.g. <| and end|> as separate pieces).
 *   2. Remove any line that starts with \( or ends with \) — LaTeX leak guard.
 *   3. Strip trailing whitespace.
 */
static void strip_output(char *text)
{
    /* 1 — truncate at first <| */
    char *cut = strstr(text, "<|");
    if (cut) *cut = '\0';

    /* 2 — remove LaTeX lines in place */
    char *rd = text;
    char *wr = text;
    while (*rd) {
        char *line = rd;
        while (*rd && *rd != '\n') rd++;
        size_t len = (size_t)(rd - line);

        int skip = 0;
        if (len >= 2 && line[0] == '\\' && line[1] == '(') skip = 1;
        if (!skip && len >= 2 && line[len-1] == ')' && line[len-2] == '\\') skip = 1;

        if (!skip) {
            if (wr != line) memmove(wr, line, len);
            wr += len;
            if (*rd == '\n') *wr++ = '\n';
        }
        if (*rd == '\n') rd++;
    }
    *wr = '\0';

    /* 3 — strip trailing whitespace */
    size_t n = strlen(text);
    while (n > 0 && (unsigned char)text[n - 1] <= ' ') text[--n] = '\0';
}

/*
 * Internal: run inference on a complete prompt string.
 * Clears memory (KV cache) before each call — fully stateless per inference.
 * Returns heap-allocated output string. Caller must free. NULL on error.
 */
static char *eduos_llm_infer(eduos_session_t *session,
                              const char *prompt,
                              int max_output_tokens)
{
    if (!session->llm_model || !session->llm_ctx || !session->llm_sampler)
        return strdup(MODEL_NOT_LOADED_MSG);

    struct llama_model   *model = session->llm_model;
    struct llama_context *ctx   = session->llm_ctx;
    struct llama_sampler *smpl  = session->llm_sampler;
    const struct llama_vocab *vocab = llama_model_get_vocab(model);

    /* Reset KV cache and sampler before new prompt */
    llama_memory_clear(llama_get_memory(ctx), false);
    llama_sampler_reset(smpl);

    /* Tokenise prompt — first call with NULL buffer returns needed count */
    int n_prompt = -llama_tokenize(
        vocab, prompt, (int32_t)strlen(prompt),
        NULL, 0, true, true
    );
    if (n_prompt <= 0)
        return NULL;

    llama_token *tokens = malloc((size_t)n_prompt * sizeof(llama_token));
    if (!tokens) return NULL;

    llama_tokenize(vocab, prompt, (int32_t)strlen(prompt),
                   tokens, n_prompt, true, true);

    /* Process the full prompt batch */
    struct llama_batch batch = llama_batch_get_one(tokens, n_prompt);
    if (llama_decode(ctx, batch) != 0) {
        free(tokens);
        return NULL;
    }
    free(tokens);

    /* Autoregressive generation loop */
    char *output = calloc(1, 8192);
    if (!output) return NULL;
    size_t out_len = 0;

    for (int i = 0; i < max_output_tokens; i++) {
        llama_token token = llama_sampler_sample(smpl, ctx, -1);

        if (llama_vocab_is_eog(vocab, token))
            break;

        char piece[64];
        int n = llama_token_to_piece(vocab, token, piece, sizeof(piece) - 1, 0, true);
        if (n <= 0) break;
        piece[n] = '\0';

        /* Belt-and-suspenders: llama_vocab_is_eog() may not cover every
         * model family's end-of-turn token in this pinned llama.cpp commit.
         * Stop before appending if the rendered piece is a chat delimiter. */
        if (strcmp(piece, "<|end|>")      == 0 ||
            strcmp(piece, "<|im_end|>")   == 0 ||
            strcmp(piece, "<|endoftext|>") == 0 ||
            strcmp(piece, "</s>")         == 0)
            break;

        if (out_len + (size_t)n + 1 > 8191) break;
        memcpy(output + out_len, piece, (size_t)n);
        out_len += (size_t)n;
        output[out_len] = '\0';

        struct llama_batch next = llama_batch_get_one(&token, 1);
        if (llama_decode(ctx, next) != 0) break;
    }

    strip_output(output);
    return output;
}

/*
 * Concept explanation — LLM call type 1.
 * Fires at CONCEPT state. Prompt ≤ 300 input tokens.
 * No conversation history. No few-shot examples.
 */
char *eduos_llm_explain_concept(eduos_session_t *session,
                                const char *question_text)
{
    if (!session->llm_model)
        return strdup(MODEL_NOT_LOADED_MSG);
    if (!question_text)
        return NULL;

    static const char *age_label[] = {
        "PLAYGROUND", "EXPLORER", "LAUNCHPAD", "PROFESSIONAL"
    };
    const char *mode = age_label[(int)session->age_mode];

    long long ram_kb = 4LL * 1024 * 1024;
    const eduos_tier_config_t *tier = eduos_tier_select(ram_kb, session->is_mobile);
    int max_tok = tier->max_output_tokens > 512 ? 512 : tier->max_output_tokens;

    char prompt[2048];
    snprintf(prompt, sizeof(prompt),
        "<|system|>\n"
        "You are an EduOS tutor for Nigerian secondary school students.\n"
        "Age mode: %s.\n"
        "Do not use LaTeX. Do not use backslash notation. "
        "Write all math in plain text: write 'log base 2 of x = 5' not '\\log_2(x) = 5'.\n"
        "Be concise. Maximum 3 sentences.\n"
        "<|end|>\n"
        "<|user|>\n"
        "Question: %s\n"
        "Explain the concept tested by this question simply.\n"
        "<|end|>\n"
        "<|assistant|>\n",
        mode, question_text
    );

    return eduos_llm_infer(session, prompt, max_tok);
}

/*
 * Rephrase — LLM call type 2.
 * Fires at STEPS state when RE_EXPLAIN intent detected.
 * max_output_tokens hard-capped at 150.
 */
char *eduos_llm_rephrase(eduos_session_t *session,
                          const char *previous_explanation,
                          const char *student_message)
{
    if (!session->llm_model)
        return strdup(MODEL_NOT_LOADED_MSG);
    if (!previous_explanation || !student_message)
        return NULL;

    static const char *age_label[] = {
        "PLAYGROUND", "EXPLORER", "LAUNCHPAD", "PROFESSIONAL"
    };
    const char *mode = age_label[(int)session->age_mode];

    char prompt[2048];
    snprintf(prompt, sizeof(prompt),
        "<|system|>\n"
        "You are an EduOS tutor for Nigerian secondary school students.\n"
        "Age mode: %s.\n"
        "Do not use LaTeX. Do not use backslash notation. "
        "Write all math in plain text: write 'log base 2 of x = 5' not '\\log_2(x) = 5'.\n"
        "Rephrase the explanation below in simpler terms. Maximum 2 sentences.\n"
        "<|end|>\n"
        "<|user|>\n"
        "Original explanation: %s\n"
        "Student said: %s\n"
        "Rephrase more simply.\n"
        "<|end|>\n"
        "<|assistant|>\n",
        mode, previous_explanation, student_message
    );

    return eduos_llm_infer(session, prompt, 150);
}

