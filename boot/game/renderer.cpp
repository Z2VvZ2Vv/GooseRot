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

constexpr Color kBlack{5U, 7U, 13U};
constexpr Color kWhite{255U, 251U, 234U};
constexpr Color kGooseOutline{25U, 25U, 32U};
constexpr Color kGooseOrange{245U, 143U, 39U};
constexpr Color kNeonPink{255U, 45U, 170U};
constexpr Color kMatrixGreen{57U, 255U, 20U};
constexpr Color kCriticalRed{255U, 36U, 56U};
constexpr Color kPanel{15U, 20U, 35U};
constexpr Color kPanelLight{30U, 38U, 58U};
constexpr Color kShadow{9U, 10U, 17U};

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
    {'.', {0, 0, 0, 0, 0, 4, 4}},
};

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
    for (int py = top; py < bottom; ++py) {
        for (int px = left; px < right; ++px) {
            put_pixel(framebuffer, px, py, color);
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

const std::uint8_t* glyph_rows(char character) noexcept {
    for (std::size_t index = 0U; index < sizeof(kGlyphs) / sizeof(kGlyphs[0]); ++index) {
        if (kGlyphs[index].character == character) {
            return kGlyphs[index].rows;
        }
    }
    return nullptr;
}

void draw_character(
    const FrameBuffer& framebuffer,
    int x,
    int y,
    char character,
    int scale,
    Color color) noexcept {
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

void draw_text(
    const FrameBuffer& framebuffer,
    int x,
    int y,
    const char* text,
    int scale,
    Color color) noexcept {
    int cursor_x = x;
    for (const char* current = text; *current != '\0'; ++current) {
        if (*current != ' ') {
            draw_character(framebuffer, cursor_x, y, *current, scale, color);
        }
        cursor_x += 6 * scale;
    }
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

Color palette_color(std::uint8_t palette) noexcept {
    switch (palette % 3U) {
    case 0U: return kMatrixGreen;
    case 1U: return kNeonPink;
    default: return kCriticalRed;
    }
}

void transform_local(
    Facing facing,
    int center_x,
    int center_y,
    int forward,
    int side,
    int* output_x,
    int* output_y) noexcept {
    int forward_x = 1;
    int forward_y = 0;
    switch (facing) {
    case Facing::Right: forward_x = 1; forward_y = 0; break;
    case Facing::Down: forward_x = 0; forward_y = 1; break;
    case Facing::Left: forward_x = -1; forward_y = 0; break;
    case Facing::Up: forward_x = 0; forward_y = -1; break;
    }
    *output_x = center_x + forward_x * forward - forward_y * side;
    *output_y = center_y + forward_y * forward + forward_x * side;
}

void draw_goose(const FrameBuffer& framebuffer, const GameState& game, int shake_x, int shake_y) noexcept {
    const int center_x = fixed_to_pixels(game.goose.x_fp) + shake_x;
    const int center_y = fixed_to_pixels(game.goose.y_fp) + shake_y;
    int point_x = 0;
    int point_y = 0;

    // Clean-room procedural silhouette. Dimensions follow the public
    // GooseModdingAPI rig constants, while the raster implementation is new.
    fill_ellipse(framebuffer, center_x + 3, center_y + 7, 27, 18, kShadow);

    const int foot_phase = static_cast<int>((game.tick / 3U) & 1U);
    transform_local(game.goose.facing, center_x, center_y, -6 + foot_phase * 3, -10, &point_x, &point_y);
    fill_ellipse(framebuffer, point_x, point_y, 6, 3, kGooseOrange);
    transform_local(game.goose.facing, center_x, center_y, -3 - foot_phase * 3, 10, &point_x, &point_y);
    fill_ellipse(framebuffer, point_x, point_y, 6, 3, kGooseOrange);

    fill_ellipse(framebuffer, center_x, center_y, 27, 22, kGooseOutline);
    fill_ellipse(framebuffer, center_x, center_y - 1, 25, 20, kWhite);

    int neck_x = 0;
    int neck_y = 0;
    transform_local(game.goose.facing, center_x, center_y, 19, -7, &neck_x, &neck_y);
    fill_ellipse(framebuffer, neck_x, neck_y, 14, 14, kGooseOutline);
    fill_ellipse(framebuffer, neck_x, neck_y, 12, 12, kWhite);

    int head_x = 0;
    int head_y = 0;
    transform_local(game.goose.facing, center_x, center_y, 31, -10, &head_x, &head_y);
    fill_ellipse(framebuffer, head_x, head_y, 13, 11, kGooseOutline);
    fill_ellipse(framebuffer, head_x, head_y, 11, 9, kWhite);

    int beak_x = 0;
    int beak_y = 0;
    transform_local(game.goose.facing, center_x, center_y, 44, -10, &beak_x, &beak_y);
    fill_ellipse(framebuffer, beak_x, beak_y, 8, 4, kGooseOrange);

    int eye_x = 0;
    int eye_y = 0;
    transform_local(game.goose.facing, center_x, center_y, 34, -15, &eye_x, &eye_y);
    fill_ellipse(framebuffer, eye_x, eye_y, 2, 2, kGooseOutline);
}

void draw_badges(const FrameBuffer& framebuffer, const GameState& game, int shake_x, int shake_y) noexcept {
    for (std::size_t index = 0U; index < kBadgeCount; ++index) {
        const BadgeState& badge = game.badges[index];
        if (badge.active == 0U) {
            continue;
        }
        const int x = fixed_to_pixels(badge.x_fp) + shake_x;
        const int y = fixed_to_pixels(badge.y_fp) + shake_y;
        fill_ellipse(framebuffer, x, y, 12, 12, kMatrixGreen);
        fill_ellipse(framebuffer, x, y, 9, 9, kPanel);
        fill_rect(framebuffer, x - 5, y - 1, 11, 3, kMatrixGreen);
        fill_rect(framebuffer, x - 1, y - 5, 3, 11, kMatrixGreen);
    }
}

void draw_npcs(const FrameBuffer& framebuffer, const GameState& game, int shake_x, int shake_y) noexcept {
    for (std::size_t index = 0U; index < kNpcCount; ++index) {
        const NpcState& npc = game.npcs[index];
        if (npc.active == 0U) {
            continue;
        }
        const int x = fixed_to_pixels(npc.x_fp) + shake_x;
        const int y = fixed_to_pixels(npc.y_fp) + shake_y;
        fill_rect(framebuffer, x - 17, y - 12, 34, 24, kCriticalRed);
        fill_rect(framebuffer, x - 14, y - 9, 28, 18, kPanel);
        draw_text(framebuffer, x - 9, y - 3, "NPC", 1, kCriticalRed);
    }
}

void draw_cursor(const FrameBuffer& framebuffer, const GameState& game, int shake_x, int shake_y) noexcept {
    const int x = fixed_to_pixels(game.hostile_cursor.x_fp) + shake_x;
    const int y = fixed_to_pixels(game.hostile_cursor.y_fp) + shake_y;
    draw_line(framebuffer, x, y, x, y + 22, kWhite);
    draw_line(framebuffer, x, y, x + 15, y + 15, kWhite);
    draw_line(framebuffer, x, y + 22, x + 5, y + 17, kWhite);
    draw_line(framebuffer, x + 5, y + 17, x + 10, y + 27, kCriticalRed);
    draw_line(framebuffer, x + 10, y + 27, x + 14, y + 25, kCriticalRed);
    draw_line(framebuffer, x + 14, y + 25, x + 9, y + 16, kCriticalRed);
}

void draw_hud(const FrameBuffer& framebuffer, const GameState& game) noexcept {
    const Color accent = palette_color(game.palette_index);
    fill_rect(framebuffer, 0, 0, static_cast<int>(kFrameWidth), 52, kPanel);
    fill_rect(framebuffer, 0, 51, static_cast<int>(kFrameWidth), 2, accent);
    draw_text(framebuffer, 16, 13, "AURA 67", 3, accent);

    char score[16]{};
    char time[16]{};
    format_integer(game.score, score, sizeof(score));
    format_integer(static_cast<std::int32_t>(game_seconds_remaining(game)), time, sizeof(time));
    draw_text(framebuffer, 284, 9, "SCORE", 1, kWhite);
    draw_text(framebuffer, 284, 24, score, 2, accent);
    draw_text(framebuffer, 530, 9, "TIME", 1, kWhite);
    draw_text(framebuffer, 530, 24, time, 2, kCriticalRed);

    fill_rect(framebuffer, 0, 338, static_cast<int>(kFrameWidth), 22, kPanel);
    draw_text(framebuffer, 10, 346, "ARROWS/WASD MOVE  SPACE DASH  ESC HOLD EXIT", 1, kWhite);
}

void draw_background(const FrameBuffer& framebuffer, const GameState& game) noexcept {
    fill_rect(framebuffer, 0, 0, static_cast<int>(kFrameWidth), static_cast<int>(kFrameHeight), kBlack);
    const Color accent = palette_color(game.palette_index);
    for (int y = 61; y < 338; y += 20) {
        fill_rect(framebuffer, 0, y, static_cast<int>(kFrameWidth), 1, kPanelLight);
    }
    for (int x = 0; x < static_cast<int>(kFrameWidth); x += 32) {
        fill_rect(framebuffer, x, 53, 1, 285, kPanelLight);
    }
    stroke_rect(framebuffer, 7, 59, 626, 272, accent);
}

void draw_finished(const FrameBuffer& framebuffer, const GameState& game) noexcept {
    fill_rect(framebuffer, 50, 100, 540, 160, kPanel);
    stroke_rect(framebuffer, 50, 100, 540, 160, kCriticalRed);
    stroke_rect(framebuffer, 54, 104, 532, 152, kNeonPink);
    draw_text(framebuffer, 116, 124, "CRITICAL ERROR:", 3, kCriticalRed);
    draw_text(framebuffer, 92, 166, "MAXIMUM BRAINROT REACHED", 2, kMatrixGreen);
    draw_text(framebuffer, 116, 213, "[R] REBOOT    [ENTER] AGAIN", 2, kWhite);
    if (game.maximum_brainrot == 0U) {
        draw_text(framebuffer, 236, 242, "AURA INCOMPLETE", 1, kNeonPink);
    }
}

} // namespace

bool game_render(const GameState& game, const FrameBuffer& framebuffer) noexcept {
    if (!framebuffer_is_valid(framebuffer)) {
        return false;
    }

    draw_background(framebuffer, game);
    int shake_x = 0;
    int shake_y = 0;
    if (game.phase == GamePhase::Playing && game.tick % 60U < 4U) {
        static constexpr int kShakeX[4] = {-2, 3, -4, 2};
        static constexpr int kShakeY[4] = {2, -3, 2, -2};
        const std::size_t phase = static_cast<std::size_t>(game.tick % 4U);
        shake_x = kShakeX[phase];
        shake_y = kShakeY[phase];
    }

    draw_badges(framebuffer, game, shake_x, shake_y);
    draw_npcs(framebuffer, game, shake_x, shake_y);
    draw_cursor(framebuffer, game, shake_x, shake_y);
    draw_goose(framebuffer, game, shake_x, shake_y);
    draw_hud(framebuffer, game);
    if (game.phase == GamePhase::Finished) {
        draw_finished(framebuffer, game);
    }
    return true;
}

} // namespace aura67

