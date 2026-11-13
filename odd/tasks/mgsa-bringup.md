# mgsa-bringup — minimal host profile that boots and fails loudly

Branch: `main`. Delivery: single work unit, committed as one reviewable slice.
Runner: PowerShell 7 on Windows, MSYS2 UCRT64 g++ 16.2.0, CMake + Ninja.

Objective: turn the verified PSPRecomp corpus into an executable that loads `mgp_main`, runs it,
and stops with a precise reason — so the real boot dependency order becomes measured data instead
of a guess.

## Specs

- **S1** — The project must reproduce, from a clean clone, a framework build producing
  `psp_analyze`, `psp_recomp` and `dump_function` with only MSYS2 UCRT64 plus CMake and Ninja.
  No Visual Studio, no SDL3, no Vulkan, no FFmpeg.
- **S2** — `psp_analyze` on `PSP_GAME/SYSDIR/BOOT.BIN` must report module `mgp_main`, load base
  `0x08804000`, entry `0x08804128`, and exactly **121 imports**. These are the acceptance numbers
  already established in `docs/05-recompilation.md`.
- **S3** — `psp_recomp <BOOT.BIN> --auto <dir>` must emit a corpus whose units compile with
  `g++ -std=c++20 -O0`.
- **S4** — A profile `mgsa` must load and relocate `BOOT.BIN` into guest memory, register every
  generated unit, and start execution at the module entry point.
- **S5** — Every import without a real implementation must be registered as a logging stub that
  prints `library::name`, `a0`..`a3` and `ra` **on its first call only**, and returns `0` in `v0`.
- **S6** — A run must terminate with an explicit reason: either a printed stop reason from the
  runtime, or exhaustion of a bounded dispatch count. Silence is a failure of the harness, not a
  successful run.
- **S7** — `sceKernelAllocPartitionMemory` and its companions must return real guest addresses,
  because the engine stores and dereferences them. A constant placeholder is a defect.
- **S8** — If execution ever enters the widget cluster (`0x0886B834`, `0x0886B7E0`, `0x0886BB18`,
  `0x0886BC6C`), the harness must say so, because that settles the open question in
  `docs/04-ui-intent.md`.
- **S9** — The whole pipeline must be reproducible by one script in this repository, and the
  profile must be versioned here rather than inside the vendored framework.

Assumption (stated, not authorised): a run that stops before the title screen is acceptable for
this phase. The deliverable is the measurement, not playability.

## Tasks

| ID | Specs | Route | Status |
|---|---|---|---|
| T1 | S1, S9 | inline | done — `scripts/bringup.ps1` + `scripts/extract_iso.py` |
| T2 | S4, S9 | inline | done — `profiles/mgsa/CMakeLists.txt`, `profiles/mgsa/host/main.cpp` |
| T3 | S4, S5, S7 | inline | done — real guest addresses; S5 partially met, a stub and an implementation are both recorded as "bound" |
| T4 | S6, S8 | inline | done — runs stop with an explicit reason; the widget cluster is never entered |
| T5 | S2, S3 | inline | done — `mgp_main`, base `0x08804000`, entry `0x08804128`, 121 imports; corpus compiles |

## Log

- **L1** — user: `dale`, following the proposed phase: "Armar `profiles/mgsa` hasta que arranque y
  crashée fuerte. El primer run se va a parar en el primer import que no implementemos, y ese
  parate es el output útil: establece el orden real de dependencias de boot en vez de adivinarlo."

- **L2** — state at the first push (evidence: `work/boot.log`, `work/boot.err`).
  - The boot chain runs end to end on the Allegrex interpreter: `BOOT.BIN` → `kjfs` → `zlibdec` →
    substituted codecs → `libatrac3plus` → `mpeg` → `sound.prx` → `mgp_stage` loaded from the disc
    image (`disc0:/sce_lbn0x1c700_size0x14e76`, 2312 relocations).
  - The scheduler is real: 12 guest threads, semas and event flags with PSP error codes, modules
    called through a synthetic frame stack over the module-return sentinel.
  - I/O is honest: 50 `sceIoDopen` calls, a 364-entry ISO index, raw sector windows; `stage/init`
    (3,338,158 bytes) is read to completion.
  - Audio is healthy: 4 `sceAudioChReserve` calls and 10,058 successful blocking outputs. The
    earlier "invalid audio channel" reading came from a stale run and is refuted.
  - **Open blocker:** `Main` (uid 3) waits on sema 41 `'g_soundpsp.c'` for callback `0x088093E8`,
    which never runs because the ring-B consumer thread (`sdfile.c`, entry `module+0x60A0`) never
    completes a request. It is parked in a short-read retry loop at `module+0x5CD8` that calls
    `module+0x7764` and compares the result against the requested length.
  - **Lead:** `module+0x774C..0x7788` is a run of eight bare `jr $ra; nop` slots, the on-disk form
    of an import stub, but the module declares its 79 stubs in `.sceStub.text` only
    (`0x13684..0x138BC`), so the loader never registered them and they return a stale `$v0`.
    `work/prx_imports.py` prints the declared tables and every unowned `jr $ra` run.
  - Not met: nothing renders (no GE consumer; 6,103 of 6,364 GE words per frame are NOP), encrypted
    `~PSP` modules are substituted rather than executed, and `--max` does not bound dispatches at
    the value given.
