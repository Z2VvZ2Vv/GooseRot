#include "game/game.h"

#include <cstddef>
#include <cstdint>

namespace aura67 {
namespace {

struct Color {
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;
};

struct Glyph {
    char character;
    std::uint8_t rows[7];
};

constexpr Color kBlack{4U, 6U, 11U};
constexpr Color kSkyHigh{9U, 12U, 22U};
constexpr Color kSkyLow{15U, 20U, 33U};
constexpr Color kScanline{5U, 7U, 14U};
constexpr Color kPanel{12U, 16U, 28U};
constexpr Color kPanelEdge{31U, 40U, 62U};
constexpr Color kBoardFar{20U, 28U, 38U};
constexpr Color kBoardMid{30U, 43U, 54U};
constexpr Color kBoardLight{44U, 62U, 74U};
constexpr Color kSubstrate{9U, 26U, 22U};
constexpr Color kSubstrateDark{6U, 18U, 16U};
constexpr Color kTrace{26U, 68U, 52U};
constexpr Color kWhite{255U, 251U, 234U};
constexpr Color kGray{152U, 160U, 172U};
constexpr Color kDim{74U, 84U, 100U};
constexpr Color kOutline{15U, 15U, 21U};
constexpr Color kGooseOrange{245U, 143U, 39U};
constexpr Color kGooseOrangeDark{186U, 96U, 18U};
constexpr Color kNeonPink{255U, 45U, 170U};
constexpr Color kMatrixGreen{57U, 255U, 20U};
constexpr Color kAmber{255U, 176U, 0U};
constexpr Color kCyan{34U, 211U, 238U};
constexpr Color kCriticalRed{255U, 36U, 56U};
constexpr Color kPanicBlue{38U, 78U, 214U};
constexpr Color kGold{247U, 201U, 72U};

constexpr Glyph kGlyphs[] = {
    {'A', {14, 17, 17, 31, 17, 17, 17}}, {'B', {30, 17, 17, 30, 17, 17, 30}},
    {'C', {14, 17, 16, 16, 16, 17, 14}}, {'D', {30, 17, 17, 17, 17, 17, 30}},
    {'E', {31, 16, 16, 30, 16, 16, 31}}, {'F', {31, 16, 16, 30, 16, 16, 16}},
    {'G', {14, 17, 16, 23, 17, 17, 15}}, {'H', {17, 17, 17, 31, 17, 17, 17}},
    {'I', {14, 4, 4, 4, 4, 4, 14}}, {'J', {7, 2, 2, 2, 18, 18, 12}},
    {'K', {17, 18, 20, 24, 20, 18, 17}}, {'L', {16, 16, 16, 16, 16, 16, 31}},
    {'M', {17, 27, 21, 21, 17, 17, 17}}, {'N', {17, 25, 21, 19, 17, 17, 17}},
    {'O', {14, 17, 17, 17, 17, 17, 14}}, {'P', {30, 17, 17, 30, 16, 16, 16}},
    {'Q', {14, 17, 17, 17, 21, 18, 13}}, {'R', {30, 17, 17, 30, 20, 18, 17}},
    {'S', {15, 16, 16, 14, 1, 1, 30}}, {'T', {31, 4, 4, 4, 4, 4, 4}},
    {'U', {17, 17, 17, 17, 17, 17, 14}}, {'V', {17, 17, 17, 17, 17, 10, 4}},
    {'W', {17, 17, 17, 21, 21, 21, 10}}, {'X', {17, 17, 10, 4, 10, 17, 17}},
    {'Y', {17, 17, 10, 4, 4, 4, 4}}, {'Z', {31, 1, 2, 4, 8, 16, 31}},
    {'0', {14, 17, 19, 21, 25, 17, 14}}, {'1', {4, 12, 4, 4, 4, 4, 14}},
    {'2', {14, 17, 1, 2, 4, 8, 31}}, {'3', {30, 1, 1, 14, 1, 1, 30}},
    {'4', {2, 6, 10, 18, 31, 2, 2}}, {'5', {31, 16, 16, 30, 1, 1, 30}},
    {'6', {14, 16, 16, 30, 17, 17, 14}}, {'7', {31, 1, 2, 4, 8, 8, 8}},
    {'8', {14, 17, 17, 14, 17, 17, 14}}, {'9', {14, 17, 17, 15, 1, 1, 14}},
    {'-', {0, 0, 0, 31, 0, 0, 0}}, {'+', {0, 4, 4, 31, 4, 4, 0}},
    {':', {0, 4, 4, 0, 4, 4, 0}}, {'[', {14, 8, 8, 8, 8, 8, 14}},
    {']', {14, 2, 2, 2, 2, 2, 14}}, {'/', {1, 1, 2, 4, 8, 16, 16}},
    {'!', {4, 4, 4, 4, 4, 0, 4}}, {'?', {14, 17, 1, 2, 4, 0, 4}},
    {'.', {0, 0, 0, 0, 0, 4, 4}}, {'=', {0, 0, 31, 0, 31, 0, 0}},
    {'*', {0, 21, 14, 31, 14, 21, 0}}, {'>', {8, 4, 2, 1, 2, 4, 8}},
    {'<', {2, 4, 8, 16, 8, 4, 2}}, {'#', {10, 31, 10, 10, 10, 31, 10}},
    {'%', {17, 1, 2, 4, 8, 16, 17}}, {'_', {0, 0, 0, 0, 0, 0, 31}},
    {'|', {4, 4, 4, 4, 4, 4, 4}}, {',', {0, 0, 0, 0, 4, 4, 8}},
};

// ---------------------------------------------------------------------------
// Raster primitives
// ---------------------------------------------------------------------------

void put_pixel(const FrameBuffer& framebuffer, int x, int y, Color color) noexcept {
    if (x < 0 || y < 0 || x >= static_cast<int>(framebuffer.width) ||
        y >= static_cast<int>(framebuffer.height)) {
        return;
    }
    std::uint8_t* pixel = framebuffer.base +
                          static_cast<std::size_t>(y) * framebuffer.stride_bytes +
                          static_cast<std::size_t>(x) * kBytesPerPixel;
    if (framebuffer.format == PixelFormat::Bgra8888) {
        pixel[0] = color.blue;
        pixel[1] = color.green;
        pixel[2] = color.red;
    } else {
        pixel[0] = color.red;
        pixel[1] = color.green;
        pixel[2] = color.blue;
    }
    pixel[3] = 255U;
}

// Rectangles cover most of the frame every tick, so this clips once and then
// writes rows directly instead of paying a bounds check per pixel.
void fill_rect(const FrameBuffer& framebuffer, int x, int y, int width, int height, Color color) noexcept {
    if (width <= 0 || height <= 0) {
        return;
    }
    const int left = x < 0 ? 0 : x;
    const int top = y < 0 ? 0 : y;
    const int right_raw = x + width;
    const int bottom_raw = y + height;
    const int right = right_raw > static_cast<int>(framebuffer.width)
                          ? static_cast<int>(framebuffer.width)
                          : right_raw;
    const int bottom = bottom_raw > static_cast<int>(framebuffer.height)
                           ? static_cast<int>(framebuffer.height)
                           : bottom_raw;
    if (left >= right || top >= bottom) {
        return;
    }

    const bool bgra = framebuffer.format == PixelFormat::Bgra8888;
    const std::uint8_t first = bgra ? color.blue : color.red;
    const std::uint8_t third = bgra ? color.red : color.blue;
    for (int py = top; py < bottom; ++py) {
        std::uint8_t* pixel = framebuffer.base +
                              static_cast<std::size_t>(py) * framebuffer.stride_bytes +
                              static_cast<std::size_t>(left) * kBytesPerPixel;
        for (int px = left; px < right; ++px) {
            pixel[0] = first;
            pixel[1] = color.green;
            pixel[2] = third;
            pixel[3] = 255U;
            pixel += kBytesPerPixel;
        }
    }
}

void stroke_rect(const FrameBuffer& framebuffer, int x, int y, int width, int height, Color color) noexcept {
    fill_rect(framebuffer, x, y, width, 1, color);
    fill_rect(framebuffer, x, y + height - 1, width, 1, color);
    fill_rect(framebuffer, x, y, 1, height, color);
    fill_rect(framebuffer, x + width - 1, y, 1, height, color);
}

void fill_ellipse(const FrameBuffer& framebuffer, int center_x, int center_y, int radius_x, int radius_y, Color color) noexcept {
    if (radius_x <= 0 || radius_y <= 0) {
        return;
    }
    const std::int64_t radius_x_squared = static_cast<std::int64_t>(radius_x) * radius_x;
    const std::int64_t radius_y_squared = static_cast<std::int64_t>(radius_y) * radius_y;
    const std::int64_t limit = radius_x_squared * radius_y_squared;
    for (int y = -radius_y; y <= radius_y; ++y) {
        for (int x = -radius_x; x <= radius_x; ++x) {
            const std::int64_t value = static_cast<std::int64_t>(x) * x * radius_y_squared +
                                       static_cast<std::int64_t>(y) * y * radius_x_squared;
            if (value <= limit) {
                put_pixel(framebuffer, center_x + x, center_y + y, color);
            }
        }
    }
}

void draw_line(const FrameBuffer& framebuffer, int x0, int y0, int x1, int y1, Color color) noexcept {
    const int raw_dx = x1 - x0;
    const int dx = raw_dx < 0 ? -raw_dx : raw_dx;
    const int step_x = x0 < x1 ? 1 : -1;
    const int raw_dy = y1 - y0;
    const int dy = -(raw_dy < 0 ? -raw_dy : raw_dy);
    const int step_y = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        put_pixel(framebuffer, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int doubled = 2 * error;
        if (doubled >= dy) {
            error += dy;
            x0 += step_x;
        }
        if (doubled <= dx) {
            error += dx;
            y0 += step_y;
        }
    }
}

void draw_thick_line(const FrameBuffer& framebuffer, int x0, int y0, int x1, int y1, int thickness, Color color) noexcept {
    for (int offset = 0; offset < thickness; ++offset) {
        const int shift = offset - thickness / 2;
        draw_line(framebuffer, x0 + shift, y0, x1 + shift, y1, color);
        draw_line(framebuffer, x0, y0 + shift, x1, y1 + shift, color);
    }
}

Color shade(Color color, int numerator, int denominator) noexcept {
    if (denominator <= 0) {
        return color;
    }
    const int red = color.red * numerator / denominator;
    const int green = color.green * numerator / denominator;
    const int blue = color.blue * numerator / denominator;
    return Color{
        static_cast<std::uint8_t>(red > 255 ? 255 : red),
        static_cast<std::uint8_t>(green > 255 ? 255 : green),
        static_cast<std::uint8_t>(blue > 255 ? 255 : blue),
    };
}

// Deterministic on every target: only 32-bit wrapping arithmetic.
std::uint32_t hash32(std::uint32_t value) noexcept {
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    value ^= value >> 16U;
    return value;
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

const std::uint8_t* glyph_rows(char character) noexcept {
    for (std::size_t index = 0U; index < sizeof(kGlyphs) / sizeof(kGlyphs[0]); ++index) {
        if (kGlyphs[index].character == character) {
            return kGlyphs[index].rows;
        }
    }
    return nullptr;
}

void draw_character(const FrameBuffer& framebuffer, int x, int y, char character, int scale, Color color) noexcept {
    const std::uint8_t* rows = glyph_rows(character);
    if (rows == nullptr) {
        return;
    }
    for (int row = 0; row < 7; ++row) {
        for (int column = 0; column < 5; ++column) {
            if ((rows[row] & static_cast<std::uint8_t>(1U << (4 - column))) != 0U) {
                fill_rect(framebuffer, x + column * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

void draw_text(const FrameBuffer& framebuffer, int x, int y, const char* text, int scale, Color color) noexcept {
    int cursor_x = x;
    for (const char* current = text; *current != '\0'; ++current) {
        if (*current != ' ') {
            draw_character(framebuffer, cursor_x, y, *current, scale, color);
        }
        cursor_x += 6 * scale;
    }
}

int text_width(const char* text, int scale) noexcept {
    int count = 0;
    for (const char* current = text; *current != '\0'; ++current) {
        ++count;
    }
    return count * 6 * scale;
}

void draw_text_centered(const FrameBuffer& framebuffer, int center_x, int y, const char* text, int scale, Color color) noexcept {
    draw_text(framebuffer, center_x - text_width(text, scale) / 2, y, text, scale, color);
}

void format_integer(std::int32_t value, char* buffer, std::size_t capacity) noexcept {
    if (capacity == 0U) {
        return;
    }
    char reversed[16]{};
    std::size_t count = 0U;
    const bool negative = value < 0;
    std::uint32_t magnitude = negative
                                  ? static_cast<std::uint32_t>(-(static_cast<std::int64_t>(value)))
                                  : static_cast<std::uint32_t>(value);
    do {
        reversed[count++] = static_cast<char>('0' + magnitude % 10U);
        magnitude /= 10U;
    } while (magnitude != 0U && count < sizeof(reversed));
    if (negative && count < sizeof(reversed)) {
        reversed[count++] = '-';
    }

    std::size_t output = 0U;
    while (output < count && output + 1U < capacity) {
        buffer[output] = reversed[count - output - 1U];
        ++output;
    }
    buffer[output] = '\0';
}

// Odometer formatting, the way a POST counter would show it. A value too large
// for the field is printed in full rather than silently losing its high digits:
// an endless run is allowed to outgrow six digits.
void format_padded(std::int32_t value, std::size_t digits, char* buffer, std::size_t capacity) noexcept {
    if (capacity == 0U) {
        return;
    }
    if (digits > capacity - 1U) {
        digits = capacity - 1U;
    }
    const std::uint32_t magnitude = value < 0 ? 0U : static_cast<std::uint32_t>(value);
    std::size_t needed = 1U;
    for (std::uint32_t remaining = magnitude / 10U; remaining != 0U; remaining /= 10U) {
        ++needed;
    }
    if (needed > digits) {
        format_integer(value, buffer, capacity);
        return;
    }

    std::uint32_t rest = magnitude;
    std::size_t written = digits;
    buffer[digits] = '\0';
    while (written != 0U) {
        --written;
        buffer[written] = static_cast<char>('0' + rest % 10U);
        rest /= 10U;
    }
}

// Fixed-capacity line builder. The firmware targets have no stdio, so HUD lines
// that mix labels and counters are assembled here.
struct LineBuilder {
    char text[52];
    std::size_t used;

    void reset() noexcept {
        used = 0U;
        text[0] = '\0';
    }
    void append(const char* value) noexcept {
        while (*value != '\0' && used + 1U < sizeof(text)) {
            text[used++] = *value++;
        }
        text[used] = '\0';
    }
    void append_number(std::int32_t value) noexcept {
        char buffer[16]{};
        format_integer(value, buffer, sizeof(buffer));
        append(buffer);
    }
    void append_padded(std::int32_t value, std::size_t digits) noexcept {
        char buffer[16]{};
        format_padded(value, digits, buffer, sizeof(buffer));
        append(buffer);
    }
};

Color palette_color(std::uint8_t palette) noexcept {
    switch (palette & 3U) {
    case 0U: return kMatrixGreen;
    case 1U: return kAmber;
    case 2U: return kCyan;
    default: return kNeonPink;
    }
}

// ---------------------------------------------------------------------------
// Parallax firmware landscape
// ---------------------------------------------------------------------------

void draw_sky(const FrameBuffer& framebuffer) noexcept {
    fill_rect(framebuffer, 0, 0, static_cast<int>(kFrameWidth), static_cast<int>(kFrameHeight), kBlack);
    for (int y = 46; y < static_cast<int>(kGroundY); ++y) {
        const int depth = (y - 46) * 255 / (static_cast<int>(kGroundY) - 46);
        Color band{
            static_cast<std::uint8_t>(kSkyHigh.red + (kSkyLow.red - kSkyHigh.red) * depth / 255),
            static_cast<std::uint8_t>(kSkyHigh.green + (kSkyLow.green - kSkyHigh.green) * depth / 255),
            static_cast<std::uint8_t>(kSkyHigh.blue + (kSkyLow.blue - kSkyHigh.blue) * depth / 255),
        };
        if ((y & 3) == 0) {
            band = kScanline; // Free CRT scanline: the sky is filled anyway.
        }
        fill_rect(framebuffer, 0, y, static_cast<int>(kFrameWidth), 1, band);
    }
}

// Deepest layer: the setup-utility grid, plus firmware dialogs drifting far
// behind the action at a third of the run speed.
void draw_sky_furniture(const FrameBuffer& framebuffer, const GameState& game, Color accent) noexcept {
    constexpr Color kGrid{13U, 18U, 31U};
    for (int y = 68; y < static_cast<int>(kGroundY) - 38; y += 22) {
        fill_rect(framebuffer, 0, y, static_cast<int>(kFrameWidth), 1, kGrid);
    }
    const std::uint32_t line_offset = (game.scroll_fp >> 3U) >> 8U;
    const int line_phase = static_cast<int>(line_offset % 48U);
    for (int x = -48; x < static_cast<int>(kFrameWidth); x += 48) {
        fill_rect(framebuffer, x + 48 - line_phase, 68, 1, static_cast<int>(kGroundY) - 106, kGrid);
    }

    constexpr std::uint32_t kPitch = 214U;
    const std::uint32_t offset = (game.scroll_fp / 3U) >> 8U;
    const std::uint32_t phase = offset % kPitch;
    const std::uint32_t base = offset / kPitch;
    for (std::uint32_t slot = 0U; slot <= kFrameWidth / kPitch + 1U; ++slot) {
        const std::uint32_t noise = hash32(base + slot + 0x67U);
        const int x = static_cast<int>(slot * kPitch) - static_cast<int>(phase);
        const int y = 94 + static_cast<int>(noise % 72U);
        const int width = 74 + static_cast<int>((noise >> 8U) % 40U);
        fill_rect(framebuffer, x, y, width, 46, shade(kPanel, 3, 2));
        fill_rect(framebuffer, x, y, width, 8, shade(accent, 1, 5));
        for (int line = 0; line < 3; ++line) {
            fill_rect(framebuffer, x + 6, y + 14 + line * 9, width - 14 - line * 12, 3, shade(kDim, 1, 3));
        }
    }
}

// Far layer: a dim memory-map histogram sliding at a quarter of the run speed.
void draw_memory_map(const FrameBuffer& framebuffer, const GameState& game) noexcept {
    constexpr std::uint32_t kPitch = 14U;
    const std::uint32_t offset = (game.scroll_fp >> 2U) >> 8U;
    const std::uint32_t phase = offset % kPitch;
    const std::uint32_t base = offset / kPitch;
    for (std::uint32_t column = 0U; column <= kFrameWidth / kPitch + 1U; ++column) {
        const std::uint32_t noise = hash32(base + column);
        const int height = 14 + static_cast<int>(noise % 52U);
        const int x = static_cast<int>(column * kPitch) - static_cast<int>(phase);
        fill_rect(framebuffer, x, static_cast<int>(kGroundY) - height, 11, height, kBoardFar);
        if ((noise & 0x30000U) != 0U) {
            fill_rect(framebuffer, x + 2, static_cast<int>(kGroundY) - height + 3, 7, 2, kBoardMid);
        }
    }
}

// Mid layer: recognisable motherboard hardware at half speed.
void draw_board_props(const FrameBuffer& framebuffer, const GameState& game, Color accent) noexcept {
    constexpr std::uint32_t kPitch = 152U;
    const std::uint32_t offset = (game.scroll_fp >> 1U) >> 8U;
    const std::uint32_t phase = offset % kPitch;
    const std::uint32_t base = offset / kPitch;
    const int ground = static_cast<int>(kGroundY);

    for (std::uint32_t slot = 0U; slot <= kFrameWidth / kPitch + 1U; ++slot) {
        const std::uint32_t noise = hash32((base + slot) * 2654435761U);
        const int x = static_cast<int>(slot * kPitch) - static_cast<int>(phase) +
                      static_cast<int>(noise % 40U);
        switch (noise % 3U) {
        case 0U: { // Socketed processor with a heat spreader.
            fill_rect(framebuffer, x, ground - 54, 62, 54, kBoardMid);
            fill_rect(framebuffer, x + 6, ground - 48, 50, 34, kBoardLight);
            fill_rect(framebuffer, x + 14, ground - 40, 34, 18, kBoardMid);
            for (int pin = 0; pin < 6; ++pin) {
                fill_rect(framebuffer, x + 6 + pin * 9, ground - 12, 5, 8, kBoardLight);
            }
            break;
        }
        case 1U: { // Capacitor bank.
            for (int can = 0; can < 3; ++can) {
                const int cx = x + can * 22;
                const int height = 40 + static_cast<int>((noise >> (can * 3U)) % 18U);
                fill_rect(framebuffer, cx, ground - height, 15, height, kBoardMid);
                fill_ellipse(framebuffer, cx + 7, ground - height, 7, 4, kBoardLight);
                fill_rect(framebuffer, cx + 2, ground - height + 8, 11, 2, kBoardFar);
            }
            break;
        }
        default: { // Memory slot with a half-inserted module.
            fill_rect(framebuffer, x, ground - 10, 118, 10, kBoardMid);
            fill_rect(framebuffer, x + 8, ground - 62, 96, 52, kBoardFar);
            for (int chip = 0; chip < 4; ++chip) {
                fill_rect(framebuffer, x + 14 + chip * 22, ground - 52, 16, 22, kBoardMid);
            }
            fill_rect(framebuffer, x + 8, ground - 62, 96, 2, shade(accent, 1, 4));
            break;
        }
        }
    }
}

// A dim POST log scrolling behind everything, purely for flavour.
void draw_post_log(const FrameBuffer& framebuffer, const GameState& game) noexcept {
    static const char kLog[] =
        "MEM TEST OK * IRQ 67 ARMED * AURA BUS ONLINE * GOOSE FIRMWARE 67.0 * ";
    const int width = text_width(kLog, 1);
    const std::uint32_t offset = (game.scroll_fp >> 3U) >> 8U;
    const int phase = static_cast<int>(offset % static_cast<std::uint32_t>(width));
    for (int repeat = 0; repeat < 2; ++repeat) {
        draw_text(framebuffer, repeat * width - phase, 58, kLog, 1, shade(kDim, 1, 2));
    }
    const std::uint32_t second = (game.scroll_fp >> 4U) >> 8U;
    const int phase2 = static_cast<int>(second % static_cast<std::uint32_t>(width));
    for (int repeat = 0; repeat < 2; ++repeat) {
        draw_text(framebuffer, repeat * width - phase2, 76, kLog, 1, shade(kDim, 1, 3));
    }
}

// Near layer: the PCB the goose actually runs on.
void draw_ground(const FrameBuffer& framebuffer, const GameState& game, Color accent) noexcept {
    const int ground = static_cast<int>(kGroundY);
    const int height = 338 - ground;
    fill_rect(framebuffer, 0, ground, static_cast<int>(kFrameWidth), height, kSubstrate);
    fill_rect(framebuffer, 0, ground, static_cast<int>(kFrameWidth), 2, accent);
    fill_rect(framebuffer, 0, ground + 2, static_cast<int>(kFrameWidth), 1, shade(accent, 1, 3));

    const std::uint32_t offset = game.scroll_fp >> 8U;
    const int dash_phase = static_cast<int>(offset % 36U);
    for (int x = -36; x < static_cast<int>(kFrameWidth); x += 36) {
        fill_rect(framebuffer, x - dash_phase, ground + 9, 22, 2, kTrace);
        fill_rect(framebuffer, x - dash_phase + 18, ground + 22, 12, 2, kSubstrateDark);
    }
    const int pad_phase = static_cast<int>(offset % 72U);
    for (int x = -72; x < static_cast<int>(kFrameWidth); x += 72) {
        fill_ellipse(framebuffer, x - pad_phase + 30, ground + 16, 5, 4, kTrace);
        fill_ellipse(framebuffer, x - pad_phase + 30, ground + 16, 2, 2, kSubstrateDark);
        fill_rect(framebuffer, x - pad_phase + 54, ground + 28, 14, 3, kTrace);
    }
}

// ---------------------------------------------------------------------------
// Entities
// ---------------------------------------------------------------------------

void draw_ram_stick(const FrameBuffer& framebuffer, int x, int y, int width, int height) noexcept {
    fill_rect(framebuffer, x, y, width, height, kTrace);
    fill_rect(framebuffer, x + 2, y + 2, width - 4, height - 12, shade(kMatrixGreen, 1, 5));
    for (int chip = 0; chip < 2; ++chip) {
        fill_rect(framebuffer, x + 4, y + 6 + chip * 14, width - 8, 10, kOutline);
        fill_rect(framebuffer, x + 6, y + 8 + chip * 14, 3, 2, kDim);
    }
    for (int pin = 0; pin < 6; ++pin) {
        fill_rect(framebuffer, x + 2 + pin * 4, y + height - 6, 2, 6, kGold);
    }
}

void draw_capacitor(const FrameBuffer& framebuffer, int x, int y, int width, int height) noexcept {
    fill_rect(framebuffer, x + 2, y + 4, width - 4, height - 4, kOutline);
    fill_rect(framebuffer, x + 4, y + 6, width - 8, height - 6, shade(kPanicBlue, 1, 2));
    fill_ellipse(framebuffer, x + width / 2, y + 5, width / 2 - 2, 5, kGray);
    fill_rect(framebuffer, x + 5, y + 12, width - 10, 2, kDim);
    draw_text(framebuffer, x + width / 2 - 3, y + 18, "+", 1, kWhite);
    fill_rect(framebuffer, x + 3, y + height - 3, width - 6, 3, kGold);
}

void draw_error_box(const FrameBuffer& framebuffer, int x, int y, int width, int height, std::uint32_t tick) noexcept {
    fill_rect(framebuffer, x, y, width, height, kPanel);
    stroke_rect(framebuffer, x, y, width, height, kCriticalRed);
    fill_rect(framebuffer, x + 1, y + 1, width - 2, 9, kCriticalRed);
    fill_rect(framebuffer, x + width - 9, y + 2, 7, 7, kOutline);
    draw_text(framebuffer, x + 3, y + 2, "ERR", 1, kOutline);
    draw_text(framebuffer, x + 5, y + 15, "SEG", 1, kCriticalRed);
    draw_text(framebuffer, x + 5, y + 24, "FAULT", 1, (tick & 16U) != 0U ? kWhite : kGray);
}

void draw_tower(const FrameBuffer& framebuffer, int x, int y, int width, int height, std::uint32_t tick) noexcept {
    fill_rect(framebuffer, x, y + 4, width, height - 4, kOutline);
    for (int block = 0; block * 16 < height - 8; ++block) {
        fill_rect(framebuffer, x + 2, y + 8 + block * 16, width - 4, 12, shade(kBoardLight, 3, 2));
        fill_rect(framebuffer, x + 4, y + 10 + block * 16, width - 8, 3, kOutline);
    }
    fill_rect(framebuffer, x - 2, y, width + 4, 6, kGray);
    fill_ellipse(framebuffer, x + width / 2, y + 2, 3, 2,
                 (tick & 8U) != 0U ? kCriticalRed : kOutline);
}

void draw_blue_screen(const FrameBuffer& framebuffer, int x, int y, int width, int height, std::uint32_t tick) noexcept {
    fill_rect(framebuffer, x, y, width, height, kPanicBlue);
    stroke_rect(framebuffer, x, y, width, height, kWhite);
    draw_text(framebuffer, x + 6, y + 6, ":(", 2, kWhite);
    draw_text(framebuffer, x + 6, y + 24, "PANIC", 1, kWhite);
    draw_text(framebuffer, x + 6, y + 33, "0X0067", 1, (tick & 8U) != 0U ? kCyan : kWhite);
}

void draw_flying_cursor(const FrameBuffer& framebuffer, int x, int y, std::uint32_t tick) noexcept {
    // The hostile cursor of the old arena, now airborne and flapping.
    const int flap = ((tick / 4U) & 1U) != 0U ? -7 : 5;
    draw_thick_line(framebuffer, x - 4, y + 11, x - 18, y + 11 + flap, 3, kWhite);
    draw_thick_line(framebuffer, x - 18, y + 11 + flap, x - 13, y + 15 + flap / 2, 3, kGray);
    draw_thick_line(framebuffer, x + 18, y + 11, x + 32, y + 11 + flap, 3, kWhite);
    draw_thick_line(framebuffer, x + 32, y + 11 + flap, x + 27, y + 15 + flap / 2, 3, kGray);

    draw_line(framebuffer, x, y, x, y + 22, kWhite);
    draw_line(framebuffer, x, y, x + 15, y + 15, kWhite);
    draw_line(framebuffer, x + 1, y + 1, x + 1, y + 21, kGray);
    draw_line(framebuffer, x, y + 22, x + 5, y + 17, kWhite);
    draw_line(framebuffer, x + 5, y + 17, x + 10, y + 27, kCriticalRed);
    draw_line(framebuffer, x + 10, y + 27, x + 14, y + 25, kCriticalRed);
    draw_line(framebuffer, x + 14, y + 25, x + 9, y + 16, kCriticalRed);
}

void draw_obstacles(const FrameBuffer& framebuffer, const GameState& game, int shake_x, int shake_y) noexcept {
    for (std::size_t index = 0U; index < kObstacleCount; ++index) {
        const ObstacleState& obstacle = game.obstacles[index];
        const int x = fixed_to_pixels(obstacle.x_fp) + shake_x;
        const int y = obstacle.y + shake_y;
        const int width = obstacle.width;
        const int height = obstacle.height;

        if (obstacle.shatter != 0U) {
            // ROOT MODE debris: the wreck keeps travelling for a moment.
            const int spread = (10 - obstacle.shatter) * 3;
            const Color debris = palette_color(game.palette_index);
            fill_rect(framebuffer, x - spread, y - spread, 6, 6, debris);
            fill_rect(framebuffer, x + width + spread - 6, y - spread, 5, 5, debris);
            fill_rect(framebuffer, x + width / 2, y + height / 2 + spread, 4, 4, kWhite);
            continue;
        }
        if (obstacle.active == 0U) {
            continue;
        }

        switch (obstacle.kind) {
        case ObstacleKind::RamStick: draw_ram_stick(framebuffer, x, y, width, height); break;
        case ObstacleKind::Capacitor: draw_capacitor(framebuffer, x, y, width, height); break;
        case ObstacleKind::ErrorBox: draw_error_box(framebuffer, x, y, width, height, game.tick); break;
        case ObstacleKind::Tower: draw_tower(framebuffer, x, y, width, height, game.tick); break;
        case ObstacleKind::Cursor: draw_flying_cursor(framebuffer, x, y, game.tick); break;
        case ObstacleKind::BlueScreen: draw_blue_screen(framebuffer, x, y, width, height, game.tick); break;
        }
    }
}

void draw_pickups(const FrameBuffer& framebuffer, const GameState& game, int shake_x, int shake_y) noexcept {
    const int pulse = static_cast<int>((game.tick / 4U) & 3U);
    for (std::size_t index = 0U; index < kPickupCount; ++index) {
        const PickupState& pickup = game.pickups[index];
        if (pickup.active == 0U) {
            continue;
        }
        const int x = fixed_to_pixels(pickup.x_fp) + shake_x;
        const int y = pickup.y + shake_y;

        if (pickup.kind == PickupKind::Overclock) {
            fill_rect(framebuffer, x - 11, y - 11, 22, 22, kOutline);
            fill_rect(framebuffer, x - 9, y - 9, 18, 18, kNeonPink);
            fill_rect(framebuffer, x - 6, y - 6, 12, 12, kOutline);
            draw_text(framebuffer, x - 2, y - 3, "*", 1, kGold);
            for (int pin = 0; pin < 4; ++pin) {
                const int pin_y = y - 8 + pin * 5;
                fill_rect(framebuffer, x - 14, pin_y, 3, 2, kGold);
                fill_rect(framebuffer, x + 11, pin_y, 3, 2, kGold);
            }
            stroke_rect(framebuffer, x - 13 - pulse, y - 13 - pulse,
                        26 + pulse * 2, 26 + pulse * 2, shade(kNeonPink, 4 - pulse, 4));
            continue;
        }

        fill_ellipse(framebuffer, x, y, 11, 11, kMatrixGreen);
        fill_ellipse(framebuffer, x, y, 8, 8, kPanel);
        fill_rect(framebuffer, x - 5, y - 1, 11, 3, kMatrixGreen);
        fill_rect(framebuffer, x - 1, y - 5, 3, 11, kMatrixGreen);
        if (pulse == 0) {
            stroke_rect(framebuffer, x - 13, y - 13, 26, 26, shade(kMatrixGreen, 1, 2));
        }
    }
}

void draw_dust(const FrameBuffer& framebuffer, const GameState& game, Color accent) noexcept {
    for (std::size_t index = 0U; index < kDustCount; ++index) {
        const DustState& particle = game.dust[index];
        if (particle.ticks == 0U) {
            continue;
        }
        const int x = fixed_to_pixels(particle.x_fp);
        const int y = fixed_to_pixels(particle.y_fp);
        const int size = particle.ticks > 14U ? 3 : 2;
        fill_rect(framebuffer, x, y, size, size,
                  particle.ticks > 10U ? accent : shade(accent, 1, 2));
    }
}

void draw_popups(const FrameBuffer& framebuffer, const GameState& game, Color accent) noexcept {
    for (std::size_t index = 0U; index < kPopupCount; ++index) {
        const PopupState& popup = game.popups[index];
        if (popup.ticks == 0U) {
            continue;
        }
        const int x = fixed_to_pixels(popup.x_fp);
        const int y = popup.y;
        const bool blink = popup.ticks > 6U || (popup.ticks & 1U) != 0U;
        if (!blink) {
            continue;
        }

        switch (popup.kind) {
        case PopupKind::Aura: {
            char text[12]{};
            text[0] = '+';
            format_integer(popup.value, text + 1, sizeof(text) - 1U);
            draw_text(framebuffer, x, y, text, 2, accent);
            break;
        }
        case PopupKind::Clutch:
            draw_text(framebuffer, x, y, "CLUTCH", 2, kNeonPink);
            draw_text(framebuffer, x + 12, y + 16, "+67", 1, kWhite);
            break;
        case PopupKind::Root:
            draw_text_centered(framebuffer, x, y, "ROOT MODE", 2, kGold);
            break;
        default:
            draw_text_centered(framebuffer, x, y, "BIOS WAVE", 2, accent);
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// The goose
// ---------------------------------------------------------------------------

void draw_goose_legs(const FrameBuffer& framebuffer, const GameState& game, int center_x, int feet) noexcept {
    if (game.runner.on_ground == 0U) {
        // Tucked back mid-air, angled by the direction of travel.
        const int tuck = game.runner.velocity_y_fp < 0 ? 5 : -3;
        draw_thick_line(framebuffer, center_x - 3, feet - 14, center_x - 15, feet - 6 + tuck, 3, kGooseOrange);
        draw_thick_line(framebuffer, center_x - 15, feet - 6 + tuck, center_x - 21, feet - 4 + tuck, 2, kGooseOrange);
        draw_thick_line(framebuffer, center_x + 3, feet - 14, center_x - 9, feet - 2 + tuck, 3, kGooseOrangeDark);
        return;
    }
    if (game.runner.ducking != 0U) {
        draw_thick_line(framebuffer, center_x - 7, feet - 7, center_x - 13, feet - 1, 3, kGooseOrangeDark);
        draw_thick_line(framebuffer, center_x + 5, feet - 7, center_x + 11, feet - 1, 3, kGooseOrange);
        return;
    }

    // Four-frame run cycle: hip, knee, foot.
    static constexpr int kFrontSwing[4] = {9, 2, -7, 2};
    static constexpr int kBackSwing[4] = {-7, 2, 9, 2};
    const int stride = static_cast<int>(game.runner.stride & 3U);
    const int front = kFrontSwing[stride];
    const int back = kBackSwing[stride];

    draw_thick_line(framebuffer, center_x - 3, feet - 15, center_x - 3 + back, feet - 4, 3, kGooseOrangeDark);
    draw_thick_line(framebuffer, center_x - 3 + back, feet - 4, center_x + 3 + back, feet - 1, 3, kGooseOrangeDark);
    draw_thick_line(framebuffer, center_x + 5, feet - 15, center_x + 5 + front, feet - 4, 3, kGooseOrange);
    draw_thick_line(framebuffer, center_x + 5 + front, feet - 4, center_x + 11 + front, feet - 1, 3, kGooseOrange);
}

void draw_goose(const FrameBuffer& framebuffer, const GameState& game, int shake_x, int shake_y) noexcept {
    const int center_x = kRunnerX + shake_x;
    const int feet = fixed_to_pixels(game.runner.y_fp) + shake_y;
    const bool ducking = game.runner.ducking != 0U;
    const bool rooted = game.root_mode_ticks != 0U;
    const bool dead = game.phase == GamePhase::Finished;
    const Color accent = palette_color(game.palette_index);
    const Color body = dead ? shade(kWhite, 3, 5) : kWhite;
    const Color outline = rooted ? accent : kOutline;
    const int halo = rooted ? 3 : 0;

    if (rooted) {
        // Overclock after-images trailing the runner.
        for (int ghost = 1; ghost <= 3; ++ghost) {
            fill_ellipse(framebuffer, center_x - ghost * 15, feet - (ducking ? 13 : 25),
                         20 - ghost * 3, (ducking ? 10 : 13) - ghost * 2,
                         shade(accent, 4 - ghost, 10));
        }
        const int spark = static_cast<int>((game.tick / 2U) & 3U);
        for (int corner = 0; corner < 4; ++corner) {
            const int angle = (corner + spark) & 3;
            const int dx = (angle == 0 || angle == 3) ? 30 : -30;
            const int dy = (angle < 2) ? -34 : 2;
            fill_rect(framebuffer, center_x + dx, feet + dy, 4, 4, kGold);
        }
    }

    // Contact shadow on the PCB: tightens as the goose climbs.
    const int shadow_scale = 22 - (static_cast<int>(kGroundY) - feet) / 8;
    fill_ellipse(framebuffer, center_x + 2, static_cast<int>(kGroundY) + 3,
                 shadow_scale < 8 ? 8 : shadow_scale, 4, kSubstrateDark);

    draw_goose_legs(framebuffer, game, center_x, feet);

    if (ducking) {
        fill_ellipse(framebuffer, center_x - 1, feet - 13, 25 + halo, 11 + halo, outline);
        fill_ellipse(framebuffer, center_x - 1, feet - 14, 23, 9, body);
        fill_ellipse(framebuffer, center_x - 22, feet - 19, 9, 6, outline); // Tail up.
        fill_ellipse(framebuffer, center_x - 22, feet - 19, 7, 4, body);
        draw_thick_line(framebuffer, center_x + 14, feet - 16, center_x + 24, feet - 19, 9, outline);
        draw_thick_line(framebuffer, center_x + 14, feet - 16, center_x + 24, feet - 19, 6, body);
        fill_ellipse(framebuffer, center_x + 26, feet - 19, 9, 7, outline);
        fill_ellipse(framebuffer, center_x + 26, feet - 19, 7, 5, body);
        fill_ellipse(framebuffer, center_x + 36, feet - 18, 7, 3, kGooseOrange);
        fill_ellipse(framebuffer, center_x + 28, feet - 22, 2, 2, kOutline);
        fill_ellipse(framebuffer, center_x - 4, feet - 13, 11, 4, shade(body, 6, 7));
        return;
    }

    const int bob = game.runner.on_ground != 0U && (game.runner.stride & 1U) != 0U ? 1 : 0;
    const int body_y = feet - 25 + bob;

    // Tail, body, wing.
    fill_ellipse(framebuffer, center_x - 20, body_y - 5, 9, 6, outline);
    fill_ellipse(framebuffer, center_x - 20, body_y - 5, 7, 4, body);
    fill_ellipse(framebuffer, center_x, body_y, 20 + halo, 13 + halo, outline);
    fill_ellipse(framebuffer, center_x, body_y, 19, 12, body);

    const int wing_lift = game.runner.on_ground != 0U
                              ? ((game.runner.stride & 2U) != 0U ? 1 : -1)
                              : (game.runner.flaps_used != 0U ? -8 : -3);
    fill_ellipse(framebuffer, center_x - 3, body_y + 2 + wing_lift, 12, 6, shade(body, 6, 7));
    draw_line(framebuffer, center_x - 14, body_y + 1 + wing_lift,
              center_x + 8, body_y + 3 + wing_lift, shade(body, 4, 7));

    // Neck, head, beak, eye.
    draw_thick_line(framebuffer, center_x + 9, body_y - 7, center_x + 16, feet - 42 + bob, 9, outline);
    draw_thick_line(framebuffer, center_x + 9, body_y - 7, center_x + 16, feet - 42 + bob, 6, body);
    fill_ellipse(framebuffer, center_x + 18, feet - 44 + bob, 10, 8, outline);
    fill_ellipse(framebuffer, center_x + 18, feet - 44 + bob, 8, 6, body);
    fill_ellipse(framebuffer, center_x + 28, feet - 43 + bob, 7, 3, kGooseOrange);
    fill_rect(framebuffer, center_x + 23, feet - 42 + bob, 4, 1, kGooseOrangeDark);

    if (dead) {
        // Dead goose: crossed-out eye, the only frame that ever shows it.
        draw_line(framebuffer, center_x + 18, feet - 50 + bob, center_x + 24, feet - 44 + bob, kCriticalRed);
        draw_line(framebuffer, center_x + 24, feet - 50 + bob, center_x + 18, feet - 44 + bob, kCriticalRed);
    } else {
        fill_ellipse(framebuffer, center_x + 21, feet - 47 + bob, 2, 2, kOutline);
    }
}

// ---------------------------------------------------------------------------
// Firmware chrome
// ---------------------------------------------------------------------------

void draw_hud(const FrameBuffer& framebuffer, const GameState& game, Color accent) noexcept {
    fill_rect(framebuffer, 0, 0, static_cast<int>(kFrameWidth), 44, kPanel);
    fill_rect(framebuffer, 0, 44, static_cast<int>(kFrameWidth), 2, accent);
    fill_rect(framebuffer, 0, 46, static_cast<int>(kFrameWidth), 1, shade(accent, 1, 3));

    draw_text(framebuffer, 12, 6, "AURA 67", 3, accent);
    draw_text(framebuffer, 14, 30, "POST RUNNER", 1, kDim);

    char buffer[16]{};
    format_padded(game.score, 6U, buffer, sizeof(buffer));
    draw_text(framebuffer, 168, 4, "AURA", 1, kDim);
    draw_text(framebuffer, 168, 14, buffer, 3, game.maximum_brainrot != 0U ? kNeonPink : kWhite);

    format_padded(game.best_score, 6U, buffer, sizeof(buffer));
    draw_text(framebuffer, 300, 4, "HI", 1, kDim);
    draw_text(framebuffer, 300, 16, buffer, 2, kGray);

    // Bus clock: the visible face of the endless speed ramp.
    format_integer(game_speed_pixels_per_second(game), buffer, sizeof(buffer));
    draw_text(framebuffer, 384, 4, "BUS", 1, kDim);
    draw_text(framebuffer, 384, 16, buffer, 2, kGray);
    const int ramp = 74 * game_difficulty_q8(game) / 256;
    stroke_rect(framebuffer, 384, 34, 76, 6, shade(accent, 1, 2));
    fill_rect(framebuffer, 385, 35, ramp, 4, accent);

    if (game.root_mode_ticks != 0U) {
        const int bar = 96 * static_cast<int>(game.root_mode_ticks) / static_cast<int>(kRootModeTicks);
        draw_text(framebuffer, 476, 4, "ROOT MODE", 1, kGold);
        fill_rect(framebuffer, 476, 16, bar, 10, kGold);
        stroke_rect(framebuffer, 476, 16, 96, 10, shade(kGold, 1, 2));
        draw_text(framebuffer, 476, 30, "SMASH EVERYTHING", 1, kNeonPink);
    } else if (game.combo > 1U) {
        char combo[8]{};
        combo[0] = 'X';
        format_integer(static_cast<std::int32_t>(game.combo), combo + 1, sizeof(combo) - 1U);
        draw_text(framebuffer, 476, 4, "COMBO", 1, kDim);
        draw_text(framebuffer, 476, 14, combo, 3, kNeonPink);
        const int decay = 60 * static_cast<int>(game.combo_ticks) / static_cast<int>(kComboDecayTicks);
        fill_rect(framebuffer, 512, 30, decay, 4, shade(kNeonPink, 2, 3));
    } else {
        draw_text(framebuffer, 476, 4, "COMBO", 1, kDim);
        draw_text(framebuffer, 476, 14, "X1", 3, kDim);
    }

    if (game.maximum_brainrot != 0U) {
        draw_text(framebuffer, 596, 30, "MAX", 1, (game.tick & 16U) != 0U ? kNeonPink : kCriticalRed);
    }
}

void draw_status_bar(const FrameBuffer& framebuffer, const GameState& game, Color accent) noexcept {
    fill_rect(framebuffer, 0, 338, static_cast<int>(kFrameWidth), 22, kPanel);
    fill_rect(framebuffer, 0, 338, static_cast<int>(kFrameWidth), 1, shade(accent, 1, 2));
    draw_text(framebuffer, 8, 346, "SPACE JUMP", 1, kWhite);
    draw_text(framebuffer, 74, 346, "AGAIN FLAP", 1, kDim);
    draw_text(framebuffer, 148, 346, "DOWN DUCK", 1, kWhite);
    draw_text(framebuffer, 214, 346, "HOLD ESC EXIT", 1, kDim);

    char buffer[16]{};
    format_integer(static_cast<std::int32_t>(game.distance_px / 10U), buffer, sizeof(buffer));
    draw_text(framebuffer, 330, 346, "DIST", 1, kDim);
    draw_text(framebuffer, 360, 346, buffer, 1, accent);

    format_integer(static_cast<std::int32_t>(game.badges_collected), buffer, sizeof(buffer));
    draw_text(framebuffer, 436, 346, "BADGE", 1, kDim);
    draw_text(framebuffer, 472, 346, buffer, 1, kMatrixGreen);

    format_integer(static_cast<std::int32_t>(game.clutch_count), buffer, sizeof(buffer));
    draw_text(framebuffer, 512, 346, "CLUTCH", 1, kDim);
    draw_text(framebuffer, 554, 346, buffer, 1, kNeonPink);

    if ((game.tick & 16U) != 0U || game.phase == GamePhase::Finished) {
        fill_rect(framebuffer, 596, 345, 5, 8, accent);
    }
}

void draw_intro_banner(const FrameBuffer& framebuffer, const GameState& game, Color accent) noexcept {
    if (game.tick >= 62U) {
        return;
    }
    draw_text_centered(framebuffer, 320, 120, "POST COMPLETE", 2, kDim);
    if ((game.tick & 8U) != 0U) {
        draw_text_centered(framebuffer, 320, 146, "RUN GOOSE RUN", 3, accent);
    }
    draw_text_centered(framebuffer, 320, 180, "SPACE TO JUMP  DOWN TO DUCK", 1, kGray);
}

void draw_panic_screen(const FrameBuffer& framebuffer, const GameState& game, Color accent) noexcept {
    const int x = 78;
    const int y = 88;
    const int width = 484;
    const int height = 172;
    fill_rect(framebuffer, x + 6, y + 6, width, height, Color{0U, 0U, 0U});
    fill_rect(framebuffer, x, y, width, height, kPanel);
    stroke_rect(framebuffer, x, y, width, height, kCriticalRed);
    stroke_rect(framebuffer, x + 4, y + 4, width - 8, height - 8, kNeonPink);
    fill_rect(framebuffer, x + 1, y + 1, width - 2, 14, kCriticalRed);
    draw_text(framebuffer, x + 6, y + 4, "AURA67.SYS  UNRECOVERABLE", 1, kOutline);

    draw_text_centered(framebuffer, 320, y + 26, "KERNEL PANIC", 3, kCriticalRed);

    LineBuilder line{};
    line.reset();
    line.append("AURA ");
    line.append_padded(game.score, 6U);
    line.append("   BEST ");
    line.append_padded(game.best_score, 6U);
    draw_text_centered(framebuffer, 320, y + 58, line.text, 2, kWhite);

    if (game.record_beaten != 0U && (game.death_ticks & 8U) != 0U) {
        draw_text_centered(framebuffer, 320, y + 80, "NEW RECORD", 1, kGold);
    }

    // Post-mortem register dump: every counter is a real run statistic.
    line.reset();
    line.append("BADGE ");
    line.append_number(static_cast<std::int32_t>(game.badges_collected));
    line.append("  CLUTCH ");
    line.append_number(static_cast<std::int32_t>(game.clutch_count));
    line.append("  CLEARED ");
    line.append_number(static_cast<std::int32_t>(game.obstacles_cleared));
    draw_text_centered(framebuffer, 320, y + 94, line.text, 1, accent);

    line.reset();
    line.append("SURVIVED ");
    line.append_number(static_cast<std::int32_t>(game_run_seconds(game)));
    line.append(" SEC ON RUN ");
    line.append_number(static_cast<std::int32_t>(game.run_index + 1U));
    draw_text_centered(framebuffer, 320, y + 108, line.text, 1, kDim);

    if (game.death_ticks < 20U) {
        draw_text_centered(framebuffer, 320, y + 134, "MEMORY DUMP IN PROGRESS", 2, kDim);
    } else if ((game.death_ticks & 16U) != 0U) {
        draw_text_centered(framebuffer, 320, y + 134, "[SPACE] RUN AGAIN", 2, kWhite);
    } else {
        draw_text_centered(framebuffer, 320, y + 134, "[R] RESET PLATFORM", 2, kGray);
    }
}

} // namespace

bool game_render(const GameState& game, const FrameBuffer& framebuffer) noexcept {
    if (!framebuffer_is_valid(framebuffer)) {
        return false;
    }

    const Color accent = palette_color(game.palette_index);

    int shake_x = 0;
    int shake_y = 0;
    if (game.phase == GamePhase::Finished && game.death_ticks < 12U) {
        static constexpr int kShakeX[4] = {-5, 4, -3, 6};
        static constexpr int kShakeY[4] = {3, -4, 5, -2};
        const std::size_t phase = static_cast<std::size_t>(game.death_ticks & 3U);
        shake_x = kShakeX[phase];
        shake_y = kShakeY[phase];
    } else if (game.flash_ticks != 0U) {
        shake_x = (game.flash_ticks & 1U) != 0U ? 2 : -2;
    }

    draw_sky(framebuffer);
    draw_sky_furniture(framebuffer, game, accent);
    draw_post_log(framebuffer, game);
    draw_memory_map(framebuffer, game);
    draw_board_props(framebuffer, game, accent);
    draw_ground(framebuffer, game, accent);

    draw_pickups(framebuffer, game, shake_x, shake_y);
    draw_obstacles(framebuffer, game, shake_x, shake_y);
    draw_dust(framebuffer, game, accent);
    draw_goose(framebuffer, game, shake_x, shake_y);
    draw_popups(framebuffer, game, accent);

    if (game.flash_ticks != 0U) {
        stroke_rect(framebuffer, 0, 47, static_cast<int>(kFrameWidth), 291, accent);
        stroke_rect(framebuffer, 1, 48, static_cast<int>(kFrameWidth) - 2, 289, accent);
    }
    if (game.root_mode_ticks != 0U) {
        stroke_rect(framebuffer, 2, 49, static_cast<int>(kFrameWidth) - 4, 287, shade(kGold, 1, 2));
    }

    draw_hud(framebuffer, game, accent);
    draw_status_bar(framebuffer, game, accent);
    if (game.phase == GamePhase::Playing) {
        draw_intro_banner(framebuffer, game, accent);
    } else {
        draw_panic_screen(framebuffer, game, accent);
    }
    return true;
}

} // namespace aura67
