# RealDMG precompiled code: what is left to take (2026-10-27)

Measured on branch `claude/realdmg-optimize-continue-1ftus9` at `299837eb`,
host-side (Unicorn), over each game's precompiled blocks weighted by the heat
of the owner's replays: Link's Awakening (7,394 frames), Kirby's Dream Land
(the long replay, 21,260 frames, to the middle of level 3), Super Mario Land
(4,669) and Looney Tunes (3,731). `tools/gbprof/aotbounds.py` re-derives the
first two tables; the instruction mix is the host profiler's (`prof4`'s kinds,
frames 1,000-2,500, or 500-2,000 for Looney Tunes).

## 1. Dead flags across block exits - NOT TAKEN

REALDMG-SPEC 16.1.2 drops an instruction's flags when the instructions after it
**in the same block** overwrite them first. Following the block's exits into
their successors (fall-through, JR/JP/CALL targets, both arms of a conditional)
finds very little more:

| game | eligible executions | dead in-block today | more across exits | at a 12-instruction look |
|---|---|---|---|---|
| Link's Awakening | 3.13 M | 45.2% | +1.9% | +0.4% |
| Kirby's Dream Land | 18.86 M | 71.1% | +1.9% | +0.1% |
| Super Mario Land | 1.57 M | 37.5% | +5.3% | +4.3% |
| Looney Tunes | 2.65 M | 53.0% | +5.8% | +3.3% |

At ~3 x86 instructions saved per INC/DEC/CB and ~5 per ADD HL, that is 27-170
instructions a frame, **0.3-1.4% of precompiled code**. And it is not exact:
every exit carries the event check, an interrupt taken there runs a handler
that PUSHes AF, and the stale F it pushes is a memory difference `gbaot check`
reports. Taking it would mean weakening that gate for ~1%.

## 2. Folding block checks into superblocks - NOT TAKEN

`sub bp,n` and its branch are ~14% of precompiled instructions. A check can be
folded exactly only into a superblock entered with enough cycles for all of it
(`cmp bp, total`), and only where nothing inside reads the time - a read of
LY or STAT, or anything through (HL), (BC), (DE) or (C) that may reach one.

| game | block executions with no possible time read | ...entered straight from such a block (foldable) |
|---|---|---|
| Link's Awakening | 67.1% | <= 12.4% |
| Kirby's Dream Land | 57.3% | <= 12.7% |
| Super Mario Land | 62.9% | <= 18.7% |
| Looney Tunes | 58.6% | <= 15.5% |

Two instructions saved per folded block of ~10 is **at most 2.5-4% of
precompiled code**, before the head check and a refund on every side exit,
for duplicated code and a translator change. Upper bounds both: the second
column counts a block whose predecessor qualifies, not the path it came by.

## 3. The instruction mix - flat

| kind | Kirby | Zelda | Looney |
|---|---|---|---|
| plain register moves | 9.2% | 8.0% | 8.6% |
| of them `mov dx,di` / `mov di,dx` (DE has no byte halves) | 2.2% | 0.9% | 2.2% |
| flags: `or`/`and ah` | 9.3% | 6.5% | 7.8% |
| flags: `lahf`/`sahf` | 7.0% | 5.9% | 7.1% |
| the cycle check, `sub bp` | 8.9% | 9.0% | 8.3% |
| `jmp near` to another block (out of short range) | 5.1% | 6.0% | 4.9% |
| `jmp near` inside a block (split code) | 1.8% | 2.3% | 0.9% |
| `cmp` | 7.3% | 9.0% | 7.0% |
| memory moves | 6.5% | 10.8% | 8.3% |

No back-to-back `mov di,dx` / `mov dx,di` pair occurs at all, so there is no
peephole there. Nothing in the mix is above a tenth.

## 4. Outside the precompiled code

In full screen on `286-mega` the BIOS is 2.8-3.9% of the machine, 1.8% of it in
the system BIOS's I/O-delay routine (F000:A240). It is NOT `rw_fsdrain`'s
`int 16h`: reading the BIOS key buffer's head and tail first and calling
`int 16h` only when they differ was built and measured at 14.80 -> 14.81
frames drawn a second, the BIOS 3.9% -> 2.9%, and taken out. Its caller is
not known.
