# Scheduler, tasks, events

Reviewer area key: `sched`. Tree: branch `the-big-review` (cut from `elendilon` at `5e4740b`), 2026-11-08.
Phase tags: **now** = no ABI break; **flagday** = batch with docs/plans/ABI-BREAK-LIST.md; **record** = worth writing down, not recommended.
Byte figures are estimates unless marked measured. `.text` and `.cold` are RESIDENT; `.ovl`/`.boot2` and module (`.modu` = CTRL.DRV) bytes are not.

## Scope read

| file | lines | read |
|---|---:|---|
| `kernel/sched.inc` | 3,092 | all of it, every proc, both arms (`KERN_SMALL` partition and `.boot2` placement; `KERN_BIG` rate hook; `KERN_MEGA` frame/stop arms; `KERN_EMU` yield/idle arms; `KFZTRACE`, `DRV_DIAG`, `NO_CHAINPRIV`, `NO_FDDPARK`) |
| `kernel/events.inc` | 354 | all of it |
| `kernel/stkdiag.inc` | 963 | all of it (`STK_DIAG` knob only) |
| supporting code | - | `ui.inc` `.idle` (1130-1190), `vga12.inc` `gfx_lock` (5082-5132), `wm.inc` `wm_wake` (2343-2380), `instance.inc` `inst_pkg_spawn` class lookup (2098-2116), `inst_seg_parked` (640-730), `inst_task_die` (1921-1946), `memory.inc` worker restart (3290-3330), `snd.inc` clip door (945-990), `fsx.inc` `fsx_rtick` (1350-1425), `mouse.inc` `mou_evfill`/`kbm_post`/`osapi_mouse_feed`/`.lowbss` (3555-3613, 3960-4045, 6615-6640), `kernel.asm` boot calls (5675-5715), `STK0_SIZE` (1568), `OVBCALL` (5449-5486), cw cells (7955-7975); every `sch_lock` writer in the tree (grep) |
| contracts | - | SPEC.md 7 (10651-10707), 7.3 (11619-11745), 8 through 8.9 (12351-13600), 10 through 10.4 (18009-18330), 12.8.5.1 (27807-27860), 20.6 worker entry (46160-46180), 34.11.3; `apps/os88api.inc` task cells and worker rules (28-40, 338-350, 1234-1262, 3940-3975, 4244-4275); docs/KERNEL-MEMORY.md "Task stacks" (580-672); docs/plans/khp1/sched.md (whole); ABI-BREAK-LIST and KERNEL-BYTE-QUEUE (grep); CODE-REVIEW-TWO-CYCLES-2026-11-01 (grep) |

## Verdict

The scheduler is the best-engineered subsystem I read in this tree, in the plain sense that its invariants are all **checked**: one frame format with one resume path, a slice layout written once and derived everywhere by macro with `%error` guards, a canary compared on every switch including task 0's, an unconditional 0xCC high-water fill, a death panel that names the culprit, a guarded `hlt` with two separately-reasoned lost-wakeup guards, exact 32-bit PIT accounting, and a ROM-chain private stack that survives BIOSes that hop stacks. The defects I found are small: one IF=0 window that grew unnoticed (`task_spawn`'s fill, ~676 us on a 384 slice), one contract leak of IF=1 on a `kern_big` skip path, and a measurement bug in the `STKDIAG` instrument that sizes the private stacks. The real weakness is **prose**: about forty statements in comments, SPEC.md, `os88api.inc` and KERNEL-MEMORY.md describe designs that have since been replaced (the multiply-and-shift canary, 7 slots on kern_small, "nothing clears .bss", `DS = ES = CS = KERNEL_SEG` for workers, "exactly two" `sch_lock` raisers). The one systemic gap against prior art is that a task has **no way to block on anything but time**: every wait in the system is a sleep-poll or a yield-spin, and the UI task's private wake (`sch_wake_ui`) is the only event-driven wake there is. Generalising it to a per-task wake with a sticky pending bit (AmigaOS `Signal`/`Wait`, System V `sleep`/`wakeup`) is ~25 resident bytes and an additive cell, and it is the cheapest change that would make drivers, workers and the lock event-driven rather than polled.

## Strengths

1. **One frame, one resume path, one frame builder.** `sch_isr`, `task_yield` and `task_spawn` all produce the 24-byte frame `sch_resume` (sched.inc:2549-2559) consumes, and `sch_frame` (1075-1107) is the single body both `task_spawn` and the compactor's worker restart (memory.inc:3324) call - "a frame the scheduler resumes is not a structure it validates". Copy this anywhere two paths must agree byte-for-byte.
2. **The slice layout is data, written once.** `SCH_PARTITION` (198-235) expands three times - into the counters/asserts (239-269), `sch_stkbase` (2822-2830) and `sch_stksize` (2851-2857) - with ascending-order, evenness, minimum-size, count and total guards. First fit is smallest fit by construction. This is the pattern every hand-mirrored table in the kernel should follow.
3. **Thin stacks are checked, not trusted.** Canary on every switch away including task 0 (2445-2449, seed at 366-372), unconditional 0xCC fill at spawn (983-997), and a death panel that moves off the dead stack first and prints slot, SP and instance name (2686-2724). A freeze became a sentence.
4. **The idle path is race-correct twice.** `sch_wake_ui`'s one-word store (700-725) feeds both the UI task's `.idle` guard (ui.inc:1169-1185) and the idle task's halt guard (752-789), each test-halt-clear in one IF=0 window with STI's shadow covering `hlt`; the IRQ0-masked fail-safe (820-822) keeps a broken machine spinning rather than hung. SPEC.md 8.1.2.x is a model of how to write down a concurrency argument.
5. **Exact accounting at negligible cost.** `sch_account` (1272-1320) builds a 32-bit `[ticks]:phase` stamp with the IRR coherence test, charges only on a real change (2534-2541), and pauses itself under sub-ticks rather than lying.
6. **The ROM is fenced.** The chain runs on `sch_chstack` (2186-2238) only when SS is ours; `sch_switch` refuses to park a foreign SS (2417-2425); a nested tick accounts and wakes but never switches (2270, 2382). This is the right paranoia for an OS that calls `int 13h`/`int 16h` with IF=1 on unknown BIOSes.
7. **Mode switch with no rendezvous.** Pre-emptive and cooperative share all state; the watchdog (2390-2406) re-arms on every entry to `sch_switch`. No historical peer let the user flip this at run time.
8. **The event ring's policy is right and documented.** Drop-oldest with the WAKE exception (events.inc:95-116), bounded drain per pass (SPEC.md 10.2), coalesced wakes with their own full-ring refusal (wm.inc:2352-2353), the wheel as a level (10.4). Mac OS's `PostEvent` also dropped the oldest; os8088 adds the promise exception Mac never needed.
9. **Multi-byte stores are guarded where they are spelled.** Every "two bytes as one word" trick (`sch_uiwake`/`sch_idlewake`, `sch_idleslot`/`sch_top`, `sch_rbusy`/`sch_lock`, `sch_fast`/`sch_fcnt`, `T_INST`/`T_FLAGS`, `spl_busy`/`spl_quiet`) has an `%error` beside the declaration.

## Findings

Ranked by value. Evidence is `file:line`.

### sched-1 - `task_spawn` holds IF=0 across the whole 0xCC fill (hazard, medium-low, now, +8)

`task_spawn` takes `cli` at sched.inc:914-915 and the 0xCC `rep stosw` of the whole slice (983-989) runs inside it, up to 384 bytes. PERFORMANCE.md's measured RAM `rep stosw` is 1.76 us/byte, so a 384 slice is **~676 us at IF=0** (338 us for 192), plus the ~30 stores the comment counts. The comment at 882-883 and SPEC.md 8 (12446) still say "~30 stores with no I/O, shorter than `task_exit`'s" - the fill was added inside the window when it stopped being `%ifdef KFZTRACE`, and nobody re-priced the window. 676 us is under a 1200-baud mouse byte (7.5 ms) but it is ~4 pulses of a 5.5 kHz speaker-sample ISR (SPEC.md 34.11.3 counts 864-cycle IF=0 stretches as lost pulses) and two-thirds of a 9600-baud byte. Spawns happen at app launch and at every SB stream open (the transient refill/drain tasks, SPEC.md 34.5), i.e. while sound is running.

**Recommendation.** Reserve the slot under the cli and fill outside it: store a reserved state (`T_STATE = 3`, which neither the wake loop nor the scan treats as live-ready, and which `task_exit`'s bound walk and every `T_STATE != 0` reader treat as occupied), `popf`/`pushf` to drop back to the caller's IF, fill and build the frame, then `cli` for the `sch_top` raise and the publish. ~+8 bytes resident (the state store, `popf`+`pushf`, a `cli`; zero `T_FLAGS` in the reserving store so `inst_seg_parked`'s walk cannot read a stale `TF_SERVICE`). Fix the two "~30 stores" sentences either way.

### sched-2 - `sch_rhook` returns IF=1 on its skip path under a sample ISR (bug, low, kern_big, now, 0)

`sch_rhook` (sched.inc:595-627) does `sti` at 598 when `[spk_seg]` is open, *before* its three skip tests (601-609). The taken path re-`cli`s at 624; the skip path `ret`s at 627 with IF=1, against its own header ("out: ... IF=0") and `sch_isr`'s ("Runs with IF=0 throughout ... never sti", 1435). On the `.full` call (2240-2243) the rest of that tick - `sch_account`, `snd_tick` and with it `DSV_TICK` (which SPEC.md 8.7.5 says runs "inside IRQ 0 at IF = 0"), the wake loop - then runs interruptible. I checked whether this reaches `sch_switch`: it does not - each skip reason independently refuses the switch (`[sch_chbusy]` → `.tres` at 2382; `[sch_rbusy]` implies the hook raised `[sch_lock]` → `.tres` at 2388; foreign SS → `sch_switch`'s own test at 2417). What remains is extra nesting depth on `sch_chstack` in the nested case and a driver tick entered at the wrong IF.

**Recommendation.** Move the `cli` below the label: `KDS / sub word [sch_rbusy], 0x0101 / .skip: cli / ret`. The `sub` is one read-modify-write instruction and cannot be torn by an interrupt on one CPU. 0 bytes.

### sched-3 - `sch_lock` is now a general "Forbid", and yield/sleep/lock silently degrade under it (hazard, low, now, 0 or +7)

SPEC.md 7 (10687) says "exactly **two** routines raise it". The tree has `sch_lock` raised in disk.inc (888, 1462, 2035, 6405, 6458), memory.inc (4040, 4103), xmem.inc (640, 662), fsx.inc (2244), diskw.inc (6347), hiber.inc (1843, 3039, 5205), megados.inc (302) and snd.inc (955). Under it `task_yield` self-resumes (1881-1882), so: a `gfx_lock` whose owner is another task spins for ever (vga12.inc:5098 yields, nothing else can run); and `task_sleep` (1151-1164) stores `T_STATE = 2` and returns immediately, leaving a RUNNING task marked sleeping - its sleep then takes effect at the next unlocked switch instead (harmless only because the deadline has usually passed by then and the wake loop runs before the switch). memory.inc:4101-4103 dropping the lock around a park shows the authors know; nothing enforces it. AmigaOS's `Forbid()` had the same counter and made `Wait()` inside it well-defined by saving the nest count per task.

**Recommendation.** Now: rewrite SPEC.md 7's paragraph to say what `sch_lock` has become (a nestable pre-emption-off counter with a list of holders) and state the rule beside it: *no code inside it may call anything that yields, sleeps or takes `gfx_lock`*. Optionally make `task_sleep` honest under the lock (`cmp byte [sch_lock], 0 / jne .out` before the state store, +7 resident) so the sleep is a no-op rather than deferred. A `KFZTRACE`-style knob assertion in `task_yield`'s locked arm would catch the gfx-lock livelock at the site.

### sched-4 - `OSAPI_TASK_SLEEP` on the UI task ends at the next input, undocumented (hazard, low, now, 0)

`sch_wake_ui` stores `T_STATE = 1` unconditionally (sched.inc:724) on every queued event, mouse motion and keystroke (SPEC.md 8.1.2.2). The UI task's only deliberate sleep is `.idle`'s one tick (ui.inc:1173-1174), which wants exactly that - but any window callback (W_PAINT, W_ONKEY, AM_ONCMD ...) runs on the UI task, and a callback that calls `OSAPI_TASK_SLEEP` for a timed effect is cut short by the next mouse byte. The cell's prose (os88api.inc:344) says only "AX = ticks to sleep".

**Recommendation.** Document it on the cell: "on the UI task (every window callback) the sleep ends at the first input; loop on `OSAPI_GET_TICKS` if you need a minimum". If a fix is ever wanted, give `.idle`'s block its own state value so `sch_wake_ui` only promotes that one (~+8 resident) - but nothing in the tree is known to depend on it.

### sched-5 - `task_sleep`'s range is half a word and unstated (hazard, low, now, 0)

The wake test is a sign test, `cmp dx, [bx+T_WAKE] / js .next` (2365-2366), so a sleep of 0x8000..0xFFFF ticks is "already due" and wakes at the next tick; `AX = 0` sleeps to the next tick rather than not at all. Neither os88api.inc:344 nor SPEC.md 8's `task_sleep` bullet says so. 32,767 ticks is 30 minutes - a package that sleeps "an hour" gets 55 ms.

**Recommendation.** State the range (1..32767; 0 = to the next tick) on the cell and in SPEC.md 8. 0 bytes.

### sched-6 - `evq_push` wakes the UI task before the record is in the ring (hazard, low, now, 0)

events.inc:84 calls `sch_wake_ui` before the `pushf`/`cli` at 92-93. Every producer today is safe - `mou_evfill` runs in the ISR at IF=0, `wm_wake` calls it under its own `cli` (wm.inc:2345-2359), and `kbm_post` runs on the UI task itself - but the routine's contract ("ISR-safe", any caller) invites a task-context caller with IF=1, and for that caller a pre-emption between the wake and the copy lets the UI task spend `sch_uiwake` on an empty ring and go back to sleep with the event about to land: one tick of latency, the exact failure SPEC.md 8.1.2.3 exists to prevent.

**Recommendation.** Move the `call sch_wake_ui` to just after the `cli`. 0 bytes.

### sched-7 - The two private ISR stacks have no canary and sit back to back (hazard, low, now, +10)

`sch_chstack` (sched.inc:2937) hosts code the kernel does not own - the ROM's `int 08h`, its `int 1Ch` chain, everything that nests in the ROM's `sti` window (int 09h with the BIOS keyboard handler, the floppy completion, a mouse gate frame) and, under SPEC.md 96, any `int 1Ch` hook a DOS program installs, whenever the tick lands while SS = LOW_SEG. SCH_CHSTK's sizing (80-89) covers measured ROMs only. Include order (kernel.asm:7189-7212) puts mouse.inc's `.lowbss` - `mou_pstack` (mouse.inc:6635) - directly below it, so an overflow of the chain stack writes the top of the mouse ISR's stack, which is live exactly when a mouse IRQ nests in the ROM's window. Neither stack has a canary; the task slices all do.

**Recommendation.** Seed `SCH_MAGIC` at `sch_chstack`'s bottom word in `sched_init` (boot overlay, free) and compare it in `sch_switch` beside the outgoing task's canary (one `cmp word [ss:sch_chstack], SCH_MAGIC / jne sch_stkdie`, ~8 resident) - the panel would name the interrupted task, which is the right first suspect. `mou_pstack` is the mouse reviewer's to guard the same way.

### sched-8 - `STKDIAG`'s private-stack high waters scan from the wrong end (bug, low, knob-only, now, 0)

`sd_cwater` and `sd_mwater` (stkdiag.inc:303-329, 363-389) scan DOWN from the top for the first byte still equal to the 0xE5 sentinel. Any byte written during the deepest excursion whose value happens to be 0xE5 - an IP, a segment, a flags image - stops the scan early and under-reports; with ~56 used bytes the chance that some byte is 0xE5 is roughly 20%. `sd_water` (230-273) scans UP from the base for the first byte that is NOT the fill, which is robust (it can be wrong only by the deepest byte itself). These two numbers are what size `SCH_CHSTK` and `MOU_PSTK` (SPEC.md 8.5, 9.10).

**Recommendation.** Scan bottom-up like `sd_water` (`cld / repe scasb` from the base; depth = top - first non-sentinel). Same bytes. Re-take the field numbers that sized the two stacks if any came from this panel.

### sched-9 - The death panel trusts the table it calls suspect (weakness, low, record, +4)

`sch_diepanel` reads `T_INST` (2695) and, unless 0xFF, calls `inst_ptr` and prints `I_NAME` (2716-2721) with no bound on the index; its own header says every record is suspect after an overrun. An out-of-range index prints from beyond `inst_tab` until a NUL. It also draws into whatever video mode is live, including an fsx bracket's foreign mode.

**Recommendation.** Record. If touched, `cmp al, INST_MAX / jae .name` (with SI = `sch_s_ui`) is +4 resident.

### sched-10 - Comment rot is the area's real defect (weakness, medium, now, 0)

sched.inc is 3,092 lines for roughly 700 of code. Its comments carry history ("it was X until size pass N"), and history goes stale: this review found fourteen statements in sched.inc alone that describe replaced designs (see Doc/code mismatches: the multiply-and-shift canary, `sch_stacks + slot * SCH_STACK` in three places, the 256 byte-swap path "still there", kmain line numbers, the `.boot2` claims for an `.ovl` body, the orphaned `sch_switch` header sitting after `sch_clsbytes` at 2883, a dangling fragment at 2953). stkdiag.inc carries headers for two routines (`sd_pre08`/`sd_post08`, `sd_victim`) that no longer exist.

**Recommendation.** One pass that deletes history from code comments (SPEC.md and the plans hold it) and keeps the invariant and the reason. Move the `sch_switch` header above `sch_switch:`. 0 bytes.

### sched-11 - `fsx_rtick` is a third copy of the chain swap and the hook's guards (consistency, low, now, 0)

`fsx_rtick` (fsx.inc:1364-1425, kern_mega real-mode bracket) re-spells `sch_isr`'s chain-swap (sched.inc:2186-2238) and `sch_rhook`'s skip tests (595-627), including the same `sti`-before-the-tests shape. A fix to one (sched-2, sched-7) does not reach the others.

**Recommendation.** One `%macro SCH_CHAIN` for the swap/chain/restore and one for the hook guard, used at both sites. 0 bytes.

### sched-12 - `sched_init`'s zero stores rest on a contradiction nobody has resolved (weakness, low, now, -~30 .ovl)

sched.inc:341-346, 359-365 and SPEC.md 8 (12399-12402, 12869) say "nothing clears .bss at boot". events.inc:61-65, blank.inc:120-122, dock.inc:140, SPEC.md 10 and SPEC.md 12.8.5.1 say `.bss` *arrives zeroed* because the image's `.text`-to-`.cold` gap is emitted as padding and stage 2 lands it. Both cannot be the rule. If the second is true, the stores of 0 in `sched_init` (`ticks`, `sch_lock`/`sch_rbusy`, `sch_fast`, `sch_coop`, `sch_wd_hits`, the `T_STATE` loop, the `sch_cycles` loop) are ~30 bytes of the kern_big boot-overlay window, which khp1 found at 5,117 of 5,120.

**Recommendation.** Rule once in SPEC.md 2.6 (I believe the "arrives zeroed" reading, which 12.8.5.1 measured; `.lowbss` is the section that genuinely is not). Then either keep the stores as deliberate belt-and-braces and fix their comments, or drop them and give the overlay its room. Not resident either way.

### sched-13 - `evq_pop` banks DS where a prefix is as small and faster (perf, low, now, 0)

events.inc:264-271 does `push ds / push ss / pop ds / 4 x movsw / pop ds` "because the 8086 LOSES the prefix when a string op restarts after an interrupt". That bug is a property of a REP-prefixed string instruction with a segment override; four unrepeated `movsw` cannot be interrupted mid-instruction. Four `ss movsw` are the same 4 extra bytes and ~35 clocks cheaper on the 8088 (one fetch-floor byte each against two pushes and two pops). It runs once per popped event and per drain step.

**Recommendation.** Either take the prefix form (0 bytes, ~35 clocks a pop) or keep the banking and correct the reason; SPEC.md 10.1.1 (18180-18185) repeats it and also still says `rep movsw`.

### sched-14 - `sch_clsbytes` row 0 duplicates row 4 (size, low, now, -1)

`sch_clsbytes` index 0 and 4 are both `SCH_STACK` (2873, 2877), and `inst_pkg_spawn` already turns an out-of-range index into CX = 0, task_spawn's "largest" (instance.inc:2106-2111). A `dec bx` before the bound check (0 wraps to 0xFFFF, `jae` → CX = 0) drops the 2-byte row for a 1-byte instruction. -1 resident. Trivial; take it only if the file is open anyway.

### sched-15 - There is no wait-on-anything primitive (rethink, medium, now, +24..+30)

A task can block on TIME (`task_sleep`) and nothing else. Every other wait polls: the SB refill task wakes every tick to look (drivers/sound/sb.inc:2539-2542, "one-tick pacing"), `gfx_lock` waiters are READY and spin through `task_yield` (vga12.inc:5091-5104), a worker waiting for its UI half polls `OSAPI_TASK_ALIVE`, and `sch_wake_ui` (700-725) is a one-task special case of the missing primitive. On an 8088 a poll that finds nothing still costs a switch in and out.

**Recommendation.** `task_wake(AL = slot)`: if `T_STATE` is 2, make it 1 and post `[sch_idlewake]`; otherwise set a sticky wake-pending bit in `T_FLAGS` (bit 1); `task_sleep` tests-and-clears that bit under its own `cli` and returns at once if it was set (the lost-wakeup guard `.idle` already does by hand). ISR-safe, one byte per task, no record growth. ~+24..30 resident plus a rare 6-byte OSAPI cell appended at the end (additive: `abilock --update` without a `PKG_FMT` bump). This is AmigaOS's `Signal`/`Wait` with one signal bit, or System V's `sleep`/`wakeup` with the slot as the channel. It enables, later and separately: refill driven by the SB IRQ instead of the tick, ETHER.DRV's service worker woken by the NIC IRQ, and sched-16/sched-17.

### sched-16 - A UI wake gets no priority (rethink, low-medium, now, +~22)

`sch_switch` scans round-robin from `cur+1` (2477-2502). When an input wakes the UI task while two or more workers are runnable, the UI waits for the next tick *and* for every ready worker between `cur` and slot 0 in scan order: up to N x 55 ms. SPEC.md 7.3 fixed the gfx-lock half of starvation and explicitly left "the UI task does not run more often" to `QUANTUM=`. Every comparable system boosts the interactive thread - OS/2 1.x's foreground/IO boosts, GEOS's UI thread above application threads, AmigaOS's input handler at priority 20.

**Recommendation.** A one-shot boost: `sch_wake_ui` sets `[sch_uiboost]` only when task 0 was SLEEPING (`T_STATE == 2`), and `sch_switch` spends it by picking slot 0 directly when it is set, task 0 is READY and `[fsx_task]` is 0xFF. One boost per sleep-to-wake transition bounds the starvation of workers to one UI pass per input. ~+22 resident on the switch path (~+8 clocks a switch when clear). Measure first on MartyPC with two drawing workers (Fractal + Wire) - press-to-menu, as SPEC.md 7.3's breakpoint method does - and take it only if the number moves.

### sched-17 - `gfx_lock` waiters poll rather than block (rethink, low, record, +~12 after sched-15)

A blocked `gfx_lock` waiter is READY and is switched in every round only to yield again (vga12.inc:5091-5104); with the idle task in place that is a ~2-switch cost per waiter per tick for as long as the lock is held. With sched-15, `.block` could `task_sleep` a long time with the pending bit armed and `gfx_unlock` could `task_wake` a recorded waiter - Win16Mutex/OS/2 semaphore semantics. The §7.3 fairness handover would need re-proving. Record until sched-15 exists and a measurement shows the spin matters.

### sched-18 - A dying task cannot be holding `sch_lock`, and nothing says so (hazard, low, record, 0)

`task_exit` (1189-1211) jumps into `sch_switch` without looking at `[sch_lock]`; the switch it makes is forced, and the next task then runs with pre-emption off for the rest of the session and every `task_yield` a no-op. `inst_task_die` (instance.inc:1921-1946) is the normal road in and does not hold it, so this is a contract to write down ("a task may not exit with `sch_lock` raised") rather than code to add.

### sched-19 - 16-bit ticks bound every deadline at 30 minutes (rethink, low, flagday, +~8)

`[ticks]` is a word (2976), `T_WAKE` a word, `OSAPI_GET_TICKS` answers AX alone. Mac `Ticks`, Win16 `GetTickCount` and AmigaOS's `timer.device` were all 32-bit; os8088 packages that need long intervals read the RTC ladder instead. A high word incremented on the carry is +2 `.bss` and ~+6 code; answering DX from `OSAPI_GET_TICKS` changes a register the cell may today preserve. Record for the flag day only if a package asks.

## Rethinks

* **Event-driven waits (sched-15)** are the one change of model worth making. Everything else here is the right shape for an 8088: a fixed partition beats a stack allocator on 128 KB, round-robin beats a priority queue at 14 slots, and the run-time mode switch is a feature nobody else had.
* **A one-shot UI boost (sched-16)** is the cheap form of priority scheduling. A full priority field is not recommended: it costs a byte per task, a second scan key and an inversion story, and 14 slots do not need it.
* **The `sch_lock` model (sched-3)**: os8088's counter is AmigaOS `Forbid()` without the per-task save that makes `Wait()` legal inside it. Copying the save (a byte per task, saved/restored in `sch_switch`) is not worth ~12 hot-path bytes; writing the rule down is.
* **Deferred work at a secondary interrupt level** (Mac System 7's Deferred Task Manager): khp1 found 85% of a mouse packet's road is `cur_move` inside the ISR at IF=0. A deferred-task queue run on the way out of an ISR with IF=1 would shorten every IF=0 window the serial mouse imposes. That is the mouse reviewer's call; recorded here because the scheduler would own the queue.

## Prior art

| system | model | what it did better | what os8088 does better | cheap to adopt |
|---|---|---|---|---|
| **DOS** | no scheduler; `int 28h` idle, `int 1Ch` user tick, InDOS/critical-error flags | nothing at the model level | everything; the `int 28h` idea is os8088's idle task done properly | - |
| **Windows 3.0/3.1** | Win16 tasks cooperative (`GetMessage`/`Yield`), one system queue of 120 messages, `WM_MOUSEMOVE` coalesced, `WM_PAINT`/`WM_TIMER` synthesised when the queue is empty; Win386 time-slices DOS VMs with foreground/background priorities and an "Exclusive" PIF flag | coalescing of motion in the queue; low-priority synthesised messages | pre-emption; the fsx freeze is Win386's Exclusive mode done per bracket; drop-oldest instead of a full-queue beep | the synthesised-when-idle idea is already `ui_post`/damage |
| **OS/2 1.x** | pre-emptive threads, priority classes (idle / regular / time-critical) with dynamic foreground and I/O-completion boosts; semaphores, `DosSleep`, `DosSemWait` | blocking on objects; boosting the interactive thread | footprint; canaries and a death panel where OS/2 trapped | sched-15 (block on a signal), sched-16 (one-shot boost) |
| **Mac System 5-7** | cooperative MultiFinder, `WaitNextEvent(sleep, mouseRgn)`, OS event queue that drops the oldest, VBL tasks, Time Manager, Deferred Task Manager | interrupt-level task queues (VBL / deferred) as a published facility | pre-emption, accounting | a deferred-task level (rethinks, mouse area) |
| **GEOS** (8088 GUI) | pre-emptive threads with base priorities, the UI thread above application threads; per-thread message queues; semaphores; thread stacks in heap blocks | UI thread priority; message passing between threads | the fixed partition is cheaper than heap stacks on 128 KB and cannot fragment; exact per-task accounting | sched-16 |
| **AmigaOS Exec** | pre-emptive priority round-robin, 32 signal bits per task (`Signal`/`Wait`), `Forbid`/`Permit` counter saved per task, `Disable`/`Enable` | signals are the cheapest general wait there is; `Wait()` inside `Forbid()` is defined | runs in 128 KB on an 8088 | sched-15 is one signal bit |
| **Unix System V** | priority with CPU-usage decay; `sleep(chan)`/`wakeup(chan)` on an address | wait channels: any address is a wait object | determinism; no decay arithmetic on the switch path | sched-15's slot-as-channel |

## Doc/code mismatches

Quotes are exact. Kernel line numbers are this tree's.

1. **sched.inc:18** - "SCH_STACK-byte stack in .lowbss." Slices are per class (`SCH_PARTITION`, 198-235). Proposed: "a stack from SCH_PARTITION's fixed list (128..SCH_STACK bytes) in .lowbss."
2. **sched.inc:113** - "still there for 256 and there is a general" (the SCH_STACK note claims the 256 byte-swap path and a multiple-of-128 path still exist). The base is a table lookup (2445-2447). Proposed: "It used to have to be 256 exactly; the base is now looked up in sch_stkbase (SPEC.md 8.6), so changing SCH_PARTITION is the whole of the change."
3. **sched.inc:121** (an `%error` message) - "every class is one". 192 is not a multiple of 128 (220-225). The constraint is vestigial. Proposed: drop the `%if`, or reword to "SCH_STACK must be even and at least SCH_FRAME + 2" (which SCH_SLOT already checks).
4. **sched.inc:176** - "832 bytes against kern_big's 2,816." The kern_small partition (199-203) is 128 + 192 + 256 + 384 = 960. Proposed: "960 bytes against kern_big's 2,816."
5. **sched.inc:288** - "kmain:4170 is its ONE caller"; **sched.inc:805** - "One caller, kmain:4182". The calls are kernel.asm:5686 and 5698. Proposed: "kmain's OVBCALL is its ONE caller" / "One caller, kmain's OVLCALL" (no line numbers).
6. **sched.inc:296** - "In `.boot2` CS is the BLOB's". On kern_big the body is `.ovl` (302), entered `call FAT_SEG:` (kernel.asm:5450). Proposed: "In `.ovl` CS is FAT_SEG's and in `.boot2` the BLOB's, and both are about to become somebody's memory".
7. **sched.inc:808** - "of a `.text` label from `.boot2` is correct". `sch_idle_start` is `section .ovl` on both arms (815). Proposed: "Taking the address of a `.text` label from `.ovl` is correct and taking the address of an `.ovl` one would not be". The block also lacks the routine's name line: it begins at 802 as if it continued `sch_idle_body`.
8. **sched.inc:477** - "sched_init's seed at the top of this file is `.ovl`, the boot overlay," - on kern_small it is `.boot2` (305). Proposed: "is boot-overlay code (`.ovl` on kern_big, `.boot2` on kern_small),".
9. **sched.inc:882** - "The window is ~30 stores with no I/O, shorter than" - the window includes the slice's `rep stosw` (983-989), ~676 us at 384 bytes. Proposed: "The window is the slice's 0xCC fill plus ~30 stores, no I/O - ~0.7 ms on a 384 slice (sched-1)".
10. **sched.inc:1373** - "(see task_yield above)" - `task_yield` is at 1809, below. Proposed: "(see task_yield below)". The same paragraph's "freeze every Timer and Bounce instance" predates SPEC.md 14.7/14.8; "freeze every worker" is current.
11. **sched.inc:1435** - "IF=0 throughout; calls no BIOS service beyond the chain; never sti." On kern_big `sch_rhook` sti's under a sample ISR (598) from both call sites. Proposed: "IF=0 throughout except inside sch_rhook under a sample ISR (SPEC.md 34.11.3), where the hook runs at IF=1".
12. **sched.inc:1515** and **2920** - "sch_stacks + slot * SCH_STACK, never from" / "sch_stacks + slot * SCH_STACK -". The KFZ sampler and every consumer use `sch_stkbase`/`sch_stksize` (1970-1971). Proposed: "derived from sch_stkbase[slot] + sch_stksize[slot], never from SP's low bits".
13. **sched.inc:2874** - "1: a sleeper. The idle task, a C package" - contradicts SCH_PARTITION's own note (215-219: a C package whose worker only sleeps asks for 256) and SCH_DRV_STK (139: the driver stream task asks 192). Proposed: "1: a sleeper - the idle task, Bounce, a close watcher or a caret blink".
14. **sched.inc:2888** - "(falling back to the current task if nothing is ready)" - the fallback is the idle task (2510-2513); and "RE-ARMING sch_hold is the FIRST thing done here" - the SS test is first (2417-2419). The block also sits after `sch_clsbytes`, 470 lines below `sch_switch:`. Proposed: move it above `sch_switch:` and read "(falling back to the idle task, or before it exists to the outgoing task)" and "RE-ARMING sch_hold is the first thing done once SS is known to be ours".
15. **sched.inc:2953** - "of .lowbss (font_glyphs precedes" - a dangling fragment between two sentences. Proposed: delete the two-line fragment.
16. **events.inc:5** - "records in .bss." The ring is `.lowbss` (320-342). Proposed: "records in .lowbss (its three indices in .bss)."
17. **events.inc:17** - "payload c (unused so far)" - EVT_MDOWN/MUP/RDOWN carry the birth tick there (20-22, mouse.inc:3574-3575). Proposed: "payload c (mouse events: birth tick)".
18. **events.inc:78** - "out: nothing; drops the event silently when the queue is full" - a full ring drops the OLDEST unless it is a WAKE (95-116). Proposed: "out: nothing. A full ring drops its oldest record, or this one when the oldest is an EVT_WAKE (SPEC.md 10.1); no caller is told".
19. **events.inc:267** - "LOSES the prefix when a string op restarts" - there is no REP here, so no restart (sched-13). Proposed: "DS is banked rather than four `ss` prefixes: the same bytes, and one rule for both copies" (or switch to the prefix form).
20. **stkdiag.inc:35** - "two fewer worker slots than a" - a shipped kernel also has the idle task; STKDIAG costs one slot. Proposed: "one fewer worker slot than a shipped one (this one's)".
21. **stkdiag.inc:159** - "sch_switch's arithmetic, in one place that three readers here share." It is a table lookup (178-181). Proposed: "sch_stkbase's lookup, in one place the three readers here share." And **stkdiag.inc:168** "is STK0's 1,024 bytes" - `STK0_SIZE` is 512 (kernel.asm:1568). Proposed: "is STK0's 512 bytes".
22. **stkdiag.inc:196** - "; sd_pre08 / sd_post08 - what the ROM's int 08h chain costs a task stack" and **451** "; sd_victim - a task that exists to be interrupted" - neither routine exists. **416/418** - "THE FLOOR" / "0 if the victim was refused a slot": the floor is `sd_iwater`'s and there is no victim. Proposed: delete the two dead headers; sd_vwater's becomes "the deepest ANY slice has been (the panel's own painter included); out: AX = bytes".
23. **SPEC.md:10687** - "exactly **two** routines raise it" - see sched-3's list. Proposed: "it is raised as a nestable pre-emption-off bracket - across int 13h (§18), the FAT-window walk, the compactor's plan, XMS moves, the speaker clip (§34.4), hibernation and kern_mega's pool pack - all with the same meaning ... Nothing inside it may yield, sleep or take gfx_lock: task_yield self-resumes there."
24. **SPEC.md:12353** - "**7 on `kern_small`**" - sched.inc:47 is 5. Proposed: "**5 on `kern_small`**".
25. **SPEC.md:12372-12374** - "one 8-bit multiply and a" ... "with the old byte swap kept under `%if SCH_STACK == 256`" - it is an index into `sch_stkbase` (sched.inc:2445-2449). Proposed: "`sch_switch` compares the outgoing task's canary through `sch_stkbase` on every switch away (§8.6)".
26. **SPEC.md:12379** - "BIOS lands interrupt frames QEMU's SeaBIOS services on an internal stack" - disproved (SeaBIOS's chain is 56 bytes on our stack, stkdiag.inc:19-20, KERNEL-MEMORY.md), and since §8.5/§9.10 neither the chain nor the mouse lands on a slice. Proposed: "re-measures the margin on real hardware; since §8.5 and §9.10 what lands on a slice is the task's own chain, the gate frame, sch_isr's frame and the tick's driver calls".
27. **SPEC.md:12401** - "bytes on disk and the boot sector does not blank the region)" (and **12869** "nothing clears .bss at boot, §8") - contradicts SPEC.md 10 and 12.8.5.1 (`.bss` arrives zeroed). Needs one ruling (sched-12). Proposed, if 12.8.5.1 is right: "`.bss` arrives as the image's zero padding (§2.6), so these stores are belt-and-braces; a byte whose resting value is NOT zero is the case that needs one".
28. **SPEC.md:12446** - "~30 stores with no I/O, shorter than `task_exit`'s." Proposed: "the slice's 0xCC fill (one `rep stosw`, ~0.7 ms at 384 bytes) plus ~30 stores, no I/O".
29. **SPEC.md:12462-12464** - "the resume-the-outgoing-task fallback cannot pick a dead task because it is only reachable when nothing is ready and **task 0 (the UI task) never sleeps and never exits**" - the UI task sleeps (UIBLOCK); the fallback is the idle task. Proposed: "the fallback cannot pick a dead task because it picks `[sch_idleslot]`, which is always READY and never exits; the resume-the-outgoing arm survives only before `sch_idle_start`, when nothing can have exited". The same section omits that `task_exit` now lowers `[sch_top]` (§8.1.3).
30. **SPEC.md:12471** - "returns later with all registers preserved, IF=1.)" - the frame banks the caller's FLAGS (sched.inc:1855), and `ui_task`'s `.idle` relies on getting IF=0 back. Proposed: "returns later with all registers and the caller's IF preserved".
31. **SPEC.md:12517** - "routine, in BX, because CX carries the latched phase further down" and "**Clobbers AX, BX, CX, DX and SI on both arms**" - the index goes through AX into SI (sched.inc:1273-1277) and the early-out leaves BX untouched (1249-1251). Proposed: "computed at the head, through AX into SI ... Clobbers AX, CX, DX and SI on both arms and BX on the charging arm".
32. **SPEC.md:12634** - "The scan runs `MAX_TASKS` iterations" - it runs `[sch_top]` (§8.1.3). Proposed: "The scan runs `[sch_top]` iterations".
33. **SPEC.md:12939** - "Both routines must sit **outside**" ... "put them right after `task_cycles`" (and the table's `sched_mode_set` "Callable from any task at any time") - `sched_mode_set` is `cp_schset` in CTRL.DRV (`section .modu`, sched.inc:1392); the boot blob writes the two stores itself; `sched_mode_get` is far-entered and ends `retf` (1424-1426). Proposed: "`cp_schset` (CTRL.DRV) and `sched_mode_get` (resident, far-entered) ..." and drop the placement sentence for the module half.
34. **SPEC.md:12978-12979** - "and there is no keyboard\nISR" - `kbm_isr` hooks int 09h (mouse.inc:2875) and wakes the UI task. Proposed: "keys reach programs only through the UI task's int 16h poll (§13 step 1); `kbm_isr` only wakes it".
35. **SPEC.md:12995** and **46051** - "`gfx_lock` is a byte with no owner field" - `gfx_lock_own` is stored with the flag (vga12.inc:5121-5124). Proposed: "`gfx_lock` records its owner (`gfx_lock_own`) but cannot take the lock back from it, so there is no cheap kernel defence".
36. **SPEC.md:13010** - "`task_spawn` writes `SCH_STACK` bytes of `0xCC`" and **13019** "One `rep stosb` per" - the fill is the slice's own `sch_stksize` and `rep stosw` (sched.inc:983-989). Proposed: "writes the slice's own size (`sch_stksize`) of `0xCC` ... One `rep stosw` per *spawn*, ~0.7 ms at 384 bytes and inside the spawn's IF=0 window".
37. **SPEC.md:13163** - "| `.bss` — the saved SP, the flag, the skip counter | 4 |" - the SP is held in BX since kernel size pass 16 (sched.inc:2198-2201). Proposed: "| `.bss` — the flag and the skip counter | 3 |". The code block above it (13093-13104) also shows `dec byte [sch_chbusy]` and no SS test; the code stores 0 and tests SS (2202-2219).
38. **SPEC.md:13546** - "The evidence is banked into `.bss` first," - SP moves first and the evidence goes from the table into registers onto task 0's stack (sched.inc:2687-2699). Proposed: "`SP` moves first, to task 0's ...; the evidence is read out of the task table into registers and pushed there". And **13573** "(`.text` +201, `.bss` +8)" - `.bss` is now `sch_dpcol` and `sch_dphc`, 4 bytes (2966-2968).
39. **SPEC.md:18117** - "cmp word [evq_count], EVQ_CAP" (the 10.1 snippet: word ops on `evq_head`/`evq_count`) - all three indices are bytes (events.inc:95-116, 350-352). Proposed: replace the snippet with the byte form (`cmp byte [evq_count]`, `mov al, [evq_head]`, `add al, EVQ_RECSIZE`, `and al, EVQ_MASK`, `dec byte [evq_count]`).
40. **SPEC.md:18176-18182** - "`evq_push` runs in the mouse ISR, which runs on the interrupted task's stack" and "The source is the ring, and `rep movsw` reads it through DS" - the mouse ISR runs on `mou_pstack` with SS loaded (§9.10.1), and the copy is four unrepeated `movsw` (events.inc:267-270). Proposed: "which runs on `mou_pstack` with SS = LOW_SEG (§9.10.1)" and "the copy is four `movsw` through DS, which is banked: the same bytes as four `ss` prefixes" (see sched-13).
41. **SPEC.md:46163-46168** - "(kernel/instance.inc:1339)" ... "(kernel/sched.inc:422)" ... "**This spec and `apps/os88api.inc` both used to say" - the line references are stale (the frame is `sch_frame`, sched.inc:1075-1107), and `apps/os88api.inc` STILL says it (next item). Proposed: cite `sch_frame` by name and drop "both used to".
42. **apps/os88api.inc:3940** - "; Your worker gets DX = your instance index, DS = ES = CS = KERNEL_SEG," - a worker gets DS = CS = its own segment and ES = KERNEL_SEG (sched.inc:1079-1087, 1100-1104; SPEC.md 20.6). Proposed: "; Your worker gets DX = your instance index, DS = CS = YOUR segment, ES = KERNEL_SEG,". (SDK prose: `abilock.py --update` without a bump.)
43. **apps/os88api.inc:3966** - "Your stack is SCH_STACK (384) bytes in LOW_SEG" - it is the class your header declares, or larger (SPEC.md 8.7). Proposed: "Your stack is the class your header declares (OS88_STACK_*, 128..384 bytes; 384 if you declare none) in LOW_SEG". And **:34-37** "whose 384-byte stack is likewise in LOW_SEG" / "the tick, the mouse and any sound IRQ land their frames on it" - per class, and since SPEC.md 8.5/9.10 the ROM's chain and the mouse run on private stacks. Proposed: "whose stack (its declared class, SPEC.md 8.7) is likewise in LOW_SEG ... the tick's own frame and the sound driver's tick land on it while your worker runs".
44. **docs/KERNEL-MEMORY.md:587** - "2,816 (`kern_small` 1,280)" - kern_small's partition totals 960. **:596-597** "`kern_small` (`MAX_TASKS` = 7) is" / "128, 128, 192, 192, 256, 384" - it is `MAX_TASKS` = 5: 128, 192, 256, 384. **:602** "and `kern_small` five" - three (the idle task holds the 128). **:666-667** "every size must be\na multiple of 128 and even" - only even and at least `SCH_FRAME` + 2 (sched.inc:251-256). Proposed: "2,816 (`kern_small` 960)"; "`kern_small` (`MAX_TASKS` = 5) is 128, 192, 256, 384"; "and `kern_small` three"; "every size must be even".

## Open questions

1. **Is `.bss` zero at boot or not?** sched.inc and SPEC.md 8 say no; events.inc, blank.inc, dock.inc, SPEC.md 10 and 12.8.5.1 say yes (and 12.8.5.1 measured 0x00 on a live machine). The ruling decides sched-12 and a dozen comments outside this area.
2. **Does any shipped package call `OSAPI_TASK_SLEEP` from a window callback and depend on its length** (sched-4)? A grep finds ~40 packages that call the cell; which context each call is in needs a per-package look.
3. **Can a DOS program's `int 1Ch` hook run on `sch_chstack`** (sched-7)? It needs the tick to land while SS = LOW_SEG with that program's hook in the IVT - i.e. inside a kernel service the DOS back end runs on a LOW_SEG stack. If DOS virtualises 1Ch or swaps SS for every back-end call, sched-7 drops to the ROM-only case.
4. **CLAUDE.md's "`OSAPI_TASK_YIELD`, a full task switch | 693 us"** is PERFORMANCE.md Set 1, measured before khp1's sched-1 took a yield from 644 to 283 us on MartyPC's 5150 CGA. A switch to ANOTHER task costs more than a yield-to-self, so 693 may still be the right figure for what the row names - but nobody has re-measured it on the field machine since, and the table is quoted as a budget.
5. **Was SCH_CHSTK sized from `STKDIAG`'s `sd_cwater`** (sched-8), or from the A/B in STACK-SLOTS-PLAN 4.1? If the former, the 56-byte reading may be low.
