-- EduOS Curriculum Database Schema
-- This schema is public (GPL v3). Curriculum data is proprietary (eduos-premium-ai).
-- engine.py queries this schema directly — do not alter column names.

CREATE TABLE IF NOT EXISTS questions (
    id                       TEXT PRIMARY KEY,
    subject                  TEXT NOT NULL,
    year                     INTEGER NOT NULL,
    question_type            TEXT NOT NULL,          -- 'mcq' or 'theory'
    section                  TEXT NOT NULL,          -- 'objective' or 'theory'
    question_text            TEXT,
    correct_answer           TEXT,                   -- NULL for theory
    diagram_required         INTEGER DEFAULT 0,
    diagram_type             TEXT,
    diagram_status           TEXT,
    diagram_data             TEXT,                   -- JSON blob
    diagram_description      TEXT,
    diagram_recreation_notes TEXT,
    source_image_url         TEXT,
    needs_review             INTEGER DEFAULT 1,
    source_id                TEXT,
    scraped_at               TEXT,
    db_updated_at            TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS options (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    question_id TEXT NOT NULL REFERENCES questions(id) ON DELETE CASCADE,
    letter      TEXT NOT NULL,                       -- 'A', 'B', 'C', 'D'
    option_text TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS explanations (
    question_id      TEXT PRIMARY KEY REFERENCES questions(id) ON DELETE CASCADE,
    question_type    TEXT NOT NULL,                  -- 'mcq' or 'theory'
    hook             TEXT,
    predict          TEXT,
    text             TEXT,                           -- full static worked explanation
    how_to_ace       TEXT,
    steps            TEXT,                           -- JSON array
    common_mistakes  TEXT,                           -- JSON array
    exam_tips        TEXT,                           -- JSON array
    model_text       TEXT,                           -- theory model answer
    marking_scheme   TEXT,                           -- JSON blob
    source           TEXT DEFAULT 'pending',         -- 'pending', 'generated', 'verified'
    verified         INTEGER DEFAULT 0,
    db_updated_at    TEXT NOT NULL
);

-- FTS5 for full-text search across question + explanation content.
-- question_id column allows direct JOIN with questions table.
CREATE VIRTUAL TABLE IF NOT EXISTS questions_fts USING fts5(
    question_id,
    subject,
    year,
    question_type,
    search_text,
    tokenize='porter ascii'
);

CREATE INDEX IF NOT EXISTS idx_questions_subject_year ON questions(subject, year);
CREATE INDEX IF NOT EXISTS idx_questions_type         ON questions(question_type);
CREATE INDEX IF NOT EXISTS idx_questions_needs_review ON questions(needs_review);
CREATE INDEX IF NOT EXISTS idx_options_question       ON options(question_id);
