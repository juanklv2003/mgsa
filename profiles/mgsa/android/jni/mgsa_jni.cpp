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

// C API from main.cpp (compiled with -DMGSA_ANDROID_LIBRARY).
extern "C" {
void mgsa_enable_touch();
void mgsa_touch_event(int kind, int id, float x, float y);
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

    const auto px = [&](std::size_t p) {
        return AMotionEvent_getX(event, p) / screen_w * 480.0f;
    };
    const auto py = [&](std::size_t p) {
        return AMotionEvent_getY(event, p) / screen_h * 272.0f;
    };

    if (index == AMOTION_EVENT_ACTION_DOWN ||
        index == AMOTION_EVENT_ACTION_POINTER_DOWN) {
        const std::size_t p = (index == AMOTION_EVENT_ACTION_POINTER_DOWN)
                                  ? static_cast<std::size_t>((action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                                                             >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT)
                                  : 0u;
        const int id = AMotionEvent_getPointerId(event, p);
        mgsa_touch_event(0, id, px(p), py(p));
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

// Copy an asset subtree to a real filesystem directory so the host's std::ifstream
// paths work. Kept minimal: regular files only, directories created on demand.
static void extract_assets(AAssetManager *mgr, const char *asset_dir, const std::string &out_dir) {
    AAssetDir *dir = AAssetManager_openDir(mgr, asset_dir);
    if (dir == nullptr) {
        __android_log_print(ANDROID_LOG_WARN, "MGSA", "no asset dir %s", asset_dir);
        return;
    }
    mkdir(out_dir.c_str(), 0777);
    int copied = 0;
    const char *name;
    while ((name = AAssetDir_getNextFileName(dir)) != nullptr) {
        std::string rel = std::string(asset_dir) + "/" + name;
        std::string dst = out_dir + "/" + name;
        // A subdirectory has at least one entry. Try opening as a dir first.
        AAssetDir *sub = AAssetManager_openDir(mgr, rel.c_str());
        bool is_dir = false;
        if (sub != nullptr) {
            const char *first = AAssetDir_getNextFileName(sub);
            is_dir = (first != nullptr);
            AAssetDir_close(sub);
        }
        if (is_dir) {
            extract_assets(mgr, rel.c_str(), dst);
        } else {
            AAsset *a = AAssetManager_open(mgr, rel.c_str(), AASSET_MODE_STREAMING);
            if (a != nullptr) {
                std::ofstream f(dst, std::ios::binary);
                char buf[16384];
                int n;
                while ((n = AAsset_read(a, buf, sizeof(buf))) > 0) f.write(buf, n);
                f.close();
                AAsset_close(a);
                ++copied;
            }
        }
    }
    AAssetDir_close(dir);
    __android_log_print(ANDROID_LOG_INFO, "MGSA", "extracted %d files from %s", copied, asset_dir);
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
        extract_assets(g_app.app->activity->assetManager, "extract", files);
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

void render(android_app *app) {
    if (app->window == nullptr) return;
    const int w = ANativeWindow_getWidth(app->window);
    const int h = ANativeWindow_getHeight(app->window);
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
        // Diagnostic fallback: the guest produced only a transparent clear. Paint a
        // visible gradient + cross so the screen is not dead and touch can be tested.
        for (int y = 0; y < fh; ++y) {
            for (int x = 0; x < fw; ++x) {
                const std::size_t o = (static_cast<std::size_t>(y) * fw + x) * 4u;
                const bool cross = (std::abs(x - fw/2) < 3) || (std::abs(y - fh/2) < 3);
                frame[o+0] = cross ? 0xFFu : static_cast<std::uint8_t>(x * 255 / fw);
                frame[o+1] = cross ? 0xFFu : static_cast<std::uint8_t>(y * 255 / fh);
                frame[o+2] = cross ? 0x40u : 0x60u;
                frame[o+3] = 0xFFu;
            }
        }
    }

    ANativeWindow_Buffer buf;
    if (ANativeWindow_lock(app->window, &buf, nullptr) != 0) return;
    const int copy_w = fw < buf.width ? fw : buf.width;
    const int copy_h = fh < buf.height ? fh : buf.height;
    if (buf.format == WINDOW_FORMAT_RGBA_8888) {
        for (int y = 0; y < copy_h; ++y) {
            std::uint8_t *dst = static_cast<std::uint8_t *>(buf.bits) + y * buf.stride * 4u;
            const std::uint8_t *src = frame.data() + static_cast<std::size_t>(y) * fw * 4u;
            std::memcpy(dst, src, static_cast<std::size_t>(copy_w) * 4u);
        }
    }
    ANativeWindow_unlockAndPost(app->window);
}

void on_cmd(android_app *app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
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
        case APP_CMD_TERM_WINDOW:
            g_app.running.store(false);
            break;
        default:
            break;
    }
}

}  // namespace

void android_main(android_app *app) {
    g_app.app = app;
    app->onAppCmd = on_cmd;
    app->onInputEvent = on_input;

    int events;
    android_poll_source *source;
    while (g_app.running.load()) {
        const int timeout = (app->window != nullptr) ? 0 : -1;
        while (ALooper_pollOnce(timeout, nullptr, &events, reinterpret_cast<void **>(&source)) >= 0) {
            if (source != nullptr) source->process(app, source);
            if (app->destroyRequested != 0) {
                g_app.running.store(false);
                break;
            }
        }
        if (app->window != nullptr) render(app);
    }
    if (g_app.guest_thread != 0u) pthread_join(g_app.guest_thread, nullptr);
}
