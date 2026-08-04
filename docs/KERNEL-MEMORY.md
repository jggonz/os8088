# The kernel's memory

**This document is maintained.** It is the standing account of what the
os8088 kernel spends RAM on and why, and it is expected to be updated in the
same commit as any change that moves a number in it. SPEC.md §2 is the
binding contract for the addresses; this is the reasoning behind them.

---

## The rule

**The kernel is ONE contiguous span starting at linear 0x00600, and that
includes its buffers.** The span is `KERN_BUDGET` bytes — 75KB today, and
64KB for as long as that was affordable (see below); it currently runs
0x00600 through 0x11DFF.

Not the code and then some scratch elsewhere: *everything*. Code, read-only
data, `.bss`, the FAT snapshot, the directory and icon caches, the sector
buffer and every task stack are one contiguous span starting at
`KERNEL_SEG`. Guard 1 in `kernel/kernel.asm` measures that whole span
against `KERN_BUDGET` and fails the build if it is over.

**There is exactly one deliberate exception**, and it is a heap claim rather
than a reservation: the menu save-under (SPEC.md §12.4), 20KB that exists
only while a pull-down is on screen and is handed back the moment it closes.
It is not part of the kernel's footprint because on any given tick it usually
is not there — `menu_drop` claims it on the way in and releases it on the way
out, *before* the selected item runs, so a menu that launches something has
already given the 20KB back by the time the launch asks for memory. It was
claimed once at `menu_init` and held for the whole session until that was
noticed, which on a 128KB machine is more than a third of the heap held
permanently against nothing.

**The size in RAM is the actual size, not a budget.** There is no growth
room anywhere in the ladder. Each rung is the measured size of what it
holds, rounded up only as far as alignment demands, so the heap starts where
*this build's* kernel happens to end and moves when the kernel does. A fixed
ceiling with slack under it is memory that nothing can ever use — which is
what the **package pool** had become: 60KB reserved above the kernel whether
or not a package was loaded. A package's region is an ordinary heap claim now
(SPEC.md §20.1), taken from the top of the heap downward while data claims
grow up from the bottom.

**If the kernel needs to grow past its budget, that is a conversation, not a
build fix.** Raise `KERN_BUDGET` only after explaining to whoever asked for
the feature what it costs and getting an explicit yes. The guard's error
message points here for that reason.

**That conversation has happened three times.** `KERN_BUDGET` was 65,536 —
the first 64KB above the BIOS data area, which is where the "one region" rule
came from.

1. → **71,680 (70KB)**, to buy the SPEC.md §41 extended-memory store and the
   two API surfaces (`wm_geom`, `wm_about_set`) that came with it from the
   other fork.
2. → **72,704 (71KB)**, for the sound driver's Control Panel page — the
   source selection and its Test button (SPEC.md §31.7).
3. → **76,800 (75KB)**, for SPEC.md §51.5's keyed `SYSTEM.CFG`: a settings
   file where nothing is positional costs a key table, a bounded record
   walker and a writer, and that is ~260 bytes the previous ceiling had no
   room for. Granted **in advance of further work**, with an optimisation
   pass to follow that should hand some of it back — so unlike the first two,
   the slack under this one is deliberate and temporary rather than an
   invitation.

`BOOT_RELOC` moved with each of them (0x0940 → 0x0AA0 → 0x0B80 → **0x0C00**),
because guard 5 pins the kernel's landing zone below the relocated boot
sector's stack; it is mirrored in `boot/boot.asm` and the two must change
together.

What the first raise cost **at the commit that granted it** — a snapshot, not a running total; the live figures
are the table below:

| | before | after |
|---|---:|---:|
| image (`.text` + `.bss`) | 50,176 B | 52,736 B |
| whole kernel span | 65,024 B | 67,584 B |
| spare under the budget | 512 B | 4,096 B |

The 64KB *segment* limit is untouched and cannot be raised at all: `.text` +
`.bss` are addressed through one segment with 16-bit offsets, so guard 2 caps
them at 65,536 whatever the budget says. At today's 54,272 there is still
11,264 B of segment left, so the budget is what binds, which is the intended
order — a budget is a decision and a segment is physics.

Raising the budget also moved the **relocated boot sector**: `BOOT_RELOC`
went 0x0940 → 0x0AA0 (linear 0x11000 → 0x12600), because guard 5 keeps the
growing kernel clear of the sector that is still executing while it lands.
The constant is mirrored in `boot/boot.asm` and the two must move together.

---

## Where it goes

Measured on the shipped build. `make` prints the image size; the rest come
out of the same constants the guards use.

| region | size | what it is |
|---|---:|---|
| image (`.text` + `.bss`) | 57,344 B | all kernel code, its read-only data, and its scratch |
| task stacks | 6,656 B | 11 background slots + task 0's |
| disk buffers | 3,584 B | directory cache, icon cache, sector scratch |
| FAT snapshot | 4,608 B | the mounted volume's FAT, resident |
| **total** | **72,192 B** | of a 72,704-byte budget — 512 B spare |

Everything above that is the claim heap, up to whatever int 12h reports. The
arithmetic is exact and worth writing down, because every RAM figure in this
project falls out of it:

> **heap KB = what int 12h reports − 73**

`KERN_END` is 4,672 paragraphs = 74,752 bytes = 73KB, and the heap starts
there. Checked against a live machine: QEMU with `-m 1M` reports **639KB**
and the Task Manager shows **566KB** of heap. 639 − 73 = 566. Re-derive it
after any budget change — this constant has already moved once, when
`KERN_BUDGET` went to 76,800. It used to
be *nothing* on a small machine: the package pool's own top sat above 128KB,
so a 128KB machine had no heap and could load no package at all.

## What it actually takes to run

Measured, not derived — by clamping what `mem_init` believes int 12h said and
booting each size under QEMU. (The clamp is a throwaway; it is not in the
tree.) Three different questions, three different answers:

| RAM | heap | what happens |
|---|---|---|
| < 80KB | — | **cannot boot.** Nothing to do with the heap: `boot/boot.asm` relocates itself to linear 80,896 and reads the kernel from there, so the machine has to have that byte. Guard 5 checks the kernel clears its stack. |
| 80KB | 7KB | boots, full desktop, browses both floppies, **loads a package** (`hello`) |
| 96KB | 23KB | Note Pad runs. Paint loads and puts up its "Not enough memory" notice — the designed tier, not a crash |
| 176KB | 103KB | **Paint runs live**, full 448×280 canvas. 160KB still gets the notice |
| 640KB | 566KB | everything, including the 150KB back buffer |

So the honest floor is **80KB to boot and load something**, and **~176KB for
every shipped app at full function**. The often-quoted "128KB" sits between
those: it runs the OS and most of the packages, and Paint declines.

Two things this table is not. It is not a promise about *speed* — these were
measured under QEMU, which does not model 8086 timing at all (SPEC.md §5.4).
And the sizes below 640KB were simulated by clamping the heap, so they
exercise every "the heap said no" path faithfully but do not exercise a BIOS
that reports a small number, which only real hardware and 86Box can do.

The Task Manager's memory view shows this same breakdown live, one indented
row per buffer under **System**, and paints the buffer part of the kernel
span in its own texture on the RAM bar. Every figure there is an
assembly-time constant, so the twice-a-second refresh does no arithmetic to
produce them — and it does not draw them either unless they moved: every
element on the page reduces what it is drawn from to one word and compares
that against what it last drew, so a desktop sitting still costs a few string
builds and two table hashes rather than two map interiors and a dozen rows.

The page is **one map, captioned on the line directly above it** — the
second used to magnify the package pool, and there is no pool:

```
RAM 267/639K [] HEAP 181/570K       <- the map's caption, both figures
[==============================]    <- every byte the machine has
XMS   0/64448K                      <- and what it has no address for
[==============================]
```

The top line is **one string**, swatch and all — the swatch is drawn into
the two spaces between the pairs — because that makes the whole line one
comparison when the refresh asks whether it needs drawing at all.

The heap has no map of its own and never will: a claim is drawn in the
conventional map at its real address, in among the kernel and everything
else, so its figures belong to that map's caption and share the top line with
RAM. **Package regions are claims too** (SPEC.md §20.1) and are drawn there
in their per-slot patterns — at the far right, because they are claimed from
the top of the heap downward while data grows up from the bottom, so the two
kinds separate visibly.

Each row's legend square is the texture its memory is drawn in on the maps,
so the two can be read against each other:

| square | band | where |
|---|---|---|
| 50% gray | the kernel's own span | `System` |
| horizontal bars | its buffers | `Stacks`, `Disk bufs`, `FAT snap` |
| framed light block | a live heap claim | beside the `HEAP` figures |
| per-slot pattern | one package's region | each package row |

A row only gets a square when the texture is its own. `Code+data` has none —
it is drawn in the same gray as `System`, and a square that repeats one above
it is not a legend. `Builtins` has none because a built-in owns no band at
all: its code is already inside `Code+data`, and its memory is heap claims
billed to its own row. And the claim texture is keyed beside the `HEAP`
figures rather than in the list, because it belongs to the HEAP *column* and
not to any one row.

Every square goes through one routine over an 8-byte pattern, including the
two the maps themselves draw with `gfx_fill_gray` and a plain black fill:
`tm_pat_gray` is byte for byte what `gfx_fill_gray` lays down, so a square is
the same pixels as its band and not merely a similar grey. A set bit is
white (SPEC.md §5), which is why solid black is a pattern of eight zeroes.

A claim is the only band drawn with a **frame**, because it is the only one
that comes and goes while you watch, several sit shoulder to shoulder, and
the scale is coarse enough (4KB per pixel on a 640KB machine) that a 3KB
Disk-window cache is one column. The frame is what makes its edges readable;
the interior texture is light so it does not swallow it.

---

## Each region in detail

### The image — `.text` + `.bss`, 54,272 B

One flat binary at `KERNEL_SEG:0000`, assembled `-f bin` with no linker.
`.bss` follows `.text` immediately and is uninitialised by definition, so it
costs nothing on the floppy and everything in RAM.

That figure is `.text` + `.bss` **rounded up to a whole 512 bytes** (see the
alignment invariant below), so it is the only rung with any slack in it, and
the slack is a rounding remainder rather than a reservation — 291 bytes as
this is written, against 53,981 unrounded. Measure the unrounded pair by
appending
`section .text` / `times KBSS_SIZE db 0` to `kernel/kernel.asm`, assembling,
and taking the file size; revert afterwards. `make`'s own `kernel: n bytes`
line is `.text` alone.

**All of the kernel's code is here.** There used to be a `.fartext`
section — cold modules (the Control Panel, the Task Manager, one sound
routine) assembled at `vstart=0`, shipped at the tail of the image and
copied down to their own segment below the kernel at boot, so that their
5,455 bytes did not count against the kernel's 64KB window. It was retired
when the budget above replaced that window as the thing being steered by,
and the arithmetic is why: the mechanism needed a **10,752-byte
reservation** in low memory to hold a 5,455-byte blob, so the moment the
whole footprint became the number that mattered it was costing 5,297 bytes
to save nothing. Merging it back also deleted the shims — three `FARSHIM`
stubs, twenty-seven `FARK` wrappers, `far_init`, and two bytes on every one
of the 91 `KCALL` sites — which is why the image grew by less than the blob
it absorbed.

The consequence for anyone adding code: **there is no longer anywhere to
put a module that is "too cold to be worth the space".** Cold code is
ordinary code. If the image needs to shrink, it shrinks by doing less or by
doing it smaller, not by moving it somewhere the accounting cannot see.

### Task stacks — 6,656 B

Eleven background slots of `SCH_STACK` = 512 bytes (`MAX_TASKS-1`, since
task 0 owns no slice of `sch_stacks`), plus `STK0_SIZE` = 1,024 bytes for
task 0 itself. They live in `.lowbss`, addressed through SS, which is why
SS ≠ DS everywhere in the kernel (SPEC.md §1).

**Both numbers are measured.** A 0xCC fill over the whole stack region,
then the machine driven as hard as it goes — Clock, two Bounces, About, the
Control Panel on both its pages, the Task Manager with a window drag, a Disk
window, the Fractal with its worker task, and Paint saving a GIF into a
folder it created from the file dialog — leaves its deepest mark at **246
bytes** on task 0's stack and **150** on a background task's. ISR frames are
included in that: the tick and mouse handlers run on whichever stack they
interrupt. So 512 is 3.4× the worst observed background depth and 1,024 is
4× task 0's.

Task 0 gets the larger share because it is the UI task: every window
callback, every menu track, every file-dialog interaction and every built-in
app runs on it.

**`STK0_SIZE` is a constant, and that is the whole point.** It used to be
"whatever is left between the top of `.lowbss` and the kernel segment" —
which meant task 0's stack silently absorbed every byte saved anywhere below
it. Two rounds of shrinking the buffers under that rule freed exactly
nothing: the FAT snapshot gave up 7KB and task 0's stack grew by 7KB. Naming
the number is what turned those savings into memory.

Re-run the fill probe before lowering either number. Guard 3 only proves
`STK0_SIZE` is big enough to be a stack at all; nothing catches a task that
outgrows its own 512-byte slice.

### Disk buffers — 3,584 B

Three buffers in `.lowbss`, written by int 13h through ES:BX and read only
through `dsk_get_dir` / `dsk_get_icon`, which stage one entry at a time back
into the kernel segment so no drawing or parsing code has to learn about
segments:

- `disk_dir`, 1,024 B — the mount-time directory listing, 32 synthesized
  32-byte entries. The 32-entry cap is what sizes it.
- `disk_icons`, 2,048 B — one harvested 64-byte icon per listed entry.
- `dsk_secbuf`, 512 B — one sector of scratch: the directory sector being
  read-modify-written on a write, and the zero-padded final sector of a file.

### FAT snapshot — 4,608 B

`DSK_FAT_SECS` × 512, re-read from the volume on **every** mount, with
`dsk_next_clus` its single reader and `dskw_setfat` its single writer, both
through ES only.

`DSK_FAT_SECS` = 9 is not a buffer with slack — it is an **acceptance
threshold**. Mount rule 10 (SPEC.md §18.2) refuses a volume whose declared
FAT is larger before a byte of it is read, so the number is exactly the
largest FAT any geometry this OS boots or builds declares: 1.44MB = 9,
1.2MB = 7, 720KB = 3, 360KB = 2.

It also decides, on its own, that **FAT16 is unreachable**: a FAT is only
FAT16 with ≥ 4,085 clusters, which needs ≥ 16 FAT sectors, so rule 10 turns
the whole class away. The FAT16 halves of `dsk_next_clus` and `dskw_setfat`
remain in the tree and nothing can call them.

---

## Two invariants that are easy to break

### Every disk-visible base is 512-byte aligned

int 13h moves one sector per call, which bounds a transfer to 512 bytes —
but **does not stop one from straddling a 64KB physical boundary**. Only
starting on a 512-byte boundary does that, and the DMA controller answers a
straddle with error 09h.

Every base in this ladder is an int 13h target: the FAT snapshot, the disk
buffers, a package image being loaded, and a package's file buffer out of
the heap. So the image rounds up to a whole **512 bytes** rather than to a
paragraph, and because `FAT_PARA` (288) and `LOW_PARA` are both multiples of
32 paragraphs, aligning that one rung aligns the whole ladder. Guard 6 proves
it — and guard 6b proves the claim heap keeps it, since a package image is
read by int 13h into a **claim** now: `mem_claim` rounds to whole KB, so every
base it hands out is `HEAP_SEG` + n·64 paragraphs.

This held by luck until the ladder became derived: every base used to be a
round constant like `0x0300` or `0x2A00`, and nothing said why that
mattered. The symptom when it broke was a **"Disk error" toast on any save
larger than the distance from the buffer to the next 64KB boundary** —
Paint's 63KB BMP hit it immediately, a Note Pad text file never would.

### The boot sector has to get out of the way

The BIOS loads `boot/boot.asm` to 0000:7C00 and it is *still executing*
while the kernel's sectors arrive — it far-calls the splash at
`KERNEL_SEG:0008` after every one. With the kernel landing at 0x00600 and
running up to 64KB, it covers 0x7C00 long before the last sector.

So the sector's first act is to copy itself to `BOOT_RELOC:7C00` (linear
0x11000, above anything the kernel can reach) and far-jump there. **The copy
keeps the same offset**, so every label in the file still resolves at
`org 0x7C00` and only the segment registers change; its stack rides along at
the same offset and grows down from 0x11000. Guard 5 proves the kernel ends
clear of that stack.

`BOOT_RELOC` and `KERNEL_SEG` are mirrored in `boot/boot.asm`, which is
assembled separately. `apps/os88api.inc` carries a third copy of
`KERNEL_SEG`, because it is baked into every package's far-call targets —
**a kernel move means rebuilding every `.o88` and both apps floppies**, or a
package calls into empty memory.

---

## History

| change | kernel footprint |
|---|---:|
| before any of this (v1.0.20260728) | ~107 KB |
| low memory sized to measurement, kernel moved to 0x0800 | 75 KB |
| `.fartext` retired, ladder derived, buffers trimmed, kernel at 0x0060 | 63.5 KB |
| budget raised 64 → 70KB for the SPEC.md §41 XMS store | 66 KB |
| budget raised 70 → 71KB for the SPEC.md §51 driver subsystem | 70.5 KB |
| ...and where it stands now | **67.5 KB** |

The last row is the one to re-measure rather than trust: it moves with every
commit that adds code, and it is not the budget — it is what the budget is
being spent on. Above, "Where it goes" carries the same figure to the byte.

`docs/MEMORY-PLAN.md` is the narrative of how it got here, step by step, and
what was rejected along the way. This document is what it looks like now.

---

## The second budget raise: loadable drivers (SPEC.md §51)

70KB → **71KB** (71,680 → 72,704), asked for and granted, and it is the
second time the number has moved.

What it bought, and why it is not simply 1KB spent:

| | |
|---|---:|
| the driver table, loader, settings file and boot path (`driver.inc`) | ~1.4 KB |
| the Control Panel's Drivers and Sound pages, and `cp_flush` | ~1.1 KB |
| the FM and stream API slots, the tone route, the driver hooks in `snd.inc` | ~0.5 KB |
| **what it makes loadable instead of resident** | **the OPL2 and Sound Blaster tiers** |

The last row is the argument. On the other fork those tiers are kernel code:
`sndfm.inc` and `sndsb.inc` are 3,260 lines, resident on every machine
whether or not a card is in it. Here they are a file on the system disk that
a 128KB machine with no card never reads — and the same machinery will carry
the next driver for nothing.

The heap moved down by the same 1KB it gained. Today's measured figures are
in the table above; the point of this note is the direction, not the
arithmetic, and every raise of the budget comes straight off the heap.

**`BOOT_RELOC` moved with it**, 0x0AA0 → 0x0B80 (linear 0x12600 → 0x13400),
because guard 5 keeps the growing kernel clear of the boot sector that is
still executing while it lands. The constant is mirrored in `boot/boot.asm`
and the two must move together — this is the second time, and both times it
was the raise that forced it.
