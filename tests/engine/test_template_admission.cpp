// refs: DN-103.D19
// invariant: a new-template admission is asked only for a template the registry does not hold,
// and a refusal leaves the engine exactly as it was before the event.
#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace meta = insight::metalog;
using insight::metalog::test::make_event;
using namespace std::chrono_literals;

// invariant: answers every question with `answer` and records the text of each.
class RecordingAdmission final : public meta::NewTemplateAdmission
{
  public:
    explicit RecordingAdmission(bool answer) : answer_{answer} {}

    [[nodiscard]] bool admit(std::string_view template_str) override
    {
        asked_.emplace_back(template_str);
        return answer_;
    }

    [[nodiscard]] const std::vector<std::string>& asked() const noexcept
    {
        return asked_;
    }

  private:
    bool answer_;
    std::vector<std::string> asked_;
};

[[nodiscard]] std::string joined(const std::vector<std::string>& texts)
{
    std::string out;
    for (const auto& text : texts)
        out += (out.empty() ? "" : ", ") + text;
    return "[" + out + "]";
}

TEST(MetaLogTemplateAdmission, IsAskedOnlyForATemplateTheRegistryDoesNotHold)
{
    meta::MetaLogEngine engine;
    RecordingAdmission admit{true};
    const auto start{std::chrono::system_clock::now()};
    engine.open_window(start);
    EXPECT_TRUE(engine.ingest_event(make_event("a"), admit));
    EXPECT_TRUE(engine.ingest_event(make_event("a"), admit));
    (void)engine.close_window(start + 1s);
    engine.open_window(start + 2s);
    EXPECT_TRUE(engine.ingest_event(make_event("a"), admit));
    EXPECT_TRUE(engine.ingest_event(make_event("b"), admit));

    const std::vector<std::string> expected{"a", "b"};
    EXPECT_EQ(admit.asked(), expected)
        << "asked for " << joined(admit.asked()) << ", expected " << joined(expected)
        << ": a template interned in an earlier window is not new";
}

TEST(MetaLogTemplateAdmission, ARefusalIngestsAndInternsNothing)
{
    meta::MetaLogEngine engine;
    RecordingAdmission refuse{false};
    const auto start{std::chrono::system_clock::now()};
    engine.open_window(start);
    engine.ingest_event(make_event("a"));
    EXPECT_TRUE(engine.ingest_event(make_event("a"), refuse))
        << "a template the registry holds is ingested without asking the admission";
    EXPECT_FALSE(engine.ingest_event(make_event("b"), refuse));

    EXPECT_EQ(engine.registry().size(), 1U)
        << "the refused template was interned: the registry holds " << engine.registry().size();
    const auto doc{engine.close_window(start + 1s)};
    EXPECT_EQ(doc.window.lines_observed, 2U)
        << "the window counts the refused event: " << doc.window.lines_observed << " lines";
    EXPECT_EQ(doc.stats.unique_templates, 1U)
        << "the window names the refused template: " << doc.stats.unique_templates << " templates";
}

TEST(MetaLogTemplateAdmission, ADiscardedWindowIsNeverClosedAndTheRegistryKeepsItsTemplates)
{
    meta::MetaLogEngine engine;
    const auto start{std::chrono::system_clock::now()};
    engine.open_window(start);
    engine.ingest_event(make_event("a"));
    engine.discard_window();

    EXPECT_THROW((void)engine.close_window(start + 1s), std::logic_error)
        << "a discarded window was closed";
    EXPECT_EQ(engine.registry().size(), 1U)
        << "the discard evicted the registry: it holds " << engine.registry().size();

    RecordingAdmission count{true};
    engine.open_window(start + 2s);
    EXPECT_TRUE(engine.ingest_event(make_event("a"), count));
    EXPECT_TRUE(count.asked().empty())
        << "a template kept across the discard was asked for again: " << joined(count.asked());
    const auto doc{engine.close_window(start + 3s)};
    EXPECT_EQ(doc.window.lines_observed, 1U)
        << "the next window carries the discarded one's events: " << doc.window.lines_observed;
}

} // namespace
