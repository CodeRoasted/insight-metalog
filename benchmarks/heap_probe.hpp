// heap_probe.hpp — the counting passthrough on the global heap that the key-allocation benches
// share (bench_cube_key_alloc, bench_ordinal_key_alloc). A program may replace the global
// allocation functions exactly ONCE, so the replaced operator new/delete live in heap_probe.cpp
// and only the two thread_local gates are declared here; a bench TU arms the count around the
// loop it measures and reads it back.
//
// The override allocates nothing and costs one relaxed thread_local load when disarmed, a load
// plus an increment when armed — the same on every arm of a bench, which is why attribution is
// always by SUBTRACTION against a control arm and never by an absolute.
#pragma once

#include <cstdint>

namespace insight::metalog::bench
{

extern thread_local bool g_count_allocs;
extern thread_local std::uint64_t g_alloc_count;

// Arms the count for the enclosing scope: zeroes it on construction, disarms on destruction.
// Read `count()` BEFORE the scope ends — the count is not reset on destruction, so a read after
// is still correct, but the intent reads better beside the loop it measures.
class AllocCountScope
{
  public:
    AllocCountScope() noexcept
    {
        g_alloc_count = 0;
        g_count_allocs = true;
    }
    ~AllocCountScope()
    {
        g_count_allocs = false;
    }
    AllocCountScope(const AllocCountScope&) = delete;
    AllocCountScope& operator=(const AllocCountScope&) = delete;
    AllocCountScope(AllocCountScope&&) = delete;
    AllocCountScope& operator=(AllocCountScope&&) = delete;

    [[nodiscard]] static std::uint64_t count() noexcept
    {
        return g_alloc_count;
    }
};

} // namespace insight::metalog::bench
