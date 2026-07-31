# insight-metalog — the MetaLog producer (BUSL-1.1)

Reference implementation of the open MetaLog specification: consumes canon's
canonical-event stream and produces the bounded structural fingerprint of a
window — the document, `compose()`, and `diff()`. A single Conan package,
`insight_metalog`, rooted at the repo top (`packages.yml`).

## Arrival

- Build/test: `malf build .` / `malf test .` from the repo root.
- Layout: `api/` (named-module surface), `src/`, `tests/`, `benchmarks/`.
- Docs: `technical_docs/README.md`; the producer/document contract is
  `technical_docs/phases/metalog.md`.
- LogCraft integration tests are gated by the CMake option
  `INSIGHT_METALOG_ENABLE_INTEGRATION_TESTS` (needs `logcraft_core` in cache).

## Local traps

- The wire format is OWNED by `../metalog-spec/SPEC.md`; this repo conforms to
  it. A producer change that alters document bytes is a spec/version decision,
  not a local edit — reconcile spec + producer in the same pass.
- A MetaLog document is a deterministic function of its window; byte-identity
  is gate-enforced. Treat any byte drift as a determinism bug first, never as
  noise.
