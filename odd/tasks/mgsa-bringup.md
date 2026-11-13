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

- **L3** — the sound-init deadlock is gone. Root cause was a host ABI bug, not the guest.
  - The L2 lead about eight unregistered `jr $ra` slots is **refuted**: the loader registers all 79
    imports with stub offsets spanning `0x7744..0x138B4`, so those eight slots are covered. They are
    the `kjfs` imports `0xC78E5271` mount, `0x93E9297F` open, `0x1ECF095D`, `0x1B969771`,
    `0x432F631B` read, `0x2B9B31B4` close, `0x813E06AD`, `0xCD04902E`.
  - `sound.prx` `module+0x5CAC` is correct guest code: `seek(fd,0,SEEK_CUR)` saves the position,
    `read(fd,buf,size)`, and on a short read it rewinds with `seek(fd,saved,SEEK_SET)` and retries.
    It looped forever because the rewind target was wrong, not because it was stuck.
  - `sceIoLseek(fd, SceOff offset, int whence)` puts the 64-bit offset on an even register pair:
    `a2:a3` (low in `a2`), `whence` in `t0`, and `a1` is padding. Two independent proofs from the
    image: `kjfs+0xC54` rebuilds `a0/a2/a3/t0` and never touches `a1`; and its caller at
    `sound+0x5C94` sets only `a2`/`a3`/`t0`, leaving `a1` as stale garbage on purpose.
  - The binding decoded `(a1<<32)|a2` and `whence` from `a3`. Every seek therefore got a garbage
    offset, and the clamp to `file->size` turned that garbage into a plausible `792724`, so the
    guest faithfully rewound to EOF and every read returned 0. The clamp is removed: a seek past
    EOF is legal and only makes the next read return zero.
  - Evidence after the fix: `0x432F631B` (read) returns `0x000C1894` instead of 0, the completion
    callback `0x53E8` fires, `Main` leaves sema 41 and keeps calling `sound::0x531B15D1`, and the
    run ends on the dispatch limit instead of a deadlock: 1,306,820 dispatches, 2,068 frames,
    1,034 GE lists queued, guest time 34,649 ms.
  - The game now opens the assets it needs: `stage/init` (3,338,158 bytes), `mgp_stage.prx`, and
    the two streams at `sce_lbn0x1bc30` (792,724 bytes) and `sce_lbn0x1c8c0` (916,419 bytes).
  - Still open: the GE lists are empty. List #1 (`0x0893CA60`, 220 words) carries real commands;
    the lists enqueued from `ra=0x08923164` (`0x499E1000`, `0x489B8880`) are 2,048 zero words each.
    The uncached-alias theory is refuted: `GuestMemory::canonical` (`& 0x1FFFFFFF`) already maps
    both aliases to the same physical window.

- **L4** — the "empty display lists" reading in L2/L3 was a measurement bug, and the game is drawing.
  - The GE encodes one word per command: the top byte names it and the low 24 bits carry its data.
    There is no parameter-count field. `BASE` turns data bits 16..19 into address bits 24..27, and
    `ORIGIN` makes later relative addresses count from itself. That rule comes from Yakumo's
    `profiles/mhp3rd/host/gpu/ge_state.cpp`, not from memory.
  - The earlier scan reported 2,048 zero words because it read a double-buffered list the first
    time it was queued, before the guest had filled it. Re-scanning the same address on later
    enqueues shows content.
  - `walk_ge_list` now follows the list the way the hardware does (CALL/RET/JUMP/END/FINISH, with
    BASE and ORIGIN applied) and runs on every enqueue. Over the run: 1,031 lists, 127,940 commands,
    **1,030 draws, 2,060 vertices, richest list 1 draw / 2 vertices, 0 unfinished, 0 left RAM**.
  - So every queued list draws exactly one primitive of two vertices. The scissor pair in the
    sublist at `0x09B21000` decodes to `(0,0)-(479,271)`: one full-screen sprite per list. That is
    a fade or a loading screen, not a scene, and it is what a boot that has not reached gameplay
    looks like from the GE's side.
  - `0x0AB21000` is a `CALL` whose data is only the low 24 bits; it resolves through BASE/ORIGIN to
    `0x09B21000`, which is inside RAM and holds a real sublist. Reading it as an absolute address
    is what made it look like the list left guest memory.
  - Not a defect, but worth naming: the same 124-command, one-sprite list repeats every two frames,
    so the guest is spending its frame budget on state setup for a single quad.
