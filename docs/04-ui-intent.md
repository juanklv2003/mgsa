# 04 — UI intent map

Phase 1.5, second half. What the game *does* with the input it receives, as far as static
analysis can prove it. This is the document that decides how a tap becomes a game action.

## Method

`mgp_main` is a 2005 shipping build with debug vocabulary left in `.rodata`: 1,743 printable
strings, 212 of them UI-related. They are plain ASCII and xref-able, so they serve as anchors
into an otherwise symbol-less binary.

```
python work/strings.py      # string inventory
python work/ui_xrefs.py     # string -> code xrefs -> containing function
python work/ui_buttons.py   # functions touching key[] x dotted with button-bit tests
python work/ui_accessors.py # caller census of the input API
python work/demo_input.py   # the input-override mechanism
```

## First correction: the accessor API is mostly bypassed

There is a clean input API at the end of `.text`, but almost nothing calls it:

| Address | Meaning | Callers |
|---|---|---|
| `0x000FBDE0` | `input_override_push(word)` | 0 |
| `0x000FBE04` | `is_circle_pressed()` — `pressed[0] & 0x2000` | 0 |
| `0x000FBE24` | `is_cross_pressed()` — `pressed[0] & 0x4000` | 0 |
| `0x000FBE44` | `get_buttons(player)` | 1 |
| `0x000FBE7C` | `get_pressed(player)` | 3 |
| `0x000FBEB4` | `get_released(player)` | 0 |

Game code reads the **key array at `0x00094B0C` inline**, with computed offsets. That is why a
"who calls the accessor" search finds nothing useful: there are exactly **16 functions** that
reference the key array, and those are the real input consumers.

| Function | Button bits tested |
|---|---|
| `0x000018AC` | TRIANGLE |
| `0x00001B20` | CROSS ×2, TRIANGLE, L, R |
| `0x00002B70` | DOWN ×3, SELECT ×2, UP ×2, START |
| `0x00002CE8` | DOWN ×2, SELECT ×2, UP ×2, RIGHT ×2, LEFT |
| `0x00004410` | SELECT ×3, START |
| `0x00007F78` | START ×2, SELECT |
| `0x0006778C` | RIGHT, CIRCLE, LEFT, DOWN, UP, SELECT |
| `0x00074C84` | (key builder — not UI) |
| `0x00074EB4` | (input post-process — not UI) |
| `0x0007753C` | SELECT, UP, CIRCLE |
| `0x000AB86C` | CROSS ×2, CIRCLE ×2 |
| `0x000AE9AC` | CROSS, `0x00F0` mask, CIRCLE, SELECT, RIGHT |
| `0x000AFE60` | CROSS ×3 |
| `0x000FBE24` | (the `is_cross_pressed` helper itself) |
| `0x0011775C`, `0x001177F4` | none — these only write the `key+0x1E` flags |

Two facts follow. `CROSS` is the confirming button in gameplay code (`0x000AB86C`,
`0x000AE9AC`, `0x000AFE60`). `SELECT`/`START` belong to a debug/overlay cluster
(`0x00002B70`, `0x00002CE8`, `0x00004410`, `0x00007F78`) that the retail build still contains —
worth a separate look, because debug menus are usually the easiest way to reach game state.

## There is a built-in input override

`0x000839E0` is a switch and `0x000839E4` a button word:

```
0x000FBDB8   *(u32*)0x839E0 = 1        // enable override
0x000FBDC8   *(u32*)0x839E0 = 0        // disable override
0x000FBDD4   return *(u32*)0x839E0     // query
0x000FBDE0   push: copy word to 0x839E4, 0x839E8, 0x839EC
0x000FBDA0   clear all four words
```

When the switch is set, `get_buttons` / `get_pressed` / `get_released` return the word at
`0x839E4` instead of reading `key[]`.

**Caveat, important:** the 16 inline consumers read `key[]` directly and do not go through
those accessors, so this override is a partial hook, not a universal one. It is useful for
scripted input experiments, not as the primary touch path. The primary path stays
`sceCtrlReadBufferPositive`.

## The list/scroll widget

`0x00067834` is a generic dpad-driven stepper, and it is where the 21-frame repeat observed
earlier actually lives. Field roles inferred from usage:

| Offset | Role | Evidence |
|---|---|---|
| `+0x78` | current item pointer | loaded and compared against `+0x40` group root |
| `+0x7C` | current index | used as index into arrays at `*(a1+0x14)` |
| `+0x88` | cached input word | compared with `+0x8C` each frame |
| `+0x8C` | previous input word | stored at the end of the update |
| `+0x90` | repeat counter | incremented, compared against 21 |
| `+0x94` | repeat step | 1 normally, 4 once the repeat engages |

Behavior: on a changed direction it resets the counter and applies step `1`; after 21 frames
of holding it switches to step `4`. Movement is clamped against limits read from the widget
descriptor, so lists stop at their ends.

It is also the *only* stepper of this shape. The constant `21` appears in seven functions
(`0x0006778C`, `0x00069144`, `0x000A0CB0`, `0x000C4858`, `0x000E9F04`, `0x0011E348`), but
only `0x0006778C` carries the field layout above, so the repeat logic is not duplicated per
screen. That matters for the port: a tap layer does not have to reimplement scrolling seven
times.

### The widget object and its global

Reading the constructor chain around the stepper produced the object map:

| Address | Role |
|---|---|
| `0x00067BD0` | object init — zeroes `+0x40`, `+0x44`, and `+0x78`..`+0x94`, returns 1 |
| `0x00067B18` | per-frame update — gated by flag `0x00076C70 & 1`, then calls the tick and the stepper |
| `0x0006778C` | frame body — clears `+0x78`..`+0x90` |
| `0x00067C18` | constructor — allocates **`0x98` = 152 bytes** at `0x00073BA8`, stores the two method pointers `0x00067B18` and `0x00067BB8` |
| `0x00076C6C` | **global pointer to the active widget** (bss, zero-initialised) |
| `0x00067CD4` | reads `0x00076C6C`, walks the doubly-linked list at `+0x40`, inserts a node, returns `1` on insert / `-1` when there is no widget |

So the object is 152 bytes and the field map above fits inside it exactly, and `+0x40` is a
doubly-linked **list head** — the widget's children. That list is the closest thing to a widget
registry found so far.

### A reachability problem worth recording

None of `0x00067B18`, `0x000677E0`, `0x00067834`, `0x00067BD0` or `0x00067C18` has a direct
`jal` caller, a data pointer anywhere in either loadable segment, or an address materialised
in code — with one exception: `0x00067C18` materialises `0x00067B18` and `0x00067BB8` while
allocating an object, and even that constructor has no caller.

Two readings remain, and static analysis alone cannot separate them:

1. the chain is reached from a per-stage PRX overlay through a mechanism not yet mapped, or
2. the whole cluster is development leftover that the retail build never executes.

The second would be consistent with the debug/overlay cluster noted above, and it would mean
option B (writing `+0x7C` directly) has no proven live target. It also means the field map in
this document is a *reconstruction of a data structure*, not proof that it is populated at
runtime. Confirming it needs execution, not more static reading: patch the recompiled binary
to log whether `0x00067834` is entered during a normal list interaction.

This is the component to study for board scrolling and hand browsing, and it is the reason a
tap cannot be treated as an absolute position: the engine models *movement*, not coordinates.

## Command menu is data-driven

At `0x00137640` there is a table of five handler pointers
(`0x000EDEBC`, `0x000EDFA0`, `0x000EE4B8`, `0x000EE500`, `0x000EE54C`), immediately followed by
the labels:

```
0x00137654  "Use"
0x00137658  "Equip"
0x00137660  "Attack"
0x00137668  "Discard"
0x00137670  5 more handler pointers (0x000EFD14 .. 0x000EFDF8)
```

And at `0x00133B80` a second cluster of action captions:

```
"SPEEDY"  "CANNOT USE"  "CANNOT EQUIP"  "CANNOT ATTACK NOW"  "CHARGING NOW..."
"CANNOT SUPPLEMENT"  "LOADING"  "ATTACK"  "SUPPLEMENT"  "REPLACE"  "EQUIP"
"suspend"  "return"  "end"  "punch"  "knock"  "discard"  "system"
```

Two consequences for the port:

1. The action menu is an array, so a native UI can enumerate and render the same commands
   without reverse-engineering a layout.
2. The captions are English literals; French, German, Italian and Spanish equivalents sit in
   the same `.rodata`, which means the touch UI can be localized from the game's own strings
   instead of a separate translation.

## Named anchors for the next pass

Functions found through string xrefs, usable as landmarks:

| Address | Anchor |
|---|---|
| `0x0000C9F4` | turn banner (`"1P TURN"`, `"2P TURN"`, `"ENEMY TURN"`) |
| `0x000BAF88` | AI think state (`"start think state : use card num over --> finish turn"`) |
| `0x000C9A5C` | `DumpHandCard` |
| `0x000CD9FC` | `"draw hands"` |
| `0x000D0224` | book mode |
| `0x000EE794`, `0x000EE954` | equip command handlers |
| `0x000EEB78`, `0x000EEDAC`, `0x000EF0D0`, `0x000EF2A4` | attack command handlers |

A handful of `rodata` strings retain their original names, which give the subsystem a label
without decompiling it:

| String address | Value |
|---|---|
| `0x0012FD94` | `mgp_main` — the boot module name |
| `0x001301A0` | `user_main` |
| `0x001340E4` | `enemy_think_thread` — the AI thread |
| `0x00133D48` | `ene_commander.c` — source filename of the commander |

Only these four survive; the build was otherwise stripped.

The gameplay action cluster is concentrated in `0x000EE000`–`0x000EF400`, which is a
manageable 5 KB window for a focused read.

## What a tap can and cannot be

| Goal | Feasible now | How |
|---|---|---|
| confirm an action | yes | pulse `CROSS` for 2–4 frames |
| move a cursor one step | yes | pulse a dpad direction |
| move a list quickly | yes | hold the direction; the engine repeats at step 4 after 21 frames |
| drag a board camera | likely | push `Lx`/`Ly` past the ±30 dead zone |
| tap an absolute board tile | **no, not yet** | the engine has no absolute cursor; a tap must be translated into a movement sequence |
| jump a list to an absolute index | **unproven** | write the widget's `+0x7C` index; `0x00076C6C` gives the object, but the cluster may be unreachable (see above) |

The bottom two rows are where the remaining work is, and they are no longer the same problem.
Tap-to-tile is a translation layer — coordinate in, dpad sequence out — and it needs the board's
camera and tile geometry, not more engine knowledge. Index injection is blocked on the
reachability question above.

The practical consequence: **option A is the architecture**. Pulsing buttons at
`sceCtrlReadBufferPositive` reaches every screen the engine has, needs no per-screen knowledge,
and is the only path proven to work. Option B stays a possible optimisation for one screen at a
time, and only after execution proves the widget object exists at runtime.

## Open items

- Decompile the 16 key-array consumers and label each one with its screen.
- Identify the debug/overlay cluster (`0x00002B70`, `0x00002CE8`, `0x00004410`, `0x00007F78`)
  and whether retail builds expose it.
- Confirm the role of `CIRCLE` in gameplay screens (`0x000AB86C`, `0x000AE9AC`).
- Settle the widget reachability question by execution: log whether `0x00067834` is entered
  during a normal list interaction in the recompiled build.
- Map the board camera and tile geometry, which is what a tap-to-tile translation layer needs.
