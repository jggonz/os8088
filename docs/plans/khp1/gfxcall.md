# Kernel hot-paths pass 1: gfxcall, the fixed part of every drawing call

Agent `gfxcall`, branch `khp1-gfxcall`, 2026-11-06. The brief is
`docs/plans/khp1/BRIEF.md`. This note follows its section 6.

The subject is everything a drawing call pays before and after its pixels:
the `OSAPI_SLOT` cell, the gfx and font shims, the gfx lock and its lazy
cursor hide (SPEC.md 7.1, 7.1.4), the `GFXCLIP`/`GFXDISP` heads, the shared
rect set-up `gfx_rect_setup` and `gfx_rowbase`, `gfx_clip_run`, and the
non-VESA cost of the VESA hook heads. `kernel/clip.inc` is the CLIPBOARD
(SPEC.md 55), not the clip region, and is on no drawing path. `kernel/band.inc`
ships only in a `BAND=1` knob build (kernel.asm, SPEC.md 5.9.6), so no user
operation of the shipped kernel runs it, and it was not worked.
`kernel/fprog.inc` is in no agent's row. This pass took one routine in it,
`fpg_finish`, because `gfx_unlock` calls it on every drawing burst.

## 1. BASE AND TIP

Base `a7ee7fab` (= `6d6c23ae`). Tip = this branch, which includes the
coordinator's merged shared fixes (`mod_cfix`, +2 bytes of `.cold` on
kern_big, not this agent's). Assembled with `make`'s own `kernsize`.

| kernel | `.text` base -> tip | other sections | resident | `KERN_SIZE` / budget |
|---|---|---|---|---|
| kern_big | 40,631 -> **40,681 (+50)** | `.cold` +2 (shared fix), rest unchanged | +52 | 96,768 -> 96,768 / 100,864 |
| kern_small | 30,220 -> **30,256 (+36)** | unchanged | +36 | 59,392 -> 59,392 / 64,000 |
| kern_emu | 40,885 -> **40,935 (+50)** | `.cold` +2 (shared fix) | +52 | 97,280 -> 97,280 / 100,864 |
| kern_mega | 45,853 -> **45,903 (+50)** | unchanged | +50 | resident 62,976 -> 62,976 of 68,096 |

All of this branch's growth is `.text`, which is BEFORE `.cold`: **50 bytes**
against the coordinator's ~1.5 KB pass-wide limit before the next
unresolvable COLD_HOME (BRIEF.md section 8). `KERN_CODE_MAX`: 19,478 bytes
left on kern_big. Every byte is in an `[+N]` commit except gfxcall-2, which
is FREE (0 bytes).

## 2. WHERE THE TIME GOES

### 2.1 The instrument

Besides `tools/os88hot.py` (bench), this note's per-instruction numbers come
from a scratch tool that SINGLE-STEPS a real call on MartyPC and keeps
`(cycles, CS:IP, SP)` for every instruction: a breakpoint on the routine's
entry, then `step` until the return address comes back with SP above the
entry's. About 3,500 instructions a second, so a whole 160 ms window raise
(41,000 instructions) is one minute, and every cycle in it is attributed to
the routine and the instruction that spent it, with inclusive per-call costs
recovered from SP. The step cycle counts agree with `os88hot`'s free-running
timings (the raise: 159 ms stepped, 170 ms busy in `os88hot`'s window, which
also holds the press's serial flight and the dock tile). The tool is a
measurement, not a row, and is not committed. Its source and every trace are
in this agent's scratch directory (`htrace.py`, `run.py`, `an.py`).

### 2.2 One small call, instruction by instruction (MartyPC 5150, base)

The cheapest `gfx_fill` in a CGA raise is a 1-row, 1-byte rect from
`gfx_hline`: **2,280 cycles**, of which about 120 are pixels.

| part | CGA cycles | VGA cycles | owner |
|---|---:|---:|---|
| `gfx_hline` shim | 31 | 31 | gfxcall |
| `GFXCLIP` head: `gfx_clip_query` call (160), cursor-lazy test (40) | 200 | 200 | gfxcall |
| `GFXDISP` (`[vid_ndisp]`), `VXMONO`, the jump to the renderer | 125 | 154 | gfxcall |
| `sw_rect` push set and mode tests / `vga_solid_rect` pushes and GC arm | 385 | 654 (incl. the one column) | mono / vga |
| **`gfx_rect_setup`** | **652** | **652** | gfxcall |
| `gfx_rowbase` (called from it) | 132 | 128 | gfxcall |
| `sw_ink`, `sw_plane_op`, `sw_col` (the one row) | 627 | — | mono |
| `vga_gc_reset` | — | 172 | vga |
| pops and returns (`kret_*`) | 128 | (in the body) | — |

**`gfx_rect_setup` plus `gfx_rowbase` were 34% of the cheapest fill on CGA
and 39% on VGA**, and the single largest item of the fixed part on both.

### 2.3 Named operations, base, MartyPC (os88hot `time`, medians of 3)

| operation | CGA | Hercules | VGA |
|---|---:|---:|---:|
| raise a Disk window | 178.6 ms | 229.3 | 365.9 |
| menu open / close | 44.0 / 23.4 | 47.4 / 21.6 | 48.4 / 32.1 |
| PgDn / PgUp in a Disk window | 131.3 / 124.2 | 131.9 / 124.0 | 104.1 / 96.8 |
| type 31 keys in Note Pad (busy) | 617.6 | 560.2 | 626.6 |

Within them, the routines this agent owns (os88hot `profile`, share of busy
time, base, from the bench report and this agent's runs):

| routine | raise C/H/V | PgDn C/H/V | drag CGA | type CGA |
|---|---|---|---|---|
| `gfx_rect_setup` | 4.3 / 4.0 / 2.4% | 2.4 / 1.0 / 1.6% | 3.3% | 0.6% |
| `gfx_rowbase` (+ `_calc` on VGA) | 1.1 / 1.2 / 0.6% | 0.9 / — / 1.0% | — | 1.0% |
| `gfx_clip_query` | 0.8 / 0.6 / 0.4% | 1.5 / — / — | 0.1% | 0.1% |
| `gfx_unlock` (+ `fpg_finish`, `cur_lazyend`) | <0.5% | <0.5% | 0.2% | 1.0% + |
| `gfx_nextrow` (callers' rows) | — | 2.8 / 4.4 / — | — | — |

So the fixed part this agent owns is **~6-8% of a raise and ~5% of a PgDn
on 1bpp**, and the rest of every operation is pixel loops (mono, vga),
glyph cells (text), the save-under and the scheduler. Two things outside the
fixed part are the largest costs that a small kernel change touches:

- **Typing in Note Pad spends 9.7% of its busy time hiding and re-showing a
  pointer parked nowhere near the text** (`cur_put_mono` 6.2%, `cur_get_mono`
  3.5% on CGA): every keystroke's unclipped `font_run` and caret `gfx_vline`
  spend the deferred hide unconditionally. Section 6 prices the fix.
- A drag spends 24% in `sch_switch` and 3.9% in `task_yield` (sched's).

## 3. TAKEN

| id | agent | routine / path | hotness | change | kern_big B | kern_small B | cycles base -> tip (input) | user-visible (operation, adapter: ms base -> tip) | M/P | 286 | commit | FREE / [+N] |
|---|---|---|---|---|---:|---:|---|---|---|---|---|---|
| gfxcall-1 | gfxcall | `gfx_rect_setup` (softgfx.inc), every rect primitive in both renderers, `gfx_save`/`gfx_restore`, the plane blits | H2 | the row table read inline (no `gfx_rowbase` call, no reload of y1), the y empty test is the `sub` that counts the rows, both masks held in DL/DH so a one-byte rect combines in a register | +6 | +6 | **751 -> 639** a call (54 calls in a CGA raise; stepped) | raise C 178.6 -> 176.7, H 229.3 -> 226.6, V 365.9 -> 363.4 (all three items together); a CGA raise's 54 set-ups -6,048 cycles = **-1.27 ms** | M | fewer instructions and no new word operand at an odd address; hot list parity kept. Not measured on 86Box | `10323197` | [+6] OWNER'S PICK |
| gfxcall-2 | gfxcall | `gfx_hline` shim (vga12.inc) | H2 | BX restored from gfx_fill's preserved CX instead of a push/pop pair | 0 | 0 | 3,536 -> 3,511 (a 312-px pinstripe) | ~25 cycles a hline, 9 in a raise: 0.05 ms | M | same | `9fd79fb6` | FREE |
| gfxcall-3 | gfxcall | `gfx_frame` (vga12.inc): every window frame, title-bar box, menu frame, dialog and button outline | H2 | `GFXCLIP`'s three questions (region, cursor hide, display count) asked once an outline; the four strips go to `gfx_fill_raw`. Region, hole, second display and GFXAUDIT keep the old per-strip path | +26 (1 is a parity pad) | +20 | **11,355 -> 10,707** an 11x11 title-bar box (stepped); gfxbench `GFX_FRAME 64x64` 6,142 -> 5,895 us | 2 frames in a raise: -0.27 ms; menu drop -0.25 ms; scales with every outline a dialog draws | M | same | `4ea77a8b` | [+26] OWNER'S PICK |
| gfxcall-4 | gfxcall | `fpg_finish` (fprog.inc), reached by every `gfx_unlock` | H2 (per drawing burst) | the unarmed state answered before the frame: its only effect there was `[cur_barok]` = 0, which `fpg_step.out` does | +18 (1 is a parity pad) | +10 (1 pad) | `fpg_finish` **483 -> 176**; `gfx_unlock` without interrupts 1,497 -> 1,183 (stepped); gfxbench `GFX_UNLOCK+LOCK` pair **473 -> 403 us (-14.8%)** | -64 us per drawing burst: typing 31 keys = 31 unlocks = -2.0 ms; a Task Manager refresh, a clock tick, every drag step's unlock | M | same | `2f15d7df` | [+18] OWNER'S PICK |

**gfxbench, CGA, base -> tip, all four items** (`os88hot ab --base 6d6c23ae
gfxbench --machine cga`, per call): `GFX_PIXEL` 538.8 -> 508.7 us (-5.6%),
`GFX_HLINE 8px` 539.5 -> 508.4 (-5.8%), `GFX_VLINE 8px` 757.5 -> 727.3
(-4.0%), `GFX_FILL 8x8` 666.5 -> 636.4 (-4.5%), `GFX_HLINE 256px` -3.2%,
`GFX_FILL 256x1` -2.9%, `GFX_FRAME 64x64` -4.0%, `GFX_XOR_RECT 64x64` -2.1%,
`WM_TITLE strip` 33.3 -> 32.5 ms (-2.3%), `GFX_UNLOCK+LOCK pair` 473.4 ->
403.4 (-14.8%), `FULLSCREEN in+out` -1.4%. The control rows sat still: every
FONT_*, BLIT4, BLITP, SCROLL, POINTS, the RAM/VRAM rows and the loop overhead
are within 0.1%. One row moved the wrong way and is not real: `whole page of
rows` 851,968 -> 917,504 counts at N = 2 is 13 against 14 ticks of 65,536,
the row's own granularity (Part 3 of PERFORMANCE.md: single-digit rows are
not quotable); `hline ns per pixel` +3% is derived from two of the rows above.

**User operations, `os88hot ab --base 6d6c23ae time <scen> --all -n 3`,
medians:**

| operation | CGA | Hercules | VGA |
|---|---|---|---|
| raise (busy) | 170.3 -> 168.4 (-1.1%) | 221.9 -> 219.3 (-1.2%) | 357.8 -> 355.4 (-0.7%) |
| PgDn | 131.3 -> 130.4 (-0.7%) | 131.9 -> 131.5 (-0.3%) | 104.1 -> 103.7 (-0.4%) |
| PgUp | 124.2 -> 123.3 (-0.7%) | 124.0 -> 123.6 (-0.3%) | 96.8 -> 96.4 (-0.4%) |
| menu open, stepped exactly (CGA, `menu_drop` to its poll, interrupts excluded) | 335,265 -> 333,908 cycles (-0.28 ms) | — | — |

The menu scenario's `time` medians are NOT resolvable at this size: a menu's
reps land on one of two values ~2.5 ms apart and the medians flipped both
ways (close: CGA -10.9%, Hercules +7.7%) with equal minimums. The stepped
number above is the menu's measurement.

**Pixels and behaviour.** gfxcall-1: a unicorn harness ran base's and tip's
`gfx_rect_setup` over 20,000 rects (random and edge cases, empties included)
on CGA, Hercules and VGA geometries, with the row table full, short and
absent: 0 differences in CF and all six outputs, SI/DI/BP/SP preserved; the
same harness against a deliberately broken tip (`and dl,dh` -> `or`) reports
59 differences in 3,000. gfxcall-3: base and tip booted on CGA, and the
rendered frame compared at six points (desktop, two windows, a raise, the File
menu open, closed, the About box): 0 differing pixels, except the About box's
build number (538 against 547, the commit count, SPEC.md 14.2). gfxcall-2 and
-4 are register- and state-equivalent by construction, argued in each
commit.

## 4. TRIED AND REFUSED

| idea | what it buys | why not |
|---|---|---|
| Inline `CLIPQ` (the `gfx_clip_query` call) in `GFXCLIP` | -79 cycles a rect primitive (160 -> 81) | +14 bytes a site, +56 for the four `GFXCLIP` entries; ~20 `gfx_fill` calls a raise now = -0.33 ms. Weakest value per byte found; offered if the owner wants it, at `gfx_fill` alone (+14) |
| ONE "slow path" gate byte for the four head tests (`[wm_clip_n]`, `[gfx_hole]`, `[cur_lazy]`, `[vid_ndisp]`) | ~-285 cycles a rect primitive, ~12% of a small call | the state has ~30 writers in wm, dock, dockmod, menu, stikmenu, sched, mega, fprog and vga12. A missed writer draws through a clip region. Too fragile for a speed pass |
| Inline `vid_ctx_act`'s one-display compare in `gfx_unlock` | -45 cycles an unlock | +5 bytes for ~1.4 ms across 31 typed keys. Not worth it |
| `xlat` for the two mask lookups in `gfx_rect_setup` | 0 | as many bytes and clocks as the indexed loads |
| Faster `gfx_rowbase` | — | 128-133 cycles including the call; nothing found that is shorter on the 8088 |
| Per-call dispatch vectors instead of `GFXDISP`/`VXMONO` | ~-75 cycles a call | every writer of `[vid_ndisp]`/`[vid_planes]` must re-point them; the same fragility as the gate, smaller prize |
| The `OSAPI_SLOT` cell, `api_x` | — | already minimal for the frozen ABI (section 5) |
| `band.inc` | — | knob-only (`BAND=1`), no shipped operation runs it |
| `gfx_clip_run`'s per-fragment cost | — | below 0.15% in drag and raise profiles; nothing to buy |

## 5. WANTS AN ABI BREAK

| speedup | frozen interface | cycles / ms | bytes | callers |
|---|---|---|---|---|
| Let `OSAPI_GFX_FILL` (and pixel/hline/vline/frame) **clobber AX..DX** | the register contract "clobbers: flags" (SPEC.md 5.7's last paragraph says this is why `sw_rect` pushes eight registers) | drops 4 push/pop pairs from every rect primitive: ~120 cycles, ~25 us of a ~510 us small call (5%) | kernel -8; every package caller that keeps a coordinate across a call +2..4 per site | every package that draws (all of `apps/`) and the C runtime's thunks |
| A **batched rect call**: N fills/frames in one far call from a package, the clip and display questions asked once | an ABI ADDITION (a new slot), not a break | per extra primitive: the far-call cell (~165 cycles), the head (~300) and the lock-free audit; a Calc or Minesweeper redraw of ~30 cells would save ~14k cycles (~3 ms) | ~60-80 bytes of `.text` for the walker | packages that redraw grids; `gfx_spans` (SPEC.md 5.10) is the shipped precedent for spans |

## 6. WANTS THE OWNER

**gfxcall-5 (proposed, not taken): the unclipped rect and run ask about
THEIR OWN RECTANGLE before hiding the pointer.** SPEC.md 7.1.4.5 already does
this for `gfx_blit1` (`cur_lazyrect`, kern_big). `GFXCLIP`'s unclipped arm
and `fnt_unlazy` still spend the deferred hide unconditionally, so typing in
Note Pad hides and re-shows a pointer parked anywhere on the screen, every
keystroke.

Prototype (built, measured, reverted):

```
; vga12.inc GFXCLIP, unclipped arm (kern_big)     ; font.inc fnt_unlazy (kern_big)
    cmp byte [cur_lazy], 0                          cmp word [wm_clip_n], 0
    je %%spent                                      jne .out
    call cur_lazyrect      ; AX..DX = the rect      cmp byte [cur_lazy], 0
%%spent:                                            je .out
                                                    push ax/bx/cx/dx
                                                    ; the run's superset:
                                                    ; (x-8, y, 7FFFh, y+15)
                                                    call cur_lazyrect
                                                    pop dx/cx/bx/ax
```

| | busy, 31 keys, CGA, pointer parked at (600,150) |
|---|---:|
| tip | 611.4 ms |
| `GFXCLIP` alone | 612.6 ms (the run spends the hide first) |
| **both** | **553.9 ms: -57.5 ms, -1.85 ms a keystroke, -9.4%** |

MEASURED on MartyPC (os88hot `type` with the pointer moved off Note Pad; the
shipped scenario leaves it on Note Pad's title bar, next to the text, where
the arrow is in the way and nothing changes). Bytes: the `GFXCLIP` arm is the
same 10 bytes as `CURUNLAZY` (0); `fnt_unlazy` +~22, kern_big only.

Why the owner, and why it is not simply taken:

- **It changes transients.** The arrow stays on the glass through a burst that
  does not reach it, which is SPEC.md 7.1.4's own goal (no blink) - but
  SPEC.md 7.1.4.3/7.1.4.4 records that keeping the arrow lit was reported
  from the iron as a stutter for a MOVING pointer, and `CURFIX=1` is off by
  default because the eye ranked it. The same question applies here.
- **It costs while the promise stands.** `cur_lazyrect` is ~300 cycles (five
  pushes) where the spent-hide test was 40, paid by every primitive of a hold
  that never reaches the arrow. A hold of ~50 primitives drawn away from the
  pointer pays about what one hide-and-show costs on CGA (~1.9 ms). A leaner
  test (the cell's four bounds computed once at `gfx_lock`) would be ~150.
- **`fnt_unlazy` is the text agent's file**, `cur_lazyrect` the pointer's.

## 7. CROSS-FILE

| file / routine | owner | speedup | worth |
|---|---|---|---|
| `font.inc` `fnt_unlazy` | text (+ pointer) | ask `cur_lazyrect` about the run, with gfxcall-5's `GFXCLIP` arm | -1.85 ms a Note Pad keystroke on CGA with the pointer away (section 6). Owner's call |
| `font.inc` `font_run_x` | text | on one display, `.nosplit`'s `GFXDENTERCD` re-tests `[vid_ndisp]` right after the split test answered it | ~49 cycles a run (24+12+37+12 measured, CGA): the 20-run PgDn ~1k cycles |
| `mouse.inc` `cur_lazyend` | pointer | test `[cur_level]` and the position BEFORE the eight pushes and `pushf`; the common answer ("not moved") needs none of them | 498 -> ~170 cycles on every unlock whose promise survived (stepped: the 8 pushes, `pushf`, the compares, `kret_es`) |
| `icons.inc` `ico_pass_bb`, font row loops | files, text | inline `gfx_nextrow` (three instructions) in per-row loops | `gfx_nextrow` is 2.8% of a CGA PgDn and 4.4% of a Hercules one; the call and ret are ~40 of its ~120 cycles: ~1.0-1.9 ms a PgDn |
| `softgfx.inc` `sw_rect`, the 1bpp renderer entry | mono | gfxcall-3 leaves each strip of a frame paying the renderer's own set-up four times: `sw_rect`'s push set and mode tests (385), `gfx_rect_setup` (639), `sw_ink` (128). A frame-aware body (one set-up for the two full-width rows, one for the two columns) is what is left of the files agent's find. **Compatibility**: gfxcall-3 calls `gfx_fill_raw` per strip, so any rework of `sw_rect`/`sw_fill` or of the row walk underneath it (mono's bank-major walk) lands under it unchanged | up to ~4.6k of an 11x11 frame's ~10.7k cycles |
| `sched.inc` | sched | a CGA drag spends 24% of its busy time in `sch_switch` and 3.9% in `task_yield` (the drag loop yields per step) | measured share, not a design |

## 8. ROWS

Covering rows: `fpgop`, `fpgcold`, `fpgnotch` (gfxcall-4); `xorrect`,
`btnall`, `disptitle`, `dispcalc` (gfxcall-3, the second display's per-strip
path); `gfxlk`, `curdisk`, `curbusy`, `curshape`, `mseglazy`, `clipkeep`,
`deskclip` (the lock, the lazy hide, the clip arms both items touch). The fast
tier (`make`) ran green at every commit, `hotpar` included.

Run once after the last code commit, `os88test.py soak -k <the fourteen
above> --marty-jobs 1`: **14 passed, 0 failed, 0 skipped**, 837.6 s.
`make small`, `make emu` and `make mega` build at the tip;
`tools/stkbalance.py` reports the same counts at base and tip on every file
touched: 3 pre-existing tail-jump paths in softgfx.inc, 11 in vga12.inc with
fprog.inc - nothing new.

## 9. DEFECTS FOUND

- SPEC.md 39.29.10.1 described `gfx_rect_setup`'s "existing `gfx_rowbase`
  call" and names `vbe_bank`/`VBROW`/`vbe_home`, none of which exist in the
  tree any more. The first is corrected for this change; the VBE paragraphs
  are left as they are (history, not this pass's).
- No kernel defect found.
- Instrument note for bench: `os88hot time menu`'s medians flip between two
  values ~2.5 ms apart rep to rep, so a sub-millisecond change to a menu
  cannot be read from them; the stepped trace can.
