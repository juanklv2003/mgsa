// Software renderer and hit testing for the native touch game.
// The coordinate space is the PSP's 480x272, letterboxed to the real window by the platform layer.
#ifndef MGSA_RENDER_HPP
#define MGSA_RENDER_HPP

#include <cstdint>
#include <vector>

#include "mgsa/game.hpp"

namespace mgsa {

constexpr int kViewWidth = 480;
constexpr int kViewHeight = 272;

struct Canvas {
    std::vector<std::uint8_t> pixels;
    int width = 0;
    int height = 0;
};

void canvas_resize(Canvas &canvas, int width, int height);
void fill_rect(Canvas &canvas, int x, int y, int w, int h, std::uint32_t argb);
void draw_text(Canvas &canvas, int x, int y, const char *text, int scale, std::uint32_t argb);

struct ViewState {
    float pan_x = 0.0f;
    float pan_y = 0.0f;
    float zoom = 1.0f;
    int selected_card = -1;
    int redraw_first = -1;
    bool menu_open = false;
    char toast[96] = {};
};

void render_game(Canvas &canvas, const Game &game, const ViewState &view);

struct Hit {
    enum class Kind : std::uint8_t {
        None, Card, Cell, EndTurn, Menu, MenuResume, MenuRestart,
        StartMission, Begin, ResultRestart
    };
    Kind kind = Kind::None;
    int index = -1;  // hand slot for Card
    int x = 0;       // board cell for Cell
    int y = 0;
};

Hit hit_test(const Game &game, const ViewState &view, int x, int y);

// Board transform, shared by the renderer and hit testing so they cannot drift apart.
void board_geometry(const ViewState &view, float &origin_x, float &origin_y, float &cell);

}  // namespace mgsa

#endif  // MGSA_RENDER_HPP
