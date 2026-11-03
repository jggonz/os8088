# ARCADE - arcade boards, their ROMs translated to 8086 ahead of time (`apps/arcade/`)

**The binding contract for ARCADE.** It stands to `apps/arcade/` as
docs/REALDMG-SPEC.md stands to `apps/realdmg/`: the register map, the
layouts, the interfaces between the translated code and the package that
hosts it, and the rules the timing keeps are pinned here, and a change goes
here *before* it goes in the code. The design record is
docs/plans/ARCADE-EMU-PLAN.md. It is cited from outside as "ARCADE-SPEC
section N".

**What is built, stated once:** one board, Midway's 8080 B&W board running
Space Invaders (Taito, 1978), full screen on VGA, Hercules and CGA, with
speaker sound. There is no windowed picture, no Sound Blaster, no second
board, no save state, and no interpreter for an 8080 PC the translation does
not hold (section 3.2 says what happens then).

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
| `apps/arcade/arcaot.py` | the translator: ROM + seeds -> one NASM file, assembled by itself until its short branches settle (section 3) |
| `apps/arcade/arcrt.inc` | the runtime the translation carries: the code part's header, `arc_enter` and the ways back to the host (section 3.4) |
| `apps/arcade/arcdefs.inc` | the layout both sides share: the view's cells, the aux segment, the header (section 2) |
| `apps/arcade/arcade.asm` | the package: the window, the full-screen bracket, the clock, the keys, the picture and the sound (sections 4, 5) |
| `apps/arcade/invaders.seeds` | entry points no static walk finds, as ADDRESSES (section 3.2). No ROM byte |
| `tools/getarcrom.py` | fetches and checks the ROM (section 1.3) |
| `tools/arcsim.py` | the reference model: an 8080 and the board in Python (section 6) |
| `tests/arclock.py` | the translation in lockstep with the model (section 6.1) |

### 1.2 The package

`ARCADE.O88` is the package image plus three parts (SPEC.md 20.12):

| part | what | size |
|---|---|---|
| 0 | the CODE PART: the PC table, the header, the runtime, the ROM image and the translation (section 3.3), LZ4-packed on the disk | 53KB, 20KB packed |
| 1 | the VIEW: the board's 8080 address space and the cells (section 2.1), zero-filled | 17KB |
| 2 | AUX: the write list and the cell flags (section 2.2), zero-filled | 18KB |

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

One segment holding the board's 8080 address space IN PLACE: ROM at
0000-1FFF (copied from part 0 at power-on), RAM 2000-23FF, VRAM 2400-3FFF.
DS = SS = the view while translated code runs, so an 8080 address is an 8086
offset and an 8080 PUSH is an 8086 PUSH.

Above it, at 4000h - which the game never reaches (MEASURED: no access above
3FFF in 6,000 frames of tools/arcsim.py's script) - are the cells the core
and the host share:

| cell | what |
|---|---|
| `ST_AF` .. `ST_PC` (4000-400B) | the guest's registers between runs: AL = A, AH = F in LAHF's layout, then BC, DE, HL, SP, PC |
| `ST_BUD`, `ST_LEFT` | the run's poll budget in, what was left out (section 5.2) |
| `ST_WHY` | why the run ended: `ARCX_POLL`, `ARCX_MISS`, `ARCX_HALT` |
| `ST_INTE` | the 8080's interrupt enable: EI and DI write it |
| `ST_PEND`, `ST_EIX` | the host has an interrupt waiting; EI ended the run because of it (section 5.1) |
| `ST_HSS`, `ST_HSP` | the host's SS:SP across a run |
| `V_IN0`..`V_IN2` | the board's input ports |
| `V_SHLO`, `V_SHHI`, `V_SHOFF` | the shift register and its offset |
| `V_SND3`, `V_SND5` | the two sound latches |
| `V_SIG` 4040h | 'ARCV': where an instrument finds the view in guest memory |
| `V_NFRAME` .. `V_NCELL` | the host's counters: frames delivered, half frames dropped, runs, the budget, cells drawn |
| `V_PROBE` | `arcaot.py --probe`'s counts, in an instrument build only |

### 2.2 Aux

| offset | what |
|---|---|
| `AUX_LPTR` 0 | the write list's next free offset. ES:DI holds it while translated code runs |
| 2400-3F1F | the CELL FLAGS: one byte per 8x8 cell, at the cell's own key (section 4.1), the low 32 bytes of each 256 |
| `AUX_LIST` 4000h | the write list: cell keys, a word each, each at most once until drawn (at most 896) |

### 2.3 The code part's header

At `ARC_HDR` (8000h), straight after the PC table: `dw ARC_MAGIC` ('AC'),
`dw arc_enter`, `dw` the ROM image's offset and length, `dw` the end of the
translation. The package checks the magic and refuses a damaged part.

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

**The PC table** is the first 32KB of the code part: 16,384 words, the
translation of every 8080 PC 0000-3FFF, or `arc_miss`. RET, RETcc and PCHL
go through it (`pop si / lahf / shl si,1 / sahf / jmp [cs:si]`), and so does
every entry from the host. **A miss stops the game** with "Arcade: no code
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
