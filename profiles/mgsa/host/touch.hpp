// mgsa touch-native input layer.
//
// The whole game reads the controller through one HLE call,
// sceCtrlReadBufferPositive (NID 0x1F803938). This layer produces the 16-byte
// SceCtrlData that call returns, from touch gestures instead of a virtual
// d-pad. Design constraints come from docs/03-input-seam.md and
// docs/04-ui-intent.md:
//   - raw PSP button bits are preserved; the game relocates L/R itself;
//   - a press must be held for at least one poll frame to be observable
//     (the game's edge-detect + auto-repeat at fn 0x74EB4);
//   - the engine models movement, not coordinates (list stepper at 0x67834),
//     so a swipe maps to a directional bit and a drag to the analog stick.
//
// Platform-neutral: gestures are fed as down/move/up events with coordinates
// in a normalized 480x272 board space. Android JNI and a desktop command
// file both feed the same queue.
#ifndef MGSA_TOUCH_HPP
#define MGSA_TOUCH_HPP

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>

namespace mgsa {

// Raw PSP SceCtrlData button bits (docs/03-input-seam.md). Do not remap.
enum Button : std::uint32_t {
    kSelect   = 0x0001u,
    kStart    = 0x0008u,
    kUp       = 0x0010u,
    kRight    = 0x0020u,
    kDown     = 0x0040u,
    kLeft     = 0x0080u,
    kL        = 0x0100u,
    kR        = 0x0200u,
    kTriangle = 0x1000u,
    kCircle   = 0x2000u,
    kCross    = 0x4000u,
    kSquare   = 0x8000u,
};

struct TouchEvent {
    enum Kind { Down, Move, Up } kind;
    std::uint32_t id;
    float x;  // normalized 0..480
    float y;  // normalized 0..272
    std::uint32_t at_frame{0};  // 0 = immediate; else fire at this poll frame
};

// The state the HLE copies into SceCtrlData: buttons at +4, lx at +8, ly at +9.
struct InputState {
    std::uint32_t buttons{0};
    std::uint8_t left_x{0x80};
    std::uint8_t left_y{0x80};
    std::uint8_t right_x{0x80};
    std::uint8_t right_y{0x80};
};

class TouchLayer {
public:
    void push(const TouchEvent &event);

    // Produce the input state for the current poll. Call once per guest poll
    // (sceCtrlReadBufferPositive). Latches a press for at least one poll.
    InputState poll(std::uint32_t display_frame = 0u);

    // Parse a command file of down/move/up lines into the queue. Returns the
    // number of events parsed. Lines: "down <id> <x> <y>", "move <id> <x> <y>",
    // "up <id>". '#' starts a comment.
    std::size_t load_file(const std::string &path);

    bool empty() const { return queue_.empty(); }

private:
    struct Pointer {
        float x{0}, y{0};
        float start_x{0}, start_y{0};
        std::uint32_t start_frame{0};
        bool moved{false};
    };

    std::deque<TouchEvent> queue_;
    std::unordered_map<std::uint32_t, Pointer> pointers_;
    std::uint32_t frame_{0};

    // Latched synthetic buttons that must survive at least one poll.
    std::uint32_t latch_{0};
    std::uint32_t latch_until_frame_{0};

    // Continuous drag -> analog.
    float drag_x_{0x80}, drag_y_{0x80};
};

}  // namespace mgsa

#endif  // MGSA_TOUCH_HPP
