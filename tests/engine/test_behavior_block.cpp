// NOLINTBEGIN
// Unit tests: allow short identifiers and test-specific patterns
// BehaviorBlock: n-gram emission, conditional probabilities, bounded keys (MetaLogEngine
// close_window).

#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;
using insight::metalog::test::make_event;

class BehaviorBlockTest : public ::testing::Test
{
  protected:
    insight::Timestamp start_ = std::chrono::system_clock::now();
};

TEST_F(BehaviorBlockTest, EmitsBigramsByDefault)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{
        .top_k_size = 8,
        .top_ngrams_size = 8,
    }};
    engine.open_window(start_);

    // Sequence A,B,A,B,A => 4 bigrams across 2 distinct keys.
    auto a{make_event("alpha <*>")};
    auto b{make_event("beta <*>")};
    engine.ingest_event(a);
    engine.ingest_event(b);
    engine.ingest_event(a);
    engine.ingest_event(b);
    engine.ingest_event(a);

    auto doc{engine.close_window(start_ + std::chrono::seconds(60))};

    ASSERT_TRUE(doc.behavior.has_value());
    EXPECT_EQ(doc.behavior->ngram_size, 2U);
    EXPECT_EQ(doc.behavior->top_ngrams.size(), 2U); // A->B, B->A
    EXPECT_EQ(doc.behavior->top_ngrams.front().sequence.size(), 2U);
}

TEST_F(BehaviorBlockTest, ProbabilityIsConditionalOnPrefix)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{
        .top_k_size = 8,
        .top_ngrams_size = 8,
    }};
    engine.open_window(start_);
    auto a{make_event("alpha")};
    auto b{make_event("beta")};
    auto c{make_event("gamma")};
    // A,B,A,C => bigrams A->B (1), B->A (1), A->C (1). p(B|A) = 0.5,
    // p(C|A) = 0.5, p(A|B) = 1.0.
    engine.ingest_event(a);
    engine.ingest_event(b);
    engine.ingest_event(a);
    engine.ingest_event(c);
    auto doc{engine.close_window(start_ + std::chrono::seconds(1))};

    ASSERT_TRUE(doc.behavior.has_value());
    const auto a_id{insight::template_id_of("alpha")};
    const auto b_id{insight::template_id_of("beta")};
    bool found_b_to_a = false;
    bool found_a_to_b = false;
    for (const auto& ng : doc.behavior->top_ngrams)
    {
        if (ng.sequence.size() != 2)
            continue;
        if (ng.sequence[0] == b_id && ng.sequence[1] == a_id)
        {
            EXPECT_NEAR(ng.probability, 1.0, 1e-9);
            found_b_to_a = true;
        }
        if (ng.sequence[0] == a_id && ng.sequence[1] == b_id)
        {
            EXPECT_NEAR(ng.probability, 0.5, 1e-9);
            found_a_to_b = true;
        }
    }
    EXPECT_TRUE(found_b_to_a);
    EXPECT_TRUE(found_a_to_b);
}

TEST_F(BehaviorBlockTest, TrigramOrderRespected)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{
        .top_k_size = 8,
        .ngram_size = 3,
        .top_ngrams_size = 8,
    }};
    engine.open_window(start_);
    engine.ingest_event(make_event("a"));
    engine.ingest_event(make_event("b"));
    engine.ingest_event(make_event("c"));
    engine.ingest_event(make_event("d"));
    auto doc{engine.close_window(start_ + std::chrono::seconds(1))};
    ASSERT_TRUE(doc.behavior.has_value());
    EXPECT_EQ(doc.behavior->ngram_size, 3U);
    ASSERT_FALSE(doc.behavior->top_ngrams.empty());
    EXPECT_EQ(doc.behavior->top_ngrams.front().sequence.size(), 3U);
}

TEST_F(BehaviorBlockTest, TopNgramsSizeZeroOmitsBlock)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{
        .top_k_size = 8,
        .top_ngrams_size = 0,
    }};
    engine.open_window(start_);
    engine.ingest_event(make_event("x"));
    engine.ingest_event(make_event("y"));
    auto doc{engine.close_window(start_ + std::chrono::seconds(1))};
    EXPECT_FALSE(doc.behavior.has_value());
}

TEST_F(BehaviorBlockTest, DiffDoesNotInventBranchingDeltaWhenComposedBaselineDropsBranching)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{
        .top_k_size = 8,
        .top_ngrams_size = 8,
        .top_branching_size = 8,
    }};

    engine.open_window(start_);
    const auto a{make_event("alpha")};
    const auto b{make_event("beta")};
    const auto c{make_event("gamma")};
    engine.ingest_event(a);
    engine.ingest_event(b);
    engine.ingest_event(a);
    engine.ingest_event(c);
    const auto doc{engine.close_window(start_ + std::chrono::seconds(1))};

    ASSERT_TRUE(doc.behavior.has_value());
    ASSERT_TRUE(doc.behavior->branching.has_value());
    ASSERT_FALSE(doc.behavior->branching->empty());

    const auto composed{meta::compose(doc, doc)};
    ASSERT_TRUE(composed.behavior.has_value());
    // compose drops branching (cannot recompute from aggregated counts), so
    // it is absent — not present-but-empty.
    EXPECT_FALSE(composed.behavior->branching.has_value());

    const auto diff{meta::diff(composed, doc)};
    EXPECT_TRUE(diff.branching_delta.empty())
        << "A composed baseline has no comparable branching entropy rows; missing rows must not "
           "be treated as zero entropy.";
}

TEST_F(BehaviorBlockTest, BoundedNgramKeysCapDistinctEntries)
{
    meta::MetaLogEngine engine{meta::MetaLogConfig{
        .top_k_size = 16,
        .top_ngrams_size = 16,
        .max_ngram_keys = 4, // tiny cap
    }};
    engine.open_window(start_);
    for (int i = 0; i < 20; ++i)
        engine.ingest_event(make_event(std::string{"t"} + std::to_string(i)));
    auto doc{engine.close_window(start_ + std::chrono::seconds(1))};
    ASSERT_TRUE(doc.behavior.has_value());
    EXPECT_LE(doc.behavior->top_ngrams.size(), 4U);

    // The cap's loss is VISIBLE, not silent. 20 distinct templates form 19 bigrams (N-1, one per
    // event once the ring holds a predecessor); the first 4 fill the table and every later one is
    // refused → 15 refused observations.
    //
    // This counts OBSERVATIONS, never distinct keys. The refusal in account_ngram happens BEFORE
    // insertion, so a refused key is refused again on each later occurrence; distinct-key loss is
    // not knowable without retaining exactly the unbounded set the cap exists to refuse. There is
    // no correct distinct-key value to assert here, which is why none is asserted.
    //
    // Read after close_window returns: the counter is snapshotted into the last_window_ member
    // ahead of reset_window_state(), so the live counter is already zero by now.
    EXPECT_EQ(engine.last_window_ngram_observations_dropped(), std::uint64_t{15})
        << "n-gram drop counter wrong at max_ngram_keys=4: 20 events -> 19 bigrams, 4 admitted, "
           "so 15 observations must be refused and counted";

    // …and the count reaches the DOCUMENT, which is the only surface a consumer reads (SPEC §4
    // `dropped_ngram_observations`). The engine-side accessor above is observability; this is the
    // claim on the wire. Same hand-computed oracle, deliberately: a second implementation of the
    // arithmetic would prove the two agreed, not that either was right.
    ASSERT_TRUE(doc.behavior->dropped_ngram_observations.has_value())
        << "the cap bound this window (15 refused observations) and §4 makes an ABSENT key mean "
           "zero in a 0.7.0+ document — omitting it here ships a false statement";
    EXPECT_EQ(*doc.behavior->dropped_ngram_observations, std::uint64_t{15})
        << "document field must carry the same 19-4=15 the engine counter does";

    // Per-window, like the table it guards. reset_window_state() clears ngram_counts_ AND the
    // global ring, so this second window starts with an empty table: 3 events form 2 bigrams,
    // both admitted under the cap of 4, and nothing is refused.
    //
    // Reading the SECOND close is what makes this an arm rather than a restatement — the snapshot
    // is taken in close_window ahead of reset_window_state(), so a counter that failed to reset
    // would surface here as the first window's 15 leaking into a clean window.
    engine.open_window(start_ + std::chrono::seconds(2));
    for (int i = 0; i < 3; ++i)
        engine.ingest_event(make_event(std::string{"clean"} + std::to_string(i)));
    auto clean_doc{engine.close_window(start_ + std::chrono::seconds(3))};
    ASSERT_TRUE(clean_doc.behavior.has_value());
    EXPECT_EQ(engine.last_window_ngram_observations_dropped(), std::uint64_t{0})
        << "the drop counter is per-window: a clean second window must report 0, not the previous "
           "window's count";

    // The OMISSION arm, and what makes it an arm rather than an incidental absence: this document
    // carries a populated behavior block (asserted above and below), so the only thing that can
    // explain the missing key is the zero itself. A block-absent document would satisfy a naive
    // "key not present" check while testing nothing.
    ASSERT_FALSE(clean_doc.behavior->top_ngrams.empty())
        << "fixture must produce a POPULATED behavior block, or the absence below proves nothing";
    EXPECT_FALSE(clean_doc.behavior->dropped_ngram_observations.has_value())
        << "SPEC §4: OMITTED when zero — a clean window must say nothing, not say 0. It reported "
        << *clean_doc.behavior->dropped_ngram_observations;
}

// ── SPEC §12.1: the composed document's drop count is the SUM, absent counting as zero ────────
//
// The three arms — absent+absent, absent+N, M+N — are the whole algebra of the clause, and the
// commutativity check that closes the test is what the sum gives for free where the two caps
// beside it do not (DN-56.D6). Every expectation is the arithmetic written out, never a second
// call into the code under test. Windows are produced by the real engine so the numbers are the
// producer's own: with `max_ngram_keys = K` and `E` distinct templates in one window, the events
// form `E-1` bigrams, `K` are admitted and `E-1-K` refused.
TEST_F(BehaviorBlockTest, ComposeSumsDroppedNgramObservationsAndOmitsAZeroSum)
{
    constexpr std::size_t kTinyCap{4};
    // E distinct templates in one window under kTinyCap -> (E-1-kTinyCap) refused observations.
    const auto window_with_distinct_templates{
        [&](const std::string& prefix, int distinct)
        {
            meta::MetaLogEngine engine{meta::MetaLogConfig{
                .top_k_size = 16,
                .top_ngrams_size = 16,
                .max_ngram_keys = kTinyCap,
            }};
            engine.open_window(start_);
            for (int i = 0; i < distinct; ++i)
                engine.ingest_event(make_event(prefix + std::to_string(i)));
            return engine.close_window(start_ + std::chrono::seconds(1));
        }};

    // 20 distinct -> 19 bigrams, 4 admitted, 15 refused.
    const auto dropped_15{window_with_distinct_templates("t", 20)};
    // 10 distinct -> 9 bigrams, 4 admitted, 5 refused.
    const auto dropped_5{window_with_distinct_templates("u", 10)};
    // 4 distinct -> 3 bigrams, all 3 admitted under the cap of 4, nothing refused.
    const auto dropped_0{window_with_distinct_templates("v", 4)};

    ASSERT_TRUE(dropped_15.behavior.has_value());
    ASSERT_TRUE(dropped_5.behavior.has_value());
    ASSERT_TRUE(dropped_0.behavior.has_value());
    ASSERT_EQ(dropped_15.behavior->dropped_ngram_observations, std::uint64_t{15});
    ASSERT_EQ(dropped_5.behavior->dropped_ngram_observations, std::uint64_t{5});
    ASSERT_FALSE(dropped_0.behavior->dropped_ngram_observations.has_value())
        << "the zero-drop input must be genuinely absent, or the absent+N arm is an N+N arm";

    // absent + absent -> omitted. compose() carries lhs.metalog_version (0.9.0), where §4 reads an
    // absent key as an affirmative "nothing was dropped" — which is true here and must stay silent.
    const auto zero_sum{meta::compose(dropped_0, dropped_0)};
    ASSERT_TRUE(zero_sum.behavior.has_value());
    EXPECT_FALSE(zero_sum.behavior->dropped_ngram_observations.has_value())
        << "0 + 0 is omitted, never written as 0 (SPEC §12.1 + §4). It reported "
        << *zero_sum.behavior->dropped_ngram_observations;

    // absent + N -> N. The absent side counts as zero; it must not make the whole sum absent.
    const auto absent_plus_n{meta::compose(dropped_0, dropped_15)};
    ASSERT_TRUE(absent_plus_n.behavior.has_value());
    ASSERT_TRUE(absent_plus_n.behavior->dropped_ngram_observations.has_value())
        << "0 + 15 = 15, and a composed document that stays silent about it declares zero drops "
           "for a pair that dropped 15";
    EXPECT_EQ(*absent_plus_n.behavior->dropped_ngram_observations, std::uint64_t{15})
        << "absent counts as 0, so the sum is the present side's 15";

    // M + N -> M+N. Both sides present: the only arm a carry-one-side implementation passes is
    // the one where the sides are equal, so the two counts are deliberately different.
    const auto m_plus_n{meta::compose(dropped_15, dropped_5)};
    ASSERT_TRUE(m_plus_n.behavior.has_value());
    ASSERT_TRUE(m_plus_n.behavior->dropped_ngram_observations.has_value());
    EXPECT_EQ(*m_plus_n.behavior->dropped_ngram_observations, std::uint64_t{20})
        << "15 + 5 = 20 — neither input's value, so carrying one side reds here";

    // Commutative, which the sum is by construction and the two sibling caps are not (DN-56.D6).
    const auto n_plus_m{meta::compose(dropped_5, dropped_15)};
    ASSERT_TRUE(n_plus_m.behavior.has_value());
    EXPECT_EQ(n_plus_m.behavior->dropped_ngram_observations,
              m_plus_n.behavior->dropped_ngram_observations)
        << "compose(A,B) and compose(B,A) must agree on the drop count";
}

// ── O2 trace-scoped graph: the de-pollution proof ─────────────────────────
// The measure-first gate. Two concurrent transactions, fully interleaved in the GLOBAL order:
//   trace A: "alpha step1" -> "alpha step2" -> "alpha step3"
//   trace B: "beta step1"  -> "beta step2"  -> "beta step3"
// Emitted round-robin (a1,b1,a2,b2,a3,b3) across several instances, so EVERY globally-adjacent
// pair crosses traces. The global-order n-gram graph therefore sees ONLY cross-trace NOISE edges
// and not a single real within-trace transition — exactly the structural_surprise pollution O2
// exists to kill. With OTEL trace context, O2's per-trace ring forms each n-gram WITHIN its
// transaction, recovering the real transitions and emitting ZERO noise. The same events, the only
// difference being whether the trace_id is present, must produce a strictly cleaner graph — else
// trace-scoping is not earning its cost.
TEST_F(BehaviorBlockTest, OtelTraceScopingDepollutesConcurrentInterleave)
{
    constexpr int kInstances{4};
    const auto a1{make_event("alpha step1")};
    const auto a2{make_event("alpha step2")};
    const auto a3{make_event("alpha step3")};
    const auto b1{make_event("beta step1")};
    const auto b2{make_event("beta step2")};
    const auto b3{make_event("beta step3")};

    const auto build = [&](bool with_trace, bool trace_scoping_enabled)
    {
        meta::MetaLogEngine engine{
            meta::MetaLogConfig{.top_k_size = 16,
                                .top_ngrams_size = 32,
                                .trace_scoping_enabled = trace_scoping_enabled}};
        engine.open_window(start_);
        for (int instance = 0; instance < kInstances; ++instance)
        {
            const std::uint64_t trace_a{1000U + static_cast<std::uint64_t>(instance)};
            const std::uint64_t trace_b{2000U + static_cast<std::uint64_t>(instance)};
            const auto emit = [&](tok::CanonicalEvent event, std::uint64_t trace_value)
            {
                if (with_trace)
                {
                    event.trace.present = true;
                    event.trace.trace_id = insight::TraceId{trace_value};
                }
                engine.ingest_event(event);
            };
            // Round-robin interleave: every adjacent global pair crosses traces.
            emit(a1, trace_a);
            emit(b1, trace_b);
            emit(a2, trace_a);
            emit(b2, trace_b);
            emit(a3, trace_a);
            emit(b3, trace_b);
        }
        return engine.close_window(start_ + std::chrono::seconds(60));
    };

    const auto scoped{build(true, true)};
    const auto global{build(false, true)};
    // The CONTROL ARM: trace context IS present, but trace_scoping_enabled=false → the engine
    // falls back to the global ring, so the SAME OTEL input reproduces the polluted graph. This
    // is the config flag that drives the scenario's trace_scoping_disabled_control arm.
    const auto control{build(true, false)};
    ASSERT_TRUE(scoped.behavior.has_value());
    ASSERT_TRUE(global.behavior.has_value());

    // The 4 real within-trace transitions (TemplateId pairs); any other bigram is cross-trace
    // noise.
    const std::array<std::pair<insight::TemplateId, insight::TemplateId>, 4> real_edges{{
        {insight::template_id_of("alpha step1"), insight::template_id_of("alpha step2")},
        {insight::template_id_of("alpha step2"), insight::template_id_of("alpha step3")},
        {insight::template_id_of("beta step1"), insight::template_id_of("beta step2")},
        {insight::template_id_of("beta step2"), insight::template_id_of("beta step3")},
    }};
    const auto count_edges = [&](const meta::MetaLogDocument& doc)
    {
        int real{0};
        int noise{0};
        for (const auto& ngram : doc.behavior->top_ngrams)
        {
            if (ngram.sequence.size() != 2)
                continue;
            bool is_real{false};
            for (const auto& edge : real_edges)
                if (ngram.sequence[0] == edge.first && ngram.sequence[1] == edge.second)
                {
                    is_real = true;
                    break;
                }
            (is_real ? real : noise)++;
        }
        return std::pair{real, noise};
    };

    const auto [scoped_real, scoped_noise]{count_edges(scoped)};
    const auto [global_real, global_noise]{count_edges(global)};
    const auto [control_real, control_noise]{count_edges(control)};

    // The de-pollution number, pinned exactly (deterministic fixture). Trace-scoped recovers ALL
    // 4 real transitions and emits ZERO cross-trace noise; the global-order graph, under this
    // concurrency, sees NO real transition and only its 6 cross-trace/boundary noise edges.
    EXPECT_EQ(scoped_real, 4) << "trace-scoping must recover every within-trace transition";
    EXPECT_EQ(scoped_noise, 0) << "trace-scoping must emit no cross-trace edge";
    EXPECT_EQ(global_real, 0) << "the global-order graph cannot see a within-trace transition here";
    EXPECT_EQ(global_noise, 6) << "the global-order graph is pure cross-trace noise";
    // The gate: trace-scoping is strictly cleaner (it beats the global-order baseline).
    EXPECT_LT(scoped_noise, global_noise)
        << "O2 trace-scoping must reduce structural noise vs the global-order graph";
    // The control arm (trace_scoping_enabled=false on OTEL input) reproduces the global-order
    // graph EXACTLY — proof the flag is a true A/B on the same input (the scenario control arm).
    EXPECT_EQ(control_real, global_real) << "scoping-disabled control must match the global graph";
    EXPECT_EQ(control_noise, global_noise)
        << "scoping-disabled control must match the global graph";
}

} // namespace

// NOLINTEND
