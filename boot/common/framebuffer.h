#pragma once

#include <cstddef>
#include <cstdint>

namespace aura67 {

constexpr std::uint32_t kFrameWidth = 640U;
constexpr std::uint32_t kFrameHeight = 360U;
constexpr std::uint32_t kBytesPerPixel = 4U;

enum class PixelFormat : std::uint8_t {
    Bgra8888,
    Rgba8888,
};

// Rendering detail is presentation policy, not game state. Platforms can
// reduce decorative work without changing simulation, collision visibility or
// deterministic replay.
enum class RenderDetail : std::uint8_t {
    Detailed,
    Reduced,
};

// The owner supplies the storage. The game never allocates or frees it.
struct FrameBuffer {
    std::uint8_t* base;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t stride_bytes;
    PixelFormat format;
    RenderDetail detail = RenderDetail::Detailed;
};

inline bool framebuffer_is_valid(const FrameBuffer& framebuffer) noexcept {
    return framebuffer.base != nullptr &&
           framebuffer.width == kFrameWidth &&
           framebuffer.height == kFrameHeight &&
           framebuffer.stride_bytes >= kFrameWidth * kBytesPerPixel;
}

} // namespace aura67
