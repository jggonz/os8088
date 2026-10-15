# Kernel size pass 10 — agent "gfx"

Files: kernel/vga12.inc, kernel/softgfx.inc, kernel/font.inc, kernel/band.inc,
kernel/splash.inc, kernel/spinner.inc. Branch `ksp10-gfx`, cut at `833f13e4`.

#230's review fix `3b503fd` (gfx_blit1's refusal ladder) is in the base and was
read; nothing below changes its order of refusals.

## TOTAL (at the branch tip)

    kernsize[big]:   text 44,103 -205  bss +0  cold +0  lowbss +0  vgabuf +0   (sum -205)
    kernsize[small]: text 32,841 -63   bss +0  cold +0  lowbss +0              (sum -63)

Per file (kern_big `.text`): vga12.inc 5,391 -> 5,224 (-167), font.inc 2,262 ->
2,224 (-38), softgfx.inc 0. kern_emu (`make emu`) builds. Knob arms assemble on
both kernels: SCROLL_ROWBASE, NOPLANE, BAND, GFXAUDIT, NOBLITCUT, NOSEAMCUT,
NOUNAL, NOSPLIT, NOCOLFAST, SNAPAUDIT, THEMEDARK, FONTSLOW. stkbalance over
kernel/*.inc: identical report at base and tip (0 unbalanced).

Soak rows run, one at a time on MartyPC, all green: gfxpoints (twice), dispblit,
wdenter, tmrepair, ptsext.

## OBSERVATION (not changed - behaviour)

On a VGA build, gfx_xor_fill_raw falls into vga_xor_fill_vram, which calls
cur_unlazy - so a CLIPPED XOR fill (gfx_clip_run re-entering the raw body per
fragment) spends the deferred hide on VGA, where the 1bpp arm (`jne
sw_xor_fill`, before the call) does not. Harmless (an extra hide), but it is
the one rect body whose clipped path hides the arrow, and moving the hide to
the VRAM-only entry would be ~-4 bytes and a call off every VGA XOR fill. Left
for whoever owns the lazy-hide contract (SPEC.md 7.1.4) to judge.

## TAKEN

### Batch 1 — kern_big .text -100, kern_small .text -28

| item | big | small | speed |
|---|---:|---:|---|
| `gfx_points` exit -> `jmp kret_es` (8 pops + ret) | -6 | -6 | +1 taken jmp a CALL, not a point |
| `gfx_spans` exit -> `clc / jmp kret_es` | -6 | 0 (stub) | +1 jmp a span LIST |
| `gfx_rect_hit` / `gfx_rect_isectcf` share one `stc/ret` and one `clc/ret` | -4 | -4 | identical branches |
| `vgas_left` deleted: `[vgas_rows]` is the countdown (two copies of `mov ax,[rows] / mov [left],ax`) | -12 | -12 | faster (two loads/stores gone a scroll) |
| `vgas_lincopy` inlined into `gfx_scroll` (one caller) | -4 | 0 (VGA only) | faster (call/ret gone) |
| `gfx_fill_pat_raw` teardown -> `vga_solid_rect.reset/.done` (gray's was taken too and GIVEN BACK in batch 5: +0.29% measured) | -5 | 0 (VGA only) | +1 taken jmp a pat FILL |
| `gfx_fill_pat_raw`'s two edge columns -> one local `.col` | -21 | 0 (VGA only) | +2 call/ret a pat fill (rare: TaskMgr map) |
| `gfx_restore`'s tail is `gfx_save`'s (`gfx_sr_tail`) | -6 | -3 | +1 jmp a restore (not the cursor's path) |
| `vga_sr_on` inlined at its two callers | -3 | 0 | faster (call/ret and push/pop gone) |
| `gfx_frame` and `gfx_xor_rect_clip` share one tail (batch 5: the FRAME falls through, the clipped XOR outline jumps) | -3 | -3 | frame unchanged; +1 short jmp a clipped XOR outline |
| `gfx_pt_resolve` reads each origin once, AX's short stores | -4 | 0 | faster |
| `osapi_gfx_fill_pat`'s copy: `es lodsb` | -2 | -2 | faster |
| font_run: `[font_rn_fg]`/`[font_rn_bg]` as ONE word, three sites (+ %if) | -12 | 0 | faster (per RUN) |
| `fnt_rn_edge` VGA arm: a dead push/pop AX and a DH hop | -4 | 0 | faster (per unaligned run) |

(kern_small's -28 is the rows marked; the per-row arithmetic is approximate where
an arm assembles out.)

### Batch 2 — kern_big .text -66 (running -166), kern_small .text -19 (running -47)

| item | big | small | speed |
|---|---:|---:|---|
| `gfx_denter` deleted: its two callers (gfx_blit4's, gfx_blitp's hooks) already sit under `cmp [vid_ndisp],1 / jbe`, so they call `gfx_disp_enter` straight (+ macro GFXDENTER gone) | -11 | 0 | faster (a call and a compare a hooked blit) |
| `gfx_disp_run`: the eight clamp compares are `call gfx_rect_isectcf / jc .next`; the loop test reuses `mov ax,bp` | -34 | 0 | extended desktop only: ~+40 clocks a display a primitive |
| `gfx_blit4 .cut`: VX tested in memory, `mov di,[di+VID_CTX_*]` | -3 | 0 | faster, straddling blit only |
| `gfx_sub_arm` derives every term in registers (the three result words were parking S.x1, S.x2, S.y2) | -19 | -19 | faster: 3 stores and 3 loads gone, once a sub-rect restore |

### Batch 3 — kern_big .text -16 (running -182), kern_small .text -14 (running -61)

| item | big | small | speed |
|---|---:|---:|---|
| `gfx_xor_rect` unclipped FALLS INTO `vga_xor_rect_vram`'s `call cur_unlazy` instead of its own call and a `jmp short` over it | -5 | -5 | faster (a jmp) |
| `gfx_clip_run`: the primitive's rect is loaded and the fragment at SI is the isect block (intersection is symmetric) - no push/mov/pop of SI a fragment | -1 | -1 | ~25 clocks a fragment faster |
| font: `es lodsb` for `mov al,[es:si] / inc si` in font_str_x, font_run_x's .p1, .cell, .cells, .cells_nx (every exit's SI is popped) | -10 | -8 | faster, per character |

### Batch 4 — kern_big .text -16 (running -198), kern_small .text -2 (running -63)

| item | big | small | speed |
|---|---:|---:|---|
| font_ch_cut's three byte loops (seam cut, two-card only): `es lodsb` | -6 | 0 | faster |
| font_run_cell's row: `es lodsb` (per glyph ROW of a clipped/1bpp opaque cell) | -2 | -2 | faster: 1 instruction and ~5 clocks a row fewer |
| fnt_rn_edge's two column loops: `lodsb` (DS is the kernel's) | -4 | 0 | faster |
| vga_blit_prow's odd-x shift: `rcr byte [di], 1` in memory | -4 | 0 | faster (~29 -> ~20 clocks a byte) |

### Batch 5 — measured, and two items re-cut (kern_big +5, running -193)

gfxbench on MartyPC, base `833f13e4` against the tip of batch 4, one run per
machine (VGA XT, 5150 Hercules GLaBIOS, 5150 CGA GLaBIOS), counts per row:

* every FONT_RUN row 0.5-1.2% FASTER on all three adapters (the colour pair
  as a word, `es lodsb` in the character loops); FONT_STR/PAIR -0.1 to -0.4%;
  `one full-width row` -0.6 to -0.7%; GFX_FILL 64x64 clipped -0.15 to -0.2%
  (gfx_clip_run's fragment loop); GFX_XOR_RECT -0.1 to -0.6%.
* everything per-pixel, per-span and per-glyph flat to within 0.02%.
* UP: VGA GFX_FILL_GRAY 64x64 +0.29% and GFX_FILL_PAT 64x64 +0.28% (the tail
  jumps into vga_solid_rect, ~57 clocks a fill, more than the ~15 a jmp was
  priced at); VGA GFX_FRAME +0.19% (the jmp short into the shared tail).
  GFX_UNLOCK+LOCK VGA +2.7% and SET_COLOR VGA +1.9% are code this branch did
  not touch (pass 9 recorded the same two rows as VGA noise); Herc/CGA flat.

So: gfx_fill_gray_raw's own teardown is BACK (+5): it is the common fill. The
frame/clip-outline tail is kept but turned round, so gfx_frame falls through
and the rare clipped XOR outline takes the jump. gfx_fill_pat keeps its
+0.28%: the Task Manager's map and files.inc's one band, -26 bytes for it.

### Batch 6 — kern_big .text -12 (running -205), kern_small +0 (running -63)

| item | big | small | speed |
|---|---:|---:|---|
| gfx_ls_box's NO-REGION arm: the box is the display, and gfx_pt_resolve (once a pass) writes it; ls_box's unarmed arm is `je .oobck` (kern_small keeps the copy - it has no resolve) | -12 | 0 | faster unarmed (4 loads + 4 stores off every ls_box call, +4 stores a pass); +4 stores a pass armed |

Why it is exact: a region's presence cannot change inside one gfx_points call
(the hidden dock's hole is armed by the first CLIPQ that sees it and stays
armed), so an unarmed ls_box call is preceded in its pass only by unarmed ones,
none of which writes the box; and the second pass of a straddling array runs
gfx_pt_resolve again for the other display. `gfxpoints` and `ptsext` green.

## REFUSED

* `lea sp, [bp+18]` for `mov sp,bp / add sp,18` in gfx_blit1_x's `.noswap`
  (-2 cold, and faster): `tools/stkbalance.py` does not model `lea sp`, so the
  routine reads as unbalanced. Cross-file below.
* gfx_fill_gray_raw's edge columns as a subroutine (-12): +2 call/ret on every
  gray fill (desktop, every scrollbar trough), ~3.5% on an 8x8 gray fill.
  gfx_fill_pat took it because it is the rare fill.
* gfx_clip_run's pop run -> `jmp kret_di` (-4): per clipped primitive; the kret
  banner in kernel.asm lists it as deliberately kept.
* sw_fill_pat through sw_rect's prologue with a 4th sw_mode (~-0 net): the
  dispatch it needs in sw_rect_pl costs what it saves, and touches the SOLID
  fill's path.
* vga_seq+gc reset helper for gfx_blitp/vga_blit_prow: one beneficiary, +6 net.
* gfx_ls_box's armed clamp through gfx_rect_isect (-24 there): needs gfx_ls_d1/d3
  as real union words (gfx_pt_resolve +8 stores, union +1 .bss), net -15 for
  ~+30 clocks on EVERY gfx_points pass and ~+50 on an armed re-resolve -
  gfx_points is a per-call-cost primitive (GFX_POINTS 8 pts).
* gfx_disp_enter / gfx_disp_enter_n sharing one head (-7): the shared order
  calls vid_disp_find (a display loop) even when NESTED, which is font_char's
  path inside font_run's fallback on an extended desktop.
* GFXDENTERCD's one-display test moved into gfx_disp_enter_cd (-14): a
  call/ret on every glyph of every one-card machine (the macro's own banner).
* gfx_ls_box's unarmed copy (24 bytes) as the armed clamp run against a
  constant whole-plane rect (-13 with the 8-byte constant): ~+128 clocks on
  every gfx_points pass. (Batch 6 took the unarmed copy a better way.)
* GFXCLIP_ARM's body pointer as an inline `dw` after a `call` (~-10): +40
  clocks on every clipped primitive.

## CROSS-FILE (for the coordinator)

* `kernel/wm.inc` wm_su_srect `.isect:` (around :9172): `cmp ax,cx / jg .none /
  cmp bx,dx / jg .none` after two `gfx_rect_isect` calls is gfx_rect_isectcf's
  tail; the second call could be `call gfx_rect_isectcf / jc .none` when the
  visible-rect one is armed - a few bytes, the owner of wm.inc to judge.

* `tools/stkbalance.py`: teach it `lea sp, [bp+N]` (= `mov sp,bp` + `add sp,N`).
  Then vga12.inc's gfx_blit1_x `.noswap` can take it: -2 .cold, faster.

## NOTE FOR THE MERGE

`docs/INDEX.md` is regenerated in this branch only because this notes file is a
new tracked `docs/**.md` (the docindex gate). It conflicts with every other
agent's identical one-line change: resolve by re-running `tools/os88index.py`.
