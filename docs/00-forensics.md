# 00 — Forensic analysis: Metal Gear AC!D (Europe)

Phase 0 deliverable. Read-only analysis of the retail dump.

## Target

| Field | Value |
|---|---|
| File | `Metal Gear Ac!d (Europe) (En,Fr,De,Es,It).iso` |
| Size | 324,534,272 B (309.5 MiB) |
| SHA-256 | `CBEB13506E67572DF775B49039CCAFF7EF887911A1313AAA8A718EA956339123` |
| Filesystem | ISO9660 (`CD001` @ 0x8001) |
| Contents | 311 files / 53 folders, 290,703,752 B under `PSP_GAME` |
| Build dates | 2005-03-09 (system files), 2005-05-19 (game data) |
| Languages | En, Fr, De, Es, It (6 video variants per cutscene) |

## Verdict: no decryption required

| File | Magic | Meaning |
|---|---|---|
| `PSP_GAME/SYSDIR/BOOT.BIN` | `7F 45 4C 46` | **Plain ELF32**, `e_type=0xFFA0` (ET_SCE_PPURELEXEC), `e_machine=0x08` (MIPS), `e_flags=0x10A23001` |
| `PSP_GAME/SYSDIR/EBOOT.BIN` | `7E 50 53 50` | `~PSP` wrapped copy of the same module, internal name **`mgp_main`** |
| `PSP_GAME/USRDIR/stage/*/*.prx` | `7F 45 4C 46` | Plain ELF32, ET_SCE_PPURELEXEC |
| `PSP_GAME/USRDIR/modules/*.prx` | `7F 45 4C 46` | Plain ELF32 |

`pspdecrypt`, KIRK keys and any DRM work are **out of scope**: the dump already exposes
unencrypted REL executables. Use `BOOT.BIN` as the analysis target and ignore `EBOOT.BIN`.

`PSP_GAME/SYSDIR/UPDATE/` (13.25 MB) is a firmware updater shipped on the UMD. Ignore it.

## Executable inventory

| Binary | Code (.text) | Role |
|---|---|---|
| `BOOT.BIN` = `mgp_main` | 1,242,136 B | Engine core + boot |
| `stage/<id>/<id>.prx` × 44 | 17 KB – 105 KB each | Per-chapter game logic (overlays) |
| `modules/sound.prx` | 79,492 B | Konami audio service |
| `modules/kjfs.prx` | 47,268 B | Konami file system layer |
| `modules/zlibdec.prx` | 10,644 B | Decompression service |
| `modules/*.prx` (Sony) | — | `libatrac3plus`, `mpeg`, `psmf`, `audiocodec`, `videocodec`, `sc_sascore`, `libfont`, full `pspnet` stack |

`mgp_main` is a relocatable module: load base 0, `e_entry = 0x128`,
`.bss = 0xA0A80` (≈ 660 KB of static state).

Total recompilable code ≈ 1.5 M MIPS instructions — comparable to a Nintendo 64 title.

## Import surface (the HLE budget)

`mgp_main` imports **20 libraries / 121 NIDs** — 108 Sony NIDs plus 13 Konami-private.
This is the entire OS contract for the core. Full table: [`01-nid-map.md`](01-nid-map.md).

| Library | Funcs | Notes |
|---|---|---|
| `ThreadManForUser` | 25 | threads, semaphores, delays |
| `sceMpeg` | 23 | PSMF video playback |
| `sceGe_user` | 11 | GU display lists |
| `IoFileMgrForUser` | 9 | async file I/O |
| `UtilsForUser` | 7 | cache control, libc time |
| `sceDisplay` | 6 | framebuffer / vblank |
| `sceCtrl` | **2** | **`SetSamplingMode` + `ReadBufferPositive`** |
| `ModuleMgrForUser` | 5 | dynamic PRX load/unload |
| `SysMemUserForUser` | 3 | memory blocks |
| `sceSuspendForUser` | 3 | suspend/power |
| `InterruptManager`, `Kernel_Library`, `LoadExecForUser`, `StdioForUser`, `sceRtc`, `sceOpenPSID`, `sceUmdUser` | 3/2/2/3/1/1/2 | infrastructure |
| `zlibdec`, `sound`, `kjfs` | private | Konami services loaded from `USRDIR/modules` |

## ISA density (`mgp_main` `.text`, measured)

| Class | Count | Share |
|---|---|---|
| Instructions decoded | 310,790 | 100 % |
| COP2 — **VFPU** | 1,078 | **0.35 %** |
| COP1 — FPU | 18,689 | 6.01 % |
| `nop` | — | 2.2 % |

The VFPU finding closes the single largest technical risk of this port: `ghidra-allegrex`
documents incomplete VFPU/decompiler support, but this engine barely uses the vector unit.
3D-heavy PSP titles run 20–60 % VFPU in hot code; 0.35 % is close to noise level.

## Module dependency graph

```
mgp_main ──► zlibdec, sound, kjfs        (Konami services, PRX)
         ──► sceCtrl, sceGe_user, sceMpeg, sceDisplay, ...

sound.prx ──► sceAudio, sceAtrac3plus, sceSasCore, kjfs
kjfs.prx  ──► zlibdec, sceUmdUser, IoFileMgr
lobby.prx ──► sceNet, sceNetAdhocctl, sceNetAdhocMatching, kjnet, sceLibFont, sceUtility, sceImpose
title.prx ──► sceUsb, sceUtility, sceImpose
```

Stage modules import only Sony libraries, which means the engine API reaches them at
runtime (entry argument or exported symbol table) rather than by static linking.
Confirming this is a Fase 1 task — it determines how the runtime must satisfy
cross-module calls.

## Assets

| Group | Size | Format |
|---|---|---|
| `USRDIR/mpg` | 149.2 MB (75 files) | PSMF (`PSMF0012`), MPEG-PS, 6 language variants |
| `USRDIR/a3p` | 24.8 MB (52 files) | RIFF/WAVE, `WAVE_FORMAT_EXTENSIBLE`, 2ch 44.1 kHz → **ATRAC3plus** |
| `USRDIR/psq` | 12.6 MB (44 files) | magic `IPSQ8` — unknown, suspected streaming BGM |
| `USRDIR/stage/*/` | ~57 MB | payload packs |
| `USRDIR/modules` | 1.2 MB | 40 PRX |

Stage payloads observed:

- `_zar` — `u32 size` followed by a **zlib** stream (`78 9C`)
- `*.bin`, `*.psq` — sizeable opaque blobs
- no `GIM`/texture files at top level: textures and geometry are packed inside the stage blobs

## Consequences for the Android port

1. **No DRM work.** Start at `BOOT.BIN`.
2. **Modular engine.** The runtime must implement `sceKernelLoadModule` /
   `StartModule` / `StopModule` / `UnloadModule`. Every stage is a separate PRX.
3. **One input seam.** `sceCtrl` imports exactly two functions, and only
   `sceCtrlReadBufferPositive` is a poll. Replacing that single call is the whole touch
   pipeline — the most valuable finding of this phase.
4. **Standard audio path.** `sceAtrac3plus` + `sceSasCore`; either HLE them or decode
   offline. `audiocodec.prx`/`sc_sascore.prx` ship with the game.
5. **Standard video path.** `sceMpeg` + `psmf.prx`: no custom codec reverse engineering.
6. **Multiplayer is optional.** The `pspnet`/adhoc surface can be stubbed for
   single-player.
7. **No VFPU-heavy 3D world.** Stage modules are small and the game is card/tile based,
   so the GE workload is far below Monster Hunter Portable 3rd (our reference port).

## Open questions carried into Phase 1

- `IPSQ8` container layout (`.psq`).
- How stage modules reach engine functions (argument vs. exported symbol table).
- Internal layout of the `_zar` stage packs and the `.bin` blobs.
- Identity of the 13 Konami-private NIDs in `zlibdec`, `sound`, `kjfs`.
- The 17 unresolved Sony NIDs (4 in `UtilsForUser`/`sceSuspendForUser`, plus gaps in the
  `sceKernel*` tables) — resolve from local PPSSPP sources.

## Reproduction

```
7z l  "<iso>"                                   # 311 files / 53 folders
7z x  "<iso>" -o<out> PSP_GAME\SYSDIR\BOOT.BIN  # plain ELF, analysis target
python work/census.py                           # asset census
python work/headers.py                          # magic identification
python work/elf.py                              # program headers / sections
python work/imports.py                          # .lib.stub + .rodata.sceNid walk
python work/graph.py                            # per-module import graph
```

Scripts under `work/`; extracted samples under `extract/` (both git-ignored).

## Toolchain state

| Tool | Status |
|---|---|
| Python 3.11 | present |
| 7-Zip | present |
| git | present |
| JDK 21 (Temurin 21.0.13+9) | installed at `work/tools/jdk-21.0.13+9` |
| Ghidra 12.0 PUBLIC | installed at `work/tools/ghidra_12.0_PUBLIC`, headless verified |
| ghidra-allegrex v21.3 | installed at `Ghidra/Processors/ghidra-allegrex`, language present |

Launch with `work/run-ghidra.ps1` (sets `JAVA_HOME` to the bundled JDK 21).
