# insight-metalog Technical Documentation

Technical reference for the insight-metalog library: MetaLog production, composition, diff, and spec conformance.

## Read Order

1. [metalog.md](phases/metalog.md) — MetaLog v0.9.0 producer lifecycle, document structure, `compose()`, `diff()`, and the `MetaLogEngine` API.

## Pipeline Position

```text
Raw log line
  -> insight-canon tokenization  ->  CanonicalEvent
  -> insight-metalog             ->  MetaLogDocument
  -> insight-eidos               ->  DetectionReport + Insight
```

insight-metalog is the compression boundary: it turns an unbounded event stream into a bounded statistical fingerprint that downstream detection and explanation can process without raw log access. It implements the open [MetaLog specification](https://github.com/CodeRoasted/metalog-spec); **insight-canon** is the upstream tokenizer and **insight-eidos** the downstream detector.
