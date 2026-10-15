# A stream's read-ahead in XMS — what it buys, what it costs a 286

**Status: BUILT through section 9's waves 1-4 and section 10's E1-E3**
(SPEC.md 98.3.18.2-98.3.18.7, 98.2.1.3.2, 107). The rest of this file is the
investigation it began as; docs/plans/VIDEO-OVERAGE-PLAN.md is the
follow-on, banking only the overage, and it corrected the encoder's charge
for the bank's copies.
The owner's answers to section 8 are in it (2026-10-08), and **section 10
is EXPANDED MEMORY** - what LIM EMS would cost the kernel, measured on
MartyPC's Lo-tech board, and why it is the better store for an 8088 and a
286 than XMS is.
What exists is the instrument that produced every number below
(`tools/os88vidbuf.py`) and the field row that measures the one number it
cannot compute (VIDDISK's `X`, `tests/vidbench/viddisk.asm`). Section 9 is
the build, in waves, for whoever takes it.

**The answer in four lines:**

1. **The player cannot use XMS for these clips today.** SPEC.md 98.3.18's
   hold takes a file only when the WHOLE file fits the pool, and the two
   clips are 36 MB and 75 MB. A 286 has 15 MB of address above 1 MB at most.
2. **A bank — a FIFO of chunks in XMS, ahead of the ring — takes a clip's
   disk requirement from its PEAKS down to its MEAN, and no further without a
   wait.** For the lossless 320 x 240 clip: 458 KB/s with the ring alone, 348
   with 4 MB banked, the clip's own mean being 363. Below the mean only a
   prefill buys anything, at about a second of waiting per 30 KB/s saved:
   285 KB/s with an 8 MB bank filled for 30 s before the first frame.
3. **On a 286 the bank costs CPU, and that is the whole risk.** Every banked
   byte crosses `int 15h AH=87h` twice (section 4.2 is why it cannot be
   once), and almost every byte of a busy clip is banked (87-98% in every
   model run). At an ESTIMATED 0.25 ms a KB the bank pays for itself; at
   1 ms a KB the copies make the 286 CPU-bound and **the play is worse with
   XMS than without it**. Which side a real 286 BIOS is on is unmeasured,
   and VIDDISK `X` is the row that measures it.
4. **The bigger lever is the ENCODER**, told about the bank: the same clip
   encoded at 250 KB/s must leave out 32% of what a lossless encode wants
   with today's ring, 20% with a 4 MB bank and a 17 s prefill, 7.8% with
   8 MB and 33 s (section 3.3). That is how a file gets *closer to a period
   disk*; the player change is what lets such a file play.

## 1. The two clips

Both are the Last Exile opening, `MODEX` VGA8 320 x 240 at 24 fps, 93 s,
made by `os88venc.py` (their options blocks say so, SPEC.md 98.1.1.4):

| | `05-320FS.V88` | `08-486.V88` |
|---|---|---|
| profile | `lossless` | `486`, `--frame-cap 63.5` (BIGSP) |
| sound | PCM8 11,025 Hz | PCM8 22,050 Hz |
| stream | 33.0 MB, mean **363 KB/s** | 70.4 MB, mean **780 KB/s** |
| a second's bytes: p10 / median / p90 / max | 172 / 374 / 514 / 613 KB | 455 / 797 / 1,062 / 1,272 KB |
| 5 s means, KB/s | 225 253 511 362 293 379 430 328 458 248 420 409 353 500 411 412 318 284 151 | 479 563 1083 768 638 795 852 715 970 574 921 881 776 1006 872 901 683 626 307 |
| decode, share of a period | 286-vga: median 40%, p90 67%, max 82% | 486: 29% / 50% / 65% (286: 86% / 139% / 180% - not a 286 file) |

**What matters is the median against the mean: 374 against 363, 797 against
780.** These clips are not "slow parts and fast parts" in the sense a bank
exploits - they are fast almost all the way through, with one quiet stretch
at each end. A bank smooths a clip down to its mean; it cannot take a clip
whose mean IS its typical second much below that.

## 2. What exists, and why it never engages here

SPEC.md 98.3.18 holds a streamed file in XMS **whole**: `vp_xopen` asks the
file's size and `OSAPI_XMEM_CAPS`, and takes the hold only if the pool
covers the file. It then fills from the front - the window's timer while
nothing plays (`vp_xstep`), and behind the stream while it plays
(`vp_xput`) - and every read the hold can answer is a copy (`vp_xfill`,
`vp_xrdat`). What it buys is a second pass with no disk at all: a seek, a
Repeat's lap, the next play, Live (98.3.18.1).

It is the right design for a clip that fits, and it is most of the
machinery a bank needs (section 4.1). But it is all-or-nothing: a 36 MB
file on a 4 MB pool takes no hold, and the play is the ring's.

## 3. What a bank would buy - the measurements

### 3.1 The instrument

`tools/os88vidbuf.py CLIP.V88 --minrate` plays the clip's real super-packets
through a model of the player's reader on a model of the machine, a
millisecond at a time, and bisects the slowest DISK it plays on:

- **the player**: a ring of K = 10 slots (a 640 KB VGA machine with a card,
  98.3), `vp_fill`'s rule - chunk *c* may load while *c* < the hook's
  super-packet's chunk + K - and a super-packet drawable once every chunk it
  touches is in. With `--xms N`, an N KB FIFO ahead of the ring with section
  4's policy. The play starts when the ring and the bank are full, or at
  `--wait` seconds;
- **the machine**: the hook first - each frame's decode at the profile's own
  measured cost (`os88venc.profile_table`, SPEC.md 98.2.3.3) - and the reader
  gets the rest. A disk chunk takes the media's time and, on a controller the
  CPU copies (every AT controller, IDE and MFM alike), the profile's CPU per
  KB (`--pio`: 1 / VIDDISK's idle row, 0.759 ms/KB on `286-vga`, which its
  25/50/75% rows bear out to within 5%). An XMS copy is CPU alone
  (`--xcopy`). `--dcache` is what the DRIVE reads ahead while the CPU is
  decoding: 0 for an MFM drive, which loses the turn; 64 KB here, for the
  IDE drives and CF cards that period machines are run with now;
- **a stall** is a due frame whose super-packet is not in; a play passes if
  its stalls total under 0.1 s (`--tol`).

**Calibration**: with the bank off and the CPU taken out, the model's
answer for `05-320FS` (458 KB/s) is exactly a plain bandwidth calculation's,
and on the owner's 86Box `286-vga` (an IDE disk the CPU copies at 1,318 KB/s
idle) it says the lossless clip plays with 0.01 s of stalls - the profile's
own note says *"the owner's lossless Last Exile, which plays on this
machine with nothing late"* (with no drive cache, so the model's harsher
case).

### 3.2 The player: the slowest disk that plays, KB/s

`05-320FS` on `286-vga` (drive cache 64 KB; the XMS copy ESTIMATED at three
costs because nobody has measured one on a 286):

| bank | 0.25 ms/KB copy | 0.40 ms/KB | 1.0 ms/KB | prefill (at that disk) |
|---|---|---|---|---|
| none (today) | **458** | 458 | 458 | 0.7 s |
| 1 MB | 407 | 430 | CPU-bound: stalls 0.2 s at ANY disk | 3 s |
| 2 MB | 387 | 430 | stalls 0.2 s | 6 s |
| 4 MB | **348** | 410 | stalls 0.3 s | 11-13 s |
| 8 MB | 285 | 310 | stalls 0.4 s | 28-30 s |
| 15 MB | 197 | 204 | stalls 0.6 s | 77-80 s |
| 8 or 15 MB, prefill held to 10 s | 346 | 390 | - | 10 s |

`08-486` on `486` (copy 0.05 ms/KB, unreal mode - SPEC.md 41.4):

| bank | slowest disk | prefill |
|---|---|---|
| none (today) | **1,078** | 0.3 s |
| 1 MB | 865 | 1.6 s |
| 4 MB | **780** | 5.7 s |
| 8 MB | 722 | 11.8 s |
| 15 MB | 627 | 25 s |
| 15 MB, prefill held to 10 s | 712 | 10 s |

With **no drive cache** (an MFM drive, or a model of one), the CPU-copied
disk loses every turn a frame's decode holds the CPU, and the 286's ring
needs 1,192 KB/s of media for the same clip; a 4 MB bank brings that to
804-973. The cache is the difference between "a disk the CPU copies" and "a
disk that waits for the CPU", and a real drive is somewhere between.

What the tables say:
- **A bank of 1-4 MB is worth 10-25% of disk rate** to these clips, and its
  prefill is seconds. Every MB past that is mostly bought with waiting.
- **"Buffer the whole of RAM, then spend it down" works, at the rate the
  arithmetic says**: to play at rate R below a clip's mean M for T seconds,
  the bank must hold at least (M - R) x T before the first frame.
  `05-320FS` at 285 KB/s: (363 - 285) x 93 = 7.1 MB, against the 8 MB the
  model needed - a clip's bytes do not arrive evenly, so the bank must also
  cover the worst stretch on the way.
- **The 286's copy cost decides whether the bank helps at all.** 0.40 ms/KB
  - ~2.5 MB/s, a 12 MHz 286 doing `rep movsw` with one wait state plus a
  reset - and the 4 MB bank buys 48 KB/s where at 0.25 it buys 110. At 1
  ms/KB it costs more than it buys.

### 3.2.1 The number nobody has: VIDDISK `X`

`int 15h AH=87h` on a 286 enters protected mode, copies, and gets back to
real mode by RESETTING THE CPU (through the 8042 or a triple fault - the
286 has no way back otherwise), and the BIOS's shutdown path runs on the
way. What that costs a call is a property of the BIOS and the board, and it
is not in any document in this tree. QEMU cannot say (it has no 286, and
times nothing); MartyPC is an 8088.

So VIDDISK grew an `X` key. It reports, on the machine it runs on:
- the CPU tier and the pool's free KB;
- **a 32 KB round trip, checked byte for byte** - a copy that times well and
  moves the wrong bytes is not a measurement, and the row was verified to go
  `BAD` with the copy back aimed at the wrong half;
- `OSAPI_XMEM_COPY` up and down at **32, 16, 8 and 4 KB a call**, PIT-timed
  with the copy inside the span - which splits a call's cost into its fixed
  part (the reset) and its bytes, and gives the interrupts-off window per
  piece size (on tier 1 all of the call is masked);
- **the bank filling**: 5 s of 32 KB `READ_SEQ` alone, then 5 s of the same
  with every chunk copied up - KB/s x 10, the two rows' ratio being what
  banking a byte costs over reading it.

Measured on QEMU, tier 2 (the mechanism, not a timing - QEMU's times are
the host's): the check `ok`, every row reporting, the block freed, and with
`-m 1` the run says `NO EXTENDED MEMORY` and stops. **What it needs is a run
on the owner's 286 and 486** (`tests/vidbench/FIELDDISK.TXT` says how); its
`XMS up 32K` row divided by 32,000 is `--xcopy` in ms/KB.

**MEASURED on the owner's real 286, 2026-10-08**
(docs/reports/VIDDISK-XMS-286-2026-10-08.md): **0.345 ms a KB**, up and down
alike - ~0.45 ms fixed a call plus 0.339 a KB, so a 32 KB piece is 11.3 ms
masked. The middle of the 0.25-0.40 range, and nowhere near the 1 ms/KB that
would have made the bank a loss. Its disk streamed 736 KB/s idle (486 at a
50% hook), and banking every chunk cost the fill 14%, not the copy's whole
price. Re-run with that machine's figures, the lossless clip STALLS with the
ring alone (the disk's ~0.9 ms of CPU a KB lands in the bursts) and plays
cleanly with a 2-3 MB bank off a disk needing 420-485 KB/s - the report has
the table, and its caveat: the decode is still priced at 86Box's mr286.

**And the caveat bit** (docs/reports/VIDBENCH-PVGA286-2026-10-08.md, the
same day): VIDBENCH on that machine - a 16 MHz AMD 286, a PVGA1A on the
board - decodes 12-29% slower than the mr286, and with its own table
(profile `286-pvga`) the lossless clip is CPU-bound in every cell: a bank
takes its stalls from 1.1-2.2 s to 0.2-0.6 s and cannot take them to 0. On
that machine the bank is the second half of the answer; the first is an
encode budgeted for `286-pvga`.

### 3.3 The encoder: what a bank lets a period-rate encode keep

`os88vidbuf.py --deficit R ...` takes the lossless file's own super-packets
as what an encode WANTS, runs the encoder's disk bucket (`Budget`,
`os88venc.py`) at R as deep as the read-ahead is, and sums the bytes it
could not send: the share of the picture an encode at R must leave out,
before later frames repair it. It is a proxy - a real encode chooses WHICH
bytes go (98.2.1, best first) and its error as seen is far smaller than the
share - but it ranks the configurations, and it is free.

`05-320FS` (the bucket is the ring less two slots, 256 KB, plus the bank;
"half / full" is the bucket started half full - today's assumption - or
full, which is the prefill, with that prefill's wait):

| R KB/s | ring | +1 MB | +4 MB | +8 MB | +15 MB |
|---|---|---|---|---|---|
| 150 | 58.6% | 57.0 / 55.5%, 8 s | 52.5 / 46.4%, 29 s | 46.4 / 34.3%, 56 s | 35.8 / 13.0%, 104 s |
| 200 | 45.1% | 43.4 / 42.0%, 6 s | 38.9 / 32.9%, 21 s | 32.8 / 20.8%, 42 s | 22.2 / 0.0%, 78 s |
| **250** | **32.2%** | 29.9 / 29.0%, 5 s | 25.3 / **19.9%**, 17 s | 19.3 / **7.8%**, 33 s | 8.6 / 0.0%, 62 s |
| 300 | 21.0% | 16.6 / 16.6%, 4 s | 11.9 / 7.5%, 14 s | 5.8 / 0.0%, 28 s | 0 / 0, 52 s |
| 400 | 4.7% | 0 / 0, 3 s | 0 | 0 | 0 |

`08-486` at the rates a 486 IDE of the period streams: at 600 KB/s, 26.4%
with the ring, 18.9% with 4 MB (7 s), 13.2% with 8 MB (14 s), 3.2% with 15
MB (26 s); at 800, 9.2% with the ring and nothing from 4 MB up.

**This is the lever the request is really about.** A file made for a 250
KB/s disk with an 8 MB bank and a 33 s prefill is within 8% of lossless
where today's is 32% short. The player change in section 4 is what lets such
a file play; the encoder change in section 5 is what makes it.

## 4. The player: a BANK, when the file does not fit

### 4.1 The shape

When `vp_xopen` finds the pool smaller than the file, it takes the largest
block it can (section 4.6) as a **bank: a FIFO of 32 KB chunks ahead of the
ring**, addressed by chunk number mod its size. Its state is three words:
the chunk at its head (the ring's next), the chunk at its tail, and its
size. The ring's rule is unchanged, and so is everything past it.

The reader (`vp_fill`, the bracket's foreground) does, in order:
1. **the ring has room and the bank has its next chunk**: copy it DOWN
   (`vp_xcopy`, DI = 1) into the slot, and the mirror after it as today;
2. **the ring has room and the bank is empty**: read the disk STRAIGHT into
   the slot, as today - no copy at all. This is what keeps a bank from
   costing a byte read while the disk is keeping up;
3. **the ring is full and the bank has room**: read the next chunk into a
   bounce claim on the BANK'S OWN cursor and copy it UP - `vp_xstep`'s body,
   run from the bracket's loop rather than the window's timer;
4. else nothing: wait a period, as today.

**Two cursors, handed over, never re-seeded.** The stream's cursor
(`[vp_cur]`) and the bank's (`[vp_xcur]`) both read the same file forward.
Step 3 advances the bank's; step 1 advances only the head. When the bank
empties, step 2 takes over **with the bank's cursor copied into the
stream's** - the FSEQ block is the state of a forward read, and copying it
is free. `vp_xfill` today zeroes the stream's cursor "but for FSEQ_OFF" so a
later disk read seeds again from the name (SPEC.md 18.4.8) - affordable
once a play for a hold, and NOT for a bank, which empties and refills many
times a play: a seed walks the chain from the front (98.1's 142 ms a MB on
the W0 machine, a 5150), which at 50 MB into `08-486` would be seconds of
disk on any machine.

**A pause fills the bank for nothing.** While paused (98.3.4) the reader's
loop runs and the hook draws nothing, so step 3 has the whole machine: on
the owner's 286 (1,318 KB/s idle, and a copy ESTIMATED at 0.40 ms/KB) ten
seconds paused is ~8.6 MB banked.

### 4.2 Why a banked byte is copied twice, and it cannot be once

The ring is contiguous in stream order - a super-packet straddling two slots
is contiguous in memory because slot *c* + 1 IS chunk *c* + 1, and the
mirror is that rule at the wrap (98.3). So the chunk a freed slot takes must
be the stream's next, and while the bank holds anything, the bank holds it.
A chunk the disk reads while the bank is non-empty therefore goes to the
bank's tail (up) and comes down later (down). Breaking that would mean a
chunk-to-slot map and a straddle the ring can no longer make contiguous -
every super-packet decode would change. **So a byte is banked only if the
bank was non-empty when the disk read it**, and that is nearly every byte of
a clip that keeps the bank in use: 87-98% in every run of section 3.2.

What it costs: one disk read and two copies a banked byte, where a direct
one is the read alone. The two copies fall at different times - UP when the
ring is full, which is when the decode has left CPU over; DOWN when a burst
is spending the bank, which is when the CPU is scarce - but a copy down is
cheaper than the disk read it replaces on a CPU-copied disk (0.25-0.40
ms/KB against 0.76), which is why the 286 still gains.

### 4.3 The prefill

A play that has a bank fills it before the first frame - the request's
"buffer the full ram at the start of play". The full screen says
**`Buffering 1,234 of 8,192 KB`** (98.3.13's box), updated a chunk at a
time, with **the time left estimated from the rate the fill is actually
reading at** - `Buffering 1,234 of 8,192 KB, ~24 s` - so a slow disk says
so; **Space starts the play at once** with what is in; Esc cancels.

**How much is the ENCODER's decision, and the player has no cap of its
own** (the owner, 2026-10-08): the encode says it in seconds of its own
byte rate, or UNLIMITED - everything the machine's bank holds. **The
default is 10 s at the encode's rate.** So:
- a file that asks (section 5) is filled to its ask, or to the whole bank if
  it asks for unlimited; a bank smaller than the ask plays anyway and says
  `Low memory` (98.3), the file being short exactly where it was banked;
- a file that asks nothing - every file made before this - is filled for 10 s
  at its own mean rate (its stream's bytes over its length, two dwords the
  player already has), so an unbudgeted file gains from a bank's smoothing
  (section 3.2) without anybody waiting a minute for it.

In the window (98.3.7) the same loop runs and the box says it.

### 4.4 What else a bank meets

- **A seek** (98.3.14) inside the banked range moves the head; outside it,
  flushes the bank and seeds the stream's cursor as today. Seeking back is
  the disk's.
- **Repeat** (98.3.9): the seam's read is the disk's, and the bank is
  flushed at the lap; the next lap's first chunks refill it.
- **F and Esc, brackets ending and starting**: the bank outlives a bracket
  as the ring does not - a play stopped and played again from the same
  key keeps what is banked.
- **Live** (98.3.10) is not a bank's: it plays from a whole hold or not at
  all, and keeps doing so.
- **A file that fits** keeps SPEC.md 98.3.18's hold, unchanged: the hold is
  strictly better where it is possible.

### 4.5 The 286's own problems

- **The interrupts-off window.** On tier 1 the whole copy is masked
  (`int 15h AH=87h`). At 0.25-1.0 ms/KB a 32 KB piece is 8-32 ms, and the
  rate hook - every 20.8 ms with a card (98.3.1) - waits it out. The hook
  draws an owed frame on its next call, so a late call is absorbed unless the
  frame was already near its period; the Sound Blaster's DMA does not stop,
  but its block interrupt waits too, and a 512-byte block at 22 kHz is
  23 ms. **The piece size is a measured choice**: VIDDISK `X` gives the
  fixed cost at 4-32 KB, and the bank copies in the largest piece whose
  window is under half a block.
- **Calibrate, don't guess** (SPEC.md 47). The player times its first bank
  copy with the PIT (benchlib's method, a dozen instructions) and refuses the
  bank - saying so on the card - where a KB costs more than the disk read it
  replaces, which is the line section 3.2 puts the 1 ms/KB column on the
  wrong side of. An unbudgeted file then plays as it does today; a file
  encoded for a bank plays with `Low memory`.
- **The worker rule.** `OSAPI_XMEM_COPY` is UI-task only (SPEC.md 41.8): the
  bracket's foreground is the UI task, which is where the copies already
  are; the hook never touches the bank.

### 4.6 How big

The pool is shared (eight blocks, SPEC.md 41.5) and other programs use it,
so the bank asks for what is free less a reserve - 256 KB, enough for a
clone's round (18.99.4) - and no more than the file. It is freed when the
file closes, and the kernel frees it with the instance.

### 4.7 What it costs

**No kernel byte.** `VIDEO.O88`: the bank's three words, the policy in
`vp_fill` (step 1 is `vp_xfill` with a modular address; step 2 is today's
path; step 3 is `vp_xstep`'s body), the cursor hand-over, the prefill's
loop and line, the calibration, the flush on seek and lap - ESTIMATED at
400-600 bytes against 98.3.18's measured 955 for the hold.

## 5. The encoder and the file

- **`--xms KB`** deepens the disk bucket by that much (`Encoder.reserve`,
  98.2.1.3), the way `--memory` sizes the ring; **`--prefill S`** starts it
  that many seconds of disk fuller, up to its depth, where it starts half
  full today ("the player fills its ring before the first frame").
- **The header says so, with NO FLAG** (the owner, 2026-10-08: the disk may
  well keep up with the encode, and older players are not a concern). Two
  words in the header's zero tail at 472 (98.1.1): the bank the encode
  assumes, in KB, and the prefill, in tenths of a second at the encode's
  rate - FFFFh for unlimited, 0 for "say nothing" (the 10 s default). **An
  older player plays the file**, because no player reads that tail - it
  reads named fields (`V88_LEAD0` at 464, `V88_KLEADS` at 468) and nothing
  past them - and stalls where it is short. The host `Reader` DOES check the
  tail (`any(d[tail0:SECTOR])` names it "a loop block with no LOOPREC
  flag"), so it learns the two words in the same change; a host tool older
  than the file is the only thing that refuses it.
- **A profile for a bank is a VIDDISK `X` reading.** `disk_at` is already
  the disk's rate under load (98.2.1.3); a bank's refill is the `READ_SEQ +
  copy up` row, so a `286-vga-xms` profile is that row in place of the
  `READ_SEQ` one, and the encoder needs nothing else to price it.
- The window (98.2.8) gets the two boxes beside `--memory`'s.

## 6. Is this a bad example?

**It is a hard one, and a fair one.** A bank is worth what a clip's calm
stretches save for its bursts, and Last Exile's opening has few: its typical
second is its mean. Two consequences:
- these clips gain 10-25% from a bank of a few MB, and the rest only by
  waiting. A clip that is mostly talking heads, credits and slow pans, with a
  few fast cuts, would gain several times more for the same bank - its mean
  is far below its peaks, which is exactly the gap a bank closes;
- for a VGA DEMO video the wait may be the right trade: a 30 s "Buffering"
  in front of 93 s of near-lossless video off a 285 KB/s disk is a demo a
  period 286 with 8 MB could show.

**The original video would help, and is not needed for the design.**
Section 3.3's deficit is a proxy; with the source, the same clip can be
encoded at 250 KB/s with each bank size and its error AS SEEN read off the
encoder's own summary - the number the window shows - instead of a share of
bytes. That is the measurement to take before section 5 is built.

## 7. Considered and not proposed

- **EMS instead of XMS** - section 10. It was listed here as not proposed;
  the owner asked for it to be costed, and it came out the better store.
- **LOADALL for the 286 copy** (HIMEM.SYS's trick: no reset). Undocumented,
  286-only, and the kernel's transport, not the player's (SPEC.md 41.9).
- **Keeping what has been played**, as the hold does: a bank that kept its
  past would serve seeks back, but it is the same bytes spent on the
  direction the play does not go.

## 8. The owner's answers (2026-10-08)

1. **VIDDISK `X` on the 286 and the 486**: being run, together with the
   machines' own disk rates - which are slow even for period drives in the
   machines being tested, so the readings are the floor this plan is for.
2. **The prefill**: no cap in the player; the encoder sets it, in seconds at
   the encode's rate or unlimited, default 10 s, and the player estimates and
   shows the time left as it fills (section 4.3).
3. **The format**: no flag; an older player plays the file (section 5).
4. **The original video**: coming, for section 6's real encodes.
5. **EMS**: wanted - "investigate what EMS would cost us to implement in the
   kernel". Section 10.

## 9. Waves

0. **Measure** (this document): VIDDISK `X` on the 286 and the 486, and the
   source re-encoded at a period rate per bank size.
1. **The bank in the full-screen play** - **BUILT (SPEC.md 98.3.18.2)**:
   section 4.1-4.2, 4.5, 4.6 - a file that does not fit plays with a bank,
   smoothing only (no header ask). The gate is `tests/vidbank.py` on QEMU,
   and it came out a different shape from the one written here, for two
   reasons: B: is a 1.44 MB floppy, so the clip is 1.33 MB against `-m 2`
   rather than 6 MB against `-m 4`; and with no prefill yet, what fills the
   bank before the swap is the gate's own HOLD (`vp_stopat`) - the hook
   stopped, the ring full, and every idle pass of the reader a chunk up.
   The picture is compared with the decode at every key frame, so a bank
   that delivered chunks out of order or short is a wrong frame and not a
   count. One defect worth writing down: `OSAPI_FILE_READ_SEQ` answers the
   bytes in DX:AX, so the bounce segment held in DX across the call was
   ZERO afterwards and the first build banked the interrupt table - the
   ring's own `vp_fill` pushes DX for exactly this reason. Swapping to a
   blank floppy under QEMU does not make the stream's cursor refuse: it
   reads the blank disk's zeros, so past the bank's end the play errors
   rather than stalls, which is still the bite the gate needs.
2. **The prefill and its line**, Space and Esc during it - **BUILT (SPEC.md
   98.3.18.3)**. The line is `Buffering 37% ~24s` rather than section 4.3's
   `Buffering 1,234 of 8,192 KB, ~24 s`: the full screen's box held ten
   characters, and every character more is 96 bytes of the player's bss
   (a 13h glyph's 8 bytes by 12 rows, on two pages), so it went to 18 for
   1,536 bytes and the KB went. The first build redrew the box on every pass
   of the loop, which the gate caught as a box found mid-redraw - and which on
   the glass is flicker.
3. **The encoder's `--bank` / `--prefill` and the header**, the player's ask,
   `Low memory` - **BUILT (SPEC.md 98.2.1.3.2)**. Two departures from section
   5: `--xms` was already the encoder's (98.3.18.1's streamed Live file), so
   the bank is `--bank`; and the header's prefill is in KB, not tenths of a
   second - the encoder knows exactly what it banked on, the player only
   the file's mean, and seconds at "the encode's rate" would have been two
   different numbers at the two ends. The seconds are the encoder's option
   and the player's estimate, which is what the owner asked for. The
   profile's VIDDISK `X` reading went in as `xcopy` (section 5's last-but-
   one point) - a banked byte's two copies charged to the disk's rate.
   `--deficit` was not re-checked: the real encode is the better number now
   (98.2.1.3.2's table, on the owner's source).
4. **The window play, seeks inside the bank, a pause's banking** - **BUILT
   (SPEC.md 98.3.18.4)**, and with them a defect the owner's 286 found in
   wave 1 that no emulator here could: **the bank and the PC speaker cannot
   both run** (SPEC.md 98.3.18.5). The speaker's clock is its own
   interrupts and every copy masks them, so a banked play through the
   speaker ran ~19% slow and said nothing late. Section 4.5's "calibrate,
   don't guess" was about the RATE hook, which a copy only delays; the
   speaker was the case it did not price. The window's line is the thumb
   rather than section 4.3's text, the window having no text of its own in
   a bracket. Not done: section 4.5's PIT-timed refusal of a bank whose copy
   costs more than the read it replaces, and section 4.4's bank that
   outlives a stop - a play from the same key again starts behind the
   bank's head, which a FIFO ahead of the ring cannot serve.

## 10. EXPANDED MEMORY: what LIM EMS would cost the kernel

**The short answer: about 100 resident bytes on `kern_big` and none on
`kern_small`, for a user-ticked `EMS.DRV` that packages reach through
`OSAPI_DRV_CALL` - and for the video bank it is the better store on BOTH
of this project's slow machines**, because EMS is MAPPED where XMS is
copied, and the one thing an 8088 cannot afford is copying.

### 10.1 Why it is worth having

- **It reaches the 8088.** XMS needs a 286. A LIM EMS board sits in an
  8-bit slot of a 5150 - the Intel AboveBoard, AST RAMpage, BocaRAM and
  today's Lo-tech 2 MB board - and **the owner's 5150 #2 has one already**:
  its PicoMEM provides EMS through the Lo-tech driver (docs/FIELD-MACHINES.md;
  the PicoMEM project says its EMS uses LTEMM, so it most likely answers at
  the Lo-tech defaults, 260h and frame E000h - unconfirmed).
- **It is reached by OUT, not by a mode switch.** A board has four page
  registers; writing one maps a 16 KB page of the board into one quarter of
  a 64 KB frame below 1 MB. No `int 15h`, no reset, no interrupts masked,
  and the CPU then reads and writes the page as ordinary memory.
- **It is testable here.** The pinned MartyPC (0.4.2) models the Lo-tech
  2 MB board (`crates/marty_core/src/devices/lotech_ems.rs`), so an EMS
  path runs cycle-exact on an 8088 - unlike XMS, which is QEMU's alone. 86Box
  models seven boards (`src/device/isamem.c`: Lo-tech, Intel AboveBoard,
  AST RAMpage/XT, Everex EV-159, BocaRAM XT and AT, Micro Mainframe
  EMS-5150, AST MegaPlus II), so the period ones can be looked at too.

### 10.2 Measured: VIDDISK `E` on MartyPC's 8088 with a Lo-tech board

A new profile, `os8088_5150_herc_hdd_sb_ems_gla` - `herc_hdd_sb_gla` with
`[machine.ems]` at the board's defaults and nothing else changed - and a new
VIDDISK key, `E`, which probes the board (an option ROM anywhere in the
frame is refused with no port written; then pages 0 and 1 are mapped and
written, page 0 mapped into both quarters, and a write through one must be
seen through the other), sizes it by a signature per page written from the
top down (a smaller board aliases), and times what a bank would do:

| row | 4.77 MHz 8088 |
|---|---|
| board found | **128 pages = 2 MB** |
| map the whole frame (four OUTs, a call and a loop) | **143 us** |
| 16 KB `rep movsw`, frame -> RAM | **45.0 ms = 2.75 ms/KB** |
| 16 KB, RAM -> frame | 45.0 ms |
| 16 KB, RAM -> RAM (the baseline) | 45.0 ms |
| 32 KB `READ_SEQ` for 5 s into RAM (XT-IDE) | 151.9 KB/s |
| **32 KB `READ_SEQ` for 5 s STRAIGHT INTO THE FRAME** | **151.9 KB/s**, and the last chunk read back through the frame is STREAM.DAT's bytes |

Verified both ways: on the same machine with no board, `E` says `no paging
board at 260h / E000h` and times nothing; with the read aimed at the buffer
instead of the frame, the frame check says `BAD`. MartyPC prices the board's
memory as RAM (no wait states); a real Lo-tech board on a 5150 is on the
same 8-bit bus as the system RAM and should be no slower, while **a PicoMEM
adds wait states** to its EMS (its own documentation), so its copy rows will
read slower - which is what running `E` on 5150 #2 will say.

### 10.3 What it means for the bank

- **Filling is free.** A disk read lands in the frame at the disk's own
  speed: the bounce buffer and the copy UP of section 4.2 are gone, on any
  CPU. That alone halves the bank's copy traffic against XMS.
- **Draining by copy is NOT affordable on an 8088.** 2.75 ms a KB, and a
  bank in use passes almost every byte (section 4.2): at the ST-225's
  119 KB/s that is **33% of a 5150**, beside a decode budgeted at 50%. So on
  an 8088 the bank must be **decoded where it lies** - map, don't copy:
  - the ring BECOMES the bank: chunks live in EMS pages, and the hook
    decodes from the frame;
  - a super-packet of up to 32 KB starting anywhere touches at most THREE
    16 KB pages, so three quarters are the DECODE WINDOW, remapped as the
    hook enters each super-packet - three OUTs, ~0.25% of the machine at 25
    a second - and contiguous, which is what the ring's mirror exists to
    make (98.3) and here costs nothing;
  - the fourth quarter is the READER's: 16 KB `READ_SEQ` calls into it, a
    page at a time, its register its own, so the hook and the reader never
    write the same register. What the smaller call costs, VIDDISK `R` on the
    same machine: 32 KB in 212.8 ms and 16 KB in 116.7 - **10% more a byte**
    on a CPU-copied disk, the call's fixed part spread over half the bytes;
  - the conventional ring - up to 15 x 32 KB on a 640 KB machine - is no
    longer needed for the stream at all.
  The hard parts, stated: the SOUND's audio cursor reads records ahead of
  the picture (98.3.1, 98.1.8), possibly in the next super-packet, which the
  three-quarter window does not hold; a BIGSP super-packet (98.1.4.1, up to
  63.5 KB) needs five pages and does not fit at all - it is a 486 file, and a
  486 has XMS in unreal mode at 0.05 ms/KB; and the hook is an ISR, which may
  call nothing, so it does its own OUTs from a recipe the driver hands over
  (10.5). On a 286 the in-place design is not required - a copy down out of
  an 8-bit card is ESTIMATED at ~1 us a byte, one copy where XMS needs two,
  with interrupts ON - so wave E2 can be the hybrid (section 4's policy, the
  fill free and the drain a `rep movsw`) and the in-place decode wave E3.

### 10.4 The boards: two register families

From MartyPC's and 86Box's models and Lo-tech's own documentation:

| family | boards | registers | a page's value |
|---|---|---|---|
| CONSECUTIVE | Lo-tech 2 MB (and so PicoMEM, MartyPC) | base .. base+3, base 260h/264h/268h/26Ch (the board offers 040h/060h/240h/260h by jumper), frame C000h/D000h/E000h | the page, 0..127; **write-only** |
| SPACED | Intel AboveBoard, BocaRAM, AST RAMpage/XT, Everex EV-159, EMS-5150 | quarter *q* at base + *q* x 4000h (base 258h, 268h, 2A8h, 2B8h...), frame set by the board's own switch or registers | 80h + the page (bit 7 enables); readable on most |

Two backends of a few dozen bytes each. **Chipset EMS** on 286 boards (the
NEAT, SCAT and Headland sets map system RAM into a frame through their own
index registers) is one backend per chipset and is not proposed until a
machine in the field has one. **A 386 has no EMS hardware** - EMM386 makes
it out of paging in V86 mode, which a real-mode kernel cannot offer, and does
not need to: its XMS is the fast unreal-mode copy.

### 10.5 Where it lives, and what the kernel pays

**Recommended: `EMS.DRV`, a driver the user ticks, with a class of its own.**

- **A tick and not a sniff.** XMEM.DRV's boot sniff is EXACT (`int 15h
  AH=88h`, SPEC.md 41.12.1), so it is an overlay nobody ticks. EMS has no
  such question: a Lo-tech board has no ID register and write-only page
  registers, and asking means WRITING PORTS on a machine that may have
  something else at 260h. So the user says the board is there - on the
  Drivers page, with the port and frame on the driver's own page (DSV_CPNAME,
  which is driver code, not kernel code) - and the probe VERIFIES it, the
  `E` row's probe being the worked example.
- **Packages reach it through `OSAPI_DRV_CALL`** (`DSV_PKGCALL`, SPEC.md
  20.11), as they reach ETHER.DRV's sockets, so it needs **no new API
  cell**. The verbs: CAPS (pages free and total, the frame segment); ALLOC
  (pages -> a handle, stamped with the calling instance) and FREE; MAP (a
  quarter, a handle, a logical page); FRAME (claim quarters EXCLUSIVELY -
  the frame is one machine-wide window, and two owners of a quarter would
  corrupt each other - and get back the RECIPE an ISR needs to remap a
  quarter itself: the register's port and the value to add to a page, which
  hides the two families from the package).
- **The kernel's bill**, from the CH375 USB mouse's MEASURED one (SPEC.md
  9.12.5: a row, its two strings and its tick, 45 bytes of `.text`; a new
  class's publication slot, 38 of `.bss`; `drv_cfgbit`, 1 of `.ovl`), plus
  one thing that class does not need: **`DSV_RELINST` delivered to the new
  class** - today the teardown calls only the SOUND class's
  (`snd_release_inst`), and ETHER.DRV publishes 0 there - so a handle left
  by a package that died would stay allocated until the driver unloads.
  That is ~10-15 bytes of `.text`, ESTIMATED. **~95-100 resident bytes in
  all, every one of them ESTIMATED by analogy until the row is built, and
  `kern_small` pays nothing because it has no driver layer** (SPEC.md 51.0).
- **The driver itself**: probe, two backends, a page bitmap (16 bytes for
  128 pages, 32 for 256), the handle table, the verbs and its page -
  ESTIMATED 1.2-1.8 KB of image on the system disk, read only on a machine
  that ticked it.

**Two alternatives, priced the same way:**
- **An app-side library** (`apps/os88ems.inc`, `os88gfx.inc`'s shape,
  SPEC.md 5.12): ZERO kernel bytes and reachable from `kern_small`, but no
  allocator, no teardown and no sharing - two packages using it at once
  corrupt each other, and nothing frees a page when one dies. Good enough
  for the Video Player alone, which plays in an exclusive bracket; wrong as
  the machine's EMS.
- **Resident, as xmem.inc was before SPEC.md 41.12**: REFUSED for 41.12's
  own reason - every machine without a board would carry it for ever.

### 10.6 What else could use it, later

Each is a decision of its own, and none is in this plan's waves:
- **The DOS box's `int 67h`.** docs/plans/DOS-EXEC-PLAN.md 2.4 says EMS
  "should be refused rather than faked" - with a real board it is not faked:
  LIM 3.2's functions (40h-4Eh) over the driver's verbs would give a DOS
  program real expanded memory on an 8088, which is where most DOS software
  that wanted more than 640 KB looked for it.
- **The kernel's purgeable caches** (the directory read-ahead, the raise
  cache): mapped rather than copied, they would stop competing with packages
  for the heap.
- **Large documents** in Paint, Sheet and Word.

### 10.7 Not known yet

- **The PicoMEM's family, port, frame and wait states**: VIDDISK `E` on
  5150 #2 says all four (the probe refuses at once if it is not a Lo-tech-
  style board at 260h / E000h, which is itself the answer).
- **A real 286's 8-bit card copy**: VIDDISK `E` on a 286 with a board (86Box's
  Lo-tech board in the `286` profile is the emulated twin).

### 10.8 Waves

- **E0** (done): the MartyPC profile and VIDDISK `E`.
- **E1 - BUILT (2026-10-08), SPEC.md 107**: `EMS.DRV`, class 7, row 6,
  SYSTEM.CFG bit 7; the CONSECUTIVE family only, probed at E000h/D000h/C000h
  x 260h-26Ch with no setting; nine verbs through `OSAPI_DRV_CALL`. Two
  departures from 10.5, both cheaper: teardown is not `DSV_RELINST` but the
  package door itself with ES = `KERNEL_SEG` (`EMSV_GONE`, from
  `xm_release_rec`, so the loader's abort sweep is covered too), and the
  caller's identity is the instance slot `drv_pkg_call_x` puts in BH for
  this class alone. **113 resident bytes** where 10.5 estimated 95-100, no
  rung crossed, `kern_small` +0; the driver 1,206 bytes, 813 on the floppy.
  The gate is `tests/ems.py`, on MartyPC's board and on no board. The SPACED
  family (and so 86Box's AboveBoard) is still to come.
- **E2 - BUILT (2026-10-09), SPEC.md 98.3.18.6**: the video bank on EMS,
  the hybrid - section 4's policy with a free fill and a copied drain. The
  fill reads into the frame and the drain is one `rep movsw` with interrupts
  on, so the speaker's rule (98.3.18.5) does not bind it and no bounce slot
  is claimed. Taken only where there is no XMS pool; a machine with both
  keeps the XMS bank, which is the faster on a 286 for a card's play and the
  wrong one under the speaker - choosing per play is a later question. One
  defect worth keeping: `EMSV_CAPS` answers its quarters in SI, and the
  first build read the file's size out of SI after the call, sizing every
  bank at 2 pages and refusing it. The 8088's cost of the drain is still to
  be measured on a real play.
- **E3 - BUILT (2026-10-09), SPEC.md 98.3.18.7**: the in-place decode -
  the hook reading from the frame, the three-quarter window, the reader's
  quarter. The sound cursor's answer turned out to be that there was no
  question: it runs in the hook, between frames, so it maps the window as
  the video cursor does, and no pointer is held across another's mapping.
  Taken for the plain stream; BIGSP, sound ahead, Repeat and Live keep E2.
  With it, EMS is preferred over XMS below a 386 (`vp_epref`). Still to
  measure: the 8088's play at a period disk rate with the copy gone, which
  is the number E3 exists for.
