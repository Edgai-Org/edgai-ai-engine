/*
 * signals.c — EduOS libeduos
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 *
 * Phase 6 — D-Bus compositor signal not yet implemented.
 * Session age_mode update is live in all phases.
 */

#include "eduos/eduos.h"

void eduos_set_mode(eduos_session_t *session, eduos_age_mode_t mode)
{
    if (!session) return;
    session->age_mode = mode;
    /* TODO Phase 6: fire org.eduos.AgeModeChanged D-Bus signal to compositor */
}
