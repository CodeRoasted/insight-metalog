// NOLINTBEGIN Smoke test: consumes insight::metalog as if external.
#include <chrono>
#include <string>

#include <glaze/glaze.hpp>
#include <gtest/gtest.h>

// Black-box consumer: the package's only public surface is its named modules — there are no
// textual public headers.
// insight.metalog re-exports the MetaLog DTOs + engine + to_json; canon types (Timestamp/LogLevel/
// CanonicalEvent) are reachable-not-visible through it, so the consumer imports insight.canon too.
import insight.metalog;
import insight.canon;

TEST(InsightMetaLogPackage, ProducesSpecConformantDocument)
{
    using insight::Timestamp;
    namespace tok = insight::tokenization;
    namespace meta = insight::metalog;

    meta::MetaLogEngine engine;
    const Timestamp start = std::chrono::system_clock::now();
    engine.open_window(start);

    tok::CanonicalEvent ev_a;
    ev_a.template_str = "User <*> logged in";
    ev_a.level = insight::LogLevel::Info;

    tok::CanonicalEvent ev_b;
    ev_b.template_str = "Disk usage <*>%";
    ev_b.level = insight::LogLevel::Warn;

    engine.ingest_event(ev_a);
    engine.ingest_event(ev_a);
    engine.ingest_event(ev_b);

    auto doc{engine.close_window(start + std::chrono::minutes(5))};
    EXPECT_EQ(doc.metalog_version, "0.9.0");
    // The package version, not the spec version (SPEC §2.1) — the two moved apart when producer
    // 0.6.0 stayed frozen through the whole 1.x line. This is the only gate outside the cut
    // checklist that reads the emitted value, and it is the reason `kProducerVersion` cannot sit
    // stale: the expectation is the RELEASED REFERENCE's version, handed in by the test_package
    // recipe, so the two authorities are independent and the check follows every bump on its own.
    EXPECT_EQ(doc.producer.version, INSIGHT_METALOG_TESTED_VERSION);
    EXPECT_EQ(doc.window.lines_observed, 3U);
    EXPECT_EQ(doc.stats.unique_templates, 2U);
    EXPECT_EQ(doc.stats.top_k.size(), 2U);
    EXPECT_EQ(doc.stats.top_k.front().count, 2U);
    ASSERT_TRUE(doc.stats.entropy_bits.has_value());
    EXPECT_GT(*doc.stats.entropy_bits, 0.0);

    // 2 of A then 1 of B => bigrams A->A (1) and A->B (1).
    ASSERT_TRUE(doc.behavior.has_value());
    EXPECT_EQ(doc.behavior->ngram_size, 2U);
    EXPECT_EQ(doc.behavior->top_ngrams.size(), 2U);

    // First window in a session => no stability block.
    EXPECT_FALSE(doc.stability.has_value());

    // to_json() now emits a serialised glaze string; re-parse it generically to
    // assert the on-the-wire contract (keys/values), as an external consumer would.
    const std::string serialized = to_json(doc, engine.registry());
    auto parsed = glz::read_json<glz::generic>(serialized);
    ASSERT_TRUE(parsed.has_value()) << "serialised output did not parse: " << serialized;
    auto& json = *parsed;
    EXPECT_EQ(json["metalog_version"].get<std::string>(), "0.9.0") << serialized;
    EXPECT_EQ(json["stats"]["top_k"][0]["count"].get<double>(), 2.0) << serialized;
    EXPECT_TRUE(json["stats"]["top_k"][0]["template_id"].get<std::string>().starts_with("h:"))
        << serialized;
    EXPECT_TRUE(json["stats"].contains("entropy_bits")) << serialized;
    EXPECT_TRUE(json.contains("behavior")) << serialized;
    EXPECT_FALSE(json.contains("stability")) << serialized;
    EXPECT_FALSE(json.contains("attribution")) << serialized; // reserved by spec for v1.0
}

TEST(InsightMetaLogPackage, SecondWindowEmitsStability)
{
    using insight::Timestamp;
    namespace tok = insight::tokenization;
    namespace meta = insight::metalog;

    meta::MetaLogEngine engine;
    const Timestamp t0 = std::chrono::system_clock::now();

    auto fire = [&](const char* tmpl)
    {
        tok::CanonicalEvent ev;
        ev.template_str = tmpl;
        ev.level = insight::LogLevel::Info;
        engine.ingest_event(ev);
    };

    engine.open_window(t0);
    for (int i = 0; i < 10; ++i)
    {
        fire("alpha");
        fire("beta");
    }
    auto d1{engine.close_window(t0 + std::chrono::seconds(60))};

    engine.open_window(t0 + std::chrono::seconds(60));
    for (int i = 0; i < 10; ++i)
    {
        fire("alpha");
        fire("beta");
    }
    auto d2{engine.close_window(t0 + std::chrono::seconds(120))};

    ASSERT_TRUE(d2.stability.has_value());
    EXPECT_EQ(d2.stability->previous_window_end_iso, d1.window.end_iso);
    EXPECT_LT(d2.stability->js_divergence, 1e-6);
    EXPECT_GT(d2.stability->stability_score, 0.999);

    const std::string serialized = to_json(d2, engine.registry());
    auto parsed = glz::read_json<glz::generic>(serialized);
    ASSERT_TRUE(parsed.has_value()) << "serialised output did not parse: " << serialized;
    auto& json = *parsed;
    EXPECT_TRUE(json.contains("stability")) << serialized;
    EXPECT_EQ(json["stability"]["previous_window_end"].get<std::string>(), d1.window.end_iso)
        << serialized;
}
// NOLINTEND
