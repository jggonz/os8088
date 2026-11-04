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

## Measured (MartyPC `os8088_5150_sb_gla`, 360 KB disk)

| | |
|---|---|
| the load before the intro | 14 guest seconds |
| a battle to victory, every command | 49.8 guest seconds, 0 late frames |
| the card's capture against the frames | 49.9 s against 49.8 s; no silent 20 ms window |
| the demo disk | 324 of 354 clusters at 360 KB |

Tested: the boot, the battle, the sound and the read-ahead, all on MartyPC.
Not tested: real hardware, 86Box, a VGA, kern_mega.
