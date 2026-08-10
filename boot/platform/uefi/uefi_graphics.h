#pragma once

#include "common/framebuffer.h"
#include "platform/uefi/uefi_base.h"

namespace aura67::uefi {

struct GraphicsContext {
    BootServices* boot_services;
    GraphicsOutputProtocol* protocol;
    FrameBuffer game_framebuffer;
    volatile std::uint32_t* video_base;
    std::uint32_t* blt_pixels;
    UintN blt_buffer_bytes;
    std::uint32_t horizontal_resolution;
    std::uint32_t vertical_resolution;
    std::uint32_t pixels_per_scan_line;
    std::uint32_t presentation_width;
    std::uint32_t presentation_height;
    std::uint32_t destination_x;
    std::uint32_t destination_y;
    GraphicsPixelFormat video_format;
    PixelBitmask pixel_masks;
    std::uint32_t original_mode;
    bool mode_changed;
    bool direct_framebuffer;
};

Status graphics_initialize(SystemTable* system_table, GraphicsContext* context) noexcept;
Status graphics_present(GraphicsContext* context) noexcept;
Status graphics_shutdown(GraphicsContext* context) noexcept;

} // namespace aura67::uefi
