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
