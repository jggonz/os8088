# Kernel hot-paths pass 1: sched

The heartbeat and the doors: `kernel/sched.inc` (IRQ0's `sch_isr`,
`sch_switch`, `task_yield`), `ui_task`'s idle pass (kernel/ui.inc),
`kernel/clock.inc`, `kernel/snd.inc`, `kernel/serial.inc`,
`kernel/memory.inc`, `kernel/xmem.inc`, the `evq_*` primitives and
kernel.asm's non-drawing API shims. The brief is `docs/plans/khp1/BRIEF.md`.

Branch `khp1-sched`, cut from `6d6c23ae` (the same kernel as `a7ee7fab`).
**Headline: an idle tick 17,558 -> 9,982 cycles (3.68 -> 2.09 ms, -43%;
kern_small 12,393 -> 9,362), `OSAPI_TASK_YIELD` 644 -> 283 us, the pointer
sweep's busy time 236.6 -> ~176 ms and typing's 618 -> ~517 ms, for 47 + 12
resident bytes (sched-1, sched-4, both owner's picks) and one free item
(sched-2).**

Every figure below is MartyPC guest cycles on a 4.77 MHz 8088 (209.5 ns a
cycle) unless it says otherwise. Two instruments were written for this
pass and kept out of the tree (they are in this agent's scratch, and both
are described here so they can be rebuilt):

* a **single-step tick tracer**: stop at `sch_isr`'s entry on an idle
  desktop, then step one instruction at a time (64 per pipelined round trip)
  until the idle task's `hlt`, recording the CS:IP and the cycle delta of
  every instruction and resolving it by linear address. One idle tick is
  ~1,050 instructions, so the trace is exact, per instruction, and
  repeatable to a few cycles tick to tick;
* a **unicorn equivalence harness** for `sch_switch`: both kernels' own
  `.text` bytes are loaded at 0060:0000, a random task table is written
  (states 0/1/2, the idle slot, the fsx freeze, `TF_SERVICE`, an outgoing
  task that has just exited below the bound), every task is given a frame
  tagged by its IP, and the switch is run to its `iret`. The resumed IP,
  `[sch_cur]` and the parked SP must agree.

## 1. BASE AND TIP

Assembled (`tools/os88rdiff.py kernel 6d6c23ae .` per kernel, and `make`'s
`kernsize` lines). Resident = `.text` + `.bss` + `.cold` + `.vga` +
`.lowbss` + `.vgabuf`. The `.cold` +2 on kern_big and kern_emu is the
coordinator's `mod_cfix` fix merged from `kernel-hotpaths`, not this branch.

| kernel | `.text` | `.bss` | other | resident delta (this branch) | `KERN_SIZE` base -> tip |
|---|---:|---:|---|---:|---|
| kern_big | 40,631 -> 40,689 | 5,377 -> 5,378 | `.ovl` +1 (not resident) | **+59** (47 + 12) | 96,768 -> 96,768 |
| kern_small | 30,220 -> 30,278 | 2,945 -> 2,946 | `.boot2` +1 (not resident) | **+59** | 59,392 -> 59,392 |
| kern_emu | 40,885 -> 40,931 | 5,377 -> 5,378 | `.ovl` +1 | **+47** (sched-4 is not built there) | 97,280 -> 97,280 |
| kern_mega | 45,853 -> 45,911 | 5,415 -> 5,416 | `.ovl` +1 | **+59** (conventional) | resident 62,976 -> 62,976 of 68,096 |

All 59 bytes sit before `.cold` (BRIEF section 8's COLD_HOME limit), well
under its ~300-byte flag. No
rung moved on any kernel; the rung is not the point, the 47 bytes are.

## 2. WHERE THE TIME GOES

### 2.1 One idle tick, base, CGA (`os8088_5150_cga_gla`, kern_big)

From `sch_isr`'s entry to the idle task's next `hlt`: **1,046 instructions,
17,558 cycles = 3.68 ms a tick, 6.7% of the machine**, identical within 40
cycles on Hercules (17,597) and the VGA XT (17,566) - none of it draws.

| routine | cycles | share | what it is |
|---|---:|---:|---|
| `sch_switch` | 7,389 | 42.1% | THREE switches a tick (ISR -> ui_task, ui_task's sleep -> idle, the idle task's own yield after the `hlt`), each scanning all 14 slots at ~144 cycles a slot |
| `sch_isr` | 2,052 | 11.7% | entry, the ROM chain's stack swap, and the wake loop over 14 slots at ~73 a slot |
| `ui_task` | 1,976 | 11.3% | the pass's ~25 flag tests |
| `sch_account` | 1,501 | 8.5% | three charges at ~500 (the ISR's, and one per real task change) |
| ROM | 707 | 4.0% | the BIOS `int 08h` chain and `int 16h` AH=01h |
| `task_yield` | 614 | 3.5% | three frames built |
| `clk_tick` | 590 | 3.4% | a `mul` and a `div` (~275 of it) |
| `sch_resume` | 471 | 2.7% | three frame pops and `iret`s |
| `mou_hotplug`, `toast_pass`, `task_sleep`, `evq_pop`, `blk_pass`, `sch_idle_body`, `kbm_ui`, `snd_tick`, ... | 2,258 | 12.9% | each under 2% |

**The scan of FREE slots was 5,184 of those cycles**, and the wake loop's
876 more: 34.5% of an idle tick was looking at task records that were
empty. os88hot's `profile idle` agrees (the coordinator's notice:
`sch_switch` 45% of busy, `sch_account` 15%).

### 2.2 The mouse packet's road to its handler, base, CGA

Logging breakpoints over 30 packets: from `sch_wake_ui` (the third byte's
ISR) to the next `ui_task` pass, **23,974 cycles median (5.02 ms)** - which
is SPEC.md 8.1.2.2's 5.14 ms. Of that, **~20,400 is the mouse ISR itself
after the wake** (the cursor redraw, `cur_move_mono` - the pointer agent's),
and **3,520 (0.74 ms) is the scheduler's hand-off**: the idle task's `hlt`
ending, its `task_yield`, a 14-slot scan, a charge, and the UI task's
return out of `task_sleep`.

### 2.3 Named operations (os88hot `profile`, tip, CGA)

* **pointer sweep** (194.5 ms busy at the tip): `cur_move_mono` 32%, then
  ui_task 7.5%, `sch_switch` 6.7%, `sch_resume` 6.4%, `task_yield` 4.2%,
  `sch_account` 4.1%, `sch_idle_body` 2.6% - the scheduler is ~35% of the
  busy time of moving the mouse, because every serial BYTE ends the idle
  task's `hlt` and each one cost a switch until sched-4.
* **launch Calc (cached)** (951 ms busy): `lz_lzb` 17%, `sw_col` 12%,
  `sch_switch` 9.1% + `task_yield` 4.7% + `sch_pit_now` 1.1% - that last
  group is `wm_anim` spinning on `task_yield` until a frame deadline, which
  costs no milliseconds (the animation is time-paced) but is CPU a worker
  could have had (CROSS-FILE).
* **heap**: `mem_claim_1` is 2 calls a launch at 9,496 cycles median - 3.98
  ms, **0.4% of a Calc launch**. Nothing in memory.inc or xmem.inc is on a
  path a user waits on at a cost worth a byte.
* **the clock**: `clk_tick` is 590 cycles a tick (0.12 ms, 0.23% of an idle
  machine); with seconds hidden the bar's text is redrawn once a MINUTE, and
  `clk_fmt` is a few hundred cycles of it - the redraw is `font_run`'s
  (text agent's).
* **sound**: `snd_tick` is 153 cycles a tick, a gate. No speaker ISR runs on
  the desktop.

## 3. TAKEN

| id | agent | routine / path | hotness | change | kern_big B | kern_small B | cycles base -> tip (input) | user-visible (operation, adapter: ms base -> tip) | M/P | 286 | commit | FREE / [+N] |
|---|---|---|---|---|---:|---:|---|---|---|---|---|---|
| sched-1 | sched | `sch_isr` wake loop, `sch_switch` scan, `task_spawn`, `task_exit` | H2 | the two loops stop at `sch_top`, one past the highest slot that can be live (SPEC.md 8.1.3) | +47 | +47 | idle tick 17,558 -> 11,533 (CGA, kern_big); 12,393 -> 10,897 (kern_small); a free slot in the scan ~144 -> 0; a no-op yield ~2,630 -> ~1,100 | `OSAPI_TASK_YIELD` 644 -> 283 us (sysbench, CGA, -56%); mouse packet to UI pass, scheduler share 0.74 -> 0.38 ms; menu open 45.0 -> 42.9 ms and close 24.3 -> 22.7 ms; pointer sweep busy 236.6 -> 189.8 ms (-19.8%); typing 31 keys busy 618 -> 545 ms; idle busy 6.5% -> 4.1%; with Fractal's worker running a tick 11,811 -> 9,516 cycles (+0.9% to the render) | M (all; ms rows are sched-1 + sched-2 together, os88hot `ab`) | fewer iterations on every CPU; the parity of `sch_isr`, `sch_switch` and everything after them is kept by three never-executed pads (`t_hotpar` green) | 488ea2d5 | **[+47] OWNER'S PICK** |
| sched-4 | sched | `sch_idle_body`, `sch_isr`'s wake loop, `sch_switch`'s fallback | H2 (every interrupt while idle) | the idle task halts again after an interrupt that posted no wake; the tick's wake loop posts `[sch_idlewake]`, the "nothing ready" fallback spends it (SPEC.md 8.1.2.5). Not on kern_emu | +12 | +12 | idle tick 11,205 -> 9,982 (CGA); kern_small 10,897 -> 9,362 (with sched-2); the idle task's yields over 30 mouse packets 218 -> 29 | pointer sweep busy 191.1 -> 175.9 ms (-8.0%); typing 31 keys busy 544.6 -> 516.7 ms (-5.1%); idle busy 203.2 -> 179.7 ms / 5 s (-11.6%); ms unchanged (+0.2-0.3%, rep noise; a wake pays ~90 cycles more before its yield) | M (os88hot ab vs 89fe86d7) | none beyond the cycles; parity kept by one pad | d0ad21da | **[+12] OWNER'S PICK** |
| sched-2 | sched | `ui_task` (the pass's first look at the event ring) | H2 (every pass) | enter the drain through `.yield`'s byte compare instead of a full `evq_pop` on an empty ring; `ui_drain` stored and decremented as a byte to pay for the near jump | 0 | 0 | ui_task + evq_pop ~2,180 -> ~1,870 a pass; idle tick 11,533 -> 11,205 on top of sched-1 | every UI pass, idle or event: ~300 cycles (0.06 ms) off the time from an event's wake to its handler | M | none | 31309aca | **FREE** |

The ms rows from os88hot are `ab --base 6d6c23ae time <scen> -n 3 --machine
cga` medians: the two items are in one tree there and sched-2 is ~300
cycles a pass of them. `drag` moved -0.4% (512.3 -> 510.1 ms): a drag is
drawing.

**Equivalence, sched-1.** The unicorn harness ran 5,000 random task tables
through both kernels' `sch_switch`: **0 mismatches** in the task resumed,
`[sch_cur]` and the parked SP, with every slot picked between 144 and 1,163
times. Set `[sch_top]` one short and it reports 16 mismatches in 200: the
harness can fail. The wake loop's change is the same argument by
construction (a free slot is never in state 2). `task_spawn` raises the
bound before its publish under its own `cli`; `task_exit` lowers it under
its own. `uiblock`, `schacct`, `tmload`, `tmsmall`, `tmmaxrun`, `stkpanel`
and the kern_mega rows are the rows that drive spawn and exit (section 8).

**Equivalence, sched-4.** The unicorn harness against 89fe86d7: 3,000
task tables, identical picks (the fallback's new store changes no pick).
The argument for the idle loop is SPEC.md 8.1.2.5's: every READY-making
event posts the byte, and the fallback spends it in the scan's own IF = 0
window. What it buys is CPU the idle task was burning - so, as the bench
note says of every switch item, it frees CPU under the pointer, typing and
idle without shortening a drag. It is independent of sched-1 and reverts
alone (its wake-loop comment names `[sch_top]` as CL's bound, which would
read MAX_TASKS without sched-1; CL is non-zero either way).

**Equivalence, sched-2.** The drain allows exactly `EVQ_CAP` pops as
before (the count is seeded one higher for `.yield`'s `dec`), every event is
dispatched by the same pass, and `evq_pop` on an empty ring has no effect
but its return; `ui_drain`'s high byte is read nowhere. `evqfull` and
`wakedrain` cover the drain.

## 4. TRIED AND REFUSED

| what | bytes | cycles | why not |
|---|---:|---|---|
| **sched-3: a tighter scan loop** - BX stepped with DL instead of re-derived by `shl bx, cl` every slot, the wrap and the READY arm out of line | +4 (kern_big) | a free slot ~144 -> ~100; a 256- or 384-class worker's switch (Tracker, Telnet, Word, CWORD, anything undeclared: `[sch_top]` 11-13) ~380 cycles less; but an idle-desktop switch ~30 MORE (the two out-of-line arms are the common ones when two slots are live) - an idle tick 11,205 -> 11,298 with it | ~0.15% of the CPU with a high-slot worker running, a loss on the idle desktop. Built, measured, equivalence-checked (5,000 cases) and reverted. It depends on sched-1's CH = `[sch_top]` |
| **`sch_isr`'s charge on a tick that switches**: let `.pick` bill the interval | 0 | ~500 a switching tick (0.2% of the machine) | see section 6: it trades exactness for a 1-hour wrap |
| `clk_tick`: a `delta == 1` path with no `mul`/`div` | ~+12 | ~250 a tick | 0.1% of an idle machine, no operation faster |
| `ui_task`'s `int 16h` AH=01h poll (~340 cycles a pass) gated on a keyboard-IRQ flag from `kbm_isr` | ~+10, cross-file | ~340 a pass | changes behaviour: a key STUFFED into the BIOS ring (int 16h AH=05h) would wait for the next keystroke, and an AT BIOS's function 01h drops extended keys a buffer-pointer test would see. A BDA head/tail compare instead of the call is wrong for the same reason |
| dropping `sch_account`'s OCW3 "read IRR" select | -4 | ~20 a charge | unsafe: the IBM BIOS's unexpected-interrupt handler (D11) selects the ISR register and leaves it |
| heap claim/free | - | 0.4% of a launch | not worth a byte (2.3) |

## 5. WANTS AN ABI BREAK

Nothing large: **the far-call floor is already the shape's minimum.**
sysbench prices the hot SLOT round trip at 24.9 us (`API far call cell`,
`OSAPI GET_TICKS` inline at the same), a near call at 10.7 us, and nothing
in this pass changed either.

* **`OSAPI_TASK_YIELD` (0x009E) as a cell that enters the yield directly.**
  The 8-byte SLOT switches DS around `task_yield`, which then saves the
  caller's DS and loads the kernel's itself. A far-entered yield that builds
  the `iret` frame under the far return could skip the cell's `push ds / push
  cs / pop ds / pop ds` and the near call: ~60 cycles (13 us) of a 283 us
  yield, 4.5%. It changes the cell's length (every later slot moves). Callers:
  every package loop that yields per frame or per poll. Not worth a flag day
  on its own; worth folding into one if one happens.
* **The rare (6-byte) cells cost ~85 cycles more than a hot one** (SPEC.md
  20.3). Of the rare cells in my files, `OSAPI_XMEM_RUN`/`_WALK`/`_SEL`
  (0x04B3..0x04CB, xmem.inc) are the only ones a package might call per
  frame (a player streaming from XMS); at a few calls a frame that is
  ~0.1 ms a second. Promote them in the next renumbering, not alone.

## 6. WANTS THE OWNER

* **The tick's own charge** (`call sch_account` after `inc word [ticks]` in
  `sch_isr`). SPEC.md 8.1 already says a switching tick charges twice and
  declines to correct it ("0.19%"); with the scan now cheap that is ~500 of
  a ~10,000-cycle idle tick, 5%. Moving the call to the refusal arm
  (`.tres`, which the lock, the nested chain and the cooperative mode all
  reach) costs 0 bytes and leaves every Task Manager figure exact - EXCEPT
  that a tick which switches back to the SAME task would no longer stamp
  `sch_pit_last`, so a single task that ran for over an hour with no other
  task ever picked (no UI sleep, no idle, no `task_cycles`) would lose an
  hour of its count to the 32-bit wrap. That cannot happen on a kernel whose
  UI task sleeps a tick at a time, but it is the case 8.1 wrote the
  unconditional call to rule out. Not taken: the decision is SPEC.md's.

## 7. CROSS-FILE

* **pointer (kernel/mouse.inc): the mouse ISR's cursor redraw is 85% of a
  packet's road to its handler** - ~20,400 of ~24,000 cycles from the wake
  to the UI pass are the ISR after `sch_wake_ui`, at IF = 0 for the serial
  ISR's duration. `cur_move_mono` is 32% of the pointer sweep's busy time.
* **wm (kernel/wm.inc, `wm_anim`): the open/close animation waits on its
  frame deadline by spinning `task_yield`** - `sch_switch` + `task_yield` +
  `sch_pit_now` are 15% of a Calc launch's busy time and buy nothing (the
  animation is time-paced). A `task_sleep` to the next tick, or an idle-task
  wait, would hand that CPU to a worker and to the decoder of the NEXT
  launch; the launch's milliseconds would not move.
* **wm (kernel/blank.inc, `blk_pass`)**: ~200 cycles every pass, idle or
  not.
* **wm (kernel/ui.inc, SPEC.md 13.13)**: the tick sweep sets `[ui_post]`
  every tick, so the posted-work walk (~12 flag tests, ~500 cycles) runs on
  every tick pass whether or not anything posted. It is the documented
  safety net for writers that do not post; a census of those writers would
  let it go.

## 8. ROWS RUN

Scoped, `--marty-jobs 1`. The set ran once after sched-1 and sched-2 (18
passed), and once more after sched-4, the last code commit, with
`dskwstage` added because it stops at the idle loop's head:

| row | result (after sched-4) |
|---|---|
| `uiblock`, `schacct`, `stk0water`, `stkpanel`, `evqfull`, `wakedrain`, `tmload`, `tmsmall`, `tmmaxrun`, `tickzero`, `saverate`, `paintsize`, `uilayer`, `cpup` (the cooperative mode), `deskcaltask`, `dskwstage` (MartyPC) | ok |
| `megatmmem`, `mega-cpup` (kern_mega, Bochs) | ok |
| `box-dtwrite` (kern_big on 86Box's mr286, a 286) | ok |

19 passed, 0 failed, 364.3 s. The fast tier passed in every `make` at each
commit (73 passed, `hotpar` green on kern_big and kern_small); `make small`,
`make emu` and `make mega` build at the tip; `tools/stkbalance.py` over
sched.inc and ui.inc reports the base's own two pre-existing tails and
nothing new.

Rows that cover the items: sched-1 - `uiblock`, `schacct`, `tmload`,
`tmsmall`, `tmmaxrun`, `stk0water`, `stkpanel`, `stkclass` (fast),
`tickzero`, `saverate`, `paintsize`, `cpup`, `deskcaltask`, `megatmmem`,
`mega-cpup`, `box-dtwrite`; sched-2 - `evqfull`, `wakedrain`, `uilayer`,
`uiblock`; sched-4 - `uiblock`, `cpup`, `saverate`, `paintsize`,
`dskwstage`, `uilayer`, `mega-cpup`.

## 9. DEFECTS FOUND

* **sysbench's `PIT/tick want 65536` row prints 131050** on the tip (65536
  on the base) - not a timing change: `sb_ticklen` takes the residual of two
  same-phase PIT samples modulo 65536 and adds it to 65536, so a second
  sample taken 22 counts LATER in its period than the first (ISR-entry
  jitter, which its own comment expects) reads as +65514. The row should
  sign-extend the residual. Not fixed (tests/sysbench is no agent's file).
* **sysbench's `int 16h AH=01h` row moved +9.8%** (67.7 -> 74.3 us) and
  `mov [disp16],ax` +0.5% with nothing in their path changed: method P
  rows still take the tick when the ROM `sti`s inside `int 16h`, and the
  run's phase moved. Read those two rows as noise in any A/B.
* The boot overlay's window half has **3 bytes** spare on kern_big
  (`OVL_SIZE` 5,117 of 5,120). sched-1 needed a byte there and got it by
  sharing `sched_init`'s `sch_idleslot` store; the next agent will not have
  room for a 5-byte store.
