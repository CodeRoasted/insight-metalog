#!/usr/bin/env bash
# The roster guard — the mechanical half of "a section list written twice is not a list".
#
# ── What went wrong, so the guard is read as a repair and not as ceremony
# The determinism digest's synthetic sections were enumerated in TWO emitters:
# scripts/determinism_bitidentity.sh (the four Linux legs) and .github/workflows/golden.yaml's
# PowerShell (the MSVC leg). On 2026-08-24, --latency-shift and --collapse-depths landed in the
# fixture and the bash driver (1b4f7b4) and the PowerShell copy was not touched: 8 section kinds on
# one side, 6 on the other, with golden.yaml's compare demanding FIVE byte-identical legs. Nothing
# reported it, because nothing was asking. This repo's gate runs at the tag, so the first reader of
# that divergence would have been a release ceremony, on the one leg a Linux desk cannot reproduce.
#
# scripts/determinism_sections.txt removed the second copy: both emitters read it. That leaves
# exactly two seams a future edit can still open, and this script is what stands in them:
#
#   1. The roster and the FIXTURE's argument dispatch can disagree — a scenario compiled into
#      determinism_fixture.cpp with no roster row is a code path no leg ever replays (a silent loss
#      of coverage), and a roster row with no fixture branch is a section whose body is the
#      fixture's usage error (a loud one, but at the tag). Checked in BOTH directions.
#   2. An emitter can quietly stop consuming the roster, or grow a hand-written `### --… ###`
#      header beside it — which is the original defect, re-created. Checked by asserting each
#      emitter names the roster file and contains no literal synthetic header.
#
# It deliberately does NOT try to prove the two emitters agree by reading their code: that question
# is answered by construction now (one source, read by both), and a checker that re-implements the
# comparison would become a third copy of the thing it guards.
#
# USAGE:  bash scripts/determinism_sections_check.sh        (from anywhere; paths are repo-anchored)
# EXIT:   0 = the roster, the fixture and both emitters agree
#         1 = a real divergence — fix it before the tag; the message names the identifier
#         2 = the guard could not run honestly (a subject file is missing) — never a green
# Runs at the desk with no build and no toolchain, which is the point: golden.yaml gates the whole
# five-leg matrix behind it, so a divergence costs seconds instead of a metered MSVC build.
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
META="$(cd "$SCRIPT_DIR/.." && pwd)"

ROSTER="$META/scripts/determinism_sections.txt"
FIXTURE="$META/scripts/determinism_fixture.cpp"
EMITTERS=(
  "$META/scripts/determinism_bitidentity.sh"
  "$META/.github/workflows/golden.yaml"
)

die2() { echo "::error::$*" >&2; exit 2; }
rc=0
fail() { echo "::error::$*" >&2; rc=1; }

for f in "$ROSTER" "$FIXTURE" "${EMITTERS[@]}"; do
  [ -f "$f" ] || die2 "missing subject: $f — this guard cannot answer its question without it"
done

# ── The roster, parsed exactly as the emitters parse it (strip trailing space, drop # and blanks).
mapfile -t ROWS < <(sed -E 's/[[:space:]]+$//' "$ROSTER" | grep -vE '^([[:space:]]*#|[[:space:]]*$)')
[ "${#ROWS[@]}" -gt 0 ] || die2 "$ROSTER lists no section — every digest would carry the corpus only"

roster_flags=()
for row in "${ROWS[@]}"; do
  flag="${row%% *}"
  case "$flag" in
    --*) ;;
    *) fail "roster row '$row' does not start with a fixture flag (expected '--something <annotation>')" ;;
  esac
  [ "$flag" != "$row" ] || fail "roster row '$row' has no annotation — the row IS the header text, so a bare flag makes an unreadable section header"
  roster_flags+=("$flag")
done

dups="$(printf '%s\n' "${roster_flags[@]}" | LC_ALL=C sort | uniq -d)"
[ -z "$dups" ] || fail "roster lists the same flag twice:$(printf ' %s' $dups) — the digest would carry the section twice, and the duplicate is invisible in a byte compare until a leg drops one"

# ── Seam 1: the roster vs the fixture's own dispatch, both directions.
# The dispatch is the compiled truth — `std::string{argv[1]} == "--flag"` in the fixture's main().
mapfile -t fixture_flags < <(grep -oE 'argv\[1\][^=]*== *"--[a-z0-9-]+"' "$FIXTURE" |
  grep -oE -- '--[a-z0-9-]+' | LC_ALL=C sort -u)
[ "${#fixture_flags[@]}" -gt 0 ] ||
  die2 "found no 'argv[1] == \"--flag\"' dispatch in $FIXTURE — either the fixture stopped taking flags or this guard's pattern went stale; both make its verdict meaningless"

missing_row="$(comm -23 <(printf '%s\n' "${fixture_flags[@]}") <(printf '%s\n' "${roster_flags[@]}" | LC_ALL=C sort -u))"
missing_branch="$(comm -13 <(printf '%s\n' "${fixture_flags[@]}") <(printf '%s\n' "${roster_flags[@]}" | LC_ALL=C sort -u))"
[ -z "$missing_row" ] ||
  fail "the fixture dispatches$(printf ' %s' $missing_row) and the roster does not list it — that scenario is compiled, and NO leg replays it. Add the row to $ROSTER (emission order is digest order)."
[ -z "$missing_branch" ] ||
  fail "the roster lists$(printf ' %s' $missing_branch) and the fixture has no branch for it — every leg would emit the header and the fixture's usage error under it. Add the branch to $FIXTURE, or drop the row."

# ── Seam 1b: the fixture's own usage string is a third hand-kept copy of the same list.
# It is only stderr, but a usage line that omits a live flag is the same defect at a smaller scale.
usage="$(sed -nE '/usage: determinism_fixture/,/\\n";/p' "$FIXTURE")"
[ -n "$usage" ] || die2 "no 'usage: determinism_fixture' string in $FIXTURE — this guard checks it against the roster and cannot find it"
for flag in "${roster_flags[@]}"; do
  grep -qF -- "$flag" <<<"$usage" ||
    fail "the fixture's usage string does not name $flag — it is a hand-kept copy of the roster and it has drifted"
done

# ── Seam 2: every emitter must READ the roster and enumerate nothing itself.
for emitter in "${EMITTERS[@]}"; do
  name="${emitter#"$META"/}"
  grep -qF 'determinism_sections.txt' "$emitter" ||
    fail "$name does not read determinism_sections.txt — an emitter that stopped consuming the roster is back to carrying its own list, which is the 2026-08-24 defect"
  hand="$(grep -nE '###[[:space:]]+--[a-z]' "$emitter" || true)"
  [ -z "$hand" ] ||
    fail "$name writes a synthetic section header by hand — that is the defect this roster exists to remove:
$(sed 's/^/    /' <<<"$hand")"
done

if [ $rc -eq 0 ]; then
  echo "roster OK: ${#roster_flags[@]} synthetic section(s), the fixture dispatches exactly those, and both emitters read $ROSTER"
  printf '  %s\n' "${ROWS[@]}"
fi
exit $rc
