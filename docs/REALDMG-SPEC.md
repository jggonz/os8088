# REALDMG - a real-mode Game Boy emulator (`apps/realdmg/`)

**The binding contract for RealDMG.** It stands to `apps/realdmg/` as
docs/C64-SPEC.md stands to `apps/c64`: the register map, the layouts, the
interfaces between the core and the code that hosts it, and the rules the
timing model keeps are pinned here, and a change goes here *before* it goes
in the code. The design record - why a threaded interpreter, why two page
tables, the waves and the gates - is docs/plans/REALDMG-PLAN.md; the
measurements are docs/reports/REALDMG-CORE-COST-2026-10-19.md. This file says
what the code does today.

It is cited from outside as "REALDMG-SPEC section N"; inside it, "section N"
is a section of this file.

**What is built, stated once so no section below has to hedge:** the plan's
waves 1 and 2 and wave 3 - the SM83 core, the system code that hosts it, the
composer of section 13, and `REALDMG.O88`, the windowed package of section
14. There is no fullscreen path, no sound, no save RAM, no MBC3 clock, no
speed overlay and no ROM over 256KB.

---

## 1. What it is

A Game Boy (DMG) emulator for os8088, in **pure 8086 assembly**, one core
shared by every machine: the primary target is a ~12 MHz 286 at full game
speed, and the same core carries a HALT-heavy demo of our own on the 5150
(the plan's section 6). There is **no boot ROM**: the core starts at 0100h
with the registers and I/O state the DMG boot ROM leaves (section 6.1).

| file | what it is |
|---|---|
| `apps/realdmg/rdggen.py` | the core's GENERATOR: writes `build/realdmg/rdgcore.asm`, one handler per opcode. Nothing generated is committed |
| `apps/realdmg/rdgdefs.inc` | the layout: the view, the state block's fields, exit codes, MBC types, flag bits, the PPU's clock constants. **The field table there is this contract's list of fields** |
| `apps/realdmg/rdgsys.inc` | the system code: init, run, events, the I/O and MBC registers rdgsys answers, the raster log, the trace hook. Included by a host |
| `apps/realdmg/rdgppu.inc` | the picture: `rdg_render` (section 13). Included by a host after rdgsys.inc |
| `apps/realdmg/realdmg.asm` | the package: the window, the worker, the writers (section 14) |
| `tests/rdgcore/` | the bare harness and the instruments (section 12) |

No ROM, test ROM or reference emulator source is ever committed. Test ROMs
are fetched by `tools/getgbtest.py`, Peanut-GB by `tools/gbprof.py`, each at
a pinned commit or SHA-256.

## 2. The register map

| SM83 | 8086 | notes |
|---|---|---|
| A | AL | |
| F | AH | in **LAHF's layout**: Z bit 6, H (AF) bit 4, C bit 0, and N kept in bit 5, which LAHF leaves alone. `RDG_F2AH`/`RDG_AH2F` (section 3) translate for PUSH/POP AF and the trace |
| BC | CX | |
| DE | DI | |
| HL | BX | |
| SP | SP | with **SS = the view** (section 3), so a Game Boy push is an 8086 push |
| PC | SI | with **DS = the region holding it**: the view, or ROMX for 4000-7FFF |
| - | BP | the **cycle countdown**: M-cycles to the next event, signed (section 6) |
| - | DX | dispatch scratch. **DL is 0 at every dispatch**; DH carries a read's byte between a handler and a tail |
| - | ES | scratch, any handler |

## 3. Memory: the view and ROMX

The core addresses the Game Boy's 64KB through **two segments**:

- **The view**, one 64KB claim, which SS always holds:
  - 0000-3FFF: a copy of ROM bank 0, made at init;
  - 4000-7FFF: rdgsys's own data, never the game's: the state block at
    `RDG_STATE` (4000h), `RDG_F2AH` (4100h), `RDG_AH2F` (4200h), the raster log
    `RDG_RLOG` (4300h, 192 entries of 4 bytes), `RDG_SERBUF` (4700h), the
    frame-start snapshot `RDG_SNAP` (4800h), and a save state's record
    `RDG_STREC` (7800h, 10.4) - just under 8000h, so it and the Game Boy's
    half are one run;
  - 8000-FFFF: the Game Boy's own RAM and registers, at their own addresses -
    VRAM, cartridge RAM (one bank), WRAM, OAM, the I/O registers and HRAM.
- **ROMX**, the current switchable bank: `S_ROMX` is the segment whose
  4000h is the bank's first byte, read out of **the bank table**, `RDG_BTAB`
  (6800h in the view, a word a bank, 512 of them). For a cartridge in one
  claim it is `S_ROMSEG` + bank x 400h - 400h; bank 0's is the view's own
  0000-3FFF less 400h. **A bank switch is a pointer change and never a
  copy** - unless the bank is not in memory at all, which is section 10.3's.

A data access tests the address (section 5.2): the view takes every address
directly but 4000-7FFF (ROMX) and FF00-FF7F (registers), and a write to
0000-7FFF is a cartridge register write.

## 4. Dispatch and the core's layout

The core is one 64KB segment. **The handler for opcode X is at X x 256, and
the handler for CB-prefixed X at X x 256 + 128**, so dispatch is

    mov dh,[si] / inc si / jmp dx        ; DL = 0

and a CB handler is reached by `mov dl,80h` instead. A handler's slow paths
follow it inside its own half-slot. The eleven opcodes the SM83 does not
have - D3, DB, DD, E3, E4, EB, EC, ED, F4, FC, FD - each begin with a stub
that stops the core (`S_BADOP`, `RDGX_BADOP`), and their half-slots carry the
shared tails:

| slot | tails |
|---|---|
| D3 | `rd_slow`, `wr_slow` - a read of ROMX or a register, a cartridge or register write |
| DB | `force_ev` - make the next check fire (EI) |
| DD | `RDG_DISPATCH`, which **must be at DD00h** (rdgsys enters the core there), and `mbc_wr` |
| E3 | `daa_tail` - the SM83's DAA rule |
| E4, EB, EC, ED | the idle detector (section 8) and `mbc_set` |
| F4 | `io_rd` - register reads answered in the core |
| FC | `io_lcd` - STAT and LY |
| FD | `io_wr` - register writes answered in the core |

A tail is entered by a near jump with `[S_RET]` holding where to resume and
returns with `jmp word [ss:S_RET]`. A half-slot holds 128 bytes; the
generator's `times` fails the assembly when one overflows.

## 5. The core and rdgsys

### 5.1 Calls a host makes

rdgsys is included by a host and assembled into the host's code segment. It
asks the host for two near procs:

- `rdg_putc`, AL = a byte the game sent on its link port (may be a bare RET);
- `rdg_trbyte`, AL = a byte of an `RDG_TRACE` core's trace record (may be a
  bare RET; preserves every register).

and offers:

- **`rdg_init`** - AX = the view's segment, BX = the core's, CX = the ROM
  image's, DX = the image's size in 16KB banks. Copies bank 0, builds the
  flag tables, parses the cartridge header's MBC byte, sets the post-boot
  state (section 6.1) and points the vectors (section 5.2) at rdgsys.
- **`rdg_run`** - AX = a budget in M-cycles. Runs the core until a frame is
  ready, the budget is spent, the game executes STOP or a bad opcode, and
  returns AX = `RDGX_FRAME` (1), `RDGX_BUDGET` (2), `RDGX_BADOP` (3) or
  `RDGX_STOP` (4). Every register but AX is preserved.
- **`rdg_trace_on`** - with an `RDG_TRACE` core, trace the first instruction
  after every return to the core too (section 12.2).

### 5.2 Calls the core makes

The core leaves through far vectors in the state block, so the core's
segment never needs to know the host's:

| vector | entry | entered with | returns |
|---|---|---|---|
| `S_EVVEC` | `rdg_ev_entry` | the registers live, BP <= 0 or an event forced (section 6) | `rdg_resume`, or `rdg_run`'s caller |
| `S_IORVEC` | `rdg_ior` | BX = FF00-FF7F, `[S_RET]` | DH = the byte, at `S_RETFAR` |
| `S_IOWVEC` | `rdg_iow` | BX = FF00-FF7F or FFFF, `[S_WVAL]` = the byte | at `S_RETFAR` |
| `S_MBCVEC` | `rdg_mbc` | BX = 0000-7FFF, `[S_WVAL]` | at `S_RETFAR` |
| `S_TRACEVEC` | `rdg_trace` | (`RDG_TRACE` cores, at every dispatch) | `S_DISP` |
| `S_RESVEC` | - | where `rdg_resume` enters the core: `RDG_DISPATCH`, or `rdg_trace` | |

**An entry switches to the host stack first** (`RDG_TOHOST`): the core's
SS:SP is the Game Boy's stack inside the view, which may point anywhere - a
game that reads a table with `LD SP,HL / POP` points it into ROM - so no
host code ever pushes on it.

**For the same reason the core runs with interrupts OFF**, and rdgsys, on the
host stack, with them on. An IRQ taken in the core would push its frame
below the Game Boy's SP: into HRAM's variables when the stack is there, into
the copy of bank 0 when SP points at a table in ROM. So an interrupt waits
for the core's next trip out - an event, a register, a bank switch - or the
end of `rdg_run`'s budget, and **a host sizes that budget for its mouse**: a
serial mouse on an 8250 delivers a byte every 8.3 ms. On os8088 this also
means the scheduler never switches away from the core (`sch_switch` switches
only a task whose SS is `LOW_SEG`). Every trip out of the core turns them
back on, on the host's task stack, so the tick pre-empts the host between two
budgets as it would any task: no yield is needed for that, and the package
yields once a frame, for a UI task a click woke.

**A POP with SP in 4000-7FFF reads the BANK**, not the view's rdgsys half
there: Pokemon's VBlank copy points SP at tiles in a ROM bank and POPs them,
and the compatibility sweep's Red and Yellow drew their menus out of
RealDMG's own state until it did. Each POP compares SP with C000h first and
leaves for `pop_slow` below it, which reads 4000-7FFF through `S_ROMX` and
the rest as the view - one compare and an untaken branch on the common path,
the stack being in WRAM or HRAM. `pop_slow` hands the word back in DX, so
each POP clears DL after it: the dispatch is `jmp dx`, and an `RDG_TRACE`
core, whose dispatch is a far vector, cannot see a DL left over
(`tests/gbcpu.py`'s `pop from ROM`, `tools/gbprof/gbpop.asm`, red with POP
BC's branch taken out). **What it still does not cover** is a RET or a PUSH
there, which stay the view's: no game seen returns through a table in ROM,
and a push into ROM writes nothing on the hardware.

**The core answers the common registers itself** and leaves only for the
rest (section 9). That rule exists because the trip out - the stack switch
and the register saves - was a quarter of the 8088's time on Super Mario
Land (the report, section 4).

## 6. Time

**Every time is a dword of M-cycles since `rdg_init`.** "Now" is
`S_TNEXT - BP`: `S_TNEXT` is when the next event is due and BP counts down to
it, every handler subtracting its own M-cycles (`sub bp,n`). A taken or
not-taken branch, HALT, RETI, EI, STOP and NOP check `js` and enter
`S_EVVEC`; straight-line code does not check, so an event is late by at most
one basic block. NOP checks because a game that has crashed runs off into
zeroed memory, and a core that never checks never gives its host the machine
back.

| event | time | does |
|---|---|---|
| `S_TVBL` | line 144 of a frame | VBlank: IF bit 0, the STAT mode-1 interrupt, `RDGX_FRAME` |
| `S_TSTAT` | the next enabled STAT condition (LYC, mode 2, mode 0) | IF bit 1 |
| `S_TTIMA` | TIMA's next overflow | IF bit 2, TIMA = TMA |
| `S_TSTOP` | the end of `rdg_run`'s budget | `RDGX_BUDGET` |

`rdg_sched` sets `S_TNEXT` to the earliest, never more than 7FFFh M-cycles
ahead, and BP to the distance. It also normalises `S_TFRAME` (the start of
the frame holding now) and sets **`S_TNF` = `S_TNEXT` - `S_TFRAME`** (low 16
bits), so the core reads the PPU's place as `S_TNF - BP` modulo a frame
without leaving it. Anything that moves `S_TNEXT` or `S_TFRAME` keeps
`S_TNF` with it (`rdg_force`, `force_ev`).

The PPU's clock: **114 M-cycles a line, 154 lines, 17,556 a frame**; mode 2
for M-cycles 0-19 of a visible line, mode 3 for 20-62, mode 0 from 63
(`RDG_HBL`), mode 1 on lines 144-153.

**DIV and TIMA run off one free-running counter**, as the hardware's do:
DIV is `(now - S_TDIV) >> 6`, and TIMA ticks each time
`(now - S_TDIV) >> shift` does, shift being TAC's rate - that is, when the
counter's bit `shift - 1` falls. A DIV write sets `S_TDIV` = now, and a DIV
or TAC write that makes that bit fall ticks TIMA once (rdg_tbit). TIMA reloads
from TMA at the overflow itself, with none of the hardware's one-M-cycle
delay. `instr_timing` fails without the counter.

### 6.2 When inside an instruction a register is touched

A register read or write is answered **as of the M-cycle the hardware makes
it in**, counted from the instruction's first: 1 for `LD r,(HL)`,
`LD (HL),r`, `LD A,(BC)`/`(DE)`, `LDI`/`LDD`, `LD A,(C)` and the ALU's
`(HL)` forms, and `INC`/`DEC (HL)` read; 2 for `LDH A,(n)`, `LDH (n),A`,
`LD (HL),n`, `INC`/`DEC (HL)`'s write and a CB `(HL)` read; 3 for
`LD A,(nn)`, `LD (nn),A` and a CB `(HL)` write; 3 and 4 for
`LD (nn),SP`. The generator's `read8(at)`/`write8(at)` take BP back by
`at` around the slow path only, so the fast path pays nothing. `mem_timing`
fails without this.

### 6.1 The state at 0100h

A = 01, F = B0, BC = 0013, DE = 00D8, HL = 014D, SP = FFFE, PC = 0100, and
the I/O registers the DMG boot ROM leaves, with LCDC = 91h, STAT = 85h and
BGP = FCh. The counter under DIV reads **ABCCh T-states** at 0100
(`S_TDIV` = -(ABCCh / 4)), TAC F8h, DMA FFh, NR52 F1h. The LCD is on and line
0 begins at time 0. FF4C-FF7F read FFh (section 9).

### 6.3 An event every line: the HBlank interrupt

**Pokemon Gold runs with STAT = 0Dh, the mode-0 interrupt on, for the whole
game**: ~148 events a frame, one a visible line, where most games take two
or three. Its handler does nothing while `hLCDCPointer` is zero, and its main
loop HALTs until VBlank, so what a frame costs is almost entirely the trip
out of the core and back - the per-event path, not the game. Three cuts, each
measured on `GBHBLANK.GB` (`tools/gbprof/gbhblank.asm`: Gold's handler, its
HALT loop and a raster effect every eighth frame; `tools/gbours.py gbhblank`)
with `tests/rdgcore/rdgbench.py --box 286-victory --mhz 16 --rom`:

| each cut on top of the ones above it | ms a frame |
|---|---|
| before | 39.45 |
| a mode-0 STAT event schedules the next line's without a walk | 34.10 |
| `rdg_sched`'s minimum and `rdg_events`' due tests inline, not called | 31.49 |
| a due test is the borrow of `now - T`, not a compare and branch | 30.76 |
| HALT's skip asks `rdg_nextev` (the minimum alone), not `rdg_sched` | 29.93 |

**24% off the frame**, and Gold off the hard disk
(`tests/rdgcore/rdghd.py`) goes from ~32-36 frames in 1.5 s to ~45 on its
title and menus.

The first is **`S_STATLN` and `S_STATT`**: when `rdg_statsch` schedules a
mode-0 interrupt it records the line it is for and the time it scheduled.
`rdg_statev` then takes a fast path when STAT asks for mode 0 alone (bits
3, 5 and 6 = 08h), `S_TSTAT` is still exactly the time recorded - so nothing
else has moved it: a LCDC write, a STAT write, a state loaded - and the line
is below 143: it raises IF bit 1 and moves both on one line, 114 M-cycles,
with no `rdg_lcdpos` division and no walk. Line 143's next is a frame on,
past VBlank, and takes the walk.

`rdg_nextev` is `rdg_sched`'s first half: the earliest event, capped 7FFFh
past now. A HALT (or spin-wait) skip wanted only that, and was paying for
`S_TFRAME`'s normalisation and `S_TNF` too - which `rdg_events` computes again
on its way out. What is left of a line is the trip itself: `rdg_ev_entry`,
the interrupt's push, `tr_ekey`'s lookup of the handler's block at 0048h, and
`rdg_resume` (`tests/rdgcore/rdgprof.py`).

## 7. Interrupts

An enabled, pending interrupt is taken at the next event entry: IME cleared,
IF's bit cleared, PC pushed, PC = 40h + 8 x the bit, and **5 M-cycles**
charged before the handler's first instruction. A write to IF or IE takes
one **at once**, from inside the writing instruction (`rdg_irqnow`), so the
pushed PC is the next instruction's. HALT ends when IF AND IE is non-zero,
whether or not IME is set, and **a budget that ends inside a HALT leaves it a
HALT**: the next `rdg_run` goes on to the next event rather than into the
instruction after it (a game that counts VBlanks with `HALT` counted host
budgets before this). **The HALT bug**: HALT executed with IME clear and
an interrupt already pending does not halt, and the byte after it is read
twice - the next opcode runs with PC still on itself.

**EI is not delayed by one instruction.** It sets IME at once and, when an
interrupt is already pending, forces the next event check, so it is taken at
the next branch, HALT or RETI rather than after exactly one more
instruction. **RETI enters the event path only for one pending** too, and
otherwise goes on as a RET does (into translated code). Nothing else can be
waiting on IME: IF gains a bit only at an event, which checks, or at an
IF/IE write, which takes it at once. `EI / HALT` and
`EI / RET` come out as the hardware's; Mooneye's `ei_sequence`, `ei_timing`,
`rapid_di_ei` and `di_timing` do not (section 12.3). A read of IF first runs every event already due
(`rdg_catchup`), so a game polling IF sees what the hardware would.

**The joypad interrupt** (bit 4, 60h) is the host's: a button pressed since
the package's last look (`rw_keys`, once a frame) sets IF bit 4 between two
`rdg_run`s, and the next one's event check takes it or ends a HALT. A game
asleep in HALT with only it enabled - a title or pause screen waiting for a
press - wakes once a press (`tests/gbheader.py`, `GBJOY.GB`). STOP is not
modelled: it returns `RDGX_STOP` and the package runs on, as if a button had
been pressed at once.

## 8. The spin-wait rule

**A skip does not spend the budget.** `rdg_run`'s budget bounds how long the
core runs with interrupts off, and a skip runs in rdgsys with them on, so a
spin-wait's skip sets TNEXT by every event but the budget's and moves the
budget's end on by what it skipped. Before, about 3 of Super Mario Land's
4.4 budget exits a frame landed in a skip, each one a host round trip and
another pass of the loop: its events in the bench went 9,949 -> 3,750 and
the CPU alone 6.13 -> 5.72 ms a frame on `286-victory`. HALT keeps lapsing
the budget as before (section 7), and with the LCD off the skip keeps it,
since no VBlank would end the run.

A taken backward `JR` of **-4 to -16** to a loop head is a SPIN-WAIT,
skipped to the next event at no cost, when the loop's body is one read and
then only tests, each of which may be followed by a `JR cc` that goes back
to the head or forward out past the closing `JR`:

| read | then, one or more |
|---|---|
| `LD A,(nn)` of WRAM or HRAM | `AND A`, `OR A`, `CP n`, `AND n`, `BIT b,A`, `JR cc` |
| `LDH A,(n)` of HRAM (n >= 80h) | the same |
| `LD A,(HL)`, HL in WRAM or HRAM | the same |
| `BIT b,(HL)`, HL in WRAM or HRAM | (it is the test, alone) |

Nothing in such a loop can change what it reads until an interrupt does, so
every pass takes the same path, and an interrupt is an event. Kirby's
Dream Land in play waits on a read and TWO tests, `LDH A,(8C) / BIT 5,A /
JR NZ / BIT 6,A / JR NZ,-10`: when the first rule stopped at one test and
-7, the second jump was never skipped, and that scene ran at 27.8% of real
time full screen on 86Box's 286 where it runs at 37.5% now.

### 8.1 Countdowns

A taken `JR NZ,-3` back to `DEC A`, `DEC B` or `DEC C` is a COUNTDOWN, and
`cnt_ff` runs it to its end in one step: from the taken `JR`, r more `DEC`s
take 4r + 2 M-cycles, and the register ends 0 with the flags of 1 -> 0
(Z and N set, H clear, C kept). It does so only when BP covers the whole of
it, so no event can fall inside; otherwise the loop runs as it would have.
The OAM DMA routine every game copies to HRAM waits on one (`LD A,28h /
DEC A / JR NZ,-3`), and so does a delay loop. The interpreter and the
recompiler both take it. A register is never a spin-wait's subject - LY and
STAT change on their own. A loop head found NOT to be one is remembered in
`S_IDLENO`, so a busy loop pays the test once. An `RDG_TRACE` core skips
nothing (section 12.2).

## 9. The registers

**Answered in the core** (`io_rd`, `io_lcd`, `io_wr`, `mbc_wr`), each
exactly as rdgsys would:

- **P1 (FF00)** reads, from `S_JOY` (bits Right, Left, Up, Down, A, B,
  Select, Start; 1 = held) and the select bits; writes keep bits 4-5.
- **STAT (FF41)** and **LY (FF44)** reads, from `S_TNF - BP` (section 6):
  LY 0 and STAT mode 0 with the LCD off; the coincidence bit against LYC.
- **Sound (FF10-FF3F)** writes are stored.
- **FF4C-FF7F** - nothing on a DMG: reads FFh, writes are dropped.
- **ROM bank switches** (2000-3FFF) on an MBC1, MBC3 or MBC5 whose high bank
  bits are 0, the bank masked by `S_BMASK` (banks - 1); DS moves with ROMX
  when the game runs from 4000-7FFF.

**Read masks.** A read of FF00-FF3F answered by rdgsys is ORed with the bits
a DMG reads back as 1: SC's unused bits, FF03 and FF08-FF0E, the sound
registers' write-only halves and unused bits, FF15, FF1F and FF27-FF2F
(`rdg_iomask`).

**Answered by rdgsys:** DIV, TIMA, TAC, IF, IE, SC (a transfer sends SB to
`rdg_putc` and completes at once: nobody is on the other end, so SB reads FFh
and IF bit 3 is set), LCDC (the LCD starting restarts the frame
at line 0), STAT and LYC writes (rescheduling `S_TSTAT`), OAM DMA (FF46,
160 bytes at once), and LCDC/SCY/SCX/BGP/OBP0/OBP1/WY/WX writes, which go
through the raster log (section 11). Every other register is stored.

## 10. Cartridges

**The package reads the header before it runs anything** (`rw_hdrchk`, on
both load paths, while what was claimed can still be given back), and
refuses with a toast naming why:
- **the header checksum** (014Dh, the boot ROM's own test: x = x - byte - 1
  over 0134h-014Ch) wrong - "not a GB ROM", the file not a cartridge;
- **0143h = C0h**, a game for the Game Boy Color alone - "needs GB Color".
  A game for both (80h) runs as a Game Boy, and `.GBC` is a RealDMG
  document beside `.GB`, the extension such games mostly carry;
- **a type rdgsys does not have** - "MBC xxh unknown". The ones it runs are
  00-03, 05-06, 08-09, 0F-13 and 19-1E (`rw_mbcok`, a bit each); MMM01,
  HuC1, HuC3, MBC6, MBC7, TAMA5 and the Pocket Camera used to fall through
  to "none" below and run with their banks wrong, saying nothing.

The header's byte 0147h picks the MBC: none, **MBC1**, **MBC2**, **MBC3**
or **MBC5**. Bank 0 written to a bank register reads as 1 on MBC1, MBC2 and
MBC3 (not MBC5). RAM enable is recorded in `S_RAMEN` and not enforced: a
game that writes RAM it has not enabled keeps what it wrote.

### 10.1 Cartridge RAM and its battery

**A000-BFFF in the view is a window onto one 8KB bank**, so reads and
writes there are plain memory, in the interpreter and in translated code
alike. The other banks are kept elsewhere.

- **Where the banks live.** The host keeps them in `S_RAMSEG`, which holds
  `S_RAMBANKS` 8KB banks, bank 0 first. `S_RAMCUR` records what the window
  holds.
- **Switching.** A bank switch is a write to 4000 on MBC3 and MBC5, or a
  high-bits or mode write on MBC1 in mode 1. `rdg_rammap` copies the window
  out to its bank and the new bank in: 16KB moved, on a switch games make
  rarely.
- **One bank.** With `S_RAMSEG` 0 nothing ever moves; that is the bare
  harness, and any cartridge with one bank and no battery.
- **What the package claims.** The header's RAM size code decides: code 1 is
  2KB, 2 is 8KB, 3 is four banks, and 4 and 5 are also kept as four banks.
  An MBC2 has 512 bytes. The package claims the banks
  (`rw_ramsetup`) when there is more than one bank, a clock or a battery,
  and fills them with FFh, as an unsaved cartridge reads.

**A battery cartridge is saved when its window closes.** Battery types are
03h, 06h, 09h, 0Fh, 10h, 13h, 1Bh and 1Eh.

- **When and where.** The close negotiator (`rw_onclose`, SPEC.md 75.1)
  runs on the UI task. It writes `<game>.SAV` **beside the game**, in the
  folder the ROM was opened from (`rw_romdir`, kept at the load), then
  always lets the close go ahead. Opening the game reads the file back from
  the same folder.
  - **There is no File > Close**: the window's close box, the locator's and
    the dock's are the ways out, all three through the negotiator - and a
    "Close" item was too easy to read as closing the game rather than the
    program. (When there was one, it had to call `rw_onclose` itself: a
    close a package asks for (`OSAPI_WM_CLOSE`) is never put to its
    negotiator, SPEC.md 75.2.)
  - It was `SYSTEM\APPDATA` on the game's volume until the second field run.
    A game disk has no such folder unless someone makes one. Beside the ROM
    is also where every other emulator puts it.
- **The format is the common one.** The file is the RAM, at its header size.
  For a clock cartridge it is followed by VBA's 48-byte trailer: ten dwords
  (the live then the latched registers), then the time it was written, as
  64-bit seconds since 1970. So a save moves to and from other emulators.
- **Reset.** Machine > Reset keeps the RAM and the clock, as the cartridge
  would (`rw_ramkeep`).

### 10.2 The MBC3 clock

**The five clock registers are banks 08h-0Ch of the window.** Selecting one
fills A000-BFFF with its latched value.

- **Writes.** A value the game writes there is taken into the register when
  the window changes or the clock is latched (`rdg_rtccap`).
- **Latching.** Writing 00h then 01h to 6000-7FFF latches the live registers
  (`rdg_rtclatch`).
- **Counting.** The clock counts GAME time: `rdg_rtcframe` adds 1,097/65,536
  of a second each Game Boy frame, unless DH's halt bit is set.
  - Seconds and minutes count past 59 to 63 and wrap without a carry, as the
    chip does. Hours do the same past 23, to 31.
  - Day 511 to 0 sets DH's carry.
- **Switched off.** A save's trailer stamp is compared with `OSAPI_CLK_GET`
  when the game is opened. The seconds in between are added at once
  (`rdg_rtcadd`), so the clock has run while the game was put away. A system
  clock that reads earlier than the stamp adds nothing.

`tests/gbsave.py` is the gate. It runs GBSAVE.GB (`tools/gbprof/gbsave.asm`,
ours: MBC3, timer, four banks, battery) twice through a close.
- It checks that the four banks are four, and that the clock counts two
  seconds in 120 frames.
- It checks the `.SAV` on the flushed floppy.
- On the second boot it checks that the counters come back, with the clock
  past where it was saved.

### 10.3 The bank cache: cartridges over 256K, and the room for the recompiler

**A cartridge that does not fit as one claim lives above 1MB** and its banks
come down as the game switches to them (`apps/realdmg/rwbank.inc`). It is
REALDMG-PLAN's wave 6, and the second field run is why it came first: Kirby's
Dream Land (256K) took the heap a 640KB Victory had, so its 64KB recompiler
cache could not be claimed and the game ran interpreted (its log has no J).

- **The plan** (`rw_plan`), from the size probe and `OSAPI_MEM_AVAIL`,
  taken AFTER the damage memory and the recompiler's 80KB are claimed: the
  third field run's logs showed the heap the plan saw (346KB) holding the
  ROM and the recompiler with room over, and still no recompiler - the
  ROM's own read rebuilds the disk caches `OSAPI_MEM_AVAIL` counts as free,
  in exactly the room the cache was to have. So the recompiler is claimed
  first, and given back only if the cartridge then fits no other way. One
  claim when the ROM fits with some slack (24KB) beside it - and the
  recompiler is NOT kept a second time: it once was, the claim having moved
  in front of the plan, and the field Victory's 179K heap gave Kirby's
  Dream Land 5 slots where 9 fit, 50-80 fetches a second full screen and
  12-15% of real time in its window. Otherwise, when
  `OSAPI_XMEM_CAPS` has room for banks 1 on: the cache, with **K slots of
  16KB**, as many as the heap affords after those, at most 40 or one a
  bank - and **at least 8 (`RW_SLOTWANT`) while the recompiler still has
  memory to give up**: the second segment goes first, then the 64KB table
  for the 16KB one, and only then are 4 taken, and 4 with no recompiler at
  all. The fourth field run is why: on a 116K heap with SOUND.DRV mounted
  the plan stopped at 5 slots with both kept, and Kirby's Dream Land
  fetched ~95 banks a second over a 286's XMS at 15% of real time - in its
  window as in full screen, with the recompiler on or off, so it read as
  the interpreter running whatever the status line said. Otherwise one
  claim without that room, as before, and then
  over 256K is refused (`over 256K wants XMS`). The probe answers a KB over
  an exact size (SPEC.md 20.14.6.3), so the plan takes whole banks; bank 0's
  header must then say the same size, which a compressed file - read raw
  here - does not. The log's head gives the heap the plan saw.
- **The load** (`rw_bload`, the UI task): the slots, one `OSAPI_XMEM_ALLOC`
  for banks 1 on, then the file through `OSAPI_FILE_READ_SEQ` a bank at a
  time - bank 0 straight into the view's 0000-3FFF (so `rdg_init` is handed
  the view as the image and copies nothing), banks 1 to K-1 into slots, the
  rest through the last slot, and every one copied up as it comes.
- **The table is the host's** when `rdg_nres` is not 0: `rdg_init` zeroes it
  and calls `rdg_bfill`, and a switch to a bank with no segment goes to rdgsys
  (the core's `mbc_set` leaves through `S_MBCVEC` when the entry is 0), whose
  `rdg_setbank` calls `rdg_bmiss`.
- **A fetch** (`rw_bfetch`) takes the slot whose bank the game switched to
  LONGEST AGO, by frame: every switch stamps `RDG_BUSE` (6C00h, a word a
  bank) with `S_FRAMES` - the core's `mbc_set`, the recompiler's bank
  stores and `rdg_setbank`, which MBC2's switches and MBC1's high bits come
  through - and the victim is the slot of greatest age, the frame now less
  the stamp, unsigned, so the counter's wrap costs nothing; of equal ages the
  first. It replaced a count halved at every fetch, which the compatibility
  sweep found two ways wrong: a bank just fetched starts at a count of 0 or
  1, so two banks the game alternates between and neither of which the load
  put in a slot take each other's slot at every switch (Pokemon Yellow), and
  MBC2's switches were never counted at all (Final Fantasy Adventure, 1% of
  real time). Replayed over sixteen games' bank switches from Peanut-GB
  (4,000 frames of the profiler's input script, 8 slots) the stamp misses
  31 times on Yellow where the count missed 1,467, 26 on Super Mario Land 2
  where it missed 102 and 5,984 on Donkey Kong Land where it missed 10,180,
  and matches true LRU elsewhere. `mbc_set` lives in page ECh's CB half
  (`rdggen.CBTAILS`): a tail page is copied whole, and stamping does not fit
  the main half. `OSAPI_XMEM_COPY` is UI-task context, so the WORKER posts
  the bank (`rw_bankreq`), wakes the window and YIELDS until the UI task has
  it in (`rw_onwake`, and `rw_fsenter`'s wait) - the wake makes the UI task
  runnable at once, where the tick's sleep this replaced made every fetch a
  whole tick and capped them at 18 a second (gbbank's cartridge, which
  switches among all 64 banks, went from 18 to ~500). Full screen runs the
  machine on the UI task, which fetches for itself. A window closing while
  the worker waits ends it at `OSAPI_TASK_ALIVE`.
- **What it costs is nearly nothing**, because a game's working set is small.
  gbprof's LRU column (`tools/gbprof.py`), two minutes of its platformer
  script: Kirby uses **6 banks of 16** in play and Looney Tunes **8 of 64**,
  and with that many slots or more a fetch comes **under once a second**.
  Looney Tunes (1MB, MBC5) runs at **101% of real time in its window on
  86Box's 286** with 18 slots and one bank fetched in ten seconds, and with
  the slots cut to 4 it fetched 12 in ten seconds and kept running.

### 10.4 Save states

**Nine slots a game, `NAME.SS1` to `NAME.SS9` beside it** as its `.SAV` is
(10.1). 1 to 9 picks the slot, F5 saves it and F8 loads it, in the window
and in full screen alike; Save State > Save State and Load State are the same two.
The slot is the session's, slot 1 at the start.

**No buffer is claimed: the machine is already where a state file wants
it**, and the file is written from there and read back there
(`apps/realdmg/rwstate.inc`):

| at | bytes | what | where it lives |
|---|---|---|---|
| 0 | 32,768 | VRAM, the RAM window, WRAM, OAM, I/O, HRAM | the view's 8000-FFFF (3) |
| 32K | 0, 8,192 or 32,768 | the cartridge's RAM banks (`rdg_stram`) | `S_RAMSEG` (10.1) |
| then | 2,048 | the record (`RDG_STREC`, rdgdefs.inc's `ST_*`) | the view's 7800-7FFF |

- **The record** is the header - `RDST`, the format's version, the RAM
  part's KB and the ROM's header 0134h-014Fh - then the 256-byte state
  block, `RDG_SNAP` and the raster log, so the first frame drawn after a load
  is the one that was saved. rdgsys composes it (`rdg_stcompose`), checks it
  (`rdg_stcheck`) and applies it (`rdg_stapply`); the package and the bare
  harness share all three.
- **The RAM part** is there when `S_RAMSEG` is: a bank switcher's four, or
  the one bank a battery or a clock claims - which must be saved, because an
  MBC3 can have a CLOCK REGISTER in the window and bank 0 only in the
  segment. One bank and nothing to keep is the window itself, in the view.
- **Why this order.** Every part but the last starts at a multiple of 32KB
  and the RAM part is a multiple of 8KB, so each `OSAPI_FILE_APPEND` starts
  on a cluster - its rule - on any volume whose clusters are 8KB or less,
  and the record, last, may be any length. 34, 42 or 66KB.

**The state block comes back by a WHITELIST** (`rdg_sttab`): the registers,
the interrupt and HALT state, the MBC's banks and modes, the frame count,
the clocks and timers, the MBC3 clock and the two square channels. The rest
is the host's - segments, far vectors, the recompiler's place, the profile,
scratch - and stays as it is, and so do the buttons held now (`S_JOY`) and
what the CARTRIDGE says rather than the game (`S_MBC`, `S_NBANKS`,
`S_BMASK`, which `rdg_init` set from the ROM this window is running). A
field added to the block is the host's until it is listed, which is the
safe way round. Nothing the file says is trusted to INDEX: `S_RLOGN` is
held to `RDG_RLOG_MAX`, and `S_RAMCUR` to a bank the cartridge has (the
mask `rdg_rammap` uses) or, on an MBC3 with the clock, 88h-8Ch - else bank
0 - so a damaged or edited `NAME.SSn` whose header still matches cannot
send a RAM bank's copy past the claim or the bank table's write past the
view. After the fields, what the host derives from them: the bank
in 4000-7FFF (`rdg_setbank`, which may fetch it, 10.3), and the spin-wait's
"known not to spin", which was another machine's. The renderer draws the
next frame whole (`RDG_DFULL`).

**The recompiler's cache is KEPT across a load**: its code is the ROM's
(16.3), which a load does not change, and the HRAM routines it translated
check their own bytes before they run (16.3). Static's fixed table
(16.8) survives a load with it.

**The machine is still while the file is written or read.** In the window
the worker backs off at the top of its loop while `[rw_stq]` is set - full
screen's handshake (15): the UI task raises the byte, then waits for the
worker's `[rw_incore]` to fall, and raises it to 2 once it has, which is
what lets `rw_bmiss` fetch a bank on the UI task during a load and never on
the worker. In full screen the machine is the UI task's own, between frames.

**A load touches nothing until the file is a state of THIS game.**
1. Its size: the probe `OSAPI_FILE_READ` with no room answers `FERR_BIG`
   with the file's KB plus one (from the directory entry), which must be
   this game's. A missing file is "Slot n is empty".
2. Its record, into `RDG_STREC` - scratch - and `rdg_stcheck`. The probe
   allows up to 1,023 bytes over, which a read of the record would carry
   into the Game Boy's 8000h, so 8000-83FF are kept aside at `RDG_STKEEP`
   and put back if the read delivered more than 2,048. Another game's, an
   older format's or a damaged file is "not this game".
3. Then the view's half and the RAM banks, and `rdg_stapply`. A read that
   fails past that point resets the game (Machine > Reset's `rw_reset`)
   rather than leave it half one machine and half another.

A save that fails part way deletes its file: a half-written state is no
state. The battery's `.SAV` is still the close's alone (10.1) - a load
replaces the RAM, and the next close saves what the RAM then is, as on any
emulator.

**The gate is a REPLAY** (`tests/gbstate.py`). The bare harness's MODE 13
takes a state 200 frames before the end of a scripted run, sums the picture
and the Game Boy's 8000-FFFF at the end, puts the state back and runs the
same 200 frames on the same buttons: the sums must agree. It does so twice,
into a machine just reset (`rdg_init`, the cache emptied) - which is what
makes a field the whitelist misses show, and leaving the clocks out turns
Super Mario Land's and Kirby's Dream Land's legs red - and into the machine
that ran on, its translated code kept. The bank fields are in the whitelist
but neither game can test them: both select their bank again before they
touch 4000-7FFF at a frame's end, and even a state put back with the bank
deliberately wrong replays exactly. The package's half on MartyPC writes
`CLOCK0.SS3`, reads it back off the floppy at 34,816 bytes, and loads it in
the window and in full screen.

### 10.5 Replays

**A replay is a save state and the buttons from there, a frame at a time**
(`apps/realdmg/rwreplay.inc`). F6 - Save State > Record Replay - saves the slot's
state exactly as F5 does (`rw_stwr`, the save's body without its words) and
keeps the joypad from then on; F6 again writes the buttons beside the game
as `NAME.RP1` to `NAME.RP9`, the slot's digit. F7 - Save State > Play Replay -
reads that file, loads the slot's state exactly as F8 does (`rw_strd`) and
gives the game the file's buttons in the keyboard's place until they run
out; F7 again stops it. Both work in the window and in full screen. It is
for MEASURING: the field could not run one game the same way twice, and a
log of a replay against a log of the same replay on another build is the
first comparison that is about the build and not the play.

**The buttons change only between two frames** while a replay records or
plays. `rw_keys` writes `S_JOY` once before every `rw_frame`, but a run
that ends short of a VBlank - the LCD off, a flip's short budget (15.4) -
is followed by another `rw_keys` mid-frame, and how many there are depends
on the machine's timing, not the game's. So `rw_frame` sets `[rw_rpatb]`
when its run ended at RDGX_FRAME, and with a replay on `rw_keys` writes
`S_JOY` only then (`rw_rpkey`): frame *n* gets the same buttons in every
play, on any machine, at any speed, whatever the recompiler did. It costs
nothing a game could see - games read the pad in VBlank - and nothing at
all without a replay.

**The file is the buttons, RLE**: a byte of `S_JOY` and the frames it held
for (1-255), after a 16-byte head - `RDRP`, the format (1), the `S_JOY` the
recording began with (a press is the JOYPAD interrupt's, so the first
frame's must find what the recording's found), the state's record summed
(`rw_rpsum`), the frames as a dword, the machine summed at the end
(`rw_rphash`) and the pairs' bytes. A play of a game is a few hundred
bytes; the claim is 8KB, 4,088 changes of the buttons, and a recording that
fills it ends and is written ("full, saved").

**Two sums make it honest.**
- **The state's**: a replay is of THAT state, so the 2KB record the slot's
  file carries (10.4) is summed at the recording and again when the play
  loads it. A slot saved over since is "Slot n was saved over", and the
  play does not start - it would be the right buttons into the wrong game.
- **The machine's at the end**: VRAM, WRAM, OAM and HRAM, summed when the
  recording ends and when the play runs out. A play that comes out
  anywhere else says "OUT OF STEP" instead of "ended" (`[rw_rpres]`). Not
  the cartridge's RAM window, which a load may leave holding another bank
  until the game touches it, nor the I/O registers, which the core brings
  up to date when they are read rather than as time passes. It is what a
  machine that is not deterministic would be caught by.

**Both ends are between frames.** F6 grabs the machine as a save does
(`rw_stgrab`): parked between frames, as a rule, and the recording ends
there; if the last run did not end a frame, at the next that does
(`[rw_rpstq]`). A play ends at the boundary its buttons run out at - the
same one.

**Who does what.** The worker records and plays (`rw_rpkey`, on
`rw_keys`); the UI task claims, writes, frees and speaks (SPEC.md 20.6 rule
7). The worker finishing one queues `[rw_rpq]` and wakes the window
(`rw_onwake` calls `rw_rpserve`); in full screen `rw_keys` IS the UI task,
which serves it at once. Reset, Load State and Machine > Load Game are refused
while one runs ("A replay is running"): each is another machine.

`tests/gbreplay.py` is the gate: GBPAD.GB (ours) folds the buttons and the
frame into a sum at C000h every VBlank, and on 86Box's 286 - in the window,
and in full screen with the canvas, the recompiler and the page flips - a
scripted play is recorded, played back and must end in step, and played
again with that sum changed under it and must end OUT OF STEP.

## 11. The raster log

A write to LCDC, SCY, SCX, BGP, OBP0, OBP1, WY or WX during the visible lines
is logged in `RDG_RLOG` as (line, register, value), at most 192 a frame - the
line it takes effect on, the next one for a write in HBlank - and the first
one in a frame first snapshots all eight as the frame began into `RDG_SNAP`
(LCDC, SCY, SCX, BGP, OBP0, OBP1, WY, WX), tagged with the frame's number in
`S_SNAPTAG`. It is the renderer's input (section 13).

## 12. The harness and the instruments

### 12.1 The bare harness

`tests/rdgcore/rdghost.asm` is a boot sector and a host: it loads itself, the
64KB core and a ROM (to 3000:0, up to 368KB) and runs it with no os8088.
`tests/rdgcore/rdgimg.py` builds the floppy (`--patch OFF:HEX` breaks the core
on purpose for a negative control). Modes:

| mode | does |
|---|---|
| 0 `test` | runs N frames; the link port's bytes go to the screen and COM1 |
| 1 `bench` | runs N frames as fast as it can and prints the BIOS ticks |
| 2 `trace` | an `RDG_TRACE` core: COM1 carries trace records and nothing else |
| 3 `play` | as bench, pressing buttons on gbprof's platformer script, counters and ticks restarted at frame 800, then VRAM, OAM and the LCD registers on COM1 |
| 4 `idle` | as play, no buttons |
| 5 `render` | as play, composing every frame with `rdg_render` |
| 6 `damage` | as play, and every third frame composed both by `rdg_renderd` and whole, counting the frames that differ (section 13.2) |

It ends with `RDGHOST DONE frames F ticks T events E skipped S END`.

### 12.2 The trace

An `RDG_TRACE` core calls `S_TRACEVEC` at every dispatch. The record is 14
bytes, words little-endian: **PC, A, F (as the Game Boy holds it), BC, DE,
HL, SP, and now** (low 16 bits). An `RDG_TRACE` core skips no spin-wait, so
every loop iteration is in the trace.

### 12.3 The instruments

| | |
|---|---|
| `tests/gbcpu.py` | the correctness ROMs, with a broken-DAA control: Blargg's `cpu_instrs` (all), `instr_timing`, `mem_timing` and `halt_bug`, and 26 of Mooneye's DMG acceptance tests |
| `tests/rdgcore/rdgacid.py` | dmg-acid2 through `rdg_render`, pixel for pixel, with a broken-`LDH` control |
| `tests/gbwindow.py` | `REALDMG.O88` on MartyPC's VGA XT, 5150 Hercules and 5150 CGA: dmg-acid2 opened from the Disk window, running, and on the glass pixel for pixel through each writer - and full screen (sections 15 and 15.1) against the same reference, then back to the desktop |
| `tests/gbdamage.py` | `rdg_renderd` against `rdg_render` every third frame of a game, under QEMU (section 13.2) |
| `tests/rdgcore/rdgwin.py`, `tests/rdgspeed286.py` | the package's game speed and frames drawn on an 86Box machine, in its window or full screen, with a profile on request; `rdgspeed286` runs Super Mario Land and Kirby on the 286 (`$OS88_GB_ROMS`) |
| `tools/gbtrace.py`, `tests/gbtrace.py` | the differential trace against Peanut-GB, with a broken-`JR NZ` control |
| `tests/rdgcore/rdgbench.py` | cost per M-cycle on gbclock, or a game's frames against real time |
| `tests/rdgcore/rdgprof.py` | where the time goes, by handler, on MartyPC or 86Box |
| `tests/rdgcore/rdgshot.py` | a play image's last frame, drawn from its VRAM |

### 12.4 The Mooneye acceptance tests that do not pass

Of the 63 that are about a DMG (the release `tools/getgbtest.py` pins), the
core passes the 26 `tests/gbcpu.py` names. The others fail for four reasons,
each a model this core chose not to carry:

- **Instruction timing measured by OAM DMA** - `call`, `call_cc`, `jp`,
  `jp_cc`, `ret`, `ret_cc`, `reti`, `rst`, `push`, `pop`, `add_sp_e`,
  `ld_hl_sp_e` `_timing`, `reti_intr_timing`, `oam_dma/sources-GS`,
  `interrupts/ie_push`: they find an access's M-cycle by racing a DMA that
  blocks the bus, and the core's DMA is a 160-byte copy at once.
- **The PPU's variable mode 3, and STAT's finer rules** - every `ppu/` row but
  `intr_2_mode0_timing` and `intr_2_mode3_timing`: mode 3 lengthens with
  sprites and SCX on the hardware and is a fixed 43 M-cycles here, and the
  STAT line's blocking and LCD-on timing are not modelled.
- **EI's delay** (section 7) - `ei_sequence`, `ei_timing`, `rapid_di_ei`,
  `di_timing-GS`, `halt_ime1_timing2-GS`.
- **TIMA's reload delay** (section 6) - `tima_reload`,
  `tima_write_reloading`, `tma_write_reloading`, `rapid_toggle`; and the
  serial clock's phase, `boot_sclk_align`.

## 13. The picture

`rdg_render` (`apps/realdmg/rdgppu.inc`) composes the frame that ended at the
last `RDGX_FRAME` into a host's 5,760-byte buffer: **144 lines of 40 bytes,
the shade's bit 0 for pixels 0-159 (20 bytes, the leftmost pixel in bit 7)
and then its bit 1**. Shade 0 is the lightest. Every register is preserved;
its working memory is the view's 4900h-4B5Fh (and the tables up to 66FFh).
(`rdg_render4`, which composed the frame straight into `OSAPI_GFX_BLITP`'s
four planes, went when the damage path's `rw_planes` replaced it: nothing
called it.)

- **The state** each line is drawn with is the frame's own: `RDG_SNAP` with
  `RDG_RLOG` applied at each entry's line when `S_SNAPTAG` is the frame just
  ended (`S_FRAMES` - 1), the registers as they stand otherwise. VRAM and OAM
  are read as they stand: the frame is composed at VBlank's start, before
  the game's VBlank handler has touched either.
- **A byte - eight pixels - at a time, never a pixel.** The background is 21
  tile rows from the map, each already two bit-planes, shifted left by
  `SCX AND 7`. The window replaces it from X = WX - 7 when LCDC bits 0 and 5
  are set, WY <= LY and WX <= 166, with **its own line counter**, which
  counts only the lines it was drawn on. LCDC bit 0 clear makes the
  background and window colour 0 and every sprite visible over them.
- **Palettes** are four minterm masks: colour k's shade bits as 00h or FFh,
  so a byte's shade is `(c0 AND m0) OR ... OR (c3 AND m3)` for each bit.
- **Sprites**: the first ten in OAM order whose rows cover the line (8 or 16
  tall; a 16-tall sprite ignores its tile number's bit 0), then **sorted by
  X, equal X keeping OAM order**, and drawn highest priority first through a
  CLAIMED mask: a pixel the first opaque sprite took stays its, so a sprite
  behind the background still hides the sprites under it, and it shows only
  over background colour 0.

- **What a line costs is the code a 286 fetches**, since every byte of it
  crosses a bus with wait states: the background's fetch is unrolled
  (`ppu_fshift`, `ppu_fcopyb`, `ppu_fcopyw`), the two tile rows alternating
  between AX and DX and the outputs absolute; `ppu_list` builds the 21 tile
  addresses a line reads from its first column, once a map row and column;
  and with BGP the identity and no sprite on the line, the line is left
  where it was composed (`PPU_OSRC`) rather than copied through the palette.
  On 86Box's 286 Super Mario Land's composition fell from **39.8 to 27.0 ms
  a frame**, every line every frame (`tests/rdgcore/rdgbench.py --render`).

`tests/rdgcore/rdgacid.py` holds it to dmg-acid2's reference, all 23,040
pixels. It is the composer only: the writers that take the planes to a
screen are the package's.

### 13.2 By damage: rdg_renderd

**`rdg_renderd` is `rdg_render` that composes only what changed since its last
call.** It composes only the lines whose inputs changed, and writes only the
lines whose pixels then differ. The caller's buffer is ES:DI, and it holds
the last frame. `[rdg_dmgseg]` is `RDG_DMG_SIZE` bytes of the host's memory
(about 14.9KB); with it 0, `rdg_renderd` is `rdg_render` and every line has
changed. On return, `RDG_DCHG` in that memory holds a byte for each line (1:
it changed), and AX holds the count.

- **A line's inputs** are its eight registers and the window's line counter,
  the map cells and tile rows under its background and window, and its
  sprites: their OAM entries and their tiles.
- **VRAM and OAM are compared** against a shadow of what the last call saw
  (`ppu_dscan`). The comparison runs `repe cmpsw` over the tile data a tile
  at a time and over both maps a word at a time. A changed tile, map byte,
  map row or sprite sets its byte, and a sprite that changed marks the lines
  it left and the lines it covers now.
- **Only the VRAM the game wrote is compared** (13.2.2): the core marks a
  16-byte block as it stores to 8000-9FFF, and the scan visits the marked
  blocks alone.
- **Each line is then looked up** (`ppu_ldirty`). Its registers are compared
  with what the last call kept for it. Its background row and window row are
  judged once per map row and first column and the verdict kept
  (`ppu_rowdirty`). Each sprite that may cover it has its tiles checked.
- **FAST**: in a frame whose raster writes are the last call's frame's -
  none after none, or the same writes at the same lines (a status bar's
  split every frame: `ppu_logsame`, up to 64 entries kept in `RDG_DLOG`) -
  and with the registers as they were at its start, every line's registers
  are known to be the same. The rows and sprites are then judged a map row or a sprite at a
  time (`ppu_dfast`), not a line at a time. With nothing changed at all, no
  line is visited.
- **`RDG_DFULL`** = 1, set by the host after it cleared the screen or when its
  buffer holds something else, composes and writes everything, and takes
  the shadows whole.
- **A SCROLL is composed whole** (`RDG_DFULL` = 2, set by `rdg_renderd`
  itself): when SCY or SCX moved since the last call - as the frame began,
  or in a raster write the last call's frame had not - every line under it
  changes anyway, so the VRAM scan would only find what is composed
  regardless. Every line is composed, and `ppu_dput` still writes only
  those that differ. The shadows are KEPT CURRENT by the marks' scan
  (13.2.2: what the game wrote, about a block a frame) with OAM's taken
  whole (`RDG_DSCR`: a scroll moves every X, and the 1,280 line marks a
  frame its entries made were read by nothing), so the first still frame
  after a scroll compares one frame's writes (the scroll round, 13.3). Until
  then the shadows were not even taken while it scrolled, and the first
  still frame compared everything the scroll had streamed in - a stale
  shadow can only make a call compose more, never less, so what it found
  changed was a line too many and never one too few. It was composed whole
  until the rolling-background round (13.3), which made a game whose
  camera moves every other frame pay a whole frame for every still one:
  Kirby's Dream Land in play 50.67 -> 48.75 ms a frame on `286-victory`,
  Super Mario Land unchanged. Scrolling alone keeps it whole (below). The bare harness's
  MODE 7 (`rdgbench.py --renderd`, the package's composer timed) read a
  scrolling Super Mario Land's frame, the CPU with it, 26.32 ms before and
  24.40 after on `286-victory`.
  Super Mario Land splits its status bar every frame, so until the log was
  compared it was never FAST: standing still on the Victory's 86Box twin,
  judging 144 lines was 5 ms a draw and the picture 23 frames a second; it
  is 41 now, the judging 0.15 ms.
- **INTERLACE** (`[rdg_ilace]`, Machine > Interlace: Off, On, Scrolling,
  Doubled;
  off by default): a call composes the changed lines of one parity and
  leaves the other's pending (`RDG_DPEND`, a byte a line) for the next call,
  whose parity (`RDG_DPAR`) is the other. A line pending from the last call
  is composed whatever its parity, so nothing changed is lost for more than
  one draw; `RDG_DFULL` = 1 composes both. **Scrolling** (2) defers a parity
  only in a scroll's whole frame (`RDG_DFULL` = 2) - where every line changes
  and the picture is moving anyway - so a still picture is every line's,
  the moment the scroll stops. The first still frame after a scroll is
  whole and halved here too, as before 13.3's round: half of a whole is
  less than half of what a comparison finds and judges, and compared it
  cost Kirby's Dream Land 38.95 -> 39.55 ms. A still frame after an interlaced call that
  deferred nothing exits as it would without interlace (`PPU_PANY`); it
  walked all 144 lines every draw until the fourth field run's round. It halves the composition of a picture
  that changes everywhere, a scroll above all, at a price the requester
  named beforehand: a moving edge is drawn half a frame apart on
  alternate lines. It is there to be looked at on the Victory, not as a
  default. The bare harness never sets it, so its frame need not reach
  `RDG_DPEND`.
- **Doubled** (3) is Scrolling without the combing: a scroll's frame
  composes the even lines - always the even, so the copy below is always
  the line just composed - and each odd one is that line again
  (`ppu_dput` of `[PPU_OSRC]`), pending as in Scrolling and composed the
  moment the scroll stops. Two lines are not doubled: a line whose own
  scroll did not move (`PPU_LSCR`, each line's SCY:SCX at the last call;
  Super Mario Land's status bar) is composed whole, as it would otherwise
  lose its detail or, left alone, show half of two digits for as long as
  the scroll lasts - and the still frame after a scroll (`RDG_DSTALE`) is
  compared and defers nothing. It writes the card as
  much as Off does, which is what it costs against Scrolling. Drawing
  every frame on the Victory twin, a frame:

  | | Off | Scrolling | Doubled |
  |---|---|---|---|
  | Super Mario Land (CPU alone 6.18 ms) | 22.70 ms | 18.54 ms | 21.01 ms |
  | Kirby's Dream Land (15.38 ms) | 48.88 ms | 39.04 ms | 45.22 ms |

  (After 13.3's rolling-background round; before it, 24.35 / 19.36 /
  21.88 and 50.67 / 38.95 / 45.54.)
- **WHOLE**: a frame composed whole (`RDG_DFULL`, and no parity deferred)
  judges no line - it keeps each line's registers for the next call, as
  `ppu_ldirty` would, and composes. The CPU and `rdg_renderd` every frame
  on `286-victory`: Super Mario Land 23.98 -> 22.93 ms.
- **The snapshot's tag** (`S_SNAPTAG`, the low byte of the frame RDG_SNAP
  was taken in) is aged at every VBlank whose frame took none: a game with
  no raster write left it alone, so every 256th frame took the stale
  snapshot as its own and was composed from it - all zeros, an LCD off, a
  white frame. `gbdamage`'s interlaced legs found it.
- **What it costs** an 8088 is the comparison, ~20 ms of 8KB a call. It
  saves ~4 ms of composition for every line it skips. On Super Mario Land it
  composed 38 lines a call of 144, and on Kirby's Dream Land 27.

It took gbclock's do-nothing variant in a Hercules window on MartyPC's 5150
from **43% of real time to 100%**: the drawing had been most of the machine.

`tests/gbdamage.py` is the gate. The bare harness's MODE 6 composes every
third frame both ways, by damage into a buffer it keeps and whole by
`rdg_render`, and counts the frames whose pictures differ. MODES 8 and 9 are
MODE 6 interlaced, On and Scrolling, and MODE 10 Doubled, composing every
frame as the package does and a checked one twice; without the pending lines composed they fail
on 151-499 pictures of 500.
- It runs on gbclock's frame counter (ours), and with `$OS88_GB_ROMS` on
  Super Mario Land and Kirby's Dream Land.
- Taking out the map-row marks fails it on 497 frames of 500.

### 13.2.2 VRAM marked as it is written

**A store to VRAM leaves the fast path.** `write8`'s first test is `cmp
bh,0A0h / jb` where it was `test bh,bh / jns`, so a store to 8000-9FFF goes
the way the cartridge's do, to `wr_slow` (or `wr_sw`, ahead of the I/O
timing), and on to `vr_wr`: the byte stored, unless it is already that, and
its 16-byte block marked in `RDG_VMARK` (512 bytes in the view at 7200h).
The direct `LD (nn),A` the translator chooses (16.6) starts at A000h for
the same reason, and the read-modify-write templates (`rmw_lines`) fall to
the interpreter for VRAM as they do for the cartridge.

**`ppu_dscan` then compares only the marked blocks** - the block PAIRS
`vr_new` listed as it marked them for the first time (`RDG_VLIST`, 64 in
the view at 4610h; in play about one a frame), or, when the list ran out,
`repe scasw` over the marks, two blocks a word - against the same shadows, with the same
change bytes as the whole compare set, so nothing after it changes. The
copy idiom (`cpy_ff`, 16.6) moves bytes with `REP MOVSB` and marks none:
when either of its ranges starts in VRAM it sets `RDG_VALL` and the next
scan compares all of it, as before the marks. A full frame (`RDG_DFULL`)
clears the marks. A Game Boy stack in VRAM (`PUSH`, `CALL` are the 8086's
own) would be unseen; no game puts one there.

- **What it buys**: the whole compare was ~38.7K clocks a drawn frame on a
  286 - in Super Mario Land full screen, drawing every frame, 17.7% of the
  machine. With the damage renderer every frame on `286-victory`, Super
  Mario Land 499 -> 463 ticks (2,000 frames).
- **What it costs**: ~80 clocks a VRAM store, once per Game Boy frame
  rather than per drawn one - the CPU alone moved by one tick in 124.
- **The gate** is `tests/gbdamage.py`: with the mark not set, 500 of 500
  pictures differ.

### 13.3 What a line costs, and the cuts that did not pay

Kirby's Dream Land IN PLAY (section 15.3), drawn every frame onto VGA on the
Victory twin, spends its composing time flat across the composer: the fine
scroll's shift 15%, the sprites' fetch, shade and placing ~21%, the line's
own setup 9%, the buffer and the card ~14% - no routine is more than a
sixth, and no instruction in one more than 1% (`tests/rdgcore/rdgprof.py
--vga --seg2 --hostips`). Each change was timed alone (`rdgbench --vga`,
which is exact here: the same build gives the same ticks):

- **Kept**: the line lists hold the sprite's OFFSET in OAM (4 x index),
  and its tile row's address is two byte adds and an OR - the tile's low
  byte is 16 x (n AND 0Fh) and the row's 2 x 0-15 never carries. 0.7%.
- **Kept**: the changed line to the card where it is decided (`ppu_dvga`,
  above). 0.7% in Kirby's Dream Land, more where more lines are unchanged.
- **Kept**: a sprite row whose two bytes are both on the line (B 0-18:
  all but the edges) is placed as one word - claimed, masked behind the
  background and drawn with word operations - where it was two calls of a
  byte routine and the registers shuffled between them. Kirby's Dream
  Land 1,156 -> 1,107 ticks (52.9 -> 50.7 ms a frame), Super Mario Land
  540 -> 532; the sprites ~19% of Kirby's frame -> ~13%.
- **Refused**: skipping the shift for a byte from two blank tile rows. It
  is 59% of Super Mario Land's bytes and 31% of Kirby's Dream Land's, and
  it cost the latter 4.5% - the test and the branch on every other byte
  outweigh the shifts they save.
- **Refused**: testing a sprite's priority bit once a row instead of once
  a byte (two put routines): a few cycles, and a tail the stack checker
  cannot read.

**The rolling-background round** (REALDMG-PLAN lever 6, the owner's form:
the background kept wider than the screen and copied at the scroll's
offset) was measured before it was built, and what it would cost is the
scan. Every scrolled frame would have to run `ppu_dscan` to keep the image
current, which `rdg_renderd` skips there today: **+2.4 ms a scrolled frame
in Super Mario Land**, against ~1.5-2 ms of tile lookups the image takes
away (`ppu_list` and the address load before each tile row - the shift
itself stays, the card cannot do it for free, 15.3). A net loss for Super
Mario Land and ~5% for Kirby's Dream Land at best, so it was not built.
What the measurement found instead, each timed alone, drawing every frame
with interlace Off:

- **Kept**: the still frame after a scroll is compared, not composed whole
  (13.2, `RDG_DSTALE`). Kirby's Dream Land 1,107 -> 1,065 ticks (50.67 ->
  48.75 ms a frame), Super Mario Land unchanged. Composing it whole was
  never needed for correctness - a stale shadow only over-reports - and
  taking the scan away from that frame instead fails `gbdamage` (Super
  Mario Land 3 frames, Kirby's Dream Land up to 56 frames differ), which is
  the negative control.
- **Kept**: a line whose 21 tile addresses are ONE tile (a sky) is that
  tile's row, turned by the fine scroll, stored 20 times as two `rep
  stosw` - asked once a map row in `ppu_list` (`[BP+42]`, a `repe scasw`
  over the list it has just built), not once a line. 44% of Super Mario
  Land's playfield lines are one tile. Super Mario Land 532 -> 496 ticks
  (24.35 -> 22.70 ms, **-6.8%**), Kirby's Dream Land 1,065 -> 1,068.
- **Refused**: the same idea a segment at a time. 67% of Super Mario Land's
  bytes on a line that is not all one tile are in runs whose two tiles are
  one (32% of Kirby's Dream Land's), so the line was cut into fills and
  shifts (`ppu_lsegs`, `ppu_fseg`) wherever the fills were most of it.
  The frames were identical and both games got slower: Super Mario Land
  496 -> 520 ticks, Kirby's Dream Land 1,068 -> 1,123 - and 515 / 1,120
  with the gate tightened to 16 filled bytes in at most 3 segments. On a
  286 the segment loop's per-byte and per-segment overhead and the cut
  itself cost more than the shifts saved; `ppu_fshift`'s unrolled bytes
  are the floor for a line of mixed tiles.

**With the recompiler's round beside it** (the `gameboy-static` branch:
fewer events, a 64KB block table, VRAM marked as it is written, the shift
right past 4), the two sets STACK: from 532 / 1,107 ticks for Super Mario
Land / Kirby's Dream Land, this round took 36 / 39 and that one 43 / 82, and
merged they read 451 / 970 against the 453 / 986 the sum predicts. Then,
profiled again (Kirby's Dream Land: the sprites 20.6% of the machine, the
card 14.3%; Super Mario Land: the sprites 8%, the card 14.4%, no
instruction over 1.6%):

- **Kept**: `ppu_oam` builds the line lists again only when a sprite's Y
  moved (`PPU_OAMY`, the 40 Ys it built from; since the scroll round, below,
  it moves only the rows those sprites left and joined) - they hang on nothing else,
  and a scroll moves every X and no Y. 451 / 970 -> 449 / 965. **No row can
  see this one go wrong**: the A/B frames and dmg-acid2 compose one frame
  each, so the lists are always built once, and `gbdamage`'s reference
  shares `PPU_LSPR` with the composer it checks - it rests on the build
  reading the Ys alone, and on nothing else writing the lists.
- **Kept**: `ppu_sprites` counts in registers - DI's distance into
  `PPU_SPR` is the sprites found, a list end in `PPU_TMP` replaces a
  decremented byte, LY + 16 is DH - and draws to BP's end without saving
  CX around each call. -> 448 / 959.
- **Kept**: `ppu_dvga` takes 40 x the line from `ppu_dput` instead of
  multiplying again. -> 448 / 957 (20.51 / 43.80 ms a frame).
- **Refused**: writing each plane's span from the first word that differs
  to the last (a backward `repe cmpsw` for the last) instead of all twenty.
  475 / 1,017: the bookkeeping is ~135 clocks a line and the writes it
  saves are cheaper than that on the Victory twin's card.
- **Refused**: dropping a write of what is there in the interpreter's
  `rdg_iowrite`, as the recompiler's `io_wr` already does (REALDMG-PLAN
  lever 7): level with the recompiler off, 561 ticks either way.

**The sorted sprite list cache**, after the recompiler's next three commits
merged (dead flags, bank selects by MBC; 448 / 957 -> 445 / 939, theirs
stacking again). Each line's sprites as `ppu_sprites` found and sorted them
were kept in the view (up to five a line, 144 x 21 bytes at 7400h), used
while nothing they hang on changed, and forgotten by `ppu_scinv` every
frame - scroll or not, since the scan does not run while it scrolls: the
lines an OAM entry that changed covered or covers (against a copy of OAM),
the lines a sprite whose tile was written covers (`RDG_DTD` and the
unscanned `RDG_VMARK`), every line when the scan took the shadows whole.
Only `rdg_renderd` kept it, so `gbdamage`'s reference stayed independent.

- **Refused**: keyed on the whole entry. 460 / 939 against 445 / 939: in
  Kirby's Dream Land `ppu_sprites` fell 10.2% -> 7.9% of the machine and
  `ppu_scinv` cost 1.6%; in Super Mario Land `ppu_scinv` alone was 3.6%
  against a fall of 4.7% -> 4.0%. A scroll moves every X, and the games
  rewrite their OAM entries nearly every frame.
- **Refused**: keyed on Y, tile and attributes only, X read again from OAM
  and the short list sorted at each use, so a scroll keeps it. 459 / 942:
  it is not X alone that churns - Super Mario Land's entries change in Y,
  tile or attributes nearly every frame - and the check, which has to run
  every frame, costs what the hits save.

**The scroll round** (2026-10-21, the composer's half; the research behind
it and the hardware half are REALDMG-PLAN's scroll round). A scrolled
frame is composed whole, and the profile of one had no routine above a
sixth and no instruction above 2% - so the cuts are many and small, each
timed alone on `286-victory` (`rdgbench --renderd`, 1,200 frames of play,
Super Mario Land / Kirby's Dream Land ticks) and every one checked FRAME
FOR FRAME against the composer before it: the bare harness sums every
picture it composes (`FRAME_SUM` on its DONE line), and the kept set reads
the same sums as before on both games and Dr. Mario in all four interlace
modes over 3,000 frames. `gbdamage` cannot see a defect here, its
reference sharing `ppu_line` and the sprites with what it checks.

- **Kept**: `ppu_log` is called only from the line its next entry takes
  effect on (`PPU_LOGL`), not from every line to find nothing due; the
  Off-mode whole frame has its own loop (`RDG_DCHG` one `rep stosw`, the
  count added once); `ppu_dput` copies from the first word that differs,
  where `repe cmpsw` left it. 410 / 859 -> 395 / 840.
- **Kept**: seven lines of eight reuse the last line's 21 tile addresses
  and take the next tile row, when no register was written since
  (`PPU_RCH`, set by `ppu_log` and each frame's start). -> 390 / 834.
- **Kept**: with BGP the identity the sprites draw straight into the
  background's planes (`PPU_BL`/`PPU_BH`) instead of a copy of them - exact,
  since a sprite changes only pixels it claims and a later one reads the
  background only where nothing is claimed. -> 385 / 825.
- **Kept**: the line lists are MOVED, not rebuilt (`ppu_oamv`): a sprite
  whose Y moved leaves the rows it no longer covers and joins those it now
  does, in OAM order; past `PPU_OAMFEW` = 8 movers a frame it rebuilds as
  before. -> 381 / 817.
- **Kept**: each map row's 21 tile addresses are kept from frame to frame
  in the damage memory with the 21 map bytes they were built from
  (`ppu_blist`, `RDG_DLC`), and copied while those bytes and the tile mode
  match. Kept by CONTENT rather than by the VRAM marks (378 / 807): it rests
  on no write tracking, and survives a long scroll in which the scan never
  runs. `RDG_DMG_SIZE` 0x3314 -> 0x3B96, the package's claim 13KB -> 15KB.
  -> 375 / 811.
- **Kept**: the line lists are built 8 tall when every sprite is 8x8 for
  the whole frame (`ppu_oamh`), 16 otherwise, and rebuilt when that
  changes. -> 370 / 811. And the tile-mode check skipped while `PPU_RCH` is
  clear. -> **368 / 808**: **-10.0% / -5.8%**, Super Mario Land composing
  every frame at 100.6% of real time where it was 111.8%; `--vga` 444 / 935
  -> 403 / 884. About 1KB of code.
- **Refused**: a one-tile line signed per line and not composed when its
  buffer already holds it. Super Mario Land gained 3-5 ticks and Kirby's
  Dream Land lost 7-11 in four placements: ~20 of the ~56 sky lines
  qualify, and the bookkeeping is every line's.
- **Refused**: `ppu_fshift` touched at all - a table per phase, a string
  shift and interleaved planes all cost more per byte than its 10
  instructions a tile, which are this design's floor.
- **The bare harness moved** to make room (it was 20 bytes from its core):
  the boot sector is copied to 2000:7C00, the host loads at 0760h with its
  own stack at 0:7600, the frame and profile buffers at 05A0h.
- **Kept** (the scan's half): the shadows kept current while it scrolls,
  OAM's taken whole there (13.2), and `ppu_dlines` one clipped `rep stosb`
  where it tested 16 rows. 369 / 810 -> 370 / 765: Kirby's Dream Land
  **-5.6%**, its still frames comparing one frame's writes where they
  compared a whole scroll's. Then the marked blocks LISTED by the core as
  it marks them (`vr_new`, 13.2.2), so the scan visits what was written
  rather than sweeping 512 bytes - every frame now runs it. -> **367 /
  762**. The list's tail takes a slot of the core, which the recompiler's
  cache gives up (256 bytes).


## 14. The package

`REALDMG.O88` (`apps/realdmg/realdmg.asm`, `OS88_STACK_384`) is a window of
160x144 content that runs one game.

- **Its parts** (SPEC.md 20.12): 0 and 1 are the core's two 32KB halves, LZ4
  on the disk - one part is at most 65,024 bytes, and two adjacent rows are
  one segment in the carve - and 2 is the view, a zeroed 64KB carve with no
  file bytes. `op_load` claims all three at launch and refuses there, in its
  own toast, on a heap without 128KB.
- **A game** opens from Machine > Load Game or as a document: the `.GB` association
  launches the package with it. `rw_load` asks the file its size with the
  size probe (a read with no room answers the KB it needs, unpacked, with a
  compressed file's slack), claims that, reads, and then checks the bytes
  that came in: **whole 16KB banks, 32KB to 256KB**, refused in a toast
  otherwise. One game a window: another opens in another.
- **The worker** owns the machine. It counts BIOS ticks into frames owed, 840
  of 256 a tick (59.7275 Hz against 18.2065), at most eight behind, and
  reads the clock again after every frame - so it sleeps only when nothing
  is owed. It runs the core a frame at a time in budgets of 2,000 M-cycles,
  700 on an 8088 (`OSAPI_CPU_INFO`), where 2,000 is ~20 ms with interrupts
  off, and 4,000 while the recompiler is on, where 2,000 are ~2 ms on a 286
  (each run is an `rdg_sched` and an event exit, and halving them took
  Kirby's Dream Land from 85.9% to 90.5% of real time on `286-victory`;
  8,000 read 91.8% and is a mouse byte nearer the edge); and composes and blits a frame when it has caught up or every three
  ticks whatever happens, so a machine that is behind still shows ~6 frames
  a second. A worker that has caught up and would only sleep draws a frame
  it has not drawn yet whatever the spacing of section 15.2 says - that
  half of the machine is for a game with frames to run, and the third
  field run's Super Mario Land interlaced slept ~190 ms a second at 20
  pictures. F1 puts the game's speed and the frames drawn a second over the
  picture. F2 is Machine > Interlace's next setting and F3 CPU >
  Recompiler's, each on its press and in the window and full screen alike
  (`rw_keys`, between two core runs - where the recompiler may change); the
  setting it changed to is said (`rw_note`): a toast on the desktop, and in
  full screen the speed line's row for ~2.5 s, where a toast would wait for
  the desktop. F3 from On to Static first does what the menu does (16.8,
  16.2.1: `rw_stprep` - the profile on and read, the cache's second segment
  if it names more than one holds), which is file and heap work for the UI
  task: in full screen, which IS the UI task, at once; in the window the
  worker wakes it (`[rw_f3q]`, `rw_onwake`) and the switch comes back
  through `rw_jitq`, said all the same (`[rw_jitqn]`). The joypad is the held keys - as shipped arrows, X = A,
  Z = B, Enter = Start, Space = Select, and whatever Machine > Controls...
  has made them since (section 14.2) - and nothing while another window
  has the keyboard. Two things a keyboard does that a pad cannot are
  settled before a frame sees them (`rw_keys`). **Opposite directions**:
  a pad cannot hold Left and Right together and a keyboard can, and a
  player who rolls from one to the other holds both for a moment - Super
  Mario Land given both ran on to the right, which the fourth field run
  reported as reversals the game missed. Of a pair held together the one
  pressed LATER wins, and releasing it returns to the other. **A tap
  between two looks**: the keys are a level read once a frame, so a press
  and release inside a draw was never seen; a press TYPED since the last
  look (the BIOS's buffer, which the bracket drains and the window is
  handed) counts for one frame, but only for a button that was up at the
  last look - a held key's typematic repeat, taken after its release,
  would otherwise be a press nobody made. Reset is the worker's to do (`rdg_init` again); Pause
  stops the clock.
- **The writers** both start from `rdg_renderd`'s changed lines (section
  13.2), and blit only those, a band for each run of them. A frame that
  changed nothing draws nothing.
  - On a colour display, `OSAPI_GFX_BLITP` with the clip walk, of the four
    planes `rw_planes` makes from those lines (NOT bit 1 three times, then
    NOT bit 0 - the desktop's 15, 7, 8 and 0 for shades 0-3). Damage took Super Mario Land's window
    on 86Box's 286 from 7 frames drawn a second to 28.
  - On a 1bpp display, or where BLITP refuses, `OSAPI_GFX_BLIT1` through a
    2x2 ordered dither: shade 0 lit, 1 a quarter dark, 2 a checkerboard, 3
    dark.
  - F1's line over the picture starts the next draw from every line, as it
    covers or uncovers pixels the game did not change. The damage memory is claimed
  with the game (`rw_dmgclaim`); a buffer that held the other format, a
  reset and full screen all start the next draw from every line. The buffer remembers which
  it holds, and a paint on the other kind of display (the window moved
  between two) is white until the worker's next frame. A refusal dithers
  until the next paint asks BLITP again.
- **Alt+Enter** is Machine > Full Screen (SPEC.md 11.2.1.1, `apps/os88alt.inc`):
  the window's W_ONKEY takes the kernel's synthesised `KEY_ALTENTER`, and the
  bracket's loop polls `os88alt_edge` to leave, seeded so the press that got
  it there does not take it out again. While Alt is held the joypad reads
  nothing, so the Enter of the chord is not the game's Start.
- **Its region is movable**, its worker restartable at the top of its loop;
  `rw_reloc` re-points the six state-block vectors that name its code
  segment. The core, view and ROM claims are pinned.
- **A bad opcode** stops the machine and the window says so.

### 14.1 The log

**Machine > Log keeps a record a second and writes them as text beside
REALDMG.O88**, when the log is turned off or the window closes
(`apps/realdmg/rwlog.inc`). It is the field run's instrument: the F1 line
says how a game runs this second, and the log says how it ran for minutes,
on a machine with no debugger.

- **Each log takes a name of its own**: the game's first six letters, two
  digits and `.LOG`, the first such name not already in the folder
  (`SUPER-00.LOG`, `SUPER-01.LOG`; `rw_lname` asks with a zero-length read).
  The folder is the program's own, kept at entry (`rw_pkgdir`). These two
  are the second field run's asks: no folder to make by hand, and no files
  to rename between runs.
- **The head says what ran on what**, in two lines:
  - the game's name, its ROM size, its MBC and its RAM banks, and whether
    it has a battery or a clock;
  - the CPU tier, the display's bits per pixel, which full-screen writer
    the display offers, whether a recompiler cache was claimed, the heap's
    largest run as the load found it, and - for a cartridge in the bank
    cache (section 10.3) - its slots and the banks fetched so far.

- **The records live in a 4 KB claim** taken when Machine > Log is
  turned on (`RW_LOGRKB`) and given back once they are written, so a
  session that never logs pays nothing for them; they were 4 KB of the
  package's bss. The worker makes each second's record in the package
  (`rw_lrec`) and copies it in with interrupts off; the UI task clears
  `[rw_logseg]` before it frees the claim, so a copy either lands whole
  or is not made. A claim the heap cannot give refuses the log with a
  toast.
- **The text is claimed at the write for the lines there are**: 176
  bytes a line and 640 for the head (`RW_LLMAX`, `RW_LHMAX`), so 30
  seconds is 5 KB and two minutes 19 KB; with no room for that, the latest
  half, and so on down to one line, and only then `Log: no room to
  write`. Every answer stays up ~8 s (`rw_ltoast`), and a refused write
  says why in the kernel's words (`Log: ` and `OSAPI_FERR_TEXT`): two of
  the refusals were longer than the 24 characters a toast shows, and all
  of them came and went in ~3 s at a moment nobody watches the bar.
- **F12 writes it NOW** (`rw_logflush`) and begins a new log in a fresh
  claim, so a field run's log is not hostage to the close, where the
  toast that says how the write went dies with the program. It is seen
  HELD (`OSAPI_KEY_DOWN`, `rw_fedge`), because an AT BIOS's `int 16h`
  function 00h drops F11 and F12; the write is the UI task's - in the
  window it is woken (`[rw_lflq]`, `rw_onwake`), and in full screen it
  waits for the bracket to end, where the toast can be read. It was 24 KB whatever was recorded, which a cartridge in the bank
  cache could not have: the plan leaves 24 KB of heap (section 10.3), the
  records take 4 KB of it, and the fourth field run's logs were refused
  with a toast nobody saw, after the picture had gone.
- **A record** (32 bytes, the latest 128 kept, about two minutes) holds
  the seconds since the log began, the game's speed and frames run, the
  frames drawn, the lines composed and the lines changed (section 13.2),
  the milliseconds spent drawing and the longest draw, the most frames
  owed (at most 31), and W or F (window or full screen) with J when the
  recompiler ran and I when Machine > Interlace was on.
- **Where a second went**, which the third field run asked for: of the
  drawing's milliseconds, those comparing VRAM and OAM (**scan**,
  `ppu_dscan`, timed through `rdg_dhook`) and those composing lines
  (**cmp**), the rest being the writer's; and the milliseconds running the
  Game Boy (**cpu**, `rw_frame` to its yield). Then four counters: the
  core's event entries, the blocks the recompiler translated, its cache
  flushes and the banks the bank cache fetched (section 10.3). A machine
  whose cpu and draw add up to about 1,000 is the machine's limit; one
  that does not is waiting.
- **And what the user did in that second**, by name: RESET, PAUSE, GO
  (unpaused), FS+ and FS- (into and out of full screen), JIT+ and JIT-
  (CPU > Recompiler), F1, and REC+, REC-, PLAY+ and PLAY- - a replay
  recorded and played (10.5), which is how two logs of one replay are
  lined up second for second. The user's byte is a word now (`RL_EVT`,
  the replays' bits its high byte, `rw_logevt2`), the record's spare byte
  17 being where it grew, and a line's text 176 bytes (`RW_LLMAX`).
- **The clock** is the 8253's counter 0 extended by the BIOS tick
  (`apps/os88pit.inc`, 838 ns a count). Inside a rate bracket the counter
  runs at the bracket's divisor, so there it is the hook's period count and
  the counter within the period (`rw_clk`).
- **Who does what.** The worker and the bracket record, in `rw_stat`, once
  a second. Only the UI task writes (SPEC.md 20.6 rule 7). Nothing
  recorded, nothing written.

`tests/gblog.py` takes two logs over the demo, one through the menu and
one at the close, with a pause inside the first. It reads both on the
host: the names, the head, PAUSE and GO, and the speed.

**A second at a time the speed jitters**: it read 89-111% here across a run
that averaged 101.1%. That is the field run's "90 to 110". The frames are
run in bursts, a tick's worth or a draw's worth at a time, and a burst
straddles a second's edge, so one second is credited with frames another
earned. It is the F1 line's sampling, not the game's pace.

### 14.2 Machine > Controls...

Which key is which button, chosen by the player (`apps/realdmg/rwkeys.inc`).
A window of its own, **"RealDMG Controls"**: a low-detail Game Boy - its body
on a grey mat with the bottom-right corner it rounds, a screen, the D-pad as
four boxes in a cross, B and A, Select and Start - each button a box naming
the key it has. The window is 234 x 154 with its frame: a CGA's desktop band
is 155 rows, and the window fits it whole.

- **Click a button** and its box goes white on black and the screen says
  *Press a key for* and its name; **the next key typed is that button's**.
  A key another button had changes places with this one's, so two buttons
  never share a key. A click anywhere else, or Esc, stops the wait with
  nothing changed; with nothing waiting, Esc puts the window away.
- **Refused, and said so on the screen**: Esc, Alt (Alt+Enter is full
  screen, and while Alt is held the joypad reads nothing), F1-F4 - the
  speed line and Machine > Interlace, Recompiler and Speed's next settings
  - the save states' F5, F8 and 1-9 (10.4), the replays' F6 and F7
  (10.5), and F12, the log written now
  (section 14.1's Machine > Log), documented shortcuts every one. A REALDMG.CFG whose map holds one of
  them is read with the keys as they shipped (`rw_cfgkeys`). The wait
  goes on after a refusal. A key with no make code of its own is refused
  too: SysRq, and the codes 54h-56h a shifted F-key gives. The keypad's
  Enter is Enter and F11/F12 are their make codes (57h, 58h), because the
  map is asked of `OSAPI_KEY_DOWN`, which knows only make codes. Shift, Ctrl
  and Caps alone cannot be given: they never reach a W_ONKEY.
- **Default** puts the keys back as they shipped.
- **Kept** in REALDMG.CFG, version 3: the eight make codes at +136, in
  S_JOY's order, written at every change (`rw_cfgsave`). A version 2 file
  is read with the shipped keys; a version 3 file whose keys include one
  Controls could not have given is read with the shipped keys too
  (`rw_cfgkeys`).
- **The map is `rw_keymap`**, the bytes the worker's `rw_keys` asks
  `OSAPI_KEY_DOWN` about every look and `rw_klat` matches a typed key
  against - so a change is the game's at its next look, and no copy of the
  map exists to go stale. While the Controls window is on top the game reads
  no keys at all (`rw_keys` asks for its own window first), so typing a
  key there never presses the button it is becoming.
- **A package's second window** is not bound to its instance (SPEC.md
  42.1): its close box hides it, so it is made once, shown again by the
  menu, and destroyed with the instance (`wm_destroy_seg`). It paints every
  pixel of its own (`OSAPI_WM_OWNBG`), the mat included. Each box is one
  opaque `OSAPI_FONT_RUN` of its name padded to its cells, between two rows
  of its ground, so a change rewrites the box without blanking it first.
- **The About card** says *Keys: Machine > Controls...* where it listed
  the shipped keys, which would have gone stale at the first change.

**What it cost**: the package's image and bss were 63,430 bytes of
APP_MAX_SIZE's 65,024, and the window is ~1.6KB - over by ~50. The 768
bytes came from section 15's speed-line glyphs, which are read in place now
rather than copied. 64,470 with it.

**And then the save states (10.4) met it at a merge, 614 bytes over.** What
made the room was the window's second frame buffer, `RW_PBUF` - 11,520
bytes, the frame as the screen takes it (the dither's band or BLITP's four
planes) - which is a claim of its own now, `[rw_pseg]`, made in the entry
proc beside the core and the view: the region is that much smaller as the
claim is made, so the machine's free memory comes out the same. Image and
bss are 54,142 of 65,024 with both features, 10,882 to spare.

**And then the replays (10.5)**: the same move again, made on the branch
where the canvas had spent the room (`gameboy-static`) and kept here, where
it costs nothing either way, so both builds lay memory out alike - for the
other frame: rdg_render's own, the 5,760 bytes of two shade planes,
is `[rw_fseg]` now - the same claim as `[rw_pseg]`, 17KB, from its
11,520th byte. Its readers take DS from it (`rw_dither`, `rw_planes`, and
`rw_dchg` reads the changed lines through CS so either may call it); the
Hercules and CGA writers read their own table beside the line, so each
line is copied into `rw_lbuf` first, 40 bytes. 53,096 of 65,024 with
replays.

`rw_render` hands `rdg_renderd` its output in ES and copies the changed
lines into `rw_chg` through ES as well, so ES is taken from DS again for
that copy now that the frame is not in our segment.

**It shipped once taking the UI task with it**: `jmp OSAPI_WM_SHOW` as a
tail call assembles - every `OSAPI_*` is a far address - and the cell's
`retf` returned through the near return address and the word under it. The
window opened and painted, and then no click or key reached any window
again, the picture perfect. `tests/unit/t_asmrules.py`'s rule 6 refuses a
jump to an API cell in apps/ and drivers/ now.

`tests/gbkeys.py` is the gate, on VGA and on a CGA: the waiting box lit, a
key taken, a reserved one refused, two buttons changing places, the GAME
reading the new map out of S_JOY (Q held is B, Z is A, X is nothing), the
file off the floppy as the guest left it, and Default.

### 14.3 The menus

Three on the bar, none over 11 items - a pull-down taller than that runs
off CGA's 200 lines, and the kernel does not clamp one (`menu_pull`):
- **Machine:** Load Game..., Reset, Pause, Full Screen (Alt+Enter) | Sound
  (F9), Show Stats (F1), Interlace (F2), Log (F12) | Controls...
- **CPU:** Recompiler, Static Recompiler, Interpreter (F3 steps through
  them, 16.7) | Profile for Static, Cache: 128K | Speed 100%, 90%, 75%, 50%
  (F4 steps through them).
- **Save State:** Slots 1-6 (keys 1-9 choose any of nine; a 14-item menu
  would not fit) | Save State (F5), Load State (F8), Record Replay (F6),
  Play Replay (F7) - each of the last two saying Stop while it runs.

**Load Game over a running game swaps it** (`rw_unload`, before
`rw_load`): the old one is put away as the close puts it away - the battery
save, the profile, the log, the FM chip - and its own memory given back
(the ROM's claim, or the bank cache's slots and XMS store; its RAM; its
profile; the cache's second segment unless Cache: 128K keeps it), with the
worker kept out of the machine meanwhile; the recompiler's cache and the
damage memory stay for the next game. It waits for a replay to end, as
Reset and Load State do. A load that then fails leaves no game. It was a
refusal ("one game each") until the menus' rework.

**An item is `RW_MI`'s**: `MENU_DIS`, a MARK byte, the text and its key
right-aligned to `RW_MW` (= `MENU_MAXCH`, 24) glyphs, the build failing on
one that is too long. The list names `label + 1` for the item and `label`
to grey it, so one string is both. The kernel's menu has no check mark and
the font is printable ASCII (SPEC.md 6), so the mark is `*` on the setting
chosen and a space on the others, written by `rw_mgrey` (`rw_mark`,
`rw_mitem`). A setting with more values than a mark can say - Sound,
Interlace - names its value instead. A separator is a greyed line of
dashes, which the kernel's keyboard walk steps over. `rw_note` says an item
without its mark or key (a toast, or the speed line in full screen): F2,
F3, F4 and F9 say what they changed to.

### 14.3.1 The window says it is slow

A game that starts (Load Game, or a file opened with the package) shows
three lines in the window for `RW_NTCT` = 27 ticks, 1.5 seconds, before its
first frame: **Windowed is slow: / Alt+Enter for / Full Screen**. The
window composes a frame and hands it to the kernel to blit, where full
screen writes the card itself (section 15), so on every machine the window
is the slow way to play and nothing else said so.
- `[rw_ntc]` is the notice; `rw_paint` draws it in place of the picture
  while it is set, and the worker's `rw_ntcwait` holds the game's first
  frame back until the time is up, then clears it and marks every line
  changed (`rw_dmgfull`) so the picture covers the notice whole. A full
  screen asked meanwhile ends the wait at once - it is what the notice asks
  for - and so does a Load Game over it (`rw_unload` clears the byte).
- It costs a game that is played full screen nothing but the wait it skips,
  and a restarted worker (SPEC.md 66.6.2) finds the byte clear and does
  not wait again.

## 15. Full screen

Machine > Full Screen runs the machine **on the UI task inside an
`OSAPI_FSX_RUN` bracket** (SPEC.md 53), in `apps/realdmg/rwfsx.inc`. The
mode depends on the display (`rw_fspick`, `OSAPI_FSX_CAPS`, asked at every
paint): VGA's mode 0Dh, else a Hercules page, else CGA's 320x200 in four
colours (section 15.1). It is greyed without a game and on a display with
none of the three. Esc leaves; it is looked for between frames and after
every draw, as a turn of frames on an 8088 is longer than a key press.

- **The worker keeps out of the machine.** It is frozen in the bracket, so it
  must not be frozen in the core, or in `rdg_render`, whose working memory is
  the view's. It raises `[rw_incore]` at its loop's top and drops it at
  `.sleep` or when its render is done, before it asks for the gfx lock; it
  backs off at the top while `[rw_fsq]` is set. `rw_fsenter` sets `[rw_fsq]`
  and yields until `[rw_incore]` is clear. The worker raises its byte BEFORE
  it reads ours, so once ours is set and its is seen clear, every later entry
  of its sees ours.
- **The screen** is mode 0Dh, 320x200 planar, with its DISPLAY cut to the
  picture (`rw_fscrtc`): 160 pixels by 154 rows from VRAM offset 0 - the
  picture at 1x, a gap and the speed line's row - centred by moving the
  blanking and the retraces, with the total, the sync widths and every
  frequency mode 0Dh's own. The border around it is the card's OVERSCAN
  (AR11) in DAC entry 4's grey. It is what lets the picture move in VRAM
  under a fixed window (SPEC.md 53.7: a bracket's own mode is the app's to
  program, and the exit's mode set puts it all back); an emulator's frame
  buffer cannot show the centring, which is the sync's, so a real monitor
  is the check. `rdg_renderd`'s two shade planes go to VGA planes 0 and 1 as
  they are, each line as `ppu_dput` finds it changed (`[rdg_dvga]`, the
  card's segment, `ppu_dvga`): the plane the map mask already holds first,
  so a line turns it once, and no second pass over the buffer - Super Mario
  Land's frame on the Victory twin 25.7 -> 24.7 ms with the sprite fetch
  below. It already compared them, so the RAM shadow this writer once kept
  was a second comparison of the same bytes. Plane 2 is set under the gap and the speed line's row,
  entry 4 like the overscan, and F1's speed line is drawn there in the
  kernel's own glyphs, read where `OSAPI_FONT_GLYPHS` says they are (a far
  pointer taken before the mode: a memory read is not a drawing slot), entry
  7. They were a 768-byte copy in the package's bss until section 14.2 needed
  the room (they stay read in place; 14.2 says where the room came from
  since). Shades 0-3 are DAC 0-3, greys. The DAC is
  ours in there; the restore's BIOS mode set puts the desktop's back.
- **The clock** is `FSXF_RATE` at the Game Boy's own 59.7275 Hz (PIT divisor
  19,977): the hook counts frames owed and does nothing else. The loop runs
  the machine to the count, at most eight behind, in budgets of 6,000
  M-cycles (1,400 on an 8088) - with no mouse to serve only IRQ0 bounds them,
  and a 286 spends ~7 ms on 6,000 busy ones against a 16.7 ms period. It
  draws a frame when it has caught up, or after four undrawn; with nothing
  owed it waits for the next period (`FSXW_FRAME`). It empties the BIOS key
  buffer every pass, since a full one beeps. `kern_small`, which refuses the
  rate and has no VGA, is retried without it and paced by the tick.
- **Coming back**: the window's buffer was the bracket's shadow, so it is
  marked empty and the restore's repaint is white until the worker's next
  frame; the worker's clock restarts from the moment the bracket ended.

### 15.1 Hercules and CGA

**On a Hercules the page is 720x348, with the picture at 2x2 from (200, 26):
320x288.** Each Game Boy pixel is a 2x2 dither of its shade: four lit, three,
a checkerboard, or none. **On a CGA the mode is 320x200 in four colours, with
the picture at 2x1 from (0, 28): 320x144.** The shades are white, cyan,
magenta and black, the BIOS's palette 1.

- **Only the changed lines are written**, straight to the page:
  `rdg_renderd`'s lines (section 13.2), so a still frame writes nothing.
- **A table does the work** (`rw_fstab`). A Game Boy byte (eight pixels,
  two shade bits each) is two nibble indices, `(bit 0's four pixels << 4) |
  bit 1's`. Each index is one entry: two bytes on a Hercules (the dither's
  top and bottom rows of those four pixels, doubled), and one word on a CGA
  (the four pixels doubled in four colours).
- **The page is cleared at entry**, and F1's speed line goes under the
  picture in the glyphs' own bits.

`tests/gbwindow.py` reads both out of the card's memory, against the same
dmg-acid2 reference as the window, on MartyPC's 5150 Hercules and CGA.

### 15.2 When a frame is drawn

**The game keeps its speed; the picture takes what is left.** The draw clock
is `rw_clk`'s, 838 ns a count, and each draw measures itself (`rw_timed`).
A draw is due (`rw_drawdue`) in one of two cases:
- **The game has caught up**: once as long as the last draw took has passed
  since it ended. So drawing takes at most half the machine.
- **The game is behind**: another such length later still. So drawing takes
  a third of the machine and the rest goes to catching up.
- **The machine would otherwise sleep** - caught up, nothing owed, and a
  frame run since the last draw: it is drawn now, whatever the first case
  says (`[rw_undrawn]`; section 14's worker and `rw_fsmain` alike). The
  first two cases ration the machine between the picture and a game with
  frames to run; idle time is nobody's.
- **On a 286** a draw is milliseconds, and the rule comes down to "every
  frame it has caught up".
- **On an 8088** a draw is a large part of a second, and the picture rate
  falls while the game holds its speed.

**The first field run is why "behind" is a share and not a period.** The
first form drew every three ticks whenever the game was behind at all. On
the Victory, Super Mario Land slips under 100% for a moment with an enemy
on screen as it scrolls, and that dropped it from ~30 frames drawn to 4.
The clock was a tick then, 55 ms, which could not tell a 286's draw from
nothing.

**A draw that a clock change ran across counts for nothing.** The window's
worker can sit inside a draw, waiting for the gfx lock, for the whole of a
full-screen session. It then measured a 34-second "draw", and put its next
draw 34 seconds out: the second field run's log showed a window that drew
nothing after leaving full screen. `rw_drawnow` counts the clock changes
(`[rw_dgen]`), and `rw_timed` keeps nothing from a draw that saw one.

**Catching up has a limit.** A machine that falls behind catches up at most
`RW_BEHIND` (30) frames, half a second. That is longer than an 8088's draw,
whose frames would otherwise be lost. A fresh start, and the change of clock
going into or out of full screen, make the next draw due at once
(`rw_drawnow`).

The window and full screen keep the same rules. In full screen, Esc and
Alt+Enter are also looked for after each draw.

**CPU > Speed: 100%, 90%, 75%, 50% - a rate, not a floor.** The
game runs to a clock scaled by 256ths (`rw_spdtab`: 256, 230, 192, 128;
60% went with the menus' rework, a file's 60% reading as 50%): the window's frames a BIOS tick are `RW_PERTICK` x that / 256
(`rw_pertick`), and the full screen's rate periods are multiplied by it
with the 256ths left over kept for the next (`rw_fsowed`, `rw_fsfrac`).
100% is exactly the clock before it. So 75% is three Game Boy frames to
every four periods, steady, and everything the game no longer uses goes to
the picture by the rules above: Kirby's Dream Land's game alone is 92% of
the Victory at 100% (section 13.3), so it cannot hold its speed and its
picture both, and at 75% it holds 75% and draws with the rest. F4 is
Speed's next, in the window and full screen alike, and says it
(`rw_note`).
- **Machine > Sound: On, At 100% only, Off.** The speaker plays the game's
  own note every frame (section 17), so a game slowed plays its tune at the
  same pitch and a slower tempo; At 100% only is silent below 100%.
- **Both are the game's**, remembered in REALDMG.CFG version 2: a count at
  +4 and, from +8, `RW_CFGGN` (32) entries of four bytes, the latest first
  - the header's 014Dh byte and 014Eh word (as a profile's name takes
  them), then Speed in bits 0-2 and Sound in 3-4 (`rw_cfgrec`). A game
  opened (`rw_spdgame`) takes its entry; one with none keeps what was
  chosen before it, kept for it at its first change. The menu writes the
  file at once; F4's change is the UI task's to write (`[rw_spdq]`,
  `rw_onwake`), the worker that sees it not being allowed a file.
- `tests/gbspeed.py` is the gate: CLOCK0.GB on MartyPC at ~100%, at 75%
  after two picks, and at 75% again opened anew. Without the clock's
  scaling it reads 101%; without `rw_spdgame` the reopened game 103%.

### 15.3 Hardware scroll: built, measured and refused

VGA full screen was written a second time (commit a2af224e) to follow the
game's scroll with the CRTC: the picture at a movable origin, the start
address and the pel panning moved with line 100's SCY and SCX, plain lines
written only where they scrolled into view, unchanged lines copied by the
card from pre-shifted copies (write mode 1), the speed line under the line
compare. It drew the right picture - `rdg_render`'s, read back through the
panning, on 500 checked frames each of Super Mario Land and Kirby's Dream
Land - and it was taken out, for two reasons:

- **It was not in step with the card.** A draw is longer than a refresh,
  and the lines it composes (sprites, the status bar) are written for the
  NEW origin while the card still shows the old one: until the start
  address is latched at the next retrace they sit displaced by the scroll,
  so the status bar and Mario jittered by the scroll's step every draw - on
  86Box and on the Victory alike. Writing the registers first only turns
  the jump the other way. The cure is two pages and a flip, and the card
  latches the start address at the retrace and the panning at once, so a
  flip that changes both must wait for the retrace: half a refresh, ~7 ms,
  on every draw.
- **It bought less than that wait costs.** On the Victory twin, drawing
  every frame of the platformer script (`tests/rdgcore/rdgbench.py --vga`
  times the writer that stayed), a frame:

  | | CPU only | lines copied | hardware scroll |
  |---|---|---|---|
  | Super Mario Land | 6.09 ms | 25.72 ms | 24.90 ms |
  | Kirby's Dream Land (128K cache) | 15.38 ms | 53.64 ms | 51.81 ms |

  On that 286 a line's bookkeeping and the bytes that scrolled in cost about
  what composing the line costs, the VRAM scan the copying writer skips
  while it scrolls comes back, and the lines with sprites are composed
  either way.

What it left: Kirby's Dream Land's figures above are of the game in PLAY.
Every earlier one - 7.37 ms CPU only, 12.18 drawn - was of the game
PAUSED, by the third Start of gbprof's platformer script; the harness now
leaves that Start out for it and flies it through the level (`b_joy`,
`JOY_NO3RD` and `JOY_FLAP`). And the harness refuses to assemble when it
runs into the core it loads, which the experiment did, silently.

## 16. The recompiler

On a 286 RealDMG translates the Game Boy's ROM code into 8086 code, a block
at a time, and runs that instead of interpreting it (`apps/realdmg/rdgjit.inc`,
`rdgtmpl.py`; `tests/rdgcore/rdghost.asm`'s `b_jit` and the package's CPU >
Recompiler turn it off). What it saves is the interpreter's fixed cost: the
three-instruction dispatch, which flushes a 286's prefetch queue every time,
and the operand fetches through SI.

### 16.1 Templates

A translated instruction is its interpreter handler's own body. `rdggen.py`'s
`template_lines()` writes each handler with the dispatch taken off, `[si]`
operands as `IMM8`/`IMM16`, `xor dl,dl` dropped (DL = 0 is the dispatch's
invariant, which `tr_int` restores) and external jumps `near`; `rdgtmpl.py`
assembles every one three times - at its origin, 2000h further on, and with
other operand values - and finds by comparing the bytes where the operands
are, which words are references to the template itself (`S_RET = .k`) and
which are relative jumps out of it to a core helper. **So translated code
computes exactly what the interpreter does**, and `gbcpu`, `gbacid` and the
harness's event counts hold it to that: Super Mario Land's 1,200 frames of
gbprof's script take the same events and skip the same M-cycles either way.

- Control transfers have no template: the translator writes their M-cycles,
  the event check the interpreter makes at a branch, and the way on (16.4).
  An opcode with neither - HALT, STOP, RETI, an invalid one - ends the block
  with SI = its PC and `tr_int`, the interpreter's dispatch.
- The read-modify-writes of `(HL)` - INC/DEC `(HL)` and the CB group on it
  but BIT - carry the read's and the write's slow paths both and are over
  80 bytes, so they are chosen by operand too (`rdggen.rmw_lines`): the
  address is checked once for what both paths take directly, 8000-FEFF and
  FF80-FFFE, and everything else - ROM, the MBC, I/O, IE - goes to
  `tr_int` at the instruction itself, the template's IMM16 being its PC and
  BP exact (it is not pure). They used to end the block, and the interpreter
  ran on from there to the next branch and `tr_enter`: Kirby's Dream Land's
  sound driver alone is a DEC (HL) per channel per frame. With them, the CPU
  alone on `286-victory` went from 9.06 to 8.19 ms a frame on Kirby's Dream
  Land and from 7.19 to 6.91 on Super Mario Land. `tools/gbprof/gbrmw.asm`
  (`gbcpu`) runs them on WRAM and on TMA and must see the two agree.
- A template over 80 bytes is the interpreter's: LD (nn),SP.
- `LD (C),A` and `LD A,(C)` have templates of their own (rdggen's JIT arm):
  C 80h and up is HRAM, one access through SS with SI = C, and only I/O and
  IE take the general path.
- A memory template keeps the slow path's stub BEFORE the fast access, so
  the common case takes one jump and none over the stub; the 286 here is
  bound by fetching code bytes, so a jump and its bytes are the cost. The
  stub is two instructions, `mov word [ss:S_RET],.k / jmp rd_sN` (or
  `wr_sN`), N the M-cycle of the instruction the access is in: the shared
  `rd_s1`-`rd_s3` answer ROMX at once and `wr_s1`-`wr_s4` hand the MBC on,
  and only for an I/O register (`rw_io`) is BP taken back by N around
  `io_rd`/`io_wr` and forward by N after (`S_ATN`), not restored, since an
  I/O write may reschedule it. That took eight bytes out of every memory
  access the cache holds - and the cache is what binds: Super Mario Land
  had come to flush it twice in play instead of once.
- `LDH (n)` and `LD (nn)` on an address the interpreter's own fast path takes
  directly - HRAM, WRAM, VRAM, bank 0 - are chosen by operand: one `MOV`
  through SS and the M-cycles (`rdggen.EXTRA`). A store to VRAM is not one
  any more (13.2.2). **A store to a sound register `io_wr` only stores** -
  FF10-FF3F but FF12, FF14, FF17, FF19 and FF26, which rdgsys keeps for the
  speaker (17) - is one too (`rt_snd`), where the template's took the slow
  path and `io_wr`'s compares, ~140 clocks on a 286. (It measured level on
  both games' benches: their drivers write the registers rdgsys keeps.)
- **`LD (nn),A` to a bank select** - 2000-3FFF on an MBC1 or MBC3,
  2000-2FFF on an MBC5 - is chosen by the cartridge (`rdggen.bank_lines`,
  records `RDGT_XBANK` + 0/1/2): the bank number is masked in the template and
  it goes straight to `mbc_set`, where the store took the slow path, `wr_s3`,
  `wr_sw` and `mbc_wr`'s type compares to get there. BX and `S_WVAL` are
  `mbc_wr`'s, so a bank the cache has not got still reaches rdgsys from
  `mbc_set`, and a set `S_BANKHI` still goes to `mbc_wr`. Its flags are the
  plain store's (4), so run from ROMX the block is left after it (16.5).
  Kirby's Dream Land switches about 95 times a frame: 252 to 247 ticks on the
  286 bench, the CPU alone with two segments; Super Mario Land 114 to 112.
  **Then `mbc_set`'s body went INLINE**: the bank table read, the count, DS
  moved with the bank, no `S_RET` round trip and no BX saved around it. The
  two rare cases - `S_BANKHI` set, a bank not in memory - hand the whole
  instruction to the interpreter at IMM16 - 3 (flags 16 make IMM16 the next
  PC; nothing else needs nn), which runs it the long way and counts the
  bank's switch a second time, harmless to the cache's eviction. 77 bytes,
  under `TMAX`; `rdgtmpl.py` now refuses an EXTRA record over it, which it
  used to drop - quietly sending that operand the slow way. Kirby's Dream
  Land 235 -> 230 ticks, Super Mario Land 112 -> 111.

### 16.1.1 Pure templates share their M-cycles

A template whose last three bytes are `sub bp,n`, that names BP nowhere
else, has no tail and jumps nowhere out of itself is **pure** (`rdgtmpl.py`,
flags bit 3; 398 of 445): nothing in it reads the time, so the translator
copies it without its `sub bp,n` and adds n to the block's pending count,
which it takes off BP in one `sub` (`rt_flushbp`) before the next template
that is not pure, a control transfer or the block's fall-through. Super
Mario Land, the CPU alone: 9.57 to 9.29 ms a frame.

**The pending count goes INTO the control transfer's own `sub`**: each arm
of a conditional takes it in its `sub bp,n` (`rt_subbp`, `rt_pend0` giving
the second arm its own), where a flush before the transfer made two subs
at every block end; a fall-through's flush stands in for its `or bp,bp`.
The idioms that read BP (`cnt_ff`, `cpy_ff`, `stat_ff`, `idle_yes`) and the
untemplated opcode's way to the interpreter take the flush first. Super
Mario Land 119 -> 115 ticks and Kirby's Dream Land (two segments) 266 ->
258, the CPU alone on `286-victory`.

**A template that is not pure may still defer its own sub** (flags bit
5, 88 of them: the memory accesses through (HL), (DE), (BC), the ALU on
(HL), the read-modify-writes): it needs BP exact only at its START, which
the flush before it makes - its slow paths take BP back and forth around
an I/O access by the access's own M-cycle (`S_ATN`), and its only other
`sub bp` is one straight before an exit to `tr_enter` or `tr_int`. So with
no guard after it (whose exit leaves with BP charged) its `sub bp,n` joins
the pending count. EI reads BP (`force_ev`) and is not one. Kirby's Dream
Land 258 -> 252 ticks, Super Mario Land 115 -> 114.

### 16.1.2 Dead flags

F lives in AH, and most of what writes it spends more on F than on its
result: `ADD HL,rr` is `add bl,cl / adc bh,ch` and five instructions keeping
Z, `INC E` a `sahf` and a `lahf` around its `inc dl`. When the instructions
after one in the same block overwrite every flag it writes before any of
them reads one, those flags are DEAD and the translator takes the opcode's
second record instead (`rdggen.nof_lines`, `rt_deadf`): `ADD HL,rr` as
`add bx,cx` (`di`, `bx`, `sp`), INC/DEC r as the bare `inc`/`dec`, INC/DEC D
as `add di,100h`. Kirby's Dream Land runs `ADD HL,DE / LD A,L / CP C` and
`ADD HL,BC` five times running in its hottest loops.

`rdggen.flag_masks` says what every opcode writes (all of it decided by the
instruction, nothing carried from F) and reads, four bits each in a 256-byte
table at the head of `rdgtmpl.bin` (`RDGT_FLG`); the CB group's are a rule in
`rt_deadf` (`rdggen.flag_masks_cb`, which `rdgtmpl.py` checks against it),
an instruction's length is its own record's, and the second records sit in
slots `rt_nofx` finds by formula - so the whole mechanism is 256 bytes of
table and ~200 of code. The look goes on past instructions that write some
of the live flags or none, and STOPS, calling F live, at:

- anything that reads a live flag - ADC and SBC, RLA, RRA, DAA, CCF, the
  CB group's RL and RR, PUSH AF, and every control transfer (whose target
  may read it);
- what could hand F on before it is overwritten: EI (an interrupt is taken
  after it) and any template whose record says it stores (flags bits 0, 1,
  2, 4: a store's guard may take an interrupt, whose handler may PUSH AF)
  or may leave the block (bit 6, `rdgtmpl.LEAVES`: to the interpreter, the
  event entry or rdgsys), and an opcode with no template at all;
- the block's end, or `RT_DLOOK` (6) instructions on.

The instruction that finishes overwriting may itself store or leave: every
template writes F before its exits, and one that leaves for the interpreter
(`rmw_lines`' I/O case) leaves it the whole instruction to do, with no event
between. A chain of them is safe for the same reason, each one's writes
covering the one before's. Kirby's Dream Land 247 -> 235 ticks, the CPU
alone with two segments; Super Mario Land 112, level.

The ALU on A was given second records too (register and byte forms, CP
excepted) and measured 235 -> 234: its flags are nearly always read, by the
JR after it or by the ALU after that. Not kept - it cost ~1KB of records and
took the bare harness (`tests/rdgcore/rdghost.asm`, 32KB under the core) to
within 56 bytes of its end.

Blargg's cpu_instrs cannot see this - it runs the instruction under test
from RAM, which is never translated, and it passed with the read check
removed. `tools/gbprof/gbflags.py` generates the gate instead: 160 seeded
sequences of register instructions weighted to the three the pass drops and
what it looks past, half in ROM0 and half in ROMX, each run 100 times so it
is translated, AF/BC/DE/HL folded after every pass into a sum. `gbcpu`'s
"dead flags" leg wants the sum with the recompiler equal to the interpreter's,
and its control (`RT_DEADBUG`: the read check removed) wants it to differ.

### 16.2 The cache

The code goes into a 64KB segment of its own - and, when that is full, into
the CORE's own fill (16.2.2) before a second segment or a flush. It holds copies of the core's
helper tails - the slots `rdggen.TAILS` uses - at their own offsets, so a
template reaches every helper with a near jump; ~58KB is left for code, in
the free ranges `rdgtmpl.py` lists. **The block table (16KB here; 64KB now, 16.3) has a segment of
its own** (`S_JITHSEG`), in the package the 16KB after the cache in one 80KB
claim and in the harness 8C00h. It was slots 00-3F of the cache, and the
cache had ~44KB for code: Kirby's Dream Land's working set is ~500 blocks
of ~96 bytes, so in the package it flushed every five seconds and ran no
faster recompiled than interpreted (61.7% of real time against 62%). With
the table out, 0 flushes in 30 seconds and **84.4%**; the bank cache it
shares the heap with went from eight slots to seven. A second segment
is 16.2.1. A helper
that leaves for rdgsys records its CS in `S_RETFAR` first, so rdgsys comes back
to the copy that called it, and every way out of the cache to the
interpreter is a far jump. When the cache is full everything is forgotten
and translation starts again. The package claims it on a 286 or later, after
the ROM, and the load's plan keeps 80KB for it - a cartridge that would not
leave it goes to the bank cache (section 10.3); without the memory there is
no recompiler.

### 16.2.1 The second segment

**A second 64 KB cache segment** (`S_JITSEGB`, rt_init's CX) takes the
code the first has no room for, before anything is flushed:
- **It is 64 KB or nothing.** A template reaches every helper tail by a
  near jump, and the tails sit in slots 40h to FDh, so a segment holds
  copies of all of them at their own offsets. A 32 KB one was built and
  ran into its missing tails (`BADOP`). Its code room is the first's,
  58,336 bytes.
- **The order:** `rt_room` at the end of the first segment's gaps moves on
  to the second's first gap (`S_JITSEG` = the one being written,
  `S_JITSW` counts the moves) and the block starts again there. A full
  second segment flushes both, back to the first.
- **A block's segment is in its table entry**, the word at +6 that was
  spare. `tr_go` takes the offset and the segment in one `LES` and jumps
  far, as it always has. `tr_count` is entered through `tr_cnt0`, which
  loads the table's segment again, since the `LES` replaced it.
- **Chaining across the two** is a far jump:
  - the translator's own direct chain (`rt_exit`, a target already
    translated) is made only within the segment being written; a target
    in the other goes through `tr_link`;
  - `tr_link` patches its site through `tr_ljmp`: a `jmp near` in the same
    segment, a `jmp far` in the other. An exit to bank 0 has 6 bytes and
    takes either. An exit into 4000-7FFF has **13** bytes (16.4), so the
    bank check and a far jump fit. A RET's inline cache entry is **19**
    bytes, for the same reason.
  - Before the exits grew, a cross-segment exit into a bank and a
    cross-segment return could not be chained and went through the lookup
    every time. That made Kirby's Dream Land SLOWER with the second
    segment than without it (the CPU alone on `286-victory`, 2,000 frames:
    7.64 against 7.41 ms a frame). Grown: 6.96 against 7.09 - 666 blocks
    translated and no flush, against 1,081 and a flush.
- **Measured cost to a game that fits one segment:** Super Mario Land 6.18
  ms a frame against 6.13 - one tick in 135, within the bench's noise.
- **Who claims it** is the package (14), by need: at the load, when the
  game's `NAME.RDP` names more blocks than the first segment and the core's
  holes hold (`RW_JIT2N`, 900; Kirby's Dream Land's first segment held 594,
  and with the core's holes its 765 translated with no flush, 16.2.2), BEFORE
  the ROM's plan, so a cartridge that then does not fit whole goes to the
  bank cache (10.3) rather than the code going without. The plan gives
  the second segment back first and plans again, and only then the
  recompiler. When Static is chosen in a session whose profile has grown
  past it, the package asks for it then, from what the heap has left.
  Without the memory there is one segment.
- **CPU > Cache: 128K** asks for it for every game, whatever its
  profile says (greyed on an 8088). It is a SETTING, kept in
  `SYSTEM\APPDATA\REALDMG.CFG` (`'RB'`, the version, bit 0 of the fourth
  byte; version 2 goes on with each game's Speed and Sound, section 15.2,
  and version 3 with Machine > Controls' keys, section 14.2)
  through `apps/os88data.inc` - SYSTEM\APPDATA on the package's OWN
  volume, so `make realdmgdisk`'s floppies carry the folder, and a copy of
  REALDMG.O88 put where there is none keeps nothing - and read at the start - a game opened
  from its icon is loaded before its menu can be touched. Chosen in a
  session, 128K takes the second segment from what the heap has left and
  the worker starts the recompiler again over both (`[rw_seg2q]`,
  `rw_jiton`); without the room, and going back to 64K, it is the next
  game's.
  - **Why it is a setting and not the rule:** the second segment is 64 KB
    the bank cache does not get. Kirby's Dream Land in the package on
    `286-victory` (`rdgwin.py --persec`, 40 s walking right): with one
    segment 94.3% of real time, 49-89% for its first five seconds and
    47-57% for three more when the cache flushed at 19 s; with two,
    98.3%, 79-92% while it warmed and 97-105% after, no flush - in 8 bank
    slots rather than 11, with 11 banks fetched in the span against 1. A
    game whose code fits one segment (Super Mario Land) gains nothing for
    the 64 KB.
- **The harness** (12.1) takes it at 7C00:0000 (`b_jit` bit 2) over a ROM
  of 304 KB or less, and can squeeze both segments to 1 KB of code (bit 4,
  `RT_SQUEEZE` builds only) so the test ROMs, which fit one segment many
  times over, fill both. `tests/rdgstatic.py`'s second half is that, with a
  negative control that makes `rt_exit` chain across the segments.

### 16.2.2 The core's holes: the second tier

**The core's 0CCh fill is the cache's second tier**, for no claim at all.
Dispatch is `jmp dx` onto opcode x 256 (section 4), so every half-slot owns
128 bytes whatever its handler takes: 17,241 bytes of code in 64KB, and the
rest fill. A cache segment has to hold copies of the helper tails at their
own offsets so that a template reaches every one by a near jump (16.2) -
and the core is the one segment that has them without a copy, so code
translated into its fill runs exactly as code in a cache segment does.
- **The tiers**, in `rt_room`'s order: the first cache segment (tier 0),
  the core's holes (tier 1), the second cache segment if there is one
  (tier 2, 16.2.1); then a flush, which forgets all three and starts again
  at tier 0. `[S_JITTIER]` is the one being written, `[S_JITGL]` and
  `[S_JITGN]` its gap list. Moving on counts in `[S_JITSW]`, as moving
  into the second segment always did.
- **The list** is `rdgtmpl.py`'s, a byte a half-slot - where its fill
  begins, or FFh for less than 48 bytes (`HMIN`: a gap that cannot take a
  request is a jump through it on every run of the block that crossed it).
  `rdggen.py` labels the end of each half-slot's code (`..@hole_N`), and
  `rdgtmpl.py` REFUSES to build when a listed byte is not the generator's
  fill: a byte of code listed there would be overwritten. 455 holes,
  46,992 bytes; `rt_gnext` walks the list, a hole's end being its
  half-slot's (the last capped at FFF0h, as the cache's list is).
- **A block crosses holes as it crosses a cache segment's gaps**: when the
  next instruction does not fit, `rt_room` writes a `jmp near` to the next
  hole and goes on there. The core's holes are 48-127 bytes where a cache
  segment's free ranges are runs of pages, so a block crosses more of them.
  Every request fits the larger holes (124 bytes usable: the longest
  template with its guards is 116, `RT_CTL` + 4 is 100), and a hole too small
  for one is jumped past.
- **Chaining** between the core and a cache segment is 16.2.1's far jump:
  `tr_ljmp` compares CS with the target's segment, and a block in the core
  runs with CS the core's.
- **The interpreter is untouched**: nothing is written into a half-slot's
  code, and nothing reads the fill. A flush leaves the old code in the holes
  - unreachable, as it is in a cache segment.
- **An 8088 never uses it** (no recompiler), and **the host may turn it
  off**: `[S_JITNOC]` = 1, the bare harness's `JF_NOCORE` and
  `rdgbench.py --nocore`, which is the A/B.
- **Static** preloads through the same `rt_room`, so its preload fills the
  tiers in the same order and stops at the last (16.8).
- **What it buys, MEASURED** (`rdgbench.py --box 286-victory --mhz 16
  --rom`, 2,200 frames of play, the CPU alone):

  | Kirby's Dream Land | ms a frame | blocks translated | flushes |
  |---|---|---|---|
  | one segment, no core tier (`--nocore`) | 14.95 | 4,033 | 6 |
  | **one segment and the core's holes** | **11.08** | **765** | **0** |
  | ...and the second segment | 11.08 | 765 | 0 (never reached) |
  | two segments, no core tier | 10.94 | 765 | 0 |

  The core's holes do what the second 64KB did, for 1% of its speed (the
  jumps between holes) and none of its memory. Super Mario Land, which fits
  one segment, is 4.74 ms either way. In the package on `286-victory`
  (`rdgwin.py --persec`, 40 s walking right) Kirby's Dream Land runs at
  100.0% of real time, 95-105% every second, with no flush.
- **The gate** is `tests/rdgstatic.py`: its squeezed rows (`RT_SQUEEZE`:
  each cache segment's first gap cut to 1KB, the core's first `RT_SQHOLES`
  half-slots) pass through all three tiers; one row runs cpu_instrs with one
  segment so that it moves into the core and flushes from there; and its
  NEGATIVE CONTROL lowers every hole's start by 8 bytes in the harness, so
  that translated code is written over the ends of op_00-op_07's handlers,
  and cpu_instrs must then fail.

### 16.2.3 Round robin: the oldest tier goes, not the cache

A full cache used to be FLUSHED (16.3): every block forgotten and
translated again from nothing, which takes a game to ~30% of real time for
seconds - Kirby's Dream Land in long play, Super Mario Land a level or two
on. With tiers there is a better unit than the cache: **the tier written
longest ago**. `rt_room`'s last gap steps to the NEXT tier in turn (0, the
core's holes, the second segment, 0 again - the ones there are), and one
already written since the last flush is RECYCLED: `rt_forget` clears every
table entry whose block is in that segment, and it is written from its
first gap again. What the game is playing now - the code translated last -
is in the tiers that stay.
- **Each tier carries its generation** in the word at its `FFF0h`, free in
  all three (`rt_tier` writes `[S_JITGNX]`, counted up at every tier
  started). It is what makes recycling free of an undo log.
- **A chain into an OLDER tier is logged, and undone when that tier goes.**
  `tr_lpatch` compares the target's generation with the site's: the same
  tier or a newer one needs nothing, since the site's tier is recycled
  first and the link goes with it. An older one is written to the LINK LOG
  - 124 entries of a site's offset and segment, at `RT_LOG` (FE00-FFEF of
  the first cache segment, which `rdgtmpl.py` takes out of the free ranges
  of both) - and `rt_unlink`, before `rt_forget`, walks it: an entry whose
  site is in the tier going is dropped, an exit that jumps into it gets its
  unlinked bytes back (`mov dx,SITE / jmp tr_lsite`, with the seven NOPs
  of one into 4000-7FFF), and a RET's inline cache entry that jumps into it
  gets PC FFFFh, which nothing returns to. A full log refuses the link: the
  exit then looks its block up each time, which costs a lookup and not a
  translation.
- **Why a log and not a rule**: refusing every link into an older tier,
  which needs no undo at all, was built first and MEASURED at 11.86 ms a
  frame on Kirby's Dream Land against 11.08 - the core tier's code calls
  back into the first segment's all the time, and every such exit took
  `tr_link`'s lookup. With the log it is 11.08 again.
- **What still flushes**: one tier alone (no core tier, no second
  segment), and a generation counter near its wrap (`FFF0h`, which resets
  it). A preload or a FIXED table (Static, 16.8) STOPS at a full cache
  instead, as before: nothing in it may be forgotten.
- `[S_JITRCY]` counts the recycles (and `[S_JITFL]` counts each one as a
  flush as well, so the field log's flush column reads as it did); the bare
  harness prints it as `recycled`.
- **The gate** is `tests/rdgstatic.py`'s squeezed one-segment row, which
  must recycle at least twice through cpu_instrs and pass; its NEGATIVE
  CONTROL makes the age test's `jae` a `jmp` in the core, so a link into an
  older tier is made and NOT logged, and the recycling of that tier leaves
  it pointing into new code - cpu_instrs must then fail, and does.
- **The stress**, by hand: the two games squeezed the same way on
  `286-victory` (`make_image(squeeze=True)`, 3,000 frames) recycle 5,419
  times (Kirby's Dream Land) and 1,583 (Super Mario Land), 5,036 and 1,430
  with the second segment as a third tier, and end with the same event
  count and skip checksum as the unsqueezed run - the same game, frame for
  frame.

### 16.3 Blocks and tr_enter

A block is straight-line code from a PC to the first control transfer, at
most 40 instructions, never across 4000h or 8000h, keyed by its PC and - in
4000-7FFF - the ROM bank. `tr_enter` finds it: a direct-mapped table of 2,048
entries (PC, bank key, a count, the block's offset), indexed by the PC's low
11 bits with the bank key and **the PC's top five bits** folded into the low
byte - so blocks 2KB apart, which bank 0 and every bank of 4000-7FFF are
full of, do not take turns at one slot. That fold took Kirby's Dream Land,
the CPU alone, from 15.15 to 13.73 ms a frame on 86Box's 286.

**A slot has a NEIGHBOUR** (its index XOR 1), which `tr_new` and `tr_link`
both look in when the slot holds another key, and a new key
takes whichever of the two is worth less - a counting entry before a
translated one, and of two counting ones the colder (`tr_new2`); the
translator writes a block into the slot its PC counted in (`S_JITSLOT`).
Counters in the package showed why: with one slot, two hot PCs that shared
it took it from each other before either was counted to `RDG_JITHOT`, so
both ran interpreted for ever, and every exit to them missed its link -
in Super Mario Land walking, 10 lost slots, 17 entries counting and 17
failed links a frame. With the neighbour the CPU alone (`rdgbench.py --box
286-victory`) went from 9.29 to 7.32 ms a frame on Super Mario Land and from
11.35 to 9.75 on Kirby's Dream Land. **A PC is translated
only on its 64th entry** (`RDG_JITHOT`), so code that runs a few times is
interpreted and never takes the cache's room; a translated entry gives its
slot up only after 64 others have asked for it. 16 was tried twice. With
the block table still in the cache it had Kirby's Dream Land walking
translate 2,933 blocks in 15 seconds and flush 7 times. With the table out
(16.2) the twin's scripted walk read better at 16 (96.9% of real time over
40 seconds against 94.5%: warming is the COUNTING, `tr_ekey`, `tr_enter`
and `tr_count` ~29% of Kirby's first seconds against the translator's
~4%), and the fourth field run REFUSED it: played on the Victory, Kirby
flushed every 5-6 seconds and retranslated 350-500 blocks each time, the
music slowing for a second at 47-77% of real time, where at 64 the flushes
had been ~20 seconds apart. **A flush forgets everything**, the table with the code. Keeping what was
hot (an entry that had a block counting again from near `RDG_JITHOT`) was
tried and REFUSED: on the bare harness it took Kirby's Dream Land from 7.64
to 7.14 ms a frame, but in the package on `286-victory`, the recompiler
forced on, it went from 61.7% of real time to 45.3% (counting again from
`RDG_JITHOT - 16`) and 19.8% (`- 1`), with 22 and 83 flushes in 30 seconds
against 6 - a working set bigger than the cache refills it at once after
every flush when nothing has to earn its place again. Tried a second time
once the block table had left the cache and the working set fitted (a flush
every 15-20 seconds, from code a level left behind): counting again from
`RDG_JITHOT - 8` read 85.9% over 40 seconds against the plain flush's 93.2%. RAM is never translated - the interpreter's own
control transfers stay in the interpreter there (`JDISPATCH`) - **but HRAM
is** (FF80-FFFE, `tr_hram`), where games keep their OAM DMA routine and
thunks such as Looney Tunes' `LD (DE),A / RET` that a `RST` jumps to in a
copy loop: an HRAM block is at most 16 bytes and is entered through a GUARD
(`rt_guard`) that compares its bytes with HRAM as it is now, and a block
whose bytes changed is forgotten (`tr_stale`) and its code interpreted.
`gbrmw` rewrites a routine there once it has been translated. **With no
recompiler** - an 8088, no memory for the cache, or CPU > Interpreter
- `rt_dispatch` writes the plain dispatch over the first five bytes of all
45 `JDISPATCH` sites (rdgtmpl.py lists them), so the interpreter is the one
there was before the recompiler, and writes them back when it is turned on. Translated code does not keep DS;
`tr_int` sets it for SI before it dispatches.

**The block table is 64KB when the heap has it** - 8,192 entries, mask 31
(`S_HMASK`) - and 16KB, 2,048 and mask 7, when it does not. Every PC that
reaches `tr_enter` takes a slot to count in, so at 2,048 Kirby's Dream Land
in play fought for them (`tr_new2` and `tr_link` in its profile): the CPU
alone on `286-victory` with the second segment (16.2.1) went 330 -> 285 ticks
at 4,096 and 266 at 8,192; Super Mario Land 124 -> 119. The mask is an
immediate in the core's three lookups (`tr_hm0`-`tr_hm2`), which `rt_init`
writes before it copies the tails; the translator's own two read
`S_HMASK`. **With ONE segment**, Kirby's Dream Land loses ~2% (366 -> 374):
fewer PCs are refused a slot, so its too-small cache fills and flushes
more - that game wants CPU > Cache: 128K anyway. The package claims 64
+ 64KB first, then 64 + 16; a ROM that does not fit gives back the second
segment, then the big table, then the recompiler.

**`tr_enter`'s far ends are stubs inside its slot.** A Jcc out of short
range is assembled as an inverted Jcc around a `jmp`, so every one of them
was a TAKEN jump on the hit path; and `JDISPATCH` enters at `tr_ejd`, past
the two guards its own site has made (it is patched away whenever the
translator is off). Both games -1.6%, the CPU alone.

### 16.4 Chaining

An exit to a constant address in bank 0 is `mov dx,SITE / jmp tr_lsite`,
six bytes that `tr_link` rewrites into `jmp near` the block once it is
translated, or that the translator writes so at once when it is already
there (`jmp far` to the other cache segment, 16.2.1). An exit into
4000-7FFF gets thirteen: a check of `S_BANK` and the jump, falling onto the
exit's own `mov si,T / jmp tr_enter` when the bank is another. (It had
eleven, which held a near jump and no far one.) An exit sets SI only on the ways that need it - the event's
(`jmp far [ss:S_EVVEC]`) and the bank's fall - and not before its `js`, so
a chained exit is the event check and the jump: `tr_lsite` takes T out of
the exit's own code (the `mov si,T` after its jump, SITE + 7, or SITE + 14
after the seven spare bytes of one into a bank). With the stubs below, it
took the CPU alone on `286-victory` from 6.77 to 6.59 ms a frame on Super
Mario Land, 7.87 to 7.64 on Kirby's Dream Land and 2.01 to 1.97 on Dr.
Mario. Every
`RET` carries an inline cache, `cmp si,PC / jne / cmp byte [ss:S_BANK],B /
jne / jmp near BLOCK` and two spare bytes (or `jmp far BLOCK`, 16.2.1),
else `mov dx,SITE / jmp tr_link`: `tr_link` looks the
return up as it does an exit and, seeing the site's `cmp` (81h) where an
exit has its `mov dx`, fills the cache with the last return (`tr_rpatch`) -
the bank check made a `jmp short` over itself when the block is in bank 0.
A routine called in a loop then returns without a lookup. **The cache
holds two returns** (38 bytes): a miss moves the last one down
(`tr_rpatch`, a near jump's displacement 19 less, a far one's as it was) and takes the first place,
so a routine called from two places in turn - Kirby's Dream Land is full
of them - returns without a lookup too: in the package on `286-victory`,
Kirby went from 93.2% of real time to 94.6% over 40 seconds, and its
picture from 12 frames a second to 15. It once cached
bank-0 returns only, and every return into 4000-7FFF went the whole way
round `tr_enter`: 35 a frame in Kirby's Dream Land walking, and caching them
took the CPU alone from 9.75 to 9.06 ms a frame on `286-victory`.

### 16.5 Guards

Translated code does not keep SI either, so a store that can reach the I/O
path must say where it is: `rdg_irqnow` takes an interrupt inside an IF or IE
write by moving SI to the vector. A store whose address is not known - LD
(HL),r, LD (DE),A, LD (C),A and the rest of `rdggen.SELFGUARD` - GUARDS
ITSELF, in its slow path, which is the only one that can take an interrupt
or reach the MBC: SI = NEXT (the template's IMM16, record flag 16) before
`wr_slow`, and after it the block is left through `tr_enter` when SI moved
or the write went below 8000h, where the bank may have changed. The fast
path, a write to RAM, carries no guard at all; the guards around every such
store used to be 12 bytes, and 23 in a block running from 4000-7FFF, and
taking them out of the RAM path took the CPU alone on `286-victory` from
8.19 to 7.87 ms a frame on Kirby's Dream Land and from 6.91 to 6.77 on Super
Mario Land. `LDH`/`LD (nn)` to IF or IE keep `mov si,NEXT` and a check after;
a block running from 4000-7FFF compares the bank after an `LD (nn),A` below
8000h, and leaves when it changed.

### 16.6 What it buys, MEASURED on 86Box's 286

gbclock's busy loop costs **13.95 clocks a busy M-cycle against the
interpreter's 26.2**. Super Mario Land, the CPU alone on gbprof's script, is
11.67 ms a frame against 13.04; in the package, in play, **80.1% of real
time in the window and 76.0% full screen**, against 59.5% and 58.0%
interpreted. docs/reports/REALDMG-JIT-2026-10-19.md has the record.

**With drawing by damage and the draw clock** (sections 13.2 and 15.2),
Super Mario Land runs at **100.7% in the window and 101.5% full screen**,
drawing 7 and 34 frames a second. Kirby's Dream Land runs at 31.6% and
37.2%, bound by the Game Boy CPU: 48% of its time is in translated code and
32% in the interpreter and `tr_enter`. **That is Kirby walking**, and walking
is what costs: standing in the same scene it runs at **78.7%** in the window
drawing 16 frames a second, and walking right 35.8% drawing 2 - the scroll's
own code, not the picture (`rdgwin.py`, the hash fold of section 16.3 in).
docs/reports/REALDMG-DAMAGE-2026-10-19.md has the record.

**Loop idioms** (from docs/reports/REALDMG-STATIC-2026-10-20.md, which
found three loops that each jump to their own head to be 17-26% of Super
Mario Land's machine while it scrolls with enemies). `rt_jrt` recognises
two more shapes beside the countdown of section 8.1, and the taken `JR NZ`
goes to a core tail that runs the rest of the loop in one step when BP
covers all of it - so no event can fall inside - and back to the loop
otherwise:

- **`add_ff`**: `ADD HL,DE / DEC B` (or `C`) `/ JR NZ,-4`, a background
  map's row stepped through (3EF9, B = 32). HL += DE x r, r = 0, the last
  ADD's carry and DEC's Z and N, 6r + 2 M-cycles.
- **`cpy_ff`**: `LD A,(HL+) / LD (DE),A / INC DE / DEC B / JR NZ,-6` and
  `LD A,(DE) / LD (HL+),A / ...`, a copy (2CF0 and 2D02, an enemy's 13-byte
  slot between WRAM and HRAM). Only when both ranges lie wholly in
  8000-FEFF or FF80-FFFE, where translated loads and stores take memory
  directly; `REP MOVSB` forward, so an overlap smears as the loop does.
  10r + 2 M-cycles.

- **`stat_ff`**: a wait on the PPU - `LDH A,(41h) / AND 3` or `BIT 1,A`
  `/ JR NZ,-6` (HBlank, or HBlank or VBlank; Super Mario Land's ~20 a frame
  before a VRAM write) and `LDH A,(44h) / CP n / JR NZ,-6` (line n). LY and
  STAT are `S_TNF - BP`'s, so how long the wait has to run is known: whole
  passes of its 8 M-cycles that are certain to read "not yet" come off BP,
  a pass short of the change and never past the next event, and the loop
  reads the change itself - A and F are its own. A margin of 0 is exact and
  8 over is not, on 128 phases each (`gbrmw.asm`'s Waits, below); 8 short
  is kept. Every other emulator surveyed (REALDMG-PLAN, wave 7) does this.
  **`CP (HL) / JR NZ,-3` with HL on LY** (FF44h) is the same wait for line
  A, and Donkey Kong Land's (the compatibility sweep found it going round
  one at 12% of real time on a 286; skipped, 101% and the machine 69% idle).
  It arrives as a countdown would - a taken `JR NZ,-3` - so the translator
  sends it to `cnt_ff` with DX = 00BEh, which hands it to `cnt_cph`
  (page 47h's CB half): HL is looked at when it runs, and `stat_ff` is
  entered with DL = 45h. Its pass is 5 M-cycles, so `stat_t5` takes off
  whole FIVES - another size moves the loop's phase. The interpreter's
  `JR NZ,-3` reaches `cnt_ff` with DH = FFh, the displacement's sign, so it
  never matches and `gbrmw.asm`'s WaitLYH run from WRAM is the reference
  the translated copy is held to. That gate goes red for a skip 50 M-cycles
  too far; it does NOT tell eights from fives, its counting loop being too
  coarse for a phase of a cycle or two, so the rounding stands on the
  argument above rather than on a row.

The CPU alone on `286-victory` (`rdgbench.py`): Super Mario Land 6.55 ms a
frame without any, 6.32 with `add_ff`, 6.22 with `cpy_ff` and 6.13 with
`stat_ff` - its reads themselves (`io_rd`, `io_lcd`) are what is left of
the waits.
`tools/gbprof/gbrmw.asm` runs the first two against the same loop closed by
`JP NZ`, which no idiom takes, and the waits from ROM against a copy in
WRAM, which is never translated, each after a HALT on VBlank so both start
in step; it fails when any tail is broken (the last ADD's carry dropped; A
taken from the wrong byte; a wait skipped one pass too far).

### 16.7 Three modes, and no Auto

**CPU has three modes**: **Recompiler** (the default), **Static
Recompiler** (16.8) and **Interpreter**; F3 steps through them in that
order. On an 8088 the first two are greyed with "(286+)" and Interpreter is
the mode; on a 286 they are settings before a game is loaded too, which its
load then takes up.

There was a fourth, **Auto**, that timed the recompiler against the
interpreter when its cache thrashed and kept the cheaper, and it is gone.
Kirby's Dream Land once ran faster interpreted (59% of real time against
46-51%) while the block table lived in the cache; every measurement since
the table left it has the recompiler ahead on both games, and Auto's own
timing was the slowdown - four seconds of every cycle interpreted. With the
core's holes as a second tier (16.2.2) the thrash it watched for no longer
happens on the games that had it.

Turning the recompiler off keeps the cache, and turning it back on
(`rt_resume`) forgets nothing: the cache holds only ROM's translations,
keyed by PC and bank, and neither changes while it is off.

### 16.8 Static: the code translated before the game runs

**CPU > Static Recompiler** translates a game's code BEFORE it runs,
then fixes the block table, so that nothing is translated, counted or
evicted during play. A PC with no block is interpreted.
- **What it is for:** the stall. A translation burst during play or a whole
  flush (16.3) slows the game for seconds: Kirby's Dream Land on the Victory
  flushes every ~25 s and slumps after each. Static cannot do that, because
  the cache never changes once the game is running.
- **What it is not:** a second code generator. It is section 16's own
  translator, called once a block before the game starts, so a block is
  exactly what tr_miss would have made and every gate on the recompiler
  holds for it.
- **The analysis behind it** is docs/reports/REALDMG-STATIC-2026-10-20.md.
- **Memory** is the recompiler's own 80 KB plus the profile's claim. The
  fallback is the interpreter and not the dynamic recompiler: running both
  would need two caches.

**The profile** is what to translate. It is a table of `S_PRFMASK` + 1
entries of 4 bytes in the segment `S_PRFSEG` (0: none):
- **An entry:** a word PC, a byte bank key (as the block table's: FFh below
  4000h, else the bank) and a byte weight (0: empty).
- **Recording** (`rt_prfadd`): every block the translator finishes enters
  the profile with weight 1, or adds 1 to its key's weight. A new key takes
  the first empty entry in the 16 from its hash; when all 16 are taken it
  replaces the lightest of them, if that one weighs 1.
- **There is no heat in the weight.** Two samplers were built and both
  were refused:
  - **at every event:** events fall where the game waits, frame after
    frame, so in Kirby's Dream Land one block took a weight of 199 and 511
    kept 1;
  - **at the end of a budget:** a budget is Game Boy time, and most of
    Kirby's Game Boy time is its spin-wait, which is skipped for nothing.
    So the budgets end there, and one block took 158 while 654 kept 1.

  Where the HOST's time goes cannot be sampled from inside the core, which
  runs with interrupts off (5.2). The weight therefore counts translations,
  which separates blocks once a long session has flushed: a block that is
  translated again after every flush is one the game keeps coming back to.
  It matters only when a profile holds more than the cache (below).
- **HRAM** (PC 8000h and up) is never recorded, because its bytes are not
  there before the game copies them in.
- **Over sessions:** the profile accumulates across every session of the
  game played with the Recompiler and **CPU > Profile for Static** on.
- **CPU > Profile for Static** (On or Off; greyed on an 8088, and settable before
  a game, whose load then claims it). Off claims
  nothing and writes no file. What it saves is the 9 KB claim and the 8 KB
  write at the close; the cycles are nothing to speak of, since recording
  runs once a TRANSLATION (at most 16 probes) and not once a block run.
  - **Off by default, and a `NAME.RDP` beside the game does not turn it
    on**: the load reads the file to ask whether the cache wants its second
    segment (16.2.1), and keeps the claim only for Profile for Static on or
    Static chosen - otherwise it goes back. Static still uses the file when
    it is chosen. (It turned itself on for a game with a profile until the
    menus' rework; recording is the user's to ask for.)
  - **Static turns it on** (it reads the profile) and keeps it on: Off is
    refused with a toast while Static is the setting.
  - **Turned off** in a session, it saves what was recorded first, then
    clears `S_PRFSEG` and waits to see the worker out of the machine
    (`[rw_incore]`) before it frees the claim, so a `rt_prfadd` that had
    read the segment has finished with it.

**The preload** (`rt_preload`) runs with DS = the view, the cache just
flushed by `rt_init`, and the core not running:
- **Order:** every entry by weight, heaviest first.
- **Each entry** is translated as tr_miss would translate it. Its bank is
  mapped in for the call (`S_BANK`, and `S_ROMX` from `RDG_BTAB`) and put
  back after. It goes into its slot, or the slot's neighbour when the slot
  already holds a block.
- **Skipped entries:** one whose bank is not in memory (the bank cache's,
  10.3), and one whose slot and neighbour both hold blocks.
- **A full cache ends the preload.** While `S_JITPRE` is set, `rt_room`
  answers a cache with no room left with `S_JITPRE` = 2 and no flush, and
  `rt_translate` gives up its block before it enters the table.
- **After it**, `S_JITFIX` = 1. AX returns the blocks translated.

**Fixed** (`S_JITFIX` = 1):
- `tr_new2` takes no slot for ROM, so a miss there is the interpreter's,
  with no count, and it never evicts a block.
  - **Why no count:** with a count, every cold ROM PC came to `rdg_jit` once
    in 63 entries just to be refused, and that cost more than it saved:
    Super Mario Land 6.18 -> 6.22 ms a frame, Kirby's Dream Land 7.55 ->
    7.60.
- **HRAM is still translated, on demand**, as the dynamic recompiler does
  it (16.3): its bytes are the game's to copy there, so a preload cannot
  make its blocks. Super Mario Land runs a routine at FFB0h every frame;
  interpreting it cost 1 tick in 133 of the bench below.
- **A full cache is never flushed** while the table is fixed: `rt_room`
  answers it as it answers a preload's (`S_JITPRE` = 2), and the block is
  interpreted.
- `rdg_jit` translates no ROM block.
- **Choosing Recompiler clears** `S_JITFIX` and `S_JITPRE`, and the
  dynamic recompiler carries on over the preloaded cache.
- **`S_JITNBR`** counts the ROM blocks translated. A fixed table's must not
  move, and the bare harness's DONE line says by how much it did.

**The file is `NAME.RDP` beside the game**, the save's folder (10.1):
- **Layout:** the table, then a 9-byte trailer: `'RDP1'`, the cartridge
  header's checksums (the byte at 014Dh and the word at 014Eh) and the
  entry count as a word.
- **Read at the load**, before the ROM's plan (16.2.1 asks it how big it
  is), and checked once the ROM is in: a file that is not the game's
  (magic, checksums or count) gives an empty profile. Turned on in a
  session, it is read from the game's own folder (`rw_romdir`), wherever
  the current one has moved to.
- **Written when the window closes**, only if a translation added to it this
  session.

**F3's order** is Recompiler -> Static Recompiler -> Interpreter -> Recompiler.

**CPU > Interpreter gives the recompiler's memory back** - the 80-128 KB of
cache and table, and its second segment. Chosen before a game, the load
claims none of it and the ROM has the room (a cartridge that would have gone
to the bank cache may fit whole). Chosen in a game, the worker switches to
the interpreter between two core runs (`rw_jitset`) and the UI task frees
both with the worker kept out of the machine (`rw_jfree`), the view's
`S_JITSEG`, `S_JITSEGA` and `S_JITSEGB` cleared so that the recompiler starts
again from nothing. Leaving it claims the cache again on the UI task first
(`rw_jitprep`, and F3's through `[rw_f3q]`); with no room the mode stays and
it is said. Measured on `vm/286` with Super Mario Land: 97.5 KB free on the
Recompiler, 225.5 KB on the Interpreter, 97.5 again on the Recompiler.
- **Static with an empty profile** is the interpreter with the recompiler's
  lookups: play the game with the Recompiler first.
- **What it buys, MEASURED on `vm/286-victory`** (`tests/rdgcore/rdgwin.py
  --static 45 --persec`: 45 s played with the Recompiler, then Static,
  Machine > Reset and the same 40 s timed again, against a fresh run's
  40 s):
  - **Super Mario Land** holds real time EVERY second, 94-105% (100.1%, 29
    pictures a second), nothing translated. The dynamic recompiler's run
    reads 95-112% (100.2%, 28). Its 489 blocks fit the cache.
  - **Kirby's Dream Land** with one cache segment was steady but slower:
    72-89% (85.1%, 12 a second) against the dynamic recompiler's 47-95% for
    its first six seconds and ~100% after. Its 45 s of play are ~670
    blocks, more code than one segment holds: 594 were preloaded and the
    rest ran interpreted.
  - **...and with the second segment (16.2.1)**, which the Static switch
    took from the heap because the profile named 668 blocks: **655
    preloaded and 100.0% of real time, 95-109% every second after the
    first (80%), 24 pictures a second** against 12 - in the bank cache,
    12 slots, no bank fetched. Static is then the better setting for it
    too.
  - **A profile turned on in the middle of a session** has missed what
    ran before it - a title, a death, a game over - and Static interprets
    that code. One Super Mario Land run with such a profile (421 entries)
    held real time but for two seconds at those transitions, one at 2%
    and caught up after (98.7% over 40 s); the same run with a profile
    recorded from the game's start held 94-105% every second, on this
    build and the one before it alike. Why a missed transition costs more
    than the interpreter does is OPEN. CPU > Profile for Static can be set before
    the game is opened, which records it from the start.
- **The CPU alone** (`tests/rdgcore/rdgbench.py --static`, the second pass
  timed): Super Mario Land 6.18 ms a frame against 6.09 dynamic, Kirby's
  Dream Land 7.55 against 7.37 - a fixed table's misses cost a little more
  than a counting table's (above).

**The bare harness** (12.1, `b_jit` = 2):
- **The profile** is 1,024 entries at 0500:0000, outside the view, which
  `rdg_init` zeroes; so not in MODES 5 and 6, which draw there.
- **Two passes**: FRAMES frames with the recompiler recording the profile,
  then the game again from its start (`rdg_init`), the profile preloaded and
  the table fixed, for FRAMES more - the frames a bench times are the
  second pass's. That is the package's order too: play with On, then
  Static.
- **The DONE line** gives the blocks preloaded, and the ROM blocks
  translated after the switch, which must be 0. `tests/rdgstatic.py` is the
  gate, with its negative control.

### 16.9 The recompiler is a part of its own

The recompiler - `rdgjit.inc`'s code and the 16,477 bytes of `rdgtmpl.bin` it
translates with, 20,952 bytes in all - is not in the package's image. It is
part 3 of `REALDMG.O88` (`apps/realdmg/rdgjitp.asm`, assembled at org 0),
compressed, `OP_LAZY | OP_SELF`, and `rw_jitget` reads it into the TAIL of
the recompiler's own claim - past the cache and its block table - when it
claims them. So:
- **An 8088 never holds it.** The image went from 52.7K to 33.2K, and the
  region a game holds with it (SPEC.md 20.12). An 8088 never claims a cache
  and never reads the part.
- **A 286 holds it only with a cache.** CPU > Interpreter frees the claim
  and the part with it (16.7); choosing a recompiler again claims and reads
  both. It is one `OSAPI_FILE_READ_AT` of the packed stream at the part's
  own sector, R up the claim, and `OSAPI_DECOMP` down onto its base - the
  read `apps/midirack`'s `mra_load` makes, at the moment the claim is made,
  which is at a game's load or at the menu's switch. It takes no claim of
  its own: a package holds eight at most (SPEC.md 50) and RealDMG's can come
  near that with the log and a replay on.
- **The read is made in `REALDMG.O88`'s own folder**, which the entry proc
  banks with `OSAPI_FILE_HERE` (`rw_pkgdir`, the log's folder too):
  `op_name` is a bare name, and by the time a game is open the instance
  stands in the GAME's folder. Read there, a game anywhere but beside the
  package found no part and ran on the interpreter with the recompiler
  chosen - Super Mario Land at ~75-110% on the Victory, with the apps disks
  keeping `REALDMG.O88` in `GAMES` and the cartridge wherever the user keeps
  it. `rw_fgo`/`rw_fback` go there and back around the read, MIDIRack's
  `mr_homedir` shape (SPEC.md 105.10).
- **Four far doors**: `rdgjitp.asm` starts with `rt_init`, `rt_resume`,
  `rt_preload` and `rt_dispatch` at offsets 0, 10, 20 and 30, each a near
  call and a `retf`; the package reaches them through four far pointers in
  its data (`rw_jinit` and the rest) whose segment `rw_jpart` writes. Inside,
  CS is the part's, which is all the translator's `[cs:rt_tab + ...]` reads
  need, and the core reaches `rdg_jit` through `[S_JITVEC]` as before.
  `rdg_jit` leaves through the part's own copy of `rdg_ret`, and `RDG_TOHOST`
  reads the view from the part's own word, which `rt_init`'s door sets.
- **What stays in the image** is `rw_jdoff`: the interpreter's 46
  JDISPATCH sites made plain DISPATCH (16.4), which an 8088 needs without
  the part - `rdgtmpl.py` writes the list into `rdgcore.inc` again as
  `RDGC_JDTAB` for it.
- **The bare harness** (`tests/rdgcore/rdghost.asm`) still includes
  `rdgjit.inc` inline, so every gate on the translator runs the same source.
- **What it cost**: nothing measurable at run time - the same code, called
  far at its four doors, which run once a game or once a switch. On
  `286-victory` Super Mario Land plays at 99.6% of real time with 291 blocks
  translated, Interpreter at 98.1%, and Kirby's Dream Land's Static preloads
  175 blocks into a fixed table, as before the move. A volume whose clusters
  are larger than a sector on `kern_small` refuses the read (`READ_AT`'s
  unit), and the game then runs on the interpreter, as with no memory.

## 17. Sound

**With an FM chip all four channels play (17.1); without one the speaker
plays the louder of the two square channels.** rdgsys keeps a record of each
channel (`S_SQ1`, `S_SQ2`, and `S_SQ3`, `S_SQ4` for the wave and the noise)
from the writes that decide whether it sounds, and the package turns those
records into sound. The other sound registers are plain memory, read where
they lie: every write to FF01-FF26 reaches rdgsys from the interpreter's
`io_wr` (one compare: the core's slot had no room for a list), which stores
those it does not follow, and a translated store to one of those is a single
MOV (`rt_snd`).

- **The writes rdgsys takes**: NR12/NR22, NR14/NR24 and NR52. The core's
  `io_wr` sends each of them to `rdg_iowrite`.
  - A trigger (bit 7 of NRx4) turns a channel on at NRx2's starting volume,
    unless NRx2's top five bits are zero: then the DAC is off and the trigger
    does nothing.
  - An NRx2 write that switches the DAC off silences the channel at once.
  - Bit 6 of NRx4 arms the length counter at `64 - (NRx1 & 3Fh)` steps of
    256 Hz.
  - NR52 with bit 7 clear silences both channels.
- **Time**: `rdg_vblank` calls `rdg_sqframe` once a Game Boy frame. Each call
  adds 274/256 of a step to the 64 Hz envelope and 1,097/256 of a step to
  the length counter: 64 / 59.7275 and 256 / 59.7275 Hz, kept in 1/256ths.
  An envelope steps the volume towards 0 or 15. A length counter that
  reaches zero turns its channel off.
- **The speaker**: after every frame it runs, the worker calls `rw_sound`.
  It does this in the window and in full screen alike.
  - It takes the channel that is on with the higher volume, channel 1 on a
    tie, and computes `131,072 / (2048 - x)` Hz from its NRx3/NRx4.
  - **The channel playing stays** while it sounds, unless the other is
    louder by 4 or more (`[rw_sch]`). Without that, two voices whose
    envelopes cross made the one speaker hop between melody and harmony
    note by note, which the first field run heard as "very odd".
  - It passes that pitch to `OSAPI_SND_TONE`, but only when the pitch has
    changed. Volume 0, and a pitch outside 20 to 15,000 Hz, are silence -
    and `2048 - x` under 9 is silence BEFORE the divide: x = 7FEh and 7FFh
    divide by 2 and 1, whose quotients do not fit in 16 bits, and a divide
    error on a 286 returns to the DIV and takes it again for ever. The
    compatibility sweep's Alleyway sets x = 7FEh when it pauses and hung the
    package so (`tests/gbhifreq.py`, on 86Box's 286: an 8088's divide error
    returns past the DIV, which is why gbsound never saw it).
  - A refusal (another owner of the speaker) is asked again the next frame.
  - A paused or stopped machine turns the tone off (`rw_quiet`).
- **The pitch is exact and the timing is the game's.** Notes follow the
  game's frames. A game running at half speed plays its tune at half speed,
  at the right pitch.

`tests/gbsound.py` is the gate. GBTONE.GB (`tools/gbprof/gbtone.asm`,
assembled by `tools/gbours.py` with the pinned rgbds) plays a trigger, a DAC
switched off, and a length counter that runs out. The row captures
MartyPC's speaker and asserts all three. With the length counter broken it
fails, naming the counter.

### 17.1 The FM chip

**All four channels on an OPL2** (`apps/realdmg/rwfm.inc`), when `SOUND.DRV`
has one and the Control Panel sends tones to it (`OSAPI_SND_CAPS`' route,
as 1942's): the squares on channels 0 and 1, the wave on 2, the noise on 3.

- **The chip is claimed whole** (`SND_FM_CLAIM`, SPEC.md 34.12) on the UI
  task when a game starts, and given back at the close (verb 3; the
  teardown would too). It is written through `SNDV_OPLW`, which takes no
  lock and touches no disk, so the worker writes it after each frame in the
  window and the bracket does on the UI task.
- **A frame writes only what changed**: a shadow of each channel's A0h,
  B0h and two levels (`rw_fmsh`) keeps a held note at no writes at all, and
  what did change goes in one call. A key off leaves the F-number alone.
- **The patches hold their level** (EG type 1, attack and release at once),
  and the Game Boy's own envelopes - rdgsys's records - set the carrier's
  attenuation frame by frame: v/15 as 20 log10(v/15) / 0.75 dB steps
  (`rw_fmtl`), plus NR50's louder side the same way (`rw_fmmv`). NR51 routes
  a channel to neither side: silent. NR52 off: all silent.
- **A square is the modulator at TWICE the carrier**, no feedback: 1:2 FM
  makes the odd harmonics alone, which are a 50% square's. Its duty is the
  modulator's depth (`rw_fmduty`) - the thinner the pulse, the brighter.
  Measured off MartyPC's OPL: square 1 alone peaks at 440 Hz with 1318 and
  2198 beside it and nothing at 880, where the first patch (1:1 with
  feedback) put 880 Hz LOUDER than 440.
- **The wave is near a sine**, an octave under a square at the same x
  (65,536 / (2048 - x) Hz), its volume NR32's code. **The noise is a
  modulator at x15 and full feedback** - an OPL2 has no noise generator but
  the rhythm section, which the driver keeps for itself with channel 8's
  system beep - at a pitch from NR43's clock (524,288 / (r x 2^(s+1)) Hz,
  r = 0 a half), the slowest and fastest held at what the chip can say.
- **The F-number**: f = fnum x 49,716 / 2^(20 - block), so a square's is
  2,764,482 / (2048 - x) at block 0, halved into range a block at a time;
  past 5,958 Hz (2048 - x < 22) the chip cannot say it and the channel is
  off.
- **rdgsys follows the wave and the noise as it does the squares**: the
  noise's registers sit as a square's (NR42 its envelope, NR44 its trigger,
  NR41 its length), so it is a square's record exactly; the wave's NR30 is
  its DAC, NR34 its trigger and NR31 a 256-step length, and it has no
  envelope. `S_SQ3`/`S_SQ4` are out of the full state block, at 48F0h, and
  a save state carries them (`ST_SQX`).

`tests/gbfm.py` is the gate, on MartyPC's XT with a Sound Blaster: GBFM.GB
(`tools/gbprof/gbfm.asm`, ours) starts the four channels a frame apart and
holds them, and the row checks each channel's F-number and key against the
Game Boy's own formulas, `SOUND.DRV`'s `opl_b0` against those, the capture
for sound, and square 1's spectrum as above. Without the claim it fails on
all four channels.

## 18. The 5150 demo

**`GBDEMO.GB` is RealDMG's own Game Boy ROM, written for an IBM PC 5150**
the way WireBall is written for it. It is assembled with the pinned rgbds by
`tools/gbours.py` from `tools/gbprof/gbdemo.asm`, and it runs on a real Game
Boy as well. What it does:
- It draws a still scene once, with the LCD off: a title, three lines of
  text, a band of the four shades and a ground strip.
- One 8x8 sprite, a ball, bounces on the ground in a 40-frame parabola, and
  between the walls a pixel a frame.
- A clock, MM:SS, is four map bytes rewritten once a second.
- Square channel 2 blips at 659 Hz each time the ball lands, and its length
  counter ends the blip.
- It HALTs for the rest of every frame.

So a frame costs the core a few dozen instructions. Drawing it costs the
lines the ball crossed, and once a second the clock's (section 13.2).

**MEASURED on MartyPC's 5150 with a Hercules** (cycle-exact, so these are
the 5150's numbers): **1.000 of real time** full screen, drawing 4 frames a
second; 0.999 in a window, at 3. The draw clock of section 15.2 is what
holds the game at full speed, and with it always due the game falls to
0.388. `tests/gbdemo.py` is the gate.

On a real machine the F1 line reads the same two numbers: the game's speed
and the frames drawn a second.

## 19. The disk and the Wire listing

**`make realdmgdisk`** writes the package's floppy in all four geometries:
`build/realdmg.img` (1.44MB), `realdmg720.img`, `realdmg120.img` and
`realdmg360.img`, each `--verify`'d.
- **The REALDMG folder holds three files**: `REALDMG.O88`, `GBDEMO.GB` and
  `README.TXT`. That is 64 of a 360KB disk's 354 clusters, so there is room
  for the user's own games.
- **It is on demand**, as every package's own disk is. `GBDEMO.GB` is
  assembled with the pinned rgbds, which is fetched and built on first use,
  so the disks take a COMMITTED copy, `apps/realdmg/GBDEMO.GB`: the build is
  deterministic, and `soak -k 'gbship'` rebuilds it and compares.
- **The shipped disks carry it too, without the README** (the owner's
  decision of 2026-10-22): `GAMES/REALDMG.O88` and `MEDIA/GBDEMO.GB` on the
  1.44MB and 1.2MB apps disks, kern_mega's two apps disks, the everything set
  (`make allapps`) and the live media, and `REALDMG.O88` at the root of
  SPEC.md 24.6's `emu360.img` with the cartridge in its `MEDIA/`. There the
  cartridge is LZ4-packed, 32,768 bytes to 1,110: a ROM of up to 256KB is
  read in ONE `OSAPI_FILE_READ` into a single claim (section 10), which is the
  transparent read's condition (SPEC.md 20.14.3), and only a cartridge in the
  XMS bank cache (section 10.3, over 256K) is read raw. The 720KB apps disk
  does not carry it: it has 98 clusters free and is being re-curated.
- **No game ships.** A cartridge is the user's own.

**`make realdmghd GBROMS=<dir>`** writes a HARD DISK, `build/REALDMG-HD.VHD`,
and **`make 286-realdmg`** boots it: the owner's mr286 (16 MHz, 4MB, VGA, an
SB16 for 17.1's FM chip) with the disk on IDE and nothing in A:. It is where
a cartridge over 1MB, which no floppy holds, can be opened at all - the
compatibility sweep's Pokemon Gold, Dragon Warrior Monsters and Wario Land II
DX are 2MB.
- **The disk is the whole install** (the Makefile's `VIDDEMO_INSTALL`, the
  system and apps disks' payload derived rather than listed), **RealDMG's
  folder**, and **`GAMEBOY/`** with the cartridges in `<dir>` - `GAMES/` is
  the apps disk's, and already there. `tools/gbromdir.py` names each from
  the title in its header (`POKEMONG.GBC`) and writes `GAMEBOY/ROMS.TXT`
  saying which file is which; the cartridges are only ever copied under
  `build/`.
- **The geometry is the AT drive table's type 3**, 615/6/17 (32MB): the MR
  BIOS then wants only "type 3" on its Fixed-Disk page, where a user type
  would have to be typed in. A fresh run has no CMOS, and
  `tests/rdgcore/rdghd.py`'s `setup` answers it - floppies 1.44M, type 3, F10 -
  waiting for each screen rather than counting keys, the BIOS taking an early
  key as nothing.
- **`rdghd.py` opens a cartridge from the keyboard**: C:, the folder by its
  row, then Down to the file's place (sorted as the directory sorts, with
  `..` first) and Enter - and checks the title at 0134h in the running view
  against the file's, so a wrong row cannot measure one game under another's
  name.
- **Measured there** (86Box, the profiler's Enter/X/Right script): Gold runs
  at 30-39% of real time, its time in the event scheduler for per-line STAT
  interrupts (`rdg_events`, `rdg_sched`, `rdg_statev`, 40%), Dragon Warrior
  Monsters 53-100%, Wario Land II DX 100% after its intro; Crystal is
  refused for the Color alone.

**The Wire record is PREPARED and NOT PUBLISHED.** It goes in os8088-web's
`data/wire.json` when the owner says go, and that repository is outside this
one. It was validated by packing it with `tools/os88wire.py --pack` and
reading the result back with `--verify`:

| field | value |
|---|---|
| `stem` | `REALDMG` (7 characters, so a `REALDMG.TXT` sidecar fits the 11-character field) |
| `title` | `RealDMG` |
| `kind` | 0 (Applications) |
| `tier` | **1** (286): the primary target, and where the measured speed is |
| `flags` | `new`. `WF_DISK` is derived: the package carries parts, and the record has sidecars |
| `files` | `REALDMG.O88`, `GBDEMO.GB`, and `{name: REALDMG.TXT, source: apps/realdmg/README.TXT}` - the README under a name of its own, as `public/wire/pkg/` is one flat directory (APPLE2-SPEC section 18.2) |
| `summary` | `A Game Boy (DMG) emulator: your own cartridges, in a window or full screen.` |
| `description` | `A Game Boy for your PC. Bring your own cartridges. Near full speed on a 12MHz 286; its demo runs full speed on a 5150.` (5 lines at 27) |

The record is 27,407 bytes of package, 62,566 with its sidecars. The
description's "near full speed" stands on 86Box's 286 figures (section
16.6), not yet on the owner's iron. It is the field run's to confirm or to
correct before the record is published.
