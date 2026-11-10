# Kernel size pass 19 - `timing`: the before/after on MartyPC

Branch `ksp19-timing`, cut from `kernel-size-19` at `11015e90` (TIP). BASE is
`4beca096`, the commit before the pass. The owner's ask: *"Do the
before/after timing. For gfxbench, redline, sysbench, and pick a couple games
to measure as well (get them in game and actually drawing usefully, not a
static title screen; skies has a good benchmark for this). Make sure we had no
meaningful regressions."*

**Verdict: no meaningful regression.** Every move outside noise is one of the
pass's PRICED costs (the rare-call door, the window callback) or is not the
kernel at all (the floppy's rotational phase; MartyPC's Hercules wait-state
model, whose mechanism this note pins down - section 6). The two predicted
wins are real: `OSAPI_TASK_YIELD` is 109 clocks faster, and boot is 100-200 ms
quicker on CGA and Hercules.

## 1. Method

* BASE built in its own worktree and tree (`build/os88hot-ab/wt-4beca09`,
  `make BUILD=` never pointed at the shared `build/`); TIP in this worktree.
  `make small` in both for kern_small.
* Everything is GUEST time off MartyPC's cycle counter (4.772727 MHz), so host
  load cannot move it. Base and tip ran side by side, one emulator each.
* Machines: kern_big on `os8088_5150_cga_gla`, `os8088_5150_herc_gla`,
  `os8088_xt_vga`; kern_small on `os8088_5150_cga_gla` and the 128KB
  `os8088_5150_cga_128k`.
* `tools/os88hot.py time <scenario> --all -n 3` on each tree (the same thing
  `os88hot ab` does, two lanes at once instead of one after the other);
  medians of 3 unless marked. `gfxbench`/`sysbench` through `os88hot`;
  REDLINE through `tools/redline_profile.py` (median of three trials, each the
  mean of three complete runs), compared BASE against TIP directly and never
  against the recalibrated reference.
* Games IN PLAY: Clear Skies' pinned-frame benchmark (`tests/skiesperf.py
  --nostages --work`, two scenes, 3 reps, every adapter), WireBall's
  timed rallies (`tests/wireball.py --video`, 8 rallies x 4 adapters, 2 reps),
  ExciteBike's whole first course at turbo with the reference rider
  (`tests/excitebike_perf.py --lap`, plus `--scroll` for per-component
  clocks).
* Harness fix on this branch: `tests/skiesperf.py` could not find any stage's
  call site at the tip - since the flag day every package `call` is the
  `OSAPI_RARE` macro, so the source line carries no bytes. A matching line
  with no bytes now arms a pending site and the next line with bytes is it
  (pre-pass trees resolve exactly as before). The same file ran on both arms.

## 2. Desktop operations (`os88hot time`), kern_big

`busy` is CPU time inside the window; `ms` is what the user waits.

| operation | CGA | Hercules | VGA |
|---|---|---|---|
| raise a window (ms) | 162.5 -> 162.2 (-0.2%) | 209.3 -> 210.5 (+0.6%) H | 358.9 -> 359.2 (+0.1%) |
| drag, 8 packets (ms) | 495.7 -> 495.7 (0.0%) | 517.6 -> 522.0 (+0.8%) H | 658.2 -> 658.2 (0.0%) |
| drag (busy ms) | 481.6 -> 484.3 (+0.6%) | 502.6 -> 510.7 (+1.6%) H | 645.1 -> 645.1 (0.0%) |
| PgDn (ms) | 106.0 -> 106.2 (+0.2%) | 124.9 -> 126.2 (+1.0%) H | 99.2 -> 99.4 (+0.2%) |
| PgUp (ms) | 100.0 -> 100.1 (+0.2%) | 118.2 -> 119.4 (+1.1%) H | 93.0 -> 93.1 (+0.2%) |
| menu open (busy ms) | 30.9 -> 30.9 (-0.1%) | 30.7 -> 30.7 (+0.1%) | 35.0 -> 35.0 (0.0%) |
| menu close (ms, n=8) | 23.1 -> 22.1 (-4.2%) T | 21.2 -> 23.0 (+8.3%) T,H | 30.5 -> 30.3 (-0.6%, n=3) |
| launch Calc, cached (ms) | 832.5 -> 831.5 (-0.1%) | 838.3 -> 840.4 (+0.3%) | 851.6 -> 850.2 (-0.2%) |
| launch Calc, cold (ms, n=1) | 1,261.1 -> 1,238.8 (-1.8%) D | - | - |
| type 31 keys (busy ms) | 489.7 -> 491.6 (+0.4%) | 431.1 -> 434.3 (+0.7%) H | 511.7 -> 516.2 (+0.9%) |
| pointer sweep (busy ms) | 140.1 -> 140.1 (0.0%) | 137.0 -> 138.4 (+1.0%) H | 181.2 -> 181.3 (0.0%) |
| idle desktop 5 s (busy ms) | 177.2 -> 177.2 (0.0%) | 177.2 -> 177.2 (0.0%) | 177.2 -> 177.2 (0.0%) |
| open A:'s Disk window (ms) | 1,285 -> 1,335 (+3.9%) D | 1,327 -> 1,303 (-1.8%) D | 1,255 -> 1,336 (+6.5%) D |
| boot to the desktop (ms) | 14,352 -> 14,251 (-0.7%) | 14,455 -> 14,250 (-1.4%) | 15,772 -> 15,772 (0.0%) |

* **D - the platter, not the code.** Identical int 13h work on both arms:
  opening A: is 3 reads / 18 sectors / 1 seek (first rep) or 0 seeks, a cold
  Calc 1 read / 9 sectors. The reps scatter with rotational phase (CGA base
  1,330 / 1,285 / 1,205, tip 1,360 / 1,335 / 1,235; VGA's FASTEST rep is the
  tip's, 1,201 against 1,244). P19 section 7 found the same.
* **T - the tick.** Menu close is multi-modal in ~3 ms steps (a tick's work
  lands in the 20 ms window or it does not): at n=8 CGA base reads 21.4-24.8,
  tip 19.4-24.3; Hercules base 19.6-23.0, tip 19.4-25.2. The minimum is the
  tip's on both. Menu OPEN's busy, which the tick cannot reach, is flat.
* **H - MartyPC's Hercules model** (section 6): every Hercules move here is
  inside the spread the SAME kernel shows when only the emulated card's
  clock residue is changed.
* **Typing** (+0.4% CGA, +0.9% VGA) is the priced costs. Per rep: 31 key
  callbacks through `wm_hnd` (+158 clocks each, `wmrec.md`) and 20 calls
  through the rare door (`os88_door`: 20.0 calls a rep, measured with
  `os88hot cycles`; ~+220 clocks each, section 3) predict ~9,300 clocks;
  CGA measured +8,970 (+1.9 ms over 2.9 s of typing). `wm_pkgcall` into Note
  Pad is 31 calls, 29,528 clocks median, on both arms. VGA's extra ~2.6 ms
  a rep (+0.5%) is section 7.
* Drag CGA busy +2.8 ms over 8 packets (the profile run read -0.3%): the
  callback price again, inside noise on VGA.

### kern_small (`OS88_DEFINES=KERN_SMALL OS88_BUILD=build/smallk`)

| operation | CGA, 640KB | CGA, 128KB machine |
|---|---|---|
| raise (ms) | 158.8 -> 158.5 (-0.2%) | 160.0 -> 158.7 (-0.8%) |
| drag (ms / busy) | 490.9 -> 491.0 (0.0%) / +0.5% | 490.8 -> 490.8 (0.0%) / -0.2% |
| PgDn / PgUp (ms) | 115.2 -> 115.4 (+0.2%) / +0.2% | 115.2 -> 115.4 (+0.2%) / +0.2% |
| menu open (busy ms) | 19.65 -> 19.66 (0.0%) | 19.65 -> 19.66 (+0.1%) |
| menu close (ms) | 17.9 -> 19.2 (+7.3%) T | 22.2 -> 18.8 (-15.1%) T |
| launch Calc, cached (ms) | 767.2 -> 765.0 (-0.3%) | 766.7 -> 764.5 (-0.3%) |
| launch Calc, cold (ms, n=1) | - | 1,193 -> 1,237 (+3.7%) D (1 read, 9 sectors, 1 seek both) |
| type 31 keys (busy ms) | 420.8 -> 423.0 (+0.5%) | - |
| pointer sweep (busy ms) | 134.1 -> 134.3 (+0.1%) | 133.4 -> 133.3 (-0.1%) |
| idle 5 s (busy ms) | 165.7 -> 165.8 (0.0%) | 165.8 -> 165.8 (0.0%) |
| open A: (ms) | 597 -> 619 (+3.7%) D (1 read, 8 sectors both; fastest 563 / 571) | - |

`time desktop` does not find kern_small's desktop on either arm ("no
desktop in 200 guest seconds" - the harness, at base as at tip), so kern_small
boot is not in the table.

## 3. gfxbench and sysbench (`os88hot gfxbench|sysbench --all`)

74 timed gfxbench rows a machine. **CGA: 71 within 0.5%. VGA: 70.** The rest:

| row | CGA | Hercules | VGA | why |
|---|---|---|---|---|
| WM_BAND set+retire (us a pair) | 332.8 -> 428.3 (+28.7%) | same | same | **the rare door**: `OSAPI_WM_BAND` is an `RS` cell now; +95.5 us = +456 clocks a set+retire pair, **+228 clocks a call** (predicted +205). Paint and Task Manager are its only callers |
| FULLSCREEN in+out | +1.6% | - | +0.8% | 61 -> 62 and 131 -> 132 whole ticks over 4 reps: tick quantisation (and a door cell) |
| whole page of rows | -8.3% | - | - | 12 -> 11 whole ticks |
| GFX_BLIT4 4px runs | - | -0.7% | -5.0% | 20 -> 19 whole ticks |
| GFX_UNLOCK+LOCK pair | 0.0% | 0.0% | +1.4% | an interrupt in the window: a second VGA run reads +0.0% |

**Hercules: 45 of 74 within 0.5%** as measured - VRAM write word +7.1%,
fills +2-5%, FONT_RUN -2.6 to -3.2%. Every one of those is the emulated
card (section 6): BASE run with a 5-cycle text-mode excursion before the
bench reproduces the tip's Hercules figures to within 0.1% (VRAM write word
6,535.3 against the tip's 6,535.3; FILL 64x64 6,286 / 6,286; SCROLL 31,455 /
31,455; FONT_RUN 10 aligned 2,794.6 / 2,795.3), and against that matched base
**70 of 74 are within 0.5%** - the same four as VGA (WM_BAND's door, two
whole-tick rows, the lock pair).

sysbench: 61 timed rows on CGA and Hercules (64 VGA), 56 / 56 / 58 within
0.5%:

| row (us a call) | CGA | Hercules | VGA | why |
|---|---|---|---|---|
| TASK_YIELD | 278.7 -> 255.8 (-8.2%) | -8.2% | -8.2% | **the V2 win**: -109 clocks (predicted ~-95) |
| FILE_HERE | 156.5 -> 204.0 (+30.3%) | +30.3% | +30.3% | **the rare door**: +226 clocks a call |
| FILE_DFREE | 7,612.9 -> 7,659.5 (+0.6%) | +0.6% | +0.6% | **the rare door**: +222 clocks on a call that reads the disk |
| int 1Ah / int 16h | -13.6% / - | +0.8% | +16.3% / -8.8% | BIOS calls; whichever 60 or 100 calls an IRQ0 lands in. Opposite signs per adapter |
| read 16K, cold motor | +8.3% | +4.2% | -3.8% | one sample, rotational |
| boot ticks | 171 -> 167 | 172 -> 168 | 169 -> 170 | |

The door therefore measures **+222 to +228 clocks a call** against section
2's predicted +205 (+193 priced, +12 for the `add`) - about 10% over the
prediction, on cells the pass chose because they are rare. `OSAPI_GFX_DESK`
and `READ_AT` have no gfxbench/sysbench row; the window callback has none
either and is the typing figure above.

## 4. REDLINE (`tools/redline_profile.py`, BASE vs TIP)

Counts are PIT-count equivalents (lower is faster), median of three trials.

| | CGA (`redline_pc_gla`) | Hercules | VGA |
|---|---|---|---|
| CPU and RAM rows (6) | all within 0.01% | all within 0.01% | all within 0.01% |
| graphics rows (19) | 17 identical to the count; Wire -0.15%, Resize -0.49% | -3.4% (Text) .. +6.5% (Blit4 large) H | 18 identical; Fractal -0.35% |

Hercules row by row: fill +2.1, line +1.1, frame +0.1, text -3.4, blit1
+0.6, blit4 -0.7, fill large +4.7, lines many +1.1, frames many 0.0, text
grid -2.0, blit1 large +2.7, blit4 large +6.5, wire +0.2, shaded +0.2,
fractal +0.3, pattern blit 0.0, scroll 0.0, composition +0.3, resize +1.0 -
the same fills-slower / text-faster signature as gfxbench's Hercules column,
i.e. section 6, not the kernel.

## 5. Games, in play

### Clear Skies - the pinned frame (`skiesperf.py --nostages --work --warm 20`)

Twelve consecutive frames by breakpoint on `cs_render`, the aeroplane parked
in flight; every rep identical to 0.1 ms. `work` is `cs_render` to
`cs_r_end` (the drawing), `frame` adds the page flip and the steps.

| scene / machine | frame base -> tip | work base -> tip |
|---|---|---|
| city, CGA (15 objects) | 208.37 -> 208.37 ms (4.80 fps), 0.0% | 187.27 -> 187.26 (0.0%) |
| city, Hercules (13) | 160.32 -> 160.71 (+0.24%) H | 144.90 -> 144.86 (-0.03%) |
| city, VGA Mode X (15) | 251.20 -> 251.20 (0.0%) | 240.39 -> 240.38 (0.0%) |
| runway, CGA (7) | 183.08 -> 183.08 (0.0%) | 155.86 -> 155.85 (0.0%) |
| runway, Hercules (7) | 158.19 -> 158.99 (+0.50%) H | 136.96 -> 136.80 (-0.12%) |
| runway, VGA (7) | 234.46 -> 234.46 (0.0%) | 218.04 -> 218.04 (0.0%) |

The renderer's work is unchanged or faster everywhere; Hercules' frame moves
only in its blit to VRAM, which is section 6.

### WireBall - a live rally (Set 160's method, `wireball.py --video`)

8 rallies x 4 adapters, both reps identical:

| adapter | display fps base -> tip | physics steps/s |
|---|---|---|
| VGA | 59.7 -> 59.7 (110 frames a rally, every rally) | 72.7 -> 72.7 |
| CGA | 60.2 -> 60.2 (111) | 72.7 -> 72.7 |
| Hercules | 51.0 -> 51.0 (94) | 72.3 -> 72.3 |
| Hercules + colour card | 51.0 -> 51.0 (94) | 72.6 -> 72.6 |

### ExciteBike - the whole first course at turbo (`excitebike_perf.py --lap`)

The reference rider through both laps, the lap change, ramps, hurdles,
crashes and the finish, rendering on:

| adapter | frames | period mean (clk) | p99 | simulation mean / step |
|---|---|---|---|---|
| VGA | 2,498 -> 2,497 | 87,380 -> 87,380 (54.62 Hz) | 100,691 -> 100,632 | 3,054 -> 3,054 |
| CGA | 2,741 -> 2,741 | 79,646 -> 79,648 (59.92 Hz) | 109,171 -> 109,064 | 3,109 -> 3,122 |
| Hercules | 2,327 -> 2,327 | 93,852 -> 93,852 (50.85 Hz) | 125,066 -> 124,832 | 3,119 -> 3,110 |

Per component, the scripted scroll (`--scroll`, clocks a frame): VGA erase
4,743 / 4,743, columns 8,946 / 8,962, sprites 8,485 / 8,485, HUD 1,482 /
1,481, flip 393 / 393; CGA boxes 8,968 / 8,968, columns 6,085 / 6,122, HUD
4,394 / 4,452 (an interrupt in a frame moves a mean by tens of clocks);
Hercules columns 8,090 / 7,840, HUD 5,165 / 5,299 (H).

## 6. Hercules: the mechanism P19 left open

P19 section 7 found Hercules moving +-2.5% "with placement alone", CGA and
VGA not, and could not say why. This pass's Hercules column moved the same
way - gfxbench's `VRAM write word`, a loop INSIDE THE BENCH PACKAGE whose
bytes are unchanged and which calls no kernel code, read +7.1% - so the
cause is below the kernel. It is MartyPC's MDA/Hercules model
(`marty_core/src/devices/mda/`):

* A VRAM access's wait states come from `WAIT_TABLE[(self.cycles + cycles +
  1) & 0x0F]` - the CGA's 16-phase table, indexed by the CARD's clock
  counter (`mmio.rs`, `get_read_wait`/`get_write_wait`).
* That counter advances by `char_clock` per character: 9 in text mode, **16
  in graphics mode** (`tick_gchar`, `HGC_CHAR_CLOCK`). So once the card is in
  graphics mode, `cycles & 0x0F` never changes again: the residue the card
  had at its last text-to-graphics switch picks one of sixteen wait patterns
  for the WHOLE session, and every VRAM-heavy loop is priced by it.
* Shown on ONE build: BASE's gfxbench on Hercules, with nothing but a
  text-mode excursion poked in just before Run (`3B8h <- 08h`, N guest
  cycles, `3B8h <- 0Ah`): `VRAM write word` reads 6,104.8 us with none,
  5,252.7 at N = 1, 2 or 3, and **6,535.3 at N = 5 - the tip's figure,
  with every other Hercules row of the tip reproduced to 0.1%** (section 3).
  Over the excursions tried, the same base build spans 5,252.7 to 6,535.3
  (-14% to +7%); the tip spans 5,622.9 to 6,535.3. Delaying the Run itself,
  or changing conventional memory (636/632/624KB, so the POST and heap move),
  changes nothing - only the card's residue does. The disk read before the
  mode set re-aligns everything upstream of it, which is why only a kernel
  change (the path from the last boot read to the mode set) ever moved it.
* The os88hot scenarios across residues (excursions N = 0, 1, 2, 3, 5, 9,
  13): PgDn base 124.9-126.0 ms, tip 124.6-126.2 (means 125.5 / 125.4);
  PgUp base 118.2-119.3, tip 117.8-119.4 (118.8 / 118.6); drag busy base
  502.6-512.1, tip 502.0-510.7 (507.4 / 506.3). **Averaged over the card's
  phase the tip is equal or faster on Hercules too.**

So a Hercules A/B on MartyPC compares two arbitrary residues unless it pins
one. The fix belongs to the instrument, not the kernel: an `os88hot` step
that re-seats the residue (or averages several) before a Hercules window.
Not done here (the excursion probes are `herc_toggle.py`/`hot_toggle.py`
in this session's scratch, two monkeypatches of `os88hot`); a real Hercules
is asynchronous to the CPU and averages over the phases anyway.

## 7. What is left over

* **VGA typing +0.5% beyond the priced costs** (~2.6 ms over 31 keys, ~400
  clocks a key, 0.08 ms on a 16 ms keystroke): below the 1% bar. The
  sampling profile (`profile type --machine vga -n 6`, deterministic) moves
  time between neighbouring routines whose labels changed in the pass
  (`gfx_clip_query` -12.9 / `gfx_rect_setup` +12.1, `wm_pkgcall` -6.0 / `pkg
  Note Pad` +5.7, `sch_switch` -3.5 / `sch_account` +7.3 / `task_yield` +3.6)
  rather than naming a routine that does more work; gfxbench's VGA FONT_RUN
  and GFX_* rows are identical to the count, `wm_pkgcall` is identical, and
  CGA shows no residual. Exact figures (`os88hot cycles`, VGA, one rep a
  arm) for the profile's biggest gainer agree: `sch_account` is 290 -> 289
  calls a rep at a median of 448 clocks on both arms (the profile's +7.3 ms
  is attribution), and `wm_pkgcall` 31 calls at 29,528 on both. What is
  left is under 0.5% and was not chased further.
* Nothing was changed in the kernel: no regression needed fixing.

## 8. Rows and runs

The data (JSON and logs, both arms) are this session's scratch; every
figure above is re-derivable with: `OS88HOT_ROOT=<tree> python3
tools/os88hot.py time <scenario> --all -n 3`, `... gfxbench --all`, `...
sysbench --all`, `tools/redline_profile.py --machine os8088_redline_<x>_gla`,
`tests/skiesperf.py --nostages --work --scene city|runway --warm 20
--machine <m>`, `tests/wireball.py --video`, `tests/excitebike_perf.py --lap`
/ `--herc --lap` / `--scroll` / `--herc --scroll`, and for kern_small the
same `time` runs under `OS88_DEFINES=KERN_SMALL OS88_BUILD=build/smallk`
with `build/smallk/os8088-360.img` linked to `small360.img`.
