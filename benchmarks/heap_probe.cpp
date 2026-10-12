// invariant: the ONE definition of the replaced global allocation functions.
#include "heap_probe.hpp"

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
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

// invariant: an over-aligned block is carved from ONE malloc, the malloc's pointer kept in the
// word before it: MSVC has no std::aligned_alloc, and a replaced operator new cannot call one.
// post: at least `bytes` bytes aligned to `alignment` (a power of two), or nullptr.
[[nodiscard]] void* over_aligned_allocation(std::size_t bytes, std::size_t alignment) noexcept
{
    if (bytes > std::numeric_limits<std::size_t>::max() - alignment - sizeof(void*))
        return nullptr;
    void* const origin{std::malloc(bytes + alignment + sizeof(void*))};
    if (origin == nullptr)
        return nullptr;
    void* cursor{static_cast<std::byte*>(origin) + sizeof(void*)};
    std::size_t space{bytes + alignment};
    void* const aligned{std::align(alignment, bytes, cursor, space)};
    std::memcpy(static_cast<std::byte*>(aligned) - sizeof(void*), &origin, sizeof(void*));
    return aligned;
}

// post: at least `size` bytes aligned to `alignment`, or nullptr; an armed scope counts it.
// invariant: every allocating form below passes through here, so no form escapes the counter.
[[nodiscard]] void* counted_allocation(std::size_t size, std::size_t alignment) noexcept
{
    if (insight::metalog::bench::g_count_allocs)
        ++insight::metalog::bench::g_alloc_count;
    const std::size_t bytes{size != 0 ? size : 1};
    if (alignment <= alignof(std::max_align_t))
        return std::malloc(bytes);
    return over_aligned_allocation(bytes, alignment);
}

// pre: `block` came from counted_allocation at this `alignment`, or is nullptr.
void aligned_release(void* block, std::align_val_t alignment) noexcept
{
    if (block == nullptr || static_cast<std::size_t>(alignment) <= alignof(std::max_align_t))
    {
        std::free(block);
        return;
    }
    void* origin{nullptr};
    std::memcpy(&origin, static_cast<std::byte*>(block) - sizeof(void*), sizeof(void*));
    std::free(origin);
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

void operator delete(void* ptr, std::align_val_t alignment) noexcept
{
    aligned_release(ptr, alignment);
}

void operator delete[](void* ptr, std::align_val_t alignment) noexcept
{
    aligned_release(ptr, alignment);
}

void operator delete(void* ptr, std::size_t /*size*/, std::align_val_t alignment) noexcept
{
    aligned_release(ptr, alignment);
}

void operator delete[](void* ptr, std::size_t /*size*/, std::align_val_t alignment) noexcept
{
    aligned_release(ptr, alignment);
}

void operator delete(void* ptr, std::align_val_t alignment, const std::nothrow_t& /*tag*/) noexcept
{
    aligned_release(ptr, alignment);
}

void operator delete[](void* ptr, std::align_val_t alignment,
                       const std::nothrow_t& /*tag*/) noexcept
{
    aligned_release(ptr, alignment);
}
