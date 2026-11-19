# 08 — Stage data formats (title stage)

Work in progress: decoding the game's data formats so RE of the stage-load gate
has meaning. This file records what is established; `work/*.py` are the probes.

## `_zar` stage pack — structure (established)

`stage/<id>/_zar` = `u32 decompressed_size` + zlib stream. Inflating with
`zlib.decompress(data[4:])` gives the FSD-like image (~2.96 MB for title).

The inflated image starts with:
```
u32  count (6 for title)
...  TOC entries, each: NUL-terminated ASCII name, padded, then metadata
```

TOC member names for `title` (from `data.cnf`, which lists them):
`data.cnf`, `localfont.fcx`, `cache.qar`, `@cache.qar`, `cache.dar`,
`scenerio.gcx`, `title.rlc`.

A **file directory** lives deeper in the image (e.g. `scenerio.gcx` entry at
offset `0x25e6be`). Each entry is:
```
NUL-terminated name
u32 size          (e.g. scenerio.gcx = 0x71e5a = 466,522 B)
u32 crc32         (e.g. scenerio.gcx = 0x41ec68d2)
...               (further fields / page-offset tables not yet decoded)
```
Padding between name and fields is 4 bytes (`name\0\0\0` for len%4==...).

A table of `u32` pointers with the high bit set (`0x8005a0ed`, `0x8005a0fe`,
...) appears around offset `0x5000`. **RESOLVED**: the base is **`0x80000000`**.
The table from `0x4000` to `0x6de4` holds 2,937 pointers `0x80000000 + offset`
into the string block at `0x5885f..0x61149` — it is a string-index table, not
opcodes. Resolved all 2,937 `(index, ptr, offset, text)` pairs to
`work/gcx_string_table.txt`. Consecutive pointers often point into overlapping
substrings (space optimization). A second table at `0x5000` indexes German
item descriptions the same way (`0x8005a0ed -> 0x5a0ed`).

Text contains format control tokens: `0x8c16`, `0x8305`, `0x1f0b` embedded in
the strings (number/color/parameter markers for the engine's text renderer).

So `scenerio.gcx`'s 0x71e5a bytes are mostly the 5-language string database;
the per-stage "logic" is likely these string indices plus a small command
sequence not yet isolated. This is enough for a native UI: the text and its
index mapping are directly usable.

### Exhaustion check (2026-11-19)

Tried to isolate a command/opcode sequence from the data side:
- The directory entry gives `scenerio.gcx size=0x71e5a, crc=0x41ec68d2` but
  that exact payload is NOT a contiguous slice of the inflated image (CRC scan
  over all aligned starts found nothing) and NOT any single `78 9c` stream
  inside it. The inflated image is a container (`cache.dar`-like) whose members
  are addressed by the page-offset arrays, not by simple concatenation.
- The 2,937-entry pointer table at `0x4000` is a **string index**, and its tail
  (indices 2800+) is just item/card names in the 5 languages with shared
  substrings — it is a dictionary, not an ordered command stream.

Conclusion for path (a): the data side yields the full UI text and its index
mapping (done), but NOT the command sequence that triggers a stage load — that
sequence lives in engine code and references these indices. The data-format
path has reached its useful limit for the stage-load gate. Further progress on
the gate requires reading the engine command-selection code (path (b) in the
journal is out of scope per the no-emulator-oracle constraint).

## UI strings — EXTRACTED (deliverable)

The game stores UI/card text **uncompressed** inside the `_zar`. Extracted
11,591 clean strings (5 languages: En/Fr/De/Es/It) to `work/title_ui_strings.txt`.
They start around inflated offset `0x6de4`, preceded by a pointer table
(`u32` values like `0x80060a32`, high bit set) at `0x6d00`.

Sample (English command help):
```
End the turn.
(4 COST if no cards used.)
Attack the enemy you are facing with hand-to-hand combat. (COST: 4)
Knock on the wall. (COST: 0)
Select 2 cards from your hand to discard, then draw 2 new cards from the deck.
Display the System Menu.
Abort the current mission.
```
Card data is also present in all 5 languages, e.g.
`Gun del Sol|USTIONE|Antiarmatura|Anti-immortale`,
`Uniform-R|Schaden von vorne -50|Schaden von hinten -80`.

This text is directly usable by a native touch UI without rendering through the
engine's font path.

## Not yet decoded

- `scenerio.gcx` payload location and opcode format (the per-stage script that
  would tell us what command the engine expects to trigger a stage load).
- `cache.qar` / `cache.dar` nesting.
- `.rlc` resource layout.

## Probes (work/)

`zar.py`, `zar_hex.py`, `zar_tree.py`, `zar_len.py`, `zar_parse3.py`,
`zar_scan.py`, `zar_streams.py`, `zar_entry.py`, `zar_dirents.py`,
`zar_5xxx.py`, `zar_script_cmds.py`, `extract_strings.py`,
`extract_ui_text.py`.
