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

## Key layer — the API the game actually reads

The pad array is not read by game logic. It is consumed by a per-player "key" state that adds
edge detection, and that is what the UI polls.

`0x00074C84  key_update(key *k, pad *p)`:

```
old = k->buttons;                       // 0x00074C9C
k->buttons = p->buttons;                // p+0x0C
k->axes    = p->axes;                   // p+0x10..0x13, packed
k->btn[0..3]  = p->btn[0..3];
k->btn[4..7]  = p->btn[4..7];
k->btn[8..11] = p->btn[8..11];
0x00074A9C(k, p);                       // axis shaping
new = k->buttons;
k->pressed  = new & ~old;               // 0x00074CE0
k->released = old & ~new;               // 0x00074CE4
```

Key array: `0x00094B0C`, **2 entries of `0x24`**, updated from `0x00074EB4` for both slots.

| Offset | Type | Meaning |
|---|---|---|
| `+0x00` | u32 | buttons (game-internal bits, see above) |
| `+0x04` | u32 | packed axes `rx \| ry<<8 \| lx<<16 \| ly<<24` |
| `+0x08` | u32 | `btn[0..3]` — up, right, down, left |
| `+0x0C` | u32 | `btn[4..7]` — `_`, `_`, L, R |
| `+0x10` | u32 | `btn[8..11]` — triangle, circle, cross, square |
| `+0x14` | u32 | **pressed** this frame (`new & ~old`) |
| `+0x18` | u32 | **released** this frame (`old & ~new`) |
| `+0x1C` | u32 | copied from `pad+0x20` |
| `+0x1E` | u16 | per-player flags, gated by game code |

Public accessors for the edge words:

| Address | Returns |
|---|---|
| `0x000FBEA4` | `key[i] + 0x14` — pressed |
| `0x000FBEC4` | `key[i] + 0x18` — released |

Both fall back to a global at `0x000839E4` when the flag byte at `0x000839E0` is zero, so a
host that never fills the key array still gets a defined value.

Game code flips input-enable bits in `key[i]+0x1E` at `0x0000191C`, `0x00001C4C`,
`0x00002BCC`, `0x00002D1C`, `0x0000444C`, `0x00117798`, `0x0011786C` — that is how screens
lock and unlock input. A touch layer must not fight it.

A second device registry sits at `0x00077544` (stride `0x28`) with attach/detach setters at
`0x00074D00` (flag `0x10`), `0x00074D88` (flag `0x08`), `0x00074E10` (flag `0x1000`, copies a
six-word descriptor) and `0x00074D44` / `0x00074DCC` for detach.

## The analog stick is used

This is the difference between "touch replaces buttons" and "touch replaces buttons and a
cursor". `0x00008060` reads it directly:

```
lbu a1, 6(key0)          // Lx
lbu v0, 7(key0)          // Ly
x = a1 - 128
y = v0 - 128
if (x < -30) x = a1 - 98        // dead zone, ±30
else if (x < 31) x = 0
...same shaping for y...
```

So the game treats the stick as a signed offset from centre with a **±30 dead zone**, and stores
the shaped value for the cursor/animation code.

Auto-repeat also lives here: `0x000677E0` watches `key[0].buttons`, and if the same value holds
for **21 frames** it switches to a repeat state. The rate limit is therefore the engine's, not
the touch layer's.

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

Because the analog stick is live and the engine owns repeat timing, the touch layer can be
built without any per-screen patching:

| Gesture | Injection |
|---|---|
| tap a card / tile | set the confirming button (usually `CROSS`) for 2–4 frames, release |
| tap a menu entry | same, with `CROSS` |
| drag the board | hold a direction, or push `Lx`/`Ly` past the ±30 dead zone |
| cancel | `CROSS` + `TRIANGLE`, whatever the screen expects |
| card detail / rotate | `SQUARE` / `L` / `R` |

The only piece that still needs game-specific work is a **screen-aware mapping**: a tap on a
board tile has to become the movement sequence that walks the cursor there, because the engine
only understands direction, not absolute position. That is the state machine document.


## Open items

- Semantics of the `+0x3C` and `+0x3D` flags written by `0x00075108` / `0x00075120`.
- Which flag bit in `key[i]+0x1E` maps to which screen lock (bits `0x01`, `0x02`, `0x04`,
  `0x08`, `0x10`, `0x1000` are all written by game code).
- Which internal button bits each UI screen polls, i.e. the state machine that decides what a
  tap means. That is game logic, not platform code, and it is the next document.

## Reproduction

```
python work/disasm.py       # raw decoder around both call sites
python work/disasm2.py      # full read_pad function and its callers
python work/disasm3.py      # init, poll loop, pad-array references
python work/disasm4.py      # corrected stub layout, caller map
python work/disasm5.py      # stub-to-NID resolution, frame entry point
python work/seam_check.py   # uniqueness proof: 1 direct call, 0 indirect refs
```
