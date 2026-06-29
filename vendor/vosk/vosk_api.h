/*
 * vosk_api.h — Vosk offline speech recognition C API
 *
 * Version: 0.3.45
 * License: Apache 2.0 — compatible with GPL v3
 *
 * libvosk.so is NOT committed to this repo. Download the prebuilt shared
 * library for your platform and place it in this directory:
 *
 *   Debian bookworm (amd64):
 *     wget https://github.com/alphacep/vosk-api/releases/download/v0.3.45/vosk-linux-x86_64-0.3.45.zip
 *     unzip vosk-linux-x86_64-0.3.45.zip
 *     cp vosk-linux-x86_64-0.3.45/libvosk.so vendor/vosk/
 *
 *   Model download (run once, not committed to repo):
 *     mkdir -p ~/.edgai/models/vosk
 *     cd ~/.edgai/models/vosk
 *     wget https://alphacephei.com/vosk/models/vosk-model-small-en-us-0.15.zip
 *     unzip vosk-model-small-en-us-0.15.zip
 */

#ifndef VOSK_API_H
#define VOSK_API_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VoskModel       VoskModel;
typedef struct VoskRecognizer  VoskRecognizer;

/* Silence noisy diagnostic output from Vosk internals. Call before any other API. */
void vosk_set_log_level(int log_level);

/* Load a Vosk acoustic model from disk. Returns NULL on failure.
 * path: directory containing model files (am/, conf/, graph/, etc.) */
VoskModel *vosk_model_new(const char *model_path);

/* Free a model loaded with vosk_model_new. */
void vosk_model_free(VoskModel *model);

/* Create a recognizer for continuous speech at the given sample rate.
 * sample_rate must match the audio capture rate (16000 Hz for EDGAI_CAPTURE_RATE). */
VoskRecognizer *vosk_recognizer_new(VoskModel *model, float sample_rate);

/* Create a recognizer with domain-specific grammar for improved accuracy.
 * grammar: JSON array of expected words, e.g. '["logarithm", "quadratic", ...]'
 * Use NULL grammar to fall back to free-form recognition. */
VoskRecognizer *vosk_recognizer_new_grm(VoskModel *model, float sample_rate,
                                         const char *grammar);

/* Tune the built-in endpointer. Call immediately after vosk_recognizer_new*.
 *   t_start_max: max seconds of silence at start before giving up (5.0)
 *   t_end:       seconds of trailing silence that marks end of utterance (0.8)
 *   t_max:       maximum utterance duration in seconds before forced end (10.0) */
void vosk_recognizer_set_endpointer_delays(VoskRecognizer *rec,
                                            float t_start_max,
                                            float t_end,
                                            float t_max);

/* Feed a chunk of raw PCM audio to the recognizer.
 *   data:   pointer to S16_LE samples
 *   length: byte count (not sample count)
 * Returns 1 if the utterance is complete, 0 if more audio is needed. */
int vosk_recognizer_accept_waveform(VoskRecognizer *rec,
                                     const char *data, int length);

/* Get the final recognition result as a JSON string.
 * Call after vosk_recognizer_accept_waveform returns 1.
 * Returns: '{"text": "recognised words here"}' or '{"text": ""}' on silence.
 * The returned pointer is valid until the next call to this recognizer. */
const char *vosk_recognizer_result(VoskRecognizer *rec);

/* Get an in-progress partial result as a JSON string.
 * Call while vosk_recognizer_accept_waveform is returning 0.
 * Returns: '{"partial": "words so far"}' */
const char *vosk_recognizer_partial_result(VoskRecognizer *rec);

/* Free a recognizer created with vosk_recognizer_new or vosk_recognizer_new_grm. */
void vosk_recognizer_free(VoskRecognizer *rec);

#ifdef __cplusplus
}
#endif

#endif /* VOSK_API_H */
