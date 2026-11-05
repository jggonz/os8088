# ARCADE to a release - the plan

**Status: PROPOSED** (2026-11-05). ARCADE today is `docs/ARCADE-SPEC.md`;
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
3. **Build the game's CACHE FILE**: the board image, the recipe filled, the
   PC table expanded, the derived tables (turned tiles and sprites, colour
   maps, Q\*bert's patch applied). One file a game, `<SET>.ARC`, in
   `SYSTEM\APPDATA\ARCADE\` on the volume the user picks; the hard disk is
   offered first when there is one. The board then loads from its cache
   exactly as it loads its lazy parts today.
4. **Remember it**: a small index in APPDATA says which games are ready and
   where their caches are. A game whose cache has vanished is "ROM needed"
   again, never an error.

One zip can hold several games (`invaders.zip` carries ~40 clone folders);
import takes every set it recognises. Import is one-time and may take
minutes on an 8088, so it shows progress per chip. It runs on the desktop,
not in the full-screen bracket, so a refusal is a toast.

**Memory.** Inflating needs its 32KB window plus a game's largest chip.
Building the cache needs the largest part (about 57KB for Pac-Man's code).
Both fit on 640KB. kern_small refuses, as `op_load` already does.

**Where the user gets ROMs** is said plainly in the README: dumps of boards
they own, or a licensed collection. Each set is named exactly as MAME names
it, with its chips' CRC32s. Nothing in the tree, the disk or the README
points at a download.

### 2.3 For the builder

`make arcade` builds the ROM-free package and the recipes. `make arcadecache
GETARCFROM=<dir>` builds cache files on the host from the same ROMs, for
the test rows and for an owner who would rather not import on the guest.
The fetch at pinned hashes stays as the developer's route. It never touches
what ships.

## 3. A game browser instead of a menu

A list in a menu says nothing about the game, and seven games will become
more. The window becomes the **frontend**, the way MAME's and every
cabinet's is: pick by looking.

| part | what it does |
|---|---|
| **the list** | every game ARCADE knows: title, maker, year, each with its state - READY, ROM NEEDED, NEEDS VGA, NEEDS A 286 FOR FULL SPEED (`OSAPI_CPU_INFO` and `OSAPI_FSX_CAPS`, a fact rather than a guess, PERFORMANCE.md rule 7) |
| **the screen** | the selected game's picture. **Taken from the game itself**: the first time it is played, ARCADE keeps a frame of its attract mode in the cache, scaled into the card. No artwork is shipped, so no artwork can be someone else's. Before that it shows the game's name |
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
5. **Q\*bert's sound.** Its sound is a 6502 board running its own two ROMs.
   The plan: a small 6502 interpreter (the generated core the survey
   proposed; ARCADE-EMU-PLAN 8.1) runs at IMPORT, playing every sound
   command into a PCM clip in the cache. Then the stream and FM routes play
   clips, as Space Invaders' do. Speech (the Votrax) is a later question.
   Conditional: in the release if it lands in time, else the first update.
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
- [ ] `arcaot.py`/`arcz80t.py` `--recipe`: holes, fill list, PC pairs.
- [ ] Set descriptors for the seven sets, from `getarcrom.py`'s pins.
- [ ] `make arcade` builds the ROM-free package. `make arcadecache` builds
      caches on the host.
- [ ] Row `arcrecipe` (host): every recipe filled from its pinned ROM is
      byte-identical to the translation. Red with one fill entry removed.
- [ ] A scan of the package (host) for any 16-byte run of any pinned ROM:
      none.

### R2: import on the guest
- [ ] `apps/os88inflate.inc` out of PIXEL, PIXEL on it, PIXEL's rows green.
- [ ] ZIP central directory, STORED and DEFLATE, CRC32, loose-file folders.
- [ ] The cache writer, the index, "ROM needed" and its toasts.
- [ ] Rows: import from a zip on B: on MartyPC, then play (the existing
      `arcspeed`/`arcglass`/`qbglass` rows run from imported caches); a
      wrong CRC refused naming the chip; a truncated zip refused; import
      timed on the 5150.

### R3: the frontend
- [ ] retro-face mockups first, for the owner to pick.
- [ ] The list, the card, the screenshot taken from the game, the buttons,
      the keys.
- [ ] Mono faces for Hercules and CGA, looked at on each (SPEC.md 39.4).
- [ ] Flicker and repaint measured (`m.flicker`), against PERFORMANCE.md
      Part 5's launcher rows.

### R4: settings, scores, pause, help
- [ ] DIP switches in words and keys remapped, both saved.
- [ ] High-score ranges per game, saved and restored.
- [ ] Pause and the F1 card. Input state cleared at every door.

### R5: Q\*bert's sound (conditional)
- [ ] The 6502 core and the sound board at import, the clips cached, the
      stream and FM routes.

### R6: the release
- [ ] ARCADE leaves `apps/RETIRED.txt`'s `local` line. Its disk is decided
      by its ROM-free size (section 7).
- [ ] The README as a user's guide: the accepted sets, import, the keys.
- [ ] ARCADE-SPEC brought up to date, the diary, this plan moved to
      `completed/`.
- [ ] The final soak union and `test-full`. Field runs: 5150 #1 (Hercules
      and CGA), 5150 #2 (VGA and Sound Blaster), the 286.
- [ ] The `release-os8088` skill.

## 7. For the owner to decide

1. **The recipe's standing.** Section 2.1 argues that a recipe of
   addresses, holes and translator output carries no ROM content. Whether
   that is enough to ship it is a judgement for the owner, not for the
   code. If it is not, the alternative is translating ON THE GUEST at
   import: `arcaot.py` ported to 8086, which would make R1 by far the
   largest wave.
2. **Where caches live** when there is no hard disk. A 360KB machine needs a
   floppy of its own for them: one game's cache is 60-130KB.
3. **Which games ship.** All seven, Pac-Man included, imported from the
   user's own set? Or Pac-Man held back as it is today?
4. **Which disk ARCADE rides**: its own disk in four geometries, the games
   category disk (SPEC.md 24.6), or both.
5. **Q\*bert's sound**: in the release, or the first update after it.

## 8. Risks

| risk | what would show it | what then |
|---|---|---|
| A recipe's fill list misses a ROM-derived byte | R1's byte-identity row | It cannot ship: the row is the gate |
| Inflate on an 8088 is too slow for a 64KB set | R2's timing on the 5150 | Progress and patience: import is once. Loose files are the fast route |
| A user's set is a clone with a different program | R2's CRC refusal names it | Add the clone with its own recipe (section 5, item 7) |
| Caches do not fit the user's disks | the index's free-space check | Import one game at a time, to the disk the user names |
| The frontend's art costs too much on CGA | R3's measurement | The compact layout; art is a role table, not a requirement |
