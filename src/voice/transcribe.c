/*
 * transcribe.c — EduOS libedgai
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 *
 * Phase 5 — not yet implemented.
 * See AI_ENGINE.md for the full roadmap.
 */

#include "edgai/edgai.h"

char *edgai_transcribe(EdgaiSession *session __attribute__((unused)),
                       const uint8_t   *audio   __attribute__((unused)),
                       size_t           len     __attribute__((unused)))
{
    return NULL; /* whisper.cpp integration — Phase 5 */
}
