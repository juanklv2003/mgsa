# Stage-load RE — decision journal

Scope: find why the PSP guest boots to an alive frame loop but never loads
`stage/title` content. Desktop host only (`mgsa_boot.exe`); the Android APK is
out of scope until there is real guest content to display.

This is a log of **decisions and falsified hypotheses**, not a plan. Each entry
records the question, the evidence, and the conclusion. Do not re-run a falsified
hypothesis unless new evidence contradicts it.

## Established facts (do not re-derive)

- Guest boots fully: 11 modules, 516 frames, 412793 dispatches, 8712 ms guest time.
- `sceCtrlReadBufferPositive` is polled 258 times (once per presented GE list).
- `sceIoDread`: 398 calls; `zlibdec::0x216D1BF1`: 106 calls; 50 stage dirs scanned at boot.
- `stage/title/` has exactly 3 files: `title.prx` (151473 B), `_zar` (916419 B),
  `e002st00_1.psq` (238564 B). Directory listing from the host is correct.
- At frame 36: `e001system.psq` read in full via ISO LBA 0x1bc30 (792724 B);
  `mount_stage(".../stage/title/data.cnf")` opens LBA 0x1c8c0 size 0xdfbc3
  (fd 318), `preseek` seeks it and exits. **No further file I/O for 480 frames.**
- `title.prx` is never opened, never loaded, never started.
- VRAM stays 0 non-zero words. The guest's own GE list sets
  `ALPHATESTENABLE 0x22000001` before its only `PRIM 0x04060002`, so every
  fragment is discarded. The black screen is a symptom, not a render bug.
- The game self-patches zero-based addresses at module load (guest code at
  `pc=0x088050D0` rewrites `lui r,0` + `lw ...,0x4A5C(r)` to `0x0894/0x27DC`).
  The stage-state pointer at `0x089427DC` holds `0x089D2920`. This is NOT missing.

## Debug method

Full runs cost ~385 s and get killed by server restarts. Use the bounded-trace
technique: instrument `on_pre_dispatch` behind an env var, `runtime.stop()` at
frame ~40. Each iteration is ~53 s. Keep the trace code out of committed source.

## Falsified hypotheses

| # | Hypothesis | Evidence | Verdict |
|---|-----------|----------|---------|
| 1 | `sceImpose::0x36AA6E91` returning 0 fails the mount | `MGSA_IMPOSE_RET` 0..5 all leave engine stalled | FALSIFIED |
| 2 | Missing host fixup for the `0x4A5C` state pointer | Guest patches it itself (run57) | FALSIFIED |
| 3 | Language id selects a bad state | `MGSA_LANGUAGE` 1→2: identical (run59) | FALSIFIED |
| 4 | `mount_stage` return value gates the load | returns 0; forcing 1 changes nothing (run60) | FALSIFIED |
| 5 | Memory stick devctl `mscmhc0:` gates the load | present/absent: 516→520 frames only (run61) | FALSIFIED |
| 6 | Title is a PMF needing substituted `videocodec` | no PMF opened, no `sceMpeg` past init (run58) | FALSIFIED |
| 7 | Savedata dialog status never becomes ready | zero `[savedata]` calls in any log (L9) | FALSIFIED |
| 8 | Stage-load request entry `0x0891BA00` is called by pointer | 0 entries in a whole run, 0 words hold its address (run73/74) | FALSIFIED |
| 9 | `0x089238C8` is a task enqueuer | generic helper, many unrelated callers (run72) | FALSIFIED |
| 10 | `mgp_stage+import stubs at 0xDE38/0xDE60 are state code` | they are `sceImpose`/`GetSystemParamInt` stubs (run77) | FALSIFIED |

## Net state

Every concrete gate hypothesis has been falsified. What remains established is
only: `mgp_stage+0x1B8` (GetSystemParamInt + sceImpose) executes once in frame 36;
the engine never enters the stage state machine; no title content is requested.
The original conclusion (memory #70) stands: the trigger is in BOOT.BIN's main
loop and is never reached, but the exact missing condition is still unknown.

## Next decision

Run ONE definitive trace: log every entry from engine code into interpreted
`mgp_stage` code (PC in `0x09FDF000..0x09FEEC40` with `ra` outside that range)
across the whole run. That yields the complete engine→mgp_stage call sequence
and ends the guesswork about which module functions are actually invoked.

- [x] Run the engine→mgp_stage entry trace (run79). `on_pre_dispatch` does NOT see
      interpreted module code (runtime.cpp:618 only calls the hook for AOT functions);
      the correct hook is `on_missing_function` (runtime.cpp:606), which fires right
      before `run_interpreter_fallback` takes over.
- [x] Result CORRECTS the "only +0x1B8 runs" claim: the engine calls MANY mgp_stage
      methods, all through a single AOT thunk whose return site is `0x08808260`
      (generated_unit_0001.cpp:4853). The call itself is at `L_08808258`:
      it saves a value at `gpr[17]+26628`, sets `jump_target = gpr[3]`, and tail-calls
      the function pointer — a generic vtable-style method dispatcher.
- [x] Offsets observed (run79, frame 36): `0x34` (module_start), `0x0`, `0x2110` (×10),
      `0x286C`, `0x1B8`, `0x70`, `0x90`, `0x1F58` (×many), `0x2090`, `0x20E0`.
      `a0` is always the module data base `0x09FEDEC0`; `a1` looks like a hash/NID
      (`0x00C74F97`, `0x00D42B14`, ...); `a2` is a small index (3,5,6,7,9,10).
      `0x1F58` (a2=5) and `0x20E0` (a2=6) alternate repeatedly = state polling.
- [x] Return values at the thunk (run80): overwhelmingly 0; nonzero queries were
      `0x3FF`, `0xF`, `0x1`, `0xC09262B6`. No "ready to load" value observed.

## Corrected net state

The engine DOES interact heavily with mgp_stage via a vtable-style dispatcher; it is
NOT true that only `+0x1B8` runs. The methods are reached through function pointers
published by `module_start` (`0x34` writes 3 pointers into `*argv[0]`). What is still
unknown: which method index corresponds to "load the title stage" and why its return
never triggers the load. Those hashes/NIDs do not appear as constants in the AOT or
mgp_stage disassembly, so they are likely computed at runtime (string hashes).

- [x] Dump the published function pointers (run82). `module_start` (`0x34`) writes
      into `argv[0]=0x08B500A0`: `[0]=0x09FDF000` (module base), `[1]=0x09FEDEC0`
      (data base), `[2]=0x09FDF000` (module base again). `[8..11]` hold the original
      argv passed by the engine (`0x08B500A0, 0x08945BC0, 0x08804A8C, 0x089C0000`).

## Architecture — now understood

`mgp_stage` exports a **hash-addressed API**, not a plain vtable. `module_start`
publishes `{code_base, data_base, code_base}`. The engine calls everything through a
single AOT thunk (`0x08808258`, returns at `0x08808260`) that tail-calls a function
pointer with `a0 = data_base (0x09FEDEC0)`. Module functions then take `(a0=data_base,
a1=hash, a2=index)` — the engine asks for a method by hash (`a1`, e.g. `0x00C74F97`)
and index (`a2`, e.g. 3,5,6,7,9,10). Observed module offsets in frame 36: `0x34`,
`0x0`, `0x2110`, `0x286C`, `0x1B8`, `0x70`, `0x90`, `0x1F58`, `0x2090`, `0x20E0`.
Methods `0x1F58` (a2=5) and `0x20E0` (a2=6) alternate = state polling.

`0x0` is engine-API init (writes module globals); `0x34` is module_start; the rest are
API methods. The hashes in `a1` do not appear as constants in AOT or the static
disassembly — they are computed at runtime, probably string hashes of method names.

## Next decision (revised again)

The failing piece is now narrow: identify the hash for the "load/enter stage" command
and check whether the engine ever requests it. Two concrete options:
1. Dump the hash→function table inside mgp_stage: at module load, scan guest RAM for a
   table of (hash, handler) pairs and match observed `a1` values to module offsets.
2. Trace ALL `a1` hash values requested across the whole run and look for one that is
   requested exactly once near frame 36 (the stage-load command) whose handler either
   is missing or returns an error.

Option 2 is a single bounded trace (log `a1`/`a2` at each thunk entry, frames 0-45).

- [x] Traced all method hashes (run83/84). Map of `(hash, idx)` requested from
      mgp_stage, all one-shot commands in frame 36:
      - `0x00DD258E idx=0x0B` at module offset `0x12C` = `mount_current_stage`
      - `0x002986E9 idx=0x01` at `0x1B8` = language/impose setter
      - `0x006F7D4D idx=0x07` at `0x70`
      - `0x00DE07C0 idx=0x0C` at `0x90`
      - `0x003A9224 idx=0x03` at `0x2090`
      - `0x00D42B14 idx=0x0A` at `0x286C`
      Repeated polls: `0x00542B2D idx=5` (42×), `0x006B237D idx=6` (38×),
      `0x00C74F97 idx=9` (24×).
      The engine DOES request the mount command, and its return is 0 (success).
      Non-zero returns at the thunk were only query values (`0xC09262B6`, `0x1`,
      `0x3FF`, `0xF`) -- no error.

## Assessment after full hash trace

The engine requests every stage command in frame 36 and they return plausible
values. The blocker is NOT a missing command, NOT an error return, and NOT a
state the methods report. What remains unexamined is the ENGINE side of the
thunk: `0x08808258` (generated_unit_0001.cpp:4840-4853), which builds the method
call and then decides what to do with the result. The decision to actually
load `title.prx` must be in that function or its caller, not in mgp_stage.

The bounded-trace approach has now been applied to every layer (dispatcher,
methods, hashes, returns). Further progress requires reading the AOT logic at
`L_08808258` backwards to its caller and following the branch on the result --
static reading of ~a few hundred instructions, not more dynamic probes.

Host code clean; all temporary traces reverted; build verified.

## Engine-side call chain (static AOT reading, 2026-11-19)

Followed the thunk backwards through the AOT:

- `0x08808248`–`0x08808258` (generated_unit_0001.cpp:4835-4848) is a **null-guarded
  method-call wrapper**: `if (gpr[3]==0) return -1;` else save `v0=1` to
  `gpr[17]+0x6804`, set `ra=0x08808260`, tail-call `gpr[3]` (the method pointer).
  The `gpr[2]` seen at `0x08808260` after the call is the method's real return.
- Its caller is `L_08808244`, fed by `invoke_chained_direct(... 0x08806288)` — so
  `0x08806288` (recomp_unit_0000) **resolves the method by hash**.
- Before that, `L_08808234` calls `0x08879F28` (recomp_unit_0029) which returns the
  method pointer. So `0x08879F28` is where the engine **selects which command to send**.
- `0x08879F28` is itself a wrapper around a chain: `0x08879EF0` and `0x08879D20`.
  The actual decision logic is deep in that wrapper chain (4+ levels).

This is the true location of the missing condition: the engine's command-selection
logic around `0x08879D20`, not mgp_stage. Following a 4-level wrapper chain
instruction-by-instruction is slow and low-yield compared with a targeted dynamic
read of which command the engine chooses at frame 36 — but every method the engine
already requested returned plausibly, so the likely failure is a command it does
NOT send (or a branch after a query result) in that chain.

Status: architecture fully understood, blocker localized to engine command
selection. No further dynamic probes planned until there is a specific hypothesis
about which command/branch is missing.
