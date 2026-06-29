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
| pkg-config | any | SQLite + ALSA detection |
| SQLite3 dev headers | any | `libsqlite3-dev` on Debian |
| Python | ≥ 3.12 | only for SOCKET_FALLBACK builds |
| RAM | ≥ 2 GB | minimum to load Qwen2.5-0.5B |
| Disk | ≥ 2 GB | model file + build artifacts |

**OS for EduOS OS builds:** Debian bookworm only. Ubuntu is NOT supported — glibc
and library version mismatches cause silent runtime failures on target hardware.

Development on macOS is fine for the C library (libedgai). For packaging EduOS as
an OS image, or for voice builds, use Debian bookworm.

---

## Clone

Always clone with `--recurse-submodules`. The llama.cpp and piper submodules are
pinned to specific commits for API stability — do not update them without explicit
review.

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
    --local-dir ~/.edgai/models/
mv ~/.edgai/models/Qwen2.5-1.5B-Instruct-Q4_K_M.gguf \
   ~/.edgai/models/qwen2.5-1.5b-instruct-q4_k_m.gguf
```

The engine searches for models in this order:
1. `$EDGAI_MODELS_DIR`
2. `/usr/share/edgai/models/`
3. `~/.edgai/models/`

Tests that require a model skip gracefully if none is found.

---

## Build (text-only, no voice)

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

## Build with voice (Phase 5)

Voice requires Debian bookworm with ALSA, Vosk 0.3.45, and the Piper submodule.

### 1. Install ALSA dev headers

```bash
sudo apt-get install libasound2-dev
```

### 2. Download Vosk 0.3.45 prebuilt shared library

```bash
wget https://github.com/alphacep/vosk-api/releases/download/v0.3.45/vosk-linux-x86_64-0.3.45.zip
unzip vosk-linux-x86_64-0.3.45.zip
cp vosk-linux-x86_64-0.3.45/libvosk.so vendor/vosk/
```

### 3. Download the Vosk small English model

```bash
mkdir -p ~/.edgai/models/vosk
cd ~/.edgai/models/vosk
wget https://alphacephei.com/vosk/models/vosk-model-small-en-us-0.15.zip
unzip vosk-model-small-en-us-0.15.zip
```

### 4. Download the Piper voice model

```bash
mkdir -p ~/.edgai/models/piper
cd ~/.edgai/models/piper
wget https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/lessac/medium/en_US-lessac-medium.onnx
wget https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/lessac/medium/en_US-lessac-medium.onnx.json
```

### 5. Build with voice enabled

```bash
cmake -S . -B build -DEDGAI_VOICE=ON
cmake --build build -j$(nproc)
```

### 6. Run the voice demo

```bash
# Test one voice turn (speak a question, hear the answer):
cd build && ./edgai-chat
# At the prompt, type: /voice
```

Voice degrades gracefully when models are missing — the library falls back to
text-only mode without crashing.

---

## CMake options

| Option | Default | Description |
|---|---|---|
| `EDGAI_VOICE` | `OFF` | Enable Phase 5 voice pipeline: Vosk STT + Piper TTS + ALSA. Requires Debian bookworm, `libasound2-dev`, `vendor/vosk/libvosk.so`, and the `vendor/piper` submodule. Enable with `-DEDGAI_VOICE=ON`. |
| `EDGAI_SOCKET_FALLBACK` | `OFF` | Use Phase 3 engine.py socket proxy instead of llama.cpp. Emergency fallback only. Enable with `-DEDGAI_SOCKET_FALLBACK=ON`. |

---

## Test suite

| Test | Needs model | Needs DB | Notes |
|---|---|---|---|
| `test_session` | no (skips LLM) | no | init/destroy lifecycle |
| `test_query` | no (skips LLM) | no | intent + state dispatch |
| `test_rag` | no | no (partial) | preprocessor always runs; retriever skips if no DB |
| `test_voice` | no | no | sanitize, VAD, and session voice field tests — no audio hardware required |
| `test_inference` | yes | no | skips cleanly if no model in search path |
| `test_retriever` | no | yes | FTS5 query against demo_curriculum.db; skips if not found |
| `test_state` | no | no | teaching state machine transitions |
| `test_preprocessor` | no | no | Porter stemmer + stop words + synonyms |

Tests that skip still exit 0 — ctest reports them as passed, with a SKIPPED notice
on stdout.

Set `EDGAI_AUDIO_TEST=1` to run audio hardware tests in `test_voice`. These require
a working ALSA capture device and the Vosk/Piper models installed.

---

## Environment variables

| Variable | Example | Effect |
|---|---|---|
| `EDGAI_MODELS_DIR` | `/home/user/.edgai/models` | First search path for GGUF files |
| `EDGAI_DB_PATH` | `/path/to/curriculum.db` | Overrides all built-in DB search paths |
| `EDGAI_IS_MOBILE` | `1` | Forces mobile RAM tier (lower context size) |
| `GGML_METAL` | `0` | Disable Metal GPU on macOS (CPU only) |
| `EDGAI_VOSK_MODEL` | `/path/to/vosk-model-small-en-us-0.15` | Vosk model directory (Phase 5) |
| `EDGAI_VOICE_MODEL` | `/path/to/en_US-lessac-medium.onnx` | Piper voice model path (Phase 5) |
| `EDGAI_ALSA_DEVICE` | `hw:1,0` | ALSA device for capture and playback (Phase 5); defaults to `default` |
| `EDGAI_AUDIO_TEST` | `1` | Enable audio hardware tests in test_voice (Phase 5) |

---

## engine.py — frozen

`src/rag/engine.py` is the Phase 3 Python socket proxy. It is **frozen**. Do not
modify it. It exists only as a fallback for `EDGAI_SOCKET_FALLBACK=ON` builds.
All active development happens in the C inference stack.

To run the socket fallback:

```bash
python3 src/rag/engine.py &
cmake -S . -B build -DEDGAI_SOCKET_FALLBACK=ON
cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)
```

---

## Repo structure

```
eduos-ai-engine/
├── include/edgai/          public API headers
│   ├── edgai.h             main session/query/voice_turn types
│   ├── edgai_rag.h         RAG result types + retrieve API
│   └── edgai_types.h       enums, session struct (inc. voice fields)
├── src/
│   ├── core/               session init, query dispatch, state machine
│   ├── inference/          llama.cpp tier selection, model loader, backend
│   ├── rag/                preprocessor, retriever, ranker, formatter
│   ├── voice/              Phase 5: audio_capture, vad, sanitize, transcribe, speak
│   └── dbus/               Phase 6 D-Bus signal stubs
├── tests/
│   ├── test_voice.c        Phase 5 tests: sanitize (T1–T8), VAD (T9–T12), session (T13)
│   └── fixtures/voice/     WAV fixtures generated at test runtime
├── db/
│   ├── schema.sql          canonical curriculum schema
│   └── demo/               demo_curriculum.db (3 WAEC questions)
├── vendor/
│   ├── llama.cpp/          git submodule — pinned to 0ed235ea2, do not update casually
│   ├── piper/              git submodule — OHF-Voice/piper1-gpl @ d6975e2 (Phase 5)
│   └── vosk/               vosk_api.h + libvosk.so (download separately, not committed)
├── tools/
│   └── chat.c              interactive CLI with /voice, /voiceon, /voiceoff commands
├── bindings/android/jni/   Phase 7 JNI stub
└── src/rag/engine.py       frozen Phase 3 socket proxy
```

---

## Phase map

| Phase | Description | Status |
|---|---|---|
| 1 | Python RAG prototype (`engine.py` + SQLite FTS5) | done |
| 2 | C scaffold — headers, CMakeLists, folder structure | done |
| 3 | libedgai socket proxy — C library talks to engine.py | done |
| 4 | Direct llama.cpp inference — replaces socket proxy | done |
| 5 | Voice — Vosk STT + Piper TTS + ALSA + VAD + sanitize | **done** |
| 6 | D-Bus signals — OS integration (signals.c) | stub only |
| 7 | Android JNI + Debian OS image packaging | stub only |

---

## Naming conventions

All code in this repo follows these conventions:

- **Functions:** `edgai_module_verb()` — e.g. `edgai_speak_init()`, `edgai_vad_process()`
- **Constants:** `EDGAI_UPPER_SNAKE_CASE`
- **Types:** `EdgaiPascalCase`
- **Local variables:** `lower_snake_case`

The library was previously named `libeduos`. All `eduos_` function prefixes and
`EDUOS_` macro prefixes are now `edgai_` and `EDGAI_` respectively. Any contributor
examples or documentation using the old prefix should be updated.

---

## Voice pipeline architecture (Phase 5)

```
mic (ALSA, 16 kHz S16_LE mono)
  │
  ▼
audio_capture.c  — EdgaiAudioCapture, 4096-frame ALSA periods
  │
  ├──► vad.c     — energy-based VAD; 20 ms frames; SILENCE→SPEECH→DONE/TIMEOUT
  │               (800 ms trailing silence = done, 10 s max)
  │
  └──► transcribe.c  — feeds PCM to Vosk; returns heap-allocated transcript
          │
          ▼
       edgai_query()  (existing LLM + RAG pipeline)
          │
          ▼
       sanitize.c  — strips LaTeX/Markdown/URLs before TTS
          │
          ▼
       speak.c  — Piper TTS → S16_LE 22050 Hz → ALSA playback
                  barge-in: session->speak_interrupt checked per ALSA period
```

Memory tiers:
- **2 GB tier** (`session->is_mobile = true`): Vosk model and Piper context are
  loaded at the start of each `edgai_voice_turn()` call and freed immediately after.
  This keeps resident memory under 1.8 GB at the cost of ~2 s load time per turn.
- **4 GB+ tier**: Vosk model is loaded at `edgai_init()` and stays resident.
  Piper context is also kept resident. Each voice turn reuses the loaded models.

The compile-time flag `EDGAI_VOICE_ENABLED` (set by `-DEDGAI_VOICE=ON`) gates all
ALSA, Vosk, and Piper code. A text-only build (`-DEDGAI_VOICE=OFF`, the default)
has zero voice dependencies and compiles on macOS, Android, and headless Linux.

---

## Do NOT

- Modify `src/rag/engine.py` — it is frozen.
- Commit GGUF model files — they are in `.gitignore` and are too large for git.
- Commit `vendor/vosk/libvosk.so` — it is in `.gitignore` and must be downloaded.
- Run `git submodule update` without explicit intent — submodules are pinned.
- Add external C dependencies beyond libc, SQLite3, llama.cpp, ALSA, Vosk, Piper.
- Add Python dependencies to the non-fallback path.
- Use Ubuntu for EduOS OS builds — Debian bookworm only.
- Use cloud APIs — the engine must run fully offline.
- Add LaTeX formatting to LLM output — the sanitizer strips it for TTS, and target
  users are secondary school students on low-res screens, not mathematicians.

---

## Questions

Open an issue or contact @davidokocha086.
