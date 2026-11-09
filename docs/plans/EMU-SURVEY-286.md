# What else a 16 MHz 286 could emulate (survey, 2026-11-03)

A PROPOSAL, not a contract (docs/README.md). Every speed below for a machine
we do not emulate yet is a **PREDICTION** made by arithmetic from RealDMG's
measurements, not a measurement. The target is the owner's Packard Bell
Victory 286 (docs/FIELD-MACHINES.md): 16 MHz AMD 286, PVGA1A VGA, 4MB, a
PicoMEM with a Sound Blaster.

## 1. The yardstick: what the 286 already does

| package | guest CPU | core | measured on a 286 |
|---|---|---|---|
| RealDMG (Game Boy) | SM83, 1.05M M-cycles/s | generated 8086 asm, register-mapped, plus a block recompiler and AOT images (docs/REALDMG-SPEC.md) | **Super Mario Land 98-100% of real time, interpreter or recompiler; Kirby ~60% interpreted, 84-100% recompiled; Looney Tunes 95% precompiled** (86Box `286-victory`/`286-mega`) |
| APPLE2 | 6502, 1.02 MHz | asm, VICE-derived, every cycle accounted (APPLE2-SPEC 4) | not measured on a 286. XT: **0.54%** (CPU alone caps it at 1.13%). Its own record calls the 386 usable and puts it at tier 3, 486+ |
| C64 | 6510, 0.985 MHz | the same VICE-derived core (C64-SPEC 4) | not measured on a 286. Tier 3, 486+, "for the same honest reason" |
| RUNCPM | Z80 | asm jump-table core, passes ZEXDOC (SPEC.md 74) | not measured on a 286 (SPEC.md 74 says the 386/XT figures are pending). CP/M has no real-time target |

The figure that matters is RealDMG's. All-in, the 286 spends about **15 host
clocks per Game Boy M-cycle**, on the CPU, the PPU, events and the window.
That is the budget a new machine has to fit, and it is only reachable with
RealDMG's techniques: a generated core, guest registers living in 8086
registers, flags taken straight from `LAHF`, and recompiled hot blocks.

So the 6502 machines already in the tree are slow because of how their core
is built. A 286 has enough CPU for them, which section 3 picks up.

## 2. Two facts about the 8086 that decide the ranking

1. **The 8086 was designed to translate 8080 code, and that shows in the Z80's
   flags.** The low byte `LAHF` loads is `S Z - A - P - C`, and the Z80's F is
   `S Z Y H X P/V N C`, so sign, zero, half-carry, parity and carry are already
   in the right bits. RealDMG uses the same trick for the SM83 (REALDMG-SPEC 2).
   **Z80 and 8080 machines are the cheapest guests there are for this host.**
2. **The 6502 is nearly as good.** N is SF, Z is ZF, V is OF, and decimal mode
   is `DAA`/`DAS`. Only the carry needs care, because the 6502's subtract
   carry is inverted. At about 3 cycles an instruction, a 1 MHz 6502 issues
   fewer instructions a second than the DMG does.

A 68000 guest is the opposite case: 32-bit registers on a 16-bit host, and
twice the work on every operation.

## 3. The candidates

Load is a rough instruction rate against the DMG's (~450k a second = 1.0).
**Draw** is pixels a second against the DMG's 160x144 at 60 Hz, with the
caveat that drawing only the damaged lines took gbclock's do-nothing variant
on the 5150 from 43% of real time to 100% (REALDMG-SPEC 13.2): draw cost is
not secondary.

### Tier A: full speed expected from a RealDMG-class core

| machine | CPU | load | video | notes |
|---|---|---|---|---|
| **Apple II / II Plus** (already here) | 6502 1.02 MHz | ~0.75 | text, lo-res, 280x192 hi-res, memory-mapped | **Re-core it.** The CPU is lighter than the DMG's and the screen only needs redrawing where a write lands. This moves an existing package from 486+ to the 286 |
| **ColecoVision / SG-1000 / MSX1** | Z80 3.58 MHz | ~1.0 | TMS9918: 256x192, 15 fixed colours, 2 colours per 8-pixel row, 32 sprites | The fixed palette maps onto VGA's 16. The 2-colours-per-row layout fits planar VGA well. One VDP covers three machines. The BIOS is the owner's to supply; C-BIOS is a free MSX one |
| **Sega Game Gear** | Z80 3.58 MHz | ~1.0 | 160x144 visible, 4bpp tiles | **The shortest step from RealDMG**: the same screen size, the same window and FSX plumbing, and a bank mapper like an MBC |
| **ZX Spectrum 48K** | Z80 3.5 MHz | ~0.9 | 256x192 bitmap plus attributes | Trap writes to 4000-5AFF and redraw only the dirty cells. The beeper (`OUT FE` bit 4) maps straight onto the PC speaker. Border effects need a per-line log |
| **ZX81, TRS-80 Model I/III** | Z80 1.8-3.25 MHz | ≤0.6 | text | Easy wins |
| **Space Invaders and other 8080 arcade boards** | 8080 2 MHz | ~0.6 | 1bpp 256x224, rotated | The classic first emulator, nearly a 1:1 translation. Likely full speed even at 8 MHz |
| **Pac-Man arcade board** | Z80 3.07 MHz | ~0.9 | 28x36 tiles and 8 sprites, 60 Hz | GB-class work. The ROMs would be the owner's, as with RealDMG. PACCMAN is a C port of a remake, not this |
| **VIC-20** | 6502 1.02 MHz | ~0.75 | 22x23 text, programmable characters | Shares the 6502 re-core with the Apple II and C64 |
| **TRS-80 CoCo / Dragon 32** | 6809 0.89 MHz | ~0.45 | MC6847 text and graphics modes | A new CPU family, but a slow one |
| **CHIP-8 / SCHIP** | - | tiny | 64x32 / 128x64 | Runs on the XT too |

### Tier B: 60-100%, needing the recompiler, frame skip or per-line rendering

| machine | why it is harder |
|---|---|
| **Sega Master System** | The same CPU as the Game Gear, but 256x192 at 4bpp is 2.1x the DMG's pixels, and games split the scroll on line interrupts. Do it after the Game Gear, sharing the VDP |
| **ZX Spectrum 128 / +2** | 48K's work plus AY sound (to the OPL or PCM) and paging |
| **Amstrad CPC** | Z80 at ~3.3 MHz effective and a 16KB screen. Games that reprogram the CRTC mid-frame are the hard part |
| **BBC Micro** | 6502 at 2 MHz (~1.5x) and mode-dependent 6845 framebuffers |
| **Vectrex** | 6809 at 1.5 MHz, and the vector integrator made into VGA lines. WIRE (SPEC.md 78) is the line drawing |
| **Game Boy Color, single-speed games** | RealDMG plus banked VRAM and WRAM and a 15-bit palette through the VGA DAC |
| **Donkey Kong-class Z80 boards** | Z80 3 MHz. Skip the i8035 sound CPU or treat it as samples |

### Tier C: a playable fraction, 30-60%

| machine | the cost |
|---|---|
| **NES** | 6502 at 1.79 MHz (~1.3x), 256x240 at 60 Hz (2.7x the pixels), mid-frame scroll splits, sprite-0 hit and the mapper zoo. The best NES result on this host still means frame skip |
| **C64 at full VIC-II fidelity** | Badlines, sprites, raster splits and border tricks over 320x200 at 50 Hz, plus the SID. A RealDMG-class 6510 would make BASIC and many games playable; demos stay on the 386 |
| **Atari 2600** | A light CPU, but the TIA needs the beam raced every scanline. Logging TIA writes with cycle stamps and rendering a line at a time is the only shape that fits |
| **Atari 400/800** | 6502 at 1.79 MHz with ANTIC's cycle stealing, display lists, GTIA and POKEY |
| **Game Boy Color, double-speed games** | Twice the CPU of the case RealDMG already fills |

### Out of reach

68000 machines (Mac 128K, Atari ST, Amiga, Mega Drive), SNES (65816 at
3.58 MHz plus its PPU), PC Engine (HuC6280 at 7.16 MHz), Atari Lynx (65C02 at
4 MHz plus a blitter), Neo Geo, GBA. Their CPU work alone is several times the
budget in section 1.

### A curiosity worth a note: the WonderSwan

Its V30MZ executes 8086/80186 code, and a 286 runs that natively. Only the
display (224x144 tiles) and the I/O ports would need emulating. The catch is
that its code loads absolute segments (ROM at 2000h and up, RAM at 0). Native
execution would need kern_mega's LDT to give selectors those values, which
is speculative. It is worth a probe if the mega arm ever grows a "guest
segment map".

## 4. Recommendations, in order

1. **A RealDMG-style 6502 core, generated, shared by APPLE2 and C64**,
   keeping the VICE-derived core as the accurate setting. This is the biggest
   return available: two shipped packages move from "486+" to playable on the
   owner's machine, and the same generator is the base for the VIC-20, the BBC
   and the NES. Measure APPLE2 and C64 on `286-victory` first, to see how far
   off they are today. Their records hold no 286 number.
2. **A Z80 sibling of RealDMG** (`rdggen.py`'s method with the Z80's opcode
   table, and RUNCPM's ZEXDOC gate as the oracle), **starting with the Game
   Gear**, then the SMS, then the TMS9918 family (SG-1000, ColecoVision, MSX1),
   then the Spectrum. One core, seven machines, all on the host's best-suited
   CPU.
3. **RUNCPM's Z80 measured on the 286**, as SPEC.md 74 already asks. It also
   tells us how far a plain jump-table Z80 core sits from item 2's target.
4. Everything in tier C waits until items 1 and 2 have shown what the shared
   cores actually reach, rather than what this page predicts.

Each new machine follows RealDMG's ROM policy: no ROM or BIOS is committed,
and the owner supplies them or a pinned fetcher fetches them.
