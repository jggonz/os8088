# VBATTLE-XMS-PLAN - the memory rework, all at once

The owner (2026-11-07): *"lets take one additional step and break down conv
memory usage too; lets make sure what we are doing is efficient ... I'd like
to do all our memory rework at once, so we don't have to keep coming back to
it"*, and *"for prefetch - as long as we can do that without going back to
making the menu sluggish (right now things on XT take 0.3-1.5s to respond,
and are nearly instant on 286)"*. SPEC.md 126.4.6 is the contract; this is
the story list and what was found.

## What the profile said

The mrbios 286, BATTLEV.VBT (15.3 MB, VGA4 at 15 fps), VBPROF1.DAT:

- the hold: XMS, **2,336 KB in 16 ranges, "filled"** - the builder's hold
  order (header 398) names 16 ranges: idle + music (1,564 KB) and 40 KB of
  each clip. The pool was larger; nothing else was ever held.
- 46 frames of sound late, all in heavy clips (CHAR1 20 and 13), every one
  streamed off the disk through a 104 KB ring with its sound 50 KB ahead of
  its picture.
- the disk busy 24% of the play at 372 KB/s: the rings, not the disk, were
  the limit.

Conventional memory on the owner's 565 KB (half the kernel and both drivers
in the HMA):

| | KB |
|---|---|
| the window (code, bss) | 46 (48 VBATTLED) |
| resident claim: tables 4, menu sounds 12, music ring 36, band buffers 24, band pictures 16, **idle loop 72** | 164 |
| the card's ring | 17 |
| the XMS fill's bounce | 32 |
| kept free (`VB_RKEEP`) | 8 |
| the profile's staging (VBATTLED) | 4 |
| two clip rings, **104 KB + a 32 KB guard each** | 272 |

## Stories

- [x] **S1 the cache.** Wherever 126.4.2's in-place check passes at the
  hold's open, the hold is a cache of whole clips: slots, FIFO, eviction
  that never takes a clip in use. A ring given a clip with a slot is in
  place on what has ARRIVED; the stream fills the slot. *Verify*: vbthold
  plays the demo with the pool under the bundle and every clip played
  twice reads nothing the second time.
- [x] **S2 the idle loop in the block**, decoded in place; the resident
  claim loses it. *Verify*: vbthold's title and menu exact on the glass.
- [x] **S3 prefetch**: the clip under the cursor whole, the enemy's whole,
  then every clip into room never used - 16 KB reads at a 286's menu, 4 KB
  at an 8088's, nothing for `VB_KQUIET` after a key. *Verify*: the profile's
  key-to-band latency (S6) on the owner's 286.
- [x] **S4 the rings**: two, ring 0 the run, ring 1 `VB_CRMIN` - fallback
  only once the cache is on. The guard is the largest record in 4 KB.
- [x] **S5 the builder's "XMS on the target"** (`target.xms_kb`): with room
  for the idle loop and two of the largest clips the cap prices a stream
  with no ring limit; the Build tab says what the cache holds.
- [x] **S6 the profile, version 4**: the pool, the cache's mode, KB filled,
  clips held whole, rings in place, evictions, and every key's time to its
  band's redraw.

## Not changed

- An 8088 with no EMS board, kern_mega's bracket, a slow board: as before.
- The music still streams through its ring (copied out of the block when the
  pool has room for it).

## What was verified (2026-11-07)

- `vbthold` (QEMU, 128 MB): the cache over the whole file - in place from
  the open, every clip filled into its place while the title went round,
  each byte for byte; the play to victory and again off a BLANK floppy
  with 0 disk reads; the idle loop at the menu and the title again exact on
  the glass and going round. Broken on purpose: a fill read from the file's
  start (every slot wrong), the idle loop read from the block's start (the
  menu frozen on one frame) - both red.
- `vbtholdpart`: 100 KB of slots - 5 evictions, the play to victory and
  again, every whole slot byte for byte. `vbtholdcopy`, `vbtholdorder`,
  `vbtholdnox`: the copying hold and the disk's play, as before.
- `vbtdiagx`: version 4 - the cache on, the idle loop in its block, rings
  in place, a key at the menu timed to its band.
- `vbtcap`: a 330 KB machine with 4 MB of XMS on the target holds ARI's
  attack only to the disk (12 frames against 22 in its 84 KB ring) - red
  without the cache's line in the cap.
- `vbtgui`: the memory lines; the rules battle's rings 180 -> 188 KB from
  the guard alone.

Not measured here: the owner's 286 (the profile's key latency and sound
late are the field's numbers), and an 8088 with a Lo-tech board running
the cache (`vbtems`, in the final pass).

## The owner's next profile (2026-11-07) and what is queued

VBPROF1.DAT on the 286 (a 2,678 KB pool): 0 sound frames late, 0 stalls,
16 slots evicted, menu keys 97 ms to their band on average and 247 at
most ("feels fine"). One video-only stutter in the scopes and the potion
that version 4 could not show - its frame gaps lapped at 55 ms - so
version 5 counts gaps in the hook's periods and times every XMS copy,
the suspect being an `int 15h` copy's interrupts-off window.

Queued after performance (the owner): **a clip that joins by a FADE, or
that goes to black itself, may have colours of its own.** The title fades
out through the engine's fade, so it could pick its own palette
automatically; 01-Opening fades to black itself, so it could too (or by a
setting). Victory into its ending does NOT fade and keeps the shared
palette though it joins the menu to full screen.
