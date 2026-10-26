# How WaveRack was built

A diary kept while the work happened, for later reuse. Observations and
intentions are kept apart: what was MEASURED or SEEN says so, and what was
planned says that. The plan is [WAVERACK-PLAN.md](plans/WAVERACK-PLAN.md)
and the contract SPEC.md §122.

## The ask

The owner handed over three mockups of a "digital sound recorder" - a colour
face, the same face in black and white, and a shallow version for a
640 x 200 screen - and a written brief of thirteen user stories: record,
play, see and edit sound on an IBM XT-class machine, save WAV files, and do
it in a face that looks like a piece of 80s studio hardware. The brief asked
for the plan in a draft pull request before any code, and for that pull
request to be kept current as each story landed.

![The colour mockup](waverack/design/waverack-color.png)

## Wave 0 - finding out what the machine can already do

The first question was not "how do I draw a waveform" but "can this system
record at all". It can - SOUND.DRV's Sound Blaster tier has a record stream,
and an old package, the Recorder (§35), uses it - but only into ONE buffer of
the driver's staging pool, 20 KB at most: 1.9 seconds at 11,025 Hz. Worse,
the bytes reach that buffer through a kernel task, and a disk transfer
freezes every task for up to ~400 ms on an XT while the card's own ring is
372 ms deep. A recorder that writes to disk while it records would lose
audio on every write.

So the one change outside the package is a small one in the driver: let the
record stream capture straight into a ring the APPLICATION owns, the way the
Video Player's playback already plays out of one (§34.5.3). The card then
writes a 32 KB ring by DMA with nobody's help, and a 400 ms disk write is 4 KB
of a ring that holds three seconds.

The emulators set the limits of what can be checked here: QEMU's Sound
Blaster never raises a capture interrupt, and MartyPC's records a flat
mid-scale value. Both are useful - one for "no input", the other for rates,
counts and files - and neither is a microphone.

## Wave 1 - the face (US-01, US-02, US-03)

**What was built.** A real package, `WAVERACK.O88`, with all three layouts
in both themes and every control drawn from real state - which, before the
document wave, is an empty document: `NO FILE`, `00:00.00 / 00:00.00`, the
next take's format on the LCD's bottom line, a dark meter, an empty
selection field, and the waveform area showing only its ruler, grid and
centre line. Everything a later wave builds - Open, Save, the edits, zoom,
play, record - is greyed through ONE predicate, `wr_cmdok`, which a key, its
menu item and its keystroke all ask; a press on a greyed control says why on
the status bar (`Needs a Sound Blaster`, `No capture device`, `Not in this
build yet`). What already has state works: Loop, the next take's rate (a
real os88ui drop-down), INPUT GAIN and VOLUME (own hit-test and drag, `<`
`>` and `[` `]`, the wheel), the theme and the layout.

![FULL on a VGA, colour](waverack/waverack-vga-color-full.png)

**The layouts became data.** CDRack's three layouts are numbers written into
its asm, and it took three looks to get them right. WaveRack's are written
once, in Python (`tools/os88wrlay.py`): every key, slider, drop-down, field,
panel, label and picture, emitted as layout records and static-part op lists
that the asm interprets (`wf_deco`). That moved the "does it fit" question
to the host, where it is cheap: the generator's `--selfcheck`, in the make
rule, MEASURES the layouts - no two controls overlap, every label is clear of
every control, every caption and drop-down item fits at 8 px a system-font
character and 6 px a small-caps one, and each window fits the screen it was
cut for with the menu bar, the dock and the title bar counted. Its first run
caught three things before any asm existed: COMPACT's edit-row keys were one
row too shallow for a small-caps caption, a mitred 24-row digit set (MID's)
whose `1` inked 12 of 24 rows - CDRack's own lesson, caught by CDRack's own
check - and a logo letter whose rows were uneven. The previews (`--png`)
rendered each layout in each theme as a rough picture to LOOK at before the
asm was written; the real face matched them on the first boot.

![MID on EGA](waverack/waverack-ega-mid.png)

**The CGA's layout** is the mockup's shallow one at 624 x 132, the footer as
two rows. On 2.4:1 pixels the 7-row pictures sit beside small-caps captions,
the LCD's digits are 16-row block segments, and the drop-downs speak in short
forms (`Mic`, `11 kHz`). Every control was clicked with `tools/mouse.py
--screen 640x200`, and each answered: the greyed keys with their reasons,
Loop with `Loop on`, the sliders with their values, the Rate list sliding up
over its own box (os88ui's §13.14.2) and the pick reaching the LCD.

![COMPACT on the CGA, 3x](waverack/waverack-cga-compact-3x.png)

**Black and white** is the same face from the second role table: white
faceplate, black rules, the LCD still black with white segments, the meter's
dark cells outlined, greyed captions half their ink.

![FULL in black and white](waverack/waverack-vga-bw-full.png)

**What was discovered.**

- *The generated include has to be two halves.* The icon must follow the
  header, so its macro and every constant are needed before the first
  instruction, while the pictures and layouts are data that belong at the
  end. `build/wrart.inc` emits both under `%ifndef WRART_DATA` and is
  included twice.
- *Record's red dot without writing a pixel twice.* A key is one band and
  one blit, and a blit has one ink. The red dot is two bands that TILE the
  key's inside - rows (stacked) or columns (side by side) - the picture's
  part in red and the caption's in the key's ink.
- *A slider is one blit too*: groove, ticks and the thumb's outline in the
  slider ink with the faceplate as paper, so the thumb's face is simply
  paper and nothing under it is drawn twice.
- *The waveform band*, the heap claim §122.5 sizes for the document wave,
  is claimed at entry already (7 KB): the empty waveform's ruler and dotted
  grid are composed in it and put down in three blits around the centre
  line. Its x phase is composed in, because a 1bpp blit at an x off the byte
  grid starts at the byte column holding x (§5.4.2.8, CDRack's lesson).
- *A bug the boots found*: a View > Full Layout from the menu hung the UI
  task, because `wr_menu` reused SI as the window after the command - and a
  resize clobbers SI. The same shape was in the wheel handler. Both take the
  window off the stack now.
- *Disk space decided a disk.* Wave 1 alone took the 720 KB apps disk from
  701 to 713 of 713 clusters, so that disk leaves WaveRack off (a dated
  decision, §24.6.1); the media disk carries it at 360 KB.

**What was measured.** Image 16,310 bytes, packed file 11,646, bss 1,414,
the waveform band 7 KB. The identity check - change the face, force a full
repaint, PIL difference `None` - held on VGA colour, VGA black and white and
EGA, and across a COMPACT and back round trip on VGA. A relaunch after Close
drew a face identical to the first launch. Hercules was not looked at here
(QEMU cannot show it); that look is MartyPC's.

## Wave 2 - the document (US-04)

**The story.** Open a WAV and inspect it without loading it into RAM: a
real RIFF walk, the sample frame as the one unit of time, min/max envelopes
so a single-sample click is visible at full zoom out, both channels of a
stereo file, zoom down to the samples themselves, an overview, and a long
file that stays cancellable while it loads.

**The reference came first.** `tools/wrsim.py` is the package written in
Python: the same chunk walk with the same refusal codes, the same envelope
and the same view-cache columns, plus a generator for seventeen fixtures -
a tone, single-sample impulses, a stereo file whose right channel is
silence and one spike, an odd `LIST` chunk, unknown chunks, `data` before
`fmt `, the extensible format, a data size past the end, one that is not a
whole number of frames, an unfinished take, six files that must be refused,
and a 1.1 MB file that only a streaming scan can take. Its selfcheck reads
every file it can back through Python's own `wave` and is a fast suite row,
so the reference cannot drift under the soak row that holds the guest to it.

**What was built.** Three modules beside the face: `wrwav.inc` (the reader:
any 1..48 bytes of a file at any offset, read as the two clusters holding
them, so a header of any shape is one read), `wrdoc.inc` (sources, the
piece table, the 32-bit arithmetic, and `wo_read`, which reads document
frames for the UI task after restanding in the source's folder) and
`wrenv.inc` (the scan, the view, the view cache and its refinement, the
ruler, the lanes, the overview and the cursor). The scan and the refinement
run from the window's one-shot timer - a 16 KB gulp or an 8 KB read a tick -
so the desktop stays live and Stop, Escape or another Open cancels.

![A 1 kHz tone at Fit](waverack/waverack-w2-tone.png)

![Single-sample impulses at Fit: a min/max envelope keeps every one](waverack/waverack-w2-impulse-fit.png)

**The band holds everything at once.** The waveform band grew from the
lanes alone to the ruler, the lanes and the overview (10 KB), so the lanes
are still in RAM after they are drawn: moving the cursor puts the one
column it left back from the band with a 1-pixel blit and draws the line
again, which is exactly how wave 3's playhead will move.

![A stereo file: two lanes, L over R, the right channel's one spike](waverack/waverack-w2-stereo.png)

**Three colours in a 1bpp blit.** A blit has one ink and one paper. The
trace is green, the grid dark grey and the ground black, so in colour the
grid's columns are drawn as one-pixel blits whose PAPER is dark grey and
whose ink is the trace's - a trace crossing a grid line stays green - and
the runs between them one blit each. In black and white everything is
white and the whole lanes area is one blit. The colour face lost its dotted
quarter rows for it; the time grid on the ruler's majors stayed.

**Zoom to the samples.** Below the block size a column first shows its
block's range and is then refined from the samples, carrying a column
across reads when a column is bigger than one read, and including the
previous column's last sample so a sine at one frame a column is a line,
not a cloud of dots.

![The tone at fpp = 1, refined from the samples](waverack/waverack-w2-tone-fpp1.png)

![LONG.WAV, 104 seconds, scanned in 72 gulps](waverack/waverack-w2-long.png)

**Refusals say why.** Each refusal is its own code and its own sentence of
at most 22 characters, and the document is left empty with every claim
given back.

![ADPCM refused: Format 0011h: not PCM](waverack/waverack-w2-refused.png)

**The greyed keys on one bit, fixed.** Wave 1's review found the CGA's
greyed keys illegible: a checkerboard over a 5 x 7 caption with one-pixel
strokes leaves noise. A greyed key in black and white now keeps its caption
solid, dithers only its picture, and draws its frame dotted - one band, the
frame included, so it is one call rather than two.

![COMPACT on the CGA: live OPEN and +, greyed REC, STOP, PAUSE, PLAY, SAVE and -](waverack/waverack-w2-cga-keys-3x.png)

![The same on the black-and-white VGA face](waverack/waverack-w2-bw-keys-2x.png)

**What was discovered.**

- *A stale floppy reads as a broken package.* The first look read the bss
  through the current map while the scratch floppy still carried the
  previous build - every variable four bytes off, `we_busy` reading 3. The
  soak row now checks that the running package's code is
  `build/waverack.bin`'s before it reads a byte of its state.
- *Two real bugs the first boots found:* the block store clobbered the
  scan's buffer pointer (every block after the first was garbage - the
  reference comparison named the column), and the scan re-armed its timer
  only from its first step, so a file longer than one gulp stopped at 74%.
- *The format line is longer than wave 1 measured*: `22,050 Hz 16-bit
  Stereo` is 23 characters. The layout's longest text moved to it and the
  layout selfcheck re-measured every layout before the asm was rebuilt.
- *A packed WAV is not a WAV.* READ_AT is raw, so a compressed file's bytes
  are the compressor's; the open finds the file with `OSAPI_FILE_FIND_RAW`
  and refuses one with the `CZ` bit (`File is compressed`).

**What was measured.** Image 25,010 bytes (packed 18,926), bss 2,525. Heap
with a document open and idle, read off the kernel's claim map by the soak
row: 31 KB for a small mono file, 33 KB for LONG.WAV, 36 KB for the
22,050 Hz stereo file (38 KB is the ceiling); a refused open leaves only the
10 KB band. The soak row opened all seventeen fixtures through File > Open
and found the parse, the status sentence and the view cache equal to the
reference for every one, the impulses refined at one frame a column with
their neighbours reaching them, and a forced full repaint pixel-identical to
the incremental picture after the zoom. The CGA's 360 KB media disk is now
full (354 of 354 clusters): the next wave needs a decision there. Not seen:
Hercules, EGA's MID with a document, the wheel (QEMU's serial mouse sends
none), 86Box, real hardware, and any XT timing.

### Hercules, looked at by hand

QEMU cannot show a Hercules card, so the MID and COMPACT faces were looked at
on MartyPC's cycle-exact 5150 with a Hercules card, captured straight out of
video memory. Both draw as on the other adapters; COMPACT leaves the Disk
window below it repainted correctly.

![MID on Hercules](waverack/waverack-herc-mid.png)
![COMPACT on Hercules](waverack/waverack-herc-compact.png)

## Wave 3 - the transport (US-05, US-08, US-07's playback half)

**The story.** Play what is on the screen at the right pitch, pause and
resume without a stutter, stop and find yourself where you began, drag a
range and loop it until it is right - and see the level of what is playing
while it plays.

**The shape was Audio's, on purpose.** The Audio Player (§86) already
streams a WAV through the Sound Blaster's ring stream: the UI task reads
the disk into a look-ahead ring, a worker task converts out of it into the
driver's ring grant, and the worker wakes the UI when the look-ahead runs
low. WaveRack copies that shape, with one difference that matters: under it
is a *document*, not a file - a piece table that later waves will cut and
paste - so the fill reads document frames piece by piece through the reader
wave 2 wrote, and copies them into the ring. The worker never sees a file,
a piece or a loop; it sees a stream of frames.

**A loop is just a refill position.** When the fill reaches the end of the
region it carries on from the region's start in the same pass. The worker's
box filter runs straight across the seam, so a looped selection plays
seamlessly - and the soak row checks it by finding the selection's bytes
repeated, sample for sample, in what QEMU's card actually played.

**Pause closes the stream.** The obvious pause - stop feeding and let the
driver underrun - plays out whatever is in the ring first: up to 16 KB, most
of a second at 22 kHz. Instead Pause reads the exact played count (verb 9,
the DMA's own progress), closes the stream and keeps the position; Play
opens a new stream from it. Pause is immediate, nothing stale ever plays,
and resume and seek become the same operation.

![Playing LONG.WAV: LEVEL PLAY lit, the Play key latched, the output on the status bar](waverack/waverack-w3-play.png)

**The output is what the card takes, and the status bar says so.** An
8-bit mono stream at `R / d`, `d` the smallest whole number that brings the
rate under the card's ceiling: a 44.1 kHz file on an old Sound Blaster
plays at 22,050 Hz through a two-frame box filter and the status bar reads
`Out 22,050 Hz mono /2`. A 16-bit sample gives its high byte, a stereo pair
its average, and VOLUME is a 256-byte table the worker passes every byte
through - the document never changes. `tools/wrsim.py` grew the same
conversion in Python, and the soak row holds the captured output to it.

**The playhead is a number first.** It is derived from the played count -
`n` output samples are `n x d` frames into the play, then round the loop -
and drawn the way wave 2 drew the cursor: the column it left put back from
the band, the line drawn again. The view pages when the playhead leaves it,
rather than scrolling every tick.

![Zoomed in while playing: the view paged to follow the playhead](waverack/waverack-w3-play-zoom.png)

![Paused: the Pause key latched, the playhead held, LEVEL PLAY dark](waverack/waverack-w3-paused.png)

**Selecting without repainting.** A drag changes the selection on every
mouse move, and redrawing the waveform for each would be ~20 blits a move.
So the lanes and the overview are now put down as *runs of one class each*:
outside the selection, inside it, its edges, the grid's columns. A change
redraws only the columns between where the edges were and where they are -
measured with a counter build at five or six drawing calls a move. In
colour the selection is the trace on a dark grey ground with cyan edges; in
black and white a dither cannot be a blit's paper, so the span is the band
turned over for its one blit and turned back - an inverted span.

![A selection in colour: the trace on its own ground, cyan edges, its extent in the overview](waverack/waverack-w3-sel-col.png)

![The same in black and white: the span inverted](waverack/waverack-w3-sel-bw.png)

![A looped selection playing](waverack/waverack-w3-loop.png)

![COMPACT in black and white with the selection](waverack/waverack-w3-sel-cmp-bw.png)

![COMPACT on the CGA knob tree, a selection looping (3x)](waverack/waverack-w3-cga-cmp-sel-3x.png)

**The meter reads the half that is sounding.** The worker notes each
staged half's peak before the volume - and whether it touched a rail - in a
table indexed by the half's place in the stream; the UI looks up the half
the card is playing *now* from the played count, so the bar moves with the
sound and not with the staging, which runs ahead of it. The cells are two
24-bit masks, shown and wanted, and only the changed runs are drawn.

**What was discovered.**

- *SOUND.DRV is not wanted by default.* The first look said `Needs a Sound
  Blaster` with QEMU's SB16 in the machine: §51.3 makes every driver
  not-wanted until a `SYSTEM.CFG` asks, and the test's system disk now
  carries `build/mixercfg/SYSTEM.CFG`, as XTMP3's rows do.
- *A refinement talks over a pause.* Zooming while playing leaves the view
  coarse (the refinement waits, its reads would race the look-ahead's); on
  Pause it resumed and its `Ready` replaced `Paused`. It is silent now while
  a play is held.
- *The harness's absolute mouse is not a drag.* `tools/mouse.py to` pins the
  pointer against the bottom-right corner and walks back, so a "move" in a
  drag is dozens of moves across the screen; the first measurement read 88
  drawing calls a move. Relative moves (`mouse_move 8 0`) gave the real
  figure: five or six.
- *A selection is the cursor's too.* With a selection the cursor stands at
  its start, and a rightward drag moved the time display every move for
  nothing; it is redrawn only when `s` moves.

**Measured** (the `-DWR_CALLS` counter build): idle 0 drawing calls a tick;
playing 1.70 a tick at Fit and 3.08 at 256 frames a column; a selection
drag five or six a move; a full repaint 120. Image 30,179 bytes (packed
23,129), bss 5,016; heap while playing the idle document's plus the 32 KB
look-ahead; the 16 KB ring grant is the driver's.

**The 360 KB disk.** Wave 2 left `media360.img` with no cluster to spare,
and this wave's growth does not fit, so WaveRack is off it (a dated note in
the Makefile and §122.13). A 360 KB home for it is the owner's decision.

**Wave 2's review, folded in.** An independent read of wave 2 found no
memory fault and five things to fix, all done in this wave: a chunk size
that carried the walk's next position to just under 4 GB made the next
header wrap to offset 0 (the walk now tests that carry, and a new fixture,
`WRAPPOS.WAV`, holds it); the view's last first frame was rounded DOWN, so
below Fit the last few frames of a file could never be shown and a cursor
parked there vanished (now rounded up - the view may end a few empty columns
past the document); a frame over 2^31 read as negative; a short disk read
ended the scan silently over uninitialised blocks (now it refuses, and says
why); and §122.5 had a block count off by one.

**The soak row's first run caught the meter.** STEREO16.WAV's right channel
is silent but for one full-scale sample, and PEAK never lit: the meter read
the half sounding every three ticks, and a 93 ms half fell between two
looks. The hold and PEAK now take every half played since the last look.
The second run passed: STEREO16 and TONE44 at d = 2 sample for sample, the
loop's four laps byte-identical in the capture, the paused position equal
to the reference's arithmetic, and every claim back after Stop and after a
close.
