# Live DOS in a window, several at once — a TopView/DESQview shape for `DOS.O88`

**Status: EVERY WAVE IS BUILT (SPEC.md 96.54 and 96.54.1-7; `apps/dos/dosw.inc`
and `doswm.inc`; rows `doswlive`, `doswfs`, `doswtwo`, `doswspin`, `doswcap`,
`doswcell`, `doswmouse`), with one deliberate exception stated where it
belongs: §7.1 (one host, many sessions) is not taken. The lone Hercules's
second page (§7.2) IS switched on now - SPEC.md 96.54.6.1, +336 bytes, as a
DETECTOR rather than a capture, checked on MartyPC and 86Box alike. §10 onward is what was built and measured; the
sections before it are the plan as written.**

SPEC.md 96 is the contract for the DOS that exists. This plan changes none of
it yet; the SPEC section for the windowed run is written by wave 1, before the
code (CLAUDE.md: *update it before the change*).

---

## 0. The answer in one screen

1. **It can be done for ZERO resident kernel bytes.** Everything — the
   per-program interrupt vectors, the stack swap a DOS program's own `SS`
   needs, the virtual video and keyboard BIOS — fits in `DOS.O88`, which is
   only in RAM while a DOS window is open. §3 is why that holds, and the one
   kernel fact it rests on is already in SPEC.md 8.1.2.2: *only IRQ0 falls into
   `sch_switch`*; every other entry is a voluntary call the package makes
   itself.
2. **The package grows by ~1.7 KB of image and ~170 bytes of bss** (ESTIMATE,
   §6), against today's 38,511 + 9,560 (MEASURED). That is ~4%, it is per
   open DOS window, and it is not resident.
3. **What a window costs is the region plus the program's arena, and the
   ARENA dominates.** A DOS window is one `DOS.O88` instance — 48,128 bytes
   of region (MEASURED, rounded to 512) — plus the memory its program is
   given. Today a program is given *everything* (`OSAPI_MEM_AVAIL`, SPEC.md
   96.3); several at once means sizing each one (§5.6). On a 640 KB machine
   that is **about four windows of 64 KB `.COM` programs, or two to three at
   128 KB** (§6.3).
4. **Well-behaved programs run live in a window; programs that write `B800`
   directly run full screen, and are FROZEN with their last screen when sent
   back to the window** — which is exactly what DESQview did on an 8088,
   because an 8086 has no way to move a program's view of `B800` (§4).
   `Alt+Enter` already exists in both directions (SPEC.md 96.33.5.1), and the
   two halves of moving a live screen between the glass and the window —
   seeding (96.34.4) and capture (96.34) — are already built.
5. **The biggest further saving is a single DOS host serving many sessions**
   (~40 KB per extra window), and it is DEFERRED with its arithmetic (§7.1):
   it needs a kernel change, a core-state swap that costs ~8 ms per owner
   change, and it buys about one extra window on a 640 KB machine.

---

## 1. What exists, and what it already gives this

Read SPEC.md 96.2, 96.33 and 96.34 first; this section is only what they mean
for the brief.

| already built | where | what it buys here |
|---|---|---|
| The program runs inside ONE `OSAPI_FSX_RUN` bracket on the **UI task** | SPEC.md 96.2 | the reason the machine stops: the desktop is frozen for the program's whole life. This is the thing to replace for a windowed run, and the thing to KEEP for full screen |
| An 80x25 console in the window, `apps/os88con.inc` | SPEC.md 96.33 | **`con_scr` is text VRAM's own layout** (character, attribute, 80 to a row) — so the virtual screen a windowed program writes into already exists, 4,000 bytes, in every DOS instance's bss. Zero new storage for the screen |
| `DHK_TTY`: DOS's `AH=02h/09h/40h` already go to `con_write` when no bracket is up | SPEC.md 96.33, 96.44.3 | the INT 21h half of "capture the output" is already a hook |
| `dos_snap`: the program's last screen becomes the console at teardown | SPEC.md 96.34 | the **window ← glass** half of a live toggle |
| The console is SEEDED onto the program's screen | SPEC.md 96.34.4 | the **glass ← window** half |
| A worker may mark the console while the UI task paints it | SPEC.md 96.33.12 (`con_takerow`), Telnet's worker | a worker writing `con_scr` concurrently with the band's paint is a solved case, not a new one |
| `dos_be`: every file call goes through ONE door, `dos_be_go` | SPEC.md 96.4, 96.4.1, 96.4.2 | the one place to marshal file I/O from a worker to the UI task (§3.4) — the seam was cut for `kern_dos`, and it is exactly the seam this needs |
| `OSAPI_WM_WAKE` is worker-safe and coalesced (one queued wake per window) | `apps/os88api.inc` | the worker's "please paint / please serve" signal, with rate-limiting for free |
| `FSXF_KEEPWORKER` | SPEC.md 53.2 | a full-screen bracket can keep ONE worker running — the DOS instance's own, so a live program can be taken full screen without being stopped |

**SPEC.md 96.34.5 already names this work and defers it** — *"a program's
output arriving in the band as it runs, with the bracket never taking the
screen at all ... deferred behind the hibernate phase, because a program that
writes `B8000` directly has to be refused into full screen and that decision
reads differently once `kern_dos` exists."* `kern_dos` exists now (96.40), so
the decision can be taken; §4 takes it. And docs/plans/DOS-EXEC-PLAN.md §10
lists *"Multitasking. The bracket is exclusive by design"* as what will not
work — true of the bracket, and the reason this plan runs the program
**outside** it.

---

## 2. What has to change, stated as problems

A DOS program in a window has to stop being the UI task and become a task the
scheduler pre-empts. That raises six problems, each with a known answer:

| # | problem | why it is a problem here | answer |
|---|---|---|---|
| P1 | **The program's `SS` is not `LOW_SEG`** | `sch_switch` refuses to switch a task whose `SS` is not `LOW_SEG` (SPEC.md 8.5, `kernel/sched.inc` `sch_switch`'s first test): a DOS program would never be pre-empted, and the desktop would hang behind it | the package's IRQ0 stub moves onto the worker's own slice before it enters the kernel (§3.1) |
| P2 | **The IVT is global** | two programs both own `INT 21h`, both hook `00h`/`1Ch`/`23h`/`24h`; the kernel and the UI task need the ROM's `16h` | per-instance vectors, exchanged on entry to and exit from the instance's own slices (§3.2) |
| P3 | **The BDA's video block describes the DESKTOP** | a C runtime reads `0040:0049` (mode), `004A` (columns), `0050` (cursor), `0084` (rows) — in a window those say mode 12h / 6 / Hercules graphics, and a text program decides it is in a graphics mode | the video block is exchanged with the vectors (§3.2) — each program sees mode 3, 80x25, its own cursor |
| P4 | **A worker may not touch a file** | SPEC.md 20.6 rule 7; the file layer shares one sector buffer and the FAT snapshot | every `dos_be` door that touches the disk or `.lowbss` (96.4.1.1's own test) is posted to the UI task, which runs it — on its own stack, which is where `dos_be_go` runs it today (§3.4) |
| P5 | **A worker may not draw** | no gfx lock off the UI task | the worker writes `con_scr` and marks; it posts `OSAPI_WM_WAKE`; the window's wake handler paints the marked rows (§3.5) |
| P6 | **Output that bypasses INT 21h** | `int 10h` teletype/write/scroll, and direct `B800` writes | `int 10h` is virtualised onto `con_scr` (§3.3); `B800` cannot be (§4) |

---

## 3. The design: the multiplexer lives in the package

### 3.1 The IRQ0 stub, and why it costs the kernel nothing

While an instance's worker is executing — and **only** then — the IVT's
`08h` entry points at a stub in that instance's image. The stub:

```
dvw_irq0:                       ; IF = 0, on whatever stack was live
    ; 1. if SS != LOW_SEG (the program's stack, or the core's gate stack):
    ;    bank SS:SP in the instance's bss, load SS:SP = the worker's LOW_SEG
    ;    reserve (banked by the package every time it enters program code)
    ; 2. exchange the instance's vector set and BDA video block OUT (3.2) -
    ;    this puts the kernel's sch_isr back into IVT 08h
    ; 3. pushf / call far [dvw_k08]   - the kernel's own int 08h, which may
    ;    switch away; when this task is RESUMED, sch_resume's iret lands HERE
    ; 4. exchange IN, int 1Ch if the program hooked it (3.2.2)
    ; 5. restore the banked SS:SP if step 1 moved it; iret
```

Step 3 is the whole trick. `sch_isr` saves the task's full frame on the
`LOW_SEG` slice, which now *includes the stub's own return*, so a later
resume of this task comes back through the stub and the stub puts the
program's world back. `sch_switch` sees `SS = LOW_SEG` and does what it does
for every worker: parks `SP`, checks the canary, scans, charges.

**Why this is sound** is one fact and one discipline:

- **The fact**: SPEC.md 8.1.2.2 — *only IRQ0 falls into `sch_switch`; every
  other ISR `iret`s straight back*. So the only involuntary way off the
  instance's CPU is through IVT `08h`, which the instance owns while it runs.
- **The discipline**: every VOLUNTARY way off it — `OSAPI_TASK_YIELD`,
  `OSAPI_TASK_SLEEP`, a wait for the UI task (§3.4) — is a call the package
  makes from its own code, and the package exchanges OUT before it and IN
  after it. One wrapper, `dvw_yield`.

So **the invariant is "an instance's vectors are in the IVT exactly while its
own task holds the CPU"**, and everything else follows from it: any other task
— the UI task, another DOS window, a driver's worker — runs with the desktop's
vectors, and closing a window, killing it from the Task Manager or a fault
that tears the instance down needs **no cleanup at all**, because at that
moment some other task is running and the vectors are already out.

**What the kernel would have cost instead** (the design this replaces,
costed so it is not re-proposed as the obvious one): a foreign-`SS` arm in
`sch_switch` (~70 bytes `.text`), a per-slot record for the banked stack and a
vector-block pointer (`MAX_TASKS` = 14 x 6 = 84 bytes `.bss`), a vector
exchange at `.pick` behind a count test on *every* switch (~60 bytes, ~20
cycles a switch on every machine), a hardware-vector guard (~40), a cell to
register a task (~40 + the cell) — **~250-300 resident bytes on `kern_big`**,
ESTIMATE, for a feature most sessions never open. The package version spends
roughly the same bytes where they are paged in only with a DOS window. That is
the banner rule (CLAUDE.md, *RUNGS ARE TEMPORARY. BYTES ARE FOREVER.*) applied
before the first line is written, and it is the ON-DEMAND rule one level
further out: not a module, not even a driver — the program that needs it.

**The fallback**, if wave 1's gate finds a kernel path that reaches
`sch_switch` with an instance's vectors in (a cell that blocks internally,
say): wrap that cell in `dvw_yield`'s exchange. The kernel version above is
the last resort, and is a conversation with the owner (it is resident bytes).

### 3.2 The exchange: vectors and the BDA's video block

One routine, `dvw_xchg`, symmetric — the same loop swaps in and swaps out —
over a byte list of vectors and a byte range of the BDA:

| | entries | why |
|---|---|---|
| vectors | `00h` `10h` `16h` `1Ch` `20h` `21h` `23h` `24h` `27h` `28h` `29h` `2Fh` `33h` | the ones a DOS program calls or routinely hooks (a C runtime hooks `00h`, `23h`, `24h`; a utility hooks `1Ch`, `2Fh`); `10h`/`16h`/`33h` are the virtual BIOS and mouse, `29h` is DOS's fast console out |
| BDA | `0040:0049`..`0066`, `0040:0084`..`008A` | mode, columns, page size and offset, the eight cursor positions, cursor shape, active page, CRTC base, mode/palette registers; rows-1, char height, EGA info |

13 vectors (52 bytes) + 37 BDA bytes per instance: **89 bytes of bss**, and
`~47 word exchanges` each way — **ESTIMATE ~0.45 ms a direction on a 4.77 MHz
8088**, so ~0.9 ms of each 55 ms tick an instance holds: **~1.6% of its own
slice**, and nothing at all for any other task.

**Not in the list, deliberately:**

- **`1Bh`** (Ctrl-Break) — the ROM's keyboard ISR calls it during *whichever*
  task is running, so an exchanged `1Bh` would break the program in the window
  the user was not typing into. Break and Ctrl+C reach a program through its
  own window's keys instead (§3.6), which is where DOS checks them anyway: at
  its next INT 21h call.
- **`08h`, `09h`, the IRQ lines** — the kernel's (§3.7).
- **`13h`** — see §3.4: a program's direct disk call is posted, not exchanged.

#### 3.2.1 Kernel code that runs on the instance's task

A kernel cell the package calls from its worker (`OSAPI_WM_WAKE`, a mem slot)
runs with the instance's vectors IN. None of the exchanged vectors is one the
kernel invokes from a cell — the kernel reads the keyboard on the UI task and
calls `int 10h` only for mode sets inside a bracket — but **the virtual `10h`
and `16h` test the caller's `CS` anyway** and pass a `KERNEL_SEG` or ROM-segment
caller to the desktop's vector: ~8 bytes a trap for a guarantee that does not
depend on an audit staying true.

#### 3.2.2 `1Ch` and a program's own timer

The ROM's `int 08h` runs from inside `sch_isr`, i.e. with the vectors OUT, so
a program's `1Ch` hook would never fire. The stub calls it itself at step 4 —
once per tick in which the instance holds the CPU. That is DESQview's
semantics (a background program's timer runs at its share of the machine) and
it is ~6 bytes.

### 3.3 The virtual video BIOS (`int 10h`)

Implemented against `con_scr` and the console's cursor, marking with
`con_markc`/`con_mark` so the band repaints the cells that changed:

| `AH` | | |
|---|---|---|
| `00h` | set mode | `2`/`3`/`7` clear the screen and are accepted; `0`/`1` (40 columns) accepted as 80 with the left half used (96.34.2's rule); **anything else is a graphics mode**, and the instance asks to go full screen (§4.2) |
| `01h` `02h` `03h` | cursor shape / set / get | the console's cursor; `0040:0050` kept in step, since programs read it |
| `05h` | active page | page 0 only; other pages refused into full screen |
| `06h` `07h` | scroll a WINDOW up/down | the one real piece of work: a rectangle, a count and a fill attribute |
| `08h` `09h` `0Ah` | read / write char+attr / write char | |
| `0Eh` | teletype | `con_write` (it is the same thing) |
| `0Fh` | get mode | answers mode 3, 80 columns, page 0 |
| `13h` | write string | |
| `FEh` `FFh` | TopView's *get / update video buffer* | §3.3.1 |
| anything else | | returned unchanged, as an absent function |

**ESTIMATE ~450 bytes**, against `con_scrollup`/`con_write` already being
there to call.

#### 3.3.1 TopView's shadow buffer — the period's answer to `B800`

TopView published the escape hatch DESQview, Windows' DOS box and OS/2 all
then honoured: a program asks `int 10h AH=FEh` with `ES:DI` = the video
buffer it intended to write, and is handed back the SHADOW buffer to write to
instead; `AH=FFh` (`ES:DI`, `CX` cells) says which part changed. A program that
checks this — and a fair number of period programs did, because it was the
documented way to be "TopView-aware" — writes "directly to the screen" and
still runs in a window.

Here the shadow buffer **is `con_scr`**: `AH=FEh` returns its address in the
instance's segment and costs nothing to store. `AH=FFh` marks the rows. For a
program that asks once and never calls `FFh`, a row checksum (25 words, 50
bytes of bss) taken by the window's wake handler at ~4 Hz finds the changed
rows: ~6 ms of a 4.77 MHz 8088 per check (ESTIMATE, 2,000 words summed). **~80
bytes of code.**

### 3.4 File I/O: the door, served by the UI task

`dos_be_go` is the one place every file call already passes (SPEC.md 96.4.1).
On the windowed run it posts instead of calling:

1. the worker writes the door ordinal and the registers into a 12-byte request
   block in its bss, sets a byte, `OSAPI_WM_WAKE`s its window, and waits in
   `dvw_yield` until the byte clears;
2. the window's wake handler — the UI task, lock-free, legal for file slots
   (SPEC.md 74.1) — runs the door **through the existing `dos_be` table**, on
   its own stack, exactly as `dos_be_go` runs it today, and clears the byte.

The FTPD handshake (`apps/ftpd/`, SPEC.md 77) is the worked example: a single
byte written last by the producer and cleared last by the consumer, no lock.

**Which doors post is 96.4.1.1's rule verbatim** — *does any kernel code under
it touch `.lowbss` or the disk* — so `DBE_HERE` and `DBE_VKIND` stay on the
worker and are not paid twice. **ESTIMATE ~120 bytes**; latency one round-trip,
~2 task switches at 693 us (PERFORMANCE.md's table) — about 1.5 ms against
~400 ms for the `int 13h` call it usually stands in front of.

**The ROM services the core itself calls** go the same way: `int 1Ah` for
§96.13's date and time (an AT's RTC is two port writes the kernel's clock
ladder also makes, and two tasks interleaving them read the wrong register)
and a program's **direct `int 13h`**, which is posted as a refusal in a window
(*"this program reads the disk directly; run it full screen"*) because a raw
sector transfer must not share the drive with the file layer mid-operation.

### 3.5 Painting

The worker never draws. It marks, then `OSAPI_WM_WAKE`s at most once per
tick (a byte it clears from the wake handler; the kernel coalesces anyway).
The wake handler takes the gfx lock and paints the marked rows with the band
code that exists — `con_scrollpaint` for a scroll, which Telnet already uses
for exactly this.

**This is where the windowed renderer's deferred span narrowing (SPEC.md
96.33.12, last paragraph) becomes worth taking**: today the band repaints
whole rows because output arrives once, after the verb; a live program that
updates a clock in one corner will repaint that row ~18 times a second. It is
its own measured change (`disp*` and the Telnet rows assert against the band)
and is wave 2's, not a rider on wave 1. **Built: §10.7.**

### 3.6 Input

The window's key callback (`dos_onkey`, today the prompt's line editor) feeds
a 16-entry ring in the instance's bss (32 bytes) while a program runs.

- **`int 16h`** (`AH=00h/01h/02h`, `10h/11h/12h`) is answered out of the ring;
  `02h`/`12h` answer the real shift state, which is global and true.
- **Idle detection**, because this is what makes several programs bearable on
  one 8088: `AH=00h` with an empty ring SLEEPS (`dvw_yield` + sleep until a key
  is posted, the key callback `OSAPI_WM_WAKE`-free since it runs on the UI task
  and can just clear a byte); `AH=01h` returning "no key" k times in a row
  yields; `int 28h` and INT 21h `AH=0Bh`/`06h` polling likewise. DESQview
  called this *"optimise keyboard polling"* and shipped it on by default.
- **Ctrl+C / Ctrl+Break** set a flag the gate tests at the next INT 21h call
  and raises `int 23h` there — which is when DOS raises it too.

`dos_getkey` (`apps/dos/dos.asm`) already polls `int 16h` and samples the
mouse between polls; in the window it polls the virtual `16h` and inherits the
idle yield for free. **ESTIMATE ~150 bytes** + 34 of bss.

`INT 33h` in a window — the pointer translated to the band's cells, live only
while the window is in front — is wave 5's and is costed there.

### 3.7 What makes a program NOT windowable, and how it is caught

| the program | caught by | what happens |
|---|---|---|
| sets a graphics mode (`int 10h AH=00h`, mode not 0-3/7) | the virtual `10h` | the instance asks to go full screen (§4.2) |
| hooks `08h`, `09h` or an IRQ line through `AH=25h` | the gate's `AH=25h` | the same |
| pokes `08h`/`09h` into the IVT directly | `dvw_xchg` on the way OUT compares the two words with the desktop's — ~20 bytes | the desktop's words are put back, the instance is frozen, the window says why (SPEC.md 47: grey a fact) |
| reprograms the PIT or the PIC mask | **nothing** — port writes cannot be seen on an 8086 | the machine's clock or an IRQ breaks while that window runs. Stated as the limit it is; the user's remedy is full screen, which restores both (SPEC.md 96.5's ledger) |
| reads the keyboard buffer at `0040:001A` directly | nothing | it sees the desktop's keys. Rare; full screen |
| writes `B800` directly | §4 | |

**ESTIMATE ~60 bytes** for the two refusals that can be caught.

---

## 4. `B800`, adapter by adapter — the one thing a window cannot take

An 8086 has no MMU, so a program's `mov es:[di], ax` into `B800` goes where
`B800` IS on that machine. What that means depends on the adapter the desktop
is on, and it is not the same answer on any two:

| desktop | what `B8000` is while the desktop is up | a direct write in a window |
|---|---|---|
| **CGA** (640x200, mode 6) | **the desktop's own framebuffer** | lands on the desktop as pixels. Cosmetic — a repaint mends it — but visible and immediate. Wave 1 must confirm nothing of the kernel's lives in `B800:3E80..3FFF`, the 384 bytes mode 6 does not show |
| **VGA / EGA** (mode 12h / 10h) | **not decoded at all** — the Graphics Controller's memory map is `A0000-AFFFF` in both modes | vanishes. Harmless and invisible: the window simply shows nothing new |
| **lone Hercules** | **not decoded** — `3BFh` bit 1 is left clear on purpose (`kernel/viddet.inc`, `vidsel.inc`: the page is a CGA's address) | vanishes, as on VGA. §7.2 is the one capture this permits |
| **Hercules + CGA** (the owner's 5150 has both) | **the CGA's screen** | lands on the other monitor, readably. Not a capture: a second screen |

So **no adapter lets a window capture a direct writer as it runs**, and the
design does not pretend to. What it does is the period's behaviour, and every
piece of it is already built:

### 4.1 Full screen keeps the program running

`Alt+Enter` on a running windowed program: the UI task enters an
`OSAPI_FSX_RUN` bracket with **`FSXF_KEEPWORKER` naming the instance's own
worker**, seeds the glass from `con_scr` (SPEC.md 96.34.4's `dos_fsx_con`,
unchanged), and the virtual `10h` switches to *mirror*: it writes `con_scr`
AND the real VRAM, and passes graphics mode sets to the ROM. The program never
stops; it is now on the glass.

### 4.2 Back to the window: capture, and decide

`Alt+Enter` again (SPEC.md 96.33.5.1's door, both directions): before
`dos_snap` (96.34, unchanged) copies the glass into `con_scr`, **one
`repe cmpsw` of the glass against `con_scr`** answers the question the design
could not answer up front — *did anything other than our own `int 10h` write
this screen?* The two are identical unless the program wrote VRAM itself.

- **Identical**: the program is well-behaved; it carries on running in the
  window.
- **Different**: it writes the screen itself. Its last screen is captured
  into the window as today, and **the instance is frozen there** — its worker
  parked, its window titled as suspended — until it is taken full screen
  again. This is DESQview on an 8088, exactly: a program that "writes directly
  to the screen" ran in the foreground and stood still in the background,
  showing its last picture.
- **In a graphics mode**: nothing to compare and nothing to capture (96.34.1);
  frozen, the console keeps its history.

That one compare is ~9 ms of a 4.77 MHz 8088 (ESTIMATE, 2,000 words), taken
once per `Alt+Enter`, and ~30 bytes. **The bit is sticky for the session**, so
a program that has once written `B800` is never let loose in a window where,
on a CGA, it would paint the desktop.

### 4.3 What a FIRST launch does

A program has never been seen until it runs, so the first launch needs a
default:

- **On VGA, EGA and a lone Hercules: windowed.** A direct writer is harmless
  there — its window stays blank and `Alt+Enter` is the visible way on, which
  the window's status line names after a second of output-free running with
  the program still alive.
- **On CGA: windowed too, with the cost said plainly** — a direct writer
  scribbles on the desktop until `Alt+Enter`, and a repaint mends it. The
  alternative (full screen by default on CGA) gives up the feature on the
  adapter it is most wanted on.

The setup area (SPEC.md 96.32.2) gains a **Run in: Window / Full screen**
choice so a known direct writer can be launched straight to the glass, and
**it is remembered per program** in `SYSTEM/APPDATA/` beside DOS's other
state (SPEC.md 19.9) once §4.2 has decided it — ~80 bytes plus the list
(ESTIMATE). A shortcut (SPEC.md 96.21) is a Windows `.LNK` and has no field
for it; there is no PIF, so the PIF's *Directly modifies screen* bit is not
ours to read, and this list stands in for it.

---

## 5. The rest of the shape

### 5.1 One DOS window = one `DOS.O88` instance = one worker

SPEC.md 20.6: a package instance owns **exactly one** worker. A DOS window is
already an instance, so this needs no kernel change: open a second DOS window
and you have a second instance with its own region, bss, console, gate stack,
vector block and worker. `INST_MAX` is 12 and `MAX_TASKS` 14 (`kernel/sched.inc`,
MEASURED), so the task table allows twelve; memory binds long before (§6.3).

The worker is spawned from the click that launches (`OSAPI_TASK_SPAWN` wants
the gfx lock held — a window callback). Its slice class is measured in wave 1
with `tools/stkdepth.py` (STACK-SLOTS-PLAN §12): it holds the stub's frame,
`sch_isr`'s, and the non-posted doors' calls — probably 256, possibly 192.

### 5.2 The core's gate stack

The INT 21h gate already runs on DOS's own stack (SPEC.md 96.7.2, 510 bytes
of the instance's bss), never on the program's. That is unchanged: it is a
foreign `SS` like the program's, and the stub's step 1 covers both.

### 5.3 Hibernate, kern_dos, kern_mega

- **Hibernate refuses while a windowed DOS program runs** — a program holding
  absolute segments in a pinned arena is the case its image cannot carry
  safely. One test in the existing refusal list.
- **Arm 3 (`kern_dos`, SPEC.md 96.40) is unchanged**: it is the whole machine
  for one program and remains the way to give a program 600 KB.
- **kern_mega**: the windowed DOS is refused there (KERN-MEGA-DOS-PLAN 2.1),
  so the multiplexer is `%ifndef DOS_MEGA` — **zero bytes on kern_mega's
  disks** (rule 4's arm rule, SPEC.md 20.18).
- **kern_small**: DOS is not on the small disks (SPEC.md 24.5) — zero.

### 5.4 What a full-screen window does to the others

Entering a bracket freezes every task but the exclusive one and its kept
worker (SPEC.md 53.2), so **the other DOS windows pause while one is full
screen** — where DESQview kept them running. Keeping them would mean an fsx
freeze that keeps several workers, which is a kernel change for a
behaviour nobody has asked for; it is recorded and not proposed.

### 5.5 Heap: every running window is a wall

A running program's arena cannot move (it holds absolute segments), and a
worker pins its instance's region (SPEC.md 66.6.2) unless it declares a restart
point, which a DOS program cannot have. So each running window is **two pinned
claims for its life**. Both are claimed `OSAPI_MEM_CLAIM_HI` (top-down) so the
walls stack at the ceiling rather than standing mid-arena, and the
`tests/pins.txt` line each one needs says so (docs/plans/NO-PINNING-PLAN.md's
policy allows a claim pinned by what it IS). At the prompt — no program — both
are movable again, as the trace part is today (SPEC.md 96.29.1.2).

### 5.6 Sizing the arena — the lever that matters most

Today: one claim of `OSAPI_MEM_AVAIL`, everything (SPEC.md 96.3, 96.35). With
several programs that is wrong: the first takes the machine and the second
cannot start. The windowed run sizes the arena **from the program**:

- an `.EXE`: image + `MINALLOC`, raised towards `MAXALLOC` up to a cap — the
  header's own statement of what it needs, which DOS honours too (DOS hands
  `MAXALLOC`, usually `FFFFh` = everything; this deliberately does not);
- a `.COM`: **64 KB**, the most a `.COM` can address without asking (`AH=4Ah`
  and `48h` still work inside the arena, against the MCB chain of SPEC.md 96.9);
- an override on the setup page (*Memory: 64 / 128 / 256 KB / All*), remembered
  with §4.3's choice.

`All` is today's behaviour and stays the default for a single window, so a
user who never opens a second DOS window sees no change. **ESTIMATE ~100
bytes.**

---

## 6. Byte costs, and what keeps them small

### 6.1 The baseline (MEASURED, this tree)

| quantity | bytes | source |
|---|---|---|
| `DOS.O88`'s windowed image (part 0, unpacked) | 38,511 | `build/dos.o88` header `+8` |
| its bss | 9,560 | header `+10` |
| **one DOS instance's region** | **48,128** | `roundup512(image + bss)` (SPEC.md 21 step 4) |
| of which the core's state (`DBSS`) | 3,509 | `CORE_BSS_SIZE`, `apps/dos/doscall.inc` |
| of which the console | ~4,100 | `con_scr` 4,000 + its bitmaps and spans |
| `kern_big` spare | 3,584 (7 steps) | `kernsize[big]` on this tree |

### 6.2 What the plan adds (ESTIMATES)

| piece | § | image | bss |
|---|---|---|---|
| IRQ0 stub, `dvw_xchg`, `dvw_yield`, the reserve banking | 3.1, 3.2 | ~160 | ~95 |
| run on a worker; spawn, park, freeze | 5.1 | ~150 | ~6 |
| posted doors at `dos_be_go`, ROM posts | 3.4 | ~120 | 12 |
| virtual `int 10h` | 3.3 | ~450 | — |
| TopView `FEh`/`FFh` and the row checksum | 3.3.1 | ~80 | 50 |
| virtual `int 16h`, key ring, idle yield, break | 3.6 | ~150 | 34 |
| wake-driven paint, rate byte | 3.5 | ~60 | 2 |
| live `Alt+Enter` both ways, the glass compare | 4.1, 4.2 | ~150 | 2 |
| the two refusals | 3.7 | ~60 | — |
| Run in / Memory choices, the APPDATA list | 4.3, 5.6 | ~200 | ~20 |
| `INT 33h` in a window (wave 5) | 3.6 | ~200 | ~10 |
| **total** | | **~1,780** | **~230** |
| **kernel, every build** | | **0** | **0** |

So **~2 KB of region per open DOS window, 0 resident**, against a 48 KB
region — and the system disk carries it compressed (lz4 packs code ~1.4:1 in
this tree's measurements, so ~1.3 KB of the 360 KB disk; wave 1 measures it,
since part 0 is packed with the rest of `DOS.O88`).

### 6.3 What a window costs the machine

Per window: **~50 KB of region + the arena**. KERN-DOS-PLAN measures **449 KB**
of arena free beside one DOS instance on a 640 KB machine, i.e. ~497 KB before
the first instance:

| programs | per window | windows that fit in ~497 KB |
|---|---|---|
| 64 KB `.COM`s | ~114 KB | **4** |
| 128 KB `.EXE`s | ~178 KB | **2**, a third if the disk cache sheds |
| one with `All` | everything | 1 — today's behaviour |

The arena is 55-70% of every row. **That is why §5.6 is the lever and the
package's ~2 KB is not**: shaving the code buys nothing a user can count;
sizing the arena from the program is the difference between one window and
four.

### 6.4 The minimisation rules this plan follows

1. **Nothing resident.** The kernel version is costed (§3.1) and refused
   unless wave 1 proves the package version unsound.
2. **No new storage for a screen.** `con_scr` is the virtual `B800` and the
   TopView shadow buffer; the capture and the seed are the existing routines.
3. **One seam for file I/O.** The post is in `dos_be_go`, so the 22 doors
   change in one place and the `t_dosseam` gate keeps meaning what it means.
4. **Exchange, don't save-and-restore.** One symmetric `xchg` loop is both
   directions; 89 bytes of state, not the 1,280 the bracket's ledger keeps
   (SPEC.md 96.5 saves the whole IVT and BDA because a bracket hands over the
   whole machine — a window hands over thirteen vectors and a video block).
5. **Refuse by fact, not by guess.** A graphics mode, an `08h` hook, a glass
   that differs from our own writes: each is detected at the instruction that
   proves it, and each answer is the full screen that already exists.

---

## 7. Deferred, with the arithmetic

### 7.1 One host, many sessions (~40 KB a window)

The region is 48 KB and ~38.5 KB of it is the same code in every window. A
single `DOS.O88` instance owning N windows would pay it once. It needs:

- **more than one worker per instance** — SPEC.md 20.6 says one, so a kernel
  change and an ABI clarification (ESTIMATE ~40-60 resident bytes: the
  instance's worker count and `inst_task_die` killing all of them);
- **the core made per-session.** The core's state is ONE contiguous block
  (`DBSS`, 3,509 bytes, based at `os88_image_end` — SPEC.md 96.44.2), so the
  cheapest shape is a core lock with a LAZY swap of that block when a
  different session enters INT 21h. Without the bracket-only IVT/BDA saves it
  is ~2,230 bytes: ~8 ms a swap on a 4.77 MHz 8088 (ESTIMATE). Two chatty
  programs alternating INT 21h every tick would spend **~30% of the machine
  swapping** — so it wants the hot/cold split of that block measured first.

What it buys is ~40 KB a window, which §6.3's table turns into **about one
extra window** on a 640 KB machine. Worth revisiting only once §5.6 has made
arenas small and the region is the larger share.

### 7.2 One direct writer captured live, on a lone Hercules

On a Hercules with no CGA beside it, setting `3BFh` bit 1 turns `B8000-BFFFF`
into the card's second 32 KB page: real RAM nobody displays, which the desktop
does not use (`fsx_page` enables it only inside a bracket, SPEC.md 53.10). A
direct writer's text then lands somewhere readable, and the row checksum of
§3.3.1 pointed at `B800` instead of `con_scr` would carry it into the window
live. **One** such program at a time, because there is one page (two would
need 4 KB copied at every switch between them, ~14 ms). ~60 bytes, and the
only live capture of a direct writer any adapter here permits.

### 7.3 Background DOS windows that keep running while one is full screen

§5.4. A kernel change to the fsx freeze; not proposed.

---

## 8. Waves

Each wave lands on one branch and one draft PR (CLAUDE.md, *Building an app
or game in waves*). Soak rows are named per wave and run **once, after the
last** (the Testing section's rule).

| wave | delivers | user-visible outcome | rows that cover it (named, run at the end) |
|---|---|---|---|
| **0** | the measurements this plan quotes as ESTIMATES: the stub's soundness audit (every path into `sch_switch` from a worker — the call graph `t_dosseam` already walks, extended to the kernel cells the package calls), the worker's slice depth, the B800 spare-bytes check on CGA | none — it decides go/no-go for the zero-kernel shape | a new `dvwaudit` fast row (host-side, call graph) |
| **1** | §3.1, 3.2, 3.4, 3.5: a program runs on its worker in its window; INT 21h output live; file I/O posted; the SPEC section written first | `TYPE`-style and `DIR`-style programs print line by line into the window while the desktop stays live; a second window's prompt still answers | `doscon`, `kdmouse`, `dos*`, and a new `doswlive` (a `tests/dostrap/` probe printing one line a second; assert the band advances while a Calculator window answers clicks) |
| **2** | §3.3, 3.6: virtual `10h` and `16h`, idle yield; span-narrowed band paint (96.33.12's deferred change, measured) | two DOS windows each running a different program at once; a program waiting on a key costs nothing | `doswlive`, a new `doswtwo`, `disp*` |
| **3** | §4.1, 4.2: live `Alt+Enter` both ways, the glass compare, freeze-in-window | a direct-writing program goes full screen and keeps running; back in the window it is shown suspended with its last screen | `doscon`, a new `doswzoom` (one well-behaved probe resumes running; one `B800` writer freezes) |
| **4** | §3.3.1: TopView `FEh`/`FFh` | a TopView-aware program writes "directly" and runs in a window | `doswtv` with a probe that does exactly that |
| **5** | §5.6, 3.7, 4.3, `INT 33h` in a window | four small programs at once on a 640 KB machine; a program that hooks `09h` is sent full screen with the reason on the glass | `doswtwo` extended to four; `dos*` |
| **6** (optional) | §7.2 | a direct writer live in a window on a lone Hercules | `doswherc`, on `os8088_5150_herc_gla` |

**Every emulator row is MartyPC's** (docs/TESTING.md): nothing here is on
QEMU's closed list, and the idle-yield and CPU-share claims are exactly what
only a cycle-accurate 8088 can measure. **The claim to hold this to** is a
measurement: with two windows each running a busy probe, the desktop's
pointer and a window drag are still ISR-paced (SPEC.md 8.1.2), and each probe
gets ~half of what it gets alone.

---

## 10. Wave 1, as built — and what it found

**Built and green**: Program > Run in Window (opt-in; Run, a double-click and
the prompt still take the bracket). `tests/doswlive.py` drives a probe that
WAITS BY SPINNING on the BIOS tick, so every leg is a statement about
pre-emption: lines arrive in the band while it runs, through INT 21h and the
video BIOS both; another window comes to the front and it keeps printing; the
window's key is its INT 21h `AH=08h` answer; IVT 08h is the kernel's again
afterwards (sampled twelve times - one read passed with the exchange-out
deliberately removed, because the stub swaps the world back around every tick
it catches); and closing the window mid-run stops the program and leaves the
machine ticking with the whole IVT as it was. 14.9 s on MartyPC.

**MEASURED cost**, against `b1e4085`: the windowed host's image **+2,345**
and bss **+335**, so a DOS window's region is **2,560 bytes** larger;
`DOS.O88` on the disk **+2,048**. The core part (`doscore.bin`) and
`kern_dos` (`kerndos.bin`) are **byte-identical**, and so is every kernel
(`kernsize[big]` +0). §6.2's estimate was ~1,780 + ~230: the virtual video
BIOS and the 22 posting stubs (110 bytes + a 48-byte table) are what it
under-counted.

### 10.1 Two decisions changed

- **IVT 08h and 09h are EXCHANGED, not refused** (§3.7 said refuse). The
  exchange already puts the instance's IVT 08h in place, so a program's own
  `AH=25h` hook of 08h or 09h is simply part of its world: it runs on every
  IRQ in the program's own slices and in nobody else's, and chains back into
  `dvw_irq0`. The exchanged set is 00h, 08h-09h and 10h-3Fh plus the BDA's
  memory word, video block and EGA rows - **121 words, about 2 ms each way on
  the 8088 (ESTIMATE)**, against §3.2's 47 words and 0.45 ms. Narrowing it is
  wave 2's (most of 10h-1Fh never changes).
- **The close is REFUSED and finished by the package** (SPEC.md 75.1's
  pattern). Letting it through hid the window and left the worker to free the
  region at its next `OSAPI_TASK_ALIVE`, which it reached while the UI task
  was still inside the wake handler ending the run: the region was freed
  under it and the machine ran into the heap with the tick stopped - measured,
  with Works and with the probe alike. The end of a run now belongs to the UI
  task, which asks for the close last.

### 10.2 Three things found on the way

- **A YIELD IS NOT IDLE.** Every eighth empty `AH=01h` yielded, per §3.6. With
  nothing else ready a yield returns at once, so a program waiting for a key
  spent its slice exchanging its world in and out: **every CPU sample landed
  in `dvw_walk`**. It sleeps a tick now.
- **The kill could not reach a program waiting for a key**: INT 21h's own key
  read polls the virtual 16h from the GATE's stack, where `dvw_irq0` never
  sees a foreign `SS`. The poll hears the kill itself now.
- **THE OWNER'S FIRST FIELD REPORT, off Prince of Persia's installer**:
  *"after the first input it only updates after dragging the DOS window"*.
  The paint was posted only by `dvw_irq0`, which runs only when a tick lands
  on the program - and a program that prints its question and then waits
  for the answer spends every tick asleep in `dvw_nap`. `dvw_nap` posts it
  too now. `doswlive`'s probe asks a second question for exactly this, and
  leg 4 reads `con_drb` - what a paint spends - while it waits: `0000c001`,
  rows still owed, without the post.
- **`kdarena` failed in the wave's soak and is PRE-EXISTING**:
  `os88bisect classify` reads 3/3 failed at `b1e4085` on the same leg (the
  program given 580 KB where the machine has room for 581). Every other
  `dos*`/`kd*` row, 80 of 81, passed.
- **The Makefile did not list `dosw.inc`**, so edits to it rebuilt nothing,
  and the row ran yesterday's DOS against a symbol map of today's - which
  failed at leg 1 on an EMPTY band (a one-byte shift of every bss cell) and
  read exactly like a parity bug in DOS. The five DOS rules list it now.

### 10.3 The census, program by program

| program | what it does with the screen and the machine | in a window today |
|---|---|---|
| `tests/dostrap/dwtick.asm` | INT 21h and int 10h teletype; spins on 0040:006C | **live** |
| IBM DOS 3.30 `XCOPY.EXE` (the owner's disk 1) | INT 21h only | **the same output as the bracket**, which is `Invalid parameter` in BOTH: a DOS defect that predates this work (the core has no `AH=37h`, which DOS 3.3 utilities ask for the switch character - a suspicion, not yet a trace). Typed bare, `XCOPY` is the shell's own verb and IBM's never runs |
| **Microsoft Works 1.05** (the owner's disk) | reads the mode with `int 10h AH=0Fh`, then writes the screen ITSELF at B800 (mode 3) or B000 (mode 7); hooks 08h, 09h, 1Bh and 24h with `AH=25h`, chaining; installs an INT 33h event handler; checks `int 16h AX=55FEh` for a cooperating keyboard driver and hooks 09h only when none answers | **runs, cannot be seen.** It asks our mode, gets 3, and writes B800 directly - which on MartyPC's Hercules reads back as B000, so it wrote over the desktop's framebuffer. Its 08h and 09h hooks are contained by the exchange and closing the window stops it cleanly. **It is a wave-3 program**: Alt+Enter to the full screen live, frozen with its last screen when it comes back |

**So Works is not a first target for the live window** and cannot become one
on any adapter: it is a direct writer by construction, and §4's table is the
whole answer. What it IS is the best target for wave 3, the live Alt+Enter -
and for §7.2's capture on a machine where B800 is RAM nobody shows (a lone
Hercules with page 1 enabled, or the owner's 5150, where B800 is the CGA's).

## 10.4 Wave 3, as built — the default, and the live full screen

**The owner's question was whether a program's intent can be detected and
shunted to the full screen; the answer splits exactly in two.** A graphics
mode set CAN: it has to come through the virtual `int 10h`, so the window
opens the bracket at that instruction, before a pixel is drawn, and the
program never stops (`FSXF_KEEPWORKER`). A program writing the text screen
ITSELF cannot be seen on an 8086 at all - Works is that program. So the
default stays the bracket, as the owner directed for this case, and `Run in
Window` is the choice for a program known to print.

Built: the shunt; Alt+Enter both ways on a live program; back to the window
FROZEN on its captured last screen (§4.2's shape, with no comparison needed:
the freeze is unconditional, so nothing that draws its own screen is ever let
loose in a window); a graphics program cannot come back; a program that ends
on the full screen leaves its last screen in the console. **+528 image, +6
bss** against wave 1, measured. `doswfs` drives all of it in 28.7 s and is red
with the shunt removed. §4.2's glass comparison is therefore NOT built - it
would let a well-behaved program come back running, which is wave 2's to
decide once the virtual BIOS is complete.

## 10.5 Wave 2's speed half, as built

**MEASURED**, by `doswspin`: a CPU-bound program in a window ran at **81.1%**
of its bracketed speed, and **15.3%** of a sampled profile was the per-tick
exchange loop - §3.2's estimate of ~1.6% was off by ten times, because the
loop tested a mode byte on every word. Two blocks and `rep movsw` take it to
**88.8%** (SPEC.md 96.54.3) for +36 image and +242 bss. The vector set was
not narrowed: a vector outside it would carry a program's hook into every
other task. The band's span-narrowed paint (§3.5) is §10.7.

## 10.7 Band paint, as built - the span, not the row

**MEASURED**, by `doswcell` on MartyPC: one cell changed by a live program -
`int 10h AH=09h` in place, or a teletype digit and a backspace - cost the band
**81,013 guest cycles (17.0 ms) on Hercules and 85,596 (17.9 ms) on VGA** a
pass, 47,027 of it composing eighty cells to change one. SPEC.md 96.33.12's
span is honoured by the windowed renderer now (`con_rowspan`), the row walk
skips clean bitmap bytes, and `con_markcells` marks cells rather than rows: the
same pass is **11,305 (2.4 ms) and 12,429 (2.6 ms)**, the blit still one call.
The caret is tracked by the renderer (`con_curmove`), because a span does not
forgive a mover that marks one side - and building it found the caret had
never been drawn at all (SPEC.md 96.33.12). +215 image and +2 bss of DOS.O88.

## 10.6 Waves 4 and 6, as built — and Works, live

**Built**: TopView's `AH=FEh`/`FFh` answered with the window's own cells and
row sums (SPEC.md 96.54.5), and the CAPTURE of a second card's text RAM
(96.54.6) - which turned out to be the general form of §7.2: the RAM nobody
shows is the other card's, `B800` under a Hercules desktop and `B000` under
anything else, decided at every run from published facts (RAM, not an alias
of the desktop's framebuffer, desktop not extended onto it). The lone
Hercules's second page was NOT enabled in this wave: it means writing `3BFh`
under a kernel that keeps the bit clear on purpose. **It is now** (SPEC.md
96.54.6.1, +336 bytes): the bit is kept clear only because a CGA would answer
at B800 too, and the CGA case is tested first, so it is turned on only where
nothing else can. What this wave read as "MartyPC decodes B800 onto B000"
was MartyPC latching its decode on a `3B8h` write and nowhere else, so
`dvw_hpage` writes the desktop's own 0Ah there after each `3BFh` change;
86Box maps B8000 straight off `3BFh`, as the card does, and passes the row
too. The first build made the page a CAPTURE like the CGA's and broke every
BIOS-output program on a lone Hercules - the capture path displaces the
exact virtual int 10h - so it is WATCHED instead: int 10h stays ours, one row
of the page is summed per pass inside the tick, and the first direct write
promotes the window to a full per-row copy. 0.6 points of `doswspin`.

**Microsoft Works 1.05 runs live in a window** on the owner's arrangement
(Hercules desktop + CGA), and it took three findings to get there, all three
Works' own:

1. its screen writer waits for a CGA retrace before every character, and a
   card Single mode only blanks has no retrace - so the capture programs the
   card's 6845 (`dvw_cprog`);
2. its blink setting writes `3B8h`, `3D8h` and `3C0h` at once, dropping a
   Hercules desktop to text - so the desktop card's mode register is put back
   every time the program's world goes out (`dvw_vguard`, 96.54.7); that is
   also what every early Works screenshot here was;
3. the DOS `.EXE` loader reads the WHOLE FILE (319 KB) where DOS reads the
   load module (2.8 KB), so the window's 128 KB refused it - the Memory limit
   is the way round today, the refusal now says so in the window's terms, and
   reading only the load module is with an agent.

`doswcap` is the gate (25 s, red with the detector off). +940 image, +57 bss.

## 10.8 Wave 5, as built — INT 33h in a window

SPEC.md 96.54.4. The pointer is translated to the band: column x 8 and row x
8 from the band's top-left and `[con_vtop]`, clamped, buttons live only for a
press whose down edge was read over the band with the window in front at that
sample and the one before (so the raising click is the desktop's). It is two
host hooks (`DHK_MOUSE`, `DHK_TXT` refusing) in `apps/dos/doswm.inc`, bound
for the run; the full screen keeps the bracket's scale. **The band is sampled
once a BIOS tick**: asking `WM_GEOM`/`WM_CONTENT`/`WM_TOP` on every read
measured 143 polls a second against 428, and per tick it is 361-367. It found
two defects: an `0Ch` handler never ran for a program waiting on a key in its
window (it sleeps through the ticks with its world out), mended by splitting
the core's dispatch into `dos_m33_evt` and calling it as the worker wakes;
and `[dos_m33hk]` is never cleared, which `dwm_start` mends for the window
and which still loses a second BRACKETED run's handler. **+400 image, +10
bss**, the core +20 (7 of code, 13 of alignment; `CORE_MAX` +16), against the
~200 / ~10 costed in section 6. `doswmouse` is the gate, 26.5 s, red two ways.

## 9. What would kill it

1. **Wave 0 finds a kernel path into `sch_switch` the package cannot wrap** —
   the zero-kernel shape fails and §3.1's ~250-300 resident bytes become the
   question for the owner.
2. **The ROM's `int 08h` chain or a driver's IRQ0 work assumes the
   interrupted task's vectors** — nothing in the tree is known to, and the
   chain runs on its own stack (SPEC.md 8.5) with the desktop's vectors in,
   which is the point; wave 0 checks `SOUND.DRV`'s `snd_tick` and the fsx rate
   hook explicitly.
3. **Real programs are mostly direct writers.** DOS-EXEC-PLAN §10 says *"most
   of them do"*. If the period programs the owner cares about are all `B800`
   writers, the windowed-live half buys little and the live `Alt+Enter`
   (wave 3) is the feature — still worth having, since it is what makes
   several programs usable at all, but the brief would then be mostly wave 3.
   **Wave 0 should run the owner's own list of programs through the probe of
   §4.2 (full screen, then compare) before wave 2 is built**, which turns
   this risk into a table.
