#!/usr/bin/env bash
# Determinism golden driver — the metalog leg of the Determinism-Golden-Proof (golden.yaml). Builds the
# canon+metalog MODULE-LIB TOWER from source for ONE toolchain leg across the -O{0,3}×-ffp-contract
# {off,fast} corners, asserts the serialized MetaLog document (corpus + the ADR-31.D8 --reservoir-nearfull
# and --reservoir-streaming arms — the Sift BATCH tuple and the SHIPPED streaming tuple — plus the §C3
# --cube-collapse and O4b --service-edges scenarios) is byte-identical across that leg's corners,
# and EMITS the leg's digest (DETERMINISM_OUT).
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
# Requires conan + the leg profiles seeded in CONAN_HOME/profiles. Exit non-zero on any -O/-ffp divergence
# or a cell build failure (a one-corner green is hollow). Run locally (sweep-invariance check) or by golden.yaml.
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
META="$(cd "$SCRIPT_DIR/.." && pwd)"
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

CORPUS="$(ls "$META"/scripts/determinism_corpus/*.log 2>/dev/null | sort)"
[ -n "$CORPUS" ] || { echo "no corpus under $META/scripts/determinism_corpus"; exit 1; }
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

# Each cell emits the committed corpus (5 files) THEN --reservoir-nearfull (the ADR-31.D8 synthetic M=128
# scenario, the Sift BATCH tuple) THEN --reservoir-streaming (the SECOND ADR-31.D8 arm, at the tuple the
# streaming surface ships — salience-1/k128-m64-c0-e16, where the error-class reserve is live and the
# batch arm has no opinion) THEN --cube-collapse (the §C3 cube dimensional-collapse guardrail — a window
# that FIRES a collapse, so its content-driven axis-selection tie-break is proven cross-leg) THEN
# --service-edges (the O4b service-topology over-cap window — the emitted block rides the top-K select's
# canonical-key tie-break, proven cross-leg). Compare every built cell to the reference — byte-identity
# across the leg's -O/-ffp sweep.
for ctag in "${builds[@]}"; do
  : >"$WORK/$ctag.out"
  for f in $CORPUS; do echo "### $(basename "$f") ###" >>"$WORK/$ctag.out"; "${BIN[$ctag]}" "$f" >>"$WORK/$ctag.out" 2>/dev/null; done
  echo "### --reservoir-nearfull (ADR-31.D8 synthetic M=128) ###" >>"$WORK/$ctag.out"
  "${BIN[$ctag]}" --reservoir-nearfull >>"$WORK/$ctag.out" 2>/dev/null
  echo "### --reservoir-streaming (ADR-31.D8 shipped streaming tuple k128-m64-c0-e16) ###" >>"$WORK/$ctag.out"
  "${BIN[$ctag]}" --reservoir-streaming >>"$WORK/$ctag.out" 2>/dev/null
  echo "### --cube-collapse (SecC3 dimensional-collapse guardrail) ###" >>"$WORK/$ctag.out"
  "${BIN[$ctag]}" --cube-collapse >>"$WORK/$ctag.out" 2>/dev/null
  echo "### --service-edges (O4b service-topology over-cap top-K tie-break) ###" >>"$WORK/$ctag.out"
  "${BIN[$ctag]}" --service-edges >>"$WORK/$ctag.out" 2>/dev/null
done
rc=0; ref="${builds[0]}"
echo "reference: $ref  sha=$(sha256sum "$WORK/$ref.out" | cut -c1-16)"
echo "── built Linux legs (cross-stdlib when >1 leg; -O{0,3}×-ffp{off,fast} corners always) ──"
for ctag in "${builds[@]}"; do
  if cmp -s "$WORK/$ref.out" "$WORK/$ctag.out"; then st=IDENTICAL; else st=DIVERGENT; rc=1; fi
  printf "  %-28s %s\n" "$ctag" "$st"
done

if [ $rc -eq 0 ]; then
  echo "PASS: byte-identical across ${#builds[@]} built cell(s) — corpus + --reservoir-nearfull +"
  echo "  --reservoir-streaming + --cube-collapse + --service-edges, over the '${LINUX_LEGS[*]}' leg's"
  echo "  -O{0,3}×-ffp{off,fast} sweep."
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
  echo "  about the DEPLOYED tuple. Localize: diff \$WORK/<ctag>.out vs \$WORK/$ref.out."
fi
exit $rc
