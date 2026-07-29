#include "platform/uefi/uefi_input.h"

#include <cstddef>
#include <cstdint>

namespace aura67::uefi {
namespace {

constexpr std::uint32_t kMaximumKeysPerPoll = 32U;
// Simple Text Input exposes key presses and firmware-generated repeats, but no
// key-up event.  A 20-tick lease bridges the usual initial repeat delay for
// directional keys and Escape.  One-shot actions remain active for one tick.
constexpr std::uint8_t kRepeatLeaseTicks = 20U;

std::uint32_t button_for_key(const InputKey& key) noexcept {
    switch (key.scan_code) {
    case kScanUp: return input_mask(InputButton::Up);
    case kScanDown: return input_mask(InputButton::Down);
    case kScanLeft: return input_mask(InputButton::Left);
    case kScanRight: return input_mask(InputButton::Right);
    case kScanEscape: return input_mask(InputButton::Escape);
    default: break;
    }

    Char16 character = key.unicode_char;
    if (character >= static_cast<Char16>('a') && character <= static_cast<Char16>('z')) {
        character = static_cast<Char16>(character - static_cast<Char16>('a') +
                                        static_cast<Char16>('A'));
    }
    switch (character) {
    case static_cast<Char16>('W'): return input_mask(InputButton::Up);
    case static_cast<Char16>('S'): return input_mask(InputButton::Down);
    case static_cast<Char16>('A'): return input_mask(InputButton::Left);
    case static_cast<Char16>('D'): return input_mask(InputButton::Right);
    case static_cast<Char16>(' '): return input_mask(InputButton::Dash);
    case static_cast<Char16>('\r'): return input_mask(InputButton::Confirm);
    case static_cast<Char16>('R'): return input_mask(InputButton::Reset);
    case static_cast<Char16>(0x1b): return input_mask(InputButton::Escape);
    default: return 0U;
    }
}

bool uses_repeat_lease(std::size_t index) noexcept {
    return index <= 3U || index == 7U;
}

} // namespace

Status input_initialize(SystemTable* system_table, InputContext* context) noexcept {
    if (system_table == nullptr || context == nullptr || system_table->console_in == nullptr ||
        system_table->console_in->read_key_stroke == nullptr) {
        return kInvalidParameter;
    }

    context->protocol = system_table->console_in;
    context->held = 0U;
    for (std::size_t index = 0U; index < 8U; ++index) {
        context->lease_ticks[index] = 0U;
    }

    if (context->protocol->reset != nullptr) {
        const Status reset_status = context->protocol->reset(context->protocol, 0U);
        if (efi_error(reset_status) && reset_status != kUnsupported) {
            return reset_status;
        }
    }
    return kSuccess;
}

Status input_poll(InputContext* context, InputState* state) noexcept {
    if (context == nullptr || state == nullptr || context->protocol == nullptr ||
        context->protocol->read_key_stroke == nullptr) {
        return kInvalidParameter;
    }

    std::uint32_t observed = 0U;
    for (std::uint32_t read_count = 0U; read_count < kMaximumKeysPerPoll; ++read_count) {
        InputKey key{};
        const Status status = context->protocol->read_key_stroke(context->protocol, &key);
        if (status == kNotReady) {
            break;
        }
        if (efi_error(status)) {
            return status;
        }
        observed |= button_for_key(key);
    }

    const std::uint32_t previous_held = context->held;
    std::uint32_t current_held = 0U;
    for (std::size_t index = 0U; index < 8U; ++index) {
        const std::uint32_t bit = 1U << index;
        if ((observed & bit) != 0U) {
            context->lease_ticks[index] = uses_repeat_lease(index) ? kRepeatLeaseTicks : 1U;
        } else if (context->lease_ticks[index] != 0U) {
            --context->lease_ticks[index];
        }
        if (context->lease_ticks[index] != 0U) {
            current_held |= bit;
        }
    }

    context->held = current_held;
    state->held = current_held;
    state->pressed = current_held & ~previous_held;
    state->released = previous_held & ~current_held;
    return kSuccess;
}

} // namespace aura67::uefi

