# The UI freeze during disk I/O — what it is, and what it would take to end it

It answers three questions asked in that order: why the machine freezes for
disk I/O, what it would take not to, and what it would take to keep only the
pointer alive.

**§5 IS NOW BUILT — SPEC.md §7.4 is the contract and this section is the
costing behind it.** §4 is not: it remains an investigation, and nothing in it
has been started. What shipped is the cursor, at **180 bytes** (`.text` +150,
`.cold` +30, no rung crossed - 125 before §5.3.3's three additions), behind `NOCURDISK=1` whose build is
byte-identical to the kernel before it. Two things in §5 as first written were
wrong and are corrected in place: the hider was **`menu_draw_bar`**, not
`fpg_paint` (§5.3), and the three extra safety conditions the ISR needs were
not in the sketch at all (§5.3.1).

**Read first:** SPEC.md §7 (the concurrency model), SPEC.md §12.8/§12.8.3
(the progress widget and what its extent is), SPEC.md §18 (the disk is the UI
task's), SPEC.md §18.9.3 (the batch bracket), SPEC.md §7.1.4–§7.1.4.4 (the
deferred hide, and the two sections that are OFF by default because the eye
disagreed with the instruments).

---

## 1. The answer in one paragraph: it is TWO locks, and only one of them is the disk's

A file operation stops the machine because two independent things are held at
once, by two different pieces of code, for two different reasons:

| | held by | what it stops | for how long |
|---|---|---|---|
| **`[sch_lock]`** | `dsk_xfer`, `inc`/`dec` around the whole transfer | involuntary task switching — **no other task runs at all** | one `disk_read`/`disk_write` call |
| **`[gfx_lock_flag]`** | the window callback, taken by `ui_task` *before* it dispatches the event | all task-level drawing, and the mouse ISR's right to move the arrow | the whole event handler |

The second is the bigger one and it is not disk code. `ui_task` takes
`gfx_lock` around the handler because the handler draws (kernel/ui.inc has
about thirty such sites); the handler then does disk I/O in the middle of that
hold. `disk.inc` says so itself — *"The gfx lock is NOT held across disk I/O by
`disk.inc` itself — a window callback that calls the §18.4 write path holds it,
and accepts the stall."*

So **the freeze is one `gfx_lock` hold**, which SPEC.md §12.8.3 already states
in as many words and builds the progress widget's whole lifetime out of. The
disk's own lock is the smaller, shorter, better-justified half.

There is a third effect that is easy to miss and is what makes the freeze
*look* total: once an operation moves `FPG_WARM` = 3 sectors inside one hold,
`fpg_busy` arms the progress widget, `fpg_paint` calls `cur_unlazy`, and
`cur_unlazy` calls `cursor_hide`. **The arrow does not merely stop — it leaves
the screen** for the rest of the freeze.

## 2. What is actually stopped, and what is not

Worth being exact, because three of these are commonly assumed to stop and do
not:

| still running | why |
|---|---|
| **IRQ0** | `sch_lock` makes the tick decline to *switch*; it still counts, still chains the ROM (the floppy motor needs it), still runs `sch_account`, still marks sleepers ready |
| **the mouse ISR** | interrupts stay on through `int 13h`. `mouse_x`/`mouse_y`/`mouse_btn` stay fresh the whole way, and clicks are queued into the event ring with their real birth `[ticks]` |
| **`fpg_step`** | called from inside `dsk_xfer`'s per-sector notch loop — the one piece of task-level code that still runs |
| **the CPU, mostly idly** | SPEC.md §15.3.8, measured on the 5150: 83% of a load has CS = F000, the longest unbroken in-ROM run is **21 consecutive timer interrupts with IF set throughout**, and the ROM in there is not computing — it has handed the transfer to DMA and is spinning on IRQ6 |

| stopped | why |
|---|---|
| every other task | `[sch_lock]` |
| every window's painting | `[gfx_lock_flag]` |
| the arrow's *motion* | `[gfx_lock_flag]`, and `[fpg_on]` once the widget arms (SPEC.md §12.8.4) |
| the arrow's *presence* | `fpg_paint`'s `cur_unlazy` |

**The last row is the one worth attacking first and it is not a concurrency
problem at all.**

## 3. Why each half exists — separating the shortcut from the load-bearing

Four reasons are usually collapsed into one. They are not equally hard.

### 3.1 `[sch_lock]` — three real hazards, one cheap answer

1. **`int 13h` is not re-entrant**, and neither is the BDA it works out of. Two
   tasks inside it at once is corruption.
2. **The whole FAT layer is single-instance global state**: `dsk_secbuf`,
   `dsk_bpb`, the FAT snapshot in `FAT_SEG`, `disk_dir`, `disk_icons`,
   `disk_drive`, `disk_spt`/`disk_heads`, `[dsk_cwd]`, the §18.95 read-ahead
   cache. A second concurrent caller does not race on a sector, it races on
   *which volume the machine is standing on*.
3. **`[mem_pinseg]` pins the destination claim** for the length of the
   transfer, because `ES:BX` is walked across calls that themselves claim.
   It is one word, saved and restored as a nest — not a set.

`sch_lock` answers all three at once by making concurrency impossible rather
than safe. That is a shortcut, and a defensible one: **a mutex that yields
would answer all three too**, at the price of the state in (2) needing an owner
and (3) needing a real pin rather than a current one.

### 3.2 `[gfx_lock_flag]` — this one is architectural, and one part of it is a correctness guarantee

The handler holds the lock because it draws before and after the I/O, and the
clip region, the pen, the display context and the batch bracket all die at
`gfx_unlock` by design. Releasing it mid-operation is not a matter of moving
two calls:

- **the clip region dies** (SPEC.md §11.3 rule 1), so the handler must re-arm;
- **the window can change underneath** — another repaint, a drag, a close;
- **and the batch bracket ends** (SPEC.md §18.9.3). This is the load-bearing
  one. The bracket's entire safety argument is *"any unlocking of the user
  interface ends a batch"*, resting on **"what a user can do between two reads
  of a batch is bounded by what they can do while their machine is frozen,
  which is nothing."**

**So the freeze is not only a scheduling shortcut. It is the removable-media
consistency model.** A live UI is a UI in which the user can change the floppy
in the middle of a copy. Ending the freeze means that assumption goes, and
every multi-read operation has to re-validate a BPB it currently gets for free.
No design below may skip that line.

## 4. Option 1 — not freezing. Three shapes, cheapest first

### 4.1 Slice the operation and go round the event loop (recommended)

**The machinery already exists and is shipped.** `fcp_step` (SPEC.md §22.3)
already runs an arbitrarily long recursive copy as a resumable state machine:
every byte of its state is in `.bss`, its walk keeps an explicit `FCP_MAXD`
frame array rather than a call stack, and it already **suspends to the event
loop and resumes** — today on a *question* ("Replace this file?"), through
`fcp_answer`. `EVT_WAKE` is already the return path: SPEC.md §74.1's window
wake is *"popped by the UI task and dispatched to the window's `W_ONWAKE`
handler WITHOUT the gfx lock"*, and it coalesces, so it cannot fill the ring.
`[dsk_lstale]`/`dsk_relist` already exist to reconcile the listing on *"every
path that can return to the event loop"*, and `fcp_step` already re-opens its
batch on the way back in.

So the change is: make `fcp_step` return a fourth answer — **`FCPS_MORE`,
"my slice is spent"** — beside `DONE`, `ASK` and the failure, post a wake, and
return. The unlock that follows is the one it already performs for a question.

What it buys: between slices the machine is genuinely live — other tasks run,
windows paint, the pointer moves, the menu works.

What it costs, honestly:

- **Choosing the slice.** A slice must end at a point the state machine can
  already resume from, and its natural grain is one `fcp_file1`/`fcp_file2`
  chunk. `FCP_MAXKB` is 64KB, so on a slow floppy one chunk is still seconds.
  A finer grain means resuming mid-file, which is state `fcp_` does not keep.
- **Re-validation.** Each slice re-opens the batch, so the first read of each
  slice pays a BPB check per volume it touches. That is exactly what the ask
  path pays today, and SPEC.md §18.9.3's table prices it: the whole install's
  BPB phase is 2 calls, so this is small — but it is not nothing, and it grows
  with the number of slices.
- **What the user can now do mid-copy** is the whole of the risk, and it is
  the §3.2 problem arriving in its cheapest form: they can swap the disk, hit
  Refresh, unmount, or start a second copy. The re-validation covers the
  first; **the second copy needs a refusal** (the clipboard is one
  `[fcp_op]`), and unmount-under-a-live-operation needs `dsk_vol_del` to
  either refuse or abort the operation.
- **The progress widget's whole lifetime is wrong afterwards.** `[fpg_on]` is
  cleared by `fpg_finish` at `gfx_unlock`, deliberately, so that no caller can
  leave it up. A sliced operation unlocks every slice, so the widget would be
  taken down and rebuilt per slice — a flash, once per slice, of exactly the
  kind PERFORMANCE.md rule 2 exists to forbid. **The widget needs a second
  owner: the operation, not the hold.** This is a real redesign of
  SPEC.md §12.8.3 and should be costed with the rest, not discovered later.

### 4.2 A disk service task, with callers blocking on it

Move `dsk_xfer` onto a task of its own; callers post a request and block.
`sch_lock` becomes a mutex that yields, exactly as `gfx_lock` does.

This is the shape people reach for first and it is worse than §4.1 here:

- it needs a **new blocking primitive** — the scheduler has `task_yield`,
  `task_sleep` and `task_exit` and no wait/signal at all;
- it needs `SCH_STACK` (384 bytes) for a task that spends its life in the ROM,
  against a table of `MAX_TASKS` = 14 and docs/plans/completed/STACK-SLOTS-PLAN.md's finding
  that the floor is what binds;
- it inherits **all** of §3.1's global state as a shared resource rather than
  removing the problem, so the FAT layer still needs an owner;
- and it does **not** on its own unfreeze anything, because the caller is
  still the UI task holding the gfx lock. It has to be combined with §4.1 or
  §4.3 to be worth a byte.

**It only pays when a non-UI task needs the disk** — which is SPEC.md §20.6
rule 7's *"a worker may not touch a file"*, and the reason `apps/ftpd`'s
worker-stages/UI-commits handshake exists. Relaxing that rule is the real
prize here, and it is a separate argument from unfreezing the desktop.

### 4.3 Genuine concurrency — the UI live while I/O runs on another task

Everything in §4.2, plus: per-request FS state instead of the single-instance
globals in §3.1(2), `[mem_pinseg]` becoming a set or the destination becoming a
genuinely pinned claim (SPEC.md §66), and the mount/refresh/unmount paths all
learning that an operation may be in flight.

**Not recommended, and the reason is a measurement rather than taste.**
SPEC.md §15.3.8 measured what the CPU is doing inside `int 13h` on the 5150:
waiting on DMA, with 21 consecutive ticks taken and not one lost. There is
real time to reclaim — but the machine is a 4.77 MHz 8088 whose disk work is
already priced in *revolutions*, and docs/plans/completed/HANDOFF-DISK-IO.md got the install
from 356 `int 13h` calls to 114 without touching concurrency at all. **The next
3x is far likelier to come from another round of that than from running the
desktop during the wait**, and it costs no new failure modes.

## 5. Option 2 — leave the cursor unfrozen

This is a much smaller change than §4 and it is **not a subset of it** — it
touches neither lock.

### 5.1 The trap that has to be cleared first

**A visible but frozen arrow is a known regression, already measured and
already rejected.** SPEC.md §7.1.4.2 shipped a change that kept the arrow lit
through lock holds; the field reported *"the cursor freezes and jerks as it
moves"*, and SPEC.md §7.1.4.3 has the A/B on a cycle-accurate 5150 — identical
lock behaviour, 4% of holds arrow-lit against 96%, opposite visibility. Its
conclusion is the sentence this whole option has to answer:

> **hidden and stuck** — the eye gets a blink and never sees the pointer lag
> the hand. **lit and stuck** — it sits still while the hand moves and then
> teleports. Same timing to the microsecond; the second one is what a person
> calls a stutter.

Both §7.1.4.2 and §7.1.4.3 are **off by default** (`CURFIX=1`) for that reason.

**So "un-hide the arrow during a file operation" is not the option. The option
is "make the arrow track the hand during a file operation", and anything short
of that is a change the project has already taken and reverted.**

### 5.2 What makes it possible, and it is measured

SPEC.md §15.3.8, on the field machine: `int 13h` runs with interrupts enabled,
**not one IRQ0 was lost across a whole load**, the CPU is idle inside the ROM
waiting on DMA, and *"a frame drawn from the timer ISR costs no real time at
all — it is drawn in a gap the machine was going to spend idle."* The boot
splash's spinner already animates from IRQ0 **during** `int 13h`, and ships.

The mouse ISR is already running during the transfer and already decodes every
packet; the only thing it declines to do is draw. So the cost of moving the
arrow during a file operation is the cost of `cur_move` alone — the pair
measured at 5.41 PIT counts on Hercules (SPEC.md §7.1) — spent in a gap that
is otherwise idle.

### 5.3 The shape — as built

Three pieces, and they are independent:

1. **A "the CPU is in the ROM and nothing is drawing" flag.** Two stores
   bracketing the `int 0x13` instruction in `dsk_xfer` — and nothing else, so
   the safety argument is a line of code rather than an audit: the path from
   `.attempt` to `int 0x13` draws nothing, so no primitive can be in flight.
   `mou_apply`'s gate becomes *"the lock is free and `[fpg_on]` is clear, **or**
   this flag is set"*, with the `cur_level` test unchanged. Note the flag must
   live in `.text` with a real initialiser, not `.bss` — `-f bin` zeroes
   nothing, and `drv_boot` reads the disk before any init routine runs
   (`fprog.inc` has the same constraint for the same reason).

2. **Something must stop spending `gfx_lock`'s promised hide** — and the
   obvious candidate was **wrong**. `fpg_paint`'s `cur_unlazy` looks like what
   removes the pointer, and making that one call conditional was built first
   and moved the lit share **from 0% to 0%**. Sampling the kernel through a
   real operation says why: the arrow tracked perfectly until the instant
   `[fpg_on]` went to 1, and `fpg_arm` calls **`menu_draw_bar` before
   `fpg_paint`** — an unclipped composition, so `GFXCLIP`'s own `cur_unlazy`
   had already hidden it. Two painters, and the fix was aimed at the second.

   What shipped puts the rule where it is true of both: **`[cur_barok]`**,
   "the painter running now is confined to the menu bar", set by fprog's five
   public drawing entries and read once in `cur_unlazy`. `cur_lazyck`'s trick
   with a fixed rect instead of a window frame, and one test in one routine
   rather than one per call site.

3. **And then the arrow can move *into* the bar**, which it could not before,
   because the hand is now live during the operation. `fpg_step`'s fill would
   draw over a lit arrow and make the save-under a lie — SPEC.md §12.8.4's
   third bullet, exactly. Cheapest answer: the ISR, when drawing under the
   flag from (1) with `[fpg_on]` set, defers if the new cell meets the widget's
   strip. The arrow then stalls if the user parks it on the progress widget,
   which is a corner, and it is the *hidden-and-stuck* treatment §7.1.4.3
   prefers rather than the lit-and-stuck one it rejects.

#### 5.3.1 Three conditions the sketch above did not have

The flag says *this task* is in the ROM. It does not say the screen is safe,
and each of the three tests `mou_apply` gained closes a hole that was open in
the sketch (SPEC.md §7.4.2): the lock must be **held by us** — another task's
hold means it was pre-empted mid-`gfx_fill` with its `vga_rect_setup` scratch
and GC state live; **no clip region may be armed** — a painter that already
asked `cur_lazyck` and was told the arrow was out of reach spends that answer
*after* the read; and the move must not land on the widget. The lock-free case
is deliberately left to the ordinary gates: with the lock free `[fpg_on]` is
the only thing between IRQ4 and an unlocked `fpg_step` fill on another task
(SPEC.md §12.8.4), and that is not a guard to step around. **Superseded by
§5.3.3 below**: that arm was measured, was where almost every operation a
person notices lives, and is covered now (SPEC.md 7.4.2.1).

#### 5.3.3 Half-built is what one scenario looks like

§5.3 as first shipped was **verified and wrong**, and the way it was wrong is
worth more than the fix. `tests/curdisk.py` drove one operation — a folder
open — and reported 28 cursor moves under a held lock against `NOCURDISK=1`'s
0. Every number in it was true. The field then reported that *the only* thing
whose pointer moved was a folder open, and named five operations that still
froze.

A folder open is the one case in the machine that reads with the gfx lock
**held**. Everything else on the list reads with it **free** (a package launch,
an assoc open, a package's file dialog) or has already painted before it reads
(the Control Panel, a copy, the installer). The test asked the one question the
code answered.

Three causes, and the sampling found each one in a single run:

| case | what the samples said | what it needed |
|---|---|---|
| package launch, assoc open, a package's file dialog | `gfx_lock_flag` 0 and `cur_inxfer` 1 for 80 samples of 80, arrow up and never moving | SPEC.md §7.4.2.1 — the lock-free arm, which §7.4.2 had **refused on purpose** |
| Control Panel, a copy, the installer's panel | `cur_level` −1 for the whole freeze, no clip region, lock ours | SPEC.md §7.4.3.1 — `fpg_arm` puts the arrow back and re-arms the promise |
| the hard-disk install's own transfers | never bracketed at all — a `DVK_DRV` volume leaves `dsk_xfer` before the `int 13h` loop | SPEC.md §7.4.1.1 — bracket the driver's block verb too |

The registered row now drives **two** scenarios, one either side of the lock.
That is the smallest change that would have caught this, and it is the general
lesson: **a gate that drives one path measures one path**, and here the one it
drove was the exceptional one.

#### 5.3.2 What it cost, measured

**180 bytes** — `.text` +150, `.cold` +30 — no rung crossed, and
`make NOCURDISK=1` assembles **byte-identical to the kernel before the
change** on both `kernel.bin` and `kernel-full.bin`. (It was 125 before
§5.3.3's three additions.) One byte of that is a
`jmp short .attempt` in `dsk_xfer` that had to widen: the two stores put the
label out of a byte's reach on the `DISKCNT=1` build, which nothing ships and
only `make test-full` compiles.

`tests/curdisk.py` is the A/B, on `os8088_5150_cga_gla` opening `B:\SYSTEM`:

| scenario | | default | `NOCURDISK=1` |
|---|---|---|---|
| folder open (lock **held**) | moves during the freeze | **28** | **0** |
| | arrow lit, widget phase | **19 of 24** | **0 of 23** |
| package launch (lock **free**) | moves during the freeze | **37** | **0** |
| | arrow lit, widget phase | 63 of 84 | 64 of 85 |

**The launch's lit share does not separate the arms, and that is the shape of
the defect rather than a null.** A launch reads with the lock free, so nothing
ever promised a hide for anything to spend: the old kernel's arrow was **lit
and frozen** for the whole of it — §7.1.4.3's rejected state, reached by a
different door. Motion is what separates them there, which is why `moves` is
the assertion and the lit share is a second reading beside it.

The last part of a hold is legitimately hidden on both arms: that is when the
window starts repainting its list, and a painter that is not bar-confined
**must** take the arrow down (SPEC.md §7.1.4).

### 5.4 What it buys, and the honest ceiling

Updates arrive at the mouse's own report rate — a 1200-baud packet is ~25–40 ms
(SPEC.md §7.1.4.3), so ~25–40 Hz — *for the whole of every `int 13h`*. That is
tracking, not stepping.

The alternative that needs **no ISR change at all** is to step the cursor from
`dsk_xfer`'s existing `.notch` loop, beside `fpg_step`. It is smaller and it is
not good enough: an `int 13h` in a coalesced run is ~400 ms on the field
machine (PERFORMANCE.md Part 2), so the arrow would update at **~2.5 Hz** — the
teleporting pointer of §7.1.4.3 with extra steps. **If only one of these is
built, it must be the ISR one.**

What it does *not* buy: nothing paints, no window moves, no menu opens, no
other task runs. The machine still cannot be *used*. It is feedback — the same
argument SPEC.md §12.8 makes for the widget itself: *"what was missing was not
concurrency but feedback"* — one step further along.

## 6. What to measure before building either

1. **Is a live pointer during a load actually better?** This is a look
   question with a rejected precedent behind it (§5.1), so it is settled on
   glass and not by a counter. SPEC.md §7.1.4.3's closing rule binds: a cursor
   change needs a **moving-pointer** reading and a **still-pointer** reading,
   and they are different instruments. `make combo` builds the pair of field
   disks for exactly this comparison.
2. **Does drawing inside `int 13h` cost the transfer anything?** §15.3.8 says
   no, on a Hercules 5150 with a GLaBIOS ROM, for IRQ0. It should be re-taken
   for **IRQ4** and for the period IBM ROM before the flag ships, because the
   mouse ISR is longer than a splash frame and the two ROMs differ by 1.61x
   per call. `make DISKCNT=1`'s counters plus a cycle count over a fixed read
   is the measurement, and it needs no new instrument.
3. **How much of a real operation is one `gfx_lock` hold?** Everything in §4
   is priced by how many slices an operation splits into, and nothing here
   knows that number yet. `[gfx_lock_flag]`'s duty cycle over an install and
   over a package launch is the input to §4.1's slice grain.

## 7. Refusals and negatives already on record — do not re-derive these

- **A visible-but-frozen arrow is worse than a hidden one** when the hand is
  moving. Measured, twice, and it is why `CURFIX` is a knob (SPEC.md §7.1.4.3,
  §7.1.4.4).
- **An unlocked painter and the mouse ISR are two painters.**
  `[gfx_lock_flag]` = 0 is not "nobody is drawing", it is the one state in
  which IRQ4 draws. This cost a real field defect (SPEC.md §12.8.4,
  docs/FIELD-NOTES.md §34) and any change that lets the ISR draw in a new
  window is arguing against that section directly.
- **A second buffer buys nothing while the disk holds the scheduler.**
  SPEC.md §77's FTP two-stage path measured an order of magnitude fewer passes
  and **no change in wall clock**: *"While the UI task is writing, no task runs
  at all. There is no concurrency for a second buffer to exploit, and there
  never was."* `FD_STG2` is 0 and the path is kept, gated, *"worth exactly what
  it costs the day the disk stops holding the scheduler"* — so §4 has a
  waiting consumer, and that is the strongest argument in its favour.
- **A motor-timeout gate for "is this the same floppy" was proposed and
  rejected** (SPEC.md §18.9.3). Do not reintroduce it as the answer to §3.2.
