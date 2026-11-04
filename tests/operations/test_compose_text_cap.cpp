// invariant: compose() and diff() over documents declaring different text caps read them at the
// smaller cap, re-capped exactly, so a long value never reads as two values.
// refs: DN-148.D5
#include <gtest/gtest.h>

import insight.metalog.test;

#include "../written_or_fail.hpp"

namespace
{

namespace meta = insight::metalog;
using insight::metalog::test::ParamEvent;

[[nodiscard]] std::chrono::system_clock::time_point hour(int index)
{
    return std::chrono::system_clock::time_point{} + std::chrono::hours{index};
}

// post: one window at text cap `cap` whose template carries long and short values and long
// components, every one in its shared registry.
[[nodiscard]] meta::MetaLogDocument produce_window(std::size_t cap, int hour_index,
                                                   std::string_view tag,
                                                   meta::TemplateRegistry& registry)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{
        .max_param_histograms = 2,
        .max_histogram_values = 8,
        .max_text_bytes = cap,
    }};
    engine.open_window(hour(hour_index));
    for (int value{0}; value < 5; ++value)
        for (int seen{0}; seen <= value; ++seen)
        {
            const std::string long_param{std::format(
                "request-{}-{}-with-a-correlation-identifier-that-runs-well-past-every-cap-{}", tag,
                value, std::string(40, 'z'))};
            const std::string short_param{std::format("s{}", value % 3)};
            auto event{ParamEvent::make("handled <*> as <*>", {long_param, short_param})};
            const std::string component{
                std::format("org.example.service.module.submodule.component.instance-number-{}-{}",
                            value % 2, std::string(20, 'q'))};
            event.event.component = component;
            engine.ingest_event(event.event);
        }
    meta::MetaLogDocument doc{engine.close_window(hour(hour_index + 1))};
    registry.merge(engine.registry());
    return doc;
}

} // namespace

TEST(ComposeTextCap, ComposeAtTwoCapsIsComposeAtTheSmallerInEitherOrder)
{
    meta::TemplateRegistry registry;
    const auto wide{produce_window(64, 0, "a", registry)};
    const auto narrow{produce_window(32, 1, "b", registry)};
    const auto wide_at_narrow{produce_window(32, 0, "a", registry)};

    const meta::MetaLogDocument mixed_ab{meta::compose(wide, narrow)};
    const meta::MetaLogDocument mixed_ba{meta::compose(narrow, wide)};
    const meta::MetaLogDocument direct{meta::compose(wide_at_narrow, narrow)};
    EXPECT_EQ(mixed_ab.max_text_bytes, std::optional<std::size_t>{32U})
        << "L_C is the minimum over the declared caps";
    const std::string ab{written_or_fail(meta::to_json(mixed_ab, registry))};
    const std::string ba{written_or_fail(meta::to_json(mixed_ba, registry))};
    const std::string expected{written_or_fail(meta::to_json(direct, registry))};
    EXPECT_EQ(ab, ba) << "compose at two caps must be byte-commutative";
    EXPECT_EQ(ab, expected)
        << "compose of an L = 64 and an L = 32 document must equal compose of the two documents "
           "produced at L = 32: the re-cap is exact.\n  mixed:  "
        << ab << "\n  direct: " << expected;
}

TEST(ComposeTextCap, AnInputDeclaringNoCapIsCappedAtTheOthers)
{
    meta::TemplateRegistry registry;
    meta::MetaLogDocument undeclared{produce_window(4096, 0, "a", registry)};
    undeclared.max_text_bytes.reset();
    const auto narrow{produce_window(32, 1, "b", registry)};
    const auto direct{meta::compose(produce_window(32, 0, "a", registry), narrow)};
    const meta::MetaLogDocument mixed{meta::compose(undeclared, narrow)};
    EXPECT_EQ(written_or_fail(meta::to_json(mixed, registry)),
              written_or_fail(meta::to_json(direct, registry)))
        << "an input declaring no cap holds its values whole, so compose caps each at L_C";
    for (const meta::TopKEntry& entry : mixed.stats.top_k)
        for (const meta::FieldHistogram& slot : entry.field_histograms)
            for (const auto& [value, count] : slot.value_counts)
                EXPECT_LE(value.size(), 32U) << "a composed value past L_C: " << value;
}

TEST(ComposeTextCap, DiffAtTwoCapsReadsTheLargerAtTheSmaller)
{
    meta::TemplateRegistry registry;
    const auto previous_wide{produce_window(64, 0, "a", registry)};
    const auto previous_narrow{produce_window(32, 0, "a", registry)};
    const auto current{produce_window(32, 1, "a", registry)};
    const meta::MetaLogDiff mixed_diff{meta::diff(previous_wide, current)};
    EXPECT_EQ(mixed_diff.max_text_bytes, std::optional<std::size_t>{32U})
        << "the diff declares the cap its members were read at, the smaller";
    const std::string mixed{written_or_fail(meta::to_json(mixed_diff))};
    const std::string direct{written_or_fail(meta::to_json(meta::diff(previous_narrow, current)))};
    EXPECT_EQ(mixed, direct)
        << "a diff across caps must read the larger document at the smaller cap, or every long "
           "value reads as turnover.\n  mixed:  "
        << mixed << "\n  direct: " << direct;
}
