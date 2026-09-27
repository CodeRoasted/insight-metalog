// refs: DN-103.D22, ADR-16.D3
// invariant: a consumer that outlives the engine takes its registry over whole, and every template
// a closed window named still resolves through it once the engine is gone.
#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace meta = insight::metalog;
using insight::metalog::test::make_event;
using namespace std::chrono_literals;

TEST(MetaLogRegistryTakeOver, EveryTemplateAClosedWindowNamedResolvesAfterTheEngineIsGone)
{
    meta::MetaLogDocument doc;
    meta::TemplateRegistry taken;
    {
        meta::MetaLogEngine engine;
        const auto start{std::chrono::system_clock::now()};
        engine.open_window(start);
        engine.ingest_event(make_event("alpha job started"));
        engine.ingest_event(make_event("beta job finished"));
        doc = engine.close_window(start + 1s);
        taken = std::move(engine).take_registry();
    }

    ASSERT_EQ(doc.stats.top_k.size(), 2U)
        << "the window names " << doc.stats.top_k.size() << " templates, the stream carried 2";
    EXPECT_EQ(taken.size(), 2U) << "the registry taken over holds " << taken.size()
                                << " templates, the engine interned 2";
    for (const auto& entry : doc.stats.top_k)
        EXPECT_FALSE(taken.lookup(entry.template_id).empty())
            << "template " << insight::render(entry.template_id)
            << " named by the closed window does not resolve through the registry taken over";
}

} // namespace
