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
