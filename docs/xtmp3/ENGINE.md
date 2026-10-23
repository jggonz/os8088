# XTMP3's engine: contract notes for SPEC.md §121.5 to §121.7

This file belongs to the engine track (stories S9, S6's live path and S8's
ladder, on `xtmp3/eng`). SPEC.md §121 is the contract. This file records what
the engine does to meet it: the numbers it chose, the handshakes, the formats
it writes, and the places where the contract and the code part ways. The code
is `apps/xtmp3/x3eng.inc` (the live path, the ladder, finished-file playback)
and `apps/xtmp3/x3conv.inc` (Decode first and the cache). It sits behind the
face's `x3_e_*` names and writes `x3_st`, as docs/xtmp3/FACE.md describes.

The engine runs over the STANDARD track's real front end (`x3pars.inc` in
the primary, the FRONT part). Its behaviour was first tested over the **stub
backend** (`make xtmp3stub`, never shipped, section 7); the integration track
(phase 3) then ran it end to end over the REAL decoders, and the `xtmp3live`
row holds what the card plays to `tools/x3sim.py` sample for sample (section
10). The timings are still not an 8088's: section 11.

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
the Audio Player's own source (section 8). Its size is the backend's
(`x3_lasize`). It is 32 KB, except 16 KB under the XT backend: that
machine's whole working set is 400,000 bytes (section 6), and 16 KB of
128 kbps is a second ahead of a decoder that slow. `AP_LA_SZ`, `AP_LA_MASK`,
`AP_LA_LOW` and `AP_RD_CHUNK` (the gulp's cap, `x3_rdmax`) are words, which
aplook.inc reads unchanged. `x3_lasize` sets the cap to `X3_RDMAX` (32 KB,
FAT16's largest cluster) and the low water to half the ring.

**A live XT stream's gulp** (`x3_plan`, a card and the XT backend, live or
probing): 4 KB (`X3_RDXT`, or one cluster where that is more), and the low
water is the ring less a gulp, so a refill is asked for as soon as one fits.
A read holds `sch_lock`, so the card plays its ring alone through it: 8
sectors are one `int 13h` call, two across a cylinder, which the 16 KB
grant's 1.49 s of 11 kHz outlasts. And a refill every gulp's worth of audio
keeps a floppy's motor turning: the BIOS stops it 2 s after a read, and
86Box's 386 underran once on the first refill after the prime, 2.7 s on,
with the half-ring low water (one run), and not at all with this (one run).

The ring shares **one claim** with two other buffers: one granule's PCM
(`X3_PCMSZ` = 2,304 bytes, room `X3_PCMRES` = 2,560 so what follows is
512-aligned) and, while converting, the conversion's stage (`x3_laxkb`).
One claim rather than three matters because the heap allows 8 claims per
owner (SPEC.md 50). The XT backend's work alone is 5 claims, and with the
eager carve and the part that leaves exactly one. The claim is taken when a
track opens, given back and taken again around every backend OPEN (section
3), and given back at stop. The front end's `X3R` ring record is
aliased onto the Audio Player's variables (`X3R_SEG`, `X3R_RD`, `X3R_WR`
and `X3R_EOF` are `ap_la_seg`, `ap_la_rd`, `ap_la_wr` and `ap_src_done`).

**The card's ring** (`x3_ringget`) depends on the stream's bytes a second:

| output bytes a second | ring | why |
|---|---|---|
| under `X3_RINGHI` = 32,000 | the SND grant (verb 7): 16 KB, else 8 | the Audio Player's shape, out of the driver's pool |
| 32,000 or more | an EXTERNAL ring (`SND_OPENF_EXT`, §34.5.3): 32 KB of our own plus the control words, `OSAPI_MEM_CLAIM_DMA_HI`, written directly | the pool tops out at 16 KB, which is 93 ms of 44.1 kHz 16-bit stereo |
| any, under the XT backend | x3_plan's grant: 16 KB with the look-ahead made smaller to fit, else 8 KB (`X3_RINGSM`); never the external ring | the XT path's 400,000 bytes hold the card's ring too (section 6). 16 KB is 1.5 s of 11 kHz where 8 is 0.74, and a smaller look-ahead is a shorter floppy gulp: 86Box's 386 underran 11 times live on the 8 KB grant from a floppy (FIELD.md round 2). Measured live on QEMU's SB16: 16 KB grant, 8 KB look-ahead, 387 KB = 396,288 |

The external ring only exists above 22,222 Hz or in stereo or 16-bit. All of
those need DSP 3.00 or later, which always has the auto-init that an external
ring needs. Which ring the backend's stream takes is decided by `x3_bkind`,
the kind of the backend OPEN (FFh once it is dropped), so a finished WAV
made by the XT backend plays from the ring its own rate wants.

**The format** (`x3_ofmt`): §121.6's rule. 16-bit stereo when the card
answers both `SND_CAP_STEREO` and `SND_CAP_PCM16`. 8-bit stereo with
`STEREO` alone. 8-bit mono otherwise. Verb 11 runs immediately before verb 0,
in the same UI callback. The rate is the rung's (the source's rate divided by
the rung's divisor), held to the card's range (`X3_DEVMIN` = 4,000, then
22,050 without `SND_CAP_PCM_HI`; 22,050 a channel for stereo on an SB Pro,
44,100 on a DSP 4.xx with or without its 16-bit DMA channel - the SDK's
`SND_CAP_ADPCM4Q` is "the DSP answers 4.xx", §34.5.5's table). A rung the
device cannot play is SKIPPED, which is not the same as failed: Original
skipped still tries Half before settling for the cheaper rung. A format the
device limits is labelled DEVICE on the LCD, not shown as a quality step.

**The device's conversion is the backend's own, and it is exact**: a 16-bit
stream is `x3sim`'s int16 PCM; an 8-bit one is its high byte XOR 80h
(`X3_FMT_U8`); a mono stream from a stereo source is `x3sim`'s mono downmix
(`(a >> 1) + (b >> 1) + ((a | b) & 1)`). `xtmp3live` captures each of the
three on QEMU's SB16 and finds them sample for sample.

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
live underrun is a reason to re-evaluate (section 3). On a finished file it
is counted as a disk or device fault and never steps (§121.5).

**A card that stops by itself** (verb 3's `SND_ST_ENDED`, SOUND.DRV's
watchdog, §34.5) before the stream's last half is published sets
`x3_cstop`: `x3_ended` closes the stream, shows ERROR `The sound card
stopped` and does not advance the queue. Before, it read as the track's end.
The owner's 86Box XT showed it: an SB 2.0 on IRQ 5 beside an `st506_xt`
fixed disk on IRQ 5 played one ring of a finished WAV (16,384 of 55,308
bytes). On IRQ 7 every byte played.

**States** (`x3_md` behind `x3_st`'s state): `MD_CHECK` (calibrating, "Checking
performance"), `MD_LIVE`, `MD_FILE` (a finished WAV on the card), `MD_SPK` (a
finished WAV on the speaker or Covox), `MD_CHOOSE` (the Decode-first dialogs),
`MD_CONV`, `MD_STEP` (live, draining for a rung change), and `MD_IDLE`. Pause
closes the stream at the sample played (`x3_pos`), and resume re-opens it
there, with pre-roll on a live stream. Stop, previous, next and
seek go through `x3_live_at`: the look-ahead is re-aimed at the frame
`X3_PREROLL` before the target (`x3p_seekpos`'s estimate of it, or the
audio's own first frame when the pre-roll reaches back that far), the
reservoir is emptied and the backend RESET, those frames are decoded to
nothing, and the position the face shows stays the target.

**`X3_PREROLL` = 6, and why.** It is the least pre-roll after which the
output is an uninterrupted decode's, MEASURED over 749 seeks into the 33
clean fixtures (every third frame from the tenth; the analysis script is in
the integration report): 1 frame reproduces 3.2% of them, 2 frames 15.8%, 3
60.7%, 4 92.5%, 5 99.9% and 6 all of them. One frame - §121.5's figure - is
not enough for two reasons a model of the decoder shows: a frame's main
data reaches back up to 511 bytes (`main_data_begin`), which at 32 kbps is
several frames, and a frame whose reservoir is short is silence; and the
short block's IMDCT chains its overlap through all three windows, so a run
of short blocks carries history past one granule. The cost is six frames
decoded on the UI task before the stream re-opens - 157 ms of audio at
44.1 kHz, decoded faster than that by any machine that plays the rung live.
`xtmp3live`'s seek leg holds the engine to `x3sim` decoding from the very
offset the engine aimed at (`x3_rsoff`), and finds it identical to an
uninterrupted decode from the first sample after the pre-roll.

A seek on a stream with no Xing count is a CBR ESTIMATE: the byte offset is
`frames x 576 x granules x kbps x 125 / rate`, and on a VBR file the frame
there is not the frame the position names. `xtmp3live` measured it on
NEONDRV.MP3: the clock shows frame 344, and the audio after the pre-roll is
frame 343's - 26 ms. A stream with a Xing TOC seeks by its TOC.

**Teardown** (`x3_onclose`, `x3_park`): the job is set to `WJ_NONE` and the
UI task spins on `x3_wbusy` until the worker is between passes. Then the
stream is closed (verb 2), the external ring's claim freed, the grant freed
(verb 7), and the backend's work segments and its part dropped. The worker is
`OS88_WORKER_RESTARTABLE_NOKEEP` and dies at `OSAPI_TASK_ALIVE` when the
window goes.

### 1.1 The hold: a live play reads no disk (§121.6.1)

`apps/xtmp3/x3hold.inc` puts the Video Player's desktop rule (§98.3.18.1)
into XTMP3. The reason is the 86Box 386's: a read holds `sch_lock`, and a
floppy charges its spin-up to the first refill after a quiet spell.
- **When.** `x3_go` asks `x3h_begin` after a track opens and before its
  ladder or job.
- **One XMS block** where the pool has the file's KB.
- **Else a conventional claim:** the file at most 192 KB, the heap's
  largest run holding it, 96 KB left free, and not an 8086.
- **The load.** It reads 32 KB `READ_SEQ` chunks: into the look-ahead's
  claim and up with `OSAPI_XMEM_COPY`, or straight into the claim. A timer
  pass reads chunks until about 110 ms have gone (`MD_LOAD`, state
  `X3ST_LOAD`, "Loading NN%"). Then `x3l_start` runs as it would have.
- **The copies.** aplook.inc's read is the `AP_RSEQ` macro: `os88_rseq` for
  the Audio Player (byte-identical) and `x3_rseq` here. That copies the held
  file's gulps out of the hold, with an odd last byte through a word for
  `XMEM_COPY`, and passes everything else to the disk. `[x3_nrseq]` counts
  the disk's gulps and `[x3h_ncopy]` the hold's. `x3_readat` (the header,
  the fingerprint) stays on the disk on purpose: a finalisation's
  "has the source changed" check must see the file, not the hold.
- **The key** is folder, volume and name (`x3h_same`).
  - A replay, a seek, a step or a Decode first of the same file keep the
    hold.
  - A new track, Stop, a load cut short (`x3_stopall` in `MD_LOAD`) and a
    failed read free it.
  - A conventional hold is the first thing `x3_plan` gives up when an XT
    plan cannot fit the 400,000-byte ceiling beside it.
- **Measured.**
  - `xtmp3live`'s `orig44` (QEMU `-m 4`, an XMS pool) and `holdconv`
    (`-m 1`, a conventional claim) play C128 sample-exact with no disk
    gulp from LIVE to the end.
  - 86Box's 386DX on AFTERHRS from its floppy played live XT Standard with
    0 underruns and 0 disk gulps all session. Its probes rose from 0.96 and
    2.14 to 1.06 and 2.71, because they no longer pay the floppy.
  - The primary is 44,591 + 14,348 = 58,939 of 65,024 bytes.

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

**XT Standard is divided by its source** (`X3_DIVSRC` in `x3_rungs`;
`x3_rungdiv`): 1 at 8-12 kHz, 2 at 16-24 kHz, 4 at 32-48 kHz. Divided, it
opens the XT part with profile 3, the speech tables, which are the same
output bit for bit (`x3_rungprof`, docs/xtmp3/XT.md 8.1). **XT Fast is
asked in Auto only on an 8-12 kHz source**, because it has no divided
kernels. XT Speech is refused on a 32-48 kHz source, a rule that was the
part's and is now the engine's (`x3l_okc`). So a 44.1 kHz song's ladder is
Quarter, then XT Standard at 4, then Decode first, and **Decode first is
reached only when no live rung at or above the floor passed** (SPEC.md
121.5).

**A probe** decodes the actual track from its start, with no pre-roll to pay
and the reservoir genuinely empty. It times, with `pit_now`, the front end,
GRANULE, and the feed's copy into the card's ring (verb 6). The disk's share
is the priming reads' measured cost per byte times the bytes the probe
consumed. A warm-up granule runs untimed first. Before it existed, the first
probe of a session ran slow by the cost of every cold cache. **A probe is
ARMED and starts two timer passes later** (`x3l_arm`, counted down in
`x3_e_tick`): a double-clicked document's play arms it inside the timer
pass whose `x3f_update` then repaints the whole new track, and a probe
running beside that repaint pays for it. Measured on MartyPC's XT: XT
Standard's first probe read 0.06x; armed, 0.11x - the figure FIELD.md had
when it was the session's second probe (every 86Box probe read lower on the
build that made it the first). It costs each probe up to two passes
(0.55 s), and the `ladder` legs' bounds hold (a hopeless machine 2.7 s). `pit_now` can
step back one tick under QEMU, because the BIOS tick and the 8253 disagree
across a pending IRQ0. `x3_pitd` treats a negative span as zero, and the
stub's own clock adds the pending tick.

**An 8086 probes ONE rung first** (`PH_HINT`, the integration track's
second round, on FIELD.md's 16.1 s of XT calibration). When
`OSAPI_CPU_INFO` answers `CPU_8086` and Quality is Auto, XT Standard - the
rung an XT's conversion uses - is probed first. If it is short of passing
by `X3_CAL_HOPE` (4x), nothing else is probed and the machine asks for a
drive: STANDARD at a quarter of the rate is no cheaper than XT Standard on
an 8088 (1.76 M cycles a granule against 1.62 M), and XT Fast is under 1.3
times as fast, so no Auto rung is within reach. The CPU is a HINT that
picks the order; the verdict is still the measure (a fast 8086 or a V20
whose XT Standard is not hopeless runs the whole ladder, that measure
cached). And the job's rung is the probe's, so its OPEN is reused. Measured
on MartyPC's 5160 (`xtmp3xt`, AFTERHRS): **6.4 s to the decision, against
16.1 s** (one probe: the fetch 1.0 s, OPEN 3.3 s, the prime and the probe
2.1 s), and the conversion's setup no longer fetches or OPENs at all.

**A rung that cannot start is a rung that failed** (`x3l_done`). Its claims
refused (out of memory), or the card, the rung is stored at 0 for the
session and the ladder runs again from its cache, to the next rung or to
Decode first; never an error. A step down whose rung cannot open tries the
one below it, and below the last live rung asks for a drive from the
track's start (`x3_steptick`). The `rungfail` leg makes Original's live
start fail (the stub's test hook) and plays Half with no ERROR on the way.
On 86Box's 486 (FIELD.md section 3) XT Standard died "Out of memory" at
44.1 kHz on the external ring; under the XT backend the card's ring is now
the grant the plan budgeted, which the probe already held, so that start
fits.

**The session cache** (`x3l_cached`, `x3l_store`): up to 8 results, keyed
by fingerprint, rung (backend, profile and divisor together) and device
(the format bits and the rate cap). A second play of the same track asks no
probe.

**Live step down.** Each live granule's wall time, from the front end
through the feed's copy, is summed over `X3_WINGR` granules (`x3_window`).
A window under 1.10x counts. Two in a row post `X3R_STEP`. A window at or
above 1.10x clears the count. That is the hysteresis. One underrun also
steps. **Nothing ever steps up automatically.** An override (Options >
Quality) is shown with `(set)` and is never stepped.

**The step's re-open, and its ramps.** Every rung below Original changes the
card's rate (Half, Quarter) or its format (XT: mono, 8 bits), so the stream
must re-open; only XT Standard to XT Fast keeps both, and that step changes
backend tables on the UI task, which the 8 KB ring would not cover either.
So the step is a re-open, and what was a DC step at each end is now a ramp:

1. `x3_stepbegin` sets `[x3_rampout]`; the worker's next feed pass ends the
   old stream with what it has staged and a ramp to silence (`x3_ramp`,
   `X3_RAMP` = 32 frames from the last sample made), published as the LAST
   half, and banks the stream's REAL bytes (`x3_stepby`) - so the position
   handed to the new rung is the last real sample's, not the ramp's or the
   silence after it.
2. `x3_steptick` waits for that half to be played (verb 3), closes the
   stream, opens the rung below (`x3_rungset`), re-aims with the pre-roll
   (`x3_resync`) and sets `[x3_rampin]` = 32.
3. The new stream's first 32 frames are scaled from silence up (`x3_rampup`,
   k/32 for frame k) before they are staged.

The gap between the two ramps is what the re-open costs: the card playing
out the old ring's remaining half, the new rung's OPEN (a part fetch only
when the backend changes - Quarter to XT Standard), the pre-roll's six
frames and the new ring's first fill. It is a short silence with no click,
not the seamless switch §121.5's "at the next granule boundary" pictures.

The disk's time is not in a live window. A read stalls the worker while the
ring plays on, so a disk that cannot keep up shows as underruns.

**An underrun re-evaluates** (`x3_under`, §121.5): it steps only when the
decode is short - `x3_wspeed`: this window's speed from 4 granules, else
the last whole window's (`X3S_SPEED`), else the rung's probe, under
`X3_SLOW` - or when it is the second inside one window (`x3_wund`, cleared
by a closing window and a new rung). Otherwise it is the disk's: the rung is
kept and the low water raised to 3/4 of the ring (never lowered), until the
ring is sized again. The X3REPORT build records the first underrun's
`x3_grans` and its kind (`U` the card ran dry, `S` the look-ahead starved).

**Without a card** there is nothing live to choose (§121.6: MP3 is never
decoded into the speaker), and the same probes become the conversion's
estimate. The offline choice (`x3l_offpick`) is the highest-quality rung
measured at `1 / X3_OFFMAX` or better.

## 3. The claim order, and the ladder's rows

**SPEC.md 121.10's ceiling decides the order** (the lead's decision, on the
XT track's numbers in docs/xtmp3/XT.md section 6). `x3_rungset`:

1. gives the look-ahead claim back (`x3_la_free`);
2. fetches the backend's part, claims its work segments, and runs OPEN;
3. shrinks the XT part to CAPS' `X3C_PARTKB` (29 KB) with
   `OSAPI_MEM_REGROW` (`x3_shrink`), which always succeeds in place;
4. only then claims the ring, at the backend's size.

Every caller re-aims and refills the ring afterwards (`x3_resync`,
`x3_live_at`, a probe's prime). A conversion claims its stage inside that
claim. A shrunk part refuses a second OPEN, so `x3_be_get` drops it and
fetches it whole again before any OPEN of a DIFFERENT record.

**The card's grant is given back before any OPEN** (`x3_rungset`, with no
stream open). A probe's grant was held until the NEXT probe took one, so an
XT probe's OPEN ran beside the previous probe's 16 KB grant: 402 KB =
411,648 bytes on MartyPC (FIELD.md 2.3). SPEC.md 121.10's ceiling is the
XT path's WHOLE working set, calibration included; `xtmp3xt` now asserts it
through the probes: **387 KB = 396,288 bytes**.

**RESET instead of a re-fetch** (`x3_reuse`). `x3_rungset` builds the
rung's OPEN record first (`x3_onew`: profile, divisor, channels, format,
the source's channels, version and rate index, the gain). When the backend
part is resident, OPEN is in effect (`x3_bopen`, cleared with the work
segments and by `x3_be_drop`) and the record is byte for byte the one it
was opened with, nothing is fetched, claimed or opened again: the caller's
`x3_resync` RESETs the histories, which is all a new start needs. That is
the case of a job whose rung is the probe's - XT Standard set by the user,
probed, then converted - and it saves an XT the 24 KB packed re-read of a
part the probe shrank. `xtpeak` asserts the XT part is fetched ONCE
(`x3_nfetch`, a fetch count a part).

Calibration probes shrink too. A probe holding the part unshrunk, its work
and the ring at once measured 404 KB, which is over the ceiling. A probe of
a DIFFERENT profile after it (Auto's XT Standard, then XT Fast) still
re-fetches, since the tables differ. No sound grant is held with the XT
part during a conversion, and a finished WAV plays only after the
decoder's parts are dropped.

**The buffers are planned last, MEASURED** (`x3_plan`, after OPEN and the
shrink, before the ring's claim). Under the XT backend everything left -
the look-ahead, the PCM's 3 KB, a conversion's stage, and on a live play or
a probe the card's grant to come (16 KB first, the look-ahead halved as far
as two source clusters allow; else 8 KB) - must fit `X3_CEILKB` = 390 KB
(399,360 bytes) less what is held now (`x3_curkb`: the region, the eager
carve and the heap's fall since launch, `x3_memck`'s own sample).
STANDARD has no ceiling. The rules:

- the look-ahead holds TWO of the source's clusters at least, and 4 KB at
  least: a gulp is whole clusters and the parser consumes whole frames, so
  a ring of one cluster waits for a gulp it has no room for (an XT's 16 KB
  ring over a 16 KB-cluster MP3 deadlocked so, before the plan). An MP3 on a
  32 KB-cluster volume is refused by name (`Its disk's clusters are too
  large`); a finished WAV plays from one (`AP_RD_CHUNK` is 32 KB);
- a conversion's stage is two halves of max(8 KB, 4 KB under XT, the
  destination's cluster); where that does not fit, ONE half (section 4),
  and then the look-ahead halves, down to its two clusters;
- nothing fits: the job fails before a byte is written,
  `Too little memory for that disk`.

**The ladder's rows.**

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

**Volume labels are not shown, and cannot be by any package-level means
there is.** The integration track looked for one: `OSAPI_FILE_FIND` never
answers the label entry (`kernel/disk.inc`'s walk skips attribute 08h along
with long-name fragments); `OSAPI_FS_ENT` is a `DRVC_FILE` driver's slot,
legal only inside `FSV_LIST`; the boot sector's `BS_VolLab` is sector 0,
and no slot reads a sector for a package (`OSAPI_VOL_*` are the driver's);
`OSAPI_FILE_READ_AT` reads files, and the root directory is not one. The
desktop label (`DV_LBL`) belongs to the driver that mounted the volume and
is not published to packages. So it stays a gap in the OS, and it needs a
slot; the dialog shows the letter, `Hard disk`, the free space and the
writability.

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

1. The stage: planned by `x3_plan` once the decoder is in (section 3):
   two halves, each `max(8 KB, the destination's cluster)`, or
   `max(4 KB, cluster)` under the XT backend - or, where the XT path's
   ceiling has no room for two, ONE half (`x3c_one`): the worker fills it,
   hands it to the writer and waits for it back, and the rest of a granule
   that did not fit waits in the PCM (`x3c_pend`) until it is free. On an
   XT the decode is far slower than the disk, so one half costs nothing
   measurable. The stage rides in the ring's claim, after the ring and the
   PCM, 512-aligned. Clusters up to 32 KB (FAT16's largest) are taken.
2. `X3nnnnnn.TMP`, the first free name in the folder, created by writing its
   header cluster with `OSAPI_FILE_WRITE`. A `JUNK` chunk pads the header to
   exactly one cluster, so every later write is whole clusters.
3. The worker (`WJ_CONV`, `x3c_wpass`) decodes into the half it owns and
   posts `X3R_WRITE`. The UI task writes the other half with
   `OSAPI_FILE_WRITE_SEQ` (`WSEQF_HELD | WSEQF_KEEP`, §18.4.9). Each half is
   a byte handshake, written last by the producer and cleared last by the
   consumer.
4. The progress window, at most ONCE a second (`XC_DRAWT` = 18 ticks), and
   only the characters that changed (`x3c_linec`): output, quality, decoded
   audio against track time, percent, wall time and speed (`0.31x`) - and,
   once the job has measured itself for `XC_ESTT` = 182 ticks (10 s), `About
   mm:ss left` from that speed. The figures behind the face (the LCD's
   clock, the bar, the status, the BUFFER gauge) move at the same rate
   (`x3c_prog`), and the dials rest (FACE.md). **The drive dialog shows no
   time**: a probe times the decoder, not the job, and on the XT its "About
   01:31" was a 3:03 conversion (FIELD.md) - SPEC.md 121.7 shows an estimate
   "only when the estimate is justified".
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

**The primary segment** (`APP_MAX_SIZE` 65,024, image + bss), measured from
the assembler's map at this branch's head, merged with `feature/xtmp3`:

| build | image | bss | image + bss | left |
|---|---|---|---|---|
| shipped | 42,935 | 14,279 | 57,214 | 7,810 |
| `PKG_MEGA` | 43,832 | 14,279 | 58,111 | 6,913 |
| `X3STUB` (test only) | 43,225 | 14,354 | 57,579 | 7,445 |

(At the integration track's head, from each build's own package header.
The engine track's figures were 41,275 + 14,078: the integration added
1,660 bytes of image - the plan, the OPEN reuse, the ramps, the packed and
cluster refusals, the probe's 32 KB path, the rung fall-through, the 8086
hint, the stated minimum, the face's throttles - and 201 of bss.)

**The engine's share.** Against `feature/xtmp3`'s primary with the demo
engine (image 19,670, image + bss 29,883), the engine added **21,605 bytes
of image and 3,865 of bss**, and the integration the 1,861 above. The
`X3STUB` build adds 260 bytes of image and 71 of bss more: the test hook and
the debug ring.

**Parts** (as built): FRONT 24,509 and ART 12,985 (eager, one carve);
STANDARD 57,732 and XT 58,634 (lazy, one resident at a time); and the stub
688 (row 4, `X3STUB` builds only).

**Heap claims while playing**, each one given back at stop:

| claim | size | when |
|---|---|---|
| ring + PCM (+ stage) | 34.5 KB, or 18.5 KB under XT (`x3_plan` may halve the ring), plus the stage when converting | a track is open |
| the card's ring | 16 KB grant (8 when the pool is short) or 33 KB external; x3_plan's 16 or 8 KB grant under the XT backend | live, or a finished file on a card |
| backend work | the backend's `X3C_NSEG` x `X3C_PARA`: STANDARD 0, XT Standard 5 claims and 236 KB | live, a probe, a conversion |
| the backend's part | STANDARD 57 KB; XT 58 KB, then 29 after OPEN | live, a probe, a conversion |
| conversion stage | 2 x max(8 KB, cluster), or 2 x max(4 KB, cluster) under XT - or ONE of them where the XT ceiling has no room for two (`x3_plan`) - inside the ring's claim | converting |

**`X3S_PEAK` is measured** (`x3_memck`). It adds the region (image + bss,
`X3_REGIONKB` = 54), the eager carve (`op_kb` = 37), and the fall in the
heap's free total (`OSAPI_MEM_AVAIL`) since launch, sampled after every
claim and kept at its highest. A conversion zeroes it at its start, so it is
that job's peak. Because it is the heap's own total, it also counts what
SOUND.DRV claims per stream on our behalf, and anything another task claims
meanwhile.

**The XT path, measured** on QEMU with the real XT part, every figure
`X3S_PEAK` (KB of 1,024; the ceiling is 390 KB = 399,360 bytes):

| case | leg | peak | the plan |
|---|---|---|---|
| XT Standard conversion, 2 KB-cluster disk, no card | `xtpeak` | 385 KB = 394,240 | two 4 KB halves, 16 KB look-ahead |
| XT Standard LIVE on an SB16 (NEONDRV, 44.1 kHz) | `xtpeak` | 387 KB = 396,288 | 8 KB look-ahead, the card's 16 KB grant |
| XT Standard conversion, 8 KB-cluster destination | `xtclu/8k` | 385 KB | ONE 8 KB half, 16 KB look-ahead |
| XT Standard conversion, 16 KB-cluster destination | `xtclu/16k` | 385 KB | ONE 16 KB half, 16 KB look-ahead |
| XT Standard conversion, 32 KB-cluster destination | `xtclu/32k` | refused, 377 KB at the refusal | nothing fits: `Too little memory for that disk` |
| XT Fast conversion, 32 KB-cluster destination | `xtclu/32k` | 387 KB | two 32 KB halves (its work is 182 KB) |
| XT Standard CALIBRATION on MartyPC's 5160 (an SB 2.01) | `xtmp3xt` std | 387 KB = 396,288 (was 402 KB = 411,648: the previous probe's grant was held through the OPEN) | the grant given back before any OPEN |

The peak of a conversion is at OPEN (the part unshrunk, 58 KB, with its
work), not during the decode; the plan sizes what follows the shrink. The
first engine attempt, with the ring held through OPEN, a 32 KB ring and a
separate stage, ran out of memory at 421 KB: its stage was the ninth claim
of eight.

**Why XT Standard cannot convert to a 32 KB cluster inside 400,000 bytes.**
After the shrink it holds the region 54, the carve 37, the part 29 and its
work 236 - 356 KB and a little rounding - which leaves 34 KB for the
look-ahead, the PCM's 3 and the stage. `OSAPI_FILE_WRITE_SEQ` takes whole
clusters (all but the last call), and `OSAPI_FILE_WRITE_AT` writes from a
cluster boundary, so the stage cannot be smaller than one cluster: 32 + 3 +
a 4 KB look-ahead (two of a 2 KB-cluster source's clusters) is 39. Nothing
smaller exists; XT Fast, XT Turbo, XT Speech and every STANDARD rung convert
there.

## 7. The stub backend and the test hook

`apps/xtmp3/x3stub.asm` is an `OP_SEG` part that speaks x3gran.inc's ABI.
In an `X3STUB=1` build (`make xtmp3stub` builds `build/xtmp3s.o88`) it is
the parts table's row 4, after XT, and it answers for both backends
(`X3_PART_STD` = `X3_PART_XT` = `X3PART_STUB`). The front end is the real
one. The test hook is `apps/xtmp3/x3hook.inc`. The front-end stand-in that
carried the engine before the merge (`x3stubp.inc`) is gone.

GRANULE makes 576/divisor frames of a 440.8 Hz square (the step is
655/65536 of the output rate), +-12,288 or 80h +- 30h, in the requested
channels. Its cost comes from the test hook: `X3STUB.CFG`, beside the
package, holds a speed x 100 for each rung, a slowdown rung, the granule it
starts at, and the slow speed. The primary copies it into the part at each
fetch (`x3_stubpoke`). A speed S makes each granule take its audio duration
x 100 / S of wall time, spun on channel 0 of the 8253 and the BIOS tick, so
the ladder's transitions are deterministic on any host. The stub never masks
interrupts. Its clock adds a pending IRQ0's tick rather than stepping back.

`x3_nfetch` (every build: 5 bytes of bss) counts each part's fetches; the
rows read it to prove Original never fetched the XT part and a job reused
its probe's OPEN. `x3_rsoff` (4 bytes) is where the last resync's scan
began, which the seek leg decodes from.

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

**The stated minimum** (SPEC.md 121.10; 24.5's rule that a package that
cannot run refuses in its own words). Each rung's working set, measured
(section 6), is `x3_needkb`: XT Standard 390 KB (the ceiling; 385-387
measured), XT Fast 360, XT Turbo 330, XT Speech 340, a STANDARD rung 220
(with a card's external ring). What the machine HAS is the same measure's:
the region, the eager carve and the heap free at launch (`x3_haskb`). The
conversion's rung is checked before the drive is asked for (`x3_fits`, in
`x3l_done`), and any rung whose part or claims are refused for memory says
it the same way (`x3_needmsg`): **`Needs 390 KB, has 300 KB`** - 24
characters, so the status line holds it whole. The `shortmem` leg holds
the sentence (the machine's figure from the stub's test hook), and that no
drive was asked for.

## 9. Where the code and §121 part ways

- **The pre-roll** is `X3_PREROLL` = 6 frames, the seek's and the step's.
  §121.5 says one. Measured: one frame reproduces an uninterrupted decode
  in 3.2% of seeks, six in all of them (section 1).
- **A step is a re-open, with ramps.** A rung change changes the card's
  rate or format, so the old stream ends on a 32-frame ramp to silence, the
  rung below opens with the pre-roll, and its first 32 frames ramp up: a
  short silence with no click, rather than §121.5's "at the next granule
  boundary" (section 2).
- **XT Standard does not convert to a 32 KB-cluster volume** inside
  §121.10's 400,000 bytes, and refuses by name; the other rungs do (section
  6).
- **An MP3 on a 32 KB-cluster volume is refused by name** (`Its disk's
  clusters are too large`): its look-ahead would need two clusters, 64 KB.
  A finished WAV plays from one, and the queue's probe reads its title and
  duration there (STD.md).
- **An 8086 probes XT Standard first, and alone when it is hopeless**
  (section 2): the CPU picks the order, the measure decides.
- **The drive dialog shows no time estimate**; the progress window shows
  one after 10 s of the job's own speed (section 4).
- **An lz4-packed MP3 is refused by name** (`The MP3 is stored packed`,
  and the probe's `PACKED FILE`), never decoded as noise.
- **Volume labels** are not shown, and no package-level slot can give one
  (section 4).
- **Offline Auto** picks the best rung converting in at most 8x the
  track's length (`X3_OFFMAX`). It can therefore pick Quarter over XT
  Standard when Quarter converts fast enough. §121.7 does not say which
  rung a conversion uses.
- **Decoded Files** is in the File menu, as §121.9 lists it. §121.7 says
  Options.

## 10. The rows

**`xtmp3live`** (soak, `tests/xtmp3live.py`, QEMU, about 4 minutes): the
SHIPPED package with its real FRONT, STANDARD and XT parts, the SB16's
output captured by QEMU's `wav` audiodev at the stream's own rate (so the
mixer resamples nothing), every capture aligned by search and held sample
for sample:

| leg | asserts |
|---|---|
| `orig44` | C128.MP3 (44.1 kHz L/R stereo) under Auto: Original, 16-bit stereo, the capture IS x3sim's trimmed PCM (66,150 frames), the channels distinct; the XT part never fetched (`x3_nfetch`), STANDARD resident; on the host, the STANDARD part's map has no `x3x_` label and none of XTGEN.BIN's table bytes |
| `orig22` | M22.MP3 (22.05 kHz joint stereo): the same |
| `half` | C128.MP3 with Quality: Half set: 22,050 Hz 16-bit stereo, x3sim's divisor 2 |
| `dev8st` | an SB16 with no programmable 16-bit channel (`dma16=1`): Original in 8-bit stereo, labelled DEVICE, the bytes the card got = x3sim's high byte XOR 80h |
| `dev8mono` | a DSP 2.01 (`version=0x0201`): M22.MP3 as 8-bit mono, labelled DEVICE, the bytes = x3sim's mono downmix through the same conversion (captured at the time constant's 22,222 Hz) |
| `seek` | NEONDRV.MP3 live, then 0:09: the output after the pre-roll = x3sim from the engine's offset (`x3_rsoff`) with an empty reservoir and zero history, every sample - and identical to an uninterrupted decode from the first sample |
| `xtfile` | XT Standard set by Q, decoded first to C: (an SB16 present): the WAV byte-exact to x3simxt's XT Standard trimmed (111,215 bytes), the peak 385 KB, the XT part fetched once; then the WAV on the card with no decoder resident, and a seek to 0:09 played from the WAV's data directly |

QEMU 11's mixing engine widens an 8-bit sample `u` to `(u - 127) x 257`
(measured: every captured value is in that table), so an 8-bit capture is
read back into the bytes the card was given. QEMU drops what its mixer still
holds when a stream closes (measured: 49 ms at a WAV's end), so a capture
may end up to 0.1 s short of the stream and no more.

**`xtmp3eng`** (soak, `tests/xtmp3eng.py`, QEMU because behaviour is what
is asserted): each leg boots its own private QEMU with its own disks under
`build/x3e*`.

| leg | asserts |
|---|---|
| `ladder` | six machines by stub speed: capable stays Original; middling takes Half or Quarter; slower Quarter; **xt44**, every STANDARD rung short and XT Standard passing on the 44.1 kHz song, plays LIVE XT Standard at 4 (11,025 Hz) with XT Fast never asked; XT-class reaches Decode first with Original and Half never probed, within 6 s of guest time; hopeless the same, within 4 s (it measured 1.4 to 1.8) |
| `slowdown` | at 1.05x the windows step Original to Half with no underrun; at 0.60x one step, at most one underrun; never back up; position monotonic. Then the underrun rule, through the stub's hook (`X3SB_UNDAT`/`X3SB_UNDN`, counted as the card's own): one at 4x keeps Original and raises the low water to 3/4; two in one window step to Half |
| `conv` | drive dialog, Save dialog, the WAV's every field and its frame count on the host, the finished file plays, a second Play reuses the cache with no granule, no probe and no part |
| `cancel` | Esc mid-conversion: READY, the temp file gone, nothing else touched |
| `full` | a disk filled mid-write: the error, the temp gone, nothing played |
| `stale` | a changed source and an incomplete WAV are both refused and re-converted (three boots) |
| `novol` | no fixed disk: the dialog says so |
| `twovol` | two writable volumes, the second chosen, the WAV lands there |
| `playwav` | a finished WAV on the SB16: no decoder resident, no granule, no underrun, the square's frequency in the capture |
| `pause` | pause, resume, rewind |
| `xtpeak` | the SHIPPED package with the real XT part, no card: XT Standard set by Q, Decode First, the conversion finishes, `X3S_PEAK` x 1024 <= 400,000 (385 KB), and the XT part is fetched ONCE (the probe's OPEN reused); then LIVE on an SB16: the 16 KB grant with an 8 KB look-ahead, never the external ring, a 4 KB gulp, no underrun, 388 KB |
| `xtclu` | three boots, the destination D: a FAT16 volume of 8, 16 and 32 KB clusters (sparse images, the least FAT16 that has them): XT Standard converts to 8 and 16 KB with ONE stage half, 385 KB, its WAV byte-exact to x3simxt; on 32 KB the drive dialog greys D: (`32 KB clusters`) for XT Standard and Decode and Play refuses it, and XT Fast converts there (two 32 KB halves, 387 KB), byte-exact |
| `chgsrc` | a byte of the source's first 16 KB changed on the host DURING a (stub-slowed) conversion: `The MP3 changed meanwhile`, nothing plays, no .TMP or .WAV left |
| `dfdel` | two boots: a conversion, then on the host a second index record naming OTHER.WAV (a WAV whose chunk is not `xtm3`) and an unrelated KEEP.TXT; File > Decoded Files, Delete, Proceed, twice: XTMP3's WAV is deleted, OTHER.WAV STAYS, KEEP.TXT untouched, both records gone |
| `batch` | two tracks queued (File > Add to Queue), the first's drive dialog with `Queue too: Yes`: the second converts into the same folder with no dialog; both WAVs on C:, no .TMP |
| `rovol` | a read-only fixed volume: QEMU's IDE has no write-protect for a hard disk, so the one this machine has - a CD through CDROM.DRV (VK_FIXED, `VT_FILE`) - is listed flagged read-only, and Decode and Play refuses it |
| `packed` | an MP3 wrapped by `tools/os88lz.py --wrap`: ERROR `The MP3 is stored packed`, no decoder fetched |
| `shortmem` | an XT-class ladder (the stub) on a machine the test hook says HAS 300 KB: ERROR `Needs 390 KB, has 300 KB`, and the drive dialog never opened |
| `rungfail` | Original calibrates (4x) but its live start is refused as out of memory (the stub's hook): the ladder stores it at 0 and plays HALF, with no ERROR on the way |
| `offvol` | the SHIPPED package on B:, the song on C: with an SB16: a part is fetched standing in the launch folder (`x3_fetchhome`), so Auto plays live on Original with STANDARD fetched; then a forced XT Standard converts onto C: and plays. Red without the bracket (`Cannot read its decoder`: a fetch that fails while `x3_fits` says the machine has room is not reported as memory) |
| `offdir` | the same drive, another folder (the owner's 86Box XT: C:\XTMP3 and C:\MEDIA): the package at C:'s root, the song in C:\MEDIA through File > Add to Queue. STANDARD's fetch follows the carve's read and rides `os88_rseq`'s cursor, so it never looks the name up and played even before the fix; the forced XT Standard's part is read by NAME, and without the bracket refused (`Cannot read its decoder`, the old `Needs ... KB`) |
| `wavdirs` | the owner's layout (package in C:\XTMP3, the song in C:\MEDIA, the WAV saved to C:\): an XT Standard conversion and its play to the end, then a second play through the cache. Every byte of the data reaches the card (`x3_played` against the data's length), no underrun, Finished at the total |
| `qagain` | a song queued (File > Add to Queue) while live, its probe refused by the stub's hook (`X3SB_QFAIL`): the row keeps the stem flagged `QEF_AGAIN`, and the tag's title arrives once the player is idle (`x3_qagain`) |
| `probe32` | an MP3 whose first frame sits 200 bytes short of 32 KB, on a 32 KB-cluster C:: the queue's probe reads its title and x3sim's duration across the cluster's end, and Play refuses it by name (`Its disk's clusters are too large`) |
| `covox` | the finished file's ACTUAL SOUND with no card: QEMU's pcspk renders only a PIT mode-3 square (its capture of the speaker bracket was EMPTY), so the Covox tier (SYSTEM.CFG, LPT1): every byte written to the LPT data port traced (`-trace parallel_ioport_write`) and the WAV's samples found there, in order, one for one - 32,724 identical in 45 s, QEMU running the bracket's 11 kHz IRQ0 slowly |

## 11. What the real backends have and have not shown

- **Shown, on QEMU:** live STANDARD sound through this engine, sample for
  sample, in every device format; the XT backend's conversion byte for
  byte; the seek; the ceiling in every plan; the finished file's bytes
  reaching a Covox.
- **Not shown here:** any timing. QEMU's wall clock says nothing about an
  8088, and the ladder chose Original on every QEMU leg. The FIELD track
  measures the real machines (MartyPC, 86Box). Ring sizes against a real
  decoder's jitter, and the XT backend's 16 KB look-ahead against a real XT
  disk's latency, are its to check.
- **The speaker** cannot be heard on QEMU (its pcspk is a mode-3 square
  generator); the bracket is the Audio Player's own, whose bytes `apspk`
  holds to `tools/os88spkfx.py` on MartyPC, and the Covox leg shows XTMP3
  hands it the finished file's samples.

## 12. Open items

- **A volume-label slot** (section 4: no package-level means exists).
- **XT Standard on a 32 KB-cluster destination** is refused by the ceiling
  (section 6). A slot that wrote less than a cluster at a time would lift
  it; nothing in the SDK does.
- **The drive dialog greys only what it can predict**: a 32 KB-cluster
  volume under XT Standard (`x3c_vscan`, `32 KB clusters`; the `xtclu/32k`
  leg). Any other memory shortfall the plan measures after OPEN fails the
  job at once, before a byte is written, with the stated minimum.
- **Every timing** is the FIELD track's (section 11), and the integration
  track's second round measured its own changes there (`xtmp3xt`, MartyPC's
  5160 with a VGA): XT Standard's conversion of AFTERHRS 0.055x -> 0.0946x
  (the decode loop 0.0586x -> 0.1004x; the face's share 44.6% -> 9.8%),
  calibration 16.1 s -> 6.4 s, its peak 402 KB -> 387 KB.
- Closed by the integration track: live XT with a card (387 KB, the 8 KB
  grant), the ceiling on any cluster (the plan), the re-fetch after a probe
  (`x3_reuse`), the untested paths (the `chgsrc`, `dfdel`, `batch`, `rovol`
  and `covox` legs), and the step's audible gap (the ramps).
