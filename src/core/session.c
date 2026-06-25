/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>

#include "eduos/eduos.h"

#define RAG_SOCK_PATH "/tmp/eduos_rag.sock"

/* forward declaration — implemented in profile.c */
eduos_age_mode_t eduos_profile_read_age_mode(const char *profile_path);

static eduos_ram_tier_t detect_ram_tier(void)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f)
        return EDUOS_RAM_TIER_HIGH; /* not Linux or unreadable — assume high */

    char line[256];
    long long kb = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            kb = atoll(line + 9);
            break;
        }
    }
    fclose(f);

    if (kb < 2LL * 1024 * 1024) return EDUOS_RAM_TIER_LOW;
    if (kb < 4LL * 1024 * 1024) return EDUOS_RAM_TIER_MID;
    return EDUOS_RAM_TIER_HIGH;
}

static int open_socket(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, RAG_SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

eduos_session_t *eduos_init(const char *profile_path)
{
    eduos_session_t *session = calloc(1, sizeof(eduos_session_t));
    if (!session)
        return NULL;

    snprintf(session->session_id, sizeof(session->session_id),
             "%08lx%08x", (unsigned long)time(NULL), (unsigned int)getpid());

    session->ram_tier  = detect_ram_tier();
    session->age_mode  = eduos_profile_read_age_mode(profile_path);
    session->socket_fd = open_socket(); /* -1 if engine.py not running — not fatal */
    session->llm_ctx   = NULL;

    return session;
}

void eduos_destroy(eduos_session_t *session)
{
    if (!session)
        return;
    if (session->socket_fd >= 0)
        close(session->socket_fd);
    free(session);
}
