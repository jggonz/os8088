# The Victory's broken plays are WRONG BYTES FROM THE DISK (2026-10-16)

**Machine**: the owner's Packard Bell Victory 286, a PicoMEM 1.x as the hard
disk (multi-sector firmware), no XMEM.DRV (deleted for the A/B), 8 wait
states. **File**: `GasGas54.V88` (1416 frames, 20 fps, 16 colours 640 x 400,
page-ahead, 23,424,000 bytes), muted. **Player**: VIDEOR.O88 at 11add1ff
(SPEC.md 98.3.22.2's read log). **Profile**: the one saved after the play
that stopped "damaged frame".

## The symptom

Plays of this file freeze (with sound) or stop on `a damaged super-packet`
/ `a damaged frame` (muted), at a point that is the same play to play in one
configuration and MOVES with the player (VIDEO against VIDEOD), the wait
states (14 against 8) and XMS's presence. Of four plays at 8 wait states
with no XMS: two froze, one stopped on a damaged frame, one played to the
end. The owner has seen the same message on earlier plays, before the XMS
in-place work existed.

## What it is not

- **The file**: it plays to its last frame in QEMU through the same player
  (tests: `vidxip --arm bankdry`/`bankdrysnd`, and the file itself off a
  32 MB IDE disk) in place through the XMS bank and off the conventional
  ring, at full disk speed and throttled under its own rate (250,000 B/s
  against 278.5 KB/s) - so running dry is handled.
- **The player's decode**: the read log sums each chunk the moment
  `OSAPI_FILE_READ_SEQ` returns, before any decode reads it.

## The read log

12 reads logged, file offsets 3,268,608 to 3,629,056, in 32 KB chunks from
a cluster-aligned start (the volume's clusters are 8 KB). **Two differ from
the file, consecutively**:

| read | file offset | file chunk | sum logged |
|---:|---:|---:|---|
| 9 | 3,563,520 | 108 | 9DC4 |
| 10 | 3,596,288 | 109 | D8D7 |

Reads 0-8 and 11 match. No 32 KB window of the file at any 512-byte
position within +/- 600 KB of the reads has either sum, so what came back is
not a whole chunk from elsewhere in the file (a wrong cylinder, a stale
32 KB buffer): it is a chunk partly wrong, which a whole-chunk sum cannot
place further.

## Reading

The bytes are wrong on arrival, intermittently, in a pattern that moves
with timing. The kernel reads a fixed disk to the end of the CYLINDER per
`int 13h` call (SPEC.md 18.91.5) - many heads in one call - and that section
already names the failure: a ROM that does not carry one call onto the next
head returns the wrong sectors with CF = 0. The PicoMEM's `int 13h` is its
own firmware, and its multi-sector mode reads the next run off the SD card
while the CPU copies the last. The A/B is `NOHDCYL=1` (track-bound reads,
kernel and HDD.DRV) on the same machine and file.

The player has a robustness gap beside it: with sound, bad data FREEZES the
machine where muted it is caught and named. Every byte off a disk is
hostile (CLAUDE.md, the package pipeline), so that is a defect to close
whatever the disk turns out to be doing.
