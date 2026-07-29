#pragma once

#include <cstdint>

namespace aura67::bios {

inline std::uint8_t io_read8(std::uint16_t port) noexcept {
    std::uint8_t value = 0U;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

inline void io_write8(std::uint16_t port, std::uint8_t value) noexcept {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

inline void io_wait() noexcept {
    // Port 0x80 is unused by the adapter. A write gives legacy devices enough
    // time to settle without depending on a firmware service.
    io_write8(0x80U, 0U);
}

} // namespace aura67::bios

