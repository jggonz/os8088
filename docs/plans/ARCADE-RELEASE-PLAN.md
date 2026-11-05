# ARCADE to a release - the plan

**Status: AGREED** (2026-11-05; the owner's answers are section 7). ARCADE today is `docs/ARCADE-SPEC.md`;
how it got here is `docs/HOW-ARCADE-WAS-BUILT.md` and the build plan
`docs/plans/ARCADE-EMU-PLAN.md`. This document is the work between that and
a release, and the decisions it needs from the owner (section 7).

---

## 1. What stands in the way

ARCADE plays seven boards: five 8080 boards, Q\*bert's native 8088 board and
Pac-Man. It cannot ship as it is, for one reason that outweighs all the
others: **the package carries the games.** `make arcade` fetches each ROM,
translates it, and puts the ROM, the translation and the tables derived from
it into `ARCADE.O88`'s parts. That is why the package is a `local` line in
`apps/RETIRED.txt` and rides no standard image. A released ARCADE has to be
a package with **no ROM byte and nothing derived from one**, plus a way for
the user to give it their own ROM sets on the machine itself.

The other gaps are about being good rather than being allowed:
- The game is picked from a drop-down menu.
- The window is a plain text card.
- Nothing about a game is kept: settings, high scores, keys.
- Q\*bert is silent.

## 2. ROM sets the user provides

### 2.1 What ships, and what the user brings

| ships in ARCADE.O88 | comes from the user's ROM set, once, on their machine |
|---|---|
| the host, the 8080 and Z80 runtimes, the N88 core, the renderers, the sounds `arcsfx.py` synthesises | the program bytes |
| per game, a **RECIPE**: the translated code with every byte that came from the ROM left as a hole, a FILL LIST (code offset <- ROM address, length), and the PC table as a list of (PC, offset) pairs | the bytes that fill the holes |
| per game, a **SET DESCRIPTOR**: the MAME set's name, each chip's file name, size, CRC32 and where it maps | the chips themselves, checked against it |
| `qbert.patch`'s lines (already addresses only) | the program they are applied to |
| the turning rules for tiles and sprites (code, not data) | the tiles and sprites they turn |

**Why a recipe and not the translation.** A translated code part embeds the
ROM's constants: every `MVI A,n`, every `LXI H,table` and every immediate
compare is a ROM byte in the 8086 code. Shipping it would be shipping part
of the ROM. The recipe keeps those bytes out and keeps the project's
existing posture: the `.seeds` files and `qbert.patch` already hold
ADDRESSES only and never a ROM byte (ARCADE-SPEC 3.2, 10.2). The fill list
is the same kind of thing.

The translator gains one mode for this: `arcaot.py --recipe` (and
`arcz80t.py`) records, as it emits, which output bytes came from which ROM
address. **The test is byte-identity:** a recipe filled from the pinned ROM
must equal today's code part exactly, for every board. That is a host-only
row, so the recipe can never drift from the translation it stands for.

### 2.2 Import, on the guest

**Game > Add ROM sets...** opens the system file dialog (`OSAPI_DRV_DLG`,
`FDLG_OPEN`) on a MAME `.zip`, or on any file of a set in a folder of loose
chips.

1. **Read the set.** ZIP's central directory gives each member's name, size
   and CRC32. A member is STORED or DEFLATEd. The inflater already exists in
   8086 in PIXEL (`apps/pixel/pxpng.asm`). It moves to a shared
   `apps/os88inflate.inc` that both include: shared SOURCE, never a copy
   (CLAUDE.md, *Before designing anything*).
2. **Check it.** CRC32 of every chip as it is inflated, against the set
   descriptor. A set may name known-good ALTERNATES: Pac-Man's `puckman` and
   `pacman`, Q\*bert's `qberta`, when their translation is the same or has a
   recipe of its own. Anything else is refused, naming the chip and the
   CRC32 it wanted. This is `tools/getarcrom.py`'s rule, on the machine.
3. **Keep the CHIPS, and nothing else, WHERE THE ROMS ARE.** The verified
   chips of a set go into one file, `<SET>.ROM` (the board image in the
   layout `getarcrom.py` writes today, plus Q\*bert's two sound chips). It
   goes in the folder the set was imported FROM, or a folder the user picks
   in the save dialog, which defaults there. The reason is the constrained
   machine: on the emulators' disk only the program fits (section 7), so a
   360KB user keeps ROMs and their `.ROM` files on a disk of their own. On a
   286 with a hard disk the ROMs and the program will usually sit in the
   same folder anyway, and then this rule changes nothing. That
   one file is the whole cache: everything else is REBUILT each time the
   game is picked, from the recipe in the package (section 2.4).
4. **Find them by looking, in known places.** A game with no `.ROM` found is
   "ROM needed", never an error. The places, each listed by its directory
   and each `.ROM`'s header read (the set's name, the CRC32s it was checked
   against, the snapshot):
   - the folder `ARCADE.O88` was started from;
   - `\ARCADE\` on every floppy and hard disk the frontend can see, read
     when it opens and again on **Look for ROMs** (a floppy is not scanned
     behind the user's back: each read is a disk call, about 400 ms);
   - every folder an import wrote to, remembered in `SYSTEM\APPDATA` on
     ARCADE's OWN disk (`os88data.inc`, SPEC.md 19.9): it is the program's
     information, so it lives with the program, not with the boot disk.

   A remembered place on a floppy that is not in the drive is a normal
   case: picking its game asks for the disk by its label and drive ("Insert
   the disk with QBERT.ROM in B:"), the way any floppy-era program did.

One zip can hold several games (`invaders.zip` carries ~40 clone folders);
import takes every set it recognises. Import is one-time and may take
minutes on an 8088, so it shows progress per chip. It runs on the desktop,
not in the full-screen bracket, so a refusal is a toast.

**Memory.** Inflating needs its 32KB window plus a game's largest chip.
A pick rebuilds into the claims the board's lazy parts use today. Both fit
on 640KB. kern_small refuses, as `op_load` already does.

### 2.4 What a pick rebuilds, and what it costs

Filling the recipe's holes is a copy. The PC table is expanded from its
pairs. Q\*bert's patch is 17 checked stores. The derived tables are loops
over the chips:
- Pac-Man's turned tiles, sprites and plane selectors (`arcpmgfx.py`'s);
- Q\*bert's turned tiles and sprites (`arcqbgfx.py`'s), written once in
  8086;
- the Taito boards' colour maps, which are the chips themselves.

The heaviest is Q\*bert's 256 sprites, about 65,000 pixels. PREDICTED (not
measured): a second or two on a 4.77 MHz 8088, under a tenth of that on a
286. R2 measures it on the 5150. If a pick is too slow there, the derived
tables can be kept beside the `.ROM` as an option; they are not the default.

### 2.5 What it weighs on a disk

MEASURED today (`build/arcade.o88`, 261KB, its parts' sizes as packed):

| game | the package's share now | of which ROM or derived from it | the user's `.ROM` file |
|---|---|---|---|
| Space Invaders | 20.5KB | 5.6KB | 8KB |
| Space Invaders Part II | 35.7KB | 9.3KB | 12KB |
| Lunar Rescue | 40.9KB | 10.3KB | 13KB |
| Gun Fight | 11.4KB | 3.4KB | 4KB |
| Sea Wolf | 15.1KB | 3.6KB | 4KB |
| Q\*bert | 25.9KB | 25.8KB | 68KB (with the sound chips) |
| Pac-Man | 63.8KB | 24.5KB | 25KB |

Taking the ROM-derived parts out leaves a package of about **175KB**
(PREDICTED: R1 measures it, and the recipes' PC pairs may weigh a little
more or less than today's packed tables). The seven sets' files are
**134KB** together. **ARCADE and every game's ROMs fit on one 360KB disk**
(354KB usable), with room to spare. The user's zips need not stay on it:
`invaders.zip` alone is 220KB, almost all of it clones.

The chips are kept as they are, not compressed: the machine has an
inflater but no compressor, and the disk does not need one.


**Where the user gets ROMs** is said plainly in the README: dumps of boards
they own, or a licensed collection. Each set is named exactly as MAME names
it, with its chips' CRC32s. Nothing in the tree, the disk or the README
points at a download.

### 2.3 For the builder

`make arcade` builds the ROM-free package and the recipes. `make arcaderoms
GETARCFROM=<dir>` writes the `.ROM` files on the host from the same ROMs,
for the test rows and for an owner who would rather not import on the
guest.
The fetch at pinned hashes stays as the developer's route. It never touches
what ships.

## 3. A game browser instead of a menu

A list in a menu says nothing about the game, and seven games will become
more. The window becomes the **frontend**, the way MAME's and every
cabinet's is: pick by looking.

| part | what it does |
|---|---|
| **the list** | every game ARCADE knows: title, maker, year, each with its state - READY, ROM NEEDED, NEEDS VGA, NEEDS A 286 FOR FULL SPEED (`OSAPI_CPU_INFO` and `OSAPI_FSX_CAPS`, a fact rather than a guess, PERFORMANCE.md rule 7) |
| **the screen** | the selected game's picture. **Taken from the game itself**: the first time it is played, ARCADE keeps a frame of its attract mode, scaled down (about 2KB), at the end of the game's `.ROM` file. No artwork is shipped, so no artwork can be someone else's. Before that it shows the game's name |
| **the card** | the game's controls, players, and its settings button |
| **the buttons** | PLAY, ADD ROM SETS, SETTINGS |
| **keys** | Up and Down move, Enter plays, type-ahead jumps by name, Esc closes. The mouse does the same |

The Game menu stays for the same commands, plus Reset and About, so the
menu bar is what os8088 users expect.

## 4. Looking like an arcade frontend

This is the `retro-face` skill's ground (`.claude/skills/retro-face/`,
CDRack, SPEC.md 119.6): an app whose UI is a styled piece of hardware.

- **The cabinet:** a marquee strip across the top with the ARCADE name,
  backlit. The game's screen in a CRT bezel. A coin-door panel at the
  bottom: an LED-segment CREDITS counter, the coin slot (Add ROM sets) and
  button caps (Play, Settings). Host-generated art and fonts (the
  seven-segment digits, the small caps, the marquee), kept as SOURCE with
  their prompts and turned by a deterministic converter (CLAUDE.md,
  *Building an app or game in waves* rule 5).
- **Role tables per adapter:** a colour face on VGA and EGA, and a black and
  white one on Hercules and CGA that is designed rather than greyed (SPEC.md
  39.4, 47). Full, mid and compact layouts picked from the content box.
- **Composed, not painted:** each element is drawn into one band and put
  down with one `OSAPI_GFX_BLIT1`, with cached keys so only what changed is
  redrawn. PERFORMANCE.md rule 1 holds for a launcher too.
- **Original art only.** No game's logo, marquee or character. Titles are
  text. The only picture of a game is the one its own ROM draws (section 3).
- **In the game:** on VGA the portrait boards leave margins either side.
  These can carry an original generic bezel. It is drawn once at setup, so
  it costs nothing per frame. Optional: off by default on an 8088.

## 5. What else I recommend for a release

In order of value:

1. **Pause and help.** P pauses, the board's clock held. F1 shows that
   game's controls over the picture. The two questions the owner's tests
   kept asking.
2. **Per-game settings, kept.** The DIP switches that matter (lives,
   difficulty, bonus, free play), written in words rather than bits, and
   key remapping. Both are saved in APPDATA (`os88data.inc`), as MAME's
   per-game `.cfg` files are.
3. **High scores that survive.** Per game, the RAM range that holds its
   table (MAME's `hiscore.dat` idea, written here as addresses only): saved
   when the game is left and put back after its power-on test. Q\*bert's
   table is in its NVRAM (0000-0FFFh), so it is simply that range saved.
4. **Input state cleared on every board change and every entry into a game.**
   The owner's one unexplained Q\*bert run fits a held key surviving from
   Pac-Man. A clean slate at the bracket's door costs nothing.
5. **Q\*bert's sound.** Its sound is a 6502 board running its own two
   ROMs, writing a DAC. A small 6502 interpreter (the generated core the
   survey proposed; ARCADE-EMU-PLAN 8.1) runs that board. WHEN it runs is
   R5's first measurement, because the two choices pull against section
   2.5:
   - rendered ONCE, every sound command into a clip, kept beside the
     `.ROM`: free at play, but PCM costs disk (MEASURE the commands' total
     length first; 4-bit ADPCM halves it);
   - run LIVE beside the game: no disk at all, but a 6502 at the board's
     ~0.9 MHz is affordable on a 286 and not on an 8088. There, the
     speaker or FM plays the command's pitch instead.

   Speech (the Votrax chip) is a later question. In the release (section 7).
6. **A safety catch for native boards.** The game runs with interrupts off
   (ARCADE-SPEC 10.3), so a game that never yields would freeze the machine.
   A per-run cap counted by the N88 core is not possible without traps, so
   the protection is the test instead: `qbnative` plays hours of attract
   mode and every scripted game state.
7. **Accepted-set coverage.** For each game, the clones whose program is the
   same, so a user's set is accepted when it should be. Each needs an
   identity check (its recipe fills to the same code) or a recipe of its own.

**Not recommended for this release:** save states, a windowed (non-full
screen) picture, more boards. Each is worth having, and none is what the
first release is judged by.

## 6. Waves

Each wave is reviewable on its own and lands on `arcade-emu`. Rows are
named per wave and run once, after the last (CLAUDE.md, *Testing*).

### R1: the recipe, and a package with no ROM in it
- [x] `arcaot.py`/`arcz80t.py` `--recipe`: holes and the fill list. (The
      PC table ships as it is: it is addresses.)
- [x] Set descriptors: the `.ROM` header names the set, its length and its
      CRC-32 (ARCADE-SPEC 1.4); the chips' pins stay `getarcrom.py`'s.
- [x] `make arcade` builds the ROM-free package (184KB, MEASURED). `make
      arcaderoms` writes the `.ROM` files; the development disk carries them.
- [x] Each board rebuilt at its pick from recipe and chips (section 2.4):
      `ar_fill`, `pm_build`, `qb_build`. Row `arcbuilt`: what the guest
      builds is the host tools' output byte for byte.
- [x] Row `arcrecipe` (host): every recipe filled from its pinned ROM is
      byte-identical to the translation, and holds no 8-byte ROM run. Red
      with one fill entry removed.
- [x] Found on the way, and fixed: Q\*bert's picture repainted a change of
      scene in one 1.7 s draw on the 8088 and dropped frames; changed tiles
      now wait in boxes drawn until the next frame is due (ARCADE-SPEC 10.4).
- [ ] MEASURED and open for R2: a Q\*bert pick on the 8088 from floppy is
      about 15 s - the 64KB read 4.6 s, the CRC 2.4 s, the build 4.2 s.

### R2: import on the guest
- [x] `apps/os88inflate.inc`: a DEFLATE decoder for a stream held whole
      (puff's bit-at-a-time Huffman, ~1KB of work space, the output its own
      window). DECIDED against moving PIXEL's: that one is table-driven and
      streams through a 32KB ring on a worker, bound to PIXEL's part
      variables; a ZIP member fits its destination whole and is decoded
      once. Two decoders of different shape, not a copy (row `inflate86`:
      345 streams held to zlib).
- [x] ZIP central directory (EOCD found backwards past a comment, root
      members only, names matched without case), STORED and DEFLATE, CRC32
      checked twice - the directory's against the pin, and the inflated
      bytes against it again.
- [x] The `.ROM` writer: `<SET>.ROM` beside its zip, the place remembered
      in `ARCADE.DAT` so a pick finds it from anywhere; toasts within
      TOAST_MAX.
- [x] Loose-file folders: any one chip of a set picked, every chip read
      by its MAME name beside it.
- [x] Chips matched by CRC-32 in any folder (merged sets, renamed chips);
      a split clone completed from its parent's zip beside it (Pac-Man's
      `puckman`).
- [x] The menu bar's file progress through read and making
      (`OSAPI_FS_OP`, `OSAPI_FS_PROG`).
- [x] "`<SET>.ROM`: disk in B:" for a set last imported on a floppy.
- [x] Row `arcimport`: a one-bit-wrong chip writes nothing; invaders.zip
      and qbert.zip each make a `.ROM` byte-identical to the host's, both
      games run, and are found again after a restart.
- [x] A truncated zip refused on the guest (row `arcimport`).
- [ ] Import timed on the 5150 (field; the owner: a one-time cost, and
      15 s for a pick is fine).

### R3: the frontend
- [x] One generic cabinet (the owner: not one per machine), straight to
      code rather than mockups first: a bulb marquee, the list with each
      game's state, the screen, the card, a coin door (ARCADE-SPEC 11).
- [x] The list, the card, the screenshot taken from the game (`.PIC`), the
      buttons, the keys and the mouse. Opening is a look, not a load.
- [x] Mono faces for Hercules and CGA (a compact layout on CGA), looked at
      on each: `docs/arcade/face-*.png`.
- [x] Flicker and repaint measured (`m.flicker`: no transient pixel on a
      move) and the identity check (rows `arcface`, `arcfaceherc`,
      `arcfacecga`).
- [ ] SETTINGS is drawn unlit until R4 wires it.

### R4: settings, scores, pause, help
- [x] DIP switches in words, kept: Space Invaders and Pac-Man, the ones
      MAME defines with certainty (ARCADE-SPEC 12.3).
- [ ] Keys remapped: not done.
- [x] High-score ranges per game, saved and restored: Space Invaders and
      Pac-Man. Q*bert's (battery RAM with the operator's settings): not done.
- [x] Pause (P) and the F1 card over the picture; Esc from either.
- [x] Input state cleared at every door (`ar_door`).

### R5: Q\*bert's sound
- [x] Measured first (`tools/qbsnd.py`): 28 sounding codes, 36.4 s in all;
      the game's 13 about 11.7 s; ~4,200 6503 instructions a frame. Too
      much to render and keep beside a `.ROM`, so LIVE (ARCADE-SPEC 13.1).
- [x] The 6502 core (`arc6502.inc`) and the board (`arcqbs.inc`): the
      RIOT, the latch, the per-sample stream at 10,989 Hz. `QBERT.ROM`
      carries the two sound chips (68KB). Rows `m6502`, `qbsmix`.
- [x] The routes: the stream on a 386 or better - the core's ESTIMATED
      cost is a 29 MHz 286's worth, so the owner's 16 MHz 286 is silent
      too. FM and the speaker have nothing to say about a DAC board. The
      Votrax speech and the knocker: not done.
- [ ] Field: the sound on a 386, and the core's real cost. A faster core
      (lazy flags, S in memory: ~3-4 instructions saved) would not reach a
      16 MHz 286 by itself.

### R6: the release
- [ ] ARCADE leaves `apps/RETIRED.txt`'s `local` line: **the owner's call**.
      The package carries no ROM byte now, but BUILDING it still needs the
      ROMs (the recipes are translated from them), fetched at pins, and
      Pac-Man's only from the owner's copy. The entry says so.
- [x] Its disk: ARCADE.O88 is 189KB packed; `emu360.img` has 139KB free,
      so it rides its OWN 360KB disk. `make arcaderel` builds the release
      floppies in four geometries - the package and its README, no ROM;
      198 of 354 clusters at 360KB, room for several games' `.ROM`s.
- [x] The README as a user's guide: the sets, import (merged, split,
      loose), the cabinet, the keys, settings, sound, the games' owners.
- [x] ARCADE-SPEC brought up to date (11, 12, 13), the diary.
- [ ] The final soak union and `test-full`. Field runs: 5150 #1 (Hercules
      and CGA), 5150 #2 (VGA and Sound Blaster), the 286.
- [ ] The `release-os8088` skill: after the owner's call above.

## 7. The owner's answers (2026-11-05)

1. **The recipe is fine.** Section 2.1 stands. No ROM byte and no
   translation ships; holes, fill lists and addresses do.
2. **Caches** were the question of a disk of their own. Section 2.5's
   measurement answers it: the cache is only the chips, so a 360KB disk
   holds ARCADE and all seven games' ROMs.
3. **Pac-Man is a game like any other**: its set is imported, checked and
   rebuilt the same way. `ARC_PACMAN` and the build's special case go.
4. **ARCADE rides its own disk at 360KB**, unless the emulators' disk has
   room. MEASURED: `emu360.img` as `all` builds it has 139 of 354 clusters
   free, against ARCADE's predicted 175KB, so it does not, unless R1 comes
   in well under the prediction. R6 decides on R1's number. At 720KB and
   larger it can ride whichever disk has room. **On the emulators' disk only
   the program fits**, so the ROMs and their `.ROM` files live on another
   disk or the hard disk: section 2.2's rule (the `.ROM` goes where the
   ROMs are, never by default where the program is) is what makes that
   work, and ARCADE's own 360KB disk is just the case where they are the
   same disk.
5. **Q\*bert's sound is in the release**: R5 is no longer conditional.

## 8. Risks

| risk | what would show it | what then |
|---|---|---|
| A recipe's fill list misses a ROM-derived byte | R1's byte-identity row | It cannot ship: the row is the gate |
| Inflate on an 8088 is too slow for a 64KB set | R2's timing on the 5150 | Progress and patience: import is once. Loose files are the fast route |
| A user's set is a clone with a different program | R2's CRC refusal names it | Add the clone with its own recipe (section 5, item 7) |
| The `.ROM` files do not fit the user's disk | the import's free-space check | Import to the disk the user names; a `.ROM` is 4-68KB |
| A remembered ROM disk is not in its drive | the pick | Ask for it by label and drive; cancel leaves the game "ROM needed" |
| A pick on an 8088 is slow (section 2.4) | R2's timing on the 5150 | Keep the derived tables beside the `.ROM`, as an option |
| The frontend's art costs too much on CGA | R3's measurement | The compact layout; art is a role table, not a requirement |
