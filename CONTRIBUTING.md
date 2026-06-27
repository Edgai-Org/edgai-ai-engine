# Contributing to eduos-ai-engine

EduOS is a curriculum-driven, offline-first AI teaching engine for African schools.
It runs on low-spec hardware with no internet connection. Keep that constraint in mind
for every decision you make here.

---

## Prerequisites

| Requirement | Version | Notes |
|---|---|---|
| Git | any | needed for submodule init |
| CMake | ≥ 3.16 | |
| C compiler | C11 | GCC or Clang |
| C++ compiler | C++17 | llama.cpp requires it |
| pkg-config | any | SQLite detection |
| SQLite3 dev headers | any | `libsqlite3-dev` on Debian |
| Python | ≥ 3.12 | only for SOCKET_FALLBACK builds |
| RAM | ≥ 2 GB | minimum to load Qwen2.5-0.5B |
| Disk | ≥ 2 GB | model file + build artifacts |

**OS for EduOS OS builds:** Debian bookworm only. Ubuntu is NOT supported — glibc
and library version mismatches cause silent runtime failures on target hardware.

Development on macOS is fine for the C library (libeduos). For packaging EduOS as
an OS image, use Debian bookworm.

---

## Clone

Always clone with `--recurse-submodules`. The llama.cpp submodule is pinned to a
specific commit for API stability — do not update it without explicit review.

```bash
git clone --recurse-submodules https://github.com/EduOS-Org/eduos-ai-engine.git
cd eduos-ai-engine
```

If you already cloned without submodules:

```bash
git submodule update --init --recursive
```

---

## Download a model

At least one GGUF model is required to run the LLM inference tests. The default
for a 4 GB development machine is Qwen2.5-1.5B-Instruct Q4_K_M.

```bash
pip install huggingface_hub[cli]
huggingface-cli download bartowski/Qwen2.5-1.5B-Instruct-GGUF \
    Qwen2.5-1.5B-Instruct-Q4_K_M.gguf \
    --local-dir ~/.eduos/models/
mv ~/.eduos/models/Qwen2.5-1.5B-Instruct-Q4_K_M.gguf \
   ~/.eduos/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
```

The engine searches for models in this order:
1. `$EDUOS_MODELS_DIR`
2. `/usr/share/eduos/models/`
3. `~/.eduos/models/`

Tests that require a model skip gracefully if none is found.

---

## Build

```bash
cmake -S . -B build
cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)
```

Run tests from the **repo root** (tests resolve `db/demo/demo_curriculum.db`
relative to the working directory):

```bash
cd build && ctest --output-on-failure
```

---

## CMake options

| Option | Default | Description |
|---|---|---|
| `EDUOS_SOCKET_FALLBACK` | `OFF` | Use Phase 3 engine.py socket proxy instead of llama.cpp. Emergency fallback only. Enable with `-DEDUOS_SOCKET_FALLBACK=ON`. |

---

## Test suite

| Test | Needs model | Needs DB | Notes |
|---|---|---|---|
| `test_session` | no (skips LLM) | no | init/destroy lifecycle |
| `test_query` | no (skips LLM) | no | intent + state dispatch |
| `test_rag` | no | no (partial) | preprocessor always runs; retriever skips if no DB |
| `test_voice` | no | no | Phase 5 stub — always passes |
| `test_inference` | yes | no | skips cleanly if no model in search path |
| `test_retriever` | no | yes | FTS5 query against demo_curriculum.db; skips if not found |
| `test_state` | no | no | teaching state machine transitions |
| `test_preprocessor` | no | no | Porter stemmer + stop words + synonyms |

Tests that skip still exit 0 — ctest reports them as passed, with a SKIPPED notice
on stdout.

---

## Environment variables

| Variable | Example | Effect |
|---|---|---|
| `EDUOS_MODELS_DIR` | `/home/user/.eduos/models` | First search path for GGUF files |
| `EDUOS_DB_PATH` | `/path/to/curriculum.db` | Overrides all built-in DB search paths |
| `EDUOS_IS_MOBILE` | `1` | Forces mobile RAM tier (lower context size) |
| `GGML_METAL` | `0` | Disable Metal GPU on macOS (CPU only) |

---

## engine.py — frozen

`src/rag/engine.py` is the Phase 3 Python socket proxy. It is **frozen**. Do not
modify it. It exists only as a fallback for `EDUOS_SOCKET_FALLBACK=ON` builds.
All active development happens in the C inference stack.

To run the socket fallback:

```bash
python3 src/rag/engine.py &
cmake -S . -B build -DEDUOS_SOCKET_FALLBACK=ON
cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)
```

---

## Repo structure

```
eduos-ai-engine/
├── include/eduos/          public API headers
│   ├── eduos.h             main session/query types
│   ├── eduos_rag.h         RAG result types + retrieve API
│   └── eduos_types.h       enums, session struct
├── src/
│   ├── core/               session init, query dispatch, state machine
│   ├── inference/          llama.cpp tier selection, model loader, backend
│   ├── rag/                preprocessor, retriever, ranker, formatter
│   ├── voice/              Phase 5 TTS/STT stubs
│   └── dbus/               Phase 6 D-Bus signal stubs
├── tests/                  one .c file per test, 8 total
├── db/
│   ├── schema.sql          canonical curriculum schema
│   └── demo/               demo_curriculum.db (3 WAEC questions)
├── vendor/llama.cpp/       git submodule — pinned, do not update casually
├── bindings/android/jni/   Phase 7 JNI stub
└── src/rag/engine.py       frozen Phase 3 socket proxy
```

---

## Phase map

| Phase | Description | Status |
|---|---|---|
| 1 | Python RAG prototype (`engine.py` + SQLite FTS5) | done |
| 2 | C scaffold — headers, CMakeLists, folder structure | done |
| 3 | libeduos socket proxy — C library talks to engine.py | done |
| 4 | Direct llama.cpp inference — replaces socket proxy | **done** |
| 5 | Voice — TTS (speak.c) + STT (transcribe.c) | stubs only |
| 6 | D-Bus signals — OS integration (signals.c) | stub only |
| 7 | Android JNI + Debian OS image packaging | stub only |

---

## Do NOT

- Modify `src/rag/engine.py` — it is frozen.
- Commit GGUF model files — they are in `.gitignore` and are too large for git.
- Run `git submodule update` without explicit intent — the submodule is pinned to a
  specific commit (`0ed235ea2c17a19fc8238668653946721ed136fd`) for API stability.
- Add external C dependencies — zero deps beyond libc, SQLite3, and llama.cpp.
- Add Python dependencies to the non-fallback path.
- Use Ubuntu for EduOS OS builds — Debian bookworm only.
- Use cloud APIs — the engine must run fully offline.
- Add LaTeX formatting to LLM output — target users are secondary school students
  reading on low-res screens, not mathematicians.

---

## Questions

Open an issue or contact @davidokocha086.
