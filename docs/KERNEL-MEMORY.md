# The kernel's memory

**This document is maintained.** It is the standing account of what the
os8088 kernel spends RAM on and why, and it is updated in the same commit as
any change that moves a number in it. SPEC.md §2 is the binding contract for
the addresses; this is the reasoning behind them, and the place the guards in
`kernel/kernel.asm` send you when one fires.

The numbers come from `tools/kernsize.py`, which `make` runs on every build.
The section sizes, the rungs, the ladder, the per-module table and the theme
table are all GENERATED; `tools/kernsize.py --bless` rewrites them into this
file. Everything else here is prose, and prose goes stale: **run the tool
before quoting a figure from this page.** The figures written into the prose
below were read off the tool at the last bless.

---

## The rules — THREE sentences, THREE quantities

| | rule | guard |
|---|---|---|
| 1 | **`kern_small` BOOTS on 128KB** | guard 5, `MIN_RAM_KB` = 128 |
| 2 | **`kern_big` BOOTS on 196KB** | guard 5, `MIN_RAM_KB` = 196 |
| 3 | **`kern_big` fully RESIDES in 128KB** once it is at the desktop | guard 1, `KERN_BUDGET`, plus `tests/kernresident.py` |

**Booting on a machine and residing in one are different questions.** Guard 5
asks whether stage 1 can put its 512-byte sector and its 2,048-byte stack at
the top of that machine and still read the kernel **and the 4,096-byte stage-2
blob** in underneath (`KERNEL_SEG*16 + KERN_SIZE + BOOT2_PAD <= MIN_RAM_KB*1024
- BOOT_SECT - BOOT_STACK`) — a *reach* at one instant of boot. Rule 3 asks
what is still occupied once the desktop is up: every non-purgeable byte, **no
drivers loaded, no XMS, on VGA** (the adapter that does not drop a rung of its
own, so the rule is stated against the configuration that costs the most).
One constant used to answer both, and the boot ceiling silently became the
binding one; `kernsize` prints both lines now.

**Rule 3's number is DERIVED, not chosen.** The span starts at `KERNEL_SEG`
(0x0060) and the rule says where it ends, so `kern_big`'s `KERN_BUDGET` is
`KERN_RESIDENT_KB*1024 - KERNEL_SEG*16` = **129,536** and there is nothing
left to decide. Raising it means changing the rule. The assembler sees only
the static half of rule 3; a claim made at boot and never given back is the
other half, and `tests/kernresident.py` boots a bare VGA desktop under MartyPC
and walks `mem_tab` for it. As blessed it reads: kernel span ends 114,176,
last non-purgeable byte 114,176, limit 131,072 — 16,896 spare, with the
directory read-ahead (63KB, purgeable) the only claim on the heap.

**`kern_small`'s `KERN_BUDGET` is a literal** — 107,520, in the `%else` arm —
mirrored as `KERN_SMALL_BUDGET` beside big's so that a big build can report
it, with a `%error` that refuses a build where the two disagree. It is a
policy figure and the owner's to move; nothing derives it.

**None of the three binds a KNOB build.** `make`'s `$(KNOBS)` — anything but a
bare `make`, `make KERN_SMALL=1` and `make emu` — passes `-DKERN_KNOB`, and
guards 1 and 5 are skipped for it. A knob kernel exists to answer a question
about a machine, nobody boots it, and every machine this project tests on
has 640KB or could have. Guard 2 (`KERN_CODE_MAX`, one 64KB segment, which
nobody can raise) is the whole of what bounds it, and `kernsize` reporting a
negative spare on a knob build is not an error. The rule replaced a
hand-maintained list of exempt knobs, and the list is the argument for it:
five diagnostics were exempted one afternoon each, and the sixth case was a
141-byte fix to the *shipped* kernel that had to be cut to 38 because
`BOOTMARK=1` would not assemble with it — a knob build setting the ceiling
for the product. `make test-full`'s build matrix is the only thing that
builds the knob kernels, so a `.text` budget is really spent there.

### The three guards

| name | what it bounds | can it be raised? |
|---|---|---|
| **`KERN_BUDGET`** (guard 1) | the **footprint** — the whole contiguous span from `KERNEL_SEG` to `KERN_END`, RAM taken from the machine | big: no, it is derived from rule 3. small: by decision |
| **`KERN_CODE_MAX`** (guard 2) | the **segment** — `.text` + `.bss` in one 64KB window | **no.** It is what a 16-bit offset reaches |
| **`MIN_RAM_KB`** (guard 5) | the smallest machine the configuration **boots** on | by decision, per configuration |

The footprint and the segment are relieved by different things. The boot
overlay (SPEC.md §2.5) and the cold segment (§2.6) buy room against
`KERN_CODE_MAX` and **nothing** against `KERN_BUDGET`: overlay code is read
off the disk into memory the machine reuses once it is up, cold code is
resident. Moving a module cold to fix a footprint overrun is a no-op that
looks like a fix, and because the two rungs round separately it usually costs
a 512-byte step. The only things that take bytes off BOTH are deleting kernel
code, an on-demand module (§2.8), an overlay driver, or moving a feature out
to a package (see "The levers that move both guards" below).

### Which guard binds

As blessed (`tools/kernsize.py`, `-DKERN_SMALL` for the second row):

| | `KERN_SIZE` | of `KERN_BUDGET` | spare | `.text`+`.bss` | of `KERN_CODE_MAX` | left | before guard 5 |
|---|---:|---:|---:|---:|---:|---:|---:|
| `kern_big` | 112,640 | 129,536 | 16,896 (33 steps) | 57,281 | 65,536 | **8,255** | 79,872 |
| `kern_small` | 80,896 | 107,520 | 26,624 (52 steps) | 44,603 | 65,536 | 20,933 | 41,984 |

**On `kern_big` the SEGMENT binds**, not the footprint: 8KB of `.text`+`.bss`
against 16KB of budget, and the segment cannot be raised. A `.bss` byte is
worth a `.text` byte there, and "Moving data out of the segment" below is the
lever. On `kern_small` neither guard is near, and that is worth saying out
loud: a literal budget 26KB loose does not fail, it stops being read, and
`kern_small` was discovered broken three times when it was tight because
`all` never builds it (only `make small` and `test-full`'s `buildmatrix` row
do). Lowering `KERN_SMALL_BUDGET` is the owner's decision;
docs/plans/MONO-RECLAIM-PLAN.md records it being deliberately left where it
is while the 128KB work is open.

---

## The price of a byte is a byte

Every section's bytes are rounded up to a whole 512 before they enter the
ladder, so `KERN_SIZE` moves only when a **rung** crosses and most changes
move it by exactly zero. That is a true statement about the machine — no
boot takes another byte of RAM — and a false statement about the cost. The
bytes came out of the slack in that rung, and that slack belongs to whoever
comes next: over 512 bytes of additions exactly one crossing happens, so the
amortised price of a byte is one byte and the rung decides nothing but which
unlucky change is standing there when the bill lands.

Two arguments follow from confusing the two, and both are refused (CLAUDE.md's
rung rule):

- *"It adds 400 bytes and crosses no rung, so it is free."* It spent 400
  bytes of slack, and the next author pays 512 for a four-byte change.
- *"It saves 500 bytes and uncrosses no rung, so it buys nothing."* It bought
  500 bytes the next feature does not have to hold a conversation about.

Slack in this project has never gone unspent: the 35-move ledger in
`kernel/kernel.asm`'s `KERN_BUDGET` comment is a list of rungs that filled,
and two merges are recorded in it where neither branch crossed a rung alone
and the union did (a size pass handed five rungs back and one was re-crossed
by an unrelated 228-byte branch before the merge landed). So report a change
as **the per-section deltas, their sum, and how much of each rung is already
spent** — which is what `kernsize`'s `sections`, `footprint` and `accrued`
lines are — and never as "crossed no rung". The `accrued` figure is printed as
the bill (`15/512`) rather than the headroom, because "497 left" invites the
next 497 bytes.

What the rung genuinely decides is *which* rung a byte lands in: four free
bytes in `.cold` bind where four hundred in `.text` do not, and a byte moved
from `.bss` to `.lowbss` helps `KERN_CODE_MAX` and can cost `KERN_BUDGET` a
whole step if the low rung is full. The rung is a fact about where the next
512 comes from, never about whether a change was free.

---

## `tools/kernsize.py` — the numbers, as a command

```
$ python3 tools/kernsize.py                 # kern_big, against the blessed baseline
$ python3 tools/kernsize.py -DKERN_SMALL    # kern_small
$ python3 tools/kernsize.py --modules       # the per-module attribution
$ python3 tools/kernsize.py --json          # the raw figures
$ python3 tools/kernsize.py --bless         # rewrite the baseline and the tables in this file
```

A real run — the reading a merge gave against the blessing before it (a
size pass had landed unblessed, so the delta is that pass):

```
kernsize[big]: sections   text 50,892 -74  bss 6,389 -4  cold 39,434 -212  lowbss 9,182 +0  vgabuf 848 +0  ovl 1,417 +0  ovlw 5,037 +0   (sum -290)
kernsize[big]: rungs      image 57,344 -512 (63 left, was 497)   cold 39,936 +0 (502 left, was 290)   low 9,728 +0 (34 left, was 34)   vgabuf 1,024 +0 (176 left, was 176)
kernsize[big]: accrued    image 449/512 (87%)   cold 10/512 (1%)   low 478/512 (93%)   vgabuf 336/512 (65%)   - spent into the current rung and NOT YET BILLED
kernsize[big]: footprint  KERN_SIZE 112,640 of KERN_BUDGET 129,536 -> 16,896 spare (33 steps), was 16,384  [-512]
kernsize[big]: boot       KERN_SIZE 112,640 of 192,512 -> 79,872 before it cannot BOOT on 196KB (guard 5)
kernsize[big]: segment    .text+.bss 57,281 of KERN_CODE_MAX 65,536 -> 8,255 left
kernsize[big]: ladder     KERNEL 0x0060  COLD 0x0e60  FAT 0x1820  LOW 0x1940  VGABUF 0x1ba0  HEAP 0x1be0 = 111.5 KB   (heap KB = int 12h - 111.5)
kernsize[big]: *** the image rung UNCROSSED: 113 -> 112 steps of 512 - 512 bytes of every machine's RAM, back ***
```

Every `+n` is a delta against the baseline blessed into this file, so it reads
"since this document last told the truth": that run is the kernel compressor
and the LZ decoder (SPEC.md §20.13, §20.15) landing without a bless. Line by
line:

- **`sections`** — the raw bytes per section and the SUM. This is the PRICE.
- **`rungs`** — each section rounded to its 512-byte rung, with what is left
  inside the rung. The `image` rung is `.text` + `.bss`; `low` is `.lowbss`
  plus task 0's `STK0_SIZE`.
- **`accrued`** — how much of each current rung changes have already spent
  without crossing it. Read this before believing any spare figure.
- **`footprint`** — `KERN_SIZE` against guard 1, in bytes and 512-byte steps.
- **`boot`** — the same size against guard 5.
- **`segment`** — `.text` + `.bss` against guard 2.
- **`ladder`** — every segment base, and the line every RAM figure in the
  project falls out of: **heap KB = int 12h − 111.5** on `kern_big`
  (**80.5** on `kern_small`; both move with every rung crossing — re-read
  the line rather than carrying the number).
- **`*** ... CROSSED`** (or `UNCROSSED`) — the BILLING EVENT: the machine's
  RAM moved.

Three things about the tool:

- **It never fails the build.** The guards in `kernel.asm` refuse an overrun;
  this says how close you came. `--strict` makes "could not measure" an
  error, for a gate.
- **The figures come out of NASM**, from `kernel.asm`'s own `KIMG_PARA` /
  `COLD_PARA` / `LOW_PARA` equations behind `%ifdef KERNSIZE`, not from a
  Python copy of them. It re-assembles the kernel in a temporary directory
  and refuses to report if that binary is not the one on disk — so **`make`
  after a commit**: the build number is the commit count and the map then
  describes a different kernel.
- **A variant is not a knob.** `KERN_BIG`, `KERN_SMALL` and `KERN_EMU` each
  produce a kernel that ships, each has a baseline of its own in the block
  below, and each is blessed separately (`--bless` merges). A knob build
  (`VIDEO=cga`, `DISKCNT=1`, ...) is measured as itself, its `-D` flags
  passed through, and `--bless` refuses it — a baseline should describe a
  binary that exists on a disk. The module and theme tables are the default
  variant's alone.

**Bless in the same commit as the change**, and paste the report into the
commit message. `tests/unit/t_kernbudget.py` fails the fast tier when the
blessed `budget` is not `kernel.asm`'s, because a budget move and a size
shrink of the same amount read identically in `spare` and the baseline once
sat two moves behind. Every other blessed figure is a measurement and is
meant to lag; a merge that moves a rung and does not bless is a rung charged
to whoever blesses next, which is how a size pass once had a `.lowbss`
crossing printed against its name for 384 bytes of stacks a different merge
had added.

<!-- kernsize:begin -->
```json
{
  "big": {
    "boot2": 2250,
    "bootmax": 192512,
    "bss": 6077,
    "budget": 129536,
    "codemax": 65536,
    "cold": 38967,
    "coldpara": 2464,
    "fatpara": 288,
    "imgpara": 3584,
    "kend": 7104,
    "kseg": 96,
    "ksize": 112128,
    "lowbss": 9182,
    "lowpara": 608,
    "minramkb": 196,
    "ovl": 1417,
    "ovlw": 5037,
    "stk0": 512,
    "text": 51051,
    "vgabuf": 848,
    "vgabufpara": 64
  },
  "emu": {
    "boot2": 2250,
    "bootmax": 192512,
    "bss": 6077,
    "budget": 129536,
    "codemax": 65536,
    "cold": 38254,
    "coldpara": 2400,
    "fatpara": 288,
    "imgpara": 3584,
    "kend": 7040,
    "kseg": 96,
    "ksize": 111104,
    "lowbss": 9182,
    "lowpara": 608,
    "minramkb": 196,
    "ovl": 1418,
    "ovlw": 5037,
    "stk0": 512,
    "text": 51068,
    "vgabuf": 848,
    "vgabufpara": 64
  },
  "small": {
    "boot2": 2250,
    "bootmax": 122880,
    "bss": 4857,
    "budget": 107520,
    "codemax": 65536,
    "cold": 26782,
    "coldpara": 1696,
    "fatpara": 64,
    "imgpara": 2784,
    "kend": 5056,
    "kseg": 96,
    "ksize": 79360,
    "lowbss": 6064,
    "lowpara": 416,
    "minramkb": 128,
    "ovl": 423,
    "ovlw": 2789,
    "stk0": 512,
    "text": 39380,
    "vgabuf": 0,
    "vgabufpara": 0
  }
}
```
<!-- kernsize:end -->

---

## Where it goes

The kernel is ONE contiguous span starting at linear 0x00600, buffers
included: code, read-only data, `.bss`, the cold segment, the FAT window, the
disk buffers, every task stack and the planar decoder's buffers. Every rung
is the measured size of what it holds, rounded up to a whole 512 bytes and
nothing more, so the heap starts where *this build's* kernel happens to end.
There is no growth room anywhere in the ladder: a fixed ceiling with slack
under it is memory nothing can use, which is what the old 60KB package pool
was, and a package's region is an ordinary heap claim now (SPEC.md §20.1).

`kern_big`, as blessed (`kernsize --json`; the `.text`/`.bss`/`.cold`/
`.lowbss` figures are the `sections` line and the rungs are `kernel.asm`'s):

| rung | segment | size | what it is |
|---|---|---:|---|
| image (`.text` 50,892 + `.bss` 6,389) | `KERNEL_SEG` 0x0060 | 57,344 | resident code in the kernel's own segment, its read-only data and its scratch |
| `.cold` (39,434) | `COLD_SEG` 0x0E60 | 39,936 | resident code with a CS of its own (SPEC.md §2.6): the file system, the Standard File dialog, associations, drivers, the heap, the desktop and the on-demand modules' thunks |
| FAT window | `FAT_SEG` 0x1820 | 4,608 | `DSK_FAT_SECS` = 9 sectors of the mounted volume's FAT (SPEC.md §18.8); 2 sectors = 1,024 bytes on `kern_small` |
| `.lowbss` (9,182) + task 0's stack (512) | `LOW_SEG` 0x1940 | 9,728 | the mount-owned disk buffers, every task stack, the two private ISR stacks, and the tables that left the segment |
| `.vgabuf` (848) | `VGABUF_SEG` 0x1BA0 | 1,024 | `vga_p4tab` and `vga_pbuf`, SPEC.md §5.4.1.3's planar decoder. **The only rung a machine can decline**: `mem_floor_ax` seeds the heap floor UNDER it when `[vid_avail] & VID_A_VGA` is clear, so a mono machine's heap starts 1,024 bytes lower (§39.22). 0 on `kern_small` and on `NOPLANE` builds |
| **`KERN_SIZE`** | heap at `HEAP_SEG` 0x1BE0 | **112,640** | 111.5 KB on VGA, 110.5 on a 1bpp adapter |

`kern_small`'s ladder is `KERNEL 0x0060  COLD 0x0B60  FAT 0x1240  LOW 0x1280
HEAP 0x1420` — **80,896 bytes, 80.5 KB** — so a 128KB machine has 47.5 KB of
heap by arithmetic; `tests/small128.py` boots one under MartyPC and reads
what is actually free after the boot-time claims (50.5 KB when `kend` was
78.0 KB, docs/plans/completed/KERN-SMALL-CUT-BUILT.md). A 640KB machine
reporting 639KB has ~527 KB under `kern_big` before any driver or read-ahead
claim.

**Not in the span**: the boot overlay (`.ovl` 1,417 bytes in stage 2's blob,
`.ovlw` 5,037 bytes loaded onto the FAT window and the mount buffers, both
dead by the first desktop — see below), the on-demand modules (files read
into a heap claim when asked for, §2.8), and the menu save-under, which is a
heap claim taken by `menu_drop` and released before the picked item runs,
sized from the rect actually dropped (`menu_save_kb`, §12.4; `MENU_SAVE_KB`
= 20 is only the ceiling guard 4 proves the arithmetic cannot exceed).

### What the Task Manager shows

The memory view's `System` row is `KERN_KB` = `(KERN_SIZE + 1023) / 1024`,
**111** on `kern_big`, with one indented row per span the ladder declares,
every figure from `OSAPI_SYS_KB` (SPEC.md §20.9) so that a package never
carries a copy of the ladder:

| row | constant | bytes | shown |
|---|---|---:|---:|
| `Code+data` | `SKB_IMG` = the rest | 101,888 | 100 |
| `Stacks` | `SKB_STK` + `SKB_STK0` | 2,816 + 512 | 3 |
| `Disk bufs` | `SKB_DSK` = `DSK_WIN_BYTES` | 3,328 | 3 |
| `FAT snap` | `SKB_FAT` = `FAT_PARA`·16 | 4,608 | 5 |
| **`System`** | **`KERN_SIZE`** | **112,640** | **110** |

The KB column is rounded **cumulatively** — each running boundary to the
nearest kilobyte, each row the difference of two boundaries — so the rows
telescope to the total whatever they are individually, and no row is a
residual anybody can dump error into. `Code+data` is accumulated last and
takes the build's own remainder; it covers the cold segment, the decoder's
rung and the kernel's own tables in `.lowbss`, all of which are code or
`.bss`-class data and not buffers. Guard 7 in `kernel.asm` proves that the
parts sum to `KERN_KB` and that none is a kilobyte from what it measures.
The `HEAP` column beside `System` is the kernel's own claims, drawn in the
conventional map at their real addresses; package regions are claims too
(§20.1), at the far right because they are taken from the top of the heap
downward.

---

## What it actually takes to run

Three floors, and they are different questions:

| floor | `kern_big` | `kern_small` | where it is asserted |
|---|---:|---:|---|
| stage 1 refuses to load the kernel over itself — prints `RAM`, halts | ~118.5 KB | ~87 KB | `boot/boot.asm`'s `MEMFIT`: `HEAP_PARA` + 8 blob sectors + the sector and its 2,048-byte stack, derived from `kernsize`'s `kend` at build time. `make test RAMKB=<n>` makes the sector believe a smaller machine (SeaBIOS answers 639 whatever `-m` says) |
| the machine the configuration is CLAIMED to boot on | 196 KB | 128 KB | guard 5, `MIN_RAM_KB`; `kernsize`'s `boot` line says how far under it the kernel is |
| what is left for programs | 527 KB on 639 | 47.5 KB on 128 | the `ladder` line; `tests/small128.py` and `tests/kernresident.py` read the real figure off a booted machine |

The boot floor and the useful floor are not the same machine: a kernel that
can be loaded leaves a heap, and a heap has to hold a package image plus its
bss in one contiguous claim (a Note Pad instance is ~20KB, the small build
13.5KB) plus whatever the package claims after that. A 128KB machine runs
the OS and most of the packages on `kern_small`; what does not fit says so
(SPEC.md §24.5 is what is left off the small disks, and a package that only
wants heap refuses itself in its own words).

**A knob kernel is not what any of this is about.** It is measured as itself
and bound by the segment alone; a RAM-constrained test states its own
constraint instead of borrowing the shipped kernel's.

---

## Each region in detail

### The image — `.text` + `.bss`

One flat binary at `KERNEL_SEG:0000`, assembled `-f bin` with no linker.
`.bss` follows `.text` immediately and is uninitialised by definition, so it
costs nothing on the floppy and everything in RAM; the pair is charged
rounded up to a whole 512 bytes (the alignment invariant below). **The whole
of `.bss` is zeroed before `kmain` runs**; `nasm -f bin` zeroes nothing, which
is why anything read before that point (`[fdlg_win]`, the splash's state)
lives in `.text` as a `dw 0`.

The file on disk runs past the image rung, and the gap is where `.cold`
lands: it is declared with an explicit `start=` at the rung boundary, so NASM
emits the padding as zeros and stage 2's single contiguous read puts it on
`COLD_SEG`. Expressing it as a section start is what makes it non-circular —
padding *inside* `.text` would grow `KTEXT_SIZE`, which grows `KIMG_PARA`,
which grows the padding.

### Task stacks

Every task's stack is in `.lowbss`, addressed through SS — which is why
SS ≠ DS everywhere in the kernel (SPEC.md §1) — and the layout is:

| | bytes | what |
|---|---:|---|
| `sch_stacks` | 2,816 (`kern_small` 1,280) | the background slices, one per slot 1..`MAX_TASKS`−1 |
| `sch_chstack` | `SCH_CHSTK` = 128 | the ROM's `int 08h` chain runs here, not on the interrupted task (§8.5) |
| `mou_pstack` | `MOU_PSTK` = 128 | both mouse ISRs run here, not on the interrupted task (§9.10) |
| task 0 | `STK0_SIZE` = 512 | the UI task's, above the top of `.lowbss`, growing down onto it |

**The slices are a fixed partition of CLASSES, not a uniform array** (SPEC.md
§8.7). `SCH_PARTITION` in `kernel/sched.inc` is one `SCH_SLOT <bytes>` line
per background slot and the only place the layout is written; `sch_stkbase`,
`sch_stksize` and `SCH_STK_TOTAL` derive from it. `kern_big` (`MAX_TASKS` =
14) is 3 × 128, 6 × 192, 2 × 256, 2 × 384; `kern_small` (`MAX_TASKS` = 7) is
128, 128, 192, 192, 256, 384. A spawn takes the smallest free slice that fits
what the package declared in its header (§8.7.2), and a package that
declares nothing gets `SCH_STACK` = 384, the largest class — which is what
`CC_STACK` in `apps/cc/crt0.asm` mirrors and `tests/unit/t_mirror.py` guards.
The idle task holds one 128-byte slot for the life of the machine, so
`kern_big` has **twelve usable worker slots** and `kern_small` five. Running
out is `OSAPI_TASK_SPAWN`'s `CF=1`, which every package already degrades on;
a Timer or Bounce that cannot get a slot rolls its launch back instead.

**Every slice is checked, task 0's included.** `SCH_MAGIC` sits at the bottom
word of every slice, `task_spawn` fills the slice with `0xCC` first (§8.3),
and `sch_switch` compares the outgoing task's word through `sch_stkbase` on
every switch — slot 0 holds `STK0_BOT` and is seeded at `sched_init`, so an
overrun of the UI task's stack reaches `sch_stkdie` like any other (§8.6.1).
The 0xCC fill is also what makes the depth readable on a running machine and
not only its failure.

**What lands on a slice is the task's own chain and nothing else.** That is
the design, and it is recent: the interrupt floor on an idle slice read 82
bytes on QEMU with the ROM's `int 08h` chain and the mouse ISR on the task's
stack, and reads **32 on QEMU and 40–64 on real machines** (slot 1 of `make
stkdiag`'s panel, the design floor being 64 —
docs/plans/completed/STACK-SLOTS-PLAN.md §7.1) with both moved to the shared
stacks above. Only the six bytes the CPU pushes at an interrupt gate remain
foreign.

**Two corrections that bind anyone sizing a slice, both measured
(STACK-SLOTS-PLAN §9):**

- **SeaBIOS does NOT keep its `int 08h` frames off our stack.** Its chain
  costs 56 bytes; the IBM 5150 ROM's costs **36**. So QEMU *overstates* that
  term by 20 — while *understating* the floor (84–130 sampled, 118 on the
  machine, before the chain moved). Two errors of opposite sign, once quoted
  as one "+46" adder; anything sized off a QEMU floor plus a fixed adder is
  sized wrong. Design from the field floors, and take the deciding number on
  the 5150.
- **The mouse ISR's depth is set by the ADAPTER, not the mouse**: ~54 bytes
  on a planar adapter, 30 on Hercules, 23 on CGA, because `cur_move`'s 1bpp
  path is the shallow one. A class is cut from the deepest adapter.

**The instruments:**

| | what it says | how |
|---|---|---|
| `tools/stkwater.py` | how deep every slice HAS been | reads the 0xCC fill out of `LOW_SEG` on a live guest (`tests/ftpd.py --kfz` drives a whole FTP session first); the slice sizes come off the kernel via `os88sym` |
| `tools/stkdepth.py` | WHERE the depth comes from, statically | walks the NASM listing and prices every call chain (`--from NAME`, `--leaf NAME` to price a cut before writing it); `tests/unit/t_stkclass.py` reads its answer for every package's declared class |
| `tests/stk0water.py` | task 0's high water | the same fill, below task 0's saved SP; reads 238 against `STK0_SIZE` = 512 |
| `tests/stackprobe` | every slice while something else works, on real hardware | a package that fills and spins; `make stackprobe`, boot it in the other drive |
| `make stkdiag` | what an INTERRUPT costs a slice, by BIOS and adapter | a kernel knob (STACK-SLOTS-PLAN §10): three 90-second hands-off phases, read by `tools/stkdiagread.py` on an emulator |

Quote a QEMU reading as a lower bound, never as the margin: a worker's pass
there finishes between ticks, so the fill can carry no timer frames at all,
and three runs of one gate read 208, 202 and 178.

**Where the margins stand** (STACK-SLOTS-PLAN §12's survey of every shipped
worker): 192 is the modal class with eleven of the twenty; Frotz fixes the
top class, a 22-level chain reading **240 of 384**, the thinnest margin in
the tree at 1.26× and accepted because a slice's depth is deterministic now;
task 0 reads 238 of 512, 2.08× the heavier 246 SPEC.md §15.1 divides by. The
field once took a worker through the floor — `ETHER.DRV`'s service worker at
220 of a slice that was 256 bytes at the time, during a 300KB FTP upload
(docs/FIELD-NOTES.md §29.6) — which is what moved `SCH_STACK` to 384 and
what `tests/stackprobe` was written for.

#### The switch is a constant and a rebuild now

`SCH_STACK` and `MAX_TASKS` used to be unchangeable in practice: `sch_switch`
turned a slot index into a slice base with a byte swap that only works at
256, and `apps/taskmgr`'s bss was a hand-chained map of literal offsets. Both
are gone — the base is looked up in `sch_stkbase` (§8.6), every size must be
a multiple of 128 and even, and the Task Manager derives every offset — so a
class change is one line of `SCH_PARTITION` and a rebuild. `MAX_TASKS` is
mirrored in `apps/os88api.inc` at the larger of the two kernels' values (14),
because a package sizes its snapshot buffer from it and over-allocating is
the safe direction; `SS_TSTATE` is `MAX_TASKS` bytes.

### Disk buffers

Three buffers, `DSK_WIN_BYTES` = **3,328** bytes, the FIRST thing in
`.lowbss` (`kernel/dskwin.inc`, SPEC.md §2.1.2), written by int 13h through
ES:BX and read only through `dsk_get_dir` / `dsk_get_icon`, which stage one
entry at a time back into the kernel segment:

- `dsk_secbuf`, 512 B — one sector of scratch. First, because it is the int
  13h target and takes the rung's 512-aligned base.
- `disk_dir`, 768 B — the mount-time listing, `DSK_NENT` = 32 synthesized
  entries at `DSK_DE_STRIDE` = 24 bytes. The 32-entry cap is what sizes it.
- `disk_icons`, 2,048 B — one harvested 64-byte icon per listed entry (on
  `kern_small` a POOL of `DSK_ICO_N` bodies plus a one-byte index per entry,
  §25.8).

They sit immediately above the FAT window because both come alive at the
same instant — the first mount — and are untouched before it, which makes the
two together one 7,936-byte region (7,680 readable) that is dead for the
whole of `kmain`. That is what `.ovlw` is loaded into (below). Since SPEC.md
§20.9 this is exactly what the Task Manager's `Disk bufs` row reports, and
guard 7 recomputes it from the three buffers independently rather than from
`DSK_WIN_BYTES`, so a fourth buffer or a resized one fails the build.

### FAT window — `DSK_FAT_SECS` × 512

The whole FAT on any floppy, a sliding window on a hard disk (SPEC.md §18.8),
with `dsk_next_clus` its single reader and `dskw_setfat` its single writer,
both through ES only. `DSK_FAT_SECS` = 9 is not a buffer with slack, it is an
**acceptance threshold**: mount rule 10 (§18.2) refuses a volume whose
declared FAT is larger before a byte is read, and 9 is exactly the largest
any geometry this OS boots or builds declares (1.44MB = 9, 1.2MB = 7, 720KB
= 3, 360KB = 2). `kern_small` sets it to 2 and mounts nothing above a 360KB
floppy; `tools/os88disk.py --fatcap` keeps every geometry available to it
anyway by formatting with larger clusters.

Since §18.8.3 `FAT_SEG` is not a fallback but a **pin**: one volume holds it,
recorded like any claim, and the other volumes' windows are purgeable heap
claims (`MEM_P_FATW`, §18.8.4) that `mem_fatw_dirty` refuses to shed only
while FAT edits are in them. A desktop with A: mounted holds no heap FAT
claim at all.

### What is reserved on every machine, used or not

`DRV_BLOB_SZ` = 34 bytes of `.bss` plus `CFG_FBUF` for the file's record is
the hard-disk driver's settings, carried inside `SYSTEM.CFG` (SPEC.md §51.9)
rather than in a file of its own. A machine with no hard disk reserves them
anyway; the alternative was a second file, and it was measured and rejected —
a second directory search, a second read, and two full remounts around them,
because every file slot resolves in the *current* volume. `CFG_FBUF` is an
expression (`CFG_REC0 + CFG_NKEY * CFR_HDR + CFG_NB + 2`), so there is no
slack in it to spend.

The directory read-ahead (`MEM_P_DIRW`, §19.2.3) and the per-volume FAT
windows are the two places the kernel claims heap for speed rather than
capacity, and both are purgeable: given back the instant anything else wants
the room, and never asked for unless `mem_avail` reports twice their size.

---

## Two invariants that are easy to break

### Every disk-visible base is 512-byte aligned

int 13h moves one sector per call, which bounds a transfer to 512 bytes —
but **does not stop one from straddling a 64KB physical boundary**. Only
starting on a 512-byte boundary does that, and the DMA controller answers a
straddle with error 09h. The symptom is a **"Disk error" toast on any save
larger than the distance from the buffer to the next 64KB boundary**: Paint's
63KB BMP hits it immediately, a Note Pad text file never does.

Every base in the ladder is an int 13h target — the FAT window, `dsk_secbuf`,
a package image being loaded, a package's file buffer out of the heap — so
the image rounds to a whole 512 bytes rather than to a paragraph, `FAT_PARA`
and `LOW_PARA` are multiples of 32 paragraphs, and `VGABUF_PARA` is too so
that the mono floor under it stays aligned. Guard 6 proves the ladder, guard
2d the decoder rung, and guard 6b the heap: `mem_claim` rounds to whole KB,
so every base it hands out is `HEAP_SEG` + n·64 paragraphs.

### The boot sector has to get out of the way

The BIOS loads `boot/boot.asm` to 0000:7C00 and it is still executing while
the kernel's sectors arrive, and the kernel runs up through 0x7C00 long
before the last of them. So the sector's first act is to copy itself to the
**top of conventional RAM** — `int 12h`, times 64, less its own offset — and
far-jump there, keeping the same offset so every label still resolves at
`org 0x7C00`; its 2,048-byte stack grows down from the new base (SPEC.md
§2.7). That is 2,560 bytes at the ceiling, and only until handoff: `kmain`
sets `SS:SP` in its fourth instruction, after which those bytes are ordinary
heap. The address is computed, so there is no constant to keep in step and
no fixed ceiling on the footprint; guard 5 is a statement about `MIN_RAM_KB`
instead, and the sector itself refuses (`RAM`) any machine where the read
would reach it (`MEMFIT`, §2.7.1).

`KERNEL_SEG` is the only constant `boot/boot.asm` and `kernel/kernel.asm`
share. `apps/os88api.inc` carries a third copy, baked into every package's
far-call targets — **a kernel move means rebuilding every `.o88`** —
and `tests/unit/t_mirror.py` checks every name defined in more than one file.

---

## Where the code goes

Every byte of `.text` and `.cold`, attributed to the file that emitted it.
**Both tables are generated** — `tools/kernsize.py --modules`, blessed in by
`--bless` — by bracketing every `%include` in a temporary copy of
`kernel/kernel.asm` with a bare label in each section and reading the
differences back. Bare labels emit nothing, and the tool proves it rather
than assuming it: it assembles the plain source and the instrumented one and
refuses to report unless the two binaries are byte for byte identical. The
module rows sum to the section totals, and `kernel.asm`'s own row is the
**residual**, so anything the pass failed to attribute lands there in plain
sight. Descriptions are read back out of the table itself, so a row's prose
survives regeneration; a module the table has never seen renders as
**(undescribed)**, which is a prompt to write one.

`.boot2` is a column and not a rung: the loader blob is padded to
`BOOT2_PAD` whatever is in it, so nothing is spent by growing it up to the
cliff on `BOOT2_SIZE`. It is measured per module because `splash.inc` lives
there and nowhere else.

<!-- kernsize:themes -->
| theme | bytes | share |
|---|---:|---:|
| the file system, end to end | 31,771 | 35.3% |
| the window system and its furniture | 24,577 | 27.3% |
| drawing: adapters, primitives, glyphs, icons | 14,852 | 16.5% |
| hardware: drivers, clock, mouse, sound, CPU, XMS | 8,658 | 9.6% |
| the kernel proper: API table, heap, scheduler, events | 8,042 | 8.9% |
| the three built-in kinds | 1,542 | 1.7% |
| the Control Panel | 576 | 0.6% |
| **total** | **90,018** | |
<!-- /kernsize:themes -->

<!-- BEGIN generated table -->
| module | `.text` | `.cold` | code | `.bss` | `.lowbss` | `.boot2` |
|---|---:|---:|---:|---:|---:|---:|
| `wm.inc` — the window manager (§11) | 11,684 | 141 | **11,825** | 1,092 | — | — |
| `files.inc` — the Disk window (§22) | 1,083 | 8,255 | **9,338** | 465 | — | — |
| `vga12.inc` — the VGA planar primitives (§5) | 7,228 | 724 | **7,952** | 165 | 526 | — |
| `disk.inc` — volumes, mount, the FAT read path (§18–19) | 359 | 5,902 | **6,261** | 890 | — | — |
| `fdlg.inc` — the Standard File dialog (§38) | 241 | 4,969 | **5,210** | 168 | — | — |
| `diskw.inc` — the FAT write path (§18.4–18.6) | 179 | 4,740 | **4,919** | 158 | — | — |
| `mouse.inc` — serial mouse and the cursor (§9) | 3,814 | — | **3,814** | 151 | 128 | — |
| `ui.inc` — the UI task and the event ladder (§13) | 3,399 | — | **3,399** | 58 | — | — |
| `memory.inc` — the claim heap (§50) | 207 | 2,795 | **3,002** | 20 | 324 | — |
| `menu.inc` — the menu bar and pull-downs (§12) | 2,794 | 177 | **2,971** | 197 | 84 | — |
| `driver.inc` — loadable drivers + `SYSTEM.CFG` (§51) | 502 | 2,071 | **2,573** | 262 | — | — |
| `assoc.inc` — file type associations (§54) | 480 | 2,010 | **2,490** | 43 | — | — |
| `instance.inc` — instances and the built-in kinds (§29) | 2,088 | 236 | **2,324** | 724 | — | — |
| `font.inc` — the 8×8 glyph renderer (§6) | 2,186 | — | **2,186** | 215 | 784 | — |
| `filecp.inc` — Cut/Copy/Paste (§22.3–22.5) | — | 2,116 | **2,116** | 142 | — | — |
| `apps.inc` — the three built-in kinds (§14) | 282 | 1,260 | **1,542** | 11 | 240 | — |
| `sched.inc` — pre-emptive scheduling (§7–8) | 1,410 | — | **1,410** | 207 | 2,944 | — |
| `softgfx.inc` — the software renderer, §39.5's 1bpp driver (§32) | 1,292 | — | **1,292** | 20 | — | — |
| `vidsel.inc` — which adapters the machine HAS, and switching between them (§39.11) | 1,148 | — | **1,148** | 74 | — | — |
| `loader.inc` — the package loader (§21) | 4 | 1,051 | **1,055** | 42 | — | — |
| `desk.inc` — the desktop and volume zones (§14/§26.1) | 11 | 1,025 | **1,036** | 79 | — | — |
| `snd.inc` — the sound layer (§34) | 1,024 | — | **1,024** | 287 | — | — |
| `fsx.inc` — fullscreen exclusive (§53) | 989 | — | **989** | 9 | — | — |
| `icons.inc` — the icon renderer (§10) | 977 | — | **977** | 281 | — | — |
| `viddet.inc` — adapter detection and geometry (§39) | 867 | — | **867** | — | 696 | 3 |
| `fprog.inc` — the file-operation progress widget (§12.8) | 725 | — | **725** | — | — | — |
| `dock.inc` — the dock strip (§30) | 696 | — | **696** | 35 | — | — |
| `clock.inc` — the clock ladder (§37) | 606 | — | **606** | 59 | — | — |
| `ctrl.inc` — the Control Panel (§31) | 335 | 241 | **576** | 28 | — | — |
| `toast.inc` — the menu bar's transient message (§59) | 433 | — | **433** | 25 | — | — |
| `blank.inc` — the idle screen blanker (§64) | 194 | 236 | **430** | — | — | — |
| `hiber.inc` — hibernate, the resident half of `HIBER.DRV` (§87) | 69 | 324 | **393** | 28 | — | — |
| `mod.inc` — on-demand kernel modules (§2.8) | 56 | 309 | **365** | 112 | — | — |
| `lz.inc` — the LZ decoder for packages, drivers, files and the kernel itself (§20.13) | — | 340 | **340** | — | — | — |
| `xmem.inc` — memory above 1MB (§41.4–41.5) | 242 | — | **242** | 22 | — | — |
| `clip.inc` — the system clipboard (§55) | 179 | — | **179** | 5 | — | — |
| `events.inc` — the event ring (§10) | 159 | — | **159** | 3 | 128 | — |
| `clone.inc` — the disk cloner (§18.99) | 15 | 27 | **42** | — | — | — |
| `cpudet.inc` — CPU tiers and the A20 gate (§41.1–41.3) | 6 | — | **6** | — | — | — |
| `splash.inc` — the boot splash (§15) | — | — | **0** | — | — | 1,826 |
| `dskwin.inc` — the mount-owned window at the bottom of `.lowbss` (§2.1.2) | — | — | **0** | — | 3,328 | — |
| `band.inc` — the 1bpp band composer (§5.9), `BAND=1` | — | — | **0** | — | — | — |
| `vmmouse.inc` — the VMware absolute pointer's resident half (§9.11), `kern_emu` only | — | — | **0** | — | — | — |
| `bootprof.inc` — the boot phase table (§15.5), `BOOTPROF=1` | — | — | **0** | — | — | — |
| `stkdiag.inc` — what an interrupt costs a task stack (STACK-SLOTS-PLAN §10), `STKDIAG=1` | — | — | **0** | — | — | — |
| `moudiag.inc` — what the identify window saw (§9.4.6), `MOUDIAG=1` | — | — | **0** | — | — | — |
| `compress.inc` — the LZB compressor (§20.15), an on-demand module and 0 resident | — | — | **0** | — | — | — |
| `kernel.asm` — API table, entry points, `kmain`, the shims | 3,088 | 18 | **3,106** | — | — | 421 |
| **total** | **51,051** | **38,967** | **90,018** | **6,077** | **9,182** | **2,250** |
<!-- END generated table -->

### Reading it

- **The file system is the largest theme by a wide margin** — bigger than
  the whole window system and its furniture together, and `files.inc` is the
  largest single module. What `files.inc` keeps in `.text` is DATA, not code:
  the window template, the menu sets, every string and the dispatch tables,
  because cold code reads all of it through DS.
- **Nearly half the kernel's code is not in the kernel's segment**, and it is
  the file system, the dialog, associations, drivers, the heap and the
  desktop. That is what bought `KERN_CODE_MAX` its headroom, and it bought
  the footprint nothing.
- **`kernel.asm`'s row is the residual** and almost entirely tables of stubs:
  the API jump table with its X and N stubs, the `cw_*` shims and the
  resident thunks for the cold segment — the price of a package living in its
  own segment (§20.1) and of code living outside the kernel's, paid once
  rather than per call site. The counts are one grep each and not written
  here: `grep -c '^cw_' kernel/kernel.asm` and
  `grep -cE '^[a-z_0-9]+:\s+call\s+COLD_SEG:' kernel/kernel.asm`.
- **`clock.inc` is four clocks and more than half of it boots away**: each
  rung of §37.90's ladder is a different chip, only one can ever run, and the
  probe-and-read halves are in the boot overlay. What stays is the writers,
  the software calendar and the formatters.
- **`splash.inc` reads 0 and that is the truth**: it is `.boot2`'s (§2.9.4)
  and no rung charges for that section. It open-codes its own hline, vline
  and fill because it runs before `vga12.inc` is aboard.

### How to re-measure this

    tools/kernsize.py --modules          # look
    tools/kernsize.py --bless            # ...and write it back into this file

The marker block ends in `section .text` because every `%include` sits at
`.text` scope and every module must switch back before it ends (SPEC.md §4).
The theme grouping is a judgement and lives in `THEMES` in the tool; a module
in no theme stops the report and names itself. For per-routine detail there
is no tool: take each label's address from a `-l` listing and attribute by
address range, not by the listing's `<1>` include markers, because macro
expansions are marked at include depth too.

---

## Moving data out of the segment, and where that stops

`KERN_CODE_MAX` counts `.text` + `.bss`. It does **not** count `.lowbss`,
which lives in `LOW_SEG` and is reached through SS — so a table moved from
one to the other hands its whole size back to the segment guard. SS is
`LOW_SEG` from `kmain` onwards and never changes, so the access is an `ss:`
prefix with nothing to set up: one byte and about two cycles per field. It is
not free on the footprint: `.lowbss` and the image are different rungs, so a
byte that leaves the segment when the low rung is full costs a whole step
until the image falls far enough to drop one — and the low rung reads
478/512 accrued as blessed.

> **THE ESCAPE VALVE IS CLOSED, and the next table to try it pays 512.**
> `.lowbss` has **34 bytes left in its rung** (478/512 accrued, 93%). Every
> migration in the list below was taken while there was room: `vga12.inc`'s
> own comment beside `gfx_pairtab0/1` says *"the low rung had 1,258 bytes
> standing"*, and `viddet.inc:377` explains `vid_rowtab` as costing *"one
> 512-byte rung of `KERN_BUDGET` and nothing of the segment"*. Neither
> sentence is true of the next one: 35 bytes into `.lowbss` bills a whole
> step immediately. The move is still the right shape when the segment is
> what binds — it just is not the cheap trick those comments describe any
> more, so price it as 512 and say so, rather than re-deriving the trick and
> being surprised by the bill.

What decides a migration is **how many places dereference the pointer**, not
size. The objects that made the trip are the `.lowbss` column of the table
above — the glyph table (`font.inc`, 784), the claim map (`memory.inc`, 324),
the row table (`viddet.inc`, 696), the pair tables (`vga12.inc`, 526), the
event ring, the menu bar and the built-in state pools — beside the stacks and
disk buffers that were `.lowbss` by design. Three candidates on size alone
did not go, and the reasons stop them being re-proposed:

- **`fm_pool` (80 B) is a net loss.** `[fm_vp]` points into it and the Disk
  window dereferences that pointer over a hundred times; the prefixes cost
  more than the table is worth. Bytes-per-dereference is the metric.
- **`inst_tab` is entangled, not merely expensive.** `I_NAME` is handed out
  as an ordinary near string pointer — a Disk window's `W_TITLE` aims into
  the record, and the dock, the menu bar and the Task Manager all letter it
  through DS. This was tried: the build was clean and the machine booted to a
  desktop that could not launch anything.
- **`snd_xlat` (256 B) is refused on speed.** Two sites, but they are
  `spk_pcm_run`'s per-sample loop. **And three harnesses now depend on those
  256 bytes staying idle**: `tests/evqfull.py`, `tests/linefast.py` and
  `tools/os88linecost.py` each plant an executable stub in `snd_xlat`
  *because* it is 256 unused `KERNEL_SEG` `.bss` bytes at a fixed symbol. So
  the largest single `.bss` item in the kernel is held in place by the test
  rig as well as by the mixer, and anybody who reclaims it has three rigs to
  re-home first. That is a reason that could be removed, and writing it down
  is not the same as endorsing it.

**`font_glyphs` needed the ABI amended, and was worth it**: `OSAPI_FONT_GLYPHS`
answers `DX:SI` now, a recorded one-time amendment to a shipped slot (§20.8
rule 4), because exactly one package reads it. The five dereferences are the
glyph-row loops, so it has a measurable run-time cost — 16–32 clocks a glyph,
0.34–0.67% of a ~4,770-clock cell on the 8088 — and no cheaper encoding
exists: `[bp]` would default to SS, but 8086 addressing has no `mod=00` form
for BP, so it is the same extra byte and a worse effective address.

**The trap to expect**: a field-offset regex finds `[di+I_STATE]`; it does not
find `add di, I_NAME` followed by a bare `[di]`, nor a `rep stosb` whose ES
was set with `push ds / pop es`. Both assemble and both write the wrong
segment. Every migration has to be checked for three shapes — field accesses,
bare dereferences of an advanced pointer, and string operations whose segment
came from DS.

**A diagnostic buffer belongs in `.lowbss` on the first day.** The disk trace
ring (`dsk_dbg_trc`, §18.94.4) is 1,600 bytes of zeros written at two sites
and read by nobody in the kernel; in `.text` it was 1,600 bytes of
`KERN_CODE_MAX` in a build no user runs, and the failure arrived as `kernel
image + bss overflows KERN_CODE_MAX` on an unrelated UI change. The footprint
guard skips knob builds; the segment guard has no way out but leaving the
segment.

---

## The boot overlay: code that costs no memory at all

Some of the kernel runs exactly once, from `kmain`, and is then unreachable.
It goes in the overlay, which is the only code in this file that costs
**nothing** — not RAM, not budget, not the segment — and since SPEC.md §2.5.3
it is two sections, because the two halves die at different times:

| | bytes | lives until | lands on | reached by |
|---|---:|---|---|---|
| `.ovl` | 1,417 | `spl_finish` | stage 2's blob, at `OVL_AT` = 2,624 of `BOOT2_PAD` = 4,096 | `[spl_fseg]`, the pair of §2.9.5.1 |
| `.ovlw` | 5,037 | **the first mount** | `FAT_SEG`, off the kernel's own contiguous read, spilling through the mount-owned buffers (7,936 bytes, 7,680 readable — SPEC.md §2.1.2) | `call FAT_SEG:`, a constant |

The blob is 8 sectors; whatever the loader is not using below `OVL_AT` the
overlay can have for the cost of moving that one line, and the two assertions
at the foot of `kernel.asm` say which half ran out. `.ovlw`'s bound is the
window: docs/plans/KERN-SMALL-CUT-PLAN.md §7 is why `kern_small`'s two-sector
FAT window could not be taken until the clock ladder was gated out of that
half. `tools/os88ovlchk.py` is the gate for both, and rule 2e is the one that
bit: nothing in `.ovlw` may be called after the mount, checked against
`kmain`'s own order — `xm_boot_x` was, the machine far-called into a FAT
table, and `sch_switch`'s canary caught the wreckage.

**It is one assembly, and that is the whole trick.** A separate build would
not know where `cpu_tier` or the `snd_*` words live; because the overlay is a
section of the same source, every kernel symbol resolves and — because it
runs with **DS = `KERNEL_SEG`** — every data reference executes as it did in
`.text`. The contract is `CS = the blob's segment` (or `FAT_SEG`), `DS =
KERNEL_SEG`, `SS = LOW_SEG`, with one sharp edge: **the overlay may not reach
its own labels through DS** — its strings are `cs:`-addressed and NASM will
not warn about one that is not. `clock.inc`'s split was derived rather than
eyeballed: everything reachable from `clk_init`, minus everything reachable
from the six symbols called from outside the module, is movable by
construction; five helpers both halves use cannot move.

`.ovl` is declared `start=OVL_AT vstart=OVL_AT` — `start=` is the file
offset, so NASM emits the gaps as zeros and stage 1's single read lands it;
`vstart=` makes its labels offsets from the blob's base, so one segment holds
both `.boot2` and `.ovl`. Guard 4b refuses an EMPTY overlay, because every
`OVLCALL` in `kmain` would then land on padding.

---

## Cold code: resident, but not in the segment

The overlay works because its code is transient. Most kernel code is not:
the file manager has to be there whenever a window is clicked. `.cold` is a
second code segment, resident for the whole session, that `KERN_CODE_MAX`
cannot see — `COLD_PARA` is its size rounded to 512 with no slack, nothing is
copied and nothing is reserved. It shares the overlay's contract exactly:
**CS = `COLD_SEG`, DS = `KERNEL_SEG`**, so every data reference in a cold
module is unchanged because the data did not move. Its tenants and their
sizes are the `.cold` column of the generated table and are stated nowhere
else.

Modules went cold in sets that call each other, so a call inside the set
stays near and only the ones that leave it pay a shim; growing the set makes
what is already in it cheaper. The second round was steered by the segment
reading 65,065 of 65,536 and asked "what is cold in cadence" — a double-click,
a mount bounded by a floppy at ~24 ms a sector, a boot, a claim, a drive-zone
click — and the deliberate non-candidates matter as much: the drawing
primitives and both ISRs on cadence, `splash.inc` and `viddet.inc`
structurally, because they must be inside the image's opening sectors.

Four rules hold it up, `tools/os88ovlchk.py` refuses the build on each, and
every one describes something that assembles cleanly and runs wrong:

- **Data stays in `.text`.** DS is still `KERNEL_SEG`, so a string or table
  that moved with its code is read at the wrong segment. A module with data
  islands toggles sections around each.
- **Nothing may take a kernel segment from `CS`.** `push cs`/`pop es` and
  `[cs:x]` are the two spellings; a cold module's CS is `COLD_SEG`.
- **A `.text` table of cold pointers is fine only if cold code alone
  dispatches through it.** A table `.text` dispatches through must name the
  resident thunk, not the `_x` body.
- **A macro argument is a call site.** `OSAPI_SLOT dskw_dfree` near-calls its
  argument from inside the macro, and the checker had to be taught the cell
  macros to see it.

The wiring is `cw_*` shims outward and resident thunks inward, the thunk
keeping the public name and the body taking an `_x` suffix, so no caller
outside a cold module changed; SPEC.md §2.6.1 is where a far entry ends in
`retf` and the thunk in the middle is deleted. `wm_pkgcall` sets DS from
`W_SEG`, the wrong contract for cold code, which is why window callbacks go
through thunks rather than through the window record. A far call costs two
extra bytes of stack per crossing; the cold modules are UI-task code, so that
lands on task 0's 512 and not on a worker's slice.

---

## The levers that move both guards

`.cold` and the overlay relieve the segment. Nothing relieves the footprint
except doing less, and there are three shapes of doing less, each with a
worked example:

**A PACKAGE on the system disk** — the Task Manager (SPEC.md §28). It read
`sch_cycles`, `sch_tasks`, `inst_tab`, `mem_tab` and seven assembly-time
constants of this ladder directly, because it was kernel code and could;
§20.9's four snapshot cells are the API that replaced that, added FIRST with
the module still built in, so the API was proved sufficient while a debugger
could still reach the code. Net: −5,380 on `KERN_CODE_MAX`, −5,120 on
`KERN_BUDGET`, and the memory it uses is spent only while the window is open.
The cost is that opening it needs a working disk and ~8KB of heap on the
machine where something is already wrong, and the failure is a notice naming
the reason rather than a silent one (§47 rule 3).

**An ON-DEMAND MODULE** (§2.8, `kernel/mod.inc`) — kernel code cut out of
`KERNEL.SYS` into a file, read into a heap claim when the feature is asked
for and freed when it is done: `CTRL.DRV`, `FORMAT.DRV`, `CLONE.DRV`,
`HIBER.DRV` (which carries the compressor too, §20.15.3), and on `kern_small`
`FILECP.DRV` and `FDLG.DRV` too. What stays resident is the menu item, the greying predicate and the
thunks (`MOD_NENT` = 7 far-pointer slots per module). A feature qualifies
when the system disk is already required to use it, or can be required
without interrupting what the user was doing. **What the mechanism refuses**
is in docs/plans/completed/KERN-SMALL-MODULE-SPLIT.md: `mod_need`'s own
transitive cone (assoc had to be GATED, not moved), and a layer with more
entry points than `MOD_NENT` (diskw). A module's DATA must stay in `.text`,
because `DS = KERNEL_SEG` is the whole of how it reaches anything.

**An OVERLAY DRIVER** — the screen saver (§79, `SAVER.DRV`, `DRVC_OVL` like
`XMEM.DRV` and `HDDTOOL.DRV`): read into a heap claim when the idle period
runs out. It is the right lever where the thing is mostly DATA — a sine
table, mode state, vertex lists, strings and shared `os88ui.inc` control code
— because an overlay owns a segment and every byte of it is on the floppy,
where a module would have charged all of it to the segment nobody can raise.
The kernel keeps 113 bytes of `.text`, 6 of `.bss` and 250 of `.cold`.

So the question to ask of a candidate is not *"is this code that need not be
resident"* but **"how much of it is data, and can it be reached through a
segment of its own?"** — and the close negotiation (§75) is the same lesson
one level down: built in the kernel it was 1,291 bytes across three rungs;
split so that the kernel keeps only what needs kernel state (`W_ONCLOSE`,
`OSAPI_WM_CLOSE`, the side table — 222 bytes) and the alert lives in
`apps/os88ui.inc` behind `%define OS88UI_ALERT`, it costs each including
package 607 bytes of its own image and the kernel no rung at all.

---

## "My buffer has to be in bss because a driver points at it" — measured

Two packages have grown their own segment to feed a driver, and the second
(`apps/ftpd`'s second staging buffer, SPEC.md §77.41) is an *optimisation*
buffer. This is the price list for lifting the rule; **it is not landed.**

**The rule is four instructions in one macro.** `OSAPI_XSTUB` is `push ds /
push es / push ds / pop es`, so ES = the caller's DS on entry to
`drv_pkg_call_x`, which never reads or writes ES itself — the segment flows
straight through to the driver. It is a *default*, not a guard; there is no
memory protection on an 8086. And only one of a package's two consumers has
the restriction: the file cells (`OSAPI_FILE_APPEND` / `WRITE` / `READ_AT`)
are N stubs that restore the caller's ES explicitly and take any segment
already; `OSAPI_DRV_CALL` is an X stub and overwrites it.

**Lifting it costs one more table slot pointing at `drv_pkg_call_x`** through
an ordinary `OSAPI_SLOT`, which does not touch ES: 8 bytes of API table, no
footprint change measured, three deliberate gates firing (the table-length
assertion, `api-abi`'s alias rule, `checkdocs` wanting a §), and zero blast
radius because it is a new cell. Drivers already cope — `ETHER.DRV` banks
`[eth_useg]` at `eth_pkg`'s door and holds nothing across calls.

**Why the default should stay.** ES on entry to every package callback is
`KERNEL_SEG` (CLAUDE.md); a package that opts into the segment-taking cell
and forgets to set ES points a receiving driver verb at the kernel, silently.
So the shape is an added cell, never a changed one, and the claim behind it
must be PINNED (§66's default) or a compaction moves it under the driver.
What it would buy is `ftpd`'s second stage as a refusable heap claim; what it
would not is Tracker's ring, which is already granted out of the driver's own
staging pool (`OSAPI_SND_STREAM` verb 7, §34.6) — the grant model is the other
answer for anything a single driver consumes.

---

## History

| change | budget | footprint |
|---|---:|---:|
| before any of this (v1.0.20260728) | — | ~107 KB |
| `.fartext` retired, the ladder derived, buffers trimmed, kernel at 0x0060 | 64 KB | 63.5 KB |
| `.lowbss` migration + 256-byte task stacks | 79 KB | 76.5 KB |
| the Task Manager becomes a package on the system disk (§28) | 79 KB | 69 KB |
| the five file modules into `.cold` (§2.6); the budget meets the fixed boot sector and the sector moves to the top of RAM (§2.7) | 80.5 KB | 78 KB |
| the kernels split, `kern_small` for the 128KB machine (docs/history/KERN-SPLIT-PLAN.md) | 92 KB each, then apart | — |
| `KERN_BUDGET` moves 1–35 | 64 → 119.5 KB (big), → 105 KB (small) | — |
| kernel size passes 2 and 3 (docs/plans/completed/HANDOFF-KERNEL-SIZE-P2.md, -P4.md) | unchanged | −5,632 (big) |
| rule 3 makes big's budget DERIVED; `MIN_RAM_KB` per configuration | big 126.5 KB | — |
| `kern_small` stops supporting VGA, on-demand `FILECP.DRV`/`FDLG.DRV`, the two-sector FAT window (docs/plans/completed/KERN-SMALL-CUT-BUILT.md) | small 105 KB | small 76.5 KB |

**The 35-move ledger of `KERN_BUDGET` lives in `kernel/kernel.asm`**, in the
constant's own comment, per move: what was asked for, what was granted, what
spent the previous step and which kernel moved. It closes at move 35 by
construction, because big's figure is derived now. Every move was a rung that
filled, and the moves worth knowing the shape of are the merges — 26, 27, 28,
30, 31, 33 and 34 — where two branches were each measured against the guard
alone and the union crossed it. docs/plans/completed/MEMORY-PLAN.md is the
narrative of how the ladder got here and what was rejected on the way.

### Size pass 2 gave five rungs back

Two size passes handed bytes back rather than spending them (the ledger has
no mechanism for that direction), and the pair of lessons is why they are
still here. Pass 2 uncrossed five rungs on big, and one was re-crossed by an
unrelated 228-byte branch against the 174 bytes the pass had left in that
rung before the merge landed — the amortised price of a byte, with real
numbers on it. Pass 3's bless then printed a `.lowbss` crossing for
`kern_small` that the pass had nothing to do with: 384 bytes the
worker-stack-slots merge had added without blessing. A merge that moves a
rung and does not bless is a rung charged to whoever blesses next. After the
two passes the SEGMENT binds on `kern_big` for the first time, which is where
"Which guard binds" above still stands.
