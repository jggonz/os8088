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

## 1. Audio tails off for ~1/3 second, every few seconds (Tracker) (FIXED — and it was never a field report)

> **CLOSED on two counts, and the second is the one worth reading.**
>
> **It is fixed.** The owner reports the audio smooth, and smoother again
> after PERFORMANCE.md Set 20 took the mixer's inner loops from 45% of the
> machine to 29.6%. Which change fixed it is not established — this entry
> outlived several rounds of work on the replayer, the ring and the feed —
> so it is closed on the symptom being gone rather than on a diagnosis.
>
> **AND IT WAS NEVER MEASURED ON THE FIELD MACHINE.** The 5150 this project
> is calibrated against **has no sound card at all**
> (docs/FIELD-MACHINES.md), so no report about audio can have come off it.
> This one came from **PCem** — which models period hardware at period
> speed, so its figures are in the right units and do not announce
> themselves. That is exactly the rule FIELD-MACHINES.md's last section
> states: *a number is not a field number because a human handed it to you*,
> **ASK which machine a report came from** — and this entry sat here for
> months saying "Reported on a real 8088 at 4.77 MHz", which was read as the
> 5150 by everyone who came after, including the sessions that spent time on
> it. The rule exists because of cases like this one; it was written down and
> then not applied.
>
> Kept rather than deleted for that reason. The ruled-out list below is still
> sound and still useful if anything like it returns.

**Observed.** A MOD plays normally for a few seconds, then the sound "slows
down" or tails off for about a third of a second, then continues normally.
The cycle repeats. Reported on a real 8088 at 4.77 MHz — **on PCem, not on
the 5150**; see the note above.

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
rather than failing late. **"Do not add a compacting allocator" was this
note's advice and it has been superseded** — SPEC.md §66 is one, and the half
of the reasoning that stands is the half about REGIONS: a region's base is its
CS and still can never move. What that ruled out was a compactor that slides
everything; what was actually needed was one that slides DATA claims whose
holders can be told, which is a smaller and safer thing. The note's own
analysis is what made the case: the two blocks that stranded a 116KB reload
were a driver's staging pool and a package's ring, so a compactor confined to
the kernel's own claims would have moved neither.

**A related honesty bug worth fixing at the same time:** the splash says only
`Out of memory`. It should say which figure failed and how short it was —
§31.3's three-layer refusal pattern (§47: say *why* not).

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
at once**.

> **The interleave sentence is wrong and PERFORMANCE.md Set 37 is the
> correction.** The media is 1:1; the second turn is the IBM ROM's own
> head-settle wait, 25 ms asked for and 52.5 ms of `LOOP $` delivered, once
> per `int 13h`. The 11,520 agreement cannot discriminate, being bytes over
> the whole call. Everything else in this note stands, including the
> conclusion it exists for. `int 13h track, 1 call` *is* our batched read done right, and it
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
is what this drive does.** (The third figure agrees by coincidence — see the
correction above — and the target is the two measured ones.)

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

**What the pair can vary by.** Nothing in `gfx_unlock` spends the deferred
hide before `cur_lazyend`, so it takes `.never` → `cur_lazyend` every
iteration. (This was measured while SPEC.md §32's `gfx_flush` still stood at
the top of `gfx_unlock`; it returned without spending the hide on a mono
adapter, so the path measured is the one that runs today.) That path is a
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

## 17. Four reports off the first `combo.img` field run (OPEN, three queued, one reproduced)

The 5150 run that confirmed SPEC.md §18.97's probe on real hardware — `ST3
0021` twice, `probe stop 03`, `verdict 0`, and drive B gone — also brought
back four things about the formatter. They are queued rather than fixed, and
one of them is already root-caused.

**The probe's other half is NOT one of them.** The report reads
`drives int 11h claims 2`, and §18.98's loop over units 2 and 3 starts at
`cl = 2` against that count, so it correctly never runs: SW1 says two drives,
which is units 0 and 1, and there is no third to ask about. The removal of
unit 1 then sets the count to 1. **To exercise units 2 and 3 the switches have
to claim three or four drives** — which is also the only way the external pair
can appear at all (§18.98). Nothing is wrong here; it is worth writing down
because "the probe for drive 2 did not run" and "the probe for drive 2 is
broken" look identical in the report.

**One gap it did expose, now FIXED**: `fdd_dbg_*` (§57.5) was a single set of
bytes while §18.98 probes up to three units, so the block described the LAST
one asked — on a machine claiming four drives the operator saw unit 3 and had
no visibility into units 1 and 2. That is a diagnostic going blank on exactly
the configuration it exists for, an internal drive plus a 4865 on the 37-pin
connector. It is **one five-byte row per unit** now, with `probe ran` a bitmap
(bit n = unit n was asked) and a unit printed only if its bit is set — so a
correctly-switched two-drive machine still reads five short rows and not
twenty. Ten bytes of `.text`, sixteen of `.ovl`, no rung crossed. Verified on
`os8088_5150_cga_ext720`: `claimed 4`, `probe ran 0E`, three rows whose `ST3`
reads `79`, `7A`, `7B` — the low two bits being the unit, which is what says
each row is about the drive it names.

**And `sysbench`'s own reader had two equs on one word**, found while
rewriting it: `sb_fdstate` and `sb_skcyl` were both `os88_image_end + 240`.
It worked only because `sb_disk` runs before `sb_fdd` and `sb_fdd` reads that
word in the same breath it writes it — neither of which anything enforces,
and the comment above `sb_fdstate` is *about* the last time this happened.
Moved past every scalar, with `SB_O_SYSKB` raised to match.

### 17.1 The format prompt does not clear on Escape (FIXED, verified on Hercules and CGA)

Two of the four reports — "the size/format prompt doesn't clear after
formatting" and "there is some corruption over the size prompt" — are **one
bug**, and it is a regression from §26.4.

`Format Disk…` armed, then **Escape**: line two is correctly replaced by the
resting `Size ? Free ?`, and line one — `Format B: as 360K?` — **stays on
screen**, sitting over the row above the status line. That is what reads as
corruption; there is nothing corrupt about it, it is a line nobody erased.

The cause is that §22.12's prompt became **two lines** at §26.4 (one line is
44 characters and `fm_stat_line` truncates at 38, losing `Esc=no`), and the
cancel path did not follow: `.cancel` calls `fm_edit_end` and falls into
`.lineonly`, which is CF = 0 — "the status line is all that moved" — so
`fm_status_only` repaints **one** line. It was right for every other mode,
because modes 1, 2 and 3 are one-line prompts, and mode 4's two-line replace
question never reaches `.cancel` at all (it is handled at `.replace`).

**Reproduced on Hercules and on CGA**, drive B, with the shipped kernel: arm
the prompt and press Escape. It does NOT show on the Enter path, because the
format's own `fmv_load` repaints the whole window — which is why the harness
missed it: every test here pressed Enter.

**Fixed**: mode 5's cancel takes the full-repaint exit (`.out`, CF = 1)
instead of `.lineonly`. Verified by re-running the repro on both adapters —
the prompt clears whole.

### 17.2 Format Disk stays greyed on a disk it just made (FIXED)

Reported as "after formatting a disk, Format Disk becomes greyed out for the
newly formatted disk forever". That was §22.12's predicate working as written:
the item was live only while `FS_MOK == 0`, and a disk that formatted now
mounts. The comment there said why — *"it MOUNTED: there is nothing here to
format, and 'erase this perfectly good disk' is a different feature with a
different question"*.

**The plainest form of it is that os8088 could format any disk except one of
its own**, which is what settled the design question: the second feature
should exist, and declining to ask a question is not the same as protecting
anybody from its answer. The `FS_MOK` test is gone from `fm_fmt_ok`, and the
different question is *asked* — a mountable volume gets
`ERASE and format A: as 360K?` where an unreadable one gets
`Format A: as 360K?`. Enter still means yes and every other key no, which is
already the strongest confirmation in the system (§22.12).

Three pieces of fallout, all of which existed only because the greying had
made them unreachable:

- **`dskw_fmt_probe` forced 9/2 on the way out.** Its own comment justified
  that with *"this routine is only ever called with a failed mount behind
  it"*, which is exactly the premise that was just removed — so a **cancelled**
  confirmation on a mounted 1.2MB or 1.44MB volume would have left the machine
  believing that volume had 9 sectors a track. It banks the caller's
  `[disk_spt]`/`[disk_heads]`/`[dsk_totsec]` and puts those back instead.
  Like the routine's own note about why it restores anything at all, this
  **closes a trap rather than fixing an observed bug**: on a 9-sector volume
  the constant was accidentally right, and every drive in this harness — and
  on the field machine — is 9-sector, so the case cannot be exhibited here.
  What is gated is the round trip: the three words read identically before
  the confirmation, while it is up, and after Escape cancels it.
- **A sibling Disk window could be showing a folder on the disk being
  replaced.** It could not before, because a window on an unmountable volume
  is always at the root (§19.2). `fm_fmt_home` sends every other window on
  that drive back to the root, with a caption to match and §22.8's deferred
  re-list rather than three mounts paid at once.
- **§18.96.2's fallback leaned on the greying** — a failed 720K reach test
  re-formats as 360K rather than stopping, and the stated reason was that
  stopping would leave a mountable, lying 720KB volume with Format Disk…
  disabled. The behaviour is kept and the reason is restated: the user asked
  for a formatted disk, and the useful answer is the one the drive *can* make
  plus a toast saying why. §18.96.2 still runs, because a disk that silently
  loses whatever is written past cylinder 39 is not a thing to ship on the
  grounds that it can be reformatted afterwards.

Verified on a cycle-accurate 5150/CGA with B: a copy of the shipped apps disk
— four folders on a perfectly good FAT12 volume, which is exactly the disk the
old predicate refused: the confirmation reads `ERASE and format B: as 360K?`,
Enter makes an empty 360KB volume that the HOST's own fsck accepts
(`os88disk.py --verify`: 354 clusters, 0 files), and with two windows open —
one left inside `B:\APPS` — the sibling's `FS_CWD` goes 2 → 0 with the
re-list debt set, its caption comes back as `Disk`, and raising it lists the
new root. The 720K/360K gate (§18.96.2) still passes both legs.

Cost: `.text` +24, `.cold` +152, **no rung crossed** and footprint unchanged
— but `kern_big`'s cold rung has 107 bytes left in it afterwards, so the next
cold addition buys a whole 512. `kern_small` is untouched: the formatter is
`kern_big` only (§18.96).

### 17.3 720K on the field machine came back 360K (NOT A BUG — the reach test)

Reported as "switching to 720k formatted the disk in drive A; result was a
360k formatted disk, claiming 354k free". That is §18.96.2 working: the Tandon
TM100-2 is a 40-cylinder drive, the marker written to the volume's last sector
did not come back, and the fallback re-made the disk as 360KB. **354K free on
a 360KB volume is correct** — 354 clusters of 1KB after the boot sector, two
FATs and the root directory.

What the operator should have seen is the toast saying so —
`Drive cannot reach 720K - made 360K` — and §17.1 is a good reason they may
not have: with a stale prompt line on screen the window was not saying what it
looked like it was saying.

### 17.4 Do not offer the size toggle on units 0 and 1 (DONE)

Asked for directly: the internal drives are the machine's own and their
geometry is not in question, so the `Spc=size` key should only appear for
units **2 and 3** — the external pair, which is exactly where an 80-cylinder
3.5" drive can turn up and where the BIOS can say nothing about it (§18.98).

It is a narrowing of §18.96.2 rather than a removal: the toggle exists because
`AH=08h` is refused and the drive cannot be asked, and that ignorance is only
*actionable* on a drive the operator may have changed. The row is already in
`dsk_vtab` with its `DV_UNIT`, so the test is available where the prompt is
composed.

**Done**, as `fm_fmt_sizeable` — one predicate serving both the line and the
key. Verified with a scratch disk on unit 2 (`--mount fd:2:`): on B: the
second line reads `Enter=yes  Esc=no` and Space leaves the row at 3, on C: it
reads `Spc=size  Enter=yes  Esc=no` and Space moves it 3 → 2.

**And a third thing came out of setting the switches up for it**, which is in
§18.98 rather than here because it is not what the field reported: §18.97's
removal set the drive COUNT to 1 as well as freeing row 1, which truncates
§18.98's loop — so a machine claiming three or four drives with no unit 1
would never have asked about the external pair. That is the field machine with
a 4865 plugged in, i.e. the exact configuration this round exists to serve, and
it would have cost the next field run for nothing.

## 18. The switches were flipped and no external drive appeared (NOT A BUG — the count is the highest UNIT plus one)

Reported off the second `combo.img` run: the operator set SW1 for the external
4865 and no third drive showed up. The report is unambiguous about where the
fault is not.

```
drives int 11h claims         2
probe ran bitmap hex  0002
--- unit 1   ST3 0021 / 0021   ST0 0071   probe stop 03 (ABSENT)   verdict 0
```

**The kernel is exonerated by the first row.** `desk_init` is
`((AX >> 6) & 3) + 1` straight off `int 11h` with no clamp — the clamp to 2 is
what §18.98 removed — so `claims 2` *is* the BIOS equipment word's bits 7:6.
And §18.97's probe is demonstrably working in the same block: unit 1 answers
`ST3 = 21` twice, `ST0 = 71`, stop 03, verdict 0, which is that section's exact
documented signature, and drive B is correctly gone.

**Nor were the switches set backwards**, and that is worth stating because it
is the first thing anybody checks. Bits 7:6 carry exactly one encoding per
count, so moving *either* switch necessarily changes the number: backwards
would have read 1 or 4. Reading the same 2 as the previous run means those two
bits did not move at all — a mechanical question (SW1 versus SW2, counting from
the wrong end of an 8-way block, or a 40-year-old rocker that travels without
making contact), not a polarity one.

**And then the operator supplied the fact that settles it**: the switches
*were* moved, and the previous run — the one that reported `claims 2` and had
drive B taken away by the probe — was taken with the DIPs set to **one drive**.
So two different physical settings both produced bits 7:6 = `01`. Those bits
are not tracking the switches at all, which is a stuck line or a wiring
question rather than anything about the encoding, and it retires the "did you
cold boot" and "which end of the block" questions together.

It also explains something that had been read as os8088 being wrong: **drive B
appeared on this machine before §18.97's probe existed**, on a machine with one
floppy, because the equipment word claims two whatever the switches say. The
probe removing it is not a workaround for a mis-set switch — it is the only
thing on that machine that can answer the question at all. And it is why DOS
never needed the switches either: `DRIVER.SYS` assigns a logical drive to a
physical unit without consulting the equipment word, which is what "it just
claimed it with the command line command" was.

**And the likely misunderstanding is arithmetic, not electrical.** J1's first
external drive is physical **#2**, so one internal drive plus one external is
**three** claimed drives — units 0, 1 and 2, with nothing at unit 1 — not two.
At a claim of two the word says "units 0 and 1" and §18.98's loop over the
external pair correctly never runs. SPEC.md §18.98 now says so where the count
is described.

**What changed as a result**: `sysbench` prints the **raw equipment word** in
hex beside the derived count, because the count alone cannot say *which* switch
moved and a run that reads the expected number for an unexpected reason is the
one that costs a second trip. Bits 7-6 drives-1, 5-4 display, 3-2 planar RAM,
1 the 8087, 0 "there is a diskette drive at all" — so one hex word is very
nearly SW1 itself, and flipping SW1-1 or SW1-2 by miscounting the block is
visible rather than silent. It is read from the BIOS rather than from the
kernel's banked byte, so the two disagreeing would itself be news. Verified on
`os8088_5150_cga_ext720`: `equip word hex 04EF` beside `claims 4`, which is
§18.98's own measured figure for a four-drive machine.

**And SW1 itself**, read off the 8255's port A rather than out of the POST
snapshot, because on this machine the interesting question is no longer "what
did the BIOS latch" but "did the switches ever reach the chip". Equal to the
equipment word's low byte ⇒ the BIOS is faithful and the fault is in the
switches or their wiring; different ⇒ something rewrote `0040:0010` after
POST, which a machine with an ST11M and a SixPakPlus in it can do and which
`int 11h` can never reveal. IBM PC only, gated on the model byte at
`F000:FFFE`: port B bit 7 moves port A onto the switch block on a 5150, and on
a 5160 the same bit clears the keyboard while SW1 sits on port C. Port B is
banked and restored byte for byte inside an `IF = 0` window. Verified on
`os8088_5150_cga_ext720`, where the switches *do* reach the chip:
`equip word hex 04EF` and `SW1 direct hex 00EF`, the same byte.

**Paying for it emptied the package's last slack.** `sysbench` is at its
ceiling — `image + bss` must fit `APP_MAX_SIZE`, which is the 60KB *segment*
and unraisable, so with `bss` at 38,452 the image may not cross 22,528. Both
obvious sources of relief are refused by their own comments: `BL_MAXROWS` and
`BL_ARENA` each record a report that TRUNCATED in the field. The two rows were
bought by merging two pairs of header lines instead, and the file now says so
where the `align` is. **9 bytes left**, after §57.6's build row took the rest.

## 19. The 765 cannot see the external drive that DOS reads fine (OPEN — routed around)

The field 5150's IBM 4865 on the 5.25" adapter's 37-pin connector is powered,
cabled and **works**: with a disk in it, os8088 mounts it, the file manager
lists it, and `sysbench` reads a file off it. Every one of those goes through
`int 13h` with `DL = 2`.

`dsk_fdd_probe`, which drives the FDC directly, cannot see it at all:

```
  --- unit 2
  ST3 motor off hex     0022      bit 4 (TRK0) CLEAR
  ST3 after seek hex    0022      ...and still clear after a RECALIBRATE
  ST0 drained hex       0072      IC 01, SE, EC - the 765's Equipment Check
  probe stop hex        0003      ABSENT
```

The unit-select bits in both answers (`ST3` low two bits = 10, `ST0` low two
= 10) say the commands really did address unit 2, and `ST0`'s EC is the
controller reporting that it issued the step pulses and never saw track 0.
So the command reached the chip and the chip found nothing on the far end.

**Media has been ruled out**, which is what makes this interesting rather than
merely inconvenient. §18.97's whole premise is that TRK0 is a position sensor
that answers with or without a disk; the obvious explanation was that this
drive gates it on media. It does not — the capture above was taken **with a
formatted disk in the drive, in the same boot that mounted and read it**.

Ruled out so far: the drive (DOS and os8088 both use it), power (it has its
own — J1 supplies none), the cable and the select jumper (`int 13h DL=2`
reaches it), and media.

Still open: why the ROM's own select sequence reaches unit 2 and ours does
not. The next step is to disassemble the 27 Oct 82 ROM's motor-on/select/seek
and diff it against `dsk_fdd_probe`'s — the ROM image is the one thing about
this machine that is already in hand. Candidates worth carrying into that
read: whether the IBM adapter decodes DOR bits 4–7 as motor enables for units
2 and 3 the way a stock FDC does, and whether the drive's status outputs need
something we are not asserting before it will drive TRK0.

**It is routed around rather than fixed** (SPEC.md §18.98.1): units 2 and 3
trust the equipment word, so the drive appears and works. The probe still runs
for the published state above, which is the only reason any of this could be
diagnosed at all. Unit 1 is unaffected and still contested.

## 20. A window dragged from BEHIND another lands at the covered rect's corner (FIXED, SPEC.md 11.96.10.1)

**Found by `tests/dispsave.py` failing after the `elendilon` merge**, and it is
not what that test is about — the raise cache it gates works. The failure is
that the window the test drops over another lands somewhere else, so its raise
click hits the covering window instead of the covered one and ~4,300 pixels are
legitimately still covered.

**Narrowed to one number.** Reading `ui_drag`'s own words out of the guest
straight after the drop:

```
the harness grabbed at          (1020,129)   = the title bar's centre
ui_startx / ui_starty            990, 175    <-- 30 LEFT and 46 DOWN of that
ui_origx  / ui_origy             860, 120    = the window, correctly
mouse_x   / mouse_y              890,  69    = the pointer, where asked
ui_curx = orig + (mouse - start)  760,  14   ...clamped up to y = 20
the window lands at              760,  20
```

So the arithmetic is right and **the mousedown point it was handed is wrong**.
`ui_drag` takes it from the EVT_MDOWN's `EV_X`/`EV_Y`, which `mou_isr` fills
from `[mouse_x]` when it posts — so at the moment the press was decoded the
pointer was still at (990,175), one convergence step short of where
`os88mouse.to()` had already PROVED it to be by reading that same word.

**`ui_drag` itself is not the bug.** Two isolated drags on the second display,
one down-right and one up-left, land pixel-exact:

```
grab (840,29) -> (710,89)   window (680,20) -> (550,80)   exact
grab (710,89) -> (580,29)   window (550,80) -> (420,20)   exact
```

**What has been ruled out:**

- **The clamps.** `ui_curx`/`ui_cury` already hold the wrong answer before
  them; the y clamp only turns 14 into 20.
- **A release race.** `ui_drag` samples `[mouse_x]` once per tick (§7.1.3's
  linger) and drops the window at the last position it SAMPLED, so a release
  arriving between samples would land it behind the pointer — but holding the
  release for two guest ticks after the pointer arrives changes nothing, and
  the arithmetic above says the error is in `start`, not in `cur`.
- **Single-display drags.** `tools/subcheck.py` drags on one card across 11
  steps and matches its reference at **0 differing pixels**, positions
  included.
- **The raise cache.** `wm_su_segs[slot]` is non-zero after the cover on both
  trees; §39.14.8.1's fix is orthogonal to this.

**It is timing, which is why it looks like a branch difference.** The same
scripted session lands the window correctly on `origin/elendilon` and on this
tree built `REDRAWFULL=1`, and wrongly on the shipped build — the redraw round
changed how long the guest spends between packets, not what it does with them.
So this is a race that a faster or slower kernel moves either side of, and
**the bug is that a press can be decoded before the move that precedes it has
been applied**, on a serial line where they cannot overtake.

**`mou_isr` is not the bug either, and reading it is what leaves one
hypothesis standing.** The decoder applies the packet's delta to
`mouse_x`/`mouse_y` and *then* fills the event from those same words, so a
press can only ever be posted at the position after itself. And the error is
exactly one convergence step: (990,175) − (1020,129) = **(−30, +46)**, the
negation of the correction `os88mouse.to()` sent last.

**So the press `ui_drag` acted on is not the press the harness sent — it is an
OLDER one, still queued.** `ui_dispatch` pops events in order, and an EVT_MDOWN
that nothing dispatched at the time it was posted keeps its own coordinates for
as long as it sits there. Whether one lingers depends on how busy the UI task
was, which is precisely what a redraw optimisation changes — and that is why
the same scripted session lands correctly on `origin/elendilon` and on this
tree built `REDRAWFULL=1`, and wrongly on the shipped build.

**The queue was empty, and that killed the last hypothesis.** Read inside
`dispsave`'s own session, immediately before the press: `count = 0`, and after
it `head`/`tail` advanced by exactly one record. So the event `ui_drag` acted
on **was** the press just sent, the pointer was proved at (1020,129) before and
after it, and `mou_isr` filled the record from `[mouse_x]` — which said
(1020,129). The event could not have carried (990,175). Something between the
push and `ui_drag` was changing `CX`/`DX`.

### The answer: `wm_raise` ate two registers that were not its to eat

`ui_dispatch` holds the mousedown point in `CX`/`DX` and hands it to `ui_drag`.
Between the two it calls **`wm_front`** — but only when the clicked window is
not already frontmost. `wm_front` saves `AX`/`BX`/`BP`/`DI`, which is exactly
what `wm_raise`'s contract says it clobbers, and §11.96.10's arming site loads
a rect into `AX`/`BX`/`CX`/`DX` for `wm_su_sub`.

**(990,175) is `wm_cov_x2`/`wm_cov_y2`** — the bottom-right corner of the box
the covering window was sitting on. Two `push`es fix it; SPEC.md §11.96.10.1 is
the write-up, and `tests/dispsave.py` passes.

**Why it hid for a whole round of work.** It needs a window that is NOT already
frontmost, so every scripted drag that had one window, or grabbed the front
one, landed pixel-exact — `tools/subcheck.py` drags across 11 steps and never
touched it. And on a 16px cascade the wrong answer and the right one are a few
pixels apart, which reads as drag imprecision; the two-display arrangement
pulled them 130 px apart and made it obvious. The gate that caught it was
measuring the raise cache, which was working. If the
queue is empty, the press really is being posted at a superseded position and
the search moves back to the ISR; if it is not, the question becomes *why* a
press outlives its dispatch — `ui_drag`'s own `.track` drains the queue looking
for the MUP and discards everything else, which is the first place to suspect
of leaving one behind.

## 21. No Drive B on the Packard Bell 286, and a 1.2MB drive sitting right there (FIXED, SPEC.md 18.97.2 — CONFIRMED on the machine)

**Reported:** the Packard Bell Victory 286 (docs/FIELD-MACHINES.md) comes up
with **no Drive B on the desktop**. The machine has a 1.44MB drive A and a
**1.2MB 5.25" drive B**, and the report came with the right second guess
attached — *or alternatively we don't support 1.2MB 5.25 drives*.

**It is not the media, and that half was settled by reading.** §18.2's BPB
rule 11 whitelists **15** sectors per track explicitly, rule 12 takes 2 heads,
and rule 13's `spt*heads*80` is 2,400 sectors — which is a 1.2MB disk exactly.
A 1.2MB FAT12 volume mounts. It could never have been the symptom either: a
media problem shows as a drive icon that fails to open, and what was missing
was **the icon**.

**It is §18.97's probe, and the tree already held the demonstration.** The one
path that removes a drive fires when ST3's TRK0 reads clear *before and after*
a RECALIBRATE. Note 19 above is a **present, powered, DOS-readable** IBM 4865
answering exactly that — `ST3 = 22` twice, `ST0 = 72`, `probe stop 03` — with
media in it, mounting and being read through `int 13h` at the same moment.
So "TRK0 never came up" already had a known second meaning, and §18.98.1
routed the external pair around it while **unit 1 was left standing on it**.

**The fix is about the CLAIM, not the drive** (SPEC.md §18.97.2). §18.97
contests unit 1 because on a 5150 the equipment word is the **SW1 DIP
switches** — two drives is the factory position and is wrong on most 5150s, so
it is a default worth disproving. An AT-class machine takes the identical
count out of **CMOS setup**, which is somebody's decision, and §18.98.1 had
already ruled that a deliberate assertion is trusted. The rule was right and
was applied to the wrong axis: it is not *how many* drives are claimed that
decides whether a claim is evidence, it is **where the number came from**. So
the probe still runs everywhere and still publishes what it found, and the row
is retired **on tier 0 only** (`[cpu_tier]`, §41.1).

**Verified, and the A/B is one binary on two CPUs.** No emulator here can
produce a real absent verdict — MartyPC synthesizes `ST3 = 0x79` (TRK0 **set**)
for a drive its own config does not have, and QEMU's FDC returns
`0x28 | (track==0 ? 0x10 : 0)` off a `track` that is 0 for an absent drive, so
both answer *present* unconditionally (measured: QEMU reads `ST3 = 0x39`,
`probe stop 01`). `make FDDABSENT=1` forces the verdict without touching a
port, which makes the DECISION testable while leaving the FDC conversation the
5150's question:

| | MartyPC 5150 (8088) | QEMU (386) |
|---|---|---|
| `cpu_tier` | 00 | 02 |
| claimed / ran | 02 / 02 | 02 / 02 |
| ST3 / ST3B / ST0 | 21 / 21 / 71 | 21 / 21 / 71 |
| `probe stop` | 03 (absent) | 03 (absent) |
| `verdict` | **0 — retired** | **1 — kept** |
| `dsk_vtab` row 1 | KIND FF, flags 0 | KIND 00, flags 1 |
| desktop | `A:` alone | `A:` **and** `B:` |

The shipped (no-knob) kernel is **0 differing framebuffer bytes of 384,000**
against the build before the change, on a machine where the probe finds a
drive — the new branch is behind a `jnz` no emulator reaches.

**What is still open is the Packard Bell's ST3 itself**, and this fixes the
kernel's response to it rather than explaining it. It is note 19's question on
a second machine and a second controller. One candidate is the µPD765's
**77-step RECALIBRATE limit** against an **80-cylinder** drive — which both
the 1.2MB 5.25" and the 4865 are, and which is why real drivers issue the
command twice — but it does not survive on its own: a head parked past
cylinder 77 is walked to within 3 of track 0 by the failed attempt, so the
*next* boot would succeed and the fault would not repeat. **Ask for a
`sysbench` run** (`make combo`): §57.5's `FD` block reports `claimed`,
`probe ran`, both ST3 reads, the drained ST0, `probe stop` and `verdict` per
unit, and it separates the three live possibilities in one line —
`claimed 1` (the drive count never reached us at all, which on a machine with
a potted DS1287 whose battery is 30 years old is worth ruling out first),
`claimed 2` + `probe stop 03` (this bug, now fixed), or `probe stop 04` (the
controller refused, which keeps the drive and was never the symptom).

**Confirmed on the machine.** A 1.44MB `combo` disk was built for it — the
Packard Bell has no 360KB drive, which is the first time the register's
default ask has not fitted a machine in it — and **drive B is back on the
desktop**. That is the reported symptom, gone, on the hardware that reported
it.

**What that confirms and what it does not.** It confirms the diagnosis was in
the right routine and that §18.97.2's tier test is what the machine needed:
the equipment word DID claim two drives (so the `claimed 1` branch — a dead
DS1287 — is ruled out), and the probe DID reach its removing path, because
nothing else on that machine had changed. It does **not** explain the FDC:
no `sysbench` report has come back yet, so the ST3/ST0 bytes this machine
actually answers are still unread, and note 19's question — why a present
drive reports TRK0 clear through a recalibrate, on two different controllers
now — stays open. §57.5's block is on the disk that is over there; the run is
still worth asking for, and it is now the only thing that could ever explain
this rather than route around it.

**The `sysbench` came back, and it diagnoses the controller** (SPEC.md
§18.97.3). Unit 1 on the Packard Bell, whose drive is present and works:

```
drives int 11h claims    2      ST3 motor off hex    0021
probe ran bitmap hex  0002      ST3 after seek hex   0021
probe stop hex        0003      ST0 drained hex      0021
```

`claimed 2` closes the last alternative — the count did reach us, so the
DS1287 is fine and the probe really did reach its removing path. And set
against §18.97.1's 5150, whose drive B genuinely is not there, the two bytes
say opposite things:

| | ST3, twice | ST0 | truth |
|---|---|---|---|
| Packard Bell, unit 1 | **`21`** | `21` — IC 00, SE, **EC clear** | present |
| 5150, unit 1 | **`21`** | `71` — IC 01, SE, **EC set** | absent |

**ST3 is the same byte for a present drive and an absent one.** Not similar —
identical, on both reads, on two machines with opposite ground truth. That
retires the last hope that §18.97's discriminator could be made to work by
being read more carefully.

**ST0 separates them outright**, and it says something specific: interrupt
code **00, normal termination**, seek end set, no Equipment Check is the
controller reporting *the recalibrate completed and the head reached track
0*. So the drive is there, the head is where the FDC says it is, and only
TRK0's path to ST3 bit 4 is missing. That is a fact about a **controller**,
not about a drive — which is the shape note 19 has been missing, and the
first real progress on it.

**So ST0 is now consulted before anything is removed, and only ever to change
the answer to *keep*** (`FDD_S_SEEKST0`, step 05). EC clear proves presence;
EC **set proves nothing**, because the 4865 above is present and sets it.
That asymmetry is the whole design and it is why this cannot make the probe
remove a drive it would not already have removed.

**It is a second guard, not a replacement for §18.97.2**, and they cover
different machines: the tier test declines a 5150-shaped correction on a
machine whose count came from CMOS, and this one saves a **tier 0** machine
with a drive of this kind — which the tier test by construction cannot. The
Packard Bell needed the first. The next XT with a 1.2MB drive needs this.
Measured, all four cells (`make FDDABSENT=1` / `=2`, MartyPC 5150 and QEMU):
the only one that removes a drive is an absent drive on tier 0.

**Two things in that report are NOT faults and should not be chased.**
`mouse found 0` with both ports present and no identify bytes is what a
machine with nothing plugged into either port says — ask before diagnosing
it (docs/FIELD-MACHINES.md's rule, learned on the 5150). And `est CPU MHz
x100` reading **8879** on a 16MHz 286 is the known 8088-only derived row,
still outstanding in the register: it is computed against instruction timings
a 286 does not have, and it should say so rather than print a number.

---

## 22. Tracker "hardlocks" at the end of a large module (CLOSED — NOT A DEFECT, the module says stop)

> **Reported**: a 297KB module (`banana split`, by Dizzy / CNCD '93) loaded
> into Tracker in **XT mode, fullscreen**, played to `Pos 30/30` and then
> "hardlocked the system".
>
> **It is not a lock.** The module ends with an explicit `F00` and the FT2
> text screen then legitimately has nothing left to draw. Reproduced on a
> cycle-accurate 4.77MHz 8088 with 640KB, CGA and a Sound Blaster DSP 2.01:
> the song runs 00 → 30, `mp_playing` goes 0, the grid stops moving, the
> BIOS tick counter keeps advancing, `CS:IP` keeps wandering through the
> kernel's idle, and **Esc exits the bracket back to the desktop**. The
> reporter's Esc did nothing because the emulator did not have keyboard
> focus. Nothing in os8088 is at fault and nothing was changed.

**Three things made it read as a crash, and they are worth knowing because
the next large module will do the same.**

**The module really does stop.** Pattern 5 is order 48, the last one, and row
0x39 channel 0 carries `F00` — ProTracker's "speed 0 = stop" — immediately
after a `C00` fade to silence on all four channels. `mp_readrow`'s `.fF` arm
sets `mp_playing = 0` and does nothing else. To an ear it ends abruptly and
does not *sound* finished, which is a property of the music rather than of
the player.

**Almost every other module loops, and that is the DEFAULT rather than a
command.** Running off the end of the order list wraps to the restart byte
(header offset 951, forced to 0 when it is >= songlen), so a module with no
end-of-song effect at all loops for ever — `beverly.mod` is that case, with
zero `Bxx`, zero `Dxx` and zero `F00`. A module that wants to loop
*somewhere else* says so with `Bxx`: `elysium.mod` ends order 28 with `B09`
and jumps back to order 9, which is the classic play-the-intro-once shape
(verified live: `pos=1C` → `pos=09`). So there are three outcomes and all
three are the file's choice — wrap (default), `Bxx` (chosen target), `F00`
(stop). `banana split` is the only one of the three that stops.

**A finished song in the fullscreen bracket looks exactly like a dead
machine.** Inside SPEC.md §53's bracket the kernel does not run, so there is
no clock, no cursor and no chrome to reassure anybody; the app draws
change-driven (§45.13), so once the song stops the screen is *correctly*
static; and the only keys that do anything are ENTER, F and Esc. The status
line does say `Stopped  ENTER play  F/ESC exits`, in one small row above a
frozen grid. **That is the whole diagnostic surface.** If this is ever
reported again, the first question is whether Esc exits, and the second is
whether the module's last pattern has an `F00`.

**Which screen the report came from was the fastest clue and cost nothing.**
`Pos 30/30` cannot come from the windowed splash: `tui_wpos` prints
`mp_songlen` and would have said `30/31`. Only `trktxt.inc` prints
`songlen - 1` ("FT2's own reading"), so the `/30` pinned the report to the
XT-mode fullscreen text screen before anything was run. **A readout that is
formatted differently in two places is a locator** — worth remembering the
next time a field report quotes a number back.

### 22.1 `BPM 125` on every module is correct, and it was verified against a control

Asked in the same round: every module reads `BPM 125`, which looks like a
stuck field. It is not. A MOD header carries no tempo at all — 125 BPM and
speed 6 are ProTracker's defaults, and the only thing that moves the tempo is
`Fxx` with `xx >= 0x20`. Scanned across the patterns each song actually
plays: `beverly.mod` **0** tempo commands, `banana split` **0** (54 *speed*
commands, `F02`–`F07`, which is what makes it feel fast), `elysium.mod`
**0**. All three are honestly 125 for their whole length.

**The readout was proved live rather than argued to be**, because "all three
agree" is equally what a hardcoded constant looks like. `TEMPO.MOD` — one
pattern, `F96` at row 0 and `F3C` at row 32 — reads **BPM 150** for rows
0..31 and **BPM 60** for 32..63, tracking the effect exactly. A negative
result across three files is not evidence about a field until one file
disagrees with it.

---

## 23. A black dash on the desktop after mounting a hard drive (FIXED, SPEC.md 51.2.4)

Reported off a PCem 286/VGA machine with a screenshot: one 16-pixel black run
on a single scan line of the bare desktop, well below the Control Panel
window, appearing when a hard drive was mounted or unmounted. Two things in
the report were worth more than the picture. **"On at least vga"** — the
reporter had only seen it there. And, in the second message, **"I've been
seeing this on and off; after a reboot mount/unmount is not recreating it,
but during the boot when it happened each mount/unmount would redraw it."**

That pair is the whole diagnosis in advance: *deterministic within a boot,
different between boots* is memory nobody initialised, and *one adapter only*
is an address that lands somewhere harmless on the other two.

**QEMU could not show it, and the reason is worth keeping.** `make test`
reproduces nothing here because QEMU hands the guest **zeroed RAM**, and the
value being written was 0. `.bss` is not the variable either — `-f bin`
zeroes nothing, but `.cold`'s `start=COLD_START` makes nasm pad the file with
zeros across the whole `.bss` range and the boot sector's single read lands
that padding on it, so kernel scratch arrives zeroed by accident on every
machine. **The heap does not**, and that is what differs from boot to boot on
iron. `make DIRTYRAM=1` was written for this: it fills the claim heap with
0xAA before anything can claim from it, and with that one knob the defect
reproduced under QEMU on the first try, in the same shape, at a different
position.

**Finding it from there took one watchpoint.** `qmp.py … 'gdbserver tcp::1234'`
on the running machine, `gdb` with a hardware watchpoint on the framebuffer
byte the artifact sits in (`0xA0000 + row*80 + x/8`), then the click. It
stopped on `pop word [snd_inst]` in `drv_call` with `CS = COLD_SEG` and
**`DS = 0x9B00`, a heap segment** — the driver's own — so the restore was
writing 0xD5F9 bytes past the driver's base, which on a 640KB machine is past
the top of the heap and inside VGA memory. Two bytes, one scan line, sixteen
pixels. On Hercules and CGA the same address is the unused hole below B000,
which is the whole of "at least vga".

The fix and both defects are SPEC.md §51.2.4. What it says generally:
**anything banked in kernel memory across a call that changes `DS` is pushed
before `DS` and popped after it is back** — `loader.inc` had it right, the two
driver dispatchers did not, and `wm_pkgcall`'s `SNAPAUDIT` bracket had the
same shape.

---

## 24. The VGA's colours corrupt after a few minutes (FIXED — oxidised sockets on the card; §24.1.3)

**5150 #2, the "not period" one** (docs/FIELD-MACHINES.md): a **PVGA1A-JK**
as primary, a Hercules GB101 beside it, stock 4.77 MHz 8088. After a few
minutes of ordinary use the whole screen **recolours** — the desktop's
black/white dither goes lavender, Arkanoid's red brick row goes purple, a
white window frame goes cyan — and the same wrong palette is on the Locator's
own screens, not just the game's.

**Every shape, glyph, window edge and brick is still exactly where it
belongs**, and that is the finding rather than the decoration. In mode 12h a
pixel is four plane bits → one of 16 **attribute** palette registers → one of
256 **DAC** entries → three analog guns. A fault in the RAM moves *pixels*:
speckle, dropped columns, sheared glyphs, garbage in patches, always local,
because a plane bit belongs to one pixel. A fault in any stage after it
recolours a correct picture **uniformly**, because those stages are shared by
every pixel on the screen. **A photograph of intact text in the wrong colours
has already ruled out the memory** — which is why the first answer to "is this
bad video RAM, shall I write a RAM tester" is *no, and a RAM tester would
measure the one part the evidence has cleared*.

**What has NOT been ruled out, and how to tell them apart.** The three
candidates are the attribute registers, the DAC, and everything after the DAC
(output stage, cable, connector, monitor). The first two can be read back and
the third cannot, so SPEC.md §39.21 puts the readback in `tests/sysbench`'s
video block: DAC entry 0, DAC entry 0x3F, a checksum over all 768 DAC bytes,
the first three attribute palette registers and a checksum over all sixteen,
plus `SR01` and `GR06`. (0x3F and not 15: the attribute palette maps colour 15
to 3Fh, so entry 15 is a number no pixel goes through.) **Run it with the screen right and again with it wrong.**

- Numbers **differ** ⇒ a register somebody wrote, and it is ours. The
  checksums say which stage.
- Numbers **identical** ⇒ the digital side is intact and the fault is after
  the DAC. No software can reach it, and the next moves are physical: reseat
  the card and the monitor cable (a high-resistance ground on one colour pin
  raises that gun's black level, which is exactly a lavender black), try the
  other monitor, and try the card in another slot.

**DAC entry 0 is the row to read first.** It is black. A black that is not
black tints every dithered pixel on the screen, and a lavender desktop is
precisely that.

**One thing the kernel does NOT do, which is worth knowing before suspecting
it:** os8088 never writes the attribute controller or the DAC. Neither port
appears anywhere in `kernel/`; the palette is whatever the BIOS mode set
installed, and the only 3C0-family access in the tree is `fsx_insync` READING
3DAh, which resets the port's flip-flop rather than disturbing it. So if the
registers have changed, something outside this kernel's own drawing changed
them — the BIOS during an fsx mode round trip, a package, or the hardware.

**The one software path that touches the palette at all is `int 10h`**, and
that gives a cheap discriminating experiment. Nothing in this tree — kernel,
apps or drivers — writes 3C0h, 3C8h or 3C9h; the palette is whatever the BIOS
mode set installed. After boot the ONLY thing that re-issues a mode set is a
fullscreen bracket that changes mode (SPEC.md §53.4) and its restore
(§53.6). So:

- if the recolour **only ever appears after entering and leaving a fullscreen
  app**, suspect this card's BIOS mode set and say which app and which mode;
- if it appears with **no mode set having happened in the session** — no
  fullscreen, no reboot — then nothing in software wrote those registers and
  the fault is drift, in the DAC or after it.

Both are worth recording when it next happens, and both are one sentence.

**Not reproducible here in any form.** No emulator in the container models a
DAC that drifts, and 5150 #2 is the only real VGA in the register.

### 24.1 The first field data, and what it changed

Two `sysbench` runs minutes apart on 5150 #2, one screen-corrupt and one only
slightly so:

| row | run A | run B |
|---|---|---|
| `dac 0 r g b` | 0 0 0 | 0 0 0 |
| `dac 3F r g b` | 63 63 63 | 63 63 63 |
| `dac 0..255 sum` | **2130** | **32FD** |
| `attr pal 0 1 2` | 0 1 2 | 0 1 2 |
| `attr pal sum` | 0206 | 0206 |
| `SR01 GR06` | 0105 | 0105 |

**The DAC's contents moved and the attribute stage did not** — `0206` is also
exactly what a known-good emulated card reads, so those sixteen registers are
the standard table and intact. Black is black and white is white in both runs,
which is why the *shown-16* sum was added: the all-256 checksum detects a
change and cannot say whether it is in an entry anybody can see.

**It is not yet drift, and the missing measurement is cheap.** No good/good
pair exists — the machine will not stay clean long enough — and a DAC read
that is merely noisy on a PVGA1A produces the same two numbers. **Two runs
back to back while the screen is good** separate them, and nothing else does.

### 24.1.1 The second pair: it is the DISPLAYED entries, and it happens ON ACTION

Hercules removed, Picomem still in (it is the floppy controller, so it cannot
come out without swapping hardware). Two more runs, with the shown-16 row:

| row | less corrupt | more corrupt | healthy |
|---|---|---|---|
| `attr pal sum` | 0206 | 0206 | 0206 |
| `dac 0` / `dac 3F` | 0 0 0 / 63 63 63 | 0 0 0 / 63 63 63 | same |
| `dac 0..255 sum` | 4CAF | 3161 | — |
| **`dac SHOWN 16 sum`** | **057E** | **044B** | **05D3** |

`05D3` is the standard 16-colour VGA palette summed by hand (0,0,0 / 0,0,42 /
… / 63,63,63). **Both runs are below it** — 1406 and 1099 against 1491 — so
the entries the screen goes through really are being changed, the "less
corrupt" screen really was already corrupt, and the direction is *darker*.
That is the measurement §39.21's shown-16 row was added to make, and it makes
it: this is not a read that is merely noisy, and it is not confined to the 240
entries nobody displays.

**AND THE TRIGGER IS AN ACTION, NOT TIME.** After switching the primary to Cga
and back, the screen "stays good forever" if the machine is left alone.
Switching to the File Manager window corrupts it; opening `sysbench` corrupts
it again, differently; each action moves it to a *new* corruption state and it
then holds that state until the next action. **A supply rail or a thermal
fault does not wait to be clicked**, so 24.2's marginal-machine reading is
wrong, or at least is not the whole of it.

**What every one of those actions has in common is the DISK.** Raising a Disk
window re-lists its folder (§22.8's `fm_focus`), which is a mount; launching
`sysbench` is a mount plus a 21KB read. Sitting still is the only state with
no floppy activity in it — and the Picomem is the floppy controller. So the
correlation on the table is **disk activity ⇒ palette damage**, not
**drawing ⇒ palette damage**, and the two are trivially separable:

- **Drawing with no disk**: drag a window around the screen for a while, open
  and close menus, run the pointer over the dock. Nothing there touches the
  floppy. Does it corrupt?
- **Disk**: open a Disk window on A: and press Refresh a few times.

If dragging is safe and Refresh is not, it is the Picomem or the bus and no
change to this kernel will help. If dragging alone corrupts it, it is the
drawing path and it is ours.

**What is already ruled out on the kernel side:** every VGA port write in the
drawing path is a canonical Graphics Controller or Sequencer index+data pair
written as one 16-bit `out dx, ax` — Set/Reset, Enable Set/Reset, Data
Rotate, Read Map Select, Mode, Bit Mask, Map Mask, all with standard values —
and **nothing in the tree writes 3C6h, 3C7h, 3C8h or 3C9h at all**. The
kernel has no code that can change a DAC entry. That does not clear the *bus*,
which is what the experiment above is for.

### 24.1.2 The experiment ran, and it is DRAWING VOLUME — not the disk

Reported, in one sitting: boot, open the Control Panel — *slight* corruption
as the menu dropped, *more* when the panel opened. Switch the primary to Cga:
corruption gone. Switch back to Vga: clean. **Wait 60 seconds: still clean.**
Drag the Control Panel window: **instant** corruption.

**A drag touches no disk whatever.** `ui_drag`'s tracking loop is an XOR
outline redrawn at every mouse sample with the gfx lock held — nothing else,
no mount, no read. So §24.1.1's disk correlation is dead and the Picomem is
cleared: what the actions had in common was not the floppy, it was **how much
they draw**.

And the gradient is right there in the report: a menu drop is a save-under
plus a highlight (slight), a window paint is a few hundred primitives (more),
a drag is a continuous stream of read-modify-writes for as long as the button
is held (instant). Sitting still draws nothing and never corrupts. **Mode 6
never corrupts either**, and it is the same card: 640x200 in ONE plane against
mode 12h's 640x480 in four, which is roughly an eighth of the memory traffic
and none of the planar read-modify-write.

**So the provocation is VGA memory traffic, and the damage lands in the DAC.**
Those are different parts of the card, which is what makes this hardware
rather than a register the kernel mismanages: no sequence of writes to the
Graphics Controller or the Sequencer can change a DAC entry, and the kernel
issues nothing else — it never writes 3C6h/3C7h/3C8h/3C9h at all (§24.1.1).
A card whose DAC RAM loses bits while its display RAM is being hammered is a
marginal card, a marginal slot, or a supply that sags under burst load, and
this backplane is 384KB of ISA RAM plus a Picomem plus a VGA on an IBM 5150's
63.5W supply.

**One more measurement is free and worth having**, because it separates "any
heavy drawing" from "something os8088 does": `gfxbench` on the combo disk
hammers every primitive for minutes with almost no disk in it. If it corrupts
within seconds, the answer is drawing volume and nothing about which program
is doing it.

**And a workaround exists if it is wanted, at a price worth naming.** The 16
entries are deterministic — the standard table, summing to 05D3 — and a mode
set already repairs them, which is why Cga-and-back works. The kernel could
reload just those 16 entries on demand (a Control Panel button) or on every
full repaint. That would make this machine usable and it would **end the rule
that os8088 never writes the DAC**, which is currently what makes the readback
above a diagnosis rather than a measurement of our own writes. It is not done
unasked.

### 24.1.3 FIXED — oxidised sockets — and the instrument was flawed

**Four socketed chips on the PVGA1A, pulled, sockets sprayed with DeoxIT,
reseated. No corruption for a whole session.** That is the fault: contact
resistance on the card, provoked by the memory traffic that mode 12h drawing
generates and not by time, the disk, or anything in this kernel — which is
exactly where §24.1.2's gradient pointed, and it is a satisfying place for a
software chase to end.

**AND THE READBACK ROW DOES NOT SURVIVE THAT.** The clean, fixed machine
reports `dac SHOWN 16 sum` = **0441**, where a known-good VGA reads **05D3**
(verified independently: the sixteen attribute registers read the standard
table and the entries they name sum to 1491). Worse, the three field readings
do not order the way the screen did: **057E** on the *less* corrupt run,
**044B** on the *more* corrupt one, **0441** on the *clean* one. **A number
that does not correlate with the thing it is measuring is not measuring it.**

The likely reason is in the IBM VGA documentation and I did not honour it: a
palette access can collide with the display's own lookup, which is why period
software programs the DAC during vertical retrace. A read taken mid-display
can return what the CRT is fetching rather than what was addressed.

So the row now **takes the sum twice and prints both**. Two sums that differ
say the read is unreliable on this card and the first number cannot be
trusted — and nothing about diffing two reports could ever have said that.
**The claim in §24.1 and §24.1.1 that "the DAC's contents moved" is withdrawn:
what moved may have been the reads.** What the field data does still support
is the trigger — idle is safe, drawing is not, mode 6 is not — and that
survives because it was observed on the glass rather than through this row.

### 24.1.4 The double read fired on its first outing

The very next field run, on the repaired machine, in ONE pass:

    dac SHOWN 16 sum (hex)  0433
    ...read again (hex)     03D2

**Two sums of the same sixteen entries, taken milliseconds apart, disagree.**
That settles it: the DAC readback is unreliable on this card, every number
§24.1 and §24.1.1 read off it was noise, and the withdrawal of "the DAC's
contents moved" was right. The instrument now says so itself instead of
needing a second report and a hunch — which is the whole difference between a
diagnostic and a number.

The likely mechanism is the one §39.21 names: a palette access colliding with
the display's own lookup, which is why period software programs the DAC during
vertical retrace. **Fixing it properly means reading inside the retrace
window**, and that is worth doing only if this row is ever needed again — the
fault it was written for turned out to be contact resistance, and it was found
by watching the screen rather than by reading registers.

`3BA or/and` reads `9F16` in the same run and `3DA` reads `3D04`, both with
AND a subset of OR, which is the byte order corrected — the `88FF` that
exposed it was impossible.

### 24.2 The Hercules destabilised too, and that outranks the DAC

With the desktop extended onto it (which SPEC.md §39.11.1.1 made possible for
the first time), the **Hercules** went wavy at the edges and "out of phase" as
well. **A Hercules has no DAC, no attribute controller and no palette at
all** — it is TTL mono, one bit per pixel, straight out of a 6845. Whatever is
happening there cannot be a palette fault, so it cannot be the same fault as a
palette fault, so *either* there are two faults *or* the common cause is
upstream of both cards.

The rest of the picture points the same way. The VGA **repairs itself on a
mode set** (switching the primary to Cga and back, which is two `int 10h`
calls on the same PVGA1A — §39.11's "a VGA can always do CGA" — and rewrites
the DAC and the CRTC). Mode 6 at 640x200 **never** corrupts, which is a much
lower dot clock than mode 12h's 640x480. And the fault drifts through
*states* over minutes: whole screen purple, then reddish, then wavy lines.

Latched state decaying, a lower-bandwidth mode surviving, a second unrelated
card losing its timing, and a rewrite fixing it until it decays again looked
like the signature of a **marginal machine** rather than a marginal card:
supply rail or bus. **§24.1.1's trigger narrows that and §24.1.2 finishes it**: the
damage arrives on an *action* and not with time, so a slowly-sagging rail is
out; and the action turns out to be **drawing**, not disk, so the Picomem is
out too. What is left is the card and the bus under burst load — which is
exactly the reading a second card losing its 6845 timing supports. 5150 #2 carries 384KB of ISA RAM, a Picomem and **two** video cards on
an IBM 5150's 63.5W supply, which is the load that supply is famous for not
having. **This is a hypothesis about hardware nobody here can see**, and the
tests that settle it are physical: pull the Picomem, then the Hercules, and
see whether the VGA steadies; measure +5V under load; reseat both cards and
try other slots. If it steadies with less in the backplane, no amount of
software is the answer.

## 25. An XMS RAM disk "corrupted" what was copied onto it (SOLVED — the volume was too small, and the copy TRUNCATED IN SILENCE)

Reported on an 86Box 386 with 4MB. The reporter's own sequence, which is what
solved it:

* type **1024** into the size box — **it corrects to 264**
* mount, open the RAM drive (the correction to 264K was not noticed)
* drag a **297KB** mod onto it from a hard-disk Disk window
* it appears to arrive; double-clicking it says *not a mod*
* delete it, copy **BEVERLY.MOD** (116KB) instead — apparent success, *not a mod*
* drag that back to the hard disk — still *not a mod*

### It was two defects, and neither is the extended-memory store

**The 264K was ours** (SPEC.md §62.9.10.3): `rd_kb_max` subtracted conventional
room for the chain table and the bounce *from the ceiling* instead of gating on
it, so an extended store could never exceed the conventional heap it exists to
escape. That is why a 4MB machine offered 264K. **Fixed** — and confirmed by the
reporter, who then mounted 1024K in extended memory, copied the 297KB mod
cleanly and played a 150KB one off the drive.

**And a copy that runs out of room leaves a TRUNCATED FILE with no error left on
screen.** Reproduced exactly: a 264K store, `BANANA.MOD` 304,552 bytes dragged
on, and the volume afterwards lists **`BANANA.MOD 260096`** — `Size 254K
Free 10K` — a file whose directory entry states the truncated length as though
it were the file's own. `fcp_file1`'s `.err` returns and **nothing deletes the
partial**; the verdict goes to a §59.5 toast, which expires. So the reporter's
first file was truncated, and their second failed for the *same* reason: after
the first copy the store was 10K free, and a 116KB file into 10K truncates too.
Deleting the first file between them did not help, because the free space they
were told about was §62.9.10.5's figure — which was also wrong.

**This one is NOT specific to the RAM disk and is NOT fixed.** `dskw_append`
grows a file incrementally by design and the copy engine chunks, so *any*
destination that fills mid-copy leaves a partial — a floppy does the same. The
fix belongs in `fcp_file1`/`fcp_file2`: a copy that fails after creating its
destination should delete it, so a failed copy leaves nothing rather than
something that looks like the file. It is a change to the engine every volume
shares and it wants its own commit and its own testing.

### What was ruled out along the way

**The extended-memory ownership fence**, which looked exactly like it.
`OSAPI_XMEM_COPY` refuses a range whose block is not the caller's, and
`drv_fs_call` was not clearing the dispatch stamp (§62.9.10.4) — so the driver
could ask for its own bounce as somebody else and be refused, while
`rd_stage_in`/`rd_stage_out` threw the refusal away (§62.9.10.2). That composes
into precisely this symptom.

It is fixed, and **it is not this.** `snd_req_inst` answers `0xFF` when no
callback is being dispatched, which is what a Disk window's own drag or
Edit ▸ Paste is — Locator has no instance — so the copy asks as `0xFF`, matches
a block stamped `0xFF` at Mount, and works. Measured on QEMU: the same scripted
floppy → XMS volume → floppy round trip of that 116,085-byte file comes back
**byte-identical under `make FSNOSTAMP=1`**, the build with the defect put back,
and byte-identical with it fixed. Two builds, one gesture, 0 differing bytes
either way — at 16MB with 8KB extents *and* at 264K with 1KB extents.

**It is still worth having fixed**, because it is reachable from every path that
carries an instance stamp: `wm_pkgcall` does `push word [snd_inst]` /
`call snd_disp_set` around every `W_PAINT`, `W_ONKEY` and `W_ONCLICK`, so a
package's Save, a file-dialog commit inside an application and a worker all ask
as themselves. Saving from an app onto an extended-memory RAM disk *was* broken
and silent. Nobody had done it.

### The one that reads as a bug and is not

**A drag between Disk windows is a COPY, not a move** (SPEC.md §22.3/§22.5), so
the source file staying where it was is correct.

---

## 26. A window dragged onto the second monitor comes back smaller (FIXED, SPEC.md 11.100/39.16.3, gated by tests/dispsize.py) — and §26.2, which looked like the worse half of it, is NOT A BUG

**Reported with two 86Box screenshots** of the extended desktop — a Hercules
beside a CGA, the field machine's own pair (docs/FIELD-MACHINES.md) — as *"in
extended desktop mode resizable windows will resize to fit the smaller screen
when they are dragged over. This is good, but it can end up with too small of
windows."*

**Both halves reproduce, and `tests/dispsize.py` is the reproduction** — one
run, both measurements, on `os8088_5150_both_gla`, a cycle-accurate 5150 with
those two cards in it, Hercules primary, Extend / Right.

### 26.1 The straddle cut is permanent

A Disk window opened on the Hercules, dragged across the seam, on to the CGA,
and back. `rect` is the record, `bank` is the natural bank (SPEC.md
§39.11.2.1) — the rect the window is supposed to go *back* to:

| step | rect | bank |
|---|---|---|
| opened on the Hercules | (103,80) **320x200** | (110,80) 320x200 |
| straddling the seam | (607,80) **320x140** | (607,80) **320x140** |
| wholly on the CGA | (759,80) 320x140 | (759,80) 320x140 |
| dragged back to the Hercules | (199,80) **320x140** | (199,80) 320x140 |

**Mechanism identified, not theorised.** `ui_drag`'s release runs
`wm_strad_fit` (§39.16.3) and *then* `wm_nat_bank`, so the bank records the
cut. The comment at that call site says the ordering is deliberate — "BEFORE
the bank, so what is remembered is what the record holds" — and it is the
wrong way round for this: §39.11.2.1 introduced the bank *because* a clamp
throws the number away, and `wm_strad_fit` is a clamp. Nothing else in the
machine can put the size back except a `wm_refit`, which only an adapter
switch runs.

### 26.2 …and a window dropped clear across the seam is not cut at all (NOT A BUG, SPEC.md 39.16.3.2)

Solitaire — 258x**303** on the Hercules — dragged onto the CGA in **one**
motion lands at (743,20) still **258x303**. The CGA holds virtual rows
20..219, so **104 rows are in the dead zone** (§39.2.1): drawn nowhere,
clickable nowhere, on no monitor.

`wm_strad_fit` answers `.none` when the frame does not *reach* the other
display, and it is evaluated once, at the release. `ui_drag` bounds x and y
against the whole union and never calls `wm_fit`, so a release wholly on the
short display meets no size clamp of any kind.

This is the worse of the two and it is not what was reported — the report is
§26.1's symptom. It was found looking for §26.1's mechanism.

**And it is not a defect at all, which the SECOND field report settled**
(SPEC.md §39.16.3.2). The fix below cut it to 200 rows, and the machine came
back: *"On the primary screen, windows are allowed to go 'below the desktop' —
they keep their shadow under, and can be moved down and up. On the secondary
screen they are always resizing, even if they have not crossed a screen
boundary."* Both sentences are about one act. A 303-row window dragged low on
the **Hercules** hangs off the bottom of the desktop and is left alone; the same
window on the **CGA** hangs off the bottom of the CGA, and the clamp cut it —
because `[vid_h]` is 348, so the guard "is this limit inside the desktop"
answers *yes* for a primary window at row 348 and *no* for a secondary one at
row 220, having compared both against a bounding box neither of them is
bounded by. Rows 220..347 at x ≥ 720 are not a hole in the desktop; there is no
display there at all, exactly as there is none at row 349 on the Hercules. So
the §39.2.1 dead zone is a real place a window may hang into, on either
display, and the clamp is gated on the window actually **reaching** the other
one again. What survives of this note is the finding: a rule derived from the
union's bounding box treats regions no display has as though the desktop owned
them.

**What has been ruled out:** the drag arithmetic (both rects land exactly where
the pointer asked, and `tests/dispstrad.py` passes — the straddling case it
gates is the one that *works*), and `wm_fit` (it is never called on this path).

**What was missing rather than broken:** the kernel had a clamp and a bank and
neither was *the size this window wants on the display it is on*. SPEC.md
§11.100 is what it has now — a preferred size per adapter kind, a minimum the
kernel may not cut through, and the two ordering fixes above;
`docs/WINDOW-SIZING-PLAN.md` is the investigation, including what all 24
packages do about their size today.

**Fixed, measured the same way it was found.** §26.1 is 200 → 140 → **200**:
`ui_drag` banks its POSITION and re-derives its SIZE from the bank, so the
straddle cut is no longer what the window goes back to (§11.100.3). §26.2 is 303
→ **303** — the second report reversed its verdict, above, and the clamp is
gated on the frame reaching the other display again (§39.16.3.2). Both
measurements are `tests/dispsize.py`'s, which is the file that found them and
is the file that now asserts the second one is left alone.

**The straddle clamp is `wm_reflows`-gated** (§39.16.3.1), or it would put note
26's neighbouring defect straight back. For the fixed layouts that gate
refuses, the answer is §11.100.4: a window that has DECLARED a size for that
adapter is handed it and told, and `apps/modplug` is the first — its compact
face onto the CGA and its full one home, **0 differing pixels** against a
forced full repaint.

---

## 27. A window that draws every frame starves the pointer — REPRODUCED; the LOCKOUT is fixed, the starvation is not

**Reported** while looking at `apps/wire` (SPEC.md §78) on the field machine:
*"we are only getting mouse input when not drawing, and edge-then-repair is
always drawing. I could not even click to close the window, after over a
minute of trying."* The same reporter names `apps/paint` as the program this
has always been worst in, and that is the reason to write it down rather than
file it under the demo.

**The arithmetic says it should happen and that is not the same as reproducing
it.** §78's worker takes the gfx lock for one burst a frame. At `Medium` /
`Edge at a time` that burst is ~52 ms of a 54.9 ms tick; at `Edge, then
repair` it is ~82 ms of an 82 ms period — 1.5× the line work and no sleep left
at all (PERFORMANCE.md Set 73's 12.1 fps). Whatever the UI task needs the lock
for is then waiting on a routine that never lets go for longer than it takes
to re-take it.

### 27.1 Reproduced, and it is all three of the old candidates at once

`apps/wire` at any draw order, on `os8088_5150_herc`. The pointer moves
throughout; the clicks do not arrive.

**Liveness first, with `tests/dispfreeze.py`'s own instrument** — a MEMORY
breakpoint on the byte `ui_task` step 0 reads, which is the only honest pass
counter (an exec breakpoint fires on the 8088's prefetch, and `[ticks]` is
bumped from inside IRQ0 so it advances through a UI task that has stopped):

| | `ui_task` passes, per second of GUEST time |
|---|---:|
| desktop, nothing running | **650** |
| `apps/wire`, Whole figure | **18** |
| `apps/wire`, Edge at a time | **18** |
| `apps/wire`, Edge, then repair | **18** |

**18 is the tick.** The UI task is not dead — it is making exactly one pass
per tick, a **36× drop**, and the number is the same for all three draw
orders because all three hold the lock for very nearly the whole frame.

**And a pass pops ONE event.** `ui_task` step 2 is a single `evq_pop` and then
the pass runs its end-of-pass housekeeping (`kbm_ui`, `ui_arm_chk`,
`ui_timer_pass`, `fdlg_reap`, `wm_close_pass`, the deferred launch) before
looking again. So the drain rate is **18 events a second at best** — and worse
whenever a pass actually dispatches something, because that pass then blocks
on `gfx_lock` for a whole frame.

**The ring is 16 records and a full one drops the NEWEST** (SPEC.md §10,
`evq_push`'s `cmp word [evq_count], EVQ_CAP` / `jae .full`). Measured while
clicking as fast as the packets go:

```
evq depth: peak 16 of 16, last ten [16, 16, 16, 16, 16, 16, 16, 15, 16, 16]
closed on click NEVER (40 tries)          ...and 35 on a second run
```

**Then stop clicking, and it cures itself:**

```
after 3.6s of not clicking: evq depth 0
one calm click: CLOSED
```

Which is the report exactly — sixty seconds of clicking with nothing getting
through — and it is also the workaround: **take your hand off the mouse for a
second.**

### 27.2 Three defects, and the fix for any one of them would have hidden the others

1. **`gfx_lock` has no fairness** (`kernel/vga12.inc`): `.retry` is `cli`,
   test, `sti`, `task_yield`, round again. No queue, no ticket. A worker that
   releases and immediately re-takes wins against a UI task that has to be
   scheduled first, so a 95%-duty worker starves it 36×. **Right, and FIXED —
   SPEC.md §7.3. §27.4 below says how nearly it was thrown away.**
2. **The UI task drains one event a pass.** At 650 passes a second nobody
   would ever notice; at 18 it is the whole bandwidth of the machine's input.
3. **A full ring drops the NEWEST press.** For input this is the wrong end:
   dropping the newest means a *sustained* burst locks the user out for as
   long as they keep trying, where dropping the oldest would always keep the
   press they most recently meant. It is why clicking harder makes it worse.

Paint's symptom is the same three seen from the other side and is **not** a
lost press: the ISR keeps `mouse_x`/`mouse_y` fresh, so the *positions* are
right and the cursor tracks — what is lost is the intermediate motion between
one UI pass and the next, which is why a fast swing draws one long straight
segment ("ziggy and zaggy") and a stroke ends a little short of where the hand
stopped rather than nowhere near it.

**It is not caused by SPEC.md §5.6.4.1** — the same starvation arithmetic
holds for any worker that fills its tick, and `apps/paint` predates all of it.
What §5.6.4.1 changed is that a *line-drawing* program can now fill its tick
with far more drawing, which is why this surfaced now.

### 27.3 Defects 2 and 3 are fixed, and it was 2 that mattered

SPEC.md §10.1 turned the full-ring policy round and §10.2 made the pass drain
the ring instead of sipping one record. Defect 1 is untouched: the UI task
still gets 18 passes a second under a drawing worker, and nothing here claims
otherwise.

**The reproduction.** `apps/wire` at `Edge at a time`, its close box hammered
sixty times with no pause between press and release — as close to the reported
hand as a script gets — on `os8088_5150_herc`. "Closed" is the window actually
going away; "last-popped" samples `ui_ev`'s type once per click, so it says
which half of each gesture `ui_task` was getting.

| | last-popped types | result |
|---|---|---|
| as shipped before this (one record a pass, drop newest) | `{MDOWN: 60}` | **never closed** |
| §10.1 alone (one record a pass, drop oldest) | `{MDOWN: 9, MUP: 51}` | **never closed** |
| §10.2 (drain), either ring policy | mixed | **closed in 13–40 clicks, 1.8–5.8 guest s** |

The first two rows are the finding, and neither was predicted. **One record a
pass separates a press from its release, and which half survives is decided by
the queue's arithmetic rather than by anything about the user.**

- Refusing the newest leaves exactly one free slot per dispatch and the
  *press* wins it every time, because a press is what a hand does next. Sixty
  hammered clicks produced sixty dispatched `EVT_MDOWN`s and not one
  `EVT_MUP`: the close box was armed sixty times and spent none. That is
  precisely *"I could not even click to close the window, after over a minute
  of trying"*, and precisely why it comes right the moment the hand stops.
- Discarding the oldest lands on the other parity — the drop and the pop both
  take the head — so the releases arrive and the presses are eaten. Same
  outcome, opposite half.

So §10.1 is not what fixed this; §10.2 is. §10.1 is kept because bounded
staleness is right on its own terms and measured no worse: with the drain in,
the two policies close the window in 27/36 clicks and 13/40 clicks
respectively, which is one distribution.

**Paint is not fixed.** Its symptom is defect 1 — the UI task not running
often enough to sample the pointer — and the drain does nothing for a stream
of positions that were never queued in the first place. Expect it to still be
ziggy and to still stop short.

### 27.4 Defect 1 was nearly thrown away on a measurement taken at the wrong moment

This section said the opposite for one round, and the way it got there is worth
more than the conclusion.

Three counters went into `gfx_lock` — acquires, blocks, handovers — and were
sampled per guest second with `apps/wire` drawing. They reported **0 blocks a
second** at every draw order. A fairness handover was built, measured against
that, found to fire zero times, and **reverted as dead weight**.

The counters were right. The measurement was taken with **no input pending**,
and the UI task only asks for the lock when it has something to draw. So it was
taken at precisely the moment the defect cannot appear. A worker drawing to an
empty desk contends with nobody, and *that* is what 0 blocks a second means.

What exposed it was building an instrument for the thing actually being
complained about — latency — rather than for the thing suspected. A memory
breakpoint on `evq_tail` (the mouse ISR queueing the press) and one on
`menu_ent` (`menu_track` with the pull-down up), reading `cycles` at each:

| | press queued → menu up | blocks, across that window |
|---|---|---:|
| idle desktop | 1–2 ms | 0 |
| wire drawing, no fairness | **1,382 – 14,722 ms** | 26 – 268 |
| wire drawing, §7.3's handover | **37 – 70 ms** | **1** |

The same counters, counted across the click instead of across the second, say
the UI task was blocking once per pass, every pass, for seconds. **Contention
is a property of the moment a click lands.** Counting it anywhere else answers
a question nobody asked.

The quantum work (§27.4.1) came out of the wrong diagnosis and survives it: it
is real, it is measured, and with §7.3 in place it is no longer the lever.

#### 27.4.1 The quantum is real, and it is not the lever

`ui_task` yields the moment its pass is done; a drawing worker spends its whole
55 ms slice; so the UI task gets one pass per timer tick. That is why the pass
count is 18 for all three of wire's draw orders and why it is *exactly* the
tick. SPEC.md §53.2.1's sub-tick already fixes it — `sch_fast_on` makes IRQ0
arrive N times a tick with `[ticks]` unchanged — and `make QUANTUM=2|3|4` arms
it system-wide:

| | ui passes/s | wire fps (whole / edge / repair) |
|---|---:|---|
| 55 ms quantum, as shipped | 18 | 18.2 / 18.2 / 12.1 |
| 18 ms quantum, `QUANTUM=3` | **54** | 17.1 / 16.1 / 12.1 |

Before §7.3 that was the only thing that moved the field symptom at all: it
took a machine on which a menu could not be opened to one where it could,
reported as *"it functions, but 2-3s before anything happens"*. The latency
instrument explains both halves — 3× the passes against a defect that costs
seconds is still seconds.

On top of §7.3 it measures 16–61 ms against 37–70, which is inside the noise.
So it stays a knob and stays off: with the handover in place the UI task does
not need more passes, it needs the one it gets to succeed.

### 27.5 The press-and-hold report does NOT reproduce here

Reported after §10.1/§10.2 landed: a click now works, but *"click, hold down,
move across the screen, let up — did nothing"*, and on the menu bar
*"clicking and releasing makes the menu flash; clicking and holding did not
bring and keep the menu open."* The reporter's own reading is that a dropped
event makes a hold look like a click, which is the right shape: every
press-and-hold path in this kernel (`menu_track`, `ui_drag`, `ui_grow`,
`fm_drag`) asks a LIVE question — the queued `EVT_MUP`, or `mouse_btn`'s level
— so a press dispatched *after* the hand let go collapses instantly.

Four instruments, on `os8088_5150_herc` with `apps/wire` drawing at both fast
draw orders, and none of them shows it:

- **menu press-and-hold**: press on the chip menu, hold 1.6 guest seconds
  without releasing, sample `menu_ent` every two frames — **menu up in 40 of
  40 samples**, idle and loaded alike.
- **title-bar drag**: wire's own window dragged +40/+20 — arrives at
  +40/+29, the same overshoot the no-worker control shows, so the drag tracks.
- **single unverified packets**: `os88mouse._edge` resends the button packet up
  to twenty times and proves it landed, which a real serial mouse never does —
  so every scripted gesture in this tree is immune to a defect a hand is not.
  Sending **one** raw packet per edge: press seen 25/25, release seen 25/25,
  idle and loaded.
- **the lock**: 0 blocks a second (§27.4).

Every one of those four is measured through `tools/os88mouse.py`, whose
injection path costs ~0.51 guest seconds flat (SPEC.md §7.3.1) — three of them
also *resend until the guest agrees*, which a hand never does. So they proved
the packets arrive and the handlers work, and were blind by construction to the
one thing being reported, which was how long it all took.

**It was the latency, and §7.3 is the fix.** The `QUANTUM=3` A/B is what said
so: on the field machine it took a wire window whose menu would not open and
whose title bar would not drag to one where both work, *"still painfully
unresponsive — 2-3s before anything happens after clicking — but it functions"*.
Three times the passes against a defect that costs seconds is still seconds,
and the second half of that sentence is the defect. §7.3 takes the same click
from 1,382–14,722 ms to 37–70 ms.

`apps/paint` was expected to be unchanged by all of this and was reported
unchanged. **It is a separate defect and it is mostly fixed in SPEC.md
§42.8.1** (and §42.8.2 is what the field said it was worth, including one
prediction it contradicted): not
the UI task's pass rate at all, but `pt_stroke`'s own wait. Its idle branch
spun to the next `[ticks]` boundary whenever the pointer was where it already
was — and at 1200 baud that means "the next report has not arrived yet", not
"the hand stopped", so a 40 Hz mouse was aliased to 18.2 Hz. Measured 20
samples a second, which is the tick to the digit; 88 after. The reporter's
`hello world` GIF is a hand drawing a letter in half a second and getting ten
samples.

## 28. CURFIX still reads wrong to the eye — OPEN, second report

SPEC.md §7.1.4.4 left §7.1.4.2 + §7.1.4.3 behind `make CURFIX=1` because the
instruments and the eye disagreed, and asked for the pair to be judged on a
real machine. It has now been judged twice, by the same reader, on an
`os8088_5150_herc`-class field machine, and the answer both times is a
qualified no:

> *"I once again think curfix feels 'slightly weird'. Almost like the
> acceleration is wrong, even though we shouldn't have changed that. And it
> flashes just as much with wire running as no-curfix."*

Two separate claims and they are worth keeping apart.

- **"The acceleration is wrong."** Nothing in either section touches
  `mou_isr`'s deltas — §7.1.4.3 adds one store of `[ticks]` into `[cur_mvt]`
  and reads it in `cur_lazyck`, and that is the whole of the arithmetic. So
  either the report is about *when the arrow is redrawn* rather than where —
  a pointer hidden through a draw and put back at the new place reads as a
  jump, which is §7.1.4.3's own "hidden and stuck" — or it is something not
  yet found. **It is a claim about motion, and §7.1.4.4 already says the four
  instruments in `tools/` all park the pointer.** The missing instrument is
  still missing.
- **"It flashes just as much with wire running."** `apps/wire` holds the gfx
  lock for very nearly the whole frame (note 27), and the mouse ISR does not
  move the arrow while it is held (§7.1). Neither knob changes that, so a
  cursor over a window that is drawing every tick is expected to behave the
  same on both builds — this half is consistent rather than surprising, and
  it means the wire case cannot discriminate between them.

**The default does not move.** Both disks were built at one commit with only
the knob between them and a marker file in each root, which is the comparison
§7.1.4.4 asks for; the pair stays available and stays off. The next step is an
instrument that reads a MOVING pointer, not another A/B of the same two disks.


---

## 29. The 5150 hard-freezes on an FTP upload to the hard disk (TWO FAULTS found and fixed; the stack one is MEASURED at 220 of 256 and is a margin decision now, not a bug)

**Observed.** With the FTP server (§77) running and its Root pointed at
`C:/`, a client connects, logs in, and the machine hard-freezes. The last
line on the FTP window's log is **`CWD`**. No error, no toast, no cursor —
the guest stops.

**The one fact that shapes the whole investigation.** It reproduces on
**every build tried, including ones that demonstrably worked earlier the same
session**:

| build | what it did before | now |
|---|---|---|
| `3969745` | ran a full transfer at 8720 B/s | — |
| `9c11182` | produced the first complete profile, 36,080 ms wall | **freezes on CWD** |
| `0c12f31` | a transfer in 24,565 ms | — |
| `45ee710` | a transfer in 22,891 ms | — |
| `4322e5f` | — | **freezes on CWD** |

`9c11182` is the reference build: it was cut *before* `rep movsb` (§72.16),
it carries the profiler, and it completed a 297 KB upload on this machine an
hour before it started freezing. **A build cannot regress against itself.**
So the change is in the machine, not in the tree.

**Ruled out.**

- **`rep movsb` (§72.16).** The reference build predates it and freezes too.
- **`netbench`.** The freezing run had no benchmark window open at all.
- **The profiler being on or off.** It froze with it never started.
- **The report save.** Driven under QEMU with the FTP server running: `W`
  pressed, the system tick kept advancing for twenty seconds afterwards, the
  server still answered a fresh `LIST`, and the `NETBENCH.TXT` recovered off
  the field's own disk is complete and well-formed to its last byte. The save
  works.
- **The full client opening sequence.** `SYST`, `FEAT`, `PWD`, `TYPE I`,
  three `CWD`s, `LIST` and a 64 KB `STOR`, with `netbench` open and the
  profiler deliberately *not* armed — clean under QEMU.
- **A cross-linked directory chain cycling forever.** `dsk_dirw_*` already
  caps any one directory walk at `DSK_DIRW_MAX` = 256 sectors, and that guard
  exists precisely for a hostile FAT.

**Standing theory: the hard disk.** `CWD` with a `C:/` root is a *mount* of a
hard-disk directory — `dsk_chdir` → `disk_mount` → `int 13h` on the ST-225.
Nothing above that layer is unbounded, but `int 13h` itself is: a BIOS
spinning on a controller status bit that never comes is a hard freeze with no
code involved. The machine has been serving FTP writes for hours, and every
freeze since the first one has left the volume mid-write, which is a
mechanism for the fault to feed itself.

**The one-move test came back, and it is the disk.** With the server's Root
on the **floppy**: half a dozen connects, no freeze. Mounting the hard drive:
**instant freeze**. So it is not the socket stack, not the FTP server and not
the client — it is the hard-disk path, and the FTP server was only ever the
thing that made the machine touch it.

### 29.1 ...and the disk itself is NOT corrupt

The 20MB image came off the machine and was checked end to end. It is clean:

```
partition table at physical sector 68 - 68 reserved sectors in front of it
partition 0 type 0x04 at LBA 17, 41667 sectors: FAT16, 10388 clusters of 2048
verify-hdd OK: 53 file(s), FATs agree, no loops, no cross-links,
               every chain matches its size
```

**Raw sector 0 is not the MBR on this drive**, and reading it as one is how
this looked, for an hour, like a destroyed partition table. A **Seagate
ST-11M** controller reserves the front of the drive for itself and presents
the sector after its area as the BIOS's LBA 0 — so the real table is 68
sectors in, and every partition LBA is relative to there. Raw sector 0 holds
the controller's own geometry block (`SEAGATE`, `ST-225`, 615 cylinders, 4
heads, 17 sectors), repeated at sectors 1, 17 and 18, and it looks exactly
like garbage written over an MBR. The tell is that the partition's `hidden`
field agrees with the table entry **only** at the right offset: 17 both ways.

Two files on it do not match the current build — `ETHER.DRV` 21,411 bytes
against 17,052, and `NET.DRV` 5,592 against 5,599. **Both are internally
consistent**: each driver header's own image-size word equals the file's
length, so neither is a truncated or over-written file. The install on that
disk is simply old; the machine boots from floppies and mounts the disk only
to write to it.

`python3 tools/os88disk.py --verify-hdd IMG` is that check, kept rather than
thrown away — `--verify` is a floppy's and refuses a FAT16 volume and any
geometry that is not one of six real floppy shapes, so there was no way to
ask this question without a throwaway script, which is how a throwaway answer
gets trusted.

### 29.2 FOUND: a 512-alignment violation the 37KB claim moved onto a page boundary

A **brand-new** hard-disk image, connect, list the directory — fine — start an
upload: **instant freeze**. So it is not corruption of any kind, and it is
writing rather than reading. The field named the mechanism in the same
message: *"Think we just moved some memory around with the 37KB buffer
additions, and now we're crossing a 64KB?"*

`fd_stage`, the FTP server's 8KB staging buffer and the only buffer in that
package `int 13h` ever touches, sat at package offset **0x4233 — 51 bytes
into a sector**. A region base is a whole number of KB, so its linear address
was 51 mod 512 too. That breaks the project's one-line hard rule, and the
kernel comments on it at the very instruction that hands the BIOS a sector
which crosses a 64KB page (`dsk_runcap`'s `mov ax, 1`: *"only reachable from a
base that is not 512-aligned, which SPEC.md 2.4 forbids"*).

It was harmless until §72.13 made the socket rings a 37KB heap claim, which
moved every region above it — and where in a 64KB page an 8KB buffer lands is
precisely what decides whether it crosses one. **Nothing about the FTP server
changed; its buffer was standing somewhere else.** Fixed in SPEC.md §77.31,
with a `%error` beside the offset so a scalar added above cannot move it back.

`apps/cyclone`'s high-score buffer had the same violation and is fixed with
it. `apps/cc/os88thunk.asm` has it structurally — a C caller's pointer is not
this layer's to align — and is recorded rather than patched.

**What is still not proven** is why the floppy survived it: a floppy BIOS
answers a straddle with error 09h, which is a failed write and not a stopped
machine, and HDD.DRV's BIOS rung reaches a controller that evidently does
something worse. The buffer violated a documented rule on the exact operation
that froze; that is enough to fix it and not enough to close this note.

### 29.4 ...and at least one "freeze" was the machine being BUSY

Immediately after: *"Ok, I just went back to the VM and its NOT frozen. I
might just have not waited long enough for the text file save? I waited a
good 10 seconds, but..."*

Ten seconds is not obviously enough. `bl_save` writes the report with
`OSAPI_FILE_WRITE`, which creates or truncates a file, updates two FAT copies
and a directory entry — several `int 13h` calls, and PERFORMANCE.md prices one
at **~400 ms** on this machine whatever it moves. Then `bl_paint` redraws 29
rows at ~71 ms a row. All of it inside one window callback, so **the gfx lock
is held for the whole thing**: the cursor is parked, nothing on screen moves,
and the machine is indistinguishable from a dead one.

**`bl_progress` exists for exactly this** and its header says so —
*"a machine that has stopped answering is indistinguishable from a machine
that has died, and the first thing a user does about the second is reach for
the power switch"* — and `bl_save` was the one path in the file not using it.
It says `WRITING THE REPORT - a floppy write is seconds on this machine` now,
painted before the write starts. That is PERFORMANCE.md Part 6 rule 6 (*do not
ship a feature that silently costs seconds on the target*) applied to the
harness itself, and it cost this investigation two rounds.

**A later save on the same file did NOT come back after 60 seconds**, and that
is not explained by slowness. The second `W` overwrites rather than creates,
which frees the old chain and reallocates — a different path. Driven under
QEMU with the FTP server running, `W` pressed twice with a wait between: the
system tick kept advancing through both, and the server answered a fresh
`LIST` afterwards. So the overwrite path is not broken in a way QEMU can see.

**That is where this stands, and the two halves must not be merged.** The
512-alignment violation (§27.2) was real, is fixed, and was on the exact
operation that froze an upload. The save is a separate symptom, at least once
was ordinary slowness with no indication, and once was something else that is
still open.

### 29.5 The instrument for the next round, and the one thing it must not be

The freeze is still here after §27.2's fix: the most recent one is on a bare
`PWD`, with `netbench` open behind and nothing transferring. So the next round
is instrumentation, and the shape of it is already settled by the field:

> *"that 30s task pass never fired - the isr was frozen and gone with
> everything else. The end tool was one that constantly printed state, and we
> tracked it down from the 'last printout'."*

**That rules out every watchdog.** `KHB_STUCK`'s thirty-second report
(kernel/sched.inc) is printed by `sch_isr` — so a freeze that takes the timer
interrupt with it never prints anything, and the report's absence says only
that the report did not run. The instrument has to be one that is **already on
the glass** when the machine stops.

`KFZ=1` is that instrument and it is already built. `sch_isr` paints fifteen
bytes of kernel state into the top-left of the menu bar from IRQ0, **twice per
tick** — once at entry and once after the BIOS `int 08h` chain returns — so
the last picture on a stopped screen is a reading rather than a guess. It
found §9.6.5's int 09h self-jump, which is the same shape of failure: the
machine dead inside an interrupt gate with `IF` clear, nothing running, and
the screen holding whatever was on it.

| | reads |
|---|---|
| `beat`, `chain` | entries to `sch_isr` and returns from the BIOS chain. **One apart = it died inside the timer interrupt**; equal = it ran to the end and the fault is out in task code |
| `CS hi`, `IP hi`, `IP lo` | the interrupted address — `nasm -l` turns it back into a routine. CS high 00 = kernel, 0D = cold, anything else = a package |
| `sch_cur`, `sch_lock` | which task, and whether the scheduler is held |
| `gfx_lock_flag`, `gfx_lock_own` | the mutex and its holder |
| `SP hi/lo`, `stk0 bad` | the stack, and whether task 0's floor canary is still there |
| `PIC mask`, `PIC in-service` | is IRQ0 still let in, and is an interrupt still in service with no EOI behind it |

`tools/kfzread.py` decodes it out of a screenshot, so the reading is
mechanical: four pixels per bit and four rows tall is what makes a photograph
legible, and it was still read by eye before.

**It is MONO ONLY** — the paint is Hercules/CGA banked and is not done on VGA
at all — which suits the field machine, a 5150 on a green monitor.

    make KFZ=1
    ...freeze it, screenshot it...
    python3 tools/kfzread.py shot.png

### 29.3 What is left, and the next one-move test

A mount is `dsk_chdir` → `disk_mount` → `int 13h`, and the volume under it is
provably well-formed, so a wrong LBA computed from a bad BPB is ruled out
too. What is not ruled out is the layer below: `int 13h` is the one
unbounded thing in the path, and a controller that never raises its
completion bit is a hard freeze with no code involved.

**The test:** boot the floppy, do **not** start the FTP server and do not
load `ETHER.DRV` at all (take it out of `SYSTEM.CFG`), then mount C: from the
Disk window. If it still freezes, nothing in this session's work is
involved and this is a hard-disk-path bug that has been there all along; if
it does not, the difference is what else is resident, and that is a memory
question rather than a disk one.

**Do not "fix" this from the tree until that answer comes back.** Four
rounds of evidence say the code that keeps being blamed worked on this
machine the same afternoon.

### 29.6 THE INSTRUMENT ANSWERED, and there were two faults wearing one symptom

Two `KFZ=1` screenshots came back from the field within half an hour of each
other, both called "hard freeze". They are **not the same failure**, and the
first cell of the strip is what separates them: the field's own note — *"the
animating dots at the top completely stopped"* on one and *"the top bar is
still animating in what looks like a loop"* on the other — turned out to be
the diagnosis rather than a description.

`tools/kfzread.py` needed two fixes before it could read either. The captures
are of the monitor **window**, 2x and filtered, off a green phosphor: the
reader took the RED channel (in which the lit background is 0x39 and the whole
picture reads as black) and required each bit to be four *solid* pixels (which
a filtered edge never is). It reads the brightest channel and the middle half
of each cell now, with the threshold taken from the image.

#### The one where the dots stopped: `sch_stkdie`

```
beat F7  chain F7   sch_cur 01   SP 0314   CS:IP 97:0184   stk0 bad 00
```

...and **eight solid black bytes at framebuffer byte 20**, which is `KHB_STK`
and nothing else: `sch_stkdie`'s bar. **A task overran its 256-byte stack
slice and the kernel halted itself** (`cli`/`hlt`, the only one in the tree) —
so the timer interrupt really is gone, and every instrument with it. That is
why the dots stopped, and it is what the field has been calling a hard freeze
all along.

The arithmetic is unambiguous. `sch_stacks` is at `0x0300` in `LOW_SEG` and
slot 1 owns the first slice, `0x0300`–`0x03FF`. `SP` = `0x0314` is read in the
heartbeat block **after** `sch_isr`'s 9 pushed words and the block's own 8, so
the interrupted task's own SP was `0x0314 + 34 + 6` = `0x033C` — **196 of its
256 bytes already spent, in the package at `97xx:0184`, before the tick
arrived**. `khb_paint`'s 8 words take it to `0x0304`, and the BIOS `int 08h`
chain then runs on that same stack and goes straight through the canary.

**`SCH_STACK` = 256 was sized at 1.8× a 142-byte mark, and that mark was taken
before `ETHER.DRV` existed** (docs/KERNEL-MEMORY.md, "Task stacks"). The
projection recorded there — ~160–170 of 256 worst case on real hardware — is
now beaten by 30 bytes by the task's own frames alone. `KFZ=1` makes it 16
bytes likelier by adding `khb_paint`'s frame, but it does not *cause* it: at
196 + 40 the shipping kernel overruns too, silently, into `cli`/`hlt`.

**And it reproduces here, without the field.** `task_spawn` fills every slice
with `0xCC` under `KFZ=1` now, and `tools/stkwater.py` reads the slices back
out of `LOW_SEG`; `python3 tests/ftpd.py --kfz` drives a whole session —
connect, LIST, STOR, RETR, ABOR, a 20,000-byte upload — and reports:

```
slot 1   232 used   24 free       deepest 232 of 256 (91%)
```

Slot 1 is `ETHER.DRV`'s service worker and it is the only slice this
configuration spawns. **232 of 256 under QEMU**, which is the understating
end: SeaBIOS services its interrupt entries on a stack of its own where an
IBM ROM runs `int 08h` on the current task's, worth ~20 bytes
(docs/KERNEL-MEMORY.md) — so **~252 of 256 on the 5150**, and the field went
through the remaining four.

**This one is NOT fixed, and it is a memory decision rather than a bug fix.**
`.lowbss` has 362 bytes left in its rung and `KERN_SIZE` 1,024 under
`KERN_BUDGET`, so doubling every slice — 2,816 bytes — does not fit; the four
ways to pay for it are tabulated in docs/KERNEL-MEMORY.md under "Task stacks",
and raising `KERN_BUDGET` is a decision to take with whoever asked for the
feature (CLAUDE.md).

#### The one where the dots kept going: a livelock in `menu_bpadc`

The second capture had the thirty-second watchdog's own text line on the
glass, which is only printed by `sch_isr` — so the timer interrupt was alive
and this is a different animal entirely:

```
AT 00:9402 OWN 01 CUR 01 SP 0384 R 93E7 0003 003C M AC V 01 F 02 BK 1514
```

`00:9402` is `menu_bput`'s entry; `93E7` on the interrupted stack is the
return address of the `call menu_bput` inside `menu_bpadc`'s pad loop; `F 02`
says interrupts were enabled, so nothing is masked or wedged. **Task 1 is
spinning in the pad loop holding the drawing mutex** (`OWN 01`), and the UI
task is blocked on `gfx_lock` behind it. The strip agrees independently:
`gfx_lock_flag` 1, `gfx_lock_own` 1, `IP 9407`.

The saved `AX` two words further up the stack is `003C` — the pad target,
**60 cells** — and that is `[menu_bn]` as it was when `menu_bpadc` clamped to
it. `menu_bput` drops a cell at or past `[menu_bn]` *without advancing DI*, so
the loop only terminates while the bound it cached is still the bound the drop
rule uses. §12.8's progress widget lowers `[menu_bn]` by `FPG_CELLS` at the
next composition, and `fpg_arm` forces one — **from whichever task is writing
a file**, which needs no mutex and in this session is FTPD committing an
upload on task 0 while task 1 was already inside `menu_bar_text`.

Both halves are fixed (SPEC.md §59.7.1, §12.8.3): the loop re-reads
`[menu_bn]` every pass, so no bound can leave it spinning; and `fpg_arm`
refuses to arm while another task owns the screen, so the second painter stops
existing. This is almost certainly the *"file progress bar completes, then it
stays frozen 7–10 s, then a second write never unlocks"* the field reported
against `netbench` — that bar **is** the widget.

#### And the instrument had broken the mouse

Same round, same build: *"the mouse was not detected on this build at all"*,
and on a reboot *"sometimes it gets the mouse, sometimes not; when it does,
it's hard to move."* `khb_paint` was ~10 ms per call with `IF` clear, twice a
tick, against a 1200-baud mouse byte every 7.5 ms into a one-byte 8250 — so
packets arrived with holes and `mou_claim`'s run never completed. It composes
the row once and blits it now, ~2 ms (SPEC.md §9.6.5) — and A/B'd on MartyPC's
4.77 MHz 5150 with the period serial mouse, 60 injected packets of `dx = 5`
moved `[mouse_x]` **113 pixels of 300 before and 300 of 300 after**. Sixty-two
per cent of the hand's movement was going in the bin, which is exactly the
*"hard to move it"* half of the report.

**An instrument that changes what it measures cost this investigation a round.**

### 29.7 …and the margin is 36 bytes, measured on the machine

`tests/stackprobe` reads every slice on the shipping kernel now (SPEC.md
§8.3), so the question stopped needing a photograph. On the 5150, during a
300KB WinSCP upload with the mouse moving and keys held down:

```
High water:  146 of 256          the probe's own slice
Other tasks: 220 slot 002        FTPD's worker
```

**220 of 256 — thirty-six bytes, 1.16×**, and 208 before the keyboard was
touched, so `int 09h` nesting on the tick is worth about twelve. Many
transfers, no overflow: the driver going from 150 bytes to 118 (SPEC.md
§72.16.4) is what bought that.

**It is an observation and not a bound**, and the distinction is the whole
finding. The deepest chain a socket-using worker can take prices at ~200
before any interrupt; the 220 that was seen is a tick landing 168 bytes in.
A tick landing at the *bottom* — inside `ne_dma_write`'s byte loop, which is
also where the driver spends most of its time — puts it at 250–270.

So this note closes as a **margin decision** rather than a defect:
docs/KERNEL-MEMORY.md's table prices the alternatives, and 8 tasks × 384 bytes
costs nothing at all.

## 30 A window drag during an FTP upload kills the transfer, permanently

**Reproduced, root-caused, fixed — and it is a KERNEL bug that presents as a
network one.** Recorded here because of how it was found, not because it is
still open.

The field, on the 5150, during a 304552-byte WinSCP STOR:

> *"Dragging the window during the write was smooth on B, but it also killed
> the transfer — the file progress stops popping up, the writes stop, and the
> client eventually times out on a control connection error."*

Then, an hour later:

> *"It did not ever free up, even after 200s. Stopping then starting the ftp
> server allowed the client to reach 'pwd', but then it timed out."*

And finally the sentence that placed it:

> *"I went back to A — the unmodified one — and tested, and a drag there ALSO
> kills the transfer."*

**Two rounds were spent inside `ETHER.DRV` before that.** The bug arrived
alongside an experiment (the `ETHPUMP` pump worker, SPEC.md §72.19) and every
symptom fitted the experiment: a stalled transfer looks exactly like card-mutex
contention, and a dead listener looks exactly like a driver that has wedged.
A real defect *was* found down there on the way — the worker's frame budget
was the last frame's length rather than eight (§72.19) — which made the wrong
theory more convincing, not less, because fixing it moved the number.

The actual cause is SPEC.md §74.1.1: `ui_drag` drains every event that is not
an `EVT_MUP`, `wm_wake`'s per-slot coalescing flag stayed set with no record
behind it, and **the window never received another wake for the rest of its
life**. ftpd's worker stages and its UI task commits, so the commit that was
in flight when the drag began never happened; the worker waited on a handshake
byte that would never clear, the session was never released, and `NET_SOCKS`'s
four handles were gone — which is the "cannot reconnect".

Three things this is worth keeping for:

- **A control experiment is cheap and it was the whole answer.** One run of the
  unmodified build settled two rounds of theory. Ask for it first when a
  symptom appears next to a change.
- **A bug found while chasing another one does not confirm the theory that led
  you there.** The budget bug was real, the fix moved the throughput, and the
  drag kept killing the transfer — that gap was the evidence and it was one
  more round before it was read that way.
- **It was never FTP's, and it is not fixed only for FTP.** Any package built
  on `OSAPI_WM_WAKE` across a worker boundary was one window drag, resize,
  full-screen app or sound bracket away from the same silence.

**Confirmed on the 5150**, on the plain build with no driver worker in it:
*"Drug the window around many times, including one drag that I held for a good
10 seconds. The transfer smoothly continued, no disconnect. Dragging was just
fine. There was no drag lag, or drag slowdown."* The experiment the bug had
been attributed to was removed on the strength of that run (SPEC.md §72.19).


---

## 31. A 286 clone loads a scrambled kernel and freezes at 92% (FIXED, SPEC.md 18.93.1/18.93.2 — and the BIOS survey is recorded here, not guessed)

**Reported on 86Box `mr286` — an MR BIOS 286 clone — and on no physical
machine.** The splash drew perfectly, the bar reached 92%, and the machine then
froze, cold-reset, warm-rebooted or bootstrapped back to the splash depending on
which build was running. Four instrumented builds gave four different symptoms,
which is what a scrambled kernel looks like: the garbage differs with the image.

**Mechanism, measured rather than theorised.** `make rdiag` builds a payload the
same sector count as `KERNEL.SYS` where every sector names itself; sector 0 walks
the rest and draws a map. It reported **92 bad, first at file sector 15, each one
holding 0000** — and a simulation of `read_run` against the model "the BIOS stops
at the head boundary and answers CF = 0 for the whole request" reproduces all
three numbers exactly. So this controller **will not do the multi-track flip at
all**, and SPEC.md 18.91.1's cylinder-bounded run leaves the back half of every
crossing run *never written*. It is note 7's hazard — a short transfer taken as
a complete one — reached through a different door than note 5's wrong EOT.

Why it hid so well: the splash module is file sectors 0-8, and on a 360KB disk
the first head flip is at LBA 27, **file sector 15**. Everything the screen could
show was loaded before the first sector that could be wrong.

**Fixed** by SPEC.md 18.93.1's canary — the loader verifies the *transfer*
against a word the build reads out of the image, and repeats the load
track-bounded if it fails — and 18.93.2's XT gate, so a 286 takes the safe bound
from the start instead of paying for the discovery.

### The BIOS survey — what was actually tested

`build/rdiag360.img` on MartyPC, one boot per BIOS, 206 sectors each. The three
clone ROMs are in `tools/martypc/roms/` (untracked, for the IBM BIOS's reason)
and their machines are `os8088_compaq_revh`, `os8088_eagle_spirit` and
`os8088_columbia_mpc`.

| BIOS | class | result |
|---|---|---|
| IBM 5150 / 5160 | XT | **crosses a head correctly** |
| GLaBIOS 0.2.6 | XT | **crosses a head correctly** |
| Compaq Deskpro Rev H (106265-001, 11/10/86) | XT clone | **crosses a head correctly** |
| Eagle PC Spirit 1.9 | XT clone | **crosses a head correctly** |
| Columbia MPC 1600 3.02 REVB | XT clone | **could not be tested** — halts at 0000:0407 on MartyPC's 5160 hardware, before reaching the loader. Not a pass and not a failure |
| MR BIOS 286 (86Box `mr286`) | 286 clone | **WILL NOT CROSS A HEAD** — 92 of 206 sectors never written |

Four XT-class BIOSes pass and the one failure is a 286 — which is the evidence
18.93.2's gate rests on. It is a small sample and it is not proof: the gate is
still a bet about a population, which is why the canary runs underneath it and
can still lower the bound on an XT that turns out to be the exception.

### CONFIRMED ON THE 5150: §18.91.1 is worth 2,197 ms

**This is the number CLAUDE.md said no 5150 had ever seen.** It has now seen it.

| row | P1 cylinder | P2/P3 track | delta |
|---|---|---|---|
| **boot + early init** | **7,416** | **9,613** | **+2,197 ms** |
| clock + video + heap | 8 | 7 | |
| mouse_init | 590 | 591 / 1,195 | see below |
| desktop + drivers | 1,881 | 1,881 | 0 |
| drv_boot | 3,535 | 3,574 / 3,511 | ±60 |
| first paint | 223 | 223 | 0 |
| **TOTAL, OS only** | **13,623** | 15,875 / 16,425 | |

`boot + early init` read **9,613 ms in both track-bounded runs** — the same
figure twice, on a disk the owner describes as perfectly consistent — against
7,416 ms cylinder-bounded. So §18.91.1 saves **2.197 s** on the target machine,
which is the 2.2 s the emulators predicted and slightly more than MartyPC's
1,923 ms (−12% on the delta, against +30% on the absolute load time — the delta
travels better than the level does).

To be unambiguous about direction, because it is easy to get backwards and was:
**P1 is the FAST disk.** Its boot sector carries §18.93.2's gate, which raises
`run_max` to 18 on an 8088 — verified in the shipped bytes, not inferred from
the filename. P2 and P3 are `TRACKRUN=1`, `run_max` fixed at 9, the pre-§18.91.1
loader.

**The mouse row is not noise and not the disk.** 591 ms against 1,195 ms across
two runs of the *same* build is §9.4.5's identify window ending early or running
to its full 1,200 ms — the behaviour `MOUIDSLOW=1` exists to bracket. It accounts
for 604 of the 550 ms between the two totals; everything else is rounding.

### CORRECTION: TRACKRUN=1 was never broken — our own imaging tool wrote a bad disk

The previous revision of this note recorded that P2 "does not boot the 5150 at
all", with a theory about track-bounded runs beginning on head 1. **That was
wrong**, and the correction matters more than the theory did.

The P2 floppy had been written with **os8088's own disk imaging utility**, and
that **corrupted the disk's sector format**. DOS's `dskimage` then refused the
same floppy — *"unable to use sector 22"* — until the disk was reformatted under
DOS. Rewritten with `dskimage` onto a freshly formatted disk, **both P2 and P3
boot the 5150 to a desktop**.

So the failure I attributed to the run bound was media, and the media was damaged
by us. The instinct in that entry — *"whether the failure is repeatable at the
same point separates a logic fault from marginal media"* — was the right question
and the answer was media; I should have waited for it rather than writing the
head-1 theory up first.

**That leaves a real, unfixed bug of its own, and a worse one: os8088 can damage
a floppy's low-level format while writing an image to it.** Not a data-integrity
bug in a file — the *format*, recoverable only by reformatting under another OS.
Recorded as note 32.

### A SECOND way the same corruption came back, found while checking the gate

The owner asked the right question about the shipped fix: *"once into the OS,
on a 286+, we're not going to corrupt reads by trying to use the head switch
command on a bios like the mr286?"* — and the honest answer was **we were**,
by a different door.

The loader published its final `run_max` as a **number**, and the kernel turned
that into "may a run cross a head" by comparing it against **the mounted
volume's** SPT: greater meant crossed. That reads correctly on the volume the
loader loaded from and wrongly on any other, because the two numbers come off
different disks. A 286 boots a 1.44MB disk track-bounded and publishes **18** —
a *track* there — and then the first mount of a 360KB floppy (SPT 9) or a
17-sector hard disk makes `18 > SPT` true and switches the crossing back on. On
the MR BIOS that is the original defect exactly, arriving after the desktop is
up instead of during the load, and just as silently.

**Reproduced and fixed before shipping.** QEMU is a 286-and-up CPU, so
§18.93.2's gate takes its non-XT arm there — this is the closed list's first
row doing its job. `make test TESTAPPS=build/apps360.img` puts a 1.44MB system
disk in A: and a 360KB apps disk in B:, and reading the two words at the
desktop and again after opening `Disk B`:

| | before the fix | after |
|---|---|---|
| `boot_cylrun` at the desktop | 18 | **0** |
| `[dsk_cylrun]`, A: mounted (SPT 18) | 0 | 0 |
| `[dsk_cylrun]`, after `Disk B` mounts (SPT 9) | **1** | **0** |

The fix is that the loader now writes that word **on one path only** — the path
where a run crossed a head and the canary came back right — so zero is every
other case at once and the kernel's test is `!= 0` rather than a comparison
against a geometry. It costs the boot sector nothing: the `je` that skipped the
check now skips the publish with it. §18.93.1 carries the rule.

**What this cost is worth writing down.** The published number was a *fact about
one disk* being read as a *fact about the machine*, and it passed review twice
because on the boot volume the two readings agree. The general form: a
measurement taken on one subject and stored as a machine-wide truth needs to
say which subject it came from, or be reduced to a boolean before it is stored.

### ANSWERED: what the cylinder bound is worth, on iron

This section stood open for one round. The section above closes it: **2,197 ms
on the 5150**, from the plain-`make` against `make TRACKRUN=1` A/B it asked
for. CLAUDE.md's standing caveat on §18.91.1 — *"a 2.2 s win two emulators
agree on and no 5150 has yet seen"* — is retired, and the row now records the
measurement instead.

The 86Box figure it was reasoning from **splits cleanly**, which is worth
keeping because it is the only cross-check the two instruments allow: 86Box
showed the 5150 going ~12 s → ~9 s with the cylinder bound and §9.4.5's halved
mouse identify window *together*, ~3,000 ms for the pair. On the iron the pair
measures **2,197 ms of disk plus 604 ms of mouse = 2,801 ms**. An emulator
that models the CALL and not the REVOLUTION got the *split* right to within
7%, having got the absolute boot time wrong by 30%. That is PERFORMANCE.md
rule 5's blind spot behaving exactly as Part 4 says it does — a delta travels,
a level does not — and it is a reason to keep quoting deltas from emulators
and levels only from the field.

The separate observation is about the GATE, not the bound, and it is unchanged:
a 286 with the bad BIOS boots in ~9 s doing **24** `int 13h` calls, and the
5150 boots in ~9 s doing **13**. The 286 keeps up while paying twice the calls
— it is a faster machine and its drive is not its whole boot. That is the
evidence that §18.93.2 gives up little by taking the cylinder bound away from
286-class machines, and it says nothing against the bound itself.

---

## 32. os8088's own `Write Img...` left a floppy whose LOW-LEVEL FORMAT was damaged (OPEN — reported once, not reproduced here, and NOT reproducible on any emulator in this tree)

This came out of note 31's A/B and is the more serious of the two findings in
it. It is recorded separately because it is not a disk-run-bound bug and
nothing above it depends on it.

### The report

Verbatim, from the 5150's owner:

> I wrote P1 in dos, with dskimage. I had written the P2 disk image with
> os8088's new disk image utility - and apparently that corrupted the sector
> format on the disk. Tried to write P3 with dskimage from dos, and it was
> unable to use sector 22. Formatted the disk it in dos, wrote P3 with
> dskimage, and goes to desktop fine.

So the sequence on **one physical 360 KB floppy** was:

1. os8088's `Write Img...` (§18.99.8) wrote an image to it. It did not boot.
2. DOS's `dskimage` was then pointed at the same disk and **refused it** —
   *"unable to use sector 22"*.
3. A DOS `FORMAT` recovered the disk. `dskimage` then wrote it and it booted
   to a desktop.

### Why this is a FORMAT fault and not a data fault

Step 2 is the whole finding. Bad *data* written by us is overwritten by the
next tool that writes the disk; `dskimage` would not have noticed, and step 3
would not have needed a format. A write tool that cannot use a particular
sector number is being refused by the **ID address marks on the media** — the
sector's low-level identity, which is written by a FORMAT and never by a
write. And a plain DOS `FORMAT` put it back, which is what says the platter is
serviceable and the *format* was what was wrong.

**Severity, plainly: this is data loss on media the user did not ask us to
touch the format of.** A user who images a floppy expects that floppy's
contents replaced, not the disk rendered unusable until reformatted somewhere
else — and os8088 has no format command of its own to recover it with, so the
recovery path requires a second operating system.

### What the source says, and it does not obviously do this

Facts, from the tree rather than from reasoning:

- **os8088 never formats.** Every `int 13h` this system issues on a floppy is
  `AH = 02h` read or `AH = 03h` write — `dsk_op` is one byte and takes exactly
  those two values (`kernel/disk.inc:938/941`) — plus `AH = 00h` reset on a
  retry and `AH = 08h` geometry in `clone.inc`. There is **no `AH = 05h`
  format-track call anywhere in the tree**, and no `AH = 17h`/`18h`
  set-media-type either. *That last absence is a candidate, not an
  exoneration — see below.*
- **The geometry `Write Img...` writes with comes from the image's SIZE**
  (§18.99.8), not from the target drive, and for these disks it was right: a
  368,640-byte file is 9 SPT / 2 heads / 40 cylinders, which is what the media
  is.
- **The diskette parameter table is COPIED from the ROM's** (`dsk_dpt_init`,
  §18.92) and only byte 4 (EOT) is ever rewritten. So the step rate, head
  load/unload, gap lengths and format gap in use are *that machine's own BIOS
  values*, not guesses of ours.

### Candidates, in the order the evidence ranks them

**0. The disk was already failing, and our write was simply the first thing to
touch it.** The null hypothesis, and it is the FRONT-RUNNER: a marginal
40-year-old floppy presents exactly like this, and a reformat "fixes" one of
those too. The owner adds the fact that promotes it — *"I've written
successfully with that tool before, to this exact same disk"* — so `Write
Img...` and this platter have a working history and only this one write went
wrong. *Discriminating test:* point `Write Img...` at a **fresh, known-good,
DOS-formatted** disk and then read it with `dskimage`. If that disk survives,
this note is about one tired floppy and closes.

**1. We freeze int 1Eh at the BOOT media's parameters, and an AT-class BIOS
expects to swap that table per media.** §18.92 points `0000:0078` at
`dsk_dpt` **permanently**, on every machine. On an XT with one drive and one
media type that is exactly right and is why it has never bitten. On a BIOS
that keeps several tables and re-points the vector when it detects a different
media — 360 KB media in a 1.2 MB drive being the classic case — our takeover
**prevents the swap**, and the FDC is then handed the wrong gap length and the
wrong step behaviour for the media actually in the drive. A write under a
too-long read/write gap (byte 5) can run the write gate past the data field
and over the **following sector's ID address mark**, which is a documented µPD765
behaviour and is precisely the damage observed. *Discriminating test:* which
drive was the disk written in, and is the machine's BIOS AT-class? If it was a
1.2 MB drive, this is the leading candidate; if it was a genuine 360 KB drive
on an XT BIOS, it is close to ruled out.

**2. We never set the media type before writing** (`AH = 17h`/`18h`). DOS's
own `FORMAT` and `DISKCOPY` do, and step 6 of the seven-step path in
docs/FIELD-MACHINES.md already warns in this same area: *"It has to be a real
360 KB drive — head geometry differs between 360 KB and 1.2 MB drives, and a
360 KB disk written in a 1.2 MB drive is not reliably readable in one."* That
warning is about the *track width* a 96-tpi head writes over 48-tpi tracks,
which leaves the old ID fields partly intact under the new ones — again
exactly the symptom. This is really candidate 1's twin and the same test
separates them.

**3. Something in `clone.inc`'s window arithmetic writes off the end of a
track.** §18.99.9 rounds the transfer window down to a whole number of
cylinders using the **live** `[disk_spt] × [disk_heads]`, and `clo_span` sets
those from the image size for the duration of the write (`clone.inc:508/599`).
**Weak, and here is why**: §18.91.3 already bounds every WRITE run at the
track — the cylinder bound is reads only, measured rather than cautious — so a
clone's writes never carry the multi-track bit past a head in the first place,
whatever `clo_span` rounded the window to. What is left of this candidate is
only an arithmetic slip inside one track, and that is still worth one cheap
check: a `DISKCNT=1` clone of a 360 KB image, with the CHS of every write
logged and compared against the image's own geometry. It cannot show the
*damage* — see below — but it can show a write aimed at a sector that should
not exist.

### No emulator in this tree can reproduce it, and that is a property of the bug

86Box, QEMU and MartyPC all present a floppy as an **array of sectors**. A
write with a wrong gap length, a wrong data rate or a wrong track width lands
in the right array slot in all three and the image is correct afterwards. The
damage here is to the **flux on the platter**, and none of these model flux for
a raw `.img`. So:

- **candidate 3 can be tested here** (it is arithmetic, and a wrong CHS shows
  up in a call log);
- **candidates 1 and 2 can only be tested on iron**, and the test costs a
  floppy;
- a green `make test-full` says nothing about this note, and neither does a
  successful `Write Img...` under any emulator. Do not close this on one.

### What the next report needs to say

Batched, because each answer is a trip (docs/FIELD-MACHINES.md):

1. **Which machine and which drive** did `Write Img...` run on — 5150 #1, 5150
   #2 with the Picomem, or the writer box? A genuine 360 KB drive or a 1.2 MB
   one? This is the question that ranks candidates 1 and 2 against 0.
2. **Was the target disk freshly formatted before we wrote it**, or an old one?
3. **Does it happen again** on a known-good disk — and if it does, is it the
   same sector number each time? A repeatable sector number is arithmetic
   (candidate 3); a wandering one is the media/timing pair.

Until (1) and (3) are answered this note stays open, and **`Write Img...`
should be treated as unsafe on media anyone minds losing.**
