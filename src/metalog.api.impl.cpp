// insight.metalog.api implementation unit — MSVC C++23-modules port (see metalog.api.cppm).
//
// Homes MetaLogDocument's `= default` special members so their bodies live in metalog's OBJECT,
// NOT the module BMI. A defaulted special member that is implicit / `= default` in-class / defined
// in the interface unit has its body in the BMI; a consumer module TU (insight.detection) then
// INLINES the copy/move at the call site under MSVC Release /O2 /Ob2 and miscompiles the
// `std::optional<CubeBlock> cube` member — the destination optional is left spuriously ENGAGED over
// an unconstructed CubeBlock, so a later ~MetaLogDocument frees a garbage std::vector<CubeAxis>
// (AV 0xc0000005). Defining the bodies HERE (an implementation unit, no BMI) forces every consumer
// to emit a real CALL into this codegen instead. Behaviour is identical on gcc/clang — this unit is
// purely about WHERE the special members are emitted.
module insight.metalog.api;

import insight.metalog.internal; // std
import insight.canon;            // canon types reachable from MetaLogDocument's members

namespace insight::metalog
{

MetaLogDocument::MetaLogDocument() = default;
MetaLogDocument::MetaLogDocument(const MetaLogDocument&) = default;
MetaLogDocument::MetaLogDocument(MetaLogDocument&&) noexcept = default;
MetaLogDocument& MetaLogDocument::operator=(const MetaLogDocument&) = default;
MetaLogDocument& MetaLogDocument::operator=(MetaLogDocument&&) noexcept = default;
MetaLogDocument::~MetaLogDocument() = default;

} // namespace insight::metalog
