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
...) appears around offset `0x5000`. Likely the script's opcode/command pointer
table, but the address base is not `0x08000000` (canonicalization puts it
out of range), so the base is still unknown.

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
