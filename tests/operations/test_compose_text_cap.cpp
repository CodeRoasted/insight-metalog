// invariant: compose() and diff() refuse two documents declaring different text caps, an absent
// declaration being a value of its own, because no member of a document is ever capped again.
// refs: DN-148.D8
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

// post: one failure naming `call` unless `operation` throws std::invalid_argument naming
// max_text_bytes.
template <typename Operation>
void expect_text_cap_refusal(Operation operation, std::string_view call)
{
    try
    {
        static_cast<void>(operation());
        ADD_FAILURE() << call << " accepted two documents declaring different max_text_bytes";
    }
    catch (const std::invalid_argument& error)
    {
        EXPECT_NE(std::string_view{error.what()}.find("max_text_bytes"), std::string_view::npos)
            << call << " refused, but its message does not name max_text_bytes: " << error.what();
    }
}

} // namespace

TEST(ComposeTextCap, ComposeRefusesTwoDeclaredCapsInEitherOrder)
{
    meta::TemplateRegistry registry;
    const auto wide{produce_window(64, 0, "a", registry)};
    const auto narrow{produce_window(32, 1, "b", registry)};
    expect_text_cap_refusal([&] { return meta::compose(wide, narrow); }, "compose(64, 32)");
    expect_text_cap_refusal([&] { return meta::compose(narrow, wide); }, "compose(32, 64)");
}

TEST(ComposeTextCap, ComposeRefusesADeclaredCapAgainstAnUndeclaredDocumentInEitherOrder)
{
    meta::TemplateRegistry registry;
    const auto wide{produce_window(64, 0, "a", registry)};
    meta::MetaLogDocument undeclared{produce_window(64, 1, "b", registry)};
    undeclared.max_text_bytes.reset();
    expect_text_cap_refusal([&] { return meta::compose(wide, undeclared); },
                            "compose(64, undeclared)");
    expect_text_cap_refusal([&] { return meta::compose(undeclared, wide); },
                            "compose(undeclared, 64)");
}

TEST(ComposeTextCap, DiffRefusesTwoDeclaredCapsInEitherOrder)
{
    meta::TemplateRegistry registry;
    const auto wide{produce_window(64, 0, "a", registry)};
    const auto narrow{produce_window(32, 1, "a", registry)};
    expect_text_cap_refusal([&] { return meta::diff(wide, narrow); }, "diff(64, 32)");
    expect_text_cap_refusal([&] { return meta::diff(narrow, wide); }, "diff(32, 64)");
}

TEST(ComposeTextCap, DiffRefusesADeclaredCapAgainstAnUndeclaredDocumentInEitherOrder)
{
    meta::TemplateRegistry registry;
    const auto wide{produce_window(64, 0, "a", registry)};
    meta::MetaLogDocument undeclared{produce_window(64, 1, "a", registry)};
    undeclared.max_text_bytes.reset();
    expect_text_cap_refusal([&] { return meta::diff(wide, undeclared); }, "diff(64, undeclared)");
    expect_text_cap_refusal([&] { return meta::diff(undeclared, wide); }, "diff(undeclared, 64)");
}

// assert: the control: two documents at one cap, or two declaring none, go ahead, and the result
// declares the cap both inputs declare.
TEST(ComposeTextCap, EqualDeclarationsComposeAndDiffDeclaringTheirCommonCap)
{
    meta::TemplateRegistry registry;
    const auto earlier{produce_window(32, 0, "a", registry)};
    const auto later{produce_window(32, 1, "a", registry)};
    EXPECT_EQ(meta::compose(earlier, later).max_text_bytes, std::optional<std::size_t>{32U})
        << "compose of two L = 32 documents must declare 32";
    EXPECT_EQ(meta::diff(earlier, later).max_text_bytes, std::optional<std::size_t>{32U})
        << "diff of two L = 32 documents must declare 32";

    meta::MetaLogDocument earlier_undeclared{earlier};
    meta::MetaLogDocument later_undeclared{later};
    earlier_undeclared.max_text_bytes.reset();
    later_undeclared.max_text_bytes.reset();
    EXPECT_EQ(meta::compose(earlier_undeclared, later_undeclared).max_text_bytes, std::nullopt)
        << "compose of two undeclared documents must declare none";
    EXPECT_EQ(meta::diff(earlier_undeclared, later_undeclared).max_text_bytes, std::nullopt)
        << "diff of two undeclared documents must declare none";
}
