// invariant: a program may replace the global allocation functions exactly ONCE, so the
// replacements live in heap_probe.cpp and only the two thread_local gates are declared here.
// invariant: the override allocates nothing -- one relaxed thread_local load when disarmed, a load
// plus an increment when armed, identically on every arm.
// note: so an arm's allocation figure is read by SUBTRACTION against a control arm.
#pragma once

#include <cstdint>

namespace insight::metalog::bench
{

extern thread_local bool g_count_allocs;
extern thread_local std::uint64_t g_alloc_count;

// post: the count is zeroed on construction and disarmed on destruction, but never reset, so
// reading count() after the scope ends is still correct.
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
