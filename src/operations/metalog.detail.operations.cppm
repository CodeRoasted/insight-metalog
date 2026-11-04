// refs: ADR-3.D4
export module insight.metalog.detail.operations;
import insight.metalog.internal;
import insight.metalog.api;

export namespace insight::metalog
{

// post: throws std::invalid_argument when both sides carry the identifier and the values differ;
// one side omitting it is not a failure and the operation may proceed.
inline void check_processing_identifier_gate(const std::optional<std::string>& lhs,
                                             const std::optional<std::string>& rhs,
                                             std::string_view field, std::string_view operation)
{
    if (lhs && rhs && *lhs != *rhs)
        throw std::invalid_argument{std::string{"metalog::"} + std::string{operation} +
                                    ": incompatible " + std::string{field} + " — \"" + *lhs +
                                    "\" vs \"" + *rhs + "\" (SPEC §2.4 comparability gate)"};
}

// post: carries the identifier only when both inputs supplied it and they matched, so a merged
// document never states a contract one of its inputs did not.
[[nodiscard]] inline std::optional<std::string>
carry_processing_identifier(const std::optional<std::string>& lhs,
                            const std::optional<std::string>& rhs)
{
    return (lhs && rhs && *lhs == *rhs) ? lhs : std::nullopt;
}

// post: the document a producer declaring the text cap `target` would have emitted for the same
// observations as `doc`, declaring max_text_bytes as target.
// invariant: every observed-text member is re-capped by recap_text: field histogram values,
// dominant components, reservoir cube coordinates, cube labels and service edge endpoints.
// invariant: members that coincide after it are one member with their counts summed.
// post: throws std::invalid_argument when target is not a valid text cap below an engaged
// declaration.
// invariant: members coincide only on a digest collision, or as the verbatim and digest forms of
// one value, so on every other input only the long members change.
// refs: DN-148.D5
[[nodiscard]] MetaLogDocument recapped_document(const MetaLogDocument& doc, std::size_t target);

// post: the minimum over the declared text caps, absent only when neither declares one.
// refs: DN-148.D5
[[nodiscard]] inline std::optional<std::size_t>
common_text_cap(std::optional<std::size_t> lhs, std::optional<std::size_t> rhs) noexcept
{
    if (lhs && rhs)
        return std::min(*lhs, *rhs);
    return lhs ? lhs : rhs;
}

} // namespace insight::metalog
