/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 *
 * Private header — not part of the public API.
 * Defines the tier config struct and selector used by session.c, llama_backend.c.
 */

#ifndef EDGAI_TIER_H
#define EDGAI_TIER_H

#include <stdbool.h>

/* ggml_type is defined in ggml.h, included transitively via llama.h.
 * Include ggml.h here only when building C files that include this header. */
#include "ggml.h"

typedef struct {
    const char    *model_filename;   /* GGUF filename under models dir */
    int            n_gpu_layers;     /* 0 = CPU only                  */
    int            n_ctx;            /* context window size           */
    int            n_batch;          /* logical batch size            */
    int            n_threads;        /* CPU thread count              */
    bool           flash_attn;       /* must be true for KV quant    */
    enum ggml_type type_k;           /* KV key cache quantization     */
    enum ggml_type type_v;           /* KV value cache quantization   */
    int            max_output_tokens;
} EdgaiTierConfig;

/*
 * Select the appropriate tier config based on detected RAM and platform.
 * Returns a pointer to a static (never NULL) config entry.
 */
const EdgaiTierConfig *edgai_tier_select(long long ram_kb, bool is_mobile);

#endif /* EDGAI_TIER_H */
