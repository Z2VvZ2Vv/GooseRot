#include "platform/bios/bios_platform.h"

#include "game/game.h"
#include "platform/bios/bios_io.h"

#include <cstddef>
#include <cstdint>

extern "C" void* memset(void* destination, int value, std::size_t count) noexcept {
    auto* output = static_cast<std::uint8_t*>(destination);
    for (std::size_t index = 0U; index < count; ++index) {
        output[index] = static_cast<std::uint8_t>(value);
    }
    return destination;
}

extern "C" void* memcpy(void* destination, const void* source, std::size_t count) noexcept {
    auto* output = static_cast<std::uint8_t*>(destination);
    const auto* input = static_cast<const std::uint8_t*>(source);
    for (std::size_t index = 0U; index < count; ++index) {
        output[index] = input[index];
    }
    return destination;
}

namespace aura67::bios {
namespace {

GameState g_game;

} // namespace

[[noreturn]] void platform_halt() noexcept {
    __asm__ volatile("cli");
    for (;;) {
        __asm__ volatile("hlt");
    }
}

[[noreturn]] void platform_reset() noexcept {
    __asm__ volatile("cli");

    // The reset is reachable only after the core has finished and the player
    // explicitly presses R. It writes no disk or firmware state. Prefer the
    // legacy keyboard-controller pulse, then use the chipset reset port as a
    // fallback if the first request is ignored.
    for (std::uint32_t count = 0U; count < 65536U; ++count) {
        if ((io_read8(0x64U) & 0x02U) == 0U) {
            io_write8(0x64U, 0xFEU);
            break;
        }
    }
    for (volatile std::uint32_t delay = 0U; delay < 1000000U; ++delay) {
        __asm__ volatile("pause");
    }
    io_write8(0xCF9U, 0x06U);
    platform_halt();
}

} // namespace aura67::bios

extern "C" [[noreturn]] void bios_main() noexcept {
    using namespace aura67;
    using namespace aura67::bios;

    if (!graphics_initialize()) {
        platform_halt();
    }

    input_initialize();
    clock_initialize();
    game_initialize(&g_game, 67U);
    static_cast<void>(game_render(g_game, graphics_render_target()));
    graphics_present();

    for (;;) {
        clock_wait_tick();
        const InputState input = input_poll();
        const GameSignal signal = game_tick(&g_game, input);
        if (signal == GameSignal::QuitRequested) {
            platform_halt();
        }
        if (signal == GameSignal::ResetRequested) {
            platform_reset();
        }
        static_cast<void>(game_render(g_game, graphics_render_target()));
        graphics_present();
    }
}

