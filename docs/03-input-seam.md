# 03 — The input seam

Phase 1.5 deliverable. This is the most valuable finding for a touch-native port: the whole
game reads the controller through **one function call**, and everything a gesture layer needs
to produce is 16 bytes.

## Evidence

`mgp_main` imports exactly two `sceCtrl` NIDs, and static analysis finds **exactly one direct
call site for each, plus zero indirect references** (no `lui/addiu` of the stub address, no
function-pointer table):

| Stub | NID | Function | Call sites |
|---|---|---|---|
| `0x0012FA4C` | `0x1F4011E6` | `sceCtrlSetSamplingMode` | `0x0007488C` |
| `0x0012FA54` | `0x1F803938` | `sceCtrlReadBufferPositive` | `0x00074908` |

There is no `sceCtrlPeekBufferPositive`, no second polling path, no raw GE input, no
`sceKernelGetGPI`. **One function is the entire input surface of the game.**

Stub layout note: library stubs are 8 bytes apart (`stub_addr = stub_table + 8 * index`), not
20. An earlier reading of `.lib.stub` (20-byte records) is the *record* stride, not the stub
stride; mixing them mislabels every NID after the first.

## Call graph

```
0x00075154  frame_input(force)
            ├─ if (force == 0) 0x000749DC  poll_pads()
            └─ 0x00074EB4                  input_post_process()
0x00075138  input_init()  ->  0x00074864
```

`0x000749DC` iterates the two pad slots and calls the one poller per slot:

```
0x000748D8  read_pad(pad *p)          // a1 unused, forced to 0
            if (*(u32*)0x00007500 != p->serial) {
                p->buttons = 0;
                p->rx = p->ry = p->lx = p->ly = 0x80;
                return;
            }
            SceCtrlData d;
            if (sceCtrlReadBufferPositive(&d, 8) != 0) {   // 0x00074908
                p->buttons = p->saved_buttons;             // read failed: keep last
                return;
            }
            p->buttons = (d.Buttons & 0xF0F0)
                       | (d.Buttons & 0x0100 ? 0x0400 : 0)  // L -> internal bit 10
                       | (d.Buttons & 0x0200 ? 0x0800 : 0)  // R -> internal bit 11
                       | (d.Buttons & 0x0001)                // SELECT
                       | (d.Buttons & 0x0008);               // START
            p->rx = p->ry = 0x80;
            p->lx = d.Lx;
            p->ly = d.Ly;
            for (i = 0; i < 12; i++)
                p->btn[i] = (p->buttons >> (i + 4)) & 1 ? 0xFF : 0x00;
```

Frame counter semantics of `0x00007500`: it is a **slot selector**, not a frame counter. It is
written to `0` once at init (`0x00074878`) and never elsewhere in `.text`. With the default
value, slot 0 receives input and slot 1 is forced to neutral every frame.

## `SceCtrlData` is used as the standard structure

The poller reads exactly `TimeStamp` (unused), `Buttons` at `+4`, `Lx` at `+8`, `Ly` at `+9`.
`Rx`/`Ry` are ignored: the engine overwrites its internal copy with `0x80`. So a replacement
only has to produce **buttons plus one analog pair**.

## Pad slot layout — `0x00077504`, 2 entries of `0x40`

The array is `memset` to zero (`0x0007489C`, size `0x80`) and then each `serial` field is set
to its index. Field layout as observed:

| Offset | Type | Meaning | Written at |
|---|---|---|---|
| `+0x00` | u32 | slot serial, compared with `*(u32*)0x7500` | init `0x000748B4` |
| `+0x04` | u32 | state (0 = idle, 1 = ready) | poll `0x00074A40` |
| `+0x08` | u32 | timestamp / `0xF0000` scratch | poll `0x00074A1C` |
| `+0x0C` | u32 | **buttons**, game-internal bit layout | read `0x00074984` |
| `+0x10` | u8 | forced `0x80` | read `0x00074954` |
| `+0x11` | u8 | forced `0x80` | read `0x00074950` |
| `+0x12` | u8 | `Lx` from `SceCtrlData` | read `0x00074958` |
| `+0x13` | u8 | `Ly` from `SceCtrlData` | read `0x0007495C` |
| `+0x14..0x1F` | u8[12] | per-button `0x00`/`0xFF` derived from bits 4..15 | `0x0007498C` loop |
| `+0x20` | u32 | cleared each frame | `0x00074A24` |
| `+0x3D` | u8 | flag written by `0x00075120(slot, value)` | setter |

Consumer view used by the post-processor is `pad + 8`, i.e.
`struct { u32 time; u32 buttons; u8 rx, ry, lx, ly; u8 btn[12]; ... }`.

## Internal button bits

The mask is `0xF0F0` plus the L/R relocation, so the internal word carries:

| Bit | Internal meaning | Source in `SceCtrlData.Buttons` |
|---|---|---|
| `0x0010` | up | `0x0010` |
| `0x0020` | right | `0x0020` |
| `0x0040` | down | `0x0040` |
| `0x0080` | left | `0x0080` |
| `0x0400` | L | `0x0100` |
| `0x0800` | R | `0x0200` |
| `0x1000` | triangle | `0x1000` |
| `0x2000` | circle | `0x2000` |
| `0x4000` | cross | `0x4000` |
| `0x8000` | square | `0x8000` |
| `0x0001` | select | `0x0001` |
| `0x0008` | start | `0x0008` |

The derived byte array indexes bits 4..15, so `btn[4]` and `btn[5]` (bits 8 and 9) are always
zero: the engine's canonical 12-button enum is `up, right, down, left, _, _, L, R, triangle,
circle, cross, square`.

**Consequence:** the HLE must reproduce raw PSP bits. The relocation to `0x0400`/`0x0800` is
done by the game itself, one instruction after the read. Do not "help" by pre-remapping.

## What the touch layer has to do

The replacement is one function, at the HLE boundary for NID `0x1F803938`:

```c
int host_sceCtrlReadBufferPositive(SceCtrlData *pad, int count) {
    *pad = touch_layer_state();   // 16 bytes: timestamp, buttons, lx, ly
    return 0;
}
```

Constraints derived from the game's own code:

1. **One sample per frame is enough.** The game passes `count = 8` but only ever consumes the
   newest state, and it polls once per frame from `0x00075154`.
2. **Press and release are both observable.** `0x00074EB4` is an edge-detect plus auto-repeat
   stage with per-player state, so a button must be held for at least one full poll to register
   as a press, and must return to zero to register a release. A tap that is set and cleared
   between two polls is invisible.
3. **Analog is live.** `Lx`/`Ly` reach the engine unchanged, and the pad's `state` field is
   re-armed each frame, so a continuous drag can drive them like a stick.
4. **A failed read keeps the previous buttons.** Returning non-zero is a valid "no new input"
   path, not an error, and it does not clear state. Return `0` with `buttons = 0` to release.

This is why gesture mapping is cheap: a tap on a card becomes "CROSS pressed for N frames",
a drag on the board becomes "d-pad held in direction D with magnitude", and the engine's own
repeat logic handles rate limiting. No per-screen patching, no state injection.

## Where the seam sits in the port

```
Android touch  ->  gesture recognizer  ->  SceCtrlData{buttons, Lx, Ly}
                                       ->  host_sceCtrlReadBufferPositive  (NID 0x1F803938)
                                       ->  read_pad 0x000748D8
                                       ->  pad[slot] 0x00077504
                                       ->  input_post_process 0x00074EB4
                                       ->  game
```

Only the first two boxes are new code. Everything below the seam is the recompiled original.

## Open items

- Semantics of the `+0x3D` flag written by `0x00075120`.
- The two `0x24`-byte handler states at `0x00094B54`/`0x00094B78` consumed by
  `0x00074EB4` (edge detection and repeat timing constants).
- Which internal button bits the hand/targeting screens poll, i.e. the UI state machine that
  decides what a tap means. That is the next document, and it is game code, not platform code.

## Reproduction

```
python work/disasm.py       # raw decoder around both call sites
python work/disasm2.py      # full read_pad function and its callers
python work/disasm3.py      # init, poll loop, pad-array references
python work/disasm4.py      # corrected stub layout, caller map
python work/disasm5.py      # stub-to-NID resolution, frame entry point
python work/seam_check.py   # uniqueness proof: 1 direct call, 0 indirect refs
```
