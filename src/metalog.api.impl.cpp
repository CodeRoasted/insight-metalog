module;
#include <utf8/well_formed.hpp>

module insight.metalog.api;
import insight.metalog.internal;
import insight.canon;

namespace insight::metalog
{

namespace
{
    // invariant: a UTF-8 continuation byte, 0x80..0xBF: never the first byte of a code point.
    [[nodiscard]] constexpr bool is_continuation_byte(char byte) noexcept
    {
        constexpr unsigned kContinuationMask{0xC0U};
        constexpr unsigned kContinuationTag{0x80U};
        return (static_cast<unsigned char>(byte) & kContinuationMask) == kContinuationTag;
    }

    void refuse_invalid_text_cap(std::size_t max_text_bytes, std::string_view caller)
    {
        if (!valid_max_text_bytes(max_text_bytes))
            throw std::invalid_argument{
                std::string{caller} + ": max_text_bytes " + std::to_string(max_text_bytes) +
                " is not a power of two in [" + std::to_string(kMinTextBytes) + ", " +
                std::to_string(kMaxTextBytes) + "]"};
    }

    // pre: text.size() >= max_text_bytes, and text is well-formed when it is a whole value; a
    // digest form's prefix is well-formed by construction.
    // post: the longest prefix of text of at most L - 17 bytes whose next byte is not a
    // continuation byte, followed by the tilde run that brings it to L - 16 bytes.
    void append_capped_prefix(std::string& out, std::string_view text, std::size_t max_text_bytes)
    {
        std::size_t prefix{max_text_bytes - kTextDigestHexChars - 1U};
        while (prefix > 0 && is_continuation_byte(text[prefix]))
            --prefix;
        out.append(text.substr(0, prefix));
        out.append(max_text_bytes - kTextDigestHexChars - prefix, '~');
    }
} // namespace

std::string cap_text(std::string_view value, std::size_t max_text_bytes)
{
    refuse_invalid_text_cap(max_text_bytes, "cap_text");
    std::string whole{insight::utf8::replace_ill_formed(std::string{value})};
    if (whole.size() < max_text_bytes)
        return whole;
    constexpr std::string_view kHexDigits{"0123456789abcdef"};
    constexpr unsigned kNibbleBits{4U};
    constexpr unsigned kNibbleMask{0xFU};
    const auto digest{insight::sha256_of(whole)};
    std::string out;
    out.reserve(max_text_bytes);
    append_capped_prefix(out, whole, max_text_bytes);
    for (std::size_t index{0}; index < kTextDigestHexChars / 2U; ++index)
    {
        out.push_back(
            kHexDigits[(static_cast<unsigned>(digest[index]) >> kNibbleBits) & kNibbleMask]);
        out.push_back(kHexDigits[static_cast<unsigned>(digest[index]) & kNibbleMask]);
    }
    return out;
}

std::string recap_text(std::string_view member, std::optional<std::size_t> declared,
                       std::size_t target)
{
    refuse_invalid_text_cap(target, "recap_text");
    if (declared.has_value() && target >= *declared)
        throw std::invalid_argument{"recap_text: target " + std::to_string(target) +
                                    " is not below the declared max_text_bytes " +
                                    std::to_string(*declared)};
    if (member.size() < target)
        return std::string{member};
    if (!is_text_digest_form(member, declared))
        return cap_text(member, target);
    // assert: target <= declared / 2, so the cut reads only bytes inside the prefix the digest form
    // kept (target - 17 < declared - 20), and the digest is the whole value's.
    std::string out;
    out.reserve(target);
    append_capped_prefix(out, member, target);
    out.append(member.substr(member.size() - kTextDigestHexChars));
    return out;
}

std::string text_display_form(std::string_view member, std::optional<std::size_t> declared)
{
    if (!is_text_digest_form(member, declared))
        return std::string{member};
    std::string_view prefix{member.substr(0, member.size() - kTextDigestHexChars)};
    while (!prefix.empty() && prefix.back() == '~')
        prefix.remove_suffix(1);
    constexpr std::string_view kTruncationMarker{"\xE2\x80\xA6"};
    std::string out{prefix};
    out.append(kTruncationMarker);
    return out;
}

std::string_view TemplateRegistry::intern(TemplateId template_id, std::string_view template_str)
{
    const auto [iter, inserted]{table_.try_emplace(template_id, template_str)};
    return iter->second;
}

std::string_view TemplateRegistry::lookup(TemplateId template_id) const noexcept
{
    const auto iter{table_.find(template_id)};
    return iter != table_.end() ? std::string_view{iter->second} : std::string_view{};
}

bool TemplateRegistry::contains(TemplateId template_id) const noexcept
{
    return table_.contains(template_id);
}

std::size_t TemplateRegistry::size() const noexcept
{
    return table_.size();
}

void TemplateRegistry::clear() noexcept
{
    table_.clear();
}

void TemplateRegistry::merge(const TemplateRegistry& other)
{
    for (const auto& [template_id, template_str] : other.table_)
        table_.try_emplace(template_id, template_str);
}

} // namespace insight::metalog
