#include "game/game.h"

#include "common/random.h"

#include <cstdint>
#include <cstring>

namespace aura67 {
namespace {

// Axis-aligned box in whole pixels. Everything that can hurt or reward the
// goose is resolved with these, so the rules stay readable and testable.
struct Box {
    std::int32_t left;
    std::int32_t top;
    std::int32_t right;
    std::int32_t bottom;
};

bool boxes_overlap(const Box& a, const Box& b) noexcept {
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

std::int32_t clamp_i32(std::int32_t value, std::int32_t minimum, std::int32_t maximum) noexcept {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

// Both operands are non-negative AURA totals, so a single compare keeps the
// score inside int32 for a run that never ends.
std::int32_t add_aura(std::int32_t total, std::int32_t gain) noexcept {
    if (gain > kAuraCeiling - total) {
        return kAuraCeiling;
    }
    return total + gain;
}

std::int32_t random_range(GameState* game, std::int32_t minimum, std::int32_t maximum) noexcept {
    const std::uint32_t span = static_cast<std::uint32_t>(maximum - minimum + 1);
    return minimum + static_cast<std::int32_t>(random_bounded(&game->rng_state, span));
}

void obstacle_metrics(ObstacleKind kind, std::int32_t* width, std::int32_t* height) noexcept {
    switch (kind) {
    case ObstacleKind::RamStick: *width = 24; *height = 46; break;
    case ObstacleKind::Capacitor: *width = 30; *height = 34; break;
    case ObstacleKind::ErrorBox: *width = 58; *height = 38; break;
    case ObstacleKind::Tower: *width = 28; *height = 66; break;
    case ObstacleKind::Cursor: *width = 26; *height = 26; break;
    case ObstacleKind::BlueScreen: *width = 68; *height = 44; break;
    }
}

Box runner_box(const GameState& game) noexcept {
    const std::int32_t feet = fixed_to_pixels(game.runner.y_fp);
    const std::int32_t height =
        game.runner.ducking != 0U ? kRunnerDuckHeight : kRunnerStandHeight;
    return Box{kRunnerX - kRunnerHalfWidth, feet - height, kRunnerX + kRunnerHalfWidth, feet};
}

Box obstacle_box(const ObstacleState& obstacle) noexcept {
    const std::int32_t left = fixed_to_pixels(obstacle.x_fp);
    const std::int32_t top = obstacle.y;
    return Box{left, top, left + obstacle.width, top + obstacle.height};
}

Box pickup_box(const PickupState& pickup) noexcept {
    const std::int32_t x = fixed_to_pixels(pickup.x_fp);
    const std::int32_t y = pickup.y;
    return Box{x - 12, y - 12, x + 12, y + 12};
}

ObstacleState* free_obstacle(GameState* game) noexcept {
    for (std::size_t index = 0U; index < kObstacleCount; ++index) {
        if (game->obstacles[index].active == 0U && game->obstacles[index].shatter == 0U) {
            return &game->obstacles[index];
        }
    }
    return nullptr;
}

PickupState* free_pickup(GameState* game) noexcept {
    for (std::size_t index = 0U; index < kPickupCount; ++index) {
        if (game->pickups[index].active == 0U) {
            return &game->pickups[index];
        }
    }
    return nullptr;
}

void spawn_popup(
    GameState* game,
    std::int32_t x,
    std::int32_t y,
    std::int32_t value,
    PopupKind kind) noexcept {
    PopupState* chosen = &game->popups[0];
    for (std::size_t index = 0U; index < kPopupCount; ++index) {
        if (game->popups[index].ticks == 0U) {
            chosen = &game->popups[index];
            break;
        }
        if (game->popups[index].ticks < chosen->ticks) {
            chosen = &game->popups[index];
        }
    }
    chosen->x_fp = pixels_to_fixed(x);
    chosen->y = static_cast<std::int16_t>(clamp_i32(y, 60, 330));
    chosen->value = static_cast<std::int16_t>(clamp_i32(value, -9999, 9999));
    chosen->kind = kind;
    chosen->ticks = 34U;
}

void spawn_dust(
    GameState* game,
    std::int32_t x,
    std::int32_t y,
    std::int32_t count,
    std::int32_t spread) noexcept {
    for (std::int32_t emitted = 0; emitted < count; ++emitted) {
        DustState* chosen = &game->dust[0];
        for (std::size_t index = 0U; index < kDustCount; ++index) {
            if (game->dust[index].ticks == 0U) {
                chosen = &game->dust[index];
                break;
            }
            if (game->dust[index].ticks < chosen->ticks) {
                chosen = &game->dust[index];
            }
        }
        chosen->x_fp = pixels_to_fixed(x);
        chosen->y_fp = pixels_to_fixed(y);
        chosen->velocity_x_fp = random_range(game, -spread, spread);
        chosen->velocity_y_fp = random_range(game, -spread - 180, 40);
        chosen->ticks = static_cast<std::uint8_t>(random_range(game, 12, 22));
    }
}

void bump_combo(GameState* game) noexcept {
    if (game->combo < kMaximumCombo) {
        ++game->combo;
    }
    game->combo_ticks = kComboDecayTicks;
}

void award_aura(
    GameState* game,
    std::int32_t base,
    PopupKind kind,
    std::int32_t x,
    std::int32_t y) noexcept {
    const std::int32_t gain = base * static_cast<std::int32_t>(game->combo);
    game->bonus_aura = add_aura(game->bonus_aura, gain);
    game->last_award_aura = static_cast<std::uint16_t>(gain);
    spawn_popup(game, x, y, gain, kind);
}

// Difficulty is the normalised scroll-speed ramp in Q0.8: 0 at the first step
// of a run, 256 once the speed cap is reached. Everything that gets nastier
// over time reads this single value.
std::int32_t difficulty_from_speed(std::int32_t speed_fp) noexcept {
    const std::int32_t span = kMaximumSpeedFp - kStartSpeedFp;
    const std::int32_t progressed = speed_fp - kStartSpeedFp;
    if (progressed <= 0) {
        return 0;
    }
    if (progressed >= span) {
        return 256;
    }
    return progressed * 256 / span;
}

std::int32_t next_spawn_interval(GameState* game, std::int32_t difficulty) noexcept {
    const std::int32_t base = 58 - (22 * difficulty) / 256;
    const std::int32_t spread = 16 - (8 * difficulty) / 256;
    const std::int32_t interval = base + random_range(game, 0, spread);
    return interval < kMinimumSpawnTicks ? kMinimumSpawnTicks : interval;
}

ObstacleState* place_obstacle(
    GameState* game,
    ObstacleKind kind,
    std::int32_t x,
    std::int32_t y) noexcept {
    ObstacleState* obstacle = free_obstacle(game);
    if (obstacle == nullptr) {
        return nullptr;
    }
    std::int32_t width = 0;
    std::int32_t height = 0;
    obstacle_metrics(kind, &width, &height);
    obstacle->x_fp = pixels_to_fixed(x);
    obstacle->y = static_cast<std::int16_t>(y);
    obstacle->width = static_cast<std::uint8_t>(width);
    obstacle->height = static_cast<std::uint8_t>(height);
    obstacle->kind = kind;
    obstacle->active = 1U;
    obstacle->passed = 0U;
    obstacle->shatter = 0U;
    return obstacle;
}

ObstacleState* place_ground_obstacle(GameState* game, ObstacleKind kind, std::int32_t x) noexcept {
    std::int32_t width = 0;
    std::int32_t height = 0;
    obstacle_metrics(kind, &width, &height);
    return place_obstacle(game, kind, x, kGroundY - height);
}

// A trio of badges tracing the jump the obstacle already demands: clearing the
// hazard well is what pays, not a detour.
void place_badge_arc(GameState* game, std::int32_t center_x) noexcept {
    static constexpr std::int32_t kOffsets[3] = {-42, 0, 42};
    static constexpr std::int32_t kHeights[3] = {88, 106, 88};
    for (std::size_t index = 0U; index < 3U; ++index) {
        PickupState* pickup = free_pickup(game);
        if (pickup == nullptr) {
            return;
        }
        pickup->x_fp = pixels_to_fixed(center_x + kOffsets[index]);
        pickup->y = static_cast<std::int16_t>(kGroundY - kHeights[index]);
        pickup->kind = PickupKind::Badge;
        pickup->active = 1U;
    }
}

ObstacleKind roll_ground_kind(GameState* game, std::int32_t difficulty) noexcept {
    const std::int32_t roll = random_range(game, 0, 99);
    if (difficulty >= 90 && roll >= 82) {
        return ObstacleKind::Tower;
    }
    if (roll >= 58) {
        return ObstacleKind::ErrorBox;
    }
    if (roll >= 30) {
        return ObstacleKind::Capacitor;
    }
    return ObstacleKind::RamStick;
}

// One spawn event lays down one clearable wave. Patterns unlock as the ramp
// climbs, so the first half-minute teaches jumps before it asks for ducks.
void spawn_wave(GameState* game, std::int32_t difficulty, std::int32_t interval) noexcept {
    const std::int32_t roll = random_range(game, 0, 99);
    const bool allow_cluster = interval >= 42;

    if (difficulty >= 150 && roll >= 88) {
        place_ground_obstacle(game, ObstacleKind::BlueScreen, kSpawnX);
        return;
    }
    if (difficulty >= 110 && roll >= 76) {
        // Flies at head height for a jumping goose: staying grounded is safe.
        place_obstacle(game, ObstacleKind::Cursor, kSpawnX, kGroundY - 106);
        return;
    }
    if (difficulty >= 70 && roll >= 62 && allow_cluster) {
        place_ground_obstacle(game, ObstacleKind::RamStick, kSpawnX);
        place_ground_obstacle(game, ObstacleKind::RamStick, kSpawnX + 60);
        if (random_range(game, 0, 99) < 40) {
            place_badge_arc(game, kSpawnX + 42);
        }
        return;
    }
    if (difficulty >= 26 && roll >= 44) {
        // Duck under it, or clear it with a full jump.
        place_obstacle(game, ObstacleKind::Cursor, kSpawnX, kGroundY - 52);
        return;
    }

    const ObstacleKind kind = roll_ground_kind(game, difficulty);
    std::int32_t width = 0;
    std::int32_t height = 0;
    obstacle_metrics(kind, &width, &height);
    place_ground_obstacle(game, kind, kSpawnX);
    if (random_range(game, 0, 99) < 40) {
        place_badge_arc(game, kSpawnX + width / 2);
    }
}

void update_spawner(GameState* game) noexcept {
    if (game->spawn_timer_ticks != 0U) {
        --game->spawn_timer_ticks;
        return;
    }

    const std::int32_t difficulty = difficulty_from_speed(game->speed_fp);
    const std::int32_t interval = next_spawn_interval(game, difficulty);
    spawn_wave(game, difficulty, interval);
    game->spawn_timer_ticks = static_cast<std::uint16_t>(interval);

    if (game->overclock_timer_ticks == 0U) {
        PickupState* pickup = free_pickup(game);
        if (pickup != nullptr) {
            // Drop the chip in the middle of the gap that was just rolled, so
            // it never shares its lane with the next wave.
            pickup->x_fp = pixels_to_fixed(kSpawnX) + (interval / 2) * game->speed_fp;
            pickup->y = static_cast<std::int16_t>(kGroundY - 74);
            pickup->kind = PickupKind::Overclock;
            pickup->active = 1U;
        }
        game->overclock_timer_ticks = static_cast<std::uint16_t>(random_range(game, 620, 980));
    }
}

void update_runner(GameState* game, const InputState& input) noexcept {
    RunnerState& runner = game->runner;
    const bool jump_pressed =
        input.was_pressed(InputButton::Dash) || input.was_pressed(InputButton::Up);
    const bool duck_held = input.is_held(InputButton::Down);

    if (jump_pressed) {
        if (runner.on_ground != 0U) {
            runner.velocity_y_fp = -kJumpVelocityFp;
            runner.on_ground = 0U;
            runner.flaps_used = 0U;
            runner.ducking = 0U;
            runner.duck_ticks = 0U;
            spawn_dust(game, kRunnerX - 14, kGroundY - 2, 3, 200);
        } else if (runner.flaps_used == 0U) {
            // One wing flap per airtime: recoverable, but not a free ceiling.
            runner.velocity_y_fp = -kFlapVelocityFp;
            runner.flaps_used = 1U;
            spawn_dust(game, kRunnerX - 18, fixed_to_pixels(runner.y_fp) - 26, 3, 260);
        }
    }

    if (runner.on_ground == 0U) {
        runner.ducking = 0U;
        runner.duck_ticks = 0U;
        if (duck_held && runner.velocity_y_fp < kFastFallVelocityFp) {
            runner.velocity_y_fp = kFastFallVelocityFp; // Dive back to the deck.
        }
    } else if (duck_held) {
        runner.ducking = 1U;
        runner.duck_ticks = 6U; // Bridges firmware key-repeat gaps.
    } else if (runner.duck_ticks != 0U) {
        --runner.duck_ticks;
    } else {
        runner.ducking = 0U;
    }

    runner.velocity_y_fp += kGravityFp;
    runner.y_fp += runner.velocity_y_fp;

    const std::int32_t ground_fp = pixels_to_fixed(kGroundY);
    if (runner.y_fp >= ground_fp) {
        if (runner.on_ground == 0U) {
            spawn_dust(game, kRunnerX - 10, kGroundY - 2, 4, 240);
        }
        runner.y_fp = ground_fp;
        runner.velocity_y_fp = 0;
        runner.on_ground = 1U;
        runner.flaps_used = 0U;
    } else {
        runner.on_ground = 0U;
    }

    if (runner.on_ground != 0U && runner.ducking == 0U && (game->tick % 6U) == 0U) {
        spawn_dust(game, kRunnerX - 16, kGroundY - 1, 1, 120);
    }
    runner.stride = static_cast<std::uint8_t>((game->tick / 3U) & 3U);
}

void scroll_world(GameState* game) noexcept {
    const std::int32_t step = game->speed_fp;

    for (std::size_t index = 0U; index < kObstacleCount; ++index) {
        ObstacleState& obstacle = game->obstacles[index];
        if (obstacle.active == 0U && obstacle.shatter == 0U) {
            continue;
        }
        obstacle.x_fp -= step;
        if (obstacle.shatter != 0U) {
            --obstacle.shatter;
        }
        if (fixed_to_pixels(obstacle.x_fp) + obstacle.width < kDespawnX) {
            obstacle.active = 0U;
            obstacle.shatter = 0U;
        }
    }

    for (std::size_t index = 0U; index < kPickupCount; ++index) {
        PickupState& pickup = game->pickups[index];
        if (pickup.active == 0U) {
            continue;
        }
        pickup.x_fp -= step;
        if (fixed_to_pixels(pickup.x_fp) < kDespawnX) {
            pickup.active = 0U;
        }
    }

    for (std::size_t index = 0U; index < kPopupCount; ++index) {
        PopupState& popup = game->popups[index];
        if (popup.ticks == 0U) {
            continue;
        }
        // Awards belong to a spot on the track and drift away with it;
        // announcements are addressed to the player and hold their place.
        if (popup.kind == PopupKind::Aura || popup.kind == PopupKind::Clutch) {
            popup.x_fp -= step / 2;
        }
        popup.y = static_cast<std::int16_t>(popup.y - 1);
        --popup.ticks;
    }

    for (std::size_t index = 0U; index < kDustCount; ++index) {
        DustState& particle = game->dust[index];
        if (particle.ticks == 0U) {
            continue;
        }
        particle.velocity_y_fp += 46;
        particle.x_fp += particle.velocity_x_fp - step;
        particle.y_fp += particle.velocity_y_fp;
        --particle.ticks;
    }
}

void shatter_obstacle(GameState* game, ObstacleState* obstacle) noexcept {
    const Box box = obstacle_box(*obstacle);
    obstacle->active = 0U;
    obstacle->passed = 1U;
    obstacle->shatter = 10U;
    ++game->obstacles_cleared;
    spawn_dust(game, (box.left + box.right) / 2, (box.top + box.bottom) / 2, 5, 420);
    award_aura(game, kShatterAura, PopupKind::Aura, box.left, box.top - 8);
    bump_combo(game);
}

void end_run(GameState* game) noexcept {
    game->phase = GamePhase::Finished;
    game->death_ticks = 0U;
    if (game->score > game->best_score) {
        game->best_score = game->score;
        game->record_beaten = 1U;
    }
    spawn_dust(game, kRunnerX, fixed_to_pixels(game->runner.y_fp) - 20, 8, 520);
}

void resolve_obstacles(GameState* game) noexcept {
    const Box runner = runner_box(*game);
    for (std::size_t index = 0U; index < kObstacleCount; ++index) {
        ObstacleState& obstacle = game->obstacles[index];
        if (obstacle.active == 0U) {
            continue;
        }
        const Box box = obstacle_box(obstacle);

        if (boxes_overlap(runner, box)) {
            if (game->root_mode_ticks != 0U) {
                shatter_obstacle(game, &obstacle);
                continue;
            }
            end_run(game);
            return;
        }

        if (obstacle.passed == 0U && box.right < runner.left) {
            obstacle.passed = 1U;
            ++game->obstacles_cleared;
            const std::int32_t above = box.top - runner.bottom;
            const std::int32_t below = runner.top - box.bottom;
            const std::int32_t clearance = above >= 0 ? above : (below >= 0 ? below : -1);
            if (clearance >= 0 && clearance <= 14) {
                ++game->clutch_count;
                award_aura(game, kClutchAura, PopupKind::Clutch, kRunnerX - 30, runner.top - 12);
                bump_combo(game);
            }
        }
    }
}

void resolve_pickups(GameState* game) noexcept {
    const Box runner = runner_box(*game);
    for (std::size_t index = 0U; index < kPickupCount; ++index) {
        PickupState& pickup = game->pickups[index];
        if (pickup.active == 0U || !boxes_overlap(runner, pickup_box(pickup))) {
            continue;
        }
        const std::int32_t x = fixed_to_pixels(pickup.x_fp);
        const std::int32_t y = pickup.y;
        pickup.active = 0U;

        if (pickup.kind == PickupKind::Overclock) {
            game->root_mode_ticks = kRootModeTicks;
            game->flash_ticks = 10U;
            spawn_popup(game, x, y - 16, 0, PopupKind::Root);
            spawn_dust(game, x, y, 6, 380);
            bump_combo(game);
            continue;
        }

        // Badges pay the current multiplier but never build it: the chain is
        // earned by risk (clutch passes and ROOT MODE smashes), not by pickups
        // that happen to sit on the jump the obstacle already demanded.
        ++game->badges_collected;
        award_aura(game, kBadgeAura, PopupKind::Aura, x, y - 10);
    }
}

void update_score(GameState* game) noexcept {
    game->distance_remainder += static_cast<std::uint32_t>(game->speed_fp);
    const std::uint32_t travelled = game->distance_remainder >> 8U;
    game->distance_remainder &= 0xFFU;
    if (game->distance_px < 0xF0000000U) {
        game->distance_px += travelled;
    }
    game->scroll_fp += static_cast<std::uint32_t>(game->speed_fp);

    const std::int32_t from_distance =
        static_cast<std::int32_t>(game->distance_px / static_cast<std::uint32_t>(kPixelsPerAura));
    const std::int32_t previous = game->score;
    game->score = add_aura(from_distance, game->bonus_aura);

    if (game->score >= kBrainrotAura) {
        game->maximum_brainrot = 1U;
    }

    // The firmware palette flips every 670 AURA. It is this game's day/night
    // cycle: pure feedback, no rule attached.
    const std::uint8_t wave =
        static_cast<std::uint8_t>((game->score / kPaletteWaveAura) & 3);
    if (wave != game->palette_index && game->score > previous) {
        game->palette_index = wave;
        game->flash_ticks = 12U;
        spawn_popup(game, 320, kGroundY - 150, 0, PopupKind::Wave);
    }
}

void start_run(GameState* game) noexcept {
    game->tick = 0U;
    game->score = 0;
    game->bonus_aura = 0;
    game->distance_px = 0U;
    game->distance_remainder = 0U;
    game->speed_fp = kStartSpeedFp;

    std::memset(game->obstacles, 0, sizeof(game->obstacles));
    std::memset(game->pickups, 0, sizeof(game->pickups));
    std::memset(game->popups, 0, sizeof(game->popups));
    std::memset(game->dust, 0, sizeof(game->dust));

    game->runner.y_fp = pixels_to_fixed(kGroundY);
    game->runner.velocity_y_fp = 0;
    game->runner.on_ground = 1U;
    game->runner.flaps_used = 0U;
    game->runner.ducking = 0U;
    game->runner.duck_ticks = 0U;
    game->runner.stride = 0U;

    game->spawn_timer_ticks = static_cast<std::uint16_t>(kOpeningGraceTicks);
    game->overclock_timer_ticks = static_cast<std::uint16_t>(random_range(game, 620, 980));
    game->root_mode_ticks = 0U;
    game->combo_ticks = 0U;
    game->death_ticks = 0U;
    game->flash_ticks = 0U;
    game->badges_collected = 0U;
    game->clutch_count = 0U;
    game->obstacles_cleared = 0U;
    game->combo = 1U;
    game->palette_index = 0U;
    game->maximum_brainrot = 0U;
    game->record_beaten = 0U;
    game->last_award_aura = 0U;
    game->phase = GamePhase::Playing;
}

void hash_u32(std::uint64_t* hash, std::uint32_t value) noexcept {
    constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
        *hash ^= static_cast<std::uint8_t>(value >> shift);
        *hash *= kFnvPrime;
    }
}

} // namespace

void game_initialize(GameState* game, std::uint32_t seed) noexcept {
    if (game == nullptr) {
        return;
    }
    std::memset(game, 0, sizeof(*game));
    game->seed = seed;
    game->rng_state = random_seed(seed);
    game->best_score = 0;
    game->run_index = 0U;
    game->escape_hold_ticks = 0U;
    start_run(game);
}

GameSignal game_tick(GameState* game, const InputState& input) noexcept {
    if (game == nullptr) {
        return GameSignal::None;
    }

    if (input.is_held(InputButton::Escape)) {
        ++game->escape_hold_ticks;
        if (game->escape_hold_ticks >= kTickRate * 2U) {
            return GameSignal::QuitRequested;
        }
    } else {
        game->escape_hold_ticks = 0U;
    }

    if (game->phase == GamePhase::Finished) {
        if (game->death_ticks < 0xFFFFU) {
            ++game->death_ticks;
        }
        if (input.was_pressed(InputButton::Reset)) {
            // The platform decides whether this request has any meaning. The
            // Win32 preview deliberately ignores it and never reboots Windows.
            return GameSignal::ResetRequested;
        }
        // A short lockout keeps the panic screen from being skipped by the
        // very keypress that caused the crash.
        const bool restart = input.was_pressed(InputButton::Confirm) ||
                             input.was_pressed(InputButton::Dash) ||
                             input.was_pressed(InputButton::Up);
        if (restart && game->death_ticks >= 20U) {
            // The generator is never re-seeded, so every run of a session is a
            // fresh course while the whole session stays reproducible.
            ++game->run_index;
            start_run(game);
        }
        return GameSignal::None;
    }

    if (game->root_mode_ticks != 0U) {
        --game->root_mode_ticks;
    }
    if (game->combo_ticks != 0U) {
        --game->combo_ticks;
        if (game->combo_ticks == 0U) {
            game->combo = 1U;
        }
    }
    if (game->flash_ticks != 0U) {
        --game->flash_ticks;
    }
    if (game->overclock_timer_ticks != 0U) {
        --game->overclock_timer_ticks;
    }
    game->last_award_aura = 0U;

    if (game->speed_fp < kMaximumSpeedFp) {
        game->speed_fp += kSpeedGainFp;
    }

    update_runner(game, input);
    scroll_world(game);
    update_spawner(game);
    resolve_obstacles(game);
    if (game->phase == GamePhase::Finished) {
        return GameSignal::None;
    }
    resolve_pickups(game);
    update_score(game);

    ++game->tick;
    return GameSignal::None;
}

std::uint32_t game_run_seconds(const GameState& game) noexcept {
    return game.tick / kTickRate;
}

std::int32_t game_speed_pixels_per_second(const GameState& game) noexcept {
    return fixed_to_pixels(game.speed_fp * static_cast<std::int32_t>(kTickRate));
}

std::int32_t game_difficulty_q8(const GameState& game) noexcept {
    return difficulty_from_speed(game.speed_fp);
}

std::uint64_t game_checksum(const GameState& game) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    hash_u32(&hash, game.seed);
    hash_u32(&hash, game.rng_state);
    hash_u32(&hash, game.tick);
    hash_u32(&hash, game.run_index);
    hash_u32(&hash, static_cast<std::uint32_t>(game.score));
    hash_u32(&hash, static_cast<std::uint32_t>(game.best_score));
    hash_u32(&hash, static_cast<std::uint32_t>(game.bonus_aura));
    hash_u32(&hash, game.distance_px);
    hash_u32(&hash, game.distance_remainder);
    hash_u32(&hash, game.scroll_fp);
    hash_u32(&hash, static_cast<std::uint32_t>(game.speed_fp));
    hash_u32(&hash, static_cast<std::uint32_t>(game.runner.y_fp));
    hash_u32(&hash, static_cast<std::uint32_t>(game.runner.velocity_y_fp));
    hash_u32(&hash, game.runner.on_ground);
    hash_u32(&hash, game.runner.flaps_used);
    hash_u32(&hash, game.runner.ducking);
    hash_u32(&hash, game.runner.duck_ticks);

    for (std::size_t index = 0U; index < kObstacleCount; ++index) {
        const ObstacleState& obstacle = game.obstacles[index];
        hash_u32(&hash, static_cast<std::uint32_t>(obstacle.x_fp));
        hash_u32(&hash, static_cast<std::uint32_t>(obstacle.y));
        hash_u32(&hash, obstacle.width);
        hash_u32(&hash, obstacle.height);
        hash_u32(&hash, static_cast<std::uint32_t>(obstacle.kind));
        hash_u32(&hash, obstacle.active);
        hash_u32(&hash, obstacle.passed);
        hash_u32(&hash, obstacle.shatter);
    }
    for (std::size_t index = 0U; index < kPickupCount; ++index) {
        const PickupState& pickup = game.pickups[index];
        hash_u32(&hash, static_cast<std::uint32_t>(pickup.x_fp));
        hash_u32(&hash, static_cast<std::uint32_t>(pickup.y));
        hash_u32(&hash, static_cast<std::uint32_t>(pickup.kind));
        hash_u32(&hash, pickup.active);
    }
    for (std::size_t index = 0U; index < kPopupCount; ++index) {
        const PopupState& popup = game.popups[index];
        hash_u32(&hash, static_cast<std::uint32_t>(popup.x_fp));
        hash_u32(&hash, static_cast<std::uint32_t>(popup.y));
        hash_u32(&hash, static_cast<std::uint32_t>(popup.value));
        hash_u32(&hash, static_cast<std::uint32_t>(popup.kind));
        hash_u32(&hash, popup.ticks);
    }
    for (std::size_t index = 0U; index < kDustCount; ++index) {
        const DustState& particle = game.dust[index];
        hash_u32(&hash, static_cast<std::uint32_t>(particle.x_fp));
        hash_u32(&hash, static_cast<std::uint32_t>(particle.y_fp));
        hash_u32(&hash, particle.ticks);
    }

    hash_u32(&hash, game.escape_hold_ticks);
    hash_u32(&hash, game.spawn_timer_ticks);
    hash_u32(&hash, game.overclock_timer_ticks);
    hash_u32(&hash, game.root_mode_ticks);
    hash_u32(&hash, game.combo_ticks);
    hash_u32(&hash, game.death_ticks);
    hash_u32(&hash, game.badges_collected);
    hash_u32(&hash, game.clutch_count);
    hash_u32(&hash, game.obstacles_cleared);
    hash_u32(&hash, game.combo);
    hash_u32(&hash, game.palette_index);
    hash_u32(&hash, game.maximum_brainrot);
    hash_u32(&hash, game.record_beaten);
    hash_u32(&hash, game.last_award_aura);
    hash_u32(&hash, static_cast<std::uint32_t>(game.phase));
    return hash;
}

} // namespace aura67
