// NOLINTBEGIN
// Unit tests: allow short identifiers and test-specific patterns
// Standing gate: full-document cross-machine bit-identity golden (SHA-256 frozen).

#include <gtest/gtest.h>
#include <picosha2.h>

import insight.metalog.test;

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;
using insight::metalog::test::ParamEvent;

// ── Standing gate: full-document cross-machine bit-identity golden ────────────
// The permanent determinism fixture (alongside S15Conformance). A fixed two-window
// scenario drives every float path — entropy, KL/JS/stability, branching
// entropy, per-param histograms, HLL approximate_cardinality — and the SHA-256 of
// the serialised documents is FROZEN. Any architecture/compiler MUST reproduce the
// exact bytes; a mismatch is a cross-machine determinism regression. Re-derive the
// golden ONLY for an intentional contract change — and it must then hold across the
// cross-arch CI matrix (.github/workflows/determinism.yml; determinism_model.md).
TEST(DeterminismGate, FullDocumentByteIdentityGolden)
{
    meta::MetaLogConfig cfg;
    cfg.max_param_histograms = 3; // emit_stability defaults true
    meta::MetaLogEngine engine{cfg};
    using Clock = std::chrono::system_clock;
    const Clock::time_point t0{std::chrono::seconds{1700000000}};
    const Clock::time_point t1{std::chrono::seconds{1700000060}};
    const Clock::time_point t2{std::chrono::seconds{1700000120}};

    const auto ingest_window = [&engine](int err_modulus)
    {
        for (int i = 0; i < 300; ++i)
        {
            const std::string path{"/api/r" + std::to_string(i % 7)};
            const bool err{i % err_modulus == 0};
            auto event{ParamEvent::make("GET <*> -> <*>", {path, err ? "500" : "200"},
                                        err ? insight::LogLevel::Error : insight::LogLevel::Info)};
            engine.ingest_event(event.event);
        }
        for (int i = 0; i < 60; ++i)
        {
            auto event{ParamEvent::make("worker <*> step <*>",
                                        {std::to_string(i % 11), std::to_string(i % 3)})};
            engine.ingest_event(event.event);
        }
        auto fatal{ParamEvent::make("disk <*> failed", {"sda1"}, insight::LogLevel::Fatal)};
        engine.ingest_event(fatal.event);
    };

    engine.open_window(t0);
    ingest_window(5); // window 1: ~20% errors
    const auto doc1{engine.close_window(t1)};
    engine.open_window(t1);
    ingest_window(2); // window 2: ~50% errors → divergence / stability vs window 1
    const auto doc2{engine.close_window(t2)};

    const std::string combined{meta::to_json(doc1) + "\n" + meta::to_json(doc2)};
    const std::string digest{picosha2::hash256_hex_string(combined)};

    // Re-derived 2026-06-04 (was 798463…, frozen 2026-05-31). The window-2 GET
    // template is deliberately ~50% errors → a level-count TIE (150 INFO / 150
    // ERROR). The old value encoded a non-deterministic tie result: dominant_level_of
    // broke the tie by unordered_map iteration order, so the template resolved to
    // INFO under libstdc++ but ERROR under libc++ — both a cross-stdlib determinism
    // break AND a latent detection defect (a half-error endpoint hidden as INFO).
    // dominant_level_of now breaks count ties by higher severity (ERROR here), a
    // pure function of the contents. The same value must hold on every
    // compiler/architecture (re-verify across the gcc×clang×-O×-ffp-contract matrix).
    constexpr std::string_view kGolden{
        "5782e79f6096cff581ab6c05ee9b08a3de4382702121db85578d68028816e279"};
    EXPECT_EQ(digest, kGolden)
        << "MetaLog document determinism golden mismatch — a cross-machine bit-identity "
           "regression, OR an intentional contract change needing the golden re-derived "
           "(and re-verified across the cross-arch CI matrix).\nDOC:\n"
        << combined;
}

} // namespace

// NOLINTEND
