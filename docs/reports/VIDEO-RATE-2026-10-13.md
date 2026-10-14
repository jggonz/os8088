# Rate steps for a clip's own sixteen: colours, dither, hold

**A measurement, not a description.** Taken 2026-10-13 on branch
`video-palette` at `cd38b4d`, on a 4-core Xeon container with ffmpeg 6.1.1.
True of that tree's encoder and its `286-fast` profile; not maintained
against later trees. docs/plans/VIDEO-PALETTE-PLAN.md section 10 is what it
means.

**The clip**: the owner's Gas Gas Gas (not committed), 40 to 60 s - inside
the window of the owner's own `03-640FS.V88` - at `--preset vga4-full`
(640 x 360 on mode 12h), 30 fps, PCM8 at 11,025 Hz.

**The arms** (`os88venc`, `--start 40 --end 60 --fps 30 --audio pcm8
--rate 11025`):
- `--palette auto` (the EGA's sixteen), `clip16`, `clip8`, `clip4`;
- `clip16` with Knoll's plan cut to 4 candidates and to 1 (nearest colour)
  - an experiment in a scratch copy of the encoder (`KnollDitherer.N` from
  the environment, the 4 x 4 cell scaled to N), not shipped;
- `clip16` with a COLOUR HOLD of 24 and 40, first in the scratch copy and
  then as the shipped `--vga4-hold`, which reproduces the scratch figures
  exactly (433.6 and 241.9 KB/s lossless) and, at 0, the records and
  palette of the encoder before it byte for byte (MD5 of every record).

Each arm twice: `--profile lossless` for what the picture ASKS for, and
`--profile 286-fast` for what that machine's budget makes of it. The
budgeted files were then decoded record by record as the player would and
scored against the source - PSNR of a 3 x 3 blur, every third frame -
beside the encoder's own frames cut and seconds VISIBLY BROKEN.

| arm | lossless KB/s | flickering back a frame | `286-fast` KB/s | cut of 599 | broken | PSNR | colours a frame |
|---|---|---|---|---|---|---|---|
| EGA's sixteen | 165.8 | 575 | 158.0 | 27 | 0.3 s | 26.51 | 3.7 |
| own 16 | 433.6 | 3,026 | 338.6 | 289 | 0.9 s | 31.34 | 7.3 |
| own 16, plan of 4 | 430.8 | 3,052 | 337.4 | 289 | 0.9 s | 31.38 | 7.3 |
| own 16, nearest | 435.0 | 3,295 | 337.0 | 297 | 0.8 s | 31.27 | 7.2 |
| own 16, hold 24 | 241.9 | - | 227.7 | 71 | 0.5 s | 30.06 | 7.4 |
| own 16, hold 40 | 171.3 | - | 164.3 | 28 | 0.4 s | 27.63 | 7.5 |
| own 8 | 297.2 | 2,227 | 274.6 | 103 | 0.9 s | 28.75 | 5.3 |
| own 4 | 158.1 | 1,075 | 155.4 | 15 | 0.3 s | 27.17 | 3.5 |

And the hold alone through `KnollDitherer`, no encoder, the first 10 s at
30 fps: pixels changed a frame 29,028 held by the plan, 12,761 at 24, 8,253
at 40, 6,431 at 56; blurred PSNR 33.58, 31.41, 28.40, 27.00 (the frames
scored every fifth, 640 x 360, by a one-off script in `os88palsim`'s shape).

**Two cautions.** Blurred PSNR cannot see what the hold risks - a dot left
standing by a dim trail, the defect SPEC.md 98.2.5's plan test was added
for - and an average over the clip hides tearing, which is why the broken
seconds sit beside it. Both want eyes on the files.
