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

## Final entry (2026-11-19) — stop dynamic probing

Followed the caller chain as far as dynamic tracing allows (runs 85-87). The
`0x08879F28`/`0x08879EF0` pair I suspected was the command selector is NOT: trace
showed its callers are `ra=0x09FE11xx` (sound.prx, base 0x09FE0000) and its `a0`
arguments are binary arrays (`0x098402A5`, patterns of `0x06 0x18`) — it is a
data-copy helper in the sound system, not stage-load logic. `0x08879844` (command
decoder) is never entered in frame 36.

Every dynamic probe this session corrected the previous interpretation rather than
converging on the gate. Root cause of that: without understanding the game's data
formats (the `scenerio.gcx` script, the `_zar` member layout, the mgp_stage API
hash→name mapping), reading the assembly chains is guesswork and each chain turns
out to belong to a different subsystem. Continuing to trace blind has negative
returns.

**Honest conclusion**: the engine requests all stage commands with plausible
returns; the reason it never proceeds to load `title.prx` remains unidentified.
Further progress requires either (a) decoding the game's script/data formats
first, or (b) a reference emulator trace of the same frame as an oracle (explicitly
out of scope per project constraints). All trace code reverted; host clean;
journal committed up to 26751f0.

## Session 2026-11-20 — static gate RE, no convergence

User asked to pursue the gate via static reading of the engine command-selection
chain. Attempts (all falsified):
- Traced all AOT dispatches in the decision region 0x08877xxx-0x0887Axxx in frame 36
  (run86, 500 entries). Observed sceImpose/GetSystemParamInt calls, devctl returning
  -3/-1, and the engine calling its own internal helpers, but no single decision
  point that skipped the title load.
- Read `0x08879400`: it checks a task group at `0x089B4B00` (direct.log showed task
  groups at 0x089B4B50+). It is task bookkeeping, not the load decision.
- The Ghidra decomp's config flags: `0x0890C428=0x3021`, `0x0890C42C=0x02A02021`
  (bit 0x200000 set, which the decomp's PMF branch tests). Cleared that bit in a
  53s test (run89): baseline unchanged (516 frames). NOT the gate.
- The Ghidra function FUN_000fd10c (mapped 0x0891108C) that sets `+0x50=1` is
  NEVER executed in frame 36 (run88: 0 dispatches in 0x08911000-0x08911600).

Honest status: ~20 probes this session, every concrete lead falsified. The gate
remains unidentified. Both dynamic tracing and data-format decoding are exhausted;
static AOT reading has not converged either. No emulator oracle per constraint.
The Android app is fully working (assets extracted, guest boots, native menu visible
on Pixel 9a), but the GAME itself never loads a stage. Host clean, no experiments
committed.

## Hash-addressed API table (runtime dump, run106, frame 37)

The dispatcher at `0x0887A348` is a binary search over 20 eight-byte entries
`{hash u32, handler u32}` at guest address `0x09FEDEC0` (mgp_stage data base).
This is the authoritative map, obtained by reading guest RAM, not inferred:

| hash | handler | meaning |
|---|---|---|
| 0x00118631 | 0x08809408 | engine |
| 0x002986E9 | 0x09FDF1B8 | mgp_stage+0x1B8 (language/impose) |
| 0x0034CC13 | 0x088083EC | engine |
| 0x003A9224 | 0x09FE1090 | mgp_stage+0x1090 |
| 0x004E557B | 0x088174EC | engine |
| 0x00542B2D | 0x09FE0F58 | mgp_stage+0x0F58 (polled 42x/frame) |
| 0x006B237D | 0x09FE10E0 | mgp_stage+0x10E0 (polled 38x/frame) |
| 0x006F7D4D | 0x09FDF070 | mgp_stage+0x70 |
| 0x00BC0B7D | 0x08847B68 | engine |
| 0x00C74F97 | 0x09FE1110 | mgp_stage+0x1110 (polled 24x/frame) |
| 0x00D42B14 | 0x09FE186C | mgp_stage+0x186C |
| 0x00DD258E | 0x09FDF12C | **mgp_stage+0x12C = mount_current_stage** |
| 0x00DE07C0 | 0x09FDF090 | mgp_stage+0x90 |
| 0x00E5C433 | 0x0880930C | engine |
| 0x00E79927 | 0x08808448 | engine |
| 0x00EA0E2C | 0x0881747C | engine |
| 0x00EFB2D7 | 0x09FE0E9C | mgp_stage+0x0E9C |
| 0x00F02E23 | 0x08863F08 | engine |
| 0x00F7AF2C | 0x09FE05E0 | mgp_stage+0x05E0 |
| 0x00FB834B | 0x0881E970 | engine |

This confirms the mount hash and corrects the earlier offsets: the three methods
the engine keeps polling are `+0x0F58`, `+0x10E0` and `+0x1110`, all inside
mgp_stage (base 0x09FDF000). `+0x20E0` in older notes was a transcription error
for `+0x10E0`. The engine calls the mount exactly once at frame 36 and then
polls those three methods on every subsequent frame; none ever reports "ready".

Note on the disassembly tool: `jal` targets such as `0x75C2C` in mgp_stage are
BOOT.BIN-relative offsets (base 0x08804000), i.e. `0x08879C2C`, not module
offsets. `0x08879F28`/`0x08879EF0` are sound-system data-copy helpers.

## Session 2026-11-20 (second pass) — kjfs mount protocol decoded, more gates falsified

New evidence from runs 97-102:

- `kjfs::mount` (+0x1094) decoded: `wait(mount_sema)`; if `state[+0x78] != 0`
  skip everything; else `state=1`, `strncpy(name, +0x7C, 128)`,
  `sceKernelStartThread(preseek_tid)`, `signal`. It never parses the archive.
- `kjfs::preseek` (+0x1154) decoded: `open(name)` via internal `0x634`,
  `sceIoLseek(fd,0,0)`, `wait(mount_sema)`, `state=2`, exit.
- `kjfs::open` (+0x634) decoded: it **only parses the archive when `state[+0x78] == 2`**,
  then resets the state to 0 and resolves the virtual path. Therefore the required
  transition sequence is `mount(name) -> open(virtual_path)`. In the logs the engine
  calls `mount("stage/title/data.cnf")` once at frame 36 but never issues the
  follow-up `open`; kjfs stays pinned at `stage_state=2` for the remaining 480 frames.
  The only frame-36 `open` is the sound thread opening `e001system.psq`.
- FALSIFIED: forcing `stage_state 2 -> 0` at frame 38 (`MGSA_CONSUME_MOUNT`, run98)
  changes nothing (74 frames, no title files). The engine does not poll that word.
- FALSIFIED: `sceIoDevctl("ms0:", 0x02425818)` is `SCE_PR_GETDEV` (PSPSDK
  `pspiofilemgr_devctl.h`): the 4-byte input is a pointer to a 20-byte `SceDevInf`.
  The HLE previously returned success without filling it — a manufactured result.
  Now filled correctly plus `mscmhc0:` inserted=1 (run100); still no title load.
  Keep the fix because it is correct HLE behaviour, not because it is the gate.
- FALSIFIED: thread exit — `user_main` calls `sceKernelExitDeleteThread` at frame 0
  by design after creating `Main`; not a premature death.
- FALSIFIED: guest clock — `sceKernelGetSystemTimeWide` uses the advancing virtual time.
- Engine hash trace (`MGSA_ENGINE_TRACE`, run102) at frame 36: the dispatcher at
  `0x0887A348` calls hash `0x00DD258E` exactly once (the mount) and then polls
  `0x00542B2D` (42x), `0x006B237D` (38x) and `0x00C74F97` (24x) repeatedly.
  `mgp_stage+0x20E0` (one of the polled handlers) always returns 0. The repeated
  `0x08879EF0`/`0x08879F28` entries are sound-module data-copy helpers, already
  falsified in an earlier session.
- `sceCtrlReadBufferPositive` is called 258 times and a START press reaches the guest
  (run92 `[press] frame 62 buttons=0x00000008`), with a byte-identical run result.

Net: the engine requests the mount, receives success, then keeps polling three
methods whose answers never indicate "stage ready". The missing transition is on
the engine side of that poll loop, not in kjfs I/O and not in input delivery.

## Session 2026-11-21 (script bytecode decoding) — FIRST REAL FORMAT DATA

Instrumented the AOT decoder entry `0x08879844` by patching the gitignored
`generated_unit_0029.cpp` with a C hook (`mgsa_decoder_hook`, run127-129). This gave the
first actual stage-script bytes the engine reads.

**`0x08879844` is a byte-stream token decoder, not a generic copy helper.** Given
`(src, out1, out2)`, it reads a token from `src`, writes its value to `out1`, and advances.
Return sites show it is used by many callers (`0x08879D34` 57x, `0x0887BD94` 4x, others).
The engine's decision loop at `0x0887BD94` compares the returned token to 7 and 14.

**Title script bytes at guest `0x09842C6E`** (1024-byte dump in run129):
- `0x73` = string-literal token: `73 07 06 'title' 00 00` and `73 ... 'stdch.h_end' 00`.
- `0x6d`, `0x6a`, `0x69`, `0x6c` = references into the string table, e.g.
  `6d c9 2b 8 04 06 ...`, `69 c9 2b 8 04 06 ...`.
- Short control tokens `37 12 10 00`, `37 14 11 00`, `73 04 00 00`, `73 0b 00 00`.
- The token `title` sits at script offset `0xE2`.

**`MGSA_SCRIPT_FLOW` (run130)** traced the pointer passed to `process_script(0x0887BD7C)`:
29 blocks are processed in frame 36, all returning via `0x0887A1B0`. The engine reaches
`0x09842D06` (offset `0x98` of the title script) but **never** the `title` token block at
`0x09842D50` (offset `0xE2`). This is the closest view yet of where the script walk stops.

Next: determine why the walk stops at `0x09842D06` — decode the `0x6d` control token that
precedes `title` and find what condition advances the script pointer past it. The AOT patch
lives only in gitignored generated code; main.cpp's hook is env-gated (`MGSA_DECODER_TRACE`).

**Follow-up (run132):** instrumented the decoder hook to print any `0x6D` token it sees.
It saw NONE. The engine's `process_script` is entered at the *sub-block* offsets (`0x0`,
`0x13`, `0x98`), never at the `0x6D` bytes themselves. So the `0x6D` tokens are consumed by a
different function before `process_script` is called, and this decoder hook cannot observe
the branch that selects sound (`stdch.h_end`, visited) versus `title` (skipped). Finding that
branch requires tracing the caller that extracts sub-block pointers — beyond what this hook
can see. AOT instrumentation restored; base run unchanged.

**Event system traced (run134-139)** — this CORRECTS the "title block not visited" theory:
- Event handler table at guest `0x0893DF58`, 6 entries:
  `0x37C884->0x088080D8`, `0x82BC9->0x08808224` (title), `0x6592A7->0x088081D8`,
  `0x6BB005->0x08808288`, `0x3BD490->0x08816494`, `0x3822C7->0x088082D0`.
- The title event (`0x82BC9`) IS handled: `0x08808224` runs in frame 36 (run136), dispatching
  15 mgp_stage hashes (`0xC74F97` 11x, `0x542B2D` 10x, `0x6B237D` 7x, plus one-shots).
- The `0x6D` token payloads ARE the event ids: `3B B2 3A 0E` -> `0x3AB23B` (stdch.h_end),
  `C9 2B 08` -> `0x82BC9` (title). Both are looked up.
- **Decisive (run138):** the kjfs mount stack for `stage/title/data.cnf` contains
  `0x09842D4B` — the exact address of the second `0x6D` "title" token — and `0x00DD258E`
  (mount_current_stage). So the FULL chain works: title token -> mount_current_stage ->
  kjfs::mount. The mount is NOT skipped.
- mgp_stage data_base = `0x0A075EC0` (hash table location). `*(db+0x40)=0x00BC0B7D`
  (active event hash), `*(db+0x44)=0x08847B68` (its handler).
- **Root cause restated precisely:** mount_current_stage calls kjfs::mount and returns 0
  (success) synchronously. kjfs::mount only sets `stage_state=2` and starts `preseek`, which
  opens+lseeks the `_zar` but never reads it. Parsing the archive requires a follow-up
  `kjfs::open(virtual_path)` that the engine never issues. The kjfs root stays at the init
  directory (`count=5`), and title.prx / e002st00_1.psq are never opened. All attempts to
  synthesise that open from the host (synthetic ctx, interrupt shim, guest thread) failed on
  module-frame semantics and were reverted.

This is the deepest localisation reached. The remaining work is to make the guest issue the
follow-up open with a proper module-call frame, or to find why the engine's normal post-mount
path omits it.

**Post-mount flow traced (run141, MGSA_POST_MOUNT):** armed on the mount's thunk return, the
engine's next 40 dispatched PCs are captured. It CONTINUES processing script events after the
mount: handler `0x08808224` processes block `0x09842D60`, extracts hash `0xF02E23`, dispatches it
(handler `0x08863F08` returns 1), then processes more events with hashes `0xEFB2D7`, `0x8D775B`.
The engine does NOT stop at the mount — it keeps running events. So the mount is not where the
flow dies; the engine simply never reaches a kjfs `open()` after it. The `0x08879C2C` reads that
produce values like `0xE09262C1` are ordinary memory loads, not PSP error codes (corrected an
earlier misreading).

The `_zar` files of init and title are structurally identical (6-entry TOC, same member names),
so the archive format is not the blocker. kjfs::preseek opens+lseeks the title `_zar` (fd=318)
but never reads it; the engine never issues the follow-up virtual `open()`. Every attempt to
synthesise that open from the host failed on module-frame semantics and was reverted.

## Session 2026-11-21 — ROOT CAUSE FOUND AND FIXED: broken R_MIPS_26 in kjfs

**THE GATE IS OPEN.** The stage-load blocker was a single malformed relocation in kjfs.prx.

kjfs boot-registration (`0xC78E5271`) builds its virtual directory tree through a relocated
`jal` at module offset `+0x444`. The PRX word there is `0x0C00000D` (`jal 0x34`) with an
`R_MIPS_26` relocation. The correct relocated word is `0x0E71840D` (target `base+0x34`, the
directory-builder callback). The loader instead left `0x0E718400` — the addend `0x0D` was lost,
so the `jal` jumped to `base+0x0` (the ELF header, not executable code). That callback failure
left the directory root empty (`count=0`), so stage archives were opened (fd=318) but never
parsed, and no virtual file could be resolved.

**Fix (main.cpp, advance_vblank, frame 2):** if `*(kjfs.base+0x444) == 0x0E718400`, write
`0x0E71840D`. Applying it during `load_and_relocate` is TOO EARLY ($gp not live yet) and
crashes at `+0x414`; frame 2 works.

**Verified (run144, run146):** with the fix, frame 36 opens ALL title virtual files:
`localfont.fcx`, `cache.qar`, `cache.dar`, `scenerio.gcx`, `title.rlc`, and crucially
`title.prx`. `fd=318 reads=2 bytes=916419 max_pos=916419 complete` (was `reads=0`). The
`_zar` is finally read and parsed.

**New state (next layer):** after title.prx is opened, a new crash appears in engine code
`0x08879400` with `a1=0x00010000` used as a pointer (frame ~112). EDRAM is still zero. This is
a DIFFERENT problem — loading/executing title.prx — not the mount gate. The mount gate is fixed.

`MGSA_SLOT_TRACE` (run147) falsified the "negative slot sentinel" theory: the global count at
`0x089B7B80` oscillates 0/1 normally through frame 112. The crash follows a zlib inflate
(`0x461C7724`, frame 110, dest `0x08B44E10`); the engine then reads a freshly-decompressed
buffer and some field resolves to `0x10000`, producing an out-of-RAM access. That is the next
layer: parsing the newly-loaded title data with an incorrect HLE-provided value.

## Session 2026-11-21 (forced-open attempt) — kjfs open synthesis, FALSIFIED

Hypothesis: the engine issues `kjfs::mount("stage/title/data.cnf")` at frame 36 but never the
follow-up `kjfs::open()` that parses the `_zar` and publishes its virtual directory. Attempts to
perform that open with the guest's own code:

1. `runtime.invoke_import("kjfs", 0x93E9297F, ctx)` with a synthetic context — returns fd=0 because
   the forwarder sets `ctx.pc` but the dispatcher runs the real thread's context, not ours.
2. Queue a one-instruction MIPS shim (`addiu a1,1; jr kjfs::open`) as an `IntrHandler` — the shim
   DID execute guest code (confirmed by a real `[kjfs] open ->` line) but `kjfs::open` wrote to
   Main's stack and crashed at 0x0000003C, because an interrupt frame has no private stack.
3. Create a real guest thread with its own stack, `$gp=kjfs.gp`, entry=shim, `ra=kThreadExitStub`,
   path passed as start argument — still crashes at 0x0000002C: a module export must return through
   `kModuleReturnSentinel`, not the thread exit stub; entering it without a `FrameKind::Export`
   frame leaves the frame stack inconsistent.

Conclusion: the mechanism to synthesise a guest call exists but needs a full module-call frame,
not just a stack. This was not pursued further because it risks corrupting guest state. The real
engine still never issues the follow-up open, and the stage gate remains open. Reverted all
force-open code; base run restored (92 frames, no crash).

## Session 2026-11-21 (continued) — hash table re-based, poll return values

The hash table was relocated by the module rebase, but searching guest RAM by content
(`MGSA_FIND_TABLE`: two adjacent known hashes `0x00542B2D`/`0x006B237D`, 8 bytes apart)
found it at `0x0A075EC0` (run117). Every handler is exactly the old value plus
`0x0A067000 - 0x09FDF000`, so the rebase is linear and all earlier disassembly remains
valid: mount `0x00DD258E -> 0x0A06712C`, polls `0x00542B2D -> 0x0A068F58`,
`0x006B237D -> 0x0A0690E0`, `0x00C74F97 -> 0x0A069110`.

`MGSA_RET_TRACE` (run118) captured the return values at the engine thunk's return site
`0x08808260`: in frame 36 all three polled methods return `v0 = 0`. Method `0x006B237D`
also leaves `v1 = 0x089E0000`, but that is a leftover from a 64-bit helper return and the
engine only consumes `v0`. So no polled method ever signals "ready" — consistent with the
stage never loading.

Falsified this continuation: `0x08879844` is not the scenerio command decoder called from
the decision loop; no entries to it occur in frames 34-44. Earlier AOT reading of
`0x0887BD94` as a script return site was wrong — it is the return from a module call.
The engine does drain *something* in that loop, but its selector has not been identified.

## Session 2026-11-21 — encrypted system modules decrypted from the user's ISO

The user supplied the SHA-1 of their retail dump:
`2d2e5604297ac922f4a117952315ea91348af174` for
`Metal Gear Acid (Europe) (En,Fr,De,Es,It).iso`. The local ISO matches byte-for-byte
(324,534,272 bytes), confirming the exact release.

Nine `~PSP` modules in `USRDIR/modules/` (audiocodec, ifhandle, memab, mpegbase,
pspnet_adhoc_auth, rinit, sc_sascore, usbpspcm, videocodec) were decrypted **from that
ISO** with the public KIRK global keys using `pspdecrypt` (github.com/John-K/pspdecrypt,
cloned to gitignored `work/pspdecrypt`, built with the local gcc). Each decrypted stream
was gunzipped where needed and validated as ELF32 `ET_SCE_PPURELEXEC` (0xFFA0), MIPS (0x08).
Installed over `extract/` with the encrypted originals backed up to
`work/encrypted_backup/` (all paths gitignored — game data never enters Git).

**Result (run110):** all nine load as real guest code with exports and relocations;
`sc_sascore`/codec/MPEG imports are now executed, not stubbed. `mgp_stage` also loads
as real code again. To fit them, the host now backs 64 MiB of guest RAM and moves the
synthetic module-return sentinel to `0x0A7FF000` (it is only a PC value compared by the
host, never guest memory), extending the arena to `0x0A7EF000`. All earlier hard-coded
`0x09FFF000`/`0x0A000000` comparisons were replaced with the named constants.

**The stage gate is unchanged:** `fd=318 reads=0` — the title archive is still never read.
Decrypting the modules fixed the audio/video layer, not the load gate, as expected.

**Important consequence:** `mgp_stage` now loads at base `0x0A067000` instead of
`0x09FDF000`. Every module-relative offset and the hash table address in the sections above
are stale until re-derived against the new base.

## Session 2026-11-20 (earlier pass) — devctl branch identified, later FALSIFIED as the gate

The heading below said CONFIRMED at the time; the second pass above falsified it. Kept as the
record of how the lead was found.

Two earlier readings were corrected with measured evidence:

1. The engine does NOT stall after the title mount. run89/92: 516 frames,
   412793 dispatches, 258 `sceCtrlReadBufferPositive` calls and 2816 draws.
   START presses produce a byte-identical run. `Main` spins in the frame limiter
   at 0x0887BF38 with no blocked thread.
2. Every draw is a full-screen sprite (0,0)-(480,272), vtype 0x0080011C,
   pos=2 (s16), stride=12, color=0x00000000, texturing off (run94,
   `MGSA_SPRITE_DBG`). EDRAM staying at 0/524288 words is therefore CORRECT:
   the guest clears to black and draws nothing else. The rasterizer is not the gate.

**Confirmed gate (run95):** `mgp_stage+0x197C` calls
`sceIoDevctl("mscmhc0:", 0x02025806, out, 4)`. The guest then does
`xori v0,v0,1; sltiu v1,v0,1; beql v1,zero,0x1BFC` at +0x1990..+0x1998:
it only enters the stage-load path when `out[0] == 1`. The HLE handler wrote
`0` ("no media"), so every stage transition after boot silently took the
no-memory-stick branch. Boot's own stage (`init`) is auto-mounted by kjfs and
never goes through this call, which is why init loaded and title did not.
Changed the answer to `store32(out_ptr, 1u)`. With that single change the guest
takes the load branch and a NEW devctl appears at `mgp_stage+0x1A00`:
`0x02425818` on `"ms0:"` with a 4-byte input (never reached before).

run96 (full run, --max 600000) is in progress to determine whether the title
archive is then read and title.prx loaded. The "SYSTEM MENU" seen on the Pixel
is `host/ui.cpp`, a host-owned overlay, not game output.
