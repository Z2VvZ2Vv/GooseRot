#include "common/arena.h"
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

aura67::InputState scripted_input(std::uint32_t tick) noexcept {
    aura67::InputState input = aura67::no_input();
    if ((tick / 45U) % 2U == 0U) {
        input.held |= aura67::input_mask(aura67::InputButton::Right);
    } else {
        input.held |= aura67::input_mask(aura67::InputButton::Down);
    }
    if (tick % 67U == 0U) {
        input.held |= aura67::input_mask(aura67::InputButton::Dash);
        input.pressed |= aura67::input_mask(aura67::InputButton::Dash);
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

    for (std::uint32_t tick = 0U; tick < 1000U; ++tick) {
        const aura67::InputState input = scripted_input(tick);
        CHECK(aura67::game_tick(&first, input) == aura67::GameSignal::None);
        CHECK(aura67::game_tick(&second, input) == aura67::GameSignal::None);
        CHECK(aura67::game_checksum(first) == aura67::game_checksum(second));
    }
    // Cross-toolchain golden checkpoint: the simulation hashes fields in a
    // specified byte order, so this value must also match BIOS/UEFI adapters.
    CHECK(aura67::game_checksum(first) == 12129605124478255814ULL);
}

void test_dash_is_exactly_67_pixels() {
    aura67::GameState game{};
    aura67::game_initialize(&game, 67U);
    for (std::size_t index = 0U; index < aura67::kNpcCount; ++index) {
        game.npcs[index].active = 0U;
    }
    const std::int32_t before = game.goose.x_fp;
    aura67::InputState input = aura67::no_input();
    input.held = aura67::input_mask(aura67::InputButton::Right) |
                 aura67::input_mask(aura67::InputButton::Dash);
    input.pressed = aura67::input_mask(aura67::InputButton::Dash);
    aura67::game_tick(&game, input);
    CHECK(game.goose.x_fp - before == aura67::pixels_to_fixed(67));
    CHECK(game.last_dash_pixels == 67U);
}

void test_cursor_push_is_exactly_67_pixels() {
    aura67::GameState game{};
    aura67::game_initialize(&game, 67U);
    game.hostile_cursor.x_fp = game.goose.x_fp + aura67::pixels_to_fixed(20);
    game.hostile_cursor.y_fp = game.goose.y_fp;
    const std::int32_t before = game.hostile_cursor.x_fp;
    aura67::InputState input = aura67::no_input();
    input.held = aura67::input_mask(aura67::InputButton::Right) |
                 aura67::input_mask(aura67::InputButton::Dash);
    input.pressed = aura67::input_mask(aura67::InputButton::Dash);
    aura67::game_tick(&game, input);
    const std::int32_t displacement = game.hostile_cursor.x_fp - before;
    CHECK((displacement < 0 ? -displacement : displacement) == aura67::pixels_to_fixed(67));
    CHECK(game.last_cursor_push_pixels == 67U);
    CHECK(game.hostile_cursor.stun_ticks == 29U);
}

void test_badge_scoring() {
    aura67::GameState game{};
    aura67::game_initialize(&game, 67U);
    for (std::size_t index = 0U; index < aura67::kNpcCount; ++index) {
        game.npcs[index].active = 0U;
    }
    for (std::size_t index = 1U; index < aura67::kBadgeCount; ++index) {
        game.badges[index].active = 0U;
    }
    game.hostile_cursor.x_fp = aura67::pixels_to_fixed(20);
    game.hostile_cursor.y_fp = aura67::pixels_to_fixed(70);
    game.badges[0].x_fp = game.goose.x_fp;
    game.badges[0].y_fp = game.goose.y_fp;
    game.badges[0].active = 1U;
    aura67::game_tick(&game, aura67::no_input());
    CHECK(game.score == -1);
    CHECK(game.badges_collected == 1U);
}

void test_round_and_end_controls() {
    aura67::GameState game{};
    aura67::game_initialize(&game, 67U);
    for (std::uint32_t tick = 0U; tick < aura67::kRoundTicks; ++tick) {
        aura67::game_tick(&game, aura67::no_input());
    }
    CHECK(game.phase == aura67::GamePhase::Finished);
    CHECK(game.tick == aura67::kRoundTicks);
    CHECK(aura67::game_seconds_remaining(game) == 0U);

    aura67::InputState reset = aura67::no_input();
    reset.pressed = aura67::input_mask(aura67::InputButton::Reset);
    CHECK(aura67::game_tick(&game, reset) == aura67::GameSignal::ResetRequested);

    aura67::InputState confirm = aura67::no_input();
    confirm.pressed = aura67::input_mask(aura67::InputButton::Confirm);
    CHECK(aura67::game_tick(&game, confirm) == aura67::GameSignal::None);
    CHECK(game.phase == aura67::GamePhase::Playing);
    CHECK(game.tick == 0U);
    CHECK(game.score == -10000);
    CHECK(game.seed == 67U);
}

void test_escape_hold() {
    aura67::GameState game{};
    aura67::game_initialize(&game, 67U);
    aura67::InputState input = aura67::no_input();
    input.held = aura67::input_mask(aura67::InputButton::Escape);
    for (std::uint32_t tick = 0U; tick < aura67::kTickRate * 2U - 1U; ++tick) {
        CHECK(aura67::game_tick(&game, input) == aura67::GameSignal::None);
    }
    CHECK(aura67::game_tick(&game, input) == aura67::GameSignal::QuitRequested);
}

void test_framebuffer_contract_and_guards() {
    aura67::GameState game{};
    aura67::game_initialize(&game, 67U);
    g_render_a[0] = 0x11223344U;
    g_render_a[kPixelCount + 1U] = 0x55667788U;
    aura67::FrameBuffer framebuffer{
        reinterpret_cast<std::uint8_t*>(&g_render_a[1]),
        aura67::kFrameWidth,
        aura67::kFrameHeight,
        aura67::kFrameWidth * aura67::kBytesPerPixel,
        aura67::PixelFormat::Bgra8888,
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
    test_dash_is_exactly_67_pixels();
    test_cursor_push_is_exactly_67_pixels();
    test_badge_scoring();
    test_round_and_end_controls();
    test_escape_hold();
    test_framebuffer_contract_and_guards();
    test_fixed_arena();

    if (g_failures != 0) {
        std::printf("%d GooseBoot core test(s) failed.\n", g_failures);
        return 1;
    }
    std::printf("All GooseBoot core tests passed.\n");
    return 0;
}
