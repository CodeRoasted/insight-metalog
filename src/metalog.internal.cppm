// refs: ADR-3.D4
// invariant: the one `import std` of the metalog library module graph; every api/ and src/ unit
// imports this module plain to reach std.
export module insight.metalog.internal;
export import std;
