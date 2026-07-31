#pragma once

#include "common/framebuffer.h"
#include "common/input.h"

#include <cstddef>
#include <cstdint>

namespace aura67 {

constexpr std::uint32_t kTickRate = 30U;
constexpr std::int32_t kFixedShift = 8;
constexpr std::int32_t kFixedOne = 1 << kFixedShift;

// The run never ends on a timer. It ends when the goose eats an obstacle, and
// the next run starts immediately, so the only real clock is the player's.
// Every reward keeps the original "67" economy of the project.
constexpr std::int32_t kBadgeAura = 67;
constexpr std::int32_t kClutchAura = 67;
constexpr std::int32_t kShatterAura = 67;
constexpr std::int32_t kPixelsPerAura = 5;
constexpr std::int32_t kPaletteWaveAura = 670;
constexpr std::int32_t kBrainrotAura = 9999;
constexpr std::int32_t kAuraCeiling = 999999999;
constexpr std::uint16_t kRootModeTicks = 67U;
constexpr std::uint16_t kComboDecayTicks = 150U;
constexpr std::uint8_t kMaximumCombo = 9U;

// Track geometry, in pixels of the fixed 640x360 frame.
constexpr std::int32_t kGroundY = 300;
constexpr std::int32_t kRunnerX = 134; // Two times 67 pixels from the left edge.
constexpr std::int32_t kRunnerHalfWidth = 18;
constexpr std::int32_t kRunnerStandHeight = 44;
constexpr std::int32_t kRunnerDuckHeight = 24;
// Waves are laid down well off the right edge so badge arcs never pop in.
constexpr std::int32_t kSpawnX = 700;
constexpr std::int32_t kDespawnX = -96;

// Motion, all Q24.8 units per tick. A ground jump lasts exactly 25 ticks and
// peaks 86 pixels up, which is the fairness bound the spawner is built on.
constexpr std::int32_t kGravityFp = 290;
constexpr std::int32_t kJumpVelocityFp = 3700;
constexpr std::int32_t kFlapVelocityFp = 2600;
constexpr std::int32_t kFastFallVelocityFp = 2900;
constexpr std::int32_t kJumpAirTicks = 25;
constexpr std::int32_t kJumpApexPixels = 86;

// Endless difficulty ramp: the scroll speed climbs one Q24.8 unit per tick from
// 11.5 to 20.75 pixels per tick, then obstacle density keeps rising alone.
constexpr std::int32_t kStartSpeedFp = 2944;
constexpr std::int32_t kMaximumSpeedFp = 5312;
constexpr std::int32_t kSpeedGainFp = 1;

// Guaranteed clearable spacing: the shortest gap the spawner may roll stays
// above the 26-tick jump plus a reaction margin.
constexpr std::int32_t kMinimumSpawnTicks = 36;
constexpr std::int32_t kOpeningGraceTicks = 45;

constexpr std::size_t kObstacleCount = 8U;
constexpr std::size_t kPickupCount = 12U;
constexpr std::size_t kPopupCount = 4U;
constexpr std::size_t kDustCount = 12U;

enum class GamePhase : std::uint8_t {
    Playing,
    Finished,
};

enum class GameSignal : std::uint8_t {
    None,
    QuitRequested,
    ResetRequested,
};

enum class ObstacleKind : std::uint8_t {
    RamStick,   // Short memory module, one hop.
    Capacitor,  // Squat electrolytic can.
    ErrorBox,   // Wide SEGFAULT dialog.
    Tower,      // Tall stack-overflow column.
    Cursor,     // Flying hostile cursor, duck or hop.
    BlueScreen, // Very wide panic window.
};

enum class PickupKind : std::uint8_t {
    Badge,     // +67 AURA, multiplied by the combo.
    Overclock, // Arms ROOT MODE for exactly 67 ticks.
};

enum class PopupKind : std::uint8_t {
    Aura,
    Clutch,
    Root,
    Wave,
};

struct RunnerState {
    std::int32_t y_fp;          // Foot line; kGroundY while grounded.
    std::int32_t velocity_y_fp; // Positive falls towards the ground.
    std::uint8_t on_ground;
    std::uint8_t flaps_used;
    std::uint8_t ducking;
    std::uint8_t duck_ticks; // Minimum duck hold, keeps firmware input usable.
    std::uint8_t stride;     // Leg animation phase.
};

struct ObstacleState {
    std::int32_t x_fp; // Left edge.
    std::int16_t y;    // Top edge, pixels.
    std::uint8_t width;
    std::uint8_t height;
    ObstacleKind kind;
    std::uint8_t active;
    std::uint8_t passed;  // Clutch bonus already resolved.
    std::uint8_t shatter; // Remaining ROOT MODE debris ticks.
};

struct PickupState {
    std::int32_t x_fp;
    std::int16_t y;
    PickupKind kind;
    std::uint8_t active;
};

struct PopupState {
    std::int32_t x_fp;
    std::int16_t y;
    std::int16_t value;
    PopupKind kind;
    std::uint8_t ticks;
};

struct DustState {
    std::int32_t x_fp;
    std::int32_t y_fp;
    std::int32_t velocity_x_fp;
    std::int32_t velocity_y_fp;
    std::uint8_t ticks;
};

// This is the complete mutable game state. It owns only fixed-size values and
// arrays, making snapshots cheap and firmware ports independent of a heap.
struct GameState {
    std::uint32_t seed;
    std::uint32_t rng_state;
    std::uint32_t tick;      // Ticks inside the current run.
    std::uint32_t run_index; // Runs completed since power-on.
    std::int32_t score;
    std::int32_t best_score;
    std::int32_t bonus_aura;          // Everything not earned by distance.
    std::uint32_t distance_px;        // Pixels scrolled in the current run.
    std::uint32_t distance_remainder; // Sub-pixel carry, always below 256.
    std::uint32_t scroll_fp;          // Free-running parallax phase.
    std::int32_t speed_fp;
    RunnerState runner;
    ObstacleState obstacles[kObstacleCount];
    PickupState pickups[kPickupCount];
    PopupState popups[kPopupCount];
    DustState dust[kDustCount];
    std::uint32_t escape_hold_ticks;
    std::uint16_t spawn_timer_ticks;
    std::uint16_t overclock_timer_ticks;
    std::uint16_t root_mode_ticks;
    std::uint16_t combo_ticks;
    std::uint16_t death_ticks;
    std::uint16_t flash_ticks;
    std::uint16_t badges_collected;
    std::uint16_t clutch_count;
    std::uint16_t obstacles_cleared;
    std::uint8_t combo;
    std::uint8_t palette_index;
    std::uint8_t maximum_brainrot;
    std::uint8_t record_beaten;
    std::uint16_t last_award_aura; // Bonus paid on the most recent award tick.
    GamePhase phase;
};

void game_initialize(GameState* game, std::uint32_t seed) noexcept;
GameSignal game_tick(GameState* game, const InputState& input) noexcept;
bool game_render(const GameState& game, const FrameBuffer& framebuffer) noexcept;

std::uint32_t game_run_seconds(const GameState& game) noexcept;
std::int32_t game_speed_pixels_per_second(const GameState& game) noexcept;
std::int32_t game_difficulty_q8(const GameState& game) noexcept;
std::uint64_t game_checksum(const GameState& game) noexcept;

constexpr std::int32_t pixels_to_fixed(std::int32_t pixels) noexcept {
    return pixels * kFixedOne;
}

constexpr std::int32_t fixed_to_pixels(std::int32_t fixed) noexcept {
    return fixed / kFixedOne;
}

} // namespace aura67
