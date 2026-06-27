/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 *
 * session.c — eduos_init() and eduos_destroy().
 * Phase 4: loads llama.cpp model, opens curriculum DB.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sqlite3.h>
#include "llama.h"
#include "eduos/eduos.h"
#include "inference/eduos_tier.h"

/* Forward declarations from other translation units */
eduos_age_mode_t  eduos_profile_read_age_mode(const char *profile_path);
int               eduos_profile_read_is_mobile(const char *profile_path);
long long         eduos_detect_ram_kb(void);
char             *eduos_find_model_path(const char *filename);

/* ── llama.cpp global backend — initialised once per process ────────────── */

static int g_backend_initialized = 0;

static void ensure_backend(void)
{
    if (!g_backend_initialized) {
        llama_backend_init();
        g_backend_initialized = 1;
    }
}

/* ── DB path resolution ─────────────────────────────────────────────────── */

static sqlite3 *open_curriculum_db(void)
{
    char path[4096];
    sqlite3 *db = NULL;

    char cwd[4096];
    fprintf(stderr, "eduos: cwd = %s\n",
            getcwd(cwd, sizeof(cwd)) ? cwd : "(getcwd failed)");

    /* 1 — EDUOS_DB_PATH env var */
    const char *env = getenv("EDUOS_DB_PATH");
    if (env) {
        fprintf(stderr, "eduos: trying DB path: %s\n", env);
        if (sqlite3_open_v2(env, &db, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK) {
            fprintf(stderr, "eduos: DB opened via EDUOS_DB_PATH: %s\n", env);
            return db;
        }
        if (db) { sqlite3_close(db); db = NULL; }
        fprintf(stderr, "eduos: DB not found at EDUOS_DB_PATH=%s\n", env);
    }

    /* 2 — production installation */
    snprintf(path, sizeof(path), "/usr/share/eduos/curriculum/mathematics.db");
    fprintf(stderr, "eduos: trying DB path: %s\n", path);
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK) {
        fprintf(stderr, "eduos: DB opened: %s\n", path);
        return db;
    }
    if (db) { sqlite3_close(db); db = NULL; }

    /* 3 — dev fallback: repo-relative demo DB (run from repo root) */
    snprintf(path, sizeof(path), "db/demo/demo_curriculum.db");
    fprintf(stderr, "eduos: trying DB path: %s\n", path);
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK) {
        fprintf(stderr, "eduos: DB opened: %s\n", path);
        return db;
    }
    if (db) { sqlite3_close(db); db = NULL; }

    fprintf(stderr,
        "eduos: DB not found — LLM-only mode\n"
        "       Fix: set EDUOS_DB_PATH or run from repo root (not build/)\n");
    return NULL;
}

/* ── Sampler chain ──────────────────────────────────────────────────────── */

static struct llama_sampler *build_sampler(void)
{
    struct llama_sampler_chain_params sparams = llama_sampler_chain_default_params();
    struct llama_sampler *smpl = llama_sampler_chain_init(sparams);

    llama_sampler_chain_add(smpl, llama_sampler_init_penalties(
        64,    /* penalty_last_n  */
        1.1f,  /* penalty_repeat  */
        0.0f,  /* penalty_freq    */
        0.0f   /* penalty_present */
    ));
    llama_sampler_chain_add(smpl, llama_sampler_init_min_p(0.05f, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(0.4f));
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    return smpl;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

eduos_session_t *eduos_init(const char *profile_path)
{
    ensure_backend();

    eduos_session_t *session = calloc(1, sizeof(eduos_session_t));
    if (!session) return NULL;

    snprintf(session->session_id, sizeof(session->session_id),
             "%08lx%08x", (unsigned long)time(NULL), (unsigned int)getpid());

    session->age_mode   = eduos_profile_read_age_mode(profile_path);
    session->is_mobile  = (bool)eduos_profile_read_is_mobile(profile_path);
    session->socket_fd  = -1;
    session->current_state = EDUOS_STATE_CONCEPT;

    /* Detect RAM and select tier */
    long long ram_kb = eduos_detect_ram_kb();
    if (ram_kb < 0) ram_kb = 4LL * 1024 * 1024; /* assume mid on error */

    if (ram_kb < 2LL * 1024 * 1024)      session->ram_tier = EDUOS_RAM_TIER_LOW;
    else if (ram_kb < 4LL * 1024 * 1024) session->ram_tier = EDUOS_RAM_TIER_MID;
    else                                   session->ram_tier = EDUOS_RAM_TIER_HIGH;

    const eduos_tier_config_t *tier = eduos_tier_select(ram_kb, session->is_mobile);

    /* Locate GGUF model file */
    char *model_path = eduos_find_model_path(tier->model_filename);
    if (!model_path) {
        fprintf(stderr, "eduos: model not found: %s — LLM disabled\n",
                tier->model_filename);
        /* Session still works — DB-driven states will function */
        session->db = open_curriculum_db();
        return session;
    }

    /* Load model */
    struct llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = tier->n_gpu_layers;

    session->llm_model = llama_model_load_from_file(model_path, mparams);
    free(model_path);

    if (!session->llm_model) {
        fprintf(stderr, "eduos: failed to load model — LLM disabled\n");
        session->db = open_curriculum_db();
        return session;
    }

    /* Create inference context */
    struct llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx        = (uint32_t)tier->n_ctx;
    cparams.n_batch      = (uint32_t)tier->n_batch;
    cparams.n_threads    = (int32_t)tier->n_threads;
    /* flash_attn is an enum in this llama.cpp version, not a bool */
    cparams.flash_attn_type = tier->flash_attn
                              ? LLAMA_FLASH_ATTN_TYPE_ENABLED
                              : LLAMA_FLASH_ATTN_TYPE_DISABLED;
    cparams.type_k       = tier->type_k;
    cparams.type_v       = tier->type_v;

    session->llm_ctx = llama_init_from_model(session->llm_model, cparams);
    if (!session->llm_ctx) {
        fprintf(stderr, "eduos: failed to create llama context — LLM disabled\n");
        llama_model_free(session->llm_model);
        session->llm_model = NULL;
        session->db = open_curriculum_db();
        return session;
    }

    session->llm_sampler = build_sampler();
    session->db          = open_curriculum_db();

    return session;
}

void eduos_destroy(eduos_session_t *session)
{
    if (!session) return;

    if (session->llm_sampler) llama_sampler_free(session->llm_sampler);
    if (session->llm_ctx)     llama_free(session->llm_ctx);
    if (session->llm_model)   llama_model_free(session->llm_model);
    if (session->db)          sqlite3_close(session->db);

#ifdef EDUOS_SOCKET_FALLBACK
    if (session->socket_fd >= 0) close(session->socket_fd);
#endif

    free(session);
}

/* eduos_set_mode is defined in src/dbus/signals.c (Phase 6 D-Bus stub lives there) */
