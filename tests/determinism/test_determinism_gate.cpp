// NOLINTBEGIN
// Unit tests: allow short identifiers and test-specific patterns
// Standing gate: full-document cross-machine bit-identity golden (SHA-256 frozen).

#include <gtest/gtest.h>
#include <picosha2.h>

import insight.metalog.test;

// AFTER the imports (plain TU, ordinary textual include): the shared F5-M8 near-full reservoir
// scenario, shared with scripts/determinism_fixture.cpp so both oracles run the identical window.
#include "reservoir_nearfull_scenario.hpp"

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

    const std::string combined{meta::to_json(doc1, engine.registry()) + "\n" + meta::to_json(doc2, engine.registry())};
    const std::string digest{picosha2::hash256_hex_string(combined)};

    // Re-derived 2026-06-15 (was 5782e7…) for the metalog 0.6.0 format-version bump
    // (metalog_version / producer.version "0.6.0" → "0.6.0", the SPEC §16 cube landing).
    // This scenario does NOT enable the cube (emit_cube defaults false), so the ONLY
    // wire change is the two version strings — the cube's own bytes are golden-gated
    // separately by CubeDeterminism.ByteIdentityGolden. The window-2 GET template is
    // ~50% errors → a level-count TIE (150 INFO / 150 ERROR); dominant_level_of breaks
    // it by higher severity (ERROR), a pure function of the contents. The same value
    // MUST hold on every compiler/architecture (re-verify across the cross-stdlib diagonal).
    constexpr std::string_view kGolden{
        "2bd6f92139524391c55c76ed12ca545bcae4c8a6d955b5cdecc44309d5773bc4"};
    EXPECT_EQ(digest, kGolden)
        << "MetaLog document determinism golden mismatch — a cross-machine bit-identity "
           "regression, OR an intentional contract change needing the golden re-derived "
           "(and re-verified across the cross-arch CI matrix).\nDOC:\n"
        << combined;
}

// ── Near-full reservoir determinism golden (F5-M8) ────────────────────────────
// The oracle that closes the F5-M8 coverage gap. FullDocumentByteIdentityGolden above
// never drove the item-reservoir (§2.11) to its admit/evict boundary — its fixture has a
// handful of templates, so the reservoir stays empty and the salience pipeline that decides
// bag membership is never exercised. F5-M8: the reservoir salience inputs (structural_surprise
// in particular) were not bit-identical clang≢gcc, and an order-dependent surprise pick flipped a
// near-tie admit/evict at the M=128 boundary → a different bag → a Tier-1 violation in the
// production Sift batch-diff (eidos ships reservoir_size=128). "golden green" did NOT certify the
// reservoir until this fixture exists. (insight_determinism_model.md §F5-M8.)
//
// Design: a window whose item-reservoir fills to the FULL production M=128 with the admit/evict
// boundary contested by structural_surprise — the non-deterministic input. per_kind_cap=0 (Founder
// ruling 2026-06-14): the cap keys on (StructuralRole×LogLevel)=4×7=28 kinds, so cap=4 would
// hard-ceiling the reservoir at 112; cap=0 admits the top-M by pure salience rank, the clean oracle
// for the salience VALUE (the F5-M8 root, upstream of the cap). Salient-by-structure benign spokes
// (reached only via a rare off-path edge from a busy hub) make structural_surprise — not a fixed
// level — decide membership, including AMBIGUOUS spokes reached by two EQUAL-RATIO edges (count 1
// vs 2): the exact most-likely-edge tie F5-M8's order-dependent pick resolves differently per
// stdlib.
//
// GREEN-FROZEN 2026-06-16 (the SHA below). The freeze gate was strict — never freeze a
// non-deterministic value — and is now satisfied: Heph's F5-M8 fix (the exact most-likely-edge
// tie-break in build_transition_graph) made this document bit-identical across the cross-stdlib
// diagonal (the only axis that exposes the iteration-order flip) AND the orthogonal -O{0,3} ×
// -ffp-contract{off,fast} corners. The standing repro is scripts/determinism_bitidentity.sh (the
// same fixture replayed across that matrix); the coverage assertion below (reservoir == M,
// structural-surprise boundary) is ALWAYS live so the oracle can never silently stop exercising the
// regime. RELEASE-BLOCKING: a mismatch here blocks any cut that ships the eidos M=128 batch-diff.
TEST(DeterminismGate, ReservoirNearFullByteIdentityGolden)
{
    // The scenario lives in scripts/reservoir_nearfull_scenario.hpp so this in-suite golden and the
    // cross-compiler matrix fixture (scripts/determinism_fixture.cpp, via
    // determinism_bitidentity.sh) exercise the EXACT same M=128 admit/evict boundary — they cannot
    // drift.
    meta::MetaLogConfig cfg;
    meta::nearfull::configure(cfg);
    meta::MetaLogEngine engine{cfg};

    using Clock = std::chrono::system_clock;
    const Clock::time_point t0{std::chrono::seconds{1700000000}};
    const Clock::time_point t1{std::chrono::seconds{1700000060}};
    engine.open_window(t0);
    meta::nearfull::emit_window(engine);
    const auto doc{engine.close_window(t1)};

    // ── Coverage invariant (ALWAYS live): the fixture MUST drive the reservoir to the full M and
    //    the admit/evict boundary MUST be structural-surprise-driven — else it stops exercising the
    //    F5-M8 regime and a green golden would be hollow. Verbose on failure (CLAUDE.md).
    std::size_t surprise_driven{0};
    for (const auto& entry : doc.stats.reservoir)
        if (entry.structural_surprise > 0 && entry.dominant_level != insight::LogLevel::Error &&
            entry.dominant_level != insight::LogLevel::Fatal)
            ++surprise_driven;
    ASSERT_EQ(doc.stats.reservoir.size(), cfg.reservoir_size)
        << "near-full reservoir fixture must fill the item-reservoir to the full production M="
        << cfg.reservoir_size << " (got " << doc.stats.reservoir.size()
        << ").\n  unique_templates=" << doc.stats.unique_templates
        << " top_k=" << doc.stats.top_k.size()
        << " surprise_driven_reservoir_entries=" << surprise_driven;
    EXPECT_GT(surprise_driven, 0U)
        << "the reservoir boundary must be structural_surprise-driven so the F5-M8 hazard "
           "(a non-deterministic surprise score) flips bag membership; none were.";

    const std::string doc_json{meta::to_json(doc, engine.registry())};
    const std::string digest{picosha2::hash256_hex_string(doc_json)};

    // FROZEN 2026-06-16 (Argos) — the F5-M8 freeze gate is satisfied. Heph's root fix landed (the
    // build_transition_graph most-likely-incoming-edge pick now breaks an EQUAL ratio by MORE
    // observations — a pure content function, not unordered_map iteration order; engine.cpp). The
    // near-full reservoir document is now byte-identical across the cross-stdlib diagonal (the ONLY
    // axis that exposes the F5-M8 iteration-order flip) AND the orthogonal -O{0,3}×-ffp{off,fast}
    // corners: scripts/determinism_bitidentity.sh with DETERMINISM_LEGS="gcc15-libstdcxx
    // clang21-libcxx" reports all 8 cells IDENTICAL on --reservoir-nearfull, and gcc-15.3/libstdc++
    // ≡ clang-21/libc++ produce this exact SHA in-suite. The coverage invariant above (reservoir == M,
    // structural-surprise-driven boundary) guarantees this golden keeps exercising the M=128 regime.
    // Re-derive ONLY for an intentional contract change — and it must then hold across the
    // cross-stdlib diagonal again (a bare mismatch = an item-reservoir admit/evict determinism
    // regression; insight_determinism_model.md §F5-M8).
    constexpr std::string_view kGolden{
        "853daec50d5b2cb656ce6ca711606391f1847b5541c244bf1740429aa2bc2d78"};
    EXPECT_EQ(digest, kGolden)
        << "near-full reservoir determinism golden mismatch (F5-M8) — the item-reservoir admit/evict "
           "boundary is non-deterministic across this build, OR an intentional contract change needs "
           "the golden re-derived (and re-verified across the cross-stdlib diagonal). RELEASE-BLOCKING "
           "for the eidos M=128 batch-diff.\nDOC:\n"
        << doc_json;
}

// ── emit_where-only document shape golden (the Sift WHERE carrier; D-WHERE-13) ─
// The Sift diff path runs emit_where=true, emit_cube=FALSE — the carrier added so a
// finding can name WHERE without paying for the cube (sift_where_attribution.md). That
// document shape — per-template `dominant_component` + the per-window `acquisition`
// block, with NO cube — was, before this gate, exercised only INDIRECTLY through the
// cube golden (emit_cube IMPLIES emit_where), never on its own. This is the dedicated
// byte-identity golden for the cube-independent shape (D-WHERE-13 cascade-owner: Argos).
// It pins (a) the emit_where-only wire (acquisition + component present, cube ABSENT) and
// (b) the dominant_component_of tie-break (ties → component string ascending — a pure
// function of contents, the determinism-sensitive path, the F5-M8 lesson applied). The
// new fields are integer / string only (no float, no float→int, no wall-clock), so the
// document MUST be bit-identical across the cross-stdlib diagonal (gcc-15/libstdc++ ≡
// clang-21/libc++) — the only axis that would expose an unordered_map iteration-order
// leak in records_with_component / distinct_components / the dominant pick.
TEST(DeterminismGate, EmitWhereOnlyDocumentByteIdentityGolden)
{
    // component is a string_view INTO the event; string literals have static storage, so
    // the views stay valid for the whole test (the cube-suite ev() pattern). Empty
    // component → a free-text line that carries no WHERE (not counted in coverage).
    const auto ev = [](std::string_view tmpl, insight::LogLevel level, std::string_view component)
    {
        tok::CanonicalEvent e;
        e.template_str = tmpl;
        e.level = level;
        e.component = component;
        return e;
    };

    meta::MetaLogConfig cfg;
    cfg.emit_where = true; // the Sift carrier — populate the cube-independent WHERE...
    ASSERT_FALSE(cfg.emit_cube) << "this golden pins the cube-ABSENT shape; emit_cube must stay off";
    meta::MetaLogEngine engine{cfg};

    using Clock = std::chrono::system_clock;
    const Clock::time_point t0{std::chrono::seconds{1700000000}};
    const Clock::time_point t1{std::chrono::seconds{1700000060}};
    const Clock::time_point t2{std::chrono::seconds{1700000120}};

    // Window 1: PARTIAL coverage (a free-text template carries no component) over four
    // distinct components — one of which is a per-template TIE (ping: zebra×2, alpha×2)
    // the ascending-string tie-break MUST resolve to "alpha" identically on every stdlib.
    engine.open_window(t0);
    for (int i = 0; i < 6; ++i) engine.ingest_event(ev("login ok", insight::LogLevel::Info, "auth"));
    for (int i = 0; i < 4; ++i) engine.ingest_event(ev("query slow", insight::LogLevel::Warn, "db"));
    for (int i = 0; i < 2; ++i) engine.ingest_event(ev("ping", insight::LogLevel::Info, "zebra"));
    for (int i = 0; i < 2; ++i) engine.ingest_event(ev("ping", insight::LogLevel::Info, "alpha"));
    for (int i = 0; i < 3; ++i) engine.ingest_event(ev("starting up", insight::LogLevel::Info, ""));
    const auto doc1{engine.close_window(t1)};

    // Window 2: a db ERROR burst (the regression) over steady auth traffic; FULL coverage.
    engine.open_window(t1);
    for (int i = 0; i < 6; ++i) engine.ingest_event(ev("login ok", insight::LogLevel::Info, "auth"));
    for (int i = 0; i < 5; ++i) engine.ingest_event(ev("pool timeout", insight::LogLevel::Error, "db"));
    const auto doc2{engine.close_window(t2)};

    // ── Coverage invariants (ALWAYS live): the document MUST carry the emit_where-only
    //    shape, or a green golden would be hollow. Verbose on failure (CLAUDE.md).
    ASSERT_FALSE(doc1.has_cube) << "emit_where must NOT build the cube — the Sift path is cube-free";
    ASSERT_FALSE(doc2.has_cube);
    ASSERT_TRUE(doc1.acquisition.has_value()) << "emit_where must emit the per-window acquisition block";
    ASSERT_TRUE(doc2.acquisition.has_value());
    EXPECT_EQ(doc1.acquisition->records_with_component, 14U)
        << "located events = 6 auth + 4 db + 2 zebra + 2 alpha; the 3 free-text lines carry none";
    EXPECT_EQ(doc1.acquisition->distinct_components, 4U) << "auth, db, alpha, zebra";
    EXPECT_EQ(doc2.acquisition->records_with_component, 11U) << "6 auth + 5 db (all located)";
    EXPECT_EQ(doc2.acquisition->distinct_components, 2U) << "auth, db";
    std::set<std::string> labels;
    for (const auto& entry : doc1.stats.top_k)
        if (entry.dominant_component) labels.insert(*entry.dominant_component);
    EXPECT_EQ(labels, (std::set<std::string>{"alpha", "auth", "db"}))
        << "ping ties zebra/alpha → ascending picks 'alpha' (never zebra); the free-text "
           "template carries no label (disengaged, never \"\")";

    const std::string combined{meta::to_json(doc1, engine.registry()) + "\n" + meta::to_json(doc2, engine.registry())};
    const std::string digest{picosha2::hash256_hex_string(combined)};

    // FROZEN 2026-06-23 (Argos) — the dedicated emit_where-only (cube-absent) Sift shape.
    // Verified bit-identical at freeze across gcc-15.3/libstdc++ ≡ clang-21/libc++ (the
    // cross-stdlib diagonal, the only axis that would expose an iteration-order leak in
    // the new aggregate facts). Re-derive ONLY for an intentional contract change — and it
    // must then hold across the cross-stdlib diagonal again (a bare mismatch = an
    // emit_where wire / dominant-component determinism regression; sift_where_attribution.md
    // D-WHERE-13).
    constexpr std::string_view kGolden{
        "a5e4e7862c540b3104ed37a9cdffacecd36c705499f6b234e2c320789ea0ead5"};
    EXPECT_EQ(digest, kGolden)
        << "emit_where-only document determinism golden mismatch — the cube-independent WHERE "
           "wire is non-deterministic across this build, OR an intentional contract change needs "
           "the golden re-derived (and re-verified across the cross-stdlib diagonal).\nactual digest: "
        << digest << "\nDOC:\n"
        << combined;
}

} // namespace

// NOLINTEND
