#include "game/game.h"

#include "common/random.h"

#include <cstdint>
#include <cstring>

namespace aura67 {
namespace {

constexpr std::int32_t kPlayerMinX = 30;
constexpr std::int32_t kPlayerMaxX = static_cast<std::int32_t>(kFrameWidth) - 31;
constexpr std::int32_t kPlayerMinY = 68;
constexpr std::int32_t kPlayerMaxY = static_cast<std::int32_t>(kFrameHeight) - 31;
constexpr std::int32_t kEntityMinX = 18;
constexpr std::int32_t kEntityMaxX = static_cast<std::int32_t>(kFrameWidth) - 19;
constexpr std::int32_t kEntityMinY = 64;
constexpr std::int32_t kEntityMaxY = static_cast<std::int32_t>(kFrameHeight) - 19;
constexpr std::int32_t kWalkSpeedFpPerTick = 683; // 80 pixels/second at 30 Hz.
constexpr std::int32_t kWalkAccelerationFpPerTick = 370; // 1300 pixels/second^2 at 30 Hz.
constexpr std::int32_t kDiagonalScale = 181; // Approximately 1/sqrt(2), in Q8.
constexpr std::int32_t kBadgeCollisionRadius = 30;
constexpr std::int32_t kNpcCollisionRadius = 35;
constexpr std::int32_t kCursorCollisionRadius = 27;
constexpr std::int32_t kCursorPushRadius = 110;

std::int32_t clamp_i32(std::int32_t value, std::int32_t minimum, std::int32_t maximum) noexcept {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

std::int32_t approach(std::int32_t value, std::int32_t target, std::int32_t amount) noexcept {
    if (value < target) {
        const std::int32_t advanced = value + amount;
        return advanced > target ? target : advanced;
    }
    if (value > target) {
        const std::int32_t advanced = value - amount;
        return advanced < target ? target : advanced;
    }
    return value;
}

std::int64_t distance_squared_pixels(
    std::int32_t a_x_fp,
    std::int32_t a_y_fp,
    std::int32_t b_x_fp,
    std::int32_t b_y_fp) noexcept {
    const std::int64_t dx = static_cast<std::int64_t>(fixed_to_pixels(a_x_fp - b_x_fp));
    const std::int64_t dy = static_cast<std::int64_t>(fixed_to_pixels(a_y_fp - b_y_fp));
    return dx * dx + dy * dy;
}

bool is_near_player(const GameState& game, std::int32_t x_fp, std::int32_t y_fp, std::int32_t radius) noexcept {
    return distance_squared_pixels(game.goose.x_fp, game.goose.y_fp, x_fp, y_fp) <=
           static_cast<std::int64_t>(radius) * radius;
}

std::int32_t random_pixel(GameState* game, std::int32_t minimum, std::int32_t maximum) noexcept {
    const std::uint32_t range = static_cast<std::uint32_t>(maximum - minimum + 1);
    return minimum + static_cast<std::int32_t>(random_bounded(&game->rng_state, range));
}

void place_badge(GameState* game, BadgeState* badge) noexcept {
    for (std::uint32_t attempt = 0U; attempt < 8U; ++attempt) {
        badge->x_fp = pixels_to_fixed(random_pixel(game, 28, static_cast<std::int32_t>(kFrameWidth) - 29));
        badge->y_fp = pixels_to_fixed(random_pixel(game, 72, static_cast<std::int32_t>(kFrameHeight) - 29));
        if (!is_near_player(*game, badge->x_fp, badge->y_fp, 72)) {
            break;
        }
    }
    badge->active = 1U;
}

void initialize_npc(GameState* game, NpcState* npc, std::size_t index) noexcept {
    for (std::uint32_t attempt = 0U; attempt < 8U; ++attempt) {
        npc->x_fp = pixels_to_fixed(random_pixel(game, 55, static_cast<std::int32_t>(kFrameWidth) - 56));
        npc->y_fp = pixels_to_fixed(random_pixel(game, 90, static_cast<std::int32_t>(kFrameHeight) - 36));
        if (!is_near_player(*game, npc->x_fp, npc->y_fp, 95)) {
            break;
        }
    }

    const std::int32_t speed = 192 + static_cast<std::int32_t>(index) * 24;
    const bool horizontal = (random_next(&game->rng_state) & 1U) == 0U;
    const std::int32_t sign = (random_next(&game->rng_state) & 1U) == 0U ? -1 : 1;
    npc->velocity_x_fp = horizontal ? sign * speed : sign * (speed / 3);
    npc->velocity_y_fp = horizontal ? sign * (speed / 4) : sign * speed;
    npc->active = 1U;
}

void choose_direction(const InputState& input, const GooseState& goose, std::int32_t* x, std::int32_t* y) noexcept {
    *x = (input.is_held(InputButton::Right) ? 1 : 0) - (input.is_held(InputButton::Left) ? 1 : 0);
    *y = (input.is_held(InputButton::Down) ? 1 : 0) - (input.is_held(InputButton::Up) ? 1 : 0);
    if (*x != 0 && *y != 0) {
        // Dashes are cardinal so their traveled distance is exactly 67 pixels.
        *y = 0;
    }
    if (*x == 0 && *y == 0) {
        switch (goose.facing) {
        case Facing::Right: *x = 1; break;
        case Facing::Down: *y = 1; break;
        case Facing::Left: *x = -1; break;
        case Facing::Up: *y = -1; break;
        }
    }
}

void update_facing(GooseState* goose, std::int32_t input_x, std::int32_t input_y) noexcept {
    if (input_x > 0) {
        goose->facing = Facing::Right;
    } else if (input_x < 0) {
        goose->facing = Facing::Left;
    } else if (input_y > 0) {
        goose->facing = Facing::Down;
    } else if (input_y < 0) {
        goose->facing = Facing::Up;
    }
}

void push_cursor_exactly_67(GameState* game) noexcept {
    CursorState& cursor = game->hostile_cursor;
    if (distance_squared_pixels(
            game->goose.x_fp,
            game->goose.y_fp,
            cursor.x_fp,
            cursor.y_fp) > static_cast<std::int64_t>(kCursorPushRadius) * kCursorPushRadius) {
        return;
    }

    const std::int32_t dx = fixed_to_pixels(cursor.x_fp - game->goose.x_fp);
    const std::int32_t dy = fixed_to_pixels(cursor.y_fp - game->goose.y_fp);
    bool horizontal = (dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy);
    std::int32_t direction = horizontal ? (dx < 0 ? -1 : 1) : (dy < 0 ? -1 : 1);
    if (direction == 0) {
        direction = 1;
    }

    if (horizontal) {
        const std::int32_t current = fixed_to_pixels(cursor.x_fp);
        if (current + direction * kDashPixels < kEntityMinX ||
            current + direction * kDashPixels > kEntityMaxX) {
            direction = -direction;
        }
        cursor.x_fp += pixels_to_fixed(direction * kDashPixels);
    } else {
        const std::int32_t current = fixed_to_pixels(cursor.y_fp);
        if (current + direction * kDashPixels < kEntityMinY ||
            current + direction * kDashPixels > kEntityMaxY) {
            direction = -direction;
        }
        cursor.y_fp += pixels_to_fixed(direction * kDashPixels);
    }
    cursor.stun_ticks = 30U;
    game->last_cursor_push_pixels = static_cast<std::uint8_t>(kDashPixels);
}

bool dash(GameState* game, const InputState& input) noexcept {
    if (!input.was_pressed(InputButton::Dash) || game->dash_cooldown_ticks != 0U) {
        return false;
    }

    std::int32_t direction_x = 0;
    std::int32_t direction_y = 0;
    choose_direction(input, game->goose, &direction_x, &direction_y);

    if (direction_x != 0) {
        const std::int32_t x = fixed_to_pixels(game->goose.x_fp);
        if (x + direction_x * kDashPixels < kPlayerMinX ||
            x + direction_x * kDashPixels > kPlayerMaxX) {
            direction_x = -direction_x;
        }
        game->goose.x_fp += pixels_to_fixed(direction_x * kDashPixels);
    } else {
        const std::int32_t y = fixed_to_pixels(game->goose.y_fp);
        if (y + direction_y * kDashPixels < kPlayerMinY ||
            y + direction_y * kDashPixels > kPlayerMaxY) {
            direction_y = -direction_y;
        }
        game->goose.y_fp += pixels_to_fixed(direction_y * kDashPixels);
    }

    update_facing(&game->goose, direction_x, direction_y);
    game->goose.velocity_x_fp = 0;
    game->goose.velocity_y_fp = 0;
    game->dash_cooldown_ticks = 10U;
    game->last_dash_pixels = static_cast<std::uint8_t>(kDashPixels);
    push_cursor_exactly_67(game);
    return true;
}

void update_player(GameState* game, const InputState& input) noexcept {
    game->last_dash_pixels = 0U;
    game->last_cursor_push_pixels = 0U;
    if (dash(game, input)) {
        return;
    }

    std::int32_t input_x = (input.is_held(InputButton::Right) ? 1 : 0) -
                           (input.is_held(InputButton::Left) ? 1 : 0);
    std::int32_t input_y = (input.is_held(InputButton::Down) ? 1 : 0) -
                           (input.is_held(InputButton::Up) ? 1 : 0);
    update_facing(&game->goose, input_x, input_y);

    std::int32_t desired_x = input_x * kWalkSpeedFpPerTick;
    std::int32_t desired_y = input_y * kWalkSpeedFpPerTick;
    if (input_x != 0 && input_y != 0) {
        desired_x = desired_x * kDiagonalScale / kFixedOne;
        desired_y = desired_y * kDiagonalScale / kFixedOne;
    }

    game->goose.velocity_x_fp = approach(
        game->goose.velocity_x_fp, desired_x, kWalkAccelerationFpPerTick);
    game->goose.velocity_y_fp = approach(
        game->goose.velocity_y_fp, desired_y, kWalkAccelerationFpPerTick);
    game->goose.x_fp += game->goose.velocity_x_fp;
    game->goose.y_fp += game->goose.velocity_y_fp;

    const std::int32_t bounded_x = clamp_i32(
        game->goose.x_fp,
        pixels_to_fixed(kPlayerMinX),
        pixels_to_fixed(kPlayerMaxX));
    const std::int32_t bounded_y = clamp_i32(
        game->goose.y_fp,
        pixels_to_fixed(kPlayerMinY),
        pixels_to_fixed(kPlayerMaxY));
    if (bounded_x != game->goose.x_fp) {
        game->goose.velocity_x_fp = 0;
    }
    if (bounded_y != game->goose.y_fp) {
        game->goose.velocity_y_fp = 0;
    }
    game->goose.x_fp = bounded_x;
    game->goose.y_fp = bounded_y;
}

void update_npcs(GameState* game) noexcept {
    for (std::size_t index = 0U; index < kNpcCount; ++index) {
        NpcState& npc = game->npcs[index];
        if (npc.active == 0U) {
            continue;
        }
        npc.x_fp += npc.velocity_x_fp;
        npc.y_fp += npc.velocity_y_fp;

        if (npc.x_fp < pixels_to_fixed(kEntityMinX) || npc.x_fp > pixels_to_fixed(kEntityMaxX)) {
            npc.velocity_x_fp = -npc.velocity_x_fp;
            npc.x_fp = clamp_i32(
                npc.x_fp, pixels_to_fixed(kEntityMinX), pixels_to_fixed(kEntityMaxX));
        }
        if (npc.y_fp < pixels_to_fixed(kEntityMinY) || npc.y_fp > pixels_to_fixed(kEntityMaxY)) {
            npc.velocity_y_fp = -npc.velocity_y_fp;
            npc.y_fp = clamp_i32(
                npc.y_fp, pixels_to_fixed(kEntityMinY), pixels_to_fixed(kEntityMaxY));
        }
    }
}

void update_hostile_cursor(GameState* game) noexcept {
    CursorState& cursor = game->hostile_cursor;
    if (cursor.stun_ticks != 0U) {
        --cursor.stun_ticks;
        return;
    }

    constexpr std::int32_t kCursorStep = 128; // Half a pixel per simulation tick.
    if (cursor.x_fp < game->goose.x_fp) {
        cursor.x_fp += kCursorStep;
    } else if (cursor.x_fp > game->goose.x_fp) {
        cursor.x_fp -= kCursorStep;
    }
    if (cursor.y_fp < game->goose.y_fp) {
        cursor.y_fp += kCursorStep;
    } else if (cursor.y_fp > game->goose.y_fp) {
        cursor.y_fp -= kCursorStep;
    }
}

void resolve_collisions(GameState* game) noexcept {
    for (std::size_t index = 0U; index < kBadgeCount; ++index) {
        BadgeState& badge = game->badges[index];
        if (badge.active != 0U && is_near_player(*game, badge.x_fp, badge.y_fp, kBadgeCollisionRadius)) {
            game->score += 9999;
            ++game->badges_collected;
            place_badge(game, &badge);
        }
    }

    if (game->damage_cooldown_ticks == 0U) {
        bool damaged = false;
        for (std::size_t index = 0U; index < kNpcCount && !damaged; ++index) {
            const NpcState& npc = game->npcs[index];
            damaged = npc.active != 0U &&
                      is_near_player(*game, npc.x_fp, npc.y_fp, kNpcCollisionRadius);
        }
        if (!damaged) {
            damaged = is_near_player(
                *game,
                game->hostile_cursor.x_fp,
                game->hostile_cursor.y_fp,
                kCursorCollisionRadius);
        }
        if (damaged) {
            game->score -= 67;
            ++game->npc_hits;
            game->damage_cooldown_ticks = 30U;
        }
    }

    game->maximum_brainrot = game->score >= 9998 ? 1U : 0U;
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
    game->score = -10000;
    game->goose.x_fp = pixels_to_fixed(320);
    game->goose.y_fp = pixels_to_fixed(250);
    game->goose.facing = Facing::Right;
    game->hostile_cursor.x_fp = pixels_to_fixed(67);
    game->hostile_cursor.y_fp = pixels_to_fixed(100);
    game->phase = GamePhase::Playing;

    for (std::size_t index = 0U; index < kBadgeCount; ++index) {
        place_badge(game, &game->badges[index]);
    }
    for (std::size_t index = 0U; index < kNpcCount; ++index) {
        initialize_npc(game, &game->npcs[index], index);
    }
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
        if (input.was_pressed(InputButton::Reset)) {
            // The platform decides whether this request has any meaning. The
            // Win32 preview deliberately ignores it and never reboots Windows.
            return GameSignal::ResetRequested;
        }
        if (input.was_pressed(InputButton::Confirm)) {
            const std::uint32_t seed = game->seed;
            game_initialize(game, seed);
        }
        return GameSignal::None;
    }

    if (game->dash_cooldown_ticks != 0U) {
        --game->dash_cooldown_ticks;
    }
    if (game->damage_cooldown_ticks != 0U) {
        --game->damage_cooldown_ticks;
    }

    update_player(game, input);
    update_npcs(game);
    update_hostile_cursor(game);
    resolve_collisions(game);

    ++game->tick;
    game->palette_index = static_cast<std::uint8_t>((game->tick / (kTickRate * 8U)) % 3U);
    if (game->tick >= kRoundTicks) {
        game->tick = kRoundTicks;
        game->phase = GamePhase::Finished;
    }
    return GameSignal::None;
}

std::uint32_t game_seconds_remaining(const GameState& game) noexcept {
    if (game.tick >= kRoundTicks) {
        return 0U;
    }
    const std::uint32_t ticks = kRoundTicks - game.tick;
    return (ticks + kTickRate - 1U) / kTickRate;
}

std::uint64_t game_checksum(const GameState& game) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    hash_u32(&hash, game.seed);
    hash_u32(&hash, game.rng_state);
    hash_u32(&hash, game.tick);
    hash_u32(&hash, static_cast<std::uint32_t>(game.score));
    hash_u32(&hash, static_cast<std::uint32_t>(game.goose.x_fp));
    hash_u32(&hash, static_cast<std::uint32_t>(game.goose.y_fp));
    hash_u32(&hash, static_cast<std::uint32_t>(game.goose.velocity_x_fp));
    hash_u32(&hash, static_cast<std::uint32_t>(game.goose.velocity_y_fp));
    hash_u32(&hash, static_cast<std::uint32_t>(game.goose.facing));
    for (std::size_t index = 0U; index < kBadgeCount; ++index) {
        hash_u32(&hash, static_cast<std::uint32_t>(game.badges[index].x_fp));
        hash_u32(&hash, static_cast<std::uint32_t>(game.badges[index].y_fp));
        hash_u32(&hash, game.badges[index].active);
    }
    for (std::size_t index = 0U; index < kNpcCount; ++index) {
        hash_u32(&hash, static_cast<std::uint32_t>(game.npcs[index].x_fp));
        hash_u32(&hash, static_cast<std::uint32_t>(game.npcs[index].y_fp));
        hash_u32(&hash, static_cast<std::uint32_t>(game.npcs[index].velocity_x_fp));
        hash_u32(&hash, static_cast<std::uint32_t>(game.npcs[index].velocity_y_fp));
        hash_u32(&hash, game.npcs[index].active);
    }
    hash_u32(&hash, static_cast<std::uint32_t>(game.hostile_cursor.x_fp));
    hash_u32(&hash, static_cast<std::uint32_t>(game.hostile_cursor.y_fp));
    hash_u32(&hash, game.hostile_cursor.stun_ticks);
    hash_u32(&hash, game.escape_hold_ticks);
    hash_u32(&hash, game.dash_cooldown_ticks);
    hash_u32(&hash, game.damage_cooldown_ticks);
    hash_u32(&hash, game.badges_collected);
    hash_u32(&hash, game.npc_hits);
    hash_u32(&hash, game.palette_index);
    hash_u32(&hash, game.maximum_brainrot);
    hash_u32(&hash, game.last_dash_pixels);
    hash_u32(&hash, game.last_cursor_push_pixels);
    hash_u32(&hash, static_cast<std::uint32_t>(game.phase));
    return hash;
}

} // namespace aura67

