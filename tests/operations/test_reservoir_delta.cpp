// NOLINTBEGIN
// Unit tests: allow short identifiers and test-specific patterns.
// diff(): MetaLogDiff::reservoir_delta — the §5.3 chronic-vs-new streaming seam. Set-difference
// over the two documents' salience memory (top_k ∪ reservoir): new_salient / vanished_salient +
// the ERROR/FATAL failure-frontier crossings. Additive, sorted by template_id, no version bump.

#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace meta = insight::metalog;
using insight::LogLevel;
using insight::StructuralRole;
using insight::template_id_of;

// A reservoir entry for `tmpl` at `level`, salience/count carried for the snapshot assertions.
[[nodiscard]] meta::ReservoirEntry reservoir_entry(std::string_view tmpl, LogLevel level,
                                                   std::uint32_t salience, std::uint64_t count)
{
    meta::ReservoirEntry entry;
    entry.template_id = template_id_of(tmpl);
    entry.dominant_level = level;
    entry.salience = salience;
    entry.count = count;
    return entry;
}

// A top_k entry for `tmpl` at `level` — a salience-memory member that does NOT draw from the
// reservoir (so it suppresses "new" without ever appearing as a new/vanished snapshot).
[[nodiscard]] meta::TopKEntry top_k_entry(std::string_view tmpl, LogLevel level,
                                          std::uint64_t count)
{
    meta::TopKEntry entry;
    entry.template_id = template_id_of(tmpl);
    entry.dominant_level = level;
    entry.count = count;
    return entry;
}

// A minimal comparable document: default canonicalization_version / retention_profile (so the
// diff() comparability gate passes), carrying the given top_k + reservoir memory.
[[nodiscard]] meta::MetaLogDocument doc_with(std::vector<meta::TopKEntry> top_k,
                                             std::vector<meta::ReservoirEntry> reservoir)
{
    meta::MetaLogDocument doc;
    doc.stats.top_k = std::move(top_k);
    doc.stats.reservoir = std::move(reservoir);
    return doc;
}

[[nodiscard]] bool contains_id(const std::vector<meta::ReservoirDeltaEntry>& list,
                               std::string_view tmpl)
{
    const auto id{template_id_of(tmpl)};
    return std::ranges::any_of(list, [&](const auto& e) { return e.template_id == id; });
}

// Membership snapshots must be sorted ascending by template_id — the sole output order (§5.3).
template <class Entry> [[nodiscard]] bool sorted_by_id(const std::vector<Entry>& list)
{
    return std::ranges::is_sorted(list, [](const Entry& lhs, const Entry& rhs)
                                  { return lhs.template_id < rhs.template_id; });
}

} // namespace

// A rare-salient template present in current.reservoir but absent from the previous window's
// salience memory is `new_salient`; a chronic one already in that memory is NOT.
TEST(ReservoirDeltaTest, NewSalientIsAbsentFromPreviousMemory)
{
    const auto prev{
        doc_with({}, {reservoir_entry("chronic db timeout", LogLevel::Error, 8000, 1)})};
    const auto curr{
        doc_with({}, {reservoir_entry("chronic db timeout", LogLevel::Error, 8000, 1),
                      reservoir_entry("brand new oom kill", LogLevel::Fatal, 9500, 1)})};

    const auto d{meta::diff(prev, curr)};
    const auto& delta{d.reservoir_delta};

    ASSERT_EQ(delta.new_salient.size(), 1u) << "expected exactly the genuinely-new template; got "
                                            << delta.new_salient.size() << " new_salient entries";
    EXPECT_TRUE(contains_id(delta.new_salient, "brand new oom kill"))
        << "the current-only rare-fatal must be flagged new";
    EXPECT_FALSE(contains_id(delta.new_salient, "chronic db timeout"))
        << "a template already in the previous reservoir is chronic, never new";

    // Snapshot carries the current-side entry's severity + loudness.
    const auto& snap{delta.new_salient.front()};
    EXPECT_EQ(snap.dominant_level, LogLevel::Fatal);
    EXPECT_EQ(snap.salience, 9500u);
    EXPECT_EQ(snap.count, 1u);
}

// A previous.reservoir template gone from the current window's salience memory is
// `vanished_salient`.
TEST(ReservoirDeltaTest, VanishedSalientIsAbsentFromCurrentMemory)
{
    const auto prev{doc_with({}, {reservoir_entry("held error", LogLevel::Error, 8000, 1),
                                  reservoir_entry("gone error", LogLevel::Error, 8200, 1)})};
    const auto curr{doc_with({}, {reservoir_entry("held error", LogLevel::Error, 8000, 1)})};

    const auto d{meta::diff(prev, curr)};
    const auto& delta{d.reservoir_delta};

    ASSERT_EQ(delta.vanished_salient.size(), 1u)
        << "expected exactly the vanished template; got " << delta.vanished_salient.size();
    EXPECT_TRUE(contains_id(delta.vanished_salient, "gone error"));
    EXPECT_FALSE(contains_id(delta.vanished_salient, "held error"))
        << "a template still present must not be reported vanished";
    EXPECT_TRUE(delta.new_salient.empty());
}

// The absence set is top_k ∪ reservoir: a template held in the PREVIOUS top_k (not its reservoir)
// still suppresses "new" — memory is memory regardless of which tier holds it.
TEST(ReservoirDeltaTest, TopKMembershipSuppressesNew)
{
    const auto prev{doc_with({top_k_entry("frequent path", LogLevel::Info, 5000)}, {})};
    const auto curr{doc_with({top_k_entry("frequent path", LogLevel::Info, 5000)},
                             {reservoir_entry("frequent path", LogLevel::Info, 3000, 1),
                              reservoir_entry("truly new", LogLevel::Error, 8000, 1)})};

    const auto d{meta::diff(prev, curr)};
    const auto& delta{d.reservoir_delta};

    EXPECT_FALSE(contains_id(delta.new_salient, "frequent path"))
        << "present in previous top_k → known memory → not new";
    EXPECT_TRUE(contains_id(delta.new_salient, "truly new"));
    EXPECT_EQ(delta.new_salient.size(), 1u);
}

// A template in BOTH sides' memory whose dominant_level crosses the ERROR/FATAL failure frontier
// is a signed, polarity-mute frontier crossing (up = into failure, down = out).
TEST(ReservoirDeltaTest, FrontierCrossingsAreSignedAndPolarityMute)
{
    const auto prev{doc_with({top_k_entry("escalating call", LogLevel::Warn, 100)},
                             {reservoir_entry("recovering call", LogLevel::Fatal, 8000, 1)})};
    const auto curr{doc_with({top_k_entry("escalating call", LogLevel::Error, 100)},
                             {reservoir_entry("recovering call", LogLevel::Info, 3000, 1)})};

    const auto d{meta::diff(prev, curr)};
    const auto& crossings{d.reservoir_delta.frontier_crossings};

    ASSERT_EQ(crossings.size(), 2u)
        << "expected one up + one down crossing; got " << crossings.size();

    const auto up_id{template_id_of("escalating call")};
    const auto down_id{template_id_of("recovering call")};
    const auto up{
        std::ranges::find_if(crossings, [&](const auto& c) { return c.template_id == up_id; })};
    const auto down{
        std::ranges::find_if(crossings, [&](const auto& c) { return c.template_id == down_id; })};
    ASSERT_NE(up, crossings.end());
    ASSERT_NE(down, crossings.end());
    EXPECT_EQ(up->direction, meta::FrontierDirection::Up)
        << "Warn→Error crosses INTO the failure band";
    EXPECT_EQ(up->previous_level, LogLevel::Warn);
    EXPECT_EQ(up->current_level, LogLevel::Error);
    EXPECT_EQ(down->direction, meta::FrontierDirection::Down)
        << "Fatal→Info crosses OUT of the failure band";
}

// A level change that does NOT change failure-membership (Error→Fatal, both in-band) is not a
// crossing.
TEST(ReservoirDeltaTest, WithinBandLevelChangeIsNotACrossing)
{
    const auto prev{doc_with({}, {reservoir_entry("severe", LogLevel::Error, 8000, 1)})};
    const auto curr{doc_with({}, {reservoir_entry("severe", LogLevel::Fatal, 8000, 1)})};

    const auto d{meta::diff(prev, curr)};
    EXPECT_TRUE(d.reservoir_delta.frontier_crossings.empty())
        << "Error→Fatal stays inside the failure band — no frontier crossing";
}

// Every output list is sorted by template_id regardless of reservoir insertion order (F5-M8: the
// unordered membership lookups must never leak into output order).
TEST(ReservoirDeltaTest, OutputListsSortedByTemplateId)
{
    // Insert in deliberately non-id order.
    const auto prev{doc_with({}, {})};
    const auto curr{doc_with({}, {reservoir_entry("zeta", LogLevel::Error, 8000, 1),
                                  reservoir_entry("alpha", LogLevel::Error, 8000, 1),
                                  reservoir_entry("mike", LogLevel::Error, 8000, 1)})};

    const auto d{meta::diff(prev, curr)};
    EXPECT_EQ(d.reservoir_delta.new_salient.size(), 3u);
    EXPECT_TRUE(sorted_by_id(d.reservoir_delta.new_salient))
        << "new_salient must be sorted by template_id, not reservoir order";
}

// Byte-additive guard: when neither side carries any salience memory, the block is empty AND the
// serialized diff carries no `reservoir_delta` key at all (emptiness-as-absence, §5.3).
TEST(ReservoirDeltaTest, OmittedFromJsonWhenBothMemoriesEmpty)
{
    const auto prev{doc_with({}, {})};
    const auto curr{doc_with({}, {})};

    const auto d{meta::diff(prev, curr)};
    EXPECT_TRUE(d.reservoir_delta.empty());

    const std::string json{meta::to_json(d)};
    EXPECT_EQ(json.find("reservoir_delta"), std::string::npos)
        << "empty reservoir_delta must be omitted from the wire; json was:\n"
        << json;
}

// NOLINTEND
