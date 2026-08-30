# MetaLog (insight-metalog)

Status: shipped. Package: insight-metalog (single-package repo). The public API is the `insight.metalog` named module (`api/insight/metalog.cppm` — `MetaLogEngine`, `compose()`, `diff()`) over the `insight.metalog.api` contract layer; the envelope internals are the sealed `insight.metalog.detail` module. Tests mirror the concerns under `tests/{engine,serialize,diff,compose,reservoir,determinism}/` and import the `insight.metalog.test` aggregate.

insight-metalog is the compression layer. It turns one bounded window of canonical events into a deterministic MetaLog v0.9.0 document, plus spec-level `compose()` and `diff()` helpers used by detection.

## Input

The producer lifecycle is:

```cpp
insight::metalog::MetaLogEngine engine;
engine.open_window(window_start);
engine.ingest_event(event);
auto doc = engine.close_window(window_end);
```

Input events are `CanonicalEvent` records from insight-canon tokenization, optionally enriched by insight-canon sequence behavior summaries through the shared event stream. Source metadata can be attached with `set_source()` before closing a window.

## Output

The primary output is `MetaLogDocument` with `metalog_version == "0.9.0"`:

| Block | Contents |
|---|---|
| `producer` | Producer name, implementation URI, and producer version. |
| `window` | Start/end timestamps, duration, and `lines_observed`. |
| `source` | Optional service, fleet, host, host count, and tags. |
| `stats` | Top-K templates, frequencies, tail summary, dominant level, optional entropy. |
| `templates` | Optional top-level template-id to template-string map. |
| `behavior` | Top n-grams, dominant path, branching entropy, graph edge count, session flags. |
| `stability` | Cross-window KL/JS divergence, new/vanished template counts, stability score. |
| `provenance` | Inputs that fed a composed document. Empty on raw producer output. |

The package also exports:

| Function | Purpose |
|---|---|
| `to_json(MetaLogDocument)` | Serialize a document to the MetaLog envelope. |
| `to_json(MetaLogDiff)` | Serialize a diff document. |
| `compose(lhs, rhs)` | Merge two documents into one coarser baseline. |
| `diff(previous, current)` | Compute structured change between two documents. |

## What It Does

insight-metalog answers: what did this log stream do during this window?

It compresses raw event volume into a bounded statistical fingerprint: template distribution, tail mass, behavior signatures, branching structure, field-value histograms when enabled, HLL-backed approximate cardinality, and stability against the previous window.

It intentionally does not decide whether a window is anomalous. Cross-window baselines, multi-scale agreement, alert gating, and operator-facing detections belong to insight-eidos detection.

## Configuration

`MetaLogConfig` controls envelope size and optional detail:

| Field | Default | Effect |
|---|---:|---|
| `top_k_size` | `64` | Number of template entries retained before tail summarization. |
| `ngram_size` | `2` | Behavior n-gram order; current supported values are 2 and 3. |
| `top_ngrams_size` | `32` | Number of behavior n-grams emitted. Set to 0 to disable behavior output. |
| `max_ngram_keys` | `4096` | Memory cap for distinct n-gram keys. |
| `emit_stability` | `true` | Emits stability after the first closed window. |
| `top_branching_size` | `64` | Branching entries retained in behavior output. |
| `dominant_path_max_steps` | `8` | Dominant path length cap. |
| `max_param_histograms` | `0` | Number of wildcard positions histogrammed per top-K template. |
| `max_histogram_values` | `64` | Per-slot value cap for field histograms. |

## Contracts

- MetaLog is lossy by design; lossiness is acceptable only while LogCraft-injected incidents remain detectable downstream.
- The document is bounded by top-K, tail summaries, n-gram caps, and histogram caps.
- Every cap the producer applies is DECLARED in the document beside the array it bounds — `stats.top_k_size`, `stats.reservoir_size`, `behavior.top_ngrams_size`, `behavior.branching_size`, `cube.cell_budget` — and only when that array is emitted. SPEC §8 clause 4 makes a declared cap a checkable claim, and §4.2 makes an omitted `branching_size` the positive assertion "no cap", so silence is never the default here. `compose()` declares `stats.reservoir_size` too, and it is the MINIMUM over the caps its inputs declared: a merge is never finer than its coarsest member, and `min` is symmetric where picking a side is not — so the field is commutative by construction, which §12.2 makes a MUST for required fields and this producer holds for this optional one as well. An input that declares no cap makes no claim at all (SPEC §8 clause 4), so it is SKIPPED rather than folded in as a cap of zero; folding it in would break §12.2's identity MUST, since a ZERO document declares no reservoir cap and `compose(A, ZERO)` would then lose the cap `A` declared. Where NEITHER input declared one, the composed document declares none either and admits every salience-positive candidate. The cap is written at the site that enforces it, so the declared value and the admission bound cannot drift apart.
- `max_ngram_keys` is the one bound whose loss is REPORTED rather than inferred, because it refuses a key *before* it is ever counted — unlike `top_ngrams_size`, which cuts a ranking over keys that were all seen. A window that refused observations carries `behavior.dropped_ngram_observations` (OBSERVATIONS, never distinct keys: the distinct count would need exactly the unbounded set the bound exists to refuse); a window that refused none OMITS the key, which SPEC §4 reads as an affirmative zero from `0.7.0` on, so a never-binding producer stays byte-identical to one with no bound at all. `compose()` sums both inputs, an absent input counting as zero, and omits a zero sum.
- `attribution` is reserved by the spec and is not emitted by the current producer.
- `compose()` drops fields that cannot be represented exactly after aggregation, such as some raw branching/histogram detail.
- `diff(previous, current)` treats `current - previous` as the polarity for counts, rates, and structural deltas.

## Consumers

insight-eidos detection consumes raw `MetaLogDocument` windows, composed baselines, and `MetaLogDiff` documents. The insight-eidos explain layer reads the same documents and acute diff to build bounded `ContextPacket`s for explanations.
