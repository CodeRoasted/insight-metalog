#!/usr/bin/env bash
# The section-writer guard — proves scripts/determinism_emit.sh emits the digest's exact byte shape
# AND that a failing fixture reds the run, against a STUB binary whose exit code this script chooses.
#
# ── The defect it stands in
# Until 2026-09-04 the Linux driver ran the fixture as `"$bin" "$arg" >>out 2>/dev/null` under
# `set -uo pipefail`: no `-e`, no `||`, no `$?`. The fixture's exit status was discarded. Its two
# error paths (the usage error on an unknown flag, and "cannot open" on an unreadable corpus file)
# both exit 2 after writing to stderr, so either produced an EMPTY body under a correctly-spelled
# `### … ###` header, in every one of the four -O/-ffp cells identically. `cmp` then called the empty
# bodies IDENTICAL, the driver printed PASS naming every section, and it wrote the truncated digest
# to DETERMINISM_OUT for golden.yaml's five-leg compare. golden.yaml's MSVC leg has always thrown on
# the same condition (`Add-Section`: `if ($p.ExitCode -ne 0) { throw … }`), so the one gate's legs
# disagreed about what a failure is, and the only reader of a broken fixture was a metered Windows
# build at a tag.
#
# ── Why a stub and not the real fixture
# Reaching the real emit loop costs a four-cell conan module-tower build under `import std`
# (~2-3 GB per compile). A gate nobody can run at the desk is a gate nobody can prove reds — the
# reason scripts/determinism_sections_check.sh and scripts/spec_conformance_gate.sh are committed
# scripts too. det_emit_section and det_emit_digest read no roster, no directory and no environment
# variable; they are handed a binary path and two lists. That is what makes substituting a stub an
# honest test of the real code path rather than a re-implementation of it.
#
# ── Arms (LOCAL ordinals — they number THIS instrument's arms and mean nothing outside it)
#   G1  byte shape: `### <header> ###\n` + the fixture's raw stdout, corpus rows then synthetic rows,
#       each list in the order given. Compared against an independently written expected file.
#   G2  G1 is not vacuous on ORDER: a permuted input must change the bytes (the digest is compared
#       across five legs, so emission order is load-bearing, not cosmetic).
#   G3  PROPAGATION — the subject: a fixture exiting non-zero makes det_emit_section AND
#       det_emit_digest return that same status, and det_emit_digest stops at the failing section
#       instead of writing the rest (fail-fast, matching the MSVC leg's throw).
#   G4  the diagnostic names the section header and the exit status — verbose-on-failure, so a CI
#       log localizes the fault without a re-run.
#   G5  the fixture's stderr NEVER enters the digest, on either the passing or the failing path.
#   G6  STRUCTURAL: the Linux driver actually routes through this writer and guards its return.
#       Structural rather than behavioural on purpose, and this is its declared limit: the driver's
#       own body cannot be reached without the tower build this script exists to avoid.
#   G7  STRUCTURAL: the MSVC leg still refuses a non-zero exit. G3 makes the Linux leg agree with the
#       Windows leg; nothing else would notice the Windows half being deleted, and one leg silently
#       relaxing is exactly how the two came to disagree in the first place.
#   G8  STRUCTURAL: the writer names no section itself — it is handed its header text. Extraction
#       added one new place a section name could be hand-typed, and that is the defect
#       scripts/determinism_sections_check.sh refuses in the two enumerators.
#
# USAGE:  bash scripts/determinism_emit_check.sh        (from anywhere; paths are repo-anchored)
# EXIT:   0 = the writer emits the right bytes and reds on a failing fixture
#         1 = a real divergence — the message names the arm and prints actual vs expected
#         2 = the guard could not run honestly (a subject file is missing) — never a green
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
META="$(cd "$SCRIPT_DIR/.." && pwd)"

EMIT_LIB="$SCRIPT_DIR/determinism_emit.sh"
DRIVER="$SCRIPT_DIR/determinism_bitidentity.sh"
WORKFLOW="$META/.github/workflows/golden.yaml"

die2() { echo "::error::$*" >&2; exit 2; }
rc=0
fail() { echo "::error::$*" >&2; rc=1; }

for f in "$EMIT_LIB" "$DRIVER" "$WORKFLOW"; do
    [ -f "$f" ] || die2 "missing subject: $f — this guard cannot answer its question without it"
done

# shellcheck source=determinism_emit.sh
. "$EMIT_LIB"
for fn in det_emit_section det_emit_digest; do
    declare -F "$fn" >/dev/null ||
        die2 "$EMIT_LIB defines no $fn — either the writer was renamed or this guard went stale; both make its verdict meaningless"
done

WORK="$(mktemp -d)" || die2 "cannot create a work directory"
trap 'rm -rf "$WORK"' EXIT

# ── The stub fixture. Deterministic bytes per argument, no clock, no randomness, no environment
# read beyond the two knobs this script sets. STUB_FAIL_ARG selects the ONE argument it fails on, so
# a run can place the failure at a chosen position in the emission order (G3 checks that the rows
# AFTER it are not written). STUB_STDERR makes it write a diagnostic without failing (G5).
STUB="$WORK/stub_fixture"
cat >"$STUB" <<'STUB_EOF'
#!/usr/bin/env bash
[ -n "${STUB_STDERR:-}" ] && printf 'stub diagnostic for %s\n' "$1" >&2
if [ "${STUB_FAIL_ARG:-}" = "$1" ]; then
    printf 'stub usage error\n' >&2
    exit "${STUB_FAIL_CODE:-2}"
fi
printf 'BODY(%s)\nline2\n' "$1"
STUB_EOF
chmod +x "$STUB"

CORPUS_DIR="$WORK/corpus"
mkdir -p "$CORPUS_DIR"
: >"$CORPUS_DIR/alpha.log"
: >"$CORPUS_DIR/beta.log"
CORPUS="$CORPUS_DIR/alpha.log
$CORPUS_DIR/beta.log"
SECTIONS=("--first (annotation with spaces)" "--second (another)")

# ── G1 — the byte shape, against an expected file written out by hand rather than by the code
# under test. `printf`, not `echo`: the expectation must be built by a different mechanism than the
# writer's, or the arm compares the writer with itself.
expected="$WORK/expected"
{
    printf '### alpha.log ###\n'; printf 'BODY(%s)\nline2\n' "$CORPUS_DIR/alpha.log"
    printf '### beta.log ###\n';  printf 'BODY(%s)\nline2\n' "$CORPUS_DIR/beta.log"
    printf '### --first (annotation with spaces) ###\n'; printf 'BODY(--first)\nline2\n'
    printf '### --second (another) ###\n';               printf 'BODY(--second)\nline2\n'
} >"$expected"

actual="$WORK/actual"
g1rc=0
det_emit_digest "$STUB" "$actual" "$CORPUS" "${SECTIONS[@]}" || g1rc=$?
if [ "$g1rc" -ne 0 ]; then
    fail "G1: det_emit_digest returned $g1rc on a stub that exits 0 — a passing fixture must emit cleanly"
elif ! cmp -s "$expected" "$actual"; then
    fail "G1: the emitted digest is not the expected byte shape.
$(diff -u "$expected" "$actual" | sed 's/^/    /')"
fi

# ── G2 — G1 is not vacuous on order. Five legs are compared byte for byte, so a permuted emission is
# a divergence; if a permutation produced the same bytes, G1 would be pinning a set, not a sequence.
permuted="$WORK/permuted"
det_emit_digest "$STUB" "$permuted" "$CORPUS" "${SECTIONS[1]}" "${SECTIONS[0]}" || true
cmp -s "$actual" "$permuted" &&
    fail "G2: swapping two synthetic rows produced identical bytes — the emitter is not order-sensitive, so G1 proves nothing about emission order"

# ── G3 — PROPAGATION. The fixture fails on the SECOND synthetic row, i.e. the last position, and
# then on the FIRST corpus row, i.e. the first position: the first placement proves the status
# survives, the second proves the walk stops rather than emitting the remaining three headers.
sec_out="$WORK/section_only"
: >"$sec_out"
s3rc=0
STUB_FAIL_ARG="--second" STUB_FAIL_CODE=7 \
    det_emit_section "$STUB" "$sec_out" "--second (another)" "--second" 2>"$WORK/g3.err" || s3rc=$?
[ "$s3rc" -eq 7 ] ||
    fail "G3: det_emit_section returned $s3rc for a fixture that exited 7 — expected 7. THIS IS THE DEFECT THE ROW NAMED: the status is being dropped."

late="$WORK/late_failure"
l3rc=0
STUB_FAIL_ARG="--second" STUB_FAIL_CODE=7 \
    det_emit_digest "$STUB" "$late" "$CORPUS" "${SECTIONS[@]}" 2>"$WORK/g3b.err" || l3rc=$?
[ "$l3rc" -eq 7 ] ||
    fail "G3: det_emit_digest returned $l3rc for a fixture that exited 7 on the last section — expected 7"

early="$WORK/early_failure"
e3rc=0
STUB_FAIL_ARG="$CORPUS_DIR/alpha.log" STUB_FAIL_CODE=2 \
    det_emit_digest "$STUB" "$early" "$CORPUS" "${SECTIONS[@]}" 2>"$WORK/g3c.err" || e3rc=$?
[ "$e3rc" -eq 2 ] ||
    fail "G3: det_emit_digest returned $e3rc for a fixture that exited 2 on the FIRST section — expected 2"
headers_after="$(grep -c '^### ' "$early" || true)"
[ "$headers_after" -eq 1 ] ||
    fail "G3: after a failure on the first of four sections the digest holds $headers_after header(s), expected 1 — the walk did not stop, so the remaining sections were emitted under a run already known to be broken"

# ── G4 — verbose on failure: the diagnostic must locate the fault without a re-run.
for token in "--second (another)" "exited 7"; do
    grep -qF -- "$token" "$WORK/g3.err" ||
        fail "G4: the failure diagnostic does not contain '$token' — a CI reader cannot tell which section failed or how.
    got: $(sed 's/^/        /' "$WORK/g3.err")"
done

# ── G5 — the fixture's stderr never enters the digest. It is surfaced to the caller's stderr (the
# MSVC leg inherits it into the runner console) and the digest carries stdout only. A digest that
# absorbed a diagnostic would differ between a leg that emitted one and a leg that did not.
noisy="$WORK/noisy"
STUB_STDERR=1 det_emit_digest "$STUB" "$noisy" "$CORPUS" "${SECTIONS[@]}" 2>"$WORK/g5.err" || true
cmp -s "$expected" "$noisy" ||
    fail "G5: a fixture writing to stderr changed the digest bytes — stderr is leaking into the emitted digest.
$(diff -u "$expected" "$noisy" | sed 's/^/    /')"
grep -qF 'stub diagnostic' "$WORK/g5.err" ||
    fail "G5: the fixture's stderr was swallowed instead of surfaced — the MSVC leg shows it, and a black-holed diagnostic is half of what this repair is about"
grep -qF 'stub diagnostic' "$noisy" &&
    fail "G5: the fixture's stderr text is present in the digest file"

# ── G6 — STRUCTURAL: the Linux driver routes through this writer and refuses its non-zero return.
# Declared limit: reaching the driver's own emit block behaviourally costs the four-cell tower build
# this whole script exists to avoid, so these are read as text.
grep -qF 'determinism_emit.sh' "$DRIVER" ||
    fail "G6: $(basename "$DRIVER") does not source determinism_emit.sh — the driver has its own emit loop again, which is the defect this writer was extracted to close"
grep -qE '^\s*det_emit_digest .*\|\| \{' "$DRIVER" ||
    fail "G6: $(basename "$DRIVER") does not guard its det_emit_digest call with '|| {' — an unguarded call discards the status exactly as the inline loop did"
grep -nE '"\$\{BIN\[[^]]*\]\}" .*>>' "$DRIVER" | grep -v det_emit &&
    fail "G6: $(basename "$DRIVER") still invokes the fixture binary directly and appends to a digest — that path bypasses the exit-status check above"

# ── G7 — STRUCTURAL: the MSVC leg still refuses a non-zero exit, so the two legs keep agreeing about
# what a failure is. Nothing else in the repo would notice this line being deleted.
grep -qF 'if ($p.ExitCode -ne 0)' "$WORKFLOW" ||
    fail "G7: golden.yaml's PowerShell emitter no longer refuses a non-zero det_fixture exit — the MSVC leg has stopped agreeing with the Linux leg about what counts as a failure, which is the asymmetry this pair of guards exists to hold closed"

# ── G8 — the writer must stay a pure appender that is HANDED its header. Extracting it created one
# new place a synthetic section name could be typed by hand, which is the 2026-08-24 defect
# scripts/determinism_sections_check.sh exists to refuse — it scans the two ENUMERATORS, and the
# writer is neither, so this arm covers the file that extraction added rather than widening its list.
hand="$(grep -nE '###[[:space:]]+--[a-z]' "$EMIT_LIB" || true)"
[ -z "$hand" ] ||
    fail "G8: $(basename "$EMIT_LIB") writes a synthetic section header by hand — the writer is handed its header text and must never name a section:
$(sed 's/^/    /' <<<"$hand")"

if [ $rc -eq 0 ]; then
    echo "emit OK: the digest's byte shape is pinned (4 sections, order-sensitive), a fixture exiting"
    echo "  non-zero returns that status and stops the walk, its stderr is surfaced and never digested,"
    echo "  and both emitters — the bash driver and golden.yaml's PowerShell — refuse a failing fixture."
fi
exit $rc
