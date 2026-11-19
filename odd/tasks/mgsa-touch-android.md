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
T5 S1-S6: end-to-end verification + docs. PARTIAL � desktop verified; Android APK built with NDK r27c and installed on the Pixel 9a. Native touch surface renders and polls input, but the guest never loads stage content, so the game area stays black. Norton Mobile Security repeatedly uninstalls the APK; a package exception is needed.
T6 S6: stage content never loads after the title mount. BLOCKED -- see L4. The engine is alive (516 frames, 258 input polls) but mgp_stage's stage-state pointer reads as null, and no title.prx is ever loaded.

## Log

L1 (original request): "Reverse-engineer Metal Gear AC!D so it runs natively on Android with a real touch interface instead of emulated PSP buttons. Touch-native gameplay: no d-pad, no L/R, no 'press X' — tap a card to select, drag/pinch to pan and zoom the board."

L2 (2026-11-17): Pivoted from BOOT.BIN stage-transition RE (diminishing returns, multiple falsified hypotheses) to this touch+Android feature. T1-T4 implemented and pushed. Desktop gesture test and PNG dump verified. Android APK not built here (no SDK/NDK); build scaffolding is complete and documented.

L3 (2026-11-18): Continued RE of the title-stage gate. Root cause narrowed: `mgp_stage`'s `mount_stage` calls `sceImpose::0x36AA6E91`, whose stub returned 0; mount_stage treats 0 as "failed" (`v1 = v0 - 1; if (v1 >= 5) goto fail`) and sets the stage state to 0, so the engine never advances past the title mount. Implemented an honest-ish HLE: `sceImpose::0x36AA6E91` now returns `MGSA_IMPOSE_RET` (default 0) instead of always 0, with the value configurable at runtime. Tested 0..5: mount_stage's success path (1..5) does not unblock the title load either, so the gate is not solely this stub — the engine still opens `title/_zar` (fd 318) and never reads it. Next: identify which condition the engine (AOT `Main`, 0x0880447C) checks after mount_stage returns, and whether a different HLE return or an unread `_zar` precondition is the real blocker. Desktop verification unchanged: 516 frames, empty screen, zero I/O after frame 36.

L4 (2026-11-19): MEASURED the post-mount path instead of guessing. Correction to L3: the title archive IS read. Frame 36 sequence from work/run48.log: kjfs open e001system.psq -> ISO LBA 0x1bc30, 56-byte header read then 792724-byte payload into 0x09CB7000, close; mount_stage('.../stage/title/data.cnf') -> kjfs open ISO LBA 0x1c8c0 size 0xdfbc3 (fd 318); preseek seeks fd 318 and exits; no further I/O for the remaining 480 frames. sceCtrlReadBufferPositive is called 258 times (once per presented GE list), so the game loop is alive and polling input.

Decoded mgp_stage with relocations applied (work/dis_prx.py, work/reloc_dump.py): module_start at +0x34 publishes {code base, data base, code base}; state setter at +0x1B8 and getter at +0x25C both do `lw v*,0x4A5C(v*)` after `lui v*,0x0000`. There is NO relocation at .text+0x204, so the address is genuinely 0x4A5C. work/run51.log instrumented the single sceImpose call and measured: gp=0x0894DB30, word(0x00004A5C)=0, word(0x08004A5C)=0, word(0x08804A5C)=0x3C04000F (instruction, not data), word(gp+0x4A5C)=0. With language id 1 (English), the setter's jump table at 0xDF50 entry[0] writes 0, so get_state returns 0 and the engine never advances. This is the concrete reason the stage never loads; the fix still needs the correct source of that pointer on real PSP hardware.

Also corrected: MGSA_IMPOSE_RET cannot be the gate. The sceImpose return value is discarded by +0x1B8 at 0x1D8 (lw v0,0(sp) overwrites it), and language 1 already takes the valid table entry. Committed f502bcd (GE CALL/RET decode, 3-component s16 vertex stride, arena first-fit/coalescing and real FPL/partition frees), baseline reconfirmed at 516 frames / 412793 dispatches / 8712 ms guest time, pushed to origin/main. Temporary probes removed; working tree clean.

L5 (2026-11-19): FOUND THE POINTER'S ORIGIN. The raw `lui rX,0` + `lw rY,0x4A5C(rX)` pair is a zero-based address with no relocation entry, used by the boot binary and by every stage PRX (work/find_4a5c.py: 259 `lw r2,0x4A5C(r2)` in BOOT.BIN alone). The AOT recompiler resolves the identical boot-binary pair at guest 0x08808108 into `ctx.gpr[2] = 2196u << 16` (0x08940000) + `aot_load32(gpr[2] + 10204)` (0x27DC); a second site at guest 0x08809CF4 resolves the same way. So the game's loader rebases zero-based 0x4A5C by 0x0893DD80, giving guest address 0x089427DC, which holds 0x089D2920 at runtime. Secondary PRX modules never get that fix-up from our loader, so mgp_stage dereferences unmapped 0x4A5C and reads null.

An env-gated experiment patched only mgp_stage's setter/getter pairs (module offsets 0x0204 and 0x025C) to the 0x0894/0x27DC pair: the engine advanced past the stall but crashed at guest 0x1128055C with Main at pc 0x08809564 (work/run56.log). Patching all five pairs crashed identically. The slot's +4 word is 0x089D85A0, so writing a 16-bit state there looks wrong -- the PRX fix-up may target a different per-module slot even though the rebase delta is proven. All experimental code was removed; the host is back at the committed state. Next: identify the per-module target of the game's zero-address fix-up rather than assuming the main ELF's slot.

L6 (2026-11-19): CORRECTION TO L5 -- the fixup is NOT missing. A write-watch over the mgp_stage range (work/run57.log) shows the guest's own code at pc=0x088050D0 rewriting every zero-based pair during module load: 0x09FDF204 `lui v1,0` -> `lui v1,0x0894`, 0x09FDF20C `lw v0,0x4A5C` -> `lw v0,0x27DC`, and the same at 0x25C/0x260 and all other 0x4A5C sites. Different sites receive different bases (0x09FDF008 gets 0x089D with offset 0xA1F4). In a clean run the state pointer is therefore valid: 0x089427DC holds 0x089D2920. The earlier null-pointer conclusion came from probing the pre-patch address 0x4A5C and was wrong. The manual repair was redundant and produced a crash in Main at 0x08809564 whose cause is still unexplained. Next: treat the stage state machine as working and find the real downstream reason the engine never loads title.prx.

L7 (2026-11-19): RULED OUT several gates and reframed the black screen. Measured: `mount_stage` actually returns 0 (work/run58.log), and forcing it to 1 with MGSA_MOUNT_RET changes nothing (run60). Changing sceUtilityGetSystemParamInt language 1->2 (MGSA_LANGUAGE) changes nothing (run59). Reporting the mscmhc0: memory-stick devctl 0x02025806 as present shifts 516->520 frames but does not unblock (run61). No PMF/movie is ever opened and sceMpeg is never called past module init, so the 'title is a PMF needing substituted videocodec' theory is unsupported.

The engine is demonstrably alive: 106 zlibdec decompressions, 398 sceIoDread, all 50 stage directories scanned, 258 sceCtrl polls, 520 frames. The black pixels come from the guest's OWN GE list: `work/run44.log [gedump]` shows `ALPHATESTENABLE 0x22000001` set immediately before the single `PRIM 0x04060002` (2-vertex fullscreen sprite), with BLENDENABLE 0 and alpha ref 0, so every fragment is discarded and EDRAM stays 0 non-zero words. The blank screen is a symptom of the engine deliberately drawing nothing, not proof the render pipeline is broken (the Android pipeline was verified on device in #77). Touch injection works: work/taps.txt with `at <frame> down/up` produced `[press] frame 46 buttons=0x00004000` (run64). All host experiments were reverted; working tree clean and binary rebuilt from clean source. Next: the gating condition is in BOOT.BIN's main loop (~0x088047B8 caller per #70); decompile that path to find why the frame loop never requests the title stage.

L8 (2026-11-19): TOOK MY OWN ADVICE -- a bounded dynamic trace beat static RE. Instrumented on_pre_dispatch (env MGSA_TRACE_STAGE) to capture Main-thread dispatches in exactly frame 36 and stop at frame 37; each iteration then took ~53s instead of 385s. work/run68.log + run69.log show:

1. Main DOES execute the full stage-load path in frame 36: calls into mgp_stage (ra=0x09FE1898 returns v0=1, ra=0x09FECC34 returns v0=0x145=325), calls sceImpose, sceUtility, devctl, etc. It then returns to the frame limiter 0x0887BF38. So there is no untaken gate -- the decision runs to completion and the engine chooses not to load title.prx.
2. The engine's task dispatcher is at AOT 0x089237D0 (generated_unit_0071.cpp:17683). It checks `task_index < 5`, then calls a handler from a table at 0x0893CEE4; otherwise returns 0. Volatile table (run69): [0]=0x089237FC, [1]=0x08923810, [2]=0x08923810, [3]=0x08923818, [4]=0x08923830, [5]=0x089238F0. Handlers 1 and 2 are 0x08923810 which just returns 0 (no-op tasks).
3. Active task mask at 0x089C207C = 0x00200287 (bits 0,1,2,7,13,21). Frame counter at 0x089B7DD8 = 0.

Next concrete step: map which mask bit is the title-stage load task (read mask consumers at generated_unit_0071.cpp lines 15699/15976/16031/16048/16615/16883/18027) and determine why its bit is not set / its handler is a no-op. The fast trace technique is the way to verify each hypothesis. All trace code reverted; working tree clean.
