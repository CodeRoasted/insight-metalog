#!/usr/bin/env bash
# SPEC §8 clauses 1 and 4 over the digest THIS repo publishes — the producer-side half
# of "the schema is the test" (metalog-spec SPEC.md §8). Clause 4 joined at spec v0.9.0:
# it is decided in the shipped validator rather than the schema, because `maxItems` takes
# a constant while the bound is a sibling field's value. Both clauses are asked of the
# exact bytes that ship.
#
# ── Why the producer needs its own leg
# The superproject's `metalog-conformance` job judges the coderoast-hub bytes at
# `ref: main` — what a reader is exposed to RIGHT NOW. That is a real and different
# question, and it stays. What it cannot do is stop a drift: its subject only exists
# after tag → release → publish, so the public hub is the first surface to learn that
# this producer stopped matching the standard it declares it writes against.
# This script closes that ordering: golden.yaml runs it on the AGREED digest, before
# the upload that `attach-golden` publishes, so a nonconformant golden never becomes
# an artifact at all.
#
# ── Why it judges the digest and not a freshly-serialized document
# The subject has to be the bytes that ship. The agreed digest is what
# `attach-golden` attaches and what the hub evidence pipeline republishes; a document
# minted by a test would be a different artifact produced by a different harness, and
# a green over it would be truthful about something nobody reads.
#
# ── Why the subject is TWO artifact species, and what it cost to learn that
# This producer serializes two things: a MetaLogDocument and a MetaLogDiff. The standard
# ships a schema for each. Until 2026-08-24 this gate opened ONE of them: its own header
# printed `kind : metalog`, it ran over 19 documents and ZERO diffs, and
# `metalog_diff.v0.schema.json` was never applied to our output at all. Every axis it read
# really was coherent, so the green was TRUTHFUL and USELESS — the gate could not see half
# the format. That is not a hypothesis: a live §8 clause-1 violation was sitting under the
# green (`cube_diff.axes[].kind = "ordinal"`, outside the schema's closed
# `["categorical","chain"]` enum), and the reason nobody's instrument could report it is
# that no instrument had ever been pointed at a diff.
#
# So the digest now carries both species (determinism_bitidentity.sh: a diff per corpus
# section, plus the --latency-shift pair that is the only source of a `cube_diff`), and the
# verdict below is TWO validator runs over one file partitioned by a STRUCTURAL fact — a
# document carries `metalog_version`, a diff carries `diff_version`, both required by their
# own schema and absent from the other's. Not a section-name convention: a name is prose, and
# an arm located by prose is disarmed by a rename.
#
# The floors that keep this from decaying back: a partition where SOME record is neither
# species is exit 2, not a skip; a partition where EITHER count is zero is exit 2, because
# "no diffs in the digest" is precisely the blindness this closes and it must never again be
# able to read as a pass; and the diff roster is re-derived from the corpus files on disk by
# the same loop as the document roster, so a corpus section that stops emitting its diff reds
# instead of shrinking quietly.
#
# ── Why the reconciliation below runs BEFORE the verdict
# A verdict over a corpus that quietly shrank is the failure this gate exists to
# prevent, not an acceptable version of passing it. An emission that produced nothing
# leaves its `### name ###` header in place — every leg agrees, the compare passes,
# and the validator truthfully reports a smaller, entirely plausible, all-conformant
# number. So the roster is rebuilt from the corpus files on disk and the document
# count is re-derived by a second reader before anything is allowed to say CONFORMANT.
#
# ── The one thing that can red this gate with no commit in this repo
# The oracle is metalog-spec at `main` — the standard as published NOW, which is the only
# version a reader can hold this producer to, and which never carries the uniform vX.Y.Z
# tag anyway. So a schema tightened over there reds this gate here on the next run, with
# nothing changed locally. That is a TRUE red, not a flake: the reference implementation
# has genuinely stopped matching the standard. It is not automatically a fix HERE, though,
# and the validator's `[in SPEC.md]` / `[nowhere]` tag is the lead for telling the two
# apart — `nowhere` is this producer writing outside SPEC §7 `extensions`, `in SPEC.md`
# may instead be a schema lagging its own prose, which is a metalog-spec change under its
# GOVERNANCE §3. Read the section before acting on either.
#
# INTERFACE (all optional; the defaults are the desk):
#   GOLDEN      the digest to judge — DETERMINISM_OUT from determinism_bitidentity.sh
#   SPEC_ROOT   a metalog-spec checkout (SPEC.md + schema/ + conformance/)
#   CORPUS_DIR  the committed determinism corpus the roster is derived from
#
# Desk run, end to end:
#   DETERMINISM_LEG=clang DETERMINISM_OUT=/tmp/d.txt bash scripts/determinism_bitidentity.sh
#   GOLDEN=/tmp/d.txt bash scripts/spec_conformance_gate.sh
#
# EXIT CODES — the validator's own contract, carried end to end. Collapsing 2 into 1
# would price an instrument that could not run as a producer defect, and collapsing
# either into 0 is the only outcome that cannot be recovered from:
#   0  BOTH species conformant against their published schemas
#   1  at least one document or diff is schema-invalid — §8 clause 1 FAILS
#   2  this gate could not run honestly (missing subject, missing oracle, a roster or
#      count that does not reconcile, a record that is neither species, an empty species,
#      a validator whose self-test no longer bites)
# 2 dominates 1: a run that could not look is never reported as a run that looked and found
# nothing, and a second subject that could not be read never hides behind the first one's
# verdict.
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
META="$(cd "$SCRIPT_DIR/.." && pwd)"

GOLDEN="${GOLDEN:-$META/determinism-golden.txt}"
SPEC_ROOT="${SPEC_ROOT:-$META/../metalog-spec}"
CORPUS_DIR="${CORPUS_DIR:-$SCRIPT_DIR/determinism_corpus}"

die2() { echo "::error::conformance gate could not run honestly — $*" >&2; exit 2; }

# ── Preflight. Every one of these is exit 2: a missing oracle is not a clean subject,
# and CORPUS_DIR ships beside this script precisely so "absent" can never mean "skip".
[ -f "$GOLDEN" ] || die2 "no digest at GOLDEN=$GOLDEN"
[ -s "$GOLDEN" ] || die2 "the digest at $GOLDEN is empty — a gate with nothing to judge is green for the one reason that matters: it never looked"
VALIDATOR="$SPEC_ROOT/conformance/metalog_validate.py"
SCHEMA_DIR="$SPEC_ROOT/schema"
SPEC_MD="$SPEC_ROOT/SPEC.md"
[ -f "$VALIDATOR" ] || die2 "no validator at $VALIDATOR (set SPEC_ROOT to a metalog-spec checkout)"
[ -d "$SCHEMA_DIR" ] || die2 "no schema dir at $SCHEMA_DIR"
# Named one by one rather than trusted to the dir: this gate applies BOTH, and a missing diff
# schema must say which subject it cost, not just that a directory was thin.
for schema in metalog.v0.schema.json metalog_diff.v0.schema.json; do
  [ -f "$SCHEMA_DIR/$schema" ] || die2 "no $schema under $SCHEMA_DIR — this gate judges both artifact species and cannot judge one of them"
done
[ -f "$SPEC_MD" ] || die2 "no SPEC.md at $SPEC_MD"
[ -d "$CORPUS_DIR" ] || die2 "no determinism corpus at $CORPUS_DIR — the roster below is derived from it, and deriving it from nothing would pass vacuously"

echo "── subject ──"
echo "  digest : $GOLDEN  ($(wc -l <"$GOLDEN") lines, sha256 $(sha256sum "$GOLDEN" | cut -c1-16)…)"
echo "  oracle : $SPEC_ROOT  (schema/ + SPEC.md + conformance/)"
echo "  corpus : $CORPUS_DIR"
echo

# ── ARMING, first on purpose. Fixtures with hand-authored expectations, several of them
# controls the self-test refuses to run without; it prints its own counts, so they are not
# restated here to go stale. The one that decides whether this gate means anything: the digest is `### name ###` sections whose bodies are JSONL, one
# document PER LINE, and a reader that takes a section as one document validates its
# first line and reports a smaller, entirely plausible number. A failure here means
# repair the instrument — it says nothing about this producer.
echo "── arming: the instrument must be able to fail before it judges ──"
if ! python3 "$VALIDATOR" --selftest; then
  die2 "the validator's self-test did not pass; its verdict below would be worthless"
fi
echo

# ── RECONCILIATION. Two independent readers of the same file, and a roster rebuilt
# from the filesystem rather than kept by hand.
echo "── reconciliation: what is in the digest vs what the producer was asked to emit ──"

# Reader B (this one is trivially correct; the validator's sectioned-form parser is
# reader A, and --expect-documents below makes any disagreement exit 2). It counts RECORDS —
# a record is a document OR a diff; which of the two it is, is reader C's question below.
records=$(awk 'NF && $0 !~ /^### .+ ###$/' "$GOLDEN" | wc -l) || die2 "could not count records in $GOLDEN"
rc=0; sections=$(grep -cE '^### .+ ###$' "$GOLDEN") || rc=$?
[ "$rc" -le 1 ] || die2 "grep refused to read $GOLDEN (exit $rc)"
echo "  sections: $sections · records (independent count): $records"

# Every emitted section must carry at least one document. A header survives an emission
# that produced nothing, so this is the only arm that can see that shrink.
hollow=$(awk '
  /^### .+ ###$/ { if (seen && n == 0) print name; name = $0; n = 0; seen = 1; next }
  NF { n++ }
  END { if (seen && n == 0) print name }
' "$GOLDEN") || die2 "could not walk the sections of $GOLDEN"
if [ -n "$hollow" ]; then
  echo "::error::a section of the golden carries NO document — the producer emitted a header and nothing under it. The verdict below would be a truthful green over a corpus that shrank:" >&2
  printf '    hollow: %s\n' "$hollow" >&2
  exit 2
fi

# The roster, derived from the files on disk rather than listed here: a corpus file
# added tomorrow is demanded on arrival, and one that stopped emitting is a red instead
# of a smaller green. Containment, not equality — the driver's synthetic scenario
# sections (--reservoir-nearfull, --reservoir-streaming, --cube-collapse, --ngram-cap,
# --service-edges, --latency-shift) are extra sections
# by design, and demanding an exact set would red this gate the day a scenario is added.
#
# `roster_over` takes the file to check and a noun for the message, so the SAME loop runs
# over the whole digest and, further down, over the diff partition alone. One mechanism,
# two subjects: a hand-written second copy is how the second subject drifts into a weaker
# check than the first.
roster_over() {
  local subject="$1" noun="$2" missing=""
  for f in "$CORPUS_DIR"/*.log; do
    [ -e "$f" ] || die2 "no *.log under $CORPUS_DIR"
    local base; base="$(basename "$f")"
    grep -qxF "### $base ###" "$subject" || missing="$missing $base"
  done
  if [ -n "$missing" ]; then
    echo "::error::no $noun for committed corpus file(s):$missing — the digest covers less than the corpus, so any verdict over it is about a different subject than this repo believes." >&2
    echo "  sections present in $subject:" >&2
    sed -nE 's/^### (.+) ###$/    \1/p' "$subject" >&2
    exit 2
  fi
}
roster_over "$GOLDEN" "section in the golden"
echo "  roster: all $(ls -1 "$CORPUS_DIR"/*.log | wc -l) committed corpus file(s) have a section, and every section carries a record"
echo

# ── THE PARTITION (reader C). One digest, two artifact species, two schemas. The
# discriminator is a STRUCTURAL fact and not a section name: `metalog_version` is required by
# metalog.v0.schema.json and described nowhere in the diff schema; `diff_version` is required
# by metalog_diff.v0.schema.json and described nowhere in the document schema. A record
# carrying both, neither, or no JSON object at all is EXIT 2 — there is no path through this
# that skips a line, because a skip is exactly how a subject shrinks while every reading of it
# stays true.
echo "── partition: which schema each record must be judged against ──"
DOCS_PART="$(mktemp)"; DIFFS_PART="$(mktemp)"; PART_META="$(mktemp)"
trap 'rm -f "$DOCS_PART" "$DIFFS_PART" "$PART_META"' EXIT

python3 - "$GOLDEN" "$DOCS_PART" "$DIFFS_PART" "$PART_META" <<'PARTITION' || die2 "the digest could not be partitioned into its two artifact species (see above)"
import json
import sys

src, docs_path, diffs_path, meta_path = sys.argv[1:5]
HEADER = lambda t: t.startswith("### ") and t.endswith(" ###")

section = None          # the header line currently in force, or None before the first
buckets = {"metalog": [], "diff": []}
per_section = []        # (name, documents, diffs) in file order — records only, never headers


def flush():
    if section is None:
        return
    # Census the RECORDS before the header is prepended — a header is not a record, and
    # counting one as a record is precisely the arithmetic the reconciliation arm below
    # exists to refuse.
    per_section.append((section, len(buckets["metalog"]), len(buckets["diff"])))
    for kind in buckets:
        if buckets[kind]:
            # The header travels with its records, so the validator's SECTIONED parser is
            # exercised on both partitions and --expect-documents stays a real cross-reader
            # tripwire rather than a count of lines this script just wrote.
            buckets[kind].insert(0, section)


out = {"metalog": [], "diff": []}
for lineno, raw in enumerate(open(src, encoding="utf-8"), start=1):
    text = raw.strip()
    if not text:
        continue
    if HEADER(text):
        flush()
        for kind in buckets:
            out[kind].extend(buckets[kind])
            buckets[kind] = []
        section = raw.rstrip("\n")
        continue
    try:
        obj = json.loads(text)
    except json.JSONDecodeError as exc:
        sys.exit(f"::error::line {lineno} of {src} is not JSON ({exc}). A record this gate "
                 f"cannot read is a subject it cannot judge, not one it may drop.")
    if not isinstance(obj, dict):
        sys.exit(f"::error::line {lineno} of {src} is a JSON {type(obj).__name__}, not an "
                 f"object — neither artifact species.")
    is_doc = "metalog_version" in obj
    is_diff = "diff_version" in obj
    if is_doc == is_diff:
        both = "carries BOTH `metalog_version` and `diff_version`" if is_doc else \
               "carries NEITHER `metalog_version` nor `diff_version`"
        sys.exit(f"::error::line {lineno} of {src} {both}, so this gate cannot tell which "
                 f"schema decides it. Root members: {sorted(obj)}. Judging it against a "
                 f"guessed schema would fabricate either a red or a green.")
    buckets["metalog" if is_doc else "diff"].append(raw.rstrip("\n"))

flush()
for kind in buckets:
    out[kind].extend(buckets[kind])

for path, kind in ((docs_path, "metalog"), (diffs_path, "diff")):
    with open(path, "w", encoding="utf-8") as fh:
        for line in out[kind]:
            fh.write(line + "\n")

docs = sum(row[1] for row in per_section)
diffs = sum(row[2] for row in per_section)
for name, doc_count, diff_count in per_section:
    print(f"  {doc_count:>3} document(s) · {diff_count:>3} diff(s)   {name}")
with open(meta_path, "w", encoding="utf-8") as fh:
    fh.write(f"PART_DOCS={docs}\nPART_DIFFS={diffs}\n")
PARTITION

# shellcheck source=/dev/null
. "$PART_META"
docs="$PART_DOCS"
diffs="$PART_DIFFS"
echo "  totals: $docs document(s) · $diffs diff(s)"

# Reader B vs reader C. They read the same bytes by different rules, so a disagreement means
# one of them is wrong and neither verdict below is worth printing.
[ $(( docs + diffs )) -eq "$records" ] || die2 "the partition accounts for $docs + $diffs = $(( docs + diffs )) record(s) but the independent count found $records — two readers of the same file disagree"

# BOTH emptiness floors. A digest with no documents is the old failure; a digest with no
# diffs is the failure this gate was extended to stop, and it is the one that reads as
# success — every remaining axis is coherent, the count is smaller, and nothing says why.
[ "$docs" -gt 0 ] || die2 "the digest carries ZERO documents — a verdict over an empty species is green for the one reason that matters: it never looked"
[ "$diffs" -gt 0 ] || die2 "the digest carries ZERO diffs — this producer serializes MetaLogDiffs (to_json(const MetaLogDiff&), republished verbatim as \`raw[].diff\` in every Sift change report), so a digest without one puts half the format back outside this gate. Restore the diff emission in determinism_bitidentity.sh; do NOT relax this floor."

# The diff roster: the SAME derived-from-disk loop, now over the diff partition. Without it,
# six of the seven corpus sections could stop emitting their diff and the gate would still
# find a diff to judge — a smaller, entirely plausible, all-conformant number.
roster_over "$DIFFS_PART" "diff in the golden's section"
echo "  roster: every committed corpus file's section carries a diff as well as its documents"
echo

# ── THE POPULATION (DN-42.D18). A diff arm whose corpus happens to hold only 3-D borders would go
# green and be blind on exactly the shape that failed — the same SUBJECT-INCOMPLETE one level down.
# So three shapes are REQUIRED witnesses, and a missing one is exit 2 ("the corpus cannot answer"),
# never a smaller green:
#   (i)   a cube_diff carrying a DIFFERENTIAL axis — one neither input's cube declares — with at
#         least one border cell pinning it;
#   (ii)  a diff of two cubes at DIFFERENT collapse depths (§16.10 compare-at-min), the case
#         §13.6's non-normative example comment denies;
#   (iii) a cube_diff carrying `axes` and NO border at all — the vacuous-witness shape, which is
#         this producer's ORDINARY no-change output and not an edge case.
#
# The identification predicate for (i) is CONTAINMENT — an axis name in `cube_diff.axes` that
# neither input's `cube.axes` carries — and deliberately NOT the axis's `kind`. `kind` is a
# value-shape discriminator the standard owns and is itself under change; an arm keyed on today's
# spelling stops recognising the thing it guards the moment that spelling moves, which is the
# failure where the gate greens exactly when it should red.
#
# This costs the digest a structural obligation: every diff sits in a section beside the TWO
# documents it was taken from. That is what makes all three shapes derivable inside one section
# with no external oracle — and a diff without its inputs is exit 2, because a shape census that
# quietly skips what it cannot pair is a census of whatever it could see.
#
# ── AND THE FALSIFIER §13.6 actually supports. `cube_diff.axes` EQUALS both inputs' axes appears
# only in an unbolded comment inside a JSONC example and in a schema `description`; none of §13.6's
# normative bullets state it, and §16.10 mandates the very case that refutes it. So this gate
# asserts CONTAINMENT, never equality — plus the document-local consequence of a differential axis
# being emergent-at-diff: its baseline projection is uniformly mute, so a border cell pinning one
# MUST carry previous_count 0 and MUST sit under `emerging`. That is a producer defect (exit 1),
# not an instrument failure, and it is NOT SPEC §8 — it is a house invariant, fenced and labelled
# as one so no reader mistakes it for something the standard decided.
echo "── population: the shapes DN-42.D18 requires, and the §13.6 falsifier ──"
census_rc=0
python3 - "$GOLDEN" <<'CENSUS' || census_rc=$?
import json
import sys

src = sys.argv[1]
HEADER = lambda t: t.startswith("### ") and t.endswith(" ###")

sections, current, name = [], None, None
for lineno, raw in enumerate(open(src, encoding="utf-8"), start=1):
    text = raw.strip()
    if not text:
        continue
    if HEADER(text):
        name = text[4:-4]
        current = {"name": name, "docs": [], "diffs": []}
        sections.append(current)
        continue
    if current is None:
        sys.exit(f"::error::{src} carries a record before its first `### name ###` header, so no "
                 f"record can be attributed to a section")
    # The partition above already refused an unparsable line, so this cannot normally raise. It is
    # guarded anyway because an UNCAUGHT exception here would exit 1, and this block's 1 means
    # "producer defect" — the one number a broken instrument must never be able to hand back.
    try:
        obj = json.loads(text)
    except json.JSONDecodeError as exc:
        print(f"::error::the shape census could not read line {lineno} of {src} ({exc})",
              file=sys.stderr)
        sys.exit(2)
    current["diffs" if "diff_version" in obj else "docs"].append(obj)


def axis_names(document):
    cube = document.get("cube")
    return {axis["name"] for axis in cube["axes"]} if cube else set()


def border_cells(cube_diff):
    for region_name in ("emerging", "vanishing"):
        region = cube_diff.get(region_name)
        if not region:
            continue
        for side in ("lower", "upper"):
            for cell in region.get(side, []):
                yield region_name, side, cell


witnesses = {"differential-axis": [], "mixed-depth": [], "vacuous": []}
violations = []
unpaired = []

for section in sections:
    if not section["diffs"]:
        continue
    if len(section["docs"]) != 2:
        unpaired.append(f"{section['name']} ({len(section['docs'])} document(s), "
                        f"{len(section['diffs'])} diff(s))")
        continue
    previous, current_doc = section["docs"]
    stored = axis_names(previous) | axis_names(current_doc)
    previous_axes = (previous.get("cube") or {}).get("axes")
    current_axes = (current_doc.get("cube") or {}).get("axes")
    for delta in section["diffs"]:
        cube_diff = delta.get("cube_diff")
        if cube_diff is None:
            continue
        # (ii) BOTH sides must carry a cube whose axes DIFFER. "one side has no cube" also makes
        # the two unequal and is not the same fact at all — it is a pair the compare-at-min never
        # ran on, and counting it here would witness the shape with a section that cannot show it.
        if previous_axes and current_axes and previous_axes != current_axes:
            witnesses["mixed-depth"].append(section["name"])
        if "emerging" not in cube_diff and "vanishing" not in cube_diff:
            witnesses["vacuous"].append(section["name"])
        differential = {axis["name"] for axis in cube_diff["axes"]} - stored
        if not differential:
            continue
        pinned = 0
        for region_name, side, cell in border_cells(cube_diff):
            hit = differential & set(cell.get("coord", {}))
            if not hit:
                continue
            pinned += 1
            if region_name != "emerging":
                violations.append(
                    f"{section['name']}: a cell pinning {sorted(hit)} sits under `{region_name}` "
                    f"({side}); a differential axis has no stored-cube domain, so it can only "
                    f"ever EMERGE")
            if cell.get("previous_count") != 0:
                violations.append(
                    f"{section['name']}: a cell pinning {sorted(hit)} carries previous_count="
                    f"{cell.get('previous_count')!r}; the axis's baseline projection is uniformly "
                    f"mute, so 0 is the only value it can hold")
        if pinned:
            witnesses["differential-axis"].append(section["name"])

for shape in ("differential-axis", "mixed-depth", "vacuous"):
    found = witnesses[shape]
    mark = "ok " if found else "NONE"
    listed = ", ".join(sorted(set(found))) if found else "— no section in this digest produces it"
    print(f"  [{mark}] {shape:<18} {len(set(found))} witness(es): {listed}")

rc = 0
if unpaired:
    print("::error::a diff in this digest does not sit beside the TWO documents it was taken "
          "from, so its shape cannot be derived and this census would be a census of whatever it "
          "could pair:", file=sys.stderr)
    for row in unpaired:
        print(f"    {row}", file=sys.stderr)
    rc = 2
empty = [shape for shape in witnesses if not witnesses[shape]]
if empty:
    print(f"::error::the diff corpus carries NO witness for {sorted(empty)}. The gate refuses to "
          f"pass: an arm that has never seen a shape is silent about it, and DN-42.D18 makes these "
          f"three a precondition rather than a hope. Add the emitting scenario in "
          f"determinism_bitidentity.sh; do NOT drop the requirement.", file=sys.stderr)
    rc = 2
if violations:
    print("::error::§13.6 differential-axis falsifier — a border cell contradicts the axis's own "
          "emergent-at-diff construction (this is a PRODUCER defect, and it is NOT SPEC §8):",
          file=sys.stderr)
    for row in violations:
        print(f"    {row}", file=sys.stderr)
    rc = max(rc, 1)

sys.exit(rc)
CENSUS

case "$census_rc" in
  0) echo "  falsifier: every border cell pinning a differential axis emerges from previous_count 0" ;;
  1) : ;;                          # a producer defect; folded into the verdict below
  *) exit "$census_rc" ;;          # the corpus cannot answer — never a verdict
esac
echo

# ── THE VERDICT. Two runs, one per species. --expect-documents is the tripwire between
# readers: if the validator's parser sees a different number than the partition above, it
# exits 2 rather than judging whatever subset it could see.
echo "── SPEC §8 clauses 1 and 4 over the agreed golden ──"
rc_docs=0
python3 "$VALIDATOR" \
  --schema-dir "$SCHEMA_DIR" \
  --spec "$SPEC_MD" \
  --kind metalog \
  --expect-documents "$docs" \
  "$DOCS_PART" || rc_docs=$?
echo
rc_diffs=0
python3 "$VALIDATOR" \
  --schema-dir "$SCHEMA_DIR" \
  --spec "$SPEC_MD" \
  --kind diff \
  --expect-documents "$diffs" \
  "$DIFFS_PART" || rc_diffs=$?

# The verdict is COMPUTED FROM the two runs, never printed after them: a summary line that
# can disagree with its own findings is not a check. 2 dominates 1 dominates 0.
rc=0
if [ "$rc_docs" -gt 1 ] || [ "$rc_diffs" -gt 1 ]; then rc=2
elif [ "$rc_docs" -eq 1 ] || [ "$rc_diffs" -eq 1 ] || [ "$census_rc" -eq 1 ]; then rc=1
fi

echo
echo "── verdict ──"
printf '  documents (%s, schema/metalog.v0.schema.json)      exit %s\n' "$docs" "$rc_docs"
printf '  diffs     (%s, schema/metalog_diff.v0.schema.json) exit %s\n' "$diffs" "$rc_diffs"
printf '  §13.6 falsifier (house invariant, not SPEC §8)     exit %s\n' "$census_rc"

case "$rc" in
  0)
    echo
    echo "PASS: BOTH species of this producer's published digest are conformant with SPEC §8"
    echo "  clauses 1 and 4."
    echo "  READ IT NARROWLY. §8 has four clauses and this covers two. Clause 2 (every"
    echo "  required field populated per its definition) is covered only where the schema"
    echo "  can express it; clause 3 (template_id computed per §3.2) has no pinned"
    echo "  cross-implementation vector to check against. Clause 4 carries its own limit:"
    echo "  it checks the caps the documents DECLARE, so a cap this producer stops"
    echo "  declaring leaves its array unchecked and the green says nothing about it."
    echo "  \`format\` is an annotation here, not an assertion — asserting it would be"
    echo "  stricter than the clauses this names."
    echo "  And it reaches only the records the digest CARRIES: both rosters above are"
    echo "  derived from the corpus on disk, but a code path no section drives is a path"
    echo "  this gate has still never opened."
    ;;
  1)
    echo "::error::this producer's digest does NOT validate against the published MetaLog schemas — see the path/keyword/member list above, and the per-species exit codes for which artifact failed. A member reported [nowhere] is this producer extending outside SPEC §7 \`extensions\`, which is a fix here; a member reported [in SPEC.md] may instead be a schema that lags its own prose, which is a metalog-spec change under its GOVERNANCE §3. The tag is a lead, not a verdict — open the section before acting on it." >&2
    ;;
  *)
    echo "::error::the conformance validator could not run honestly (documents exit $rc_docs, diffs exit $rc_diffs) — repair the instrument or the subject. This is NOT a clean result and must never be read as one." >&2
    ;;
esac
exit "$rc"
