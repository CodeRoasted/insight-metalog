// invariant: the whole import surface a benchmark TU needs -- the facade, the sealed detail module
// and canon -- so a TU adds only google-benchmark, which stays textual.
// refs: ADR-3.D4
export module insight.metalog.bench;
export import std;
export import insight.metalog;
export import insight.metalog.detail.stats;
export import insight.metalog.detail.operations;
export import insight.metalog.detail.cube;
export import insight.canon;
