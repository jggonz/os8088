# VIDEO-PLAN — Video Player: full-motion video on a 4.77 MHz 8088

**Status: every wave BUILT, and the owner's round of 2026-09-26 (section
14) DONE** - Repeat, W10 resident files, W9 Live, the logo video, CGA in
colour and the encoder's window; 14.9 is what the round left for later.
Revision 3, 2026-09-25, with section 8's waves updated as they land.
- Revision 1 (commit `672a72a`) planned a port of XDC's own format.
- Revision 2 (`249f4e5`) replaced that with our own format and listed sixteen
  open questions.
- This revision records the owner's answers to those questions (section 12)
  and the design that follows from them. The four questions it raised are
  answered in section 13. Nothing is open, and Wave 0 has started.

**The owner's standing decisions:**
- Not XDC-compatible. Our own format, with the goal of playing as well as
  XDC does.
- Running code read off a disk is acceptable in principle.
- ≤ ~500 bytes of kernel, and less is better.
- No `kern_small` in this plan.

How to read the numbers:
- *Measured* means decoded exactly from the five XDC streams the owner
  supplied (BADAPPLE, THUNDERC, TRONDISC, BBBB_BW, BBBBCOMP), or taken from a
  PERFORMANCE.md set.
- *Model* means arithmetic: XDC's own cycle constants (from `XDC_CODE.PAS`,
  with CGA wait states and DRAM refresh folded in) plus an estimate of our
  decoder's instructions.
- Wave 0 replaces every *model* figure with a MartyPC reading before any
  shipped byte is written.

## 0. Summary

**Video Player** (`VIDEO.O88`, playing `.V88` files) is our own format and
player. It takes three ideas from XDC (MobyGamer, MIT, © 2014 Jim Leonard):
1. **Frames are decoded inside the audio or timer interrupt.** That work fills
   the time the CPU would otherwise spend spinning in the BIOS while disk DMA
   runs.
2. **The audio clock is the frame clock.**
3. **A budgeted delta encoder.** It commits the changed byte spans largest
   first, against a CPU pool and a disk pool, and lets a starved frame
   converge over the next few.

**The format ships operands, not code.** A frame is ten skip-coded lists of
changes. The player holds the unrolled loops that apply them. XDC's
frame-programs spend 48–65% of their bytes on `mov di,<address>` (measured).

On XDC's own 640×200 content, ours is:
- **21–46% less disk.** BADAPPLE drops from 93.8 to **57.7 KB/s**, and from
  20.1 to **12.3 MB**, so it fits the owner's ST-225.
- **+2–7 points of CPU on the mean frame.** The worst frames measure **1.01–1.28×
  XDC's cycles** on MartyPC's cycle-exact 5150: BADAPPLE's worst is 66% of a
  frame against XDC's 56%. Frames of long spans are faster than XDC's code
  (Wave 0, sections 2.2 and 2.4).

**The canvas is generic.** Any byte-aligned width and any height, in any
pixel format the target mode stores one byte at a time. So a video is encoded
for the screen it will be seen on:
- 640×200 fills a CGA, and plays unchanged on a VGA through the BIOS's CGA
  mode;
- **400×200 is 4:3 on a Hercules**;
- 320×240 or 400×300 is 4:3 on a VGA's square pixels.

Each fullscreen plays at 1:1, so no pixel is scaled at playback.

**Kernel: ~240–350 of the 500 bytes** (section 4), on `kern_big` only. It
buys a caller-rated timer interrupt for machines with no card, the streaming
file read the tree keeps needing, and a one-line fence.

**SOUND.DRV** gains a one-interrupt-per-frame stream and 4-bit ADPCM. It has
797 bytes of slack inside its 7 KB claim (measured: the image is 6,371
bytes), so that should cost no heap at all.

**What the user gets:**
- **Fullscreen** on CGA, Hercules and VGA.
- **In-window**, where the desktop stays on screen, frozen, and the video
  plays in the window at full rate.
- A windowed **Preview** with a poster frame and a keyframe scrub bar.
- The goal the owner named the tour de force: **Live windowed**, a small
  video in a window while the desktop runs, decoded in the audio interrupt so
  it keeps pace even through disk reads.

## 1. Why not XDC's format

XDC's frame is a program: `mov di,imm16` followed by unrolled `movsw`/`movsb`,
`rep movsb` or `rep stosb`, between a fixed header and `pop ds / retf`. The
player far-calls it. Measured across the samples:

| | BADAPPLE | THUNDERC | BBBB_BW |
|---|---|---|---|
| XDC code bytes per frame | 1,386 | 893 | 218 |
| …of which `mov di,imm16` | 898 (**65%**) | 426 (**48%**) | 135 (**62%**) |

Most of the "code" is addresses, and the writing instructions come from a
vocabulary of about six. XDC's encoder caps every frame at 50% of the CPU,
and the samples' worst frames all sit on that cap. So on this machine the CPU
has room and the disk does not.

Per change, the two forms compare like this:

| | bytes on disk | cycles (model) |
|---|---|---|
| **XDC** 1-byte change: `mov di,addr / movsb` (4 code bytes + 1 data) | 5 | ~35 |
| **Ours** 1-byte change: 1 skip + 1 data byte, through the player's resident `lodsb / add di,ax / movsb` | **2** | ~40 |

The 8088 has no cache, only a 4-byte queue, so our loop's instruction bytes
are fetched per iteration just as XDC's are. The difference per change is one
extra bus byte plus a register add, not a dispatch.

**Rejected after measurement:**
- **Per-row groups.** BADAPPLE averages ~2 changes per touched row, so a row
  header costs as much as the changes it groups.
- **"Hop" entries.** Bridging long gaps by rewriting unchanged bytes made
  THUNDERC 105.4 KB/s, bigger than XDC's. Segments (section 2.2) replace
  them.

## 2. The format

### 2.1 The canvas

The header names the canvas:
- **Width in bytes** (so the pixel width is a multiple of 8 at 1 bpp) and
  **height**, any value up to the target surface's.
- A **row interleave**: 1 for linear rows; 2 when the rows are stored in CGA
  bank order (even rows, then odd); 4 for Hercules order. When a canvas
  matches its surface exactly, the player decodes it with no translation at
  all (section 2.3).
- A **pixel format**. The decoder never looks at it; it moves bytes. It
  decides which fullscreen mode the player sets and which surfaces may show
  the file.
- The **aspect** the encoder assumed, so the Preview can say "made for
  Hercules" when the file is played somewhere else.

**Pixel formats.** Only version 1's are built; the rest are named so the
header has room for them.

| format | bytes mean | shown on | version |
|---|---|---|---|
| **MONO1** | 1 bpp, 1 = white | every adapter | **1** |
| **CGACOMP** | 1 bpp; each 4-bit group is one of 16 composite artifact colours (160×200×16 on a composite monitor) | **CGA only** (colour burst on). Elsewhere it plays as its mono stripe pattern, which is what an RGB monitor shows on a real CGA | **1**, and it is how XDC streams import |
| CGA4 | 2 bpp, 320×200×4, CGA mode 4, byte-native | CGA, and VGA through the BIOS's mode 4 | later: the first **colour on an 8088** with an RGB monitor |
| VGA8 | 8 bpp chunky, mode 13h, byte-native | VGA, 286+ | later: colour at 8× the bytes per pixel of MONO1 |

### 2.2 A frame's video part: ten lists of segments (settled by Wave 0)

```
frame   = [len16] [y0 16] [y1 16] video audio      (SPEC.md 98.1.3)
video   = P1 P2 P3 P4 P5 P6 SLICE RUN SLICEL RUNL     ten lists, this order
list    = segment* 00
segment = count(1..127) address(16) entry × count      skip-coded
        | 80h+count(1..127)         aentry × count     absolute (P1..P6)
entry   = skip, the change          aentry = address(16), the change
P1..P6  = 1..6 bytes
SLICE   = len8 bytes  (7..255)      RUN  = len8 value  (6..255)
SLICEL  = len16 bytes (256+)        RUNL = len16 value (256+)
```

Wave 0 changed this section more than any other. Each rule below is there
because a measurement put it there (section 8, W0).

- **The addresses are the target adapter's own memory image.** A file is
  laid out for its surface **on the host**, and nothing at playback knows
  about rows. A span that is contiguous in memory is one entry however many
  rows it crosses, so a fill of the screen is one RUNL, just as XDC makes it
  one `rep stosb`.
- **`skip`** is the number of bytes from the end of the previous write in the
  segment, one byte always. For a segment's first entry it is measured from
  the segment's address.
- **A list per short length.** A change of 1–6 bytes has its own list, and
  its store is XDC's own unrolling (`movsw / movsb` for 3). Sent through
  `rep movsw` + `rep movsb`, a short span cost +40–60 cycles, all of it rep
  start-up.
- **Absolute segments.** A skip segment's set-up measured ~215 cycles, and
  half of all segments held one entry. So an isolated change is pooled into
  an absolute segment, which costs ~14 cycles more per entry and no set-up of
  its own.
  - Below 4 entries the absolute form is also the smaller one, so it wins
    both ways there.
  - Between 4 and ~20 entries it is a cycles-against-bytes trade, and the
    encoder's machine profile makes it (section 6).
- **Hidden runs.** A stretch of one value, 6 bytes or longer, inside a slice
  becomes a RUN. That is XDC's own `FindHiddenRuns`, re-applied because the
  streams do not carry it through. It is 3 bytes on disk however long the
  stretch, and `rep stosw` into RAM is ~7 cycles a byte against a copy's ~13.
- **SLICE and RUN are Duff-unrolled too**, four to a round. RUN's length and
  value load as one `lodsw`. Spans of 256 bytes and up have lists of their
  own, so the short loops test for nothing.
- **The lists are disjoint** and each is sorted by address. The order between
  lists does not matter.
- **`y0`/`y1`** is the frame's dirty band of canvas rows, a word each
  because a Hercules or VGA canvas has more than 255 rows. It is what a
  shadow copies and what Live blits (section 3).
- **Reserved list ids** leave room for later operations, first of all
  **COPY**: a block copied from elsewhere on the screen, for pans.

**The decoder is `apps/video/vdec.inc`** (it moved there from
`tests/vidbench/` in W3, unchanged).
- It is one straight-line loop per list, entered Duff-style on the segment's
  count.
- AH = 0 is an invariant, so a skip is `lodsb / add di,ax`.
- BP is added to every segment address, which is how a window or a letterbox
  places the canvas.

**What each construct costs, measured.** MartyPC's CGA 5150, cycles, writing
the screen, each construct 60–400 times in one frame (`tests/vidbench.py`'s
synthetic rows):

| construct | XDC's code | ours | |
|---|---|---|---|
| a frame's fixed cost | 309 | 1,214 | ten list heads against XDC's header |
| 1-byte change (P1) | 36.0 | 49.6 | **+13.6**: reading the skip |
| 2-byte (P2) | 50.8 | 65.2 | +14.4 |
| 3-byte (P3) | 72.0 | 89.1 | +17.1 |
| 4-byte (P4) | 88.0 | 104.8 | +16.8 |
| 6-byte (P6) | 126.0 | 146.1 | +20.1 |
| 16-byte slice | 378 | 372 | parity |
| 40-byte slice | 895 | 804 | **−10%**: `rep movsw` against `rep movsb` |
| 16-byte run | 277 | 309 | +32 |
| 40-byte run | 638 | 621 | parity |
| a skip segment's set-up | — | ~215 | why isolated changes go absolute |

Raw stores, 8,000 bytes:
- **To CGA memory:** `rep movsw` is 18.0 cycles a byte and `rep movsb` 21.6.
- **To RAM:** `rep movsw` is 13.1 and `rep movsb` 18.0. CGA's wait states
  cap a long fill at ~18 cycles a byte whichever instruction writes it.

### 2.3 Surfaces, layouts and presets

**One decoder, and the layout is decided on the host.** A file's addresses
are the memory image of the surface it was made for:
- **CGA layout:** B800, two banks, 80 bytes a row. It plays natively on a CGA
  and, through the BIOS's mode 6, on a VGA or an EGA.
- **Hercules layout:** B000, four banks, 90 bytes a row.
- **VGA mode 12h layout:** A000, linear, 80 bytes a row.

A fullscreen position or a window's content origin is BP, added to every
segment address. That requires the origin to keep the bank phase: y a
multiple of the bank count, and x on a byte.

**Wave 0 built and measured the alternative, and it is REFUSED.** It was a
translating decoder that placed a canvas on a surface of another layout
through a row table at playback:
- it cost **~480 cycles per row change**, and the frames change rows almost
  every entry (3–12× XDC's cycles on the samples);
- it forced every span to be split at its row end, and that split is what
  turned the heaviest frames into ~290 runs where XDC has one `rep stosb`.

So translation moved to the host. **`os88vid import --target herc|vga`**
re-lays an XDV out for another adapter, and the result plays natively there.

**A file on a surface it was not laid out for** decodes natively into a RAM
**shadow**, and the frame's dirty band is copied (or, in a window, blitted)
to the screen:
- **Decode stays real-time and in sync with the sound.** Only the DISPLAY
  rate drops, because a full-screen copy measured **3–5× a native decode**:
  the dirty band spans nearly the whole screen on real content.
- It is the Live window's path too (section 3.4), where the canvases are
  small.
- The Preview says the file was made for another screen and names the
  import that makes it native.

**Fullscreen modes**, all at 1:1, each canvas centred:

| adapter | mode | 4:3 presets the encoder offers | notes |
|---|---|---|---|
| CGA | `FSXM_CGA640` (mode 6) | **640×200** (full screen); **320×100** (a quarter of the data, which also fits a window) | CGACOMP turns the burst on (3D8h ← 1Ah); the 6845 and 3D8h are the app's past the mode set (§53.7) |
| Hercules | `FSXM_HERC` (720×348) | **400×200** (the fast default); **480×232**; 720×348 (full, heavy) | Hercules layout. The 6845 is never retimed (section 7) |
| VGA / EGA | `FSXM_VGA12` for square-pixel canvases; `FSXM_CGA640` for CGA canvases | **320×240** (the fast default); **400×300**; 640×480 (full, heavy) | In mode 12h, Map Mask 0Fh with write mode 0 puts a MONO1 byte into all four planes, so MONO1 is byte-native there. A CGA canvas plays through mode 6 and looks exactly as it does on a CGA |

**Data cost follows pixel count, not screen area.** Against 640×200's
128,000 pixels:

| canvas | pixels | relative data |
|---|---|---|
| 400×200 (Hercules) | 80,000 | ×0.63 |
| 320×240 (VGA) | 76,800 | ×0.60 |
| 320×100 (CGA small) | 32,000 | ×0.25 |

This is an estimate from pixel count; Wave 1 re-encodes to measure it.
**CGACOMP off a CGA** plays as its mono pattern.

### 2.4 The format against XDC, on the samples

These are XDC's 640×200 frames exactly as XDC's encoder chose them,
re-expressed in the format above (`os88vid verify`: all 11,192 frames of the
five streams decode to XDC's screen exactly). Our encoder will re-budget on
*our* costs, so its frames will sit under their cap the way XDC's do.

| stream | disk, XDC → ours | file, XDC → ours | CPU mean, XDC → ours (model) | worst frame, XDC → ours (measured) |
|---|---|---|---|---|
| BADAPPLE (mono, 30 fps, 22 kHz) | 93.8 → **57.7 KB/s (−39%)** | 20.1 → **12.3 MB** | 14.8% → 21.5% | 56.1% → 65.9% |
| THUNDERC (composite, 23.976 fps) | 92.8 → **72.9 (−21%)** | 6.9 → 5.4 | 24.4% → 26.8% | 54.5% → 57.6% |
| TRONDISC (composite, 23.976 fps) | 105.1 → **80.8 (−23%)** | 5.0 → 3.8 | 27.0% → 30.3% | 56.3% → 62.2% |
| BBBB_BW (mono, 60 fps, 8 kHz) | 42.1 → **22.9 (−46%)** | 0.3 → 0.2 | 5.9% → 9.4% | 54.8% → 52.9% |
| BBBBCOMP (composite, 60 fps) | 46.4 → **28.0 (−40%)** | 0.3 → 0.2 | 7.9% → 11.3% | — |

How the columns were taken:
- **Disk** includes the audio, identical in both. XDC's figure also carries
  its 512-byte padding per frame; ours pads once per 32 KB super-packet.
- **CPU mean** applies the measured per-construct costs of section 2.2 to
  every frame of the stream.
- **Worst frame** is a direct measurement on MartyPC's CGA 5150, as a
  percentage of the frame period. XDC's column is XDC's heaviest frame. Ours
  is the heaviest of the frames the model ranks worst for us, all measured.
- **Neither CPU column includes the audio copy.**

What the table says:
- **Frames of long spans are FASTER than XDC's code.** They measured
  0.67–0.98× on CGA and 0.48–0.90× on VGA, which MartyPC models without wait
  states.
- **Frames of many small changes are slower,** 1.2–1.6×. They are cheap
  frames either way.
- **The heaviest frames are 1.01–1.28× XDC's.**

On the owner's machine the disk binds and the CPU has room (section 3.2), so
that trade buys a **39% smaller BADAPPLE for a worst frame at two-thirds of
the machine**.

**Audio is ~38% of BADAPPLE's stream in our form**, which is why section 2.5
carries ADPCM.

### 2.5 Audio

The header names the audio format, and both are built.
- **PCM8**: 8-bit unsigned mono at the encoder's chosen rate, as XDC.
- **ADPCM4**: Creative's 4-bit ADPCM, which a DSP 2.00 or later decodes in
  hardware at **no CPU cost**. It is half PCM8's bytes, so BADAPPLE would
  drop from 57.7 to ~47 KB/s.
  - It is noisier than 8-bit PCM at the same rate. The trade is between
    ADPCM at a high rate and PCM at half that rate for the same bytes, and
    that is a listening test Wave 0 sets up.
  - The DSP's rate ceiling for ADPCM is unverified, and **MartyPC does not
    emulate ADPCM** (its `sblaster.rs` says so, and wave 0 read zero
    interrupts). It is a field item: `tests/vidsnd.py`'s bench carries the
    row.

The encoder picks per stream, and its profile says which one hits the disk
budget. **No card means silent video.** A PC-speaker path may come later for a
video small enough to leave the CPU it needs (§34.1 priced it at 36–50%).

### 2.6 The container

**SPEC.md 98.1 is the contract now** (wave 1), and it changed two things
below. **The stream has no index**: each super-packet carries the next one's
size, so a player holds nothing but the super-packet it is reading, where a
one-hour index would have been ~54 KB. **Everything the Preview reads is at
the front** (the header, the keyframe table and every keyframe), because
`READ_AT`'s cost grows with the offset. The list below is the design as it
was proposed.

It is written by the host tools only. The player checks every field it
depends on, because a truncated copy is ordinary.

- **Header** (one sector):
  - signature, version;
  - a **rendition count** (always 1 in version 1) and a table of renditions,
    each a canvas (section 2.1) with its own super-packet index and keyframe
    table. A later version can then carry several canvases in one file, and a
    player reads only the one it plays (section 13, answer A);
  - rate: `samplerate / achunk` = fps, as in XDC; audio format;
  - frame count, largest super-packet;
  - the **poster keyframe**;
  - table offsets, and a title and credits field for the Preview.
- **Super-packets** of whole frames, at most 32 KB, padded to 512 bytes once
  each. 32 KB is XDC's own read size and fits every cluster size up to
  32 KB.
- **Super-packet index:** sectors, first frame and frame count for each. The
  ring wraps only on a super-packet boundary.
- **Keyframe table** (section 2.7).

### 2.7 Keyframes and the poster

- **What a keyframe is.** The encoder simulates the stream exactly and stores
  the decoded screen after frame *k*, every **2 seconds**. It is an ordinary
  frame of lists against black, so the player needs no second decoder.
  It lives in its own region and costs disk space and **no playback
  bandwidth**.
- **Space.** The encoder prints what the keyframes took. If they come to a
  large share of the file, the interval is the knob (the owner's rule: a
  bonus feature does not get half the disk).
- **The poster** is a keyframe index in the header, chosen at encode time. It
  defaults to the first keyframe that is not ≥ 98% one value, and it can be
  overridden.
- **Seeking** snaps to the keyframe at or before the target: decode it to the
  surface, re-seed the read at its super-packet, restart the audio there. An
  exact seek (keyframe plus replay) is offered only on a paused picture.

## 3. The player

### 3.1 The engine

**The interrupt hook**, XDC's shape:
1. Copy the frame's audio chunk into the half of the double buffer the card
   just finished, with `rep movsw` (7.9% → 5.8% of a 30 fps frame at 22 kHz
   against XDC's `rep movsb`, model). ADPCM chunks are copied the same way.
2. Decode the frame's lists onto the surface.
3. Advance.

If the ring is empty, it plays silence and counts a *pause*.

**The foreground** fills the ring and polls the keyboard:
- It reads a whole super-packet at a time through `OSAPI_FILE_READ_SEQ`
  (section 4.2).
- The ring is claimed at start-up, up to ~256 KB of the 426 KB arena a
  640 KB machine has with the hard disk ticked (§50.3). A clip that fits is
  read whole before it plays.
- **Esc always stops**, whatever the frame rate. The hook runs with
  interrupts off (the owner accepts a lost second keypress), but the
  keyboard latches a scancode and the foreground sees Esc at its next poll.

**Where the interrupt comes from:**
- **With a card:** SOUND.DRV's frame stream (section 4.4).
- **Without one:** `FSXF_RATE` (section 4.1).

**At the end**, a statistics card shows pauses, CPU idle and time on the
disk, so a field run is a photograph.

### 3.2 The CPU budget — what the cap is and what sets it

XDC's encoder caps every frame at 50% of the machine. That figure was
calibrated on hardware, and the rest is left for the disk and the audio. With
decoding in the interrupt, the question is not "video or audio". It is:
**how much CPU does the foreground need to keep the disk streaming?**

- **On a DMA disk (the ST-225 on its ST11M)**, the controller moves the bytes
  while the CPU spins in the BIOS, and the interrupt uses that spin. The
  foreground needs little: issuing each transfer, walking the cursor's
  clusters, bookkeeping. Plus the DMA's own bus cycles (~4 a byte, ~5% at
  60 KB/s) and DRAM refresh, which the cost model already carries. So the
  cap there can plausibly go **well above 50%**.
- **On a disk the CPU copies**, which is likely the PicoMEM 2's path and is
  certainly XT-IDE's, every byte read is foreground CPU. The cap must be
  lower.
- **A silent video** hands the audio copy's share (~6% at 22 kHz/30 fps) back
  to the picture. A PIT interrupt is otherwise the same as the card's.

**So the cap is a property of the machine's disk path, and the encoder takes
it from a profile.** It sets two limits:
- **An average over any one second.** This is the real constraint, because
  it is what the foreground has to live beside. The default is 50%, XDC's
  proven figure, raised per profile once Wave 0 and a field run have
  measured the ceiling.
- **A per-frame ceiling, 85% by default.** One frame may spend far more than
  the average, so a scene cut is drawn in one or two frames instead of
  converging over ten, as XDC's `frameIntegrity` must. It still has to finish
  inside its own frame period.

The burst allowance is the one encoder change that should make ours look
*better* than XDC at the same average cost, and Wave 8 measures it.

### 3.3 The windowed tiers

| tier | what the user sees | how |
|---|---|---|
| **Preview** | The poster in the window; a scrub bar over the keyframes; an info panel with fps, length, KB/s, the screen the file was made for, and whether *this* machine's disk keeps up | Decode one keyframe into a RAM shadow, then `OSAPI_GFX_BLIT1` (§5.4.2) |
| **In-window** | Full-rate video with sound in the window's rect. The rest of the desktop stays on screen, frozen, with no pointer | A **same-mode** bracket (§53.7: no `fsx_mode`, nothing cleared), with the decoder's BP at the content origin when the file's layout is the desktop's, and the shadow path otherwise (section 2.3). On exit the rect is read back into the shadow for repaints. **Only canvases that fit the content area are offered**: on CGA that is 320×100, on Hercules up to 640×200 (so XDC's 640×200 plays in a window there), on VGA anything to ~624×400 |
| **Live** | A video in a movable window while the desktop runs: small canvases (e.g. 160×120, 240×180, CGA 320×100) | Section 3.4 |

### 3.4 Live windowed — the tour de force

**Two walls** stood in the way, and both come from the kernel's shape, not
from speed:
- A disk read holds `[sch_lock]`, which freezes every worker
  (UI-FREEZE-PLAN 1).
- Nothing may draw from an interrupt while the desktop is live.

**The design goes round both. Decode and display are split:**
- **Decode stays in the interrupt.** SOUND.DRV's frame stream calls the hook
  outside a bracket as well. The hook decodes into the **RAM shadow**, which
  is not the screen, so it keeps pace with the audio *through* disk reads,
  just as fullscreen does. It also ORs each frame's `y0`/`y1` into a
  pending dirty band.
- **Display is a worker.** At ≤ 18.2 Hz it takes the gfx lock and the window
  clip, and `OSAPI_GFX_BLIT1`s the pending band of the shadow (the Wire
  pattern, `apps/wire/wire.asm`). At ~5 µs a byte (PERFORMANCE.md Set 64), a
  whole 240×180 frame is ~27 ms, and a typical frame's band far less.
- **The disk is read on the UI task** (§20.6 rule 7), in `W_ONWAKE`, one track
  at a time (~8.5 KB on an ST-225) so each freeze is short.
- **During a read the picture holds** for the read's length, ~100 ms twice a
  second at ~15 KB/s. The sound does not break, and when the read ends one
  blit shows the *current* frame. It never falls behind.
- **A clip that fits in memory has no holds at all.**

**Without a card**, Live decodes in the worker from `[ticks]`. It then stops
during reads and catches up afterwards, so on such machines Live is offered
for RAM-resident clips only.

**The region must not move** while an interrupt can call into it. Every
package declares its region movable (§66.6.1.1), so Live pins it for the
length of the stream. The pin itself is section 13's question C.

## 4. What os8088 grows

**The kernel items are `kern_big` only, per the owner's decision.**

### 4.1 `FSXF_RATE` — a caller-rated IRQ0 inside a bracket *(kernel)*

- It is a third `OSAPI_FSX_RUN` flag. DX is a PIT divisor, and a far hook
  goes with it.
- `sch_isr` adds the divisor to a 16-bit accumulator. It chains the BIOS tick
  and `[ticks]` only on **carry**, so the 18.2 Hz clock stays exact at any
  rate. That is XDC's `noSoundIntCaller`. `sch_fast_on`
  (`kernel/sched.inc:486`) is exact only for 65536/N.
- The hook runs on every entry with IF = 0.
- §53.6's restore removes it like `FSXF_FASTTICK`, so a crashed program
  cannot leave the PIT fast.
- It is general: a game at an exact 35 Hz, or a tracker at its row rate, can
  use it.
- **70–110 bytes of `.text` + 8 of `.bss`, resident** (estimate).

### 4.2 `OSAPI_FILE_READ_SEQ` — the streaming read *(kernel)*

The owner notes the tree keeps needing this and keeps finding it has only
been danced around. `OSAPI_FILE_READ_AT` re-walks the directory and the
cluster chain on every call (§18.4.4). A 20 MB partition has 2 KB clusters
(§52.3), so 13 MB into a file that is ~6,600 FAT steps per call (model;
DISK-CPU-PLAN 3.1).

§18.4.4's reason for statelessness stands: a token held **in the kernel** is
destroyed by the writes a copy loop does. So the token is the **caller's**:
- **The first call** stats by name and fills a caller-owned 16-byte cursor:
  volume, directory cluster, first cluster, size, current cluster, offset,
  and the volume's mount generation.
- **Later calls** read up to 63,488 bytes of whole clusters from the cursor's
  cluster.
- **A remount or media change** answers `FERR_NAME`, and one `READ_AT`
  re-seeds.
- **The rule:** *a cursor is valid until anything writes that file.* No
  sector number is ever exposed to the caller.
- It is published as a general slot. The Audio player (§86.5) is the next
  consumer.
- **150–220 bytes of `.cold`, resident** (estimate).

### 4.3 The progress-box fence *(kernel)*

`fpg_arm` refuses only in a *foreign* mode (`kernel/fprog.inc:331`), so a
same-mode bracket, which is In-window, gets the disk box drawn over the
video. The fix is to refuse in **any** bracket. It is a bug fix in its own
right (GFX-FSX-PLAN 4.2.1). **< 10 bytes.**

### 4.4 SOUND.DRV *(driver, not kernel)*

**Superseded by what W4 built** (section 8, W4; SPEC.md 34.5.3): an
external ring the card plays in place, not a frame stream with a callback.
What follows is the design as it was costed before §34.5.2 existed.

The driver's block is fixed at 2048 bytes (`drivers/sound/sb.inc:103`) and
`sbl_isr` has no callback (§34.3). So:
- **`SND_OPENF_FRAME`.** The caller gives a block (= `achunk`, 64–4096) and a
  far callback. The driver runs auto-init DMA over a `2 × block` page-safe
  double buffer with DSP block length = `achunk`, so it raises one IRQ per
  frame. It calls the hook after the DSP acknowledge and before EOI, with
  IF = 0 and ES:DI at the half just played.
- **ADPCM4 auto-init** (section 2.5).
- **The owner's limit: no more than 1 KB of heap growth, and no loss of
  performance.** The image is 6,371 bytes in a 7,168-byte claim (measured),
  so **up to 797 bytes cost no heap at all**. The two items are estimated at
  ~250–400 bytes. If they cross 797, one more KB is claimed: still inside the
  limit, but it will be reported.
- **The fallback** is to suspend the driver (§51.11.1) and program the DSP
  from the player. That duplicates ~400 bytes of driver code and inherits
  §96.17.1's unknown-IRQ problem.

### 4.5 The budget

| item | bytes (estimate) | **measured, W2** | resident |
|---|---|---|---|
| `FSXF_RATE` | 78–118 | **~180 `.text` + 11 `.bss`** | kern_big |
| `OSAPI_FILE_READ_SEQ` | 150–220 | **252 `.cold` + 9 `.text` + 21 `.bss`** (W3 moved the buffer to DX:BX: +55 / +20) | kern_big (`.cold` is resident) |
| progress fence | < 10 | **3**, plus **7** for the nested-chain switch guard it found | both kernels |
| **kernel total** | **~240–350 of ~500** | **kern_big 483** (`.text` +199, `.bss` +32, `.cold` +252); **kern_small 26** | |

**Wave 2 came in at 408 of the owner's ~500.** It was 574 on the first
build. Two changes brought it down:
- `READ_SEQ` became a wrapper around `READ_AT`, 331 → 197 bytes: its cursor
  stands in for READ_AT's stat and walk rather than duplicating them.
- The rate hook reaches the package's dispatcher through its window record
  (`wm_pkgcall`'s own way) instead of a kernel copy of the pointer, −16.

The estimates were low for `FSXF_RATE` because the hook has to be safe, not
just called:
- it skips while nested in the ROM's chain or on a ROM's stack;
- it runs under `[sch_lock]`;
- it counts the periods it skipped.

The `.cold` bytes crossed one 512-byte cold rung (footprint +512, 45 steps
of `KERN_BUDGET` left).
| *in reserve:* hard-disk runs that cross a head, as `CYLRUN` does for floppies (§18.91.1), **if Wave 0 shows** rung 0's stop at each track end (17 sectors, §52.1) is what caps the ST-225 | ~100–150 | kern_big |
| SOUND.DRV frame stream + ADPCM | ~250–400 | inside the driver's existing 7 KB claim. **Built as an external ring + ADPCM4: 194 bytes**, 6,371 → 6,565, no heap (W4) |

`kernsize` is quoted in bytes at every wave, per CLAUDE.md's banner.

## 5. Same or better? The ledger against XDC

| | XDC | ours |
|---|---|---|
| Disk, XDC's content | 42.1–105.1 KB/s | **22.9–80.8, −21 to −46%** (measured) |
| BADAPPLE on a 20 MB ST-225 | 20.1 MB (does not fit), 93.8 KB/s | **12.3 MB, 57.7 KB/s**; ~47 with ADPCM4 |
| Decode CPU, mean | reference | **+2–7 points** (measured costs, applied to every frame) |
| Decode CPU, worst frame | reference | **1.01–1.28×** (measured); long-span frames 0.67–0.98× |
| Scene cuts | converge over several frames | drawn inside the per-frame ceiling (section 3.2) |
| Adapters | CGA (composite colour) | CGA (composite colour); Hercules and VGA with canvases made for them |
| Windowed, seek, poster | none | Preview, In-window, Live, keyframes |

**Why the extra CPU is affordable.** It is spent in the interrupt, which on a
DMA disk overlaps the transfer rather than competing with it.

**The failure to watch for** is a run of heavy frames while the disk is also
near its limit. Wave 0 plays BADAPPLE's heaviest ten seconds from the
hard-disk profile and counts pauses. The count must be zero.

## 6. The host tools — `tools/os88vid.py`

**The first user is the developer**, so version 1 is the tool that works best
for building and testing the player. The friendly tool comes after the player
works.

- **`encode`.** XDC's encoder, ported:
  1. find the changed spans;
  2. pull hidden runs out;
  3. subdivide oversize spans;
  4. shave (optional);
  5. combine close slices;
  6. commit largest first.

  Beyond XDC it adds:
  - our own cost model, taken from Wave 0;
  - a per-second disk pool;
  - section 3.2's average and per-frame CPU limits;
  - spans split at row ends;
  - any canvas, keyframes and a poster;
  - PCM8 or ADPCM4;
  - long slices as `rep movsw`.

  **Machine profiles** hold the disk rate, the CPU limits and the default
  canvas. The first two:
  - **`5150-st225`**, the default: the owner's machine, disk capped around
    64 KB/s for margin;
  - **`5150-picomem2`**: the owner's second machine, whose storage is
    effectively unlimited and much faster. It is the machine to test on
    before a stream meets the ST-225's limits, and whether its disk path
    costs the CPU is a Wave 0 question.

  Version 1 takes XDC's script format (pre-dithered BMP frames, a WAV,
  `sourcefps=` …) and PNG frames.
- **`import`.** XDV → `.V88`. It decodes XDC's frame programs exactly (the
  grammar is ~10 opcodes, and all five samples parse with none unknown).
- **`stat`, `decode --frame N --png`, `verify`**, and `--selfcheck` in the
  build.
- **Later: a friendly front end.** Any video through ffmpeg, with our own
  scaling and dithering to each preset, and composite colour matching.
- **Also later: an os8088 logo video**, the owner's end goal for a showpiece.

## 7. Not taken, and why

- **XDC's code-as-frames format.** Section 1.
- **Translating layouts at playback.** It was built and measured in Wave 0 at
  ~480 cycles per row change, and it forced row splits that multiplied the
  heaviest frames' entries. Layout is the host's job (section 2.3).
- **Per-row groups; hop entries.** Measured worse, section 1.
- **A JIT from our lists to XDC-style code.** It pays per change in the
  foreground, which does not overlap the disk wait.
- **Scaling at playback.** A canvas is encoded for its screen and played at
  1:1. The owner's rule: speed before filling pixels.
- **Retiming the Hercules 6845 into CGA's layout.** The owner's call: it
  needs real research before it goes near real hardware. The Hercules
  presets make it unnecessary.
- **Writing a live window's framebuffer directly.** It smears the pointer
  (§7.1) and breaks a straddled display (§39.14). Live blits a shadow
  instead.
- **CGA In-window at half height.** The owner found dropping rows "not really
  windowed". CGA's In-window offers only canvases that fit.
- **`kern_small`.** STRUCK (the owner, 2026-09-26): the player needs too
  many buffers for the 128KB machine, and it will not be reconsidered.

## 8. Waves

Each gate follows docs/WRITING-TESTS.md: break it on purpose and watch it go
red. A row about one package goes in `soak`.

- **W0 — measure. No shipped byte.** A bench package in `tests/vidbench/`.
  - (a) **DONE** (`tests/vidbench.py`). The list decoder against XDC's own
    frame code, on picked, model-worst and one-construct synthetic frames,
    into CGA, Hercules and VGA memory, all verified against the host's
    picture. It reshaped the format (sections 2.2 and 2.3):
    - ten lists, not four;
    - absolute segments;
    - no row splits and host-side layout, with the translating decoder
      REFUSED at ~480 cycles a row change;
    - hidden runs.

    Two emulator facts belong beside the numbers. MartyPC charges Hercules
    memory exactly what it charges CGA's, where the field measured 40–49
    cycles a word (§88.3.6). And its XT VGA has no wait states at all.
  - (b) **DONE** (`tests/viddisk.py`). `READ_AT` grows **142 ms per MB of
    offset**: 32 KB costs 220 ms at 0 MB and 1,922 ms at 12 MB, so a
    57.7 KB/s stream dies ~2 MB in. The ROM's own `int 13h` reads a track at
    237 KB/s. That was measured on XT-IDE (CPU-copied); the owner's DMA ST11M
    is a field item. Section 4.2 is confirmed.
  - (c) **DONE** (`tests/vidsnd.py`). One interrupt per frame off an
    SB 2.0: **30.01/s** at 22,050/735 and **60.02/s** at 8,040/134. The line
    was found with DSP F2h. **ADPCM4 is unanswerable here**, because MartyPC's
    Sound Blaster has no ADPCM; it is a field item, and the bench carries the
    row for it.
  - (d) **DONE**: section 2.2's raw stores.
  - (e) **DONE for a CPU-copied disk**: an interrupt burning 25/50/75% of
    each frame leaves the reader 72/47/19% of its rate. The DMA curve is a
    field item.
  - **The profile exists now**: `os8088_5150_herc_hdd_sb[_gla]` has
    Hercules, the fixed disk and an SB 2.0. Its disk is XT-IDE, not a DMA
    controller, and its comment says so.
  - **The measurement is docs/reports/VIDEO-W0-2026-09-25.md.**
  - **Still for the field** (the owner's 5150):
    - the ST-225's streaming rate once `READ_SEQ` exists;
    - the DMA ceiling curve;
    - Hercules' real wait states (MartyPC charges it exactly CGA's);
    - ADPCM4 on a real SB 2.0.
- **W1 — host tools. DONE** (SPEC.md 98, `tools/os88vid.py`,
  `tests/vidfmt.py`). `import`, `encode`, `info`, `decode`, `verify` and
  `--selfcheck`. All five samples import and verify **frame by frame
  against XDC's screen and audio**, the whole row in 16 s. `import --target
  herc|lin80` came forward from W5, because it is the same machinery as the
  encoder: BADAPPLE re-laid for Hercules and for mode 12h verifies against
  XDC too.

  | stream | `.V88` | stream rate | keyframes, share of the file | CPU (model) mean / worst |
  |---|---|---|---|---|
  | BADAPPLE | 13.3 MB | 58.2 KB/s | 110, 1.6% | 21.5% / 67.7% |
  | THUNDERC | 6.0 MB | 73.6 | 39, 4.0% | 26.8% / 58.9% |
  | TRONDISC | 4.3 MB | 81.6 | 25, 5.9% | 30.3% / 62.2% |
  | BBBB_BW | 0.19 MB | 23.2 | 4, 8.5% | 9.4% / 57.0% |
  | BBBBCOMP | 0.25 MB | 28.6 | 4, 14.7% | 11.3% / 33.8% |
  | BADAPPLE on HERC | 13.5 MB | 59.0 | 110, 1.8% | 22.0% / 68.2% |
  | BADAPPLE on LIN80 | 13.1 MB | 57.6 | 110, 1.6% | 20.8% / 67.4% |

  The CPU columns are wave 0's CGA-screen model. Keyframes every 2 s cost
  1.6–15% of a file, so the owner's rule (a bonus does not get half the
  disk) holds with room to spare. The encoder is lossless and has no budget;
  that is W8.
- **W2 — kernel. DONE** (SPEC.md 53.2.2, 12.8.5.2, 18.4.8; `tests/vidkern.py`,
  a soak row, on `os8088_5150_herc_hdd_sb_gla`). All three green, and all
  three red when broken on purpose.
  - **`FSXF_RATE` at 30.0 Hz:** 150 periods against 91 ticks, 1.6484 against
    the exact 1.6478; the BIOS's 40:6C moved with `[ticks]`. A hook that
    `sti`s and runs two periods long every 16th call was skipped and handed
    up to 3 periods at once, with none lost.
  - **The fence:** a read before the bracket armed the widget (the
    control), the same-mode bracket's door took it down, and a read inside
    did not arm it.
  - **`READ_SEQ`, 32 KB a call on the XT-IDE disk:** **226.6 ms at 0 MB and
    219.7 at 12 MB**, against `READ_AT`'s 1,922 at 12 MB. At 12 MB its 28
    `int 13h` calls went nowhere near the FAT. A seek costs one walk, the
    same 1,922 ms, once. Every byte arrived at its offset across a seek, a
    write and a delete mid-run, and the end of the file.
  - **What the gate found:**
    - The cursor needs less than the plan gave it: no first cluster, only
      the size, because a stale cursor re-seeds from the name.
    - The nested-chain guard: a `FSXF_FASTTICK` sub-tick landing inside the
      ROM's own tick handler would have switched tasks on the chain's
      private stack (SPEC.md 53.2.2). That fault predates this wave.
  - **What it leaves for the field:**
    - `READ_SEQ` streams 32 KB in ~220 ms on this CPU-copied disk, ~145
      KB/s against the controller's 237. The rest is the chain reader's
      per-cluster work, the reserve row above.
    - What the ST-225's DMA controller makes of it is the field's to say.
- **W3 — player, fullscreen CGA, silent (`FSXF_RATE`). DONE** (SPEC.md 98.3,
  `apps/video/`, `tests/vidplay.py` as two soak rows).
  - **Frame-exact:** with the ring held to 2 slots the stream wraps it, and
    at every hold the adapter equals the host's decode byte for byte, on CGA
    and on Hercules (native HERC layout, centred). The holds include one
    after each frame whose video runs into the mirror slot, found on the
    host; with the mirror copy deleted they go red.
  - **On time:** 150 frames at 30 fps in 91 ticks (ideal 91.0), no stall,
    no late period.
  - **What it found:**
    - **A deadlock in the ring rule** (SPEC.md 98.3): a super-packet's
      chunks must be released when its last frame is drawn, not when the
      next one is entered.
    - **`READ_SEQ`'s first shape could not serve a ring.** The cursor had to
      share the buffer's segment, so the buffer moved to DX:BX; that cost
      the kernel +55 `.cold` and +20 `.bss`, now 483 of the owner's ~500.
    - **Content outside a CPU budget runs LATE, and must.** A synthetic
      frame of 16 KB of slices is ~67 ms of decode against a 33 ms period.
      The player counts it and never draws a wrong picture; the encoder
      that prevents it is W8.
  - **LIN80 (mode 12h) plays on time on the XT VGA** (`vidplayvga`), but
    its picture is not read back: mode 12h is planar, and a CPU read of A000
    is one plane. That check is W5's, with the shadow path and the CGACOMP
    burst.
  - **The player is on every apps disk** (`$(APPS_TOOLS)`, 4 clusters of
    the 360KB one) and on no kern_small disk (`SMALLOMIT`: `FSXF_RATE` and
    `READ_SEQ` are kern_big's).
  - **The field hard disks** (`make vidfieldhd XDCSAMPLES=<dir>`): bootable
    VHDs for the PicoMEM machine and 86Box, a Hercules and a CGA layout
    (five streams are ~24 MB in one layout) at three geometries - 615/4/26
    plain (MartyPC's XT-IDE), 615/4/17 for the IBM/Xebec MFM card (20 MB, so
    no THUNDERC), and the SEAGATE ST11 layout (`os88hdd.py --st11`) at
    615/4/26 for an ST11R and 615/4/17 for an ST11M, the owner's own card. **A disk is only readable at the geometry and
    layout it was written with**, and the owner's 86Box found that the hard
    way: the ST11M and WD1002A-WX1 are MFM (17 sectors) and saw nothing,
    the ST11R saw a drive with no record of its own. Read off a disk that
    ST11R formatted and os8088's installer then wrote: the card keeps a
    40-byte record (`DA BE`, the geometry, "SEAGATE30M") in sectors 1-2 of
    heads 0 and 1 of cylinder 0, hides that cylinder, and hands the BIOS
    two fewer - the installer partitioned 63,726 sectors from LBA 26. The
    generated image matches that disk's record, footer, partition entry,
    MBR and VBR byte for byte. An ST11M-formatted ST-225 then read the same:
    the same record at 17 sectors with the name "SEAGATEST225", the volume
    a cylinder in, 41,667 sectors from LBA 17 (613 cylinders again), and
    the ST11M image matches its record, partition entry, MBR and BPB. MartyPC mounts only the drive types on its
    own list, so the ST11R volume is proven by booting it cut out and
    padded back to 615 cylinders: BADAPPLE plays with no stall. Each
    carries the player, the streams and the four benches - each of which now SAVES its report as a
    `.TXT` beside itself (benchlib's `bl_save`). What the benches gained
    from W1-W3: VIDBENCH three kinds a frame instead of five and the worst
    frame in thousandths of a period at 30 and 23.976 fps (its report had
    been truncating); VIDDISK the `READ_SEQ` rows, the int 13h calls one
    makes, and the SILENT PLAYER'S CEILING - `READ_SEQ` streaming inside an
    `FSXF_RATE` bracket whose 30 Hz hook holds 0-75% of every period;
    VIDSND a 50% row with interrupts ON, as the player's hook runs; VIDKERN
    a run-all for a person, with the fence's parks timed. Verified in
    MartyPC off the images themselves: on the Hercules 5150 all five
    videos draw every frame with **no stall and no late period** -
    BADAPPLE's 6,570 frames in 3,988 ticks against 3,987.2 - and every
    bench's `.TXT` reads back off the disk whole.
  - **The ceiling, on this XT-IDE (CPU-copied):** 198 KB/s with the hook
    idle, 150 / 99 / 49 at 25 / 50 / 75%, so the disk share falls exactly
    as the decode takes the CPU. Interrupts on or off make no difference
    HERE because an XT-IDE raises none; on the owner's ST11M (DMA, IRQ 5)
    that row is the question. A `READ_SEQ` of 32 KB is 219.7 ms at the
    desktop and ~161 ms inside the bracket.
  - **A player with the benches open gets a 2-slot ring** and a 60 fps
    stream then stalls (44 of 437 frames), and a second instance is refused
    for memory. Every bench keeps its claims until its window closes; the
    field README says so.
- **W4 — sound. DONE** (SPEC.md 34.5.3, 98.1.1.1, 98.3.1;
  `tests/vidsound.py` as two soak rows).
  - **The shape changed, and section 4.4 is why.** 4.4 asked for a frame
    stream - a block per frame and a far callback per interrupt. By the time
    W4 was built, SOUND.DRV already played a package's ring IN PLACE
    (§34.5.2) and could say exactly how much it had played (verb 9), so
    what was missing was smaller: a ring the hook may WRITE, since it may
    call nothing. So:
    - **SOUND.DRV** takes an external ring in the package's own `MC_DMA`
      claim, whose block interrupt reads the package's total and writes the
      consumed count back, and an ADPCM4 start (7Dh). **194 bytes**, inside
      its 7KB claim: no heap at all, against the owner's 1 KB.
    - **The player** walks the records twice - the video cursor draws, an
      audio cursor ahead of it copies each frame's audio into the ring -
      and the hook's clock is the card: the frames wholly played at its last
      block interrupt, plus the periods since, capped at a block's worth. No
      callback into a package from a driver's interrupt, and the kernel's
      hook protections (§53.2.2) stay in force.
  - **The gate**, 60 s of 22 kHz PCM8 off a fixed disk: every frame, no
    stall, **no pause**, the picture never more than 2 frames behind the
    sound, the play as long as the sound at the card's real rate, and **the
    card's captured output byte for byte the clip's sound**.
  - **What it found:**
    - **The timer at the frame rate was too coarse.** The card reports every
      93 ms, and a heavy frame plus a report's lag put the picture 3 frames
      behind the sound four times in 60 s. At twice the frame rate, none.
    - **A clock that stops at the last frame has no end.** The silent play
      ended when the stream's chain did; with sound the picture never asks
      for a frame past the last, so the end is the header's frame count.
    - **MartyPC's card had no ADPCM** (wave 0 read zero interrupts), so it
      was added (`tools/martypc/patches/06`), with DOSBox's tables - the ones
      `os88vid` encodes against. That makes the path testable here and the
      TABLES the field's: 86Box's card and a real one.
  - **ADPCM4 wants an even chunk.** BADAPPLE's 735 is odd, so its ADPCM
    version waits for wave 8's encoder; THUNDERC's, TRONDISC's and BBBB's
    convert.
  - **The owner's verdict on ADPCM4** (86Box, the field disks' `TRONDA4`
    and `BBBBA4`): no noise and no buzz, so the tables agree with a card
    that is not ours - and **noticeably worse sound** than PCM8. So it stays
    an ENCODER OPTION for videos that are on the line, and W8's profiles
    pick it only when the disk budget needs it. On MartyPC's CPU-copied disk
    TRONDISC paused once in PCM8 and not at all in ADPCM4, which is the case
    it is for.
- **W5 — surfaces. DONE** (SPEC.md 98.3.2, 98.3.3). The native Hercules
  and VGA plays and `import --target` had come forward into W1 and W3; the
  presets are W8's encoder profiles. What W5 added:
  - **The shadow path**: a file whose mode the display lacks decodes into a
    RAM image of its own layout and the dirty band is copied, re-addressed a
    row at a time, to the first screen here that holds it. A CGA file plays
    on a Hercules frame-exact in 95 ticks against 91. **What it found**: a
    shadow play must not FORGIVE frames the way a native one does - that
    made it 20% slow, because the copy is once a call and the decode is
    cheap - so the frames past a call's cap stay owed, the cap is 8, and
    while the play is behind the copy waits (at most 8 calls).
  - **The CGA composite burst**: a CGACOMP file on a real CGA clears 3D8h's
    black-and-white bit; not through an EGA's or VGA's mode 6.
  - **Mode 12h read back**: a MONO1 byte goes to all four planes, so the
    plane a debug read returns is the picture, and `vidplayvga` is now
    frame-exact at every hold instead of timing-only.
- **W6 — Preview. DONE** (SPEC.md 98.4, 98.3.4, 98.3.5). The association
  and the Open dialog came forward into W3. What W6 added:
  - **The window is the Preview**: the poster keyframe halved 2x2 with an
    ordered dither into a box, a scrub bar over the keyframes, an info
    panel (canvas, screen, fps, length, KB/s, sound, where Play starts,
    what the last play cost), and Tracker's transport pictures as four
    buttons - Open, previous key, Play, next key. It fits CGA's desktop
    band. A key step is 1.2-1.5 s on the 5150 off a floppy.
  - **SEEK came forward from W7**: Play starts at the picked keyframe -
    decoded onto the screen (or the shadow) before the ring is filled over
    it, the stream from its super-packet with *idx* records stepped over,
    the frame count and the card's clock based at *k*+1. Frame-exact at
    every hold, natively and through the shadow, with sound too.
  - **Space pauses**, with sound: SOUND.DRV's **verb 10** halts the card
    mid-block (+65 bytes, 259 of the 1 KB now spent), verb 1 resumes it,
    and the hook counts no period while paused. Not a frame drawn and not a
    byte consumed in a 2 s pause, and the play still on time without it.
  - **What it found**:
    - **ADPCM4 could not seek.** The card restarts its decoder at 80h and
      scale 0, and Creative's ADPCM never decays an error, so a seek played
      the rest of the file ~40 of 128 off centre - and a true sample alone
      left 24, the scale being wrong too. The encoder now steers the scale
      to 0 at every keyframe's frame k+1 (three samples, 0.14 ms every 2 s)
      and the keyframe record carries the sample there: a seek is exact,
      sample for sample. **This changes the file**: the field disks'
      `TRONDA4`/`BBBBA4`, made before it, still play and still seek, from
      80h, and fail `verify` until re-made.
    - **The W5 shadow was claimed at the image's size**, 16-38 KB, which a
      hostile list could write past (SPEC.md 98.1.6). It is 64 KB now, and
      claimed before the ring.
    - **A play based at a keyframe needs its clock SEEDED there**: the
      frames due are counted from the card's played bytes plus the base, and
      until the first block interrupt the count was 0 - three frames held,
      then due at once.
    - **A 638-wide window cannot be snapped** on a 640-wide screen, so its
      content sat at x = 2 and `OSAPI_GFX_BLIT1` refused the poster. 628
      wide at frame x 7 is on the byte, and the poster rounds to the
      screen's byte anyway.
  - **Play does not yet become Pause**: that is a play in the window, W7.
  - **The owner's interface round** (after testing W6 on the field disk):
    **F and Alt+Enter** go full screen PAUSED and back (SPEC.md 98.3.6), the
    card opened at the first Space so its sound starts on the frame shown;
    Alt+Enter's poll is the foreground's, so it costs no frames. **A play
    left part way leaves Play at the keyframe at or before where it got
    to**, and one played out rewinds to the start. **The layout is
    dynamic** (98.4.1): the picture at the video's own size where the
    screen has room (Hercules, for a 640 x 200 video), the buttons centred
    under the bar with an `i` for the info card - and on CGA over the dock,
    which took **16 bytes of kernel** (`OSAPI_WM_RESIZE` now honours
    `WF_KEEPH`, SPEC.md 11.93.1). **The thumb drags** (98.4.2), loading on
    the release on an 8088 and every 9 ticks mid-drag on a 286.
- **W7 — In-window. DONE** (SPEC.md 98.3.7). The play is a SESSION that
  brackets come and go on - ring, cursors, the card, and a keeper copy of
  the canvas - so it can pause back to the desktop with its frame in the box
  and resume, and swap between the window and the full screen playing or
  paused. In the window it is a same-mode bracket and the decoder writes the
  desktop's own framebuffer, in place when the file's layout is the
  desktop's (every adapter's desktop is one of the three layouts) and
  through the shadow otherwise: 92 ticks of 91.0 on the Hercules 5150. Play
  shows Pause while it plays there; with no pointer, a click anywhere
  pauses. Offered only where the picture is at its own size - on CGA a
  640 x 200 video is shown at half, so it plays full screen, as the plan's
  3.3 said. **Not taken, and now declined**: the exact seek on a paused
  picture. It would replay every frame from the keyframe - up to 2 s of
  stream, ~115 KB off the ST-225 and ~60 decodes, 2-3 s a step on the
  5150 - and the owner, asked, finds keyframe accuracy enough.
- **W8 — the encoder. W8a IS BUILT** (SPEC.md 98.2.1, `tools/os88venc.py`):
  any video ffmpeg reads, to a canvas of the source's displayed shape in a
  preset's box (the layout's pixels are not square), grey levels stretched
  to the range the clip uses, an ORDERED dither anchored to the canvas with
  a dead band so a still picture costs nothing, and then the budgets of
  3.2 - a disk bucket and a CPU bucket a second deep, a per-frame ceiling,
  the cycles MEASURED on each record and the frame retried when the model
  under-priced it - with a cut frame committing its spans best-first by
  pixels fixed per unit of the scarcer budget, aged so an old error
  outranks a new one. The owner's first three clips, on the ST-225 profile
  (PCM8 11 kHz):

  | clip | canvas | KB/s | frames exact | CPU mean / worst |
  |---|---|---|---|---|
  | Bad Apple (4:3), Hercules | 400 x 193 | 44.4 | 6,368 of 6,563 | 21.4% / 84.7% |
  | Bad Apple, CGA | 640 x 200 | 51.1 | 5,766 of 6,563 | 25.5% / 85.0% |
  | Bad Carrot (16:9), Hercules | 400 x 145 | 38.6 | 6,767 of 6,770 | 18.2% / 84.7% |
  | Bad Carrot, CGA | 640 x 150 | 45.8 | 6,532 of 6,770 | 22.4% / 84.6% |
  | Trackmania 3-15 s, Hercules | 400 x 145 | 56.5 | 359 of 360 | 27.6% / 83.0% |
  | Trackmania 3-15 s, CGA | 640 x 150 | 61.7 | 144 of 360 | 31.0% / 83.7% |

  **The budget is not what spoils a picture; one bit is.** Trackmania's
  budgeted CGA frame against the same frame with no limits differs by a
  handful of pixels: both are a mid-grey road dithered to a mid-grey
  stipple. Hercules' 400 x 145 of near-square pixels reads far better than
  CGA's 640 x 150 of tall ones. **The dither is the clip's choice**: a
  plain threshold is 6% smaller than Bayer on Bad Apple, which is black
  and white already, and blue noise 8% larger. **The owner's first look**
  (5150 and 86Box): *"very similar in quality to the originals, or even
  better"*, Trackmania with *"very little ghosting"*. It found an even grid
  of single dots over every flat black and white - the threshold map's
  extreme cells at grey 2 and 253, lit by the MP4's noise - so the ends of
  the map are solid now (`--clip`, 3,222 isolated pixels a frame to 46),
  and posters can be chosen (`--poster-at`, the keyframe nearest a moment;
  Bad Apple's and Bad Carrot's are key 4, the apple and the carrot held
  out). **W8b and W8c are BUILT.** ADPCM4 is searched rather than chosen
  a nibble at a time (SPEC.md 98.2.1): a Viterbi pass over the decoder's
  1,024 states, **~7 dB better** than the greedy encoder (27.3 against
  19.9), stitched across cores byte-identically. Composite colour comes
  from a video (98.2.2) through reenigne's model - the one MartyPC and
  86Box use - ported to `tools/os88cgacomp.py`, with Knoll's pattern
  dither over the 16 nibbles; it is expensive (115 KB/s of picture for
  640 x 150 with no limits) and plays on time under the ST-225 budget by
  cutting. **Then it was aimed at XDC's own** (the owner's two Big Buck
  Bunny XDVs, rendered through the model): composite is error-diffused
  through the model now, cell by cell against what it looks like beside
  its neighbours, with a lookahead and a dead band - 294 of 360 frames
  exact under the ST-225 budget where the pattern dither managed 42 (SPEC.md
  98.2.2). **What is left of W8 is the field's**: the profiles' figures
  (`floppy`, `picomem2` and `286` are arithmetic) and how composite
  and the searched ADPCM4 look and sound on a real monitor and card.
- **W9 — Live windowed.** The encoder's `live-*` presets (240 x 116 on
  Hercules, 320 x 100 on CGA, 160 x 120 on VGA) are its canvases, so a clip
  can be made for it before it plays.
- **W10 — ONE FILE FOR EVERY SCREEN, IN MEMORY.** The owner's end goal: an
  os8088 logo video, small, that plays Live on any adapter, carrying every
  format it needs in one file, loaded whole before it plays, each format
  compressed on its own. What it needs, and what it does NOT:
  - **The header already has the room.** 98.1.1 reserved four rendition
    slots of 64 bytes, 32 of each unused, for exactly this (13, answer A).
    Version 2 adds header flag bit 0, RESIDENT, and uses the spare bytes:
    a slot gains its BLOCK - offset, packed and unpacked bytes, and the
    packing (none, LZ4, LZB) - and the header's 16 bytes at 176 gain the
    AUDIO block. A version 1 reader refuses the flag, as it does any.
  - **A block is the rendition's records back to back** - no super-packets,
    no padding, no chain: those exist to be read a sector at a time, and a
    resident file is read once. Each record keeps its length word, so the
    decoder is unchanged.
  - **The sound is ONE block for every rendition**, not a part of every
    record: carried per record it would be stored once per format. The
    hook takes frame *f*'s audio at *f* x abytes into it.
  - **Loading copies nothing it does not have to.** The player claims the
    rendition's UNPACKED size, reads the packed block into the TOP of that
    claim, and `OSAPI_DECOMP` expands it in place: SPEC.md 20.13.7's raw
    tail is what makes a buffer of exactly the output enough, so there is
    no scratch buffer and no second copy. The decoder then reads records
    where they lie - no ring, no READ_SEQ. The one copy left is Live's
    shadow-to-screen blit, which 3.4 requires (a live desktop may not be
    written under its pointer). An unpacked block is read straight in.
  - **Each block packs with LZB where that is worth it, LZ4 otherwise.**
    MEASURED on 10 s of Bad Apple and of Trackmania at the three Live
    sizes: LZ4 keeps **83-94%** of a record stream and LZB **74-87%** -
    ten points better, at a decode that costs once, at load. **So
    compression is not what makes it small**: a dithered picture's changes
    are close to incompressible, and 10 s of Bad Apple at 160 x 120 is
    ~100 KB even packed. A logo is SMALL BY BEING MADE SMALL - flat
    shapes, few changes a frame, a low frame rate, a few seconds - and the
    encoder's job is to say how big it came out, per rendition, and to
    refuse past a stated size. `--resident --max-kb N` makes the disk
    bucket a SIZE bucket (N over the length) and keeps the CPU's, at Live's
    share of the machine rather than a fullscreen play's.
  - **A loop is a record.** With header flag bit 1, LOOP, a block carries
    one record more than it has frames: the change from the last frame
    back to the first. Looping then costs a frame like any other, where
    re-decoding the first keyframe would be a whole canvas every lap.
  - **The player picks the rendition that is native to the desktop** (no
    shadow copy), else the first that fits through the shadow, else it
    refuses with the arithmetic - the same rule as 13, answer A.
  - **The gate** encodes a fixture logo in three renditions, loads each on
    its own adapter under MartyPC, asserts the claim is exactly the
    unpacked size and nothing else was claimed, and compares every frame
    of a lap and the loop's seam with the host's decode.
- **W11 — 256 COLOURS ON A 286's VGA: mode 13h, then Mode X.** The
  owner's ask (2026-09-26): *"continue on to mode x (and 13h probably as
  an option...), assuming we have any room on 286+/vga"*. **There is room,
  and it was measured before anything was designed.**
  - **The machine.** The owner's 86Box `286-video` (mr286, 16 MHz, OTI067)
    ran the four benches: our decoder's worst frame to the screen is **193
    thousandths** of a 30 fps period against the 5150's 877, and `rep
    movsw` of 8,000 bytes takes **7.3 ms to the VGA** against 1.05 to RAM.
    So on a 286 the limit is the VGA bus, **~1.1 MB/s of stores**, not the
    CPU. The disk read ~380 KB/s off an ST11R at a 286's speed, and an IDE
    disk is faster.
  - **The stream.** Trackmania, the hard case (a moving camera: the whole
    picture changes), quantised to one 256-colour palette and delta-coded
    with a stability threshold (`scratchpad` estimate, not the encoder):
    at **320 x 180**, **235-312 KB/s at 15 fps** and **370-504 at 30**;
    the VGA stores a frame average 10-18 KB and peak 29-41 KB. So 15 fps
    fits even the ST11R outright, and 30 fps fits an IDE disk with the
    budgeted encoder cutting the peaks exactly as it does for one bit a
    pixel. 160 x 90 at 30 fps is 100-140 KB/s.
  - **13h costs the decoder NOTHING.** The format is byte-oriented and its
    addresses are the surface's own memory image (98.1.2), and 13h is
    64,000 linear bytes - inside 16 bits. So it is **layout 4, LIN320**
    (A000h, stride 320, 200 rows, one byte a pixel) plus a palette. Every
    list is already right for it: a RUN is a flat colour, a SLICE is
    pixels. **W11a**: layout 4, `PF_VGA8` with the palette's 768 bytes in
    the header's sector (the six-bit DAC values, after the rendition
    slots' room), the player setting 13h in its bracket and loading the
    DAC, and `os88venc --pixfmt vga8`: one palette for the whole clip
    (ffmpeg's `palettegen`, stats over every frame), an ordered dither in
    colour space, the same stability rule and the same budgets. It is
    **fullscreen only**: a 256-colour picture cannot be shown on a
    16-colour desktop, so the Preview's poster is the keyframe's LUMA,
    dithered to one bit through the existing halving path.
    **W11a IS BUILT** (SPEC.md 98.1.2, 98.2.3, 98.3, 98.4.4): the decoder
    did not change by a byte, as predicted. The palette rides in sector 1
    (slot +32 names it), and the player loads the DAC after the bracket's
    mode set. A VGA8 frame record is capped at 30 KB so it fits a
    super-packet, so a play from the start opens on keyframe 0, which is
    the whole picture. `vidvga8` is the gate on MartyPC's VGA XT, and it
    FAILS with the DAC load removed or the poster's luma compare flipped.
    **The IDE run** (`docs/reports/VIDEO-86BOX-286-2026-09-26.md`) read
    1,197 KB/s with nothing else running and 627 with half the period
    decoding, so `286-vga` budgets 400 KB/s. Trackmania at 320 x 150:
    15 fps holds 162 of 180 frames exact at 301 KB/s, 30 fps 295 of 360
    at 419. The full-screen mono TRACKF that smeared on the first landing
    at the old `286` profile's 150 KB/s is exact in every frame at this
    one, at 187 KB/s.
  - **Mode X is a decoder change, and it buys two things.** 320 x 240 is
    square pixels (13h's are 5:6), and the planar store has a trick a
    linear one cannot: **with Map Mask 0Fh one byte store writes four
    pixels**, so a run of one colour across a four-pixel group costs a
    quarter. **W11b**: layout 5, MODEX (A000h unchained, 80 bytes a plane
    row, 240 rows), and a frame record becomes **up to five sub-records,
    each led by its Map Mask** - 0Fh for the four-pixel groups the encoder
    found uniform, then 01h, 02h, 04h, 08h for the rest, plane by plane -
    so the decoder is the same list decoder called once per sub-record
    with one OUT before it. **Page flipping is W11c and optional**: three
    pages fit (19,200 bytes a plane), and a flip ends tearing, but each
    page is TWO frames behind, so the encoder diffs against n-2 and the
    stream grows; it is a flag the encoder sets and the player honours.
  - **W11b IS BUILT** (SPEC.md 98.1.3.1): MODEX records are sub-records,
    each a Map Mask and ten lists, and the decoder OUTs the mask and runs
    them once - `vidmodex` reads the RENDERED glass, since the planes are
    not flat memory, and FAILS with the OUT removed. On camera footage the
    0Fh stores carry only 4.4% of the pixels (the dither breaks flat areas
    up); flat-shaded pictures are where they pay.
  - **The owner's verdict on W11a** (2026-09-26, 86Box 286): the small
    30 fps cut perfect; of the two full-size ones, 30 fps with smears
    looks better than 15 fps for Trackmania - *"entirely based on feeling
    smooth"* - where film or anime may want the lower rate. The 30 fps cut
    stalled twice a second in: the encoder's disk bucket banked a second
    at 400 KB/s where the player's ring holds 64-256 KB, now capped at 96
    KB (SPEC.md 98.2.1). Asked next: 25 fps as a middle, and **a lower
    effective resolution at the same screen size** - which is W11c.
  - **W11c IS BUILT** (SPEC.md 98.2.4): `--detail WxH`, the owner's
    *"lower the effective resolution without lowering the real
    resolution"*. The width by repeating pixels, which Mode X stores two or
    four to the byte under the pair masks 03h/0Ch and 0Fh, and the height by
    the file's row scale, the CRTC showing each row twice. Trackmania in
    Mode X at 30 fps: 1x1 404 KB/s with 88 of 360 frames exact, 2x1 292 with
    all 360, 2x2 158, 4x2 89. `vidmodex2` gates it and FAILS with the CRTC
    write taken out.
  - **The thumb in the window** follows the play on every desktop now,
    written into the framebuffer by the player (SPEC.md 98.3.7) - it was a
    whole-bar kernel repaint once a second, and never on VGA, which is what
    the owner saw. `vidthumb` / `vidthumbherc`.
  - **W11d IS BUILT** (SPEC.md 98.3.8): Mode X page flipping, the
    encoder's `--flip`. Each record is decoded into the back page after
    the one it missed, and the CRTC start address is written, latched at
    the next retrace, so the hook never waits. It costs decode, not data,
    so it is optional: at detail 2x2 every frame stays exact, at 2x1 22 of
    360 are cut.
- **W12 - SIXTEEN COLOURS IN THE WINDOW. BUILT** (SPEC.md 98.1.3.2, 98.2.5,
  98.4.5). The owner's ask: *"a 12h 16 colour that can play in the imposter
  window"*. A VGA desktop IS mode 12h, so VGA4 is LIN80's bit-planes under
  98.1.3.1's sub-records - the planes that want one byte stored together,
  so black and white still costs a store per eight pixels - in the desktop's
  own sixteen, no palette loaded. The poster is in colour through
  `OSAPI_GFX_BLIT4`. The dither had to change: an ordered dither shifts the
  three channels together and made the sky and grass grey; Knoll's pattern
  dither mixes the sixteen, and a stability rule on the SOURCE brings it
  from 383 KB/s to 254 for Trackmania at 320 x 180, 30 fps, every frame
  exact. `vidvga4` plays it in the window and full screen off the glass.
  - **Known intermittent**: `vidwin` and `vidwinshd` each failed once
    under a four-lane soak at the same step - F while playing, then the
    hold at frame 100 never comes - and pass alone (3 of 3 each). It
    predates W11a. Not yet diagnosed.
  - **What the field answers**: the 86Box 286 for both modes, and the
    owner's 5150 is out of it - a VGA on an 8-bit bus at 4.77 MHz is
    wave 0's finding times four.
- **Field.** The owner's 5150 with the ST-225: BADAPPLE with sound, zero
  pauses, fullscreen and In-window. Then the PicoMEM 2 machine for the
  streams the ST-225 cannot carry.

## 9. Credit

The architecture is XDC's, and the About card says so: Jim Leonard
(Trixter / Hornet), XDC, 2014, MIT, with the Sound Blaster shell credited to
Stefan Goehler as XDC's own card does. The format and every line of code are
new.

## 10. Where the numbers came from

Throw-away scripts decoded the five samples frame by frame against XDC's
grammar and priced each candidate format. Wave 1's `os88vid.py stat` is
those scripts made permanent, and it regenerates section 2.4's table.

## 11. Relation to other plans

- DISK-CPU-PLAN 3.1 prices the chain re-walk that section 4.2 removes.
- GFX-FSX-PLAN 4.2.1 is section 4.3's gap.
- UI-FREEZE-PLAN 1 is why Live splits decode from display.

## 12. The owner's answers to revision 2's questions (2026-09-25)

| # | question | answer, and where it went |
|---|---|---|
| 1 | Names | **Video Player**, `VIDEO.O88`, `.V88` |
| 2 | Canvases | 640×200 is the start; Hercules and VGA should look less awkward, and speed matters more than filling the screen; ideally arbitrary resolutions, at least for mono → **section 2.1's generic canvas and section 2.3's presets** |
| 3 | Colour | XDC-style composite colour on CGA via a CGA-only format; mono first; VGA/RGB colour 286+ if possible → **CGACOMP in v1; CGA4 and VGA8 named for later** |
| 4 | Hercules | Prefer a smaller, less squashed canvas; do not retime the 6845 without research → **400×200 preset; retime not taken** |
| 5 | Audio | Whatever hits the speed goals; possibly both, typed in the header → **PCM8 and ADPCM4** |
| 6 | No card | Silent is fine; speaker audio maybe some day |
| 7 | SOUND.DRV | Fine if it stays within 1 KB of heap growth and is as fast → **section 4.4: 797 bytes of slack** |
| 8 | Streaming read | Yes, and it is overdue → **section 4.2, a general slot** |
| 9 | `kern_small` | Not in this plan; STRUCK outright on 2026-09-26 (section 14) |
| 10 | Windowed | CGA half-rows is not really windowed; restrict CGA to canvases that fit; Live, even small and in memory, is the tour de force → **sections 3.3 and 3.4** |
| 11 | Keyframes | 2 s; reduce if they eat the disk; the poster is a keyframe named in the header, chosen at encode, default first mostly-not-empty |
| 12 | Encoder | Easy Python with ffmpeg and our own scaling and dithering eventually; the first tool is whatever works for the developer |
| 13 | Interrupts off | Fine, as long as a video can always be stopped without a reboot → **Esc, section 3.1** |
| 14 | CPU cap | The developer's call → **section 3.2: a profile's per-second average plus a per-frame burst ceiling** |
| 15 | Samples | May be used as test content; an os8088 logo video is a later goal |
| 16 | Target | The 5150 with the ST-225; also a 5150 with a PicoMEM 2 for fast storage, and the emulators |

## 13. The owner's answers to revision 3's questions (2026-09-25)

| # | question | answer |
|---|---|---|
| A | A file on a screen it was not made for | **Play it at 1:1, centred, when it fits; refuse it when it is larger than the screen.** The header must stay able to carry **several canvases in one file**. That comes after the basics work, so version 1 reserves a rendition count (always 1) and a per-rendition table slot rather than building the feature |
| B | Live without a card | **RAM-resident clips only.** A live desktop cannot be fed from the disk without the card's interrupt, and the kernel bytes it would take to try are not worth it |
| C | Live's region pin | The developer's call. **The player pins its own region for the length of a Live stream and unpins it at close**, and the frame stream carries no pin. This keeps SOUND.DRV ignorant of packages' regions, the same shape §66.5.7.1 uses for a file read into a movable claim. W9's SPEC section fixes the mechanism |
| D | The first colour format after mono | **CGA4 first, if it can be made to work; VGA8 after.** |

**Wave 0 is started** (section 8). The owner supplied the IBM 5150 27-Oct-82
ROM for the emulator, so W0 runs on the genuine ROM as well as the GLaBIOS
twins. **The ROM image is the owner's and is never committed.**

## 14. The owner's round of 2026-09-26: what is left, and the logo

The owner took stock after wave 12 and asked for the remaining items to be
worked through **in one go, with every question asked up front**. This
section is that round: what was settled, the work list, the logo's brief,
and the questions (14.5) whose answers the round waits on.

### 14.1 Settled

- **The 286 disk at `17885e84` is good.** TRK4M (400 x 225, sixteen
  colours) plays with no visible pause or smear, and the flipped files
  start. 86Box shows no tearing either way, so whether flipping is worth it
  is a real-monitor question.
- **`vidwin`/`vidwinshd`'s intermittent** (8, W12) is, by the owner's
  experience of this suite, almost certainly a wait timed on the HOST that
  a loaded box shortens. It is converted to the guest's clock in this round
  (docs/plans/SOAK-PARALLEL.md 1).
- **`kern_small` is struck** (section 7).
- **Real hardware** (the 5150 with the ST-225, the PicoMEM 2, composite and
  ADPCM4 on a real monitor and card) waits for the owner to be home.
- **Deferred to after the round, as reminders**: an 86Box machine with a
  Sound Blaster 1.0/1.5 for the owner to test; the Hercules 6845 retime
  (section 7). **PC speaker PCM comes before SB 1.x** - the owner's
  judgement is that almost nobody outside an emulator has a 1.x card.
  What the tree already has there: SOUND.DRV's single-cycle path plays
  PCM8 on a DSP below 2.00 (never tried under the player, and it carries a
  written-down unverified bound at the half re-arm), and ADPCM4 is REFUSED
  on it, the 7Dh start being auto-init only.
- **Future, not this round**: XDC's newer features (PC speaker PCM, SB
  1.0/1.5), and an optimisation pass, speed first and bytes second.

### 14.2 The round's work

1. **Repeat.** A button with an icon that stays pressed while it is on. A
   play that reaches the end continues from the last frame to the first
   without leaving its view mode - fullscreen stays fullscreen, the window
   stays the window. A file may ask for it to be on when it loads (a header
   flag). It is how the logo loops. W10's *"a loop is a record"* is the
   seam: a file encoded for looping carries one record more than it has
   frames, the change from the last back to the first.
2. **CGA RGB.** 320 x 200 in four colours (the CGA4 of section 13, answer D,
   which VGA8 overtook), and 160 x 100 in sixteen through the 80-column
   text trick, Clear Skies being the reference (`apps/skies/`).
3. **W9, Live windowed** - an ADDITIONAL mode for a file that flags itself
   as able, not a replacement for the in-window play.
4. **W10 and the logo video** (14.3), the shipping example of both.
5. **An encoder interface**: every option exposed and explained, and
   defaults chosen from the input video and the target profile the user
   picks.

### 14.3 The logo video's brief

**The requirements**, the owner's:
- 4 to 7 seconds, **the long end if it can be afforded**;
- black and white;
- **~120 KB at most, ~100 KB better** - the whole file, every rendition;
- every adapter;
- loaded whole into memory on a 640 KB machine;
- played Live on the desktop;
- each adapter's copy LZB-packed on its own;
- repeats.

That is W10 as planned, plus the loop.

**The content, the owner's**: a DIP chip with wires running out of it.
Lit 1s and 0s travel along the wires, into the chip and out of it. The
chip starts blank, and then the os8088 logo is emblazoned on it. The
reference is the website's current header - grey right-angled traces ending
in a square pad, small 0 and 1 digits travelling along them - **for the
traces and the bits only**: the ASCII-art wordmark is not wanted.

**The style, the owner's**: *"more modern and complex than our simple logo.
Still a DIP chip, but something more akin to what you would expect in a
2000s era commercial"* than `OS8088.GIF`'s flat chip (`tools/os88logo.py`,
SPEC.md 63). So there is depth, shading, light and a reveal, not a flat
icon.

**What the budget allows, as arithmetic (not measured).** Three renditions
at ~40 KB each fill 120 KB. Seven seconds at 15 fps is 105 records, so a
record gets **~390 bytes** before LZB, which will do better on drawn
material than on the dithered footage it was measured on (W10's 74-87%)
by an amount nobody has measured. What things cost in that:
- **A 5 x 7 digit moving two pixels** changes one or two bytes on each of
  its rows: about **20-25 bytes a digit a frame**, so about fifteen in
  flight at once with nothing else moving.
- **A light sweep** across a dither-shaded chip changes every byte in its
  band: a 16-pixel band 100 rows tall is **~400 bytes, a whole frame's
  share**. It can run briefly, once or twice a lap, and not continuously.
- **Moving the camera** changes nearly the whole canvas every frame. A
  320 x 160 one-bit canvas is 6.4 KB, so a moving camera costs about a
  second's budget in every frame, and is out of reach.

So the commercial look has to come from the DRAWING, and the motion from
light:
- **The drawing**: a three-quarter perspective chip with a bevelled,
  dither-shaded package, lit pins, and a reflection or shadow on the board.
- **The motion**: the digits, pulses along the traces, a sheen that
  crosses the package, and the reveal.

The art is generated, as `os88logo.py` generates its logo: a Python
drawing rendered natively for each adapter's pixel shape, rather than a
video scaled and dithered, so every edge lands where it was meant.

### 14.4 How the loop and the reveal fit

A chip that starts blank and ends emblazoned cannot loop from its last
frame to its first without the logo vanishing every lap. The proposed
answer is a **loop-start frame in the header**. The intro - the blank chip
and the reveal - plays once. From then on the loop runs from frame *L* to
the end, and its seam record is the change from the last frame back to
frame *L*. That costs one header word, and it serves any file with an
intro. Question L1 asks the owner.

### 14.5 Questions for the owner

Each carries a recommendation, so *"the defaults"* is a full answer.

**The logo**
- **L1 - loop shape.** (a) the intro once, then loop frames L to the end
  with the logo lit (14.4), or (b) the whole video loops and the logo
  fades back out at the end. *Recommended: (a).*
- **L2 - ground.** Lit white bits on a black ground, the chip picked out by
  its highlights; or the website's dark-on-light. *Recommended: black
  ground*, for "lit", and because a mostly-black picture makes changes
  stand out.
- **L3 - the reveal.** (a) the incoming bits burn the letters in stroke by
  stroke; (b) a dither fade; (c) a light sweep that leaves the logo behind
  it. *Recommended: (a), then (c)'s sweep once over the finished logo* -
  the commercial beat.
- **L4 - size.** *Recommended: about half the screen's width*, the same
  physical size on every adapter.
- **L5 - renditions.** CGA (640 x 200), Hercules (720 x 348) and VGA
  (640 x 480). EGA's 640 x 350 is within 12% of Hercules' pixel shape.
  *Recommended: three renditions, EGA playing Hercules'*, leaving ~40 KB
  each; four would leave 30.
- **L6 - frame rate.** 15 fps gives ~15 digits in flight (14.3); 10 fps
  gives more motion per second's budget but visibly steps. Live's display
  is capped at 18.2 Hz. *Recommended: 15.*
- **L7 - sound.** *Recommended: silent*, so it plays on any machine with no
  card, which Live requires of a resident clip anyway (13, answer B).
- **L8 - where it ships and what plays it.** It is ~120 clusters at 360KB,
  so the 360KB system disk cannot carry it and `media360.img` can. Does
  anything play it by itself (the About box, a first boot), or is it a file
  in `MEDIA/`? *Recommended: `MEDIA/` on every apps disk with room and on
  `media360`, and nothing auto-plays it* until the owner has seen it.
- **L9 - the camera.** *Recommended: a still camera* (14.3). A slow push-in
  is the one move that might be afforded, over the intro only, and it would
  cost the loop's digit count.

**Repeat**
- **R1 - a file with no loop record.** Repeat still works, and the seam
  shows the first keyframe - one whole-canvas frame, and on a disk-streamed
  file a seek back to the start, which is a visible hold. *Recommended:
  allowed, with that hold*, rather than greying Repeat.
- **R2 - state.** Two header flags: LOOPREC (the seam record exists) and
  REPEAT (start with Repeat on). The button's state belongs to the window,
  and nothing is remembered across opens beyond the file's own flag.
  *Recommended: as described.*

**CGA RGB**
- **C1 - the four colours.** The encoder picks, per file, the palette of
  the three that fits the clip best - 0, 1, and mode 5's cyan/red/white,
  each at either intensity - and the background of 16, with an override.
  *Recommended.*
- **C2 - snow.** 160 x 100 is 80-column text, which snows on a genuine IBM
  CGA when written during display. Clear Skies accepts it (SPEC.md 88's
  *"ACCEPTED"*), and waiting for retrace would spend most of the write
  bandwidth. *Recommended: accept it, the same.*
- **C3 - other adapters.** A VGA runs mode 4 natively and the text trick
  with its own CRTC figures, and an EGA probably both. *Recommended: VGA
  plays both formats; EGA if it comes cheap*, otherwise it refuses.
- Both formats are fullscreen only, the CGA desktop being one-bit, and the
  poster is a grey dither as VGA8's is (98.4.4). There is nothing to ask
  there.

**Live (W9)**
- **V1 - which mode Play picks.** *Recommended: Live, when the file flags
  it and it fits memory*, with the in-window play as the alternative on
  the menu.
- **V2 - disk-fed Live.** The plan's design needs a card, and the picture
  holds for ~100 ms at each disk read. *Recommended: resident Live in this
  round* (the logo, any machine) *and disk-fed Live with a card in the
  same round*, if the owner wants the whole of W9 now.
- **V3 - colour.** A VGA4 file can play Live on a VGA desktop through
  `OSAPI_GFX_BLIT4`. *Recommended: yes*, because it is cheap.
- **V4 - several at once.** Each Live window pins its region and takes a
  worker. *Recommended: no limit beyond memory.*

**The encoder interface**
- **E1 - toolkit.** *Recommended: tkinter*, as `tools/os88proxygui.py` has
  it: the standard library, on Linux, macOS and Windows.
- **E2 - preview.** A scrubber over the ENCODED frames, as the chosen
  adapter shows them, composite model and palettes included, before
  anything is written. *Recommended: yes, as stills*, not real-time
  playback.
- **E3 - output.** *Recommended: the .V88, plus an optional "make a disk"*
  that writes a floppy or VHD in the chosen geometry with the player on it.
- **E4 - resident files.** *Recommended: yes*: multi-rendition, looping,
  resident files (the logo's kind) can be built from it too, on an
  advanced page.

### 14.6 Reminders for the end of the round

- The Sound Blaster 1.x 86Box machine.
- The Hercules retime research.
- The real-hardware checks.

All three are the owner's to pick up, and were put to the owner at the end
of the round (14.9).

### 14.8 Progress

- **Repeat is BUILT** (SPEC.md 98.1.1.2, 98.3.9): the REPEAT and LOOPREC
  flags and the loop block; `--loop-from` / `--repeat` in the encoder; the
  button, R and the menu item; the seam ARMED at the file's end by the
  reader and taken by both cursors, so a streamed file joins its laps with
  no read at the join; a file with no seam joins through keyframe 0 over a
  cleared canvas. Gates: `vidrepeat`, `vidrepeatshd`, `vidsndloop` (two
  laps with the card: the capture byte for byte), `vidmodexrk`,
  `vidmodexrs`.
- **Found on the way, fixed**: the first flipped build zeroed its 31 KB
  record copy at the keeper's 75 KB - 45 KB of heap overrun - and left the
  keeper uncleared on both pages (98.3.8). `vidmodexfl`'s clip starts on
  black now and FAILS that.
- **The vidwin intermittent** (14.1) was not host timing after all: the
  row armed its hold at frame 100 only after seeing frame 60, and a late
  poll let the play pass 100 first. It arms it before the swap now; a
  4-second stall injected there passes.

- **W10, RESIDENT files, is BUILT** (SPEC.md 98.1.7): one to four
  renditions, each one block of its records - LZB, LZ4 or stored, whichever
  is smaller - and the sound one audio block; the player takes the best
  rendition for the screen (the desktop's own layout, then a native mode,
  then the shadow), loads and expands it once at the first Play, keeps it
  while the file is open, and plays from memory with no ring and no reader.
  Gates: `vidresident` (Hercules), `vidresidentcga`, `vidresidentvga`,
  `vidsndres`. The first cut chose CGA's rendition on a VGA - a VGA has
  CGA's mode, so it read as native - and the desktop's own layout is scored
  first because of it.
- **Found**: Repeat turned off after the sound had queued the next lap
  drained that lap too; the drain now stops at the frames drawn.
  `vidprevshd`'s allowance counted one tick for a pause's two ends; 97 was
  measured before this round as after, and it allows two.

- **W9, LIVE, is BUILT** (SPEC.md 98.3.10): a resident file flagged LIVE,
  its renditions one-bit LIN80 canvases each naming its screen, plays on the
  desktop through the package's worker - a frame per PIT period owed, up to
  four a tick, decoded into the shadow and blitted into the box through the
  window's clip, the whole frame inside one gfx-lock hold. Pause, drag, F to
  the full screen and back, Esc. Gates: `vidlive`, `vidlivecga`,
  `vidlivevga`. **V2, disk-fed Live, is DROPPED** by the owner's own rule: a
  read holds the picture ~100 ms. **V3, Live in colour, is BUILT** since
  (15.2, SPEC.md 98.3.10.4).
- **Found**: `vidsndres` failed one run in two, and it was the harness:
  polled at 0.5 host s, the guest ran two of the clip's one-second laps a
  poll, so R landed a lap late. It polls at 0.02 s.

- **The logo video is BUILT and SHIPS** (SPEC.md 98.3.11): 
  `apps/video/os8088.v88`, 100,352 bytes (98,304 until the owner's test
  round put a key back at frame 0 and the far bits behind the chip), made
  by `tools/os88logovid.py`
  and committed. The CIRCUIT BOARD won over the dither (L2): against the
  50% dither the package reads as a smudge and the digits need their black
  halos to be seen at all, where on the board the black package stands out
  and the thin white traces carry the digits as the owner described; the
  generator keeps `--ground dither` for anyone who wants to look again.
  The listening copy with sound (L7) is `--sound` - 126 KB, not live
  (Live is silent), not shipped. Gates: `vidlogo`, `vidlogocga`,
  `vidlogovga`. A rendition may now name its screen in any resident file,
  not only a live one (98.1.7), which the listening copy needed: three LIN80
  renditions are one layout, and only the target tells the player which is
  whose.
- **The hard disk** carries it the way it carries the rest of the system:
  an install copies the boot floppy, so a hard disk installed from a 720KB
  or larger system disk has it.

- **CGA in colour is BUILT** (SPEC.md 98.1.3.3, 98.3.12, 98.4.6, 98.2.6):
  **CGA4**, 320 x 200 in four colours on mode 4, with ONE palette byte for
  the file (C1) that the encoder picks out of the 96 and three flags
  override; and **C160**, 160 x 100 in all sixteen on the text hack, on a
  layout of its own that always plays through the shadow because the
  screen's stride is two. Both full screen only, on a CGA or a VGA (C3) - a
  VGA has mode 4, and its text mode retimed to rows of four lines holds the
  hundred rows too - and CGA4 on an EGA as well. Snow is accepted on an
  original card (C2): nothing here waits for retrace. Gates: `vidcga4`,
  `vidcga4p`, `vidcga4vga`, `vidc160`, `vidc160vga`, each checking every
  pixel's rendered colour; `videnc` and `os88vid --selfcheck` for the host.
- **Found on the way**: mode 5's cyan-red-white set on a VGA came out as set
  0 - the BIOS was asked for set 0 and register 2 patched, where mode 5 is
  set 1 with magenta made red; a real CGA hid it behind 3D8h's
  black-and-white bit. And MartyPC's VGA draws text attribute 6 as red,
  not brown - the same file is brown on its CGA - so `vidc160vga` takes
  either for that one colour and says so.

- **The encoder's window is BUILT** (SPEC.md 98.2.8, E1-E4):
  `tools/os88vencgui.py`, tkinter, the form DERIVED from `os88venc`'s own
  parser so every option is on a tab with its help as the tooltip - which
  meant giving the seventeen options that had no help one each. "Made for"
  sets preset, format and profile in one choice; ffprobe's answer fills the
  defaults; a scrubber shows the ENCODED frames as the screen will; "make a
  floppy" writes the .V88 and VIDEO.O88 onto an image of any size. The
  advanced tab carries **`--resident` and `--live cga|herc|vga`**, new in
  the encoder for it (98.2.7): one rendition read whole, and a Live file for
  one screen. Gate: `vencgui`, with no display. Driven under Xvfb, it found
  one defect no test without a display could - the encode thread read two
  Tk variables, which Tk refuses off the main thread.
- **Found**: `vidpreview` lost its Esc one run in fifty under a loaded soak
  - sent while the bracket was still putting the desktop back. It resends.

### 14.7 The owner's answers (2026-09-26)

| # | answer |
|---|---|
| L1 | **Loop-start frame**, as 14.4 proposed: a `.MOD`-style "loop to frame x", so the logo stays once it has appeared |
| L2 | **White bits, a black package, black traces, a white logo** - on a **circuit board** ground, textured enough to show things against; try the board and the desktop's dither and keep what works. The board's traces may be thin and white and still carry white digits: the digits are an effect and need not always be readable |
| L3 | **The incoming bits burn the letters in, then a light sweep** |
| L4 | Whatever size Live allows and looks good; with a three-quarter view, **not too wide and thin** |
| L5 | **No EGA copy**. If the room is needed, one copy for Hercules, EGA and VGA is acceptable if the design survives the "warp" of their pixel shapes |
| L6 | **15 fps**, smoothness the target - got from animation choices as much as frame rate, or the desktop's rate if needed |
| L7 | **Sound, if it can be afforded**: stable and not obnoxious over and over, the loop seamless. 28 KB is a lot, so a **sound variant is made to listen to, outside the 120 KB**, and the shipped file is probably silent. No card plays it on the PIT |
| L8 | `media360.img` at 360KB, other apps disks with room, **no autoplay**. The **system disk carries it (and `VIDEO.O88`) at 720KB, 1.2MB, 1.44MB, the hard disk and the live media** and never at 360KB. An About-box link to it and to `OS8088.GIF` is a future item |
| L9 | **Still camera** |
| R1 | **Never grey Repeat.** A file with no loop record wraps by SEEKING AHEAD - reading the start of the file before the end is reached, as the stream reads any next section - so the only cost is the first keyframe drawn whole |
| R2 | Two flags; the usual case sets both |
| C1 | **One palette per file**, the encoder's best pick, with overrides. No mid-video palette flips - they flash |
| C2 | **Snow is accepted**, as on the original card |
| C3 | VGA plays the CGA formats; fullscreen only |
| V1 | **A Live-capable file plays Live and is not offered In-window**; In-window stays for everything else |
| V2 | **Disk-fed Live is built last**, and dropped if it needs kernel changes or is large: ~100 ms holds cannot look smooth |
| V3 | **Live in colour** if it can be afforded |
| V4 | No artificial limit. **XMS is a future investigation** (people boot os8088 on Pentium 4s) |
| E1-E4 | **tkinter** like the proxy's GUI, **the preview scrubber**, **save the .V88 plus an optional "make a disk"**, advanced options on **tabs** |
| - | **The logo's `.V88` is COMMITTED**, not generated by `make`: it changes rarely, and generating it would make numpy a build dependency. Its generator is committed beside it and re-run by hand |
| - | Two decisions the owner accepted: a Live file that does not fit in memory falls back to In-window, and Live with no card is resident only |

### 14.9 What the round left, and why

**Every item of 14.2 is built** (14.8). What is not, each by a decision on
the record - and section 15 says what each would NEED:
- **Live fed from the disk** - DROPPED by the owner's own rule (V2): a read
  holds the picture ~100 ms, which cannot look smooth, and the only cure is
  kernel work.
- **Live in colour** (V3) - BUILT since, 15.2.
- **Live with sound** - not built: the card's clock is a bracket's, and a
  Live play is silent (98.3.10). The logo's listening copy is not live for
  that reason.
- **The About box's link to the logo video and to OS8088.GIF** - the
  owner's future item (L8).

**Future work the owner named** (14.7): Scali's XDC additions - PCM
through the PC speaker first, then the Sound Blaster 1.0 and 1.5; an
optimisation pass, speed first and bytes second; and XMS as a place to hold
a resident file or more live windows (V4), for the machines that have it.

**The end-of-round reminders** (14.6): the Sound Blaster 1.x 86Box machine
(low priority, after PC speaker PCM); the Hercules 6845 retime research;
and the real-hardware checks - the logo on all three screens, CGA4 and
C160 on a real CGA (snow is accepted, C2), and the page-flipping files on
a real screen.

## 15. What is outstanding, and what each would need

Written at the end of the 2026-09-26 round, for whoever picks an item up.
Ordered cheapest first. None of it is started unless it says so.

### 15.1 Live with sound - BUILT (SPEC.md 98.3.10.1), shipped on by default

231 bytes of the package, `NOLIVESND=1` builds it out. Gates: `vidlivesnd`,
`vidlivesndp` (a pause), `vidlivesndl` (a seam, two laps), `vidlivesnds` (F
out and back). What building it found, beyond the list below: a Live play's
END has to be found by the worker, since the card's clock stops at the last
frame and `vp_frame` is then never asked; the sound has to DRAIN, a pass at a
time, or the last block is cut off; a paused Live session's worker must
sleep one tick and not four, or a resume starts four ticks behind a card
already playing; and a return from the full screen must repaint the box
BEFORE resuming, or the card plays on while the repaint holds the lock. A
Live play may trail its sound by up to six frames once when a UI callback
holds the lock (the gate allows it and says why); the sound is never held.

The design, as it was costed:

Medium, package-only, no kernel change. In a bracket the Sound Blaster is
the clock and the player feeds it from the bracket's rate hook; a Live play
has no bracket, so nothing feeds the card and the worker paces frames by
PIT counts. What it needs:
- **The worker feeds `SOUND.DRV`**, as Tracker's worker does (`trk_feed`,
  which takes no lock) - the precedent that a worker may.
- **The sound is already in memory**: a Live file is resident, so a frame's
  sound is copied out of the audio block (98.1.7) - no disk.
- **The card becomes the clock**: frames paced by what the card has
  consumed rather than by PIT counts, or picture and sound drift.
- **The ring holds several ticks**: the worker runs once a tick (55 ms).
- **Pause, F both ways and the seam keep the stream whole**; the bracket
  side already knows how to take over a running stream.
- **The encoder and the format stop refusing** sound on a LIVE file, and a
  gate captures the sound the way `vidsndres` does.
- **Gated by an assembly knob** so it can be built out if it costs too many
  bytes (the owner: not shipped at once if it grows the package a lot).

### 15.2 Live in colour (V3) - BUILT 2026-09-27

**BUILT - SPEC.md 98.3.10.4 is the contract, 5.4.3.6 the kernel half, and
the gate `vidlivevga4`.** What the design below got wrong is the last
bullet but one: the repack for a covered window was built as planned and
MEASURED at **1,088 ms a pass** on a 4.77 MHz VGA XT with a Disk window over
part of a 160 x 60 box - `OSAPI_GFX_BLIT4` under a region is one clipped
`gfx_hline` per colour change per row, ~0.5 ms each, with the gfx lock held,
so the desktop stood still for a second at a time. "In practice a 286" was
not a premise worth resting it on. So it stopped being package-only:
**`OSAPI_GFX_BLITP` walks the clip on request** (`DI` bit 14, 5.4.3.6),
5.4.2.7's walk shared and cut EXACTLY (a planar piece's left edge is a mask
the emitter already had), 127 resident bytes of `kern_big` and none of
`kern_small`. Covered: **51.7 ms a pass**, exact to the pixel beside the
window; uncovered 26.8 ms. The repack stays as the fallback where BLITP
refuses (a one-bit display, a straddle, off the screen's side). An encoded
clip (`--live vga --pixfmt vga4`, 264 x 200) plays at 14.7 fps with no late
frame on the same machine. The package grew 439 bytes.

The design as written:

Medium, package-only, VGA desktop only.
- A VGA4 file decodes into four bit-planes. **`OSAPI_GFX_BLITP` takes planes
  as they are** - a `rep movsb` per plane per row, the cheapest colour blit
  there is - so an uncovered window is nearly free.
- **But `GFX_BLITP` refuses under an armed clip region**, and Live clips to
  what is visible. So a covered window needs the fallback its contract names:
  repack the changed rows to packed nibbles for `OSAPI_GFX_BLIT4`. That is
  CPU per pixel, but Live colour is only on VGA, in practice a 286 or more.
- The format's check (Live is MONO1 today), `vp_canlive` and a colour
  branch in `vp_lblit`; the encoder's `--live` taking `--pixfmt vga4`.
- On a CGA or Hercules desktop a colour file stays non-Live.

### 15.3 The About box's link (L8)

Small, but a kernel-byte question. A link in the system About box that
opens `MEDIA\OS8088.V88` (and `OS8088.GIF`) through the association, as a
double-click would - greyed with the reason when the boot volume has not
got the file (the 360KB system disk). If the About box is already in an
on-demand module the link is nearly free; if it is resident, it belongs in
one (CLAUDE.md's module rule). Not yet looked at which.

### 15.4 Longer resident and Live clips - RESEARCHED 2026-09-27

The owner's ask: *"as much ram as we have that isn't other buffers we need
to run the video performantly"*. Found making the ST11R demo disk: an
ENCODED Live clip is about four seconds. What bounds it, what the machine
actually has, and what lifting it takes, measured on MartyPC's
`os8088_5150_herc_hdd_sb_gla` (the owner's 5150: 640 KB, Hercules, SB)
with the owner's own clips (build/demo, never committed).

**What bounds a resident clip today is two policy numbers, not the design.**
- `vp_pbk` refuses a block packed over 60 KB or unpacked at 128 KB or more,
  and the encoder writes to the same bounds (98.1.7).
- `vp_ldblk` makes one 16-bit `READ_AT` and one `OSAPI_DECOMP`, and
  `OSAPI_DECOMP`'s INPUT may not cross 64 KB (its output may).

Everything after the load is ALREADY size-free:
- `vp_rwalk`, `vp_rnext` and `vp_rcur` step a paragraph-plus-offset cursor,
  normalised after every record;
- `vp_raud` computes a 32-bit offset;
- the block fields are 32 bits in the file.

**What the machine has** (the heap map, `tools/heapmap.py` read through
MartyPC):

| moment | free, one run |
|---|---|
| bare desktop | 495.5 KB (arena 534 KB) |
| the player open, poster up | 445.5 KB |
| a Live clip playing full screen: keeper 64 K, picture block 48 K, sound block 46 K, SB ring 17 K (top-down) | 270.5 KB still unused |

A further ~44 KB is purgeable caches (a 32 KB directory cache and two
smaller ones) that a big claim sheds before it refuses.

**The buffers a play needs besides the clip**:
- the canvas keeper, 64 KB whenever the play goes through the shadow and
  always for Live (98.3.10);
- the SB ring, 17 KB from the top of the heap;
- a 31 KB copy of the last record when page flipping.

So on the owner's machine **about 360 KB is left for the clip**, and ~400 KB
with the caches shed.

**What a second of clip costs in RAM**, both blocks being held unpacked:

| preset | picture | sound (PCM8, 11 kHz) | seconds in 360 KB | ...silent |
|---|---|---|---|---|
| Live, Hercules, 15 fps | 11.0 KB/s | 10.8 | 16.5 | 33 |
| Live, CGA | 8.0 | 10.8 | 19 | 45 |
| Live, VGA | 14.4 | 10.8 | 14 | 25 |
| full screen Hercules, Bad Apple 30 fps | 31.0 | 10.8 | 8.6 | 11.6 |
| full screen CGA4, Trackmania | 40.9 | 10.8 | 7.0 | 8.8 |
| 13h, Trackmania 15 fps | 151.7 | 21.5 | 2.1 | 2.4 |

**Five findings decide the design:**

1. **Packing saves disk and load time, never RAM**: the block is expanded
   before it plays. Record blocks pack to 0.55-0.76 (Live) and 0.83-0.90
   (full screen) with LZ4/LZB.
2. **On a hard disk, packing makes the LOAD SLOWER.** The ST11R class reads
   ~300 KB/s under any decode share (docs/reports/VIDEO-86BOX-ST11R), while
   `OSAPI_DECOMP`'s LZ4 costs ~50 cycles an output byte (LZB ~207). So
   360 KB loads in ~1.2 s stored against ~3.8 s packed with LZ4 and ~16 s
   with LZB, on a 4.77 MHz 8088. Only a floppy gains from packing, and a
   floppy cannot hold such a clip anyway.
   **So a big block is STORED, and that needs NO format change**: packing 0
   already exists, and only the 60 KB and 128 KB bounds apply to it.
3. **For Live, the SOUND is half the RAM.** PCM8 at 11 kHz is 10.8 KB/s
   against 8-14 of picture. ADPCM4 halves it (5.4 KB/s), taking a Live
   Hercules clip from 16.5 s to ~22 s, and ADPCM4 at 5.5 kHz quarters it
   (2.7 KB/s, ~26 s). (This line first said ADPCM4 alone quartered it; it
   is half the bytes of PCM8 at the same rate. BUILT, 98.1.7.2.) Resident sound is PCM8-only
   because ADPCM4 needs a reference byte per seek (98.1.1.1). A
   one-byte-per-frame reference table beside the audio block (15 bytes a
   second) removes that; the worker's feed then plays ADPCM4 as the bracket
   already does. 5.5 kHz PCM8 would halve it with no format change at all.
4. **Live's 64 KB keeper is a bound, not a need.** It is 64 KB because a
   list's writes are not checked and can reach anywhere in ES (98.1.6). A
   RESIDENT block is walked once at load (`vp_rwalk`). If that walk also
   checked every write against the canvas, the keeper could be the canvas's
   own size - 4 KB for a Hercules Live window - which is ~60 KB (~3 s of
   Live Hercules) given back to every Live window. The cost is CPU once, at
   load.
5. **A big claim held while the window is open is a WALL** (HEAP-UNPIN-PLAN
   2.0). The blocks are claimed bottom-up and PINNED today, and a Live
   window keeps them for its whole life. At 90 KB that barely mattered; at
   360 KB it is the whole arena. Either the blocks go top-down
   (`OSAPI_MEM_CLAIM_HI`), or they get a relocation proc that moves the six
   segment words derived from them (`vp_rblk`, `vp_rablk`, `vp_rbseg`,
   `vp_rlseg`, `vp_rsseg`, and the cursor). A Live worker reads the block
   from its tick, so a move has to be one its restart declaration covers.

**What building it would take**, cheapest first:

- ~~**A. Stored blocks of any size**~~ - BUILT (SPEC.md 98.1.7.1), as
  below:
  - `vp_pbk` lets a STORED block be any size; a packed one keeps today's
    bounds.
  - `vp_ldblk`'s stored path reads in 32 KB pieces (`READ_SEQ`, which does
    not re-walk the chain per call the way `READ_AT` does, W0 (b)) straight
    into the claim, then moves it down by the block's offset into its first
    cluster with a segment-stepping move.
  - The encoder stores a block past the bounds instead of refusing it.
  - An OLD player meets such a file at `vp_pbk` and refuses it cleanly, so
    it is compatible as it is.
  - The fit is asked BEFORE the load: `OSAPI_MEM_AVAIL_MAX` after the keeper
    and the ring, so a clip that cannot fit refuses with the numbers
    ("needs 380 KB, 360 KB free"). It does not claim and fail, because a
    failed claim sheds the caches for nothing (REGION-SELF-COMPACT 5.1.1).
  - A few hundred bytes of package, estimated.
- ~~**B. The keeper cut to the canvas for resident plays**~~ - BUILT
  (SPEC.md 98.1.7.3). Finding 4's "4 KB" was right for the Live window's
  LIN80 canvas (5 KB claimed, 60 rows of 80); a banked layout's canvas
  spans its banks, so a Hercules 320 x 100 is 27 KB. The check is a parse
  at load, 33 cycles a byte, and only for a play that decodes into the
  keeper. A play onto the screen walks nothing.
- ~~**C. ADPCM4 resident sound**~~ - BUILT (SPEC.md 98.1.7.2), with the
  5.5 kHz option beside it: no format addition was needed after all - the
  key records carry the reference as a streamed file's do - and the lap
  join is EXACT, which a streamed file's is not.
- ~~**D. Where the blocks live**~~ (finding 5) - BUILT, RELOCATABLE (SPEC.md
  98.1.7.4; the owner: *"movable would be fine, but don't overly stress
  implementing this if it is not clean"*). It was clean for the blocks,
  and needed two more things to work at all:
  - `OSAPI_MEM_PARKSAFE`, since the Live worker otherwise blocks on the
    lock a claiming callback holds;
  - the poster and keeper claimed from the top, since pinned under a block
    they were its floor.

  The keeper stays pinned. It is held in ES across drawing calls, and a
  window call can claim; proving that safe is a later pass.
- **E. Packed chunks** (a block split into independently packed chunks of
  60 KB or less, loaded through a staging buffer): only for disk space,
  since finding 2 says it slows the load on a hard disk. **SET ASIDE by the
  owner, 2026-09-27** (*"we'll leave the compression/window painting for
  later"*), with the shape they named: *"Disk space is sometimes an issue -
  trying to fit the maximum on a 360k floppy. We can do the simple route
  first, and then look at 'compressed parts' or uncapping the decompressor
  like we recently did the compressor."* The simple route (A) is built; this
  is the second step, and it has two candidate shapes - chunks as above, or
  a decompressor whose output is not capped at one 60 KB block (the
  compressor's cap was lifted the same way, SPEC.md 20.13.7).
- **Beyond 640 KB**: XMS on a 286 and up (15.6's V4). The 5150 has only
  conventional memory.

**What the owner decides first:**
- **How much of the machine a LIVE window may take.** A full-screen play
  owns the machine, so "all of it" is right there. A Live window shares the
  desktop, and one taking 360 KB leaves nothing to open beside it. Options:
  - the file's own need, whatever it is;
  - a cap, a fraction of free memory;
  - the encoder's Live targets carrying a RAM budget, so a clip is made to
    fit the machine it is for.
- **Whether ADPCM4 resident sound (C) is worth its format addition**, or
  5.5 kHz PCM8 is enough.

### 15.5 Live fed from the disk (V2) - DROPPED

Large, and dropped by the owner's rule. A worker may not touch a file
(SPEC.md 20.6 rule 7), so the reads would be the UI task's on the worker's
request (FTPD's `OSAPI_WM_ONWAKE` handshake) - and `dsk_xfer` holds the
scheduler lock for every transfer (UI-FREEZE-PLAN 1), so the worker cannot
draw while one is in flight: a ~100 ms hold a read on a hard disk, more on
a floppy. The cure is a transfer that lets other tasks run through the DMA
wait (UI-FREEZE-PLAN 4), which is kernel work and ends the "nothing happens
during disk I/O" consistency model the copy code rests on. A large
read-ahead only makes the holds rarer, not invisible.

### 15.6 Future items the owner named (14.7)

- **PCM through the PC speaker** (Scali's XDC addition, first): the speaker
  has no buffer, so it is an interrupt a SAMPLE - thousands a second - run
  by the player inside its bracket, where it owns the machine; the kernel
  only grants the rate, which `FSXF_RATE` may already cover. The cost is
  CPU taken from the decode budget on a 4.77 MHz 8088, so the encoder's
  profiles want a speaker-sound variant.
- **Sound Blaster 1.0 and 1.5**: DSPs before 2.00 have no auto-init DMA, so
  `SOUND.DRV` restarts a single-cycle transfer each interrupt, and their
  ADPCM commands differ from 2.00's `7Dh`. Driver work; wants the SB 1.x
  86Box machine (14.6) to test on.
- **An optimisation pass**, speed then bytes: 15.8 has what is known.
- **XMS** (V4): a resident block, or more Live windows, in extended memory
  through `XMEM.DRV`, a frame's records moved in as needed; costs a move a
  frame and a fallback without it.

### 15.7 Loose ends

- ~~**The old field disks carry stale ADPCM4 files.**~~ Not maintained: they
  were test disks (the owner, 2026-09-26). `make vidfieldhd XDCSAMPLES=...`
  rebuilds them if they are ever wanted.
- ~~**The ST11R demo disk has no make target.**~~ - DONE, IN THE WINDOW
  (SPEC.md 98.2.12.1). The owner (2026-09-27): *"build the 'make a hard
  drive with this video' into the encoder interface ... 32mb ide, 32mb
  st11r, 20mb st11m"*. "...and make a disk of it" now offers those three
  BOOTABLE hard disks beside the four floppies, and `os88hdd.py` writes its
  own VHD footer, so a Windows machine with no MartyPC can make one.
  `vidhdmake` boots one. Found on the way: every video whose name was not
  8.3 failed "make a floppy", and it is cut to 8.3 now. And (the owner, the same
  day): *"if we don't have [a built tree], can we just make the .vhd
  formatted, but unbootable? ... we'll ship it to users to do their
  encoding"*. It does: the disk is formatted and not bootable,
  `HDD.DRV` mounts it off a floppy boot, and a VIDEO.O88 shipped beside the
  tool rides every disk it makes. Found on the way: the bootable disk had
  carried `KERNEL.SYS` twice.
- ~~**A key pressed while a bracket is tearing down can be lost.**~~ - NOT
  A CONCERN (the owner, 2026-09-27: *"they are waiting for fullscreen to
  close"*). `vidpreview` met it: an Esc sent the moment the player reports
  it is out of the bracket went with the bracket's input, one run in fifty
  under load. The harness resends. docs/plans/MARTYPC-PLAN.md 2 keeps it
  beside the parallel-load key loss, in case they are one thing.
- **MartyPC's VGA draws text attribute 6 red**, not brown (98.3.12); a real
  VGA and MartyPC's own CGA draw brown. An emulator defect, recorded so no
  one "fixes" C160's palette for it - and MOVED to
  docs/plans/MARTYPC-PLAN.md 1 (the owner, 2026-09-27: *"we'll get around to
  patching it sometime"*). It is traced there to the attribute controller's
  25 MHz text path, which folds each palette register to four bits.
- **Live drew over a window that partly covered it** (owner's test,
  Hercules). A kernel defect and not the player's: SPEC.md 11.3.4 made
  `wm_clip_rows` answer a cell that is only partly visible, and
  `gfx_blit1` took the answer at the band's full width. Fixed in the kernel
  (5.4.2.7, 231 resident bytes, a covered blit 36% FASTER), gated by
  `vidlive`'s step 4b; and the one other caller the change reached, 1bpp
  `font_run`'s per-cell path, fixed with it (11.3.4.2, 25 bytes, `runclip`).
- ~~**The encoder window was driven only under Xvfb**~~ - it runs well on
  Windows (the owner, 2026-09-26).
- **AUDIO.O88's handoff poll used `OSAPI_FILE_GOTO_Q`** - FIXED (SPEC.md
  86.11.1): it looked for APQUEUE.DAT in its own folder, so a handoff was
  never seen unless the player lived at the system root. `GOTO_QM` now, and
  an empty poll measures ~330 ms. The handoff is still `-DAP_HANDOFF`, off
  by default. Gate: `audhand`.
- ~~**The encoder's drag and drop on Windows**~~ - confirmed working (the
  owner, 2026-09-26).
- ~~**The full screen's text flickered**~~ - FIXED (SPEC.md 98.3.13.1). The
  owner's terms were to fix it if that hurt neither the play with no text
  up nor, too much, the play with it up. It hurts neither. The decode now
  goes round the box, so the text never leaves the glass. It costs nothing
  measurable with no text up (23,931 cycles a frame against 23,840 on Bad
  Apple, Hercules), and less than the flickering version with it up (median
  +31K a frame against +75K; on 13h +35K against +92K).
- **MartyPC loses a key press now and then under parallel load** -
  RECORDED, for the next full soak (the owner, 2026-09-26). The `vidfskeys`
  rows met it: a key made and broken while the Mode X flipper had the
  machine never arrived, about one run in five, four emulators at once.
  `vidfskeys`'s `press()` sends it again, up to three times, and PRINTS how
  many it resent, so the count cannot hide. `vidpreview`'s lost Esc (above)
  may be the same thing. The question for that pass is whether the harness
  (`Marty.key`'s make/break timing against a busy guest) or the emulator's
  keyboard controller drops it. A person has never been seen to meet it.

- ~~**Nothing packs the encoder for people without the tree.**~~ - DONE
  (SPEC.md 98.2.13, the owner 2026-09-27: *"a make target makes it easy to
  get to"*). `make vencbundle` makes `build/os8088-encoder.zip`, its file
  list computed from the encoder's own imports; `vencbundle` uses it
  outside the tree.
- **`vidlivesndl` fails now and then under parallel load** - RECORDED
  2026-09-27, for the same soak pass. The picture is late against the card
  at the seam: `late` 3 or 4 where the row allows 2. It was seen in the soak
  of 2026-09-27 00:09, and again in a 4-lane run of the `vid*` rows after
  98.1.7.3 landed. It passed alone twice and 4/4 at four lanes, so the keeper
  change did not cause it. It is a GUEST counter, so contention should not
  move it, and that is the question: either the harness's reads land in
  the Live worker's window, or MartyPC's Sound Blaster paces against the
  host.
- **`vidfskeysflip` fails under parallel load** - RECORDED 2026-09-27. Leg 3
  reads "the toast left the glass 2 times in 15 looks": the text box on a
  flipped Mode X play (98.3.13.1) is missing from some screen captures. The
  other session measured it at 3 of 4 and 2 of 4 runs at four lanes, on its
  base and on its change alike. Here it failed in one run of the 70 `vid*`
  rows at four lanes and passed alone twice. With page flipping the glass is
  whichever page the CRTC shows, so a look can land between a flip and the
  text reaching the new page. Whether that is the harness's look or the
  player's order of work is the question.
- **A closed window's area stayed unpainted until the next window change** -
  RECORDED 2026-09-27, found by `vidmove`, NOT DIAGNOSED, and **SET ASIDE by
  the owner the same day** (it is the "window painting" of *"we'll leave the
  compression/window painting for later"*). A player window
  (C) had played a 240 KB file, whose claim shed the caches, and so the
  save-under taken when C opened. When C closed, the part of the Disk window
  it had covered was not repainted. The UI task was idle and the lock free,
  so it was not still busy. The area stayed stale 3 guest seconds later,
  and was right after the next raise. The window manager's path when a
  closing window's save-under has been shed is the first thing to read.
  `VIDMOVE_MAP=1` shows the heap at each step.

### 15.8 The optimisation pass - TAKEN 2026-09-27

Not needed to ship: the owner's call, once it was clear what the shadow copy
is for. Speed first, then bytes, and **measured before redesigned**: profile a
play on MartyPC and quote cycles. The decoder, the shadow copy and the Live
blit are the likely heads.

**The pass is done, and `docs/reports/VIDEO-PROFILE-2026-09-27.md` is its
measurement.** The owner's brief (2026-09-27): the native path first, C160
free to be reworked, the other-adapter path in scope; kernel bytes only
by agreement; package bytes where they earn their keep. What it found, in
order of size:

1. **On `5150-st225` the DISK binds, not the CPU.** The encoder now says
   which budget cut each frame (`cut by:`): on Sonic 2, Trackmania and
   camera footage nearly every cut is the disk's, with the CPU at 25-32% on
   the mean. So on the default profile a faster decode buys picture only at
   a scene cut. **The 60 KB/s is a margin, never a measurement** - 86Box's
   ST11R streams 253 KB/s with the hook holding half of every period. The
   ST-225's own figure is the one number that would move the default most,
   and VIDDISK measures it - on the field disk (`make vidfieldhd`), or with
   nothing copied to the machine at all: `make viddisk360` is VIDDISK and
   VIDSND on a 360 KB floppy, whose W writes a 12.5 MB STREAM.DAT in C:'s
   root for R to stream back (`tests/vidbench/FIELDDISK.TXT` is the run).
   **MEASURED 2026-09-27** (docs/reports/VIDDISK-ST225-2026-09-27.md):
   104.2 KB/s with the hook holding half of every period, 110.3 idle, 86.7
   at 75% - the DMA controller nearly flat under the decode - so the
   profile is 96,000 now, ~90% of the 50% row. Trackmania's C512 goes 289
   -> 319 of 363 frames exact on it; Sonic 2's camera footage stays the
   disk's.
2. **C160 cost four times what the encoder priced** - BUILT (SPEC.md
   98.3.12.1): decoded straight onto the text screen, 85.9% -> 32.7% of a
   5150, and priced with its own fitted constants.
3. **The native decoder is at the bus**: the model is exact to 0.4% on CGA
   and a change is four bytes of code. The FRAME's fixed cost was trimmed -
   the hook's cursor stepped in place (1,765 -> 966 cycles a call) and the
   lists chained (1,787 -> 1,462) - 3,840 -> 3,040 cycles a silent frame
   outside the decode.
4. **The shadow copy's arithmetic was a quarter of the machine** - item 1
   below, BUILT as a step table rather than row tables: 84.3% -> 74.0% for a
   Hercules file on a CGA. What is left is the stores; item 2 is not taken.
5. **Live's cost is the blit** - 33-45% of a 5150 for the logo, where its
   decode is 2%. Tracking columns at playback was BUILT, MEASURED and
   REFUSED: the walk costs what it saves (15.9% against 30% on the logo,
   49.5% and the play falling behind on Bad Apple). 15.8.1 is the proposal.

#### 15.8.1 Open: the owner's to decide

- ~~**Live bands in the file.**~~ - BUILT (SPEC.md 98.1.3.4, 98.3.10.2),
  the owner (2026-09-27): *"Video Player has never released ... format
  changes are fine if they gain us something"*. A LIVE file's frame records
  carry their blit runs under flag 16 (RUNS); a pass gathers them and blits
  each. The logo's blit 45.2% -> 22.5% of a 5150 on Hercules, 32.6% -> 20.0%
  on CGA; Bad Apple Live 46.4% -> 32.1%. 3 KB more logo, 468 bytes more
  package.
- ~~**Counting the blit in Live's CPU budget.**~~ - BUILT (SPEC.md 98.2.7),
  the owner (2026-09-27): *"Option 2 on the live video defaults, but
  obviously the person encoding could override it. Maybe 60%."* A Live
  frame is charged its decode and its runs' blit at 18.2 passes a second,
  against `LIVE_AVG` = 0.60 of the machine; `--avg` overrides it.
- ~~**The per-frame ceiling (`--peak`).**~~ - BUILT as OWED TIME (SPEC.md
  98.2.1.1), the owner (2026-09-27): *"make the frame jitterwait a little
  instead of smearing on extreme cuts"*. A frame on time may run to 1.6
  periods and the next call's two share one steady ceiling, so the play is
  back on schedule a call later; the encoder simulates the hook's schedule
  to keep it so. Encoder only - no player or format change.
- ~~**The encoder's second pass**~~ - BUILT (SPEC.md 98.2.1.2 to
  98.2.1.4), the owner (2026-09-27) after the pass above found the disk,
  not the decode, binding on motion: *"Don't pay for pixels that are about
  to change"* - a cut frame ranks its changes over the next two targets
  too, and one the picture is about to undo is not sent (error as seen
  -12 to -20%, flicker back -10 to -53%, on four clips); the error as SEEN
  - a dither pattern swapped for another of its grey counts a quarter (a
  further 1-8%; the first form amplified edges and measured worse); a disk
  RESERVE of 192 KB in the player's 8-slot ring, the ring named in the
  header and the disk refilled at its MEASURED rate under each frame's load
  (the flat rate stalled a deep reserve 19 times in 20 s on MartyPC, the
  curve and a one-read floor 0); and `--aim quality` / `--aim size`. The
  report's `picture:` line is how every one of them was judged.
- ~~**The ring's mirror copy**~~ - TAKEN, it needed no decision: `vp_mneed`
  walks the chain in memory to the super-packet that runs on into slot 0
  and mirrors what it runs on, none when nothing does. 1.8% -> 1.2% of the
  machine streaming off XT-IDE at *K* = 8, and the same third at any *K*.

**The shadow copy is ADDRESS TRANSLATION, not shape** (asked 2026-09-26: is
"slow, right shape" against "fast, wrong shape" an option to offer?). It is
not. A V88 frame is a list of byte addresses in its layout's framebuffer
(98.1.2), so what a file is "made for" is a MEMORY LAYOUT: CGA's two
interleaved banks at 80 a row, Hercules' four at 90, VGA's flat 80. The
pixels are 1 bpp everywhere. `vp_blit` copies the changed rows 1:1, each
re-addressed, and rescales nothing. So a CGA file on a Hercules is already
the wrong shape: at roughly 65% of its intended height, CGA's pixels being
about 2.4 times taller than wide and Hercules' about 1.55. A CGA file full
screen on a VGA takes no copy at all: the VGA has mode 6 and decodes
straight into it. A "right shape" option would mean scaling rows, and would
be SLOWER. What there is to win is the translation's cost, two ways, neither
measured yet:

1. **A cheaper copy** - BUILT (98.3.2): the band's first row placed by the
   formula and each row after it a STEP out of a four-entry table a side,
   since every layout's banks divide four. `vp_rowaddr` had been 25.3% of the
   machine and the copy is now the stores. What it said before:
   Each row now calls `vp_rowaddr` twice, for the
   source and the destination, and each call is a `mul` and a bank loop.
   Then it runs a `rep movsw` of the row. The whole copy measured ~60 ms for
   a full 640 x 200 band onto a Hercules (98.3.2). Row-address tables, built
   once per play for the file's layout and the screen's, take the arithmetic
   out of the loop and leave only the stores, which cannot get cheaper.
   Small in bytes. The share it removes is a guess (a quarter to a third)
   until the copy is split in a profile: arithmetic against stores.
2. **No copy: decode straight onto the other layout** - NOT TAKEN; the
   stores are what is left (49% of the machine for a Hercules file on a
   CGA), but C160, the one layout where this applied, took it (98.3.12.1).
   A second inner loop
   for the decoder that translates each span's address as it writes it.
   It has to SPLIT a span at a row's end, because the encoder merges spans
   across row ends and on another layout those bytes are not adjacent. That
   removes the copy and the 64 KB shadow claim, and charges every span a
   translation. It wins most where a frame changes most of the screen and
   little where it changes a corner. It is the decoder's hot path, so it
   wants both loops kept in step, and a gate that plays every layout onto
   every screen. A few hundred bytes of package, estimated.

Profile first; take 1 if the arithmetic is a real share, and 2 only if the
stores still dominate a full-screen play after it.

**And one kernel candidate from the same round**: `font_run_cell`'s column
mask (SPEC.md 11.3.4.2) costs a WHOLE cell in a cut run +210 cycles, one
compare and an untaken branch a row. A second copy of the row loop for the
masked case would take that to ~0 for about 20 bytes. Not taken, since the
path is only covered text; recorded because the owner asked about the column
mask's cost.

### 15.9 PC speaker PCM - BUILT 2026-09-27

The owner's brief:
> *PC Speaker PCM playback. This should be generic, not just for video
> player. If it is going to cost more than ~400b in the kernel then it can
> be a library rather than a call, used per app like our UI libraries.
> Audio, Tracker and Video Player would be the current consumers I think?
> ... This costs Video Player CPU - so it should be an option, and accounted
> for in the encoder when calculating its targets - so a video could be
> encoded for "PC speaker PCM" as its intended target and perform well. And
> on videos not targeted to that the user should be able to disable it and
> choose performant silence.*

The compression and window-painting items were set aside by the same message
and are still open: 15.4 E and 15.7's unpainted closed window, each marked
there, and both in 15.10's list.

**What was built**:
- **A kernel door, `OSAPI_FSX_SPK`** (SPEC.md 34.11.1), costing **314 bytes
  on `kern_big` and 11 on `kern_small`**. A player written wholly in the
  kernel measured 554, over the owner's line.
- **A library, `apps/os88spk.inc`** (34.11.2): the sample ISR, the count
  table, and §34.5.3's ring layout, so the player feeds the speaker exactly
  as it feeds a Sound Blaster.
- **The player on it** (98.3.15). S chooses silence in the window, and in
  the full screen it turns the speaker off at once.
- **The encoder's `--audio speaker`** (98.2.15), which budgets every frame
  around what the pulses leave.

**What it measured** (34.11.4):
- **~400 cycles a pulse**: 343 in the ISR plus the interrupt acknowledge.
  That is **54% of a 5150 at 5,512 Hz**.
- **The first build lost 9.0% of its pulses**, because long stretches ran
  at IF = 0. Each loss was traced to what it had interrupted, and four
  changes (34.11.3) took it to **1.8% in the window and 2.3% in the full
  screen**.
- **11,025 Hz on an 8088 played a 4 s clip in 20 s**, so the player mutes
  above 8,000 Hz on an 8086-class CPU and says so on the info line.

**Then XDC's fork was read** (Scalibq/XDC, 2026-09-27) and one of its ideas
taken: a file made for the speaker carries the COUNTS (SPEC.md 98.1.1.3),
so the player copies where it translated - `vp_aput` 92.8 -> 27.7 cycles a
byte, ~7% of the machine, and the speaker's share 54% -> 48% in the
encoder's budget. The other, the 8259 in auto-EOI for the play, was built
as an experiment, MEASURED and REFUSED (SPEC.md 34.11.6): 16 cycles a pulse
saved, but more pulses lost (1.8% -> 2.5%) because they now enter the ROM's
own tick handler, onto the 128-byte chain stack, on a controller
reprogrammed for everyone.

**Open, and each is its own item**:
- **Tracker's full screen** could adopt the library - but it is a
  KEEPWORKER|FASTTICK bracket and not a rate one (this line said otherwise,
  wrongly, until the handoff was written), so it needs KEEPWORKER|RATE, a
  hook, counts from its mixer and a rate row under 8,000 Hz.
- **Audio** plays on the desktop, where channel 0 is not its (34.1), so it
  would need a full-screen play first. (ModPlug was in this line once; it is
  RETIRED, SPEC.md 56.15, and needs nothing.) **Tracker and Audio are a
  handoff to another session: docs/plans/SPEAKER-PCM-HANDOFF.md.**
- **No C binding**: a C package would need an assembly module for the ISR.
- ~~**ADPCM4 on the speaker**~~ - SET ASIDE, LIKELY PERMANENTLY (the
  owner, 2026-09-27: *"That would leave almost no room at all for video"*):
  the player would decode it in the hook,
  another ~15% of the machine at 5,512 Hz. Not built.
- **Live stays silent without a card**, for the same reason as Audio.
- **The last ~one pulse a period** is the period's own entry: the grant, the
  jump and `sch_isr`'s prologue, ~1,300 cycles. Getting under 864 would mean
  a cheaper grant at the period boundary. It is worth measuring by ear on
  the 5150 before anyone builds it.
- **The field reading**: every figure here is MartyPC's 5150. The speaker's
  sound on the owner's machine is the check nothing here can make.

### 15.10 The open list (2026-09-27)

Everything still open, in one place, so the next session needs this file
and not a transcript. Each line names where the detail is.

**Set aside by the owner** - not to be started without asking:
- **Compression** (15.4 E): more clip on a 360 KB floppy, by packed chunks or
  an uncapped decompressor. *"Look at later."*
- **Window painting** (15.7): a closed player window's area left unpainted
  when its save-under had been shed. Recorded, not diagnosed.
- **ADPCM4 on the speaker** (15.9): set aside, likely permanently - it
  leaves no room for video.
- **Live fed from the disk** (15.5): DROPPED by the owner's rule.

**Next, and in this order:**
- Nothing picked. **Live in colour** (15.2) is BUILT (2026-09-27).

**Features not started:**
- **The About box link** (15.3): a kernel-byte question first - is the
  About box resident or in a module?
- **Sound Blaster 1.0 and 1.5** (15.6): SOUND.DRV work, wants an SB 1.x
  86Box machine.
- **XMS** (15.6, V4): 286 and up; the 5150 cannot use it.
- **The keeper relocatable** (15.4 D): the blocks move, the keeper stays
  pinned until its use across window calls is proven safe.

**PC speaker follow-ons** (15.9):
- **Tracker's full screen and Audio**: a HANDOFF to another session,
  docs/plans/SPEAKER-PCM-HANDOFF.md. Audio needs a full-screen play first.
- **A C binding**: none; a C package would need an assembly module.
- **Live without a card stays silent**: the desktop cannot give up
  channel 0.
- **The last ~one pulse a period** (1.8% lost): the period's own entry.
  Listen on the 5150 before building anything.
- **A field listen on the 5150**: every speaker figure is MartyPC's.

**Optimisation, not taken** (15.8.1): decoding straight onto another
layout (no shadow copy); `font_run_cell`'s masked row loop (~20 kernel
bytes, ~210 cycles a clipped cell).

**Recorded, not player defects** (15.7): `vidlivesndl`, `vidfskeysflip` and
(once) `vidplay` fail now and then under parallel load and pass alone;
MartyPC loses a key press under the same load; MartyPC's VGA draws text
attribute 6 red (MARTYPC-PLAN 1).

**One report that did not reproduce** (2026-09-27): the owner saw the
player use the Sound Blaster with the Control Panel on PC Speaker. After a
reboot it played the speaker and the info line said so; the route set
before the player was opened, SOUND.DRV and HDD.DRV loaded. Rows
`vidspkroute` and `vidspkcp` cover both ways of choosing the route. Set
aside unless it happens again - the info line's third row (`, speaker` or
not) is the first thing to ask for.
