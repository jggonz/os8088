# Can a 4.77 MHz 5150 emulate a Game Boy at full speed?

**A feasibility estimate, not a measurement.** Written 2026-10-19 on a
four-core cloud container, at `elendilon` e8009a6d, on branch `gameboy`.
Nothing here has been built or run on an 8088. Every 8088 figure is either
**PREDICTED** by hand from the 8086/8088 data-book timings and PERFORMANCE.md
Part 2's fetch floor (an 8088 instruction costs at least ~4 clocks for each of
its bytes), or **MEASURED** elsewhere in this tree and cited by section. A
later measurement goes in a new file.

The question, as it was asked:

> Investigate a gameboy emulator, for a 4.77Mhz 5150. Is that feasible? [...]
> is it within the realm of possibility to run fullspeed if we optimize, or is
> it completely out of the league of the 5150?

## 0. The answer

**Full speed is out of reach for Game Boy games in general, and optimisation
cannot close the gap.** The CPU alone needs about **8-10x** the 5150 with the
best interpreter that can be written in assembly. A dynamic recompiler still
needs **2.5-4x** the 5150. The picture alone, redrawn at 60 frames a second,
needs about **2x** the 5150 on its own.

The deepest of these limits is the bus, and code cannot change it.
**The 8088's bus moves only about 1.14 bytes for every memory access the Game
Boy's CPU makes** (section 2). Any emulator has to fetch at least one byte of
its own code for each guest access, plus the guest's data, and usually several
more bytes. So even a perfect translator with no overhead would land at
roughly half to two-thirds of full speed when the game keeps its CPU busy.

**One narrow corner might work.** Picture a dynamic recompiler, a game that
idles in `HALT` for most of each frame, and a mostly static screen shown at
about 15 frames a second. That combination might keep full GAME speed, though
not a full frame rate. Whether any real game is that light is an open
question, and it can be measured on the host in an afternoon (section 8).

**The machine where full speed becomes ordinary engineering is a 286**
(section 6).

## 1. The two machines

| | Game Boy (DMG) | IBM 5150 |
|---|---|---|
| CPU | Sharp SM83 (LR35902), 8080/Z80-like | Intel 8088 |
| clock | 4,194,304 Hz (T-states); **1,048,576 M-cycles/s** | 4,772,727 Hz |
| bus | one memory access per M-cycle, 8-bit | at most one byte per 4 clocks, 8-bit, **1.19 MB/s** |
| frame | 154 lines x 456 T = 70,224 T = **17,556 M-cycles**, 59.73 Hz | **79,909 clocks** per Game Boy frame |
| refresh | none | DRAM refresh DMA takes ~4 clocks every 72, **~5%** |
| screen | 160x144, 2 bits per pixel, tiles plus 40 sprites, hardware scroll | CGA, Hercules or VGA, no sprites, no fine scroll |

**The budget is 4.3 clocks of 8088 for each M-cycle of Game Boy**
(79,909 x 0.95 / 17,556). An 8088 cannot do much in 4.3 clocks. That is
about one byte fetched, and less than one complete instruction.

## 2. The bus ceiling, which no code can get under

The SM83 makes one memory access per M-cycle, nearly always. Opcode bytes,
operands and data each take their own M-cycle, and only a few cycles are
internal. Over a second that is about **1.05 million byte accesses**. The 8088
can make at most **1.19 million**. The ratio is **1.14**, and refresh pulls it
down to about 1.08.

An emulator does more bus work than the machine it emulates:

- **An interpreter** fetches the guest's opcode byte as data, which is one host
  access per guest access, plus the bytes of its own handler code.
- **A recompiler** fetches its translated code instead, at 4 clocks per byte.
  The closest SM83-to-8086 translations are still 2 host bytes or more per
  guest byte. For example, `ld a,b` (1 byte, 1 M-cycle) becomes
  `mov al,ch` (2 bytes, 8 clocks). Guest data accesses come on top of that.

So the floor for a perfect translator is about **1.5-2 host bus accesses per
guest M-cycle**. That is 6-8 clocks against a budget of 4.3, a ceiling of
**55-70% of full speed** for code that never idles. Real translators land well
below their ceilings. This is what separates the 5150 from the NES-class
claims: the 5150 lacks the bus to keep up even in principle, so the problem
is not code quality.

## 3. Calibration from this tree

The tree already ships two emulators of 8-bit CPUs written in C. One of them
has a measured figure on the 5150:

- **APPLE2** (docs/APPLE2-SPEC.md, the speed section): **~415 8088 clocks per
  emulated 6502 cycle**, MEASURED on MartyPC's 4.77 MHz 8088. That caps raw
  core throughput at **1.13%** of a 1.02 MHz Apple. Redraw takes it to
  **0.54%**. The plan had predicted 3-4%, an error of six times.

A 6502 cycle and an SM83 M-cycle are the same unit of work: one bus access.
At 415 clocks per M-cycle against a budget of 4.3, **this tree's C toolchain
would run a Game Boy at about 1% of full speed**. That rules out C: the core
has to be assembly, as already proposed.

The Apple figure is also a warning about estimates. The plan's arithmetic was
three times too optimistic per instruction. Every PREDICTED figure below
should be read with that in mind, and section 8 is how to replace them with
measurements.

## 4. The CPU

### 4.1 The best interpreter (PREDICTED: ~10-12% of full speed)

The SM83 descends from the 8080, as the 8086 does, so the register file maps
almost one to one:

| SM83 | 8086 |
|---|---|
| A | AL |
| F (Z, N, H, C) | AH via `lahf`. x86 ZF, AF and CF are Z, H and C directly; only N needs a separate bit |
| BC | CX |
| DE | DX |
| HL | DI |
| PC | SI (a host pointer into the current code region) |
| cycle budget | BP |
| dispatch | BX, with **BL held at 0** |

**The cheapest dispatch available** puts each handler at `opcode x 256` in a
64KB code segment of its own: 256 handlers of up to 256 bytes each, exactly
one segment, plus a second segment for the `CB` page. Each handler then ends
with the dispatch inline:

```
mov  bh,[si]     ; 2 bytes, ~13 clocks  - the next opcode is the high byte
inc  si          ; 1 byte,  ~4 (fetch-bound)
jmp  bx          ; 2 bytes, ~11, and the prefetch queue is flushed
```

That is **~28 clocks** before the handler does any work, and a refilled queue
after it. Three representative instructions:

| SM83 | M | budget | handler | predicted total |
|---|---|---|---|---|
| `ld a,b` | 1 | 4.3 | `mov al,ch` (8, fetch-bound) | **~36** (8x) |
| `add a,b` | 1 | 4.3 | `add al,ch` / `lahf` / clear N (~20) | **~48** (11x) |
| `ld a,(hl)` | 2 | 8.6 | page lookup for H, load ES, read, I/O test (~45) | **~75** (9x) |

**Every memory access needs a region test.** The Game Boy's 64KB address
space contains a switchable 16KB ROM bank, banked cartridge RAM, VRAM, OAM, and
I/O registers whose reads have side effects (`LY`, `DIV`, the timer, the
joypad). Writes are worse. A write to ROM is an MBC bank switch, a write to
VRAM has to invalidate a tile cache, and a write to I/O can start DMA or
change the display.

**One flat 64KB "view" segment is not an option either.** Its bank switch
would be a 16KB copy, about 8,192 `rep movsw` words at ~25 clocks each, which
is **~205,000 clocks: 2.6 Game Boy frames per switch**. Many games switch
banks several times a frame, because a music driver in another bank runs every
vertical blank.

Add the event checks (the PPU line every 114 M-cycles, the timer,
interrupts) and the average comes to **~35-45 host clocks per guest M-cycle**.
That is **10-12% of full speed**, about ten times the C figure in section 3.

### 4.2 A dynamic recompiler (PREDICTED: ~25-35%; ceiling 55-70%)

The same register map lets most SM83 instructions translate to a single 8086
instruction, the way Intel's CONV86 translated 8080 source. H and L become BH
and BL. Flags stay in the x86 flags until something reads them, so
`dec b / jr nz` becomes `dec ch / jnz` with no flag traffic at all. The gains
are large:

- simple register operations cost ~8 clocks per M-cycle (2x budget) where the
  interpreter needs ~36;
- an operand known when translating stays a constant: `ld a,(nn)` into WRAM
  becomes one `mov al,[es:nn]`, and `ldh a,(44h)` becomes a direct call to the
  `LY` handler;
- block linking removes dispatch from straight-line code;
- cycle accounting moves to block ends, ~20 clocks per block of ~5-8
  instructions.

What stays costly is every `(hl)` access whose region is unknown, every I/O
read and write, every bank switch (blocks translated from the old bank must
not be reused), self-modifying or RAM-resident code (the OAM DMA routine every
game copies to `FF80h`), and the translator itself, all paid on a 4.77 MHz
machine. An average of **~12-18 host clocks per guest M-cycle** is a
reasonable prediction, which is **25-35% of full speed** for busy code. The
bus ceiling in section 2 is 55-70%.

**Translating one game ahead of time on the host** removes the translator's
cost, but changes nothing else. It is bound by the same bus ceiling and the
same region tests, and it works for only one cartridge.

### 4.3 What the game itself saves

Most games finish their frame's work early, then wait for vertical blank. They
either `HALT`, which costs an emulator nothing because it skips to the next
event, or poll `LY` in a tight loop, which can be detected and skipped the same
way. So the CPU cost is

    CPU time per frame = busy fraction x (1 / emulator speed)

On the interpreter, a game must be busy **under ~10%** of each frame to keep
up, with nothing left over for drawing. Few real games are that light. On the
recompiler, the limit is under ~25-35%, which a light game might meet. No
measured busy fractions exist yet for any game. Section 8's first experiment
measures them.

## 5. The picture (PREDICTED: ~2x the 5150 at 60 frames a second)

A Game Boy frame is 20x18 tiles of 8x8 pixels, which is **2,880 tile rows**.
Each tile row is two bit planes, a low-bits byte and a high-bits byte. Host
formats are packed, so every tile row has to be converted:

- **CGA 320x200x4** comes closest: the four shades become the four colours,
  and the frame fits at 1:1. Converting through two interleave tables, with
  `BGP` folded into the tables and rebuilt only when it changes, is
  **~50-70 clocks** per tile row. Writing to CGA memory is more on top of that.
- **Hercules 720x348** needs the frame doubled (320x288) and the shades
  dithered, so a tile row becomes 8 output bytes.
- **VGA** on an 8-bit ISA slot writes a byte per pixel, 23,040 bytes a frame.

**A full CGA redraw is ~2,880 x 60 = ~173,000 clocks, about 36 ms.** That is
2.2 times the 16.7 ms Game Boy frame, before sprites, before the window layer,
and before any time for the CPU. Caching pre-converted tiles, and
reconverting only tiles that VRAM writes have dirtied, halves the cost to
**~86,000 clocks** (1.08 frames). That is still more than the whole machine
at 60 frames a second.

The levers that remain:

- **Frame skipping.** Showing 1 frame in 4 (15 fps) cuts the cached redraw to
  ~27% of the machine. Game speed and display rate are separate clocks, as
  CLAUDE.md's fullscreen-game notes already say.
- **Damage tracking.** Redraw only tiles whose map entry, tile data or sprite
  overlap changed. This is cheap on menus, text and puzzle screens, but
  useless on a scrolling level: the 5150 has no fine scroll, so moving `SCX` or
  `SCY` by one pixel changes every tile on screen.
- **Mid-frame effects get in the way of both.** Many games change `SCX` per
  line or split the screen with the window or an `LYC` interrupt to draw a
  status bar. Getting those right needs per-scanline state. A frame-at-once
  tile renderer either gets them wrong or pays for line granularity.

For comparison, this tree's C Apple II flush measures **110.9 ms** for a
280x192 frame (docs/APPLE2-SPEC.md). A Game Boy frame has 43% as many
pixels, and assembly can do several times better than that C code, which is
consistent with the ~36 ms predicted above.

## 6. Putting it together

With a recompiler (CPU at ~30% of full speed) and cached tiles shown 1 frame
in k:

    3.3 x busy fraction + 1.08 / k  <=  1

| display rate | CPU share left | the game must be busy under |
|---|---|---|
| 30 fps (k = 2) | 46% | **~14%** of each frame |
| 15 fps (k = 4) | 73% | **~22%** |
| 10 fps (k = 6) | 82% | **~25%** |

Damage tracking on static screens makes the drawing cheaper, and scrolling
or raster effects make it dearer. **Sound, joypad, timers, the OS's own tick
and the MBC are not in this sum at all.** For sound, the two square-wave
channels map onto the speaker or PIT cheaply, while the wave and noise
channels do not.

| target | interpreter | recompiler |
|---|---|---|
| **5150, 4.77 MHz 8088** | ~10% (a curiosity, though ~20x the Apple II port) | ~25-35% busy code; full game speed only for very light games at 10-15 fps |
| 8088 + NEC V20 | ~12-14% | ~30-45%, same conclusion |
| **286 at 10-12 MHz** | ~50-80%, plausible with care | **full speed is realistic**, with headroom for the picture |
| 386DX-25 and up | full speed | full speed |

The 286 row is a PREDICTION. It rests on the 286's 16-bit bus and its
clocks-per-instruction being roughly two to three times better than the
8088's, which together make it about 6-8 times an XT. The tree has the
machines to check it (`vm/286`, the owner's mr286).

**The V20's 8080 mode does not help.** The SM83 is 8080-shaped but not
8080-compatible: it changes or removes about a dozen opcodes (`LDH`,
`LD (HL+)`, `SWAP`, `STOP`, `RETI`, the `CB` page) and has no I/O
instructions. More importantly, a native mode cannot trap the memory-mapped
I/O or bank switches. It would also exclude a stock 5150.

**Memory is a second, separate limit.** Without EMS, a 640KB machine has
roughly a **449 KB** heap arena (docs/plans/KERN-DOS-PLAN.md), which must
hold the cartridge ROM, its RAM, WRAM, VRAM, the tile cache, the converter
tables and (for a recompiler) the code cache. **ROMs up to ~256 KB fit
comfortably**: Tetris is 32 KB and Super Mario Land 64 KB. 512 KB and 1 MB
ROMs (Link's Awakening, Pokemon) would have to page banks from disk, which
costs ~400 ms per `int 13h` call (CLAUDE.md's cost table), or come from an
EMS board.

## 7. On the NES comparison

I could not find a record of Peanut-GB, or any Game Boy emulator, running at
full speed on an NES. Two searches turned up nothing. What I did find about
Peanut-GB's speed:

- its maintainer reports **full speed on the RP2040**, a 133 MHz 32-bit
  Cortex-M0+;
- the **Playdate** port (a 168 MHz Cortex-M7) reports **20-30 fps** for most
  games.

The NES's 6502 runs at 1.79 MHz, with less bus bandwidth than the 5150. If a
full-speed Game Boy on NES hardware exists, it almost certainly had help: a
coprocessor in the cartridge, or a static translation of one specific game.
The NES's sprite and tile hardware is also much closer to the Game Boy's than
anything in a 5150, which takes the whole of section 5 off its bill.

### 7.1 The video's project: NESPC

The project in the video turned out to be **NESPC**
(github.com/ThroatyMumbo/NESPC, demo at youtu.be/o0s7tRcqsOs), a desktop
environment that replaces the EverDrive N8 Pro's menu. I read the repository
at its default branch on 2026-10-19. I did not watch the video, so this is
about the code and not about what the video shows.

**The repository contains no Game Boy emulator on the 6502.** There is no
Peanut-GB, no SM83 core, and no `.gb` handling in the 6502 code. Its README
says only that `games/` holds "Regular NES/GB ROMs".

**Every game launches as an FPGA core in the cartridge.**
`desk/nes/src/launch.s`'s `find_core` looks for a `name.rbf` next to the ROM,
or falls back to the EverDrive's stock core for the cartridge mapper. It then
loads that core into the N8 Pro's FPGA. The README says "the FPGA is heavily
utilized to make this thing work well". So a Game Boy game in that window can
only run as a Game Boy implemented in the cartridge's FPGA, with the NES left
to show the picture and read the controller. I could not find which core that
is: Krikzz's own Game Boy hardware is a separate cartridge (the EverDrive GB
X7).

**The RP2350 in the project is the keyboard and mouse adapter.** It is on the
controller cable, not on the cartridge bus.

So the video is very likely real, but it is the section 7 case of a
coprocessor in the cartridge doing the emulation. It is not evidence that a
1.79 MHz 6502 can emulate a Game Boy at full speed, and it does not change
the 5150 answer.

## 8. If it is pursued anyway: two measurements, in this order

Both are cheap, and both replace predictions above with facts.

1. **Busy fractions, on the host, with no 8088 involved.** Instrument
   Peanut-GB (it is one C header) to count per frame: active M-cycles against
   `HALT` and `LY`-poll M-cycles, MBC switches, VRAM tile writes, and
   mid-frame register writes to `SCX`, `SCY`, `LCDC`, `WX` and `WY`. Run it on
   a handful of candidate games. This decides whether section 6's light-game
   corner contains any real games. If none come in under ~20% busy, the 5150
   question is closed.
2. **The dispatch, on MartyPC.** Write section 4.1's threaded dispatch and
   about twenty handlers in NASM, run a synthetic instruction mix, and read the
   guest cycle counter. That turns "~35-45 clocks per M-cycle" into a measured
   number, before anyone commits to the whole design. The Apple II plan was out
   by six times for lack of exactly this.

**If the honest target is "full speed somewhere in os8088's machine range",
the answer is yes, on a 286, and the design above is the way there.** On a
stock 5150 the best outcome is a slow but working Game Boy that plays very
light games at full game speed with a low frame rate. Ordinary games would run
at a third of full speed or less.
