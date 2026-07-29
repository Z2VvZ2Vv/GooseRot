#pragma once

#include <cstdint>

namespace aura67 {

constexpr std::uint32_t kZeroSeedFallback = 0xA0670067U;

inline std::uint32_t random_seed(std::uint32_t seed) noexcept {
    return seed == 0U ? kZeroSeedFallback : seed;
}

// xorshift32 has a deliberately tiny, completely specified state. Its output
// is identical on all targets with a conforming 32-bit uint32_t.
inline std::uint32_t random_next(std::uint32_t* state) noexcept {
    std::uint32_t value = *state;
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    *state = value;
    return value;
}

inline std::uint32_t random_bounded(std::uint32_t* state, std::uint32_t upper_exclusive) noexcept {
    return upper_exclusive == 0U ? 0U : random_next(state) % upper_exclusive;
}

} // namespace aura67

