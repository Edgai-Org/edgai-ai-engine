/* sanitize.h — LLM output sanitizer for TTS
 *
 * Strips patterns that Piper would speak literally and incorrectly:
 * LaTeX math notation, Markdown formatting, and bare URLs.
 *
 * Preserves sentence-ending punctuation (.?!) so Piper can apply natural
 * prosody. Preserves Nigerian curriculum terms (WAEC, JAMB, SSS, JSS).
 *
 * All functions are pure and thread-safe: no global state, no side effects.
 */

#ifndef EDGAI_SANITIZE_H
#define EDGAI_SANITIZE_H

#include <stddef.h>

/* Sanitize text for TTS synthesis.
 *   input:       NUL-terminated LLM output string; not modified
 *   out_buf:     caller-supplied buffer for the cleaned text
 *   out_buf_len: capacity of out_buf in bytes
 * Returns the length of the cleaned text (excluding NUL) on success,
 * or -1 if out_buf is too small to hold the result.
 * Single pass — O(n) time, no allocations. */
int edgai_sanitize_for_tts(const char *input,
                             char *out_buf,
                             size_t out_buf_len);

#endif /* EDGAI_SANITIZE_H */
