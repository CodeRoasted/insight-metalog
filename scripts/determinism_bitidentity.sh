#!/usr/bin/env bash
# Determinism gate — the cross-stdlib DIAGONAL bit-identity of the MetaLog document.
#
# Builds the canon+metalog MODULE TOWER on the TWO real toolchain legs and asserts the
# serialized document is byte-identical across them:
#   - g++     (gcc-15, ship) + libstdc++   (profile linux-gcc15-release)
#   - clang++ (clang-21, dev) + libc++      (profile linux-clang21-libcxx-release)
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

# The two diagonal legs: "tag:cxx-bin:cc-bin:conan-profile". Each builds the tower under its own
# compiler+stdlib via the conan toolchain; -ffp-contract=off is also baked PUBLIC into the targets.
legs=(
  "gcc15-libstdcxx:g++-15:gcc-15:linux-gcc15-release"
  "clang21-libcxx:clang++-21:clang-21:linux-clang21-libcxx-release"
)
declare -A BIN
builds=()
for leg in "${legs[@]}"; do
  IFS=: read -r tag cxxbin ccbin profile <<<"$leg"
  command -v "$cxxbin" >/dev/null || { echo "MISSING COMPILER: $cxxbin (leg $tag) — cannot run the diagonal"; continue; }
  [ -f "$CONAN_HOME/profiles/$profile" ] || { echo "MISSING PROFILE: $profile (leg $tag)"; continue; }

  legdir="$WORK/conan-$tag"
  if ! conan install "$META" --profile:host="$profile" --profile:build="$profile" \
        --build=missing -of "$legdir" >"$legdir.install.log" 2>&1; then
    echo "CONAN INSTALL FAIL: $tag"; tail -4 "$legdir.install.log" | sed 's/^/   /'; continue
  fi
  toolchain="$(find "$legdir" -name conan_toolchain.cmake 2>/dev/null | head -1)"
  [ -f "$toolchain" ] || { echo "CONAN INSTALL FAIL: $tag — no conan_toolchain.cmake"; continue; }

  bdir="$WORK/build-$tag"
  # Separate configure from build so a failure names the stage, and print the REAL tail of the
  # failing stage's log (a one-line "BUILD FAIL" with no diagnostics is useless under CI).
  if ! cmake -S "$HARNESS" -B "$bdir" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER="$ccbin" -DCMAKE_CXX_COMPILER="$cxxbin" \
        -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
        -DCANON_ROOT="$CANON" -DMETA_ROOT="$META" \
        -DCELL_FLAGS="-ffp-contract=off -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_OFF" >"$bdir.cfg.log" 2>&1; then
    echo "CONFIGURE FAIL: $tag"; tail -40 "$bdir.cfg.log" | sed 's/^/   /'; continue
  fi
  if ! cmake --build "$bdir" --target det_fixture --parallel "$JOBS" >"$bdir.build.log" 2>&1; then
    echo "BUILD FAIL: $tag"; tail -40 "$bdir.build.log" | sed 's/^/   /'; continue
  fi
  bin="$(find "$bdir" -name det_fixture -type f -perm -u+x 2>/dev/null | head -1)"
  if [ -x "$bin" ]; then builds+=("$tag"); BIN["$tag"]="$bin"; else echo "BUILD FAIL: $tag (no det_fixture produced)"; fi
done

# Gate integrity: the diagonal needs BOTH legs. A single leg verifies nothing about the
# cross-stdlib property — refuse to report a hollow green. DETERMINISM_REQUIRE_COMPILERS (default
# "g++ clang++") is the coverage invariant: every required compiler MUST have produced a build, else
# the gate is a one-compiler hollow green and FAILS (it does NOT silently degrade). [F5-M8]
REQUIRE_COMPILERS="${DETERMINISM_REQUIRE_COMPILERS:-g++ clang++}"
if [ "${#builds[@]}" -ne "${#legs[@]}" ]; then
  echo "GATE INTEGRITY FAIL: DETERMINISM_REQUIRE_COMPILERS='$REQUIRE_COMPILERS' needs all ${#legs[@]} stdlib legs (gcc15/libstdc++ AND clang21/libc++); ${#builds[@]} built."
  exit 3
fi

# Each leg emits the committed corpus (5 files) THEN the F5-M8 synthetic near-full reservoir scenario
# (--reservoir-nearfull). The reservoir section is what would have caught F5-M8: a cross-stdlib flip
# of the M=128 admit/evict boundary shows up as that section DIVERGENT. (The -O{0,3}×-ffp{off,fast}
# corners are an orthogonal FP-hazard sweep — they do NOT perturb stdlib iteration order, so they are
# not what catches F5-M8; tracked separately as the coverage-matrix expansion.)
for tag in "${builds[@]}"; do
  : >"$WORK/$tag.out"
  for f in $CORPUS; do echo "### $(basename "$f") ###" >>"$WORK/$tag.out"; "${BIN[$tag]}" "$f" >>"$WORK/$tag.out" 2>/dev/null; done
  echo "### --reservoir-nearfull (F5-M8 synthetic M=128) ###" >>"$WORK/$tag.out"
  "${BIN[$tag]}" --reservoir-nearfull >>"$WORK/$tag.out" 2>/dev/null
done
ref="$WORK/${builds[0]}.out"; rc=0
echo "reference: ${builds[0]}  sha=$(sha256sum "$ref" | cut -c1-16)"
for tag in "${builds[@]}"; do
  if cmp -s "$ref" "$WORK/$tag.out"; then st=IDENTICAL; else st=DIVERGENT; rc=1; fi
  printf "  %-22s %s\n" "$tag" "$st"
done
if [ $rc -eq 0 ]; then echo "PASS: MetaLog document (corpus + near-full reservoir) byte-identical across the gcc-15/libstdc++ ≡ clang-21/libc++ diagonal."
else
  echo "FAIL: cross-stdlib divergence — a determinism regression on the diagonal."
  echo "  If the DIVERGENT section is '--reservoir-nearfull', that is the F5-M8 item-reservoir"
  echo "  salience leak (insight_determinism_model.md §F5-M8) — RELEASE-BLOCKING for the eidos M=128"
  echo "  batch-diff. Localize: diff the two legs' \$WORK/*.out around that section."
fi
exit $rc
