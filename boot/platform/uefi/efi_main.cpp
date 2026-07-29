#include "game/game.h"
#include "platform/uefi/uefi_clock.h"
#include "platform/uefi/uefi_graphics.h"
#include "platform/uefi/uefi_input.h"

namespace aura67::uefi {
namespace {

GameState g_game;
GraphicsContext g_graphics;
InputContext g_input;
TickClock g_clock;

// Keep one absolute address in the PE/COFF image so a no-CRT GNU link emits a
// real base-relocation directory.  UEFI normally loads applications away from
// image base zero.  The firmware never reads this anchor; link this target
// without section garbage collection so it remains in the final image.
GameState* volatile g_relocation_anchor = &g_game;

Status run_game(SystemTable* system_table) noexcept {
    Status status = graphics_initialize(system_table, &g_graphics);
    if (efi_error(status)) {
        return status;
    }

    status = input_initialize(system_table, &g_input);
    if (efi_error(status)) {
        const Status restore_status = graphics_shutdown(&g_graphics);
        return efi_error(restore_status) ? restore_status : status;
    }

    status = clock_initialize(system_table, kTickRate, &g_clock);
    if (efi_error(status)) {
        const Status restore_status = graphics_shutdown(&g_graphics);
        return efi_error(restore_status) ? restore_status : status;
    }

    game_initialize(&g_game, 67U);
    if (!game_render(g_game, g_graphics.game_framebuffer)) {
        status = kDeviceError;
    } else {
        status = graphics_present(&g_graphics);
    }

    while (!efi_error(status)) {
        std::uint32_t ticks_due = 0U;
        status = clock_wait_for_ticks(&g_clock, &ticks_due);
        if (efi_error(status)) {
            break;
        }

        InputState input = no_input();
        status = input_poll(&g_input, &input);
        if (efi_error(status)) {
            break;
        }

        // Simple Text Input has no key-release event, so requiring a two-second
        // held Escape would depend on firmware autorepeat.  A deliberate press
        // exits this adapter immediately; other platforms keep the core's
        // held-Escape contract.
        if (input.was_pressed(InputButton::Escape)) {
            status = kSuccess;
            break;
        }

        GameSignal signal = GameSignal::None;
        for (std::uint32_t tick = 0U; tick < ticks_due; ++tick) {
            const InputState tick_input = tick == 0U
                                              ? input
                                              : InputState{input.held, 0U, 0U};
            signal = game_tick(&g_game, tick_input);
            if (signal != GameSignal::None) {
                break;
            }
        }
        if (!game_render(g_game, g_graphics.game_framebuffer)) {
            status = kDeviceError;
            break;
        }
        status = graphics_present(&g_graphics);
        if (efi_error(status)) {
            break;
        }

        if (signal == GameSignal::QuitRequested) {
            status = kSuccess;
            break;
        }
        if (signal == GameSignal::ResetRequested) {
            if (system_table->runtime_services == nullptr ||
                system_table->runtime_services->reset_system == nullptr) {
                status = kUnsupported;
                break;
            }
            // UEFI ResetSystem is invoked only after the explicit post-game R
            // signal from the portable core.  It must not return on success.
            system_table->runtime_services->reset_system(
                ResetType::Cold, kSuccess, 0U, nullptr);
            status = kDeviceError;
            break;
        }
    }

    clock_shutdown(&g_clock);
    const Status restore_status = graphics_shutdown(&g_graphics);
    if (!efi_error(status) && efi_error(restore_status)) {
        status = restore_status;
    }
    return status;
}

} // namespace
} // namespace aura67::uefi

extern "C" aura67::uefi::Status AURA67_EFIAPI
efi_main(aura67::uefi::Handle image_handle, aura67::uefi::SystemTable* system_table) noexcept {
    (void)image_handle;
    if (system_table == nullptr || system_table->boot_services == nullptr) {
        return aura67::uefi::kInvalidParameter;
    }
    // A Boot Manager may arm its five-minute image watchdog before StartImage.
    // The result screen is intentionally unbounded, so disarm it up front and
    // fail closed if firmware reports a real error. EFI_UNSUPPORTED is allowed.
    if (system_table->boot_services->set_watchdog_timer != nullptr) {
        const aura67::uefi::Status watchdog_status =
            system_table->boot_services->set_watchdog_timer(0U, 0U, 0U, nullptr);
        if (aura67::uefi::efi_error(watchdog_status) &&
            watchdog_status != aura67::uefi::kUnsupported) {
            return watchdog_status;
        }
    }
    // Boot Services deliberately remain active for the entire game.  This
    // adapter never locates a disk protocol and never calls ExitBootServices.
    return aura67::uefi::run_game(system_table);
}
