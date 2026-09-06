
// refs: ADR-31.D8
// invariant: reservoir_delta is the chronic-versus-new streaming seam, without which a chronic rare
// fatal and a genuinely new one fire identically every window.
// invariant: it is a set difference over the two documents' salience memory, top_k union reservoir,
// yielding new and vanished salients plus the failure-frontier crossings.
// note: additive, sorted by template_id, and no version bump.
#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace meta = insight::metalog;
using insight::LogLevel;
using insight::StructuralRole;
using insight::template_id_of;

// note: a reservoir entry, salience and count carried for the snapshot assertions.
[[nodiscard]] meta::ReservoirEntry reservoir_entry(std::string_view tmpl, LogLevel level,
                                                   std::uint32_t salience, std::uint64_t count)
{
    meta::ReservoirEntry entry;
    entry.template_id = template_id_of(tmpl);
    entry.dominant_level = insight::EventLevel::declared(level);
    entry.salience = salience;
    entry.count = count;
    return entry;
}

// invariant: a top_k entry is a salience-memory member that does NOT draw from the reservoir, so it
// suppresses new without ever appearing as a new or vanished snapshot.
[[nodiscard]] meta::TopKEntry top_k_entry(std::string_view tmpl, LogLevel level,
                                          std::uint64_t count)
{
    meta::TopKEntry entry;
    entry.template_id = template_id_of(tmpl);
    entry.dominant_level = insight::EventLevel::declared(level);
    entry.count = count;
    return entry;
}

// invariant: default processing identifiers, so the comparability gate passes and the salience
// memory is the only variable.
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

// invariant: membership snapshots are sorted ascending by template_id -- the sole output order.
template <class Entry> [[nodiscard]] bool sorted_by_id(const std::vector<Entry>& list)
{
    return std::ranges::is_sorted(list, [](const Entry& lhs, const Entry& rhs)
                                  { return lhs.template_id < rhs.template_id; });
}

} // namespace

// invariant: a rare salient in the current reservoir and absent from the previous window's salience
// memory is new; one already in that memory is not.
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

    // note: the snapshot carries the current-side entry's severity and loudness.
    const auto& snap{delta.new_salient.front()};
    EXPECT_EQ(snap.dominant_level, LogLevel::Fatal);
    EXPECT_EQ(snap.salience, 9500u);
    EXPECT_EQ(snap.count, 1u);
}

// invariant: a previous reservoir template gone from the current window's memory is vanished.
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

// invariant: the absence set is top_k union reservoir, so a template held in the previous top_k
// still suppresses new -- memory is memory regardless of which tier holds it.
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

// invariant: a template in BOTH sides' memory whose dominant level crosses the failure frontier is
// a signed, polarity-mute crossing, up meaning into failure.
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

// invariant: a level change that does not change failure membership is not a crossing.
TEST(ReservoirDeltaTest, WithinBandLevelChangeIsNotACrossing)
{
    const auto prev{doc_with({}, {reservoir_entry("severe", LogLevel::Error, 8000, 1)})};
    const auto curr{doc_with({}, {reservoir_entry("severe", LogLevel::Fatal, 8000, 1)})};

    const auto d{meta::diff(prev, curr)};
    EXPECT_TRUE(d.reservoir_delta.frontier_crossings.empty())
        << "Error→Fatal stays inside the failure band — no frontier crossing";
}

// invariant: every output list is sorted by template_id regardless of insertion order, so the
// unordered membership lookups never leak into output order.
TEST(ReservoirDeltaTest, OutputListsSortedByTemplateId)
{
    // note: inserted in deliberately non-id order.
    const auto prev{doc_with({}, {})};
    const auto curr{doc_with({}, {reservoir_entry("zeta", LogLevel::Error, 8000, 1),
                                  reservoir_entry("alpha", LogLevel::Error, 8000, 1),
                                  reservoir_entry("mike", LogLevel::Error, 8000, 1)})};

    const auto d{meta::diff(prev, curr)};
    EXPECT_EQ(d.reservoir_delta.new_salient.size(), 3u);
    EXPECT_TRUE(sorted_by_id(d.reservoir_delta.new_salient))
        << "new_salient must be sorted by template_id, not reservoir order";
}

// invariant: with no salience memory on either side the block is empty AND the serialized diff
// carries no reservoir_delta key at all -- emptiness as absence.
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
