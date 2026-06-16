#!/usr/bin/env bash
# Determinism gate — CONFIGURABLE multi-leg bit-identity of the MetaLog document (cross-stdlib +
# cross-OS + optimization-corner).
#
# Asserts the serialized document is byte-identical across N legs (2 or 3, see `LEGS` below):
#   • BUILT Linux toolchains (DETERMINISM_LEGS): g++ (gcc-15/libstdc++) and/or clang++ (clang-21/libc++),
#     each across the -O{0,3} × -ffp-contract{off,fast} corners → the cross-STDLIB diagonal (catches an
#     unordered_*/iteration-order leak, i.e. F5-M8) + the FP-contraction/optimization corners.
#   • the MSVC leg (cross-OS): the committed corpus golden, which the per-repo
#     windows-portability-probe.yml verifies == real MSVC — no cl.exe needed in this Linux script.
# DEFAULT = clang(built) + MSVC(golden) [2 legs] while gcc-15.3 is out of CI; gcc(built)+clang(built)+
# MSVC = the full 3-leg diagonal once /opt/gcc-15.3 is provisioned. (insight_determinism_model.md §F5 /
# cxx_modules_migration_contract.md §5.)
#
# This IS the determinism diagonal (insight_determinism_model.md §F5 / cxx_modules_
# migration_contract.md §5): the strongest bit-identity oracle, because the two legs
# differ in BOTH compiler AND stdlib — it hunts the libm/FMA/reassociation divergences
# AND the unordered_*/iteration-order ones (a libstdc++↔libc++ map-order bug shows up
# here). Cross-compiler/stdlib proxy for the cross-arch (x64+arm) gate; the in-suite
# DeterminismGate golden pins the same artifact per build.
#
# ── Approach B (Daidalos ruling 2026-06-06; the private twin of canon's det_public_proof.sh)
# The 1.5.1 unwrap turned canon+metalog into C++20 modules, deleting the textual headers the
# old single-shot `$cxx canon/src metalog/src fixture.cpp` recompile #include'd. The methodology
# is unchanged (build N ways → digest → assert identical); only the "N ways" MECHANIC changed:
# per leg, build canon+metalog as a MODULE STATIC-LIB tower via their real CXX_MODULE_STD build
# (scripts/det_harness: add_subdirectory(canon)+add_subdirectory(metalog) + the fixture importing
# the tower), driven by the leg's conan profile. Building the libs IS recompiling canon+metalog
# under the leg's codegen; only the trivial fixture moves from recompile to link, so the diagonal's
# coverage is unchanged. BMI ordering + the std module are delegated to the build system the
# modules cascade already proves green. The fixture/digest/compare core below is byte-untouched —
# so the 2 legs staying IDENTICAL is the same invariant the retired source-recompile asserted.
#
# Requires conan + the leg profiles seeded in CONAN_HOME/profiles (malf seeds them); a prior dep
# resolution populates the cache. Run locally or via the superproject determinism-gate.yml. Exit
# non-zero on any divergence, or if either diagonal leg fails to build (a one-leg gate is hollow).
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
META="$(cd "$SCRIPT_DIR/.." && pwd)"
CANON="$(cd "$META/../insight-canon" && pwd)"
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
#   - stdlib leg (gcc-15/libstdc++ vs clang-21/libc++): catches ITERATION-ORDER leaks (unordered_*),
#     the F5-M8 class — the cross-stdlib axis is the ONLY one that exposes a hash-order flip.
#   - optimization corner (-O{0,3} × -ffp-contract{off,fast}): catches FP-CONTRACTION / reassociation
#     leaks — a stray float op in the det_math / salience path that -ffp=fast would reorder. A correct
#     integer/fixed-point det core is -ffp-INVARIANT, so these MUST stay identical; a divergent -ffp
#     cell is a det_math gap. (-DNDEBUG is fixed from Release; only -O / -ffp vary per cell.)
#
# LEGS — CONFIGURABLE, 2 or 3. Two KINDS of leg, byte-compared to each other:
#   • BUILT Linux toolchains (DETERMINISM_LEGS, space-separated keys of LEG_SPEC) — each builds the
#     canon+metalog tower across the -O/-ffp corners and runs the fixtures. gcc-15/libstdc++ ≡
#     clang-21/libc++ = the cross-STDLIB diagonal (the only axis that exposes an unordered_* order
#     leak, i.e. F5-M8).
#   • the MSVC leg — NOT built here (Linux bash can't run cl.exe); it enters as the committed CORPUS
#     golden (determinism_golden.txt), which the per-repo windows-portability-probe.yml independently
#     verifies == real MSVC. Comparing a Linux leg's corpus to it asserts CROSS-OS bit-identity.
#       - DEFAULT now: clang(built) + MSVC(golden) = 2 legs. gcc is OUT because CI has no /opt/gcc-15.3
#         (its profile pins from-source 15.3 for PR124309; PPA is 15.2 → module tower ICEs).
#       - once /opt/gcc-15.3 is provisioned: DETERMINISM_LEGS="gcc15-libstdcxx clang21-libcxx" →
#         gcc(built) + clang(built) + MSVC(golden) = the full 3-leg diagonal.
#       - DETERMINISM_MSVC=0 drops the MSVC anchor (pure Linux cross-stdlib run).
declare -A LEG_SPEC=(
  [gcc15-libstdcxx]="g++-15:gcc-15:linux-gcc15-release"     # cxx-bin:cc-bin:conan-profile
  [clang21-libcxx]="clang++-21:clang-21:linux-clang21-libcxx-release"
)
read -ra LINUX_LEGS <<<"${DETERMINISM_LEGS:-clang21-libcxx}"
DETERMINISM_MSVC="${DETERMINISM_MSVC:-1}"
GOLDEN_TXT="$SCRIPT_DIR/determinism_golden.txt"

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
  IFS=: read -r cxxbin ccbin profile <<<"$spec"
  tag="$legkey"
  [ -f "$CONAN_HOME/profiles/$profile" ] || { echo "MISSING PROFILE: $profile (leg $tag)"; continue; }

  # Preflight: the PROFILE is the source of truth for the compiler (its [buildenv] CXX), NOT the
  # leg-array `cxxbin` — e.g. linux-gcc15-release pins CXX=/opt/gcc-15.3/bin/g++ (from-source 15.3,
  # PR124309) which the runner may not have provisioned. Check the profile's compiler exists and FAIL
  # FAST with the path, instead of letting conan invoke a missing compiler and surfacing it as a
  # buried "fmt cmake.configure Error 1" 1000 lines deep. (Was lost on the gcc-15.3 CI-drift, 2026-06-15.)
  prof_cxx="$(sed -nE 's/^[[:space:]]*CXX[[:space:]]*=[[:space:]]*//p' "$CONAN_HOME/profiles/$profile" | tail -1)"
  # A profile CXX may be an ABSOLUTE path (linux-gcc15-release → /opt/gcc-15.3/bin/g++) OR a
  # PATH-relative name (linux-clang21-libcxx-release → clang++-21). Accept either: `-x` for a path,
  # `command -v` for a PATH lookup. (A bare name MUST NOT be tested with `-x` alone — that checks the
  # CWD, falsely failing a PATH-resolvable compiler; it silently skipped the clang leg, 2026-06-16.)
  if [ -n "$prof_cxx" ] && ! { [ -x "$prof_cxx" ] || command -v "$prof_cxx" >/dev/null 2>&1; }; then
    echo "MISSING COMPILER: $prof_cxx — profile '$profile' [buildenv] points here but the runner has"
    echo "  no such binary on PATH or at that path. (e.g. linux-gcc15-release pins from-source"
    echo "  /opt/gcc-15.3 for PR124309 — provision it in CI, or point the profile at an available toolchain.)"
    continue
  fi
  command -v "$cxxbin" >/dev/null || { echo "MISSING COMPILER: $cxxbin (leg $tag)"; continue; }

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
          -DCMAKE_C_COMPILER="$ccbin" -DCMAKE_CXX_COMPILER="$cxxbin" \
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
# degrade. DETERMINISM_LEGS is the source of truth for the built set; the MSVC golden anchor is checked
# separately below. [F5-M8]
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

# Each cell emits the committed corpus (5 files) THEN --reservoir-nearfull (the F5-M8 synthetic M=128
# scenario). Compare every built cell to the reference; then anchor the CORPUS to the MSVC golden.
for ctag in "${builds[@]}"; do
  : >"$WORK/$ctag.out"
  for f in $CORPUS; do echo "### $(basename "$f") ###" >>"$WORK/$ctag.out"; "${BIN[$ctag]}" "$f" >>"$WORK/$ctag.out" 2>/dev/null; done
  echo "### --reservoir-nearfull (F5-M8 synthetic M=128) ###" >>"$WORK/$ctag.out"
  "${BIN[$ctag]}" --reservoir-nearfull >>"$WORK/$ctag.out" 2>/dev/null
done
rc=0; ref="${builds[0]}"
echo "reference: $ref  sha=$(sha256sum "$WORK/$ref.out" | cut -c1-16)"
echo "── built Linux legs (cross-stdlib when >1 leg; -O{0,3}×-ffp{off,fast} corners always) ──"
for ctag in "${builds[@]}"; do
  if cmp -s "$WORK/$ref.out" "$WORK/$ctag.out"; then st=IDENTICAL; else st=DIVERGENT; rc=1; fi
  printf "  %-28s %s\n" "$ctag" "$st"
done
# CROSS-OS anchor (MSVC): the corpus PORTION of the reference (before the reservoir marker) must equal
# the committed golden, which the windows probe verifies == real MSVC. The golden is corpus-only; the
# reservoir's cross-OS is F5-M8-gated, so it rides the built legs only, NOT this MSVC anchor.
if [ "$DETERMINISM_MSVC" = "1" ]; then
  echo "── MSVC cross-OS anchor (corpus vs determinism_golden.txt) ──"
  if [ -f "$GOLDEN_TXT" ]; then
    sed '/^### --reservoir-nearfull/,$d' "$WORK/$ref.out" >"$WORK/$ref.corpus"
    if cmp -s "$GOLDEN_TXT" "$WORK/$ref.corpus"; then printf "  %-28s %s\n" "msvc(golden)" "IDENTICAL"
    else printf "  %-28s %s\n" "msvc(golden)" "DIVERGENT — corpus differs from the cross-OS golden"; rc=1; fi
  else
    echo "  msvc(golden)  MISSING $GOLDEN_TXT — cannot assert cross-OS"; rc=1
  fi
fi
[ "${#LINUX_LEGS[@]}" -ge 2 ] || {
  echo "NOTE: 1 built leg ('${LINUX_LEGS[*]}') — corpus is cross-OS-anchored to MSVC, but the F5-M8"
  echo "  RESERVOIR cross-stdlib repro needs >=2 built legs (restore gcc via DETERMINISM_LEGS @ gcc-15.3)."
}
if [ $rc -eq 0 ]; then
  echo "PASS: byte-identical across ${#builds[@]} built cell(s)$([ "$DETERMINISM_MSVC" = 1 ] && echo ' + MSVC golden') —"
  echo "  corpus (cross-stdlib + cross-OS) and --reservoir-nearfull."
else
  echo "FAIL: determinism divergence."
  echo "  vs msvc(golden) ⇒ CROSS-OS break; vs another built cell ⇒ cross-stdlib or -O/-ffp break."
  echo "  a '### --reservoir-nearfull' divergence across stdlibs ⇒ the F5-M8 item-reservoir leak"
  echo "  (insight_determinism_model.md §F5-M8 — RELEASE-BLOCKING for the eidos M=128 batch-diff)."
  echo "  Localize: diff \$WORK/<ctag>.out vs \$WORK/$ref.out."
fi
exit $rc
