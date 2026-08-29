# The idle machine: what it costs, and what to do about it

**Design not started. This is a handoff.** Nothing here has been built; every
number in §1, §2.3, §2.4 and §2.5 has been measured on MartyPC's cycle counter
at a 4.77 MHz 8088, and every number elsewhere is labelled where it is an
estimate.

It exists because a question about *accounting* turned out to be a question
about the *idle loop*, and the accounting was only the thing that noticed.

---

## 1. What an idle os8088 actually spends

Measured on `os8088_5150_cga_gla`, 360KB system + apps disks, at a bare
desktop with nothing open and the mouse not moved. Breakpoints cost the guest
nothing, so these are the 5150's own cycles.

### 1.1 The task table has ONE task in it

```
task 0  state=READY     ui_task
task 1..7 free
sch_cycles[0] = 15,486,213      (every other slot zero)
```

That is the whole system. `ui_task` is the only thing runnable, and 100% of
the machine is billed to it — the books balance, which was established in an
earlier round and is confirmed here.

### 1.2 It goes round 1,028 times a second, at 4,536 cycles a pass

`blk_pass` is called exactly once per `ui_task` pass, so pass-to-pass deltas
on it price the loop:

```
300 passes:  median 4,536 cycles   p10 4,536   p90 4,536
rate over the window: 1,028.3 passes/s
```

286 of those 300 passes are **identical to the cycle**. 4,536 x 1,028.3 =
4,664,000 cycles/s against the 8088's 4,772,727: the poll loop is **97.7% of
the machine**, and the machine is doing nothing.

### 1.3 Where the 4,536 go

A breakpoint trace of the median pass, marked at every routine it enters:

| segment | cycles | share |
|---|---:|---:|
| `sch_resume` -> `blk_pass` (iret, `ret`, `.loop`, the reboot check, **`int 16h` AH=01**) | 615 | 13.6% |
| `blk_pass` — the blanker's idle clock (SPEC.md §64.1) | 328 | 7.2% |
| `wm_wake_redo` (SPEC.md §74.1.1) | 152 | 3.4% |
| `evq_pop` + the `.yield`/`.tail`/posted ladders + the `[ticks]` compare | 1,099 | 24.2% |
| `task_yield` frame build (pushf/cli/push cs/call, 9 pushes, DS) | 251 | 5.5% |
| **`sch_account`** (SPEC.md §8.1) | 570 | 12.6% |
| **`sch_switch`** — canary + the 8-slot round-robin scan | 1,521 | 33.5% |

`sch_account` measured on its own, stepped out of its byte range: **500
cycles** entry to return, 500 min / 503 max over 12 samples. The 570 above is
the same call plus `task_yield`'s `cmp [sch_lock]` and the jump.

### 1.4 The three numbers that matter

- **`sch_switch` is the biggest single item, not the accounting.** 1,521
  cycles x 1,028/s = 1.56 M cycles/s = **32.7% of the machine**, spent walking
  eight task slots to discover — 1,028 times a second, on a table with one
  entry — that there is nobody to switch to, and resuming the caller.
- **Accounting is 10.9%.** 500 x 1,043.6 calls/s (1,028 yields + 18.2 ISR
  entries) = 521,786 cycles/s.
- **286 of 300 passes find nothing to do.** The tick-gated section behind
  `.posted_done` runs 18.2 times a second; the event-driven sections run
  never, because nothing happened. **56 passes in 57 are pure waste.**

A pass that *does* have a tick in it costs 5,290 cycles to its last mark
against the null pass's 3,921 — so the whole of the useful work an idle
desktop does is 18.2 x ~6,000 = **109,000 cycles/s, 2.3% of the machine.**

> The other 95.4% is a loop asking questions whose answers have not changed.

---

## 2. Turning accounting off when nobody is listening

Taken on its own terms first, because it was asked on its own terms. **The
answer is not to measure less often; §2.5 measured that and it is 21 points
wrong on this machine.** It is to notice that most of what is being measured
is a switch that does not switch.

### 2.1 There is exactly one consumer

`sch_cycles[]` reaches `SS_TCYC` (`kernel/instance.inc`) and `I_CYC` reaches
`SSI_CYC`, and both are read only through `osapi_sys_snapshot`, which only the
Task Manager calls. With the Task Manager shut, every one of those 521,786
cycles a second is written and never read.

### 2.2 The options, priced

| | cost when OFF | leak | bytes |
|---|---:|---|---|
| **A. gate byte** at the top of `sch_account`, beside the existing `sch_fast` test | ~45 cycles (the call, the compare, the ret) | 45 x 1,043 = **0.98% forever** | +13 `.text`, +12 `.bss` (measured, previous round) |
| **B. indirect `call [vec]`**, vector to a bare `ret` | ~49 cycles | same as A | worse than A, and no clearer |
| **C. patch the call sites** (self-modifying) | **0** | none | ~30, plus §8's knob and verify table |
| **D. sample in the tick ISR** | n/a — never worth turning off | **0.015%**, but **21 points wrong** — §2.5 | net negative |
| **E. account only when the task CHANGES** | **0** on an idle machine, exactly | none | ~10, and see §2.3 |

**A is the conventional answer and it is not free.** The 45 cycles is the
*call frame*, not the work: gating inside the routine cannot remove the call,
and `task_cycles`/`task_debit` add `pushf`/`cli`/`popf` around theirs. Arming
it (`osapi_sys_snapshot` sets a TTL, the tick ISR decrements) costs another
~10 cycles a tick, and re-arming must **re-stamp `sch_pit_last`** or the first
slice after a quiet period is charged the whole quiet period.

**C costs an instrument, and that is a price rather than a refusal.**
docs/FIELD-MACHINES.md's rule is that linear `0x600` onward is
`build/kernel.bin` byte for byte apart from writable state; `tools/os88marty.py
verify` is that rule as one command, and it is what proves a field dump came
off the build that was sent. Patched call sites put a difference in `.text`
*instructions*, which is precisely the class of difference the rule uses to
say "you are running a different kernel". **§8 is what buying it back looks
like**, and it is the only option here that costs literally zero when off.

### 2.3 The exact answer, and it is free

The reason A needs arming is that its cost scales with the **switch rate** —
something the kernel does not control and an application can raise without
limit. But look at what those switches actually do.

**On a bare desktop, `sch_cur` never changes.** Measured, 400 consecutive
`sch_resume` entries with the byte read at each:

```
400 resumes in 0.382 guest s (1,048/s); 0 changed sch_cur (0.0%)
who was resumed: {task 0: 400}
```

There is one task in the table (§1.1), so `sch_switch`'s scan always falls
through to "nothing ready, resume the outgoing task". Every one of those
500-cycle calls charges task 0 and then goes on running task 0.

So **account only when the picked task differs from the running one.** At
`.pick`, `cmp dl, [sch_cur]` and skip. It is exact, not statistical:
`sch_account`'s own contract is that "the timestamp is exact for intervals of
any length", so a skipped no-change switch simply makes the next interval
longer and bills it to the same task, which is the same task. **Not one cycle
is mis-attributed and nothing is lost.**

The interval cannot run away either: `sch_isr` stamps every tick regardless,
above the `sch_lock` check, so the longest interval this can ever produce is
one tick against a 32-bit timestamp that wraps at ~65,536 of them.

| | idle accounting |
|---|---:|
| now | 500 x 1,043.6/s = **10.9%** of the machine |
| account on CHANGE only | 500 x 18.2/s = **0.19%** |

Cost: about ten bytes, and ~30 cycles of push/pop on the switches that are
real (`sch_account` clobbers AX/BX/CX/DX/SI, and `.pick` is holding the picked
index in DL and the record in BX).

### 2.4 What it does NOT cover, measured

With a second runnable task the trick buys nothing — **and that held only
until §6.3 shipped**, which is worth leaving in because it is the one thing
this file got wrong. Measured against a spinning `ui_task`:

```
Task Manager open: 400 resumes in 0.298 guest s (1,341/s);
                   399 changed sch_cur (99.8%);  {task 0: 200, task 1: 200}
```

Two ready tasks alternated perfectly, so every switch was real. Once `ui_task`
blocks it is not competing for the slot, and the package worker yields to
**itself**:

```
Task Manager open, after 6.3: 1,541 switches/s, and 6 of 200 change
```

So the case §2.3 was written off for is the case it does its biggest work in:
1,541 switches a second and **54 charges**. Accounting is **0.57% of the
machine both idle and with an app open**, against 10.94% and 14.98% before
these two changes — and `tests/schacct.py`'s strongest row is now a 28:1
separation measured with a worker spinning, which is the opposite of where it
started.

**And the callback-billing pair costs nothing at rest.** `task_cycles` and
`task_debit` bracket every `W_ONKEY` / `W_ONCLICK` / paint dispatch at 500
cycles each, which reads like a standing 1,000-cycle tax. Measured over a
guest second in both states:

```
                 bare desktop      Task Manager open
task_cycles          0 /s               0 /s
task_debit           0 /s               0 /s
sch_account      1,044 /s (10.94%)  1,430 /s (14.98%)
```

Zero, because a callback fires on **input**, and a quiet machine has none —
the Task Manager's own refresh is drawn by its worker under the gfx lock, not
through a dispatched `W_PAINT`. Against a human's click rate 1,000 cycles is
nothing. **The callback path is not a target; the switch path is the whole of
it.**

Nor can the routine be made cheaper. Stepped instruction by instruction:

```
29 instructions, 500 cycles, no single instruction above 43
```

No hot spot, not even the five ISA port accesses — it is a uniform ~17 cycles
an instruction, which is what an 8088 costs. **The only lever is calling it
less.**

### 2.5 Sampling was the first answer here, and it is WRONG on this machine

The obvious alternative is to stop stamping switches and instead record
`sch_cur` at every IRQ0 — ~40 cycles, 18.2 times a second, **0.015%**, with no
arming and no "is anyone listening" check because a thing that costs 728
cycles a second is never worth switching off. It was written up as the
recommendation here. **Measuring it killed it**, and the reason is worth
keeping because it will look attractive again.

A tick sample answers correctly for a task that uses a whole quantum: the tick
*is* the pre-emption point, so `sch_cur` at `sch_isr` entry names exactly who
was interrupted. os8088 has almost no such work. It has 1,048 yields a second
against 18.2 ticks — **57 yields per tick** — so essentially every slice is
sub-quantum, and a sub-quantum slice is counted only if the tick happens to
land inside it.

That would be tolerable if it were noise. It is not noise, it is **aliasing**,
because os8088's tasks are metronomes. Measured over 3,000 consecutive slices
with the Task Manager open:

```
task 1  3,000..3,249 cycles   x1471 of 1500
task 0  2,750..2,999 cycles   x1421 of 1500
```

A workload period of ~6,000 cycles against a tick of 262,144 — 43.7 of one in
the other. A sampler at that rate visits a small lattice of phases inside the
cycle instead of sweeping it. Taking the same timeline and asking what a
sampler would have answered at 40 different sampling phases:

```
EXACT share:                       task 0 45.5%   task 1 54.5%
tick sampler, task 0, by phase:    min 18.9%   median 40.5%   max 64.9%
                                   spread with phase: 45.9 points
```

**45.9 points of error decided by nothing but when in the tick you sample**,
against a true 45.5%. A separate 22-second run with the exact `sch_cycles[]`
read either side of it came out 20.7 points wrong in a fixed direction — 8
sigma on 400 samples, so it is not the window being short. And a per-refresh
number, which is what the page draws every 9 ticks, spread **67 points**
across 44 refreshes (11% to 78%) for a task whose true share was 62%.

Averaging longer does not fix any of this. At a fixed sampling phase the
error is systematic, and every candidate for making it unbiased — dithering
the sample point, sampling on the PIT phase rather than the tick edge, taking
the sub-tick while `sch_fast` is armed — costs the cycles the idea was
supposed to save and still has to be argued for. §2.3 is exact and cheaper.

## 3. The scheduler redesign

§2 saves 10.9% of an idle machine. §1.4 says the idle machine is 97.7% waste.
The redesign is about the other 87%.

### 3.1 The shape, in four pieces

#### 3.1.1 `ui_task` blocks instead of spinning

It already has the wake condition: `[ui_post]` is a single byte set by **27
sites**, including both ISRs — `kbd_ovflow` sets it (`kernel/mouse.inc`), and
the mouse ISR sets it with `[cur_shchk]` beside it (SPEC.md §7.2.1.1, §13.13).
The int 09h vector is already hooked on every tier (SPEC.md §9.6.4), so **a
keystroke already reaches kernel code**; nothing new has to be taken away from
the BIOS. The loop becomes: do the pass, then block until `[ui_post]`, a
queued event, or the next tick.

#### 3.1.2 A tick wake stays, unconditionally

`blk_pass`, `clk_tick`, `toast_pass`, `mou_hotplug` and `wm_tarm` are all
time-driven and all tolerate 55 ms of lateness by design — the section comment
at `.posted_done` says so. Waking on the tick keeps every one of them exactly
as it is, and caps the win at 18.2/1,028 = **1.8% of what the loop costs
now**. That cap is the win.

#### 3.1.3 An idle TASK, not an idle path

This is the part that cannot be short-cut. `sch_switch`'s "nothing ready,
resume the outgoing task" fallback is correct only while task 0 never sleeps;
the moment `ui_task` can block, that fallback resumes a sleeper. The tempting
fix — `sti; hlt; cli` and re-scan, inside `sch_switch` — **nests the stack
without bound**: the ISR that ends the `hlt` falls into `sch_switch`, finds
nothing ready, and halts again one frame deeper, on a slice that is 384 bytes
(SCH_STACK) sized at 1.8x the measured worst case. A real task slot whose body
is `sti; hlt; jmp` parks its frame in its own record like any other task and
cannot nest.

It is close to free: `sch_stacks` already reserves `(MAX_TASKS-1) * SCH_STACK`
= 7 slices and **one is in use**, so the idle task takes an existing slice and
adds no bytes. It costs one of seven application task slots, against
`INST_MAX` = 12 instances.

The scan must prefer any other ready task over it — one remembered index in
the existing loop, not a second pass.

#### 3.1.4 Accounting stays exact, and the idle task is the idle bucket

§2.3's rule, and no flag and no arming: with `ui_task` blocked, the picked
task is the idle task tick after tick and the compare skips every one of them.
"Who owns idle" then answers itself — it is whoever `sch_cur` names, and while
the machine is idle that is the idle task, accumulating real PIT cycles while
halted. **The idle bucket is exact, not estimated.**

### 3.2 What it means for the Task Manager's own 34%

`apps/taskmgr/taskmgr.asm` says it outright at the top of the file:

> `tm_worker` is the monitor task and doubles as the system idle soak: it
> never sleeps, spinning `{ count += 1; OSAPI_TASK_YIELD }` for 9-tick
> intervals.

The load meter **is** a spinner: fewer iterations in a fixed interval means a
busier machine. So the 34–38% the page charges itself is not a bug, it is the
instrument, and it cannot be optimised away while the measurement works like
that. Under §3.1 it does not have to: load becomes `1 - idle_cycles /
total_cycles`, read straight off the idle task's own `sch_cycles` slot — which
§2.3 keeps **exact** — and `tm_worker` drops to `OSAPI_TASK_SLEEP` for its
interval. **The Task Manager's own share goes to
roughly nothing, and the "TaskMan: 34% while the graph shows 0–2%"
contradiction goes with it** — both numbers were right, they were measuring
different things.

### 3.3 Estimated result

| | now (measured) | after (estimated) |
|---|---:|---:|
| idle `ui_task` passes/s | 1,028 | 18.2 |
| idle cycles/s in the loop | 4,664,000 | ~109,000 |
| share of a 4.77 MHz 8088 | 97.7% | **~2.3%** |
| accounting | 10.9% | **0.19%** (§2.3, and exact) |

The right way to read that is **not** "the machine gets 95% faster" — there is
nothing else for an idle 8088 to do. It is:

- a background worker stops competing with a spinner. Today, with one
  application open, round-robin gives `ui_task` a slice per yield and it
  spends every one of them on 4,536 cycles of nothing; that is where the
  earlier round's **62% / 38%** split came from. A MOD mixer that needs 55% of
  the machine (SPEC.md §53.5.1 documents exactly this fight) would get it.
- `hlt` on a real XT is less heat and, under every emulator here, the host's
  CPU back.
- interrupt latency improves: a halted CPU takes the interrupt at once instead
  of finishing whatever the loop was in.

---

## 4. Where it bites

Written as a list to argue with, not a list of blockers. Each has been checked
against the source; none is speculative.

1. **The spin is currently a fail-safe.** A machine whose IRQ0 is masked spins
   uselessly today and **hangs on `hlt` tomorrow**. `khb_imr` exists because
   "is IRQ0 still let in" is a real field question. The idle task's `hlt`
   should be `sti; hlt` with the mask asserted on the way in, or the KFZ
   heartbeat needs a reading that distinguishes the two.
2. **The KFZ watchdog is fine, but only just.** `KHB_STUCK` is 546 ticks (30
   s) with no `ui_task` pass. A tick wake gives 18.2 passes a second, so it
   clears by three orders of magnitude — **provided §3.1.2 stays**. A later
   "why wake on a tick nothing needs?" tickless change silently disarms the
   one instrument that reports a wedged machine in the field.
3. **`ui_arm_trk` is a latency requirement, not a throughput one.** The held
   chrome box follows the pointer (SPEC.md §13.8.1) and today does so at 1,028
   Hz. Tick-only wakes make it 18.2 Hz and it will be seen. The mouse ISR
   already sets `[ui_post]` on every motion packet, so a wake there fixes it —
   but it must be a *wake*, not just the flag.
4. **`sch_lock`.** A floppy transfer bars switching. An idle task changes
   nothing here (the fallback still resumes the outgoing task), but the
   interaction is worth a test: `sch_lock` held across a `hlt` in the idle
   task means the machine idles with the lock set, which is correct and looks
   alarming in a dump.
5. **`fsx` (SPEC.md §53.2).** `fsx_wait` FSXW_TICK already does `hlt` in task
   context — precedent, and it works. FSXW_FRAME yield-spins on `[sch_subs]`
   deliberately, and must keep doing so: inside a fullscreen bracket the spin
   is correct. The whitelist scan in `sch_switch` must not let the idle task
   through while `[fsx_task]` is armed, or an exclusive bracket loses slices
   to it.
6. **`sch_fast` (SPEC.md §53.2.1) already pauses accounting**, and §2.3
   leaves that gate exactly where it is — the change-compare sits at `.pick`,
   above it, so a sub-tick bracket still stops the arithmetic its own divider
   invalidates. What DOES need checking is `sch_pit_last` across a bracket:
   fewer accounting calls means a longer stale interval on the way out, and
   the interval that spans an fsx bracket is charged to whoever the bracket
   ran on. That is arguably right and it is not what happens today.
7. **Sixteen packages call `OSAPI_TASK_YIELD`** (`apps/`, `drivers/`). None
   breaks — a spinning package simply keeps the machine out of idle, so the
   benefit is capped by what packages do rather than by the kernel.
   `OSAPI_TASK_SLEEP` is already published (AX = ticks) and is the answer for
   each of them, one at a time, as its own change. `apps/ftpd` and
   `drivers/net` are the ones to look at first: a socket poll loop is the
   shape that spins hardest.
8. **In-kernel spinners.** `kernel/menu.inc` ("keep the scheduler healthy
   while we spin"), `kernel/vga12.inc`'s compactor, three sites in
   `kernel/instance.inc` and `ui_drag`'s `.linger`. All stay spinners; all are
   bounded and all are inside a user gesture. They are named here so a later
   sweep does not treat them as oversights.
9. **`task_exit` jumps into `sch_switch`** after freeing its own record. With
   an idle task always ready, the scan always finds one, which is *safer* than
   today — but the comment that reasons about "task 0 never sleeps and never
   exits" becomes stale and must be rewritten rather than left. 10. **One of
   seven task slots.** `MAX_TASKS` = 8, mirrored in `apps/os88api.inc`; the
   idle task takes a slice `sch_stacks` already reserves, so it is free in
   bytes and costs a slot. If that is the wrong trade, the alternative is a
   dedicated ~64-byte idle stack outside `sch_stacks` — cheaper in RAM, but it
   breaks the canary math's assumption that every slice is `SCH_STACK` bytes,
   which `sch_switch`, the KFZ deep sampler and `tests/stackprobe` all derive
   from.

## 5. What this does NOT fix

- **The 51 ms heap-page walk.** That is real work under the gfx lock and is
  unaffected; SPEC.md §28.6.1 already keeps it off the quiet intervals.
- **Anything on a busy machine.** The 97.7% is 97.7% *of an idle machine*.
  When there is work, the loop count collapses on its own and all of this is a
  rounding error — which is exactly why it went unnoticed.
- **Drawing costs.** Nothing here puts a pixel on the screen faster.

## 6. The plan

Five stages. Each is separately shippable, each has a gate that fails loudly,
and stages 1 and 2 are worth having even if the rest is never built. Every
"expected" below is a number to check against, not a hope.

### 6.1 Stage 1 — account only on a real change — **BUILT**

`kernel/sched.inc`, SPEC.md §8.1.1, `tests/schacct.py`. The unconditional call
came out of `task_yield`'s `.save` stub; `sch_switch`'s `.pick` charges the
outgoing task behind `cmp dl, [sch_cur]`, with DX and BX pushed around it.
`sch_isr`'s call stayed where it was — it is what bounds the stale interval to
one tick, and that bound is what makes the skip exact.

Measured on `os8088_5150_cga_gla`, predicted against actual:

| | predicted | measured |
|---|---|---|
| `sch_account`, bare desktop | ~18/s | **17.0/s** (was 1,044.1) |
| idle accounting | 0.19% | **0.18%** (was 10.94%) |
| idle `ui_task` pass | ~4,030 cycles | **4,032** (was 4,536) |
| `.text` | ~+10 | **+10**, bss +0 |
| with the Task Manager open | unchanged | **1,426.4/s** (was 1,430.1) |
| the books | unchanged | **100.0%** of elapsed, both before and after |

`tests/schacct.py` is six assertions in the soak tier, and it was checked
against a reverted kernel: it fails on the rate row and on nothing else, which
is the point — **the rule is exact, so no counter, no screen and no snapshot
can see it and only the rate can.**

#### 6.1.1 What it did NOT buy, which is most of it

**The idle machine is still at 100%.** 4,032 cycles x 1,183.7 passes/s =
4,773,888 against the 8088's 4,772,727. The 10.7% did not become free time: on
a desktop with one runnable task there is nothing else to run, so the loop
reinvested every cycle in more passes of itself.

That is not a disappointment, it is §1.4 restated — **the accounting was never
the problem, it was the thing that noticed the problem** — and it is worth
writing down because the headline number invites exactly the misreading
CLAUDE.md warns about, a ratio of totals passed off as a measurement of a
design.

What stage 1 is actually worth, then:

- **It is exact and it is ten bytes.** Work that provably cannot matter is no
  longer done. That should just be true.
- **Poll granularity 1,028 → 1,184 Hz.** An event waiting on `ui_task` is
  picked up ~13% sooner. Small, real, free.
- **The idle profile is now honest.** `sch_switch`'s eight-slot scan is the
  top item at ~37% of a pass with nothing sharing the blame.
- **It stands alone.** If stage 3 is never built, this stays true and costs
  nothing.

And what it is **not**: a prerequisite for stage 3. Once `ui_task` blocks, the
switch rate collapses on its own and the accounting collapses with it whether
this rule is there or not. The two are independent, which is why this one
could land first and alone.

#### 6.1.2 …and stage 3 then subsumed it

Worth writing down, because it is the second thing this file got wrong about
§8.1.1 and because a reader will otherwise quote the 10.9%.

The rule pays in proportion to how many switches resume the caller. With
`ui_task` blocked and the Task Manager sleeping its interval, an idle machine
switches **53.8 times a second**, and reverting §8.1.1 today moves accounting
from 53.6/s to 72.2/s — **0.56% of the machine to 0.76%**, against the 10.94%
the same revert was worth before stage 3, and 14.98% with the page open.

So §8.1.1 is still exact, still ten bytes, and **no longer load-bearing**. Its
test is a correctness check now rather than a performance one, and
`tests/schacct.py`'s strongest row arms itself only when something on the
machine is spinning — which, since §28.7 retired the Task Manager's worker
loop, nothing on the shipped disk does. That row is left in because the day
something spins again, it is what will see it.

**The order still mattered.** Stage 1 was the cheap, exact, zero-risk change
that could land first and alone, and it was worth 10.9% for as long as it was
the only one built.

### 6.2 Stage 2 — the idle task — **BUILT, and inert**

`kernel/sched.inc`, `kernel/kernel.asm`, SPEC.md §8.1.2. A task whose body is
`sti`/`hlt`/`jmp`, spawned at boot; `sch_switch` skips its slot in the scan and
picks it where it used to resume the outgoing task. **+66 bytes of `.text`,
+1 of `.bss`**, and no RAM: it takes one of the seven slices `sch_stacks`
already reserves.

Inert, as designed and as measured: the scan runs `MAX_TASKS` iterations from
`cur+1` so it reaches `cur` itself last, and with `ui_task` always ready the
fallback is never taken. `tests/schacct.py` returns every figure unchanged and
`sch_cycles[idle]` reads 0.

It also cost a **one-line fix in `wm.inc`** that had nothing to do with the
scheduler: `wm_zoom`'s `jcxz .out` sat at displacement 113 of the 127 an 8086
short jump allows, and adding to `sched.inc` broke the build from across the
tree — NASM's jump optimiser reaches a different fixpoint when anything
shifts. It is now `or cx, cx` / `jz`, which has no range limit; `.out` is four
pops and a `ret`, so nothing reads the flags that costs. **The next person to
add sixty bytes anywhere would have hit it.**

### 6.3 Stage 3 — `ui_task` blocks — **BUILT, and the default**

`kernel/ui.inc`, `kernel/sched.inc`, `kernel/mouse.inc`, `kernel/events.inc`;
SPEC.md §8.1.2.1–§8.1.2.3; `tests/uiblock.py`. `.idle` is `task_sleep(1)`, and
`make NOUIBLOCK=1` puts the spin back — the A/B, and the only thing keeping
that path assembling.

**No new scheduler primitive was needed.** `task_sleep` writes `T_WAKE` then
`T_STATE`=2 and yields; `sch_isr`'s existing wake scan is the tick wake. All
that had to be added was the early wake and the idle task's yield.

| | `NOUIBLOCK=1` | default |
|---|---:|---:|
| `ui_task` passes | 1,134.6 /s | **17.7 /s** |
| `ui_task` CPU, bare desktop | 99.94% | **2.7%** |
| the idle task (halted) | 0% | **96.9%** |
| `sch_account` | 17.7 /s (0.19%) | 55.0 /s (0.58%) |
| the books | 99.6–100.4% | **99.6–100.4%** |
| `.text` / `.bss` | — | **+104 / +3** over stage 1 |

#### 6.3.1 THE POINTER IS NOT ui_task's TO DRAW, and that settled the veto

`mou_apply` — reached from `mou_isr`, in interrupt context — calls `cur_move`
itself on every motion packet. The arrow is **ISR-paced, not pass-paced**, and
blocking `ui_task` cannot slow it down. Measured both ways: 112 `cur_move`
draws against 113 over a scripted sweep, landing on the same coordinate. The
deferred case (`cur_dirty`, when a lock holder has the cursor hidden) is spent
by `gfx_unlock` on whichever task held the lock, which is also not the poll
loop.

What *is* pass-paced is the cursor's **shape** (SPEC.md §7.2), a held chrome
box tracking the pointer, and every dispatched event. Those are what the wake
path is for.

#### 6.3.2 The wake byte alone buys nothing

Only IRQ0 falls into `sch_switch`; every other ISR — the mouse's IRQ4 above
all — `iret`s straight back to what it interrupted, so a task an ISR has just
made ready would wait for the next **tick**. The fix is in the idle task, not
in any ISR: `sti` / `hlt` / **`call task_yield`** / `jmp`. It needs no change
to any ISR's return path, which is the alternative and is a re-entrancy
problem rather than a line of code.

Measured, 40 samples each, from the mouse ISR finishing a packet to the next
`ui_task` pass — the scheduler's own share of input latency, the 1200-baud
line's 22.5 ms of packet transit excluded because both kernels pay it:

| | median | min | max |
|---|---:|---:|---:|
| `NOUIBLOCK=1` (the spin) | 5.31 ms | 4.44 | 6.57 |
| default | **5.18 ms** | 5.03 | **5.90** |

**The blocking kernel is better on every statistic** — lower median and a
lower worst case. Without the idle task's yield the same figure is 32.4 ms
median and 54.5 max.

#### 6.3.3 The lost wakeup, which the first build had

Shipping it turned up the hazard §6.3 had predicted, and the prediction is the
only reason it was looked for: an ISR firing *during* a pass has already
delivered its event, and a pass that then sleeps sits on it until the tick.
Measured on the first build: median 5.20 ms and **one sample in fourteen at
54.15 ms**.

`sch_wake_ui` now also sets `sch_uiwake`, and `.idle` spends it with the test
and `task_sleep`'s `T_STATE` store inside **one** `cli` window (SPEC.md
§8.1.2.3). After: **worst wake 5.22 ms over 24 samples.**

It is a dedicated byte and deliberately not `[ui_post]`, which looks like it
should serve and cannot: §13.13's tick sweep sets `[ui_post]` on every tick
for the *next* pass, so a tick pass always reaches `.idle` with it set and the
UI task would never sleep at all.

`tests/uiblock.py`'s last row is a **maximum** for exactly this reason, and it
was checked against a kernel with the guard removed: median 5.15 ms, max 53.96,
row failed. A median cannot see this defect.

#### 6.3.4 The fail-safe, and what is still owed

**The masked-IRQ0 case is handled** (SPEC.md §8.1.2.1): `sch_idle_start` reads
PIC1's mask once and the idle loop spins instead of halting when IRQ0 is dead
— exactly what the machine did before the task existed. It is a boot-time
guard; a driver that masks IRQ0 later is out of its reach and is a driver bug.

**The keyboard wake is in** (SPEC.md §8.1.2.2): `kbm_isr` wakes the UI task at
its head, on every tier, because int 16h is polled once a pass and a blocked
UI task has no other reason to take one.

What is left is not measurable here:

- **A person looking at the pointer, the menus and a held chrome box on real
  hardware.** Every number above says it is fine and none of them is that.
- **`ui_arm_trk` and the cursor shape** are the two pass-paced things a hand
  would notice first.
- **KFZ's `KHB_STUCK`** is 546 ticks against a guaranteed 18.2 passes a
  second, so it clears — but it clears *because* the tick wake is there. A
  later "why wake on a tick nothing needs?" change disarms the one instrument
  that reports a wedged machine in the field.

### 6.4 Stage 4 — the load meter off the idle bucket — **BUILT**

`apps/taskmgr/taskmgr.asm`, `kernel/instance.inc`, SPEC.md §28.7,
`tests/tmload.py`. `tm_worker` sleeps its interval instead of spinning it, and
the meter is

```
load% = 100 - 100 * idle_cycles / total_cycles
```

off the same snapshot, the same interval and the same total the process rows
are shares of — so **the meter and the list cannot disagree any more.**

Measured with the page open on a bare desktop, three readings computed three
different ways:

| | reading |
|---|---|
| kernel (`sch_cycles` directly) | ui\_task 2.4%, **idle 88.8%**, TaskMgr 8.3% |
| the meter (`tm_load`) | **12%** — against 100 − 88.8 = 11.2 |
| the rows (`tm_pct`) | System **90%**, TaskMgr **9%** — 99% total |

**And the Task Manager's own share fell from 34–38% to 8.8%.** What is left is
real work — the snapshot, the walk, the paint — not spin.

The contradiction the last round could photograph is gone: the earlier
screenshot showed "CPU 1%" beside "TaskMgr 97%", and both were right, because
one was CPU time and the other was how much spinning got done.

#### 6.4.1 The spin WAS the instrument, which is why it could not just go

`tm_worker` counted `{ count += 1; yield }` iterations over `TM_INT` ticks and
scored them against a rolling maximum over two epochs, because a spin count
has no absolute scale — nothing in it says what "idle" is worth on *this*
machine, so it had to be discovered, re-discovered, and protected from being
poisoned by a cheap poll loop elsewhere. Deleting it needed a replacement
first, and stage 2's idle bucket is that replacement.

It came out **smaller**: `TM_EPOCH`, `tm_cnt`, `tm_cmax`, `tm_pmax`, `tm_epc`
and `tm_t0` are all gone, and the package lost 76 bytes.

#### 6.4.2 Naming the idle slot cost no ABI

The page has to know *which* slot is idle. A slot index in the `SS_*` header
would have cost a byte the header has not got, and every field after it moving
is an ABI break for a structure `SYS_SNAPSHOT_SIZE` sizes in every package's
bss. So the kernel gives it a **fourth `T_STATE` value in the snapshot** — 3,
where its own task table records 1/ready like anything else. No byte moved,
and `apps/taskmgr` is the only reader of that array in the tree.

#### 6.4.3 Idle folds into "System"

The idle task owns no instance, so it belongs to no application, and its
cycles go into row 0 beside the UI task's. A machine that is 97% idle reads as
a System row of 97% — which is the older Windows Task Manager's shape and what
a reader expects. It also keeps the rows summing to the whole machine, which
is what lets the meter and the list share a denominator.

The alternative — leaving idle out of every row — is what the page did between
stages 3 and 4, and it reads as every application using far more of the
machine than it does, because the rows are then shares of the *busy* time
only.

### 6.5 Stage 5 — §8's patching, optional

Only worth doing after 1–4, and only for the case they do not reach: **two
runnable tasks with nobody listening.** See §8 for what it is worth there and
what it costs.

## 7. Open questions

- **What is left after stage 4 is one case: two runnable tasks with nobody
  listening.** Measured, the Task Manager open costs 1,430 `sch_account` calls
  a second — **14.98%** — and every one is a real switch, so §2.3 saves none
  of it. That is fine while the Task Manager is the second task, because
  somebody IS listening. It is not fine for Tracker's mixer, or a driver's
  service worker, running with the page shut. §8 is the only thing here that
  reaches it.
- Does the accounting bill the **tick's own ISR time** to whoever it
  interrupted? It does, and that is probably right, but say so on the page
  rather than leaving the Task Manager quietly charging `snd_tick` to the
  front window.
- ~~Should the idle task be visible on the Task Manager's process page as
  "System"?~~ **Answered in §6.4.3: yes, folded into row 0.** It keeps the rows
  summing to the whole machine, which is what lets the meter and the list share
  a denominator, and it is the shape a reader already expects.

---

## 8. Patching the call sites, behind a knob

Reconsidered rather than refused. It is the only option in §2.2 that costs
**literally zero** when off, and the objection — that it breaks a self-
validating instrument — is a bill that can be paid rather than a wall.

### 8.1 What it is worth, honestly

After stages 1–4 the cases divide:

| machine | `sch_account` | §2.3 | §8 |
|---|---:|---:|---:|
| idle desktop | 18.2/s | 0.19% | 0% |
| Task Manager open (measured) | 1,430/s, 14.98% | 14.98% | n/a — somebody IS listening |
| two tasks, page shut, **post-redesign** | whatever real work switches at | proportional | **0%** |

The third row is the one it exists for, and its size depends on the workload:
a MOD mixer yielding per audio buffer switches perhaps 100 times a second,
which is ~1% of the machine. **~1% is the honest figure**, not the 14.98%,
because a spinning worker is a bug the redesign fixes rather than a cost
patching should paper over.

It is worth building anyway for a reason the accounting does not own: it is a
**mechanism**, and "retire this check for the boot" is a pattern this kernel
already reaches for by hand (`[mou_ptr]` retiring `kbm_ui`, `[fdlg_win]`
guarding the dialog reap). The accounting gate is its first customer.

### 8.2 The mechanics, and the two 8086 traps

The site is a 3-byte near `call` patched to three `nop`s and back. Both traps
are real on an 8088 and neither shows on a fast emulator:

- **Patch under `cli`.** Three byte writes are not one atomic operation, and
  every intermediate state is executable garbage — `90 xx xx` is a nop
  followed by a stray displacement, `E8 xx 90` is a call to a random address.
  An IRQ that reaches the site mid-patch takes it. IF=0 for the write closes
  it; there is no second CPU to worry about.
- **Flush the prefetch queue.** The 8088's BIU runs up to 4 bytes ahead, so a
  patched byte already in the queue executes in its old form. Any jump flushes
  it, and the patcher must take one before returning. It cannot self-hazard
  (the patcher is not the patched code), but the discipline is cheap and the
  failure is a one-in-a-thousand ghost.

### 8.3 `NOSMC=1`, and buying the instrument back

Two things, and the second is the one that matters:

- **`NOSMC=1`**, stamp-tracked like every other knob in the Makefile: the
  sites assemble as plain unconditional calls and nothing writes to `.text`.
  This is the A/B for "is the patching what broke it", and — as with `NOBAND`
  and `NOPLANE` — it is also the only thing that keeps the unpatched path
  assembling.
- **`os88marty.py verify` learns the patch table.** A field dump comes off a
  *shipped* kernel, so the knob does not help there; what does is teaching
  `verify` the N site addresses and their two legal byte patterns. Then a
  shipped kernel still self-validates, every other differing byte is still a
  finding, **and the dump additionally reports whether accounting was armed
  when it was taken** — which is information the instrument does not have
  today. Done that way the instrument comes out ahead.

The table has to be generated, not hand-maintained: a site list that drifts
from the code turns `verify` into a check that passes for the wrong reason,
which is worse than the diff it replaced.

### 8.4 Arming

`osapi_sys_snapshot` patches the sites in and sets a TTL; the tick ISR counts
it down and patches them out. Two things follow from stage 1 and must not be
forgotten:

- **Re-stamp `sch_pit_last` on the arming edge**, or the first slice after a
  quiet period is charged the entire quiet period.
- `sch_isr`'s own call is one of the sites, and it is what bounds the stale
  interval to one tick (§6.1). With it patched out the interval is bounded by
  the TTL instead — set the TTL well under the 32-bit timestamp's ~65,536-tick
  wrap and say so at the constant.
