# Tracker's spectrum on an XT at 5.5 kHz

**A measurement, not a description.** Taken 2026-09-24 on a four-core cloud
container, on the tree at `a3b63c9` plus SPEC.md 45.23.1 (the XT visualiser
button cycling VU Meter / Off, which changes nothing measured here). MartyPC
at the pinned commit, machine `os8088_5150_herc_sb_gla`: a 4.77 MHz 5150,
Hercules, Sound Blaster, GLaBIOS. Every figure is GUEST time, so it is exact
at any host load. It is true of that tree and is not maintained against later
ones.

> **CORRECTED the same day, and the correction is at the end.** The *lines
> only* arm below was the wrong experiment. It put a line at the bar's own
> LEVEL, which falls six steps every frame, so every line moved every frame.
> What the owner asked about is the PEAK MARKERS without the bars, and that
> arm is cheaper than the full spectrum, as it has to be: **10.8 frames a
> second against 7.7**. The section *Correction* has the numbers, and those
> taken after SPEC.md 45.21.8's LCD fix.

The question was the owner's: *with the recent sound work, does an XT at XT
mode's 5,500 Hz have room for the Spectrum? Thinner lines and no bars would
do if that keeps up.*

**The answer is no, and thinner lines make it worse.** The audio holds in
every arm. The picture does not: the spectrum halves the face's frame rate.
The cheapest lever found is not in the visualiser at all.

## Method

`BEVERLY.MOD` playing in the full (720x348) windowed layout, with XT mode
armed automatically on a tier-0 machine. Two instruments were used.

- **Frames.** Exec breakpoints on the package's `tw_update` and `tw_vizdraw`,
  counted over 10 guest seconds each. A breakpoint costs the guest nothing.
- **Where the time goes.** `tools/os88rate.py --rkey --rates 0 --secs 60`,
  an IP sampler of ~11,500 samples bucketed by symbol, plus
  `trk_consumed` against guest cycles for the audio actually delivered.

The shipped `TRACKER.O88` is the VU arm. The other arms are the same source
with the research patch at the end of this file. Every disk was written
uncompressed, so the listing and the binary could be compared byte for byte.

**A trap cost one run and is worth knowing.** `os88rate.symbols()` assembles
into ONE fixed listing, `/tmp/os88rate.lst`. Two instances started together
can read each other's listing, and then every bss read lands on the wrong
word. That run read `mp_xt=230` and a rate of 110 Hz. It was thrown away and
the matrix was re-run with a listing path per process (`os88rate.LST` patched
by the caller).

## Frames and audio

| arm | face frames/s | audible | ring lead, median | idle (`hlt`) |
|---|---|---|---|---|
| Off | **17.9** (17.86, 17.85) | 100.5% | 6,144 | 11.6% |
| VU, thin XT meter (shipped) | **15.5** (15.62, 15.43) | 100.4% | 6,144 | 5.6% |
| Spectrum, as on a 286 | **7.6** (7.51, 7.78) | 100.4% | 4,096 | 1.1% |
| Spectrum, half the bands each frame | 8.9 | 100.5% | — | — |
| Lines only: marker at the level, no bar | **6.6** | 100.5% | — | — |
| Scope | 4.4 | 100.5% | — | — |

The spectrum on the CGA twin (`os8088_5150_sb_gla`) runs at 8.3 frames a
second.

- **The audio keeps up in every arm.** At 5.5 kHz the mixer, the replayer
  and the driver take about 41.5% of the machine whatever is drawn. The ring
  lead never falls below 2,048 in any arm.
- **The picture does not keep up.** `tw_update` draws the whole face, so the
  spectrum slows everything in the window to 7.6 frames a second, including
  the scrubber, the LCD clock and the buttons. `tw_vtick` also decays per
  FRAME and not per tick. So at 7.6 fps the bars fall at about 42% of their
  designed speed, and a peak marker's 4-frame hold lasts ~0.5 s instead of
  0.2 s. The spectrum would look sluggish, not just choppy.
- **Thin lines are slower, not faster (6.6 fps).** On this machine the cost
  is per CALL and not per pixel (PERFORMANCE.md). A moving line is two
  fills, one to erase it and one to draw it again, and it takes
  `tw_scol`'s general walk (cut, sort and compare) every frame. A bar
  usually takes the fast path instead: one fill, which was measured at 11 of
  every 13 column updates (SPEC.md 45.24).
- **Updating half the bands each frame buys only 15%.** The per-band cost is
  not the whole bill.

## Where a spectrum frame goes

At 7.6 frames a second, the spectrum-specific package symbols (`tw_scol*`,
`tw_scolr`, `tw_spec.b`, `tw_fills`, `tw_abs`/`tw_rel`, `tw_vtick`, and
`mp_pfind` for the note lookup) come to **≥ 11.5%** of the machine. The kernel's
fill path (`sw_col`, `sw_plane_op`, `gfx_clip_run`, `gfx_rect_setup`, the API
cells) rises from 14.2% with the pane off to 25.7%, another **~11.5 points**.
So about half the cost is bookkeeping in the package and half is drawing.
Per frame, the spectrum adds about **50 ms of CPU** to a face that costs about
26 ms with the pane off. A tick is 55 ms, and 23 ms of it belongs to the
mixer. At PERFORMANCE.md's 756 us fixed part of a drawing call, sixteen bands
that all move are ≥ 12 ms in fills alone before any bookkeeping. So no
spectrum shaped like this one fits a tick-rate face on an 8088.

## The larger finding: the LCD costs more than the meter

With the pane **off**, `tw_pad` (its `.h` hash loop and `.p` padding) and
`tw_put` come to **18.8% of the whole XT**. The VU arm reads 17.2% and the
spectrum arm 9.8%, lower only because it runs fewer frames. That is about
10.5 ms of every frame. Every frame composes the four LCD lines and the
status line into `tw_line`, pads each to 50 cells, and hashes all 250 bytes
with a shift-and-add, only to find the text unchanged. The needles cost about
5 ms a frame. The LCD compose costs twice that, and it is paid in every arm.

Composing a line only when its inputs change (the elapsed second, the
position, the rate, the volume) would return most of that 18.8% on every XT,
whichever visualiser is picked. It is the first thing to do before trying to
fit any spectrum. That estimate is PREDICTED from the profile and has not
been built.

## If an XT spectrum is wanted anyway

Everything in this section is prediction, and nothing in it is built.

1. Fix the LCD compose first. It is worth ~10 ms a frame in every mode.
2. Decay per TICK rather than per frame, the way `ttx_vu` already does
   (SPEC.md 45.16.1). The bars then fall at the right speed at any frame
   rate, and a low frame rate looks choppy rather than slow.
3. Use fewer, wider bands. Eight bands would take half the fills and half
   the walks.
4. Keep the bars and drop the markers. The bar's one-fill fast path is the
   cheap case, and the marker is what forces the general walk.

Even with all four, a figure near the needles' 15 frames a second is
unlikely. The realistic target is a spectrum that updates on alternate ticks
(~9 fps) without dragging the rest of the face down with it.

## The research patch

It is not in the tree. To re-run the table, apply it by hand and build with
`nasm -f bin -w+error -DTRKXTSPEC [...] -I apps/ -I apps/tracker/`. Then
pass the same names to `os88rate.py --defines`, or the listing will describe
a different binary.

- `TRKXTSPEC`: in `tw_vizfx`, `cmp al, al` ahead of the `TRKLOG` arm, so the
  meter is never forced. In `trk_entry`, skip the `trk_cpu0` test, so the
  pick starts at Spectrum.
- `TRKXTINIT=<n>`: `mov byte [tw_viz], TRKXTINIT` instead of `TWV_SPEC`
  (2 is Scope, 3 is Off).
- `TRKXTLINE`: in `tw_spec`'s band loop, just before `call tw_scol`, add
  `mov bp, si` / `xor si, si`. The marker then sits at the level and no bar
  is drawn.
- `TRKXTHALF`: skip `call tw_scol` when `(band ^ [tw_hpar]) & 1` and the
  pane is not owed whole, and flip `[tw_hpar]` once per `tw_spec`. Add a
  `TRKB tw_hpar` beside `tw_viz`.

## Correction: the markers without the bars, and after the LCD fix

Same machine and module, same day. The tree is `4ac28d6` plus the working
tree that became SPEC.md 45.21.8. `tw_fills` is counted by breakpoint, like
the frames.

| arm | LCD | frames/s | fills/s |
|---|---|---|---|
| Spectrum, bars + markers | old (hash) | 7.7 | 125 |
| **Markers only, no bars** (`xor si, si` before `call tw_scol`) | old (hash) | **10.8** | **76** |
| Spectrum, bars + markers | **keyed + diffed** | 8.4 | 152 |
| **Markers only, no bars** | **keyed + diffed** | **12.9** | 103 |
| VU, thin XT meter (shipped) | keyed + diffed | **18.2** (every tick) | — |

- **Drawing less costs less.** A marker holds for four frames and then steps,
  so most frames draw nothing in most columns. The *lines only* arm above
  moved every line every frame, and so it measured a different design.
- **The LCD fix raised both spectrum arms.** The fills per second rose with
  them because more frames ran, and the audio stayed at 100.5% throughout.
- **Frame rates come in steps.** A frame that runs past a tick waits for the
  next one, so the rates cluster at 18.2 / 2 = 9.1 and 18.2 / 3 = 6.1. The
  full spectrum sits just over one tick a frame and pays for a whole second
  tick. The markers-only arm is a mix of one-tick and two-tick frames.
- **What is still wrong at 12.9 fps is the decay, not the drawing.**
  `tw_vtick` decays per FRAME, so markers step and fall at 12.9 / 18.2 = 71%
  of their designed speed on this machine. Decaying per tick
  (`ttx_vu`'s rule, SPEC.md 45.16.1) would make the timing right at any
  frame rate.

The profile of the shipped VU arm after 45.21.8 puts the LCD (`tw_lkey`,
`tw_lkey.cmp`) at ~1.7% of the machine, where `tw_pad` and `tw_put` were
17.2%. The idle share went from 5.6% to **19.5%**. So "fix the LCD first"
from the section above is done, and the room it found is real.

