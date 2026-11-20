Objective: replace the stalled PSP-engine path with a native, touch-first implementation of the core Metal Gear AC!D card game, using the extracted game text and data formats.
Branch: main. Delivery: work-unit commits on main; push and device install remain explicit user decisions. Test runner: standalone desktop C++ test binary (`native/tests`) plus on-device Pixel 9a verification.

## Specs

S1. "Y NO SE PUEDE HACER JUGABLE EL JUEGO?" — deliverable is a genuinely playable game on Android, not a boot/log demo.

S2. "Touch-native gameplay: no d-pad, no L/R, no 'press X' — tap a card to select, drag/pinch to pan and zoom the board." Acceptance: every game action is reachable by tap/drag/pinch; no emulated PSP button is required.

S3. "Native Android build, not PPSSPP with button mapping." Acceptance: an APK built with NDK/CMake and `android_native_app_glue`, running our own C++ game loop; no recompiler runtime in the product path.

S4. "Reimplementar jugabilidad nativa (Recommended)" — "Construir el bucle de juego touch-native sobre los datos extraídos. Es la única ruta con final previsible para llegar a 'jugable', pero no ejecuta el código original." Acceptance: the game uses real extracted strings and the documented AC!D rules, and the README states clearly that it does not execute the 2005 binary.

S5. "cuando acabes y necesites el movil aviame" — verify on the user's physical Pixel 9a only; notify before requiring the device. PPSSPP is not an oracle and not a product path.

S6. "Do not present fallback/diagnostic screens as progress" — acceptance: no overlay titled "SYSTEM MENU" or similar diagnostic surface is shipped as gameplay. Every rendered screen is a real game state (title, mission, board, result).

S7. Real game copy from `work/title_ui_strings.txt` (verbatim, English set):
"End the turn."
"(4 COST if no cards used.)"
"Attack the enemy you are facing"
"with hand-to-hand combat. (COST: 4)"
"Knock on the wall. (COST: 0)"
"Select 2cards from your hand to discard,"
"then draw 2 new cards from the deck."
"(COST: 6)"
"Display the System Menu."
Acceptance: those exact strings appear in the corresponding actions; card/ability names use the extracted set (`Quick Draw`, `Preemptive Atk.`, `Double Action:Cost+`, `Tie-In Card`, `Support Atk.`).

S8. Core AC!D rules as observed in the extracted strings and format work: grid movement, cards with COST, hand-to-hand attack (COST 4), knock on wall (COST 0), discard-two/draw-two (COST 6), end turn (COST 4 when no cards used), guards with facing/detection, mission win/lose. Acceptance: a full mission can be started, played and finished; illegal actions are rejected with no state change.

## Tasks

- T1 — S4,S7,S8: design the portable data model (board, card, guard, turn/COST, mission) and freeze the playable slice. Route: inline. Status: done (see L6). Commit: -
- T2 — S8: implement pure C++ game rules with deterministic tests through the public interface (movement, attack, detection, turn/COST, deck/discard, win/lose). Route: inline. Status: done — 67 checks, 0 failures. Commit: -
- T3 — S2,S3,S6,S7: implement the touch renderer and NativeActivity loop (tap card, tap target, drag pan, pinch zoom, real title/board/result screens). Route: inline. Status: pending. Commit: -
- T4 — S1,S5: package the APK with the existing `work/package_apk.ps1` pipeline and verify on the Pixel 9a. Route: inline. Status: pending. Commit: -

## Log

L1 (original request, verbatim): "Y NO SE PUEDE HACER JUGABLE EL JUEGO?" → assistant: technically yes, but months of work across four layers; user: "si, sigue".

L2 (continuation, verbatim): "dale" / "dale" / "sigue" — execute the highest-ROI next step, no menus, no diagnostic screens presented as progress.

L3 (2026-11-20, user answer to the strategy question, verbatim): "Reimplementar jugabilidad nativa (Recommended)" with description "Construir el bucle de juego touch-native sobre los datos extraídos. Es la única ruta con final previsible para llegar a 'jugable', pero no ejecuta el código original." This authorizes the native reimplementation and closes the original-engine stage-load investigation as the product path.

L4 (2026-11-20, verbatim): "cuando acabes y necesites el movil aviame" — finish desktop work first, then ask for the device.

L5 (evidence, 2026-11-20): original-engine path exhausted for product purposes — 516-frame runs never load `title.prx`; `sceCtrlReadBufferPositive` is called 258 times and START reaches the guest but changes nothing; kjfs virtual root stays count=5 and byte-identical across the title mount; every GE draw is a full-screen black clear sprite, so EDRAM zero is correct guest behavior. Documented in `docs/07-stage-load-re.md` and committed as 097ea9f.

L6 (T1 design, 2026-11-20): frozen playable slice — 8x8 grid (Floor/Wall/Exit), player with HP and facing, guards with HP/facing/patrol/alert and a forward detection cone blocked by walls; a fixed 18-card deck using extracted names; hand of 5, COST 10 per turn. One public entry point `mgsa_act(Game&, const Action&)` returns false and leaves state untouched on any illegal action. Actions: StartMission, PlayCard(hand index, target cell), EndTurn, Redraw(two indices), Restart. Turn flow: spend COST on cards; EndTurn costs 4 when no card was played; guards detect/attack/patrol; win by reaching Exit or removing all guards, lose at 0 HP. The renderer shows four real states (Title, Briefing, Board, Result); no diagnostic overlay. Deterministic core (fixed deck order, no RNG) so tests assert exact state through the public API. This is a reconstruction of the documented rules, not a claim of bytecode fidelity.

L7 (T2 evidence, 2026-11-20): `native/include/mgsa/game.hpp`, `native/src/game.cpp`, `native/tests/game_tests.cpp`, `native/CMakeLists.txt`. All rules go through `mgsa_act()` on a copy, so rejected input is atomic. 67 checks pass with g++ -std=c++17 -Wall -Wextra.
