# WINDOWED-DRAW-PLAN - what a windowed picture costs, and what is left to cut

**Status: OPEN. One item BUILT (W2), the rest NOT BUILT** (written
2026-10-28 on `claude/realdmg-optimize-continue-1ftus9`). The first
customer is RealDMG (REALDMG-SPEC 15.2) and the measurement is RealDMG's,
but two of the items are about `OSAPI_GFX_BLITP` itself and reach every
package that puts a planar band into a window. Sections 2 and 3 keep them
apart: section 2 is the KERNEL's, section 3 is RealDMG's.

The owner's words, when this was asked: *"IIRC the windowed draw is mostly
because we are using the os's draw routines ... maybe set an agent on it
just to check if there are any optimizations we can do. We already flash a
'windowed is slow' message on every startup, so the user knows we are
optimized for fullscreen."* So this is a list of options to take when
somebody next wants the window faster. It is not a defect list. The
startup notice stays true whatever is taken here (section 4).

## 1. The measurement

Taken on the 86Box `286-mega` profile (the mr286, 16 MHz, OTI-067 VGA),
kern_mega, Kirby's Dream Land's long replay, windowed, `--profile 4000`
with `RDGWIN_FINE`. The package and its listing were built together with
`tools/gbaot.py make --mega --embed`.

- Steady scrolling draws **5 to 6 pictures a second at 49 to 57 ms each**,
  with the game held at ~99%. A still scene draws in ~10 ms at 35 to 51 fps.
- Of the whole machine over the scrolling span: the game's precompiled
  code **59.5%**; composing the frame (`ppu_*`, `rdg_renderd`) **~16%**,
  about **27 ms a full draw**, which is the same work full screen does;
  `gfx_blitp` **5.7%**, about **9.5 ms a draw**; `rw_planes` (two shade
  planes made into BLITP's four) **3.7%**, about **6 ms a draw**.
- **Every other kernel routine is under 0.5%**: the gfx lock, the clip,
  the region walk, `cur_unlazy`, the display hook and the API cells. A
  scrolling picture is one changed-line run and so ONE `BLITP` call.

**The theory that the window pays for the OS's paperwork is WRONG.** It
pays for BYTES WRITTEN TO THE CARD. 5.6 of `gfx_blitp`'s 5.7 points is one
16-byte block, `.frow`'s copy, at ~66 us a line: 80 VGA byte writes at
86Box's OTI-067 ISA price (6 ISA cycles, 12 CPU clocks each). The window
writes **4 planes x 20 bytes** a line, **11,520 writes** a full frame;
full screen writes **2 planes x 10 words**, **2,880**. That difference
and the separate `rw_planes` pass are ~12 to 13 ms of a ~52 ms draw,
about a quarter. The other three quarters are composing, which full
screen also pays, and the pacing rule (REALDMG-SPEC 15.2), which gives the
picture only what the game leaves over.

## 2. The kernel's items - `OSAPI_GFX_BLITP`, every caller

### W1. A shared-planes form of BLITP - NOT BUILT, needs an ABI decision

A flag (or a sibling slot) meaning *"planes 0 to 2 are the same bytes"*:
the kernel writes plane 0's bytes once under Map Mask 0111b and plane 3's
under 1000b, so the card takes **two writes a byte instead of four**, and
the caller builds two planes instead of four.

- **Why it fits RealDMG exactly**: after the `BGP` map the desktop's four
  shades are indices 0, 8, 7 and 15 (REALDMG-PLAN section 4, the windowed
  table), so planes 0 to 2 ARE one plane, the high bit inverted. Any
  four-grey source has the same shape.
- **Estimate**: 52 -> ~43 ms a full scrolling draw (~17%), as `.frow`'s
  writes halve and `rw_planes` builds half as much.
- **Cost**: an `OSAPI_*` contract change. A new flag bit on an existing
  slot re-contracts it; a new slot at the end of the table only adds one.
  Either way it is SPEC.md 20.2.0.1's written decision and an `abilock`
  update. The mono path is not touched, because BLITP is only ever called
  on a planar display.
- **Risk**: low to medium. Its own row is the existing `blitp` and
  `dispblitp` rows, plus one that compares the shared form against the
  same picture sent as four identical planes, pixel for pixel. Break it
  on purpose by writing plane 3 under 0111b, and that row must go red.

### W2. Word copy in `.frow` - BUILT (commit 26b839d0)

`rep movsw` in `gfx_blitp`'s plane-major copy, with a leading `movsb`
when the framebuffer byte is odd and a trailing one for an odd count. It
is 8086 code and safe on every adapter that takes BLITP.

**On 86Box's OTI-067 it bought nothing measurable**: that card's ISA
interface splits a word write into two byte cycles, so the bytes cost what
they cost. The estimate it was taken on (~3 ms, ~6%) is what a card with a
real 16-bit path should show, and a Paradise PVGA1A (the Victory) is one,
but nothing has measured that. **It is still OPEN as a number**: a
`blitpair`/`blitplane` reading on the Victory, or a `gfxbench` BLITP row
taken there, says whether it paid. On an 8-bit XT card it gains nothing and
costs nothing.

## 3. RealDMG's items - package only, no kernel byte

### W3. Write the window's planes in `ppu_dput` - NOT BUILT

Full screen's `ppu_dvga` already writes VRAM straight from the composed
line. The window could have `ppu_dput` write the window's plane format
into `[rw_pseg]` in the same pass and drop `rw_planes` entirely.

- **Estimate**: ~3 to 4 ms a full draw.
- **Risk**: medium. `ppu_dput` is hot shared code and is entered at the
  `rdg_lk_dput` sites the core patches, so the window's arm must be chosen
  ONCE (at mode change), not tested per line. The gates are `gbdamage`, and
  `rdgwin` windowed for the timing.
- **It composes with W1**: with shared planes `ppu_dput` writes two planes,
  not four, so W1 and W3 together are the most this file can buy.

### W4. Unroll `rw_planes`' inner loop - NOT BUILT

**~1.5 ms a draw**, low risk. It is moot if W3 is taken, because W3
deletes the routine. Take W4 only when W3 is refused.

## 4. What it can reach, and what it cannot

The best combination, W1 with W3 (or W2+W3+W4 on a card where W2 pays),
takes a full scrolling draw from ~52 ms to **~40 to 43 ms**, roughly 6 to
7.5 pictures a second instead of 5 to 6. That is worth having, but it is
not full screen. The ceiling is the ~27 ms of composing, which full screen
pays too, plus the game's own ~60% of a 286. A window can never draw as
often as the fullscreen bracket does, so RealDMG's *"windowed is slow"*
startup notice stays as it is.

**What is NOT on this list**: the window manager's per-call overhead, the
clip, the lock and the cursor. Together they measured under half a point,
and that is the reason section 1 exists.
