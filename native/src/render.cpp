#include "mgsa/render.hpp"

#include <cstdio>
#include <cstring>

namespace mgsa {
namespace {

struct Glyph { char c; std::uint8_t cols[5]; };

// 5x7 ASCII bitmap font, rows of 5 bits packed column by column.
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
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    for (const Glyph &g : kFont) if (g.c == c) return &g;
    return nullptr;
}

constexpr std::uint32_t kBackground = 0xFF0B1220u;
constexpr std::uint32_t kPanel      = 0xC0101828u;
constexpr std::uint32_t kText       = 0xFFF2F2F2u;
constexpr std::uint32_t kGold       = 0xFFFFC040u;
constexpr std::uint32_t kCyan       = 0xFF50D0E0u;
constexpr std::uint32_t kRed        = 0xFFFF6060u;
constexpr std::uint32_t kGreen      = 0xFF60C878u;

void pixel(Canvas &canvas, int x, int y, std::uint32_t argb) {
    if (x < 0 || y < 0 || x >= canvas.width || y >= canvas.height) return;
    const std::size_t offset = (static_cast<std::size_t>(y) * canvas.width + x) * 4u;
    canvas.pixels[offset + 0] = static_cast<std::uint8_t>(argb >> 16);
    canvas.pixels[offset + 1] = static_cast<std::uint8_t>(argb >> 8);
    canvas.pixels[offset + 2] = static_cast<std::uint8_t>(argb);
    canvas.pixels[offset + 3] = static_cast<std::uint8_t>(argb >> 24);
}

bool inside(int x, int y, int bx, int by, int w, int h) {
    return x >= bx && y >= by && x < bx + w && y < by + h;
}

void button(Canvas &canvas, int x, int y, int w, int h, const char *label, bool active) {
    fill_rect(canvas, x, y, w, h, active ? 0xFF2850A0u : kPanel);
    fill_rect(canvas, x, y, w, 1, 0xFF708090u);
    const int tw = static_cast<int>(std::strlen(label)) * 6;
    draw_text(canvas, x + (w - tw) / 2, y + (h - 7) / 2, label, 1, kText);
}

void draw_board(Canvas &canvas, const Game &game, const ViewState &view) {
    float origin_x = 0.0f, origin_y = 0.0f, cell = 0.0f;
    board_geometry(view, origin_x, origin_y, cell);
    const int step = static_cast<int>(cell);
    for (int y = 0; y < kBoardSize; ++y) {
        for (int x = 0; x < kBoardSize; ++x) {
            const int px = static_cast<int>(origin_x) + x * step;
            const int py = static_cast<int>(origin_y) + y * step;
            const Cell kind = game.cells[y][x];
            const std::uint32_t color = kind == Cell::Wall ? 0xFF354050u
                                      : kind == Cell::Exit ? 0xFF205040u
                                      : ((x + y) & 1) ? 0xFF182434u : 0xFF1E2C3E;
            fill_rect(canvas, px, py, step - 1, step - 1, color);
            if (kind == Cell::Exit) draw_text(canvas, px + 2, py + 2, "EXIT", 1, kGreen);
        }
    }
    for (int i = 0; i < game.guard_count; ++i) {
        const Guard &guard = game.guards[i];
        if (!guard_alive(guard)) continue;
        const int px = static_cast<int>(origin_x) + guard.x * step;
        const int py = static_cast<int>(origin_y) + guard.y * step;
        fill_rect(canvas, px + 2, py + 2, step - 5, step - 5, guard.alert ? kRed : 0xFFB07040u);
        if (guard.stunned) draw_text(canvas, px + 2, py + 2, "Z", 1, kText);
    }
    const int px = static_cast<int>(origin_x) + game.player_x * step;
    const int py = static_cast<int>(origin_y) + game.player_y * step;
    fill_rect(canvas, px + 1, py + 1, step - 3, step - 3, kCyan);
    draw_text(canvas, px + 3, py + 3, "S", 1, 0xFF081018u);
}

void draw_hand(Canvas &canvas, const Game &game, const ViewState &view) {
    for (int i = 0; i < game.hand_count; ++i) {
        const Card &card = card_info(game.hand[i]);
        const int x = 12 + i * 60;
        const bool selected = view.selected_card == i || view.redraw_first == i;
        fill_rect(canvas, x, 222, 58, 44, selected ? 0xFF2850A0u : kPanel);
        fill_rect(canvas, x, 222, 58, 1, selected ? kGold : 0xFF708090u);
        char cost[16];
        std::snprintf(cost, sizeof(cost), "%d", card.cost);
        draw_text(canvas, x + 3, 225, cost, 1, kGold);
        const char *name = card.name;
        if (std::strlen(name) > 8) {
            char short_name[9];
            std::memcpy(short_name, name, 8);
            short_name[8] = '\0';
            name = short_name;
        }
        draw_text(canvas, x + 3, 238, name, 1, kText);
    }
}

void draw_hud(Canvas &canvas, const Game &game, const ViewState &view) {
    char line[64];
    fill_rect(canvas, 326, 12, 146, 198, kPanel);
    draw_text(canvas, 334, 20, "METAL GEAR ACID", 1, kGold);
    std::snprintf(line, sizeof(line), "COST %d", game.cost_left);
    draw_text(canvas, 334, 40, line, 1, kText);
    std::snprintf(line, sizeof(line), "TURN %d", game.turn);
    draw_text(canvas, 334, 56, line, 1, kText);
    std::snprintf(line, sizeof(line), "HP %d / 8", game.player_hp);
    draw_text(canvas, 334, 72, line, 1, game.player_hp <= 2 ? kRed : kGreen);
    std::snprintf(line, sizeof(line), "GUARDS %d", living_guards(game));
    draw_text(canvas, 334, 88, line, 1, kText);
    draw_text(canvas, 334, 112, "TAP CARD THEN", 1, kText);
    draw_text(canvas, 334, 124, "TAP A TARGET", 1, kText);
    draw_text(canvas, 334, 140, "DRAG TO PAN", 1, kText);
    draw_text(canvas, 334, 152, "PINCH TO ZOOM", 1, kText);
    button(canvas, 336, 170, 126, 26, "END TURN", true);
    button(canvas, 336, 202, 126, 26, "SYSTEM MENU", true);
    (void)view;
}

}  // namespace

void canvas_resize(Canvas &canvas, int width, int height) {
    canvas.width = width;
    canvas.height = height;
    canvas.pixels.assign(static_cast<std::size_t>(width) * height * 4u, 0u);
}

void fill_rect(Canvas &canvas, int x, int y, int w, int h, std::uint32_t argb) {
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx) pixel(canvas, xx, yy, argb);
}

void draw_text(Canvas &canvas, int x, int y, const char *text, int scale, std::uint32_t argb) {
    int cursor = x;
    for (const char *p = text; *p; ++p) {
        const Glyph *glyph = find_glyph(*p);
        if (glyph != nullptr) {
            for (int col = 0; col < 5; ++col) {
                for (int row = 0; row < 7; ++row) {
                    if (glyph->cols[col] & (1u << row)) {
                        for (int sy = 0; sy < scale; ++sy)
                            for (int sx = 0; sx < scale; ++sx)
                                pixel(canvas, cursor + col * scale + sx, y + row * scale + sy, argb);
                    }
                }
            }
        }
        cursor += 6 * scale;
    }
}

void board_geometry(const ViewState &view, float &origin_x, float &origin_y, float &cell) {
    cell = 24.0f * view.zoom;
    origin_x = 20.0f - view.pan_x;
    origin_y = 16.0f - view.pan_y;
}

void render_game(Canvas &canvas, const Game &game, const ViewState &view) {
    fill_rect(canvas, 0, 0, canvas.width, canvas.height, kBackground);

    switch (game.phase) {
        case Phase::Title:
            draw_text(canvas, 100, 70, "METAL GEAR ACID", 3, kGold);
            draw_text(canvas, 132, 110, "TOUCH EDITION", 1, kText);
            button(canvas, 170, 180, 140, 32, "START MISSION", true);
            draw_text(canvas, 76, 238, "NATIVE REIMPLEMENTATION - NOT THE 2005 BINARY", 1, 0xFF8899AAu);
            break;

        case Phase::Briefing:
            draw_text(canvas, 60, 50, "MISSION: INFILTRATION", 2, kGold);
            draw_text(canvas, 60, 90, "REACH THE EXIT OR NEUTRALIZE", 1, kText);
            draw_text(canvas, 60, 104, "ALL GUARDS. AVOID THEIR", 1, kText);
            draw_text(canvas, 60, 118, "FORWARD FIELD OF VIEW.", 1, kText);
            draw_text(canvas, 60, 142, "TAP A CARD, THEN TAP A TARGET.", 1, kText);
            draw_text(canvas, 60, 156, "END TURN COSTS 4 IF NO CARD", 1, kText);
            draw_text(canvas, 60, 170, "WAS USED.", 1, kText);
            button(canvas, 336, 216, 120, 30, "BEGIN", true);
            break;

        case Phase::Board:
            draw_board(canvas, game, view);
            draw_hand(canvas, game, view);
            draw_hud(canvas, game, view);
            if (view.toast[0] != '\0') {
                fill_rect(canvas, 12, 198, 306, 20, 0xE0402020u);
                draw_text(canvas, 18, 203, view.toast, 1, kText);
            }
            break;

        case Phase::Result:
            draw_text(canvas, 120, 80,
                      game.outcome == Outcome::Win ? "MISSION COMPLETE" : "MISSION FAILED",
                      2, game.outcome == Outcome::Win ? kGreen : kRed);
            draw_text(canvas, 160, 120, "TURNS USED:", 1, kText);
            char turns[16];
            std::snprintf(turns, sizeof(turns), "%d", game.turn);
            draw_text(canvas, 270, 120, turns, 1, kGold);
            button(canvas, 170, 200, 140, 32, "RESTART", true);
            break;
    }

    if (view.menu_open) {
        fill_rect(canvas, 120, 90, 240, 120, 0xF008101Cu);
        draw_text(canvas, 170, 105, "SYSTEM MENU", 2, kGold);
        button(canvas, 170, 140, 140, 30, "RESUME", true);
        button(canvas, 170, 180, 140, 30, "RESTART", true);
    }
}

Hit hit_test(const Game &game, const ViewState &view, int x, int y) {
    Hit hit;
    if (view.menu_open) {
        if (inside(x, y, 170, 140, 140, 30)) hit.kind = Hit::Kind::MenuResume;
        else if (inside(x, y, 170, 180, 140, 30)) hit.kind = Hit::Kind::MenuRestart;
        return hit;
    }
    switch (game.phase) {
        case Phase::Title:
            if (inside(x, y, 170, 180, 140, 32)) hit.kind = Hit::Kind::StartMission;
            break;
        case Phase::Briefing:
            if (inside(x, y, 336, 216, 120, 30)) hit.kind = Hit::Kind::Begin;
            break;
        case Phase::Result:
            if (inside(x, y, 170, 200, 140, 32)) hit.kind = Hit::Kind::ResultRestart;
            break;
        case Phase::Board:
            if (inside(x, y, 336, 170, 126, 26)) { hit.kind = Hit::Kind::EndTurn; break; }
            if (inside(x, y, 336, 202, 126, 26)) { hit.kind = Hit::Kind::Menu; break; }
            for (int i = 0; i < game.hand_count; ++i) {
                if (inside(x, y, 12 + i * 60, 222, 58, 44)) {
                    hit.kind = Hit::Kind::Card;
                    hit.index = i;
                    return hit;
                }
            }
            {
                float origin_x = 0.0f, origin_y = 0.0f, cell = 0.0f;
                board_geometry(view, origin_x, origin_y, cell);
                const int cx = static_cast<int>((x - origin_x) / cell);
                const int cy = static_cast<int>((y - origin_y) / cell);
                if (cx >= 0 && cy >= 0 && cx < kBoardSize && cy < kBoardSize) {
                    hit.kind = Hit::Kind::Cell;
                    hit.x = cx;
                    hit.y = cy;
                }
            }
            break;
    }
    return hit;
}

}  // namespace mgsa
