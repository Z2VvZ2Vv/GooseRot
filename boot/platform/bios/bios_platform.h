#pragma once

#include "common/framebuffer.h"
#include "common/input.h"

#include <cstdint>

namespace aura67::bios {

constexpr std::uint32_t kBootInfoMagic = 0xA067B105U;

#pragma pack(push, 1)
struct BootInfo {
    std::uint32_t magic;
    std::uint32_t framebuffer_address;
    std::uint16_t width;
    std::uint16_t height;
    std::uint16_t pitch_bytes;
    std::uint8_t bits_per_pixel;
    std::uint8_t red_position;
    std::uint8_t green_position;
    std::uint8_t blue_position;
};
#pragma pack(pop)

static_assert(sizeof(BootInfo) == 18U, "BootInfo must match stage2_entry.S");

extern "C" BootInfo bios_boot_info;

bool graphics_initialize() noexcept;
const FrameBuffer& graphics_render_target() noexcept;
void graphics_present() noexcept;

void input_initialize() noexcept;
InputState input_poll() noexcept;

void clock_initialize() noexcept;
void clock_wait_tick() noexcept;

[[noreturn]] void platform_halt() noexcept;
[[noreturn]] void platform_reset() noexcept;

} // namespace aura67::bios

