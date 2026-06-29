/* audio_capture.h — ALSA microphone interface
 *
 * Wraps ALSA capture for 16 kHz S16_LE mono audio, the format required
 * by Vosk. All functions return EDGAI_OK on success or a negative
 * EDGAI_ERR_* code on failure — never abort() or exit().
 *
 * Thread safety: EdgaiAudioCapture handles are NOT shared across threads.
 * Each thread must open its own capture handle.
 */

#ifndef EDGAI_AUDIO_CAPTURE_H
#define EDGAI_AUDIO_CAPTURE_H

#include <stdint.h>
#include <stddef.h>

/* ── Capture parameters (must match Vosk input requirements) ─────────────── */
#define EDGAI_CAPTURE_RATE       16000   /* Hz — required by Vosk              */
#define EDGAI_CAPTURE_CHANNELS       1   /* mono                               */
#define EDGAI_CAPTURE_PERIOD_FRAMES 4096 /* frames per ALSA period             */

typedef struct EdgaiAudioCapture EdgaiAudioCapture;

/* Open the ALSA capture device.
 *   device: ALSA device string, e.g. "default" or "hw:0,0".
 *           Pass NULL to use the EDGAI_ALSA_DEVICE env var, or "default".
 * Returns a handle on success, NULL on failure (device busy, not found, etc.).
 * The caller owns the handle and must free it with edgai_audio_capture_close(). */
EdgaiAudioCapture *edgai_audio_capture_open(const char *device);

/* Read PCM samples from the microphone into buf.
 *   buf:         destination buffer for S16_LE samples
 *   buf_frames:  capacity of buf in frames (1 frame = 1 int16_t for mono)
 * Returns the number of frames read on success, or a negative EDGAI_ERR_* code.
 * Blocks until buf_frames are available or an error occurs. */
int edgai_audio_capture_read(EdgaiAudioCapture *cap,
                              int16_t *buf, size_t buf_frames);

/* Close the capture device and free all resources associated with the handle.
 * Safe to call with NULL. */
void edgai_audio_capture_close(EdgaiAudioCapture *cap);

#endif /* EDGAI_AUDIO_CAPTURE_H */
