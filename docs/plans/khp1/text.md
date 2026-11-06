# Kernel hot-paths pass 1: text (`kernel/font.inc`)

The `text` agent's record (BRIEF.md section 6). Branch `khp1-text`, cut from
`6d6c23ae`. Owns `kernel/font.inc` entire: `font_run`/`font_run_x`,
`font_run_cell`, `font_run_scell`, `font_char`, `font_char_bb`, `font_ink`,
`font_str_x`, `font_width`/`font_width_x`, `fnt_rn_edge`. There is no
proportional or `.F88` renderer in the kernel: `.F88` faces are drawn by the
package library `apps/os88type.inc` (SPEC.md 6.5), outside this row.

**How the numbers were taken.** Two instruments, both MartyPC guest cycles
(exact under any host load, 209.6 ns a cycle at 4.77 MHz):

* **A park rig** (`os88linecost.py`'s method): boot a desktop, write a
  `call <routine> / jmp $` stub over `fsx_caps`, park the CPU on it (IF = 0)
  with the routine's registers, and read the cycle counter either side. 39
  cases per adapter - aligned, skewed, short, long, padded, disabled, colour
  pairs, off the right edge, `font_char`, `font_str_x`, `font_width_x`, and
  four ARMED clip-region shapes (one fragment, a horizontal cut, a vertical
  cut, a hole) - each drawn in a band of its own, median of three; then the
  whole framebuffer (all four planes on VGA) is captured and compared with
  the base kernel's, byte for byte. Every item below is **FB IDENTICAL on
  CGA, Hercules and VGA** on that rig. The script is scratch (not committed):
  it is `tools/os88linecost.py`'s Rig with the font routines' registers.
* **`tools/os88hot.py ab --base 6d6c23ae time <scenario> -n 3 --all`** for
  the user operations (busy ms, the medians).

`os8088_5150_cga_gla`, `os8088_5150_herc_gla`, `os8088_xt_vga`.

## 1. BASE AND TIP

Assembled by `make` / `make small` / `make emu` / `make mega` at `6d6c23ae`
(base) and at this branch's tip. The tip carries the coordinator's
`kernel-hotpaths` merges, whose only kernel change is `mod_cfix` (+2 `.cold`
on kern_big and kern_emu), so the `.cold` columns below are that and not this
branch.

| kernel | `.text` base -> tip | `.bss` | other | resident base -> tip | `KERN_SIZE` / budget base -> tip |
|---|---|---|---|---|---|
| kern_big | 40,631 -> 40,975 (**+344**) | 5,377 -> 5,375 (-2) | `.cold` +2 (merge) | 91,530 -> 91,874 (+342 this branch) | 96,768 -> **97,280** / 100,864 (one rung) |
| kern_small | 30,220 -> 30,560 (**+340**) | 2,945 | - | 57,258 -> 57,598 (+340) | 59,392 -> **59,904** / 64,000 (one rung) |
| kern_emu | 40,885 -> 41,229 (+344) | 5,377 -> 5,375 | `.cold` +2 (merge) | +342 | 97,280 -> 97,792 / 100,864 |
| kern_mega | 45,853 -> 46,201 (+348) | 5,415 -> 5,413 | `.lowbss` -2 | +344 | resident (switched) 62,976 -> 62,976 / 68,096 |

**Of the +344 on kern_big, +370 is the three owner's picks** (`[+362]`,
`[+6]`, `[+2]`) and **-26 is the free items**. Revert the three picks and the
branch is 26 bytes of `.text` and 2 of `.bss` SMALLER than the base on
kern_big, 6 smaller on kern_small. All of it is `.text`, i.e. BEFORE `.cold`
(BRIEF section 8's COLD_HOME note): **this branch alone adds 344 bytes before
`.cold` with the picks, -26 without them**, and `[+362]` is the whole of the
excess over the ~300 the coordinator asked to hear about.

## 2. WHERE THE TIME GOES

### 2.1 Inside the routines (park rig, base, CGA; Hercules within 1%)

| case | cycles | us | what dominates |
|---|---:|---:|---|
| `font_run_x`, 1 cell aligned | 5,334 | 1,118 | **the per-RUN floor**: prologue ~2,000 (fnt_unlazy + 7 pushes 274; GFXDENTER/stores/edge tests 230; **two `font_ink` calls ~550**; gfx_rowbase 239; clip query + pops 219), and 8 row passes at ~300 each of which ~185 is the row's own overhead |
| `font_run_x`, 10 cells aligned | 14,280 | 2,992 | floor ~4,350 + ~990 a cell (pass 1 ~200 a cell, pass 2 ~100 a cell row) |
| `font_run_x`, 78 cells aligned | 81,464 | 17,069 | per cell |
| `font_run_x`, 78 cells at x&7=5 | 126,714 | 26,550 | ~1,540 a cell: the unaligned walk's per-cell `cmp si,[cs:send]` |
| `font_char` 'W' | 3,669 | 769 | ~1,750 overhead, 8 rows ~240 each: the row advance's three CS memory operands are ~70 of them |
| `font_str_x`, 10 cells | 35,853 | 7,512 | **3,585 a cell, ~80% of it per-cell overhead** |
| `font_str_x`, a 17-cell title at x&7=3 | 62,728 | 13,143 | the same |
| `font_width_x`, 40 chars | 2,621 | 549 | ~65 a character |

VGA (`os8088_xt_vga`): `font_char` 2,961, `font_str_x` 10 cells 29,373, a
`font_run_x` 10 cells 13,100 (planar prologue), 78 cells 75,932. A colour
pair sharing no plane (CDGRAY on CLGRAY) takes `.slow`: 35,404.

### 2.2 In user operations (base; `os88hot` and an entry-to-return profiler)

| operation (CGA) | span / busy | text share | text calls |
|---|---|---|---|
| a Disk window's File menu, open | 93 ms span | **27%** (25.0 ms) | 8 `font_run_x` (14,137 cycles each), 8 `font_width_x` (1.3 ms) |
| desktop File menu, open (`os88hot menu`) | 36.0 ms busy | `font_run_x` 20-22% (7.1 ms) | - |
| raise a background Disk window | 128 ms span | 6% (7.3 ms) | 2 captions through `font_str_x` (6.6 ms), 8 `font_width_x` |
| one key into Note Pad | 6.9 ms span | 34% (2.3 ms) | 1 `font_run_x` |
| 31 keys into Note Pad (`os88hot type`) | 617 ms busy | `font_run_x` 5.6% (34 ms) | - |
| PgDn in a Disk window (`os88hot scroll`) | 131 ms busy | `font_run_x` 20-26% (bench's report) | - |

**Ranking for this file**: (1) `font_run_x`'s per-run floor (~0.9 ms a run
on a 5150, paid by every menu item, list row and echoed key); (2)
`font_str_x`'s per-cell overhead (every caption and every package's
transparent text, ~0.75 ms a cell); (3) `font_run_x`'s per cell; (4)
`font_width_x` (menu layout, ~1 ms a menu open); (5) `font_char` on VGA.

## 3. TAKEN

Cycles are the park rig's (the routine alone); user-visible is `os88hot ab`
busy medians over three reps unless marked. **M** = measured, **P** =
predicted.

| id | routine / path | hot | change | kern_big B | kern_small B | cycles base -> tip (input) | user-visible | M/P | 286 | commit | FREE / [+N] |
|---|---|---|---|---:|---:|---|---|---|---|---|---|
| text-1 | `font_ink` | H2 | `[font_dith]` = `[gfx_dis]` (one store); `gfx_ink`'s lookup inline; 00/FF by `add al,1 / sbb ch,ch` | -1 | -6 | part of text-2's row | per transparent glyph | M | same direction (call/ret and a branch gone) | b35e32d6 | FREE |
| text-2 | `font_char_bb` | H1 | row step and wrap bit in DX/BP (pushed once a cell), not CS memory operands; the disabled loop's checkerboard DL -> CH | -6 (+1 PARITY pad) | -6 | `font_char` 'W' 3,669 -> 3,314 (-9.7%); `font_str_x` 10 cells 35,853 -> 32,303 (-9.9%) CGA, Herc the same | caption, CGA: ~1.0 ms off a 17-cell title | M | wins more (word operands, no EA) | b35e32d6 | FREE |
| text-3 | `font_run_x` prologue | H2 | the two `font_ink` calls and the `[gfx_dis]` save/clear/restore become an inline `gfx_inktab` reduction; xm/bm one word store | (commit 7dec8353 net: kern_big -2, kern_small 0) | | run1 5,334 -> 4,536 with text-4/5 | with text-4/5 below | M | same | 7dec8353 | FREE |
| text-4 | `font_run_x` pass 1 | H2 | how many cells fit is asked ONCE ((cwm8-x)/8+1, capped at FONT_RN_MAX+1); a cell is one `loop`, one unsigned range test, printable falls through | (in 7dec8353) | | ~100 cycles a cell | with text-5 | M | same | 7dec8353 | FREE |
| text-5 | `font_run_x` pass 2 | H2 | table biased +8, BP counts -8..0 (`inc`/`jnz`, three loops); the one-display door skips GFXDENTERCD's second test | (in 7dec8353) | | text-3..5 together: 10 cells 14,280 -> 12,582 (**-11.9%**) CGA / 14,240 -> 12,542 Herc / 13,100 -> 12,098 VGA; 78 cells -10.4%; padded 20 -16% | **menu open busy CGA 36.0 -> 35.3, Herc 36.1 -> 35.0, VGA 39.3 -> 38.3 ms; PgDn CGA 131.2 -> 126.5, Herc 131.9 -> 127.0, VGA 104.1 -> 100.8 ms** (whole branch) | M | same | 7dec8353 | FREE (the commit: -2 / 0, incl. a PARITY pad) |
| text-7 | `font_run_x` unaligned walk (SPEC.md 6.1.11) | H1 | previous composed byte parks in BH (the next `xchg ax,bx` hands it back as AH), CH counts the cells: the per-cell `cmp si,[cs:font_rn_send]` and `mov ah,ch` go; `font_rn_send`/`font_rn_sendm` retire | -18, bss -2 | 0 | 10 cells at x&7=5 21,836 -> 19,846 this commit (base 23,534, **-16%**); 78 cells skewed 126,714 -> 102,390 (**-19%**); VGA 21,770 -> 18,778 | every off-grid run: captions after wm's `[+24]`, Disk window names in a dragged window | M | same | 6729dd47 | FREE |
| text-6 | `font_str_x` -> new `font_str_run` | H2 | a transparent string in ONE row-major pass (SPEC.md 6.1.13): pass 1 into `font_rn_tab`, a row of the whole string at a time, the byte two cells straddle merged once; anything not provably identical returns CF=1 and the old walk runs | **+362** | **+346** | 10 cells 35,853 -> 16,103 (**-55%**), 17-cell title at x&7=3 62,728 -> 26,523 (**-58%, 2.4x**), in a one-fragment region -63%; CGA and Herc alike. Cut by a region: +4% (pass 1 wasted). VGA: +0.3% (the door) | **raise a Disk window, busy: CGA 170.3 -> 168.0, Herc 221.9 -> 219.0 ms** (whole branch; the raise letters two short captions); a 17-cell caption 13.1 -> 5.6 ms on the XT | M | same | c01cde29 | **[+362] OWNER'S PICK** |
| text-8 | `font_width_x` | H3 | `repne scasb` | **+6** (5 + PARITY) | **+6** | 40 chars 2,621 -> 810 (-69%), same AX | ~0.9 ms of a menu open's layout (8 strings), P | M (routine) / P (op) | wins (string instructions are cheap on a 286) | 1139897c | **[+6] OWNER'S PICK** |
| text-9 | `font_char` VGA writer | H1 | GC index set to 8 once a cell, masks as one-byte outs to 3CFh; column mask in BL | **+2** | 0 | `font_char` 2,961 -> 2,775 (-6.3%), skewed -8.3%; `font_str_x` VGA -5 to -6.5% | VGA only: transparent text, P ~0.2 ms a caption | M | same | 499dfc5c | **[+2] OWNER'S PICK** |

Whole-branch `os88hot ab` (busy medians, three reps): menu open CGA -2.0%,
Herc -3.2%, VGA -2.7%; menu close CGA -7.3%, Herc +4.1% (one rep's
alignment: the close's text is one run); PgDn -3.6% / -3.7% / -3.2%; PgUp
-3.1% / -3.0% / -2.4%; raise -1.3% / -1.3% / -0.1%; typing 31 keys -0.9% /
-0.8% / +0.4% (VGA: Note Pad's text is planar, untouched but for the door).

**The owner's picks, best value first**: `[+6]` (0.9 ms a menu open for 6
bytes), `[+362]` (every caption and every package's transparent string 2.2-
2.4x on the two mono adapters; 2.3-2.9 ms off a CGA/Hercules raise; it is
the only pick that crosses a rung on its own, on both kernels), `[+2]`
(VGA only, -6% a transparent glyph). Note wm's `[+24]` turns an UNCLIPPED
caption into one `font_run_x`; with it, `[+362]`'s caption share shrinks to
clipped captions, and what is left is every package's `OSAPI_FONT_STR_XPARENT`
(Paint, Solitaire, ArtfulType and the 70 registered kernel sites).

## 4. TRIED AND REFUSED

* **`lea bp, [bx - font_rn_tab]`** to count pass 1's cells (-2 bytes): NASM
  refuses a negative label in an effective address under `-f bin`.
* **Doing `font_str_run`'s clip test before pass 1** (to stop the +4% on a
  cut string): it needs n, which is what pass 1 computes; an over-estimate
  from the screen's room (L) fails the containment test for almost every
  caption, so the fast path would be lost to save 1,100 cycles on the rare
  cut one. Not taken.
* **Unifying `font_str_run`'s set and clear loops** by
  `new = old ^ ((old ^ ink) & bits)`: -50 bytes, +~25 cycles a written byte
  (an extra framebuffer read). The shared row structure with two 25-byte
  inner loops was taken instead.
* **A plain `font_run_x` row loop for `xm = FF`, `dt = 0`** (drops `and al,dl`
  a cell row and `xor dl,[cs:font_rn_dt]` a row): PREDICTED ~9% of a mono
  run for ~60 bytes (two more loop copies). Not taken: under 1 ms a menu.
* **Unrolling pass 2 by row** (`mov al,[bx+r]`, frees BP): ~10% of a cell row
  for ~120 bytes. Not taken.
* **Inlining `fnt_unlazy`** at its two sites: ~35 cycles a run or glyph for
  +6 bytes. Not taken (0.3% of a run).

## 5. WANTS AN ABI BREAK

None found that pays. Two considered and priced:

* **A length-counted `OSAPI_FONT_RUN` (CX = cells)** instead of ASCIIZ:
  pass 1 would drop its `or al,al / jz` a cell, ~14 cycles of ~150, under 1%
  of a run. Not worth a cell contract.
* **`OSAPI_FONT_CHAR_XPARENT` per cell** is the expensive door for a package
  that letters strings a character at a time (~3,300 cycles a cell on mono
  against ~1,200 inside `font_str_run`) - but the cure is the package using
  `OSAPI_FONT_STR_XPARENT`, which already exists, not a new cell.

## 6. WANTS THE OWNER

Nothing changes the settled picture or a result. Only the three `[+N]`
picks above.

## 7. CROSS-FILE

* **gfxcall (`vga12.inc`, `CLIPQ`)**: on kern_big `CLIPQ` is
  `call gfx_clip_query` (`cmp [wm_clip_n] / jne / cmp [gfx_hole] / je / ret`),
  ~110 cycles; `font_char` pays it per glyph, `font_run_x` once a run plus
  again inside `wm_clip_rows`/`wm_clip_test` when armed. A macro expansion is
  ~13 bytes a site against 3 and saves the call/ret (~35 cycles) - ~1% of a
  glyph, ~0.8% of a 10-cell run. PREDICTED.
* **wm / clip (`wm_clip_rows`)**: a run an armed region CUTS goes per cell
  through `font_run_cell`, which asks `wm_clip_rows` about every cell:
  measured `run clip hcut` (10 cells, a horizontal cut) **51,428 cycles
  against 13,154 inside one fragment (3.9x), ~5,100 a cell**, and a skewed
  one 164,596. A damage repaint of a partly covered window is this path. A
  run-level answer (the cut's row range asked once for the whole run, when
  every cell would get the same answer) would make it one row-pass at the
  run's rows; it needs `wm_clip_rows`' per-cell semantics (best single
  fragment, 11.3.4.3's growth, kern_big's column mask) proved equal for the
  run, which is wm's file and a design question.
* **wm (`wm_draw_title`)**: the caption is `gfx_fill` + `font_str_x`. With
  `[+362]` that pair costs ~5.6 ms for a 17-cell caption on CGA instead of
  13.1; wm's own `[+24]` (one `font_run_x`) is cheaper still where it
  applies, and the two compose (`[+362]` keeps the clipped captions fast).

## 8. ROWS RUN, AND THE ROWS THAT COVER THIS

Run ONCE after the last code commit, `python3 tools/os88test.py soak -k ...
--marty-jobs 1`: **17 passed, 0 failed** (`megafont`, `disptitle`, `thm95`, `dispnp`, `fontpick`, `dispseam`, `dispstrad`, `disptext`, `editmove`, `fdlggrey`, `cplistrow`, `glyphcost`, `vidgrey`, `vidgreyherc`, `dmgcull`, `tmdmg`, `runclip`; 1,133 s). `make` (fast tier), `make small`, `make emu` and `make mega` pass at the tip; `tools/stkbalance.py kernel/font.inc` reports the one path the base already has (`font_run_x`'s tail jump to `kret_bp`).

**286 (86Box `--cpu 286`, CGA, RELATIVE, 1 ms sampling)**: PgDn busy 19 -> 19 ms, PgUp 19 -> 18 ms - no regression at that resolution. Every change is fewer instructions or fewer memory operands on any x86, so none should lose on a 16-bit bus; the PARITY pads keep the pinned routines' word alignment.

Covering rows: `runclip`, `dmgcull`, `disptext`, `disptitle`, `dispseam`,
`dispstrad`, `dispnp`, `glyphcost`, `cplistrow`, `tmdmg`, `fdlggrey`,
`vidgrey`, `vidgreyherc`, `megafont`, `fontpick`, `thm95`, `editmove`, and the
fast tier's `fonts`, `textrules`, `vidext` (re-registered for
`font_str_run`'s planar test), `hotpar` (no pinned routine moved parity: four
PARITY pads keep the downstream list even on both kernels).

## 9. DEFECTS FOUND

* `tools/os88sym.py --all` crashes on a symbol in section `.cfx` ("no
  segment in SECTION_SEG"); `os88sym.syms()` and `linear()` are fine. Not
  fixed (a tool outside this row).
* None in `font.inc`.

## 10. FOLLOW-UPS FROM gfxcall's NOTE (section 7), after the merge of kernel-hotpaths 8f85f95c

* **(1) `.nosplit`'s GFXDENTERCD re-testing `[vid_ndisp]`**: already TAKEN as
  part of text-5 (7dec8353): on one display the split test's `jbe` lands on
  `.onedisp`, past the macro. Nothing further to do.
* **(2) Inlining `gfx_nextrow` in font.inc's row loops**: font.inc has **no**
  `call gfx_nextrow` left - every row loop already inlines it
  (`FONT_RN_NEXTROW`, `font_char_bb`, `font_run_cell`, `fnt_rn_edge`), and
  text-2 moved `font_char_bb`'s into registers. **font.inc's share of the
  1.0-1.9 ms is 0.** The remaining callers are `icons.inc` (two: the 1bpp
  icon pass's `.fnext` at :744 and the AND/OR row walk at :804) and
  `softgfx.inc` (two, mono's file). CROSS-FILE for files: inlining the
  three-instruction body at each `icons.inc` site is +14 bytes a site
  (17 bytes of body against the 3-byte call) and saves the call and ret,
  ~35 clocks plus ~3 bytes of fetch, ~45 cycles an icon row - for a 32-row
  icon ~1,400 cycles, ~0.3 ms a pass on the XT, so ~4 ms across a Disk
  window of a dozen icons if each pays one pass. PREDICTED; +28 bytes for
  both sites.

Rows run once after the merge: `disptext`, `dispseam`, `runclip` pass;
**`glyphcost` FAILS** - `radio, set` through fills costs +3.295 ms over the
bitmap against a 2 ms bound (FILLS 256,579 cycles). It passed at this
branch's own tip (eb353e57, section 8), so it came in with the merge
(pointer / gfxcall), not with font.inc: the check and radio glyphs are drawn
with gfx fills, not text.
