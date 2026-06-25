/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eduos/eduos_types.h"

eduos_age_mode_t eduos_profile_read_age_mode(const char *profile_path)
{
    if (!profile_path)
        return EDUOS_MODE_LAUNCHPAD;

    FILE *f = fopen(profile_path, "r");
    if (!f)
        return EDUOS_MODE_LAUNCHPAD;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);

    if (sz <= 0 || sz > 65536) {
        fclose(f);
        return EDUOS_MODE_LAUNCHPAD;
    }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return EDUOS_MODE_LAUNCHPAD;
    }

    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = '\0';
    fclose(f);

    eduos_age_mode_t mode = EDUOS_MODE_LAUNCHPAD;

    char *key = strstr(buf, "\"age_mode\"");
    if (key) {
        char *colon = strchr(key + 10, ':');
        if (colon) {
            long val = strtol(colon + 1, NULL, 10);
            if (val >= 0 && val <= 3)
                mode = (eduos_age_mode_t)val;
        }
    }

    free(buf);
    return mode;
}
