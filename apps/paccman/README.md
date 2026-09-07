# PaccMan

A native reimplementation, in C, of Andre Weissflog's **`pacman.c`** — the
arcade-faithful C99 Pac-Man at <https://github.com/floooh/pacman.c> — for
os8088. Package `PACCMAN`, product name **PaccMan**. `SPEC.md` §91 is the
contract and `docs/PACCMAN-PORT-PLAN.md` the design record.

The reference commit is **`0f5ec5a384c1988d9889046d92e615219e1cf3b4`**
(2 Jul 2026). That hash is pinned in `tools/paccman_assets.py`, printed in
`apps/paccman/pmc_rom.c`'s header, checked by `tests/unit/t_paccman.py`, and
named on the About card.

## It is not PACMAN

os8088 ships **two** Pac-Men and they share nothing:

| | `apps/pacman` (§89) | `apps/paccman` (§91) |
|---|---|---|
| what | the Roklan/Atari disk version | the Namco arcade layout |
| written in | hand-written 8086 assembly | C (§73), plus one assembly composer |
| the field | 40×22 tiles, horizontal | 28×36 tiles, 224×288, vertical |
| package | `PACMAN` | `PACCMAN` |
| disks | `GAMES/` on the shipped apps floppies | `build/paccman*.img`, on demand |
| targets | — | `make paccman`, `make paccmandisk` |

No file, name, image, make target or vm directory answers to both, by §73.12's
rule — *"because the next person to touch either will silently get the other."*
The names differ by one letter, so `make paccman` and `build/pacman.o88` are a
typo apart: check twice.

## Provenance

- **The code** this port follows is Andre Weissflog's, **MIT**, © 2020 Andre
  Weissflog. `apps/paccman/LICENSE` is that licence verbatim, and it is also
  `incbin`'d into `PACCMAN.O88` so a copy of the package that has been
  separated from this directory still carries the terms.
- **The tile, sprite, hardware-colour and palette tables** are **Pac-Man
  arcade ROM data (Namco)**, which the reference embeds and this port carries
  the same way. Shipping them was a decision the user took, following this
  tree's existing precedent for third-party ROM data — `apps/c64/rom/`, a
  stated departure from `CONTRIBUTING.md` §6 (see `docs/C64-SPEC.md`).
- **The two sound register dumps** (the prelude and the death tune) were
  captured from an arcade emulator, as the reference's own header says.
- **The gameplay rules** follow Jamey Pittman's *Pac-Man Dossier*, which is
  where the reference took them from.
- **Nothing is vendored** (`CONTRIBUTING.md` §6). What is committed is
  `apps/paccman/pmc_rom.c` — the mechanical output of
  `tools/paccman_assets.py` — with the pin in its header. Every file in this
  directory that carries derived material says so in its own header.

### Reproducing `pmc_rom.c`

```
git clone https://github.com/floooh/pacman.c ../pacman.c
git -C ../pacman.c checkout 0f5ec5a384c1988d9889046d92e615219e1cf3b4
python3 tools/paccman_assets.py --ref ../pacman.c            # rewrite it
python3 tools/paccman_assets.py --ref ../pacman.c --check    # or just diff it
```

The extractor **refuses a checkout that is not the pin** unless `--unpinned` is
given. `apps/paccman/build.sh` runs the `--check` only when `$PACMANC_SRC`
names a checkout, and otherwise prints one note: **the committed file is the
build's truth**, and an ordinary build needs neither the tool nor the
reference.

## Building and running

```
make paccman        the host checks, then build/paccman.o88
make paccmandisk    ...and the floppy in all four geometries
make pmcbandbench   the band composer's benchmark (below)
make xt-paccman     an 86Box IBM XT at 4.77MHz with that floppy in B:
make 386-paccman    an 86Box 386DX/25 with the 1.44MB floppy in B: (full speed)
make test TESTAPPS=build/paccman.img
```

Then double-click **Disk B**, then **PACCMAN.O88**. It needs the C toolchain:
`tools/setup-cc.sh` first (`docs/C-TOOLCHAIN.md`).

## Keys

Every key's meaning is the reference's (`pacman.c` 782–817, 926–946) except
where this table says otherwise.

| key | what |
|---|---|
| arrows, **W A S D** | steer. These are **levels**, not events: the frame reads whether the key is *down* and applies it to every game tick of that frame, with priority up > down > right > left and the current direction as the default |
| **F** | full screen on and off. It is **never** an "any key" — the reference gives it its own case with no any-key beside it, so pressing F on the attract screen toggles full screen and does *not* start the game |
| **Esc** | leaves full screen when we hold it; otherwise an ordinary "any key" |
| **P** | pause / resume — an addition; the reference has no pause. **In play only**: on the attract screen it is an ordinary "any key" |
| **N** | new game — an addition, and **in play only**, for the same reason |
| **Space** | in play, resume — `PACMAN.O88` advertises "P OR SPACE TO RESUME" and this follows it. On the attract screen it is an ordinary "any key", which is all the reference makes of it |
| any other key | "any key": starts the game from the attract screen |

The About card is dismissed by any key or menu command, and **reading it pauses
the game and dismissing it leaves the game paused** — `apps/pacman` does exactly
this, and whoever opened the card was not watching the maze. `P`, `Space` or the
menu starts it again. It pauses a **game** and not the attract screen: `P` and
`Space` are bound in play only, so a card that stopped the attract screen would
leave the menu as the only way back — and a **stopped window takes no game input
at all**, so keys pressed while it is stopped are dropped rather than remembered
and spent on the first frame after `Resume`.

There is no Ctrl or Alt binding, so the BIOS folds of Ctrl-H/I/M and
Ctrl+Space cost this program nothing.

### The one input divergence

**A key press is latched until the next frame's poll.** The reference runs at
60 Hz, so a frame is one game tick and reading the key's live state each tick
loses nothing. Here a frame is 3–11 game ticks long, and a tap shorter than
one frame would be lost entirely — so a press sets a latch that the next poll
consumes. A tap therefore becomes exactly one frame of "held", and nothing
more: **a hold released before a junction is still forgotten, as in the
reference.** There is no buffered turn. It changes the feel against
`PACMAN.O88`, which does buffer one.

## The Game menu

`New Game`, `Pause`/`Resume`, `Sound Off`/`Sound On`, `Full Screen`. The
reference has no menu at all — `sokol_main` asks for a bare window — so every
item is an addition, and the shape is the one `apps/pacman` already set on this
system.

**All four act, and nothing is greyed.** Two of them were: `Pause` while the
image had no tick loop to stop, `Sound` while it had no sound code to silence.
Each un-greying was the deletion of one marker byte and one reason, which is
what §47 predicts an un-greying costs. **The machine was never either reason** —
`osapi_snd_caps` answers a constant on every kernel this OS boots — so a greyed
`Sound` saying "no speaker" would have been greying a guess rather than a fact.

**Two of the labels carry the state, because the label is all there is.** The
kernel has one item marker and it is `MENU_DIS`; there is no check mark, PaccMan
has no status line (its content is the 224-pixel arcade field) and the title bar
does not change, so a paused window would otherwise be pixel-identical to a hung
one. Each label therefore names the ACTION on offer — `Pause` while running and
`Resume` while stopped, `Sound Off` while sound is on and `Sound On` while it is
off. That wording is not decoration: an imperative asserts the state it would
leave, which is why `Sound Off (No Sound Yet)` was wrong for a control that
could not act, and why `Sound Off` is right for one that can.

**A paused window says so in the item label, and that is the only place it
can.** The kernel has no check-mark marker, PaccMan has no status line — its
content is the arcade field — and the title does not change, so a paused
window would otherwise be pixel-identical to a hung one; `apps/pacman` puts
`PAUSED - P OR SPACE TO RESUME` in its own footer for the same reason. The
item reads `Pause` while the game runs and `Resume` while it is stopped, and
**`SPACE` resumes as well as `P`**, which is `apps/pacman`'s binding rather
than one invented here — so the About card advertises the pair, on two key
lines rather than one (`Arrows/WASD move. N new.` / `F full. P/Space pause.`).
For a program with no status line that card is the only place inside it a key
can be discovered, and `SPACE` was bound, specified and advertised nowhere
until the lines were reflowed. The card stays at ten lines: eleven is 146 rows
against CGA's 144-row content box, and the last one would be cut off.

**The About card is dismissed by any key and by any menu command.** The
standard card only *draws*; the flag and the dismissal belong to the package
(`apps/cc/os88.h`), and a key that takes it down is swallowed rather than also
starting a game — which is the reference's own "any key" posture.

## What the reference itself does not have

The port carries **the reference**, not the arcade. Three of the five below
are `pacman.c`'s own header saying what it leaves out (lines 50–54, under the
sentence introducing them at 44–48); the other two are read off the source
itself, and each line says which. Every one is absent here for the
reference's reason and is a recorded follow-up, not a defect:

- **the attract-mode chase** — the header, line 50–51; a `// FIXME: animated
  chase sequence` stands where it would be, at `pacman.c` 2391;
- **the coffee-break intermissions** between levels — the header, line 52;
- **the per-round speed table** and the post-round-5 dot-limit changes — the
  header, lines 53–54; the constant speeds are the reference's;
- **Pinky's diagonal-overflow targeting bug** of the arcade — not in the
  header: `// FIXME: does not reproduce 'diagonal overflow'` at `pacman.c`
  1910, in Pinky's target case;
- **two-player mode and credits** — not in the header either: `2UP` and
  `CREDIT  0` are static text at `pacman.c` 2335 and 2341 with nothing behind
  them, and they are carried verbatim.

## What this platform bends

- **Three voices become one.** The arcade has a three-voice wavetable with
  per-voice volume; the PC speaker is one square wave with neither. One voice
  is chosen per OS tick by priority — effects, then the siren / frightened
  tone / prelude *melody*, then the prelude bass — and `rom_wavetable` is not
  carried at all. Because 3.3 game ticks pass per sampled OS tick, an effect
  shorter than about three game ticks can fall between two samples.
- **The alpha fade is a cut.** 4bpp has no alpha, so a fade-out is one black
  fill and a fade-in a full repaint, with the reference's tick counts kept so
  every sequence keeps its length. The whole fade costs ONE `gfx_fill`, not one
  a frame: the black is remembered.
- **On a 1bpp adapter every coloured LABEL goes white; the pictures do not.**
  The reference colours each ghost's name and nickname with that ghost's own
  colour, and two of the four — Blinky's red and Inky's cyan — reach a
  monochrome screen as a 50% checkerboard, which is a fine ghost and an
  unreadable letter. The game screen's two coloured labels are the same two
  colours — `PLAYER ONE` in Inky's cyan and `GAME  OVER` in Blinky's red — and
  `GAME  OVER` is the one message the player most needs to read. So on CGA and
  Hercules all of them are drawn in the default colour, and the 2×3 ghost
  pictures keep the arcade's four, which is what tells them apart. VGA and EGA
  are the reference's, unchanged. The ink is chosen when the label is WRITTEN,
  so a window carried onto a display of a different depth (§39.12's extended
  desktop) keeps what it was written with: the attract screen re-writes its four
  names every cycle and heals itself, while `PLAYER ONE` and `GAME  OVER` are
  written once per game and once per game over and do not.
- **On a SHORT display the two source rows are MERGED, per pixel.** CGA's
  window is 163 rows, so the composer has half the rows the field has — and
  sampling every other one drops source row 3, which is where the arcade font
  keeps every horizontal middle stroke. Sampled, **B E F G H S 3 6 9** all lose
  theirs: `E` and `G` read as `C`, `H` as two bars, and `HIGH SCORE` reads
  `IIIGII SCORC`. So the CGA arm composes

      merged = even | (odd & pmc_zmask[even])

  — *take the odd row's pixel only where the even row's is 0*. It cannot be an
  `OR`: a tile pixel is a 2-bit colour **index**, and `1 | 2` is 3, an ink that
  89 of the 256 tiles do not have. `pmc_zmask` is 0b11 in every zero field of a
  source byte, and one `xchg bx, dx` swaps it against the colour table around
  each byte, so the merge is one pass and not two. It costs a CGA band
  **22.39 → 26.16 ms, +16.8%** (per tile 1.164 → 1.520 counts, **+31%**), a CGA
  full repaint **+16.4%** and a CGA play frame **+7.5%** (with the sprite layer's
  share of it, below), and it changes the picture of **32 of the 36
  alphanumerics**.

  **What it costs the picture is stated with its numbers, because the merge
  only ever adds ink.** `HIGH` reads as `HIGH` where the sampled arm
  photographed `IIIGII` — and `SCORE` now reads `8GORE`, `CHARACTER` reads
  `GHARAGTER` (`build/port-shots/wave4k-cga-text-zoom.png`): a merged `C` grows on its third row exactly the spur that makes
  a `G` a `G`, so merged `C` and merged `G` are identical on their first and
  third rows and differ only on the second and the last. And the separation it
  costs is worst between DIGITS, which is the one text in this game whose
  characters change. Counting differing lit pixels over the 36 alphanumerics:

  | | closest pair, all 36 | closest DIGIT pair |
  |---|---|---|
  | the reference's own 8-row font | 4 px | 13 px (`6`/`8`) |
  | CGA SAMPLED | 2 px (`B`/`D`, `M`/`N`) | **5 px** (`0`/`6`, `0`/`9`) |
  | CGA **MERGED — what ships** | **1 px** (`5`/`S`, `6`/`S`) | **2 px** (`5`/`6`) |

  Merged `S` is 1 pixel from `5` and 1 from `6` (sampled: 6 and 4), and merged
  `5` is 2 from `6` where the sampled pair was 8 apart. On
  `build/port-shots/wave4k-cga-legend-zoom.png` the attract screen's
  `50 PTS` legend reads `60 PTS`, and on `wave4k-cga-intro.png` `PRESS` reads
  `PRE88` and `CLYDE` reads `GLYDE`.

  **The same merge is on the SPRITE layer**, where the alternate-row sample was
  taking the top and bottom caps off Pac-Man's circle and thinning every
  ghost's fringe — his widest sprite read as a flat-topped blob — and where the
  union of the pair keeps the cap and still leaves the mouth open, the mouth
  being a wedge of transparency several rows deep. The partner is always the
  next row down the sprite (flipy walks the rows backwards, so its pair is the
  same pair), source row 15 has no partner and is split off and asked for
  unmerged, and it costs a sprite row **+20%** (2.422 → 2.891 counts) plus one
  test a source byte — **+3.9%** — on every adapter that does NOT merge, which
  is 0.9% of a VGA play frame. A whole CGA play frame is **+7.5%** with both
  merges in, against the 25% bound; the full repaint, which draws no sprite at
  all, is +16.4% — the tile merge alone.

  **Neither arm satisfies "the text reads", and the merge is kept as the lesser
  defect rather than as a pass**: it is better on LETTERS, which is nearly all
  of the text this game draws, and worse on DIGITS, which is the score. The
  sampled arm is still there — the routine's `zmask` argument is a pointer and
  0 asks for it — still under test, still on the bench, and now priced on a
  whole FRAME by `pmcuitest`'s `drive_cga_arms`, which drives the same CGA
  round on both arms and audits every frame of each, so the look question can
  be re-opened with numbers as well as with the three photographs.
- **The hiscore lives for the instance.** The reference has no file I/O of any
  kind — no hiscore file, no config, no save — and neither does this. A
  `SYSTEM/APPDATA` record (§19.9) is a follow-up, not a port item.
- **Five actors render in four colours, and one of them shows no eyes.**
  `tools/paccman_assets.py` maps each arcade colour to the nearest of the OS's
  sixteen (§39) by Euclidean RGB distance, and three pairs land on the same
  one: **Pinky's body and eye-whites** are both white, so the ghost is a white
  blob with two floating blue pupils; **Clyde and Pac-Man** are both the same
  yellow; and **Pinky and a blinking frightened ghost** are both white, so a
  ghost about to stop being edible reads like a normal one. Blinky is red and
  Inky cyan. The metric is not at fault — Pinky's arcade colour is
  (255, 184, 222), which is **6,130** from white against **10,890** from light
  magenta, and light magenta is further by every distance. (No arcade colour
  can have blue 255: the hardware palette is 3-3-2 and blue gets two bits,
  `0x47 + 0x97` = 222.) The only fix is an explicit per-block override, which
  is a trade against the arcade look and is recorded in SPEC.md §91 as a
  decision rather than taken here — and for Clyde it is a **legibility** trade
  and not a distance one: brown `0x06` is 22,067 away where the yellow it
  would replace is 5,237.

## Layout, per adapter

The content is the arcade field: 224 pixels wide, and 288 or 144 rows deep.
The kernel's own arithmetic makes that a **226 × 307** frame (or 226 × 163),
because the content is the frame less one border pixel each side and less
`TITLE_H + 1`.

| adapter | field | band | frame | rows over the dock |
|---|---|---|---|---|
| VGA 640×480 | 224×288, every source row | 8 rows | 307 | none |
| EGA 640×350 | 224×288, every source row | 8 rows | 307 | 1 |
| Hercules 720×348 | 224×288, every source row | 8 rows | 307 | 3 |
| CGA 640×200 | 224×144, alternate source rows | 4 rows | 163 | 7 |

All three overlaps are inside §11.93's `DOCK_H/2` line, which is what
`WF_KEEPH` is for: the window hangs over the dock strip by those few rows
rather than being **shortened**, which would cost the bottom tile row on three
adapters of four.

## How a frame is drawn

A frame's damage is the set of tiles a `vid_*` write actually changed —
**compare-then-write: a write that changes nothing marks nothing** — coalesced
into **two** column spans per 8-row band. Each dirty span is composed in a
960-byte scratch by `pmcband.inc`'s assembly loops and sent with **one** blit:

- `OSAPI_GFX_BLITP` (four bitplanes) on a colour display, when nothing covers
  the window and the probe says the rect would be taken;
- `OSAPI_GFX_BLIT1` on a monochrome one;
- `OSAPI_GFX_BLIT4` when either refuses — a covering window, a straddle, a
  `kern_small` kernel. The composer's own format *is* `blit4`'s, so the
  fallback needs no conversion.

There is no persistent canvas. The dirty span is exactly the tile set that
would have to be recomposed into one anyway, so the band scratch does the same
work in 3% of the memory that `apps/pacman`'s 28 KB canvas takes.

**A full repaint is 36 blits plus the probe, and that is 2.51 seconds on the
target XT** — 36 × the 69.78 ms a colour band costs in the table below (mono
1.44 s; CGA's half-height field 1.09 s). The call count is not the cost here;
the composition is, which is why the number is stated in milliseconds and not
only in calls.

**So a repaint asks what it owes.** The window sets `WF_OWNBG`, which is the
precondition §11.90.2 puts on a partial answer, and `os88_paint` calls
`OSAPI_WM_DAMAGE` and recomposes only the bands inside the rect it gets back —
two shifts turn the rect into tile columns and band rows, and the marks union
with whatever play has already dirtied. A menu dropped over the top three tile
rows owes **3 bands = 209 ms** instead of 2,512, and an empty rect draws
nothing at all. A menu close, a drag of another window across a corner and a
toast going away all arrive this way.

**The black border is the letterbox, not the content.** `WF_OWNBG` also means
the kernel no longer whitens the content, so the margin around the 224-wide
field in a wider window is this program's to paint — but at the default size
there is no margin, because the content box is exactly the field and
`os88_wm_minsize` pins it there. Filling the whole content instead wrote
64,512 pixels that a band covered a moment later. Up to four strip fills are
computed from the two insets and each is skipped when empty: **0 fills** at the
shipped size, four on a window the user has grown.

## The measurements

### The band composer — `make pmcbandbench`

`tests/pmcband/pmcbandbench.asm` `%include`s the **shipping**
`apps/paccman/pmcband.inc` and times it. Taken under
`qemu-system-i386 -icount shift=3` and converted at PERFORMANCE.md Part 4's
**one count = 0.359 ms of real XT**. These numbers are the only source of any
microsecond in §91, in this file, or in the harness's cost table.

| row | N | counts/op | real XT |
|---|---|---|---|
| `TILE` step 1 — one 8×8 tile, 8 rows | 256 | 1.949 | **0.70 ms** |
| `TILE` step 2 — the CGA layout SAMPLED, 4 rows | 256 | 1.164 | 0.42 ms |
| `TILE` step 2 **MERGED** — the shipping CGA arm | 256 | 1.520 | **0.55 ms** |
| `PACK_PL` — one 8-row band → four bitplanes | 8 | 116.250 | **41.73 ms** |
| `PACK_1` — one 8-row band → 1bpp | 8 | 50.375 | 18.08 ms |
| `SPRITE` 16×8, even nibble, SAMPLED | 8 | 19.375 | 6.96 ms |
| `SPRITE` 16×8, odd nibble + flipx, SAMPLED | 8 | 20.375 | 7.31 ms |
| `SPRITE` 16×8, even nibble, **MERGED** | 8 | 23.125 | **8.30 ms** |
| `SPRITE` 16×8, odd + flipx, **MERGED** | 8 | 24.375 | **8.75 ms** |
| `BLITP` 224×8, four planes | 8 | 20.500 | **7.36 ms** |
| `BLIT4` 224×8, packed | 8 | 134.625 | **48.33 ms** |
| `BLIT1` 224×8, 1bpp | 8 | 3.375 | 1.21 ms |
| `BAND colour` — 28 tiles + pack + `BLITP` | 8 | 193.875 | 69.60 ms |
| `BAND mono` — 28 tiles + pack + `BLIT1` | 8 | 110.750 | 39.76 ms |
| `BAND cga` 4 rows, SAMPLED | 8 | 62.375 | 22.39 ms |
| `BAND cga` 4 rows, **MERGED** | 8 | 72.875 | **26.16 ms** |
| `28 × (MERGED − sampled)` — `pb_recon`'s own check | — | 9.97 | 3.58 ms |
| `BAND cga MERGED − sampled` — the same quantity | — | 10.50 | 3.77 ms |

**Read the second run.** The first run of a session prices `BLIT4` about 10%
high (148.0 counts against 134.75) and every other row within one count; runs
2 and 3 agree within a quarter of a count on the other thirteen.

**The three `TILE` rows run at `PB_N_TILE` = 256 and the rest at `PB_N` = 8.**
A tile is about two PIT counts, so at eight iterations a per-tile figure lands
on a 0.125-count grid — the size of the whole difference the row merge makes.
Measured that way the merge cost 0.125 counts a tile while the `BAND cga` A/B
over the same 28 tiles said 10.5, a 3× disagreement between two rows whose
packer and blit cancel. At N = 256 they agree, and the bench's last two lines
(`pb_recon`) print that reconciliation every run: 28 × 0.371 = **10.4 counts**
against a band A/B of 9.875–10.25. If those two lines disagree, nothing above
them is a number to quote.

**The lever works and the repack eats it.** `OSAPI_GFX_BLITP` puts a 224×8
band down in **7.36 ms** where `GFX_BLIT4` takes **48.38** — 6.6×, and that is
the whole of the "maybe more performant on XTs" premise. But turning the
packed band into four planes costs **41.78 ms**, so a whole colour band is
69.60 ms against the ~68.5 ms it would cost to compose and send the same band
through `BLIT4`: **a wash**, which is the outcome
`docs/PACCMAN-PORT-PLAN.md` named as its first risk. Composing straight into
planar on the colour path — the answer that risk carried with it — is a
recorded follow-up and is what the fps below would move.

### A frame, and the worker's stack

Measured by `apps/paccman/hosttest/pmcuitest.c` on VGA at the shipped size,
against a whole repaint of **36 bands, 1,008 tiles, 2,476.1 ms**, and now with
**all six** terms in it — both sprite-row prices and the game logic included:

| | calls | bands | tiles | sprite rows | game ticks | ms |
|---|---|---|---|---|---|---|
| a play frame | 23 | 18 | 46 | 80 | 3 | **257.5** |
| the frame a dot goes in | 23 | 18 | 57 | 80 | 3 | 284.2 |
| worst of 24 consecutive | — | — | — | — | — | **294.7** |
| a whole repaint, About card up | 53 | 52 | 592 | 0 | 0 | **1,479.7** |
| the About card dismissed | 25 | 20 | 560 | 0 | 0 | 1,379.2 |
| a full repaint, letterboxed | 41 | 36 | 1,008 | 0 | 0 | 2,594.7 |
| one tile changed | 2 | 1 | 1 | 0 | 0 | 4.1 |
| nothing written at all | 0 | 0 | 0 | 0 | 0 | **0.0** |

Dismissing the About card marks only the bands the card covered — 20 of 36,
including two bands of slack each side of the widget's own measurement — where
re-marking the whole field would be a full repaint, from an ordinary
keystroke. **And a whole `W_PAINT` taken WHILE the card is up marks the
COMPLEMENT of it** rather than everything: the card is 216 px of the 224-px
field, so the bands it crosses keep only the columns hanging out either side —
592 tiles against 1,008 on VGA and **176 against 1,008 on CGA** — and what was
drawn under it before was drawn and then immediately covered.
`tests/unit/t_paccman.py` pins the three constants that measurement mirrors
against `apps/os88ui.inc`'s own.

Wave 3's own table read **131.2 ms** for that play frame with the sprite and
logic terms still zero, and its closing line said so by name; the two terms it
was missing are 125 ms of it.

**A tile has three prices and the model uses the right one.** Every row above
is VGA, where a tile is `PMC_T_TILE`; on CGA a tile is composed at rowstep 2
and costs `PMC_T_TILE2M`, 546 µs against 700 — and a CGA sprite row costs
`PMC_T_SPRROWM`, 1,066 µs against 892. Pricing every tile at the
step-1 term made the CGA rows ~60% high and — the real defect — made the row
merge invisible to the cost model, since sampled and merged then priced the
same. `pmc_draw_band` counts step-2 tiles separately and `drive_cga_arms`
prices a CGA repaint and a CGA play frame on both arms:

| CGA, the same round | full repaint | worst play frame |
|---|---|---|
| sampled | 784.4 ms | 160.6 ms |
| **MERGED — what ships** | **913.5 ms** | **172.6 ms** |
| the merge costs | **+16.4%** | **+7.5%** |

The repaint draws no sprite, so its column is the TILE merge alone; a play
frame is 46 tiles and 80 sprite rows, so most of its 7.5% is the sprite half.

**And the worker's own lock hold is timed, not only counted.** `os88_worker`
brackets the whole of `pmc_frame`, so the first chunk of a flush is the game
logic — up to `PMC_CATCHUP_MAX` OS ticks of it — plus `PMC_HOLD_BANDS` bands:
`pmcuitest`'s `worst hold, one chunk` reads **387.4 ms** on a late frame (six
game ticks and four full-width VGA bands) and fails the build over 460.

`tests/paccman.py` measures the worker's own stack slice on MartyPC: **188,
190 and 188 of 256** on the three profiles, against the 208 the row asserts and the
`OS88_STACK_256` the package declares. (Wave 2 read 162–164 and 170; wave 3's
`pmc_step_tick` put one more call level on the tick path and took it to 178;
wave 4's TILE merge added no call level and no local and the mark did not
move, but its review's SPRITE merge did — `pmc_band_sprites` is on the
worker's deepest chain and SmallerC gives every declared local its own slot,
so six new ones read **196**. Four of them were written out again, which is
what 190 is; the bar leaves 18.)

### The hypothesis, answered

`tests/paccman.py` runs **one** bracket — the same code, the same 4.77 MHz
cycle counter, the same four kernel API slots counted by exec breakpoint on
the table entries themselves — over this port's frame proc and over
`PACMAN.O88`'s, on the same MartyPC profile, and reads each one's effective
game speed over the very frames it timed:

| profile | | fps | ms/frame | gfx calls | speed |
|---|---|---|---|---|---|
| `os8088_xt_vga` | PaccMan (C) | 2.18 | 459.8 | 16.7 | 24% |
| | PACMAN (asm) | **4.14** | 241.6 | 9.0 | 23% |
| `os8088_5150_cga_gla` | PaccMan (C) | 2.94 | 340.0 | 13.3 | 32% |
| | PACMAN (asm) | **18.21** | 54.9 | 7.0 | 100% |
| `os8088_5150_herc_gla` | PaccMan (C) | 2.62 | 381.9 | 14.3 | 29% |
| | PACMAN (asm) | **16.71** | 59.9 | 8.0 | 92% |

The fps column reproduces to about ±2% between runs and the call column to
about ±1, because which sixteen frames of which round the bracket lands on is
not fixed. The verdict is a factor of two away from that band on VGA and six
on the two 1bpp adapters.

**Both ports are bracketed on a DRAWN frame**, which took a correction: this
port's `pmc_frame` has one exit and always draws, while §89's `pm_frame`
returns without drawing on five guards and its worker sleeps to an 18.2 Hz
deadline — so counting its entries counted the scheduler. The bracket is
`pm_step`, which §89 reaches only on the path that redraws, and the row waits
for `PM_PLAY` first so that both ports are timed in a round rather than in a
hold.

**The answer is no.** "Maybe this port is more performant on XTs" does not
hold on any of the three, and the row prints that sentence either way rather
than gating on it. `GFX_BLITP` is the 6.6× lever the bench measured and it is
not enough: the repack that feeds it costs 41.78 ms a band against the blit's
7.36, one `game_tick` is **18.6 ms** and a frame carries three, and this port
draws the arcade's 28×36 field where §89's draws Roklan's 40×22. The two are
not the same picture, so the fps column is not a like-for-like race between C
and assembly — it is the answer to the question that was asked, on the machine
it was asked about.

**The one column PaccMan holds is `speed`, and only on VGA** — 24% against
23%, at a ninth of the frame rate — which is `PMC_CATCHUP_MAX` = 2 doing what
it is there for: at ~2.2 fps the game still advances 24% of arcade time,
because a slow frame carries two OS ticks of game rather than one. On both
1bpp adapters §89 is AT its worker's 18.2 Hz deadline and asleep (100% and
92%), and no catch-up scheme beats a port that has already finished.

### Size

`os88pkg: 'PACCMAN' entry=+0x0060 image=42050 bss=5230 icon=yes assoc=0` —
**47,280** of the 61,440 `APP_MAX_SIZE` allows, with the whole program in it.
About 17 KB of the image is the arcade tables, which do not grow. **§73.14's
split trigger is 55,000 resident bytes — image *plus* bss — and this line is
7,720 away from it**; `pmc_intro.c` is the first `ovl_*` candidate, being
once-per-attract code a keystroke never touches.

That line is re-pasted from the build each wave and never typed — it is the
number §73.14's overlay trigger is read off, and it appears here and in
SPEC.md §91, which must agree word for word. Wave 1 was `image=21844
bss=4498`, 26,342 of 61,440; wave 2 `image=37332 bss=5188`, 42,520; wave 3
`image=40848 bss=5222`, 46,070. Wave 4's 1,210 bytes are the CGA row merge (256
of `pmc_zmask`, the merged loops in `_pmc_tile` and `_pmc_sprite` with the
latter's split call), `pmc_ab_box` and `pmc_dirty_not_card`.

## The disks, and the period machine

`make paccmandisk` builds the floppy in **all four geometries** —
`build/paccman.img` (1.44MB), `paccman720.img`, `paccman120.img` (1.2MB 5.25"
HD) and `paccman360.img` — each `os88disk.py --verify`ed in its own recipe. The
package and this README sit at the root of each: there is no `.OVL`, so there
is nothing a folder would keep together. The 360KB disk uses 77 of its 354
clusters.

`make allapps` puts a `PACCMAN/` folder on `build/apps-all.img` beside
`GAMES/PACMAN.O88`, and `make live` carries the same payload onto the live
USB image and CD by derivation.

`make xt-paccman` boots **`vm/xt-paccman`** — an 86Box `ibmxt86`, an 8088 at
4.77 MHz with 640KB and an OTI-067 VGA, the 360KB system floppy in A: and
`build/paccman720.img` in B:. `make 386-paccman` boots **`vm/386-paccman`**,
`vm/386-c-word`'s 386DX/25 with `build/paccman.img` in B: — the machine to
play it on at the arcade's own speed; the XT is the one to measure on. It is `vm/xt-word`'s machine with `fdd_02_fn`
and the uuid changed and nothing else, which is deliberate: 86Box does not
reject an unrecognised key, it substitutes a default and rewrites the config
on the way out. It cannot **assert** anything — `tests/paccman.py` on MartyPC
does that — it is where a human watches the reveal, stopwatches its 630 game
ticks, and judges whether `PMC_CATCHUP_MAX` = 2 feels like Pac-Man. It has to
be a human: the profile's mouse is `msserial`, so 86Box captures the host
pointer before a click reaches the guest, and nothing in this tree drives an
86Box.

## The checks

`apps/paccman/build.sh` runs three things before anything is built for the
8086, and each stops the build:

- **`tools/paccman_assets.py --check`** — the committed `pmc_rom.c` against
  what the extractor writes now, when a reference checkout is present.
- **`hosttest/pmcuitest.c`** — the whole translation unit under clang against a
  stub `os88.h`, with a **pixel model of the glass**. It drives five layouts
  and all three blit paths, and after every frame rebuilds what the screen
  ought to show from an **independent recomposition** of `video_ram` and
  `color_ram`: 0 differing pixels or FAIL. A dirty span the program forgot to
  mark fails here rather than becoming a stale tile nobody notices. It also
  prints the cost table above, and writes the vectors the next check uses.
- **`hosttest/pmcbandtest.sh`** — `pmcband.inc`'s shipping routines on a **real
  x86 with SS ≠ DS** and an ES sentinel, in raw QEMU, against those vectors.
  The vectors come from C twins written a *different* way — through `pmc_pal`,
  the bit values and `pmc_mono`, where the assembly goes through `pmc_pairs`,
  `pmc_planar` and `pmc_mono2` — so a byte agreeing is two implementations
  agreeing, and the three generated lookup tables are under test with the
  loops. Three negative controls must FAIL: a routine that leaves ES loaded,
  one that returns with DF set, and a deliberately flipped bit in an expected
  vector.

Beyond those, `tests/unit/t_paccman.py` is a fast-tier row (the maze's 244
dots, the door tiles, the table sizes, the pin, the prelude melody's first
notes, and that `pmcband.inc` and `paccman.c` agree about the three band
constants); `tests/unit/t_ctoolchain.py` builds this package in the **full**
tier — deleting `build/.paccman-hostchecks` along with the `.o88`, which is
what makes the three checks above run there rather than only when somebody
types `make paccman` (the row went 7.7 s → 10.1 s for it, and the tier
measured 500.8 s, 510.7 s and 502.8 s of its 600 s budget with paccman in it); and `tests/paccman.py` is the soak-tier MartyPC row —
the attract screen, a real key press through `int 09h`, autonomous play, the
speaker, the adapter's layout, two scoring **fixtures** written into bss by
symbol (a frightened ghost must score exactly 200 and become eyes; the bonus
fruit exactly 100), the image unmodified outside its writable statics, the
stack water mark, and the measurement above with its verdict line.
