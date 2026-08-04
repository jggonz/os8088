# The kernel's memory

**This document is maintained.** It is the standing account of what the
os8088 kernel spends RAM on and why, and it is expected to be updated in the
same commit as any change that moves a number in it. SPEC.md §2 is the
binding contract for the addresses; this is the reasoning behind them.

---

## The rule

**The kernel fits in the first 64KB above the BIOS data area — linear
0x00600 through 0x105FF — and that includes its buffers.**

Not the code and then some scratch elsewhere: *everything*. Code, read-only
data, `.bss`, the FAT snapshot, the directory and icon caches, the sector
buffer and every task stack are one contiguous span starting at
`KERNEL_SEG`. Guard 1 in `kernel/kernel.asm` measures that whole span
against `KERN_BUDGET` and fails the build if it is over.

**There is exactly one deliberate exception**, and it is a heap claim rather
than a reservation: the menu save-under (SPEC.md §12.4), 20KB that exists
only while a pull-down is on screen and is handed back the moment it closes.
It is not part of the kernel's footprint because on any given tick it
usually is not there.

**The size in RAM is the actual size, not a budget.** There is no growth
room anywhere in the ladder. Each rung is the measured size of what it
holds, rounded up only as far as alignment demands, so the package pool
starts where *this build's* kernel happens to end and the heap starts after
the pool. Both move when the kernel does, and that is intended — a fixed
ceiling with slack under it is memory that nothing can ever use.

**If the kernel needs to grow past 64KB, that is a conversation, not a
build fix.** Raise `KERN_BUDGET` only after explaining to whoever asked for
the feature what it costs and getting an explicit yes. The guard's error
message points here for that reason.

---

## Where it goes

Measured on the shipped build. `make` prints the image size; the rest come
out of the same constants the guards use.

| region | size | what it is |
|---|---:|---|
| image (`.text` + `.bss`) | 50,176 B | all kernel code, its read-only data, and its scratch |
| task stacks | 6,656 B | 11 background slots + task 0's |
| disk buffers | 3,584 B | directory cache, icon cache, sector scratch |
| FAT snapshot | 4,608 B | the mounted volume's FAT, resident |
| **total** | **65,024 B** | of a 65,536-byte budget — 512 B spare |

Everything above that is somebody else's: the 60KB package pool, then the
claim heap up to whatever int 12h reports.

The Task Manager's memory view shows this same breakdown live, one indented
row per buffer under **System**, and paints the buffer part of the kernel
span in its own texture on the RAM bar. Every figure there is an
assembly-time constant, so the twice-a-second refresh does no arithmetic to
produce them — and it does not draw them either unless they moved: every
element on the page reduces what it is drawn from to one word and compares
that against what it last drew, so a desktop sitting still costs a few string
builds and two table hashes rather than two map interiors and a dozen rows.

The page is **two maps, each captioned on the line directly above it**:

```
RAM  89/639K [] HEAP  23/514K       <- the conventional map's caption
[==============================]    <- every byte the machine has
PACKAGES   2/ 60K                   <- the pool map's caption
[==============================]    <- the 60KB pool, magnified
```

The top line is **one string**, swatch and all — the swatch is drawn into
the two spaces between the pairs — because that makes the whole line one
comparison when the refresh asks whether it needs drawing at all.

The heap has no map of its own and never will: a claim is drawn in the
*conventional* map at its real address, in among the kernel and the pool, so
its figures belong to that map's caption and share the top line with RAM. On
its own line above the second map — which is where it used to sit — it read
as that map's label, and the second map is the package pool, the one thing on
the page that is emphatically not the heap.

Each row's legend square is the texture its memory is drawn in on the maps,
so the two can be read against each other:

| square | band | where |
|---|---|---|
| 50% gray | the kernel's own span | `System` |
| horizontal bars | its buffers | `Stacks`, `Disk bufs`, `FAT snap` |
| solid black | the package pool | `Packages` |
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

### The image — `.text` + `.bss`, 50,176 B

One flat binary at `KERNEL_SEG:0000`, assembled `-f bin` with no linker.
`.bss` follows `.text` immediately and is uninitialised by definition, so it
costs nothing on the floppy and everything in RAM.

That figure is `.text` + `.bss` **rounded up to a whole 512 bytes** (see the
alignment invariant below), so it is the only rung with any slack in it, and
the slack is a rounding remainder rather than a reservation — 50 bytes as
this is written. Measure the unrounded pair by appending
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
paragraph, and because `FAT_PARA` (288), `LOW_PARA` and `PKG_PARA` (3,840)
are all multiples of 32 paragraphs, aligning that one rung aligns the whole
ladder. Guard 6 proves it.

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
| `.fartext` retired, ladder derived, buffers trimmed, kernel at 0x0060 | **63.5 KB** |

`docs/MEMORY-PLAN.md` is the narrative of how it got here, step by step, and
what was rejected along the way. This document is what it looks like now.
