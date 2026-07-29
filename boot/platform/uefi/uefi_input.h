#pragma once

#include "common/input.h"
#include "platform/uefi/uefi_base.h"

namespace aura67::uefi {

struct InputContext {
    SimpleTextInputProtocol* protocol;
    std::uint32_t held;
    std::uint8_t lease_ticks[8];
};

Status input_initialize(SystemTable* system_table, InputContext* context) noexcept;
Status input_poll(InputContext* context, InputState* state) noexcept;

} // namespace aura67::uefi

