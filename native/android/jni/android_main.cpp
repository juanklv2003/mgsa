// Native touch entry point: renders the card game into an ANativeWindow and turns touch gestures
// into game actions. No PSP runtime, no emulated buttons, no diagnostic overlay.
#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>

#include <android/input.h>

#include <pthread.h>
#include <unistd.h>

#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <mutex>
#include <unordered_map>

#include "mgsa/game.hpp"
#include "mgsa/render.hpp"

namespace {

constexpr const char *kTag = "MGSA-TOUCH";

struct State {
    std::mutex mutex;
    mgsa::Game game;
    mgsa::ViewState view;
    mgsa::Canvas canvas;
};

State g_state;
android_app *g_app = nullptr;
std::atomic<ANativeWindow *> g_window{nullptr};

struct Pointer {
    float start_x;
    float start_y;
    float x;
    float y;
};

std::unordered_map<int, Pointer> g_pointers;
float g_last_pinch = 0.0f;

void log_info(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    __android_log_vprint(ANDROID_LOG_INFO, kTag, fmt, args);
    va_end(args);
}

void transform(int screen_w, int screen_h, float &scale, float &off_x, float &off_y) {
    scale = screen_w / static_cast<float>(mgsa::kViewWidth);
    if (screen_h / static_cast<float>(mgsa::kViewHeight) < scale)
        scale = screen_h / static_cast<float>(mgsa::kViewHeight);
    off_x = (screen_w - mgsa::kViewWidth * scale) * 0.5f;
    off_y = (screen_h - mgsa::kViewHeight * scale) * 0.5f;
}

void set_toast(State &state, const char *message) {
    std::snprintf(state.view.toast, sizeof(state.view.toast), "%s", message);
}

void apply(State &state, mgsa::Action::Kind kind, int a = -1, int b = -1, int x = 0, int y = 0) {
    mgsa::Action action{};
    action.kind = kind;
    action.a = a;
    action.b = b;
    action.x = x;
    action.y = y;
    if (!mgsa::mgsa_act(state.game, action)) set_toast(state, mgsa::mgsa_last_error());
    else state.view.toast[0] = '\0';
}

void handle_tap(float screen_x, float screen_y) {
    ANativeWindow *window = g_window.load();
    if (window == nullptr) return;
    const int screen_w = ANativeWindow_getWidth(window);
    const int screen_h = ANativeWindow_getHeight(window);
    float scale = 1.0f, off_x = 0.0f, off_y = 0.0f;
    transform(screen_w, screen_h, scale, off_x, off_y);
    const int x = static_cast<int>((screen_x - off_x) / scale);
    const int y = static_cast<int>((screen_y - off_y) / scale);

    State &state = g_state;
    std::lock_guard<std::mutex> guard(state.mutex);
    const mgsa::Hit hit = mgsa::hit_test(state.game, state.view, x, y);
    if (state.view.menu_open) {
        if (hit.kind == mgsa::Hit::Kind::MenuResume) state.view.menu_open = false;
        else if (hit.kind == mgsa::Hit::Kind::MenuRestart) {
            state.view.menu_open = false;
            apply(state, mgsa::Action::Kind::Restart);
        }
        return;
    }

    switch (hit.kind) {
        case mgsa::Hit::Kind::None:
            break;
        case mgsa::Hit::Kind::StartMission:
            apply(state, mgsa::Action::Kind::Briefing);
            break;
        case mgsa::Hit::Kind::Begin:
        case mgsa::Hit::Kind::ResultRestart:
            apply(state, mgsa::Action::Kind::StartMission);
            state.view.selected_card = -1;
            state.view.pan_x = 0.0f;
            state.view.pan_y = 0.0f;
            state.view.zoom = 1.0f;
            break;
        case mgsa::Hit::Kind::EndTurn:
            apply(state, mgsa::Action::Kind::EndTurn);
            break;
        case mgsa::Hit::Kind::Menu:
            state.view.menu_open = true;
            break;
        case mgsa::Hit::Kind::Card: {
            const int id = state.game.hand[hit.index];
            const mgsa::Card &card = mgsa::card_info(id);
            if (card.type == mgsa::CardType::Redraw) {
                if (state.view.redraw_first < 0) {
                    state.view.redraw_first = hit.index;
                    set_toast(state, "SELECT A SECOND CARD");
                } else {
                    apply(state, mgsa::Action::Kind::Redraw, state.view.redraw_first, hit.index);
                    state.view.redraw_first = -1;
                }
            } else {
                state.view.selected_card = hit.index;
            }
            break;
        }
        case mgsa::Hit::Kind::Cell:
            if (state.view.selected_card < 0) {
                set_toast(state, "SELECT A CARD FIRST");
            } else {
                apply(state, mgsa::Action::Kind::PlayCard, state.view.selected_card, -1, hit.x, hit.y);
                state.view.selected_card = -1;
            }
            break;
        case mgsa::Hit::Kind::MenuResume:
        case mgsa::Hit::Kind::MenuRestart:
            break;
    }
}

int32_t on_input(android_app *app, AInputEvent *event) {
    (void)app;
    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) return 0;
    const int32_t raw = AMotionEvent_getAction(event);
    const int32_t action = raw & AMOTION_EVENT_ACTION_MASK;
    const int index = (raw & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                      AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
    ANativeWindow *window = g_window.load();
    if (window == nullptr) return 0;
    const int screen_w = ANativeWindow_getWidth(window);
    const int screen_h = ANativeWindow_getHeight(window);
    float scale = 1.0f, off_x = 0.0f, off_y = 0.0f;
    transform(screen_w, screen_h, scale, off_x, off_y);
    const int count = AMotionEvent_getPointerCount(event);

    if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_POINTER_DOWN) {
        const int id = AMotionEvent_getPointerId(event, index);
        Pointer pointer{AMotionEvent_getX(event, index), AMotionEvent_getY(event, index),
                        AMotionEvent_getX(event, index), AMotionEvent_getY(event, index)};
        g_pointers[id] = pointer;
        if (g_pointers.size() == 2) {
            auto it = g_pointers.begin();
            const float dx = it->second.x - std::next(it)->second.x;
            const float dy = it->second.y - std::next(it)->second.y;
            g_last_pinch = std::sqrt(dx * dx + dy * dy);
        }
        return 1;
    }

    if (action == AMOTION_EVENT_ACTION_MOVE) {
        for (int p = 0; p < count; ++p) {
            const int id = AMotionEvent_getPointerId(event, p);
            auto it = g_pointers.find(id);
            if (it == g_pointers.end()) continue;
            it->second.x = AMotionEvent_getX(event, p);
            it->second.y = AMotionEvent_getY(event, p);
        }
        std::lock_guard<std::mutex> guard(g_state.mutex);
        if (g_pointers.size() >= 2) {
            auto it = g_pointers.begin();
            const float dx = it->second.x - std::next(it)->second.x;
            const float dy = it->second.y - std::next(it)->second.y;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (g_last_pinch > 8.0f) {
                g_state.view.zoom *= distance / g_last_pinch;
                if (g_state.view.zoom < 0.6f) g_state.view.zoom = 0.6f;
                if (g_state.view.zoom > 2.5f) g_state.view.zoom = 2.5f;
            }
            g_last_pinch = distance;
        } else if (!g_pointers.empty()) {
            Pointer &pointer = g_pointers.begin()->second;
            const float dx = pointer.x - pointer.start_x;
            const float dy = pointer.y - pointer.start_y;
            g_state.view.pan_x -= dx / scale;
            g_state.view.pan_y -= dy / scale;
            if (g_state.view.pan_x < -160.0f) g_state.view.pan_x = -160.0f;
            if (g_state.view.pan_x > 160.0f) g_state.view.pan_x = 160.0f;
            if (g_state.view.pan_y < -160.0f) g_state.view.pan_y = -160.0f;
            if (g_state.view.pan_y > 160.0f) g_state.view.pan_y = 160.0f;
            pointer.start_x = pointer.x;
            pointer.start_y = pointer.y;
        }
        return 1;
    }

    if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_POINTER_UP ||
        action == AMOTION_EVENT_ACTION_CANCEL) {
        const int id = AMotionEvent_getPointerId(event, index);
        auto it = g_pointers.find(id);
        if (it != g_pointers.end()) {
            const float dx = AMotionEvent_getX(event, index) - it->second.start_x;
            const float dy = AMotionEvent_getY(event, index) - it->second.start_y;
            const bool tap = action != AMOTION_EVENT_ACTION_CANCEL &&
                             std::sqrt(dx * dx + dy * dy) < 14.0f;
            g_pointers.erase(it);
            if (tap && g_pointers.empty()) handle_tap(AMotionEvent_getX(event, index),
                                                      AMotionEvent_getY(event, index));
        }
        g_last_pinch = 0.0f;
        return 1;
    }
    return 0;
}

void present(ANativeWindow *window) {
    if (window == nullptr) return;
    const int dw = ANativeWindow_getWidth(window);
    const int dh = ANativeWindow_getHeight(window);
    if (dw <= 0 || dh <= 0) return;
    float scale = 1.0f, off_x = 0.0f, off_y = 0.0f;
    transform(dw, dh, scale, off_x, off_y);
    const int out_w = static_cast<int>(mgsa::kViewWidth * scale);
    const int out_h = static_cast<int>(mgsa::kViewHeight * scale);

    ANativeWindow_Buffer buffer;
    if (ANativeWindow_lock(window, &buffer, nullptr) != 0) return;
    const std::uint8_t *source = g_state.canvas.pixels.data();
    if (buffer.format == WINDOW_FORMAT_RGBA_8888) {
        for (int y = 0; y < out_h; ++y) {
            const int sy = y * mgsa::kViewHeight / out_h;
            std::uint8_t *dst = static_cast<std::uint8_t *>(buffer.bits) +
                                (y + static_cast<int>(off_y)) * buffer.stride * 4u +
                                static_cast<int>(off_x) * 4u;
            for (int x = 0; x < out_w; ++x) {
                const std::uint8_t *src = source + (static_cast<std::size_t>(sy) * mgsa::kViewWidth +
                                                    x * mgsa::kViewWidth / out_w) * 4u;
                dst[x * 4 + 0] = src[0];
                dst[x * 4 + 1] = src[1];
                dst[x * 4 + 2] = src[2];
                dst[x * 4 + 3] = 0xFFu;
            }
        }
    } else if (buffer.format == WINDOW_FORMAT_RGB_565) {
        for (int y = 0; y < out_h; ++y) {
            const int sy = y * mgsa::kViewHeight / out_h;
            std::uint16_t *dst = static_cast<std::uint16_t *>(buffer.bits) +
                                 (y + static_cast<int>(off_y)) * buffer.stride +
                                 static_cast<int>(off_x);
            for (int x = 0; x < out_w; ++x) {
                const std::uint8_t *src = source + (static_cast<std::size_t>(sy) * mgsa::kViewWidth +
                                                    x * mgsa::kViewWidth / out_w) * 4u;
                dst[x] = static_cast<std::uint16_t>(((src[0] >> 3) << 11) |
                                                    ((src[1] >> 2) << 5) | (src[2] >> 3));
            }
        }
    }
    ANativeWindow_unlockAndPost(window);
}

void *render_loop(void *) {
    static int frames = 0;
    while (true) {
        ANativeWindow *window = g_window.load();
        if (window != nullptr) {
            {
                std::lock_guard<std::mutex> guard(g_state.mutex);
                mgsa::render_game(g_state.canvas, g_state.game, g_state.view);
            }
            present(window);
            if (++frames <= 3 || frames % 600 == 0)
                log_info("frame %d phase=%d zoom=%.2f", frames,
                         static_cast<int>(g_state.game.phase), g_state.view.zoom);
        }
        usleep(33000);
    }
    return nullptr;
}

void on_cmd(android_app *app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            g_app = app;
            ANativeWindow_acquire(app->window);
            g_window.store(app->window);
            log_info("window ready %dx%d", ANativeWindow_getWidth(app->window),
                     ANativeWindow_getHeight(app->window));
            break;
        case APP_CMD_TERM_WINDOW: {
            ANativeWindow *window = g_window.exchange(nullptr);
            if (window != nullptr) ANativeWindow_release(window);
            break;
        }
        default:
            break;
    }
}

}  // namespace

void android_main(android_app *app) {
    mgsa::init_game(g_state.game);
    mgsa::canvas_resize(g_state.canvas, mgsa::kViewWidth, mgsa::kViewHeight);
    app->onAppCmd = on_cmd;
    app->onInputEvent = on_input;

    pthread_t thread;
    pthread_create(&thread, nullptr, render_loop, nullptr);

    int events = 0;
    android_poll_source *source = nullptr;
    while (true) {
        while (ALooper_pollOnce(-1, nullptr, &events, reinterpret_cast<void **>(&source)) >= 0) {
            if (source != nullptr) source->process(app, source);
            if (app->destroyRequested != 0) {
                g_window.store(nullptr);
                pthread_join(thread, nullptr);
                log_info("destroyed");
                return;
            }
        }
    }
}
