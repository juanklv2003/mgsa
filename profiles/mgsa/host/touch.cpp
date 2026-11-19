#include "touch.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>

namespace mgsa {

void TouchLayer::push(const TouchEvent &event) {
    queue_.push_back(event);
}

InputState TouchLayer::poll(std::uint32_t display_frame) {
    ++frame_;
    const std::uint32_t now = display_frame != 0u ? display_frame : frame_;
    InputState out;

    // Drain this frame's queued events.
    std::size_t budget = queue_.size();
    while (budget-- > 0 && !queue_.empty()) {
        TouchEvent e = queue_.front();
        if (e.at_frame != 0u && e.at_frame > now) {
            queue_.push_back(e);
            continue;
        }
        queue_.pop_front();

        if (e.kind == TouchEvent::Down) {
            Pointer p;
            p.x = p.start_x = e.x;
            p.y = p.start_y = e.y;
            p.start_frame = frame_;
            p.moved = false;
            pointers_[e.id] = p;
        } else if (e.kind == TouchEvent::Move) {
            auto it = pointers_.find(e.id);
            if (it != pointers_.end()) {
                const float dx = e.x - it->second.start_x;
                const float dy = e.y - it->second.start_y;
                if (std::fabs(dx) > 12.0f || std::fabs(dy) > 12.0f) it->second.moved = true;
                it->second.x = e.x;
                it->second.y = e.y;
                // Continuous drag drives the analog stick (center 0x80).
                // Clamp to a 64-unit radius around center like a real stick.
                const float ax = std::clamp(128.0f + dx * 0.75f, 64.0f, 192.0f);
                const float ay = std::clamp(128.0f + dy * 0.75f, 64.0f, 192.0f);
                drag_x_ = ax;
                drag_y_ = ay;
            }
        } else {  // Up
            auto it = pointers_.find(e.id);
            if (it != pointers_.end()) {
                const Pointer p = it->second;
                pointers_.erase(it);

                const bool was_multi = pointers_.size() >= 1u;  // another finger still down
                const std::uint32_t held = frame_ - p.start_frame + 1u;
                const float dx = p.x - p.start_x;
                const float dy = p.y - p.start_y;
                const float dist = std::sqrt(dx * dx + dy * dy);

                if (was_multi) {
                    // Two-finger interaction: back / cancel.
                    latch_ |= kCircle;
                    latch_until_frame_ = frame_ + 1u;
                } else if (!p.moved && dist < 16.0f) {
                    // Tap: the game's confirm button.
                    latch_ |= kCross;
                    latch_until_frame_ = frame_ + 1u;
                } else if (dist >= 16.0f) {
                    // Swipe/fling: a directional step for the list stepper.
                    std::uint32_t dir = 0u;
                    if (std::fabs(dx) > std::fabs(dy)) {
                        dir = dx > 0.0f ? kRight : kLeft;
                    } else {
                        dir = dy > 0.0f ? kDown : kUp;
                    }
                    latch_ |= dir;
                    // Hold the direction for a few polls so the 21-frame stepper sees it.
                    latch_until_frame_ = frame_ + 3u;
                }
                // A drag that ends returns the stick to center.
                drag_x_ = 128.0f;
                drag_y_ = 128.0f;
            }
        }
    }

    // Latched buttons survive until their frame deadline (>=1 poll).
    if (frame_ <= latch_until_frame_ && latch_until_frame_ != 0u) {
        out.buttons = latch_;
    } else {
        latch_ = 0u;
        latch_until_frame_ = 0u;
    }

    out.left_x = static_cast<std::uint8_t>(std::clamp(drag_x_, 0.0f, 255.0f));
    out.left_y = static_cast<std::uint8_t>(std::clamp(drag_y_, 0.0f, 255.0f));
    out.right_x = 0x80;
    out.right_y = 0x80;
    return out;
}

void TouchLayer::press_button(std::uint32_t buttons, std::uint32_t polls) {
    latch_ = buttons;
    latch_until_frame_ = frame_ + (polls == 0u ? 1u : polls);
}

std::size_t TouchLayer::load_file(const std::string &path) {
    std::ifstream in(path);
    if (!in) return 0u;
    std::size_t count = 0u;
    std::string line;
    while (std::getline(in, line)) {
        const auto comment = line.find('#');
        if (comment != std::string::npos) line.erase(comment);
        std::istringstream ss(line);
        std::string kind;
        if (!(ss >> kind)) continue;
        std::uint32_t at = 0u;
        if (kind == "at") { ss >> at; if (!(ss >> kind)) continue; }
        if (kind == "up") {
            std::uint32_t id;
            if (ss >> id) {
                push({TouchEvent::Up, id, 0.0f, 0.0f, at});
                ++count;
            }
        } else if (kind == "down" || kind == "move") {
            std::uint32_t id;
            float x, y;
            if (ss >> id >> x >> y) {
                push({kind == "down" ? TouchEvent::Down : TouchEvent::Move, id, x, y, at});
                ++count;
            }
        }
    }
    return count;
}

}  // namespace mgsa
