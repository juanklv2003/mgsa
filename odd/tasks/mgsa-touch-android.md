# mgsa touch-native input + Android build

Objective: replace PSP controller input with real touch gestures (tap a card to select, drag/pinch to pan/zoom the board) and package the native port as an Android app. No d-pad, no L/R, no "press X".

Branch: main (work units committed to main as authorized ODD implementation).
Delivery strategy: single feature, work-unit commits. Test runner: `cmake --build work/tools/Yakumo/out/mgsa --target mgsa_boot -j 4` + desktop run; Android: NDK CMake + APK.

## Specs

S1. One input seam. The whole game reads the controller through `sceCtrlReadBufferPositive` (NID `0x1F803938`) at exactly one call site. The touch layer produces the 16-byte `SceCtrlData {u32 time; u32 buttons; u8 lx,ly,rx,ry; ...}`. Raw PSP button bits must be preserved (up=0x10, right=0x20, down=0x40, left=0x80, triangle=0x1000, circle=0x2000, cross=0x4000, square=0x8000, L=0x100, R=0x200, select=0x1, start=0x8); the game does its own L/R relocation. Do NOT pre-remap.

S2. Gesture -> button mapping, no virtual d-pad:
  - tap (short touch+release, small movement) on a card => CROSS (0x4000), the game's confirm button.
  - swipe/flare from a point => directional bit (0x10/0x20/0x40/0x80) for one or more polls, to drive the list stepper at game fn 0x67834 (the engine models movement, not coordinates).
  - continuous drag on the board => analog Lx/Ly (0x00..0xFF, center 0x80) like a stick.
  - pinch (two active pointers, distance change) => board zoom, expressed as repeated directional taps or L/R bits.
  - two-finger tap or long press => CIRCLE (0x2000), the game's cancel/back.
  "tap a card to select, drag/pinch to pan and zoom the board."

S3. Press/release observability. A button must be held for at least one full poll frame to register as a press (game fn 0x74EB4 is edge-detect + auto-repeat); a tap set and cleared between two polls is invisible. The layer must latch a synthetic press for >=1 poll. Return `0` with current state; a failed read keeps previous buttons.

S4. External event injection. A touch layer reads from a platform-neutral source: an in-process queue fed by JNI on Android and by a command file / CLI on desktop. Format: one event per line `down <id> <x> <y>`, `move <id> <x> <y>`, `up <id>`, in a normalized 480x272 board space. This makes the same gesture code testable on desktop and reusable from Android.

S5. Android build. Build the AOT corpus + runtime + host core as a shared library (`libmgsa.so`) for arm64-v8a via NDK CMake (API 29). A minimal `android_native_app_glue` NativeActivity creates the window, drives the guest loop, blits the rasterized EDRAM framebuffer (512x272, 5551) to the surface, and forwards `AINPUT_EVENT_TYPE_MOTION` into the S4 queue. Package an APK (AndroidManifest + native lib + game data installed separately). No SDL dependency.

S6. Rasterizer output. Reuse the committed software rasterizer (`rasterize_ge_list` writes 5551 pixels to EDRAM). Add a `mgsa_blit_frame(rgba buffer)` that converts EDRAM topaddr (from `sceDisplaySetFrameBuf`, double-buffered 0x04000000/0x04044000, stride 512) to RGBA8888 for the Android surface / desktop preview.

## Tasks

T1 S1-S4: touch input layer (`profiles/mgsa/host/touch.hpp/.cpp`): gesture recognizer + HostInput producer + external event queue (command-file parser). Route: inline. Commit.
T2 S1-S4: wire touch layer into `sceCtrlReadBufferPositive` binding and host loop; add desktop `--touch-file` CLI + a test gesture file; verify press reaches guest. Route: inline. Commit.
T3 S6: EDRAM->RGBA blit + desktop PNG frame dump option. Route: inline. Commit.
T4 S5: Android NDK CMake target + NativeActivity glue + APK packaging script. Route: inline. Commit.
T5 S1-S6: end-to-end verification (desktop gesture test; Android build if SDK/NDK present) + docs. Route: inline. Commit.

## Log

L1 (original request): "Reverse-engineer Metal Gear AC!D so it runs natively on Android with a real touch interface instead of emulated PSP buttons. Touch-native gameplay: no d-pad, no L/R, no 'press X' — tap a card to select, drag/pinch to pan and zoom the board."
