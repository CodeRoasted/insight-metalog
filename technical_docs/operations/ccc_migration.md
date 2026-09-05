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
Transcript checked: `GIT COMMANDS RUN: none`. **Score: 12 of 12 recovered, 0 not recovered, 0
wrong**, every answer at high confidence.

Four answers went past the prose they replaced. Q2 recovered the non-associativity ruling AND named
the test that asserts the scope-dependence from both sides — the divergence under a binding cap and
the equality when none binds — which is the evidence the old prose only referred to. Q3 derived
that dropping the residual bucket over-states concentration while attributing it invents an
attribution, from the arithmetic rather than from the comment. Q7 found `STU-3.A1`'s pre-registered
scan, its three measured null rates and both guard tests, and then bounded the claim correctly: the
number was picked against one binding shape, a bimodal cache. Q10 confirmed both point-lookup maps
are never iterated into content and named the sorted output that makes each safe.

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

**Witnesses.** Comment-only: both files, code token stream byte-identical to `HEAD`. Grammar:
`malf format --check` over the unit — 154 comment lines, forms `pre=1 post=25 invariant=10
assert=11 note=44 refs=23 continuation=34 tool=6`, **0 would-be violations**; the kept suppression
still sits directly under its `note:` and directly above its target. Comment lines 424 → 154 (64 %
fewer); would-be violations 419 → 0.

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
those. Numbering is dense and append-only, so **a lane asks the pilot and never picks** — and the
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

**Nothing else in the converted surface owes a block.** `api/` and the test tier are unread for
declaring `SRC-<code>` sites and may owe more; that read is the next lane's first step.

---

# The `SRC-<code>` census of this repo — for the pilot's cross-repo repointing pass (`OPS-8.O5`)

Recorded because `OPS-8.O5` makes the citer list the lane's deliverable and the repointing itself
the pilot's single cross-repo pass. Measured 2026-09-06 over `api/ src/ scripts/ benchmarks/
tests/ test_package/` with `registry_grammar_lint`'s own decider pattern: **26 distinct codes,
169 occurrences.**

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
codes by this run and must be before the `api/` unit converts.

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

---

# Where this run stopped, and why

**Nine units converted, one law minted, the repo NOT armed.** Arming (`OPS-8.S12`) requires the whole repo at zero,
which this run does not reach, so `comment_contract: true` is **not** set and the CCC phase still
counts `insight-metalog` rather than failing it.

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
| | **total** | **14** | **1 530** | **1 550 → 607 (61 % fewer)** | **75 of 75 recovered, 0 not recovered** |

**1 530 of the repo's 6 701 would-be violations, 22.8 %, in nine commits.** Every claim held for a
reader was recovered — **45 of 45, 0 not recovered** — so nothing had to be re-homed above the
comment rung. **Two lines this conversion itself wrote were found defective by the readers and
corrected before their commits**: the `observability only` note in unit 3, which was false, the
`assert:` in unit 5, which dropped the qualifier its premise rested on, and the `open_window`
`invariant:` in unit 7, whose universal *"every"* was false for three members. **Three lines, all
found by the readers, none by a gate.** **One defect this lane filed
was withdrawn** after unit 2's reader found the figure's authority in the published spec. Every unit comment-only
against `HEAD` by code-token-stream equality, every unit at zero would-be violations under `malf
format --check`, and `malf test insight-metalog` **297 of 297 on clang-21 and 297 of 297 on
gcc-16** after conversion, equal to the baseline taken before the first unit.

**The repo's own gate reading after the run, verbatim, and it matches the per-unit arithmetic
exactly** — 6 868 - 1 097 + 427 = 6 198 comment lines, 6 701 - 1 083 = 5 618 would-be violations:

```
malf format: CCC SUMMARY · mode=check-paths · files 70 = armed 0 + report-only 70 + NOT CHECKED 0 ·
armed repos: none · comment lines 6198 · forms pre=7 post=73 invariant=38 assert=35 note=94 refs=63
continuation=96 tool=174 · violations in armed files 0 (none) · would-be violations in report-only
files 5618 (bare=4935 tag-mid-line=1 slash3=28 spacer=347 ruler=3 trailing=299
suppression-without-why=5) · rc=0
```

Tool forms went 167 → **174** and `suppression-without-why` 13 → **5**: seven directives this run
re-homed under their own `note:` moved out of the violation class into the recognised tool forms,
and one was deleted with the measurement that it silences nothing (its check family is disabled in
the one shared `.clang-tidy`). The five that remain are all in units this run did not convert.
**Zero law blocks** — one is owed and is recorded
above for the pilot to number.

**What remains, in the order a next lane should take it.** Source: `src/operations/` (compose 223 + diff 196) 419 · `src/serialization/serialize.cpp` 326 ·
`src/stats/wire_format.cpp` 28 (**blocked on the law number**) · `api/` 1 414 (**must be read for
statement-bearing `SRC-<code>` sites first — 73 declaring-position occurrences across its two
files**). Harness: `scripts/` 545 · `benchmarks/` 241 · `test_package/` 16. Test tier:
`tests/operations` 1 402 · `tests/engine` 268 · `tests/serialization` 252 · `tests/reservoir` 244 ·
`tests/cube` 218 · `tests/determinism` 173 · `tests/stats` 61 · `tests/metalog.test.cppm` 11.

**Declared departures from `OPS-8` in this run.**

* **The build slot was held per build, not for the whole run** (`OPS-8.S1.1`, and the pilot's brief
  instructed it): four CCC lanes shared one global slot in this session. Three acquisitions, 4
  minutes blocked in total.
* **The behaviour witness is per BATCH, one per slot acquisition** — `OPS-8.S7.4` as amended on
  2026-09-06, with this lane's 40-minute block as the measurement that forced it. **The grain, named
  so a later reader does not assume a finer one:**

  | behaviour witness | slot acquired | units it covers | result |
  |---|---|---|---|
  | baseline | 2026-09-05 23:51 | none — the pre-conversion reference | 297 of 297 clang-21, 297 of 297 gcc-16 |
  | batch A | 2026-09-06 00:16 | units 1-2 | 297 of 297 clang-21; the gcc leg's provenance is void — a sibling reclaimed the slot mid-leg (see verdict item 1) |
  | batch B | 2026-09-06 00:30 | units 1-4, all four in the tree | 297 of 297 clang-21, 297 of 297 gcc-16 |
  | batch C | 2026-09-06 00:55 | units 5-7 | 297 of 297 clang-21, 297 of 297 gcc-16 |
| batch D | **owed** | units 8-9 | NOT TAKEN — the slot was held continuously by sibling lanes from 01:09 |

  **Batch D is OWED and this ledger says so rather than implying it was taken.** Units 8 and 9
  carry three of the four witnesses — comment-only, grammar at zero, and readers at 6 of 6 and 12
  of 12 — and the fourth is a `malf test insight-metalog` on both toolchains at the next
  acquisition. They are landed rather than held back because a shared worktree with uncommitted
  work is the state this programme does not tolerate, and the batched rule puts the witness AFTER
  the landing by construction. If batch D reds, bisect by unit.

  Batch B supersedes batch A: it re-ran both legs with units 1-2 still in the tree, so nothing
  rests on the leg whose provenance was void. Batch C was taken at a slot acquired in the
  FOREGROUND, whose stamp read `anchor 3053 … ALIVE` before the release, so its provenance is
  clean. Detection is unaffected by the batching: witness 1 proves each file's code token stream
  byte-identical to `HEAD`, so a comment-only unit can reach behaviour through `__LINE__` and
  nothing else. Two comment-only repairs landed AFTER their batch — unit 5's dropped qualifier and
  unit 7's false `invariant:` — and each was re-witnessed against `OPS-8.S7` steps 2 and 3.
* **Units 5 and 6 landed in one commit** (`OPS-8.S10`'s one-commit-per-unit), 47 violations between
  them. The ledger keeps a separate entry per unit.
* **Two directories were split by file group** (`OPS-8.S2` allows it): `src/stats/` because
  `wire_format.cpp` is blocked on a law number, and `src/cube/` because 383 violations in one
  questionnaire is two interrogations pretending to be one.
