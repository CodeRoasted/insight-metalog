# insight-metalog

**Phase 3 of the InSight pipeline** — MetaLog v0.2.0 producer.

`insight_metalog` consumes an event sequence (Phase 2 output from `insight_sequence`) and produces a **bounded statistical fingerprint** of a window of log behaviour: composition, session framing, HLL-backed field-cardinality estimation, transition-stability ratios, and diff-encoded deltas between windows.

It is the reference implementation of the open [MetaLog specification](https://github.com/coderoast-dev/metalog-spec) and the direct upstream of the detection layer (`insight_detection`).

## Pipeline

```text
Raw logs
  -> Phase 1: tokenization -> CanonicalEvent         (insight-canon)
  -> Phase 2: sequence     -> event stream            (insight-canon)
  -> Phase 3: MetaLog      -> bounded fingerprint     (this repo)
  -> Phase 4: detection    -> precision-first reports (insight-eidos)
  -> Phase 5: explain      -> human explanation layer (insight-eidos)
```

> **Phase 1 — Canonical event** (`insight_tokenization`): Format strategies,
> parser, Drain clustering, tokenizer facade. Produces the canonical
> event representation that all downstream phases consume.

> **Phase 2 — Sequence** (`insight_sequence`): Event ordering, n-gram model,
> transition graph. Produces the event stream / transition model.

## Package

| Field | Value |
|---|---|
| Conan name | `insight_metalog` |
| Version | `1.2.8` |
| Spec conformance | MetaLog v0.2.0 |
| Visibility | Closed source |

## Requirements

- GCC 13+ with C++23
- CMake 3.28+
- Ninja
- Conan 2.27+
- clang-tidy-18 (lint only)

## Quick Start

```sh
# Install with malf (from coderoast parent repo)
malf build .

# Or directly
conan install . \
  --profile:host=.conan2/profiles/linux-gcc13-release \
  --profile:build=.conan2/profiles/linux-gcc13-release \
  --build=missing
cmake --preset conan-release
cmake --build build --preset conan-release
ctest --test-dir build --output-on-failure

# Create the Conan package
conan create . \
  --profile:host=.conan2/profiles/linux-gcc13-release \
  --profile:build=.conan2/profiles/linux-gcc13-release \
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

The CI fetches `insight_sequence` (and its transitive dependencies) from the `insight-canon` GitHub Release. Set the repository secret `INSIGHT_RELEASE_TOKEN` to a fine-grained PAT with read access to `coderoast-dev/insight` releases.
