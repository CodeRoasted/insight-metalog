// invariant: the ONE definition of the replaced global allocation functions.
#include "heap_probe.hpp"

#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>

namespace insight::metalog::bench
{
thread_local bool g_count_allocs{false};
thread_local std::uint64_t g_alloc_count{0};
} // namespace insight::metalog::bench

// invariant: the set is COMPLETE, all eight allocating forms and all twelve deallocating ones,
// because a sanitizer runtime defines every form itself and none of its forms forwards to ours.
// invariant: so every array, nothrow and aligned allocation is counted on every leg, and every
// pair the binary can form is malloc/free, never a runtime `new` freed here.
namespace
{

// post: at least `size` bytes aligned to `alignment`, or nullptr; an armed scope counts it.
// invariant: every allocating form below passes through here, so no form escapes the counter.
[[nodiscard]] void* counted_allocation(std::size_t size, std::size_t alignment) noexcept
{
    if (insight::metalog::bench::g_count_allocs)
        ++insight::metalog::bench::g_alloc_count;
    const std::size_t bytes{size != 0 ? size : 1};
    if (alignment <= alignof(std::max_align_t))
        return std::malloc(bytes);
    if (bytes > std::numeric_limits<std::size_t>::max() - alignment)
        return nullptr;
    const std::size_t rounded{(bytes + alignment - 1) / alignment * alignment};
    return std::aligned_alloc(alignment, rounded);
}

[[nodiscard]] void* throwing_allocation(std::size_t size, std::size_t alignment)
{
    void* block{counted_allocation(size, alignment)};
    if (block == nullptr)
        throw std::bad_alloc{};
    return block;
}

} // namespace

void* operator new(std::size_t size)
{
    return throwing_allocation(size, alignof(std::max_align_t));
}

void* operator new[](std::size_t size)
{
    return throwing_allocation(size, alignof(std::max_align_t));
}

void* operator new(std::size_t size, const std::nothrow_t& /*tag*/) noexcept
{
    return counted_allocation(size, alignof(std::max_align_t));
}

void* operator new[](std::size_t size, const std::nothrow_t& /*tag*/) noexcept
{
    return counted_allocation(size, alignof(std::max_align_t));
}

void* operator new(std::size_t size, std::align_val_t alignment)
{
    return throwing_allocation(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return throwing_allocation(size, static_cast<std::size_t>(alignment));
}

void* operator new(std::size_t size, std::align_val_t alignment,
                   const std::nothrow_t& /*tag*/) noexcept
{
    return counted_allocation(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment,
                     const std::nothrow_t& /*tag*/) noexcept
{
    return counted_allocation(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* ptr) noexcept
{
    std::free(ptr);
}

void operator delete[](void* ptr) noexcept
{
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t /*size*/) noexcept
{
    std::free(ptr);
}

void operator delete[](void* ptr, std::size_t /*size*/) noexcept
{
    std::free(ptr);
}

void operator delete(void* ptr, const std::nothrow_t& /*tag*/) noexcept
{
    std::free(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t& /*tag*/) noexcept
{
    std::free(ptr);
}

void operator delete(void* ptr, std::align_val_t /*alignment*/) noexcept
{
    std::free(ptr);
}

void operator delete[](void* ptr, std::align_val_t /*alignment*/) noexcept
{
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t /*size*/, std::align_val_t /*alignment*/) noexcept
{
    std::free(ptr);
}

void operator delete[](void* ptr, std::size_t /*size*/, std::align_val_t /*alignment*/) noexcept
{
    std::free(ptr);
}

void operator delete(void* ptr, std::align_val_t /*alignment*/,
                     const std::nothrow_t& /*tag*/) noexcept
{
    std::free(ptr);
}

void operator delete[](void* ptr, std::align_val_t /*alignment*/,
                       const std::nothrow_t& /*tag*/) noexcept
{
    std::free(ptr);
}
