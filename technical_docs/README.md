# insight-metalog Technical Documentation

Technical reference for the insight-metalog library: MetaLog production, composition, diff, and spec conformance.

Cross-repo status, package pins, and planning live in the parent docs: [../../technical_docs/README.md](../../technical_docs/README.md) and [../../technical_docs/ROADMAP.md](../../technical_docs/ROADMAP.md).

## Read Order

1. [metalog.md](phases/metalog.md) — MetaLog v0.5.0 producer lifecycle, document structure, `compose()`, `diff()`, and the `MetaLogEngine` API.

## Pipeline Position

```text
Raw log line
  -> insight-canon tokenization  ->  CanonicalEvent
  -> insight-canon sequence      ->  SequenceEngine summaries
  -> insight-metalog             ->  MetaLogDocument
  -> insight-eidos               ->  DetectionReport + Insight
```

insight-metalog is the compression boundary: it turns an unbounded event stream into a bounded statistical fingerprint that downstream detection and explanation can process without raw log access.

## Cross-Project Map

| Project | Role | Start here |
|---|---|---|
| insight-canon | Upstream: tokenization, sequence, shared core types | [../../insight-canon/technical_docs/README.md](../../insight-canon/technical_docs/README.md) |
| insight-metalog | This repo: MetaLog producer, compose, diff, spec conformance | This folder |
| metalog-spec | Spec that this package implements | [../../metalog-spec/README.md](../../metalog-spec/README.md) |
| insight-eidos | Detection, explain, engine, CLI, and phase-level implementation docs | [../../insight-eidos/technical_docs/README.md](../../insight-eidos/technical_docs/README.md) |
| CodeRoast parent docs | Product strategy, cross-repo status, compatibility matrix, and roadmap | [../../technical_docs/README.md](../../technical_docs/README.md) |

## Key Cross-References

- Parent compatibility matrix: [../../technical_docs/compatibility_matrix.md](../../technical_docs/compatibility_matrix.md)
- insight-canon phase reference: [../../insight-canon/technical_docs/phases/tokenization.md](../../insight-canon/technical_docs/phases/tokenization.md)
- InSight product strategy: [../../technical_docs/product/strategy.md](../../technical_docs/product/strategy.md)
- InSight implementation pipeline: [../../insight-eidos/technical_docs/README.md](../../insight-eidos/technical_docs/README.md)
