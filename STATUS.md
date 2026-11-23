# Project status — Metal Gear AC!D native Android (reverse-engineered original engine)

Last updated: 2026-11-23. This file records exactly where the work stands so a fresh session
(or a clean checkout) can resume without repeating days of reverse engineering.

## Objective
Run the **original PSP game** (not a reimplementation) natively on Android with a touch interface.
The user explicitly rejected the native card-game reimplementation in `native/` — that directory
is parked and must NOT be presented as the game. Product path = the original recompiled engine.

## Verified achievements (all committed and pushed to `main`)
1. `4568d81` — **Stage-load gate FIXED.** The blocker was a single malformed `R_MIPS_26`
   relocation in `kjfs.prx`: the `jal` at module offset `+0x444` landed on the module's ELF header
   (`base+0`) instead of the directory-tree builder (`base+0x34`). Effect: `kjfs::mount()` of a
   stage reported success but never parsed the `_zar`, so no virtual file could be resolved.
   Fix in `profiles/mgsa/host/main.cpp` (`advance_vblank`, frame 2): if
   `*(kjfs.base+0x444) == 0x0E718400`, write `0x0E71840D`.
2. Nine `~PSP`-encrypted system modules decrypted from the user's verified ISO using `pspdecrypt`
   (public KIRK keys) and installed as real ELF PRX in `extract/`. They now load as guest code
   (exports/relocations), not stubs. Encrypted originals backed up in `work/encrypted_backup/`.
3. `d9af3e7` — **Crash after stage load FIXED** (runtime patch, see below). `exit=0`, no crash.
4. **`title.prx` now LOADS as a real module** — `[module] loaded sce_lbn0x1ca80_size0x24fb1
   'mgp_stage' uid=4106 base=0x0A183000 relocations=3816 imports=14`. The full chain works:
   mount -> all 7 title files opened -> `_zar` read (916,419 bytes) -> title.prx loaded/executed.
   Run reaches 246 frames / 1265 draws (was 92 / 429 before the fixes).

## CRITICAL: uncommitted runtime patch (gitignored — MUST be re-applied on a clean checkout)
The post-load crash was caused by BOOT.BIN `lui` immediates holding low-range addresses
(`0x00010000`, `0x0007xxxx`) that a real PSP loader rebases but our `load_and_relocate` leaves low
(no explicit relocation records). The first store `sw v1,0(a1)` at guest `0x0887523C` hit
`0x00010000` outside backed memory. Fixed by aliasing low addresses to high RAM in:

**`work/tools/Yakumo/src/guest_memory.cpp`** — in `contains()` and `resolve()`, add:
```cpp
// MGSA low-address alias: BOOT.BIN lui immediates at 0x00010000/0x0007xxxx lack relocation
// records; a real PSP loader rebases them. Alias the 0x00010000..0x00020000 window into
// unused high RAM so stores through those pointers land in backed memory.
constexpr std::uint32_t kMgSaLowBase = 0x00010000u;
constexpr std::uint32_t kMgSaLowSize = 0x00010000u;
constexpr std::uint32_t kMgSaLowAlias = 0x0A800000u;
// contains(): before returning false:
if (c >= kMgSaLowBase && c + length <= kMgSaLowBase + kMgSaLowSize &&
    kMgSaLowAlias - kPhysicalBase + (c - kMgSaLowBase) + length <= bytes_.size()) return true;
// resolve(): before the final Ram return:
if (c >= kMgSaLowBase && c < kMgSaLowBase + kMgSaLowSize)
    return {Region::Ram, (std::size_t)(kMgSaLowAlias - kPhysicalBase + (c - kMgSaLowBase))};
```
The AOT fast path declines low addresses (negative `ram_offset_of`), so these accesses reach
`resolve()`. DO NOT lose this patch — it lives in gitignored `work/tools/Yakumo` (external repo,
never commit there; its remote is TeamGDB/Yakumo). `docs/yakumo-mgsa-runtime.patch` tracks the
other Yakumo patches; this one must be added there too before relying on a clean rebuild.

## Where we stopped / next step
EDRAM is still zero (`non_zero_words=0/524288`) even though the guest issues ~1265 draws. The
title module is executing but **nothing visible is rendered yet**. Last investigation: the GE
display lists are the empty 220-word clear list (`commands=220 draws=0 vertices=0`). Next:
dump the title stage's real display lists (`MGSA_GE_DUMP_LATE=1` with a late-frame budget) and
implement the missing primitive/alpha-test paths in `rasterize_ge_list` in `main.cpp` until EDRAM
becomes non-zero. The rasterizer currently only handles `primitive == 6` (sprites), s16 positions,
CLUT4 textures, and ignores alpha test.

## Key facts / gotchas
- ISO SHA-1 `2d2e5604297ac922f4a117952315ea91348af174` matches the user's `.sha1` exactly.
- Guest RAM is 64 MiB (was 32; the decrypted modules need the space); `kModuleReturnSentinel`
  moved to `0x0A7FF000` (synthetic PC, not backed memory); arena ends `0x0A7EF000`.
- `PSPRECOMP_NO_CHAIN=1` and stdout redirected to a file is UTF-16LE on Windows PowerShell —
  read logs with `encoding='utf-16'` in Python.
- `L_####` labels in generated AOT code are NOT guest addresses. Use `work/dis.py` on BOOT.BIN
  with segments `0:0x80:0x139D74,0x139D80:0x139E00:0x7DBC` for real MIPS.
- Full journal of every falsified lead: `docs/07-stage-load-re.md`.
- Build: copy `profiles/mgsa/host/*.cpp` into `work/tools/Yakumo/profiles/mgsa/host/`, then
  `cmake --build work/tools/Yakumo/out/mgsa --target mgsa_boot -j 4`.
- Run: `PSPRECOMP_NO_CHAIN=1 mgsa_boot.exe --root extract --elf
  extract/PSP_GAME/SYSDIR/BOOT.BIN --iso "Metal Gear Ac!d (Europe) (En,Fr,De,Es,It).iso"
  --max 600000`.
