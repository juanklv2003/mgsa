# Yakumo runtime patch (MGSA-specific)

The Allegrex runtime lives in the nested git repo at `work/tools/Yakumo` (a checkout
of `https://github.com/TeamGDB/Yakumo.git`). The whole `work/` directory is gitignored
by this repo, so the MGSA-specific changes to the runtime are **not protected by git**
and would be lost if `work/` is deleted or reset.

`docs/yakumo-mgsa-runtime.patch` captures those changes. It must be applied to the
nested checkout after any fresh clone/reset of `work/tools/Yakumo`.

## What the patch adds

1. **`OpcodeKind::Vi2x`** (decoder.hpp) — the VFPU narrowing conversions
   `vi2uc`/`vi2c`/`vi2us`/`vi2s` (opcode 0x34, VFPU4Jump group 1, operations 28-31).
2. **Decode of `Vi2x`** (decoder.cpp) and of **VFPU1 function 5 = `vcrs`**
   (3-element cross product / `vqmul`), which the stock decoder left as the
   unhandled `vfpu1` opcode. Without these, the guest hits `rt.unsupported()` in
   the AOT code at `0x0887CF9C` / `0x08887978`, the interpreter cannot decode the
   instruction, and the dispatcher spins forever on the same PC.
3. **Execution of `Vi2x`** (interpreter.cpp) — saturating integer narrowing.
4. **The retired-PC histogram** (interpreter.hpp/.cpp) — `set_pc_histogram_enabled`,
   `set_pc_histogram_thread`, `pc_histogram`, `report_pc_histogram`, `kAllThreads`,
   and the `record_pc()` calls in the interpreter loop. The host's `--pchist-range`
   CLI flag (in the main repo) calls these; without them `mgsa_boot` fails to link.

## Why this matters

With the patch applied, the desktop baseline is restored exactly:

```
--- run stopped after 412793 dispatches
--- frames: 516 presented, 258 GE lists queued, guest time 8712 ms
```

The AOT code was generated with a toolchain whose `codegen_main.cpp` did not lower
`vi2x`/`vcrs` into inline C++, so those instructions execute through the interpreter
via `on_unsupported` -> `interpret_guest_site`. Functionally correct, but slower than
a native lowering (~52 s wall vs ~11 s wall before the runtime source was lost).
Restoring the full original runtime (or lowering these opcodes in `codegen_main.cpp`)
would recover the original speed.

## How to apply

```sh
cd work/tools/Yakumo
git apply ../../../docs/yakumo-mgsa-runtime.patch
```

Then build as usual:

```sh
cmake --build work/tools/Yakumo/out/mgsa --target mgsa_boot -j 4
```
