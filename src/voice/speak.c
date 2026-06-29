/*
 * speak.c — EduOS libedgai
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 *
 * Phase 5 — not yet implemented.
 * See AI_ENGINE.md for the full roadmap.
 */

#include "edgai/edgai.h"

uint8_t *edgai_speak(EdgaiSession *session __attribute__((unused)),
                     const char      *text    __attribute__((unused)),
                     size_t          *out_len)
{
    if (out_len) *out_len = 0;
    return NULL; /* Piper TTS integration — Phase 5 */
}
