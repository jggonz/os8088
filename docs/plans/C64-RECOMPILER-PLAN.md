# C64 dynamic recompiler — plan

Status: **PLANNED**. Nothing below is built. C64-SPEC §4.9 pins the contract
the waves build to; this file is the reasoning, the stories and the gates.

## 1. Why — MEASURED

86Box `286-victory`, C64.O88 at `10297fb0`:

| program | speed | the 6502's share of the host | where |
|---|---|---|---|
| BASIC `10 i=i+1:goto 10` (warp) | 107.8% | small | the ROM, already translated ahead of time (C64-SPEC §4.8) |
| Bright Pixel card | 35.5% | ~7% | drawing (§9.13) |
| Portland CUG Christmas, first scene | ~30% | **~49%** | RAM code: a character scroller (`LDA ($A8),Y / INY / STA ($A8),Y / DEY / DEY / BPL`), the snow, the music |

What the fast core (C64-SPEC §4.7) still pays per instruction is the
dispatch: `sub bp,n / jle / mov dh,[es:si] / inc si / jmp dx`, a taken jump
that empties the prefetch queue every time, and every operand fetched
through `ES:SI`. The ROMs escape it through `m65aot.py` (§4.8), which can
only translate what is known at build time. **Demo and game code is in RAM,
loaded at run time, and is the case the AOT cannot reach.**

The precedent is RealDMG's recompiler (REALDMG-SPEC §16): the busy loop from
26.2 host clocks a Game Boy M-cycle to 13.95, Super Mario Land from 59.5% to
80.1% in the window. A like gain on the CUG scene's 49% would take it from
~30% to ~40%. That is the expectation to measure against, not a promise.

## 2. What is different from RealDMG

1. **RAM code that rewrites itself.** RealDMG translates ROM, and RAM only
   through a byte-compare guard on HRAM's 16-byte blocks. C64 programs run
   from RAM and modify their own operands (the CUG scene stores into its own
   code at `$C353`). Invalidation is the core of the design, not an edge.
2. **Bank maps.** `$A000-$BFFF`, `$D000-$DFFF` and `$E000-$FFFF` change
   meaning with `$01`. The ROMs are translated ahead of time already, so the
   recompiler takes **RAM PCs only**: below `$A000`, `$C000-$CFFF`, and RAM
   under a ROM only while the map shows RAM there.
3. **A package at its segment's ceiling** (62,712 of 65,024, §13.3). The
   recompiler, its templates and its tables live in PARTS and heap claims;
   the resident cost is the glue alone.

## 3. The design (C64-SPEC §4.9 is the contract)

- **Templates are the fast core's own handlers** (`m65gen.py`), the
  dispatch taken off and `[es:si]` operands turned into immediates, found by
  assembling each handler twice with different operands and diffing - the
  `rdgtmpl.py` method. Translated code computes exactly what the fast core
  does, so the fast core's gates (`c64cputest`'s fourteen rows, Dormann) hold
  the recompiler to it by running them with it on.
- **A block** is straight-line RAM code from a PC to the first control
  transfer, at most 32 instructions, never across a page it cannot fetch.
  It is translated on its Nth entry (start at RealDMG's 64), into a code
  cache of one or two 64KB heap segments, found through a direct-mapped
  table keyed by PC.
- **Cycles** stay the fast core's `BP` countdown; pure templates batch their
  cycles into the block's next exact point (REALDMG-SPEC §16.1.1).
- **Invalidation by page.** A per-page CODE bitmap (256 bits). The fast
  core's write mark (`c64_mark`, which already runs on every RAM write) tests
  it; a write into a page holding code invalidates that page's blocks before
  the store completes, and if the write is inside the block now running, the
  block exits to the fast core at the next instruction. A page invalidated
  too often becomes VOLATILE and is never translated again this session:
  self-modifying loops stay interpreted rather than retranslated forever.
- **Exits** chain to the next block when it exists (patched jumps,
  REALDMG-SPEC §16.4) and fall back to the fast core's dispatch otherwise.
  Interrupts, the I flag and decimal mode keep the fast core's hand-backs.
- **The translator is a part** (`OP_SEG | OP_LAZY | OP_COMP`), like the fast
  core and RealDMG's `rdgjitp.asm`; with no memory for it, the fast core runs
  alone, as it does today.

## 4. Waves

Each wave lands whole, on `emu-survey`, with its gates; the soak union runs
once after the last.

| wave | stories | gate |
|---|---|---|
| **W1 templates** | `m65tmpl.py`: every fast-core handler as a template with its operand and relocation sites; a table part; `--check` that every legal opcode has one or is listed as handed back | a host test re-assembling each template at two origins |
| **W2 translator, straight line** | blocks of RAM code translated and entered from the fast core's control transfers; no chaining; a full flush on ANY write to a code page | `c64cputest`'s rows and Dormann's functional test with the recompiler forced on, on both the plain and translated legs; a new row that rewrites an operand inside a translated loop and sees the new value |
| **W3 invalidation by page, volatile pages** | the CODE bitmap in `c64_mark`; per-page block lists; the volatile threshold | rows for: a write into the running block, a write into another block's page, a volatile page staying interpreted |
| **W4 chaining and batching** | patched exits, pure-cycle batching, hot-count tuning | cycle-exact rows (row 9's totals, row 14's loops) unchanged; 86Box measurements |
| **W5 integration** | Preferences or a menu row to force it off (greyed with its fact when there is no memory), the status of the cache on the row, C64-SPEC and the manual | the CUG scene and the card measured; BASIC unchanged |

## 5. Risks, said now

- **Self-modifying demos may not gain.** A loop that patches its own
  operands every pass (the CUG scroller's `STA $C353`) invalidates its block
  every pass; the volatile rule bounds the cost at "interpreted, as today".
- **The segment.** The glue must stay small; the package has ~2.3KB left.
- **kern_mega**: code in a heap claim is written through a data alias and
  run through a code selector (REALDMG-SPEC §16.2.4) - an arm of its own,
  after W5, gated to the mega build (SPEC.md §20.18).
