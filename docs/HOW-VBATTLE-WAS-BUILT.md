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
- **The disk's kind decides the rate a clip may have.** With the bugs gone,
  the first Limit (34.6 KB/s plus 11 for the music) still could not be fed
  by MartyPC's XT-IDE, whose CPU copies every byte while it also decodes.
  Its sky flashes, 7 KB a frame, were the weight; without them the clip is
  29 KB/s and plays in real time. A DMA disk is another matter, and the
  builder will budget each clip against the profile's disk.

| MartyPC `os8088_xt_hdd_sb`, C: | |
|---|---|
| the Limit, 234 KB | 7.58 s captured for 7.8 s |
| a battle to victory | 0 late frames, 0 stalls, 0 sound frames late, 0 music underruns |
| the music's join | at 3.903 s, the openings' end, correlation 0.97 |

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
from a floppy and from a hard disk. Not tested: real hardware, 86Box, a
VGA, kern_mega.
