// invariant: a window none of whose lines carried an event time has NO envelope — no start, end or
// duration_seconds on the wire, and no previous_window_end in the next window's stability block.
// note: a unit test of the engine and its serializer; no RNG, no threads, no wall clock.
// refs: DN-137.O1
#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace meta = insight::metalog;
using insight::metalog::test::make_event;

const insight::Timestamp kOpen{std::chrono::sys_days{std::chrono::year{2026} / 10 / 1}};

[[nodiscard]] std::string window_object(const std::string& json)
{
    const std::size_t at{json.find("\"window\":{")};
    if (at == std::string::npos)
        return "<no window object>";
    return json.substr(at, json.find('}', at) - at + 1);
}

} // namespace

TEST(AbsentEnvelope, AWindowReportedWithNoEventTimeCarriesNoEnvelopeOnTheWire)
{
    meta::MetaLogEngine engine;
    engine.open_window(kOpen);
    engine.ingest_event(make_event("connection reset"));
    const auto doc{
        engine.close_window(kOpen + std::chrono::seconds{30}, meta::ReportedWindowBounds{})};
    EXPECT_FALSE(doc.window.envelope.has_value())
        << "the machinery times were substituted for an event time no line carried: ["
        << doc.window.envelope->bounds.start_iso << " .. " << doc.window.envelope->bounds.end_iso
        << "]";
    EXPECT_EQ(doc.window.lines_observed, 1U);

    const auto written{meta::to_json(doc, engine.registry())};
    ASSERT_TRUE(written.has_value()) << written.error();
    const std::string window{window_object(*written)};
    EXPECT_EQ(window, R"("window":{"lines_observed":1})")
        << "the window object must carry lines_observed alone; got " << window;
}

TEST(AbsentEnvelope, AReportedEnvelopeIsWrittenAsTheEventTimeBounds)
{
    meta::MetaLogEngine engine;
    engine.open_window(kOpen);
    engine.ingest_event(make_event("connection reset"));
    const meta::EventTimeEnvelope reported{.start = kOpen + std::chrono::seconds{5},
                                           .end = kOpen + std::chrono::seconds{9}};
    const auto doc{engine.close_window(kOpen + std::chrono::seconds{30},
                                       meta::ReportedWindowBounds{.envelope = reported})};
    ASSERT_TRUE(doc.window.envelope.has_value());
    EXPECT_EQ(doc.window.envelope->bounds.start_iso, meta::format_rfc3339_utc(reported.start));
    EXPECT_EQ(doc.window.envelope->bounds.end_iso, meta::format_rfc3339_utc(reported.end));
    EXPECT_EQ(doc.window.envelope->duration_seconds, 4U);
}

TEST(AbsentEnvelope, TheNextWindowsStabilityOmitsAnEndThePreviousWindowNeverHad)
{
    meta::MetaLogEngine engine;
    engine.open_window(kOpen);
    engine.ingest_event(make_event("a"));
    const auto first{
        engine.close_window(kOpen + std::chrono::seconds{30}, meta::ReportedWindowBounds{})};
    ASSERT_FALSE(first.window.envelope.has_value());

    engine.open_window(kOpen + std::chrono::seconds{30});
    engine.ingest_event(make_event("a"));
    const auto second{engine.close_window(kOpen + std::chrono::seconds{60})};
    ASSERT_TRUE(second.stability.has_value())
        << "the stability block must still be emitted: the template distributions exist";
    EXPECT_FALSE(second.stability->previous_window_end_iso.has_value())
        << "previous_window_end names an end the previous window never had: "
        << *second.stability->previous_window_end_iso;

    const auto written{meta::to_json(second, engine.registry())};
    ASSERT_TRUE(written.has_value()) << written.error();
    EXPECT_EQ(written->find("previous_window_end"), std::string::npos) << *written;
}
