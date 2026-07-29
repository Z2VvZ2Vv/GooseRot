#pragma once

#include <cstddef>
#include <cstdint>

namespace aura67 {

// Optional fixed storage helper for future firmware adapters. It performs no
// allocation itself and never calls an operating-system service.
struct Arena {
    std::uint8_t* base;
    std::size_t capacity;
    std::size_t used;
};

inline void arena_initialize(Arena* arena, void* storage, std::size_t size) noexcept {
    if (arena == nullptr) {
        return;
    }
    arena->base = static_cast<std::uint8_t*>(storage);
    arena->capacity = size;
    arena->used = 0U;
}

inline void arena_reset(Arena* arena) noexcept {
    if (arena != nullptr) {
        arena->used = 0U;
    }
}

inline void* arena_allocate(Arena* arena, std::size_t size, std::size_t alignment) noexcept {
    if (arena == nullptr || arena->base == nullptr || alignment == 0U ||
        (alignment & (alignment - 1U)) != 0U) {
        return nullptr;
    }

    const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(arena->base) + arena->used;
    const std::uintptr_t aligned = (start + alignment - 1U) & ~(alignment - 1U);
    const std::size_t padding = static_cast<std::size_t>(aligned - start);
    if (padding > arena->capacity - arena->used ||
        size > arena->capacity - arena->used - padding) {
        return nullptr;
    }

    arena->used += padding + size;
    return reinterpret_cast<void*>(aligned);
}

} // namespace aura67

