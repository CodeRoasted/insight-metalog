// insight.metalog.detail.operations — SEALED operations domain (domain decomposition, §11.9.11).
// SPEC §2.4 comparability gate: decides whether two documents may be composed/diffed, and which
// opaque contract identifier the result may carry. Shared by compose and diff.
// Never re-exported by the facade and never installed (PRIVATE file set).
export module insight.metalog.detail.operations;
import insight.metalog.internal;
import insight.metalog.api;

export namespace insight::metalog
{

// §2.4 comparability gate. When both sides carry the identifier, the values MUST
// be equal; throwing satisfies the spec's "MUST fail" branch. When one side omits
// it, the operation MAY proceed (the consumer should treat the result with
// caution); see callers for the carry rule.
inline void check_processing_identifier_gate(const std::optional<std::string>& lhs,
                                             const std::optional<std::string>& rhs,
                                             std::string_view field, std::string_view operation)
{
    if (lhs && rhs && *lhs != *rhs)
        throw std::invalid_argument{std::string{"metalog::"} + std::string{operation} +
                                    ": incompatible " + std::string{field} + " — \"" + *lhs +
                                    "\" vs \"" + *rhs + "\" (SPEC §2.4 comparability gate)"};
}

// Carry an identifier into a compose() output only when BOTH inputs supplied it
// (and matched — already checked). When only one side has it, omitting from the
// output is honest: the merged document covers an input under an unstated
// contract; consumers see the absence rather than an over-claim.
[[nodiscard]] inline std::optional<std::string>
carry_processing_identifier(const std::optional<std::string>& lhs,
                            const std::optional<std::string>& rhs)
{
    return (lhs && rhs && *lhs == *rhs) ? lhs : std::nullopt;
}

} // namespace insight::metalog
