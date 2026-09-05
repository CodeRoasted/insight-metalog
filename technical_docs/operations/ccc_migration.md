# The Code & Comment as Contract migration — `insight-metalog`, the ledger

This file is the **evidence** of the migration of `insight-metalog` to the closed comment grammar:
for every unit converted, the claims its deleted comments carried, the cold-reader interrogation
that tested whether the code alone still carries them, and where every claim the code did not
carry was re-homed. It is a record, written as each unit lands; it decides nothing. The protocol
it follows is `ADR-26.D8`, its operator's order of steps is `OPS-8`, and the grammar is
`ADR-26.D5`; the gate that judges a converted unit is the second phase of `malf format --check`,
reading post-format text.

**Claim classes.** Every comment block of a unit was read before deletion and each claim it
carried was classed: **M** a mirror of the code beside it · **H** history or intention (*"this
used to"*, *"will"*) · **C** a contract (`pre` / `post` / `invariant` / `assert`) · **X** a
citation · **R** rationale — a why, a measurement, a rejected alternative, an ordering that is
content. M and H are deleted; C becomes tagged lines; X becomes `refs:`; **R is held** until a
fresh agent, reading the converted working tree only and never git, answers one neutral question
per R claim. *Recovered* means the prose was redundant and stays deleted. *Not recovered* or
*wrong* means the claim needs a home above the comment rung — a law block, a paragraph in an
owning doc, or a single `note:` — and never comes back as prose. **A claim that cannot be
re-derived today is deleted and becomes a finding**, never re-homed (`OPS-8.S9`): re-asserting an
unsourced measurement is the conversion inventing a fact and signing it.

**Witnesses every unit carries.** Comment-only: the code token stream of each file, comments
removed and whitespace dropped, is byte-identical to `HEAD`'s. Grammar: `malf format --check
<unit>` reports zero would-be violations for the unit. Behaviour: `malf test insight-metalog`
green on clang-21 and on gcc-16 after the conversion, against a green baseline taken before the
first conversion. A binary diff is not a witness — `__LINE__` legitimately changes when a comment
is deleted.

**Grammar baseline, 2026-09-05, before any conversion (the gate's own count, re-measured by this
lane and equal to the pilot's):** 70 files, 6 868 comment lines, 6 701 would-be violations —
bare 5 910, trailing 378, spacer 367, `///` 28, suppression without a why 13, ruler 4, tag
mid-line 1; tool forms already present 167. Zero tagged forms existed in the repo.

**Behaviour baseline, 2026-09-05, both toolchains equal:** `malf test insight-metalog` —
**297 of 297 tests passing** on clang-21 (`linux-clang21-libcxx-release`) and on gcc-16
(`linux-gcc16-release`). One ctest population; `benchmarks/` is built and never run by `malf
test`, and `scripts/det_harness` is an inventory target linked but not tested.

**This repo's shape.** 88 % of the violations are bare prose (5 910 of 6 701) and only 0.4 % are
`///` (28). Like `insight-canon` and unlike `coderoast-ipc`/`insight-twin` (46-47 % `///`, which
deletes mechanically), the per-unit cost here is the reading and classing, not the stripping.

**Law numbering.** This repo declares **zero** law blocks and this lane was instructed not to mint
one: `D-LSRC-n` numbers are workspace-global, append-only and checked dense, so a lane that picks
its own number collides with a sibling (`OPS-8.O4`). Every site this run found that needs a law
block is recorded below as a **stopped unit** with the statement the law would carry, for the
pilot to issue a number. The token is not spelled in this file: the registry lint reads a spelled
`D-LSRC-<digits>` anywhere as a declaration.

---

## The unit plan and the `SRC-<code>` boundary

70 files and 6 701 would-be violations, ordered **source before tests** (`OPS-8.S2`). The order is
additionally constrained by `OPS-8.O5`: a **declaring** site for a retired `SRC-<code>` needs a
law block, which this lane may not mint, so units are ordered so that every unit converted here
contains **citing sites only**.

`registry_grammar_lint`'s `src_codes_present` classes a site as a declaration by **position** — a
site in a `.cppm`/`.hpp`/`.h`/`.ipp`, or a site in a `.cpp`'s first 40 lines. Measured on this
repo, the declaring-position `SRC-` sites are:

| file | occurrences | position class |
|---|---|---|
| `api/metalog.api.cppm` | 50 | interface unit |
| `api/metalog.cppm` | 23 | interface unit |
| `src/stats/metalog.detail.stats.cppm` | 1 | interface unit |
| `scripts/service_edges_overcap_scenario.hpp` | 1 | interface unit |
| `tests/engine/test_ordinal_histograms.cpp` | 1 (line 1) | leading block |
| `tests/engine/test_span_edges.cpp` | 1 (line 2) | leading block |
| `tests/stats/test_stats.cpp` | 1 (line 17) | leading block |

Every other `SRC-` site in the repo is a body site in a `.cpp` past line 40 and is therefore a
**citation**, which keeps `refs: SRC-<code>` unchanged (`OPS-8.O5`).

Violation count by unit, source tier first: `src/` root 8 · `src/stats` 312 · `src/cube` 383 ·
`src/engine` 361 · `src/operations` 431 · `src/serialization` 361 · `api/` 1 414. Harness tier:
`benchmarks/` 241 · `scripts/` 545 · `test_package/` 16. Test tier: `tests/operations` 1 402 ·
`tests/engine` 268 · `tests/serialization` 252 · `tests/reservoir` 244 · `tests/cube` 218 ·
`tests/determinism` 173 · `tests/stats` 61 · `tests/metalog.test.cppm` 11.

---

## Unit 1 — `src/` root · `metalog.internal.cppm` + `metalog.api.impl.cpp` (2 files, 8 would-be violations)

The calibration unit: the two files that sit outside every `src/` subdirectory, small enough that
the protocol's cost and the interrogation's signal are measurable before the large units. Nine
comment lines, eight of them violations (six bare, one spacer, two trailing — the spacer and one
bare share a block), one tool form (a namespace closer).

**Census (`OPS-8.S4`).** Zero `NOLINT` of any spelling, zero `/*name*/` or `/*name=*/`, zero
`clang-format off`, zero `wall-clock:`, zero `SPDX-License-Identifier:` in either file. The only
machine-read form is the `} // namespace insight::metalog` closer, which the stripper kept. Census
after the strip: identical. No census decision was needed.

**Stripper cross-check (`OPS-8.S5`).** No suppression in either file, so
`removed == violations − (suppression-without-why + trailing-nolint)` reduces to
`removed == violations`: `metalog.internal.cppm` 2 == 2, `metalog.api.impl.cpp` 6 == 6, kept
0 and 1 (the closer).

| id | class | the claim, as the deleted comment stated it | disposition |
|---|---|---|---|
| X1 | X | `insight.metalog.internal` exists because of the named-modules ruling `ADR-3.D4` | **`// refs: ADR-3.D4`** at the module declaration |
| C1 | C | it is the tier's single `import std` manifest — *"lone import-std manifest"* | **`// invariant:`** at the module declaration, written after the interrogation (see below) |
| M1 | M | the `export { using std::int64_t; … }` block re-exports the global C fixed-width type spellings | deleted — the block below the comment is the statement |
| H1 | H | *"metalog source uses unqualified `uint64_t`/`int64_t`/`size_t` etc."* | deleted — **measured FALSE**, see the findings below |
| M2 | M | `metalog.api.impl.cpp` is the implementation unit of `insight.metalog.api` | deleted — `module insight.metalog.api;` on the first code line says it |
| M3 | M | it is the out-of-line home of `TemplateRegistry`'s named members | deleted — the six definitions are the statement |
| R1 | R | `TemplateRegistry`'s special members are defaulted in the class body, not here | held → Q2 |
| M4 | M | `import insight.metalog.internal;` brings `unordered_map` / `string` / `string_view` / `size_t` | deleted — a trailing mirror of the internal module's own export list |
| R2 | R | `import insight.canon;` brings `TemplateId`, *"the module-attached map key"* | held → Q3 |

**Interrogation** — one fresh agent, three questions, 39 tool uses, 96 k tokens, 4.6 minutes.
Transcript checked: `GIT COMMANDS RUN: none`, and no `git` invocation appears in its tool calls.

| Q | claim | verdict | what the agent found, and where |
|---|---|---|---|
| Q1 | X1 + C1 | **recovered**, high | `ADR-3.D4` MUST 6 (*"`import std` everywhere in a unit or nowhere"*), the module-conformance lint's RULE-2 (*"internal std-manifest must be imported PLAIN"*), and its own sweep: `src/metalog.internal.cppm` is the only `export import std;` under `api/` or `src/`, and no unit in either directory says `import std;`. It also observed that the converted sibling manifests `insight-canon/core/src/canon.internal.cppm` and `logcraft/core/src/core.internal.cppm` each state the rule in an `invariant:` line and this one did not |
| Q2 | R1 | **recovered**, high — and it found a FALSE claim in another unit | the split survives only as *"function bodies live in the implementation unit"*; `DONE.md`'s 2026-09-04 entries record the gcc `_Hashtable` half measured inert on gcc-16.2 and the MSVC `LNK2005` half **refuted** by an eidos golden run on the probe branch, after which the special members moved back into the class body. See the findings below |
| Q3 | R2 | **recovered**, high | `insight::TemplateId` (canon's 16-byte truncated SHA-256 of the masked template) plus the `std::hash<insight::TemplateId>` specialization canon ships in the same module interface, which is what supplies the `unordered_map`'s Hash; canon's own comment states the reason. It bounded its own answer: it is confident on the entity and the hash property, less so on the language rule that forces the explicit `import insight.canon;` in this TU |

**Score: 3 of 3 recovered, 0 not recovered, 0 wrong.**

**Dispositions.** Nothing was re-homed on a *not recovered*, because there was none. One line was
**added** on a re-classing rather than on a reader verdict: the *"lone import-std manifest"* claim
is a **contract** (C), not rationale, and `OPS-8.S3` sends C to a tagged line regardless of the
interrogation. It was under-classed as R in the first pass, and the reader's observation that both
converted sibling manifests carry an `invariant:` line is what surfaced the misclassing.
`src/metalog.internal.cppm` now reads:

```cpp
// refs: ADR-3.D4
// invariant: the one `import std` of the metalog library module graph; every api/ and src/ unit
// imports this module plain to reach std.
export module insight.metalog.internal;
```

The claim was verified before being asserted (`OPS-8.O3`): three `export import std;` sites exist
in the whole repo — this one, `tests/metalog.test.cppm` and `benchmarks/metalog.bench.cppm` — so
the invariant is scoped to `api/` and `src/`, which is what it says.

**A stale claim deleted, with the evidence — the `export { using std::… }` block is DORMANT.** The
deleted header claimed *"metalog source uses unqualified `uint64_t`/`int64_t`/`size_t` etc."*. A
PCRE sweep for those spellings **not** preceded by `std::` returns **zero files** over
`insight-metalog/{api,src,tests,benchmarks,scripts}`. The sweep was verified against a positive
control before the zero was believed (`CLAUDE.md` § Searching the workspace): the identical pattern
over `logcraft/core/src` returns hits in five files on the first page. So the sentence was false at
the moment it was deleted, and the re-export block it justified is dormant plumbing. **Removing the
block is a code change and is therefore a finding, not this lane's edit.**

**Findings for other lanes.**

1. **`api/metalog.api.cppm` lines 274-281 contradict the code twelve lines above them** — a finding
   for whoever converts the `api/` unit, and for the pilot if that unit stays blocked. The block
   states *"Every member is defined OUT OF LINE in the implementation unit `src/metalog.api.impl.cpp`,
   NOT here in the interface"* and *"Never fold these back into the interface"*, while lines 250-255
   of the same file define all six special members `= default` **in the class body**. Verified by
   reading both spans. `DONE.md` (2026-09-04) records why: the gcc `_Hashtable` internal-linkage
   defect was measured inert on gcc-16.2 on 2026-09-03, and the MSVC `LNK2005` half was refuted by a
   golden run with the special members moved back into the interface (zero `LNK2005`, two
   anti-vacuity checks), after which they were moved into the class body — and the paragraph
   forbidding exactly that shape was left standing. It is comment-only to repair.
2. **`src/metalog.internal.cppm`'s `export { using std::int64_t; … }` block has no consumer in this
   repo** (measured above). Ripping dormant plumbing is `CLAUDE.md`'s standing rule and a code
   change; it belongs to the lane that owns `insight-metalog` source, not to a comment-only commit.

**Witnesses.** Comment-only: the code token stream of both files is byte-identical to `HEAD`'s
(`code_only_diff.py`). Grammar: `malf format --check` over the two files — 4 comment lines, forms
`invariant=1 refs=1 continuation=1 tool=1`, **0 would-be violations**. Comment lines 9 → 4;
would-be violations 8 → 0.
