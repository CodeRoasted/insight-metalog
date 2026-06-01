# insight-metalog

**insight-metalog** — MetaLog v0.5.0 producer.

`insight_metalog` consumes an event sequence from `insight_canon` and produces a **bounded statistical fingerprint** of a window of log behaviour: composition, session framing, HLL-backed field-cardinality estimation, transition-stability ratios, and diff-encoded deltas between windows.

It is the reference implementation of the open [MetaLog specification](https://github.com/CodeRoasted/metalog-spec) and the direct upstream of the detection layer in **insight-eidos**.

The MetaLog format is published as an open specification at [CodeRoasted/metalog-spec](https://github.com/CodeRoasted/metalog-spec).

## Pipeline

```text
Raw logs
  insight-canon   ->  CanonicalEvent  ->  event stream
  insight-metalog ->  bounded behavioral fingerprint
  insight-eidos   ->  detection reports + explain packets
```

> **insight-canon** (`insight_canon`): Tokenization — format strategies, parser, Drain clustering,
> tokenizer facade, producing the canonical event representation — then Sequence — event ordering,
> n-gram model, transition graph.

## Determinism

A MetaLog document is a **deterministic** function of its input window — the same canonical events produce a byte-identical fingerprint, and `compose()` / `diff()` are deterministic too, so *same inputs ⇒ same diff* on any machine (bit-identity is a standing golden-hash gate, built on canon's `det_math`). This is the **format** link of the pipeline's end-to-end determinism: content (`insight-canon`) → transport (`coderoast-ipc`) → format (`insight-metalog`).

## Package

| Field | Value |
|---|---|
| Conan name | `insight_metalog` |
| Version | `1.4.1` |
| Spec conformance | MetaLog v0.5.0 |
| Visibility | CodeRoast-owned package |

## Requirements

- GCC 13+ with C++23
- CMake 3.28+
- Ninja
- Conan 2.27+
- clang-tidy-18 (lint only)

## Quick Start

```sh
# Local CodeRoast workspace iteration
malf build .
malf test .

# Or directly
conan install . \
  --profile:host=linux-gcc13-release \
  --profile:build=linux-gcc13-release \
  --build=missing
cmake --preset conan-release
cmake --build build --preset conan-release
ctest --test-dir build --output-on-failure

# Create the Conan package
conan create . \
  --profile:host=linux-gcc13-release \
  --profile:build=linux-gcc13-release \
  --build=missing \
  --build-test=missing
```

## CMake Options

| Option | Default | Description |
|---|---|---|
| `INSIGHT_METALOG_ENABLE_INTEGRATION_TESTS` | OFF | Enable LogCraft integration tests (requires `logcraft_core` in Conan cache) |

## CI

| Workflow | Trigger | Description |
|---|---|---|
| `CI` | PR to `main` | Build + test via `conan create` |
| `Release publish` | Push `vX.Y.Z` tag | Build and attach `insight_metalog-X.Y.Z.tgz` to GitHub Release |
| `Validate GitHub Actions` | PR touching `.github/workflows/**` | `actionlint` |

### Release token

The CI fetches `insight_canon` from the `insight-canon` GitHub Release. Set the repository secret `INSIGHT_CANON_RELEASE_TOKEN` to a fine-grained PAT with read access to `CodeRoasted/insight-canon` releases.

## Technical Docs

MetaLog producer phase reference lives in [technical_docs/](technical_docs/README.md).

## License

Source license: BUSL-1.1 (Business Source License 1.1) — source-available, converting to an open license over time. The upstream tokenization layer `insight-canon` is Apache-2.0; the downstream detection layer `insight-eidos` is closed source. See [LICENSE](LICENSE).
