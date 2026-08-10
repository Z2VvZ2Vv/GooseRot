#include "common/arena.h"
#include "common/render_policy.h"
#include "game/game.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>

namespace {

int g_failures = 0;

#define CHECK(expression)                                                                    \
    do {                                                                                     \
        if (!(expression)) {                                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression);                \
            ++g_failures;                                                                    \
        }                                                                                    \
    } while (false)

constexpr std::size_t kPixelCount =
    static_cast<std::size_t>(aura67::kFrameWidth) * aura67::kFrameHeight;
std::uint32_t g_render_a[kPixelCount + 2U]{};
std::uint32_t g_render_b[kPixelCount + 2U]{};

aura67::InputState press(aura67::InputButton button) noexcept {
    aura67::InputState input = aura67::no_input();
    input.held = aura67::input_mask(button);
    input.pressed = aura67::input_mask(button);
    return input;
}

aura67::InputState hold(aura67::InputButton button) noexcept {
    aura67::InputState input = aura67::no_input();
    input.held = aura67::input_mask(button);
    return input;
}

void run_ticks(aura67::GameState* game, std::uint32_t count) noexcept {
    for (std::uint32_t tick = 0U; tick < count; ++tick) {
        aura67::game_tick(game, aura67::no_input());
    }
}

void clear_hazards(aura67::GameState* game) noexcept {
    for (std::size_t index = 0U; index < aura67::kObstacleCount; ++index) {
        game->obstacles[index].active = 0U;
        game->obstacles[index].shatter = 0U;
    }
    for (std::size_t index = 0U; index < aura67::kPickupCount; ++index) {
        game->pickups[index].active = 0U;
    }
}

aura67::ObstacleState* place_test_obstacle(
    aura67::GameState* game,
    aura67::ObstacleKind kind,
    std::int32_t x,
    std::int32_t y,
    std::int32_t width,
    std::int32_t height) noexcept {
    clear_hazards(game);
    aura67::ObstacleState& obstacle = game->obstacles[0];
    obstacle.x_fp = aura67::pixels_to_fixed(x);
    obstacle.y = static_cast<std::int16_t>(y);
    obstacle.width = static_cast<std::uint8_t>(width);
    obstacle.height = static_cast<std::uint8_t>(height);
    obstacle.kind = kind;
    obstacle.active = 1U;
    obstacle.passed = 0U;
    obstacle.shatter = 0U;
    return &obstacle;
}

// A deliberately simple pilot: it only knows the nearest hazard, the current
// scroll speed and the fixed 25-tick jump. If the spawner ever rolls a wave
// this pilot cannot answer, the fairness test below stops surviving.
aura67::InputState autopilot(const aura67::GameState& game) noexcept {
    const std::int32_t speed_px = game.speed_fp / aura67::kFixedOne;
    const std::int32_t runner_right = aura67::kRunnerX + aura67::kRunnerHalfWidth;
    bool jump = false;
    bool duck = false;
    bool blocked_overhead = false;

    for (std::size_t index = 0U; index < aura67::kObstacleCount; ++index) {
        const aura67::ObstacleState& obstacle = game.obstacles[index];
        if (obstacle.active == 0U) {
            continue;
        }
        const std::int32_t left = aura67::fixed_to_pixels(obstacle.x_fp);
        const std::int32_t right = left + obstacle.width;
        if (right < aura67::kRunnerX - aura67::kRunnerHalfWidth) {
            continue;
        }
        const std::int32_t ticks_away = (left - runner_right) / (speed_px > 0 ? speed_px : 1);
        const bool grounded = obstacle.y + obstacle.height >= aura67::kGroundY - 2;

        if (!grounded && obstacle.y + obstacle.height < aura67::kGroundY - 70) {
            // High cursor: staying on the deck is the only safe answer.
            if (ticks_away <= 14) {
                blocked_overhead = true;
            }
            continue;
        }
        if (!grounded) {
            if (ticks_away <= 6 && left <= runner_right + 40) {
                duck = true;
            }
            continue;
        }
        if (ticks_away >= 4 && ticks_away <= 8) {
            jump = true;
        }
    }

    if (jump && !blocked_overhead && game.runner.on_ground != 0U) {
        return press(aura67::InputButton::Dash);
    }
    if (duck) {
        return hold(aura67::InputButton::Down);
    }
    return aura67::no_input();
}

aura67::InputState scripted_input(std::uint32_t tick) noexcept {
    aura67::InputState input = aura67::no_input();
    if (tick % 37U == 0U) {
        input.held |= aura67::input_mask(aura67::InputButton::Dash);
        input.pressed |= aura67::input_mask(aura67::InputButton::Dash);
    }
    if ((tick / 23U) % 5U == 0U) {
        input.held |= aura67::input_mask(aura67::InputButton::Down);
    }
    if (tick % 211U == 0U) {
        input.held |= aura67::input_mask(aura67::InputButton::Confirm);
        input.pressed |= aura67::input_mask(aura67::InputButton::Confirm);
    }
    return input;
}

void test_determinism() {
    aura67::GameState first{};
    aura67::GameState second{};
    aura67::GameState other_seed{};
    aura67::game_initialize(&first, 67U);
    aura67::game_initialize(&second, 67U);
    aura67::game_initialize(&other_seed, 68U);
    CHECK(aura67::game_checksum(first) == aura67::game_checksum(second));
    CHECK(aura67::game_checksum(first) != aura67::game_checksum(other_seed));

    for (std::uint32_t tick = 0U; tick < 4000U; ++tick) {
        const aura67::InputState input = scripted_input(tick);
        CHECK(aura67::game_tick(&first, input) == aura67::GameSignal::None);
        CHECK(aura67::game_tick(&second, input) == aura67::GameSignal::None);
        CHECK(aura67::game_checksum(first) == aura67::game_checksum(second));
    }
    // The script crashes and restarts several times, so this also proves that
    // a session made of many runs stays reproducible end to end.
    CHECK(first.run_index >= 2U);
    // Cross-toolchain golden checkpoint: the simulation hashes fields in a
    // specified byte order, so this value must also match BIOS/UEFI adapters.
    CHECK(aura67::game_checksum(first) == 9787892968645355273ULL);
}

void test_jump_arc_is_fixed() {
    aura67::GameState game{};
    aura67::game_initialize(&game, 67U);
    clear_hazards(&game);

    CHECK(game.runner.on_ground == 1U);
    CHECK(aura67::fixed_to_pixels(game.runner.y_fp) == aura67::kGroundY);

    std::int32_t apex = aura67::kGroundY;
    std::uint32_t airborne_ticks = 0U;
    aura67::game_tick(&game, press(aura67::InputButton::Dash));
    while (game.runner.on_ground == 0U && airborne_ticks < 200U) {
        const std::int32_t y = aura67::fixed_to_pixels(game.runner.y_fp);
        if (y < apex) {
            apex = y;
        }
        ++airborne_ticks;
        clear_hazards(&game);
        aura67::game_tick(&game, aura67::no_input());
    }

    CHECK(airborne_ticks == static_cast<std::uint32_t>(aura67::kJumpAirTicks) - 1U);
    CHECK(aura67::kGroundY - apex == aura67::kJumpApexPixels);
    CHECK(game.runner.on_ground == 1U);
    CHECK(aura67::fixed_to_pixels(game.runner.y_fp) == aura67::kGroundY);
}

void test_single_flap_per_airtime() {
    aura67::GameState game{};
    aura67::game_initialize(&game, 67U);
    clear_hazards(&game);

    aura67::game_tick(&game, press(aura67::InputButton::Dash));
    run_ticks(&game, 10U);
    CHECK(game.runner.on_ground == 0U);
    CHECK(game.runner.flaps_used == 0U);

    const std::int32_t before_flap = game.runner.velocity_y_fp;
    aura67::game_tick(&game, press(aura67::InputButton::Dash));
    CHECK(game.runner.flaps_used == 1U);
    CHECK(game.runner.velocity_y_fp < before_flap);

    const std::int32_t after_flap = game.runner.velocity_y_fp;
    aura67::game_tick(&game, press(aura67::InputButton::Dash));
    CHECK(game.runner.flaps_used == 1U);
    CHECK(game.runner.velocity_y_fp > after_flap); // Gravity only, no second flap.

    for (std::uint32_t tick = 0U; tick < 80U && game.runner.on_ground == 0U; ++tick) {
        clear_hazards(&game);
        aura67::game_tick(&game, aura67::no_input());
    }
    CHECK(game.runner.on_ground == 1U);
    CHECK(game.runner.flaps_used == 0U); // Landing refills the flap.
}

void test_duck_clears_the_flying_cursor() {
    aura67::GameState standing{};
    aura67::game_initialize(&standing, 67U);
    for (std::uint32_t tick = 0U; tick < 30U; ++tick) {
        place_test_obstacle(&standing, aura67::ObstacleKind::Cursor,
                            aura67::kRunnerX - 10, aura67::kGroundY - 52, 26, 26);
        aura67::game_tick(&standing, aura67::no_input());
    }
    CHECK(standing.phase == aura67::GamePhase::Finished);

    aura67::GameState ducking{};
    aura67::game_initialize(&ducking, 67U);
    for (std::uint32_t tick = 0U; tick < 30U; ++tick) {
        place_test_obstacle(&ducking, aura67::ObstacleKind::Cursor,
                            aura67::kRunnerX - 10, aura67::kGroundY - 52, 26, 26);
        aura67::game_tick(&ducking, hold(aura67::InputButton::Down));
    }
    CHECK(ducking.phase == aura67::GamePhase::Playing);
    CHECK(ducking.runner.ducking == 1U);
}

void test_ground_obstacle_ends_the_run() {
    aura67::GameState game{};
    aura67::game_initialize(&game, 67U);
    place_test_obstacle(&game, aura67::ObstacleKind::RamStick,
                        aura67::kRunnerX - 10, aura67::kGroundY - 46, 24, 46);
    aura67::game_tick(&game, aura67::no_input());
    CHECK(game.phase == aura67::GamePhase::Finished);
    CHECK(game.best_score == game.score);
    CHECK(game.record_beaten == 0U); // Crashing at zero AURA beats nothing.
}

void test_badge_pays_67_at_the_current_multiplier() {
    aura67::GameState game{};
    aura67::game_initialize(&game, 67U);
    clear_hazards(&game);
    CHECK(game.combo == 1U);

    game.pickups[0].x_fp = aura67::pixels_to_fixed(aura67::kRunnerX);
    game.pickups[0].y = static_cast<std::int16_t>(aura67::kGroundY - 20);
    game.pickups[0].kind = aura67::PickupKind::Badge;
    game.pickups[0].active = 1U;
    aura67::game_tick(&game, aura67::no_input());
    CHECK(game.badges_collected == 1U);
    CHECK(game.bonus_aura == aura67::kBadgeAura);
    CHECK(game.last_award_aura == static_cast<std::uint16_t>(aura67::kBadgeAura));
    CHECK(game.combo == 1U); // Badges spend the chain, they never build it.

    // With a chain running, the same badge pays the multiplier.
    clear_hazards(&game);
    game.combo = 4U;
    game.combo_ticks = aura67::kComboDecayTicks;
    game.pickups[1].x_fp = aura67::pixels_to_fixed(aura67::kRunnerX);
    game.pickups[1].y = static_cast<std::int16_t>(aura67::kGroundY - 20);
    game.pickups[1].kind = aura67::PickupKind::Badge;
    game.pickups[1].active = 1U;
    aura67::game_tick(&game, aura67::no_input());
    CHECK(game.bonus_aura == aura67::kBadgeAura * 5); // 67 then 67 x4.

    // The chain lapses on its own when no risk is taken.
    for (std::uint32_t tick = 0U; tick <= aura67::kComboDecayTicks; ++tick) {
        clear_hazards(&game);
        aura67::game_tick(&game, aura67::no_input());
    }
    CHECK(game.combo == 1U);
}

void test_root_mode_shatters_instead_of_killing() {
    aura67::GameState game{};
    aura67::game_initialize(&game, 67U);
    clear_hazards(&game);

    game.pickups[0].x_fp = aura67::pixels_to_fixed(aura67::kRunnerX);
    game.pickups[0].y = static_cast<std::int16_t>(aura67::kGroundY - 20);
    game.pickups[0].kind = aura67::PickupKind::Overclock;
    game.pickups[0].active = 1U;
    aura67::game_tick(&game, aura67::no_input());
    CHECK(game.root_mode_ticks == aura67::kRootModeTicks);
    CHECK(game.combo == 2U);

    const std::int32_t bonus_before = game.bonus_aura;
    place_test_obstacle(&game, aura67::ObstacleKind::Tower,
                        aura67::kRunnerX - 10, aura67::kGroundY - 66, 28, 66);
    aura67::game_tick(&game, aura67::no_input());
    CHECK(game.phase == aura67::GamePhase::Playing);
    CHECK(game.obstacles[0].active == 0U);
    CHECK(game.obstacles[0].shatter != 0U);
    CHECK(game.bonus_aura == bonus_before + aura67::kShatterAura * 2);

    // ROOT MODE lasts exactly 67 ticks and then stops protecting anything.
    for (std::uint32_t tick = 0U; tick < aura67::kRootModeTicks; ++tick) {
        clear_hazards(&game);
        aura67::game_tick(&game, aura67::no_input());
    }
    CHECK(game.root_mode_ticks == 0U);
    place_test_obstacle(&game, aura67::ObstacleKind::Tower,
                        aura67::kRunnerX - 10, aura67::kGroundY - 66, 28, 66);
    aura67::game_tick(&game, aura67::no_input());
    CHECK(game.phase == aura67::GamePhase::Finished);
}

void test_clutch_bonus_rewards_tight_jumps() {
    aura67::GameState game{};
    aura67::game_initialize(&game, 67U);
    clear_hazards(&game);

    // Park the goose just above a low obstacle that is about to pass behind.
    game.runner.y_fp = aura67::pixels_to_fixed(aura67::kGroundY - 40);
    game.runner.on_ground = 0U;
    place_test_obstacle(&game, aura67::ObstacleKind::Capacitor,
                        aura67::kRunnerX - aura67::kRunnerHalfWidth - 20,
                        aura67::kGroundY - 34, 30, 34);
    aura67::game_tick(&game, aura67::no_input());
    CHECK(game.clutch_count == 1U);
    CHECK(game.bonus_aura == aura67::kClutchAura);
    CHECK(game.obstacles_cleared == 1U);
    CHECK(game.combo == 2U); // Risk is what builds the multiplier.

    // A lazy, roomy jump over the same obstacle pays nothing extra.
    aura67::GameState roomy{};
    aura67::game_initialize(&roomy, 67U);
    clear_hazards(&roomy);
    roomy.runner.y_fp = aura67::pixels_to_fixed(aura67::kGroundY - 85);
    roomy.runner.on_ground = 0U;
    place_test_obstacle(&roomy, aura67::ObstacleKind::Capacitor,
                        aura67::kRunnerX - aura67::kRunnerHalfWidth - 20,
                        aura67::kGroundY - 34, 30, 34);
    aura67::game_tick(&roomy, aura67::no_input());
    CHECK(roomy.clutch_count == 0U);
    CHECK(roomy.bonus_aura == 0);
    CHECK(roomy.obstacles_cleared == 1U);
}

void test_run_never_ends_on_a_timer() {
    aura67::GameState game{};
    aura67::game_initialize(&game, 67U);

    // Ten thousand ticks is five times the old 67-second round.
    for (std::uint32_t tick = 0U; tick < 10000U; ++tick) {
        aura67::game_tick(&game, aura67::no_input());
        clear_hazards(&game);
        CHECK(game.phase == aura67::GamePhase::Playing);
    }
    CHECK(game.tick == 10000U);
    CHECK(game.speed_fp == aura67::kMaximumSpeedFp);
    CHECK(game.score > 20000);
    CHECK(aura67::game_run_seconds(game) == 10000U / aura67::kTickRate);
    CHECK(game.maximum_brainrot == 1U);
    CHECK(aura67::game_difficulty_q8(game) == 256);
    CHECK(aura67::game_speed_pixels_per_second(game) == 622);
}

void test_spawner_stays_clearable() {
    for (std::uint32_t seed = 1U; seed <= 6U; ++seed) {
        aura67::GameState game{};
        aura67::game_initialize(&game, seed * 9781U);
        std::uint16_t previous_timer = game.spawn_timer_ticks;
        std::uint32_t waves = 0U;

        for (std::uint32_t tick = 0U; tick < 12000U; ++tick) {
            aura67::game_tick(&game, aura67::no_input());
            clear_hazards(&game);
            if (game.spawn_timer_ticks > previous_timer) {
                // A wave was just rolled: check the gap it reserved.
                CHECK(game.spawn_timer_ticks >=
                      static_cast<std::uint16_t>(aura67::kMinimumSpawnTicks));
                ++waves;
            }
            previous_timer = game.spawn_timer_ticks;
        }
        CHECK(waves > 200U);
    }
}

void test_autopilot_survives_the_ramp() {
    // Fairness harness: an ordinary reactive player must be able to keep going
    // well past the old round length on every seed.
    for (std::uint32_t seed = 1U; seed <= 5U; ++seed) {
        aura67::GameState game{};
        aura67::game_initialize(&game, seed * 4409U + 67U);
        std::uint32_t survived = 0U;
        while (game.phase == aura67::GamePhase::Playing && survived < 6000U) {
            aura67::game_tick(&game, autopilot(game));
            ++survived;
        }
        if (survived < 3000U) {
            std::printf("  autopilot seed %u survived only %u ticks\n", seed, survived);
        }
        CHECK(survived >= 3000U);
    }
}

void test_death_restart_keeps_the_record() {
    aura67::GameState game{};
    aura67::game_initialize(&game, 67U);
    for (std::uint32_t tick = 0U; tick < 400U; ++tick) {
        aura67::game_tick(&game, aura67::no_input());
        clear_hazards(&game);
    }
    const std::int32_t reached = game.score;
    CHECK(reached > 0);

    place_test_obstacle(&game, aura67::ObstacleKind::RamStick,
                        aura67::kRunnerX - 10, aura67::kGroundY - 46, 24, 46);
    aura67::game_tick(&game, aura67::no_input());
    CHECK(game.phase == aura67::GamePhase::Finished);
    CHECK(game.best_score == reached);
    CHECK(game.record_beaten == 1U);

    // The panic screen ignores the keypress that came with the crash.
    CHECK(aura67::game_tick(&game, press(aura67::InputButton::Confirm)) ==
          aura67::GameSignal::None);
    CHECK(game.phase == aura67::GamePhase::Finished);
    run_ticks(&game, 25U);
    CHECK(aura67::game_tick(&game, press(aura67::InputButton::Confirm)) ==
          aura67::GameSignal::None);
    CHECK(game.phase == aura67::GamePhase::Playing);
    CHECK(game.score == 0);
    CHECK(game.best_score == reached);
    CHECK(game.run_index == 1U);
    CHECK(game.speed_fp == aura67::kStartSpeedFp);
    CHECK(game.seed == 67U);

    // A second session run differs from the first: the generator is never
    // re-seeded, so the course is fresh while the session stays reproducible.
    aura67::GameState replay{};
    aura67::game_initialize(&replay, 67U);
    CHECK(aura67::game_checksum(replay) != aura67::game_checksum(game));
}

void test_platform_reset_is_only_offered_after_a_crash() {
    aura67::GameState game{};
    aura67::game_initialize(&game, 67U);
    CHECK(aura67::game_tick(&game, press(aura67::InputButton::Reset)) ==
          aura67::GameSignal::None);
    CHECK(game.phase == aura67::GamePhase::Playing);

    place_test_obstacle(&game, aura67::ObstacleKind::RamStick,
                        aura67::kRunnerX - 10, aura67::kGroundY - 46, 24, 46);
    aura67::game_tick(&game, aura67::no_input());
    CHECK(game.phase == aura67::GamePhase::Finished);
    CHECK(aura67::game_tick(&game, press(aura67::InputButton::Reset)) ==
          aura67::GameSignal::ResetRequested);
}

void test_escape_hold() {
    aura67::GameState game{};
    aura67::game_initialize(&game, 67U);
    aura67::InputState input = hold(aura67::InputButton::Escape);
    for (std::uint32_t tick = 0U; tick < aura67::kTickRate * 2U - 1U; ++tick) {
        clear_hazards(&game);
        CHECK(aura67::game_tick(&game, input) == aura67::GameSignal::None);
    }
    CHECK(aura67::game_tick(&game, input) == aura67::GameSignal::QuitRequested);
}

void test_render_policy() {
    const aura67::PresentationSize exact =
        aura67::fit_frame_to_bounds(1280U, 720U);
    CHECK(exact.width == 1280U);
    CHECK(exact.height == 720U);

    const aura67::PresentationSize awkward =
        aura67::fit_frame_to_bounds(1279U, 719U);
    CHECK(awkward.width == 1278U);
    CHECK(awkward.height == 719U);

    const aura67::PresentationSize tall =
        aura67::fit_frame_to_bounds(800U, 600U);
    CHECK(tall.width == 800U);
    CHECK(tall.height == 450U);

    const aura67::PresentationSize empty =
        aura67::fit_frame_to_bounds(0U, 720U);
    CHECK(empty.width == 0U);
    CHECK(empty.height == 0U);

    const aura67::PresentationSize tiny =
        aura67::fit_frame_to_bounds(1U, 1U);
    CHECK(tiny.width == 1U);
    CHECK(tiny.height == 1U);

    const aura67::PresentationSize firmware =
        aura67::fit_frame_to_bounds(1024U, 768U);
    CHECK(firmware.width == 1024U);
    CHECK(firmware.height == 576U);
    const aura67::PresentationSize legacy_firmware =
        aura67::fit_frame_to_bounds(
            static_cast<std::uint16_t>(1024U),
            static_cast<std::uint16_t>(768U));
    CHECK(legacy_firmware.width == firmware.width);
    CHECK(legacy_firmware.height == firmware.height);

    const aura67::PresentationSize maximum =
        aura67::fit_frame_to_bounds(0xffffffffU, 0xffffffffU);
    constexpr std::uint32_t kExpectedMaximumHeight = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(0xffffffffU) * aura67::kFrameHeight /
        aura67::kFrameWidth);
    CHECK(maximum.width == 0xffffffffU);
    CHECK(maximum.height == kExpectedMaximumHeight);

    const aura67::PresentationSize bounded =
        aura67::fit_frame_to_bounded_surface(
            3840U,
            2160U,
            aura67::kMaximumSoftwarePresentationWidth,
            aura67::kMaximumSoftwarePresentationHeight);
    CHECK(bounded.width == 1920U);
    CHECK(bounded.height == 1080U);

    const std::uint32_t expected_upscale[] = {0U, 0U, 1U, 1U, 2U, 2U, 3U};
    aura67::NearestNeighborAxis upscale(4U, 7U);
    for (std::size_t index = 0U;
         index < sizeof(expected_upscale) / sizeof(expected_upscale[0]);
         ++index) {
        CHECK(upscale.source_index == expected_upscale[index]);
        upscale.advance();
    }

    const std::uint32_t expected_downscale[] = {0U, 2U, 4U, 6U};
    aura67::NearestNeighborAxis downscale(8U, 4U);
    for (std::size_t index = 0U;
         index < sizeof(expected_downscale) / sizeof(expected_downscale[0]);
         ++index) {
        CHECK(downscale.source_index == expected_downscale[index]);
        downscale.advance();
    }

    aura67::NearestNeighborAxis firmware_x(aura67::kFrameWidth, 1365U);
    for (std::uint32_t destination_x = 0U; destination_x < 1365U;
         ++destination_x) {
        const std::uint32_t expected_source_x = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(destination_x) * aura67::kFrameWidth /
            1365U);
        CHECK(firmware_x.source_index == expected_source_x);
        firmware_x.advance();
    }

    constexpr std::uint64_t kSixteenGibibytes =
        16ULL * 1024ULL * 1024ULL * 1024ULL;
    constexpr std::uint64_t kTwelveGibibytes =
        12ULL * 1024ULL * 1024ULL * 1024ULL;
    constexpr std::uint64_t kEightGibibytes =
        8ULL * 1024ULL * 1024ULL * 1024ULL;
    CHECK(aura67::render_detail_for_machine(12U, kSixteenGibibytes) ==
          aura67::RenderDetail::Detailed);
    CHECK(aura67::render_detail_for_machine(12U, kTwelveGibibytes) ==
          aura67::RenderDetail::Detailed);
    CHECK(aura67::render_detail_for_machine(12U, kTwelveGibibytes - 1U) ==
          aura67::RenderDetail::Reduced);
    CHECK(aura67::render_detail_for_machine(8U, kSixteenGibibytes) ==
          aura67::RenderDetail::Reduced);
    CHECK(aura67::render_detail_for_machine(16U, kEightGibibytes) ==
          aura67::RenderDetail::Reduced);

    CHECK(aura67::render_detail_for_surface(1280U, 720U) ==
          aura67::RenderDetail::Detailed);
    CHECK(aura67::render_detail_for_surface(1281U, 720U) ==
          aura67::RenderDetail::Reduced);
    CHECK(aura67::render_detail_for_surface(1920U, 1080U) ==
          aura67::RenderDetail::Reduced);

    CHECK(aura67::render_detail_for_machine_and_surface(
              12U, kSixteenGibibytes, 1280U, 720U) ==
          aura67::RenderDetail::Detailed);
    CHECK(aura67::render_detail_for_machine_and_surface(
              8U, kSixteenGibibytes, 1280U, 720U) ==
          aura67::RenderDetail::Reduced);
    CHECK(aura67::render_detail_for_machine_and_surface(
              12U, kSixteenGibibytes, 1920U, 1080U) ==
          aura67::RenderDetail::Reduced);
}

void test_reduced_render_keeps_gameplay_visible() {
    aura67::GameState game{};
    aura67::game_initialize(&game, 67U);
    game.tick = 100U;
    place_test_obstacle(&game, aura67::ObstacleKind::RamStick, 400, 254, 24, 46);
    game.pickups[0].x_fp = aura67::pixels_to_fixed(300);
    game.pickups[0].y = 210;
    game.pickups[0].kind = aura67::PickupKind::Badge;
    game.pickups[0].active = 1U;
    game.popups[0].x_fp = aura67::pixels_to_fixed(220);
    game.popups[0].y = 90;
    game.popups[0].value = 67;
    game.popups[0].kind = aura67::PopupKind::Clutch;
    game.popups[0].ticks = 10U;

    g_render_a[0] = 0x11223344U;
    g_render_a[kPixelCount + 1U] = 0x55667788U;
    g_render_b[0] = 0x99AABBCCU;
    g_render_b[kPixelCount + 1U] = 0xDDEEFF00U;
    aura67::FrameBuffer detailed{
        reinterpret_cast<std::uint8_t*>(&g_render_a[1]),
        aura67::kFrameWidth,
        aura67::kFrameHeight,
        aura67::kFrameWidth * aura67::kBytesPerPixel,
        aura67::PixelFormat::Bgra8888,
        aura67::RenderDetail::Detailed,
    };
    aura67::FrameBuffer reduced{
        reinterpret_cast<std::uint8_t*>(&g_render_b[1]),
        aura67::kFrameWidth,
        aura67::kFrameHeight,
        aura67::kFrameWidth * aura67::kBytesPerPixel,
        aura67::PixelFormat::Bgra8888,
        aura67::RenderDetail::Reduced,
    };

    const std::uint64_t before = aura67::game_checksum(game);
    CHECK(aura67::game_render(game, detailed));
    CHECK(aura67::game_render(game, reduced));
    CHECK(aura67::game_checksum(game) == before);
    CHECK(g_render_a[0] == 0x11223344U);
    CHECK(g_render_a[kPixelCount + 1U] == 0x55667788U);
    CHECK(g_render_b[0] == 0x99AABBCCU);
    CHECK(g_render_b[kPixelCount + 1U] == 0xDDEEFF00U);

    bool backgrounds_differ = false;
    for (std::size_t index = 0U; index < kPixelCount; ++index) {
        if (g_render_a[index + 1U] != g_render_b[index + 1U]) {
            backgrounds_differ = true;
            break;
        }
    }
    CHECK(backgrounds_differ);

    const auto pixel_at = [](const std::uint32_t* guarded,
                             std::uint32_t x,
                             std::uint32_t y) noexcept {
        return guarded[static_cast<std::size_t>(y) * aura67::kFrameWidth + x + 1U];
    };
    const std::size_t obstacle_pixel =
        static_cast<std::size_t>(254U) * aura67::kFrameWidth + 400U + 1U;
    const std::size_t pickup_pixel =
        static_cast<std::size_t>(210U) * aura67::kFrameWidth + 300U + 1U;
    const std::size_t popup_pixel =
        static_cast<std::size_t>(90U) * aura67::kFrameWidth + 222U + 1U;
    const std::size_t hud_pixel =
        static_cast<std::size_t>(6U) * aura67::kFrameWidth + 15U + 1U;
    const std::size_t goose_pixel =
        static_cast<std::size_t>(aura67::kGroundY - 25) * aura67::kFrameWidth +
        static_cast<std::size_t>(aura67::kRunnerX) + 1U;
    CHECK(g_render_a[obstacle_pixel] == g_render_b[obstacle_pixel]);
    CHECK(g_render_a[pickup_pixel] == g_render_b[pickup_pixel]);
    CHECK(g_render_a[popup_pixel] == g_render_b[popup_pixel]);
    CHECK(g_render_a[hud_pixel] == g_render_b[hud_pixel]);
    CHECK(g_render_a[goose_pixel] == g_render_b[goose_pixel]);

    constexpr std::uint32_t kBgraTrace = 0xff1a4434U;
    constexpr std::uint32_t kBgraMatrixGreen = 0xff39ff14U;
    constexpr std::uint32_t kBgraNeonPink = 0xffff2daaU;
    constexpr std::uint32_t kBgraCriticalRed = 0xffff2438U;
    CHECK(g_render_b[obstacle_pixel] == kBgraTrace);
    CHECK(g_render_b[pickup_pixel] == kBgraMatrixGreen);
    CHECK(g_render_b[popup_pixel] == kBgraNeonPink);
    CHECK(g_render_b[hud_pixel] == kBgraMatrixGreen);

    aura67::GameState panic = game;
    panic.phase = aura67::GamePhase::Finished;
    panic.death_ticks = 20U;
    CHECK(aura67::game_render(panic, detailed));
    CHECK(aura67::game_render(panic, reduced));
    CHECK(pixel_at(g_render_a, 78U, 88U) == kBgraCriticalRed);
    CHECK(pixel_at(g_render_b, 78U, 88U) == kBgraCriticalRed);
    CHECK(pixel_at(g_render_a, 212U, 114U) ==
          pixel_at(g_render_b, 212U, 114U));
    CHECK(g_render_a[0] == 0x11223344U);
    CHECK(g_render_a[kPixelCount + 1U] == 0x55667788U);
    CHECK(g_render_b[0] == 0x99AABBCCU);
    CHECK(g_render_b[kPixelCount + 1U] == 0xDDEEFF00U);
}

void test_framebuffer_contract_and_guards() {
    aura67::GameState game{};
    aura67::game_initialize(&game, 67U);
    run_ticks(&game, 90U);

    g_render_a[0] = 0x11223344U;
    g_render_a[kPixelCount + 1U] = 0x55667788U;
    aura67::FrameBuffer framebuffer{
        reinterpret_cast<std::uint8_t*>(&g_render_a[1]),
        aura67::kFrameWidth,
        aura67::kFrameHeight,
        aura67::kFrameWidth * aura67::kBytesPerPixel,
        aura67::PixelFormat::Bgra8888,
        aura67::RenderDetail::Detailed,
    };
    CHECK(aura67::game_render(game, framebuffer));
    CHECK(g_render_a[0] == 0x11223344U);
    CHECK(g_render_a[kPixelCount + 1U] == 0x55667788U);

    aura67::FrameBuffer invalid = framebuffer;
    invalid.width = aura67::kFrameWidth - 1U;
    CHECK(!aura67::game_render(game, invalid));

    aura67::FrameBuffer second{
        reinterpret_cast<std::uint8_t*>(&g_render_b[1]),
        aura67::kFrameWidth,
        aura67::kFrameHeight,
        aura67::kFrameWidth * aura67::kBytesPerPixel,
        aura67::PixelFormat::Bgra8888,
        aura67::RenderDetail::Detailed,
    };
    CHECK(aura67::game_render(game, second));
    bool same = true;
    for (std::size_t index = 0U; index < kPixelCount; ++index) {
        if (g_render_a[index + 1U] != g_render_b[index + 1U]) {
            same = false;
            break;
        }
    }
    CHECK(same);

    // The panic screen must also stay inside the buffer.
    place_test_obstacle(&game, aura67::ObstacleKind::RamStick,
                        aura67::kRunnerX - 10, aura67::kGroundY - 46, 24, 46);
    aura67::game_tick(&game, aura67::no_input());
    CHECK(game.phase == aura67::GamePhase::Finished);
    framebuffer.detail = aura67::RenderDetail::Reduced;
    CHECK(aura67::game_render(game, framebuffer));
    CHECK(g_render_a[0] == 0x11223344U);
    CHECK(g_render_a[kPixelCount + 1U] == 0x55667788U);
}

void test_fixed_arena() {
    alignas(16) std::uint8_t storage[67]{};
    aura67::Arena arena{};
    aura67::arena_initialize(&arena, storage, sizeof(storage));
    void* first = aura67::arena_allocate(&arena, 7U, 8U);
    void* second = aura67::arena_allocate(&arena, 16U, 16U);
    CHECK(first != nullptr);
    CHECK(second != nullptr);
    CHECK(reinterpret_cast<std::uintptr_t>(first) % 8U == 0U);
    CHECK(reinterpret_cast<std::uintptr_t>(second) % 16U == 0U);
    CHECK(aura67::arena_allocate(&arena, 67U, 1U) == nullptr);
    aura67::arena_reset(&arena);
    CHECK(arena.used == 0U);
}

} // namespace

int main() {
    static_assert(std::is_trivially_copyable<aura67::GameState>::value,
                  "GameState must remain snapshot-friendly and heap-free");
    test_determinism();
    test_jump_arc_is_fixed();
    test_single_flap_per_airtime();
    test_duck_clears_the_flying_cursor();
    test_ground_obstacle_ends_the_run();
    test_badge_pays_67_at_the_current_multiplier();
    test_root_mode_shatters_instead_of_killing();
    test_clutch_bonus_rewards_tight_jumps();
    test_run_never_ends_on_a_timer();
    test_spawner_stays_clearable();
    test_autopilot_survives_the_ramp();
    test_death_restart_keeps_the_record();
    test_platform_reset_is_only_offered_after_a_crash();
    test_escape_hold();
    test_render_policy();
    test_reduced_render_keeps_gameplay_visible();
    test_framebuffer_contract_and_guards();
    test_fixed_arena();

    if (g_failures != 0) {
        std::printf("%d GooseBoot core test(s) failed.\n", g_failures);
        return 1;
    }
    std::printf("All GooseBoot core tests passed.\n");
    return 0;
}
