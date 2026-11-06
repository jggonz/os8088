# Kernel hot-paths pass 1: mono, the 1bpp software renderer

Agent `mono`, branch `khp1-mono`, 2026-11-06. The brief is
`docs/plans/khp1/BRIEF.md`. This note follows its section 6.

The subject is `kernel/softgfx.inc`, the renderer CGA and Hercules draw
through: `sw_rect` and its pass (`sw_plane_op`, `sw_col`), `sw_xfer`
(gfx_save / gfx_restore), `sw_fill_pat`, `sw_spans`, `sw_blit_row` and the
1bpp arm of `gfx_scroll` (`vgas_bankcopy`, which is 1bpp-only code in
`vga12.inc` and so this agent's by the brief). `gfx_rect_setup` sits in this
file but is gfxcall's by the brief (the set-up every primitive runs), and was
not edited here.

## 1. BASE AND TIP

Base `6d6c23ae` (= `a7ee7fab`). The tip includes `kernel-hotpaths` at
`8f85f95c` (gfxcall, text, pointer, files and sched merged). The byte figures
below are this branch's own five commits, measured as routine SPANS in each
kernel's map (`kernel.map`, base against tip), not as rungs.

| kernel | `.text` | `.bss` | resident, this branch | where |
|---|---:|---:|---:|---|
| kern_big | +448 | +6 | **+454** | all `.text`, before `.cold` |
| kern_small | +94 | 0 | **+94** | all `.text`, before `.cold` |
| kern_emu | +448 | +6 | +454 | as kern_big (same `KERN_BIG` gates) |
| kern_mega | +448 | +6 | +454 | as kern_big; `.text`, not `.mega` |

**+454 bytes on kern_big land before `.cold`**, which is past the ~300-byte
line BRIEF.md section 8 asks to be reported: 344 of them are the bank walk
(mono-1). The merged tree at `8f85f95c` + this branch builds kern_big at
`KERN_SIZE` 98,304 of 100,864, kern_small 59,392 of 64,000; `make`, `make
small`, `make emu` and `make mega` all pass at the tip. kern_mega's resident
after the switch is 63,488 of 68,096.

Every item costs bytes, so every item is an `[+N]` commit, revertable alone
except mono-5, which edits a body mono-1 adds (see its row).

## 2. WHERE THE TIME GOES

### 2.1 The base, measured

`docs/reports/HOTPATH-BASE-2026-11-06.md` (bench) is the authority. Its line
for this subject is that the 1bpp backbuffer row loops (`sw_col`, `sw_xfer`,
`sw_plane_op`, `sw_rect`) are **25% of CGA's and 28% of Hercules's
interactive busy time**, second only to `sch_switch`. Per operation, CGA
then Hercules, share of busy time:

| operation | `sw_xfer` | `sw_col` | `sw_plane_op` |
|---|---:|---:|---:|
| raise a Disk window (179 / 229 ms) | 35.4 / 37.9% | 16.7 / 20.2% | 7.0 / 6.1% |
| drag a window (457-512 / 483 ms) | 13.6 / 17.1% | 26.2 / 28.0% | 8.4 / 7.6% |
| menu open (45 / 44 ms) | 16.3 / 18.1% | 4.3 / 3.9% | 5.5 / 5.6% |
| Disk window PgDn (131 / 132 ms) | 11.3 / 10.6% | 12.0 / 12.8% | 13.5 / 11.4% |

### 2.2 Which shapes, and why they cost what they did

A breakpoint probe on `sw_rect` through a raise, a drag and a close (CGA,
scratch `probe.py`) gives the shape mix: **one-row rects** (hlines, title
stripes, outline tops and bottoms) are the most frequent; **one-byte-column
rects** are the most ROWS (1x153 drag-outline sides, 1x155 window shadows);
then byte-aligned chrome fills (264x16 title bars, 13x13 boxes). Before this
pass every one of them went through the same three-pass body:

* `sw_col` on the left edge column, `sw_col` on the right, then the interior
  rows, each pass stepping rows with `gfx_nextrow`'s add / test / conditional
  wrap and toggling the dither pattern on every row. That is ~21 to 25
  instruction bytes a row a column, ~130-146 cycles a row on CGA for one masked
  byte (`GFX_VLINE 128px` = 31 us a row);
* a 64x64 aligned fill paid ~430 cycles a row for 8 bytes;
* `sw_xfer` moved bytes with `rep movsb` (17 clocks a byte on an 8088) and
  stepped through three CS-overridden memory reads a row;
* `vgas_bankcopy` (the scroll) paid `rep movsb`, a `call bp` to
  `gfx_nextrow`, a DS switch and a memory countdown per row: `GFX_SCROLL
  256x128` was ~1,208 cycles a row for 32 bytes.

### 2.3 What is left, at the tip (CGA, `os88hot profile`)

| operation | top routines, busy ms a rep |
|---|---|
| raise, 151 ms | `sw_xfer` 49.2 (33%), `sw_k_col` 9.8, `gfx_rect_setup` 7.5, `sw_k_lir` 5.7, `sw_k_i` 4.4, `sw_col` 4.4 |
| drag, 476 ms | `sch_switch` 134 (32%), `sw_xfer` 49.4, `sw_k_xcol` 40.2, `task_yield` 21.6, `sw_k_lir` 21.0 |

`sw_xfer` is now near its floor (about 19 cycles a byte against `rep movsw`'s
12.5 plus CGA's ~5-cycle VRAM wait; section 4). What remains of a raise is the
NUMBER of bytes the raise cache moves, which is wm's.

## 3. TAKEN

All MEASURED on MartyPC (`os88hot ab`, guest cycles, `-n 3` medians) unless
marked P. "Isolated" figures were taken against the previous commit;
"merged" ones against `8f85f95c` (all other agents merged, none of this
branch) and are the whole branch.

| id | routine / path | hotness | change | kern_big B | kern_small B | cycles base -> tip (input) | user-visible (operation, adapter: ms base -> tip) | M/P | 286 | commit | FREE / [+N] |
|---|---|---|---|---:|---:|---|---|---|---|---|---|
| mono-1 | `sw_plane_op` / `sw_col` -> bank walk (`sw_walk`, six row bodies), `kernel-hotpaths`' SPEC section 39.31 | H1 | rect walked as B bank phases, whole rows a pass, whole edges folded into `rep stosw`, one-row rects direct, XOR interior a word at a time; short rects keep the old body | +344 (338 `.text` + 6 `.bss`) | 0 | `GFX_FILL 64x64` 30,250 -> 14,870 cyc; `GFX_VLINE 128px` 21,330 -> 16,370; `GFX_FILL 8x8` 3,180 -> 3,250 (+2%) | raise CGA 179.1 -> 170.4, Herc 229.3 -> 214.2; PgDn CGA 131.2 -> 122.7, Herc 131.8 -> 127.7; menu open CGA 46.2 -> 40.9, Herc 47.6 -> 42.8; drag busy per pass CGA 75.0 -> 69.0, Herc 94.3 -> 73.0 | M | 86Box AT/286 raise: busy 23 -> 20 ms (whole branch, relative only) | `13aeb31a` | **[+344]** |
| mono-2 | `sw_xfer` (gfx_save / gfx_restore), SPEC.md 39.32 | H1 | `rep movsw` + `adc cx,cx`/`rep movsb` odd byte; row step and wrap bit in registers | +16 | +16 | ~26 -> ~19 cyc/byte (P from the raise) | raise CGA 170.4 -> 159.3, Herc 214.2 -> 198.8; menu open CGA 40.9 -> 40.2; menu close CGA 22.8 -> 22.2 | M | movsw also wins on a 16-bit bus | `58ea45aa` | **[+16]** |
| mono-3 | `vgas_bankcopy` (1bpp `gfx_scroll`), `kernel-hotpaths`' SPEC section 39.33 | H1 | whole-bank scroll walked bank by bank, register step, `rep movsw` | +56 | +56 | `GFX_SCROLL 256x128` 1,208 -> ~900 cyc a row | `GFX_SCROLL 256x128` CGA 30.40 -> 22.55, Herc 30.09 -> 22.85; PgDn CGA 122.7 -> 95.0, Herc 127.7 -> 111.2 (isolated) | M | 86Box AT/286 scroll: PgDn 16 -> 14 ms (whole branch) | `1eabf883` | **[+56]** |
| mono-4 | `sw_fill_pat` / `sw_patcol`, `kernel-hotpaths`' SPEC section 39.34 | H1 | row step inlined in all three loops, no DI push/pop, one-store masked byte | +22 | +22 | `GFX_FILL_PAT 64x64` 64,130 -> 46,140 cyc | `GFX_FILL_PAT 64x64` CGA 13.44 -> 9.67, Herc 13.08 -> 9.59 (Word's ruler, Disk-window chevrons) | M | - | `87574fa3` | **[+22]** |
| mono-5 | `sw_k_xcol` (drag outline sides) | H1 | two rows a `loop` | +16 | 0 | `XOR_RECT 256x128` 6.12 -> 5.58 ms | drag, `sw_k_xcol` 49.0 -> 40.2 ms of busy a drag (CGA) | M | - | `bab1b495` | **[+16]** (needs mono-1) |

**The whole branch on the merged tree** (`os88hot ab --base 8f85f95c`):

| operation | CGA | Hercules |
|---|---|---|
| raise a Disk window | 174.0 -> **151.1 ms (-13.2%)** | 222.2 -> **193.5 ms (-12.9%)** |
| drag a window (7 passes both sides) | 506.5 -> **475.6 ms (-6.1%)** | 532.4 -> **497.0 ms (-6.6%)** |
| menu open (File) | 45.2 -> **42.6 ms (-5.8%)** | 45.3 -> **40.0 ms (-11.7%)** |
| menu close | 21.2 -> 20.7 ms | 21.2 -> 23.3 ms (min 20.5; noise, see 8) |
| Disk window PgDn | 92.7 -> **88.5 ms (-4.4%)** | 111.0 -> **103.5 ms (-6.7%)** |
| open a Disk window on A: | 1,708 -> 1,716 ms (+0.5%, disk) | 1,753 -> **1,601 ms (-8.7%)** |

gfxbench on the merged tree, CGA / Hercules: `GFX_FILL 64x64` -51.0% /
-47.7%; `GFX_FILL_GRAY 64x64` -51.0% / -47.6%; `GFX_FILL 256x128` -32.2% /
-30.5%; `GFX_XOR_FILL 64x64` -33.6% / -31.5%; `GFX_XOR_RECT 256x128` -30.8% /
-26.4%; `GFX_FILL_PAT 64x64` -28.1% / -26.7%; `GFX_SCROLL 256x128` -25.8% /
-24.1%; `GFX_VLINE 128px` -23.1% / -18.8%; `GFX_FILL 256x1` -18.8%;
`GFX_HLINE 256px` -18.2%; `GFX_FRAME 64x64` -17.7% / -12.7%; `WM_TITLE strip`
-13.0% / -10.4%; `GFX_HLINE 8px` -1.7%; `GFX_PIXEL` 0.0%; **`GFX_VLINE 8px`
+2.1% and `GFX_FILL 8x8` +2.4%** (about +15 us, the five-instruction question
that keeps a short rect on the old body). Rows no item touches (RAM and VRAM
bandwidth, font, API cells) moved by 0.0%.

**Drag is reported per pass as well as whole**: the scenario's number of
outline passes (`unl`) varies between kernels with how fast each pass is
(5, 6 or 7 for the same packets), so a whole-drag figure taken across
different pass counts compares different work. The merged A/B above has 7
passes on both sides.

**Pixels.** A unicorn harness (scratch `uni.py`) loads each tree's
`kernel-full.bin` and map into its own machine, writes the CGA and Hercules
geometry as `vid_apply` would, starts both kernels from the same random
framebuffer, and calls `sw_fill`, `sw_fill_gray`, `sw_xor_fill`,
`sw_fill_pat`, `sw_save`, `sw_restore` (random sub-rect strides) and
`vgas_bankcopy` (both directions, whole-bank and not) with random rects:
clipped, one-row, one-column, single-pixel, byte-aligned, tall. It compares
the framebuffer, the caller's buffer and every register each contract
preserves. **Base `6d6c23ae` against the merged tip: 11,996 cases, 0
differ.** Every item had a negative control run: dropping the per-phase
pattern toggle fails 1,257 of 4,000; the odd byte dropped from the restore
fails 610 of 2,000; the odd byte dropped from the scroll fails 733 of 1,992.
`tests/swcolsame.py` (an A/B of this body against `NOCOLFAST=1`'s old
column-major one) is in section 8.

**CHANGES TRANSIENTS (order only).** mono-1 and mono-3 change the ORDER rows
land in inside one call: on CGA, the even rows of a rect, then the odd rows.
No pixel is written twice by either order, so `m.flicker`'s transient count
cannot rise. `deskflash` (section 8) is the row that watches it.

## 4. TRIED AND REFUSED

* **The bank walk for every rect.** The first form walked all rects. A
  phase costs ~250 cycles (three pushes, an indirect call, the next phase's
  `gfx_nextrow`, the pattern toggle, three pops), and that cost lost on short
  rects: `GFX_FILL 8x8` +17%, `GFX_VLINE 8px` +17%, `GFX_PIXEL` +7%. Kept:
  the threshold (walk from 9 rows with whole bytes, from 25 for one byte
  column) and the old body for the rest. A per-kernel threshold computed from
  B (`k << bshift`) cost +140 cycles on every short rect for the question
  alone; two constants cost ~48.
* **A load-modify-store edge byte** (`mov al,[es:di] / and / or / stosb`, 7
  cycles faster a byte). Refused by SPEC.md 39.25.1: an interrupt between the
  read and the store rewrites the bits outside the mask.
* **Bank order for `sw_xfer`.** The save buffer is row-major and published
  through `OSAPI_GFX_SAVE`. Walking banks would need per-phase buffer strides
  (save and the sub-rect restore both) to buy the wrap test only, about 20
  cycles of ~500 a row (P: ~4% of `sw_xfer`). See section 5.
* **Unrolling `sw_k_col` and `sw_k_colf`** as mono-5 did `sw_k_xcol`. P:
  -11% of a masked column's row and -15% of a whole one for +16 bytes each,
  worth ~1-1.5 ms a raise on CGA (`sw_k_col` is 9.8 ms of it). Not taken; the
  ratio is a third of mono-5's.
* **`sw_blit_row` (the 1bpp `gfx_blit4` decoder).** Its own header records
  `.abyte` at its fetch floor (37 bytes, 145 clocks per eight pixels against
  a 160.6 fetch floor) and costs the two remaining levers; nothing in this
  pass changes that arithmetic.
* **`sw_spans`.** One row a span by contract, so the bank walk does not
  apply. Its per-row cost is mostly `gfx_sp_next` in `vga12.inc`, shared with
  the VGA path.

## 5. WANTS AN ABI BREAK

* **`gfx_save`'s buffer in bank order** (SPEC.md 5's "same rounding and
  buffer layout"). With the buffer bank-major, `sw_xfer` could walk banks as
  mono-1 does, losing the per-row wrap test: P ~20 cycles a row, ~4% of
  `sw_xfer`, ~2 ms of a CGA raise. Callers: every `OSAPI_GFX_SAVE`/`RESTORE`
  user that indexes its own buffer. None found in `apps/`, but the layout is
  published. Bytes: +~40 (per-phase strides in both directions). Low value;
  recorded so nobody re-derives it.

## 6. WANTS THE OWNER

Nothing changes a settled picture or a result. The decision that is the
owner's is the five `[+N]` commits: +454 bytes on kern_big, +94 on
kern_small, best value first:

1. mono-3 (+56): -27.7 ms of a CGA PgDn on this branch's own base (much of
   it overlapped by other agents' changes on the merged tree: -4.1 ms there).
2. mono-2 (+16): -11 ms (CGA) / -15 ms (Hercules) a window raise.
3. mono-5 (+16): -8.8 ms a CGA drag.
4. mono-1 (+344): -9 to -15 ms a raise, -5 ms a menu open, -6 to -21 ms a
   drag pass; halves an aligned fill. The biggest gain and the biggest cost.
5. mono-4 (+22): -3.8 ms a 64x64 patterned fill; Word's ruler is the case.

## 7. CROSS-FILE

* **gfxcall: a frame-aware body for `gfx_frame` / `gfx_xor_rect`**, asked for
  in gfxcall's note section 7. Each of a frame's four strips pays
  `sw_rect`'s push set and mode tests, `gfx_rect_setup` and `sw_ink`: P
  ~4.6k of an 11x11 frame's ~10.7k cycles. One set-up for the two rows and one
  for the two columns would save ~2k cycles (~0.4 ms) a frame: ~6.5 ms of a
  CGA drag (16 outline frames) and ~1.6 ms of a raise. P ~80-100 bytes, with
  the four strips' independent clipping to reproduce. Not taken in this pass:
  about a tenth of mono-1's ms per byte, and the clip equivalence needs the
  strip head, which is gfxcall's. The bank walk sits under it unchanged.
* **wm: the raise cache's bytes.** At the tip `sw_xfer` is near its floor
  and is still 33% of a CGA raise (49 ms). The remaining lever is moving
  fewer bytes (a raise that restores only what was covered, SPEC.md 11.96.10,
  already does this; the save of the whole content is the other half).
* **gfxcall: `gfx_rect_setup`** is 7.5 ms of a CGA raise at the tip, now the
  third-largest drawing item there.

## 8. ROWS RUN

Covering rows, from `tests/suite.py`: `swcolsame` (the A/B of the bank walk
against `NOCOLFAST=1`'s column-major body, CGA and Hercules), `deskflash`
(transients), `fillpat` (mono-4), `wdmenusu` and `paintsu` (save-unders,
mono-2), `npscroll` and `drvscroll` (scrolls, mono-3), `win1bpp`, `blitpair`.
Run once after the last code commit with `--marty-jobs 1`:

**9 passed, 0 failed, 0 skipped in 379 s**: `fillpat` 7.0 s, `drvscroll`
16.1 s, `npscroll` 15.6 s, `wdmenusu` 25.9 s, `deskflash` 51.4 s, `blitpair`
12.3 s, `paintsu` 19.3 s, `swcolsame` 205.7 s (0 differing pixels, bank walk
against the column-major body, CGA and Hercules), `win1bpp` 25.6 s. On the
merged tip (`77d31f3c` + this branch's commits). The coordinator's union
should also carry `deskbench` on CGA and Hercules (a measurement, not a gate)
and any row that draws a dither fill on Hercules.

Measurement runs (`os88hot ab` time / profile / gfxbench on CGA and
Hercules, the 86Box 286 pair) are not rows. The menu-close medians move by
±2 ms with min values 20.5 / 21.1 on both sides; treat that row as noise.

## 9. DEFECTS FOUND

None in the shipped kernel. Two in this agent's own instrument, for whoever
writes the next one:

* editing a kernel source while an `os88hot` run is live makes every later
  rep of that run die on os88sym's "the map describes a DIFFERENT kernel".
  `os88sym` re-assembles the TREE's sources. Edit in a second worktree while
  a measurement runs;
* section padding hides the cost of a change: `.text` read 40,961 with and
  without a parity pad. Price by routine span (`kernel.map`), which this
  note does throughout.
