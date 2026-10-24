// invariant: compose() re-admits every field histogram slot under the minimum of the slot and value
// caps its inputs declare, never under a cap of its own the producer did not run under.
// refs: DN-148.D3, DN-148.D5, ADR-25.D5
#include <gtest/gtest.h>

import insight.metalog.test;

#include "../written_or_fail.hpp"

namespace
{

namespace meta = insight::metalog;
using insight::metalog::test::ParamEvent;

constexpr std::size_t kProducerCap{8};

// post: one producer window whose single template carries one slot holding `distinct` values
// named `prefix<i>`, value i seen i + 1 times.
[[nodiscard]] meta::MetaLogDocument produce_window(std::string_view prefix, std::size_t distinct,
                                                   std::size_t producer_cap)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{
        .max_param_histograms = 1,
        .max_histogram_values = producer_cap,
    }};
    const auto start{std::chrono::system_clock::time_point{} + std::chrono::hours{1}};
    engine.open_window(start);
    for (std::size_t value{0}; value < distinct; ++value)
        for (std::size_t seen{0}; seen <= value; ++seen)
        {
            auto event{ParamEvent::make("charge id=<*>", {std::format("{}{}", prefix, value)})};
            engine.ingest_event(event.event);
        }
    return engine.close_window(start + std::chrono::seconds{1});
}

[[nodiscard]] std::string render_slot(const meta::FieldHistogram& slot)
{
    std::vector<std::pair<std::string, std::uint64_t>> rows(slot.value_counts.begin(),
                                                            slot.value_counts.end());
    std::ranges::sort(rows);
    std::string out{std::format("total={} values={}:", slot.total, rows.size())};
    for (const auto& [value, count] : rows)
        out += std::format(" {}={}", value, count);
    return out;
}

TEST(ComposeDeclaredHistogramCap, TwoProducerWindowsComposeUnderTheProducersCap)
{
    const auto lhs{produce_window("a", kProducerCap, kProducerCap)};
    const auto rhs{produce_window("b", kProducerCap, kProducerCap)};
    ASSERT_EQ(lhs.stats.top_k.size(), 1U);
    ASSERT_EQ(lhs.stats.top_k[0].field_histograms.size(), 1U);
    ASSERT_EQ(lhs.stats.top_k[0].field_histograms[0].value_counts.size(), kProducerCap)
        << "fixture: each producer window must fill its slot to the cap, or the merge below "
           "cannot exceed it: "
        << render_slot(lhs.stats.top_k[0].field_histograms[0]);

    const auto composed{meta::compose(lhs, rhs)};
    ASSERT_EQ(composed.stats.top_k.size(), 1U);
    ASSERT_EQ(composed.stats.top_k[0].field_histograms.size(), 1U);
    const auto& slot{composed.stats.top_k[0].field_histograms[0]};
    EXPECT_EQ(slot.value_counts.size(), kProducerCap)
        << "both inputs were admitted under a cap of " << kProducerCap
        << " values a slot, so the composed slot is re-admitted under it; a composer cap of its "
           "own lets a block hold min(cap x width, 64) values and its size grow with its width: "
        << render_slot(slot);
    EXPECT_EQ(slot.total, lhs.stats.top_k[0].field_histograms[0].total +
                              rhs.stats.top_k[0].field_histograms[0].total)
        << "re-admission drops values, never mass: " << render_slot(slot);
    for (std::size_t value{0}; value < kProducerCap / 2; ++value)
    {
        EXPECT_FALSE(slot.value_counts.contains(std::format("a{}", value)))
            << "a" << value << " has the lowest counts and is cut first: " << render_slot(slot);
        EXPECT_FALSE(slot.value_counts.contains(std::format("b{}", value)))
            << "b" << value << " has the lowest counts and is cut first: " << render_slot(slot);
    }
}

TEST(ComposeDeclaredHistogramCap, TheProducerDeclaresBothCapsOnlyWhenItBuildsHistograms)
{
    const auto with{produce_window("a", 3, kProducerCap)};
    EXPECT_EQ(with.stats.param_histograms_size, std::optional<std::size_t>{1U});
    EXPECT_EQ(with.stats.value_counts_size, std::optional<std::size_t>{kProducerCap});

    meta::MetaLogEngine engine{meta::MetaLogConfig{}};
    const auto start{std::chrono::system_clock::time_point{} + std::chrono::hours{1}};
    engine.open_window(start);
    auto event{ParamEvent::make("charge id=<*>", {"a0"})};
    engine.ingest_event(event.event);
    const auto without{engine.close_window(start + std::chrono::seconds{1})};
    EXPECT_FALSE(without.stats.param_histograms_size.has_value())
        << "a producer building no histogram declares no slot cap";
    EXPECT_FALSE(without.stats.value_counts_size.has_value())
        << "a producer building no histogram declares no value cap";
}

// post: a hand-built document whose one template carries the given slots, declaring the caps.
[[nodiscard]] meta::MetaLogDocument declared_document(std::vector<meta::FieldHistogram> slots,
                                                      std::optional<std::size_t> slot_cap,
                                                      std::optional<std::size_t> value_cap)
{
    meta::MetaLogDocument doc;
    doc.window.lines_observed = 1000;
    doc.stats.unique_templates = 1;
    doc.stats.top_k_size = 8;
    doc.stats.param_histograms_size = slot_cap;
    doc.stats.value_counts_size = value_cap;
    meta::TopKEntry entry;
    entry.template_id = insight::template_id_of_label("h:declared");
    for (const auto& slot : slots)
        entry.count = std::max(entry.count, slot.total);
    entry.field_histograms = std::move(slots);
    doc.stats.top_k.push_back(std::move(entry));
    return doc;
}

// post: one slot holding `distinct` values `prefix<i>`, value i counted i + 1 times.
[[nodiscard]] meta::FieldHistogram slot_of(std::uint32_t param_index, std::string_view prefix,
                                           std::size_t distinct)
{
    meta::FieldHistogram slot;
    slot.param_index = param_index;
    for (std::size_t value{0}; value < distinct; ++value)
    {
        slot.value_counts.emplace(std::format("{}{}", prefix, value), value + 1);
        slot.total += value + 1;
    }
    return slot;
}

TEST(ComposeDeclaredHistogramCap, ComposeTakesTheMinimumAndCutsAOneSidedSlotToIt)
{
    constexpr std::size_t kNarrow{4};
    constexpr std::size_t kWide{16};
    const auto narrow{declared_document({slot_of(0, "n", kNarrow)}, 2, kNarrow)};
    const auto wide{declared_document({slot_of(0, "w", kWide), slot_of(1, "x", kWide)}, 2, kWide)};

    const auto composed{meta::compose(narrow, wide)};
    EXPECT_EQ(composed.stats.value_counts_size, std::optional<std::size_t>{kNarrow});
    EXPECT_EQ(composed.stats.param_histograms_size, std::optional<std::size_t>{2U});
    ASSERT_EQ(composed.stats.top_k.size(), 1U);
    const auto& slots{composed.stats.top_k[0].field_histograms};
    ASSERT_EQ(slots.size(), 2U);
    EXPECT_EQ(slots[0].value_counts.size(), kNarrow)
        << "the two-sided slot is cut to the minimum: " << render_slot(slots[0]);
    EXPECT_EQ(slots[1].value_counts.size(), kNarrow)
        << "the slot only the wide input carries is cut to the declared minimum too, or the "
           "composed document breaks the cap it declares: "
        << render_slot(slots[1]);
    for (std::size_t value{kWide - kNarrow}; value < kWide; ++value)
        EXPECT_TRUE(slots[1].value_counts.contains(std::format("x{}", value)))
            << "x" << value
            << " is among the highest counts and is kept: " << render_slot(slots[1]);
    EXPECT_EQ(slots[1].total, wide.stats.top_k[0].field_histograms[1].total)
        << "the one-sided cut drops values, never mass: " << render_slot(slots[1]);
}

TEST(ComposeDeclaredHistogramCap, ASlotAtOrAboveTheMinimumSlotCapIsDropped)
{
    const auto one{declared_document({slot_of(0, "a", 2)}, 1, kProducerCap)};
    const auto two{declared_document({slot_of(0, "b", 2), slot_of(1, "c", 2)}, 2, kProducerCap)};
    const auto composed{meta::compose(one, two)};
    EXPECT_EQ(composed.stats.param_histograms_size, std::optional<std::size_t>{1U});
    ASSERT_EQ(composed.stats.top_k.size(), 1U);
    const auto& slots{composed.stats.top_k[0].field_histograms};
    ASSERT_EQ(slots.size(), 1U) << "a producer at one slot holds position 0 only";
    EXPECT_EQ(slots[0].param_index, 0U);
}

TEST(ComposeDeclaredHistogramCap, UndeclaredInputsComposeUnderTheComposersDeclaredDefault)
{
    constexpr std::size_t kDefault{meta::MetaLogConfig::kDefaultMaxHistogramValues};
    const auto lhs{declared_document({slot_of(0, "a", kDefault)}, std::nullopt, std::nullopt)};
    const auto rhs{declared_document({slot_of(0, "b", kDefault)}, std::nullopt, std::nullopt)};
    const auto composed{meta::compose(lhs, rhs)};
    EXPECT_EQ(composed.stats.value_counts_size, std::optional<std::size_t>{kDefault})
        << "a composer holding a cap of its own declares it";
    EXPECT_FALSE(composed.stats.param_histograms_size.has_value())
        << "no input declared a slot cap, so the composer claims none";
    EXPECT_EQ(composed.stats.top_k[0].field_histograms[0].value_counts.size(), kDefault);

    const auto declared{declared_document({slot_of(0, "c", 3)}, 1, 3)};
    EXPECT_EQ(meta::compose(lhs, declared).stats.value_counts_size, std::optional<std::size_t>{3U})
        << "an input that declares nothing is skipped, never folded in as the default";
}

TEST(ComposeDeclaredHistogramCap, DifferentCapsComposeToTheSameBytesInEitherOrder)
{
    const auto narrow{declared_document({slot_of(0, "n", 6), slot_of(1, "m", 6)}, 2, 4)};
    const auto wide{declared_document({slot_of(0, "w", 12), slot_of(2, "z", 12)}, 3, 16)};
    const meta::TemplateRegistry registry;
    const std::string forward{
        written_or_fail(meta::to_json(meta::compose(narrow, wide), registry))};
    const std::string backward{
        written_or_fail(meta::to_json(meta::compose(wide, narrow), registry))};
    EXPECT_EQ(forward, backward) << "compose(A, B) and compose(B, A) must be the same bytes";
    const auto ab{meta::compose(narrow, wide)};
    const auto ba{meta::compose(wide, narrow)};
    EXPECT_EQ(ab.stats.value_counts_size, ba.stats.value_counts_size);
    EXPECT_EQ(ab.stats.param_histograms_size, ba.stats.param_histograms_size);
}

} // namespace
