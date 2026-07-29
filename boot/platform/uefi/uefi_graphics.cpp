#include "platform/uefi/uefi_graphics.h"

#include <cstddef>
#include <cstdint>

namespace aura67::uefi {
namespace {

constexpr std::size_t kPixelCount =
    static_cast<std::size_t>(kFrameWidth) * static_cast<std::size_t>(kFrameHeight);
constexpr std::uint32_t kMaximumEnumeratedModes = 4096U;

// Fixed storage keeps the adapter heap-free.  Its byte layout is also exactly
// EFI_GRAPHICS_OUTPUT_BLT_PIXEL (B, G, R, reserved), so GOP::Blt can consume it
// directly on firmware that exposes no linear framebuffer.
alignas(16) std::uint32_t g_game_pixels[kPixelCount];

std::uint32_t channel_to_mask(std::uint8_t channel, std::uint32_t mask) noexcept {
    if (mask == 0U) {
        return 0U;
    }

    std::uint32_t bit_count = 0U;
    for (std::uint32_t bit = 0U; bit < 32U; ++bit) {
        if ((mask & (1U << bit)) != 0U) {
            ++bit_count;
        }
    }
    const std::uint32_t maximum = bit_count >= 32U ? 0xffffffffU : ((1U << bit_count) - 1U);
    const std::uint32_t scaled = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(channel) * maximum + 127U) / 255U);

    std::uint32_t packed = 0U;
    std::uint32_t source_bit = 0U;
    for (std::uint32_t target_bit = 0U; target_bit < 32U; ++target_bit) {
        const std::uint32_t target_mask = 1U << target_bit;
        if ((mask & target_mask) == 0U) {
            continue;
        }
        if ((scaled & (1U << source_bit)) != 0U) {
            packed |= target_mask;
        }
        ++source_bit;
    }
    return packed;
}

std::uint32_t convert_pixel(std::uint32_t bgra, const GraphicsContext& context) noexcept {
    const std::uint8_t blue = static_cast<std::uint8_t>(bgra & 0xffU);
    const std::uint8_t green = static_cast<std::uint8_t>((bgra >> 8U) & 0xffU);
    const std::uint8_t red = static_cast<std::uint8_t>((bgra >> 16U) & 0xffU);

    switch (context.video_format) {
    case GraphicsPixelFormat::BlueGreenRedReserved8BitPerColor:
        return static_cast<std::uint32_t>(blue) |
               (static_cast<std::uint32_t>(green) << 8U) |
               (static_cast<std::uint32_t>(red) << 16U);
    case GraphicsPixelFormat::RedGreenBlueReserved8BitPerColor:
        return static_cast<std::uint32_t>(red) |
               (static_cast<std::uint32_t>(green) << 8U) |
               (static_cast<std::uint32_t>(blue) << 16U);
    case GraphicsPixelFormat::BitMask:
        return channel_to_mask(red, context.pixel_masks.red_mask) |
               channel_to_mask(green, context.pixel_masks.green_mask) |
               channel_to_mask(blue, context.pixel_masks.blue_mask);
    default:
        return 0U;
    }
}

void clear_direct_framebuffer(const GraphicsContext& context) noexcept {
    if (context.video_base == nullptr) {
        return;
    }
    for (std::uint32_t y = 0U; y < context.vertical_resolution; ++y) {
        volatile std::uint32_t* row =
            context.video_base + static_cast<std::size_t>(y) * context.pixels_per_scan_line;
        for (std::uint32_t x = 0U; x < context.horizontal_resolution; ++x) {
            row[x] = 0U;
        }
    }
}

Status clear_blt_framebuffer(GraphicsContext* context) noexcept {
    if (context == nullptr || context->protocol == nullptr || context->protocol->blt == nullptr) {
        return kInvalidParameter;
    }
    GraphicsOutputBltPixel black{};
    return context->protocol->blt(
        context->protocol,
        &black,
        GraphicsOutputBltOperation::VideoFill,
        0U,
        0U,
        0U,
        0U,
        context->horizontal_resolution,
        context->vertical_resolution,
        0U);
}

bool mode_supports_game(const GraphicsOutputModeInformation* info) noexcept {
    return info != nullptr && info->horizontal_resolution >= kFrameWidth &&
           info->vertical_resolution >= kFrameHeight &&
           info->pixel_format < GraphicsPixelFormat::Maximum;
}

Status select_compatible_mode(BootServices* boot_services,
                              GraphicsOutputProtocol* protocol,
                              std::uint32_t* original_mode,
                              bool* mode_changed) noexcept {
    if (boot_services == nullptr || protocol == nullptr || protocol->mode == nullptr ||
        original_mode == nullptr || mode_changed == nullptr) {
        return kInvalidParameter;
    }

    *original_mode = protocol->mode->mode;
    *mode_changed = false;
    if (mode_supports_game(protocol->mode->info)) {
        return kSuccess;
    }
    if (protocol->query_mode == nullptr || protocol->set_mode == nullptr ||
        boot_services->free_pool == nullptr || protocol->mode->max_mode == 0U ||
        protocol->mode->max_mode > kMaximumEnumeratedModes) {
        return kUnsupported;
    }

    std::uint32_t best_mode = protocol->mode->max_mode;
    std::uint64_t best_area = UINT64_MAX;
    for (std::uint32_t mode = 0U; mode < protocol->mode->max_mode; ++mode) {
        UintN information_size = 0U;
        GraphicsOutputModeInformation* information = nullptr;
        const Status query_status =
            protocol->query_mode(protocol, mode, &information_size, &information);
        if (!efi_error(query_status) && information != nullptr &&
            information_size >= sizeof(GraphicsOutputModeInformation) &&
            mode_supports_game(information)) {
            const std::uint64_t area =
                static_cast<std::uint64_t>(information->horizontal_resolution) *
                information->vertical_resolution;
            if (area < best_area) {
                best_area = area;
                best_mode = mode;
            }
        }
        if (information != nullptr) {
            const Status free_status = boot_services->free_pool(information);
            if (efi_error(free_status)) {
                return free_status;
            }
        }
    }
    if (best_mode == protocol->mode->max_mode) {
        return kUnsupported;
    }

    const Status set_status = protocol->set_mode(protocol, best_mode);
    if (efi_error(set_status)) {
        return set_status;
    }
    *mode_changed = best_mode != *original_mode;
    if (protocol->mode == nullptr || !mode_supports_game(protocol->mode->info)) {
        if (*mode_changed) {
            const Status restore_status = protocol->set_mode(protocol, *original_mode);
            if (efi_error(restore_status)) {
                return restore_status;
            }
            *mode_changed = false;
        }
        return kUnsupported;
    }
    return kSuccess;
}

} // namespace

Status graphics_initialize(SystemTable* system_table, GraphicsContext* context) noexcept {
    if (system_table == nullptr || system_table->boot_services == nullptr || context == nullptr) {
        return kInvalidParameter;
    }
    BootServices* boot_services = system_table->boot_services;
    if (boot_services->locate_protocol == nullptr) {
        return kUnsupported;
    }

    void* interface_pointer = nullptr;
    const Status locate_status = boot_services->locate_protocol(
        &kGraphicsOutputProtocolGuid, nullptr, &interface_pointer);
    if (efi_error(locate_status)) {
        return locate_status;
    }

    auto* protocol = static_cast<GraphicsOutputProtocol*>(interface_pointer);
    if (protocol == nullptr || protocol->mode == nullptr || protocol->blt == nullptr) {
        return kUnsupported;
    }
    std::uint32_t original_mode = 0U;
    bool mode_changed = false;
    const Status mode_status = select_compatible_mode(
        boot_services, protocol, &original_mode, &mode_changed);
    if (efi_error(mode_status)) {
        return mode_status;
    }
    GraphicsOutputModeInformation* info = protocol->mode->info;

    context->boot_services = boot_services;
    context->protocol = protocol;
    context->game_framebuffer = FrameBuffer{
        reinterpret_cast<std::uint8_t*>(g_game_pixels),
        kFrameWidth,
        kFrameHeight,
        kFrameWidth * kBytesPerPixel,
        PixelFormat::Bgra8888,
    };
    context->horizontal_resolution = info->horizontal_resolution;
    context->vertical_resolution = info->vertical_resolution;
    context->pixels_per_scan_line = info->pixels_per_scan_line;
    context->video_format = info->pixel_format;
    context->pixel_masks = info->pixel_information;
    context->original_mode = original_mode;
    context->mode_changed = mode_changed;
    const std::uint32_t horizontal_scale = info->horizontal_resolution / kFrameWidth;
    const std::uint32_t vertical_scale = info->vertical_resolution / kFrameHeight;
    context->scale = horizontal_scale < vertical_scale ? horizontal_scale : vertical_scale;
    if (context->scale == 0U) {
        const Status restore_status = graphics_shutdown(context);
        return efi_error(restore_status) ? restore_status : kUnsupported;
    }
    const std::uint32_t presented_width = kFrameWidth * context->scale;
    const std::uint32_t presented_height = kFrameHeight * context->scale;
    context->destination_x = (info->horizontal_resolution - presented_width) / 2U;
    context->destination_y = (info->vertical_resolution - presented_height) / 2U;

    const bool directly_addressable =
        info->pixel_format == GraphicsPixelFormat::RedGreenBlueReserved8BitPerColor ||
        info->pixel_format == GraphicsPixelFormat::BlueGreenRedReserved8BitPerColor ||
        info->pixel_format == GraphicsPixelFormat::BitMask;
    const bool valid_scan_line =
        info->pixels_per_scan_line >= info->horizontal_resolution;
    const std::uint64_t required_bytes =
        static_cast<std::uint64_t>(info->pixels_per_scan_line) * info->vertical_resolution *
        sizeof(std::uint32_t);
    context->direct_framebuffer = directly_addressable &&
                                  valid_scan_line &&
                                  protocol->mode->framebuffer_base != 0U &&
                                  protocol->mode->framebuffer_size >= required_bytes;
    context->video_base = context->direct_framebuffer
                              ? reinterpret_cast<volatile std::uint32_t*>(
                                    static_cast<std::uintptr_t>(protocol->mode->framebuffer_base))
                              : nullptr;

    if (context->direct_framebuffer) {
        clear_direct_framebuffer(*context);
        return kSuccess;
    }

    // GOP Blt has no scaling operation.  A Blt-only mode therefore receives a
    // centered 1:1 image while directly addressable modes use integer scaling.
    context->scale = 1U;
    context->destination_x = (info->horizontal_resolution - kFrameWidth) / 2U;
    context->destination_y = (info->vertical_resolution - kFrameHeight) / 2U;
    const Status clear_status = clear_blt_framebuffer(context);
    if (efi_error(clear_status)) {
        const Status restore_status = graphics_shutdown(context);
        return efi_error(restore_status) ? restore_status : clear_status;
    }
    return clear_status;
}

Status graphics_present(GraphicsContext* context) noexcept {
    if (context == nullptr || !framebuffer_is_valid(context->game_framebuffer) ||
        context->protocol == nullptr) {
        return kInvalidParameter;
    }

    if (!context->direct_framebuffer) {
        if (context->protocol->blt == nullptr) {
            return kUnsupported;
        }
        static_assert(sizeof(GraphicsOutputBltPixel) == sizeof(std::uint32_t));
        return context->protocol->blt(
            context->protocol,
            reinterpret_cast<GraphicsOutputBltPixel*>(g_game_pixels),
            GraphicsOutputBltOperation::BufferToVideo,
            0U,
            0U,
            context->destination_x,
            context->destination_y,
            kFrameWidth,
            kFrameHeight,
            kFrameWidth * sizeof(GraphicsOutputBltPixel));
    }

    for (std::uint32_t source_y = 0U; source_y < kFrameHeight; ++source_y) {
        const std::uint32_t* source =
            g_game_pixels + static_cast<std::size_t>(source_y) * kFrameWidth;
        for (std::uint32_t repeat_y = 0U; repeat_y < context->scale; ++repeat_y) {
            const std::uint32_t destination_y =
                context->destination_y + source_y * context->scale + repeat_y;
            volatile std::uint32_t* destination =
                context->video_base +
                static_cast<std::size_t>(destination_y) * context->pixels_per_scan_line +
                context->destination_x;
            for (std::uint32_t source_x = 0U; source_x < kFrameWidth; ++source_x) {
                const std::uint32_t pixel = convert_pixel(source[source_x], *context);
                const std::uint32_t destination_x = source_x * context->scale;
                for (std::uint32_t repeat_x = 0U; repeat_x < context->scale; ++repeat_x) {
                    destination[destination_x + repeat_x] = pixel;
                }
            }
        }
    }
    return kSuccess;
}

Status graphics_shutdown(GraphicsContext* context) noexcept {
    if (context == nullptr) {
        return kInvalidParameter;
    }
    Status restore_status = kSuccess;
    if (context->mode_changed && context->protocol != nullptr &&
        context->protocol->set_mode != nullptr) {
        restore_status = context->protocol->set_mode(
            context->protocol, context->original_mode);
    }
    context->video_base = nullptr;
    context->protocol = nullptr;
    context->boot_services = nullptr;
    context->mode_changed = false;
    return restore_status;
}

} // namespace aura67::uefi
