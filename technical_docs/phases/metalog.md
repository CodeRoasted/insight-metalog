# MetaLog (insight-metalog)

Status: shipped. Package: insight-metalog (single-package repo). The public API is the `insight.metalog` named module (`api/insight/metalog.cppm` — `MetaLogEngine`, `compose()`, `diff()`) over the `insight.metalog.api` contract layer; the envelope internals are the sealed `insight.metalog.detail` module. Tests mirror the concerns under `tests/{engine,serialize,diff,compose,reservoir,determinism}/` and import the `insight.metalog.test` aggregate.

insight-metalog is the compression layer. It turns one bounded window of canonical events into a deterministic MetaLog v0.5.0 document, plus spec-level `compose()` and `diff()` helpers used by detection.

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

The primary output is `MetaLogDocument` with `metalog_version == "0.5.0"`:

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
| `compute_template_id(template)` | Produce the spec content hash (`h:` plus 32 hex chars). |

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
| `template_emission` | `Inline` | Inline, deduplicated, or id-only template strings. |
| `top_branching_size` | `64` | Branching entries retained in behavior output. |
| `dominant_path_max_steps` | `8` | Dominant path length cap. |
| `max_param_histograms` | `0` | Number of wildcard positions histogrammed per top-K template. |
| `max_histogram_values` | `64` | Per-slot value cap for field histograms. |

## Contracts

- MetaLog is lossy by design; lossiness is acceptable only while LogCraft-injected incidents remain detectable downstream.
- The document is bounded by top-K, tail summaries, n-gram caps, histogram caps, and template emission mode.
- `attribution` is reserved by the spec and is not emitted by the current producer.
- `compose()` drops fields that cannot be represented exactly after aggregation, such as some raw branching/histogram detail.
- `diff(previous, current)` treats `current - previous` as the polarity for counts, rates, and structural deltas.

## Consumers

insight-eidos detection consumes raw `MetaLogDocument` windows, composed baselines, and `MetaLogDiff` documents. The insight-eidos explain layer reads the same documents and acute diff to build bounded `ContextPacket`s for explanations.
