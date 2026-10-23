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

## 2026-10-22 — the front end and the STANDARD decoder on the target

*The STANDARD track (S5 and the bit-exact half of S6), on `xtmp3/std`. The
contract notes and every number are [docs/xtmp3/STD.md](xtmp3/STD.md).*

**The contract went first.** Within the first hour `apps/xtmp3/x3gran.inc`
was pushed - the granule record, a backend's six far verbs and their
register contracts, the front end's API - because two other tracks were
writing against it at the same time: the XT backend reads the record and
the engine calls the verbs. One correction followed it, in a comment
(x3pars.inc reaches the FRONT part through ES, not DS); the ABI did not
move.

**Observed, in this order.** The front end matched x3sim on its first full
run: every granule record of 32 fixtures - each `ix`, each requantisation
exponent, each stereo plan byte - and only the five titles were wrong (the
title copy wrote its NUL at the start of the field). The bench that showed
it is a bare-metal harness on QEMU that %includes the shipping parser and
loads the package's own parts, far-called on a 384-byte stack; QEMU is
exact about the work, which is all bit-exactness needs.

Then the STANDARD part, 1,700 lines of assembly, came out **bit-exact on
all 141 golden cases on its first run**. That was not luck so much as a
decision: the two stages whose order of operations is the hardest to
transcribe - the recursive DCT-II and the 16-tap window - were never
transcribed. `tools/x3stdgen.py` runs x3sim's own `dct2_i` on symbolic
values and writes down the operations it is asked for, in order.

![The queue reading real titles and durations (QEMU, VGA)](xtmp3/s5-queue.png)

S5's outcome was looked at on QEMU: the queue shows NEON DRIVE from its
ID3v2 tag, AFTER HOURS from an ID3v1 tag, and a tagless VBR file under its
name with its exact length from the Xing count; an estimated length carries
its `~`.

**The 8088 said what the model could not.** On MartyPC's 5150 the first
measurement was 6.81 M cycles a granule for an 11 kHz mono stream - twice
NUMERICS.md's model, and 27 times too slow for real time. The breakpoint
timer showed where: the window was exactly where the model put it, but a
32 x 16 multiply with its 48-bit accumulation costs ~460 cycles, not 330,
and the band scale, Huffman and requantisation ran 4 to 10 times their
model figures. Six rounds of exact optimisation, each re-measured, took
it to 5.57 M (and 44.1 kHz stereo from 12.86 M to 9.31 M); the window and
the DCT are now within about a fifth of the multiply floor. The 8088's PCM
was checked against x3sim's by CRC-32 on every case timed.

**What the clean fixtures could not show.** No golden fixture saturates, so
every clamp in the decoder was correct only by reading - including the
ones that had just been moved out of line. Six fixtures with their global
gain raised by 24 were added to the gate; they saturate in five stages, and
on their first run they found a real defect: the requantiser tested SF
after `RCL`, which sets no SF. A normal stream never reaches that line.

**Intentions, not yet observations.** No 286, 386 or 486 has run it (S8's
ladder measures those on 86Box), the engine behind the face is still the
demo, and seeking is written but unexercised.

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

## 2026-10-22 — the engine, over a backend that only pretends

*The engine track (S9 Decode first, with the plumbing of S6's live path and
S8's ladder), on `xtmp3/eng`. The contract notes are
[docs/xtmp3/ENGINE.md](xtmp3/ENGINE.md).*

**The premise.** The real decoders were being written beside it at the same
moment, so the engine was built against a **stub backend**: an `OP_SEG` part
that speaks the decoder ABI exactly, makes a square wave instead of music,
and takes as long per granule as a test tells it to. A rung configured at
0.80x calibrates under 1.25 on any host, QEMU included. That one hook made
the ladder's every transition (stay, step, step twice, give up and decode
first) something a row can assert rather than something a person has to
hear.

**Shared source, not a copy.** The look-ahead ring and the speaker and Covox
bracket are the Audio Player's own code, included as it is. The look-ahead
had to be split out of `apengine.inc` into `aplook.inc` first, and the test
of that split was that AUDIO.O88 assembles byte for byte as before on all
five of its arms. It does.

**Observed, in roughly the order it was found.**

- The Save dialog's callbacks run unbound, billed to no instance, so the
  first Save dialog opened wherever the *machine* was standing, which was
  B:, and not on the volume just chosen. Every dialog action is now posted
  to the main window's wake.
- `pit_now` steps back one tick under QEMU when the 8253 has reloaded and
  its IRQ0 is still pending, and a negative span read as a huge positive
  one. The engine treats a span that goes back as zero, and the stub's own
  clock adds the pending tick.
- At 44.1 kHz 16-bit stereo the card wants 176 KB a second, and the
  driver's 16 KB grant is 93 ms of it. Fast streams now play from a 32 KB
  external ring of the engine's own.
- MPEG-1's second granule was being dropped: one `jb` that should have been
  a `ja`.
- The first probe of a session measured slow. Part of that was the stub's
  spin ending early across the same IRQ0, and part was every cold cache's
  first use. A warm-up granule, untimed, now goes first.
- The last one: a rung slowing to 1.05x underran **before** its slow windows
  could step it down. The trace showed the lead falling from 24 KB to 8 KB
  across a single sleep. The worker slept one tick whenever the ring was
  half full, and one tick is up to two. On 176 KB/s's 32 KB ring, that left
  less than the two blocks the SB's interrupt needs fed. The worker now
  sleeps only on a lead that a sleep cannot drain. At 176 KB/s that means
  never.

**What was tested, and where.** All of it on QEMU, with an SB16 whose output
was captured to a WAV on the host, over hard disk images built for each
case. The `xtmp3eng` row's ten legs passed on the branch's final build:
the ladder on five machines, the slowdown at 1.05x and 0.60x, Decode first
end to end with the WAV's every field checked on the host, cancel (0.22 s
from Esc to READY), a disk filled mid-write, stale and incomplete caches,
no fixed disk, two volumes, a finished WAV on the card with no decoder
resident, and pause.

**Intentions, not yet observations.** No real MP3 granule has gone through
this engine. The STANDARD track's front end does not yet fit beside it in
the primary segment, about 2.3 KB short. No timing here is an 8088's: the
stub's costs are set by hand, and MartyPC's turn is S10. The speaker path
was checked by its state only, because QEMU runs an 11 kHz IRQ0 too slowly
to listen to. A volume label cannot be shown, because no slot gives one to
a package.

### Later the same day: onto the real front end, and under 400,000 bytes

The face's art moved into a part of its own, which freed the primary. The
engine then merged `feature/xtmp3`, dropped its stand-in front end, and ran
on STANDARD's `x3pars.inc`. Every leg of the row passed on the first run
over the real front end. The stub backend became the parts table's row 4.

**Observed.** The XT ceiling was where the work went. The first XT Standard
conversion, run on the shipped package with the real XT part, measured
**421 KB** and then stopped with "Out of memory". There were two causes:

- The ring was claimed before OPEN, and the probe's unshrunk part stayed
  resident into the job.
- The stage was a separate claim. With the XT backend's five work segments
  it was the ninth claim of an owner the heap allows eight.

The order is now OPEN, shrink, then the ring. The ring is 16 KB under the XT
backend, and the stage rides inside the ring's claim with 4 KB halves. The
fingerprint, which had read its 16 KB in one go, now reads in pieces. On the
first run after that, the conversion finished at **386 KB = 395,264 bytes**.
`X3S_PEAK` is now the heap's own measurement, not a sum of estimates.

**Tested where.** QEMU, every leg; the ceiling on the real XT part, on QEMU.
No XT has run it.

## 2026-10-22 — the documents, the guide and the disks

The last track before acceptance turned the six tracks' contract notes into
one contract and decided which floppies carry the player.

**The contract as shipped.** SPEC.md §121 was pinned on the first day and
had drifted from the code in a dozen places. It now describes the five-row
package (FRONT, ART, STANDARD and XT parts beside the primary), the front
end split between the primary and FRONT's NEXT verb, the ABI in
`x3gran.inc`, the claim order that keeps an XT conversion under 400,000
bytes, the meters' rule, the layouts as built, and every place the engine
departs from the plan. Each subsection says whether it is shipped or still
pinned. The measured section (§121.11) keeps every number the tracks took on
MartyPC, and leaves marked rows for the period-machine runs that come next.

**Where it ships, decided by counting clusters.** The player file is 96,723
bytes and the three demo songs 151,261. The 1.44MB software disk had room
for all of it, and has 383 of 2,847 clusters left. The 1.2MB disk had 393
clusters free; the player takes 189, and the songs that fit beside it are
NIGHT SIGNALS and AFTER HOURS, leaving 45. The 720KB disk had 36 free
against the player's 95, and the 360KB software disk 14. The 360KB music
disk had 147. XTMP3 went there, at its root, the way MIDIRack did, with
AFTER HOURS, the 11 kHz mono song an XT is for. That leaves the music disk
22 clusters. A new `make xtmp3disk` puts the player and all three songs on
a floppy of its own in every size, which is the 720KB machine's only copy.
The small kernel's disks leave it off on a requirement, not on size: it
streams with two calls that kern_small lacks, its sound is SOUND.DRV's, and
its XT working set is three times that machine's RAM.

**MP3s ship plain.** The player streams its file, and a compressed file
reaches it raw, so a packed `.MP3` would be refused by the player's own
probe. The Makefile refuses to route a demo song through the packer, and
the image gate now reads every `.MP3` on every shipped floppy to prove it
starts with a tag or a frame. It found six, all plain.

**The pictures the earlier tracks had not taken.** QEMU, with an SB16 and a
scratch hard disk, drove the shipped package through the states nobody had
photographed: live 16-bit stereo, the queue with the songs' own titles, the
Decode First dialog, the Save dialog, a conversion in progress and the
decoded file playing. A ten-second song converts too fast on QEMU to
photograph, so the last four pictures use a five-minute stream,
`AFTERX30.MP3`, made by repeating AFTER HOURS' frames thirty times. It has
no tag, so its title is its name. The machine was paused at 33% for the
progress picture.

| | |
|---|---|
| ![Playing live: NEON DRIVE in 16-bit stereo on an SB16, both dials VU](xtmp3/s10-live-stereo.png) | ![The queue with the songs' own titles; AFTER HOURS live in mono, so the right dial is BUFFER](xtmp3/s10-queue-live.png) |
| ![Decode First, with C: and the room and time the file needs](xtmp3/s10-decode-drive.png) | ![The Save dialog standing on C:, the name already the song's with .WAV](xtmp3/s10-decode-save.png) |
| ![Decoding to disk at 33%, with the speed QEMU manages](xtmp3/s10-decode-progress.png) | ![The decoded file playing](xtmp3/s10-decoded-play.png) |

![The progress window, enlarged](xtmp3/s10-decode-progress-zoom.png)

**Observed.** The status bar's quality field is too narrow for its longest
name with the override marker: it reads `Quality: XT STANDARD (se`. That
has gone to the integration track rather than being fixed here, along with
the finding that a machine short of memory says only `Error: Out of memory`
and states no minimum.

**On an XT.** The guide's screenshots were taken on MartyPC's 640KB VGA XT
with the music disk in B: and no sound card or hard disk. A double-click on
AFTERHRS.MP3 opened the player, the check found no live quality, and Decode
First came up by itself with XT STANDARD chosen and *This machine has no
hard drive to decode to.* That is the shipped package choosing for a
4.77 MHz 8088 on its own.

**Tested where.** `make` and `make KERN_SMALL=1` on the host, the image
gates (`t_image` with its new MP3 check, `t_livefull`), `guidelock` and the
guide render, the kern_mega apps disks built and measured, and the derived
disk lists printed through make. The pictures are QEMU's (VGA, SB16) and
MartyPC's (VGA XT). No soak row and no `test-full` was run; those wait for
the final pass.

## 2026-10-22 — the field: the whole player on the XT, and the ladder up to a Pentium

Every number before this day timed a PART. This was the first time anyone
measured XTMP3 itself, doing what a user does: open a track, let Auto
decide, decode first, and play.

**The instrument came first, and it changed once.** MartyPC's XT
(`os8088_xt_vga_hdd_sb`: VGA, XT-IDE, a DSP 2.01) boots off a VHD holding
the system, the package and one MP3. The first version sampled the 8088's
IP at host-timed intervals, and it said the front end took 28% of a
conversion. That figure was wrong. A sample past the end of the FRONT
part's 24 KB image had been charged to its last label, a data table called
`xf_blk`, and the code really running there was the face's. Bounding each
region by its image fixed it.

The second version drives the machine with `advance` in pseudo-random steps
of guest cycles. That makes every sample uniform in GUEST time, and the
breakpoints on the engine's procs land in the same steps as exact stamps.
With the same buckets, the two samplers agreed to within a point, so the
host's speed was not biasing the first one. The guest-uniform one is what `tests/xtmp3xt.py`
keeps.

**Observed (MartyPC, cycle counter):**
- XT Standard converted the 10 s, 11 kHz test track in **182.8 s
  (0.055x)**. The part alone is 0.166x. XT Fast took 169.3 s (0.060x), XT
  Speech 118.3 s (0.085x).
- Every WAV, read off the disk image afterwards, was **bit for bit** what
  `tools/x3simxt.py` makes after the trim. So was the Quarter conversion of
  the 44.1 kHz track, against `tools/x3sim.py`.
- Broken on purpose (XT Standard's WAV held to XT Fast's model), the row
  failed at byte 6.

**Where the time went is the day's finding.** The face took 44.5% of the
conversion's cycles, more than the decoder's 38%. While XTMP3 decodes, the
VU meter follows the decoded audio. On a 4.77 MHz XT with a VGA each needle
redraw costs about 140 ms, at most every five ticks. A knob turn costs
400 ms. Playing the finished WAV, the face takes 55-74% of the machine. The
Sound Blaster's DMA carries the sound, so nothing was heard: no underrun,
and no granule decoded. The decode-first dialog's estimate ("about 1:31")
is therefore half the real three minutes, because the probe never paid for
the face.

**Calibration** took 16.1 s on the XT before the dialog: three probes, each
mostly the part's fetch and OPEN rather than its timed granule. X3S_PEAK
during calibration read 402 KB, which is over SPEC.md §121.10's
400,000 bytes. The conversion's own peak, 386 KB, is inside.

**The ladder on 86Box** was run with no hands. A system floppy's
`\AUTOSTRT` opens the track, a report build of the package writes what
it decided to the floppy, and the host reads the floppy image. The 486's
first boot stopped at "CMOS system options not set - press F1", which
nobody here can press. The fix was to borrow a checksum-valid CMOS image of
the same board, set the drive types and re-sum it.

**Observed (86Box, the emulated clock):**
- The XT and the 286 decode first on both tracks. The 286's best rung on
  11 kHz mono was XT Fast at 0.80x.
- The 386 plays the 11 kHz track live on XT Standard. It underran once and
  stepped down to XT Fast, as the contract says it should.
- The 486 plays 11 kHz on Original. The 44.1 kHz stereo track sent it to
  XT Standard live, and that ran out of memory. The engine does not step
  past a rung whose claims fail, so a 486 cannot play that file.
- The Pentium-133 plays it on Quarter.
- On the XT the two instruments agree: 0.03 / 0.11 / 0.11 against
  0.03 / 0.11 / 0.12. That disagreement sits exactly on the offline pick's
  1/8 threshold.

**Sent to the integration track:** rest the needles during a conversion;
step on, or decode first, when a rung's claims fail; the calibration peak;
the estimate.

**Tested where:** MartyPC (cycles) and 86Box (its emulated clock); QEMU
only to watch the report file get written. No real machine has run any of
it.

## 2026-10-22 — the real decoders, end to end

Phase 3 put the STANDARD and XT parts behind the engine and asked one thing
of every leg: that what comes out be held to the reference, sample for
sample, not listened to.

**How the card was heard.** QEMU's SB16 writes what it plays to a `wav`
audiodev, and the audiodev was set to the STREAM's own rate, so QEMU's mixer
resamples nothing. A capture is then searched for `tools/x3sim.py`'s PCM for
the same file and divisor, and compared whole. The first run of the first
leg, 44.1 kHz stereo on Original as 16-bit stereo, was exact: 66,150 frames
of 66,150, at capture offset 0, the two channels distinct. So were 22.05 kHz
stereo and Half by override.

**Observed.** The 8-bit cases were where QEMU had to be read before the
decoder could be. QEMU 11 widens an unsigned 8-bit sample `u` to
`(u - 127) x 257`, not `(u - 128) << 8`. Read back through that table, an
SB16 with no 16-bit channel played x3sim's high byte XOR 80h exactly, and a
DSP 2.01 played x3sim's mono downmix exactly - once the capture was taken at
the 22,222 Hz its time constant really gives, not the 22,050 asked for.

The 8-bit stereo leg also found two engine defects on its first run. A
DSP 4.xx without a 16-bit channel was treated as an SB Pro and held to
22,050 Hz a channel, so Original was skipped; and a skipped Original jumped
straight to the cheaper rung that had passed, Quarter, without trying Half.
Both are fixed, and the leg now plays Original in 8-bit stereo.

**"Original never calls the XT kernels"** is proved three ways: a fetch
counter a part (`x3_nfetch`) reads 0 for the XT part after every Original
leg; the STANDARD part's map has no `x3x_` label; and none of the XT tables'
generator bytes occur in it.

**The seek.** The engine's pre-roll was 2 frames. Held to x3sim decoding
from the same offset with the same reset, it was exact; held to an
uninterrupted decode, it was not. A host study answered how much pre-roll a
seek needs: over 749 seeks into 33 fixtures, 2 frames reproduced the
uninterrupted decode 15.8% of the time, 4 frames 92.5%, 6 frames all of
them. The reservoir reaches back up to 511 bytes, which at low bitrates is
several frames, and short blocks chain their overlap. `X3_PREROLL` is 6 now,
and the seek leg's output is identical to an uninterrupted decode from the
first sample after it.

**The XT path, end to end.** XT Standard set by the user, decoded first to a
QEMU hard disk: the finished WAV is byte for byte `tools/x3simxt.py`'s,
111,215 bytes. It then played on the card with no decoder resident and
seeked directly. Its part was fetched once, not twice: a job whose rung is
the probe's now keeps the probe's OPEN and only RESETs.

**The ceiling on any disk.** The engine had left two holes in §121.10's
400,000 bytes: a live XT play with a card, and a destination whose clusters
are larger than 4 KB. The buffers are now planned after OPEN, from what the
heap says is left (`x3_plan`). Under the XT backend the card gets the 8 KB
grant at any rate, and a conversion's stage is ONE cluster where two do not
fit. Measured: live with a card 387 KB; conversions to 8 and 16 KB clusters
385 KB. A 32 KB cluster cannot hold XT Standard at all - its work alone
leaves 34 KB, and one cluster plus the look-ahead and the PCM is 39 - so it
is refused by name, and XT Fast converts there at 387 KB. The plan also
found a deadlock no leg had met: an XT's 16 KB look-ahead over a 16 KB
cluster can never take another gulp, so the look-ahead now holds two of the
source's clusters at least.

**A step down is no longer a click.** A rung change changes the card's rate
or format, so the stream must re-open. The old stream now ends on a
32-frame ramp to silence, published as its last half, and the new rung's
first 32 frames rise from silence; the position handed across is the last
REAL sample's, not the ramp's.

**The paths no one had taken**, each a leg now: a source changed during a
conversion (the job fails with the reason, nothing left behind); Decoded
Files deleting XTMP3's own WAV and leaving someone else's that a record
named; an approved batch converting the second track with no dialog; a
read-only fixed volume (a CD through CDROM.DRV - QEMU's IDE has no
write-protect for a hard disk); an lz4-packed MP3 refused by name.

**The speaker could not be heard on QEMU, and the Covox could.** QEMU's
pcspk only synthesises a PIT mode-3 square, so its capture of the speaker
bracket was empty. The same bracket on a Covox writes each sample to the
LPT data port, and QEMU traces those writes: 32,724 bytes in 45 seconds,
every one the finished WAV's own, in order.

**Tested where.** QEMU, every figure; the `xtmp3live` and `xtmp3eng` rows.
No timing here is an 8088's: the FIELD track measures the real machines.
