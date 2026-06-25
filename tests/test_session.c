/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 */

#include <stdio.h>
#include <assert.h>
#include "eduos/eduos.h"

int main(void)
{
    printf("test_session: testing eduos_init and eduos_destroy\n");

    eduos_session_t *session = eduos_init(NULL);
    assert(session != NULL);

    printf("  session_id:  %s\n", session->session_id);
    printf("  ram_tier:    %d\n", session->ram_tier);
    printf("  age_mode:    %d\n", session->age_mode);
    printf("  socket_fd:   %d\n", session->socket_fd);

    eduos_destroy(session);
    printf("test_session: PASS\n");
    return 0;
}
