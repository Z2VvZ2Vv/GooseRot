#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>

namespace {

constexpr std::size_t kSectorSize = 512U;
constexpr std::size_t kStage2SectorCount = 127U;
constexpr std::size_t kImageSize = kSectorSize * (1U + kStage2SectorCount);

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool is_safe_image_path(const std::string& path) {
    const std::string normalized = lowercase(path);
    if (normalized.rfind(R"(\\.\)", 0U) == 0U ||
        normalized.rfind(R"(//./)", 0U) == 0U ||
        normalized.rfind(R"(\\?\globalroot)", 0U) == 0U) {
        return false;
    }
    return normalized.size() >= 4U && normalized.substr(normalized.size() - 4U) == ".img";
}

bool read_file(const char* path, std::uint8_t* destination, std::size_t capacity, std::size_t* size) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return false;
    const std::streamoff length = input.tellg();
    if (length < 0 || static_cast<std::uint64_t>(length) > capacity) return false;
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(destination), length);
    if (!input) return false;
    *size = static_cast<std::size_t>(length);
    return true;
}

} // namespace

int main(int argumentCount, char** arguments) {
    if (argumentCount != 4) {
        std::cerr << "usage: gooseboot_image_builder <stage1.bin> <stage2.bin> <output.img>\n";
        return 2;
    }
    if (!is_safe_image_path(arguments[3])) {
        std::cerr << "refusing output: expected an ordinary .img path, never a raw device\n";
        return 3;
    }

    std::array<std::uint8_t, kImageSize> image{};
    std::size_t stage1Size = 0U;
    std::size_t stage2Size = 0U;
    if (!read_file(arguments[1], image.data(), kSectorSize, &stage1Size) || stage1Size != kSectorSize) {
        std::cerr << "stage1 must be exactly 512 bytes\n";
        return 4;
    }
    if (image[510] != 0x55U || image[511] != 0xAAU) {
        std::cerr << "stage1 is missing the 0x55AA boot signature\n";
        return 5;
    }
    if (!read_file(arguments[2], image.data() + kSectorSize,
                   kStage2SectorCount * kSectorSize, &stage2Size) || stage2Size == 0U) {
        std::cerr << "stage2 is empty, unreadable, or exceeds 127 sectors\n";
        return 6;
    }

    std::ofstream output(arguments[3], std::ios::binary | std::ios::trunc);
    if (!output) {
        std::cerr << "unable to create the output image\n";
        return 7;
    }
    output.write(reinterpret_cast<const char*>(image.data()),
                 static_cast<std::streamsize>(image.size()));
    if (!output) {
        std::cerr << "unable to finish the output image\n";
        return 8;
    }

    std::cout << "assembled a " << image.size()
              << "-byte read-only-layout BIOS image; stage2 uses " << stage2Size << " bytes\n";
    return 0;
}
