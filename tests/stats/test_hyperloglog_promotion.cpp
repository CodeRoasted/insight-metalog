// refs: DN-139.D2
// invariant: the sparse-until-dense sketch and a dense register array fed the same adds give the
// same estimate at EVERY prefix, on both sides of the promotion point and over the full array.
// invariant: determinism -- every stream is a fixed enumeration of keys, and both sides are
// integer.
#include <gtest/gtest.h>

import insight.metalog.test;

namespace
{

namespace meta = insight::metalog;
using Sketch = meta::HyperLogLog;

// invariant: one count at and past the inline store, below, at and above the promotion point, plus
// a single index and every index, so each change of representation is crossed.
constexpr std::array<std::size_t, 7> kDistinctIndexCounts{1,
                                                          Sketch::kInlineEntries,
                                                          Sketch::kInlineEntries + 1,
                                                          Sketch::kSparseMaxEntries - 1,
                                                          Sketch::kSparseMaxEntries,
                                                          Sketch::kSparseMaxEntries + 1,
                                                          Sketch::kNumRegisters};

// invariant: revisits of an already-reached index exercise the keep-the-maximum update; a quarter
// of the distinct count, and never fewer than kMinRevisits.
constexpr std::size_t kRevisitDivisor{4};
constexpr std::size_t kMinRevisits{8};
// invariant: far above the ~170 000 keys a full 16 384-index cover needs, so only a broken hash
// exhausts it.
constexpr std::uint64_t kMaxCandidates{std::uint64_t{1} << 24U};

struct Stream
{
    std::vector<std::string> keys;
    std::size_t reached{0};
};

// post: keys spelled k<i> in order, keeping each one that reaches a new register index until
// `distinct` are reached, and each one that revisits a reached index until the revisit quota.
[[nodiscard]] Stream stream_reaching(std::size_t distinct)
{
    const std::size_t revisit_quota{std::max(distinct / kRevisitDivisor, kMinRevisits)};
    std::vector<bool> reached(Sketch::kNumRegisters, false);
    std::size_t revisits{0};
    Stream stream;
    for (std::uint64_t candidate{0};
         candidate < kMaxCandidates && (stream.reached < distinct || revisits < revisit_quota);
         ++candidate)
    {
        std::string key{"k" + std::to_string(candidate)};
        const std::uint32_t index{Sketch::register_update(key).index};
        if (reached[index])
        {
            if (revisits < revisit_quota)
            {
                ++revisits;
                stream.keys.push_back(std::move(key));
            }
        }
        else if (stream.reached < distinct)
        {
            reached[index] = true;
            ++stream.reached;
            stream.keys.push_back(std::move(key));
        }
    }
    return stream;
}

} // namespace

TEST(HyperLogLogPromotion, SparseAndDenseEstimatesAgreeAtEveryPrefixAcrossThePromotionPoint)
{
    for (const std::size_t distinct : kDistinctIndexCounts)
    {
        const Stream stream{stream_reaching(distinct)};
        ASSERT_EQ(stream.reached, distinct)
            << "the first " << kMaxCandidates << " keys reach only " << stream.reached << " of "
            << distinct << " register indices";

        Sketch sketch;
        auto reference{std::make_unique<Sketch::Registers>()};
        std::size_t reached{0};
        for (std::size_t prefix{0}; prefix < stream.keys.size(); ++prefix)
        {
            sketch.add(stream.keys[prefix]);
            const Sketch::RegisterUpdate update{Sketch::register_update(stream.keys[prefix])};
            auto& reg{(*reference)[update.index]};
            if (reg == 0)
                ++reached;
            reg = std::max(reg, update.rank);

            const std::uint64_t expected{Sketch::estimate_registers(*reference)};
            const std::uint64_t actual{sketch.estimate()};
            ASSERT_EQ(actual, expected)
                << "stream of " << distinct << " indices, prefix " << prefix + 1 << " of "
                << stream.keys.size() << " (" << reached << " indices reached, sketch "
                << (sketch.dense() ? "dense" : "sparse") << ")";
            ASSERT_EQ(sketch.dense(), reached > Sketch::kSparseMaxEntries)
                << "stream of " << distinct << " indices, prefix " << prefix + 1 << ": " << reached
                << " indices reached against a promotion point of " << Sketch::kSparseMaxEntries
                << ", sketch " << (sketch.dense() ? "dense" : "sparse");
        }
    }
}
