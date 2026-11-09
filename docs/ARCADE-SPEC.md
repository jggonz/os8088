# ARCADE - arcade boards, their ROMs translated to 8086 ahead of time (`apps/arcade/`)

**The binding contract for ARCADE.** It stands to `apps/arcade/` as
docs/REALDMG-SPEC.md stands to `apps/realdmg/`: the register map, the
layouts, the interfaces between the translated code and the package that
hosts it, and the rules the timing keeps are pinned here, and a change goes
here *before* it goes in the code. The design record is
docs/plans/ARCADE-EMU-PLAN.md. It is cited from outside as "ARCADE-SPEC
section N".

**What is built, stated once:** seven boards. Midway's 8080 B&W board running
Space Invaders (Taito, 1978), and Taito's two colour boards on the same
8080 design - Space Invaders Part II (1979) and Lunar Rescue (1979) - full
screen on VGA (in colour), Hercules and CGA; Midway's two LANDSCAPE boards
on it, Gun Fight (1975) and Sea Wolf (1976), on the same three; Gottlieb's
Q\*bert board (1982), whose CPU is an 8088 and whose program therefore RUNS
NATIVELY, untranslated (section 10), full screen on VGA, silent; and
Namco's Pac-Man board (Midway, 1980), its Z80 translated by section 7's
translator, full screen on VGA only, built in only when the owner has
supplied the ROM. All but Q\*bert have sound on a Sound Blaster, an AdLib
or the speaker (section 8). There is no
windowed picture, no save state, and no interpreter for a guest PC the
translation does not hold (section 3.2 says what happens then).

---

## 1. What it is

An arcade-board emulator in which **the guest CPU is not emulated at run
time**. `apps/arcade/arcaot.py` translates the board's whole ROM to 8086 code
when the package is BUILT, and the package runs that code as its own: no
fetch, no decode, no dispatch. The board around the CPU - its interrupts, its
video, its sound latches and its controls - is the package's.

The whole translation fits because an arcade ROM is small (8KB for Space
Invaders) and fixed: never banked, never written, never loaded from
anywhere else. That is what makes ahead-of-time translation the default here
where RealDMG, whose cartridges bank and grow to megabytes, interprets first
and recompiles hot code (REALDMG-SPEC 16, 20).

### 1.1 The files

| file | what it is |
|---|---|
| `apps/arcade/arcaot.py` | the 8080 translator: ROM + seeds -> the code part and aux's PC table, assembled by itself until its short branches settle (section 3) |
| `apps/arcade/arcz80t.py`, `z80dec.py` | the Z80 translator and its decoder (section 7) |
| `apps/arcade/arcz80rt.inc` | the Pac-Man board's helpers inside its code part: the I/O, LDIR and kin (section 7.4) |
| `apps/arcade/arcpm.inc` | the Pac-Man board's host half: its interrupt, keys, sound and picture (section 7.7) |
| `apps/arcade/arcpmgfx.py` | Pac-Man's ROM part: the ROM and what is derived from it (section 7.7) |
| `apps/arcade/arcsnd.inc` | the sound: the route, the stream, FM, and Space Invaders' half (section 8) |
| `apps/arcade/arcland.inc` | the landscape boards: their picture, Gun Fight's and Sea Wolf's controls, sound and lamps (sections 4.6, 5.3) |
| `tests/arcglass.py` | the landscape boards' glass held to their video RAM (section 4.6) |
| `apps/arcade/arcn88.asm` | the N88 core: the door between the host loop and a board's own 8088 code (section 10.2) |
| `apps/arcade/arcqb.inc` | the Q\*bert board's host half: its power-on, NMI, keys, palette and picture (section 10) |
| `apps/arcade/arcqbgfx.py` | Q\*bert's ROM part and sprite part: the program patched, the tiles and sprites turned (section 10.4) |
| `apps/arcade/qbert.patch` | what Q\*bert's program needs changed to run in a segment of the PC, as ADDRESSES and kinds, each checked against the ROM (section 10.2) |
| `tests/qbnative.py` | the patched program under Unicorn as the N88 core runs it: the patch's claims checked (section 10.8) |
| `tests/qbglass.py` | Q\*bert's glass held to MAME's picture of its memory, read out of the VGA's planes (section 10.8) |
| `apps/arcade/arcsfx.py` | Space Invaders' sounds, SYNTHESISED - no recording, no ROM (section 8.3) |
| `tests/arcsound.py` | the stream and FM routes on the 8088, the sound read back (section 8.5) |
| `tools/arcz80.py`, `tools/arcpac.py` | the Z80 and the Pac-Man board in Python: the reference (section 7.6) |
| `apps/arcade/arcrt.inc` | the runtime the translation carries: the code part's header, `arc_enter` and the ways back to the host (section 3.4) |
| `apps/arcade/arcdefs.inc` | the layout both sides share: the view's cells, the aux segment, the header (section 2) |
| `apps/arcade/arcade.asm` | the package: the window, the full-screen bracket, the clock, the keys, the picture and the sound (sections 4, 5) |
| `apps/arcade/<game>.seeds` | entry points no static walk finds, as ADDRESSES (section 3.2): `invaders`, `invadpt2` (six), `lrescue` (none), `pacman`. No ROM byte |
| `tools/getarcrom.py` | fetches and checks the ROM (section 1.3) |
| `tools/arcsim.py` | the reference model: an 8080 and the board in Python (section 6) |
| `tests/arclock.py` | the translation in lockstep with the model (section 6.1) |

### 1.2 The package

`ARCADE.O88` is the package image plus its parts (SPEC.md 20.12): a VIEW all
boards share and, for each board, what it ships instead of its ROM
(docs/plans/ARCADE-RELEASE-PLAN.md 2.1). **There is no ROM byte in the
package**: a translated board's code part is its RECIPE (section 3.10), and
the ROM is the user's `<SET>.ROM` file (section 1.4), read at the pick.
**Every part is LAZY** (`OP_LAZY`): Game reads the board's parts and its
`.ROM` (`ar_select`, on the desktop, where a refusal may toast) and drops
the previous board's, so one board is in memory, never all of them - and a
board that cannot be read leaves no board picked. At its start ARCADE picks
the first board whose `.ROM` is there, quietly; none is a window that says
so, not a refusal to open.

| part | what | size |
|---|---|---|
| 0 | the VIEW: the board's address space and the cells (section 2.1), zero-filled, the one eager part | 25KB |
| 1, 2 | Space Invaders: the RECIPE (header, runtime, translation with its ROM bytes as holes, the fill list), AUX (the PC table, then the flags and the write list) | 14KB, 50KB |
| 3, 4 | Space Invaders Part II: the same | 26KB, 50KB |
| 5, 6 | Lunar Rescue: the same | 35KB, 50KB |
| 7, 8 | Gun Fight: the same | 13KB, 50KB |
| 9, 10 | Sea Wolf: the same | 18KB, 50KB |
| 11 | Q\*bert: the N88 core (not packed: LZ4 makes 103 bytes larger) | 103 bytes |
| 12, 13 | Pac-Man (built when the builder has its set): the recipe, aux | 60KB, 50KB |
| last (12, or 14 with Pac-Man) | the 8080 boards' sounds (`arcsfx.py`), read only for a stream | 58KB |

With every board it is 184KB on the disk (MEASURED), against 261KB when it
carried the ROMs. What a pick builds from the `.ROM` - Pac-Man's turned
tables (section 7.7), Q\*bert's patched program and turned tiles and sprites
(section 10.4) - are claims `BD_BUILD` makes and `BD_OFF` frees, like
Pac-Man's 64KB and Q\*bert's 94KB of shadow and segment (126KB with its
sound board's memory, section 13.3).

The shipped build refuses to start under kern_mega's protected mode
(`OS88_MEGA_REFUSE`): kern_mega's disk carries the ARM instead, built with
`-DPKG_MEGA` (section 14), which runs there in protected mode with its
region and claims allowed above 1MB. kern_small has no room and refuses at
`op_load`.

### 1.4 The user's ROM: `<SET>.ROM`

A set the user has given ARCADE is one file, named for MAME's set -
`INVADERS.ROM`, `INVADPT2.ROM`, `LRESCUE.ROM`, `GUNFIGHT.ROM`, `SEAWOLF.ROM`,
`QBERT.ROM`, `PACMAN.ROM` - a 32-byte header and the board image of section
1.3:

| offset | what |
|---|---|
| 0 | `ARCROM`, then the version (1) and a zero |
| 8 | the set's name, upper case, NUL-padded to 8 |
| 16 | the image's length, a dword (at most 65,536) |
| 20 | the image's CRC-32 (zlib's), a dword |
| 24 | zero (reserved: the snapshot, the sound chips) |
| 32 | the image |

`ar_romload` reads it whole from ARCADE's current folder (`OSAPI_FILE_READ`:
a probe for its size, a claim, the read; the file may be packed on the
disk) and refuses it, with a toast naming the file, unless the header is
right, the name is the board's, the length is the set's in `arcsets.inc` (a
header that agrees with a SHORT image of its own would otherwise have
`ar_fill` and `BD_BUILD` read past the claim), and the image's CRC-32 is the header's
(`ar_crc32`, a table made at start). The recipe is then filled from it
(`ar_fill`) and the board builds what it needs (`BD_BUILD`). `make
arcaderoms` writes the files from the pinned sets (`getarcrom.py
--rom-file`); the development disk (`make arcadedisk`) carries them beside
the package. A release disk carries none: the guest's import writes them
(the release plan's R2).

### 1.5 Importing a set: from MAME's zip to `<SET>.ROM`, on the machine

**Game > Add ROM sets...** opens the system file chooser (`OSAPI_FILE_DLG`)
on a `.ZIP` (`arcimp.inc`), or on any one file of a set kept as LOOSE CHIPS
in a folder. The board picked is let go for the memory.

**A zip** is read whole (at most 448KB: MAME's `invaders.zip`, forty clones
in folders, is 220KB), its End Of Central Directory found back from its end
over any comment (TorrentZip leaves one), and every set ARCADE knows is
looked for in its directory **by each chip's CRC-32 and size, in any
folder** - not by name. A MERGED set keeps a clone's chips in a folder of
the clone's name, and a chip two sets share is the same bytes under either
name, so the checksum is the identity. The table of sets is `arcsets.inc`,
which `tools/getarcrom.py --asm-sets` generates from its pins: each chip's
MAME name, its place and size in the image and its CRC-32, and for a clone
MAME's parent zip - names and checksums, no ROM byte.

A set some of whose chips are there and some not is a clone from a SPLIT
set, which keeps only the chips that differ: its parent's zip
(`PUCKMAN.ZIP` for `pacman`) is read from the same folder and the set looked
for again across both. A set is made only when the directories alone say
it can be: every chip found, stored or deflated. Then each chip is copied
or INFLATED (`apps/os88inflate.inc`: zlib's `puff` in 8086, the output its
own window, 1KB of work space) into the image, its CRC-32 taken again as it
comes out, and the image written with its header as `<SET>.ROM` **in the
folder the zip is in**. Every offset and length a zip gives is checked
against the file before it is used - a chip's packed size too: 1 to 32KB, a
stored chip's exactly its size, and the inflater reads no byte past it.

**Loose chips**: each set's chips are read as files of their MAME names from
the folder the picked file is in, each at its exact size and checked against
its CRC-32. A set whose first chip has no file there is not there, and says
nothing.

A set some of whose chips are there but not all, or not right, says so
("not a set I know"); a file with none, "no set in it". The menu bar's file
progress (SPEC.md 12.8.10, `OSAPI_FS_OP`) runs through the read and through
the making - each chip counted as it comes out, each `.ROM` as it is
written - since an import on an 8088 takes seconds.

**Where a pick looks** (`ar_romload`): the folder ARCADE stands in, then
each folder an import wrote to, newest first - up to eight, kept in
`ARCADE.DAT` in `SYSTEM\APPDATA` on ARCADE's own disk (`os88data.inc`), the
program's own information, so a disk of ROMs works wherever it is put.
`ARCADE.DAT` also keeps the drive each set was last imported on. A pick that
finds nothing says `<SET>.ROM: not found` - or, for a set imported on a
floppy drive, `<SET>.ROM: disk in B:`, since the disk it was made on is
most likely in a drawer.

`tests/arcimport.py` (row `arcimport`) is the import on MartyPC's VGA XT:
the zips in a `ROMS` folder, each `.ROM` written there read back off the
floppy and held byte for byte to `make arcaderoms`'s - MAME's own
`invaders.zip` and `qbert.zip`, Gun Fight's chips renamed in a clone's
folder, a split Pac-Man with its parent beside it, Sea Wolf as loose files -
the menu bar's progress seen armed, two games run from their imported
files, and after a restart from ARCADE's own folder Q\*bert found through
`ARCADE.DAT`. A zip with one bit of one chip flipped, and a zip cut short,
make nothing. `tests/inflate86.py` holds the inflater to zlib under Unicorn:
stored, fixed and dynamic blocks to 64KB, every member of the sets' zips,
and the streams it must refuse. The "disk in B:" toast is not driven by a
row: the harness cannot change a floppy under a running guest.

### 1.6 The committed build state: no `make` needs a ROM

The package is ROM-free, but it is MADE from the ROMs: the recipes and aux
parts are the translator's output over each board's program, Q\*bert's
patch table and the import's set table are checked against the pinned
chips. The owner's rule (2026-11-05): ROM fetches stay optional - no
release, and certainly no `make`, may need archive.org. So the last state
of those 14 parts is committed, gzipped (no timestamps, so the same bytes
each time), in `apps/arcade/prebuilt/`, 140KB, and:

| | |
|---|---|
| `make arcade`, `make arcaderel` | the default `ARCFROM=prebuilt`: the parts unpacked from `apps/arcade/prebuilt/` into `build/arcade/pre/`. No ROM, no network. A fresh tree with no ROM cache builds `ARCADE.O88` byte for byte the one `ARCFROM=rom` builds |
| `make ARCFROM=rom arcade` | the parts made here from the ROMs, fetched at their pins |
| `make arcadeprebuilt` | `ARCFROM=rom`'s parts written into `apps/arcade/prebuilt/` with `STAMP`, the SHA-256 of every input that makes them (`tools/arcprebuilt.py`) |
| fast tier `arcprebuilt-stamp` | `STAMP` against the tree: a translator, seed, patch or pin change without a refresh fails `make`, naming the file - no ROM needed to say so |
| soak row `arcprebuilt` | where the ROMs are: the parts made again are the committed ones byte for byte, and none holds a run of 8 bytes of any ROM |

Every set, Pac-Man's too, is fetchable at a pin: Pac-Man's from the Internet
Archive's Namco Classics item, whose `pacman.zip` holds exactly MAME's
`pacman` chips. The development disks (`arcadedisk`) and the rows that run
the games still fetch the ROMs; the package and its release disks do not.

### 1.3 The ROM

**No ROM, and nothing derived from one, is committed or shipped.** `tools/getarcrom.py`
fetches MAME's `invaders` set from ONE archive.org item
(`arcade_invaders/invaders.zip`, SHA-1 `8d2d4172...`), checks each of its
four 2KB chips by SHA-256, and writes the 8KB board image
`build/arcade-rom/INVADERS.ROM` with `invaders.h/g/f/e` at 0000/0800/1000/
1800. A mismatch is a hard failure. **Taito's colour boards** the same way:
`invadpt2` (`arcade_invadpt2/invadpt2.zip`, SHA-1 `228cadc2...`) and
`lrescue` (`arcade_lrescue/lrescue.zip`, SHA-1 `3da8bff2...`), and Midway's
`gunfight` (`arcade_gunfight/gunfight.zip`, SHA-1 `52fcc4a3...`) and
`seawolf` (`arcade_seawolf/seawolf.zip`, SHA-1 `18d15528...`) - four 1KB
chips each, a 4KB image - every chip's
CRC32 MAME's, into a 22KB image: the CPU's 0000-4FFFh as it sees it (RAM's
2000-3FFFh zero) and the colour maps at 5000h and 5400h (Lunar Rescue has
one map, loaded twice, as MAME's `ROM_RELOAD`). **Q\*bert** is MAME's
`qbert` (`arcade_qbert/qbert.zip`, SHA-1 `b560f935...`): its three 8KB
program chips, two 4KB tile chips and four 8KB sprite chips, every CRC32
MAME's, into a 64KB image - the program as the 8088 sees it at A000-FFFFh
put at 0000h, the tiles at 6000h, the sprite planes at 8000h, MSB first.
`make arcaderom` fetches them all. `--from <dir-or-zip>` takes a set the
owner already has (`make arcaderom GETARCFROM=...`). The code part is built
from that image, so it too lives only in `build/` and on the floppy, and the
package is a `local` line in apps/RETIRED.txt: standalone disks, no standard
image, no live media.

---

## 2. The shared layout (`arcdefs.inc`)

### 2.1 The view

One segment holding the board's address space IN PLACE: for Space Invaders
ROM 0000-1FFF (copied from its ROM part at power-on), RAM 2000-23FF, VRAM
2400-3FFF; for Pac-Man section 7.4's map. DS = SS = the view while
translated code runs, so a guest address is an 8086 offset and a guest PUSH
is an 8086 PUSH.

Above both, at 6000h - which neither game reaches (MEASURED: Space Invaders
touches nothing above 3FFF, Pac-Man nothing above 50FF but its A15 mirrors,
section 7.4) - are the cells the core and the host share:

| cell | what |
|---|---|
| `ST_AF` .. `ST_PC` (6000-600B) | the guest's registers between runs: AL = A, AH = F in LAHF's layout, then BC, DE, HL, SP, PC |
| `ST_BUD`, `ST_LEFT` | the run's poll budget in, what was left out (section 5.2) |
| `ST_WHY` | why the run ended: `ARCX_POLL`, `ARCX_MISS`, `ARCX_HALT` |
| `ST_INTE` | the 8080's interrupt enable: EI and DI write it |
| `ST_PEND`, `ST_EIX` | the host has an interrupt waiting; EI ended the run because of it (section 5.1) |
| `ST_HSS`, `ST_HSP` | the host's SS:SP across a run |
| `V_IN0`..`V_IN2` | the board's input ports |
| `V_SHLO`, `V_SHHI`, `V_SHOFF` | the shift register and its offset |
| `V_SND3`, `V_SND5` | the two sound latches |
| `ST_IX` .. `ST_SAVE` (6024-603D) | the Z80's own: IX, IY, the alternate set, I, IM, the vector, the interrupt enable latch, IFF2, and the helpers' ways back (section 7.5) |
| `V_SIG` 6040h | 'ARCV': where an instrument finds the view in guest memory |
| `V_NFRAME` .. `V_NCELL` | the host's counters: frames delivered, half frames dropped, runs, the budget, cells drawn |
| `V_SBTOT`, `V_SBSEG` | the stream's total, written each frame after the mix, and its ring's segment (0 when there is none): enough for a test to read back every byte mixed (section 8.1) |
| `V_SROUTE`, `V_SWHY` | the sound's route (section 8), and the last step that declined a better one: 1 the Control Panel's speaker route, 2 no stream caps, 3 the board's preparation, 4 the ring's claim, 5 verb 0, 6 no FM, 7 the driver's ident, 8 the FM claim |
| `V_PROBE` | `arcaot.py --probe`'s counts, in an instrument build only (6060-607F) |
| `NV_GSEG`, `NV_GSP`, `NV_NMI` (6080-6084) | the N88 core's (section 10.2): the game's own segment G, its SP when it yielded, and the NMI the host has delivered for the next run |

### 2.2 Aux

| offset | what |
|---|---|
| `AUX_TABLE` 0 | the PC TABLE: a word for every guest PC 0000-3FFF, or 0000-4FFF on a Taito colour board (section 3.2). RET and JP (HL) are `jmp [es:si]` |
| `AUX_FLAGS` + key, A400-BF1F | the 8080 boards' CELL FLAGS: one byte per 8x8 cell, at 8000h + the cell's key (section 4.1) |
| `PM_TFLAG` + tile, 9000-93FF | Pac-Man's TILE FLAGS: a byte for each video RAM index (section 7.7) |
| `AUX_LPTR` C000h | the write list's next free offset. ES:DI holds it while translated code runs |
| `AUX_LIST` C002h | the write list: cell (or tile) keys, a word each, each at most once until drawn - to C701h at most |

The list moved above everything else, from 8000h, when a colour board's
table grew to 4FFFh's PC and reached it: every board's aux is 50KB.

The table moved here from the code part when Pac-Man's translation, 56KB on
its own, left no room for it beside.

### 2.3 The code part's header

At `ARC_HDR`, its offset 0: `dw ARC_MAGIC` ('A3': the table in aux, the ROM
a part of its own), `dw arc_enter`, `dw` the end of the translation. The
package checks every board's magic at its start and refuses a damaged part.
The N88 core's header (section 10.2) is the same with one word more,
`AH_YIELD`: the far call a native game makes in place of its wait.

---

## 3. The translation (`arcaot.py`)

### 3.1 The register map

| 8080 | 8086 | |
|---|---|---|
| A | AL | |
| F | FLAGS | **between instructions the 8080's flags ARE the 8086's.** LAHF loads `SF ZF 0 AF 0 PF 1 CF`, which is the 8080's PSW bit for bit, so PUSH PSW is `lahf / xchg al,ah / push ax / mov al,ah` and POP PSW `pop ax / xchg al,ah / sahf` |
| B, C | CH, CL | |
| D, E | DH, DL | |
| H, L | BH, BL | M is `[bx]` |
| SP | SP | SS = the view |
| PC | - | static: every instruction is a label `Lxxxx` |
| - | AH | scratch, and where a guard keeps the flags (section 3.5) |
| - | SI | scratch |
| - | DI | the write list's next free word in aux (section 4.1) |
| - | BP | the poll budget (section 5.2) |
| - | ES | aux |

### 3.2 What is translated, and the PC table

A **walk** from 0000 (reset), 0008 and 0010 (the board's two interrupts) and
every seed follows every direct JMP, Jcc, CALL, Ccc and RST, and the return
site after every call. **Seeds** are what no walk finds:

- `apps/arcade/<game>.seeds`, written by `tools/arcsim.py seeds`: every PCHL
  and RET target the model reached that is neither a call's return site nor
  an interrupt's. For Space Invaders that is six: 026F (a return address the
  object dispatcher pushes as a constant) and five object handlers.
- **Speculative walks** of every word in the ROM's DATA (bytes no decoded
  instruction covers) that points into the ROM. One is kept only if every
  instruction it decodes is whole, overlaps nothing already decoded, uses
  only the board's ports, and jumps or calls nowhere outside the ROM. This
  found 050E, a sixth object handler in the table at 1BC0 that the model's
  script had not reached. Most of what it adds is runs of zero bytes, which
  translate to nothing.
- First among those tentative walks, **every constant the code loads into
  a pair and pushes at once** (`LXI rp,nn / PUSH rp`): a return address
  made by hand, reached only by the RET of whatever the code jumps to next.
  Part II's 1F49 pushes 1F52 - a RET right after a PCHL, which no walk
  reaches - and the owner's play hit it ("no code at 1F52"). The same rule
  added 24 instructions to Lunar Rescue that had the same hole; a pushed
  data pointer fails the trial and is left alone.

**The ROM** is 0000-1FFFh on Space Invaders, and 0000-1FFFh and 4000h up to
the last 2KB chip that is not all zeros on a Taito colour board (`board_rom`:
47FFh on Part II, 4FFFh on Lunar Rescue). Without the bound a speculative
walk ran through Part II's empty 4800h-4FFFh and into a jump past 7FFFh.

`--no-speculate` turns the second off. The result for Space Invaders: 4,366
instructions, 2,086 of them speculative; Part II 4,416 (707); Lunar Rescue
5,251 (764); Gun Fight 1,426 (178); Sea Wolf 2,078 (473).

**An interrupt's return PC is a seed too.** `arcsim.py seeds` once left them
out, as PCs the walk reaches anyway - until Gun Fight's 03C9, the
instruction after a PCHL that a jump table sends it to, was ALSO where an
interrupt had landed, and the translation had nothing there. Gun Fight now
has 158 seeds, most of them return PCs; the older boards' seed files are
kept as they were, which their lockstep holds.

**The PC table** is the first 32KB of aux: 16,384 words, the translation of
every 8080 PC 0000-3FFF, or `arc_miss`, written by the translator from
NASM's symbol map. RET, RETcc and PCHL go through it (`pop si / lahf /
shl si,1 / sahf / jmp [es:si]`), and so does every entry from the host. **A miss stops the game** with "Arcade: no code
at XXXX" and returns to the window; nothing is interpreted (the plan's wave
4 is the fallback).

### 3.3 Each instruction

Most 8080 instructions are one 8086 instruction with the same flags: the ALU
ops (`add`/`adc`/`sub`/`sbb`/`and`/`xor`/`or`/`cmp`), INR/DCR (`inc`/`dec`,
CY untouched on both machines), the rotates (`rol`/`ror`/`rcl`/`rcr` by 1,
CY only), DAA, CMA (`not`), STC, CMC, every MOV and MVI to a register, the
loads, PUSH/POP of a pair, XCHG, SPHL. Jcc maps condition for condition (JM
is `js`, JPE `jpe`). CALL is `mov si,ret / push si / jmp`, RST likewise to
n x 8, and a short Jcc NASM reports out of range is re-emitted inverted
around a near JMP (`arcaot.py` assembles until none is).

**A store through M, STAX or STA** is the RAM fast path - `cmp bh,24h /
jb .r ... .r: mov [bx],r` - or, in VRAM, section 4.1's compare-and-list.
INX/DCX are `inc`/`dec` (guarded) or `lea bx,[bx+-1]` for HL; DAD is `add
bx,rp`, with the S Z P it must keep rebuilt around it when they are live.

The board's ports (`IN_PORTS`, `OUT_PORTS`): IN 1 and IN 2 read the view's
cells; IN 3 is INLINE - `push cx / mov cl,[V_SHOFF] / mov si,[V_SHLO] /
shl si,cl / pop cx / mov ax,si / mov al,ah` - because it is 30 reads a frame
and the CALL with its PUSHF was 7% of an 8088's time in play (MEASURED); OUT 2
stores the offset masked to 3 bits, OUT 4 shifts a byte in at the top, OUT 3
and OUT 5 latch the sound, OUT 6 (the watchdog) is nothing. EI and DI write
`ST_INTE` (section 5.1). HLT returns `ARCX_HALT`.

### 3.4 The runtime (`arcrt.inc`)

`arc_enter` is far-called with AX = the view and DX = aux. It disables
interrupts, banks the host's SS:SP in the view, loads the guest's registers
and DI, and jumps through the PC table. **Translated code runs with
interrupts OFF**: SS is the view, and an IRQ would push its frame below the
8080's SP into the board's RAM - RealDMG's rule (REALDMG-SPEC 5.2). A run is
bounded instead (section 5.2) and the host runs with interrupts on between
runs.

The ways back - `arc_poll` (the budget ran out at a poll point; the CALL to
it carries the 8080 PC as a `dw` after it), `arc_poll_ah` (the same, the
flags already in AH), `arc_miss` and `arc_halt` - store the registers, DI
and `ST_WHY`, restore the host's SS:SP and RETF. A CALL made inside a run
pushes below the 8080's SP, which is dead space by the 8080's own
definition.

### 3.5 The flags, and the one difference

A backward **liveness pass** over two groups - CY, and S Z AC P - marks
where the 8080 still needs its flags. A sequence of the translation's own
that sets FLAGS (a VRAM test, a poll's DEC, INX's `inc`) is wrapped in
`lahf ... sahf`, or `pushf ... popf` where AH is in use, only where they are
live after it. At an unknown successor (RET, PCHL) everything is live.

**AC after a logical op or a subtract is the 8086's, not the 8080's.** The
8080 sets AC on ANA from the OR of bit 3 of its operands, and on a subtract
as NOT a half borrow; the 8086 leaves AF undefined and sets it AS a half
borrow. Only DAA reads AC, and DAA here follows an ADD, where the two agree;
a PSW pushed by PUSH PSW can differ in that one bit. tests/arclock.py
forgives exactly that bit and nothing else.

### 3.6 Loop idioms

A loop whose whole body is one of these shapes, ending DCR B / JNZ to its
own head, is ONE 8086 loop with no poll inside (B bounds it at 256 turns),
matched by its BYTES wherever it occurs:

| idiom | 8080 | 8086 | where, in Space Invaders |
|---|---|---|---|
| `copy` | LDAX D / MOV M,A / INX H / INX D / DCR B / JNZ | `rep movsb` when the destination ends at or below 2400h; else byte by byte with section 4.1's VRAM rule | 1A32: 18% of the interrupt handlers' instructions |
| `count` | MOV A,M / ANA A / JZ +6 / INR C / INX H / DCR B / JNZ | C += the bytes that are not zero | 15F9: counting the live invaders, a third of every instruction the board runs |
| `scan` | MOV A,M / ANA A / JNZ x / INX H / DCR B / JNZ | stop at the first that is not zero, and go to x | 15C7 |

Each leaves exactly the registers, memory and FLAGS the instruction-by-
instruction translation would - the last DCR B's S Z P with the CY the body
last set - so the liveness pass and the lockstep cannot tell. An idiom is
not taken if any other instruction jumps inside it. `--no-idioms` turns
them off.

### 3.7 Spin loops are a HLT

A loop whose head loads A afresh (LDA, LDAX, MOV A,r, MOV A,M, MVI A, IN),
whose every other instruction touches only A and the flags without reading
the carry (the ALU ops but ADC, SBB, ACI, SBI; RLC, RRC, CMA, NOP, another
load of A), which stores nothing, writes no port but the watchdog's (OUT 6),
and leaves only by its own backward branch, does the same thing every pass:
nothing but an INTERRUPT can end it. Its branch becomes `mov si, head /
jmp arc_halt` (`find_spins`), and the host sleeps to the next interrupt as
for a HLT, resuming at the head. A run so ended does not adapt the budget,
any more than one an EI ended (section 5.2): it measured a wait, not work.

Lunar Rescue's wait for its interrupt (05D8: LDA / ANA A / OUT 6 / JNZ) was
**63% of the 5150 in play**, the budget grown long over its cheap passes,
and 32 half frames dropped in 10 s; as a HLT, 1 (row `arcspeedlr`, red with
`--no-spins`). Found: Space Invaders 0ADA 18B8 18C0, Part II 0CB9 45E4
45FF, Lunar Rescue 010A 0481 05D8 46EA 4ECA 4F36. The lockstep lets the
model spin and puts its interrupt where the 8086 side resumes, the head - a
few T-states late at most, on a pass that changes nothing.

### 3.8 Idle returns: measured, and NOT taken

Gun Fight's and Sea Wolf's main loops never wait in a loop of their own: a
dispatcher runs a task list round and round, and a task that is not due
returns at once - Gun Fight's at 0820 restores everything it touched, 64%
of all it runs. `arcsim.py idles` finds such returns (a pass back to the
return's target with registers, flags and every RAM byte as they were). As
a HLT they changed the GAME: in the model, halting at every such return
changed the picture in 1,035 of 1,500 frames of Gun Fight; halting only at
the passes an oracle proved idle, in 1 - and in 1,106 of Sea Wolf's, where
resuming at the loop's head moves where the next interrupt lands. And one
of Sea Wolf's ran with interrupts off, a HLT there a hang. No idle return
is translated as a HLT; the instrument stays. Both boards keep the board's
time on the 5150 without it (row `arcspeedgf`, `arcspeedsw`).

### 3.9 The boards' ports, their ROM, and a PUSH into the screen

`arcaot.py --board` picks the I/O map (`set_ports`, MAME mw8080bw.cpp's):
Space Invaders' and Taito's as section 2.1 has it; **Gun Fight**'s three
address lines, IN 4-7 mirroring IN 0-3, and an OUT that is three AND gates -
bit 0 the sound latch, bit 1 the shift count, bit 2 the shift data, any of
them in one OUT; **Sea Wolf**'s shifter read BIT-REVERSED at IN 0 (a 256-byte
table in the code part, `arc_rev`), its controls at IN 1 and 2, its lamps
at OUT 1 and 2 (`V_LAMP1`, `V_LAMP2`), the shift data and count at OUT 3 and
4, its sound at OUT 5.

**A store into ROM is no store** (MAME: 0000-1FFFh and 4000-5FFFh take no
write, RAM's 2000-3FFFh is mirrored at 6000h): Gun Fight and Sea Wolf draw
past the screen's end, into 4000-42FFh. A store through H at 40h or above
skips (one compare on the video path only, none on RAM's), INR/DCR M lists
no cell there, and a constant address is decided by the translator
(`ram_of`: ROM stores nothing, the mirror is RAM).

**Gun Fight and Sea Wolf erase the screen with SP in it** (LXI SP,4010h,
PUSH, PUSH...). On those two boards a PUSH whose SP is in video RAM lists
both bytes' cells; the others' PUSH is unchanged. The lockstep cannot see a
cell left unlisted - RAM agrees - so `tests/arclock.py --marks` holds the
write list to the screen (every cell a frame changed is on it), and
`tests/arcglass.py` the glass to video RAM; without it Sea Wolf's INSERT
COIN stayed on the glass in play.

### 3.10 The recipe: what ships instead of the translation

`arcaot.py --recipe` (and `arcz80t.py`) writes, beside each code part, the
code part as it SHIPS: the translation with every byte that came from the
ROM zeroed, and after the code (at `AH_END`) the FILL LIST that puts them
back. The ROM reaches a translation only through the DATA IMMEDIATES of
three kinds of instruction - MVI/LD r,n; LXI/LD rp,nn; the ALU's immediate
forms - the same encodings on both CPUs. Addresses, jump targets and ports
are the program's structure, as the seeds and `qbert.patch` are.

The holes are found by DIFFERENCE: the translation is emitted again with
every data immediate inverted and nothing else. It must assemble to the
same shape, the bytes that differ are the holes, and each is tied to the
operand byte of its own instruction it equals. The fill list is a word
count, then each hole as the step from the one before: offset +1..255 and
address -128..127 in two bytes, else 0 and both as words.

`ar_fill` (every translated board's `BD_BUILD`) fills the holes from the
user's image after the pick has read it. MEASURED: 327 to 2,034 holes a
board, the recipe 0.8 to 4.1KB over its code. `tests/arcrecipe.py` (row
`arcrecipe`, host-only) holds every recipe, filled from its pinned ROM, to
the translation byte for byte, and finds no 8-byte run of the ROM in it.

---

## 4. The picture

### 4.1 The write list

A VRAM store COMPARES FIRST: a store that changes nothing is not made
(MEASURED in play: 95 VRAM writes a frame, 20 changing anything). One that
changes a byte computes the cell's KEY - the address AND FF1Fh, which is the
cell's 256-byte band and its column, and also the offset of the cell's own
flag byte in aux - and, if that flag is clear, sets it and appends the key
at ES:DI. So the list is the cells that changed, each once, and the host
draws exactly the list and clears each flag as it goes. INR M and DCR M,
which always change their byte, only mark. SHLD into VRAM marks both cells.

### 4.2 A cell, turned

VRAM is 224 rows of 32 bytes; a byte is 8 pixels along one guest row, and
the cabinet's monitor is turned, so on the screen it is 8 pixels down one
COLUMN. A cell is 8 guest rows of one byte: turning it is an 8x8 bit
transpose (`ar_cell`: 64 SHR/RCL pairs, eight words pushed), giving 8 screen
rows of 8 pixels, each doubled across to 16 (`ar_dbl`) for the aspect. The
screen row of each of the 256 is `ar_ytab`, built per adapter, and an
adapter's own loop puts them (`ar_put_vga`, `_herc`, `_cga`).

### 4.3 Per adapter

| adapter | mode | rows | colour |
|---|---|---|---|
| VGA | `FSXM_VGA12` 640x480 | 256 rows into 480: in each cell 7 doubled and 1 single; 448 wide, centred | Space Invaders: the game writes PLANE 0 only. The cabinet's cellophane is filled ONCE into planes 1-2 at entry (red over rows 32-63, green over 184-239, green over the spare bases in 240-255), and DAC entries 0-5 colour each band's lit pixels, so the overlay costs nothing a frame. A Taito COLOUR board: see below |
| Hercules | `FSXM_HERC` 720x348 | 256 rows 1:1, from row 46; 448 wide | mono |
| CGA | `FSXM_CGA640` 640x200 | 256 into 200: each cell's 8 rows into 6, or 7 in every fourth cell, MERGED by OR inside the cell so no two cells share a screen row | mono |

The first adapter `OSAPI_FSX_CAPS` offers, in that order, is used.

**A Taito colour board on VGA** has a colour a CELL: its map's byte at
(H - 20h) x 32 + the column - MAME indexes it from RAM's start, and
2000-23FFh is never shown - its low 3 bits RBG (bit 0 red, 1 blue, 2 green).
Part II's maps are two, OUT 5 bit 5 picking; Lunar Rescue's one. OUT 3 bit 2
turns every cell red. The DAC's registers for pixel values 0-7 - through
mode 12h's attribute palette, where 6 is 14h - get the eight colours, all
three planes are written, and SET/RESET zeroes the planes a cell's colour
has not got, so a cell is still ONE write a byte; a cell whose colour
differs from the last costs one OUT. A change of map or of red redraws every
cell. Hercules and CGA draw them in black and white.

### 4.4 Entering

A mode set clears the screen, so entry lists all 896 cells (`ar_dirtyall`)
and draws them.

### 4.5 The sound

Section 8: the board's sound bits drive a Sound Blaster stream, the FM chip
or the speaker, whichever the machine has and the Control Panel sends tones
to.

### 4.6 The landscape boards

Gun Fight and Sea Wolf's monitor is the right way up (MAME ROT0): a video
byte is 8 pixels ACROSS one of 224 lines, bit 0 leftmost. A cell is still 8
lines of one byte column, so the write list and its keys are unchanged; the
drawer (`ar_cellh_*`, `arcland.inc`) pushes the cell's 8 bytes and puts each
line's byte doubled across (`ar_dblr`, the doubling table bit-reversed):

| adapter | where | how |
|---|---|---|
| VGA | 512 x 448 from (64, 16) | every line twice; plane 0, DAC 1 white |
| Hercules | 512 x 224 from (104, 62) | 1:1 |
| CGA | 512 x 200 from x 64 | a cell's lines 6 and 7 MERGED (OR) in 24 cells of every 28; bands 3, 10, 17 and 24 keep 8 |

**Sea Wolf's cabinet LAMPS** - its four torpedoes, READY and RELOAD (OUT 2) -
are boxes under the picture on VGA and Hercules, lit or outlined, drawn when
they change; CGA has no room under its picture. The explosion lamps (OUT 1)
are not shown.

---

## 5. Time

### 5.1 The interrupts run on the PC's clock

The board raises RST 1 at mid-screen and RST 2 at VBlank, 59.54 times a
second (MAME's `mw8080bw`: 4.992 MHz pixel clock, 320 x 262). The host
delivers them on **`pit_now`**, alternating every `AR_HALF` = 10,020 PIT
counts, and **does not count 8080 cycles at all**. Space Invaders keeps its
game time in those two handlers and spins in its main loop between them, so
a host too slow for the whole of it still runs the GAME at full speed: it
goes round the main loop fewer times a frame (MEASURED: 3.85 at a real
8080's speed, tools/arcsim.py).

Delivery is at the end of a run, when the interrupt is due and `ST_INTE` is
set: the 8080 PC goes onto the guest's stack, `ST_INTE` clears and the run
resumes at n x 8. If `ST_INTE` is clear, `ST_PEND` says so, and the EI that
sets it ends the run at its next poll point (`mov bp,1`, with `ST_EIX` set).
More than two frames behind, the schedule is dropped to now, never raced.

**A masked interrupt is not lost, and what waits for it is per board.** On
the 8080 boards the frame's clock goes on - the period advances, the frame
hook draws - and the interrupt waits (`ar_pend`) until the run its EI ended:
then it goes in at once, not at the next period. Pac-Man's board is
`BDF_HOLD` (its line is held until taken): its clock waits instead, as it
always has, because its start-up masks the interrupt through a RAM test that
rewrites the whole screen, and a frame drawn each period there crawled at 3
a second on the 286. Neither is free to swap: holding the 8080 boards' clock
ran Gun Fight and Sea Wolf at 40 frames a second on the 5150, and losing the
interrupt (as wave 7b briefly did) left Pac-Man in its RAM test.

### 5.3 The boards

The loop is one loop for every board; a BOARD is the table of what differs
(`ar_boards`, `BD_*`): its parts, its program's length, a reset proc for its
own cells, a setup proc for the bracket (the mode, the tables, the first
picture), its interrupt proc - deliver, or say it is masked - and its frame
proc (picture, sound, keys), its PIT period and its interrupts a frame,
how many frames behind it may fall before the loop drops one (`BD_LATE`;
0 is two, the boards whose picture always fits a frame - Q\*bert's is 30,
section 10.5), its sound procs (section 8), and the hooks its pick and its
drop run (`BD_ON`, `BD_OFF`). The loop holds that lateness in 32 bits. Game > a board's name selects one, which reads its parts and
powers it on. A coin, a start and Esc are TYPED for every board
(`ar_typed`); the held controls are each board's own.

**The three 8080 boards are one board with a WIRING** (`BD_X`, `X_*`): its
input ports at rest (Space Invaders' IN 1 08h; Part II's IN 0 F4h and IN 1
81h, Lunar Rescue's FFh and 89h - MAME 8080bw's), whether it has colour
maps, and its sound: the speaker's cues, the stream's clips, the FM voices,
and which bit's sound LOOPS while held (Space Invaders' saucer, OUT 3 bit 0;
Lunar Rescue's rescue ship, OUT 5 bit 4). The coin is IN 1 bit 0 FLIPPED:
active high on our Space Invaders, as it has always been run, and low on
Taito's boards. Fire, left and right are bits 4-6 of IN 1 and of IN 2 (an
upright cabinet's wiring).

**Gun Fight and Sea Wolf are LANDSCAPE wirings** (`X_LAND`) with procs of
their own: the held keys (`X_KEYS`), the sound latches as the sound layers'
bit word (`X_SDEC`), the cabinet's lamps (`X_LAMPS`). Gun Fight is two
gunmen at one keyboard, each a stick, an aim lever and a trigger on IN 0
and IN 1: the left W A S D, R and F to raise and lower the gun, G to fire;
the right the arrows, Page Up and Page Down, Space. A lever moves a notch
every 6 frames a key is held, through MAME's 7-position encoder table,
inverted; the coin and the start are IN 2 bits 6 and 7. Its sticks are read
every other frame: fourteen keys are fourteen far calls. Sea Wolf's
periscope is a 30-position encoder (Left and Right, a position every 2
frames), its trigger Space; the coin and start IN 2's bits 0 and 1.

### 5.2 A run's length

A **poll point** is the target of every backward branch, plus every PCHL.
Each subtracts its WEIGHT from BP and leaves through `arc_poll` at zero or
below. The weight is its loop's size - the instructions from the head to the
branch, plus 15 for each CALL in them, at most 127 - so a budget counts
roughly 8080 instructions whatever the code's shape: a spin-wait polls every
few instructions and a main loop every few hundred, and a budget counted in
POLLS sized in one ran a hundred times too long in the other (MEASURED,
runs of 85 ms).

The host times each run from the end of the previous one, with one
`pit_now` a run, in 32 bits (a run past 55 ms wraps a word to a small,
plausible number). It doubles the budget below 2 ms and halves it above 6
ms. A run EI ended early teaches it nothing.

---

## 7. The Z80 and the Pac-Man board

### 7.1 The translation

`apps/arcade/arcz80t.py` is section 3's method for a Z80, sharing its
runtime (`arcrt.inc`) and its assembling (`arcaot.settle`, `finish`). The
register map is the 8080's - A=AL, BC=CX, DE=DX, HL=BX, SP=SP, the flags in
FLAGS - and what the 8086 has no register for lives in view cells: IX, IY,
the alternate set (EX AF,AF' and EXX are XCHGs with them), I, IM, the
vector, IFF2. An `(IX+d)` operand is `mov si,[ST_IX] / lea si,[si+d] / and
si,7FFFh` (section 7.4 says why the AND).

### 7.2 What is translated

The walk follows section 3.2's rules, with one exception the Z80 boards
need: **an RST's return site is NOT assumed.** Pac-Man's RST 28h and 30h are
calls followed by inline data (2 and 3 bytes), and RST 20h is a jump table.
`tools/arcpac.py seeds` LEARNS where each RST comes back - the PC a RET (or
a dispatcher's POP / JP (HL)) reaches at the stack depth the RST left, when
at least 90% of that RST's runs do so, always the same distance on - and
writes it as `RST 28 +3` lines in the seeds file; the walk and the
speculative walks follow them. Pac-Man: RST 08h, 10h, 18h +1, 28h +3, 30h
+4, and 20h none. 5,615 instructions, 578 of them speculative.

**RST 20h's tables are read, not learned.** A learned seed is a target the
script REACHED, and an RST 20h table entry is reached only in the state that
selects it: a script that never ate a ghost never put 2 in 4DAFh, and 11DBh
(10B7h's table: 000Ch, 11C9h, 11DBh, 11FCh) had no translation - "no code
at 11DB" in play, halfway through a first level (the owner's field report,
2026-11-06). So `arcpac.py seeds` reads every table of every RST 20h the
run executed, off the ROM: an entry is a word below 4000h, and the table
ends at the first that is not, at a position that ran or that any entry
names (the code after it), or at its own lowest target. 42 targets the
script never reached, the intermissions among them; the code part grew 193
bytes, most of those routines having been translated speculatively with no
entry in the PC table. `tests/arcpacghost.py` (86Box's 286, kern_big) puts
each ghost in state 2 in play: red with the parts from before.

An instruction the translation does not take (IXH/IXL, IN, OUT other than
OUT (00),A, RRD/RLD, CPI/CPD, INI and kin) is a build failure in real code
and drops a speculative walk.

### 7.3 The flags

Liveness runs over five groups: C, Z (S and Z), P (P/V), H, N. S Z H C are
the 8086's own after the ALU ops - the Z80's H on a subtract is a half
borrow, as AF is, unlike the 8080's (section 3.5). P/V is parity after a
logical op, as PF is, and OVERFLOW after arithmetic, so where P is live after
one, OF is copied into PF (`jo` and a LAHF/SAHF pair). The CB rotates set S Z
P, which the 8086's leave alone, so where those are live a `lahf / test /
rcr ah,1` makes them and puts CY back. BIT n is `test` (its Z, S and P are the
Z80's), with CY kept the same way. N is read only by DAA, so it is not kept:
each DAA is DAA or DAS by the last N-setter on every path into it (Pac-Man's
six all follow an add). CPIR and CPDR are LOOPNE, which is the instruction
exactly; LDIR, LDDR, LDI, LDD are helpers that store each byte through the
board's map.

### 7.4 Pac-Man's memory map, in the code

As MAME's `pacman.cpp`: ROM 0000-3FFF, video RAM 4000-43FF, colour RAM
4400-47FF, nothing at 4800-4BFF (reads give BFh: the view holds BFh there),
RAM 4C00-4FEF, sprite attributes 4FF0-4FFF, and at 5000-50FF the inputs
(read: IN0 5000, IN1 5040, DSW1 5080, DSW2 50C0, which the view holds where
the board puts them) and the writes: the latches 5000-5007 (bit 0 of 5000
enables the VBlank interrupt), the WSG's voices 5040-505F, the sprites'
positions 5060-506F, the watchdog 50C0. A15 is not decoded.

A STORE is classified by its address's high byte: RAM (4C-4F) is one MOV, the
branch taken; video (40-47) compares first and lists its tile, as section
4.1; I/O (50 and up) goes to `arc_iow`, which files what the game wrote at
`PM_LATCH` 5100h, `PM_SOUND` 5140h and `PM_SPRXY` 5060h; ROM and 4800-4BFF
take nothing. A fixed address is classified by the translator. **A15:**
Pac-Man's text routine (2C65) points IX at C3D4 for 43D4, so an (IX+d)
address is masked to 7FFFh. HL, BC and DE are not; the lockstep would see it.

### 7.5 Nothing below the guest's SP

**The Z80 translation CALLs nothing and PUSHes nothing of its own.**
Pac-Man's RAM test pushes words, steps SP down with DEC SP and POPs them back
- it READS below its own SP - and the 8080 translation's CALLs (a poll's,
a helper's) write exactly there. So a poll point is `mov si,PC / jmp
arc_pollsi`; a helper is `mov word [ST_RET],.back / jmp helper`, returning
with `jmp [ST_RET]` (`ST_RETIO` for `arc_iow`, which the block helpers call
in turn); what a helper saves goes to `ST_SAVE`; EX (SP),IX swaps through
the IX cell with XCHGs. The host's interrupt is the only thing that pushes,
as the board's own does.

### 7.6 The reference and the lockstep

`tools/arcz80.py` is a Z80 in Python, every documented instruction and flag
and the undocumented SLL; `tools/arcpac.py` the board around it, with a
`stats`, `seeds` and `digest` like `arcsim.py`'s. `tests/arclock.py --board
pacman` runs the code part in lockstep with it as section 6.1 says, with the
Z80's own rules: IX and IY compared too; a repeating block instruction
(LDIR and kin) one boundary on both sides, its interrupt at its end; a HALT
waiting for the model's interrupt; flags compared where read, P/V included.
**7,418,709 instructions over 1,200 frames - boot, the attract mode, a coin,
a start and play - all agree.**

### 7.7 The picture

VGA only (640x480x16; the board has exactly 16 colours, which go into the
DAC once, at the indices the attribute controller's default palette sends
them to). The 224 x 288 portrait screen is drawn twice as wide and 1.5 times
as tall - 448 x 432, centred: each even row twice.

**By damage, in boxes of tiles.** A tile the Z80 wrote (video or colour RAM:
the key is the index, the flag at `PM_TFLAG`) is composed alone; a sprite that
moved or changed is composed with the tiles its old and new places cover,
one box when they fit 4 x 4 tiles. More than 96 tiles at once (a clear) and
the whole screen is composed in 4 x 4 blocks.

**Composed in planes.** `apps/arcade/arcpmgfx.py` derives from the ROM, at
build time, the tiles and sprites TURNED to portrait as value MASKS (a row's
pixels of value 1, 2, 3), each palette's plane SELECTORS (FFh where value
v's colour has plane p's bit) and OPACITY, the DAC triples, and the two tile
maps. A tile row's byte for plane p is (m0 AND S0p) OR ... OR (m3 AND S3p); a
sprite row is the same in words, shifted into the box (two shifts by CL),
cut into each plane through its opaque mask. Sprites 7 to 0, so 0 is on top;
clipped to the playfield's rows 16-271 (MAME's spriteclip). An attribute's
bit 0 is the x flip and bit 1 the y flip, as in MAME's driver (x being the
board's unturned axis); the x flip is the table's rows reversed and the y
flip its second copy. Sprites 0-2 sit one pixel over (MAME's xoffsethack).

**Cached, and written only where the glass changes.** Each screen tile's 32
plane bytes are kept as composed (part 7) and copied into the next box that
covers it, until the Z80 writes the tile. The box goes to the screen a plane
at a time, each byte compared with the SHADOW of the glass (part 7) and
written - doubled across, and down on an even row - only when it differs.
MEASURED on 86Box's 286, CS:IP sampled in play: VGA writes and the tiles'
planes were 24% and 18% of the machine before these two; with them, the
board's frame rate holds with no frame dropped in play.

### 7.8 Sound and keys

The Namco WSG is section 8's: mixed whole on a Sound Blaster, three FM
voices on an OPL, the loudest voice's pitch on the speaker. The arrows are
the joystick (IN0 bits 0-3, held, `OSAPI_KEY_DOWN`); a coin (IN0 bit 5) and
the starts (IN1 bits 5, 6) are typed. All active low.

### 7.9 The ROM

**Never fetched**: no public archive.org item carries Pac-Man
(`arcade_pacman`, `arcade_puckman` and `arcade_mspacman` are dark).
`make arcaderom-pacman GETPACFROM=<dir-or-zip>` checks the owner's set chip by
chip against SHA-256 pins (CRCs as MAME's `pacman`) and writes
`build/arcade-rom/PACMAN.ROM`; while it exists, `make arcade` builds the
Pac-Man board in (`-DARC_PACMAN`), and without it, Space Invaders alone.

## 8. Sound (`arcsnd.inc`)

One ROUTE a bracket, chosen when it opens and given back when it closes:
`OSAPI_SND_CAPS` answers, and BL = 0 (the Control Panel sends tones to the
speaker) means the speaker whatever card there is - WireFleet's rule
(SPEC.md 114.4). Otherwise the first a board offers and the machine has:

| route | needs | what it plays |
|---|---|---|
| 2 the STREAM | `SND_CAP_PCM_BG` and `SND_CAP_EXTBLK`, the 5KB ring claimed, verb 0 taking it | the board's own mixer, every sound at once |
| 1 FM | `SND_CAP_FM`, `SNDV_IDENT` answering `SND_SIG`, `SND_FM_CLAIM` | the board's voices as OPL channels |
| 0 the speaker | nothing | one tone at a time through `OSAPI_SND_TONE`, as before this section |

A route that fails on the way in falls to the next, so a machine always
gets the best it can say, and nothing is retried inside the bracket.

### 8.1 The stream

An EXTERNAL RING (SPEC.md 34.5.3) in a claim of the package's own: 4,096
bytes of unsigned 8-bit mono and the two control words, `OSAPI_MEM_CLAIM_DMA_HI`
(5KB, the whole head page-safe), born pinned and never declared movable -
it lives exactly as long as the bracket. The card's block is 256 bytes
(`SND_OPENF_BLKSH` code 3). The open stages 2,048 bytes of silence (80h):
verb 0 takes no less than the regime's FULL half, whatever block the ring
asks for, and the queue falls to the lead within a fifth of a second.

**Once a frame**, after the picture, the host tops the ring up to a LEAD of
768 bytes past the consumed count - three blocks, 70 ms at 11 kHz - asking
the board's MIXER for what is missing, in at most two calls where the ring
wraps, and only then writes the new total. Filling to a lead rather than a
frame's worth follows the CARD's clock, so the two crystals never drift
apart: a frame that comes early mixes less. More than 1,024 missing (a slow
frame, a pause in the window) mixes 1,024. The board's mixer WRITES the
bytes it is asked for, unsigned around 80h: the sum of its voices' signed
samples, whose ranges are chosen so that it never wraps a byte.

**An underrun resumes**: verb 3 only once the consumed count has stood still
for 12 frames (a block is a few frames of sound, so a stall is the card's -
and asking every frame was a measurable share of an 8088); state 1 and a
block queued is
verb 1 with the total, the Video Player's `vp_skeep`. State 2 (the
watchdog gave up) falls to the speaker for the rest of the bracket.

**The close**, on every way out of the bracket (Esc, a miss): verb 2, then
the claim freed.

### 8.2 FM

The chip claimed whole in OPL2 mode (SPEC.md 34.12), each channel's patch
written once, then each frame at most an F-number, a key and a carrier
level a channel, and only the registers that changed (a shadow of each):
RealDMG's `rw_fmput` (REALDMG-SPEC 17.1). A pitch is Hz to block and
F-number, f = fnum x 49,716 / 2^(20 - block); a level is a volume 0-15
through `rw_fmtl`'s table. Verb 3 gives the chip back at the close.

### 8.3 Space Invaders

**Clips, synthesised.** The real board's sounds are analog circuits, and
emulators play somebody's recordings of them; this one ships none.
`apps/arcade/arcsfx.py` SYNTHESISES nine clips, deterministically, from a
description of what each circuit does - a noise burst through a falling
filter for the shot, a triangle swept by a slow triangle for the saucer -
into a lazy part (`OP_LAZY | OP_COMP`, the last part, as every lazy one must
be) fetched on the desktop before the first bracket whose stream could open -
`op_fetch` may toast, which a bracket in a foreign mode may not - and kept
while an 8080 board is picked: picking Q\*bert or Pac-Man drops it
(`ar_select`), its claim being one Q\*bert's pick cannot spare (13.3). 8,000 Hz, signed 8-bit, peaks within +-42, so the
three that may sound at once cannot wrap. **An 8086-class machine plays every
other sample, at 4,000 Hz** (`OSAPI_CPU_INFO`): at 8,000 the clips cost the
5150 a half frame dropped every second and a half of play.

| bit | clip | how it plays |
|---|---|---|
| OUT 3 bit 0 | the saucer | LOOPS while the bit is held, from where it was |
| OUT 3 bit 1 | a shot | once, from its rise |
| OUT 3 bit 2 | the base hit | once |
| OUT 3 bit 3 | an invader hit | once |
| OUT 3 bit 4 | an extra base | once |
| OUT 5 bits 0-3 | the fleet's four steps | once each |
| OUT 5 bit 4 | the saucer hit | once |

Space Invaders Part II's sound bits are Space Invaders' (MAME plays it the
same samples). **Lunar Rescue's** are its own (MAME 8080bw_a.cpp
`lrescue_sh_port_*`): OUT 3 bits 0-4 the thrust, a shot, the death, an alien
hit, the bonus ship; OUT 5 bits 0-2 a footstep high and low and the count of
the men saved, bit 4 the rescue ship (on while held). `arcsfx.py`
synthesises five clips of its own - the thrust, the two footsteps, the
count, the rescue ship's beep as a seamless loop - and it shares the shot,
the hits and the extra ship. OUT 5 bit 3 is a ONE-BIT tune the CPU plays by
toggling it, which a frame's latch cannot carry: not played.

**Gun Fight**'s sound latch holds a code in its top four bits, inverted - a
left or right shot, a left or right hit - which `gf_sdec` turns into four
bits; `arcsfx.py` synthesises the shot (a crack) and the hit (a falling
cry). **Sea Wolf**'s OUT 5 bits 0-4: the ship hit (Space Invaders' base hit
clip), a torpedo, the dive, the sonar's ping (three of its own), a mine
hit (the invader hit's).

A rising bit starts its clip again from the top; up to three clips sound at
once, and a fourth takes the slot of the one nearest its end. **The mix is one
pass**: a chunk runs to the first clip's end through a kernel for that many
clips (0-3) and that step, the clips at DS:SI, DS:BX and DS:BP, the sum
XOR 80h stored - nothing read back from the ring, no silence laid first. OUT 3 bit 5 is
the amplifier: off, the mixer adds nothing and FM keys every channel off.

**FM**: channel 0 the fleet (a short decaying bass note on each step's
rise), channel 1 the saucer (a tone swept 600-1,000 Hz and back each 16
frames while held), channel 2 the noises (RealDMG's noise patch, a
modulator at x15 and full feedback, its level falling frame by frame:
shot, invader hit, base hit), channel 3 the extra base and the saucer hit,
from the speaker's own note tables.

**The speaker** keeps section 4.5's cues as they were.

### 8.4 Pac-Man: the WSG

The board's three voices (5040-505F, the latches' nibbles): a 20-bit
accumulator stepped by the frequency at 96 kHz (voice 0's 20 bits, voices
1 and 2's 16 shifted up by 4), its top five bits indexing one of eight
32-sample waveforms of 4 bits in the sound PROM (82s126.1m), times a 4-bit
volume. 5001 bit 0 is the sound's enable.

**On the stream**, at 10,989 Hz (the DSP's time constant 165), each voice
keeps a 16-bit PHASE, stepped per output sample by f16 x 96,000 / 10,989 =
(f16 x 2,236) >> 8, f16 being the frequency's top 16 bits. Its high byte
indexes a 256-byte table, each of the waveform's 32 samples (w - 8) x volume
x 21 / 64 eight times over - built again only when the voice's waveform or
volume changes. A voice adds +-40 at most, three +-120. A voice at volume 0
or frequency 0 adds nothing and keeps its phase. The first sounding voice
WRITES its samples and the others add theirs in, four samples a pass with the
pointer moved once; silence is written only when none sounds.

**On FM**, channels 0-2 are the voices: Hz = f16 x 375 / 256 and the level
from the volume; one patch, near a sine, for every waveform.

**On the speaker**, the loudest voice's pitch, as before.

### 8.5 What it costs, measured

- **Space Invaders on the 5150's stream** (MartyPC, CS:IP sampled in play,
  fire held): the clips' tick, kernels and the stream's frame together about
  4% of the 8088 - which was 46 half frames dropped in 30 s at 8,000 Hz with
  the first mixer (silence laid, each voice added in place, the driver asked
  for the stream's state every frame), and is **0 or 1 in 20 s of play** now:
  at 4,000 Hz, one pass, and the driver asked only once the consumed count
  stands still for 12 frames. `tests/arcsound.py`, row `arcsound`.
- **Space Invaders on FM** (the Hercules XT with an AdLib): 59.4 frames a
  second, at most one dropped in 20 s.
- **Pac-Man on 86Box's 286 with a Sound Blaster**: the stream and FM alike
  drop a few frames a second in the heaviest chase and death scenes, where
  the same machine with no card drops none. It is SOUND.DRV loaded at all,
  not the route (FM with no stream drops the same), and 86Box is not
  cycle-accurate: the proportion, not the count, is the claim.

## 6. Verification

`tools/arcsim.py` is the reference: an 8080 and the board in Python, the two
interrupts at fixed T-states, a scripted coin, start and play. `stats` prints
the per-frame load, `seeds` the entry points of section 3.2, `digest` the
SHA-1 of VRAM after a frame and its screen as a PBM.

### 6.1 The lockstep (`tests/arclock.py`, row `arclock`)

The code part runs under Unicorn's 8086 beside the model. At every 8080
instruction boundary - every PC-table entry the code reaches - the two must
agree on the PC, A, BC, DE, HL and SP; on the flags wherever the 8080 is
about to read them; and once a frame on every byte of RAM and VRAM, except
the dead space under the stack (2300h up to SP, where no data lives and the
translation's own CALLs push) and the one AC bit of section 3.5 in a pushed
PSW. Interrupts go in on the same instruction in both, at the model's
T-states; flags DEAD at the interrupted instruction are taken from the 8086
side, since the translation may leave garbage there and the handler's PUSH
PSW / POP PSW carry it through harmlessly. An idiom is one boundary on the
8086 side and many in the model, which runs the whole loop at once and takes
an interrupt at its exit. Result: **2,293,923 instructions over 600 frames,
all agree.**

### 6.2 On the glass

MartyPC, `os8088_xt_vga` and `os8088_5150_cga`, with the counters of section
2.1 read out of guest memory. In play on the 5150 (a 4.77 MHz 8088):
**59.4-59.9 frames a second delivered**, the board's 59.54, with 0.4 to 2.2
main-loop passes a frame against a real 8080's 3.85.

---

## 10. The native 8088 board: Q\*bert (`arcqb.inc`, `arcn88.asm`)

### 10.1 Why it is not translated

Gottlieb's GG-III board is an 8088 at 5 MHz - the one arcade CPU this
machine IS. Q\*bert's program lives flat in one 64KB space: RAM 0000-2FFFh,
sprite RAM 3000h, video RAM 3800h, the palette 5000h, the inputs and
outputs 5800-5807h, the program A000-FFFFh. Its reset sets every segment
register to 0 and nothing else in it names a segment (`tests/qbnative.py`:
the segments never move). So it runs AS IT IS, in G, a 64KB claim of the
host's, with G in CS, DS, ES and SS: the board's addresses are G's offsets.
MEASURED (`tests/qbnative.py --cycles`, an 8088 cycle table over Unicorn):
the game's work is a median **7,500 cycles a frame**, 13,000 at the 90th
percentile and 54,000 at the start of a scene, of the **77,700** a 4.77 MHz
8088 has at the board's 61.42 Hz - about a tenth. Its own CPU was a 5 MHz
8088 and it used a fraction of it.

### 10.2 The patch and the N88 core

`apps/arcade/qbert.patch` names what has to change, as addresses and kinds,
and `arcqbgfx.py` applies it to the ROM part, REFUSING unless the bytes at
each address have the shape its kind names (an address is not a ROM byte;
a ROM that is not the one the list was found in fails the build):

- `nop`: the reset's three segment loads (B25E-B263h);
- `port`: every store to 5800-5807h moved 8 up, to 5808-580Fh - the
  watchdog, the sound latch (5802h, at 580Ah) and the outputs (5803h, at
  580Bh: bit 0 the background's priority, 5 the knocker). The board READS
  its inputs at the same addresses, and the host fills them there before
  every run (5800h the DIPs at MAME's defaults, 5801h the coin and starts
  with the service switch off, 5804h the joystick), so a store must not
  land on them;
- `yield`: the game's two waits for the frame (`cmp ax,[0080h] / jz $-4`,
  the main loop's at B36Dh and the service mode's at DB77h) become
  `call far` + NOP into the N88 core. The host writes the far pointer at
  power-on, from the core's header (`AH_YIELD`) and its segment.

**The N88 core** (`arcn88.asm`, part 16, 103 bytes) is the door every core
has: `AH_ENTER`, far-called with AX = the view. It keeps the host's SS:SP
in the view, puts the game's stack back (`NV_GSP`, SS = G) and resumes the
game - or, when the host has delivered the NMI (`NV_NMI`), starts in the
game's own handler, through the vector the game wrote at G:0008h, with an
interrupt frame that IRETs into the resume. The yield pushes FLAGS and the
seven general registers onto the GAME's stack, keeps its SP, and returns to
the host's call with `ST_WHY` = `ARCX_HALT`. The host loop treats that as
any board's HLT (section 5.1): the next run follows the next NMI. One run
is one frame of the game; the game is cooperative and cannot be cut short.

### 10.3 The game runs with interrupts OFF

A run starts with CLI and the game has no STI (nor CLI, nor INT, nor IN or
OUT - `tests/qbnative.py` would see a store at 5800-5807h, and the walk that
wrote the patch saw none of the rest). So nothing of the host's ever lands
on the game's stack, below which is its own RAM, and no kernel interrupt
runs with SS = G: the question the plan asked - how deep os8088's ISRs reach
below a guest SP - does not arise. The cost is the run's length with the
PC's interrupts held: 1.6 ms a frame typically, 11 ms at the start of a
scene, on the 8088. The keyboard and the timer keep what arrives; a serial
mouse is not read in the bracket. **A game that never reached a yield would
hang the machine**, which no Esc could reach; Q\*bert's every frame ends at
one (`tests/qbnative.py`, 3,000 frames of attract mode and play).

### 10.4 The picture

**VGA only**, 640x480x16 (`FSXM_VGA12`): the board has 16 colours and a
pixel's value is its DAC register, so a palette change - 5000-501Fh, two
bytes a colour, green and blue in the even, red in the odd, four bits each
(MAME's resistor weights are taken as linear) - is 16 DAC writes and never
a redraw. Hercules and CGA refuse with a toast, as Pac-Man's do.

The cabinet's monitor is on its side: MAME rotates the board's 256 x 240
picture 270 degrees, so the board's (x, y) is the portrait column y, row
255 - x. `arcqbgfx.py` turns the tiles and sprites once: a tile's portrait
(c, r) is its own (7 - r, c), a sprite's (15 - r, c). The 240 x 256
portrait is drawn **twice as wide and 15/8 as tall** - 480 x 480, centred
from byte 10 - by a row table (`qb_yoff`): 7 rows of 8 are two screen rows.

**What changed is found by comparing**, because a native store is not
trapped and there is no write list: the board's 960 visible video RAM bytes
against a copy (REPE CMPSW), its 256 bytes of sprite RAM against another
(an unchanged sprite RAM skips the sprites altogether), its palette against
a third. A changed tile marks its 4 x 4 box (`qb_dirty`), and each frame's
draw composes marked boxes, in turn from where the last stopped, **until
the next frame is due** (one at least): a 286 draws a change of scene in
one frame, an 8088 over as many as it takes, while the game keeps its time.
A change of the background's priority repaints the whole screen at once
instead: a box left for later would show the old order exactly where a
sprite crosses a tile, and it happens a few times a minute.
A sprite that moved or changed code has the box it left and the box it is
in redrawn - one box when the two fit 4 x 4 tiles. Tiles from MAME's
`bgtiles` (Q\*bert's character RAM is unused: `init_romtiles`), sprite code
= the RAM's byte XOR FFh, at column [n] - 13 and row 244 - [n+1], clipped
to rows 0-247 (MAME's x >= 8).

**A box is COMPOSED in planes in RAM** - its tiles' four plane bytes a row,
then each ACTIVE sprite over it in RAM order so that a later one is on top
(an active sprite has a code with pixels and is on the screen: the board
parks its unused ones at 0,0 with a blank code), each sprite row's opaque
mask and four plane words shifted into the box by `ar_shift` (shared with
Pac-Man) and cut in - and, when 5803h bit 0 gives the BACKGROUND priority,
the tiles' own non-zero pixels again over the sprites. Then it is put on
the glass ROW BY ROW: each plane's bytes passed over by REPE CMPSB where
they equal a 30KB SHADOW of the glass, the others written doubled across,
and a row the table makes two screen rows copied down through the latches
(write mode 1, a byte read and a byte written for all four planes) when any
of it changed. No pixel is written twice (PERFORMANCE.md rule 2).

### 10.5 Time

The board's NMI is MAME's vertical blank: 5 MHz of pixels, 318 x 256, 61.42
Hz - a PIT period of 19,427. One NMI a frame, delivered by the host loop's
clock as every board's interrupt is (section 5.1).

**On an 8088 the picture skips frames so that the game does not.** A box
costs the 8088 about 130,000 cycles (MEASURED, MartyPC's VGA XT, a 3 x 3
box: some 65,000 to compose, as much again to put), so two moving sprites
are three frames' time and a whole screen 107. The frame hook therefore
draws only when it is not already late for the next frame - at most 20
frames in a row go undrawn - changed tiles wait in their boxes (section
10.4), and the board may fall 240 frames behind (`BD_LATE`) before the loop
drops one, while the game's own frames, at 8,000 cycles, catch up at about
four times their rate. MEASURED, `tests/arcspeed.py --hz 61.42`:

| machine | frames delivered | dropped | boxes drawn |
|---|---|---|---|
| MartyPC, 4.77 MHz 8088, VGA (`os8088_xt_vga`), 30 s of rough play | **61.5 a second** | 0 | 26-29 a second |
| 86Box, the owner's 286 profile (`at286_vga`), 10 s | 62.7 a second | 0 | 81 a second, every frame drawn |

Repainting a scene's worth of changed tiles in one draw, as the first
version did, ran the 8088 at 45 frames a second with 3 dropped over the
same 30 s.

**A pick costs the 8088 most of 15 seconds** from a floppy (MEASURED):
4.6 s to read `QBERT.ROM`'s 64KB, 2.4 s for its CRC-32, 4.2 s for
`qb_build` (its sprite turn unrolled: 7.6 s before), and the first
picture. The release plan's R2 takes the read and the CRC on; on a 286
with a hard disk it is a blink.

86Box is not cycle-accurate: the 286's row says the picture keeps up, not
by how much.

### 10.6 Sound

Section 13: the board's sound board runs live on a 386 or better, into the
Sound Blaster stream. The game's command at 5802h lands at 580Ah, where
`qbs_tick` takes it once a frame. The knocker (5803h bit 5) is not sounded,
nor is the Votrax speech.

### 10.7 Keys

The joystick is turned 45 degrees on the cabinet. MAME's mapping holds:
Right is down-right, Left up-left, Up up-right, Down down-left (IN4 bits
0-3, active high, held, `OSAPI_KEY_DOWN`) - and the keypad's corners are
the four ways as they look on the screen: 9 up-right, 3 down-right, 1
down-left, 7 up-left. A coin and the starts are typed (`ar_typed`): IN1
bits 2, 0, 1.

### 10.8 Verification

- `tests/qbnative.py` (Unicorn, no emulated machine): the shipped ROM part
  run as the N88 core runs it, 3,000 frames of attract mode, a coin, a
  start and moves. Every frame ends at a yield, no store lands on
  5800-5807h, the segments stay G. A patch line taken out fails it (`a
  store of 00 at 5803, from B034`).
- `tests/qbglass.py` (MartyPC's VGA XT): three moments of the attract mode
  - one with the background's priority set - stopped the instant a frame
  was DRAWN, every screen row of the picture read out of the VGA's four
  planes and compared with MAME's picture of G's memory. 0 pixels differ.
  With the priority pass and the latch copy both taken out, every look
  failed: 4,430 and 9,354 pixels where the priority is clear (the doubled
  rows) and 40,102 where it is set.
  (The card's rendered frame cannot be the instrument: it is the last one
  the raster FINISHED, and a box put after the beam passed is not in it.)
- `tests/arcspeed.py --game Q*bert --hz 61.42 --machine os8088_xt_vga`:
  the game's time held on the 8088 (section 10.5).

### 10.9 Why the probe was a probe

The plan (section 8.2) made this conditional: one wave to boot the attract
mode natively and decide. The decision was the measurement of 10.1 - the
game a tenth of an 8088 - and the cost of the board is the picture's, which
section 10.5 meets by skipping frames of it on the 8088 and not at all on a
286.


## 11. The frontend (`arcface.inc`, `tools/arcart.py`)

ARCADE's window is a GAME BROWSER in ONE generic cabinet: a backlit
marquee, the list of games, the selected game's screen and card, and a coin
door of buttons. The owner's rule: one cabinet for every machine, no art
per game. docs/plans/ARCADE-RELEASE-PLAN.md sections 3 and 4 are the
design; this is the contract.

### 11.1 Opening is a look, not a load

Opening ARCADE reads no game. It LOOKS for each game's `<SET>.ROM` with
`OSAPI_FILE_FIND`, in the folder ARCADE stands in and then each place in
`ARCADE.DAT` (1.5), and reads each found game's `<SET>.PIC` (11.4). Each
game in the list gets one STATE:

| state | when | the tag |
|---|---|---|
| READY | its `.ROM` was found and this display runs it | `READY` |
| ROM NEEDED | no `.ROM` found | `ROM NEEDED` |
| NEEDS VGA | Pac-Man and Q\*bert, on a display without `FSXM_VGA12` (`OSAPI_FSX_CAPS`, a fact) | `NEEDS VGA` |

A game is LOADED (read, checked, built: 1.4, 2.4 of the plan) when it is
played, and stays loaded until another one is. **Play** on a game that is
not READY says why in a toast and does nothing else. After an import the
look is taken again.

### 11.2 Layouts

The content box is 8-aligned and every field sits on the byte grid, so
every band is blitted at a multiple of 8 and no band is shifted (the
retro-face skill's first lesson).

| layout | content | where | what |
|---|---|---|---|
| FULL | 592 x 280 | VGA, EGA, Hercules (a desktop of 300 rows or more) | marquee, list, screen, card, coin door |
| COMPACT | 592 x 124 | CGA | a short marquee, the list, the card; no screen |

The window is created at its layout's size and centred, from `OSAPI_VIDEO`.
The faces as built: `docs/arcade/face-vga.png`, `face-herc.png`,
`face-cga.png`.

**The look happens BEFORE the window**: a file call inside the entry proc
may compact the heap, and once `OS88_REGION_MOVABLE` is declared the
package's own region can move under the entry proc's feet (MEASURED: the
first build looked after `OSAPI_WM_CREATE` and ran on in an emptied
segment). Only the VGA check, which needs the window and no disk, comes
after it (`af_vgacheck`).

### 11.3 Drawing

Two themes, as role tables (`af_tcol`, `af_tmono`): colour where
`OSAPI_VIDEO` answers DH > 1, black and white otherwise - a dark cabinet,
white on black, the selected row the one inverted field (SPEC.md 39.4: no
grey, no dither in a 1bpp face). MEASURED (rows `arcface`, `arcfaceherc`,
`arcfacecga`): a move of the selection is 4 to 6 displayed frames with no
transient pixel, and the face after moves is pixel for pixel the face after
a whole repaint. A full paint lays the whole ground once
(`OSAPI_WM_OWNBG`); everything after it is FIELDS: a list row, the screen,
the card, a button, the counter. A field is one or a few calls that write
each pixel once - a text run is `OSAPI_FONT_RUN` at the field's full width,
small caps and pictures are composed into a band and put down in one
`OSAPI_GFX_BLIT1`. Moving the selection redraws the two rows that changed,
the screen and the card, and nothing else.

The art is generated, never drawn by hand (`tools/arcart.py`, the retro-face
skill's shape): the marquee's letters as rows of `#`, a 5x7 small-caps face,
the cabinet's coin-door lamps. `--selfcheck` and `--png` as CDRack's. No
game's logo, title art or character appears anywhere; titles are text.

### 11.4 The screen: a picture the game itself drew

The only picture of a game is one its own ROM drew on this machine. When a
game is left (Esc) and has no picture yet, ARCADE samples the screen it was
playing on (`af_snap`, still inside the bracket) into a 120 x 144 1bpp
picture and writes it as `<SET>.PIC` (4-byte header `'AP'`, width, height,
then the rows, 15 bytes each: 2,164 bytes) in the folder its `.ROM` was
found in. A disk that refuses the write leaves the picture in memory for
the session, and costs nothing else.

LIT means "not black on the glass". On a 16-colour mode a colour number is
taken plane by plane, and the colour numbers that are black are found by
reading each one's DAC register through the standard Attribute Controller
map every board here keeps (`ar_acmap`). It is not "any plane set": Space
Invaders' colour overlay lives in a plane of its own whose colour numbers
show black, and sampled that way the shields' band came out solid. The
Graphics Controller is put in read mode 0 first (a game's latch copies
leave it comparing colours). On a 1bpp mode a set bit is lit. The lit
COLUMNS are found from every 16th row, then 120 x 144 points are taken
across them and the screen's full height. MEASURED on MartyPC: the
picture is kept within 3 guest seconds of Esc on the VGA XT (VGA's planes),
1.5 on Hercules and CGA, the bracket's own exit included.

The screen panel shows the picture in the phosphor colour (white on mono),
or the game's name in small caps when it has none. The pictures are read
at the look (11.1) into one claim of 17KB, a slot a game, so moving the
selection reads no disk.

### 11.5 Keys and the mouse

| key | does |
|---|---|
| Up, Down | the selection, one row |
| Home, End | the first, the last |
| a letter | the next game whose title starts with it |
| Enter, Space | Play |
| Insert | Add ROM sets... |

A click on a row selects it; a click on the selected row plays it. The coin
door's buttons are PLAY, ADD ROMS and SETTINGS (R4; until then greyed).
The Game menu keeps Play, Reset and Add ROM sets..., and lists the games, a
pick from it selecting and playing as before.

## 12. Settings, scores, pause and help (`arcopt.inc`)

### 12.1 The doors

Every entry into a game (`ar_door`, at the top of `ar_loop`) empties the
BIOS keyboard buffer and clears the typed coin and start bits and any hold.
A key typed on the desktop, or a coin pulse left from the last game, cannot
reach the board: the owner's one unexplained Q\*bert run (a game over the
moment the intro ended) fits a start left over from the game before.

### 12.2 Pause and help

In a game, **P** holds the board and **F1** holds it with the game's card
(11.5's) shown. Held, the board runs no instruction and takes no interrupt,
its sound is closed (`ar_snd_close`), and a box over the picture says so:
white on black in the system font, whose glyphs (32 to 127) are copied
before the bracket (`ar_ovinit`), since the desktop's drawing calls are off
limits in a game's mode. The box is written straight into the framebuffer
as `ar_fsi` describes it: all four planes at once on VGA, the banks on
Hercules and CGA. Any key plays on and Esc goes back to the list; either
way the board's own setup (`BD_SETUP`) draws the picture again, and the
frame clock starts afresh from that moment, so no frames are owed.
MEASURED (row `arcopt`): no frame in two held seconds, after both P and F1.

### 12.3 The DIP switches, in words

**SETTINGS** (the coin door's third cap, or F2) turns the card into the
selected game's switches: one line a setting, its name and its choice, the
one being changed marked `>`. Up and Down pick the setting; Left, Right,
Space and Enter change it; Esc, F2 or the cap again leave. The choices are
kept in `ARCADE.DAT` (a byte a game) and set into the board's switch byte
at every power-on (`ar_dsapply`, after `BD_RESET`), leaving the bits ARCADE
does not offer as the board's reset made them. A game in memory is powered
on again when its switches change.

Only switches whose meaning is certain are offered, from MAME's own
definitions:

| game | byte | settings |
|---|---|---|
| Space Invaders | IN 2 (`V_IN2`) | BASES 3/4/5/6 (bits 0-1), BONUS AT 1500/1000 (bit 3) |
| Pac-Man | DSW1 (`PM_DSW1`) | LIVES 1/2/3/5 (2-3), BONUS AT 10000/15000/20000/NONE (4-5), DIFFICULTY (6), GHOST NAMES (7); coinage left at one coin a credit |

The other games have none ARCADE offers yet, and their SETTINGS cap is
drawn unlit. **Not done: key remapping.**

### 12.4 High scores

A game's best is a few bytes of its RAM (MAME's `hiscore.dat` idea, written
here as addresses alone): Space Invaders' at 20F4h (2 bytes), Pac-Man's at
4E88h (4). As a game is left, a nonzero best that differs from the kept one
is kept (`ar_hssave`) and `ARCADE.DAT` written. After a power-on, from frame
300 to frame 1,200, the kept best is put back whenever the game's own reads
zero (`ar_hsput`) - its power-on test clears RAM, and may do so after a
first put. **Not done: Q\*bert's table**, which lives in its battery RAM
with the operator's settings and wants more than a range of bytes.

`ARCADE.DAT` is now one 73-byte record: the places (1.5), each set's import
drive, each game's switches and each game's best.

`tests/arcopt.py` (row `arcopt`): BASES 5 kept in `ARCADE.DAT` and in IN 2
at the next power-on; P and F1 each hold the board for two seconds and a key
plays on; a best put in RAM is kept as the game is left from a hold with
Esc, and back after Game > Reset. Red (`0000`) without `ar_hsput`.

## 13. Q\*bert's sound board, live (`arcqbs.inc`, `arc6502.inc`)

### 13.1 Measured first: live, not rendered

Gottlieb's rev. 1 sound board is an M6503 at 894,886 Hz, a 6532 RIOT, an
MC1408 DAC at 1000h and a Votrax SC-01 at 2000h, its program in the two
2KB chips the set now carries (`QBERT.ROM` is 68KB: the board image, then
`qb-snd1` and `qb-snd2` at 10000h; 1.4). `tools/qbsnd.py` is the board in
Python, after MAME's `gottlieb_a.cpp`, and it measured the choice:

| | MEASURED |
|---|---|
| codes 1 to 63 that sound | 28 |
| their length, all together | 36.4 s (one runs to the 8-second limit) |
| the 13 codes Q\*bert sends in 3,000 frames of attract and a game (`tests/qbnative.py --sndlog`) | about 11.7 s |
| the 6503's instructions a frame | about 4,200 |

Rendered once and kept, the sounds the game uses would be some 90KB at
8 kHz - a quarter of a 360KB disk beside the `.ROM` - and minutes of
rendering on an 8088. Live, the board runs at its clock: about 350,000
6502 instructions a second, and the core's cost, ESTIMATED from its
listing (22 x86 instructions, ~82 286-cycles a 6502 instruction), is a
29 MHz 286's worth for the board alone. So it runs **live, on a 386 or
better** (`OSAPI_CPU_INFO`, `CPU_386`), **into the Sound Blaster stream**;
a 286, an 8088, and a machine with no stream route play Q\*bert silent.

The first measurement, and the first build, counted every 6502 branch as
2 cycles: `tools/qbsnd.py`'s `self.cyc += f()` read the count before
`branch()` added its taken and page-cross cycles to it. The core's own
test found it (its author could not see why silicon's count and the
reference's differed by exactly those cycles). Mended, a delay loop runs
at its true speed - a quarter slower, so a tone made by one is lower - but
the sounds' LENGTHS moved by under 2% (35.8 s to 36.4 s), since the board
times them by its RIOT's timer. The table above is re-measured with it.

### 13.2 The 6503 (`arc6502.inc`)

An NMOS 6502 interpreter, every documented opcode, with
`tools/qbsnd.py`'s CPU6502 as its reference - decimal mode, JMP (ind)'s
page wrap, the cycle counts with page-cross and taken-branch penalties.
Every 6502 register lives in an 8086 register for the whole of `m6_run`,
and an opcode is one masked fetch and one `jmp [bx+m6_tab]`. The board
decodes A0-A14 only, so A15 is masked off every access; 0000-01FF and
6000-7FFF are plain bytes in the board's memory (32KB, claimed with the
shadow at the pick), everything else is I/O through the includer's `m6_ior` and
`m6_iow`. `m6_run` runs a cycle budget and carries its overrun as a debt
into the next call, so many short runs end where one long one would.
Undocumented opcodes are 2-cycle NOPs, counted in `m6_bad`.

### 13.3 The board (`arcqbs.inc`)

- **The RIOT** (`qr_*`): port A (the command), port B (the test switches,
  at rest), the timer (a divider of 1, 8, 64 or 1024, read off the 6503's
  cycle count), and the PA7 edge detector. Its IRQ line is the 6503's,
  refreshed at each sample and after every RIOT access, so it drops the
  moment the handler reads the flags.
- **The command** (`qbs_tick`, `BD_SBTICK`): Q\*bert writes FFh then the
  command, never two in a frame. Once a frame a byte at 580Ah that is not
  FFh is taken, FFh put back, and port A strobed with the code the 6503
  reads: the byte's low six bits inverted, PA7 up then down (MAME's
  `gottlieb_sound_r1::write`).
- **The stream** (`qbs_mix`, `BD_MIX`): at 10,989 Hz (the DSP's time
  constant 165, as Pac-Man's), each sample is the DAC after the 6503 has
  run 81 cycles and a fraction of 28,490/65,536 more - 894,886 / 10,989
  exactly, so the pitch is the board's.
- **The board's memory and chips are kept at the pick** (`qbs_want` in
  `qb_on`, `qbs_keep` in `qb_build`): on a 386 or better with a `QBERT.ROM`
  that carries the chips, the shadow of the glass is claimed 32KB larger
  and the 6503's memory is its last 32KB, at 780h paragraphs (`ar_sub`, a
  selector of its own on kern_mega; `qb_shmv` derives it again when the
  shadow moves). The `.ROM` image is freed when the board is built, long
  before the first play, so the ROM part is claimed 4KB larger and the two
  chips copied to its end (`QBS_CHIPAT`, 8C00h). Neither is a claim of its
  own because of `MEM_OWNER_MAX`, eight on kern_big: the carve, the
  pictures, the N88 part, G, the shadow, the ROM part and the sprites are
  seven, and the play's Sound Blaster ring is the eighth (at the pick the
  `.ROM` image is held in the ring's place). That also makes the 8080
  boards' sound part one claim too many: a pick of Q\*bert drops it (8.3). A shadow that cannot be claimed 32KB larger is
  claimed as it is, and the board plays silent.
- **Power-on** (`qbs_prep`, `BD_SBPREP`, on the desktop at the first play):
  the memory zeroed, the chips copied from the ROM part to 7000h, the RIOT
  and the 6503 reset, and 0.2 s of the board's own start-up run. It stays on
  between plays, and goes with the board (`qb_off`).

### 13.4 Verification

- `tests/m6502.py` (Unicorn): the core opcode by opcode against the
  reference, and in lockstep with it on the sound program.
- `tests/qbsmix.py` (Unicorn, row `qbsmix`): `arcqbs.inc` and the core as
  ARCADE assembles them, powered on and fed 15 of Q\*bert's commands over
  640 frames: all 115,840 samples are the reference's, driven the same way.
  Red at frame 42 with the cycle fraction one 65,536th off. It runs the
  PACKAGE's order - the chips kept, the image freed, then the power-on - and
  is red at frame 2 with `qbs_keep` a bare `ret`: run with the image still
  held, as it was, it passed while the package's `qbs_prep` found
  `ar_rimgc` zero at every play and the board was never heard.
- Not measured: the board's cost on a real machine (the 29 MHz figure is an
  estimate), and the sound itself in a field run on a 386.

## 14. On kern_mega: the arm (`-DPKG_MEGA`, docs/plans/ARCADE-MEGA-PLAN.md)

kern_mega's own disks carry a second build of the package,
`$(MEGAAPPDIR)/arcade.o88`, on a floppy of its own - `arcrelmega.img`
(1.44MB) and `arcrelmega120.img` (1.2MB), the twin of `arcrel*.img`: the arm
and `ARCADE.TXT`, 399 of 2,847 clusters. It runs in PROTECTED MODE (the
owner, 2026-11-05: it frees conventional memory for larger ports later),
its header saying `OS88_HIGH | OS88_HIGHMEM`: its region may load above
1MB, and its claims of 64KB or less may be there too. Claims over 64KB (a
zip, Q\*bert's 69KB image) are conventional, each named by a selector.

On kern_mega a segment is an LDT SELECTOR: adding to one names another
descriptor, a far transfer through a data selector and a store through a
code selector each fault (the kernel completes them, ~77 us each), and a
selector has a LIMIT. So the package keeps four rules, on every build - a
selector is just another opaque segment - with the arm's few lines that
name a kernel marker under `%ifdef PKG_MEGA`:

1. **Every derived segment goes through one door**, `ar_sub` (DX = a claim,
   AX = a paragraph offset): `OSAPI_SEG SEG_SUB` in the arm, a window that
   moves and goes with its claim; the sum in the shipped build. The ROM
   image (its claim + 2), Q\*bert's sound chips at 10000h and its 6503's
   memory past the shadow, the import
   image's clear and each chip's place (`ar_iplace`), the pictures
   (`af_slot`). `ar_crcimg` takes a claim and a paragraph, and asks again
   for the second 64KB. The zip walk (`ar_zat`) takes the 32KB GRANULE an
   offset lies in, BX up to 32KB into it - at most 14 lasting windows over
   448KB; a re-aimed `SEG_AT` window would not follow the claim when the
   pool packs, which any task's claim can make it do. So every chip is 8KB
   at most and a central directory is refused past 32KB.
2. **Nothing is stored through CS.** The inflater takes `INF_DSWS`
   (`apps/os88inflate.inc`): its work space through DS, the stream's
   segment in AX, loaded at each byte's refill and a stored block's copy.
   Pac-Man's shadow pass carries its row in AX and its plane on the stack;
   its sprite build keeps its counters in CH and on the stack. The N88
   core keeps the view's segment at G:9FFEh (`N88_GVIEW`, a word Gottlieb's
   map leaves empty), not in its own code.
3. **Code is reached through a CODE selector.** A board's code part is
   far-called through `op_cseg` (`OP_WANT_CSEG`, the arm's alone), its
   recipe still filled through `op_seg`. Q\*bert's G is run through its
   code half (`qb_gcode`: `OSAPI_MEM_CODESEL`, or `SEG_SUBC` over a lone
   pool selector), which the first run's frame and the N88 core's NMI frame
   push (`NV_GCS`, 6086h in the view); its yields far-call the N88 part's
   code half.
4. **Nothing reaches past what it was given**, which a real-mode segment
   never checked and a selector's limit does. Three defects of every build
   were found this way and fixed in both:
   - Q\*bert's power-on wrote the write list's pointer (C000h) into its
     40KB sprite part, 8KB past its end: only a board with an AUX part has
     a write list (`ar_reset1`).
   - Pac-Man's reset left the Z80's SP at FFFFh (MAME's), and the core runs
     on SS:SP = the view at the guest's SP, so a push before the game's own
     `ld sp, 4FC0h` landed 64KB above the view's base - a stack fault on
     kern_mega, which reset the machine. It is 5000h now, the RAM's top.
   - Pac-Man's write list holds 1,024 tile keys - a screen clear before the
     first render - which end at C802h, one word past its 50KB aux part:
     51KB now (`--aux-kb 51`).

**The zip is ONE HUGE CLAIM on the arm** (docs/plans/MEGA-HUGE-PLAN.md 3,
SPEC.md 50.3.5): `ar_zread` asks `OSAPI_MEM_HUGE` with BX = 8000h, a window
every 32KB, each 64KB long - which is exactly `ar_zat`'s granule, so the walk
takes granule g as the run's selector g (`[ar_zclaim]` + 8g, `[ar_zrun]`) and
needs no `SEG_SUB` and no LDT entry of its own however long the zip; above
1MB where the pool holds it, the heap's run of the same shape otherwise
(never a bridge: the windows overlap). So the arm's cap is `AR_ZMAXKB` =
4096KB rather than the shipped 448KB, the memory the machine has being the
real one (two zips are held at once, a split set's and its parent's). In real
mode (BX kept, the step 1000h) the granules' SUBs are as before; the shipped
build is unchanged. `arcimporthuge` (tests/arcimporthuge.py, 86Box's
at286_vga_sb at 4MB) is the gate - with the ROMs fetched data and not in the
tree, its zip is SYNTHETIC: Space Invaders' four chips forged to the CRC-32s
`arcsets.inc` pins (the import's identity for a chip, 1.5), stored after
600KB of filler (a 609KB zip), imported to a `INVADERS.ROM` byte for byte
the forged chips' - red on the arm before, refused as too big. A real MAME
zip past 448KB is not in the container to try.

The class bits are declared by the shipped build's header as well (they
mean nothing on a real-mode kernel), so `tests/megahigh.txt` carries no
ARCADE line.

MEASURED on Bochs (a 386, so the counts are the guest's and not a timing)
with `make megatrap`'s kernel, which logs every #GP: the arm off a 720KB
floppy, each board picked, coined and started, its counters read out of the
view over 20 guest seconds of play.

| board | frames in 20 s | dropped | #GPs from ARCADE |
|---|---|---|---|
| Space Invaders | 1,170 | 0 | 0 |
| Space Invaders Part II | 1,077 | 1 | 0 |
| Lunar Rescue | 1,073 | 1 | 0 |
| Gun Fight | 1,240 | 0 | 0 |
| Sea Wolf | 1,208 | 0 | 0 |
| Q\*bert | 1,035 | 0 | 0 |
| Pac-Man | 835 | 34 (its start) | 0 |

Every log holds the same two #GPs BEFORE the switch to protected mode
(`1058:1416`, `1058:142F`, DS = 0010h: the boot, not ARCADE).

**On a 286** - 86Box's `vm/at286-port`, the owner's mr286 (MR BIOS, 16 MHz,
4MB) - every board plays on kern_mega: in 20 guest seconds Invaders 1,205
frames, Part II 1,039, Lunar Rescue 996, Gun Fight 1,269, Sea Wolf 1,237
(at most one dropped), Q\*bert 896, Pac-Man 423 with 55 dropped at its
start. Against kern_big ON THE SAME MACHINE, with a scratch build counting
the loop's idle spins while the board is halted (86Box keeps no cycle
time, so these are relative):

| board | kern_big | kern_mega (protected mode) |
|---|---|---|
| Space Invaders | 60.15 frames/s, 0 dropped | 59.85, 0 dropped |
| Q\*bert | 61.85 frames/s, 22,915 idle spins/s | 62.05, 22,931 idle spins/s |
| Pac-Man | 50.70 frames/s, 70 dropped | 51.25, 64 dropped |

Protected mode costs nothing measurable: Q\*bert's spare time is the same
to 0.1%. Pac-Man is under its 60.61 Hz on this 286 on BOTH kernels - the
board's own cost, not the mode's. Not yet measured: the import on kern_mega
(ARCADE-MEGA-PLAN M5).

**Every claim of ARCADE's MOVES** but the import's two (one chooser
callback each) and the Sound Blaster's ring (the DMA reads it): the carve
and every fetched part (`OP_ONMOVE ar_partsmv`: the view, the code part -
its code half in the arm - the aux part, and Q\*bert's yields in G), the
`.ROM` image (`ar_rimgmv`, once read and checked), the pictures
(`af_pcmv`, pinned across `af_look`'s reads and `af_snapsave`'s write),
Pac-Man's ROM part and shadow, Q\*bert's G, ROM part, sprites and shadow,
with its sound board's memory in the shadow's claim (`qb_shmv`). A move happens only inside a claim the UI
task makes (SPEC.md 66.3.1), and in a bracket every other instance is
frozen (53.2), so a play never sees one: ARCADE claims nothing between a
pick's end and the bracket's. Between runs Q\*bert's G holds one segment
of its own - the far return its stack keeps, CS = G - which `qb_gmv` puts
right with the view's `NV_GSEG`/`NV_GCS`. On kern_mega a move is a rebase
and no proc is called; a declaration refused there (a pool claim) leaves
the claim as it was. `tests/arcmove.py` (soak) moves them on MartyPC's XT
and plays on: 11 moves over Q\*bert and Pac-Man, each board at its rate
after, red with `qb_gmv` a bare `ret`.

