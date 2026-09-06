# A 1bpp canvas for Paint: what the options actually are

**Status: OPTION A IS BUILT — SPEC.md §42.23 is the contract and this file is
the design record behind it.** Read §8 first: **open question 1 is ANSWERED
and the answer is NO** — `gfx_blit1` does not accept a negative stride, two
unsigned `mul bp` are why, and that is what decided the screen half. What
shipped is the memory win in full (448×258 goes 56.6 KB → **14.2 KB**,
measured on a Hercules under MartyPC) with `gfx_blit4` drawing it a row at a
time; the fast path is a dozen bytes in Paint once the kernel's two row-skips
are made sign-aware. Option B still stacks on top unchanged.

Two things the build settled that this page only guessed at. **Every 1bpp arm
turned out to be the planar arm with the plane loop removed** — eight pixels
to a byte is eight pixels to a byte whether that byte is one plane of four or
the whole picture — so declaring `[pt_bpr]` to be the whole stride at one bit
made `pt_lose_w`, `pt_lose_h` and `pt_ublocks` fall out for the price of a
plane *count*, and the clipboard, the resize copy and the undo image needed
nothing at all (SPEC.md §42.23.7). And the cost came in at **+527 bytes an
instance** against §6.3's predicted +800 to +1,400 for big — because §6.1 read
the fork sites as "2-way → 3-way" and they are really "2-way, plus one arm
that reuses the second's arithmetic".

**What it cost and what it verified.** +527 bytes an instance at the first
commit, then 2 bytes back when the swatch rule collapsed into `pt_ncolset`.
The gates: `paint1bpp` and `paint1load` are new and assert the claim, the DIB
field by field, and the load; `paint1bpp-colour` and `paint1load-vga` assert
the **negative** on a VGA, which nothing in `tests/` did before — a change
that made every canvas one bit deep would otherwise have passed the whole
suite and quietly cost the VGA fifteen of its colours. `paintblank`,
`paintsize`, `paintshrink`, `paintrz-1bpp`, `paintwipe`, `paintundo`,
`paintanchor`, `paintdirty`, `paintsu`, `paintbig`, `paintcull`, `paintplan`,
`paintdraw`, `paintfill` and `paintback` all pass. **`paintrow` fails and did
so before this work** — A/B'd against a worktree at the preceding commit, and
recorded as docs/HANDOFF-SOAK-FINDINGS.md A6.

**One consequence the design did not anticipate**, and it is worth reading
before touching the load rule: `build/OS8088.GIF` has a **two-entry colour
table**, so §42.23.6 opens the tree's only picture one bit deep on every
adapter. That is correct for that file and it left three rows whose subject is
the four-plane canvas unable to get one — `paintplan` kept *passing* while its
stated proof, "the canvas went planar by construction", had silently stopped
being true. `dispapps.colour_gif` derives a colour fixture by appending two
unused table entries, changing not one pixel.

**Nothing below has been rewritten to match.** It is the reasoning as it stood
before the work, which is what makes it worth keeping; where it is wrong the
paragraphs above say so.

---

**Status when this was written: RESEARCH. Nothing here is built and nothing is
decided.** It exists
because the motivation changed. `docs/PAINT-STROKE-PLAN.md` §5 already carries
a 1bpp-canvas entry, as its item 1, marked *"VALID, MAYBE SOMEDAY, NOT TODAY"*
on four reasons — and **every one of those four is a speed argument**. The new reason is
memory, which that entry does not weigh at all, so its verdict does not bind
this decision. §5 below corrects one of its four on the evidence as well.

Every figure here is measured on the tree at `a0e9ce2` unless it says
*predicted*, and the predictions are marked as such because no 8088 has run any
of this (CLAUDE.md's rule).

---

## 1. The prize, in one table

Paint's canvas is **4 bits per pixel on every adapter** — `pt_paras` is
`ceil(w/2)` rounded up to 4, and SPEC.md §42.13's four-plane form is *the
identical size* at every width (`ceil(w/2)` rounded to 4 **is** `4 * ceil(w/8)`).
So the planar format that already exists buys speed and has never bought a byte.

A 1bpp canvas is the first thing that would.

| canvas | 4bpp today | 1bpp | saved | ratio |
|---|---:|---:|---:|---:|
| VGA default 448×280 | 61.4K | 15.4K | 46.0K | 3.99x |
| **Hercules default 448×258** | **56.6K** | **14.2K** | **42.4K** | **3.99x** |
| CGA default 448×110 | 24.2K | 6.1K | 18.1K | 3.98x |
| VGA max 736×464 | 166.9K | 41.7K | 125.1K | 4.00x |
| the floor machine's square cut 203×110 (SPEC.md §42.6.5) | 11.3K | 3.1K | 8.2K | 3.68x |

…and the same arithmetic read the other way round, which is the sentence that
matters. **What a given heap buys at 448 wide:**

| free heap | 4bpp rows | 1bpp rows |
|---:|---:|---:|
| 13.5K — the floor machine, Files open | 61 | **245** |
| 20.0K | 90 | 364 |
| 28.0K | 127 | 510 |
| 40.5K — kern_small's whole heap | 184 | 739 |

**13.5 KB is what the 128KB floor machine has left with the file browser open.**
Today it funds a 203×110 letterbox. At 1bpp it funds the **full 448×258
Hercules default with 43% to spare.** That is the entire argument, and it is not
close.

---

## 2. Is 1bpp packed a valid format? Three separate answers, all yes

**2.1 The kernel's.** `OSAPI_GFX_BLIT1` (SPEC.md §5.4.2) takes *exactly* a 1bpp
packed band: `ES:SI` = row-major, bit 7 leftmost, **1 = a lit pixel**; `BP` =
stride in bytes; `AX`/`CX` = destination x and width in pixels, **both multiples
of 8**; `BX` = y, `DX` = rows 1..255. x and y are signed. It honours the clip
region to the exact pixel row. So the format is not something Paint would be
inventing — it is a published kernel argument type with a slot of its own.

**2.2 The file's.** A 1bpp BMP is `biBitCount = 1`, `biClrUsed = 2`, an 8-byte
palette, `bfOffBits = 62`. **Paint's reader already accepts it**: `pt_bmp_in`
tests `biBitCount` against 1, 4, 8 and 24, `pt_bmp_row` has a 1bpp arm that has
been there all along, and `pt_bmp_pal` reads an arbitrary palette size. The
canvas *is* the file (SPEC.md §42), so a 1bpp canvas writes a smaller, entirely
standard BMP that any host reads.

**2.3 Ours.** `PT_CV_X` is 48 — a multiple of 8, and commented as one — and
SPEC.md §11.94 snaps a window's content origin to a multiple of 8. So the
canvas's *screen* x already satisfies `gfx_blit1`'s hard rule for free. The 1bpp
BMP stride is `ceil(w/8)` rounded to 4 bytes, i.e. a multiple of **32 pixels**,
so a whole-canvas blit is legal without rounding anything. A partial rect (a
stroke's damage) needs x masked down to /8 and width rounded up to /8; the extra
columns are inside the canvas and inside the window, so they are redrawn with
what they already held — which is exactly what `pt_blit` already does today for
its even-column rule.

**2.4 Polarity — and a free 36%.** `gfx_blit1` maps a *set* bit to a *lit*
pixel, and takes `rep movsw` at **12.5 clocks a byte** when the band is already
that way up, against a hand loop at **17** when it has to complement (SPEC.md
§5.4.2.2). Standard 1bpp BMP palettes are `{0 = black, 1 = white}`. So
**storing the canvas as 1 = white costs nothing, agrees with the file format,
and takes the fast loop.** Storing it the intuitive way round (1 = ink) would
cost 36% of every canvas blit for ever.

---

## 3. The four screen paths, and which build has them

This is the fork in the road, and it is not where one would expect it.

| path | kern_big | kern_small | shape |
|---|---|---|---|
| `OSAPI_GFX_BLIT1` | **yes** | **NO — `stc`/`ret`** | `rep movsw` a row |
| `OSAPI_GFX_BLIT4` | yes | yes | run-coalescing, one `gfx_hline` a run |
| `OSAPI_GFX_BLITP` | yes | yes | refuses 1bpp adapters outright |
| `OSAPI_ICON_DRAW` | yes | yes | masked, any x, **16×16 maximum** |

**`gfx_blit1` is kern_big's alone.** `kernel/vga12.inc` gates `gfx_blit1_x`
inside `%ifdef KERN_BIG` and `kernel/kernel.asm` gives the small build
`gfx_blit1: stc / ret`, on the stated ABI rule that a slot number may not depend
on a knob. The SDK says so in the slot comment: *"or this is a kern_small
machine, which carries the slot and not the body. TEST CF AND HAVE A SECOND
PATH."*

**So the build that most wants a 1bpp canvas is the one build with no 1bpp
blit.** Everything awkward in this document comes from that one line.

`gfx_blit1_x` is **698 bytes** of `.cold` plus a 4-byte far entry, and it needs
four `.cold` shims (`cw_cur_unlazy`, `cw_cursor_hide`, `cw_cursor_show`,
`cw_wm_clip_rows`) that kern_small does not build today. Call it **~730 bytes**
to give kern_small the body. Some of that is the VGA per-column path and the
VGA arm of the pen; a mono-only variant would be smaller, but that is a second
body, not a smaller one, unless kern_small drops the card (which is
docs/MONO-RECLAIM-PLAN.md's question, not this one).

**It is not a pure ungate, and the reason is one call.** All four shim BODIES
exist in both builds — `cursor_show`, `cursor_hide`, `cur_unlazy` and
`wm_clip_rows` are ungated where they are defined, so only the `cw_` shims are
missing. But the body's *straddle* test calls `cw_vid_span_one`, and
`vidsel.inc`'s whole dual-display half is inside one `%ifdef KERN_BIG`, so
`vid_span_one` **does not exist** in the small build. That check has to compile
out — which is correct rather than a compromise: kern_small has no extended
desktop, so there is never a seam to detect. It does mean the port is a small
build variant of the routine and not an ungate, and that the variant needs
saying in SPEC.md §5.4.2 rather than discovered.

**`ICON_DRAW` does not scale to a canvas.** 16×16 is the stage's fixed size
(`ICO_STAGE_WW` = 1 word, `ICO_STAGE_H` = 16, and `icon_draw_x` refuses any
other width in one compare). A 448×258 canvas is 28 × 17 = **476 tiles**, each a
far call plus a shape draw. It is the right primitive for a card pip (SPEC.md
§43.11.3) and the wrong one for a picture.

---

## 4. The four options

**WHAT THE LOAD COSTS, which this document never priced.** The decoder is a
separate axis from the screen, and the first 1bpp build regressed it badly:
`pt_line_put`'s bit arm shipped at **199 cycles a pixel** against the packed
arm's 49, taking `OS8088.GIF` on a Hercules from 2,687 ms to 4,207. SPEC.md
§42.25 is the account and the fix — `xlatb` and a straight-line eight, for
**124.3 c/px** and 3,520 ms. The lesson is §42.13.1.4's unchanged: on an 8088
the loop costs what it is *spelled with*, and two `shr r16, cl` a pixel is 8+4
per bit, twice.

### Option A — 1bpp everywhere, `gfx_blit1` where it exists

The canvas format byte becomes three-valued (`packed4` / `planar4` / `packed1`).
`pt_blit` tries `GFX_BLIT1` and falls back on CF. Both builds; both adapters.

- **Big**: full speed on 1bpp adapters *and* on VGA (`gfx_blit1_pen` supplies
  the two colours there).
- **Small**: `gfx_blit1` always refuses, so small always takes the fallback.
  Small gets the memory and none of the speed.

### Option B — A, plus give kern_small `gfx_blit1`

Ungate `gfx_blit1_x` and its four shims: **~730 bytes** of kern_small's `.cold`,
which is resident (only `mod.inc` modules leave RAM). That is 1.8% of the
40.5 KB heap, spent to make a package 10× faster.

Today Paint would be the only beneficiary on that build — the band composer is
`BAND=1`-only and neither Word nor cword is on a small floppy. **It is the
architecturally right answer and the hardest one to justify on its own
arithmetic.** It gets much easier if anything else on kern_small ever wants a
composed band.

### Option C — A, with an expansion fallback for small

Fallback = expand one 1bpp row into a 224-byte 4bpp buffer and hand it to
`gfx_blit4`. ~80 bytes of code, a 16-entry word table (32 bytes), 224 bytes of
`.bss` → **~340 bytes**, all of it Paint's rather than the kernel's.

Predicted cost: ~112 table lookups a row, ~0.7 ms a row on a 4.77 MHz 8088, so
**~180 ms added to a full 448×258 repaint.** Whether it also costs a per-call
floor depends on whether the band is split per row or per band; a 224-byte
buffer means per row, which is 258 `gfx_blit4` calls where there is one today.
**That number is the one to measure first** — the far call is 46.7 µs (12 ms
over the canvas, nothing) but if `gfx_blit4`'s own fixed part is anywhere near
SPEC.md §5.7's 756 µs it is 195 ms instead, and the buffer should be a band.

### Option D — A, with an app-side run walk for small

Fallback = coalesce runs out of the 1bpp rows and emit one `OSAPI_GFX_HLINE`
each — the path `pt_blit` had before `gfx_blit4` existed. ~150 bytes, **no
buffer at all**, and on 1bpp it is *cheaper* than it ever was on nibbles: a run
boundary is a `repe scasb` over `0x00`/`0xFF`. Flat art is 1–2 runs a row.

It degenerates on dithered art to one `hline` a pixel, which is catastrophic and
has no floor. Option C degrades gracefully; D is faster when it is faster and
unusable when it is not.

---

## 5. The speed case — and a correction to PAINT-STROKE-PLAN §5

**One of that entry's four reasons is wrong, and it is the third.** It reads:

> An extended desktop cannot use planar anyway — a straddling window is one of
> `gfx_blitp`'s refusals, so there is no benefit there to win.

True of `gfx_blitp`. **Not true of `gfx_blit1`**, which does not refuse a
straddle: it detects one (`cw_vid_span_one` answering CF=1) and falls to
`.percol`, drawing per band column with each column resolving its own display
(SPEC.md §39.14.6). So the extended desktop is the one place a 1bpp canvas has a
fast path and the planar canvas can never have one. The other three reasons
stand.

**What the speed is actually worth**, off that same §5's item 3, whose maximize is measured
(Hercules, 448×258 → 670×258, total 1.107 s):

| | measured | at 1bpp (predicted) |
|---|---:|---:|
| `pt_wipe` | 0.148 | 0.037 |
| the copy-back | 0.228 | 0.057 |
| `pt_blit` — the canvas | 0.200 | ~0.03 |
| **those three** | **0.576** | **~0.12** |
| the whole maximize | 1.107 | ~0.66 |

The wipe and the copy are canvas-sized, so they fall by four with the canvas.
The blit falls further: 14,448 bytes at 12.5 clocks a byte is **37.9 ms**
predicted, against a run-coalesced 4bpp blit's several hundred.

**But read it honestly**: `pt_draw_pal` is 0.251 s of that same maximize and is
untouched by any of this. The speed case for 1bpp is a **40% cut to one
operation**, not a transformation — which is roughly what that entry
concluded, on worse evidence. **Memory is the argument. Speed is a bonus that arrives with
it.**

---

## 6. What this would cost Paint, in bytes

Measured per routine off a `-DAPP_SMALL` listing.

**6.1 What has to learn a second format.** Eighteen routines address canvas
pixels: `pt_setpx` (166), `pt_getpx` (93), `pt_rect` (475), `pt_line_put` (376),
`pt_fpix` (107), `pt_frow` (277), `pt_wipe` (112), `pt_cvspans` (230), `pt_seg`
(408), `pt_oval` (293), `pt_blit_1` (277), `pt_lose_w` (215), `pt_lose_h` (114),
`pt_cvrow` (146), `pt_cvmove` (177), `pt_cvwipe` (234), `pt_paras` (44),
`pt_layout` (127) — ~3,900 bytes, of which the depth arithmetic is a minority;
most of it is loop control that does not care.

**Big must carry both**, because a colour document must stay colour. The fork
sites already exist — `[pt_planar]` is tested at 13 of them — so the change is
2-way → 3-way, not nothing → something. **Predicted +800 to +1,400 bytes.**

**Small, if it is 1bpp-only, REPLACES rather than adds**, and 1bpp arithmetic is
generally the cheaper of the two: a byte is eight pixels, runs are byte-aligned,
`pt_wipe` becomes `rep stosb` of `0x00`/`0xFF`, and flood-fill run detection
stops being a nibble walk. Predicted **roughly break-even** on this set.

**6.2 The 4bpp-only gates for small — measured, and larger than expected.**

| gated out | bytes |
|---|---:|
| `pt_toplanar` + `pt_topacked` (whole routines) | 539 |
| `pt_rect`'s two planar arms | 272 |
| `pt_blit_1`'s §5.4.3.2 probe + `BLITP` arm | 159 |
| `pt_setpx` planar | 105 |
| `pt_getpx` planar | 70 |
| `pt_sparm` planar | 41 |
| `pt_fpix` planar | 33 |
| `pt_wipe` planar | 29 |
| the 13 `cmp byte [pt_planar], 0` tests + 4 bytes of `.bss` | ~69 |
| **the planar machinery, total** | **~1,320** |
| `pt_map16` + `pt_pal_rgb`, replaced by a luminance threshold | ~120 |
| a 16-swatch palette reduced to two | ~80 |
| `PT_BMPHDR` 118 → 62 (image, not code) | 56 |
| **total** | **~1,580** |

**The planar half of that is available TODAY and needs no 1bpp work.** On a 1bpp
adapter `[pt_planar]` is never set — `pt_geom` sets it only when `[pt_mono]` is
0, and §5.4.3.2's probe refuses a 1bpp adapter on its first guard — so the whole
1,320 bytes is dead code on every machine a small build is meant for. Gating it
costs a small-built Paint its planar speed-up *if someone runs it on kern_big and
a VGA*, which the "not a second ABI" rule permits and nobody does.

**6.3 Net.** Small: **~1,500 bytes smaller** and a canvas a quarter the size.
Big: **~1,000 bytes larger** and a canvas a quarter the size *for mono
documents*. Today small is 19,788 image + 3,525 bss = 23,313.

---

## 7. What already exists and does not need building

Three of the four things the design asks for are already in the tree.

- **`File > Save` shunting to `Save As`.** `[pt_trunc]` does exactly this, with
  the toast `'Cropped - use Save As'`, and it fences **both** paths — the menu
  item and SPEC.md §42.16's close question, the second having been the one a
  user reaches by accident. A colour down-convert sets the same byte. **Cost: 5
  bytes and a second string.**
- **Reading a 1bpp file.** `pt_bmp_in` and `pt_bmp_row` already do (§2.2).
- **A depth-agnostic decode seam.** `pt_line` is one byte per pixel, canvas
  width, and every file row already passes through it; `pt_line_put` is the
  single place a decoded row becomes canvas bytes. A 1bpp packer is one new arm
  in one routine, not a second reader.

What is *not* there: writing a 1bpp BMP header (the template is a fixed 4bpp
`pt_hdrtpl`), and the detection rule.

**The cheap detection rule**, and it should stay cheap: **a BMP with
`biBitCount == 1`, or a GIF whose global colour table has two entries, opens
1bpp. Everything else opens 4bpp.** Scanning a 4/8/24bpp image for "no other
colour in it" is a pass over the whole picture before the canvas is claimed, and
it buys a case (a monochrome picture saved at 8bpp) that is rare enough not to
pay for it. A new/blank canvas opens 1bpp on big per the owner's rule, and
always on small.

---

## 8. Open — settle these before building

**Items 1, 4 and 5 are answered and built; 2 is answered by being
overtaken.** What is left is 3, which is a design question rather than
a measurement.

1. ~~**Does `gfx_blit1` accept a NEGATIVE stride?**~~ **YES — ANSWERED, AND THE
   FIRST ANSWER WAS WRONG.** It was read as *no*, on the ground that
   `gfx_blit1_x` skips rows with `mul bp` in two places and `MUL` is unsigned;
   SPEC.md §42.23.4 shipped that reasoning and refused the fast path on it.
   Both sites `push dx` first and use only `AX` — the high half is deliberately
   discarded — and **the low 16 bits of a multiply are identical signed or
   unsigned**. Every other use of the stride there is a 16-bit `add` or `sub`.

   The tell was in the tree the whole time: `gfx_blit4` takes a negative stride
   from §42's 4bpp path, for exactly the same reason. **No kernel change was
   needed**, and the lesson is the cheaper one — an instruction's *name* said
   unsigned and its *use* did not, and reading only the name cost a section of
   SPEC.md that had to be withdrawn.
2. ~~**What does `gfx_blit4`'s own fixed part cost?**~~ **OVERTAKEN.** It
   mattered while the expansion loop was the *only* path; it is now the
   FALLBACK, taken on `kern_small` and on a canvas whose width is off the byte
   grid. `gfx_blit1` handles the rest a band at a time, so the per-row call
   count stopped being the thing that decides the repaint. Worth measuring if
   `kern_small` ever becomes the machine people watch a repaint on.
3. **What happens to a 1bpp document dragged onto a colour display?** It should
   stay 1bpp — depth is a property of the *document*, not the adapter, which is
   what the owner's rule already says and what makes the whole thing coherent.
   That differs from `[pt_planar]`, which follows the adapter both ways. Two
   format axes with different lifetimes in one byte is how this gets confusing;
   `[pt_fmt]` wants to be a small enum with the rule written next to it.
4. ~~**Where does a grey go?**~~ **ANSWERED AND BUILT — SPEC.md §42.23.1.**
   The third swatch came back as a **pattern rather than a colour**:
   `[pt_ncol]` is 3 on a one-bit canvas, and drawing with the grey swatch lays
   §39.4's checkerboard into the canvas itself. It is the one place a one-bit
   canvas is more truthful than a 4bpp one — a 4bpp canvas stores `CLGRAY` and
   the renderer dithers it on the way to the glass, so what is saved is not
   what was seen.

   **It also caught a defect in the first 1bpp build.** `PT_LIT16` was a
   guess — colours 7..15 white — and `gfx_inktab` says six of them are the
   DITHER class. So six colours were stored as flat white that every 1bpp
   screen draws as a checkerboard. `tests/unit/t_inktab.py` now re-derives
   both masks from the kernel's table, because `t_mirror` cannot see a `db`.

   The phase was verified against a dither the *kernel* drew in the same
   frame — the strip's own grey swatch — and both read white-on-even and
   **zero** white-on-odd. `paint1bpp` asserts it on the bytes rather than the
   screen, because a wrong phase is still a 50% checkerboard and a screenshot
   cannot tell them apart.

   One behaviour differs and is not a defect: a flood fill inside a dithered
   area fills the half matching its seed, because alternating pixels are what
   is actually there. §42.23.1 records it.
5. ~~**Option B or C for small.**~~ **NEITHER, AND THE QUESTION DISSOLVED.**
   It was posed as ~730 kernel bytes (give `kern_small` the `gfx_blit1` body)
   against ~340 package bytes (the expansion fallback) — on the belief that
   `kern_big` could not use `gfx_blit1` either, because of the negative
   stride. Once that turned out to be wrong, `kern_big` takes the fast path
   for nothing and `kern_small` takes the fallback that had to exist anyway.
   Option B is still available if `kern_small`'s repaint ever justifies 730
   bytes, but nothing now depends on it.
