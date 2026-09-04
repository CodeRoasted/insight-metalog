#!/usr/bin/env bash
# The determinism digest's SECTION WRITER — sourced, never executed. It owns exactly one thing: how
# one section becomes bytes, and what happens when the fixture that produces those bytes FAILS.
#
# ── Why this is a file of its own, and what it deliberately does NOT own
# It does NOT enumerate. The corpus comes off disk and the synthetic roster comes off
# scripts/determinism_sections.txt, and both enumerations stay in the caller
# (scripts/determinism_bitidentity.sh) so there is still exactly ONE enumerator per emitter — the
# property scripts/determinism_sections_check.sh guards. This file is handed the two lists already
# built; it turns them into the digest's bytes.
#
# It is separate so the failure semantics below can be PROVEN at a desk in under a second, against a
# stub binary whose exit code the test chooses — see scripts/determinism_emit_check.sh. While the
# emit loop lived inline in the driver, the only way to reach it was a four-cell conan module-tower
# build, so nothing ever asked what it did with a non-zero exit, and the answer was: nothing.
#
# ── The defect this file exists to close (re-derived at HEAD, 2026-09-04)
# The driver's emit loop ran the fixture as
#     "${BIN[$ctag]}" "$f" >>"$WORK/$ctag.out" 2>/dev/null
# under `set -uo pipefail` — no `-e`, no `||`, no `$?`. The exit status was discarded and stderr was
# sent to /dev/null, so a fixture that exited 2 (its usage error, and its "cannot open" error) wrote
# an EMPTY body under a correctly-spelled header and the driver carried on. All four -O/-ffp cells
# fail the same way, so `cmp` calls the empty bodies IDENTICAL, the driver prints PASS naming every
# section it "emitted", and it writes the truncated digest to DETERMINISM_OUT. The four Linux legs of
# golden.yaml would then agree with each other perfectly.
#
# The MSVC leg never had this hole: golden.yaml's `Add-Section` does
#     if ($p.ExitCode -ne 0) { throw "det_fixture failed (exit $($p.ExitCode)) on section '$Header'" }
# So one gate's five legs disagreed about what counts as a failure, and the only thing standing
# between a broken fixture and a green Linux determinism proof was a metered Windows build.
#
# ── The contract
# det_emit_section <fixture> <digest-file> <header-text> <fixture-argument>
#   Appends `### <header-text> ###\n` (LF, ASCII) then the fixture's RAW stdout to <digest-file>.
#   Returns the fixture's exit status. Non-zero is reported on stderr naming the header, the status
#   and the argument. The fixture's stderr is surfaced whenever it is non-empty and NEVER enters the
#   digest — that matches the MSVC leg, where Start-Process redirects only stdout and stderr is
#   inherited by the runner's console.
#
# det_emit_digest <fixture> <digest-file> <corpus-newline-list> <section-row>...
#   Truncates <digest-file>, then writes the corpus sections in the order given followed by the
#   synthetic rows in the order given. Order is load-bearing: five digests are compared byte for
#   byte, so a reordered emission is a divergence. Returns 0, or the first failing section's status —
#   FAIL-FAST, matching the MSVC leg's throw. A caller that continues past a non-zero return is
#   re-opening the hole.
#
# Both functions are pure appenders over stdout bytes; neither reads a roster, a directory or an
# environment variable, which is what makes the check script's stub substitution honest.

det_emit_section() {
    local fixture="$1" out="$2" header="$3" arg="$4"
    printf '### %s ###\n' "$header" >>"$out" || return 2

    local err rc=0
    err="$(mktemp)" || return 2
    "$fixture" "$arg" >>"$out" 2>"$err" || rc=$?

    if [ -s "$err" ]; then
        echo "  det_fixture stderr on section '$header':" >&2
        sed 's/^/    /' "$err" >&2
    fi
    rm -f "$err"

    if [ "$rc" -ne 0 ]; then
        echo "FIXTURE FAIL: det_fixture exited $rc on section '$header' (argument '$arg')" >&2
        echo "  Its body is whatever it wrote before failing — on the fixture's two error paths," >&2
        echo "  the usage error and 'cannot open', that is nothing at all. Every cell and every leg" >&2
        echo "  fails the same way, and a byte-compare of empty bodies is not agreement." >&2
        return "$rc"
    fi
    return 0
}

det_emit_digest() {
    local fixture="$1" out="$2" corpus="$3"
    shift 3
    : >"$out" || return 2

    local path
    while IFS= read -r path; do
        [ -n "$path" ] || continue
        det_emit_section "$fixture" "$out" "$(basename "$path")" "$path" || return $?
    done <<<"$corpus"

    local row
    for row in "$@"; do
        det_emit_section "$fixture" "$out" "$row" "${row%% *}" || return $?
    done
    return 0
}
