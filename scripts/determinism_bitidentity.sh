#!/usr/bin/env bash
# Determinism gate — cross-compiler bit-identity of the MetaLog document.
#
# Compiles scripts/determinism_fixture.cpp (canon + metalog, self-contained)
# across the gcc x clang x -O{0,2,3} x -ffp-contract={off,fast} matrix — the
# divergence sources hardened against (libm, FMA, reassociation, opt level) —
# runs each build on the committed corpus under scripts/determinism_corpus/, and
# asserts the serialized document is byte-identical across every build. It is the
# cross-compiler proxy for the cross-arch (x64 + arm) gate; the in-suite
# DeterminismGate golden test pins the same artifact per build. Run locally or via
# the superproject CI (.github/workflows/determinism-gate.yml).
# See technical_docs/architecture/insight_determinism_model.md.
#
# Build flags/includes are read from the package's compile_commands.json, so dep
# versions/paths track the real build (run `malf build` once first to populate it).
# Exit non-zero on any divergence. clang-21 (the dev default AND the bare `clang++`)
# defines __cpp_concepts=202002 natively, so std::expected compiles with no macro hack.
#
# Set DETERMINISM_REQUIRE_COMPILERS="g++ clang++" (CI) to fail unless every listed
# compiler actually built — else a clang-only break would pass on the g++ builds alone.
# Post dev-flip, CI pins clang++ -> clang-21 (the dev compiler), so the gate proves
# gcc-15 (ship) ≡ clang-21 (dev) byte-identical on the real toolchains.
set -uo pipefail
META="$(cd "$(dirname "$0")/.." && pwd)"
CC="$META/build/compile_commands.json"
[ -f "$CC" ] || { echo "no $CC — run 'malf test' first"; exit 1; }

# python emits three lines: canon repo dir, defines, include flags. We union the
# include dirs from BOTH metalog's and canon's compile_commands so canon-only deps
# (e.g. simdjson) are covered. Read separately so multi-word vars aren't split.
{ read -r CANON; read -r DEFS; read -r INCS; } < <(python3 - "$CC" <<'PY'
import json, os, re, sys
def flags(path):
    cmds = json.load(open(path))
    e = next((c for c in cmds if c['file'].endswith(('metalog_engine.cpp', 'drain.cpp'))), cmds[0])
    cmd = e.get('command') or ' '.join(e.get('arguments', []))
    defs = re.findall(r'-D\S+', cmd)
    incs = [a or b for a, b in re.findall(r'-I *([^ ]+)|-isystem *([^ ]+)', cmd)]
    return defs, incs
mdefs, mincs = flags(sys.argv[1])
canon_api = next((i for i in mincs if i.endswith('insight-canon/api')), '')
canon = os.path.dirname(canon_api)
incs = list(mincs)
canon_cc = os.path.join(canon, 'build', 'compile_commands.json')
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
)
INCS="-I$CANON/src -I$META/src $INCS"

# Static libs: sibling lib/ of each fmt/spdlog/simdjson include dir.
LIBS=""
for dep in spdlo fmt simdj; do
  for inc in $(echo "$INCS" | tr ' ' '\n' | grep -E "/${dep}[^/]*/p/include$"); do
    a=$(ls "${inc%/include}"/lib/lib*.a 2>/dev/null | head -1)
    [ -n "$a" ] && LIBS="$LIBS $a"
  done
done
LIBS="-Wl,--start-group $LIBS -Wl,--end-group -pthread"

SRCS="$(find "$CANON/src" "$META/src" -name '*.cpp')"
# Committed, license-clean, hermetic corpus (no external dataset / network). Local
# and CI tokenize the identical input, so a local PASS guarantees the CI corpus.
CORPUS="$(ls "$META"/scripts/determinism_corpus/*.log 2>/dev/null | sort)"
[ -n "$CORPUS" ] || { echo "no corpus under $META/scripts/determinism_corpus"; exit 1; }
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
echo "canon=$CANON  libs=$(echo $LIBS | grep -o '/[^ ]*\.a' | wc -l)  corpus=$(echo "$CORPUS" | wc -l) files"

builds=()
for cxx in g++ clang++; do
  command -v "$cxx" >/dev/null || { echo "skip $cxx (not installed)"; continue; }
  for opt in -O0 -O2 -O3; do for fpc in off fast; do
    tag="${cxx//+/p}_${opt#-}_${fpc}"
    if $cxx -std=c++23 "$opt" -ffp-contract="$fpc" $DEFS $INCS $SRCS \
        "$META/scripts/determinism_fixture.cpp" $LIBS -o "$WORK/$tag" 2>"$WORK/$tag.log"; then
      builds+=("$tag")
    else echo "BUILD FAIL: $tag"; tail -2 "$WORK/$tag.log" | sed 's/^/   /'; fi
  done; done
done
[ "${#builds[@]}" -gt 0 ] || { echo "no builds succeeded"; exit 1; }

# Gate-integrity guard. DETERMINISM_REQUIRE_COMPILERS lists compilers that MUST each
# have produced at least one successful build (e.g. "g++ clang++" in CI). Without it a
# clang-only compile break — like the glaze anon-namespace regression — would
# pass silently on the g++ builds alone, leaving a hollow green gate. Local proxy
# runs leave it unset and degrade to whatever compilers are installed.
for req in ${DETERMINISM_REQUIRE_COMPILERS:-}; do
  pfx="${req//+/p}_"
  printf '%s\n' "${builds[@]}" | grep -q "^$pfx" || {
    echo "GATE INTEGRITY FAIL: required compiler '$req' produced no successful build"
    echo "  — the gate would be hollow (cross-compiler property unverified). See build logs above."
    exit 3
  }
done

for tag in "${builds[@]}"; do
  : >"$WORK/$tag.out"
  for f in $CORPUS; do echo "### $(basename "$f") ###" >>"$WORK/$tag.out"; "$WORK/$tag" "$f" >>"$WORK/$tag.out" 2>/dev/null; done
done
ref="$WORK/${builds[0]}.out"; rc=0
echo "reference: ${builds[0]}  sha=$(sha256sum "$ref" | cut -c1-16)"
for tag in "${builds[@]}"; do
  if cmp -s "$ref" "$WORK/$tag.out"; then st=IDENTICAL; else st=DIVERGENT; rc=1; fi
  printf "  %-26s %s\n" "$tag" "$st"
done
if [ $rc -eq 0 ]; then echo "PASS: MetaLog document byte-identical across ${#builds[@]} builds."
else echo "FAIL: cross-build divergence — a determinism regression."; fi
exit $rc
