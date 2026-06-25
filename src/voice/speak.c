/*
 * speak.c — EduOS libeduos
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 *
 * Phase 5 — not yet implemented.
 * See AI_ENGINE.md for the full roadmap.
 */

#include "eduos/eduos.h"

uint8_t *eduos_speak(eduos_session_t *session __attribute__((unused)),
                     const char      *text    __attribute__((unused)),
                     size_t          *out_len)
{
    if (out_len) *out_len = 0;
    return NULL; /* Piper TTS integration — Phase 5 */
}
