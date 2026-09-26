#pragma once

#include <cmath>
#include <concepts>
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include <glaze/glaze.hpp>
#include <utf8/well_formed.hpp>

namespace insight::metalog::json_egress
{

// invariant: the Glaze write entry points appear in exactly one file per package, this one; every
// other site writes through to_string below and cannot supply raw opts.
// note: opt_true re-derives the caller's opts type, so prettify and skip_null_members live.
// refs: ADR-26.D12
template <auto Opts>
inline constexpr auto conformant = glz::opt_true<Opts, glz::escape_control_characters_opt_tag{}>;

namespace detail
{

    // post: `inner` prefixed by one path segment: a dot joins a member to a member and nothing
    // joins one to an element, so the root is empty and a path reads `top_k[3].frequency`.
    [[nodiscard]] inline std::string join_path(std::string segment, const std::string& inner)
    {
        if (!inner.empty() && inner.front() != '[')
            segment.push_back('.');
        segment += inner;
        return segment;
    }

    template <class Key> [[nodiscard]] std::string key_segment(const Key& key)
    {
        if constexpr (glz::str_t<Key>)
            return std::string{key};
        else if constexpr (std::integral<Key>)
            return std::to_string(key);
        else
            static_assert(false, "DN-99.D8: a map key the walk cannot spell as a path segment");
    }

    template <class Value>
    [[nodiscard]] std::optional<std::string> find_non_finite(const Value& value);

    // post: the path of the first non-finite value in `map`, its key spelled as the segment.
    template <class Map> [[nodiscard]] std::optional<std::string> find_in_map(const Map& map)
    {
        for (const auto& [key, element] : map)
        {
            if (auto inner{find_non_finite(element)})
                return join_path(key_segment(key), *inner);
        }
        return std::nullopt;
    }

    // post: the path of the first non-finite value in `array`, its index spelled as the segment.
    template <class Array>
    [[nodiscard]] std::optional<std::string> find_in_array(const Array& array)
    {
        std::size_t index{0};
        for (const auto& element : array)
        {
            if (auto inner{find_non_finite(element)})
                return join_path("[" + std::to_string(index) + "]", *inner);
            ++index;
        }
        return std::nullopt;
    }

    // post: member `Index` of a reflectable or `glz::object` struct, read the way glaze's writer
    // reads it.
    template <std::size_t Index, class Struct>
    [[nodiscard]] decltype(auto) member_at(const Struct& value)
    {
        if constexpr (glz::reflectable<Struct>)
            return glz::get_member(value, glz::get<Index>(glz::to_tie(value)));
        else
            return glz::get_member(value, glz::get<Index>(glz::reflect<Struct>::values));
    }

    // invariant: a member's segment is the key glaze writes, so a renamed member is named as
    // written.
    template <class Struct>
    [[nodiscard]] std::optional<std::string> find_in_struct(const Struct& value)
    {
        static_assert(!glz::meta_has_skip<Struct> && !glz::meta_has_skip_if<Struct>,
                      "DN-99.D8: a skip hook would make the walk see a member the writer skips");
        std::optional<std::string> found;
        glz::for_each<glz::reflect<Struct>::size>(
            [&]<std::size_t Index>()
            {
                if (found)
                    return;
                if (auto inner{find_non_finite(member_at<Index>(value))})
                    found = join_path(std::string{glz::reflect<Struct>::keys[Index]}, *inner);
            });
        return found;
    }

    // refs: DN-99.D8
    // post: the path of the first NaN or infinite number in the order glaze writes the document,
    // or nullopt; the path is built only while a hit unwinds, so a finite document allocates none.
    // invariant: every category is glaze's own, and a category the walk does not dispatch fails to
    // compile.
    // note: the recursion depth is the document's nesting, which glaze's writer recurses on too.
    template <class Value>
    [[nodiscard]] std::optional<std::string> find_non_finite(const Value& value)
    {
        using T = std::remove_cvref_t<Value>;
        if constexpr (std::floating_point<T>)
        {
            if (std::isfinite(value))
                return std::nullopt;
            return std::string{};
        }
        else if constexpr (std::integral<T> || std::is_enum_v<T> || glz::str_t<T> ||
                           std::same_as<T, glz::raw_json>)
        {
            return std::nullopt;
        }
        else if constexpr (glz::nullable_t<T>)
        {
            if (!value)
                return std::nullopt;
            return find_non_finite(*value);
        }
        else if constexpr (glz::writable_map_t<T>)
        {
            return find_in_map(value);
        }
        else if constexpr (glz::writable_array_t<T>)
        {
            return find_in_array(value);
        }
        else if constexpr (glz::reflectable<T> || glz::glaze_object_t<T>)
        {
            return find_in_struct(value);
        }
        else
        {
            static_assert(false, "DN-99.D8: a document member of a category the walk does not "
                                 "dispatch");
        }
    }

} // namespace detail

// refs: DN-99.D8, ADR-26.D12, DN-43.D20
// post: RFC 8259-conformant JSON for every string input, including log-derived bytes below 0x20.
// post: well-formed UTF-8, each maximal ill-formed subpart of a string replaced by one U+FFFD.
// post: a NaN or an infinity refuses the document before any byte, the error naming its path.
// note: 5 of the 32 C0 bytes escape via char_escape_table regardless; the option governs 27.
template <auto Opts = glz::opts{}, class Value>
[[nodiscard]] std::expected<std::string, std::string> to_string(const Value& value)
{
    if (auto path{detail::find_non_finite(value)})
        return std::unexpected{std::move(*path)};
    std::string buffer;
    // assert: a walked document written into a growable string has no reachable failure -- the
    // error channel carries fixed-capacity buffer exhaustion and user-writer errors, neither here.
    (void)glz::write<conformant<Opts>>(value, buffer);
    return insight::utf8::replace_ill_formed(std::move(buffer));
}

} // namespace insight::metalog::json_egress
