#pragma once

#include <string>

#include <glaze/glaze.hpp>

namespace insight::metalog::json_egress
{

// invariant: the Glaze write entry points appear in exactly one file per package, this one; every
// other site writes through to_string below and cannot supply raw opts.
// note: opt_true re-derives the caller's opts type, so prettify and skip_null_members live.
// refs: DN-65.D2, DN-65.O4
template <auto Opts>
inline constexpr auto conformant = glz::opt_true<Opts, glz::escape_control_characters_opt_tag{}>;

// post: RFC 8259-conformant JSON for every string input, including log-derived bytes below 0x20,
// with no upstream precondition.
// note: 5 of the 32 C0 bytes escape via char_escape_table regardless; the option governs 27.
// refs: DN-65.D1
template <auto Opts = glz::opts{}, class Value>
[[nodiscard]] std::string to_string(const Value& value)
{
    std::string buffer;
    // assert: a fully-formed value written into a growable string has no reachable failure -- the
    // error channel carries fixed-capacity buffer exhaustion and user-writer errors, neither here.
    (void)glz::write<conformant<Opts>>(value, buffer);
    return buffer;
}

} // namespace insight::metalog::json_egress
