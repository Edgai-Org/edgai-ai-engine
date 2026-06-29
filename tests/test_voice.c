/*
 * test_voice.c — EduOS libedgai Phase 5 voice pipeline tests
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2024 EduOS-Org
 *
 * Tests are grouped by subsystem:
 *   T1–T8   sanitize.c   — LLM → TTS text cleaning
 *   T9–T12  vad.c        — energy-based Voice Activity Detection
 *   T13     session      — voice fields initialised correctly
 *
 * Audio hardware tests (transcribe/speak) require EDGAI_AUDIO_TEST=1
 * and the Vosk/Piper models installed. They are skipped otherwise so
 * this test suite passes cleanly in CI without a sound card.
 *
 * WAV fixture generation:
 *   make_silence_wav() writes a minimal 16 kHz S16_LE mono WAV file
 *   filled with zeros to tests/fixtures/voice/silence_1s.wav.
 *   This is used by audio integration tests when EDGAI_AUDIO_TEST=1.
 */

/* 1. System includes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

/* 2. Third-party includes — none */

/* 3. Local includes */
#include "edgai/edgai.h"
#include "../src/voice/sanitize.h"
#include "../src/voice/vad.h"

/* 4. Test helpers */

#define PASS(name) \
    do { printf("  PASS  %s\n", (name)); pass_count++; } while (0)
#define FAIL(name, msg) \
    do { fprintf(stderr, "  FAIL  %s: %s\n", (name), (msg)); \
         fail_count++; } while (0)
#define ASSERT_EQ_INT(name, got, expected) \
    do { if ((got) == (expected)) { PASS(name); } \
         else { char _buf[128]; \
                snprintf(_buf, sizeof(_buf), "got %d, expected %d", \
                         (int)(got), (int)(expected)); \
                FAIL(name, _buf); } } while (0)
#define ASSERT_STR_EQ(name, got, expected) \
    do { if (strcmp((got), (expected)) == 0) { PASS(name); } \
         else { char _buf[256]; \
                snprintf(_buf, sizeof(_buf), "got '%s', expected '%s'", \
                         (got), (expected)); \
                FAIL(name, _buf); } } while (0)
#define ASSERT_STR_CONTAINS(name, haystack, needle) \
    do { if (strstr((haystack), (needle))) { PASS(name); } \
         else { char _buf[256]; \
                snprintf(_buf, sizeof(_buf), \
                         "'%s' not found in '%s'", (needle), (haystack)); \
                FAIL(name, _buf); } } while (0)
#define ASSERT_STR_NOT_CONTAINS(name, haystack, needle) \
    do { if (!strstr((haystack), (needle))) { PASS(name); } \
         else { char _buf[256]; \
                snprintf(_buf, sizeof(_buf), \
                         "'%s' unexpectedly found in '%s'", \
                         (needle), (haystack)); \
                FAIL(name, _buf); } } while (0)

static int pass_count = 0;
static int fail_count = 0;

/* Generate n samples of silence (all zeros) */
static void make_silence_frame(int16_t *buf, size_t n)
{
    memset(buf, 0, n * sizeof(int16_t));
}

/* Generate n samples above the speech threshold (value = threshold + 1000) */
static void make_speech_frame(int16_t *buf, size_t n)
{
    int16_t val = (int16_t)(EDGAI_VAD_SPEECH_THRESHOLD + 1000);
    for (size_t i = 0; i < n; i++)
        buf[i] = (i & 1) ? val : (int16_t)-val; /* alternating so RMS ≈ val */
}

/*
 * Write a minimal PCM WAV file to path.
 * Format: 16 kHz, S16_LE, mono, duration_ms milliseconds of silence.
 * Returns 0 on success, -1 on failure.
 */
static int make_silence_wav(const char *path, int duration_ms)
{
    int sample_rate   = 16000;
    int channels      = 1;
    int bits          = 16;
    int n_samples     = sample_rate * duration_ms / 1000;
    int data_bytes    = n_samples * channels * (bits / 8);

    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    /* RIFF header */
    uint32_t chunk_size    = (uint32_t)(36 + data_bytes);
    uint32_t subchunk2_size = (uint32_t)data_bytes;
    uint16_t audio_format  = 1; /* PCM */
    uint16_t num_channels  = (uint16_t)channels;
    uint32_t byte_rate     = (uint32_t)(sample_rate * channels * bits / 8);
    uint16_t block_align   = (uint16_t)(channels * bits / 8);
    uint16_t bits_per_smp  = (uint16_t)bits;
    uint32_t subchunk1_size = 16;

#define WR4(v) do { uint32_t _v = (v); fwrite(&_v, 4, 1, f); } while (0)
#define WR2(v) do { uint16_t _v = (v); fwrite(&_v, 2, 1, f); } while (0)

    fwrite("RIFF", 1, 4, f);
    WR4(chunk_size);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    WR4(subchunk1_size);
    WR2(audio_format);
    WR2(num_channels);
    WR4((uint32_t)sample_rate);
    WR4(byte_rate);
    WR2(block_align);
    WR2(bits_per_smp);
    fwrite("data", 1, 4, f);
    WR4(subchunk2_size);

    /* Write silence */
    for (int i = 0; i < n_samples; i++) {
        int16_t s = 0;
        fwrite(&s, sizeof(s), 1, f);
    }

#undef WR4
#undef WR2

    fclose(f);
    return 0;
}

/* ── Sanitize tests (T1–T8) ─────────────────────────────────────────────── */

static void test_sanitize_display_math(void)
{
    char out[512];
    int n = edgai_sanitize_for_tts("The area is $$E = mc^2$$ for all cases.",
                                    out, sizeof(out));
    (void)n;
    ASSERT_STR_NOT_CONTAINS("T1_sanitize_display_math_stripped", out, "$$");
    ASSERT_STR_CONTAINS("T1_sanitize_display_math_kept_text",
                         out, "The area is");
    ASSERT_STR_CONTAINS("T1_sanitize_display_math_kept_end",
                         out, "for all cases");
}

static void test_sanitize_inline_math(void)
{
    char out[512];
    edgai_sanitize_for_tts("The value $x = 5$ is the answer.", out, sizeof(out));
    ASSERT_STR_NOT_CONTAINS("T2_sanitize_inline_math_stripped", out, "$");
    ASSERT_STR_CONTAINS("T2_sanitize_inline_math_kept_prefix",
                         out, "The value");
    ASSERT_STR_CONTAINS("T2_sanitize_inline_math_kept_suffix",
                         out, "is the answer");
}

static void test_sanitize_latex_commands(void)
{
    char out[512];
    edgai_sanitize_for_tts("Simplify \\frac{a}{b} using \\sqrt{c}.",
                             out, sizeof(out));
    ASSERT_STR_NOT_CONTAINS("T3_sanitize_latex_cmds_frac", out, "\\frac");
    ASSERT_STR_NOT_CONTAINS("T3_sanitize_latex_cmds_sqrt", out, "\\sqrt");
    ASSERT_STR_CONTAINS("T3_sanitize_latex_cmds_kept", out, "Simplify");
}

static void test_sanitize_markdown_bold(void)
{
    char out[512];
    edgai_sanitize_for_tts("This is **important** to know.", out, sizeof(out));
    ASSERT_STR_NOT_CONTAINS("T4_sanitize_bold_markers", out, "**");
    ASSERT_STR_CONTAINS("T4_sanitize_bold_word_kept", out, "important");
}

static void test_sanitize_markdown_heading(void)
{
    char out[512];
    edgai_sanitize_for_tts("# Introduction\nHello world.", out, sizeof(out));
    ASSERT_STR_NOT_CONTAINS("T5_sanitize_heading_hash", out, "#");
    ASSERT_STR_CONTAINS("T5_sanitize_heading_kept_text", out, "Hello world");
}

static void test_sanitize_url(void)
{
    char out[512];
    edgai_sanitize_for_tts("See https://example.com for details.", out, sizeof(out));
    ASSERT_STR_NOT_CONTAINS("T6_sanitize_url_stripped", out, "https://");
    ASSERT_STR_CONTAINS("T6_sanitize_url_context_kept", out, "See");
    ASSERT_STR_CONTAINS("T6_sanitize_url_suffix_kept", out, "for details");
}

static void test_sanitize_empty(void)
{
    char out[64];
    int n = edgai_sanitize_for_tts("", out, sizeof(out));
    ASSERT_EQ_INT("T7_sanitize_empty_returns_zero", n, 0);
    ASSERT_EQ_INT("T7_sanitize_empty_nul_terminated", (int)out[0], 0);
}

static void test_sanitize_preserves_numbers(void)
{
    char out[512];
    edgai_sanitize_for_tts("The WAEC score is 87.5 out of 100.", out, sizeof(out));
    ASSERT_STR_CONTAINS("T8_sanitize_numbers_kept", out, "87.5");
    ASSERT_STR_CONTAINS("T8_sanitize_waec_kept", out, "WAEC");
    ASSERT_STR_CONTAINS("T8_sanitize_100_kept", out, "100");
}

/* ── VAD tests (T9–T12) ─────────────────────────────────────────────────── */

/* One VAD frame = 20 ms @ 16 kHz = 320 samples */
#define VAD_FRAME_SAMPLES  ((EDGAI_CAPTURE_RATE * EDGAI_VAD_FRAME_MS) / 1000)

static void test_vad_initial_state(void)
{
    EdgaiVad *vad = edgai_vad_create();
    assert(vad != NULL);

    /* Feed one silence frame — should stay SILENCE */
    int16_t frame[VAD_FRAME_SAMPLES];
    make_silence_frame(frame, VAD_FRAME_SAMPLES);

    EdgaiVadState s = edgai_vad_process(vad, frame, VAD_FRAME_SAMPLES);
    ASSERT_EQ_INT("T9_vad_initial_silence", (int)s, (int)VAD_STATE_SILENCE);

    edgai_vad_destroy(vad);
}

static void test_vad_silence_stays_silence(void)
{
    EdgaiVad *vad = edgai_vad_create();
    assert(vad != NULL);

    int16_t frame[VAD_FRAME_SAMPLES];
    make_silence_frame(frame, VAD_FRAME_SAMPLES);

    EdgaiVadState s = VAD_STATE_SILENCE;
    /* Feed 50 silent frames (1 second) — must stay SILENCE, not TIMEOUT */
    for (int i = 0; i < 50; i++)
        s = edgai_vad_process(vad, frame, VAD_FRAME_SAMPLES);

    ASSERT_EQ_INT("T10_vad_silence_stays_silence", (int)s, (int)VAD_STATE_SILENCE);

    edgai_vad_destroy(vad);
}

static void test_vad_speech_detection(void)
{
    EdgaiVad *vad = edgai_vad_create();
    assert(vad != NULL);

    int16_t frame[VAD_FRAME_SAMPLES];
    make_speech_frame(frame, VAD_FRAME_SAMPLES);

    /* One loud frame should trigger SPEECH */
    EdgaiVadState s = edgai_vad_process(vad, frame, VAD_FRAME_SAMPLES);
    ASSERT_EQ_INT("T11_vad_speech_detected", (int)s, (int)VAD_STATE_SPEECH);

    edgai_vad_destroy(vad);
}

static void test_vad_trailing_silence_done(void)
{
    /* TRAIL_FRAMES = 800 ms / 20 ms = 40 frames of silence after speech */
    EdgaiVad *vad = edgai_vad_create();
    assert(vad != NULL);

    int16_t speech_frame[VAD_FRAME_SAMPLES];
    int16_t silence_frame[VAD_FRAME_SAMPLES];
    make_speech_frame(speech_frame, VAD_FRAME_SAMPLES);
    make_silence_frame(silence_frame, VAD_FRAME_SAMPLES);

    /* Start speaking */
    edgai_vad_process(vad, speech_frame, VAD_FRAME_SAMPLES);

    /* Now silence for TRAIL_FRAMES + 1 to ensure DONE */
    int trail_frames = EDGAI_VAD_TRAIL_MS / EDGAI_VAD_FRAME_MS;
    EdgaiVadState s = VAD_STATE_SILENCE;
    for (int i = 0; i <= trail_frames; i++)
        s = edgai_vad_process(vad, silence_frame, VAD_FRAME_SAMPLES);

    ASSERT_EQ_INT("T12_vad_trailing_silence_done", (int)s, (int)VAD_STATE_DONE);

    edgai_vad_destroy(vad);
}

/* ── Session voice field test (T13) ─────────────────────────────────────── */

static void test_session_voice_fields(void)
{
    EdgaiSession *session = edgai_init(NULL);
    assert(session != NULL);

    /*
     * speak_interrupt must be 0 at session start so TTS does not
     * immediately cancel the first utterance.
     */
    ASSERT_EQ_INT("T13_session_speak_interrupt_zero",
                   session->speak_interrupt, 0);

    /*
     * voice_state must be IDLE (0) at session start.
     */
    ASSERT_EQ_INT("T13_session_voice_state_idle",
                   (int)session->voice_state, (int)EDGAI_VOICE_STATE_IDLE);

    edgai_destroy(session);
}

/* ── WAV fixture generation ──────────────────────────────────────────────── */

static void generate_fixtures(void)
{
    /* Create fixtures directory path — relative to build dir or repo root */
    const char *paths[] = {
        "tests/fixtures/voice",
        "../tests/fixtures/voice",
        NULL
    };

    for (int i = 0; paths[i]; i++) {
        char wav_path[4096];
        snprintf(wav_path, sizeof(wav_path), "%s/silence_1s.wav", paths[i]);

        /* Try to write; if mkdir is needed, skip silently — CI sets up dirs */
        if (make_silence_wav(wav_path, 1000) == 0) {
            printf("  fixture: %s written\n", wav_path);
            return;
        }
    }
    printf("  fixture: could not write WAV fixture "
           "(run from repo root or build dir)\n");
}

/* ── main ───────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("test_voice: Phase 5 voice pipeline tests\n");
    printf("-----------------------------------------\n");

    /* Sanitize tests */
    printf("[sanitize]\n");
    test_sanitize_display_math();
    test_sanitize_inline_math();
    test_sanitize_latex_commands();
    test_sanitize_markdown_bold();
    test_sanitize_markdown_heading();
    test_sanitize_url();
    test_sanitize_empty();
    test_sanitize_preserves_numbers();

    /* VAD tests */
    printf("[vad]\n");
    test_vad_initial_state();
    test_vad_silence_stays_silence();
    test_vad_speech_detection();
    test_vad_trailing_silence_done();

    /* Session voice fields */
    printf("[session]\n");
    test_session_voice_fields();

    /* Generate WAV fixtures for integration tests */
    printf("[fixtures]\n");
    generate_fixtures();

    printf("-----------------------------------------\n");
    printf("Results: %d passed, %d failed\n", pass_count, fail_count);

    if (fail_count > 0) {
        fprintf(stderr,
            "\nNote: audio hardware tests (transcribe/speak) are skipped in CI.\n"
            "Set EDGAI_AUDIO_TEST=1 with a sound card to run them.\n");
        return 1;
    }
    return 0;
}
