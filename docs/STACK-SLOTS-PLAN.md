# Task stack slots — where the bytes actually go, and what a class scheme buys

**Design not started.** This is a handoff. It exists because a session was
asked why a fresh boot with the sound driver off opens only **six** programs
that want a worker when `MAX_TASKS` is 8, and the answer — the idle task takes
one of the seven dynamic slots — turned into the larger question of whether the
per-task slice has to be as big as it is.

Every number below was measured on this tree under QEMU with the shipping
kernel, on the base that carries the `origin/main` merge. **QEMU counts work
exactly and cannot time it** (CLAUDE.md), and stack depth is work, so this is
the right instrument for everything here except the one correction §9 names.

---

## 0. The headline

**A task's slice is not sized by what the task does. It is sized by what
lands on it.**

An idle desktop's idle task — whose own footprint is at most 4 bytes — reaches
**82 of 384**. A package worker that does nothing but spin reaches **88**.
Six Bounce instances reach **92–106**. The Fractal's drawing worker reaches
**142**, which is the same figure docs/KERNEL-MEMORY.md recorded for it before
any of this.

So of the Fractal's 142, about 82 belongs to no program at all. It is the
interrupt floor, and **every task pays it simultaneously** — seven times over
in `sch_stacks`.

**The single largest item in that floor is the BIOS.** `sch_isr` chains to the
ROM's `int 08h` on every full tick, with `pushf` / `call far [sch_old08]`, and
that call runs the ROM's handler on whichever task stack it interrupted.
Measured by A/B on a bare desktop: **82 with the chain, 32 without it.**

**The BIOS timer chain costs 50 bytes of every task stack in the machine.**

And the second headline, which took §12's survey to see: **the programs are
genuinely spread across the classes.** Twenty shipped workers, from ten bytes
(`CWORD`'s, which only sleeps) to 240 (Frotz's, twenty-two levels deep), with
eleven of them in one class. That is what makes a partitioning scheme worth its
flag day — and what turns **six usable slots into twelve, in the bytes
`sch_stacks` already costs** (§7.3).

---

## 1. What was measured, and how

`task_spawn` fills every slice with `0xCC` before it writes the canary and
carves the frame (§8.3), on the shipping kernel and not only a `KFZ=1` one, so
each slice carries its own high water. Three readers were used:

- `tools/stkwater.py` against a live QEMU. **Its `DEF` is `("KFZTRACE",)`**, a
  `KFZ=1` build, so `os88sym` refuses on a plain kernel and every reading here
  went through a five-line driver that passes no defines. That is a wart worth
  fixing when somebody is next in the file.
- `tests/stackprobe` — the package built for this. Its worker refills its own
  slice and then **spins**, so its reading is the floor plus its own two
  `OSAPI` calls and nothing else. It also reports every other slice.
- A word-by-word dump built on `stkwater.annotate()`, which names only the
  words a `call` actually ends at.

**The stress protocol matters and must be identical between arms.** A high
water is a sample of a nesting distribution, not a bound: an early
`NOBIOSTICK` run read 102 where the controlled re-run of the same build read
76. Every A/B below is 239 scans of the same scripted sweep — ten mouse
positions and ten keys, eight rounds — with the probe launched the same way.

### 1.1 The readings

| task | high water of 384 | above the floor |
|---|---|---|
| idle task (own footprint ≤ 4 bytes) | **82** | — it *is* the floor |
| `tests/stackprobe` worker (spins) | **88** | +6 |
| Bounce ×6 | **92–106** | +10…24 |
| Fractal drawing worker | **142** | +60 |
| Tracker streaming worker (docs/KERNEL-MEMORY.md) | 142 | +60 |
| ftpd's worker carrying `ETHER.DRV`, QEMU after an FTP session | 232 | +150 |
| the same slice, **5150 field**, upload + typing | 220 | — |

**The last two rows were labelled "`ETHER.DRV` service worker" until §12.0.**
The driver spawns no task at all — `OSAPI_DRV_TASK` has exactly one caller in
the tree and it is the sound driver — so the slice measured was `FTPD.O88`'s
own worker, carrying the driver's ~126-byte verb chain on top of itself. The
number is right; only its owner was wrong, and it matters because a *program*
cost is something a class can be cut for (§12.2).

The idle task is the control that makes the rest readable. Its body is

```
    pushf / cli / cmp / sti / hlt / mov / popf / sti / call task_yield
```

and `task_yield`'s first two instructions are `pushf` / `cli`, so **the only
windows in which an interrupt can reach it leave it 2 or 4 bytes deep.**
Whatever it reads above 4 is not its own.

---

## 2. What is actually on the stack

Decomposed by A/B rather than by reading, because the dead region a high water
leaves is not contiguous with the parked frame and cannot simply be walked:

| component | bytes | how it was established |
|---|---|---|
| the interrupted task's own depth (idle) | ≤ 4 | source: `pushf` then `cli` |
| CPU frame + `sch_isr`'s nine register pushes | 24 | `SCH_FRAME`, source |
| `sch_switch`'s `push dx` / `push bx` / `call sch_account` | ~4 | source; `sch_account` is a leaf |
| **the ROM's `int 08h` chain** | **50** | bare desktop, 82 → 32 with the chain removed |
| the mouse path (`mou_isr` → `mou_apply` → `cur_move`) | ~58 | idle read 62 with the chain removed and the mouse driven |

Two ISR paths, and **they cannot nest on each other** — `mou_isr` runs with
IF=0 throughout and never `sti`s — so the floor is the deeper of them, not
their sum:

```
   floor today   = max(tick 28 + BIOS 50, mouse 58)  =  78, + own  =  82
   floor without the BIOS chain
                 = max(tick 28,           mouse 58)  =  58, + own  =  62
```

Ruled out along the way, each by measurement rather than argument:

- **`snd_tick`.** It calls `drv_svc_call` from inside the ISR, which looked
  like the deep one. Booting with no sound card at all changed the probe's
  reading by **zero** (88 → 88). With no driver loaded the call returns at its
  first test.
- **`sch_account`.** A leaf: no pushes, no calls. It costs the 2 bytes of its
  own return address.
- **The splash.** `sch_isr`'s `call far [spl_ifp]` was the first suspect for
  the idle task's residue, on the theory that the idle task is spawned early in
  `kmain` and the deep bytes were boot-time. `tests/stackprobe` refutes it: a
  worker spawned at the desktop, long after the splash is gone, reads 88.

---

## 3. Is there a common base? (yes, and it is bigger than the programs)

Every task carries the same two things, and neither is the program:

1. **24 bytes at the very base, by construction.** `task_spawn` carves
   `SCH_FRAME` at the top of the slice for every task alike.
2. **The interrupt floor, ~82 bytes,** which any task can be made to pay at any
   moment because interrupts land on whichever stack they interrupt.

The program's own contribution is the *small* term for everything except the
network stack and Frotz: **+6** for a spinning worker, **+10…24** for a Bounce,
**+60** for the Fractal. A socket verb's **~126** — which is the driver's and
lands on the *calling* package's slice, not on a task of its own (§12.2) — and
Frotz's **240** (§12.3) are the two outliers, and between them they are what
sets `SCH_STACK` today.

That is the finding that governs the rest of this document. Shaving a program
is worth its own bytes once. Shaving the floor is worth its bytes **seven
times**, and is the difference between the small classes in §6 being possible
and being arithmetic.

---

## 4. What is compressible

### 4.1 The ROM tick chain — 50 bytes × every slot

> **BUILT. SPEC.md 8.5 is the contract and this is the design record behind
> it.** It ships as the default; `NOCHAINPRIV=1` is the A/B and arm 3 of
> `make stkdiag`. Cost on the tree that shipped it: `.text` +36, `.bss` +5,
> `.lowbss` +128 — **169 bytes resident, no rung crossed**.
>
> **Measured end to end on the shipping kernel** (QEMU/SeaBIOS, the 90-second
> run): the idle task's slice — the floor — reads **32 of 384 on arm 1 against
> 82 on arm 3**. Fifty bytes exactly, agreeing to the byte with §2's original
> A/B, which reached the same number by deleting the call rather than by moving
> it. The chain's own high water on `sch_chstack` reads **56**, which is the
> same figure the prototype instrument measured and is what sizes `SCH_CHSTK`
> at 128 (2.29×).
>
> **`[sch_chskip]` reads 0** over that run, which is §4.1's open question
> answered rather than argued — see the note at the end of this section.

`kernel/sched.inc`, in `sch_isr`:

```nasm
.full:
    pushf                       ; chain: BIOS ticks its count and sends EOI
    call far [sch_old08]
```

One call, made from the deepest-nesting context in the kernel, costing more
than everything around it put together. The fix is the classic one and is
**cheap because SS never changes** (§2.1 — every task runs `SS = LOW_SEG`): a
private stack for the chain needs `SP` swapped and nothing else.

```
    save SP -> a word;  SP = the shared chain stack's top
    pushf / call far [sch_old08]
    SP = the saved word
```

Six or seven instructions around the one call. What it buys: the 50 bytes stop
being per-task and become **one shared allocation**.

**And it has a re-entrancy problem that must be designed, not waved at.** The
ROM's handler sends the EOI and then typically `sti`s to call `int 1Ch`, so a
second IRQ0 can arrive while the first chain is still running, re-enter
`sch_isr`, and switch to the *same* shared stack. Two ways out, and they are
not equivalent:

- **A busy flag that falls back to the task stack.** Safe and simple — but the
  worst case is then still 50 bytes on a task slice, so **no class in §6 may be
  sized below it** and the change buys speed and RAM but not the floor.
- **A busy flag that skips the chain** (EOI only, as the sub-tick path at
  `sch_isr`'s `.tick` already does for its own reason). Bounds the floor for
  real, at the price of the ROM's tick count losing an occasional increment
  under a load that is already a tick behind.

**The second is the one that makes the small classes possible, and it is a
behaviour change somebody has to agree to.** It was the central open question
of this plan — **and it dissolved when the window was looked at properly**
(SPEC.md 8.5.1).

`sch_isr`'s contract is IF=0 *throughout*, so there is exactly one window in it
where anything can arrive: inside the ROM's handler, after its own EOI and
`sti`. Everything that lands there — a keystroke's `int 09h`, a mouse IRQ's
six-byte gate frame, the floppy's completion — nests on the private stack
harmlessly, and `SCH_CHSTK` is sized for it. **Only a second IRQ0 is a problem**,
and not for stack reasons: the re-entered `sch_isr` runs on to `sch_switch`,
which would save a *private* SP into a task record. So that one, and only that
one, takes the skip arm.

And it is not the "worst case is still 50 bytes" of the first option, because
the skipping entry never swapped: it carries on down the rest of the tick on the
task's own stack, exactly as it did before. **No task slice ever carries the ROM
chain on either arm**, which is what the classes needed.

What it costs is one BIOS tick increment, in a case the machine is already a
tick behind for: a second IRQ0 can only reach the ROM handler if the first was
itself delayed by nearly a whole tick — the PIT's edges are 54.9 ms apart and the
handler is ~0.2 ms — so it takes a long IF=0 window such as a floppy transfer,
and then only for the one tick whose phase lands inside it. The 8259 latches one
pending IRQ0 and no more, so a backlog is *already* collapsed to a single tick
before any of this. **`[sch_chskip]` counts them, and over the 90-second QEMU
run it read 0.**

### 4.2 The mouse ISR — 54 bytes, and it moves for 34

> **BUILT. SPEC.md 9.10 is the contract and this is the design record behind
> it.** It ships as the default; `NOMOUPRIV=1` is the A/B and arm 2 of
> `make stkdiag`. The measured cost on the tree that shipped it is **164 bytes
> resident** — `.text` +34, `.bss` +2, `.lowbss` +128 — against ~48 off each of
> seven slices, and it crossed no rung. The `.lowbss` figure is **128 and not
> the 256 this section first proposed**: nothing can interrupt that stack (the
> ISR holds IF=0 throughout), so 54 is the whole answer rather than a sample,
> and 2.4× is margin against a future `cur_move` rather than against an unknown.

**Measured, not estimated.** `make STKDIAG=1` runs the whole mouse ISR on a
stack of its own and reads its high water off it: **54 bytes**, on QEMU, after
45 seconds of continuous movement.

Of those 54, only **six** have to be on the interrupted task's stack — the
FLAGS/CS/IP the CPU pushed before we had control. Everything after that can be
somewhere else, because the swap needs no register:

```nasm
    mov [cs:mou_psave], sp      ; 2E 89 26 xxxx   5 bytes
    mov sp, mou_pstack + MOU_PSTK   ; BC xxxx     3 bytes
```

`mov [cs:x], sp` and `mov sp, imm16` both work with nothing to spare, which is
what makes this possible at all at an interrupt gate where every register
belongs to the interrupted task. CS is `KERNEL_SEG` at the gate, so the save
slot is reached before DS is ours; SS is already `LOW_SEG` for every task
(§1/§2.1), so the private stack lives in `.lowbss` and the swap is SP alone.

**It needs no re-entrancy guard, and that is a property rather than a hope.**
`mou_isr` runs with IF=0 from the CPU's gate to the `iret` and never `sti`s
(§7/§9), so it cannot interrupt itself, and IRQ3 and IRQ4 cannot interrupt each
other. §4.1's chain needs a busy flag precisely because a real BIOS `sti`s
inside it; this one does not. `mou_eoi` is the single exit — one `iret` in the
whole ISR — so there is exactly one place to swap back.

**The cost, counted off the listing rather than estimated:**

| | bytes |
|---|---|
| `.text` — the 8-byte entry at **both** vectors, plus a 5-byte restore | **21** |
| `.bss` — the saved SP | 2 |
| `.lowbss` — one shared 256-byte stack (4.4× the measured 54) | 256 |
| **removed from every task slice** | **48** |

**The entry is duplicated on purpose**, and it is the duplication this document
was told to spend if it helped: there are two vectors and the swap has to
happen before the first `push`, so the eight bytes are written twice. Sixteen
bytes of `.text`, once, against 48 bytes of every task stack in the machine —
seven times over today and seventeen under §7.

### 4.2.1 The two fixes compose, and not additively

**Demonstrated in the other direction now that both ship.** Arm 3 —
`NOCHAINPRIV=1`, so the mouse ISRs are still private and only the chain is back
on the slice — reads a floor of **82 on QEMU, which is the whole of the original
pre-both-fixes figure.** The mouse fix alone bought *nothing* on the floor
there, because the floor is the deeper of the two paths and
`max(tick 28 + BIOS 50, mouse 58)` is decided by the first term. Both had to
move before either showed.


The ROM's `int 08h` handler sends the EOI and then `sti`s before `int 1Ch`, so
**IRQ4 can arrive inside the tick's chain** — and when it does, one task stack
carries `sch_isr`'s frame *and* the ROM's chain *and* the whole mouse ISR at
once.

That is the 130-byte excursion §10.3 caught: 24 (`SCH_FRAME`) + 4 (the idle
task's own) + ~50 (the ROM) + 54 (the mouse) ≈ 132. It is rare — three
controlled 45-second runs since have peaked at 84, 88 and 84 — which is exactly
why it must be designed for rather than sampled for. **Either fix removes the
product case**, and both together leave `sch_isr`'s own 28.

### 4.2.2 Why relocation and not compression

Compressing the 54 in place was priced and is the worse trade:

- **24 of it is the entry frame** — the CPU's 6, `push bx`, and eight more
  registers the body genuinely uses across a call into the drawing layer.
- **`mou_byte` already tail-`jmp`s to `mou_apply`** rather than calling it, so
  the obvious flattening is done and cost nothing.
- The remaining ~26 is inside `cur_move`'s drawing chain, and shortening that
  means restructuring the graphics layer for less than the 48 that 21 bytes of
  `.text` buys outright.

**And the earlier refusal still stands, because it is a different change.**
§4.2's predecessor refused moving `cur_move` *out* of the ISR — that would cost
the ISR-paced pointer that docs/SCHED-IDLE-PLAN.md §6.3 rests on. Running the
same ISR, drawing included, on a different stack changes no behaviour at all.

### 4.2.3 Which size pass, and why the 54 is already safe

There are two, and telling them apart mattered — the first answer here was
wrong in both directions.

**The pass that rewrote `mouse.inc` is DONE, and the 54 is measured on top of
it.** It landed as `2f33456` — *"Elendilon -> Main (Kernel Size Pass, Boot
Overlay, Boot Ladder, Soak Harness Repairs)"* — which changed `kernel/mouse.inc`
by 231 lines, well before this document's base. Its branch
(`claude/kernel-size-optimization-vx08di`) still exists and still reports 497
changed lines against the *merge-base*, which is what misled the first reading:
**upstream squash-merges** (CLAUDE.md rule 6), so a squashed branch keeps a huge
merge-base diff and `--is-ancestor` answers "not merged" about content that is
fully merged. Against `origin/elendilon` it is 33 lines and net-negative — the
branch is *behind*, not ahead.

**The pass still running does not touch `mouse.inc` at all.**
`claude/kernel-size-optimization-p2-zcuuac` against `origin/elendilon` is
`kernel/sched.inc` and nothing else: 481 lines, 339 insertions.

So the exposure is the other way round from what §4.2.3 first said:

| number | at risk from the running pass? |
|---|---|
| the mouse ISR's **54** | **no** — `mouse.inc` is untouched by it and already carries the finished pass |
| the ROM chain's **56**, the floor's **84** | **yes** — `sch_isr` and `sch_switch` are exactly what it is rewriting |

Its diff already moves the frame those two are measured against — `push cx` and
`push bp` appear, a `push cx` goes away with *"sch_currec clobbers BX only"*,
and `.pick` is described as pushing DX and BX. **`sch_isr`'s frame is the floor's
largest fixed term**, so both tick numbers want re-taking when it lands, and
`STKDIAG=1`'s hook sits in the lines being rewritten and will conflict.

The general caution still holds and is worth keeping: **a size pass that factors
common code into shared helpers makes stacks deeper**, because every new call is
two more bytes of return address on every path through it. That is a reason to
re-measure after a size pass, not a reason to distrust a measurement taken after
one — and `make stkdiag` makes the re-take one boot.

### 4.3 What is not compressible

`SCH_FRAME`'s nine register pushes are the context. There is nothing to win
there and an attempt would only move where it is spent.

---

## 5. The idle task's own stack (Q3)

Its own footprint is ≤ 4 bytes (§1.1), so what it needs is the floor plus
margin, and nothing else. Against this project's convention of ~1.75× the
measured figure:

| idle stack | floor today (82) | floor with §4.1 taken (62) |
|---|---|---|
| 128 | 1.56× — **too thin**, and thinner still on iron (§9) | 2.06× — **viable** |
| 192 | 2.34× — **viable today** | 3.10× — comfortable |
| 256 | 3.12× | 4.13× |

So the answer to *"192 now, 128 if we find real compressibility"* is
**exactly that, and the compressibility is §4.1's**. 128 is not safe against
today's floor and is safe against §4.1's — which is the same sentence as "128
depends on the ROM chain moving off the task stack, in its bounding form".

**It does not free a slot on its own.** The slot and the slice are separate
resources: `task_spawn` allocates a `sch_tasks` record and *derives* the slice
from its index, so moving the idle task's stack out leaves `sch_tasks[1]`
occupied and the worker count still six. Getting the seventh back needs
`MAX_TASKS` 8 → 9, and that is an ABI change — `apps/os88api.inc` mirrors the
constant and it sizes `SS_TSTATE`, `SS_TCYC` and the `SS_INST` offset, hence
`SYS_SNAPSHOT_SIZE`, so every `.o88` is rebuilt. There is no way round it: the
Task Manager's meter reads the idle bucket out of the snapshot, and the header
has no spare byte to name it another way (§20.6, and the `SS_TSTATE` comment in
the SDK).

---

## 6. Pre-declared slot classes (Q4), and the canary

> **BUILT. SPEC.md 8.6 and 8.7 are the contract and this is the design record
> behind them.** The partition is `SCH_PARTITION` in `kernel/sched.inc` -
> 3x128, 6x192, 2x256, 2x384 - `MAX_TASKS` is 14, and `task_spawn` takes the
> stack a task asks for in CX and gives it the smallest free slice that fits.
>
> **Read off a running machine** (`STKDIAG=1`, the panel's own slice list):
> the idle task takes slot 1's 128 and reads **32 of 128, 25%**; the panel's
> own worker asks for the largest and takes slot 12's 384, reading 70 of 384.
> Smallest-fit is doing exactly what it was cut to do - all six 192s and both
> 256s are still free with two tasks running.
>
> Costs, measured: the machinery `.text` +74 / `.cold` +3 / `.ovlw` +3, and the
> layout flip a further `.text` +24, `.bss` +78 and `.lowbss` +128. No rung
> crossed; the low window is at 93% accrued with 34 bytes left, which is what
> makes a third 384 slice cost 512 rather than 192 (SCH_PARTITION says so at
> the line where it would go).


### 6.1 The canary gets simpler, not harder

Today `sch_switch` derives the slice base from the slot index with an 8-bit
multiply and a shift — about 75 clocks, and a `%if SCH_STACK == 256` special
case beside it for the arrangement that no longer ships. Variable sizes make
that arithmetic impossible, which is the objection; the answer is that the
arithmetic should not have been there in the first place.

**A per-slot base table replaces it.** `sch_stkbase: resw MAX_TASKS` — 16 bytes
of `.bss` — and the canary check becomes an index and a compare:

```nasm
    mov bl, [sch_cur]
    xor bh, bh
    shl bx, 1
    mov bx, [bx+sch_stkbase]
    cmp word [ss:bx], SCH_MAGIC
    jne sch_stkdie
```

Cheaper than what is there now, shorter, and it deletes the 256-byte special
case and the `SCH_STACK % 128` guard with it. **The canary is not weakened at
all** — `SCH_MAGIC` still sits at the bottom word of every slice, still written
by `task_spawn`, still compared on every switch away.

The four instruments that derive the same base independently — the `KFZ` deep
sampler in `sch_isr`, `tests/stackprobe`, `tools/stkwater.py` and
`tools/kfzread.py` — all read kernel symbols already, so they read the table
too. A parallel `sch_stksize` byte table (8 more bytes) is what lets them
report a size they did not assume. **That is the whole cost of the exception**,
and it is worth saying plainly that the comment at `sch_switch`'s canary
records two of those instruments having already read garbage once by deriving a
base the wrong way — a table is what stops that class of bug, not what invites
it.

### 6.2 Fragmentation is avoided by partitioning, not by allocating

Variable-size slices handed out and given back will fragment. The cheap answer
is a **fixed partition** — the classes exist as slices from boot, a spawn takes
the smallest free slice that is big enough, and a refusal is
`OSAPI_TASK_SPAWN`'s existing CF=1, which §20.6 already requires every package
to degrade on and retry. No allocator, no compaction, no new failure mode.

A package declares its class in its header, which is a header-version change of
the shape `.DRV` version 4 already was. **A package that declares nothing gets
the largest class**, so nothing existing has to be touched to keep working.

### 6.3 What the classes have to be

> **SUPERSEDED BY §7.2, which is cut on §12's survey.** Kept because the
> *sizes* it picked survived and the reasoning for them is here; what it got
> wrong is the population — it had four programs to go on and the survey has
> twenty. Read §7.2 for who is in which class.

Sized from §1.1's readings, not from round numbers:

| class | fits | on today's floor | on §4.1's floor |
|---|---|---|---|
| 128 | the idle task; a spinning service worker | no (1.5×) | yes |
| 192 | Bounce, Timer, most simple workers | yes | yes |
| 256 | Fractal (+60), Tracker (+60) | tight | yes |
| 384 | ftpd over `ETHER.DRV` (+150) | yes | yes |

**The Fractal is the case that stops the class list being shorter.** At +60
over a floor of 82 it needs 142 and a 192 slice gives it 1.35× — thin by this
project's standards. On §4.1's floor it needs 122 and 192 gives it 1.57×.
The top class stays at 384 either way; it is the reason `SCH_STACK` is 384 and
nothing here changes that. (**The survey later found a deeper tenant than the
network stack** — Frotz, at 240 — which does not move the class but does change
which program defines it: §12.3.)

---

## 7. The classes, cut from the field floors

**Rewritten on the measured numbers, after size pass 2** (§9.8), which moved
none of them. The requester's framing stands: anything freed goes to **more
slots**, not back to the heap, and `sch_stacks` may grow to about **3,072
bytes** from today's 2,688. The aim is a slot count nobody has to revisit.

### 7.1 The floor to design from is 64

Read from **slot 1** and not `FLOOR MAX` (§9.8.2). The real machines:

| | slot 1 | arm |
|---|---|---|
| 5150 Hercules | **64** | 2-or-3 |
| 5150 CGA | 62 | 2-or-3 |
| Packard Bell 286 | 40 | **3, confirmed** |
| 86Box XT, EGA | 48 | **3** |
| *(QEMU, SeaBIOS)* | *84* | *3 — not a real machine* |

**64 is the design floor**: the worst reading from real hardware, and taken on
an arm that is *at least* 2 — so arm 3 can only be at or below it. QEMU's 84 is
excluded deliberately: its ROM chain is 56 against real iron's 18–36 (§9.6.2),
so it is the outlier and not the worst case.

### 7.2 The classes

**Recut on §12's survey**, which is the first time anybody looked at where the
shipped programs actually are rather than at the four numbers §1.1 happened to
measure. Program depth above the floor travels between machines (§1.1) and the
survey is the population:

| class | shipped tenants | deepest of them | margin |
|---|---|---|---|
| **128** | the sound driver's stream tasks, Bounce, Timer | 64 + 28 = 92 | 1.39× |
| **192** | WIREFRAME, Artful, Task Manager, Fractal, `WEAVE`, Cyclone, Notepad, Arkanoid, Tracker | 64 + 80 = 144 | 1.33× |
| **256** | Modplug, Missile Command, Word, Telnet, Tamegram, Tank Attack, `CWORD`, `RUNCPM` | 64 + 130 = 194 | 1.32× |
| **384** | ftpd, Browser, **Frotz** | 64 + 240 = 304 | 1.26× |

**The rule is a margin of at least 1.25× over `floor + depth`**, taking the
larger of the static and measured figures — stated here because a class scheme
with no stated rule is a set of numbers somebody will later argue with. It is
below this project's usual ~1.75×, deliberately, and the trade is the slot
count in §7.3; the floor it is taken over is itself the worst of four machines
(§7.1), so the conservatism is in the base rather than in the multiplier.

**Frotz is what fixes the top class at 384**, at 1.26× — the thinnest margin in
the tree, and the reason `SCH_STACK` cannot simply become 256 for everybody.
§12.3 has the 22-level chain and the 42 bytes of unused register saves inside it
that would take it to 1.47× if anybody wants the headroom back.

**192 is the modal class**, with eleven of the twenty shipped workers in it.
That is the survey's most useful single result: the distribution is real, the
programs are spread across all four classes, and a scheme that partitions is
therefore worth its flag day. Had they all wanted 256 there would have been
nothing to do here.

### 7.3 What fits — and it is twelve, not sixteen

| | bytes |
|---|---|
| 2 × 128 | 256 |
| 6 × 192 | 1,152 |
| 2 × 256 | 512 |
| 2 × 384 | 768 |
| **slices** | **2,688** |
| the idle task, external (§5) | 128 |
| the ROM chain's private stack (§4.1) | 128 |
| the mouse ISRs' private stack (§4.2) | 128 |
| **total** | **3,072** |

**Twelve usable slots against today's six, and the slices cost exactly what
they cost today** — 2,688 bytes, unchanged. The only new money in the whole
scheme is the 384 bytes of the three shared external stacks, which is what
brings the floor down far enough for the small classes to exist at all. That is
the requester's own target arithmetic ("12 user slices + the three externals")
landing on the nose.

**This is four fewer than the sixteen §7.3 claimed before the survey**, and the
survey is why: that mix was 10 × 128, and only three shipped packages fit a 128
slice. Sixteen was arithmetic; twelve is the programs. Recorded as a downgrade
rather than quietly restated, because the earlier number is in this document's
history and somebody will find it.

Twelve is a **floor on the answer, not a ceiling**: a slice may host any program
whose class is at or below it (§6.2 partitions, it does not allocate), so the
mix above is sized for a plausible worst-case *set* of twelve rather than for
twelve copies of the worst tenant. Trimming Frotz (§12.3) would let a 384 become
a 256 and a 192; that is a later decision and this arithmetic does not depend
on it.

### 7.4 What binds after that is the ABI, not RAM

`MAX_TASKS` is mirrored in `apps/os88api.inc` and sizes `SS_TSTATE` (a byte per
task) and `SS_TCYC` (four), so `SYS_SNAPSHOT_SIZE` grows **5 bytes per slot** and
every `.o88` is rebuilt. Going 8 → 18 is +50 bytes of every package's snapshot
buffer and one flag day — the decision to take deliberately, once.

### 7.5 The two readings this arithmetic still wants

- **The 5150 on arm 3.** Its 64 is an arm 2-or-3 reading (§9.6.5), so the design
  floor is conservative by an unknown margin — the 286 fell 60 → 40 on the same
  step, and if the 5150 does likewise the 128 class gains ~20 bytes of margin.
- **EGA** (§9.8.3), which has no mouse-ISR reading at all.

Neither can raise the floor — both fixes only remove bytes — so **16 slots is a
floor on the answer, not a ceiling.** That is why it is safe to start.

**Design for bytes, never for rungs** (CLAUDE.md): nothing above is quoted as a
rung, and the ledger position is whoever builds this to report with `kernsize`.

## 8. Refusals

- **Moving `cur_move` out of the mouse ISR.** §4.2.2, and it still stands: it
  costs the ISR-paced pointer that docs/SCHED-IDLE-PLAN.md §6.3 rests on.
  Running the *same* ISR on a different stack (§4.2) is a different change and
  is not refused — it changes no behaviour at all.
- **Shrinking `SCH_FRAME`.** §4.3.
- **Deleting the idle task.** It is what `sch_switch` picks where it used to
  resume the outgoing task, and with a `ui_task` that can sleep that fallback
  would resume a sleeper. It can be moved off a worker slot (§5); it cannot be
  removed.
- **An allocator for the slices.** §6.2 — a fixed partition has no
  fragmentation and no new refusal path.

---

## 9. The 5150 has answered — and QEMU was wrong in BOTH directions

**Run, on an IBM PC 5150: ROM `10/27/82`, model `FF`, Hercules (720).** Two
arms, `make stkdiag`'s first and second, ~1,800 chain samples each.

| | as it ships | `MOUPRIV=1` | delta |
|---|---|---|---|
| ROM `int 08h` chain | 46 | **36** | −10 |
| floor, quiet | 94 | 96 | +2 |
| floor, +mouse | 118 | **100** | −18 |
| floor, +keys | 118 | 100 | −18 |
| **FLOOR MAX** | **118** | **100** | **−18** |
| slot 1 (the idle task) | 74 | **46** | −28 |
| slot 2 (the painter) | 118 | 100 | −18 |
| mouse ISR, own stack | — | **30** | |

### 9.1 The ROM is 36, and the two arms explain their own difference

**36 is the number**, and it comes from the `MOUPRIV` arm for a structural
reason rather than a preference. In the shipping arm the ROM's chain runs on
the scratch, and the ROM `sti`s before `int 1Ch` — so a mouse packet arriving
inside it lands *on the scratch too* and is counted as the ROM's. Move the
mouse ISR to its own stack and only the ROM is left: **46 → 36, and the 10 is
the nesting.** The instrument measured §4.2.1's product case without being
asked to.

**SeaBIOS is 56. A real IBM ROM is 36.** So for this one term **QEMU
OVERSTATES by 20**, which is the opposite direction from
docs/KERNEL-MEMORY.md's standing "+46 understates a real BIOS" — and that
paragraph's other claim, that SeaBIOS keeps its interrupt frames off our stack
entirely, is refuted outright by both machines. **Neither half of it survives.**

### 9.2 …but the FLOOR is higher on iron, not lower

118 as shipped, against 84–130 sampled on QEMU. So QEMU understates the floor
while overstating the ROM chain: the +46 correction is not a constant to add,
it is two errors of opposite sign that happened to be quoted as one. **Anything
sized off a QEMU floor plus a fixed adder is sized wrong.**

### 9.3 The mouse relocation, measured on iron

The mouse phase adds **24 bytes** to a task stack as shipped and **4** with
`MOUPRIV=1` — the residual being the six the CPU pushes, within sampling. So
the change removes 20 of the 24 it was designed to remove, and the FLOOR MAX
falls **118 → 100** for the 21 bytes of `.text` §4.2 priced.

The idle task's own slice falls further, **74 → 46**, because the machine's
maximum sits on the busier painter slice rather than on it.

### 9.4 Two findings nobody was looking for

- **The mouse ISR is adapter-dependent.** 30 bytes here on Hercules against 54
  on QEMU's VGA — `cur_move`'s 1bpp path is shallower than the planar one
  (§39). **A class scheme must be sized from the deepest adapter, so 54 is the
  number to design with and 30 is not.**
- **The keyboard adds nothing.** `+keys` is +0 on both arms, where
  docs/KERNEL-MEMORY.md's field note has `int 09h` nesting worth about twelve.
  On this machine, at this depth, it is not.

### 9.5 What is still open

Both arms still **alternate** the ROM chain, so half the ticks put 36 bytes
back on a task stack and the 100 above is not the end state. `make stkdiag`'s
**third arm** (`stkdiagfix*`, `STKFIX=1` — both proposals on at once) is what
reads it; the arithmetic predicts **~64**, and that is the number the classes
in §6 and §7 should finally be cut from.

Also unrun: CGA and VGA (the mouse ISR is deeper on both), and any ROM that is
not this one — an XT clone, a 286, a 386 — which is the whole reason the disk
is a disk.

## 9.6 Three more machines, and a hole the third one found

| | 5150 Herc | 5150 CGA | Packard Bell 286 |
|---|---|---|---|
| ROM / model | 10/27/82 `FF` | 10/27/82 `FF` | **01/15/88 `FC`** |
| adapter | Hercules 720 | CGA 640 | VGA 640 |
| mouse | serial | serial | **PS/2** |
| **ROM `int 08h`** | **36** | **36** | **18** |
| floor, quiet | 86 | 98 | 70 |
| floor, +mouse | 98 | 98 | 86 |
| floor, +keys | **112** | 98 | 90 |
| **FLOOR MAX** | **112** | **98** | **90** |
| chain samples | 2,016 | 2,291 | 2,008 |
| mouse ISR, own stack | 30 | **23** | **0 — see below** |
| slot 1 / slot 2 | 64 / 112 | 62 / 98 | 60 / 90 |

### 9.6.1 The PS/2 mouse was never covered, and the panel said so

**`mouse ISR, own stack` reads 0 on the 286 while the mouse phase still adds 16
bytes to a task slice** (70 → 86). Those two rows together are the diagnosis: a
PS/2 mouse arrives on IRQ12 through `mou_p2_isr` (§9.9), which is a **separate
ISR** that §4.2's change never touched — so nothing was relocated and nothing
was measured.

Fixed: `mou_p2_isr` takes the same `MOUPRIV_ENTER`/`LEAVE` pair. The safety
argument carries over unchanged — it runs IF=0 from the gate to its single
`iret` and never `sti`s — and the two ISRs **share one private stack**, because
neither can nest on the other for that same reason.

This is what a knob that reports *its own* coverage is for: a 0 beside a
non-zero effect is a hole, where a missing row would have been silence.

### 9.6.2 The ROM term is BIOS-specific and adapter-independent

**36 on both 5150 runs** — same ROM, different adapter, same number, which is
the consistency check the pair was worth taking. And **18 on the 01/15/88
ROM**. So the range on real iron so far is **18–36 against SeaBIOS's 56**:
every ROM measured is cheaper than QEMU, and they differ from each other by 2×.
**There is no single "BIOS adder" to design with** — only a per-machine
measurement, which is the disk's whole reason to exist.

### 9.6.3 The mouse ISR really is adapter-dependent

**23 on CGA, 30 on Hercules, 54 on QEMU's VGA.** `cur_move`'s 1bpp path is
shallower than the planar one (§39). No real-hardware VGA + serial-mouse
reading exists yet — the 5150 has no VGA card and the 286 has no serial mouse —
so **54 remains the number a class scheme must be cut from**, and it is still
the one figure in this table that comes from an emulator.

### 9.6.4 The keyboard, on a harder protocol

Spamming several keys at once rather than holding one down found **+14 on
Hercules** (98 → 112), **+0 on CGA** and **+4 on the 286** — so
docs/KERNEL-MEMORY.md's "`int 09h` worth about twelve" is real, but it is a
**rare coincidence rather than a standing cost**: the same protocol on the same
machine one adapter along found nothing. That is the nesting distribution
again, and it is the argument for designing to a margin rather than to a
sampled maximum.

### 9.6.5 Which arm were these? — the instrument could not say, and now it can

All three carry a `mouse ISR` row, so all three are at least `MOUPRIV=1`;
whether `STKFIX=1` was also on **cannot be read off the photographs**, and the
arithmetic does not settle it either. Three pictures that cannot be told apart
is a measurement somebody has to remember, and remembering is what the disk
exists to replace.

The panel now prints its own build, inverted, under the title: `ARM 1 of 3 as
it ships` / `ARM 2 of 3 MOUPRIV` / `ARM 3 of 3 MOUPRIV + STKFIX`. **The three
readings above should be re-taken on the labelled disks**, and until they are,
treat this table as arm 2-or-3 rather than as either.

## 9.7 ARM 3 on the 286 — the end state, and the PS/2 fix validated

**`ARM 3 of 3 MOUPRIV + STKFIX`**, read off the panel itself. Same machine as
§9.6's third column: Packard Bell 286, VGA 640, PS/2 mouse, ROM 01/15/88 `FC`.

| | §9.6 (PS/2 uncovered) | **ARM 3** | delta |
|---|---|---|---|
| ROM `int 08h` | 18 | 18 | — |
| floor, quiet | 70 | 70 | — |
| floor, +mouse | 86 | **70** | **−16** |
| floor, +keys | 90 | 74 | −16 |
| **FLOOR MAX** | **90** | **74** | **−16** |
| mouse ISR, own stack | **0** | **52** | now measured |
| slot 1 (idle) | 60 | **40** | −20 |
| chain samples | 2,008 | 2,062 | |

### 9.7.1 The mouse now adds exactly nothing

`quiet 70 → +mouse 70`. **Zero**, where the same machine read +16 before
`mou_p2_isr` was covered. Not "mostly removed" — removed. The six bytes the CPU
pushes are still there, they simply never became the deepest thing on any
slice.

### 9.7.2 It is the ADAPTER that sets the mouse ISR's depth, not the mouse

**52 bytes for the PS/2 ISR on VGA**, against **54 for the serial ISR on QEMU's
VGA** — two different ISRs, two different machines, the same number. And
against **23 on CGA / 30 on Hercules**.

So the depth is dominated by `cur_move`'s adapter path (§39) and the ISR around
it is nearly free. That settles §9.6.3's open question the other way round from
how it was asked: **there is no missing "real VGA" reading to wait for** — the
286 is one, it agrees with QEMU to two bytes, and **~54 is confirmed as the
number a class scheme must be cut from.**

### 9.7.3 What the end state actually is

**Slot 1 — the idle task's slice — reads 40 with both proposals on**, against
74 for the busiest slice on the same run. 40 is the honest floor for a *minimal*
task on this machine: its own ≤4 bytes plus ~36 of interrupt frame that nothing
proposed here removes, because it is `sch_isr`'s own context.

The keyboard's **+4** reproduced exactly, on the same spamming protocol as
§9.6.4 — so on this machine it is a standing cost, small and real, where on the
5150 it was +14 once and +0 on the adapter next door.

### 9.7.4 Recorded, not yet spent

**§6 and §7's class arithmetic is deliberately NOT rewritten on these numbers.**
Size-optimisation pass 2 (`claude/kernel-size-optimization-p2-zcuuac`) is still
running and rewrites `kernel/sched.inc` — which is precisely where the ~36
bytes of §9.7.3's remaining floor live, and where `STKDIAG`'s own hook sits
(§4.2.3). Cutting classes from a floor that pass is about to move would be
sizing against a number with a known expiry.

What is banked and will not move: the ROM term is per-BIOS (18–36 on iron, 56
on SeaBIOS); the mouse ISR is ~54 on VGA and 23–30 on mono, whichever mouse;
and both relocations do what they were priced to do, measured on three
machines.

## 9.8 Size pass 2 landed, and it moved nothing on the stack

`origin/elendilon` at `465a07c`, 153 commits including the pass that
§4.2.3 warned about. Re-measured under QEMU with the identical protocol:

| arm 1, as it ships | before pass 2 | after |
|---|---|---|
| ROM `int 08h` | 56 | **56** |
| floor, quiet | 84 | **84** |
| idle slice | 130 | **130** |

**Identical.** The reason is checkable rather than lucky: the pass's headline
item — *"the shared epilogue ladder: 141 sites"* — is entered by a **near
`jmp`**, not a `call`, so a factored epilogue costs **zero** stack. `sch_isr`'s
nine pushes are untouched and `SCH_FRAME` is still 24.

§4.2.3's caution was worth having and did not fire. **A size pass deepens
stacks only when it factors with `call`; this one factored with `jmp`.**

Both hooks auto-merged; plain, `MOUPRIV`, `KERN_SMALL`, the three-knob build and
the new `VIDEO=ega` all assemble, and `make test-full` is 41 passed, 0 failed.

### 9.8.1 ARM 3 after pass 2

| | arm 1 | **arm 3** |
|---|---|---|
| idle slice | 130 | **84** |
| mouse phase adds | — | **0** |

### 9.8.2 An instrument caveat: read SLOT 1, not FLOOR MAX

`FLOOR MAX` and the phase rows report the deepest of **all** slices, and one of
those is the diagnostic painter — which draws under the gfx lock and is
**116 on both arms**, because that depth is its own work and not an interrupt at
all. It is the deepest slice on the machine and it is not a floor.

**Slot 1, the idle task's slice, is the honest floor** — its own footprint is
≤4 bytes by construction (§1.1), so everything above that is interrupt. Every
run in this document reports it, so nothing needs re-taking:

| | slot 1, arm 1 | slot 1, arm 2-or-3 | slot 1, **arm 3** |
|---|---|---|---|
| QEMU (SeaBIOS 56, VGA) | 130 | — | **84** |
| 5150 Hercules | 74 | 64 | — |
| 5150 CGA | — | 62 | — |
| Packard Bell 286 | — | 60 | **40** |

The panel keeps `FLOOR MAX` because it is the right answer to *"has anything on
this machine gone deeper than X"* — which is the overrun question. It is the
wrong answer to *"what does a slot cost before its program runs"*, and §7 uses
slot 1.

### 9.8.3 Two new things to measure

- **EGA (`VID_EGA`, 640×350) is a new adapter**, and §9.7.2 established that the
  adapter is what sets the mouse ISR's depth. It has no reading. The prediction
  is that a planar 4bpp EGA sits with VGA at ~52–54 rather than with the mono
  pair at 23–30, and that is a prediction rather than a measurement.
- **A 1.2MB 5.25" geometry now ships.** `make stkdiag` builds it, so the arms
  are **twelve disks, three arms of four**.

## 9.9 EGA, and the "extra stack on an emulator" that was the panel

86Box `ibmxt86`, EGA 640×350, ROM `05/09/86` `FB`, 10 MHz 8088, arm 3. EGA
arrived from upstream and nobody here owns a card, so an emulator is the only
way to run it at all.

| | reading |
|---|---|
| ROM `int 08h` | **18** |
| mouse ISR, own stack | **54** |
| **slot 1 — the floor** | **48** |
| slot 2 — the panel's own painter | 124 |

### 9.9.1 The EGA prediction was right

§9.8.3 predicted a planar EGA would sit with VGA at ~52–54 rather than with the
mono pair at 23–30. **54.** The adapter sets the mouse ISR's depth (§9.7.2) and
it is *planar versus 1bpp* that does it, not the card:

| | mouse ISR |
|---|---|
| VGA (PS/2, real 286) | 52 |
| VGA (serial, QEMU) | 54 |
| **EGA (serial, 86Box)** | **54** |
| Hercules | 30 |
| CGA | 23 |

So §7's design number of **54** is now confirmed on three planar adapters and
needs no further reading.

### 9.9.2 There is no extra stack, and the panel caused the question

The field asked what was eating the extra stack on an emulator — 124 on EGA
against 74 on the 286 — and whether the NIC could be doing it with no driver
loaded.

**Nothing is.** 124 is `slot 2`, and slot 2 is **this panel's own painter**,
which draws the panel under the gfx lock twice a second. That depth is its own
work, and it tracks the adapter's drawing cost exactly as it should — EGA's
planar 640×350 is deeper than VGA's 640×480 window and much deeper than mono.
**It is not a floor and never was.**

The floor is `slot 1`, and it is entirely in family:

| | slot 1 |
|---|---|
| PB 286 (VGA, ROM 18) | 40 |
| **86Box XT (EGA, ROM 18)** | **48** |
| 5150 CGA (ROM 36) | 62 |
| 5150 Hercules (ROM 36) | 64 |

Eight bytes between the two ROM-18 machines, and the 5150s are higher for the
reason §9.6.2 already gives — their ROM chain is twice the size.

**The NIC is ruled out twice over**, and neither reason is a judgement call:
`net_01_link = 0` in both configs, so the card raises no interrupt at all; and
the kernel hooks `int 08h`, `int 09h`, `int 0Bh`/`0Ch` and `int 19h` and
**names no NIC vector anywhere** — that hook lives in `ETHER.DRV`, which is not
loaded. Neither is the Sound Blaster or the ST-11M: an unloaded driver runs no
code, and `ROM int08` already measures whatever the tick chain reaches.

### 9.9.3 The panel now leads with the floor

This is §9.8.2's caveat reaching the field one run after it was written down,
which is what a caveat in a document does instead of a change in the code. The
panel now prints

```
FLOOR, idle  <- quote this      48
deepest slice (inc. panel)     124
```

so the row labelled "quote this" is the one worth quoting, and the deepest
slice keeps its place as the answer to *"has anything here gone deeper than
X"* — the overrun question, which is a real one and a different one.

**No field reading needs re-taking**: every run in this document already
reports slot 1, and the table above is those numbers.

## 10. The measurement disk — `make stkdiag`

**Built, and it answers §9 on any machine.** `STKDIAG=1` (`kernel/stkdiag.inc`)
is a kernel that measures itself and draws the answer on the desktop, in all
three geometries, with **no package to launch and nothing to click**. That is
why it is a knob: a package would need a double-click, and the double-click
lands inside the quiet phase it would be perturbing.

### 10.1 How the ROM number is taken

`sch_isr`'s `pushf` / `call far [sch_old08]` is replaced by `sd_chain_call`,
which fills a private 512-byte stack in `.lowbss` with a sentinel, **swaps SP
to it** (SS is already `LOW_SEG` for every task, so it is an SP swap and
nothing else), runs the chain, swaps back, and scans down for the first
surviving sentinel byte. What came back scrubbed is what the chain cost.

**No task stack is touched**, which is what makes it safe to ship to a machine
nobody here can debug. Two earlier designs sentinelled the free bytes below SP
on a task stack instead; the first took zero samples (on a desktop the tick
lands on task 0, not the idle task) and the second left the panel half drawn.

**It runs on alternate ticks.** Measuring every chain on the private stack would
make the floor a lie — it becomes the floor of the machine §4.1 *proposes*, not
the one that ships. So odd ticks are measured and even ticks run plainly on the
task stack where the `0xCC` fill records them. Both numbers on the panel are
then true of the kernel they describe, **and the difference between them is what
§4.1 is worth on that machine.**

### 10.2 What the operator does

Three phases on a wall clock, 90 seconds in total, with a five-second **HANDS
OFF** window before each reading is taken:

| | | |
|---|---|---|
| 0–30s | `QUIET — TOUCH NOTHING` | needs no operator at all |
| 30–55s | `MOVE THE MOUSE NOW` | |
| 60–85s | `HOLD DOWN A KEY` | |
| 90s | `DONE — WAIT 2 MIN, THEN PHOTOGRAPH` | |

A phase nobody performs reads equal to the one before it, which is a reading
and not a hole — and **the quiet phase, the one that matters most, is the one a
human cannot perturb.**

**`FLOOR MAX` is the row to quote.** The high water is a sample of a nesting
distribution, not a bound: on QEMU the quiet phase latched 84 while the machine
was still climbing to 130 a minute later. The MAX row only rises, which is why
the panel asks for two more minutes before the photograph.

On an emulator nothing needs photographing: the same values are published in
§57's registry as `SD` and `tools/stkdiagread.py` prints them.

### 10.3 What it reads here, and the cross-check that validates it

QEMU/SeaBIOS, one 95-second run:

```
ROM int08 chain          56
floor  quiet             84
floor  +mouse            84
floor  +keys             86
FLOOR MAX                130
chain samples taken     1146
```

**56 is the independent confirmation of §2's A/B.** That A/B removed `pushf` +
`call far [sch_old08]` and moved a bare desktop from 82 to 32 — 50 bytes. This
measures the same chain including the 6 bytes of `pushf` and the far call's own
frame that the A/B also removed: **50 + 6 = 56**. Two instruments, different
mechanisms, agreeing to the byte.

And it demonstrates the fix at the same time: with every chain on the private
stack the floor fell **82 → 38**, which is §4.1 working, measured, on a machine.

## 11. Instruments, and one wart

- `tests/stackprobe` — the probe. Its worker spins, so its reading is the floor.
- `tools/stkwater.py` — reads the fill back. **Its `DEF` defaults to
  `("KFZTRACE",)`**, so it refuses on a shipping kernel with "the map describes
  a DIFFERENT kernel"; it wants a defines argument.
- `tools/stkdepth.py` — static, and **it is not usable on `kernel/kernel.asm`**:
  its linear walk runs past routines that end in `iret` or fall through, and it
  reported `snd_tick: 0 bytes` and an 84-byte chain for the leaf `sch_account`.
  It works as documented on a driver or package `.asm`. Every kernel-side
  number in this document came from an A/B on the machine instead.
- A temporary `NOBIOSTICK` A/B — replacing the ROM chain with a bare EOI — is
  what priced §4.1. It is **not** proposed as a shipped knob: it stops the ROM's
  tick count advancing and changes motor timeout, so it is a measuring tool and
  its stressed readings are indicative only. §4.1's own busy-flag form is the
  shippable shape.

---

## 12. The survey: where every shipped program lands

> **BUILT — every package in the table below now declares its class in its
> header** (SPEC.md 8.7.2), with the measurement and the margin written beside
> it in the source. Verified on the glass: `ARKANOID.O88` declares 192 and lands
> in **slot 4, the first 192**, stepping over two free 128s because they are too
> small.
>
> One thing the survey got wrong about itself, found while building it:
> §12.4 called `CC_STACK` an unguarded mirror that would break if a C package
> declared a smaller class. It is the opposite — `CC_STACK` has to be the
> **largest** class precisely because first fit can hand a package a *bigger*
> slice than it asked for, and a window cut to the declared class would then be
> short by up to 256 bytes exactly when the machine is busy. It is now taken
> from the SDK's `SCH_STACK` and guarded by `tests/unit/t_mirror.py`, which
> compares every name defined in more than one file — so the drift is closed,
> just not in the direction the survey predicted.


The requester's question, before any of this is built: *"for the apps we
currently have, where would each of them land on requirements? We're going to
have to update each of them anyway, so before we start is the best time to do
the survey."*

It is the right moment for a second reason. A class scheme is only worth the
flag day if the programs are actually **spread** across the classes — if they
all wanted 256 there would be nothing to partition — and until this section
nobody had looked.

### 12.0 First, the accounting — and one premise that is wrong in our favour

The requester's arithmetic was *"the UI task will always take its own. If sound
is on, that also eats a worker forever right? So a target of 12 user slices +
the three 'externals' would give us back 10 bounces, even with sound on."*

Three corrections, all of which make the answer better rather than worse:

1. **The UI task takes no slice at all.** Task 0 runs on `SS:STK0_TOP`, a
   separate 1,024-byte region above `.lowbss` (`kernel/sched.inc`'s header says
   so explicitly: *"it owns no slice of `sch_stacks`"*). It costs a **task-table
   record** — one of `MAX_TASKS` — and not a stack slice. Those are two
   different budgets and only the second one is what this document is spending.
   `sch_stacks` is `(MAX_TASKS-1) * SCH_STACK` = 7 × 384 = 2,688 bytes, and all
   seven slices are for dynamic tasks.

2. **Sound does not eat a worker forever.** It is transient, and
   `drivers/sound/sb.inc` says so at the routine that spawns it: *"No resident
   sound task exists (rejected, SPEC.md 34.5): this slot and its stack come
   from the ordinary dynamic pool and return to it with the stream."*
   `sbl_refill_task` is spawned by the stream-open verb and calls `task_exit`
   when the stream closes, is torn down, is superseded by a newer open, or is
   stopped by the watchdog; `sbl_drain_task` is the same shape for record. So
   the cost is one slice **while audio is playing**, which for Tracker and
   Modplug is concurrent with the app that asked for it, and it comes back.

3. **`ETHER.DRV` takes none, ever.** `OSAPI_DRV_TASK` has exactly one caller in
   the whole tree and it is `drivers/sound/sb.inc` (four sites: two spawns, two
   exits). The Ethernet driver pumps **synchronously inside its own service
   verbs**, so its ~122 bytes land on whichever task called it — see §12.2.
   §1.1's row *"`ETHER.DRV` service worker"* is therefore a **mislabel**: the
   232-byte slice measured after an FTP session was **`FTPD.O88`'s own worker**
   carrying the driver's chain, not a driver task. Corrected here rather than
   silently in the table, because the number is right and only its owner was
   wrong — and the correction is what makes it a *program* cost that a class
   can be cut for.

So today's six is: 7 slices, minus **one** permanently held by the idle task.
Not two. And under §5 the idle task gets its own external stack, which returns
that one — so **N slices means N concurrent workers**, full stop.

Against the requester's target: **12 slices gives 12 workers, or 11 while music
is playing.** The stated goal of ten is met with one to spare with audio and two
without — and §7.3's 16 is what the same 3,072 bytes actually buys.

### 12.1 The survey

`tools/stkdepth.py` on every shipped package that hires a worker, walked from
the worker's own entry point. The number is the program's own depth **above the
floor** — it ends at the `OSAPI_*` far call and counts its 4 bytes, so it is
directly comparable with §1.1's "above the floor" column and with §7.2's
budgets.

| program | worker entry | static | measured | class |
|---|---|---|---|---|
| `CWORD.O88` (C) | `os88_worker` | ~10, **but see below** | — | 256 |
| `RUNCPM.O88` (C) | `os88_worker` | ~10, **but see below** | — | 256 |
| sound driver | `sbl_refill_task` / `sbl_drain_task` | **28** | — | 128 |
| WIREFRAME | `wr_worker` | 40 | — | 128 |
| `WEAVE.O88` (C) | `os88_worker` → `WEAVE.WSM` | ~40 + 22 | — | 192 |
| Fractal | `fr_worker` | 48 | **+60** | 192 |
| Artful | `at_worker` | 50 | — | 128 |
| Task Manager | `tm_worker` | 56 | — | 192 |
| Telnet | `te_worker` | 66 | — | *socket, §12.2* |
| Cyclone | `cy_worker` | 66 | — | 192 |
| Notepad | `np_worker` | 74 | — | 192 |
| Arkanoid | `ark_worker` | 76 | — | 192 |
| Tracker | `trk_worker` | 80 | **+60** | 192 |
| Tamegram | `tg_worker` | 82 | — | 256 |
| Tank Attack | `tk_worker` | 82 (a floor: three indirect call sites below which the walker counts nothing) | — | 256 |
| FTP server | `fd_worker` | 90 | **+150** | 256 |
| Modplug | `mpp_worker` | 98 | — | 256 |
| Missile Command | `mc_worker` | 106 | — | 256 |
| Word | `wd_worker` | 126 | — | 256 |
| Browser | `br_worker` | 148 | — | 384 |
| **Frotz** | `zx_worker` | **240** | — | **384** |
| *(Bounce, builtin)* | — | — | +10…24 | 128 |

The two instruments **err in opposite directions and that is what makes the
pair usable**: static walks the fall-through path of every branch, so it counts
depth a run may never reach; a high water is a sample, so it counts only depth
some run did reach. Where both exist they bracket the truth — the Fractal reads
48 static against +60 measured (the kernel below its API call is the
difference), the Tracker 80 static against +60 measured (the sample never took
the deepest branch). **Take the larger of the two**, which is what the class
column does.

### 12.2 The network trio is a class, not a driver cost

`ETHER.DRV` spawns nothing, so its depth is the *caller's*. Measured statically
on the driver, a socket verb costs:

```
eth_v_open   122      eth_v_recv   120      eth_v_send   120
eth_v_state  120      eth_v_accept 120      eth_v_status 116
```

plus the 4 bytes of `OSAPI_DRV_CALL`'s far frame — call it **~126 bytes on the
calling task's slice**, arriving through `apps/os88sock.inc`. That is far more
than any of the three network packages costs on its own (`te_worker` 66,
`fd_worker` 90, `br_worker` 148).

It does **not** simply add to the worker's static number, because the socket
call and the worker's own deepest chain are different branches — `fd_worker`'s
deepest static path is the glass painter (`fd_flush_glass` → `fd_paint_now` →
`fd_spend` → …), not the socket one. The measurement settles it: ftpd's slice
read **232 of 384 on QEMU** (floor 82, so **+150**) and **220 on the 5150** —
i.e. the socket branch is the deeper of the two and lands at about +150, not at
90 + 126 = 216. Browser is then the worst of the three: it has ftpd's socket
branch *and* a 148-byte layout branch of its own.

The consequence for the design is a good one: **"network app" is a class, and
it is one class rather than a per-driver tax on everybody.** A machine with no
NIC pays nothing for it, and a machine with one pays it only on the three
packages that open a socket.

### 12.3 Frotz is the tenant that sets the top class, and 42 of its bytes are free

`zx_worker` walks **22 levels** to reach `OSAPI_GFX_LOCK`:

```
zx_worker -> ... -> zw_wflush -> zw_break -> zw_scroll1 -> zw_paint
   -> zw_status_draw -> zw_status -> zt_print -> zt_print_ret -> zt_decloop
   -> zt_zchar -> zt_putc -> zt_screen -> zt_screen1 -> zw6_putc -> zw6_wrap
   -> zw6_newline -> zw6_scroll -> zw6_flush -> zw6_lock -> OSAPI_GFX_LOCK
```

240 bytes, which on the §7.1 design floor of 64 is **304 of 384** — 1.26×, the
thinnest margin of anything in the tree, and it is the reason `SCH_STACK` may
not simply become 256 for everybody.

**42 of those 240 bytes are registers that chain pushes and never uses**, which
`stkdepth` names one routine at a time: `zt_putc` saves ax bx cx dx si (10),
`zw_putc` ax cx dx si (8), `zt_print_ret` bx cx si di (8), `zt_zchar` dx si di
(6), and four more of 2 each. Cutting them is the cheapest depth in the survey —
no restructuring, no behaviour change, and `stkdepth --check` already gates the
`; STKDEPTH-NOSAVE:` markers that record such a decision. That takes Frotz to
**198**, or 262 of 384 at 1.47×, in line with everything else.

**Not proposed as a prerequisite.** Frotz fits 384 today and would fit it after
the class scheme; this is recorded because the survey is where it became
visible, and because a 22-level chain is worth somebody's attention on its own.

### 12.4 What the survey found that changes the build

1. **`CC_STACK equ 384` in `apps/cc/crt0.asm` is an unguarded mirror of
   `SCH_STACK`,** and it is load-bearing: `cc_iswk` decides *"am I the worker"*
   by testing whether SP lies in `(cc_wksp - CC_STACK, cc_wksp]`, and SPEC.md
   20.6 rule 7 hangs off that answer. Under a class scheme the constant must
   become the package's own declared class — which it can, the class being a
   compile-time constant of the package — but a package that declares 192 and
   leaves `CC_STACK` at 384 gets a window **192 bytes too wide, reaching into
   the slice below it**, and the failure mode is a task told it is the worker
   when it is not. Silent, and exactly the shape docs/UPSTREAM.md warns about.
   It wants the same treatment `tests/unit/` gives the other mirrored constant.

2. **There is nowhere to put a class today.** `task_spawn` takes AX = entry,
   BX = segment, DX = the argument word (DL → `T_INST`, DH → the task's DH);
   CX is pushed and restored and is not an input, so it is free — but a
   register nobody sets today holds garbage, so it cannot be added silently.
   The package header is the better home anyway: SPEC.md 20.6 allows one
   background task per instance, so the class is a property of the package and
   not of the call. Either way it needs a flag day — and §7.4 already names one
   for `MAX_TASKS` in the snapshot ABI. **One flag day, not two.**

3. **A worker that only sleeps costs almost nothing**, and three of the C
   packages are exactly that: `CWORD.O88` and `RUNCPM.O88` both spawn a worker
   whose entire body is `task_sleep(4)` / `task_alive()` with a `gfx_lock` /
   `wm_destroy` / `gfx_unlock` bracket on the close path. They exist to make
   File > Close work and they draw nothing THEMSELVES — but `wm_destroy`
   repaints what the window uncovered on the CALLER's stack (`wm_paint_dmg`,
   then every uncovered window's own painter through `wm_pkgcall`), so the
   close path lands a Task Manager's or a Notepad's paint chain on this
   slice. At ~10 bytes of their own they LOOKED like the cheapest tenants in
   the tree and the clearest argument for a 128 class; §0's rule — a slice is
   sized by what lands on it — puts them at 256, which is what they declare
   existing at all.

### 12.5 What the survey cannot see

**The kernel's own depth below an `OSAPI_*` call**, which lands on the worker's
slice like everything else. `tools/stkdepth.py` reports **633 indirect call
sites** in `kernel/kernel.asm` and answers `ui_task: 0 bytes`, so it cannot be
walked — §11 already records that the tool is not usable on the kernel.

It is bounded rather than unknown, from the two places both instruments read
the same task: the Fractal is 48 static against +60 measured, so the kernel adds
**about 12** below its deepest drawing call. That is consistent with the design
— `gfx_hline` and its relatives are near-leaves — and it is why §7.2's margins
are stated over the floor rather than over the program.

The way to close it properly is one addition to `kernel/stkdiag.inc`: fill
`STK0` at boot the way `task_spawn` fills a slice, and publish its high water
in the panel. The UI task runs the *deepest* kernel paths there are — a full
`wm_paint_all` with a menu open — so its water is the kernel term plus the UI
task's own, on a 1,024-byte stack that can afford to be measured. It is
deliberately **not** added now: three machines are already running the current
disks (§9.6–§9.9) and a changed panel means a new round of field runs for a
number that only refines margins already taken.

### 12.6 The survey missed a whole branch, and the machine froze — SPEC.md 8.7.4

§12.5 says what the survey cannot see. This is what it could have seen and did
not, and both halves of the miss were in the instrument rather than in the
arithmetic.

**`tools/stkdepth.py` followed `call` edges and not tail jumps.** A routine
ending `ja somewhere_else` was walked as if `somewhere_else` did not exist —
and nothing in the output said so, which is the property that makes a
measurement worse than no measurement: the number looked exactly like a number
that had counted everything. `tm_update` reaches the Task Manager's three pages
that way (the performance list by falling through, the other two by `je
tm_upd_mem` / `ja tm_upd_heap`), so §12.1 recorded `tm_worker` at **56 bytes**,
which is the performance list alone. The heap page under it is **96**. The tool
adds tail edges at +0 now — nothing is pushed, and the target runs on top of
the frame the jumping routine still holds. Re-walking every worker in §12.1
moved exactly two: the Task Manager 56 → 96 and Missile Command 106 → 126.

**And §12.5's "about 12" is light by a factor of four on a drawing worker.**
That figure is the Fractal's — 48 static against 60 measured — and the Fractal's
deepest call is `gfx_hline`, which §12.5 correctly calls a near-leaf. The Task
Manager's heap page bottoms out in `font_run`, and the same subtraction there
is **180 measured − 96 static − 32 floor = ~52**. So the kernel term is not a
constant at all; it is a property of *which* primitive the worker's deepest
chain ends in, and 12 is the cheap end of it.

The two together are why 192 looked like 1.60× when it was 1.20×, and why the
slice measured **180 of 192** with the heap page open beside PAINT. It held
here — this emulator's interrupt floor is 32 where §7 sizes against 64 — and
went through the canary on a real IBM 5150, which is `sch_stkdie`: `cli`/`hlt`,
a dead machine, and until SPEC.md 8.8 nothing on the glass to say which of the
five things that look like a freeze it was.

Three things came out of it, and only the first is the Task Manager's:

- the class is **256**, which is the 1.60× the header comment meant to buy;
- `stkdepth.py` follows tail jumps, so the next `ja` is counted;
- **`tests/unit/t_stkclass.py`** reads the declared class back out of the built
  `.o88` and compares it with the tool plus the 64-byte floor, at Frotz's
  1.25×. Until it existed, `OS88_STACK_192` was a number a human typed after
  running a tool once and no gate ever compared it with anything — which is the
  same shape as §12.4's `CC_STACK` mirror, and it is fixed the same way.

`tests/gifdrag.py` is the recipe driven end to end. It asserts the **margin**
and not the survival, because "it did not freeze" is what every run before the
report also said.
