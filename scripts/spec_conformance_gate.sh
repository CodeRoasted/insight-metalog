#!/usr/bin/env bash
# SPEC §8 clause 1 over the digest THIS repo publishes — the producer-side half of
# "the schema is the test" (metalog-spec SPEC.md §8).
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
#   0  conformant against the published schema
#   1  at least one document is schema-invalid — §8 clause 1 FAILS
#   2  this gate could not run honestly (missing subject, missing oracle, a roster or
#      count that does not reconcile, a validator whose self-test no longer bites)
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
[ -f "$SPEC_MD" ] || die2 "no SPEC.md at $SPEC_MD"
[ -d "$CORPUS_DIR" ] || die2 "no determinism corpus at $CORPUS_DIR — the roster below is derived from it, and deriving it from nothing would pass vacuously"

echo "── subject ──"
echo "  digest : $GOLDEN  ($(wc -l <"$GOLDEN") lines, sha256 $(sha256sum "$GOLDEN" | cut -c1-16)…)"
echo "  oracle : $SPEC_ROOT  (schema/ + SPEC.md + conformance/)"
echo "  corpus : $CORPUS_DIR"
echo

# ── ARMING, first on purpose. Twelve fixtures with hand-authored expectations, four of
# them controls the self-test refuses to run without. The one that decides whether this
# gate means anything: the digest is `### name ###` sections whose bodies are JSONL, one
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
# reader A, and --expect-documents below makes any disagreement exit 2).
docs=$(awk 'NF && $0 !~ /^### .+ ###$/' "$GOLDEN" | wc -l) || die2 "could not count documents in $GOLDEN"
rc=0; sections=$(grep -cE '^### .+ ###$' "$GOLDEN") || rc=$?
[ "$rc" -le 1 ] || die2 "grep refused to read $GOLDEN (exit $rc)"
echo "  sections: $sections · documents (independent count): $docs"

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
# sections (--reservoir-nearfull, --reservoir-streaming, --cube-collapse, --service-edges) are extra sections
# by design, and demanding an exact set would red this gate the day a scenario is added.
missing=""
for f in "$CORPUS_DIR"/*.log; do
  [ -e "$f" ] || die2 "no *.log under $CORPUS_DIR"
  base="$(basename "$f")"
  grep -qxF "### $base ###" "$GOLDEN" || missing="$missing $base"
done
if [ -n "$missing" ]; then
  echo "::error::the golden has no section for committed corpus file(s):$missing — the digest covers less than the corpus, so any verdict over it is about a different subject than this repo believes." >&2
  echo "  sections present:" >&2
  sed -nE 's/^### (.+) ###$/    \1/p' "$GOLDEN" >&2
  exit 2
fi
echo "  roster: all $(ls -1 "$CORPUS_DIR"/*.log | wc -l) committed corpus file(s) have a section, and every section carries a document"
echo

# ── THE VERDICT. --expect-documents is the tripwire between the two readers: if the
# validator's parser sees a different number than the count above, it exits 2 rather
# than judging whatever subset it could see.
echo "── SPEC §8 clause 1 over the agreed golden ──"
rc=0
python3 "$VALIDATOR" \
  --schema-dir "$SCHEMA_DIR" \
  --spec "$SPEC_MD" \
  --expect-documents "$docs" \
  "$GOLDEN" || rc=$?

case "$rc" in
  0)
    echo
    echo "PASS: this producer's published digest is conformant with SPEC §8 clause 1."
    echo "  READ IT NARROWLY. §8 has four clauses and this is the first. Clause 2 (every"
    echo "  required field populated per its definition) is covered only where the schema"
    echo "  can express it; clause 3 (template_id computed per §3.2) has no pinned"
    echo "  cross-implementation vector to check against; clause 4 (top_k truthfully"
    echo "  bounded at top_k_size) is not checked at all. \`format\` is an annotation here,"
    echo "  not an assertion — asserting it would be stricter than the clause this names."
    ;;
  1)
    echo "::error::this producer's digest does NOT validate against the published MetaLog schema — see the path/keyword/member list above. A member reported [nowhere] is this producer extending outside SPEC §7 \`extensions\`, which is a fix here; a member reported [in SPEC.md] may instead be a schema that lags its own prose, which is a metalog-spec change under its GOVERNANCE §3. The tag is a lead, not a verdict — open the section before acting on it." >&2
    ;;
  *)
    echo "::error::the conformance validator could not run honestly (exit $rc) — repair the instrument or the subject. This is NOT a clean result and must never be read as one." >&2
    ;;
esac
exit "$rc"
