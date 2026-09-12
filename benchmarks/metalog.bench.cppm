// invariant: the whole import surface a benchmark TU needs -- the facade, the sealed detail module
// and canon -- so a TU adds only google-benchmark, which stays textual.
// refs: ADR-3.D4
export module insight.metalog.bench;
export import std;
export import insight.metalog;
export import insight.metalog.detail.stats;
export import insight.metalog.detail.operations;
export import insight.metalog.detail.cube;
export import insight.canon;

export namespace insight::metalog::bench
{

// invariant: the ONE draw every benchmark corpus uses — fully specified integer arithmetic, so a
// corpus is the same bytes under libstdc++ and libc++, which a std distribution is not.
struct SplitMix64
{
    std::uint64_t state;
    [[nodiscard]] std::uint64_t next() noexcept
    {
        state += 0x9E3779B97F4A7C15ULL;
        std::uint64_t mixed{state};
        mixed = (mixed ^ (mixed >> 30)) * 0xBF58476D1CE4E5B9ULL;
        mixed = (mixed ^ (mixed >> 27)) * 0x94D049BB133111EBULL;
        return mixed ^ (mixed >> 31);
    }
    // post: an index in [0, n) whose density falls as it rises — the square of a uniform 32-bit
    // fraction, in integer arithmetic only.
    [[nodiscard]] std::size_t skewed(std::size_t n) noexcept
    {
        const std::uint64_t fraction{next() >> 32};
        const std::uint64_t squared{(fraction * fraction) >> 32};
        return static_cast<std::size_t>((squared * n) >> 32);
    }
    // post: an index in [0, n), uniform up to a modulo bias below n / 2^64.
    [[nodiscard]] std::size_t below(std::size_t n) noexcept
    {
        return static_cast<std::size_t>(next() % n);
    }
};

} // namespace insight::metalog::bench
