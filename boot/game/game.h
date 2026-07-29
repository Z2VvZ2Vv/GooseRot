#pragma once

#include "common/framebuffer.h"
#include "common/input.h"

#include <cstddef>
#include <cstdint>

namespace aura67 {

constexpr std::uint32_t kTickRate = 30U;
constexpr std::uint32_t kRoundSeconds = 67U;
constexpr std::uint32_t kRoundTicks = kTickRate * kRoundSeconds;
constexpr std::int32_t kFixedShift = 8;
constexpr std::int32_t kFixedOne = 1 << kFixedShift;
constexpr std::int32_t kDashPixels = 67;
constexpr std::size_t kBadgeCount = 6U;
constexpr std::size_t kNpcCount = 4U;

enum class GamePhase : std::uint8_t {
    Playing,
    Finished,
};

enum class GameSignal : std::uint8_t {
    None,
    QuitRequested,
    ResetRequested,
};

enum class Facing : std::uint8_t {
    Right,
    Down,
    Left,
    Up,
};

struct GooseState {
    std::int32_t x_fp;
    std::int32_t y_fp;
    std::int32_t velocity_x_fp;
    std::int32_t velocity_y_fp;
    Facing facing;
};

struct BadgeState {
    std::int32_t x_fp;
    std::int32_t y_fp;
    std::uint8_t active;
};

struct NpcState {
    std::int32_t x_fp;
    std::int32_t y_fp;
    std::int32_t velocity_x_fp;
    std::int32_t velocity_y_fp;
    std::uint8_t active;
};

struct CursorState {
    std::int32_t x_fp;
    std::int32_t y_fp;
    std::uint16_t stun_ticks;
};

// This is the complete mutable game state. It owns only fixed-size values and
// arrays, making snapshots cheap and firmware ports independent of a heap.
struct GameState {
    std::uint32_t seed;
    std::uint32_t rng_state;
    std::uint32_t tick;
    std::int32_t score;
    GooseState goose;
    BadgeState badges[kBadgeCount];
    NpcState npcs[kNpcCount];
    CursorState hostile_cursor;
    std::uint32_t escape_hold_ticks;
    std::uint16_t dash_cooldown_ticks;
    std::uint16_t damage_cooldown_ticks;
    std::uint16_t badges_collected;
    std::uint16_t npc_hits;
    std::uint8_t palette_index;
    std::uint8_t maximum_brainrot;
    std::uint8_t last_dash_pixels;
    std::uint8_t last_cursor_push_pixels;
    GamePhase phase;
};

void game_initialize(GameState* game, std::uint32_t seed) noexcept;
GameSignal game_tick(GameState* game, const InputState& input) noexcept;
bool game_render(const GameState& game, const FrameBuffer& framebuffer) noexcept;

std::uint32_t game_seconds_remaining(const GameState& game) noexcept;
std::uint64_t game_checksum(const GameState& game) noexcept;

constexpr std::int32_t pixels_to_fixed(std::int32_t pixels) noexcept {
    return pixels * kFixedOne;
}

constexpr std::int32_t fixed_to_pixels(std::int32_t fixed) noexcept {
    return fixed / kFixedOne;
}

} // namespace aura67

