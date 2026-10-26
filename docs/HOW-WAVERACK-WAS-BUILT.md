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
