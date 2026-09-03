// Unit tests: allow short identifiers and test-specific patterns
//
// retention_profile_name() — the DERIVATION of SPEC §2.4's `retention_profile` from the producer
// configuration in force. The gate that consumes the value is measured next door
// (test_processing_identifiers.cpp); this file measures the two properties the gate silently
// assumes of whatever string it is handed.
//
// HOMING — unit grain. The subject is a pure function of a config struct: no window, no engine, no
// seam. Whether the SHIPPING pipeline actually calls it is a different property with a different
// oracle, and it is measured where the pipeline lives
// (insight-eidos/engine/tests/pipeline/retention_profile_gate_test.cpp) — a unit test here cannot
// see a caller and must not pretend to.

#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace meta = insight::metalog;

[[nodiscard]] meta::MetaLogConfig tuple_of(std::size_t top_k, std::size_t reservoir,
                                           std::size_t per_kind_cap, std::size_t error_reserve)
{
    meta::MetaLogConfig cfg;
    cfg.top_k_size = top_k;
    cfg.reservoir_size = reservoir;
    cfg.reservoir_per_kind_cap = per_kind_cap;
    cfg.reservoir_error_reserve = error_reserve;
    return cfg;
}

} // namespace

// ── The spelling, pinned once ──────────────────────────────────────────────────────────────────
//
// One literal, so the shape is reviewable and a reader can check the doc comment against a fact.
// It also makes an accidental reformatting (a separator swap, a dropped axis) a red rather than a
// silent re-mint of every profile name in the fleet — which would refuse every existing baseline
// on the next deploy.
TEST(RetentionProfileName, SpellsTheGenerationThenTheFourAxes)
{
    EXPECT_EQ(meta::retention_profile_name(tuple_of(128, 64, 0, 16)), "salience-1/k128-m64-c0-e16");
    EXPECT_EQ(meta::retention_profile_name(tuple_of(0, 0, 0, 0)), "salience-1/k0-m0-c0-e0");
}

// ── DETERMINISM — the same tuple always derives the same name ──────────────────────────────────
//
// Not a tautology about a pure function: the derivation could have reached for a hash with an
// unseeded engine, a locale-sensitive integer formatter, or a pointer/address ordering. Two
// independently built configs, compared byte-for-byte.
TEST(RetentionProfileName, TheSameTupleAlwaysDerivesTheSameName)
{
    const auto first{meta::retention_profile_name(tuple_of(128, 64, 4, 16))};
    const auto second{meta::retention_profile_name(tuple_of(128, 64, 4, 16))};
    EXPECT_EQ(first, second);
    // Also stable against a config that reached the same tuple by a different route (defaults
    // partly untouched vs. every member assigned).
    meta::MetaLogConfig assembled;
    assembled.reservoir_error_reserve = 16;
    assembled.reservoir_per_kind_cap = 4;
    assembled.reservoir_size = 64;
    assembled.top_k_size = 128;
    EXPECT_EQ(meta::retention_profile_name(assembled), first);
}

// ── INJECTIVITY — moving ANY axis moves the name ───────────────────────────────────────────────
//
// The property the §2.4 gate rests on: two different retention tuples must never collide onto one
// name, or the gate certifies a comparability that does not hold. Each axis is moved on its own,
// so a derivation that forgot one member (the shape of DN-52's original defect, one level down)
// is caught by the arm naming that member rather than by an aggregate.
TEST(RetentionProfileName, EveryAxisIsPartOfTheName)
{
    const auto base{tuple_of(128, 64, 4, 16)};
    const auto base_name{meta::retention_profile_name(base)};

    struct Axis
    {
        std::string_view name;
        meta::MetaLogConfig moved;
    };
    const std::array<Axis, 4> axes{{
        {"top_k_size", tuple_of(64, 64, 4, 16)},
        {"reservoir_size", tuple_of(128, 32, 4, 16)},
        {"reservoir_per_kind_cap", tuple_of(128, 64, 8, 16)},
        {"reservoir_error_reserve", tuple_of(128, 64, 4, 8)},
    }};
    for (const auto& axis : axes)
        EXPECT_NE(meta::retention_profile_name(axis.moved), base_name)
            << "moving " << axis.name << " left the profile name unchanged at " << base_name
            << " — a document produced under the new value would compare EQUAL to one produced "
               "under the old, which is exactly what §2.4's gate exists to refuse.";
}

// ── INJECTIVITY, the adversarial arm — digits must not run together ────────────────────────────
//
// The failure this arm exists for: a derivation that concatenated the numbers without separators
// (or with an ambiguous one) maps (k=1, m=28) and (k=12, m=8) onto the same string. That is not a
// hypothetical shape — it is what "just join the values" produces.
TEST(RetentionProfileName, AdjacentAxesCannotBorrowEachOthersDigits)
{
    EXPECT_NE(meta::retention_profile_name(tuple_of(1, 28, 0, 0)),
              meta::retention_profile_name(tuple_of(12, 8, 0, 0)));
    EXPECT_NE(meta::retention_profile_name(tuple_of(0, 0, 1, 28)),
              meta::retention_profile_name(tuple_of(0, 0, 12, 8)));
}

// ── The arithmetic generation is IN the name ───────────────────────────────────────────────────
//
// §2.4 requires the profile to cover the salience arithmetic, which no config member expresses. If
// the generation ever dropped out of the derivation, two binaries with different band ladders
// would publish the same profile at the same tuple and compare as comparable.
TEST(RetentionProfileName, CarriesTheSalienceArithmeticGeneration)
{
    const auto name{meta::retention_profile_name(tuple_of(128, 64, 4, 16))};
    EXPECT_TRUE(name.starts_with(meta::kSalienceArithmeticGeneration))
        << "the derived name does not open with the salience arithmetic generation ("
        << meta::kSalienceArithmeticGeneration << "): " << name;
}
