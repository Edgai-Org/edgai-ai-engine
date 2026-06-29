/* sanitize.c — LLM output sanitizer for TTS, EduOS libedgai
 *
 * Single-pass O(n) cleaner. Walks the input byte-by-byte and copies
 * characters to out_buf, skipping or replacing patterns that would be
 * spoken literally by Piper and sound wrong to a student.
 *
 * What is stripped:
 *   LaTeX:    \( \) \[ \] $$ $ \frac \sqrt \cdot ^{ _{
 *   Markdown: ** * _ # ` > ---
 *   URLs:     http:// and https://
 *   Long parentheticals (> EDGAI_SANITIZE_PAREN_MAX chars) — spoken awkwardly
 *
 * What is preserved:
 *   Sentence punctuation . ? ! , — Piper uses these for natural prosody
 *   Numbers and decimals — Piper pronounces them correctly
 *   Nigerian curriculum terms: WAEC JAMB SSS JSS — preserved as-is
 *   All regular words and spaces
 *
 * Author: Edgai Contributors
 * License: GPL v3
 */

/* 1. System includes */
#include <string.h>
#include <stddef.h>

/* 2. Third-party includes — none */

/* 3. Local includes */
#include "sanitize.h"

/* 4. Module-private constants */
#define EDGAI_SANITIZE_PAREN_MAX 80  /* parens longer than this are dropped */

/* 5. Module-private types — none */

/* 6. Static function declarations */
static int  starts_with(const char *s, const char *prefix);
static int  skip_url(const char *s);
static int  skip_paren_aside(const char *s);

/* ── Public API ─────────────────────────────────────────────────────────── */

int edgai_sanitize_for_tts(const char *input,
                             char *out_buf,
                             size_t out_buf_len)
{
    if (!input || !out_buf || out_buf_len == 0)
        return -1;

    const char *r = input;
    char *w = out_buf;
    char *end = out_buf + out_buf_len - 1; /* reserve slot for NUL */

    while (*r != '\0') {
        /* ── URLs ─────────────────────────────────────────────────────── */
        if (starts_with(r, "http://") || starts_with(r, "https://")) {
            int skip = skip_url(r);
            r += skip;
            /* replace with a space so words don't run together */
            if (w < end) *w++ = ' ';
            continue;
        }

        /* ── LaTeX display math: $$ ... $$ ───────────────────────────── */
        if (r[0] == '$' && r[1] == '$') {
            r += 2;
            while (*r != '\0' && !(r[0] == '$' && r[1] == '$')) r++;
            if (r[0] == '$') r += 2;
            if (w < end) *w++ = ' ';
            continue;
        }

        /* ── LaTeX inline math: $ ... $ ──────────────────────────────── */
        if (r[0] == '$') {
            r++;
            while (*r != '\0' && *r != '$') r++;
            if (*r == '$') r++;
            if (w < end) *w++ = ' ';
            continue;
        }

        /* ── LaTeX commands starting with backslash ───────────────────── */
        if (r[0] == '\\') {
            /* \( ... \) — inline math */
            if (r[1] == '(') {
                r += 2;
                while (*r != '\0' && !(r[0] == '\\' && r[1] == ')')) r++;
                if (r[0] == '\\') r += 2;
                if (w < end) *w++ = ' ';
                continue;
            }
            /* \[ ... \] — display math */
            if (r[1] == '[') {
                r += 2;
                while (*r != '\0' && !(r[0] == '\\' && r[1] == ']')) r++;
                if (r[0] == '\\') r += 2;
                if (w < end) *w++ = ' ';
                continue;
            }
            /* Named LaTeX commands (\frac, \sqrt, \cdot, \log, etc.) */
            if (starts_with(r, "\\frac")  ||
                starts_with(r, "\\sqrt")  ||
                starts_with(r, "\\cdot")  ||
                starts_with(r, "\\times") ||
                starts_with(r, "\\div")   ||
                starts_with(r, "\\pm")    ||
                starts_with(r, "\\log")   ||
                starts_with(r, "\\sin")   ||
                starts_with(r, "\\cos")   ||
                starts_with(r, "\\tan")) {
                /* skip the command name, leave the argument text */
                while (*r != '\0' && *r != ' ' && *r != '{' && *r != '(')
                    r++;
                continue;
            }
            /* ^{ ... } or _{ ... } — superscript/subscript */
            if ((r[0] == '^' || r[0] == '_') && r[1] == '{') {
                r += 2;
                int depth = 1;
                while (*r != '\0' && depth > 0) {
                    if (*r == '{') depth++;
                    else if (*r == '}') depth--;
                    r++;
                }
                if (w < end) *w++ = ' ';
                continue;
            }
            /* Unknown backslash sequence — skip just the backslash */
            r++;
            continue;
        }

        /* ── Superscript ^{ or subscript _{ outside backslash context ── */
        if ((r[0] == '^' || r[0] == '_') && r[1] == '{') {
            r += 2;
            int depth = 1;
            while (*r != '\0' && depth > 0) {
                if (*r == '{') depth++;
                else if (*r == '}') depth--;
                r++;
            }
            if (w < end) *w++ = ' ';
            continue;
        }

        /* ── Markdown: ** bold ────────────────────────────────────────── */
        if (r[0] == '*' && r[1] == '*') {
            r += 2;
            continue;
        }

        /* ── Markdown: * italic ───────────────────────────────────────── */
        if (r[0] == '*') {
            r++;
            continue;
        }

        /* ── Markdown: _ emphasis ─────────────────────────────────────── */
        if (r[0] == '_') {
            r++;
            continue;
        }

        /* ── Markdown: # heading (at start of line) ───────────────────── */
        if (r[0] == '#' && (r == input || r[-1] == '\n')) {
            while (*r == '#' || *r == ' ') r++;
            continue;
        }

        /* ── Markdown: > blockquote ───────────────────────────────────── */
        if (r[0] == '>' && (r == input || r[-1] == '\n')) {
            r++;
            continue;
        }

        /* ── Markdown: --- horizontal rule ───────────────────────────── */
        if (r[0] == '-' && r[1] == '-' && r[2] == '-') {
            r += 3;
            if (w < end) *w++ = ' ';
            continue;
        }

        /* ── Markdown: backtick code span ─────────────────────────────── */
        if (r[0] == '`') {
            r++;
            while (*r != '\0' && *r != '`') r++;
            if (*r == '`') r++;
            continue;
        }

        /* ── Long parenthetical aside ─────────────────────────────────── */
        if (r[0] == '(') {
            int skip = skip_paren_aside(r);
            if (skip > 0) {
                r += skip;
                if (w < end) *w++ = ' ';
                continue;
            }
            /* Short paren — keep it */
        }

        /* ── Curly braces (LaTeX argument wrappers) ───────────────────── */
        if (r[0] == '{' || r[0] == '}') {
            r++;
            continue;
        }

        /* ── Normal character — copy through ──────────────────────────── */
        if (w >= end)
            return -1; /* out_buf too small */
        *w++ = *r++;
    }

    *w = '\0';

    /* Collapse runs of whitespace down to single spaces */
    char *rd = out_buf;
    char *wr = out_buf;
    int last_was_space = 0;
    while (*rd != '\0') {
        if (*rd == ' ' || *rd == '\t' || *rd == '\n' || *rd == '\r') {
            if (!last_was_space && wr > out_buf) {
                *wr++ = ' ';
            }
            last_was_space = 1;
        } else {
            *wr++ = *rd;
            last_was_space = 0;
        }
        rd++;
    }
    /* Trim trailing space */
    if (wr > out_buf && wr[-1] == ' ') wr--;
    *wr = '\0';

    return (int)(wr - out_buf);
}

/* ── Static helpers ─────────────────────────────────────────────────────── */

static int starts_with(const char *s, const char *prefix)
{
    while (*prefix) {
        if (*s++ != *prefix++) return 0;
    }
    return 1;
}

/* Return the number of bytes to skip for a URL starting at s.
 * Stops at whitespace or end of string. */
static int skip_url(const char *s)
{
    const char *p = s;
    while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n')
        p++;
    return (int)(p - s);
}

/* If the parenthetical starting at s[0]=='(' is longer than
 * EDGAI_SANITIZE_PAREN_MAX characters (content only), return its total
 * byte length including the parens. Return 0 to keep the paren as-is. */
static int skip_paren_aside(const char *s)
{
    if (s[0] != '(')
        return 0;

    const char *p = s + 1;
    int depth = 1;
    int content_len = 0;
    while (*p != '\0' && depth > 0) {
        if (*p == '(') depth++;
        else if (*p == ')') depth--;
        if (depth > 0) content_len++;
        p++;
    }
    if (depth != 0)
        return 0; /* unmatched paren — don't skip */
    if (content_len > EDGAI_SANITIZE_PAREN_MAX)
        return (int)(p - s);
    return 0;
}
