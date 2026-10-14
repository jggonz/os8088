# Banking only the overage — what it would buy over the bank we have

**Status: THE LAYER IS BUILT (2026-10-09, SPEC.md 98.1.9; section 8).**
Measured first (section 7: it recovers 64-80% of the gap to a file made for
the better machine, and on a 286 the CPU binds it past ~350 KB/s), then
built end to end - format, encoder, player - with the layer read at the
ring-full moment as 7.3 proposed. Two findings on the way are built too:
the encoder's charge for the bank's copies (3.2) and a sign bug in its
retry (7.2). The bank-only overage of sections 4-5 is not built: the
layer is what it became. It follows docs/plans/VIDEO-XMS-PLAN.md, whose
waves 1-4 and E1-E3 are built (SPEC.md 98.3.18.2-98.3.18.7).

**The owner's question** (paraphrased): instead of cutting, put only the
OVERAGE - the bytes a disk at 448 KB/s and a 192 KB ring cannot carry - in
XMS or EMS. Load the early overages in the prefill, load later ones while
the disk has time to spare, and spend them as the bursts come.

## 1. The answer in five lines

1. **In BYTES it carries nothing the bank does not already carry.** A single
   sequential disk read in decode order with a bounded buffer is already the
   optimum (section 2). What a FIFO bank holds and what an overage store
   would hold are the same number of bytes at every instant.
2. **What it changes is which bytes pay for passing through the bank** - and
   on a 286's XMS that is real money: every byte that passes costs two
   `int 15h` copies with interrupts off. A FIFO bank passes EVERY byte while
   it holds anything; an overage store passes only the overage.
3. **Half of that was a modelling error, and it is FIXED.** The encoder
   charged the bank's copies on every byte of the play; the player only
   copies while the bank holds something. Charged as the player behaves, the
   same 3 MB bank filled first goes from **3.27% to 2.95%** error as seen,
   and a bank barely prefilled stops being WORSE than no bank (section 3).
4. **On EMS in place (98.3.18.7) there is no copy to save**, so the overage
   format buys nothing there but its fallback.
5. **What the format would buy that nothing else can is the FALLBACK**: a
   base stream any machine plays without a stall, and an enhancement a
   machine with a bank adds. Today a file made `--bank` plays on a machine
   without one and PAUSES in its bursts. That, and the 286's remaining copy
   traffic, are the case for building it (section 4), and it is several
   major changes.

## 2. Why the bank is already the optimum in bytes

Let D(t) be the bytes the disk has read by time t and S(t) the bytes the
decoder has consumed. Whatever ORDER bytes are read in, the bytes held in
memory are D(t) - S(t), and memory bounds it: D - S <= ring + bank. The
disk bounds D' <= its rate. A play does not stall while D(t) >= S(t) for the
bytes each frame needs. Reading in decode order is earliest-deadline-first,
and for a single resource with a bounded buffer EDF meets every deadline any
order can. So moving the overage elsewhere in the file - or into a region of
its own - cannot let a given disk and a given bank carry one more byte.

The encoder's `--bank` (98.2.1.3.2) is exactly that model: a token bucket of
depth ring + bank, refilled at the disk's rate, started fuller by the
prefill, and a frame is cut only when the bucket cannot pay for it. "Push the
overage into the bigger RAM instead of cutting" is what the deeper bucket
does: a byte is cut only when no amount of read-ahead within that RAM could
have brought it in time.

**Two consequences worth keeping:**
- **The prefill is the only lever on a clip with no calm stretches.** If the
  disk never outruns the stream during the play, nothing refills the bank,
  and what the bank holds at the start is all it ever gives (section 3.1).
- **The ideal prefill is computable.** The least initial level that leaves
  the bucket above zero is the largest cumulative deficit (want - disk)
  over any prefix. The encoder could write THAT as the file's prefill
  (`--prefill auto`, section 5) instead of the owner's default of 10 s.

## 3. Measured: Last Exile's opening, 30 s of it

`--preset vga8 --profile 286-pvga --disk 204800 --start 20 --end 50`: the
owner's 286 with a disk at 200 KB/s. "Error" is the encoder's error as
seen; "cut" the frames the disk cut.

### 3.1 The bank's whole benefit here is its prefill

With the bank 3 MB and only the prefill varied (the encoder as it shipped
in 98.2.1.3.2):

| prefill | cut by the disk | error |
|---|---|---|
| no bank | 611 | 3.81% |
| 0.5 s | 624 | **4.22%** |
| 3 s | 586 | 3.99% |
| 6 s | 524 | 3.79% |
| 9 s | 451 | 3.65% |
| 12 s | 398 | 3.49% |
| all (15 s) | 348 | 3.27% |

Linear: about 0.2 points of error per 3 s of prefill, because this clip has
no calm for the bank to refill in. And at 0.5 s the bank was WORSE than none,
which is section 3.2.

### 3.2 The encoder charged the copies on every byte - FIXED

98.2.1.3.2 charged the disk `1 + 2 x xcopy x rate` for the whole play: 12%
of a 200 KB/s disk on a 286. The player's FIFO hands the ring its chunks
through the bank only WHILE THE BANK HOLDS SOMETHING; empty, the disk reads
into the ring and nothing is copied. A bank that is prefilled a little and
never refilled is empty for nearly the whole play, so it was charged 12% for
copies that never happen. Now the charge applies only on frames where the
bucket stands above the ring's reserve - the bank's share of it - and the
same table (`os88venc.py`'s `Encoder._drate`):

| prefill | cut by the disk | error, before | error, now |
|---|---|---|---|
| no bank | 611 | 3.81% | 3.81% (byte-identical file) |
| 0.5 s | 605 | 4.22% | **3.75%** |
| 6 s | 509 | 3.79% | **3.37%** |
| all (15 s) | 338 | 3.27% | **2.95%** |

The no-bank file is byte-identical with the code before the change, and the
encoder is byte-identical run to run.

### 3.3 What the overage format would save on top

With the model fixed, the FIFO's remaining cost is that once the bank holds
anything, EVERY byte goes through it, base and overage alike: VIDEO-XMS-PLAN's
opening (its point 3) measured 87-98% of a busy clip's bytes banked. An overage
store copies only the overage. On this clip with 3 MB filled first the
overage is ~2.1 MB of a 10.4 MB play (the 'all' encode's bytes less the
no-bank encode's), so the copies fall to about a FIFTH:

| | bytes through XMS | copy time at 0.69 ms/KB (two copies) | interrupts off |
|---|---|---|---|
| FIFO bank, filled first | ~10.4 MB (nearly all) | ~7.0 s of 30 | ~23% of the play |
| overage store | ~2.1 MB | ~1.4 s of 30 | ~5% |

ESTIMATED, not measured: the bytes are the two encodes', the 0.69 ms/KB the
owner's 286 (VIDDISK `X`, 0.345 a copy). What the 5.6 s buys is disk time -
about 1.1 MB more at 200 KB/s, roughly another 5 s of prefill's worth by
section 3.1's slope - and it is also the speaker's clock (98.3.18.5, where
every copy loses samples) and the hook's latency. On EMS in place the FIFO
already copies nothing, so none of this applies there.

## 4. The design: a base stream and an enhancement

**The shape that makes it worth building is not "move the overage" but
LAYERS**, because a layered file is what gives the fallback:

- **The base stream** is the file's stream as today, encoded to the disk and
  the ring alone - what an encode with no `--bank` makes. Every player plays
  it, with no stall, at today's quality.
- **The enhancement** is a second region of the file: for each frame, the
  writes that take the screen from what the BASE leaves to what the full
  encode wanted. It is read into the bank - by the prefill first, then while
  the disk has time - and the hook applies a frame's enhancement records
  after its base record.
- **It is not just the cut bytes.** Base records are writes of new values,
  and the base encoder leaves a byte alone when ITS screen already holds the
  next value. An enhanced screen can differ there, so the enhancement must
  carry every byte where (enhanced screen + base record) differs from the
  frame - the encoder tracks the two screens, base and enhanced, and emits
  the difference. ESTIMATE: the cut bytes plus a share of divergence that a
  first build must measure; it could be large on noisy content.
- **Losing it degrades rather than breaks.** An enhancement missing (a bank
  short, a seek, a slow disk) means the play continues on the base from an
  ENHANCED screen, and the base's later writes repair it the way a cut frame
  is repaired today. A keyframe resynchronises both screens.

### 4.1 What it costs

| piece | where | the work |
|---|---|---|
| the region and its index | format (98.1) | a block per keyframe interval, located by a table like the keys'; older players never read it - **no flag needed**, the tail word at 474 rules |
| the encoder | `os88venc.py` | two screens, two buckets (the disk's for the base, the bank's for the enhancement), and the schedule that places enhancement blocks where the disk can fetch them |
| the reader | `video.asm` | a SECOND cursor on the region, and reads that alternate between the two places on the disk - a seek each switch, so it reads the region in big blocks (64 KB or more) in the calm, and never mid-burst |
| the hook | `video.asm` | after a frame's base record, its enhancement records out of the bank: through the frame on EMS, copied down on XMS - the only copies left |
| seeking | `video.asm` | the enhancement from a key onwards is the region's block for that key; until it is in, the play is base only |

Of these the encoder is the largest and the riskiest, the hook's change the
smallest. ESTIMATE: two to three waves of work the size of 98.3.18.2-4
together, and a format version in 98.1.

### 4.2 What it does NOT do

- **It does not beat the bank on bytes** (section 2). A clip that needs
  more than disk + bank + prefill still cuts; on Last Exile at 448 KB/s the
  next limit is the CPU, which neither scheme touches (SPEC.md 98.2.1.3.2's
  table: 407 frames cut by the CPU's average once the disk binds none).
- **It does not help EMS in place**, which already copies nothing - except
  for the fallback.
- **Repeat, BIGSP and sound ahead** would each need their answer again.

## 5. Recommended, in order

1. **Done: the copy charged only while the bank holds** (section 3.2). Every
   `--bank` encode improves; no file or player change.
2. **`--prefill auto`**: the encoder computes the least prefill that the
   bank can use - the largest cumulative deficit, capped at the bank - and
   writes it in KB. The player already reads that word. One pass over the
   bucket's history after the encode; no format change. Section 3.1 says
   the prefill IS the bank on a calm-less clip, so this is the knob that
   should not be a guess.
3. **The layered format** (section 4), now that section 7 has measured it:
   one file made for a slow disk that plays a third better on the owner's
   286, fetched at the ring-full moment with no extra RAM (7.3). Its first
   wave is the encoder's second pass, which `tools/os88vidlayer.py` already
   is in outline; the player's three pieces follow.

## 6. Questions for the owner

1. Is the fallback worth a format change? A `--bank` file today plays
   without a bank and pauses in its bursts; a layered one would play at
   no-bank quality instead.
2. `--prefill auto` (5.2) as the default when `--bank` is given, in place of
   10 s?
3. The two-screen enhancement's size is the unknown that decides section 4;
   measure it first (an encoder-only wave) before deciding?

## 7. Measured: the layer (2026-10-09, `tools/os88vidlayer.py`)

The owner asked for section 6.3's measurement, with the idea widened: not
only more RAM but a FASTER DISK - "encoded for this disk speed, able to play
better if you have better", the player fetching the layer in real time when
the disk has time to spare. Same clip, same settings; the BASE is the file
made for 200 KB/s with no bank (3.81% error as seen), and each layer is
encoded for one better machine.

### 7.1 The wrong way to make a layer: the difference to a full encode

The first measurement took the layer as whatever turns the base's screen
into a SEPARATE full-quality encode's (the clip with the disk out of the
way, 401 KB/s, 1.73%): **224 KB/s, 91% the size of the base**, and base +
layer 470 KB/s against the full stream's 376. The two encodes chose
differently from the first frame, so the layer spends most of its bytes
undoing base writes that were themselves good. Layering that way costs more
than not layering. Kept as the reason section 4's layer must be ENCODED,
not diffed.

### 7.2 The right way: a second encoder on top of the base

The tool runs the base encode exactly as it ships and, beside it, a second
`Encoder` per better machine whose screen is the enhanced player's: each
frame the base's writes go onto it first, then it spends its own budget on
the best further writes toward the same target - the encoder's own ranking,
so a good base write is never undone. Both budgets are what the base cannot
use on that machine, so the base plays exactly as it would without the
layer:
- **the disk**: the base's own bucket simulated at the machine's rate (the
  profile's measured curve at the base's CPU share, as the base's encoder
  prices it); what it would CLIP at the ring's depth - the ring full and the
  disk idle - is the layer's, banked in the layer's memory;
- **the CPU**: one bucket the two share. A first pass logs every base
  record's cost; a backward pass gives the least the bucket must hold after
  each frame to pay every later base frame on time, and the layer takes only
  what stands above it. Exact: no base frame runs late, and nothing is
  withheld that the base could not have used.

| the machine's disk | layer | its CPU on top | layered play | one stream made for that disk |
|---|---|---|---|---|
| (the base's 200 KB/s) | - | - | 3.81% | 3.81% |
| 250 KB/s | 46.0 KB/s | +13.6% | 3.34% | 3.08% |
| 300 KB/s | 97.7 KB/s | +28.4% | 2.83% | 2.59% |
| 350 KB/s | 118.7 KB/s | +34.2% | 2.55% | 2.19% |
| 448 KB/s (the owner's 286) | 118.8 KB/s | +34.2% | 2.48% | 1.77% |
| 448 KB/s, 3 MB for the layer | 118.9 KB/s | +34.2% | 2.48% | - |
| unlimited | 118.9 KB/s | +34.2% | 2.48% | 1.73% |

**What it says:**
- **It works.** A file made for 200 KB/s plays 35% better on the owner's
  286, and recovers 64-80% of the gap to a file made for that machine: 80% at
  300 KB/s, 78% at 350, 65% at 448.
- **Past ~350 KB/s the CPU binds, not the disk.** The base took ~68% of the
  286's budget and the layer can have the rest, ~34% - and a picture drawn as
  two records a frame costs more cycles than the same picture as one, so the
  layer runs out before the single stream does. On a FASTER CPU the layer
  would keep growing; a layer is encoded for one machine's CPU as well as its
  disk.
- **More RAM buys nothing on this clip** - no calm stretches to fill a bank
  in, and the CPU binds first. A clip with calm in it would differ.
- **The measurement's own bias**, both ways: it does not price the seek
  between the base's place on the disk and the layer's (a few ms a switch, a
  32 KB block every ~0.3 s at these rates: ~5-10% of the disk), which makes
  the layer's disk rows optimistic; and the CPU is exact against the 286
  profile, so the CPU rows are what they say.

**A finding on the way, FIXED**: the encoder's retry, when a frame's CPU room
was already overdrawn, scaled its estimate by a NEGATIVE ratio and came back
with a large positive room - the layer spent 685K cycles a frame against a
room of minus two million before it was found. `er` is clamped at 0 in all
three encoders now; no shipped encode had met it (the no-bank and banked
encodes are byte-identical before and after).

### 7.3 Fetching it in real time: viable

**Yes, and the signal already exists.** What the layer's disk budget models
- the base's bucket clipping at the ring's depth - is in the player exactly
the moment `vp_fill` answers that the ring is full: the bracket's loop today
calls `vp_bstep` there to fill the bank (98.3.18.2). A layered player calls
a layer reader there instead, reading the layer's next block from its region
into the layer's memory, with no measurement of the disk's speed at all: a
fast disk is simply one that finds the ring full more often. What it needs:
- **somewhere to put it**: the layer's records wait from their read to their
  frame. The table's 64 KB rows are a conventional claim of that size; a bank
  is more, and buys nothing here (7.2);
- **the CPU in real time too**: the encode prices the layer against ONE
  machine's CPU, and a machine slower than that must drop layer records
  rather than fall behind. The hook already knows when it is late (owed
  time, 98.3); a layer record is skippable by construction - its writes are
  values, not deltas, so a skipped one leaves those bytes at the base's, no
  worse than the base, and later records go on improving the screen;
- **the seek**: the layer in its own region costs a seek a block on the
  machines that read it and nothing on the ones that do not - which is the
  right way round. Interleaving it in the base's stream would remove the
  seek and make every base-only machine read the layer's bytes too,
  defeating the point.

So "made for this disk, better on a better one" is three things in the
player - a second cursor and region, a reader at the ring-full moment, the
hook applying a layer record when it is not late - and the encoder's second
pass, which this tool already is in outline.

## 8. Built (2026-10-09)

SPEC.md 98.1.9 is the contract. What the build changed against 7.3's
sketch, so it is not re-derived:

- **Every layer super-packet is a whole 32 KB slot on a 32 KB boundary.**
  `READ_SEQ` reads at a cluster multiple and the file cannot know the
  cluster of the disk it will be played from; 32 KB is a multiple of every
  FAT volume's up to 32 KB and is a player slot, so the reader is one call
  into one slot and has no mirror. ~Half a record of padding a super-packet.
- **The layer's slots are claimed before the ring is sized**, and only when
  the ring still gets its stream's slots. Claimed after it they found
  nothing: the ring takes every slot the heap offers (98.3's headroom).
- **The "seek" cost is real on a floppy**: `vidlyplaystream` reads base and
  layer alternately off a 5150's floppy and the layer falls behind (49
  drawn, 21 read too late) - the degrade 7.3 promised, measured. The
  encoder prices a seek each way per 32 KB (`--layer-seek`, 10 ms), which
  is a hard disk's figure; a floppy's is several times it.
- **Open**: the layer's PREFILL (its header word is written; the player
  fills only its slots before the first frame, not a bank) - BUILT since,
  as `--layer-bank`/`--layer-prefill`; a layer beside a `--bank` (refused
  by the encoder); and a layer for a DIFFERENT CPU - BUILT since, as
  `--layer-profile`.
- **For ONE machine, one stream wins** (SPEC.md 98.1.9's table): on the
  owner's 286 a single stream for 448 KB/s with a 3 MB bank prefilled is
  0.11% error as seen, against 0.32% for a base for 200 KB/s with a layer
  for 448 and the same bank. The layer's price is that it cannot undo the
  base's writes and that two records cost more CPU than one. So the layer
  is a two-machine format, and `--layer-profile` (built) makes the second
  machine another CPU: a 5150 CGA file with a layer for the 286 went 1.32%
  -> 0.00% in `vidlayer`'s third arm.
