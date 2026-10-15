# ksp11 agent "caption" - `kernel/wm.inc`, SPEC.md 11.3.4.3

Kernel size pass 11. Concept: `f8ba8eb3` ("font-bug: captions keep their rows
beside a covering window's corner"), which added the row GROW to
`wm_clip_rows` (+92 `.text` on kern_big, 0 on kern_small) and had no size
pass. Branch `ksp11-caption`, cut from `elendilon` at `2a05e31f`. Figures are
assembled bytes, `tools/kernsize.py --json`, against the base's absolutes
(NOT kernsize's printed deltas, which are against pass 10's blessed close).

## BASE AND TIP

| | base `2a05e31f` | tip | delta |
|---|---:|---:|---:|
| kern_big `.text` | 43,428 | 43,400 | **-28** |
| kern_big `.bss` / `.cold` / `.lowbss` / `.vgabuf` | 5,190 / 37,943 / 5,598 / 336 | same | 0 |
| kern_big resident | 92,495 | 92,467 | **-28** |
| kern_small `.text` | 32,022 | 32,014 | **-8** |
| kern_small resident | 61,442 | 61,434 | **-8** |
| `KERN_SIZE` big / small | 98,304 / 63,488 | same | no rung moved (image rung 44 -> 72 left on big) |
| kern_emu `.text` | | 43,659 | **-28** (the same routine; `make emu` assembles at the tip) |
| overlay, modules, drivers | | | 0 (nothing outside `wm_clip_rows` touched) |

## WHY KERN_SMALL WAS 0

The grow is `%ifdef KERN_BIG` on purpose, not by accident of path: SPEC.md
11.3.4.3 says *"kern_big only, for 11.3.4.1's and 39.27.4's reasons"* -
kern_small is on a diet (39.27.4: bytes returned stay returned, none may be
spent), and its `wm_clip_rows` is the pre-11.3.4 full-width walk with no
winner bank, so the grow would have to be built for it from scratch (the same
~90 bytes). The defect is the same shape there; it is left alone, as the
merge decided. kern_small's -8 here is the SHARED exit restructure (below),
not the grow.

## TAKEN

One commit, all inside `wm_clip_rows`. -28 kern_big, -8 kern_small.

* **The exit moved ABOVE the walk** (both kernels). `.ok`/`.none`/`.out` sat
  after the mask block, so every jump to them crossed the whole tail: on
  kern_big that cost 11.3.4's two head trampolines (`.allj: jmp .all` and
  `.nonej: jmp .none`, both NEAR - 3 bytes each plus their 2-byte `jcc`s) and
  forced 11.3.4.3's grow OUT OF LINE (`jne .grow0` + `jmp .mask` back). Now
  `.all`, `.ok` and `.out` follow the head's `jc` directly: the head's `je` /
  `jc` reach them short, the refusal at the end of the walk is a short
  BACKWARD jump however long the tail is, and the grow sits in line and FALLS
  into the mask block (`je .mask` skips it for a spanning winner). kern_big
  -6 trampolines -3 grow jumps; kern_small loses its `jmp short .all`.
* **The refusal's carry is the answer** (both): `or bp, bp / jz .none` +
  `.none: stc` became `cmp bp, 1 / jb .out` (CF = 1 exactly when BP = 0),
  and the head's `jc` goes straight to `.out` with its CF = 1. `clc` /
  `jmp short .out` / `stc` are gone from the exit; `.all` keeps one `clc`
  because its `sub` borrows when y1 < 0 <= y2 (the side-by-side harness
  found that - see below).
* **`xchg ax, bp`** (both) for `mov ax, bp` at the mask/walk arrival: AX and
  BP are both restored by the epilogue. -1.
* **r0 and rn in ONE word store on kern_big.** The three CF = 0 arrivals all
  bring AL = rows, AH = r0 (`.all`: AH = 0 since rows <= 255; spanning:
  BP < 256; grown: the tail packs `BP = rows | r0 << 8`), and `.ok` is
  `xchg al, ah / mov [wm_clip_r0], ax` - the pair is adjacent by the
  existing assertion. So the entry `mov byte [wm_clip_r0], 0` (5 bytes, on
  EVERY call including the refusals) is kern_small-only, and the grow's
  `mov [wm_clip_r0], bl` is gone. Consequence: on kern_big a REFUSAL no
  longer zeroes r0. Every caller (font_char, font_run_cell, font_run_scell,
  gfx_blit1_x) reads r0/rn only on CF = 0 - checked at each `jc`, including
  `.pcq`/`.percol` and `font_ch_drop` - and SPEC.md 11.3.2's contract never
  promised it.
* **The grow's clamp** keeps the bottom as "the row below" (`y2 + 1` pushed,
  not `y2`), so the `dec dx` before the clamp and the `inc` after the
  subtract both go. -2 net with the packing.
* **The seed and the downward merge are one body**: the seed's last two
  instructions (`mov dx, [si+WCR_Y2] / inc dx`) ARE the merge with SI = the
  winner, so `.gdn` is a label on them and the merge's own copy plus its
  `jmp short .grow` are gone. -6.
* **The mask block's `xchg cx, dx`** (11.3.4's, not this concept's): d1 is
  made in CX first, where the first shift wants it, and d0 in DX, so the
  shifts run `shl ah, cl` (d1) then `mov cl, dl / shr al, cl` (d0). -2, a
  little faster.

**Equivalence is PROVED by execution, not argued.** A scratch harness (not
committed; `rowsim.py` in the session scratchpad) loads the base kernel and
the tip into unicorn, sets `wm_clip_tab`/`wm_clip_n`/`wm_dmg_cull`/
`gfx_dnest`/`vid_ox`/`vid_oy`/`gfx_hole` from the map, and calls
`wm_clip_rows` in both with the same registers over 30,000 cases - half
random well-formed rect lists, half regions built by `wm_clip_split`'s own
strip cut (a window minus up to four occluders), cells 8 wide and 1..14 tall
anywhere around them, 5% disarmed, 10% culled, 10% under a display hook with
an offset. It compares CF, r0, rn, cm on CF = 0, and asserts all seven
registers and SP come back. **Identical in all 30,000.** It was shown to have
teeth: removing the grow-UP arm fails it at case 863, and the first build of
this change failed it at case 224 (the `.all` borrow above).

Instructions per call (unicorn's count, includes `gfx_clip_query` and
`kret_bp`), median old -> new, and no path takes more branches:

| path | n | old | new | bytes executed |
|---|---:|---:|---:|---:|
| refused (walk found nothing) | 18,900 | 63 | 61 | -5 |
| spanning (no grow) | 3,186 | 105 | 102 | -7 |
| cut / grown | 3,405 | 223 | 218 (up to -10) | -13 |
| disarmed | 1,502 | 33 | 31 | -8 |
| culled | 3,007 | 78 | 75 | -9 |

**kern_small and kern_emu were run through the same harness** (their own
base and tip, assembled with `-DKERN_SMALL` / `-DKERN_EMU`): identical over
30,000 and 5,000 cases. kern_small, median instructions old -> new: refused
55 -> 54, spanning 64 -> 62, cut 101 -> 99, disarmed 26 -> 25, culled 69 ->
67.

Taken branches per path: refused 1 -> 1 (`jz` -> `jb`); spanning 2 -> 2
(`jmp short .ok` + `jmp short .out` -> `je .mask` + `jmp .ok`); cut 4 -> 1
(outside the grow loop itself);
disarmed 3 -> 1; culled 2 -> 1. Not measured on MartyPC: every path is fewer
instructions, fewer bytes and no more taken jumps, so it cannot be slower on
the 8088's model, and 11.3.4.3's table (802 / 1,314 / 1,730 / 2,346 cycles)
is the upper bound.

## REFUSED

* **Dropping the spanning check (9 bytes)**: the grow on a spanning winner
  gives the same answer, but costs a whole extra walk (~600 cycles, 11.3.4.3's
  "partly cut, nothing to grow" row) on the common clipped cell. No.
* **Clamping during the grow instead of at the end**: a merged fragment can
  overshoot the cell either way, so the end clamp stays whatever the grow
  does; every spelling measured longer.
* **A "running total" in the main walk instead of the grow walk** (11.3.4.3
  names it): full-width union contiguous => answer it with mask 0FFh. Exact
  for every cell no occluder overlaps, but ~3 words of state and ~30 bytes in
  the walk against the grow's ~70, AND it under-draws the corner cell more
  than the grow in one orientation (a taller partial side piece no longer
  inherits the strip's rows). Bigger and worse.
* **Sharing the grow-UP merge with the seed** as `.gdn` is shared: the seed
  must set both ends, and an up-merge that fell into the down half would
  shrink the range. Only one direction can share.
* **Swapping `wm_clip_r0`/`wm_clip_rn` so the word store needs no `xchg`**:
  the order is asserted and four readers (`font_run_cell`, `font_run_scell`,
  font_char's `.noclip`, `gfx_blit1_x`) take `AL = r0, AH = rn` off one word.
* **Avoiding `.all`'s `clc`** (1 byte): `mov ax, dx / sub ax, bx` borrows
  when the cell crosses y = 0; every flag-free spelling of the row count is
  longer than the `clc`.
* **`wm_hit`'s relaxed `je .none`** (pre-existing): a short stub beside
  `.minbox` is -1 at best and moves code inside the hit test. **`wm_tpen_bg`
  / `wm_tpen_ink`**: their relaxed `je thm_t*` is already the minimum (two far
  targets). **`wm_dmg_wins`' `jae .draw`**: rotating the loop is the same 8
  bytes. None taken.

## DEFECTS

None found in the concept. The fix stands as 11.3.4.3 describes; the corner
cell keeps its accepted under-draw.

## OUTSIDE THE CONCEPT: pass 10's `dskwstage` leftover (0 bytes)

Pass 10's record 5 asked for `tests/dskwstage.py` to breakpoint
`dskw_xclus.stg` directly and for `kernel/diskw.inc`'s two `equ` aliases
(`dskw_wdata.stg`, `dskw_rdata.stg`) and their comment to go. Done in its own
commit: the test names the label once (`STG`), its messages say "the write's
/ the read's staging arm", and `build/kernel.bin` is byte-identical before
and after (`md5sum`). `diskw.inc` is the diskwrite agent's file - the hunk
is the six lines at the old 2160-2165 and nothing else, so a merge conflict,
if any, is "delete these lines". `dskwstage` green.

## CROSS-FILE

None. Nothing outside `wm_clip_rows` changed, no contract another concept
relies on moved, and `wm_clip_r0`/`wm_clip_rn`'s adjacency (the only thing
the new store leans on) is the assertion already in `wm.inc`.

## WHAT WAS RUN

* `make -j2`, `make -j2 small`, the fast tier (61/61) at the tip.
* `tools/stkbalance.py kernel/kernel.asm kernel/*.inc`: 0 unbalanced, base
  and tip.
* The unicorn side-by-side above (30,000 cases, identical).
* Soak rows at the tip (`82fd9ede` and on): `clipgrow` (the concept's own
  gate, 11.3.4.3) ok; `runclip`, `runclipcga` (11.3.4.2), `clipkeep`,
  `zonedmg`, `dmgcull` (11.3.3's cull arm) ok; `wmchrome` ok; `dskwstage`
  ok. **`deskflash` 3 of 4**: the one red read *"cell repainted in place: 3
  px changed, 3 flashed; cell (selected) ... 1 px changed, 1 flashed"*,
  which is the row's own documented flake (its docstring: three alternating
  CGA pixels in about one reading in six, on the kernel before as well) and
  pass 10's record 4.1 signature at its base; re-run three times alone, all
  three green.
