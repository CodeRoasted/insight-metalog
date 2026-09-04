#!/usr/bin/env bash
# Determinism golden driver — the metalog leg of the Determinism-Golden-Proof (golden.yaml). Builds the
# canon+metalog MODULE-LIB TOWER from source for ONE toolchain leg across the -O{0,3}×-ffp-contract
# {off,fast} corners, asserts the serialized MetaLog document, the serialized MetaLogDiff — the producer's
# second artifact species — and the serialized output of compose(), its second published operation
# (DN-82.D2), are byte-identical across that leg's corners, and EMITS the leg's digest
# (DETERMINISM_OUT). WHAT it replays is not written here: the committed corpus comes off disk, and
# the synthetic scenarios come from scripts/determinism_sections.txt, which owns their
# identity, their order and their reason for existing (this driver and golden.yaml's MSVC leg both
# read it; neither enumerates them).
#
# There is NO committed golden (retired — no more committed-golden apparatus). Cross-toolchain / cross-
# stdlib / cross-ISA / cross-OS bit-identity is asserted by golden.yaml's `compare` job, which byte-
# compares the digests this driver emits for every leg (gcc/clang × x86/arm64) against the MSVC leg's.
# This script proves only the per-leg -O/-ffp sweep-invariance and emits — one leg per CI job.
#
# ── Approach B (Daidalos ruling 2026-06-06; the private twin of canon's det_public_proof.sh)
# The 1.5.1 unwrap turned canon+metalog into C++20 modules, so "build the tower N ways" means: per leg,
# build canon+metalog as a MODULE STATIC-LIB tower via their real CXX_MODULE_STD build (scripts/
# det_harness: add_subdirectory(canon)+add_subdirectory(metalog) + the fixture importing the tower),
# driven by the leg's conan profile. Building the libs IS recompiling canon+metalog under the leg's
# codegen; only the trivial fixture moves from recompile to link.
#
# INTERFACE (set by golden.yaml):
#   DETERMINISM_LEG=gcc|clang        the single compiler leg to build (→ its profile below)
#   DETERMINISM_OUT=<path>           emit this leg's full digest to <path> for the cross-leg compare
#   DETERMINISM_CANON_ROOT=<dir>     canon SOURCE root (the harness add_subdirectory-builds it per cell)
#   DETERMINISM_{GCC,CLANG}_PROFILE  conan profile per leg (x86 defaults; arm64 legs inject the arm profiles)
#
# Requires conan + the leg profiles seeded in CONAN_HOME/profiles. Run locally (sweep-invariance check)
# or by golden.yaml. EXIT CODES — every one of them is a red, and they are distinct so a CI log names
# the stage without being read:
#   0  every built cell agreed byte for byte (and, with DETERMINISM_OUT set, the digest was emitted)
#   1  -O/-ffp divergence within the leg
#   2  bad interface (unknown DETERMINISM_LEG / unknown leg key) or a missing subject file
#   3  gate integrity: some configured cell did not build (a one-corner green is hollow)
#   4  the fixture itself failed on a section — see scripts/determinism_emit.sh for why that is a red
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
META="$(cd "$SCRIPT_DIR/.." && pwd)"

# The section writer — how a section becomes bytes, and what happens when the fixture producing them
# fails. Sourced rather than inlined so those failure semantics are provable at a desk with no
# toolchain (scripts/determinism_emit_check.sh); the ENUMERATIONS stay here, in this file.
EMIT_LIB="$SCRIPT_DIR/determinism_emit.sh"
[ -f "$EMIT_LIB" ] || { echo "no $EMIT_LIB (the section writer)"; exit 2; }
# shellcheck source=determinism_emit.sh
. "$EMIT_LIB"
# canon SOURCE root (the harness add_subdirectory-builds it per cell). Defaults to the sibling checkout;
# DETERMINISM_CANON_ROOT overrides it for CI, where insight-canon is checked out into a job-local path
# (e.g. $GITHUB_WORKSPACE/_canon) rather than a sibling of metalog.
CANON="${DETERMINISM_CANON_ROOT:-$META/../insight-canon}"
[ -d "$CANON" ] && CANON="$(cd "$CANON" && pwd)"
HARNESS="$SCRIPT_DIR/det_harness"
FIXTURE="$SCRIPT_DIR/determinism_fixture.cpp"

[ -f "$HARNESS/CMakeLists.txt" ] || { echo "no $HARNESS/CMakeLists.txt (the Approach-B tower harness)"; exit 1; }
[ -f "$FIXTURE" ] || { echo "no $FIXTURE"; exit 1; }
[ -d "$CANON" ] || { echo "no canon repo at $CANON"; exit 1; }
command -v conan >/dev/null || { echo "conan not found — the module tower build needs it"; exit 1; }
command -v cmake >/dev/null || { echo "cmake not found"; exit 1; }
export CONAN_HOME="${CONAN_HOME:-$(cd "$META/.." && pwd)/.conan2}"

# LC_ALL=C: the corpus order IS digest order, and the MSVC leg sorts the same names with .NET's
# ordinal comparer. A locale-aware collation treats punctuation as ignorable, so `service.log` vs
# `service_a.log` orders differently under en_US.UTF-8 than under C — a cross-leg divergence with no
# code change behind it. Both emitters pin byte order. (Today's seven names agree under either;
# this keeps that from being luck.)
CORPUS="$(ls "$META"/scripts/determinism_corpus/*.log 2>/dev/null | LC_ALL=C sort)"
[ -n "$CORPUS" ] || { echo "no corpus under $META/scripts/determinism_corpus"; exit 1; }

# The synthetic section roster — read, never enumerated here. See the file's own header for the
# format and for what it cost to learn that a twice-written list is not a list.
SECTIONS_FILE="$META/scripts/determinism_sections.txt"
[ -f "$SECTIONS_FILE" ] || { echo "no $SECTIONS_FILE (the synthetic section roster)"; exit 1; }
mapfile -t SECTIONS < <(sed -E 's/[[:space:]]+$//' "$SECTIONS_FILE" | grep -vE '^([[:space:]]*#|[[:space:]]*$)')
[ "${#SECTIONS[@]}" -gt 0 ] || { echo "$SECTIONS_FILE lists no section — the digest would carry the corpus only"; exit 1; }
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
echo "canon=$CANON  conan_home=$CONAN_HOME  corpus=$(echo "$CORPUS" | wc -l) files"

# `import std` module-tower builds are memory-hungry (~2-3 GB per concurrent compile). On a small
# CI runner (2-core / ~7 GB), ninja's default ncpu+2 parallelism OOM-kills the build mid-compile —
# which truncates the log to nothing and surfaces as a bare "BUILD FAIL" with no cause. Cap the
# build job count by BOTH cpu and free memory (≈3 GB/job) so peak stays under the runner's limit.
mem_gb=$(awk '/MemAvailable/{print int($2/1024/1024)}' /proc/meminfo 2>/dev/null || echo 4)
ncpu=$(nproc 2>/dev/null || echo 2)
JOBS=$(( mem_gb / 3 )); [ "$JOBS" -lt 1 ] && JOBS=1; [ "$JOBS" -gt "$ncpu" ] && JOBS=$ncpu
echo "build parallelism: $JOBS jobs (MemAvailable=${mem_gb}GB cpu=$ncpu)"

# The CELL MATRIX = stdlib leg × optimization corner. Two orthogonal hazard axes:
#   - stdlib leg (ship gcc/libstdc++ vs clang-21/libc++): catches ITERATION-ORDER leaks (unordered_*),
#     the ADR-31.D8 class — the cross-stdlib axis is the ONLY one that exposes a hash-order flip.
#   - optimization corner (-O{0,3} × -ffp-contract{off,fast}): catches FP-CONTRACTION / reassociation
#     leaks — a stray float op in the det_math / salience path that -ffp=fast would reorder. A correct
#     integer/fixed-point det core is -ffp-INVARIANT, so these MUST stay identical; a divergent -ffp
#     cell is a det_math gap. (-DNDEBUG is fixed from Release; only -O / -ffp vary per cell.)
#
# LEG — golden.yaml runs ONE leg per CI job via DETERMINISM_LEG (gcc|clang), keyed into LEG_SPEC below.
# The cross-STDLIB property (ship gcc/libstdc++ ≡ clang-21/libc++ — the only axis that exposes an
# unordered_* iteration-order leak, ADR-31.D8), cross-ISA, and cross-OS are all the golden.yaml `compare` of
# every leg's emitted digest; this script proves only THIS leg's -O/-ffp sweep-invariance and emits.
# The conan PROFILE per leg is overridable so the SAME driver runs on a 2nd ISA: the arm64 legs inject
# DETERMINISM_GCC_PROFILE=linux-gcc16-arm64-release / DETERMINISM_CLANG_PROFILE=linux-clang21-libcxx-arm64-release
# (the only x86↔arm64 difference is the profile's arch/-march; the compiler binaries are wired identically).
GCC_PROFILE="${DETERMINISM_GCC_PROFILE:-linux-gcc16-release}"
CLANG_PROFILE="${DETERMINISM_CLANG_PROFILE:-linux-clang21-libcxx-release}"
# The PROFILE is the ONLY compiler authority: the harness cmake gets CC/CXX extracted from the
# leg profile's [buildenv], never a hand-kept binary name. WHY (measured 2026-08-16): the old
# leg array carried literal `g++-15:gcc-15` bins that were passed as -DCMAKE_C/CXX_COMPILER;
# conan_toolchain.cmake sets no compiler, so the -D literals WON over the profile — a desk run
# with DETERMINISM_GCC_PROFILE=linux-gcc16-release silently built BOTH gcc legs with the PATH's
# g++-15 and the 15.3-vs-16.2 byte-compare was 15.3-vs-15.3, vacuous. (In CI the missing g++-15
# skipped the leg and the ADR-31.D8 cell-count gate redded — loud there, silent at the desk.)
declare -A LEG_SPEC=(
  [gcc-libstdcxx]="$GCC_PROFILE"
  [clang-libcxx]="$CLANG_PROFILE"
)
# DETERMINISM_LEG (singular) = the canon-golden-workflow interface: run ONE compiler per job so the
# cross-stdlib property comes from the workflow's compare of the per-leg digests (gcc-x86 == clang-x86),
# exactly like canon's det_public_proof.sh. Maps to the single LEG_SPEC key; overrides DETERMINISM_LEGS.
case "${DETERMINISM_LEG:-}" in
  gcc)   DETERMINISM_LEGS="gcc-libstdcxx" ;;
  clang) DETERMINISM_LEGS="clang-libcxx" ;;
  '')    : ;;
  *) echo "::error::unknown DETERMINISM_LEG='$DETERMINISM_LEG' (expected gcc|clang)" >&2; exit 2 ;;
esac
read -ra LINUX_LEGS <<<"${DETERMINISM_LEGS:-clang-libcxx}"

cells=(
  "O3_off:-O3 -ffp-contract=off"
  "O0_off:-O0 -ffp-contract=off"
  "O3_fast:-O3 -ffp-contract=fast"
  "O0_fast:-O0 -ffp-contract=fast"
)
declare -A BIN
declare -A LEG_BUILT
builds=()
for legkey in "${LINUX_LEGS[@]}"; do
  spec="${LEG_SPEC[$legkey]:-}"
  [ -n "$spec" ] || { echo "UNKNOWN LEG: '$legkey' (known: ${!LEG_SPEC[*]})"; exit 2; }
  profile="$spec"
  tag="$legkey"
  [ -f "$CONAN_HOME/profiles/$profile" ] || { echo "MISSING PROFILE: $profile (leg $tag)"; continue; }

  # Preflight: the PROFILE is the source of truth for the compiler (its [buildenv] CXX/CC) —
  # e.g. linux-gcc16-release pins CXX=/opt/gcc-16.2/bin/g++ (the published from-source asset)
  # which the runner may not have provisioned. Check the profile's compiler exists and FAIL
  # FAST with the path, instead of letting conan invoke a missing compiler and surfacing it as a
  # buried "fmt cmake.configure Error 1" 1000 lines deep. (Was lost on the gcc-15.3 CI-drift, 2026-06-15.)
  prof_cxx="$(sed -nE 's/^[[:space:]]*CXX[[:space:]]*=[[:space:]]*//p' "$CONAN_HOME/profiles/$profile" | tail -1)"
  # A profile CXX may be an ABSOLUTE path (linux-gcc16-release → /opt/gcc-16.2/bin/g++) OR a
  # PATH-relative name (linux-clang21-libcxx-release → clang++-21). Accept either: `-x` for a path,
  # `command -v` for a PATH lookup. (A bare name MUST NOT be tested with `-x` alone — that checks the
  # CWD, falsely failing a PATH-resolvable compiler; it silently skipped the clang leg, 2026-06-16.)
  if [ -z "$prof_cxx" ] || ! { [ -x "$prof_cxx" ] || command -v "$prof_cxx" >/dev/null 2>&1; }; then
    echo "MISSING COMPILER: '${prof_cxx:-<no CXX in profile>}' — profile '$profile' [buildenv] points here"
    echo "  but the runner has no such binary on PATH or at that path. (e.g. linux-gcc16-release pins"
    echo "  from-source /opt/gcc-16.2 — provision it in CI, or point the profile at an available toolchain.)"
    continue
  fi
  prof_cc="$(sed -nE 's/^[[:space:]]*CC[[:space:]]*=[[:space:]]*//p' "$CONAN_HOME/profiles/$profile" | tail -1)"
  [ -n "$prof_cc" ] || prof_cc="$prof_cxx"   # a CXX-only profile: harness C code rides the CXX driver's cc
  # Resolve PATH-relative names to absolute paths and PRINT the leg's compiler identity — the
  # one line that makes a wrong-compiler leg impossible to miss in any log.
  cxx_abs="$(command -v "$prof_cxx")" || cxx_abs="$prof_cxx"
  cc_abs="$(command -v "$prof_cc")" || cc_abs="$prof_cc"
  echo "leg $tag: CXX=$cxx_abs ($("$cxx_abs" --version 2>/dev/null | head -1))"

  # conan install once per leg (the deps are -O/-ffp-independent); each cell re-cmakes the tower.
  legdir="$WORK/conan-$tag"
  if ! conan install "$META" --profile:host="$profile" --profile:build="$profile" \
        --build=missing -of "$legdir" >"$legdir.install.log" 2>&1; then
    # Print a generous tail: a dep's cmake.configure error (e.g. fmt) sits well above conan's final
    # ConanException summary — `tail -4` hid the real cause on the gcc-15.3 CI-drift.
    echo "CONAN INSTALL FAIL: $tag"; tail -50 "$legdir.install.log" | sed 's/^/   /'; continue
  fi
  toolchain="$(find "$legdir" -name conan_toolchain.cmake 2>/dev/null | head -1)"
  [ -f "$toolchain" ] || { echo "CONAN INSTALL FAIL: $tag — no conan_toolchain.cmake"; continue; }

  for cell in "${cells[@]}"; do
    IFS=: read -r cname cflags <<<"$cell"
    ctag="$tag-$cname"
    bdir="$WORK/build-$ctag"
    # Separate configure from build so a failure names the stage, and print the REAL tail of the
    # failing stage's log (a one-line "BUILD FAIL" with no diagnostics is useless under CI).
    if ! cmake -S "$HARNESS" -B "$bdir" -G Ninja \
          -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_C_COMPILER="$cc_abs" -DCMAKE_CXX_COMPILER="$cxx_abs" \
          -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
          -DCANON_ROOT="$CANON" -DMETA_ROOT="$META" \
          -DCELL_FLAGS="$cflags -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_OFF" >"$bdir.cfg.log" 2>&1; then
      echo "CONFIGURE FAIL: $ctag"; tail -40 "$bdir.cfg.log" | sed 's/^/   /'; continue
    fi
    if ! cmake --build "$bdir" --target det_fixture --parallel "$JOBS" >"$bdir.build.log" 2>&1; then
      echo "BUILD FAIL: $ctag"; tail -40 "$bdir.build.log" | sed 's/^/   /'; continue
    fi
    bin="$(find "$bdir" -name det_fixture -type f -perm -u+x 2>/dev/null | head -1)"
    if [ -x "$bin" ]; then builds+=("$ctag"); BIN["$ctag"]="$bin"; LEG_BUILT["$tag"]=$(( ${LEG_BUILT[$tag]:-0} + 1 ))
    else echo "BUILD FAIL: $ctag (no det_fixture produced)"; fi
  done
done

# Gate integrity: every configured BUILT leg must produce ALL cells (no hollow one-corner green). A
# missing compiler or a cell that fails to build (e.g. a toolchain ICE) FAILS the gate — no silent
# degrade. DETERMINISM_LEGS is the source of truth for the built set. [ADR-31.D8]
expected=$(( ${#LINUX_LEGS[@]} * ${#cells[@]} ))
for legkey in "${LINUX_LEGS[@]}"; do
  [ "${LEG_BUILT[$legkey]:-0}" -eq "${#cells[@]}" ] ||
    echo "GATE INTEGRITY: leg $legkey built ${LEG_BUILT[$legkey]:-0}/${#cells[@]} cells"
done
if [ "${#builds[@]}" -eq 0 ] || [ "${#builds[@]}" -ne "$expected" ]; then
  echo "GATE INTEGRITY FAIL: built ${#builds[@]} cells, expected $expected"
  echo "  (DETERMINISM_LEGS='${LINUX_LEGS[*]}' × ${#cells[@]} -O/-ffp corners). See the FAIL tails above."
  exit 3
fi

# THE EMIT — the shape of the digest, not its contents. Each cell writes the committed corpus first
# (enumerated from disk, never a hand-kept count; each section is doc1 + doc2 + the MetaLogDiff
# BETWEEN them + the document compose() merges them into), then one section per roster row, in roster
# order. A section is the ASCII header `### <name> ###\n` followed by the fixture's raw stdout.
# golden.yaml's MSVC leg writes the same two loops over the same two sources — that is the whole
# point of them being sources.
#
# The per-section WHY lives beside its row in scripts/determinism_sections.txt. It is deliberately
# NOT restated here: an enumeration written in two places is what put the MSVC leg two sections
# behind on 2026-08-24, and a prose copy of a list rots exactly the same way the code copy did.
#
# The per-section fixture EXIT STATUS is checked, and a non-zero one FAILS THE RUN rather than
# leaving an empty body under a correctly-spelled header. Until 2026-09-04 it was discarded here
# while golden.yaml's MSVC `Add-Section` threw on it — two legs of one gate disagreeing about what
# counts as a failure. det_emit_digest (scripts/determinism_emit.sh) owns both halves now, and it
# is handed the two lists THIS file enumerated: one enumerator per emitter, unchanged.
for ctag in "${builds[@]}"; do
  det_emit_digest "${BIN[$ctag]}" "$WORK/$ctag.out" "$CORPUS" "${SECTIONS[@]}" || {
    echo "EMIT FAIL: cell $ctag — the fixture failed on a section (cause above)."
    echo "  Not a determinism result: the digest is truncated, and every cell would truncate the same"
    echo "  way, so the -O/-ffp compare below would have called that agreement. golden.yaml's MSVC leg"
    echo "  throws on this same condition; this leg no longer disagrees with it."
    exit 4
  }
done
rc=0; ref="${builds[0]}"
echo "reference: $ref  sha=$(sha256sum "$WORK/$ref.out" | cut -c1-16)"
echo "── built Linux legs (cross-stdlib when >1 leg; -O{0,3}×-ffp{off,fast} corners always) ──"
for ctag in "${builds[@]}"; do
  if cmp -s "$WORK/$ref.out" "$WORK/$ctag.out"; then st=IDENTICAL; else st=DIVERGENT; rc=1; fi
  printf "  %-28s %s\n" "$ctag" "$st"
done

if [ $rc -eq 0 ]; then
  # The section list in this message is DERIVED, not typed: a PASS line that names a roster it did
  # not read is how a green comes to describe a digest smaller than the one it judged.
  echo "PASS: byte-identical across ${#builds[@]} built cell(s) — $(echo "$CORPUS" | wc -l) corpus section(s)"
  echo "  (documents + the diff between them + their composition) + ${#SECTIONS[@]} synthetic:$(printf ' %s' "${SECTIONS[@]%% *}")"
  echo "  over the '${LINUX_LEGS[*]}' leg's -O{0,3}×-ffp{off,fast} sweep."
  # Cross-leg-agreement mode (the ONLY mode now — no committed golden): emit this leg's full digest
  # (corpus + reservoir, byte-identical across its own -O×-ffp cells above) for the golden.yaml compare
  # job to byte-compare against every other leg (gcc/clang × x86/arm64 + msvc). One leg per job → the
  # cross-stdlib/ISA/OS assertion is that compare, not this script.
  if [ -n "${DETERMINISM_OUT:-}" ]; then
    cp "$WORK/$ref.out" "$DETERMINISM_OUT"
    echo "emitted digest → $DETERMINISM_OUT  (leg '${LINUX_LEGS[*]}', $(sha256sum "$WORK/$ref.out" | cut -c1-16)…)"
  fi
else
  echo "FAIL: determinism divergence within the '${LINUX_LEGS[*]}' leg's -O/-ffp sweep — a det_math"
  echo "  -ffp-contraction leak, an -O-sensitive ordering leak, OR (on either --reservoir-* marker)"
  echo "  the ADR-31.D8 item-reservoir admit/evict leak — the streaming marker is the one that speaks"
  echo "  about the DEPLOYED tuple. On a DIFF line it is neither: a diff carries this producer's"
  echo "  floating point (kl/js divergence, frequencies, entropy bits), so an -ffp cell that diverges"
  echo "  there is a det_math gap in the diff path, which no document section can exhibit."
  echo "  Localize: diff \$WORK/<ctag>.out vs \$WORK/$ref.out."
fi
exit $rc
