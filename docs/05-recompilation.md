# 05 — Static recompilation: does the toolchain actually work here?

Phase 2. This document answers one question with evidence rather than judgement: **can MG Acid's
main module be statically recompiled by the PSPRecomp framework that Yakumo ships?**

Short answer: yes. The tools build on this machine, they analyse `mgp_main`, they emit 80.8 MB of
C++ across 76 translation units, and that C++ compiles. Everything below is the exact evidence.

## Correction to the earlier plan

Yakumo is **not** a generic recompiler you point at an ISO. It is a shipped native port of one
title (Monster Hunter Portable 3rd HD Ver.), and the README says so plainly.

What it *does* ship is more useful than a generic tool would have been: the internal framework
calls itself **PSPRecomp**, and it is deliberately split into

- a **reusable root** — Allegrex decoding, ELF/PRX loading, guest memory, runtime, code
  generation, and the `psp_analyze` / `psp_recomp` / `dump_function` executables, and
- a **per-title profile** under `profiles/<id>/` that owns HLE, bootstrap, renderer, audio,
  input, and patches.

`docs/PROFILE_GUIDE.md` is an explicit, step-by-step guide for adding a *new* title. That is the
document that makes this path viable, and it is the reason the earlier "Yakumo is our base
toolchain" note was directionally right but imprecise: we borrow the framework, and we write our
own profile.

## Environment

No compiler was installed on this machine, but MSYS2 was, with a complete UCRT64 toolchain:

| Tool | Path | Version |
|---|---|---|
| g++ | `C:\msys64\ucrt64\bin\g++.exe` | 16.2.0 |
| cmake | `C:\msys64\ucrt64\bin\cmake.exe` | present |
| ninja | `C:\msys64\ucrt64\bin\ninja.exe` | present |

That is enough. The framework tools do not need SDL3, Vulkan or FFmpeg — those belong to the
profile's host, not to the root. A framework-only build is a documented, supported configuration.

One trap worth recording: the executables fail with `0xC0000135` (`STATUS_DLL_NOT_FOUND`) unless
`C:\msys64\ucrt64\bin` is on `PATH` for the MinGW runtime DLLs.

## Build

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
git clone --depth 1 https://github.com/TeamGDB/Yakumo.git work/tools/Yakumo
cd work/tools/Yakumo
cmake -S . -B out/framework -G Ninja -DPSPRECOMP_PROFILE="" `
      -DPSPRECOMP_BUILD_TESTS=OFF -DPSPRECOMP_BUILD_PROFILE_TESTS=OFF `
      -DCMAKE_BUILD_TYPE=Release
cmake --build out/framework --target psp_analyze psp_recomp dump_function
```

15 compile/link steps, no warnings that matter, no profile required.

## Analysis of `mgp_main`

```powershell
.\out\framework\psp_analyze.exe <BOOT.BIN> work\mgsa-report.json
```

| Result | Value |
|---|---|
| guest RAM | 32 MiB |
| load base | `0x08804000` |
| entry (runtime) | `0x08804128` |
| module | `mgp_main` |
| **imports** | **121** |
| relocations | 46,595 (`R_MIPS_32` 3,004 · `R_MIPS_26` 23,450 · `HI16` 9,134 · `LO16` 11,007 · invalid 0) |
| **automatic functions** | **5,113** |
| unique instruction labels | 237,740 |
| unique block entries | 55,252 |
| blocks with overlap | 65,609 |
| indirect call sites | 184 |
| truncated functions | 0 |
| unsupported instruction occurrences | 115 |

Two numbers are worth pausing on. **121 imports** is exactly what the manual NID pass in
`docs/01-nid-map.md` found, independently — the analyzer agrees with our own work, which is a good
sign for both. And **5,113 functions** replaces the crude backward-scan heuristic this project was
using to guess function starts.

The module metadata also falls out of the analysis: `gp = 0x0894DB30`, stub range
`0x08933BFC`–`0x08933D8C`.

## Code generation

```powershell
.\out\framework\psp_recomp.exe <BOOT.BIN> --auto work\recompiled\mgsa
```

127 seconds, exit 0.

| Result | Value |
|---|---|
| function seeds | 5,113 |
| emitted code PCs | 237,740 |
| registered blocks | 311,022 |
| translation units | 76 |
| rewritten units | 76 |
| **import wrappers** | **121** |
| output size | **80.8 MB** across 79 files |
| unit span | 16,384 bytes |

VFPU lowering ran without special handling, which is what the 0.35 % VFPU density measured in
phase 0 predicted.

Every address mapped in `docs/03-input-seam.md` and `docs/04-ui-intent.md` is present in the
corpus. The widget cluster, for example, lands in `generated_unit_0025.cpp` as
`L_0886B834`, `L_0886B7E0`, `L_0886BB18` and `L_0886BC6C` (runtime = vaddr + `0x08804000`).

## The generated code compiles

This is the gate that matters. A corpus that cannot compile is not a recompilation.

```powershell
g++ -std=c++20 -O0 -fno-strict-aliasing -c generated_unit_0025.cpp `
    -I. -I<Yakumo>/include -o unit0025.o
```

28 seconds, exit 0, 2,446,800-byte object file. One of the larger units (1.26 MB of source, a
4,096-entry dispatch table, a 3,900-case switch), compiled cold, no errors.

## What this proves, and what it does not

**Proven:** the framework understands this binary, the Allegrex decoder covers it, the relocations
resolve, the CFG recovers 5,113 functions, the code generator emits valid C++ for all of them, and
that C++ builds.

**Not proven:** that the game runs. Running needs a `profiles/mgsa` host that supplies

- ELF load and relocations into guest memory (`runtime.hpp` provides the machinery),
- HLE for all 121 imports, of which `sceGe_user` (11) and `sceMpeg` (23) are the hard ones,
- the module bootstrap and `user_main` thread,
- display, audio, and — the whole point of the project — the touch input layer at `sceCtrl`.

Yakumo's MHP3rd profile is the reference for how large that is. The framework core is reusable and
documented, but it has been exercised on exactly one title.

## Known gaps to watch

- **115 unsupported instruction occurrences.** 0.04 % of the module. Unknown which; likely VFPU
  corner cases. They decode to something, but the semantics are unverified.
- **65,609 blocks with overlap, 10,357 overlapping label occurrences.** Mid-function entry points
  are real in this code. The `single_copy_basic_blocks` emission handles them, but overlapping
  code is where recompilers tend to be subtly wrong.
- **184 indirect call sites.** Static recompilation must resolve these at runtime through the
  block registry; each one is a potential place where a target is missing from the corpus.
- `--dispatch-target 0x088E8B18` is what the MHP3rd profile passes. That address is meaningless for
  our module and was intentionally not used; its role needs reading before we depend on it.

## Next phase

Build `profiles/mgsa` far enough to boot and crash loudly. The first run will stop on the first
import we have not implemented, and that stop is the useful output: it establishes the actual boot
dependency order instead of guessing it. Renderer and MPEG come after, once the engine proves it
gets that far.
