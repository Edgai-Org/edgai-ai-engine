/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 *
 * model_loader.c — RAM detection and GGUF model path resolution.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

#include "eduos/eduos_types.h"

/*
 * Detect total system RAM in kilobytes.
 * Linux/Android: reads /proc/meminfo.
 * macOS/iOS:     uses sysctl hw.memsize.
 * Returns -1 on failure (caller should default to high tier).
 */
long long eduos_detect_ram_kb(void)
{
#ifdef __APPLE__
    long long hw_memsize = 0;
    size_t len = sizeof(hw_memsize);
    if (sysctlbyname("hw.memsize", &hw_memsize, &len, NULL, 0) == 0)
        return hw_memsize / 1024LL;
    return -1;
#else
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f)
        return -1;

    char line[256];
    long long kb = -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            kb = atoll(line + 9);
            break;
        }
    }
    fclose(f);
    return kb;
#endif
}

/*
 * Resolve GGUF model file path.
 *
 * Search order:
 *   1. EDUOS_MODELS_DIR env var (developer override on any platform)
 *   2. /usr/share/eduos/models/ (production EduOS installation)
 *   3. ~/.eduos/models/ (user-level fallback)
 *
 * Returns heap-allocated path string on success; caller must free.
 * Returns NULL if not found in any location.
 */
char *eduos_find_model_path(const char *filename)
{
    if (!filename)
        return NULL;

    char path[4096];

    /* 1 — EDUOS_MODELS_DIR env var */
    const char *models_dir = getenv("EDUOS_MODELS_DIR");
    if (models_dir) {
        snprintf(path, sizeof(path), "%s/%s", models_dir, filename);
        FILE *f = fopen(path, "rb");
        if (f) { fclose(f); return strdup(path); }
    }

    /* 2 — production installation path */
    snprintf(path, sizeof(path), "/usr/share/eduos/models/%s", filename);
    {
        FILE *f = fopen(path, "rb");
        if (f) { fclose(f); return strdup(path); }
    }

    /* 3 — user-level fallback */
    const char *home = getenv("HOME");
    if (home) {
        snprintf(path, sizeof(path), "%s/.eduos/models/%s", home, filename);
        FILE *f = fopen(path, "rb");
        if (f) { fclose(f); return strdup(path); }
    }

    return NULL;
}
