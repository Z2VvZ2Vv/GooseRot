#include "platform/bios/bios_platform.h"

#include "platform/bios/bios_io.h"

#include <cstdint>

namespace aura67::bios {
namespace {

constexpr std::uint16_t kControllerStatusPort = 0x64U;
constexpr std::uint16_t kControllerDataPort = 0x60U;
constexpr std::uint8_t kStatusOutputFull = 1U << 0U;
constexpr std::uint8_t kStatusAuxiliaryData = 1U << 5U;

std::uint32_t g_held = 0U;
bool g_extended = false;
std::uint8_t g_pause_bytes_remaining = 0U;

std::uint32_t button_for_scan_code(std::uint8_t scan_code, bool extended) noexcept {
    if (extended) {
        switch (scan_code) {
        case 0x48U:
            return input_mask(InputButton::Up);
        case 0x50U:
            return input_mask(InputButton::Down);
        case 0x4BU:
            return input_mask(InputButton::Left);
        case 0x4DU:
            return input_mask(InputButton::Right);
        default:
            return 0U;
        }
    }

    switch (scan_code) {
    case 0x11U: // W
        return input_mask(InputButton::Up);
    case 0x1FU: // S
        return input_mask(InputButton::Down);
    case 0x1EU: // A
        return input_mask(InputButton::Left);
    case 0x20U: // D
        return input_mask(InputButton::Right);
    case 0x39U: // Space
        return input_mask(InputButton::Dash);
    case 0x1CU: // Enter
        return input_mask(InputButton::Confirm);
    case 0x13U: // R
        return input_mask(InputButton::Reset);
    case 0x01U: // Escape
        return input_mask(InputButton::Escape);
    default:
        return 0U;
    }
}

void consume_scan_code(std::uint8_t value) noexcept {
    if (g_pause_bytes_remaining != 0U) {
        --g_pause_bytes_remaining;
        return;
    }
    if (value == 0xE1U) {
        // Pause uses a six-byte sequence in scan-code set 1.
        g_pause_bytes_remaining = 5U;
        g_extended = false;
        return;
    }
    if (value == 0xE0U) {
        g_extended = true;
        return;
    }

    const bool released = (value & 0x80U) != 0U;
    const std::uint8_t scan_code = static_cast<std::uint8_t>(value & 0x7FU);
    const std::uint32_t button = button_for_scan_code(scan_code, g_extended);
    g_extended = false;
    if (button == 0U) {
        return;
    }

    if (released) {
        g_held &= ~button;
    } else {
        g_held |= button;
    }
}

} // namespace

void input_initialize() noexcept {
    g_held = 0U;
    g_extended = false;
    g_pause_bytes_remaining = 0U;

    // Drain stale controller bytes. The status guard prevents an unbounded
    // loop on a broken controller while still clearing a complete PS/2 packet.
    for (std::uint32_t count = 0U; count < 32U; ++count) {
        if ((io_read8(kControllerStatusPort) & kStatusOutputFull) == 0U) {
            break;
        }
        static_cast<void>(io_read8(kControllerDataPort));
    }
}

InputState input_poll() noexcept {
    const std::uint32_t previous = g_held;
    for (std::uint32_t count = 0U; count < 64U; ++count) {
        const std::uint8_t status = io_read8(kControllerStatusPort);
        if ((status & kStatusOutputFull) == 0U) {
            break;
        }
        const std::uint8_t value = io_read8(kControllerDataPort);
        if ((status & kStatusAuxiliaryData) == 0U) {
            consume_scan_code(value);
        }
    }

    return InputState{
        g_held,
        g_held & ~previous,
        previous & ~g_held,
    };
}

} // namespace aura67::bios

