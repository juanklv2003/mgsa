#include "ui.hpp"

#include <cstring>

namespace mgsa {
namespace {

// Classic 5x7 ASCII bitmap font, rows of 5 bits packed column-by-column.
// Each glyph is 5 bytes: bit 0 = top row ... bit 6 = bottom row.
struct Glyph { char c; std::uint8_t cols[5]; };

static const Glyph kFont[] = {
    {' ', {0x00,0x00,0x00,0x00,0x00}},
    {'A', {0x7E,0x11,0x11,0x11,0x7E}},
    {'B', {0x7F,0x49,0x49,0x49,0x36}},
    {'C', {0x7E,0x41,0x41,0x41,0x41}},
    {'D', {0x7F,0x41,0x41,0x41,0x7E}},
    {'E', {0x7F,0x49,0x49,0x49,0x49}},
    {'F', {0x7F,0x09,0x09,0x09,0x01}},
    {'G', {0x7E,0x41,0x49,0x49,0x7E}},
    {'H', {0x7F,0x08,0x08,0x08,0x7F}},
    {'I', {0x00,0x41,0x7F,0x41,0x00}},
    {'J', {0x20,0x40,0x41,0x3F,0x01}},
    {'K', {0x7F,0x08,0x14,0x22,0x41}},
    {'L', {0x7F,0x40,0x40,0x40,0x40}},
    {'M', {0x7F,0x02,0x0C,0x02,0x7F}},
    {'N', {0x7F,0x04,0x08,0x10,0x7F}},
    {'O', {0x7E,0x41,0x41,0x41,0x7E}},
    {'P', {0x7F,0x09,0x09,0x09,0x06}},
    {'Q', {0x7E,0x41,0x51,0x21,0x5E}},
    {'R', {0x7F,0x09,0x19,0x29,0x46}},
    {'S', {0x46,0x49,0x49,0x49,0x31}},
    {'T', {0x01,0x01,0x7F,0x01,0x01}},
    {'U', {0x7F,0x40,0x40,0x40,0x7F}},
    {'V', {0x1F,0x20,0x40,0x20,0x1F}},
    {'W', {0x7F,0x20,0x18,0x20,0x7F}},
    {'X', {0x63,0x14,0x08,0x14,0x63}},
    {'Y', {0x03,0x04,0x78,0x04,0x03}},
    {'Z', {0x61,0x51,0x49,0x45,0x43}},
    {'0', {0x7E,0x81,0x81,0x81,0x7E}},
    {'1', {0x00,0x42,0x7F,0x40,0x00}},
    {'2', {0x42,0x61,0x51,0x49,0x46}},
    {'3', {0x21,0x41,0x45,0x4B,0x31}},
    {'4', {0x18,0x14,0x12,0x7F,0x10}},
    {'5', {0x27,0x45,0x45,0x45,0x39}},
    {'6', {0x3C,0x4A,0x49,0x49,0x30}},
    {'7', {0x01,0x71,0x09,0x05,0x03}},
    {'8', {0x36,0x49,0x49,0x49,0x36}},
    {'9', {0x06,0x49,0x49,0x29,0x1E}},
    {':', {0x00,0x36,0x36,0x00,0x00}},
    {'.', {0x00,0x60,0x60,0x00,0x00}},
    {',', {0x00,0x50,0x30,0x00,0x00}},
    {'(', {0x00,0x1C,0x22,0x41,0x00}},
    {')', {0x00,0x41,0x22,0x1C,0x00}},
    {'/', {0x20,0x10,0x08,0x04,0x02}},
    {'-', {0x08,0x08,0x08,0x08,0x08}},
    {'+', {0x08,0x08,0x3E,0x08,0x08}},
    {'!', {0x00,0x5F,0x00,0x00,0x00}},
    {'?', {0x02,0x01,0x51,0x09,0x06}},
    {'\'',{0x00,0x05,0x03,0x00,0x00}},
};

const Glyph *find_glyph(char c) {
    // The embedded font is uppercase-only; game strings are mixed case.
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    for (const auto &g : kFont) if (g.c == c) return &g;
    return nullptr;  // unknown glyph = blank
}

void plot(std::uint8_t *rgba, int bw, int bh, int x, int y, std::uint32_t color) {
    if (x < 0 || y < 0 || x >= bw || y >= bh) return;
    const std::size_t o = (static_cast<std::size_t>(y) * bw + x) * 4u;
    rgba[o + 0] = static_cast<std::uint8_t>(color >> 16);
    rgba[o + 1] = static_cast<std::uint8_t>(color >> 8);
    rgba[o + 2] = static_cast<std::uint8_t>(color);
    rgba[o + 3] = static_cast<std::uint8_t>(color >> 24);
}

constexpr std::uint32_t kTextColor = 0xFFF2F2F2u;
constexpr std::uint32_t kPanelColor = 0xC0101828u;
constexpr std::uint32_t kHiliteColor = 0xFF2850A0u;

// Menu geometry in the 480x272 board space.
constexpr int kMenuX = 24;
constexpr int kMenuY = 56;
constexpr int kRowH = 26;
constexpr int kRowW = 432;

}  // namespace

const MenuItem kMenuItems[] = {
    {"End the turn.",                 0x4000u},  // CROSS - confirm
    {"Attack the enemy you are facing", 0x1000u},// TRIANGLE - attack
    {"Knock on the wall. (COST: 0)",  0x8000u},  // SQUARE - interact
    {"Display the System Menu.",      0x0008u},  // START
    {"Cancel the current mission",    0x2000u},  // CIRCLE - back
};
const int kMenuItemCount = static_cast<int>(sizeof(kMenuItems) / sizeof(kMenuItems[0]));

void ui_fill_rect(std::uint8_t *rgba, int bw, int bh, int x, int y, int w, int h, std::uint32_t color) {
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx)
            plot(rgba, bw, bh, xx, yy, color);
}

void ui_draw_text(std::uint8_t *rgba, int bw, int bh, int x, int y,
                  const char *text, int scale, std::uint32_t color) {
    int cx = x;
    for (const char *p = text; *p; ++p) {
        const Glyph *g = find_glyph(*p);
        if (g) {
            for (int col = 0; col < 5; ++col) {
                for (int row = 0; row < 7; ++row) {
                    if (g->cols[col] & (1u << row)) {
                        for (int sy = 0; sy < scale; ++sy)
                            for (int sx = 0; sx < scale; ++sx)
                                plot(rgba, bw, bh, cx + col * scale + sx,
                                     y + row * scale + sy, color);
                    }
                }
            }
        }
        cx += 6 * scale;  // 5 columns + 1 space
    }
}

void ui_draw_menu(std::uint8_t *rgba, int w, int h) {
    if (w <= 0 || h <= 0) return;
    // Header using real UI copy from the title stage.
    ui_draw_text(rgba, w, h, 24, 20, "SYSTEM MENU", 2, 0xFFFFC040u);
    ui_draw_text(rgba, w, h, 24, 40, "(native touch UI - game strings)", 1, 0xFF9AA0A8u);
    for (int i = 0; i < kMenuItemCount; ++i) {
        const int ry = kMenuY + i * kRowH;
        ui_fill_rect(rgba, w, h, kMenuX, ry, kRowW, kRowH - 4, kPanelColor);
        ui_draw_text(rgba, w, h, kMenuX + 10, ry + 6, kMenuItems[i].label, 1, kTextColor);
    }
    (void)kHiliteColor;
}

std::uint32_t ui_menu_hit(int x, int y) {
    for (int i = 0; i < kMenuItemCount; ++i) {
        const int ry = kMenuY + i * kRowH;
        if (x >= kMenuX && x <= kMenuX + kRowW && y >= ry && y <= ry + kRowH - 4)
            return kMenuItems[i].button;
    }
    return 0u;
}

}  // namespace mgsa
