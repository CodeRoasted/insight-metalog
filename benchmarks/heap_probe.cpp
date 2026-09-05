// invariant: the ONE definition of the replaced global allocation functions.
#include "heap_probe.hpp"

#include <cstddef>
#include <cstdlib>
#include <new>

namespace insight::metalog::bench
{
thread_local bool g_count_allocs{false};
thread_local std::uint64_t g_alloc_count{0};
} // namespace insight::metalog::bench

// invariant: only the non-array forms are replaced; both libstdc++ and libc++ implement the default
// operator new[] through operator new, so array allocations are counted too.
// invariant: the aligned forms are neither replaced nor counted, and no key-allocation arm reaches
// them -- a std::string's buffer comes from unaligned new.
void* operator new(std::size_t size)
{
    if (insight::metalog::bench::g_count_allocs)
        ++insight::metalog::bench::g_alloc_count;
    if (void* ptr{std::malloc(size)})
        return ptr;
    throw std::bad_alloc{};
}

void operator delete(void* ptr) noexcept
{
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept
{
    std::free(ptr);
}
