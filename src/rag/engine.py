#!/usr/bin/env python3
"""
EduOS RAG Engine — Phase 2
Production conversational RAG engine.

Architecture:
  Student message
        ↓
  Greeting? → respond directly, no DB, no LLM
        ↓
  No question loaded? → FTS5 search → LLM concept explanation (1.5B)
        ↓
  Question loaded? → Rule-based intent detection (no LLM)
        ↓
  Intent → State machine update → DB field served directly
        ↓
  Confused? → LLM rephrase (1.5B, one job: simplify)

LLM is called for exactly 2 things:
  1. Concept explanation — student asks "what is X"
  2. Rephrase — student is confused, simplify current DB content

Everything else: DB direct or rule-based. Instant.

Socket protocol:
  IN  → {"session_id": "abc123", "text": "ok I get it"}
  OUT → {"session_id": "abc123", "text": "...", "sequence_state": "STEPS", ...}

Usage:
  python3 src/rag/engine.py
  python3 src/rag/engine.py --socket /tmp/eduos.sock
"""

import sqlite3
import json
import re
import os
import random
import socket
import threading
import logging
import argparse
import urllib.request
import urllib.error
from pathlib import Path
from dataclasses import dataclass, field
from enum import Enum
from datetime import datetime
from nltk.stem import PorterStemmer

_stemmer = PorterStemmer()


# ─── LOGGING ───────────────────────────────────────────────────────────────────

LOG_DIR = Path("logs")
LOG_DIR.mkdir(exist_ok=True)

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    handlers=[
        logging.FileHandler(LOG_DIR / "engine.log"),
        logging.StreamHandler(),
    ]
)
log = logging.getLogger("eduos.engine")


# ─── CONFIG ────────────────────────────────────────────────────────────────────

DB_ROOT        = "db"
OLLAMA_URL     = "http://localhost:11434/api/generate"
MODEL          = "qwen2.5:1.5b"
SOCKET_PATH    = "/tmp/eduos_rag.sock"
MAX_HISTORY    = 10
OLLAMA_TIMEOUT = 60


# ─── LANGUAGE ──────────────────────────────────────────────────────────────────

STOP_WORDS = {
    "how", "do", "i", "find", "what", "is", "the", "a", "an",
    "to", "of", "in", "and", "or", "can", "you", "me", "my",
    "explain", "show", "help", "please", "tell", "get", "give",
    "understand", "solve", "calculate", "work", "out", "need",
    "want", "know", "learn", "about", "this", "that", "for",
    "with", "on", "at", "by", "from", "it", "its", "thing",
    "stuff", "like", "just", "really", "very", "much", "more",
    "some", "any", "all", "so", "up", "down", "did", "does",
    "was", "were", "be", "been", "have", "has", "had", "not",
    "but", "if", "then", "when", "where", "which", "who", "us",
    "whats", "dont", "im", "ive", "youre", "theyre",
}

GREETING_TOKENS = {
    "hey", "hi", "hello", "yo", "sup", "hiya", "heya", "hola",
    "morning", "afternoon", "evening", "night", "ola", "wassup",
    "good", "okay", "ok", "alright", "fine", "cool", "nice",
    "thanks", "thank", "pls", "bro", "sis", "abeg", "sir", "ma",
    "oya", "na", "sha", "oo", "ooo", "nah", "yh", "yep", "yea",
    "yeah", "howdy", "greetings", "waddup", "whatsup", "bonjour",
    "hii", "hiii", "hiiii", "heyy", "heyyy",
    "bye", "later", "goodbye", "goodnight",
}

SYNONYMS = {
    "sequence": "sequence progression", "sequences": "sequence progression",
    "series": "series progression", "ap": "arithmetic progression",
    "gp": "geometric progression", "nth": "nth term",
    "pattern": "sequence pattern", "quadratic": "quadratic equation",
    "quadratics": "quadratic equation", "factorize": "factorization",
    "factorise": "factorization", "factorising": "factorization",
    "factorizing": "factorization", "bracket": "factorization expand",
    "brackets": "factorization expand", "expand": "expansion algebra",
    "simplify": "simplification algebra", "expression": "algebraic expression",
    "simultaneous": "simultaneous equations", "inequality": "inequalities",
    "gradient": "gradient slope", "slope": "gradient slope",
    "intercept": "intercept graph", "graph": "graph function",
    "parabola": "parabola quadratic", "fraction": "fraction",
    "fractions": "fraction", "percentage": "percentage",
    "ratio": "ratio proportion", "proportion": "proportion ratio",
    "set": "set theory", "sets": "set theory", "union": "union set",
    "intersection": "intersection set", "venn": "venn diagram set",
    "prime": "prime number", "factor": "factor prime",
    "factors": "factor prime", "multiple": "multiple LCM",
    "lcm": "LCM lowest common multiple", "hcf": "HCF highest common factor",
    "log": "logarithm", "logs": "logarithm", "logarithm": "logarithm",
    "indices": "indices powers", "index": "indices powers",
    "power": "indices powers", "exponent": "indices powers",
    "surd": "surds", "surds": "surds", "area": "area mensuration",
    "perimeter": "perimeter mensuration", "volume": "volume mensuration",
    "circle": "circle geometry", "triangle": "triangle geometry",
    "angle": "angle geometry", "angles": "angle geometry",
    "bearing": "bearing geometry", "bearings": "bearing geometry",
    "locus": "locus geometry", "theorem": "theorem geometry",
    "pythagoras": "pythagoras theorem", "trig": "trigonometry",
    "sine": "sine trigonometry", "cosine": "cosine trigonometry",
    "tangent": "tangent trigonometry", "sin": "sine trigonometry",
    "cos": "cosine trigonometry", "tan": "tangent trigonometry",
    "sohcahtoa": "trigonometry sine cosine tangent",
    "mean": "mean average statistics", "median": "median statistics",
    "mode": "mode statistics", "average": "mean average",
    "probability": "probability", "chance": "probability",
    "frequency": "frequency statistics", "histogram": "histogram statistics",
    "ogive": "ogive cumulative frequency", "quartile": "quartile statistics",
    "deviation": "standard deviation", "matrix": "matrix",
    "matrices": "matrix", "determinant": "determinant matrix",
    "vector": "vector", "vectors": "vector",
    "times": "multiplication", "minusing": "subtraction",
    "plusin": "addition", "dividing": "division",
    "squareroot": "square root", "sq": "square root",
    "base": "number base conversion", "binary": "base number",
    "roots": "roots quadratic solution",
}


# ─── INTENT SIGNALS ────────────────────────────────────────────────────────────

ADVANCE_WORDS = {
    "ok", "okay", "yes", "yep", "yh", "yeah", "sure", "alright",
    "next", "continue", "got it", "get it", "understand", "understood",
    "makes sense", "clear", "oya", "go ahead", "proceed", "good",
    "great", "nice", "cool", "fine", "right", "correct", "true",
    "show me", "show steps", "what are the steps", "steps please",
    "let's go", "let's continue", "keep going", "move on",
    "i see", "i'm ready", "ready", "go on",
}

SKIP_PHRASES = [
    "skip", "next question", "another question", "different topic",
    "pass", "not interested", "change topic", "next one", "another one",
    "something else", "move on to",
]

CONFUSED_PHRASES = [
    "don't get", "dont get", "don't understand", "dont understand",
    "confused", "lost", "huh", "explain again", "not clear",
    "still don't", "still confused", "what do you mean", "say again",
    "repeat", "rephrase", "simpler", "too hard", "not following",
    "what does that mean", "i'm lost", "im lost",
]

DECLINE_WORDS = {
    "no", "nah", "nope", "not now", "not yet", "not really",
    "nah fam", "no thanks", "later", "not interested",
}


# ─── TYPES ─────────────────────────────────────────────────────────────────────

class SequenceState(str, Enum):
    CONCEPT   = "CONCEPT"     # student asked "what is X", explanation given
    HOOK      = "HOOK"
    PREDICT   = "PREDICT"
    STEPS     = "STEPS"
    VERIFY    = "VERIFY"
    PRACTICE  = "PRACTICE"
    CLOSE     = "CLOSE"
    DONE      = "DONE"


class Intent(str, Enum):
    ADVANCE    = "advance"
    RE_EXPLAIN = "re_explain"
    SKIP       = "skip"
    DECLINE    = "decline"
    UNKNOWN    = "unknown"


class AgeMode(str, Enum):
    PLAYGROUND   = "playground"
    EXPLORER     = "explorer"
    LAUNCHPAD    = "launchpad"
    PROFESSIONAL = "professional"


AGE_NOTES = {
    "playground":   "Simple language for a child aged 6-10. Short sentences. Warm.",
    "explorer":     "Clear language for age 10-15. Step by step.",
    "launchpad":    "Direct Nigerian classroom English for SS3 WAEC prep. Sharp, practical.",
    "professional": "Concise and technical.",
}


@dataclass
class Message:
    role: str
    text: str
    timestamp: str = field(default_factory=lambda: datetime.utcnow().isoformat())


@dataclass
class QuestionState:
    question_id:    str
    question_text:  str
    correct_answer: str
    hook:           str
    predict:        str
    steps:          list
    how_to_ace:     str
    explanation:    str
    options:        list = field(default_factory=list)  # [("A", "0.118"), ...]
    step_index:     int  = 0


@dataclass
class Session:
    session_id:       str
    age_mode:         AgeMode       = AgeMode.LAUNCHPAD
    sequence_state:   SequenceState = SequenceState.HOOK
    current_question: QuestionState = None
    history:          list          = field(default_factory=list)
    question_count:   int           = 0


# ─── DB ────────────────────────────────────────────────────────────────────────

def get_db(country: str, exam_body: str, subject: str):
    path = Path(DB_ROOT) / country / exam_body / subject / f"{subject}.db"
    if not path.exists():
        raise FileNotFoundError(f"DB not found: {path}")
    conn = sqlite3.connect(path, check_same_thread=False)
    conn.row_factory = sqlite3.Row
    return conn


# ─── PREPROCESSING ─────────────────────────────────────────────────────────────

def is_greeting(query: str) -> bool:
    tokens = re.findall(r"[a-zA-Z0-9_]+", query.lower())
    if not tokens:
        return False
    return all(t in GREETING_TOKENS or t in STOP_WORDS for t in tokens)


def preprocess(query: str) -> str:
    tokens = re.findall(r"[a-zA-Z0-9_]+", query.lower())
    tokens = [t for t in tokens if t not in STOP_WORDS and len(t) > 1]
    if not tokens:
        return query.strip()

    expanded = []
    seen = set()
    for token in tokens:
        stemmed = _stemmer.stem(token)
        synonym = SYNONYMS.get(token) or SYNONYMS.get(stemmed)
        if synonym:
            for st in synonym.split():
                s = _stemmer.stem(st)
                if s not in seen:
                    expanded.append(s)
                    seen.add(s)
        else:
            if stemmed not in seen:
                expanded.append(stemmed)
                seen.add(stemmed)
    return " ".join(expanded) if expanded else query.strip()


# ─── RETRIEVAL ─────────────────────────────────────────────────────────────────

def search(conn, query: str, top_k: int = 3) -> list:
    clean = preprocess(query)
    if not clean:
        return []
    try:
        results = conn.execute("""
            SELECT
                q.id, q.year, q.question_text, q.correct_answer,
                e.hook, e.predict, e.text AS explanation_text,
                e.steps, e.how_to_ace, e.model_text,
                e.source AS explanation_source, fts.rank
            FROM questions_fts fts
            JOIN questions q         ON q.id = fts.question_id
            LEFT JOIN explanations e ON e.question_id = fts.question_id
            WHERE questions_fts MATCH ?
            ORDER BY rank
            LIMIT ?
        """, (clean, top_k)).fetchall()
        return [dict(r) for r in results]
    except Exception as e:
        log.error(f"Search error: {e}")
        return []


def row_to_question(row: dict, conn=None) -> QuestionState:
    steps = []
    if row.get("steps"):
        try:
            raw = json.loads(row["steps"]) if isinstance(row["steps"], str) else row["steps"]
            if isinstance(raw, list):
                steps = [str(s) for s in raw]
            elif isinstance(raw, dict):
                steps = [str(v) for v in raw.values()]
        except Exception:
            steps = [str(row["steps"])]

    # Fetch options from DB if connection provided
    options = []
    if conn:
        try:
            rows = conn.execute(
                "SELECT option_label, option_text FROM options WHERE question_id = ? ORDER BY option_label",
                (row["id"],)
            ).fetchall()
            options = [(r[0], r[1]) for r in rows]
        except Exception as e:
            log.warning(f"Options fetch failed: {e}")

    return QuestionState(
        question_id    = row["id"],
        question_text  = row.get("question_text") or "",
        correct_answer = row.get("correct_answer") or "",
        hook           = row.get("hook") or "",
        predict        = row.get("predict") or "",
        steps          = steps,
        how_to_ace     = row.get("how_to_ace") or "",
        explanation    = row.get("explanation_text") or row.get("model_text") or "",
        options        = options,
    )


# ─── LLM — two jobs only ──────────────────────────────────────────────────────

def call_ollama(prompt: str) -> str:
    body = json.dumps({
        "model": MODEL, "prompt": prompt, "stream": False,
    }).encode("utf-8")
    req = urllib.request.Request(
        OLLAMA_URL, data=body,
        headers={"Content-Type": "application/json"}, method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=OLLAMA_TIMEOUT) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            return data.get("response", "").strip()
    except urllib.error.URLError:
        log.error("Ollama not running — start with: ollama serve")
        return ""
    except Exception as e:
        log.error(f"Ollama error: {e}")
        return ""


def llm_concept(student_text: str, explanation: str, age_note: str, age_mode: str = "launchpad") -> str:
    """Job 1: explain a concept using curriculum content as reference."""

    examples = {
        "playground": (
            'Student: "what is multiplication"\n'
            'Tutor: "Multiplication is like super-fast adding! Instead of adding 3 plus 3 plus 3, you just say 3 times 3 equals 9. Want to try a fun question?"'
        ),
        "explorer": (
            'Student: "what is fraction"\n'
            'Tutor: "A fraction shows part of a whole. Cut a pizza into 4 slices and eat 1 — you ate 1 out of 4, which we write as 1/4. Want to try an example question?"'
        ),
        "launchpad": (
            'Student: "what is simultaneous equation"\n'
            'Tutor: "A simultaneous equation is when you have two equations with two unknowns and you solve them together to find both values. WAEC tests this every year using substitution or elimination. Want to try an example question?"'
        ),
        "professional": (
            'Student: "what is a determinant"\n'
            'Tutor: "A determinant is a scalar value computed from a square matrix that tells you if the matrix is invertible. For a 2x2 matrix with elements a, b, c, d — the determinant is ad minus bc. Want to try one?"'
        ),
    }

    example = examples.get(age_mode, examples["launchpad"])

    prompt = f"""You are a maths tutor talking directly to one student.

{age_note}

Example of a perfect response for this age group:
{example}

Now answer this student. Use the reference below but write in your own words.
Rules: under 60 words. No markdown. No bold. No bullet points. No LaTeX. No backslashes. No dollar signs. No brackets around maths. Write numbers and equations in plain text only — example: "10 squared equals 100", not "\(10^2 = 100\)". End by asking if they want to try an example question.

Student: "{student_text}"
Reference: {explanation[:400]}
Tutor:"""
    return call_ollama(prompt)


def llm_rephrase(text: str, age_note: str) -> str:
    """Job 2: rephrase DB content more simply when student is confused."""
    prompt = f"""A student is confused by this explanation. Rephrase it more simply in under 50 words. No maths symbols. Plain sentences.

{age_note}
Original: "{text[:300]}"
Simpler:"""
    result = call_ollama(prompt)
    return result if result else text


# ─── INTENT DETECTION — rule-based ─────────────────────────────────────────────

def detect_intent(text: str) -> Intent:
    t = text.lower().strip()

    for phrase in SKIP_PHRASES:
        if phrase in t:
            return Intent.SKIP

    for phrase in CONFUSED_PHRASES:
        if phrase in t:
            return Intent.RE_EXPLAIN

    # Decline
    if t in DECLINE_WORDS:
        return Intent.DECLINE

    # Advance — check exact match and phrase containment
    if t in ADVANCE_WORDS:
        return Intent.ADVANCE
    for phrase in ADVANCE_WORDS:
        if len(phrase.split()) > 1 and phrase in t:
            return Intent.ADVANCE
    for word in ADVANCE_WORDS:
        if len(word.split()) == 1:
            if t == word or t.startswith(word + " ") or t.endswith(" " + word):
                return Intent.ADVANCE

    return Intent.UNKNOWN


# ─── STATE MACHINE ─────────────────────────────────────────────────────────────

SEQUENCE = [
    SequenceState.CONCEPT,
    SequenceState.HOOK,
    SequenceState.PREDICT,
    SequenceState.STEPS,
    SequenceState.VERIFY,
    SequenceState.PRACTICE,
    SequenceState.CLOSE,
    SequenceState.DONE,
]


def advance_state(session: Session) -> None:
    q = session.current_question

    if session.sequence_state == SequenceState.STEPS:
        if q and q.step_index < len(q.steps) - 1:
            q.step_index += 1
            return
        session.sequence_state = SequenceState.VERIFY
        return

    try:
        idx = SEQUENCE.index(session.sequence_state)
        if idx < len(SEQUENCE) - 1:
            session.sequence_state = SEQUENCE[idx + 1]
    except ValueError:
        session.sequence_state = SequenceState.DONE


def get_response_for_state(session: Session, age_note: str, rephrase: bool) -> str:
    """Pick the right DB field for the current state. LLM only if rephrase=True."""
    q     = session.current_question
    state = session.sequence_state

    if state == SequenceState.HOOK:
        # Show question + options first, then the hook
        question_block = q.question_text or ""
        if q.options:
            opts = "  ".join(f"{label}. {text}" for label, text in q.options)
            question_block = f"{question_block}\n\n{opts}"
        hook = q.hook or "Let's get into this."
        return f"{question_block}\n\n{hook}"

    if state == SequenceState.PREDICT:
        text = q.predict or "Before I explain — what do you think the answer is?"
        return llm_rephrase(text, age_note) if rephrase else text

    if state == SequenceState.STEPS:
        if q.steps and q.step_index < len(q.steps):
            text = q.steps[q.step_index]
        else:
            text = q.explanation[:300] if q.explanation else "Let me walk you through this."
        return llm_rephrase(text, age_note) if rephrase else text

    if state == SequenceState.VERIFY:
        return f"Now try it yourself: {q.question_text[:200]}"

    if state == SequenceState.PRACTICE:
        return "Try solving it. What answer do you get?"

    if state == SequenceState.CLOSE:
        text = q.how_to_ace or "You've got this. Keep practising."
        return llm_rephrase(text, age_note) if rephrase else text

    return "What topic next?"


# ─── SESSION HANDLER ───────────────────────────────────────────────────────────

class SessionHandler:
    def __init__(self, conn, session_id: str, age_mode: str = "launchpad"):
        self.conn    = conn
        self.session = Session(session_id=session_id, age_mode=AgeMode(age_mode))
        self.age_note = AGE_NOTES.get(age_mode, AGE_NOTES["launchpad"])
        log.info(f"[{session_id}] Session started (age_mode={age_mode})")

    def handle(self, student_text: str) -> dict:
        s = self.session
        q = s.current_question

        # ── 1. Greeting ──
        if is_greeting(student_text):
            r = random.choice(["Hey!", "Hi!", "Yo!"])
            self._record("student", student_text)
            self._record("tutor", r)
            return self._out(r)

        # ── 2. No question loaded — concept explanation ──
        if q is None:
            results = search(self.conn, student_text, top_k=1)
            if not results:
                return self._out(
                    "I couldn't find that topic. Try: 'quadratic', 'logarithm', 'nth term', 'probability'.",
                    error="no_match",
                )

            s.current_question = row_to_question(results[0], self.conn)
            s.question_count  += 1
            s.sequence_state   = SequenceState.CONCEPT
            log.info(f"[{s.session_id}] Loaded: {s.current_question.question_id}")

            exp = s.current_question.explanation
            self._record("student", student_text)
            response = llm_concept(student_text, exp, self.age_note, s.age_mode.value)
            if not response:
                response = "Good topic. Want to see an example question on this?"
            self._record("tutor", response)
            return self._out(response)

        # ── 3. Question loaded — detect intent ──
        self._record("student", student_text)
        intent = detect_intent(student_text)
        log.info(f"[{s.session_id}] intent={intent.value} state={s.sequence_state.value}")

        # Decline in CONCEPT state → student said "no" to example question
        if intent == Intent.DECLINE and s.sequence_state == SequenceState.CONCEPT:
            s.current_question = None
            s.sequence_state   = SequenceState.HOOK
            r = "No problem. What else do you want to learn about?"
            self._record("tutor", r)
            return self._out(r)

        # Decline in other states → treat as needing re-explanation
        if intent == Intent.DECLINE:
            r = get_response_for_state(s, self.age_note, rephrase=True)
            self._record("tutor", r)
            return self._out(r)

        # Skip → reset
        if intent == Intent.SKIP:
            s.current_question = None
            s.sequence_state   = SequenceState.HOOK
            r = "No wahala. What topic do you want to try next?"
            self._record("tutor", r)
            return self._out(r)

        # Unknown intent → check if this is a NEW topic question
        if intent == Intent.UNKNOWN:
            preprocessed = preprocess(student_text)
            keywords = preprocessed.split()
            # Only treat as new topic if:
            # - message is long enough to be a real question (5+ chars)
            # - preprocessing produced 2+ meaningful keywords
            # - it's not a short response or sequence-related phrase
            sequence_phrases = {
                "steps", "step", "next", "show", "what", "how",
                "tell", "continue", "go", "more", "again"
            }
            is_short_response = len(student_text.strip()) < 5
            is_sequence_phrase = all(w in sequence_phrases for w in student_text.lower().split())
            has_enough_keywords = len(keywords) >= 2

            if not is_short_response and not is_sequence_phrase and has_enough_keywords:
                new_results = search(self.conn, student_text, top_k=1)
                if new_results:
                    new_id = new_results[0]["id"]
                    current_id = q.question_id if q else None
                    if new_id != current_id:
                        s.current_question = row_to_question(new_results[0], self.conn)
                        s.question_count  += 1
                        s.sequence_state   = SequenceState.CONCEPT
                        log.info(f"[{s.session_id}] New topic: {s.current_question.question_id}")

                        exp = s.current_question.explanation
                        response = llm_concept(student_text, exp, self.age_note, s.age_mode.value)
                        if not response:
                            response = "Good topic. Want to see an example question?"
                        self._record("tutor", response)
                        return self._out(response)

        # Re-explain → stay in state, rephrase with LLM
        if intent == Intent.RE_EXPLAIN:
            r = get_response_for_state(s, self.age_note, rephrase=True)
            self._record("tutor", r)
            return self._out(r)

        # Advance or unknown (no new topic found) → move forward
        if intent in (Intent.ADVANCE, Intent.UNKNOWN):
            advance_state(s)

        # If done → reset
        if s.sequence_state == SequenceState.DONE:
            s.current_question = None
            s.sequence_state   = SequenceState.HOOK
            r = "You've finished this one. What do you want to work on next?"
            self._record("tutor", r)
            return self._out(r)

        # Serve DB content for current state
        r = get_response_for_state(s, self.age_note, rephrase=False)
        self._record("tutor", r)
        return self._out(r)

    def _record(self, role: str, text: str):
        self.session.history.append(Message(role=role, text=text))
        if len(self.session.history) > MAX_HISTORY * 2:
            self.session.history = self.session.history[-(MAX_HISTORY * 2):]

    def _out(self, text: str, error: str = None) -> dict:
        q = self.session.current_question
        return {
            "session_id":     self.session.session_id,
            "text":           text,
            "sequence_state": self.session.sequence_state.value,
            "step_index":     q.step_index if q else 0,
            "question_id":    q.question_id if q else None,
            "can_skip":       self.session.sequence_state not in (SequenceState.DONE, SequenceState.HOOK),
            "error":          error,
        }


# ─── SOCKET SERVER ─────────────────────────────────────────────────────────────

def handle_client(client_sock, conn):
    handlers = {}
    try:
        buf = ""
        while True:
            chunk = client_sock.recv(4096).decode("utf-8")
            if not chunk:
                break
            buf += chunk
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                line = line.strip()
                if not line:
                    continue
                try:
                    msg        = json.loads(line)
                    session_id = msg.get("session_id", "default")
                    text       = msg.get("text", "").strip()
                    age_mode   = msg.get("age_mode", "launchpad")
                    if not text:
                        continue
                    if session_id not in handlers:
                        handlers[session_id] = SessionHandler(conn, session_id, age_mode)
                    result = handlers[session_id].handle(text)
                    client_sock.sendall((json.dumps(result) + "\n").encode("utf-8"))
                    log.info(f"[{session_id}] state={result['sequence_state']} q={result['question_id']}")
                except json.JSONDecodeError as e:
                    log.warning(f"Bad JSON: {e}")
                    client_sock.sendall((json.dumps({"error": "invalid_json", "text": ""}) + "\n").encode())
                except Exception as e:
                    log.error(f"Handler error: {e}", exc_info=True)
                    client_sock.sendall((json.dumps({"error": "internal", "text": "Something went wrong."}) + "\n").encode())
    except Exception as e:
        log.error(f"Client error: {e}")
    finally:
        client_sock.close()
        log.info("Client disconnected")


def run_server(socket_path: str, conn):
    if os.path.exists(socket_path):
        os.unlink(socket_path)
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(socket_path)
    server.listen(5)
    log.info(f"EduOS RAG engine listening on {socket_path}")
    try:
        while True:
            client_sock, _ = server.accept()
            log.info("Client connected")
            threading.Thread(target=handle_client, args=(client_sock, conn), daemon=True).start()
    except KeyboardInterrupt:
        log.info("Engine shutting down")
    finally:
        server.close()
        if os.path.exists(socket_path):
            os.unlink(socket_path)


# ─── ENTRY POINT ───────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="EduOS RAG Engine")
    parser.add_argument("--socket",    default=SOCKET_PATH)
    parser.add_argument("--subject",   default="mathematics")
    parser.add_argument("--country",   default="nigeria")
    parser.add_argument("--exam-body", default="waec", dest="exam_body")
    args = parser.parse_args()
    try:
        conn = get_db(args.country, args.exam_body, args.subject)
        log.info(f"DB loaded: {args.country}/{args.exam_body}/{args.subject}")
    except FileNotFoundError as e:
        log.error(str(e))
        exit(1)
    run_server(args.socket, conn)


if __name__ == "__main__":
    main()