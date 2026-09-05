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

} // namespace insight::metalog
