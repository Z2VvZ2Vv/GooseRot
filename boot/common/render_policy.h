#pragma once

#include "common/framebuffer.h"

#include <cstdint>

namespace aura67 {

struct PresentationSize {
    std::uint32_t width;
    std::uint32_t height;
};

constexpr std::uint64_t kDetailedPresentationPixelBudget =
    static_cast<std::uint64_t>(1280U) * 720U;

// A Blt-only GOP needs a complete software-scaled transfer buffer. Capping that
// fallback at 1080p keeps the one-time allocation below 8 MiB while still
// making high-resolution firmware displays substantially more legible than a
// centered 640x360 image.
constexpr std::uint32_t kMaximumSoftwarePresentationWidth = 1920U;
constexpr std::uint32_t kMaximumSoftwarePresentationHeight = 1080U;

// Fits the fixed logical frame inside arbitrary bounds without the abrupt 2x
// to 1x size drop caused by unconditional integer scaling.
constexpr PresentationSize fit_frame_to_bounds(std::uint32_t bounds_width,
                                                 std::uint32_t bounds_height) noexcept {
    if (bounds_width == 0U || bounds_height == 0U) {
        return PresentationSize{0U, 0U};
    }

    const std::uint64_t width_limited_height =
        static_cast<std::uint64_t>(bounds_width) * kFrameHeight / kFrameWidth;
    if (static_cast<std::uint64_t>(bounds_width) * kFrameHeight <=
        static_cast<std::uint64_t>(bounds_height) * kFrameWidth) {
        return PresentationSize{
            bounds_width,
            static_cast<std::uint32_t>(width_limited_height == 0U
                                           ? 1U
                                           : width_limited_height),
        };
    }

    const std::uint64_t height_limited_width =
        static_cast<std::uint64_t>(bounds_height) * kFrameWidth / kFrameHeight;
    return PresentationSize{
        static_cast<std::uint32_t>(height_limited_width == 0U
                                       ? 1U
                                       : height_limited_width),
        bounds_height,
    };
}

// Legacy VBE reports 16-bit dimensions. Keeping that information in the
// overload lets the 32-bit freestanding build use native arithmetic instead of
// pulling a hosted 64-bit division helper into the BIOS image.
constexpr PresentationSize fit_frame_to_bounds(std::uint16_t bounds_width,
                                                std::uint16_t bounds_height) noexcept {
    if (bounds_width == 0U || bounds_height == 0U) {
        return PresentationSize{0U, 0U};
    }

    const std::uint32_t width = bounds_width;
    const std::uint32_t height = bounds_height;
    if (width * kFrameHeight <= height * kFrameWidth) {
        const std::uint32_t scaled_height = width * kFrameHeight / kFrameWidth;
        return PresentationSize{
            width,
            scaled_height == 0U ? 1U : scaled_height,
        };
    }
    const std::uint32_t scaled_width = height * kFrameWidth / kFrameHeight;
    return PresentationSize{
        scaled_width == 0U ? 1U : scaled_width,
        height,
    };
}

constexpr PresentationSize fit_frame_to_bounded_surface(
    std::uint32_t bounds_width,
    std::uint32_t bounds_height,
    std::uint32_t maximum_width,
    std::uint32_t maximum_height) noexcept {
    const std::uint32_t limited_width = bounds_width < maximum_width
                                            ? bounds_width
                                            : maximum_width;
    const std::uint32_t limited_height = bounds_height < maximum_height
                                             ? bounds_height
                                             : maximum_height;
    return fit_frame_to_bounds(limited_width, limited_height);
}

// Division-free nearest-neighbour stepping for freestanding presenters. The
// firmware callers upscale (or copy 1:1), so advance normally performs at most
// one iteration; the loop also keeps the helper correct for modest downscales.
struct NearestNeighborAxis {
    std::uint32_t source_index;
    std::uint32_t source_extent;
    std::uint32_t destination_extent;
    std::uint64_t remainder;

    constexpr NearestNeighborAxis(std::uint32_t source_size,
                                  std::uint32_t destination_size) noexcept
        : source_index(0U),
          source_extent(source_size),
          destination_extent(destination_size),
          remainder(0U) {}

    constexpr void advance() noexcept {
        if (source_extent == 0U || destination_extent == 0U) {
            return;
        }
        remainder += source_extent;
        while (remainder >= destination_extent) {
            remainder -= destination_extent;
            if (source_index + 1U < source_extent) {
                ++source_index;
            }
        }
    }
};

constexpr RenderDetail render_detail_for_machine(
    std::uint32_t logical_processors,
    std::uint64_t physical_memory_bytes) noexcept {
    constexpr std::uint64_t kTwelveGibibytes = 12ULL * 1024ULL * 1024ULL * 1024ULL;
    return logical_processors >= 12U && physical_memory_bytes >= kTwelveGibibytes
               ? RenderDetail::Detailed
               : RenderDetail::Reduced;
}

constexpr RenderDetail render_detail_for_surface(
    std::uint32_t width,
    std::uint32_t height,
    std::uint64_t maximum_detailed_pixels = kDetailedPresentationPixelBudget) noexcept {
    const std::uint64_t pixels =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    return pixels > maximum_detailed_pixels
               ? RenderDetail::Reduced
               : RenderDetail::Detailed;
}

// Hosted adapters can combine the machine check with the same presentation
// budget used by firmware. Firmware deliberately calls render_detail_for_surface
// directly because neither legacy BIOS nor this minimal UEFI adapter has a
// trustworthy, portable CPU/RAM capability probe.
constexpr RenderDetail render_detail_for_machine_and_surface(
    std::uint32_t logical_processors,
    std::uint64_t physical_memory_bytes,
    std::uint32_t width,
    std::uint32_t height) noexcept {
    return render_detail_for_machine(logical_processors, physical_memory_bytes) ==
                   RenderDetail::Reduced ||
               render_detail_for_surface(width, height) == RenderDetail::Reduced
           ? RenderDetail::Reduced
           : RenderDetail::Detailed;
}

} // namespace aura67
