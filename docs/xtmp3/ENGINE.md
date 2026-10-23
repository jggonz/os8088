# XTMP3's engine: contract notes for SPEC.md §121.5 to §121.7

This file belongs to the engine track (stories S9, S6's live path and S8's
ladder, on `xtmp3/eng`). SPEC.md §121 is the contract. This file records what
the engine does to meet it: the numbers it chose, the handshakes, the formats
it writes, and the places where the contract and the code part ways. The code
is `apps/xtmp3/x3eng.inc` (the live path, the ladder, finished-file playback)
and `apps/xtmp3/x3conv.inc` (Decode first and the cache). It sits behind the
face's `x3_e_*` names and writes `x3_st`, as docs/xtmp3/FACE.md describes.

Everything below that runs was tested over the **stub backend**
(`make xtmp3stub`, never shipped), described in its own section. Nothing here
has decoded a real MP3 granule yet. See "What waits for the real backends".

## 1. The live path (§121.5, §121.6)

```
 file --(UI task: os88_rseq gulps)--> look-ahead ring (32 KB)
      --(worker: x3p_next / x3p_gran / backend GRANULE)--> PCM (one granule)
      --(worker: verb 6 stage, or a direct copy)--> the card's ring
      --(SB block IRQ)--> DAC
```

**Two tasks, one handshake.** The UI task does every file call. The worker
never touches a file (§20.6 rule 7). The worker asks for things by setting a
bit in `x3_req` and calling `OSAPI_WM_WAKE` on the main window (`x3_kick`).
The UI task's `W_ONWAKE` (`x3_onwake`) answers each bit and clears it:

| bit | set by the worker when | the UI task |
|---|---|---|
| `X3R_READ` | the look-ahead holds under `AP_LA_LOW` = 16 KB (`x3_lalow`, asked *before* the ring runs dry) | reads the next gulp into the ring (`ap_refill_chunk`, the Audio Player's code), timed for the ladder's disk share |
| `X3R_WRITE` | a conversion stage half is full | `OSAPI_FILE_WRITE_SEQ` (x3conv.inc) |
| `X3R_END` | the stream played out, or the source ended | the next track, or DONE |
| `X3R_STEP` | two slow windows | the live step down (section 3) |
| `X3R_CAL` | a calibration probe finished | the ladder's next phase |
| `X3R_FAIL` | the decoder refused (`x3_wfail` says why) | ERROR with the reason |
| `X3R_UIOP` | (set by a dialog) | the dialog's file action (section 5) |

**The look-ahead ring** is the Audio Player's `aplook.inc`, assembled from
the Audio Player's own source (section 8). It is one 35 KB claim: 32 KB of
ring (`AP_LA_SZ`, a power of two) and one granule's PCM (`X3_PCMSZ` = 2,304
bytes: 576 frames x 2 channels x 16 bits) after it. The PCM sits there
because the primary segment has no room for it. The claim is taken when a
track opens and given back at stop. The front end's `X3R` ring record is
aliased onto the Audio Player's variables (`X3R_SEG`, `X3R_RD`, `X3R_WR`
and `X3R_EOF` are `ap_la_seg`, `ap_la_rd`, `ap_la_wr` and `ap_src_done`).

**The card's ring** (`x3_ringget`) depends on the stream's bytes a second:

| output bytes a second | ring | why |
|---|---|---|
| under `X3_RINGHI` = 32,000 | the SND grant (verb 7): 16 KB, else 8 | the Audio Player's shape, out of the driver's pool |
| 32,000 or more | an EXTERNAL ring (`SND_OPENF_EXT`, §34.5.3): 32 KB of our own plus the control words, `OSAPI_MEM_CLAIM_DMA_HI`, written directly | the pool tops out at 16 KB, which is 93 ms of 44.1 kHz 16-bit stereo |

The external ring only exists above 22,222 Hz or in stereo or 16-bit. All of
those need DSP 3.00 or later, which always has the auto-init that an external
ring needs.

**The format** (`x3_ofmt`): §121.6's rule. 16-bit stereo when the card
answers both `SND_CAP_STEREO` and `SND_CAP_PCM16`. 8-bit stereo with
`STEREO` alone. 8-bit mono otherwise. Verb 11 runs immediately before verb 0,
in the same UI callback. The rate is the rung's (the source's rate divided by
the rung's divisor), held to the card's range (`X3_DEVMIN` = 4,000, then
22,050 without `SND_CAP_PCM_HI`). A clamp caused by the device is shown as
the device's limit, not as a quality step.

**The feed** (`x3_feed`, the worker, lock-free) uses SPEC.md 86.5's pattern.
Verb 3 reads the card's consumed count, which is the clock. Then up to
`X3_MAXFEED` = 8 halves of `X3_HALF` = 4,096 bytes are made and published
while the ring has room. On the external ring the total is published by
writing its control word; verb 1 is still called, because only verb 1
resumes an underrun. 4,096 bytes is the half on every SB regime (§34.5.5).
The 2,048-byte legacy half divides it.

**When the worker sleeps.** It sleeps one tick only when the ring's lead is
at least `x3_slp` = three blocks plus 110 ms of the stream's bytes (110 ms is
the two ticks a one-tick sleep can last). Otherwise it yields. The SB's block
IRQ needs the *next* block already fed (§34.5.2). After a wake, the first
half takes a rung near 1.00x about a half's time to make. The first rule
slept on half a ring, and that drained 176 KB/s's 32 KB ring dry at 1.05x,
before the slow windows could see anything. At 176 KB/s the worker now never
sleeps. At 11 kHz mono 8-bit it sleeps on about 13 KB of a 16 KB grant.

**Underruns** (§121.6). A verb-3 state of `SND_ST_UNDER` is counted once
(`x3_drvund` debounces it), unless the last half is already in, in which case
it is the end of the stream. A feed that has nothing to publish closes the
stream on a 32-frame ramp to silence (`X3_RAMP`) rather than a DC step. A
live underrun is also a step down (section 3). On a finished file it is
counted as a disk or device fault and never steps (§121.5).

**States** (`x3_md` behind `x3_st`'s state): `MD_CHECK` (calibrating, "Checking
performance"), `MD_LIVE`, `MD_FILE` (a finished WAV on the card), `MD_SPK` (a
finished WAV on the speaker or Covox), `MD_CHOOSE` (the Decode-first dialogs),
`MD_CONV`, `MD_STEP` (live, draining for a rung change), and `MD_IDLE`. Pause
closes the stream at the sample played (`x3_pos`), and resume re-opens it
there, with pre-roll on a live stream. Stop, previous, next and
seek go through `x3_live_at`: the look-ahead is re-aimed at the frame
`X3_PREROLL` = 2 before the target, those frames are decoded to nothing, and
the position the face shows stays the target.

**Teardown** (`x3_onclose`, `x3_park`): the job is set to `WJ_NONE` and the
UI task spins on `x3_wbusy` until the worker is between passes. Then the
stream is closed (verb 2), the external ring's claim freed, the grant freed
(verb 7), and the backend's work segments and its part dropped. The worker is
`OS88_WORKER_RESTARTABLE_NOKEEP` and dies at `OSAPI_TASK_ALIVE` when the
window goes.

## 2. Calibration (§121.5)

| constant | value | what |
|---|---|---|
| `X3_SAFETY` | 125 | a rung passes at 1.25 audio seconds a wall second |
| `X3_SLOW` | 110 | a live window under 1.10x is slow |
| `X3_FLOOR` | 8,000 Hz | no automatic rung below it while the source is above it |
| `X3_CAL_GRAN` | 24 | granules a probe times, at most |
| `X3_CAL_TICKS` | 18 | ticks a probe runs, at most (about 1 s) |
| `X3_CAL_HOPE` | 4 | a first granule 4x short of passing ends the probe |
| `X3_CAL_SKIP` | 2 | frames decoded untimed after a probe's seek |
| `X3_WINGR` | 32 | granules a live slow window spans |
| `X3_NCAL` | 8 | probe results the session keeps |
| `X3_OFFMAX` | 8 | offline Auto: the best rung converting in at most 8x the track's length |

**The probe order is not the ladder's order**, and that is what bounds the
wall time. Reduced-rate STANDARD does strictly less work than full rate
(§121.4: subbands above 32/div are never reconstructed), so the cheapest
valid STANDARD rung is probed first. If it fails, every dearer STANDARD rung
is skipped without a probe. If it passes, Original is tried, then Half if
Original failed. Then XT Standard and XT Fast are tried in the same way. An
8088 therefore probes three rungs (Quarter, XT Standard, XT Fast) and never
Original or Half.

**A probe** decodes the actual track from its start, with no pre-roll to pay
and the reservoir genuinely empty. It times, with `pit_now`, the front end,
GRANULE, and the feed's copy into the card's ring (verb 6). The disk's share
is the priming reads' measured cost per byte times the bytes the probe
consumed. A warm-up granule runs untimed first. Before it existed, the first
probe of a session ran slow by the cost of every cold cache. `pit_now` can
step back one tick under QEMU, because the BIOS tick and the 8253 disagree
across a pending IRQ0. `x3_pitd` treats a negative span as zero, and the
stub's own clock adds the pending tick.

**The session cache** (`x3l_cached`, `x3l_store`): up to 8 results, keyed
by fingerprint, rung (backend, profile and divisor together) and device
(the format bits and the rate cap). A second play of the same track asks no
probe.

**Live step down.** Each live granule's wall time, from the front end
through the feed's copy, is summed over `X3_WINGR` granules (`x3_window`).
A window under 1.10x counts. Two in a row post `X3R_STEP`. A window at or
above 1.10x clears the count. That is the hysteresis. One underrun also
steps. The step drains the card's ring (no new half is published, the card
plays what it has, and the position is kept to the sample). The stream is
then closed, the rung below opens at the position the card reached, and
`X3_PREROLL` frames of pre-roll run. **Nothing ever steps up
automatically.** An override (Options > Quality) is shown with `(set)` and
is never stepped.

The disk's time is not in a live window. A read stalls the worker while the
ring plays on, so a disk that cannot keep up shows as an underrun, and the
underrun steps down by itself.

**Without a card** there is nothing live to choose (§121.6: MP3 is never
decoded into the speaker), and the same probes become the conversion's
estimate. The offline choice (`x3l_offpick`) is the highest-quality rung
measured at `1 / X3_OFFMAX` or better.

## 3. The ladder's rows

`x3_rungs` maps each `X3Q_*` to (backend, profile, divisor): Original
(STD, 0, 1), Half (STD, 0, 2), Quarter (STD, 0, 4), XT Standard (XT, 0, 1),
XT Fast (XT, 1, 1), XT Turbo (XT, 2, 1), and XT Speech (XT, 3, the divisor
set by the source). One backend is resident at a time (§121.3):
`x3_be_get` drops the other part before fetching, checks the part's header
(`'X3'`, ABI, verb count), and banks the far vectors.

## 4. Decode first (§121.7)

**The drive dialog** is a window of our own (`x3c_dopen`). It shows the
brief's sentence exactly: *This computer needs to decode this track before
playing.* / *Choose a hard drive for the decoded audio.* Below that it lists
every `VK_FIXED` volume (`OSAPI_VOL_KIND`) with its letter, its free space
(`GOTO_QM` + `VOL_STAT`), and whether it can be written. A volume is greyed
with its reason: read-only, too small for the estimate, a cluster the stage
cannot use, or gone. It never assumes C:, never lists a floppy, and says so
when there is no hard drive. It also shows the quality, the PCM the job makes
(rate, channels, bits) and the disk it needs: `frames x channels x bytes`
plus the header cluster, in 32-bit arithmetic, with a `~` when the duration
is an estimate. The last destination is preselected. When more of the queue
follows, a switch approves the batch, and an approved batch reuses the
folder without asking again.

**Volume labels are not shown.** No slot gives a package a FAT volume label
(the kernel's directory walk skips the label entry), and a driver's desktop
label belongs to the driver. This is a gap in the OS, not a choice, and it
needs a slot. See "Open items".

**The Save dialog** is the Standard File dialog (§38). The instance stands
in the chosen volume first (`GOTO_QM`), in the last destination's folder
when that folder is on it. `x3c_onsave` refuses a folder that is not on a
writable fixed volume, a name that already exists, and too little room. The
`.WAV` extension is forced. The dialog comes back after a refusal. The Save
dialog's window is unbound (billed to no instance). A file call made from
its callbacks would move the machine's current folder rather than ours, so
its actions are posted to the main window's wake (`X3R_UIOP`, `x3c_uiop`).
The dialog's own window slot is watched (`OSAPI_WM_OWNSEG`) to see a cancel.

**The conversion** (`x3c_start`):

1. The stage: two halves, each `max(8 KB, the volume's cluster)`, in one
   claim, 512-aligned by construction.
2. `X3nnnnnn.TMP`, the first free name in the folder, created by writing its
   header cluster with `OSAPI_FILE_WRITE`. A `JUNK` chunk pads the header to
   exactly one cluster, so every later write is whole clusters.
3. The worker (`WJ_CONV`, `x3c_wpass`) decodes into the half it owns and
   posts `X3R_WRITE`. The UI task writes the other half with
   `OSAPI_FILE_WRITE_SEQ` (`WSEQF_HELD | WSEQF_KEEP`, §18.4.9). Each half is
   a byte handshake, written last by the producer and cleared last by the
   consumer.
4. The progress window, at most twice a second (`XC_DRAWT` = 9 ticks):
   output, quality, decoded audio against track time, percent, wall time and
   speed (`0.31x`).
5. **Cancel** (Esc or the button) is answered within one granule batch. The
   worker parks, the token is abandoned, and **only this job's** temporary
   file is deleted. The measured time was 0.22 s from the key to READY.
6. **Failure** for a full disk, a write error, a volume that left, or a
   source that changed: the job stops with its reason, the temporary file
   goes, and nothing plays.

**Finalisation** (`x3c_final`): the decoder's tail is drained and the LAME
delay and padding trimmed (529 + the tag's encoder delay at the head,
padding - 529 at the tail, x3sim's arithmetic). The stream is committed. The
RIFF and `data` lengths are written with `OSAPI_FILE_WRITE_AT`. The source's
fingerprint is taken **again**, and a changed source fails the job. The file
is re-read and its header checked. Only then is it renamed to the `.WAV`,
entered in the index and played (section 6). **Only a finalised file is
ever played.**

### The WAV XTMP3 writes

| offset | what |
|---|---|
| 0 | `RIFF` size `WAVE` |
| 12 | `fmt ` 16: PCM, channels, rate, bytes a second, frame bytes, bits |
| 36 | `xtm3` 44: the identity (below) |
| 88 | `JUNK`, to 8 bytes before the end of the first cluster |
| cluster - 8 | `data` size, then the samples from the second cluster's first byte |

The `xtm3` identity (other readers skip an unknown chunk):

| at | size | what |
|---|---|---|
| 0 | 1 | version (1) |
| 2 | 4 | the source's bytes |
| 6 | 4 | CRC-32 of its first 16 KB |
| 10 | 4 | CRC-32 of its last 16 KB |
| 14 | 2 | `X3_DECVER` |
| 16 | 2 | `X3_TABVER` |
| 18 | 1 | backend (`X3K_*`) |
| 19 | 1 | profile |
| 20 | 1 | divisor |
| 21 | 1 | rung (`X3Q_*`) |
| 22 | 2 | output rate |
| 24 | 1 | channels |
| 25 | 1 | bits |
| 26 | 4 | output frames in `data` |
| 30 | 9 | the source's stem, NUL-terminated |

The fingerprint is size + both CRCs (12 bytes). The CRC is a nibble-table
CRC-32 (64 bytes of table rather than 1 KB), and it reads the file's two
ends once a session, through the ring's claim (about 1 s on an XT, by the
read count).

### `SYSTEM\APPDATA\XTMP3.DAT`

An 8-byte header (`'X3DT'`, version 1, a count) and 16 records of 48 bytes
(776 bytes, through `os88data.inc`'s `od_read`/`od_write`, standing in the
launch folder):

| at | size | what |
|---|---|---|
| 0 | 12 | the fingerprint |
| 12 | 2 | decoder version |
| 14 | 2 | table version |
| 16 | 1 | rung |
| 17 | 1 | channels |
| 18 | 2 | rate |
| 20 | 1 | bits |
| 21 | 1 | the WAV's volume |
| 22 | 2 | its folder's cluster |
| 24 | 13 | its 8.3 name |
| 37 | 9 | the source's stem |
| 46 | 1 | 1 = a record |

A full index drops its oldest record. The file that record named is left
alone.

**Cache reuse** (`x3c_cachefind`) needs no prompt. A record whose fingerprint
and versions match is opened and its own `xtm3` chunk checked
(`x3c_wavchk`): the identity must match again, the `data` length must equal
frames x frame bytes, and the file must hold that many bytes. A record whose
file is gone, changed or incomplete is dropped, and the file is not touched.
With a card the match is **strict**: the cached format must be one the card
plays as it is, otherwise the track is converted again. Without a card any
finished file of the track is good enough, because the speaker plays any
of them.

**Decoded Files** (`x3c_dfopen`) lists the index's files and deletes only a
file whose own `xtm3` chunk says XTMP3 made it, after an `ADANGER` question
(`os88ui_ask`). The deletion is posted to the main window's wake, because the
dialog is unbound (`XU_DELETE`).

## 5. Finished WAVs (§121.6)

**On a card** (`x3_file_play`, `WJ_FILE`): the decoder's part and its work
segments are dropped first, and the WAV's bytes go straight from the
look-ahead ring into the card's ring. No granule is decoded, and the
`playwav` leg proves it by the granule counter and an empty backend slot. An
underrun here is counted and never steps.

**Without a card**: the Audio Player's own speaker and Covox bracket
(§86.21, `apps/audio/apspk.inc`, assembled here unchanged) plays the file
inside its `FSXF_RATE` bracket. Seek (`x3_spk_seek`) restarts the bracket
at the new sample.

## 6. Memory

The primary segment (`APP_MAX_SIZE` 65,024, image + bss), measured from the
assembler's map at this branch's head:

| build | image | image + bss | left |
|---|---|---|---|
| shipped | 49,837 | 63,756 | 1,268 |
| `PKG_MEGA` | 50,734 | 64,653 | 371 |
| `X3STUB` (test only) | 50,878 | 64,970 | 54 |

The STANDARD track's real front end (`x3pars.inc`, about 5.5 KB of code plus
its tables in the primary) replaces the stand-in (`x3stubp.inc`, about
1.9 KB). **The merge does not fit as things stand**, by about 2.3 KB before
`x3pars.inc`'s bss is counted. The candidates for making room are listed
under "Open items".

Heap claims while playing (each one given back at stop):

| claim | size | when |
|---|---|---|
| look-ahead + PCM | 35 KB | a track is open |
| the card's ring | 16 KB grant (8 when the pool is short) or 33 KB external | live or a finished file on a card |
| backend work | the backend's `X3C_NSEG` x `X3C_PARA` | live, a probe, a conversion |
| the backend's part | its own size | live, a probe, a conversion |
| conversion stage | 2 x max(8 KB, cluster) | converting |
| probe buffer | 8 KB, transient | the queue's title and duration probe (the stand-in's; the real front end brings its own) |

`X3S_PEAK` (Status's `Peak NNN KB`) adds the look-ahead, the backend's
`TKB` and `PARTKB`, and 16 KB for the card's ring. It does not yet count the
external ring's extra 17 KB or the conversion stage. See "Open items".

## 7. The stub backend and the test hook

`apps/xtmp3/x3stub.asm` is an `OP_SEG` part that speaks x3gran.inc's ABI.
`apps/xtmp3/x3stubp.inc` is a front-end stand-in with real frame sync, ID3v2
skip, Xing/Info with the LAME delay and padding at x3sim's offsets, ID3v1,
duration, seekpos, and probe. Both are assembled only under `X3STUB=1`
(`make xtmp3stub` builds `build/xtmp3s.o88`). The stand-in front end is in
the shipped build too until the real one fits (section 6).

GRANULE makes 576/divisor frames of a 440.8 Hz square (the step is
655/65536 of the output rate), +-12,288 or 80h +- 30h, in the requested
channels. Its cost comes from the test hook: `X3STUB.CFG`, beside the
package, holds a speed x 100 for each rung, a slowdown rung, the granule it
starts at, and the slow speed. The primary copies it into the part at each
fetch (`x3_stubpoke`). A speed S makes each granule take its audio duration
x 100 / S of wall time, spun on channel 0 of the 8253 and the BIOS tick, so
the ladder's transitions are deterministic on any host. The stub never masks
interrupts. Its clock adds a pending IRQ0's tick rather than stepping back.

`X3LOG` (the stub build only) keeps a ring of 16 byte+word marks
(`x3_dlog`), frozen at the first underrun. The row prints it when a
slowdown check fails.

## 8. The Audio Player's source

`apps/audio/aplook.inc` is new. It is the look-ahead's `ap_la_avail`,
`ap_src_eof`, `ap_la_get` and `ap_refill_chunk`, split out of
`apengine.inc`, which now includes it at their old places. **AUDIO.O88
assembles byte for byte as before** on every arm (plain, `APROF`,
`PKG_MEGA`, `AP_HANDOFF`, and both together), checked with `cmp`.
`apspk.inc` (the speaker/Covox bracket) is included unchanged. XTMP3 defines
the Audio Player's names over its own state (x3eng.inc's shim block). No
Audio row needs re-running for these bytes. If someone wants a behavioural
check anyway, the rows that drive AUDIO.O88 are `apspk`, `apspkcard`,
`apspkcardc`, `apspkpause`, `apspkcovox` and `audhand`.

## 9. Where the code and §121 part ways

- **The step's pre-roll** is `X3_PREROLL` = 2 frames, the seek's. §121.5
  says one. The second frame covers a reservoir that reaches back two
  frames.
- **A step is audible.** A rung change changes the card's rate, so the ring
  is drained and the stream re-opened: a short gap rather than §121.5's
  "at the next granule boundary".
- **Volume labels** are not shown (section 4).
- **Offline Auto** picks the best rung converting in at most 8x the
  track's length (`X3_OFFMAX`). It can therefore pick Quarter over XT
  Standard when Quarter converts fast enough. §121.7 does not say which
  rung a conversion uses.
- **Decoded Files** is in the File menu, as §121.9 lists it. §121.7 says
  Options.

## 10. The row

`xtmp3eng` (soak, `tests/xtmp3eng.py`, QEMU because behaviour is what is
asserted): each leg boots its own private QEMU with its own disks under
`build/x3e*`.

| leg | asserts |
|---|---|
| `ladder` | five machines by stub speed: capable stays Original; middling takes Half or Quarter; slower Quarter; XT-class reaches Decode first with Original and Half never probed, within 6 s of guest time; hopeless the same, within 4 s (it measured 1.4 to 1.8) |
| `slowdown` | at 1.05x the windows step Original to Half with no underrun; at 0.60x one step, at most one underrun; never back up; position monotonic |
| `conv` | drive dialog, Save dialog, the WAV's every field and its frame count on the host, the finished file plays, a second Play reuses the cache with no granule, no probe and no part |
| `cancel` | Esc mid-conversion: READY, the temp file gone, nothing else touched |
| `full` | a disk filled mid-write: the error, the temp gone, nothing played |
| `stale` | a changed source and an incomplete WAV are both refused and re-converted (three boots) |
| `novol` | no fixed disk: the dialog says so |
| `twovol` | two writable volumes, the second chosen, the WAV lands there |
| `playwav` | a finished WAV on the SB16: no decoder resident, no granule, no underrun, the square's frequency in the capture |
| `pause` | pause, resume, rewind |

## 11. What waits for the real backends

- Integrating `x3pars.inc` and the FRONT/STD parts table (section 6), and
  the XT row. `x3_be_get` banks the part's **data** segment as the code
  selector, which is right on every real-mode kernel. On kern_mega it must
  be `op_cseg`, as std's `x3p_init` already does.
- Every timing figure. The stub's costs are set by hand. The real ones come
  from MartyPC (S10).
- Ring sizes against a real decoder's jitter. A real granule's cost varies
  by frame, and the stub's does not.

## 12. Open items

- **The primary's room** for the real front end: about 2.3 KB short.
  Candidates: x3conv.inc's dialogs, index and Decoded Files (about 7 KB, all
  UI-task and menu-frequency) as an `OP_SEG` overlay part, or the front
  end's open/probe path moved into FRONT.
- **A volume-label slot.**
- **`X3S_PEAK`** does not count the external ring's extra 17 KB or the
  conversion stage.
- **Untested**: a source changed during a conversion, the Decoded Files
  window's delete, batch approval over two tracks, a read-only fixed
  volume, and the speaker path's sound (QEMU checked only its state, since
  its 11 kHz IRQ0 runs slowly there).
