# mgsa — Metal Gear AC!D, native on Android

Reverse-engineering research and, eventually, a native Android build of *Metal Gear AC!D*
(PSP, Konami, 2005) — with a real touch interface instead of emulated PSP buttons.

> **Bring your own copy.** No game code, assets, or disc images live in this repository.
> Everything under `docs/` is original analysis. This project ships tooling and findings,
> never the game.

## Why not an emulator

Emulators map touch zones to PSP buttons, so the game keeps thinking in d-pads and X.
The goal here is that a tap on a card *is* a card selection. That requires native code,
which means static recompilation: Allegrex MIPS translated ahead of time to C++ and run on
an HLE reimplementation of the PSP system software.

## Status

| Phase | Scope | State |
|---|---|---|
| 0 | Binary forensics, toolchain | **done** |
| 1 | Container formats, module linkage, engine API | in progress |
| 1.5 | Input seam (done) + UI state machine map | in progress |
| 2 | PC bring-up (recompiled build) | pending |
| 2.5 | Touch layer | pending |
| 3 | Android packaging | pending |

## Findings so far

- `PSP_GAME/SYSDIR/BOOT.BIN` is a **plain ELF32** — no decryption required. Internal
  module name: `mgp_main`.
- 20 imported libraries / 121 NIDs (108 Sony, 13 Konami-private).
- **VFPU density: 0.35 %** — the usual blocker for Allegrex recompilation is absent here.
- `sceCtrl` is polled through **one** call: `sceCtrlReadBufferPositive` at `0x00074908`, with
  exactly one direct call site and zero indirect references. That single call is the entire
  input seam the touch layer has to replace.
- The game consumes raw PSP `SceCtrlData` (buttons + `Lx`/`Ly`) and does its own remapping and
  edge detection, so a gesture layer only has to produce 16 bytes per frame.
- Modular engine: `mgp_main` + 44 per-stage PRX overlays + `sound` / `kjfs` / `zlibdec`
  services loaded at runtime.

Full detail:

- [`docs/00-forensics.md`](docs/00-forensics.md) — binary, modules, assets, toolchain
- [`docs/01-nid-map.md`](docs/01-nid-map.md) — complete import map
- [`docs/02-containers.md`](docs/02-containers.md) — `_zar`, `.psq`, `.a3p`, `.pmf`
- [`docs/03-input-seam.md`](docs/03-input-seam.md) — the single input call and pad state
- [`docs/04-ui-intent.md`](docs/04-ui-intent.md) — what the game does with that input; where a tap can and cannot reach

## Repository layout

```
docs/     analysis output (committed)
work/     local scripts and toolchain (git-ignored)
extract/  files pulled out of the disc image (git-ignored)
```

## Toolchain

Python 3.11 · 7-Zip · JDK 21 · Ghidra 12.0 · ghidra-allegrex v21.3

`work/run-ghidra.ps1` launches Ghidra with the bundled JDK.
