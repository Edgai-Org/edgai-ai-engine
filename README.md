# eduos-ai-engine

The open source AI engine powering EduOS — an offline-first, AI-native education OS built for African schools.

> Every student in Lagos or a village in Borno State deserves a private AI tutor that knows their exact syllabus, speaks their language, and never harvests their data.

## What this is

`eduos-ai-engine` is a C library (`libedgai`) that delivers curriculum-driven AI tutoring entirely offline. It runs on school desktops, laptops, and Android devices with no internet connection and no cloud dependency. The engine selects a quantised GGUF model based on available RAM, retrieves curriculum content from a local SQLite FTS5 database, and walks each student through a structured teaching sequence in their age mode.

Phase 4 is complete. The runtime is pure C — no Python, no Ollama.

## How it works

```
Student input
      │
      ▼
edgai_query()   ─── C API, single call per turn
      │
      ▼
Keyword intent detection
  ADVANCE / RE_EXPLAIN / SKIP / DECLINE / UNKNOWN
      │
      │  UNKNOWN ──► FTS5 retrieval (SQLite, Porter stemmer, synonym expansion)
      │                   │
      │               topic found ──► load question_id
      │               not found   ──► "I don't know that topic yet"
      │
      ▼
Teaching state machine
  CONCEPT → HOOK → PREDICT → STEPS → VERIFY → PRACTICE → CLOSE → DONE
      │
      ├── CONCEPT ──► llama.cpp inference  (concept explanation)
      ├── STEPS + RE_EXPLAIN ──► llama.cpp inference  (rephrase)
      └── all other states ──► DB content served directly
      │
      ▼
EdgaiResponse  { text, sequence_state, step_index, question_id, error }
```

The LLM is called for exactly two things: concept explanation at CONCEPT state, and rephrase at STEPS state when the student signals confusion. Everything else — hook, predict, steps, verify, practice — is served from the curriculum DB with zero inference cost.

## Hardware tiers

### Desktop / OS (no Android overhead)

| RAM | Model | Context | Notes |
|-----|-------|---------|-------|
| < 2 GB | Qwen 2.5 1.5B Q4_K_M | 512 | Minimum viable |
| 2–4 GB | Phi-3 Mini Q4_K_M | 2048 | Most school hardware |
| 4 GB+ | Llama 3 8B Q4_K_M | 4096 | School servers |

### Mobile (Android/iOS — OS eats 2–4 GB)

| Device RAM | Model | Context | Notes |
|------------|-------|---------|-------|
| 2 GB | Qwen 2.5 0.5B IQ4_XS | 512 | Only model that fits |
| 4 GB | Qwen 2.5 1.5B Q4_K_M | 512 | Tight config |
| 6 GB+ | Phi-3 Mini Q4_K_M | 2048 | Comfortable |

KV cache is quantised (Q4_0 or Q8_0 depending on tier) with flash attention enabled. Tier selection is automatic via `/proc/meminfo` on Linux or `sysctl hw.memsize` on macOS/Android.

## Age modes

| Mode | Ages | Style |
|------|------|-------|
| **Playground** | 3–10 | Warm, simple language, short sentences |
| **Explorer** | 10–15 | Step-by-step, encouraging |
| **Launchpad** | 15–19 | Direct Nigerian classroom English, WAEC prep |
| **Professional** | 20+ | Concise, technical, no hand-holding |

Age mode is set at session init from a JSON profile or the `EDGAI_IS_MOBILE` environment variable. It shapes formatter truncation, LLM system prompt tone, and DB content depth.

## Quick start

**Requirements:** Git, CMake ≥ 3.16, C11 compiler, C++17 compiler, pkg-config, libsqlite3-dev, 2 GB RAM, 2 GB disk.

```bash
# 1 — Clone with submodules (llama.cpp is pinned — do not update casually)
git clone --recurse-submodules https://github.com/EduOS-Org/eduos-ai-engine.git
cd eduos-ai-engine

# 2 — Download a GGUF model (example: 4 GB machine)
pip install huggingface_hub[cli]
huggingface-cli download bartowski/Qwen2.5-1.5B-Instruct-GGUF \
    Qwen2.5-1.5B-Instruct-Q4_K_M.gguf --local-dir ~/.edgai/models/
mv ~/.edgai/models/Qwen2.5-1.5B-Instruct-Q4_K_M.gguf \
   ~/.edgai/models/qwen2.5-1.5b-instruct-q4_k_m.gguf

# 3 — Build (run from repo root — tests resolve demo DB by relative path)
cmake -S . -B build
cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)

# 4 — Run tests
ctest --test-dir build --output-on-failure

# 5 — Chat
./build/edgai-chat
```

Override the model without changing tier config:

```bash
EDGAI_MODEL_OVERRIDE=llama-3-8b-instruct-q4_k_m.gguf ./build/edgai-chat
```

## Socket protocol (Phase 3 legacy / SOCKET_FALLBACK builds)

`src/rag/engine.py` is frozen. It exists only for builds compiled with
`-DEDGAI_SOCKET_FALLBACK=ON`. The default build uses llama.cpp directly.

The protocol is preserved for reference and for systems that still use the
Phase 3 bridge:

**Input (newline-terminated JSON over Unix socket `/tmp/edgai_rag.sock`):**
```json
{"session_id": "abc123", "text": "what is logarithm", "age_mode": "launchpad"}
```

**Output:**
```json
{
  "session_id": "abc123",
  "text": "A logarithm is the power to which a base must be raised...",
  "sequence_state": "CONCEPT",
  "step_index": 0,
  "question_id": "demo-log-001",
  "can_skip": true,
  "error": null
}
```

## Curriculum DB

The production curriculum database is a private repo. The engine works with any
SQLite database following the schema in `db/schema.sql`. A three-question demo
database ships at `db/demo/demo_curriculum.db` (logarithms, quadratic equations,
arithmetic progression) for development and CI.

Set `EDGAI_DB_PATH` to an absolute path to override the search order.

## Roadmap

- [x] Phase 1 — Python RAG prototype (FTS5 + Ollama, proven architecture)
- [x] Phase 2 — C scaffold: `libedgai`, CMakeLists, public API headers
- [x] Phase 3 — C socket proxy: `libedgai` ↔ `engine.py` bridge, 8 tests passing
- [x] Phase 4 — llama.cpp direct inference, full RAG in C, Ollama removed
- [ ] Phase 5 — whisper.cpp voice input + Piper TTS voice output
- [ ] Phase 6 — D-Bus signals, compositor integration, EduOS desktop shell
- [ ] Phase 7 — Android JNI bridge, Debian OS image packaging

## License

GPL v3 — see [LICENSE](LICENSE)

The engine is open source. The production curriculum database and any fine-tuned
model weights are proprietary and not distributed here.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md)
