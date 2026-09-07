# PaccMan

A native reimplementation, in C, of Andre Weissflog's **`pacman.c`** — the
arcade-faithful C99 Pac-Man at <https://github.com/floooh/pacman.c> — for
os8088. Package `PACCMAN`, product name **PaccMan**. `SPEC.md` §91 is the
contract and `docs/PACCMAN-PORT-PLAN.md` the design record.

> **What is in the build on this disk.** It opens on the ATTRACT SCREEN and it
> PLAYS, in colour or on either 1bpp adapter, with sound: the reveal, the
> round, the keys, the movement rules, the four ghosts and their dot counters,
> the score and the reserve strip, the prelude and the siren, and all four
> Game menu items act. What is still to come is polish — the About card's
> final wording, the application floppy's place on `apps-all.img`, the period
> 86Box machine and the measured side-by-side against `PACMAN.O88` — so a
> number quoted below as *measured* is measured and the comparison table is
> the one thing not filled in yet. This paragraph is deleted by the wave that
> fills it.

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
- **And on a SHORT display the font loses its middle stroke.** CGA's window is
  163 rows, so the composer samples source rows 0, 2, 4 and 6 of every 8 at a
  fixed parity (which is what keeps the picture stable across a repaint) — and
  **row 3 is where the arcade font keeps every horizontal middle stroke**. The
  leading `-` of `-SHADOW`, `-SPEEDY`, `-BASHFUL` and `-POKEY` is a single run
  on that row and goes; so do the strokes of **B, E, F, G, H, S, 3, 6 and 9**.
  `B` then reads as `O`, `E` and `G` as `C`, `F` as a corner and `H` as two
  bars, so on CGA `CHARACTER / NICKNAME` reads `CIIARACTCR / NICKNAMC`,
  `-SHADOW BLINKY` reads `SIIADOW OLINKY` and `PLAYER ONE` reads `PLAYCR ONC`.
  Every other glyph is carried whole. It is stated rather than repaired because
  a tile pixel is a 2-bit colour *index* and not a bit: OR-ing the dropped row
  in would invent a colour in the 89 tiles of 256 that use more than one ink,
  and the sound per-pixel form costs the CGA tile composer about eight
  instructions a source byte, thickens the maze's own strokes and re-dates the
  band gate's fixtures — arithmetic for a later wave, not a doc fix. SPEC.md §91
  has it in full.
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

| row | counts/op | real XT |
|---|---|---|
| `TILE` step 1 — one 8×8 tile, 8 rows | 2.000 | **0.72 ms** |
| `TILE` step 2 — the CGA layout, 4 rows | 1.250 | 0.45 ms |
| `PACK_PL` — one 8-row band → four planes | 116.375 | **41.78 ms** |
| `PACK_1` — one 8-row band → 1bpp | 50.500 | 18.13 ms |
| `BLITP` 224×8, four planes | 20.500 | **7.36 ms** |
| `BLIT4` 224×8, packed | 134.625 | **48.33 ms** |
| `BLIT1` 224×8, 1bpp | 3.375 | 1.21 ms |
| `BAND colour` — 28 tiles + pack + BLITP | 194.375 | 69.78 ms |
| `BAND mono` — 28 tiles + pack + BLIT1 | 111.250 | 39.94 ms |

**The lever works and the repack eats it.** `OSAPI_GFX_BLITP` puts a 224×8
band down in **7.36 ms** where `GFX_BLIT4` takes **48.33** — 6.6×, and that is
the whole of the "maybe more performant on XTs" premise. But turning the
packed band into four planes costs **41.78 ms**, so a whole colour band is
69.78 ms against the 68.4 ms it would cost to compose and send the same band
through `BLIT4`: **a wash.**

That is the outcome `docs/PACCMAN-PORT-PLAN.md` named as its first risk, with
the answer already decided: *"If the bench shows the repack dominating, sprites
compose straight into planar on the colour path and packed stays for the 1bpp
adapters only."* **That is still owed** — wave 2's subject was the game, and
the arithmetic above is the case for taking it. **No fps figure is claimed
here** — wave 4 measures `PACCMAN.O88` beside `PACMAN.O88` on the same MartyPC
profile and prints the verdict either way.

### A frame, and the worker's stack

Measured by `apps/paccman/hosttest/pmcuitest.c` on VGA at the shipped size,
against a whole repaint of **36 bands, 1,008 tiles, 2,496 ms**:

| | calls | bands | tiles | ms |
|---|---|---|---|---|
| a play frame | 23 | 18 | 46 | **131.2** |
| the frame a dot goes in | 19 | 15 | 46 | 128.0 |
| worst of 24 consecutive | — | — | — | **144.3** |
| a menu dismissed over three tile rows | 4 | 3 | 84 | 208.7 |
| the About card dismissed | 21 | 20 | 560 | **1,386.8** |
| one tile changed | 2 | 1 | 1 | 4.1 |
| nothing written at all | 0 | 0 | 0 | **0.0** |

Dismissing the About card marks only the bands the card covered — 20 of 36,
including a band of slack each side of the widget's own measurement — where
re-marking the whole field would be the 2,496 ms of a full repaint, from an
ordinary keystroke. `tests/unit/t_paccman.py` pins the two constants that
measurement mirrors against `apps/os88ui.inc`'s own.

**Those milliseconds understate the frame**: the sprite composer's term and
the game logic's are still zero in the table, and the harness's closing line
says so by name rather than letting a plausible number stand.

`tests/paccman.py` measures the worker's own stack slice on MartyPC: **178 of
256 on an XT with VGA and 178 on a 5150 with CGA**, against the 208 the row
asserts and the `OS88_STACK_256` the package declares. (Wave 2 read 162–164 and
170; wave 3's `pmc_step_tick` put one more call level on the tick path.)

### Size

`os88pkg: 'PACCMAN' entry=+0x0060 image=40848 bss=5222 icon=yes assoc=0` —
**46,070** of the 61,440 `APP_MAX_SIZE` allows, with the whole program in it.
About 17 KB of the image is the arcade tables, which do not grow. **§73.14's
split trigger is 55,000 resident bytes — image *plus* bss — and this line is
8,930 away from it**; `pmc_intro.c` is the first `ovl_*` candidate, being
once-per-attract code a keystroke never touches.

That line is re-pasted from the build each wave and never typed — it is the
number §73.14's overlay trigger is read off, and it appears here and in
SPEC.md §91, which must agree word for word. Wave 1 was `image=21844
bss=4498`, 26,342 of 61,440; wave 2 `image=37332 bss=5188`, 42,520.

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
constants), and `tests/paccman.py` is the soak-tier MartyPC row.
