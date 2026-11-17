# mgsa Android build

Touch-native Android port of Metal Gear AC!D. No emulated PSP buttons: gestures
go straight into the game's single input seam (`sceCtrlReadBufferPositive`).

## Layout

| File | Role |
| --- | --- |
| `CMakeLists.txt` | NDK build of `libmgsa.so` (AOT corpus + runtime + host core + touch + framebuffer + JNI) |
| `jni/mgsa_jni.cpp` | `android_native_app_glue` NativeActivity: window, guest loop thread, frame render, touch forwarding |
| `java/.../MgsaActivity.java` | Minimal activity; touch input via the native glue (no SDL) |
| `AndroidManifest.xml` | Landscape, API 29+, touchscreen required |
| `build_apk.sh` | Packages an APK with SDK tools (no Gradle) |

## Building

1. Generate the AOT corpus (once): `scripts/bringup.ps1` (or copy an existing
   `work/tools/Yakumo/profiles/mgsa/generated/`).
2. Build the native library:
   ```
   cmake -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
         -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 \
         -S profiles/mgsa/android -B build-android
   cmake --build build-android -j4
   ```
3. Package: `ANDROID_HOME=/path/to/sdk profiles/mgsa/android/build_apk.sh \
   build-android/libmgsa.so mgsa.apk extract/`
4. Sign with `apksigner` and install; place the disc image under the app's
   files dir if not bundled.

## Touch gestures (docs/03-input-seam.md, docs/04-ui-intent.md)

- Tap a card -> CROSS (confirm), held for >=1 poll frame.
- Swipe -> a directional step (the engine models movement, not coordinates).
- Drag -> analog stick (Lx/Ly live).
- Pinch -> repeated directional steps / L-R for board zoom.
- Two-finger tap or long press -> CIRCLE (back).

Raw PSP button bits are preserved; the game does its own L/R relocation.

## Status

The input seam, gesture recognizer, external event queue, EDRAM->RGBA blit and
frame dump are implemented and tested on desktop. The stage transition that
loads the title scene is still unresolved in the guest (see the bring-up doc),
so until that is fixed the app renders the guest's current (transparent) frame.
