// NOLINTBEGIN(cppcoreguidelines-owning-memory) — the global new/delete override IS the instrument.
//
// heap_probe.cpp — the ONE definition of the replaced global allocation functions (see
// heap_probe.hpp). Only the non-array forms are replaced: both libstdc++ and libc++ implement
// the default operator new[] by calling operator new, so array allocations are counted through
// the forwarding; the aligned forms are not replaced and not counted, which no key-allocation
// arm exercises (a std::string's buffer is unaligned-new).

#include "heap_probe.hpp"

#include <cstddef>
#include <cstdlib>
#include <new>

namespace insight::metalog::bench
{
thread_local bool g_count_allocs{false};
thread_local std::uint64_t g_alloc_count{0};
} // namespace insight::metalog::bench

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

// NOLINTEND(cppcoreguidelines-owning-memory)
