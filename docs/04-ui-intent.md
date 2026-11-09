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
| jump a list to an absolute index | maybe | write the widget's `+0x7C` index, but the widget object address is dynamic per screen |

The last two rows are the real remaining work, and both need the same thing: knowing which
widget object owns the focused element at any given moment.

## Open items

- Decompile the 16 key-array consumers and label each one with its screen.
- Identify the debug/overlay cluster (`0x00002B70`, `0x00002CE8`, `0x00004410`, `0x00007F78`)
  and whether retail builds expose it.
- Confirm the role of `CIRCLE` in gameplay screens (`0x000AB86C`, `0x000AE9AC`).
- Locate the widget registry so a tap can resolve "which list is on screen".
