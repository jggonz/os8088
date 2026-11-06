# How Video Battle was built

A diary of the first build of Video Battle (SPEC.md §125,
docs/plans/VIDEO-BATTLE-PLAN.md). It records what was decided and what was
discovered, and names what was measured and where. It says how the engine
came to be, not how it works now: SPEC.md §125 is the authority on that.

## The question (2026-11-03)

The owner asked whether one battle of a turn-based game could be played as
video on a 5150. The idea:
- an intro;
- an idle loop on the upper part of the screen;
- a simple interface drawn under it;
- each command a clip that joins the loop and leaves it smoothly;
- music with one sound effect at a time.

The feasibility study is the plan's sections 1 to 9. Its findings:
- A Sound Blaster at 11 kHz leaves the CPU nearly free, about 8% for the
  sound with an effect playing.
- The hard part is **joins**. A delta record is right only on the screen
  it was made against, and a lossy encoder's "frame 12" is a particular
  decoded screen.
- The answer to joins already existed in the format, as the seam.

## Decisions

- **Original content only.** The owner's own footage was from a
  commercial game. The engine and its tools are general; the demo battle
  is drawn and scored from nothing captured (`tools/vbtscene.py`): two
  heroes and an iron beetle on a grid under a starry sky, a chiptune loop
  in A minor, six synthesized effects.
- **A container of its own, the record format shared.** V88 interleaves
  sound with frames in one stream. A battle is many short streams over
  one sound, so `.VBT` is new, and every frame is still the Video Player's
  ten lists, decoded by the same `vdec.inc`.
- **Exact joins by construction.** The host makes every record between
  the screens the decoder will really hold:
  - a loop record from each frame to the next;
  - an entry record from each ANCHOR frame to the action's first;
  - a seam from the action's last frame back to the loop.

  `os88vbt.py verify` decodes every path through `os88vid.decode_lists`.
- **Several anchors, not one.** The idle loop's period was measured off
  the owner's own capture as about 24.5 frames. With one entry frame a
  command could wait 1.6 s; with four anchors it waits at most 0.47 s.
  This costs one small record per anchor per action and no new footage.
- **The selection lives in the band.** The member whose turn it is is
  drawn inverse in the interface, so no clip carries a marker and no clip
  is captured twice.
- **The card is the clock**, as in the Video Player. The effects are
  booked at the sample their frame will be shown at, because the ring
  runs 0.37 s ahead of what is heard.

## Discoveries

- **Keys are delayed, not lost, behind a floppy read.** An Enter was taken
  0.4 s after it was pressed, behind an 8 KB read (MartyPC 5150). The read
  piece became 4 KB, at most about one revolution.
- **Eight claims an owner.** One claim per region and per clip passed
  `MEM_OWNER_MAX`, so claims were refused with 250 KB free. The read-ahead
  then threw clips away, and actions streamed from the floppy while they
  played: 23 late frames in a battle. With three claims, carved, the same
  battle had 0 late frames.
- **A whole-canvas change tears on CGA.** The first bolt flashed the whole
  sky and was captured half drawn, one bank new and one old. A captured
  battle's camera cuts will do this. The cure is a second page, on
  Hercules or VGA.
- **The emulator's audio capture drains only while it runs freely.** Exact
  `advance` steps lost the Sound Blaster's samples. The recording therefore
  stamps each frame with its guest cycle and lays the frames on the
  audio's timeline.

## The second wave: the music streams (2026-11-04)

The owner: assume a hard disk; a floppy is a bonus. A clip's own effects
can stream in with it, and the menu's sounds can stay resident.

Decisions:
- **Format version 2.** Each clip's region carries its effects ahead of its
  records, so they are in memory before the clip can start. The resident
  effects are the menu's three.
- **A 32 KB ring for the music**, refilled 4 KB a piece at the music's own
  cluster-aligned offset. A piece's overhang lands in a 4 KB guard and is
  copied to the ring's start, so no read has to be split.
- **Menu sounds mixed backwards** into bytes already queued for the card,
  from a block past its last report, rather than at the write position a
  whole lead ahead.
- **512-byte card blocks** where the driver offers them, which makes that
  report four times fresher.

Discoveries:
- **The card plays 11,111 Hz**, not 11,025: the DSP's time constant rounds
  it. The capture only lined up with the bundle's music at that rate.
- **A streamed music and a floppy do not mix.** The ring ran dry 337 times
  in 150 s on MartyPC's 5150, and the clip chosen never loaded: the reader
  rightly put the music first, every time. Short music is held resident
  instead.
- **My own 48-byte row arithmetic was wrong** (34, not 48), and every row
  but the first was read from the wrong place. The battle stalled with a
  clip "playing" nothing. The fix is two lines; the lesson is a probe of the
  bss, which found it in one run.
- **A menu sound's latency is mostly the foreground's.** Breakpoints on the
  code that asks for it and the code that mixes it split the time:
  - the key took 29 to 47 ms to reach the menu when the loop was free, and
    about 180 ms when it landed during a read already under way (the CPU
    copies on MartyPC's XT-IDE);
  - the mix took 39 to 63 ms to be heard.

  Holding clip reads off for a third of a second after a key moved the
  median little, because the read a key waits for is usually one already
  started.

## The third wave: clips stream (2026-11-04)

The owner, on reading the builder's design: streaming is core, so it comes
before the builder. With it came openings with their own sound, the music
joining after them, and the band's and video's sizes as the project's.

Decisions:
- **Format version 3**: a clip is a stream of any length, and each record
  carries its frame's sound. An action's effects are mixed into its sound
  by the builder, so the engine's effect queue went. The openings are clips
  of their own kind, chained exactly by seams from a black screen.
- **Two 48 KB clip rings with 16 KB guards.** The guard holds a copy of
  the ring's first 16 KB, so a record that starts near the end is whole
  where it lies, and records are capped at 16 KB.
- **Sound and picture are two cursors on one ring.** The sound runs ahead
  by the card's lead, so the ring keeps what is past the earlier one.

Discoveries:
- **Every read was a seek.** One file cursor served the music and the
  clips, so each read walked the file's chain from its start. A 279 KB clip
  600 KB into the bundle took 36 s to play its 7.8. Each stream got its own
  cursor; the battle's length halved at once.
- **An off-by-one in the openings' order** left the second opening unread
  until its sound was due. The stalls it caused looked like disk throughput
  until a frame-by-frame log showed them starting exactly at the join.
- **The first Limit (34.6 KB/s plus 11 for the music) could not be fed**
  on MartyPC's XT-IDE, which moved about 35 KB/s. This diary first put that
  down to the disk's kind. It was wrong, and the owner doubted it: a
  PicoMEM in a 5150 moves close to 150. It was the reader's 4 KB pieces
  and its seeks; with per-stream cursors and adaptive pieces the same disk
  moved 68.6 KB/s while a clip played (see the check, below).
  Its sky flashes, 7 KB a frame, were the weight; without them, and with
  its rings of light drawn as thin outlines moving every other frame (they
  were drawn filled, a bug), the clip is 31 KB/s and plays in real time. A DMA disk is another matter, and the
  builder will budget each clip against the profile's disk.

| MartyPC `os8088_xt_hdd_sb`, C: | |
|---|---|
| the Limit, 250 KB | 7.67 s captured for 7.8 s |
| a battle to victory | 0 late frames, 0 stalls, 0 sound frames late, 0 music underruns |
| the music's join | at 3.903 s, the openings' end, correlation 0.97 |

## The check: the owner's footage on the owner's disk (2026-11-04)

The owner asked where the engine stands, with three clips of their own made
by an image model: an idle and two attacks. The attacks do not share the
idle's motion, so only their first frame matches it. A prototype of the
builder's footage path (encode with `os88venc`, decode to the screens the
engine shows, search by frame difference) made a bundle of them:

| | |
|---|---|
| the idle loop | frames 1 to 30 of the idle (2 s), its seam 1.8% of pixels |
| attack 1 | 77.3 KB/s, in at its frame 0 onto idle 0 (about 4.6% of pixels differ), out onto idle 0 (5.3%) |
| attack 2 | 53.5 KB/s, the same shape |
| anchors | idle frame 0 only: the attacks' first frame matches nowhere else |

And 86Box became the instrument for the owner's disk: a 5150 with a CGA,
an SB 2.0 and an ST-225 on a Seagate ST11M, the image wrapped by
`os88hdd.py --st11` and the engine started by an `AUTOSTRT` link.

It found three bugs the demo battle could not:
- **The music fell silent at 22 s and never came back.** Near the track's
  end the reader rounded what was left up to a piece in 16 bits; 61,441
  bytes and up lapped to 0, and it read nothing, over and over.
- **The enemy's attack played as an idle screen.** It used the clip a
  member had just played, and a ring given back kept its played cursors, so
  the clip was "played" from its end. A ring given back now rewinds: a clip
  that fit whole plays again unread, one that did not lets the ring go.
- **The victory never ended.** The adaptive pieces (up to 32 KB) read past
  the last clip, which ends at the file's end; the read failed and was
  asked again forever. A piece is now cut to the clip's last 4 KB.

Then the measurement. The battle plays to victory, but **every clip
stalls**: while a clip played the reader moved 59 to 77 KB/s, so the
77 KB/s attack stalled 1.8 to 2.7 s in its 6 and the 53.5 KB/s ones 0.6 to
0.8 s in their 5. With the music resident, so that no read seeks between
two streams, it was 52 to 62 KB/s and the same stalls: the seeks are not
it. The reader's loop does not wait either (its frame wait is the hook's
1.7 ms period). The disk is being slowed by the hook's decoding, to about
half of the 119 to 129 KB/s Video Player gets out of the same controller.
Observed: the numbers above, on 86Box (not cycle-accurate). Not yet done:
Video Player on the same 86Box machine, to say how much of the gap is the
engine's.

## Chasing the disk: Video Player as the yardstick (2026-11-04)

The owner: Video Player plays their `TAKEONME.V88` (95 KB/s, 640x200
mono, 24 fps) perfectly on the real machine, against a disk measured at
119 KB/s. "These two are completing the same task, aside from the
interface and sound mixing." So the same file went on the same 86Box
ST11M: **0 stalls, its reader moving about 115 KB/s.** The gap was ours.

The engine was given an account of its own time, in the hook's periods
(1.7 ms): periods inside `vb_read`, and marks around each part of the main
loop. The first answer was surprising - **inside a read the disk moved 140
to 200 KB/s**, faster than Video Player's average. The reader was simply
not reading most of the time. The marks then split the rest: the copying
after each read was **a third of the machine**. The ring copied every byte
that landed in its first 16 KB into the guard behind it, "so a record near
the end is whole", and every byte a piece put past the end back to the
start. That was 124 to 136 KB of `rep movsb` for a 412 KB clip, with the
hook's decoding on top of it.

Video Player had met this before (its `vp_mneed`: copy only as much as the
super-packet that runs on needs). The engine now does the same: a piece
ends at the ring's end, the reader walks the records it completed, and
only the tail of the one record that runs past the end is copied. 18 to 30
KB a clip. Stalls on the 77 KB/s attack fell from about 2 s to 0.5-0.95 s,
the battle from 54 s to 49, and the demo still plays with none.

Observed, not inferred: every number above is from the engine's own
counters on 86Box. What is left is the ring's depth (48 KB, 0.6 s of that
clip, against Video Player's up to 480 KB), and that is the next step.

## The rings take all there is (2026-11-04)

The owner: "We can and probably should take everything available; video
player also made this change. Nothing else can take RAM from us while we
run FSX." So the two clip rings left the resident claim, which the window
holds as long as it is open, and became a claim of their own made inside
the full-screen bracket and given back as it ends: the largest run less
8 KB, split two ways. The card's ring had to move first, to the load, so
the rings could not leave it without room.

A ring of 172 KB does not fit a 16-bit index, so every place in a ring
became a segment and an offset of 0..15, advanced by one helper the hook
and the reader share, and the stream's cursors became dwords. The hook's
stack budget (48 bytes) was the constraint on how that helper could look.

Measured on 86Box's 5150 with the ST11M, the owner's footage with their two
new gunner attacks (55.8 and 70.6 KB/s): **0 stalls, 0 late frames, 0
frames of sound late, 0 music underruns**, every clip of a battle to
victory, and a second play reclaiming the rings. The demo bundle the same.
The gunner clips need nothing of their own: in at their first frame, out
onto idle frame 0 at about 5.5 to 6% of pixels, like the knight's.

## The builder, wave 1: the project is the product (2026-11-04)

The owner, on the engine: "good enough for now to move on to the builder."
And on joins: their clips are generated, so each has one join point at
most, and "we'll probably need a way to hold more than one set of join
clips". The ~2 s an action can wait for its one anchor was, in their
words, not intolerable for this style of battle.

Decisions:
- **The entries got a region of their own first** (bundle version 4).
  Eight of the owner's clips had already overflowed the 60 KB the idle
  loop and the entries shared. A paragraph number names each entry, so
  any number of join sets can be held, as far as memory goes, and fewer
  join points simply cost fewer bytes.
- **The model before the window** (the plan's section 2): a project file,
  every edit a method through one door so undo is free, the checks and
  every recommendation computed and never stored. The window will be a
  view of it; a test already drives it as the window will.
- **A source is encoded once, whole, and sliced.** The joins are made
  between DECODED screens, so a clip cut from the middle of an encode is
  as exact as one encoded alone. The decoded screens are cached beside the
  project by the source's content hash: the owner's eight clips took 90 s
  the first time and the build 3 s after.

Discoveries:
- **The shipped bundle was never reproducible.** Re-expressing the demo as
  a project and building it gave the same pictures and different sound.
  `vbtscene` seeded its effects' noise with `hash(name)`, and Python
  randomises a string's hash in every process. The fix is a stable seed
  (a CRC of the name); the new test builds the demo in separate processes
  to keep it fixed.
- **A demo action does not start exactly on idle frame 0.** The test's
  first draft asserted that it did, and the analysis disagreed for the
  beetle's lunge (anchors 7, 9, 14, 23 score better). The analysis was
  right; the test now plants a clip that starts on a known idle frame
  instead.

Observed: the demo project builds `demo`'s bundle byte for byte, full and
floppy; the H3 project, its points from `recommend`, plays to victory on
86Box's ST11M with 0 stalls, 0 late, 0 sound late and 0 underruns.

## The builder, wave 2: a window over the model (2026-11-04)

The owner sent two more clips (a victory, and the scorpion hitting the
knight), "not great, but building up a test setup we can exercise", and
said to go for wave 2.

Decisions:
- **The window is a view and every long thing is a process.** The encoder
  window learned that the hard way (98.2.11.1), so here it is the shape
  from the first line: finding loops, encoding a source, costing the loop
  and making a WAV to listen to are `vbtana` commands that answer in JSON
  lines, and Cancel kills the process group.
- **The music's loop is found by listening, then placed by the waveform.**
  Short spectra are compared over a 2 s window after each candidate start
  and end; the best end is then moved to the sample where the waveforms
  agree, so the jump is in phase. On the demo's music the candidates come
  out on whole bars (19.2, 6.4, 12.8 s for bars of 1.6 s), found in 0.2 s.
- **The difference is marked in a colour the palette does not have.** The
  first draft marked it red, which is one of the battle's own four.

Discoveries:
- **An ending clip is not an exit.** The analysis offered the new victory
  clip an "out" point that rejoins the idle loop (44% of pixels apart:
  it never will); an ending plays to its last frame. The project now says
  so, and the Clips tab will need to.
- **A cancelled encode leaves workers dying for a moment.** The test's
  first look, half a second after the kill, saw nine. They are a pool's,
  in the same session, and gone within a second; the test now waits, and
  fails when the kill misses them (broken on purpose: killing only the
  parent leaves them running).
- **This container's Python has no Tk.** The system's 3.12 does once
  `python3-tk` is installed; the window was driven and photographed there,
  under Xvfb.

Observed: every tab action above, driven by a script under Xvfb on the H3
project. Not observed: the window on Windows or macOS, or a real person's
hand on it.

## The builder, wave 3: the Clips tab, and a zip (2026-11-04)

The owner asked for a package target like the encoder's, "then send me
the zip of that after each round, will make it easier to set up". `make
vbtbundle` is the encoder bundle's shape, its file list computed by the
same import closure; its test unpacks it outside the tree and builds the
demo battle there, byte for byte the tree's. Broken on purpose (a module
taken out of the zip) it fails as the owner would have, with an
ImportError.

Decisions:
- **The filmstrip shows BYTES as well as scores.** A score says how alike
  two frames look; the entry's size says what joining them costs. Costing
  all 30 of the owner's idle frames takes 0.1 s, so every frame shows both.
- **The report is computed in the window, the encode in a job.** Once a
  source's screens are cached a clip's report is 0.3 s; the first look at
  a source is still a cancellable process, and the tab will not compute
  anything until both sources are in hand.
- **Listen is the build's own mix.** The Clips tab's sound and the
  bundle's sound come from one call (`os88vbt.place`), and the test holds
  them equal frame for frame; broken on purpose (cues a frame late), it
  fails on every clip with an effect.
- **A clip's own soundtrack follows its range.** An action cut from
  footage usually wants the footage's sound; it is a sound of its own,
  named after the clip, and moving the clip's in or out moves it too.

Observed: the tab driven under Xvfb on the H3 project - the scorpion's
hit against the strip, idle frame 3 selected and differenced, an anchor
toggled and undone, the new victory clip shown as an ending with no exit.

## Wave 4: the rules, and a reference to hold the engine to (2026-11-04)

The owner: "lets build out first, then we can modify. Go for wave 4." Two
more clips came with the round before (a victory, the scorpion hitting the
knight), and the owner's five additions to the verbs (misses, defended
clips, timed stances, untargeted and area moves) were already in the plan.

Decisions:
- **The reference first, in Python, and the engine held to it number for
  number.** A battle's arithmetic is the kind of code that is wrong in a
  way nobody sees: a damage a few points off looks like a design. So
  `tools/vbtrules.py` defines the rules as the bytes the engine reads and
  a `Sim` that plays them, with the draws' order written down (SPEC.md
  125.6.3); the engine is that, routine for routine, and a test reads the
  engine's whole state at every turn and compares.
- **Draw at commit, peek at hover.** Rolling an action's outcome while the
  cursor merely rests on it would tie the battle to how the player moves
  the cursor. Instead nothing draws until an action is taken, and the
  read-ahead PEEKS the next draw - the pool's pick - to fetch the clip the
  action would most likely play.
- **The outcome before the clip.** Finishing, critical, defended, missed,
  hit: decided from the numbers, then its pool, so the footage always
  agrees with the damage on the band.
- **The first region grows only as far as the rules need** (8 to 16 KB,
  its length in the header), so a small battle's floppy keeps its room.

Discoveries:
- **[si+di] is not an 8086 address.** The first assembly said so in one
  line; an index plus an index needs an add first.
- **The engine stalled at its first Attack** in the owner's battle and not
  in the demo's: a project with no opening is given a START opening at
  clip 0 when the bundle is written, and the rules numbered the clips
  without it. Every clip was one off; Attack pointed at START, which has no
  join point. The numbering now counts it.
- **A rebuilt band flashed.** Putting the ground on the screen and then the
  letters over it shows the ground alone while the letters are drawn - the
  double-draw flash this project keeps a rule against. The band is now
  composed off the screen, text and all, and put up once.
- **The reference had its own bug**: two HEAL effects on one action added
  the total twice. Damage was already applied once; heal is now too.
- **The first engine-against-reference failures were the driver's**: it
  read whose turn it was before its settling pause and the state after
  it, and pressed the knight's keys into the gunner's menu after a
  clipless Defend. Read after the pause, the 4 HP difference went, and so
  did every other.

Measured: the owner's H3 battle with the full rules on 86Box's ST11M, 27
turns in lockstep with the reference, every number equal; the rules battle
on MartyPC's XT, 43 turns, the same; broken on purpose (the engine's
damage shift 3 for 4) it fails at turn 1. The builder's three new tabs were
driven under Xvfb on the H3 project; not seen on Windows or macOS.

## The owner's first look at the window (2026-11-04)

The owner used the builder and came back with a list before wave 5. Most of
it was one complaint in different places: the window answered what was SET
when it was asked what was CLICKED, and it did things quietly.

- **The idle preview jumped.** Two faults, both the window's, neither the
  loop's. "Play across the seam" played the loop's last eight frames into
  its first eight and then went round - so every sixteen frames it jumped
  from the eighth frame back to the end, a jump the battle never plays.
  And Recommend highlighted its best loop without putting it in the boxes
  (Tk's `selection_set` fires no event), so Play went on playing the old
  loop, or after a fresh load the whole video, which is no loop at all.
  Now "Play the loop" plays start to end-1 round and round, as the engine
  does; "Across the seam" holds black before it goes round; and the
  highlighted candidate is the one previewed, marked "not Set yet".
- **The previews show the choice.** The Idle tab's picture, seam and cost
  follow the boxes and the clicked candidate; the Clips tab's picture is
  whatever was clicked last - a frame of the clip (the scrubber), of its
  source (the In or Out field, without setting it), or of the idle loop
  (the filmstrip).
- **Adding.** Drag and drop anywhere: a project opens; a video becomes a
  clip (on the Idle tab, the idle's source, loaded and recommended at
  once); a sound joins the sounds (on the music, it is the music; on a
  clip, a cue at its frame); a picture joins the band pictures. The Clips
  tab has its own "Add videos...", several at once, read one after another.
  A video already a source is that source, not a second copy.
- **A bar.** Reading a video showed nothing for a minute or two; the
  encoder's own `--progress` now drives a percentage and the bar.
- **Own soundtrack, frames x to y**, and any cue can STOP at a frame. One
  helper (`os88vbt.cue_key`) cuts and levels a cue for both the build and
  Listen, so they stay equal - the test holds them so with a stopped cue.
- **The endings** moved from the Enemy tab to the Battle tab: how a battle
  ends is the battle's, whoever the enemy is. The bundle is unchanged.

Found by driving it under Xvfb, not by the owner: the clip list, made a
Treeview so its notes had room, re-selected its clip on every rebuild, and
the selection event rebuilt it again - a loop that froze the window the
first time a clip was chosen. `_pick` now ignores a re-selection of the
clip it already holds.

## Wave 5: the openings, the join, and the Build tab (2026-11-04)

**The join is matched by loudness, not by pitch.** The music comes in when
the scene opening's sound ends, and the question the plan asked is where in
the music it should come in so that it goes on with what the opening was
doing. The answer is a correlation of loudness envelopes (RMS a 256-sample
hop, in decibels): every place in the music's loop is scored by how much
the two seconds BEFORE it look like the openings' last two seconds. The
envelope rather than the spectrum, because an opening's sound is usually
not music at all - a riser, a fanfare, a crash - and what carries over is
its shape. Planted (the openings ending on exactly the two seconds before
a sample of a random-envelope music) it is found within one hop, scored
0.97 against 0.66 for the next; on the demo, whose riser is like nothing
in its music, every candidate scores about 9%, and the window says so in
those words rather than ranking noise as a recommendation. Listen mixes
the join at the engine's own levels - an opening's sound is OWN, the full
range; the music half of it - because a join heard louder than it plays is
judged wrongly.

**The memory figure had to be measured, and the first number was a
guess.** The Build tab says what the engine will claim on a 640 KB machine
and how big its two clip rings come out, and the arithmetic is 125.4's
carving part for part. What it cannot know is how much is free when the
engine opens. The 172 KB rings measured on 86Box earlier were against a
resident claim the bundle no longer has (wave 4 added the band's two 16 KB
buffers), so they could not be worked back. One run on MartyPC's XT read
`vb_rkb` straight off the engine - 144 KB beside a 116 KB claim - which is
461 KB free; 86Box's 5150 had left about 20 KB more. The tab uses the lower
and says whose it is, and the test holds the arithmetic to the measurement:
the music ring's 4 KB guard dropped on purpose and it fails.

**One list of disks.** The encoder's window already made every disk a
video could go on - four floppies and three hard disks, booting when the
boot files are at hand - and the builder wanted the same seven. Its making
moved into `tools/os88mkdisk.py`, which both windows use; the encoder's own
command lines were compared before and after for all seven disks, with and
without boot files, and are the same. The builder's zip now carries the
disk makers and `boot/`, so a battle's hard disk made on a machine with no
os8088 tree carries the kernel, the drivers, the engine and the bundle. A
floppy too small says so with the sizes before anything is written: the
demo's 848 KB against a 360 KB disk's 354.

**On the owner's footage** the budget reads: a 152 KB resident claim, so
124 KB rings, and every attack streams (none fits a ring whole) at 47 to
76% of the ST-225 with the music beside it - inside the disk, which is what
the 86Box runs with no stalls said.

## Loops at either end, and a fade the engine draws (2026-11-04)

The owner asked for a looped opening and ending, "the intent here is fade
transitions, but the user could provide clips that join in other ways too",
and for the end to lead back round so the battle can be played in a loop.
The pieces were mostly there: a clip that seams onto a loop (every action
does it to the idle loop), clips entered at a loop's anchors, the openings'
chain from black. What was new was a loop that is not the idle loop, and a
fade.

**A loop goes round in one ring.** The idle loop is resident; a title made
from footage is not small enough for that. Streaming a loop would have
meant the reader wrapping on the disk at a record boundary, which is not a
cluster boundary. So a loop is read into one of the two clip rings whole,
and at its lap's end the picture's and the sound's cursors simply go back
to record 1. The price is a ceiling - a loop must fit a ring, 144 KB on a
640 KB XT - and the engine refuses to play rather than lap over its own
head, while the Build tab says TOO BIG first.

**The fade freezes the frame and dissolves it.** "Draw less of the frame
until we're drawing none of it" could have meant fading a moving picture,
but masking every decoded frame on an XT is a second full-screen pass a
frame it does not have. So the hook draws nothing while the UI task ANDs
the screen with a 4 x 4 ordered threshold a step at a time, each step paced
by the hook's own frame clock so the fade takes the same time on any
machine, and the sound comes down a shift every quarter. The openings and a
title both start from black, which a fade leaves, so the joins stay exact.
The window previews it with the same table the engine uses; the test
reads the table out of the engine's source to hold them equal.

**An end loop lives in the canvas, not the whole screen.** A JOIN has to
be exact, and an ending's last frame has the band under it - text the host
cannot know. Keeping the end loop to the canvas keeps the join exact and
leaves the band up to say "Enter: again".

**Found on the way: `vb_start_clip` clobbered AL**, and both openings'
sound bookings read it afterwards as the clip. It worked because opening 0
was clip 0 and its exit 0 - the low byte of what AX held. It keeps AX now.

Measured on MartyPC's XT (`tests/vbtloops.py`): the title laps and what is
on the glass is one of its own frames 58 to 59 times in 61 to 63 looks
(the rest catch a frame half drawn); the fade's 12 steps come in order and
take the lit bytes from 636 to under a third; victory fades or joins its
end loop and that laps; Enter plays again from the title. With `vb_fmask`
drawing nothing the row fails (693 lit bytes before and after). The engine
grew 1,620 bytes of image (14,645 to 16,265), 1,124 on the disk.

## Every loop streams (2026-11-04)

VGA at sixteen colours is three times CGA4's data (VBATTLE-MEMORY-PLAN 2),
so a ring holds half a second of it and a loop "held whole in a ring" stops
being a loop of any use. So a loop too big for its ring is now READ ROUND:
the builder pads record 0 out to a 4 KB boundary and the lap to whole 4 KB
- the padding inside records, after their ten lists, where the decoder
never looks - and the reader's next read goes back to the lap at its end
as the music's does, with the file cursor kept from the lap's first read
so going round need not seek. The picture and the sound just go on through
the ring. A loop that fits is still held, which leaves the disk to the
heads. The idle loop left the 60 KB block for a region named by paragraph,
the entries' arrangement, because a VGA idle loop is ~120 KB.

The test that proved it was a 204 KB Limit used as both title and end loop
on a 140 KB ring. The first run "failed" twice, and neither was the engine:
the fade check counted lit BYTES, which a busy picture keeps lit with one
pixel in four (now pixels), and the lap check ran out of looks on a
110-frame loop before its second lap (now 1,200 looks).

## Hercules and VGA (2026-11-04)

One project now builds a bundle per adapter, and the trick that made it
small was refusing to let any rendition have its own frames. Loop points,
anchors and cues are frame NUMBERS, so a VGA frame 40 must be the CGA frame
40: the VGA and Hercules pictures are read through the encoder's own
`ffmpeg_video` at the CGA encode's fps and quantized by the builder - an
ordered dither, and a pixel's value HELD while its source barely moves, so
still footage costs nothing frame to frame. A VGA battle has ONE palette,
the band's four CGA colours in slots 0..3 and a k-means of the footage in
the rest; the first k-means left two blacks and then two near-identical
greens, which a last pass that re-seeds a near copy at the worst-served
pixel fixed.

**The band is the same picture on all three.** It is still composed two
bits a pixel; on VGA and Hercules it always goes into the composition
buffer, and a present turns rows of it into that adapter's bytes - VGA's
planes 0 and 1 from a table, Hercules' every pixel two by two. That made
the band TESTABLE exactly: the row reads the engine's own buffer out of
guest memory, converts it on the host, and holds the glass to it.

That test then failed in a way that taught two things. On Hercules the
first failures were real and not the present at all: the demo's made-up
scenes are whole CGA screens, and scaling all 200 rows to 348 put the
picture's bottom in Hercules' band rows, which the openings then wrote over
the band. A whole screen is now scaled in two parts. The later failures
were the test's: on MartyPC's 4.77 MHz XT the band is rebuilt for ~1.2 s of
the foreground's time on EVERY adapter - CGA too - and presented in ~0.6 s
more on VGA and Hercules, so "the menu's state is set" is seconds before
"the menu is on the glass". The row now waits for the glass to agree.

Measured: the owner's H3 battle is 3.0 MB in CGA4, 4.0 MB in VGA4 at eight
colours (far better looking - the brick, the copper and the light all
read), 4.1 MB on Hercules. Tested on MartyPC's VGA XT and Hercules 5150;
not on a real VGA or Hercules card, and no second page used yet.

## The memory faster machines have, and the board an 8088 can have (2026-11-04)

Every read the engine makes - a ring's piece, a clip's head, the music, a
loop's lap, the play-again reload - goes through one routine, so the hold
sits there and nowhere else: a read lying wholly inside what the hold has
is a copy, and the stream's file cursor is left unseeded so a later disk
read seeks from the name, Video Player's lesson. The hold fills on the
reader's last turn, and the title loop's wait for a key turned out to be
the natural place: most of a battle arrives before the first key.

One change was needed first: a playing clip whose ring had no room still
ended the reader's pass, so a loop read round - always playing, never all
read - would have kept the fill from ever running. It passes the turn on
now.

When the pool is smaller than the bundle, the builder decides what goes
first and writes it in the header: the streamed music, the loops, every
action's first 40 KB (what a start waits for), the openings, then the rest
smallest first.

**The tests are disk swaps and disk counts.** On QEMU (the only XMS here)
the hold fills, B: is changed to a blank floppy, and the battle plays to
victory and starts again - nothing left to read but the hold. On MartyPC's
5150 with the Lo-tech 2 MB board, an 8088 with EMS and the owner's class of
machine, the engine counts its own disk reads: none, from the fill on,
through a whole battle - and the title played again out of the board is
the host's decode on the glass. The first try at breaking it on purpose was
symmetric (the fill and the read both one page off agree with each other)
and proved only that the check had to be broken on ONE side; the read alone
two bytes off stalls the battle.

## Reading the hold in place (2026-11-04)

The owner's pointer was to Video Player, which decodes straight out of XMS
(through LOADALL on a 286) and out of an EMS board's pages. Reading its
SPEC sections and code before writing a line said what to take: not its
code, whose ring is a FIFO of slots, but its three moves - keep the ring's
bookkeeping and change only where a record's address comes from; on EMS
let the hook map pages with the driver's own recipe; on XMS make every
`mov ds, dx` a borrow of DS's hidden base and let the decoder yield at the
sites `vdec.inc` already has. The battle's own twist is coarser and
cheaper: a ring given its clip after the hold is whole is simply FULL at
once, so every cursor it already had is an offset into a clip that is all
there.

EMS worked on the first run on MartyPC's 5150 with the Lo-tech board: six
rings in place, two thousand records read off the board, not one copy, the
title played again exact on the glass. XMS on QEMU's 386 played to victory
from a blank disk at once, too, but yielded NOT ONCE - the demo's records
are mostly under the 2 KB step, and the reader had read the openings and
the attack's head into rings before the fill ended, so few frames were in
place at all. So the step became a word a test can poke (256), and the
title again was checked in B8000 byte for byte: 135 yields and 16 looks of
16 exact. Broken on purpose, a yield re-borrowing one byte on stops the
guest outright, and a page on in the hook's map never reaches victory. The
286's LOADALL arm is Video Player's borrow called as Video Player calls it,
and that first write-up said no emulator here could run it. The owner
corrected it within the hour - *"I think 86Box models loadall?"* - and it
does: Video Player's own docs had said so, beside the line that no row had
been written against it. So `vbt286` is the first: the mr286 profile, the
battle off B:, hma286's os88ui adapter with two verbs it lacked (a key, and
a clock that steps the held machine when it is looked at). The check
passed on that CPU at the first run, 503 records read in place across 136
yields, each a `la_restore` and a fresh LOADALL, the title exact in B8000
and nothing in the fault log.

Then the board's speed, the owner's next ask ("Video player has the same I
think" - it does, 98.3.18.8): the frame timed against the package's own RAM
at the hold's open, Video Player's table taken whole. MartyPC's Lo-tech
reads at 1.0x; the two slower tiers are a poke away, and each plays as its
row of the table says.

## The owner's Hercules battle (2026-11-05)

The owner built a battle from their own footage, at 100% and 75% size, and
played it on 86Box's `pc5150` profile, an IBM 5150 with a Hercules card and
an ST-225 behind an ST11M. It was the first real load the engine had
carried, and most of this day's work came from it. The footage stayed on
the owner's side: the bundles were used here to debug and measure, and
none of them is in the tree.

What it found, in the order it was found:

- **The music went silent while the menus drew.** Rebuilding the band took
  the reader away for about 2 s on Hercules, and the music's 3 s ring was
  only refilled when it fell under half. Now the ring is topped up before a
  rebuild and fed between the band's lines. `vbtsound` ran six turns
  without the ring running dry; with the top-up taken out, it ran dry 23
  times.
- **The menus were slow** ("any way to draw the menus faster?"). Every menu
  rebuilt the whole band. A rebuild whose background has not changed now
  erases only the old menu's lines and redraws the text. A menu is up in
  0.1 to 0.3 guest seconds, against about 0.9 s on CGA and 1.5 s on
  Hercules. Two older defects that the full rebuilds had been hiding came
  out with it.
- **The fades took 8 to 10 seconds.** A fade advanced one step per pass of
  the UI loop. Now it jumps to the step the hook's clock says, and takes
  3 s.
- **Joins to once-played clips froze, and openings started before their
  heads were read.** Both are fixed. Each fix has a row (`vbtendingjf`,
  `vbtendingop`) that goes red when the fix is taken out.
- **The machine was spending as much time in the hook as in decoding.** At
  582 Hz, the hook and the kernel's timer interrupt under it took 23% of
  the machine, the same as decoding. At 72.8 Hz they take 8%. On the 100%
  bundle the music ran dry 56 times before and 0 after. With 512-byte
  blocks, the card was only 2 KB ahead, and one hook call's two heavy
  frames could run past that and halt it. At 4 KB ahead it halted 0 times
  in eight turns, against 8 before.
- **Clips with bursts lost their own sound.** The clips' average rates hid
  bursts: Bolt averages 74 KB/s, but its first 1.5 s run at 129 KB/s. Two
  changes fixed it. The reader now follows the sound's cursor, the one that
  runs ahead. And the builder holds a streamed clip to what the disk can
  deliver, drawing a frame in parts when the model says it won't arrive in
  time. On the same machine, Bolt, Scope and Search went from 51, 35 and 38
  frames of late sound to 0, 0 and 0. The demo, which never overdraws,
  builds byte for byte as before.
- **A sound stream the driver ended froze the play for good.** Now it is
  closed and opened again on the same timeline. `vbtsndend` fires the
  driver's own watchdog under a battle, and the music comes back.

The owner also asked for two builder features in this round: a 90% picture
size, and a project's files saved relative to the project.

## A machine that has been used (2026-11-05)

The owner's battle at its first menu, mapped on MartyPC's Hercules 5150,
showed two clip rings of 88 KB each. The desktop's 44.5 KB of caches sat
stranded under the pinned resident block. The first plan was written for a
fresh boot, and the owner sent it back: *"we can't treat 'straight out of a
fresh boot' as a normal situation."* VBATTLE-RAM-PLAN was rewritten for a
machine where other programs have been opened and closed, and it took four
waves, R1 to R4:

- **R1:** the resident block is claimed from the top of memory, the band's
  buffers are the band's real size, and Play posts a compaction through
  Video Player's door when moving the battle's own memory would buy 16 KB.
  The rings are two claims made after the largest free run has been
  claimed and given back once, which is what makes the kernel shed its
  caches. Rings: 88 + 88 KB became 128 + 128.
- **R2:** each action's entries ride at the front of its own clip (format
  8), so the resident block no longer holds every action's entries for the
  whole play. Rings: 168 + 172 KB at 100%, 172 + 172 at 75%.
- **R3:** the menu reads nothing a key would wait on. A clip is fetched only
  when its action can be taken and the cursor has rested about 0.5 s. A key
  is taken in 0.12 s at most.
- **R4:** the builder holds each clip to the ring this bundle will actually
  get, not to a constant 56 KB.

Format 8 then had to grow twice. The title's way out carried 55 KB of
entries, and then 75 KB on Hercules, so a clip's entries may now be up to
96 KB, and a ring must hold the largest of them.

At the end, before the merge, the owner asked for nothing to be pinned
without a reason. `t_pins` found six of the battle's claims pinned with no
registered reason. Three of them are now movable through one relocation
proc: the resident block between plays, the title picture, and the XMS
hold's bounce buffer. The other three are now registered with their
reasons: the clip rings are held only inside the play's bracket, the
packing probe is freed inside its own call, and the card's DMA ring is
freed when each play ends. `vbtram` checks this on MartyPC: it posts a
compaction, the block moves up into the hole a second battle window left,
its six derived segments move with it, and the next play reaches its menu.
With the relocation table walk broken, the row goes red.

## Sound with its picture (2026-11-05 to 06)

Three timing bugs, each reported by the owner and each with a row that
measures it on MartyPC:

- **An action's sound about 2 s late, half the time on Bolt.** When an
  anchor is nearer than the lead, the sound is booked for the loop's next
  lap, but the picture went in on the first pass. Now the frames are
  counted down. `vbtanchor` checks 14 turns, 5 of which take the lap: every
  one is within two frames, against 22 to 23 frames out when broken.
- **Openings and end clips with their own sound 0.3 to 0.5 s late.** Their
  sound was booked at the frame's own time, which the ring, written a lead
  ahead, had already passed. Now it is booked at the write position, and
  the picture waits for the clock. `vbtownsnd` compares the record on the
  glass with the record heard: within a frame and a half, against 3 to 4.5
  frames out when broken.
- **A card count that steps back.** SOUND.DRV steps its consumed count back
  to the total after the card plays past it. Read unsigned, that step back
  looked like 62,000 samples forward. Holding the count at its highest made
  the play stand still. The hook now takes the count's changes signed.

## Finishing (2026-11-05 to 06)

The owner's list of demo polish and ship features:

- Fit or stretch for footage that isn't quite the screen's shape.
- A colour clip, so slow fades end in solid black instead of leaving dim
  dots lit.
- An item's count shown beside its action.
- No silent frame before the music.
- Full-screen seams that cover the band.
- Missing-clip warnings only where the battle can actually go.
- An overlap check based on the party's real values.

Then the ship features:

- Characters can be reordered.
- "Any key" continues from the title.
- A .VBT is a document that plays when double-clicked (`vbtdoc`).
- A battle can be embedded in its own copy of the engine: one file per
  adapter, named after the battle (`vbtgame`).
- A title picture shows in the battle's window on the desktop, with the
  battle's name, up to 31 letters, in its title bar (`vbtcover`, bit for
  bit on CGA and Hercules). It is read back while the screen is still the
  play's, so the window never shows its text layout first.
- A new icon: a sword on a film frame. A document's own glyph is the sword
  alone.

The owner confirmed the timing, the title picture and the embedding on
86Box's 5150 before the merge.

Left open: one pause on a long title idle, which the owner saw once and
neither side could reproduce (the signed count is a plausible cause);
`vbtmenu`'s Bolt check, which failed about one run in four; and
`vbtownsnd`'s first opening, an overloaded stress fixture. Its sound starts
with the picture and then drifts about 4 frames later during the clip, and
the row judges the median of the first three samples, which sits on its
2-frame line: it failed 2 runs in 7 on the merge's engine and 0 in 3 on the
engine before it, with the same samples.

## Measured (MartyPC `os8088_5150_sb_gla`, 360 KB disk)

| | |
|---|---|
| the load before the intro | 14 guest seconds |
| a battle to victory, every command | 49.8 guest seconds, 0 late frames |
| the card's capture against the frames | 49.9 s against 49.8 s; no silent 20 ms window |
| the demo disk | 324 of 354 clusters at 360 KB |

Second wave, MartyPC `os8088_xt_hdd_sb` with C::

| | |
|---|---|
| the load before the intro | 1.7 guest seconds |
| a battle to victory | 50 guest seconds, 0 late frames, 0 music underruns |
| the music against the bundle | median correlation 0.95 at offset 0, across the loop |
| a menu sound after its key | 69 to 96 ms free, about 240 ms behind a read |

Tested: the boot, the battle, the sound and the read-ahead, all on MartyPC,
from a floppy and from a hard disk; Hercules and VGA renditions on MartyPC's
Hercules 5150 and VGA XT; the hold on QEMU (XMS), MartyPC's Lo-tech EMS and
86Box's 286 (LOADALL); the owner's footage on 86Box's `pc5150` (ST11M).
Not tested: real hardware, a real VGA or Hercules card, kern_mega (the next
round).
