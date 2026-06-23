# Contributing to eduos-ai-engine

Thanks for your interest. EduOS is being built for African students — contributions that move that mission forward are welcome.

## Before you start

Read the architecture overview in `README.md`. Understand what this engine is and what it is not. It is not a general-purpose chatbot. It is a curriculum-driven teaching engine built for low-spec offline hardware.

## Environment requirements

- **OS:** Debian bookworm (required). Do not use Ubuntu — glibc mismatch causes build failures.
- **Python:** 3.10+
- **Ollama:** installed and running (`ollama serve`)
- **Model:** `ollama pull qwen2.5:1.5b`

**Docker (recommended for contributors):**
```bash
docker run -it --rm -v $(pwd):/eduos debian:bookworm bash
```

## Setup

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

## What to work on

Check open issues. Priority areas:

- `src/rag/` — RAG pipeline improvements (retrieval, preprocessing, intent detection)
- `src/core/` — C libeduos scaffold (Phase 3)
- `tests/` — test coverage for RAG pipeline
- `db/schema.sql` — schema improvements
- `db/demo/` — demo curriculum content

## What NOT to work on

- Do not modify the curriculum database schema without discussion — it affects the entire pipeline
- Do not add cloud dependencies — this engine must run fully offline
- Do not add Python dependencies without justification — every dependency must work on Debian bookworm and eventually be replaced by C

## Pull request process

1. Fork the repo
2. Create a branch: `git checkout -b feature/your-feature`
3. Make your changes
4. Test against the demo curriculum DB
5. Open a PR with a clear description of what you changed and why

## Code style

- Python: follow existing style, no external formatters required
- C (Phase 3+): C11, no external dependencies beyond what's in `vendor/`
- Comments: explain WHY, not what

## Questions

Open an issue or find @davidokocha086 on X.