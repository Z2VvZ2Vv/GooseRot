#include "platform/bios/bios_platform.h"
#include "common/render_policy.h"

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

std::uint32_t g_presentation_width = kFrameWidth;
std::uint32_t g_presentation_height = kFrameHeight;
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
        RenderDetail::Detailed,
    };

    const PresentationSize presentation = fit_frame_to_bounds(
        bios_boot_info.width, bios_boot_info.height);
    if (presentation.width < kFrameWidth || presentation.height < kFrameHeight) {
        return false;
    }
    g_presentation_width = presentation.width;
    g_presentation_height = presentation.height;
    g_render_target.detail = render_detail_for_surface(
        g_presentation_width, g_presentation_height);

    g_offset_x = (bios_boot_info.width - g_presentation_width) / 2U;
    g_offset_y = (bios_boot_info.height - g_presentation_height) / 2U;
    clear_physical_framebuffer();
    return true;
}

const FrameBuffer& graphics_render_target() noexcept {
    return g_render_target;
}

void graphics_present() noexcept {
    volatile std::uint8_t* const output = physical_framebuffer();
    NearestNeighborAxis source_y(kFrameHeight, g_presentation_height);
    for (std::uint32_t destination_y = 0U;
         destination_y < g_presentation_height;
         ++destination_y) {
        const std::uint8_t* const source_row = g_render_target.base +
            static_cast<std::size_t>(source_y.source_index) *
                g_render_target.stride_bytes;
        volatile std::uint8_t* const destination_row = output +
            static_cast<std::size_t>(g_offset_y + destination_y) *
                bios_boot_info.pitch_bytes +
            static_cast<std::size_t>(g_offset_x) * kBytesPerPixel;

        NearestNeighborAxis source_x(kFrameWidth, g_presentation_width);
        std::uint32_t cached_source_x = kFrameWidth;
        std::uint32_t pixel = 0U;
        for (std::uint32_t destination_x = 0U;
             destination_x < g_presentation_width;
             ++destination_x) {
            if (source_x.source_index != cached_source_x) {
                cached_source_x = source_x.source_index;
                pixel = pack_pixel(
                    source_row +
                    static_cast<std::size_t>(cached_source_x) * kBytesPerPixel);
            }
            write_pixel(
                destination_row +
                    static_cast<std::size_t>(destination_x) * kBytesPerPixel,
                pixel);
            source_x.advance();
        }
        source_y.advance();
    }
}

} // namespace aura67::bios
