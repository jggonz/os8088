# BLIT4-WALK-PLAN: `gfx_blit4` under a region, every fragment at the fast path's price

**Status: BUILT - SPEC.md 5.4.1.5 is the contract and this file is the
design record behind it.** A handoff from the Video Player work of
2026-10-19 (`video-things`, commit cd2a49e2). The owner's words: *"Would it
be possible to 'fix' blit4 in the kernel so its as fast as v4blit? That being
so slow does cause a lot of games issues."* Section 9 is what the build
decided and measured; the sections before it are the plan as handed over.

## 1. The finding

**`OSAPI_GFX_BLIT4` under an armed clip region is two orders of magnitude
slower than the same blit uncovered, whenever the block crosses a fragment
edge.** SPEC.md 5.4.1.4 keeps the fast path only when the WHOLE block lies
inside ONE fragment (`wm_clip_t1`); otherwise `gfx_blit4` jumps to `.slow`
(`kernel/vga12.inc`, the `jc .slow` after `call wm_clip_t1`, ~line 1949),
where each coalesced run is a clipped `gfx_hline` -> `gfx_fill`, ~756 us of
arrival each (SPEC.md 5.7). A dithered picture is about **one run a pixel**.

Every `W_PAINT` has a region armed (SPEC.md 11.3.3's cull), so this is not
a corner case: it is every repaint of a picture that another window
partly covers - a window dragged across a Paint canvas, a Video Player
poster, a windowed game's frame.

**Measured** on MartyPC's `os8088_xt_vga` (4.77 MHz 8088), one
`vp_pposter` repaint of a 200 x 112 VGA4 poster of random colours from an
8-colour set, a Disk window moved from one place over part of the box to
another (so the heal's region is in pieces):

| path | heal |
|---|---|
| `gfx_blit4`, region in pieces (`.slow`) | **13,401 ms** |
| `gfx_blit4`, region contains the box (the fast path, §5.4.1.3's VGA decoder) | 558 ms |
| `gfx_blitp` walking the clip (SPEC.md 5.4.3.6), the player's planes converted from packed (`video-things` cd2a49e2) | 588 ms (of which the blits are ~40; the rest is the conversion) |

On the owner's 286 that 13.4 s was "a second or two" a drag step.

**The Video Player no longer pays it** - `video-things` cd2a49e2 draws its 16-colour
poster as planes through BLITP's walk - but every other `gfx_blit4` caller
still does: Paint, `os88img.inc`, PixelStein, Scribe, every windowed game.

## 2. What the fix is - and what it is not

**It is: give `gfx_blit4` the fragment walk `gfx_blit1` and `gfx_blitp`
already share**, so a covered blit costs what the visible pieces cost at the
fast path's price - near enough the uncovered blit, ~558 ms in the row
above rather than 13.4 s.

**It is NOT "as fast as v4blit".** `vp_v4blit` is `gfx_blitp` on PLANES;
`gfx_blit4` takes PACKED nibbles, and on VGA it has to turn every pixel into
four plane bits (SPEC.md 5.4.1.3's decoder, ~107 cycles a pixel). Uncovered,
the measurement above is 558 ms for BLIT4 against ~40 ms of BLITP blits
for the same picture. A caller that wants BLITP's speed hands planes; that is
SPEC.md 5.4.3's contract and this plan does not change it. Say so to the
owner in those terms - the owner asked for "as fast as v4blit", and the
honest answer is "as fast as BLIT4 uncovered".

## 3. The walk that exists

`kernel/vga12.inc`, **`kern_big` only** (`%ifdef KERN_BIG`):

- `gfx_blit1_x.frags` (SPEC.md 5.4.2.7): banks the block's rect in
  `gfx_cl_x1..y2`, walks every fragment of the region (at most
  `WM_CLIP_MAX`), and for each intersection calls the PIECE routine in
  `[gfx_bpfn]` with the region disarmed. A band's piece is `gfx_blit1_x`
  itself; a planar one is `gfx_bppiece`.
- `gfx_bpwalk` (SPEC.md 5.4.3.6): sets `[gfx_bpfn]` to `gfx_bppiece`, calls
  `.frags`, restores it. `gfx_bppiece` reads its extra argument (the plane
  step) back out of the frame `.frags` pushed (`GFX_BPSTEP`, 12 or 14 with
  `DOCK_OPT`).
- The pattern to copy is `gfx_bpwalk` + `gfx_bppiece`, one level up: a
  `gfx_b4walk` that sets `[gfx_bpfn]` to a `gfx_b4piece`, and a piece that
  advances the SOURCE to the piece's top-left and calls `gfx_blit4` on the
  sub-block with the region disarmed.

## 4. The one hard part: the nibble phase

**A piece whose left edge is an ODD number of pixels into the block cannot
start the fast path**: the source row of a packed block starts on a byte,
two pixels a byte, and `gfx_blit4`'s own comment says it - *"a left clip
advances the source by a pixel count that may be ODD, and that shifts the
nibble phase of every pair after it"* (the `js .slow` / `ja .slow` x guard).
SPEC.md 39.14.7.2 hit the same wall cutting a block at a display seam and
REFUSED an odd cut.

Rounding the piece outward draws a column OUTSIDE its fragment - over the
window that cut it - so that is not available. The options, cheapest first:

1. **The odd column the slow way, the rest fast**: draw the piece's first
   column through `.slow` (one 1-pixel run a row, region still armed, so it
   clips itself) and the piece from x + 1 - now an even offset - through the
   fast path. ~756 us a row for that column: 85 ms for a 112-row piece on
   the 8088, against 13 s for the whole thing today. **Probably the answer.**
2. **A nibble-phase arm in the fast path**: the pair decoder (1bpp) and
   §5.4.1.3's planar decoder each taking a half-byte start. More bytes, on
   the hottest code in the kernel - weigh it against how often a fragment
   edge is odd in practice (window rects are on the 8-pixel grid, SPEC.md
   11.94; the fragments are windows' OUTER rects, so measure the parity
   distribution before building this).
3. Check first whether option 1's case even occurs on the real desktop:
   if every fragment edge a desktop produces is even relative to a block
   whose x is on a byte, option 1's code is a guard that never runs. It
   still has to exist, because a caller may blit at any x.

## 5. Where it hooks

At the existing `jc .slow` after `wm_clip_t1` (the block crosses a fragment
edge): under `KERN_BIG`, go to the walk instead. Things the walk must keep:

- **The display hook** (SPEC.md 39.14.7.1-2): by this point AX is the
  TRANSLATED x and `[gfx_blit_hk]` may be set; a straddling block may be one
  half of a `.cut`. A piece is a recursive `gfx_blit4` - decide whether it
  re-enters with the region disarmed BEFORE or AFTER the display decision,
  and test on `os8088_xt_vga_mda` (two displays) as well as one.
- **The scratch**: `gfx_blit_w/x/h/sv`, `gfx_blit_fst`, `gfx_blit_pok`
  (SPEC.md 5.4.1.3's arming - the comment at the top of `gfx_blit4` explains
  why a stale value draws over the window on top) are per-CALL and a piece
  is a call; the outer call must not have armed anything the walk's pieces
  then inherit, and must leave through an epilogue that resets what it did
  arm.
- **The cursor**: `cur_unlazy` is at the top of `gfx_blit4`; a recursive
  piece calls it again, which should be harmless - verify.
- **`gfx_bpfn` is shared**: `gfx_blit1_x` is its resting value and line
  ~2747 tests it (`cmp word [gfx_bpfn], gfx_blit1_x`) to know whether a call
  is a piece of a region walk. A third value must be accounted for there.

**`kern_small`** has no walk (`.frags` is `kern_big`). Either leave
`kern_small` on `.slow` (the 128 KB machine's desktop is a CGA or Hercules
in practice, and its callers are few) or weigh moving the walk - which
SPEC.md 5.4.2.7 put under `KERN_BIG` deliberately. Decide by bytes
(CLAUDE.md: *RUNGS ARE TEMPORARY. BYTES ARE FOREVER*), and say which.

## 6. What it should cost

ESTIMATED, not measured: `gfx_bpwalk` is 4 instructions and `gfx_bppiece`
4, so the walk entry and the piece are ~30 bytes; the source advance
(`SI += dy * BP + dx / 2`) and the odd-column split of option 1 are the rest
- **~80-150 resident bytes of `kern_big`**, `.cold` where it can be (the
walk is cold; it runs only under a region). Measure the SPAN
(`tools/kernsize.py` and the symbol span, never a rung) and quote it as
*"+N bytes, M ms a covered repaint"* per CLAUDE.md's banner.

## 7. The gate

Build the row before the fix and watch it FAIL (docs/WRITING-TESTS.md 1):

- **A new soak row**, MartyPC `os8088_xt_vga` and a 1bpp adapter
  (`os8088_5150_herc_gla`, where §5.4.1.1's pair decoder is the fast path):
  a package or harness that `gfx_blit4`s a dithered block in its `W_PAINT`,
  a Disk window moved from one place over part of it to another, and the
  heal read three ways - its cost in guest cycles (an upper bound: well
  under 2x the uncovered blit), **zero `gfx_fill` calls** inside the blit
  (`os88marty.bp_trace`), and the box's pixels on the RENDERED glass
  (`m.fbuf()`) equal to the uncovered picture everywhere the Disk window is
  not, and the Disk window's pixels untouched. Include a fragment edge at an
  ODD offset into the block (place the Disk window at an odd x - the WM
  snaps windows to the grid, so the block's x may need to be the odd one).
- **The Video Player's instrument** is a ready-made A/B: `video-things` cd2a49e2's
  measurement was taken with a scratch script that boots a 200 x 112 Live
  VGA4 file and traces `vp_pposter` to `vp_pposter.out` around a
  `move_window`. To use it against BLIT4, revert `vp_p4planes`'s call in
  `vp_pposter` (`.bp:`) in a private build - that is the BLIT4 arm; the
  kernel fix should take that arm from ~13.4 s to ~0.6 s.
- **The existing rows** a change to `gfx_blit4` can reach: `blitplane`,
  `blitcut` (the seam cut), `vidvga4`, `vidposter`, `vidlivevga4`, Paint's
  rows (`soak -k 'paint*'`), `disp*` for the second display. Name them in
  the commit; run their union once at the end (CLAUDE.md, Testing).

## 8. Open questions for the owner

1. `kern_small`: walk it or leave it on `.slow`?
2. The odd column: option 1 (slow column) unless the parity measurement in
   4.3 says option 2 is worth its bytes.
3. Whether to publish a "walk" request bit on `OSAPI_GFX_BLIT4` the way
   BLITP has DI bit 14, or make it unconditional. BLITP needed a request
   because a refusal there changed the CALLER's behaviour (Paint converts
   its canvas); BLIT4 never refuses, so unconditional is the likely answer
   and needs no ABI change - confirm with `tools/abilock.py` that nothing in
   the SDK moved.

## 9. What was built (2026-10-19, `blit4-covered`)

SPEC.md 5.4.1.5 is the contract. The plan's answers, in its own order:

- **The walk is `.frags`, unchanged in shape**: `gfx_b4walk` puts
  `gfx_b4piece` in `[gfx_bpfn]` and shares `gfx_bpwalk`'s tail, and `.frags`
  now leaves the piece's source ROW in DI - it computed it anyway, and the
  reorder is a byte shorter - so the piece only adds the column half at two
  pixels a byte. It is asked at the top of `gfx_blit4`, ABOVE the display
  hook (section 5's first question): `wm_clip_tab` is virtual and so is the
  x there, and each piece re-enters with the region disarmed and takes the
  display decision, the cut included, as an uncovered blit does. A call
  nested in an outer hook keeps `.slow`; nothing nests one today, so on
  `kern_big` the containment test of SPEC.md 5.4.1.4 went with it.
- **Section 4, the nibble phase: option 1**, and section 4.3's measurement
  came back the other way from the guess in it - every fragment edge a
  window makes is an EVEN number of pixels off the content grid, so the odd
  column is never reached by a block at an even x and is reached at every
  cutting edge by a block at an odd one. The column is a `gfx_vline` per run
  of one colour down it, so a flat column is one call. Option 2 was not
  needed by any measurement taken.
- **Section 5's three cautions**: the walking call leaves through `kret_bp`
  and not `.out`, because the pieces leave `[gfx_blit_fst]`/`[gfx_blit_hk]`
  as they left them; `cur_unlazy` again per piece is three instructions; and
  `gfx_blitp`'s `cmp word [gfx_bpfn], gfx_blit1_x` reads the third value as
  "inside a walk", which is true, and no blit4 piece reaches `gfx_blitp`.
- **Section 8.1, `kern_small`: left on `.slow`**, byte-identical. `.frags`
  is `kern_big`'s; bringing it would cost the whole walk's bytes on the build
  that is defended hardest, for a machine whose desktop is a CGA or a
  Hercules and whose `gfx_blit4` callers are few.
- **Section 8.3, a request bit: none.** `OSAPI_GFX_BLIT4` never refuses, so
  no caller's behaviour can change; `tools/abilock.py` has nothing to lock
  because nothing in the SDK moved.
- **Section 6's estimate was 80-150 bytes and it landed at 86**: `.cold` +90,
  `.text` -4. Four of them are PARITY - the nested test lives in the cold
  walk so `gfx_blit4`'s run loop and all of `.text` after it move by an even
  4, and `.frags` keeps one `mov` where an `xchg` would have put the blit1
  pen and every band's emit at an odd shift (PERFORMANCE.md Part 7).

**The gate** is `tests/b4walk.py` (`soak -k b4walk`), on a fixture of its
own (`tests/b4walk/`), and it was broken on purpose twice: the kernel before
the walk reads 23.6x the uncovered blit and 600+ fills, and the walk with
its odd column's draw removed reads 68 and 52 missing pixels - which the
first spelling of the pixel check could NOT see, for two reasons worth
keeping. A column that was never covered keeps its right pixels from an
earlier paint, so the fixture clears its block before each blit; and the
rendered Hercules raster is offset from the screen by its overscan, so a 1bpp
card is compared out of its framebuffer memory.

**What was left open - a narrow piece - is SPEC.md 5.4.1.6 now.** VGA kept a
block under 64 pixels off the planar decoder (SPEC.md 5.4.1.3), so a 46-pixel
dithered piece paid the run writer's price a pixel and the heal that had one
read 1.67x the uncovered blit. A floor of 16 alone cured it and made a flat
narrow block 2.65x worse; a run budget `max(W / 32, k)` was built at k = 2, 3
and 4 and refused, each making some flat row 1.5-1.8x worse, because a run
COUNT cannot tell a five-run flat row from the start of a dither. A narrow
row's DENSITY can - the pixels its runs have spent - and that is what shipped,
for 18 bytes.
