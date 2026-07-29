#pragma once

#include "platform/uefi/uefi_base.h"

namespace aura67::uefi {

struct TickClock {
    BootServices* boot_services;
    Event timer_event;
    GetTime get_time;
    std::uint64_t previous_seconds;
    std::uint32_t previous_nanosecond;
    std::uint32_t ticks_per_second;
    std::uint64_t wall_scaled_nanoseconds;
    std::uint64_t logical_ticks;
    bool has_wall_sample;
};

Status clock_initialize(SystemTable* system_table, std::uint32_t ticks_per_second,
                        TickClock* clock) noexcept;
Status clock_wait_for_ticks(TickClock* clock, std::uint32_t* ticks_due) noexcept;
void clock_shutdown(TickClock* clock) noexcept;

} // namespace aura67::uefi
