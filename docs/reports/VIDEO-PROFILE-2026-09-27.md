# Video Player: where a play's time goes, and what binds it

**A measurement, not a description.** Taken 2026-09-27 on a four-core cloud
container, on `video-player` from `5ec87d2c` to the commits that landed beside
this report. MartyPC at its pinned commit. Every cycle figure is GUEST time,
counted by the emulator's cycle-exact 4.77 MHz 8088, so it holds at any host
load. It is true of that tree and is not maintained against later ones. It
answers docs/plans/VIDEO-PLAN.md 15.8, the optimisation pass.

**The content** is real footage: the owner's `BadApple.mp4`, `Sonic2.mp4` and
`Trackmania.mp4` (20 s from each), and Big Buck Bunny plus scikit-video's
`bikes` clip (camera motion, 5 s). None of it is in the tree. Every file was
made by `tools/os88venc.py` at its preset's defaults on the `5150-st225`
profile unless a row says otherwise.

**The instrument** is `tools/os88vidprof.py`, new with this pass:
- the default mode SAMPLES the guest's IP between ~1,193-cycle batches of
  `advance` and buckets it by the package's own map (locals rolled up by
  routine), the kernel's (every section at its segment) and the ROM - a
  profile of a whole play that costs the guest nothing;
- `--cal` breaks on `vp_frame`'s call of `vp_decrec` and on the kernel's far
  call of the hook, times every frame's decode to the cycle, fits it to the
  encoder's cost model, and splits every hook call into the decode and the
  rest;
- `--live` plays a Live file on the desktop; `--hdd` boots an XT-IDE VHD.

## 1. What binds a play

**On the default profile the DISK binds, and the CPU is mostly idle.** The
encoder now says which budget cut each frame it could not make exact
(`cut by:` in its report):

| clip | preset | KB/s | CPU mean / worst | frames cut | by the disk / CPU average / ceiling |
|---|---|---|---|---|---|
| Sonic 2 | cga | 54.5 | 25.0% / 85.0% | 471 of 601 | 465 / 0 / 6 |
| Sonic 2 | herc | 52.1 | 24.4% / 83.7% | 455 of 601 | 454 / 0 / 1 |
| Sonic 2 | vga4 | 65.3 | 27.2% / 84.9% | 502 of 605 | 494 / 0 / 8 |
| Sonic 2 | vga8 | 86.3 | 27.6% / 84.7% | 441 of 507 | 436 / 0 / 5 |
| Trackmania | cga | 53.4 | 28.8% / 84.8% | 34 of 601 | 31 / 0 / 3 |
| Trackmania | modex | 73.6 | 31.9% / 84.1% | 482 of 504 | 468 / 0 / 14 |
| Bad Apple | cga | 38.5 | 22.0% / 83.1% | 2 of 601 | 0 / 0 / 2 |
| bbb+bikes | cga, `5150-picomem2` | 75.6 | 33.9% / 80.0% | 42 of 459 | 0 / 19 / 23 |

So on `5150-st225` a faster decoder buys picture only at a SCENE CUT (the
per-frame ceiling), and on a CPU-bound profile - `5150-picomem2`, the 286 ones
- it buys it everywhere. **The 60 KB/s the profile assumes has never been
measured on the ST-225.** It is "capped around 64 KB/s for margin"
(VIDEO-PLAN 6); 86Box's ST11R streamed 253 KB/s with the hook holding half of
every period (VIDEO-86BOX-ST11R). If the owner's ST11M streams even twice the
profile's figure, the default profile becomes CPU-bound and every cycle below
is picture.

**The stream is near-incompressible to a byte LZ.** 60-80% of a record is the
pixel bytes themselves (skips 5-22%, addresses 6-8%, headers ~5%). LZ4 over
each record saves 3-14%, over each 32 KB super-packet 8-25%, at ~40 cycles an
output byte to expand - more than the decode it would feed. The idle CPU on a
disk-bound 5150 has no cheap way to be traded for bytes.

## 2. The native decode is at the 8088's bus

`--cal` over 151 frames of camera footage on the CGA 5150 fits the encoder's
own model to 0.4% (residual 237 cycles a frame): **measured / model = 1.001**
on the mean. Hercules on MartyPC 1.007. A one-byte change is `lodsb / add
di,ax / movsb`, 4 bytes of code, and costs 47.7 cycles on CGA memory; the
instruction mix has no slack an 8088 can use. What was trimmed is the frame's
FIXED cost, outside the lists:

| per frame, silent, Hercules | before | after |
|---|---|---|
| `vd_native`'s fixed part (ten list heads) | 1,787 | 1,462 |
| the hook outside the decode | 3,840 | 3,040 |
| ...of which `vp_next` (a call) | 1,765 | 966 |

With 22,050 Hz PCM8 the hook outside the decode is 14,331 -> 12,946 a frame
drawn, plus ~1 call a frame that draws none at 4,263 -> 4,103. Of that,
~9,500 is the audio copy (735 bytes of `rep movsw`, which the model prices
at 13 a byte and measures at that), so sound costs a 5150 ~6% of itself at
22 kHz and 3% at 11.

`vp_next` copied the eight-word stream cursor in and out on every record; it
steps it in place through DI now. The ten lists were ten calls; each ends in
a jump to the next. A Duff entry takes the whole round from its table.

## 3. C160 was four times what the encoder thought

C160 was decoded into a RAM shadow and every dirty row copied to the text
screen at the stride of two, `movsb / inc di / loop`. On the CGA 5150, 150
frames of `bikes` at 160 x 75:

| | before | after |
|---|---|---|
| the package's share of the machine | **85.9%** | **32.7%** |
| ...`vp_blit` + `vp_rowaddr` (the copy) | 55.9% + 11.3% | 0 |
| the decode | ~16% | ~30% |
| the encoder's model / measured | 0.25 (the copy unpriced) | 1.00 (fitted) |

`vd_c160` writes the lists straight onto the screen (SPEC.md 98.3.12.1). A
change on the text screen costs ~1.5x the one-bit decoder's and a slice ~34
cycles a cell (CGA wait states on every store), so the encoder has its own
constants for the layout now, fitted by `--cal` to 0.6%. A C160 file made
before this pass was budgeted as if it were ~2/3 its real cost on the direct
decoder, and should be made again.

## 4. A file on another screen: the copy's rows

A Hercules file on the CGA 5150 through the shadow (98.3.2): the copy placed
every row by 98.1.2's formula, a multiply and a bank loop, twice a row.

| | before | after |
|---|---|---|
| the package's share of the machine | 84.3% | 74.0% |
| `vp_rowaddr` | 25.3% | - |
| `vp_blit` (the stores) | 35.3% | 49.2% |

The rest is the stores, which only a smaller band can reduce. Decoding
straight onto the other layout (VIDEO-PLAN 15.8 option 2) is not taken.

## 5. Live: the blit, not the decode

The logo (`OS8088.V88`) Live on the desktop, 6 s:

| | CGA | Hercules |
|---|---|---|
| `gfx_blit1_x` | 32.6% | 45.2% |
| the decode | ~2% | ~2% |

A pass blits the HULL of the rows its frames wrote, at the canvas's width:
7,173 bytes a frame for 232 changed on the logo. Bad Apple made `--live herc`
is 102,833 cycles of blit a frame by a byte-and-call model (20 cycles a byte,
4,000 a call), 65% of a 30 fps period, where its decode is 12%.

**Tracking the written columns at playback was BUILT, MEASURED and REFUSED.**
A walk of each Live record after its decode, widening each row's columns, cut
`gfx_blit1_x` to 15.2% on the logo - and cost 15.9% itself, plus 9.2% for the
runs. On Bad Apple it cost 49.5% of the machine and the play fell behind
(47 late): 228 writes, 170 row steps and 34 divides a frame at ~120 cycles a
write, against a saving of ~40,000. An 8088 cannot classify a write for less
than the decoder spends making it.

What the same model says the blit would cost if the ENCODER named the
rectangles (it does now - SPEC.md 98.1.3.4 landed beside this report, and
measured the logo's `gfx_blit1` at 22.5% on Hercules and 20.0% on CGA, Bad
Apple Live's at 32.1%):

| per frame | hull (today) | one column range a record | row runs, each at its columns |
|---|---|---|---|
| the logo | 147,451 | 122,234 | 57,607 |
| Bad Apple, `--live herc` | 102,833 | 63,930 | 62,770 |
| Bad Apple, `--live cga` | 78,193 | 48,852 | 47,995 |

## 6. Smaller things measured and left

- **The ring's mirror copy** (98.3): 32 KB of `rep movsw` once per *K*
  chunks, 1.8% of the machine streaming from XT-IDE with *K* = 8, and 8% at
  *K* = 2. Copying only what a super-packet straddling into slot 0 needs
  (`vp_mneed`, landed beside this report) measured 1.2%.
- **The CPU-copied disk**: MartyPC's XT-IDE option ROM took 16% of the
  machine streaming 58.5 KB/s.
- **`vp_adue`** (the card's clock) is ~900 cycles a call, twice a frame with
  sound: two divides and 32-bit arithmetic on memory.
- **`vidfskeysflip`** fails under four parallel lanes 3 times in 4 on the base
  and 2 in 4 after this pass, and passes alone: a Mode X flip's text caught
  off the glass on 1-2 looks in 16. **`vidwinshd`** fails its hold at frame
  100 alone, on the base as after (VIDEO-PLAN 8's known intermittent).
