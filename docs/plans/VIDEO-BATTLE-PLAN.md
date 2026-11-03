# VIDEO-BATTLE — a battle played as joined video clips, on a 5150

**Status: FEASIBILITY STUDY (2026-11-03).** Nothing here is built. This
document answers the owner's question: can a single turn-based battle run as
video on an IBM 5150? The design is a full-screen intro, then an idle loop
in the upper part of the screen, a small interface drawn in the band below
it, and the player's choices joining one clip to the next. It also lists
what each part costs.

**The sound target is PCM at 11,025 Hz on a Sound Blaster** (the owner,
2026-11-03: *"PC Speaker is optional here - assume PCM 11khz"*). The
speaker is kept as an optional tier in 3.2.

Figures are labelled **measured** (with the machine), **model** (the
encoder's own cost model, SPEC.md 98.2.1) or **estimate** (arithmetic, not
yet run). Per PERFORMANCE.md Part 6 rule 8, an estimate needs a MartyPC or
5150 run before anyone quotes it.

The battle content is the owner's own capture and stays **outside the
tree**. What would ship is a generic engine plus a clip-graph format. Any
footage that is checked in is original test footage, the way
`apps/video/os8088.v88` is.

## 1. The answer, short

**It is feasible on a 4.77 MHz 5150 with a Sound Blaster and a hard
disk.** Four things are true about it:

1. **The extras are cheap.** The card plays by DMA, so the CPU only fills
   its ring. Mixing music with one effect, drawing the interface, polling
   input and switching clips come to **about 7-8% of the machine** together
   (section 3). The rest is what the current files already spend: their
   model CPU mean is 5.5-18.5%. These clips were encoded for exactly this
   budget, a card at 11,025 Hz on the `5150-st225` profile.
2. **The real problem is joins, not cycles.** A V88 frame is a list of
   absolute stores against whatever is on the screen, and the encoder is
   lossy. So "idle frame 12" means a particular *decoded* screen, not the
   source frame. A clip that starts there has to be encoded *against that
   decoded screen*, and its last frame has to land *exactly on* the decoded
   screen of the idle frame it hands back to. The format already has the
   object for this: the **seam** (SPEC.md 98.1.1.2), one record that turns
   one decoded screen into another frame's screen. What is new is a seam
   **between files**. That is work in `tools/os88venc.py`. The player's
   decoder needs no changes for it.
3. **Sound has to come apart from the video.** Today the sound is
   interleaved in each frame record and the card is the clock (98.3.1).
   Here the video is silent and the **music ring is the clock**. Music
   never stops at a clip switch, and one effect is mixed into the ring as
   it is filled.
4. **It runs from the hard disk.** The clips of a whole battle come to
   megabytes (section 4), and the 5150 has one 360 KB floppy drive. A
   floppy build can be a demo, not the battle.

## 2. What the four files say

`tools/os88vid.py info` on the three V88 files. All three are CGA4, 15 fps,
PCM8 at 11,025 Hz interleaved, with the `5150-st225` profile's model:

| file | frames | canvas | stream | of it, sound | video alone | model CPU mean / worst |
|---|---|---|---|---|---|---|
| 1-Open | 236 (15.7 s) | 320 x 200 | 29.3 KB/s | 10.8 KB/s | **~18.5 KB/s** | 10.6% / 68.2% (frame 0) |
| 2-Open | 74 (4.9 s) | 320 x 142 | 46.3 KB/s | 10.8 KB/s | **~35.5 KB/s** | 18.5% / 40.5% |
| 3-Idle | 72 (4.8 s) | 320 x 142 | 20.6 KB/s | 10.8 KB/s | **~9.8 KB/s** | 5.5% / 55.9% (frame 0) |

The sound column is 11,025 bytes/s of PCM8, and "video alone" is the
difference. Front matter is not counted.

Three facts follow from the table:

- **320 x 142 is already the right shape.** It leaves rows 142-199 (58
  rows, seven 8-pixel text lines) for the interface. That is about the
  share of the screen the screenshot's menu band takes.
- **The idle loop is cheap.** Its mean is 5.5% of a frame period. Its
  worst frame is the keyframe, and a resident loop pays the keyframe once,
  not once per lap.
- **The palettes differ.** 1-Open is palette 40h (colours 0 3 4 7) and the
  other two are 50h (0 11 12 15). CGA mode 4 has ONE palette for the whole
  screen, so the video and the interface band share the same four colours.
  Every battle clip must be encoded to one palette, and the interface is
  drawn in those four colours too. Changing the palette at the intro's
  last cut is legal, because it is a full-screen change anyway.

**The idle animation's period, measured off the MP4** at 15 fps (mean
absolute difference between frames, host-side): frame 0 is closest to
frames 24 and 49, frame 10 to 34 and 59, and frame 20 to 44 and 69. The
movement repeats every **~24.5 frames, 1.63 s**. The 4.8 s capture is three
laps of one cycle. **The resident idle loop should be one period, about 25
frames**, not 72. That is roughly a third of the bytes, and it bounds the
wait in section 5.

## 3. The CPU budget, frame by frame

One 15 fps period on a 4.77 MHz 8088 is **318,182 cycles**. At 11,025 Hz
a frame's sound is 735 samples.

| item | share of the machine | cycles a frame | basis |
|---|---|---|---|
| the card's DMA and its block IRQ | ~0.5% | ~1,500 | **estimate**: one IRQ per block (SPEC.md 34.5), DMA moves the bytes |
| music copied into the ring, no effect playing | ~3.5% | ~11,000 | **measured** per-byte copy 15 cycles (98.1.1.3) x 735 |
| + one effect mixed in, while it plays | ~3.8% more | ~18,400 | **estimate**: `lodsb` / `add` / `xlatb` through a clip table, ~25 cycles a sample |
| video decode, idle loop | 5.5% mean | 17,500 | **model** (`info` above) |
| video decode, a busy action (2-Open) | 18.5% mean, 40.5% worst | 59,000 / 129,000 | **model** |
| video keyframe / big cut | 56-68% | 178,000-217,000 | **model**, once per clip at most |
| interface: one cursor move, ~6 glyph cells, direct to VRAM | <0.1% amortised | ~3,000 per event | **estimate**: 16 bytes stored per CGA cell, no desktop calls |
| interface: ATB/limit bars, a few pixels per tick | ~0.2% | ~600 | **estimate** |
| input: one keyboard poll per frame | ~0 | ~100 | **estimate** |
| clip switch: changing the decoder's source pointer | ~0 | ~200 | **estimate**; the disk read behind it is DMA (below) |

**Worst case, with an effect playing during a busy action:** about 8% for
sound, under 1% for the interface and input, and 18.5% mean / 40.5% worst
for the picture. That is **~27% mean and ~50% at the worst frame**, against
the profile's own budget of 50% average and 85% peak
(`os88venc.py PROFILES['5150-st225']`). The keyframes (56-68%) were already
inside the peak when these files were encoded, and the extra ~4% for the
effect still fits under 85%.

**The interface and the input are not the problem.** A cursor move costs
less than one percent of one frame. Inside an `OSAPI_FSX_RUN` bracket the
band is drawn straight into VRAM, the way WireBall draws its HUD (the
section at the top of CLAUDE.md): cache the state, then draw only the cells
that changed. The band is outside the canvas, so the decoder never writes
there, and nothing has to be saved or restored around it.

**The disk is not the problem either.** The `5150-st225` profile models
the ST-225 at **119,000 B/s**, moved by the ST11M's DMA. A busy action
needs ~35.5 KB/s of video plus 10.8 KB/s of music. That is 46.3 KB/s, the
same as 2-Open needs today with its sound interleaved. An XT-IDE or
PicoMEM would change that, because the CPU copies every byte there
(98.2.15.5).

### 3.1 Why the music is pre-mixed PCM, not synthesised

A four-channel MOD mixer is the obvious alternative, and it is too dear on
this CPU. Tracker's mixer alone costs about **44% x rate / 5,920** at its
bench (SPEC.md 45.25, measured on MartyPC). At 11,025 Hz that is more than
the whole machine. A stored PCM stream costs 15 cycles a byte to copy. So
the music is rendered on the host, at 11,025 Hz, 8-bit mono, and streamed.

The mix rule is simple. Leave the music ~6 dB of headroom on the host,
store each effect at the level it should sound, and add the two through a
256-entry clip table. "One effect at a time" means the second effect
replaces the first.

### 3.2 The speaker, optional

On a 5150 with no card, the same engine plays the same mixed stream
through `apps/os88spk.inc`, but the cost changes completely. The ISR is a
**measured** 449 cycles a pulse: 52% of the machine at 5,512 Hz and 75% at
8,000 Hz (SPEC.md 98.2.15). The clips then have to be re-encoded
`--audio speaker`, against what the pulses leave, and the pictures lose
detail. 11,025 Hz is past `VP_SPKMAX` on an 8088. It is an optional tier,
not the design point. The cheapest fallback of all is a PIT-tone tune
(`OSAPI_SND_TONE`, ~0%) with no PCM effects.

## 4. Data, and where it lives

**Estimate** for a basic battle: about 15-25 clips. Each party member's
Attack, a spell or two, Item, a Limit, each enemy attack and its counter,
being hit, victory and defeat. At 3-6 s each and ~20-35 KB/s of video,
that is **~1.5-3 MB of video**. The music is 10.8 KB/s, so a 90 s loop is
**~1 MB**, and each effect is 11 KB per second of effect.

- **The hard disk holds it with room to spare** (20 MB ST-225).
- **Memory (640 KB, ~500 KB of heap after the kernel):**
  - the idle loop, resident: one period, ~20 KB per rendition (98.1.7);
  - the effects, resident: **estimate** 50-100 KB for a battle's worth
    (they start at an exact frame, so they should not depend on a seek);
  - the entry records (section 5): a few KB per action;
  - a video prefetch ring of 64-128 KB;
  - a music ring of 32 KB (3 s);
  - the engine itself: **estimate** 15-25 KB.

  That leaves the music streaming from disk, not resident.
- **Floppy:** one 360 KB disk holds ~10 s of action with its music. That
  is enough for a demo (intro, idle, Attack), not for the whole battle.

**Each adapter is its own rendition.** A V88 is laid out for its surface
(SPEC.md 98.1.2). So CGA4, Hercules and VGA (13h or Mode X) are three
encodes of the same clip graph. Hercules and VGA have a second page and can
flip. **CGA has one page**, so a keyframe or a large cut is drawn where you
can see it, from the top down, and tears for one frame. That is acceptable
at a camera cut and worth hiding everywhere else (section 5).

## 5. Joining clips: the real design problem

### 5.1 Exact joins

A delta record is only correct on top of the screen it was encoded
against. So each action clip needs both ends pinned down:

- **Entry**: the clip's first record is encoded against the **decoded**
  screen of idle frame *k*, not against black. Today a file's first
  record is a keyframe applied to black (98.1.3).
- **Exit**: the clip's last record is a **seam onto idle frame *j***. It
  makes the screen equal to the decoded idle screen at *j*, and the idle
  loop goes on at *j* + 1. This is the same contract as LOOPREC
  (98.1.1.2), and `verify_v88` already checks it within one file.

**The encoder change**: `os88venc --enter IDLE.V88:k --exit IDLE.V88:j`.
To do it, the encoder decodes IDLE.V88 to frame *k* (`decode_lists` is its
reference decoder), starts its picture model from that screen, and writes
the exit record against IDLE's decoded frame *j*. **The player needs no
new decoder for any of this**: an entry is an ordinary delta record, and
an exit is a seam. Without the change, every clip has to start with a
keyframe and end on one. That works, with a visible pop wherever the poses
differ and a one-frame tear on CGA.

### 5.2 The wait for the entry frame

If an action can only start at idle frame *k*, a command waits up to one
idle period: **≤1.63 s, mean 0.82 s** with the measured period. Three ways
to shorten it, in order of preference:

1. **Several entry records per action.** Store a small delta from each of
   *n* anchor frames (say every 5th idle frame) to the action's first
   frame. With *n* = 5 the wait is ≤0.33 s. Each entry record is about the
   size of one ordinary frame, because the idle frames all look alike (the
   mean difference from one frame to the next is 1-2 grey levels out of
   255, measured above). So this costs **a few KB per action** and no new
   footage. Capturing the action once from any idle frame is enough: the
   encoder writes the *n* entries.
2. **Accept the wait.** A command already has a natural beat before the
   action begins, and the wait also hides the disk read (5.3).
3. A shorter idle loop. That depends on the footage, not on the engine.

Your plan of starting each action at a variable idle frame is fine. The
anchor of each entry record is just whichever frame you captured from.

### 5.3 Reading ahead of the commit

The idle loop is resident, so the only disk traffic during it is the
music (10.8 KB/s). **Prefetch on selection, not on commit.** When the
cursor lands on a command (or on a target), the reader seeks to that
action and reads its first ~1.5 s (~50 KB) into the ring. That takes an
average seek of ~65 ms plus ~0.45 s of transfer at 119 KB/s, interleaved
with the music's reads, and the menu and target steps take at least that
long. The entry wait (5.2) covers the rest. If the cursor moves, the reader
drops the read and starts on the new command. In the worst case (Enter
pressed before the read finishes) the engine holds idle until the ring has
the clip's first second.

**Seeks cost the music nothing** as long as its ring holds more than the
worst seek plus a read. A 32 KB ring is 3 s.

### 5.4 The clock

The Video Player interleaves the sound in the frame records and clocks the
video off the card (98.3.1). Here **the video files are silent**
(`audio none`) and the clock is the **music ring's bytes played**
(`OSAPI_SND_STREAM` verb 9, exact to the byte, 34.5.1): frame *n* is due at
*n* × 735 samples. Video and music are two cursors on the disk, so a clip
switch never touches the card's stream. That is the main structural
difference from the player, where a seek restarts the sound (98.3.14).

**Effects are on the same clock.** The clip graph says "effect E starts at
frame *f* of clip C". The mixer starts adding E's bytes at sample *f* ×
735 of that clip's start, so a hit sounds on the frame the hit is drawn.
The ring runs ahead of the card, by up to the ring's length. So an effect
is mixed in when the producer *writes* that sample position, not when it
is played, and the producer must not run so far ahead that the decision
(which clip follows) is not yet made. The cure is to keep the ring's lead
over the *video's* committed future short (~0.5 s). Music-only stretches
can run further ahead.

## 6. The selected-player marker

You proposed two idle animations and switching between them. That works,
but it doubles the idle captures, the idle memory, and the join problem
(an A→B switch needs an exact cross-seam at every frame you allow it on).
Two cheaper options are worth trying first:

- **In the band, for free.** Highlight the active character's name row. In
  the screenshot the active character is shown in the menu, not in the
  scene.
- **In the scene, drawn over the video.** The player already decodes
  around a box drawn on the glass (`vd_clip`, SPEC.md 98.3.13.1). A store
  that would land in the box goes into a save instead, and the box is
  drawn once and stays up. Its cost is proportional to how many changes
  reach the box. **Measured** on a busy clip (Bad Apple, Hercules): median
  31,000 cycles a frame extra, about 10% of a 15 fps frame. The idle loop
  changes very little, so a small marker over a static part of the scene
  should cost a fraction of that. **Estimate**: under 3% of the machine.
  The same mechanism covers a target-selection hand and damage numbers
  that vary.

The two-idle design is still the right one if the marker has to *be part
of the footage*, for example a lighting change on the selected character.

## 7. What gets built, and what is reused

The Video Player is 25,599 lines and does a great deal this does not need
(windowed play, seeking, Live, XMS banks, layers, posters). **The engine is
a new small package** that includes the shared parts as SOURCE:

| reused | for |
|---|---|
| `apps/video/vdec.inc` (`vd_native`, `vd_clip`) | the frame decoder, and the decode-around for overlays |
| `OSAPI_SND_STREAM` through `SOUND.DRV` (34.5) | the card's ring, and verb 9 as the clock |
| `apps/os88spk.inc`, `apps/os88spkfx.inc` | the optional speaker tier (3.2) |
| `OSAPI_FSX_*` (53) | the bracket, the mode, page flips on Hercules and VGA |
| the V88 format (98.1), unchanged | clip files, with `audio none` |
| `tools/os88venc.py` | encodes, plus `--enter` / `--exit` (5.1) and an "anchors" option (5.2) |

| new | |
|---|---|
| a **clip-graph file** | the states, each state's clips, its entry anchors and exit frame, the menu tree, the numbers (HP, MP, damage), and which effect plays at which frame of which clip |
| the reader | two cursors (video, music) on one file handle, plus prefetch on selection |
| the mixer | music plus one effect, an add through a clip table into the card's ring |
| the band | FF-style boxes, the cursor, numbers and bars, drawn into VRAM and changed cell by cell |
| a host tool | builds the graph's files, checks every join with `verify_v88`'s seam rule, renders the music and effects to 11,025 Hz PCM8, and reports each clip's budget |

## 8. Risks, in order

1. **Exact joins.** This is encoder work (5.1). Until it exists, joins are
   keyframes, and on CGA that means a visible frame at each one.
2. **The effect's timing against a ring that runs ahead** (5.4). The cure
   is to bound the producer's lead, which is new code but not much of it.
3. **Capture discipline.** Every action has to start from the same camera
   and the same idle, and end in it. The engine can hide a small pose
   difference, but not a moved camera.
4. **One CGA palette** for video and interface alike.
5. **Clip-count growth.** Every new action is footage plus disk space. The
   graph format should make it cheap to leave an action out.
6. **MartyPC's limits.** MartyPC has no DMA hard disk (98.2.15.4) and no
   Sound Blaster, so the disk-and-card combination is a question for 86Box
   (`xt-*` profiles, not cycle-accurate) and finally for a 5150 with a
   card. The cycle counts in section 3 can be measured on MartyPC with
   the sound muted.

## 9. Proposed first steps (not authorized yet)

1. Re-encode 2-Open and Idle **video-only**, on one palette, with the
   `5150-st225` profile. Cut Idle to one 25-frame period with a seam and
   encode it resident.
2. Render a music loop and two effects to 11,025 Hz PCM8. Play idle plus
   music through a minimal bracket on MartyPC's 5150 (`_gla`), muted, and
   take the frame times and the mixer's cycles off the guest's counter
   (PERFORMANCE.md Part 3). Play it with the card on 86Box for the
   sound.
3. Add `--enter` / `--exit` to the encoder, verified on the host by
   `verify_v88` across two files.
4. Build the engine: band, menu, one action, prefetch on selection,
   effects on the clip clock.
5. A 5150 with a card: one listen and one look.
