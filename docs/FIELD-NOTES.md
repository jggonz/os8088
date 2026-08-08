# Field notes — things real hardware found that the harness did not

Symptoms observed on a **real 4.77 MHz 8088 under PCem v17** (and on period
machines generally) that are open, reproduced, and not yet fixed. Each entry
records what was *seen*, what has been *ruled out*, and the standing theory —
so the investigation starts from evidence rather than from scratch.

The rule these entries exist to serve: **the emulator is exact about how much
work the guest does and useless about how long it takes** (PERFORMANCE.md).
Notes 1 and 2 are things QEMU cannot show, because QEMU is ~1000x the target
machine. **Note 3 is the other half of that rule** and the happier case: the
symptom is only visible on hardware, but the suspected causes are all *work*
rather than timing, so QEMU can count them and the investigation should start
there. **Note 4 is a third shape again** — reproduced on hardware, its
mechanism *identified* rather than theorised, and now **fixed** (SPEC.md
§22.8) and reproduced under QEMU both ways: it never needed the iron at all,
it needed somebody to open a Disk window and save a file from a package. **Note 5 is a fourth, and the most valuable one to read**: a
correctness bug the harness cannot see at all, at any speed, because the
difference is in the *BIOS*, not in the timing.

---

## 1. Audio tails off for ~1/3 second, every few seconds (Tracker)

**Observed.** A MOD plays normally for a few seconds, then the sound "slows
down" or tails off for about a third of a second, then continues normally.
The cycle repeats. Reported on a real 8088 at 4.77 MHz.

**Ruled out — this is NOT the fsx work.** The reporter A/B'd the shipped
images against `a4facf0`, the commit immediately *before* Tracker moved onto
the §53 bracket (Tracker still on §11.2 `wm_fullscreen`, its worker still
doing `feed` then `render` in one slice). **The stutter is present in both.**
It is therefore older than the fsx adoption and is not the bracket, the
freeze, `FSXF_KEEPWORKER`, or the drawing/feeding split.

Also ruled out by the same session: it is **not** about what is on screen.
The reporter saw it fullscreen, windowed, with the Tracker window *completely
covered* by another window, and *minimized*. Covered and minimized are the
cases where the drawing path does the least work (§11.3's clip region skips
it, `wm_obscured` vetoes it) — so a redraw cost that crowds out the audio feed
is a poor fit for the evidence.

> **PARTLY RESOLVED, and the remainder has moved somewhere specific.** The
> **first** hitch — the one about half a second into the first play after a
> load, absent on a stop-then-restart — was the pre-roll boundary, and
> `TRK_PREROLL` = 6 fixed it: the cushion is now staged before the stream
> opens, with no DSP consuming anything, instead of being deepened by the
> worker in competition with playback (SPEC.md §45.2). The reporter confirms
> it is gone.
>
> The **later** hitches are still there, and the margin meter (§45.14, the D
> key) has now answered the question the analysis below was built to ask.
> On the reporting machine, across hitches, it reads **`MIN 6  LATE 000`** —
> pegged. 6 halves is the pre-roll value, so the ring was **never drawn down
> at all**: at 5,500 Hz the DSP always had 2.2 seconds of audio in hand, and
> not one feed wake ever arrived with under one half. That kills the two
> theories the analysis below spends its length on. It is **not** mixer
> throughput, and it is **not** CPU contention from drawing — a repaint
> cannot starve a ring that never drains. The meter's own doc comment
> anticipated this reading: *"MIN staying high while the audio still hitches
> says the feed was never the problem and the fault is downstream."*
>
> **Downstream means one specific buffer.** There are two in the chain and
> they have different pacers: Tracker's 16KB staging ring in its grant, fed
> by the package worker (this is all `MIN`/`LATE` can see), and the driver's
> **2 × 2KB DMA double buffer** at `SND_SEG:0`, fed by `sbl_refill_task`
> (SPEC.md §34.5). If the half the DSP wants next is not valid at the block
> IRQ, `sbl_isr` pauses output (D0h) and marks the stream `SBL_ST_UNDER` —
> bounded silence, never stale audio looping. **One 2KB half at 5,500 Hz is
> 372 ms**, which is "tails off for about a third of a second" to the
> precision of the report.
>
> Nothing in the app could see that, which is why it went unmeasured for two
> rounds: an underrun-pause stops `consumed` advancing, so `total − consumed`
> *grows*, and the meter reads healthier the worse it gets. The app was
> already polling the state (verb 3 returns it in AX every wake) and
> discarding everything but `ENDED`/`STALE`.
>
> **`UND` came back `000` too**, on the same machine, across hitches. So the
> driver believes it is playing throughout: its own double buffer was never
> starved either. Every buffer in the chain is provably full at the moment
> the sound tails off.
>
> **The content is exonerated as well**, and that one was settled here rather
> than in the field, because content is exactly what QEMU *is* exact about
> (PERFORMANCE.md). `BEVERLY.MOD` was captured through an SB16 twice —
> once in XT mode at 5,500 Hz, once at 11,000 — and the two amplitude
> envelopes match block for block, with their only two real dropouts at
> **7.50–8.75 s and 16.50–17.50 s in both**. Those are the song. There is no
> periodic tail-off anywhere in 67 and 80 seconds of capture, at either rate,
> and a mixer arithmetic bug would neither be rate-independent nor absent.
> (`make test-snd SB16=1` plus a block-RMS profile of `build/snd.wav`; the
> two rates matter because a rate-dependent overflow was the leading content
> theory, and identical envelopes kill it.)
>
> **So: the app is fine, the driver is fine, the samples are fine, and the
> emulator cannot see it.** That is a timing defect that exists only on the
> real machine, below the driver's bookkeeping — which is a much smaller
> place than where this started.
>
> **The next instrument is `BLK` and `WAKE`, on the same D line** (§45.14).
> `consumed` advances by one whole half and only from `sbl_isr`, so the
> wall-clock gap between two different readings **is** the block-IRQ
> interval — 6.8 ticks at 5,500 Hz — and `WAKE` is the same measurement for
> the worker's own pass, as a control. Baseline under QEMU: `BLK 08 WAKE 01`.
> Three outcomes, and they need no interpretation:
>
> - **`BLK` ≈ 13–14 with `WAKE` at 01** → a block IRQ arrived one whole
>   period late or was lost, while this app ran perfectly. 372 ms is the
>   reported hitch, exactly. On single-cycle DSPs (< 2.00) `sb.inc` already
>   records a known bound of that species: the ISR reprograms the 8237, whose
>   byte-pair flip-flop is shared with the BIOS's channel-2 programming
>   inside int 13h. Check the reporting machine's DSP version first — that
>   branch is the one QEMU never runs.
> - **`BLK` and `WAKE` both climb** → the whole machine was descheduled, and
>   `sch_lock` held across int 13h (§7, the one sanctioned long lock) is the
>   first suspect. That is theory 1 below, and this is how it gets confirmed
>   without instrumenting the kernel.
> - **Both stay at 08/01 through an audible hitch** → the interruption is
>   below the block IRQ, in the DSP or the analogue side, and nothing running
>   on the CPU can measure it. That is the point to stop instrumenting the
>   guest and start swapping hardware.
>
> **The text screen visibly emptying is a separate observation, and it has
> two possible meanings that matter very differently.** `SHB` (§45.14) tells
> them apart in one number, live, and the QEMU reading is `SHB 00` with the
> top nine rows of the area blank at row 0 — i.e. the expected one.
>
> - **`SHB 00` — it is the pad, and the pad is by design** (§45.13.2). The
>   shadow carries `TTX_HALF` = 9 blank rows above pattern row 0 and below row
>   63 so the blit window needs no clamp; they are on screen at the ends of
>   every pattern. Watched rather than reasoned about, the second one reads
>   exactly as reported: the lower half loses its text while the upper half
>   keeps scrolling. **The useful consequence is that it is a free, precise,
>   visible clock.** The blank reaching its full nine rows *is* row 63, so
>   "the audio hitches at the end of the missing lines" times the hitch to the
>   **pattern boundary** — the one frame in ~8 seconds that costs 256
>   `mp_cell2txt` calls and 4,838 word stores instead of 1,121. The honest
>   reading of that, though, is **correlation and not cause**: a one-frame CPU
>   stall cannot starve a ring that `MIN 6` says never drew down, and `WAKE
>   01` says the worker kept its slice. If the hitch really does land there,
>   what to look at is `BLK` on that same frame — a pattern boundary costing a
>   block IRQ would be a real finding, and a pattern boundary merely being a
>   memorable place in the music is the null result.
> - **`SHB` non-zero — something is writing into Tracker's bss**, and that
>   would change the audio investigation completely rather than adding to it.
>   The shadow is 9,676 bytes of a 30,498-byte bss, the largest single object
>   in it, so a wild write lands there first by probability alone — and the
>   *same* writer would reach `mp_voltab`, `mp_chans` and `mp_outbuf`, where
>   the symptom is not a blank row but **a channel going quiet or an
>   amplitude going wrong**. That is one root cause for both reports, and it
>   would explain the thing that is otherwise strange about them: every
>   transport counter reads perfect because the transport is faithfully
>   delivering corrupted samples. It would also have to be 8088-specific,
>   since QEMU shows neither symptom — and the one known 8088-specific
>   memory hazard in this tree is the one `tests/stackprobe` exists for (a
>   real BIOS services interrupts on the current task stack; SeaBIOS does
>   not). That lands in `LOW_SEG` rather than package bss and `SCH_MAGIC`
>   would catch it, so it does not fit as written — but it is the right
>   family to think in, and `stackprobe` on the reporting machine is the
>   cheap first move.
>
> **THE TEXT FREEZE IS SOLVED, AND IT WAS NOT THE PAD.** `SHB 00` on the
> reporting machine settled the pad question — the shadow is intact — and the
> reporter then described the thing the pad was masking: *"when the scrolling
> empty lines reach roughly the middle of the screen the entire text screen
> stops updating for 1/3rd a second or so, then when it resumes it jumps."*
> **Reliably, every ~8 seconds.**
>
> Both halves of that are arithmetic. The blank reaching the middle is
> row 63; 64 rows at 125 BPM speed 7 is 50 ticks/s ÷ 7 = 7.14 rows/s =
> **8.96 s a pattern**. So the freeze is the pattern boundary, and the
> pattern boundary is `ttx_shbuild` — which formatted all 64 rows in one
> frame. Priced against PERFORMANCE.md Part 9's measured 8088 (RAM
> `rep stosw` 1.76 us/byte; the 4.34-clocks-per-instruction-byte floor;
> 4.66 MHz): the 9,676-byte blank is **17 ms**, the 3,776 `lodsb`/`stosw`
> pairs are **28–32 ms**, and 256 `mp_cell2txt` calls — each one a linear
> `mp_pfind` scan over up to 36 periods plus three hex fields — are the rest.
> **140–330 ms**, against a frame that otherwise costs about 6. The reporter's
> "1/3rd a second" sits at the top of that range, and the *jump* on resume is
> the view having advanced two rows while nothing was drawn.
>
> Fixed by spreading it: `TTX_SHCHUNK` = 4 rows a frame, cursor starting at
> the visible window and wrapping (SPEC.md §45.13.2). Worst frame ~25 ms.
>
> **What it does NOT explain is the audio**, and that has to be said plainly
> because the temptation is strong. The reporter says the hitch "usually
> occurs during that freeze, but it doesn't occur every time" — but
> `ttx_shbuild` runs on the bracket task with the worker still whitelisted and
> pre-emption still working (a full switch is 693 us, and the kernel's own
> tick + mouse + scheduler is 1–3% of a busy CPU). A 330 ms drawing stall
> cannot drain a ring that holds 2.2 s, and `UND 000` says the driver's own
> buffer never starved either. So the correlation is real and the causation
> is not established.
>
> **The first field reading of `BLK`/`WAKE` was `BLK 32 WAKE 29`, with
> `MIN 4`** — and those three are consistent with each other rather than with
> the freeze: 29 ticks is 1.59 s, at 5,500 Hz that is 8,745 bytes, and a ring
> that starts 8 halves deep and loses 4.3 of them lands at exactly `MIN 4`.
> `BLK 32` is then explained *by* `WAKE 29`, because `BLK` is sampled by the
> worker and a worker that did not run could not sample. So one ~1.6 s
> deschedule accounts for all three — and it is far more likely the load
> repaint than a pattern boundary, because the extremes reset only with the
> stream and the load is inside the window.
>
> **The meter has been retired for a LOG** (§45.14, `tests/trklog.inc`), and
> the reason is exactly the reading above: three numbers that are consistent
> with *one* event cannot say *which* event, and an extreme cannot be placed in
> time at all. `TRKLOG.O88` is Tracker assembled with `-DTRKLOG` and writes one
> record per system tick to `TRKLOG.TXT` — tick, consumed, total, stream state,
> song position, frames, feeds, flags, tempo — which answers all of it at once:
> a **gap in the TICK column** is the whole machine stopping, `CONS` spacing is
> every block-IRQ interval rather than the worst, and `FR 0` against `FD 1`
> separates the drawing freezing from the worker starving. Verified on QEMU:
> 706 records, zero tick gaps, block-IRQ median 7 ticks against 6.8 predicted.
>
> **THE FIRST FIELD LOG IS IN, and it settles the TEXT freeze completely.**
> 755 records over 62.1 s (1,130 ticks), 51.2 s of it inside the bracket.
> Every frame spacing in the whole fullscreen run: **432 × 1 tick, 247 × 2,
> 2 × 3, and nothing else.** The 1/3-second stall every ~9 seconds is gone,
> and it is gone *at the place it used to be*: all five pattern boundaries in
> the capture are `FL 07` followed by frames spaced 1–2 ticks, **indis-
> tinguishable from the baseline**. SPEC.md §45.13.2's spread rebuild is
> confirmed on the target machine, not just modelled.
>
> **And the audio chain measures clean over the same 51 seconds.** The DSP
> consumed **303.2 bytes/tick — an implied 5,521 Hz against the 5,500
> asked, 0.4% out** — so the card did not stall in aggregate for even one of
> those windows. The ring lead never fell below **5.0 halves (1.86 s)**, the
> stream state is `0` in every one of the 755 records, and `UND` (which is
> now `S 1`) never appears.
>
> Three things the log priced that nothing had measured before:
>
> - **Bracket entry costs 22 ticks (1.2 s)**, and it is not the video mode.
>   `trk_play` re-opens the stream there, and its 6-half pre-roll is 12,288
>   samples of `mp_gen`; at §45.9's ~2.1M cycles/s for 5,500 Hz that is
>   ~1.0 s on its own. The synchronous `ttx_shbuild` and the mode set are the
>   rest. That is a legitimate one-off at a moment the user expects a pause,
>   but it is the largest single stall in the file by a factor of seven.
> - **Windowed costs 4–5 ticks a frame against fullscreen's 1–2** (the tail
>   after Esc: 35 × 1 tick, 5 × 4, 14 × 5, 2 × 9). The text mode is ~3×
>   cheaper per frame in the field, which is §45.13's whole argument arriving
>   as a measurement.
> - **A feed pass can span 23 ticks (1.26 s)** — the longest gap between two
>   `FD 1` records. That is not a starved worker: it is one `TRK_MAXFEED`
>   burst mixing three halves while being pre-empted, and the ring goes 5
>   halves → 8 across it. It is worth knowing because it is the closest the
>   ring came to empty in the whole capture, and a burst twice that long
>   would drain it.
>
> **What the log cannot say is whether anything was audible during it**, and
> that is now the only missing datum. Every counter in the guest is healthy
> for 62 seconds; if a hitch happened in there, it is invisible to all of
> them — the third branch of the split above, below the block IRQ. So the
> instrument grew the one input that is not a measurement: **`M` stamps
> `FL` bit 10h into the current tick**, so the listener can mark the moment
> they hear one. A file with marks in it answers in one pass what no amount
> of counter-reading can.
>
> Everything below this line is the earlier analysis, kept because its
> ruling-out is still valid and because the two theories it eliminates are
> the ones anybody would reach for again.

**Second report, and it moves the needle a long way.** The hitches land at
roughly the **same point in the song** each time — so the trigger is
song-position-correlated, not wall-clock-periodic. There is **always one
about half a second into the first play after a load**, and that one **does
not happen on a stop-then-restart**. A later one lands maybe ten seconds in
(approximate — not measured).

That set is very restrictive, and the arithmetic of the ring is what makes it
so. On a tier-0 machine XT mode is auto-armed (§45.9, `osapi_cpu_info` in the
entry proc), so the rate is 5,500 Hz, and with `TRK_RING` = 16,384 and
`TRK_HALF` = 2,048:

| quantity | at 5,500 Hz (XT) | at 11,000 Hz |
|---|---|---|
| one ring half | 372 ms | 186 ms |
| whole ring | 2.98 s | 1.49 s |
| **pre-roll (2 halves)** | **744 ms** | **372 ms** |

**The first hitch is the pre-roll boundary.** `trk_play` pre-mixes two halves
and starts; that buffer is all the slack there is, and when it runs out the
worker's own refill has to carry the stream for the first time. Half a second
in is exactly where that happens. So the question is not "what stalls the
machine at 0.5 s" — nothing does — it is **"what is competing with the worker
during the first second after a load, and only then"**.

The answer that fits *"not on a restart"* is the **full-screen repaint a load
performs and a restart does not**: the completion callback ends in
`tui_draw_all`, the whole FT2 screen, which on a 4.77 MHz machine is hundreds
of glyph cells and hundreds of milliseconds of UI-task work. It cannot block
the worker (the feed takes no lock), but it does halve its CPU share while
round-robin has two runnable tasks — and if the mixer needs more than half a
CPU at this rate, the ring drains for exactly as long as the repaint lasts.
The later hitches fit the same shape at pattern boundaries, where
`tui_draw_dyn` escalates to a full pattern redraw: a 64-row pattern at 125 BPM
speed 6 is **7.7 s**, which is the right order for "maybe ten seconds", and
pattern boundaries are at fixed song positions — the "same point in the song"
observation.

**The one piece of evidence that does not fit** is from the first report:
covering the window completely and minimizing it did not help. Both should
skip the drawing outright (`wm_clip_set` refuses, `wm_obscured` vetoes). If
that holds up under a careful re-test, the drawing theory is dead and the
answer is the other one below — the mixer simply not sustaining real time on
this content, with the ring hiding it until it drains.

**Two decisive experiments, in this order.** Both are minutes of listening,
no code:

1. **The Rate menu (R, or Rate ▸).** Mixer cost is linear in output samples,
   so 22 kHz doubles it while leaving every UI cost identical. If the hitches
   get markedly worse or more frequent, the cause is **mixer throughput**; if
   they are unchanged, it is **CPU contention from drawing**.
2. **Minimize, then play through a known hitch point** (say the ~10 s one),
   twice, listening for it. This re-tests the one contradictory data point
   deliberately rather than in passing.

**Standing theories, cheapest first.**

1. **A periodic kernel activity that holds the CPU or `sch_lock` long enough
   to starve the ring.** The period ("every few seconds") and the duration
   (~1/3 s ≈ 6 ticks) are the shape of something scheduled, not something
   continuous. Candidates with a period: the menu-bar clock cell (§12.1, once
   a second — too frequent), the Control Panel's `cp_tick`, a floppy **motor
   spin-down** or any residual `disk_read` (`disk.inc` raises `sch_lock`
   across int 13h — the one sanctioned long lock, §7), and the Task Manager's
   sampler if a window is open. **A floppy access is the strongest fit**: it
   is periodic-ish, it holds `sch_lock` for exactly the kind of duration
   described, and it happens regardless of what is on screen — matching the
   covered/minimized evidence.
2. **The mixer's own cost against the ring's depth.** If a refill pass
   occasionally has to mix more than one half (`TRK_MAXFEED` bounds it at
   several), the worst-case pass on a 4.77 MHz machine may exceed the ring's
   remaining play time, and the DSP underruns until the next pass catches up.
   That would be periodic in the *music*, not the machine — checkable by
   whether the tail-off lands at the same song positions each loop.
3. **The §34.5 stream watchdog rewinding on a late refill**, which by design
   pauses output rather than playing stale samples.

**How to investigate.** The one measurement that separates theory 1 from 2:
instrument `sch_lock` hold time (or count `disk_read` entries) and see whether
a spike coincides with the tail-off. PERFORMANCE.md's counter-over-QMP recipe
is the mechanism, but the *timing* only reproduces on real hardware or a
cycle-accurate emulator — PCem is the right tool here and QEMU is not.

---

## 2. Heap fragmentation: a second Tracker load says "Out of memory"

> **RESOLVED — two real bugs, both fixed.** The reporter's order of
> operations (open Tracker, load, **play**, close, reopen the file manager,
> open the Task Manager, open Tracker again, load → refused) pinned it. The
> driver's 20KB staging pool was being stranded mid-heap two different ways:
>
> 1. **`DSV_RELINST` only released the FM half.** The cell published
>    `opl_release_inst`, which keys off OPL channels and touches nothing of
>    the Sound Blaster — so **closing a package that had streamed left its
>    staging grant behind, and with it the pool, for the rest of the
>    session**. That is the reporter's exact path. It is now
>    `snd_release_both`, which releases both halves and is published by
>    *either* half attaching (a card with no OPL still has memory to give
>    back). Measured: System heap stayed at 122K after a close-while-playing
>    and now returns to 102K.
> 2. **Tracker held its ring grant for its whole lifetime**, allocating it
>    once and leaving it to teardown — so even a *stopped* Tracker kept the
>    pool claimed. It now frees the grant in `trk_stream_close`, after the
>    worker-park handshake the SDK's one author rule requires. Measured:
>    System 122K while playing → 102K on stop → 122K on replay.
>
> The general lesson is the one §50.3 already states and this is the first
> field proof of: **a long-lived claim in the middle of the heap splits it**,
> and the total free figure will happily say there is room while the largest
> run says otherwise. The analysis below is kept because it is still the
> right way to think about the next one.


**Observed.** On a 384KB machine: launch Tracker, load `BEVERLY.MOD`
(116,085 bytes) — fine. Then load again (or run a second instance), and the
splash reads **`Out of memory`**. The Task Manager at that moment showed:

```
RAM 279/384K   HEAP 208/312K
System   0600  71K   50K heap
Packages       41K
  TRACKER 5640 33K  114K heap
  TaskMgr 5440  8K    —
```

So ~104KB of heap was free and the module needs ~114KB — but the interesting
question is *why the free space could not be reused*, because Tracker **frees
its old buffer before claiming the new one** (`tracker.asm`: the
`OSAPI_MEM_FREE` at the top of the load path runs before the
`OSAPI_MEM_CLAIM`). A straight free-then-claim of the same size should always
succeed on an unfragmented heap.

**What the code says (checked while writing this note).**

- The Sound Blaster driver's **DMA buffer** is claimed in `sbl_attach` — i.e.
  at **boot**, on an empty heap, before any app runs — and freed only at
  detach. It is not a late claim and is *not* the fragmenting party, so the
  original guess ("the driver took a buffer after Tracker had loaded") is not
  quite it for that buffer.
- The driver's **20KB staging pool** (`SBL_POOLKB`) *is* a late claim:
  `sbl_pool_get` takes it on the first stream grant — that is, **after**
  Tracker has already claimed its module buffer — and `sbl_pool_put` releases
  it when the last grant goes. **This is the fragmentation candidate**: it
  lands *above* Tracker's module claim, so when the module is freed the hole
  it leaves is bounded above by the pool, and the largest free run can be
  smaller than the total free.
- §50.3's design anticipates exactly this: package **regions** are claimed
  top-down (`mem_claim_hi`) and data claims bottom-up *because* "a long-lived
  data claim mid-heap permanently splits the space". The pool is precisely
  such a claim, and it is long-lived relative to a load/free cycle.

**Standing theory.** Free-then-claim of ~114KB fails because the freed hole is
no longer the largest contiguous run once the driver's staging pool (and
possibly the Task Manager's own claims, which were open in the screenshot)
sit inside the heap. The total says there is room; the *largest run* is what
`mem_claim` needs, and `OSAPI_MEM_AVAIL` deliberately reports both for this
reason.

**Directions when this is picked up.** In rough order of value-for-effort:
claim the staging pool at attach like the DMA buffer (trading 20KB of resident
heap for no mid-heap claim); or take it from the top like a region; or give
`mem_claim` a compaction-free "grow into the adjacent hole" path
(`mem_regrow` already does something adjacent); or have Tracker size its
request from `OSAPI_MEM_AVAIL`'s **largest-run** figure and say so honestly
rather than failing late. **Do not add a compacting allocator** — a region's
base is its CS and can never move (§50.3).

**A related honesty bug worth fixing at the same time:** the splash says only
`Out of memory`. It should say which figure failed and how short it was —
`bb_avail`'s pattern (§47: say *why* not).

---

## 3. Disk access is horribly slow, and three mechanisms are already visible

**Observed.** Navigating the file manager on real hardware feels far slower
than the work being done should justify. Not a specific operation — the
general texture of using the disk.

**This entry is different from 1 and 2 in one important way**: nothing here has
been measured yet. It was found by *code reading*, while costing the file-type
association plan (`docs/ASSOC-PLAN.md` §2.5.1), and it is recorded because
three plausible mechanisms are visible in the source and each is separately
addressable. The symptom is a field report; the causes below are hypotheses
with line numbers.

**Mechanism A — `dsk_chdir` is a full `disk_mount`.** The body is four lines
and the middle one is `call disk_mount`. So moving between two folders *on the
same volume already mounted* re-validates the BPB against SPEC.md §18.2's
17 rules, re-snapshots the FAT window, re-scans, re-sorts and re-harvests every
icon. Nothing about the volume changed. `dsk_chdir_q` (§18.9) exists and skips
the second half, but skips none of the first.

**Mechanism B — the FAT window is re-read on every one of those.**
`DSK_FAT_SECS` is 9, so that is 9 sectors per directory change on a floppy,
for a FAT that cannot have changed if nothing wrote. §18.8.1 already gives
*driver-backed* volumes a banked per-volume window for exactly this reason
("that is what stops a copy reloading nine sectors on every switch: 45 mounts,
3 loads") — **and a floppy explicitly gets none**, on the reasoning that its
window is the whole FAT and never moves. That reasoning is about the window
never *sliding*; it does not cover re-reading it from the disk.

**Mechanism C — one int 13h per sector.** `dsk_xfer`'s `.sector` loop
recomputes CHS and calls the BIOS once per 512 bytes. On real hardware a
single-sector read has missed the sector under the head by the time the next
call is issued, so consecutive sectors plausibly cost a full revolution each —
200 ms at 300 RPM. Nine "consecutive" FAT sectors would then be ~1.8 s rather
than the ~200 ms one multi-sector read would take. int 13h AH=02h takes a
sector *count*; the loop exists because it also walks the destination, which
`dskw_norm` (§18.4.1) has since made unnecessary — the offset is 0..15 and the
segment advances, so a run within one track and one 64KB page could be issued
as a single call.

**Corroboration, from an unrelated route.** CLAUDE.md already records that a
`SYSTEM.CFG` write is "2+ seconds of completely frozen UI on the floor machine
(mount, data, FAT, directory, FAT, remount)" — which puts a single mount near a
second, and was observed long before this analysis.

**What the harness can and cannot answer.** PERFORMANCE.md's rule cuts
favourably here for once: QEMU is useless about the *time* but exact about the
*work*, and all three mechanisms are **work**, not timing. Counting int 13h
calls per directory change under QEMU — the `inc word [cs:dbg_x]` counter
recipe in CLAUDE.md, on `dsk_xfer`'s `.sector` — answers A and B outright and
sizes C, with no hardware needed. **Do that first**; only C's cost per call
needs the XT.

> **PARTLY FIXED, and the rest deliberately declined.** Mechanism C is done:
> `dsk_xfer` batches a run into one int 13h (SPEC.md 18.91), which took a
> directory change from 12 BIOS calls to 5 and, because the FAT window's nine
> sectors are contiguous, took most of mechanism B with it. Mechanisms A and B
> are **not** being fixed: the only honest swap test is int 13h AH=16h and a
> 5150 with a Tandon TM100 has no change line, so reusing a FAT window there
> would give a file manager that lists correctly and reads garbage. Mechanism D
> is the remaining work. Details below and in docs/DISK-PERF-PLAN.md 3.2/4.
>
> **PICKED UP.** `docs/DISK-PERF-PLAN.md` is the plan for all three
> mechanisms, with the counting phase first, and it carries the budget grant
> that funds it. The directions below are what that plan was built from and
> stay here as the evidence; the plan is where the sequencing, the traps and
> the testing live. This entry stays **open** until the counters say otherwise.

**Directions when this is picked up**, in rough order of value-for-effort:

1. **Count first.** A counter on `.sector` and a walk through two folders.
   Everything below is speculation until that number exists.
2. **Multi-sector int 13h** for a run inside one track and one 64KB DMA page.
   This is mechanism C and probably the largest single win; the run coalescer
   in `dsk_read_chain` already computes runs, and §52.1 records that *both*
   hard-disk transports already batch a run into one command — so the floppy
   path is the one that did not follow.
3. **A same-volume, same-BPB fast path in `dsk_chdir`** — if the volume index
   and the media are unchanged and nothing has written, the BPB and the FAT
   window are already right. The disk-change line (int 13h AH=16h) is the
   honest test on hardware that has it; a media change must still fall back to
   the full path.
4. **Bank the floppy's FAT window** the way §18.8.1 banks a driver-backed
   volume's, so a same-volume chdir does not re-read nine sectors. **This may
   be much smaller than it sounds, and 3 may fall out of it.**
   `dsk_fatw_pick` already states and enforces the safety rule — "only a QUIET
   mount may reuse a banked window; a full mount is a re-validation of the
   whole volume, the disk may have been swapped" — so the swap question is
   already answered, not open. A floppy is excluded from banking because it has
   no donated claim to bank *into*, and its window is `FAT_SEG`: resident, and
   by §18.8.1's own reasoning never sliding. What is missing is not policy or a
   buffer but permission — letting a quiet, same-volume mount reuse what is
   already in memory, which needs one byte recording whose FAT `FAT_SEG`
   currently holds. Check `dsk_fatw0`/`dsk_fatd0` first; they may already carry
   it.

**Mechanism D — the icon harvest re-reads every package on every mount.**
Added after the first three. `disk_mount` step 4 reads the first sector of
every type-1 file in the directory, and mechanism A means every directory
change is a mount — so entering `APPS/` (8 packages) costs 8 extra sector
reads, ~1.6 s at C's revolution apiece, **every time you open that folder**.
It is already correctly conditional — a type-0 file gets no read and a folder
uses the built-in body — so there is nothing to save per *file*; the waste is
in doing it again per *mount*. `docs/DISK-PERF-PLAN.md` §5.5 has the options.

**What this means for the earlier caution below:** it was written before D and
said "do not assume the icon harvest is the cost". Half of that stands and half
does not. A and B are still paid on **every** directory change regardless of
contents, so in a folder of *documents* they are the whole story. But in a
folder of *programs* the harvest is real and can exceed them — which is exactly
`APPS/`, the folder a user opens most. **Count both**; the counters in the plan
separate them.

---

## 4. "Bad package" on a file that is perfectly good, until the Disk window is refreshed (FIXED, verified under QEMU)

> **FIXED — SPEC.md §22.8.** Very nearly the "Directions" below, with the
> counter turned inside out: rather than a mount generation every cache
> compares itself against, `dskw_sync` — the one routine a successful file
> operation passes through — marks `FS_DIRTY` on every Disk window showing
> the folder that changed, and `fm_focus` spends the mark when that window
> next comes to the front, re-listing and repainting **together**. A
> generation counter would have re-listed on a *paint*, which is the half of
> §22.1 that must not cost I/O; a mark spent at the focus is the same
> invalidation charged where the user is already waiting for a window.
>
> Reproduced under QEMU exactly as reported, using Note Pad in place of Gfx
> Bench: Disk window open on `B:APPS`, launch `NOTEPAD.O88`, Ctrl-S (which
> writes `NOTES.TXT` into that folder), close Note Pad. Before: the promoted
> window still says `9 files`, still lists no `NOTES.TXT`, still says
> `Free 1201K`, and double-clicking the row labelled `PAINT.O88` — index 6,
> which the rebuilt globals now call `NOTES.TXT` — opens nothing at all.
> After: the window comes forward saying `10 files` with `NOTES.TXT` in its
> sorted place, and the same double-click launches Paint. **What the harness
> could always have shown, and did not, is the whole lesson of this note**:
> the mechanism was pure bookkeeping, and nothing about it needed a 5150.

**Observed.** On a real 5150, on the boot floppy (drive A:): run `GFXBENCH.O88`
from an open Disk window, press `S` to save its report — which creates
`GFXHERC.TXT` in that same directory — close Gfx Bench, then double-click
`SYSBENCH.O88` in the *still-open* Disk window. It fails as **Bad package**.
It fails again, every time, five times running. Click **Refresh** in that
window and it launches normally. A reboot also fixes it.

**Ruled out — the disk is fine, and so is the write path.** The volume was
dumped afterwards and every file compared byte-for-byte against the originals;
`SYSBENCH.O88` was intact and `os88disk.py --verify` was clean. The failure
does not survive a reboot, and it is *deterministic* within a session, which
also rules out the marginal-media and mis-seek theories that a 40-year-old
drive invites. Nothing was corrupted at any point.

**Mechanism — this one is identified, not theorised.** It is a stale
per-window listing cache (SPEC.md §22.1) resolved against a fresh global
snapshot:

1. A package's `OSAPI_FILE_WRITE` succeeds, so `dskw_write` re-runs
   `disk_mount` — "coherence by remount" (SPEC.md §18.4). The **global**
   snapshot now has the new file in it, sorted into place by name (§19.4).
2. The open Disk window's own cache — `VIEW_KB` of heap behind `FS_VSEG` — is
   **not** touched. Nothing tells a window that a package wrote to its folder;
   only the file manager's own operations rebuild caches.
3. A double-click resolves the clicked row against that cache and hands
   `loader_run` a **directory INDEX** (SPEC.md §22.1: "the loader gets the
   poster's state block in `[ld_pwin]` as well as the index").
4. `loader_run` calls `fmv_sync` — which compares `FS_DRV`/`FS_CWD` against
   `[disk_drive]`/`[dsk_cwd]`, finds them equal, and **returns without
   re-listing**. It has no notion of "the directory changed underneath me".
5. The index is then resolved against the rebuilt globals. Every entry at or
   after the inserted name has shifted by one.

In the observed case the sorted root went

```
... FONTBNCH.O88(2) GFXBENCH.O88(3) SYSBENCH.O88(4) TASKMGR.O88(5) ...
... FONTBNCH.O88(2) GFXBENCH.O88(3) GFXHERC.TXT(4)  SYSBENCH.O88(5) ...
```

so the row the window still labelled `SYSBENCH.O88` was index 4, and index 4
in the new listing is `GFXHERC.TXT`. The loader read a text file, found no
`O8` magic, and said **Bad package** — correctly, about the wrong file.

**Which is why it is rare and why it looked like corruption.** It needs a new
name that sorts *before* something you then launch. A report saved as
`SYSBENCH.TXT` would have sorted after `SYSBENCH.O88` and shifted nothing.
And the error names the file the user thinks they clicked, so it reads as that
file being damaged.

**Directions when this was picked up** (kept for the reasoning; the fix taken
is at the top of this note). The invariant to restore is SPEC.md
§22.1's own sentence — "paints read the cache, actions re-sync". Step 4 is
where it is false: `fmv_sync` re-lists on a *location* change and not on a
*content* change. The cheapest honest fix is a **mount generation counter**:
`disk_mount` bumps a word, each state block records the generation its cache
was built at, and `fmv_sync` re-lists when the generation differs as well as
when the drive or cwd does. That is one word of kernel `.bss`, one word per
state block, and one extra compare on a path that already compares two things
— and it makes every cache in the system self-invalidating, not just this one.

Worth noting what it is *not*: it is not `[dsk_lstale]` (SPEC.md §18.9), which
tracks a debt the **global** snapshot owes after a quiet mount. This is the
opposite direction — the globals are current and a *window* is behind them.
The two want the same counter and neither can serve the other.

A second, independent hardening is worth considering at the same time:
`ld_run_body` could check that the entry it resolved is a **type-1 file whose
name ends `.O88`** before reading it, so a mis-resolved index reports
something better than "Bad package" — §47's say-*why*-not, applied to the
loader.

---

## 5. Multi-sector floppy reads returned the wrong sectors (FIXED, confirmed on PCem)

**Observed.** With SPEC.md §18.91's transfer batching enabled, *every* package
hard-froze the machine as its window drew — Note Pad, Paint, Tracker, the Task
Manager alike. A kernel identical but for one line forcing `AL = 1` was fine.
Reported on PCem; never once reproduced under QEMU, on VGA or Hercules, at
1.44MB or 360KB.

**What was ruled out first, and wrongly.** `AH=02h` answers with `AL` = the
sectors actually transferred, and a real BIOS can return **short** where
SeaBIOS never does. That is true, the transfer loop now advances by the
returned count, and **it did not fix the freeze**. Three app-side handoffs
were then built on top of the still-broken kernel and their freezes read as
three new app bugs — until Note Pad, which had not been touched, froze too.
That is the tell worth keeping: *a component you did not change failing is
evidence about the component you did.*

**Cause.** SPEC.md §18.92. int 1Eh's diskette parameter table carries **EOT**,
the last sector number the FDC may touch, and the IBM PC/XT ROM ships **EOT =
8** — a DOS 1.x number that every DOS since has overwritten at boot. os8088
never did. A single-sector transfer never consults it, so this was inert for
years; the BIOS issues READ DATA with the **multi-track bit set**, so a
multi-sector run reaching sector 9 on a 9-sector track flips to the other head
and returns **head 1's sector 1** instead, with `CF = 0` and the full count.
Correct opening sectors, wrong bytes in the middle, header validates, load
"succeeds", window draws, machine dies on the substituted code.

**Why nothing here could have found it.** SeaBIOS never reads the table. The
boot sector reads `AL = 1`, so §18.91's batching introduced the only
multi-sector int 13h in the system, and the only machines that judge it are
the ones with a real BIOS and a real FDC.

**Fix.** `dsk_dpt_init` copies the ROM's table, patches EOT to the mounted
volume's `[disk_spt]` before every transfer, and installs the vector. The boot
sector does the same for its own load, into `0000:0580` (SPEC.md §18.93).
**`make FLOPPY1=1` is the A/B** — it forces `AL = 1` in both loops and changes
nothing else, so a field run can take the batching out of the picture without
a source edit.

**Confirmed on PCem**, batching on, apps launching. Kept here because the
mechanism is worth reading before anyone touches a transfer loop again: the
harness cannot see this class of bug at all, at any speed, because the
difference is in the *BIOS* and not in the timing.

---

## 6. The cursor washes out to white while the mouse is moving (Hercules) (FIXED, awaiting field confirmation)

**Observed.** On the 5150's Hercules card, moving the mouse around makes the
arrow's white outline appear to come away from the black body — "the shadow
desyncs from the pointer" — and, watched more closely, what it looks like is
that the **whole cursor turns white** for an instant. Intermittent, only while
moving, and it never persists: stop moving and the arrow is correct.

**Long-standing, and newly visible.** It predates SPEC.md §7.1's cursor work.
What changed is that the *other* cursor flicker went away: `gfx_lock` /
`gfx_unlock` used to erase and redraw the arrow on every lock hold, including
holds that drew nothing, and that blink masked this one. Fixing the loud
problem exposed the quiet one — worth recording as a shape, because it is the
second time in this file that a fix has revealed its neighbour.

**Ruled out — it is NOT the white and black passes coming apart.** That was
the first theory and it is measurably wrong on this adapter. On a 1bpp
adapter `cur_put_mono` reads the byte under the arrow, ORs the outline in,
ANDs the body out and writes it back **in one store** (§7.1), so the halo and
the body reach the glass in the same bus cycle and cannot separate. It *was*
two passes on VGA, and that has since been fused too — but the reporter is on
Hercules, so that is not this.

**Ruled out — the drawn cell is not wrong.** A checker reads the kernel's own
`cur_save`, `cur_off`, `cur_rows`, `cur_b1ok` and the two bitmap tables out of
guest RAM, reconstructs `(saved | white) & ~black`, and compares it against
the framebuffer. Sixteen cursor positions — every shift 0..7, both screen
edges, over glyphs, over the desktop dither and inside a window — all match
exactly, and the row-0 address it derives independently agrees with
`[cur_off]` every time.

**Standing theory: it is the ERASE-then-DRAW gap, and what you are seeing is
the background.** Moving the cursor is two separate framebuffer walks —
`cur_get` puts the old cell's saved bytes back, then `cur_put` saves and draws
at the new one — so between them the cell holds the *background*. Read back
from the machine, that background is `ffff` on all twelve rows inside a window
and `aaaa`/`5555` (the 50% dither) on the desktop. **A cell of `ffff` is a
solid white blob exactly where the arrow was**, which is the symptom as
reported.

The timing fits. The pair is **5.41 icount PIT counts ≈ 568 guest
instructions ≈ 1.3 ms** on a 4.77 MHz 8088 (PERFORMANCE.md Part 9 Set 7),
against a **20 ms** Hercules frame — so the window is ~6.5% of a frame, on
every mouse packet. At ~40 packets a second while moving, that is a couple of
opportunities a second for the beam to scan that cell mid-update. "Sometimes",
"only while moving", "never persists". A long-persistence monitor phosphor
would smear it further toward white rather than showing a clean flash.

**Fixed: a move writes every byte exactly once** (SPEC.md §7.1.2,
`cur_move_mono`). The property that matters is not that the walk be a union —
it is that no framebuffer byte be written twice. The two passes still walk the
old cell and the new cell exactly as they did, and each byte is written once
because **pass 1 skips the bytes pass 2 is going to write**, and **pass 2
takes their background from the save buffer rather than from the screen**. So
there is no union to bound and no gate: cells that do not overlap degenerate
to the old behaviour on their own, because the skip never fires and the
background always comes from the screen.

It needs a second 24-byte save buffer, since pass 2 reads the old one while
filling the new, and the two are swapped by pointer so nothing is copied. The
`GFX_UNLOCK+LOCK pair` row is unmoved at 544 counts against 541 (0.6%, noise)
— the move is a different path from the lock's, and the pair pays only the one
extra indirection for the buffer pointer.

**Verified the only way a save-under can be.** A dense walk — 37 moves with
byte-column deltas of 0, ±1, ±2 and larger, in every shift phase, plus the
right and bottom screen edges where the second byte and the lower rows are
clipped away — then park the cursor back where it started and compare the
whole screen: **0 differing pixels of 237,600**. A wrong background is
permanent rather than transient, so a zero there means every one of those
moves restored exactly. And the test has teeth: with pass 2's background
source deliberately broken back to "always read the screen", the same walk
leaves **98 permanent differing pixels**.

What is NOT fixed is the planar path — VGA still moves erase-then-draw,
because its save is four planes through Read Map Select and cannot take a
background from a buffer. Its *draw* is one store now (§7.1), which was the
larger of the two windows there.

---

## 7. The floppy is 6x slow because int 13h answers AL = 1 (FIXED, 3.9x measured)

**Observed.** PERFORMANCE.md Part 9 Set 11, on the IBM 5150 the whole disk
ladder was calibrated against. `sysbench`'s floppy block, same machine, same
media, same test, kernel before and after both SPEC.md §18.4.2 (run
coalescing in `dskw_rdata`) and §18.91/§18.93 (per-track batching in
`dsk_xfer` and the boot sector):

| | Set 1 (before) | Set 11a (after) |
|---|---|---|
| 16 KB read, cold motor | 7.63 s | **8.07 s** |
| 16 KB read, warm | 7.80 s | **8.18 s** |
| a one-sector file, open and read | 796 ms | 796 ms |
| throughput | 2,100 B/s | **2,001 B/s** |

Set 1's prediction was "about **9x** on every load in the system, which is the
largest single number in this document". It measured **1.0x**, and 6% the
wrong way. `boot ticks` says the same thing independently: 138 kernel sectors
at the unbatched 238 ms is 32.8 s, and the boot measured **708 ticks
(38.9 s)** against a predicted 4–5 s.

**Already ruled out.**

- **Fragmentation.** Every file on the field image is one run —
  `KERNEL.SYS` 69 clusters, `BENCH.DAT` 16, all `runs=1` — so the coalescer
  hands `dsk_xfer` a single 32-sector run and it splits only at the track and
  the DMA page, exactly as designed.
- **A one-machine artefact.** The Toshiba T1100 Plus, a different real
  machine with a different drive and different media, reads at **2,161 B/s**.
  Two real machines, one wall. (The Packard Bell 286 does gain: 9,041 B/s.)
- **The emulators.** They report 13,562 B/s (PCem) and 59,795 (MartyPC) and
  are worthless here: neither models rotational latency, which is the entire
  thing the batching exists to avoid. A green run on either proves nothing
  about this, and one was taken.
- **The code not being built in.** `FLOPPY_ONE` is not defined; the batching
  block and `dsk_dpt_init`'s EOT patch are both in the shipped kernel.

**The A/B has been run, and the answer is worse than "nothing"**
(PERFORMANCE.md Part 9 Set 13). Same machine, same session, same kernel but
for the knob:

| | batched | `FLOPPY1=1` |
|---|---|---|
| 16 KB read, cold motor | 8.90 s | **7.69 s** |
| 16 KB read, warm | 8.73 s | **7.58 s** |
| throughput | 1,875 B/s | **2,161 B/s** |
| **`boot ticks`** | **715** (39.3 s) | **621** (34.1 s) |

**The batching costs 13% on the boot and 15% on a file read.** Two
independent measurements, one sitting, with the drawing rows of the same two
reports 0.13% apart — so it is not the machine drifting.

It also answers the question the note could not: the multi-sector command
**is** reaching the hardware. Were it being silently decomposed into
single-sector transfers the two columns would be identical, and they are 94
ticks apart on the boot alone. Something about a multi-sector `int 13h` on
that drive, that controller or that media costs more than the revolutions it
saves. The mechanism is still unknown — candidates worth a look are the ten
non-EOT bytes `dsk_dpt_init` copies out of the ROM's diskette parameter
table (step rate, head settle, motor start), a run that crosses a track
boundary mid-command, and the physical interleave of media written by
`dskimage` — but the measurement does not depend on knowing which.

**Do not quote the 9x**, do not cost a future disk change against it, and
treat SPEC.md §18.91/§18.93's stated gain as refuted on the target machine.

**And the batching is the smaller half of the problem.** The owner then ran
the decisive cross-check: DOS 3.3 on the same 5150, the same Tandon TM100-2
and the same disk copied roughly **62 KB in about 5 seconds** — ~12,700
bytes/second, *including* a per-file round trip because that is how `COPY`
works. A 360KB disk turns at 300 RPM, so a revolution is 200 ms and a
9-sector track holds 4,608 data bytes:

| | bytes/second | sectors per revolution |
|---|---|---|
| a whole track per revolution (1:1) | 23,040 | 9 |
| a 2:1 interleave | 11,520 | 4.5 |
| one sector per revolution | 2,560 | 1 |
| **DOS 3.3, this machine** | **~12,700** | **5.0** |
| os8088, `FLOPPY1=1` | 2,161 | **0.84** |
| os8088, batched | 1,877 | **0.73** |

So we are **6x slower than DOS on identical hardware and media**, and we are
not merely missing the next sector — at 0.84 sectors a revolution we are
missing whole turns. **That rules out the drive, the controller and the
physical interleave**: whatever they are, DOS gets 5 sectors a revolution out
of them. The batching being *worse* is a second symptom of the same fault
rather than a separate puzzle — if a multi-sector command streamed at all,
nine sectors in one call could not cost more than nine separate calls.

**The instrument is built and shipped.** `sysbench` now has a raw `int 13h`
block that calls the BIOS directly, with no os8088 code in the path, timing
one sector, a whole track in one call, and the same track one call at a time
— plus the four diskette-parameter-table bytes the BIOS is actually using.
It splits the question cleanly:

- `int 13h track, 1 call` near **one revolution** → the hardware streams and
  the fault is entirely in `dsk_xfer`;
- near **nine** → the BIOS or the media does not stream, and no batching
  above it could ever have helped.

Two things on that table are already worth watching. **`DPT motor start`** is
in eighths of a second and reads **8** under QEMU — a full second before a
transfer the BIOS believes needs the motor started, where DOS installs its
own table with smaller values; if the BIOS thinks the motor has stopped
between our calls, that alone is the whole gap. And **`DPT head settle`**,
for the same reason.

**The instrument answered it** (PERFORMANCE.md Part 9 Set 14). Three rows,
every one landing on a whole number of the 200 ms revolution:

| | measured | revolutions | bytes/second |
|---|---|---|---|
| `int 13h 1 sector` | 199.1 ms | **1.00** | 2,571 |
| `int 13h track, 1 call` | 384.5 ms | **1.92** | **11,985** |
| `int 13h track, 9 calls` | 2,004.8 ms | **10.02** | 2,298 |
| os8088's own 16 KB read | 8.57 s | **1.34 per sector** | 1,912 |

So the media is **2:1 interleaved** — a whole track in one command takes two
turns, 11,520 B/s by arithmetic against 11,985 measured — and **the drive,
the controller and the BIOS all stream perfectly when asked for nine sectors
at once**. `int 13h track, 1 call` *is* our batched read done right, and it
is **6.3x faster than what `dsk_xfer` achieves**.

The decisive comparison is the last two rows against each other. os8088 costs
**1.34 revolutions a sector** — worse than the 1.00 that nine separate BIOS
calls cost, and nowhere near the 0.21 of one batched call. **Whatever
`dsk_xfer` is issuing is not reaching the hardware as multi-sector
commands**: §18.91's batching is either not forming the runs it believes it
is, or something between it and the BIOS is decomposing them.

That also explains note 7's own A/B with no second mechanism. If the batched
path issues per-sector commands anyway, `FLOPPY1=1` measuring 15% faster is
just the batched path's extra arithmetic with none of its benefit — so the
question "should the default flip" is **dead**: there is nothing to flip
between, both paths are doing the same thing at the hardware.

**The call counter was built and it answered** (PERFORMANCE.md Part 9
Set 15, SPEC.md §18.94). One 16 KB `OSAPI_FILE_READ` on the 5150:

| | |
|---|---|
| sectors moved | **148** |
| int 13h calls | **34** |
| longest run | **9** |
| controller resets | **0** |
| sectors per call | **4.35** |

**The splitter works and was never the problem.** 4.35 sectors a call with a
longest run of 9 is not one-per-call, nothing retried, and at 252 ms a call
each transfer costs about what the BIOS charges (199 ms for one sector, 398
for nine). §18.91 is doing exactly what it says.

**The fault is that a 32-sector file cost 148 sectors of traffic.** 4.6x, and
that one number is the whole gap: 32 sectors batched 9 to a call would be ~4
calls and 1.59 s, against 34 calls and 8.57 s measured. So every mechanism
this note has blamed in turn — the interleave, the drive, the controller, the
BIOS, the batching — has now been measured and cleared, and what is left is
**116 sectors nobody asked for**.

**Where they come from is machine-dependent**, which is why reading the
source never found them: the same binary on the same image under QEMU moves
**34 sectors in 6 calls** — 32 of data and 2 of directory, very nearly
optimal — with **zero `disk_mount` calls**. The instrument now measures each
operation on its own and counts mounts inside it, and a one-sector read
beside the 16 KB one isolates the fixed cost from the data (QEMU: 3 sectors,
3 calls). The next field run says whether the 116 are a remount, the
directory walk, or something in the chain walker that only fires on real
geometry.

**FOUND, by the (LBA, run) trace** (PERFORMANCE.md Part 9 Set 16). The LBA
advances by one while the run counts down — 7,6,5,4,3,2,1 then 9,8,…,1 —
thirty-four calls, thirty-three distinct LBAs, nothing read twice and nothing
skipped. **`dsk_xfer` asked for nine sectors, the BIOS moved nine, and
answered `AL = 1`.** §18.91's short-count handling believed it, advanced one
sector and re-asked for the rest, so every sector cost its own revolution.
The data stayed correct because the sectors it re-read were sectors it had
already read; `CF` stayed 0 so nothing retried; and QEMU cannot show any of
it because SeaBIOS returns the full count.

That single fact retires every earlier entry in this note. It is why 148
sectors were requested for 32; why we cost 1.34 revolutions a sector against
the BIOS's 0.21; why DOS is 6x faster on the same drive and media (it trusts
`CF`); and — the one that looked like a fact about hardware — **why batching
measured 15% SLOWER than one-sector-per-call**: the same call count plus the
run arithmetic, for nothing. The splitter did all its work and then threw the
result away one sector at a time.

**The fix is to read the contract.** `CF = 0` is the BIOS saying the whole
request completed; `AL` is not. `dsk_xfer` advances by `[dsk_run]` now, which
is what DOS does. `make DISKAL=1` restores the old behaviour for an A/B.

The old reasoning was sound and is kept in view: a BIOS *may* terminate a
multi-sector read early, and advancing by the request would then step past a
hole and produce a file with a gap and a shifted tail — first sector intact,
so the load succeeds and the package dies when its code is reached. Note 5
already recorded that the short-count fix "changed nothing" when it landed;
it changed something now. Because that risk is real if the reading is wrong,
**`sysbench` verifies the file**: `BENCH.DAT` holds `(i >> 9) & 0xFF`, so
every byte of sector *n* is *n*, and a gap or a repeat names itself.

**Confirmed on the iron** (PERFORMANCE.md Part 9 Set 17): a 16 KB read is
**2.09 s against 8.29**, throughput **7,457 B/s against 1,912** — **3.9x**,
on the ordinary kernel. Everything else in the report is unchanged to within
a tick, and `read 1 sector file` is 810 ms both times because a one-sector
read never had a run to lose.

**The BOOT SECTOR had the same bug and it was never fixed with the kernel.**
`read_run`'s `.done` believed `AL` too, which is why `boot ticks` did not
move in Set 17 while the file read got 4x faster: 140 sectors at a revolution
each is 33 of the 40 seconds. Both loops read `CF` now, under the one
`DISKAL=1` knob. **Measured: 726 ticks to 181 — 39.88 s to 9.94, 4.0x**
(Set 18). A cold boot of this OS on a 4.77 MHz 8088 is ten seconds.

**And the data check passed on iron, on two machines.** Set 17 took its 4x
with the guard switched off by a scoping mistake; it is unconditional now and
reads `data check, 32 sectors  OK` on the 5150 and on a Compaq Portable III —
two BIOSes, two drives, one of them 1.2 MB reading 360 KB media. Trusting
`CF` reads the right bytes.

**Still 1.55x short of the ceiling**, and the arithmetic says where: the
BIOS's own whole-track-in-one-call is 11,570 B/s, 32 sectors in ~4 calls of
~400 ms is 1.6 s, and we measure 2.09 — about two extra calls. A run
coalesces only up to the track and the DMA page, and a file's first cluster
is rarely track-aligned. Worth about a second on every large load; not
chased yet.

**And the check that licensed the change did not run.** `sb_verify` was
written inside the `DISKCNT=1` block, the field booted the plain kernel, so
it printed "this kernel carries no disk instrument" and skipped — the 4x was
taken with the one guard that made it safe switched off. It is unconditional
now. *A guard that only runs on the build you are not shipping is not a
guard.*

**And the target is now a measured number rather than a model.** DOS copying
the single 170 KB file off the same disk runs at **~13,390 B/s** (about 15
seconds, 2 of them not spinning, stopping three times for a 64 KB buffer),
the BIOS's own one-call track read is **11,570**, and 2:1 interleave by
arithmetic is 11,520. Three independent figures within 16%. **11.5-13.4 KB/s
is what this drive does.**

---

## 10. A package cannot safely call int 13h (FOUND, not fixable from a package)

**Observed.** `sysbench`'s raw `int 13h` block **hard froze the IBM 5150** on
the first run after a cold boot, then ran normally after a reboot and
produced note 7's answer.

**The mechanism is structural.** A BIOS runs its disk handler, and the IRQ6
nesting inside it, on whichever **256-byte task stack** is current
(SPEC.md §8) — here on top of the benchmark's own frame, `bl_run`'s and
benchlib's. And the kernel's `dsk_xfer` holds **`sch_lock`** across every
`int 13h` so nothing can switch underneath one; a package has no way to ask
for that, because there is no slot for it. Whether it dies depends on where
the PIT tick lands relative to the BIOS's wait loop, which differs every
boot — hence intermittent, which is the worst kind.

`tests/stackprobe` exists precisely because SeaBIOS hides a real BIOS's
interrupt stack use, so QEMU can never show this; the block ran clean there
every time.

**Kept, not fixed.** It is the only instrument that could answer note 7 and
the answer was worth a 6.3x correction, so it stays in the harness with the
hazard written at the top of it. **Nothing shipped may copy it**, and if the
BIOS-direct number is ever wanted routinely it belongs in the kernel behind a
build knob, where it can hold `sch_lock` like every other transfer does.

---

## 8. `GFX_UNLOCK+LOCK` was 9x dearer on the 5150 (CLOSED — it was not the mouse)

**Observed.** PERFORMANCE.md Part 9 Set 11, `gfxbench`'s composite block:

| 5150 Herc | 5150 CGA | T1100 Plus | PB 286 | PCem | MartyPC |
|---|---|---|---|---|---|
| **2,241 µs** | **2,402 µs** | 119 µs | 36 µs | 223 µs | 246 µs |

Normalised against each machine's own `GFX_PIXEL` — which removes the clock
entirely — the 5150 is **3.49** and every other machine is **0.16–0.38**.
That is the only row where the 5150 and MartyPC disagree at all; the other 45
gfxbench rows agree within 0–4%. So the 5150 is doing *different work* in
this pair, not the same work more slowly.

**What the pair can vary by.** With no back buffer (both mono adapters,
`[bb_dbl]` = 0) `gfx_flush` returns before it can spend the deferred hide, so
`gfx_unlock` takes `.never` → `cur_lazyend` every iteration. That path is a
few compares **unless `[mouse_x]`/`[mouse_y]` differ from
`[cur_drawn_x]`/`[cur_drawn_y]`**, in which case it is a full `cur_move` —
which is about the right size to explain 2 ms on a 4.77 MHz machine.

**Already ruled out.**

- **An interrupt storm.** This is the one row in either harness that cannot
  be measured with interrupts off (`gfx_lock` ends with `sti` by contract),
  which makes it the obvious suspect — but `sysbench` puts the whole
  interrupt load at **3%** on the same machine in the same session.
- **Cursor position.** Under QEMU the pair is **0.5 instructions an
  iteration** with the pointer in the middle of the screen and again with it
  parked in the top-left corner, so a clipped cursor cell is not it.
- **The adapter.** Both 5150 columns are expensive and the T1100's CGA column
  is not.

**CLOSED by PERFORMANCE.md Part 9 Set 13, and the answer was neither the
mouse nor the machine.** With the pointer demonstrably untouched — the new
`-- the run --` block reporting **0 of 120** samples moved and a **0 x 0**
bounding box — the row measures **290 µs**, against PCem's 223 and MartyPC's
246. A deliberate second run with the pointer moved continuously (64 of 120
samples, a 706 x 332 box) measures **369 µs**, so the mouse is worth **+27%**
and never 9x.

What changed is the KERNEL. Every other row moved 0-4.4% between the two
field builds and `GET_TICKS` and `GFX_BLIT4 solid` did not move at all, so
the machine and the operator are both ruled out. Two commits in that range
touch the path — SPEC.md §7.1.4.1 (`cur_lazyend` saving every register) and
the COM1/COM2 probe (§9.5) — and which of them did it is not established;
§7.1.4.1 only ADDS pushes, so on its face it should have made this row
dearer rather than cheaper. If the anomaly returns, the pointer block now
answers the operator half in the report itself.

The rest of this note is kept because the reasoning is still the reasoning,
and because the mechanism it names was measured independently. SPEC.md §7.1.4.1 — a *different* bug,
`cur_lazyend` eating the caller's registers — was found by flooding the
machine with mouse packets and counting, and the count is the interesting
part here: **279 `cur_move` calls in 972 unlocks**, so under continuous
movement roughly **29% of unlocks take the expensive path**. The row is 100
iterations and lasts about 224 ms on that machine, which is long enough for a
hand resting on a mouse, or a ball mouse picking up the machine's own
vibration, to keep that rate up throughout.

If that is the answer the row is honest and the lesson is that it measures
**unlock+lock with the pointer moving**, not idle — which is arguably the
number that matters, since CLAUDE.md prices this pair at 21.8% of a Missile
Command session and a Missile Command player never stops moving the mouse.
It would then want taking twice, labelled. If it is *not* the answer,
something makes `cur_drawn_*` chase `mouse_*` without ever catching it, and
that is a real bug.

**The field disks predate the §7.1.4.1 fix**, so the runs above were taken on
a kernel where a `cur_move` inside an unlock destroyed the caller's
registers. That does not invalidate the data: `bl_time` banks CX around
`call word [bl_body]` and keeps every accumulator in memory, so the harness
survives a body that clobbers everything — which is its documented contract.
A re-run should still be on a fixed kernel.

---

## 9. A stale cursor save-under, restored mid-benchmark (EXPLAINED — harness only)

**Observed**, once, by the 5150's owner during the Part 9 Set 11 runs: the
mouse was left resting **over a window's title bar, near the left side**, and
the arrow was "visually corrupted as the bench ran". One sighting; the
machine and the adapter are both unremembered — the same session covered a
5150 with a Hercules and a CGA card, a T1100 Plus and a 286.

**It is easily repeatable on the machine**, and two photographs of it exist:
the pointer resting on the title bar with its 8x12 cell hanging over the
bottom edge into the content, and then, mid-suite, a light fragment left
inside the black rectangle the benchmark is filling. The reporter's own
description is the useful part — *"the corruption starts higher up, then
clips down to this, then disappears as the next tests overwrite it"* — which
is the signature of a **stale cursor save-under**: pixels put back by
`cursor_hide` that describe the screen as it was before the app drew there.

**Not reproduced under QEMU, at either position, on either adapter.** The
arrow was checked **exactly** rather than by eye — the composite
`(background | white) & ~black` computed from `CUR_ARROW` and compared
against the framebuffer cell:

| | |
|---|---|
| CGA, current kernel | before / mid-run / after **pixel-identical**, whole screen |
| Hercules, current kernel | **0 of 96** cell mismatches, before and mid-run |
| Hercules, `16844dd` — the build the field disks were made from, *before* SPEC.md §7.1.4.1 | **0 of 96**, before, mid-run and after |

A second round, after the photographs arrived, added the **straddle**
position — the cell half in the title bar and half in the content, which is
what the photograph shows and what the first round did not test — sampled at
60 ms intervals straight through the suite (250 framebuffer dumps over one
QMP connection, because the run outpaces one screenshot a second even under
`-icount`). Every frame in which the sandbox was being filled had the cursor
correctly **absent**, and the title bar intact.

So four things are ruled out:

- **the deferred hide not being spent by `wm_draw_title`** — it draws through
  `gfx_fill` and `gfx_hline`, and both spend it;
- **a primitive missing the `cur_unlazy` hook** — `gfx_blit4` and
  `gfx_scroll` are off §11.3's clip list and both call it explicitly, and
  everything else reaches it through `GFXCLIP` or `fnt_unlazy`;
- **a package arming a clip region without spending it** — `wm_clip_set`
  calls `cur_lazyck` at its `.done`, and the API slot goes straight there, so
  a cursor inside the window's own frame rect is hidden before any clipped
  primitive runs;
- **the mouse ISR drawing into a half-drawn frame** — it tests
  `[gfx_lock_flag]`, not `[cur_lazy]`, so for the whole of a lock hold it
  only sets `[cur_dirty]` and moves nothing;
- and **the pre-§7.1.4.1 register bug on its own**, which the third row above
  tests directly.

**What is left needs the real machine**, and most plausibly a real mouse.
Every negative above was taken with QEMU's `msmouse`, which sends nothing at
all while it is not being driven — so the one path in the suite where the
cursor is legitimately put back on screen (`gfx_unlock` in the
`GFX_UNLOCK+LOCK` row calls `cursor_show`; the ninety-nine unlocks after it
take `cur_lazyend`) is never followed by any cursor motion here. On the 5150
a ball mouse resting on the same desk as the drive motor is not silent — §7.1.4.1 counted `cur_move` firing on
279 of 972 unlocks under a flood of packets, and on the pre-fix kernel that
call ate the caller's registers. It is also the same unknown that note 8
turns on, which is why both got the same instrument rather than two.

**What to do next time, and it is now automatic.** `tests/benchlib.inc`
samples the pointer twice per row, outside every timed span, and every report
ends with a block that says whether it moved: `pointer moved (samples)`,
`pointer x span` / `y span` (a nudge that returns between samples moves no
sample but still widens the box — the counter alone shipped first and would
have missed exactly that), and where the pointer started and ended. A run
with all three at **0** rules the mouse out; a run with the pointer parked on
a title bar and a non-zero span, that then corrupts, confirms it in one
sitting. The operator's side of it is to leave the mouse alone from before
`R` is pressed.

**Two of the three observations came back, and together with the reporter's
own reading they explain it.** In their words: *"the 'corruption' is just the
background that was there BEFORE the video tests started running,
restored"*, and

- **the cursor is hidden for almost the entire run**. On the bare desktop, or
  low in the bench window, it disappears and the saved background still
  matches what is underneath, so nothing looks wrong;
- **the fragment is gone at the very next drawing row**, because that row
  writes over that part of the screen.

So it is a save-under captured before the suite started, restored once,
somewhere the suite had since drawn — and it is **visible only where the
saved pixels disagree with what is now underneath**, which is why the
straddling position over the sandbox is the one that shows it and every
other position does not.

**It is a property of this harness and not of the window manager, and that
is the reason to stop here.** `gfxbench` is the only program in the tree that
writes the framebuffer **directly** — its raw-bandwidth block is `rep stosw`
at `[vid_rseg]`, deliberately, because measuring the bus is the point — so it
is the only program that can put pixels on screen without passing a routine
that spends the deferred hide. An ordinary application cannot: every path it
has runs through `GFXCLIP`, `fnt_unlazy` or an explicit `cur_unlazy`, and the
four mechanisms ruled out above cover all of them. It has also never moved a
number: the artifact lands in the sandbox the benchmark is already
scribbling in, and the report is redrawn afterwards.

Left open rather than fixed, deliberately. The fix would be for `gb_bw` to
force the hide before its first raw store — there is no API for "hide the
cursor", so it would have to draw something through a kernel primitive first,
which is a wart in a bandwidth measurement — and it buys nothing but a
tidier screen during a benchmark nobody watches. Worth doing only if the same
shape ever shows up somewhere that is not this harness.

---

## 11. Freehand circles in Paint come out as long straight chords (FIXED, awaiting field confirmation)

**Observed.** On the 5150's Hercules card, drawing two circles freehand in
Paint's full screen: "it feels like the mouse is not being read, and it just
keeps going in a straight line. The bigger circle, it missed one entire curve,
and way overdrew my intended motion in every direction." The evidence is
`TWOCIRC.GIF`, saved off the machine — a 674×258 canvas (the Hercules
fullscreen size) holding long dead-straight chords, with smooth curve only
where the hand was moving slowly.

**It is not the mouse; it is the drawing.** `pt_seg` issued one `gfx_fill` per
pixel at width 1 and a second whenever the minor axis moved. At the field
machine's 933 µs for a 1×1 fill that is 373 ms for a 300-pixel chord, which
gives the pencil a **maximum drawable speed of ~1,000 px/s** — and a hand
drawing a circle passes that on the fast part of the arc. Past it the lag
compounds: a longer chord takes longer to draw, so the hand is further away at
the next sample. "Missed one entire curve" is one arc collapsed into one
chord; "overdrew in every direction" is the user still moving because nothing
had appeared yet, and the app eventually drawing straight to where the hand
had got to.

**Two fixes, SPEC.md §42.8.** A width-1 segment's screen half is now one
`OSAPI_GFX_LINE` (576 drawing calls → 66 over one scripted stroke, and the
ceiling moves to ~6,000 px/s), and the fullscreen bracket's 55 ms per-sample
floor is gone on 1bpp adapters, where nothing is owed a present.

**What it cost to get right, and the finding underneath it.** The canvas is
what a repaint draws from, so Paint's own walk had to become `gfx_line`'s
exactly — the DDA form it used put almost every pixel of a chord somewhere
else (3,015 differing bytes over twelve strokes, against 0 with the fast path
off). And the kernel's own `gfx_line` **does not rasterize the same on every
adapter**: `gfx_line_raw` sends mono to `gfx_line_mono` and VGA to
`gfx_line_runs`, and the same test returns 0 on Hercules and CGA but 663 bytes
on VGA. Nothing depends on the two agreeing today — Missile Command draws and
erases through the same path on the same machine — so it is recorded rather
than fixed, and Paint's fast path is gated to 1bpp.

---

## 12. The mouse does nothing for a third of a second, then jumps (FIXED, confirmed on the 5150 and MartyPC)

**Observed.** After the desktop appears, the first mouse movement of the
session did nothing for roughly a third of a second, then the cursor jumped
and tracked perfectly for the rest of the session. **1/1, every boot** — not
intermittent, which is the detail that pointed at the cause.

**Why the harness could not show it.** QEMU's `msmouse` is not a UART-level
device: it ignores MCR/DTR outright and emits packets during boot regardless,
so `[mou_seen]` is 1 and `[mou_hpst]` 2 straight out of `make test`. The
container therefore sits permanently in the state that comes *after* the bug,
and no amount of scripting reaches it.

**Two causes, not one, and the smaller one looks like the culprit** (SPEC.md
§9.4.1). The kernel never *identified* a mouse — `[mou_seen]` is set only by
the ISR on a completed packet, so a plugged-in mouse nobody has touched is
indistinguishable from no mouse. That cost (a) the §9.5 contest, where a
two-port machine discards its first eight clean packets, deterministic and
worth ~200 ms of continuous motion; and (b) `mou_hotplug` power-cycling the
mouse every 3.19 s, 20.7% of it dead, with its first cycle landing on the
desktop. The first estimate here attributed the whole symptom to (b) and was
wrong: (b) is probabilistic in where a nudge lands and cannot be 1/1.

**Fixed** by reading the identify burst `mouse_init` already provokes and then
discards (SPEC.md §9.4.1), and **confirmed on both machines through §9.4.2's
published block**:

- **the 5150** — one serial port, so no contest: `first byte 004D`,
  `identified 1`, **`poller stamp 0`**. A real Microsoft mouse on a real card
  answers the DTR/RTS raise with exactly one `'M'` and then goes silent, which
  is the premise the whole fix rests on and the one thing no emulator could
  have settled.
- **MartyPC** — two ports, so the contest is live: `[mou_need]` `01 08`, the
  threshold drop doing the visible work.

**Still owed:** a real two-port machine. The Compaq Portable III is that
machine (§9.5.2 is its bug) and is not booting these images yet.

**The reusable lesson** is about the report rather than the code: *"1/1"* was
worth more than any measurement taken here. A deterministic symptom cannot
have a probabilistic cause, and that alone ruled out the mechanism the
container had made easy to measure, in favour of the one it could not see.

## 13. The mouse is detected, moves exactly once, then freezes (Compaq Portable III) (FIXED, CONFIRMED on the machine)

**Observed**, on the Compaq Portable III, booting `elendilon` at `c4aaab2`:
the mouse is found, the cursor moves once, and then nothing for the rest of
the session. The operator worked around it by booting with the mouse
unplugged, driving to the benchmark with §9.6's keyboard mouse, plugging the
mouse in and running the tool — which is why the run's `mouse found 0` is not
a second bug.

**This is §9.5.2's machine and NOT §9.5.2's symptom**, and that distinction
is the whole of the diagnosis. §9.5.2 was "the mouse is never seen at all",
caused by an ISR that read the port its *vector* named; it was fixed, and the
fix works — the mouse is now found, which is the movement being reported.
What was left behind was the same wrong assumption in the retirement path.

**Cause** (SPEC.md §9.5.2.1). `mou_lockon` retires the losing ports with
IER = 0 *and* an 8259 mask taken from `[bx+mou_masks]` — the line the row's
**base** implies. This machine's mouse is at 0x2F8 and pulls **IRQ4**, so
retiring the 3F8 row masked the mouse's own line. Eight clean packets settle
`[mou_port]`, `mou_hotplug`'s stand-down path calls `mou_lockon` on the UI
task's next pass, and the line goes into OCW1. One movement, then silence,
permanently.

**The container CAN show this one**, which is worth recording because §9.5.2
and note 12 could not. `make test MOUSEPORT=com2irq4` is exactly this
machine — a live silent UART at 3F8 and the mouse at 2F8, both on IRQ4 — and
the reproduction is unambiguous: the cursor tracks to (380, 300) as the port
settles and then does not move again through four further moves, with
`[mou_seen]` = 1 and `[mou_port]` = 2 the whole time. What made it reachable
here is that the failure is in the *kernel's own* 8259 bookkeeping rather than
in anything about how a real UART behaves, and QEMU's `irq=` option models
the one hardware fact that matters.

**Fixed** by taking the mask from the line the winning packets actually
arrived on (`[mou_line]`, stamped by the ISR entry and banked by
`mou_claim`) instead of from the base. Verified on all four configurations —
`com1`, `com2`, `com1irq3`, `com2irq4` — each tracking to an exact final
position, because the two ordinary ones are what a fix like this silently
breaks. `[mou_line]` is published in §9.4.2's block, so `winning row 2`
beside `winning IRQ hex 10` now names a cross-wired card on any machine that
runs `sysbench`.

**Confirmed on the Compaq Portable III**, which reports exactly that pair —
`winning row (0/2) 2` with `winning IRQ hex 10=4 0010`: the mouse is the
device on 0x2F8 and its packets arrive on IRQ4. The mouse works for the whole
session. Two more numbers in the same block are worth keeping, because they
are the first real-hardware confirmation of §9.4.1: `packets needed COM1 8`
against `packets needed COM2 1`, which is the identify burst having named
COM2 the only mouse-shaped port and dropped its threshold — so the modem-ish
device on COM1 was still owed its full eight while the mouse settled on its
first packet.

**The reusable lesson.** A wrong assumption usually has more than one reader,
and fixing the one that produced the symptom leaves the others armed. §9.5.2
found "the base does not determine which port has the byte" and changed the
code that *reads* bytes; the same sentence was true of the code that *masks
lines*, one routine away, and nothing in the fix or its testing had any
reason to look there. The second failure was not a regression and not a new
bug — it was the rest of the first one, and it could only surface once the
first fix let the mouse get far enough to be retired.

## 14. The 5150's clock was not detected once, and has not failed since (OPEN, instrumented, one observation)

**Observed**, once: the 5150 came up on the 4 July 2026 fallback date with its
AST SixPakPlus in the machine and working. On the next run, with SPEC.md
§37.92's instrument in the kernel, it answered `tier that answered 2` and
`NS probe stop hex 00FF` — the MM58167 rung, every gate passed — and the
operator's report is *"the clock just worked this time. I have no idea what
happened before. If it's intermittent, I will call it out again."*

**So there is one failing observation with no data and one passing
observation with all of it, and that is not enough to change code on.** What
it is enough for is to say what to look at when it recurs, and the whole
value of the instrument is that the next failure names its own gate.

**The passing card, in full:**

```
tier that answered            2      MM58167 (rung 4 of the ladder)
int 1Ah readable              0      an XT BIOS: AH=02h is not implemented
NS probe stop hex     00FF           every gate passed
NS reg 00 hex         0090           ms counter, high nibble only
NS reg 08 hex         0000           RAM 08h, no low nibble
NS 0D wr AA rd hex    000A           the strict test, and it PASSES
NS 08 wr FF rd hex    00F0
```

Two of those settle questions that were open in the source. `NS 0D wr AA rd
= 0A` is the byte `clk_ns_probe`'s own comment nominates as the first thing
to suspect — three sources say the absent nibble reads `0Ah`, GLaTICK's
author's comment expected it might float to ones — and on this card **it
reads `0Ah`**, so the strict test is right and is not the intermittency.
`NS reg 00 = 90` is the *largest* value gate 2 accepts (`cmp al, 0x90 / ja`),
which looks alarming and is not: the register is a tens-of-milliseconds digit
and 9 is simply its top BCD value.

**The one gate whose result depends on the time of day is gate 4**, the
RP5C01 veto, and that is the shape an intermittent has. `clk_rp_fields` reads
the *low nibble* of each port and refuses the MM58167 if they spell a
plausible RP5C01 page 0 — but on an MM58167 those ports are its own counters,
so what the veto is really testing is the current time:

| port | RP5C01 wants | MM58167 has there | passes when |
|---|---|---|---|
| 0x01 | 0..5 | hundredths of a second | units digit ≤ 5 — **changes 100x/second** |
| 0x03 | 0..5 | minutes | units digit ≤ 5 |
| 0x05 | 0..3 | day of week, 1..7 | ≤ 3 |
| 0x06 | 0..6 | day of month | units digit ≤ 6 |

Multiply those and a genuine SixPakPlus looks like an RP5C01 a few percent of
the time — **and the first row changes a hundred times a second**, so it is
not reproducible boot to boot at the same time of day either. That is exactly
"it worked this time and I do not know what happened before".

**Nothing is being changed on this.** The veto is not silly — it exists so
that two writes cannot land on a TC8521's MODE register and move a stranger's
chip onto its NVRAM page — and one unexplained boot is not evidence enough to
trade that away. What to do instead is read **one number** when it recurs:

- `NS probe stop hex` = **04** → the veto fired, this table is the cause, and
  the fix is to make gate 4 time-invariant rather than to delete it.
- anything else → the veto is exonerated and the named gate is the lead.
- `00` → an earlier rung claimed, which on an XT means something much
  stranger than a clock fault.

**The reusable lesson** is about what to build when a bug will not reproduce:
not a fix, and not a bisect, but the one byte that makes the *next*
occurrence self-diagnosing. This cost eleven bytes of kernel and the failure
has not happened again since — which is the awkward part, and precisely why
the instrument had to go in before it was understood rather than after.

## 15. Fullscreen Tracker plays for 10-20 s and then drops audio, and the scroll micro-hitches (FIXED, reproduced under MartyPC)

**Observed.** In Tracker's XT-mode fullscreen (the SPEC.md §45.13 text
screen): three or four rows scroll smoothly and the next one "hitches" — a
micro-stutter — and, separately and much larger, *"the music plays smoothly
for the first 10-20 s of fullscreen, then has dropouts"*. Windowed is fine.

**Both are one defect, and it is in the KERNEL rather than in Tracker.**
`fsx_wait`'s retrace clock is a **busy-wait on the CRT status port**, and to
a round-robin scheduler a busy-wait is indistinguishable from work — so the
drawing task took its half of the machine whether it needed it or not, and
the half it did not need belonged to the `FSXF_KEEPWORKER` mixer. That is the
exact contradiction of the flag: the promise of `KEEPWORKER` is that the
worker keeps running, and the frame clock was spending its CPU.

**Reproduced and priced under MartyPC** (`os8088_5150_sb`, cycle-accurate
4.77MHz 8088, `BEVERLY.MOD`), by two instruments that cost the guest nothing:
a **sampling profiler** (ask for CS:IP a few thousand times, bin by the
nearest symbol out of a NASM listing) and a **poll of the ring counters out
of guest RAM** with cycle timestamps.

| | windowed | fullscreen, before | fullscreen, after |
|---|---|---|---|
| `mp_mixch_xt` — the mixer | 47.2% | **35.0%** | **51.4%** |
| `fsx_insync` + `fsx_wait` | — | **28.8%** | *absent* |
| bytes/second reaching the card | 5,533 | **4,808** | **5,529** |
| ring lead, halves of 2,048 | 7.0–8.0 | **1.0**–8.0 | **7.0**–8.0 |

4,808 against 5,533 is **13% of the audio never played** — the DSP pausing,
which is what an underrun does by design. The ring's eight halves are ~3
seconds of cushion, which is why it sounds perfect for the first half-minute
and then does not, and why the field machine (slower still) hears it sooner.

**The fix is a third clock**, `FSXW_FRAME` (SPEC.md §53.5.1): it waits on the
new `[sch_subs]` — a free-running counter of IRQ0 entries, sub-ticks included
— and between polls it calls `task_yield`, so the frame time the app does not
spend goes to the worker instead of to a port. It is also *faster* than what
it replaces: with `FSXF_FASTTICK` armed it returns 54.6 times a second,
quicker than a Hercules' 50 Hz retrace and evenly spaced by the crystal.

**And the micro-hitch was TWO bugs, the second only visible once the first
was fixed.** With the clock even, `[tui_fpt]` still read **2** on a machine
measured at three frames a tick: `[tui_fcnt]` is reset *by* the frame standing
on the tick edge, so it ends a tick holding the frames *inside* the tick.
§45.15.2's interpolation therefore divided by two and then hit its own cap —
`0, bpt/2, bpt/2` — so the position **froze for the last third of every tick
and then jumped half a tick**. Measured on the card's own rendered output
(`os88marty.py pace`, BIG-update class = the pattern-grid blit):

| | before | + frame clock | + the `[tui_fpt]` fix |
|---|---|---|---|
| mean row interval | 8.88 fr (148.2 ms) | 8.20 fr (136.9 ms) | 8.27 fr (138.0 ms) |
| **jitter (sd)** | **5.60 fr** | 1.57 fr | **1.31 fr** |
| evenness | **0.63** — judder | 0.19 | **0.16** |

96% of row intervals are now 7, 8 or 9 CRT frames, which is what a 54.6 Hz
display of a 7.14 Hz row stream quantizes to and is therefore the floor.

**Three reusable lessons.**

- **A busy-wait is work.** Any poll loop in a task, on this scheduler, is a
  claim on half the machine. That is free when the task is alone and it is a
  starved worker when it is not — so the pairing is the rule: *a bracket with
  a kept worker must not pace itself on `FSXW_VSYNC`*.
- **The careful part was careful about the wrong quantity.** `ttx_clkprobe`
  measured the retrace clock honestly and at length — timeouts, refusals, a
  zero case — and never asked what polling it *cost*. Measuring the thing you
  chose is not the same as measuring the choice.
- **QEMU could not have found this and MartyPC could.** The defect is a CPU
  budget on a 4.77MHz 8088; on a host-speed emulator every task has all the
  time in the world and the counters read perfect. It needed a cycle-accurate
  machine with a real Sound Blaster in it, which is `make marty`'s whole
  argument arriving as a bug.

## 16. The scroll runs about three rows ahead of the music (FIXED, confirmed on the field machine — all of it was ours)

**Reported** off PCem with a Sound Blaster, playing `CLICK.MOD` (the metronome
module — `make clicktest` — one note every two seconds, 125 ms rows) in
fullscreen XT mode: *"I think its exactly three rows"*, and the offset is
**completely consistent**, so the reporter timed their keypress *ahead* of the
note rather than reacting to it. `TRKLOG.TXT` from that run carries seven
`MARK` records, and the row on screen at each is **3 to 6 rows past the note
that was sounding** (mean 4.9, which includes however far ahead they aimed).

**What the log settles without needing the human at all.** `PLAY-CONS` — the
interpolated display position minus the driver's last block report — must
sweep `0 .. 2048` once per block, because a report names the last block
boundary the card crossed. In that capture it sweeps **696 .. 2,982**: it
rides ~800 bytes high for the whole run and its peak is 630 bytes *past the
physical ceiling*. 800 bytes is **1.2 rows**, and it is ours.

**Why the bench could not find it by waiting.** MartyPC at the same rate
measures the phase at −0.06 rows — near perfect — because its worker never
starves, so its phase is never displaced. §45.15.1's estimator had **no
downward correction at all**: the only thing that ever pulled it back was its
own rate error, at ~10 bytes a block. Displaced 900 bytes from the debugger it
took **55 reports — 20.5 s of music** to come back, which is longer than the
gap between starves on a busy machine, so a real machine accumulates
displacements instead of shedding them. The field log has a ten-second hole in
it, which is what a starve looks like from the inside. SPEC.md §45.15.3 closes
a first-order loop on the report edge: **6 reports, 2.2 s.**

**What is left, and the experiment that settles it.** ~1.2 rows of 3 is
accounted for. The residual is either **PCem's own output buffering** or
something between `[trk_consumed]` and the speaker that this instrument cannot
see — it compares the display against the *driver's DMA counter*, so a clean
reading does not exonerate anything downstream of that counter.

The discriminator is the **sample rate**, and it needs no instruments:

- A **guest-side** offset is a fixed number of BYTES. One DMA block is 2,048
  bytes, which against a fixed 125 ms row is **4.15 rows at 4,000 Hz, 2.98 at
  5,500 and 1.49 at 11,000** — the "exactly three rows" is exactly one block
  at the XT rate, which is why this hypothesis is worth testing first.
- **Host output buffering** is a fixed number of MILLISECONDS and therefore a
  fixed number of rows, whatever the rate.

**`X` is the wrong way to change the rate, and that is worth writing down
because it was the first thing tried.** Leaving XT mode changes the
**surface** as well: fullscreen is then the graphics FT2 screen, which
SPEC.md §45.9.1 measured at 2,567 glyph cells a second and which on a real XT
is unusable. It also destroys the measurement — with the frame clock starved
to one call every 3.4 ticks, `[tui_lcons]` never advances between reports at
all and is simply snapped, so §45.15.2's interpolation degenerates and the
phase being measured is not the phase under test. On MartyPC that alone reads
as −0.79 rows on the graphics screen against −0.21 on the text screen at the
same 11,000 Hz.

So the bench build has **`K`** (`make clicktest`), which moves XT mode's rate
through 4,000 / 5,500 / 11,000 **without leaving XT mode** — same text screen,
same everything, only the block duration. It is windowed-only and takes effect
at the next Play, like `X`; the header cell says which rate is running. The
floor is the hardware's: a Sound Blaster's rate is `1000000/(256-tc)` with `tc`
a byte, so nothing below 3,906 Hz exists to ask for.

Validated on MartyPC — where the estimator is rate-invariant after §45.15.3,
which is exactly what makes the sweep a measurement of the *residual*:

| XT rate | block | display vs the driver's counter |
|---|---|---|
| 4,000 Hz | 4.15 rows | −0.18 rows |
| 5,500 Hz | 2.98 rows | −0.20 rows |
| 11,000 Hz | 1.49 rows | −0.21 rows |

**On the field machine: judge the offset at each of the three.** Tracking
4/3/1.5 → guest-side, one DMA block, and the hunt moves into the driver's
`sbl_consumed`. Flat at ~3 → a fixed time, i.e. PCem's output path — and the
matching control is FM: Missile Command and Arkanoid are timing-tight on the
same machine and read as matched, but they are OPL, not DSP.

### The answer: neither. It was all phase.

The field machine reports the click **spot on**, at every rate `K` offers —
*"I tried pressing K a few times and it stayed spot on"* — and a `TRKLOG.TXT`
from that run puts a number on it. Same machine, same module, same column:

| | mean | median | min | max | |
|---|---|---|---|---|---|
| before | 1,835 | 1,830 | 696 | 2,982 | **101 of 253 samples past the 2,048 ceiling** |
| after | 884 | 906 | −174 | 1,874 | never leaves the band |

**+1.20 rows → −0.21 rows**, and the −0.21 is the same figure MartyPC gives at
all three rates (−0.18 / −0.20 / −0.21). So §45.15.3 accounts for the whole
reported offset: there is no one-DMA-block error in `sbl_consumed`, and PCem's
DSP output is not meaningfully buffered — which is also why FM always read as
matched. Both hypotheses above are retired, and they are left standing because
the sweep that killed them is the useful part.

**Two things worth keeping from how this was got wrong.** The estimate that
"about 1.2 rows of the 3 is ours" came from measuring a *drifting* quantity
once: the phase has no controller, so it is a different number every minute,
and that 24-second capture caught it decaying from something higher (its
sawtooth floor falls 1,274 → 696 across the log — visible in the file, and
read past). **A single capture of an uncontrolled quantity is a sample, not a
size.** And "exactly three rows" being almost exactly one DMA block at the XT
rate (2.99) was a **coincidence**, and a persuasive one — it survived a
capture, a plausible mechanism and a designed experiment before the experiment
killed it.
