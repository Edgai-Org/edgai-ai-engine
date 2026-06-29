/*
 * signals.c — EduOS libedgai
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 *
 * Phase 6 — D-Bus compositor signal not yet implemented.
 * Session age_mode update is live in all phases.
 */

#include "edgai/edgai.h"

void edgai_set_mode(EdgaiSession *session, EdgaiAgeMode mode)
{
    if (!session) return;
    session->age_mode = mode;
    /* TODO Phase 6: fire org.edgai.AgeModeChanged D-Bus signal to compositor */
}
