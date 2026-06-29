/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 *
 * session.c — edgai_init() and edgai_destroy().
 * Phase 4: loads llama.cpp model, opens curriculum DB.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sqlite3.h>
#include "llama.h"
#include "edgai/edgai.h"
#include "inference/edgai_tier.h"

/* Forward declarations from other translation units */
EdgaiAgeMode  edgai_profile_read_age_mode(const char *profile_path);
int               edgai_profile_read_is_mobile(const char *profile_path);
long long         edgai_detect_ram_kb(void);
char             *edgai_find_model_path(const char *filename);

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
    fprintf(stderr, "edgai: cwd = %s\n",
            getcwd(cwd, sizeof(cwd)) ? cwd : "(getcwd failed)");

    /* 1 — EDGAI_DB_PATH env var */
    const char *env = getenv("EDGAI_DB_PATH");
    if (env) {
        fprintf(stderr, "edgai: trying DB path: %s\n", env);
        if (sqlite3_open_v2(env, &db, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK) {
            fprintf(stderr, "edgai: DB opened via EDGAI_DB_PATH: %s\n", env);
            return db;
        }
        if (db) { sqlite3_close(db); db = NULL; }
        fprintf(stderr, "edgai: DB not found at EDGAI_DB_PATH=%s\n", env);
    }

    /* 2 — production installation */
    snprintf(path, sizeof(path), "/usr/share/edgai/curriculum/mathematics.db");
    fprintf(stderr, "edgai: trying DB path: %s\n", path);
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK) {
        fprintf(stderr, "edgai: DB opened: %s\n", path);
        return db;
    }
    if (db) { sqlite3_close(db); db = NULL; }

    /* 3 — dev fallback: repo-relative demo DB (run from repo root) */
    snprintf(path, sizeof(path), "db/demo/demo_curriculum.db");
    fprintf(stderr, "edgai: trying DB path: %s\n", path);
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) == SQLITE_OK) {
        fprintf(stderr, "edgai: DB opened: %s\n", path);
        return db;
    }
    if (db) { sqlite3_close(db); db = NULL; }

    fprintf(stderr,
        "edgai: DB not found — LLM-only mode\n"
        "       Fix: set EDGAI_DB_PATH or run from repo root (not build/)\n");
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

EdgaiSession *edgai_init(const char *profile_path)
{
    ensure_backend();

    EdgaiSession *session = calloc(1, sizeof(EdgaiSession));
    if (!session) return NULL;

    snprintf(session->session_id, sizeof(session->session_id),
             "%08lx%08x", (unsigned long)time(NULL), (unsigned int)getpid());

    session->age_mode   = edgai_profile_read_age_mode(profile_path);
    session->is_mobile  = (bool)edgai_profile_read_is_mobile(profile_path);
    session->socket_fd  = -1;
    session->current_state = EDGAI_STATE_CONCEPT;

    /* Detect RAM and select tier */
    long long ram_kb = edgai_detect_ram_kb();
    if (ram_kb < 0) ram_kb = 4LL * 1024 * 1024; /* assume mid on error */

    if (ram_kb < 2LL * 1024 * 1024)      session->ram_tier = EDGAI_RAM_TIER_LOW;
    else if (ram_kb < 4LL * 1024 * 1024) session->ram_tier = EDGAI_RAM_TIER_MID;
    else                                   session->ram_tier = EDGAI_RAM_TIER_HIGH;

    const EdgaiTierConfig *tier = edgai_tier_select(ram_kb, session->is_mobile);

    /* Locate GGUF model file */
    char *model_path = edgai_find_model_path(tier->model_filename);
    if (!model_path) {
        fprintf(stderr, "edgai: model not found: %s — LLM disabled\n",
                tier->model_filename);
        /* Session still works — DB-driven states will function */
        session->db = open_curriculum_db();
        edgai_voice_init(session);
        return session;
    }

    /* Load model */
    struct llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = tier->n_gpu_layers;

    session->llm_model = llama_model_load_from_file(model_path, mparams);
    free(model_path);

    if (!session->llm_model) {
        fprintf(stderr, "edgai: failed to load model — LLM disabled\n");
        session->db = open_curriculum_db();
        edgai_voice_init(session);
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
        fprintf(stderr, "edgai: failed to create llama context — LLM disabled\n");
        llama_model_free(session->llm_model);
        session->llm_model = NULL;
        session->db = open_curriculum_db();
        edgai_voice_init(session);
        return session;
    }

    session->llm_sampler = build_sampler();
    session->db          = open_curriculum_db();

    edgai_voice_init(session);

    return session;
}

void edgai_destroy(EdgaiSession *session)
{
    if (!session) return;

    edgai_voice_destroy(session);

    if (session->llm_sampler) llama_sampler_free(session->llm_sampler);
    if (session->llm_ctx)     llama_free(session->llm_ctx);
    if (session->llm_model)   llama_model_free(session->llm_model);
    if (session->db)          sqlite3_close(session->db);

#ifdef EDGAI_SOCKET_FALLBACK
    if (session->socket_fd >= 0) close(session->socket_fd);
#endif

    free(session);
}

/* edgai_set_mode is defined in src/dbus/signals.c (Phase 6 D-Bus stub lives there) */

/* ── Phase 5 voice init/destroy wired into session lifecycle ────────────── */

#ifdef EDGAI_VOICE_ENABLED
#include "../voice/transcribe.h"
#include "../voice/speak.h"
#endif

/*
 * Called at the tail of edgai_init() after the LLM and DB are ready.
 * On success, session->voice_enabled is set to 1.
 * On failure (missing models), voice is silently disabled — text-only mode.
 */
void edgai_voice_init(EdgaiSession *session)
{
    if (!session) return;
#ifdef EDGAI_VOICE_ENABLED
    /* Transcribe init: loads Vosk model on 4 GB+ tier */
    edgai_transcribe_init(session);
    /* Speak init: loads Piper model on 4 GB+ tier */
    edgai_speak_init(session);
    /* Both must succeed for voice to be enabled */
    if (!session->vosk_model && !session->is_mobile)
        session->voice_enabled = 0;
#else
    session->voice_enabled = 0;
#endif
}

/* Called at the head of edgai_destroy() before LLM teardown. */
void edgai_voice_destroy(EdgaiSession *session)
{
    if (!session) return;
#ifdef EDGAI_VOICE_ENABLED
    edgai_speak_destroy(session);
    edgai_transcribe_destroy(session);
#endif
}
