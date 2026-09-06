
// invariant: per-slot histograms are CARRIED through compose() and not dropped, so a per-slot
// distribution shift stays visible in a diff against a composed baseline.
// invariant: for a slot present in BOTH inputs the composer unions value_counts, sums the counts,
// truncates to the producer's cap by count, sums the totals and recomputes entropy over the merge.
// note: a slot present in only ONE input may be carried unchanged.
#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace tok = insight::tokenization;
namespace meta = insight::metalog;

namespace
{
    [[nodiscard]] meta::MetaLogDocument
    make_doc_with_histogram(std::string_view template_id, std::uint32_t param_index,
                            std::unordered_map<std::string, std::uint64_t> values,
                            std::uint64_t total, std::uint64_t approximate_cardinality,
                            std::uint64_t lines_observed)
    {
        meta::MetaLogDocument doc;
        doc.window.lines_observed = lines_observed;
        doc.stats.unique_templates = 1;
        doc.stats.top_k_size = 8;
        meta::TopKEntry entry;
        entry.template_id = insight::parse_template_id(template_id);
        entry.count = total;
        entry.frequency = lines_observed > 0
                              ? static_cast<double>(total) / static_cast<double>(lines_observed)
                              : 0.0;
        meta::FieldHistogram fh;
        fh.param_index = param_index;
        fh.value_counts = std::move(values);
        fh.total = total;
        fh.approximate_cardinality = approximate_cardinality;
        entry.field_histograms.push_back(std::move(fh));
        doc.stats.top_k.push_back(std::move(entry));
        return doc;
    }
} // namespace

TEST(ParamHistogramsCompose, MergesValueCountsAndTotalForSharedSlot)
{
    const auto lhs{make_doc_with_histogram("h:abc", 0, {{"/api/users", 800}, {"/health", 200}},
                                           /*total=*/1100, /*card=*/1847, /*lines=*/2000)};
    const auto rhs{make_doc_with_histogram("h:abc", 0, {{"/api/users", 100}, {"/admin", 50}},
                                           /*total=*/200, /*card=*/50, /*lines=*/500)};

    const auto composed{meta::compose(lhs, rhs)};
    ASSERT_EQ(composed.stats.top_k.size(), 1U);
    ASSERT_EQ(composed.stats.top_k[0].field_histograms.size(), 1U);
    const auto& fh{composed.stats.top_k[0].field_histograms[0]};
    EXPECT_EQ(fh.param_index, 0U);
    EXPECT_EQ(fh.value_counts.at("/api/users"), 900U) << "overlapping key sums counts";
    EXPECT_EQ(fh.value_counts.at("/health"), 200U) << "lhs-only key carried";
    EXPECT_EQ(fh.value_counts.at("/admin"), 50U) << "rhs-only key carried";
    EXPECT_EQ(fh.total, 1300U) << "composed total must be the exact sum lhs.total + rhs.total";
    EXPECT_GT(fh.entropy_bits, 0.0) << "entropy recomputed from merged counts";
}

// invariant: HLL sketches are not carried in the document, so the composer cannot union them and
// takes the max as the conservative lower-bound estimate it can defend.
TEST(ParamHistogramsCompose, ApproximateCardinalityIsMaxAcrossInputs)
{
    const auto lhs{make_doc_with_histogram("h:abc", 0, {{"x", 10}}, 10, /*card=*/1847, 100)};
    const auto rhs{make_doc_with_histogram("h:abc", 0, {{"x", 5}}, 5, /*card=*/50, 50)};
    const auto composed{meta::compose(lhs, rhs)};
    EXPECT_EQ(composed.stats.top_k[0].field_histograms[0].approximate_cardinality, 1847U);
}

// invariant: a one-sided slot is CARRIED rather than omitted, preserving more information, so the
// composed drift and shift can still see the asymmetric distribution.
TEST(ParamHistogramsCompose, CarriesOneSidedHistogramUnchanged)
{
    const auto lhs{make_doc_with_histogram("h:abc", 0, {{"x", 10}}, 10, /*card=*/100, 100)};
    meta::MetaLogDocument rhs;
    rhs.window.lines_observed = 100;
    rhs.stats.unique_templates = 1;
    rhs.stats.top_k_size = 8;
    meta::TopKEntry rhs_entry;
    rhs_entry.template_id = insight::parse_template_id("h:abc");
    rhs_entry.count = 50;
    rhs.stats.top_k.push_back(std::move(rhs_entry));

    const auto composed{meta::compose(lhs, rhs)};
    ASSERT_EQ(composed.stats.top_k[0].field_histograms.size(), 1U);
    const auto& fh{composed.stats.top_k[0].field_histograms[0]};
    EXPECT_EQ(fh.value_counts.at("x"), 10U) << "lhs-only slot carried unchanged";
    EXPECT_EQ(fh.total, 10U) << "total reflects only the input that had the histogram";
}

// invariant: the merged value_counts is truncated to the producer's cap, keeping the top-N by count
// with a deterministic key tie-break.
// note: that bound keeps a composed document's size independent of input cardinality.
TEST(ParamHistogramsCompose, TruncatesMergedValueCountsToCap)
{
    constexpr std::size_t kCap{meta::MetaLogConfig::kDefaultMaxHistogramValues};
    std::unordered_map<std::string, std::uint64_t> a;
    std::unordered_map<std::string, std::uint64_t> b;
    // note: fifty per side over disjoint keys gives a hundred, exceeding the cap.
    for (std::uint64_t i = 0; i < 50; ++i)
        a["a" + std::to_string(i)] = i + 1;
    for (std::uint64_t i = 0; i < 50; ++i)
        b["b" + std::to_string(i)] = 100 + i;
    const auto lhs{make_doc_with_histogram("h:abc", 0, std::move(a), 1275, 0, 5000)};
    const auto rhs{make_doc_with_histogram("h:abc", 0, std::move(b), 6225, 0, 5000)};

    const auto composed{meta::compose(lhs, rhs)};
    ASSERT_EQ(composed.stats.top_k[0].field_histograms.size(), 1U);
    EXPECT_EQ(composed.stats.top_k[0].field_histograms[0].value_counts.size(), kCap)
        << "merged value_counts MUST be truncated to the cap, keeping the top-N by count — "
           "an untruncated merge makes composed document size grow with input cardinality";
}

// invariant: when neither input emits histograms the composed entry stays empty, with no spurious
// empty histogram introduced.
TEST(ParamHistogramsCompose, NoHistogramsWhenInputsHaveNone)
{
    meta::MetaLogDocument lhs;
    lhs.window.lines_observed = 100;
    lhs.stats.top_k_size = 8;
    meta::TopKEntry lhs_e;
    lhs_e.template_id = insight::parse_template_id("h:abc");
    lhs_e.count = 50;
    lhs.stats.top_k.push_back(std::move(lhs_e));
    const meta::MetaLogDocument rhs{lhs};

    const auto composed{meta::compose(lhs, rhs)};
    ASSERT_EQ(composed.stats.top_k.size(), 1U);
    EXPECT_TRUE(composed.stats.top_k[0].field_histograms.empty());
}

} // namespace
