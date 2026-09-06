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

## Unit 2 — `src/stats/` minus `wire_format.cpp` (3 files, 284 would-be violations)

`metalog.detail.stats.cppm` (126), `salience.cpp` (125) and `statistics.cpp` (33). 289 comment
lines, 284 of them violations (249 bare, 25 trailing, 7 spacer, 3 suppression-without-why), 5 tool
forms.

**Why `wire_format.cpp` is NOT in this unit — a stopped file, not a skipped one.** Its 28
violations include one block (lines 60-82) that is a **law**: the same four `insight::RunOutcome`
classes reach two wires under two spellings, the MetaLog document's lower-case case-sensitive
vocabulary and Sift's upper-case one, neither routing through the other's renderer, with the
rejected alternative (align Sift onto the spec's spelling) recorded because it would break a
published customer-facing format. That rule is obeyed in **three repos**
(`insight-metalog`, `insight-eidos/sift/src/report/change_report_serialize.cpp`,
`sift-action/src/types.ts`) and `ADR-26.D1` sends a rejected alternative a rule depends on to a
law block. **This lane may not mint a law number** (`OPS-8.O4`), so the file is left untouched and
recorded below under *Sites that need a law block*. Splitting the directory is `OPS-8.S2`'s own
file-group split; the unit is whole as declared, and no file in it is half-converted.

**The test this run applied for "does this need a law block?", stated once so it is auditable.**
A rule that a second site obeys needs a `D-LSRC-n` block **only when it has no addressable
owner**. Where an ADR slot, a design-note slot or a bible already owns the rule, the site carries
`refs:` to that owner and the rule is addressed without a new declaration. Measured on this repo:
`src/cube/metalog.detail.cube.cppm`'s two no-axes-equality-gate sites are owned by `DN-42.D17`,
`src/engine/engine.cpp`'s four *"same shape as the ... above"* transparent-key sites are owned by
`ADR-9.D2`, and `src/serialization/json_egress.hpp`'s one-JSON-entry-point rule is owned by
`DN-65.D2` — none of the three needs a block. `wire_format.cpp`'s rule has no owner: no ADR or
design-note slot in the workspace carries it (searched), and its only written authority is
`metalog-spec/GOVERNANCE.md` §3, which `.claude/rules/adr-shelf-boundary.md` states is **not
citable in registry form** from this shelf. That is what makes it, and only it, a block site.

**Census (`OPS-8.S4`).** Before: `NOLINT` 3 (all `NOLINTNEXTLINE`), `/*name*/` 0, `clang-format
off` 0, `wall-clock:` 0, SPDX 0. After: `NOLINT` **2**. The one decision:

* **`// NOLINTNEXTLINE(clang-analyzer-core.DivideZero)` (`metalog.detail.stats.cppm` line 87) was
  DELETED, with the evidence that it silences nothing.** The one shared `.clang-tidy` (a symlink to
  `malf/config/.clang-tidy` from every C++ repo) disables the whole family: `- -clang-analyzer-*`,
  with its own reason recorded there (redundant with the sanitizers, notes trace through
  third-party headers). Measured rather than read off the config alone: `clang-tidy-21
  --list-checks --config-file=<that file>` reports **0** enabled `clang-analyzer` checks and
  **1** for `readability-use-std-min-max`. `.clangd` is stated in that same file to inherit it
  verbatim, so the editor has no reader for it either. The claim it carried (`sum_fixed > 0`
  always) survives as an `assert:` line at the divide.
* The two `NOLINTNEXTLINE(readability-use-std-min-max)` directives are **kept** — that check is
  enabled — each now under its own `note:` giving the why, which is what the grammar requires and
  what neither had (both were classed `suppression-without-why`).

**Stripper cross-check (`OPS-8.S5`).** `removed == violations − (suppression-without-why +
trailing-nolint)`: `metalog.detail.stats.cppm` 124 == 126 − 2, `salience.cpp` 125 == 125 − 0,
`statistics.cpp` 32 == 33 − 1. Kept == tool forms + those same classes: 3 == 1 + 2, 2 == 2 + 0,
3 == 2 + 1.

**The claims, by class.** 56 claim blocks were written: `pre` 1, `post` 15, `invariant` 1,
`assert` 10, `note` 21, `refs` 9, with 16 untagged continuations. The `refs:` targets are
`ADR-3.D4`, `BIB:determinism_model`, `DN-32.D3`, `DN-43.D10`, `DN-64.D3` and `SRC-D-PROV-1` — the
last unchanged in form, because `metalog.detail.stats.cppm`'s site **cites** the code and does not
declare it (`OPS-8.O5`); the statement lives in `insight-canon`, and the site stays inside the
`.cppm` position class `registry_grammar_lint` reads as a declaration, so `G5` is unmoved.
`registry_grammar_lint` re-run after the conversion: **0 failures, 95 claimed codes and 95
declared in source**.

Held for the interrogation (R): the module's install/re-export seal · the HyperLogLog accuracy
figure · why the harmonic sum is fixed-point and not `double` · why the float-to-integer cast is
avoided · what makes a failure cue · whether the Terminator arm is redundant with the level arm ·
the measured `##[error]` salience · what the `as_i128` detour buys · whether the key sort is
required for determinism · what a partial `counts` does to the entropy · what `nullopt` means on
the wire · what checks the exported salience full scale.

**Interrogation** — one fresh agent, twelve questions, 77 tool uses, 181 k tokens, 7.7 minutes.
Transcript checked: `GIT COMMANDS RUN: none`.

**Score: 12 of 12 recovered, 0 not recovered, 0 wrong.** Nothing was re-homed, because nothing
had to be. What the reader added is below, because three of its answers are worth more than the
score.

| Q | verdict | what the agent found |
|---|---|---|
| Q1 seal | recovered, high | the repo's `CMakeLists.txt` puts the detail modules in a `PRIVATE FILE_SET cxx_modules_detail` while only three units go into the `PUBLIC FILE_SET cxx_modules` that `install(TARGETS …)` ships; the facade states the seal; `ADR-3.D4` clause 4 and `ADR-3.D5` govern it. It also reported that no lint in this workspace enforces it — the enforcement is the CMake file-set and install seal |
| Q2 HLL accuracy | recovered, high — **and it refuted this lane's own finding**, see below | `metalog-spec/SPEC.md` §3.5.1: *"Producers SHOULD use a HyperLogLog sketch with standard error ≤ 1.5% (precision `p = 14`, 16 384 registers)"* |
| Q3 fixed-point sum | recovered, high | the sum over 16 384 registers reaches about 2^66, past `double`'s 53-bit exact-integer range, so it rounds and the rounding depends on the partial-sum tree; and `raw` feeds the `raw < kSmallRangeThreshold` branch, so one ULP could flip which estimator runs. Cited the determinism bible's taxonomy and `ADR-31.D5`/`D6` |
| Q4 no float-to-int cast | recovered, high | `BIB:determinism_model` (the `refs:` this conversion wrote) plus `CLAUDE.md`; and a mechanical reason the prose never had — on the portable/MSVC leg `insight::det::u128` has no floating-point constructor, so the cast would not compile at all |
| Q5 failure cue | recovered, high | canon's contract block: a whitespace-delimited, punctuation-trimmed token that is a lexicon word, a CamelCase `…Error`/`…Exception` type, or the `segmentation fault` pair — with collision-prone words firing only when verdict-anchored, and exclusions for substrings in paths, negated type names, pass-led lines, count-register summaries and tokens inside a compiler `note:` message |
| Q6 Terminator arm | recovered, high — **and it found a vacuous test**, see below | three independent reasons: the bands and the stamped axis differ (90 vs 80, `Terminator` vs `Level`); the GitHub dialect gates the role row `any` and the level lift `self`; and an echoed line loses its level but keeps its role |
| Q7 `##[error]` salience | recovered, medium | derived `90 × rarity` ∈ {9000, 8100, 5400, 2700}, axis `Terminator`, and that echoed and non-echoed land on the **same** score because `normalize()` strips the SGR before classification. It also bounded the claim: `salience_score` runs only for templates below the top-K cut |
| Q8 `as_i128` | recovered, high | on the portable leg `i128` has constructors from `u128`, `int64_t` and `int` but **none from `uint64_t`**, so a direct cast would resolve through the sign-extending one and turn any count at or above 2^63 negative |
| Q9 the key sort | recovered, high | not required — the accumulators are `i128` integers, so the reduction is order-independent by construction; the sort discharges `ADR-31.D2`'s defined-order obligation belt-and-braces |
| Q10 partial `counts` | recovered, high | the entropy is under-stated: the unlisted mass contributes nothing to the numerator while `total` still divides. It read this off the `post:` this conversion wrote **and** off the engine's own call-site note |
| Q11 `nullopt` on the wire | recovered, high | omission is the spec's only spelling of *no level observed*; rendering `INFO` would be a positive claim that could manufacture or erase a crossing of the failure frontier `{ERROR, FATAL}` in the diff |
| Q12 `kSalienceFullScale` | recovered, high | `10000U`, declared in the public API and divided by in `insight-eidos`; checked by the `static_assert` at the end of `salience.cpp`'s anonymous namespace — and it **bounded the check**: it binds only the product of the two maxima, so every intermediate rung is unconstrained and a proportional change would still pass |

**THIS LANE'S OWN PRE-DELETION REASONING WAS WRONG ONCE, AND THE READER IS WHAT CAUGHT IT.** Before
the strip, the deleted line *"Sketch with p=14 (m=16384 registers, ~1.5% standard error)"* was
classed as an **unsourced and arithmetically false** measurement, on the ground that the textbook
HyperLogLog relative standard error is `1.04/√m` = `1.04/128` = **0.81 %** at this precision, not
1.5 %, and that no document in the repo carried the figure. The first half of that was a
mis-reading and the second was a search that stopped at the repo boundary: `metalog-spec/SPEC.md`
§3.5.1 states a **ceiling** — *"standard error ≤ 1.5%"* — which 0.81 % satisfies, and the comment
was a loose restatement of a spec requirement rather than a false measurement. The deletion still
stands (the reader recovered the figure and its authority unaided, which is the disposition test),
but the **finding** is withdrawn and recorded here as withdrawn, because a lane that files a false
defect costs the next reader more than the comment did.

**Findings for other lanes.**

3. **A `SHOULD` of the published spec has no witness in this repo — for Kleio.** `metalog-spec`
   §3.5.1 requires `p = 14` / 16 384 registers with standard error ≤ 1.5 %. Nothing binds
   `kPrecision` to that: the cold reader read `scripts/spec_conformance_gate.sh` and found no arm
   for it, and `tests/engine/test_hll_cardinality.cpp` asserts only loose ranges (`> 0`, `>= 5` for
   50 distinct values, `> 50` past a 10-value cap) — a smoke check, not an error-rate measurement.
   Changing `kPrecision` today reds nothing.
4. **`ReservoirTest.TerminatorRoleIsSalient` would still pass with the code it names deleted — for
   Kleio.** The cold reader found that it sets `LogLevel::Error` **and**
   `StructuralRole::Terminator` together, so the `Error` band alone carries it; no test isolates
   the `StructuralRole::Terminator` arm of `salience_score`. The arm is the one that stamps
   `RetentionAxis::Terminator` and outranks the level band 90 to 80, and it is exactly the arm the
   deleted prose spent twenty-four lines defending.
5. **Two documents cited from `insight-metalog` source exist only in the attic** — informational,
   for whoever converts `src/cube/`. `cube_differential_axes.md` and `cube_perf_and_collapse.md`
   resolve only under `technical_docs/history/architecture-v1/`. Under the Founder's ruling of
   2026-09-02 the attic is disposable and a pointer into it is ungated best-effort provenance, so
   the conversion simply drops these pointers; recorded because the sentences that carry them are
   the ones a later unit must check are still complete without them.

**Witnesses.** Comment-only: all three files, code token stream byte-identical to `HEAD`. Grammar:
`malf format --check` over the unit — 80 comment lines, forms `pre=1 post=15 invariant=1 assert=10
note=21 refs=9 continuation=16 tool=7`, **0 would-be violations**. Comment lines 289 → 80 (72 %
fewer); would-be violations 284 → 0.

## Unit 3 — `src/cube/` minus `cube.cpp` (2 files, 134 would-be violations)

`metalog.detail.cube.cppm` (122) and `cube_cardinality.cpp` (12). 136 comment lines, 134 of them
violations (126 bare, 4 spacer, 4 trailing, 1 ruler), 2 tool forms. `cube.cpp` (249 violations) is
the same directory's second file group and is a unit of its own (`OPS-8.S2`): a reader can answer
about the module interface and the cardinality compute without it, and 383 violations in one
questionnaire is two interrogations pretending to be one.

**Census (`OPS-8.S4`).** Zero `NOLINT`, zero `/*name*/`, zero `clang-format off`, zero
`wall-clock:`, zero SPDX, before and after. Two namespace closers kept. No census decision.

**Stripper cross-check (`OPS-8.S5`).** No suppression in either file, so the equality reduces to
`removed == violations`: 122 == 122 and 12 == 12, kept 1 and 1.

**The claims.** 30 blocks: `pre` 1, `post` 12, `invariant` 8, `note` 7, `refs` 3, with 14 untagged
continuations. `refs:` targets: `ADR-3.D4` once and `DN-42.D17` twice — the latter at **both**
no-axes-equality-gate sites, which is what replaces the unaddressable *"same shape as
`cube_diff_of` above"* the second site used to carry.

Held for the interrogation (R): the fourth cube dimension and what a stored cube looks like
because of it · whether the cube judges an up-shift · why `operator==` is written out · whether
the diff refuses unequal axes · whether compose keeps its inputs' closures · what makes the cube
bit-identical · what reading the cardinality off the coords buys · why `Unknown` is its own axis
value · what a DAG WHERE-chain breaks · where the collapse warning is emitted.

**Interrogation** — one fresh agent, ten questions, 39 tool uses, 133 k tokens, 4.4 minutes.
Transcript checked: `GIT COMMANDS RUN: none`.

**Score: 10 of 10 recovered, 0 not recovered — and ONE LINE THIS CONVERSION WROTE WAS FALSE, caught
before the commit.**

| Q | verdict | what the agent found |
|---|---|---|
| Q1 fourth dimension | recovered, high | pinned in `cube_diff_of` only, on the current side only; a stored cube's slot is uniformly `kStar`, `coord_of` omits the key, and it costs no cells because `populate` subsets the PINNED dims. It confirmed the byte-identity against a committed test vector |
| Q2 polarity | recovered, high | the cube judges nothing; `insight-eidos`'s `ordinal_polarity` maps Up to Regression and Down to Recovery, and `collect_causal_chains` reads a coord whose `latency_shift` starts with `up_` |
| Q3 hand-written `operator==` | recovered, medium — **and it bounded the evidence**, see below | it read the `note:` and then reported that nothing else in the tree corroborates it: no bug id, no reproducer, no test, and the API's own types do use `= default` — as MEMBER operators, which it flagged as its own inference rather than the tree's |
| Q4 axes equality | recovered, high | `DN-42.D17` §4, the `post:` this conversion wrote, the caller's `has_cube` presence check in `diff.cpp`, and SPEC §16.10's mandate to diff at the minimal common collapse |
| Q5 compose closures | recovered, high | there is nothing to keep — a closure is `populate`'s internal artifact, never on a `CubeBlock` and never on the wire; a cell closed in one input can stop being closed in the merge, which is why compose re-closes |
| Q6 bit-identity | recovered, high | nine mechanisms, including one this lane had not listed: `-ffp-contract=off` on every target in `CMakeLists.txt` |
| Q7 cardinality from coords | recovered, high — **and it falsified a `note:` this conversion had just written** | see below |
| Q8 `Unknown` as its own value | recovered, high | the §3.8 absence-rendered-as-present argument and the §16.4 absent-axis-means-aggregated collision, plus `DN-43.D10`; and the two visible consequences — `level_from_spec` reads an unknown token as `Unknown`, and `kMaxLevelBandFloor` stops below it |
| Q9 DAG WHERE-chain | recovered, high | roll-up stops being a function, a count is added twice, the aggregate no longer dominates its children, and the order-convex border's parent test is undefined; enforcement is a hard `std::logic_error`, not a degradation |
| Q10 the collapse warning | recovered, high — **and it corrected the question** | the warning fires in `insight-eidos`'s `insight_pipeline.cpp` from `collapse_note`, and metalog excludes spdlog by a Founder ruling of 2026-06-20 recorded in `metalog.cppm`. It also reported that the *over-cardinality* warning the question presupposed was **retired** — today's warning names the axis that was collapsed |

**THE WRONG LINE, AND HOW IT GOT WRITTEN.** This conversion wrote, above `cube_cardinality`:
`// note: observability only -- the result never feeds the deterministic content stream.` It was a
faithful compression of the deleted prose (*"OBSERVABILITY ONLY: a deterministic function of the
counts that never feeds the deterministic content stream"*), and **both are false.** The reader
traced it: `engine.cpp`'s `build_acquisition` copies `card.cells`, `card.per_axis[Level]` and
`card.per_axis[Role]` into `AcquisitionBlock::closed_cells` / `level_cardinality` /
`role_cardinality`; `serialize.cpp` writes those three fields; and they are visible in the
committed vectors. Re-derived at the source before acting on it (`MEM:verify-audit-findings`):
`engine.cpp` lines 1315-1318, `serialize.cpp` lines 292-293 and 1099-1100, and
`tests/vectors/service_a.vectors.jsonl` carries `closed_cells":3`. The line now reads
`// note: a deterministic function of the closed cube; its values reach the acquisition block.`
and `OPS-8.S7` steps 2 and 3 were re-run after the hand edit: 0 would-be violations, comment-only
against `HEAD`. **This is `OPS-8.O3`'s second lesson firing exactly as written — a claim moved into
a tagged line is a claim the converter now asserts — and the cold read is what caught it.**

**Finding 6 — an unsourced compiler-defect claim this conversion now asserts, for the lane that owns
`insight-metalog` source.** `Cell::operator==` is written out by hand, and the `note:` this run wrote
says *"not `= default`: a defaulted friend `operator==` on an import-std type is a GNU defect"* —
carried from the deleted prose, which said the same. The cold reader searched and found **no bug id,
no reproducer, no test and no other mention anywhere in the workspace**, and observed that the API's
own `CubeCoord` / `CubeCell` / `CubeAxis` all use `= default` — as member operators rather than
hidden friends. The note is kept because deleting it invites the next reader to "simplify" the
operator and break a build nobody would expect to break; but it is a claim with no witness, and the
owning lane should either pin it (a bug id in the `refs:`, or a compile test that fails without the
hand-written form) or retire it. Recorded here rather than repaired, because settling it needs a
compile on the gcc leg, which a comment-only commit may not carry.

**Witnesses.** Comment-only: both files, code token stream byte-identical to `HEAD`, re-taken after
the hand edit. Grammar: `malf format --check` over the unit — 47 comment lines, forms `pre=1 post=12
invariant=8 note=7 refs=3 continuation=14 tool=2`, **0 would-be violations**. Comment lines 136 → 47
(65 % fewer); would-be violations 134 → 0.

## Unit 4 — `src/cube/cube.cpp` (1 file, 249 would-be violations)

The cube's heavy machinery: closure, lossless base recovery, the per-window dimensional-collapse
guardrail, the order-convex border, and the diff/compose re-closure. 251 comment lines, 249 of them
violations (223 bare, 24 trailing, 1 spacer, 1 ruler), 2 tool forms.

**Census (`OPS-8.S4`).** Zero `NOLINT`, zero `/*name*/`, zero `clang-format off`, zero
`wall-clock:`, zero SPDX, before and after; the two namespace closers kept. No census decision.
**Stripper cross-check:** removed 249 == 249 violations (no suppression in the file), kept 2 == the
two tool forms.

**The claims.** 67 blocks: `pre` 3, `post` 27, `invariant` 7, `assert` 10, `note` 16, `refs` 5, with
28 untagged continuations. `refs:` targets: `ADR-31.D8` twice (the collapse policy's total-order
tie-break), `DN-43.D10` twice (`Unknown` as a live level value rather than a fold or an omission),
`DN-42.D17` once (the differential axis's `kind`). **No law block was owed:** every multi-site rule
in this file already has a registry-form owner, which is the test stated in unit 2's entry.

Held for the interrogation (R): why `populate` subsets only the pinned dims · why an unstable sort
is admissible under byte-identity · what decides LEVEL versus WHERE at equal gain · which levels can
never be banded · why the budget is stamped in the caller · what the empty-WHERE residual is for ·
what the recovery path costs · why the shift pins on one side only · what keeps `signed_shift_label`
off the NONE band · what makes the border's binary search sound · what a compose seed does to
precision · whether `categorical` is a downgrade for an ordinal axis.

**Interrogation** — one fresh agent, twelve questions, 27 tool uses, 112 k tokens, 5.3 minutes.
Transcript checked: `GIT COMMANDS RUN: none`.

**Score: 12 of 12 recovered, 0 not recovered, 0 wrong.** Every answer at high confidence, and five
of them are strictly sharper than the prose they replaced:

* **Q1** quantified what the deleted prose only asserted: enumerating all `2^kCellDims` masks would
  emit each generalization `2^s` times where `s` is the tuple's starred count, so counts inflate
  **non-uniformly** — ×2 for an ordinary stored row, ×4 for an empty-component one — and it named the
  test that pins it, `CubeBlock.EmptyComponentAggregatesNoWhere`, with the arithmetic (apex reads 2;
  an all-mask enumeration gives 1×4 + 1×2 = 6). It also bounded the damage: the closure and the
  distinct-cell set are unaffected, only the counts break.
* **Q3** corrected the emphasis. At **equal** Δcardinality the LEVEL step wins **on cost**, not on
  the tie-break: a level step costs its target `band_floor` (2, 3 or 4) against `kWhereDropCost` 100.
  The strict `>` tie-break only decides an equal **ratio**. The prose ran the two together.
* **Q4** derived the widest reachable band — `{Trace, Debug, Info, Warn} → Warn` — and observed that
  `Unknown` (index 6, above `Fatal`) is unreachable by any floor for the same structural reason, then
  named `CubeCollapse.SeverityFrontierNeverCrossedWhereCollapsesInstead` as the pin.
* **Q8** derived, rather than repeated, why the shift pins on the current side only: `kThetaWas` is
  0, so a nonzero previous count kills emergence outright — pinning both sides would leave a declared
  `latency_shift` axis with nothing ever on the border, and the dual would kill vanishing too.
* **Q12** found that the question is not even open: `kind`'s enum is closed to
  `["categorical", "chain"]` in **both** published schemas, so `"ordinal"` is unavailable, and
  `level` — itself ordinal — is declared `categorical` while carrying `band_floor`.

**Finding 7 — a latent encoding collision the prose never mentioned, for the lane that owns
`insight-metalog` source.** The cold reader observed, and this lane re-derived at the source, that
`signed_shift_id` is **total over its argument types and returns a colliding id for a legal input**:
`OrdinalShift::None` is 0 and `kMagnitudeBands` is 3, so `signed_shift_id(None, Down)` returns 3 —
the same value-id as `signed_shift_id(High, Up)` — and `signed_shift_label(3)` renders it `up_High`,
because its `down` test is `band_id > kMagnitudeBands`. Two guards keep it unreachable today, both
spelled `!= OrdinalShift::None`: the one writing site in `cube_diff_of` (`cube.cpp` line 721) and the
producing caller `component_latency_shifts` (`diff.cpp` line 508). Verified by reading
`api/metalog.api.cppm`'s two enums (`OrdinalShift::None = 0 … High = 3`,
`OrdinalDriftDirection::None = 0`) and both call sites. **What makes this a finding rather than a
note is the enforcement ladder (`ADR-26.D1`): the property is carried on rung 4, a `pre:` line, when
rung 1 or 2 could carry it** — reserving 0 for NONE and shifting the bands by one would make the
encoding total, and an `assert()` or a `std::expected` at `signed_shift_id` would make a future
caller that drops the guard fail loudly instead of silently emitting `up_High` for a downward
no-drift. That is a code change and is not this comment-only lane's to make.

**Witnesses.** Comment-only: code token stream byte-identical to `HEAD`. Grammar: `malf format
--check` over the file — 98 comment lines, forms `pre=3 post=27 invariant=7 assert=10 note=16 refs=5
continuation=28 tool=2`, **0 would-be violations**. Behaviour: `malf test insight-metalog` with
units 1-4 in the tree, **297 of 297 on clang-21 and 297 of 297 on gcc-16**, equal to the baseline,
under a slot held and released correctly (`HELD … ALIVE` at the release check). Comment lines
251 → 98 (61 % fewer); would-be violations 249 → 0.

## Unit 5 — `src/serialization/json_egress.hpp` (1 file, 35 would-be violations)

The package's single JSON write entry point. 36 comment lines, 35 of them violations (31 bare, 4
spacer), 1 tool form. Converted with unit 6 in one commit; the entries stay separate.

**Census (`OPS-8.S4`).** Zero of every machine-read token before and after. **Stripper
cross-check:** removed 35 == 35 violations, kept 1 == the one tool form.

| id | class | the claim | disposition |
|---|---|---|---|
| C1 | C | the Glaze write entry points may appear in exactly ONE file per package, this one; every other site writes through the wrapper and cannot supply raw opts | **`invariant:`** at `conformant` |
| X1 | X | the rule is `DN-65.D2`'s; the rejected alternative — a rule keyed on the opts SPELLING, falsified before any code was written — is `DN-65.O4`'s | **`refs: DN-65.D2, DN-65.O4`** |
| C2 | C | the result is RFC 8259-conformant for every string input, including log-derived bytes below 0x20, with no upstream precondition | **`post:`** at `to_string`, **`refs: DN-65.D1`** |
| R1 | R | Glaze's `char_escape_table` is consulted first and unconditionally, so five of the 32 C0 bytes escape whatever the option says; the option governs the other 27 | **`note:`** at `to_string` |
| C3 | C | a fully-formed value written into a growable string has no reachable failure — the error channel carries buffer exhaustion and user-writer errors, neither on this path | **`assert:`** at the `(void)glz::write` call |
| R2 | R | the entry point is the gate's key because a write cannot happen without one, so it is the only axis on which the enumeration of writes is complete | deleted — `DN-65.O4` owns it and the `refs:` reaches it |
| H1 | H | *"5 of the workspace's 7 egress writers took the default"* | deleted — a census of a state, and `DN-65.D2` holds it |

**No law block is owed here, and the site is the reason the test in `OPS-8`'s verdict item 9 was
written.** The one-entry-point rule is exactly the shape `ADR-26.D5` sends to a block — a MUST that
other sites obey — but `DN-65.D2` owns it at a registry address, so the site cites rather than
declares.

**Witnesses.** Comment-only against `HEAD`. Grammar: 0 would-be violations. Comment lines 36 → 11.
Behaviour: covered by the batch witness named at the end of this file, NOT by the one carried by
unit 4's commit.

## Unit 6 — `src/operations/metalog.detail.operations.cppm` (1 file, 12 would-be violations)

The SPEC §2.4 comparability gate shared by compose and diff. 13 comment lines, 12 of them violations, 1
tool form. **Stripper cross-check:** removed 12 == 12, kept 1 == the tool form. Census: zero of
every machine-read token before and after.

| id | class | the claim | disposition |
|---|---|---|---|
| X1 | X | the module exists under the named-modules ruling | **`refs: ADR-3.D4`** |
| C1 | C | when both sides carry the processing identifier the values MUST be equal, and throwing is the spec's "MUST fail" branch; when one side omits it the operation may proceed | **`post:`** at `check_processing_identifier_gate` |
| C2 | C | an identifier is carried into a compose output only when both inputs supplied it and matched, so the merged document never states a contract one input did not | **`post:`** at `carry_processing_identifier` |
| M1 | M | the module is the §2.4 gate, shared by compose and diff | deleted — the two functions and their names are the statement |
| M2 | M | *"see callers for the carry rule"* | deleted — the carry rule is the second function, in the same file |

**Witnesses.** Comment-only against `HEAD`. Grammar: 0 would-be violations. Comment lines 13 → 6.
Behaviour: same batch as unit 5.

## The units 5 and 6 interrogation — one reader, eight questions

Run after the two drafts were in the tree, over both files at once: the two units are small enough
that one reader's load covers them, and the questionnaire is split 5 + 3 so each block is answerable
from its own file. 25 tool uses, 108 k tokens, 4.0 minutes. Transcript checked:
`GIT COMMANDS RUN: none`. **Score: 8 of 8 recovered, 0 not recovered — and one line this conversion
wrote was found INCOMPLETE and repaired before the commit.**

| Q | unit | verdict | what the agent found |
|---|---|---|---|
| Q1 | 5 | recovered, high — **and it found an enforcement this lane had not** | the rule is mechanically gated by `scripts/json_write_closure_lint.py`, run in `lint.yml`'s `pin-coherence` job, with every list derived rather than hand-kept and an exit 2 on an empty derivation. Two limits it read off the tree: the gate proves LOCATION only, and `lint.yml` has no `push:` trigger, so it fires on pull request, dispatch and the release tag, never on a main push |
| Q2 | 5 | recovered, high | `DN-65.O4` in full: the opts-spelling rule was written first and falsified the same day — 5 true positives, 9 false positives, 2 false negatives as a grep, and it missed the live defect |
| Q3 | 5 | recovered, high | `opt_true` assigns the member when the caller's type carries it and derives a new type when it does not, so `prettify`/`skip_null_members` survive and no caller can spell the escape false. It added a "cannot" specific to this file: metalog's wrapper exposes only `to_string`, with no `write(value, buffer)` overload, so a metalog caller cannot recover Glaze's error context — sift's twin offers both |
| Q4 | 5 | recovered, high | the 5/27 split with a third, independent source this lane had not used: a measurement recorded in the release history that reverted this very wrapper to a raw `glz::write`, rebuilt, and redded on 27 of 32 C0 bytes with the survivors *"exactly `0x08 0x09 0x0a 0x0c 0x0d`"*. It also bounded the claim: nothing in the tree covers bytes at or above 0x20 |
| Q5 | 5 | recovered, medium — **and it caught the incompleteness**, see below | |
| Q6 | 6 | recovered, high | absence is not a mismatch — the predicate short-circuits — and it is normative upstream in SPEC §2.4 (*"the operation MAY proceed but the consumer SHOULD treat the result with caution"*) |
| Q7 | 6 | recovered, high | `nullopt`, and it added what the helper's own contract cannot say: two neighbouring fields are handled by hand rather than through it — `out.ruleset` by a mirror, `out.transport` by an explicit equality because no gate ran for it |
| Q8 | 6 | recovered, high | the module, its PRIVATE file-set seal, and the exact caller list — `compose()` three times, `diff()` three times, `carry_processing_identifier` once — with the search scope it used stated |

**THE INCOMPLETE LINE, AND WHY IT MATTERS MORE THAN IT LOOKS.** The `assert:` this conversion wrote
above the discarded `glz::write` return read: *"the error channel carries buffer exhaustion and
user-writer errors, neither on this path."* The reader compared it against the sibling wrapper in
`insight-eidos/sift/src/json_egress.hpp` and found that **the metalog copy drops the qualifier that
makes the argument work**: buffer exhaustion applies to **fixed-capacity** buffers only, which is
precisely why writing into a growable `std::string` cannot hit it. Without the qualifier the line
asserts a conclusion whose premise is missing. The line now reads
`// error channel carries fixed-capacity buffer exhaustion and user-writer errors, neither here.`
and `OPS-8.S7` steps 2 and 3 were re-run: 0 would-be violations, comment-only against `HEAD`. The
edit also met `OPS-8.S9`'s named trap first-hand — the exact-string replacement failed because
clang-format had already reflowed the line, and the repair had to be anchored on a line prefix.

The reader also recorded a limit this lane accepts: the failure set is asserted from the wrapper's
own comment, not from Glaze's source, which `DN-65.D1` deliberately treats as non-citable (*"a
dependency may change under us"*), so nothing in this workspace independently enumerates
`glz::write`'s error cases.

## Unit 7 — `src/engine/` · `engine.cpp` (1 file, 361 would-be violations)

The stateful producer: window lifecycle, the hot-path per-event accumulator, the trace-scoped
n-gram graph, span-edge resolution, the salience reservoir, and every window-close block builder.
363 comment lines, 361 of them violations (329 bare, 24 trailing, 5 suppression-without-why, 3
spacer), 2 tool forms. **This is where the repo's `SRC-D-OTEL-*` band lives.**

**Census (`OPS-8.S4`).** `NOLINT` 5 before and 5 after; zero `/*name*/`, zero `clang-format off`,
zero `wall-clock:`, zero SPDX. No census decision — every suppression was kept, because both checks
they name are enabled under the one shared `.clang-tidy`: `clang-tidy-21 --list-checks` lists
`readability-function-cognitive-complexity` and `readability-use-std-min-max`. Each directive now
sits directly under the `note:` carrying its why, with the directive still immediately above its
target — the silent-disarm shape `OPS-8.S5` names (a claim landing *between* a kept directive and
the line it suppresses) was checked for at all five sites and is absent.

**Stripper cross-check (`OPS-8.S5`).** `removed == violations − kept violation classes`:
356 == 361 − 5. `kept == tool forms + those classes`: 7 == 2 + 5.

**The `SRC-<code>` disposition, and the address census that proves nothing was lost.** Every
occurrence in this file is a body site past line 40, so `registry_grammar_lint` classes them as
**citations**, not declarations (`OPS-8.O5`): each became a `refs:` line unchanged in form and none
was repointed. Measured over the file: **38 occurrences before, 37 after, and a set diff of the
DISTINCT codes is empty** — the one lost occurrence is a second mention of a code the file still
cites. `registry_grammar_lint` after the conversion: **0 failures, 95 claimed codes, 95 declared in
source**.

**The claims.** 97 blocks: `pre` 2, `post` 16, `invariant` 20, `assert` 14, `note` 48, `refs` 42,
with 32 untagged continuations. The 42 `refs:` lines carry `SRC-D-OTEL-9`, `SRC-D-OTEL-11`,
`SRC-D-OTEL-13`, `SRC-D-OTEL-20`, `SRC-D-OTEL-21`, `SRC-D-TIR-2`, `SRC-D-TIR-5`,
`SRC-D-WHERE-2`, `SRC-D-WHERE-4`, `SRC-D-WHERE-5`, `SRC-D-W1-2`, `SRC-D-PROV-1`, `SRC-D-RNK-2`, `SRC-D-OUT-4`,
`SRC-II-7`, `ADR-9.D2`, `ADR-9.D3`, `ADR-23.D1`, `ADR-29.D1`, `DN-32.D3`, `DN-50.D4` and
`DN-64.D3`. **No law block was owed:** the file's one unaddressable cross-reference — four sites
saying *"same shape as the … above"* about the transparent-key copy-on-first-sight idiom — is owned
by `ADR-9.D2`, and all four now cite it.

**A stale claim deleted, with the evidence.** `MetaLogEngine::HllState` carried *"Key =
content_template_id + '\x1f' + decimal(param_index)"* — a concatenated string key the code does not
use and has no trace of. The member one line below is
`std::unordered_map<std::string, std::vector<HyperLogLog>> sketches`: keyed by content_id and
**indexed** by param_index, with the per-content vector grown on demand. The line described a design
its own type contradicts. Replaced by an `invariant:` stating what the code does; the cold reader
then reconstructed the real key unaided, down to the content_id's `"h:"`-plus-32-hex shape.

**Interrogation** — one fresh agent, twelve questions, 30 tool uses, 117 k tokens, 3.6 minutes.
Transcript checked: `GIT COMMANDS RUN: none`.

**Score: 12 of 12 recovered, 0 not recovered — and ONE `invariant:` THIS CONVERSION WROTE WAS
FALSE, caught before the commit.** Every answer at high confidence. Four went beyond the prose they
replaced: Q3 bounded the drop counter (*observations*, not distinct keys — the distinct count is
deliberately unknowable, being the unbounded set the cap refuses); Q6 closed the residual case the
old prose left open (when ratio **and** count are equal, `outgoing` is equal too, so both candidates
yield the same band and the ambiguity is unobservable); Q8 found `DN-50.D4`'s own record that the
event-free branch is unreachable from the product's ingest today; and Q9 named both benchmarks that
measure the copy-on-first-sight shape, with `bench_ordinal_key_alloc`'s pre-fix figure of one
allocation per observation per event and +7.9 ns/event on the libstdc++ ship leg.

**THE FALSE LINE, AND WHAT IT COST TO FIND.** This conversion wrote, above `open_window`:
`// invariant: every per-window accumulator is cleared here and the cross-window stability state
(prev_freq_, prev_window_end_iso_) is left untouched.` The reader answered Q2 correctly and then
went further than the question asked, reporting that `open_window` does **not** clear
`pending_link_edges_`, `orphan_link_edges_` or `service_edges_`, although `reset_window_state`
does. Re-derived here by diffing the two functions' cleared-member sets: **exactly those three are
in `reset_window_state` and not in `open_window`**. So the universal *"every"* was false. The line
now reads:

```cpp
// invariant: the cross-window state -- prev_freq_, prev_window_end_iso_, registry_ -- survives
// this call and feeds the stability block and the display vocabulary.
// note: the span-link and service-topology accumulators are cleared at close, not here.
```

`OPS-8.S7` steps 2 and 3 were re-run after the hand edit: 0 would-be violations, comment-only
against `HEAD`.

**Finding 8 — the `open_window` / `reset_window_state` asymmetry, for the lane that owns
`insight-metalog` source.** Three per-window accumulators — `pending_link_edges_`,
`orphan_link_edges_`, `service_edges_` — are cleared only at close. Today that is harmless because
`close_window` always runs `reset_window_state`, but it makes `open_window` a partial reset: two
`open_window` calls without an intervening `close_window` would carry a previous window's span-link
and service-topology state into the new one, while every other accumulator resets. The other
seventeen members appear in both functions, so the omission reads as an oversight rather than a
decision — nothing in the tree states an intent for it. Repairing it is a code change. The reader
also flagged a smaller sibling: `last_window_ngram_observations_dropped_` is documented as *"valid
between `close_window()` and the next `open_window()`"* and is cleared by neither.

**Witnesses.** Comment-only: code token stream byte-identical to `HEAD`, re-taken after the hand
edit. Grammar: `malf format --check` over the file — 181 comment lines, forms `pre=2 post=16
invariant=20 assert=14 note=48 refs=42 continuation=32 tool=7`, **0 would-be violations**.
Behaviour: batch C (below) — 297 of 297 on clang-21 and 297 of 297 on gcc-16. Comment lines
363 → 181 (50 % fewer); would-be violations 361 → 0.

## Unit 8 — `src/stats/wire_format.cpp` (1 file, 28 would-be violations) — THE LAW BLOCK LANDS

The file unit 2 stopped on. The pilot issued the law number on 2026-09-06 and this unit mints it.
29 comment lines, 28 of them violations (24 bare, 3 spacer, 1 trailing), 1 tool form. Census: zero
`NOLINT`, zero `/*name*/`, zero `clang-format off`, zero SPDX, before and after. Stripper
cross-check: removed 28 == 28 violations (no suppression), kept 1 == the one tool form.

**The law is cited here as `LSRC-8` and its framed `D-` line is deliberately NOT reproduced in this
ledger.** A spelled declaration token is read by `registry_grammar_lint` as a declaration wherever
it appears, so quoting the frame here would declare the law a second time and break the
single-declaration property that is the whole point of form 1 — the failure mode currently live on
`main` from a sibling repo (verdict item 9). The block sits at `spec_run_outcome_of`, in
`src/stats/wire_format.cpp`, and that site is its one declaration.

**What the law says**, paraphrased: the same four `insight::RunOutcome` classes reach two wires
under two spellings that are not interchangeable, and a consumer takes its spelling from the
boundary it reads rather than from `RunOutcome`, which has no wire spelling of its own. The MetaLog
document side is minted by a vendor-neutral standard as lower-case and case-sensitive and pinned as
a closed schema enum; the Sift change-report side is this product's own upper-case format with a
live TypeScript consumer. The rejected alternative — unify by moving Sift onto the spec's spelling
— is named with its cost: it breaks a published customer-facing format to buy a symmetry no
consumer asked for. The block closes on why the MetaLog mapping is partial by construction.

**Why this one needed a block when three other candidates did not.** The test stated in unit 2's
entry: a block is owed only where the rule has **no addressable owner**. Its authority is
`metalog-spec/GOVERNANCE.md` §3, a different owner's shelf with no registry form in this workspace,
so no `refs:` line can reach it. The pilot issued the number on that reasoning and refused blocks
for the three candidates that resolve to `DN-42.D17`, `ADR-9.D2` and `DN-65.D2`.

**The other claims.** One `note:` plus one `refs: ADR-3.D4` above the `#include <ctime>` in the
global module fragment, carrying why the header stays textual. Everything else in the file was
either a mirror of the code or a duplicate of a contract the module interface already carries after
unit 2.

**Interrogation** — one fresh agent, six questions, 40 tool uses, 97 k tokens, 3.3 minutes.
Transcript checked: `GIT COMMANDS RUN: none`. **Score: 6 of 6 recovered, 0 not recovered, 0 wrong**,
every answer at high confidence.

**The law block was not merely readable — the reader independently re-derived its content and then
found where it had been living.** Q1 and Q4 were answered from the published `SPEC.md` §2.5 and the
schema's closed `enum`; Q2 from `insight-canon`'s `to_string(RunOutcome)`, which the reader checked
is upper-case AND total (it renders `Unknown`, which this wire has no value for) — two independent
reasons the seam cannot route through it, one of which the deleted prose never stated. Q3 recovered
the rejected alternative from the block and then corroborated it against the live consumer
`sift-action/src/types.ts` and the mirror comment in
`insight-eidos/sift/src/report/change_report_serialize.cpp`.

**AND IT FOUND THE ARGUMENT'S ONLY PREVIOUS HOME, WHICH IS THE CASE FOR THE BLOCK.** The reader
reported that the Founder's ruling on this subject — the MetaLog standard and Sift's product format
keep different spellings — survived in `technical_docs/history/1.10.0.md`, and correctly flagged
that shelf as disposable provenance rather than authority. That is exactly the LEANING failure
`CLAUDE.md` names: a sentence whose completeness depends on a pointer into an attic that could be
wiped tomorrow. Before this unit the rule's only written home outside the deleted source prose was
that frozen record. It now has a durable address at the site it governs.

**A zero-citer law is legal and this is one.** The reader's own sweep for the citation form returned
exactly the declaring line and no citing site. `ADR-26.D5` rules that a law owes no citer — *"a rule
with exactly one obeying site is complete without one"* — and no gate reds on it, which
`registry_grammar_lint` confirms: **FORM 1: 8 declarations, 2 citations, single-declaration checked
BOTH ways and numbering checked DENSE**, with zero failures naming this repo.

**Witnesses.** Comment-only: code token stream byte-identical to `HEAD`. Grammar: `malf format
--check` over the file — 26 comment lines, forms `note=1 refs=1 law=1 tool=1`, **0 would-be
violations**; the checker classes the frame as `law`, not `law-malformed`, and the 100-byte rule
lines survive clang-format unchanged. Comment lines 29 → 26; would-be violations 28 → 0.

## Unit 9 — `src/operations/` · `compose.cpp` + `diff.cpp` (2 files, 419 would-be violations)

The compose and diff semantics: the §12 merge with its cap algebra and re-derived reservoir, and
the §13 pairwise delta with the latency-shift axis and the comparison-outcome evaluation. 424
comment lines, 419 of them violations (389 bare, 20 trailing, 9 spacer, 1 suppression-without-why),
5 tool forms. `metalog.detail.operations.cppm`, the third file of these two directories, converted
as unit 6.

**Census (`OPS-8.S4`).** `NOLINT` 1 before and 1 after; zero `/*name*/`, zero `clang-format off`,
zero SPDX. **Stripper cross-check:** removed 418 == 419 − 1 kept suppression; kept 6 == 5 tool
forms + 1.

**The claims.** 76 blocks: `pre` 1, `post` 25, `invariant` 10, `assert` 11, `note` 44, `refs` 23,
with 34 untagged continuations. `refs:` targets: `DN-56.D2`, `DN-56.D3`, `DN-56.D6`, `DN-56.D7`,
`DN-50.D4`, `DN-64.D3`, `DN-42.D17`, `ADR-16.D1`, `ADR-17.D8`, `ADR-23.D4`, `ADR-31.D8`,
`STU-3.A1`, `SRC-II-7`, `SRC-D-TIR-4`, `SRC-D-TIR-5`, `SRC-D-PROV-1`, `SRC-D-W1-1`, `SRC-D-W1-4`,
`SRC-D-OTEL-20`, `SRC-D-OTEL-21`. Every `SRC-` site is a body citation past line 40; none is a
declaration. **No law block owed:** the two rules here that a second site obeys — the composed cap
algebra and the non-associativity it buys — are owned by `DN-56`, and the no-axes-equality-gate
rule by `DN-42.D17`.

**A measurement this unit produced rather than carried: `STU-3.A1` is what backs the
sample floor.** The `kShiftSampleFloor` prose carried a corpus-picked number with its false-actionable
rates. Rather than re-assert the figures in a `note:`, the claim is a `refs: STU-3.A1` plus an
`invariant:` stating the property that matters — the floor is an ABSOLUTE paired-event count and not
a ratio, precisely because the W1 thresholds are scale-relative and that is what lets a tiny sample
read HIGH.

**Interrogation** — one fresh agent, twelve questions, 44 tool uses, 128 k tokens, 5.7 minutes.
Transcript checked: `GIT COMMANDS RUN: none`. **Score: 12 of 12 recovered, 0 not recovered — and ONE `note:` THIS CONVERSION WROTE WAS NOT
LITERALLY TRUE, caught before its witness landed.**

Four answers went past the prose they replaced. Q2 recovered the non-associativity ruling AND named
the test that asserts the scope-dependence from both sides — the divergence under a binding cap and
the equality when none binds — which is the evidence the old prose only referred to. Q3 derived
that dropping the residual bucket over-states concentration while attributing it invents an
attribution, from the arithmetic rather than from the comment. Q7 found `STU-3.A1`'s pre-registered
scan, its three measured null rates and both guard tests, and then bounded the claim correctly: the
number was picked against one binding shape, a bimodal cache. Q10 confirmed both point-lookup maps
are never iterated into content and named the sorted output that makes each safe.

**THE NOT-TRUE LINE.** This conversion wrote, above `salience_memory`:
`// note: point-lookup only, so the map is not a determinism surface.` The reader answered Q10
correctly and then went past it: `diff_reservoir_delta` **does** range-iterate `cur_memory`, at
`for (const auto& [template_id, cur_side] : cur_memory)`, to build `frontier_crossings`. Verified
here at the source — the iteration is real, and what makes the output safe is the explicit
`std::ranges::sort(delta.frontier_crossings, by_crossing_id)` three lines later, over a total order
on unique 16-byte ids. The deleted prose said *"never iterated into output"*, which is defensible
read as *"its order never reaches the output"*; the paraphrase *"point-lookup only"* is the half
that is false. The line now names the real mechanism:
`// note: its order never reaches the wire -- every emitted list is sorted by template_id.`
`OPS-8.S7` steps 2 and 3 were re-run after the edit: 0 would-be violations, comment-only against
`HEAD`. **`prev_memory` genuinely is lookup-only; only the `cur_memory` half was wrong.**

**Finding 9 — a malformed suppression that silences everything, and the reader derived it
independently.** `compose.cpp` carries
`// NOLINTNEXTLINE (readability-use-std-min-max) defensive clamp (hot path)` — with a **space**
before the check list. Measured here with a purpose-built clang-tidy fixture before the unit was
written: a spaced directive is parsed as a **bare** `NOLINTNEXTLINE` and suppresses **every** check
on the next line, while the tight spelling scopes correctly. The probe ran three functions past
`clang-tidy-21` with two checks enabled — the spaced site reported neither diagnostic, the tight
site reported the un-named one, and the unsuppressed site reported both. The cold reader, given no
hint beyond the question, reached the same verdict from clang-tidy's own directive grammar. **The
directive is left byte-identical** and re-homed under a `note:` that states the measured scope:
narrowing it would change what the linter checks, which is not a comment-only act. Repairing it —
and deciding whether the line has a second finding the over-broad directive was hiding — is a
finding for the lane that owns this source.

**Finding 10 — the workspace's own comment gate admits the malformed suppression, for Argos.** The
reader checked `malf/comment_contract_lint.py`'s `NOLINT` recogniser, `^NOLINT(NEXTLINE|BEGIN|END)?\b`,
and it matches the spaced spelling — so the CCC gate counts a suppress-everything directive as a
well-formed tool form. A one-character tightening of that pattern would make the class visible
workspace-wide; how many other sites carry it is unmeasured.

**Witnesses.** Comment-only: both files, code token stream byte-identical to `HEAD`, re-taken after
the hand edit. Grammar:
`malf format --check` over the unit — 154 comment lines, forms `pre=1 post=25 invariant=10
assert=11 note=44 refs=23 continuation=34 tool=6`, **0 would-be violations**; the kept suppression
still sits directly under its `note:` and directly above its target. Comment lines 424 → 154 (64 %
fewer); would-be violations 419 → 0.

## Unit 10 — `src/serialization/serialize.cpp` (1 file, 326 would-be violations)

The JSON serialiser: the omit-empty glaze DTO mirror of the MetaLog envelope and the `make_*`
builders behind the two `to_json` overloads. 329 comment lines, 326 of them violations (275 bare,
42 trailing, 9 spacer), 3 tool forms. Census: zero `NOLINT`, zero `/*name*/`, zero `clang-format
off`, zero SPDX, before and after. Stripper cross-check: removed 326 == 326 violations (no
suppression), kept 3 == the three tool forms.

**The claims.** 58 blocks: `pre` 1, `post` 5, `invariant` 27, `assert` 2, `note` 28, `refs` 24,
with 31 untagged continuations. The `invariant` count is the highest of any unit in this run and
that is the file's nature: a DTO layer's contracts are almost all structural — *this field is an
optional so the wire omits it*, *this container is installed only when a member landed*, *this
member is declared last so the standard's half comes first* — and most of its deleted prose was
per-field trailing text mirroring `std::optional` plus `skip_null_members`, which the code says
outright.

**THE LAW MINTED IN UNIT 8 GAINS ITS FIRST TWO CITERS HERE, and they are exactly the shape the
form exists to replace.** Two sites in this file pointed at the `RunOutcome` argument by prose —
*"See the argument at spec_run_outcome_of's definition — the two tokens are NOT interchangeable"* —
which is an unaddressable reference to another file's comment. Both now carry `refs: LSRC-8`. A
sweep of `src/` returns one declaring line and two citing lines.

**One limit worth stating so it is not mistaken for a defect:** `registry_grammar_lint` still
reports *"2 citations"* for form 1 after these two landed, because its citation leg walks the DOC
tier only — the limit `ADR-26.D7` records in its own text. A law cited from source is invisible to
that count by construction.

**FIVE DISTINCT ADDRESSES WERE LOST AND RESTORED — the failure that produced verdict item 10.**
The first pass of this unit dropped `SRC-D-OTEL-13`, `SRC-D-OTEL-11`, `SRC-D-OTEL-9`,
`SRC-D-W1-4` and `SRC-D-WHERE-2`: three rode trailing comments on individual `Acquisition` fields,
one on the `TopKExtensions` ordinal-histogram member, and one on the `component` field of two DTO
structs — sites the claims script had no `refs:` line for. **Every witness stayed green while they
went**: comment-only passed, the grammar gate passed, both toolchains passed, and
`registry_grammar_lint` passed at 95 claimed and 95 declared, because all five are declared and
cited elsewhere in the repo so nothing dangled at the workspace level. What was lost is
addressability at the obeying site, and no instrument in the protocol looks for it. Repaired before
the commit by adding four `refs:` lines, and the per-file DISTINCT-set census now returns empty.
**The same census was then re-run over all fifteen files this run has converted: unit 10 was the
only loss.**

**Interrogation** — one fresh agent, ten questions, 26 tool uses, 111 k tokens, 4.6 minutes.
Transcript checked: `GIT COMMANDS RUN: none`. **Score: 9 of 10 recovered, 0 not recovered, 1
WRONG — and the wrong one was a line this conversion wrote.**

**THIS ENTRY FIRST RECORDED 10 OF 10 AND THAT WAS THIS LANE'S ERROR, corrected in a second commit
rather than an amend (`OPS-8.S10`).** The score was written from the reader's summary before its
per-question evidence was read in full; Q2's answer carries an explicit disagreement verdict that a
skim reads as agreement. The correction is recorded here rather than silently applied, because a
migration ledger whose scores are optimistic is worth less than one that is short.

**THE LAW BLOCK WAS FOLLOWED, WHICH IS THE FIRST DIRECT TEST OF THE FORM IN THIS REPO.** Q6 asked
where the reasoning for rendering `run_outcome` through the spec helper rather than
`insight::to_string` is recorded. The reader followed the `refs:` address to the block in
`src/stats/wire_format.cpp`, read the argument out of it — the two minted vocabularies, the closed
schema enum, the governance clause that decides which side moves, and the rejected alternative with
its cost — and then corroborated it against the schema independently. Before unit 8 that reasoning
lived in a prose paragraph pointed at by *"see the argument at its definition"*; the reader now
reaches it by address.

**THE WRONG LINE — Q2, and it is `OPS-8.O3`'s lesson firing a second time in this run.** This
conversion wrote, on `approximate_cardinality`:
`// note: approximate_cardinality is HLL-derived: same-machine replay only, not cross-machine.`
It was a faithful compression of the deleted prose, and **both are false.** The reader reported the
contradiction and this lane re-derived it at two sources: `metalog-spec/SPEC.md` §3.5 states the
field **MUST** be computed *"deterministically — no libm transcendentals — via an exact dyadic
register sum plus a fixed-point logarithm, so that it is bit-identical across"* machines; and
`HyperLogLog::estimate()` does exactly that — every `double` in it is `constexpr`, the numerator
reaches `u128` through `std::bit_cast` and integer shifts, and the small-range arm uses
`det_ln_fixed`, so no runtime float and no libm call exists on the path. The deleted prose was
**stale**: it predates the determinism hardening that unit 2's own reader independently established
three units earlier. The line now reads
`// note: an HLL estimate, but no libm and no runtime float, so it is bit-identical.`
The reader bounded its own verdict honestly — high confidence that note and implementation
disagree, medium on which is stale — and the spec settles it.

**A SECOND DEFECT THE READER FOUND WHILE ANSWERING Q6 CORRECTLY: a misplaced `refs:`.** The
`note:` and `refs: LSRC-8` for the run verdict had landed above `coordinate`/`cube` rather than
beside the `run_outcome` member they annotate — the claims script anchored on "the first code line
at or after" a line number, and three unrelated members sat between the block and its subject. Both
lines were moved to sit directly above `run_outcome`. **The gate cannot see this class**: placement
is exactly what `ADR-26.D5` says the checker does not verify, *"because checking placement would
mean parsing C++ with a second, weaker parser."* A tagged line on the wrong declaration is a false
contract, and only a reader catches it.

One answer went past the prose it replaced: Q7 bounded the emit gate correctly, distinguishing the
per-row block (nothing lost — the base element is recoverable from the standard members) from the
document roll-up (not derivable, but a single-window range has no oscillation to report).

**Declared limit on this one measurement:** the four repair `refs:` lines
landed while the reader was working, so its tree was not frozen for the whole read. The added lines
are bare registry addresses carrying no prose, and none of the ten questions asks about a `SRC-`
code, so the exposure is nil in substance — but the run is not reproducible byte-for-byte and this
ledger says so rather than implying it was.

**Witnesses.** Comment-only: code token stream byte-identical to `HEAD`, re-taken after the repair.
Grammar: `malf format --check` over the file — 121 comment lines, forms `pre=1 post=5 invariant=27
assert=2 note=28 refs=24 continuation=31 tool=3`, **0 would-be violations**. Behaviour: batch E —
297 of 297 on clang-21 and 297 of 297 on gcc-16, at a slot acquired in the foreground whose stamp
read alive before the release. Comment lines 329 → 121 (63 % fewer); would-be violations 326 → 0.

---

## Unit 11 — `api/` · `metalog.cppm` (1 file, 326 would-be violations)

The facade module: the `MetaLogEngine` class with its whole private state, and the free
`to_json` / `compose` / `diff` / `comparison_outcome_of` / `withheld_signals_of` /
`cube_cardinality` / `collapse_note` declarations. 327 comment lines, 326 of them violations
(301 bare, 17 spacer, 8 trailing), 1 tool form. Split from `api/metalog.api.cppm` by file group
(`OPS-8.S2`): 1 416 comment lines across the two is one questionnaire pretending to be two.

**Census (`OPS-8.S4`).** Zero `NOLINT` of any spelling, zero `/*name*/` or `/*name=*/`, zero
`clang-format off`, zero `wall-clock:`, zero `SPDX-License-Identifier:`; one
`} // namespace insight::metalog` closer. Identical after the strip. No census decision needed.

**Stripper cross-check (`OPS-8.S5`).** No suppression in the file, so
`removed == violations − (suppression-without-why + trailing-nolint)` reduces to
`removed == violations`: 326 == 326, kept 1 == the one tool form.

**The `SRC-<code>` read this unit was blocked on, and its answer (`OPS-8.O5`).** The previous run
left `api/` marked *"must be read for statement-bearing `SRC-<code>` sites first"*. It was read.
`api/metalog.cppm` carries 13 distinct codes over 22 occurrences, and **every one of them is a
CITATION, not a declaration** — so this unit owes no law block and was not blocked:

| code | where its statement actually is | how that was established |
|---|---|---|
| `SRC-D-TIR-5` (×4) | `api/metalog.api.cppm`, at `TemplateRegistry` | the deleted prose said so itself, at all four sites |
| `SRC-D-OTEL-11` (×4) | `insight-canon/core/api/canon.api.cppm` and `core/src/strategy/json.cpp` | `ADR-29.D6` rules that the `SRC-D-OTEL-*` statements live in canon's interface; the sites are there |
| `SRC-D-OTEL-21` (×3), `SRC-D-OTEL-9` (×2), `SRC-D-OTEL-1`, `SRC-D-OTEL-13` | design-note slots (`DN-029`, `DN-014`, `DN-008`) plus, for `-9`/`-21`, `insight-canon/core/api/canon.spi.cppm` | doc-tier sweep + the canon sites |
| `SRC-D-TIR-2` (×2), `SRC-D-WHERE-2`, `-4`, `-5`, `SRC-D-W1-2`, `-5`, `SRC-D-PROV-1` | design-note slots (`DN-001`, `DN-002`, `DN-054`, `DN-029`) | doc-tier sweep |

Every code stays in a `refs:` line **in the same `.cppm`**, so the position class
`registry_grammar_lint` reads a declaration by is unchanged: after the conversion it reports
**95 claimed codes, 95 declared in source, 0 failures**, the same as before.

**A NEAR-MISS ON `OPS-8.O3`'s MIRROR LESSON, recorded because the search that nearly failed is the
interesting half.** A first sweep for `SRC-D-OTEL-11` truncated its per-code output at twelve lines,
all twelve from `insight-metalog`, and the draft finding was *"`ADR-29.D6` claims the `SRC-D-OTEL-*`
statements live in `insight-canon`'s interface and for `-11` there is no canon site at all"* — which
would have been a false defect filed against a true ADR slot. Re-running the sweep scoped to
`insight-canon` alone returned `core/api/canon.api.cppm:335`, `:361` and
`core/src/strategy/json.cpp:541`. The finding was never filed. The lesson generalises past the
truncation: **a per-code sweep whose output is capped reads as a complete population**, and the
repo-boundary rule `OPS-8.O3` states does not save you if the sweep that crosses the boundary is
itself truncated.

**The address census, before and after (`OPS-8.S10`).** Every registry address the deleted prose
carried survives: `ADR-16.D5`, `ADR-9.D2` ×3, `ADR-9.D3` ×2, `DN-32.D3`, `DN-50.D4`, `DN-56.D2`,
`DN-56.D3`, `DN-64.D3`, `DN-65.D1`, `DN-65.D5` and the 13 `SRC-` codes at their original counts.
`ADR-3.D4` went 2 → 1: both occurrences were in the one file-header block and one `refs:` carries
it. **`ADR-29.D2` was ADDED** (0 → 2) — the declared-edge rule the span sites obey has an owner,
and naming it is what makes those two sites cite a statement rather than a code alone.

**The claims.** 57 blocks: `pre` 4, `post` 27, `invariant` 41, `note` 10, `refs` 30, with 41
untagged continuations. `refs:` targets: `ADR-3.D4`, `ADR-9.D2`, `ADR-9.D3`, `ADR-16.D5`,
`ADR-29.D2`, `DN-32.D3`, `DN-50.D4`, `DN-56.D2`, `DN-56.D3`, `DN-64.D3`, `DN-65.D1`, `DN-65.D5`,
`SRC-D-OTEL-1`, `SRC-D-OTEL-9`, `SRC-D-OTEL-11`, `SRC-D-OTEL-13`, `SRC-D-OTEL-21`, `SRC-D-PROV-1`,
`SRC-D-TIR-2`, `SRC-D-TIR-5`, `SRC-D-W1-2`, `SRC-D-W1-5`, `SRC-D-WHERE-2`, `SRC-D-WHERE-4`,
`SRC-D-WHERE-5`, `F-SRC-insight-metalog:test_golden_vectors.cpp`,
`F-SRC-insight-metalog:spec_conformance_gate.sh`. **No law block owed** — see the table above.

**Four determinism claims were verified at source before being asserted**, because unit 9's reader
had caught exactly this shape ("point-lookup only") false for half of one map. `declared_level_counts`
is read only by `declared_levels.find(*dominant)` in `dominant_event_level_of` (`src/stats/salience.cpp`)
— never iterated, so the `invariant:` stands. `trace_rings_` and `span_templates_` take only
`clear`/`find`/`contains`/`size`/`erase`/`emplace` in `src/engine/engine.cpp` — no range-for anywhere,
so both `point-lookup only` lines stand. `component_counts` **IS** range-iterated
(`engine.cpp:1146`, into a sum and a `distinct` set), so no never-iterated line was written for it;
what was written is the `always populated` invariant and the WHERE-label fact.

**Interrogation** — one fresh agent, fifteen questions, 67 tool uses, 205 k tokens, 8.1 minutes.
Exclusions given: `OPS-8`, `ADR-26`, any `ccc_migration.md` in any repo including this one, any
`ccc_migration_tools` directory. Transcript checked: `GIT COMMANDS RUN: none`, and no `git`
invocation appears in its tool calls. **Score: 15 of 15 recovered, 0 not recovered — and ONE
`refs:` THIS CONVERSION WROTE NAMED THE WRONG WITNESS.**

Five answers went past the prose they replaced. Q6 named the failure-cue band constant
(`kBandFailureCue` = 70) and the test that shows one genuine runtime occurrence restores the tier.
Q7 found that `Terminator` is the only structural role that scores at all, at 90. Q8 read the
committed benchmark results and quoted `allocs_per_event = 1.0` with 47.48 ns/event on the 16-char
arm against 39.49 and zero allocations on the 15-char control. Q10 found the fixture that measures
the trace-scoping claim — 4 real within-trace transitions and 0 noise edges scoped, against 0 real
and 6 noise on one global ring. Q12 found the cap-probe window that refuses 1 903 observations,
which is the number that makes "a per-drop warning is a log flood" a fact rather than an opinion.

**THE WRONG WITNESS.** This conversion wrote, at the `close_window` orchestration block:
`// refs: F-SRC-insight-metalog:test_determinism_gate.cpp`, beside the `invariant:` about the
const and non-const steps. The reader answered Q11 by refusing the premise — nothing in the tree
proves equivalence with the pre-split code, which is right, that claim was history and was deleted
— and then named what WOULD catch a difference: `tests/operations/test_golden_vectors.cpp`, four
byte-exact records per corpus compared record-for-record. **Confirmed at source**, and the
confirming sentence is in the file this conversion had cited: `test_determinism_gate.cpp`'s own
header says *"There is NO committed golden hash here (or anywhere) — determinism is proven by
cross-leg AGREEMENT … These tests keep the SCENARIOS non-hollow and pin the derived field
VALUES."* The `refs:` now names `test_golden_vectors.cpp`, whose header says it pins the wire
bytes with no free field at compare time. `OPS-8.S7` steps 2 and 3 were re-run after the edit:
0 would-be violations, comment-only against `HEAD`, and `registry_grammar_lint` 0 failures with the
new address resolved on its source leg.

**A defect in this run's own QUESTION, not in the tree.** Q4 asked about
`TemplateBucket::first_seen_index`. The reader opened its answer by correcting the name — the type
is `MetaLogEngine::Bucket` and no `TemplateBucket` exists anywhere — and then answered the question
that was meant. A questionnaire that names a symbol wrongly invites a *"the code does not say"*
that would be scored as a knowledge loss; the reader read the tree instead of the question, which
is the behaviour the prompt is asking for, but the next lane should spell symbols from the file.

**An inconsistency between the two `api/` files, recorded rather than repaired.** The deleted prose
in `metalog.cppm` said the transparent-hash fix removed **1** general-heap allocation per event for
an over-SSO component; `api/metalog.api.cppm` — not converted by this unit — says **2**, with
~19 ns/event on gcc-15 = 28 % of `ingest_event`. Both can be true at different scopes: two hot-path
maps take a key per event, and the `metalog.cppm` site spoke about `component_counts` alone. No
number was carried into a tagged line, so this conversion asserts neither. The `api/metalog.api.cppm`
unit should resolve which scope its "2" is stated at.

**Witnesses.** Comment-only: code token stream byte-identical to `HEAD`, re-taken after the `refs:`
repair. Grammar: `malf format --check` over the unit — 154 comment lines, forms
`pre=4 post=27 invariant=41 note=10 refs=30 continuation=41 tool=1`, **0 would-be violations**.
Comment lines 327 → 154 (53 % fewer); would-be violations 326 → 0. Behaviour: batch A below.

---

## Unit 12 — `benchmarks/` (8 files, 241 would-be violations)

The whole benchmark harness: the shared module (`metalog.bench.cppm`), the custom entry point
(`bench_main.cpp`), the global-heap counting probe (`heap_probe.hpp`/`.cpp`), the two
key-allocation arms (`bench_cube_key_alloc.cpp`, `bench_ordinal_key_alloc.cpp`), the compression
and ingest-cost benchmark (`bench_metalog.cpp`) and the stage-level cube benchmark
(`bench_compose_diff_cube.cpp`). 250 comment lines, 241 of them violations (202 bare, 22 spacer,
14 trailing, 3 suppression-without-why), 9 tool forms.

**Census (`OPS-8.S4`), and the one census DECISION of this run.** `NOLINT` 6 before, **0 after**;
zero `/*name*/` or `/*name=*/`, zero `clang-format off`, zero `wall-clock:`, zero SPDX, before and
after. The six were three file-wide `NOLINTBEGIN`/`NOLINTEND` pairs and **all three were deleted
with the measurement that they silence nothing.** Two suppress `readability-magic-numbers`
(`bench_cube_key_alloc.cpp`, `bench_ordinal_key_alloc.cpp`) and one `cppcoreguidelines-owning-memory`
(`heap_probe.cpp`); both check families are disabled in `malf/config/.clang-tidy`, which is the one
file `malf lint` and `.clangd` both read. **The measurement, not the config read alone**: a fixture
carrying three magic numbers and a raw `malloc` return produced **zero** diagnostics of either
family under that config, and **3 magic-number plus 1 owning-memory** diagnostics on the same bytes
with the two checks explicitly enabled. **Caveat recorded rather than discovered later:**
`cppcoreguidelines-owning-memory` is disabled because it crashes clang-tidy on module code, not
because it is unwanted, and `heap_probe.cpp` — which replaces the global allocation functions — is
exactly the site that would fire if it is ever re-enabled.

**Stripper cross-check (`OPS-8.S5`).** `removed == violations − (suppression-without-why +
trailing-nolint)`. In aggregate: **238 removed against 241 would-be violations, the difference being
exactly the three kept `NOLINTBEGIN` lines**. Per file — `metalog.bench.cppm` 5 == 5, `bench_main.cpp` 10 == 10, `heap_probe.hpp` 12 == 12, `heap_probe.cpp`
6 == 7 − 1, `bench_cube_key_alloc.cpp` 31 == 32 − 1, `bench_ordinal_key_alloc.cpp` 52 == 53 − 1,
`bench_metalog.cpp` 62 == 62, `bench_compose_diff_cube.cpp` 60 == 60.

**Two claims deleted as MIRRORS of a compile-time check, which is the repair the grammar asks for.**
`bench_ordinal_key_alloc.cpp` carried *"the catalog membership of each real key is PINNED, so the arm
names stay true if the catalog is ever edited"* — five `static_assert`s directly below say exactly
that, three on the key lengths (15, 16, 23) and two on catalog membership, each with its own message.
A comment carrying a value the compiler checks is a mirror and goes.

**One stale pointer deleted, and where the search went before condemning it.**
`bench_main.cpp` opened with *"Mirrors tokenization/benchmarks/bench_main.cpp"*. There is no
`tokenization/` directory anywhere in the workspace; `find` for `*/benchmarks/bench_main.cpp` returns
four files, in `insight-metalog`, `insight-eidos/detection`, `insight-eidos/engine` and
`logcraft/core`, and none in `insight-canon`, which is what `tokenization` was renamed to. The claim
is a history pointer with no residual contract, so it goes on both counts.

**The claims.** 40 blocks: `pre` 1, `post` 8, `invariant` 37, `note` 8, `refs` 6, with 31 untagged
continuations. `refs:` targets: `ADR-3.D4`, `ADR-9.D2` ×2, `ADR-19.D1`, `DN-53.D7`,
`F-SRC-metalog-spec:SPEC.md`. **No law block owed:** the transparent-hash idiom is `ADR-9.D2`'s, the
always-on cube `ADR-19.D1`'s, the logging silence `DN-53.D7`'s, and the byte budget the published
specification's.

**Interrogation** — one fresh agent, thirteen questions, 40 tool uses, 127 k tokens, 6.3 minutes.
Same exclusion list as unit 11, plus `technical_docs/DONE.md` (it records this benchmark's own
measured figures and would read as an answer key). Transcript checked: `GIT COMMANDS RUN: none`.
**Score: 13 of 13 recovered, 0 not recovered — and THIS CONVERSION HAD DELETED A TRUE CLAIM, which
the reader restored.**

**THE WRONGLY-CONDEMNED CLAIM, and it is the finding of the unit.** `bench_metalog.cpp` carried
*"The '≤ 4 KB per million lines' architectural target lives in technical_docs/overview/architecture.md
§6 and is aspirational"*. This lane filed it as stale: there is no `technical_docs/overview/`
directory, and the figure appears nowhere in the live doc tier. **Where the search went, so the gap
is nameable:** the full `technical_docs/` top-level listing, an `rg` over `technical_docs/`,
`insight-metalog/technical_docs/` and `insight-metalog/benchmarks/` for
`4 ?KB per million|bytes per million|BytesPerMillion`, `DN-024` opened directly, and an `rg -l` over
`technical_docs/adr`, `product` and `bibles` for `byte budget|BytesPerMillion|compression budget`.
Every one of those is inside the superproject's own doc tier or this repo. **The reader crossed the
repo boundary and found it alive**: `metalog-spec/SPEC.md` § 11.5, *"the headline '≤ 4 KB per MetaLog
covering ≥ 1 M log lines' target is a statement about the `stats`-only document — no `reservoir`, no
`behavior`, no `cube`"*, reached at `top_k_size ≤ 32` inline or `≤ 64` id-only; named again in
`metalog-spec/README.md` and restated in `technical_docs/LEXICON.md`. Verified here at the source.

This is `OPS-8.O3`'s mirror lesson firing on the lane that wrote the runbook amendment. **A deletion
leaves no witness**, so without the question the pointer would have gone silently and the benchmark
would carry no address for the budget it exists to track. The line is re-homed as a `note:` naming
the SCOPE — the target is stats-only and this arm carries the behavior block and the cube, so it is
**not** measured against it — with `refs: F-SRC-metalog-spec:SPEC.md`. The reader also priced the
gap: the published 100 000-line arm reads `per_million=160280`, about **156 KB per million lines**,
roughly forty times the headline, against a scope the headline does not govern.

**A second line strengthened on the reader's evidence.** `bench_main.cpp`'s `pre:` said *"nothing
else in main has run yet"* — true, but not the mechanism. The reader found the load-bearing one: the
ASLR re-exec must precede `benchmark::Initialize`, **which consumes and rewrites `argc` and `argv`**.
The `pre:` now names that. `OPS-8.S7` steps 2 and 3 re-run after both edits.

**Findings for other lanes.**
* **`bench_metalog.cpp` breaks the rule its sibling benchmark states — for Kleio, with Argos to say
  whether the published numbers move.** `bench_compose_diff_cube.cpp` refuses a
  `std::*_distribution` by name because its draw sequence is unspecified and differs across standard
  libraries. `bench_metalog.cpp` uses one at **three** sites on `std::mt19937` —
  `std::uniform_real_distribution<double>` in `run_once`, `std::uniform_int_distribution<int>` in the
  field-histogram arm and `std::uniform_int_distribution<std::size_t>` in the WHERE arm — and
  `BM_MetaLogCompress` additionally advances its seed per iteration, so its corpus is not fixed even
  within one run. Its figures are therefore not comparable across the two toolchains they are
  measured on, and `coderoast-hub/benchmarks/` publishes them.
* **A standing guard that nothing enforces — for Kleio.** `bench_ordinal_key_alloc.cpp` is described
  as the regression guard for zero allocations per event, and the reader confirmed that **nothing in
  the tree compares `allocs_per_event` to a threshold**: a run reading 1 again blocks nothing and has
  to be noticed by a human. A search over `insight-metalog`, `coderoast-hub`, `.github` and `malf`
  finds the identifier only in the two benchmark sources and one published column header.
* **A gap in `heap_probe`'s own statement — for whoever next touches it.** The reader observed that
  the nothrow allocation forms are named nowhere, so the tree does not say whether they are counted.
  The old prose did not say either, so nothing was lost; the gap is pre-existing.

**A trap this unit hit that `OPS-8` does not name.** Stripping a file-leading comment block leaves
the blank line that followed it, so six of the eight files came out of the strip with a **leading
blank line**. `malf format --check` reports 0 misformatted on it and the CCC phase counts no
violation, so nothing catches it; the code-token witness is blind to it by construction. Removed by
hand and re-witnessed; a repo-wide sweep confirms zero leading blanks in any C++ file.

**Witnesses.** Comment-only: all eight files, code token stream byte-identical to `HEAD`, re-taken
after both repairs and after the blank-line removal. Grammar: 97 comment lines, forms
`pre=1 post=8 invariant=37 note=8 refs=6 continuation=31 tool=6`, **0 would-be violations**.
Resolution: `registry_grammar_lint` 0 failures, `F-SRC-metalog-spec:SPEC.md` resolved on its source
leg and proven non-vacuous by a negative control — substituting a repo name that does not exist
makes the arm report it, substituting it back makes the report go. Comment lines 250 → 97 (61 %
fewer); would-be violations 241 → 0. Behaviour: batch A.

---

## Unit 13 — `scripts/` (9 files, 545 would-be violations)

The determinism harness: the eight shared `*_scenario.hpp` windows and `determinism_fixture.cpp`,
the binary `scripts/determinism_bitidentity.sh` builds across the compiler × optimisation ×
floating-contraction matrix. 554 comment lines, 545 of them violations (464 bare, 44 spacer, 35
trailing, 1 ruler, 1 suppression-without-why), 9 tool forms.

**Census (`OPS-8.S4`), and the decision it forced.** `NOLINT` 2 before, 2 after and byte-identical;
zero `/*name*/` or `/*name=*/`, zero `clang-format off`, zero `wall-clock:`, zero SPDX, before and
after. The two are `determinism_fixture.cpp`'s `// NOLINTBEGIN Test` and `// NOLINTEND Test` — **a
directive with no parenthesis at all**, which clang-tidy parses as bare, so it suppresses every
check over the whole file. It is re-homed under a `note:` stating the measured scope and **left
byte-identical**: narrowing it changes what the linter checks and is not a comment-only act. The
repair is a finding below.

**What the bare directive hides, measured rather than reasoned.** `clang-tidy-21` over the file with
and without the pair, under the one shared `malf/config/.clang-tidy` and the file's own compile
command from the clang build's database, differ by **exactly 9 diagnostics**: 1
`bugprone-exception-escape` on `main` — and `bugprone-*` is in `WarningsAsErrors`, so this file would
FAIL rather than warn — 6 `readability-identifier-length` on `t0`/`t1`/`t2` at two sites, and 2
`readability-use-concise-preprocessor-directives`. **`scripts/` is not in `malf lint`'s prune list**
(that list is `tests/`, `benchmarks/`, `test_package/`, `technical_docs/`), so the file is walked and
the blanket is live. A first run of the same comparison reported a tenth diagnostic, a
`clang-diagnostic-error` for a header not found; that was an artifact of running over a scratchpad
copy whose quoted include could not resolve, and it disappears once the real `scripts/` directory is
on the include path. It is named here so the number 9 is not later read as 10.

**Stripper cross-check (`OPS-8.S5`).** removed 544 == 545 − 1 kept suppression; kept 10 == 9 tool
forms + 1.

**The claims.** 58 blocks: `pre` 8, `post` 20, `invariant` 57, `note` 12, `refs` 11, with 51
untagged continuations. `refs:` targets: `ADR-9.D3`, `ADR-17.D8`, `ADR-29.D2`, `ADR-31.D8` ×3,
`ADR-20.D7`, `DN-42.D18` ×2, `DN-53.D3`, `DN-82.D2`, `SRC-D-OTEL-21` ×2. Both `SRC-` sites are
citations, not declarations: the code's statement is in `insight-canon`'s interface
(`ADR-29.D6`), and `service_edges_overcap_scenario.hpp` — a declaring-POSITION file — keeps the code
in a `refs:` line in the same header, so the position class does not move. **No law block owed.**

**Interrogation** — one fresh agent, thirteen questions, 74 tool uses, 178 k tokens, 7.4 minutes.
Same exclusion list as the other units. Transcript checked: `GIT COMMANDS RUN: none`.
**Score: 13 of 13 recovered, 0 not recovered — and ONE CLAIM THE OLD PROSE MADE WAS FALSIFIED, after
this conversion had already asserted it.**

**THE FALSE CLAIM.** `collapse_depths_scenario.hpp` said its pair is read at the minimal common
collapse and *"the diff's axes equal NEITHER input's"*. The conversion carried that into an
`invariant:` at the header and a `post:` in the fixture — a claim it now asserted. The reader read
`min_common_collapse` in `src/cube/cube.cpp` — `max` of the band floors, `min` of the where depths —
and observed that with one side un-collapsed it returns the collapsed side's state **exactly**, so
the diff's axes equal the **collapsed** input's. **Verified here at the source, and the confirming
sentence sits in the test that pins the case**:
`tests/determinism/test_determinism_gate.cpp`'s `CollapseDepthsPairIsReadAtTheMinimalCommonCollapse`
says *"the diff is read at the COARSER of the two — the max band floor — so its axes equal the
COLLAPSED input's and not the un-collapsed one's"*, and it asserts `EXPECT_NE` against the current
window alone, deliberately asserting no equality. Both lines now say the collapsed input's axes.
`OPS-8.S7` steps 2 and 3 re-run after the edit.

**A second line corrected.** `corpus_windows_scenario.hpp`'s `invariant:` named **two** callers; the
reader found **three** — `scripts/determinism_fixture.cpp`, `tests/operations/test_golden_vectors.cpp`
and `tests/operations/test_stability_vs_diff_divergence.cpp`. The line no longer enumerates.

**Four answers went past the prose they replaced.** The reader named the exact canon mask rule that
makes the n-gram scenario's already-canonical feed load-bearing — a digit-leading whole token masks
to a wildcard, so all 6 000 templates would collapse into one, the stream would form one self-loop
bigram, nothing would be refused and the section would go silently hollow. It reproduced the
streaming reservoir's measured mutation from the in-suite guard — inverting the edge tie-break moves
`ambiguous` 8 → 0 and `error_class` 16 → 24 while `reservoir.size()` stays at M, which is the whole
reason a size assertion is blind. It byte-scanned the seven committed corpus files and found **zero**
CR bytes, so the carriage-return strip is a general-case guard and not a live one. And it placed the
collapse-depths pair between the two spec clauses it exists to arbitrate — § 13.6's unbolded
equality comment against § 16.10's compare-at-min mandate.

**A finding the reader produced that no prose carried — for the lane that owns the corpus sections.**
`corpus_windows_scenario.hpp`'s `configure()` never sets `cfg.ruleset`, so the emitted document omits
the `ruleset` block entirely. The empty composition is a deliberate choice with a stated reason, but
its `semantic_identity` is not declared on the wire, and a consumer reads that absence as *legacy
producer*, not as *composed against nothing*. Recorded, not acted on: it is a producer decision, not
a comment.

**Witnesses.** Comment-only: all nine files, code token stream byte-identical to `HEAD`, re-taken
after the three repairs. Grammar: 169 comment lines, forms
`pre=8 post=20 invariant=57 note=12 refs=11 continuation=51 tool=10`, **0 would-be violations**.
Comment lines 554 → 169 (69 % fewer); would-be violations 545 → 0. Behaviour: batch B.

---

## Unit 14 — `test_package/` · `test_package.cpp` (1 file, 16 would-be violations)

The external-consumer smoke test: two gtest cases that build a document through the package's
public module surface and assert the wire contract as an outside consumer would see it. 16 comment
lines, all 16 violations (15 bare, 1 trailing), zero tool forms.

**Census (`OPS-8.S4`).** Zero `NOLINT`, zero `/*name*/` or `/*name=*/`, zero `clang-format off`,
zero `wall-clock:`, zero SPDX, before and after. No census decision needed. **Stripper
cross-check:** removed 16 == 16 violations (no suppression), kept 0 == zero tool forms.

**The claims.** 6 blocks: `invariant` 4, `assert` 3, `note` 1, `refs` 1, with 4 untagged
continuations. `refs:` target: `F-SRC-metalog-spec:SPEC.md`. **No law block owed.** One history
claim deleted: *"the two moved apart when producer 0.6.0 stayed frozen through the whole 1.x line"*
— the FACT that the package version and the specification version are two different things is kept
as an `assert:`; the episode that made them diverge is history and belongs to the record tier.

**Interrogation** — one fresh agent, five questions, 31 tool uses, 95 k tokens, 2.6 minutes. Same
exclusion list as the other units. Transcript checked: `GIT COMMANDS RUN: none`. **Score: 5 of 5
recovered, 0 not recovered, no line corrected.**

The reader traced `INSIGHT_METALOG_TESTED_VERSION` back through **both** of its producers — the
recipe's `generate()` under `conan create`, and `malf` passing `-DMALF_TESTED_VERSION` when it
configures the directory for the editor index — and named the CMake `FATAL_ERROR` that refuses a
default rather than inventing one. It then answered the claim this unit had held out of the tree.
The deleted prose said this was *"the only gate outside the cut checklist that reads the emitted
value"*; the reader found a second, `tests/operations/test_golden_vectors.cpp`'s
`ProducerVersionIsStampedFromTheOnePackageConstant`. **The two are not the same check**: that one
compares the emitted value against the package's OWN constant, so it proves the engine stamps it and
by construction cannot see the constant go stale — which is exactly the gap the macro covers. No
claim was carried into a tagged line on this point, so nothing was asserted and nothing needed
repair; the held claim is recovered in its true, narrower form.

**Witnesses.** Comment-only: code token stream byte-identical to `HEAD`. Grammar: 13 comment lines,
forms `invariant=4 assert=3 note=1 refs=1 continuation=4`, **0 would-be violations**. Comment lines
16 → 13; would-be violations 16 → 0. Behaviour: batch B.

---

## Unit 15 — `api/metalog.api.cppm` (1 file, 1 088 would-be violations)

The public DTO surface and the last source unit: 1 910 lines, 200 comment blocks, 1 089 comment
lines. Converted as ONE unit because the grammar witness is per file — half a converted file still
reads violations — and interrogated by **two** cold readers over disjoint question sets, because
one questionnaire over 200 blocks is two interrogations pretending to be one.

**Census (`OPS-8.S4`), derived rather than taken from the written list.** The gates that read a
comment token in this repo were enumerated first: `malf/comment_contract_lint.py` (the CCC tool
forms), clang-tidy (`NOLINT` in all spellings, `/*name*/`, `/*name=*/`), clang-format
(`clang-format off`/`on`, the namespace closer), `scripts/wallclock_lint.py` and
`scripts/random_determinism_lint.py` (`DETERMINISM-ALLOW`), `scripts/log_seat_routing_lint.py`
(`LOG-SEAT-ALLOW`), `scripts/retired_structure_lint.py` (its `allow` marker, whose regex does not
require an HTML comment and can therefore sit in C++), plus `wall-clock:` and
`SPDX-License-Identifier:`. Two scope facts fell out of that walk and are recorded because they
bound what the census can mean here: `wallclock_lint.py`'s `SCOPE` covers `insight-canon` and
`insight-eidos` only, so its token has **no reader in this repo**, while
`random_determinism_lint.py`'s `SCOPE` **does** name `insight-metalog` (6 module interface units),
so `DETERMINISM-ALLOW` does have one. Counted in this file: every token above at **0**, one
namespace closer at **1**. After the strip: identical. No census decision was needed.

**Stripper cross-check (`OPS-8.S5`).** The file carries no suppression of any kind, so
`removed == violations - (suppression-without-why + trailing-nolint)` reduces to
`removed == violations`: the stripper removed **1 088** and the whole-repo gate moved
**3 717 -> 2 629**, which is 1 088 exactly. Per class: bare 3 265 -> 2 301, spacer 243 -> 197,
trailing 178 -> 102, ruler 2 -> 0.

**The `SRC-<code>` boundary at this site, and where the previous run's census was short
(`OPS-8.O5`).** The previous session left a table of **three** codes whose STATEMENT lives in this
file, each with an addressable owner to cite instead of a law block. Two of the three verify:

| code | the rule stated here | owner, verified by reading it |
|---|---|---|
| `SRC-D-TIR-5` | `TemplateRegistry` is the single id -> string home, display-only, append-only | **`ADR-16.D3`** states it verbatim: *"the engine-owned `TemplateRegistry` is the single `id -> str` home … display-only by construction: it never feeds a decision path, intern order never affects content, and it is append-only"*. TRUE. |
| `SRC-D-W1-4` | the schedule id is the diff's comparability key | **`insight-canon/core/api/canon.api.cppm`**: *"The schedule is a versioned catalog (`SRC-D-W1-4`): its stable string id is the eidos diff's comparability key."* TRUE. |
| `SRC-D-W1-1` | the exact-integer W1 distance and its frozen octave thresholds | **`STU-3.A1`** pre-registers the bands (*"a MED (>= 2 octaves) or HIGH (>= 5)"*, *"the LOW half-octave band"*) and names them pre-registered. TRUE for the THRESHOLDS. The *distance* half — exact integer, 128-bit reducer, no float, no division — is stated **only here**, and no slot owns it. |

**Two more codes are statement-bearing in this file and the previous table did not list them.**
`SRC-D-W1-5` (*a field is ordinal XOR categorical, so the two never collide*) declares at two
sites here — `DN-1` verified that independently and records it — and canon's mention of it is a
citation (*"the `SRC-D-W1-5` mis-route hazard"*), not the statement. `SRC-D-OTEL-21` (the distilled
service-edge block: its own additive flag-gated block with its own diff, not a cube dimension, not
folded into `top_ngrams`) declares here across nine occurrences, and **`ADR-29.D6` explicitly
refuses to own it**: *"the comment at the declaring site … IS the statement … This ADR owns the
subject … and deliberately does not mirror that enumeration."*

**What this unit did with them, and what it did NOT do.** Every rule above is written AT its site
as tagged contract lines, which is what the closed grammar asks of a statement, and every code
keeps its `refs: SRC-<code>` — so `registry_grammar_lint`'s position-based declaration check still
sees a declaring site in this interface unit and G5 stays green. **No law was minted**: numbering is
workspace-global and this lane may not pick one (`OPS-8.O4`). What remains owed, for the pilot to
issue numbers against, is the ADDRESSED form for the three rules that have no owner —
`SRC-D-W1-1`'s exact-integer distance, `SRC-D-W1-5`'s ordinal-XOR-categorical exclusivity, and
`SRC-D-OTEL-21`'s service-edge block — together with the cross-repo citer repointing, which
`OPS-8.O5` places with the pilot and not with a lane.

**Forms written and the file's own reading.** 208 insertions producing, at the file's own
standalone gate reading, **`pre` 4 · `post` 10 · `invariant` 335 · `assert` 2 · `refs` 83 ·
continuations 201 · tool forms 1** over **636 comment lines**, from 1 089 before — a 42 %
reduction, against the repo's running 62 %. **Zero `note:` lines were written**, which is the
form the doctrine says to starve, and the reason this surface produced none is structural: a DTO
member's residual claim is almost always a population condition, an absence semantics or a
representation contract, and each of those is an `invariant:` by definition. The high invariant
count is the same fact from the other side — 60 exported types whose contract the C++ type system
cannot carry.

**The `refs:` targets used**: `F-SRC-metalog-spec:SPEC.md` ×11 · `SRC-D-OTEL-21` ×8 · `DN-50.D4`
×7 · `DN-64.D3` ×5 · `SRC-D-W1-2`, `SRC-D-W1-4`, `SRC-D-TIR-5`, `SRC-D-OTEL-11`, `DN-32.D3` ×4 ·
`SRC-II-7`, `SRC-D-WHERE-2`, `SRC-D-W1-1`, `ADR-31.D8`, `ADR-23.D4` ×3 · `SRC-D-WHERE-4`,
`SRC-D-W1-5`, `DN-64.D6`, `DN-64.D4`, `DN-50.D5`, `ADR-9.D2`, `ADR-17.D3` ×2 · and one each of
`STU-3.A1`, `SRC-D-WHERE-6`, `SRC-D-WHERE-5`, `SRC-D-W1-3`, `SRC-D-TID-16`, `SRC-D-RNK-2`,
`SRC-D-PROV-1`, `SRC-D-OUT-RUN-1`, `SRC-D-OTEL-9`, `SRC-D-OTEL-22`, `SRC-D-OTEL-20`,
`SRC-D-OTEL-13`, `SRC-D-OTEL-1`, `OPS-1.S15`, `DN-56.D2`, `DN-42.D17`, `BIB:determinism_model`,
`ADR-9.D3`, `ADR-31.D2`, `ADR-3.D4`, `ADR-29.D2`, `ADR-29.D1`, `ADR-23.D6`, `ADR-23.D3`,
`ADR-23.D2`, `ADR-17.D5`, `ADR-17.D2`, `ADR-16.D3`.

**A declared departure from this repo's own converted style, and it is deliberate.** The sibling
`api/` unit dropped every `SPEC §n` section number, because a `refs:` admits registry forms only.
Where the claim IS a specification MUST, the section number is the coordinate that makes the claim
checkable, so it is written **inside the tagged claim's own text** — a tagged line carries free
prose and the gate reads the tag, not the sentence. Where it was decoration it is gone.

**Interrogation — TWO cold readers over disjoint question sets, both spawned against a frozen
tree and both finished before anything was touched.** Reader A took Q1-Q22 (the ordinal/W1
channel, the transparent-lookup helpers, `TemplateRegistry`, presence churn, `TopKEntry`,
`AcquisitionBlock`, the service edges, the ruleset and transport blocks, the cube, the failure
frontier and the salience scale): 98 tool uses, 244 k tokens, 10.1 minutes. Reader B took Q23-Q42
(the retention axis, the reservoir entry, `StatsBlock`, the behaviour block, the two version
constants, `MetaLogDocument`, `MetaLogConfig`, the retention profile and the whole diff surface):
69 tool uses, 182 k tokens, 8.4 minutes. Both final messages carry `GIT COMMANDS RUN: none` and
neither transcript contains a `git` invocation. The exclusion list named this repo's own ledger,
LogCraft's, the `ADR-26` file and `OPS-8` — the first because from unit 2 onward it is an answer
key, the rest because they state the grammar.

**Score: 38 of 42 recovered, 0 not recovered, 4 wrong — and all four `wrong` verdicts are
CONVICTIONS** (`OPS-8.S9`, split (c)): in each one the reader answered the underlying question
correctly from the tree and thereby contradicted a line **this conversion wrote**. None is
reader-wrong and none is a tree that misleads, so none is evidence against the form. Scored from
the per-question evidence, never from a summary line.

**THE FOUR LINES THIS CONVERSION WROTE THAT A READER CAUGHT, each repaired before the commit.**

1. **`ordinal_w1`'s thresholds — an anti-endogamy claim resting on nothing.** The conversion wrote
   *"the thresholds are pre-registered, not fitted"* with `refs: STU-3.A1`. Reader A read
   `STU-3.A1` and found it **holds the three bands fixed as INPUTS** while pre-registering
   acceptance criteria for a different constant, the thin-sample floor; `DN-11` quotes them the
   same way, as a derivation input. The deleted prose asserted *"pre-registered (anti-endogamy)"*
   in one sentence and *"FROZEN from scenario-35's measured numerators"* with two quoted measured
   poles in the next, which is the tension the reader's answer resolves against the conversion.
   The line now states only what is checkable at the site — frozen bands, exact integer
   cross-multiply, conservative bias — and the pre-registration question is a finding below.
2. **`TemplateRegistry`'s out-of-line split — a reason that was REFUTED two days before the
   conversion carried it.** The conversion wrote *"every named member is defined in the
   implementation unit and never here — MSVC re-emits an out-of-line defaulted special member of a
   module interface into each importer"*. Reader A found in `DONE.md` that the MSVC half was
   measured on 2026-09-04 by a real MSVC golden run and **refuted**, after which the split was
   ripped and the six special members moved back into the class body — which is the shape the file
   carries today, so the hazard the line names cannot govern this class at all. The residue
   (*the named members live in the implementation unit*) is a mirror of the declarations. The
   whole line is deleted.
3. **`MetaLogDocument::acquisition` — "trivially copyable" is false of a struct holding a vector.**
   The conversion carried *"all-integer and trivially copyable, so `std::optional` is sound here"*.
   Reader B pointed at `AcquisitionBlock::where_cardinality_per_depth`, a
   `std::vector<std::uint64_t>`. The line now gives the ground its two neighbours already give and
   that does hold: stamped once at close and only read.
4. **`BehaviorBlock::dropped_ngram_observations` — a normative absence quoted without its scope.**
   The conversion wrote *"an omitted key MEANS zero"*. Reader B quoted the specification: absence
   is disambiguated by `metalog_version` — zero in a document declaring 0.7.0 or later,
   **UNKNOWN** in an earlier one, which consumers MUST NOT read as zero. The scope is now on the
   line.

**One more repair a reader prompted without contradicting anything.** `MetaLogDiff::cube_diff`'s
presence flag was justified by the MSVC precedent alone; reader B gave the stronger and more
useful ground, which is now written beside it: `CubeDiffBlock` carries `axes`, a required
descriptor, so *present-but-empty* is a real state that absence cannot express — where
`reservoir_delta`, which is three lists and nothing else, has emptiness AS its absence. And the
`refs: DN-42.D17` on `has_cube` was moved: it owns the minimal-common-collapse rule (its §4) and
nothing else in that block, and trailing four invariants it read as covering the MSVC line too.

**Stale, false and dangling prose deleted, each with the evidence and the search that backs it.**

1. **A FALSE ATTRIBUTION ON A LIVE ADDRESS, which is the worst shape because the address resolves.**
   `CubeCardinalityStat` carried *"The pre-collapse WARN thresholds (`kComponentWarn`/`kCellsWarn`)
   were RETIRED (ADR-18 / studies/005 disposition-D)"*. **`ADR-18` is
   `018-insight-intent-identity-and-alignment.md`** — it says nothing about the cube. The cube
   subject is `history/adr-v1/0018-cube-a-attribution.md`, i.e. the number is PRE-REFORM and now
   points at a different, live subject. A workspace sweep for `kComponentWarn`/`kCellsWarn` over
   every repo returns exactly one hit, `STU-5` line 127, which names `kComponentWarn=64` as latent
   and does not state a retirement; the constants exist nowhere in any source tree. The retirement
   is real and it IS sourced, just not where the prose pointed: `STU-5.O3` item 1 names
   `kComponentWarn=64` as the WARN monitor and not the collapse trigger, and
   `tests/cube/test_cube.cpp` states the retirement in its own comment beside the assertion that
   replaced it. The cold reader recovered both, plus the replacement itself — `collapse_note()`,
   fired by the eidos pipeline when a collapse is APPLIED. So nothing is re-homed: the false
   pointer is deleted and the fact is recoverable. The attic is not cited in its place, because a
   citation into it is owed nothing. A sweep of every other live `ADR-18` citation in the
   workspace found them all correctly about intent identity, so the class is one site wide.
2. **A DANGLING COMMENT DESCRIBING AN ENTITY THAT DOES NOT EXIST, its own sentence truncated.**
   Directly above `struct MetaLogDocument` stood *"Template-string emission mode (SPEC §3.4).
   Defined before MetaLogDocument so the document can carry"* — and nothing followed. No
   `TemplateStringMode`, `template_string_mode` or equivalent enum exists anywhere in
   `insight-metalog`, `insight-eidos`, `insight-canon`, `logcraft`, `coderoast-server` or
   `metalog-spec`. Deleted. The one live claim next to it — that this producer emits the INLINE
   mode only — was re-homed at the document, which is what it is about.
3. **A claim that is FALSE for composed documents, in the carried prose and twice over.**
   `AcquisitionBlock` and `MetaLogDocument::acquisition` both read *"Always present."*
   `src/operations/compose.cpp` never mentions `acquisition` and builds its output from a
   default-constructed `MetaLogDocument`, so a **composed document carries no acquisition block at
   all**. The lines now say what is true: `close_window()` sets it on every raw window and
   `compose()` sets none. The consequence for the consumer is a finding below.
4. **A file-header claim that reads as false and is at best ambiguous.** The header said
   *"Header-only structs (no impl units)"*, while `src/metalog.api.impl.cpp` is the implementation
   unit of this very module and defines `TemplateRegistry`'s six named members. Read as *the
   structs have none* it is true and it is a mirror; read as *the module has none* it is false.
   Deleted rather than repaired: no form admits it.
5. **Three citations into the ATTIC, removed by the conversion rather than by a decision.** The
   prose cited `cube_differential_axes.md §7.4`, `sift_where_attribution.md SRC-D-WHERE-4`, `SRC-D-WHERE-5` and
   `cube_perf_and_collapse.md §C`; all three live in `technical_docs/history/architecture-v1/`,
   which `CLAUDE.md` rules disposable and owed nothing. Each carried an `SRC-` code alongside, and
   the code is what the `refs:` now carries — the address survives a wipe of the attic and the
   path does not.

**The address census, both legs (`OPS-8.S7.3b`).**

*Outbound.* Three bare addresses were REFINED to the slot that owns the claim at that site
(`ADR-17` -> `ADR-17.D2`/`D3`/`D5`, `ADR-29` -> `ADR-29.D2`, `ADR-9` -> `ADR-9.D3`) and six were
ADDED (`ADR-16.D3`, `ADR-31.D2`, `BIB:determinism_model`, `DN-50.D5`,
`F-SRC-metalog-spec:SPEC.md`, `STU-3.A1`). One address the instrument called LOST was restored
before commit — `ADR-23.D6`, whose claim (*nothing here licenses a comparability statement across
transport*) had no other home in the file and is now an `invariant:` beside the `refs:`. One LOST
line stands as a **deliberate refinement with its evidence**: the bare `ADR-23` occurred at four
sites, and each is now cited at the slot that actually owns its sentence — the section header at
`ADR-23.D2`/`D4`/`D6`, and the two `transport` members plus the peel-boundary sentence at
`ADR-23.D4`, whose own title is *"The peel boundary: the identity path CANNOT see the stack, by
construction"*. Nothing points at a whole ADR where a slot exists.

*Inbound.* The committed instrument's inbound leg searches from the **current working directory
only** (`roots=["."]`), so run from inside the repo it cannot see a sibling repo naming your file —
which is exactly the class it exists to find. The sweep was therefore re-run workspace-wide with
the `CLAUDE.md` recipe. Leads and their disposition:

| citing site | what it rests on | verdict |
|---|---|---|
| seven `insight-eidos` sites (`sift.cppm`, `sift.api-config.cppm`, `diff_engine.cpp`, `reservoir.cpp`, `playground.contract.cppm`, `playground.replay.cppm`, `raw_log_fidelity_test.cpp`, `rule_backend_test.cpp`, `bench_pyramid.cpp`) saying *"see metalog.api.cppm (TemplateRegistry) for the contract"* | the `TemplateRegistry` contract at this site | **not falsified** — the contract is still stated here, as three `invariant:` lines plus `refs: ADR-16.D3, SRC-D-TIR-5`. The citation is a BASENAME and not an address; the repair is `F-SRC-insight-metalog:metalog.api.cppm:TemplateRegistry`, and it belongs to the `insight-eidos` lane |
| `insight-eidos/engine/tools/incident_episode_measure.cpp:1737` quoting `metalog.api.cppm: "a bounded floor of the M slots"` | a verbatim quote of this file's prose | **falsified in letter** — the line now reads *"a bounded floor of the reservoir slots"*. The fact it rests on (the reserve is a SUBSET of the reservoir, clamped to `reservoir_size`) is unchanged and still stated at the member. Finding for the `insight-eidos` lane |
| `insight-eidos/insight-e2e/test_contract.md:145` and `:449` citing `insight-metalog/api/metalog.api.cppm:756` | a LINE-NUMBER coordinate | **dead, and it was already dead before this unit** — line 756 at the previous revision was `CubeCardinalityStat::cells`, not the cube border the sentence claims. It now lands in `TailSummary`. Finding for the lane that owns `test_contract.md` |
| `DN-43` quoting *"distinct log levels observed (the cube's level axis)"*, `DN-59` quoting *"chain: WHERE …"*, `DN-2` quoting the `SRC-D-WHERE-2` statement | verbatim quotes of trailing comments | **stale in letter, intact in substance** — each quoted fact is now a tagged line at the same member, and each of those notes already carries the `F-SRC-` address. Findings for the design-note owner |
| `DN-63` attributing *"a streaming consumer MUST NOT alert on it"* to `metalog.api.cppm:TailDelta` | a scoped `F-SRC-` address | the quoted sentence is `ReservoirDelta`'s and always was; the scope in the address is wrong. Still stated, at `ReservoirDelta`. Finding for the design-note owner |
| `insight-metalog/tests/operations/test_compose_algebra.cpp:1236` | *"`retention_profile_name` (metalog.api.cppm) derives the §2.4 stamp from exactly four axes"* | **not falsified** — the four-axis derivation and the injectivity are both tagged lines now. The basename gains its address when `tests/operations/` converts |
| `DN-74` quoting *"DERIVED from …"* | the `retention_profile_name` prose | **not falsified** — the phrase survives verbatim in the `post:` |

**Findings for other lanes, each with its addressee.**

**A. A composed document carries NO acquisition block, and a consumer reads that block to decide
WHERE admissibility — for Daidalos, with Eqya on whether the gap is a claim.**
`src/operations/compose.cpp` builds its output from a default-constructed `MetaLogDocument` and
never mentions `acquisition`, so `MetaLogDocument::acquisition` is disengaged on every composed
document. `insight-eidos/sift/src/engine/diff_engine.cpp` reads exactly that block to answer *"is
`component` an admissible WHERE dimension for this window?"* — *"read off the window's own
self-assessment and NEVER synthesized here"*. Whether a composed document should carry a merged
self-assessment, or whether the consumer's absent-block path is the correct posture, is a design
question with two repos in it. The comment now states the truth rather than *"Always present."*

**B. The W1 octave thresholds are described as pre-registered and nothing records a
pre-registration — for Eqya as claim-boundary owner, with Daidalos.** The deleted prose asserted
*"The θ_k are pre-registered (anti-endogamy)"* and, four lines below, *"FROZEN from scenario-35's
measured numerators"* with *"(measured pole 9.96)"* and *"(pole 0.14)"*. A cold reader that went
looking found `STU-3.A1` holding the bands fixed as inputs while pre-registering a different
constant, and `DN-11` quoting them as a derivation input. Anti-endogamy is a claim about ORDER —
the constant fixed before the data — and the tree carries no record of that order for these three
numbers. Either the record exists somewhere this search did not reach, or the anti-endogamy
framing is unearned; both are decisions, not comments.

**C. `ADR-9.D2` states the cube-key allocation is UNFIXED, and the fix has shipped — for
Daidalos.** That slot reads *"`cube_base_`'s keys are plain `std::string` on `std::allocator` —
general heap, two constructions per event … Fixing it is measure-first work with its own gate,
deliberately not done here"*. `TransparentCubeKeyLess` is live on `cube_base_`
(`api/metalog.cppm`), `bench_cube_key_alloc.cpp` and `bench_ordinal_key_alloc.cpp` exist, and the
cold reader read the measured before/after out of `DONE.md` — the ordinal key going 1 -> 0
allocations per event on the ship leg. The slot's own trap paragraph about SSO is still exactly
right and worth keeping; the *"deliberately not done here"* clause is what has been overtaken.

**D. Nine `insight-eidos` sites cite this file by BASENAME rather than by address — for the
`insight-eidos` lane.** *"see metalog.api.cppm (TemplateRegistry) for the contract"* appears in
`sift.cppm`, `sift.api-config.cppm`, `diff_engine.cpp`, `reservoir.cpp`,
`playground.contract.cppm`, `playground.replay.cppm`, `raw_log_fidelity_test.cpp`,
`rule_backend_test.cpp` and `bench_pyramid.cpp`. Nothing is falsified — the contract is still
stated here, as tagged lines — but a basename is not an address, and
`F-SRC-insight-metalog:metalog.api.cppm:TemplateRegistry` is what survives the next rename.

**E. Two LINE-NUMBER citations into this file are dead, and one was dead before this unit — for
the lane that owns `insight-eidos/insight-e2e/test_contract.md`.** Lines 145 and 449 both cite
`insight-metalog/api/metalog.api.cppm:756` for the cube border. At the pre-conversion revision line
756 was `CubeCardinalityStat::cells`, not the border, so the coordinate was already wrong; it now
lands in `TailSummary`. `registry_grammar_lint`'s `G15` bans a `file:line` coordinate and reports 0
live sites — that arm walks the doc tier it knows, and this file is not on it, which is the gate's
blind spot rather than the citation's excuse.

**F. One `insight-eidos` measurement tool quotes this file's prose verbatim — for the
`insight-eidos` lane.** `engine/tools/incident_episode_measure.cpp:1737` reads
`metalog.api.cppm: "a bounded floor of the M slots"`. The line now says *"a bounded floor of the
reservoir slots"*; the fact the tool rests on (the reserve is a SUBSET of the reservoir, clamped to
`reservoir_size`) is unchanged and still stated at the member.

**G. Three design notes quote deleted trailing comments — for the design-note owner.** `DN-43`
quotes *"distinct log levels observed (the cube's level axis)"*, `DN-59` quotes *"chain: WHERE
prefix-path"*, and `DN-2` quotes the `SRC-D-WHERE-2` statement. Each fact survives as a tagged line
at the same member and each note already carries the `F-SRC-` address, so nothing is lost — the
quotes are stale in letter. Separately, `DN-63` attributes *"a streaming consumer MUST NOT alert on
it"* to this file's `TailDelta`; that sentence is `ReservoirDelta`'s and always was, so the scope
in the address is wrong.

---

## Unit 16 — `tests/reservoir/` (2 files, 244 would-be violations)

The first test-tier unit, and both files in it are about one subject — what the salience reservoir
retains and why — so one reader answers for both (`OPS-8.S2`: split where a reader can answer from
a subset, keep together where it cannot). `test_reservoir.cpp` 814 lines and
`test_retention_axis_census.cpp` 253, 267 comment lines between them.

**Census (`OPS-8.S4`).** Derived the same way as unit 15's. `test_reservoir.cpp`: **13**
`/*name=*/` argument comments and **3** namespace closers; `test_retention_axis_census.cpp`: **6**
and **1**. Zero `NOLINT` of any spelling, zero `clang-format off`, zero `wall-clock:`, zero
`DETERMINISM-ALLOW`, zero `LOG-SEAT-ALLOW`, zero `SPDX-License-Identifier:` in either file. After
the strip: identical, 13/3 and 6/1, and the gate's `tool=23` equals the two kept counts (16 + 7).
No census decision was needed. **The `/*name=*/` forms matter here in a way they did not in the
source tier**: they are what `bugprone-argument-comment` reads against the parameter name, and
this file uses them on every boolean and count literal it passes — `/*top_k=*/16`,
`/*reservoir_size=*/0`, `/*error_reserve=*/1`.

**Stripper cross-check (`OPS-8.S5`).** No suppression in either file, so
`removed == violations`: `test_reservoir.cpp` 144 == 144, `test_retention_axis_census.cpp`
100 == 100.

**The leading-blank-line repair `OPS-8.S5` records was owed on BOTH files** — each opened with a
header comment block, so each came out of the strip starting on an empty line, invisible to
`malf format --check` (0 misformatted) and to the comment-only witness (whitespace is dropped by
construction). Removed by hand before the copy.

**Forms written.** 33 insertions. `test_reservoir.cpp`: 64 comment lines, `pre` 1 · `post` 4 ·
`invariant` 9 · `assert` 9 · `refs` 9 · 16 continuations · 16 tool forms.
`test_retention_axis_census.cpp`: 36 comment lines, `post` 1 · `invariant` 11 · `assert` 2 ·
`refs` 3 · 12 continuations · 7 tool forms. Comment lines 267 → 100, would-be violations 244 → 0, and the repo's own gate moved
2 629 → 2 385 — 244 exactly.

**The test tier converts differently, and the ratio says so.** In unit 15, 335 of 434 tagged lines
were `invariant:` and there were two `assert:` in the whole file. Here `assert:` is 11 of 49, because what
a test's deleted prose actually carried is an **isolation argument** — *"the echoed run is
ingested FIRST and self-loops, so novelty and structural surprise are both 0 and the failure-cue
tier is the only axis under test"* — which is a local execution assumption at a point in a body,
and that is exactly `assert:`. The rest went nowhere: `ADR-26.D6` makes the test NAME the claim,
and every arm here is already named as a sentence about its subject
(`ErrorClassReserveIsExemptFromPerKindCap`, `NoveltyAdmitsLateEmergingBenignTemplate`), so the
paragraph restating the name was a mirror.

**`refs:` targets used**: `SRC-D-TIR-5` ×2, `SRC-D-PROV-1` ×2, `SRC-D-RNK-2` ×2, `DN-64.D6` ×3,
`ADR-31.D8`, `DN-56.D2`, `DN-64.D3`.

**Address census.** Outbound: **exit 0, nothing lost**, four addresses added (`ADR-31.D8` and
`DN-56.D2` in `test_reservoir.cpp`, `DN-64.D3` and `DN-64.D6` in the census file). Inbound: the
committed instrument reports **clean** from inside the repo, and the workspace-wide re-run returns
only generated `build-*/…_tests.cmake` entries — ctest's `DEF_SOURCE_LINE` properties, regenerated
by every build, which are line coordinates into these files but not citations anybody maintains.
No inbound citation to falsify.

**A rider on verdict item 14, measured on this unit.** The workspace-wide inbound sweep the
committed instrument's `roots=["."]` makes necessary has a cost of its own: run from the workspace
root it walks `build-*/` and returns ctest's generated `DEF_SOURCE_LINE` properties — 30-odd hits
into these two files, none of them a citation anybody maintains. The repo-root default avoids that
noise and misses the siblings; the sweep finds the siblings and eats the build trees. Both are
correct behaviours of the wrong scope, and what the instrument owes is a `--roots` argument, not a
different default.

**Interrogation — one cold reader over twelve questions, spawned against a frozen tree and finished
before anything was touched.** 69 tool uses, 200 k tokens, 8.6 minutes; the final message carries
`GIT COMMANDS RUN: none` and no `git` invocation appears in the transcript. Same exclusion list as
unit 15's readers.

**Score: 12 of 12 recovered, 0 not recovered, 0 wrong.** No line this conversion wrote was
contradicted. Scored from the per-question evidence.

What the reader recovered that the deleted prose only gestured at is worth recording, because it is
the argument for deleting it. On the two control arms it re-derived the discriminator itself: an
absence has two candidate causes — no retention path, or a broken fixture — and *"one arm cannot do
both jobs, because only the pair discriminates between them"*. On the reserve's window shape it
recomputed the design from the code: `beta verify token` emits 200 + 9 = 209 outgoing transitions
and each branch edge is 3, so p ≈ 1.4 % is inside the strong-off-path band at 90, the lone Error is
80 × 90 = 7 200 against each branch's 8 100 — *"exactly the `EXPECT_LT(error_salience,
min_branch_salience)` the arm rests on"* — and it then answered the counterfactual the prose never
raised, that at 30 recurrences the edge is 10.3 %, the branches fall to a lower band and the arm
would stop isolating the reserve at all.

**One precision fix, caught by the lane and not by the reader.** The `build_high_card_window`
`invariant:` first read *"about 1.4 % of the window"*, which names the wrong denominator: the
figure is 3 of the 209 transitions **out of the branches' shared predecessor**, which is what the
strong-off-path band is computed over, not a share of the window's 637 events. The reader used the
line and derived the correct denominator itself, so it never became a conviction; the line now says
what it meant.

**Stale or unsourced prose deleted, with the evidence.** Nothing in either file was measured false.
The one class worth naming is **history**: `test_reservoir.cpp`'s `SRC-D-RNK-2` block carried the
narrative of the measured loss that motivated the error-class reserve, and
`test_retention_axis_census.cpp`'s header carried a falsifiability record of two applied-and-reverted
mutations. Both are history (`H`) under the claim classes and go; both are also findings, below,
because of where their records live.

**Witnesses.** Comment-only: both files' code token streams byte-identical to `HEAD`. Grammar:
`malf format --check tests/reservoir` reports **0 would-be violations** over the unit. Behaviour:
`malf test insight-metalog` **297 of 297 on clang-21 and 297 of 297 on gcc-16**, taken on its own
slot acquisition and covering unit 16 alone — and the run is not vacuous, each toolchain having
rebuilt exactly the two converted translation units (2 `Building CXX object` lines apiece, both
naming these files). Address census: exit 0 outbound, clean inbound. Slot wait for this witness:
**925 seconds**, against **161** for unit 15's and **0** for the session's first — 1 086 seconds in
total across three acquisitions, with four CCC lanes sharing the one global slot.

**Findings for other lanes, with their addressees.**

**H. The measurement that justifies a SHIPPED retention policy lives in the disposable attic — for
Eqya as claim-boundary owner, with Daidalos.** `MetaLogConfig::reservoir_error_reserve` exists
because a real `testTimeout (FAILED)` was evicted from both `top_k` and the reservoir in a live CI
window of 3 508 templates. The cold reader traced that number to
`technical_docs/history/architecture-v1/detection_provenance_and_legibility.md` § 5.2, and noted
without prompting that `CLAUDE.md` declares `technical_docs/history/` disposable. The only live
source witness is a comment in **another repo** — `insight-eidos/sift/api/sift.api-config.cppm` —
which names the same case at its forwarding field. A retention policy that ships in the product has
its justifying measurement recorded nowhere that is owed to survive.

**I. The falsifiability record for the retention-axis gate has the same problem — for Kleio, with
Eqya.** The two mutation controls that show `RetentionAxisCensus`'s arms are not vacuously green —
dropping `engine.cpp`'s axis stamp reds the `close_window` arm alone, dropping `compose.cpp`'s reds
the `compose` arm alone, which is precisely the two-producer partition the arming condition exists
for — are recorded only in `technical_docs/history/1.10.3.md`. The reader found them and flagged
the provenance as best-effort in the same breath. What survives the attic is the arms themselves and
their assertion messages; the evidence that they can go red does not.

---

## Unit 17 — `tests/cube/` (2 files, 218 would-be violations)

The first unit of the test tier's cube subject and the resume point the previous session prepared:
`test_cube.cpp` (170 would-be violations, 967 lines) and `test_cube_emerging_border.cpp` (48, 253
lines). One subject, one reader. Ordered here rather than after `tests/operations/` because the
cube's source unit (units 3 and 4) is already converted, so every rule these tests witness has
either a converted `invariant:` at its site or a design-note slot to cite.

**The three leading-block `SRC-` files were read FIRST, because the plan was blocked on them**
(`OPS-8.O5`). `tests/engine/test_ordinal_histograms.cpp` (line 1, `SRC-D-W1-2`),
`tests/engine/test_span_edges.cpp` (line 2, `SRC-D-OTEL-11`) and `tests/stats/test_stats.cpp`
(line 17, `SRC-D-TIR-2`) are **all three CITING sites, none statement-bearing**, so neither
`tests/engine/` nor `tests/stats/` owes a law number and both are unblocked. The measurement, run
with `registry_grammar_lint`'s own decider (`SRC_SWEEP` + `SRC_CITE`, declaration classed by
position) over every sibling repo: `SRC-D-W1-2`'s statement is `insight-canon`'s
`core/api/canon.api.cppm` (the per-schedule wire identity, the frozen versioned ladder and its bin
count B, three declaring-position sites); `SRC-D-OTEL-11`'s is the same file (a SPAN record's
declared causality resolved into the n-gram graph at window close, four declaring-position sites);
`SRC-D-TIR-2` has declaring-position sites in `insight-eidos` (`sift/src/sift.detail-shared.cppm`
and three `*.test.cppm` fixtures) and its full statement in the disposable attic
(`technical_docs/history/architecture-v1/insight_perf_template_id.md` § 3). In all three metalog
test files the prose beside the code describes **the test**, not the code's rule, which is the
`OPS-8.O5` criterion. Each becomes `refs: SRC-<code>` unchanged when its unit converts.

**Census (`OPS-8.S4`), re-derived from the gates this repo runs** rather than read off unit 15's
entry — the gate inventory in `insight-metalog/scripts/` and the superproject's `scripts/` was
re-enumerated and is unchanged. Counted in the two files: `NOLINT` (every spelling) 0,
`clang-format off/on` 0, `wall-clock:` 0, `DETERMINISM-ALLOW` 0, `LOG-SEAT-ALLOW` 0, the
retired-structure marker 0, `SPDX-License-Identifier:` 0; `/*name=*/` **1** and namespace closers
**4** (three in `test_cube.cpp`, one in the border file). After the strip: identical, 1 and 4. No
census decision was needed.

**Stripper cross-check (`OPS-8.S5`).** Neither file carries a suppression of any kind, so
`removed == violations - (suppression-without-why + trailing-nolint)` reduces to
`removed == violations`: `test_cube.cpp` **170 == 170**, `test_cube_emerging_border.cpp`
**48 == 48**. Kept 4 and 1, equal to the token census exactly. Both files also carried the
leading-blank-line defect the previous session recorded — removing a file's leading comment block
leaves an empty first line that no witness sees — and both were trimmed by hand in the draft.

**The claims ledger.** 96 comment blocks over the two files. M and H dominate this unit: the
`CubeCollapse` and `CubeDiff` suites carry assertion messages that already state what the prose
above the test restated, and an assertion message is code, so it survives the strip.

| id | class | the claim, as the deleted comment stated it | disposition |
|---|---|---|---|
| M1 | M | `test_cube.cpp` covers "closure/condensation, the order-convex border, compose re-closure, the reservoir→cell LOCATION cross, the single-parent-tree guard, and a byte-identity golden" | deleted — the TEST names are the enumeration, and the last item is **stale**, see the findings |
| C1 | C | `ev()`'s component/template are string literals, so the returned event's `string_view`s stay valid | **`// pre:`** at `ev` — the caller's storage outlives the event |
| M2 | M | `find_cell` finds a closed cell by optional level + where-leaf + role | deleted — the four parameters are the statement |
| X1 | X | the collapse oracle must exercise a real collapse for the rebuild-equality assertion to witness anything (`ADR-31.D8`) | **`// refs: ADR-31.D8`** at `GuardrailBoundsAnExplodingWindowByLevelBanding` |
| R1 | R | 1500 components × two bandable levels is what pushes the un-collapsed cube past the budget | held → Q2 |
| R2 | R | `comps` is `static` so the component `string_view`s stay valid | held → Q1 |
| M3 | M | "the guardrail's CONTRACT: every window's cube is bounded by the budget" | deleted — `EXPECT_LE(cell_count, kCellsHard)` and its message say it |
| R3 | R | the collapse must be RECORDED in the axes so mismatched-collapse cubes are detectable | held → Q3 |
| X2 | X | compare-at-min: an unequal-axes pair is read at the minimal common collapse, never refused (`DN-42.D17`) | **`// refs: DN-42.D17`** at `CompareAtMinDiffsAcrossDifferentCollapseDepths` |
| R4 | R | without compare-at-min the diff would VANISH on axis mismatch, losing attribution at the collapse transition | held → Q4 |
| X3 | X | the severity fixture's rebuild-equality assertion is the same `ADR-31.D8` witness | **`// refs: ADR-31.D8`** at `SeverityFrontierNeverCrossedWhereCollapsesInstead` |
| R5 | R | 2000 components × six levels ≫ the budget even at the maximal band, so WHERE must drop | held → Q5 |
| M4 | M | "LEVEL banding climbed to the frontier ceiling and stopped"; "ERROR and FATAL are DISTINCT cells" | deleted — four assertion messages state both |
| R6 | R | closure is lossless and always applied; collapse is lossy and applied only over budget | held → Q6 |
| R7 | R | the cardinality monitor is the PURE compute; the eidos pipeline emits the WARN | held → Q7 |
| H1 | H | "the pre-collapse WARN predicates were RETIRED" | deleted — history |
| R8 | R | the window total lives in the closure of coord `{}`; closure pins the constant dims and stars the varying ones | held → Q8 |
| R9 | R | a where-pinned cell carrying the same count as its parent is redundant and is not stored | held → Q9 |
| C2 | C | `two_windows()` builds a pair under a shared processing contract so `diff()` does not trip the comparability gate | **`// post:`** at `two_windows` |
| X4 | X | the registry resolves template strings at serialise (`SRC-D-TIR-5`) | **`// refs: SRC-D-TIR-5`** above the `if (out_registry != nullptr)` guard |
| R10 | R | the upper border is the headline — the minimal generators that characterise everything that emerged | held → Q11 |
| R11 | R | a document with `has_cube == false` can only come from a composed axis-mismatch, which is why the tests set it by hand | held → Q12 |
| R12 | R | the reservoir→cell cross is a LOCATION firewall: no salience and no role leak into it | held → Q13 |
| R13 | R | the cube's cross-machine byte identity is proved by the golden workflow over the committed corpus, NOT by an in-test frozen hash | **`// note:` + `// refs: F-SRC-insight-metalog:golden.yaml`** at `CubeDeterminism` — judged non-recoverable and written; Q14 tests the line |
| R14 | R | the cube's three dims are per-line-pure and each count is a plain sum, so the closed cube is permutation-invariant | held → Q15 |
| H2 | H | the playground copies of the order-independence property could retire once it was asserted here | deleted — history |
| X5 | X | the up→regression / down→recovery reading is proved eidos-side | **`// refs: F-SRC-insight-eidos:ordinal_drift_test.cpp`** at `DriftUpEmergesUpShiftCell`; the prose's path was **stale**, see the findings |
| R15 | R | the homing call: this is metalog integration (engine → diff → cube_diff), not a LogCraft e2e | held → Q23 (the border file carries the same claim) |
| R16 | R | determinism: a seed-free integer ladder, single worker, no wall clock, no float→int | held → Q15/Q16 |
| M5 | M | `kMsToNs` exists because the `DurationLog2Ns` schedule reads nanoseconds | deleted — canon's `OrdinalSchedule` states the unit at its declaration; held → Q16 |
| R17 | R | 100 ms is bin 26, 100 s is bin 36, so the move is ten octaves and lands in the HIGH bucket | held → Q17 |
| M6 | M | `kShiftSampleFloor = 32`, so 40 events clear it | deleted — a mirror of `diff.cpp`'s constant; the rule is held → Q21 |
| R18 | R | `max_param_histograms > 0` is what enables the ordinal (DurationLog2Ns) histograms | held → Q18 |
| R19 | R | the STABLE `auth` component keeps payments' WHERE cell from collapsing as redundant, so the shifted coord is pinned rather than the aggregate | held → Q19 |
| R20 | R | the shift only ever pins on the CURRENT side, so a real match lands in emerging; vanishing is scanned so the control can prove NONE | held → Q20 |
| R21 | R | ordinal_w1's thresholds are scale-relative, so without the floor an 8-vs-8 pairing would manufacture `up_high` | held → Q21 |
| H3 | H | the `b60ec47` regression the polarity-MUTE arm guards | deleted — history; the assertion message keeps "REGRESSION GUARD … polarity-MUTE, not up-clipped" |
| R22 | R | the border file's homing argument: its path is canon-tokenize → metalog document → `meta::diff()` → cube border, touching no LogCraft generator, transport or replay | held → Q23 |
| R23 | R | canon ships NO default composition; every BINARY, never a library, declares its semantic package set | held → Q24 |
| R24 | R | under an EMPTY package set the multi-generator class recovers 1 of 2 declared members (recall 0.5) — `role: Terminator` never emerges, because the `##[error]` row is the github package's | held → Q25 |
| R25 | R | the recall floor and mis-point ceiling are PRE-REGISTERED, carried over verbatim from the retired fixture | held → Q26 |
| C3 | C | a `DeclaredCell` dimension left empty is a wildcard | **`// invariant:`** at `struct DeclaredCell` |
| C4 | C | `composed` must outlive the `Tokenizer` it feeds | **`// pre:`** at `build_doc` |
| M7 | M | presence is a bool plus an inline value, not an optional, because of the MSVC consumer-synthesis miscompile | deleted — `api/metalog.api.cppm` carries the same claim as two `invariant:` lines at `has_cube` and `has_emerging`; held → Q28 |
| H4 | H | the cube is always built "since 1.7.2"; the fixture was re-homed from the playground on 2026-07-18 | deleted — history |

**Interrogation** — one fresh agent, 29 questions, 113 tool uses, 224 k tokens, 13.9 minutes.
Transcript checked: `GIT COMMANDS RUN: none`, and no `git` invocation appears in its tool calls.
**The reader disclosed one contamination itself and it is recorded rather than hidden:** an early
`grep -rn` over `insight-metalog/technical_docs/` printed matching LINES from this ledger — an
excluded file — and one of them named Q7's answer. It did not open the file and re-derived Q7 from
`api/metalog.cppm` and `insight-eidos`'s pipeline, but **Q7's independence is compromised and is
reported as such**. A recursive grep reaches an excluded file's contents without opening it, which
the interrogation prompt's exclusion list cannot prevent — a finding for `OPS-8` below.

**Score: 29 of 29 recovered, 0 not recovered, 0 wrong — and 0 convictions.** Scored from the
per-question evidence, not from the reader's own framing: every answer was read back against the
code. Twenty-five answers came at high confidence, three at medium (Q1 *"the tree states no reason
for the `static`"*, Q2's *"1500 exactly"* against a computed threshold of ~1365, Q19 *"the test file
itself does not say"* — recovered from a sibling fixture in this repo), and Q7 is the contaminated
one.

**Every line THIS conversion wrote was put to the reader, and none was faulted.** Q1 read the
`pre:` at `ev` and confirmed the lifetime obligation it states; Q14 read the `note:` + `refs:` pair
at `CubeDeterminism` and independently verified both halves against
`.github/workflows/golden.yaml` and `scripts/determinism_sections.txt`; Q27 read the `invariant:`
at `DeclaredCell` and confirmed it against `coord_of` and `score_border`; Q4 confirmed the
`DN-42.D17` citation, Q22 confirmed that the reading half lives in `insight-eidos/sift`, which is
where the repaired `F-SRC-` address points.

**What the reader found that the conversion had not.** Four answers went past the question and
each is verified below before being filed — none was taken on the reader's word.

| Q | what it answered | verdict |
|---|---|---|
| Q1 | the `static` on `comps` buys **nothing**: the engine copies the component out of the view (`engine.cpp`, `std::string{event.component}`) and a block-scoped vector already outlives every event built from it; removing `static` only rebuilds 1500 strings per entry | recovered — and the deleted prose's stated reason was **wrong**, see the findings |
| Q4 | compare-at-min never refuses — and **two `invariant:` lines in the already-converted `api/` unit still assert an axes-equality gate that does not exist** | recovered — the two lines are verified false and repaired in a separate commit, see the findings |
| Q9 | the dropped cells are the where-**starred** generalizations; the where-**pinned** base cell is the one that survives, so the assertion message beside it is inverted | recovered — verified against `close_and_emit`'s `agg.closure == cell` and the file's own next assertion; a finding, not a repair |
| Q24 | `ADR-17.D2` rules **one composition point per PRODUCT LINE**, owned by the lowest package that tokenizes, and names a per-binary `compose({…})` list a *drift hazard, refused* — the opposite of what the deleted prose asserted | recovered — verified at the slot; a finding for Daidalos |
| Q29 | the minimal-generator loop establishes **emergence, not minimality**, and its dimension bound is a tautology over three `bool`-to-`int` terms that also ignores `latency_shift`, `CubeCoord`'s fourth key | recovered — confirms and sharpens the finding the conversion had already opened |

**Dispositions.** No claim was *not recovered*, so nothing was re-homed above the comment rung.
Two repairs were made in the tree before the commit, both comment-only and both re-gated
(`OPS-8.S7` steps 2 and 3 re-run, `wc -c` clean at 100 bytes):

1. **`F-SRC-metalog-spec:SPEC.md` added at the three collapse tests.** Chasing Q5's answer settled
   a question this unit had opened as a possible law-block candidate: the severity frontier
   (*"`{ERROR, FATAL}` are never banded"*), closure-first/collapse-last, the static 4096 budget and
   the minimal-common-collapse read are **all four stated verbatim in the specification's §16.10**,
   which is an addressable owner reachable by a form-3 address this repo already uses. So **no law
   number is owed here** — the `OPS-8.S9` test refuses a block wherever a slot already owns the
   argument, and this is that case. The three tests now cite it.
2. **The `post:` at `two_windows` was sharpened.** It read *"…under one processing contract, so
   `diff()` judges them comparable"*, which the reader's Q10 shows invites a wrong inference: both
   windows come from ONE engine under ONE config, so the two explicit assignments are documentation
   and the comparability gate could not trip either way. The line now states the pair's shape and
   its distinguishing burst instead of a causal claim it does not need. Not scored as a conviction —
   the reader did not contradict the line, it contradicted an inference the line permitted — and
   recorded here because the distinction is the ledger's, not the converter's, to blur.

**Forms written (10 insertions, 18 comment lines after, from 227 before).** `pre:` 2 · `post:` 1
(one untagged continuation) · `invariant:` 1 · `note:` 1 · `refs:` 7 · tool forms kept 5
(1 `/*name=*/`, 4 namespace closers). `refs:` targets used: `ADR-31.D8` twice,
`F-SRC-metalog-spec:SPEC.md` three times, `DN-42.D17`, `SRC-D-TIR-5`,
`F-SRC-insight-metalog:golden.yaml`, `F-SRC-insight-eidos:ordinal_drift_test.cpp`.

**Address census (`OPS-8.S7.3b`), outbound and inbound.** Outbound: `test_cube.cpp` **added**
`DN-42.D17`, `F-SRC-metalog-spec:SPEC.md`, `F-SRC-insight-metalog:golden.yaml` and
`F-SRC-insight-eidos:ordinal_drift_test.cpp`, and lost none — its pre-unit distinct set was
`{ADR-31.D8, SRC-D-TIR-5}` and both survive as `refs:` lines. `test_cube_emerging_border.cpp`
carried zero addresses before and after. Exit 0 on both files.
Inbound: **5 mentions, 4 benign and 1 real.** Three are the metalog-conformance design note's, and
every one names a **code** symbol or an assertion's exact text (`has_latency_shift_axis`,
`EXPECT_EQ(c.axes[0].kind, "categorical")`), all of which survive the strip. One is in the
disposable attic. The fifth is finding **D** below.

**Stale claims deleted, with the evidence and the sibling repos searched BY NAME.** Every
condemnation below was searched outside this repo before it was filed, in `insight-canon`,
`insight-eidos`, `metalog-spec`, `logcraft` and the superproject's `technical_docs/` shelf.

1. **The file header claimed `test_cube.cpp` ends in "a byte-identity golden". It does not, and
   the file's own body said so.** No test in the file asserts a frozen hash: the suites are
   `CubeBlock`, `CubeCollapse`, `CubeCardinality`, `CubeDiff`, `CubeCompose`, `CubeReservoirCross`,
   `CubeMustOne`, `CubeDeterminism` and `CubeDiffLatencyShift`, and the block at the old line 614
   said in terms that *"the cube's cross-machine BYTE-IDENTITY proof is a cut/gate-time cross-leg
   assertion … NOT an in-test frozen hash"*. The same header also omitted two whole sections that
   ARE in the file — order-independence, and the six-test `latency_shift` suite — which is the
   shape of an enumeration that stopped being maintained. The nearest live golden is
   `tests/operations/test_golden_vectors.cpp`, a different file. Deleted; the true half survives as
   the `note:` + `refs:` at `CubeDeterminism`, which the reader then verified independently.
2. **The eidos pointer's PATH was stale.** The `latency_shift` header named
   `insight-eidos/diff/tests/classify/ordinal_drift_test.cpp`. `insight-eidos/diff/` **does not
   exist** — that repo's packages are `detection`, `engine`, `explain`, `fuzz`, `insight-e2e`,
   `llm`, `sift` and `test_package` — and the file is at
   `insight-eidos/sift/tests/classify/ordinal_drift_test.cpp`; the only `diff/` occurrences are
   stale object files under a build tree. The remedy is `OPS-8`'s: an ADDRESS at the citing site,
   so the pointer is now the form-3 `F-SRC-` address, which `registry_grammar_lint` resolves
   unconditionally and a future rename reds. The reader independently placed the reading layer in
   `insight-eidos/sift` (Q22), which confirms the repair points at the right file.
3. **The justification for `static std::vector<std::string> comps` is false.** Two tests declare
   that vector `static` under the trailing comment *"static storage → the component string_views
   stay valid"*. The lifetime it names does not need `static`: a block-scoped vector already
   outlives every event built from it inside the same test body, and the engine copies the
   component out of the view at ingest (`src/engine/engine.cpp`, `std::string{event.component}`)
   into a `std::map` key. What `static` actually buys is skipping the 1500-string rebuild, and
   since gtest enters a test body once per run it buys that only under `--gtest_repeat`. Deleted
   rather than carried; the surviving `pre:` at `ev` states the real obligation (the caller's
   storage outlives every use of the returned event) without attributing it to the `static`.

**A near-miss recorded because the search IS the finding.** The collapse-oracle block cited
*"ADR-31.D8 oracle-coverage"*, and the token `oracle-coverage` appears **nowhere else in the
workspace** while `ADR-31` contains the string *"oracle"* **zero times** — which reads as a false
attribution. It is not one. Read as *"the oracle FOR `ADR-31.D8`"* the sentence is true and
load-bearing: the rebuild-equality assertion witnesses that slot's determinism claim only if a
collapse actually fired, so the fixture's job is to keep the witness non-vacuous. Filed as a
citation KEPT, not as a conviction — `OPS-8.O3`'s mirror lesson applied to a hyphenation.

**Findings for other lanes, with their addressees.**

**A. A vacuous assertion in `CubeDiff.EmergingHeadlineIsMinimalGenerator` — for Kleio.** The loop
over `emerging.upper` asserts
`EXPECT_LE(level.has_value() + where.has_value() + structural_role.has_value(), 3)`. Three `bool`s
promote to `int`, so the sum is in `[0, 3]` and the bound **cannot fail**; the reader added that it
also ignores `latency_shift`, `CubeCoord`'s fourth optional key. The deleted comment claimed the
property the assertion was meant to carry — *"a minimal generator has no emergent parent"* — and
nothing in the file checks it; minimality is enforced only inside `border_of`'s `has_parent` pass,
which the loop does not re-derive. This is a code change, so it is a finding and not a repair: the
arm needs an assertion that starring any pinned dimension leaves the emergent set, or a bound that
can fail.

**B. An inverted assertion message in `CubeBlock.ClosureCollapsesSingleComponent` — for Kleio.**
Its message reads *"a single-component window must collapse (redundant where-pinned cells
dropped)"*. It is backwards. With one component the base tuple is `(Info, auth, None)` and closure
stores exactly the cells that equal their own closure (`close_and_emit`'s `agg.closure == cell`),
which is that fully-pinned cell alone — the file's very next assertion proves it, expecting
`find_cell(doc.cube, "INFO", "auth", "None")` to be non-null. What is dropped is the seven
where-**starred** generalizations. An assertion message is code, so this is a finding; the deleted
prose above the test carried the same inversion and was not carried forward.

**C. Two `invariant:` lines in the already-converted `api/` unit assert an axes-equality gate that
does not exist — repaired by this lane in a separate commit, and recorded here because it is a
CONVERTED line that was wrong.** `api/metalog.api.cppm` stated, at `CubeDiffBlock`,
*"emitted only when BOTH documents carried a cube AND their axes are equal"*, and at
`MetaLogDiff::has_cube_diff`, *"present only when both documents carried a cube and their axes are
equal"*. Both are false, and two other converted sites in the same repo say so: `src/operations/
diff.cpp` carries `assert: the ONE gate is that both carried a cube; there is no axes-equality
gate, since the contract freezes the axis SET, not the collapse stamps` with `refs: DN-42.D17`, and
`src/cube/metalog.detail.cube.cppm` carries `note: unequal axes are the mandated case: the diff
reads the pair's minimal common collapse`. The lesson for the programme is the sharp one: **a
converted `invariant:` is an assertion the conversion signs, the CCC gate cannot check its truth,
and this pair survived a cold reader on its own unit** — it took a reader on a DIFFERENT unit,
asking a question whose answer crossed the file, to catch it.

**D. A cross-repo rationale whose only copy is prose in a repo that has not been converted yet —
for the pilot, addressed to whoever takes `insight-eidos`'s `insight-e2e` unit.** The inbound
census turned up `insight-eidos/insight-e2e/tests/playground.contract.cppm`, whose comment carries
the SAME homing argument this unit deleted, in the same words — *"its path was canon-tokenize →
metalog document → meta::diff() → cube border, touching no LogCraft generator, no transport and no
replay"*. Nothing is lost today. But it is prose in an unconverted repo: the eidos lane will delete
it, and at that moment the argument leaves the workspace with no witness reporting the loss — the
outbound census cannot see it (no address), and the inbound census only fires for the lane
converting the OTHER end. Either site can fix it, and the fix is one `note:` or a doc paragraph
with an address.

**E. The composition rule the deleted prose stated is the opposite of `ADR-17.D2`'s — for
Daidalos.** `test_cube_emerging_border.cpp`'s header asserted *"every BINARY, never a library, owns
its composition"* and defended declaring a MINIMAL one-package set as *"the more isolating
instrument"*. `ADR-17.D2` rules **one composition point per PRODUCT LINE**, owned by the lowest
package that tokenizes, in one TU (`insight::engine::composed_semantics()`), with every downstream
consumer reaching the set through it — and names a per-binary `compose({…})` list a **drift hazard,
refused**. The prose was not carried into any tagged line, so nothing false was signed; the reader
found the slot unaided. What remains is a real question the conversion may not settle: **does
`ADR-17.D2`'s refusal reach a TEST binary?** This one declares `insight::semantic::github` alone
while production composes four packages, and `insight-metalog/CMakeLists.txt` marks the dependency
*test-only*. Either the slot admits a test-binary carve-out or this test is the drift it names.

**F. Two dangling pointers in `insight-eidos`'s e2e coverage surface — for the pilot, addressed to
the eidos lane.** Reaching for Q23, the reader found that
`insight-eidos/insight-e2e/coverage/25_cube_emerging_border_bred.md` and
`insight-eidos/insight-e2e/tests/cube/cube_bred_oracle_test.cpp` both still point at a
`25_…_bred.contract.yaml` and a paired contract test that are **not in the tree**, and observed
that `insight-metalog/tests/cube/test_cube_emerging_border.cpp` is now the only surviving
border-recovery assertion against the shipped engine. Not this lane's repo; recorded with its
addressee.

---

## Unit 18 — `tests/serialization/` (4 files, 252 would-be violations)

`test_egress_encoding_conformance.cpp` (91), `test_transport_declaration_extension.cpp` (83),
`test_serialization.cpp` (59) and `test_run_outcome_field.cpp` (19). One subject — what this
package puts on the wire — so one reader.

**Census (`OPS-8.S4`).** `NOLINT` (every spelling) 0, `clang-format off/on` 0, `wall-clock:` 0,
`DETERMINISM-ALLOW` 0, `LOG-SEAT-ALLOW` 0, the retired-structure marker 0, SPDX 0;
`/*name=*/` **4** (three in the egress file, one in `test_serialization.cpp`) and namespace closers
**6**. After the strip: identical. No census decision was needed.

**Stripper cross-check (`OPS-8.S5`).** No suppression in any of the four, so the equality reduces
to `removed == violations`: **91 == 91**, **83 == 83**, **59 == 59**, **19 == 19**. Kept 4, 1, 4
and 1, equal to the token census per file. All four carried the leading-blank-line defect after the
strip and all four were trimmed in the draft.

**The claims ledger.** 105 comment blocks over the four files. The egress file is the dense one:
its four header sections are an ownership argument, an oracle-independence argument, a homing
argument and a scope statement, and all four have addressable owners in the egress design note.

| id | class | the claim, as the deleted comment stated it | disposition |
|---|---|---|---|
| C1 | C | every byte this writer emits into a declared encoding is legal there — a MUST on the emitting surface, over ALL string inputs, never an upstream precondition | **`// invariant:`** + **`// refs: DN-65.D1, DN-65.D5`** at the egress file head |
| X1 | X | the sibling eidos arm asserting the same property is GREEN and BLIND — it injects into a message body, which never reaches the diff wire | **`// refs:`** carrying the same form-3 address the prose named, scope included, at the same head — preserved verbatim rather than shortened |
| C2 | C | the scanner is independent of the writer by construction — no shared code, table or header | **`// invariant:`** at `ConformanceScanner` |
| C3 | C | its scope is the RFC 8259 grammar plus the §7 control-byte ban, and NOT UTF-8 well-formedness | **`// invariant:`** at the same site |
| X2 | X | re-reading Glaze's output with Glaze is SUT == ORACLE | **`// refs: DN-65.D7`** — added after the interrogation, see the dispositions |
| C4 | C | the depth bound stops a malformed input recursing the test binary off its stack, and reports the overrun as a violation | **`// note:`** at `kMaxDepth` |
| C5 | C | `std::nullopt` means the text is conformant | **`// post:`** at `scan()` |
| C6 | C | the caller owns the `component` storage — `CanonicalEvent::component` is a view | **`// pre:`** at `make_event` |
| R1 | R | all 32 C0 bytes are driven because five have short escapes the writer emits regardless, so a five-byte gate would be green and vacuous | held → Q2 |
| R2 | R | the injection point is a `where` coordinate because the diff wire carries template IDs, not template text | held → Q3 |
| R3 | R | the byte sits mid-string because Glaze's string writer has a vectorised body and a scalar tail that corrupt differently | held → Q4 — **NOT RECOVERED, and deleted rather than re-homed**, see below |
| R4 | R | the two ordinary neighbours keep the WHERE axis at full depth so nothing collapses the tainted label away | held → Q5 — recovered as a different, TRUE role; the stated mechanism is **wrong**, see the stale claims |
| R5 | R | `kMarker` on the wire is the anti-vacuity guard | held → Q6 — recovered from the assertion messages, which are code |
| R6 | R | a gate whose oracle cannot FAIL is not a gate | held → Q7 |
| R7 | R | the legal twin spells the byte as an escape in a raw literal, never as a literal control byte in this source file | held → Q8 |
| R8 | R | both `to_json` overloads share one `kWriteOpts`, so both are driven rather than one standing in for the other | held → Q9 |
| M1 | M | the value-counts test pins key-sorting, byte-stability on repeat and omission on the default path | deleted — three test names and their assertion messages |
| R9 | R | `value_counts` is an `unordered_map`, so emission MUST key-sort or replay bit-identity is lost | **`// note:`** at `ValueCountsEmittedKeySorted` — carried in from an ORPHANED section header in `tests/engine/test_field_histograms.cpp`, see the findings |
| R10 | R | `entropy_bits` must not be emitted: a float, losslessly derivable, and every emitted field is integer-typed | held → Q12, Q13 |
| R11 | R | the overhead bound is cap-derived, and the guard is that `param_histograms` can never make the document unbounded | held → Q14 |
| R12 | R | 96 bytes per value entry is a *generous* per-entry figure | held → Q15 — **NOT RECOVERED**, re-homed as a `note:` stating only what is checkable, see the dispositions |
| X3 | X | §8 clause 4 makes the caps decidable from the document alone and §4.2 makes an absent `branching_size` a positive assertion | **`// refs: F-SRC-metalog-spec:SPEC.md`** at `EveryEmittedCappedBlockDeclaresItsCapAndHonoursIt` |
| C7 | C | the fixture window carries all three capped blocks — a reservoir entry, branching transitions and the always-on cube | **`// post:`** at `window_with_every_capped_block` |
| X4 | X | §4's `dropped_ngram_observations`, whose ABSENCE is normative in a document declaring 0.7.0 or later | **`// refs: F-SRC-metalog-spec:SPEC.md`** at `ACappedWindowWritesTheRefusedObservationCount` |
| R13 | R | the count is hand arithmetic, never a second call into the producer | held → Q19 |
| M2 | M | a section ruler for `FieldHistogramDiffTest` | deleted — **stale**, the suite moved, see the stale claims |
| C8 | C | the transport member is emitted even when nothing was declared, because a conditionally emitted key is indistinguishable from a key this producer cannot emit | **`// invariant:`** + **`// refs: ADR-23.D4, ADR-23.D6`** at the transport file head |
| C9 | C | the helper returns the serialized bytes of one window closed with NOTHING declared | **`// post:`** at `produce_undeclared_document_json` |
| C10 | C | the recorded emission state, whose comparison reds in BOTH directions | **`// invariant:`** at `kTransportMemberIsEmitted` |
| H1 | H | four sites describing a pre-registered RED that was repaired on 2026-09-04 | deleted — **stale**, see the stale claims |
| C11 | C | `RunOutcome::Unknown` is both the in-memory default and the wire ABSENCE, so a verdict-free document is byte-identical to a pre-outcome producer's | **`// invariant:`** + **`// refs:`** to the specification and its schema, at the run-outcome file head |
| R14 | R | the Sift change report spells the same four classes UPPER-CASE, and the two wires are deliberately not aligned | held → Q28 |
| R15 | R | a diff here is a wire-contract break — fix the code, never the assertion | held → Q29 |

**Interrogation** — one fresh agent, 29 questions, 67 tool uses, 160 k tokens, 8.8 minutes.
Transcript checked: `GIT COMMANDS RUN: none`. The exclusion list was strengthened for this reader
after unit 17's disclosure — it now forbids a recursive `grep` whose OUTPUT would print an excluded
file's lines, not only opening one — and no contamination was reported or observed.

**Score: 27 of 29 recovered, 2 not recovered, 0 wrong — and 0 convictions.** Scored from the
per-question evidence. Every one of the twelve lines this conversion wrote that a question touched
was read back and confirmed: Q11 confirmed the scanner's declared scope against the specification's
encoding section, Q21 confirmed the transport head's `invariant:` against `ADR-23.D4`, `compose.cpp`
and the api's own invariants, Q23 confirmed the two-way ratchet at `kTransportMemberIsEmitted` and
added that the constant now stands at `true` so the check behaves as a plain assertion, and Q27
confirmed the run-outcome head against the specification's §2.5 and `wire_format.cpp`. **No line
this conversion wrote was faulted.**

**The two NOT RECOVERED, and their dispositions are different on purpose.**

* **Q4 — why the injected byte sits mid-string. NOT re-homed, deleted, and a finding.** The prose
  gave a mechanism: *"Glaze's string writer has a vectorised body and a scalar tail that corrupt
  differently — the body substitutes NUL bytes for the offending byte, the tail copies it
  verbatim"*. The reader answered at medium confidence with a different, plausible account and said
  plainly *"the file states no reason"*. A workspace-wide sweep over every sibling repo and the
  superproject for that measurement returns **one** hit, in an unrelated `insight-canon` scan test
  about its own loop; the egress design note does not carry it. So it is an **unsourced
  measurement**, `OPS-8.S9`'s last row: re-homing it would be the conversion inventing a fact about
  a third-party library's codegen and signing it. Deleted, with the finding below.
* **Q15 — where the 96-byte per-entry constant comes from. Re-homed, narrowly.** The reader
  established the stronger fact: `kBytesPerValueEntryUpperBound` **occurs exactly once in the whole
  workspace**, with no comment, no `refs:`, and no doc, ADR, spec clause or measurement anywhere
  that derives it. The deleted prose called it *"generous bytes per entry"*, which is a
  quantitative claim this lane cannot check. What IS checkable is what the assertion does with it,
  and that is what the `note:` now says — a cap-derived ceiling, not the measured overhead, which
  the test prints and never asserts. The provenance gap is a finding.

**Two more dispositions, both improvements the reader handed over.** `refs: DN-65.D7` was added at
`ConformanceScanner`: the SUT-versus-oracle argument has its own slot, which the conversion had not
found and the reader cited from the design note in its first answer. And the address census's two
`LOST` lines were re-derived at the artifact before being accepted (below).

**Forms written (14 insertions, 36 comment lines after, from 262 before).** `pre:` 1 · `post:` 3 ·
`invariant:` 6 · `note:` 3 · `refs:` 7 · six untagged continuations · tool forms kept 10
(4 `/*name=*/`, 6 namespace closers).

**Address census (`OPS-8.S7.3b`), outbound and inbound.** Added `DN-65.D7`,
`F-SRC-metalog-spec:SPEC.md` (twice) and `F-SRC-metalog-spec:metalog.v0.schema.json`. **Two `LOST`
lines, both re-derived at the artifact and both legitimate**, so the census exits 1 and the
disposition is recorded here rather than the address restored:

1. **`ADR-23` (bare, no slot) at the transport file.** The prose said *"Two ADR-23 slots meet
   here"* and then named them; the conversion carries `refs: ADR-23.D4, ADR-23.D6`, which the
   census scores as three distinct tokens where the bare number was one. The bare form is strictly
   less addressable than the two slots that replace it.
2. **`ADR-26` (bare, no slot) at the egress file.** The prose read *"(ADR-26 at drain; the argument
   is DN-65.D1)"* — a pointer to a **planned drain destination**, not a current owner. `ADR-26`
   contains zero occurrences of *egress*, *encoding* or *RFC 8259* today, the egress design note is
   still IN FLIGHT and records `ADR-26` as its *disposition target at drain*, and **every other
   converted site in the workspace that obeys this rule cites the DN slot alone** —
   `src/serialization/json_egress.hpp`, `api/metalog.cppm`, and three `json_egress.hpp` files in
   `insight-eidos`. Repointing the citers is the drain's pass (`ADR-6.D9`), not this unit's.

**Inbound census.** Run over all four files; every mention resolves to a code symbol or a document
paragraph that names the file without resting on its prose. Nothing to repair.

**Stale claims deleted, with the evidence and the sibling repos searched BY NAME** — searched in
`insight-canon`, `insight-eidos`, `metalog-spec`, `sift-action`, `logcraft` and the superproject's
`technical_docs/` shelf.

1. **Four sites in `test_transport_declaration_extension.cpp` still described a pre-registered RED
   that was repaired on 2026-09-04.** The file's own `///` block recorded the repair — *"now
   `true`: the producer DOES emit …"* — and `kTransportMemberIsEmitted` stands at `true`; but a
   section ruler still read *"PRE-REGISTERED RED"*, a line above the test still read *"Flips to
   green when `fr.coderoast.transport` is emitted"*, a body block still described the skip that had
   been replaced, and one line stated flatly *"The red still stands, pinned by the assertion
   above"*. The file contradicted itself. Verified independently by the reader, which observed that
   with the constant at `true` the assertion behaves as a plain positive check. The residual
   contract — that the comparison reds in both directions — survives as the `invariant:` at the
   constant.
2. **An orphaned section ruler in `test_serialization.cpp`.** `// ── FieldHistogramDiffTest ──`
   stands at the end of the file with no test under it; the three `FieldHistogramDiffTest` cases
   live in `tests/operations/test_diff_blocks.cpp`. The suite moved and the header stayed.
3. **The two ordinary components in the egress probe are not there for the reason the prose gave.**
   It said they *"keep the WHERE axis at full depth so nothing collapses the tainted label away"*.
   Closure does not work that way: with a single component the base tuple is the only closed cell
   and it is where-PINNED, so the tainted label reaches the wire either way — which unit 17's
   `CubeBlock.ClosureCollapsesSingleComponent` demonstrates directly (`raw_cell_count` 8,
   `cell_count` 1, the pinned cell surviving). The reader found the role that IS load-bearing and
   the prose never stated: the same builder with `include_tainted = false` is the diff arm's
   **baseline**, so the neighbours are the shared content that makes the diff a one-component
   change rather than a comparison between documents with nothing in common.

**Findings for other lanes, with their addressees.**

**A. `kBytesPerValueEntryUpperBound{96}` has no provenance anywhere in the workspace — for Kleio.**
The constant occurs exactly once, and the bound it builds (`top_k_count × 2 × 64 × 96`) evaluates
to about 6.3 MB against a real overhead the test prints and never asserts. Either it was measured
and the measurement is unrecorded, or it is a round number — and the arm's strength depends on
which. The `note:` now says only what is checkable; the number's ground is the finding.

**B. The mid-string injection point rests on an unrecorded claim about Glaze's codegen — for
Kleio.** The deleted prose justified placing the byte between a marker and a literal tail with a
measurement about a vectorised body and a scalar tail corrupting differently. If that was measured,
it is recorded nowhere; if it was reasoned, the placement's extra value over an end-of-string
injection is unproven. The fixture is unchanged either way — this is a request for the measurement
or for the claim to be dropped, not for a code change.

**C. An orphaned rationale in `tests/engine/test_field_histograms.cpp`, carried across the unit
boundary into this one — recorded because the carry is unusual.** That file ends with a
`FieldHistogramSerializationTest` section header whose body states the real reason this unit's
key-sorting test exists: `FieldHistogram::value_counts` is an `unordered_map`, so emission must
key-sort or replay bit-identity is lost. The suite it heads is in THIS directory. The claim was
verified at the artifact (`api/metalog.api.cppm` declares `std::unordered_map<std::string,
std::uint64_t> value_counts`) and written here as a `note:` at
`ValueCountsEmittedKeySorted`; the orphaned header is `tests/engine`'s to delete when that unit
converts.

---

## Unit 19 — `tests/engine/` (9 files, 268 would-be violations)

The engine's own behavioural suites: `test_behavior_block.cpp` (77),
`test_field_histograms.cpp` (45), `test_windowing_seam_over_raw_lines.cpp` (31),
`test_span_edges.cpp` (28), `test_stats_block.cpp` (27), `test_ordinal_histograms.cpp` (26),
`test_hll_cardinality.cpp` (24), `test_stability_block.cpp` (5) and
`test_engine_lifecycle.cpp` (5). Nine files, one subject — what `MetaLogEngine` puts in a closed
document — so one reader.

**Two of the three leading-block `SRC-` files are here, and unit 17 had already read them.**
`test_ordinal_histograms.cpp` (line 1) and `test_span_edges.cpp` (line 2) are CITING sites, not
statement-bearing: the statements of both codes are `insight-canon`'s `core/api/canon.api.cppm`.
Each becomes a one-line `refs:` at the file head, position class unchanged, so
`registry_grammar_lint`'s declaration-by-position classing is unmoved. No law number is owed and
none was minted.

**Census (`OPS-8.S4`).** `NOLINT` (every spelling) 0, `clang-format off/on` 0, `wall-clock:` 0,
`DETERMINISM-ALLOW` 0, `LOG-SEAT-ALLOW` 0, the retired-structure marker 0, SPDX 0; `/*name=*/`
**6** (all in `test_span_edges.cpp`) and namespace closers **9**, one per file. After the strip:
identical. No census decision was needed.

**Stripper cross-check (`OPS-8.S5`).** No suppression in any of the nine, so the equality reduces
to `removed == violations`, and it holds file by file: 77, 45, 31, 28, 27, 26, 24, 5, 5 — each
equal to its own would-be violation count. Kept 7 in `test_span_edges.cpp` (6 `/*name=*/` plus its
closer) and 1 in each of the other eight. All nine carried the leading-blank-line defect after the
strip and all nine were trimmed in the draft.

**The claims ledger.** 78 comment blocks over the nine files. This unit is unusually M-heavy: the
`BehaviorBlockTest` and `SpanEdges` suites carry assertion messages that already state the
arithmetic and the anti-vacuity argument the prose above them restated, and an assertion message is
code.

| id | class | the claim, as the deleted comment stated it | disposition |
|---|---|---|---|
| X1 | X | the W1 ordinal carrier, its ladder and its emit order (`SRC-D-W1-2`) | **`// refs: SRC-D-W1-2`** at the ordinal file head — a CITING site, verified against canon's interface |
| X2 | X | the observed causal DAG: a span's causality is DECLARED, so it never enters an adjacency ring (`SRC-D-OTEL-11`) | **`// refs: SRC-D-OTEL-11`** at the span file head — likewise a citing site |
| X3 | X | template identity is a pure function of the line's own tokens (`SRC-D-TID-3`), which is what retired the evolving-cluster test | **`// refs: SRC-D-TID-3`** at `UniqueTemplateCount`, the test whose claim rests on it |
| C1 | C | the `ordinals` span is valid through the ingest call, which copies what it keeps | **`// pre:`** at `ingest_latency_ms` |
| C2 | C | `tail_summary` is absent when the tail is empty and otherwise carries all three fields; a partial one is never emitted | **`// invariant:`** at `TailSummaryAbsentWhenTailEmpty` |
| X4 | X | the accounting bound and its omit-when-zero rule are the specification's §4 | **`// refs: F-SRC-metalog-spec:SPEC.md`** at `BoundedNgramKeysCapDistinctEntries` |
| X5 | X | the composed drop count is the SUM, absent counting as zero, and commutativity is what the sum gives free | **`// refs: F-SRC-metalog-spec:SPEC.md, DN-56.D6`** at the compose-sum test |
| X6 | X | the HLL estimate's error ceiling | **`// refs: F-SRC-metalog-spec:SPEC.md`** at the first HLL test — the *~1.5 %* figure is the specification's stated ceiling, not this repo's measurement |
| X7 | X | the joint grain canon and metalog were never proven at together | **`// refs: DN-43.O3`** at the windowing-seam file head |
| X8 | X | the two windows are shaped after the determinism corpus, the population the format-strategy gate pre-registers | **`// refs: DN-43.O2, F-SRC-insight-metalog:service_a.log`** at `kWindowOne` |
| X9 | X | the publication half — an absent level is omitted from the wire, while the cube's axis carries its own value | **`// refs: DN-43.D10, F-SRC-metalog-spec:SPEC.md`** at the second windowing test |
| R1 | R | `compose` drops branching because it cannot recompute it from aggregated counts, so it is absent and never present-but-empty | held → Q1 |
| R2 | R | the counter counts OBSERVATIONS, never distinct keys, and no correct distinct-key value exists to assert | held → Q3 |
| R3 | R | the snapshot is taken in `close_window` ahead of `reset_window_state`, so the live counter is already zero at the read | held → Q4 |
| R4 | R | reading the SECOND close is what makes the reset arm an arm rather than a restatement | held → Q5 |
| R5 | R | the two present-side counts are deliberately different, because the only arm a carry-one-side implementation passes is the one where the sides are equal | held → Q7 |
| R6 | R | the round-robin interleave makes every globally adjacent pair cross traces, which is what leaves the global graph with zero real transitions | held → Q8 |
| R7 | R | the third arm is the config flag's A/B on identical input | held → Q9 |
| H1 | H | a TDD structure — *"Test 1 regression guard; Tests 2–5 RED before engine impl, GREEN after"* — and *"this is the state before the feature"* | deleted — history, and **stale**, see below |
| M1 | M | field histograms re-surface the causal structure *"that Drain removes"* | deleted — **stale**, Drain is retired, see below |
| R8 | R | the HLL estimate is accepted in a range because it is an estimator, not an exact count | held → Q12 |
| M2 | M | a `FieldHistogramSerializationTest` section header, at the END of a file that contains no such test | deleted — **orphaned**, its content carried into unit 18, see below |
| R9 | R | a child span serialises before its parent routinely, and close-time resolution handles it | held → Q16 |
| R10 | R | an unresolvable parent becomes an orphan FACT, never a guessed edge | held → Q17 |
| R11 | R | the inferred control run is what makes the observed graph's zero noise a delta rather than a vacuous zero | held → Q18 |

**Interrogation** — one fresh agent, 27 questions, 63 tool uses, 216 k tokens, 10.4 minutes.
Transcript checked: `GIT COMMANDS RUN: none`. **The reader disclosed one contamination and it is
recorded:** a single recursive `grep` for a decision code, run before it adopted the exclusion
glob, printed reference-index lines from the two excluded ledgers; it reports those lines were
codename lists carrying no answer, and every later search carried the exclusion. Kept as a
disclosure rather than a discount, because the questions it could plausibly have touched (the two
`SRC-` citations) were answered from `insight-canon` and `insight-metalog` sources it names.

**Score: 27 of 27 recovered, 0 not recovered, 0 wrong, 0 convictions.** Twenty-four at high
confidence, three at medium (Q6 on why the drop count gets a commutativity arm the sibling caps do
not, Q10 on whether the disabled-by-default test pins a limitation or a guarantee, Q12 on the HLL
range's width). **Every line this conversion wrote that a question touched was read back and
confirmed**: Q19 verified the `tail_summary` `invariant:` against the specification and the type's
own shape; Q21 answered the template-identity question the `SRC-D-TID-3` citation now carries, from
canon's stateless masker and its own test; Q23 and Q24 landed on the two design-note slots the
windowing citations name; and Q25 read the `service_a.log` address and then **verified it line by
line** — `kWindowOne` is that corpus file's lines 1-4 and `kWindowTwo` its lines 25-27, byte-for-byte
apart from a dropped trailing request id.

**Dispositions.** Nothing was not-recovered, so nothing was re-homed and no line was repaired.

**Forms written (11 insertions, 27 comment lines after, from 289 before).** `pre:` 1 ·
`invariant:` 1 · `refs:` 9 · one untagged continuation · tool forms kept 15 (6 `/*name=*/`, 9
namespace closers).

**Address census (`OPS-8.S7.3b`).** Exit 0 on all nine files, no address lost. Three files carried
one address each before and after (`SRC-D-W1-2`, `SRC-D-OTEL-11`, `SRC-D-TID-3` — all three
preserved as `refs:` lines at the same position class); four gained one or two
(`F-SRC-metalog-spec:SPEC.md` three times, `F-SRC-insight-metalog:service_a.log` once); two carried
none either way. Inbound leg run over all nine: every mention resolves to a code symbol or to a
design-note paragraph that names the file without resting on its prose.

**Stale claims deleted, with the evidence and the sibling repos searched BY NAME** — searched in
`insight-canon`, `insight-eidos`, `metalog-spec`, `logcraft` and the superproject's
`technical_docs/` shelf.

1. **`test_field_histograms.cpp` said field histograms re-surface the causal structure "that Drain
   removes" — in the present tense, about a component that no longer exists.** The only occurrence
   of *Drain* anywhere in `insight-canon`'s or this repo's interfaces is a retirement note in
   `insight-canon/core/api/canon.api.cppm` recording that the Drain clustering knobs were removed;
   `test_stats_block.cpp`'s own deleted prose says the `migrate_bucket` path "was deleted with the
   Drain clustering". A claim that was true when written and was falsified by a later change — the
   *world moved* class, not a claim that was ever wrong.
2. **The same file's TDD scaffolding.** *"Tests 2–5 — RED before engine impl, GREEN after"* and
   *"This is the state before the feature"* describe a state three years of commits ago; all five
   tests are green and the feature shipped. History, deleted.
3. **The HLL range claimed an upper bound the code does not assert.** The prose read *"50 distinct
   values → estimate should be within a reasonable range (5..200)"*; the assertion is
   `EXPECT_GE(fh.approximate_cardinality, 5u)` and there is no upper bound anywhere in the test.
   The reader established the same fact independently. The *~1.5 % error* half of that prose is
   NOT stale — it is the specification's own stated ceiling for the sketch's precision — which is
   why the surviving form at that test is a `refs:` to the specification rather than a deletion.
4. **An orphaned section header.** `test_field_histograms.cpp` ends with a
   `FieldHistogramSerializationTest` block whose body states the real reason a *different*
   directory's test exists. Deleted here; the claim it carried was verified at the artifact and
   written into `tests/serialization/test_serialization.cpp` in unit 18.

**Findings for other lanes, with their addressees.**

**A. The dropped-observation fixture cannot separate the two quantities its prose distinguished —
for Kleio.** The deleted comment insisted the counter measures refused OBSERVATIONS and never
refused distinct KEYS. True of the engine — but in this fixture all 20 templates are distinct, so
all 19 bigrams are distinct and the 15 refused observations are also 15 distinct refused keys. The
reader stated it plainly: a fixture that repeats a refused key would be needed to separate them.
The distinction is real and the arm does not witness it.

**B. `TEST(FieldHistogramTest, DisabledByDefault_ParamsDiscarded)`'s assertion message reads as
pinning a limitation, while the assertion pins a guarantee — for Kleio.** The message says the
default config "must produce no field histograms (zero-overhead guarantee)" and then describes the
`status_code` distribution being invisible downstream. The reader had to reach `LEXICON.md`, the
api's default and Sift's own opt-in constant to settle that the opt-in default is a kept guarantee
rather than a tracked gap. An assertion message is code, so this is a finding.

**C. Two threshold pairs in this unit are fixture arithmetic with headroom and nothing derives
them — for Kleio, informational.** `EXPECT_GE(approximate_cardinality, 5u)` for a true cardinality
of 50, and `tail_entropy_bits < 0.4` / `tail_max_rate > 0.32` against computed values of about
0.161 and 0.3267. The reader derived the second pair exactly from the fixture; neither pair has a
stated derivation, and the sibling test one section away pins its own value exactly (`3.0/14.0`),
which is the shape that makes the looseness visible.

---

# The law block: one was owed, one was issued, one is minted

This lane was instructed not to pick a law number — they are workspace-global, append-only and
checked **dense**, so a lane that picks its own collides with a sibling holding the open range
(`OPS-8.O4`). One site in this repo was found to owe a block. It was recorded here as stopped, the
pilot issued the number on 2026-09-06, and unit 8 minted it at
`src/stats/wire_format.cpp`, above `spec_run_outcome_of`. The subject is the two
non-interchangeable wire spellings of `insight::RunOutcome`; unit 8's entry above paraphrases what
the block says and explains why the frame is not reproduced on this shelf.

**The census the pilot supplied, recorded so the next lane does not re-derive it:** `1` and `2` are
declared in `malf/malf`, `3` in `insight-eidos`, `4` in `malf-toolchain`, and the `insight-canon`
lane consumed the next three at sites in its conformance module. This repo took the one after
those. **Updated 2026-09-06 at the close of the second session: the next two have since been
consumed by sibling lanes and the numbering past them is issued elsewhere, so this repo's number is
still the one it minted and no other.** The workspace gate reads the declaration count directly —
`registry_grammar_lint`'s form-1 line — which is the figure to trust over any list. Numbering is dense and append-only, so **a lane asks the pilot and never picks** — and the
next number may already be promised to another lane.

**The test that decided it, and the three sites it refused.** A rule that a second site obeys needs
a block **only when it has no addressable owner**; where an ADR slot, a design-note slot or a bible
already owns the rule, the site carries `refs:` and the rule is addressed without a new
declaration. Measured on this repo:

| site | the rule | owner | verdict |
|---|---|---|---|
| `src/cube/metalog.detail.cube.cppm`, two sites | no axes-equality gate on a cube pair | `DN-42.D17` | `refs:`, no block |
| `src/engine/engine.cpp`, four sites | the transparent-key copy-on-first-sight idiom | `ADR-9.D2` | `refs:`, no block |
| `src/serialization/json_egress.hpp` | one JSON write entry point per package | `DN-65.D2` + `DN-65.O4` | `refs:`, no block |
| `src/stats/wire_format.cpp` | the two wire spellings of `RunOutcome` | **none reachable** | **block minted** |

Only the last has no owner, and the reason is structural: its authority is
`metalog-spec/GOVERNANCE.md` §3, which `.claude/rules/adr-shelf-boundary.md` rules is a **different
owner** whose numbering is not citable from this shelf — so no `refs:` line can reach it. One
block, not four. The pilot ratified the test when issuing the number.

**Nothing else in the converted surface owes a block, and `api/` has since been READ.** The
2026-09-06 session read both `api/` files for statement-bearing codes: `api/metalog.cppm` carries
citations only (unit 11), and `api/metalog.api.cppm` holds the statements of three codes, each of
which has an addressable owner — `SRC-D-TIR-5` is `ADR-16.D3`'s subject, and the two W1 codes resolve
to `STU-3` and to `insight-canon`'s interface. So the last source unit is NOT blocked on a number.
The test tier's three leading-block `SRC-` sites are still unread; that is the test tier's own first
step.

---

# The `SRC-<code>` census of this repo — for the pilot's cross-repo repointing pass (`OPS-8.O5`)

Recorded because `OPS-8.O5` makes the citer list the lane's deliverable and the repointing itself
the pilot's single cross-repo pass. Measured 2026-09-06 over `api/ src/ scripts/ benchmarks/
tests/ test_package/` with `registry_grammar_lint`'s own decider pattern: **26 distinct codes,
169 occurrences.** **Re-derived after units 11-14 of the same date: the same 26 codes, 158
occurrences** — eleven fewer because a prose block that named one code twice becomes a single
`refs:` line. No code was lost, and `registry_grammar_lint` reports 95 claimed codes with 95
declared in source and 0 failures both before and after.

`SRC-D-OTEL-21` 28 · `SRC-D-TIR-5` 20 · `SRC-D-OTEL-11` 15 · `SRC-II-7` 13 · `SRC-D-WHERE-2` 9 ·
`SRC-D-W1-4` 9 · `SRC-D-W1-2` 9 · `SRC-D-PROV-1` 9 · `SRC-D-OTEL-9` 9 · `SRC-D-WHERE-4` 7 ·
`SRC-D-WHERE-5` 5 · `SRC-D-TIR-2` 5 · `SRC-D-RNK-2` 5 · `SRC-D-OTEL-13` 5 · `SRC-D-W1-1` 4 ·
`SRC-D-W1-5` 3 · `SRC-D-OTEL-20` 3 · `SRC-D-TIR-4` 2 · `SRC-D-OTEL-1` 2 · `SRC-D-WHERE-6` 1 ·
`SRC-D-W1-3` 1 · `SRC-D-TID-3` 1 · `SRC-D-TID-16` 1 · `SRC-D-OUT-RUN-1` 1 · `SRC-D-OUT-4` 1 ·
`SRC-D-OTEL-22` 1.

**The declaring-POSITION population is 7 files** (`registry_grammar_lint` classes a declaration by
position — an interface unit, or a `.cpp`'s first 40 lines): `api/metalog.api.cppm` 50
occurrences, `api/metalog.cppm` 23, `src/stats/metalog.detail.stats.cppm` 1,
`scripts/service_edges_overcap_scenario.hpp` 1, and the leading blocks of
`tests/engine/test_ordinal_histograms.cpp` (line 1), `tests/engine/test_span_edges.cpp` (line 2)
and `tests/stats/test_stats.cpp` (line 17). Every other occurrence is a body site past line 40 and
is therefore a **citation**, which `OPS-8.O5` leaves in `refs: SRC-<code>` form untouched.

**The one declaring-position site this run converted was checked, not assumed.**
`src/stats/metalog.detail.stats.cppm`'s `SRC-D-PROV-1` is a *content citation* — its statement is
in `insight-canon` (`core/src/parse/log_parser.cpp` leading block, `core/api/canon.transport.cppm`),
and a workspace design note owns the subject. The conversion carried it into a
`refs:` line **in the same `.cppm`**, so the position class is unchanged and `G5` is unmoved:
`registry_grammar_lint` after the conversion reports **95 claimed codes, 95 declared in source, 0
failures**. `api/metalog.api.cppm` and `api/metalog.cppm` were NOT read for statement-bearing
codes by that run; the 2026-09-06 session read both, and the resume marker at the foot of this file
records what each holds.

---

# The `OPS-8` verdict — read against the runbook as it stood at 00:36 on 2026-09-06

`OPS-8` was **edited by a sibling lane while this run was in flight**, and two of the four findings
this lane had written up were published by `insight-eidos` before this ledger was committed — the
slot-anchor hazard and the parallel-wave slot regime, both now in `OPS-8.S1.1`. They are kept below
only where this lane's measurement adds something the new text does not have. Everything else is
checked against the live file, not against the version this lane read at 23:47.

## 1. CONFIRMED INDEPENDENTLY, AND THE NEW TEXT'S REMEDY IS NARROWER THAN IT NEEDS TO BE

`OPS-8.S1.1` now says: *"ACQUIRE IN THE FOREGROUND, NEVER FROM A DETACHED BACKGROUND POLLER … the
slot reads reclaimable about a second after a successful acquire and the next sibling takes it out
from under a live build."* This lane hit the same hazard independently, before that text existed,
and it **fired**: a background acquire took the slot at 00:16:46, the clang leg ran and exited 0,
and at **00:17:41 — while the gcc leg was running — the sibling `ccc-canon` acquired the same
slot.** Two `malf test` runs then ran unprotected over one editable tree. The lane's own
`malf slot release --token …` was **REFUSED**, which is the token half of the design working
exactly as written: no third party's hold was destroyed, only the aliveness proof failed.

**What this lane adds is a falsifiable pair and a second correct shape.** Reproduced in an isolated
`MALF_BUILD_SLOT_DIR`, same script, one environment variable apart:

| | stamp | `malf slot status` after the script exits |
|---|---|---|
| background acquire, no override | `anchor 3429780` | **STALE — anchor pid 3429780 is GONE** |
| background acquire, `MALF_BUILD_SLOT_ANCHOR=<session pid>` | `anchor 3053` | **HELD — anchor pid 3053 is ALIVE** |

`malf` documents the override at the anchor function itself — *"Override with
MALF_BUILD_SLOT_ANCHOR for anything this cannot see"* — and a detached script is exactly a thing it
cannot see. So the rule is not *never acquire from a script*; it is **never acquire from a script
without pinning the anchor**. This lane's last two behaviour witnesses were taken by a background
script with the anchor pinned, and both printed `HELD … ALIVE` at the pre-release check. That is
worth having, because a foreground-only rule makes a lane in a four-lane wave burn its turns
polling.

**And there is a case for moving this out of the runbook entirely**: the acquire already computes
both halves of the comparison it would need (`_malf_slot_anchor_is_shared`), so it could **warn when
its derived anchor is not the session**. That is Argos's call, not this lane's.

## 2. `OPS-8.S1.5` says the shared scripts "carry no path constants at all" — measured FALSE for half of them

Still live in the current text. `strip_to_v1.py` line 14 and `code_only_diff.py` line 15 each carry
`sys.path.insert(0, "/home/windows/workspace/coderoast/malf")`, the import root for the gate's own
scanner. It is correct in this workspace, so it does not bite today; but it is an absolute path
constant in two of the four scripts the sentence names, and the step tells an operator there is none
to look for. If the workspace ever moves, the failure is an `ImportError` from a script the runbook
has just declared constant-free.

## 3. A claim block can hold TWO forms, and a budget checker that treats it as one reports a false overrun

`OPS-8.S3.3` requires an indent-aware byte budget in the claims script and does not say how a block
that carries more than one form is measured. A block that is a `note:` followed by its
`NOLINTNEXTLINE`, or a `note:` followed by a `refs:`, is **two forms**; a checker that measures the
whole block against the first tag's budget reports *"note needs 2 lines, budget 1"* on shapes the
gate accepts. Three false overruns on this run's first dry run, every one a legal shape. The budget
must be computed **per tagged sub-claim**, with a tool-form line closing the open claim and carrying
no budget of its own.

The step's own requirement is worth the effort, confirming `insight-canon`'s finding 3 from a third
corpus: the indent-aware BYTE check rejected **four claim lines across units 2 and 3** that a flat
100-character check would have passed, every one at indent 4 or 8, two of them within three bytes of
the limit.

## 4. The cold-reader prompt must exclude `ADR-26`, not only the ledgers

`OPS-8.S8` widens the exclusion to the migrating repo's own ledger, which is right and was applied
here. It needs one more file: **`technical_docs/adr/026-code-doctrine.md`**, which prints the
grammar, the migration protocol, and in `ADR-26.D8`/`ADR-26.O3` the worked results of previous
interrogations — a reader that opens it learns what the measurement is for and what a good answer
looks like. Every reader on this run was given `ADR-26` in the exclusion alongside `OPS-8`, any
`ccc_migration.md` in any repo, and this repo's own ledger by name.

## 5. `OPS-8.O5` tells a lane to stop a unit, and no step says what the unit then IS

`OPS-8.O5` sends a declaring site that needs a law block back to the pilot. Neither it nor
`OPS-8.S2` says whether the rest of the directory is then converted. Taken literally, one law site
costs a whole directory. This run declared the unit as **the directory minus the blocked file** —
`OPS-8.S2`'s own file-group split, used for a different reason — so every witness stays whole, the
blocked file is untouched, and nothing is half-converted. Without that reading, 284 convertible
violations in `src/stats/` would have been stranded behind 28 blocked ones.

## 6. THE CONVERSION'S OWN PRE-DELETION ANALYSIS CAN BE WRONG, AND NO STEP COVERS THAT DIRECTION

`OPS-8.O3`'s second lesson is *"carried prose is frequently false, and the conversion carries it"*.
The mirror case is not covered and it happened here: **the conversion judged a TRUE claim false and
was one commit away from filing a defect against it.** The line was *"~1.5% standard error"* on a
HyperLogLog at `p = 14`; the textbook relative standard error is `1.04/√16384` = 0.81 %, so it was
classed as unsourced and arithmetically wrong. The cold reader found `metalog-spec/SPEC.md` §3.5.1 —
*"standard error ≤ 1.5%"* — a **ceiling**, which 0.81 % satisfies. **The search that would have
caught it stopped at the repo boundary**: this repo's own docs carry the figure nowhere, and that
zero was read as *unsourced*. `OPS-8.S9` needs the sentence: *before filing a deleted claim as a
stale claim, search the published specification and the sibling repos, not only this one.* A lane
that files a false defect costs the next reader more than the comment did.

## 7. A law block is owed only where the rule has NO ADDRESSABLE OWNER — the test, stated so it can be argued with

`ADR-26.D5` says a rule other sites obey is declared once in a block and cited by `LSRC-n`. It does
not say what to do when an ADR slot, a design-note slot or a bible **already** owns that rule, and
`OPS-8.O5` inherits the gap. Reading it as *any multi-site rule needs a block* would have demanded
at least four numbers from this repo's first four units, none of which this lane may issue. The test
used instead: **a block is owed only where the rule has no registry-form address.** Four candidate
sites were measured:

| site | the rule | owner | verdict |
|---|---|---|---|
| `src/cube/metalog.detail.cube.cppm`, two sites | no axes-equality gate on a cube pair | `DN-42.D17` | `refs:`, no block |
| `src/engine/engine.cpp`, four sites | the transparent-key *"same shape as the … above"* copy-on-first-sight rule | `ADR-9.D2` | `refs:`, no block |
| `src/serialization/json_egress.hpp` | one JSON write entry point per package | `DN-65.D2` (+ `DN-65.O4` for the rejected alternative) | `refs:`, no block |
| `src/stats/wire_format.cpp` | the two non-interchangeable wire spellings of `RunOutcome` | **none** | **block owed** |

Only the last has no owner, and the reason is structural rather than an oversight: its authority is
`metalog-spec/GOVERNANCE.md` §3, which `.claude/rules/adr-shelf-boundary.md` rules is a **different
owner** whose numbering is *"NOT citable from here"* — so no `refs:` line can reach it. One block,
not four. If the pilot rejects the test, the other three rows are the ones to revisit.

## 8. A NOTE ON THIS RUN'S OWN MEASUREMENT, because `OPS-8.S3.2` named the flaw mid-run

The step gained this text on 2026-09-06, after units 1-4 of this run were already drafted:
*"R IS HELD, AND THIS STEP USED TO READ AS AN INSTRUCTION TO DEFEAT THE MEASUREMENT … a claim you
have already written into the tree is a claim the reader can read back to you."* **This run wrote R
claims into the tree as `note:` lines before its readers ran, and several readers did read them
back.** The affected answers are named in each unit's table; the clearest are unit 2's Q9 and unit
4's Q2 and Q10, where the agent quoted a line this conversion had written.

**What survives the flaw, stated so it is not over-claimed.** Nearly every answer cited independent
evidence *alongside* the converted line — canon's source, the published spec, a `DN` slot, a gtest
name, `CMakeLists.txt` — and four answers went strictly beyond anything in the tree's comments: the
false `observability only` note (unit 3), the vacuous `TerminatorRoleIsSalient` test and the
unwitnessed spec `SHOULD` (unit 2), and the `signed_shift_id` NONE/High-up collision (unit 4). So
the interrogation did its work as a **truth instrument** throughout; what it measured less strictly
than it should have, for the R claims written early, is **recoverability without the note**. Units 5
and 6 were drafted after the step changed and hold their R claims out of the tree.

## 9. A LEDGER THAT QUOTES A LAW BLOCK VERBATIM DECLARES IT A SECOND TIME — and the workspace gate is RED on it right now

Measured 2026-09-06, not on this repo: `python3 scripts/registry_grammar_lint.py` from the
workspace root exits non-zero with three `G15` failures, and every one of them is a law number
declared more than once because the declaring lane pasted its framed block into its own
`ccc_migration.md` as evidence. The ledger copy is indistinguishable from the source copy to the
sweep, which is the single-declaration property `ADR-26.D5` says form 1 exists to have: *"a sweep
for the declaration returns exactly one line. Two is an ambiguity, and every citation naming it is
now unresolvable."*

**`OPS-8.S10` walks a lane straight into this.** It requires the ledger entry to carry *"the forms
written"* and *"every stale claim deleted with the evidence"*, and the natural way to evidence a
newly minted law is to show the block. `OPS-8.S3.3` already warns *"never spell `D-LSRC-<digits>`
outside a real law block"* — but it says it about the **claims script**, in a step about `refs:`
grammar, where a lane writing its ledger four steps later has no reason to look. The rule needs to
be in `OPS-8.S10` too, phrased for the ledger: **describe what the law says, name it by its citation
form, and never paste the framed block.** This lane's own law-block section does exactly that, which
is why `insight-metalog` contributes none of the three failures — but that was luck of habit, not a
step it was following.

The three live failures are `insight-canon`'s to repair and are named here only because a red
workspace gate is a fact every lane in the wave needs, and because the runbook gap that produced it
is the same one the next lane will meet.

## 10. THE PER-FILE ADDRESS CENSUS IS NOT A STEP, AND ITS ABSENCE COST THIS RUN FIVE CODES

**Measured on this run's tenth unit, and it is the most serious thing this lane found in `OPS-8`.**
Converting `src/serialization/serialize.cpp` dropped **five distinct `SRC-<code>` addresses** —
three span-native codes carried on trailing comments inside one DTO struct, the ordinal-histogram
row-identity code, and the WHERE-label code that appeared on two `component` fields. The prose
carried them; the conversion deleted the prose and the claims script had no `refs:` line at those
four sites.

**Every witness stayed green.** Comment-only passed (it is comment-only). The grammar gate passed
(a missing citation is not a violation). Both toolchains passed. And
`registry_grammar_lint` passed — **95 claimed codes, 95 declared in source, zero failures** —
because each of the five is declared and cited *elsewhere in the repo*, so nothing dangled at the
workspace level. The loss is of **addressability at the site that obeys the rule**, which is
exactly what `SRC-`/`LSRC-` exist to provide, and no instrument in the protocol looks for it.

`OPS-8.O5` asks the lane to "record the code's full citer list in its ledger" — a repo-level
inventory for the pilot's cross-repo pass. That is not the same measurement and does not catch
this: a repo-level list is unchanged when a file stops citing a code the file next door still
cites.

**The check is two lines and belongs in `OPS-8.S7`, beside the code-only diff:**

```sh
diff <(rg -oP '<the SRC pattern>' <HEAD copy> | sort -u) \
     <(rg -oP '<the SRC pattern>' <converted file> | sort -u)
```

Per FILE, on the DISTINCT set, not the occurrence count. The occurrence count legitimately falls —
unit 7 went 38 → 37 because one code was mentioned twice in prose and once in a `refs:` — so
comparing counts produces a false alarm every time and trains the operator to ignore it. Comparing
the distinct SET is exact: it is empty or it names precisely what to repair. Unit 7 and unit 9 were
re-checked this way and both are clean; unit 10 was repaired to clean before its commit.

**Where it bites hardest is still ahead.** `api/` holds 73 declaring-position occurrences across
two interface units. There, a dropped address is not a lost citation but a lost DECLARATION, which
`registry_grammar_lint`'s G5 *would* catch per code — so the api/ unit fails loudly where this one
failed silently. The silent half is every `.cpp` body site in the repo, which is most of them.

---

---

# The address census (`OPS-8.O1` witness 5), run retroactively over units 11-14

The per-file registry-address census did not exist when units 11-14 landed; the instrument
(`technical_docs/operations/ccc_migration_tools/address_census.py`) was committed on 2026-09-06 and
this section is its retroactive run, one unit at a time against that unit's own pre-unit revision.
It compares the DISTINCT SET of addresses per file, so a conversion that folds two citations of one
code into a single `refs:` line moves no verdict.

**Five addresses were lost and are restored**, each re-derived at the artifact first, as the
instrument's output demands. They landed in their own commit.

| file | lost | disposition |
|---|---|---|
| `api/metalog.cppm` | `ADR-9` | restored as `refs: ADR-9.D4`, a document-to-slot refinement: that slot owns the ruling the note states — *"producers emit template strings inline only — SPEC-conformant because the emission modes are a producer MAY"* |
| `scripts/determinism_fixture.cpp` | `ADR-31.D8` | restored at three branches — the two reservoir oracles and the cube-collapse oracle — each of which obeys that rule; a `post:` describing what a branch emits does not carry it |
| `scripts/determinism_fixture.cpp` | `DN-056` | restored as `refs: DN-56.D2` at the compose record, the canonical form of a non-registry spelling |
| `scripts/service_edges_overcap_scenario.hpp` | `ADR-31.D8` | restored: the scenario names that hazard class as the reason its tie-break must be replayed cross-leg |

**Two losses are deliberate and are recorded with their evidence, not repaired.** Both are
memory-store citations — `MEM:toolchain-clang21-dev-gcc16-ship` in
`benchmarks/bench_ordinal_key_alloc.cpp` and `MEM:naming-a-class-does-not-immunize-you-against-it` in
`scripts/reservoir_streaming_scenario.hpp`. `ADR-26.D5` enumerates what a `refs:` may carry —
`ADR-n.Dm`, `DN-n.Dm`, `LSRC-n`, `SRC-<code>`, `F-SRC-<repo>:<file>`, `STU-n.Am`, `OPS-n.Sm`,
`BIB:<name>` — and a memory slug is not among them; the memory store's own index says it holds
neither project description nor decisions. **Both rules have durable owners, and those are now cited
at the sites**: `ADR-9.D2` carries the small-string trap with its 15- and 22-character bands,
`ADR-3.D4` the compiler-to-stdlib pairing, and `ADR-31.D8` with `ADR-20.D7` the reservoir arm's
subject. What was deleted is a pointer to a weaker authority; the claims themselves stand as tagged
lines.

**Three refinements the instrument reports and that must NOT be repaired**, since restoring them
would put back the weaker citation: `ADR-19` → `ADR-19.D1` (`bench_compose_diff_cube.cpp`),
`ADR-17` → `ADR-17.D8` (`corpus_windows_scenario.hpp`), `ADR-29` → `ADR-29.D2`
(`service_edges_overcap_scenario.hpp`).

**Additions, which the instrument reports and never fails**: `ADR-29.D2`,
`F-SRC-insight-metalog:test_golden_vectors.cpp` and `F-SRC-insight-metalog:spec_conformance_gate.sh`
in `api/metalog.cppm`; `F-SRC-metalog-spec:SPEC.md` in `bench_metalog.cpp` and in
`test_package.cpp`; `ADR-9.D2` and `ADR-3.D4` in `bench_ordinal_key_alloc.cpp`; `ADR-9.D3` in
`ngram_cap_scenario.hpp`.

**Two observations about the instrument itself, for whoever maintains it.**

* **It cannot see that a padded spelling and its slot are the same note.** `DN-056` was replaced by
  `DN-56.D2` at the same site; the census still reports `DN-056` as LOST, because it compares literal
  address tokens. That is the right default — normalising would risk calling a real loss a
  refinement — but the residue has to be dispositioned by hand, and a lane that trusts a clean exit
  code will not notice.
* **A hand-rolled census misses the bare document form.** This unit ran its own address comparison at
  unit 11 with a pattern that required a `.Dn` slot, and it reported *"no address lost"*. The
  committed instrument, whose pattern admits a bare `ADR-n`, found `ADR-9` gone from the same file.
  The two disagreed because one of them was written by the lane whose work it was checking.

---

# Findings this session raised, with their addressees

Recorded here because `OPS-8.S10` makes the findings-for-other-lanes list part of the ledger entry,
and gathered in one place because four of the six cross a lane boundary.

**1. A bare, file-wide `NOLINT` directive over a linted file — for the lane that owns
`insight-metalog` source, and it is READY TO EXECUTE.** `scripts/determinism_fixture.cpp` opens with
`// NOLINTBEGIN Test` and closes with `// NOLINTEND Test`. There is no parenthesis, so clang-tidy
reads a **bare** directive and suppresses every check between them. `scripts/` is not in `malf lint`'s
prune list, so the file is walked. Measured with `clang-tidy-21` under the one shared
`malf/config/.clang-tidy` and the file's own compile command, with and without the pair: **exactly 9
diagnostics differ** —

| check | count | class |
|---|---|---|
| `bugprone-exception-escape` on `main` | 1 | **`WarningsAsErrors`** — this file would FAIL, not warn |
| `readability-identifier-length` on `t0`, `t1`, `t2` | 6 | warning; the identifiers are at lines 213-215 and 264-266 |
| `readability-use-concise-preprocessor-directives` | 2 | warning; the two `#if defined(_WIN32)` sites |

The eight style diagnostics are trivially fixable at root — rename the three timestamps in both
blocks, and spell the two preprocessor tests `#ifdef`. The ninth is a design point rather than a
typo: a standing-gate fixture whose `main` lets an exception escape terminates the process, which is
arguably the intended failure mode, and that is the only suppression that should survive. **A
half-measure is available and was deliberately not taken**: narrowing the directive to the three
named checks fits no single line under the 100-byte column limit, and it would bless bypassing two
classes that a two-character edit removes. This unit therefore left the bytes alone and states the
scope in a `note:`.

**2. A census of malformed suppressions that searches for a SPACE cannot see the whole class — for
Argos.** The workspace's malformed-`NOLINT` population has so far been taken as the sites spelled
`NOLINTNEXTLINE (check)`, with a space before the parenthesis. **That search cannot find
`NOLINTBEGIN Test`**, which has no parenthesis at all and is the identical defect — clang-tidy reads
both as bare and suppresses everything — reached by another door. The pattern that finds both is
`NOLINT(NEXTLINE|BEGIN|END)?(?!\()`: every directive token not immediately followed by `(`. Over
`insight-metalog` it returns 18 lines, 16 of them the tight sites' own `note:` prefaces and 2 the
one bare pair in `scripts/determinism_fixture.cpp`. The workspace-wide number is unmeasured here.
Whoever tightens `malf/comment_contract_lint.py`'s recogniser should tighten it against BOTH
spellings, or the second class stays invisible to the instrument after the first is fixed.

**3. A benchmark that breaks the rule its sibling states — for Kleio, with Argos to say whether the
published numbers move.** `benchmarks/bench_compose_diff_cube.cpp` refuses a `std::*_distribution`
by name, because the engine-bits-to-value mapping is implementation-defined and the draw sequence
therefore differs between libstdc++ and libc++. `benchmarks/bench_metalog.cpp` uses one at three
sites on `std::mt19937` — `std::uniform_real_distribution<double>` at line 49 in `run_once`,
`std::uniform_int_distribution<int>` at line 134 in the field-histogram arm, and
`std::uniform_int_distribution<std::size_t>` at line 188 in the WHERE arm — and `BM_MetaLogCompress`
additionally advances its seed per iteration (`seed++` at line 86), so its corpus is not fixed even
within one run. The engine is portable; the distributions are not. Its figures are therefore not
comparable across the two toolchains they are measured on, and `coderoast-hub/benchmarks/` publishes
them.

**4. A standing regression guard that nothing enforces — for Kleio.**
`benchmarks/bench_ordinal_key_alloc.cpp` is the regression guard for zero allocations per event on
both toolchains. Nothing in the tree compares `allocs_per_event` to a threshold: a run reading 1
again blocks nothing and has to be noticed by a person. A search over `insight-metalog`,
`coderoast-hub`, `.github` and `malf` finds the identifier only in the two benchmark sources and one
published column header.

**5. The `≤ 4 KB per million lines` target has a live home and this repo's benchmark is outside its
scope — for Eqya.** `metalog-spec/SPEC.md` § 11.5 scopes the headline to a `stats`-only document, no
`reservoir`, no `behavior`, no `cube`, reached at `top_k_size ≤ 32` inline or `≤ 64` id-only.
`benchmarks/bench_metalog.cpp`'s `BM_MetaLogCompress` sets `top_ngrams_size = 32` and
`max_ngram_keys = 4096` with the cube always on, and the published 100 000-line arm reads about
**156 KB per million lines** — roughly forty times the headline, against a scope the headline does
not govern. Whether the repo wants an arm that IS in scope is a plan-tier question, not a comment.

**6. An empty composition is not declared on the wire — for the lane that owns the producer.**
`scripts/corpus_windows_scenario.hpp` composes against an empty semantic set on purpose, so the
emitted document is a function of the corpus bytes plus canon core and the engine alone. But
`configure()` never sets `cfg.ruleset`, so the document omits the `ruleset` block entirely, and a
consumer reads that absence as *legacy producer* rather than as *composed against nothing*. Raised
by the `scripts/` cold reader from the code, not from any prose.

---

# What this session adds to the `OPS-8` verdict

Read against the runbook as it stood at 02:00 on 2026-09-06; the earlier items in the verdict above
are the previous run's and are not restated.

## 10. A COMMENT-ONLY STRIP LEAVES A LEADING BLANK LINE, AND NO GATE SEES IT

Stripping a file-leading comment block removes the comment lines but not the blank line that
followed the block, so a file whose first construct was a header comment comes out of `OPS-8.S5`
starting with an empty line. **Six of the eight files in the `benchmarks/` unit did.** Nothing
catches it: `malf format --check` reports 0 misformatted, the CCC phase counts no violation, and the
comment-only witness drops all whitespace by construction, so it is invisible to every arm
`OPS-8.O1` lists. Removed by hand here and confirmed by a repo-wide sweep that now returns zero.
The one-line addition `OPS-8.S7` wants: after copying the draft over the unit, strip a leading blank
line, because the strip creates one wherever the file opened with a comment.

## 11. `claims_lib`'s ANCHOR PARSER MIS-READS A C++ DIGIT SEPARATOR, AND THE SYMPTOM POINTS AT THE WRONG THING

The shared claims placer decides what a line's code part is by walking it and tracking string and
character literals. A C++ digit separator — `0x5EED'0003` — is an apostrophe, so the walk enters a
"character literal" it never leaves, a trailing `//` comment on that line is never stripped, and the
original and the stripped draft disagree about that line's code text. The failure is LOUD, which is
why it costs little: the placer refuses with *"draft code lines differ from original — strip is not
comment-only"*. **But the message names the STRIPPER**, and the stripper is innocent; a lane that
believes it will go looking for a comment-only violation that does not exist. Hit once here, on
`benchmarks/bench_compose_diff_cube.cpp`, whose seeds are all written with separators. The fix is
three lines: an apostrophe between two alphanumeric characters is a separator, not a quote. Note
that the committed per-unit claims scripts in the LogCraft tools directory carry a different parser
(`re.sub(r"\s*//.*$", "", line)`) with a different bug of the same family — it would strip a `//`
inside a string literal — so neither is a safe base without a look.

## 12. THE MIRROR LESSON FIRED ON THE LANE THAT WROTE IT, AND THE BOUNDARY THAT MATTERED WAS A SIBLING REPO

`OPS-8.O3` gained its third bullet from this repo's previous run: *"before filing carried prose as
false, widen the search past the repo boundary — the sibling repos, the spec, the ADR shelf — and
record where you looked."* This run then condemned `bench_metalog.cpp`'s *"≤ 4 KB per million
lines"* target as unsourced after searching the full `technical_docs/` listing, an `rg` over the
superproject doc tier and this repo's own, `DN-024` opened directly, and an `rg -l` over `adr/`,
`product/` and `bibles/`. **Every one of those is inside the superproject or this repo.** The
cold reader found the target alive in `metalog-spec/SPEC.md` § 11.5. The rule is right and was not
followed; what makes it hard to follow is that the search felt exhaustive because it enumerated
several surfaces. The sharper form: **name the sibling repos you searched, by name, or you did not
search them** — a list of surfaces inside one repository is not a widened search.

## 13. A PER-CODE SWEEP WHOSE OUTPUT IS CAPPED READS AS A COMPLETE POPULATION

Establishing where a retired `SRC-<code>`'s statement lives means sweeping the workspace per code.
A sweep that pipes each code's hits through `head -12` returns twelve lines that all happen to come
from the migrating repo, and the conclusion *"there is no site outside this repo"* follows and is
false. Caught here before a finding was filed against `ADR-29.D6`; re-running the sweep scoped to
`insight-canon` alone returned three sites. The cap is the hazard, not the pattern.

---

# Where this run stopped, and why

**Fourteen units converted, one law minted, one lint-surface repair landed, the repo NOT armed.**
Arming (`OPS-8.S12`) requires the whole repo at zero, which this run does not reach, so
`comment_contract: true` is **not** set and the CCC phase still counts `insight-metalog` rather than
failing it.

| unit | surface | files | would-be violations | comment lines | reader |
|---|---|---|---|---|---|
| 1 | `src/` root — `metalog.internal.cppm`, `metalog.api.impl.cpp` | 2 | 8 | 9 → 4 | 3 of 3 recovered |
| 2 | `src/stats/` minus `wire_format.cpp` | 3 | 284 | 289 → 80 | 12 of 12 recovered |
| 3 | `src/cube/` minus `cube.cpp` | 2 | 134 | 136 → 47 | 10 of 10 recovered |
| 4 | `src/cube/cube.cpp` | 1 | 249 | 251 → 98 | 12 of 12 recovered |
| 5 | `src/serialization/json_egress.hpp` | 1 | 35 | 36 → 11 | 8 of 8 recovered, one |
| 6 | `src/operations/metalog.detail.operations.cppm` | 1 | 12 | 13 → 6 | reader over both |
| 7 | `src/engine/engine.cpp` | 1 | 361 | 363 → 181 | 12 of 12 recovered |
| 8 | `src/stats/wire_format.cpp` — the law block | 1 | 28 | 29 → 26 | 6 of 6 recovered |
| 9 | `src/operations/` compose + diff | 2 | 419 | 424 → 154 | 12 of 12 recovered |
| 10 | `src/serialization/serialize.cpp` | 1 | 326 | 329 → 121 | 9 of 10, 1 wrong |
| 11 | `api/metalog.cppm` | 1 | 326 | 327 → 154 | 15 of 15 recovered |
| 12 | `benchmarks/` | 8 | 241 | 250 → 97 | 13 of 13 recovered |
| 13 | `scripts/` | 9 | 545 | 554 → 169 | 13 of 13 recovered |
| 14 | `test_package/test_package.cpp` | 1 | 16 | 16 → 13 | 5 of 5 recovered |
| | **total** | **34** | **2 984** | **3 026 → 1 161 (62 % fewer)** | **130 of 131 recovered, 0 not recovered, 1 wrong** |

**2 984 of the repo's 6 701 would-be violations, 44.5 %, in fourteen commits.** Every claim held for
a reader was recovered; nothing had to be re-homed above the comment rung, and no claim was lost.

**NINE LINES THESE CONVERSIONS THEMSELVES WROTE WERE FOUND DEFECTIVE BY A COLD READER AND CORRECTED
BEFORE OR JUST AFTER THEIR WITNESS — AND NOT ONE BY ANY GATE.** In order: the `observability only`
note in unit 3, false; the `assert:` in unit 5, which dropped the qualifier its premise rested on;
the `open_window` `invariant:` in unit 7, whose universal *every* was false for three members; the
`salience_memory` `note:` in unit 9, false for the half of the map that is range-iterated; in unit
10 both a false `note:` on `approximate_cardinality` and a `refs:` that had landed on the wrong
declaration; in unit 11 a `refs:` naming the weaker of two witnesses — the derived-value pins rather
than the byte-exact golden vectors; in unit 13 an `invariant:` and its `post:` twin asserting that a
compare-at-min diff's axes equal *neither* input's, where the arithmetic makes them equal the
collapsed input's. Seven of the nine are false CLAIMS; two are placement or reference errors the
gate is documented as unable to check.

**AND ONE CLAIM THE CONVERSION WRONGLY DELETED WAS RESTORED BY A READER, which is the failure mode
that leaves no witness.** Unit 12 filed the *"≤ 4 KB per million lines"* target as unsourced after
searching the superproject's doc tier and this repo; the reader found it alive in
`metalog-spec/SPEC.md` § 11.5, scoped to a `stats`-only document. Re-homed with the scope stated and
the specification cited. This is the second time the mirror direction has fired in this repo — the
first was withdrawn before it reached a commit, this one was already in the tree.

Every unit comment-only against `HEAD` by code-token-stream equality, every unit at zero would-be
violations under `malf format --check`, and `malf test insight-metalog` **297 of 297 on clang-21 and
297 of 297 on gcc-16** after conversion, equal to the baseline taken before the first unit. **The
fifth witness — the per-file address census — did not exist for units 1-14 and was run
retroactively over units 11-14** once its instrument was committed; it found five lost addresses,
all restored in their own commit, and two deliberate deletions with their evidence. Units 1-10
have not been censused; that is the next lane's cheapest first act, one command per unit against
each unit's own pre-unit revision.

**The repo's own gate reading at the close of this session, verbatim:**

```
malf format: CCC SUMMARY · mode=check-paths · files 70 = armed 0 + report-only 70 + NOT CHECKED 0 ·
armed repos: none · comment lines 5007 · forms pre=22 post=158 invariant=214 assert=51 note=198
refs=163 continuation=288 law=1 tool=173 · violations in armed files 0 (none) · would-be violations
in report-only files 3717 (bare=3265 tag-mid-line=1 slash3=28 spacer=243 ruler=2 trailing=178) ·
rc=0
```

**It matches the per-unit arithmetic, and the residue is accounted for rather than rounded away.**
The units alone give 6 868 − 3 026 + 1 161 = **5 003** comment lines, and the address-census commit
then added **four** `refs:` lines at the sites whose addresses it restored, which is the 5 007 above
and the `refs=163` against the units' 159. Would-be violations are untouched by that commit:
6 701 − 2 984 = **3 717**, as read.

**`suppression-without-why` has reached 0** (13 at the baseline, 4 after the first ten units): three
file-wide directive pairs in `benchmarks/` were deleted with the measurement that they silence
nothing, and the one in `scripts/` was re-homed under its `note:`. `law=1` is unit 8's block. Tool
forms 175 → 173, the three deleted `NOLINTEND` lines less the one `scripts/` retained.

**What remains, and it is now exactly two surfaces.** `api/metalog.api.cppm` **1 088** — the public
DTO surface, and the last source unit — and the **test tier, 2 629** over 34 files, largest first:
`tests/operations/test_compose_algebra.cpp` 439, `test_golden_vectors.cpp` 196,
`tests/determinism/test_determinism_gate.cpp` 173, `tests/cube/test_cube.cpp` 170,
`tests/reservoir/test_reservoir.cpp` 144, `test_canonicalization_version_ruleset_coverage.cpp` 136,
`tests/reservoir/test_retention_axis_census.cpp` 100, then 27 files under 100 each.

**THE `api/metalog.api.cppm` UNIT IS NOT BLOCKED ON A LAW NUMBER, and the read that establishes it
is done.** The previous run left `api/` marked *"must be read for statement-bearing `SRC-<code>`
sites first"*. Both files were read. `api/metalog.cppm` converted as unit 11 and carries citations
only. `api/metalog.api.cppm` holds the statements of three codes, and **each has an addressable
owner**, so the next lane cites rather than mints:

| code | statement site in `api/metalog.api.cppm` | owner to cite |
|---|---|---|
| `SRC-D-TIR-5` | the `TemplateRegistry` class block | **`ADR-16.D3`** — *"the engine-owned `TemplateRegistry` is the single id → str home … display-only by construction: it never feeds a decision path, intern order never affects content, and it is append-only"* |
| `SRC-D-W1-1` | the `ordinal_w1` function block | `STU-3` for the pre-registered thresholds, `insight-canon/core/api/canon.api.cppm` for the catalog |
| `SRC-D-W1-4` | the schedule-id comparability gate | `insight-canon/core/api/canon.api.cppm`, which states the versioned catalog's stable id IS the comparability key |

The test-tier files with a leading-block `SRC-` site — `tests/engine/test_ordinal_histograms.cpp`,
`tests/engine/test_span_edges.cpp`, `tests/stats/test_stats.cpp` — were not read for statement
bearing; that is the test tier's own first step.

**Declared departures from `OPS-8` in this session.**

* **The build slot was held per build, not for the whole run** (`OPS-8.S1.1`, and the pilot's brief
  instructed it): four CCC lanes shared one global slot. Two acquisitions, **24 minutes 2 seconds
  blocked in total** — 8 minutes 16 seconds for batch A and 15 minutes 46 seconds for batch B, both
  taken by a background poller with `MALF_BUILD_SLOT_ANCHOR` pinned to the session pid, whose stamp
  read `ALIVE` at every check and at the pre-release check.
* **The behaviour witness is per BATCH, one per slot acquisition** (`OPS-8.S7.4`). The grain, named
  so a later reader does not assume a finer one:

  | behaviour witness | slot acquired | units it covers | result |
  |---|---|---|---|
  | batch A | 2026-09-06 01:51 | units 11 and 12 | 297 of 297 clang-21, 297 of 297 gcc-16 |
  | batch B | 2026-09-06 02:19 | units 13 and 14 | 297 of 297 clang-21, 297 of 297 gcc-16 |

  Detection is unaffected by the batching: witness 1 proves each file's code token stream
  byte-identical to `HEAD`, so a comment-only unit can reach behaviour through `__LINE__` and
  nothing else. Five comment-only repairs landed AFTER their batch — unit 11's `refs:`, unit 12's
  restored target and strengthened `pre:`, unit 13's false `invariant:`/`post:` pair and its caller
  enumeration — and each was re-witnessed against `OPS-8.S7` steps 2 and 3.
* **One commit in this session is NOT a CCC unit and says so in its subject.** `fix(lint)` repairs
  `src/operations/compose.cpp`'s spaced `NOLINTNEXTLINE`, which unit 9 had measured and deliberately
  left. Both changed lines are comments and the file's line count is unchanged at 719, so the object
  code cannot move and no behaviour witness is owed; what changes is the lint surface, which is why
  it is separate. The narrowing surfaced **nothing new** — the diagnostic set over the file is
  byte-identical before and after, one `readability-avoid-nested-conditional-operator` at line 509
  in both — and removing the directive entirely surfaces exactly one more, the
  `readability-use-std-min-max` on the clamp, so the suppression is not one that silences nothing.
  The site was tightened rather than removed because it is a copy of `src/engine/engine.cpp`'s twin,
  which already carries the tight spelling, and the repo has three sibling suppressions of the same
  check all stating the same conditional-store why. Whether that house idiom is worth its four
  suppressions is a design question with four sites and was not settled here.
* **THE TREE WAS NOT FROZEN FOR ONE COLD READER, and it is declared rather than hoped over.**
  `test_package/test_package.cpp` was converted and formatted in the working tree at about 02:04
  while the `scripts/` reader was live (spawned 02:02, finished about 02:09). The file is outside
  that reader's unit and its answers cite no `test_package` path, so no contamination is visible —
  but a harness notice carrying pre-conversion bytes cannot be ruled out from here, and **a
  contaminated reader that does not notice scores as a clean one**. The rule the wave adopted the
  same evening is right and was not followed: freeze the tree for the reader's whole window,
  including files outside its unit.
* **`OPS-8.S2`'s source-before-tests order was kept, with one file-group split.** `api/` was split
  into its two files: 1 416 comment lines across them is one questionnaire pretending to be two, and
  the two halves have disjoint subjects — the facade's engine state against the DTO surface.
* **The harness tier was taken before the last source unit.** `benchmarks/`, `scripts/` and
  `test_package/` are three completable units against `api/metalog.api.cppm`'s 200 comment blocks;
  taking them first banked 802 violations and left the last source unit whole for a lane that can
  give it one session. The ordering rule's own reason — that a test's `refs:` cites slots the source
  unit names — does not bind here: the harness cites `ADR`/`DN` slots and the specification, not
  slots this repo's `api/` unit would have minted.

---

# What the unit-15 session adds to the `OPS-8` verdict

Read against the runbook as it stood at 05:30 on 2026-09-06; the earlier items in the verdicts
above are the previous runs' and are not restated.

## 14. THE COMMITTED `address_census.py`'s INBOUND LEG SEARCHES ONE DIRECTORY, AND THAT IS THE ONE PLACE THE CLASS IT EXISTS TO FIND DOES NOT LIVE

`OPS-8.S7.3b` says to run the census *"from inside the repo that owns the files"*, and the inbound
mode calls `inbound(paths, ["."])` — the current working directory. Run as instructed, from inside
the migrating repo, the inbound leg **cannot see a sibling repo naming your file**, which is
exactly the population the mode was added for: its own docstring cites `insight-eidos` resting a
claim on prose `engine/api/engine.cppm` had lost. Measured here: from inside `insight-metalog` the
leg returned **2** mentions; the same sweep over the workspace with the `CLAUDE.md` recipe returned
**over sixty files**, including nine `insight-eidos` sites resting on this file's `TemplateRegistry`
contract, a verbatim prose quote in an `insight-eidos` measurement tool, and two dead line-number
coordinates in an `insight-eidos` contract document. The instrument is right and its default root
is wrong; until it takes a `--roots` argument, run the workspace sweep by hand and say so.

## 15. `claims_lib.render()` EMITS AN UNTAGGED SPEC WITHOUT ITS `//`, AND EVERY WITNESS BUT ONE IS BLIND TO IT

The shared placer treats a spec that does not match its tagged-line regex as *"a tool form or a
law-block line, verbatim"* and writes it as `f"{indent}{spec}"`. That is correct for `NOLINTNEXTLINE`
and for a law-block frame. It is a trap for the shape a lane will actually write, because a
two-line contract claim is naturally authored as two strings — and the second one, carrying no
tag, lands as a **bare code line**. Measured here on the first run: about eighty broken lines, and
**the CCC gate reported only 2 violations** over the file, because a line with no `//` is not a
comment and the checker never looks at it. `malf format --check` said 0 misformatted. The witness
that catches it is `code_only_diff.py` and nothing else. The fix is three lines at the call site —
join a spec that does not open with a tag onto the one before it, so `flow()` wraps the whole claim
and the two-line contract budget is enforced on the claim rather than on each fragment.

## 16. A CONVICTION CAN BE ABOUT A CITATION AS WELL AS A CLAIM, AND `OPS-8.S9`'s SPLIT READS AS THOUGH IT IS ONLY ABOUT SENTENCES

Two of this unit's four conviction verdicts were about the sentence a line asserts. A third was
about a `refs:` — a reader pointed out that `DN-42.D17` owns the minimal-common-collapse rule and
nothing else in the block it trailed, so at the end of four invariants it read as vouching for an
MSVC miscompile claim it says nothing about. The address resolved, the gate was green, and the
citation was still wrong in the only sense that matters. `OPS-8.O2` already records that the gate
checks a tagged line's FORM and never its PLACEMENT; the same blindness applies to a `refs:` inside
a multi-claim block, and the remedy is the same — read where each line landed, and put the `refs:`
next to the claim it answers for rather than at the end of the block.

## 17. THE MIRROR LESSON'S OTHER HALF: A CARRIED CLAIM CAN BE FALSE BECAUSE THE WORLD MOVED, NOT BECAUSE THE PROSE WAS EVER WRONG

`OPS-8.O3` warns that carried prose is frequently false. Two of this unit's four convictions are a
narrower species worth naming, because the search that catches them is different: the prose was
TRUE when written and a **measurement since** made it false. `TemplateRegistry`'s MSVC reason was
refuted by a real MSVC golden run on 2026-09-04 — one day after the prose's own dated
re-measurement, which is why the comment reads current — and the specification scoped
`dropped_ngram_observations`' normative absence to `metalog_version` 0.7.0 or later while the
comment stated it flatly. Neither is findable by asking *is this sentence internally coherent?*;
both are found by asking *when was this last measured, and has anything measured it since?*
(`MEM:an-instrument-has-a-lifetime`). For a claim with a date in it, read the record AFTER that
date before carrying it into a tagged line.

---

# Where the unit-15 session stopped

**Fifteen units converted in total, no law minted in this session, the repo still NOT armed.**
Arming (`OPS-8.S12`) needs the whole repo at zero and the test tier is still 2 629 violations, so
`comment_contract: true` is not set and the CCC phase keeps counting `insight-metalog` rather than
failing it.

| unit | surface | files | would-be violations | comment lines | readers |
|---|---|---|---|---|---|
| 1-14 | (the two previous sessions) | 34 | 2 984 | 3 026 → 1 161 | 130 of 131 recovered |
| 15 | `api/metalog.api.cppm` | 1 | 1 088 | 1 089 → 636 | 38 of 42 recovered, 4 wrong |
| | **total** | **35** | **4 072** | **4 115 → 1 797 (56 % fewer)** | **168 of 173 recovered, 0 not recovered, 5 wrong** |

**4 072 of the repo's 6 701 would-be violations — 60.8 %, in fifteen commits.** Every claim held
for a reader was recovered or repaired; **nothing has had to be re-homed above the comment rung in
any unit of this repo**, and no claim has been lost.

**THIRTEEN LINES THESE CONVERSIONS THEMSELVES WROTE HAVE NOW BEEN FOUND DEFECTIVE BY A COLD READER
AND CORRECTED BEFORE COMMIT — nine in the earlier sessions and four here — and not one by any
gate.** The four here, in order: an *"pre-registered, not fitted"* claim about the W1 octave
thresholds whose cited owner holds them fixed as inputs and pre-registers something else; a
`TemplateRegistry` MSVC justification that a real MSVC run had refuted two days earlier; a
*"trivially copyable"* claim about a struct that owns a vector; and a normative-absence claim
quoted without the `metalog_version` scope the specification puts on it. All four are
**convictions** rather than reader-wrong: in each the reader answered the underlying question
correctly from the tree and thereby contradicted the residual line, which is the form succeeding.

**Behaviour, the whole session in ONE batch.** One slot acquisition covering unit 15 alone;
`malf test insight-metalog` **297 of 297 on clang-21 and 297 of 297 on gcc-16**, equal to the
baseline the first session took. Slot wait for the session: **0 seconds** on the first acquisition
and **161 seconds** on the second, 161 seconds in total, both taken with `MALF_BUILD_SLOT_ANCHOR`
pinned to the lane's session pid and both tested on the acquire's EXIT STATUS — the refusal prints
the *holder's* token, so a grep for the word passes on failure.

**What remains: the test tier alone, 2 629 would-be violations over 34 files.** Largest first:
`tests/operations/test_compose_algebra.cpp` 439, `test_golden_vectors.cpp` 196,
`tests/determinism/test_determinism_gate.cpp` 173, `tests/cube/test_cube.cpp` 170,
`tests/reservoir/test_reservoir.cpp` 144, `test_canonicalization_version_ruleset_coverage.cpp` 136,
`tests/reservoir/test_retention_axis_census.cpp` 100, then 27 files under 100 each. **Every source,
harness and package surface in this repo now reads zero.**

**THE RESUME POINT IS `tests/reservoir/` (2 files, 244 would-be violations), and it is prepared
rather than merely named.** Its token census is taken and non-trivial — `test_reservoir.cpp`
carries **13** `/*name=*/` argument comments and 3 namespace closers, `test_retention_axis_census.cpp`
**6** and 1, and the stripper's kept counts match those 16 and 7 exactly, with removed 144 and 100
equalling each file's violation count. Two notes for whoever takes it. `test_retention_axis_census.cpp`
opens with a 69-line header carrying the test-homing call, three arming conditions, a **declared
expiry** and a falsifiability record of two applied-and-reverted mutations; the arming conditions
survive in the assertion messages, which are code, and the expiry's argument is owned by
`DN-64.D6`, so the header converts by citation rather than by re-homing. And `test_reservoir.cpp`'s
`SRC-D-RNK-2` block carries the measured loss that motivated the error-class reserve, which is
history and goes, while the rule it states is already `refs:`-able at the config member this unit
converted.

**The three test-tier files with a leading-block `SRC-` site — `tests/engine/test_ordinal_histograms.cpp`,
`tests/engine/test_span_edges.cpp`, `tests/stats/test_stats.cpp` — still have not been read for
statement bearing.** That remains the first act of whichever unit covers them.

---

# Where the session stopped, after unit 16

**Sixteen units converted, no law minted in this session, the repo still NOT armed.** Arming
(`OPS-8.S12`) needs the whole repo at zero.

| unit | surface | files | would-be violations | comment lines | reader |
|---|---|---|---|---|---|
| 1-14 | (the two previous sessions) | 34 | 2 984 | 3 026 → 1 161 | 130 of 131 recovered |
| 15 | `api/metalog.api.cppm` | 1 | 1 088 | 1 089 → 636 | 38 of 42 recovered, 4 wrong |
| 16 | `tests/reservoir/` | 2 | 244 | 267 → 100 | 12 of 12 recovered |
| | **total** | **37** | **4 316** | **4 382 → 1 897 (57 % fewer)** | **180 of 185 recovered, 0 not recovered, 5 wrong** |

**4 316 of the repo's 6 701 would-be violations — 64.4 %, in sixteen commits.**

**What remains: 2 385 would-be violations over 32 test files.** Largest first:
`tests/operations/test_compose_algebra.cpp` 439, `test_golden_vectors.cpp` 196,
`tests/determinism/test_determinism_gate.cpp` 173, `tests/cube/test_cube.cpp` 170,
`test_canonicalization_version_ruleset_coverage.cpp` 136, `test_presence_churn_property.cpp` 98,
`test_comparison_outcome.cpp` 98, `test_stability_vs_diff_divergence.cpp` 95,
`tests/serialization/test_egress_encoding_conformance.cpp` 91, then 23 files under 90 each.

**THE RESUME POINT IS `tests/cube/` (2 files, 218 would-be violations —
`test_cube.cpp` 170 and `test_cube_emerging_border.cpp` 48).** One subject, one reader. After it,
`tests/serialization/` (4 files, 252) and `tests/engine/` (9 files, 268) are the next two
self-contained directories; `tests/operations/` (15 files, 1 402) is the one that needs a split by
file group and two readers, and it should be taken last so its `refs:` can cite everything the
earlier test units name.

**The three test-tier files with a leading-block `SRC-` site — `tests/engine/test_ordinal_histograms.cpp`,
`tests/engine/test_span_edges.cpp`, `tests/stats/test_stats.cpp` — still have not been read for
statement bearing.** That remains the first act of whichever unit covers them, and it is what
decides whether `tests/engine/` and `tests/stats/` are unblocked or owe a law number.

---

# Where the session stopped, after unit 19 — the handover

**Nineteen units converted, no law minted in this session, the repo is NOT armed.** Arming
(`OPS-8.S12`) needs the whole repo at zero and the test tier still carries 1 647 would-be
violations.

| unit | surface | files | would-be violations | comment lines | reader |
|---|---|---|---|---|---|
| 1-14 | (the first two sessions) | 34 | 2 984 | 3 026 → 1 161 | 130 of 131 recovered |
| 15 | `api/metalog.api.cppm` | 1 | 1 088 | 1 089 → 636 | 38 of 42 recovered, 4 wrong |
| 16 | `tests/reservoir/` | 2 | 244 | 267 → 100 | 12 of 12 recovered |
| 17 | `tests/cube/` | 2 | 218 | 227 → 18 | 29 of 29 recovered |
| 18 | `tests/serialization/` | 4 | 252 | 262 → 36 | 27 of 29, 2 not recovered |
| 19 | `tests/engine/` | 9 | 268 | 289 → 27 | 27 of 27 recovered |
| | **total** | **52** | **5 054** | **5 160 → 1 978** | **263 of 270 recovered, 2 not recovered, 5 wrong** |

**5 054 of the repo's 6 701 would-be violations — 75.4 %, in nineteen unit commits plus one
correction commit.** The gate's own line after unit 19, verbatim:

> `malf format: CCC SUMMARY · mode=check-paths · files 70 = armed 0 + report-only 70 + NOT CHECKED
> 0 · armed repos: none · comment lines 3700 · forms pre=31 post=177 invariant=577 assert=64
> note=202 refs=281 continuation=525 law=1 tool=173 · violations in armed files 0 (none) ·
> would-be violations in report-only files 1647 (bare=1461 tag-mid-line=1 slash3=1 spacer=144
> trailing=40) · rc=0`

**Behaviour, one batch covering units 17, 18 and 19.** One slot acquisition;
`malf test insight-metalog` **297 of 297 on clang-21 and 297 of 297 on gcc-16**, equal to the
baseline the first session took. Slot wait for this session: **1 171 seconds on one acquisition**,
1 171 seconds in total, taken with `MALF_BUILD_SLOT_ANCHOR` pinned to the lane's session pid and
tested on the acquire's EXIT STATUS.

## What remains: 1 647 would-be violations over 18 files, and the arithmetic is the gate's

**Checked against `malf format --check insight-metalog`, not against the previous marker.** Per
directory: `tests/operations/` 1 402 · `tests/determinism/` 173 · `tests/stats/` 61 ·
`tests/metalog.test.cppm` 11. Those four sum to **1 647**, which is the gate's own figure.

Per file, largest first — these eighteen also sum to 1 647:

`tests/operations/test_compose_algebra.cpp` 439 · `test_golden_vectors.cpp` 196 ·
`tests/determinism/test_determinism_gate.cpp` 173 ·
`test_canonicalization_version_ruleset_coverage.cpp` 136 · `test_presence_churn_property.cpp` 98 ·
`test_comparison_outcome.cpp` 98 · `test_stability_vs_diff_divergence.cpp` 95 ·
`test_presence_churn_monoid.cpp` 68 · `test_presence_churn_rank_boundary.cpp` 63 ·
`tests/stats/test_stats.cpp` 61 · `test_shift_sample_floor.cpp` 45 ·
`test_retention_profile_name.cpp` 41 · `test_ruleset_identity.cpp` 31 · `test_diff_blocks.cpp` 31 ·
`test_reservoir_delta.cpp` 27 · `test_param_histograms_compose.cpp` 25 ·
`tests/metalog.test.cppm` 11 · `test_processing_identifiers.cpp` 9.

## The unit plan for what is left — four units, in this order

**Unit 20 — `tests/determinism/test_determinism_gate.cpp` + `tests/stats/test_stats.cpp` +
`tests/metalog.test.cppm` (3 files, 245).** Taken together because the harness module is what the
other two import and a reader must read it either way. `tests/stats/test_stats.cpp` carries one of
the three leading-block `SRC-` sites (line 17, `SRC-D-TIR-2`), settled below.

**Unit 21 — `tests/operations/test_compose_algebra.cpp` alone (1 file, 439).** One file, one
subject: the composition algebra's three clauses at three modal strengths. It is the densest file
in the repo — 1 399 lines, with derived arithmetic written out per arm — and it earns its own
reader.

**Unit 22 — the identity and versioning group (5 files, 413):** `test_golden_vectors.cpp` 196 ·
`test_canonicalization_version_ruleset_coverage.cpp` 136 · `test_retention_profile_name.cpp` 41 ·
`test_ruleset_identity.cpp` 31 · `test_processing_identifiers.cpp` 9.

**Unit 23 — the diff group (9 files, 550):** `test_presence_churn_property.cpp` 98 ·
`test_comparison_outcome.cpp` 98 · `test_stability_vs_diff_divergence.cpp` 95 ·
`test_presence_churn_monoid.cpp` 68 · `test_presence_churn_rank_boundary.cpp` 63 ·
`test_shift_sample_floor.cpp` 45 · `test_diff_blocks.cpp` 31 · `test_reservoir_delta.cpp` 27 ·
`test_param_histograms_compose.cpp` 25.

245 + 439 + 413 + 550 = **1 647**. At zero, arm the repo: `comment_contract: true` at the top level
of `packages.yml`, then re-run `malf format --check insight-metalog` and confirm the summary reads
`armed 1` with `violations in armed files 0` rather than `report-only`.

## The three leading-block `SRC-` files: READ, and NONE is statement-bearing

This was the blocking question and it is answered. Measured with `registry_grammar_lint`'s own
decider — its `SRC_SWEEP` and `SRC_CITE` patterns, declaration classed by POSITION (an interface
unit, or a `.cpp`'s first 40 lines) — over every sibling repo:

| file | code | where the code's STATEMENT lives | verdict |
|---|---|---|---|
| `tests/engine/test_ordinal_histograms.cpp` line 1 | `SRC-D-W1-2` | `insight-canon/core/api/canon.api.cppm` — the per-schedule wire identity, the frozen versioned ladder and its bin count, three declaring-position sites | **citing** |
| `tests/engine/test_span_edges.cpp` line 2 | `SRC-D-OTEL-11` | the same canon interface — a span record's declared causality resolved into the n-gram graph at window close, four declaring-position sites | **citing** |
| `tests/stats/test_stats.cpp` line 17 | `SRC-D-TIR-2` | `insight-eidos` — `sift/src/sift.detail-shared.cppm` plus three `*.test.cppm` fixtures; the full argument is in the disposable attic | **citing** |

In all three the prose beside the code describes **the test**, never the code's rule, which is
`OPS-8.O5`'s criterion. **So neither `tests/engine/` nor `tests/stats/` ever owed a law number.**
The first two were converted in unit 19 as one-line `refs:` at the same position class, and
`registry_grammar_lint` stayed at 95 claimed codes, 95 declared in source, 0 failures. The third,
in unit 20, converts the same way — and unit 20's draft already does exactly that.

## The rules at `api/metalog.api.cppm` that owe a law number — text included, so no re-derivation

Recorded by an earlier session and re-stated here in full, because a law number is the pilot's to
issue and the next session should be able to ask for one without opening the file. `D-LSRC-`
numbers are workspace-global, append-only and checked DENSE; **15 are declared today** (the
registry lint's own form-1 line is the figure to trust), so the next free integer is 16 and it was
issued elsewhere. **A lane never picks — it asks.**

1. **The exact-integer distance rule.** The W1 ordinal distance is computed as an exact integer
   cross-multiply against frozen band numerators and denominators, never in floating point, so the
   band a pair falls in is bit-identical on every leg. No ADR or design-note slot states it; the
   sites that obey it are the api's `ordinal_w1` and the cube's `signed_shift_id`.
2. **The ordinal-XOR-categorical exclusivity rule.** A `TopKEntry` carries EITHER the categorical
   `field_histograms` stream OR the ordinal `ordinal_histograms` stream for a given field, never
   both, and the two are gated by the same `max_param_histograms` knob. Obeyed by the engine's
   accumulation path and asserted by `tests/engine/test_ordinal_histograms.cpp`; no slot owns it.
3. **The service-edge block, whose candidate owner explicitly refuses it.** Its authority is
   `metalog-spec`'s governance file, a DIFFERENT owner whose numbering is not citable from this
   shelf (`.claude/rules/adr-shelf-boundary.md`), so no `refs:` line can reach it — which is
   exactly the absence a law block exists to fix.

A fourth candidate was opened and CLOSED this session: the severity frontier
(*"`{ERROR, FATAL}` are never banded"*), closure-first/collapse-last, the static cell budget and
the minimal-common-collapse read are **all four stated verbatim in the specification's §16.10**,
which is an addressable owner reachable as `F-SRC-metalog-spec:SPEC.md`. No block is owed there and
the three collapse tests now cite the specification instead.

## What was drafted and NOT landed — re-derivable, not work in hand

Units 20, 21, 22 and 23 were scoped, stripped and drafted in this lane's scratchpad
(`ccc-metalog-4`), and **none of it is in the tree**. The scratchpad is disposable and everything in
it is re-derivable from `OPS-8.S3`-`S6` in under an hour per unit; treat the list below as a
statement of what was checked, not as a deliverable to recover.

* **Stripper cross-check already run on all four**, and `removed == violations` holds file by file
  for every one of the eighteen remaining files: no file in the test tier carries a suppression of
  any kind, so the equality reduces to the flat form with no kept-class subtraction.
* **Token census already taken on all four.** `NOLINT` (every spelling) 0, `clang-format off/on` 0,
  `wall-clock:` 0, `DETERMINISM-ALLOW` 0, `LOG-SEAT-ALLOW` 0, the retired-structure marker 0, SPDX
  0 across the whole remaining tier. `/*name=*/` counts: `test_stats.cpp` 10,
  `test_canonicalization_version_ruleset_coverage.cpp` 10, `test_param_histograms_compose.cpp` 9,
  `test_diff_blocks.cpp` 8, `test_compose_algebra.cpp` 6, `test_presence_churn_rank_boundary.cpp` 3,
  `test_presence_churn_property.cpp` 2, `test_presence_churn_monoid.cpp` 1. Namespace closers: one
  per file except `test_compose_algebra.cpp`, `test_diff_blocks.cpp`,
  `test_param_histograms_compose.cpp` and `test_processing_identifiers.cpp`, which carry two.
* **Drafts gated standalone at zero would-be violations** for units 20 (30 comment lines, 12
  insertions) and 21 (27 comment lines, 14 insertions), with `anchor_collide.py` clean on both.
  Units 22 and 23 are stripped but their claims scripts are not written.
* **Citation targets already verified at the artifact** for the drafted units, so a successor need
  not re-check them: `ADR-3.D4`, `ADR-20.D7`, `ADR-23.D3`, `ADR-31.D8`, `DN-42.D17`, `DN-43.D10`,
  `DN-56.D2`, `DN-56.D3`, `DN-56.D5`, `DN-56.D6`, `DN-56.D8`, `DN-56.D9`, `DN-56.O3`, `STU-3`
  (the thin-sample-floor study), `SRC-D-OTEL-21`, `SRC-D-TIR-2`,
  `F-SRC-metalog-spec:SPEC.md`, `F-SRC-metalog-spec:GOVERNANCE.md`,
  `F-SRC-insight-eidos:insight_pipeline.cpp`, `F-SRC-insight-metalog:golden.yaml`.

## Two traps this session paid for, for whoever takes the next unit

* **A `refs:` anchored on a SECTION RULER lands on the anonymous `namespace` that follows it, not
  on the test the section heads** — and `claims.py` reports zero anchor errors while it does.
  Caught by `anchor_collide.py`, which reported the bare token `namespace` resolving at three
  places in one file. Anchor a section's citation on its first `TEST`, never on the ruler.
* **The address census counts `ADR-23` and `ADR-23.D4` as different tokens**, so replacing a bare
  ADR number with the slots it names reads as a LOSS. Both such lines in unit 18 were legitimate,
  and the disposition belongs in the ledger with its evidence rather than in a restored address.

## Unit 20 — `tests/determinism/`, `tests/stats/`, `tests/metalog.test.cppm`

Three files, **245 would-be violations to 0**, comment lines **258 -> 123**. Converted by the pilot
working alone (the Founder ended lane delegation for conversion on 2026-09-06 and ruled in the same
breath that the `OPS-8.S8` cold reader is NOT what that ends — *keep the reader, kill everything
else*). Stripper cross-check exact, file by file: 173 / 61 / 11 removed, 13 tool forms kept (10
`/*name=*/` in `test_stats.cpp`, one namespace closer each).

### The five witnesses

* **Comment-only** — all three files' code token streams byte-identical to their pre-unit blob,
  re-taken after each of the three in-flight repairs below.
* **Grammar** — the unit reads **0** would-be violations; the repo moves **1 647 -> 1 402** over
  70 files, which is exactly -245.
* **Behaviour** — `malf test insight-metalog` **297 of 297 on clang-21 and 297 of 297 on gcc-16**,
  equal to the recorded baseline, under one slot acquire and one release.
* **Addressability** — the outbound census against `HEAD` exits 0: **no address lost**, 8 added
  (`STU-3`, `F-SRC-metalog-spec:SPEC.md`, `F-SRC-insight-metalog:` for `golden.yaml`,
  `determinism_fixture.cpp`, `diff.cpp`, and `salience.cpp` at both `surprise_band` and
  `novelty_band`). `registry_grammar_lint` reads **0 failures**, so every new form-3 address
  resolves for repo, file spelling and scope.
* **Knowledge** — one fresh cold reader, **38 questions, 38 recovered, 0 not recovered, 0 wrong,
  0 convictions**, `GIT COMMANDS RUN: none`, 55 tool uses, 161 k tokens, 7.8 minutes.

### What the reader actually bought, since a clean score reads like a formality

**Nothing this conversion wrote was contradicted. Two lines it wrote were OUT-ARGUED, and both were
repaired before the commit.**

* **Q38, and this is the one worth the reader's whole cost.** The line said the identification
  predicate is containment *"and never the axis kind, which is a value-shape discriminator the
  standard owns and would decay silently"* — the reason the deleted prose gave, faithfully carried.
  The reader answered from the tree that `kind` cannot separate a differential axis **at all**:
  `cube.cpp`'s `latency_shift_axis()` returns `kind = "categorical"`, identical to the stored `level`
  and `structural_role` axes. So the original argument stated the CONTINGENT reason and omitted the
  categorical one, and a reader could conclude that freezing the spec's spelling would make `kind`
  usable. It would not. Verified at `src/cube/cube.cpp` before the line was changed.
* **Q15.** The line said the producer holds *"two spellings of one wire absence"*. The reader
  observed the test passes **three** arguments and that two of them compare equal under
  `EventLevel::operator==` — three call spellings, two absences. The line now says so.
* **Q5 is a finding about the TEST, not about a converted line** (see below), and the reader
  reached it unprompted by reading `dominant_role_of`.

Six answers were materially richer than any comment in the file and are left in the code's own
reach rather than re-homed: macros cannot cross a module boundary (Q1); `event.params` is a span
over the ORIGINAL's `views` array, so a copy dangles for a second reason (Q3); `RetentionAxis` has
no `None` member and every enumerator names a reason a template WAS retained (Q8); `counts()` uses
`emplace`, which does not overwrite, so an id collision would silently SHRINK the map (Q16); each
Linux leg additionally sweeps `O3`/`O0` cells (Q18); dereferencing a disengaged `optional` is UB,
which is why the block check is `ASSERT` and not `EXPECT` (Q28).

### Contamination, disclosed by the reader and recorded rather than discounted

One repo-scoped `rg` for the scenario-header filenames, excluded only with `--glob '!build*'`,
returned **two matching lines out of this ledger** (its lines 1144 and 2530) into the reader's
context; the file was never opened and every later search was scoped or excluded. **This is exactly
the channel `OPS-8.S8` gained on this same day** — an exclusion list phrased *do not open* cannot
stop a recursive grep — and it recurred because that prompt offered the glob exclusion as an
alternative to scoping rather than requiring it. One leaked fragment concerns
`collapse_depths_scenario.hpp`, which touches Q35 and Q36; **both answers rest independently on
`SPEC.md` §16.10 / §13.6 and on `CubeAxis::operator==` being defaulted**, and the fragment asserts a
claim was FALSE, so it could only have misled. Neither answer is discounted, and the exposure is on
the record so a later reader of this entry can judge that for themselves.

### Findings

* **Kleio — `DominantRole.TieBreakByEnumValueIsStable` does not assert what its name says.** The
  fixture is {`None`:10, `GroupBegin`:10} and the assertion is `EXPECT_NE(dominant_role_of(roles),
  StructuralRole::None)`. `None` is also the seed value of `best` in `dominant_role_of`, so the
  assertion excludes *the seed* and would pass for any non-`None` return; it does not pin
  `GroupBegin`, and it does not distinguish "the tie broke upward" from "the loop ran at all".
  `EXPECT_EQ(..., StructuralRole::GroupBegin)` is the assertion the name claims. Found by the reader
  from the production source, not from the test.
* **Every unit in the test tier — the file header `// Unit tests: allow short identifiers and
  test-specific patterns.` is FALSE, on 27 files across two repos.** It claims an allowance from a
  tool that never opens the file, because the test tier is outside the clang-tidy surface
  unconditionally and by law (`LSRC-1`, on the Founder's ruling of 2026-08-31). Population measured
  2026-09-06: **15 in `insight-metalog/tests/operations/` and 12 across `insight-canon/core/tests/`**.
  Unit 20 deleted its one. **Delete the rest; never re-home one as a residual `note:`.**

### One trap this unit paid for, landed in `OPS-8.S6`

Gating the draft with `malf format --check <scratchpad dir>` instead of the invocation that step
specifies reported **55 violations, every one false**. The gate reads POST-FORMAT text and
clang-format discovers its style by walking UP from the file, so a draft outside the repo falls back
to LLVM's default `ColumnLimit: 80` against a repo that sets 100. The expensive part is the remedy
it suggests: shortening 55 claims to fit would have degraded 55 true statements to buy nothing.

## The resume point

**`OPS-8.S1` preflight, then unit 21. Everything left in this repo is ONE directory —
`tests/operations/`, 15 files, 1 402 would-be violations**, and the counts below are the gate's own
as of 2026-09-06, not an estimate:

| file | violations |
|---|---|
| `test_compose_algebra.cpp` | 439 |
| `test_golden_vectors.cpp` | 196 |
| `test_canonicalization_version_ruleset_coverage.cpp` | 136 |
| `test_presence_churn_property.cpp` | 98 |
| `test_comparison_outcome.cpp` | 98 |
| `test_stability_vs_diff_divergence.cpp` | 95 |
| `test_presence_churn_monoid.cpp` | 68 |
| `test_presence_churn_rank_boundary.cpp` | 63 |
| `test_shift_sample_floor.cpp` | 45 |
| `test_retention_profile_name.cpp` | 41 |
| `test_ruleset_identity.cpp` | 31 |
| `test_diff_blocks.cpp` | 31 |
| `test_reservoir_delta.cpp` | 27 |
| `test_param_histograms_compose.cpp` | 25 |
| `test_processing_identifiers.cpp` | 9 |

**Take it as four units, not one** — a 15-file unit is one questionnaire over 1 402 violations, and
a reader handles about fifty questions. The `presence_churn` trio (229) belongs together because the
three tests share one algebraic subject. **`test_compose_algebra.cpp` (439) is a unit on its own**,
and it is the file the `insight-eidos` warning below points at.

Take the build slot only around the behaviour witness; the reading, stripping and drafting need
none. **Arming is one `packages.yml` line away once this directory reads 0** — nothing else in the
repo carries a violation.

---

## THE UNIT-19 LANDING: WHY THE COMMIT MESSAGE SAYS WITNESS 4 IS OWED, AND WHY IT IS NOT

The commit that landed `tests/engine/` states that no cold reader ever read those nine files and
that the knowledge witness is owed. **That was true when it was written and it is not true now**,
and the record is corrected here rather than left to contradict itself two thousand lines apart.

**What happened, in order.** The lane placed the unit in the tree and took its four mechanical
witnesses; it spawned the cold reader; while the reader was running, a context checkpoint arrived
and the pilot — which could not see a reader it had not spawned — landed the working tree rather
than leave it dirty across the boundary, and said plainly in its message what it believed was
missing. The reader returned minutes later with 27 answers. The lane scored them, wrote the unit
entry above, and both landed in the same commit the pilot made, because the pilot swept the whole
working tree including the ledger.

**So all five witnesses hold, and the four the commit message lists are correctly stated there.**
The fifth is the entry above: **27 of 27 recovered, 0 not recovered, 0 wrong, 0 convictions**, one
fresh agent over 27 questions, 63 tool uses, 216 k tokens, 10.4 minutes, `GIT COMMANDS RUN: none`,
with one contamination disclosed by the reader itself and recorded rather than discounted. Its
claims table, its four deleted stale claims and its three findings are all in that entry.

**Nothing is owed for `tests/engine/` and its tagged claims are not to be treated as unverified.**
The one thing the episode did cost is the commit message, which cannot be amended in a shared
worktree and stands as written; this section is the correction, at the place a reader meets it.

**The lesson, and it is the pilot's rather than the lane's.** `OPS-8`'s opening already says a lane
is not finished when its report says so. The mirror is now measured too: **a lane is not idle
because it has not reported.** A background reader is invisible to everyone but the lane that
spawned it, so a checkpoint sweep of a lane's working tree can land a unit and simultaneously
declare a witness missing that is minutes from arriving. The cheap check is the same one that
catches the other direction — ask the lane before sweeping its tree, and if it cannot answer, say
what is unknown rather than what is missing.
