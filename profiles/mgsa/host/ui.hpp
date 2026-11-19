// mgsa native touch UI: bitmap text rendering + a tachable menu.
//
// The guest never renders real content (see docs/07-stage-load-re.md), so the
// fallback path draws a native menu using the game's OWN UI strings, extracted
// from stage/title/_zar (docs/08-stage-data-formats.md). Taps on a row synthesize
// the PSP button that the game's menu would react to, through TouchLayer.
#ifndef MGSA_UI_HPP
#define MGSA_UI_HPP

#include <cstddef>
#include <cstdint>

namespace mgsa {

// Draw a 5x7 bitmap glyph string into an RGBA8888 buffer.
// `scale` multiplies each pixel; `color` is packed 0xAARRGGBB.
void ui_draw_text(std::uint8_t *rgba, int buf_w, int buf_h,
                  int x, int y, const char *text, int scale,
                  std::uint32_t color);

// Fill an RGBA rect with a packed 0xAARRGGBB color.
void ui_fill_rect(std::uint8_t *rgba, int buf_w, int buf_h,
                  int x, int y, int w, int h, std::uint32_t color);

struct MenuItem {
    const char *label;   // game UI string (English set from the title stage)
    std::uint32_t button; // PSP button bit synthesized when this row is tapped
};

// The command menu shown by the fallback UI. Labels are verbatim game strings.
extern const MenuItem kMenuItems[];
extern const int kMenuItemCount;

// Draw the full fallback menu into a 480x272 RGBA frame.
void ui_draw_menu(std::uint8_t *rgba, int w, int h);

// Map a touch at (x,y) in board space to a menu row, returning its button bit,
// or 0 when the point is outside all rows.
std::uint32_t ui_menu_hit(int x, int y);

}  // namespace mgsa

#endif
