# Metal Gear AC!D — native touch edition

This is a native reimplementation of the core card-game loop, not the 2005 binary.
The PSP engine in this repository boots and renders but never finishes the
`title` stage load (see `docs/07-stage-load-re.md`), so it cannot be the product
path. This directory contains an independent C++ game built on the extracted
game text and the documented AC!D rules.

## Controls

- Tap a card in your hand, then tap a board cell to play it.
- Drag on the board to pan; pinch to zoom.
- `END TURN` passes the turn. Ending a turn without playing a card costs 4 COST.
- `SYSTEM MENU` offers Resume and Restart.
- A `Redraw` card is played by tapping it and then a second card.

## Rules

An 8x8 board with walls and an exit. You have 8 HP and 10 COST per turn. Guards
have a forward detection cone blocked by walls; an alert guard attacks or moves
toward you. Win by reaching the exit or removing every guard; lose at 0 HP.
Card names and the menu copy come from `work/title_ui_strings.txt` and
`work/gcx_string_table.txt`. The rules are a reconstruction; no bytecode from
the original game is executed.

## Build and test

Desktop tests (no Android SDK needed):

```sh
g++ -std=c++17 -I include src/game.cpp src/render.cpp tests/game_tests.cpp -o mgsa_tests
./mgsa_tests
```

Android (NDK r27c, 16 kB page alignment):

```sh
cmake -DCMAKE_TOOLCHAIN_FILE=<ndk>/build/cmake/android.toolchain.cmake \
  -DANDROID_NDK=<ndk> -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 \
  -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON -DCMAKE_BUILD_TYPE=Release \
  -S android -B android/build
cmake --build android/build -j 4
powershell -ExecutionPolicy Bypass -File package_touch_apk.ps1
apksigner sign --ks <keystore> --out native-touch.apk native-touch-aligned.apk
```

The app uses a separate package (`io.github.juanklv2003.mgsa.touch`) so it does
not replace the earlier engine-based APK.
