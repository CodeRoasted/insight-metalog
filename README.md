# insight-metalog

**insight-metalog** — MetaLog v0.6.0 producer.

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

> **insight-canon** (`insight_canon`): Tokenization — format strategies, parser, stateless masking,
> tokenizer facade, producing the canonical event representation — then Sequence — event ordering,
> n-gram model, transition graph.

## Determinism

A MetaLog document is a **deterministic** function of its input window — the same canonical events produce a byte-identical fingerprint, and `compose()` / `diff()` are deterministic too, so *same inputs ⇒ same diff* on any machine (bit-identity is a standing golden-hash gate, built on canon's `det_math`). This is the **format** link of the pipeline's end-to-end determinism: content (`insight-canon`) → transport (`coderoast-ipc`) → format (`insight-metalog`).

## Package

| Field | Value |
|---|---|
| Conan name | `insight_metalog` |
| Spec target | MetaLog v0.6.0 — **not conformant today**, see below |
| Visibility | CodeRoast-owned package |

### Conformance, stated exactly

MetaLog `SPEC.md` §8 clause 1 makes conformance a machine check: *"Every MetaLog it emits
validates against `schema/metalog.v0.schema.json`"*, and §8 closes with *"The schema is the
test."* Measured on the 17 documents this project publishes as determinism evidence
(`coderoast-hub/determinism/metalog.determinism_golden.txt`), against the published
`metalog-spec/schema/metalog.v0.schema.json`: **31 validation errors**. So the honest
statement is that this producer **targets** v0.6.0 and does not meet clause 1 at HEAD.

All 31 are one species — a field emitted inside a schema object declared
`additionalProperties: false` — and they split two ways, which decides who fixes what:

| emitted field | errors | in SPEC prose? | in the schema? | side at fault |
|---|---|---|---|---|
| `stats.top_k[].component` | 28 | no | no | **this producer** |
| `stats.top_k[].ordinal_histograms` | 2 | no | no | **this producer** |
| `cube.axes[].band_floor` | 1 | **yes** (§16.2, §16.10) | no | **the schema** |

`metalog-spec/GOVERNANCE.md` §3 decides the first two: *"If the spec and the reference
implementation disagree, the spec wins, and the reference implementation is treated as
buggy."* Two undescribed fields on a closed object is that case; `SPEC.md` §7 already names
where vendor data belongs (`extensions`, reverse-DNS-keyed) and says to open an issue for a
field the spec lacks. `band_floor` is the opposite case — the spec's normative prose defines
it as an axis collapse stamp and makes it load-bearing (*"a truncated granularity MUST NOT
be mistakable for a full one"*), and only `$defs/cube_axis` has not caught up.

This row will read `Spec conformance | MetaLog v0.6.0` again when the count is zero, and not
before.

## Requirements

- GCC 16 with C++23
- CMake 3.28+ with C++23 named-module support
- Ninja
- Conan 2.x
- clang-tidy (lint only)

## Quick Start

```sh
# Local CodeRoast workspace iteration
malf build .
malf test .

# Or directly
conan install . \
  --profile:host=linux-gcc16-release \
  --profile:build=linux-gcc16-release \
  --build=missing
cmake --preset conan-release
cmake --build build --preset conan-release
ctest --test-dir build --output-on-failure

# Create the Conan package
conan create . \
  --profile:host=linux-gcc16-release \
  --profile:build=linux-gcc16-release \
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
