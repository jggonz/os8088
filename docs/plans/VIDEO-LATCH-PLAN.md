# VIDEO-LATCH-PLAN — VRAM latch copies for sixteen-colour video

**Status: PRICED AND DEFERRED (2026-10-14). Nothing is built.** The owner:
*"record the latch option, we may implement it later when we have time."*
The measurement is docs/reports/VIDEO-ORDER-2026-10-13.md sections 4 and
7, and `tools/os88vorder.py copies` re-takes it on any decoded VGA4 file.

## 1. The idea

A VGA4 file (SPEC.md 98.1.3.2) writes a changed byte plane group by plane
group. When the picture MOVES, the last screen already holds most of the
new one, shifted. The VGA can copy a byte of the screen to another place
on the screen with all four planes in one move: in **write mode 1** a read
loads the four latches and a write stores them, so `rep movsb` with DS = ES
= the card's segment moves four planes a byte. A sub-record that says
"copy this block of the last screen to here" replaces the stores of every
byte the move gets right, and the ordinary sub-records after it fix what
it gets wrong.

The dither has to agree: Knoll's pattern (98.2.5) is anchored to the
screen in 4 x 4 cells, so a copied block keeps its pattern only when the
shift is a whole byte across (8 pixels, two cells) and a multiple of 4
rows down. A shift that breaks the cell moves a pattern the fresh dither
would not draw, and the residual pays it back.

## 2. Why it is deferred: priced in microseconds, not stores

Counted in STORES it looked free - 4.3% fewer on Gas Gas Gas (report
section 4). But the 286's binding limit on that clip is its DECODE: every
frame `286-fast` cut was the per-frame CPU ceiling's, none the disk's. So
a copy has to be priced as its TIME, and a copy pays for EVERY byte of its
block, changed or not, where a store pays only for a byte that changed.

VM3BENCH (docs/reports/VM3BENCH-PVGA286-2026-10-13.md) timed a `rep movsb`
from the screen at 1.18 us a byte and to it at 1.15, so a screen-to-screen
move is about **2.3 us a byte** on the owner's 286 - an ESTIMATE, nothing
having timed that move. A slice store is 1.227 us a byte and a lone P1
entry 2.37 us, at the `286-fast` profile's own table. On 154 frame pairs
of GASSHOTW's stream (`os88vorder.py copies`, 8 x 8-byte blocks, a block's
set-up 15 us and a row's 1.5):

| a copied byte | blocks a frame | decode | bytes |
|---|---|---|---|
| 2.3 us (the 286's, estimated) | 0.0 | +0.0% | +0.0% |
| 1.5 us | 0.1 | -0.0% | -0.1% |
| 0.5 us (a fast local-bus card) | 1.8 | -0.5% | -1.1% |
| free (only the set-up) | 5.2 | -1.6% | -2.1% |

A copy wins only where nearly every byte of the block changes on several
planes AND the moved picture is nearly exact, which dithered driving
footage seldom has: its motion is perspective, not a pan. Free, the
ceiling on that clip is two percent of the decode.

## 3. When to take it up again

Any one of these makes the numbers above worth re-taking:
- **A clip that PANS with flat shading** - a cartoon, a scrolling game
  capture, credits. A pan is one shift for the whole screen, and flat
  colour means a moved byte is exact. Run `os88vorder.py decode` on its
  file and `os88vorder.py copies` on that.
- **A machine where the move is cheap against a store** - a 486 on a
  local-bus card, where `--us` is nearer 0.5. The `486-dx2-66` profile's
  table says what a store costs there.
- **A disk-bound profile** (`floppy`, `5150-st225`): copies save BYTES
  where they save stores, and when the disk is what cuts, bytes are the
  price. The 2.1% at free is the bound to beat.
- **A field number for the move.** VM3BENCH (`make vm3bench`) times a move
  to and from the screen but not screen to screen in write mode 1; a leg
  for that is the first thing to add, and it says whether 2.3 is right.

## 4. The design, as far as it was worked

Nothing below is pinned; it is what a build would start from.

**Format.** A new header flag (the next free bit: 32768, `F_KPARTS` having taken 16384), so
a player before it refuses the file, and governing VGA4 renditions that
are SUB2 (98.1.3.2.4). A new sub-record lead beside the Map Masks
(01h..0Fh) and the write mode 3 colours (10h..1Fh):

```
COPY sub-record = 20h count(8) block*count
block           = src(16) dst(16) w(8) h(8)     canvas addresses; rows 80 bytes apart
```

**The writer's rules.**
- Copy sub-records come FIRST in their record, before any plane or colour
  sub-record, so every copy reads the last frame's screen.
- No block reads a byte an earlier block of the same record wrote. A block
  MAY overlap its own source: the player copies memmove-style. The encoder
  orders the blocks by that dependency (a uniform pan downward is bottom
  first) and drops a block that closes a cycle.
- A byte a copy wrote may be written again by the plane and colour
  sub-records after it - the residual - which is the one exception to
  98.1.3's "no byte written twice", and a reader has to know it.
- A keyframe record has none; a flipped, page-ahead, Live or resident
  rendition has none.
- Each block's rows lie in the canvas, each row inside one canvas row.

**The player** (`video.asm`'s `.mx` loop, beside `.m3`): Map Mask 0Fh, the
GC in write mode 1 (3CEh index 5 = 01h), then per block `rep movsb` row by
row with DS = ES = the card's segment - forward, or backward with `std`
from the block's last byte when the destination lies after the source -
and write mode 0 put back at the sub-record's end, as `vp_m3off` puts
write mode 3 back. A count or a height of 0 is an empty block, never a
`loop` of 65,536 (98.1.6). A hostile block can only move the card's own
segment about, so it draws garbage and nothing worse. Two other paths a
build cannot skip:
- **round the text box** (98.3.13.1, `vo_clip`): a block whose source or
  destination crosses the box has to read the video under the box from the
  save and write into the save, a byte and a plane at a time (`vc_find`'s
  test, `vc_m3b`'s shape) - slow, and taken only while text is up;
- **into a RAM image** (`vp_decram`): the same move on each of the four
  plane images, `vp_plsp` paragraphs apart.

**The reference decoder and the checks** (`tools/os88vid.py`): `Reader.apply`
takes the lead, applies the blocks from a snapshot of the surface (which is
exactly the memmove-plus-no-chaining rule), and enforces the writer's rules
above; `cycles_of` prices a block; `record` emits it.

**The encoder** (`EncoderP.frame`): before the deltas, the copies chosen
against the target (each block's stores at the profile's price against the
move's), applied to the screen model, their cycles and bytes taken off the
frame's budget, and the ordinary cut run against the copied screen. Blocks
next to each other with one shift become one block. Off by default, as an
option (`--vga4-copy`), priced by a profile field (`copy_us`) the way write
mode 3 is by `WM3_US_286`.

**The gate**: MartyPC's VGA models write mode 1 (its enum carries the
EGA's three write modes, and `tools/martypc/patches/12-vga-write-mode-3.patch`
added the fourth), so a `tests/vidfskeys.py --kind vga4cp` leg - a clip
that pans, exact on the glass at every hold, round the text box included -
is reachable; red with the backward copy taken out, and with the box's
save left alone.

**Cost, estimated**: a few hundred bytes of `video.o88` (the screen path
~80, the box path ~150, the RAM path ~50; the image is 49,357 of its room
today), no kernel byte, and the encoder's search is numpy over 45 shifts a
frame.
