// insight.metalog.bench — shared benchmark infrastructure (ADR-3.D4, the logcraft.bench pattern).
// All benchmark TUs import this instead of spelling out the full import block. Re-exports the
// complete metalog module surface (public facade + the sealed detail module + canon), so a
// benchmark TU needs no further imports beyond google-benchmark (textual, third-party).
export module insight.metalog.bench;
export import std;
export import insight.metalog;
export import insight.metalog.detail.stats;
export import insight.metalog.detail.operations;
export import insight.metalog.detail.cube; // build_closed_cube/compose_cubes/cube_diff_of + BaseRow
export import insight.canon;
