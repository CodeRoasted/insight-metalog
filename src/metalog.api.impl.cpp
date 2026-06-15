// insight.metalog.api implementation unit — MSVC C++23-modules port (see metalog.api.cppm).
//
// Homes MetaLogDocument's copy/move/assign/dtor as HAND-WRITTEN (not `= default`) bodies. A
// defaulted special member is always compiler-synthesizable, so under MSVC a consumer module TU
// (insight.detection) emits its OWN copy/move — and under Release /O2 /Ob2 inlines + miscompiles the
// `std::optional<CubeBlock> cube` member (destination optional left spuriously ENGAGED over an
// unconstructed CubeBlock → ~MetaLogDocument frees a garbage std::vector<CubeAxis>, AV 0xc0000005).
// A `= default` definition in this unit additionally collides at link (LNK2005: metalog's body vs the
// consumer's synthesized one). GENUINELY user-provided bodies are NOT synthesizable, so the consumer
// must emit a real CALL into this single, correctly-compiled definition. Bodies are member-wise (==
// the implicit semantics); `cube` is built via in_place/emplace to also avoid optional<CubeBlock>'s
// own copy/move ctor. Behaviour is identical on gcc/clang — this is purely WHERE/HOW the special
// members are emitted.
module insight.metalog.api;

import insight.metalog.internal; // std
import insight.canon;            // canon types reachable from MetaLogDocument's members

namespace insight::metalog
{

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
      cube(other.cube ? std::optional<CubeBlock>(std::in_place, *other.cube) : std::nullopt)
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
      cube(other.cube ? std::optional<CubeBlock>(std::in_place, std::move(*other.cube)) : std::nullopt)
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
    if (other.cube)
        cube.emplace(*other.cube);
    else
        cube.reset();
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
    if (other.cube)
        cube.emplace(std::move(*other.cube));
    else
        cube.reset();
    return *this;
}

// Hand-written (empty), NOT `= default`: a defaulted dtor is synthesizable, so the consumer would
// emit its own and collide with this one (LNK2005). Members are destroyed automatically.
MetaLogDocument::~MetaLogDocument() {} // NOLINT(modernize-use-equals-default)

} // namespace insight::metalog
