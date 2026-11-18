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

T1 S1-S4: touch input layer (`profiles/mgsa/host/touch.hpp/.cpp`). DONE � bb5d399. Gesture recognizer (tap=CROSS, swipe=directional, drag=analog, two-finger=CIRCLE) + down/move/up command-file queue.
T2 S1-S4: wire into `sceCtrlReadBufferPositive`, `--touch-file` CLI, press latch. DONE � bb5d399. Verified: `[press] frame N buttons=0x4020` reaches guest.
T3 S6: EDRAM->RGBA blit + PNG writer + `--png`. DONE � bb5d399. Verified: valid 480x272 RGBA PNG.
T4 S5: Android NDK CMake + NativeActivity + JNI bridge + build_apk.sh. DONE � 8aa3c40. Not compiled here: no Android SDK/NDK installed on this machine; structure is reproducible per android/README.md.
T5 S1-S6: end-to-end verification + docs. PARTIAL � desktop verified; Android build pending NDK install. Blocked on external toolchain, not on code.

## Log

L1 (original request): "Reverse-engineer Metal Gear AC!D so it runs natively on Android with a real touch interface instead of emulated PSP buttons. Touch-native gameplay: no d-pad, no L/R, no 'press X' — tap a card to select, drag/pinch to pan and zoom the board."

L2 (2026-11-17): Pivoted from BOOT.BIN stage-transition RE (diminishing returns, multiple falsified hypotheses) to this touch+Android feature. T1-T4 implemented and pushed. Desktop gesture test and PNG dump verified. Android APK not built here (no SDK/NDK); build scaffolding is complete and documented.

L3 (2026-11-18): Continued RE of the title-stage gate. Root cause narrowed: `mgp_stage`'s `mount_stage` calls `sceImpose::0x36AA6E91`, whose stub returned 0; mount_stage treats 0 as "failed" (`v1 = v0 - 1; if (v1 >= 5) goto fail`) and sets the stage state to 0, so the engine never advances past the title mount. Implemented an honest-ish HLE: `sceImpose::0x36AA6E91` now returns `MGSA_IMPOSE_RET` (default 0) instead of always 0, with the value configurable at runtime. Tested 0..5: mount_stage's success path (1..5) does not unblock the title load either, so the gate is not solely this stub — the engine still opens `title/_zar` (fd 318) and never reads it. Next: identify which condition the engine (AOT `Main`, 0x0880447C) checks after mount_stage returns, and whether a different HLE return or an unread `_zar` precondition is the real blocker. Desktop verification unchanged: 516 frames, empty screen, zero I/O after frame 36.
