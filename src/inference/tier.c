/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 *
 * tier.c — RAM-based model tier selection.
 * Tier map is locked — model filenames match bartowski HuggingFace GGUF repos.
 * Do not change filenames or RAM thresholds without updating docs.
 */

#include <stdio.h>
#include <stdlib.h>

#include "edgai_tier.h"

/* RAM thresholds in KB */
#define TIER_2GB_KB        (2LL * 1024 * 1024)
#define TIER_4GB_KB        (4LL * 1024 * 1024)
#define MOBILE_TIER_2GB_KB (2LL * 1024 * 1024)
#define MOBILE_TIER_4GB_KB (4LL * 1024 * 1024)
#define MOBILE_TIER_6GB_KB (6LL * 1024 * 1024)

/* OS tiers — controlled minimal OS overhead, no Android/iOS overhead */
static const EdgaiTierConfig OS_TIERS[] = {
    /* 2 GB OS — Qwen 1.5B, tight config, all KV optimizations on */
    {
        "qwen2.5-1.5b-instruct-q4_k_m.gguf",
        0, 512, 128, 2, true,
        GGML_TYPE_Q4_0, GGML_TYPE_Q4_0, 200
    },
    /* 4 GB OS — Phi-3 Mini */
    {
        "phi-3-mini-4k-instruct-q4_k_m.gguf",
        0, 2048, 256, 4, true,
        GGML_TYPE_Q8_0, GGML_TYPE_Q8_0, 512
    },
    /* 8 GB+ OS (school server) — Llama 3 8B */
    {
        "llama-3-8b-instruct-q4_k_m.gguf",
        0, 4096, 512, 8, true,
        GGML_TYPE_F16, GGML_TYPE_F16, 1024
    },
};

/* Mobile tiers — Android/iOS OS eats 2-4 GB, so smaller models are used */
static const EdgaiTierConfig MOBILE_TIERS[] = {
    /* 2 GB mobile — only 0.5B fits after Android/iOS overhead */
    {
        "qwen2.5-0.5b-instruct-iq4_xs.gguf",
        0, 512, 64, 1, true,
        GGML_TYPE_Q4_0, GGML_TYPE_Q4_0, 150
    },
    /* 4 GB mobile — 1.5B with tight config (Phi-3 won't fit after mobile OS) */
    {
        "qwen2.5-1.5b-instruct-q4_k_m.gguf",
        0, 512, 128, 2, true,
        GGML_TYPE_Q4_0, GGML_TYPE_Q4_0, 200
    },
    /* 6 GB+ mobile — Phi-3 fits comfortably */
    {
        "phi-3-mini-4k-instruct-q4_k_m.gguf",
        0, 2048, 256, 4, true,
        GGML_TYPE_Q8_0, GGML_TYPE_Q8_0, 512
    },
};

const EdgaiTierConfig *edgai_tier_select(long long ram_kb, bool is_mobile)
{
    /* Normal RAM-based selection — used for all parameters except model_filename
     * when EDGAI_MODEL_OVERRIDE is set. */
    const EdgaiTierConfig *base;
    if (is_mobile) {
        if (ram_kb < MOBILE_TIER_2GB_KB) base = &MOBILE_TIERS[0];
        else if (ram_kb < MOBILE_TIER_4GB_KB) base = &MOBILE_TIERS[1];
        else base = &MOBILE_TIERS[2];
    } else {
        if (ram_kb < TIER_2GB_KB) base = &OS_TIERS[0];
        else if (ram_kb < TIER_4GB_KB) base = &OS_TIERS[1];
        else base = &OS_TIERS[2];
    }

    /* EDGAI_MODEL_OVERRIDE — developer escape hatch.
     * Keeps RAM-tier context/batch/thread settings but loads the specified file. */
    const char *override = getenv("EDGAI_MODEL_OVERRIDE");
    if (override && override[0] != '\0') {
        static EdgaiTierConfig override_tier;
        override_tier              = *base;       /* copy all numeric fields */
        override_tier.model_filename = override;  /* swap in the override name */
        fprintf(stderr, "edgai: EDGAI_MODEL_OVERRIDE active — using '%s'\n", override);
        return &override_tier;
    }

    return base;
}
