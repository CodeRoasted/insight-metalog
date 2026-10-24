// refs: F-SRC-metalog-spec:SPEC.md
// invariant: SPEC 12.2 (0.10.0) -- compose(A, B) and compose(B, A) are the SAME document, so a
// deterministic serializer emits the same bytes for either operand order (RUL-184).
// invariant: the inputs disagree on EVERY member 12.1 resolves by a value rule, so a composer
// reading any of them by operand position fails the byte arm.
// note: homed as a unit test: compose() is a pure function of two literal documents.
#include <gtest/gtest.h>

import insight.metalog.test;

#include "../written_or_fail.hpp"

namespace
{

namespace meta = insight::metalog;
using insight::EventLevel;
using insight::LogLevel;
using insight::StructuralRole;

[[nodiscard]] insight::TemplateId template_number(std::uint8_t number)
{
    insight::TemplateId out;
    out.bytes.back() = number;
    return out;
}

const insight::TemplateId kHigherCount{template_number(1)};
const insight::TemplateId kEqualCount{template_number(2)};
const insight::TemplateId kReservoirTie{template_number(3)};
const insight::TemplateId kBoundaryLeft{template_number(4)};
const insight::TemplateId kBoundaryRight{template_number(5)};
const insight::TemplateId kReservoirOneSided{template_number(6)};

[[nodiscard]] meta::TopKEntry top_entry(insight::TemplateId template_id, std::uint64_t count,
                                        LogLevel level, std::optional<std::string> component)
{
    meta::TopKEntry entry;
    entry.template_id = template_id;
    entry.count = count;
    entry.dominant_level = EventLevel::inferred(level);
    entry.dominant_component = std::move(component);
    return entry;
}

[[nodiscard]] meta::ReservoirEntry reservoir_entry(insight::TemplateId template_id,
                                                   std::uint64_t count, LogLevel level,
                                                   StructuralRole role, std::string where)
{
    static constexpr std::uint32_t kSurprise{40};
    static constexpr std::uint32_t kSalience{50};
    meta::ReservoirEntry entry;
    entry.template_id = template_id;
    entry.count = count;
    entry.dominant_level = EventLevel::inferred(level);
    entry.dominant_component = where;
    entry.structural_role = role;
    entry.structural_surprise = kSurprise;
    entry.salience = kSalience;
    entry.cube_coord = meta::CubeCoord{.level = meta::level_to_spec_string(level),
                                       .where = std::vector<std::string>{std::move(where)},
                                       .structural_role = std::nullopt,
                                       .latency_shift = std::nullopt};
    return entry;
}

[[nodiscard]] meta::ReDerivationCoordinate raw_coordinate(std::string handle,
                                                          std::uint64_t start_tick)
{
    static constexpr std::uint64_t kSpanTicks{60};
    meta::ReDerivationCoordinate coord;
    coord.source_ref = meta::SourceRef{.resolver_kind = "replay", .handle = std::move(handle)};
    coord.bounds =
        meta::EventTimeBounds{.start_tick = start_tick, .end_tick = start_tick + kSpanTicks};
    return coord;
}

[[nodiscard]] meta::ProvenanceEntry provenance_entry(std::string start, std::string end,
                                                     std::uint64_t lines)
{
    meta::ProvenanceEntry entry;
    entry.window = meta::WindowBounds{.start_iso = std::move(start), .end_iso = std::move(end)};
    entry.source.service = "checkout";
    entry.lines_observed = lines;
    return entry;
}

// post: an already-composed input: it carries provenance in REVERSE time order and a composed
// coordinate, and it out-counts the right input on kHigherCount.
[[nodiscard]] meta::MetaLogDocument left_input()
{
    meta::MetaLogDocument doc;
    doc.producer = meta::ProducerBlock{.name = "left-producer",
                                       .version = "9.9.9",
                                       .implementation_uri = "https://example.invalid/left"};
    doc.window.lines_observed = 100;
    doc.window.envelope = meta::WindowEnvelope{
        .bounds = {.start_iso = "2023-11-14T22:00:00Z", .end_iso = "2023-11-14T22:01:00Z"},
        .duration_seconds = 60};
    doc.source.service = "checkout";
    doc.stats.top_k_size = 3;
    doc.stats.top_k = {top_entry(kHigherCount, 30, LogLevel::Info, "alpha"),
                       top_entry(kEqualCount, 20, LogLevel::Warn, "gamma"),
                       top_entry(kBoundaryLeft, 10, LogLevel::Debug, std::nullopt)};
    doc.stats.reservoir_size = 4;
    doc.stats.reservoir = {
        reservoir_entry(kReservoirTie, 2, LogLevel::Error, StructuralRole::GroupBegin, "zeta")};
    doc.stats.tail_count = 38;
    doc.stats.unique_templates = 4;
    doc.behavior = meta::BehaviorBlock{};
    doc.behavior->ngram_size = 2;
    doc.behavior->top_ngrams_size = 2;
    doc.behavior->top_ngrams = {
        meta::NGramEntry{.sequence = {kHigherCount, kEqualCount}, .count = 4, .probability = 0.5},
        meta::NGramEntry{
            .sequence = {kEqualCount, kBoundaryLeft}, .count = 3, .probability = 0.25}};
    doc.provenance = std::vector<meta::ProvenanceEntry>{
        provenance_entry("2023-11-14T22:00:30Z", "2023-11-14T22:01:00Z", 50),
        provenance_entry("2023-11-14T22:00:00Z", "2023-11-14T22:00:30Z", 50)};
    meta::ReDerivationCoordinate composed;
    composed.children = std::vector<meta::ReDerivationCoordinate>{raw_coordinate("z-left", 30),
                                                                  raw_coordinate("a-left", 0)};
    doc.coordinate = composed;
    doc.ruleset = meta::RulesetIdentity{.semantic_identity = "ruleset-key",
                                        .packages = {{.name = "github", .version = "2"}}};
    doc.has_cube = true;
    return doc;
}

// post: a raw input: no provenance, a raw coordinate, a different producer and package list.
[[nodiscard]] meta::MetaLogDocument right_input()
{
    meta::MetaLogDocument doc;
    doc.producer = meta::ProducerBlock{.name = "right-producer",
                                       .version = "1.0.0",
                                       .implementation_uri = "https://example.invalid/right"};
    doc.window.lines_observed = 80;
    doc.window.envelope = meta::WindowEnvelope{
        .bounds = {.start_iso = "2023-11-14T22:01:00Z", .end_iso = "2023-11-14T22:02:00Z"},
        .duration_seconds = 60};
    doc.source.service = "checkout";
    doc.stats.top_k_size = 3;
    doc.stats.top_k = {top_entry(kHigherCount, 10, LogLevel::Error, "beta"),
                       top_entry(kEqualCount, 20, LogLevel::Error, "delta"),
                       top_entry(kBoundaryRight, 10, LogLevel::Info, "omega")};
    doc.stats.reservoir_size = 4;
    doc.stats.reservoir = {
        reservoir_entry(kReservoirTie, 2, LogLevel::Warn, StructuralRole::Terminator, "eta"),
        reservoir_entry(kReservoirOneSided, 1, LogLevel::Fatal, StructuralRole::Progress, "mu")};
    doc.stats.tail_count = 37;
    doc.stats.unique_templates = 5;
    doc.behavior = meta::BehaviorBlock{};
    doc.behavior->ngram_size = 2;
    doc.behavior->top_ngrams_size = 3;
    doc.behavior->top_ngrams = {
        meta::NGramEntry{.sequence = {kEqualCount, kHigherCount}, .count = 4, .probability = 0.5},
        meta::NGramEntry{
            .sequence = {kBoundaryRight, kEqualCount}, .count = 3, .probability = 0.2}};
    doc.coordinate = raw_coordinate("m-right", 60);
    doc.ruleset = meta::RulesetIdentity{.semantic_identity = "ruleset-key",
                                        .packages = {{.name = "github", .version = "1"}}};
    doc.has_cube = true;
    return doc;
}

const meta::ProducerBlock kComposer{.name = "composer-under-test",
                                    .version = "0.0.0-compose",
                                    .implementation_uri = "https://example.invalid/composer"};

// post: the first byte offset at which the two serialisations part, with a window of each side.
[[nodiscard]] std::string first_divergence(const std::string& forward, const std::string& reverse)
{
    static constexpr std::size_t kContext{160};
    static constexpr std::size_t kLead{kContext / 4};
    const auto [forward_at, reverse_at]{std::ranges::mismatch(forward, reverse)};
    const auto offset{static_cast<std::size_t>(forward_at - forward.begin())};
    const std::size_t from{offset > kLead ? offset - kLead : 0};
    return "first divergence at byte " + std::to_string(offset) + " of " +
           std::to_string(forward.size()) + " / " + std::to_string(reverse.size()) +
           "\n  compose(A, B) ..." + forward.substr(from, kContext) + "...\n  compose(B, A) ..." +
           reverse.substr(from, kContext) + "...";
}

[[nodiscard]] const meta::TopKEntry* top_find(const meta::MetaLogDocument& doc,
                                              insight::TemplateId template_id)
{
    for (const auto& entry : doc.stats.top_k)
        if (entry.template_id == template_id)
            return &entry;
    return nullptr;
}

[[nodiscard]] const meta::ReservoirEntry* reservoir_find(const meta::MetaLogDocument& doc,
                                                         insight::TemplateId template_id)
{
    for (const auto& entry : doc.stats.reservoir)
        if (entry.template_id == template_id)
            return &entry;
    return nullptr;
}

[[nodiscard]] std::string level_text(const std::optional<EventLevel>& level)
{
    return level ? meta::level_to_spec_string(level->value()) : std::string{"<absent>"};
}

// invariant: the MUST itself -- the two operand orders serialise to the same bytes.
TEST(ComposeCommutativeBytesTest, BothOperandOrdersSerialiseToTheSameBytes)
{
    const meta::MetaLogDocument left{left_input()};
    const meta::MetaLogDocument right{right_input()};
    const meta::TemplateRegistry registry;
    const std::string forward{
        written_or_fail(meta::to_json(meta::compose(left, right, kComposer), registry))};
    const std::string reverse{
        written_or_fail(meta::to_json(meta::compose(right, left, kComposer), registry))};
    ASSERT_FALSE(forward.empty());
    EXPECT_EQ(forward, reverse)
        << "SPEC 12.2: compose(A, B) and compose(B, A) MUST be the same document; a member was "
           "taken from an input by its operand position.\n"
        << first_divergence(forward, reverse);
}

// invariant: each resolved value is the one 12.1's value rule names, so the byte arm cannot pass
// by two orders agreeing on a WRONG rule, both taking the max for instance.
TEST(ComposeCommutativeBytesTest, EveryDisagreementResolvesByTheValueRule)
{
    const meta::MetaLogDocument composed{meta::compose(left_input(), right_input(), kComposer)};

    const meta::TopKEntry* higher{top_find(composed, kHigherCount)};
    ASSERT_NE(higher, nullptr) << "the 40-count template must hold a top_k slot";
    EXPECT_EQ(level_text(higher->dominant_level), "INFO")
        << "the left input contributed 30 of 40 and carried INFO; the right carried ERROR at 10";
    EXPECT_EQ(higher->dominant_component.value_or("<absent>"), "alpha")
        << "component follows the higher-count input too";

    const meta::TopKEntry* equal{top_find(composed, kEqualCount)};
    ASSERT_NE(equal, nullptr) << "the 40-count template must hold a top_k slot";
    EXPECT_EQ(level_text(equal->dominant_level), "ERROR")
        << "20 against 20: the lexicographically smaller of WARN and ERROR";
    EXPECT_EQ(equal->dominant_component.value_or("<absent>"), "delta")
        << "20 against 20: the lexicographically smaller of gamma and delta";

    ASSERT_EQ(composed.stats.top_k.size(), 3U);
    EXPECT_EQ(composed.stats.top_k[2].template_id, kBoundaryLeft)
        << "two templates tie at count 10 for the last slot and the smaller template_id takes it; "
           "got "
        << insight::render(composed.stats.top_k[2].template_id);

    const meta::ReservoirEntry* tie{reservoir_find(composed, kReservoirTie)};
    ASSERT_NE(tie, nullptr) << "the equal-count reservoir template must ride the composed "
                               "reservoir";
    EXPECT_EQ(level_text(tie->dominant_level), "ERROR") << "2 against 2: ERROR < WARN";
    EXPECT_EQ(insight::to_string(tie->structural_role), "GroupBegin")
        << "2 against 2: GroupBegin < Terminator";
    ASSERT_TRUE(tie->cube_coord && tie->cube_coord->where);
    EXPECT_EQ(*tie->cube_coord->where, std::vector<std::string>{"eta"})
        << "2 against 2: the where-path [eta] < [zeta]";
    EXPECT_EQ(tie->dominant_component.value_or("<absent>"), "eta");

    const meta::ReservoirEntry* one_sided{reservoir_find(composed, kReservoirOneSided)};
    ASSERT_NE(one_sided, nullptr) << "the right-only reservoir template must ride through";
    EXPECT_EQ(insight::to_string(one_sided->structural_role), "Progress")
        << "a label present in only one input is carried";

    EXPECT_EQ(composed.producer.name, kComposer.name);
    EXPECT_EQ(composed.producer.version, kComposer.version);
    EXPECT_EQ(composed.producer.implementation_uri, kComposer.implementation_uri);
    EXPECT_EQ(composed.metalog_version, std::string{meta::kMetaLogSpecVersion});

    ASSERT_TRUE(composed.ruleset.has_value());
    ASSERT_EQ(composed.ruleset->packages.size(), 1U);
    EXPECT_EQ(composed.ruleset->packages.front().version, "1")
        << "equal semantic_identity, different package lists: the lexicographically smaller list";
}

// invariant: 12.1's provenance is A.provenance + B.provenance + one entry per input, even when an
// input already carries provenance, and its entries are sorted by window bounds.
TEST(ComposeCommutativeBytesTest, ProvenanceAddsBothInputsAndSortsByWindow)
{
    const meta::MetaLogDocument composed{meta::compose(left_input(), right_input(), kComposer)};
    ASSERT_TRUE(composed.provenance.has_value());
    std::string listing;
    for (const auto& entry : *composed.provenance)
        listing += "\n  " +
                   (entry.window ? entry.window->start_iso + " .. " + entry.window->end_iso
                                 : std::string{"<no bounds>"}) +
                   " lines=" + std::to_string(entry.lines_observed);
    ASSERT_EQ(composed.provenance->size(), 4U)
        << "left carries 2 entries, plus one for each input; got:" << listing;
    const std::vector<std::pair<std::string, std::string>> expected{
        {"2023-11-14T22:00:00Z", "2023-11-14T22:00:30Z"},
        {"2023-11-14T22:00:00Z", "2023-11-14T22:01:00Z"},
        {"2023-11-14T22:00:30Z", "2023-11-14T22:01:00Z"},
        {"2023-11-14T22:01:00Z", "2023-11-14T22:02:00Z"}};
    for (std::size_t index{0}; index < expected.size(); ++index)
    {
        const auto& entry{(*composed.provenance)[index]};
        ASSERT_TRUE(entry.window.has_value()) << "entry " << index << listing;
        EXPECT_EQ(entry.window->start_iso, expected[index].first) << "entry " << index << listing;
        EXPECT_EQ(entry.window->end_iso, expected[index].second) << "entry " << index << listing;
    }

    ASSERT_TRUE(composed.coordinate && composed.coordinate->children);
    std::vector<std::string> handles;
    for (const auto& child : *composed.coordinate->children)
        handles.push_back(child.source_ref ? child.source_ref->handle : std::string{"<none>"});
    // note: canonical bytes open on "bounds":{"end_tick":, so end ticks 120 < 60 < 90 as BYTES.
    EXPECT_EQ(handles, (std::vector<std::string>{"m-right", "a-left", "z-left"}))
        << "the composed coordinate's children are a set, emitted in RFC 8785 byte order";
}

// post: a provenance entry on one fixed early window, so it sorts ahead of every other entry and
// its order among its peers is decided by the canonical key alone.
[[nodiscard]] meta::ProvenanceEntry early_entry(std::uint64_t start_tick, std::uint64_t end_tick,
                                                std::optional<std::string> document_id)
{
    meta::ProvenanceEntry entry{
        provenance_entry("2023-11-14T21:00:00Z", "2023-11-14T21:01:00Z", 7)};
    meta::ReDerivationCoordinate coord;
    coord.source_ref = meta::SourceRef{.resolver_kind = "replay", .handle = "tie"};
    coord.bounds = meta::EventTimeBounds{.start_tick = start_tick, .end_tick = end_tick};
    entry.coordinate = coord;
    entry.document_id = std::move(document_id);
    return entry;
}

[[nodiscard]] meta::ProvenanceEntry tagged_entry(std::string private_use_value,
                                                 std::string emoji_value)
{
    meta::ProvenanceEntry entry{
        provenance_entry("2023-11-14T21:00:00Z", "2023-11-14T21:01:00Z", 7)};
    entry.source.tags.emplace("\xEE\x80\x80", std::move(private_use_value));
    entry.source.tags.emplace("\xF0\x9F\x98\x80", std::move(emoji_value));
    return entry;
}

// invariant: RFC 8785 properties a byte-order shortcut gets wrong, each one flipping one pair:
// an integer above 2^53 compares as its double, and key order is UTF-16 code-unit order.
// note: two values one double apart, RFC 8785's tie, still order by their exact decimals.
TEST(ComposeCommutativeBytesTest, ProvenanceOrdersByRfc8785CanonicalBytes)
{
    static constexpr std::uint64_t kTwoPow53{std::uint64_t{1} << 53U};
    static constexpr std::uint64_t kSharedEnd{kTwoPow53 + 100};
    meta::MetaLogDocument left{left_input()};
    meta::MetaLogDocument right{right_input()};
    left.provenance->push_back(early_entry(kTwoPow53, kSharedEnd, "b"));
    left.provenance->push_back(early_entry(kTwoPow53 + 1, kSharedEnd, std::nullopt));
    left.provenance->push_back(tagged_entry("b", "a"));
    right.provenance = std::vector<meta::ProvenanceEntry>{
        early_entry(kTwoPow53 + 1, kSharedEnd, "a"),
        early_entry(kTwoPow53, kSharedEnd, std::nullopt), tagged_entry("a", "b")};

    const meta::TemplateRegistry registry;
    const std::string forward{
        written_or_fail(meta::to_json(meta::compose(left, right, kComposer), registry))};
    const std::string reverse{
        written_or_fail(meta::to_json(meta::compose(right, left, kComposer), registry))};
    EXPECT_EQ(forward, reverse) << first_divergence(forward, reverse);

    const meta::MetaLogDocument composed{meta::compose(left, right, kComposer)};
    ASSERT_TRUE(composed.provenance.has_value());
    std::vector<std::string> early;
    for (const auto& entry : *composed.provenance)
    {
        if (!entry.window || entry.window->start_iso != "2023-11-14T21:00:00Z")
            continue;
        std::string label;
        if (entry.coordinate && entry.coordinate->bounds)
            label = "tick+" + std::to_string(entry.coordinate->bounds->start_tick - kTwoPow53) +
                    " doc=" + entry.document_id.value_or("-");
        else
            label = "tags private-use=" + entry.source.tags.at("\xEE\x80\x80");
        early.push_back(std::move(label));
    }
    // note: coordinate < document_id < lines_observed, so a document_id sorts first.
    const std::vector<std::string> expected{"tick+1 doc=a",       "tick+0 doc=b",
                                            "tick+0 doc=-",       "tick+1 doc=-",
                                            "tags private-use=b", "tags private-use=a"};
    std::string listing;
    for (const auto& label : early)
        listing += "\n  " + label;
    EXPECT_EQ(early, expected)
        << "2^53 and 2^53+1 are one double, so RFC 8785 ties them and document_id decides (a "
           "before "
           "b, against the exact ticks); with no document_id the exact decimal decides; the emoji "
           "key (UTF-16 D83D) precedes U+E000, so its value decides the tagged pair. Got:"
        << listing;
}

} // namespace
