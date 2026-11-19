// mgsa Android entry: android_native_app_glue NativeActivity.
// Creates the window, runs the guest loop (mgsa_main) on a worker thread,
// renders the guest's EDRAM frame via ANativeWindow, and forwards touch events
// into the same TouchLayer the sceCtrl HLE drains. No SDL, no Vulkan.
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/input.h>
#include <android/looper.h>
#include <android/asset_manager.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "ui.hpp"
#include "asset_list.h"

// C API from main.cpp (compiled with -DMGSA_ANDROID_LIBRARY).
extern "C" {
void mgsa_enable_touch();
void mgsa_touch_event(int kind, int id, float x, float y);
void mgsa_press_button(std::uint32_t buttons);
int  mgsa_read_frame(std::uint8_t *dst, int dst_len);
int  mgsa_frame_width();
int  mgsa_frame_height();
int  mgsa_main_android(int argc, char **argv);
}

namespace {

struct App {
    android_app *app{nullptr};
    std::atomic<bool> running{true};
    std::atomic<bool> guest_done{false};
    pthread_t guest_thread{};
    pthread_t render_thread{};
    std::atomic<ANativeWindow*> window{nullptr};  // acquired, kept alive across TERM_WINDOW
};

App g_app;

int32_t on_input(android_app *app, AInputEvent *event) {
    (void)app;
    const int32_t type = AInputEvent_getType(event);
    if (type != AINPUT_EVENT_TYPE_MOTION) return 0;

    const int32_t action = AMotionEvent_getAction(event);
    const int32_t index = (action & AMOTION_EVENT_ACTION_MASK);
    const float screen_w = static_cast<float>(ANativeWindow_getWidth(app->window));
    const float screen_h = static_cast<float>(ANativeWindow_getHeight(app->window));
    if (screen_w <= 0.0f || screen_h <= 0.0f) return 0;

    // Invert the render() letterbox transform: screen -> 480x272 board space.
    const float fw = static_cast<float>(mgsa_frame_width());
    const float fh = static_cast<float>(mgsa_frame_height());
    const float scale = (screen_w / fw < screen_h / fh) ? screen_w / fw : screen_h / fh;
    const float off_x = (screen_w - fw * scale) / 2.0f;
    const float off_y = (screen_h - fh * scale) / 2.0f;
    const auto px = [&](std::size_t p) {
        return (AMotionEvent_getX(event, p) - off_x) / scale;
    };
    const auto py = [&](std::size_t p) {
        return (AMotionEvent_getY(event, p) - off_y) / scale;
    };

    if (index == AMOTION_EVENT_ACTION_DOWN ||
        index == AMOTION_EVENT_ACTION_POINTER_DOWN) {
        const std::size_t p = (index == AMOTION_EVENT_ACTION_POINTER_DOWN)
                                  ? static_cast<std::size_t>((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                                                             >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT)
                                  : 0u;
        const int id = AMotionEvent_getPointerId(event, p);
        // A tap on a native-UI menu row synthesizes that row's PSP button.
        const std::uint32_t menu_button = mgsa::ui_menu_hit(static_cast<int>(px(p)),
                                                            static_cast<int>(py(p)));
        if (menu_button != 0u) {
            mgsa_press_button(menu_button);
        } else {
            mgsa_touch_event(0, id, px(p), py(p));
        }
    } else if (index == AMOTION_EVENT_ACTION_UP ||
               index == AMOTION_EVENT_ACTION_POINTER_UP) {
        const std::size_t p = (index == AMOTION_EVENT_ACTION_POINTER_UP)
                                  ? static_cast<std::size_t>((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                                                             >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT)
                                  : 0u;
        const int id = AMotionEvent_getPointerId(event, p);
        mgsa_touch_event(2, id, px(p), py(p));
    } else if (index == AMOTION_EVENT_ACTION_MOVE) {
        const std::size_t count = AMotionEvent_getPointerCount(event);
        for (std::size_t p = 0u; p < count; ++p) {
            const int id = AMotionEvent_getPointerId(event, p);
            mgsa_touch_event(1, id, px(p), py(p));
        }
    }
    return 1;
}

// mkdir -p for a path relative to the app files dir.
static void mkdir_p(const std::string &path) {
    std::string cur;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '/') {
            if (!cur.empty()) mkdir(cur.c_str(), 0777);
        }
        cur.push_back(path[i]);
    }
    // The final component has no trailing '/', so create it explicitly.
    if (!cur.empty()) mkdir(cur.c_str(), 0777);
}

// Copy the bundled game tree from assets to a real filesystem directory.
//
// AAssetDir_getNextFileName does NOT enumerate compressed assets on some
// Android versions (verified on the Pixel 9a: openDir("extract") succeeds but
// yields zero entries, while AAssetManager_open on a known file works). So we
// iterate a build-time list of asset paths and open each one directly.
static void extract_assets(AAssetManager *mgr, const std::string &out_dir) {
    int copied = 0;
    for (int i = 0; i < kAssetFileCount; ++i) {
        const char *asset_path = kAssetFiles[i];
        AAsset *a = AAssetManager_open(mgr, asset_path, AASSET_MODE_STREAMING);
        if (a == nullptr) {
            __android_log_print(ANDROID_LOG_WARN, "MGSA", "cannot open asset %s", asset_path);
            continue;
        }
        const std::string dst = out_dir + "/" + asset_path;
        mkdir_p(dst.substr(0, dst.find_last_of('/')));
        std::ofstream f(dst, std::ios::binary);
        char buf[16384];
        int n;
        while ((n = AAsset_read(a, buf, sizeof(buf))) > 0) f.write(buf, n);
        f.close();
        AAsset_close(a);
        ++copied;
    }
    __android_log_print(ANDROID_LOG_INFO, "MGSA", "extracted %d/%d asset files",
                        copied, kAssetFileCount);
}

void *guest_main(void *) {
    __android_log_print(ANDROID_LOG_INFO, "MGSA", "guest_main: starting");
    // Redirect the host's stdout/stderr to a log file we can read with run-as.
    const char *logpath = "/data/data/io.github.juanklv2003.mgsa/files/guest.log";
    freopen(logpath, "w", stdout);
    freopen(logpath, "a", stderr);
    mgsa_enable_touch();
    // Unpack the bundled game tree from assets to the app's files dir once.
    const std::string files = "/data/data/io.github.juanklv2003.mgsa/files";
    const std::string marker = files + "/extract/PSP_GAME/SYSDIR/BOOT.BIN";
    struct stat st{};
    if (stat(marker.c_str(), &st) != 0) {
        __android_log_print(ANDROID_LOG_INFO, "MGSA", "extracting assets to %s", files.c_str());
        extract_assets(g_app.app->activity->assetManager, files);
        __android_log_print(ANDROID_LOG_INFO, "MGSA", "asset extraction done");
    } else {
        __android_log_print(ANDROID_LOG_INFO, "MGSA", "game data already present");
    }
    // The disc image is installed in the app's files dir by the launcher.
    // Paths are passed to mgsa_main like the desktop CLI.
    char arg0[] = "mgsa";
    char arg1[] = "--root";
    char arg2[] = "/data/data/io.github.juanklv2003.mgsa/files/extract";
    char arg3[] = "--elf";
    char arg4[] = "/data/data/io.github.juanklv2003.mgsa/files/extract/PSP_GAME/SYSDIR/BOOT.BIN";
    char arg5[] = "--iso";
    char arg6[] = "/data/data/io.github.juanklv2003.mgsa/files/mgsa.iso";
    char *argv[] = {arg0, arg1, arg2, arg3, arg4, arg5, arg6};
    mgsa_main_android(7, argv);
    __android_log_print(ANDROID_LOG_INFO, "MGSA", "guest_main: mgsa_main returned");
    g_app.guest_done.store(true);
    return nullptr;
}

void render(ANativeWindow *window) {
    static int rcount = 0;
    if (++rcount <= 3 || rcount % 500 == 0)
        __android_log_print(ANDROID_LOG_INFO, "MGSA", "render #%d called", rcount);
    if (window == nullptr) return;
    const int w = ANativeWindow_getWidth(window);
    const int h = ANativeWindow_getHeight(window);
    if (w <= 0 || h <= 0) return;

    const int fw = mgsa_frame_width();
    const int fh = mgsa_frame_height();
    if (fw <= 0 || fh <= 0) return;
    std::vector<std::uint8_t> frame(static_cast<std::size_t>(fw) * fh * 4u);
    if (mgsa_read_frame(frame.data(), static_cast<int>(frame.size())) == 0) return;

    // The guest's clear sprite has alpha=0, so a straight copy composites as black
    // on Android. Force opaque so whatever the guest draws is actually visible.
    bool has_visible = false;
    for (std::size_t i = 3; i < frame.size(); i += 4u) {
        if (frame[i] != 0u || frame[i-3] != 0u || frame[i-2] != 0u || frame[i-1] != 0u) {
            has_visible = true;
        }
        frame[i] = 0xFFu;
    }
    if (!has_visible) {
        // Native touch UI: the guest produced only a transparent clear. Draw a real
        // menu using the game's own UI strings extracted from stage/title/_zar
        // (docs/08). Tapping a row synthesizes the matching PSP button.
        mgsa::ui_draw_menu(frame.data(), fw, fh);
    }

    ANativeWindow_Buffer buf;
    if (ANativeWindow_lock(window, &buf, nullptr) != 0) return;
    // Scale the 480x272 guest frame to fill the whole surface (nearest-neighbor),
    // preserving aspect ratio with letterboxing so the UI isn't stretched.
    const int dw = buf.width, dh = buf.height;
    const float scale = (static_cast<float>(dw) / fw < static_cast<float>(dh) / fh)
                            ? static_cast<float>(dw) / fw : static_cast<float>(dh) / fh;
    const int out_w = static_cast<int>(fw * scale);
    const int out_h = static_cast<int>(fh * scale);
    const int off_x = (dw - out_w) / 2;
    const int off_y = (dh - out_h) / 2;
    static int lockshots = 0;
    if (lockshots++ < 3)
        __android_log_print(ANDROID_LOG_INFO, "MGSA", "lock w=%d h=%d fmt=%d scale=%.2f out=%dx%d off=%d,%d",
                            dw, dh, buf.format, scale, out_w, out_h, off_x, off_y);
    const auto sample = [&](int sx, int sy) -> const std::uint8_t* {
        return frame.data() + (static_cast<std::size_t>(sy) * fw + sx) * 4u;
    };
    if (buf.format == WINDOW_FORMAT_RGBA_8888) {
        for (int y = 0; y < out_h; ++y) {
            const int sy = y * fh / out_h;
            std::uint8_t *dst = static_cast<std::uint8_t *>(buf.bits) + (y + off_y) * buf.stride * 4u + off_x * 4u;
            for (int x = 0; x < out_w; ++x) {
                const std::uint8_t *src = sample(x * fw / out_w, sy);
                dst[x*4+0] = src[0]; dst[x*4+1] = src[1]; dst[x*4+2] = src[2]; dst[x*4+3] = 0xFFu;
            }
        }
    } else if (buf.format == WINDOW_FORMAT_RGB_565) {
        for (int y = 0; y < out_h; ++y) {
            const int sy = y * fh / out_h;
            std::uint16_t *dst = static_cast<std::uint16_t *>(buf.bits) + (y + off_y) * buf.stride + off_x;
            for (int x = 0; x < out_w; ++x) {
                const std::uint8_t *src = sample(x * fw / out_w, sy);
                dst[x] = static_cast<std::uint16_t>(((src[0] >> 3) << 11) | ((src[1] >> 2) << 5) | (src[2] >> 3));
            }
        }
    }
    ANativeWindow_unlockAndPost(window);
}

// Render thread: keeps drawing into an acquired window reference independently of
// the main thread's command loop (which tore the window down within ~6 ms).
void *render_thread_main(void *) {
    __android_log_print(ANDROID_LOG_INFO, "MGSA", "render thread started");
    while (g_app.running.load()) {
        ANativeWindow *w = g_app.window.load();
        if (w != nullptr) render(w);
        usleep(16000);  // ~60 fps
    }
    return nullptr;
}

void on_cmd(android_app *app, int32_t cmd) {
    __android_log_print(ANDROID_LOG_INFO, "MGSA", "on_cmd %d", cmd);
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            __android_log_print(ANDROID_LOG_INFO, "MGSA", "INIT_WINDOW: acquiring");
            if (app->window != nullptr) {
                ANativeWindow_acquire(app->window);
                g_app.window.store(app->window);
            }
            if (g_app.guest_thread == 0u) {
                // The guest's interpreter recurses through unsupported-VFPU dispatch and
                // emulates 32 MiB of guest RAM; the default ~1 MiB pthread stack overflows
                // on ARM64. Give it room comparable to the desktop main thread.
                pthread_attr_t attr;
                pthread_attr_init(&attr);
                pthread_attr_setstacksize(&attr, 16u * 1024u * 1024u);
                pthread_create(&g_app.guest_thread, &attr, guest_main, nullptr);
                pthread_attr_destroy(&attr);
            }
            break;
        case APP_CMD_TERM_WINDOW: {
            ANativeWindow *w = g_app.window.exchange(nullptr);
            if (w != nullptr) ANativeWindow_release(w);
            g_app.running.store(false);
            break;
        }
        default:
            break;
    }
}

}  // namespace

void android_main(android_app *app) {
    g_app.app = app;
    app->onAppCmd = on_cmd;
    app->onInputEvent = on_input;

    // Start a dedicated render thread that owns an acquired window reference.
    pthread_create(&g_app.render_thread, nullptr, render_thread_main, nullptr);

    int events;
    android_poll_source *source;
    while (g_app.running.load()) {
        ALooper_pollOnce(-1, nullptr, &events, reinterpret_cast<void **>(&source));
        if (source != nullptr) source->process(app, source);
        if (app->destroyRequested != 0) g_app.running.store(false);
    }
    if (g_app.guest_thread != 0u) pthread_join(g_app.guest_thread, nullptr);
    if (g_app.render_thread != 0u) pthread_join(g_app.render_thread, nullptr);
}
