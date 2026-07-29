#include <cstddef>
#include <cstdint>

// Tiny freestanding routines required by the existing portable core
// (game_initialize uses memset) and by possible compiler-generated POD copies.
// Build this file with -ffreestanding/-fno-builtin together with the adapter.

extern "C" void* memset(void* destination, int value, std::size_t count) noexcept {
    auto* bytes = static_cast<volatile std::uint8_t*>(destination);
    const std::uint8_t byte = static_cast<std::uint8_t>(value);
    for (std::size_t index = 0U; index < count; ++index) {
        bytes[index] = byte;
    }
    return destination;
}

extern "C" void* memcpy(void* destination, const void* source, std::size_t count) noexcept {
    auto* output = static_cast<volatile std::uint8_t*>(destination);
    const auto* input = static_cast<const volatile std::uint8_t*>(source);
    for (std::size_t index = 0U; index < count; ++index) {
        output[index] = input[index];
    }
    return destination;
}

extern "C" void* memmove(void* destination, const void* source, std::size_t count) noexcept {
    auto* output = static_cast<volatile std::uint8_t*>(destination);
    const auto* input = static_cast<const volatile std::uint8_t*>(source);
    const std::uintptr_t output_address = reinterpret_cast<std::uintptr_t>(destination);
    const std::uintptr_t input_address = reinterpret_cast<std::uintptr_t>(source);
    if (output_address < input_address) {
        for (std::size_t index = 0U; index < count; ++index) {
            output[index] = input[index];
        }
    } else if (output_address > input_address) {
        for (std::size_t index = count; index != 0U; --index) {
            output[index - 1U] = input[index - 1U];
        }
    }
    return destination;
}
