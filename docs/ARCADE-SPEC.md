# ARCADE - arcade boards, their ROMs translated to 8086 ahead of time (`apps/arcade/`)

**The binding contract for ARCADE.** It stands to `apps/arcade/` as
docs/REALDMG-SPEC.md stands to `apps/realdmg/`: the register map, the
layouts, the interfaces between the translated code and the package that
hosts it, and the rules the timing keeps are pinned here, and a change goes
here *before* it goes in the code. The design record is
docs/plans/ARCADE-EMU-PLAN.md. It is cited from outside as "ARCADE-SPEC
section N".

**What is built, stated once:** six boards. Midway's 8080 B&W board running
Space Invaders (Taito, 1978), and Taito's two colour boards on the same
8080 design - Space Invaders Part II (1979) and Lunar Rescue (1979) - full
screen on VGA (in colour), Hercules and CGA; Midway's two LANDSCAPE boards
on it, Gun Fight (1975) and Sea Wolf (1976), on the same three; and Namco's Pac-Man board
(Midway, 1980), its Z80 translated by section 7's translator, full screen on
VGA only, built in only when the owner has supplied the ROM. All have sound
on a Sound Blaster, an AdLib or the speaker (section 8). There is no
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
boards share, and three for each board, LZ4-packed on the disk. **Every
board's parts are LAZY** (`OP_LAZY`): Game reads the three of the board it
picks (`op_fetch`, on the desktop, where a refusal may toast) and drops the
previous board's (`op_drop`), so one board's ~90KB is in memory, never all
four - and a board that cannot be read leaves the one before it picked.
Pac-Man's 64KB shadow of the glass is a claim its pick makes (`BD_ON`) and
its drop frees (`BD_OFF`), not a part.

| part | what | size |
|---|---|---|
| 0 | the VIEW: the board's address space and the cells (section 2.1), zero-filled, the one eager part | 25KB |
| 1, 2, 3 | Space Invaders: the CODE PART (header, runtime, translation), AUX (the PC table, then the flags and the write list), the ROM | 13KB, 50KB, 8KB |
| 4, 5, 6 | Space Invaders Part II: the same three; its ROM part is the CPU's 0000-4FFFh and the two colour maps (section 4.3) | 23KB, 50KB, 22KB |
| 7, 8, 9 | Lunar Rescue: the same | 32KB, 50KB, 22KB |
| 10, 11, 12 | Gun Fight: the same, its ROM 4KB | 12KB, 50KB, 4KB |
| 13, 14, 15 | Sea Wolf: the same | 17KB, 50KB, 4KB |
| 16, 17, 18 | Pac-Man (only with its ROM): the code part, aux, the ROM part (section 7.7) | 57KB, 50KB, 48KB |
| last (16, or 19 with Pac-Man) | the 8080 boards' sounds (`arcsfx.py`), read only for a stream | 58KB |

With every board it is about 235KB on the disk.

The shipped build refuses to start under kern_mega's protected mode
(`OS88_MEGA_REFUSE`): the host far-jumps into part 0, which is a data
selector there. kern_small has no room and refuses at `op_load`.

### 1.3 The ROM

**No ROM, and nothing derived from one, is committed.** `tools/getarcrom.py`
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
one map, loaded twice, as MAME's `ROM_RELOAD`). `make arcaderom` fetches all
three. `--from <dir-or-zip>` takes a set the
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
| `V_PROBE` | `arcaot.py --probe`'s counts, in an instrument build only |

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

**The ROM** is 0000-1FFFh on Space Invaders, and 0000-1FFFh and 4000h up to
the last 2KB chip that is not all zeros on a Taito colour board (`board_rom`:
47FFh on Part II, 4FFFh on Lunar Rescue). Without the bound a speculative
walk ran through Part II's empty 4800h-4FFFh and into a jump past 7FFFh.

`--no-speculate` turns the second off. The result for Space Invaders: 4,366
instructions, 2,086 of them speculative; Part II 4,415 (706); Lunar Rescue
5,227 (740); Gun Fight 1,426 (178); Sea Wolf 2,078 (473).

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

### 5.3 The boards

The loop is one loop for every board; a BOARD is the table of what differs
(`ar_boards`, `BD_*`): its parts, its program's length, a reset proc for its
own cells, a setup proc for the bracket (the mode, the tables, the first
picture), its interrupt proc - deliver, or say it is masked - and its frame
proc (picture, sound, keys), its PIT period and its interrupts a frame, its
sound procs (section 8), and the hooks its pick and its drop run (`BD_ON`,
`BD_OFF`). Game > a board's name selects one, which reads its parts and
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
until the package ends. 8,000 Hz, signed 8-bit, peaks within +-42, so the
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
