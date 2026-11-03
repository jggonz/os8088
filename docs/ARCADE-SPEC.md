# ARCADE - arcade boards, their ROMs translated to 8086 ahead of time (`apps/arcade/`)

**The binding contract for ARCADE.** It stands to `apps/arcade/` as
docs/REALDMG-SPEC.md stands to `apps/realdmg/`: the register map, the
layouts, the interfaces between the translated code and the package that
hosts it, and the rules the timing keeps are pinned here, and a change goes
here *before* it goes in the code. The design record is
docs/plans/ARCADE-EMU-PLAN.md. It is cited from outside as "ARCADE-SPEC
section N".

**What is built, stated once:** two boards. Midway's 8080 B&W board running
Space Invaders (Taito, 1978), full screen on VGA, Hercules and CGA; and
Namco's Pac-Man board (Midway, 1980), its Z80 translated by section 7's
translator, full screen on VGA only, built in only when the owner has
supplied the ROM. Both have speaker sound. There is no windowed picture, no
Sound Blaster, no save state, and no interpreter for a guest PC the
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
| `tools/arcz80.py`, `tools/arcpac.py` | the Z80 and the Pac-Man board in Python: the reference (section 7.6) |
| `apps/arcade/arcrt.inc` | the runtime the translation carries: the code part's header, `arc_enter` and the ways back to the host (section 3.4) |
| `apps/arcade/arcdefs.inc` | the layout both sides share: the view's cells, the aux segment, the header (section 2) |
| `apps/arcade/arcade.asm` | the package: the window, the full-screen bracket, the clock, the keys, the picture and the sound (sections 4, 5) |
| `apps/arcade/invaders.seeds` | entry points no static walk finds, as ADDRESSES (section 3.2). No ROM byte |
| `tools/getarcrom.py` | fetches and checks the ROM (section 1.3) |
| `tools/arcsim.py` | the reference model: an 8080 and the board in Python (section 6) |
| `tests/arclock.py` | the translation in lockstep with the model (section 6.1) |

### 1.2 The package

`ARCADE.O88` is the package image plus its parts (SPEC.md 20.12): a VIEW all
boards share, and three for each board, LZ4-packed on the disk.

| part | what | size |
|---|---|---|
| 0 | the VIEW: the board's address space and the cells (section 2.1), zero-filled | 25KB |
| 1, 2, 3 | Space Invaders: the CODE PART (header, runtime, translation), AUX (the PC table, then room for the write list and the flags), the ROM | 13KB, 48KB, 8KB |
| 4, 5, 6 | Pac-Man (only with its ROM): the code part, aux, the ROM part (section 7.7) | 56KB, 37KB, 48KB |
| 7 | Pac-Man: the shadow of the glass and the tiles' cached planes, zero-filled | 64KB |

With both boards it is 89KB on the disk.

The shipped build refuses to start under kern_mega's protected mode
(`OS88_MEGA_REFUSE`): the host far-jumps into part 0, which is a data
selector there. kern_small has no room and refuses at `op_load`.

### 1.3 The ROM

**No ROM, and nothing derived from one, is committed.** `tools/getarcrom.py`
fetches MAME's `invaders` set from ONE archive.org item
(`arcade_invaders/invaders.zip`, SHA-1 `8d2d4172...`), checks each of its
four 2KB chips by SHA-256, and writes the 8KB board image
`build/arcade-rom/INVADERS.ROM` with `invaders.h/g/f/e` at 0000/0800/1000/
1800. A mismatch is a hard failure. `--from <dir-or-zip>` takes a set the
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
| `V_PROBE` | `arcaot.py --probe`'s counts, in an instrument build only |

### 2.2 Aux

| offset | what |
|---|---|
| `AUX_TABLE` 0 | the PC TABLE: 16,384 words, the translation of every guest PC 0000-3FFF (section 3.2). RET and JP (HL) are `jmp [es:si]` |
| `AUX_LPTR` 8000h | the write list's next free offset. ES:DI holds it while translated code runs |
| `AUX_LIST` 8002h | the write list: cell (or tile) keys, a word each, each at most once until drawn |
| `AUX_FLAGS` + key, A400-BF1F | Space Invaders' CELL FLAGS: one byte per 8x8 cell, at 8000h + the cell's key (section 4.1) |
| `PM_TFLAG` + tile, 9000-93FF | Pac-Man's TILE FLAGS: a byte for each video RAM index (section 7.7) |

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
  instruction covers) that points below 2000h. One is kept only if every
  instruction it decodes is whole, overlaps nothing already decoded, and
  uses only the board's ports. This found 050E, a sixth object handler in
  the table at 1BC0 that the model's script had not reached. Most of what it
  adds is runs of zero bytes, which translate to nothing.

`--no-speculate` turns the second off. The result for Space Invaders: 4,469
instructions, 2,189 of them speculative.

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
| VGA | `FSXM_VGA12` 640x480 | 256 rows into 480: in each cell 7 doubled and 1 single; 448 wide, centred | the game writes PLANE 0 only. The cabinet's cellophane is filled ONCE into planes 1-2 at entry (red over rows 32-63, green over 184-239, green over the spare bases in 240-255), and DAC entries 0-5 colour each band's lit pixels, so the overlay costs nothing a frame |
| Hercules | `FSXM_HERC` 720x348 | 256 rows 1:1, from row 46; 448 wide | mono |
| CGA | `FSXM_CGA640` 640x200 | 256 into 200: each cell's 8 rows into 6, or 7 in every fourth cell, MERGED by OR inside the cell so no two cells share a screen row | mono |

The first adapter `OSAPI_FSX_CAPS` offers, in that order, is used.

### 4.4 Entering

A mode set clears the screen, so entry lists all 896 cells (`ar_dirtyall`)
and draws them.

### 4.5 The sound

Once a frame the rising bits of `V_SND3` and `V_SND5` pick a cue by priority
- the base hit, the saucer hit, an extra base, an invader hit, a shot, the
fleet's four steps - and a held saucer (OUT 3 bit 0) warbles under them.
Each cue is a short table of pitches played through `OSAPI_SND_TONE`, the
tone route a bracket may use (WireBall's), called only when the pitch
changes. OUT 3 bit 5 is the amplifier: when the attract mode turns it off,
so does this.

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
proc (picture, sound, keys), its PIT period and its interrupts a frame.
Game > Space Invaders and Game > Pac-Man select one, which powers it on.
A coin, a start and Esc are TYPED for every board (`ar_typed`); the held
controls are each board's own.

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

The Namco WSG's three voices become one speaker tone: the loudest voice with
a frequency, at f x 96,000 / 2^20 Hz, through `OSAPI_SND_TONE`; silent when
5001 bit 0 turns the sound off. The arrows are the joystick (IN0 bits 0-3,
held, `OSAPI_KEY_DOWN`); a coin (IN0 bit 5) and the starts (IN1 bits 5, 6)
are typed. All active low.

### 7.9 The ROM

**Never fetched**: no public archive.org item carries Pac-Man
(`arcade_pacman`, `arcade_puckman` and `arcade_mspacman` are dark).
`make arcaderom-pacman GETPACFROM=<dir-or-zip>` checks the owner's set chip by
chip against SHA-256 pins (CRCs as MAME's `pacman`) and writes
`build/arcade-rom/PACMAN.ROM`; while it exists, `make arcade` builds the
Pac-Man board in (`-DARC_PACMAN`), and without it, Space Invaders alone.

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
