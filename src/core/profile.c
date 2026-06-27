/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 *
 * profile.c — reads per-user profile JSON from /etc/eduos/profiles/.
 * Updated in Phase 4 to also read the is_mobile flag.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eduos/eduos_types.h"

/* Extract a JSON integer value for the given key */
static long json_int(const char *buf, const char *key, long default_val)
{
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);

    char *pos = strstr(buf, search);
    if (!pos) return default_val;

    pos += strlen(search);
    while (*pos == ' ' || *pos == ':' || *pos == '\t') pos++;

    if (*pos == '-' || (*pos >= '0' && *pos <= '9'))
        return strtol(pos, NULL, 10);
    return default_val;
}

/* Extract a JSON boolean value for the given key */
static int json_bool(const char *buf, const char *key, int default_val)
{
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);

    char *pos = strstr(buf, search);
    if (!pos) return default_val;

    pos += strlen(search);
    while (*pos == ' ' || *pos == ':' || *pos == '\t') pos++;

    if (strncmp(pos, "true",  4) == 0) return 1;
    if (strncmp(pos, "false", 5) == 0) return 0;
    return default_val;
}

static char *load_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);

    if (sz <= 0 || sz > 65536) { fclose(f); return NULL; }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

eduos_age_mode_t eduos_profile_read_age_mode(const char *profile_path)
{
    if (!profile_path) return EDUOS_MODE_LAUNCHPAD;

    char *buf = load_file(profile_path);
    if (!buf) return EDUOS_MODE_LAUNCHPAD;

    long val = json_int(buf, "age_mode", (long)EDUOS_MODE_LAUNCHPAD);
    free(buf);

    if (val >= 0 && val <= 3)
        return (eduos_age_mode_t)val;
    return EDUOS_MODE_LAUNCHPAD;
}

/*
 * Read is_mobile from profile JSON.
 * Also checks EDUOS_IS_MOBILE=1 env var as override.
 * Returns 1 if mobile, 0 for desktop OS.
 */
int eduos_profile_read_is_mobile(const char *profile_path)
{
    const char *env = getenv("EDUOS_IS_MOBILE");
    if (env && strcmp(env, "1") == 0) return 1;

    if (!profile_path) return 0;

    char *buf = load_file(profile_path);
    if (!buf) return 0;

    int val = json_bool(buf, "is_mobile", 0);
    free(buf);
    return val;
}
