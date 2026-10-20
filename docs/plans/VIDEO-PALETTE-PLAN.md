# VIDEO-PALETTE-PLAN — a palette that changes mid-play, without shifting

**Status: BUILT for sixteen colours, under the owner's eye (2026-10-13).**
The measurements are `docs/reports/VIDEO-PALETTE-2026-10-12.md` and the
reports after it, taken with `tools/os88palsim.py` and the encoder on four
of the owner's clips. BUILT, in order: **a sixteen-colour file on the
desktop's 640 x 480 may carry its own sixteen, played full screen** (SPEC.md
98.1.3.2.2); **`--palette colour16`**, chosen for colour, and **the scored
`--vga4-hold`** (98.2.5, 98.2.5.1; section 10); and **a palette a scene** -
section 2.2's CUT mode, the table of section 4 and the player of section 5
(SPEC.md 98.1.3.2.3, gate `vidpal`). Not built: CLEAN (2.1) and anything at
256 colours, CGA (section 7, refused), and the rate ladder past the hold
(section 10).

**The owner's ask** (2026-10-12): investigate palettes that change in the
middle of a video, for the Video Player's VGA formats; CGA has so few colours
a change would be jarring, though maybe on a full cut. It must not cause
SHIFTING - a large share of the screen flipping colour, back and forth or
at all. So: at scene cuts, or into colours a previous scene used and the
current one does not. **Full screen only**: the desktop's colours are the
desktop's, and a play in a window must leave everything round it alone.

That last rule costs nothing: every format that carries a palette already
plays only full screen - VGA8 always (98.2.3), VGA4's own sixteen since
98.1.3.2.2 on any screen - and CGA4/C160 likewise (98.1.3.3).

It reverses, under conditions, a standing ruling: VIDEO-PLAN 14.7's C1,
*"One palette per file ... No mid-video palette flips - they flash"*.

## 1. The owner's rulings (2026-10-13)

The first revision of this file treated a shift as something that happens
INSIDE a refresh - the old picture shown through the new colours while a
frame draws - and proposed page flipping, a palette switched in the vertical
blank, and a blink to hide it. **That was the wrong unit**, and the owner's
answer is the design now:

1. **A viewer sees in scenes and seconds, not in parts of a frame.** "The
   last scene's background was this yellow, the next scene's is that
   yellow" is fine. "The scene on the screen was this yellow, and one frame
   later it is that yellow" is a FLASH, and looks terrible - however
   atomically it was done. **Colours that change WHILE the scene is changing
   are not noticed.** So the switch belongs IN a whole-scene cut, and
   whether it lands at the start of the cut's draw or its end does not
   matter. How generic the cut can be is the experiment (section 6).
2. **Recycling is the "clean" mode** (section 2.1): a colour nothing on the
   screen uses can change at any moment.
3. **Page flipping is not part of this.** It is too expensive below a 486
   unless the video is black and white or very small (section 3.4 measured
   it: 333 frames of 600 cut on the 286), and it answers the refresh-level
   question, which is not the question. The blink is worse still. The
   letterboxed 640 x 480 screen this file proposed only to give a second
   page is WITHDRAWN with them.
4. **Gas Gas Gas gets its own sixteen in full screen: yes** - BUILT,
   SPEC.md 98.1.3.2.2.
5. **256 colours already look good**: better is still better and worth
   playing with, but **sixteen is where the big wins are**.

**And the owner's question - why should a palette change need any data
beyond the switch itself?** It does not. A frame's pixel data is pixel
values; which colours those values mean is the palette's, and changing the
palette costs the palette: **48 bytes for sixteen colours, up to 768 for
256, once per switch** - and at a whole-scene cut every pixel is redrawn
anyway. The one honest extra is a pixel the cut does NOT redraw whose value
now means a different colour; a colour that persists across the cut keeps
its value (section 4), so that is near zero by construction.

What the first revision measured as "2.6 times the bytes" is not the switch
at all. It is what a palette of its own SHOWS. Measured on the owner's
GasGas window (640 x 360, the encoder's Knoll dither):

| | colours a frame uses (over 1% of it) | information | pixels changed a frame |
|---|---|---|---|
| the EGA's sixteen | 3.8 | 0.97 bits a pixel | 14,513 |
| its own sixteen | 7.2 | 2.40 bits a pixel | 56,164 |

Through the EGA's colours that clip collapses to about four - nearly a
one-bit picture. Its own sixteen show 2.5 times the information, and more
picture is more bytes: the same reason `--palette grey16` costs more than
`grey2` (SPEC.md 98.2.5.1). One palette for the clip or one a shot makes no
difference to that. What it means for the owner's 286 is section 3.4.

## 2. The two modes

### 2.1 CLEAN: recycle colours nobody on the screen uses

**A slot no pixel on the screen names can be changed at any moment, in any
order, with no timing at all** - nothing displays it. So:
- after frame *f* - 1 is drawn, the encoder knows exactly which slots its
  screen uses (it keeps the screen; 98.2.1 does already for the deltas);
- a palette patch carried with frame *f* - 1 writes only slots outside that
  set, after the record's lists are decoded;
- frame *f* may then use them.

**It is invisible by construction, and the construction is checkable**: a
host-side reader replays every record and refuses a patch that writes a slot
the screen shows. It works on every VGA screen, on an 8088 or a 486, with no
wait for anything.

**The cost is room.** The next shot's colours must fit the slots the
current one leaves free, so a shot is capped at *P* colours and the rest is
the next shot's landing zone - and the shot must be FENCED to its own slots
(3.2.1), or its dither spreads over every slot near its colours and leaves
none. At 256 that is cheap (3.2); at sixteen every colour is on the screen
every frame, there is no room, and it loses (3.3).

**Slot 0 stays black, always.** It is what a mode set clears to, the bars
round the canvas and every keyframe's ground (98.1.3), so a seek can clear
to it, load a whole palette while only slot 0 is on the screen, and draw the
key: recycling's rule at the one moment the whole screen is a single slot.

### 2.2 CUT: switch with a whole-scene change

At a cut where the WHOLE scene changes, the palette changes with it: the new
colours are written as the cut frame is drawn, unflipped, with no wait for a
retrace. For the moment the draw takes, part of the screen is the old scene
in the new colours or the new scene in the old - and that moment is the cut
itself, which the owner's ruling 1 says is not seen. **This is where
sixteen colours win** (3.3): a palette a shot, +3.1 to +9.8 dB over the
EGA's sixteen on all four clips, with no flip.

Three things make a cut safe to switch at, and each is the encoder's to
guarantee (section 4):
- **It is the whole scene.** A shot change inside a scene - a reverse angle
  on the same yellow room - is exactly the owner's flash if the yellow
  moves. The detector is not the content rule alone but how much of the
  screen the cut frame redraws.
- **Nothing old survives in a moved colour.** A pixel the cut does not
  redraw keeps its value; if that value's colour moved, it is a flash in
  miniature - a letterbox bar, a logo, a black that changed shade. So a
  colour present on both sides of the cut keeps its slot, and any old pixel
  left on a slot whose colour moved is redrawn by the cut frame.
- **The cut is drawn whole.** The budget may leave part of a frame for a
  later one (98.2.1.2.1); a cut that carries a switch must not, or the parts
  it left show the old scene in the new colours for several frames, which
  is no longer the cut.

A cut that fails any of the three keeps its palette and the shot is drawn
in the old one - recycling can still bring its colours in over the next
frames (2.1).

### 2.3 Where each applies

| | CUT | CLEAN |
|---|---|---|
| VGA4, any screen (sixteen) | **the win**: a palette a shot | no room - not worth it (3.3) |
| VGA8, 13h and Mode X (256) | a palette a shot (3.2's `S`) | **works, fenced** (3.2.1) |
| CGA4 | no (section 7) | no |

## 3. What it buys - measured

`tools/os88palsim.py` runs a clip through the encoder's OWN ditherers
(`Vga8Ditherer` at its defaults, dither 24, stable 18, clip 16; and
`KnollDitherer`, stable 24, clip 16) under each policy, and scores each
frame as the PSNR of a 3 x 3 box blur of the source against the same blur of
what the screen shows - the blur is what an eye does to a dither, and
without it every dither scores as noise. Shots are found the way an encoder
would find them (PySceneDetect's content rule, mean HSV change over 27, shots
under 6 frames merged), and each shot's palette is ffmpeg's `palettegen`
over that shot alone, as `vga8_palette` is over the clip. It models the
picture and not the stream: the budget, the frame cuts and the bytes are
the encoder's (section 3.4). The full tables are the report's.

The policies:

| | what | invisible? |
|---|---|---|
| `EGA` | VGA4's `auto`: the EGA's sixteen (what the owner's GasGas file is) | - |
| `G` | ONE palette for the clip: VGA8's `auto`, VGA4's `clip16` | - |
| `S` | a palette per shot, swapped at the cut: section 2.2's CUT mode | at a whole-scene cut, by the owner's ruling |
| `SP` | *P* colours a shot, swapped at the cut | the same |
| `RP` | *P* a shot, RECYCLED (2.1), the next shot preloaded the frame before its cut | **always** |
| `FP` | `RP`, plus up to `VBL` slots the old page shows written in the vertical blank | a flipped screen - WITHDRAWN with flipping (ruling 3); kept as measured |

### 3.1 The four clips

The owner's (2026-10-12), not committed: GasGas over **40-114 s, the window
of the owner's own file** (`03-640FS.V88`, whose options block says VGA4,
640 x 360 on mode 12h, 30 fps, `286-speeddemon`, the EGA's sixteen), and
the whole of the Cowboy Bebop, Last Exile and Thundercats openings. Anime,
so flat colour regions and hard cuts - 41, 51, 79 and 101 shots, a median
7 to 20 frames each at 12 fps. That is the material the owner encodes, and
it is close to the best case for a per-shot palette: Bebop's opening is a
run of panels each tinted a single colour.

### 3.2 256 colours (VGA8)

Mean dB, and the worst frame, *P* = 160, `VBL` = 48:

| | `G` | `S` | `RP` (invisible anywhere) | `FP` (flipped) |
|---|---|---|---|---|
| GasGas | 34.42 / 27.0 | 34.74 / 31.7 | 34.45 / 31.6 | 34.49 / 31.6 |
| Bebop | 41.03 / 32.3 | 42.76 / 33.1 | 42.63 / **21.3** | 42.78 / 27.8 |
| Last Exile | 39.80 / **15.6** | 42.27 / 30.6 | 41.82 / 22.8 | 41.94 / 30.2 |
| Thundercats | 36.88 / 22.1 | 39.42 / 31.8 | 38.09 / 16.3 | 38.53 / 24.0 |

- **What a palette a shot is worth at 256 depends on the clip**: nothing on
  GasGas (+0.3 dB mean; its 256 already covers it), +1.7 to +2.5 on the
  other three, and on Last Exile the clip palette's worst frame (15.6 dB, a
  shot whose colours the clip's 256 never sampled) comes up to 30.
- **Recycling gets most of the per-shot MEAN, invisibly** - within
  0.15 dB of `S` on three clips, 1.3 on Thundercats - **but not its worst
  frame.** When a shot does not fit the slack the one before it left, its
  first frame is drawn from what it could get (Bebop's 21.3 dB against
  `G`'s 32.3) and it recovers over the next few frames as the old shot's
  slots come free. That is not a shift - no pixel changes colour without
  being redrawn - but it is a cut whose first frame is visibly worse than
  the clip palette's, and it must not ship without the fence (3.2.1).
- The DAC traffic is small: 6 to 27 KB over a clip, 2,127 to 9,005 slot
  writes, the largest single patch 153 slots (~460 bytes, ~4 ms of `out`s
  on an 8088).

#### 3.2.1 The fence: a shot dithers over its own slots

A smaller cap does NOT fix the worst frame - *P* = 128 left Bebop's at 21.2
- and instrumenting it says why: at the frame before each bad cut **250 to
256 slots were on the glass**, for shots of 128 colours. The ditherer's
nearest-colour table covers all 256 entries, so a shot spreads over every
slot still holding a colour near its own from any earlier shot, pins them
all, and the next finds no room (Bebop's shot 32 had 109 colours still
unplaced at its cut).

So **the encoder dithers each shot over its OWN slots only**, slot 0
included: every other slot is unreachable (the table is built with them
made slot 0's black, a tie going to the lowest index), and an old shot's
slots come free as its pixels are redrawn. Measured (`--fence`, *P* = 160):

| | `G` | `S` | `RP` fenced | blank needed |
|---|---|---|---|---|
| GasGas | 34.42 / 27.0 | 34.74 / 31.7 | 34.45 / 31.6 | 0 cuts |
| Bebop | 41.03 / 32.3 | 42.76 / 33.1 | 41.98 / 26.3 | 3 cuts |
| Last Exile | 39.80 / 15.6 | 42.27 / 30.6 | 41.80 / 26.7 | 0 cuts |
| Thundercats | 36.88 / 22.1 | 39.42 / 31.8 | 38.57 / 25.6 | 1 cut |

- **The next shot fits**: the flipped arm (`FP`) needed the blank at 0 to
  3 cuts a clip where it needed it at 9 to 72. **On VGA8, recycling alone
  is the mechanism**: 13h and Mode X the same, no retrace wait.
- The worst frames rise 4 to 9 dB on every clip that had a bad one; the
  mean holds on three and gives up 0.65 dB on Bebop. Against the CLIP
  palette the worst frame is better on three clips (Last Exile 15.6 to
  26.7) and worse on Bebop (32.3 to 26.3) - one cut frame there that the
  build's allocator has to win back.
- **They are still under the per-shot ideal** (26-27 dB against ~30):
  what is left is the allocator's - it takes the free slots for the
  incoming shot on the outgoing shot's LAST frame, so that frame loses its
  unused colours, and it places colours by popularity. Tuning those is the
  build's work.

### 3.3 Sixteen colours (VGA4)

Mean dB, and the worst frame:

| | `EGA` (`auto`) | `G` (`clip16`) | `S`: a palette a shot | `RP`: 8 + 8 recycled |
|---|---|---|---|---|
| **GasGas, 640 x 360** | 28.24 / 23.3 | 34.20 / 11.6 | **35.66** / 22.9 | 32.51 / 16.2 |
| Bebop | 29.28 / 19.0 | 29.68 / 13.9 | **39.09** / 19.1 | 34.26 / 10.1 |
| Last Exile | 32.75 / 19.3 | 33.21 / 13.0 | **35.85** / 17.6 | 33.14 / 11.8 |
| Thundercats | 28.68 / 23.0 | 27.69 / 16.4 | **31.93** / 16.8 | 27.59 / 10.0 |

- **A palette a shot (`S`) wins on every clip**: +3.1 to +9.8 dB over the
  EGA's sixteen and +1.5 to +9.4 over the clip's. On Bebop a shot's own
  sixteen beats the clip's by a median **8.9 dB** - 50 of 51 shots by 2 or
  more. It is section 2.2's CUT mode: no flip. (The first revision called
  it the bank switch and wanted a flipped screen for it; ruling 1 is why it
  does not.)
- **On the owner's GasGas the biggest single step is NOT mid-play**: the
  clip's own sixteen over the EGA's is **+6.0 dB**, and per shot adds 1.5.
  On the other three the clip's sixteen buys almost nothing (Thundercats:
  less than the EGA's) because their shots are each their own colour - for
  them it is per shot or nothing.
- **Recycling within sixteen does not pay**: halving to 8 a shot so the
  next has room is at or under the clip's sixteen on three clips - Bebop,
  whose clip sixteen is poor, is the exception - and 2 to 5 dB under a
  palette a shot on all four. For sixteen colours, CLEAN is not the mode;
  CUT is.

### 3.4 The bytes on the 286, which a palette of its own costs anyway

The encoder itself on GasGas 40-60 s with the owner's settings on
`286-speeddemon` (the report's section 2 has every arm):

| on the 640 x 400 screen, 640 x 300 | KB/s | frames cut |
|---|---|---|
| the EGA's sixteen, 30 fps | 137.9 | 14 of 599 |
| its own sixteen, 30 fps | **363.0** | **135 of 599** |
| ...flipped | 269.5 | **333 of 600** |
| ...at 15 fps | 278.6 | 71 of 300 |
| ...at 15 fps, flipped | 252.1 | 94 of 300 |
| ...30 fps, `--vga4-stable 48` | 357.8 | 124 of 599 |
| ...30 fps, the planes Gray-coded | 338.1 | 109 of 599 |
| ...flipped, on `486-dx2-66` | 377.3 | 65 of 599 |

**An own sixteen is 2.6 times the bytes of the EGA's on the 286, and the
budget cuts ten times the frames** - section 1's 2.40 bits a pixel against
0.97, not the palette changing. Doubling the dead band buys 1.4%. A Gray
code buys 7%: VGA4 pays per PLANE (98.1.3.2), `PLANE_CODES[16]` is the
identity, and the clip's luma-sorted sixteen put neighbours 7 and 8 at 0111b
and 1000b, so every alternation between them stores four planes. Flipping
makes it far worse and is out (ruling 3).

So the owner's 640 x 360 at 30 fps in its own sixteen is a file the 286
plays with a fifth of its frames cut to the budget; the encoder's own
report says which. That is the trade 98.1.3.2.2 now lets an encode make,
and `--palette auto` keeps the file it made before.

## 4. The format

What a switch costs a file is the colours it sets. Two carriers were
considered:

- **A table, read at open**: one entry per switch - the frame, the first
  slot, a count and the colours - on a sector named by the rendition (bytes
  39 on are free). **No byte in any record.** A seek finds its palette by
  replaying the table to the key, in memory. For sixteen colours an entry is
  ~52 bytes, so Thundercats' 101 shots are ~5 KB. **This is the carrier for
  CUT** (2.2), and the one the owner's "other than when it switches" asks
  for.
- **A patch in the record**, after its ten lists and before its audio -
  where the decoder's SI already is, so neither the lists nor `vdec.inc`
  change - for CLEAN's many small writes (2.1): 55 to 254 patches a clip
  on VGA8, 6 to 42 KB in all, too many to hold in memory on a machine whose
  ring wants it. A frame without one carries a zero byte (30 bytes a second
  at 30 fps), or a header flag says the file has none.

Either way:
- **A new header flag** (PALEV, 2048 - it was 1024 until the SCREEN flag took that bit) that every player before it refuses
  (98.1.1: a reader refuses any bit it does not know - "can't play this",
  never the wrong colours).
- **The header's palette (rendition +32) is frame 0's**, as now.
- **A keyframe's palette** is the table replayed to it (CUT) or carried in
  the key record (CLEAN: 768 bytes on VGA8, read only by a seek, never in
  the stream).
- **Persisting colours keep their slots** (2.2): the encoder matches each
  new palette's colours to the old one's within a step, slot 0 black always.
- **The seam (LOOPREC)** carries the palette back to frame *L*'s as part of
  its record, which is a whole-scene change by construction when *L* is a
  cut; otherwise the loop's last shot keeps frame *L*'s palette.
- **Refused together** (98.1.6): PALEV with RESIDENT, LIVE or RUNS, with a
  pixel format that has no DAC palette, and with the LAYER (98.1.9) until a
  layer is shown to keep the base's palette events in step. **Shown, and
  the refusal lifted** (SPEC.md 98.1.3.2.3.1): the player already drew both
  records after `vp_palev`; it was the encoder's layer that ranked in scene
  0's colours, and it now takes each scene's sixteen at its own frame.

**What the reader checks on the host** (`os88vid verify`): for CLEAN, that
every patch writes only slots its frame's screen does not use; for CUT, that
the switch lands on a frame that redraws every pixel whose slot moved. Those
two checks are the whole of "no shifting" as the owner defined it, minus the
one judgement only an eye makes - that the cut was a scene change (6).

## 5. The player

- **CUT**: when `[vp_done]` reaches the table's next frame, `vp_frame`
  writes that entry's colours to the DAC (`out 3C8h`, then 3C9h a byte - the
  same loop as `vp_dacw`) and decodes the frame. No wait.
- **CLEAN**: after `vp_decrec`'s lists, a non-zero patch is the same loop
  from SI.
- **A seek** (98.3.5, 98.3.14) starts from black already: the key's palette
  is loaded while only slot 0 is on the screen, then the key decoded.
- **The full screen's text** (98.3.13) draws in pixel value 0Fh on VGA4 and
  the brightest entry on VGA8; the encoder keeps that slot the brightest of
  every palette (palettegen's order already does on VGA4).
- **The poster** is the key's, in the key's palette, through `vp_lum` /
  `vp_v4xlat` as now.
- **Size**: ESTIMATED ~100-200 bytes of `video.o88` and no kernel byte - a
  table read, a compare a frame, the DAC loop, the refusals. No retrace
  wait, no flip, no bank.

## 6. The experiment: how generic can a CUT be?

The owner's ruling 1 is a claim about perception, so it is settled by
looking, and the first look costs no player work:

- **E1, on the host.** `os88palsim` already has every shot's palette and
  every frame's screen. It grows a render: the play at the 286's pace, each
  refresh showing what the glass would - the cut frame drawn over
  `os88venc`'s own per-frame decode time, so a CUT's mixed moment lasts as
  long as it would on the machine - written as an MP4 at 70 Hz for the
  owner to watch, the same clip three ways: the clip's sixteen, a palette a
  shot switched at every content-rule cut, and switched only at the cuts
  the coverage rule (below) passes.
- **E2, the detector.** A cut is whole when its frame redraws at least *C*
  of the canvas (the encoder knows exactly) AND the content rule fires.
  Sweep *C* (50%, 75%, 90%) and count, per clip, the switches kept and the
  dB each keeps of `S`'s gain; E1 shows the owner the cuts at each.
- **E3, persistence.** The palette matcher of section 4, measured as the
  pixels the cut frame must redraw that it would not have.
- **E4, the budget.** How often a switch frame would have been cut by the
  286's budget on these clips, and what deferring the switch costs.
- **Then MartyPC**, once E1 is liked: the player's half and `vidpal`
  (section 8).

## 7. CGA: why it is always teal and red, and why it stays one palette

**The pick is right by its own rule.** `cga4_pick` takes, of the 96 palette
bytes, the one whose four colours are nearest the clip's pixels on average.
On all four clips, over the owner's GasGas window and the whole of each
other clip, that is **mode 5's dim set - black, cyan, red and light grey -
on a black background**:

| clip | whole clip | per shot, most common |
|---|---|---|
| GasGas 40-114 s | 40h: mode 5 dim, black | 40h x27, 50h x9, 20h x2, 58h x2, 00h x1 |
| Bebop | 40h | **00h set 0 dim x18**, 20h x13, 40h x9, 50h x8 |
| Last Exile | 58h: mode 5 bright, dark grey | 28h x17, 40h x15, 48h x10, 50h x9, 58h x8 |
| Thundercats | 40h | 48h x32, 40h x22, 28h x6, 58h x6, 00h x4 |

and the RMS distance each set reaches at its best background (the report's
section 3; lower is better): mode 5 dim is first on all four - GasGas 51.7
against set 1's 52.5, Bebop 75.0 against 85.6, Last Exile 87.9 against
89.2, Thundercats 86.3 against 92.5.

Dim beats bright because most of a picture is dark or mid-grey and the dim
set's 170s are nearer it than the bright set's 255/85 mixtures; black beats
every other background because the darks dominate; and mode 5 beats set 1
because red covers skin and warm light where magenta covers almost nothing.
**Per shot it moves more than the whole clip suggests** - Bebop's tinted
panels take set 0 (green, red, brown) on 18 shots of 51 - so a per-shot CGA
palette is not an empty idea. It is section 7's next paragraph that rules
it out, not the colours.

**What the metric does NOT see** is the dither: it scores each colour as a
nearest match, where the encoder's Knoll dither mixes four colours into any
point of their hull. A metric over the hull might rank the bright sets
higher; that is an encoder question independent of this plan, and it is
section 9's question 5.

**Why not change it at a cut**: the palette byte is one `out` to 3D9h, so
the change itself is atomic - but mode 4 uses all 16 KB of the card, so
there is no second page to draw the cut on, and with four colours every one
is on the glass, so there is no free colour to recycle. A change at a cut is
the old picture in the new colours for the whole of the cut frame's draw,
which on a 4.77 MHz 8088 is several refreshes - long enough to be seen as
the scene's colour changing rather than the scene. **Recommended: C1 stands
for CGA**, and E1 (section 6) is what would change that: CGA's per-shot
picks are in this table already.

## 8. Gates

- **`vidpal`** (`tests/vidvga8.py` and `tests/vidscreen.py` extended): a
  clip with three shots, holds on the frame before each cut, the cut and
  the frame after, glass against the host decode through the palette as it
  stands - on 13h, Mode X and a VGA4 screen. Broken on purpose: the switch
  skipped fails every hold after the first cut.
- **`os88vid verify`'s new checks** (section 4) are the no-shift gates, and
  each gets a negative control: a hand-made file whose patch writes a slot
  on the screen, and one whose switch lands on a frame that leaves a moved
  slot's pixels standing, must be REFUSED.
- **The seek**: from a key after a switch, the glass equals the host's
  decode (98.3.5's `vidpreview` shape).
- **Built already**: `vidscreen --screen 640x480` (98.1.3.2.2's gate) and
  `videnc` 5c.3 (the encoder's 640 x 480 file, and Live refused).

## 9. Questions for the owner

1. **E1 first?** A host-side MP4 of the CUT switch on your clips, at the
   286's pace, before any player work (section 6).
2. **The detector's coverage *C*** (E2): pick from what E1 shows.
3. **CLEAN on VGA8 as well, or CUT alone?** CLEAN is fenced recycling (+0.0
   to +2.0 dB mean on these clips, the clip palette's worst frame lifted on
   three of four); CUT alone gives VGA8 `S`'s +0.3 to +2.5.
4. **Gray-code the planes for an own sixteen** (3.4)? Independent of the
   rest: 7% of the bytes and a fifth of the cuts of any own-sixteen file,
   for an encoder table - but pixel value 0Fh stops being the brightest
   colour, which the full screen's text assumes (98.3.8.2), so it comes
   with choosing that colour another way.
5. **CGA4's pick**: leave it, or score palettes by what the dither can
   reach rather than by nearest colour (section 7)?

## 10. Rate: steps before cutting (the owner's direction, 2026-10-13)

Section 1's measurement exposed what the encoder has circled without naming:
**its one answer to a stream over the profile is to CUT frames**. A
stability threshold and a palette are chosen up front, and anything that
does not fit is torn. The EGA's sixteen worked on the 286 because they forced
GasGas down to ~1 bit a pixel - a coarse quantiser nobody chose. What the
encoder wants is a way to CHOOSE that, by steps, and a say in how much it
cuts against how much it condenses.

The owner's rulings on it:
- **A cut is not equally visible everywhere.** Bebop can lose a lot of
  frames and barely show it; Last Exile, at 256 colours in Mode X, tears
  and lines. The encoder already scores this per stretch (its "VISIBLY
  BROKEN" seconds: 2% of the frame or more off by 15% as seen), so a cut
  can be priced per scene.
- **Going all the way down before cutting does not work**: two colours look
  as terrible as tears. So the steps interleave.
- **A palette condense must not flash** - at a whole-scene cut, or by
  phasing colours out within a scene (look ahead and behind).
- **A calmer dither may look better in general**: at 320 x 240 or less a
  dither's squares are seen one by one, and their flicker only sometimes
  helps - but sometimes the busier dither is what gives a scene its look.
- **It may need a person**: settings for a time period, and an editor to
  pick them. FOLLOW-UP work, not the first build.
- **All of it stands on the owner testing and liking the result**; any of
  it may be walked back.

**The ladder, as first written** - each step taken only while the stream is
still over the profile, and every one per stretch:

1. a small palette condense (16 to 12, say);
2. a slightly calmer dither;
3. cut frames, where this scene hides cuts;
4. another palette condense;
5. cut as much as needed.

The condense steps walk back up as soon as the stream allows - to the
previous scene's palette where it is the same scene, or by phasing colours
back in.

**What makes a condense invisible inside a scene is section 3.2.1's fence**,
and it needs no palette change at all. Condensing 16 to 12 is the dither
STOPPING choosing four of them: their slots stay loaded, a pixel already on
the screen in one keeps it until it is redrawn, and no colour on the screen
changes. Walking back up is the dither choosing them again - they are still
in the DAC. Only choosing NEW colours (a better-fitting 8, say) needs a
whole-scene cut (2.2). What the fenced subset gives up against a fresh
`palettegen` of the smaller count, and against `PLANE_CODES`' plane
grouping (fewer colours as fewer planes only when the values are chosen for
it), is for E5 to measure.

**E5, the first measurement** (section 6's shape;
docs/reports/VIDEO-RATE-2026-10-13.md): the owner's GasGas window
at 20 s on `286-fast`, through the encoder, at its own 16, 8 and 4 colours
and at dither widths of 16, 4 and 1 (Knoll's plan cut to N candidates; 1 is
plain nearest colour), against the EGA's sixteen - KB/s, frames cut, the
encoder's broken seconds, and blurred PSNR off the decoded file:

| GasGas 40-60 s, 640 x 360, 30 fps | demand (lossless) | on `286-fast` | frames cut | broken | PSNR |
|---|---|---|---|---|---|
| the EGA's sixteen | 166 KB/s | 158 | 27 of 599 | 0.3 s | 26.5 |
| own 16 | 434 | 339 | 289 | 0.9 s | 31.3 |
| own 16, dither of 4 | 431 | 337 | 289 | 0.9 s | 31.4 |
| own 16, nearest colour | 435 | 337 | 297 | 0.8 s | 31.3 |
| own 16, `--vga4-hold 24` | **242** | 228 | **71** | 0.5 s | **30.1** |
| own 16, `--vga4-hold 40` | **171** | 164 | **28** | 0.4 s | 27.6 |
| own 8 | 297 | 275 | 103 | 0.9 s | 28.8 |
| own 4 | 158 | 155 | 15 | 0.3 s | 27.2 |

(PSNR of a 3 x 3 blur off the DECODED file, so a cut frame scores what it
leaves on the screen; "broken" is the encoder's own seconds VISIBLY BROKEN,
the tearing an average hides.)

What it says:
- **A narrower dither pattern is NOT a rate step.** Knoll's plan cut to 4
  candidates, or to plain nearest colour, asks for the same bytes - nearest
  even flickers more, its plan being one colour and the plan test stricter
  for it. The rate follows how many colours there are, close to their
  planes: 16 is 4 planes, 8 is 3, 4 is 2.
- **The calm that IS a rate step is a COLOUR HOLD**: a pixel keeps its
  colour while that colour is within D of the source, whatever the plan
  says (VGA8's rule). It halves the changes at 24 and reaches the EGA's
  rate at 40 - and **own 16 held at 24 beats own 8 on rate AND picture**
  (242 KB/s against 297, 30.1 dB against 28.8). So the ladder's "slightly
  calmer dither" is this, and it may come BEFORE the first condense.
- **Own 4 is the EGA's rate with better colours** (158 against 166 KB/s,
  27.2 against 26.5 dB, 15 frames cut against 27): what the EGA forced on
  GasGas, chosen.
- **The hold's risk is the one 98.2.5 retired it for**: a dot a dim trail
  left behind, standing while the picture moves. Blur PSNR does not see it;
  the owner's eye does. It is BUILT as an encoder option, `--vga4-hold`,
  off by default and byte-identical when off (98.2.5), so it can be looked
  at on real clips.

So the ladder as measured so far reads: **hold (24, then 40), condense
(16 to 8, then 4), cut** - interleaved by how well each scene hides a cut,
which is the next thing to make measurable.

### 10.1 The owner looked, and the palette was the problem (2026-10-13)

The E5 sample files, watched (docs/reports/VIDEO-PALETTE-CHOICE-2026-10-13.md
has the verdicts in full): own 4 and own 8 picked ONLY GREYS - no red
headlights, no faintly blue sky; own 16 got the headlights back but still
overvalued greys; **the EGA's sixteen looked best of all, on colour**. The
rule the owner put it in is the one to design by: **greys can be dithered
into existence, colours cannot.** A pattern dither reaches the hull of its
palette; `palettegen` places colours by how many pixels have them, so a dark
scene's palette is a grey ramp and the 5% of pixels that carry its colour
fall outside the hull.

**And the score was wrong, which is the bigger lesson**: blurred-RGB PSNR
rated own 16 4.8 dB ABOVE the EGA's - in a dark scene it is lightness, which
is greys. The score that agrees with the owner is CIELAB's: the share of
chromatic pixels drawn at under half their chroma ("lost"), beside chroma
and lightness error. It ranks the files as the owner did (EGA 7.6% lost,
palettegen 16 55.7%, palettegen 4 69.7%).

**Chroma-weighted k-means** (CIELAB, each pixel weighted 1 + C*/8, black
fixed) keeps the EGA's reach - 7.2% lost - with half its chroma error and
palettegen's greys, and asks for FEWER bytes than palettegen's sixteen (368
against 434 KB/s): colours spread for reach sit further apart, so the dither
moves between them less. At 8 and 4 it does not hold yet.

**The hold comes back.** The owner saw own 16 at full blobbed too, so the
blob was the palette's as much as the hold's. With the chroma sixteen on
`286-fast`: alone 163 frames of 599 cut, 8.2% lost; **held at 24, 34 cut
(the EGA's 27) with 15.7% lost (the EGA's 16.0%) and half the EGA's chroma
error**; held at 40, 21 cut. Sent to the owner as CHR16, CHR16H24 and
CHR16H40 - which is what decides it.

So the ladder's first fix is not a rung at all: **the palette chooser**.
`--palette clip16` should choose for colour, and every rate step is measured
again on top of it.

### 10.2 The palette is right; the hold needs a memory (2026-10-13)

The owner on CHR16, CHR16H24 and CHR16H40: **the palette and the colour are
VERY good** - so the chroma-weighted chooser of 10.1 is the one to build
into `--palette clip16`. The hold was right to revive and has the old dot
dither's failure: hold 40 keeps a whole sky or mountain from the last scene
across a cut or a big camera move (24 mostly lets go), and on a turn the
mountain stops going by - at 40 completely - where the EGA's sixteen kept it
moving on less data.

Both are one omission: the hold compares the colour on the screen with the
source NOW, with no memory and no motion - and bypasses `moved <
--vga4-stable`, the shipped rule's own motion test. The owner's fix is a
temporal judgement: a pixel that WANTS another colour must be let go, and
may then hold at its new one. Its first form is a gate - hold by colour
only while the source has moved less than 24 since the colour was chosen -
and a score that sees the failure (STALENESS: blocks whose screen stands
still while their source moves on; docs/reports/VIDEO-HOLD-2026-10-13.md).
The score ranks the ungated holds as the owner did (40: 16.8% stale, 24:
5.8%, the EGA's 4.3%); gated, hold 24 is 3.7% and hold 40 4.6%, at 221 and
209 KB/s and 57 and 48 frames of 599 cut on `286-fast`. A release at
whole-frame changes adds nothing - the gate already lets go at a cut. Sent
as TH24 and TH40; the owner's eye decides it.

**The owner: TH24 and TH40 both still stick.** So the score itself: a pixel
the dither wants to change is held by colour only while a leaky sum (decay
0.8 a frame) of how far its held colour is from the source, over the frames
it wanted to change, stays under T; a pixel that changes starts again. At T
= 60 the stuck share falls to the no-hold file's (0.6% of blocks stale for
half a second, the EGA's 1.5%) at ~253 KB/s, a fifth under the no-hold 315
and 64 frames of 599 cut against 163; at T = 100 it sticks as before
(docs/reports/VIDEO-HOLD-2026-10-13.md). Sent as S40T60, S24T60 and
S40T100.

### 10.3 Stray dots: the plan's reach (2026-10-13)

**The owner: wrong colours flash constantly**, in the one-palette file and
the palette-a-scene file alike - bright red and yellow dots in a dark,
still scene, nowhere near a cut. The host's reference decode of GASONE
shows them, so they are in the stream: the dither put them there. Knoll's
plan carries its error unbounded, and the clip's sixteen have no colour
near a dull warm grey, so the plan balances it with the palette's strong
red one cell in sixteen (6 to 30% of the pixels across the warm greys) and
an olive grey with yellow; the source's noise moves which cell, and that
is the flash. Capping the reach - a candidate further than 64 from the
source, or than twice the nearest colour, is the nearest colour - takes it
from 0.031% of the picture to 0.003% on ten frames, 1,383 dots to 50 on
the worst, for 1.97 to 2.02 blurred dE and 0.6 points of colour lost. A
damped error (0.75, 0.5) does less for more. The EGA's sixteen cannot take
it (39% of colour lost) and do not: it is the clip palette's alone, as
SPEC.md 98.2.5's `--vga4-plan-cap`. Sent as GASONEC and GASSCNC.

### 10.4 The flashing was the CUT, not the dither (2026-10-13)

**The owner, on GASSHOTX: the wrong colours are not lessened at all** -
bright red and white around edges, a frame either perfect or full of them,
hundreds of frames, in every recent encode and none of the old ones. The
host's decode shows them, so the stream carries them; a lossless encode of
the same four seconds has 27 stray pixels in 120 frames, the `286-fast`
one 924, all in the 19 frames the per-frame CPU ceiling cut. A byte that
changes on several plane groups is several stores (98.1.3.2), and the cut
ranked and kept them one at a time: a dark blue's planes 3 and 0 written
and 1 and 2 not is a bright red. The old encodes were the EGA's sixteen
at far lower rates and were rarely cut; a clip's own sixteen at 300 KB/s
on a 286 is cut every few frames. The cut now keeps a byte's planes
together (`byte_units`, SPEC.md 98.1.3.2): 924 to 110 at the same bytes and
frames cut, the rest pixels a cut left in the last frame's colour. 10.3's
cap was real but small beside it. Sent as GASSHOTU.

### 10.5 The flashing that is left is the PLAYER's draw (2026-10-13)

**The owner, on GASSHOTU: it can no longer be paused and seen, and it still
happens.** A record is drawn sub-record by sub-record, one group of planes
over the whole frame at a time (SPEC.md 98.1.3.2), and a 286 takes most of
a refresh to draw a frame with nothing waiting for the retrace: a byte
written on some planes and not yet the rest shows a colour neither frame
has to any refresh that passes it. A replay of the player's write order
priced by the 286-fast's own table, counting such pixels by how long they
stand against a 70 Hz refresh, puts 288 on the glass a frame and 57 frames
of 300 past 200 on Gas Gas Gas 89-99 s. `--bands N` (98.2.1.2.2) shrinks
the window: 100 / 51 / 34 / 26 / 17 at 4 / 8 / 12 / 16 / 24 bands, for
+4 / +9 / +12 / +15 / +22% of data and +1 to +6% of CPU - every band pays
each of its masks' fourteen-byte sub-record. Two cheaper orders were tried
and are not kept: banding only the bytes with more than one group (the
split runs cost what the bands saved), and Gray-coded plane values
(TWICE the bytes with more than one group, 821 mixed pixels a frame
against 410).

### 10.6 Write mode 3: one store, every plane (proposed, 2026-10-13)

The owner's find. In write mode 3 the colour is the Set/Reset register and
the byte stored is a MASK of the pixels that take it, so a pixel goes from
its old colour to its new one on all four planes in ONE store - nothing in
between for a refresh to catch, and no bands. The rest of the byte comes
from the latches, so a store must read its byte first; read mode 1 with
Color Don't Care 0 makes that read FFh, and `AND [ES:DI], AL` loads the
latches and stores the mask in one instruction. Only a byte written on
more than one plane group needs it - a one-group byte is one store today -
so the design is a new sub-record, a colour and its mask lists, beside the
plane sub-records, under a new flag. Mode X needs none of it: a Mode X
pixel is one byte of one plane. **What a read-then-store costs on a 286's
VGA is the open number**: `make vm3bench` is its bench (VM3BENCH.O88 on a
360KB and a 1.44MB disk), and checks the card does write mode 3 right
first - MartyPC's VGA does not do it at all, so the check is BAD there and
the field run is the evidence. On MartyPC's XT, per 2,000 bytes: a plain
store loop 18.1 ms, AND-store 27.5, read-then-store 26.4, and an OUT
7.5 us.

**The owner's 286 (PVGA1A): both checks OK**, and a write mode 3 AND-store
is 3.20 us a byte against 2.11 for a store in the same loop and 1.15 a
byte of a block move; an OUT is 1.88 us. Every byte with more than one
plane group in write mode 3 would cost the decode +33% (65% of the bytes
written); only the bytes some state of which shows a colour > 25 dE from
both its old and new one, +10.6% (14% of them) - mean 25.9% of a period to
28.6%, the worst frame 51.6 to 63.1 ms - for NO wrong colour on the glass
(docs/reports/VM3BENCH-PVGA286-2026-10-13.md).

### 10.7 The list map: what a band costs, halved (2026-10-13)

The owner asked whether 10.6 replaced 10.5's other proposal - a cheaper
sub-record header, so bands cost less - or combined with it. They combine:
both are SUB2 (SPEC.md 98.1.3.2.4), one flag, one format change, and the
map is not VGA4's alone: Mode X's bands only trade combing for one tear
line, but its sub-records carry the same ten list ends. Measured on Gas
Gas Gas 89-99 s, `286-fast`, the bytes a file saves when a sub-record
carries a 16-bit map and only its present lists instead of ten list ends:

| bands | saving | per second |
|---|---|---|
| 0 | 0.9% | |
| 12 | 6.0% | 14.2 KB |
| 24 | 9.5% | 24.6 KB, 840 bytes a frame |

So the map HALVES a band's overhead - 24 bands go from about +22% of the
file to about +10% - rather than making it a few percent, which is what
was first said here and was wrong until it was measured. In the player it
is a gate at each list's head, `shr` and a jump (~35 cycles), where an
empty list's own 0 was ~50 and a byte; a file without the flag decodes
through the same gates with the map all ones.

### 10.8 Write mode 3, built (2026-10-13)

**The encoder** (98.2.5) puts a byte in write mode 3 when some state the
plane groups would pass it through - applied in ascending mask order - is
more than `WM3_DE` = 25 dE from both its old and its new colour, and only
in a file of its own sixteen (98.1.3.2.1), never flipped, Live or
resident. Its cost is priced per profile (`wm3_cost`: VM3BENCH's 3.20 us a
byte on the 286, `WM3_US_286`), so the cut sees it. On Gas Gas Gas
89-93 s at `286-fast`, every palette rule as shipped: **968 of 2,582
sub-records are colour ones; 299.1 KB/s against 306.1, 22 frames cut
against 18, the hook's mean 176.7% of a period against 173.7%** - and
10.5's replay of the player's write order counts **0 wrong pixels on the
glass a frame against 117.4**, no frame past 200 against 26.

**The player** is SPEC.md 98.1.3.2.4's: +2,012 bytes of `video.o88`'s
image (the write mode 3 chain unrolled less than the plane one: a round of
eight on every list length was 1,702 bytes of a package already at 55.8 KB
of its 60), no kernel byte. `tests/vidfskeys.py --kind vga4m3` is its gate
on MartyPC, whose VGA had no write mode 3 at all and has it now
(`tools/martypc/patches/12-vga-write-mode-3.patch`): a clip of plane and
colour sub-records together, exact on the glass at every hold, round the
text box included; red with the box's save left alone (64 pixels) and with
write mode 3 never selected (912).

### 10.9 The condense step, per shot: first measurement (2026-10-13)

The ladder's first step taken on the owner's whole window: Gas Gas Gas
40-114 s, `286-fast`, a palette a shot, hold 24 scored 60, the XMS bank on
auto and filled first, write mode 3 - GASSHOTW's settings (10.8) - with
every shot at its own 16, 12 and 8 (`--palette colour12` / `colour8`, built
for this). The disk cuts nothing here: every cut is the per-frame CPU
ceiling's or the record's 32 KB.

| every shot at | KB/s | CPU mean | frames cut | broken |
|---|---|---|---|---|
| own 16 | 280.2 | 185.5% | 466 | 4.4 s |
| own 12 | 254.8 | 171.3% | 356 | 3.9 s |
| own 8 | 218.9 | 152.0% | 306 | 3.2 s |

The shots that cut most (file time; the clip starts at 40 s):

| shot | frames | cut at 16 | at 12 | at 8 |
|---|---|---|---|---|
| 0:21.0 | 155 | 104 | 54 | 62 |
| 0:48.3 | 139 | 28 | 21 | 17 |
| 0:44.9 | 95 | 21 | 11 | 8 |
| 0:15.6 | 86 | 13 | 11 | 9 |
| 1:02.1 | 75 | 13 | 12 | 8 |
| 0:28.0 | 11 | 11 | 11 | 11 |

What it says:
- **A condense is a real rate step**: 16 to 12 is -9% of the stream and
  -24% of the cuts, 12 to 8 a further -14% and -14%.
- **It is concentrated**: one shot, 0:21.0, is 104 of 16's 466 cuts, and
  12 halves it. Most shots cut a handful of frames at any count.
- **It does not reach the short shots**: a shot of 6 to 12 frames is cut
  whole at every count. Those are the shot changes themselves - a whole
  new picture is more than a frame holds - and no colour count fixes that.
- **The heaviest shot is NOT monotone**: 8 cuts it more than 12 (62
  against 54). Fewer colours dither with more pattern, and on that shot the
  pattern moves.
- **The "error as seen" falls with the count (0.59, 0.55, 0.43%) and must
  not be read as the picture getting better**: it is measured against each
  file's own dithered target, so it sees what the cuts leave and not the
  colours a condense takes away. That half is the owner's eye.

GAS12 and GAS8 were sent to be looked at.

### 10.10 What the owner saw in 10.9, measured (2026-10-13)

**The owner**: 12 looked fine and 8 dropped red at times; the cuts at
0:21-0:26 - the slow zoom into a face - are not visible at all; what is
cut most to the eye is 1:08-1:13, the cars swerving with the camera
following; and every own-colour file dithers the road SOLID where the
EGA's sixteen draw it in a pixel-on, pixel-off pattern - "a 1990s poorly
dithered web gif".

**The solid road is `--clip`**, the solid-colours rule (98.2.5): a pixel
within 16 levels of its nearest colour IS that colour. It was made for the
EGA's sixteen, where almost nothing is that near and it only stops a grid
of dots on near-black; a clip's own sixteen are near nearly everything, so
it flattens gradients into bands. On the headlit road at 76.5 s the share
of neighbouring pixels that differ is 71% with the EGA's, 7% with own 16
as encoded, 7% without the hold, 7% without the plan cap - and 42%
without the clip rule. What turning it down costs, 70-80 s, `286-fast`,
own 16 a shot:

| `--clip` | KB/s | frames cut | broken |
|---|---|---|---|
| 16 | 291.6 | 51 | 0.5 s |
| 8 | 291.0 | 58 | 0.5 s |
| 4 | 297.4 | 65 | 0.6 s |
| 0 | 305.5 | 71 | 0.6 s |

At 0 the dot grid on near-black the rule was made for is back; 8 and 4 are
the candidates, and the default stays 16 until the owner has looked.

**The count of cuts was the wrong ranking**: a slow zoom changes every
pixel a little, so a frame cut there leaves pixels a frame behind and
nearly right, and the zoom topped the list at 104 cuts. The report now
ranks scenes by frames VISIBLY BROKEN (the picture as seen off by 15% over
2% of the frame), and the encoder's broken spans already agreed with the
owner: none in the zoom, the longest at 1:07.7.

**The swerving is the scene detector cutting a fast camera**: from 104.6
to 114 s, 17 palettes in 9.4 s, many at the 6-frame minimum. The fix that
was planned - keep the palette while it still fits - does not separate
them: a palette over both sides of a pair fits each about as well across
the owner's real cuts (93.0, 97.7, 99.6, 102.1, 104.6 s: 1.25 to 1.96 times
its own palette's error) as across the swerve's (1.07 to 1.48). And the
switches are NOT most of the damage there:

| 104-114 s | palettes | frames cut | broken |
|---|---|---|---|
| a palette a shot, 6 frames at least | 18 | 114 | 1.8 s |
| one palette | 1 | 101 | 1.7 s |
| a palette a shot, 15 frames at least | 11 | 110 | 1.6 s |

The motion is more than the 286 draws. What fewer switches buy is fewer
colour flashes, so `--scene-min` is a lever (6 frames as before); the rate
is the ladder's - the hold and the condense, where motion hides colour.

**The whole window with both changes** (own 16 a shot, 40-114 s,
`286-fast`, `--scene-min 15`), sent as GASC8 and GASC4:

| | palettes | KB/s | frames cut | broken |
|---|---|---|---|---|
| GAS16 (`--clip 16`, 6 frames) | 55 | 280.2 | 466 | 4.4 s |
| GASC8 (`--clip 8`, 15 frames) | 40 | 279.5 | 529 | 4.9 s |
| GASC4 (`--clip 4`, 15 frames) | 40 | 289.9 | 608 | 5.5 s |

The texture is paid in CPU and not in bytes: +63 and +142 frames cut, the
per-frame ceiling every time. Ranked by what is seen, the shots most broken
are 1:09.6, 0:28.0, 0:07.4, 0:28.6 and 1:06.8 - the swerves and the fast
cuts - and the zoom at 0:21.0, 106 frames cut, has ONE frame visibly
broken: the ranking agrees with the owner's eye where the count did not.

### 10.11 Detect the dot grid instead of clipping (2026-10-13)

**The owner**: can the dot grid be found and removed where it happens,
rather than flattening every near colour? It can, and on a clip's own
colours it mostly does not happen at all. Knoll's plan is sixteen picks a
pixel; a colour making one or two of them is one Bayer cell in each 4 x 4
tile - the even grid. `--vga4-sparse N` gives such a pixel its nearest
colour instead. What the pixels look like (own 16, one frame after twelve
of hold, "off" = how far a pixel is from its eight neighbours' mean):

| | neighbours differing | off > 12 | off > 24 | blurred error |
|---|---|---|---|---|
| lit road 76.5 s, `--clip 16` | 7.2% | 0.07% | 0 | 1.9 |
| `--clip 0` | 61.9% | 2.40% | 0.06% | 1.3 |
| `--clip 0 --vga4-sparse 2` | 52.6% | 0.70% | 0 | 1.3 |
| `--clip 4 --vga4-sparse 2` | 20.3% | 0.34% | 0 | 1.6 |

On the dark road (108.5 s) and Bad Carrot (1:04, the glow at 1:36) the
pixels more than 24 off their neighbours are the SAME at every setting -
edges and the watermark, not dots: a clip's own sixteen always have a
colour near, so the high-contrast stray dot is the EGA's sixteen's and the
256-colour greys' (the owner: it was Bad Carrot at 320 x 240 in 64 greys).
What `--clip 0` brings back on own colours is a LOW-contrast pattern of
two close colours - the road's texture.

And the rule is cheaper, not dearer: a lone dot is a pick that comes and
goes with the source's noise. 70-80 s, `286-fast`, own 16 a shot:

| | KB/s | frames cut | broken |
|---|---|---|---|
| `--clip 16` | 291.6 | 51 | 0.5 s |
| `--clip 0` | 305.5 | 71 | 0.6 s |
| `--clip 4 --vga4-sparse 2` | 287.2 | 61 | 0.5 s |
| `--clip 0 --vga4-sparse 2` | 286.2 | 63 | 0.6 s |

Over the whole window (GASS2, 15-frame scenes): 282.0 KB/s, 576 cut, 5.2 s
broken, against GASC4's 289.9, 608 and 5.5 - more texture than GASC4 for
less of both. **The owner: "better dither AND less data", made the default
where it wins** - `--palette colourN` on a VGA. `--clip 16` had been set in
more places than it belonged: it is the fixed palettes' rule, where the
grid is real.

### 10.12 The ladder, built as a chooser (2026-10-13)

SPEC.md 98.2.1.9 is the contract. The owner's two rules for it: a first
pass is SHARED - page-ahead's or the layer's when either runs, and every
later pass reads it rather than making its own - and where 12 already asks
for less than 8 the 8 step is dropped. What it does not yet do: the calmer
dither step (a stronger hold for a heavy scene), which needs the owner's
eye on a scored hold at 40 first; and a second evaluation pass, so 8 is
decided from the first pass's breakage (30% of a scene's frames) and the
priced demand rather than from 12's result.

**The coarse cut is the first rung** (the owner, on the other session's
`--coarse-cut`: "64x8 when needed, or 16x4 or somesuch when we can afford
that"): `--coarse-cut auto`, the default, tries each cut whole, 16x4 and
32x8 on a copy of the encoder and takes the first drawn whole.

The owner's window, Gas Gas Gas 40-114 s, `286-fast`, own 16 a shot,
`--scene-min 15`, every default of the tree on 2026-10-14:

| | KB/s | frames cut | broken | worst second |
|---|---|---|---|---|
| GASS2 (`--clip 0 --vga4-sparse 2`, 64x8 cuts not yet in) | 282.0 | 576 | 5.2 s | |
| GASLAD (+ `--step-down`, the same tree) | 269.1 | 556 | 4.8 s | 7.69% |
| GASL2 (+ `--coarse-cut auto`, page-prune's tree) | 266.3 | 550 | 4.2 s | 4.74% |

GASL2's ladder priced 19 of 40 scenes and stepped 13 (10 to 12, 3 to 8);
the guard kept five at 16 (0:28.6, 0:30.2, 0:30.7, 1:04.7, 1:12.3 - a 12
would lose a colour 16-25 dE away) and one stayed because its 12 asked
MORE than 16 (0:57.8). Its cuts: 1 whole,
4 at 16x4, 1 at 32x8, 33 at 64x8.


## 11. A palette a scene, built (2026-10-13)

Section 2.2's CUT mode as SPEC.md 98.1.3.2.3 now pins it, for sixteen
colours (the owner: "16 is where the big wins come in"):
- **section 4's TABLE was the carrier**: read at open, one 52-byte entry a
  scene, no byte in any record - the owner's "other than when it is
  switching", exactly. The patch-in-the-record carrier was for CLEAN's many
  small writes and is not built.
- **section 5's player**, less the flip and the retrace waits rulings 1
  and 3 retired: the sixteen loaded before the scene's first frame is
  decoded, and a seek's palette loaded over black. +611 bytes of
  `video.o88`, no kernel byte - section 5 ESTIMATED 100-200, and the
  difference is the hostile-input checks of 98.1.6 a table needs.
- **section 6's E2 as a rule, not yet a sweep**: a cut is WHOLE when the
  content rule fires AND half the picture changed by more than 24 levels.
  On Gas Gas Gas 40-114 s that is 22 scenes where the content rule alone
  found 41 shots.
- **section 2.2's third condition is MEASURED, not enforced - and it
  CANNOT be met at 640 x 360**: a record holds 32 KB, a whole four-plane
  canvas is several times that, so a change of the whole picture is three
  or more frames on every profile, `lossless` included. The switch is drawn
  across them. A single-palette file leaves as much of the last scene
  standing across the same cuts (33% against 39%); what the table adds is
  that the remnant is in the new colours, ~15 dE off, for those frames.
  Whether that is seen is the owner's eye, on GASSCN.
- **matching each scene's order to the last one's was tried and dropped**:
  the darkest-first sort already does it (15.2 dE moved against 17.9).
- **it costs bytes**: +5% asked on 20 s lossless, and on the 74 s at
  `286-fast` the per-scene files ask past the disk (296 frames cut by it
  against 54). docs/reports/VIDEO-SCENES-2026-10-13.md is the measurement.

The gate is `vidpal`, red with each of the player's three pieces taken out
(the switch, the seek's palette, the table's order check).

### 11.1 A scene is a SHOT (the owner's eye, 2026-10-13)

**The owner**: in the two-men shot of GASSCN, red and a bright off-yellow
that nothing in the shot has - the watermark, off-white the whole video,
drawn yellow. The palette-a-scene file had cut 88.1 to 105.0 s as ONE
scene: five hard cuts inside it (92.97, 97.73, 99.6, 102.13, 104.63 s) each
pass the content rule (mean HSV change 29.5 to 57.5 against 27) and each
fail the second test, only 7 to 35% of the picture changing by more than 24
levels - dark shots differ by small absolute amounts. So one sixteen served
tail lights, headlights, a close-up and the two men, and the men got the
lights' red and yellow. The second test was this plan's guard against a
reverse angle in one room shifting its colours; the owner's ruling is that
a change WHILE the picture changes is not seen, and a shot cut is that.
It is now `--scene-cover`, 0 by default: a palette a shot. Sent as GASSHOT
beside GASSCNC (the old cut, with 10.3's cap) and GASONEC.

### 11.2 Shifting slots inside a shot: what it could win, measured first (2026-10-14)

**The owner's ask**: inside a shot, stop using a few colours, change those
slots while nothing on the screen is in them, then start using the new
ones - so the sixteen follow a shot whose colours drift, with no flash.
The FORMAT needs nothing: 98.1.3.2.3's table takes an entry at any frame,
and reloading the colours of the slots in use shows nothing. It is an
encoder's job (drain the slots, place the entry once the canvas holds none
of them, use them after), and before building it this is what it could buy.

The instrument is `tools/os88paldrift.py` (an instrument: nothing in the
build runs it): each shot (`scene_pass` at 15 fps, 640x360, every fourth row and
column) gets its own sixteen as `colour_kmeans` makes them; each 2-second
window of it is then scored against (a) the shot's sixteen, (b) the
window's OWN sixteen - the ceiling of any shifting at all, (c) the shot's
with the 2 and (d) the 4 slots that cost least dropped and refitted to the
window - what a shift of that size reaches. Two scores, both CIELAB
weighted 1 + C*/8 as the k-means weights: the mean nearest-colour dE, and
the TAIL - the share of pixels more than 10 dE from every colour, which is
a colour the dither cannot reach.

| clip (seconds) | mean dE: shot / own / 2 / 4 | tail %: shot / own / 2 / 4 |
|---|---|---|
| Gas Gas Gas 40-114 | 2.28 / 2.13 / 2.23 / 2.21 | 1.66 / 1.51 / 1.73 / 1.88 |
| Bebop 0-90 | | 2.34 / 1.93 / 2.29 / 2.36 |
| Last Exile OP 0-90 | | 9.87 / 8.48 / 9.88 / 10.43 |
| Thundercats 0-90 | | 10.84 / 10.95 / 10.95 / 11.50 |
| Bad Carrot 0-90 | | 0 everywhere |

**The ceiling is small, and a shift reaches about none of it on average**:
a window's own sixteen take 7% off the mean and 10-18% off the tail, and
the greedy 2- or 4-slot shift - refitted for the mean - leaves the tail
where it was or worse. A shot cut already moves the palette (98.1.3.2.3),
and these clips' shots are short: Gas Gas Gas's median is about two
seconds, so most of a shot IS one window. Thundercats' and Last Exile's
tail is high everywhere, own palette included: sixteen colours are too few
for those frames, which no shift changes.

**Where it does win is a few LONG shots**: Last Exile at 63.9 s (106
frames) 6.18% -> 2.98 own, 4.94 with four slots shifted; Bebop at 2.1 s
(179 frames) 2.48 -> 1.39 own, 1.90 with two; Bebop at 35.7 s 1.82 ->
0.65 / 1.29. So if it is built, it is built as a TRIGGER - a shift only
where a window's own sixteen would take the tail down by a margin worth a
drain's bytes - and most files would carry none; the refit would aim at
the tail and not the mean, which is where the greedy step here went wrong.
Waiting on the owner's call.

**Parked** (the owner, 2026-10-14): recorded here to come back to after the
bugs in 11.3.

### 11.3 GASGAS4: a page-ahead file with no key 0 (2026-10-14)

**The report**: wrong colours at the start, pink and olive blocks that
persist through a whole shot, and at page-ahead flips blocks missing from
the corner signature that come back at the next flip. The file (encoder
options v29) was page-ahead (47 flips) on the 640 x 400 screen, with
**0 keyframes**: its first was past 65,535 bytes and was left out.

**The player was faithful**: the reference decode (`os88vid.py decode`)
of frame 50 is the owner's screenshot, pink and olive included. And the
cause is that one key: a page-ahead key 0 carries the hidden page's first
picture (98.1.3.7), and every later byte the encode writes to that page
assumes it - a byte changed in some planes is written to those planes
alone. Without the key the page starts black, so those plane writes make
colours nobody chose, and since the encoder believes the page right it
never rewrites them: the signature, which never changes, is broken on
page 1 at every flip to it for the whole minute and whole on page 0.

**Fixed both ends**: the encoder makes a page-ahead file only when the
player can read its key 0 (98.2.1.8, `page_key0`), and the player PLAYS a
page-ahead file with no key read - the options audit had made that a
refusal, and the owner's rule is that only a play that would break is
refused; bad pixels at the start are not that (98.3.8.3).

**Open, on the list and not taken now**: keyframes over 64 KB. A 640 x 300
sixteen-colour key is 96,000 bytes of planes before any list saves a byte,
so a busy first frame is past the table's length word, and a key past
61,440 is past the player's one read - both a format and a player change.

**Taken the same day** (the owner: a key is read once, at a start or a
seek, so slower is fine): SPEC.md 98.1.3.8, KEYS IN PARTS. A key past one
read is several key records, each a band of the screen and one read of at
most 32 KB, so neither limit applies and no decoder changed - and since
key 0 is no longer bounded, a page-ahead key 0 preloads the first flip
WHOLE. Measured on the noise-then-cuts clip of this section: key 0 in
three reads (21,115, 20,908 and 31,428 bytes), preloading 100% of the first
flip where one read of 63,386 bytes had held 70% and was past the player's
read - so 11.3's fix shipped the plain file - and page-ahead KEPT with one
frame cut against the plain encode's four.
