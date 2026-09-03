// Unit tests: allow short identifiers and test-specific patterns.
//
// THE JOINT GRAIN (DN-43.O3 item 3). canon was proven at the line, metalog at the window, and the
// two were never JOINED — which is why a strategy that emptied `content` on every line of a
// timestamped application stream shipped through nine canonicalization generations and reached
// published evidence.
//
// Every assertion below is FALSE under the pre-DN-43 pipeline, and each fails for its own reason:
// the whole file collapsed into ONE template (the SHA-256 prefix of the empty string), so an
// all-INFO window followed by an all-ERROR window published `new_templates: 0`, one `top_k` row,
// `level_cardinality: 1`, and every row spelled `INFO`. Nothing at either grain alone could see it:
// canon's line tests never asked what a WINDOW of such lines looks like, and metalog's window tests
// build CanonicalEvents by hand and so never route a line through a strategy.

#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;

// The two windows: a steady INFO/DEBUG regime, then an ERROR/WARN burst. Shaped after
// scripts/determinism_corpus/service_a.log — the population DN-43.O2 gate B pre-registers.
constexpr std::array kWindowOne{
    std::string_view{
        "2026-05-31T08:00:01Z INFO request method=GET path=/api/users/1000 status=200"},
    std::string_view{"2026-05-31T08:01:02Z INFO request method=POST path=/api/orders status=201"},
    std::string_view{"2026-05-31T08:02:03Z DEBUG db query=select_orders duration_ms=15 rows=2"},
    std::string_view{"2026-05-31T08:03:04Z INFO cache key=session:1021 hit=true"},
};
constexpr std::array kWindowTwo{
    std::string_view{"2026-05-31T09:00:01Z ERROR upstream timeout service=payments after_ms=80"},
    std::string_view{"2026-05-31T09:01:02Z WARN slow request path=/api/report latency_ms=103"},
    std::string_view{
        "2026-05-31T09:02:03Z ERROR request method=GET path=/api/users/1014 status=500"},
};

class WindowingSeamTest : public ::testing::Test
{
  protected:
    static constexpr std::size_t kArenaBytes{std::size_t{1} << 20};

    tok::ArenaAllocator arena{kArenaBytes};
    // Core-only: these are universal representation formats, no dialect vocabulary is involved.
    insight::semantic::ComposedSemantics composed{insight::semantic::compose({})};
    tok::Tokenizer tokenizer{arena, tok::MaskConfig{}, composed};

    void ingest(meta::MetaLogEngine& engine, std::span<const std::string_view> lines)
    {
        for (const std::string_view raw : lines)
        {
            auto event{tokenizer.process_line(raw)};
            ASSERT_TRUE(event.has_value()) << "line dropped: " << raw;
            engine.ingest_event(*event);
        }
    }
};

TEST_F(WindowingSeamTest, AnAllInfoWindowFollowedByAnAllErrorWindowPublishesNewTemplates)
{
    meta::MetaLogConfig cfg;
    cfg.emit_stability = true; // new_templates is the window-over-window carrier
    meta::MetaLogEngine engine{cfg};

    using Clock = std::chrono::system_clock;
    const Clock::time_point t0{std::chrono::seconds{1700000000}};
    const Clock::time_point t1{std::chrono::seconds{1700000060}};
    const Clock::time_point t2{std::chrono::seconds{1700000120}};

    engine.open_window(t0);
    ingest(engine, kWindowOne);
    const auto doc1{engine.close_window(t1)};

    engine.open_window(t1);
    ingest(engine, kWindowTwo);
    const auto doc2{engine.close_window(t2)};

    // 1. THE PROPERTY. An all-ERROR window after an all-INFO one carries structure the first did
    //    not. Zero here means the two windows were structurally the same document, which is what
    //    "one empty template for everything" produces.
    ASSERT_TRUE(doc2.stability.has_value());
    EXPECT_GT(doc2.stability->new_templates, 0U)
        << "window 2 declared no new template; unique_templates w1=" << doc1.stats.unique_templates
        << " w2=" << doc2.stats.unique_templates;

    // 2. Per-line structure survives to the window: four lines, four templates, none empty. The
    //    defect published ONE top_k row whose id was the hash of nothing.
    EXPECT_EQ(doc1.stats.unique_templates, kWindowOne.size())
        << "window 1 templates=" << doc1.stats.unique_templates << " lines=" << kWindowOne.size();
    for (const auto& row : doc1.stats.top_k)
        EXPECT_FALSE(engine.registry().lookup(row.template_id).empty())
            << "an empty template reached top_k: " << insight::render(row.template_id);

    // 3. The level is READ, and it partitions the cube. `level_cardinality` is exactly the field
    //    that reported 1 on the published window.
    ASSERT_TRUE(doc1.acquisition.has_value());
    ASSERT_TRUE(doc2.acquisition.has_value());
    EXPECT_EQ(doc1.acquisition->level_cardinality, 2U) << "window 1 observed INFO and DEBUG";
    EXPECT_EQ(doc2.acquisition->level_cardinality, 2U) << "window 2 observed ERROR and WARN";
}

// The publication half (DN-43.D10), asserted on the SERIALIZED bytes rather than on the domain
// object: repairing the reading and keeping the fabrication at publication would leave the only
// half an external reader sees exactly as wrong as before.
TEST_F(WindowingSeamTest, AnAbsentLevelIsOmittedFromTheWireRatherThanRenderedAsInfo)
{
    meta::MetaLogConfig cfg;
    meta::MetaLogEngine engine{cfg};

    using Clock = std::chrono::system_clock;
    const Clock::time_point t0{std::chrono::seconds{1700000000}};
    const Clock::time_point t1{std::chrono::seconds{1700000060}};

    // A body with no level word and no lexicon cue: the level is genuinely absent, and that is a
    // large, correct residue — `GET /api/users 200` declares nothing.
    engine.open_window(t0);
    for (int i = 0; i < 3; ++i)
    {
        auto event{tokenizer.process_line("Jan 15 10:00:00 web01 nginx[2451]: GET /api/users 200")};
        ASSERT_TRUE(event.has_value());
        ASSERT_EQ(event->level, insight::LogLevel::Unknown) << "the fixture must carry NO level";
        engine.ingest_event(*event);
    }
    const auto doc{engine.close_window(t1)};

    const std::string json{meta::to_json(doc, engine.registry())};
    EXPECT_EQ(json.find("\"level\": \"INFO\""), std::string::npos)
        << "an absence was published as the fact INFO; document:\n"
        << json;
    EXPECT_EQ(json.find("\"level\":\"INFO\""), std::string::npos) << json;

    // The cube is the OTHER act and it must NOT omit: §16.4 makes an absent axis mean aggregated
    // (`*`), so the cell whose level was never observed carries its own value.
    EXPECT_NE(json.find("UNKNOWN"), std::string::npos)
        << "the cube's level axis lost the observed-nothing cell; document:\n"
        << json;
    ASSERT_TRUE(doc.acquisition.has_value());
    EXPECT_EQ(doc.acquisition->level_cardinality, 1U) << "one observed level state: none";
}

} // namespace
