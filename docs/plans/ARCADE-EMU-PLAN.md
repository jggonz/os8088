# ARCADE - an arcade-board emulator: Space Invaders first, then Pac-Man

A PROPOSAL, not a contract (docs/README.md). It follows
docs/plans/EMU-SURVEY-286.md, which put "Space Invaders and other 8080 arcade
boards" and "the Pac-Man arcade board" in its tier A. The working name is
**ARCADE** (`apps/arcade/`, `ARCADE.O88`); the owner may rename it before
wave 1.

**Measured and predicted figures are labelled throughout.** The Space
Invaders figures in section 2 were MEASURED, by running the real ROM in a
Python model of the 8080 and the board (`tools/arcsim.py`, in its
wave 0 form; wave 1 grows it into the oracle). Every host-speed figure is a PREDICTION,
worked out from RealDMG's measured costs (REALDMG-SPEC 16.6 and 16.10).
None of them has been run on a 286 or an 8088 yet.

---

## 1. Decisions

### 1.1 Space Invaders first

| | Space Invaders (Taito/Midway, 1978) | Pac-Man (Namco/Midway, 1980) |
|---|---|---|
| CPU | **8080 at 1.9968 MHz** | Z80 at 3.072 MHz |
| load against the DMG (survey section 3) | **~0.6** (MEASURED: 263k instr/s, section 2) | ~0.9 (predicted) |
| how close it is to a core we have | **The SM83 is an 8080 with parts removed and added. Most of rdggen.py's handlers carry over unchanged**, and the 8080's PSW is **bit for bit what LAHF loads** (section 4.2) | needs the full Z80: IX/IY, the CB/ED/DD/FD prefixes, the alternate registers, IM 2 |
| video | **1bpp bitmap**, 7KB at 2400-3FFF, monochrome with a colour overlay | 28x36 tiles plus 8 sprites of 16x16, 2bpp through two colour PROMs |
| sound | discrete circuits, triggered by bits on two ports | Namco WSG: 3 voices of 4-bit wavetable |
| ROM | **publicly downloadable from archive.org** (section 9) | **not in any public archive.org item we found**: `arcade_pacman`, `arcade_puckman` and `arcade_mspacman` are dark. The owner supplies it, as the survey said |
| reference material | the most-documented arcade board there is: a full commented disassembly, plus the 8080 CPU tests that run under CP/M | well documented. ZEXDOC and ZEXALL already gate RUNCPM's Z80 |

Space Invaders wins on every row. **It is also the right order for the
code:** the 8080 core is a subset of the Z80 core, so Pac-Man reuses the
generator wave 1 builds, along with its oracle and its host. Pac-Man adds
prefixes, a tile and sprite composer, and a wavetable mixer.

### 1.2 One package, cores and boards as parts

This is an emulator with **cores for several games**, not a separate
package for each game. The pieces are:

| piece | what it is | ships as |
|---|---|---|
| **host** | the window, FSX, input mapping, the DIP-switch dialog, pacing, sound out, save states, the game list | `ARCADE.O88`'s resident image |
| **CPU core** | a generated 8086 interpreter, one per guest CPU family: `8080`, later `Z80`, later `N88` (native 8088, section 8.2) | a PART of `ARCADE.O88` (SPEC.md 20.12), loaded when the game list picks a board that needs it |
| **board driver** | the memory map, the I/O ports, the interrupt schedule, video, sound triggers and inputs of one board FAMILY (Midway 8080 B&W, Namco Pac-Man, ...) | a part, like a core |
| **ROM set** | the board's ROMs in a fixed layout, checked by hash, plus that set's AOT image (section 4.5) | a part assembled at build time from a FETCHED ROM, never committed. APPLE2's ROM uses the same mechanism (`tools/getapple2rom.py`) |

A board driver talks to a core through RealDMG's contract: far vectors in a
state block for events, I/O and slow memory (REALDMG-SPEC 5.2). That way the
core never needs to know the host's segment, and the same core runs under
the bare harness, the package and the AOT translator.

### 1.3 Shared source, not copies

The 8080 and Z80 generator is RealDMG's method. Where a handler is the same
(`LD r,r'`, the ALU on registers, `INC`/`DEC`, `JP`/`CALL`/`RET` and their
conditions, `PUSH`/`POP`), **the template is SHARED SOURCE**. `rdgtmpl.py`
is factored so that both generators import it, following the WEAVE-SPEC 1.2
rule. It is not copied into `apps/arcade/`. The recompiler's template
mechanism (REALDMG-SPEC 16.1), the block cache (16.2) and the static walk
(20.2) are shared in the same way, as soon as a second user exists. The
first wave that needs one of them does that factoring, and checks with
`cmp` that RealDMG's build comes out byte-identical.

### 1.4 The target machines

- **The primary target is the owner's Victory 286 at 16 MHz**
  (docs/FIELD-MACHINES.md), running `286-victory` on 86Box. "Full speed"
  means GAME speed, as RealDMG defines it: the 60 Hz interrupts and the
  game logic keep time, and the display may drop frames.
- **The 8088 at 4.77 MHz gets as much speed as possible**, measured on
  MartyPC by guest cycles. Every tier the 286 uses (interpreter, block
  recompiler, static AOT) is offered on the 8088 too, as RealDMG's 16.10
  found they should be. The 8088 also gets one more option, **Sideways**
  (section 5.4), which puts each guest byte straight on the screen.
- **Adapters:** VGA, Hercules and CGA, as SPEC.md 39 requires of every
  package, with each one's compromise stated (section 5.3).
- **kern_small** refuses the package with a reason: a 64KB core segment
  plus a 16KB view does not fit in 128KB beside the desktop.
- **kern_mega** runs the shipped build low and plain (SPEC.md 20.18), and an
  arm is added only if a measurement asks for one.

---

## 2. Space Invaders, MEASURED in the Python model

The real `invaders.h/g/f/e` set ran in the scratch model: 1,500 frames, two
interrupts a frame (RST 1 at mid-screen, RST 2 at VBlank), 16,666 8080
T-states between them. The script inserts a coin at frame 300, presses
1P Start at frame 360, then alternately fires and moves left and right for
1,080 frames of play. The final frame's screen was decoded from VRAM and
checked by eye: a real game, score 0070, with the aliens marching and the
shields chipped.

| per frame | attract (240 frames) | **play (1,080 frames)** |
|---|---|---|
| 8080 instructions | avg 3,614, max 3,834 | **avg 4,365, p90 4,479, max 4,519** |
| VRAM writes | avg 2 | avg 95, p90 311, max 496 |
| VRAM writes that CHANGE the byte | avg 1 | **avg 20**, p90 31, max 321 |
| dirty 8x8 cells (section 5.1) | avg 0 | **avg 5, p90 9, max 47** |

- **Average instruction: 7.55 T-states.** That is 263k instructions a
  second, 0.58 of the DMG's ~450k. The survey's ~0.6 holds.
- **The CPU never idles**: there is no HALT, and the main loop runs flat out
  between interrupts. **30.1% of instructions are in the two interrupt
  handlers and 69.9% in the main loop.**
- **One loop is 34% of all instructions**: 15F9-1601,
  `MOV A,M / ANA A / JZ / INR C / INX H / DCR B / JNZ`. It counts the live
  aliens, 55 bytes, and stores the result to 2082. It is called **3.85
  times a frame** from the main loop, although once a frame would do. A
  second scan loop at 15C7 is another ~12%, and the block copy at 1A32 is
  ~5%.
- **I/O is cheap**: about 30 shift-register OUT/IN pairs a frame
  (`OUT 4`, `IN 3`), about 9 watchdog writes (`OUT 6`), and about 8
  sound-latch writes (`OUT 3`/`OUT 5`, mostly unchanged values).
- **The game never wrote to ROM** in those 1,500 frames. The write guard
  (section 4.3) is there for crashes, not for play.

**What this tells us about the design:**
1. The picture is nearly free. On an average frame only 20 bytes of VRAM
   change, which is 5 cells. A design that draws only what changed
   (section 5) spends almost nothing on video, and the CPU is the whole
   problem.
2. Half of that CPU time is in two scan loops, so **loop idioms**
   (REALDMG-SPEC 16.4's `add_ff`/`cpy_ff` mechanism) are the biggest
   single lever, larger than the recompiler's general speed-up.
3. The 30/70 split, with a main loop that re-does the same work about four
   times a frame, means an idle rule (section 4.6) could do more again.
   It is also the riskiest lever, so it comes last and only if
   measurements ask for it.

## 3. The budget, PREDICTED

All figures below are host clocks per guest frame, at 60 Hz.

| host | clocks a frame | interpreter | block recompiler | recompiler + the two loop idioms |
|---|---|---|---|---|
| **286 at 16 MHz** | 266,667 | 4,365 x ~50 = 218k, **82%** | 4,365 x ~28 = 122k, **46%** | ~2,350 x ~28 + idioms ~6k = 72k, **27%** |
| 286 at 8 MHz | 133,333 | 164%, too slow | 92% | **54%** |
| **8088 at 4.77 MHz** | 79,500 | 4,365 x ~100 = 437k, **18% of real time** | 4,365 x ~43 = 188k, **42%** | ~2,350 x ~43 + ~12k = 113k, **~70%** |

The per-instruction costs are derived from RealDMG's MEASURED per-M-cycle
costs (286: 26.2 clocks interpreted and 13.95 recompiled; 8088: 50.96 and
21.74), times the ~1.9 M-cycles an average 8080 instruction is worth. Video
adds about 1.5k clocks on an average frame and about 15k on the worst
(section 5.1).

**Taken at face value, the predictions say:**
- **The 286 has room to spare**, even interpreted. The recompiler and the
  idioms are not needed for full speed; they buy headroom for the 8 MHz
  machines and for Pac-Man.
- **The 8088 could reach roughly 70% of real time**, with the
  idioms carrying most of that. Full speed there would need the idle rule
  (section 4.6), and that figure is the least certain in this plan.
  Wave 2 replaces it with a MartyPC cycle count.

## 4. The CPU core

### 4.1 Register map, which is RealDMG's

| 8080 | 8086 | |
|---|---|---|
| A | AL | |
| F (PSW low byte) | AH | **in LAHF's layout, and that IS the 8080's** (4.2) |
| BC | CX | |
| DE | DI | B, C, H and L are byte registers. D and E go through the same DI shuffles rdggen.py uses for the SM83's D and E |
| HL | BX | `MOV r,M` is `mov r,[bx]` with DS = the view |
| SP | SP | **SS = the view**, so an 8080 PUSH is an 8086 push. Host code never runs on this stack (REALDMG-SPEC 5.2) |
| PC | SI | DS = the view. The 8080 boards have no banking, so there is no ROMX |
| - | BP | cycle countdown to the next event, in **8080 T-states** (the SM83 counts M-cycles) |
| - | DX | dispatch: `mov dh,[si] / inc si / jmp dx`, with DL = 0 |

### 4.2 The flags come free

LAHF loads `SF ZF 0 AF 0 PF 1 CF` into AH. **The 8080's PSW is
`S Z 0 AC 0 P 1 C`: the same bits in the same places, including the
constant 1 in bit 1.** The 8086 was designed so that 8080 code could be
translated to it, and this is that design showing through. So `PUSH PSW`
is `push ax` after one LAHF, and `POP PSW` is `pop ax` / `sahf`. The SM83
needs `RDG_F2AH`/`RDG_AH2F` for the same instructions; the 8080 needs no
translation. Two details differ from the 8086, and the generator must get
both right:
- **The 8080 sets AC on ANA** from the OR of bit 3 of the two operands. The
  8086 leaves AF undefined after AND.
- **The 8080's subtract sets AC as "no half-borrow"**, the inverse of
  the 8086's AF, which means a borrow. INR and DCR handle AC on the same rule.

The CPU tests settle both (section 7.1). Space Invaders itself only reads
AC through DAA, in its score code.

### 4.3 Memory: one view, a write guard, a dirty map

The view is a 64KB segment that holds the board's address space in place:
ROM at 0000-1FFF, RAM at 2000-23FF and VRAM at 2400-3FFF. Reads are plain
`mov`s. **Every write goes through one high-byte test**: below 20h it is
dropped (ROM), from 24h up it also marks the dirty map (section 5.1), and
anything else is a plain store. A write that does not change the byte
marks nothing: 79% of play's VRAM writes rewrite the same value. Mirrors
above 4000h are folded in the slow path by the board's mask. The exact
mirror and mask behaviour is copied from MAME's `mw8080bw` memory map in
wave 1 and checked in the model, not assumed here.

### 4.4 Interrupts

The board raises RST 1 and RST 2. The core takes an interrupt only between
instructions, at an event, and only when the 8080's INTE flag is set (EI
sets it, and acknowledging an interrupt clears it). The board driver
schedules both interrupts as events at fixed T-states, as `rdg_sched`
does. Pac-Man's IM 2 (vector byte from `OUT 0`) uses the same event shape
in wave 5.

### 4.5 The three tiers, all of them RealDMG's

1. **Interpreter**: generated, with one handler at opcode x 256 (the 8080
   has 244 opcodes and needs no CB half).
2. **Block recompiler**: REALDMG-SPEC 16's templates, cache, chaining and
   guards, plus **the loop idioms, `cnt_nz` first** (the 15F9 loop). This
   is the same mechanism as 16's `add_ff`/`cpy_ff`: the loop runs in one
   step when BP covers all of it, and runs normally otherwise.
3. **Static AOT, the natural default here.** An arcade ROM is small (8KB
   for Invaders, 16KB for Pac-Man), fixed and never banked, so
   `tools/gbaot.py`'s walk-and-translate (REALDMG-SPEC 20) covers it more
   completely than it covers any cartridge. **The AOT image is built at
   build time from the fetched ROM** and becomes part of the ROM set's
   part. Like the ROM it is derived from, it is never committed.

### 4.6 The idle rule (a lever, conditional)

The main loop recomputes the alien count about 3.85 times a frame. If one
main-loop pass that changes no RAM and no I/O state could be detected, the
core could skip to the next interrupt event, as REALDMG-SPEC 8 does for a
spin-wait. That would bring the CPU cost close to the interrupt handlers'
30%. **It is risky**: a pass that writes the same values is not proof
that the next pass will. It is built only if wave 2's 8088 measurement
falls short, and only with a DIP-style setting to turn it off and the
model's frame-by-frame screen comparison (section 7.2) as its gate.

---

## 5. The picture

### 5.1 By damage, in 8x8 cells

VRAM is 224 rows of 32 bytes, unrotated: each byte is 8 pixels along one
guest scanline. **A cell is 8 guest rows by one byte, which is 256 bytes
of address space, so its index is `(H - 24h) x 32 + (L & 31)`**: one
subtract, one shift, one mask and one OR in the write path. There are 896
cells. Writes set a byte in a 896-byte map and append the cell to a list
if it is new. At VBlank (RST 2's event) the host draws each listed cell
and clears the map behind it.

### 5.2 Rotation is a transpose, done once a cell

The cabinet's monitor is turned 90 degrees, so a guest byte (8 horizontal
guest pixels) becomes 8 VERTICAL screen pixels. **Turning an 8x8 cell is
an 8x8 bit transpose**, which yields 8 bytes, one per screen row. On every
1bpp-packed or planar target, each of those bytes is exactly one aligned
byte of screen. Predicted at ~200-300 clocks a cell on a 286 (a table
transpose or 64 RCLs). At 5 cells a frame on average and 47 at worst,
that is 1-2k and 10-15k clocks.

### 5.3 Per adapter

| adapter | mode | fit | colour |
|---|---|---|---|
| **VGA/EGA, full screen** | `FSXM_VGA12`, 640x480x16 planar | **224x256 portrait at 1:1**, centred and byte-aligned. 2x vertical scaling would need 512 lines, which is too many | **The overlay costs nothing per frame.** The game draws only into plane 0. Planes 1-2 are filled ONCE at entry with the cabinet's cellophane bands (red at the top for the UFO, green at the bottom for the shields, the player and the lives). The DAC maps index (band x 2 + pixel), so a set pixel takes its band's colour with no per-frame work. `OSAPI_FSX_*` owns the mode and the palette |
| **Hercules, full screen** | `FSXM_HERC`, 720x348 | **224x256 portrait at 1:1**, the same 8 bytes a cell into four 90-byte banks | mono, as on a real B&W Invaders |
| **CGA, full screen** | `FSXM_CGA640`, 640x200 | **256 rows do not fit in 200.** Portrait drops 56 of every 256 screen rows through a fixed table, which is lossy and can eat a 1-pixel bullet. **The CGA default is therefore Sideways** (5.4): 256 wide, with 224 guest rows folded onto 200 by the same table, but along the axis bullets travel | mono |
| **windowed, any adapter** | the desktop | a 224x256 content box (or Sideways' 256x224) | each damaged cell's 8 bytes become one `OSAPI_GFX_BLIT1` per band run, with the cells coalesced into runs as RealDMG's composer does (REALDMG-SPEC 13). On a mono desktop it is plain B&W |

### 5.4 Sideways: the 8088's video path

When the picture is shown unrotated (the cabinet "lying down"), **a guest
VRAM byte IS a screen byte**, apart from bit order, which a 256-byte
reverse table fixes. Sideways therefore needs no dirty map, no transpose
and no VBlank pass: each guest VRAM write is stored straight through to
the screen, with the write-through done inside the write guard's VRAM
branch. That costs ~20-30 clocks a changed write, and nothing on frames
with no changes. It is offered on every adapter, and is the default on
CGA and on an 8088.

---

## 6. Sound

Space Invaders' sound board is discrete analog circuitry, triggered by
bits on `OUT 3` (UFO, shot, player death, invader death, extra life) and
`OUT 5` (the four fleet-march notes, the UFO hit). Emulators usually play
recorded samples. **We do not ship anybody's recordings.** Instead:

- **Sound Blaster** (the owner's PicoMEM SB): short PCM clips SYNTHESISED
  by a committed host generator (`apps/arcade/arcsfx.py`) from the circuits'
  documented behaviour. The clips are built deterministically, on the
  artwork-as-source rule (CLAUDE.md "Building an app in waves", item 5).
  They are mixed through SOUND.DRV's PCM path (`apps/os88pcm.inc`).
- **PC speaker**: the fleet-march notes are four low tones, which suits the
  speaker. The shot and the explosions are `os88spkfx` sweeps and noise,
  with the march taking priority.
- **Pac-Man's WSG** (wave 6): three voices of 32-sample 4-bit waveforms
  from the sound PROM, mixed into a PCM buffer once a frame for the SB. On
  the speaker, the loudest voice's pitch is played as a tone.

---

## 7. Verification

### 7.1 The CPU: two oracles

- **The 8080 test programs** (`8080PRE`, `8080EXM`, `TST8080`, `CPUTEST`)
  run under a minimal CP/M BDOS shim in the bare harness (`tests/arccore/`,
  on `tests/rdgcore/`'s pattern). They are fetched at pinned hashes by a
  `tools/getarctest.py`, never committed. `8080EXM` is the exhaustive one
  and exercises AC; the CRCs it prints are the gate.
- **`tools/arcsim.py`**, the scratch model grown up, as a reference: it
  runs the same ROM with the same scripted inputs and emits a trace (PC,
  registers, PSW per instruction) and a frame digest. A divergence
  bisector in the style of `tools/os88dosdbg.py`'s `diff` reports the
  first instruction where the core and the model disagree.
- **Z80 (wave 5)**: ZEXDOC and ZEXALL, already the gate for RUNCPM
  (SPEC.md 74), run under the same shim.

### 7.2 The machine

- **Frame digests.** The emulator under MartyPC, given the same input
  script, must match `arcsim.py`'s VRAM digest at frames 300, 600, 900 and
  1,500. An interrupt off by one shows here first.
- **The picture.** A capture of the emulated screen must match
  `arcsim.py`'s PBM through the adapter's documented mapping: exact on VGA
  and Hercules, and through the row-drop table on CGA.
- **Each row is broken on purpose and watched go red**
  (docs/WRITING-TESTS.md): swap AC's rule, drop RST 1, skip a dirty mark.

### 7.3 The speed

- **The 286** is measured on 86Box `286-victory` (docs/86BOX.md): game
  frames a second against 60, both windowed and full screen. Then a field
  run on the owner's machine (docs/FIELD-MACHINES.md).
- **The 8088** is measured on MartyPC by guest cycles per guest frame, in
  play with the scripted inputs.

### 7.4 Rows

New rows are registered in the right tier with measured `secs`. All of
them are `soak` rows, because they are about one package:

| row | what it checks |
|---|---|
| `arccpu8080` | the 8080 test programs |
| `arcinvaders` | frame digests, adapters, DIP switches, exit restoring the desktop |
| `arcspeed` | the cycle budget, on the 8088 |

Pac-Man adds `arccpuz80` and `arcpacman`. Per the owner's rule, waves NAME
these rows and run host checks. The deduplicated union and `test-full` run
once, after the last batch.

---

## 8. The cores beyond these two

ROM availability was checked against archive.org's public `arcade_*` items
on 2026-11-03. "dark" means the item exists but cannot be downloaded.

### 8.1 Board families, in order of promise

| board family | games | CPU and core | why it is promising, or not | ROM |
|---|---|---|---|---|
| **Midway 8080 B&W** (MAME `mw8080bw`) | Space Invaders, **Lunar Rescue**, Space Invaders Part II, **Gun Fight**, **Sea Wolf**, Boot Hill, 280 ZZZAP, Clowns, Space Invaders Deluxe | 8080, wave 1's core | **The same board with different port maps.** Each one is a board-driver TABLE, not new code: inputs, which sound bits, the colour PROM on the Part II/Deluxe colour boards | invaders, lrescue, invadpt2, gunfight and seawolf are **public**. The `invaders.zip` set also carries ~40 clones |
| **Namco Pac-Man** | Pac-Man, Ms. Pac-Man, Pac-Man Plus, Jr. Pac-Man, **Pengo** (Sega, with almost the same video), Lizard Wizard | Z80 | One video and WSG model covers the family. Pengo's original ROMs are encrypted, but the clone `pengo2u` is not, and Ms. Pac-Man's extra board is a patch overlay | pacman, mspacman and puckman are **dark**. **pengo is public**, with `pengo2u` included |
| **Gottlieb System 80 GG-III** | **Q\*bert**, Reactor, Mad Planets, Krull, Q\*bert's Qubes, Mr. Q | **an 8088 at 5 MHz**, so a native core (8.2) | **The one arcade CPU this machine IS.** On a 286 the CPU work is nearly free, and on a real 8088 it runs at about 95% of its own speed. The cost is the 4bpp tile and sprite video, and the 6502 sound board (treated as samples or skipped) | qbert, reactor, mplanets and krull are **public** |
| **Universal Lady Bug** | Lady Bug, Snap Jack, Cosmic Avenger | Z80 at 4 MHz | Tiles, sprites, and two SN76489s that map well onto the PC speaker or SB | ladybug is **public** |
| **Stern Berzerk** | Berzerk, Frenzy | Z80 at 2.5 MHz | A 1bpp bitmap like Invaders, plus a colour RAM. The speech chip would be samples | berzerk is **public** |
| **Galaxian/Scramble** | Galaxian, Moon Cresta, Scramble, Frogger | Z80 at 3.07 MHz | Per-column scroll, a starfield and bullets. Tier B, after Pac-Man's composer exists | galaxian, frogger and scramble are dark |
| **Donkey Kong** | Donkey Kong, DK Jr. | Z80 at 3 MHz, plus an i8035 for sound | The survey's tier B. The sound CPU would be samples | dark |
| **Atari 6502 boards** | Centipede, Millipede, Missile Command | 6502 at 1.5 MHz | Waits for the survey's recommendation 1, a generated 6502 core | centiped is **public** |

### 8.2 The native-8088 core: Q\*bert as a probe (conditional)

Q\*bert's ROMs were fetched and disassembled while writing this plan. The
reset code is `mov sp,1FF0h / xor ax,ax / mov es,ax / mov ds,ax /
mov ss,ax`, and the ROM's top bytes hold `jmp 0000:A000`. **The whole
program lives flat in one 64KB space with every segment register at 0**,
and a scan of the ROM found only those three segment-register loads. So
guest code can run **natively** in a host segment G, provided that:

- the segment loads and far jumps are patched to G, which a host-side
  static pass (like gbaot.py's walk) does at build time;
- `INT n` and the IVT stay the guest's, not physical page 0. The board's
  interrupt is delivered by the host as `pushf / call far` through G's
  own vector table;
- memory-mapped I/O (inputs, the sound latch, video registers) is
  answered by filling the inputs before each slice and comparing the
  outputs after it. The sound latch's writes are trapped by patching
  them, because two writes inside one slice would otherwise be lost;
- host interrupts push onto the guest's stack, as the real board's own
  interrupts did. Wave 7 measures the depth os8088's ISRs need below a
  guest SP.

That is the WonderSwan idea from survey section 3, without kern_mega,
because Gottlieb's code never names a segment other than 0. **It is a
probe and not a promise**: one wave to boot Q\*bert's attract mode
natively and measure it, and a decision there.

---

## 9. ROM sources, pinned

**No ROM is ever committed** (CONTRIBUTING.md 6). `tools/getarcrom.py`
fetches each set at a pinned item and archive hash, checks every member by
SHA-256 and CRC32, and assembles the ROM-set part. A mismatch is a hard
failure. The on-demand `arcadedisk` target is not in `all`, and
`make clean` spares the fetch cache. This is `make apple2rom`'s posture.

**Space Invaders**, `https://archive.org/download/arcade_invaders/invaders.zip`
(SHA-1 `8d2d41725398c33df15afa28d5a2e5350b7290c6`, 225,559 bytes, fetched
2026-11-03; it also contains ~40 clone sets as folders). The parent set's
four members, at the root of the zip:

| file | maps at | size | CRC32 (matches MAME) | SHA-256 |
|---|---|---|---|---|
| `invaders.h` | 0000 | 2048 | 734f5ad8 | `1dfb19b1eeeac5b91ff4861ab331e183500785a3c958a6fa30aac0f37a7bbc65` |
| `invaders.g` | 0800 | 2048 | 6bfaca4a | `ac7031b046274d4cd963cebf02f6e0ea50104e2a29681c387b1cb27e1e3e343d` |
| `invaders.f` | 1000 | 2048 | 0ccead96 | `5c8e573ebde9a673d4e7809d3f36aaa20d9062f3616d025d85c3f04fcfe11d72` |
| `invaders.e` | 1800 | 2048 | 14e538b0 | `0a97ed62e2063c13e373a9a61ea3d8fbb701e414af05953e16f823fd879d5f49` |

**Space Invaders Part II** (`arcade_invadpt2/invadpt2.zip`, SHA-1
`228cadc2b54fcedc131a8a46cae6818bae255b1a`) and **Lunar Rescue**
(`arcade_lrescue/lrescue.zip`, SHA-1
`3da8bff250a9665bb5f7ce2b7700dcaa579cbf84`), fetched 2026-11-04: every
chip's CRC32 is MAME's, and `tools/getarcrom.py` pins each by SHA-256.

**Pac-Man** is supplied by the owner (`--from <dir>`), and checked against
MAME's `pacman` (Midway) or `puckman` (Namco) CRCs. **Pengo `pengo2u`**
(`arcade_pengo`, public) is the fallback for proving the Namco-family
video and Z80 core before the owner's ROM arrives.

Fetching is **not** part of `make` by default. It runs only behind the
on-demand targets, as `make apple2rom` does.

---

## 10. Waves and stories

The waves follow CLAUDE.md's "Building an app in waves". The contract
moves out of this plan into **`docs/ARCADE-SPEC.md`** (the REALDMG-SPEC
precedent) at wave 1. **`docs/HOW-ARCADE-WAS-BUILT.md`**, the build diary,
is started at wave 1 and kept as the work happens. One draft PR from
`arcade-emu` carries the waves. Each wave verifies on the host and NAMES
its rows; the soak union and `test-full` run once, after the last wave.

### Wave 0: this plan
- [x] Branch `arcade-emu` from `emu-survey`.
- [x] Measure Space Invaders' load in a model, and record it (section 2).
- [x] Confirm and pin a public ROM source for Invaders, and check
      availability for the other families (sections 8 and 9).
- [x] Disassemble Q\*bert's reset code to see whether a native core is
      possible (section 8.2).

### What changed when it was built (2026-11-03)

Waves 1 to 4 were built in one round, and three of this plan's decisions
did not survive contact with the measurements. docs/ARCADE-SPEC.md is what
the code does now; docs/HOW-ARCADE-WAS-BUILT.md is how it got there.

1. **Ahead-of-time translation replaced the interpreter.** Section 4.5's
   "static AOT, the natural default" became the ONLY tier: an 8KB ROM that
   is never banked translates whole, with no dispatch at all, and the
   8080's flags stay in the 8086's FLAGS between instructions. There is no
   generated interpreter and no block recompiler, so there was nothing to
   factor out of `rdgtmpl.py` (section 1.3): the SM83's flag templates are
   not the 8080's, and the translator emits 8086 text directly.
2. **The interrupts run on the PC's clock, not the 8080's** (ARCADE-SPEC 5).
   Space Invaders keeps its game time in its two interrupt handlers, so a
   slow host keeps game speed and loses only main-loop passes. That is
   section 4.6's idle rule obtained for free, and it is why the 8088
   reaches the board's frame rate.
3. **Sideways was not needed** (section 5.4): drawing only the cells a
   store CHANGED, the transpose costs a few cells a frame.

### Wave 1: the contract, the oracle, the 8080 core
- [x] `docs/ARCADE-SPEC.md`.
- [x] `tools/arcsim.py`: `stats`, `seeds` and `digest` (PBM and VRAM SHA-1).
- [x] `tools/getarcrom.py` with the section 9 pins (`make arcaderom`).
- [x] The 8080 translator, `apps/arcade/arcaot.py`, with liveness for the
      flags and three loop idioms.
- [x] **The translation matches arcsim.py instruction by instruction**:
      `tests/arclock.py` (row `arclock`), 2,293,923 instructions over 600
      frames under Unicorn, and red when an idiom is broken.
- [ ] `tools/getarctest.py` and 8080EXM under a CP/M shim. The lockstep
      covers what Space Invaders executes; the exhaustive CPU test would
      cover what it does not, and would pin AC (ARCADE-SPEC 3.5).

### Wave 2: the board, and the number that decides the rest
- [x] The Midway 8080 board: the shift register, the watchdog, inputs, RST 1
      and RST 2. The memory mirrors were not needed: MEASURED, nothing above
      3FFF is touched.
- [x] **MEASURED on MartyPC's 5150 in play** (`tests/arcspeed.py`, row
      `arcspeed`): 59.62 frames a second against the board's 59.54, none
      dropped, 1.2 to 1.5 main-loop passes a frame against a real 8080's
      3.85. On 86Box's 16 MHz 286 (`at286_vga`, not cycle-accurate): no
      drops and about twelve times the 8080's main-loop passes.
- [ ] Machine > DIP Switches (the DIPs are fixed at 3 bases, extra at 1500).

### Wave 3: the package, playable
- [x] `ARCADE.O88`: the window, Game > Play and Reset, About, keys.
- [x] The changed-cell transpose, full screen on VGA (with the overlay
      bands), Hercules and CGA.
- [x] Sound on the speaker, through `OSAPI_SND_TONE`. NOT yet listened to:
      no row captures it.
- [x] The Sound Blaster clips (`apps/arcade/arcsfx.py`, synthesised),
      mixed into a stream; and FM on an AdLib (ARCADE-SPEC 8, row
      `arcsound`).
- [x] `OSAPI_ABOUT_SET`, `OS88_REGION_MOVABLE`, Esc restoring the desktop.
- [x] `arcadedisk` in the four geometries; a `local` line in RETIRED.txt.
- [x] A game list, once there is a second board: Game > Space Invaders, Pac-Man.
- [ ] A picture in the window, not only full screen.

### Wave 4: speed - done as part of waves 1-3, because the 8088 asked
- [x] Loop idioms: `copy` (REP MOVSB to RAM), `count`, `scan`.
- [x] A VRAM store compares first and lists its cell once; the host draws
      the list, with an unrolled put per adapter.
- [x] `IN 3` inline; four key polls a frame instead of nine; one PIT read a
      run; poll points weighted by their loop's size.
- [ ] An interpreter for a PC the translation lacks. Today a miss stops the
      game with "Arcade: no code at XXXX"; seeds from the model and
      speculative walks of the ROM's data cover every PC the model and
      MartyPC have reached.

### Wave 5: the Z80 core
- [x] The Z80 translator, `apps/arcade/arcz80t.py` (decoder `z80dec.py`):
      prefixes, IX/IY and the alternate set in view cells, IM 2, P/V from
      the 8086's OF where a reader needs it, N not kept (DAA picks add or
      subtract from the instruction that reaches it), block moves and
      CPIR/CPDR as helpers that never touch the guest stack.
- [x] `tools/arcz80.py`, a Z80 model, and `tests/arclock.py --board
      pacman`: 4.2 million Z80 instructions over 600 frames agree
      (row `arclockpm`).
- [ ] ZEXDOC passes. The lockstep proves what Pac-Man executes, not the
      whole instruction set.

### Wave 6: Pac-Man
- [x] The Namco board (`apps/arcade/arcpm.inc`, `tools/arcpac.py`): the
      memory map, inputs at 5000/5040, the IM 2 vector, the latches; the
      boot-time ROM and RAM tests pass.
- [x] The composer by damage, VGA mode 12h only: 448x432 (each pixel
      doubled across, every other line doubled down), the palette through
      the DAC, tile writes compose their own cell, sprites old-and-new
      boxes of at most 4x4 tiles, a per-tile plane cache and a screen
      shadow so only changed bytes reach VRAM. Hercules and CGA refuse,
      with the reason.
- [x] **Full speed on 86Box's 16 MHz 286 VGA, no frames dropped in play**
      (the owner's report confirmed Invaders the same way). Not cycle
      accurate; the 5150 is not a Pac-Man target.
- [x] WSG on the speaker: the loudest voice as a tone. NOT yet listened to.
- [x] WSG on the SB (every voice mixed at 10,989 Hz) and on FM.
- [ ] `pengo2u` was not needed: the owner supplied the Pac-Man ROM, and
      `make arcaderom-pacman GETPACFROM=<zip>` checks it against MAME's
      hashes.

### Wave 7: more boards (each conditional)
- [x] The 8080 family as tables (a board's WIRING, ARCADE-SPEC 5.3):
      **Space Invaders Part II and Lunar Rescue** (Taito's colour boards:
      ROM at 4000h too, a colour a cell from a map PROM on VGA), each at
      the board's full 59.5 frames/s on the 5150 - Lunar Rescue only
      after spin loops became a HLT (ARCADE-SPEC 3.7). Every board's parts
      are now lazy, so one board is in memory at a time.
- [x] **Gun Fight and Sea Wolf**: Midway's landscape boards, their own
      ports (ARCADE-SPEC 3.9), a landscape drawer (4.6), two gunmen at one
      keyboard with aim levers, a 30-position periscope, Sea Wolf's lamps;
      the board's full rate on the 5150. Their main loops never wait;
      treating their idle returns as a HLT was measured and refused
      (ARCADE-SPEC 3.8).
- [x] The native-8088 probe: Q\*bert's attract mode (section 8.2) - and,
      the measurement saying yes (the game a tenth of a 4.77 MHz 8088),
      **Q\*bert as a board**: its program run untranslated through the N88
      core with a patch list of 17 checked lines, VGA only, the board's full
      61.42 frames/s on the 286 and on the 8088 too, where the PICTURE skips
      frames instead (ARCADE-SPEC 10). Silent: its sound is a 6502 board of
      its own, not yet done.
- [ ] Lady Bug or Berzerk on the Z80 core.

---

The road from here to a release - a package with no ROM in it, ROM sets
the user imports, a frontend - is docs/plans/ARCADE-RELEASE-PLAN.md.

---

## 11. Risks

| risk | what would show it | what then |
|---|---|---|
| The 8080's AC differs from the 8086's AF in more cases than 4.2 lists | 8080EXM's `aluop` CRCs | Compute AC in the handful of handlers that need it. Only DAA reads it |
| The 8088 stays well under real time even with the idioms | wave 2 and wave 4 measurements | Ship the 8088 as "playable, slow", as RealDMG ships Kirby on the 286. Do not cut the 286 design down to suit the 8088 |
| The model is wrong in the same way as the core | the digests agree, but MAME disagrees about how the game plays | Check a few frames against MAME by eye in wave 2. The CPU tests are independent of both |
| The CGA row-drop table hides bullets in portrait | looking at the screen | Sideways is the CGA default. Portrait is offered with the caveat stated |
| The interrupt timing (RST 1 at mid-screen) drifts against the picture | the UFO and shields tearing in captures | Measure against `arcsim.py`'s frame digests, which pin the T-state of each interrupt |
| Pac-Man's ROM never arrives | - | Pengo `pengo2u` proves the Namco core. Pac-Man stays a supply question, not a code one |
| The native-8088 core needs more patching than three segment loads | the static pass's report | It is a probe, so stop there and record why. (It needed the three loads, 14 port stores moved and two waits made yields: ARCADE-SPEC 10.2) |
