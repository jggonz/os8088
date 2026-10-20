# 8BitBike: the cartridge-faithful rebuild of the native Excitebike

Updated: 2026-09-30. Branch `game/excitebike`, PR #206. Contract: SPEC.md §102.
Product name: **8BitBike**. The package is `8BITBIKE.O88`. The source directory stays
`apps/excitebike/`, as DrMarco kept `apps/drmario/`.

This plan rebuilds the native game so that it looks, sounds and plays like the 1984 Excitebike
cartridge:
- **graphics:** the cartridge's own tiles, bike poses, screens and palettes;
- **courses:** the cartridge's five course streams and 36 track pieces;
- **rules:** every table and routine, ported from its 6502 code;
- **sound:** its six songs, its engine sound and every effect.

It is a native 8086 game, not an emulator. It keeps the scroll engine, the sprite loader, the
sequencer, the harnesses and the three adapters that the original-art build proved, and changes
what they draw, simulate and play.

**Two rules bind every wave (owner, 2026-09-30):**

1. **Match the cartridge, but never depend on it.** The graphics, levels, rules and sound are
   the cartridge's. They reach this tree through a one-time **importer** that reads a local
   disassembly checkout and writes plain, reviewable, committed source files. Nothing on the
   build path, the shipped disks or the regression suite reads the disassembly:
   - `make` on a clone that has never seen `NES-Games-Disassembly` builds the whole game;
   - every gate passes on that clone.

   The disassembly is needed only to *refresh* what the importer wrote.
2. **Nintendo's marks are replaced with generated art.** The replacements are made with the
   codex CLI's image generation, the way DrMarco's splash and screen surround were made. The
   marks are the EXCITEBIKE logo, the NINTENDO banner plate on the track, and the
   "(c)1984 NINTENDO" line, which is removed and not replaced: 8BitBike shows no copyright
   line at all. Everything else the cartridge draws is kept.

This plan supersedes **§0 (the ART AND AUDIO POLICY) of `docs/plans/EXCITEBIKE-PLAN.md`**, which
required all-original art and sound. That file stays the design record of the engine: its §4
video, §8 governor, §9 budget and §13 memory still describe what this builds on. SPEC.md §102 is
changed **before** each wave's code, as CLAUDE.md requires.

An unchecked item is planned, not implemented.

---

## Baseline and reference

- **Implementation.** `apps/excitebike/`. The PR #206 head is `a6b9ca90`, merged with main
  (#207, #208) at `7ba6b6ca`.
- **Compiler and tools.** The compiler is `tools/excitebike_assets.py`, which is stdlib-only and
  already reads committed text sources: `art/*.txt`, `palette.json`, `tracks/*.trk`,
  `audio/*.mml`. The reference model is `tools/exbsim.py`.
- **Tests.** `tests/excitebike_*.py`: 35 suite rows, 2 of them fast.
- **What the original-art build proved on MartyPC's 4.77 MHz 8088:**
  - scrolling at VGA 0Dh 54.6 fps, CGA 59.7 fps and Hercules 50.6 fps (course 1 at turbo, sound
    on);
  - a worst frame of 20.5-22.2 fps, in Selection B;
  - a rider step bit-identical to `exbsim.py` over 29,465 steps;
  - the sequencer on the speaker, AdLib and Sound Blaster;
  - four floppy geometries;
  - one instance on the 256KB floor machine.
- **The reference.** NES-Games-Disassembly, `Excitebike/`, commit `df2c8e5`. It is **local and
  never committed**; the importer finds it through `EXCITEBIKE_REF`. It is an NROM-128 cartridge
  (16KB PRG, 8KB CHR, mapper 0). The importer refuses any other bytes:

  | file | bytes | SHA-256 |
  |---|---|---|
  | `CHR_ROM.chr` | 8,192 | `3c1bf416113e35d0b3c9151721acaa22aebbf02a69dcc5bc5faf11869d50b76f` |
  | `bank_FF.asm` | 564,518 | `faec8a3abb86e641d43bafe504b2582a9cfc78c28a43983fc736150c8b323dd5` |
  | PRG rebuilt from the listing | 16,384 | `9e1cca6ba64855acb98bb9ffa422c0b2d842c7dd9a2f701c15c7ac66651c813d` |

  `header.bin` + the rebuilt PRG + `CHR_ROM.chr` hash to SHA-1
  `2E9897846E54A4A9865E87DE7517C6710BDEC255`. That equals the disassembly's own `assemble.sh`
  pin, so these two files are the whole cartridge (verified 2026-09-30).
  `reference/excitebike/README.md` is committed in `reference/1942/`'s shape: the pins, the
  labels, addresses and meanings, and **no bytes**.
- **Labels.** Every label in this plan is `bank_FF.asm`'s. A line number appears only where a
  label is ambiguous.

---

## The method (what made 1942 play right, applied here)

1942 matched its cartridge in one commit, `d9313c10`. That commit fixed eight places where a
table had been **imported correctly and interpreted wrongly**: a timer decremented three times
per update, an origin 16px off, a random pick where the cartridge kept a shared counter, and a
scroll rate on the wrong clock. Every fix came from reading the routine that *uses* the table.
DrMarco showed that the art pipeline can be split: a Pillow/imagegen authoring half whose
**output is committed**, and a stdlib half on `make`'s path. This plan uses both, and adds a way
to check the port against the real game.

1. **Port routines, not tables.** Each rule cites the routine that reads its table, and the port
   follows that routine's order, cadence and edge cases.
2. **One clock.** A simulation step is one NES frame, at 60.0988 Hz virtual time. That is
   already the engine's design: EXCITEBIKE-PLAN D10, with the governor and an accumulator
   capped at 8. The scroll, the course generator, heat, the clock, the AI and the music all
   advance in that unit. Rendering runs as fast as the machine can go. Do **not** copy 1942's
   18.2 Hz update: Excitebike's rules are per frame, and many run every fourth frame
   (`$004C = frame & 3`).
3. **The cartridge is the oracle, recorded once.** "Oracle" means the source of the right
   answers a test compares against. Here it is the real game:
   - The importer's companion `tools/exbnes/` runs the cartridge in agnes, a small MIT-licensed
     NES emulator, on the host. It feeds a scripted controller byte each frame, and records what
     the real game did: the rider's speed, pitch, lane, height, heat and clock, the screen, and
     the sound registers.
   - Those recordings are **committed** as test fixtures under `tests/fixtures/excitebike/`.
     The regression rows compare our game against them on MartyPC, and never need the emulator
     or the disassembly.
   - **Selection A never reads the random-number generator.** So with the same controller bytes,
     our rider must equal the cartridge's **byte for byte, every frame, for a whole race**. That
     is the headline gate.
   - **Selection B** is made deterministic by recording the cartridge's RNG bytes each frame and
     feeding them to our game.
4. **Independent checks.** A test decodes the committed sources itself, never the compiler's
   output. Every gate has a negative control that must fail: a flipped pixel, a one-frame input
   offset, a one-bit edit.
5. **Adaptations are named.** Anything that cannot be the cartridge's goes in
   `apps/excitebike/cart/ADAPTATIONS.md` with its reason and its measurement:
   - coarse scrolling on CGA;
   - the 200-line crop;
   - FM instead of the 2A03;
   - the replaced marks.

   Nothing is adapted silently.

---

## Decisions

| # | decision | why |
|---|---|---|
| P1 | **Commit the importer's output, not the cartridge.** `tools/excitebike_import.py` reads the local disassembly once, checks the pins, rebuilds the PRG, reads tables **by label**, and writes committed text sources under `apps/excitebike/cart/` (listed below). The build reads only those | the owner's rule 1. It also means every tile, table and course a reviewer sees is a readable diff, not a binary |
| P2 | **The build path never names the reference.** The rewritten `tests/unit/t_excitebike_clean.py` (fast tier) fails `make` if any Makefile rule on the build path, `tools/excitebike_assets.py` or anything under `apps/` reads `EXCITEBIKE_REF` or a path into the disassembly. Only `tools/excitebike_import.py` and `tools/exbnes/` may | the same fence the original-art build had, moved: then it forbade the cartridge; now it forbids *depending on* the cartridge |
| P3 | **A drift row.** `excitebikeimport` (full tier) re-runs the importer when `EXCITEBIKE_REF` is present and requires `apps/excitebike/cart/` to be byte-identical, naming the file and the cure. Without the reference it SKIPs, and says so | `t_drmarcoart`'s shape: a hand edit to an imported file is caught on any machine that has the reference |
| P4 | **Generated art replaces the marks, through a committed authoring pipeline**: prompt → codex CLI → master PNG (committed) → `tools/excitebike_art.py` (Pillow, authoring only) → committed tile text in the cartridge's own format and palette | DrMarco's `art/PROMPT.md` + `drmarco-*.png` + `art/native/` arrangement. The build stays stdlib |
| P5 | **No original-art fallback.** `art/*.txt`, `audio/*.mml` and `tracks/*.trk` from the original build are deleted when their imported replacement lands | a fallback is a second game to maintain; the imported sources are committed, so there is nothing to fall back from |
| P6 | **The field is the NES's 256 pixels wide, centred with 32px margins**, on VGA and CGA. The Hercules draws 512 card pixels at its 2:1 | the cartridge's AI spawns and despawns at the edges of a 256px window (`sub_E456`, `sub_DA9F`, `sub_DAE3`). It is also 32 columns to scroll instead of 40 |
| P7 | **200 lines, cropped where a 1984 television cropped.** Keep NES rows 3-28 except row 24 (below) | CGA and Hercules are 200 lines, and one layout on three adapters is what kept the engine testable |
| P8 | **Keep the os8088 shell around the game**: desktop window, splash, help, Esc, Alt+Enter, palette key, sound toggle | the platform's conventions, not the cartridge's |
| P9 | **Ship on the standard disks where it fits, otherwise on its own disk** (owner, 2026-09-30). Wave 8 decides each image with its arithmetic | DrMarco's precedent |

**The 200-line crop (P7).** The NES draws 30 tile rows, and a 1984 NTSC set hid about 8 lines at
the top and bottom, so rows 0 and 29 were never seen. Crowd rows 0-4 alternate two tiles (`$3F`,
`$3E`) that do not change along x, so rows 1-2 repeat rows 3-4. Row 24 is the blank `$FE`
separator above the HUD boxes. What stays:

| rows | what | lines |
|---|---|---|
| 3-4 | crowd | 16 |
| 5-7 | banner | 24 |
| 8-23 | the track band | 128 |
| 25-28 | the boxed HUD | 32 |
| **total** | | **200** |

Sprite y becomes NES y − 24. The oracle recordings **measure** the smallest sprite y on every
course. If a big jump ever enters rows 1-2, the crop moves or the sprite clips at the top, and
the adaptations file says which.

### What the importer commits (`apps/excitebike/cart/`, all text, all diffable)

| file | from | contents |
|---|---|---|
| `chr.txt` | `CHR_ROM.chr` | both pattern tables, 512 tiles, as 8x8 grids of digits 0-3. **The mark tiles are replaced** (see generated art) and each is marked `# replaced: G1` and so on |
| `palettes.json` | `_off_000_D3D6_12` .. `_D450_18`, `tbl_C160`/`C166`/`C16A`/`C170` | each theme in NES colour numbers, and the finish-flash cycle |
| `nespal.json` | `tools/exbpal.py` | the 64 NES RGB entries, from a committed NTSC 2C02 decode (ours, not a copied palette file), plus the per-theme CGA and Hercules maps, reviewed by eye |
| `screens/*.txt` | the `tbl_C0F6_lo` / `tbl_C111_hi` streams | every static screen as rows of (tile, palette): race background `01`, title `02`, track select `03`, results `04`, design bar `05`, frames `06`-`08`, interstitial `09`, messages `0A`-`0D`, HUD `0E`, design menu `0F`, `10`/`11`/`19` |
| `pieces.txt` | `tbl_F063_lo` / `tbl_F087_hi` | the 36 track pieces, column by column |
| `tracks/t1..t5.txt` | `_off_003_ED46_00`, `_EE59_01`, `_EDC8_02`, `_EED2_03`, `_EFA7_04` | each course stream byte, one entry a line, commented with its meaning |
| `poses.txt` | `tbl_D892`, `tbl_D8A9`, `tbl_D8F3`, `tbl_D8C9` | the 23 poses, the thrown rider and the per-bike palettes, as tile references |
| `scripts.txt` | `tbl_E55A_lo` / `tbl_E56F_hi`, `tbl_E6AD` | the 21 element scripts |
| `rules.inc` | every rule table named in waves 3-5 | `label: values`, with `; tbl_XXXX (routine)` on each, assembled straight into the package |
| `music/*.txt`, `sfx.txt`, `engine.txt` | `tbl_FDD3_index`, `tbl_FDDB`, `tbl_FF00`, `tbl_FF3E`, `tbl_F9*`, `tbl_FA24`/`FA39`, `tbl_FB77`.., `tbl_F9A3` | the six songs as event lists, the effects, and the engine tables |
| `PROVENANCE.md` | the importer | the pins, the importer's version, and every replaced tile or line with its reason |
| `ADAPTATIONS.md` | by hand | the register (method 5) |

The recordings live under `tests/fixtures/excitebike/`, beside the importer's other output, but
they are test data and are not shipped:
- per-frame rider traces, compressed;
- the RNG streams for Selection B;
- frame buffers at chosen frames;
- APU note logs.

`make excitebike-import` and `make excitebike-fixtures` refresh them. Both need
`EXCITEBIKE_REF`, and the second needs a host `cc`.

### Generated art (owner decision 1, via the codex CLI)

**Recipe (verified 2026-09-30, codex-cli 0.157.1, `image_generation` stable):**

```
codex exec --skip-git-repo-check --sandbox workspace-write -C <scratch dir> \
  "Use your image generation tool to create <prompt>. Save it here as <name>.png"
```

- The image arrives as a 1254x1254 PNG, copied out of `~/.codex/generated_images/`.
- Run it from a scratch directory, not the repo: codex hangs in this tree.
- Commit the prompt verbatim in `apps/excitebike/art/PROMPT.md`, and the chosen master beside
  it, as DrMarco does.
- `tools/excitebike_art.py` (Pillow, authoring only) quantizes the master into tiles in the
  cartridge's 2bpp format and the target theme's palette, and writes the replaced tiles into
  `cart/chr.txt` and the placements into `cart/screens/*.txt`. A person reviews the result
  before it is committed; imagegen at 16-24 pixels tall needs a hand pass, and that edit goes
  into the committed text, not the PNG.

**Every prompt says:** flat NES-era pixel art, a hard pixel grid, the named palette only, no
gradients or anti-aliasing. It also says: no Nintendo wording, no "Excitebike", no copyright
line, and nothing traced from the cartridge.

| id | replaces | footprint | replacement |
|---|---|---|---|
| G1 | the **EXCITEBIKE title logo**, tiles `$98-$AF` at title rows 8-9, columns 7-24 | 144x16, within those 24 tile ids plus any the import proves unused | an **8BITBIKE** wordmark in the title palette (`_off_000_D450_18`, 3 colours + ground), dynamic and italic like a motocross decal. It is generated large, then reduced |
| G2 | the **NINTENDO plate** on the track banner, NT0 rows 5-7, columns 11-20 | 80x24, the plate end caps `$96`/`$97` kept | an **8BITBIKE** plate in the style of the cartridge's own BEST plate (`$93-$95`), so the banner alternates 8BITBIKE / BEST every 256px as it alternated NINTENDO / BEST |
| G3 | **"(c)1984 NINTENDO"**, title row 24 | one text row | **nothing**: the row is left blank (`$FC`). The game shows no copyright line anywhere, and neither does G5 (owner, 2026-09-30) |
| G4 | the name in the text screens: "EXCITEBIKE" in states `07` and `09` | text rows | **8BITBIKE** in the cartridge font |
| G5 | the **desktop splash** `8BITBIKE.VGA`/`.CGA`/`.HRC`, which replaces `EXBSPL.*` | 320x200 native | a generated illustrated title in DrMarco's SPLASH shape. The composition reference is our own render of the in-game title with G1; the character reference is the imported rider pose sheet. It keeps a pure-black area for the runtime ENTER/H lettering |
| G6 | the **Wire catalog picture**, at release time | the catalog's format | from G5 |

The help panel is composed from in-game tiles and the font, as today, with nothing generated.

---

## Delivery order

### Wave 0. The importer, the fence, the rename, and the oracle

- [ ] **SPEC.md §102 first.**
  - Retitle it "8BitBike — native motocross racer".
  - Replace the ART AND AUDIO POLICY paragraph with rules 1 and 2 above and P1-P3.
  - Mark `docs/plans/EXCITEBIKE-PLAN.md` §0 superseded.
  - Rewrite `apps/excitebike/README.md`'s opening.
- [ ] **The rename.**
  - `8BITBIKE.O88`; sidecars `8BITBIKE.VGA`/`.CGA`/`.HRC` (the splash) and `8BBV.GFX`,
    `8BBC.GFX`, `8BBH.GFX`.
  - The window title and About box read 8BitBike.
  - `make 8bitbike` and `make 8bitbikedisk` become the targets. The old `excitebike*` names stay
    as aliases, as `drmario` stayed for DrMarco.
  - `PKG_STEM` in `t_retired.py`, and `PKG_FILE` in `t_livefull.py` in wave 8.
  - `docs/INDEX.md` regenerated.
- [ ] **`tools/exbref.py`** (the importer's reader).
  - `check_source()`: the two pins, raising `SystemExit` that names the file.
  - `rom()`: rebuilds the PRG from the listing. It must read multi-byte `.byte` lines from their
    operands, since the hex column shows only the first byte. It raises on a conflicting byte
    at one address and asserts the SHA-256.
  - `table(label, n)` and `stream(label)`.
  - `--selfcheck`, which also proves the SHA-1.
- [ ] **`tools/excitebike_import.py`** writes `cart/` (the table above), deterministically: two
  runs are byte-identical. For wave 0, emit `rules.inc`, `tracks/`, `pieces.txt` and
  `PROVENANCE.md`. The later waves add their files.
- [ ] **The fence.** Rewrite `tests/unit/t_excitebike_clean.py` to P2's rule, with a negative
  control: a planted `EXCITEBIKE_REF` read in the compiler must fail it. Add the `excitebikeimport`
  drift row (P3).
- [ ] **`tools/exbnes/`, the recorder.**
  - Our driver, `oracle.c`, promoted from the scouting build in
    `/tmp/exb-reports/nesoracle/`, plus `getagnes.py`, which fetches agnes at pinned commit
    `0e4220b` with its MIT licence. That is the `getapple2rom.py` shape, since nothing on the
    build or gate path needs it.
  - The interface: controller bytes in; per frame, a named RAM subset, OAM, the scroll
    registers, the palette, and optionally the frame buffer and an APU register log, out.
  - Its selfcheck runs the attract demo into a race twice and requires the two recordings to be
    identical.
- [ ] **Gate.**
  - A clone with no disassembly and no `cc` builds with `make`, and the fast tier is green.
  - `t_excitebike_clean` fails on its planted read.
  - `excitebikeimport` is green with the reference, and SKIPs without it.
  - `8BITBIKE.O88` differs from `excbike.o88` only in its name strings.

### Wave 1. The cartridge's graphics, the generated marks, three adapters

- [ ] **Import `chr.txt`, `palettes.json`, `nespal.json`, `poses.txt` and `screens/01`,
  `screens/0E`.**
  - Sprites use pattern table 0 and the background table 1 (`PPUCTRL` `$90`/`$91`), at 2bpp.
  - The compiler bakes **(tile, palette) cells**, not tiles: the attribute table picks a palette
    per 16x16 block. A race uses about 149 background ids and 167 cells.
- [ ] **Colour.**
  - **VGA 0Dh:** each theme's 13 colours go into the 16 DAC entries: tracks 1-5, the shared race
    set `_D435_17` for the HUD and riders, and the title set `_D450_18`.
  - **CGA:** a per-theme map. Colour 0 is set through the colour-select port. The lead to try:
    palette 0 (green, red, brown) over a green ground is close to track 1's universal `$29`.
  - **Hercules:** a luminance map with the existing mono pairs.
  - The C key still cycles alternatives on CGA and Hercules.
- [ ] **The race picture from the imported streams.** The crowd, the banner with its plates,
  the dirt lanes `$3B-$3D`, the hedge, the tufts `$83`, and the boxed HUD at NES rows 25-28.
- [ ] **The field (P6, P7).**
  - Field x = NES x + 32.
  - The top picture is NES rows 3-7, the band 8-23, the HUD 25-28.
  - Re-derive VGA 0Dh's page and off-screen VRAM budget, and the CGA ring arithmetic, for 32
    columns. EXCITEBIKE-PLAN §4.3 is the method.
- [ ] **The column dictionary from `pieces.txt`.** 112 columns, 78 distinct, plus the plain and
  tuft defaults, with the attribute writes after each column pair. Raise `B_DICT` from 72 to what
  the pieces need (about 82-96), and assert the true count in `--selfcheck`.
- [ ] **Sprites.**
  - The 23 poses: 3x3 tiles, column-major (`sub_D1C7`), `$FC` in skipped slots.
  - The thrown rider, h-flipped and **behind the background**.
  - The shadow and streaks `$CB-$CE` (`tbl_E69A`), dust `$F6`/`$F7`, and start lights
    `$F0-$F5`.
  - The sprite text LAP, OVER HEAT (`tbl_C147_spr_T`) and GO.
  - The four gauge cells `$FE`, behind the background.
  - The per-bike palettes (`tbl_D8C9` = `0 1 1 2`).
  - `sprite.inc`'s 24x24, four-mask format takes these unchanged. Behind-background priority
    is new: AND the pose mask with the band's non-zero pixels.
  - The 8-sprites-a-line limit and its flicker are **not** emulated, and this is recorded.
- [ ] **Generate G1 and G2 with the codex recipe.**
  - Quantize each into the cartridge's tile format and palette, then hand-finish in `chr.txt`.
  - Commit the prompts, the masters and `PROVENANCE.md`'s replaced-tile list.
  - G3 and G4 are text edits in `screens/`.
- [ ] **Gate.**
  - The `tools/exbsim.py` renderer, rewritten to decode `cart/` itself, is pixel-identical to
    the guest on VGA, CGA and Hercules. This extends the `excitebikevideo*` rows: twenty random
    positions, a whole course, and the erase negative control.
  - **Against the cartridge:** the committed frame-buffer recordings for fixed positions on each
    course equal the guest's VGA field pixel for pixel after the crop and palette map. Only the
    G2 plate's cells are excluded, by a mask the test reads from `PROVENANCE.md`.
  - The smallest sprite y over every course is printed, and it tests P7.

### Wave 2. The five courses, the banner and the scroll

- [ ] **Compile `cart/tracks/` in the cartridge's grammar** (`sub_F4FF`):
  - `byte0` is the lap count;
  - bit 7 marks an entry that exists only on the second pass (`$0046` = 1);
  - bit 6 is a run of `tbl_F4C6[b & 15]`, repeated by the next byte;
  - `$09`, the finish gate, ends a lap (`loc_F62D`);
  - `$30` / `$31` switch the fallen-rider marker `$03A8`;
  - there is a 43-column runway.

  Emit `world.inc`'s column array for both passes. Delete the old `tracks/*.trk` and
  `tools/excitebike_tracks.py`.
- [ ] **Collision rows as the cartridge builds them.** `sub_F64E` stores each column's six
  lowest non-blank tiles, bottom up. `sub_D0AB` picks the row for a lane Y: under `$10` reads
  row 5, `$10` row 4, `$18` row 3, `$20` row 2, `$28` row 1, and 0 above. Tiles `$40-$97` start
  element scripts (wave 3); `$E4`, `$70-$73` and `$C0`/`$C1` keep their meanings.
- [ ] **The banner** alternates the G2 plate (NT0) and BEST (NT1) every 256px of the top zone.
- [ ] **The top zone runs at 1.125 times the track.** `sub_DBFE` adds the player's pixels plus
  their eighth, while `sub_DA6A` scrolls the track by `+$0060`. The crowd is uniform along x, so
  the plates are the only visible parallax.
  - First approach: a separate column counter for the top zone in the sheared page.
  - Alternative: the plates as blitted objects.
  - Measure both, and record which ships.
- [ ] **Scroll granularity.** The cartridge scrolls 1px; we step 8px.
  - Re-measure the VGA pel-panning option, which the original build's wave 6 refused. It now has
    32 columns and P6's 20% saving.
  - The CGA's 6845 start address moves in 8-pixel units and the Hercules in 16 card pixels, so
    both stay coarse, and that is recorded.
  - Sprites keep 1px world precision everywhere.
- [ ] **Gate.**
  - Column counts per lap, plain / pass 2: T1 667/706, T2 604/638, T3 730/750, T4 746/797,
    T5 650/674. They are checked by an independent decode of `cart/tracks/`, and against the
    recorded cartridge nametable writes for every column of every course.
  - Collision rows equal the recorded `$0400-$057F` ring at 50 points per course.

### Wave 3. The rider: the cartridge's rules, routine by routine

This wave clears all four deviations the original-art build recorded: MUD, LAND_WINDOW, TAKEOFF
and PITCH_SCALE. All four came from its own relative pitch and piece grammar. Replace the terrain
model of `sim.inc` and `exbsim.py` in lockstep. Keep the skeleton, which already follows the
cartridge: the stepping, the 4-frame cadence, heat, lanes and gravity. Every table comes from
`cart/rules.inc`.

- [ ] **Absolute pitch** `$00AC`, from 0 to `$0F`, level at 6. Steps come from `loc_CE83`:
  `tbl_C0D4` is 6 frames per step on the ground and 4 in the air, `tbl_C0C8` the Right target,
  `tbl_C0CA` the Left maximum. Past `$0A`, 13-frame steps run to `$0D`, then the flip.
- [ ] **The element-script VM.**
  - Trigger (`sub_E733` / `sub_E73B`): tile `$40-$97` gives element `((tile−$40)>>2)+1`.
  - The 21 scripts from `cart/scripts.txt` are `(x, code)` pairs by pixels travelled:
    - `$80|n` sets pitch n and slope class `tbl_E6AD[n−2]`;
    - `$40|n` defers handler n, which `sub_E927` re-runs every frame;
    - any other code runs handler n once.
  - The 18 handlers of `tbl_E6B7` (`sub_E794`): rise and fall at 1:1, 1:2 and 2:1, take-off
    `E934`, the hurdle check `E818` (on the ground, pitch below 7, speed at least 2.5), the
    finish `EA8F`, mud `E8FF` → `sub_CE5C`, the plateau and height setters, landing arming,
    arrows `E8D3` (heat := 8) and the kicker `E8E7` (+1.0 speed and a launch).
- [ ] **Speed.** `sub_CD1F`, `sub_CD59`, `sub_CE29` and `sub_CE58`:
  - accel `tbl_C0BC`;
  - caps `tbl_C0CE` / `tbl_C0D1` (A `$0320`, B `$0340`, finish coast `$017F`);
  - decel `tbl_C0C1`, **including** mud with B held (`$C0`) and without (`$7F`), with no cap;
  - grass `$E4`: `sub_CDEE`.
- [ ] **Jump and landing.**
  - Launch: `DCFA`, `DCFE` and `DD06`, with launch velocity = int(speed), halved after a
    bounce.
  - Gravity: `tbl_D868` by `input & 3`, so holding Left floats.
  - Landing: `sub_DC1A` and `sub_DC97`, with the window per slope class from `tbl_D86C` /
    `tbl_D87C` (+8 under speed 2) and the perfect pitch from `tbl_D88B`.
  - Target pitch from the slope: `sub_DCA0`.
- [ ] **Heat.** Targets `tbl_D8FB`, rates `tbl_D8F7`, cooling `$0B`. At `$20` comes the
  258-frame stall with OVER HEAT flashing (`loc_CCDD`), then heat 5.
- [ ] **Crashes.** The spin `sub_DDD1`, then the recovery timeline (`sub_D918` / `sub_D924`,
  phases `D933-D9F6`, `tbl_D8FF`, `tbl_D905`, `tbl_D90E`, `tbl_D8C4`): the rider is thrown,
  drifts, walks back 1px per A or B press, and remounts.
- [ ] **Lanes** (`sub_E96C`): 1 unit a frame, stopping at the centres `0E 1A 26 32`, with bands
  from `tbl_D913`.
- [ ] **Start and clock.** The start lights (`sub_DE31`): a 450-frame wait, then `$0024` =
  `$88`. The clock (`sub_DF30`) adds 1.6cs a frame, and **time runs out at 9:00**.
- [ ] **Keys.** Z = A (throttle), X = B (turbo), the arrows = the D-pad, Enter = Start. One pad
  byte a frame is the whole interface between the keys and the rules, the same byte the
  recordings were made with.
- [ ] **Gate: Selection A in lockstep with the recorded cartridge.**
  - The guest (`xb_tscript`) gets the recording's controller bytes, and every frame it must
    equal the recording: speed `$0090`/`$0094`, pitch `$00AC`, lane Y `$00B8`, height `$00BC`,
    air state `$00B0`, element `$0058`, script cursor `$00C4`, crash phase `$009C`, heat `$03B5`,
    clock `$0068-$006B`, pose, and screen x and y.
  - The scripts: a flat-out run and a turbo run on all five courses, both passes, to the finish;
    seeded random input; a wheelie held to the flip; every hurdle ridden too slow and too fast;
    every ramp landed nose-down, flat and nose-up; an overheat.
  - Coverage is **counted**: every handler, element, landing class and pose must be exercised,
    or the row fails.
  - Negative control: a one-frame input delay must fail within 60 frames.
  - **The rules section of `ADAPTATIONS.md` ends this wave empty.**

### Wave 4. Screens, HUD, race flow and the campaign

- [ ] **Every static screen from `cart/screens/`:**
  - title `02`, with G1 and G3;
  - track select `03` (`$79` is the cursor);
  - the interstitial `09`, with G4;
  - results `04`: the podium, BEST TIME, YOUR TIME, RANKING and the ordinals `$F3-$F7`;
  - the messages `0A-0D`: NEW RECORD, GAME OVER, TRY THE NEXT TRACK, TIME UP;
  - pause.

  Small patches come from the RAM-queue tables `tbl_C0EC_ppu_address`, `tbl_C134`, `tbl_C13C`
  and `tbl_C158`.
- [ ] **The race HUD.** Left box: rank ("3RD") and par. Centre: TEMP and the gauge. Right:
  TIME. `hud.inc` is rewritten; it was a one-row, 20-cell text line.
- [ ] **The state machine is `tbl_C000`** (`sub_C2A9`): 0 title, 1 track select, 2/5/`$A` race,
  3/6/`$B` results, 4/7 interstitial, and 8 and 9 design (wave 6). `flow.inc`'s screens and
  menus are replaced; the os8088 shell (P8) stays.
- [ ] **The cartridge's campaign.**
  - Track 1 plain, then **track 1 on the second pass**, then tracks 2-5 on the second pass.
    Track 5 repeats with the qualify window `$03F8` (starting at 4) halving each even round,
    until the rider fails. There is no ending screen.
  - Par: `tbl_C091` via `sub_C522`.
  - Rank (`ofs_001_C485`): at or under the record gives 1st and a new record. Otherwise d is
    the whole seconds over the default; 2nd or 3rd while d < 2·`$03F8`, and after that
    (d − 2·`$03F8`)/2 + 4, which does not qualify.
  - Bests are kept per track, pass and mode. The cartridge kept them across a soft reset with
    the `A5 5A` magic. Saving them to `SYSTEM/APPDATA/` (§19.9) is an adaptation, and is
    recorded.
- [ ] **The finish flash** (`sub_CA9B`): a palette rewrite every fourth frame on VGA. CGA and
  Hercules get a recorded substitute.
- [ ] **Attract.** After 560 idle frames, the AI drives bike 0 (`ofs_000_C346`).
- [ ] **The desktop splash is G5**; generate it and convert it through the DrMarco splash
  pipeline. Help is composed from in-game tiles.
- [ ] **Gate.**
  - Screens are pixel-identical to the host renderer on three adapters. On VGA they equal the
    recorded frames, masked only at G1-G4.
  - The state sequence for scripted menu inputs equals the recorded `$0041` trace.
  - The rank function is checked exhaustively on the host against a transcription of `C485`.
  - The campaign runs to the second repeat of track 5 in `excitebikeflow`.
  - Time up at 9:00 is reached.

### Wave 5. Selection B: the cartridge's riders

- [ ] **Setup** (`ofs_001_C875_02_06`): lanes `tbl_C0B4`, x `tbl_C0B8`, ring columns
  `tbl_C0B0`, and **three** opponents (bikes 1-3) on every adapter, as the cartridge has. The
  original build ran two on CGA and Hercules; wave 8's frame gate decides whether that
  adaptation comes back.
- [ ] **The brain, `sub_CED0`**, runs last in the frame, so its output is the next frame's pad
  byte `$005C,X`.
  - Pitch steering `sub_CFEA`, toward `$007C`.
  - Throttle `sub_CFCB`, pressing A while under the target speed.
  - Look-ahead `sub_D018` / `sub_D0C6`, 4 tiles: a lane change over `$C0`/`$C1` or mud
    (`sub_CF47`, `sub_CF6A`), a wheelie to target pitch 9 at a hurdle, and otherwise following
    the slope.
  - Lift-off and a lane change when a bike is within `$48` pixels ahead: `sub_CF0C`.
  - Target speed `sub_DB50`: `tbl_D8D5` gives the integer part, `tbl_D8CD[RNG & 15]` the
    fraction.
  - Accel `tbl_C0BC[X]`. No turbo, no heat.
  - The AI drives wave 3's rider step unchanged, and `ai.inc`'s swapped-state framework stays.
- [ ] **Life cycle.** Despawn `sub_E456` / `sub_DB95`; respawn from either edge `sub_DA9F` /
  `sub_DAE3`, rate-limited by `$0023` (`tbl_D8DF`, `tbl_D8E2`). The fallen-rider set piece
  `sub_DBC4` spawns bike 3 crashed ahead while the `$30` marker is on.
- [ ] **Bike against bike** (`sub_DFD5`): within 2px both crash; within 12px the rear bike
  does. The fallen-rider box is 11x7 (`sub_E09F`).
- [ ] **The RNG** is the 64-bit LFSR `sub_D326` at `$0018-$001F`. The cartridge advances it in
  its idle loop, so its sequence depends on how long each frame took, and no port can
  reproduce that. Advance it once per frame and at the cartridge's call sites; this is recorded
  as the one place play differs by design.
- [ ] **Gate.**
  - RNG-injected lockstep: the recording's `$0018-$001F` go into the guest each frame, and all
    four bikes must equal the recording, over a full Selection B race on each course.
  - An uninjected run checks the statistics: spawn rate, lane-change rate, and the distribution
    of the gap to the player.

### Wave 6. Design mode (the track editor)

- [ ] **The design screens.** The menu (state `0F`: PLAY MODE A, PLAY MODE B, DESIGN, SAVE,
  LOAD, RESET), the editor (`09`), and the item bar on row 25 (`05`: `ABCDEFGHIJKLMNOPQRS`, CL,
  END).
- [ ] **The design stream** is the cartridge's format, in the `$05E0` edit and `$06E0`
  persistent buffers, and the generator plays it as a sixth track.
- [ ] **SAVE and LOAD** go to a file in `SYSTEM/APPDATA/` (§19.9) where the cartridge used the
  Data Recorder (states `$C`/`$D`). The format is the `$06E0` stream, replacing today's
  `EXBTRACK.DAT` layout.
- [ ] **Gate.**
  - Recorded design streams (the cartridge fed at `$06E0`, as the scouting `exblib.py` does)
    generate the same columns as the guest.
  - Save, reload and play reproduce the course byte for byte.
  - A corrupt file is refused in words.

### Wave 7. Sound from the cartridge

- [ ] **Import `cart/music/`, `sfx.txt` and `engine.txt`.** `tools/excitebike_audio.py` becomes
  the compiler of those files and reads nothing else.
  - The six songs (`tbl_FDD3_index`, `tbl_FDDB`): 0 title and game over, 1 qualified, 2 track
    select, 5 race start, 6 tick, 7 pause.
  - Four streams: pulse 2 harmony, pulse 1 lead, triangle bass, noise percussion. Periods from
    `tbl_FF00` (index 1 is a rest); durations from `sub_FF53`, the tempo base and `tbl_FF3E`.
  - 707 events in all, a self-check. Songs do not loop.
  - Delete the old `audio/*.mml`.
- [ ] **The engine** (`sub_FA74`).
  - `$F3` chases `$FC & $7F` at the interval `tbl_F9F4`. Pitch = `tbl_F9C4[$F3]` + vibrato
    `tbl_F9B3`, with the danger set `tbl_F9BF`.
  - Period = `$7FF · 2^(−A/32)`, 87-416 Hz. The triangle plays a fifth below.
  - The `$4011` DAC ducking has no equivalent here, and is recorded.
- [ ] **Effects.** The crash whistle (`tbl_FA24` / `tbl_FA39`), the noise effects
  (`tbl_FB77`, `tbl_FBA5`, `tbl_FBD3`, `tbl_FBF2`) and the pulse blips (`tbl_F9A3`). They fire at
  the cartridge's own call sites: landing `DC37`, lap `EADE`, overheat `CCE4`, pause `CA4A`,
  start `DE64`. Effects are muted while a song plays.
- [ ] **Voices.**
  - FM (AdLib, and Sound Blaster FM): four OPL voices, with patches that approximate the 12.5,
    25 and 50% duty squares, the triangle, and noise.
  - Speaker: the lead during songs and the engine during races.
  - Durations are in NES frames on the game's 60.0988 Hz clock.
  - `audio.inc`'s sequencer is kept and its data swapped.
- [ ] **Gate.**
  - The recorded APU log per song is the reference: every note-on's frequency and frame time
    equals the compiled event list.
  - The engine pitch per throttle level equals the recorded pulse-2 period.
  - `excitebikeaudio`, `excitebikeaudiofm` and `excitebikeaudiocap` re-run on the new data.

### Wave 8. Performance, adapters, shipping, the audit

- [ ] **Frame gates on MartyPC at 4.77 MHz**, re-run with the cartridge's art and rules: the
  scroll, the governor, the Selection A lap and the Selection B worst frame, on VGA, CGA and
  Hercules. They update PERFORMANCE.md.
  - The original build's floor is the bar: no worse than 20 fps in the worst Selection B frame.
  - Wave 5's third opponent is judged here.
  - Launch to title is measured; it was 3.8-5.9s.
- [ ] **EGA** stays refused unless the imported palettes change §102.8.5's ground.
- [ ] **Shipping (P9: standard disks where it fits, otherwise its own disk).**
  - Delete the `local` entry in `apps/RETIRED.txt`.
  - For each image, add 8BITBIKE with a dated ground and its arithmetic, or leave it off with
    its numbers:
    - the 1.44MB, 1.2MB and 720KB apps disks;
    - `games360.img`, which is derived;
    - the everything set (`tools/os88allapps.py`) and the live media;
    - `apps360.img` only if SPEC.md §24.6.1's curation finds room (it was full on 2026-09-30);
    - the 360KB combo only if it fits;
    - kern_small only if image + bss fits the 53,760-byte arena, otherwise §24.5's omission.
  - `t_livefull.py`'s `PKG_FILE` gains `"excitebike": "8BITBIKE.O88"`.
  - `make 8bitbikedisk` still builds its own four floppies, for the images it does not fit.
- [ ] **The audit.**
  - `build/excitebike-proof/` holds side-by-side captures, recorded cartridge against the guest,
    on VGA, CGA and Hercules: the title, track select, each course at three positions, a jump, a
    crash, overheat, each rank, and time up.
  - A human looks at `vm/xt-excitebike` in 86Box.
  - Every `ADAPTATIONS.md` entry has its reason and its number.
- [ ] **Documentation.** SPEC.md §102, `apps/excitebike/README.md`, the root README's make
  lines, `docs/INDEX.md`, and the session log below. The G6 Wire picture comes with the release
  that lists the game.

---

## What the original-art build keeps, and what it loses

| part | fate |
|---|---|
| `vga.inc`, `cga.inc`, `herc.inc` | **kept**; re-sized for 32 columns (P6). They are dictionary- and slot-palette-based, so NES cells drop in |
| `sprite.inc` | **kept**, plus behind-background priority. NES poses are exactly 3x3 tiles at 2bpp |
| `world.inc`, `game.inc`, `input.inc`, `video.inc`, `front.inc` | **kept** |
| `audio.inc` | **kept**, with new data |
| `sim.inc` + `tools/exbsim.py` | **skeleton kept**; the terrain model is replaced by the element-script VM (wave 3) |
| `ai.inc` | **framework kept**; the brain is replaced by `sub_CED0` (wave 5) |
| `flow.inc`, `hud.inc` | **replaced** by the cartridge's screens, states and campaign (wave 4) |
| `tools/excitebike_assets.py` | **kept** as the stdlib compiler; its inputs become `cart/` |
| `art/*.txt`, `audio/*.mml`, `tracks/*.trk`, `tools/excitebike_tracks.py` | **deleted** (P5) |
| `art/splash.json` and the EXF1 splash path | **kept**, fed by G5 |
| the harness (`xb_tscript`, `xb_ttrace`, the MartyPC drivers, the perf and geometry rows) | **kept**, and pointed at the recordings |

## Standing requirements (every wave)

- **8086 only, and SPEC.md §1's rules.** Offline conversion. No per-frame full-screen decode, no
  new kernel services. Account for image, bss and stack before adding. The 256KB floor machine
  still holds one.
- **Checking a wave.** A box is ticked only when its gate passes on MartyPC, VGA, CGA and
  Hercules, with the numbers in the session log and *"emulator results, not physical XT
  measurements"*.
- **QEMU** is used only for the VGA line-compare proof (`excitebikeg1`).
- **No gate may need the disassembly, the recorder or a host `cc`.** Only the refresh targets
  and the `excitebikeimport` drift row touch them, and that row SKIPs cleanly without them.
- **The fast tier stays under its budget.**
- **Four geometries** build, and three boot, at the end of any wave that changes a disk file.
- **Commit hygiene.** Wave builders name the paths they commit, never `git add -A`, and never
  commit `vm/*/86box.cfg` edits.
- **Codex.** Run image generation from a scratch directory with `--skip-git-repo-check`, never
  in the repo.

## Risks

1. **Scroll smoothness** is the most visible difference: a 1px NES scroll against 8px steps on
   CGA and Hercules. It is structural there. VGA can reach 1px only if wave 2's measurement
   allows it.
2. **The 1.125x parallax** has no hardware split on any adapter we drive, so the plates may
   become objects.
3. **Three opponents on CGA and Hercules** may break the 20 fps floor. The fallback is two,
   recorded.
4. **Generated marks at tile scale.** A 144x16 wordmark and an 80x24 plate are below what
   imagegen draws well. The master is a guide, and the committed `chr.txt` edit is the art. The
   budget is one hand pass per mark.
5. **Recorder accuracy.** agnes is an emulator, and the four TAS movies in `_misc/` desync on
   modern emulators. So recordings use **synthetic** controller scripts, whose meaning does not
   depend on a movie staying in sync. The recorder's selfcheck proves two runs are identical.
6. **Committed fixtures grow.** Compressed traces for 5 courses x 2 passes x ~8 scripts. Budget
   it in wave 3, and thin to sampled frames with full frames around events if it passes about
   2MB.

## Open questions for the owner

1. **Recorder source.** The plan fetches agnes at a pinned commit (`getagnes.py`) rather than
   vendoring it, because nothing on the build or gate path needs it. Vendoring it under
   `tools/exbnes/` is the alternative.
2. **PR shape.** Merge #206 now as the engine baseline (it is `local` and ships nowhere) and
   bring each wave as its own PR, or hold #206 for all nine waves. The recommendation is the
   first: nine waves on 30,000 lines is not reviewable.

## Wave summary

| wave | delivers | the gate that says it is done |
|---|---|---|
| 0 | importer, fence, drift row, rename to 8BitBike, recorder | a bare clone builds and passes; a planted reference read fails the fence |
| 1 | the cartridge's tiles, palettes, sprites and HUD; generated G1-G4; 256x200 field | pixel-identical to the host renderer and to the recorded frames |
| 2 | the five courses, collision rows, banner, parallax, scroll decision | every column equals the recorded nametable write |
| 3 | the rider, routine by routine | Selection A lockstep with the recording, every frame, all courses |
| 4 | every screen, the boxed HUD, the campaign and rank, splash G5 | screens pixel-identical; the state trace equals the recording |
| 5 | Selection B riders | RNG-injected lockstep of all four bikes |
| 6 | design mode, save and load | a recorded design stream equals the guest's course |
| 7 | the six songs, the engine and every effect | note events equal the recorded APU log |
| 8 | performance, shipping where it fits, the audit, docs | the perf floor holds; `ADAPTATIONS.md` is complete |

## Session log

- 2026-09-30: plan written. Inputs: the disassembly inventory (every label above was read in
  `bank_FF.asm`, and the PRG rebuild and SHA-1 were verified), 1942's matching method
  (`d9313c10`, `reference/1942/README.md`, `tests/n1942.py`) and DrMarco's art pipeline (#204,
  #207). Main (#207, #208) merged into the branch at `7ba6b6ca`; `make` green, fast tier 52/52.
  Codex image generation verified with a throwaway prompt (1254x1254 PNG). Owner decisions: the
  marks replaced by generated art; the name 8BitBike; ship on the standard disks where it fits;
  no copyright line on any screen.
