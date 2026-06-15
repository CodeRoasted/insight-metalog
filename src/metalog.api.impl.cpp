// insight.metalog.api implementation unit — MSVC C++23-modules port (see metalog.api.cppm).
//
// Homes MetaLogDocument's copy/move/assign/dtor as HAND-WRITTEN (not `= default`) bodies, for TWO
// reasons that together defeat an MSVC optimizer bug around `std::optional<CubeBlock>`:
//   1. A `= default` special member is always compiler-synthesizable, so under MSVC a CONSUMER
//      module TU (insight.detection) emits its OWN copy/move and miscompiles the optional<CubeBlock>
//      member under Release /O2 /Ob2. User-provided (non-defaulted) bodies are not synthesizable, so
//      the consumer must emit a real CALL into this single definition. (A `= default` body in this
//      unit additionally LNK2005-collides with the consumer's synthesized one.)
//   2. This unit itself is compiled at /Ob1 on MSVC (CMakeLists set_source_files_properties): the
//      bug is the /Ob2 aggressive inliner mishandling optional<CubeBlock>'s _Has_value (RelWithDebInfo
//      /Ob1 never reproduced it), so the optional copy/move below is emitted correctly here. The
//      bodies are plain member-wise == the implicit/`= default` semantics — no per-member dodges.
// Behaviour is byte-identical on gcc/clang (member-wise == defaulted); the cube is pure-integer, so
// /Ob1 vs /Ob2 cannot change output — determinism goldens unaffected.
module insight.metalog.api;

import insight.metalog.internal; // std
import insight.canon;            // canon types reachable from MetaLogDocument's members

namespace insight::metalog
{

// These bodies ARE member-wise == `= default`, but MUST stay user-provided (see header): a `= default`
// special member is synthesizable, which both reintroduces the consumer-side miscompile and collides
// at link (LNK2005). So `= default` is exactly the wrong fix here — suppress the modernize hint.
// NOLINTBEGIN(modernize-use-equals-default)
MetaLogDocument::MetaLogDocument(const MetaLogDocument& other)
    : metalog_version(other.metalog_version),
      producer(other.producer),
      window(other.window),
      source(other.source),
      stats(other.stats),
      behavior(other.behavior),
      stability(other.stability),
      templates(other.templates),
      provenance(other.provenance),
      canonicalization_version(other.canonicalization_version),
      retention_profile(other.retention_profile),
      coordinate(other.coordinate),
      cube(other.cube)
{
}

MetaLogDocument::MetaLogDocument(MetaLogDocument&& other) noexcept
    : metalog_version(std::move(other.metalog_version)),
      producer(std::move(other.producer)),
      window(std::move(other.window)),
      source(std::move(other.source)),
      stats(std::move(other.stats)),
      behavior(std::move(other.behavior)),
      stability(std::move(other.stability)),
      templates(std::move(other.templates)),
      provenance(std::move(other.provenance)),
      canonicalization_version(std::move(other.canonicalization_version)),
      retention_profile(std::move(other.retention_profile)),
      coordinate(std::move(other.coordinate)),
      cube(std::move(other.cube))
{
}

MetaLogDocument& MetaLogDocument::operator=(const MetaLogDocument& other)
{
    if (this == &other)
        return *this;
    metalog_version = other.metalog_version;
    producer = other.producer;
    window = other.window;
    source = other.source;
    stats = other.stats;
    behavior = other.behavior;
    stability = other.stability;
    templates = other.templates;
    provenance = other.provenance;
    canonicalization_version = other.canonicalization_version;
    retention_profile = other.retention_profile;
    coordinate = other.coordinate;
    cube = other.cube;
    return *this;
}

MetaLogDocument& MetaLogDocument::operator=(MetaLogDocument&& other) noexcept
{
    if (this == &other)
        return *this;
    metalog_version = std::move(other.metalog_version);
    producer = std::move(other.producer);
    window = std::move(other.window);
    source = std::move(other.source);
    stats = std::move(other.stats);
    behavior = std::move(other.behavior);
    stability = std::move(other.stability);
    templates = std::move(other.templates);
    provenance = std::move(other.provenance);
    canonicalization_version = std::move(other.canonicalization_version);
    retention_profile = std::move(other.retention_profile);
    coordinate = std::move(other.coordinate);
    cube = std::move(other.cube);
    return *this;
}

// Hand-written (empty), NOT `= default`: a defaulted dtor is synthesizable, so the consumer would
// emit its own and collide with this one (LNK2005). Members are destroyed automatically.
MetaLogDocument::~MetaLogDocument() {}
// NOLINTEND(modernize-use-equals-default)

} // namespace insight::metalog
