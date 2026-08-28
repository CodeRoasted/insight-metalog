#pragma once

// json_egress.hpp — `insight_metalog`'s ONE JSON write entry point.
//
// THE RULE (DN-65.D2). The Glaze write entry points (`glz::write`, `glz::write_json`,
// `glz::write_file_json`, and any sibling that emits) may appear in exactly ONE file per package:
// this one. Every other site in the package writes through the wrapper below and cannot supply raw
// opts, because the escape member is forced HERE rather than requested THERE.
//
// WHY THE ENTRY POINT IS THE KEY, AND THE OPTS SPELLING IS NOT. Opts are OPTIONAL and come in four
// spellings — a named `constexpr`, a struct derived from `glz::opts`, an inline temporary, and none
// at all. A write cannot happen without an entry point, so the entry point is the only axis on
// which the enumeration of writes is complete, and completeness is what makes a gate a gate. A rule
// keyed on the opts spelling was written first and falsified before any code was written: it fired
// on the two writers in the workspace that were already correct and never fired on the live defect
// (DN-65.O4).
//
// WHY FORCED RATHER THAN DEFAULTED. Glaze does not escape U+0000..U+001F by default and the safe
// form requires deriving a new opts type, so every emitter decided for itself and 5 of the
// workspace's 7 egress writers took the default. `glz::opt_true` re-derives the caller's own opts
// type with the escape member set — the caller keeps `prettify` / `skip_null_members` and cannot
// spell the escape member false.
//
// SCOPE OF WHAT THE OPTION BUYS. Glaze's `char_escape_table` is consulted first and
// unconditionally, so \b \t \n \f \r (0x08 0x09 0x0A 0x0C 0x0D) already emit as short escapes
// whatever the option says; the option governs the other 27 of the 32 C0 bytes. That asymmetry is
// why the gate in `tests/serialization/test_egress_encoding_conformance.cpp` drives all 32 rather
// than a sample — a probe restricted to the five would be green and vacuous.

#include <string>

#include <glaze/glaze.hpp>

namespace insight::metalog::json_egress
{

// The caller's own opts type, re-derived with `escape_control_characters` set. Glaze's `opt_true`
// assigns the member when the caller's type already carries it and derives a new type when it does
// not, so all four caller spellings land on the same forced value.
template <auto Opts>
inline constexpr auto conformant = glz::opt_true<Opts, glz::escape_control_characters_opt_tag{}>;

// Serialise `value` to JSON. Postcondition: the result is RFC 8259-conformant for every string
// input, including log-derived bytes below 0x20 (DN-65.D1 — an egress surface owns the legality of
// the bytes it emits, unconditionally and with no upstream precondition).
template <auto Opts = glz::opts{}, class Value>
[[nodiscard]] std::string to_string(const Value& value)
{
    std::string buffer;
    // A fully-formed value written into a growable string has no reachable failure: the error
    // channel carries buffer exhaustion (fixed-capacity buffers only) and user-writer errors,
    // neither of which exists on this path.
    (void)glz::write<conformant<Opts>>(value, buffer);
    return buffer;
}

} // namespace insight::metalog::json_egress
