#!/usr/bin/env bash
# Determinism gate — the cross-stdlib DIAGONAL bit-identity of the MetaLog document.
#
# Compiles scripts/determinism_fixture.cpp (+ canon & metalog sources, self-contained)
# on the TWO real toolchain legs and asserts the serialized document is byte-identical
# across them:
#   - g++     (gcc-15, ship) + libstdc++   — deps from build-gcc15-release/
#   - clang++ (clang-21, dev) + libc++     — deps from build/ (the libc++ dev default)
#
# This IS the determinism diagonal (insight_determinism_model.md §F5 / cxx_modules_
# migration_contract.md §5): the strongest bit-identity oracle, because the two legs
# differ in BOTH compiler AND stdlib — it hunts the libm/FMA/reassociation divergences
# AND the unordered_*/iteration-order ones (a libstdc++↔libc++ map-order bug shows up
# here). It is the cross-compiler/stdlib proxy for the cross-arch (x64+arm) gate; the
# in-suite DeterminismGate golden pins the same artifact per build.
#
# Each leg links its OWN stdlib's fmt/spdlog/simdjson .a — a libc++ archive cannot link
# into a libstdc++ binary (the __1 vs __cxx11 std::string ABI). Includes are shared: the
# dep HEADERS are version-identical and stdlib-agnostic; only the archives differ. The
# determinism config (-O3 -march=x86-64-v2 -ffp-contract=off) mirrors the
# linux-{gcc15,clang21-libcxx}-release profiles.
#
# Populate BOTH builds first — gcc15 leg FIRST, libc++ leg LAST, canon→metalog within
# each leg. The order matters because of a known CMakeDeps leak:
#     ( cd insight-canon   && malf build --profile linux-gcc15-release )  # ship  -> build-gcc15-release/
#     ( cd insight-metalog && malf build --profile linux-gcc15-release )
#     ( cd insight-canon   && malf build )                                # libc++ default -> build/
#     ( cd insight-metalog && malf build )
# WHY THE ORDER (CMakeDeps leak — the CMakeConfigDeps migration is expected to fix it):
#   (1) malf refreshes the UNKEYED build/compile_commands.json for clangd on EVERY build,
#       so build/ reflects the LAST build's profile — the libc++ leg MUST build last so
#       build/ is libc++ (build-gcc15-release/ is the keyed, stable libstdc++ leg).
#   (2) canon's editable CMakeDeps reflects ITS last build, so building canon→metalog
#       consecutively keeps metalog's transitive fmt/spdlog on the same stdlib.
# Until CMakeConfigDeps lands, this gate guards explicitly: a wrong-stdlib (libstdc++)
# archive reaching the libc++ leg hard-errors below rather than emitting a cryptic link
# failure.
# Run locally or via the superproject CI (.github/workflows/determinism-gate.yml).
# Exit non-zero on any divergence, or if either diagonal leg fails to build (a one-leg
# gate is hollow — the cross-stdlib property would be unverified).
set -uo pipefail
META="$(cd "$(dirname "$0")/.." && pwd)"

# extract_flags <meta/build*/compile_commands.json> -> emits 3 lines: canon dir / defs /
# include flags. Unions canon's include dirs (canon-only deps, e.g. simdjson) taken from
# the SAME build-variant subdir, so the dep include (and hence archive) paths match this
# leg's stdlib. Read separately so multi-word vars are not word-split.
extract_flags() {
  python3 - "$1" <<'PY'
import json, os, re, sys
cc = sys.argv[1]
subdir = os.path.basename(os.path.dirname(cc))  # "build" (libc++) or "build-gcc15-release"
def flags(path):
    cmds = json.load(open(path))
    e = next((c for c in cmds if c['file'].endswith(('metalog_engine.cpp', 'drain.cpp'))), cmds[0])
    cmd = e.get('command') or ' '.join(e.get('arguments', []))
    defs = re.findall(r'-D\S+', cmd)
    incs = [a or b for a, b in re.findall(r'-I *([^ ]+)|-isystem *([^ ]+)', cmd)]
    return defs, incs
mdefs, mincs = flags(cc)
canon_api = next((i for i in mincs if i.endswith('insight-canon/api')), '')
canon = os.path.dirname(canon_api)
incs = list(mincs)
canon_cc = os.path.join(canon, subdir, 'compile_commands.json')
if os.path.isfile(canon_cc):
    _, cincs = flags(canon_cc)
    for i in cincs:
        if i not in incs:
            incs.append(i)
defs = [d for d in mdefs if not d.startswith('-DSPDLOG_ACTIVE_LEVEL')]
print(canon)
print(' '.join(defs + ['-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_OFF']))
print(' '.join(('-isystem ' + i) for i in incs))
PY
}

# dep_libs <incs> -> the fmt/spdlog/simdjson static archives sitting under those include
# dirs (sibling lib/ of each .../p/include). Stdlib-specific: derived from THIS leg's incs.
dep_libs() {
  local incs="$1" libs="" dep inc a
  for dep in spdlo fmt simdj; do
    for inc in $(echo "$incs" | tr ' ' '\n' | grep -E "/${dep}[^/]*/p/include$"); do
      a=$(ls "${inc%/include}"/lib/lib*.a 2>/dev/null | head -1)
      [ -n "$a" ] && libs="$libs $a"
    done
  done
  echo "-Wl,--start-group $libs -Wl,--end-group -pthread"
}

CC_CXX="$META/build/compile_commands.json"               # libc++ (dev default)
CC_STD="$META/build-gcc15-release/compile_commands.json" # libstdc++ (ship)
[ -f "$CC_CXX" ] || { echo "no $CC_CXX — run 'malf test' (libc++ default) first"; exit 1; }
[ -f "$CC_STD" ] || { echo "no $CC_STD — run 'malf test --profile linux-gcc15-release' first"; exit 1; }

{ read -r CANON; read -r DEFS; read -r INCS_CXX; } < <(extract_flags "$CC_CXX")
{ read -r _;     read -r _;    read -r INCS_STD; } < <(extract_flags "$CC_STD")
# Compile includes are shared (headers are version-identical across the two builds);
# only the linked archives are stdlib-specific.
INCS="-I$CANON/src -I$META/src $INCS_CXX"
LIBS_CXX="$(dep_libs "$INCS_CXX")"
LIBS_STD="$(dep_libs "$INCS_STD")"

# Guard the CMakeDeps leak (header): if build/ was clobbered to libstdc++ (a later
# gcc15 build) or the editable transitive dep crossed caches, the libc++ leg would link
# a gcc15-release (libstdc++) archive — which cannot satisfy a libc++ binary (__cxx11 vs
# __1, std::__throw_system_error). Fail loudly with the fix, not a cryptic linker error.
case "$LIBS_CXX" in
  *gcc15-release*)
    echo "GATE SETUP FAIL: the libc++ leg references a gcc15-release (libstdc++) archive —"
    echo "  build/ is not a clean libc++ build. Rebuild the libc++ leg LAST (canon then"
    echo "  metalog): see the populate order in this script's header. [CMakeDeps leak]"
    exit 2 ;;
esac
grep -q -- '-stdlib=libc++' "$CC_CXX" || {
  echo "GATE SETUP FAIL: $CC_CXX has no -stdlib=libc++ — build/ reflects a non-libc++ build"
  echo "  (malf refreshes it for clangd on every build). Rebuild the libc++ leg LAST."; exit 2; }

SRCS="$(find "$CANON/src" "$META/src" -name '*.cpp')"
# Committed, license-clean, hermetic corpus (no external dataset / network). Local and CI
# tokenize the identical input, so a local PASS guarantees the CI corpus.
CORPUS="$(ls "$META"/scripts/determinism_corpus/*.log 2>/dev/null | sort)"
[ -n "$CORPUS" ] || { echo "no corpus under $META/scripts/determinism_corpus"; exit 1; }
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
echo "canon=$CANON  libstdc++-libs=$(echo $LIBS_STD | grep -o '/[^ ]*\.a' | wc -l)  libc++-libs=$(echo $LIBS_CXX | grep -o '/[^ ]*\.a' | wc -l)  corpus=$(echo "$CORPUS" | wc -l) files"

# The two diagonal legs — the real ship + dev toolchains, each on its own stdlib + deps.
# "tag|compiler|extra-cxxflags|libs"
FLAGS="-std=c++23 -O3 -march=x86-64-v2 -ffp-contract=off"
legs=(
  "gcc15-libstdcxx|g++|$FLAGS|$LIBS_STD"
  "clang21-libcxx|clang++|$FLAGS -stdlib=libc++|$LIBS_CXX"
)
builds=()
for leg in "${legs[@]}"; do
  IFS='|' read -r tag cxx cxxflags libs <<<"$leg"
  command -v "$cxx" >/dev/null || { echo "MISSING COMPILER: $cxx (leg $tag) — cannot run the diagonal"; continue; }
  if $cxx $cxxflags $DEFS $INCS $SRCS "$META/scripts/determinism_fixture.cpp" $libs -o "$WORK/$tag" 2>"$WORK/$tag.log"; then
    builds+=("$tag")
  else echo "BUILD FAIL: $tag"; tail -3 "$WORK/$tag.log" | sed 's/^/   /'; fi
done

# Gate integrity: the diagonal needs BOTH legs. A single leg verifies nothing about the
# cross-stdlib property — refuse to report a hollow green.
if [ "${#builds[@]}" -ne 2 ]; then
  echo "GATE INTEGRITY FAIL: the diagonal needs BOTH legs (gcc15/libstdc++ AND clang21/libc++); ${#builds[@]} built."
  exit 3
fi

for tag in "${builds[@]}"; do
  : >"$WORK/$tag.out"
  for f in $CORPUS; do echo "### $(basename "$f") ###" >>"$WORK/$tag.out"; "$WORK/$tag" "$f" >>"$WORK/$tag.out" 2>/dev/null; done
done
ref="$WORK/${builds[0]}.out"; rc=0
echo "reference: ${builds[0]}  sha=$(sha256sum "$ref" | cut -c1-16)"
for tag in "${builds[@]}"; do
  if cmp -s "$ref" "$WORK/$tag.out"; then st=IDENTICAL; else st=DIVERGENT; rc=1; fi
  printf "  %-22s %s\n" "$tag" "$st"
done
if [ $rc -eq 0 ]; then echo "PASS: MetaLog document byte-identical across the gcc-15/libstdc++ ≡ clang-21/libc++ diagonal."
else echo "FAIL: cross-stdlib divergence — a determinism regression on the diagonal."; fi
exit $rc
