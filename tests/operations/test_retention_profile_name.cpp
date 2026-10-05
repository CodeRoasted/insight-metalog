
// refs: F-SRC-metalog-spec:SPEC.md
// invariant: the DERIVATION of retention_profile from the producer configuration; the gate that
// CONSUMES the value is measured in test_processing_identifiers.cpp next door.
// invariant: this file measures the two properties that gate silently assumes of whatever string it
// is handed -- determinism and injectivity.
// note: homed at unit grain: the subject is a pure function of a config struct, with no caller.
#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace meta = insight::metalog;

[[nodiscard]] meta::MetaLogConfig tuple_of(std::size_t top_k, std::size_t reservoir,
                                           std::size_t per_kind_cap, std::size_t error_reserve,
                                           std::size_t ngram_size = 2)
{
    meta::MetaLogConfig cfg;
    cfg.top_k_size = top_k;
    cfg.reservoir_size = reservoir;
    cfg.reservoir_per_kind_cap = per_kind_cap;
    cfg.reservoir_error_reserve = error_reserve;
    cfg.ngram_size = ngram_size;
    return cfg;
}

} // namespace

// invariant: the spelling is pinned as ONE literal, so the shape is reviewable and an accidental
// reformat reds rather than silently re-minting every profile name in the fleet.
TEST(RetentionProfileName, SpellsTheGenerationThenTheFiveAxes)
{
    EXPECT_EQ(meta::retention_profile_name(tuple_of(128, 64, 0, 16, 3)),
              "salience-1/k128-m64-c0-e16-n3");
    EXPECT_EQ(meta::retention_profile_name(tuple_of(0, 0, 0, 0)), "salience-1/k0-m0-c0-e0-n2");
}

// invariant: determinism is not a tautology about a pure function -- the derivation could have
// reached for an unseeded hash, a locale-sensitive formatter or a pointer ordering.
TEST(RetentionProfileName, TheSameTupleAlwaysDerivesTheSameName)
{
    const auto first{meta::retention_profile_name(tuple_of(128, 64, 4, 16))};
    const auto second{meta::retention_profile_name(tuple_of(128, 64, 4, 16))};
    EXPECT_EQ(first, second);
    // note: also stable against a config reaching the same tuple by a different route.
    meta::MetaLogConfig assembled;
    assembled.reservoir_error_reserve = 16;
    assembled.reservoir_per_kind_cap = 4;
    assembled.reservoir_size = 64;
    assembled.top_k_size = 128;
    assembled.ngram_size = 2;
    EXPECT_EQ(meta::retention_profile_name(assembled), first);
}

// refs: ADR-20.D17
// invariant: injectivity is what the comparability gate rests on: two different retention tuples
// must never collide onto one name, or the gate certifies a comparability that does not hold.
// note: each axis moves on its own, so a forgotten member is named by its own arm.
// invariant: the ngram_size arm is DN-56.D11's: an order-2 and an order-3 document under one name
// pass the gate and diff into total n-gram turnover.
TEST(RetentionProfileName, EveryAxisIsPartOfTheName)
{
    const auto base{tuple_of(128, 64, 4, 16)};
    const auto base_name{meta::retention_profile_name(base)};

    struct Axis
    {
        std::string_view name;
        meta::MetaLogConfig moved;
    };
    const std::array<Axis, 5> axes{{
        {"top_k_size", tuple_of(64, 64, 4, 16)},
        {"reservoir_size", tuple_of(128, 32, 4, 16)},
        {"reservoir_per_kind_cap", tuple_of(128, 64, 8, 16)},
        {"reservoir_error_reserve", tuple_of(128, 64, 4, 8)},
        {"ngram_size", tuple_of(128, 64, 4, 16, 3)},
    }};
    for (const auto& axis : axes)
        EXPECT_NE(meta::retention_profile_name(axis.moved), base_name)
            << "moving " << axis.name << " left the profile name unchanged at " << base_name
            << " — a document produced under the new value would compare EQUAL to one produced "
               "under the old, which is exactly what §2.4's gate exists to refuse.";
}

// invariant: the adversarial arm -- a derivation that joined the numbers without a separator maps
// (k=1, m=28) and (k=12, m=8) onto one string, which is what just-join-the-values produces.
TEST(RetentionProfileName, AdjacentAxesCannotBorrowEachOthersDigits)
{
    EXPECT_NE(meta::retention_profile_name(tuple_of(1, 28, 0, 0)),
              meta::retention_profile_name(tuple_of(12, 8, 0, 0)));
    EXPECT_NE(meta::retention_profile_name(tuple_of(0, 0, 1, 28)),
              meta::retention_profile_name(tuple_of(0, 0, 12, 8)));
}

// invariant: the n axis names the order the engine APPLIES, never the request: a request outside
// the implemented orders is clamped, and a stamp naming it would name a retention never built.
TEST(RetentionProfileName, TheNgramAxisNamesTheClampedOrderTheEngineApplies)
{
    const auto above{meta::retention_profile_name(tuple_of(128, 64, 4, 16, 7))};
    const auto below{meta::retention_profile_name(tuple_of(128, 64, 4, 16, 0))};
    EXPECT_EQ(above, meta::retention_profile_name(tuple_of(128, 64, 4, 16, 3)))
        << "a request of 7 is built at order 3, so it must stamp n3: " << above;
    EXPECT_EQ(below, meta::retention_profile_name(tuple_of(128, 64, 4, 16, 2)))
        << "a request of 0 is built at order 2, so it must stamp n2: " << below;
    EXPECT_NE(above, below) << "the two clamped orders collapsed onto one name: " << above;

    meta::MetaLogEngine engine{tuple_of(8, 0, 0, 0, 7)};
    const auto start{std::chrono::system_clock::now()};
    engine.open_window(start);
    for (int rep = 0; rep < 4; ++rep)
    {
        engine.ingest_event(insight::metalog::test::make_event("order alpha"));
        engine.ingest_event(insight::metalog::test::make_event("order beta"));
    }
    const auto doc{engine.close_window(start + std::chrono::seconds(60))};
    ASSERT_TRUE(doc.behavior.has_value()) << "eight records must yield a behavior block";
    EXPECT_EQ(doc.behavior->ngram_size, 3U)
        << "the engine built a request of 7 at an order other than the n3 its stamp names";
}

// invariant: the salience arithmetic generation is IN the name because no config member expresses
// it; without it two binaries with different band ladders would compare as comparable.
TEST(RetentionProfileName, CarriesTheSalienceArithmeticGeneration)
{
    const auto name{meta::retention_profile_name(tuple_of(128, 64, 4, 16))};
    EXPECT_TRUE(name.starts_with(meta::kSalienceArithmeticGeneration))
        << "the derived name does not open with the salience arithmetic generation ("
        << meta::kSalienceArithmeticGeneration << "): " << name;
}
