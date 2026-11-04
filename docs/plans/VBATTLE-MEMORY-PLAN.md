# VBATTLE-MEMORY — streaming the loops, and the memory faster machines have

**Status: M1, the renditions (9), M2 and M3 BUILT (2026-11-04, format 7,
SPEC.md 125.4.1); M4 proposed.** The owner's
questions, ahead of the Hercules and VGA renditions: *"Could the openings
and endings stream? ... we're about to get to vga and that is always more
data hungry, even at the 320x200n we have planned (and likely not using all
16 colors). Then there are faster machines - and their ram. Xms for example,
which video player itself uses to good effect. Make a plan for how to best
utilize this."*

SPEC.md 125 is the battle as built; SPEC.md 98.3.18 is Video Player's hold,
bank and in-place decode, which this plan borrows from and does not copy.

## 1. What streams today, and what does not

| part | today | at 640 KB |
|---|---|---|
| the openings, in turn | **stream**, ring *i* mod 2, the next one's head read while one plays (125.4) | any length |
| an action's clip, an ending's clip | **stream** through a ring, its head read before it may start | any length |
| the music | **streams** through its 32 KB ring, or is resident when 60 KB or less | any length |
| the title loop, an end loop (125.3.1) | **held whole in one ring**, round in memory | must fit a ring: 144 KB |
| the idle loop | **resident** in the block, 16-bit offsets | the block: 60 KB |
| the entries (one per anchor per clip) | resident, by paragraph (format 4) | memory |

So the openings and the ending clips already stream. What does not is
every LOOP: the two new ones, which a ring holds whole, and the oldest one,
the idle loop, which the block holds whole.

## 2. What VGA costs: measured

The owner's Attack1, 78 frames (5.2 s), 320 x 142, 15 frames a second,
no budget (`--profile lossless`, so the cost is the encode's own, not a
budget's cut), 2026-11-04:

| rendition | file | KB/s | x CGA4 |
|---|---|---|---|
| CGA4 (today's) | 428 KB | 82 | 1.0 |
| VGA4 `--screen 320x200n --palette clip4` | 515 KB | 99 | 1.2 |
| VGA4, `clip8` | 878 KB | 169 | 2.1 |
| VGA4, `clip16` | 1,339 KB | 257 | 3.1 |

The owner's guess holds and it is steep: **the palette's size is most of
the bill.** Four colours of VGA cost about what CGA4 does, eight about
twice, sixteen three times. Against the disks the profiles know (os88venc
PROFILES): an ST-225's 119 KB/s carries none of the VGA lines unbudgeted,
with the music beside them; a slow 286's 150 carries four colours; a fast
286's 450 to 600 carries all of them.

What that does to section 1's table:
- **a ring at 640 KB (144 KB) is 0.56 s of sixteen-colour footage.** A
  title or end loop of any useful length does not fit one;
- **the idle loop does not fit the block.** The owner's H3 idle loop is
  39 KB of CGA4 for 30 frames; three times that is ~120 KB against 60;
- a ring's reach ahead (what makes a clip start without a wait) falls from
  ~1.8 s to ~0.6 s at the same memory.

So two things are needed at VGA whatever the machine, and a third is what
makes VGA good:
1. the LOOPS must stream (section 3) - title, end, and idle;
2. the budget must price a VGA rendition by its colours (section 6);
3. a machine with memory past 640 KB should keep the bundle, or the parts
   that matter, out of the disk's way (sections 4 and 5).

## 3. Wave M1: every loop streams (all machines)

### 3.1 A loop on the disk, laid out to be read round

A ring is filled in 4 KB pieces at cluster multiples (125.4), and a loop's
lap starts at record 1, which is not on a cluster boundary. So the BUILDER
makes it one:
- **record 0 is padded** to end on a 4 KB boundary of the file, and **the
  last record (the seam) is padded** so the lap is a whole number of 4 KB;
- the padding goes INSIDE the record, after its ten lists and before its
  sound: `vd_native` ends at the tenth list's `ret` and never reads past
  it, and the sound is found from the record's end (RH_LEN - RH_ALEN). Only
  the host's `decode` insists the lists end at the sound (`os88vbt.decode`),
  and it learns a flag for it. A record is still at most 16 KB with its
  padding; a seam that would pass that takes its padding in the record
  before it;
- the clip table's row carries the lap's file offset and length in its
  spare bytes 18..23 (a dword offset from the stream's start, a word of 4
  KB units), flag LOOP as today.

### 3.2 The reader reads it round, as it reads the music

The music is "looped on the disk" already (`vb_mpiece`): a piece at its
own offset, the offset wrapped at the music's end. A loop's ring does the
same: its FILL is a virtual offset that only grows, and the file offset is
`lap + (fill - lap) mod laplen` once past record 0. Every piece stays a
cluster multiple because the lap is. The parse walk (`RG_POFF`) and the
guard's tail copy need nothing new: the ring sees an endless stream of
records - 0, 1..n, 1..n, ... The picture's and the sound's cursors no
longer jump back (`vb_lrewind`, `vb_lcur`); they go on through the ring,
and the lap's end is only where `[vb_cleft]` reloads.

### 3.3 Held when it fits, streamed when it does not

A loop that fits its ring stays as today - read once, round in memory, the
disk free for the clips' heads. One that does not streams. The engine
decides at play, from the ring it got; the builder's budget shows which,
per rendition and per profile, and prices a streamed loop's KB/s against
the disk WITH the music (the title's own sound replaces it; an end loop's
does or does not).

### 3.4 The idle loop leaves the block

The idle loop gets the entries' treatment (format 4): its records in a
region of their own, addressed by PARAGRAPH, resident and bounded by
memory rather than by a 16-bit offset. It stays resident, not streamed,
because an action's EXIT can land on any of its frames and the loop must
go on from there at once: a streamed idle would need a seek and a head's
read at every exit. That costs conventional memory at VGA (~120 KB for the
owner's loop at sixteen colours), which sections 4 and 5 give back.

**DONE 2026-11-04.** A 204 KB title and end loop against a 140 KB ring lap
with no stall on MartyPC's XT (`vbtloops` C), the ring's fill passing 540
KB; the idle loop is paragraph-addressed; the rings at 640 KB came to 140
KB beside a 120 KB claim (the idle loop's own 4 KB rounding), measured,
and the arithmetic agrees. Whether going round is really seek-free (the
kept cursor) is not measured: a seek would be slower, not wrong.

**Format 7** carries 3.1 and 3.4. **The gate**: `vbtloops` with a loop
bigger than its ring (a knob holds the ring small), every lap's glass one of
the loop's own frames, and the reader's counters showing it read round;
`vbtengine`'s lockstep unchanged.

## 4. Wave M2: the bundle HELD in extended memory (286 and up)

Video Player's 98.3.18 is the precedent and its lessons are this wave's
rules. **A bundle that fits the XMS pool (SPEC.md 41) is held there whole,
and every read the hold can answer is a copy instead of a disk read** - a
clip's head, its body, a loop's lap, the music. On a machine with no pool -
every 8088 - nothing changes: one `OSAPI_XMEM_CAPS` at load and a compare
in each read.

- **When it fills**: at the load, a piece a pass with the window's
  progress (as Video Player's timer does), and, because a battle has a
  natural pause, **while the title loop waits for a key** - the reader has
  nothing better to read then. Behind the play as well: a piece the disk
  reads that runs on from the hold's end goes up behind it, so the second
  play is all memory where the first was not.
- **What a read becomes**: a range wholly held is `OSAPI_XMEM_COPY` into
  the ring at exactly where the disk would have put it - no bounce, the
  ring is conventional. Then the stream's own file cursor is moved past it
  without a re-seed, as 98.3.18 learned to (two cursors, handed over,
  never sought again).
- **What it buys**: no read waits on a seek or a revolution, so an action
  starts the frame its anchor comes round, a loop never stalls, and the
  disk's rate stops being a VGA rendition's ceiling - **a held bundle can
  be encoded for memory's rate, not the disk's**, which is what sixteen
  colours want (section 6).
- **The copies' own cost**, measured by Video Player on the owner's 286:
  `int 15h AH=87h` holds interrupts off for its whole copy, ~11 ms for 32
  KB. The battle's clock is the card's DMA, which a mask does not stop, so
  that only delays a frame; with no card the clock is the PIT's periods
  counted by the hook, and a masked period is lost. So **copies are 4 KB
  (~1.4 ms on a 286) under the PIT's clock and 32 KB under the card's**,
  and a 386 takes §41's unreal-mode path in 1 KB pieces with interrupts on
  between them. The battle has no speaker output, so Video Player's
  98.3.18.5 (no copy at all under the speaker) does not arise.
- **On the UI task only**: the reader is already there (§41.8's rule).
- **The window says which**: "Held in XMS: 3,000 KB" on the window's card,
  as Video Player's line 6 does.

**M2 DONE 2026-11-04** (SPEC.md 125.4.1): on QEMU the demo is held whole,
byte for byte, while the title goes round, and with B: made blank the
battle plays to victory and starts again from the hold alone. Not on a
real 286 yet: the copies' cost under its `int 15h` is Video Player's
measured figure, not this engine's.

## 5. Wave M3: when the pool is smaller than the bundle

A 286's common pools are 1 and 3 MB (98.3.18.2.1); the owner's H3 battle is
3.0 MB in CGA4 and would be ~6 to 9 MB at sixteen colours. So most VGA
battles on most 286s are PARTLY held, and what to hold first is the design.

**The builder decides the order and the bundle carries it** - a HOLD ORDER
table in the first region: ranges of the file, most worth holding first.
The engine fills down the table until the pool is full. The policy is the
builder's because only it knows the battle: which clips a menu reaches in
one key, which the enemy's weights favour, what loops. The order it writes:
1. **the music**, when it streams: 11 KB/s off the disk for the whole play,
   9% of an ST-225 and more of a floppy;
2. **every action's HEAD** (VB_HEAD, 40 KB, or all of a shorter clip): the
   head is what a start waits for, so with every head held no action ever
   waits for the disk to start - the body streams behind it;
3. **the loops**: the title's and the end loops' laps (and the idle's
   records, if a later format streams it);
4. **whole clips**, by how often they play - the top menu's first items,
   the enemy's moves by weight - and smallest first among equals.

The window's Build tab shows, per profile with its pool, where the line
falls: "1 MB pool: the music, every head, the title - 940 KB".

**M3 DONE 2026-11-04**: the order in the header, the engine's hold its
ranges while the pool lasts (`vbtholdpart`), the Build tab's lines for a 1
and a 3 MB pool.

## 6. The budget learns colours and memory

- **A VGA rendition's palette is chosen with its price shown**: four, eight
  or sixteen colours of the clip's own, at the measured ~1.2x, 2.1x and
  3.1x of CGA4. Most battle footage will look right at eight, and the
  builder says what each costs on the target's disk.
- **A profile gains its memory**: the pool its machine has (none on a 5150;
  1 or 3 MB on a 286; more on a 386), and the budget prices a clip against
  the disk when it streams from it and against memory when M2 or M3 holds
  it. A held clip's KB/s is limited by the decode, not the disk - the
  CPU's line, which the encoder already models (`enc.cost`).

## 7. Wave M4: EMS on an 8088, in place

An XT has no XMS, but it can have an EMS board - the owner's PicoMEM, a
Lo-tech 2 MB - and on a board as fast as RAM, Video Player decodes **in
place**: the ring IS the board's pages, the hook maps them into the frame
with its own OUTs, and nothing is copied (98.3.18.7). For the battle that
is the XT's way past the 119 KB/s disk: a 2 MB board holds a whole CGA4
battle, so every action starts at once. It comes after M2 and M3 because
it reaches into the hook (the decode reads through the frame) where M2 and
M3 change only the reader; and it has Video Player's rules attached:
- **time the board first** (98.3.18.8): the owner's PicoMEM v1 answers with
  8 wait states and played worse in place than from the disk. A slow board
  is used as a copied bank or not at all;
- below a 386, **EMS before XMS** (98.3.18.6): one copy with interrupts on,
  or none, against two with them off.

## 8. What is shared, and what is not

The transports are the OS's - `OSAPI_XMEM_*` (§41) and `EMS.DRV`'s
`EMSV_*` (§107) - and both packages use them as they are. The POLICY is
each package's: Video Player's is one file played front to back with
seeks, the battle's is a graph of clips played in an order a menu chooses,
and their rings differ. So the battle does not take Video Player's code; it
takes its lessons, named where each applies:
- two cursors handed over and never re-seeded (98.3.18.2);
- the whole pool, none kept back (98.3.18.2.1);
- copies sized to the clock that pays for them (98.3.18.5);
- the board timed before it is trusted (98.3.18.8);
- the window saying which memory a play went through (98.3.18.6's line).

If a third package needs a hold, that is the time to factor one out
(`apps/os88hold.inc`); two policies are not yet a pattern.

## 9. For tonight: Hercules and VGA

- **Each adapter is its own encode of the same project** (VIDEO-BATTLE-PLAN
  4): `--preset herc` (720 x 348, one bit, two pages) and `--preset vga4
  --screen 320x200n` (mode 0Dh, 40-byte rows, page flipped - which also
  ends CGA's one-frame tear at a cut). The battle decodes with Video
  Player's own `apps/video/vdec.inc`, which plays both layouts there; that
  it does so inside the battle's hook, and how fast, is that step's first
  check, not an assumption.
- **They will stream as today and be budget-limited on an XT's disk**
  until M1 to M3 land; that is the honest first step, and the budget will
  say so.
- **M1 is needed for VGA regardless** of memory: its loops do not fit a
  ring, and its idle loop does not fit the block. So the order after the
  renditions is M1 (format 7), then M2 (the whole hold), then M3 (the
  order), then M4 (EMS in place).
- **DONE 2026-11-04** (SPEC.md 125.3.2): BATTLEV.VBT and BATTLEH.VBT, made of
  the CGA encode's own frames, one palette for a VGA battle with the band's
  colours in slots 0..3, the band presented per adapter, the fade per
  adapter; `vbtadapters` holds both on MartyPC to the byte. The owner's H3
  battle: CGA4 3.0 MB, VGA4 at eight colours 4.0 MB, Hercules 4.1 MB.
  **Measured, and a cost**: on MartyPC's 4.77 MHz XT the band's rebuild
  takes ~1.2 s of the foreground's time under the hook's load on every
  adapter (CGA too); VGA4 and Hercules add ~0.6 s to present it, where CGA
  copies in ~0.2 - the menu up ~2 s after its turn on those. A second page
  is not used yet: a cut is drawn where it is seen, as on CGA.
- **Hercules' data** has not been measured here; one bit at 720 x 348 is
  250,560 pixels against CGA4's 45,440 at 320 x 142, so expect it near the
  VGA lines. It is the first measurement of that step.

## 10. Measured, and still to measure

- Measured here: the four Attack1 encodes of section 2 (unbudgeted).
- From Video Player: a 32 KB `int 15h` copy ~11 ms on the owner's 286;
  EMS copied down 2.75 ms a KB on an 8088; the PicoMEM v1's 8 wait states.
- To measure: a Hercules rendition's KB/s; an eight-colour VGA rendition
  BUDGETED for a 286's disk, by eye, against sixteen; the hold's fill rate
  on the owner's 286; what the PIT-clocked hook loses to a 4 KB copy (a
  card-less play on the 286 with a hold).

## 11. Not in it

- kern_mega: the battle refuses it (125.7), its claims being carved by
  paragraph arithmetic.
- A faster frame rate on a fast machine: the bundle's fps is the project's,
  and a rendition at 20 or 30 frames a second is its own encode, priced the
  same way.
