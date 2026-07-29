#include "platform/uefi/uefi_clock.h"

#include <cstdint>

namespace aura67::uefi {
namespace {

constexpr std::uint64_t kNanosecondsPerSecond = 1000000000ULL;
constexpr std::uint64_t kMaximumCatchUpNanoseconds = 5ULL * kNanosecondsPerSecond;
constexpr std::uint32_t kMaximumCatchUpTicks = 150U;

bool is_leap_year(std::uint32_t year) noexcept {
    return (year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U;
}

std::uint8_t days_in_month(std::uint32_t year, std::uint8_t month) noexcept {
    constexpr std::uint8_t kDays[12] = {
        31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U,
    };
    if (month == 0U || month > 12U) {
        return 0U;
    }
    if (month == 2U && is_leap_year(year)) {
        return 29U;
    }
    return kDays[month - 1U];
}

bool timestamp_seconds(const EfiTime& time, std::uint64_t* seconds) noexcept {
    if (seconds == nullptr || time.year < 1900U || time.year > 9999U ||
        time.month == 0U || time.month > 12U || time.day == 0U ||
        time.day > days_in_month(time.year, time.month) || time.hour > 23U ||
        time.minute > 59U || time.second > 59U ||
        time.nanosecond >= kNanosecondsPerSecond) {
        return false;
    }

    const std::uint64_t completed_years = static_cast<std::uint64_t>(time.year - 1U);
    std::uint64_t days = completed_years * 365ULL + completed_years / 4ULL -
                         completed_years / 100ULL + completed_years / 400ULL;
    for (std::uint8_t month = 1U; month < time.month; ++month) {
        days += days_in_month(time.year, month);
    }
    days += static_cast<std::uint64_t>(time.day - 1U);
    *seconds = ((days * 24ULL + time.hour) * 60ULL + time.minute) * 60ULL + time.second;
    return true;
}

bool read_wall_sample(TickClock* clock, std::uint64_t* seconds,
                      std::uint32_t* nanosecond) noexcept {
    if (clock == nullptr || clock->get_time == nullptr || seconds == nullptr ||
        nanosecond == nullptr) {
        return false;
    }
    EfiTime time{};
    const Status status = clock->get_time(&time, nullptr);
    if (efi_error(status) || !timestamp_seconds(time, seconds)) {
        return false;
    }
    *nanosecond = time.nanosecond;
    return true;
}

} // namespace

Status clock_initialize(SystemTable* system_table, std::uint32_t ticks_per_second,
                        TickClock* clock) noexcept {
    if (system_table == nullptr || system_table->boot_services == nullptr || clock == nullptr ||
        ticks_per_second == 0U || ticks_per_second > 1000U) {
        return kInvalidParameter;
    }
    BootServices* boot_services = system_table->boot_services;
    if (boot_services->create_event == nullptr || boot_services->set_timer == nullptr ||
        boot_services->wait_for_event == nullptr || boot_services->close_event == nullptr) {
        return kUnsupported;
    }

    clock->boot_services = boot_services;
    clock->timer_event = nullptr;
    clock->get_time = system_table->runtime_services == nullptr
                          ? nullptr
                          : system_table->runtime_services->get_time;
    clock->previous_seconds = 0U;
    clock->previous_nanosecond = 0U;
    clock->ticks_per_second = ticks_per_second;
    clock->wall_scaled_nanoseconds = 0U;
    clock->logical_ticks = 0U;
    clock->has_wall_sample = read_wall_sample(
        clock, &clock->previous_seconds, &clock->previous_nanosecond);
    Status status = boot_services->create_event(
        kEventTimer, kTplApplication, nullptr, nullptr, &clock->timer_event);
    if (efi_error(status)) {
        clock->timer_event = nullptr;
        return status;
    }

    const std::uint64_t period =
        (kHundredNanosecondsPerSecond + ticks_per_second / 2U) / ticks_per_second;
    status = boot_services->set_timer(clock->timer_event, TimerDelay::Periodic, period);
    if (efi_error(status)) {
        boot_services->close_event(clock->timer_event);
        clock->timer_event = nullptr;
        return status;
    }
    return kSuccess;
}

Status clock_wait_for_ticks(TickClock* clock, std::uint32_t* ticks_due) noexcept {
    if (clock == nullptr || clock->boot_services == nullptr || clock->timer_event == nullptr ||
        clock->boot_services->wait_for_event == nullptr || ticks_due == nullptr) {
        return kInvalidParameter;
    }
    UintN selected_event = 0U;
    Event events[1] = {clock->timer_event};
    const Status status = clock->boot_services->wait_for_event(1U, events, &selected_event);
    if (efi_error(status)) {
        return status;
    }

    std::uint32_t due = 1U;
    std::uint64_t seconds = 0U;
    std::uint32_t nanosecond = 0U;
    if (read_wall_sample(clock, &seconds, &nanosecond)) {
        if (clock->has_wall_sample &&
            (seconds > clock->previous_seconds ||
             (seconds == clock->previous_seconds &&
              nanosecond >= clock->previous_nanosecond))) {
            const std::uint64_t second_delta = seconds - clock->previous_seconds;
            std::uint64_t delta_nanoseconds = kMaximumCatchUpNanoseconds;
            if (second_delta <= 5U) {
                if (nanosecond >= clock->previous_nanosecond) {
                    delta_nanoseconds = second_delta * kNanosecondsPerSecond +
                                        nanosecond - clock->previous_nanosecond;
                } else if (second_delta != 0U) {
                    delta_nanoseconds = (second_delta - 1U) * kNanosecondsPerSecond +
                                        (kNanosecondsPerSecond - clock->previous_nanosecond) +
                                        nanosecond;
                }
                if (delta_nanoseconds > kMaximumCatchUpNanoseconds) {
                    delta_nanoseconds = kMaximumCatchUpNanoseconds;
                }
            }
            clock->wall_scaled_nanoseconds +=
                delta_nanoseconds * clock->ticks_per_second;
        }
        clock->previous_seconds = seconds;
        clock->previous_nanosecond = nanosecond;
        clock->has_wall_sample = true;

        const std::uint64_t wall_tick_target =
            clock->wall_scaled_nanoseconds / kNanosecondsPerSecond;
        if (wall_tick_target > clock->logical_ticks) {
            const std::uint64_t lag = wall_tick_target - clock->logical_ticks;
            due = lag > kMaximumCatchUpTicks
                      ? kMaximumCatchUpTicks
                      : static_cast<std::uint32_t>(lag);
        }
    }

    clock->logical_ticks += due;
    *ticks_due = due;
    return kSuccess;
}

void clock_shutdown(TickClock* clock) noexcept {
    if (clock == nullptr || clock->boot_services == nullptr || clock->timer_event == nullptr) {
        return;
    }
    if (clock->boot_services->set_timer != nullptr) {
        clock->boot_services->set_timer(clock->timer_event, TimerDelay::Cancel, 0U);
    }
    if (clock->boot_services->close_event != nullptr) {
        clock->boot_services->close_event(clock->timer_event);
    }
    clock->timer_event = nullptr;
    clock->boot_services = nullptr;
    clock->get_time = nullptr;
    clock->has_wall_sample = false;
}

} // namespace aura67::uefi
