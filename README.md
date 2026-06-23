# eduos-ai-engine

The open source AI engine powering EduOS — an offline-first, AI-native education OS built for African schools.

> Every student in Lagos or a village in Borno State deserves a private AI tutor that knows their exact syllabus, speaks their language, and never harvests their data.

## What this is

`eduos-ai-engine` is a lightweight RAG (Retrieval-Augmented Generation) engine that runs fully offline on low-spec hardware. It powers both the EduOS Linux desktop and the EduOS mobile app through a single C library (`libeduos`).

**Phase 2 (current):** Python RAG engine with Unix socket interface  
**Phase 3+:** C library (`libeduos`) with llama.cpp, whisper.cpp, Piper TTS

## How it works

```
Student question
      ↓
Preprocessing — stop words + Porter stemmer + synonym expansion
      ↓
FTS5 search — curriculum SQLite DB
      ↓
Rule-based intent detection
      ↓
Teaching sequence — HOOK → PREDICT → STEPS → VERIFY → PRACTICE → CLOSE
      ↓
LLM generation (Qwen 2.5 1.5B via Ollama) — only for concept explanation + rephrase
      ↓
Response to student
```

## Hardware tiers

| RAM | Model | Notes |
|-----|-------|-------|
| 2GB | Qwen 2.5 1.5B (~900MB Q4_K_M) | Handles most questions |
| 4GB | Phi-3 Mini | Better reasoning |
| 12GB+ (school server) | Llama 3 8B | Full conversational tutoring |

## Age modes

- **Playground** — ages 3–10, simple warm language
- **Explorer** — ages 10–15, step by step
- **Launchpad** — ages 15–19, direct Nigerian classroom English, WAEC prep
- **Professional** — 20+, concise and technical

## Quick start

**Requirements:** Python 3.10+, [Ollama](https://ollama.com)

```bash
# Clone
git clone https://github.com/EduOS-Org/eduos-ai-engine.git
cd eduos-ai-engine

# Setup
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt

# Pull model
ollama pull qwen2.5:1.5b

# Symlink your curriculum DB (or use demo)
ln -s /path/to/eduos-curriculum/db ./db

# Start engine
python3 src/rag/engine.py

# Connect test client (separate terminal)
python3 src/rag/client.py --age-mode launchpad
```

## Socket protocol

The engine runs as a persistent Unix socket server at `/tmp/eduos_rag.sock`.

**Input:**
```json
{"session_id": "abc123", "text": "what is logarithm", "age_mode": "launchpad"}
```

**Output:**
```json
{
  "session_id": "abc123",
  "text": "A logarithm is...",
  "sequence_state": "CONCEPT",
  "step_index": 0,
  "question_id": "waec-mathematics-2025-q75851",
  "can_skip": true,
  "error": null
}
```

## Curriculum DB

The curriculum database is separate (private repo). The engine works with any SQLite DB following the schema in `db/schema.sql`.

Demo DB ships in `db/demo/demo_curriculum.db`.

## Roadmap

- [x] Phase 1 — Python RAG prototype (proven)
- [x] Phase 2 — Production Python engine with socket server
- [ ] Phase 3 — C libeduos scaffold + CMakeLists
- [ ] Phase 4 — llama.cpp integration, drop Ollama dependency
- [ ] Phase 5 — whisper.cpp (voice input) + Piper TTS (voice output)
- [ ] Phase 6 — Android JNI bridge for React Native
- [ ] Phase 7 — LoRA fine-tuning on WAEC curriculum data

## License

GPL v3 — see [LICENSE](LICENSE)

The engine is open source. The curriculum database and fine-tuned model weights are proprietary.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md)