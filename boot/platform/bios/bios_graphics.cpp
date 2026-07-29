#include "platform/bios/bios_platform.h"

#include <cstddef>
#include <cstdint>

namespace aura67::bios {
namespace {

// The stage-2 loader enables A20 before entering the C++ core. Keeping the
// 900 KiB software surface at 1 MiB avoids both the loaded image below 128 KiB
// and the protected-mode stack below conventional-memory video apertures.
constexpr std::uintptr_t kRenderBufferAddress = 0x00100000U;
constexpr std::uintptr_t kRenderBufferEnd =
    kRenderBufferAddress +
    static_cast<std::uintptr_t>(kFrameWidth) * kFrameHeight * kBytesPerPixel;
static_assert(kRenderBufferEnd < 0x00200000U, "render target must fit below 2 MiB");

FrameBuffer g_render_target{};

std::uint32_t g_scale = 1U;
std::uint32_t g_offset_x = 0U;
std::uint32_t g_offset_y = 0U;

volatile std::uint8_t* physical_framebuffer() noexcept {
    return reinterpret_cast<volatile std::uint8_t*>(
        static_cast<std::uintptr_t>(bios_boot_info.framebuffer_address));
}

std::uint32_t pack_pixel(const std::uint8_t* source) noexcept {
    const std::uint32_t blue = source[0];
    const std::uint32_t green = source[1];
    const std::uint32_t red = source[2];
    return (red << bios_boot_info.red_position) |
           (green << bios_boot_info.green_position) |
           (blue << bios_boot_info.blue_position);
}

void write_pixel(volatile std::uint8_t* destination, std::uint32_t pixel) noexcept {
    destination[0] = static_cast<std::uint8_t>(pixel);
    destination[1] = static_cast<std::uint8_t>(pixel >> 8U);
    destination[2] = static_cast<std::uint8_t>(pixel >> 16U);
    destination[3] = static_cast<std::uint8_t>(pixel >> 24U);
}

void clear_physical_framebuffer() noexcept {
    volatile std::uint8_t* const target = physical_framebuffer();
    for (std::uint32_t y = 0U; y < bios_boot_info.height; ++y) {
        volatile std::uint8_t* row = target +
            static_cast<std::size_t>(y) * bios_boot_info.pitch_bytes;
        for (std::uint32_t x = 0U; x < bios_boot_info.width; ++x) {
            write_pixel(row + static_cast<std::size_t>(x) * kBytesPerPixel, 0U);
        }
    }
}

} // namespace

bool graphics_initialize() noexcept {
    if (bios_boot_info.magic != kBootInfoMagic ||
        bios_boot_info.framebuffer_address < 0x00200000U ||
        bios_boot_info.width < kFrameWidth ||
        bios_boot_info.height < kFrameHeight ||
        bios_boot_info.bits_per_pixel != 32U ||
        bios_boot_info.pitch_bytes < bios_boot_info.width * kBytesPerPixel) {
        return false;
    }

    const bool bgra = bios_boot_info.red_position == 16U &&
                      bios_boot_info.green_position == 8U &&
                      bios_boot_info.blue_position == 0U;
    const bool rgba = bios_boot_info.red_position == 0U &&
                      bios_boot_info.green_position == 8U &&
                      bios_boot_info.blue_position == 16U;
    if (!bgra && !rgba) {
        return false;
    }

    g_render_target = FrameBuffer{
        reinterpret_cast<std::uint8_t*>(kRenderBufferAddress),
        kFrameWidth,
        kFrameHeight,
        kFrameWidth * kBytesPerPixel,
        PixelFormat::Bgra8888,
    };

    const std::uint32_t horizontal_scale = bios_boot_info.width / kFrameWidth;
    const std::uint32_t vertical_scale = bios_boot_info.height / kFrameHeight;
    g_scale = horizontal_scale < vertical_scale ? horizontal_scale : vertical_scale;
    if (g_scale == 0U) {
        return false;
    }

    g_offset_x = (bios_boot_info.width - kFrameWidth * g_scale) / 2U;
    g_offset_y = (bios_boot_info.height - kFrameHeight * g_scale) / 2U;
    clear_physical_framebuffer();
    return true;
}

const FrameBuffer& graphics_render_target() noexcept {
    return g_render_target;
}

void graphics_present() noexcept {
    volatile std::uint8_t* const output = physical_framebuffer();
    for (std::uint32_t source_y = 0U; source_y < kFrameHeight; ++source_y) {
        const std::uint8_t* const source_row = g_render_target.base +
            static_cast<std::size_t>(source_y) * g_render_target.stride_bytes;
        const std::uint32_t destination_y = g_offset_y + source_y * g_scale;

        for (std::uint32_t repeat_y = 0U; repeat_y < g_scale; ++repeat_y) {
            volatile std::uint8_t* destination_row = output +
                static_cast<std::size_t>(destination_y + repeat_y) *
                    bios_boot_info.pitch_bytes +
                static_cast<std::size_t>(g_offset_x) * kBytesPerPixel;

            for (std::uint32_t source_x = 0U; source_x < kFrameWidth; ++source_x) {
                const std::uint32_t pixel = pack_pixel(
                    source_row + static_cast<std::size_t>(source_x) * kBytesPerPixel);
                for (std::uint32_t repeat_x = 0U; repeat_x < g_scale; ++repeat_x) {
                    write_pixel(
                        destination_row +
                            static_cast<std::size_t>(source_x * g_scale + repeat_x) *
                                kBytesPerPixel,
                        pixel);
                }
            }
        }
    }
}

} // namespace aura67::bios
