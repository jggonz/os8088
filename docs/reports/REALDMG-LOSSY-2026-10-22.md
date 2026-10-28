# RealDMG: what NOT doing buys - lossy picture options, measured

**A measurement, taken in emulators.** 2026-10-22, a four-core cloud
container, branch `realdmg-speed` cut from `elendilon` at 231afcc.
- **The 286 figures are 86Box's `vm/286-victory`** (the owner's Packard Bell
  Victory 286 as near as 86Box comes: 16 MHz, 4MB, a Paradise PVGA1A), timed
  by `tests/rdgcore/rdgbench.py --box 286-victory --mhz 16 --rom GAME --vga`:
  the bare harness, 1,200 frames of play, the CPU and `rdg_renderd` EVERY
  frame with the changed lines written to VGA mode 0Dh, as the full screen
  does. A tick is 0.25% of a run; 86Box is not cycle-accurate
  (docs/86BOX.md).
- **The picture figures are Peanut-GB's** (`tools/gblossy.py`, on
  `tools/gbprof.py`'s pinned build), 2,200 frames of play each.
- **The games** are the owner's Super Mario Land, Kirby's Dream Land and
  Looney Tunes (Sunsoft, 1999; 1MB, MBC5), under the inputs the existing
  reports use: gbprof's platformer script, and for Kirby the bare harness's
  flying one (`--flap`). None is in the tree.

The question, from the owner: what can RealDMG NOT do, optionally, to run
faster - no$gmb's "don't render transparent pixels", two shades instead of
four, sprites without the background - and what else?

## 1. What a picture is made of

`tools/gblossy.py` composes every line a second time with its own renderer
(checked against Peanut-GB's: 0 of 50.5M pixels differ in Super Mario Land
and Kirby's Dream Land, 344 of 50.1M in Looney Tunes), and once per lossy
variant, counting the pixels each gets wrong:

| | Super Mario Land | Kirby's Dream Land | Looney Tunes |
|---|---|---|---|
| background, colour 0 | 70.4% | 53.1% | 27.5% |
| background, other colours | 29.1% | 32.6% | 59.1% |
| window | 0.0% | 11.2% | 11.1% |
| **sprite** | **0.5%** | **3.2%** | **2.3%** |
| lines with any sprite pixel | 11.2% | 34.7% | 30.5% |
| frames scrolled (SCX) / snapped to 8 | 62.8% / 8.0% | 42.1% / 5.4% | 53.4% / 9.0% |
| **wrong: no background or window** | 29.1% | 34.5% | 61.8% |
| wrong: no window | 0.0% | 2.0% | 3.8% |
| wrong: no sprites | 0.5% | 1.8% | 1.8% |
| **wrong: sprites opaque** | **0.05%** | **0.41%** | **1.26%** |
| wrong: priority bit ignored | 0.00% | 0.00% | 0.00% |
| **wrong: two shades** | **18.6%** | **32.1%** | **51.1%** |

**Sprites alone is not a game.** In Super Mario Land the pipes, the blocks,
the ? boxes, the coins, the ground and the status bar are all background;
the sprites are Mario and the enemies, half a percent of the screen. In
Kirby's Dream Land the platforms are background and the score is the
window. A no-background mode shows the characters walking on nothing.

**Opaque sprites cost little on the screen**: a white box round an enemy
standing in front of grey ground, more in Looney Tunes, whose sprites fly
over a grey sky.

**Two shades** reads well in Kirby's Dream Land and Super Mario Land (the
grey ground goes white, outlines stay) and loses the most in Looney Tunes,
whose sky and hills are shades 1 and 2.

## 2. What it is worth on the 286

Each option is a `%define` in `apps/realdmg/rdgppu.inc`, reached only by the
bare harness (`RDG_DEFS=LOSSY_X,LOSSY_Y` before `rdgbench.py`); the package
assembles byte-identical with them in the source. Milliseconds a frame,
drawing every frame, on `286-victory`:

| | Super Mario Land | | Kirby's Dream Land | |
|---|---|---|---|---|
| **as shipped** | **18.40** | | **38.86** | |
| CPU alone, no picture | 5.13 | | 11.08 | |
| `LOSSY_OPAQUE`: sprite colour 0 drawn, no claim, no priority | 18.26 | -0.7% | 38.22 | -1.6% |
| `LOSSY_NOWIN`: no window | 18.49 | +0.5% | 37.99 | -2.2% |
| `LOSSY_BW`: two shades - one plane composed, compared and written | 16.25 | -11.7% | 34.60 | -11.0% |
| `LOSSY_NOBG`: no background or window - sprites alone | 14.19 | -22.9% | 29.52 | -24.0% |
| `LOSSY_NOSPR`: no sprites (a bound, not an option) | 15.42 | -16.2% | 28.24 | -27.3% |
| **`LOSSY_COARSE`: the camera in steps of 8 pixels** | **13.78** | **-25.1%** | **29.98** | **-22.9%** |
| COARSE + BW | 13.04 | -29.1% | 28.65 | -26.3% |
| **COARSE + BW + OPAQUE** | **12.91** | **-29.8%** | **28.06** | **-27.8%** |
| Interlace: Doubled (shipped, for scale) | 17.62 | -4.2% | 37.21 | -4.2% |
| COARSE + Doubled | 14.78 | -19.7% | 30.90 | -20.5% |
| COARSE + BW + NOSPR (a bound) | - | | 13.18 | -66.1% |

### 2.1 Transparency is ~1%

`LOSSY_OPAQUE` drops the colour-0 test, the claimed mask, the behind-the-
background test and the per-line clear of the mask - everything
transparency costs a sprite - and buys 0.7% and 1.6%. A sprite row is a
handful of AND/OR on two words; 0.5-3% of the pixels are sprites. The other
reading of no$gmb's option, not drawing a background's colour-0 pixels, was
built and REFUSED in REALDMG-SPEC 13.3: skipping the shift for a byte from
two blank tile rows cost Kirby's Dream Land 4.5%, the test outweighing the
shifts. (What no$gmb's option actually does could not be read: the proxy
blocks its pages, as it did for REALDMG-PLAN's survey.)

### 2.2 Two shades is ~11%

`LOSSY_BW` keeps shade bit 1 alone - shades 0 and 1 white, 2 and 3 black:
`ppu_fshift` fetches and shifts one tile plane instead of two, the palette
pass composes one plane, `ppu_dput` compares and copies one, and
`ppu_dvga` writes ONE VGA plane a line. A shipped form would set the
attribute controller so that plane 0 does not matter, at no cost per line.
The sprites still compose both planes (cheap, 2.1).

### 2.3 The background is a quarter, the sprites as much again

Sprites alone saves 23-24% and is not playable (section 1). The surprise is
`LOSSY_NOSPR`: sprites are under 3.2% of the pixels and taking them away
saves 16% and 27%. It is not the drawing - 2.1 says that is ~1% - but the
DAMAGE: a sprite that moves dirties every line it covers, and a dirty line
is composed whole, background and all. In Kirby's Dream Land a third of the
lines carry a sprite.

### 2.4 The coarse camera is the best single option

`LOSSY_COARSE` snaps SCX to a multiple of 8 and adds the rest to every
sprite's X, so the picture stays together and the CAMERA moves in 8-pixel
steps. Seven scrolled frames in eight become still frames (section 1:
62.8% of Super Mario Land's frames scroll, 8.0% snapped), and a still frame
composes only what changed; and every background line takes `ppu_fcopyb`,
no fine shift at all. -25% and -23%. It is per LINE, so a status bar split
keeps its own (zero) scroll; the fine part rides in the raster log entry's
spare fourth byte, out of the log before `ppu_logsame` compares it, so the
split still reads as the same log every frame.

What it costs on the screen is the step: a smooth scroll becomes a jump of
8 pixels every few frames - the way many 8-bit computer ports scrolled.
SCY is left alone; none of the three games scrolled vertically under these
inputs. Interlace: Doubled on top LOSES (-20% against -25%): coarse leaves
almost no scrolled frames for Doubled to halve, and its per-line work
stays.

### 2.5 What is left after the cheap three

Super Mario Land with COARSE + BW + OPAQUE is 12.91 ms a frame, of which
the CPU is 5.13: it draws every frame with a quarter of the machine spare.
Kirby's Dream Land is 28.06, of which the CPU is 11.08, and with sprites
also off it is 13.18 - so **15 ms of Kirby's remaining 17 ms of drawing is
lines recomposed because a sprite moved on them.** The sampled profile
(`rdgprof.py --box 286-victory --vga`, the three on) agrees: the
background's line composition (`ppu_fcopyb`, `ppu_fshift`, `ppu_line`,
`ppu_blist`) is ~27% of samples against the sprites' own ~13%. (`rdgprof`
names `ppu_fcopyb`'s samples `ppu_fcopyw`: the two share a macro body.
`LOSSY_NOWIN` on top moved 613 ticks to 609, so they are not the window's.)

## 3. Looney Tunes: CPU-bound, so the picture matters less

The bare harness cannot hold a 1MB cartridge (it loads the ROM flat at
3000:0000, 368KB at most - a layout limit, not the machine's RAM), so it
was timed in the PACKAGE, in its window, with `tests/rdgcore/rdgwin.py
--vm 286-victory`, Right held after four Starts:
- **Its CPU is the heaviest of the three by far**: 64.0% of a frame BUSY
  against Super Mario Land's 27.0% and Kirby's Dream Land's 29.6%
  (`tools/gbprof.py`, the same script), and 19.7 bank switches a frame
  among 8 banks.
- **The first 15 seconds of play hold 100% of real time and draw 53-54
  frames a second**, with 45% of a profiled span idle.
- **Two 20-second runs read 91.0% and 93.3%**, drawing 4-5 frames a second,
  one dipping to 25% for a second. Profiled in a heavy stretch: the core
  and translated code ~69% of the samples, a third of that in the
  INTERPRETER (`tr_ekey`, `tr_int`, `op_*`), drawing ~15-20%. The scene
  was not pinned down - a screenshot after the profile shows the title,
  so that stretch may hold a death and a restart.

So in Looney Tunes' hard stretches every picture option together can buy
at most its drawing share, ~15-20%. The lever there is the CPU, and it is
not lossy: why a third of a heavy stretch runs interpreted.

## 4. Ideas not built

- **A background line cache - not lossy, the largest lead.** Keep each
  line's composed background (40 bytes a line, 5.7KB) and, when a line is
  dirty only because a sprite moved over it, copy it back and draw the
  sprites, instead of fetching and shifting 21 tile rows again. Section
  2.5 bounds it: in Kirby's Dream Land with the coarse camera, ~15 ms a
  frame is sprite-dirtied lines, of which roughly two thirds is the
  background's composition. Without the coarse camera it helps the still
  frames, ~58% of Kirby's. PREDICTED, not measured.
- **Sprites on alternate draws** (lossy): half the sprite-dirtied lines,
  sprites moving at half the picture's rate. Bounded by half of
  `LOSSY_NOSPR`'s saving.
- **The CPU has no safe lossy option.** The game's logic must run exactly
  or the game is a different game; the existing ones are CPU > Speed
  (REALDMG-SPEC 15.2: the game slowed, the picture given the rest) and the
  draw clock's own frame-skip, which every saving above already feeds -
  a cheaper draw is more frames drawn, not a faster game, once the game
  holds 100%.
- **A coarse SCY** would need the sprites' line lists moved by the rest as
  well; none of the three games scrolled vertically here.

## 5. Built as F10 and F11 (the same day)

The owner took two of them to try: two shades and the 8-pixel camera, on
F10 and F11 in the package, not kept and with no menu item (REALDMG-SPEC
13.4). As run-time toggles they read, on the same bench:

| ms a frame | Super Mario Land | Kirby's Dream Land |
|---|---|---|
| as shipped | 18.40 | 38.86 |
| the toggles assembled, both off | 18.67 (+1.5%) | 39.36 (+1.3%) |
| F10 two shades | 16.61 (-9.7%) | 35.38 (-9.0%) |
| F11 scroll by 8 | 13.87 (-24.6%) | 30.30 (-22.0%) |
| both | 13.18 (-28.4%) | 29.02 (-25.3%) |

Two lessons on the way. A first run-time two shades read only -5%: it
shifted one way only (`shl` by up to 7, where the composer turns 5-7 into
`shr` by 3-1 - a 286 shifts in 5 + CL clocks), and it copied bit 1 over
bit 0 every line so that every writer saw a consistent buffer; both out,
the buffer's bit 0 is kept only for a writer that reads it. And testing
the flags a LINE at a time cost +3.2% with both off (402 -> 415 ticks;
the harness's new placement alone reads 402): `ppu_pal` now chooses the
background's routines through two pointers, and VGA's writer has a twin
`ppu_dput` calls, which took it to +1.5%.

### 5.1 Kept, on the Screen menu, and off for nothing

The owner kept both (Scroll by 8 "makes some games hard to play, but it's
a user selectable option"): Screen > Two Shades and Scroll by 8, beside
Interlace, remembered per game in REALDMG.CFG - and the off path "needs to
be as fast as before". It is now the composer's own bytes: each of twelve
sites holds its original instruction and `rdg_lpatch` writes a same-length
CALL or JMP into a stub only while one is on (REALDMG-SPEC 13.4). Drawing
every frame on `286-victory`:

| ms a frame | Super Mario Land | Kirby's Dream Land |
|---|---|---|
| as shipped | 18.40 | 38.86 |
| the code in, both off | 18.45 (403 ticks; 1,171 against 1,172 over 4,800 frames) | 38.86 |
| Two Shades | 16.57 (-9.9%) | 35.20 (-9.4%) |
| Scroll by 8 | 13.82 (-24.9%) | 30.16 (-22.4%) |
| both | 13.23 (-28.1%) | 29.11 (-25.1%) |

The one tick at 1,200 frames is the tick's phase: the larger harness loads
1.5KB more before the timed span, and over four times the frames the two
read a tick apart the other way.

## 5.2 Scroll by 4 and by 2

The owner asked for gentler steps, F11 cycling Off, 2, 4, 8. The step is a
mask (`rdg_cmask`) the snap reads instead of a constant `0F8h`, set when the
level is patched; By 8 re-measured on the new code reads the same ticks as
before (302 and 659). Bare harness, `286-victory`, ms a frame:

| | Super Mario Land | | Kirby's Dream Land | |
|---|---|---|---|---|
| Smooth | 18.40 | | 38.86 | |
| By 8 (`LOSSY_COARSE`) | 13.82 | -24.9% | 30.16 | -22.4% |
| By 4 (`+LOSSY_COARSE4`) | 14.83 | -19.4% | 32.13 | -17.3% |
| By 2 (`+LOSSY_COARSE2`) | 16.02 | -12.9% | 34.74 | -10.6% |

By 4 keeps four fifths of By 8's saving at half its jump; By 2 a half. They
earn less than 7 in 8 / 3 in 4 / 1 in 2 still frames would suggest because a
snapped SCX of 4 or 2 is not a whole byte - the shift stays on the frames
that do move - and a scroll faster than a pixel a frame moves at every step
below 8. (The lossy harness needs `LOSSY_COARSE` beside the other two to sit
low enough: `RDG_DEFS=LOSSY_COARSE,LOSSY_COARSE4`.)

## 6. Re-taking it

```
python3 tools/gblossy.py GAME.GB [--flap] [--shots PREFIX F1 F2 ...]
RDG_DEFS=LOSSY_COARSE,LOSSY_BW python3 tests/rdgcore/rdgbench.py \
    --box 286-victory --mhz 16 --rom GAME.GB --vga
python3 tests/rdgcore/rdgwin.py GAME.GB --vm 286-victory --press ... \
    --hold 0x14D --secs 20 --persec [--profile N]
```

`LOSSY_*` are instruments, not options: OPAQUE's edge bytes read a claimed
mask it no longer clears, and none of them is in the package, a menu or a
gate.
