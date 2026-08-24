# insight-metalog

**insight-metalog** — MetaLog v0.9.0 producer.

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
| Spec conformance | MetaLog v0.9.0 — **the producer clears §8 clauses 1 and 4**, and the published determinism evidence validates CONFORMANT, see below |
| Visibility | CodeRoast-owned package |

### Conformance, stated exactly

MetaLog `SPEC.md` §8 clause 1 makes conformance a machine check: *"Every MetaLog it emits
validates against `schema/metalog.v0.schema.json`"* — *"the schema is the test **for clause
1**"*. Since v0.9.0, clause 4 is machine-decided too — in the shipped validator rather than the
schema, because `maxItems` takes a constant while the bound is a sibling field's value — so a
producer that declares a cap is held to it. Two different things can be measured against those
clauses, and conflating them is how a green gets over-read:

| subject | measured | result |
|---|---|---|
| **what this producer emits** — 19 documents regenerated from source at HEAD | `metalog_validate.py --expect-documents 19` | **0 errors · 0 cap-exceeded · 0 legal-but-undescribed · CONFORMANT** |
| **what we have PUBLISHED** — `coderoast-hub/determinism/metalog.determinism_golden.txt` | same command | **0 errors · 0 legal-but-undescribed · CONFORMANT** (17 documents) |

The published bytes are a **snapshot**, not a live measurement: they can drift from the first
row whenever the producer moves ahead of the published evidence, which is why both are measured
rather than one being inferred from the other. Regenerating the evidence is an outgoing act on
a public surface and not this repo's to take. **They have drifted, and legally:** the snapshot
was cut at MetaLog 0.8.0, the producer now emits 0.9.0, and the snapshot still validates because
0.9.0's three additions are optional — an undeclared cap is not a claim (§8 clause 4).

How the producer got there, since the count moved twice and each move had a different owner:

* **29 of the original 31** were a **schema lag**, not a producer bug — `component` and
  `band_floor` were real members the schema had no description for. `metalog-spec` v0.8.0
  describes them (§3.8, `$defs/cube_axis`), and this producer emits them **unchanged**.
* **The remaining 2** were a genuine producer bug: `stats.top_k[].ordinal_histograms` was written
  as a bare member of a closed object. Its bins ride an **unfrozen** log2 ladder and carry
  `schedule_id`, an engine-side key, so two independent producers would emit incomparable bins —
  it is vendor data, and `SPEC.md` §7 says where vendor data goes. It now ships under
  `stats.top_k[].extensions["fr.coderoast.ordinal_histograms"]`.
* `acquisition` and `service_edges` were legal (they sat at the open root) but **undescribed**, so
  a reader could not tell our error model from the standard's content. Both moved under the
  document-root `extensions` container with the same `fr.coderoast.` prefix. The content is
  unchanged: `acquisition` is our declared error model made machine-readable (it is what lets a
  consumer distinguish *"no cross-route links"* from *"links existed and the grain hid them"*), and
  deleting it would make these documents **less** falsifiable.

`metalog-spec/GOVERNANCE.md` §3 is what decides which side of a disagreement moves: *"If the spec
and the reference implementation disagree, the spec wins, and the reference implementation is
treated as buggy."*

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
