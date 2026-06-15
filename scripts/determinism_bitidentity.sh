#!/usr/bin/env bash
# Determinism gate — the cross-stdlib × optimization-corner MATRIX bit-identity of the MetaLog document.
#
# Builds the canon+metalog MODULE TOWER across a CELL MATRIX and asserts the serialized document
# (the committed corpus PLUS the F5-M8 synthetic near-full reservoir scenario) is byte-identical
# across every cell. The matrix = two stdlib legs × four optimization corners:
#   - g++     (gcc-15, ship) + libstdc++   (profile linux-gcc15-release)
#   - clang++ (clang-21, dev) + libc++      (profile linux-clang21-libcxx-release)
#   × -O{0,3} × -ffp-contract{off,fast}  (the FP-contraction/optimization corners; F5-M8 invariant).
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
# ⚠ gcc LEG DEFERRED (Founder, 2026-06-15 — ROADMAP § Bugs Encountered): the gcc profile pins
# from-source gcc-15.3 under /opt (PR124309: the module tower ICEs on the PPA's gcc-15.2), and CI has
# no /opt/gcc-15.3 provisioning yet. So the cross-stdlib gcc≡clang diagonal — and with it the F5-M8
# cross-stdlib repro on this gate — is on hold; for now CROSS-OS determinism is asserted clang-21/libc++
# (Linux) ↔ MSVC (Windows) by the per-repo windows-portability-probe.yml. This clang-only run still
# gates the -O/-ffp corners. Re-enable the gcc leg below (and DETERMINISM_REQUIRE_COMPILERS) once CI
# provisions gcc-15.3 — or once the ubuntu-toolchain-r PPA ships 15.3 (the profile's planned swap-back).
# leg = "tag:cxx-bin:cc-bin:conan-profile"; cell = "name:flags".
legs=(
  # DEFERRED until CI provisions gcc-15.3 (see ⚠ above): "gcc15-libstdcxx:g++-15:gcc-15:linux-gcc15-release"
  "clang21-libcxx:clang++-21:clang-21:linux-clang21-libcxx-release"
)
cells=(
  "O3_off:-O3 -ffp-contract=off"
  "O0_off:-O0 -ffp-contract=off"
  "O3_fast:-O3 -ffp-contract=fast"
  "O0_fast:-O0 -ffp-contract=fast"
)
declare -A BIN
declare -A LEG_BUILT
builds=()
for leg in "${legs[@]}"; do
  IFS=: read -r tag cxxbin ccbin profile <<<"$leg"
  [ -f "$CONAN_HOME/profiles/$profile" ] || { echo "MISSING PROFILE: $profile (leg $tag)"; continue; }

  # Preflight: the PROFILE is the source of truth for the compiler (its [buildenv] CXX), NOT the
  # leg-array `cxxbin` — e.g. linux-gcc15-release pins CXX=/opt/gcc-15.3/bin/g++ (from-source 15.3,
  # PR124309) which the runner may not have provisioned. Check the profile's compiler exists and FAIL
  # FAST with the path, instead of letting conan invoke a missing compiler and surfacing it as a
  # buried "fmt cmake.configure Error 1" 1000 lines deep. (Was lost on the gcc-15.3 CI-drift, 2026-06-15.)
  prof_cxx="$(sed -nE 's/^[[:space:]]*CXX[[:space:]]*=[[:space:]]*//p' "$CONAN_HOME/profiles/$profile" | tail -1)"
  if [ -n "$prof_cxx" ] && [ ! -x "$prof_cxx" ]; then
    echo "MISSING COMPILER: $prof_cxx — profile '$profile' [buildenv] points here but the runner has no"
    echo "  such binary (PPA gcc-15 is /usr/bin/gcc-15 @ 15.2; this profile wants from-source 15.3 under"
    echo "  /opt for PR124309). PROVISION /opt/gcc-15.3 in CI, or point the profile at the PPA toolchain."
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

# Gate integrity: every configured leg must build EVERY cell (no hollow one-corner green). A required
# compiler missing (or a cell that failed to build, e.g. a toolchain ICE) FAILS the gate; it does NOT
# silently degrade to fewer cells. DETERMINISM_REQUIRE_COMPILERS default is "clang++" WHILE THE gcc LEG
# IS DEFERRED (gcc-15.3 not in CI — see the ⚠ note at `legs`); restore "g++ clang++" when it returns. [F5-M8]
REQUIRE_COMPILERS="${DETERMINISM_REQUIRE_COMPILERS:-clang++}"
expected=$(( ${#legs[@]} * ${#cells[@]} ))
for leg in "${legs[@]}"; do
  IFS=: read -r tag _ _ _ <<<"$leg"
  [ "${LEG_BUILT[$tag]:-0}" -eq "${#cells[@]}" ] ||
    echo "GATE INTEGRITY: leg $tag built ${LEG_BUILT[$tag]:-0}/${#cells[@]} cells"
done
if [ "${#builds[@]}" -ne "$expected" ]; then
  echo "GATE INTEGRITY FAIL: DETERMINISM_REQUIRE_COMPILERS='$REQUIRE_COMPILERS' needs all $expected cells"
  echo "  (${#legs[@]} stdlib legs × ${#cells[@]} -O/-ffp corners); built ${#builds[@]}. See the FAIL tails above."
  exit 3
fi

# Each cell emits the committed corpus (5 files) THEN the F5-M8 synthetic near-full reservoir scenario
# (--reservoir-nearfull), and EVERY cell must be byte-identical to the reference. A DIVERGENT
# '--reservoir-nearfull' section on a cross-STDLIB pair is the F5-M8 item-reservoir leak; a divergence
# that tracks the -ffp corner is an FP-contraction leak in the det_math/salience path.
for ctag in "${builds[@]}"; do
  : >"$WORK/$ctag.out"
  for f in $CORPUS; do echo "### $(basename "$f") ###" >>"$WORK/$ctag.out"; "${BIN[$ctag]}" "$f" >>"$WORK/$ctag.out" 2>/dev/null; done
  echo "### --reservoir-nearfull (F5-M8 synthetic M=128) ###" >>"$WORK/$ctag.out"
  "${BIN[$ctag]}" --reservoir-nearfull >>"$WORK/$ctag.out" 2>/dev/null
done
ref="$WORK/${builds[0]}.out"; rc=0
echo "reference: ${builds[0]}  sha=$(sha256sum "$ref" | cut -c1-16)"
for ctag in "${builds[@]}"; do
  if cmp -s "$ref" "$WORK/$ctag.out"; then st=IDENTICAL; else st=DIVERGENT; rc=1; fi
  printf "  %-28s %s\n" "$ctag" "$st"
done
if [ $rc -eq 0 ]; then
  echo "PASS: MetaLog document (corpus + near-full reservoir) byte-identical across all $expected cells"
  echo "  (gcc-15/libstdc++ ≡ clang-21/libc++ × -O{0,3} × -ffp-contract{off,fast})."
else
  echo "FAIL: determinism divergence across the cell matrix."
  echo "  DIVERGENT on a cross-STDLIB pair + section '--reservoir-nearfull' ⇒ the F5-M8 item-reservoir"
  echo "  salience leak (insight_determinism_model.md §F5-M8) — RELEASE-BLOCKING for the eidos M=128"
  echo "  batch-diff. DIVERGENT tracking the -ffp corner ⇒ an FP-contraction leak in det_math/salience."
  echo "  Localize: diff the divergent cell's \$WORK/<ctag>.out vs \$WORK/${builds[0]}.out."
fi
exit $rc
