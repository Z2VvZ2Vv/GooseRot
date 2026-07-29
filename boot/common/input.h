#pragma once

#include <cstdint>

namespace aura67 {

enum class InputButton : std::uint32_t {
    Up = 1U << 0U,
    Down = 1U << 1U,
    Left = 1U << 2U,
    Right = 1U << 3U,
    Dash = 1U << 4U,
    Confirm = 1U << 5U,
    Reset = 1U << 6U,
    Escape = 1U << 7U,
};

constexpr std::uint32_t input_mask(InputButton button) noexcept {
    return static_cast<std::uint32_t>(button);
}

struct InputState {
    std::uint32_t held;
    std::uint32_t pressed;
    std::uint32_t released;

    bool is_held(InputButton button) const noexcept {
        return (held & input_mask(button)) != 0U;
    }

    bool was_pressed(InputButton button) const noexcept {
        return (pressed & input_mask(button)) != 0U;
    }

    bool was_released(InputButton button) const noexcept {
        return (released & input_mask(button)) != 0U;
    }
};

constexpr InputState no_input() noexcept {
    return InputState{0U, 0U, 0U};
}

} // namespace aura67

