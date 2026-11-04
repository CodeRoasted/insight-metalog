// invariant: the root max_text_bytes is declared on a raw document, a composed one and a
// MetaLogDiff exactly when the domain engages it, absence reading as uncapped.
// invariant: stats.param_histograms_size and stats.value_counts_size are declared exactly when the
// domain engages them.
// refs: DN-148.D8, F-SRC-metalog-spec:SPEC.md, F-SRC-metalog-spec:metalog.v0.schema.json
#include <glaze/glaze.hpp>
#include <gtest/gtest.h>

import insight.metalog.test;

#include "../written_or_fail.hpp"

namespace
{
namespace meta = insight::metalog;
using insight::metalog::test::make_event;

[[nodiscard]] std::chrono::system_clock::time_point hour(int index)
{
    return std::chrono::system_clock::time_point{} + std::chrono::hours{index};
}

// post: the next closed one-hour window of `engine`, opened at hour `hour_index`.
[[nodiscard]] meta::MetaLogDocument produce_window(meta::MetaLogEngine& engine, int hour_index)
{
    engine.open_window(hour(hour_index));
    engine.ingest_event(make_event("alpha"));
    engine.ingest_event(make_event("beta"));
    return engine.close_window(hour(hour_index + 1));
}
} // namespace

TEST(TextCapWire, ADefaultConfiguredDocumentDeclaresItsTextCapAtTheRoot)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{}};
    const auto doc{produce_window(engine, 0)};
    const std::string json{written_or_fail(meta::to_json(doc, engine.registry()))};
    EXPECT_NE(json.find(R"("max_text_bytes":64)"), std::string::npos)
        << "a default-configured producer caps observed text at 64 bytes, so SPEC 2.6 obliges it "
           "to declare the cap as the root member max_text_bytes; got: "
        << json;
    auto parsed{glz::read_json<glz::generic>(json)};
    ASSERT_TRUE(parsed.has_value()) << json;
    auto& root{*parsed};
    ASSERT_TRUE(root.contains("max_text_bytes")) << json;
    EXPECT_EQ(root["max_text_bytes"].get<double>(), 64.0) << json;
    ASSERT_TRUE(root.contains("stats")) << json;
    EXPECT_FALSE(root["stats"].contains("max_text_bytes"))
        << "the cap is a ROOT member, never a stats member: " << json;
}

TEST(TextCapWire, AComposedDocumentAndADiffDeclareTheCommonCap)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{.max_text_bytes = 128}};
    const auto earlier{produce_window(engine, 0)};
    const auto later{produce_window(engine, 1)};

    const auto composed{meta::compose(earlier, later)};
    const std::string composed_json{written_or_fail(meta::to_json(composed, engine.registry()))};
    EXPECT_NE(composed_json.find(R"("max_text_bytes":128)"), std::string::npos)
        << "compose carries the cap both inputs declare (SPEC 12.1); got: " << composed_json;

    const std::string diff_json{written_or_fail(meta::to_json(meta::diff(earlier, later)))};
    EXPECT_NE(diff_json.find(R"("max_text_bytes":128)"), std::string::npos)
        << "a MetaLogDiff declares the cap both inputs declare (SPEC 13.1); got: " << diff_json;
}

TEST(TextCapWire, AnUncappedPairComposesAndDiffsWithNoDeclaration)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{}};
    auto earlier{produce_window(engine, 0)};
    auto later{produce_window(engine, 1)};
    earlier.max_text_bytes.reset();
    later.max_text_bytes.reset();

    const std::string composed_json{
        written_or_fail(meta::to_json(meta::compose(earlier, later), engine.registry()))};
    EXPECT_EQ(composed_json.find("max_text_bytes"), std::string::npos)
        << "absence means uncapped, so a composer MUST NOT synthesize a cap neither input "
           "declares (SPEC 2.6, 12.1); got: "
        << composed_json;
    const std::string diff_json{written_or_fail(meta::to_json(meta::diff(earlier, later)))};
    EXPECT_EQ(diff_json.find("max_text_bytes"), std::string::npos)
        << "a MetaLogDiff omits the cap when neither input declares it (SPEC 13.1); got: "
        << diff_json;
}

TEST(TextCapWire, TheHistogramCapsAreDeclaredExactlyWhenHistogramsAreBuilt)
{
    meta::MetaLogEngine with_engine{
        meta::MetaLogConfig{.max_param_histograms = 3, .max_histogram_values = 16}};
    const auto with{produce_window(with_engine, 0)};
    const std::string with_json{written_or_fail(meta::to_json(with, with_engine.registry()))};
    auto parsed{glz::read_json<glz::generic>(with_json)};
    ASSERT_TRUE(parsed.has_value()) << with_json;
    auto& with_root{*parsed};
    ASSERT_TRUE(with_root.contains("stats")) << with_json;
    auto& stats{with_root["stats"]};
    ASSERT_TRUE(stats.contains("param_histograms_size"))
        << "a producer building histograms declares the slot cap (SPEC 3.5): " << with_json;
    EXPECT_EQ(stats["param_histograms_size"].get<double>(), 3.0) << with_json;
    ASSERT_TRUE(stats.contains("value_counts_size"))
        << "a producer building histograms declares the value cap (SPEC 3.5): " << with_json;
    EXPECT_EQ(stats["value_counts_size"].get<double>(), 16.0) << with_json;

    meta::MetaLogEngine without_engine{meta::MetaLogConfig{}};
    const auto without{produce_window(without_engine, 0)};
    const std::string without_json{
        written_or_fail(meta::to_json(without, without_engine.registry()))};
    EXPECT_EQ(without_json.find("param_histograms_size"), std::string::npos)
        << "a producer building no histograms declares no histogram cap: " << without_json;
    EXPECT_EQ(without_json.find("value_counts_size"), std::string::npos)
        << "a producer building no histograms declares no histogram cap: " << without_json;
}
