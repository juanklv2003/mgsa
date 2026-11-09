# 02 — Container formats

Phase 1 deliverable. What the engine actually stores on disc, and what a runtime has to
understand.

## Summary

| Container | Where | Status | Do we need it for the port? |
|---|---|---|---|
| `_zar` stage pack | `stage/<id>/_zar` | **decoded** (payload), TOC partially | yes — every stage loads from it |
| `.psq` audio | `psq/`, `stage/<id>/*.psq` | header mapped, payload opaque | **no** — the game's own `sound.prx` decodes it |
| `.a3p` audio | `a3p/` | **decoded** — RIFF/WAVE ATRAC3plus | no — same |
| `.pmf` video | `mpg/` | **decoded** — PSMF / MPEG-PS | no — `sceMpeg` handles it |
| `.prx` modules | `modules/`, `stage/<id>/` | **decoded** — plain ELF32 | yes |
| `.fcx` `.gcx` `.rlc` `.qar` `.dar` `.cnf` | inside `_zar` | names known, formats pending | yes |

## `_zar` — the stage pack

Layout: `u32` header, then a **zlib** stream (`78 9C`) from offset 4. The `u32` holds the
decompressed size for most files (e.g. `title`: `0x002D39F9` → 2,963,961 B).

The decompressed image is an archive:

```
u32   member_count
char  member_name[]      NUL-terminated, repeated
...   per-member records (offset / size / checksum — exact layout pending)
...   concatenated member payloads
```

Member names are plain ASCII, so the TOC is readable without any code analysis.

### Uniform member schema (all 44 stages)

| Member | Purpose | Notes |
|---|---|---|
| `data.cnf` | manifest | newline-separated list of the members below; `.nocache` entries are preloaded |
| `localfont.fcx` | font | present in every stage, including the tiny ones |
| `@cache.qar` / `cache.qar` | archive | `@` prefix appears when it is a preload hint |
| `cache.dar` / `resident.dar` | archive | `resident` variant on the `init` stage |
| `scenerio.gcx` | scenario script | Konami's own spelling; drives per-stage logic |
| `<stage>.rlc` | stage resources | geometry, textures, card data |

Every stage follows the exact same six-or-seven member pattern. `lobby` (`@cache.qar`,
`scenerio.gcx`, `lobby.rlc`, `cache.qar`) and `suspsave` are the leanest, at 54 KB raw.

## `.psq` — audio

```
0x00  "IPSQ8"    magic
0x04  u32        unknown, always 0x00000038
0x08  u32        block/offset A
0x0C  u32        block/offset B
0x10  u32        flags-ish (0 or 1)
0x14..0x37       zero padding
0x38  ...        stream, starts with a "IWAV" sub-header
```

Sample (`e002st00_1.psq`):

```
0x38  00 00 00 00 00 00 00 00  49 57 41 56  e0 02 00 00  ff 00  c0 1f 00 00 ...
      ----------- zeros -----  "IWAV"      size?        ?
```

`e001system.psq` and `e009endbgm.psq` additionally contain full `RIFF`/`WAVEfmt `
headers, so the family is definitely **ATRAC3plus audio** in a Konami wrapper.
Entropy 6.3–8.0, consistent with compressed audio plus embedded PCM.

**Not on the critical path.** The game decodes these itself through `sound.prx`
(`sceAtrac3plus` + `sceSasCore`), so a correct HLE reproduces playback for free.
Decoding matters only for asset remastering.

## `.a3p` — audio, already solved

```
52 49 46 46  64 f7 0a 00  57 41 56 45  66 6d 74 20  34 00 00 00
R  I  F  F   <size>        W  A  V  E   f  m  t    52 bytes
fe ff 02 00  44 ac 00 00  a0 1f 00 00
WAVE_FORMAT_EXTENSIBLE, 2ch, 44100 Hz, 8096 B/s

```
ATRAC3plus. `ffmpeg`/`vgmstream` decode it; no RE needed.

## `.pmf` — video

`PSMF0012` + MPEG-PS. 75 files, 149 MB, six language variants per cutscene
(`_e`, `_f`, `_g`, `_i`, `_s`). Standard `sceMpeg` input.

## Consequences

1. **One data model, 44 times.** Any runtime work on `_zar` pays off across the whole
   game, and `lobby`/`suspsave` are large enough to be realistic and small enough to
   debug.
2. **Text is stored uncompressed** inside the stage packs. UI copy, card descriptions and
   mission briefings are extractable as plain strings — directly useful for a native
   touch UI that does not have to render through the engine font path.
3. **Audio and video are off the critical path** for a first playable build.

## Open items

- Exact `_zar` per-member record layout (offset vs. size vs. checksum field order).
- `scenerio.gcx` script format — this is where per-stage game logic lives.
- `cache.qar` / `cache.dar` archive nesting.
- `.rlc` resource layout (geometry, textures, card data).
- `.fcx` font cache.
