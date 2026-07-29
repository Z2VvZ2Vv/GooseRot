#include "platform/bios/bios_platform.h"

#include "game/game.h"
#include "platform/bios/bios_io.h"

#include <cstdint>

namespace aura67::bios {
namespace {

constexpr std::uint16_t kPitCommandPort = 0x43U;
constexpr std::uint16_t kPitChannel2Port = 0x42U;
constexpr std::uint16_t kSpeakerControlPort = 0x61U;
constexpr std::uint32_t kPitFrequency = 1193182U;
constexpr std::uint16_t kPitDivisor = static_cast<std::uint16_t>(
    (kPitFrequency + kTickRate / 2U) / kTickRate);
constexpr std::uint8_t kChannel2SquareWave = 0xB6U;
constexpr std::uint8_t kChannel2Output = 1U << 5U;

bool g_previous_output = false;

} // namespace

void clock_initialize() noexcept {
    // Channel 2 is polled rather than connected to an interrupt handler. This
    // leaves the PIC and BIOS timer state untouched and keeps the adapter free
    // from an IDT. Bit 1 stays clear, so no speaker sound is emitted.
    std::uint8_t control = io_read8(kSpeakerControlPort);
    control = static_cast<std::uint8_t>((control | 0x01U) & ~0x02U);
    io_write8(kSpeakerControlPort, control);

    io_write8(kPitCommandPort, kChannel2SquareWave);
    io_write8(kPitChannel2Port, static_cast<std::uint8_t>(kPitDivisor));
    io_write8(kPitChannel2Port, static_cast<std::uint8_t>(kPitDivisor >> 8U));
    g_previous_output = (io_read8(kSpeakerControlPort) & kChannel2Output) != 0U;
}

void clock_wait_tick() noexcept {
    for (;;) {
        const bool output =
            (io_read8(kSpeakerControlPort) & kChannel2Output) != 0U;
        if (output && !g_previous_output) {
            g_previous_output = output;
            return;
        }
        g_previous_output = output;
        __asm__ volatile("pause");
    }
}

} // namespace aura67::bios
