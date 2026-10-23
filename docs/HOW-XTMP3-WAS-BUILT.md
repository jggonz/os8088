# How XTMP3 was built

A diary, written as the work happened, for later reuse on the website. It
separates what was observed from what was intended, and names what was
actually tested. The plan is [plans/XTMP3-PLAN.md](plans/XTMP3-PLAN.md) and the
contract is SPEC.md §121.

## 2026-10-22 — the brief and the two pictures

The owner handed over a development package: a long coding brief and two
concept images. The brief asks for an MP3 player that is a proper integer
decoder on machines that can afford one and an honest, table-driven
approximation on a 4.77 MHz 8088. When even that cannot keep up, the player
decodes the whole track to a hard drive first and only then plays it. The
owner attached a second colour picture to the request. Its layout is the
monochrome picture's, on a teal desktop, and that one is the colour design.
The package's own colour concept is kept beside it for reference.

| colour (the design) | black and white |
|---|---|
| ![colour](xtmp3/design/xtmp3-color.png) | ![mono](xtmp3/design/xtmp3-mono.png) |

**What the pictures ask for, in the owner's words, split into look and
behaviour.**

- *Look*: an 80s stereo component. A logo strip with cross-head screws, a
  recessed black LCD with a cyan title line, large green seven-segment
  minutes and seconds, a thin `/ 04:38`, a green dithered progress bar and
  `MONO 11 kHz`. Two cream VU meters with a red overload zone over an OUTPUT
  rule. A machined VOLUME knob with a tick ring, MIN and MAX, between two
  grilles. Bevelled transport keys with Play latched green, and an OPEN key.
  A queue with a blue selection, and a status bar.
- *Behaviour behind them*: the transport, the queue, Open, the volume, the
  quality shown in the status bar (`Quality: FAST`) and the state
  (`Playing from cache`). The brief says these must be TRUE: "Playing from
  cache" appears only once a conversion has finished, and two meters must
  not claim stereo on a mono output.

**How the OS answers what the pictures invent.** There is no check mark in an
app menu, so the quality item names its state. There is no Help menu, so
About is `OSAPI_ABOUT_SET`'s card. The window chrome, the desktop and the
menu bar are the OS's. The second meter becomes the buffer gauge when the
output is mono.

**The decision that shaped everything else** is that an 8088 cannot decode
MP3 in real time. The arithmetic below is a model to be replaced by
measurement. Synthesis alone is 16 multiply-adds per output sample. An
8088 `imul` is about 130 cycles, and a product-table lookup with its
addressing is about 50. At 11,025 Hz mono that is 8.8 M cycles a second for
synthesis by table, against 4.77 M available. So the XT path is designed
around Decode First from the start, and the tables are there to make that
conversion shorter, not to pretend.

## 2026-10-22 — the work fanned out

At the owner's request the stories were cut into tracks that touch disjoint
files, each in its own worktree: the host reference decoder, the sound
driver's 16-bit/stereo streams, the XT approximation study, and the face.
They ran at once. The table in the plan says which track waits for which.

## 2026-10-22 — the face

*The face track (S4), on `xtmp3/face`. What was built and looked at; the
contract notes are [docs/xtmp3/FACE.md](xtmp3/FACE.md).*

![The colour mockup beside the VGA face, scaled by the mockup's 1.6](xtmp3/vs-mockup-color.png)

**Observed, in this order.** The art came first, as the retro-face skill
says, and was looked at zoomed before a line of assembly. The three codex
masters turned out to be exactly on their block grids in exactly the
sixteen colours (not one non-uniform block in 10,720), so deriving them is
reading block centres, not resampling. The first previews were honest
about what needed a hand: the master's knob carries its indicator as a
white groove, which had to come out so the volume could turn it; the
first black-and-white knob dithered the master's own dither and came out
as a comb, until its greys were averaged first; and the seven-segment
digits in CDRack's mitred style looked like pills next to the mockup's
long lit bars, so a third segment style was written, trapezoids whose
45-degree ends meet along the corner's diagonal.

Then the package, CDRack's three-file shape. **It assembled and opened on
its first boot**, launched by double-clicking an `.MP3` on a scratch
floppy, which proved the association and `OSAPI_ARG_FILE` before any menu
was touched. The rest of that first look was a list: OUTPUT and VOLUME
crowded on MID, the empty total showing the estimate's tilde (its
"nothing" key has the estimate's bit set), an empty queue drawing a full
thumb, and removing the entry on show leaving the LCD saying NO TRACK
over a queue that still had tracks.

**The identity check** - change things through the face, capture, force a
full repaint, capture, difference - came back `None` on every layout and
adapter it was run on: FULL in colour and in black and white, C2, a FULL
to C2 to FULL round trip, MID on EGA in both themes, MID on Hercules and C1
on the CGA.

| | |
|---|---|
| ![VGA, colour](xtmp3/vga-color.png) | ![VGA, black and white](xtmp3/vga-bw.png) |
| ![EGA, MID](xtmp3/ega-mid.png) | ![Hercules, MID](xtmp3/herc-mid.png) |
| ![CGA, the compact player](xtmp3/cga-compact.png) | ![VGA, the compact player](xtmp3/vga-compact.png) |

**What differs from the mockup, on purpose.** The tracks are named by their
files (`NEONDRIV`) because nothing reads a tag until the parser (S5). The
right dial reads BUFFER and its zone is green while the output is mono -
the brief's rule that the meters never claim stereo the output does not
have, so with the demo's XT FAST rung the face cannot show the mockup's
L and R. The panel is the palette's light grey where the mockup's is a
lighter grey no single colour gives. The status line says "Playing
decoded file", the state the SPEC names, where the mockup says "Playing
from cache".

**Intentions, not yet observations.** No cycle has been counted: the
needle box and the knob's canvas are the two paths an XT with a VGA will
feel, and that measurement belongs to S10 on MartyPC. The engine behind
the face is a demo that plays nothing; S8 replaces it behind the same
names. Looked at on QEMU only - Hercules through its software renderer -
and never on 86Box or a real card.

## 2026-10-22 — the XT backend on the target

*The XT track (S7), on `xtmp3/xt`. The contract notes and every table are
[docs/xtmp3/XT.md](xtmp3/XT.md). The emulators were Unicorn on the host,
QEMU for the exactness row, and MartyPC's IBM 5150 for every cycle. Nothing
here ran on 86Box or on iron.*

**Observed, in this order.**

- **The model came first.** `tools/x3simxt.py` runs x3sim's front end into
  the study's integer chain. It settles what the study had left open:
  - requantisation through RQL;
  - the per-subband IMDCT switch;
  - a mono downmix that mixes spectra when both channels' blocks agree and
    windowed outputs when they do not;
  - V codes taken from the DCT's outputs, the sign going into the add or
    sub.

  Its first quality run reproduced the study's figures within a few tenths
  of a dB on every XT fixture, before a line of the part existed.
- **Then the part, and a generator beside it.** `tools/x3xtgen.py` writes
  every address the 8088 does not need to compute: the layout of the
  claim, Lee's DCT as straight-line code, each profile's window pass as
  unrolled rows.
- **OPEN's expansion matched on the first run.** In Unicorn, every table
  OPEN built was byte-identical to the generator's on all four profiles.
- **The first granules were bit-exact too**, on the two XT fixtures for
  every profile. Because that seemed too easy, a deliberately damaged
  constant went in; the first damage was too small to reach 8-bit output,
  and a larger one was caught at granule 1.
- **The STANDARD track then pinned the record and the ABI**, and the part
  was moved onto them. That meant the header vector table, CAPS and OPEN
  records, separate claims per segment, and STATS' layout. One decision
  came out of it: the OPEN record has no slot for an asset, so the part
  carries XTGEN.BIN inside itself.
- **The exactness row reuses the STANDARD track's bench mechanism.** It
  runs QEMU booting the shipping front end into the XT part. All 51 cases
  were exact.
- **MartyPC agreed bit for bit as well.** Its 8088 core gave the same PCM
  on every granule it timed.

**What the measurements changed.**

- **The model priced the kernels well and the code between them less
  well.** The first build took 1,662,211 cycles a granule for XT Standard
  on the song, where the model expected 1.37 M.
- **The IMDCT crossover is 12.5 lines.** The study had modelled 9.6, and
  the factored form costs 17,900 cycles, not 11,300.
- **Nine changes followed, each re-checked bit-exact and re-measured
  only where it moved.** The largest:
  - the bottom of Lee's recursion kept in registers;
  - the IMDCT window fused with the overlap;
  - butterflies with two zero lines skipped.

  Together they brought XT Standard to 1,505,847 (−9.4%). The window
  pass, the largest stage, was already within 11% of the 1,277 cycles a
  pair measured alone in the study.
- **Memory was the surprise.** Summed honestly with the engine's
  primary, the XT Standard path came to about 413 KB against SPEC.md
  121.10's 400,000 bytes.
- **The answer was the OS's own shrink-to-fit.** Everything only OPEN
  reads now sits behind a resident 29 KB, checked by a Fletcher sum
  before it runs. OPEN copies the chosen profile's kernels into a slot,
  and the claim can then shrink, with no change to the ABI.
- **The harness proves the shrink is safe.** It overwrites the tail with
  `int3` after every OPEN and decodes anyway.

**Intentions, not yet observations.**

- **Fitting 400,000 bytes for XT Standard also needs the engine** to
  claim its ring after OPEN and the shrink. The numbers went to the engine
  track; the decision is theirs and the integrator's.
- **No figure here includes the front end's Huffman decode or the disk.**
- **The 286, 386 and 486 numbers are 86Box's to take.**
- **Nobody has listened to the target's own output yet.** Its PCM is the
  model's to the byte, so the study's listening WAVs stand in for it.

**Later the same day: the merge, and OPEN.**

- **The integration branch now has one parts table.** FRONT and ART are
  eager and STD is lazy; the XT part became row 3.
- **OPEN dropped from 4.25 s to 2.96 s on XT Standard**, still on
  MartyPC, with every expanded byte unchanged:
  - the Fletcher loop is unrolled;
  - the encoder walks hold the midpoint they are waiting for in a
    register;
  - the expansion loops keep their constants in registers.
- **The Fletcher stays a read-back**, not a sum folded into the stores. A
  read-back is what catches a bad RAM cell on a 1981 machine, and a
  store-order sum cannot.
