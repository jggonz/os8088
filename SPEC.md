# os8088 1.0 — GUI specification

This document is the **binding contract** for all kernel modules. Every symbol
name, register contract, constant, and data layout here is pinned. Implement
exactly what is written; put questions in your report, not in the code.

### Gaps in the numbering

Two stretches of number are not used, and both are cheaper to leave alone than
to close:

- **Sections 46 to 49 are unused.** §45 is the Tracker; the four above it are
  free for whatever comes next.
- **The `.90` subsection band** — §11.90, §11.91, §18.90, §37.90 — is
  ordinary spec text that happens to sit high in its parent's range.

Renumbering either would break every citation in the tree, in source comments
as much as in prose, and `tools/checkdocs.py` would only catch the ones that
stopped resolving — not the ones that resolved to something else. That is the
whole reason to leave them.

The same rule governs the API table (§20.3): **a shipped slot keeps its
contract**, and "we no longer implement this" is a refusing stub, not a reuse
(§20.8 rule 4).

## 0. Goal

A Macintosh System 1-style graphical OS for an 8086/8088 XT-class machine. The
display adapter is detected at boot (§39): VGA 640x480 in 16 colors (mode
12h), or Hercules 720x348 / CGA 640x200, both 1bpp. Pre-emptive multitasking
via the PIT timer interrupt, switchable at run time to cooperative from the
Control Panel (§8.2/§31). Serial Microsoft mouse on COM1. Boots from floppy
straight into the GUI: gray dithered desktop, menu bar with pull-down menus,
draggable overlapping windows with title bars and close boxes. Built-in apps:
About dialog, Clock and Bounce (each running as its own
pre-empted background task, updating live while the user types or drags).

## 1. Hard rules (apply to every module)

1. **8086 only.** `kernel.asm` opens with `cpu 8086`; NASM will reject
   anything newer. Consequences: no `pusha/popa`, no `push imm`, no
   `shl reg, imm` other than 1 (use CL), no `movzx`, no 32-bit registers, no
   `imul r,r,imm`. `rep movsb/stosb/lodsb`, `mul`, `div` are fine.
2. **Near model.** CS = DS = `KERNEL_SEG` (0x0800) for all kernel code and
   tasks; **SS = `LOW_SEG`** (0x0440), because every task stack lives outside
   the kernel segment (§2.1). ES is scratch — any routine may change and use
   ES freely, but must restore it before returning unless documented
   otherwise. Calls between modules in `.text` are **near** calls; modules in
   there is no far code and no second code segment (§33).

   **SS ≠ DS has one consequence and it is easy to miss:** a `[bp]`,
   `[bp+disp]`, `[bp+si]` or `[bp+di]` operand addresses **SS**, not DS. Code
   that holds a pointer to a kernel structure in BP must write `[ds:bp+…]`.
   Code that genuinely means the stack (`mov bp, sp`) is correct as written.
   BP used as a plain scalar is unaffected.
3. **Register discipline.** Every public routine **preserves all registers**
   except its documented outputs. Callers rely on this. Flags are not
   preserved. Direction flag: assume DF=0 on entry; if you `std`, `cld` before
   returning. ISRs must `cld` before any string op, must push DS and ES on
   entry, load DS = KERNEL_SEG before touching any kernel variable, and
   restore both before iret/chaining — the interrupted context may be BIOS
   code with DS=0x40 or the ROM's DS. Any routine callable from an ISR must
   preserve the caller's interrupt flag: guard critical sections with
   `pushf`/`cli` … `popf`, never `cli` … `sti`.
4. **Large buffers go in `section .bss`** (NASM `-f bin` allocates .bss after
   the file image; it costs no disk space). Code and small data stay inline.
   NASM's section state persists across `%include`: any .inc that opens
   `section .bss` must switch back with an explicit `section .text` before
   the file ends, or the next include's code silently lands in .bss.
   kernel.asm's own final `section .bss` block (§15) is the last thing
   assembled. The build runs NASM with `-w+error` so the tell-tale
   "initialize memory in a nobits section" warning fails the build.
   Two further sections exist and follow the same switch-back rule:
   and `.lowbss` (task stacks + disk buffers in `LOW_SEG`, §2.1).
   All four are declared once, with their attributes, at the top of
   `kernel.asm`; modules switch with a bare `section <name>`.
5. **Label hygiene.** One flat namespace. Prefix every module-internal label
   with the module's prefix (`vga_`, `font_`, `mou_`, `cur_`, `sch_`, `evq_`,
   `wm_`, `menu_`, `ui_`, `app_`, `inst_`, `dsk_`/`disk_`, `ld_`/`loader_`,
   `fm_`/`files_`, `ico_`/`icon_`, `desk_`, `dock_`, `tm_`, `cp_`, `snd_`,
   `opl_`, `sbl_`, `vid_`) or use NASM local labels (`.foo`).
6. Public drawing routines may assume the caller holds the **gfx lock**
   (§7) and that the cursor is hidden. They must not take the lock
   themselves.
7. Every gfx routine clips to the live screen (§39.2 — 0..639, 0..479 on
   VGA) and leaves the VGA Graphics Controller in the default state on
   return: Set/Reset enable = 0, Bit Mask = FF, Function = replace, Data
   Rotate = 0, Map Mask = 0Fh, Read Map = 0, write mode 0 / read mode 0.
   When drawing is routed through the software renderer (§32/§39.3 — always
   on a 1bpp adapter, on VGA only while double buffering is active) the
   drawing routines render into `[vid_rseg]` and touch no VGA register at
   all; only `gfx_flush` (Sequencer Map Mask) and the cursor path program
   the hardware, and both restore the same defaults.
8. **A control that can refuse a click is greyed the way §46 says**, whether
   it is the kernel's or a package's: `CDGRAY`, the whole control and not just
   its caption, one predicate shared by the greying and the refusal, and words
   as well for anything drawn only as text — because `font_ink` rounds
   `CDGRAY` to black and a greyed *string* is invisible as such on two
   adapters out of three (§39.4).

## 2. Memory map

**A ladder, not a set of addresses.** Every region is defined as "the one
below it, plus its size", so changing a size slides everything above it and
there are no gaps to lose track of. The sizes are the only numbers, and all
of them live in one block at the top of `kernel.asm`.

**Every rung is derived, and none of them carries growth room.** The heap
starts where *this build's* kernel actually ends, so it moves whenever the
kernel does. A fixed ceiling with slack under it is memory nothing can ever
use, which is what the retired `KERN_MAX` was — and what the **package pool**
had become: 60KB reserved between the kernel and the heap whether or not a
single package was loaded. A package's region is an ordinary heap claim now
(§20.1/§50.3), which returned those 60KB to every machine and is the reason
the floor is **128KB of RAM** rather than 256KB.

| linear        | segment | contents                                          |
|---------------|---------|----------------------------------------------------|
| 0x00000       | 0x0000  | IVT + BIOS data area — theirs, 1,536 bytes         |
| 0x00600       | `KERNEL_SEG` | kernel image: `.text` + `.bss`, `KIMG_PARA` paragraphs (derived, 512-rounded) |
| derived       | `FAT_SEG` | mount-time FAT snapshot, `DSK_FAT_SECS`×512 = 4,608 bytes, via **ES only** (§18) |
| derived       | `LOW_SEG` | `.lowbss`: task stacks + disk buffers (9,216 B), then task 0's stack (`STK0_SIZE`, 1,024 B) growing down from `STK0_TOP` |
| derived       | `HEAP_SEG` | **the claim heap (§50)** — everything from there to the top of conventional memory, handed out on demand. Data claims grow **up** from here; a package's region is claimed **down** from the top (§50.3) |
| 0xA0000       | 0xA000  | VGA planar framebuffer, 80 bytes/row               |
| 0xB0000       | 0xB000  | Hercules framebuffer, 4 banks × 0x2000, 90 bytes/row (§39) — mono adapters only |
| 0xB8000       | 0xB800  | CGA framebuffer, 2 banks × 0x2000, 80 bytes/row (§39) — mono adapters only |

**The kernel is the first four rungs, and it fits in 64KB.** `KERNEL_SEG`
through the top of task 0's stack is one contiguous span — code, read-only
data, `.bss`, the FAT snapshot, the disk caches, the sector buffer and every
task stack — and guard 1 (§15.1) holds the whole of it to `KERN_BUDGET` =
65,536, the first 64KB above the BIOS data area. It measures 65,024 bytes
on the shipped build. **`docs/KERNEL-MEMORY.md` is the maintained account of
what that is spent on**; raising `KERN_BUDGET` is a decision to be taken
with whoever asked for the feature, not a build fix.

The one deliberate exception is the menu save-under (§12.4), which is a heap
**claim** rather than a reservation: 20KB that exists only while a pull-down
is on screen.

**Above `HEAP_SEG` nothing has a fixed address.** There used to be four
pinned blocks up there — `SND_SEG` (64KB), `SAVE_SEG` (48KB), `VIEW_SEG`
(16KB) and `BB_SEG` (150KB) — reserved from boot whether or not a byte of
each was ever written, 278KB of a 256KB floor machine's address space
spoken for by constants. The sound segment went with the sound cards (§34);
the other three became claims (§50), taken when they are used and released
when they are not. A package that wants memory outside its own region asks
the same allocator, which is what retires `apps/paint`'s unsanctioned grab
of linear 0x66000 (§42).

The floor machine is 256KB and the ladder fits it with room: BIOS 1.5KB, the
kernel's 63.5KB, the 60KB pool — and about 131KB of heap left over for
whoever asks first.

**`KERNEL_SEG` is 0x0060 — the first paragraph above the BIOS data area.**
It was 0x1000, then 0x0800, and the floor both times was the boot sector:
the BIOS loads `boot/boot.asm` to 0000:7C00 and that code is *still running*
while the kernel's sectors arrive, since it far-calls the splash at
`KERNEL_SEG:0008` after every one. The sector now **relocates itself** out of
the landing zone before it reads anything (§15.2), so the floor is gone.
`boot/boot.asm` carries its own `KERNEL_SEG` because it is assembled
separately, and `apps/os88api.inc` carries a third copy because it is baked
into every package's far-call targets: **all three move together, and every
`.o88` must be rebuilt**, since a package built against the old value
far-calls into empty memory.

### 2.1 The kernel's buffers

`LOW_SEG` holds the `.lowbss` section, addressed through **SS** (the task
stacks) or **ES** (the disk buffers), never DS. It sits *above* the kernel
image now, not below it — there is no low memory under the kernel any more,
because the kernel starts as low as the BIOS lets it.

- `sch_stacks` — 11 × `SCH_STACK` (512) = 5,632 bytes, task slots 1..11 (§8).
- Task 0 runs on the same segment at `STK0_TOP`, growing down onto the top
  of `.lowbss` — `STK0_SIZE` = 1,024 bytes. All tasks share one SS, so a
  switch is still an SP swap and SS is not part of the saved frame. **This
  is the reservation that lets a built-in app run at all**: About, Disk, the
  Control Panel and the Task Manager have no task of their own — they
  execute inside window callbacks on the UI task, on this stack. So does
  every package callback, the menu tracker and the Standard File dialog
  (§38), which is why task 0 gets the larger share of the two.
- `disk_dir` (the synthesized directory cache, 1,024 B), `disk_icons` (the
  harvested icon cache, 2,048 B) and `dsk_secbuf` (sector scratch, 512 B) —
  written through ES (int 13h ES:BX, or `rep movsw` from kernel scratch at
  mount, §18–19), read only through `dsk_get_dir` / `dsk_get_icon`, which
  stage one entry back into the kernel segment (§18). `dsk_secbuf` is also
  the write path's staging sector (§18.4).

**The stack numbers are measured, not guessed.** Every byte of the stack
region was filled with 0xCC at the top of `kmain` and the machine driven as
hard as it goes — Clock, two Bounces, About, the Control Panel on both its
pages, the Task Manager with a window drag, a Disk window, the Fractal with
its worker task, and Paint saving a GIF into a folder it created from the
file dialog. The deepest mark left was **246 bytes** on task 0's stack and
**150** on a background task's, ISR frames included (the tick and mouse
handlers run on whichever stack they interrupt). The reservations above are
4× and 3.4× those. Redo the fill probe before lowering either; guard 3
(§15.1) only proves `STK0_SIZE` is big enough to be a stack at all, not that
a task fits its own slice.

**`STK0_SIZE` is a constant, and that is load-bearing.** It used to be
"whatever is left between the top of `.lowbss` and the kernel segment", so
task 0's stack silently absorbed every byte saved anywhere below it and two
rounds of shrinking the buffers freed exactly nothing. Naming the number is
what turned those savings into memory.

`FAT_SEG` holds the mount-time FAT snapshot: up to `DSK_FAT_SECS` (= **9**)
sectors, 4,608 bytes, rewritten from FAT1 (or the FAT2 fallback, §18.3) on
**every** mount — no cross-mount state survives. Reached through **ES
only**, never DS: `dsk_next_clus` is the single reader and `dskw_setfat`
(§18.4) the single writer, and int 13h moves it via ES:BX only at mount (in)
and at a FAT flush (out). The whole region is the snapshot; there is no
reserve, and `FAT_PARA` is derived from `DSK_FAT_SECS` so the two can never
disagree.

**Why 9.** `DSK_FAT_SECS` is an *acceptance* threshold, not a buffer size:
§18.2 rule 10 refuses to mount a volume whose declared FAT is larger, before
a byte of it is read. 9 is exactly the largest FAT any geometry this OS boots
or builds declares — 360KB = 2, 720KB = 3, 1.2MB = 7, 1.44MB = 9 — and
nothing more. **The consequence is that FAT16 is unreachable**, because a FAT
is only FAT16 with ≥ 4,085 clusters and that needs ≥ 16 FAT sectors, so rule
10 turns every FAT16 volume away structurally. The FAT16 halves of
`dsk_next_clus` and `dskw_setfat` stay in the tree, and nothing can call
them.

### 2.1.1 Every disk-visible base is 512-byte aligned

int 13h moves one sector per call, which bounds a transfer to 512 bytes —
but **does not stop one from straddling a 64KB physical boundary**. Only
starting on a 512-byte boundary does that, and the DMA controller answers a
straddle with error 09h.

Every base in this ladder is an int 13h target: the FAT snapshot, the disk
buffers, a package image being loaded (§21), and a package's file buffer out
of the heap (§18.4). So `KIMG_PARA` rounds the image up to a whole **512
bytes** rather than to a paragraph, and because `FAT_PARA` (288), `LOW_PARA`
and `LOW_PARA` are multiples of 32 paragraphs, aligning that one
rung aligns the whole ladder. Guard 6 (§15.1) proves it.

This held by accident until the ladder became derived — every base used to be
a round constant like `0x0300` or `0x2A00`, and nothing said why that
mattered. The symptom when it broke was a **"Disk error" on any save larger
than the distance from the buffer to the next 64KB boundary**: Paint's 63KB
BMP hit it immediately, a Note Pad text file never would.

### 2.2 SND_SEG — retired

Linear 0x30000–0x3FFFF was claimed whole by the sound layer: a 4KB SB DMA
double buffer, an 8KB record ring and a ~52KB staging pool granted to
instances. All three belonged to hardware this OS no longer drives (§34),
and the segment is **gone** — 64KB back on every machine, a quarter of the
256KB floor. The speaker tiers need no buffer: `osapi_snd_play` paces the
caller's own `ES:SI`.

Nothing may quietly re-claim 0x30000 by name. The block is part of the
one heap §50 hands out, like every other free paragraph above the package
pool, and the only way to hold memory there is to claim it.

### 2.3 The file-manager view cache (§22.1)

Each open Disk window owns a **claim** of `VIEW_KB` (3KB) — 1KB of
synthesized directory entries plus 2KB of harvested icons, a byte-for-byte
image of `disk_dir` + `disk_icons` (§19). It is reached through a segment
the window's state block carries (`FS_VSEG`), read only through
`fmv_get_dir` / `fmv_get_icon`, which stage one entry back into the kernel
segment — the `dsk_get_dir` idiom of §2.1 verbatim. Slot map:

```
cache:0x0000..0x03FF   32 × 32B  image of disk_dir   (§19 synthesized entries)
cache:0x0400..0x0BFF   32 × 64B  image of disk_icons (§19 harvested icons)
```

What it buys is unchanged: a background file-manager window paints from
memory, so `wm_paint_all` — which has no clip rect and runs on every window
move — costs zero floppy I/O per repaint. What changed is that it is 3KB
per **open window** instead of four 4KB slots reserved from boot, that the
Task Manager bills it to the window that holds it, and that the instance's
teardown (§50.4) releases it with no close hook in `files.inc` at all.

**The no-cache fallback is a real path, not a panic.** `fm_kinit` takes the
claim; if the heap cannot fund 3KB the window still opens with
`FS_VSEG` = 0 and every reader falls through to the global mount snapshot in
`LOW_SEG`. That is correct — it is the same data — and merely costs a
re-mount when another window has moved the globals. `VIEW_SLOTS` (4) stays
the Disk kind's `KD_CAP`.

### 2.4 Above 1MB — the HMA and the extended-memory store (§41)

Not conventional memory and not on the ladder above: a separate space this
kernel can only reach through `xmem.inc` (§41.5), and only on a 286 or
better. `xm_init` sizes it with `int 15h AH=88h`; `cpu_hma_claim` takes the
first `XM_HMA_KB` (64) of it — the HMA, `HMA_SEG:0010`..`HMA_SEG:FFFF`, the
only bytes above 1MB real mode can name directly — and the remainder is the
pool `xm_alloc` hands out in 1KB units.

**Nothing up there holds code, on any tier** (§41.3/§41.9 rule 3), and on
tier 0 — the target machine — the whole space is zero bytes and every entry
point refuses.

## 3. Global constants (defined once in kernel.asm, used everywhere)

```nasm
KERNEL_SEG   equ 0x0800                ; linear 0x08000 - guard 8 fences it
VGA_SEG      equ 0xA000                ; against the boot sector at 0x7C00
; the memory ladder (§2) - each rung is the one below plus its size
KERN_BUDGET  equ 65536                 ; the WHOLE kernel, guard 1 (§2)
KIMG_PARA    equ ((KTEXT_SIZE + KBSS_SIZE + 511) / 512) * 32   ; 512-rounded
FAT_SEG      equ KERNEL_SEG + KIMG_PARA   ; FAT snapshot, via ES ONLY (§18)
DSK_FAT_SECS equ 9                     ; resident FAT cap, sectors (4,608 B)
FAT_PARA     equ DSK_FAT_SECS * 32     ; 512 bytes = 32 paragraphs
LOW_SEG      equ FAT_SEG + FAT_PARA    ; .lowbss: stacks + disk buffers
LOW_PARA     equ ((KLOW_SIZE + STK0_SIZE + 511) / 512) * 32
STK0_SIZE    equ 1024                  ; task 0's own stack (§2.1)
STK0_TOP     equ KLOW_SIZE + STK0_SIZE - 2
KERN_END     equ LOW_SEG + LOW_PARA    ; ...and there the kernel stops
KERN_SIZE    equ (KERN_END - KERNEL_SEG) * 16  ; what guard 1 measures
HEAP_SEG     equ KERN_END              ; the claim heap (§50). There is no
                                       ; package pool: a region is a claim
; VGA reference geometry, and the initializers of the live block (§39.2);
; the live screen is [vid_w] / [vid_h] / [vid_stride]
SCREEN_W     equ 640
SCREEN_H     equ 480
ROW_BYTES    equ 80
MBAR_H       equ 20          ; menu bar height, px
TITLE_H      equ 18          ; window title bar height, px
; colors (standard EGA/VGA 16-color indices)
CBLACK  equ 0
CWHITE  equ 15
CLGRAY  equ 7
CDGRAY  equ 8
; loadable programs (§20)
APP_MAX_SIZE equ 0xF000      ; image + bss budget: 60KB. The ceiling is the
                             ; SEGMENT - a package links at org 0 and
                             ; addresses itself with 16-bit offsets - and
                             ; what it can ACTUALLY get also depends on
                             ; what the heap has contiguous
PKG_DISP     equ 12          ; the dispatcher's fixed offset inside a
                             ; package's header (§20.2)
; double buffering (§32) - a heap claim, so there is no BB_SEG
BB_PLANE_PARA equ 0x960      ; paragraphs per plane (0x9600 bytes = 480 rows × 80)
BB_KB         equ 150        ; what bb_set claims to arm it
; the file manager's per-window view cache (§2.3/§22.1)
VIEW_SLOTS    equ 4          ; max Disk windows = the kind's KD_CAP (§29.3)
VIEW_KB       equ 3          ; each window's cache, claimed when it opens
```

## 4. Module files and ownership

| file                | owns                                                    |
|---------------------|---------------------------------------------------------|
| `kernel/kernel.asm` | entry, constants, init order, includes, .bss layout, **os8088 API jump table at 0x0010** (§20.3) + osapi helper routines, **boot splash entry at 0x0008** (§15) |
| `kernel/viddet.inc` | video adapters (§39): the boot probe, the live geometry block, mode set/teardown (`vid_setmode`/`vid_text`/`vid_init`), the shared addressing helpers `gfx_rowbase`/`gfx_nextrow`, the 1bpp colour map `gfx_ink` — prefix `vid_`; included **before** `splash.inc`, and all its data lives in `.text` |
| `kernel/splash.inc` | boot-time loading screen (§15): the first adapter probe and mode set, welcome dialog, pixel progress bar, spinning vector "8088" — on a 1bpp adapter the progress bar alone (§39.6); far-ticked by the boot sector per sector read; self-contained, no .bss |
| `kernel/vga12.inc`  | mode 12h planar primitives, save/restore, gfx lock; the coordinate core `vga_rect_setup` that both renderers share (§39.3) — the mode set left for `viddet.inc` |
| `kernel/vgabb.inc`  | the software renderer (§32/§39.3): RAM probe, back buffer, software planar primitives, dirty rect, `gfx_flush` — VGA's optional double buffer, and the only driver on a 1bpp adapter |
| `kernel/font.inc`   | 8x8 font (copied at init from the BIOS ROM set, or the IBM ROM's own on a pre-EGA machine), text draw |
| `kernel/mouse.inc`  | COM1 UART, IRQ4 ISR, packet decode, cursor (save-under) |
| `kernel/sched.inc`  | PIT hook, context switch, task table, spawn/yield/sleep |
| `kernel/events.inc` | 8-byte event records, system event ring queue           |
| `kernel/clock.inc`  | system clock (§37): the RTC ladder (§37.90 — MC146818 at 70h/71h, MM58167 and RP5C01 at 2C0h, int 1Ah last), the wall-clock date + time advanced from `[ticks]`, field editing and formatting — prefix `clk_` |
| `kernel/wm.inc`     | window records, z-order, frames, hit test, paint-all, `wm_owner` side table |
| `kernel/instance.inc` | instance table: records, kind descriptors, launch/close lifecycle (§29) |
| `kernel/memory.inc` | the claim heap (§50): the map, `mem_claim`/`mem_free`/`mem_avail`, the teardown fence — prefix `mem_` |
| `kernel/menu.inc`   | menu bar (System menu + the active application's name and menus), runtime bar layout, pull-down tracking, Locator's own menu set (§12/§12.2/§12.3) |
| `kernel/ui.inc`     | UI task: event pump, keyboard poll, drag, dispatch      |
| `kernel/apps.inc`   | built-in app kinds: About, Clock, Bounce — state pools, kinit procs, per-instance tasks |
| `kernel/disk.inc`   | BIOS int 13h floppy transfers (`disk_read`/`disk_write`), FAT12/16 mount + directory + chain walk (§18–19) |
| `kernel/diskw.inc`  | the FAT write path (§18.4): name parsing, cluster allocation + free, FAT flush, directory entry create/update/delete, the five whole-file operations — prefix `dskw_`; the ONLY caller of `disk_write` |
| `kernel/loader.inc` | package validation, pool allocation, per-instance load + relocate, launch (§21) |
| `kernel/files.inc`  | Disk window: file list UI, selection, open, refresh (§22) |
| `kernel/fdlg.inc`   | the Standard File dialog (§38): the kernel's Open/Save chooser, its modality gate and the completion callback — prefix `fdlg_` |
| `kernel/icons.inc`  | 1-bit icon format, draw routine, built-in library (§25) |
| `kernel/desk.inc`   | desktop drive icons: detect, paint, click/open (§26)    |
| `kernel/dock.inc`   | bottom dock strip: one tile per running instance, minimize/restore/activate (§30) |
| `kernel/taskmgr.inc`| Task Manager window: CPU load gauge + history graph, RAM readout, per-instance process list with CPU + memory (§28) |
| `kernel/ctrl.inc`   | Control Panel window: two-pane item list + settings pages (§31), prefix `cp_` |
| `kernel/snd.inc`    | sound core (§34): driver table + router, tone tier, speaker driver (tone + PWM clips), `snd_tick`, the five API slot targets, `snd_release_inst`/`snd_unhook` — prefix `snd_`, lands Phases 1–2 |

`kernel/video.inc`, `keyboard.inc`, `string.inc`, `gfx.inc` remain in the
tree but are **no longer included**; the GUI replaces the text shell.

## 5. vga12.inc

Mode 12h planar programming — the VGA path only: on a 1bpp adapter every
planar body here is unreachable and the software renderer draws instead
(§39.3). GC index port 0x3CE / data 0x3CF, Sequencer
0x3C4/0x3C5. Solid fills use Set/Reset (GC0=color, GC1=0Fh) with Bit Mask
(GC8) for edge bytes; a read of each VRAM byte loads the latches before the
write. XOR ops use GC3 function = XOR (bits 4:3 = 11, value 0x18) with
Set/Reset color 0Fh. For XOR ops (`gfx_xor_rect`, `gfx_xor_fill`) **every**
target byte must be read to load the latches immediately before it is
written — including full interior bytes, not just Bit-Mask edge bytes —
because the ALU XORs against the latch contents; `rep stosb` must not be
used for XOR operations (it is fine for the interior of solid replace-mode
fills, where the latches are irrelevant).

All coordinates are absolute screen pixels, x1<=x2, y1<=y2 (routines must
still behave sanely — draw nothing — if the clipped rect is empty). The
only clip is the screen edge, except while a background task has a clip
region armed (§11.3), in which case `gfx_fill`, `gfx_fill_gray`,
`gfx_xor_fill` and `gfx_xor_rect` are additionally cut to it — at the
public entry, above the `[bb_on]` dispatch, so the same hook serves both
renderers and all three adapters.
**Color for every drawing op comes from the byte variable `[gfx_color]`.**
Mode set and teardown are not in this module: `vid_setmode` / `vid_text` in
`viddet.inc` own them (§39.6).

| symbol          | in                                   | effect                                |
|-----------------|--------------------------------------|---------------------------------------|
| `gfx_color`     | byte variable                        | current drawing color                 |
| `gfx_pixel`     | CX=x, DX=y                           | plot pixel                            |
| `gfx_hline`     | AX=x1, BX=x2, DX=y                   | horizontal line, inclusive            |
| `gfx_vline`     | AX=x, BX=y1, DX=y2                   | vertical line, inclusive              |
| `gfx_fill`      | AX=x1, BX=y1, CX=x2, DX=y2           | solid rect, inclusive corners         |
| `gfx_frame`     | AX=x1, BX=y1, CX=x2, DX=y2           | 1px outline rect                      |
| `gfx_fill_gray` | AX=x1, BX=y1, CX=x2, DX=y2           | 50% dither: black/white checkerboard (pixel parity (x+y)&1: even=white, odd=black) — ignores gfx_color |
| `gfx_fill_pat`  | AX=x1, BX=y1, CX=x2, DX=y2, `[gfx_pat]` = near ptr to 8 pattern bytes | 8×8 dither fill, screen-aligned: row y uses byte `pat[y&7]`, bit 7 = leftmost pixel of each screen byte, bit set = white (15), clear = black (0) — ignores gfx_color. Writes only colors 0/15, so like `gfx_fill_gray` it never retires `[bb_mono]` (§32) |
| `gfx_blit4`     | ES:SI=source, BP=source stride (bytes), AX=dest x, BX=dest y, CX=width px, DX=height rows | draw a block of packed 4bpp pixels (§5.4) |
| `gfx_xor_rect`  | AX=x1, BX=y1, CX=x2, DX=y2           | 1px outline, XOR 0Fh (drag outline)   |
| `gfx_xor_fill`  | AX=x1, BX=y1, CX=x2, DX=y2           | filled rect, XOR 0Fh (menu highlight) |
| `gfx_save`      | AX=x1, BX=y1, CX=x2, DX=y2, ES:DI=buf| copy region to buffer; x1 is rounded **down** to a byte boundary and x2 **up** internally. Buffer layout: plane 0 rows, plane 1 rows, plane 2, plane 3 — all four on VGA, the single plane at 1bpp (§39.3). Returns DI advanced past data. |
| `gfx_restore`   | AX=x1, BX=y1, CX=x2, DX=y2, ES:SI=buf| write region back (same rounding/layout). Returns SI advanced. |
| `gfx_lock`      | —                                    | acquire drawing mutex + hide cursor (§7) |
| `gfx_unlock`    | —                                    | flush the back buffer (§32), show cursor, release mutex |
| `gfx_flush`     | —                                    | copy the dirty back-buffer rect to VRAM; no-op when double buffering is off or nothing is dirty (§32) |

Save/restore for a W-px-wide, H-px-tall rect uses
`bytes = ((x2/8) - (x1/8) + 1) * H * [vid_planes]` — ×4 on VGA, ×1 on a 1bpp
adapter (§39.2). Buffers are budgeted for the VGA worst case, so no routine
computes the size at run time.

**Software-renderer dispatch (§32/§39.5).** Every public drawing entry above
(`gfx_pixel` … `gfx_restore`) starts with a `[bb_on]` test and branches to
its `bb_*` twin in vgabb.inc when the renderer is in use — a back buffer on
VGA, permanently on a 1bpp adapter, where that renderer *is* the driver;
contracts, clipping and buffer layouts are identical in both paths. Four
VRAM bodies remain reachable under their own names, for callers that must
not touch the back buffer: `vga_save_vram`/`vga_restore_vram` (the mouse
cursor's save-under, §9) and `vga_xor_rect_vram`/`vga_xor_fill_vram` (the
drag outline and the menu highlights, §12/§13). All four are transient
overlays — drawn and erased within one held lock — so they live in VRAM
only, never in the back buffer (§32); each still opens with a `[vid_mono]`
test into its `bb_*` twin, because at 1bpp "direct to VRAM" and "through the
renderer" are the same place (§39.5).

### 5.4 `gfx_blit4` — a block of pixels

The one primitive that takes an IMAGE rather than a shape. Source is packed
4 bits per pixel, two pixels to a byte, **high nibble leftmost** — the layout
a 16-colour BMP row uses and the one apps/paint's canvas already had. BP is
the byte stride from one source row to the next and is added as a **word**,
so a negative stride (a bottom-up image) is simply `65536 − stride` and works
as long as every row of the block stays inside the segment.

It coalesces runs of equal pixels and emits one `gfx_hline` per run. That is
deliberately not a plane-parallel fast path: going through `gfx_hline` means
it works on all three adapters, in front of and behind the back buffer, and
inside a clip region without one line of adapter-specific code, because
`gfx_fill` already does. Colour reduction on a 1bpp adapter happens per run
in `gfx_fill` (§39.4), so a 16-colour picture reads as black/dither/white
exactly as the rest of the UI does. `[gfx_color]` is left holding the last
colour drawn; every register is preserved.

**The run scan compares BYTES, not pixels, and that is the whole speed of
it.** A run of colour c is a run of bytes equal to `c|c<<4`, so `repe scasb`
walks it two pixels at a time at about 15 clocks a byte — roughly seven and a
half clocks a pixel — with the odd ends handled by hand: the first pixel when
the run starts on a low nibble, the last when it ends on a high one. This is
`apps/paint`'s own `pt_runend` moved into the kernel, unchanged.

The first version of this routine decoded every pixel individually instead —
read the byte, test the parity, `shr al, cl` by four, compare, branch — which
is 75 to 90 clocks a pixel on an 8086, a 4-bit shift by CL being 24 of them
on its own. It kept the *shape* of the optimisation (one call per run) and
threw away the optimisation: a 448×280 repaint went from about a quarter of a
second on a 4.77MHz 8088 to over two, and the far calls it saved were noise
against that. **QEMU cannot show this — it does not model 8086 timing at all**
— which is why the cycle counts are written down rather than measured, and
why a change to this loop wants a cycle count in the commit message.

Why it exists at all: a package owns a segment (§20.1), so every gfx call
from one is a FAR call. The identical run scan written inside the package
cost one far call per run — hundreds to thousands per canvas repaint — and
this costs one. A plane-parallel VGA path (byte-per-8-pixels masks written
through the Map Mask) would beat this on detailed pictures and lose on flat
ones, and would need its own back-buffer and 1bpp twins; it changes no
caller, so it stays available as a later optimisation.

### 5.5 `gfx_scroll` — move a rect instead of redrawing it

**in** AX/BX/CX/DX = x1/y1/x2/y2 inclusive, absolute screen coordinates;
SI = dy **signed**, positive scrolling the content *up*. Caller holds the
gfx lock. **out** CF=0 moved, CF=1 refused and nothing touched. Every
register preserved — CF is the whole answer.

A scrolling view is the one case the repaint optimisations elsewhere in this
spec cannot help with: row signatures (§27.2) find every row changed, and a
damage rect (§11.91) is the whole view. But the pixels are not *new* — they
are the pixels one row up. Moving them is a `rep movsb` per row; redrawing
them is `font_char` per cell, which on a 4.77MHz 8088 is the difference
between a scroll that keeps up with the key repeat and one that does not.

**The |dy| vacated rows are the caller's to repaint**, and their content
after the call is unspecified — this primitive moves pixels and invents
none.

Refused, all with nothing moved, so a caller may simply fall back to a full
repaint:

- **x1 or x2+1 is not a multiple of 8.** The blit is byte-column granular
  on every adapter, because on VGA the latches move eight pixels at a time
  and on mono a byte *is* eight pixels. Sub-byte scrolling would need a
  shift-and-merge pass, which is most of the cost of drawing.
- Empty or inverted rect, `|dy|` = 0, `|dy|` ≥ the rect's height, or any
  edge off the live screen (§39.2's `[vid_w]`/`[vid_h]`, not the VGA
  reference constants).
- **An armed clip region (§11.3) that does not wholly contain the rect.**
  Whole-shape clipping, the `font_char`/`ico_core` class: a blit cannot be
  cut per pixel, so it refuses and the caller repaints — which clips per
  strip. This is the granularity rule in its third instance.

Three backends, one contract (§39), and they are *coherent* rather than
merely parallel:

| adapter | how |
|---|---|
| VGA direct | GC write mode 1: a `movsb` read fills the latches and the write stores all four planes at once. Mode restored after (§1 rule 7). |
| VGA + back buffer | `gfx_flush` **first**, then the four buffer planes *and* VRAM scroll in step. Nothing is marked dirty — they stay identical by construction. |
| Hercules / CGA | per-row `rep movsb` through `gfx_rowbase`, so the banked interleave is absorbed rather than special-cased. |

The flush-first rule is the subtle one. A pending dirty rect inside the
region means RAM holds pixels VRAM has not seen; scrolling both would move
fresh RAM against stale VRAM and the next flush would write the *unscrolled*
rect over the scrolled one. Flushing first makes the two identical before
either moves.

Verified against a byte-exact reference on all three: the mono path over
CGA's bank boundary, and the buffered path pixel-for-pixel against the
direct one.

## 6. font.inc

`font_init` runs **after** `vid_setmode` (§39.6): zero ES:BP, then int 10h
AX=1130h BH=03h returns ES:BP → the ROM 8x8 font; copy glyphs 32..126
(95 glyphs × 8 bytes) into a kernel buffer. A pre-EGA BIOS does not
implement AH=11h and leaves the pair as we set it, which is why it is zeroed
first — that case falls back to the IBM ROM 8x8 set at F000:FA6E. No font
bytes are hard-coded.

| symbol       | in                       | effect                              |
|--------------|--------------------------|--------------------------------------|
| `font_init`  | —                        | copy ROM font to RAM                 |
| `font_char`  | CX=x, DX=y, AL=char      | draw 8x8 glyph, color `[gfx_color]`, transparent background |
| `font_str`   | CX=x, DX=y, SI=NUL str   | draw string left→right               |
| `font_width` | SI=NUL str               | out AX = pixel width (8 × length)    |
| `font_str_x` / `font_width_x` | ES:SI = NUL str | the same two, reading the string through **ES** — what the `X` stubs of §20.3 call so a package's string can live in its own segment |
| `font_run` / `font_run_x` | CX=x, DX=y, SI (ES:SI) = NUL str, AL=ink, AH=background | one **opaque** run: the cells' background AND their glyphs, in a single pass (§6.1). API slot 0x0258 |
| `osapi_font_glyphs` | — | out SI = the offset of `font_glyphs` in KERNEL_SEG, AL = FONT_FIRST (32), AH = FONT_LAST (126), CX = 8 bytes per glyph. API slot 0x0218 |

**Handing out the bitmaps** (`osapi_font_glyphs`) is for an app that draws
text into its OWN pixels rather than onto the screen — apps/paint's text tool
stamps glyphs into the canvas, so `font_char` is no use to it. The table is
95 glyphs of 8 rows, row 0 first, bit 7 leftmost, and it is read through ES
(KERNEL_SEG on entry to every callback, §20.1). Before the slot existed the
package re-ran `font_init`'s probe — int 10h AX=1130h BH=03h with the
kernel's own F000:FA6E fallback behind it — to arrive at a table the kernel
had already built, and got whatever typeface the BIOS happened to hold rather
than the one the UI draws.

Characters outside 32..126 draw as space. Clipping is **whole-cell**: a
glyph whose 8x8 cell would cross a screen edge is skipped entirely, and so
is one whose cell is not wholly inside a single fragment of an armed clip
region (§11.3) — a glyph is one shape or none, and half an 8x8 glyph is
unreadable. Glyph rows may straddle two VRAM
bytes; use Set/Reset + Bit Mask with the glyph row shifted across a 16-bit
window. Under `[bb_on]` (§32/§39.5) `font_char` branches after clipping to
the software renderer, which applies the same shifted row masks to every
plane the adapter has (or/and-not per the `[gfx_color]` plane bit — at 1bpp
there is one plane and one bit, and `font_ink` rounds everything but pure
white to black, because a dithered 8x8 glyph is unreadable, §39.4).

### 6.1 `font_run` — the erase-and-letter pair, as one operation

**Every text-bearing element in this system fills a rect and then letters
it**, and each of them writes those bytes twice: once as a fill, once as a
read-modify-write per glyph. `font_run` does it once. It takes both colours
and paints each 8x8 cell complete — background where the glyph is clear, ink
where it is set.

**This is a speed optimisation and nothing else**, and it earns its place on
the two mono adapters. On a 1bpp adapter at a byte-aligned x the cell owns
its whole framebuffer byte, so there is nothing underneath to preserve: no
shift, no read, no second byte, and no separate fill pass at all. Per plane
the byte is `(glyph & ink) | (~glyph & background)` with each mask 00 or FF,
which on mono reduces to the glyph or its complement. That is the path the
slow machines are on — `[bb_on]` is permanently 1 there (§39.5) — and it is
where the cost of text actually lives.

The fast path is deliberately narrow: `[bb_on]` set **and** `x & 7 == 0`
**and** `[gfx_dis]` clear (a disabled control's glyph is masked to a
checkerboard inside `font_ink`, §47, which a plain store cannot express).
Anything else — a VRAM planar target, an unaligned run, a greyed one — falls
back to one `gfx_fill` of the run's rect in the background colour and
`font_str` over it, which is exactly what every caller wrote by hand before.
The rendered answer is identical either way, so a caller never has to ask
which path it got.

Clipping is `font_char`'s, per cell, screen edge and clip region alike — the
background clips with the glyph rather than independently of it.

**It also does not flicker, and on a slow machine that is not a small thing.**
The erase-and-letter pair leaves the run *blank* between the fill and the last
glyph, and on a 4.77MHz XT that gap is tens of milliseconds — several display
frames, plainly visible as a flash every time the text is redrawn. `font_run`
has no such interval on either path: a cell goes from its old content to its
final content in one write, so there is never a moment when the line is empty.
Observed directly on the XT — §11.94's benchmark draws the same line three
ways, and the two erase-and-letter rows flicker and flash while the `font_run`
row draws smoothly. **No timing column reports this**, because the two methods
take comparable time and differ in what is on screen during it.

#### 6.1.1 What it is worth, measured

`apps/fontbench` is the measurement. It lives on the **`testing` branch**, not
here — it ships on no disk and nothing in this tree builds or cites it, so it
is tooling rather than system, and the numbers below stand on their own record
the way any other measured figure in this document does. What it does: draws
the same ten-character run (`'C-2 01 A0F'`, a tracker pattern cell) 120 times
four ways — the hand-written `gfx_fill` + `font_str` PAIR and one `font_run`,
each at a byte-aligned x and again at x+5 — and times each against counter 0
of the 8253 read directly, because a 55ms tick cannot resolve a 3ms row.

Run under QEMU with `-icount`, so the PIT counts guest **instructions**
rather than host time and the result is deterministic and machine-independent
(runs agreed to ±1 count). Each path is measured twice: at a byte-aligned x,
and at x+5. **The skewed row is the honest status quo**, because
`ui_drag` writes `W_X` straight from the mouse and a draggable window's
content x is arbitrary mod 8; the aligned row is what a window would cost if
something guaranteed the alignment. x+5 and not x+1 because the ROM font's
rightmost column is blank in every glyph, so a one-pixel shift spills nothing
into the second byte and flatters the unaligned case.

| adapter | PAIR aligned | PAIR at x+5 | `font_run` aligned | `font_run` at x+5 |
|---|---|---|---|---|
| VGA, `[bb_on]` = 0 | 2694 | 2957 | 2763 | 3023 |
| CGA 640x200 | 3400 | 3513 | **2695** | 3580 |
| Hercules 720x348 | 3369 | 3483 | **2673** | 3548 |

**On mono, aligned `font_run` against the unaligned pair is 1.30x — and
against the aligned pair 1.26x.** The two mono adapters agree to within 1%,
which is what should happen: same renderer, different stride and bank wrap.

**Instruction count understates the win on the hardware this is for**, because
what the fast path removes is disproportionately *memory* work, and on a
4.77MHz machine a framebuffer access on the 8-bit ISA bus is the expensive
part. Counting framebuffer byte-accesses directly (temporary kernel counters
at the three write sites, read over QMP) gives the same figures on Hercules
and on CGA. `font_run`'s fast path is a flat **80** — ten cells x eight rows
x one store, no reads at all, at every alignment it accepts. The pair is not
flat, because `font_char` writes a second byte whenever the shifted glyph
spills into it and the fill covers one more byte column:

| x mod 8 | fill | glyphs | PAIR total |
|---|---|---|---|
| 0 | 128 | 100 | **228** |
| 1 | 136 | 100 | 236 |
| 2 | 136 | 128 | 264 |
| 3 | 136 | 162 | 298 |
| 4 | 136 | 174 | 310 |
| 5 | 136 | 190 | 326 |
| 6 | 136 | 194 | 330 |
| 7 | 136 | 200 | 336 |

Mean 291. So the operation goes from **228..336 depending where the window
was dragged to, to a flat 80** — 2.85x at best and 4.2x at worst, 3.6x on
average.

**And on a real XT the traffic figure is NOT the one that comes true.** It was
written here that instruction count "understates the win", that the true
figure sat between the two and probably near the traffic end. Run on a
4.77MHz 8088 with a Hercules card, the same four rows say:

| row | ms for 120 runs | per run |
|---|---|---|
| PAIR aligned | 1211 | 10.09 ms |
| `font_run` aligned | **932** | **7.77 ms** |
| PAIR at x+5 | 1251 | 10.43 ms |
| `font_run` at x+5 | 1267 | 10.56 ms |

**1.30x aligned against aligned — the instruction figure to three digits, not
the 3.6x traffic figure.** Alignment alone is 3.3% and the fallback costs 1.3%
over hand-writing the pair, both close to what `-icount` said. So on this
machine the per-cell overhead — the clip test, the glyph lookup,
`gfx_rowbase`, `font_ink`, the plane setup — dominates the eight byte-writes
it guards, and instructions are the better proxy. The traffic count remains
the right *explanation* of where the writes went; it is not the right
predictor of time, and this section previously said it was.

Worth keeping for its own sake: **about 1 ms per 8x8 cell** on that machine,
which two independent harnesses agree on (fontbench 10.09 ms per ten cells,
typebench 33.3 ms per forty). Nothing else in this document measures the
hardware all of this is for. The aligned row is measured; the rest is that measurement's model
(fill = two masked edge columns at 8 rows x 2 read-modify-writes, plus the
interior as word stores; glyphs = one read-modify-write per non-blank glyph
row, two when the shift spills) evaluated against the ROM font actually in
memory, and it predicted the aligned case and the repaint's own contribution
to the byte.

**Alignment is worth something on its own**, before `font_run` enters: the
pair itself is 1.03x cheaper aligned on mono and 1.10x on VGA (four planes,
so the spilled second byte costs four times). But most of the value is in the
single-store path, and alignment is what unlocks it — see §6.1.4.

**On VGA `font_run` costs 2.5%.** Both `font_run` rows sit 2.4-2.5% above
their matching pair, which is the tell that `[bb_on]` = 0 sends everything to
the fallback whatever the alignment; the 2.5% is the far call, the gate tests
and `font_width_x`. That is the price of the abstraction on the adapter it
cannot help, it is small, and it is why the tracker calls `font_run` only
when `[tui_mono]` says the fast path is reachable — on colour its row band
was filled once already, so the fallback's fill would be a second pass over
the same ground.

#### 6.1.4 Unaligned cannot be made fast, which is why alignment is a design question

The obvious alternative to constraining x is to widen the fast path to
handle any x. It does not work, and the arithmetic says so before any code is
written. At an unaligned x an opaque cell row spans two framebuffer bytes and
the neighbours' bits in both of them must be preserved, so the write becomes
read-both / merge / write-both: **4 accesses per cell row, 320 per
ten-character run** — against 228..336 for the pair it would replace. It is a
wash at best, because the pair's fill gets to use `rep stosw` and reads
nothing at all across the interior.

The single store exists only because the cell owns its whole byte. So the
2.85x-to-4.2x is not available by making `font_run` cleverer; it is available
only by arranging that text lands on multiples of 8, and that is a question
about **window placement**, not about the renderer.

#### 6.1.2 One decision per cell, on BOTH paths

`font_run` cannot produce §11.3's granularity failure. A fill clips per pixel
and a glyph per whole cell, so an erase-then-letter pair can disagree and
leave a cell blanked rather than stale — the sharp edge that section is mostly
about. Here the two are one decision about one cell, so a caller needs no
`wm_clip_test` gate around the pair and cannot get it wrong.

On the fast path that is free: `font_run_cell` asks about the cell once and
then either paints the whole cell or leaves it entirely alone.

**The fallback had to be taught it**, and for a while it was not. `font_run`'s
slow path *was* `gfx_fill` followed by `font_str_x` with no clip handling of
its own — the exact pair the rule warns about — so under an armed clip region
a horizontal edge filled the visible rows and lettered none of them. That was
survivable while the tracker was the only consumer, because it calls this on
mono only and the fast path always fires there; it stopped being survivable
the moment kernel code drew through it (§28). So the fallback now picks:

- **no region armed** — one `gfx_fill` and one `font_str`, as before. The pair
  cannot disagree when nothing is clipping it, and this is the overwhelmingly
  common case.
- **a region armed but the whole run inside one fragment** — the same, on one
  extra `wm_clip_test`.
- **an edge actually crossing the run** — `font_run_scell` per cell: test the
  cell, then fill it and letter it behind that one answer.

So the per-cell cost is paid only by runs an edge really cuts, and the
guarantee is now the *call's* rather than the fast path's. A caller may draw
under an armed clip region without gating.

#### 6.1.3 The first consumer, and what it changed to qualify

**The tracker's pattern view** (§45.9), whose channel columns were moved onto
8-pixel boundaries to earn the fast path: `TL_CHX` 28→32 and `TL_CHW` 145→144
with `TL_PAD` 28→24 on VGA, 70→72 / 145→144 / 28→24 on Hercules. Cell text x
is `CHX + n*CHW + PAD`, so that turned `56 + 145n` — where only channel 0
was aligned, 145 not being a multiple of 8 — into `56 + 144n`, where all four
are. The compact CGA layout was already aligned.

Alignment is why the tracker is the only consumer so far and not simply the
first of many. It is **fullscreen** (§11.2), so its content origin is (0,0)
and cannot move. An ordinary window's is `W_X + 1` and `ui_drag` writes `W_X`
straight from the mouse with no snapping, so for anything in a draggable
window `x & 7` is arbitrary and re-rolls on every drag: the fast path would
fire one time in eight. Adopting `font_run` there buys the code deletion and
§6.1.2's guarantee one press in eight, and the 2.5% fallback cost the rest of
the time. Snapping window x to a multiple of 8 would make it general and is
not worth what it would do to dragging.

## 7. Concurrency model (read carefully — this is the crux)

- **gfx lock**: byte `gfx_lock_flag` in vga12.inc. `gfx_lock`:
  `cli`; if flag set → `sti`, `call task_yield`, retry; else set flag, `sti`,
  then `call cursor_hide`. `gfx_unlock`: `call gfx_flush` (§32 — pushes the
  dirty back-buffer rect to VRAM while the cursor is still hidden; a no-op
  without double buffering), then `call cursor_show`, then clear flag. That
  order is binding: the flush must complete before the cursor may reappear,
  and the cursor's save-under must be taken from the freshly flushed VRAM.
  Every task-level drawing burst is wrapped in gfx_lock/gfx_unlock.
- **Cursor vs ISR**: the mouse ISR never draws while `gfx_lock_flag` is set
  **or while `cur_level` < 0** (cursor refcount-hidden — the save-under
  buffer is only valid while the cursor is drawn); in either case it only
  updates `mouse_x/mouse_y`, sets `cur_dirty`, and returns. `cursor_show`
  (called from gfx_unlock or directly) redraws at the latest position,
  clearing `cur_dirty`. Only when the lock is free AND `cur_level` >= 0 does
  the ISR itself move the cursor (restore save-under at old position, save +
  draw at new). The ISR runs with IF=0 throughout — it cannot race a task
  that is *inside* the lock's cli window (and nothing it calls may `sti`,
  see §1 rule 3).
- **Background drawing**: a task that draws its own window outside
  `wm_paint_all` takes the lock, re-checks the visible bit, calls
  `wm_clip_set` (§11.3) and draws only if it answers CF=0 — every `gfx_*`
  and `font_*` call it then makes is cut to the part of its content nothing
  is covering. The region is built and consumed inside that one lock hold
  and `gfx_unlock` clears it, so it can neither go stale nor leak into the
  next painter. The `gfx_*` primitives take absolute screen coordinates and
  have no other clip, which is the whole reason this rule exists: without
  it, a covered window paints over the window on top of it.
- **BIOS calls**: only the UI task calls int 10h/16h after boot. Background
  tasks (clock, bounce) use only os8088 primitives and `ticks` — as does a
  package's §20.6 worker, which reaches those primitives through the API
  table. The syscall gate
  remains for compatibility but the GUI does not depend on it.
- **Scheduler lock**: `sch_lock` byte; when non-zero the timer ISR counts
  ticks but does not switch. Normal code uses gfx_lock, which does NOT stop
  pre-emption (background tasks keep running during drags; they just block
  on gfx_lock when they try to draw). Outside the scheduler's own
  internals, exactly **two** routines raise it, both with the same meaning
  — the tick still runs, the BIOS chain feeds the floppy motor,
  `sch_account` runs and sleepers mark ready; only involuntary switching
  pauses: `disk_read` across its int 13h window (§18), and `spk_pcm_run`
  for the duration of an exclusive speaker clip (§34.4, Phase 2). The clip
  case adds nothing new for the cursor either: the mouse ISR keeps
  `mouse_x/y/btn` fresh throughout, and when the clip was started from a
  window callback — which holds the gfx lock, the normal trigger — the
  cursor is simply frozen until the clip ends, §7's own cursor rule and
  not a third state.
- **Scheduler mode**: multitasking is round-robin in both of the two modes
  of §8.2 — **pre-emptive** (the boot default: every unlocked tick
  switches) and **cooperative** (a task runs until it reaches the switch
  path itself, with a `SCH_WD_TICKS` watchdog as the backstop). The mode is
  one byte, `sch_coop`, flipped from the Control Panel (§31) at any time
  from any task; nothing else in the tree may read it to decide behavior.
  Every rule in this section holds unchanged in both modes: the locks, the
  cursor/ISR protocol and the BIOS restriction are about who may touch
  what, not about when the CPU is taken away.

## 8. sched.inc — round-robin, pre-emptive or cooperative (§8.2)

- `MAX_TASKS equ 12`. Task 0 is the boot thread (becomes the UI task); it
  runs on the task-0 stack (SS:`STK0_TOP`, §2.1) and owns **no** slice of
  `sch_stacks`.
  Slots 1..11 are dynamic — spawned by `app_launch` (§29), by
  `inst_pkg_spawn` for a package's one worker (§20.6) and by the transient
  sound refill/drain tasks (§34.5), freed by `task_exit`. Three claimants
  for eleven slots: a refused `OSAPI_TASK_SPAWN` (CF=1) and a stream
  open's err 6 are ordinary outcomes, not edge cases, and a package must
  degrade rather than abort when it cannot have its worker. Each slot has
  an `SCH_STACK`-byte stack — **512**, measured (§2.1), not guessed:
  `sch_stacks resb (MAX_TASKS-1) * SCH_STACK` **in `.lowbss`** (§2.1),
  slot n's stack top at
  `sch_stacks + n*SCH_STACK` (slot 1 owns bytes 0..511).
- Task record (8 bytes): `T_STATE` (0 free, 1 ready, 2 sleeping), `T_SP`
  (saved SP), `T_WAKE` (tick count to wake at), `T_INST` at offset 6
  (byte: owning instance index, §29; 0xFF = none — the Task Manager
  resolves slot → instance → name through it), 1 byte padding.
  `T_SIZE equ 8`.
- Timer: hook int 08h. Handler: push registers + DS/ES, load DS=KERNEL_SEG,
  chain to the saved BIOS vector first (`pushf` + far call), then
  `inc word [ticks]`, wake sleepers whose `T_WAKE` has passed, and if
  `sch_lock` is clear **and the mode check of §8.2 allows it**, switch: with
  all GP regs + ES + DS on the current stack, store SP in the task record,
  pick the next ready task round-robin, load its SP, pop, `iret`. (The BIOS
  handler already sent EOI; do not send another.)
- `sched_init` — set up task 0 as current/ready, save old int 08h vector,
  install handler. Also explicitly stores 0 to `sch_lock`, `ticks` and the
  three mode fields of §8.2 — nothing clears .bss at boot (`-f bin` puts no
  bytes on disk and the boot sector does not blank the region), so every
  .bss byte that needs a defined initial value is stored explicitly by its
  module's init routine.
- `task_spawn` — three callers: `app_launch` (a built-in kind's `KD_TASK`,
  §29.4), `inst_pkg_spawn` (a package's worker, §20.6) and the SB
  refill/drain spawns (§34.5). In: AX = entry point (near), DX = argument
  word — delivered
  in the new task's DX register; DL is additionally stored to `T_INST`
  (instance index, 0xFF = none). Builds a fresh stack frame that `iret`s
  into the entry with IF=1, DS=ES=KERNEL_SEG, all other GP regs zero.
  Out: CF set if no free slot, else CF clear and **AL = slot index**
  (1..MAX_TASKS-1; the scan starts at slot 1 — slot 0 is the UI task and is
  never free). Clobbers AX. Zeroes the slot's `sch_cycles` dword **before**
  publication (the ISR only charges the running slot, so a pre-publish
  write cannot race; §8.1/§28). **Publication order matters**: the timer
  ISR is live during spawn, so the full frame must be built and T_SP stored
  before the slot is marked ready — the `mov byte [T_STATE], 1` store must
  be the last write to the record (a byte store is atomic w.r.t. interrupts
  on the 8086). **The whole routine is atomic** (`pushf`/`cli` … `popf`
  around the slot scan *and* the publish): publication order alone only
  makes a half-built record safe to *read* — it does nothing about two
  spawners choosing the same free slot, and since §20.6 the callers are no
  longer one thread (a package's `W_PAINT` may spawn its worker, and
  `W_PAINT` is dispatched by whichever task drives the repaint, while
  `app_launch` spawns lock-free on the UI task). Losing that race costs one
  task and leaks one instance record for the session. The IF=0 window is
  ~30 stores with no I/O, shorter than `task_exit`'s. Because `popf`
  restores the caller's CF along with IF, the answer is carried across it
  in AL (0 = no slot) and re-derived by a `cmp al, 1` after the `popf`.
  A task's entry routine must never `ret` — it terminates
  only via `task_exit` (usually through `inst_task_die`, §29) or loops
  forever.
- `task_exit` — terminate the CURRENT task; self-exit only, never returns.
  In: BX = pointer to a release byte zeroed atomically with the task's
  death (an instance record's I_STATE, §29), or 0 for none. Body runs
  entirely under IF=0 (`cli` with no matching `popf` — control never
  returns): charge the final slice via `sch_account`, store 0 to the
  release byte, store 0 to the own slot's T_STATE, then `jmp sch_switch`.
  Because the release byte and T_STATE are cleared inside one IF=0 window,
  an instance slot and its task slot free at the same instant — "instance
  free but task table still holding the zombie" is unobservable. The
  freed record parks a meaningless SP (harmless); the round-robin scan
  skips it (T_STATE=0), and the resume-the-outgoing-task fallback cannot
  pick a dead task because it is only reachable when nothing is ready and
  **task 0 (the UI task) never sleeps and never exits**. PLACEMENT: the
  routine must sit outside the `sch_isr` → `sch_switch` → `sch_resume`
  fall-through run (it reaches sch_switch by explicit `jmp` only).
- `task_yield` — voluntarily give up the slice: `int 0x08`? **No** — that
  would re-run the BIOS tick. Instead simulate: `pushf`, `cli`, push CS/near
  return via a small stub so the stack looks exactly like the ISR frame, then
  jump into the common switch path. (Implementation detail is yours; the
  observable contract: returns later with all registers preserved, IF=1.)
- `task_sleep` — in: AX = ticks to sleep (18 ≈ 1 second). Sets T_WAKE =
  ticks + AX, state = sleeping, yields. A sleeping task is skipped until
  `[ticks]` passes T_WAKE, then becomes ready.
- `ticks` — public word, wraps at 65536; only compare with subtraction
  (`mov ax,[ticks]` / `sub ax,[t0]` / `cmp ax,n`).
- `sched_unhook` — restore the original int 08h and int 0Ch vectors (calls
  `mouse_unhook` too) — used before reboot. Afterwards it restores PIT
  channel 0 to the BIOS-default mode 3 (control word 0x36, then two zero
  bytes to port 0x40), the three OUTs under `pushf`/`cli` … `popf`.

### 8.1 CPU cycle accounting (for the Task Manager, §28)

- `sched_init` reprograms PIT channel 0 to mode 2 (rate generator), divisor
  0 = 65536 (`out 0x43, 0x34`, then two zero bytes to port 0x40) **before**
  installing the int 08h vector (the BIOS tick handler never touches PIT
  ports, so IF=1 is safe there), and then seeds `sch_pit_last` with a real
  timestamp (below). Mode 2 keeps the same 18.2065 Hz IRQ rate — BIOS
  timekeeping counts IRQs and is unaffected. The default mode 3 is useless
  for elapsed-time reads (it decrements by 2 and wraps twice per period);
  mode 2 counts down by 1 from 65536 with one IRQ per reload.
- `sch_account` (module-internal) — called with IF=0 and DS=KERNEL_SEG from
  exactly two places: sch_isr, after the BIOS chain **and after
  `inc word [ticks]`**, before the sch_lock check (so it runs every tick,
  including while floppy reads hold sch_lock); and task_yield's save path,
  right after DS is loaded. It builds the 32-bit timestamp
  now = [ticks]·65536 + phase, where phase = 65536 − latched count
  (0 at reload, monotone within a period): latch (out 0x43, 0), read count
  lo/hi from port 0x40, negate. Pending-IRQ disambiguation: the counter may
  have reloaded during an IF=0 window with int 08h not yet delivered (then
  [ticks] is one behind the phase) — after latching, read the PIC IRR
  (out 0x20, 0x0A; in al, 0x20); if bit 0 is set AND phase < 0x8000, use
  ticks+1 as the high word (bit set with a large phase means the reload
  happened in the µs between latch and IRR read — keep ticks).
  elapsed = now − [sch_pit_last] (32-bit sub), store now back, add elapsed
  into `sch_cycles` + 4·[sch_cur] (add/adc). Both call sites run before
  sch_switch changes sch_cur, so the slice lands on the task that just ran.
  The timestamp scheme is exact for intervals of any length — a full-tick
  slice with no yield spans exactly one reload, which a bare mod-65536
  count delta would alias to ~0.
- `sch_cycles` — MAX_TASKS dwords in .bss (lo word first): per-task CPU
  time in PIT cycles (1.19318 MHz), wrapping mod 2^32. Readers snapshot
  under `pushf`/`cli` … `popf` (the ISR writes them). `sch_pit_last`
  (dword) is module-internal. `task_spawn` zeroes a slot's counter before
  publishing it, so a reused slot never inherits a dead task's total; the
  Task Manager additionally forces a freshly appeared slot's first
  interval diff to 0 (`tm_pstate`, §28).
- **Charging a stretch of work to something other than a task.** Window
  callbacks run on whichever task drives the repaint or the event — nearly
  always the UI task, which owns no instance — so a task-granular counter
  alone would report every task-less app (About, Disk, and every
  loaded package that owns no worker task, §20.6) at 0% forever. A
  package that *has* taken a worker is billed on both counters — its
  callbacks here, its worker in `sch_cycles` — and §28's fold rule sums
  them onto one row; `task_debit` **moves** the cycles rather than copying
  them, so the two remain disjoint. Two
  public routines bracket such a stretch; both preserve every register
  except their outputs, are callable at any IF, and must sit **outside**
  the sch_isr → sch_switch → sch_resume fall-through run (same placement
  rule as sch_account):
  - `task_cycles` — out DX:AX = `sch_cycles[sch_cur]` with the pending
    slice folded in (`pushf`/`cli`, sch_account, read, `popf`).
  - `task_debit` — in DX:AX = a stamp from `task_cycles` **taken on this
    task**; out DX:AX = the cycles elapsed since it, *subtracted* from
    `sch_cycles[sch_cur]` in the same cli window. Move, not copy: the
    caller adds the amount to another counter, so the two still partition
    one total. Nesting works unaided — an inner debit lowers the outer
    pair's end reading by exactly the inner amount. **A negative
    difference is forced to 0 and nothing is subtracted**: sch_account can
    return a slightly backwards timestamp when its pending-IRQ
    disambiguation lands on the wrong side of a reload, and an unclamped
    borrow wraps the caller's counter to just under 2^32.
  Both readings come from one sch_cycles slot, which only advances while
  that task is on the CPU, so the difference is real CPU time and not
  wall-clock across a pre-emption. `inst_charge` (§29.4) is the only
  caller; it bills window callbacks to `I_CYC`.

### 8.2 Scheduler modes — pre-emptive and cooperative

Two modes, selected at run time from the Control Panel (§31), both
round-robin over the same task table:

- **Pre-emptive** (boot default, and the only behavior before this
  section existed): every tick on which `sch_lock` is clear switches.
- **Cooperative**: the timer ISR normally declines to switch, so the
  running task keeps the CPU until it reaches the switch path itself —
  `task_yield`, `task_sleep` or `task_exit`. It is *cooperative with a
  watchdog*: a task that has held the CPU for `SCH_WD_TICKS` ticks without
  ever reaching that path is switched away anyway, and the forced switch is
  counted. The watchdog is an internal safety net, **never a user-visible
  concept**: the only names for the two modes in any string the user can
  see are exactly "Pre-emptive" and "Cooperative" (§31).

```nasm
SCH_WD_TICKS equ 18       ; ~1s at 18.2065 Hz: forced switch after this many
                          ; held ticks in cooperative mode

; .bss (sched_init stores 0 to all three — nothing clears .bss at boot, §8)
sch_coop     resb 1       ; 0 = pre-emptive, 1 = cooperative
sch_hold     resb 1       ; ticks the running task has held the CPU since the
                          ; last entry to sch_switch
sch_wd_hits  resw 1       ; diagnostic count of watchdog-forced switches;
                          ; wraps mod 65536, never reset after init, read
                          ; only by a debugger (QMP `xp` on segment 0x0800)
```

**sch_isr decision order (binding).** The mode check is the *last* thing in
the ISR, and the order of everything ahead of it is load-bearing:
`inc word [ticks]` → `sch_account` (§8.1) → `snd_tick` (§34, Phase 1) →
the sleeper-wake scan → `sch_lock` → the mode check. So cycle accounting,
the sound layer's tick duties and `task_sleep` deadlines
keep working on **every** tick in **both** modes (a cooperative task that
never yields still lets sleepers become ready — they just do not run until
it yields or the watchdog fires), and because `sch_lock` is tested first, a
floppy read (§18 holds `sch_lock` across int 13h) accumulates no hold ticks
and can never trip the watchdog. The tail of the ISR is exactly:

```nasm
    cmp byte [sch_lock], 0      ; locked: count ticks but do not switch
    jne sch_resume
    cmp byte [sch_coop], 0
    je .sw                      ; pre-emptive: switch every tick
    inc byte [sch_hold]         ; cooperative: only the watchdog switches
    cmp byte [sch_hold], SCH_WD_TICKS
    jb sch_resume
    inc word [sch_wd_hits]
.sw:
    ; fall through into sch_switch
```

The `sch_isr` → `sch_switch` → `sch_resume` fall-through run stays intact:
no routine may be inserted anywhere inside it (§8 `task_exit`).

**`snd_tick` (§34, Phase 1) is a called leaf, not an insertion in the
run**: `call sch_account` is followed immediately by `call snd_tick`, so
it runs on every tick, in both modes, even while floppy reads hold
`sch_lock`. Contract: entered at IF=0 with DS = KERNEL_SEG already loaded
and the full frame saved (AX/CX/DX free); never touches the switch path;
never EOIs. Its first instruction tests `[snd_live]` (initialised `.text`
data, §34.7) and returns while it is clear — int 08h is hooked seconds
before `snd_init` runs and nothing clears `.bss` at boot (§8). Idle cost
once live is ~30 cycles; the worst case is a tone expiry, which is four
`out`s to silence the speaker.

**`sch_hold` is reset on every entry to `sch_switch`** (`mov byte
[sch_hold], 0`, at the top of the routine). `sch_switch` is entered three
ways — fall-through from `sch_isr`, the explicit `jmp` from `task_yield`'s
save path, and the explicit `jmp` from `task_exit` — and all three reset
it, because the counter's meaning is *ticks since the running task last
reached the switch path*, not ticks since the last successful task change.
Consequences that are relied upon: a task that yields voluntarily can never
trip the watchdog no matter how long it runs in total; the
"nothing ready, resume the outgoing task" fallback inside `sch_switch`
clears the counter too, so an idle single-task system does not manufacture
watchdog hits; and a task that dies clears it for its successor.

| symbol | contract |
|--------|----------|
| `sched_mode_set` | in AL = 0 pre-emptive, any non-zero = cooperative. Out: nothing; **preserves every register**, clobbers flags only. Under `pushf`/`cli` … `popf`: normalize AL to 0/1 into `[sch_coop]` and store 0 to `[sch_hold]` (the incoming mode starts with a full budget). Callable from any task at any time. |
| `sched_mode_get` | out AL = `[sch_coop]` (0 or 1); preserves every other register, clobbers flags only. Readers that only display the mode (§14 About, §28 Task Manager, §31) call this — nobody reads `sch_coop` directly. |

**PLACEMENT (safety-critical).** Both routines must sit **outside** the
`sch_isr` → `sch_switch` → `sch_resume` fall-through run — put them right
after `task_debit` and before `sch_isr`. A routine dropped between `sch_isr`
and `sch_switch` sends every unlocked tick through its `ret` with no return
address on the stack: an instant boot-killer. `task_exit`'s explicit `jmp`
into `sch_switch` is the only sanctioned way to reach the run from outside.

**Why flipping the mode needs no rendezvous.** The two modes share the task
table, the per-task stacks and the 24-byte saved-frame layout exactly; a
mode change adds no state, removes no state and invalidates no saved frame.
The whole switch is one atomic byte store (§29.2's publish-last precedent:
a byte store cannot be torn by an interrupt on the 8086), so the worst case
is that the tick which races the store uses the old mode — one tick's
scheduling decision, self-correcting. No task has to be parked, drained or
notified, which is why `cp_onclick` (§31) may call `sched_mode_set` from
inside a window callback while it holds the gfx lock.

**Why `sch_lock` is deliberately NOT reused for this.** `sch_lock` would
look like a ready-made "don't switch" flag, but `task_yield` checks it too
and self-resumes when it is set (§8), so raising `sch_lock` does not make
multitasking cooperative — it stops multitasking altogether, freezing every
Clock and Bounce instance forever, since their `task_sleep` would never
hand the CPU to anyone else. Cooperative mode must leave the *voluntary*
path fully working and suppress only the *involuntary* one, which is
exactly why the mode is a separate byte tested after `sch_lock`.

**Liveness (why cooperative mode is safe for the kernel, and what packages
owe it).** Every wait loop the kernel owns already yields: `ui_task`'s idle
pass (§13 step 4), `ui_drag`'s track loop and its linger loop (§13),
`menu_track`'s poll loop (§12), the `gfx_lock` spin (§7), `tm_task`'s
measurement spin (§28), and Clock/Bounce through `task_sleep` (§14). No
built-in path depends on being pre-empted, and a worker task is invisible
to every decision the tick makes — `sch_isr` tests `sch_lock`, `sch_coop`
and `sch_hold` and reads nothing instance-shaped, so the mode flip needs no
rendezvous with a package and cooperative mode needs no code of its own for
this.

The watchdog exists for **loaded packages** (§20–21), which are under no
obligation to yield and cannot be escaped from by keyboard — keys are
polled with int 16h from the UI task (§13 step 1) and there is no keyboard
ISR. Since §20.6 a package has two shapes, and the watchdog covers only one
of them completely:

- **A runaway window callback** (`W_PAINT`/`W_ONKEY`/`W_ONCLICK`,
  `AM_ONCMD`) runs on the UI task under the gfx lock. In a switch-free
  cooperative mode it would hard-hang the machine; with the watchdog it
  merely makes the machine slow, and the Task Manager keeps updating.
  Unchanged.
- **A runaway worker task** (§20.6) is the new case. A worker that spins
  *without* the lock is exactly the callback case — slow, not hung, in
  either mode. A worker that spins *while holding the gfx lock* wedges
  every other task, the UI task included, on `gfx_lock` — **in pre-emptive
  mode too**. The watchdog switches the CPU away correctly; there is simply
  nobody to give it to who does not need the lock. That is why worker rule
  3 ("never hold the gfx lock across a long computation") is binding and
  not advisory: it is the only thing between a package and a livelock no
  scheduler mode can break. `gfx_lock` is a byte with no owner field, so
  there is no cheap kernel defence — the same reason a `W_PAINT` proc has
  always been forbidden to take the lock.

A worker that never calls `OSAPI_TASK_ALIVE` fails a different liveness
property altogether: `app_close_win` takes its task-owned path and sets a
die flag nobody reads, so the window hides, the instance record never
frees, and the package's region leaks for the rest of the session (§20.6
rule 2, §29.2 rule 5). In cooperative mode a worker's teardown latency also
grows, because `inst_task_die`'s `gfx_lock` may wait a watchdog period
behind a non-yielding UI-side callback; the window is already hidden by
then, so this is invisible.

## 9. mouse.inc — serial Microsoft mouse + cursor

- COM1 base 0x3F8, IRQ4 → int 0x0C. `mouse_init`: save old vector, install
  ISR, program UART: 1200 baud (DLAB, divisor 96), LCR = 0x02 (7N1),
  MCR = 0x0B (DTR|RTS|OUT2), IER = 0x01 (RX interrupt), read RX/LSR/IIR once
  to flush, unmask IRQ4 in the 8259 (clear bit 4 of port 0x21).
- Microsoft protocol, 3-byte packets, 7 data bits. Byte 0 has bit 6 set:
  `1 LB RB Y7 Y6 X7 X6`; bytes 1/2 have bit 6 clear: low 6 bits of X, Y.
  dx = sign-extended {X7X6,X5..X0}, dy likewise (positive = down).
- ISR: save all registers used + DS/ES, load DS=KERNEL_SEG, then: read port
  0x3F8, assemble packet (resync: any byte with bit 6 set restarts the
  packet), update `mouse_x` (clamp 0..`[vid_wm1]`), `mouse_y`
  (0..`[vid_hm1]`; the live screen, §39.2), `mouse_btn`
  (bit 0 = left, bit 1 = right). On button *change*, push an event (§10):
  EVT_MDOWN / EVT_MUP with a=x, b=y, c=[ticks] — the click's birth time;
  double-click
  detection compares birth ticks, never processing time, so clicks queued
  behind a slow disk mount cannot collapse into a double-click.
  **The right button queues EVT_RDOWN on its PRESS edge only** — same
  record shape, same birth tick — and nothing at all on release: the
  context menu it opens ends on a *level* poll of `mouse_btn` bit 1,
  exactly as `menu_track` ends on bit 0 (§12.4), so a queued release
  would be a record nobody pops. **The right test sits on the fall-through
  of the left test**, which is binding: a packet that reports both buttons
  changing queues the LEFT event only, so a chord can never steal a left
  press from the drag loop or the double-click detector. One event per
  packet keeps the ISR's IF=0 window the length it already was.
  That rule covers one packet. A right-press chorded onto an **already
  running** left tracking loop is a different case and is deliberately
  allowed: `menu_track` and `ui_drag` poll `mouse_btn` directly and never
  drain the queue, so the `EVT_RDOWN` survives the loop and `ui_rdown`
  acts on it afterwards. The visible effect is that a right-press during a
  pull-down selects the row it was over once the menu closes — defensible,
  since the user really did right-press that row — and the popup it opens
  is drawn and restored within one poll iteration because `menu_drop`
  flushes its body before its first level test. The save-under restores
  correctly either way; if that one-frame flash is ever judged visible,
  the fix is an early `test byte [mouse_btn], 2` / `jz` in `ui_rdown`.
  Move the cursor per §7 (draw only when
  `gfx_lock_flag` is clear AND `cur_level` >= 0; otherwise just update
  position and set `cur_dirty`). Send EOI (AL=0x20 → port 0x20) — the BIOS
  does not handle IRQ4 for us. `cld` before any string op; never `sti`.
- `mouse_unhook` — restore int 0x0C vector, mask IRQ4 again, IER=0.
- Cursor: classic Mac arrow, 11 px tall, hot spot (0,0) — black body,
  1px white outline. Two 16-row × 16-bit tables: `cur_and` (white outline
  mask) and `cur_data` (black body). Draw on VGA: white pass = Set/Reset
  white, Bit Mask = mask row bits; black pass likewise. On a 1bpp adapter
  (§39) the same two passes go in with plain CPU OR/AND — no Set/Reset, no
  Bit Mask, no ports. Save-under buffer in .bss:
  3 bytes wide × 16 rows × 4 planes = 192 bytes. The cursor is **always
  VRAM-direct**, double buffering or not: save/restore go through
  `vga_save_vram`/`vga_restore_vram` (§5, §32), never the dispatching
  `gfx_save`/`gfx_restore` — the back buffer must never contain cursor
  pixels, and the ISR must never touch the back buffer.
- `cursor_hide` / `cursor_show`: refcounted (`cur_level`, hidden when < 0
  like classic HideCursor/ShowCursor; init state hidden). Must be callable
  with interrupts on; guard their critical sections with cli/sti so the ISR
  never sees a half-drawn state.
- Public: `mouse_init`, `mouse_unhook`, `cursor_show`, `cursor_hide`,
  `mouse_x` (word), `mouse_y` (word), `mouse_btn` (byte).

## 10. events.inc

Event record, 8 bytes: `EV_TYPE` dw, `EV_A` dw, `EV_B` dw, `EV_C` dw.

```nasm
EVT_NONE  equ 0
EVT_MDOWN equ 1     ; a=x, b=y, c=birth tick
EVT_MUP   equ 2     ; a=x, b=y, c=birth tick
EVT_RDOWN equ 3     ; a=x, b=y, c=birth tick - RIGHT button press (§9/§12.4)
```

There is deliberately no `EVT_RUP`: the only consumer of the right button
is the context-menu tracker, which polls `mouse_btn` bit 1 for its whole
life (§12.4). `EV_C` is carried on `EVT_RDOWN` for record symmetry and is
read by nobody — a right double-click would need no format change.

Single system queue, 16 records, ring buffer in .bss. Producers may be ISRs:
`evq_push` (SI → record; copies 8 bytes; guards the copy + index update with
`pushf`/`cli` … `popf` so the caller's IF is preserved — it is called from
the mouse ISR, which must keep IF=0 throughout (§7); never a bare `sti`;
drops silently when full) and `evq_pop` (DI → destination; CF=1 if empty;
same `pushf`/`cli` … `popf` guard).
Keyboard events are *not* queued — the UI task polls BIOS int 16h directly.

## 11. wm.inc — windows

Window record, 26 bytes, fixed offsets:

```nasm
W_FLAGS equ 0    ; word: bit0 = used, bit1 = visible, bit2 = resizable
                 ; (WF_SIZABLE, §11.1), bit3 = fullscreen (WF_FULL, §11.2)
W_X     equ 2    ; word, frame left (screen coords)
W_Y     equ 4    ; word, frame top (below menu bar: y >= MBAR_H - except
                 ; the one window holding WF_FULL, whose frame is the
                 ; whole screen, §11.2)
W_W     equ 6    ; word, frame width  (outer, includes 1px border)
W_H     equ 8    ; word, frame height (includes title bar)
W_TITLE equ 10   ; word: near ptr to NUL title string
W_PAINT equ 12   ; word: near ptr, in: SI = window ptr. Draws CONTENT only.
W_ONKEY equ 14   ; word: near ptr or 0, in: AL=ascii, AH=scan, SI=win ptr
W_ONCLICK equ 16 ; word: near ptr or 0, in: CX=x, DX=y (absolute screen
                 ; coords), SI=win ptr. Called under the gfx lock when
                 ; EVT_MDOWN lands in the CONTENT of the FRONT window (§13).
                 ; Same rules as W_PAINT: must not lock and must not
                 ; block. It MAY call OSAPI_TASK_SPAWN (§20.6) - the one
                 ; sanctioned spawn a callback can make; nothing else in
                 ; the tree may spawn from a callback.
W_MENUS equ 18   ; word: near ptr to the window's app menu set (§12.2), or
                 ; 0 = no menus of its own. Zeroed by wm_create (it is NOT
                 ; a template word); set by a built-in's KD_INIT or by a
                 ; package through `OSAPI_MENU_SET` (§20.3). Whichever
                 ; window is frontmost owns the menu bar (§12).
W_DISP  equ 20   ; dword: the far pointer `wm_pkgcall` calls to reach any of
W_SEG   equ 22   ; this window's procs - {PKG_DISP, the owning package's
                 ; segment}. W_SEG doubles as THE SEGMENT every near pointer
                 ; in this record belongs to: title, paint, onkey, onclick,
                 ; menus and onsize. Zero means a KERNEL window and makes the
                 ; dispatch an ordinary near call (§20.1). Set by wm_create
                 ; from the segment the CALLER called it in.
W_ONSIZE equ 24  ; word: near ptr or 0 - the resize negotiator (§11.1).
                 ; Called BEFORE a new size is committed, with SI = window,
                 ; CX/DX = the proposed frame size; answers in CX/DX with the
                 ; size it will accept. NOT a template word: wm_create zeroes
                 ; it, `wm_onsize` (API slot 0x0220) sets it.
WIN_SIZE equ 26
MAX_WIN  equ 12

WF_SIZABLE equ 4  ; W_FLAGS bit2: the window can be resized (§11.1)
WF_FULL    equ 8  ; W_FLAGS bit3: the window is fullscreen (§11.2)
WMIN_W     equ 96 ; smallest frame a resize can leave, outer px (§11.1)
WMIN_H     equ 64
```

(WIN_SIZE grew 16 → 18 → 20 → 24 → 26: never a shift idiom, always a true
multiply or `div cl`. The wm_create template stays **16 bytes**:
{x,y,w,h,title,paint,onkey,onclick} words — everything added since is set by
the kernel or by an explicit call, never by the template, so every shipped
.o88's 16-byte template stays valid. That is the rule the last three
additions were designed around: WF_SIZABLE is OR-ed in after wm_create
(KD_WFLAG for built-ins, §29.3; `wm_sizable` for packages, §20.3), W_MENUS
comes from `OSAPI_MENU_SET`, W_ONSIZE from `OSAPI_WM_ONSIZE`, and W_DISP/W_SEG
from wm_create itself. MAX_WIN grew 8 → 12 for instancing (§29);
`apps/os88api.inc` mirrors it. **W_W/W_H are no longer set-once**: `wm_create`
clamps them through `wm_fit` (§39.7), and `ui_grow` (§13), `wm_resize`
(§11.1) and `wm_fullscreen` (§11.2) rewrite them at runtime, so a window's
W_PAINT/W_ONCLICK must derive their layout from the live record every call —
never from constants that bake in the template size.)

**The record lives in KERNEL_SEG.** For a kernel window that is also DS and
nothing needs saying; for a package it is not, so every callback is entered
with **ES = KERNEL_SEG** and the package reads and writes the record through
an `es:` override (§20.1). Without it the package reads its own image at that
offset, which assembles cleanly and runs wrong.

`wm_pkgcall` (SI = window, BP = the callback's offset) is the single dispatch
point for all six near pointers. W_SEG = 0 → `call bp`. Otherwise DS becomes
the package's segment, ES is pointed at KERNEL_SEG, and the call goes far to
`[W_DISP]` — the three-byte `call bp / retf` DISPATCHER at PKG_DISP inside the
package's own header (§20.2). That indirection is what keeps every package
callback an ordinary near proc with an ordinary near `ret`: a package author
never writes `retf`, so a missing one cannot exist. It is re-entrant by
construction because the far pointer is read out of the record rather than a
global — a package's paint proc may call `OSAPI_WM_SHOW`, which repaints,
which dispatches another package's paint proc.

Storage: `wm_wins` (MAX_WIN × WIN_SIZE, .bss), z-order byte array
`wm_zord` (window indices, index 0 = backmost) + `wm_zn` count. Those
three must stay contiguous and in that order — wm_init zeroes them as one
`rep stosb` run. After them sits `wm_owner` (MAX_WIN bytes, **not** part
of the zero run): the instance index (§29) owning each window slot,
0xFF = unowned; wm_init fills it with 0xFF separately. Writers:
`inst_bind_win` at instance creation, `wm_destroy` (resets the destroyed
slot's entry to 0xFF). Every used window is always present in `wm_zord` —
membership is established at wm_create and removed only by `wm_destroy`;
`wm_hide` clears only the visible bit, and `wm_show`/`wm_front` only
reorder. The visible flag alone decides whether a window is painted,
hit-tested, or counted by `wm_top`/`wm_obscured`.

**Callback billing (binding).** All three dispatch sites — `wm_draw_win`'s
W_PAINT call, and ui.inc's W_ONKEY / W_ONCLICK calls (§13) — bracket the
near-call with `inst_win_owner` → `task_cycles` → callback →
`inst_charge` (§8.1/§29.4), so the work lands on the window's instance
instead of on the task that happened to drive it. An unowned window
(record ptr 0) skips the charge. The stamp lives on the stack across the
callback, never in a static, so nested paints (a W_ONCLICK that repaints
every window) bill correctly. The callbacks' own register contracts below
are unchanged — the sites restore AX/CX/DX/SI before calling.

Frame drawing (paint-all does this before calling W_PAINT):
- 1px black outline around the whole frame; 1px black drop shadow along the
  right and bottom edges, offset (+1,+1), Mac-style.
- Title bar: rows W_Y+1 .. W_Y+TITLE_H-2 (16 rows), white. If the window is
  frontmost: 6 horizontal black pinstripes inset 3px from each side, and a
  close box — 11×11 white square with black frame at x = W_X+8, occupying
  rows W_Y+4 .. W_Y+14, pinstripes broken 2px around it — and a
  **minimize box** mirrored on the right: 11×11 white square with black
  frame, cols W_X+W_W−19 .. W_X+W_W−9, same rows, stripes broken by a
  white fill cols W_X+W_W−21 .. W_X+W_W−7, rows W_Y+2 .. W_Y+16, with a
  black hline across row W_Y+9, cols W_X+W_W−17 .. W_X+W_W−11 (the
  "collapse" glyph, so it does not read as a second close box). Windows
  are assumed ≥ 48px wide. Title text centered, black, on a white gap 6px
  each side of the text (so stripes don't touch it).
- 1px black separator line at row W_Y+TITLE_H-1. Content area =
  x+1 .. x+w-2, y+TITLE_H .. y+h-2, filled white before W_PAINT is called.
- **Grow box** (frontmost + WF_SIZABLE only, and never when WF_FULL is
  set): drawn **after** W_PAINT returns, over the content's bottom-right
  corner — a 13×13 white square at cols W_X+W_W−14 .. W_X+W_W−2, rows
  W_Y+W_H−14 .. W_Y+W_H−2, 1px black frame, containing the classic
  two-overlapping-squares glyph: an 8×8 black frame at inset (+4,+4) ..
  (+11,+11), then a 7×7 white fill at inset (+2,+2) .. (+8,+8) with a
  black frame on it — the small square overlaps the big one's top-left.
  Drawing after W_PAINT is what keeps it visible without asking every
  paint proc to avoid the corner.

| symbol         | contract                                                     |
|----------------|--------------------------------------------------------------|
| `wm_init`      | zero table                                                   |
| `wm_create`    | in SI → 16-byte template {x,y,w,h,title,paint,onkey,onclick} words; out BX = window ptr, CF on table full. Calls `wm_fit`, which clamps the frame onto the live screen (§39.7) — every template in the tree is authored for 640x480, so the record, not the template, is the truth. Created **hidden**; appends the window's index to `wm_zord` (frontmost) and increments `wm_zn`. Does not repaint — callable without the gfx lock. |
| `wm_destroy`   | in BX = win ptr: clear W_FLAGS (used+visible), reset the slot's `wm_owner` entry to 0xFF, remove its index from `wm_zord` (compact the array, decrement `wm_zn`), then `wm_paint_dmg` over the rect it vacated — an **empty** rect when it was already hidden, which costs only the chrome (§11.91). Caller holds the gfx lock. The record slot becomes reusable by wm_create. |
| `wm_show`      | in BX = win ptr: set visible, bring to front, draw **just it** plus the chrome and the outgoing front's title bar (§11.90) |
| `wm_hide`      | in BX = win ptr: clear visible, then `wm_paint_dmg` over the rect it vacated (§11.91) |
| `wm_front`     | in BX = win ptr: raise to front of z-order and repaint what that changed — nothing at all if it was already frontmost, one title bar if nothing was covering it, otherwise the window (§11.90) |
| `wm_paint_dmg` | in AX,BX,CX,DX = an inclusive damage rect (may hang off the screen; empty = chrome only): repaint the desktop, the drive zones, the chrome and every window the rect reaches, plus every window those reach. Caller holds the gfx lock; all registers preserved. §11.91. |
| `wm_paint_chrome` | the dock and the menu bar and nothing else, for a change that revealed no pixels. Declines to `wm_paint_dmg` over the dock strip when a window hangs over it. Caller holds the gfx lock. |
| `wm_covered`   | in BX = win ptr; out CF=1 = every pixel of its **frame** rect (drop shadow included) is covered by visible windows above it, so a back-to-front painter may skip it entirely — **W_PAINT included**. Overflow of the 16-rect list answers "not covered". Leaves the clip list disarmed. Caller holds the gfx lock. §11.91. |
| `wm_win_rect`  | in SI = win ptr; out AX,BX,CX,DX = its occupied rect, inclusive, drop shadow included (WF_FULL: no shadow). Clobbers only those four. |
| `wm_top`       | out BX = frontmost visible window ptr, 0 if none. Takes no lock, touches no VRAM and reads only `wm_zord`, so a **worker task** may call it — which is what API slot 0x0260 (§20.3) is for: a package is told when it *gains* the front (`W_ONCLICK`, `W_PAINT`) and never when it loses it, and `W_FLAGS` bit 1 answers *visible*, which a covered window still is. apps/arkanoid pauses on it (§44.8). |
| `wm_hit`       | in CX=x, DX=y; out BX = topmost visible window ptr containing the point (0 if none), AL = 0 content, 1 title bar, 2 close box, 3 minimize box, 4 grow box. AL=2/AL=3 only when BX is the frontmost visible window (the only one with the boxes drawn); on any other window those regions report AL=1. AL=4 only when BX is the frontmost visible window **and** has WF_SIZABLE (and not WF_FULL): the 13×13 grow-box rect of the frame drawing above; anywhere else that region is plain content (AL=0). A WF_FULL window reports AL=0 for every point — it has no chrome. |
| `wm_paint_all` | full repaint: desktop gray (below menu bar), then `desk_paint` (§26 — desktop icons sit on the desktop, under every window), then `dock_paint` (§30 — the dock strip sits on the desktop under every window, like the icons), menu bar, every visible window back→front (frame + white content + W_PAINT) — **except** one `wm_covered` answers yes about, which is skipped whole (§11.91). Caller holds gfx lock. |
| `wm_content`   | in BX = win ptr; out AX = content left, DX = content top. WF_FULL set → AX = W_X, DX = W_Y (no border, no title bar — §11.2). |
| `wm_sizable`   | in BX = win ptr, AL = 0 clear / non-zero set WF_SIZABLE. No repaint (the grow box appears at the next paint). UI-task context only (entry procs and window callbacks qualify); safe with or without the gfx lock there — every W_FLAGS writer runs on the UI task or under the lock. API slot 0x0108 (§20.3). |
| `wm_grow_paint`| in BX = win ptr (caller holds the gfx lock): draw the grow box **iff** BX is the frontmost visible window with WF_SIZABLE set and WF_FULL clear; a no-op otherwise, so it is always safe to call. wm_draw_win uses it after W_PAINT, and a resizable window's **self-initiated content repaint must end with it** — the white-fill idiom (§22) erases the corner, and without the call the box vanishes until the next full repaint while wm_hit still reports AL=4 there. Packages reach it through API slot 0x0118 (§20.3). |
| `wm_fullscreen`| in AL = 1 enter (BX = win ptr) / AL = 0 exit; **caller holds the gfx lock** (the intended callers are W_ONKEY/W_ONCLICK handlers, which already do). See §11.2. Out CF=1 refused (enter while another window owns the screen), CF=0 done. API slot 0x0110 (§20.3). |
| `wm_ptr2idx`   | in BX = win ptr (record-aligned); out AL = window index, AH = 0. Clobbers nothing else. The one public home of the `(ptr − wm_wins) / WIN_SIZE` idiom. |
| `wm_obscured`  | in BX = win ptr; out CF=1 if any visible window above BX in z-order overlaps its frame rect. Result is only trustworthy while the caller holds the gfx lock — the UI task mutates `wm_zord`/window rects under it. Kept, but **no longer the right answer for a background painter**: it vetoes a whole frame for one covered pixel. Use `wm_clip_set` (§11.3). |
| `wm_clip_set`  | in BX = win ptr; **caller holds the gfx lock**. Builds BX's visible region — its content rect less every visible window above it in `wm_zord`, drop shadows included — into the clip list, and arms clipping. out CF=1 the window is entirely invisible: nothing is armed, draw nothing this frame (also the answer when the region needs more than 16 rects). CF=0 armed. Preserves every register. The region is valid only until the next `gfx_unlock`, which clears it (§11.3). API slot 0x0170 (§20.3). |
| `wm_clip_clear`| disarm clipping. Preserves every register. `gfx_unlock` already does this, so a painter only needs it to go back to drawing unclipped inside the same lock hold. API slot 0x0178 (§20.3). |
| `wm_clip_test` | in AX = x1, BX = y1, CX = x2, DX = y2 (inclusive); out CF=0 the whole rect lies inside **one** clip fragment, or nothing is armed; CF=1 it does not. Preserves every register. This is the question `font_char` and `icon_draw16` ask themselves, exposed so a caller that **erases a rect and then draws glyphs into it** can ask it first — see the granularity rule in §11.3. API slot 0x0180 (§20.3). |

Paint procs and key handlers run on the **UI task** (via wm_paint_all /
dispatch) or on the window's own background task — which, since §20.6, may
be a package's worker as well as a Clock or Bounce task. In all cases the
caller of W_PAINT already holds the gfx lock. **W_PAINT must not lock and
must not block.** `OSAPI_TASK_SPAWN` (§20.6) is legal here and is
idempotent by construction — the second call refuses with CF=1 because the
instance already owns a task — but a paint proc is a poor place for it: the
intended idiom is the first `W_ONCLICK`, an `AM_ONCMD` item, or a `W_PAINT`
guarded by the package's own once-flag.

### 11.1 Resizing

A window is resizable iff W_FLAGS bit2 (`WF_SIZABLE`) is set. The bit is
**not** part of the 16-byte template: built-ins get it from their kind row's
`KD_WFLAG` byte (§29.3, OR-ed into W_FLAGS by app_launch right after
wm_create), packages call `wm_sizable` (API slot 0x0108) from their entry
proc after OSAPI_WM_CREATE. Fixed-layout windows — dialogs, the Control
Panel, Minesweeper — simply never set it and nothing about them changes.

What the bit buys: the frontmost window draws the grow box (frame drawing
above), `wm_hit` reports AL=4 inside it, and ui.inc answers with the
**resize loop** `ui_grow` (§13) — a sibling of the title-bar drag loop with
the identical binding lock/XOR ordering, tracking an outline anchored at
(W_X, W_Y) whose size follows the mouse: cur = orig + (mouse − start),
clamped to at least WMIN_W×WMIN_H while tracking (the XOR rect must stay
well-formed). On release it clamps again — WMIN_W ≤ w ≤ `[vid_w]` − W_X,
WMIN_H ≤ h ≤ `[vid_h]` − W_Y (the frame stays on screen; position never
changes) — then **asks the window** (below), writes W_W/W_H, and repaints
under the still-held lock. **Only the rect it had and the rect it has
changed**, and a grow keeps its ORIGIN fixed, so their union is that origin
plus the larger of the two sizes per axis: `wm_paint_dmg`'s case, not
`wm_paint_all`'s (§11.91). Dragging a corner used to repaint every window on
the screen. The repaint re-enters W_PAINT, and a
resizable window's procs are required to lay out from the live record (record
note above). Self-initiated repaints (the fm_repaint idiom, §22) must
white-fill using the live W_W/W_H for the same reason.

**`wm_resize` (API slot 0x01D0) — an app changing its own size.** In:
BX = window, CX = new outer width, DX = new outer height; the caller holds
the gfx lock. Clamps exactly as a drag does (never below WMIN_W/WMIN_H, never
past the live screen or the dock row, §39.2), re-fits the origin the way
`ui_drag` does so a window that grew at its right edge slides left instead of
hanging off, then repaints everything. This is how an app that adopts a
picture's dimensions moves its own frame; before it existed `ui_grow` and
`wm_fullscreen` were the only things that could, and apps/paint wrote W_W/W_H
in the record itself (docs/PAINT-NOTES.md). **It repaints**, so it must not be
called from inside a W_PAINT — that would re-enter the caller's own paint
proc. From W_ONCLICK, W_ONKEY, a menu handler or a file-dialog completion it
is an ordinary call.

**`W_ONSIZE` — the kernel asking the app.** `wm_ask_size` (BX = window,
CX/DX = the proposed frame size) runs the window's negotiator through
`wm_pkgcall` and takes back whatever CX/DX it returns; a window with no
negotiator leaves them alone, so the feature costs everything else one
compare. `ui_grow` calls it on release, *after* its own clamps and *before*
the record changes — which is the whole point: nothing has been drawn at
either size yet, so a refusal costs no repaint. The answer is a SIZE and not
a yes/no because the case that motivated it is per-axis: apps/paint refuses a
drag that would crop artwork, and a drag that would lose columns but not rows
should still get its rows. Install it with `wm_onsize` (API slot 0x0220,
BX = window, AX = a near proc in the window's own segment, 0 clears).
The negotiator runs under the gfx lock and **must not draw** — it decides,
returns, and draws in the W_PAINT that immediately follows.
`wm_fullscreen` (§11.2) does NOT consult it: it has to be able to put the old
size back on the way out, and a refusal in the middle of that has no meaning.

`ui_drag`'s release clamp is unchanged (x + w ≤ `[vid_w]` — the live screen
width, §39.2 — with the live window width), so a grown window still cannot be dragged off screen.

### 11.2 Fullscreen

The SDK/kernel foundation for apps that want the whole screen (640×480 on
VGA, §39): a
fullscreen surface **is a real window** — that one decision buys almost
everything, because wm_obscured (which gates every unbidden background
drawer: Clock, Bounce, the Task Manager sampler) sees a frame covering the
entire screen and reports "covered" to everyone beneath it.

`wm_fullscreen` (API slot 0x0110; caller holds the gfx lock):

- **Enter** (AL=1, BX = win ptr): another window already owns the screen
  (`[wm_fs]` non-zero and ≠ BX) → CF=1, nothing changes. Else save
  W_X/W_Y/W_W/W_H into `wm_fs_save` (4 words, .bss), store BX in `wm_fs`
  (word, .bss, 0 = none — **the** fullscreen latch), set the frame to
  (0, 0, `[vid_w]`, `[vid_h]`), set WF_FULL, `wm_front` (raises + repaints
  under the held lock). Re-entering with the same window is CF=0 no-op.
- **Exit** (AL=0): `[wm_fs]` zero → CF=0 no-op. Else restore the saved
  geometry into the record, clear WF_FULL, zero `wm_fs`, `wm_paint_all`.

While WF_FULL is set: `wm_draw_win` draws **no chrome at all** — no frame,
shadow, title bar, boxes or grow box; the content area is the whole frame
rect, white-filled, then W_PAINT as usual. `wm_content` returns (W_X, W_Y).
`wm_hit` reports every point as content, so clicks flow to W_ONCLICK and
keys to W_ONKEY exactly as in a window (the app needs no second input
model). `wm_paint_all` skips the desktop fill, desk_paint, dock_paint and
menu_draw_bar whenever `[wm_fs]` names a **visible** window — painting
chrome under an opaque surface is pure waste and the §12.1 "nothing covers
the bar" assumption is suspended — and the window loop itself is unchanged.

The two drawers wm_obscured does not govern get explicit `[wm_fs]` gates
(§13): the UI task's unbidden menu-bar clock redraw (step 4) is skipped,
and the menu-bar branch of the event ladder (step 2) is bypassed so a
click in rows 0..19 routes to wm_hit like any other. The mouse cursor
stays live — a fullscreen app that wants it hidden draws its own.

Leaving fullscreen is **the app's job** (recommend Esc in its W_ONKEY →
`wm_fullscreen` AL=0; the menu bar is unreachable while it holds the
screen). The kernel's safety net: `wm_destroy` and `wm_hide` both check
BX against `[wm_fs]` and, on a match, restore the saved geometry, clear
WF_FULL and zero `wm_fs` before repainting — closing, minimizing (no box
is drawn, but app_close_win's die-flag path hides) or killing a
fullscreen window can never strand the latch, and a re-shown window comes
back windowed at its old place.

### 11.3 The clip region — what a background task may draw

Until this section a background painter had one question it could ask —
`wm_obscured` — and one answer it could act on: *anything on top of me at
all, skip the whole frame*. It had to be that blunt, because the `gfx_*`
primitives draw in absolute screen coordinates with no clipping beyond the
screen edge, so a covered window that drew would paint over the window on
top of it. The visible consequence was that a Bounce with one covered pixel
looked frozen, a Clock's digits stopped, and a fractal that takes two
minutes on an XT stopped rendering the moment anything touched its corner.

The region replaces the veto. `wm_clip_set` builds the window's **visible
region** and arms it; while it is armed, the clipped primitives below
draw only inside it.

**Building the region.** Start with the window's content rect — the same
one `wm_draw_win` white-fills before `W_PAINT`; under WF_FULL that is the
whole frame (§11.2). For every visible window later in `wm_zord`, subtract
its occupied rect **including the 1px drop shadow**: (x, y) to (x+w, y+h)
inclusive, exactly the extent `wm_obscured` has always tested against. Each
subtraction replaces one rect with up to four fragments — above, below,
left and right of the occluder.

**Storage.** `wm_clip_tab`, 16 rects of {x1, y1, x2, y2} inclusive (128
bytes of .bss), plus `wm_clip_n` — the live count, and **initialized data in
.text, not .bss**, because every `gfx_fill` reads it including the machine's
first and `-f bin` zeroes nothing. `MAX_WIN` is 12, so at most 11 occluders,
and 16 rects covers every realistic arrangement; past that `wm_clip_set`
answers CF=1 and the frame is skipped, which is exactly what `wm_obscured`
used to do and therefore cannot regress anything.

**Validity.** The region is computed from `wm_zord` and the window rects,
both of which the UI task mutates only under the gfx lock. Within one lock
hold they cannot change, so a region built at `wm_clip_set` is valid until
`gfx_unlock` — and meaningless after. That is the same argument that makes
`wm_obscured` trustworthy, and it is why the region is rebuilt on every
lock hold rather than cached across one. There is deliberately **no**
`wm_obscured` fast path inside `wm_clip_set`: the region build *is*
`wm_obscured`'s z-order walk plus a subtraction that does nothing when
nothing overlaps, so a pre-test would only walk it twice.

**Where the hook goes.** `gfx_pixel`, `gfx_hline`, `gfx_vline` and
`gfx_frame` all funnel into `gfx_fill` (§5) — a pixel is a 1×1 rect, an
hline a 1-row rect, a frame two hlines and two vlines — so the whole
rectangle vocabulary is one choke point. The clipped set is seven:

| entry | how it clips |
|-------|--------------|
| `gfx_fill` | per fragment; covers pixel, hline, vline and frame |
| `gfx_fill_gray` | per fragment (the dither is screen-parity, so fragments align) |
| `gfx_fill_pat` | per fragment (the pattern is screen-aligned for the same reason: the row byte is `y & 7` off a table staged from `[gfx_pat]` on every call, so a fragment starting at any y picks the byte the whole rect would have) |
| `gfx_xor_fill` | per fragment |
| `gfx_xor_rect` | **decomposed first**: an outline is not the intersection of its bounding rect with anything, so it becomes four `gfx_xor_fill` strips (the same decomposition `bb_xor_rect` uses, each pixel touched once, still self-inverting) |
| `font_char` | whole-cell; covers `font_str` |
| `icon_draw16` | whole-icon; covers `icon_draw` and `ico_core` |

`gfx_fill_pat` was missing from this list for as long as the list existed,
and the Task Manager's memory map is drawn almost entirely out of it — every
claim band, the kernel's buffer texture and each package's region pattern —
so a map redrawn behind another window painted its full width straight
across whatever was on top. The kernel's own band stopped at the right
place, `gfx_fill_gray` having the hook, which is what made it read as the
map drawing the wrong SHAPE rather than in the wrong PLACE. A primitive not
in this table is not "unclipped by design"; it is a hole.

**Two are still holes, and they are named here so they are not silent.**
`gfx_blit4` (0x01D8) and `gfx_scroll` (0x01F8) take no hook, because
`GFXCLIP` re-enters a body with a sub-rect and neither can honour one
without also advancing its SOURCE to match — a blit is not a fill. Nothing
in the tree reaches them from a clipped context today: both are package
slots, packages draw from callbacks where nothing is armed, and the one
package that blits (Solitaire, §43) has no worker. A package that arms
`OSAPI_WM_CLIP_SET` from a worker and then blits **will** paint over the
window on top of it.

Each hook sits at the **public entry, above the `[bb_on]` dispatch**, so one
implementation covers the VRAM path, the back-buffer path, VGA and both mono
adapters — on a 1bpp adapter the software renderer *is* the direct path
(§39.5), and a hook below the dispatch would work on VGA and silently do
nothing on Hercules. This is the same reasoning that places `bb_mono_chk`
where it is. Disarmed, the hook is one compare and a taken branch: the
repaint path pays nothing measurable. Armed, `gfx_clip_run` intersects the
primitive's rect with each fragment and re-enters the raw body per surviving
fragment.

`font_char` and `icon_draw16` cannot draw a partial shape, so they take the
whole-cell answer instead: the glyph or icon is drawn iff its cell lies
wholly inside **one** fragment, and skipped otherwise. That is the same
decision both already make at a screen edge (§6), one region in, and half an
8x8 glyph is unreadable anyway.

**The granularity rule (binding, and the sharp edge of this section).** The
fills clip **per pixel** and the glyphs clip **per whole cell**, so the two
do not agree, and a caller that **erases a rect and then draws glyphs into
it** must not let them disagree in the same frame. Ungated, a window cut
horizontally by another window's edge has its visible rows white-filled and
then gets no text back in them: the display does not freeze, it goes *blank*,
and it is re-blanked on every update. Two ways out, both in the tree:

- **Erase per cell.** Ask `wm_clip_test` for the 8x8 cell; if it answers yes,
  fill that cell and draw the glyph; if no, touch neither. The two decisions
  are then the same decision. `app_clk_render` (§14) does this, which is why
  a half-covered Clock shows whole digits on clean white rather than a blank
  band.
- **Gate the whole thing.** When the erased rect is one unit — a status
  strip, a pane — ask `wm_clip_test` for the whole rect and skip both
  operations when it says no, leaving the last good pixels alone.
  `apps/fractal`'s `fr_status` (§40) does this.

Solid-only drawing is unaffected: Bounce erases and redraws with `gfx_fill`
at both ends, so its two operations clip alike by construction.

**Rules (binding).**

1. **`gfx_unlock` clears the clip.** Making the clip state die with the lock
   hold is what stops a leaked region from silently truncating the next
   painter. Nothing relies on callers to clear it.
2. **Transient overlays are never clipped.** The drag outline and the menu
   highlights call `vga_xor_rect_vram` / `vga_xor_fill_vram` direct,
   deliberately bypassing the back buffer (§32). They are drawn by the UI
   task in a different lock hold, and rule 1 is what makes that structural
   rather than a convention.
3. **Clipping is for background tasks only.** `wm_paint_all` draws visible
   windows back to front, so the painter's algorithm resolves overlap for
   free and the repaint path must stay unclipped. A painter that arms a
   region must not call `wm_paint_all` inside the same lock hold.
4. **`wm_obscured` stays.** It is still correct and still the cheaper answer
   for a drawer that repaints its whole content in one go — `cp_tick`'s
   Date/Time rows (§31.5) and `tm_update` (§28) both keep it.

**What changed in the applications.** Bounce (§14) now **always steps** and
only the erase and the redraw are conditional; Clock (§14) substitutes
`wm_clip_set` for its veto; and `apps/fractal`'s `fr_emit_body` (§40) does
the same, so a partly covered fractal keeps rendering the part you can see.

### 11.90 Showing a window costs one window, not one screen

`wm_show` does **not** call `wm_paint_all`. Showing a window is the one
z-order change that **reveals nothing**: the window lands on top, so every
pixel that changes is either its own or chrome it does not cover, and
everything already on screen either stays as it is or is drawn over. The
full back-to-front pass — a whole-screen planar dither, the drive icons,
the dock, the bar, and every visible window's frame and `W_PAINT` — was
being paid to put one window on screen.

Raising an **already visible** window is the same argument — it moves up,
so it can only cover more — so **`wm_front` shares this path**, through
`wm_raise`. `wm_hide` and `wm_destroy` genuinely do uncover something, and
they answer it with a damage rect rather than the whole screen (§11.91).

Four things change when a window comes to the front, and `wm_raise` draws
all four, in this order:

1. **The menu bar** — `menu_activate` has just handed it to this window
   (§12), so `menu_draw_bar`. Safe unclipped: `wm_fit` floors every
   window's y at `MBAR_H`, so no window is ever over the bar.
2. **The dock** — the instance owning this window may be new (§30), so
   `dock_paint`.
3. **The outgoing front window's title bar**, via `wm_draw_title`. The
   pinstripes, the close box and the minimize box belong to the frontmost
   window alone, so the window losing the front needs exactly that rect
   redrawn and nothing else. It is drawn **before** the new window, which
   then covers whatever of it it overlaps. `wm_top` is read **before** the
   visible bit goes on — `wm_create` has already appended the new window to
   `wm_zord`, so once it is visible `wm_top` answers with itself.
4. **The window**, last and therefore on top, drop shadow included — either
   `wm_draw_win` (BP = itself) or, when only its rank changed, just
   `wm_draw_title`. `wm_show` always draws the whole window: a newly
   visible one has no pixels on screen. `wm_front` asks **`wm_obscured`
   before `wm_lift`**, while the z-order still says what was on top: if
   nothing was, then nothing of this window was hidden and the pinstripes
   are the only thing the raise changes. That is the common case, because
   a click on a background window's title bar comes through here.

The desktop and its drive icons are deliberately not redrawn: coming to the
front cannot change them, and `desk_zone_redraw` (§26) already owns the
one thing that can.

**`wm_front` on a window that is already frontmost repaints no window at
all** — not even a title bar. It is not a no-op (`menu_activate`, the bar
and the dock still run, because the caller may be re-asserting ownership),
but nothing under the bar is touched. **`wm_front` on a window that is not
visible** takes the full pass: `wm_show` is the entry point for that, and
declining is better than drawing a hidden window.

**There is no fallback to the full pass left.** A fullscreen window used to
be one (`wm_fast_ok`): `[wm_fs]` set, or the window itself carrying
`WF_FULL`, sent both entry points to `wm_paint_all` on the grounds that
§11.2 suppresses the chrome and the ordering above stops describing the
screen. It does not stop describing it — it gets *shorter*. A window shown
over a fullscreen one reveals nothing, like any other; a fullscreen window
shown over everything reveals nothing because it covers the screen. Only the
chrome had to change, and `wm_raise` now **skips** the bar and the dock
(`wm_fs_vis`) instead of the caller repainting the world in order to avoid
drawing them.

What that cost, before: opening the Standard File dialog over a fullscreen
player repainted the player — its whole `W_PAINT` — to put a small dialog on
top of it, and again to take it away. `wm_fs_vis` is the predicate the four
painters now share; `wm_paint_all` and `wm_paint_dmg` had each open-coded it.

Two things fell out of removing the veto, both of them latent bugs it had
been hiding:

- **`wm_draw_title` had no `WF_FULL` guard.** A fullscreen window has no
  title bar, and `wm_raise` hands the *outgoing* front window one when
  something is raised over it — so the first raise over a fullscreen window
  painted a full-width white strip with a centred caption across the top of
  its content. The guard belongs in `wm_draw_title`: "no title bar" is a
  fact about the window, not about the path that reached it.
- **`wm_paint_all` walks from the fullscreen window, not from the bottom.**
  Everything below a window covering the whole screen is covered by it, by
  construction. `wm_covered` reached the same answer one region-arithmetic
  pass per window later.

`wm_paint_dmg` likewise stops falling back. Under a fullscreen window the
desktop, its drive zones, the dock and the bar are all under it, so the
window pass is the whole of the work — and the empty-rect "the chrome
changed" case has no chrome on screen to change.

**A window hanging over the dock used to be the other, and it was expensive
out of all proportion.** The strip is drawn under windows (§30), so
`dock_paint` would have drawn on top of one; rather than order itself around
that, the cheap path declined and the caller repainted the whole screen. One
oversized window anywhere on screen therefore made *every* focus change, show
and un-minimize a full repaint — `wm_fit` keeps a window above the dock but
`ui_grow`'s clamp is looser, so this is reachable by dragging a corner.

**`wm_dock_under` costs a rectangle instead**, and usually nothing. It asks
two questions and both are normally no: `dock_paint` reports in CF whether it
put any pixels on the strip at all (§30 — most calls change nothing), and
`wm_dock_clear` whether any window is sitting on them. Only when both are yes
does it seed `wm_dmg_x1..y2` with the strip's own rect and call
**`wm_dmg_wins`** — §11.91's mark-and-draw pass, factored out of
`wm_paint_dmg` for exactly this — which redraws the windows the strip touches
and nothing else. `wm_raise`, `wm_front`'s chrome-only path and
`wm_paint_chrome` all reach the dock through it.

**`wm_lift`** is the z-order move split out of `wm_front` so `wm_show` can
reorder without committing to the repaint that used to be welded to it.
**`wm_raise`** is the paint half both entry points share.

**One consumer had to follow.** A file-manager window that posts a load
draws `'Loading...'` in its status line while `[ld_pending]` is set (§22).
The load clears the flag, and with `wm_show` no longer repainting anything
but the window it put up, that line would sit stale. `files_poster`
(§21 step 10) is the correction: `wm_clip_set` on the poster window, then
`fm_status_only`, then `gfx_unlock` — one **line**, clipped to whatever of
it the new window has not covered, which is exactly what §11.3 exists for.
`fm_win_of` is the reverse of `fm_vp_set` it needs, because `[ld_pwin]`
holds the poster's **state block** and not its window. It falls back to
`files_refresh`'s full pass when there is no poster (a package asked for
the load itself, §22.1) or when the poster's window is no longer visible —
and to `fm_repaint` when `fm_status_only` refuses (§22). A pending retitle
does **not** send it to the full pass any more: it spends one
`fm_title_flush` (§11.92) before arming the clip, because `wm_title_set`
does its own region arithmetic on a strip the content rect does not contain.

The **other** end of the same round trip is `fm_onclick`'s double-click:
posting a load turns the status line into `'Loading...'` and changes
nothing else on screen, so it too draws one line rather than the window's
whole content. A double-click on a *folder* still repaints the window's
content — `fm_go` replaced the listing — but no longer its frame, and no
longer the screen (§11.92).

**A window left barely on the dock is nudged back off it.** `wm_dock_under`
makes the overlap affordable; `wm_dock_snap` makes most of it not happen.
`ui_drag` and `ui_grow` call it once, after their own clamps and before the
damage rect is computed: in BX = the window, out CF = 1 if `W_Y` moved. It
moves the window **up** and never anywhere else, and only when *both* gates
open:

- **Less than `DOCK_H`/2 rows of the strip are covered.** `y+h` — the drop
  shadow's own row — against `[vid_dock_y0]`. Half the strip is the line
  between "the drag stopped a little low" and "the user put it there"; past
  it the window stays exactly where it was put and `wm_dock_under` pays.
- **It fits above.** `dock_y0 - 1 - h` must still be at least `MBAR_H`.
  A window taller than the desktop band is left **completely** alone — that
  is the case that must not break, because Paint grown to nearly the whole
  screen is a deliberate, legal size and neither snapping it nor refusing
  the resize would be honest about what the grow box does.

`ui_grow` is the caller that needs care: a snap moves the **origin**, which
nothing else in a resize does, so it banks the old rect's last row *before*
the call and unions against it afterwards.

### 11.91 Hiding, destroying and moving cost a rectangle, not a screen

The mirror of §11.90. Hiding a window, destroying one and dragging one to a
new place all **do** reveal — but only inside the rect the window vacated,
and every pixel outside that rect is already correct on screen.
`wm_paint_dmg` is that argument: in AX/BX/CX/DX, an inclusive damage rect;
out, a screen as correct as `wm_paint_all` would have left it.

```nasm
wm_hide     damage = the window's frame rect, drop shadow included
wm_destroy  same - and an EMPTY rect when the window was already hidden,
            because its pixels went at the wm_hide and only the chrome is
            owed (wm_paint_chrome). That is the second half of closing a
            task-owned app (§29): close box hides, the worker destroys.
ui_drag     damage = union(where it was, where it is) - the two overlap on
            any short drag, and the union is still a fraction of a screen
```

What it draws, in `wm_paint_all`'s order so the layering is identical:

1. the desktop dither, **clipped to the damage rect** and to the band below
   the menu bar;
2. every drive zone the rect touches (`desk_dmg_zones` / `desk_paint_mask`,
   §26), drawn whole;
3. the dock and the menu bar, **always** — both carry state that a hide or a
   destroy has just changed (a tile leaves, the focus cue moves, the bar
   may lose its owner);
4. every window that needs it, back to front.

**"Needs it" is a two-part rule, and the second part is what makes it
safe.** A window is marked if its rect overlaps the damage — *and also* if
it overlaps a window already marked below it, because that window is
redrawn **whole** and would otherwise paint over this one. The marking pass
runs bottom-to-top over `wm_zord`, so one pass reaches the whole transitive
closure. Nothing in that pass may keep a loop counter in a general
register: `wm_win_rect` writes all four.

**A touched drive zone is folded into the damage rect** before the marking
pass rather than special-cased inside it: the zone is drawn whole — gray
fill, icon, label — so a window sitting over it has to be redrawn, and
growing the rect is what makes the marking notice.

**The dock is not folded in, and that asymmetry is load-bearing.** The strip
runs the full width of the screen, so a rect grown to reach it is a rect
grown to full width *for the damage's entire height* — which erased the
drive icons out from under a window that merely reached the bottom of the
screen, and left them erased, because `desk_dmg_zones` had already run
against the smaller rect. The dock is a **per-window test** in the marking
pass instead: the strip is repainted unconditionally and is drawn under
windows, so a window whose rect reaches `[vid_dock_y0]` is marked, and no
other pixel is disturbed.

**A wholly covered window is not drawn at all.** `wm_covered` seeds §11.3's
region arithmetic with the **frame** rect instead of the content rect — a
title bar peeking out is still a pixel this window owns — and subtracts
every visible window above it. Empty means every pixel this window would
write is written again by something later in `wm_zord`, whether that window
is being redrawn in this pass or is merely still on screen, so a
back-to-front painter may skip it. `wm_paint_all` uses it too. Overflow of
the 16-rect list degrades the **opposite** way from `wm_clip_set`: not
covered, i.e. draw it, because skipping on a maybe loses pixels.
`wm_clip_occl` is the shared walk; the two callers differ only in the seed
rect and in what an empty list means.

The consequence a package author can see is that **`W_PAINT` does not run
on a wholly covered window**. A paint proc must therefore be a repaint and
nothing else — anything else it does has to tolerate being skipped. (This
is *not* the old `wm_obscured` veto coming back: a **partly** covered
window is redrawn in full, exactly as before.)

**Promotion.** Hiding or destroying the frontmost window promotes whatever
was under it, and the promotion is visible — the pinstripes and the two
boxes belong to the front window alone (§11). After the marked windows are
drawn, `wm_paint_dmg` asks `wm_top` again; if the answer was **not** redrawn
in this pass, it owes exactly one `wm_draw_title` with DI = BP.

### 11.92 Retitling costs a strip — `wm_title_set`

A caption changes on an **event** — a folder was entered, a document was
opened — and never on a paint. The window that owns it therefore knows what
it wants to be called *after* the frame carrying that caption has already
been drawn, and until this call the only correction available was "ask the
next repaint for more": the file manager kept `[fm_full]`, a flag that
escalated its next `fm_repaint` from the content to the whole frame, and
before that to `wm_paint_all`. Either way, a window's listing, its chrome
and everything overlapping it were redrawn to fix 17 rows.

`wm_title_set` (**API slot 0x0240**) is the direct answer: in BX = window
ptr, AX = the new `W_TITLE` offset — or **0**, meaning the bytes `W_TITLE`
already names changed underneath it, which is the file manager's case
because its caption *is* the instance record's `I_NAME` (§29.1). Caller
holds the gfx lock, so it is a window-callback call like every other drawing
entry point. All registers preserved.

It draws the strip `x .. x+w-1`, `y .. y+TITLE_H-1` and **nothing else**: no
content fill, no `W_PAINT`, no other window, no chrome. The pinstripes and
the two boxes still belong to the frontmost window alone, so it asks
`wm_top` for BP and hands both to `wm_draw_title`, exactly as §11.90 does.

**Three ways out, and the granularity rule (§11.3) is what picks between
them.** The region arithmetic is `wm_covered`'s — `wm_clip_occl` seeded on
the title strip instead of the frame:

- **Nothing above it** (the whole strip lies inside one fragment):
  `wm_draw_title`, clip disarmed. This is the overwhelmingly common case,
  because a window is normally frontmost at the moment it retitles.
- **Wholly covered** (the list came back empty): draw nothing at all.
  Answered *before* `wm_clip_test`, which reads an empty list as "disarmed,
  draw freely".
- **Anything in between**, or a list that overflowed: `wm_paint_dmg` over
  the strip. A caption is a white gap fill with glyphs on top; a fill clips
  per pixel and a glyph per whole 8×8 cell, so a clip edge across the strip
  would erase the text and not put it back — blank, not stale. 17 rows of
  §11.91 is the honest price of that.

A hidden window and a fullscreen one (§11.2, which has no title bar) both
return having drawn nothing, so a caller need not test either.

**The file manager is the reference consumer.** `fm_settitle` writes the 16
bytes and banks the window in `[fm_tdirty]`; `fm_title_flush` spends it, and
only `fm_repaint` and `files_poster` call that — both under the lock. It is
deferred, and it is a **pointer rather than a flag**, because `fm_settitle`'s
callers are a mixed set: `fm_go`, `fm_mount` and `fm_view` hold the lock,
`fm_kinit` runs before the window is ever shown, and `fmv_sync`'s
folder-vanished path reaches it from `ld_run`, which deliberately holds no
lock across a mount (§21). A pointer cannot be spent on whichever window
happened to repaint in between.

### 11.94 `WF_SNAP` — a window that keeps its content on a byte boundary

**Opt-in, mono only, and the whole of it is one `and`.** `wm_snap` (API slot
0x0268) sets `WF_SNAP` on a window, and every site that writes `W_X` then
keeps that window's **content origin** on a multiple of 8 — so every
`font_run` the window makes reaches the single-store fast path (§6.1) instead
of the erase-and-letter fallback. §6.1.1 measures what that is worth: the
operation goes from 228..336 framebuffer byte-accesses to a flat 80, and
§6.1.4 is why there is no other way to get it.

**Content origin, not frame x.** `wm_content` returns `W_X + 1` for the
border, so the number the kernel actually holds is `W_X ≡ 7 (mod 8)`.
Publishing the frame number would make every app rediscover that `inc ax`,
and they would each get it wrong differently.

**The gate is `[vid_mono]`, never `[bb_on]`.** They agree on Hercules and CGA,
but `[bb_on]` is also 1 on a VGA with double buffering armed — so gating on it
would make windows drag differently depending on a Control Panel setting, and
buy almost nothing: the back buffer's flush costs ~24× its render (§32), so
the render-side saving is diluted away there. On VGA the flag is therefore a
no-op, which is what lets an app set it unconditionally instead of asking
`OSAPI_VIDEO` first.

Four sites write `W_X` and all four go through `wm_snap_ax`:

- **`wm_fit`**, after both x clamps. Never before: the snap moves left, which
  is only legal once the right edge has been dealt with.
- **`wm_resize`**, after its x clamp — a width change can move x.
- **`ui_drag`**, before its "did it move" comparison rather than after the
  store, because what *moved* has to mean where the window ends up, and a drag
  inside one 8-pixel step ends up nowhere.
- **`wm_fullscreen`** needs nothing: x is 0 with no border, so the content
  origin is already 0. That is why apps/tracker has had the fast path all
  along without any of this (§45.9).

Three consequences worth naming:

- **A snapped window cannot sit flush against the left edge.** The smallest x
  with `x + 1` a multiple of 8 is 7, so that is as far left as it goes. Seven
  pixels is the visible cost of the feature.
- **The snap moves LEFT**, so it can never violate the caller's right-edge
  clamp — except in the `x = 0..6` case, where the only answer is 7 and the
  right edge is re-tested. A window too wide for that is **left unsnapped**
  rather than made illegal: it simply misses the fast path, which is a normal
  path and not a failure.
- **Dragging becomes 8-pixel steps horizontally**, and that is cheaper than it
  sounds because `ui_drag` drags an XOR *outline* — the window itself only
  moves on release, so the snap reads as the outline stepping rather than a
  window stuttering.

**`wm_snap` preserves FLAGS**, and that is load-bearing rather than polite. A
package's entry proc returns CF to the loader (§20.2) and is exactly where
this gets called — after `wm_create`'s CF has been consumed by a branch, so
the carry riding in the flags at that point *is* the return value.
`wm_snap_ax` exits on a `cmp` against the screen width, which leaves CF set
for every window that fits, so without the `pushf`/`popf` asking to be
snapped **aborted the launch**. On mono only, because that `cmp` is inside the
`[vid_mono]` gate — Note Pad loaded perfectly on VGA and answered "Load
failed" on Hercules, which is the shape every mono-gated bug in this system
has.

**What it is worth, at the workload level.** §6.1.1 prices the primitive;
`apps/typebench` (on the `testing` branch, like fontbench and for the same
reason) prices the *keystroke* — 40 random characters typed into a 40-cell
line, the whole line redrawn after each one, which is what `np_redraw` does to
its dirty band (§27.2). Under `-icount`:

| adapter | CHAR aligned | CHAR at x+5 | one `font_run` |
|---|---|---|---|
| Hercules | 3717 | 3794 | 3480 |
| VGA | 3133 | 3314 | 3137 |

So snapping is **2.1% on Hercules and 5.8% on VGA** in instructions — more on
VGA because four planes make the spilled second byte cost four times — and
drawing the row as a run instead would be a further 6.4% on mono.

**On the real machine both are bigger, and the second one is bigger than its
speed.** The same three rows on a 4.77MHz 8088 with a Hercules card:

| row | ms for 40 keystrokes | per keystroke |
|---|---|---|
| CHAR aligned | 1332 | 33.3 ms |
| CHAR at x+5 | 1370 | 34.3 ms |
| one `font_run` | **1203** | **30.1 ms** |

Snapping is 2.9% and the run conversion a further 10.7%, against 2.1% and
6.4% under `-icount`. But the number that decides it is not in the table:
**the two CHAR rows visibly flicker on that machine and the `font_run` row
does not** (§6.1). A keystroke costs 33 ms there, and the erase-and-letter
pair spends most of it with the line blank. That made Note Pad drawing
character by character a defect rather than a tuning choice, and **it has
been converted** (§27.2) — on the flicker, not on the 10.7%.

**The app's half of the contract is not enforced.** `WF_SNAP` puts the content
origin on a boundary; whether the app's own text sits at content-relative x
values that are multiples of 8 is the app's business, and getting it wrong
costs the 2.5% fallback silently rather than drawing anything wrong. Both converted consumers had to move something, and both moved the same two
pixels: the Task Manager's process list and captions from a 6-pixel inset to
8 (`TM_PEN`), and Note Pad's text margin likewise (`NP_MARGIN`). Two pixels
of margin bought a whole window the single-store path.

**The Disk window was considered and left alone.** Its dominant cost is
`fm_repaint`, which fills the whole content once with `rep stosw` and then
letters ~40 strings into it — and `font_run` per string would repaint each
string's background over ground that one fill already covered, the same
reason apps/tracker keeps `font_str` on colour (§6.1.1). So the flag would
buy it about 3% (alignment alone removes `font_char`'s second-byte spill)
while costing 8-pixel drag steps on the window users move most. Not every
text-heavy window is a candidate; the question is whether it draws its text
as *runs it erases behind*, and the Disk window does not.

## 12. menu.inc

Menu bar: rows 0..MBAR_H-1, white, 1px black line at row MBAR_H-1. Its
layout is **three zones, left to right**:

1. the **System menu**, titled by the os8088 logo: an 11×11 one-color
   DIP-chip silhouette bitmap (7px-wide body with a top notch and four
   pins per side; hand-authored `dw` rows are fine — this is the one place
   bitmap data is hand-made). It is the same three items in every
   application, always cell 0, always x 0..`MENU_SYS_XR`.
2. the **active application's name**, drawn at `MENU_NAME_X` — a label,
   not a menu: it has no hit range and drops nothing.
3. the **active application's own menus** (§12.2), laid out from
   `MENU_NAME_X + font_width(name) + MENU_NAME_PAD` rightward, each cell
   `font_width(title) + MENU_TITLE_PAD` wide. The first cell that would
   reach the clock's hit band (`[vid_clk_hx]`, §12.1) **ends the layout** —
   it is dropped whole rather than clipped, *and so is everything after
   it*, even a narrower menu that would have fitted. That is not
   thriftiness: a cell's bar index must stay equal to its index in the
   app's set, or `ui_dispatch`'s `dec ah` hands the handler the wrong
   menu (§12.2). Bar cells are a prefix of the set, always.

**Everything right of the System menu belongs to whichever application is
active**, so the bar is rebuilt whenever the active application changes —
`menu_bar` (`MENU_BARMAX` × `MB_ENTSZ`, .bss) is a *runtime* table, not
static data.

```nasm
MENU_APPMAX  equ 4              ; app menus the bar can host
MENU_BARMAX  equ MENU_APPMAX+1  ; + the System cell, which is always cell 0
MENU_SYS_XR  equ 29             ; System cell: x 0..29, glyph at MENU_SYS_TX
MENU_SYS_TX  equ 10
MENU_NAME_X  equ 38             ; app-name label pen x
MENU_NAME_PAD equ 16            ; gap after the name, before the first menu
MENU_TITLE_PAD equ 12           ; per-title cell padding (6px each side)

; one bar cell (menu_bar[], rebuilt by menu_layout):
MB_TITLE equ 0   ; word: NUL title ptr, 0 = draw the logo glyph
MB_ITEMS equ 2   ; word: array of near ptrs to NUL item strings
MB_NITEM equ 4   ; word: item count
MB_XL    equ 6   ; word: bar hit range, left x (inclusive)
MB_XR    equ 8   ; word: bar hit range, right x (inclusive)
MB_TX    equ 10  ; word: title text / glyph left x
MB_ENTSZ equ 12
```

**The active application is a window**, tracked in the word `[menu_win]`
(0 = no window owns the bar). Transitions, all of them one-liners at the
sites that already exist:

- `wm_front` (and therefore `wm_show`, and every raise in §13) calls
  `menu_activate` with the raised window: **raising a window makes its
  application active**, and `wm_raise` (§11.90) draws the new bar in the
  same pass.
- **A click on the desktop switches back to Locator** (§12.3): the
  `.desk_icons` branch of §13's ladder calls `menu_activate` with BX = 0
  before `desk_click`, and repaints the bar itself if the owner changed
  (its own gfx_lock — nothing else in that branch holds one). The dock is
  offered the click first, and a dock tile that fronts an instance
  re-activates it through `wm_front` like any other raise.
- `menu_draw_bar` calls `menu_check` first, which hands the bar **to the
  window promoted in the vacated one's place** when `[menu_win]` names a
  window that is no longer visible — `wm_top`, which answers 0 (= Locator)
  only when nothing visible is left. That single validation covers close,
  minimize and hide — none of those sites needs to know about the menu bar
  at all, and since §20.6 that includes the first non-UI-task trigger: a
  package worker tearing its own window down repaints from `wm_destroy`,
  and the same check catches it.

  **It promotes rather than reverting because the title bar does.** Losing
  the front window promotes whatever was under it, and `wm_paint_dmg`
  redraws that window's title bar with the pinstripes and the two boxes for
  exactly that reason (§11.91). A bar that fell back to Locator instead left
  the screen saying two different things about which application is active.
  A **deliberate** switch to Locator is unaffected and stays put: the
  `.desk_icons` branch sets `[menu_win]` = 0, and `menu_check` leaves at its
  first test, so a later hide cannot drag the bar back onto a window.

| symbol          | contract                                                   |
|-----------------|-------------------------------------------------------------|
| `menu_init`     | boot: `[menu_win]` = 0 and one `menu_relayout`, so the very first `wm_paint_all` already draws Locator's bar. Called from kmain after `wm_init`. |
| `menu_activate` | in: BX = window ptr, or 0 for Locator. Out: **CF = 1 if the active application changed** (the caller owes the bar a repaint), CF = 0 if it was already active. Draws nothing, takes no lock, preserves every register. |
| `menu_relayout` | recompute `[menu_set]`, `[menu_namep]` and the whole `menu_bar` from `[menu_win]`. Preserves all registers. |
| `menu_win_set`  | in: BX = window ptr, SI = menu set ptr (0 = none) — stores `[bx+W_MENUS]` and relayouts if BX is the active window. The `OSAPI_MENU_SET` target (§20.3). Preserves every register **and the flags**: its intended call site sits between a package's `wm_create` and the `ret` that owes the loader that call's CF (§20.2). |
| `menu_check`    | if `[menu_win]` names a window that is no longer visible, `menu_activate` on `wm_top` — the promoted window, or 0 for Locator when none is left. No-op while the owner is visible or already Locator. Preserves every register. Called only from `menu_draw_bar`, so it always runs under the gfx lock on the UI task. |
| `menu_draw_bar` | draw the bar: `menu_check`, white field + black rule, every `menu_bar` cell's title (cell 0 = the logo glyph), the app-name label, then `menu_draw_clock`. Gfx lock held by caller. |
| `menu_track`    | in: CX = mousedown x. Runs the whole interaction while the button is held (caller holds gfx lock): highlight title (xor), drop the menu (gfx_save under it to the save-under claim, `[menu_sseg]:0`), track item highlight following `mouse_y`, on release restore save-under + unhighlight; **out AX = 0xFFFF if nothing was selected, else AH = bar cell index (0 = System), AL = item index within that cell**. Item cells are 16px tall, menu width = widest item + 16px padding. Only the bar-specific half is its own: the cell find, `menu_title_xor` and the (cell, item) pack. The drop itself is `menu_drop` (§12.4). |
| `menu_drop`     | the tracker, anchored by variables so it serves both the bar and a context menu (§12.4). |
| `menu_popup`    | drop a menu anywhere on screen under the right button (§12.4). |
| `menu_widest`   | in: BX = array of near item-string ptrs, CX = count. Out: AX = the widest `font_width` over them, 0 when CX = 0. Parameterized by array rather than by bar cell precisely so `menu_popup` can size a menu that has no bar cell. |

`menu_track` polls `mouse_btn`/`mouse_x`/`mouse_y` directly (the ISR keeps
them fresh; cursor stays hidden during tracking since the gfx lock is held —
acceptable, tracking feedback is the highlight).

The (cell, item) pair replaced the old flat `CMD_*` return, because item
numbering now has to mean something inside a menu set the kernel has never
seen. The `CMD_*` constants survive as **ui.inc's internal encoding of the
kernel's own menus** (§12.3), reconstructed there as `base + item` from a
three-entry base table — the same consecutive-commands arithmetic as
before, just no longer baked into the bar.

`menu_track` polls `mouse_btn`/`mouse_x`/`mouse_y` directly (the ISR keeps
them fresh; cursor stays hidden during tracking since the gfx lock is held —
acceptable, tracking feedback is the highlight).

Because the whole interaction runs under one gfx_lock, `menu_track` calls
`gfx_flush` (§32) once, after the pull-down + items are drawn — that is real
back-buffer content and would otherwise stay invisible while the button is
held. The two highlights are not: `menu_title_xor` and `menu_item_xor` go
through `vga_xor_fill_vram` (§5), straight to VRAM, so tracking neither
dirties the back buffer nor flushes — the poll loop does no drawing work at
all beyond the XOR itself. The final save-under restore + title un-highlight
need no flush of their own — the caller's gfx_unlock flushes them (§13
step 2), and that flush is what clears the last item highlight from VRAM.

### 12.05 The bar is redrawn only when its contents changed

`menu_draw_bar` is on the same hot path as `dock_paint` — every window
operation calls it because the bar *might* have changed owner — and unlike the
dock there is no useful per-cell granularity: the bar's contents **are** the
active application's, so either the owner changed and every cell did, or it
did not and none of them did. So one flag, `[menu_bdirty]`, gates the white
field, the black rule, the name label and every cell title.

`menu_relayout` is the single rebuild point and sets it, which covers every
ownership change including the one `menu_check` makes at the top of
`menu_draw_bar` itself. The only other thing that can invalidate the bar is
somebody having drawn **over** it, and just two things can: a fullscreen
window (§11.2) and a dropped menu whose save-under claim was refused. So
`wm_paint_all` and `menu_track` call `menu_force` rather than reason about it
— one bar redraw per menu interaction is not worth a proof. `menu_init` sets
it too, because `-f bin` zeroes no `.bss`.

**The clock is outside all of this.** `menu_draw_clock` fills its own cell and
is called unconditionally at the end of `menu_draw_bar`, as well as directly
by the Clock task (§12.1).

### 12.1 The menu-bar clock

The right end of the bar shows the system clock of §37 — the date and the
time of day, black on the bar's white field. Its **width varies** with the
two display settings of §37 (12- or 24-hour, seconds shown or not), from 18
glyphs (`'Mmm DD YYYY  HH:MM'`) to 24 (`'Mmm DD YYYY  HH:MM:SS PM'`), so
the cell is sized for the longest and **the string is right-aligned in it**:

```nasm
MENU_CLK_W  equ 24*8                        ; the LONGEST form, not the live one
MENU_CLK_X  equ SCREEN_W - 8 - MENU_CLK_W   ; 440: cell left, 8px right margin
MENU_CLK_HX equ MENU_CLK_X - 6              ; 434: hit band left edge
```

Those three are the **VGA reference**: nothing reads them at run time. The
live cell is the derived word `[vid_clk_hx]` = `vid_w - 206` (§39.2), and the
layout limit, the erase and the hit test all read that — the clock hangs off
the right edge of whatever screen the boot probe found.

| symbol            | contract                                                  |
|-------------------|------------------------------------------------------------|
| `menu_draw_clock` | in: nothing (gfx lock held by the caller). Formats the live clock with `clk_fmt` (§37), white-fills the whole cell — x `[vid_clk_hx]`..`[vid_wm1]`, rows 0..`MBAR_H-2`, the black rule excluded — and draws the string **right-aligned**: `font_width` gives its pixel width and the pen goes at `[vid_wm8] - width`, `MENU_TEXT_Y`. Preserves all registers. |

Right alignment is what makes a format change a redraw of the same cell
rather than a relayout: the erase is always the full 24-glyph cell, so a
shorter string leaves clean white to its left and the time stays pinned to
the same right margin whichever form it is in.

`menu_draw_bar` ends with a `menu_draw_clock` call, so every `wm_paint_all`
repaints it; ui_task redraws just the cell when its text changes — once a
second with seconds shown, **once a minute without** (§13 step 4). No
`wm_obscured` check is needed anywhere: windows clamp to `y >= MBAR_H`
(§13), so nothing can ever cover the bar — **except a fullscreen window
(§11.2), which is why step 4's redraw and the ladder's menu-bar branch are
gated on `[wm_fs]` being zero.** The cell is ordinary persistent
content — it goes through the back buffer like everything else (§32) and
the caller's `gfx_unlock` flushes it.

**Clicking the cell opens the Control Panel on its Date/Time page** (§31.5).
ui_task hit-tests `x >= [vid_clk_hx]` *before* `menu_track`, so the cell is
not a menu title and never drops a pull-down; the panel window appearing
(or coming forward) is the click's feedback.

### 12.2 App menu sets — the contract every application implements

An **app menu set** is a read-only structure a window points at through
`W_MENUS` (§11). It is the whole of the interface: the kernel never learns
what an application's menus mean, only how to draw them and whom to call.

```nasm
; app menu set (mirrored verbatim in apps/os88api.inc, §20.5)
AM_NAME  equ 0   ; word: NUL app name for the bar label (<= 15 chars); 0
                 ; falls back to the owning instance's I_NAME (§29.1)
AM_ONCMD equ 2   ; word: near ptr to the command handler, or 0 = this set
                 ; is dispatched by the kernel itself (Locator only, §12.3)
AM_COUNT equ 4   ; word: menu count, 0..MENU_APPMAX (anything above is
                 ; clamped by menu_layout, never trusted)
AM_LIST  equ 6   ; AM_COUNT entries follow:
AMENU_TITLE equ 0 ; word: NUL menu title (the bar cell's text)
AMENU_ITEMS equ 2 ; word: array of near ptrs to NUL item strings
AMENU_NITEM equ 4 ; word: item count (>= 1)
AMENU_ENTSZ equ 6
```

**The command handler** (`AM_ONCMD`) is called exactly like `W_ONCLICK`
(§11/§13): on the **UI task**, **under the gfx lock**, with

```
in:  AL = item index within the menu, AH = menu index (0-based, i.e. bar
     cell - 1 — the System menu is never routed here), SI = the window
     that owns the set, BX = the menu set ptr
out: nothing; may clobber AX/BX/CX/DX/SI/DI/ES like any window callback
```

so it may draw, may call the file API of §18.4, and — like every other
window callback — **must not** take the gfx lock, and must never
wait on something only another task can deliver. It **may** call
`OSAPI_TASK_SPAWN` (§20.6): a menu command is the canonical place a package
starts its worker (a Start/Stop item), and the spawn is safe with the lock
held because `inst_pkg_spawn` takes no lock and never yields. A long
*self-terminating*
loop that yields is a different thing and is sanctioned: Piano's song
playback (§36) runs one
under the held lock, on the same footing as `menu_track`'s own
poll-under-the-lock (§12/§7) — the lock is held throughout, background
tasks block on it, and the loop ends on its own. Its
cycles are billed to the owning instance through `task_cycles`/
`inst_charge` (§8.1), and `snd_disp_set` stamps the instance for sound
grants (§34.3), because a menu command is a dispatch site exactly like a
click.

A package registers its set from its entry proc, before the loader shows
the window:

```nasm
    mov si, my_menus
    call OSAPI_MENU_SET         ; BX = the window wm_create just returned
```

Registering later is legal (`menu_win_set` relayouts when the window is
active) but draws nothing of itself — the bar catches up on the next
`wm_paint_all`. A window with `W_MENUS` = 0 owns the bar the same way; it
simply contributes a name and no menus, which is the correct result for an
accessory like Clock or Bounce.

**Disabled items.** An item string that begins with the byte `MENU_DIS` (1)
is unavailable: `menu_drop` draws the rest of it in `CDGRAY` and skips the
marker, `menu_widest` does not measure the marker, and `menu_hover` refuses
to land on the cell — so it cannot be highlighted and cannot be selected, and
the whole feature is those three places. It is a **string prefix**, not a
flags array, because an application that wants an item disabled already had
to point `AMENU_ITEMS` at a different string to relabel it ("Save Gif" vs
"Save Gif (NoRam)"); one byte in front of that string costs no structure
change, no ABI change, and works for a built-in's menus exactly as for a
package's. An app must still answer the command itself — a keyboard shortcut
never goes near a menu.

**The grey is the whole of the visual signal, and it survives 1bpp** — but only
because `menu_drop` draws the item through `gfx_pen_dis` rather than storing
`CDGRAY`, and `font_ink` masks a flagged glyph to a checkerboard (§39.4/§46).
Grey text alone rounds to black on mono, and a disabled item used to be
pixel-identical to a live one there, silently declining to highlight and never
saying why. It is a checkerboard now, the way the 1bpp Macintosh drew a
greyed-out item, so `MENU_DIS` needs nothing from the app to be *visible* on
any adapter — including when it is used as a radio mark (§43, §45), where the
dithered item is the one that is currently selected.

Relabelling is still worth doing. `'Save Gif (NoRam)'` says **why** the item is
unavailable, which the grey cannot; the grey says **that** it is, which the
words alone did unreliably. Paint does both (§46.3).

**Every string in a set is an offset in the OWNING WINDOW'S segment**
(§11's W_SEG, §20.1) — the app name, the menu titles and every item. So the
bar's runtime table `menu_bar` carries a **`MB_SEG`** word per cell
(`MB_ENTSZ` = 14) and `menu_layout` fills it in: the System menu's cell
carries KERNEL_SEG, the owner's cells carry the owner's. `[menu_dseg]` is
the same thing for the menu currently dropped, read by `menu_drop` and the
item-highlight loop. One cell, one segment — and the reason it has to be
per cell rather than one word for the whole bar is the bug it was written
to fix: with a single "the active app's segment", the System menu's own
items were read out of the *package's* segment and every one of them drew
as the first two bytes of the package header, `O8`.

Strings the kernel reads through a foreign segment are also strings it must
not assume are short: `MENU_MAXCH` (18) and `menu_trunc` bound what a title
or item can occupy, `MENU_MAXW` (160) bounds a dropped menu's width and
`MENU_POPMAX` (11) its item count. A package that hands over a 300-byte
"title" gets it truncated, not a save-under overflow.

### 12.3 Locator — the kernel's own application

**Locator is what the kernel is called when it acts as an application**:
the desktop, the drive icons, the Disk browser and the menus that launch
everything else — the os8088 answer to the Macintosh Finder. It is not an
instance and has no task; it is simply the menu set the bar falls back to
whenever no window owns it.

`menu_loc_set` (menu.inc data) is an ordinary §12.2 set with
`AM_NAME` = `'Locator'` and **`AM_ONCMD` = 0**, the one value reserved to
mean *dispatched by the kernel*: ui.inc recognises it and reconstructs a
`CMD_*` instead of calling through (§13). Its menus are unchanged from the
pre-Locator bar — **File**: "Clock" (CMD_CLOCK), "Bounce" (CMD_BOUNCE),
"Disk" (CMD_FILES), "Close Window" (CMD_CLOSE); **Special**: "Restart"
(CMD_REBOOT) — and so is the System menu, which is cell 0 for every
application: "About os8088..." (CMD_ABOUT), "Control Panel" (CMD_CTRL,
§31), "Task Manager" (CMD_TASKS, §28).

**"Close Window" greys when there is nothing to close.** `ui_loc_gate` points
that item at its `MENU_DIS` twin from `wm_top`, on the press that opens the
bar — the item list is static `.text` and nothing relays the bar out when the
last window goes, so layout time is too early. It is the §46 rule 5 case in
its purest form (a stable, one-word fact) and the one a user meets on the
desktop at boot before meeting any other; it used to be a live item that
answered with a beep. The tables-full refusals beside it — Clock, Bounce and
Disk at the instance cap — stay beeps on purpose: `MAX_TASKS` is 12, and the
predicate is per-kind rather than one word.

```nasm
CMD_ABOUT  equ 1   ; --- System (cell 0, every application) ---
CMD_CTRL   equ 2
CMD_TASKS  equ 3
CMD_CLOCK  equ 4   ; --- Locator: File ---
CMD_BOUNCE equ 5
CMD_FILES  equ 6
CMD_CLOSE  equ 7
CMD_REBOOT equ 8   ; --- Locator: Special ---
```

**Locator has two menu sets, and they share one name.** `menu_loc_set` is
the desktop's, above. `fm_menus` (files.inc data, §22) is the file
manager's: its `AM_NAME` is the same `menu_loc_name` string, so the bar
still reads **Locator** and §12.3's identity rule survives, but its
`AM_ONCMD` is a real handler, `fm_oncmd`. One application, two menu sets —
which is exactly how a Finder behaves when a window is open versus when the
desktop is bare, and it is why the file manager's commands do not have to
exist as `CMD_*` on a desktop where there is no window to act on.

That makes Locator the first **kernel** kind whose set carries a non-zero
`AM_ONCMD`, so `ui_dispatch`'s `.app` route is no longer package-only. Two
consequences, both binding:

- The `test word [bx + W_FLAGS], 2` visibility re-check in `.app` is
  **load-bearing**, and since §20.6 it is load-bearing twice over. The lock
  drops between `menu_track` and the call. On the kernel side every
  `fm_oncmd` command able to destroy the window (Close Window, Restart) is
  *deferred* through `ui_post_cmd` and drained in step 3 — the same task,
  after `ui_dispatch` has returned — so no kernel window dies inside that
  gap on its own. But a package's worker task can: `inst_pkg_alive` →
  `inst_task_die` destroys its own window from another task entirely, and
  `[menu_win]` may name it. The check is sufficient because `wm_destroy`
  clears the visible bit under the gfx lock and `.app` re-reads it *after*
  taking that same lock, and because only the UI task calls `wm_create` —
  so the slot cannot have been recycled behind us while we are the UI task
  standing here. Without it the dispatch would `call` into a freed package
  region.
- `fm_oncmd` runs under the gfx lock like any window callback, so any
  command that needs `app_launch` or `ui_cmd` goes through
  `inst_launch_post` (§29.4) or `ui_post_cmd` (§13). See §22.

The **Disk window is Locator's own window** (§22): `fm_kinit` stores
`fm_menus` into its `W_MENUS`, so fronting the file browser keeps
"Locator" in the bar instead of swapping it for a
one-window app named "Disk" — it just swaps Locator's desktop menus for its
file-manager menus. Nothing else in the kernel points at either set; every
other built-in kind leaves `W_MENUS` at 0 and shows
its `I_NAME` with no menus of its own.

### 12.4 Context menus — `menu_drop` and `menu_popup`

A menu dropped from the bar and a menu popped up under the pointer differ
in exactly three things: where the rect is, which `mouse_btn` bit ends it,
and where the item array comes from. Everything else — the save-under, the
white fill and black frame, the 16px item cells, the XOR highlight that
follows `mouse_y`, the `task_yield` in the poll loop, the restore — is the
same code and, more to the point, the same §32 back-buffer discipline.
**Two copies of that discipline would drift**, so there is one:
`menu_track` was split, and `menu_drop` is the half both callers share.

```nasm
MENU_POPMAX equ 16              ; items a popup may have on a 480-row screen;
                                ; the live cap is [vid_popmax] (§39.2) - 11 on
                                ; CGA, because the rect must still fit

; menu_drop  in:  [menu_x1] [menu_x2] [menu_y1] [menu_y2]  the menu rect
;                 [menu_cnt]   item count
;                 [menu_iptr]  array of near ptrs to NUL item strings
;                 [menu_btn]   the mouse_btn bit mask that keeps it open
;                 gfx lock held by the caller
;            out: AX = item index, or 0xFFFF if released outside
;            clobbers: nothing else
```

`menu_drop` is the body of the old `menu_track` from the save-under to the
restore, with three literals lifted into variables: `MBAR_H` became
`[menu_y1]`, `test byte [mouse_btn], 1` became a test against `[menu_btn]`,
and the item array is read from `[menu_iptr]` instead of the bar cell.
`menu_hover` and `menu_item_xor` follow — both derive the first cell's top
row from `[menu_y1] + 1` now rather than `MBAR_H + 1`. `menu_track` keeps
the bar-specific half: find the cell under the mousedown x, `menu_title_xor`,
set the rect from `MB_XL`/`MBAR_H`, `[menu_btn]` = 1, call `menu_drop`,
un-highlight, and pack (cell, item).

```nasm
; menu_popup in:  CX = anchor x, DX = anchor y (absolute screen)
;                 BX = array of near ptrs to NUL item strings
;                 AX = item count (clamped to [vid_popmax]; 0 = nothing)
;                 gfx lock held by the caller
;            out: CF = 1 nothing was chosen; CF = 0 and AL = item index
;            clobbers: AX; everything else preserved
```

The rect is `menu_widest + 16` wide and `count·16 + 2` tall — the same
arithmetic the bar uses — anchored at the pointer and then **shifted, never
clipped**: right overflow moves x1 left (floor 0), bottom overflow moves y1
up (floor `MBAR_H`). Clipping would cut items in half and leave the ones
below unreachable; shifting is what a Mac does, and it is the reason a
right-click in the bottom-right corner is as usable as one in the middle.
A popup MAY sit over the dock strip (§30) — the save-under puts it back.
`[menu_btn]` = 2, so it lives exactly as long as the right button is held.

Only one menu can be open at a time — both trackers run on the UI task
under the gfx lock and both are driven by a held button — so both use
the menu save-under claim (`MENU_SAVE_KB`, §12/§50) and no allocator
appears. `[vid_popmax]` bounds a popup's HEIGHT (258 rows at `MENU_POPMAX`) and nothing bounds its
width — `menu_widest` is taken as-is — so the honest budget is stated over
the descriptors that actually exist rather than as a general guarantee.
All three (`fm_ctx_file` / `fm_ctx_fold` / `fm_ctx_dir`) are immutable
`.text`, ≤ 8 items of ≤ 18 chars, worst case 4 planes × 130 rows × ~21
bytes against the `MENU_SAVE_KB` claim, and there is no API
slot through which a package could supply another. **A width clamp in
`menu_popup` is the fix the day that stops being true** — a 16-item popup
of screen-wide items would want ~83KB and would run off the end of the
heap into `VIEW_SEG`.

## 13. ui.inc — the UI task (task 0)

Loop forever:
1. Poll keyboard: int 16h AH=01; if a key, fetch (AH=00) and near-call the
   front window's W_ONKEY (if any) under gfx_lock, billed to the window's
   instance (§11 "callback billing"). "Front window" is `wm_top` passed
   through **`fdlg_top`** (§38.1), which substitutes an open file dialog:
   while one is up it takes every key, and the window behind it takes none.
2. `evq_pop`; on EVT_MDOWN at (x,y) — first store the event's EV_C into
   the public word `ui_click_t` (the click's birth tick; §22/§26 read it
   during dispatch), then call **`fdlg_grab`** and, on CF=1, drop the event
   and yield: a Standard File dialog (§38.1) is up and everything outside
   its frame is inert. This test sits ahead of every branch below —
   fullscreen, the bar, the clock cell, `wm_hit` — because modality is not
   a window property and a press on the menu bar must be swallowed as
   firmly as a press on another window:
   - `[wm_fs]` non-zero (§11.2) → skip both menu-bar branches below and go
     straight to `wm_hit`: the bar is under the fullscreen surface, and
     the fullscreen window claims every point as content.
   - y < MBAR_H and x >= `[vid_clk_hx]` → the menu-bar clock (§12.1): store
     `CP_ITIME` into `[cp_sel]` and `app_launch` KIND_CTRL (§31.5), no lock
     held, beeping on refusal like any other launch. Tested **before**
     `menu_track`, so the cell never drops a pull-down.
   - y < MBAR_H → gfx_lock, `menu_track`, gfx_unlock, then `ui_dispatch`
     on the returned (cell, item) pair — see "menu dispatch" below.
   - else `wm_hit`: close box → **quit**: gfx_lock, `app_close_win` BX
     (§29 — looks up the owning instance via `wm_ptr2idx` + `wm_owner`
     and runs the close protocol: synchronous teardown for task-less
     instances, die-flag + hide for task-owned ones — which a package
     instance reaches too once it owns a §20.6 worker; an ownerless window
     degrades to plain `wm_hide`), gfx_unlock. Minimize box (AL=3) →
     gfx_lock, `inst_minimize` BX (§29 — sets the instance's minimized
     flag and hides; the dock tile inverts), gfx_unlock. Title bar → if
     not front, `wm_front`; then run
     the **drag loop**: gfx_lock; xor-draw the outline at the window rect
     (via `vga_xor_rect_vram`, §5/§32 — the outline is a transient overlay
     that never enters the back buffer, so no flush is involved anywhere in
     this loop; it is always xor-erased before the lock drops, which is what
     keeps VRAM equal to the back buffer for every other drawer);
     while `mouse_btn` bit0 set: xor-erase the outline, gfx_unlock,
     `task_yield` (background tasks stay live here), gfx_lock, xor-draw the
     outline at rect offset by (mouse - start) — every iteration, even if
     the mouse has not moved. **Ordering is binding: the gfx lock may only
     be released while the outline is erased** — XOR draw/erase is
     self-inverting only if nothing touches the covered pixels in between,
     and Clock/Bounce repaint the instant the lock drops (the outline is
     not a window, so wm_obscured does not protect it). On release (the
     loop exits with the outline drawn and the lock held): xor-erase the
     outline, update W_X/W_Y (clamp: title bar fully on screen,
     y >= MBAR_H), call `wm_paint_dmg` over the **union** of the rect it
     left and the rect it now occupies (§11.91 — `wm_dmg_union` builds it),
     then gfx_unlock. A drag that did not move the window still repaints
     nothing at all. Do **not** call
     gfx_lock again in the release step — the lock is non-reentrant (§7)
     and task 0 already holds it; re-acquiring would deadlock the GUI.
   - grow box (AL=4, frontmost + WF_SIZABLE only, §11.1) → the **resize
     loop** `ui_grow`: identical structure, lock discipline and binding
     erase-before-unlock ordering as the drag loop, but the outline stays
     anchored at (W_X, W_Y) and its **size** follows the mouse:
     cur w/h = orig w/h + (mouse − start), clamped to ≥ WMIN_W/WMIN_H
     every pass. On release (outline drawn, lock held): xor-erase, clamp
     w to WMIN_W..`[vid_w]`−W_X and h to WMIN_H..`[vid_h]`−W_Y, write
     W_W/W_H, `wm_paint_dmg` over the union of the old and new rects
     (§11.91 — the origin is fixed, so that is the origin plus the larger
     size per axis), gfx_unlock — same no-relock rule.
   - content of non-front window → `wm_front`.
   - content of front window → if its `W_ONCLICK` is non-zero: gfx_lock,
     near-call it (CX=x, DX=y, SI=win ptr) billed to the window's instance
     (§11 "callback billing"), gfx_unlock; else ignore.
   - no window hit (wm_hit BX=0) → call `dock_click` (§30) with CX=x,
     DX=y, no lock held; CF=1 = the click was consumed (anywhere in the
     dock strip). Only if it declines (CF=0) this is a click on the bare
     desktop: **activate Locator** — `menu_activate` with BX = 0 and, if
     it reports CF=1 (the owner really changed), gfx_lock,
     `menu_draw_bar`, gfx_unlock — and then call `desk_click` (§26).
     Dock and desktop icons are hit-tested only after every window has
     declined the click, which gives them correct z-order semantics for
     free; the bar swap happens before `desk_click` so that a
     double-click that opens the Disk window (§26) still ends with
     whatever `wm_front` activates.

   On **EVT_RDOWN** at (x,y) → `fdlg_grab` first, on the same terms as the
   left button (a modal dialog swallows the right button too — there is no
   context menu it could usefully open), then `ui_rdown`, which is
   deliberately much narrower than the ladder above and shares none of it:
   - **`[ui_click_t]` is NOT stamped.** Binding. The double-click
     detectors of §22/§26 compare a stored click tick against it, so a
     right-press that stamped it would let a right-click followed within
     9 ticks by a left click on the same row read as a double-click and
     launch a package the user never opened. For the same reason
     `fm_rclick` moves `FS_SEL` without touching `FS_CLKT` (§22).
   - y < MBAR_H (and no fullscreen window) → **nothing**. The bar has no
     right-button behaviour, and neither does the clock cell.
   - `wm_hit` reports no window → **nothing**. `dock_click` is never
     reached from here: it toggles minimize, and a right-press must not
     do that. Nor is `desk_click`.
   - a window that is not frontmost → raise it (`wm_front` under the
     lock) and stop. A right-click brings a window forward but opens
     nothing: the popup always belongs to the window you can see.
   - the frontmost window, region 0 (content), with `W_MENUS` =
     `fm_menus` → gfx_lock, `fm_rclick` then `fm_rcmd` (§22), gfx_unlock.
     Any other window ignores the press. There is no `W_ONCTX` field and
     no API slot: a package's window keeps its bar menus and nothing
     else, and no shipped `.o88` changes.

   Billing follows §12.2's split exactly. `fm_rclick` — the row select and
   the whole tracking loop — is **unbilled**, like `menu_track`: the time
   a user spends holding the button is nobody's CPU cost, and charging it
   would make one long press dominate the Task Manager's row. `fm_rcmd`,
   the command that follows, gets the `W_ONCLICK` treatment verbatim:
   `inst_win_owner`, `snd_disp_set`, `task_cycles`/`inst_charge` (§8.1).
3. If `[inst_launch]` is non-zero (§29): AX = [inst_launch] − 1, zero
   `[inst_launch]`, call `app_launch` with AL = kind. Then if
   `[ld_pending]` is non-zero (§21): AX = [ld_pending] − 1, zero
   `[ld_pending]`, call `loader_run`. Then if `[ui_pcmd]` is non-zero:
   AX = [ui_pcmd], zero `[ui_pcmd]`, call `ui_cmd` — a **deferred kernel
   menu command** (below). Then if `[cp_dirty]` is non-zero
   (§31.2): zero it, gfx_lock, `wm_paint_all`, gfx_unlock — the scheduler
   mode was flipped from the Control Panel and every already-painted
   window that quotes it (the About box's third line, §14) must follow.
   All four run **outside** the gfx lock with the same
   consume-before-run rule — app_launch, loader_run and ui_cmd manage their
   own locking, and the repaint takes the lock here rather than inside
   `cp_onclick`, which already holds it. Then, on the same deferred
   channel, if `[clk_dirty]` is non-zero (§37): zero it and call
   `clk_rtc_write` — the Control Panel changed the time and the hardware
   RTC is written **here**, outside the lock, because a page proc may not
   call BIOS (§31.1).

   ```
   ui_post_cmd  in:  AX = CMD_* (§12.3)
                out: nothing (all registers preserved)
   ```

   `ui_post_cmd` is one word store and is legal **from any lock-held
   callback** — it is `inst_launch_post`'s counterpart for the commands
   that are not launches. It exists because `gfx_lock` is a non-recursive
   spin released only by the UI task (§7): a `W_ONCLICK` or `AM_ONCMD`
   handler that called `ui_cmd`'s `CMD_CLOSE` or `CMD_REBOOT` directly
   would take a lock it already holds and hang the machine dead — no beep,
   no watchdog, no recovery. `[ui_pcmd]` lives in `.text` with a `dw 0`
   initialiser, not in `.bss`, because ui_task reads it on its very first
   pass and `-f bin` gives `.bss` no image bytes (§1). Rapid posts coalesce,
   exactly like `[inst_launch]`; nothing in the system posts two.

   Deferring is also what keeps the billing honest. `ui_dispatch`'s `.app`
   route captures `DI = inst_win_owner` *before* the handler and calls
   `inst_charge` *after* it; `CMD_CLOSE` is the first menu command that can
   free that very record, and running it inline would write cycles into a
   freed slot.
4. **The clock (§37/§12.1).** Call `clk_tick`, which advances the wall
   clock from the `[ticks]` delta and returns **AL = a change mask**:
   bit 0 = a second passed, bit 1 = the menu bar's *text* changed. AL = 0
   → nothing to do. `[wm_fs]` non-zero (§11.2) → also nothing to draw
   (the bar and any Date/Time page are under the fullscreen surface;
   clk_tick has already kept time). Otherwise decide whether taking the
   gfx lock is worth it, because taking it blinks the cursor and that
   blink **is** the flicker the seconds setting exists to remove:
   - bit 1 set → lock, `menu_draw_clock`, `cp_tick`, unlock.
   - bit 1 clear (a second passed but the bar shows no seconds) → ask
     `cp_tick_due` (§31.5) whether a Date/Time page is on screen; only
     then lock, `cp_tick`, unlock. The panel's seconds field runs whatever
     the bar displays, so it — and only it — still wants the per-second
     redraw.
   - bit 1 clear and no page up → **do not take the lock at all**. With
     seconds hidden and no panel open, the desktop is touched once a
     minute.

   `clk_tick` itself is unconditional and cheap, so the clock keeps time
   whether or not anything is drawn. The redraw is **not** billed to any
   instance (§8.1): it is kernel chrome, drawn by the UI task on its own
   account, not a window callback dispatch.
5. `task_yield`.

**Menu dispatch** (`ui_dispatch`, in: AX from `menu_track` — 0xFFFF =
nothing selected). Three routes, decided in this order:

1. **AH = 0** — the System menu, identical in every application (§12.3):
   command = `CMD_ABOUT + AL`, handled by `ui_cmd` below.
2. **`[menu_set]` has `AM_ONCMD` = 0** — a kernel-dispatched set, i.e.
   Locator: command = `ui_loc_base[AH] + AL`, where `ui_loc_base` is the
   three-word table {CMD_ABOUT, CMD_CLOCK, CMD_REBOOT} that restores the
   old consecutive-commands arithmetic (§12). Handled by `ui_cmd`.
3. **otherwise** — an application's own set (§12.2): gfx_lock,
   `inst_win_owner` on `[menu_win]` for billing, `snd_disp_set`,
   `task_cycles`, near-call `[menu_set + AM_ONCMD]` with AL = item,
   AH = menu index (= cell − 1), SI = `[menu_win]`, BX = the set;
   `inst_charge`, restore the sound stamp, gfx_unlock. Byte for byte the
   `W_ONCLICK` dispatch of step 2, and for the same reason: it is a
   window callback that happens to have been reached through the bar.

`ui_cmd` (in: AX = CMD_*, 0 ignored) is the old flat dispatcher, unchanged:
CMD_ABOUT/CTRL/CLOCK/BOUNCE/TASKS → `call
app_launch` with AL = the matching KIND_* (§29; CMD_CTRL → KIND_CTRL, the
Control Panel of §31). It runs on the UI task with
no lock held, so the call is direct — the deferred `inst_launch` channel
in step 3 exists for lock-held posters (e.g. W_ONCLICK handlers, and now
every app menu handler, which runs under the lock by contract §12.2).
Clock/Bounce launch a **new instance** each time (up to their §29
caps); About/Control Panel/Task Manager are singletons — at cap,
app_launch fronts (and un-minimizes) the existing instance instead. CMD_FILES → call
`files_open` (§22 — mounts, then launches/fronts the Disk singleton via
app_launch; does its own locking). CMD_CLOSE → **quit** the frontmost:
gfx_lock, `wm_top`, and if BX ≠ 0 `app_close_win` under the same lock,
gfx_unlock. CMD_REBOOT → gfx_lock (never released), `vid_text` (§39.6 —
mode 3, or mode 7 with the Hercules graphics bit cleared),
`sched_unhook`, `int 0x19`.

All wm_* calls that repaint are made under gfx_lock by the UI task.

## 14. apps.inc

The built-in app **kinds**: About, Clock, Bounce. Nothing is
created at boot (there is no `apps_init`) — every instance is launched on
demand through `app_launch` (§29), which allocates an instance record and
a per-instance state block from the kind's pool, creates the window from
the kind's template (cascaded +16px per pool slot so instances don't
stack), runs the kind's init proc, and spawns the kind's task if it has
one. Closing an instance frees all of it.

Per-instance state pools (.bss; slot stride and cap pinned in the §29
kind table): `app_clk_pool` 10 × 8 (CLK_H/M/S bytes at +0/+1/+2, pad,
CLK_LAST word at +4, CLK_ACC word at +6), `app_ball_pool` 10 × 8
(BAL_X/Y/VX/VY words). About is stateless. Init procs (KD_INIT contract,
§29): `app_clock_kinit` (h/m/s = 0, last =
[ticks], acc = 0), `app_bounce_kinit` (x=4, y=4, vx=3, vy=2).

Paint/onkey procs receive SI = window ptr (§11) and find their state via
`inst_of_win` (§29) + `I_SPTR`; module-level draw scratch (used only under
the gfx lock) may stay shared. Kind behavior:

- **About** — 300×120 at (170,140), title "About os8088". Paint: four
  centered lines — "os8088 1.0", "a graphical OS for the 8086", the bare
  scheduling word, which **tracks the live mode** (§8.2, read with
  `sched_mode_get`): "pre-emptive" or "cooperative", and the adapter the
  boot probe found (§39.1): "VGA - 640x480 - 16 colors",
  "Hercules - 720x348 - mono" or "CGA - 640x200 - mono". The geometry cannot
  be a constant in the third line any more, which is why it moved into a
  fourth; `app_about_center` re-measures every string, so unequal line
  lengths cost nothing. No onkey. Singleton (cap 1).
- **Clock** — 130×60 at (350,60), title "Clock". Cap 10 (on VGA the template
  position keeps the whole +16·9 cascade on-screen and above the dock; on a
  shorter screen `wm_fit` clamps its tail back onto it, §39.7).
  Per-instance
  task (`app_clock_task`; entry receives DX = instance index, caches the
  record and state ptrs): loop { task_sleep 9; **if I_STATE = 2 →
  teardown via `inst_task_die`** (§29); AX = [ticks]; delta = AX −
  CLK_LAST (subtraction idiom, safe across wrap); CLK_LAST = AX; CLK_ACC
  += delta*10; while CLK_ACC >= 182: CLK_ACC −= 182 and advance seconds
  with carries (s 60→0/m+1, m 60→0/h+1, h 24→0). This time-keeping runs
  every iteration; only drawing is conditional: gfx_lock; **re-check
  under the lock** that the window (I_WIN) is visible, then `wm_clip_set`
  (§11.3) — if either fails, gfx_unlock and skip; else draw HH:MM:SS from
  the instance's CLK_H/M/S centered in content; gfx_unlock }. A half-covered
  Clock therefore redraws the half that shows, whole glyphs only, instead of
  stopping. **The white background is erased one 8x8 cell at a time**, inside
  `app_clk_render`, each cell gated on the `wm_clip_test` answer `font_char`
  is about to give: that is §11.3's granularity rule, and a single 64x8 fill
  followed by `font_str` would blank the visible band of a horizontally cut
  Clock twice a second. Paint proc renders the same string from the state
  block. The accumulator design is binding.
- **Bounce** — 150×130 at (300,150), title "Bounce". Cap 10 (on VGA the
  template position keeps the whole +16·9 cascade on-screen and above the
  dock; on a shorter screen `wm_fit` clamps its tail back onto it, §39.7).
  Per-instance task: loop { task_sleep 2; **if I_STATE = 2 → `inst_task_die`**;
  gfx_lock; **check under the lock** that the window is visible, then
  `wm_clip_set` (§11.3); if both hold, erase the 8×8 black square at the old
  pos (white fill), step, draw at the new pos — both fills cut to the
  visible region, so a half-covered Bounce shows the square crossing the
  half you can see. If either fails, **step anyway** and draw nothing;
  gfx_unlock }. Paint proc: square at the instance's current pos.
  **The step is unconditional**, which is what makes a fully covered or
  minimized Bounce come back where it now is rather than where it was
  buried; it is safe precisely because the paint proc draws from state.
  (Before §11.3 the frame was skipped without erasing *or* stepping, and a
  covered Bounce looked frozen. That rule is retired.)

The close protocol for these tasks is §29's: the UI task never destroys a
task-owned instance's window — it sets I_STATE = 2 and hides; the task
notices at its next wake (≤ 9 ticks for Clock, ≤ 2 for Bounce) and tears
itself down with `inst_task_die` → `task_exit`. A package's worker (§20.6)
reaches exactly this path through `OSAPI_TASK_ALIVE`, whose whole job is to
be the worker's "next wake" check; there the latency bound is whatever the
worker's own sleep is, which is why rule 2 makes the call mandatory once
per outer-loop iteration.

## 15. kernel.asm — boot sequence

Keep the 0x0000 cold entry. At 0x0010 the retired syscall gate is replaced
by the **os8088 API jump table** (§20.3) — a run of 4-byte `jmp near` slots at
pinned offsets. kernel.asm also owns the tiny osapi helper routines
(§20.4) and the `osapi_seed` word. `cpu 8086` + `bits 16` + `org 0`.

**Boot splash entry — 0800:0008.** A third fixed entry point sits between
the cold entry and the API table: a `jmp near spl_tick` at offset 0x0008,
**far-called by the boot sector after every sector it reads** once at least
`SPL_RESIDENT` (= 6) sectors are in memory. Contract: AX = sectors loaded
so far, DX = total sectors to load; `spl_tick` preserves every register and
segment (flags clobbered), runs on the boot stack with the boot sector's
segments, and returns with `retf`. The boot sector defines the same
`SPL_RESIDENT` constant; the two must agree with this section.

The splash module (`splash.inc`, prefix `spl_`) is **included first, before
every other module but `viddet.inc`** — which it calls on its first tick
(§39.1/§39.6) and which is therefore included immediately ahead of it — and
ends with a build assertion that its last byte
lies below `SPL_RESIDENT * 512` (the label difference is against `$$`, so
the assertion covers `viddet.inc` and everything else ahead of it) — it must be fully resident before the
first tick can arrive. Because it runs mid-load it is **self-contained**:
it calls int 10h (cursor set, teletype text — BIOS text is
legal here; the "only the UI task calls int 10h after boot" rule of §8
starts at kmain), `viddet.inc` and its own planar drawing primitives, never
a later module's routines (they are not yet resident). Its state lives in
in-module data words, **never .bss**: .bss begins at the image's end, so
the final sector read lands on top of it, and nothing has cleared it yet.
The splash must never delay loading — no waits, no timing loops; it only
draws, once per completed sector, inside the disk's own rotational latency.
First tick: the §39.1 probe and the mode set, then — on VGA — the chrome
(welcome dialog, bar trough, title). Every tick: bar fill = AX×288/DX
pixels, the one stage that runs on every adapter (its origin computed at
run time, §39.6), plus on VGA the right-aligned percentage and one
spin step of the vector "8088" (cosine-scaled about its vertical axis,
angle index = AX mod 16). Mono gets the bar alone: the dialog does not fit
in 200 rows, and int 10h teletype into a Hercules already in graphics
writes character/attribute pairs into the bitmap. kmain's own `vid_init`
then wipes the splash.

kmain: set DS/ES = `KERNEL_SEG` and SS:SP = `LOW_SEG:STK0_TOP` (§2.1),
`sti`, `cld`, then:
`sched_init` → `evq_init` → `clk_init` (§37 — the RTC probe, before the
mode set so a machine without one is dated from the fallback constants
from the first paint onward) → `vid_init` (§39 — re-runs the splash's probe,
apply and mode set) → `bb_init` (§32 — the RAM probe
must run after the mode set, which clears VRAM, and before the first
drawing call) → `font_init` → `wm_init` →
`inst_init` → `mouse_init` → `desk_init` → `files_init` → `loader_init` →
`tm_init` → `snd_init` (§34.7 — publishes `snd_live` last) → gfx_lock →
`wm_paint_all` → gfx_unlock → `cursor_show` → jump
into `ui_task` (task 0 never returns). (`dock_init` runs right after
`desk_init`.) **Clean boot**: no app instances exist — the first paint
shows only the desktop, drive icons, the empty dock strip and menu bar;
everything is launched from the menus (§13/§29). Include order:
`instance.inc` right after `wm.inc`; `icons.inc`, `desk.inc`, `dock.inc`,
`taskmgr.inc` and then `ctrl.inc` (§31) after `files.inc`; `clock.inc`
(§37) right after `events.inc`, since it reads `[ticks]`;
`farcall.inc` (§33) before all of them, since it defines the macros they use. The Control
Panel has no init routine — it is task-less and stateless, so nothing runs
for it at boot; forward references from `instance.inc`'s kind table to
`cp_tpl`/`cp_sname` resolve at assembly time exactly as `tm_tpl` already
does.

**The save-under is claimed per drop, not per session.** `menu_drop` takes
`MENU_SAVE_KB` from the heap on the way in and gives it back before it
returns, so those 20KB exist only while a menu is actually on screen — which
on a 128KB machine is a third of the heap it used to hold at every instant
nobody was looking at a menu. `menu_init` used to take it once and keep it.

The claim can therefore **fail**, where before it could only fail at boot, and
that needs no new failure path: `[menu_sseg]` = 0 already means "no buffer",
so the save is skipped and the restore repaints instead — slower, and
correct. A machine tight enough to hit it gets a flash when a menu closes
rather than a menu it cannot open.

**That repaint is a damage rect, not the screen.** A menu covers the rect it
drew and nothing else — no shadow outside it, which is exactly why `gfx_save`
captures that same rect — so `wm_paint_dmg` over `[menu_x1..menu_y2]` puts
back the windows it occluded and leaves everything else alone (§11.91). It
also leaves the menu **bar** alone, which is what the save-under path does
too, so `menu_title_xor`'s self-inverting cell highlight behaves identically
either way.

**The release happens before the selected item runs**, and that is what keeps
the claim from fragmenting anything. `menu_drop` frees on its way out, so a
bar menu is released inside `menu_track` — before `ui.inc` even drops the gfx
lock, let alone calls `ui_dispatch` — and a context menu before `files.inc`
looks its command up. Whatever the item then does, launching a package
included, allocates into a heap the menu has already left.

The one thing still overlapping it: `menu_drop`'s tracking loop calls
`task_yield`, so a package worker can claim while a menu is down. Its claim
lands above the save-under (both fit from the bottom) and freeing the
save-under leaves a hole under it — transient, not a split, because the next
bottom-up claim reuses it and package regions come from the other end
anyway.

### 15.1 Size guards

End of file (after all `%include` lines, with `section .text` in effect).
`kernel_text_end` **must** be the last thing in `.text`: it is at once the
image size, the base of `.bss` and — through `KIMG_PARA` — where the FAT
snapshot begins. Each section measures itself against its own `$$` — a label
difference across two sections is not a constant in `-f bin` and will not
assemble.

```nasm
kernel_text_end:
KTEXT_SIZE equ kernel_text_end - $$

section .lowbss
kernel_low_end:
KLOW_SIZE equ kernel_low_end - $$

section .bss
; (modules already declared their own .bss blocks inside their .inc files;
;  NASM accumulates them in declaration order — this block lands last)
kernel_bss_end:
KBSS_SIZE equ kernel_bss_end - $$

KERN_KB    equ (KERN_SIZE + 1023) / 1024              ; §28 RAM figures
KBUF_KB    equ ((FAT_PARA + LOW_PARA) * 16 + 1023) / 1024

%if KERN_SIZE > KERN_BUDGET
%error "kernel too big: it must fit KERN_BUDGET - see docs/KERNEL-MEMORY.md"
%endif
%if KTEXT_SIZE + KBSS_SIZE > 65536
%error "kernel image + bss overflows one 64KB segment"
%endif
%if STK0_SIZE < 512
%error "STK0_SIZE is too small to be a stack"
%endif
%if KLOW_SIZE + STK0_SIZE > 65536
%error "lowbss + task 0's stack overflows one 64KB segment"
%endif
%if 4 * (MENU_POPMAX*MENU_ITEM_H + 2) * (MENU_MAXW/8 + 2) > MENU_SAVE_KB*1024
%error "menu save-under can overflow its claim - lower MENU_POPMAX/MENU_MAXW"
%endif
%if (KERNEL_SEG % 32) || (FAT_SEG % 32) || (LOW_SEG % 32)
%error "a disk-buffer segment is not 512-byte aligned - see KIMG_PARA"
%endif
%if HEAP_SEG % 32
%error "the pool or the heap is not 512-byte aligned - see KIMG_PARA"
%endif
%if KERNEL_SEG*16 + KERN_SIZE > BOOT_LIN - BOOT_STACK
%error "the kernel would land on the relocated boot sector's stack"
%endif
```

**Guard 1 is the one the project is steered by**: the kernel's whole
footprint — image, scratch, FAT snapshot, disk buffers and every task stack
— against 64KB (§2). Raising `KERN_BUDGET` is a decision to be taken with
whoever asked for the feature, which is why its message points at
`docs/KERNEL-MEMORY.md` rather than telling you what to edit.

Guard 4 is the menu save-under, the one kernel buffer deliberately outside
that budget because it is a heap claim (§12.4/§50) that exists only while a
menu is down — it bounds what `gfx_save` can be asked to write, which is a
build-time property of the clamps and does not depend on when it is claimed. Guard 6 is the 512-byte
alignment every int 13h target depends on (§2.1.1). Guard 7 is the relocated
boot sector (§15.2). Keep this block last.

### 15.2 The boot sector relocates itself

The BIOS loads `boot/boot.asm` to 0000:7C00 and jumps there. That code is
still executing while the kernel's sectors arrive — it far-calls the splash
at `KERNEL_SEG:0008` after every one — and the kernel lands at 0x00600 and
runs up to 64KB, so it covers 0x7C00 long before the last sector.

So `start`'s first act, before it touches a drive, is to copy its own 512
bytes to `BOOT_RELOC:7C00` (linear 0x11000, above anything the kernel can
reach) and far-jump there. **The copy keeps the same offset**: every label in
the file still resolves at `org 0x7C00`, only the segment registers change,
and the stack rides along at the same offset and grows down from 0x11000.
Nothing above the far jump addresses memory through a label, so that prologue
runs correctly at 0000:7C00 where the BIOS put it.

`BOOT_RELOC` and `KERNEL_SEG` are mirrored in `kernel/kernel.asm`, whose
guard 7 proves the kernel ends clear of the relocated stack.

## 16. Build & test

- Makefile: boot-image recipes unchanged. `run` target keeps the serial
  mouse (`-chardev msmouse,id=m0 -serial chardev:m0`) and now also attaches
  the software floppy as drive B:
  `-drive file=build/apps.img,format=raw,if=floppy,index=1`.
  `test` target: same, plus `-display none -qmp unix:build/qmp.sock,server,nowait`.
- New tooling targets: see §24 (apps, packages, FAT12 data-disk images).
- Adapter knobs (§39.9): `VIDEO=cga|herc|vga` skips the probe and `HERCSEG=`
  relocates the Hercules framebuffer into RAM QEMU actually maps. Both go
  through a stamp file, because the image itself carries no record of which
  adapter it was built for and would otherwise not rebuild. `xt-cga` /
  `xt-hercules` (`vm/xt-cga`, `vm/xt-hercules`) boot 86Box with the real
  cards; every shipped image is built with neither knob set.
- AT-class 86Box targets: `286` (`vm/286`, AMI 286 clone board, 286 @
  12.5MHz, 1MB), `386sx` (`vm/386sx`, Shuttle HOT-304, 386SX @ 16MHz, 2MB)
  and `386` (`vm/386dx`, Micronics 386, 386DX @ 25MHz, 2MB), all with an
  OTI-067 VGA, a serial mouse and 1.44MB drives — they boot `$(IMG)` /
  `$(APPSIMG)`, not the 360KB pair. Nothing in the OS changes: it is 8086
  code in real mode, int 12h still caps at 640K, and the RAM above it is
  unreachable by design. Two 86Box traps apply: `mem_size` is clamped to the
  board maximum without a word (which is why the 286 is not `ibmat` — the
  5170 planar stops at 512KB), and an empty CMOS makes the BIOS stop at its
  setup screen once, until `vm/<machine>/nvr/` exists.
- Gate packages ride their own scratch images and are mounted in place of
  the apps disk with `make test-snd TESTAPPS=<img>` (the `test` target's B:
  drive is fixed):  and `build/filetest.img` / `-frag` (§18.4). A write
  test is only half-done in the emulator — finish it on the host with
  `python3 tools/os88disk.py --verify <img>`.
- **`docs/TESTING.md` is the testing map**, and its section on modelling the
  old machine from a fast one is what to read before taking any number: the
  container is ~1000x a 4.77MHz 8088, so a constant sized against it encodes
  the wrong range, and flicker and input overrun cannot be observed there.
- The **benchmarks are not here.** `apps/fontbench` (§6.1.1) and
  `apps/typebench` (§11.94) are the same idea for a *measurement* rather than
  a gate, and they live on the **`testing` branch**: they ship on no disk,
  nothing in this tree builds or cites them, and a harness that only ever
  answers a question once is tooling rather than system. Both time their
  drawing paths against counter 0 of the 8253 read directly, because a 55ms
  tick cannot resolve a 3ms row. **Run either under QEMU with `-icount
  shift=3,sleep=off`** and the PIT counts guest **instructions** instead of
  host time, which is what makes a result reproducible (±1 count across runs)
  and independent of the machine it is taken on. Without `-icount` they
  measure the host, which is not an 8088 and not a number worth quoting.
- **`check-images`**: `build/` is gitignored but a curated set inside it is
  force-added and shipped — the kernel, both boot sectors, both bootable
  floppies, both software floppies, and every package's `.bin`/`.o88`.
  Nothing makes those follow a source change, so this target builds every
  one of them a second time into `build/.check` and compares byte for byte.
  It is only meaningful because the toolchain is deterministic by design
  (§24: `os88disk.py` pins the volume serial and every FAT timestamp), so a
  difference is staleness and never noise. The set is read from
  `git ls-files build` rather than listed, so it cannot drift from what is
  tracked. Three failures: **STALE** (rebuild and commit), **ORPHAN**
  (tracked, but nothing builds it) and **SCRATCH** (a tracked `VIDEO=`/`RTC=`
  stamp — named specially because the scratch build makes one too and two
  empty files compare equal). The comparison build is always knob-free, so
  a kernel carrying a forced probe reads as stale, which is what the rule
  above ("every shipped image is built with neither knob set") needs to stop
  being a comment nobody executes. Not part of `all`: it costs a second full
  build and is a pre-commit gate.
- 86Box config (`vm/xt/86box.cfg`): set the mouse to a serial Microsoft
  mouse on COM1 (best-effort; cannot be verified headless).
- The kernel may exceed 8 sectors; the two images are already built
  separately with correct geometry, and the boot sector's LBA→CHS handles
  cylinders > 0. Nothing to change in boot.asm.

## 17. Definition of done

1. `make` builds all images with zero warnings; the §15.1 guards pass.
2. QEMU boots to a **clean desktop**: gray dither, menu bar (logo + File
   + Special), drive icons, the empty dock strip at the bottom, arrow
   cursor — no windows, nothing running. The scheduler boots
   **pre-emptive** (§8.2).
3. Apps launch from the menus as closable instances — two Clocks tick
   independently, and they keep ticking **while** typing in the Note Pad
   package and
   while a drag outline is being moved (pre-emption visibly working).
4. Mouse moves cursor; windows drag by title bar; clicking a back window
   raises it; the close box **quits** its instance (window, task and
   state freed — the Task Manager row goes free within a task period);
   the minimize box sends an instance to the dock (tile inverts) and a
   dock-tile click restores or fronts it; menus pull down and dispatch.
5. Only 8086 instructions (`cpu 8086` proves it at build time).
6. File → Disk opens the Disk window listing the software floppy's
   contents; double-clicking MINES loads and shows Minesweeper.
7. Minesweeper is playable with the mouse: reveal, flag (F), flood fill,
   win and lose states, colored numbers; the clock keeps ticking behind it.
8. Two disk icons on the desktop (QEMU reports two floppy drives);
   double-clicking one opens the Disk window freshly mounted for that
   drive; windows cover the icons correctly.
9. The Disk window shows per-file icons — MINES with its embedded mine
   glyph, HELLO with the generic application icon — and the Refresh
   button re-reads a swapped disk (QMP `change` + Refresh shows the new
   directory without rebooting).
10. System → Task Manager opens a window with a live CPU load gauge and
    history graph (near 0% idle, visibly rising while dragging a window),
    a RAM readout, and the per-instance process list with each row's CPU
    share and memory — all updating twice a second while launched Clocks
    and Bounces keep running, rows appearing and freeing as instances
    launch and quit. **Task-less apps are listed too**: About,
    Disk and every loaded package **that owns no worker** show state
    `evt` — a package with a §20.6 worker shows the worker's
    `run`/`rdy`/`slp` exactly like Clock and Bounce, with the two
    counters summed onto its one row (§28) — plus their own region size
    under MEM (`-` for built-ins, which own no region), and a CPU share
    that rises with the work their window callbacks actually do — a
    Minesweeper repainted repeatedly reads double digits.
    Clicking the content toggles the **memory view** (§28): the RAM line,
    a 640K conventional-memory map, a kernel-segment map in which each
    loaded package's region shows in its slot's dither pattern, and the
    process list with a matching legend square, hex base address and size
    per package row. Loading a second package grows the map; closing one
    frees its region on the next sample; clicking again returns to the
    gauge with a gapless history graph.
11. System → Control Panel opens a singleton two-pane window: the left pane
    lists the panel items with "Scheduler" already selected (white on a
    black bar), the right pane shows that item's page — a "Scheduler"
    heading and two radio rows, "Pre-emptive" (filled at boot) and
    "Cooperative". Clicking a row moves the dot immediately, an About box
    already open on screen has its third line repainted to the new
    scheduling word within the same UI pass (the `cp_dirty` repaint of
    §31.2), the Task Manager's SCHED field follows at its next sample, and
    no user-visible string anywhere says "watchdog" (§31).
    In cooperative mode Clocks keep ticking, Bounce keeps moving, menus
    still pull down and Note Pad still types — every wait loop yields
    (§8.2) — and a runaway package's callback is still cut off after
    `SCH_WD_TICKS` ticks (`sch_wd_hits` advances, readable with QMP `xp` on
    segment 0x0800).
12. **Writing works and the volume stays a legal FAT volume** (§18.4/§27.1):
    type into Note Pad, F2 saves ("Saved NOTES.TXT"), F3 in a *second*
    Note Pad instance loads the same text back; the Disk window's Refresh
    lists `NOTES.TXT` with its true size; and after QEMU exits,
    `python3 tools/os88disk.py --verify build/apps.img` passes — FAT1 ≡
    FAT2, no lost clusters, no cross-links, every chain's length matching
    its directory size — with the host able to read the file, CRLF line
    endings and all. Re-saving a longer note replaces it in place without
    leaking clusters, and a second file written to a **fragmented** disk
    (`--scramble`) still verifies.
13. **All three adapters come up** (§39). `make test VIDEO=cga` and `make
    test VIDEO=herc HERCSEG=0x7000` (read back with `tools/hercshot.py`)
    reach the same desktop, cursor tracking, menus pulling down and packages
    launching; `make xt-cga` / `make xt-hercules` boot the real cards on
    86Box and are the only test of the §39.1 probe. On CGA the usable
    desktop is 156 rows, so `wm_fit` (§39.7) clips the Task Manager,
    Minesweeper and Piano at the dock — they launch, run and close anyway,
    which is the documented outcome and not a bug. VGA output stays
    bit-for-bit what it was, and is checked first (§39.9).
14. **A package with a worker task** (§20.6) keeps animating while another
    window is dragged — the proof that package code is now pre-empted
    against the UI task — and its Task Manager row shows `run`/`rdy`/`slp`
    rather than `evt`. Closing it frees the region: the memory view's RAM
    figure and kernel-segment map return to their pre-launch values within
    one sample after the worker's next wake, and a re-launch reuses the
    freed hole. The same holds with the Control Panel set to cooperative
    mode, where only the teardown latency grows.

## 18. disk.inc — floppy I/O (BIOS int 13h) + the FAT driver

Only the UI task touches the disk (extends §7's BIOS rule to int 13h).
During a transfer, task switching is paused: `inc byte [sch_lock]` before,
`dec` after (the timer ISR still chains the BIOS tick, which the floppy
motor logic needs). The gfx lock is NOT held across disk I/O by `disk.inc`
itself — a window callback that calls the §18.4 write path holds it, and
accepts the stall.

Geometry lives in variables so both 1.44MB (18 spt) and 360KB (9 spt) data
disks work: `disk_spt` (word), `disk_heads` (word) — loaded from the
validated BPB at mount (§18.2 rules 11/12), restored to the 9/2 fallback on
any mount failure. LBA→CHS: cyl = LBA/(spt×heads); rem = LBA%(spt×heads);
head = rem/spt; sector = rem%spt + 1. Transfers go **one sector per int 13h
call** (AL=1) — no multi-sector calls, so track boundaries and DMA
alignment never matter. Each sector: up to 3 attempts, with AH=00 reset on
failure between attempts.

`disk_read` and `disk_write` are the same routine: both set `[dsk_op]` (02h
read / 03h write) and fall into the module-internal `dsk_xfer`, so the CHS
conversion, the cylinder guard and the retry policy exist **once** and can
never diverge between the two directions.

| symbol       | contract                                                      |
|--------------|----------------------------------------------------------------|
| `disk_read`  | in: AX=LBA, CX=sector count, ES:BX → dest (advances BX by 512 per sector; caller's ES:BX budget must cover count×512). Drive from `[disk_drive]`. Out: CF=1 on unrecoverable error. Preserves registers per §1. FS-agnostic — it knows nothing of §19. |
| `disk_write` | identical contract, source instead of destination: in: AX=LBA, CX=sector count, ES:BX → source. Out: CF=1 on unrecoverable error, and `[dsk_ioerr]` = the last int 13h status byte (AH), which is how §18.4 tells write-protected media (03h) from a real failure. Preserves registers per §1, and is likewise FS-agnostic. **No LBA gate of its own beyond `dsk_xfer`'s cyl<80 rule** — every caller is §18.4, which computes LBAs only from the validated §18.1 layout. |
| `disk_mount` | in: DL=drive (0=A, 1=B). Sets `[disk_drive]`, restores the fallback geometry 9/2 with `disk_nfiles`=0, reads LBA 0 with that *fallback* geometry (CHS 0/0/1 — identical under any real floppy geometry) into `dsk_secbuf`, then runs the §18.3 mount sequence: BPB validation (§18.2), FAT snapshot into `FAT_SEG`, root-directory scan into the synthesized `disk_dir` cache, icon harvest into `disk_icons`. Out: CF=0 with `disk_spt`/`disk_heads`/`disk_nfiles`, the §18.1 variables and both caches filled; CF=1 with `disk_nfiles`=0 and fallback 9/2 (unreadable, unformatted, or any §18.2 rule failed). Clobbers CF only. A torn mount is a failed mount; **no cross-mount state survives** — every open/refresh fully remounts, never stale. |
| `disk_drive`  | byte variable, current drive (init 1 = B:)                   |
| `disk_nfiles` | word, valid after a successful mount (else 0)                |
| `disk_dir`    | 1024-byte **`.lowbss`** buffer (§2.1): the **synthesized directory cache** — 32 × 32-byte entries in the §19 staged layout, built by `disk_mount` from the FAT root directory (never a raw on-disk image). Written through ES at mount; read only via `dsk_get_dir` |
| `disk_icons`  | 2048-byte **`.lowbss`** buffer (§2.1): 32 × 64-byte **harvested** icon bodies (§19); entry i belongs to directory entry i, all-zero = no icon. Fully rewritten every mount (the §29.1 I_ICON rule rests on that). Read only via `dsk_get_icon` |
| `dsk_get_dir` | in: AX = entry index. Stages that entry's 32 bytes from `LOW_SEG` into the kernel-segment buffer `dsk_ent`; out: SI = `dsk_ent`. Consumers keep an ordinary DS:SI pointer and never see a segment |
| `dsk_get_icon`| in: AX = entry index. Same, 64 bytes into `dsk_ico`; out: SI = `dsk_ico` |
| `dsk_copy_in` | in: SI = `LOW_SEG` offset, DI = kernel offset, CX = even byte count. With `dsk_copy_low`, one of the two routines that load DS=`LOW_SEG` |
| `dsk_copy_low`| module-internal. in: SI = source `LOW_SEG` offset, DI = dest `LOW_SEG` offset, CX = even byte count; out: nothing, clobbers flags. DS=ES=`LOW_SEG` for the `rep movsw` — same discipline as `dsk_copy_in` (no kernel variable readable while DS is switched). Used by the icon harvest (`dsk_secbuf` → `disk_icons`, both `LOW_SEG`). Writing `disk_dir` FROM kernel scratch needs no helper: DS stays kernel, ES=`LOW_SEG`, plain `rep movsw` |

The FAT routines (all UI-task-only like the rest of the module; all in
`.text` — disk code stays hot and near `disk_read`):

| symbol       | contract                                                      |
|--------------|----------------------------------------------------------------|
| `dsk_bpb_check` | module-internal, ~260 B. in: `dsk_secbuf` holds LBA 0. Out: CF=0 — the §18.2 table passed, the §18.1 variables + `disk_spt`/`disk_heads` are filled and `dsk_fattype` is set per §19's detection; CF=1 — reject, nothing guaranteed stored (the caller resets the fallback). Clobbers CF only (internally saves AX,BX,CX,DX,SI,DI). Rule 1 runs via one es: word compare against `dsk_secbuf`+510; the boot sector's first 64 bytes are then staged into `dsk_bpb` via `dsk_copy_in`, and rules 2–16 run on plain DS reads (odd-offset word loads are plain 8086 loads — no alignment requirement exists). |
| `dsk_clus2lba` | ~50 B. in: AX = cluster number. Out: CF=0, AX = first LBA of that cluster; CF=1 if AX < 2 or AX > `[dsk_maxclus]` (AX undefined). Clobbers AX (output), CF. Math: LBA = `dsk_datalba` + (AX−2) × `dsk_spc`, 16-bit mul; cannot wrap (AX ≤ maxclus ⇒ (AX−2)×spc ≤ TotSec16 − datalba, §18.2 rules 14/15); DX≠0 after the mul is treated as CF=1 anyway (belt and braces). |
| `dsk_next_clus` | ~80 B. in: AX = cluster number. Out: CF=0, AX = raw FAT entry value (FAT12: 12-bit); CF=1 if the input is outside [2, `dsk_maxclus`]. Clobbers AX (output), CF. The **single reader of `FAT_SEG`** (§2.1): push ES; ES = `FAT_SEG`. FAT12: off = AX + AX>>1; w = word [es:off] (an unaligned straddle read — legal on 8086; the 3-nibble problem dissolves against a resident FAT); odd cluster → AX = w >> 4, even → AX = w AND 0x0FFF. FAT16: off = AX << 1; AX = word [es:off]. §18.2 rule 16 guarantees off+1 < `dsk_fatsz`×512 for every valid cluster. No EOC/bad-cluster decoding — see `dsk_read_chain`. |
| `dsk_read_chain` | ~170 B. in: AX = first cluster, DX = sectors to read (= ceil(fsize/512), ≥ 1), ES:BX = destination (advances 512/sector, exactly DX sectors). Out: CF=0 all read; CF=1 with AL = 1 (disk error) / 2 (chain corrupt). Clobbers CF; AL on failure (internally saves the rest). **Size-driven walk with run coalescing**, against the unmodified single-sector `disk_read`: keep a (run_lba, run_len) pending run; per cluster, `dsk_clus2lba` (CF → fail AL=2), take = min(`dsk_spc`, DX remaining) — the final cluster may be partial; a cluster whose LBA extends the run grows it, otherwise the run is flushed (`disk_read` AX=run_lba, CX=run_len; CF → fail AL=1) and restarted. When DX reaches 0, flush and return CF=0; else `dsk_next_clus` — the result must be in [2, `dsk_maxclus`], anything else fails AL=2. Bounds: the iteration count is ≤ the caller's DX (each pass consumes ≥ 1 sector) — a looped or cross-linked chain cannot hang the walk, it just yields wrong bytes, which the loader's in-region header recheck rejects (§21). EOC / bad / free / reserved values need **no dedicated constants**: the walk is size-driven, so ANY next-value outside [2, maxclus] while sectors remain is corruption (the FAT spec guarantees bad/EOC marks never collide with valid cluster numbers under correct type detection). On a freshly built (contiguous, §24) image the whole file flushes as ONE `disk_read` call. |

### 18.1 Mount-derived variables (kernel .bss)

Valid only after a successful mount — every consumer is already gated by
`disk_nfiles` ≠ 0 (readers) or by `[dsk_mntok]` (writers, §18.4). 90 bytes
including `dsk_cherr`, `dsk_read_chain`'s failure-code byte carried across
its register-restore epilogue.

```nasm
dsk_bpb:      resb 64  ; staged boot-sector head (mount scratch, §18.2)
dsk_fattype:  resb 1   ; 0 = FAT12, 1 = FAT16 (§19 detection)
dsk_spc:      resb 1   ; BPB_SecPerClus (validated power of two)
dsk_fatlba:   resw 1   ; = RsvdSecCnt (FAT1 start LBA)
dsk_fatsz:    resw 1   ; = FATSz16 (<= DSK_FAT_SECS)
dsk_nfats:    resb 1   ; 1 or 2
dsk_rootlba:  resw 1   ; first root-dir LBA
dsk_rootsecs: resw 1   ; root-dir sector count (<= 32)
dsk_datalba:  resw 1   ; FirstDataSec
dsk_maxclus:  resw 1   ; CountOfClusters+1 = highest valid cluster number
dsk_cherr:    resb 1   ; dsk_read_chain failure code, carried across the
                       ; register-restore epilogue
dsk_op:       resb 1   ; int 13h function for dsk_xfer: 02h read / 03h write
dsk_ioerr:    resb 1   ; last int 13h status (AH) of a FAILED transfer;
                       ; 03h = write-protected media (§18.4)
dsk_mntok:    resb 1   ; 1 between a successful mount and the next mount
                       ; attempt — the §18.4 write gate. A mount that fails
                       ; (or was never run) leaves 0, so an unformatted, an
                       ; unreadable or a NON-FAT disk can never be written
dsk_rover:    resw 1   ; next cluster the allocator examines (§18.4), reset
                       ; to 2 at every mount
dsk_fatd0:    resw 1   ; dirty FAT sector range, [lo, hi] inclusive, sector
dsk_fatd1:    resw 1   ; indices within one FAT; lo = 0FFFFh = clean
```

### 18.2 BPB validation (`dsk_bpb_check`, in check order)

The BPB is **hostile** — the data disk is written by foreign machines
(§19), so every field is attacker-controlled. Each rule protects a
specific downstream consumer; a single failure = mount failure (CF=1,
`disk_nfiles`=0, geometry falls back to 9/2). The two in-kernel failure
modes the table exists to prevent: a divide fault or hang with `sch_lock`
held, and a read landing outside its destination buffer.

| # | Field / derived | Rule | Protects |
|---|-----------------|------|----------|
| 1 | bytes 510..511 | == 0xAA55 | garbage/unformatted-disk gate |
| 2 | BS_jmpBoot[0] | ∈ {0xEB, 0xE9} | MS-spec first validity test; rejects raw data ending in 55 AA |
| 3 | BPB_BytsPerSec | == 512 exactly | `disk_read` moves exactly 512 B/sector; all stride and ×512 math |
| 4 | BPB_SecPerClus | ∈ {1,2,4,8,16,32,64,128} | cluster→LBA mul bounds; 0 ⇒ CountOfClusters div-by-zero |
| 5 | BPB_RsvdSecCnt | ≥ 1 | FAT LBA math (FAT starts at RsvdSecCnt) |
| 6 | BPB_NumFATs | ∈ {1,2} | FAT2-fallback logic; layout math |
| 7 | BPB_RootEntCnt | ≥ 1, ≤ 512, (RootEntCnt×32) mod 512 == 0 | FAT12/16 must have a root dir (spec); ≤512 bounds the mount stall (≤32 root sectors); whole-sector count is a spec MUST |
| 8 | BPB_TotSec16 | ≠ 0 | 16-bit LBA bound: TotSec16==0 ⇒ the count lives in TotSec32 ⇒ ≥65,536 sectors ⇒ unaddressable by the AX=LBA `disk_read` contract. **Documented rejection.** TotSec32 otherwise ignored (some formatters set both; harmless) |
| 9 | BPB_Media | ∈ {0xF0, 0xF8..0xFF} | spec-legal set; cheap garbage gate. FAT[0]'s media echo is NOT checked (spec says don't rely on it) |
| 10 | BPB_FATSz16 | ≥ 1 and ≤ `DSK_FAT_SECS` (10) | ≥1: layout math. ≤10: the FAT snapshot buffer is 5,120 B (§2.1). Covers every real floppy FAT this OS boots or builds: 360K=2, 720K=3, 1.2M=7, 1.44M=9. Larger ⇒ documented mount failure — **including every FAT16 volume**, which needs ≥ 4,085 clusters and so ≥ 16 FAT sectors (§2.1) |
| 11 | BPB_SecPerTrk | ∈ {8, 9, 15, 18, 21, 36} | whitelist of real floppy geometries; a hostile spt×heads product past 16 bits would zero `disk_read`'s CHS divisor → divide fault with `sch_lock` held |
| 12 | BPB_NumHeads | ∈ {1, 2} | same divisor protection |
| 13 | TotSec16 coherence | TotSec16 ≤ SecPerTrk × NumHeads × 80 | every in-volume LBA is CHS-reachable under `disk_read`'s cyl<80 guard — files can't mount-list then die "Disk error" on geometry grounds |
| 14 | FirstDataSec | FirstDataSec + SecPerClus ≤ TotSec16 | at least one cluster exists; DataSec underflow guard |
| 15 | CountOfClusters | ≥ 1 and < 65,525 | empty-data-area rejection; FAT32 insurance (§19) |
| 16 | FAT capacity | FAT12: ceil((CountOfClusters+2)×1.5) ≤ FATSz16×512; FAT16: (CountOfClusters+2)×2 ≤ FATSz16×512 | a FAT too small for its own cluster count would send `dsk_next_clus` past the snapshot into garbage |
| 17 | BPB_HiddSec | ignored | floppies are unpartitioned; accepting nonzero keeps odd-but-readable disks mountable |

Rules 3/4/11/12 are the hard no-fault rules; 7/8/10/13/16 bound every loop
and every LBA the driver ever computes; the rest are spec conformance. On
success, `disk_spt`/`disk_heads` are loaded from rules 11/12's fields and
the §18.1 variables from the derived layout.

### 18.3 Mount sequence (`disk_mount`)

1. **Boot sector.** Store DL; set fallback 9/2, `disk_nfiles`=0,
   `dsk_mntok`=0 (the §18.4 write gate closes for the whole mount — a torn
   mount leaves it closed), `dsk_rover`=2, the FAT dirty range clean, and
   `ld_pending`=0 (a queued click indexes the directory being replaced —
   it must never run against the new one). Read
   LBA 0 into `dsk_secbuf` with the fallback geometry (CHS 0/0/1 under
   any geometry — the existing bootstrap trick). `dsk_bpb_check` (§18.2);
   CF → fail.
2. **FAT snapshot.** Read `dsk_fatsz` sectors from `dsk_fatlba` into
   `FAT_SEG`:0 (ES=`FAT_SEG`, one `disk_read` call). On failure and
   `dsk_nfats`==2, retry once from FAT2 (`dsk_fatlba` + `dsk_fatsz`);
   both fail → mount fails. (FAT2 is the one redundancy FAT gives for
   free.)
3. **Root-dir scan → synthesized directory.** For each root sector
   (0..`dsk_rootsecs`−1): read it into `dsk_secbuf` (read failure →
   mount fails); classify each of its 16 raw 32-byte entries per §19's
   species rules — a 0x00 first name byte **stops the whole scan** — and
   for each accepted entry synthesize the §19 staged entry in `dsk_ent`
   (kernel scratch — DS never leaves the kernel segment while parsing;
   raw fields are read via es: from `dsk_secbuf`), then `rep movsw` it
   out to `disk_dir` + accepted×32 (ES=`LOW_SEG`). Stop when 32 entries
   are accepted (§19 cap).
4. **Icon harvest.** For each accepted entry i: type≠1 → zero the 64-byte
   slot `disk_icons` + i×64 (ES=`LOW_SEG` `rep stosw`) — the generic-icon
   sentinel. Type=1 (cluster already validated, §19): `dsk_clus2lba`,
   `disk_read` 1 sector into `dsk_secbuf`; stage the 32 header bytes into
   `dsk_ent` via `dsk_copy_in`; if magic word @0 == 0x384F, version byte
   == 2 and flags bit 0 set, `dsk_copy_low` bytes 32..95 of `dsk_secbuf`
   → `disk_icons` + i×64; in every other case (read failure included)
   zero the slot. **Harvest failures are non-fatal** — the entry stays
   listed; a later load attempt reports the real error. `disk_icons` is
   fully rewritten every mount (preserves §29.1's rule that I_ICON may
   never point at it).
5. Set `disk_nfiles` = accepted count, `dsk_mntok` = 1 (the write gate opens
   only here, as the last act of a complete mount); CF=0.

Mount I/O budget, honest numbers: sectors read = 1 + FATSz (2..32) + root
sectors actually scanned (early exit at the 0x00 terminator) + one per
type-1 entry. Shipped apps disk: 1+9+1+5 = **16** (vs 7 under os88fs).
FAT16 test disk (§19): 1+23+1+5 = 30. Hostile ceiling: 1+32+32+32 = **97**
one-sector reads — on real hardware at worst one sector per revolution
that approaches ~20 s, and retries can stretch it further; bounded,
hostile-media-only, and accepted (§22 notes the user-visible stall). QEMU:
effectively instant.

### 18.4 diskw.inc — the FAT write path

`disk.inc` reads; **`kernel/diskw.inc` writes**, prefix `dskw_`. It is the
one module that may modify a data floppy, and the only module that calls
`disk_write`. Five public routines are the file surface — the same five
the API exposes to packages (§20.3) — because both the OS and its apps get
exactly one vocabulary: whole files by name, in the volume's **current
directory** (§19.2 — `[dsk_cwd]`, which `dsk_chdir` moves). There is no
open/seek/handle model, no paths, no partial rewrite: a file is written in
one call from one buffer, read back the same way, and its name is resolved
in exactly one directory — the one the volume is currently sitting in.
`dskw_mkdir` (§18.5) is the sixth routine and `dskw_rmdir` (§18.6) the
seventh: the only two that act on a directory rather than on a file, and
both kernel-internal for the same reason (a package cannot navigate). That
is the largest subset that stays honest in 256KB with 12 pre-emptive tasks
and no disk cache.

**Context (binding).** UI task only, exactly like every other int 13h
caller (§18). Window callbacks, menu handlers, file-dialog completion procs
and package entry procs qualify, and a
callback holds the gfx lock, so a write **stalls painters** for its
duration, on the same terms as `disk_mount` under the lock in §22. Never
from an ISR, and **never from a task — including a package's own worker
task (§20.6)**. This is not a consequence of packages being task-less
(since §20.6 they need not be): the write path shares `dsk_secbuf`, the
resident FAT snapshot and `[sch_lock]` with the read path and with
`disk_mount`, and is serialized by nothing but the single-task rule. A
second concurrent caller cross-links the FAT and corrupts the volume,
silently — the commit-order and rollback rules below protect against a
*crash*, not against a *second writer*. The kernel cannot enforce this; it
is an author rule with nothing behind it.

**Gate.** Every routine refuses unless `[dsk_mntok]` = 1 (§18.1) — i.e. a
`disk_mount` of *this* drive has fully succeeded, BPB validation and all.
The boot floppy is protected by construction rather than by a special case:
its sector 0 is a boot sector with no valid BPB, so drive 0 fails §18.2 and
can never be the mounted volume.

**Everything the read path validated stays validated.** Writes derive every
LBA from the §18.1 layout, never from a raw on-disk field: cluster numbers
come from `dsk_clus2lba` (which enforces [2, `dsk_maxclus`]), FAT offsets
from `dsk_maxclus` and §18.2 rule 16, and directory LBAs from
`dsk_dirw_next` (§19.2), which bounds the root by `dsk_rootsecs` and a
subdirectory's chain by both `dsk_clus2lba` and a sector-count guard. A
hostile disk can make a write **fail**; it can never make one land outside
the volume.

| symbol | contract |
|--------|-----------|
| `dskw_write` | in: SI → NUL-terminated 8.3 name (DS), ES:BX → the bytes, DX:CX = byte count (32-bit, DX high; 0 = create an empty file). Creates or **replaces** the file. Out: CF=0, AX=0; CF=1, AX = `FERR_*` — `FERR_BIG` for a count ≥ 32MB, which no volume this kernel mounts can hold. Preserves all other registers, ES and DX included. |
| `dskw_read` | in: SI → name, ES:BX → destination, DX:CX = its capacity in bytes (32-bit, DX high). Out: CF=0, **DX:AX** = bytes read (= the file's 32-bit size); CF=1, AX = `FERR_*` — `FERR_BIG` when the file does not fit DX:CX, decided from the directory entry before any data I/O, so **nothing is written** to the buffer in that case. DX is an output: it does not survive the call. |
| `dskw_delete` | in: SI → name. Frees the chain and marks the directory entry deleted (0E5h). Out: CF=0, AX=0; CF=1, AX = `FERR_*`. |
| `dskw_rename` | in: SI → old name, DI → new name. Same directory, name bytes only — chain, size, attribute and timestamps are untouched. Refuses `FERR_EXIST` if the new name already exists. Its protection mask is **0x0F, not 0x1F** (see below), so a *subdirectory* may be renamed. Out: CF/AX as above. |
| `dskw_dfree` | out: CF=0, DX:AX = free bytes (32-bit), BX = **sectors** per cluster (not bytes — `spc`×512 overflows 16 bits at the §18.2-legal `spc` = 128); CF=1, AX = `FERR_*`. Counts free entries across the resident FAT snapshot; no disk I/O. |
| `dskw_mkdir` | in: SI → name. Creates a subdirectory in the current directory (§18.5). Out: CF/AX as above. Not an API slot — kernel-internal, because a package has no way to navigate. |
| `dskw_rmdir` | in: SI → name. Removes an **empty** subdirectory of the current directory (§18.6): `FERR_PROT` if it holds anything, if it is not a directory, or if it is read-only/hidden/system/label. Out: CF/AX as above. Not an API slot, for `dskw_mkdir`'s reason. |

**Error codes (pinned; returned in AX with CF=1, mirrored as `FERR_*` in
`apps/os88api.inc`):** 0 ok, 1 no mounted disk, 2 disk I/O error, 3 bad
name or argument, 4 no such file, 5 name exists, 6 disk full, 7 root
directory full, 8 entry is protected (read-only, hidden, system, volume
label or subdirectory — *except* under `dskw_rename`, whose mask is 0x0F,
and under `dskw_rmdir`, where it also means "the folder is not empty"),
9 media write-protected, 10 too large.

**Names (`dskw_name83`) — the inverse of §19's synthesis.** A caller
supplies the display form (`"NOTES.TXT"`); the module produces the raw
11-byte space-padded field. Rules, all rejections `FERR_NAME`: 1..8 stem
characters, an optional `.` plus 1..3 extension characters, nothing after
that, NUL-terminated within 12 characters. Lower case is folded up. The
legal set is the FAT short-name set — `A-Z 0-9 $ % ' - _ @ ~ \` ! ( ) { } ^ # &`
— so spaces, path separators, wildcards, `+ , ; = [ ] :` and every byte
below 0x21 or above 0x7E are refused. A leading 0xE5 byte cannot arise
(0xE5 is not in the legal set), and neither can `.` or `..`.

**Allocation.** The resident FAT snapshot (`FAT_SEG`, §2.1) is the
authority: `dskw_alloc` scans it from `[dsk_rover]` (wrapping once through
2..`dsk_maxclus`), claims the first entry that reads 0 by writing the
end-of-chain mark **immediately** (0FFFh / 0FFFFh — ≥ the spec's EOC
floor, so a foreign fsck reads the chain the same way this kernel does),
advances the rover past it, and returns the cluster. Claiming on the spot
is what makes the multi-cluster loop safe: a half-built chain can never
hand the same cluster out twice. `dskw_free_chain` walks a chain writing
zeros, bounded by `dsk_maxclus` iterations so a looped or cross-linked
chain (a foreign machine's corruption) terminates instead of hanging with
`[sch_lock]` held. Every snapshot write goes through `dskw_setfat`, the
mirror of `dsk_next_clus` — ES-only, FAT12 nibble straddle handled by a
read-modify-write of the straddling word, FAT16 a plain word store — which
also widens the dirty range `[dsk_fatd0, dsk_fatd1]` to cover the touched
sector (**both** sectors when a FAT12 entry straddles a boundary).

**`dskw_flush`** writes the dirty sector range back to FAT1 and, when
`[dsk_nfats]` = 2, to FAT2 at the same offsets, then marks the range
clean. Flushing the range rather than the whole FAT is what keeps a save
down to a couple of sector writes instead of 18. The two copies are always
written from the same snapshot bytes, so FAT1 ≡ FAT2 holds after every
operation — `tools/os88disk.py --verify` checks exactly that.

**Write ordering (binding).** `dskw_write` commits in this order and no
other:

1. allocate the new chain in the snapshot and write its data clusters
   (partial final sector staged through `dsk_secbuf`, see below);
2. `dskw_flush` — the new chain is now durable on disk. The old chain, if
   the file existed, is still allocated and still owned by the still-old
   directory entry; the two chains are disjoint by construction, so a
   power failure here leaves **both** files' bytes intact and the volume
   coherent (the new chain is merely orphaned);
3. write the directory entry — a single sector write, and **the commit
   point**: the name now means the new bytes;
4. free the old chain in the snapshot and `dskw_flush` again.

A crash between 3 and 4 leaks the old chain as lost clusters — recoverable
by any host `fsck`/`chkdsk`, and never a cross-link and never lost data.
The reverse order (free first, then write) would risk exactly that, so it
is forbidden.

**Rollback.** Any failure *before* the commit reloads the FAT snapshot from
disk (`dskw_refat`, one `disk_read` of `dsk_fatsz` sectors) and clears the
dirty range, so a partially allocated chain never survives in RAM to be
flushed by a later, unrelated operation. Failures at or after the commit
report the error but leave the file switched over — that is what the
ordering above buys.

**No memory ever leaks past EOF.** When the byte count is not a sector
multiple, the final sector is staged into `dsk_secbuf`: the tail bytes
copied out of the caller's ES:BX, the remainder of the 512 **zeroed**, then
one `disk_write`. The kernel never hands a foreign machine the contents of
whatever happened to sit after a package's buffer.

**Directory entries.** `dskw_find` scans the **current** directory (§19.2)
one sector at a time through `dsk_secbuf` — sector LBAs from
`dsk_dirw_start`/`dsk_dirw_next`, so the root's flat run and a
subdirectory's cluster chain are the same code — in the §19 species order,
comparing the raw
11-byte name field; it reports the entry's (sector LBA, offset) and, on the
way, the first free slot it saw (a 0xE5 entry, else the 0x00 terminator) so
a create needs no second scan. A slot taken at the 0x00 terminator also
gets the *following* entry's first byte zeroed when it lies in the same
sector, so the end-of-directory marker never disappears. Entry writes are
read-modify-write of the containing sector — read it again, patch the 32
bytes, write it back — so a stale `dsk_secbuf` can never write back
neighbouring entries from some other sector. New entries carry attribute
`ARCHIVE` (0x20) and the current date and time from the system clock (§37),
encoded in the FAT form (date = (year−1980)<<9 | month<<5 | day; time =
hour<<11 | minute<<5 | second>>1) in the create-, write- and access-date
fields; a replace updates size, first cluster and the write timestamp.
Entries whose attribute byte carries read-only, hidden, system, volume-label
or subdirectory are never modified — `FERR_PROT`.

**The protection mask is 0x1F everywhere except rename, where it is 0x0F
(binding).** Replace and delete must keep the subdirectory bit in the mask:
overwriting a folder's entry with a file's would strand its whole subtree,
and freeing a folder's chain as if it were a file's would free the children's
directory sectors while leaving the children's own chains allocated — that is
what `dskw_rmdir` (§18.6) exists to do properly. **Rename is different in
kind**: it moves the eleven name bytes and nothing else — not the attribute,
not the first cluster, not the size, not a timestamp. Nothing on the volume
records a directory by *name*: a child's `..` holds the parent's first
**cluster**, its own `.` holds its own, and the parent's entry keeps pointing
at the same cluster it always did. So renaming a folder cannot break the walk
back up, and the mask drops the 0x10 bit for that one path. The other four
bits stay: a read-only, hidden or system entry is still refused, and the
volume-label entry (0x08) is emphatically not a file to be renamed.

**Long file names are invisible here, and that has one visible edge.** LFN
entries are skipped by the scan, never matched and never reused (§19), so
the write path only ever sees short names. Deleting or renaming a file a
host created with a long name therefore leaves that name's LFN entries
orphaned — harmless (every host ignores or reclaims them) but real, and the
reason the shipped apps disk and everything os8088 writes use plain 8.3
names.

**Cache coherence.** A successful `dskw_write` / `dskw_delete` /
`dskw_rename` / `dskw_mkdir` / `dskw_rmdir` ends in `dskw_sync`, which **remounts the
current drive** (§18.3) with `[dsk_keepcwd]` raised, exactly as `dsk_chdir`
does (§19.2). Raising it is **binding**, not an optimisation: a bare
`disk_mount` resets `[dsk_cwd]` to the root by design, so without it every
successful write performed inside a subdirectory would silently teleport
the volume back to the root — the file manager would keep showing a folder
whose contents it was no longer listing, and the *next* write would resolve
its name in the root instead. It costs
the §18.3 mount budget (16 sector reads on the shipped disk, instant under
QEMU) and it keeps the system's single strongest disk invariant intact:
`disk_dir`, `disk_icons` and `disk_nfiles` are *always* exactly a mount
snapshot, never a patched one. No new staleness rule enters the kernel, a
package that writes a `.o88` gets its icon harvested for free, and the
directory indices the Disk window and loader use stay meaningful. The
remount cannot repaint the Disk window (the caller may hold the gfx lock),
so an open Disk window shows the new listing at its next repaint or
Refresh (§22).

**The cost of that ordering**, stated plainly: a replace holds both chains
at once, so rewriting a file needs room for the new copy *beside* the old
one, and a nearly full disk can refuse a save it could have done in place
(`FERR_FULL`). That is the trade — an in-place rewrite risks the user's
existing bytes on every power glitch, and this OS has no journal to make
that safe.

**The gate package.** `tests/filetest/filetest.asm` (§20.3's slots, driven
end to end: write, read-back-and-compare, oversize-buffer refusal, shrink
replace, empty file, rename both ways, rename-onto-existing, bad name,
delete twice, fill-to-refusal, mass delete, free-space equality). Like
`fmtest`/`sbtest` it never ships on the apps disks — a gate package is not
software anyone would want to launch — and rides its own scratch images:
`build/filetest.img` (FAT12),
and `-frag` (`--scramble`d, so allocation and free meet holes). There was a
third, `-fat16`, on the 2.88M test geometry — the only way to exercise the
FAT16 entry encoding; it went when `DSK_FAT_SECS` fell to 10 and rule 10
(§18.2) began rejecting every FAT16 volume there can be (§2.1).
Run it with `make test-snd TESTAPPS=build/filetest.img`, then check the
volume from the host with `tools/os88disk.py --verify` — the in-kernel
free-space check and the host fsck catch different leaks, and both are part
of the gate.

**What this deliberately does not do.** No paths (a name is resolved in the
current directory and nowhere else), no *recursive* directory removal —
`dskw_rmdir` (§18.6) takes an empty folder and refuses anything else, so
emptying a subtree is the user's job one level at a time — no append or seek, no
truncate-in-place, no FAT32, no volume-label editing, no timestamp
preservation across a rewrite, and no attempt to defragment: chains are
allocated first-fit from the rover, so a full disk fragments exactly the
way DOS's did. What files are **not** capped at is 64KB — see §18.4.1.

### 18.4.1 One read, one write, and no 64KB ceiling on either

There were three routines here and there are two. `dskw_read` and
`dskw_write` are the whole read/write surface, for the kernel and for
packages alike, and neither has a size limit that a FAT floppy does not
already impose.

```
dskw_write  in   SI -> NUL 8.3 name, ES:BX -> the bytes, DX:CX = the byte
                 count (32-bit, DX high; 0 = an empty file)
            out  CF=0, AX = 0; CF=1, AX = FERR_*. DX preserved.

dskw_read   in   SI -> NUL 8.3 name, ES:BX -> the destination, DX:CX = its
                 capacity in bytes (32-bit, DX high)
            out  CF=0, DX:AX = bytes read (the file's 32-bit size);
                 CF=1, AX = FERR_* - FERR_BIG decided from the directory
                 entry's 32-bit size BEFORE any data I/O, destination
                 untouched. DX does not survive the call.
```

**The mechanism is one routine, `dskw_norm`, called once at the top of each
pipeline.** It folds the whole paragraph part of BX into ES, leaving an
offset of 0..15; the transfer loop then holds that offset and advances the
**segment** by 32 paragraphs per 512-byte sector. An offset under 16 plus
512 can never carry, so the 16-bit offset horizon is unreachable by
construction, and `dsk_xfer`'s per-sector int 13h granularity (§18.1) is
untouched — the multi-sector call this replaced was already a software loop
inside `dsk_xfer`, so nothing got slower.

**That is why the destination stays `ES:BX` rather than becoming a base
segment.** A base-segment contract would have forced every caller with a
small fixed buffer to find a segment run for it, and the kernel has one of
those it must not lose: `drv_cfg_load` reads `SYSTEM.CFG` (§51.5) into 64
bytes of `.bss` **at boot**, where a heap claim is something that can be
refused. The superset costs about ten bytes of code and removes the entire
16-bit read path, so it is smaller than what it replaced.

**A hostile size is still refused up front.** A size field whose sector
count would not fit 16 bits (≥ 32MB) cannot be a real chain on any volume
this kernel mounts (TotSec16 ≤ 65,535): on a read that is `FERR_IO`, because
a wrapped count could otherwise make the chain walk stop early and report
success over a hostile entry (§18 — every byte off the disk is hostile
input); on a write it is `FERR_BIG`, since the caller asked for something no
volume could hold. Neither is reachable from a legal floppy.

**What went, and what it cost.** `dskw_readbig` and its two private bodies
are gone, and so is the "the buffer must not wrap its segment" argument
check that both pipelines used to open with — a buffer that spans segments
is now the *normal* case, not an error. **API slot 0x01E8 is retired but not
reused** (§20.8 rule 4): the cell still exists and answers CF=1 with
`FERR_NAME`, so every slot above it keeps its number, and `apps/os88api.inc`
publishes no `OSAPI_FILE_READBIG`, so a package source that still names it
fails to assemble instead of silently calling a routine whose BX it never
set. Slots 0x0120 and 0x0128 **changed contract**, which is the one thing
§20.8 otherwise forbids; it is a deliberate, recorded exception, taken while
every package that calls them is still in this tree and rebuilt by `make`.

**It allocates nothing**, in either direction. There is no staging buffer
and no growth in the kernel's span: the caller supplies the memory, which
for anything large is an `OSAPI_MEM_CLAIM` grant (§50.3), because a
package's own region caps at one segment. Only a partial final sector
stages, through the same `dsk_secbuf` both directions already used.

`tests/filetest` checks 2..5 cover it, against a 96KB `BIG.DAT` whose byte
at offset i is `i >> 9` — one distinct value per sector, so a buffer that
failed to advance reads a *different* byte rather than a plausible one. The
probe is offset 0x11111, past the horizon the API used to stop at. Check 2
reads it into a claim at offset **0x33** — neither zero nor a paragraph
multiple, so `dskw_norm`'s arithmetic is what the probe is testing; check 3
writes those 96KB straight back out from the same `ES:BX`; check 4 reads the
copy back at offset 0; check 5 deletes it, so §18.4's free-space equality
check still closes over the whole run.

### 18.5 `dskw_mkdir` — creating a subdirectory

```
dskw_mkdir   in:  SI -> NUL-terminated 8.3 name (DS), same rules as every
                  other dskw_* name (dskw_name83, §18.4)
             out: CF=0 with AX=0, or CF=1 with AX = FERR_*
             clobbers: nothing else. UI-task/window-callback context, and
                  gated on [dsk_mntok] like every other write.
```

Creates one directory in the volume's **current** directory (§19.2). It is
kernel-internal and deliberately **not** an API slot: a package cannot
navigate, so a package that could create a folder could never enter it, and
the slot would only be a way to litter the user's disk.

**Commit order (binding, and it is §18.4's order for the same reason).**

1. `dskw_find` the name — `FERR_EXIST` if it is taken, `FERR_DIRFULL` if
   the scan reached the end of the directory with no reusable slot.
2. `dskw_alloc` one cluster (marked end-of-chain in the snapshot on the
   spot) and **write it in full**: sector 0 is `.` and `..` built by
   `dskw_dotents` with the rest of the sector zeroed, and every remaining
   sector of the cluster is written all-zero. Writing the whole cluster is
   not tidiness: a later slot search that reached uninitialised sectors
   would read whatever the disk last held there and could match a name that
   is not in this directory at all.
3. `dskw_flush` — the FAT is durable, so the cluster is no longer free.
4. Write the parent's directory entry: one sector, attribute `DSKW_DIR`
   (0x10), size field 0 per spec, first cluster = the new cluster. **This
   is the commit point.**

A crash before step 4 leaks a formatted-but-unreferenced cluster, which any
host `fsck` reclaims. The reverse order — entry first, then contents —
could leave a directory entry pointing at a cluster that was never
initialised, which is a directory full of garbage rather than a lost one.
Every failure ahead of the commit runs `dskw_refat`, so a half-built chain
never survives in RAM to be flushed by some later, unrelated write.

**`.` and `..` (`dskw_dotents`).** The new directory is born with its own
two links: `.` → its own cluster, `..` → `[dsk_cwd]`. When the parent *is*
the root, `[dsk_cwd]` is 0 — which is both the FAT spec's convention for
"parent is the root" and this kernel's own value for the root, so nothing
translates. That is what lets `dsk_dotdot` (§19.2) walk back up with no
path stack and no memory of how the user got here: the disk records it.

**`[dskw_isdir]` lives in `.text` with a `db 0` initialiser, not in
`.bss`** — the same binding rule, and the same hard-won reason, as
`[dsk_keepcwd]` (§19.2). It is the flag `dskw_commit` reads to stamp
`ATTR_DIRECTORY` instead of `ARCHIVE`, and `dskw_mkbody` is the only thing
that ever *sets* it, so on a system where no folder has yet been created it
is only ever read. `-f bin` gives `.bss` no image bytes and nothing zeroes
it, so an uninitialised `.bss` byte reads back as whatever the machine left
there — reliably **non-zero** on the hardware this was found on. Left
uninitialised, this byte made every file the OS created carry attribute 0x10;
every reader then refused it as `FERR_PROT` (a directory is protected), so
nothing could be read back or deleted and every chain those writes
allocated leaked. The `filetest` gate caught it and `--verify` did not,
which is precisely why §18.4 makes both part of the gate.

**Error set:** `FERR_NODISK`, `FERR_NAME`, `FERR_EXIST`, `FERR_DIRFULL`,
`FERR_FULL`, `FERR_IO`, `FERR_WPROT`. On success `dskw_sync` remounts with
`[dsk_keepcwd]` raised (§18.4), so the new folder appears in the listing
**and the volume stays where it was** — the whole point of creating a
folder inside another one.

### 18.6 `dskw_rmdir` — removing an empty subdirectory

```
dskw_rmdir   in:  SI -> NUL-terminated 8.3 name (DS), same rules as every
                  other dskw_* name (dskw_name83, §18.4)
             out: CF=0 with AX=0, or CF=1 with AX = FERR_*
             clobbers: nothing else. UI-task/window-callback context, and
                  gated on [dsk_mntok] like every other write.
```

The counterpart to §18.5, and the reason **Delete** in §22 is not an item
that can only ever fail. It removes one *empty* subdirectory of the volume's
current directory (§19.2). `.` and `..` cannot be named (`dskw_name83`
refuses a leading dot), and the root has no directory entry, so neither can
be the target.

**Refusals, all `FERR_PROT`:** the entry is not a directory (attribute bit
0x10 clear — the caller wanted `dskw_delete`); it carries read-only, hidden,
system or volume-label (0x0F); or **it is not empty**. A first cluster
outside [2, `dsk_maxclus`] is `FERR_IO`: a directory entry always has a real
cluster, so anything else is corruption, and refusing beats walking it.

**The emptiness scan is the whole safety argument, so it is conservative by
construction.** The target's own chain is walked with
`dsk_dirw_start`/`dsk_dirw_next` (§19.2 — bounded by `DSK_DIRW_MAX`, so a
cross-linked chain terminates the scan instead of hanging it with
`[sch_lock]` raised) and every 32-byte slot must be one of exactly four
things: `0x00` (end of directory — the scan stops there and the folder is
empty), `0xE5` (a deleted entry), or a name whose first byte is `.` **and
whose second is `.` or a pad space** — i.e. one of the two dot links, and not
a foreign disk's file called `.XT`. **Anything else means occupied**,
including an LFN entry a host left behind. That is deliberately stricter than a host `rmdir`: the cost
of refusing a folder someone else could delete is one message, and the cost
of deleting an occupied one is every child's clusters leaked at once — the
children's directory *sectors* live in the cluster being freed, so once it is
gone nothing on the volume names them any more.

**Commit order (binding — it is `dskw_delete`'s, and for `dskw_delete`'s
reason).**

1. `dskw_find` + `dskw_ent_load` the parent's entry; run the checks above.
2. Write `0xE5` into that entry — one sector, **the commit point**.
3. `dskw_free_chain` the directory's own cluster chain, then `dskw_flush`.

A crash between 2 and 3 leaks the folder's cluster as lost clusters, which
any host `fsck` reclaims. The reverse order would leave a live directory
entry pointing at free space that the next write could hand to someone else.
Nothing is allocated before the commit, so — exactly as in `dskw_delete` —
there is no half-built chain and therefore no `dskw_refat` rollback to run;
the FAT snapshot is only ever touched *after* the entry is already gone.

**Error set:** `FERR_NODISK`, `FERR_NAME`, `FERR_NOENT`, `FERR_PROT`,
`FERR_IO`, `FERR_WPROT`. On success `dskw_sync` remounts with `[dsk_keepcwd]`
raised (§18.4), so the parent listing loses the folder and the volume stays
where it was.

**What it does not do:** no recursion. Deleting `TOOLS` that holds `SUB` is
two operations in the user's hands, in that order, and `os8088` will not
guess at the second.


### 18.90 `dskw_rmtree` — removing a subdirectory and everything under it

`dskw_rmdir` refuses an occupied folder, which is the right answer for a
primitive and the wrong answer for File > Delete — the Disk window is where a
user deletes things, and "Protected" was all it could say about a folder with
one file in it. `dskw_rmtree` is what that menu item calls now; the
confirmation line says `+contents` when the target is a folder, and there is
no undo on this system, which is exactly why the line is a confirmation and
not a click.

**It is iterative, and the stack it does not have is the disk's own.** A
recursive descent would need a frame per level on a 1,536-byte task stack (§8)
and a depth bound nobody could justify. Every subdirectory records its parent
in its `..` entry (§19.2), so the walk goes down with `dsk_chdir` and comes
back up with `dsk_dotdot`, carrying nothing between iterations but the
volume's position. One rule, applied to whatever directory it is standing in:

- a **file** → delete it (`dskw_dbody_n83`)
- a **folder** → step into it (`dsk_chdir`)
- **nothing** → step out, and remove the folder just emptied

The folder just left is re-found in the parent **by its first cluster**
(`dskw_rt_byclus`), not by a name the walk would have had to carry: the
cluster is what it already holds, and it cannot be ambiguous the way a name
that failed to convert to 8.3 could.

Four properties are load-bearing:

- **Every removal still goes through `dskw_rmbody_n83`**, which re-runs the
  emptiness scan. If the scan in `dskw_rt_scan` and the one in `dskw_isempty`
  ever disagreed, the outcome is a refusal — never a lost subtree.
- **A protected file stops it.** `dskw_dbody_n83` still refuses read-only,
  hidden and system entries, so a tree containing one stops there, partially
  deleted, with `FERR_PROT`. That is what any `rm -r` does when it meets a
  permission it does not have, and it is much better than silently removing
  files another operating system marked.
- **LFN fragments are removed, and only they are removed by slot.** An entry
  whose attribute is exactly 0x0F carries no cluster chain — its FstClusLO is
  part of a checksum field — so `dskw_rt_zap` marks the slot 0xE5 and frees
  nothing. `dskw_find` refuses to match an LFN entry at all (§18.4), which is
  why the scan publishes `[dskw_dsec]`/`[dskw_doff]` itself and the zap works
  from the location rather than the name. Without this, no folder a host wrote
  a long name into could ever be deleted — which is most of them.
- **`DSKW_RT_MAX` (4,096 iterations) bounds the whole operation.** A cyclic
  `..` link looks exactly like a legitimate one from where the walk stands, so
  a guard is the only answer; no real floppy can reach it.

An unreadable directory is not an empty one: the scan records the I/O error
and the walk fails rather than freeing the clusters of files it could not see.
Whatever happens — success, refusal, or a read error half way down — the
volume is put back in the directory the call started in.

## 19. FAT12/FAT16 — the data-disk format (data floppies)

The data floppy (drive B:) is a standard **FAT12** volume — mountable and
writable by IBM PC DOS 2.0+, Windows, macOS and Linux. That is the point:
the disk is a shared medium, written by foreign machines and by os8088
alike. The kernel implements a FAT12/FAT16 subset covering exactly what
§22 and §18.4 need — mount, enumerate a directory, walk cluster
chains (`disk.inc`), navigate into and out of subdirectories (§19.2), and
create/replace/delete/rename whole files plus create and remove empty subdirectories in the
current directory (`diskw.inc`). It never
touches the boot sector or the BPB. Consequence, unchanged by write
support: everything on the disk is untrusted, and every field the kernel
reads is validated per §18.2 **before** any write derives an LBA from it.
Not bootable in earnest (the boot sector is a message stub, below). All
words little-endian. Sector size 512.

**Shipped geometries** (what §24's os88disk.py emits — canonical DOS
formats):

| BPB field (off, size)      | 1.44MB            | 360KB             |
|----------------------------|-------------------|-------------------|
| BS_jmpBoot (0, 3)          | `EB 3C 90`        | `EB 3C 90`        |
| BS_OEMName (3, 8)          | `"MSDOS5.0"`      | `"MSDOS5.0"`      |
| BPB_BytsPerSec (11, 2)     | 512               | 512               |
| BPB_SecPerClus (13, 1)     | 1                 | 2                 |
| BPB_RsvdSecCnt (14, 2)     | 1                 | 1                 |
| BPB_NumFATs (16, 1)        | 2                 | 2                 |
| BPB_RootEntCnt (17, 2)     | 224               | 112               |
| BPB_TotSec16 (19, 2)       | 2880              | 720               |
| BPB_Media (21, 1)          | 0xF0              | 0xFD              |
| BPB_FATSz16 (22, 2)        | 9                 | 2                 |
| BPB_SecPerTrk (24, 2)      | 18                | 9                 |
| BPB_NumHeads (26, 2)       | 2                 | 2                 |
| BPB_HiddSec (28, 4)        | 0                 | 0                 |
| BPB_TotSec32 (32, 4)       | 0                 | 0                 |
| BS_DrvNum (36, 1)          | 0                 | 0                 |
| BS_Reserved1 (37, 1)       | 0                 | 0                 |
| BS_BootSig (38, 1)         | 0x29              | 0x29              |
| BS_VolID (39, 4)           | 0x88000888 fixed  | 0x88000888 fixed  |
| BS_VolLab (43, 11)         | `"OS8088APPS "`   | `"OS8088APPS "`   |
| BS_FilSysType (54, 8)      | `"FAT12   "`      | `"FAT12   "`      |
| boot code (62..509)        | message stub      | message stub      |
| signature (510, 2)         | 0x55 0xAA         | 0x55 0xAA         |

Derived layout (all LBAs volume-relative = disk-absolute; unpartitioned):

|                     | 1.44MB                  | 360KB                 |
|---------------------|-------------------------|-----------------------|
| FAT1 / FAT2         | LBA 1–9 / 10–18         | LBA 1–2 / 3–4         |
| Root dir            | LBA 19–32 (14 sec)      | LBA 5–11 (7 sec)      |
| First data sector   | LBA 33                  | LBA 12                |
| CountOfClusters     | 2847 (clusters 2..2848) | 354 (clusters 2..355) |
| FAT type            | FAT12 (2847 < 4085)     | FAT12 (354 < 4085)    |
| FAT bytes needed    | 2849×1.5 = 4274 ≤ 4608  | 356×1.5 = 534 ≤ 1024  |
| FAT[0..1] reserved  | `F0 FF FF`              | `FD FF FF`            |

**There was a test-only third geometry** — 2.88M ED, 23 FAT sectors,
CountOfClusters 5698 ≥ 4085 ⇒ FAT16 — built by neither the Makefile nor the
shipped disks, and it existed for one reason: to give the FAT16 entry
encoding a positive test. It is gone (§2.1). `DSK_FAT_SECS` is 10 now, below
the 16 FAT sectors a volume must have before it can be FAT16 at all, so
rule 10 rejects the whole class before a byte of the FAT is read. The FAT16
halves of `dsk_next_clus` and `dskw_setfat` remain in the tree, unreachable;
restoring the geometry means raising `DSK_FAT_SECS` back to ≥ 23, which the
low-memory ladder no longer has room for.

**The boot-sector stub**: 62 bytes of BPB, then a fixed hand-assembled
stub (~40 bytes, a hex blob in os88disk.py) — print "Not a bootable disk.
Press any key." via int 10h AH=0Eh teletype, int 16h AH=00h wait, int 19h
reboot. Zero-padded to 510, then 55 AA. Byte-fixed ⇒ deterministic images
(§24).

**Why these choices**: canonical cluster/root/FAT counts — every era of
DOS recognizes the textbook 1.44M/360K layouts (pre-BPB DOS keys on the
media byte). NumFATs=2 — DOS and macOS expect it; single-FAT floppies
confuse CHKDSK. BS_BootSig 0x29 + serial + label — Windows/macOS mount
heuristics. OEM "MSDOS5.0" — legacy DOS/Windows mount heuristics key on
it; identity is carried by label+serial, and the kernel never reads OEM.
The volume-label root entry (attr 0x08) matches BS_VolLab so Windows shows
"OS8088APPS"; the kernel filters it out of the index space (below).

### FAT type detection (kernel, exactly per the Microsoft FAT spec)

Computed only from the cluster count — never from BS_FilSysType, the media
byte, or anything else:

```
RootDirSectors  = (RootEntCnt*32 + 511) / 512
FirstDataSec    = RsvdSecCnt + NumFATs*FATSz16 + RootDirSectors
DataSec         = TotSec16 - FirstDataSec
CountOfClusters = DataSec / SecPerClus
CountOfClusters <  4085  -> FAT12
CountOfClusters < 65525  -> FAT16
else                     -> reject (FAT32 out of scope)
```

Reachability: detection runs after §18.2 rule 13 has passed, so TotSec16 ≤
SecPerTrk×NumHeads×80 ≤ 36×2×80 = 5,760 and CountOfClusters ≤ 5,758 <
65,525 — the FAT32 branch is provably dead, kept only as two-instruction
insurance against future rule reordering. Genuine FAT16 is reachable only
on 36-spt (2.88M ED) geometries: clusters ∈ [4,085, 5,758]. All arithmetic
fits 16 bits: RootEntCnt ≤ 512 ⇒ RootDirSectors ≤ 32; FirstDataSec <
TotSec16 ≤ 65,535. There is **no separate FAT16 code path**: `dsk_fattype`
selects the entry decode inside `dsk_next_clus` (12-bit packed vs 16-bit
array); mount, enumeration, chain walk and loader are width-blind because
the walk is size-driven and range-checked against `dsk_maxclus` (§18).
FAT16 media with TotSec16 == 0 (≥32MB partitions) is rejected by rule 8 —
the documented 16-bit-LBA bound of `disk_read`.

### Directory enumeration — species rules (binding, in test order)

Raw entries are the standard 32-byte FAT directory entries, and the scan
runs over whichever directory is current (§19.2) — the root at mount, a
subdirectory after `dsk_chdir`. The scan
(§18.3 step 3) classifies each one **in this order** (each rule cites the
FAT-spec species it handles):

- name[0] == 0x00 → **end of directory: stop the whole scan** (spec: no
  allocated entries follow; entries a hostile writer hides behind a
  terminator do not exist).
- name[0] == 0xE5 → deleted; skip.
- (attr & 0x3F) == 0x0F → LFN entry (mask, then compare, per spec); skip
  — never counted, never displayed. Orphaned LFN runs handled for free.
- attr & 0x08 → volume label; skip.
- attr & 0x06 (HIDDEN or SYSTEM) → skip (DOS convention; protects the
  32-slot listing budget from foreign housekeeping files).
- name[0] == 0x20 → invalid per spec (a name may not start with a
  space); skip defensively.
- name[0] == '.' → a subdirectory's own `.` and `..` links; skip. The `..`
  row the user sees is **synthesized** (§19.5) and typed 3, so it can be
  navigated but never renamed or deleted; surfacing the raw pair would put
  two undeletable oddities at the top of every folder instead.
- name[0] == 0x05 → KANJI escape: treat the first byte as 0xE5 (spec)
  for display; falls through the sanitizer below.
- Otherwise **accept**, up to the cap of **32 accepted entries** —
  extras are invisible and the header count equals the listed count
  (documented cap, §22).

Note what is *not* here any more: `attr & 0x10` no longer skips. A
subdirectory is an accepted entry with staged type 2 (below) — that is what
made §19.2's navigation possible, and it is why the `.`/`..` rule had to be
added in the same change.

### The synthesized directory entry (the normative staged layout)

Each accepted entry is synthesized into the 32-byte staged shape that
`dsk_get_dir` serves — this layout, not any on-disk one, is the contract
every consumer (files.inc, loader.inc) reads:

| off | size | contents                                                    |
|-----|------|--------------------------------------------------------------|
| 0   | 16   | display name, NUL-padded: raw name[0..7] with trailing spaces trimmed, then '.', then ext[0..2] trimmed (dot omitted when the ext is blank); **every byte outside 0x21..0x7E replaced with '_'** (OEM-codepage bytes never reach the font renderer). Max 12 chars — fits every §22 truncation budget |
| 16  | 2    | type: 1 = loadable package, 2 = subdirectory, 3 = the parent link (§19.5), else 0 (rules below) |
| 18  | 2    | first cluster = raw FstClusLO (word @26), copied verbatim even when type=0 (harmless; the loader only reads it behind type==1, and `dsk_chdir` only behind type==2). FstClusHI (@20) is FAT32-only per spec — ignored |
| 20  | 4    | size in bytes = raw size dword @28, verbatim (lo word @20, hi @22 — drawn whole by `fm_ultoa`); forced to 0 for type 2 |
| 24  | 8    | zero                                                        |

**The type word** (binding — defense in depth with §21 step 1), tested in
this order:

- **type 2 (subdirectory)** iff `attr & 0x10` **and** raw FstClusLO ∈
  [2, `dsk_maxclus`]. A directory whose first cluster is outside that range
  is staged as type **0**: it is listed, it is inert, and it can never be
  entered — the same "listed but never acted on" outcome a garbage file
  gets. Tested **first**, ahead of the extension rule below, because a
  folder literally named `X.O88` must never type as a package and reach the
  loader.
- **type 1 (loadable package)** iff ALL of: raw ext bytes 8..10 == `"O88"`
  (uppercase exact — foreign OSes uppercase short names on write); size
  dword high word == 0; size low word ≥ 1; raw FstClusLO ∈
  [2, `dsk_maxclus`]. A garbage entry can never reach the loader as type 1.
  (Recorded tradeoff: a ≥64KB `*.O88` reads "Bad package" rather than "Too
  large" — it cannot be a package (cap `APP_MAX_SIZE`), so the message is
truthful.)
- else **type 0**.

**Type 3** is not derived from an on-disk entry at all: it is the
synthesized `..` row (§19.5), and it exists as its own type so that
"navigable, but not a thing on the disk" is a fact in the entry rather than
a string comparison. Consumers that navigate test `type >= 2`; consumers
that act on a file (Rename, Delete) refuse 3.

The type word is the *only* thing a consumer branches on: §22's open path
sends type 1 to the loader and types 2 and 3 to `dsk_chdir`, and type 0 to
the loader as well, where it is rejected as "Bad package" — which is the
truthful verdict for double-clicking a data file.

**Icons** have no on-disk table: they are **harvested at mount** (§18.3
step 4) from each type-1 file's first sector — a v2 `.o88` with the
embedded-icon flag (§20.2 bit 0) carries its 16×16 body at file offset
32..95, and that block is copied into `disk_icons` entry i. Everything
else — type-0 files, iconless packages, harvest read failures — gets the
all-zero slot, and viewers fall back to the built-in `ico_app16` (§25).
**Type 2 and type 3 are the one exception**: a folder has nothing on disk to
harvest an icon *from*, so `dsk_folder_ico` — a hand-authored 16×16 body in
`disk.inc`'s `.text`, the only icon in the kernel besides the menu-bar logo
that is drawn by hand — is copied into the slot instead. Doing it at
harvest time rather than at draw time means every viewer keeps the one rule
it already had: read `disk_icons` entry i, fall back to `ico_app16` if it
is all zero.

Display names are the 8.3 host filenames (e.g. `"MINES.O88"`), not the
16-byte header names — 8.3 cannot hold the 15-char header names; a running
instance still shows its header name (`inst_set_name` reads the loaded
region, §21). The listing is sorted by display name (§19.4), so nothing —
the Makefile's build order least of all — decides which entry an index
names.

### 19.2 Subdirectories — the current directory, one walker, and navigation

A FAT12/16 volume has **two** directory shapes, and this is the section
that keeps that fact from leaking into the rest of the kernel: the root is
a fixed run of `[dsk_rootsecs]` sectors at `[dsk_rootlba]`, and every
subdirectory is an ordinary cluster chain.

**State (two variables, both in `.text`, not `.bss`).**

| symbol | contract |
|--------|----------|
| `[dsk_cwd]` | word: the current directory's first cluster; **0 = the root**. The directory `disk_dir` lists (§18.3 step 3) and the one every `dskw_*` name resolves inside (§18.4). |
| `[dsk_keepcwd]` | byte: 1 while a `disk_mount` is being made *on behalf of* navigation or of `dskw_sync`, and only then. A mount with it clear resets `[dsk_cwd]` to 0. |

They live in `.text` with initialisers and **not** in `.bss` for a reason
worth restating: `-f bin` gives `.bss` no image bytes, so it boots as
whatever the RAM held, and the very first `disk_mount` reads
`[dsk_keepcwd]` to decide whether to reset `[dsk_cwd]`. Left in `.bss`, one
garbage byte at boot sends the mount walking a garbage cluster chain and
the volume lists two lines of noise. (This is the same trap `dsk_cwd`'s
comment in `disk.inc` records; it has cost a debug cycle once already.)

**One walker.** Every directory scan in the kernel — the mount listing
(§18.3 step 3), the write path's slot search and entry patch (§18.4),
`dsk_dotdot` below — goes through one iterator, so the two shapes are
spelled out once and cannot drift apart:

```
dsk_dirw_start  in:  AX = the directory's first cluster (0 = the root)
                out: nothing (all registers preserved)
dsk_dirw_next   out: CF=0 with AX = the next sector's LBA, or CF=1 = the
                     directory ends here
                clobbers: AX (the output), CF
```

End-of-chain needs no EOC constants, exactly as in `dsk_read_chain`: any
next-value outside [2, `dsk_maxclus`] ends the walk, which folds EOC marks,
free entries and bad marks into one case. A cross-linked FAT could cycle
forever, though — a directory walk has no file size to bound it the way
`dsk_read_chain` does — so **`DSK_DIRW_MAX` = 256 sectors** caps any one
walk. 256 sectors is 4,096 entries, past anything real and far past the
32-entry listing cap.

**Navigation is a remount.**

```
dsk_chdir    in:  AX = the directory's first cluster (0 = the root),
                  DL = drive (0 = A:, 1 = B:)
             out: CF=0 the volume is listed and [dsk_cwd] = AX; CF=1 the
                  mount failed — the volume is back at the root and
                  [dsk_mntok] is closed, exactly as after any failed mount
             clobbers: nothing else (flags)

dsk_dotdot   in:  nothing; [dsk_cwd] must be non-zero
             out: CF=0 with AX = the parent's first cluster (0 = the root);
                  CF=1 = the '..' link is missing or unreadable
             clobbers: AX (the output), CF
```

`dsk_chdir` stores `[dsk_cwd]`, raises `[dsk_keepcwd]`, calls `disk_mount`
and lowers it again. That single instruction's worth of difference is the
whole of navigation: rather than a second way to fill
`disk_dir`/`disk_icons`, entering a folder re-runs the one validated
pipeline — BPB re-checked, FAT re-snapshotted, listing and icon harvest
rebuilt. It costs a floppy's worth of sectors per navigation and buys the
property that `disk_dir` is **always** exactly a mount snapshot, with no
third staleness rule anywhere in the kernel.

`DL` is an input rather than a read of `[disk_drive]` so that a caller can
name the drive it means; today every caller passes `[disk_drive]`, but the
day a file-manager window carries its own drive it must not be able to
navigate the *other* drive's directory by accident.

`dsk_dotdot` reads the current directory's very first sector and decodes
entry 1, which is `..` by spec, taking its `FstClusLO` as the parent —
range-checked against `[dsk_maxclus]` first, because a corrupt `..` must
not become the cwd. The FAT convention that a parent *of* the root is
written as cluster 0 is exactly this kernel's own value for the root, so
going up needs no path stack and no memory of how the user got here: **the
disk itself records the parent**, and that is why there is no path string
anywhere in os8088.

**Failure is bounded, never fatal.** A `dsk_chdir` into a directory whose
chain is corrupt fails the mount, which resets `[dsk_cwd]` to 0 and closes
`[dsk_mntok]`: the volume lands back at the root with the write gate shut,
which is the same state a bad disk produces. A directory whose entries are
garbage simply lists nothing. Neither can crash, because no LBA in either
path is derived from an unvalidated field.

### 19.3 The system disk — a FAT12 volume with the kernel in its reserved area

The disk os8088 boots from is a **real FAT12 volume**: DOS, Linux and macOS
mount it, and so do os8088's own file manager and write path. That is not
cosmetic — it is what gives loadable drivers (§51) a place to live and
settings a place to be kept, without a second on-disk format and a second
set of readers.

The trick is one field. `BPB_RsvdSecCnt` covers the sectors between the boot
sector and FAT1 — the reserved area, which belongs to the boot loader by
definition and which no file system structure can reach. `tools/os88disk.py`
sets it to `1 + ceil(kernel/512)` and writes the kernel there, so
**boot/boot.asm's raw `read LBA 1..K` is unchanged and still correct** while
everything after it is an ordinary file system. Mount rule 5 (§18.2) has
always been `RsvdSecCnt >= 1`; nothing was relaxed to allow this.

Two consequences fall out, both wanted:

- **Drive A: mounts.** The Disk browser opens it, `SOUND.DRV` and
  `SYSTEM.CFG` are listed there, and the write gate (`[dsk_mntok]`, §18.4)
  opens for it — which is what lets the Control Panel keep a setting.
- **The boot sector carries a BPB.** Its first three bytes are
  `EB 3C 90` (a short jump past the BPB, and `short` because mount rule 2
  tests `BS_jmpBoot[0]`), then a 59-byte hole `os88disk.py` fills, then the
  loader at offset 62. `boot/boot.asm` reserves the hole and asserts
  nothing about its contents.

The volume is labelled `OS8088SYS`; the apps disk stays `OS8088APPS`.

### 19.4 The listing is sorted by name, in the mount

`disk_dir` comes out of `disk_mount` in **ascending display-name order**,
not the order the entries happen to sit in on the disk. `dsk_sortdir` is a
selection sort over the accepted entries, run at the end of the scan.

**It is done here and not in the file manager**, and that is the whole
point: the Disk window, the Standard File dialog (§38), every per-window
view cache (§22.1) and anything else that ever reads the snapshot get it
already sorted, and none of them carries a comparator. The alternative — a
display-order layer per consumer — is three copies of the same rule and
three chances to disagree with `fm_hit` about which entry a row is.

Four things about it:

- **It runs BEFORE the icon harvest**, which is what keeps it to one array.
  `disk_icons[i]` belongs to `disk_dir[i]`, so sorting afterwards would mean
  permuting a second 2KB array in lockstep — twice the movement, and a
  standing invitation for the two to drift apart. Harvesting afterwards
  fills slot *i* for whatever entry ended up at *i*.
- **Selection sort**, because *n* ≤ 32: at most 31 exchanges of 32 bytes for
  any input, against a bubble sort's 496. What it spends instead is
  comparisons — 496 of 16 bytes — and those are byte loads through `ES` with
  nothing copied in or out.
- **Case-insensitive.** A FAT short name is uppercase only *by convention*;
  `dsk_sanit` folds unprintables and nothing else, so a volume some other
  tool wrote with lowercase bytes would otherwise sort its whole lowercase
  half after `Z`. The compare folds `a`–`z` and nothing else.
- **The name field is NUL-padded** (`dsk_synth` zeroes the entry before
  filling it), so a shorter name sorts before a longer one that shares its
  prefix with no length test anywhere: `PAINT.O88` before `PAINT2.O88`,
  because 0 < `'2'`.

No `rep cmpsb` and no `rep movsb`: those want DS:SI, and `disk_dir` is in
`LOW_SEG`, which DS never points at (§2.1). Both operands are read and
written with `es:` overrides.

The parent link (§19.5) is **not in the sort** — it occupies slot 0 and
stays there — so `dsk_sortdir` takes the first entry's offset rather than
assuming `disk_dir`.

**What this replaced was a pinned build order.** The apps disk's listing was
directory order, so the order packages were named in the Makefile was the
order they appeared in, new packages had to be appended at the end of their
folder, and the scripted tests clicked rows by that index. None of it
survived contact with the disk being writable and mountable by a host OS —
a file the user copies on lands wherever the allocator puts it. The
Makefile's lists now say only *which* packages ship and *which folder* each
lands in.

### 19.5 `..` is an entry in the listing, not a menu command

In a subdirectory the listing **opens with a `..` row** — folder icon, no
size column, first in both the list and the icon grid, and above the sort
because it is placed before the scan runs (§19.4). Double-clicking it goes
up, exactly like double-clicking any folder.

**It is synthesized in the mount** (`dsk_synth_up`), for the same reason the
sort lives there: the Disk window, the Standard File dialog and every view
cache read one snapshot, so putting the row in that snapshot gives all of
them the same row from the same place. The dialog used to synthesize its own
— `fdlg_rows` returned `disk_nfiles + (cwd ≠ 0)` and every row ↔ index
conversion in the module carried the resulting offset — and that offset, and
the routine that existed to decide it, are gone: **a display row IS a
directory index** now. `fdlg_dive`'s `..` special case went with it, because
the entry carries the *parent's* first cluster and dives like any folder
instead of walking the directory off the disk a second time.

The entry is **type 3**, not type 2, and that distinction is the safety
property:

- Everything that **navigates** tests `type >= 2`, so `..` behaves exactly
  like a folder in `fm_open_sel`, `fdlg_act`, `fdlg_onclick`'s dive and the
  size-column suppression.
- Everything that **acts on a file** refuses 3. `fm_arm_sel` will not arm
  Rename or Delete on it — silently, like the no-selection case — and the
  context menu gets `fm_ctx_up`, which is the folder menu's first two items
  (Open, Open in New Window) and nothing else.
- A **name comparison against `..` would have been the wrong test.** The
  species filter (§19) drops the on-disk dot entries and never re-surfaces
  them, so a volume is free to hold something else that displays as `..`.
  The fact belongs in the entry.

Three consequences worth stating:

- **The header says "N files" without the link.** `fm_nfiles` asks the
  listing — is entry 0 type 3? — rather than deriving it from `FS_CWD`,
  because those two can disagree: a `..` whose on-disk link is unreadable
  gets **cluster 0, the root**, rather than no row at all, so the user is
  never stranded in a folder whose only way out is Root Folder.
- **The 32-entry cap costs one slot in a subdirectory**: 31 real entries
  plus the link.
- **The write path never sees it.** `dskw_find` scans the raw on-disk
  directory, not `disk_dir`, and `dskw_name83` rejects a leading dot, so
  `".."` can never become a raw 8.3 name to match against. Both gates
  predate this.

**Folder ▸ Up One Folder and Backspace stay.** They are the same
destination by a different route, and a one-button machine in a fullscreen
window needs the keyboard one.


## 20. Loadable programs — the .o88 package format

### 20.1 A package owns a segment, and its region is a heap claim

A package **owns a segment**. It is a flat 8086 binary assembled with
`org 0` and loaded on a paragraph boundary **claimed from the heap** (§2,
§21, §50.3); the loader hands it its own CS = DS = that paragraph, and
nothing in the image depends on where it landed.

**There is no package pool.** There was: `PKG_SEG`, 60KB of its own between
the kernel and the heap, with a first-fit allocator (`ld_alloc`) of its own
over the instance table. It was a fixed reservation — unavailable to anything
else whether or not a package was loaded — so deleting it returned 60KB to
every machine (510KB → 570KB of heap on a 640KB one) and is what makes a
**128KB** machine viable at all: the pool's own top used to sit above 128KB,
so such a machine had no heap and could load nothing.

Two consequences follow, and both are load-bearing:

- **A region is claimed from the TOP of the heap downward** (`mem_claim_hi`)
  while data claims grow up from the bottom, because a data claim can move
  within its lifetime by being freed and re-claimed and **a region can never
  move at all** — its base IS its CS, and relocating it would invalidate
  `W_SEG`, `I_SPTR`, every `MB_SEG` in the menu bar and every claim owner
  word. From one end they interleave and a long-lived data claim landing
  mid-heap permanently splits the space a package can load into; from
  opposite ends they meet only when the heap is genuinely full.
- **The region's owner word is the instance SLOT**, not the segment, while a
  package's own data claims carry the segment (§50.2). `mem_free_rec` already
  releases both, so teardown needs no new code, and the Task Manager's HEAP
  column — which sums a package's claims by segment — does not count the
  region twice against the SIZE column that already reports it. SS is still `LOW_SEG`,
shared with every task, so `[bp+disp]` still addresses SS and a BP-held
data pointer still needs a `ds:` override. §1's hard rules still apply
(cpu 8086, register discipline, no bare `sti` in handlers).

Budget: image + zeroed bss ≤ `APP_MAX_SIZE` (0xF000 = 60KB). The ceiling is
the **segment** — 16-bit offsets from `org 0` — not a pool; what a package can
actually get also depends on what the heap has contiguous, and a refusal
there is `LD_ENOMEM` like any other. Multiple package instances
can be resident at once, including two of the same package: each is its own
copy in its own segment with its own bss, so package state (equ offsets from
`os88_image_end`) is per-instance automatically. Closing an instance frees
its region (§29.2 rule 7) and every heap claim it held (§50.2).

**What owning a segment costs, and what it retires.** It retires the whole
v2 relocation machinery — the dual assembly, the diff scan, the byte-exact
reconstruction check, the author rule about whole-word package addresses,
and with them the class of bug where an address folded into a constant
assembled cleanly and relocated wrong. What it costs is that the kernel and
the package no longer share DS, so **three things cross the boundary and
each needed a mechanism**:

1. *The kernel calling into the package* — six near pointers in a window
   record that mean nothing without the segment they belong to. `W_SEG` +
   the `PKG_DISP` dispatcher solve it (§11, §20.2).
2. *The package calling into the kernel* — every API slot is a far call
   now, which is what §20.3's 8-byte cells are.
3. *Data passed either way* — a template, a string, a file name, a window
   record. The X and N stub families (§20.3) and the **ES = KERNEL_SEG on
   entry to every callback** rule (§11) cover every case in the tree.

### 20.2 Header — first 32 bytes of the file (and of each loaded region)

| off | size | contents                                                  |
|-----|------|------------------------------------------------------------|
| 0   | 2    | magic: bytes `'O','8'` (word 0x384F)                      |
| 2   | 1    | format version = 3 (segment-per-package; v1/v2 files are rejected) |
| 3   | 1    | flags: bit 0 = embedded icon follows the header; bits 1–7 zero |
| 4   | 2    | link base — must be **0**: a v3 package links at org 0     |
| 6   | 2    | entry offset (≥ 0x20; ≥ 0x60 with icon; < image size)      |
| 8   | 2    | image size = resident bytes: header + icon + code + data. Equals the file size exactly. |
| 10  | 2    | bss size — bytes the loader zeroes after the image        |
| 12  | 4    | **the dispatcher**: `FF D5` (`call bp`), `CB` (`retf`), `00` pad |
| 16  | 16   | program name, printable, NUL-padded (shown by tools)      |

**The dispatcher at +12 is the header's one piece of executable code**, and
it is what makes a package's callbacks ordinary near procs. Every
kernel-to-package call goes far to `<this package's segment>:12` with BP
holding the real target and DS already switched, so the three bytes are
`call bp` / `retf` (§11, `wm_pkgcall`). A package author never writes
`retf`, which means a missing one cannot exist; and because the pointer the
kernel calls is `{12, W_SEG}` read out of the window record, dispatch is
re-entrant across packages for free. `os88pkg.py` validates those four
bytes — an image without them would send the kernel into its data on the
first paint.

**There is no relocation table.** Total file bytes = image size, and the
tool checks that they are equal.

**Embedded icon** (flags bit 0): file offset 32..95 holds the program's
16×16 icon — 16 mask words then 16 data words (same body layout as §19's
harvested icon cache and §25's built-in library). With the flag set, image
size must be ≥ 96 and the entry offset ≥ 0x60. `disk_mount`'s icon harvest
(§18.3/§19) copies the block from the file's first sector into `disk_icons`;
the loader copies it again into `inst_icons` (§29) so the dock tile survives
in the kernel's own segment. A package with no icon gets the generic
sentinel, which `apps/hello` ships deliberately to keep that path exercised.

**Entry contract**: far-called by the loader at `packageseg:entry` with
**DS = CS = the package's segment**, ES = KERNEL_SEG, IF = 1, gfx lock NOT
held. The program creates its window(s) via the API table (wm_create is
lock-free) and **returns** BX = window ptr with CF clear; the loader
registers the instance (§29) and wm_shows it. CF set = abort (loader reports
"load failed"); on abort the loader runs `wm_destroy_seg` over the region's
segment before unreserving it, destroying every window the entry created by
its `W_SEG` creator stamp — BX is not trusted or even consulted, a package
that just declared itself broken gets nothing believed (§42.1).

The entry must not call wm_show/wm_hide/wm_front, spawn tasks, or draw. The
spawn prohibition is mechanical, not stylistic: `OSAPI_TASK_SPAWN` (§20.6)
would refuse anyway, because the loader publishes `I_STATE = 1` and binds
`wm_owner` only *after* the entry returns (§21 step 9), so `inst_of_win`
finds nothing and answers CF=1. The first legal spawn site is a window
callback. It MAY claim heap memory (§50.3): `mem_claim` identifies its
caller by the segment it runs in, not by a window, precisely so an app can
size itself before it has one.

After entry returns, the program is event-driven code: its W_PAINT /
W_ONKEY / W_ONCLICK / W_ONSIZE / `AM_ONCMD` procs run under the gfx lock per
§11, reached through the dispatcher. From any of them it may claim **one**
worker task (§20.6), which then runs pre-emptively alongside them under the
§7 rules.

### 20.3 The os8088 API jump table (kernel.asm, fixed offsets)

At KERNEL_SEG:0x0010, **8-byte cells**, one per slot, offset `0x0010 + 8n`.
A package owns a segment (§20.1), so every one of these is a **far call**
made with the package's own DS — which is why a cell is eight bytes and not
four:

```nasm
%macro OSAPI_SLOT 1                 ; exactly 8 bytes
    push ds
    push cs
    pop ds                          ; DS = KERNEL_SEG for the callee
    call %1
    pop ds                          ; ...and back to the caller's
    retf
%endmacro
```

The package writes `call OSAPI_GFX_FILL` and the SDK's
`%define OSAPI_GFX_FILL KERNEL_SEG:0x0038` makes it a far call, so **no
package call site changed** when packages moved into their own segments —
only the header did. Register contracts are the target routines' own (§5,
§6, §8, §11); every slot preserves everything but its documented outputs,
and restores DS and (where a stub borrowed it) ES.

Three cells in ten are too small to hold what they need, and those use
`OSAPI_JSLOT` — `jmp near <stub>` padded to eight — to reach a longer stub
below the table. There are two families:

- **X stubs** put the CALLER's DS in ES before calling, so a kernel routine
  can reach package data through an `es:` override: `font_str_x`,
  `font_width_x`, `wm_create` (the template), `inst_pkg_spawn` (the
  ownership fence), `osapi_mem_claim` and `osapi_mem_free` (the owner
  identity). This is why "the string/template you pass lives in your own
  segment" needs no thought from the package author.
- **N stubs** stage a NAME out of the package's segment into the kernel's
  `api_name` buffer first, because the file API and the file dialog take
  `SI` = a NUL 8.3 string and pass it on to routines that read it through
  DS many calls deep: `dskw_write`, `dskw_read`, `dskw_delete`,
  `fdlg_open`, plus a hand-written `api_file_rename` that stages both names.

The table's start (0x0010) and its span are proved by two build-time
assertions in kernel.asm; the span is **73 × 8** today. `apps/os88api.inc`
mirrors every offset as an `OSAPI_*` `%define` (§20.5).

```
0x0010 gfx_lock        0x0090 wm_front          0x0120 dskw_write     (N)
0x0018 gfx_unlock      0x0098 wm_content        0x0128 dskw_read      (N)
0x0020 gfx_pixel       0x00A0 wm_obscured       0x0130 dskw_delete    (N)
0x0028 gfx_hline       0x00A8 task_yield        0x0138 dskw_rename    (N)
0x0030 gfx_vline       0x00B0 task_sleep        0x0140 dskw_dfree
0x0038 gfx_fill        0x00B8 osapi_get_ticks   0x0148 menu_win_set
0x0040 gfx_frame       0x00C0 osapi_set_color   0x0150 fdlg_open      (N)
0x0048 gfx_fill_gray   0x00C8 osapi_mouse       0x0158 osapi_video
0x0050 gfx_xor_rect    0x00D0 osapi_srand       0x0160 inst_pkg_spawn (X)
0x0058 gfx_xor_fill    0x00D8 osapi_rand        0x0168 inst_pkg_alive
0x0060 font_char       0x00E0 osapi_snd_caps    0x0170 wm_clip_set
0x0068 font_str    (X) 0x00E8 osapi_snd_tone    0x0178 wm_clip_clear
0x0070 font_width  (X) 0x00F0 osapi_snd_play    0x0180 wm_clip_test
0x0078 wm_create   (X) 0x00F8 osapi_snd_fm  (X) 0x0188 cpu_info
0x0080 wm_show         0x0100 osapi_snd_stream  0x0190 xm_caps
0x0088 wm_hide         0x0108 wm_sizable        0x0198 xm_alloc
                       0x0110 wm_fullscreen     0x01A0 xm_free
                       0x0118 wm_grow_paint     0x01A8 xm_copy

0x01B0 wm_geom         0x01D8 gfx_blit4         0x0200 mem_claim      (X)
0x01B8 cm_alloc    (X) 0x01E0 wm_about_set      0x0208 mem_free       (X)
0x01C0 cm_free     (X) 0x01E8 (retired)         0x0210 mem_avail
0x01C8 cm_caps         0x01F0 osapi_gfx_dbuf    0x0218 osapi_font_glyphs
0x01D0 wm_resize       0x01F8 gfx_scroll        0x0220 wm_onsize
                                                0x0228 osapi_file_here
                                                0x0230 osapi_file_goto
                                                0x0238 mem_regrow     (X)
                                                0x0240 wm_title_set
                                                0x0248 osapi_drv_task (X)
                                                0x0250 mem_claim_dma  (X)
                                                0x0258 font_run       (X)
                                                0x0260 wm_top
```

**Every published slot keeps its number and contract**, on the rule that **a
slot number must never mean two contracts**: a shipped number keeps its
meaning, and the answer to "we no longer implement this" is a wrapper or a
refusing stub, never a reuse. Reusing 0x01C8 for a KB-counting `mem_avail`
where it once held a paragraph-counting one would fail silently and by a
factor of 64, which is the whole class of bug the rule exists to prevent. So
the three v3 arena slots at 0x01B8..0x01C8 stay live as `osapi_cm_*`
(kernel/memory.inc) — paragraph-counting wrappers over the claim heap that
keep their original register contracts exactly — and `OSAPI_SND_FM` /
`OSAPI_SND_STREAM` at 0x00F8/0x0100, once refusing stubs, carry the loadable
sound driver's (§51.4) real contracts. New code should prefer the KB slots
(§50.3) over the arena wrappers: they are the native shape, they work from
the entry proc, and only they carry the DMA-page and regrow contracts.

**Offsets are not stable across kernel versions, and never were pretended
to be.** Two things have moved them wholesale: the removal of the sound
cards (§34) took two slots out of the middle, and the move to 8-byte cells
doubled every stride. Both were deliberate, and both are affordable for the
same reason: every `.o88` in the tree is rebuilt from source by the same
`make` that builds the kernel, so the blast radius of an ABI change is a
rebuild. A package built against a different kernel's table will jump into
the wrong routine — `os88pkg.py` cannot detect that, and nothing at load
time can either.

Slot-specific contracts that are not simply their target routine's:

```
0x00E0 osapi_snd_caps    out AX = caps word (§34.2), BL = tone route
                         (always 0 = speaker), DX = 1 (the speaker is
                         always present).
0x00E8 osapi_snd_tone    in AX = freq Hz (0 = off), CX = duration ticks
                         (0 = until off), DL = priority (§34.3); out CF=1
                         refused (higher-priority owner), else CF=0 and
                         AL = owner generation.
0x00F0 osapi_snd_play    PCM clip (§34.4): ES:SI = 8-bit unsigned mono,
                         CX = count, DX = rate Hz (N = 1193182/rate must
                         land in 74..255); BLOCKS for the clip; a mouse
                         click aborts it. out AX = 0 ok / 1 busy / 2 rate
                         / 3 disabled-by-user / 4 no sink / 5 aborted.
                         ES restored per §1.
0x0108 wm_sizable        in BX = win ptr, AL = 0 clear / non-zero set
                         WF_SIZABLE (§11.1). UI-task context only.
0x0110 wm_fullscreen     in AL = 1 enter (BX = win) / 0 exit; caller holds
                         the gfx lock; out CF=1 = enter refused, screen
                         already owned (§11.2).
0x0118 wm_grow_paint     in BX = win ptr; lock held. The grow-box restore
                         of §11: a resizable package's self-initiated
                         content repaint must end with this call. A no-op
                         unless BX is the frontmost visible WF_SIZABLE
                         window, so it is always safe to call.
0x0120 dskw_write        in SI = NUL 8.3 name, ES:BX = bytes, DX:CX = the
                         count (0 = empty file). Creates or replaces. out
                         CF=0 AX=0, else CF=1 AX = FERR_*. DX preserved.
0x0128 dskw_read         in SI = name, ES:BX = buffer, DX:CX = its capacity;
                         out CF=0 and DX:AX = bytes read, else CF=1 AX =
                         FERR_* (FERR_BIG leaves the buffer untouched). DX
                         is an OUTPUT and does not survive the call.
                         Neither slot has a 64KB ceiling: the buffer is
                         normalised to an offset under 16 and the transfer
                         walks the segment (§18.4.1). The pair is the whole
                         read/write surface — 0x01E8 was the big-file
                         sibling and is retired, below.
0x0130 dskw_delete       in SI = name; out CF=0 AX=0, else CF=1 AX=FERR_*.
0x0138 dskw_rename       in SI = old name, DI = new name; out as delete.
0x0140 dskw_dfree        out CF=0, DX:AX = free bytes, BX = sectors per
                         cluster; CF=1 AX = FERR_*. No disk I/O — the
                         resident FAT snapshot answers it.
0x01E8 (retired)         was dskw_readbig; dskw_read absorbed it (§18.4.1).
                         The cell answers CF=1 with AX = FERR_NAME and the
                         SDK publishes no name for it, so nothing above it
                         renumbered and nothing can call it by accident.
0x0148 menu_win_set      in BX = win ptr, SI = app menu set (0 = none).
                         Stores [BX+W_MENUS] and relayouts the bar when BX
                         is active. Draws nothing, takes no lock, and
                         preserves every register AND the flags — so it can
                         sit between wm_create and the entry proc's `ret`
                         without eating the CF that `ret` owes the loader.
0x0160 inst_pkg_spawn    in AX = near entry inside the package image, BX =
                         the package's own window ptr; lock HELD. out CF=1
                         refused, else CF=0 and AL = the task slot (§20.6).
0x0168 inst_pkg_alive    in BX = the package's own window ptr; lock NOT
                         held. Returns with everything preserved while the
                         instance lives; otherwise NEVER RETURNS — it tears
                         the instance down through inst_task_die (§29.4).
0x0170 wm_clip_set       in BX = the package's own window ptr; lock HELD.
                         Arms the window's visible region for every
                         gfx_*/font_* call until the next gfx_unlock. out
                         CF=1 nothing is visible — draw nothing this frame.
0x0178 wm_clip_clear     disarm early, inside the same lock hold.
0x0180 wm_clip_test      in AX/BX/CX/DX = x1,y1,x2,y2 inclusive; out CF=0
                         the whole rect is drawable (also when nothing is
                         armed). Ask this BEFORE erasing a rect you are
                         about to draw text into — §11.3's granularity
                         rule is what happens if you don't.
0x0200 mem_claim         in AX = KB wanted; out CF=0 and DX = the base
                         segment, CF=1 refused (§50.3). The caller is
                         identified by the segment it runs in, so there is
                         nothing to pass and nothing to forge, and it works
                         from the entry proc where there is no window yet.
0x0208 mem_free          in DX = a segment this caller was given; out CF=0
                         released, CF=1 not yours.
0x0210 mem_avail         out AX = largest free run in KB, BX = total free
                         KB. The only honest number to size against — int
                         12h does not know what the kernel and the other
                         packages already hold.
0x01D8 gfx_blit4         in ES:SI = packed 4bpp source, BP = source stride
                         in bytes, AX/BX = dest x/y, CX/DX = width/height
                         in pixels (§5.4). ES is the caller's own here.
0x01D0 wm_resize         in BX = win, CX = new outer width, DX = new outer
                         height; lock held. Clamps, re-fits the origin and
                         repaints (§11.1). Never from inside a W_PAINT.
0x0218 osapi_font_glyphs out SI = the glyph table's offset in KERNEL_SEG,
                         AL = 32, AH = 126, CX = 8 (§6). Read through ES.
0x0220 wm_onsize         in BX = win, AX = a near proc in the window's own
                         segment (0 clears). The resize negotiator (§11.1).
0x0228 osapi_file_here   out DX = the current directory's first cluster
                         (0 = the root), BL = the drive. No disk I/O.
0x0230 osapi_file_goto   in DX = a cluster from `osapi_file_here`, BL = its
                         drive; out CF=1 it could not be listed and the
                         volume is back at the root with the write gate
                         shut. A REMOUNT (§19.2) - real floppy I/O, and
                         UI-task context like every other file slot.
```

**Why the last pair exists.** Every file name resolves in the volume's
CURRENT directory (§19.2), and that is one global word shared by every Disk
window and by the file dialog. Immediately after the dialog closes it still
names the folder the user picked, so an app's Save As lands there — and its
next *Save* does not, because anything that navigated in between has moved
it. An app whose Save means "the same place my Save As chose" has to say so,
and this is the pair it says it with. `apps/notepad` stores them beside the
document name and puts the volume back before every read and write, skipping
the remount when the volume is already there.

**The file slots are UI-task/window-callback context only (binding).**
They take `[sch_lock]` around int 13h and share `dsk_secbuf` and the FAT
snapshot with the mount path. **A package's worker task (§20.6) must NEVER
call them.** Spawning no longer keeps the caller set honest by construction,
so this is an author rule with no enforcement behind it, and violating it
corrupts the mounted volume — the write path is serialized by nothing but
the single-task rule. The legal sites are the entry proc, W_PAINT /
W_ONKEY / W_ONCLICK, an `AM_ONCMD` handler and an `fdlg` completion proc.
A caller inside a window callback holds the gfx lock and stalls painters for
the write's duration (§18.4) — a save is not a free operation and must never
sit in a paint path.

The file buffer is **ES:BX** (not DS:BX), like `osapi_snd_play`, so a caller
can write out of its own image without a copy; a package that keeps data in
its own bss just sets ES = DS. ES is restored per §1.

### 20.4 osapi helpers (kernel.asm)

| symbol           | contract                                            |
|------------------|------------------------------------------------------|
| `osapi_get_ticks` | out AX = [ticks]                                     |
| `osapi_set_color` | in AL → [gfx_color]                                  |
| `osapi_mouse`     | out CX=[mouse_x], DX=[mouse_y], AL=[mouse_btn]       |
| `osapi_srand`     | in AX → [osapi_seed]                                  |
| `osapi_rand`      | seed = seed×25173 + 13849; out AX = seed             |

### 20.5 apps/os88api.inc — the program-side SDK

NASM include used by packages (not by the kernel). Provides: `OSAPI_*` equs
for every table offset (§20.3), the `FERR_*` file error codes (§18.4), the
window-record W_* / template offsets
(§11), color constants, and a `OS88_HEADER 'NAME', entry_label` macro that
emits the §20.2 header (image size via a forward-referenced
`equ` to an end label the program declares with `OS88_BSS n` /
end-of-file macro — exact macro design is the implementer's, but a package
source must be able to consist of just `%include "os88api.inc"`, the header
macro, code/data, and an end macro). `OS88_HEADER` opens with `org 0`,
emits **version 3**, a zero link base, the entry offset, and the four
**dispatcher bytes at +12** (§20.2) — which is the one part a package author
must not hand-roll and, because the macro emits it, cannot get wrong.
Every `OSAPI_*` is a `%define` of `KERNEL_SEG:offset`, so `call OSAPI_X` is
a far call and **no package call site changed** when packages moved into
their own segments.

Icon support: `OS88_HEADER 'NAME', entry, 1` sets flags bit 0; the author
then writes `OS88_ICON16` (asserts, via `%if`-on-equ, that it starts at
offset 32), 32 hand-authored `dw` rows (16 mask, 16 data), and
`OS88_ICON16_END` (asserts offset 96). The third OS88_HEADER parameter is
optional and defaults to 0.

**Menu support (§12.2).** The SDK mirrors the `AM_*`/`AMENU_*` offsets and
`MENU_APPMAX`, and wraps the structure in three macros so a package never
hand-counts anything:

```nasm
OS88_MENUSET my_menus, my_name, my_oncmd
    OS88_MENU s_file, items_file, 3
    OS88_MENU s_edit, items_edit, 2
OS88_MENUSET_END my_menus
```

`OS88_MENUSET label, namestr, handler` emits `AM_NAME`, `AM_ONCMD` and an
`AM_COUNT` **computed from the label pair** — `(label_end − label_list) /
AMENU_ENTSZ` — and `OS88_MENUSET_END label` plants `label_end`. The handler
follows §12.2's contract exactly: called through the dispatcher under the
gfx lock with AL = item, AH = menu, SI = window, BX = the set — and every
string it points at is read by the kernel through the **menu's own segment**
(§12.2), which is what `MB_SEG` in the bar's runtime table carries.

**Worker support (§20.6).** The SDK mirrors `OSAPI_TASK_SPAWN` (0x0150)
and `OSAPI_TASK_ALIVE` (0x0158) and carries the seven author rules as a
comment block beside them — rule 2 (never return, never self-exit) and rule
4 (ALIVE under the lock deadlocks) especially, because neither is
detectable from the kernel and both are unrecoverable.

### 20.6 Worker tasks — one background task per package instance

A package may claim **exactly one** worker task, so that the work it does
between clicks is pre-empted rather than crammed into a callback. It is the
first time package code runs anywhere but the UI task, and therefore the
first time two packages can be pre-empted against each other. Two API slots
are the whole interface; both target `kernel/instance.inc` (§29.4), because
both are instance-lifecycle verbs.

| slot | routine | contract |
|------|---------|----------|
| 0x0150 | `inst_pkg_spawn` | in AX = near entry point inside the package image, BX = the package's own window ptr (what `wm_create` returned). **Caller holds the gfx lock.** out CF=1 refused and nothing created — BX names no live instance; that instance is not a package (`I_KIND` bit 7 clear); AX is not inside that record's own image (the fence is ES — the caller's
own segment, put there by the X stub of §20.3 — matching `[I_SPTR]`, plus
AX < I_SIZE); the instance already owns a task (`I_TASK != 0xFF`); or the task table is full. Else CF=0 and AL = the task slot. Preserves every register except AL and the flags. |
| 0x0158 | `inst_pkg_alive` | in BX = the package's own window ptr; **called from the worker**, with the gfx lock **NOT** held. Returns with every register **and the flags** preserved while the owning instance is live — and also, unconditionally, when the caller is the UI task (§8: task 0 never exits, so a wrong-context call is refused rather than obeyed). Otherwise **never returns**: it tears the instance down via `inst_task_die` (gfx_lock, `wm_destroy`, gfx_unlock, `task_exit` with BX = the record, which frees the record and with it the package region inside one IF=0 window). |

`inst_wchk` (module-internal, in BX; out CF=1 if BX is not a
record-aligned pointer inside `wm_wins`) fences both. These are the first
slots that hand a *package-supplied* pointer to `inst_of_win`, and
`inst_of_win` is not defensive: `wm_ptr2idx`'s `div cl` faults with no int 0
handler once `BX − wm_wins` exceeds 255 × `WIN_SIZE`, and the `wm_owner`
load after it is unbounded. The loader already refuses to `wm_destroy` an
aborting entry's BX without exactly this test (§21 step 9); `inst_wchk` is
that test factored out.

**Why the spawn must hold the lock.** "UI task only" would not be enough:
`W_PAINT` is dispatched by whichever task drives the repaint, so a
package's paint proc is not guaranteed to be on the UI task. It *is*
guaranteed to hold the gfx lock, and so are `W_ONKEY`, `W_ONCLICK`, an
`AM_ONCMD` handler (§12.2) and an `fdlg` completion proc (§38.6). The lock
is what excludes `app_close_win`, which also runs under it and which would
otherwise be free to read `I_TASK = 0xFF`, take its task-less path and free
the record while the brand-new worker is starting up inside the region.
Holding it is safe: nothing on `inst_pkg_spawn`'s path takes a lock, yields,
draws or calls BIOS — it is `inst_wchk`, `inst_of_win` and `task_spawn`,
all arithmetic and one 24-byte frame built in `LOW_SEG`.

The lock excludes `app_close_win`; it does **not** exclude another
*spawner*, because `app_launch` calls `task_spawn` lock-free on the UI task
(§29.4) and a paint-driven `inst_pkg_spawn` may be on any task. That
exclusion is `task_spawn`'s own IF=0 window (§8), which is what makes the
slot scan and the publish one step. Before §20.6 every caller was UI-task
only and single-threading did the job; it no longer does.

**The ownership fence.** BX is package input, so "it names *a* live
instance" is not enough. The record `inst_of_win` returns must additionally
be a **package** instance (`I_KIND` bit 7) whose own region contains the
entry point: `I_SPTR ≤ AX < I_SPTR + I_SIZE`. That is the cheap statement of
"this entry belongs to this record", and it is what ties the spawn to the
*calling* instance — the kernel has no other handle on who is calling.
Without it, a package handing over a stranger's window (a stale pointer, or
one harvested from `wm_wins`) would write the **stranger's** `I_TASK`, which
flips *both* instances onto the wrong teardown path: the stranger's close
box would take the die-flag branch and hide it forever, waiting on a worker
it does not own, while the caller's own record stayed task-less and its
close took the synchronous path — freeing, per §29.2 rule 7, the region its
worker is still executing in, for the heap to hand to the next package.
A second instance of the same package is refused by the same test: it lives
at a different base, so the caller's relocated entry is not inside it.

**The invariant everything rests on.** Once `I_TASK != 0xFF`,
`app_close_win` can only take its task-owned path: `I_STATE ← 2` plus
`wm_hide`. It frees nothing — not the record, not the window slot, not the
region. The task-less path, the only other place the UI task frees a
record, is unreachable. **So while a worker exists, only the worker's own
`task_exit` frees its instance record**, and the worker only reaches
`task_exit` through `OSAPI_TASK_ALIVE`. Everything below is a corollary.

**The liveness question and the teardown identity are asked of different
things, deliberately.** ALIVE asks about BX, per the ABI, but takes the
record it tears down from the *running task's* `T_INST` (§8). BX may name
nothing at all — a destroyed window reads `wm_owner = 0xFF` — and answering
"no record" by simply exiting would leave `I_TASK` naming a freed slot with
the instance record and the region leaked for the session, which is the
exact failure this routine exists to prevent. `T_INST` is authoritative:
`task_spawn` stored it from `inst_pkg_spawn`'s own instance index and
nothing else ever writes it. The consequence is that a wild or stale BX
becomes a clean self-destruct rather than a crash or a leak; a BX naming
*another* live instance's window makes the worker's lifetime track that
stranger's window, which is a mistake the package can make but not a hazard
to the stranger — the spawn's ownership fence has already made "attach my
worker to someone else's record" impossible.

Two teardown corollaries, both about not trading a crash for a leak:

- **The record is released even when it has no window.** `I_WIN = 0` (a
  corrupt table; not otherwise reachable) means there is nothing to
  `wm_destroy` — `wm_destroy` with BX = 0 would zero the cold-entry `jmp` at
  `0800:0000` — but the record itself is real, so the exit still passes it
  to `task_exit` as the release byte. Only a `T_INST` outside the table
  exits with no release byte at all, the `sbl_refill_task` precedent
  (§34.5).
- **The UI task is never exited.** Task 0's `T_INST` is a hard 0xFF, so a
  package that calls ALIVE from a *window callback* with a stale BX would
  otherwise reach `task_exit` on slot 0 — and §8's `task_exit` rests on task
  0 never exiting (its "resume the outgoing task" fallback is safe only
  because of that), so the machine would lose the event ladder, menu
  tracking and every future `gfx_unlock`. ALIVE therefore checks `sch_cur`
  on its death path and simply **returns** when it is the UI task. Calling
  ALIVE off the worker is a contract violation; returning is its bounded
  answer.

**Author rules (binding).**

1. **At most one worker per instance.** `I_TASK` is a single byte, is never
   cleared while the record lives, and the `I_TASK != 0xFF` test precedes
   `task_spawn`, so a second spawn consumes no slot and creates nothing. A
   second *instance* of the same package is a different record and gets its
   own worker.
2. **The worker must never return and must never exit on its own.** It
   calls `OSAPI_TASK_ALIVE` at least once per outer-loop iteration and
   sleeps when idle. This is the sharp edge of the whole feature: if the
   worker exited by itself, `I_TASK` would still name a task slot that has
   been freed and can be handed to the next `task_spawn`; `app_close_win`
   would take its task-owned path and set a die flag nobody ever reads; the
   window would hide, the instance record would never free, and the package
   region would leak for the rest of the session, while the Task Manager
   kept a row for a dead instance and billed it the cycles of whatever task
   inherited the slot. A worker that instead falls off the end of its entry
   and executes `ret` pops a word of stale task stack and jumps to a
   near-random offset in `KERNEL_SEG` — a wild crash rather than a leak.
   Neither is cheaply detectable from the kernel.
3. **The worker must not hold the gfx lock across a long computation.**
   Compute lock-free, then take the lock for a short burst of drawing and
   release it. A worker that spins under the lock wedges every other task
   on `gfx_lock` in *both* scheduler modes — a livelock the watchdog cannot
   break (§8.2).
4. **`gfx_lock` is not reentrant.** Calling `OSAPI_TASK_ALIVE` while
   holding it deadlocks, because `inst_task_die` takes the lock: the
   machine keeps scheduling, but nothing ever draws again and the cursor
   never returns. `gfx_lock` is a byte with no owner field, so there is no
   kernel defence — the same reason a `W_PAINT` proc has always been
   forbidden to take it.
5. **Re-check visibility under the lock, then set a clip.** Before
   drawing, confirm the window is still visible (`test word [bx+W_FLAGS],
   2`) and call `OSAPI_WM_CLIP_SET` (§11.3). CF=1 means not one pixel of
   your content shows — draw nothing this frame. CF=0 means draw normally,
   and the kernel cuts every `gfx_*`/`font_*` call to the part of your
   content nothing is covering; the region dies at your next
   `OSAPI_GFX_UNLOCK`, so there is nothing to undo. `OSAPI_WM_OBSCURED` is
   still there and still correct, but it vetoes the whole frame for one
   covered pixel, which for a worker that spends minutes on a frame is the
   wrong trade. This is the §14 Bounce idiom, and `app_bounce_task` in
   `kernel/apps.inc` is the reference implementation for everything a
   worker does.
6. **The worker's stack is `SCH_STACK` (512) bytes in `LOW_SEG`, and
   SS ≠ DS** (§2.1).
   No deep recursion, no large stack buffers, and remember that
   `[bp+disp]` addresses SS — a kernel or package pointer held in BP needs
   an explicit `ds:` override.
7. **What a worker may call.** Only the background-task surface: `gfx_*`,
   `osapi_set_color`, `font_*`, `wm_content`, `wm_obscured`,
   `wm_clip_set`/`wm_clip_clear`, `osapi_video`,
   `osapi_get_ticks`, `osapi_mouse`, `osapi_srand`/`osapi_rand`,
   `task_sleep`, `task_yield` and `OSAPI_TASK_ALIVE`. `osapi_set_color`
   comes with a condition, and it is the same one that makes `gfx_*` safe:
   `[gfx_color]` is a *single global with no owner*, so a worker may set it
   only inside the same lock hold as the drawing it colours. Setting it
   lock-free repaints some other window's next fill in the wrong colour.
   Everything the SPEC
   marks *UI-task/window-callback context only* is forbidden to it: the
   file slots 0x0098..0x00A8 (§18.4 — shared `dsk_secbuf`, FAT snapshot and
   `sch_lock`), the file dialog 0x00B0 (§38.6), and every verb of
   `osapi_snd_play` blocks with `sch_lock` raised and is likewise out.
   None of this is enforced.

**Refusal is normal, not exceptional.** `MAX_TASKS` is 12 and the UI task,
up to ten Clock/Bounce instances, `tm_task` and a transient SB refill/drain
task (§34.5) all draw from the same eleven dynamic slots, so CF=1 is an
ordinary outcome. A package must degrade — stay a perfectly good task-less
package, window, callbacks, menus and close box all working — and **should
retry**, because the condition is transient: closing one Clock frees the
slot, and a package that latched its refusal is broken until it is
relaunched. On refusal nothing was created and `I_TASK` is untouched, so a
retry is free — ask again from every callback that already runs under the
lock. `apps/fractal` is the reference: it retries in `fr_kick` (via
`fr_hire`), so a paint, a click and every menu command ask again, and its
`fr_spawned` byte latches only on success. Until one is granted it says so
on its canvas rather than half-rendering: with no frame buffer, a fallback
that renders under the *caller's* lock either loses its band to the next
repaint or holds the lock for a whole frame, which rule 3 forbids.

**The entry point in AX is bounded, not verified.** The ownership fence
above requires AX to lie inside the calling record's own region, so
`task_spawn` cannot be made to `iret` into kernel code — but *which* byte of
the region it lands on is the package's business, exactly like the
`W_PAINT`/`W_ONKEY`/`W_ONCLICK` near pointers a package plants in its own
window record and the kernel calls blind (§11). The entry is a plain near
label inside the package's relocated region, so it satisfies §33 rule 3 by
construction and needs no shim.

On entry the worker gets DX = its instance index, DS = ES = CS =
`KERNEL_SEG`, IF = 1, gfx lock free — the ordinary `task_spawn` frame (§8).

### 20.8 Forbidden (binding)

Six rules the package model rests on. Every one has been obeyed since it was
written down nowhere, which is the problem this section fixes: a rule that
lives only in the shape of the existing code is one an edit can violate
without anyone noticing it was a rule.

1. **No package code above 1MB, ever.** A package's region is a claim off the
   §50 heap — conventional memory — and nowhere else. Extended memory (§41) is
   a **data store**: no entry, no callback, no worker entry, no "just this one
   proc" in the HMA or above it. The highest byte any CS:IP can name is
   0x10FFEF and this design leans on that ceiling.
2. **The instance record does not grow.** `I_RECSZ` = 32 is load-bearing —
   index↔pointer is a shift (§29.1) — and the record is exactly full. Anything
   needing a new per-instance word finds a **side table** instead; `inst_icons`
   is the precedent and `wm_owner` is the other.
3. **No segment overrides in place of marshalling.** A caller's pointer
   crosses the boundary by being **copied** — at the API-slot layer's X and N
   stubs (§20.3), or at relayout (§12.2), the `dsk_get_dir` idiom (§2.1) —
   never by threading a "which segment" answer down through `font_str`,
   `menu_track`, `menu_drop` or the `dskw_*` name parsers. ES is the most
   contended register in this kernel and those bodies serve kernel-internal
   callers whose pointers are plain DS. The override version of this feature is
   where the bugs would have lived.
4. **A shipped slot keeps its contract.** The table is 8 bytes per cell from
   0x0010, and a number, once it means something, never means something else —
   retired functionality gets a **refusing stub**, not a reuse. What this rule
   is *not* is a promise that the numbers match another tree's: they did while
   two branches were live and stopped when they merged (§20.3), and closing
   that gap moved every cell above 0x01B0 down 88 bytes. Renumbering is
   therefore possible but expensive and deliberate: it invalidates every `.o88`
   at once, and it is only survivable because every package is in this tree and
   `make` rebuilds all of them. It has happened three times.

   **The unification of §18.4.1 is the one recorded exception, and it is an
   exception to the first sentence, not to the rest of the rule.** Slots
   0x0120 and 0x0128 kept their numbers and *changed* their register
   contracts: CX became DX:CX, and `dskw_read`'s answer became DX:AX. That is
   exactly what this rule forbids, and it was taken deliberately, before
   anything outside this tree can have been built against them — the whole
   point of doing it now rather than later. The retired third slot, 0x01E8,
   obeys the rule as written: a refusing stub, not a reuse. Nothing else may
   read this as licence; the next contract change is a new number.
5. **No `retf` from a package proc the kernel calls.** The
   kernel reaches a package through the three-byte `call bp` / `retf`
   dispatcher in its own header (§20.2), so entry, paint, onkey, onclick, menu
   handler and completion proc are all **near procs with a near `ret`**; the
   dispatcher owns the only `retf`. A stray `retf` returns into
   the loader's stack frame and hangs the machine at the first paint. The
   worker entry is the one proc with no return at all — it exits through
   `OSAPI_TASK_ALIVE` (§20.6 rule 2).
6. **No relocation, reintroduced under any name.** A change that seems to need
   a load-time fixup pass means the org-0, own-segment model is being violated
   somewhere else first. Fix that instead.

## 21. loader.inc

State (.bss, cleared by `loader_init`): `ld_pending` (word: 0 = none, else
directory index+1 — set by files.inc, consumed by ui.inc §13), `ld_pwin`
(word: the state block of the file-manager window that posted it, 0 =
none — §22.1; the index is into *that* window's listing, so `loader_run`
latches `ld_pwin` and calls `fmv_sync` on it **before** `ld_run_body`
resolves the index against the globals), `ld_status`
(byte: 0 ok, 1 = `LD_EDISK` disk error, 2 = `LD_EBAD` not a valid package,
3 too large, 4 entry aborted, 5 out of memory), plus loader_run scratch
words (registers run out on the 8086), including `ld_clus` (word, the
pending file's first cluster). `LD_DE_CLUS` equ 18 names the staged
entry's first-cluster word (§19; it was `LD_DE_LBA` — same offset, renamed
because the word is a cluster number now). `ld_appwin` is gone — the
instance table (§29) tracks residency.

**The region allocator is the claim heap.** A package's region is an
ordinary claim (§20.1/§50.3): `mem_claim_hi` — first fit from the TOP of the
heap downward, away from the data claims growing up from the bottom — with
the instance SLOT as its owner word. **I_SPTR is a SEGMENT** (§20.1) and
I_SIZE the region's size in bytes, now a whole-KB multiple because that is
`mem_claim`'s granularity. Freeing is `mem_free_rec` at the two §29.4
teardown sites, which releases the slot-owned region and the segment-owned
data claims together. No compaction — regions never move once loaded, which
is exactly why they are allocated from the far end.

`ld_alloc`, the first-fit-over-`inst_tab` allocator this replaced, is gone;
its scan idiom survives inside `mem_claim`. **Every failure path after the
claim must give the region back** (`ld_unreserve`): a bad read, a header that
changed under a disk swap, or an entry proc that aborts. The instance record
is still unpublished at those points, so `mem_free_rec` cannot be used — it
reads an I_SPTR that step 9 has not written — and both owner words are known
locally instead.

`ld_check_hdr` (module-internal) — in: SI → 32 readable header bytes,
[ld_fsz] = file size; out: CF=0 + scratch (img/bss/entry) filled, or CF=1 +
AL = status. Checks: magic; **version = 3** (a v1 or v2 file → "Bad
package"); link base = 0; image ≥ 0x20; entry in [0x20, image) (the icon
rule is enforced by os88pkg, not re-checked); image+bss ≤ APP_MAX_SIZE
(else "Too large"); image = file size (guards truncated files and stale
directories — sound because FAT directory sizes are byte-exact and
os88disk.py never pads the size field, §24; cluster slack on disk is
invisible here). It does **not** validate the dispatcher bytes at +12:
os88pkg.py does, and a package that reached a disk without them is
indistinguishable from one whose code is corrupt in any other way.

`loader_run` — in AX = directory index. UI task only, gfx lock not held
on entry. Steps:
1. Validate the entry: index < [disk_nfiles], type = 1, size ≤
   APP_MAX_SIZE and non-zero, and the first-cluster word at
   [si+LD_DE_CLUS] ∈ [2, [dsk_maxclus]] (belt and braces under §19's
   type-word rule — this check stands alone if the mount code ever
   changes; store it in `ld_clus`) → else status 2, step 10. **No
   eviction exists** — a load never disturbs running instances; every
   failure path below frees whatever it reserved and leaves them
   untouched.
2. Peek the header: AX = [ld_clus] → `dsk_clus2lba` (CF → status 2), then
   `disk_read` 1 sector into `dsk_secbuf` (UI-task-only shared scratch) —
   the first sector of the first cluster IS the file's first 512 bytes,
   so peek semantics are exact. CF → status 1.
3. `ld_check_hdr` on the peek → status 2/3.
4. need = roundup512(max(image+bss, file size)); > APP_MAX_SIZE → status
   3. (Sector-granular allocation makes the whole-file read safe: it
   writes ceil(fsize/512)·512 ≤ need bytes, never a neighbour's region.)
5. `inst_alloc` (§29) → CF → status 5. `mem_claim_hi` need KB → CF →
   status 5 (the unpublished instance record stays free). Note the
   record is not yet published, so the region is reserved only by
   single-threadedness (rule §29.2.8).
6. `dsk_read_chain` the whole file (§18) — AX = [ld_clus], DX =
   ceil(fsize/512) sectors — to ES=KERNEL_SEG, BX=base. CF with AL=1
   (disk error) → status 1 (`LD_EDISK`); AL=2 (chain corrupt: loop,
   cross-link, premature EOC, bad-cluster mark, out-of-range link) →
   status 2 (`LD_EBAD` — a corrupt FAT on a disk that reads fine is bad
   data, not a disk error). The write bound holds even against a corrupt
   FAT: the walk is size-driven, reading exactly ceil(fsize/512)·512 ≤
   need bytes — never a neighbour's region. Re-run `ld_check_hdr`
   against the in-region header (the disk could have been swapped
   between the peek and the read — and a looped or cross-linked chain
   can produce plausible-length garbage) → status 2/3.
7. *(retired — v3 has no relocation, §20.2)*
8. Zero bss-size bytes at segment:image.
9. **Far**-call packageseg:entry (contract §20.2) with DS = CS = the
   package's segment, ES = KERNEL_SEG, lock free. CF → the abort path (BX
   sanity-checked exactly as before: inside wm_wins, record-aligned, then
   locked wm_destroy), status 4. Else: fill the reserved instance record —
   I_KIND = KIND_PKG, I_TASK = 0xFF (a package is *born* task-less; §20.6
   lets a later callback claim one worker, which is also why the entry proc
   itself cannot spawn: neither `I_STATE = 1` nor `wm_owner` is published
   yet), I_SPTR = the base **segment**, I_SIZE = need,
   `inst_set_name_x` from segment:16 (the name is in the package's segment,
   so the ES-reading twin), I_ICON = a pointer into `inst_icons` with the
   header's 64 icon bytes **copied there** when flags bit 0 is set, else 0
   — the dock draws icons from KERNEL_SEG and cannot follow a pointer into
   a package. Then `inst_bind_win` BX, publish I_STATE = 1, **`[ld_status]`
   = LD_OK and `[ld_painted]` = 1**, gfx_lock, `wm_show` BX, gfx_unlock,
   status 0. (`wm_create` failing inside the entry surfaces as CF =
   status 4.) The status is published **before** the repaint on purpose: a
   Disk window's status line draws `[ld_status]` (§22), so setting it in
   step 10 instead forced a second whole-screen pass on every successful
   launch, purely to correct one line of text.
10. Set `[ld_status]`. When `[ld_painted]` is 0 — the failure paths — call
    `files_refresh` (§22), the full pass, as before. When it is 1, the
    success path has already published the status and drawn the new window
    itself (§11.90), and all that is left is the poster's own status line,
    which still reads `'Loading...'`: call `files_poster` instead, which
    repaints that ONE LINE of that one window, clipped to what is still
    visible of it (`fm_status_only`, §22), and falls back to the window's
    whole content only when a clip edge crosses the line.

Closing a package instance follows whichever §29 path its I_TASK selects.
With no worker it is the task-less path: locked wm_destroy + I_STATE ← 0 —
that store frees the region. With a §20.6 worker it is the die-flag path,
and the worker's next `OSAPI_TASK_ALIVE` runs `inst_task_die`; then
`task_exit`'s release byte frees the record **and with it the region**,
inside one IF=0 window (§29.2 rules 2 and 7). Identical effect, different
task and different fence. Both paths also call `mem_free_rec` (§50.2), which
returns every heap claim the instance held — that is what makes
`OSAPI_MEM_CLAIM` safe to use without a teardown hook. The Task Manager's
RAM readout sums package records' I_SIZE under one cli (§28) and no longer
peeks at package headers; the old "`ld_appwin` zeroed before the region is
overwritten" invariant is retired.

## 22. files.inc — the Disk window (file manager)

Built-in app kind (KIND_FILES), **cap 4** — up to four windows, each on its
own drive and its own folder — 320×200 at (110,80), **resizable**
(KD_WFLAG = WF_SIZABLE, §11.1/§29.3 — the one built-in that is). No
background task, no boot-time window: `files_init` (from kmain) only
resets module state; a window is created on demand by `app_launch` (§29),
whose KD_INIT is `fm_kinit`.

**The binding rule of this whole section, one sentence: paints read the
window's cache; actions re-sync the global snapshot first.** `disk_dir`,
`disk_icons` and `disk_nfiles` (§18/§19) stay exactly one global mount
snapshot and remain the only thing `loader.inc` and `diskw.inc` ever
resolve a directory index against. Each window additionally owns a
byte-for-byte **copy** of that snapshot in its `VIEW_SEG` slot (§2.3/§22.1),
which is what `W_PAINT`, `fm_layout` and the hit-tester read — so a repaint,
a drag, a raise, a resize or a `wm_paint_all` never touches the floppy. Any
*action* that resolves against the globals (open a package, `dsk_chdir`,
`dskw_mkdir`, and in general every `dskw_*`) first calls `fmv_sync`, which
remounts **only** if the acting window's `(drive, cwd)` differs from
`([disk_drive], [dsk_cwd])` — in the common case, where you act in the
window you last navigated, a compare and a `ret`.

**Per-window state** is a 16-byte `KD_POOL` block (`fm_pool`, 4 × 16, §29.3),
allocated and cascaded by `app_launch` and handed to `fm_kinit` in DI:

| off | field | meaning |
|-----|-------|---------|
| 0 | `FS_SEL` w | selected **directory index** — not a row; 0xFFFF = none |
| 2 | `FS_SCRL` w | first visible row (entry row in list view, grid row in icon view) |
| 4 | `FS_CLKT` w | birth tick (§10) of this window's last entry click |
| 6 | `FS_N` w | entries in this window's cache, 0..32 |
| 8 | `FS_CWD` w | the folder it is showing: first cluster, 0 = root |
| 10 | `FS_DRV` b | the drive it is showing, 0 = A:, 1 = B: |
| 11 | `FS_MOK` b | 1 = that listing came from a fully successful mount |
| 12 | `FS_VIEW` b | 0 = list view, 1 = icon view |
| 13 | `FS_IDX` b | its `VIEW_SEG` slot, 0..3 — derived once by `fm_kinit` |
| 14 | `FS_EDIT` b | status-line editor: 0 = off, 1 = new folder, 2 = rename, 3 = delete confirm |
| 15 | `FS_FERR` b | `FERR_*` of the last file operation **in this window**, 0 = none; 255 = "show the free-space line" (below) |

`FS_CLKT` moves with `FS_SEL` or not at all: shared, a click on row 3 in
window A followed within 9 ticks by a click on row 3 in window B would
compose into a double-click and launch a package.

Module-global state that deliberately did **not** become per-window:
`fm_ebuf` (13B) and `fm_elen`, the name being typed — only the front window
receives `W_ONKEY` (§13), so exactly one editor can be live, and arming one
window's editor clears every other block's `FS_EDIT`; `fm_onam` (13B) and
`fm_odir` (byte), the name and folder-ness of the entry Rename/Delete was
armed **on**, captured at arm time for the same reason (one live editor) and
because `fm_name` is reused by every row the painter draws; and `fm_tdirty`
(word, the window whose caption has been rewritten and not yet drawn — see
"The deferred retitle" below). `fm_msgbuf` (16B) plus `fm_msgwin` (word, the
state block that asked) carry the free-space line, which is why `FS_FERR` =
255 alone does not draw it: the buffer holds one figure and it belongs to one
window.

**The window's title is the folder it is showing.** `fm_kinit` points
`[bx+W_TITLE]` at the instance record's 16-byte `I_NAME` (§29.1) rather
than at a literal, so one write retitles the window, its dock tile (§30)
and its Task Manager row (§28) together. Navigation rewrites it: entering a
folder by name writes that name, and anything that lands at the root writes
`"Disk"`. Going **up** into a directory that is not the root writes
`"Folder"` — the honest answer, because naming it would need the
grandparent's listing, which is a second mount to display a string. Names
are ≤ 12 chars (§19) and `I_NAME` is 16 with a permanent NUL at byte 15, so
no bound can be exceeded.

The rule has one non-navigation case, and it is the one that used to break
it. `fmv_sync` (§22.1) re-lists a window where it already is, so it
normally must NOT retitle — it has no name to give and would demote a good
`"TOOLS"` to `"Folder"`. But a sync can move the window after all: if the
folder was deleted underneath it, the mount falls back to the root (§19.2).
`fmv_load` records where it really landed, so **`fmv_sync` compares the
`FS_CWD` it asked for against the one it got, and retitles only on a
mismatch** — where the answer is always the root, the one caption that
names itself. Every other path that changes `FS_CWD` is navigation and
retitles with a real name.

**The deferred retitle (`[fm_tdirty]`).** `fm_repaint` repaints content
only. A window's CAPTION lives in the frame, which a content repaint never
touches — so navigation would leave the title bar naming the folder just
left. `fm_settitle` therefore banks its window in `[fm_tdirty]`, and
`fm_title_flush` spends it through `wm_title_set` (§11.92): 17 rows, under
the caller-held lock, with no cascade. Exactly two routines call the flush,
because exactly two repaint a Disk window under the lock — `fm_repaint` and
`files_poster` (§11.90) — and each clears the word *before* the call, so
there is no recursion.

It is **deferred**, and it is a **pointer rather than a flag**, because
`fm_settitle`'s callers disagree about the lock: `fm_go`, `fm_mount` and
`fm_view` hold it, `fm_kinit` runs before the window is ever shown (and
zeroes the word afterwards — `app_launch`'s `wm_show` is about to draw the
whole window anyway), and `fmv_sync`'s folder-vanished path reaches it from
`ld_run`, which holds no lock across a mount. A pointer cannot be spent on
whichever window happened to repaint in between. `files_init` zeroes it, so
it is not a `.bss` read-before-write (§2.1).

This replaced `[fm_full]`, a byte meaning "the next `fm_repaint` owes the
frame too", which escalated that call to the window's whole frame rect and,
before that, to `wm_paint_all`. The reasoning for the escalation was that it
is only ever raised on a path that has already paid for a disk mount — true,
and still not a reason to redraw a listing, a frame and every overlapping
window to correct one string.

**The status line alone (`fm_status_only`).** The loader round trip changes
exactly one line of text — `'Loading...'` on when the load is posted, off
when it finishes — and both ends used to cost a whole `fm_repaint`: a white
fill of the content plus every row, every icon, the header and the buttons,
to correct eight pixel rows. `fm_draw_status` is that line split out of
`fm_draw_core` (which still calls it, and still white-fills first);
`fm_status_only` is the standalone version, which erases the line's own
rect and redraws it. In SI = the window, out CF = 1 **refused**.

The refusal is the §11.3 granularity rule, not caution. The erase is a fill
(per-pixel clipping) and the text is glyphs (whole-cell clipping), so a clip
edge crossing that rect would erase rows the text could not be put back
into — the line would go **blank**, not stale. `wm_clip_test` on the whole
rect answers it in one call, the `fr_status` idiom of §40.1, and CF = 1
sends the caller back to `fm_repaint`, which is what it did before this
existed. The status line's rect is the full content width at
`fm_cy`+`fm_staty`, 8 rows: the scroll bar stops at `fm_listb`, two rows
above it, so nothing else lives there.

**Live layout (binding).** The window resizes, so nothing may bake in
320×200: one helper, `fm_layout` (in BX = window ptr), computes the
frame-derived values both the painter and the hit-tester use — computed in
one place precisely so they can never disagree:

```
cw       = W_W - 2                 ; content width
ch       = W_H - TITLE_H - 1      ; content height (rows 0..ch-1)
status_y = ch - 17                 ; status line text top
list_bot = status_y - 2            ; first row BELOW the row area
rows_fit = (list_bot - 22) / row_h ; row_h: 16 list, 40 icons; 0 if negative
cols     = 1 (list) | max(1, (cw-16)/78) (icons)
```

`fm_layout` also calls `fm_vp_set` and mirrors `FS_VIEW` / `FS_N` /
`FS_SCRL` into `fm_lview` / `fm_lnf` / `fm_lscr` (§22.1) — everything below
that says "the view", "the entry count" or "the scroll position" means
those mirrors, and `fm_clamp_scroll` is the one routine that writes the
scroll position back into the block.

All coordinates below are content-relative. Header line at (6,6):
`"Drive B:  N files"` (drive letter from this window's `FS_DRV`) or, when
its listing came from a failed mount (`FS_MOK` = 0),
`"No os8088 disk (B:)"` (19 chars — short enough to clear
the buttons at the default width; it was "No os8088 disk in drive B:"
until the view toggle claimed that room). The failure string is verbatim;
its **semantics** are "no readable data disk" — unreadable, unformatted,
or any §18.2 BPB rule failed. N is the accepted-entry count (≤ 32, §19's
cap — the header count always equals the listed count), read from this
window's `FS_N`, not from the global `[disk_nfiles]`. File names are
the synthesized 8.3 display names of §19 (e.g. `"MINES.O88"`, ≤ 12
chars); sizes are the §19 staged size dword, drawn in full (`fm_ultoa`). Folders count and list exactly like files — a type-2 entry (§19)
shows the built-in folder icon and a blank size column. Two buttons at the top right,
1px black frames, labels centered: **Refresh** from (cw−68, 2) to
(cw−6, 15) — remounts the current drive so a swapped disk shows its real
contents — and **the view toggle** from (cw−136, 2) to (cw−74, 15),
labelled with the view a click switches **to** ("Icons" in list view,
"List" in icon view). The primitives clip to the *screen*, not the
window, so a shrunken window must not let the strip bleed past its own
frame: each button is drawn — and its click rect tested — only when it
fits (**Refresh iff cw ≥ 76, the view toggle iff cw ≥ 142**; one
condition gates both, so nothing invisible is ever clickable), and the
header is truncated to end 8px short of the leftmost drawn button (or of
cw). The status line at (6, status_y) shows the **first** of these that
applies — the precedence is binding, because all five can be true at once:

1. `[ld_pending]` non-zero → `"Loading..."`.
2. `FS_EDIT` non-zero → the **edit line** (below).
3. `FS_FERR` = 255 **and** `[fm_msgwin]` = this window → `fm_msgbuf`, the
   free-space line (`"1423 KB free"`).
4. `FS_FERR` non-zero → the `FERR_*` message, from an 11-entry table
   indexed by the code: (0 unused) `"No disk"`, `"Disk error"`,
   `"Bad name"`, `"No such file"`, `"Name exists"`, `"Disk full"`,
   `"Folder full"`, `"Protected"`, `"Write protected"`, `"Too large"`.
   Without this table every `dskw_*` failure is silent, which for an
   irreversible operation is the worst possible outcome.
5. else `[ld_status]`: "", "Disk error", "Bad package", "Too large",
   "Load failed", "Out of memory" (0..5).

Whichever wins is truncated to (cw−12)/8 chars
through the same scratch-buffer idiom as the header ("Out of memory" is
104px and a legal resize can leave cw = 94). In list view the name is
truncated to the room left of the size column ((cw − 88)/8 chars); every
string the window draws is bounded by the live cw one way or another.

**The row area** spans x 0..cw−16, y 22..list_bot−1 (a 2px gutter before
the scroll bar); when `rows_fit` is 0 the row area and the scroll bar are
simply omitted (a legal degenerate window). Rows shown = min(total −
`FS_SCRL`, rows_fit), where total = **this window's `FS_N`** (read through
the `fm_lnf` mirror — never `[disk_nfiles]`, which belongs to whichever
window last acted and is exactly the bug §22.1 exists to prevent) rows in
list view, ceil(FS_N / cols) grid rows in icon view. The scroll position is
clamped to
0..max(0, total − rows_fit) at every use — paint clamps first, so a
shrink-resize self-heals.

- **List view** (FS_VIEW = 0): rows 16px tall, entry index = row +
  FS_SCRL. Per row: the file's 16×16 icon at x=4 (from **this window's
  cache** via `fmv_get_icon`, §22.1 — never `disk_icons`, which belongs to
  whichever window last acted; all-zero entry → built-in `ico_app16`, §25), name at x=24,
  size right-aligned ending at cw−22, text baselines at row top + 4.
- **Icon view** (FS_VIEW = 1): a grid of 78×40 cells, `cols` per grid
  row, cell (r, c) at (c·78, 22 + (r − FS_SCRL)·40), entry index =
  r·cols + c. Per cell: the 16×16 icon centered at cell +(31, 3), the
  name below it at cell y+23, truncated to **9 chars** and centered
  (x = cell + (78 − 8·len)/2). Truncation is display-only.

Selected entry: inverted (`gfx_xor_fill` over the row band x 0..cw−16 /
the full cell) — **only when its row/cell is inside the visible band**; a
selection scrolled out of view stays selected, just not drawn.

**The scroll bar**: x cw−14..cw−1, y 22..list_bot−1, drawn whenever the
row area is. 1px black frame; an 11-row **up-arrow cell** at the top and
**down-arrow cell** at the bottom (black triangle glyphs, separated from
the track by their own 1px rule); the track between them filled 50% gray.
When total > rows_fit a white, black-framed **thumb** rides the track:
height = max(8, rows_fit·track_h/total), top = track_y0 +
FS_SCRL·track_h/total, clamped to the track; otherwise the track is
bare and the arrows are inert. One degenerate exception: a track shorter
than the 8px minimum thumb (frame heights 83..89) stays bare and its
clicks do nothing — the arrows and keys still scroll. There is **no thumb drag** — W_ONCLICK is
a one-shot dispatch under the held lock, and a tracking loop belongs to
the UI task (§13), so the bar scrolls by clicks alone (a deliberate,
recorded scope cut): up/down arrow = one row; a track click above the
thumb's top = a page (rows_fit rows) up, anywhere else in the track = a
page down.

Behaviour:
- `files_open_drive` (public; in AL = drive 0/1, no lock held): the target
  is (AL, root) — a drive icon always means that drive's top level — and it
  goes through the choose-or-create rule of §22.1. Callers: CMD_FILES
  dispatch (via `files_open`) and desk_click (§26). It no longer mounts
  unconditionally: an existing window already showing that drive's root is
  fronted, which is the same "one window per place" rule the Finder has, and
  a Refresh is one click away if the disk was swapped.
- `files_open` (from CMD_FILES dispatch, no lock held): the target is
  `([disk_drive], [dsk_cwd])` — where the volume already is — then the same
  rule.
- `W_ONCLICK` (lock held; the button and scroll-bar paths below end in
  `fm_repaint`: a content repaint — white-fill own content from the **live**
  W_W/W_H + redraw + a closing `wm_grow_paint` (§11), because the white fill
  erases the grow box — preceded by `fm_title_flush` when navigation left a
  caption owing, see "The deferred retitle" below. **A click in the row area
  usually ends nowhere near it** — see §22.2):
  test order is buttons → scroll bar → rows.
  1. Refresh rect → `fmv_load` on **this window's** `FS_DRV`, root: a
     re-mount from scratch, so a swapped disk shows its real contents.
  2. View-toggle rect → flip `FS_VIEW`, reset `FS_SCRL` (selection kept).
  3. Scroll bar (only when the row area is drawn) → adjust the scroll
     position per the arrow/track rules above, clamped; nothing else
     changes.
  4. Row area → map to an entry index per the current view (icon view:
     reject x past cols·78); y < 22, past the shown rows, or index ≥
     `FS_N` → clear selection. Index == `FS_SEL` and
     [ui_click_t]−`FS_CLKT` < 9 (birth ticks, §10) → double-click:
     `fm_open_sel` (below). Else select it (`FS_SEL` =
     directory index), stamp `FS_CLKT`. **What that costs to draw is
     §22.2, and it is normally two inverted bands or nothing at all.**
     A double-click that posted a **load** (`[ld_pending]` non-zero on the
     way back) exits through `fm_status_only` instead: the only thing it
     changed on screen is `'Loading...'` in the status line. A double-click
     on a **folder** still takes the full `fm_repaint` — `fm_go` replaced
     the whole listing.

  A click also **cancels any edit mode** before anything else, on the same
  reasoning a Mac cancels an in-place rename when you click away.
- **`fm_open_sel`** (in AX = a directory index already range-checked; lock
  held) is the one open path, shared by the double-click, by Enter and by
  **File ▸ Open**. It stages the entry from **this window's cache**
  (`fmv_get_dir`) and branches on the §19 type word — reading the
  first-cluster word into a register *before* it acts, because a `dsk_chdir`
  remount rebuilds `dsk_ent` underneath it:
  - type 2 (a folder) → `fmv_load` on this window's `FS_DRV` and that
    cluster (§19.2): the listing, its cache, the selection, the scroll
    position and the window title all follow, and no other window moves.
  - anything else → `[ld_pending]` = index+1 **and `[ld_pwin]` = this
    window's state block**, and ui.inc runs the loader after the lock drops
    (the repaint shows "Loading..."). A type-0 entry still goes here and is
    rejected as "Bad package", which is the truthful verdict for
    double-clicking a data file.

  `[ld_pwin]` is not optional once there are several windows: the index is
  into the *poster's* listing, and `loader_run` must `fmv_sync` that window
  before `ld_run_body` resolves it against the globals. Without it a click
  in a background window loads a **different** file — one that passes every
  §21 validation, because the index is in range for whatever is mounted.
  `loader_run` latches `[ld_pwin]` before the sync, because `disk_mount`
  deliberately zeroes `[ld_pending]` (§18.3).

  Branching here is not cosmetic: before it existed, a folder double-click
  reached `ld_run_body`, which rejects `type != 1`, and the status line
  read "Bad package" for an operation that had nothing to do with packages.
- `W_ONKEY` (lock held). **While `FS_EDIT` is non-zero the handler
  swallows every key** and nothing below applies — binding, because the
  bare-letter shortcuts are live otherwise and typing a folder called
  `BAK` would switch to drive B: on its first character. See "Naming"
  below. Otherwise: 'a'/'A' → drive 0, 'b'/'B' → drive 1, 'r'/'R' →
  this window's own drive; all three: `fmv_load` at that drive's root,
  repaint content. 'v'/'V' → toggle the view like the
  button. 'n'/'N' → New Folder…. **Backspace (8) → Up One Folder**, the
  `FMC_UP` body verbatim. Scan codes: Up/Down (48h/50h) scroll one row, PgUp/PgDn
  (49h/51h) a page — the view, not the selection, clamped like the bar.
  Enter (13) with a valid selection → `fm_open_sel`. Rename and Delete have
  **no key** — see the policy under "Naming and confirming". (disk_mount
  under the gfx lock stalls painters: 16 sectors on the shipped disk —
  about a second on real hardware — with a hostile-media ceiling of 97
  one-sector reads, approaching ~20 s at one sector per revolution plus
  retries; bounded and accepted, §18.3.)
- **After the OS itself writes the disk** (§18.4): the write path remounts,
  so the globals are already current, and the acting command calls
  `fmv_bcast` (§22.1) to push that snapshot into every window on the same
  folder — including its own, whose cache is otherwise a listing without
  the file it just created. It still cannot repaint the others (the writing
  callback holds the gfx lock), so a sibling shows the new listing at its
  next repaint from any cause: a click in it, a window moving over it,
  Refresh. A window somewhere *else* is untouched and correct. `FS_SEL` is a
  directory index into a listing that may have changed under it, exactly as
  it may after a disk swap, and the loader's own validation (§21) plus
  `fmv_bcast`'s selection clear are what keep a stale index harmless.
- **A package written by another package** (`OSAPI_WRITE`, §20.3) does not
  go through `fmv_bcast` and no window learns of it. Refresh, as before.

### The menu bar — `fm_menus` and `fm_oncmd`

`fm_menus` is an ordinary §12.2 set with `AM_NAME` = `menu_loc_name`
(so the bar still reads **Locator**, §12.3) and `AM_ONCMD` = `fm_oncmd`.
`fm_kinit` stores it into `W_MENUS`, so the bar carries the file manager's
menus exactly while one of its windows is active and Locator's desktop
menus otherwise. Four menus, and the layout is pinned because
`menu_relayout` drops any cell that would reach `[vid_clk_hx]` (§39.2 — 434
on either 640-wide adapter and more on Hercules, so 434 is the binding case):
`'Locator'` is 56px so the first cell starts at 38+56+16 = **110**, and
cells are `font_width + MENU_TITLE_PAD`:

| menu | width | x range | items |
|------|-------|---------|-------|
| **File** | 44 | 110..153 | Open · New Folder… · Rename… · Delete · Close Window |
| **Folder** | 60 | 154..213 | New Window · Open in New Window · Refresh · Up One Folder · Root Folder · Drive A: · Drive B: · Free Space |
| **View** | 44 | 214..257 | as List · as Icons |
| **Special** | 68 | 258..325 | Clock · Bounce · Restart |

325 against a 434 limit is 108px of slack — enough that a longer item
string can never push a *title* off the bar, since item widths do not enter
the layout at all. `MENU_APPMAX` is 4 and all four are used.

**Dispatch.** `fm_oncmd` turns (menu, item) into one `FMC_*` id —
`bl = fm_menu_base[ah] + al`, bounds-checked, then `shl bx,1` and
`call [fm_jmp+bx]` (CALL r/m16 near-indirect, 8086-legal) — and repaints
afterwards, because the kernel does not repaint after an `AM_ONCMD`
returns (§12.2). Ids are contiguous **within** a menu, which is what makes
the base+item arithmetic work; they are internal constants and may be
renumbered whenever a menu gains an item. Every handler is a near proc that
preserves nothing but must not clobber the window pointer the repaint needs
(`fm_oncmd` parks it rather than trusting the handlers).

**Which side of the lock each command is on** is the single most dangerous
detail in this section, because `gfx_lock` is a non-recursive spin released
only by the UI task (§7) and `fm_oncmd` runs *inside* it:

| command | how it runs |
|---------|-------------|
| Open | `fm_open_sel` — inline (loader is deferred, `dsk_chdir` is I/O under the lock like Refresh) |
| New Folder… / Rename… / Delete | inline: enters edit mode, draws nothing but the status line. The disk is touched at **Enter**, not here |
| Free Space | `fmv_sync` first (§22.1 — the answer must be about OUR volume, and that sync is a full mount when another window navigated last), then `dskw_dfree`, which reads the resident FAT snapshot with no I/O of its own |
| Refresh / Drive A: / Drive B: | inline `disk_mount`, exactly as the button and the a/b/r keys already do |
| Up One Folder / Root Folder | inline: `fmv_sync`, then `dsk_dotdot` + `fmv_load` / `fmv_load` AX=0 |
| New Window / Open in New Window | **deferred** — seed + `inst_launch_post` (§29.4); at cap, `snd_beep` and nothing else, because `app_launch` would front an existing window and silently drop the seed |
| as List / as Icons | inline: set `FS_VIEW`, reset `FS_SCRL` |
| Clock / Bounce | **deferred** — `inst_launch_post` (§29.4); `app_launch` takes the lock |
| Close Window | **deferred** — `ui_post_cmd` CMD_CLOSE (§13); `ui_cmd` takes the lock |
| Restart | **deferred** — `ui_post_cmd` CMD_REBOOT; `ui_cmd` takes the lock and never gives it back |

Calling `app_launch`, `files_open`, `files_open_drive`, `files_refresh` or
`ui_cmd` from here hangs the machine permanently — no beep, no watchdog, no
recovery — so each entry in the jump table carries a one-line comment
saying which column of that table it is in.

Up One Folder at the root is a no-op rather than an error; there is nothing
above the root and nothing useful to say about it.

### The context menu — `fm_rclick` and `fm_rcmd`

A right-press inside a file-manager window's content pops a menu at the
pointer (§12.4). This is **not a convenience**. Under `WF_FULL` (§11.2) the
UI ladder routes rows 0..19 to `wm_hit` and the bar is neither drawn nor
clickable, so a fullscreen file-manager window would have no menu bar at all: the
context menu and the keyboard would be its entire command surface. **That
state is not reachable today** — `wm_fullscreen`'s only caller is API slot
0x0090, a package can only fullscreen its own window, and no shipped `.o88`
does; `ui_rdown`'s `[wm_fs]` test is insurance against a Locator Fullscreen
command that does not exist yet. It is written down because the day that
command lands, the context menu is what makes the mode usable at all. That is also
why the in-window Refresh and view-toggle buttons stay — a one-button
machine queues no `EVT_RDOWN` at all and must still be able to work.

`fm_rclick` (in CX/DX = absolute press point, SI = window; gfx lock held;
out CF = 1 nothing chosen) does, in order: publish the window (`fm_vp_set`),
`fm_edit_end` — a right-click abandons a half-typed name exactly as a left
one does — `wm_content` + `fm_layout` + **`fm_hit`**, then

- **on an entry**: `FS_SEL` = that index and repaint, so the row the menu
  is about is visibly the row under the pointer. `FS_CLKT` is deliberately
  **not** stamped (§13). Descriptor = `fm_ctx_fold` for a §19 type-2 entry,
  `fm_ctx_file` for anything else.
- **off an entry**: clear `FS_SEL`, repaint, descriptor = `fm_ctx_dir`.

then `menu_popup`, and it latches the chosen command in `[fm_rcmdid]`.
`fm_rcmd` runs that id through the **same `fm_jmp` table the bar uses** and
repaints — so every command has exactly one implementation and one
lock-side answer, and the table's per-entry comments cover both callers.
`ui_rdown` calls the two separately because only the second is billed
(§13).

| descriptor | when | items |
|------------|------|-------|
| `fm_ctx_file` | the row is a file | Open · Rename… · Delete |
| `fm_ctx_fold` | the row is a folder | Open · Open in New Window · Rename… · Delete |
| `fm_ctx_dir`  | empty space, the header band, below the last row | New Folder… · Refresh · Up One Folder · Root Folder · Drive A: · Drive B: · as List · as Icons |

A descriptor is three words in `.text` — the item-string array, a parallel
array of `FMC_*` bytes, and the count — and is **immutable**: several
windows share one, so nothing per-window may live in it. There is no
disabled-item mask; a command that has nothing to do (Up One Folder at the
root, Rename with the disk gone) is the same no-op it is from the bar, and
one rule beats two.

**`fm_hit`** (in CX = content-relative y, DX = content-relative x, after
`fm_layout`; out CF = 0 and AX = directory index, CF = 1 = not on an entry)
is `fm_onclick`'s row-area mapping, extracted so the click handler and the
context menu cannot disagree about which row a point is in — the same
argument `fm_layout` already won for the painter and the hit-tester. It
handles both views, including the icon grid's column rejection.

### Naming and confirming — the status-line edit mode

There is no dialog kind and no focus concept, so **the three commands that
need an answer turn the window's own status line into an input line**:
`FS_EDIT` = 1 (New Folder…), 2 (Rename…) or 3 (Delete). Only the front
window receives `W_ONKEY` (§13), which is precisely the window the user is
looking at, so this needs neither concept. Arming clears `FS_FERR` (the
prompt replaces the old verdict) and every *other* block's `FS_EDIT`, because
the buffers below are module-global.

| mode | armed by | line | Enter does |
|------|----------|------|------------|
| 1 | File ▸ New Folder…, or `n` | `New folder: NAME` | `dskw_mkdir` SI = `fm_ebuf` (§18.5) |
| 2 | File ▸ Rename… | `Rename NAME to: NEW` | `dskw_rename` SI = `fm_onam`, DI = `fm_ebuf` |
| 3 | File ▸ Delete | `Delete NAME? Enter=yes Esc=no` | `dskw_delete`, or `dskw_rmdir` (§18.6) if `fm_odir` |

Modes 2 and 3 require a selection; without one the command is a no-op, like
File ▸ Open. **The target is captured at arm time**, not read at commit time:
`fm_stage_name` fills `fm_onam` (the 8.3 display name — itself a legal
`dskw_name83` input, §19) and `fm_odir` (1 = the §19 type word was 2). That
is not defensive: `fm_name` is overwritten by every row the painter draws, so
the confirm line could not even display the name it is asking about without a
private copy.

While `FS_EDIT` is non-zero `W_ONKEY` **swallows every key** — that rule
is binding and is stated twice on purpose. In modes 1 and 2:

- **13 (Enter)** commits. Success clears `FS_FERR` and calls `fmv_bcast`
  (§22.1), which reloads every window on this folder and clears their
  `FS_SEL`/`FS_SCRL` — the remount rebuilt the listing, so a directory index
  is meaningless. Failure stores the `FERR_*` in `FS_FERR` for the status
  line. Either way the mode ends. An empty buffer just cancels.
- **27 (Esc)** cancels; **8 (Backspace)** removes the last character.
- **`.`** is accepted once, and only after at least one character.
- every other character goes through the write path's own **`dskw_char`**
  (§18.4) — upper-cased, and rejected if it is not in the FAT short-name
  set — while the length is under 12. Filtering with the *same* routine
  that will validate the name at commit time is the point: the line shows
  exactly the bytes that will be stored, so a name can never look accepted
  and then fail as `FERR_NAME`.
- a key with no ASCII (a bare scan code) is swallowed and changes nothing.

**Mode 3 is different, and deliberately so: Enter confirms and *every other
key cancels*.** There is no undo and no trash on this system — the confirm
line is the entire distance between a menu click and a file that is gone —
so the mode accepts exactly one affirmative keystroke and treats anything
else, Esc included, as "no". A stray character cannot leave a delete armed
and waiting for an Enter the user meant for something else. A click anywhere
cancels it too, through the same `fm_edit_end` that cancels a half-typed
name.

**Binding policy: no destructive command gets a bare-letter shortcut.** The
only key source is int 16h AH=00 and shift states are never read, so there
are no modifiers to hide behind — `d` on a stray keypress would be a deleted
file. Delete and Rename are reachable from the menu only. The keys that do
exist are `n` (New Folder…, harmless: it opens an input line) and
**Backspace = Up One Folder**, alongside the a/b/r/v and scroll keys above.

`fm_kinit` clears the mode, so a closed and reopened window never resumes a
half-typed name or a pending confirmation.

### 22.1 The view cache — the per-window claim and the `fmv_*` routines

**A `VIEW_KB` heap claim per open Disk window** (§2.3/§50), taken by
`fm_kinit` and owned by the window's instance, so the kernel frees it at
teardown and the Task Manager bills it to the row. It holds a plain image of
`disk_dir` at offset 0 and of `disk_icons` at offset 1024, so staging one
entry is the same `idx<<5` / `(idx<<6)+1024` arithmetic `dsk_get_dir` /
`dsk_get_icon` already contain, and filling it is two flat `rep movsw`
(1,024 + 2,048 bytes, ≈14 ms on a 4.77 MHz 8088) after a mount that already
cost tens of sector reads. There is no per-slot header: the state block and
the cache are 1:1, so drive/cwd/count/mok live in the block and are never
duplicated.

**A window whose claim was refused still works.** `[fm_vseg]` = 0 means "no
cache", and `fmv_copy_in` then falls back to the global `disk_dir` /
`disk_icons` snapshot while `fmv_store` does nothing — the window is correct
and merely re-lists more often, which is exactly the pre-cache behaviour.
That is the shape every claim in the tree is meant to have: memory is a
performance input, not a precondition.

| symbol | contract |
|--------|----------|
| `fm_vp_set` | in BX = window ptr. Publishes the three module words every routine below reads: `[fm_vp]` = that window's state block, `[fm_vseg]` = its cache segment, `[fm_vinst]` = its instance record (0 = unowned). Preserves all registers. Called at the head of `fm_layout` and of every window callback. |
| `fmv_get_dir` | in AX = entry index (< `FS_N`); out SI → `dsk_ent` — the **existing** buffer, so every consumer keeps the plain `SI ->` pointer it has today. Reads `[fm_vseg]`. |
| `fmv_get_icon` | in AX = entry index; out SI → `dsk_ico`. Same. |
| `fmv_load` | in AL = drive, DX = cwd cluster, DI = state block; **no gfx lock requirement, but real floppy I/O**. `dsk_chdir` (DL = AL, AX = DX), then copy the fresh global snapshot into the block's slot and write back `FS_DRV`/`FS_CWD`/`FS_MOK`/`FS_N` from what the mount actually produced (a failed mount lands at the root with `FS_MOK` = 0 — §19.2). Clears `FS_SEL`/`FS_SCRL`/`FS_FERR`: every index into the old listing is meaningless. Preserves all registers. |
| `fmv_sync` | in DI = state block. Returns immediately when `(FS_DRV, FS_CWD)` already equals `([disk_drive], [dsk_cwd])`; otherwise `fmv_load` on the block's own drive and cwd, **banking `FS_SEL` and `FS_SCRL` across it** — a sync is not navigation, the window is re-listed where it already was, and clearing them would scroll a background window back to the top on a double-click. **Every action calls this first.** Preserves all registers. |
| `fmv_bcast` | after a successful metadata write, re-copies the (already correct, §18.4) global snapshot into **every** live file-manager window on the same `(drive, cwd)` — the acting one included, since its own cache is stale too — and clears their `FS_SEL`/`FS_SCRL`. Pure memory, no extra I/O. |

**`fm_layout` is the sole authority on `[fm_vseg]`** (and `[fm_vp]`): it
calls `fm_vp_set` at its head, then mirrors **eight** fields — `FS_VIEW`,
`FS_MOK`, `FS_DRV`, `FS_EDIT`, `FS_FERR`, `FS_N`, `FS_SCRL`, `FS_SEL` —
into `fm_lview` / `fm_lmok` / `fm_ldrv` / `fm_ledit` / `fm_lferr` /
`fm_lnf` / `fm_lscr` / `fm_lsel` for the painter, which uses every register
including BP and has none free to thread a pointer through. (`fm_lpad`
exists only to keep the words after it even-aligned.) **The mirror list is
part of this contract**: a new per-window field that the painter reads and
that is not mirrored here reads the previous window's value. `fm_clamp_scroll` is the **one** write-back (every scroll
path already funnels through it). Any painter or hit-tester that reads a
cache without calling `fm_layout` (or `fm_vp_set`) first reads the *previous*
window's directory: wrong names, wrong icons, and a double-click that opens
the wrong file.

**Staleness is the accepted compromise, and it is bounded.** A background
window shows the listing from when it was last loaded or refreshed. Siblings
on the same folder are repaired for free by `fmv_bcast`. A swapped floppy
leaves every window showing the old disk until Refresh — identical to the
single-window behaviour, no new rule. A window on A: while the front window
works B: is stale by construction; there is no way to know without spinning
the other drive, and that is what a 1984 Finder did too. **A folder deleted
under a live window**: its `FS_CWD` now names a freed chain, `dsk_chdir`'s
mount validates it (§19.2), and `fmv_load`'s failure path leaves the window
at the root with `FS_MOK` = 0 — bounded, never a crash, but it does mean the
window silently jumps to the root.

**Choose or create** (`files_open`, `files_open_drive`; no lock held):
1. a live file-manager instance already on the target `(drive, cwd)` → front
   and un-minimize it;
2. else below `KD_CAP` → park the target in `fm_seed_drv` / `fm_seed_cwd`
   (`app_launch` has no argument channel — the `[cp_sel]`-before-`KIND_CTRL`
   precedent of §31) and `app_launch` KIND_FILES, whose `fm_kinit` consumes
   the seed and loads the window's cache;
3. else at cap → navigate the **frontmost** file-manager window in place.
   Nothing can fail, so there is no error path and no dialog.

`files_refresh` (called by `loader_run` when `[ld_painted]` is 0, no lock
held) is just gfx_lock / `wm_paint_all` / gfx_unlock. It used to look up
"the" Disk instance with `inst_find_kind` and check its visibility; with N
instances "the first one" is the wrong window, and `wm_paint_all` repaints
every visible window anyway — which is what the call was always for (the
loaded program's window may overlap any of them). It no longer runs on the
**successful** path at all: `ld_run_body`'s own `wm_show` is that pass, and
running both was the second whole-screen redraw a launch used to show.

### 22.2 Selecting costs two bands, and re-selecting costs nothing

A click in the row area used to end in `fm_repaint` like every other click:
white-fill the whole content, redraw the header, both buttons, every visible
row with its icon, name and size, the scroll bar with its arrows, track and
thumb, and the status line — about 130 glyphs and a dozen fills, to move one
inverted strip. The first click of a **double**-click paid it too, which is
what made a double-click flash.

The selection is drawn as an **XOR fill** over the row band (list view) or
the cell (icon view), and XOR is its own inverse, so the whole change is:
invert the band the selection is leaving, invert the band it is arriving at.
`fm_sel_bar` is that operation, factored out of `fm_draw_core` so the painter
and the click path cannot disagree about which pixels a selected entry owns —
the same argument that made `fm_hit` one routine. It takes a directory index
and range-checks it itself (`0xFFFF`, past `FS_N`, or scrolled out of the row
area all draw nothing), because one caller hands it an index it has not
looked at.

Four cases, and three of them draw nothing at all:

| the click | what changes | what is drawn |
|---|---|---|
| a different row | `FS_SEL`, `FS_CLKT` | two `gfx_xor_fill`s |
| the row already selected | `FS_CLKT` | **nothing** |
| empty space, something selected | `FS_SEL` = 0xFFFF | one `gfx_xor_fill` |
| empty space, nothing selected | nothing | **nothing** |

Measured on the shipped `APPS` folder, in both views: **zero `font_char` and
zero `gfx_fill` calls** for every row of that table but the first.

Two things hold it up:

- **It is correct only because `W_ONCLICK` fires on the frontmost window
  alone** (§13). Nothing is on top of it, so the content on screen is exactly
  what the last paint put there, XOR included — a stale band would otherwise
  be inverted back to something that was never drawn. §11.3's granularity rule
  does not apply either way: an inversion erases nothing, so there is no fill
  and no glyph to disagree.
- **An editor line is the one other thing a row click changes.** `fm_edit_end`
  runs first and cancels a half-typed name, which rewrites the status line, so
  `fm_onclick` captures `FS_EDIT` into `[fm_wased]` **before** ending it. Set,
  the fast path is skipped and the click takes the old `fm_repaint` — the
  status line has to be redrawn anyway and the repaint draws the new band
  itself.

`fdlg.inc` has the same rule and its own `fdlg_sel_bar` (§38.3): the geometry
differs, the argument does not.

### 22.3 Cut, Copy and Paste (`kernel/filecp.inc`)

The clipboard is **(drive, folder, name, type)** and deliberately not an
index: an index means nothing outside the listing it came from, and the
whole point is that the listing changes between the Cut and the Paste — a
different window, a different folder, possibly the other drive. A name is a
legal `dskw_name83` input (§19) and cannot go stale the way an index can,
which is the argument `fm_onam` already won for Rename. `..` cannot be cut
or copied (§19.5).

**Surfaces.** File ▸ Cut / Copy / Paste on the bar; Cut, Copy on a row's
context menu and **Paste Into** on a folder's; **Paste** on the empty-space
menu; and Ctrl+X / Ctrl+C / Ctrl+V on the keyboard — *control codes*, not
bare letters, because `c` and `v` are already this window's own shortcuts
and int 16h hands 0x18/0x03/0x16 straight over. Every one of them runs the
same `FMC_*` id through the same `fm_jmp` table, so no command has a second
implementation. A Copy stays on the clipboard and can be pasted again; a Cut
is spent by its paste.

**Paste takes its target as an argument** (`fcp_paste`, AL = drive, BX =
folder), because three surfaces name it differently: Paste means "this
window's folder", Paste Into means "the folder under the pointer", and a
drop means "the folder I was released over, possibly in another window"
(§22.4).

**A folder is copied recursively, and the source folder's name is created
inside the target** — pasting `SUB` into `DEST` makes `DEST/SUB/…`, which is
also what pasting *onto* a folder does. A folder that already exists is
**not** an overwrite: it is adopted and the recursion continues into it. A
*file* of that name where a folder must go is a genuine collision `mkdir`
cannot resolve, and it is reported.

**The overwrite question suspends the operation.** `fcp_step` runs until it
finishes, fails, or must ask; every byte of its state is in `.bss`, so the
pause is an ordinary trip through the event loop and `fcp_answer` resumes
it. The question is `FS_EDIT` = 4 — the Delete confirmation's mechanism with
a third answer. **A** sets `[fcp_all]` and nothing is asked again for the
rest of that operation; anything else stops it with what has already been
copied left in place.

It is drawn on **two lines** — `Replace NAME?` in the row area, then
`Enter=yes  A=all  Esc=stop` on the status line — because together they are
45 characters and 38 fit the default window, which is resizable smaller
still. `fm_stat_line` is the truncate-and-draw tail of `fm_draw_status`
lifted into a proc so the question can spend it twice; the extra row is
legal because a repaint redraws both, and the question is only ever up
between two of them.

Four things hold the engine up:

- **The recursion has no call stack.** A task stack is 512 bytes (§2.1) and
  a directory tree is attacker-supplied, so the walk keeps `FCP_MAXD` = 6
  frames of (source cluster, mirror cluster, entry ordinal) in `.bss` and
  iterates — `dskw_rmtree`'s discipline. What is different is that a copy
  does not consume the source as it goes, so each frame has to remember
  *where it was*: `fcp_scan` re-walks the directory to its ordinal, which
  costs FAT lookups out of the RAM snapshot plus one sector read, because
  `dsk_dirw_next` only computes LBAs. Deeper than `FCP_MAXD` is reported,
  never walked.
- **`fcp_goto` is quiet inside one volume.** `dsk_chdir` re-runs the whole
  mount because the *listing* must follow it, but nothing here reads the
  listing — `dskw_*` resolve names by walking `[dsk_cwd]`'s raw directory,
  and the FAT snapshot belongs to the volume, not the folder. So a move
  inside one volume is a word, and a tree copy that would otherwise pay two
  full mounts per directory pays none. Crossing to the other drive is a real
  mount, because then the snapshot genuinely is wrong. The cluster is
  range-checked here, since the quiet path skips `disk_mount`'s own
  `.cwd_lost` validation.
- **`[dskw_batch]` defers the coherence remount.** §18.4 rule 3 has every
  `dskw_*` re-mount on success; a tree copy would pay that per file, so the
  flag suppresses it and the operation ends with one `fmv_reload_all` — every
  live Disk window re-listed from its own (drive, folder), because a paste
  changes the destination, a Cut's source, and every folder it created.
  `fmv_bcast` reaches only the windows on the *current* (drive, cwd), which
  after a walk is wherever the walk stopped. The flag is **dropped while a
  question is outstanding**: the pause is a trip through the event loop where
  anything else that writes must behave normally.
- **`fcp_selfchk` refuses a folder pasted into itself or its own subtree**,
  by walking up from the destination looking for the source's cluster —
  the cheapest way to ask "is the source an ancestor" on a file system with
  no paths. Without it the walk creates a copy inside the directory it is
  reading and then finds that copy, forever.

Two limits, recorded rather than hidden:

- **A Cut inside one volume re-links rather than copies** — see §22.6. Across
  drives it is still a copy followed by a delete, which is the only thing it
  can be when the two volumes share no FAT.
- **File size is not a limit** — see §22.5. The buffer is one `MEM_K_COPY`
  claim (§50) sized from `mem_avail`, held for the whole operation
  *including* the part suspended on a question, and released by `fcp_stop`
  on every exit.

`dskw_stat` (name → CF, attribute, first cluster) is published for this: the
overwrite prompt has to ask "would this replace something, and is that
something a folder?" *before* writing, and no existing entry point could
answer without also doing the thing.

### 22.4 Dragging an entry onto a folder

A press on a row that leaves it by `FM_DRAGMIN` pixels becomes a **drag**;
released over a folder, it **moves** the entry there. That is the same
operation Cut-then-Paste performs (§22.3), and it literally is: `fm_drag`
arms the clipboard as a Cut the moment the drag begins and hands the drop
target to `fcp_paste`. Giving the drag a transport of its own would be a
second thing to keep correct.

It is `ui_drag`'s loop with three differences:

- **The gfx lock is already held**, because this runs inside `W_ONCLICK`.
  So it never calls `gfx_lock` — it unlocks and re-locks around the yield
  exactly as `ui_drag`'s tracking pass does, and returns holding it.
- **The outline is a row-sized rect**, drawn VRAM-direct for `ui_drag_xor`'s
  reason (§32): a transient overlay, always XOR-erased before the lock drops.
  `[fm_dgx]`/`[fm_dgy]` exist because the erase must use the coordinates the
  draw used — XOR only removes what it put there, and the pointer moves
  between the two calls.
- **The drop is resolved against every visible window**, not the one it
  started in. That is the point: a drag from a GAMES window onto a folder in
  an APPS window is the same move Cut and Paste would have made. `wm_hit`
  finds the window, `inst_win_owner` says whether it is a Disk window, and
  that window's own `fm_layout` + `fm_hit` say which row. A folder row (type
  2 or 3) is the target; a file row, a blank row or the header all mean
  "into the folder you can see".

A drop onto the folder it came from is a no-op, not a copy of everything
into itself. `..` cannot be dragged (§19.5). A drag is always a move — the
Macintosh rule within one volume, and the only one this system can honour
without a modifier key it has no way to report.

### 22.5 A copy is a stream, so size is not a limit

The copy **buffer** is a heap claim of whatever the machine could spare, and
a file may be larger than it. (`dskw_write` itself has no size limit — that
went in §18.4.1 — but a single call still needs the whole file in memory at
once, which is the thing a floppy-sized copy cannot promise.) A copy is
therefore a **truncating create followed by a run of appends**: `fcp_xfer`
opens the
source with `fcp_rdopen`, writes a zero-length destination through the
ordinary `dskw_write` (so the replace, the free-slot hunt and the old
chain's release all keep their §18.4 discipline), then loops read-chunk /
`dskw_append` until the source runs out. The buffer bounds the **chunk**,
not the file.

**`dskw_append`** (module-internal, no API slot) carries a precondition no
published entry point could: the file's current size must be a whole number
of clusters, so an append starts on a fresh one and never has to fill a
partial sector inside a chain that is already there. `fcp_chunkset`
guarantees it by rounding the chunk down to a cluster multiple; anything
else is refused rather than mis-written. Its commit order is §18.4's in the
only shape an append can have — build and write the new sub-chain, flush the
FAT so it is durable, link the last existing cluster to it and flush again,
and only then let the directory entry take the new size, which is the single
sector write that makes those bytes part of the file.

**The read side is not a `dskw_` entry point.** Nothing there writes: the
FAT snapshot is already in RAM, so stepping the chain is a lookup and the
only I/O is the data. `fcp_rdnext` reads whole clusters and clamps the
*count*, never the transfer — the trailing bytes of the last cluster are
allocated sectors of the file's own chain, so reading them costs nothing and
saves a second, ragged case.

**Chunk size.** As much as the heap will give, up to 65,024 bytes — 127
sectors, because 64KB exactly wraps a 16-bit byte count — then rounded down
to whole clusters. Big is what we ask for: every chunk is a seek, and a real
floppy is where that is felt.

**The buffer is 512-byte aligned by hand.** `mem_claim` is only paragraph
aligned, and §2's rule that every disk-visible base is 512-aligned exists
because int 13h moves one sector per call and the DMA controller answers a
64KB-boundary straddle with error 09h. The claim's base is rounded up to a
32-paragraph boundary and one KB given back, which costs at most 496 bytes.

Two bugs this found, both worth naming because neither was in the new code's
logic:

- **`mem_claim` answers in DX, not AX.** `filecp.inc` read AX, which still
  held the KB asked for — so the copy buffer was segment 0x0040 and every
  read scribbled over the interrupt vector table. A small file wrote too
  little to matter; the first file big enough wedged the machine inside
  int 13h. The counters that found it bracketed `disk_read`: entered 23
  times, returned 22.
- **A read that crosses a 64KB physical boundary is not a *logic* error**,
  so it does not fail — it hangs in the BIOS. That is why the alignment is
  explicit here rather than assumed.

### 22.6 A move inside one volume is three sector writes

A same-volume Cut needs no data I/O at all: the cluster chain is already
where it belongs and only the *name* of it changes hands. `fcp_relink` moves
the directory entry — a read and a write per cluster becomes three sector
writes, so a folder of packages moves in less time than the copy path took
to read one of them.

**It declines rather than competes.** Another drive, a name the destination
already holds, a full destination directory, a source it cannot read: it
answers "not attempted" and `fcp_step` falls through to the copy engine,
which is the one that knows how to ask about overwrites. That is what keeps
this an optimisation rather than a second set of semantics, and it is why
the overwrite prompt, the recursion and the suspend/resume machinery needed
no changes at all.

Two things make it cheap enough to be worth having:

- **`dskw_raw` still holds the SOURCE's thirty-two bytes** when the
  destination's lookup fails — `dskw_stat` bails inside `dskw_find`, before
  `dskw_ent_load` could overwrite them. So the entry that lands in the
  destination is the source's, carrying its real 32-bit size *and* its
  original timestamps. Building a fresh one through `dskw_commit` would have
  given neither: that path writes the size from the 16-bit `[dskw_len]`.
- **`[dskw_zapnext]` is consumed by `dskw_ent_store`, not by
  `dskw_commit`.** Taking the slot that *is* the end-of-directory marker —
  the common case in a young directory — is therefore handled by the same
  routine that writes the entry, and the three-line slot hand-off
  (`[dskw_fsec]`/`[dskw_foff]`/`[dskw_fend]`) is the whole of it.

**The order is binding: the destination entry is written before the source
is unnamed.** A crash between the two leaves the chain named from two
directories — visible, and the user can delete one. The other order loses
the file. `fcp_rlzap` is the one delete in this system that leaves the
clusters alone, because they are not free: the entry just written owns them
now, and `dskw_delete` would take the moved file with it.

**A moved folder's own `..` follows it** (`fcp_rlup`). Entry 1 of a
subdirectory is `..` by spec — the same fact `dsk_dotdot` rests on (§19.2) —
and it is checked rather than assumed, because everything read off the disk
is hostile. Without it the folder still lists and still opens, but going up
out of it lands in the folder it used to live in.

## 23. Minesweeper — the first software package (apps/mines/mines.asm)

Not kernel code: a .o88 package built with os88api.inc, org 0 (§20.1), all
services via the API table. Label prefix `mn_`. Everything below is
content-relative; the procs fetch the content origin via `wm_content`
(JAPI) each call.

- Window: "Minesweeper", 146×183 at (240,120) → content 144×164 — the
  template. The board is laid out from these constants, so on a screen too
  short for it `wm_fit` clamps the frame (§39.7) and the bottom rows are
  clipped at the dock; it still plays and closes (§39.9).
- Board: 9×9 cells, 16×16 px each, at content (0,20). 10 mines.
- Status strip (content rows 0..19, light gray): mines-remaining counter
  "10" minus flags placed (may go negative → clamp display at 0) at left;
  center text: playing → "F=FLAG" when flag mode is ON else blank;
  lost → "BOOM! N=NEW"; won → "YOU WIN!". Keep every string ≤ 17 chars.
- Cell rendering, 16 colors (on a 1bpp adapter §39.4 reduces them to black,
  white and the dither class — its map is chosen so exactly these
  distinctions survive), Mac-meets-Win31: covered = light gray face,
  2px white bevel top/left, 2px dark gray bevel bottom/right; flag = red
  (12) pennant + black mast on a covered cell; revealed = white face, 1px
  dark gray grid border, centered colored digit; digit colors:
  1=1 (blue), 2=2 (green), 3=4 (red), 4=5 (magenta), 5=6 (brown),
  6=3 (cyan), 7=0 (black), 8=8 (dark gray) — the 8 is the one digit that cannot
  reduce to black, because 1..7 use all seven indices that do. It keeps dark
  gray because that is the only one of the six dither-class colours with enough
  contrast on a white cell face; the alternatives are light gray (too faint to
  read) and three bright colours (worse), and light blue collides with the 1.
  **This digit is why the disabled state is a flag and not a colour** (§46): a
  first cut had `font_ink` dither every `CDGRAY` glyph on mono, which made a
  Minesweeper 8 come out looking exactly like a menu item you cannot pick.
  Mine = black disc with spokes;
  the one you clicked sits on a light-red (12) cell, others (shown on
  loss) on light gray. Wrong flags on loss: mine + black X.
- Input: W_ONCLICK reveals (or flags, in flag mode) the cell under the
  click. W_ONKEY: 'f'/'F' toggles flag mode (repaint status strip),
  'n'/'N' new game (repaint content). Space = reveal is not required.
- Rules: first reveal is always safe — mines are placed lazily on the
  first reveal (osapi_srand with osapi_get_ticks, then osapi_rand), excluding
  the clicked cell. Reveal of a 0-count cell flood-fills neighbours
  (iterative, explicit queue in the package's own buffer — no recursion;
  stack budget is the UI task's, §2.1). Flags block reveal. Reveal of
  a mine → lost (reveal all mines, mark wrong flags). All 71 safe cells
  revealed → won. After win/lose, clicks are dead until 'N'.
- Handlers repaint only what changed (single cells; whole board on
  new-game/win/lose), Note-Pad-style, under the caller's lock.

## 24. Packaging toolchain (host side)

Python 3, stdlib only, both tools executable with clear argparse `--help`
and non-zero exit + stderr message on any validation failure.

- `tools/os88pkg.py IN.bin -o OUT.o88` — package validator and stamper.
  IN is the org-0 assembly, and it is the only assembly there is: **v3 has
  no relocation table** (§20.2), so the dual assembly at 0xB000/0xB800, the
  diff scan, the byte-exact reconstruction check and the author rule about
  whole-word package addresses are all retired, and with them the class of
  bug where an address folded into a constant assembled cleanly and
  relocated wrong. What is left is validation, which matters more than it
  used to. Any failure → exit 1 on stderr, no output: magic; version 3
  (a v2 file says so and asks for a rebuild against the v3
  `apps/os88api.inc`); flags bits 1–7 zero; **link base 0**; the four
  **dispatcher bytes `FF D5 CB 00` at offset 12** — an image without them
  would send the kernel into its data on the first paint, and the
  `OS88_HEADER` macro is what emits them, so a failure here means the image
  was built against an older SDK; image field == file size; entry in
  [0x20, image) (≥ 0x60 with the icon flag); image + bss ≤ `APP_MAX_SIZE`
  (0xF000); with the icon flag, image ≥ 96; name printable, ≤ 15, NUL-padded
  with nothing after the terminator.
- `tools/os88disk.py -o OUT.img --size {1440,360} [PKG.o88 ...]` — builds
  a FAT12 data floppy per §19. CLI shape unchanged from the os88fs era —
  the Makefile call sites need zero edits; pure Python 3 stdlib (no
  mtools). These two are the only geometries: a `--size 2880` (2.88M ED,
  FAT16) existed so the kernel's FAT16 path had a positive test and went
  with it (§2.1). The tool still derives the FAT type from the cluster
  count exactly like the kernel does, and its `--verify` fsck carries the
  same `1 ≤ FATSz16 ≤ 10` bound as mount rule 10, so a volume the kernel
  would refuse fails on the host too.
  - Package validation, kept in step with os88pkg: magic 0x384F,
    version 3, image field ∈ [32, filesize], image == file size, non-empty
    printable header name, size ≤ 0xFFFF (an older file fails with
    "rebuild with the v3 toolchain"). Corruption surfaces on the host, not
    on the 8086.
  - 8.3 names derive from the **host filename** (basename, uppercased),
    not the header name field (8.3 cannot hold the 15-char header names —
    the Disk window shows "MINES.O88", a deliberate, documented UX
    change; running instances still show the header name, §19/§21).
    Validated: stem 1–8 chars, ext exactly `O88` (the kernel only marks
    `*.O88` entries loadable, §19), charset [A-Z0-9_-]; duplicates, other
    characters, and reserved DOS device stems (CON, PRN, AUX, NUL,
    COM1–9, LPT1–9) rejected.
  - Emission: boot sector per §19 (BPB + the fixed message stub); FAT1
    with the reserved entries; files allocated **contiguously in argument
    order from cluster 2** (argument order = root order = index order —
    QMP tests click by row; the kernel never relies on contiguity, it
    chain-walks); FAT2 = copy of FAT1; root dir: the volume-label entry
    first (attr 0x08, `"OS8088APPS "`, fixed timestamp — filtered by the
    kernel, so index 0 stays the first package), then one entry per
    package: attr 0x20, NT byte 0, fixed timestamps (date 0x5C21 =
    2026-01-01, time 0), FstClusHI 0, FstClusLO, **exact byte size —
    never padded** (§21's truncation guard depends on it). File data is
    zero-padded to cluster boundaries on disk only.
  - Determinism: fixed BS_VolID 0x88000888, fixed timestamps, fixed
    label, contiguous argument-order allocation ⇒ byte-identical images
    across rebuilds (`make` twice → `cmp` clean).
  - Zero packages is legal (an empty disk — useful for testing Refresh).
    Fails (exit 1 + stderr): >32 packages (the §19 listing cap), clusters
    needed > capacity, duplicate/invalid 8.3 name. Total image size:
    1474560, 368640 or (test-only) 2949120 bytes.
  - `--scramble` (hidden test flag): reallocates cluster chains
    round-robin-interleaved across files — a **legally fragmented** image
    for chain-walk verification. Never used by the Makefile.
  - `--verify IMG`: standalone structural fsck — the §18.2 BPB rules, FAT
    type detection, FAT1==FAT2, every entry's chain walks to EOC with
    length matching its byte size, no cross-links, no lost clusters.
    Subdirectories are recursed (their files' clusters count as used —
    foreign OSes create them freely); a nonzero FstClusHI is a printed
    note, not an error (FAT32-only field; the kernel ignores it too).
    Runs on both FAT12 and FAT16 images and on foreign-written disks;
    doubles as the test-plan oracle.
- Makefile: one `nasm -f bin -w+error -I apps/` per package (dep on
  apps/os88api.inc) — a single assembly each since v3 — fed to os88pkg.py,
  then `build/apps.img` (1440) + `build/apps360.img` (360) via os88disk.py;
  all built by `all`.
  Directory order on the apps disks stays pinned, because scripted tests
  click by row (§22) — but it is pinned **per folder**: the root is
  `APPS` then `GAMES`, `APPS` holds hello, notepad, piano, fractal,
  paint, recorder, tracker, artful and then the data file `BEVERLY.MOD`
  (the shipped module the Tracker package plays; a data file rides its
  folder exactly like a package, always after every `.o88` so package row
  indices never shift), and `GAMES` holds mines, solitaire, arkanoid,
  each new package appending at the end of its own folder. The grouping
  lives in the Makefile (`APPS_TOOLS`/`APPS_GAMES` → the `DIR:`-prefixed
  `APPSARGS`), not in the tool. `run`/`debug`/`test` attach
  build/apps.img as floppy index 1. 86Box's fdd_02 gets apps360.img
  (best-effort config keys).

### 24.1 Data files on a volume

`os88disk.py` validates `*.O88` as packages and ships **anything else as-is**,
so a disk can carry a module, a picture or a text file next to the programs
that read it. The 8.3 name check, the per-directory cap and the duplicate
check apply the same either way; only the package validation is skipped.

Nothing in the kernel changed for this. A non-package already listed with the
generic icon and did nothing on a double-click — that has always been the
behaviour for any file a host OS put on the volume, and this only makes the
*builder* able to produce one. The shipped apps disks carry no data files;
`build/filetest.img` carries `BIG.DAT` for §18.4.1's check, generated rather
than committed.
## 25. icons.inc — icon format, draw routine, built-in library

1-bit icons with a mask, classic Mac style, drawn exactly like the mouse
cursor: a white underlay pass (Set/Reset white + Bit Mask from the mask
rows) then a black pass (data rows). In-memory record:

```
db wwords        ; row width in words (1 = 16px, 2 = 32px)
db height        ; rows
; then: height × wwords mask words, then height × wwords data words
; bit 15 = leftmost pixel of the word's 16px span; words left→right
```

| symbol      | contract                                                     |
|-------------|---------------------------------------------------------------|
| `icon_draw` | in CX=x, DX=y (top-left), SI → record. Caller holds the gfx lock. Clips to the screen (skip clipped rows/bytes — windows can hang off the bottom edge). Against an armed clip region (§11.3) the clip is instead **whole-icon**, like `font_char`'s cell: drawn iff the whole body lies inside one fragment, skipped otherwise. Leaves the GC in the default state (§1 rule 7); preserves registers. |
| `icon_draw16`| in CX=x, DX=y, SI → **body only** (16 mask words + 16 data words, no 2-byte header) — the §19 harvested-icon / §20.2 embedded-icon layout. |
| `ico_disk32`| library record: 32×32 floppy disk (rect body, shutter, label area) |
| `ico_app16` | library record: 16×16 generic application (a recognizable "program" glyph, e.g. a diamond/tool shape) |

**Every built-in kind carries one** (`KD_ICON`, §29.3), in the header-less
`icon_draw16` layout, stored in `instance.inc`'s `.text` next to
`inst_kinds`: About is the DIP chip the System menu draws, Clock a face with
hands, Bounce a ball with its arc and floor, Disk a folder (the drive icon is
desk.inc's job, §26), Task Manager a bar chart on an axis, Control Panel
three sliders. `KD_ICON` = 0 still means "the generic icon" and still works;
what it stops meaning is "every built-in", which made the dock six identical
tiles. The last two were the test: a framed list of bars and a framed list of
sliders are the same shape at 16px, and had to be redrawn until they were
not.

Bitmaps are hand-authored `dw` rows (like the menu-bar logo, the one
sanctioned place for hand-made bitmap data — icons are the second).

Under the software renderer (`[bb_on]`: double buffering on VGA, always set
on a 1bpp adapter — §32/§39.5) `ico_core` branches after its clip/shift
setup to a software pass pair: the white underlay ORs the shifted mask-row
bits into all `[vid_planes]` planes at `[vid_rseg]` — the back buffer on
VGA, the framebuffer itself on mono — the black pass AND-NOTs the data-row
bits out of them, same 3-byte window and same edge clipping as the VRAM
passes. Its row advance goes through `gfx_nextrow` (§39.3), because the
mono framebuffers are banked.

## 26. desk.inc — desktop drive icons

State: `desk_ndrives` (byte), `desk_sel` (byte, 0xFF = none),
`desk_clkt` (word). `desk_init` (from kmain): int 11h equipment word —
if bit 0 is set, drives = ((AX>>6) & 3) + 1, else 0; clamp to 2. QEMU
with two floppy `-drive`s reports 2.

Layout, per drive i (0 = A, 1 = B): the column hangs off the RIGHT edge, so
its left column zx is derived at boot — `[vid_desk_zx]` = screen width − 56
(§39.2), 584 at 640 wide and 664 on a 720-wide Hercules, where a pinned 584
would strand the icons 88px shy of the edge. Hit zone x zx..zx+47, y
(32+60·i)..(75+60·i); inside it the 32×32 `ico_disk32` at x=zx+8,
y=32+60·i, and below it the label "Disk A"/"Disk B" (48px), black text
on a white gap 2px around the text, centered in the zone.

| symbol       | contract                                                    |
|--------------|--------------------------------------------------------------|
| `desk_paint` | draw every drive's icon + label; the selected one (desk_sel) gets `gfx_xor_fill` over its hit zone. Called by wm_paint_all after the desktop fill (lock held by caller). |
| `desk_zone_rect` | in AL = zone index; out AX,BX,CX,DX = that zone's **drawn** rect, inclusive — `[vid_desk_zl]`..`[vid_desk_zr]`−1 horizontally, so the label's 2px overhang each side is inside it, not the 48px hit zone. Clobbers all four. |
| `desk_dmg_zones` | in `[wm_dmg_*]` (§11.91); out AL = bit n set = zone n is inside the damage rect, **and the damage rect grown to cover every one of them**. The growth is not slack: a zone is redrawn whole, so a window sitting over it has to be marked too. |
| `desk_paint_mask` | in AL = the bitmask `desk_dmg_zones` returned; draw those zones. All registers preserved. |
| `desk_click` | in CX=x, DX=y (no lock held; called by ui.inc when wm_hit found no window and `dock_click` declined the click, §30). Zone hit: if same zone as desk_sel and [ui_click_t]−desk_clkt < 9 (birth ticks, §10) → clear the selection and call `files_open_drive` with AL = drive. Else select it, stamp desk_clkt. Miss: clear any selection. All its own drawing (selection flips) happens under gfx_lock/gfx_unlock acquired internally, redrawing only the affected zones — EXCEPT when a visible window overlaps a zone's drawn rect (x `[vid_desk_zl]`..`[vid_desk_zr]`−1 = zx−2..zx+49 with the label overhang — 582..633 at 640 wide, §39.2 — window rect incl. the 1px shadow): a partial redraw would paint desktop over that window, so the flip falls back to a full wm_paint_all under the same lock. |

Selection is purely visual bookkeeping; a window covering an icon simply
paints over it (desk_paint runs before windows in wm_paint_all), and
clicks over windows never reach desk_click.

## 27. HELLO and NOTEPAD — the second and third packages

Deliberately minimal, to prove the SDK surface and the no-icon fallback:
`OS88_HEADER 'HELLO', entry` (no icon flag — the Disk window must show
`ico_app16` for it), one window "Hello" 240×90 at (200,150), paint =
two centered lines: "Hello from a" / ".o88 package!", no onkey, no
onclick, no bss. Entry: wm_create, return BX/CF. Prefix `hl_`.

**NOTEPAD** (`apps/notepad/notepad.asm`, prefix `np_`) is the former
built-in Note Pad kind, moved out of the kernel to reclaim the 1,383 bytes
it cost there — 281 of code and 1,036 of .bss, nearly all of the latter a
fixed two-instance text pool. Behaviour matches §14 plus resizing: one
window "Note Pad" 260×180 at (60,60), **resizable** — the entry calls
`wm_sizable` (slot 0x0108) right after wm_create, making it the first
package to exercise §11.1 and the proof that the SDK path works. Paint
renders the buffer at 8px per char with a 6px left/top margin, wrapping at
the content width, dropping any row whose bottom would pass the content
bottom (no scrolling) and drawing a 1px caret only when its own row fits —
all computed from the **live** window record each call, which is exactly
why resizing costs the paint proc nothing: the next repaint re-wraps at
the new width. Onkey edits at the caret, then white-fills (live W_W/W_H) and
redraws **its own content only**, ending with `wm_grow_paint` per §11.1 — the
white fill erases the grow box. It carries the page icon of §25; `apps/hello`
is the one package that still ships without one.

**The caret moves** (`[np_cur]`, the character index it sits in front of).
Left/Right step it, Up/Down keep the pixel column across rows, Home/End go to
the ends of the display row, Delete removes what is in front of it, backspace
what is behind, and a **click puts it where the user pointed** — `np_onclick`
is the window's W_ONCLICK. Typing inserts at the caret rather than appending,
and the gap is opened by hand rather than with `rep movsb`, because a callback
is entered with ES = KERNEL_SEG (§20.1) and a string move would open that gap
in the kernel's memory at the package's offsets.

**One walk answers all of it** (`np_walk`). Painting the text, finding the
pixel a caret index sits at, turning a click into an index and finding the
index at a column of a neighbouring row are the same traversal asked four
questions, and the wrap rule they share — an 8px cell that would cross the
right edge moves to the next row, a row that would cross the bottom is skipped
while the pen keeps advancing — is subtle enough that a second copy of it
would be wrong within one edit. `[np_draw]` selects paint or measure; every
index 0..`[np_len]` is visited, **including the one past the last character**,
because that is where the caret lives in a note that ends in text. The caret
occupies a cell and therefore wraps like one, which keeps it in front of the
character it precedes instead of stranded at the end of the row above.

The half-cell rule places a click: the left half of a character puts the caret
before it, the right half after — with **newlines excluded from the right
half**, which is what makes End land before the line break rather than at the
start of the next line. An extended key is recognised by AL = 0 before AH is
looked at, because the numeric keypad sends `4 6 8 2 7 1 .` with exactly the
scan codes of Left, Right, Up, Down, Home, End and Delete.

Two things got simpler in the move. The built-in reached its state through
`inst_of_win` → `I_SPTR` because every instance shared one pool; a package
addresses its own bss directly. And the cap is gone: the pool that fixed it
at 2 no longer exists, so instances are bounded only by the heap and the
instance table like any other package.

### 27.1 Save and load — the file API's first caller

NOTEPAD is where §18.4 becomes visible to a user, and the package-side
proof that the file slots work from a plain window callback. Two keys, DOS
Editor's: **F2 saves** (scan 3Ch), **F3 loads** (scan 3Dh); both arrive
through the existing onkey with AL = 0.

**The file used to be a fixed `NOTES.TXT`** — not because one note is
enough, but because a package had no way to ask for a name. §38 removed
that excuse, and NOTEPAD is the file dialog's first caller exactly as it was
the file API's. Each instance now carries `np_name`, a 13-byte current
document name seeded to `NOTES.TXT` at launch, and four File commands:

| command | behaviour |
|---------|-----------|
| **New** | empties the buffer and resets the name to `NOTES.TXT` |
| **Open…** | slot 0x0150 in Open mode, default = the current name; the callback stores the chosen name and loads it |
| **Save** (F2) | writes `np_name` — no dialog, the second and later saves of a document are silent |
| **Save As…** | slot 0x0150 in Save mode; the callback stores the chosen name and writes it |

F3 is **Open…**, i.e. it now raises the dialog rather than re-reading a
fixed file — the one behaviour change to an existing key, and the reason
for it is that a load with no way to say *what* was never the useful half.
Both dialog commands go through one completion proc, `np_ondlg` (AL = mode,
SI = our window, DI = the name): it copies the name into `np_name`, runs
`np_load` or `np_save`, and repaints its own content, which §38.5 requires
and the kernel does not do for it.

Instances no longer share one file, so the last-save-wins note is gone with
it; two Note Pads on two documents are now the ordinary case.

**Line endings are translated**, which is the point of writing a DOS
filesystem at all: the buffer stores a bare 13 on Enter (§14), the file
gets `CR LF`, and a load folds `CR LF` — and a lone `LF` — back to 13. A
note written here opens correctly in Windows Notepad, and one written there
opens correctly here. A load folds **in place** inside the document claim
(§27.3) and needs no staging at all; a save expands, so it takes a second,
transient claim sized at 2 × `[np_len]` — the worst case, every character a
newline — and hands it back the moment the write returns. A refused claim is
an ordinary path: the toast says "No memory" and the note is still there,
still editable, and still saveable when something gives memory back.

**Feedback is a toast**: `np_msg` (a near pointer, 0 = none) is drawn by
`np_paint` as a black-framed white box at the content's top-right — "Saved
<name>", "Loaded <name>" (both composed into `np_tbuf` around the live
`np_name`, since the name is no longer a constant), "Truncated", or the
`FERR_*` code mapped
through an eleven-entry table indexed by the code itself ("Done", "No
disk", "Disk error", "Bad name", "Not found", "Name exists", "Disk full",
"Dir full", "Protected", "Write protected", "Too big"). It is cleared by
the **next** keystroke — by an edit, or by the next save/load replacing it —
so it never becomes stale furniture, while a key the app ignores leaves
both the toast and the screen alone. It is drawn last, so it sits above the
text.

Save is `ES = DS` + slot 0x0120; load is slot 0x0128 with the buffer
capacity, mapping `FERR_BIG` to the same "Too big" toast the truncation
path uses. Neither call happens in the paint proc — both run in onkey,
which already holds the gfx lock, so the write's stall (§18.4) lands on a
keystroke and not on a repaint.

Removing the kind renumbered two pinned sets — `KIND_CLOCK`..`KIND_CTRL`
down by one (§29) and `CMD_CLOCK`..`CMD_REBOOT` down by one (§12), the File
menu losing its first item and re-basing on `CMD_CLOCK`. Directory order on
the apps disk stays mines, hello, notepad: the first two keep their indices
so existing tests are unaffected.

### 27.2 Row signatures — a keystroke redraws one row, not the note

Typing one character used to cost a white fill of the whole content and a
`font_char` per character in the note, and Up/Down cost that plus two extra
measuring walks. Nearly all of it redrew pixels that had not moved: an edit
at the caret cannot change a row **above** it, and it cannot change a row
below the newline that ends the caret's paragraph either, because a newline
resets the pen.

Each visible row therefore carries a one-word **signature** in `np_sig`: a
rotate-then-add fold of the characters drawn on it, plus the caret's column
when the caret is on it. Two layouts that fold to the same word put the same
glyphs at the same pixels, because on any row the k-th glyph is always at
`[np_tx] + 8k`. It is a hash and not a proof — the same trade the Task
Manager's rows make (§28) — so a collision leaves one row stale until its
content moves again.

`np_redraw` is then two walks:

1. **Measure and compare** (`np_draw` = 0, `np_sigup` = 1). Folds each row,
   compares against `np_sig`, stores the new value, and widens
   `[np_dr0]`..`[np_dr1]`. If `np_dr0` comes back 0xFFFF nothing on screen
   moved and the routine returns having drawn nothing at all.
2. **Draw the band** (`np_draw` = 1, `np_clip` = 1). The walk again,
   drawing every row inside the range and skipping every row outside it.

Measured on a 410-character note, 20 keystrokes at the end, counting calls
inside the kernel: `font_char` **8,410 → 350**, and scanlines filled
**5,020 → 1,960**.

Five things hold it up:

- **A range, not a bitmap.** The interesting cases are all contiguous — one
  row for a keystroke, one paragraph for a reflow, two adjacent rows for
  Up/Down — and a range needs no indexing and turns the erase into one fill.
  A click that jumps far redraws everything between, which is still no worse
  than the old unconditional full repaint.
- **The caret is in the signature**, folded in `np_ask` between the glyph
  before it and the glyph after it. Moving it has to dirty both the row it
  left and the row it arrived on, or it stays drawn where it was.
- **A newline is folded into neither row.** It occupies no cell, so the row
  it ends has the same pixels with it and without it. The row that
  *disappears* when it is deleted is handled at the other end: after the
  walk ends, `np_walk` keeps flushing rows until `[np_vrows]`, so a note
  that shrank compares its vacated rows against their old signatures and
  erases them.
- **`np_paint` is the baseline, and never clips.** The kernel white-filled
  the content on the way there, so that pass draws every row *and* records
  what it drew. `np_measure` — the query pass behind a click or an Up — must
  not touch the signatures at all: nothing has been drawn.
- **`np_sigsame` is the escape hatch**, and it tests five things: the four
  numbers `np_bounds` produced, so a resized window falls back to a full
  repaint, and `np_msg`, because the toast is drawn *over* the text by
  `np_toast` and is in no row's signature — the keystroke that retires one
  has to erase it the only way this module can, by painting the content
  again.

**A dirty row is drawn as ONE opaque `font_run`, and there is no band fill.**
It used to be a `gfx_fill` of rows `dr0..dr1` followed by a `FONT_CHAR` per
cell in pass 2 — the erase-and-letter pair, which on a 4.77MHz 8088 leaves the
line *blank* for several display frames and flickers on every keypress
(§6.1/§11.94). `np_walk`'s draw pass accumulates each row into `np_rbuf`
instead of drawing it, and `np_rflush` paints the row at each row transition
and at the end of the walk. The buffer is padded with **spaces** to
`np_rcols`, and that is the whole trick: `font_run` paints a space as
background on its fast path, so the padding *is* the erase and no cell is ever
momentarily blank.

Seven things this has to get right, and the first one is not obvious:

- **Rows the walk no longer reaches must still be blanked.** A backspace that
  pulls a wrapped line back up, or a deleted newline, leaves rows below the
  text whose old pixels are still on screen. The band fill erased them for
  free because it covered `dr0..dr1` whether the walk got there or not; row-by-
  row drawing does not, so `.done` walks on to `np_dr1` blanking as it goes.
  Without it a deletion leaves the row's last state behind — **caret included**,
  which is how the first test of this showed it.
- **The caret is banked, not drawn.** `np_carets` used to draw it during the
  walk; the row's run would now paint over it, so it records its x in
  `np_rcx` and `np_rflush` draws it after the run.
- **The margins are still fills.** The run covers `np_tx` to the last whole
  cell, so the inset left of the pen and the under-8px tail past the last cell
  are two thin fills over the dirty band. Neither carries a glyph, so neither
  can flicker and neither can disagree with anything at a clip edge (§11.3).
- **A row is diffed against what was last drawn on it, and only the changed
  span is run.** Typing at the end of a line changes exactly one cell, plus
  the two the caret leaves and arrives at — three cells out of forty. The
  cache is one row (`np_prow`, `np_prowi`, `np_prcc`) and it is deliberately
  the row that was last *drawn*, which under steady typing is the caret's:
  rows the walk visits but `np_rowdirty` skips leave it alone, so it does not
  get overwritten by the other rows of the same walk. It is invalidated at
  birth (`.bss` is zeroed and 0 is a real row index) and on the full-repaint
  path, which is where every disturbance that is not our own row draw ends up.
  The caret's old and new columns are folded into the span because a bare
  arrow key moves the bar without changing a character, and the cell it
  vacates has to lose it. Measured under QEMU with a counter in
  `font_run_cell`: a keystroke draws **2 cells**, at any note length and at
  any window width.
- **`np_clean` is what keeps the padding from costing rows × width.** Padding
  to `np_rcols` is only needed to erase something; after a `gfx_fill` there is
  nothing there, so a run may stop at the row's last real character. The flag
  is set for exactly as long as that is true — `np_paint` (the kernel filled
  the content on the way in) and the reconcile of §27.3 (which fills its own
  band) — and a row that trims to nothing is not drawn at all. Without it a
  fullscreen repaint is 50 rows of 90 cells whether the note has 500
  characters or five, which on a 4.77MHz 8088 is about five seconds.
- **The grow box is restored only when the dirty band could have reached it.**
  It is 13x13 at the content's bottom-right, and `OSAPI_WM_GROW` used to be
  called on every keystroke — it had to be, because the band fill spanned the
  full content width. `wm_grow_paint` fills the square before framing it, so
  that was the erase-and-letter flash surviving in one corner: with the rows
  fixed, the resize handle was still visibly flickering. One comparison of
  the band's last row against `np_bot - 12` answers it.
- **`np_rcols` is clamped to `NP_MAXCOL`**, and the accumulate step range-checks
  against it. The wrap rule means a cell past the band cannot normally arise;
  the clamp is what makes that a bounded write rather than a claim.

The full-repaint path (`np_sigsame` says the signatures no longer describe the
screen) still fills the content whole first — there the fill is erasing
everything below the text as well, and a full repaint is not what a keystroke
pays for.

### 27.3 The visual break — typing in FRONT of text without reflowing it

Inserting a character at the front of a note moves every character after it,
and there is no cheaper way to draw that than to draw it: forty rows of forty
cells is 1,600 cells, and a cell is about a millisecond (§6.1.1). One
keystroke, most of a second. §27.2's delta cache does not help — those cells
really did all change.

So they are not drawn. The rows below the caret are **scrolled down one row**
(`OSAPI_GFX_SCROLL`, §5.5), and what the screen then shows is the note with a
line break at the caret that the note does not contain: the text after the
caret hangs on the next row at the column it already occupied, and everything
below it has moved down. The caret keeps the rest of its own row to type on at
§27.2's two cells a keystroke, and when it runs out of row the rows below are
pushed down again.

```
before                  after inserting 'z' at the caret (|)
+----------------+      +----------------+
|mnoab|cdefghijab|      |mnoabz|         |
|cdefghijabcdefgh|      |      cdefghijab|
|ijabcdefghijabcd|      |cdefghijabcdefgh|
|efghij          |      |ijabcdefghijabcd|
+----------------+      +----------------+
```

`[np_bmode]` is the flag, `[np_bcrow]` the row the break sits on and
`[np_borig]` the row it started on. A keystroke while it is up is **one**
walk — no measure pass, because nothing below the caret is being laid out and
there is nothing to compare signatures against — from §27.4's checkpoint to
the caret and no further (`[np_bstop]`), then `np_rflush`'s delta.

Six rules hold it up, and each is a rule rather than a tuning:

- **It is a lie, so it settles.** `np_reconcile` runs half a second after the
  last keystroke, when the window stops being frontmost, and before anything
  that is not typing. A user's rhythm is type-then-read, and a note that
  stayed broken while it was being read would be read as the note.
- **The settle is a worker task** (§20.6), spawned lazily on the first break
  so a Note Pad that never breaks costs no task slot. It polls every 3 ticks,
  compares `OSAPI_GET_TICKS` against `[np_ktick]` and asks `OSAPI_WM_TOP`
  whether it still has the front — a package learns that it *gained* the
  front and never that it lost it. A **covered** window is skipped rather
  than drawn: the reconcile is a fill followed by runs, and under an armed
  clip region a fill cuts per pixel while a run cuts per cell (§11.3).
  Skipping costs nothing, because an uncover repaints through `W_PAINT` and
  `np_paint` draws the note and clears the flag.
- **Everything that is not typing settles first**, and the hook is at
  `np_measure` rather than in each handler: that call *means* "I need to know
  where things really are", and a click has to land on the character under
  the pointer. Enter, the menu commands, save and load reach `np_redraw`
  without `[np_fast]` and settle there; a resize or a toast fails
  `np_sigsame` and settles too.
- **The trigger is CELLS, not rows** — `(np_dr1 − caret row) × np_rcols ≥ 60`.
  This window is resizable and a row is 30 cells or 90 depending how wide it
  was dragged, so a row count is two different amounts of work wearing one
  number. The caret's own row is excluded because §27.2 already draws it for
  two cells.
- **The edit column is REPORTED, not derived**, and that is what lets every
  edit enter the break rather than only an insert. The scrolled copy below
  duplicates the row's prefix, which is **C** cells — the caret's column
  *before* the edit — for an insert and for a backspace alike. But the caret
  ends at `C+1` in one case and `C−1` in the other, so deriving C from where
  it *ended* runs the opposite way for each, and the first version of this
  did exactly that and left two stale characters behind a backspace. The key
  handler knows C outright (`[np_cur] − [np_ckpi]`, because a row start is a
  character index and every character on a row occupies one cell), so it says
  so in `[np_ecol]` and says in `[np_eext]` how many further cells its edit
  took off that row — one, for forward Delete, which removes a character that
  was on it. `np_brktry` blanks `np_ecol + np_eext` and needs to know nothing
  about directions.
- **Entering the break and continuing in it are different permissions**,
  because while it is up **the tail is not redrawn**. `[np_fast]` carries the
  kind: 1 insert, 2 backspace, 3 forward Delete, 4 a caret move. Kinds 1..4
  may resume the walk (§27.4); kinds 1..3 may *enter* the break, since a
  caret move reflowed nothing worth avoiding; only kinds 1..2 may *continue*
  in it. Right would push a character from the tail into the caret's row and
  draw it twice, Left would pull one the other way and lose it, and Delete
  eats exactly the tail's first character — all three settle instead.
- **The caret bar is erased before the scroll.** It is a 1px column at the
  caret's old x, and the scroll would carry it down into the middle of the
  tail where nothing would ever erase it. The caret's row is redrawn whole a
  moment later, so erasing it where it stands costs nothing.
- **It is gated on `CPU_8086`** via `OSAPI_CPU_INFO`: anywhere faster the
  reflow is already invisible and the lie buys nothing.
- **It needs `[np_tx]` on a multiple of 8**, because `OSAPI_GFX_SCROLL` is
  byte-column granular on every adapter. `OSAPI_WM_SNAP` (§11.94) guarantees
  that on the two mono adapters — the ones a 4.77MHz machine has — and on VGA
  it is a coin flip, so there the break does not engage. That is a FACT the
  code tests, not a guess (§47 rule 3).

**The reconcile is incremental**, and it can be: the break only ever scrolled
rows `[np_borig]` and below, so everything above it is still the note and
still carries the signature that says so. `np_reconcile` runs a measure walk
to bring the signatures up to date, then forces the band to
`np_borig..np_vrows−1` — no signature describes a fiction, so the comparison
is not consulted there — fills that band white, and draws it with `np_clean`
set. A refused scroll (the band left below the caret is shorter than the row
being inserted, which is what happens at the bottom of the window) sets
`np_bfail` and settles on the spot.

### 27.4 The layout checkpoint — a keystroke stops walking the whole note

`np_walk` is O(the note) and §27.2 runs it **twice** per keystroke. That is
about 500 8086 cycles a character a pass, so on a 4.77MHz machine a 400-
character note costs ~90ms per keystroke in layout alone — against ~2ms of
drawing. It is invisible until the note is long, and then it is the whole
cost: a user filled a fullscreen window and reported each keystroke getting
slower while a counter in `font_run_cell` said the drawing was still two
cells.

Wrapping is a **left-to-right automaton with no lookahead** — a cell that
would cross `[np_rgt]` moves to the next row, a newline resets the pen — so
the pen state at index k depends only on the characters before it. An edit at
the caret therefore cannot change the layout of anything ahead of the caret,
and the walk may **resume at the start of the caret's row**.

The start of a row is `(index, row)` alone: the pen's x is always `[np_tx]`
there, and its y is always `[np_ty] + 8·row`, because every row advance moves
`np_row` and `bp` together. `np_rstart` banks the candidate
(`np_ckpc`/`np_ckpcr`) and `np_ask` promotes it to `np_ckpi`/`np_ckpr` the
moment the walk stands on the caret, so every walk leaves the checkpoint
describing the layout it just performed.

Three things make it safe:

- **`[np_fast]` is set by the key handler, not inferred**, and only for a
  printable insert (1) or a backspace (2) whose edit index is at or after the
  checkpoint. A backspace at column 0 eats the last character of the row
  *above*, which is before the checkpoint — that is the one deletion a
  resumed walk could not see, and it is exactly the "inside the caret's own
  row" rule.
- **It is one-shot.** `np_redraw` reads `[np_fast]` and clears it, so a
  handler that forgets to set it gets the full walk, which is only slow.
- **`np_sigsame` gates it.** The checkpoint is a row index, so it means
  nothing under a different geometry; `np_redraw` only honours `[np_fast]`
  after the four `np_bounds` numbers have compared equal.

Rows above the checkpoint keep their stored signatures, which is correct
because they did not change; `np_dr0` can therefore never come back below
`np_ckpr`, and both passes resume from the same point. Measured under QEMU
with a counter in `np_walk`'s loop: at 200 characters a keystroke went from
**404 walk iterations to 35**, at 350 from **702 to 28**, and after growing
the window to the full screen to **4** — bounded by the caret's column
instead of by the note.

### 27.5 Where each row starts — a query about a row costs a row

§27.4 bounded the *keystroke*. It did nothing for the caret keys, and they
were the worse case: Up ran **four** full walks — `np_vmove` measured to find
where the caret is, `np_move` measured again to find the index at the column
above it, and `np_redraw` then measured and drew. At ~500 8086 cycles a
character a walk that is ~170 ms per arrow key on a 400-character note.

Every one of those walks exists to answer a question about **one row**: where
is the caret's row, what index sits at column C of row R, what did this click
land on. The only reason a walk had to start at index 0 was that nothing
recorded where a row begins. `np_rows` is that record — `NP_MAXROWS` words,
the buffer index each visible row starts at, written by `np_rstart` beside the
checkpoint candidate it already banks. With it, and with `[np_lastrow]`
telling `np_walk` the last row this pass cares about, every query is a walk of
at most `np_rcols` cells:

| event | before | after |
|---|---|---|
| type a character | 60 | 60 |
| Left / Right | 804 | 60 |
| Home / End | 1,608 | 90 |
| Up / Down | 1,608 | 184 |

(walk iterations on a 400-character note, counted under QEMU)

Five things hold it up:

- **`[np_lastrow]` is one-shot** — `np_walk` resets it to 0xFFFF on the way
  out. A caller that forgets to set it walks the whole note, which is slow and
  never wrong; the other polarity would silently truncate somebody else's
  pass.
- **The `np_lastrow` stop leaves `np_rows` alone; the `[np_bstop]` stop
  invalidates it.** Stopping early because the caller knows nothing below
  moved is not the same as stopping early because the note is not being laid
  out at all — the second leaves the table describing a buffer that has since
  shifted, so it clears `[np_rowsok]` and the reconcile's full pass rebuilds
  it.
- **The fallback is what makes a stale table safe.** `np_seedrow` refuses a
  row at or past `[np_rowsn]` — a click on the blank space below the text —
  and the caller runs the full walk instead. Without it a stale entry answers
  with a plausible wrong index rather than an obvious wrong one.
- **`np_bounds` invalidates on a geometry change.** The checkpoint and
  `np_rows` are row *indices*, so they mean nothing under a different layout.
  `np_sigsame` guards `np_redraw`, but nothing guarded the caret keys, so the
  test lives in `np_bounds` where every path already goes.
- **A caret move may resume too, and it must seed at the EARLIER of the two
  rows.** Up lands on the row above the checkpoint, and seeding at the
  checkpoint would walk straight past the caret without ever finding it —
  `[np_curx]`/`[np_cury]` stay 0 and the bar is drawn at the content's origin.
  `np_move` moves the checkpoint back to the target row before arming
  `[np_fast]` = 3.
- **The settle happens before the seed, not inside `np_measure`.** A reconcile
  runs walks of its own, and they would spend the seed the caller had just
  set. `np_vmove`, `np_hmove` and `np_onclick` call `np_settle` first for that
  reason; `np_measure` keeps its own call as the backstop, where it is now
  always a no-op.

Verified by differential: an identical scripted session of typing, arrows,
Home/End, backspace, Delete, Enter and two clicks produces a **pixel-identical
window** on the seeded build and the full-walk build — except for one cell,
where the full-walk build left a **stale caret** overdrawing a character and
the seeded one did not. The incremental screen is also pixel-identical to a
from-scratch `W_PAINT` repaint of the same state.

### 27.6 The note is a heap claim, and it grows

The **text is not in this package's bss**. `np_entry` claims `NP_KB0` (1KB)
before it creates the window and puts the document at `[np_dseg]:0000`;
`[np_cap]` is its capacity in bytes. A region is image + bss capped at
`APP_MAX_SIZE` (§20.1) and is fixed at load time, so anything whose size only
the *user* knows belongs in the heap (§50.3) — which is the whole point of
`OSAPI_MEM_CLAIM` existing, and what a 512-byte `np_buf` was working around.

Four movements, and every one of them goes through `np_resize`, which is
`OSAPI_MEM_REGROW` and **never** claim-copy-free (§50.3.1: a regrow extends
in place when the paragraphs above it are free, so it needs the *difference*
rather than old + new at once, and when it must move it brings the bytes):

- **`np_room`, the growth point and the only one.** A keystroke that would
  fill the claim asks for another `NP_GROWKB` first. A refusal — the heap's
  or the ceiling's — drops the keystroke, which is exactly what a full note
  did when it could not grow at all.
- **A load sizes the claim to the file.** `np_load` opens it to `NP_MAXKB`
  *before* the read, because nothing knows the file's size until the read
  reports it and there is no stat in the file API; the file then lands in the
  document buffer itself and the CR/LF fold runs **in place**, which is safe
  because folding only ever drops bytes so the write index can never outrun
  the read index. `np_fitclaim` gives the rest back afterwards — on the
  failing path too, so a refused load does not leave 8KB held for a note that
  did not change. There is no load staging buffer at all.
- **`File > New` shrinks back to `NP_KB0`.** A shrink always succeeds in
  place, so this cannot fail.
- **A save takes a second, transient claim**, sized from `[np_len]` and held
  only across the write, because expanding CR to CR LF *grows* and the
  document claim is sized for the document. A refusal is an ordinary path:
  the note is still there and still editable.

**`NP_MAXKB` is 16, and it is an arithmetic limit rather than a memory one.**
It used to be 8 and 8 was never about memory either: it was what the window
could *show*, because Note Pad did not scroll and text past the last visible
row could be typed and never read back. §27.7 removed that, so what bounds
the note now is the **save**. Staging expands every newline to CR LF, so the
worst case is twice the note — walked with a 16-bit DI, counted with a 16-bit
BX, and sized by `np_stghold`'s own `2 × [np_len]`. At 16KB that worst case
is 32,768 and all three hold; at 32KB it is 65,536 and all three wrap to
zero. 16,384 characters is 32× what the old `np_buf` held.

One behaviour changed with the ceiling. A file larger than the claim used to
fill the buffer and say "Truncated"; it is now `FERR_BIG` and "Too big", and
the note is left alone. That is the honest answer and the old one was a trap:
a half-loaded note whose next save wrote the truncation back over the whole
file.

### 27.7 The view scrolls, and one word is what makes it

`[np_top]` is the note row drawn at the top of the content, and **`np_walk`'s
`np_row` starts at minus it**. That single change is the whole mechanism: a
row above the view has a *negative* index, and every array in this module is
already indexed by an unsigned test against a limit (`np_sig`, `np_rows`, the
dirty band), so a negative word read as unsigned is past all of them and
skips itself. Nothing downstream had to learn what scrolling is.

**One place could not see it, and it is the exception that names the rule.**
`np_rflush` decides whether to draw a row from the row's *pixel* y, and a row
a little above the view has an ordinary small y rather than an implausible
one — so it grew a test of its own (`[np_rby] < [np_ty]` → drop). The bottom
edge was already tested and needed nothing.

`np_scrollto` is the only writer, it clamps to `0 .. np_scrollmax`, and it
answers CF = 0 when the view actually moved. Moving it **drops four pieces of
state rather than adjusting them** — the row signatures, the layout
checkpoint (§27.4), `np_rows` (§27.5) and the seed already loaded into
`[np_resume]` — because every one of them is counted in *visible* rows, and
adjusting them all by the same delta would be a second place that has to
agree about what a row is. It also ends the visual break (§27.3): its fiction
is about particular rows on screen.

Two things follow from "a row index means something different afterwards":

- **A scroll is a full repaint, never a band.** `np_scrollto` clears
  `[np_sigok]`, so the next `np_sigsame` refuses and `np_redraw` takes its
  `.full` path. The alternative — `OSAPI_GFX_SCROLL` on the content and a
  band for the newly exposed row — is a real optimisation and is *not* done,
  because the delta cache, the signatures and `np_rows` would all have to be
  rotated to match and any one of them missed is a wrong character on screen.
- **`[np_drows]` is recorded only by a walk that ran to its natural end.** It
  is the note's height — what the thumb is a fraction of — and the two walks
  that stop early cannot know it: the `[np_lastrow]` stop (a caller that
  knows nothing below moved) and the visual break's stop at the caret. The
  draw pass's `.blank` loop also walks `np_row` on *past* the note's last
  row to erase what a shrinking note left behind, so the height is banked
  before it rather than after.

**The caret gets a place on screen, and `[np_follow]` is what asks for it.**
Its own flag, not `[np_ekind]`: that one says which cheap redraw path this
keystroke has *earned*, and Enter, Up, Down, Home and End are all 0 there
while all five move the caret. `np_onkey` sets it at the one label every
handled key reaches; `np_redraw` spends it and clears it, so a scroll bar
click — which arrives at the same `np_redraw` — is not dragged straight back
to the caret. A click in the *text* needs no follow: it landed on a row that
was already visible.

**A resize re-clamps, and it needs a walk to do it.** The wrap width changes,
so the note is a different number of rows, so `[np_top]` may be looking past
the end of it — and only a walk knows by how much. `np_bounds` raises
`[np_gchg]` when the geometry differs from what the last paint recorded, and
`np_paint` spends it: measure, `np_scrollto` the current top, then draw.
`np_sigmark` clears it. The cost is one extra measuring walk **per geometry
change**, not per paint. `np_clamp` does the same job for the other way the
view can be left pointing at nothing — a load or a `File > New` replaces the
buffer — by putting the view back at row 0, which is also where a reader
starts.

**The bar is the Disk window's (§22)**, deliberately: same `NP_SB_W` width,
same `NP_SB_ARR` arrow cells, same proportional thumb with an 8px floor, and
a track click **pages** rather than dragging. `np_thumb` is the one piece of
geometry, shared by the painter and the hit test so the two cannot disagree
about where the thumb is — the same argument as `fm_hit` and `fm_thumb`.

Three details that are not free choices:

- **It is reserved always**, whether or not the note needs one. Whether one
  is needed depends on the row count, which depends on the wrap width, which
  would then depend on the bar — so a bar that came and went would change the
  layout that decides whether it should be there.
- **It stops short of the grow box**: `[np_sbb]` is `[np_bot] - NP_GROW`,
  because the kernel draws a 13×13 handle in the content's bottom-right
  corner and a down arrow drawn into it comes out as a filled square. That is
  how this was found.
- **`np_sbcheck`, not `np_sbar`, is what a keystroke calls.** The bar is
  redrawn only when `[np_top]` or `[np_drows]` differs from what is on
  screen, so ordinary typing inside one screenful costs the comparison and
  nothing else.

### 27.7.1 A walk stops at the bottom of the view

Scrolling made a question worth asking that had never been asked: *why does a
keystroke lay out the part of the note nobody can see?* Measured on a
2,000-character note with the caret near the top, one keystroke cost **six**
`np_walk` calls and 10,079 loop iterations, **72% of them on rows below the
window**. Every walk now carries `[np_lastrow] = [np_vrows]` and the same
keystroke costs **two** walks and 1,015 iterations — 29 of them below the
view, which is the one row past the bottom that the bound deliberately keeps.
Typing at the *end* of a note was already 2 walks and ~50 iterations, flat in
the note's length, and is unchanged: §27.4's checkpoint had that case.

Five things make the bound safe, and each of them is a thing that broke first:

- **The comparison is SIGNED.** `np_row` is a visible row and is negative
  above the view, which unsigned reads as past every limit — so a bounded
  walk stopped before drawing anything at all. `[np_lastrow]`'s "no limit"
  sentinel is `0x7FFF` for the same reason: `0xFFFF` is row minus one.
- **The bound is `[np_vrows]`, one row PAST the last visible row**, because a
  character typed at the end of the bottom row wraps the caret onto the row
  below and `np_seecaret` has to be able to see where it went.
- **`[np_curseen]`** says whether this walk stood on the caret, because 0 is a
  real pen y and `[np_cury]` cannot say "not found" by itself. When a walk
  that owes a caret-follow did not find it, `np_redraw` walks again from index
  0, **unbounded and unseeded** — a net carrying either restriction misses the
  caret exactly as the first walk did. The case is ordinary: page the view
  away with the scroll bar and then press a key.
- **`np_paint` is bounded too.** A full repaint is what every scroll step
  costs, and walking 16KB of note to draw one screenful of it is the whole
  problem in its most visible form.
- **`.pad` no longer falls into `.stop`.** It walks `np_row` past the note's
  last row without `np_rstart`, so the `np_rows` entries `.stop` would claim
  were never written.

**`[np_drows]` is the one thing a bounded walk cannot know**, and it is what
the thumb is a fraction of. Three sources, in order of exactness:

- A walk that reached the note's **end** sets it exactly and clears
  `[np_hdirty]`.
- A walk that **stopped** raises it to the lower bound it proved — the row it
  stopped on exists, so the note is at least that tall — and **never lowers
  it**. That is what keeps `np_scrollmax` from clamping the view short of a
  caret that has just moved past the old bottom, which is the difference
  between a stale thumb and a caret nobody can see.
- **`np_height`** walks the whole note when `[np_hdirty]` says an edit
  changed it. The **worker** runs it half a second after the typing stops —
  the same idle test that settles the visual break — and `np_onclick` runs it
  synchronously before `np_sbclick`, because a click on the bar is the one
  place the height has to be exact rather than generous. `np_hire` is
  therefore no longer only the break's: a note taller than its window wants a
  worker whether or not this machine draws breaks.

Between an edit and that settle the thumb can be a row or two small on a note
that **shrank**. It is the deliberate trade: the bound is monotone upward, so
the error is always in the direction that keeps the caret reachable, and the
next full repaint — which every scroll is — makes it exact.

**A scroll that follows the caret does not re-measure and re-follow.**
`np_redraw`'s band path jumps past `.full`'s clamp-and-follow block to
`.fullpaint`, because a caret that has just been followed is in view by
construction: `np_seecaret`'s target is the row itself, not a step towards it.
Two of the six walks were that block being asked a question it had already
answered.

**And a walk with no checkpoint seeds at the top of the VIEW.** `np_rows[0]`
is the index row 0 of the content starts at, so rows above the view — which
have neither pixels nor signatures — need not be walked to find it. It is
§27.4's claim applied from a higher row and therefore a weaker one, and the
two die together: `np_scrollto`, `np_bounds` and `np_clamp` clear `[np_ckok]`
and `[np_rowsok]` side by side. For it to be available at all, a **bounded**
walk has to publish `np_rowsn`/`[np_rowsok]` for the rows it did pass, which
`.stop` now does whenever the walk started at the top of the view rather than
at a seed part-way down.

### 27.7.2 A scroll moves the pixels it already has

A scroll used to be a full repaint: white-fill the content and letter every
visible row. But moving the view by `d` rows changes only `d` rows of what is
on screen — the rest is the same text at a different y, which is exactly what
`OSAPI_GFX_SCROLL` moves. An arrow click letters **4 rows instead of 20**, and
a downward one walks 174 layout iterations instead of 1,914.

**`NP_SB_STEP` is 4.** The Disk window's arrow cell steps one row, but its rows
are 16px list entries and these are 8px lines of prose; four is about the same
travel, and a blit-scrolled band costs the same whether it moves one row or
four.

What makes it safe is that the blit shifts the **pixels** and `np_shiftrows`
shifts their **description** — `np_sig` and `np_rows` — by the same `d`, as one
operation. §27.7 says those arrays are dropped rather than adjusted on a
scroll, and that was right while the pixels were being redrawn wholesale:
adjusting would have been a second place that has to agree about what a row is.
Here there is no second place. **`[np_ptop]` — the `[np_top]` the screen was
drawn for — is the one fact that says whether the two have parted**, and
`np_redraw` reconciles it before anything reads an array indexed by a visible
row. That matters because a scroll-bar click scrolls and *then* redraws.

Six things the band arithmetic has to get right:

- **The x span rounds OUTWARD to byte columns**, which is what lets this work
  on every adapter rather than only where `OSAPI_WM_SNAP` aligns the content
  (§11.94). Rounding x1 down stays inside the content because `NP_MARGIN` is 8;
  rounding x2+1 up reaches at most seven columns into the scroll bar, so that
  strip is blanked and its two owners — `np_sbar` and the grow box — put
  themselves back.
- **The y span stops at the bottom of the last WHOLE row, not at `[np_bot]`.**
  A content height that is not a multiple of 8 leaves a sliver below it;
  `np_rflush` refuses to draw a row that would cross the content edge, so
  nothing would ever erase what the blit pushed into that sliver. It showed as
  a one-pixel band of the row above's descenders, left behind permanently —
  and only on a window whose height has a remainder, which is why VGA was
  clean and Hercules was not.
- **`OSAPI_GFX_SCROLL` does not blank what it vacates** (§27.3 relies on the
  same fact), so the exposed rows are filled before they are lettered.
- **The exposed rows are drawn by ROW, not by signature.** `np_clip` with
  `[np_dr0]`/`[np_dr1]` set to the band is deterministic; an exposed row's old
  signature belongs to the row that scrolled away and could match the new one
  by luck.
- **The caller's dirty rows join the band, shifted into the new frame.** An Up
  that scrolls has two: the row the caret arrived on, and the row it left,
  which the blit carried faithfully and which therefore still shows a caret.
- **A page is refused.** `|d| >= [np_vrows]` retains nothing, so the blit would
  be pure cost; `np_scrollpaint` answers CF=1 and the full repaint happens. A
  toast is refused for the same kind of reason — it is drawn over the text and
  is in no row's signature, so the blit would carry it off its own frame.

Verified by differential: the same scripted run against a build whose
`np_scrollpaint` always refuses is **pixel-identical inside the window**, on
VGA over nine states and on Hercules over six.

## 28. taskmgr.inc — the Task Manager window

Built-in singleton app kind (KIND_TASKMGR, cap 1 — one sampler), window
"Task Manager", 200×264 at (250,100) — 176 until the memory view grew a
CLAIM column (below). Label prefix `tm_`. No onkey, no
boot-time window or task: `tm_init` (from kmain, after
loader_init) only reads total conventional RAM once via int 12h (kmain
runs on task 0, so §7's only-the-UI-task-calls-BIOS rule holds). The
window + monitor task exist only while an instance is open: `app_launch`
runs `tm_kinit` (zeroes all module state including the history ring —
the gauge calibration restarts from scratch at every launch — and caches
the window ptr in `tm_win`), then spawns `tm_task` with DX = the instance
index. `tm_task` checks I_STATE = 2 once per interval (after the spin
phase) and tears down via `inst_task_die` (§29).

**Two views.** `[tm_view]` (0 = performance, 1 = memory) selects what the
content shows; both share the sampler, the instance snapshot and the
process-row geometry. The window's W_ONCLICK is `tm_click`: ANY content
click toggles the view — the content has no other click targets, ui.inc
§13 only feeds clicks to the front window (a click that raises the window
does not toggle), and the handler runs under the gfx lock its caller
already holds, so it white-fills the whole content rect (198×245 from the
wm_content origin) and runs the new view's full paint body in place.
`tm_kinit` zeroes `[tm_view]` with the rest of the module state: every
launch opens in the performance view. The sampler never looks at the view
— history keeps accumulating while the memory view is up, so toggling
back shows a gapless graph.

**Load gauge — idle-spin calibration.** tm_task never sleeps. Each
interval it spins { 32-bit counter += 1; `task_yield` } until 9 ticks have
elapsed (wrap-safe compare), measuring how quickly a yield loop gets the
CPU back; this self-calibrates away the UI task's constant busy-poll cost.
Calibration is a phased-window maximum, not an all-time max or a decay: two
32-bit slots, `tm_cmax` (current epoch) and `tm_pmax` (previous epoch).
Every sample: tm_cmax = max(tm_cmax, count); every 20 samples (~10s) the
epochs rotate (pmax ← cmax, cmax ← count). The effective max is
max(tm_cmax, tm_pmax) — always ≥ the current count, so no clamp is needed
before the divide. An anomalously cheap phase (menu_track's poll loop
yields far faster than the normal idle loop) can poison the calibration
only until its epoch ages out (≤ ~20s); the tradeoff is that a *sustained*
saturating load also re-baselines after ~10–20s and fades toward 0 — bursts
(drags, repaints, package loads) always read correctly.
load% = 100 − 100·count/max_eff, both first normalized by shifting right
until max_eff's high word is zero; if max_eff's low word is then 0, load
reads 0. The spin phase doubles as the system idle soak; its cycle share
appears honestly in the list as TaskMgr.

**The list is an INSTANCE list, not a task list.** A task list shows only
the kinds that own a task; every task-less app — About, Disk,
and every loaded package that has not claimed a §20.6 worker — runs
purely inside window callbacks on the UI task and would never appear. Even
a worker-owning package would appear under the wrong name: the row must be
the *instance*, so its callback cycles and its worker's cycles land on one
line.
Rows are therefore `TM_ROWS = INST_MAX + 1`: row 0 is **System** (the
kernel) and rows 1.. are the instances, **grouped** by `tm_order` — the
built-ins first, indented one space under System, then the packages, then
the free slots. Grouping is what makes the list readable: the built-ins the
user cannot close sit together, everything they launched sits below, and the
free slots fall to the bottom instead of punching holes through the middle.
The cost is that a row can move when an instance dies; the dock is what
keeps the stable slot↔tile mapping (§30), and this list only ever inherited
it. `tm_pct` is still indexed by INSTANCE (row = instance + 1 as it was
built), which is why the row loop reads `[tm_rowi]` and not its own counter.
Each
instance's cycles are the sum of two disjoint counters — its `I_CYC`
(callback time billed by §11's dispatch sites) and, if I_TASK ≠ 0xFF,
that task's `sch_cycles`. They are disjoint by construction: `task_debit`
moves cycles off the task rather than copying them (§8.1), so System —
plain `sch_cycles[0]` — is exactly what the kernel did on its own
account, and the rows partition one total.

**Sampling** (once per interval, after the spin phase):

- Under ONE `pushf`/`cli` … `popf`: snapshot `sch_cycles`, the MAX_TASKS
  T_STATE bytes, `sch_cur`, and then the whole instance table — per slot
  its I_STATE, I_TASK, I_SPTR, I_SIZE, I_CYC and 16 I_NAME bytes (§29 records
  free atomically with T_STATE inside task_exit's cli window, so nothing
  read in one block can be torn against itself).
- Per-task diffs and per-instance diffs, both by the same two rules:
  diff = new − old (32-bit, old ← new); **negative-diff clamp**: a diff
  with bit 31 set (the counter regressed — a slot reused mid-interval, a
  rare PIT stamp race, or a task_debit reaching back across the sample
  point) is forced to 0, since a wrapped "huge" diff would shrink the
  total and clamp several rows to 100%; **appeared-slot rule**: a slot
  whose previous-sample state (`tm_pstate` / `tm_pist`, copied at the end
  of every sample) was free but is now used gets its diff forced to 0 —
  task_spawn and inst_alloc reset those counters (§8.1), so the interval
  diff is meaningless.
- Fold into one figure per ROW: row 0 = task 0's diff; row 1+i =
  instance i's diff, plus task `I_TASK`'s diff when the record is in use
  and I_TASK < MAX_TASKS (a free record's I_TASK byte is stale). That
  second term is reachable for a `KIND_PKG` row since §20.6, which is the
  intended behaviour: a worker-owning package's share is its callbacks
  plus its worker, on one line. Cycles
  of a task whose instance died this interval belong to no row and simply
  drop out. total = Σ row_i; normalize by shifting total and every row
  right while total's high word is non-zero; **total = 0 → every share is
  0 (no DIV ever executes with a zero divisor)**; else
  share_i = row_i·100/total (≤ 100 since row_i ≤ total).
- RAM — **the honest total: everything claimed, whether or not it is
  live.** Three terms, and each is a reservation nobody else can be given:
  `TM_KERN_KB` (the whole kernel — image, scratch, FAT snapshot, disk
  buffers and every task stack are one contiguous span since §2, and there
  is no growth room in it to bill to anybody), `TM_POOL_KB` (the whole
  heap's package regions, §20.1
  — reserved from boot, so a machine with no package open still shows it
  spoken for), and every live **heap claim** (§50, `mem_claimed_kb`). Both
  the kernel's own claims (the menu save-under, the back buffer) and every
  package's are in that last term. **All three are KB from the start**: the
  back buffer alone is 150KB and would not fit a 16-bit byte count. Total KB
  is the boot-time int 12h value. **All bar math is in KB**: barw =
  usedK·160/totalK (`mul` then `div`; totalK cannot be 0 from int 12h, but a
  0 check that skips the bar is required anyway).
  The System row's MEM cell carries `TM_KLOW_KB + TM_KSEG_KB` plus the
  kernel's *own* claims (`mem_kernel_kb`), and a package row's carries its
  region **plus its own claims** (§50.5) — so the rows still partition the
  bar and an app that claimed a quarter-megabyte of canvas says so.
- History: load% scaled to 0..40 (·40 then /100), stored at
  `tm_hist[tm_pos]`. The ring index IS the graph column — an oscilloscope
  sweep, no scrolling — then tm_pos advances mod `TM_GW`.

**Drawing.** `tm_paint` (W_PAINT) dispatches on `[tm_view]` and runs the
active view's full body — bare and unconditional, no lock, no visibility
check (wm_paint_all calls it with the lock already held, §11). tm_task's
periodic path wraps its drawing Clock-style (§14): gfx_lock, arm
`wm_clip_set` under the lock (else skip), and touches only what changed.
Performance view: the CPU + scheduler text line (checked per chunk like a
row, so a mode change shows up within one sample period without any extra
plumbing), the new sweep column plus an all-white
gap column at the advanced tm_pos, the RAM line and bar, and the process
rows — the full graph render happens only in tm_paint, so the periodic lock
hold stays small (Bounce-scale). Memory view: the RAM line and its HEAP
figures, the XMS line and bar, the map interior and the rows; the two map
frames and the column header are painted only by the full bodies (tm_paint /
tm_click). All drawing is self-backgrounding (each element white-fills its
own rect or paints both segments), so tm_paint needs no preceding content
clear beyond the one wm_paint_all already does.

**Nothing is redrawn unless what it is drawn from changed.** Every element
on this page composes or derives its content first, reduces it to one word,
and compares that against what it last drew - the rows against `tm_rowck`,
the five things around them against `tm_elck` (`TMC_*`). Equal means the
pixels on screen are already right, so the erase and the draw are both
skipped. **The erase is therefore inside the changed branch**, which is the
ordering the whole thing rests on: something not redrawn must not be blanked
either.

| element | its key |
|---|---|
| `TMC_LINE` — the RAM (+ HEAP) line | the composed string |
| `TMC_MRAM` — the conventional map | `mem_tab`, hashed (§50) |
| `TMC_BAR` — the performance RAM bar | `[tm_barw]` |
| `TMC_XMS` — the XMS line (§41) | the composed string |
| `TMC_XBAR` — the XMS bar | `[tm_xbarw]` |

The map is the one that pays: its interior is ~3,000 pixels of pattern fill,
and it changes only when a claim is made or a package loads or closes, which
on a desktop that is sitting still is never. Its key is the table it is drawn
from rather than the pixels it would produce, so the comparison costs a
byte-wise hash of 96 bytes. Reading the claim table unlocked can tear against
a claim made on another task; a torn read differs from the stored key, which
is the safe direction — one extra redraw, never a missed one.

**A row is redrawn a CHUNK at a time, and only the chunks whose text
changed.** Both views compose the whole row into `tm_str` first — name, ADDR,
SIZE/HEAP or ST/CPU/MEM, all of it — zero-pad it to `TM_NCHUNK`×`TM_CHUNK`
characters, and then hash each `TM_CHUNK`-character chunk on its own
(`tm_chunksum`, rotate-then-add, so a transposition is not invisible) against
its word in `tm_rowck`. Equal means the pixels on screen are already right:
**no fill, no legend square, no glyphs** for that chunk. So the erase is
*inside* the changed branch, which is the ordering the whole thing rests on —
what is not redrawn must not be blanked either.

The chunk, rather than the row, is the unit for a measured reason: a row
carried **one** key until it was found to be redrawing the name, the state
and the memory figure every time a CPU percentage ticked over — 20 glyphs to
change three, twice a second, on the machines least able to afford it.

**The two questions do NOT share a width, and assuming they did cost five
characters.** The chunk is right for *what changed*; for *what may be drawn*
the answer is still the 8x8 cell, because that is what `font_char` can draw
or not draw. A whole-chunk clip test throws away all five cells to protect
the one an edge crosses, and a chunk holding a single inked character loses
that character outright — a package row is `' PAINT …'`, so the split falls
`" PAIN"` | `"T …"` and a window edge in the second chunk erased the T and
nothing else. It reads as letters going missing in arbitrary positions,
sometimes several at once, because which ones go depends only on where the
edge falls against the five-character grid; and it shows as *blank* rather
than stale whenever the row is new, since nothing was ever drawn there.

So a chunk the region cuts — and only that chunk — is redrawn a **cell** at a
time (`tm_chunk_cells`): each 8x8 cell is tested on its own, and a cell
wholly inside one fragment is erased and lettered while a crossed one is left
alone entirely, both halves of the granularity rule together. That is four
extra clip tests on at most one chunk per row per vertical edge, and nothing
at all on a row nothing covers. The cut chunk's key is forgotten afterwards
whatever happened, because a chunk that took this path was by definition not
drawn whole.

**The band is wider than its chunks at BOTH ends, because the pen is inset
from it**, and each end has to be erased by something. The last chunk's fill
runs on to the band's right edge. The first chunk's is preceded by
`tm_row_lead`, which erases the band's left inset — and that one is
load-bearing rather than tidy, because the memory list letters from
`rowx+16` and its **legend square sits at `rowx+6`**. `tm_rowfill` used to
erase the whole band, so a row's square went with its text and came back with
it; chunks start at the pen, so nothing erased it any more and a row that
went away left its square behind on a line that no longer existed. Closing a
package was the way to see it. Erasing the lead-in therefore counts as a
draw, because the caller has to know to put the square back.

The string is zero-padded before any of this, so every chunk hashes
deterministically and a row that got *shorter* changes the chunk it lost its
characters from, which is what erases them.

**The CPU + scheduler caption is drawn through the same routine**, as the
virtual row `TMR_CPU` — one past the last row either list can show. Its first
two chunks carry a percentage that moves nearly every interval and its last
two a scheduler mode that essentially never does, so checking it whole would
still have cost all twenty glyphs; chunked, a load that moves costs the five
or ten characters that moved.

It is a hash and not a proof: a collision leaves one chunk stale until its
content moves again. For a status display at this cadence that is the right
trade against `TM_NCK`×`TM_NCHUNK` words of `.bss` versus the 456 bytes a
full text cache would need. Three rules keep it honest:

- **`tm_rowck_clear` runs in `tm_draw_full`, and that is the whole of the
  invalidation rule** for both arrays. It names `tm_elck` and `tm_rowck`
  **separately**, with an address and a count each: it used to zero them as
  one span on the strength of their being declared adjacent, and they were
  not — five unrelated words had drifted in between, so the run stopped five
  words short and the last row of `tm_rowck` was never invalidated. Latent
  while that was the memory list's bottom row, which is empty on any machine
  that fits its instances; immediately visible once `TMR_CPU` became the last
  row, because a view switch then white-filled the content and left four
  fifths of `CPU nnn% SCH preempt` recorded as drawn. The only things that can
  change this
  window's pixels without going through a checked draw are a `wm_draw_win`
  content fill, a view switch and a resize — and all three arrive at a full
  body. A window MOVE arrives there too, via `wm_paint_dmg` (§11.91), which
  is what stops a cached "unchanged" from leaving a map behind at the
  window's old position.
- **A row that fell off the bottom (`tm_view_begin`) forgets its entry**
  rather than recording one, so it comes back if the window ever grows.
- `tm_click`'s content clear is therefore load-bearing, and it takes its far
  corner **off the window record** rather than from a pair of constants: a
  short fill leaves the outgoing view's tail rows lettered under the incoming
  one, and the incoming view will not erase them because it thinks they are
  already blank. It stops **two** pixels short of `x+w` and `y+h`, not one:
  those are the frame's own right and bottom borders, and a fill that reached
  them left the window open-sided until something repainted the frame.
- **No live key may be zero**, because zero is what `tm_rowck_clear` writes.
  `tm_elchk` — which every checked draw in this window goes through, the
  `TMC_*` elements and each chunk of each row of both lists — maps an
  incoming 0 to 1 for
  exactly this reason. An element whose state legitimately hashes to zero
  otherwise reads as *unchanged* on the very first paint and is never drawn at
  all: `mem_tab` is all zeroes on a machine with no heap to claim from, so on
  128KB the conventional-memory map stayed blank forever, and the XMS bar does
  the same on any machine with no extended memory in use.

**Content layout — performance view** (content-relative; content is
231×282):

- (6,4): the CPU + scheduler line (white-fill (6,4)-(`TM_RW`,11) first):
  `[0..7]` `"CPU nnn%"` (n right-aligned, space-padded, 0..100), `[8]`
  space, `[9..19]` the **read-only** scheduler-mode field, left-justified
  and space-padded to 11 chars — `"SCH preempt"` or `"SCH coop   "` — from
  `sched_mode_get` (§8.2). The Task Manager only *displays* the mode; it is
  changed from the Control Panel (§31). The padding is what erases the
  longer word when the mode changes, so the field must always be written
  full-width.
- Graph: 1px black frame (6,14)-(`TM_RW`,55); interior columns x = 7+i,
  i = 0..`TM_GW`−1, rows 15..54. Column value v (0..40): white vline rows
  15..54−v, then black vline rows 55−v..54 (v=0 → all white, v=40 → all
  black). The column at tm_pos draws all white (the sweep gap).
- (6,61): `"RAM uuuK/tttK"` (white-fill (6,61)-(167,68) first).
- RAM bar: 1px black frame (6,71)-(`TM_RW`,80); interior (7,72)-(`TM_RW`−1,79):
  black for barw pixels from the left, white for the remainder.
- (6,87): header `"NAME     ST  CPU MEM"`.
- Process rows r = 0..TM_ROWS−1 at y = 97 + 11·r (white-fill
  (6,y)-(`TM_RW`,y+7) first), 21 chars. Columns, by index: `[0..8]` the
  **9-column NAME field** — an optional one-space indent, the name in 7,
  then padding out to 9 (`tm_name9`); `[9..11]` state, `[12]` space,
  `[13..15]` CPU share right-aligned, `[16]` `'%'`, `[17..19]` memory
  right-aligned, `[20]` `'K'`. Nine and not eight because the indent has to
  come from somewhere, and taking it out of the name truncated the one
  seven-character name in the tree to `TaskMg`.
- Row 0 is the kernel: name `System` (never indented), state `run` if
  `sch_cur` was 0 at snapshot time else `rdy`, MEM = `TM_KLOW_KB` +
  `TM_KSEG_KB` + the kernel's own heap claims.
- Rows 1.. render the instances in `tm_order`'s grouping. A built-in is
  indented one space; a package sits at the top level with System. Name is
  the I_NAME snapshot. State: I_STATE 2 → `die`; else I_TASK = 0xFF (or ≥
  MAX_TASKS) → `evt` (no worker task: it only runs inside window callbacks —
  a package that claimed a §20.6 worker renders `run`/`rdy`/`slp` here like
  Clock, which is correct and not a bug); else its task's slot = `sch_cur` →
  `run`, T_STATE 2 → `slp`, otherwise `rdy`. MEM = the region rounded up to
  KB **plus every KB the instance holds off the claim heap** (§50.5), or
  `"   -"` (no `'K'`) when the sum is zero — a built-in with no claims owns
  nothing, and a misleading `0K` is not used.
- A free slot renders `-` / `---` / `  -` / `   -`: name dash, state
  dashes, no `'%'` and no `'K'`.

**Content layout — memory view** (content-relative; the same 231×282). Two
map: conventional memory as a whole. There were two — the second magnified
the package pool — and the pool is gone (§20.1), so it had nothing left to
show; the package regions it drew now appear in the first map at their real
addresses, in the same per-slot patterns, which is a strictly better place
for them because it puts them next to the data claims they share the heap
with.

**The map is captioned on the line directly above it**, and the heap's
figures share that line: a claim is drawn in the map at its real address, so
its figures belong to that map's caption. They used to sit on a line of their
own above the second map and read as *its* label.

- (6,4): `"RAM uuu/tttK  HEAP uuu/tttK"` — the performance view's readout
  plus the heap's, and **one `'K'` per pair, at the end** (`tm_kpair`): eight
  characters per pair is what fits both on one line. **One string and one
  `font_str`**, not two draws — two would need two check words and a fill
  that belongs to neither, where one string is one exact key. The performance
  view builds the same line without the HEAP half (`[tm_view]`).
- (`TMM_HSQ_X`,4): the claim legend square, drawn last, into the two-space
  gap the string just lettered white.
- `tm_str` must hold the longest of these lines, and this is it:
  `TM_STRMAX` is named in its parts for that reason. It was a flat 24 when
  the line was `"RAM uuuK/tttK"` alone, and stayed 24 when the line grew to
  27 characters — writing four bytes past the end of `.bss` twice a second,
  which blanked whatever variable followed.
- Conventional-memory map: 1px black frame (6,14)-(`TM_RW`,29), interior
  (7,15)-(`TM_RW`−1,28) = `TM_GW`×14. KB scale: KB k maps to interior column
  k·`TM_GW`/totalK; a region [a,b) KB fills columns a·`TM_GW`/totalK ..
  (b−1)·`TM_GW`/totalK inclusive, clamped so no region drops below 1px. The
  interior is white-filled (free), then, in order: **the kernel** in 50%
  gray, **its buffers** over the top of that in a texture of their own
  (`tm_pat_buf`, 2-on-2-off horizontal bars — the band is 14 rows tall and,
  on a 640KB machine, four pixels wide, so a texture has to carry its
  signature vertically or it has nowhere to show it), **each live heap claim**
  as a **framed block**
  (`tm_map_claim`: `tm_pat_clm` inside, a 1px black `gfx_frame` around) —
  read live at draw time from the claim table, so arming double buffering or
  opening a Disk window makes a band appear. The buffer band is what makes
  the bar say the same thing the rows do: the kernel is not one lump, and the
  part of it that is scratch rather than program is the part these figures
  are steered by (`docs/KERNEL-MEMORY.md`).

  **A claim is framed and the others are not**, and that asymmetry is the
  point. A claim is the only kind of band here that appears and disappears
  while you watch, several sit shoulder to shoulder, and the scale is coarse
  — 4KB per pixel on a 640KB machine, so a 3KB Disk-window cache is a single
  column. No texture can say where one claim stops and the next starts at
  that size; a 1px rule can, and `gfx_frame` degenerates into exactly the
  solid vertical line a one-column claim should look like. `tm_pat_clm` is
  deliberately light for the same reason — it is there so the interior is
  not white, and a darker one swallowed its own frame at four pixels wide.
  Claims used to share the kernel's 50% gray, which made the map say one
  thing about two unrelated ones: memory reserved at build time and memory
  asked for at run time.
- (6,34): `"PACKAGES uuu/tttK"` — allocated and total pool KB (§20.1), the
  pool map's caption. The two caption lines are CAPS and the list's rows are
  mixed case, which is the whole of the distinction between a map's label and
  a row.
- (16,62): header `"NAME     ADDR SIZE   HEAP"` (25 chars, the row width).
  Two spaces of gap before HEAP, not one: a 150K back buffer beside a 150K
  package ran the two figures together at the old 22-char width.
  **The NAME field is eight columns plus a separator.** Seven of them are the
  name (`tm_copy7`) and the eighth is the indent a nested row carries, so the
  four builders line their addresses up whichever way they spend it: System
  pays two trailing spaces, an instance row pays one leading indent and one
  trailing space, and a buffer row — which has no address — spans the whole
  nine. It was eight with no separator, and a name that used all seven of them
  ran straight into the segment beside it: `ARKANOI9E40`. `TM_STRMAX` is 28
  and sized by the `RAM`/`HEAP` line above, so the row had the column to
  spend, and at 25 chars it still ends 8px inside `TM_RW`.
  **The window is sized by the line above the first map, not by this one**:
  `"RAM uuu/tttK"`, the claim square and `"HEAP uuu/tttK"` land exactly on
  `TM_RW` = 223, which puts the template at 232 wide. `TM_GW` = `TM_RW` − 7
  follows it, so both map interiors always fill their frames edge to edge.
- Rows at y = 74 + 11·r, `TMM_ROWS = INST_MAX + 7` of them — System, its
  four buffer rows, the two group headings, and one per instance. **Free
  slots are not drawn**: 19 rows at the 11px pitch is 279 of the 281-pixel
  content, which is what decides both that and the template's height.
  `tm_mrow_open` clamps to the **live** frame on top of that constant
  (`tm_view_begin`), for the screens where §39.7 shrinks the window — nothing
  in the kernel clips a draw to a window, and on a 200-row CGA this one is
  156px tall.
- **The legend squares key the rows to the maps, and a row only gets one
  when the texture is its own.** `tm_pat_gray` on System (the kernel's span),
  the row's own pattern on the three buffer rows (their band) and on each
  package row (its slot pattern, drawn on the map at the region's real
  address now that the pool is gone, §20.1), `tm_pat_blk` on the
  `Packages` heading (the pool). `Code+data` gets **none** — it is drawn in the same
  gray as System, and a square that repeats one above it is not a legend.
  Nor does `Builtins`, which owns no band at all. This is checkable by eye
  and it has been wrong: row 0 carried a solid black square from before the
  maps were reworked, by which time solid black had become the *package
  pool's* band, so the legend was pointing at the wrong region entirely.
- The three buffer squares sit at `[tm_sqox]` = 22 rather than the 6 every
  other row uses, so they land **beside** their two-space-indented names
  instead of out at the margin. `tm_mrow_open` resets the offset per row.
- **The claim texture is keyed beside the `HEAP` figures, not in the list**,
  because it belongs to a *column* and not to any one row.
- **Every square goes through one routine** (`tm_sq_pat`) over an 8-byte
  pattern, including the two the maps themselves draw with `gfx_fill_gray`
  and a plain black `gfx_fill`. `tm_pat_gray` is byte for byte what
  `gfx_fill_gray` lays down — 0xAA on even rows, 0x55 on odd, and the
  pattern fill indexes by the same y — so a square is the same pixels as its
  band and not merely a similar grey. A set bit is **white** (§5), so
  `tm_pat_blk` is eight zeroes. A square is a **request**, `[tm_sqp]`, not a
  draw: the row's band is erased between composing it and lettering it.
- Row 0 (System): legend square 50% gray, the kernel's band; ADDR `0600`
  (where the kernel starts — `KERNEL_SEG`); SIZE = `TM_KERN_KB`; CLM = the
  kernel's own heap claims.
- **Four indented buffer rows under it** — `Code+data`, `Stacks`,
  `Disk bufs`, `FAT snap` — each with its size in the SIZE column and a dash
  in CLM, because a buffer is part of the kernel and not a claim. Between
  them they account for every byte of the System figure above, so it is not
  a lump. Every one of the four is an **assembly-time constant**
  (`TM_KIMG_KB`, `TM_KSTK_KB` + `TM_K0_KB`, `TM_KDSK_KB`, `TM_KFAT_KB`), so
  the once-a-second refresh spends four string copies and no arithmetic on
  them; the kernel's footprint is fixed at build time down to the paragraph
  (§2), so there is nothing to check at run time. They give up the ADDR
  column to have twelve characters of name, and land SIZE and CLM exactly
  where row 0 puts them. `Code+data` wears the **gray** square and the other
  three the **`tm_pat_buf`** one — `Code+data` has no square, because that
  is the same gray System already wears.
- `Builtins` heading — **no square, and that is the information**: a
  built-in owns no band on either map. Its code is already inside
  `Code+data` and its memory is heap claims, billed to its own row. Then one
  indented row per built-in instance: no square, `"   -    -"` for ADDR and
  SIZE (they own no region), and a real CLM figure — the Disk window's
  listing cache shows up here (§2.3).
- `Packages` heading + allocated/size of the pool (`tm_pool_kb`, which the
  which is the KB of HEAP the resident regions hold rather than of any pool
  — so it takes the SIZE column and no square, the regions wearing their own
  per-slot patterns on the map above — then one row per package instance: square = pattern i,
  which is how the row keys the pool map below it; ADDR = the I_SPTR snapshot (a **segment**,
  four hex digits); SIZE = I_SIZE in KB rounded up; CLM = its claims.
- A dying instance (I_STATE 2) still draws its region and its row: the
  region is still resident (§21).

**Slot patterns.** `tm_pats` — 12 × 8 bytes in .text; pattern i =
`tm_pats + i·8`, fixed
slot↔pattern for the instance's life (the §30 slot↔tile rule again).
All twelve are black-on-white dither/hatch textures (§5 bit sense: set =
white) chosen to stay tellable-apart at a few pixels' width; none is the
0xAA/0x55 50% checker, which the maps already use to mean
"system-reserved".

Menu/dispatch: see §12/§13 — "Task Manager" (CMD_TASKS = 3) is the System
menu's third item, under "Control Panel"; dispatch calls `app_launch`
KIND_TASKMGR like the §14 kinds.


### 28.1 The window is sized to the SCREEN, not to a constant

The template carried a fixed height, which is right on VGA's 480 rows and too
tall on Hercules' 348 - so the window hung over the dock strip, and
`dock_paint` then had to draw *under* a window instead of over the desktop —
which at the time sent the cheap raise path back to the full pass (§11.90),
so every window opened on top of it paid for that with a full repaint. On CGA's 200
rows it never fitted at all. (`wm_dock_under` has since made that case cost a
rectangle rather than the screen, but a window that fits is still the point.)

`tm_init` derives the height once, at boot, from `[vid_dock_y0]`: as many
process rows as the space between the menu bar and the dock will take, capped
at `TMM_ROWS`, and never fewer than one. That count is `[tm_colrows]`.

**One pixel of that space is spent before the frame gets any of it.**
`wm_dock_clear` tests `y+h` against `[vid_dock_y0]` with `jae`, because the
drop shadow lives on row `y+h` — so a frame that merely *reaches* the dock is
already covering its first row. `tm_init` writes the template's **y** as
`dock_y0 - 1 - h` floored at `MBAR_H` for that reason. `wm_fit` now takes the
same pixel off both of its own clamps (§39.7), so this is belt and braces
rather than the only defence it once was — but it is the number the derived
row count is computed against, so it stays written here.

This is separate from the per-row clip against `[tm_ylim]`, which stays: that
stops a row's glyphs being lettered over the dock once a second, and it is
what made an oversized window survivable rather than correct. The frame
fitting is what stops the redraws.

### 28.1.1 Two columns, on a screen too short for one

156 usable pixels does not hold 19 rows however the frame is sized, so a
screen that fits fewer than `TM_COL2_MIN` (12) rows in one column gets a
**second column and a window twice as wide** instead — if the screen is wide
enough to carry it (`TM_W + TM_COLW`, which 640px across is). CGA is the only
adapter that takes this path; VGA (19 rows) and Hercules (17) keep one column.

The two columns are **not the same depth**. Column 0 begins under the maps,
where the maps are; every later column begins at `TM_C2_ROW_Y`, the top of the
content, because nothing is drawn above it there. On CGA that is 3 rows beside
10 rather than 3 beside 3 — which is what makes both lists fit whole. Each
column carries its own copy of the header line, at `TM_C2_HDR_Y` for the later
ones.

`tm_row_place` is the single index→pixel mapping, used by both views, and the
order is **column-major**: rows `0..[tm_colrows]-1` fill column 0, then
`[tm_col2rows]` at a time fill each column after it. That is what makes "this
row has no place" monotone — once one row is refused every later row is too,
so a caller may stop rather than test the rest.

Three traps:

- **`[tm_cols]`, `[tm_colrows]`, `[tm_col2rows]` and `[tm_maxrow]` are set at
  boot and must live OUTSIDE `tm_zero_beg..tm_zero_end`**, which `tm_kinit`
  zeroes every time a window opens. `[tm_col2rows]` is a divisor, so getting
  this wrong is a divide-by-zero on the first launch, not a layout glitch.
- **Everything a row draws reads `[tm_rowx]`, never `[tm_cx]`** — the fill, the
  text, and *both halves* of a legend square. The frame and the interior of
  that square are drawn by different routines, and one of them reading `tm_cx`
  put a solid block in column 0 on top of whatever was there.
- **The chrome above the list is column 0's** and reads `[tm_cx]`: the maps,
  the bars and the caption lines. `tm_lfill` sets `[tm_rowx]` back to `[tm_cx]`
  itself rather than relying on running before the row loop.

## 29. instance.inc — the instance table (running-app lifecycle)

Every running application instance — built-in kind or loaded package — is
one record in `inst_tab`. The table is the single source of truth for
"what is running": the dock renders from it, the Task Manager names tasks
through it, and (from format v2 on, §21) the package-region allocator
derives occupancy from it. Label prefix `inst_` (lifecycle verbs use
`app_`). Included from kernel.asm right after `wm.inc`.

### 29.1 Record — 32 bytes (power of two: index↔ptr via CL shifts), `INST_MAX equ 12`

```nasm
I_STATE  equ 0    ; byte: 0 = free, 1 = live, 2 = dying (close requested)
I_FLAGS  equ 1    ; byte: bit0 = minimized (window hidden, tile in the dock)
I_KIND   equ 2    ; byte: KIND_* below; bit 7 set = package instance
I_TASK   equ 3    ; byte: task slot index (§8), 0xFF = task-less. Set by
                  ;       app_launch for a built-in kind with KD_TASK, or
                  ;       by inst_pkg_spawn for a package's one worker
                  ;       (§20.6); one task per instance is the hard cap —
                  ;       the field is one byte — and it is never cleared
                  ;       while the record lives
I_WIN    equ 4    ; word: window record ptr (valid while I_STATE != 0)
I_SPTR   equ 6    ; word: builtin — per-instance state block ptr (0 = none)
                  ;       package - the region's base SEGMENT (§20.1)
I_SIZE   equ 8    ; word: package — allocated region bytes; 0 for builtins
I_ICON   equ 10   ; word: near ptr to a 16x16 icon BODY (16 mask + 16 data
                  ;       words, the icon_draw16 layout, §25); 0 = generic
                  ;       fallback. Must outlive the instance (static data
                  ;       for builtins; inside the region for packages) —
                  ;       never a disk_icons index (disk_mount overwrites
                  ;       that buffer on every mount).
I_NAME   equ 12   ; 16 bytes: NUL-terminated copy, <= 15 chars; byte
                  ;       I_NAME+15 is always 0
I_CYC    equ 28   ; dword (lo word first): PIT cycles billed to this
                  ;       instance's window callbacks (§8.1), zeroed at
                  ;       alloc so a reused record never inherits a dead
                  ;       instance's tally. Written only by inst_charge,
                  ;       under the gfx lock; read by the Task Manager
                  ;       under pushf/cli (§28)
I_RECSZ  equ 32

KIND_ABOUT   equ 0
KIND_CLOCK   equ 1       ; (Note Pad was kind 1 until it became the
KIND_BOUNCE  equ 2       ;  NOTEPAD package, §27 — the numbering closed up)
KIND_FILES   equ 3
KIND_TASKMGR equ 4
KIND_CTRL    equ 5       ; Control Panel (§31)
KIND_PKG     equ 0x80    ; bit 7: package instance
```

The Standard File dialog (§38) is deliberately **not** here: it is a modal
interaction the kernel runs on behalf of the front application, not an
application, and §38 explains what that buys.

### 29.2 Concurrency rules (binding)

1. **Publish-last**: a record becomes visible by the `I_STATE ← 1` byte
   store, which must be the LAST write when creating an instance (the
   task_spawn precedent, §8).
2. **Free points**: task-less instances are freed (I_STATE ← 0) by the UI
   task under the gfx lock (inside `app_close_win`); task-owned instances
   are freed by `task_exit`'s release byte — interrupt-atomically, and
   simultaneously with the task slot (§8). **Both apply to package
   instances**, which since §20.6 can be on either side: for a package
   either free point also frees its region (rule 7), so a worker-owning
   package's region is released from `task_exit` under IF=0, not from the
   UI task under the lock.
3. **Lock-held readers** (dock, wm paint paths) may only dereference
   I_WIN/I_ICON/I_NAME of records read as I_STATE = 1 *during the same
   lock hold*; records read as I_STATE = 2 (dying) must be skipped.
4. **Lock-free readers** (the Task Manager) snapshot fields under
   `pushf`/`cli` … `popf`.
5. **Only the owning task destroys a task-owned instance's window** — the
   UI task may only `wm_hide` it. (Otherwise the wm slot could be reused
   while the sleeping task still holds the old ptr and would draw into a
   stranger's window.) Since §20.6 this governs package code the kernel
   does not control, which is exactly why a worker MUST call
   `OSAPI_TASK_ALIVE` at least once per outer-loop iteration: it is the
   only thing that ever runs the destroy half for a package instance.
6. Fields are immutable after publish, except I_FLAGS bit0 (written only
   by the UI task under the gfx lock) and I_STATE (atomic byte stores).
7. **Package region occupancy is derived** (format v2, §21): the byte
   range [I_SPTR, I_SPTR + I_SIZE) is occupied iff I_STATE != 0. There is
   no explicit region-free call — the store that frees the record frees
   the region.
8. Only the UI task allocates instance records, so allocation never races
   itself; the only non-UI transition is task_exit's 2 → 0 under cli.

### 29.3 Kind descriptor table (.text, `inst_kinds`, stride KD_SIZE = 16)

Per built-in kind: `KD_TPL` (word, wm_create template ptr), `KD_TASK`
(word, task entry, 0 = task-less — built-in kinds only; a package's worker
is claimed at run time through §20.6 and is declared nowhere),
`KD_POOL` (word, state pool base, 0 =
stateless), `KD_SSIZE` (word, pool stride), `KD_INIT` (word, state-init
proc or 0 — in: BX = window ptr, DI = state ptr or 0, SI = instance
record ptr; preserves all registers; runs on the UI task with no lock
held, window not yet visible), `KD_NAME` (word, NUL name string),
`KD_ICON` (word, 16x16 icon body ptr or 0), `KD_CAP` (byte, max
simultaneous instances), `KD_WFLAG` (byte — the former pad byte): extra
W_FLAGS bits app_launch ORs into the new window right after wm_create;
only feature bits (WF_SIZABLE — never bits 0/1, and WF_FULL makes no
sense at create time). The Disk kind sets WF_SIZABLE (§22); every other
row keeps 0.

Pinned caps: About 1 (stateless), Clock 10
(stride 8), Bounce 10 (stride 8), **Files 4 (stride 16, pool `fm_pool`)**,
TaskMgr 1 (one sampler), Control Panel 1 (no per-instance state, §31). The
Files cap is 4 because each window claims its own `VIEW_KB` cache (§2.3); `KD_CAP`, `VIEW_SLOTS` and the `fm_pool` size are one
number wearing three hats and must move together. The
per-kind caps deliberately over-subscribe INST_MAX now that Clock and
Bounce allow 10 each — `INST_MAX` (and MAX_TASKS, §8) is the real ceiling,
and a launch on a full table simply fails with CF=1. Since §20.6 and §34.5
put package workers and transient sound tasks in the same pool, MAX_TASKS
is now the binding ceiling more often than INST_MAX is.

`inst_kinds` rows are in KIND_* order, one 16-byte row per kind; the
Control Panel's row is the last one and is task-less, pool-less and
init-less:

```nasm
    dw cp_tpl, 0, 0, 0, 0, cp_sname   ; tpl, task, pool, ssize, init, name
    dw 0                              ; icon: generic fallback
    db 1, 0                           ; cap 1, KD_WFLAG 0: stateless
                                      ; task-less singleton, not resizable
```

### 29.4 Routines

| symbol | contract |
|--------|----------|
| `inst_init` | zero `inst_tab` + `inst_launch`. From kmain, after wm_init. |
| `inst_ptr` | in AL = instance index; out DI = record ptr. |
| `inst_of_win` | in BX = window ptr; out CF=0 + DI = record (via `wm_ptr2idx` + `wm_owner`), CF=1 if unowned. Preserves BX. |
| `inst_win_owner` | in BX = window ptr; out DI = record ptr, or **0** if unowned. `inst_of_win` with the CF case folded into the result, so a caller can stash one word across a callback. Preserves BX. |
| `inst_charge` | in DI = record (non-zero), DX:AX = a `task_cycles` stamp (§8.1): `task_debit`, then add the returned cycles to I_CYC. Preserves all registers. Called only by the W_PAINT / W_ONKEY / W_ONCLICK dispatch sites (§11/§13), which hold the gfx lock — and a task-owned instance destroys its window, clearing `wm_owner`, under that same lock before its record is freed (29.2), so the record named by wm_owner stays live for the whole charged stretch. |
| `inst_find_kind` | in AL = kind byte (exact match incl. bit 7); out CF=0 + DI = first record with I_STATE=1 of that kind, CF=1 none. |
| `inst_alloc` | out CF=0 + DI = free record with I_FLAGS/I_SPTR/I_SIZE/I_ICON/I_CYC zeroed, CF=1 table full. Does NOT publish. UI task only. |
| `inst_set_name` | in DI = record, SI = name source (NUL-terminated or NUL-padded; at most 15 chars taken). Zero-fills all 16 I_NAME bytes first. Safe on a package header's 16-byte name field. |
| `inst_bind_win` | in DI = record, BX = window ptr: I_WIN ← BX, `wm_owner[window index]` ← record index. |
| `app_launch` | in AL = kind (built-in). UI task only, no lock held; takes its own locks. out CF=1 failed (instance/window/task table full — silent no-op for the caller), CF=0 done. Order: cap check (at cap → gfx_lock, clear the live instance's minimized bit, wm_show it, gfx_unlock — i.e. "launch" of a full singleton fronts it; only-dying-instances → CF=1, retry after a task period) → inst_alloc → pool-slot pick (first candidate `pool + s·stride` not held by a same-kind record with I_STATE != 0) → template copied to scratch with x/y cascaded +16·s → wm_create (CF → fail; record was never published), then OR the kind's `KD_WFLAG` byte into the new window's W_FLAGS (§29.3/§11.1) → fill record (I_KIND, I_TASK=0xFF, I_ICON, name) + inst_bind_win → KD_INIT → **publish I_STATE ← 1** → if KD_TASK: task_spawn (AX = entry, DX = instance index), I_TASK ← returned slot; spawn CF → rollback (I_STATE ← 0, then locked wm_destroy) → gfx_lock, wm_show, gfx_unlock. |
| `app_close_win` | in BX = window ptr; **caller holds the gfx lock**; UI task only. Unowned window → wm_hide (fallback). I_STATE = 2 already → wm_hide (idempotent). Task-less (I_TASK = 0xFF) → I_STATE ← 2, wm_destroy (clears wm_owner, repaints), I_WIN ← 0, I_STATE ← 0 — for a package instance that final store frees the region (rule 29.2.7). Task-owned → I_STATE ← 2 (the die flag), wm_hide (instant feedback); the task tears down at its next wake — and for a package instance that took a §20.6 worker, `task_exit`'s release-byte store is what frees the region. A package instance reaches this second branch exactly when it owns a worker. |
| `inst_minimize` | in BX = window ptr, lock held: set I_FLAGS bit0 (unowned → skip), wm_hide. |
| `inst_restore` | in DI = record, lock held: clear I_FLAGS bit0, wm_show I_WIN. |
| `inst_task_die` | in DI = the CURRENT task's instance record; no lock held; **never returns**: gfx_lock, wm_destroy I_WIN (clears wm_owner), I_WIN ← 0, gfx_unlock, then `jmp task_exit` with BX = record ptr (I_STATE is offset 0 — the release byte). Reached from Clock's and Bounce's own loops (§14) and, for packages, from `inst_pkg_alive` (§20.6). |
| `inst_wchk` | module-internal (§20.6). in BX = an untrusted window ptr; out CF=0 if BX lies inside `wm_wins` and is record-aligned, CF=1 otherwise. Preserves everything but the flags. The fence in front of `inst_of_win` for package-supplied pointers, whose `div cl` would otherwise fault. |
| `inst_pkg_spawn` | API slot 0x0160 (§20.6). in AX = near worker entry, BX = the package's own window ptr; **caller holds the gfx lock** (exclusion against another *spawner* is `task_spawn`'s own IF=0 window, §8, not this lock). Refuses (CF=1, nothing created) when BX fails `inst_wchk`, names no owner, names a record with I_STATE ≠ 1, that record already has I_TASK ≠ 0xFF, or the **ownership fence** rejects it — the record must be a package (I_KIND bit 7) and AX must satisfy I_SPTR ≤ AX < I_SPTR + I_SIZE, which is what ties the spawn to the *calling* instance — or when `task_spawn` finds the table full. Else `task_spawn` (AX = entry, DX = instance index derived as (record − inst_tab) >> 5, the `app_launch` idiom), I_TASK ← slot, CF=0, AL = slot. Preserves every register but AL and the flags. No rollback exists or is needed — the instance is already published and stays live on refusal. |
| `inst_pkg_alive` | API slot 0x0168 (§20.6). in BX = the package's own window ptr; **gfx lock NOT held**; called from the worker only. Returns with every register and the flags preserved while BX names a record with I_STATE = 1 — and returns unconditionally, without exiting anything, when `sch_cur` = 0: the UI task must never `task_exit` (§8), so a wrong-context call is refused. Otherwise recovers the *running task's* record from `T_INST` (§8) and `jmp inst_task_die` — never returns. A record with I_WIN = 0 (corrupt table: nothing to `wm_destroy`, and BX = 0 there would zero the cold-entry `jmp` at 0800:0000) still exits with BX = the record, so the record, its region and its dock/tm rows are released. Only T_INST ≥ INST_MAX exits with BX = 0 — no release byte because there is no record — the `sbl_refill_task` precedent (§34.5). |
| `inst_launch_post` | in AL = kind: one atomic word store of kind+1 into `inst_launch` — the deferred launch channel for lock-held posters (drained by ui_task step 3, §13). Rapid double posts coalesce (last wins). |

**Sound teardown (§34.3, Phase 1).** Both free points release the
instance's sound grants inside the teardown window they already own:
`app_close_win`'s task-less path calls `snd_release_inst` (in: AL =
instance index) before its final `I_STATE ← 0` store, and `inst_task_die`
calls it before `task_exit` — so tone ownership, FM channel bits, stream
ownership and staging grants (§34) join the window, the task slot and the
package region on the list of what teardown must free. Closing a live
stream ends its refill task. A closed package can never leave a tone
droning or a dangling SB stream — including a worker-owning one, which
rides `inst_task_die`'s call to the same routine, so a package with a
worker is safe to close mid-tone.

### 29.5 State (.bss)

`inst_tab` (INST_MAX × I_RECSZ = 384 bytes), `inst_launch` (word: 0 =
none, else kind + 1), plus module scratch (template copy buffer, pool-slot
cursor — UI task only). All zeroed by `inst_init`.

## 30. dock.inc — the dock strip

A taskbar-style strip along the bottom of the screen showing one tile per
**running** instance (I_STATE = 1, §29), built-in or package. Clicking a
tile restores a minimized instance (`inst_restore`) or fronts a visible one
(`wm_front`). Label prefix `dock_`. The dock is not exposed to packages.

A tile carries **two independent states**, drawn as two different kinds of
mark on purpose. **Minimized** (I_FLAGS bit0) inverts the tile's interior —
the app is not on screen at all. **Active** — the instance that owns the
frontmost visible window (`wm_top` → `inst_win_owner`, resolved once per
`dock_paint` into `[dock_act]`) — doubles the tile's border instead. They
are mutually exclusive in practice, since a minimized window is not visible
and so cannot be frontmost, but nothing depends on that; and a heavier
border survives the reduction to three inks (§39.4) where a second colour
would not.

### Geometry (pinned)

```nasm
DOCK_H      equ 24              ; 1px black rule + 23 white rows
; The strip is pinned to the BOTTOM of the live screen, so its top row and
; its tile top row are runtime words, not constants (§39.2):
;   [vid_dock_y0]  = vid_h - DOCK_H  ; 456 VGA, 324 Hercules, 176 CGA
;   [vid_dock_ty0] = [vid_dock_y0]+3 ; tile top row (459 / 327 / 179)
DOCK_TILE_W equ 24
DOCK_TILE_H equ 20
DOCK_X0     equ 8               ; first tile's left edge
DOCK_STEP   equ 28              ; tile + 4px gap; 8 + 12*28 = 344 < 640
```

Look: black `gfx_hline` across row `[vid_dock_y0]`, white fill from the row
below it to `[vid_hm1]`, the screen's last (§39.2)
(an inverted menu bar). Tile i (= instance index i — **stable slot↔tile
mapping**, holes stay; quitting one instance never moves another's tile):
1px black frame DOCK_TILE_W × DOCK_TILE_H at x = DOCK_X0 + i·DOCK_STEP,
row `[vid_dock_ty0]`; the instance's 16×16 icon body (`I_ICON`, via
`icon_draw16`) at (x+4, `[vid_dock_ty0]`+2); I_ICON = 0 → the generic `ico_app16` **body** (the
library record's data at `ico_app16+2` — icon_draw16 takes a header-less
body, §25). Active (the tile's record == `[dock_act]`): a second 1px black
`gfx_frame` one pixel inside the first, x+1..x+DOCK_TILE_W−2, rows
`[vid_dock_ty0]`+1 through `[vid_dock_ty0]`+DOCK_TILE_H−2 — the icon body
sits at (+4,+2) and 16×16, so it clears both new edges. Minimized (I_FLAGS
bit0): `gfx_xor_fill` over that same interior rect.

### 30.1 It is called constantly and usually has nothing to do

Raising a window, showing one, hiding one, dragging one — each repaints the
chrome because the chrome *might* have changed, and most of the time the only
thing that did is which tile wears the active mark. So `dock_paint` is
incremental, and **reports in CF whether it put any pixel on the strip at
all** — which is what lets `wm_dock_under` (§11.90) skip putting the windows
back over it.

Each tile carries a **key** (`dock_key`): its `I_ICON`, rotated so the
pointer's high bits reach the low ones, XORed with live / minimized / active.
A tile whose key still matches `dock_ck[i]` — what it was last *drawn* as —
is left alone. 0 is reserved for "no tile", so a slot going free erases and a
slot filling draws. A focus change costs two tiles; nothing changing costs no
pixels. A tile that does change is **erased to white first**, because its
marks are not nested: active → plain has an inner frame to remove, and
minimized → plain an XOR to undo.

The strip's own rule and white field are redrawn only when `[dock_full]` says
so, and the single thing that sets it is somebody having drawn **over** the
strip: `wm_paint_all`'s dither, and `wm_paint_dmg`'s when the damage reaches
`[vid_dock_y0]` — which is how *"a window that was covering the dock moved
away"* gets its full redraw, since hiding, destroying, dragging and resizing
all arrive there with a rect that reaches the strip. `dock_force` is the
entry point and it clears the per-tile keys with it: a tile whose pixels were
painted over is not "unchanged" however much its state still matches.

`.bss` is not zeroed at boot (`-f bin`), so `dock_init` sets `[dock_full]`
and clears the keys itself.

The dock renders ONLY records read as I_STATE = 1 during the same lock
hold (§29.2 rule 3); dying records are skipped, so a closing instance's
tile vanishes with the `wm_hide` repaint. Icon pointers must satisfy
§29.1's lifetime rule — never a `disk_icons` index.

### Contracts

| symbol       | contract                                                    |
|--------------|--------------------------------------------------------------|
| `dock_init`  | reset module scratch. From kmain, right after desk_init.    |
| `dock_paint` | draw the rule, the strip and every live instance's tile. Called by wm_paint_all after `desk_paint`, before the menu bar and windows (lock held by caller) — windows cover the dock exactly like desktop icons (§26). |
| `dock_click` | in CX=x, DX=y (no lock held; called by ui.inc when wm_hit found no window, BEFORE desk_click). Out: CF=1 = consumed (any click with y ≥ `[vid_dock_y0]` — strip background clicks are consumed no-ops), CF=0 = not in the dock. Tile hit on a live instance: minimized → gfx_lock, `inst_restore`, gfx_unlock; else → gfx_lock, `wm_front` on I_WIN, gfx_unlock. Single click activates; no double-click logic. |

Every dock-state transition (launch, quit, minimize, restore) — and every
change of the ACTIVE tile, which is every raise — rides a `wm_show`,
`wm_front`, `wm_hide` or `wm_destroy` repaint, all four of which call
`dock_paint` unconditionally (§11.90/§11.91). So dock_paint needs no
partial-redraw path; if a future teardown path ever changes dock state
without a repainting wm_* call, it must add a desk_zone_redraw-style
partial redraw (overlap check against all windows, full wm_paint_all
fallback).

The window drag clamp (§13) is unchanged: windows may be dropped over the
dock; clicks in the overlap go to the window (wm_hit wins), and the strip
repaints when the window moves away — desk-icon semantics throughout.

## 31. ctrl.inc — the Control Panel window

Built-in singleton app kind (KIND_CTRL = 5, cap 1), window "Control Panel",
**320×140** at (160,130). Label prefix `cp_`. Included from kernel.asm right
after `taskmgr.inc`. (It was 320×120 until the Date/Time page of §31.5
needed two more control rows; the frame grew rather than that page being
cramped, and every other page simply has more white space below it.)

The window is a **two-pane browser**. The **left pane** lists the panel's
items, one row each, the selected row drawn as a black bar with white text;
the **right pane** is that item's settings page. Item 0 is **"Scheduler"**
— the scheduler mode of §8.2 as a pair of radio rows — and is the boot
selection, so the panel opens showing the scheduler page with no click
needed.

Structurally it is still the simplest kind in the tree — **task-less, no
KD_INIT, no pool, no .bss**: nothing runs for it at boot and it exists only
while an instance is open. Its one byte of panel state is `cp_sel`, the
selected item, kept as initialised `.text` data (the `osapi_seed` trick of
§15) precisely so nothing has to be zeroed at boot. `cp_sel` is global, not
per-instance, and persists across opens; with one item that is
indistinguishable from resetting to 0.

```nasm
cp_ttl   db 'Control Panel', 0   ; window title
cp_sname db 'Control', 0         ; KD_NAME: <= 7 chars, fits the Task
                                 ; Manager NAME column (the tm_sname rule)
cp_tpl:  dw 160, 130, 320, 140, cp_ttl, cp_paint, 0, cp_onclick
         ;  x    y    w    h     title   paint     onkey  onclick
cp_dirty db 0                    ; deferred-repaint flag (§31.2)
cp_sel   db 0                    ; selected item; 0 = Scheduler
```

No onkey (the panel is mouse-only), no icon of its own (KD_ICON 0 → the
generic `ico_app16` body in the dock, §30), I_SIZE 0 → MEM reads `-` in the
Task Manager (§28).

**User-visible wording (binding).** The two modes are called exactly
**"Pre-emptive"** and **"Cooperative"**. The word "watchdog" — and any
mention of `SCH_WD_TICKS`, forced switches or `sch_wd_hits` — must **not**
appear in any string the user can see, here or anywhere else in the GUI.
The watchdog is an implementation detail of cooperative mode (§8.2), not a
third mode and not a setting.

### 31.1 Content layout (pinned, content-relative)

Content is 318×121 (frame 320×140 minus the 1px borders and the 18px title
bar). Every coordinate below is relative to the window's content origin,
which `cp_paint`/`cp_onclick` derive from the live `W_X`/`W_Y` — **never
hardcoded screen coords**, because the window drags. Both callbacks receive
the window in SI and `wm_content` takes it in BX (out AX = content left,
DX = content top), so both copy SI → BX first and then carry the origin in
**DI = content left, BP = content top** — the register pair every helper in
the module takes it in.

```nasm
CP_CW    equ 318     ; content width          CP_RX   equ 96   ; right pane x
CP_CH    equ 121     ; content height
CP_DIVX  equ 88      ; divider column: left pane is x 0..CP_DIVX-1
CP_IX    equ 6       ; item label x           CP_I0Y  equ 6    ; row 0 top
CP_IROWH equ 14      ; row pitch = hit band   CP_IBH  equ 12   ; sel bar height
CP_ITDY  equ 2       ; label y offset inside the bar
CP_IBX1  equ 2       ; sel bar left           CP_IBX2 equ CP_DIVX-3  ; 85
```

- **Left pane**, x 0..CP_DIVX−1: item *i*'s row top is
  `CP_I0Y + i*CP_IROWH`; its name is drawn at (CP_IX, row top + CP_ITDY).
  The selected item's row is a black `gfx_fill` (CP_IBX1, row top)–(CP_IBX2,
  row top + CP_IBH − 1) with the name in white; every other name is black
  on white.
- **Divider**: a 1px black `gfx_vline` at content x = CP_DIVX, y 0..CP_CH−1.
- **Right pane**, x CP_RX..CP_CW−1, top = the content top: the selected
  item's page, drawn by its own proc in **pane-relative** coordinates.

**Item table (binding).** One 8-byte record per item, stride a power of two
so index → record is three `shl`-by-1s (no CL, no 8086-illegal immediate
shift). `CP_ITEMS` is computed from the table's own extent, so adding an
item is one row plus a paint/click pair — the pane machinery does not
change.

```nasm
CP_I_NAME  equ 0   ; -> list name, ASCIIZ
CP_I_PAINT equ 2   ; -> page paint proc   (in DI = pane left, BP = pane top)
CP_I_CLICK equ 4   ; -> page click proc   (in DI/BP, CX/DX = pane-relative)
CP_ISTRIDE equ 8   ; 4th word reserved (a future page onkey proc)
cp_items:  dw cp_s_sched, cp_sched_paint, cp_sched_click, 0
           dw cp_s_disp,  cp_disp_paint,  cp_disp_click,  0   ; §31.3
           dw cp_s_snd,   cp_snd_paint,   cp_snd_click,   0   ; §31.4
           dw cp_s_time,  cp_time_paint,  cp_time_click,  0   ; §31.5
cp_items_end:
CP_ITEMS   equ (cp_items_end - cp_items) / CP_ISTRIDE
CP_ITIME   equ 3     ; the Date/Time item's index: §12.1 selects it by name
```

**List names are at most 9 characters** (72px): the selection bar runs from
CP_IBX1 to CP_IBX2 = 85 and the name starts at CP_IX = 6, so a tenth glyph
would cross the divider. `'Scheduler'` and `'Date/Time'` are both exactly
at that limit.

**Page contract.** A page proc is called with **DI = pane left (absolute
screen x), BP = pane top (absolute screen y = the content top)**, the gfx
lock held by the caller and — for paint — the pane already white-filled by
`cp_page`. Click procs additionally get **CX = pane-relative x, DX =
pane-relative y**; CX may be *negative*, because the gutter between the
divider and CP_RX (and the window's left border) reaches them. Page procs
preserve all registers, must not lock, block, spawn or call BIOS, and must
not draw outside their pane.

**The Scheduler page (item 0)**, pane-relative:

```nasm
CP_PMX   equ 2       ; left margin for the heading and the caption
CP_PHY   equ 6       ; 'Scheduler' heading text row
CP_PGX   equ 4       ; radio glyph left edge
CP_GW    equ 12      ; radio glyph is 12x12
CP_PR0Y  equ 26      ; first radio row: glyph top
CP_PROWH equ 20      ; row pitch: row i glyph top = CP_PR0Y + i*CP_PROWH
CP_PLX   equ CP_PGX + 18   ; 22: label text x
CP_PLDY  equ 2       ; label text y = glyph top + 2 (8px font in a 12px box)
CP_PCAPY equ 74      ; caption text row
```

- Heading `'Scheduler'` at (CP_PMX, CP_PHY), black — the **same string** as
  the list row that selects the page.
- Row 0: radio glyph at (CP_PGX, CP_PR0Y), label `'Pre-emptive'` at
  (CP_PLX, CP_PR0Y + CP_PLDY).
- Row 1: radio glyph at (CP_PGX, CP_PR0Y + CP_PROWH), label
  `'Cooperative'` at (CP_PLX, CP_PR0Y + CP_PROWH + CP_PLDY).
- Caption at (CP_PMX, CP_PCAPY): one short line stating that the change
  applies at once, e.g. `'Takes effect immediately'` (24 chars = 192px,
  inside the 222px pane). No other text.

**Radio glyphs.** Two hand-authored 12×12 bitmaps, one word per row, bit 15
= leftmost pixel (the §12 logo / §25 icon convention): `cp_radio_off` (an
open ring) and `cp_radio_on` (the same ring with a filled centre dot), 24
bytes each. They are drawn by a module-internal `cp_glyph` — in CX = left
x, DX = top y, SI = bitmap; 12 rows of `lodsw` + per-bit `gfx_pixel` in
`[gfx_color]`, preserving all registers — because neither existing blitter
fits: `menu_logo_glyph` hardcodes its top row at MENU_LOGO_Y, and
`icon_draw16` wants a 16 mask + 16 data word body (§25). The glyph for the
**live** mode is the filled one; the other is the ring. `sched_mode_get`
(§8.2) is the only source of that truth — the page keeps no state of its
own, so a mode changed from anywhere else still paints correctly.

### 31.2 Contracts

| symbol | contract |
|--------|-----------|
| `cp_paint` | W_PAINT (§11): in SI = window ptr; the gfx lock is **already held** by the caller and wm has already white-filled the content. Preserves all registers. Derives DI/BP from `wm_content`, then `cp_list` → `cp_divider` → `cp_page`. Must not lock, block, spawn, or call BIOS. |
| `cp_onclick` | W_ONCLICK (§11): in CX = x, DX = y (**absolute screen coords** — convert with `wm_content` before hit-testing), SI = window ptr. The gfx lock is already held and the call is already billed to the instance by ui.inc (§8.1/§13). Preserves all registers. Content-relative x < CP_DIVX → `cp_pick`: a hit on a different item stores it in `cp_sel` and redraws both panes (`cp_list` + `cp_page`); a hit on the live item, or a miss below the last row, does nothing. Otherwise the click is handed to the selected item's `CP_I_CLICK` proc with DI advanced to the pane left and CX made pane-relative. |
| `cp_entry` | module-internal: in AL = item index, out SI = record ptr (`cp_items + 8*AL`). Preserves everything else. |
| `cp_pick` | module-internal hit test of the item list: in DX = content-relative y, out CF=0 and AL = item index on a row, CF=1 above the first row or below the last. Rows are contiguous — row *i* owns `CP_IROWH` rows from `CP_I0Y + i*CP_IROWH`, so the bar and the 2px gap under it both select. x is not tested: the whole pane width selects. |
| `cp_list` | module-internal, in DI/BP = content origin, lock held. Preserves all registers. White-fills the whole left pane, then draws every item name with the `cp_sel` row barred — so it doubles as the redraw path when the selection moves. |
| `cp_divider` | module-internal, in DI/BP, lock held: the 1px black rule at content x = CP_DIVX. Preserves all registers. |
| `cp_page` | module-internal, in DI/BP, lock held. Preserves all registers. White-fills the right pane (divider column excluded), then calls the selected record's `CP_I_PAINT` with DI advanced by CP_RX — the redraw path when the selection moves. |
| `cp_sched_paint` | Scheduler page paint (page contract above): heading, both radio rows (glyph + label, filled glyph per `sched_mode_get`) and the caption. |
| `cp_sched_click` | Scheduler page click (page contract above). x is ignored — the two hit bands span the whole pane. A hit on the row whose mode is already live does nothing; a hit that changes the mode calls `sched_mode_set` with AL = the row index (0 = pre-emptive, 1 = cooperative), sets `[cp_dirty]` = 1, then redraws **only the two radio glyphs**. A click outside both bands does nothing. |
| `cp_glyph` | module-internal 12×12 bitmap blit: in CX = x, DX = y, SI = bitmap. Preserves all registers. Lock held by the caller. |
| `cp_sel` | byte, initialised `.text` data, init 0 (Scheduler). Written only by `cp_onclick`, and only to an index `< CP_ITEMS`. |
| `cp_dirty` | byte, initialised `.text` data, init 0. Set to 1 by `cp_sched_click` only when the mode actually changed; drained by ui_task step 3 (§13), which zeroes it and does gfx_lock / `wm_paint_all` / gfx_unlock. Idempotent and coalescing: repeated flips inside one UI pass cost one repaint. |

**Hit bands (Scheduler page).** Pane-relative, generous and contiguous so
that both the glyph and its label select the row: row *i* covers y from
`CP_PR0Y + i*CP_PROWH − 4` to `CP_PR0Y + i*CP_PROWH + CP_GW + 3` (row 0 =
22..41, row 1 = 42..61) across the **whole pane**. The heading above and
the caption below are outside both bands, so clicking them is a no-op.

**Signed comparisons (binding).** Every hit test in this module compares
**signed**: `wm_hit` dispatches the window's 1px border as content, so a
content- or pane-relative coordinate can legitimately be negative (or one
past the content) and an unsigned compare would place such a click inside
a band.

**Locking (binding).** `cp_onclick` runs on the UI task inside ui.inc's
W_ONCLICK dispatch, which already holds the gfx lock. It must **not** call
`gfx_lock`/`gfx_unlock` — the lock is non-reentrant (§7) and re-acquiring
it deadlocks the GUI, the same trap as §13's drag-release step. It must
**not** call `wm_paint_all`: every redraw is done in place, by erasing and
redrawing exactly the rectangle that changed — the two panes on a selection
change, the two 12×12 glyph boxes on a mode flip. No `wm_obscured` check is
needed — W_ONCLICK only ever fires on the frontmost window (§13).

**Other windows follow via `cp_dirty`.** Nothing else on screen is
invalidated by a mode change, so an already-painted About box would keep
displaying the previous scheduling word (§14) indefinitely — no wm_* path
repaints it until an unrelated raise, drag or close happens. A flip
therefore sets `[cp_dirty]`, and ui_task step 3 (§13) does the
`wm_paint_all` **outside** the lock, in the same UI pass as the click:
lock-clean, non-recursive, and each window's paint stays inside its own
`wm_draw_win` billing bracket (§8.1). A *selection* change invalidates
nothing outside this window and posts nothing.

**Why a mode flip is safe from inside a callback.** `sched_mode_set` is one
`pushf`/`cli`-guarded byte store plus a counter reset (§8.2); the task
table, the per-task stacks and the saved-frame layout are identical in both
modes, so no task has to be parked or notified and the call cannot block.
The flip therefore happens with the gfx lock held and takes effect on the
very next tick — which is exactly what the caption promises. The other two
places that show the mode read it live: the About box's third line (§14),
repainted within the same UI pass by the `cp_dirty` repaint above, and the
Task Manager's SCHED field (§28), rewritten at its next sample (≤ 9 ticks)
and by that repaint.

### 31.3 Display page — double buffering

Second item in the panel list, same two-row radio geometry as §31.2 (it
shares `cp_glyph` and the `CP_B*Y` hit bands). Heading "Display"; row 0
"Direct to screen", row 1 "Double buffered"; the filled glyph follows
`[bb_dbl]`, the armed-buffer flag and **not** `[bb_on]` (1 on any mono
adapter, §39.5); it is **0 at boot** — double buffering is opt-in.

Caption: "Smoother; costs 150K" normally, or — on a 1bpp card — the two
lines "Framebuffer is the display" / "driver on this adapter", which say why
rather than merely refusing: the software renderer *is* the direct path
there (§39.5), so there is nothing to double at any memory size and
`[bb_avail]` is never armed at all. Two lines because the pane is 27
characters wide and the answer is 49; the caption is the lowest thing on the
page, so `CP_PCAP2` sits under it. On such a machine the page is
display-only: a
click in either band is ignored outright rather than moving a dot that
`bb_set` would refuse to honour.

**"Double buffered" greys out when the heap cannot fund it.** `cpf_dbok`
asks `bb_canfit` (§32) — `[bb_avail]` set *and* a free run of `BB_KB`
available right now — and when the answer is no the row's label and glyph
are drawn in `CDGRAY` with **"Not Enough Ram"** beside it, and the click
band is inert. This is the reciprocal half of the claim heap (§50) and the
thing docs/PAINT-NOTES.md said was usually forgotten: a kernel feature that
speculatively wants 150KB has to ask the same allocator a package does, and
be told no by the same answer. It is live state, not a boot-time verdict —
open a package that claims a canvas and the row greys out; close it and the
row comes back.

`cp_disp_click` mirrors `cp_sched_click` — signed comparisons, x ignored,
a hit on the live row does nothing — and calls `bb_set` (§32), which
requires the gfx lock the click handler already holds. It then redraws just
the two glyphs. No `[cp_dirty]`: unlike the scheduler mode, no window quotes
this setting, and the switch is invisible except as speed.

### 31.4 Sound page — retired

The panel had four items; it has three. The Sound page existed to choose
a tone route, to allow or forbid exclusive clips, and to
play a test clip through the sound card. With one sink there is nothing to
choose, and the page went with §34's driver table. `CP_ITIME` — the
Date/Time item index ui.inc selects when the menu-bar clock is clicked
(§12.1) — is therefore **2**, not 3.

The three-layer refusal idiom the page demonstrated (setter refuses,
caption explains, click ignored) is not retired with it: §31.3's Display
page still uses it, and §50.5 extends it to "Not Enough Ram".

### 31.5 Date/Time page — setting the system clock

Fourth item in the panel list, index `CP_ITIME` = 3, list name and heading
`'Date/Time'`. It is the editor for the system clock of §37 and the target
of a click on the menu-bar clock (§12.1). Two rows of fields — the date
above, the time below — one of which is *selected*, a `+` / `-` button pair
that steps the selected field, and below them the clock's two **display
options**. Pane-relative geometry:

```nasm
CPT_FX    equ 8     ; field text left x       CPT_DY equ 26  ; date row y
CPT_MONW  equ 24    ; 'Mmm' field width       CPT_TY equ 48  ; time row y
CPT_NUMW  equ 16    ; 'DD'/'HH' field width   CPT_YRW equ 32 ; 'YYYY' width
CPT_BX    equ 150   ; button column x         CPT_BW equ 28  ; button size
CPT_BUY   equ 22    ; '+' button top          CPT_BH equ 18
CPT_BDY   equ 44    ; '-' button top
CPT_O0Y   equ 68    ; option row 0 glyph top  CPT_O1Y equ 84 ; option row 1
CPT_CAP1Y equ 102   ; instruction caption     CPT_CAP2Y equ 112 ; RTC caption
```

**Field table (binding).** Seven entries, `db x, w, y, 0` — stride 4, so
index → record is two `shl`-by-1s. The index *is* the field number
`clk_fld_str` / `clk_fld_adj` take (§37), which is what keeps the page free
of any knowledge of what a month or an hour is:

```nasm
cp_tflds:  db CPT_FX,    CPT_MONW, CPT_DY, 0   ; 0 month  'Mmm'
           db CPT_FX+32, CPT_NUMW, CPT_DY, 0   ; 1 day    'DD'
           db CPT_FX+56, CPT_YRW,  CPT_DY, 0   ; 2 year   'YYYY'
           db CPT_FX,    CPT_NUMW, CPT_TY, 0   ; 3 hour   'HH'
           db CPT_FX+24, CPT_NUMW, CPT_TY, 0   ; 4 minute 'MM'
           db CPT_FX+48, CPT_NUMW, CPT_TY, 0   ; 5 second 'SS'
           db CPT_FX+72, CPT_NUMW, CPT_TY, 0   ; 6 AM/PM  — 12-hour only
cp_tsel   db 0      ; selected field, initialised .text data like cp_sel
```

**The active field count is `6 + [clk_h12]`** — the meridiem field exists
only while the clock is in 12-hour mode. Both the draw loop and the hit
test derive their bound from it, and turning 12-hour mode **off** while
that field is selected resets `[cp_tsel]` to 0, so the selection can never
point past the end of the table.

The gaps between the date fields are blank; the two `:` separators of the
time row are drawn black at `CPT_FX+16` and `CPT_FX+40`. A field is drawn
by its own string from `clk_fld_str`: black on white, or — when it is the
selected one — white on a black `gfx_fill` box inset 2px around the glyphs
(x−2..x+w+1, y−2..y+9), the same look as the item list's selection bar.

- `cp_time_rows` (module-internal, in DI/BP, lock held) white-fills the
  band the two rows occupy (pane x 0..`CPT_BX`−4, y `CPT_DY`−4..`CPT_TY`+11
  — the button column excluded), takes ONE `clk_snap` (§37) and redraws
  both rows from it. It is therefore the redraw path for **every** change:
  a new selection, a `+`/`-` step, an option toggle, and the once-a-second
  tick.
- `cp_time_paint` — heading, `cp_time_rows`, both buttons (`cp_timebtn`,
  a `gfx_frame` + white interior + centred `'+'`/`'-'` glyph), the two
  option rows (12×12 `cp_chk_on`/`cp_chk_off` glyph at CP_PGX, label at
  CP_PLX — the Sound page's checkbox idiom, §31.4), and two captions:
  `'Click a field, then + or -'` at CPT_CAP1Y and, at CPT_CAP2Y,
  the `cp_rtcnam` row for `[clk_tier]` — `'Hardware clock: none'`,
  `'... AT 70h'`, `'... 58167 2C0h'`, `'... 5C01 2C0h'` or `'... BIOS'`,
  read live from `[clk_rtc]`/`[clk_tier]` (§37.90). The `bb_avail` caption
  idiom again, and it names the RUNG rather than answering yes/no because
  on a machine whose clock will not hold a setting that is the diagnosis.
  Purely informative: editing works either way.
- **The option rows** are `'12-hour clock'` (`[clk_h12]`) and
  `'Seconds in menu bar'` (`[clk_secs]`), both §37 state read live, so the
  page keeps no copy. A click toggles the byte, redraws the glyph, redraws
  the field rows (12-hour mode changes what the hour field says and
  whether the meridiem field is there at all) and sets `[clk_barq]` so
  ui_task repaints the menu bar in the same pass (§37). Not `[cp_dirty]`:
  a whole-desktop `wm_paint_all` for a clock-format change would be a
  visible flash to fix a flicker.
- `cp_time_click` — signed comparisons throughout (§31.2). **Tested in
  this order**, because the option rows own the full pane width while the
  buttons own only their column: y ≥ `CPT_O0Y`−4 → the two option bands
  (64..81, 82..99); else x ≥ `CPT_BX` → the `'+'` band
  (`CPT_BUY`..+`CPT_BH`−1) and the `'-'` band (`CPT_BDY`..+`CPT_BH`−1),
  x ≤ `CPT_BX+CPT_BW`−1 → `clk_fld_adj` with AL = `[cp_tsel]`, BL = +1/−1,
  then `cp_time_rows`; else the field bands (each x−2..x+w+1 by y−4..y+11)
  → store the index in `[cp_tsel]` and redraw the rows. A hit on the live
  field or a miss does nothing. No BIOS call anywhere: `clk_fld_adj` sets
  `[clk_dirty]` and ui_task writes the RTC outside the lock (§13 step 3).
- `cp_tick` — the live refresh, called by ui_task step 4 under the gfx
  lock, like the other two entry points. It returns
  immediately unless `[cp_sel]` = `CP_ITIME` **and** `inst_find_kind`
  KIND_CTRL finds a live instance **and** its window is visible **and**
  `wm_obscured` says clear; only then does it derive DI/BP from
  `wm_content` and call `cp_time_rows`. Every one of those guards is the
  §14 background-drawing contract, checked under the lock the caller
  already holds.
- `cp_tick_due` — the cheap gate ui_task consults *before* taking the lock
  (§13 step 4): CF = 1 if the panel is live, visible and showing this
  page. **Near code, not far** (it runs every second and is a dozen
  instructions — the `tm_init` precedent of §33), and it reads the
  instance table with **no lock held**, which is safe because only the UI
  task allocates or frees a task-less instance and `cp_tick_due` *is* the
  UI task; a stale answer costs one wasted lock, and `cp_tick` re-checks
  everything under it anyway.

Kernel routines this page reaches (all ordinary near calls since §33):
`inst_find_kind`, `clk_snap`,
`clk_fld_str`, `clk_fld_adj` (§33; `wm_content`, `wm_obscured`, `gfx_fill`,
`gfx_frame` and `font_str` already have wrappers).

### 31.6 Drivers page — loading and unloading, and remembering it

Fourth item, index `CP_IDRV` = 3, list name and heading `'Drivers'`. One row
per `drv_tab` row (§51): a checkbox, the driver's name, and under it the
sentence `drv_status` derives from the row's live state — `'Loaded'`,
`'Not loaded'`, or why the last attempt failed.

**The checkbox tracks what is LOADED, not what the settings file wants.** A
driver enabled on a machine with no card is unchecked, with `'No hardware
found'` under it. That is the `bb_avail` idiom (§31.3) again: the box, the
caption and the click all read one word, so they cannot disagree.

**A click loads or unloads on the spot**, mounting A: on demand, and only
then writes `SYSTEM.CFG`. So the page never shows a promise about the next
boot — what it shows is what is running. The two outcomes are reported
separately: a load that fails leaves the box clear with its reason, and a
*save* that fails puts `'Cannot save to the system disk'` in the caption
while the driver it just loaded stays loaded.

A load does floppy I/O with the gfx lock held. That is the bargain every
file operation in this OS makes (§18.4) — the cursor freezes for the length
of the read — and the alternative, dropping the lock inside a click handler,
is not one the window manager offers.


### 31.7 Sound page — which sound hardware the machine uses

Fifth item, index `CP_ISND` = 4. **Three** radio rows on the Display page's
geometry — **PC Speaker**, **AdLib**, **Sound Blaster** — and a **Test**
button under them that plays one second of 660 Hz at `SND_PRI_UI` through
whatever is selected. The answer to "did that do anything?" is one click away
and does not need an app.

The rows are the §34.8 ladder, and the labels are **fixed**. They used to be
two, the lower one labelled from the loaded driver's own `DSV_NAME` — which on
a Sound Blaster read `Sound Blaster` with the DSP tier up and `AdLib` with it
down, so the label moved as the setting moved and the one control on the page
could not be read as a choice between two things. Nothing stages `DSV_NAME`
into the kernel any more; it is published as a pointer and that is all
(`drv_publish`, §51.2).

**A row greys for missing hardware and never for missing memory**, and the line
between those two is the whole design of the page:

- **Hardware is a fact the driver already established.** `DSV_TIERS` (§51.2) is
  a bitmap of the tiers the driver *could* provide, decided at attach from what
  its probes found and never touched by a tier change. The page reads one word
  and greys what the machine does not have. It does not ask the driver to look
  again — an ISA card cannot arrive while the machine is running, and looking
  again is **expensive**: the Sound Blaster scan is six base addresses with a
  DSP reset timeout each and then a slow retry on 220h, run on the UI task
  under the gfx lock. Measured on QEMU it hid the cursor for 27 ms; the poll
  budgets it is built from (2,000 status reads per base, 65,536 for the retry)
  are sized for period ISA timing and cost proportionally more on the machine
  they were written for. Either way it is a visible hitch on a click that then
  refuses.
- **Memory is not a fact anyone has.** Whether the heap can fund a page-safe
  12KB *right now* is answerable only by making the claim: `mem_avail` reports
  the largest free run and says nothing about whether a 64KB-page-safe base
  exists inside it (§50.3), so only `mem_claim_dma`'s own scan knows. The click
  therefore tries, and reports what came back.

That second half is the one place the page departs from the `bb_avail` idiom
(§31.3), and deliberately: `bb_canfit` is a real predicate and `mem_claim_dma`
has none. Greying on a guess would keep a machine that has the room off a tier
it can afford.

A driver that publishes `DSV_TIERS` = 0 said nothing and is taken at its word:
every row stays live and the click finds out the slow way, which is exactly the
behaviour before the cell existed.

**A greyed row greys its GLYPH as well as its label**, and on this page that is
what carries the whole signal on two adapters out of three. `font_ink` rounds
`CDGRAY` to *black* for text — a dithered 8x8 glyph is unreadable, so mono has
no grey to draw a letter in (§39.4) — which means a disabled label is
pixel-identical to a live one on Hercules and CGA. The Display page gets away
with that because its caption says why in words; this page has no caption, so
the row has to say it some other way. A glyph can, because it is drawn with
`gfx_pixel` rather than as a font cell: `gfx_ink` maps `CDGRAY` to the 50%
dither, so the ring comes out **dotted**. Dark grey on VGA, dotted on mono, and
on all three the whole row reads as disabled instead of a black control with
faint writing beside it.

A refused tier change leaves the setting where it was and writes the reason
into a **notice line** below the Test button, using the loader's own
`drv_errstr` strings: `No hardware found` or `Not enough memory`. A tier change
and a driver load fail for the same two reasons, so they say it the same way.
The line is blank otherwise, and the next click clears it. The old
detected-card caption is gone — three named rows and the greying carry
everything it used to say.

The dot follows the **effective** tier, never the stored setting, so it cannot
claim hardware that is not sounding. The two differ in exactly one case: a disk
written on a machine with a Sound Blaster and carried to one without.
`[snd_route]` keeps `SND_RT_SB` there — taking the setting away would mean the
disk stopped working when it went home — and the dot honestly shows whichever
tier the driver actually reached.

### 31.8 Every setting is remembered — `cp_flush`

Each page calls `cp_flush` after it changes anything, and `cp_flush` writes
`SYSTEM.CFG` (§51.5) — one 32-byte file carrying the whole panel: the driver
list, the sound route, the clock options, the scheduler mode and the back
buffer. One file and one writer, so there is no per-setting bookkeeping to
keep in step, and `drv_boot` restores the lot before the desktop is painted.

It writes **once, when the panel closes**, not on every click. Pages set
`[cp_wdirty]`; `cp_flush_close` spends it. A floppy write is about a second
on the floor machine and the panel is frozen for it, so trying five settings
out used to cost five seconds of frozen UI for a state only the last one
describes.

**Both ways the panel stops existing flush it**, which is what makes the
deferral safe:

- `app_close_win`'s task-less branch (§29.4), gated on `I_KIND == KIND_CTRL`.
  The panel owns no task, so it can never reach `inst_task_die` and the one
  branch is the whole story.
- The reboot path (§12.3), before the lock is taken and before `vid_text` —
  a disk write is not something to attempt with the GUI already torn down.

**Minimizing is not one of them.** The panel is still open, and hiding it is
not a decision to save.

What this gives up is the crash window: a machine switched off with the panel
open loses whatever was changed in it. That is deliberate — these settings are
a convenience, not a document.

A failed write never undoes the change — that already happened — and
**leaves `[cp_wdirty]` set**, so the next close or reboot retries rather than
dropping it silently. It is reported in the Drivers page's caption
(`'Cannot save to the system disk'`), the one page with room to say it, which
now means the *next* time the panel is opened: by the time the write happens
the page it would be reported on is already gone.

## 32. vgabb.inc — the software renderer (double buffering, and §39's 1bpp driver)

**Why it exists.** The original design drew straight into VRAM because
256KB of RAM leaves no room for a 640×480×4-plane shadow (150KB). Machines
with more memory can afford one, and get flicker-free updates: everything
drawn inside one gfx_lock/gfx_unlock burst appears on screen at once.
Module prefix `bb_`; file included right after `vga12.inc`. The same code is
also the kernel's 1bpp driver — on a Hercules or CGA card it renders straight
to the framebuffer and nothing below applies (§39.3/§39.5).

**Probe — `bb_init`** (from kmain, after `mem_init`): on a mono adapter it
returns at once, leaving `[bb_avail]` 0 (§39.5); on a colour adapter it sets
`[bb_avail]` = 1 and that is all it does — it neither arms the buffer nor
touches the planes, and it asks int 12h nothing. `[bb_avail]` now means
**"this adapter has planes to double"**, a property of the card and not of
the machine's size; whether the memory is there is a live question the claim
heap answers (`bb_canfit`, below), because it can change while the machine
runs. The old `DB_MIN_KB` (500) floor was a boot-time verdict derived from
int 12h and is retired with it.

**Double buffering is OFF at boot and switched at runtime.** On VGA `[bb_on]`
and `[bb_dbl]` start 0, so a fresh boot runs exactly the pre-§32 direct-VRAM
code and **nothing else in this section applies** until the user turns it on
from the Control Panel's Display page (§31.3), which calls `bb_set`.
`bb_canfit` gates that — `[bb_avail]` set *and* the claim heap holding a free
run of `BB_KB` (§50) — and the page greys the row and says "Not Enough Ram"
instead of offering a switch that would refuse. All three
bytes are initialized data (`db`, next to `gfx_lock_flag`), **not** .bss — nothing
zeroes .bss at boot.

**Switching — `bb_set`** (AL = 0 off / 1 on; caller HOLDS the gfx lock).
Turning ON **claims `BB_KB` off the heap** (§50, owner `MEM_K_BB`) and stores
the base in `[bb_seg]`; a refusal leaves everything as it was. Turning OFF
frees it again — 150KB back to the heap the moment the user stops wanting it,
which is the whole reason it is a claim and not a constant. Turning ON then
calls `bb_sync`: per plane, Graphics Controller Read Map
Select (GC4) = that plane, then a straight 0x9600-byte copy from `VGA_SEG`
to the plane segment at identical offsets, leaving GC4 back at its default
(§1 rule 7). Seeding is not optional — the buffer has never been written
while disabled, and after a disable it is arbitrarily stale, so the first
flush would otherwise push dead pixels over live ones. The cursor must be
hidden for it (it is — the lock is held), or it would be captured into the
buffer and smeared by that same flush. `bb_set` then resets the dirty rect,
points `[vid_rseg]`/`[vid_rend]` at the claim,
arms `[bb_dbl]`, and publishes `[bb_on]` = 1 **last**, since every
drawing entry dispatches on it. Turning OFF calls `gfx_flush` first, so
nothing drawn under the old mode is stranded in RAM, then clears both and
releases the claim. Both directions no-op when already in that state, so a
repeated click cannot re-copy 150KB.

**A package may switch it too — `osapi_gfx_dbuf`** (slot 0x01F0, AL = 1 on /
0 off, lock held). Out CF=0 and **AL = the state before**, which the caller
hands straight back when it is done so the user's own Control Panel setting
survives an app that borrowed the buffer for one flicker-free frame. CF=1
means it did not happen, and there are two reasons: `[bb_avail]` clear, or
`bb_set`'s claim refused (§50.2 — a heap that cannot fund `BB_KB` right now).

The `[bb_avail]` gate covers **both** directions, which is not symmetry for
its own sake: `bb_set`'s AL=0 path keys on `[bb_on]`, and a mono adapter
holds that at 1 permanently because it *is* the renderer (§39.5). An
ungated disarm from a package would therefore turn off the only drawing path
Hercules and CGA have. Refusal is a normal answer here, like every other
claim in §50 — the app draws unbuffered.

**`bb_sync` reads `[bb_seg]` BEFORE it loads DS = `VGA_SEG`.** With the base
a constant this could not be got wrong; with it a variable in the kernel's
own segment, reading it after the DS load fetches framebuffer bytes as a
segment number and the screen fills with bands. It cost one debug cycle
already.

**Back buffer layout.** Plane p lives at segment `[bb_seg] + p*BB_PLANE_PARA`
(0x960 paragraphs = 0x9600 bytes apart), offsets 0..0x95FF, 80-byte rows —
byte-for-byte the same geometry as one VRAM plane, so `vga_rect_setup`'s
offsets work unchanged in both worlds. The base is wherever the heap put it,
which is the only thing about this layout that is not fixed.

**Rendering.** RAM has no latches, no Set/Reset, no write modes — the
`bb_*` twins do in software, per plane, what the VGA ALU did in hardware
(on a mono adapter there is one plane and `bb_ink` reduces the colour first,
§39.3/§39.4):

- solid ops (`bb_pixel/hline/vline/fill`): plane byte value from
  `[gfx_color]`'s plane bit — set bits with `or dest, mask`, clear with
  `and dest, ~mask`; interiors of fills are `rep stosw` of 0xFFFF/0x0000.
- `bb_fill_gray`: per row `and dest, ~mask` + `or dest, pat&mask` at the
  edges, `rep stosw` pattern in the interior; 0xAA/0x55 alternating by row
  parity, identical in all four planes (color 15/0 per pixel bit).
- `bb_fill_pat`: same edge/interior scheme with the row byte fetched from
  `[gfx_pat]`'s 8-byte table by `y&7` (§5), identical in all four planes;
  the byte is uniform across a row, so interiors are the same `rep stosw`
  as the other fills.

Interiors go by word, not byte: the pattern is uniform across a row in both
solid and dither modes, so AL=AH and one `rep stosw` (plus a `stosb` tail on
an odd width, selected by the `shr cx, 1` carry that the string op leaves
alone) replaces two `stosb`. Free on an 8088's 8-bit bus, half the bus
cycles on any 16-bit part.
- XOR ops (`bb_xor_rect/xor_fill` internals): `xor dest, mask` at edges,
  `not dest` for full interior bytes, all four planes — self-inverting
  exactly like the hardware XOR path.
- `bb_save`/`bb_restore`: plain rect copies between the planes and the
  caller's buffer, same layout and rounding as `gfx_save`/`gfx_restore`
  (§5) — the §5 size formula budgets both paths, over-generously on mono,
  where there is one plane to copy instead of four.
- `font_char` (§6) and `ico_core` (§25) branch after their clip/shift
  setup to plane-loop twins using the same shifted masks.

All bb_* routines run with the gfx lock held (they share vga12's rect
scratch and the dirty rect) and preserve registers per §1 rule 3. They
touch no VGA register (§1 rule 7).

**Dirty rect.** Words `bb_dx1/bb_dx2` (byte columns, x/8) and
`bb_dy1/bb_dy2` (rows), a single bounding box unioned by every bb_* draw
after clipping; empty is encoded as dx1 > dx2 (reset: 0x7FFF/0/0x7FFF/0).
Byte-column granularity is deliberate — the flush copies whole bytes
anyway, and it spares the union any pixel↔byte conversions.

**Clipping and the dirty rect (§11.3).** A clipped primitive re-enters its
own body once per surviving fragment, so it produces several `bb_*` calls
where it used to produce one. That is correct by construction and needed no
change here: the union only ever widens, and the only resets are `bb_set`
turning the buffer on and the tail of `gfx_flush`. N sub-rects therefore
accumulate into one enclosing box and one flush pushes exactly that box.
The clip hook itself sits **above** the `[bb_on]` dispatch, at the public
entry, so the buffered and direct paths clip identically and the mono
adapters — where that dispatch is the only path — get it for free (§39.5).

**Flush — `gfx_flush`.** Public, callable only with the gfx lock held
(cursor hidden). No-op when `[bb_dbl]` = 0 (single `cmp`/`je` — **not**
`[bb_on]`, which is 1 on mono with no buffer behind it, §39.5) or the rect
is empty. Otherwise: for each plane, Sequencer Map Mask (SEQ2) = that
plane's bit, copy the dirty rows (`rep movsw` + odd-byte tail) from the
plane segment to `VGA_SEG` at the same offsets, then Map Mask back to 0Fh
and reset the dirty rect. GC state is untouched (defaults hold outside the
primitives). Interrupts stay enabled — the tick may switch tasks mid-flush,
but any drawer blocks on the gfx lock and the mouse ISR defers the cursor
while the lock is held, so nobody else touches VRAM or the Map Mask.

**The monochrome fast path — `[bb_mono]`** (a plane-identity flag; **not**
`[vid_mono]`, the adapter kind, §39). The flush is the expensive half
of double buffering: back-buffer rendering is plain RAM, but the flush is
four passes of VRAM writes, and VRAM is the slow side on every target
(measured on QEMU: a full-screen 4-plane flush costs ~3.7× a one-plane one
and ~24× the RAM-side render of the same area).

`bb_mono` (initialized data, `db 1`, same reason as `bb_on`) records whether
all four planes hold identical bytes. That is true at boot — `bb_init` zeroes
them — and stays true for every pixel drawn in colour 0 or 15, which is the
entire System 1 UI: its greys are 0/15 dither, not a grey plane value. While
it holds, the flush sets Map Mask = **0Fh** and copies plane 0 *once*; the
Sequencer fans that byte out to all four planes. A quarter of the VRAM
writes, and — because the four planes now land in the same write — a pixel is
never briefly the wrong colour, which is what the plane-sequential copy could
otherwise show mid-flush on a large rect.

The single loop falls out of the existing one: `[bb_plane]` starts at 0Fh
instead of 1, and the `shl` + `test 0x10` that ends the four-plane loop trips
on the first iteration (0Fh << 1 = 1Eh).

`bb_mono_chk` retires the flag when `[gfx_color]` is neither 0 nor 15. It is
called from exactly two places — `gfx_fill` and `font_char` — because those
are the only paths a third colour can reach the screen by: gray dithers 0/15, XOR flips every plane alike, icons are
white-under/black-over, and save/restore only moves plane bytes that were
already there. Retirement is one-way and needs no repair pass: all four
planes are always fully rendered, so the flush simply reverts to four passes.
A Minesweeper digit (§20) is what trips it in practice.

Both call sites sit on the **mode-independent** path, ahead of the `[bb_on]`
dispatch, so the flag tracks colour even while double buffering is off. That
is required, not incidental: `bb_set` can arm the buffer at any moment and
seeds it from VRAM, and those planes are only identical if nothing but 0/15
was ever drawn — including everything drawn while the buffer was disabled.

**Flush points.** `gfx_unlock` flushes before `cursor_show` (§7) — that
covers every ordinary lock/draw/unlock burst, including `wm_paint_all` and
the boot paint. Nothing else flushes: the two interactions that draw *while
holding* the lock — `menu_track` (§12) and the ui_drag outline (§13) — draw
their feedback **VRAM-direct** instead (see below), so the back buffer stays
clean through both and there is nothing to push. `menu_track` keeps one
flush, for the pull-down itself, which is real back-buffer content.

**Transient overlays bypass the back buffer — and the clip region.** Rule 2
of §11.3 is the same statement one level up: these bodies are entered below
both dispatches, and `gfx_unlock` clears the clip, so a background task's
region can never survive into the UI task's drag or menu hold.
The drag outline and the two
menu highlights are XOR overlays: drawn, then erased, never meant to persist
— the cursor's contract, not a window's. They call `vga_xor_rect_vram` /
`vga_xor_fill_vram`, the VRAM bodies of `gfx_xor_rect` / `gfx_xor_fill` under
their own names (§5), exactly as the cursor calls `vga_save_vram`. The
public `gfx_xor_*` entries keep dispatching to the back buffer, because
packages reach them through the API table (§20.3) and their XOR output *is*
persistent content.

This is a throughput fix, not a tidiness one. Routed through the back buffer,
a 1px outline dirties the **whole window rect** — `bb_xor_rect` unions four
strips — so each drag pass flushed the entire window area, twice (once for
the draw, once inside the `gfx_unlock` after the erase), for ~1000 changed
pixels. Correctness rests on the drag loop's existing discipline: the outline
is XOR-erased before the lock ever drops, so whenever another task can draw,
VRAM still equals the back buffer. The menu highlights rely on the same
property, plus the teardown order — the save-under restore repaints the
pull-down area in the back buffer and the caller's `gfx_unlock` flushes it,
which is what actually clears the item highlight from VRAM; the title
highlight sits above `MBAR_H`, outside that rect, so its VRAM-direct
un-highlight survives the flush.

**The cursor is not double-buffered.** The ISR draws it into VRAM over
flushed content; its save-under reads VRAM through `vga_save_vram` /
`vga_restore_vram` (the VRAM bodies of gfx_save/gfx_restore, §5). Invariant:
while the gfx lock is free, VRAM = back buffer + cursor; while it is held,
the cursor is hidden and flushes may run at any point. The back buffer
never contains cursor pixels, so no flush can smear or erase a live cursor.

**Accounting.** The Task Manager bills the back buffer as 150KB of System
memory (§28) whenever `[bb_dbl]` is set — never on mono, which allocates
none (§39.5) — so the RAM line and the System row
both move the moment the Display page switches it (39K ↔ 189K on a 639K
QEMU). The §16 test flow boots direct-to-screen like every machine; turning
the buffer on is a deliberate act, and `make xt` (256K) cannot do it at all.

## 33. Far code — retired

There used to be a `.fartext` section and a `kernel/farcall.inc` to serve it.
Cold modules — `ctrl.inc`, `taskmgr.inc` and one routine of `snd.inc` — put
their **code** there; it was assembled at `vstart=0`, shipped at the tail of
the kernel image, and copied down to its own segment below the kernel by
`far_init` as kmain's first act. The point was that those 5,455 bytes did not
count against the kernel's 64KB *window*.

**It is gone, and the arithmetic is why.** The mechanism needed a 10,752-byte
reservation in low memory to hold that 5,455-byte blob, so from the moment
§2's budget made the kernel's whole *footprint* the number being steered by,
far code was spending 5,297 bytes to save nothing. Merging it back also
deleted the shims: three `FARSHIM` stubs, twenty-seven `FARK` wrappers,
`far_init` itself, and two bytes on each of the 91 `KCALL` sites — which is
why the image grew by less than the blob it absorbed.

What this means for anyone adding a module: **there is nowhere to put code
that is "too cold to be worth the space".** Cold code is ordinary code, near
called like everything else. If the image has to shrink it shrinks by doing
less or doing it smaller, not by moving it somewhere the accounting cannot
see. `docs/KERNEL-MEMORY.md` is where that budget is kept.

## 34. snd.inc — the sound layer

**One RESIDENT sink: the PC speaker.** PIT channel 2 and port 61h, driving a
square-wave tone tier and a CPU-paced PCM clip tier, and a router that says
who owns them — that is what every machine this OS boots on actually has, so
that is what the kernel carries.

Everything beyond it is a **loadable driver** (§51). `SOUND.DRV` on the
system disk fills the two API slots the kernel holds empty without it —
`OSAPI_SND_FM` (0x00F8) and `OSAPI_SND_STREAM` (0x0100) — and may take the
tone tier with it. A machine with an AdLib in it and the driver loaded has
FM; a 128KB machine with neither card pays nothing for the code that would
have driven them. That split is the point: the card tiers used to be
resident, which cost every machine their bytes whether or not the card was
there.

Four things reach the driver, and the kernel decides all four:

| | |
|---|---|
| `osapi_snd_caps` | ORs in `DSV_CAPS`, and reports the live tone route in BL |
| `osapi_snd_fm` / `osapi_snd_stream` | dispatch to `DSV_FM` / `DSV_STREAM`, CF=1 with no driver |
| `snd_tone_out` | the tone tier's sink: `DSV_TONE` if one is published, else `spk_tone` |
| `snd_tick`, `snd_release_inst` | give the driver `DSV_TICK` and `DSV_RELINST` |

**The requesting instance is stamped by the kernel, not the driver.**
`snd_req_inst` reads `[snd_inst]` and the running task's `T_INST`, both
kernel state, so DH crosses the segment boundary as an argument (§34.3's
grant-stamping rule) and a driver never needs an API slot to ask who is
calling.

What the removal bought, and why it is recorded here rather than in a
changelog: `SND_SEG` — 64KB of conventional memory at linear
0x30000–0x3FFFF, claimed whole from boot for a DMA double buffer, a record
ring and a staging pool — is **gone**, and with it the largest single
reservation in the memory map (§2). On the 256KB floor machine that is a
quarter of the RAM handed back. The speaker tiers need no kernel buffer at
all: a tone is two `out`s, and `osapi_snd_play` paces samples out of the
**caller's** `ES:SI`. A future package that plays a WAV over the speaker
therefore needs nothing new from the kernel — it holds its own samples,
claims its own memory (§50) and calls slot 0x00F0.

Module (§4): `snd.inc`, prefix `snd_` (the speaker driver's own routines
are `spk_`). `%include`d after `ctrl.inc`; the file ends on `section .text`
(§1 rule 4). Its only state is a handful of `.bss` bytes plus the 256-byte
PWM rescale table.

**No software mixing, and now nothing to mix**: one owner per tier, and
both tiers contend for the same channel 2 (§34.1). Simultaneity is not
available on a PC speaker and the contracts say so rather than pretending.

The API surface is five §20.3 slots — CAPS, TONE, PLAY resident, FM and
STREAM live only while a driver is. Both answer CF=1 with no driver loaded,
which is exactly what the held cells they replaced did, so a package may call
them unconditionally and read the refusal.

### 34.8 The sound tier

`[snd_route]` — a `.text` byte, because `snd_tone_out` reads it on the
machine's first beep and `SYSTEM.CFG` may not exist at all:

```
SND_RT_AUTO (0)  unset: the best tier that actually answered at boot
SND_RT_SPK  (1)  the PC speaker, whatever else is loaded
SND_RT_FM   (2)  AdLib: FM only - no streams, no 12KB
SND_RT_SB   (3)  Sound Blaster: FM and streams both
```

**Three tiers, not two, and the middle one is the point.** An AdLib is an OPL2
and nothing else, and every Sound Blaster carries an OPL2 — so the tiers are
not three devices to pick between, they are a **subset relationship the user
picks a point on**. An SB owner who selects AdLib is not giving their card up;
they are giving up the 12KB DMA buffer, the IRQ and the refill worker that only
the stream tier needs. On a 128KB machine that is 12KB of a 55KB heap back for
a tier they were not using, which is the whole reason the rung exists. It is a
**choice, not a probe result**, which is why it persists in `SYSTEM.CFG`.

`SND_RT_CARD` remains as an alias for `SND_RT_FM`, so a `SYSTEM.CFG` written
before the split still reads as "a card" — and reads as the *cheap* card, which
is the safe way to be wrong.

`snd_rt_card` is the single answer to "does a tone go to the driver right
now?", and the router, `osapi_snd_caps`'s BL and the Control Panel's Sound
page (§31.7) all read it — so they cannot disagree about where a beep is
about to come out.

**A tier below `SND_RT_SB` with no driver falls back rather than going
silent.** The page greys that selection while it is meaningless, but a setting
kept on a disk that later moves to a machine with a card must not take the beep
away with it in the meantime.

**`DRVV_TIER` is how the setting reaches the hardware** (§51.2). `drv_tier`
carries `AH` = the tier to the published driver, which answers `CF=0` and a
re-copied service table, or `CF=1` and a `DRVE_*` saying why not. Two rules
hold it up:

- **Turning a tier off cannot fail**, for the same reason `DRVV_DETACH` cannot.
  The sound driver's off-leg halts the DSP, waits the worker out, unhooks the
  vector and frees both claims.
- **Turning one on is a claim and so can fail**, and there is nothing to
  pre-check with, so the answer is to *try*. The Control Panel therefore
  passes a **candidate** tier and stores `[snd_route]` only on `CF=0` — which
  is why `drv_tier` takes `AH` instead of reading the byte itself. A rollback
  would otherwise have to run on the path that already failed.

**`drv_tier` saves every register, `AH` included.** `drv_call` is a far call
into code the kernel did not write, and its callers are mid-something: the
Control Panel holds `DI`/`BP` as its pane origin and redraws with them after
the call. With `DI` eaten by the driver's DMA claim the redraw went to an
off-screen x and the page simply did not change — which reads exactly like a
click that never arrived. `drv_load` and `drv_unload` save the same registers
for the same reason; the Drivers page calls both the same way.

**`DSV_NAME` names the CARD, and the Sound Blaster takes it whenever it
attaches.** It used to go to whichever tier published first, which was always
the OPL2 — so on an SB1.0, an SB1.5, an SB2.0 and an SB16 alike it read
`AdLib`. Nothing in the kernel paints it now (§31.7), and it is published as a
pointer rather than staged, but the rule stands for whatever reads it next: the
name should describe the card, not the half of it that answered first.

### 34.9 The two tiers that must never overlap

An exclusive speaker clip (§34.4) raises `sch_lock` for its whole duration,
which would freeze a loaded driver's refill worker and force its stream
through the underrun path; and a stream's block IRQ arriving mid-clip is a
DMA transfer the clip's busy loop cannot pace around. So a `PCM_EXCL` clip
and a `PCM_BG` stream may never be live at once.

**Only the kernel can enforce it**, because the clip's state is `snd.inc`'s
and the stream's is the driver's, and neither module can read the other's.
`snd_str_busy` is the question and the answer in one call — stream **verb 8**,
which is not in the package SDK: DL says what the kernel is about to do, AX
comes back saying whether a stream is open. `osapi_snd_play` asks before its
grant window opens (so the refusal owes no `popf`) and tells the driver again
when the clip ends.

**The stream slot's stub is verb-aware**, and has to be. BX carries the
*caller's segment* for verbs 5 and 6 — the driver runs with ES = KERNEL_SEG,
so BX is the only way it can reach the caller's buffer — but on verbs 0 and 4
BX is unused and the **rate** needs somewhere to live, because `snd_req_inst`
writes DH and DH is the top half of a 4,000..44,100 Hz rate. The kernel is
what stamps, so the kernel is what banks DX.

With no driver loaded there is no stream to collide with, and `drv_svc_call`
refuses — which is the same answer, arrived at for free.

### 34.1 Port ownership

- **PIT channel 2 + port 61h — one owner, one mode at a time.**
  `snd_ch2mode` (byte: 0 free / 1 tone / 2 PWM) is the state machine; tone
  and PWM cannot coexist and the router serialises them (a PWM start
  steals + silences a lower-priority tone; a tone during a clip is refused
  CF=1). Every 61h access is read-modify-write inside `pushf`/`cli` …
  `popf` — the upper bits belong to the XT's keyboard/parity logic.
  `snd_init` saves the boot state of 61h bits 0–1; `snd_unhook` restores
  it and leaves ch2 quiescent.
- **PIT channel 0 is never written.** Not re-rated, not re-moded, not
  "sped up and divided". The speaker PWM pacer *reads* the scheduler's
  clock by latching ch0 — the same non-destructive operation `sch_account`
  already performs — so §8.1's 65536 radix, the 0x8000 pending-IRQ
  threshold, `sched_init`'s seed, `sch_cycles` units and `sched_unhook`'s
  restore are all untouched by construction. **The ch0 latch-atomicity
  rule (binding):** every ch0 latch+read triple — that is, every
  `sch_account` call site (`sch_isr`, `task_cycles`, `task_debit`, the
  yield/exit save paths) and §34.4's PWM pacer poll — runs inside one
  `pushf`/`cli` … `popf` window. The 8254 ignores a second latch command
  while a latched value is unread, so an interleave would feed one reader
  a stale byte; the IF=0 window makes interleaving impossible in both
  directions. Every pre-§34 site already complies (each wraps its latch at
  IF=0 today) — the rule codifies current practice so the invariant stays
  auditable instead of folklore.
  **Rejected, recorded so it is never re-litigated:** re-rating ch0 for
  sample pacing (with a 1-in-N BIOS chain divider) breaks §8.1's radix,
  the floppy motor countdown and every tick-denominated constant — and
  even fixed, a minimal per-sample ISR eats 36–50% of the floor CPU and
  still jitters at tick scale behind the mouse ISR. Interrupt-paced
  speaker PCM is the same arithmetic and is rejected with it: speaker PCM
  is the §34.4 busy loop or nothing.
### 34.2 Capabilities and the speaker driver

There is no driver table any more — a table with one row is a lie about
how much choice there is. `osapi_snd_caps` answers a **constant**:

```
AX = SND_CAP_TONE | SND_CAP_PCM_EXCL   (01h | 08h)
BL = 0        ; the tone sink is the speaker, and only the speaker
DX = 1        ; bit 0: the speaker is present. It always is.
```

The two capability bits that survive are the two things a PC speaker can
do. `SND_CAP_FM`, `SND_CAP_PCM_BG` and `SND_CAP_PCM_IN` are retired along
with the hardware that provided them; a package must not test for them.
`BL` and `DX` are kept in the contract (rather than dropped) so the answer
stays a superset of what the old ABI returned — a package that read
"BL ≠ 0 means FM" still reads "no FM".

**The speaker ops**, both in `snd.inc`:

- `spk_tone` — in AX = Hz (19..20000, 0 = off), DL = voice (0 only); out
  CF = 1 on a bad voice or frequency. On: divisor = 1193182/AX, then the
  mode-3 quad (0B6h → 43h, divisor lo/hi → 42h, 61h |= 03h) under one
  `pushf`/`cli` … `popf`. Off: 61h &= 0FCh in the same kind of window.
- `spk_pcm_op` — the PCM_EXCL clip engine of §34.4.

Both are called directly by the router. There is no indirection to
dispatch through, no presence flag to consult and no probe at boot: the
speaker is not a device that can be absent.

### 34.3 Router — ownership, priority, generations

- **Tone tier**: one logical channel, single owner. Owner record =
  {instance byte, priority byte, generation byte, expiry ticks}. Steal
  policy: a new request with priority ≥ the current owner's takes the
  channel (kernel UI beeps use priority 0C0h; the package default is
  040h); lower priority is refused CF=1. Tone-off (AX = 0) obeys the same
  compare, so background audio cannot silence an alert. Duration-limited
  tones (CX ≠ 0) self-expire via `snd_tick` — no task needed — and the
  expiry is **generation-guarded**: the tick silences only if the owner
  generation still matches the one stamped at grant. The sink is the
  speaker; there is no other, so a grant can never fail to resolve one.
- **Grant atomicity (binding)**: every grant, steal and release updates
  its owner record (generation, priority, expiry) *and* its ports inside a
  **single** `pushf`/`cli` … `popf` window. `snd_tick` runs at IF=0
  between any two task-context instructions; without this rule a tick
  landing between the generation stamp and the expiry store sees
  new-generation-with-stale-expiry and can silence a just-granted tone.
  The generation guard is only sound because task-side writers are atomic
  w.r.t. the tick. The same rule covers the PWM steal path: `snd_ch2mode`,
  the generation stamp and the silencing are one unit.
- **Grant stamping is task-qualified (binding)**. Every grant records the
  instance that asked for it, and `snd_req_inst` is the single routine
  that answers who that is. A window callback is dispatched with
  `[snd_inst]` stamped, and the obvious reading — "a stamp is set, so use
  it" — is wrong the moment two tasks exist: a **worker that pre-empts
  that callback** is a different app entirely, and it would inherit the
  stamp. So the stamp carries **the task that wrote it** in its high byte
  and is honoured only while that task is the running one; every other
  caller falls through to its own `T_INST`. Without this a worker's tone
  is billed to, and released at the teardown of, whichever app happened to
  be dispatching when it played — and Arkanoid's worker calls
  `OSAPI_SND_TONE` while UI callbacks run, so it is reachable, not
  theoretical. Corollary: **no caller may open-code the fallback**;
  `osapi_snd_play` did, and a second copy of a rule is a second place for
  it to be wrong.
- **`snd_release_inst`** — in: AL = instance slot. Both grants a package
  can hold — tone ownership and a running clip — are stamped with the
  owner instance, and this routine force-releases them (the clip stops at
  its next emitted sample). Called from both §29 teardown paths (§29.4);
  a closed package can never leave a tone droning.
- **Notification is polling; there is no sound event type.** Rejected,
  recorded: an `EVT_SND` would enter a queue whose only consumer is the UI
  task, which discards everything but mouse events and actively drains the
  queue mid-drag; tasks must never pop the shared queue (a second popper
  steals mouse events). Sound events would be pure pressure against 16
  slots, silently eaten. Progress is observed through the STREAM status
  verb (§20.3), polled from callbacks; the kernel refill task is what acts
  on block completion.
- **Who mixes: nobody** (see the §34 intro). Same-tier contention is
  ownership + priority steal, nothing else.
- Kernel-internal: `snd_beep` — no arguments, priority 0C0h, a 3-tick
  880 Hz tone through the active tone route; for UI use (menu error,
  refused clicks).

**Every site that dispatches a package callback must stamp the instance.**
`snd_req_inst` falls back to the running task's `T_INST` when no callback is
stamped, and on the UI task that is `0xFF` — *no instance*. A grant taken
under that fallback is owned by nobody: the app's own **worker**, stamped
with the real instance, then fails the driver's owner compare on every
staging call, and `snd_release_inst` cannot free the grant at teardown
either. The failure is silent in both directions.

The sites are `W_ONKEY`, `W_ONCLICK`, the menu command handler, `W_PAINT`
and **the Standard File dialog's completion proc** (§38.6) — the last of
which did not stamp, which is what made Tracker play its ring's opening
pre-roll and then zeros, forever: it takes its module grant inside exactly
that callback. `W_ONSIZE` is the one dispatch that deliberately does not
stamp; it is a geometry negotiator and has no business granting sound.

### 34.4 Speaker PWM — exclusive clips (Phase 2)

**Modulator**: ch2 in **mode 0, lobyte-only** (90h → 43h once at clip
start), 61h bits 0–1 held high. One `out 42h, AL` per sample emits a low
pulse of AL PIT-cycles; the speaker cone integrates the pulse train; the
sample rate is the carrier. Mode-3-idle + gate low are restored at clip
end.

- **Rates**: the computed divisor N = 1193182/rate must land in
  **74..255** (mode-0 lobyte-only ⇒ N ≤ 255 ⇒ rate ≥ 4,679 Hz; N ≥ 74 ⇒
  rate ≤ 16,124 Hz). A rate whose N falls outside returns **err 2, never a
  silent clamp** — a 22 kHz clip "clamped" to N = 74 would play 27% slow
  and flat; refusal is honest, resampling is the caller's job. Default
  rate: N = 149 → 8,008 Hz carrier, depth log2(149) ≈ 7.2 bits — **a bound
  to be validated at the Phase 2 floor gate**: if the resync counter shows
  material drops at N = 149 on the XT (86Box), the default drops to
  ~6 kHz and this section records the measured figure. One more honesty
  line, pinned: above ~12 kHz the ±1-poll granularity (~23–27 µs) exceeds
  a third of the sample period, so quality is jitter-dominated below the
  nominal depth. The documented audible truth at the default: an 8 kHz
  carrier whine + telephone-grade audio — that *is* speaker PCM.
- **Rescale**: far code builds a 256-byte xlat table per clip —
  t[s] = 1 + s·(N−2)/255, ~8 ms — in `.bss` (DS-addressable for `xlat`).
  Non-destructive: the caller's clip buffer is never modified, so replay
  and re-rate work. (Rejected: per-sample `mul` scaling — the table saves
  ~70 cycles/sample.)
- **Pacing**: the clip loop reads the scheduler's ch0 (§34.1): latch
  (0 → 43h), read two bytes from 40h, negate → a free-running 16-bit phase
  incrementing at 1,193,182 Hz; deadlines advance by N per sample and the
  compare is wrap-safe signed subtraction; each poll is one atomic latch
  triple (~110 cycles all-in), IF=1 between polls. **The resync rule**: a
  sample late by more than one period resets the deadline to now and drops
  samples — `inc word [snd_pcm_resync]` — never burst catch-up writes, so
  a long interrupt becomes dropped samples, never a buzz. Jitter is ±1
  poll plus the surviving IF=0 stretches. `snd_pcm_emitted` /
  `snd_pcm_resync` are the debug counters the Phase 2 gate reads,
  surfaced on the §31.4 caption. Budget at N = 149: 4,772,727/8,008 ≈
  **596 CPU cycles/sample**; fixed work (lodsb + xlat + out + deadline +
  abort checks + loop, with 8088 fetch stalls) ≈ 120–135, plus typically
  3–4 polls → ~450–575 total — it fits with little to spare, which is
  precisely why this is a busy loop and not an ISR.
- **Abort — a click always skips the clip.** At clip start the loop
  latches `snd_btn0 = [mouse_btn]`; on each emitted sample it first
  **folds releases into the baseline** (`mov al, [mouse_btn]` /
  `and [snd_btn0], al`), then a bit set now but clear in the baseline
  aborts with err 5. The fold is load-bearing: clips start from W_ONCLICK
  handlers — dispatched on EVT_MDOWN with the button *still held* — so the
  baseline starts with bit 0 set; the release retires it and the next
  press differs from the baseline and aborts. (Without the fold, no left
  click could ever abort a click-launched clip.) `snd_abort` — set by
  `snd_stop` and `snd_release_inst` — is checked in the same window. On
  click-abort the kernel **drains the aborting EVT_MDOWN (and its EVT_MUP)
  from the event queue** before returning, so the skip gesture cannot fire
  a menu, close box or icon under the cursor. The guarantee is structural,
  not a property of the caller's identity: `osapi_snd_play` holds its own
  raise of `sch_lock` from before the driver op runs until the drain
  completes, so a tick landing between the op's internal release and the
  drain can never switch to a task that would pop the aborting click
  first. No code is added to `mou_isr` — the checks are byte loads on the
  emit path; the mouse module stays sound-free.
- **Scheduling contract**: `spk_pcm_run` executes on the **caller's task**
  with IF=1 throughout, wrapped in `inc byte [sch_lock]` …
  `dec byte [sch_lock]` — the second sanctioned raiser (§7), with exactly
  `disk_read`'s meaning (§18) and not a new one: ticks advance, the BIOS
  chain feeds the floppy motor, `sch_account` runs, sleepers mark ready;
  only involuntary switching pauses. The mouse ISR keeps `mouse_x/y/btn`
  fresh throughout — but the cursor only *moves* if the caller does not
  hold the gfx lock; from window callbacks (which run under `gfx_lock`,
  the normal trigger) it is frozen for the clip, which also shrinks the
  mouse ISR's worst IF=0 stretch to packet decode. Cap: CX ≤ 65535 samples
  (≈ 8 s at 8 kHz); callers should chunk at ≤ 2 s, and the click-abort
  bounds the user's worst case regardless. The desktop freezes for the
  clip; that is disclosed in the API (`PCM_EXCL`), in the §31.4 caption,
  and gated by the user policy byte `snd_excl_ok` (default on,
  CP-flippable) — `osapi_snd_play` returns err 3 while it is off.

### 34.5 Sound Blaster — back, as a driver

Retired once, and no longer: `drivers/sound/sb.inc` is the DSP scan, the
stream verbs, the auto-init/single-cycle strategy gate and the deferred IRQ
discovery, all of it inside `SOUND.DRV` rather than the kernel (§51.4). What
changed in the move is where its memory comes from — a **heap claim** taken at
attach (`sbl_dma_map`, `OSAPI_MEM_CLAIM_DMA`), not a pinned segment, which is
why the 64KB DMA-page rule had to become something the allocator answers
(§50.3) rather than a property the address had by construction.

The stream verbs reach it through `osapi_snd_stream` (slot `0x0100`), which is
the one slot where **DX is an input on some verbs and an output on another**:
the rate on 0 and 4, the consumed count on 3. The stub banks it for the
former and must not for the latter — getting that wrong made every poller
believe a finished stream was still playing.

### 34.6 Recording and staging — back, as a driver

Went with §34.5 and came back with it. Verb 7 grants out of the driver's
staging pool, verb 6 stages into a grant and verb 5 reads back out of one,
and verb 4 opens an input stream that the drain task fills.

**The pool is the driver's, so its size is not a constant an app may assume.**
The Sound Blaster's is 20,480 bytes.
An app that needs a big grant asks in TIERS and records into what it got;
`apps/recorder` is the reference for that, and the reason it works on a machine
whose driver claimed less than it hoped for.

**And it is not held when nobody is using it.** The pool is a **separate
claim** from the DMA buffer, taken by `sbl_pool_get` on the first grant and
released by `sbl_pool_put` with the last — including the release a dying
instance triggers, and every refusal path, so a grant that fails after the pool
was claimed cannot strand it. The two are as different as claims get, which is
the whole reason to split them:

| | DMA buffer | staging pool |
|---|---|---|
| size | 12KB | 20KB |
| who addresses it | the **8237** | us, with `rep movsb` |
| 64KB page rule | **binding** | none — it may straddle freely |
| when claimed | attach, on an empty heap where a page-safe base is easy | first grant, and it may honestly refuse (error 8) |
| lifetime | the driver's | the grants' |

Holding both from boot cost a machine 32KB for a card nobody was playing —
**58% of a 128KB machine's heap**. Measured after the split: a live grant shows
43KB of heap claimed, freeing it shows 23KB.

Two consequences worth stating. **Grant offsets are relative to the pool's own
base**, so they start at 0 rather than above the DMA buffer — invisible to
packages, which receive an opaque offset from verb 7 and hand it back, never
forming an address themselves. And the refill and drain copies now **cross two
segments**: `DS = [sbl_poolseg]` with `ES = [sbl_seg]` feeding the card, the
reverse capturing. Each was one intra-segment `rep movsb` before, and the
`push es / pop ds` idiom that expressed that is what a reader will expect to
find and must not.

**A verb on a machine with no driver answers `AX = 4`, not `AX = 0`.**
`drv_svc_call`'s generic refusal returns 0 with CF=1, which is right for the
FM and tone slots — their contract is CF alone — and exactly wrong here,
because `AX = 0` is how every stream verb says OK. The slot tests
`DSV_STREAM` itself and answers in the verbs' own vocabulary.

### 34.7 State, boot gate, teardown

- **The `snd_live` boot-gate rule (binding).** `sched_init` hooks int 08h
  as kmain's second act; `snd_init` runs seconds of ticks later, and
  nothing clears `.bss` at boot (§8). So `snd_tick`'s first instruction
  tests `snd_live` — a `db 0` in initialised `.text` data (the
  `osapi_seed` idiom of §15), set to 1 as `snd_init`'s **last** act
  (publish-last) — and returns while it is clear. **Every byte `snd_tick`
  can read must have a defined value from the instant int 08h is hooked**:
  either initialised `.text` data, or behind the `snd_live` gate.
- **Initialised `.text` data**: `snd_live`, `snd_excl_ok` (default 1) and
  the speaker's name string. That is the whole of it.
- **`.bss`** (~20 bytes of state + the 256-byte xlat table): the tone
  owner record, generations, the expiry deadline, `snd_ch2mode`, the saved
  61h boot bits, `snd_btn0`/`snd_abort`, the clip's owner and divisor, and
  the debug counters `snd_pcm_emitted` / `snd_pcm_resync`. All stored
  explicitly by `snd_init` (§8's rule) and unreadable by the tick until
  `snd_live` publishes.
- **Buffers: none.** The layer owns no memory outside its own `.bss` —
  `osapi_snd_play` reads the caller's `ES:SI` in place. This is the whole
  reason `SND_SEG` could be deleted from §2 rather than merely shrunk.
- **Boot**: `snd_init` joins kmain's §15 sequence **after `tm_init`**:
  save the 61h boot bits, store the `.bss` state, set `snd_live` last.
  There is nothing to probe. Boot stays clean — no instances, no tasks,
  no sound.
- **Teardown**: `sched_unhook` calls `snd_unhook` beside `mouse_unhook`
  (§8) — 61h bits 0–1 back to the saved boot state, ch2 quiescent. The
  speaker owns no vector, no IRQ and no DMA channel, so that is all of it.
- **Sections** (§33): everything `snd_tick` can reach stays in `.text` —
  the owner record, the tone core, `snd_beep`, `snd_tick`, the router, the
  three API slot targets, `snd_release_inst`, `snd_unhook` and
  `spk_pcm_run`. Cold, but near like everything else (§33): the PWM builder
  alone. Far code keeps DS = KERNEL_SEG, so it reads its data from `.text`
  (§33 rule 2).

## 35. Recorder — the sound layer's recording client

`apps/recorder` needs `SND_CAP_PCM_IN` (a Sound Blaster) to record and
`PCM_BG` streams to play back; both went with the sound cards when those were
removed, and both came back as `SOUND.DRV` (§51.4), so the package came back
with them. One window: a waveform strip, two status lines and four buttons
(REC / STOP / PLAY / DEMO), each also an item in a **Sound** menu (§12.2)
calling the very routine its button calls — so a command picked in the wrong
state is refused with the same status line a click on the greyed button would
have written.

Three things about it are worth knowing:

- **It sizes its grant in tiers** (§34.6): 5 s, 3, 2, 1, keeping the first the
  driver will part with, because the staging pool is the driver's and 40,000
  bytes is not always there. The floor is the demo's own length, so the
  built-in sweep fits every tier and the app is never useless. Line 2 shows
  the capacity it actually got, because that is no longer the same number on
  every machine.
- **Progress is POLLED** (§34.3 — there are no sound events): every paint and
  click runs `rc_poll`, which reads verb 3 and retires finished or
  watchdog-stopped streams. On QEMU no input IRQ ever arrives, so a recording
  always lands on the watchdog path and says so honestly.
- **It stays useful with no card**: DEMO stages a built-in 400→800→400 Hz
  sweep and PLAY falls back to `OSAPI_SND_PLAY` speaker chunks. With no driver
  at all it says NO SOUND DRIVER rather than blaming memory, which is the
  distinction §34.6's `AX = 4` exists to make.

Teardown needs nothing from the app: `snd_release_inst` (§34.3) force-closes
any live stream and frees the grant.

## 36. Piano — the fifth package (apps/piano/piano.asm)

A colorful playable piano over the §34 tone tier: 1.5+ octaves of clickable
keys, a live note viewer (a scrolling mini-staff), a recorded sequence with
timed replay, and three embedded public-domain songs. Prefix `pn_`,
embedded piano-keys icon (flags bit 0). Directory order on the apps disks
stays pinned: mines, hello, notepad — **piano is appended
fifth** so the earlier indices hold. Drawing uses colors beyond 0/15,
which retires `[bb_mono]` (§32) — supported and expected; on a 1bpp adapter
they reduce to §39.4's three inks.

- Window: "Piano", 224×177 at (250,100) → content 222×158 — the authored
  frame, which `wm_fit` clamps onto the live screen (§39.7), so at 640x200
  the keyboard row is cut off at the dock (§39.9). Content layout, top to
  bottom (all coordinates content-relative):
  - **Note viewer** — a CBLUE 1px frame (2,2)–(219,49), white field, five
    black staff lines (treble: E4 G4 B4 D5 F5) at y 40/34/28/22/16,
    x 8..213. Up to **21 note columns**, 10 px apart at x = 8 + col·10;
    each played note is a 4×3 CBLUE dot centered at y = 46 − 3·step
    (step = diatonic degree from C4 = 0 … G5 = 11), with a black ledger
    line (colx+2..colx+11 at y 46) for middle C and a small CLRED
    hand-drawn sharp glyph (two 6px vlines at colx+1/colx+3, two 5px
    hlines at y−2/y+1) beside the dot for the five sharps — the one mark
    §39.4's 1bpp reduction loses, CLRED going white on a white field. The
    viewer
    shows the **last 21** entries of the first `[pn_vcnt]` sequence
    entries: live play and song-load set vcnt = count; replay resets
    vcnt to 0 and advances it note by note, so the viewer fills live
    during replay too.
  - **Button row** at y 52..67: REPLAY (x 2, w 56) and CLEAR (x 60,
    w 48), black 1px frames + centered labels; the message area
    (white-filled 110..219, text at 112) shows the state: READY /
    REPLAYING... / REPLAY DONE / STOPPED / CLEARED / SEQ FULL / EMPTY /
    the loaded song's name.
  - **Songs row** at y 70..85: the caption "SONGS:" at (2,74) and three
    colored buttons — TWINKLE (x 52, w 64, CGREEN), ODE (x 118, w 40,
    CMAGENTA), MARY (x 160, w 48, CRED). The embedded songs (titles,
    public domain): *Twinkle Twinkle Little Star* (42 notes), *Ode to
    Joy* (30 notes), *Mary Had a Little Lamb* (26 notes) — all within
    C4..G4. Loading REPLACES the sequence buffer with the song (viewer
    filled immediately, message = song name) so the one Replay button
    plays it.
  - **Keyboard** at y 88..155: 12 white keys 18 px wide (x = 2 + i·18,
    black shared 1px frames, white faces) covering C4 D4 E4 F4 G4 A4 B4
    C5 D5 E5 F5 G5, with 8 overlaid black keys (10 px wide, y 88..129,
    x = 18·slot + 15 for slot = the left white key's index: 0 1 3 4 5 7
    8 10) covering C#4 D#4 F#4 G#4 A#4 C#5 D#5 F#5. The QWERTY mapping
    is printed subtly on the keys themselves: CBLUE letters
    `a s d f g h j k l ; ' ]` at the foot of the white keys (y 144),
    CWHITE `w e t y u o p [` on the black keys (y 116). A sounding key
    fills **CLBLUE** (white keys, letter goes CWHITE) or **CLRED**
    (black keys) for the note's duration, then repaints normal; a white
    key repaint re-draws its overlapping black neighbours.
- **Notes**: equal temperament, one pinned word table `pn_freq`
  (Hz, note ids 0..19 = semitones from C4): 262 277 294 311 330 349 370
  392 415 440 466 494 523 554 587 622 659 698 740 784. Every note goes
  through `OSAPI_SND_TONE` (package priority 40h). Live play (click or
  key) sounds CX = 5 ticks and holds the key highlight ~4 ticks inside
  the callback — bounded blocking, the §34.4 PCM_EXCL precedent; a
  fresh press cuts the hold short so fast playing stays snappy (the
  tone still self-expires).
  **In-callback waiting is `pn_wait`, a `task_yield` loop against
  `osapi_get_ticks` (wrap-safe signed compare) — NOT `task_sleep`**,
  learned the hard way: from the UI task with no other task ready,
  §8's sch_switch nothing-ready fallback resumes the sleeper at once,
  so `task_sleep` from a callback on an otherwise-idle machine returns
  immediately. `menu_track`'s poll-under-the-lock is the sanctioned
  pattern and `pn_wait` is its package-side twin (it also carries the
  release-folded mouse baseline: out CF=1 on a fresh press).
- **Sequence**: parallel bss arrays, cap **300 notes** — `pn_seqn`
  (note id bytes) + `pn_seqt` (absolute `osapi_get_ticks` words stamped
  at play). Policy at the cap, pinned: **full-stop** — further notes
  still sound but are not recorded and the message reads SEQ FULL
  (oldest-drop rejected: silently rewriting a recording the user is
  building is worse than saying it is full). CLEAR empties it. Loading
  a song synthesizes timestamps from its duration column (t += dur).
- **Replay** is a blocking loop in the REPLAY click callback — exactly
  the PCM_EXCL precedent: for each note, advance vcnt + redraw the
  viewer, highlight the key, `OSAPI_SND_TONE` with CX = the note's
  duration, then `pn_wait` that duration, polling `osapi_mouse` every
  pass against the release-folded baseline (latched before the loop,
  the kernel's §34.4 fold) — a fresh press aborts: tone off, key
  repainted, vcnt = count, viewer redrawn, message STOPPED. The aborting press is NOT drained (only the kernel
  can, §34.4), so it also lands as an ordinary click — the gap-abort
  asymmetry that makes close-box- and key-click-during-replay work
  naturally. Durations are the timestamp
  deltas (last note: 6), **clamped to 2..24 ticks** — original relative
  timing, with long thinking pauses compressed to ~1.3 s.
- **Song table format**: byte pairs (note id, duration ticks),
  terminated 0FFh; quarter = 6 ticks, half = 12.
- Register contracts: entry stamps the READY message (bss is
  loader-zeroed) and wm_creates; paint/onkey/onclick preserve all
  registers, fetch the content origin per call, and run under the
  caller's gfx lock. No caps query, no grants, no streams, no tasks —
  the tone tier is always present; teardown mid-replay rides
  `snd_release_inst`'s tone release (§34.3).
- QEMU truth: the tone tier is PIT ch2 mode 3, which the wav audiodev
  captures (unlike mode-0 PWM), so sndcheck verifies real key
  frequencies; replayed songs play tone-to-tone with no speaker-off gap,
  so a whole song is ONE contiguous wav region — per-note assertions
  slice it at the 6-tick note quantum and Goertzel each slice.

## 37. clock.inc — the system clock

The wall clock: one date + time of day for the whole system, seeded from
the hardware RTC when the machine has one and advanced from the PIT
afterwards. Label prefix `clk_` (the built-in Clock **app** of §14 keeps
its own `app_clk_` names and its own per-instance stopwatch state — the two
are unrelated). Included right after `events.inc`; `clk_init` runs in kmain
right after `evq_init` (§15). All code is near `.text`: it is called from
the UI task's inner loop and from the Control
Panel's far page (§31.5).

**State (`.bss`), the single source of truth.** Broken-down time, because
every consumer wants fields and nothing wants epoch arithmetic:

```nasm
clk_sec  resb 1   ; 0..59      clk_day   resb 1   ; 1..31
clk_min  resb 1   ; 0..59      clk_mon   resb 1   ; 1..12
clk_hour resb 1   ; 0..23      clk_year  resw 1   ; 1980..2099
clk_last resw 1   ; last [ticks] sample      clk_rtc   resb 1  ; 1 = RTC found
clk_acc  resw 1   ; tenth-of-tick remainder  clk_dirty resb 1  ; write RTC
clk_h12  resb 1   ; 0 = 24-hour (boot default), 1 = 12-hour + AM/PM
clk_secs resb 1   ; 0 = the bar hides seconds (boot default), 1 = shows them
clk_barq resb 1   ; sticky "the bar's text is stale" request
clk_sn_* : a second copy of the six fields, same order and adjacency
```

**The two display settings** are runtime state like every other Control
Panel setting (the scheduler mode, double buffering, the tone route): they
return to their defaults at boot. Since §18.4 the kernel *can* write the
data disk, so a settings file is now buildable — it is deliberately not
built: it would tie a machine's UI state to whichever floppy happens to sit
in B:, and the settings would silently change when the disk did. Persisting
them wants a home the kernel owns, which this OS does not yet have.
They change **display only**; the clock itself is
always kept as a 0..23 hour and a full seconds count, so toggling either
one loses nothing and the RTC is not rewritten.

`[clk_barq]` is how anything that changes what the bar *should* say asks
for a redraw without drawing: an edit (`clk_fld_adj`) or a settings toggle
sets it, and the next `clk_tick` folds it into the change mask and clears
it. Without it, an edit made while seconds are hidden would leave the bar
showing the old time for up to a minute.

**Ownership (binding).** Only the UI task **writes** — `clk_tick` from its
loop, `clk_fld_adj` from a Control Panel click, both on task 0. **Readers
are not so confined**: `menu_draw_bar` reaches `clk_fmt` from
`wm_paint_all`, which a *dying background task* runs inside `wm_destroy`
(§29.4) — and since §20.6 that dying task is sometimes a package's own
worker, tearing down from `inst_pkg_alive`. So a reader can be pre-empted
mid-format while the UI task carries a second. Two rules make that safe, and both are binding:

1. **Every carrying write is one critical section.** `clk_inc_sec` and
   `clk_fld_adj` mutate under `pushf`/`cli` … `popf` (§1), so the fields
   are only ever observed before or after a whole second (or a whole edit),
   never mid-carry. The guard sits inside `clk_inc_sec`, per second, rather
   than around `clk_tick`'s catch-up loop — an hour of held-open menu owes
   3600 seconds and must not hold interrupts off for all of them at once.
2. **Every reader formats from `clk_snap`**, which copies the six fields
   under the same guard (sec+min and hour+day as words — that is why they
   are laid out adjacent). `clk_fmt` snapshots itself; `clk_fld_str` reads
   the last snapshot *without* taking one, so `cp_time_rows` calls
   `clk_snap` once and its whole row is a single instant.

Nothing in an ISR touches any of it; the clock is derived from `[ticks]`,
which the PIT hook already owns (§8).

**Advancing.** `clk_tick` uses the §14 accumulator idiom verbatim, which is
what makes 18.2 Hz add up to real seconds: delta = `[ticks]` − `clk_last`
(wrap-safe subtraction), `clk_last` = `[ticks]`, `clk_acc` += delta×10 as a
32-bit value (a long lock starvation — a held-open menu — can owe more than
65535 tenths), then `DIV 182` gives whole seconds owed and the new
remainder. Each owed second is carried through sec → min → hour → day →
month → year, with month lengths from a 12-byte table and February 29 in a
leap year (`year AND 3 = 0`, exact over the 1980..2099 range the page
allows).

**The change mask (binding).** `clk_tick` returns **AL**, and ui_task's
step 4 (§13) does nothing at all when it is 0:

| bit | meaning |
|-----|----------|
| 0 | at least one second was added |
| 1 | the **menu bar's text** changed, and only then may the bar be redrawn |

Bit 1 is set when bit 0 is set **and** `[clk_secs]` is on; or the minute
value differs from what it was before the advance; or 60 or more seconds
were owed in one go (an hour of held-open menu can land on the same minute
value — comparing the minute alone would miss it); or `[clk_barq]` was
pending, which `clk_tick` then clears. Splitting the two bits is the whole
point of the seconds setting: with seconds hidden the bar's text is
unchanged 59 seconds out of 60, and ui_task must not take the gfx lock —
the cursor blink that comes with it is the flicker being removed.

**The hardware RTC.** `clk_init` stores the fallback date **first** —
**4 July 2026, 00:00:00** (`CLK_DEF_Y`/`CLK_DEF_M`/`CLK_DEF_D`) — so any
probe failure leaves a sane clock, then calls `clk_probe` and sets
`[clk_rtc]` only if it succeeds. `clk_probe` walks the four-rung ladder of
§37.90; `clk_rtc_write` dispatches on which rung answered and returns
immediately when `[clk_rtc]` is 0. It runs **only** from ui_task step 3,
outside the gfx lock, draining `[clk_dirty]` (§13) — never from a window
callback (§31.1). Only the last rung calls BIOS now, but the rule stays:
the caller cannot know which rung answered.

| symbol | contract |
|--------|-----------|
| `clk_init` | Boot: display settings to their defaults (24-hour, no seconds), fallback date, then the RTC probe. Preserves all registers. |
| `clk_tick` | UI task only. Advances the clock from the `[ticks]` delta. Out: AL = the change mask above. Clobbers AX only. |
| `clk_snap` | Copies the six fields to `clk_sn_*` under `pushf`/`cli`. Preserves all registers. Read by §31.5. |
| `clk_fmt` | Calls `clk_snap`, then formats the bar's line into `clk_str` in the live form: `'Mmm DD YYYY  HH:MM'`, plus `':SS'` if `[clk_secs]`, and in 12-hour mode the hour drawn 1..12 **without a leading zero** and a trailing `' AM'`/`' PM'`. 18..24 glyphs; `clk_str` is 26 bytes. Out: SI = `clk_str`. Preserves everything else. |
| `clk_fld_str` | In: AL = field 0..6 (month, day, year, hour, minute, second, meridiem). Out: SI = a NUL string for that field alone in `clk_fbuf` — `'Mmm'`, `'DD'`, `'YYYY'`, `'HH'`, `'MM'`, `'SS'`, `'AM'`/`'PM'`. Always the field's **full width, zero-padded** — unlike `clk_fmt`, because a field is a fixed-width editable cell whose highlight box must not change size under it; in 12-hour mode the hour reads `'12'`, `'01'`..`'11'`. Reads the last `clk_snap` and does **not** take one. Preserves everything else. Read by §31.5. |
| `clk_fld_adj` | In: AL = field 0..6, BL = +1 or −1. Steps that field with wrap (month 1..12, day 1..month length, year 1980..2099, hour 0..23, min/sec 0..59); field 6 flips the meridiem by ±12 hours, either sign. Then re-clamps the day to the new month length (31 Mar − 1 month = 28 Feb, never 31 Feb), zeroes `clk_acc` and re-samples `clk_last` so the new second starts from now, and sets `[clk_dirty]` + `[clk_barq]`. Preserves all registers. Read by §31.5. |

**The hour is always stored 0..23** and stepped 0..23 — 12-hour mode is a
rendering of it, not a second representation. So `+` on an hour showing
`11 AM` gives `12 PM`, which is what a clock does; and the meridiem field
is the only thing that jumps by 12.
| `clk_probe` | Boot, from `clk_init`. Walks the §37.90 ladder, seeds the fields from whatever answers and publishes `[clk_tier]`. Out: CF = 1 = no clock, nothing written. Preserves all registers. |
| `clk_rtc_write` | Pushes the live time back to the hardware RTC if `[clk_rtc]`, through the rung `[clk_tier]` names; no-op otherwise. Outside the gfx lock only. Preserves all registers. |
| `clk_dow` | The day of the week for the live date, Sakamoto's method. Out: AL = 0..6, 0 = Sunday. Preserves everything else. Both XT chips have a weekday counter they do not derive themselves. |
| `clk_bcd` / `clk_tobcd` | BCD ↔ binary byte helpers; `clk_bcd` returns CF = 1 on a non-decimal nibble. `clk_tobcd` clobbers AH. |

**Month names** are a 12×3 ASCII table (`'Jan'`…`'Dec'`), indexed by
month−1 ×3 — the same data serves `clk_fmt` and `clk_fld_str`.

**What this deliberately does not do.** No timezone, no DST (the DST byte
`AH=02h` returns is ignored and `AH=03h` is written 0), nothing *displays*
a day-of-week (`clk_dow` exists only because two of the chips have a
weekday counter that must be written), and no re-reading of the RTC after
boot — the PIT is the clock from then on, which is exactly how DOS behaves
on the same hardware.

### 37.90 The RTC ladder

`int 1Ah` AH=02h..05h is the **last** rung, not the only one. An XT BIOS
implements AH=00h/01h and nothing else, so on an IBM 5150 with an AST
SixPakPlus the BIOS knows nothing about a clock that is sitting right
there; and a BIOS that implements the two *read* functions may still `iret`
out of the two *write* ones, which is a clock you can read and never set.
So `clk_probe` walks four rungs and `clk_rtc_write` dispatches on
`[clk_tier]`.

| tier | `CLK_T_*` | chip | where | caption |
|------|-----------|------|-------|---------|
| 0 | `NONE` | — | — | `Hardware clock: none` |
| 1 | `AT`   | MC146818 / DS12885 | index 70h, data 71h | `Hardware clock: AT 70h` |
| 2 | `NS`   | National MM58167 | 2C0h, 32 direct ports | `Hardware clock: 58167 2C0h` |
| 3 | `RP`   | Ricoh RP5C01 / Toshiba TC8521 | 2C0h, 16 direct 4-bit ports | `Hardware clock: 5C01 2C0h` |
| 4 | `BIOS` | — | int 1Ah AH=02h..05h | `Hardware clock: BIOS` |

`[clk_rtc]` is still the yes/no everything else tests; `[clk_tier]` is the
diagnosis, and the Date/Time page prints it (§31.5) because on a machine
whose clock will not hold a setting, *which* rung answered is the whole
answer and there is nowhere else to see it.

**Probe order is the design**, and it is chosen so that no rung ever writes
to a chip a later rung might have identified differently:

1. **Rung 1, read-only.** Status Register A's divider bits must read 010 —
   the only pattern that runs the oscillator, and what every BIOS leaves
   there. That is the early-out, and it is structural rather than a test
   for 0FFh because an absent chip does not reliably float (see the bus
   note below). Register D's "bits 6..0 must be zero" is deliberately *not*
   used: two generations of chipset have since broken it. Presence is
   then the **UIP-stuck test**: Status Register A bit 7 is high for at most
   2.228 ms per second on a live chip and permanently on an absent one.
   Bounded by a plain counter (`CLK_UIP_MAX`), never by `[ticks]` —
   `clk_init` runs before the PIT hook has done anything (§15) — and the
   guard is taken **per access**, not around the poll, because 2.3 ms with
   interrupts off is forty tick periods. An AT-class machine stops here and
   never looks at 2C0h, which matters: 2B0h–2DFh is the alternate-EGA range.
2. **int 1Ah**, which is not yet a rung: it is the *reference* rung 3 is
   checked against.
3. **Rung 3**, claimed **only** when the digits at 2C0h decode to the same
   hour, day, month and year the BIOS just reported. That one test confirms
   the chip, the base, the direct (rather than latched) addressing mode and
   the MODE page at once, **with no writes**; seconds and minutes are left
   out because they can tick between the two reads. A machine whose BIOS
   cannot read the clock cannot pass it, which is exactly what keeps this
   rung off a SixPakPlus. A chip parked on any page but 0 is refused rather
   than moved — moving it is a write, and this runs before identification.
4. **Rung 2.** Two half-registers physically do not exist on an MM58167 —
   register 00h has no units digit and RAM 0Dh no high nibble — and that is
   what every driver since ASTCLOCK has recognised it by. The read-only
   halves run first and the counters must read as a date before either
   write happens; then 0AAh → 0Dh must read back 0Ah, and 0FFh → 08h must
   read back with its low nibble **clear**, which is the deterministic
   rejection of a 4-bit part at the same address (a TC8521's 08h is the
   10-day counter and stores 3). Both bytes are restored on the single path
   out of `clk_ns_half`, so no verdict can skip the restore.
5. **Rung 4**, for a machine whose BIOS is the only thing that knows where
   its clock is.

Both 2C0h probes drive all-ones onto the ISA bus first (`out 80h, 0FFh` —
the POST diagnostic port, an unused DMA page slot that every BIOS writes),
because an unclaimed IN returns the last byte *driven*, not 0FFh, and every
gate below is built on "an absent card reads 0FFh". Rung 1 cannot use that
trick — its own index write to 70h is the last thing on the bus — so it
rejects structurally instead: Register A's divider bits must read 010, which
neither 0FFh nor a held index byte can satisfy.

**Reading.** Rung 1 uses the hybrid guard Linux converged on: the payload
is bracketed **both** by UIP and by a seconds comparison, the seconds value
stashed *before* the first UIP test (an NMI landing just after it could
otherwise hide a whole update cycle) and both re-checked afterwards. Every
70h/71h pair runs with IF = 0 and every index write carries bit 7 (NMI
masked) — the index is global state shared with the BIOS, and this kernel
pre-empts every 55 ms (§7). `clk_at_done` ends each burst by parking the
index at 0Dh, read-only, with NMI back on. Register B's **DM** (0 = BCD)
and **24/12** (1 = 24-hour) are read and obeyed, never rewritten; a DM bit
that lies is a known failure mode, so a decode that fails is retried the
other way round. 12-hour mode's PM bit is stripped *before* BCD decoding
(1 PM is 81h, which is legal BCD for 81) and re-applied *after* BCD
encoding. The century comes from CMOS 32h only if it decodes to 19 or 20,
otherwise from a window over §37's own 1980..2099 range; `[clk_cent]` = 0
means this BIOS keeps no century byte and `clk_at_write` must not invent
one. Rung 2 brackets its read with the rollover status bit at 14h (read to
clear, read the fields, read again; set = discard), bounded by
`CLK_RETRY` because the loop is inside a critical section. Rung 3 has no
status bit at all, so its guard is the 1-second digit before and after.

**Writing.** Rung 1 asserts Register B's **SET** bit across the burst — the
once-a-second update cycle would otherwise land between two of the six
writes and run its carry logic against a half-written value — and restores
`save_control` whole. It deliberately does **not** touch Register A's
divider bits: the upside is sub-second phase this OS never displays and the
downside of getting it wrong is a stopped clock that no emulator here
models. Rung 2 has no halt bit of any kind, so it writes coarse to fine
with seconds last, and stamps the year into **both** conventions (0Ah, the
ASTCLOCK one, and 0Eh, what 86Box's cards read) plus GLaTICK's 222 magic at
0Bh and a last-month shadow at 09h — the chip has no year register, and the
shadow is what lets a machine switched off over New Year come back right.
Rung 3 stops the timer (MODE bit 3) across its writes, preserves
alarm-enable through all three MODE writes, and leaves TEST and RESET
alone: RESET is write-only, so its other bits cannot be read back, and two
of them *enable* pulse outputs when clear.

**Every loop is bounded.** The one way to hang on this hardware is to wait
forever for a bit that will never change on a machine where every read is
0FFh, and rungs 2 and 3 do their waiting with interrupts off.

**Testing.** QEMU models an MC146818 and nothing else, so rungs 2, 3 and 4
would be unreachable under the QMP harness. `RTC=none|at|ns|rp|bios` in the
Makefile forces one rung, exactly as `VIDEO=` forces the adapter (§39.1),
and shares its stamp-file invalidation. `RTC=ns` and `RTC=rp` on a machine
with nothing at 2C0h are the negative test: the probe must reject and boot
to the fallback date rather than hang or claim a phantom clock. Rungs 2 and
3 have no positive test outside 86Box (`isartc_type = a6pak`) and real
hardware.

## 38. fdlg.inc — the Standard File dialog

The kernel's file chooser: **one** window that lists a directory and hands
a chosen 8.3 name back to whichever application asked for it, in an **Open**
form and a **Save** form. Label prefix `fdlg_`.

It exists because §18.4 gave packages five file slots and no way to name a
file: Note Pad's F2/F3 wrote a hard-coded `NOTES.TXT` (§27.1) not because
one note is enough but because there was nothing to ask a filename with,
and the Disk window (§22) can launch a package but cannot return a name to
a caller. This is the missing half of the file API, and the kernel owns it
for the same reason the kernel owns the menu bar: it is the one piece of UI
every application needs to look identical.

### 38.1 It is not an application (binding)

The dialog is **not an instance** — no `KIND_*`, no `inst_tab` record, no
kind-descriptor row, no dock tile, no Task Manager row. It is a bare window
created with `wm_create` and destroyed with `wm_destroy`, owned by
`fdlg.inc` and by nothing else, and `wm_owner` never names it.

That is a deliberate reading of §29, not a shortcut around it. The instance
table answers "what is running", and a modal dialog is not something that
runs: it is a question the kernel asks on the front application's behalf and
then forgets, the same species as a `menu_track` pull-down. Making it an
instance would put a "Files" tile in the dock that cannot usefully be
clicked, a row in the Task Manager for a thing with no CPU, a minimize box
with nowhere to minimize *to*, and one more consumer of `INST_MAX` at the
exact moment the user is trying to save their work.

Three consequences follow, all wanted:

- **No callback billing.** `inst_win_owner` returns 0 for an unowned
  window, so the §11 dispatch sites skip `inst_charge` and the dialog's
  paint/key/click time stays on the UI task — unbilled, exactly like
  `menu_track` and `fm_rclick` (§12.4). Time the user spends deciding is
  nobody's CPU cost.
- **The close box is Cancel, for free.** `app_close_win` on an unowned
  window degrades to a plain `wm_hide` (§29.4), and §38.2's gate check turns
  a hidden dialog into a cancelled one on the next input. No close-path code
  exists in this module at all.
- **So is the minimize box**, by the same route and with the same result.
  A modal dialog that could be minimized would strand the machine behind an
  invisible modal; instead it simply cancels.

### 38.2 Modality — the whole design rests on it (binding)

`[fdlg_win]` non-zero means a dialog is on screen and the system is
**input-modal to it**. Enforced at exactly two points in the UI ladder
(§13), and nowhere else in the kernel:

- `fdlg_grab` — called on every `EVT_MDOWN` and `EVT_RDOWN` before the
  ladder branches. `CF=1` (swallow, with a `snd_beep`) for any press whose
  point lies outside the dialog's window rect: the menu bar, the dock, the
  desktop, and every other window are inert. A press **inside** the rect
  returns `CF=0` and takes the ordinary ladder, so the dialog's title-bar
  drag and content clicks work with no code of their own.
- `fdlg_top` — called right after `wm_top` in the keyboard poll: it
  substitutes the dialog for the frontmost window. Belt to `wm_hit`'s
  braces (nothing can be raised above a dialog while `fdlg_grab` is
  swallowing clicks), and it costs ten bytes to stop a keystroke from ever
  reaching the window behind.

**Self-validating (this is what makes §38.1 safe).** Both helpers begin at
`fdlg_gate`, which clears `[fdlg_win]` — and destroys the window — the
moment the gate names a record that is no longer *used and visible*. The
`menu_check` discipline of §12.3, with teeth: a dialog hidden by any route
whatsoever is a **cancelled** dialog, and no path can wedge the system
behind a dead modal.

A third call site, `fdlg_reap`, sits in the UI task's deferred section and
runs the same collection **once per loop pass**. It is not part of
modality — the two filters above are already correct without it — it is
about latency: the close box and the minimize box only *hide* the window,
and until the gate is collected `[menu_win]` still names a window nobody can
see, so §12.3 reverts the bar to Locator and leaves it there until the user
happens to click something. Reaping on the idle path makes the cancel, the
bar and the released gate all land on the same tick. It costs three
instructions when no dialog is up.

**The payoff is §38.4.** Because nothing else is clickable, no other window
can navigate the volume while a dialog is up, so the dialog needs **no
listing cache of its own**: it reads the global mount snapshot directly.
That is the exact opposite of the Disk window's rule (§22.1) and it holds
for exactly this reason — remove modality and the dialog needs a fifth
`VIEW_SEG` slot, which does not exist.

### 38.3 Layout — fixed size, so constants and not a `fm_layout`

`WF_SIZABLE` is deliberately **not** set: a dialog is a question, not a
workspace, and a fixed frame turns §22's whole live-layout apparatus into
eleven `equ`s. Window 300×170 at (90, 60); content 298×151 after the border
and `TITLE_H` — the authored frame, which `wm_fit` clamps onto the live
screen (§39.7), so on a 200-row adapter the record is shorter than these
constants and the bottom of the layout is cut. All values below are
content-relative:

```
(6,6)     header: 'A:'/'B:' + two spaces + the folder's caption
(6,20)    list frame .. (213,120)   file list, 14px scroll bar inside its
                                    right edge (x 200..213), 6 rows of 16px
                                    starting at y 22, names at x 10,
                                    right-aligned size (or 'folder') ending
                                    at x 196
(224,20)  [ Open ] / [ Save ]       62x14 buttons, the right column;
(224,40)  [ Cancel ]                Open/Save carries a second frame 2px
(224,60)  [ Drive ]                 out — the default-button convention
(6,126)   'Save as:' + name box (72,124)..(213,140), text at x 76, caret
          after it; in Open mode the same box shows the selected name
          without a caret
```

The selected row is an XOR bar across the list interior, exactly as §22
draws its own (`gfx_xor_fill` under the held lock) — and, for the same
reason, **moving the selection costs two of those bars and nothing else**.
`fdlg_sel_bar` is §22.2's `fm_sel_bar` in this module's geometry: same
range-checking, same factored-out-of-the-painter argument, same XOR-is-its-
own-inverse trick. A row click used to run `fdlg_draw_both` — refill the
list interior, re-letter six rows and their sizes, redraw the scroll bar
frame, both rules, both arrows, the track and the thumb — to move one strip.

The dialog needs one thing the Disk window does not: **the name box follows
the selection** (`fdlg_pick`), so "did the selection move" is the wrong
question and "did the box move" is the right one. Only the setter can answer
it, so `fdlg_setname` returns **CF = 1 when the stored text, length or caret
is not what it was** and `fdlg_pick` passes that through. What falls out:

- a **different** row → two bands, plus the name box only if it actually
  took a new name (landing on a *folder* row leaves the box alone, and now
  leaves its pixels alone too);
- the row **already selected** → the click re-arms the double-click window
  and draws **nothing**, unless something was typed into the box since, in
  which case the box alone is redrawn and the list still is not. That last
  case is what keeps behaviour identical: clicking the selected row still
  puts the file's name back over what you typed.

The fallback is `fdlg_setsel` scrolling the selection into view, which moves
every row: `[fdlg_scrl]` is compared across the call and a change sends the
click back to `fdlg_draw_both`. A **click** can never trigger it — the hit
test is bounded by what is drawn — but the keyboard shares `fdlg_setsel`,
and comparing one word is cheaper than proving that.

`fdlg_sel_bar` asks `fdlg_rows` for the total instead of reading
`[fdlg_shown]`, which is painter scratch and means nothing on a click.

**The button column** carries Open/Save, Cancel, Drive and — in **save mode
only** — a **two-line New / Folder** button (`FD_BH2`, drawn by `fdlg_btn2`),
which makes a folder here named by whatever is in the name box and then
navigates into it (`fdlg_newfolder`). Two lines because 63px of button holds
seven glyphs, and "New" on its own under Save/Cancel/Drive reads as new
*what*. The dialog could enter
folders and leave them and had no way to make one, so saving into a new
folder meant cancelling, opening a Disk window, pressing `n`, and starting
again. The name box is already a line editor with the FAT character set
enforced per keystroke, so the whole feature is: take what is in it,
`dskw_mkdir`, find it in the listing the mkdir's remount just rebuilt, and
dive. A refusal — empty name, bad name, full or write-protected disk —
beeps, the same answer every other refusal in this dialog gives. It is
absent in open mode because opening a file has no use for it.

**Selecting a folder and pressing Enter navigates into it in BOTH modes.**
`fdlg_act` used to test the selection's type only in open mode, so in save
mode the same keystroke committed a save under whatever name the box still
held.

### 38.4 The listing and navigation

The dialog lists **the mounted volume's current directory** — `disk_dir` /
`disk_nfiles`, the §19 mount snapshot, read one staged entry at a time
through `dsk_get_dir`. It never touches `VIEW_SEG` and never copies the
listing anywhere (§38.2).

**A display row IS a directory index**, and `fdlg_rows` is just
`disk_nfiles`. It used not to be: this module synthesized its own `..` row
and carried the resulting +1 offset through every row ↔ index conversion in
it. The mount puts the parent link in the listing now (§19.5), as a type-3
entry carrying the parent's first cluster, so the dialog, the Disk window
and every view cache get the same row from the same place — and the offset,
and the one routine that existed to decide it, are gone. The on-disk `.` and
`..` entries are still filtered out by §19's species rules; the row the user
sees is the synthesized one.

**Acting on a row** (double-click, or Enter, or the Open/Save button):

- type 2 (subdirectory) or type 3 (the `..` link) → `dsk_chdir` into the
  entry's first cluster. `..` needs no special case and no second walk of
  the directory off the disk: the mount already put the parent's cluster
  there.
- type 0/1 (a file) → **Open**: that is the answer, commit it. **Save**:
  copy the name into the edit field, do not commit — replacing a file is a
  thing the user must still press Save for.

Navigation runs `disk_mount` under the held lock, like §22's Refresh, and
resets selection and scroll. A mount that fails leaves the row area empty
and the header saying `no disk`; there is no error line, because every
failure reachable here is that one and the empty list already says it.

**Choosing sets the current directory (binding).** The dialog navigates
with `dsk_chdir`, so by the time the callback runs, `[disk_drive]` and
`[dsk_cwd]` **are** the folder the user chose in — and that is where §19.2
resolves the file slots. A chooser that returned a bare name without moving
the volume would be handing the caller a name it could not open. The Disk
windows are unaffected: they re-sync from their own caches before every
action (§22.1), which is what that machinery is for.

**The name box is a line editor, not an append-only field.** `[fdlg_ncur]`
is the caret — the character index it sits in front of, 0..`[fdlg_nlen]`.
Left/Right/Home/End move it, Delete removes what is in front of it, backspace
what is behind, typing inserts at it (`fdlg_nins`/`fdlg_ndel`), and a click in
the box positions it by the same half-cell rule the Disk window's rows use.
The five movement keys are gated on **AL = 0** first: the numeric keypad sends
`4 6 7 1 .` with the scan codes of Left, Right, Home, End and Delete, so
without the gate NumLock would turn typing a digit into moving the caret.

The name-box hit test is the **first** thing a click that missed the button
column sees, and that placement is load-bearing: the button column jumps to
that label when it misses, so a test reached only by falling out of the column
would never see a click on the left half of the dialog.

### 38.5 State (`.bss`, singleton)

```nasm
fdlg_win   resw 1   ; the live dialog window, 0 = none — ALSO the modal gate
fdlg_mode  resb 1   ; 0 = Open, 1 = Save
fdlg_rqwin resw 1   ; requester: window ptr...
fdlg_rqcb  resw 1   ; ...near completion proc...
fdlg_rqrec resw 1   ; ...instance record...
fdlg_rqsp  resw 1   ; ...and its I_SPTR, the staleness triple (§38.6)
fdlg_sel   resw 1   ; selected DISPLAY row, 0FFFFh = none
fdlg_scrl  resw 1   ; first visible row
fdlg_clkt  resw 1   ; birth tick of the last click (double-click window)
fdlg_name  resb 16  ; the name: default in, chosen out, edited in Save mode
fdlg_nlen  resb 1   ; its length, 0..12
fdlg_hdr   resb 28  ; header line scratch
fdlg_row   resb 20  ; one row's name, staged out of dsk_ent
fdlg_num   resb 8   ; the size column
```

`fdlg_name` is the one buffer the caller's default arrives in and the
chosen name leaves in; it is valid to the callback for the duration of the
call and no longer.

**Name editing** (Save mode only) reuses §22's rules verbatim so that what
the box shows is exactly what will be stored: every character goes through
`dskw_char` (upper-cases, rejects anything outside the FAT set), a `.` is
allowed once and never first, and the budget is 12 — 8 + `.` + 3. Enter is
Save, Esc is Cancel. In **Open** mode the keyboard drives the list instead
(Up/Down/PgUp/PgDn, Enter, Esc); there is no field to type in, which is
what the Standard File dialog this is modelled on did too.

### 38.6 The API slot 0x0150 and the completion callback

The table-span assertion in `kernel.asm` goes 40 × 4 → **41 × 4** (42 × 4
since §39.8 appended `OSAPI_VIDEO`, and 44 × 4 since §20.6 appended the
worker-task pair); `apps/os88api.inc` mirrors the equ (§20.5).

```
0x0150 fdlg_open   in AL = 0 Open / 1 Save, BX = requester window ptr,
                   DI = completion proc (near, in the caller's image),
                   SI = default name (NUL, <= 12) or 0 for none.
                   out CF=0 accepted — the dialog is ON SCREEN when this
                   returns; CF=1 refused (one is already up, BX is not a
                   live owned window, or the window table is full).
                   Preserves every register.
```

**The caller must hold the gfx lock** — which every legal caller does,
because the only legal callers are a window callback and an `AM_ONCMD`
handler (§12.2), the §20.3 UI-task rule. That is what lets `fdlg_open`
`wm_create` + `wm_show` **inline** and put the dialog on screen before it
returns: no `inst_launch_post`, no deferral, no next-UI-pass delay. The
handler that called it simply finishes and unlocks with the dialog already
drawn.

**The completion callback (binding).** Near-called on the UI task with the
gfx lock **held**, *after* the dialog window has been destroyed:

```
in   AL = mode (0 Open, 1 Save)
     SI = the requester window ptr it registered
     DI = the chosen name, NUL-terminated, <= 12 chars (fdlg_name)
out  nothing; no register need be preserved
```

Same rules as `AM_ONCMD` (§12.2): it may draw, it may call the file slots,
it must **not** take the lock, and it **must repaint itself** — the kernel
does not repaint after it returns. That is precisely why the order is
**teardown, then callback** and never the reverse: `wm_destroy`'s repaint
has already restored everything the dialog covered, so the callback paints
onto a clean window instead of over a dialog that is still on screen.

It is legal for a callback to open another dialog; the gate is clear by
then and the whole sequence is one lock hold deeper, which nothing in this
module minds.

**Staleness (binding).** The request records the requester's instance
record, its `I_WIN` and its `I_SPTR`. The callback is skipped, silently, if
any of the three no longer matches a live (`I_STATE` = 1) record: a package
whose window was closed while the dialog was up has had its region freed
(§29.2 rule 7), and its near pointer no longer means anything. Checking the
window pointer alone would not do — window slots are reused.

### 38.7 Lifecycle

1. **Ask.** The application calls slot 0x0150 from a menu command or a key
   handler, under the lock it already holds. `fdlg_open` refuses if
   `[fdlg_win]` ≠ 0, else records the request, seeds `fdlg_name` from SI,
   re-mounts the current directory, creates the window and `wm_show`s it.
2. **Interact.** Ordinary `W_PAINT`/`W_ONKEY`/`W_ONCLICK` under the lock,
   with §38.2 keeping every other click and key away from them.
3. **Commit.** The Open/Save button, Enter, or a double-click on a file
   runs `fdlg_commit` — still inside the dialog's own callback, still under
   that callback's lock: `fdlg_close` (destroy the window, drop the gate,
   hand the menu bar back to the requester with `menu_activate` +
   `menu_draw_bar` — `wm_front` gave it to the dialog on the way in, and
   the dialog has no menus), then the staleness check, then the callback.

   Destroying the window from inside its own `W_ONCLICK` is safe and is the
   point: the §11/§13 dispatch sites read `W_ONCLICK`/`W_ONKEY` *before* the
   call and touch nothing but the saved owner word and the lock afterwards
   — and that owner word is 0 for this window (§38.1), so there is not even
   an `inst_charge` to land in a freed record.
4. **Cancel** — the Cancel button, Esc, the close box, or the minimize box
   — is step 3 without the callback: the first two call `fdlg_close`
   directly, the last two go through `wm_hide` and are collected by
   `fdlg_check` at the next input (§38.2).

### 38.8 Partial redraws — what a keystroke actually changes

Typing a character into the name box used to repaint the whole content: the
header, both frames, six listing rows, the scroll bar, four buttons, the
label, the box and the caret — about 120 glyphs and a 298×151 white fill, to
move one letter.

The dialog **does not resize**, so unlike a text window it never has to work
out *which* pixels moved — every key and every click knows what it touched.
The content is three parts, and the two that can change on their own are a
**body** (draws) plus a **wrapper** (erases its own rectangle first, and only
its own):

| routine | erases | draws |
|---------|--------|-------|
| `fdlg_draw_name` → `fdlg_name_body` | inside the name box's frame | the name, and the caret in Save mode |
| `fdlg_draw_list` → `fdlg_list_body` | inside the list frame | `fdlg_clamp` + `fdlg_draw_rows` + `fdlg_draw_sbar` |
| `fdlg_draw_both` | both | both — a selection move changes the listing *and* takes the new name (`fdlg_pick`) |

**The default button is the fourth thing a keystroke can change**, and it is
redrawn on an *edge* rather than per key. `fdlg_actok` is the one predicate
behind the button's pen, the click on it and the Enter key (§46 rule 4) — a
chosen row always acts, and with no row chosen an empty name box is not an
answer — so typing the first character or deleting the last one flips it, and
`fdlg_draw_name` compares the answer against `[fdlg_actl]` and calls
`fdlg_defbtn` only when it moved. Every caret move comes down the same path,
and a frame plus a label per keypress is real money on the machines this runs
on.

`fdlg_btn` takes the **caller's** pen rather than forcing `CBLACK`, so the
disabled frame greys with the label — which is what shows on a mono adapter,
where a package's grey text alone would round to black (§46 rule 2/3).

`fdlg_paint` calls the **bodies**, because `wm_paint_all` already handed it a
white content; everything else calls the wrappers. Neither erase touches its
frame: the frames are chrome and never move.

Measured on the APPS folder, 8 characters typed into an empty name box,
counting calls inside the kernel: `font_char` **972 → 36**, scanlines filled
**7,600 → 184**.

Routing, and the rule for adding to it:

- name only — every printable character, backspace, Del, the four caret keys,
  and a click inside the box
- list only — the scroll bar's arrows and its two paging halves (a click *on*
  the thumb, or on an inert track, now draws nothing at all)
- both — Up/Down/PgUp/PgDn and a click on a row
- **whole** (`fdlg_repaint`) — the third part changed: the header's drive
  letter or `Disk`/`Folder` word, or the box's `Save as:`/`Folder:` label.
  That is `fdlg_go` (the Drive button, and diving into a folder),
  `fdlg_newfolder` and `fdlg_unfold`. **If a new action can change any of
  those three strings, it is a full repaint.**

One consequence to keep: `fdlg_onkey` now caches the content origin itself,
because it used to get `[fdlg_cx]`/`[fdlg_cy]` for free from `fdlg_paint` and
no longer always reaches it.

### 38.9 Symbols

| symbol | contract |
|--------|-----------|
| `fdlg_open` | API slot 0x0150, above. The only entry point applications have. Caller holds the gfx lock. |
| `fdlg_gate` | Internal. Drops the gate (and destroys the window) if `[fdlg_win]` is not used-and-visible. In: AL = 1 if the caller holds the gfx lock, 0 if not — it needs the lock to destroy and takes its own when it must. Out: CF=0 and BX = the dialog if one is live, CF=1 if none. |
| `fdlg_reap` | UI task, no lock held. `fdlg_gate` for its side effect alone, once per loop pass. Preserves all registers. |
| `fdlg_grab` | In: CX/DX = press point. Out: CF=1 = swallow (beeped). Preserves all registers. Called with no lock held. |
| `fdlg_top` | In: BX = `wm_top`'s answer. Out: BX = the dialog window if one is live. Preserves everything else. Called with the lock held by the key path, so it uses the non-destroying half of the check. |
| `fdlg_close` | Destroy the window, drop the gate, return the bar to the requester. Caller holds the lock. Preserves all registers. |
| `fdlg_commit` | `fdlg_close`, staleness check, then the callback. Caller holds the lock. |
| `fdlg_paint` / `fdlg_onkey` / `fdlg_onclick` | The window procs; all three assume the held lock and never take it. |
| `fdlg_draw_name` / `fdlg_draw_list` / `fdlg_draw_both` | §38.8. Erase one rectangle and redraw it. All assume the held lock and a valid `[fdlg_cx]`/`[fdlg_cy]`; all preserve every register. |
| `fdlg_name_body` / `fdlg_list_body` | The same drawing without the erase, for `fdlg_paint`, which is handed a white content. |
| `fdlg_rows` | Out: AX = `disk_nfiles`. There is no offset any more — §19.5 put the `..` row in the listing, so a display row is a directory index. |
| `fdlg_stage` | In: AX = display row, which IS a directory index (§19.5 put the `..` row in the listing, so this module no longer synthesizes one or carries an offset). Out: `fdlg_row` = its name, `fdlg_type` / `fdlg_size`+`fdlg_sizeh` its §19 type word and size dword. |
| `fdlg_go` | In: AX = first cluster. `dsk_chdir` + reset selection and scroll. |

**What this deliberately does not do.** No filtering by extension (an Open
dialog that hid `.TXT` from Note Pad would be a lie about what is on the
disk), no icon view (the Disk window is where you browse; this is where you
choose), no new-folder button (§22 has one, one implementation is enough),
no multiple selection, and no second dialog on top of the first — the gate
is a single word for the same reason `[menu_win]` is.

## 39. viddet.inc — video adapters, runtime geometry, the mono renderer

The kernel drives three display adapters and picks one at boot. One binary,
one set of drawing entry points, three very different framebuffers:

| kind | resolution | colour | framebuffer | stride | banks | mode set |
|---|---|---|---|---|---|---|
| `VID_VGA` (0) | 640x480 | 16, 4 planes | A000:0000 | 80 | — | int 10h AX=0012h |
| `VID_HERC` (1) | 720x348 | mono, 1bpp | B000:0000 | 90 | 4 x 0x2000 | 6845, direct |
| `VID_CGA` (2) | 640x200 | mono, 1bpp | B800:0000 | 80 | 2 x 0x2000 | int 10h AX=0006h |

Module `kernel/viddet.inc`, prefix `vid_`. It is `%include`d **before**
`splash.inc` because the boot splash probes and sets the mode on its first
tick, which means everything in it must be resident inside the first
`SPL_RESIDENT` sectors and **all of its data must live in `.text`** — `.bss`
is not cleared at that point and holds whatever the machine left there
(§15). `[vid_mono]` is **not** `[bb_mono]`: the latter means "all four
back-buffer planes hold identical bytes" (§32) and the two must never be
conflated.

### 39.1 Detection — `vid_detect`

Probe order is binding, and the equipment word is consulted **last**:

1. `int 10h AX=1A00h` (VGA/MCGA Display Combination Code). A pre-EGA BIOS
   does not implement `AH=1Ah` and returns with AL as we set it, 00h;
   `AL = 1Ah` means VGA. → `VID_VGA`.
2. `int 10h AH=12h BL=10h` (EGA "get EGA info"). An EGA or VGA BIOS
   overwrites BL with the card's memory size; an XT BIOS leaves 10h alone.
   An EGA does mode 12h, so it counts. → `VID_VGA`.
3. `int 11h` equipment word bits 5:4. `11b` = a monochrome card at B000 →
   `VID_HERC`; anything else → `VID_CGA`.

Step 3 is last because an EGA or VGA driving a monochrome monitor **also**
reports `11b`, and those machines belong on the mode 12h path.

**Documented scope cut:** no Hercules-versus-plain-MDA discrimination (the
0x3BA vertical-sync toggle). A plain MDA is text-only, so no better action
exists; driving it as a Hercules is strictly less wrong than driving it as a
CGA at B800.

`VID_FORCE` (build-time, `-DVID_FORCE=1|2|3`) skips the probe. It exists for
testing only and every shipped image is built without it — see §39.9.

### 39.2 Runtime geometry

`SCREEN_W`, `SCREEN_H` and `ROW_BYTES` (§3) survive as the **VGA reference
values** and as the initializers of the live block; they are no longer the
truth about the screen. The truth is:

**The live block — nine contiguous words, in `vid_tab`'s column order.**
`vid_apply` `rep movsw`s an 18-byte `vid_tab` record straight over them, so
reordering one without the other silently corrupts all nine:

`vid_seg`, `vid_w`, `vid_h`, `vid_stride`, `vid_bmask`, `vid_bshift`,
`vid_rowadd`, `vid_wrapbit`, `vid_wrapfix`

**Derived by `vid_apply`**, because the sites that want them are inner loops
or single instructions with no register to spare for the arithmetic:
`vid_wm1`, `vid_hm1`, `vid_wm8`, `vid_hm8`, `vid_strm1`, `vid_rseg`,
`vid_rpara`, `vid_rend`, `vid_dock_y0`, `vid_dock_ty0`, `vid_clk_hx`,
`vid_ymax`, `vid_popmax`, `vid_desk_zx`, `vid_desk_zl`, `vid_desk_zr`, and
the bytes `vid_kind`, `vid_mono`, `vid_planes`, `vid_planes_w`.

```
vid_mono   = (kind != VID_VGA)          vid_planes = mono ? 1 : 4
vid_rseg   = mono ? vid_seg : [bb_seg]
vid_rpara  = mono ? 1 : BB_PLANE_PARA   ; MUST be nonzero - see 39.3
vid_rend   = vid_rseg + (mono ? 1 : 4*BB_PLANE_PARA)
vid_popmax = min(MENU_POPMAX, (vid_h - MBAR_H - 2) >> 4)    ; 16 / 16 / 11
vid_desk_zx = vid_w - 56                ; 584 at 640 wide, as it always was
[bb_on]    = vid_mono                   ; see 39.5
[mouse_x]/[mouse_y] = centre of the new screen
```

**Homing the cursor is load-bearing, not cosmetic.** `mouse_x`/`mouse_y` are
initialized data sized for 640x480, and `cur_draw` computes its bottom clip
as an *unsigned* `[vid_h] - y`. Left at 240 on a 200-row screen that
subtraction wraps to ~65,500, the row count stays 16, and the first cursor
draw writes past the end of the framebuffer.

`MBAR_H` (20) and `TITLE_H` (18) stay assembly-time constants: they are font
and chrome dimensions, not screen-derived. Only `SCREEN_H - TITLE_H`
combined them, and that is the precomputed `[vid_ymax]`.

### 39.3 The parameterized software renderer

**There is no second graphics driver.** `vgabb.inc` (§32) was written as a
latch-free, port-free *software* renderer over `vga_rect_setup`'s coordinate
core, targeting a RAM back buffer — and nothing in it cares that the target
is RAM. Four changes make it the 1bpp driver:

- its plane segment is `[vid_rseg]` — the back buffer on VGA, the
  **framebuffer itself** on mono;
- its plane count is `[vid_planes]` — 4 or 1;
- its plane step is `[vid_rpara]`, which **must stay nonzero even at one
  plane**: `bb_xfer` terminates on a segment compare against `[vid_rend]`,
  and a step of 0 never terminates;
- every row advance goes through `gfx_nextrow`.

The planar bodies in `vga12.inc` are simply unreachable on mono, so they keep
their assembly-time `ROW_BYTES` and `VGA_SEG`, and `gfx_flush` keeps them
too — it only ever runs on VGA.

**`gfx_rowbase`** — in AX = y, out AX = that row's byte offset. Clobbers AX,
CX, DX; **preserves BX**, which `ico_core` depends on.

**`gfx_nextrow`** — in DI, out DI one scan line down. **Touches DI and flags
and nothing else**: several callers are inner loops with no spare register,
and one runs inside the IRQ4 cursor path.

```
addr(y)  = (y & bmask) * 0x2000 + (y >> bshift) * stride
nextrow  = di += rowadd; if (di & wrapbit) di += wrapfix
```

`wrapfix` is `stride - nbanks*0x2000` as a 16-bit add, and `wrapbit` is the
bit the unconditional add carries into once it steps off the last bank. On
VGA `bmask`/`bshift`/`wrapbit` are all 0, so `gfx_rowbase` reduces to exactly
`y * 80` and `gfx_nextrow` to `add di, 80` — **with no adapter test on either
path**, which is why the VGA output is bit-for-bit what it was before.

**Both read their parameters through `CS`, not `DS`.** Two callers run with
DS pointed elsewhere entirely: `bb_xfer`'s save path sets DS to the
framebuffer segment for its `movsb`, and its restore path sets DS to the
caller's buffer (the menu's save-under claim). Reading the stride through DS
there fetches framebuffer bytes as a scan-line step. CS is `KERNEL_SEG` for
every byte of kernel code (§33 — there is no far code any more), so the
override is always correct.

**Invariant, with a build assertion:** the bank number must live in DI's own
high bits, which requires a bank's rows never to reach into the next bank's
0x2000 window. Hercules uses 87x90 = 7,830 and CGA 100x80 = 8,000 of 8,192.
A change of stride or height breaks this silently, so `viddet.inc` asserts it.

### 39.4 Colour reduction at 1bpp

`gfx_ink` maps a 16-colour index to `00h` black, `FFh` white, or `01h` — the
50% dither class, which costs nothing extra because `bb_rect` has already
computed the row-parity AA/55 byte for `BBM_GRAY`; arming `[bb_altm]` is the
whole implementation.

```
0..6 -> black      7,8,9,10,11,13 -> dither      12,14,15 -> white
```

Chosen so every distinction the shipped UI carries in colour survives:
Minesweeper's covered `CLGRAY` against its open `CWHITE`, its exploded
`CLRED` mine against the `CLGRAY` ones, Piano's pressed `CLBLUE` key against
an idle `CWHITE` one.
**Accepted losses:** light grey and dark grey are indistinguishable, and
Piano's decorative sharp glyph (`CLRED` on white) disappears — no flat map
can give it black while the black-key core needs white.

Glyphs round to black rather than dithering — a dithered glyph costs half its
strokes, and Piano's coloured letters want contrast, not texture — **with one
exception: `[gfx_dis]`**, the disabled flag (§46 rule 1). `font_ink` sets
`[font_dith]`, which selects a third row loop in `font_char_bb` that ANDs the
shifted glyph with an alternating AA/55.

Without it a disabled string came out **black — pixel-identical to a live
one** — so a greyed menu item simply stopped saying it was greyed, and
`MENU_DIS`'s entire visual signal was missing on two adapters out of three
(§12.2). Half a stroke is a real cost and it is the right one to pay here: it
is what the 1bpp Macintosh did to a greyed-out menu item, and it is legible for
the same reason.

The trigger is the **flag and not the colour** because the renderer cannot ask
whether a string is a control. Keyed off `CDGRAY` it caught Minesweeper's digit
8 (§23), which is dark gray for contrast and has nothing to do with being
disabled.

The phase restarts at `AA` for every cell rather than tracking absolute y, so a
line of text is in step with itself; text rows are 8px apart, which is even, so
lines are in step with each other too. It is a **separate loop** and not an
`and` inside `.crow` because that is the innermost loop of every string on the
slowest machines we support, and almost no glyph wants this.

### 39.5 Dispatch: `[bb_on]` and `[bb_dbl]`

`[bb_on]` now means **"route drawing through the software renderer"**. It is
permanently 1 on a mono adapter, set by `vid_apply` before anything draws.
The new `[bb_dbl]` carries the narrower old meaning, **"a back buffer is
armed and must be flushed"**, and is what `gfx_flush`, the Control Panel's
Display page and the Task Manager's RAM figures read — otherwise a mono
machine would claim double buffering and bill 150KB that was never
allocated. `bb_init` refuses to set `[bb_avail]` on mono, so the Display page
cannot arm it, and its caption says *"Framebuffer is the display driver on
this adapter"* rather than lying about memory — naming the reason, because
"not here" invites the guess that a bigger machine would qualify.

This is why the nine existing `[bb_on]` dispatch sites needed **no new
dispatch bytes**. The four that did are the escape hatches — the callers that
bypass `[bb_on]` by contract because their output is transient and must never
enter the back buffer (§32): `vga_xor_rect_vram` (the drag outline),
`vga_xor_fill_vram` (menu highlights), and `vga_save_vram` /
`vga_restore_vram` (the cursor's save-under, from inside IRQ4). On mono
"direct to VRAM" and "through the software renderer" are the same place, so
each gets a `[vid_mono]` prefix into its `bb_*` twin.

Double buffering is **unavailable** on mono, by design and not by omission:
the renderer already writes the framebuffer directly, so there is nothing to
double.

The same reasoning is why the clip hooks of §11.3 sit at the **public**
entry of each primitive rather than in its VRAM body: above the `[bb_on]`
dispatch one hook serves both renderers, and below it a clip would work on
VGA and silently do nothing on Hercules and CGA. That is the expected
failure mode of getting the placement wrong, and `make test VIDEO=cga` plus
`tools/hercshot.py` are what catch it.

And it is why `font_char_bb` is the mono adapters' **only** text renderer,
which makes its eight-row loop the innermost loop of every string os8088
draws on the slowest machines it runs on. It therefore keeps three things the
VRAM path had from the start and the port did not:

- **The ink test is hoisted out of the rows.** `[gfx_color]`'s plane bit
  cannot change inside a plane, so the set/clear choice is made once per
  plane and there are two eight-row loops rather than one branch per row.
- **A blank glyph row is skipped whole.** or-ing in 0 and and-ing in FF are
  both identity, but a read-modify-write of framebuffer memory costs the
  same ~30 cycles on an 8088 whether or not it changes a pixel. Most glyphs
  have a blank descender row, many a blank top row, and a space is eight of
  them. The second byte is skipped on the same test, which is the whole cost
  of a glyph at a byte-aligned x.
- **`gfx_nextrow` is inlined**, CS overrides and all. Its body is three
  instructions and the `call`/`ret` around them cost as much again.

There is no BIOS alternative to any of this and there cannot be. `int 10h`
AH=09h/0Eh is cell-aligned to the BIOS font's own grid, knows nothing of
§11.3's clip region, and is slower than this code in mode 12h — and on
Hercules **graphics** there is no BIOS text support whatsoever, the mode
itself being set behind the BIOS's back (§39.6).

### 39.6 Mode set and teardown

`vid_setmode` is idempotent and safe to re-run with the card already in
graphics. VGA and CGA get their mode — and their clear — from the BIOS.
Hercules has no BIOS mode at all:

```
out 3BFh, 3                     ; configuration: graphics allowed, both pages
6845 at 3B4h/3B5h, R0..R11 = 35 2D 2E 07 5B 02 57 57 02 03 00 00
out 3B8h, 0Ah                   ; graphics, video on, display page 0
rep stosw  0x4000 words at B000:0000
```

The 32KB clear is **load-bearing**: `bb_init`'s ordering and the first
desktop paint both assume a cleared framebuffer, and no BIOS will do it. Its
`cld` is explicit because on the splash path this runs on the boot sector's
flags and `spl_tick` never issues one.

`vid_text` (`CMD_REBOOT`) returns VGA and CGA to mode 3; Hercules gets
`out 3BFh, 0` and mode 7.

### 39.7 Window placement — `wm_fit`

Every window template in the tree — built-in, dialog and package alike — is
authored against 640x480. `wm_create` therefore clamps the frame onto the
live screen at the single point all four creation paths funnel through
(`inst_tplbuf` and its per-slot cascade, `fdlg_tpl`, every built-in template,
and every already-built third-party `.o88`, with zero app rebuilds). Size
first, then position, and the y floor last so it wins:

```
w = min(w, vid_w)               h = min(h, vid_dock_y0 - MBAR_H - 1)
x = min(x, vid_w - w), floor 0  y = min(y, vid_dock_y0 - h - 1), floor MBAR_H
```

**Both height clamps are one pixel short of the dock, and that pixel is
load-bearing.** A window's drop shadow lives on row `y+h`, and
`wm_dock_clear` tests `y+h` against `[vid_dock_y0]` with `jae` — so a frame
that merely *reaches* the strip is already covering its first row, and every
window later shown over it pays a `wm_dock_under` pass (§11.90). Clamping
`h` alone is not enough either: the `y` clamp would put the shadow straight
back on the strip for any frame tall enough to have been clamped at all. One
subtraction here fixes it for **every fixed-size template at once** — which
is why Solitaire, Arkanoid and the Task Manager needed no per-app rule
beyond keeping their own derived layouts in step with it.

On VGA it is a no-op. **Consequence for §11:** the record may differ from the
template it was created from, so a package that lays out from its own
constants rather than re-reading `W_W`/`W_H` will draw clipped. That is the
accepted outcome — clipped but launchable — and the per-adapter acceptance
criteria in §17 record which apps it affects.

### 39.8 The package ABI

`OSAPI_VIDEO` (slot **0x00B4**; the table is 44 slots since the §20.6
worker-task pair) — no inputs;
out AX = width, BX = height, CX = the first row the dock owns (so the usable
desktop is rows `MBAR_H`..CX-1), DL = `vid_kind`, DH = bits per pixel (4 or
1). Callable from any context, lock held or not — the first slot for which
that is true.

`SCREEN_W`/`SCREEN_H`/`MBAR_H`/`TITLE_H` remain in `apps/os88api.inc` as the
**reference** geometry: fine for authoring a template, wrong for anything
that reads a screen edge at run time. Appending a slot is invisible to
already-built packages, so no `.o88` needs rebuilding.

### 39.9 Testing

**QEMU emulates no CGA and no Hercules card** (verified: `-device help`
offers only VGA-class devices), and 86Box has no automation socket. So:

- `make test VIDEO=cga` — the CGA path under the QMP harness. SeaVGABIOS's
  `int 10h AX=0006h` is a byte-exact CGA framebuffer (B800, two banks,
  80-byte stride), so this exercises the entire mono renderer, the banked
  row walk, the mono cursor and the colour map. It does **not** exercise
  detection: the machine is still a VGA. Drive it with
  `tools/mouse.py --screen 640x200`.
- `make test VIDEO=herc HERCSEG=0x7000` plus
  `python3 tools/hercshot.py build/qmp.sock 0x70000 out.png` — B000 is
  unmapped under QEMU and swallows every write, so `HERCSEG` relocates the
  framebuffer into spare RAM and `hercshot.py` reads it back and applies the
  §39.3 layout on the host. A wrong stride shears the picture sideways;
  wrong bank arithmetic shears it into four interleaved combs.
- `make xt-cga` / `make xt-hercules` — 86Box, `ibmxt`, 256KB, real `cga` and
  `hercules` cards. The **only** way to exercise the §39.1 probe and the
  Hercules 6845 programming. Interactive; no automation exists.
- **VGA first, always.** `vga_rect_setup` is shared and its row base now goes
  through `gfx_rowbase`, which must reduce to exactly `y * 80`. Screendump
  and byte-compare against a pre-change baseline before trusting anything
  else; a one-pixel VGA regression found later gets blamed on the mono work.

### 39.10 Per-adapter acceptance

On CGA the usable desktop is 156 rows, so windows authored at 640x480 meet
§39.7's clamp. Two different outcomes follow, and the difference matters:

- **A window that paints from its own live record clips correctly.** Note Pad
  (which recomputes its right/bottom edge every paint) and the Standard File
  dialog are fully usable — the dialog's Save/Cancel/Drive column sits at
  content y 20/40/60 and is untouched; only the name box loses the bottom
  pixels of its border.
- **A window that paints from fixed constants draws past its own frame**,
  because nothing clips an ordinary paint to a *window* — `vga_rect_setup`
  clips to the **screen**, and §11.3's clip region is armed only by a
  background task, never on the `wm_paint_all` path. Minesweeper and Piano are clipped at the dock and
  still play. The Task Manager was the one case where this defaced the dock
  strip once a second, so its two fixed-pitch row lists now stop at
  `[tm_ylim]` (`tm_view_begin`, §28) rather than at `TM_ROWS`.

The general rule this leaves for any new window: **the clamp is not a clip.**
If a paint proc lays out from constants rather than from `W_W`/`W_H`, it can
write outside its frame on a short screen, and only the screen edge stops it.

## 40. apps/fractal — the progressive renderer and its restore cache

The reference §20.6 worker, and the first shipped package whose window is
worth more than the state behind it. Menus (§12.2) pick one of five types,
one of four palettes, and zoom/reset/redraw; a click recentres. All of that
is a couple of word stores plus `fr_kick` — the app never draws the picture
from a callback, the worker does.

**Numerics (binding).** Q4.12 fixed point throughout, iteration cap 48, and
the escape count *is* the palette index. `fr_clamp` bounds the centre on
both axes unconditionally inside `fr_setup`, because the clamp is what keeps
the core's arithmetic from wrapping. Zoom is a shift count (step = step0 >>
z) so there is no per-frame division except the single step0 = span / cw.
`FT_SYM` — 0 none / 1 x-axis / 2 origin — is declared in the type table and
**still not exploited**; exploiting it is a free 2× on four of the five
types and the one optimisation here that can silently corrupt half a frame,
so it wants a byte-compare harness against the reference model first.

**Progressive refinement.** `fr_advance` runs three passes over the canvas:
pass 0 computes rows 0, 4, 8 … and paints each as a **4-row band**, pass 1
fills rows 2, 6, 10 … as 2-row bands, pass 2 the rest as single rows.
ch/4 + ch/4 + ch/2 = ch, so no pixel is computed twice — only painted twice
— and the finished image is exactly the non-progressive image, with a full
chunky preview after a quarter of the work. `fr_rowcalc` computes one row
into `fr_line` **with no lock held**; `fr_emit` then takes the lock for a
run-coalesced band paint and releases it (rule 3 of §20.6).

### 40.1 The pass-0 restore cache

**Why there is no frame buffer.** The canvas is 320×170. Raw at 4bpp that is
27,200 bytes against the **19,968-byte package pool then shared by every resident
package**; a run-length copy of a whole frame measures 11,712–13,928, most
of the pool for one instance. A run-length copy of **pass 0 alone** is
~3,300 bytes, and it is the right quarter of the work to keep, because pass 0
already covers the whole canvas.

**Format.** One word per run: colour in bits 15..12, the run's last column in
bits 11..0. A colour index is 0..15 and a column 0..319, so they cannot
collide and the pack is an OR. Runs start at column 0 and each begins where
the last ended, so the start column is implied and a row ends with the run
whose last column is cw−1; the row is implied too, because pass 0 emits rows
0, 4, 8 … in order. `fr_cache` is 4,000 bytes = 2,000 runs, and the ceiling
is not that number — it is that image + bss must stay inside one 512-rounded
7,168-byte region, because two Fractals plus Minesweeper plus Note Pad is
19,456 of the pool and the next step up does not fit.

**One lock hold owns "this row was consumed".** `fr_emit_body` does the cache
append, the progress count *and* `fr_advance`, all behind its one restart
check. None of them may live in the worker's loop, because the worker runs
them with the lock released and `fr_redraw` publishes the (pass, row) to
resume from: a stale `fr_advance` out there steps straight past that row, and
then no pass ever paints it — pass 1 covers rows 2 mod 4 and pass 2 the odd
ones — while `fr_crow` can never match `fr_row` again, freezing the cache for
the rest of the frame. While `fr_restart` only ever meant "restart" this was
harmless, because the worker's loop top rewrote pass and row anyway; the
resume value 2 deliberately keeps them, which is exactly what makes the
placement binding.

**Recording — `fr_cache_row`.** Called from `fr_emit_body`, under the gfx
lock and *after* its restart check but *before* the visibility check. Under
the lock because it must be atomic against `fr_kick` for the same reason the
paint is: a row computed for the old view must not be cached for the new
one. Before the visibility check because a row that cannot be seen is still
worth keeping — the cache is exactly what makes uncovering the window cheap.
A row is committed **whole or not at all**; a partial row would desync every
row after it, since each run's start column is implied by the previous run's
end. On overflow the cache simply stops growing: `fr_crow` stays put, every
later row fails its test, and the cached prefix stays replayable.

**Replay — `fr_redraw`.** `W_PAINT` no longer calls `fr_kick`. A repaint is
not a view change: the content arrived white-filled but the view state is
untouched, so `fr_redraw` replays the cached bands (`fr_replay` — the emit
loop with the arithmetic removed) and then tells the worker to **resume**
rather than restart. `fr_restart` therefore carries three values: 0 nothing
pending, 1 restart from row 0, 2 resume with the pass and row the UI just
set. The worker reads it with a read-and-clear `xchg`, not a test then a
store, because with two values a separate test and clear could see the
resume, have `fr_kick` overwrite it with a restart, and then clear the
restart away.

Resume lands at pass 0 row `fr_crow` if pass 0 was interrupted, or at pass 1
row 2 if it completed — passes 1 and 2 are recomputed, which is why the
percentage comes back at ~25% and climbs rather than at 0%. `fr_redraw` also
compares the cached canvas size against the live one, because `fr_setup`
re-reads W_W/W_H every time and `wm_fit` can clamp them (§39.7); a cache
built for a different canvas would replay runs at the wrong columns.

**Invalidation is one point.** `fr_kick` empties the cache, and every
user-side view change — type, palette, centre, zoom, reset — funnels through
it. There is nowhere else to get it wrong, and with the cache empty
`fr_redraw` is exactly the old behaviour, spelled `fr_kick`.

### 40.2 Drawing while covered

`fr_emit_body` arms a clip region (§11.3) instead of vetoing on
`wm_obscured`, so a fractal with a corner — or most of itself — under
another window goes on rendering the part that shows. The status strip is
the exception and takes §11.3's granularity rule the other way: it is
white-filled and then written over with four strings, so `fr_status` asks
`wm_clip_test` for the whole strip rect first and leaves the old strip alone
when the answer is no, rather than erasing rows it would then refuse to put
text back into. Combined with §40.1
the two halves cover the two ways a window loses its pixels: covering costs
nothing because the worker keeps drawing what is visible and caching what is
not, and moving costs one replay because the cache survives the repaint.

### 40.3 Acceptance

- Drag the window mid-render: the coarse image is back within one frame of
  the drop and the percentage resumes rather than restarting at 0%.
- Change type: the canvas clears and the render restarts at 0%.
- Bury it behind another window: it keeps rendering the visible strips and
  paints nothing outside them; raise it and the whole picture is there.
- Two instances alongside Minesweeper and Note Pad all load (the heap must
  not refuse the fourth package), and `python3 tools/os88disk.py --verify
  build/apps.img` passes.
- All three adapters, and the back buffer both off and on.

## 41. cpudet.inc / xmem.inc — CPU tiers and memory above 1MB

Two modules and five slots: `cpudet.inc` publishes the CPU tier and the A20 line,
`xmem.inc` sizes the store above 1MB, allocates out of it and moves bytes
through it. The claim heap (§50) is unaffected and remains the answer
for *conventional* memory a package cannot fit in its own segment; these are
the answer for bulk data that does not fit conventional memory at all.

**None of it exists on tier 0, which is the target machine.** An 8088 has no
A20 line and nothing above linear 0x0FFFFF; `cpu_detect` stores `CPU_8086`,
`xm_init` publishes zero KB, and every entry point below refuses having
touched no port. The Task Manager reads `XMS 0/0K` there (§28).

### 41.1 The three tiers, and how they are detected

`[cpu_tier]` is `CPU_8086` (0), `CPU_286` (1) or `CPU_386` (2), and
`[cpu_feat]` carries three verified bits: bit 0 A20 open, bit 1 HMA claimed,
bit 2 unreal mode armed. Both are **initialised `.text` data, not `.bss`**:
`-f bin` zeroes nothing at boot, so the answer a machine reads when the probe
never ran has to be the safe one — the `bb_avail` / `snd_live` idiom of §32
and §34.7.

**The tier is INFORMATION, not permission.** Nothing branches on
`[cpu_tier]` to decide whether the store is usable; it branches on the
feature bits, and a package branches on the KB figure `xm_caps` answers
(§41.8). A 386 whose A20 gate never verified has no store, and code keyed off
the tier alone walks straight into it. The one legitimate use of the tier is
choosing an instruction *encoding* — which transport `xm_copy` runs, whether
`xm_arm` may execute its `cpu 386` island.

### 41.2 A20 — the gate, and the verification that is not optional

Both enable methods are advisory: a machine may have neither, may decode port
0x92 to something else, may have a keyboard controller that accepts D1h/DFh
and does nothing. So `CPU_F_A20` is set by `cpu_a20_probe` — a wraparound
read — and by **nothing else**, never by "we wrote to the gate".

### 41.3 HMA_SEG — the one segment above 1MB, and who owns it

`HMA_SEG:0010` is linear 0x100000 and `HMA_SEG:FFFF` is 0x10FFEF, the highest
byte real mode can name at all: 65,520 bytes, **data only**. The near model
pins CS = DS = `KERNEL_SEG`, so no code ever lives up there on any tier.

### 41.4 Unreal mode, and why it is FS and GS

A segment register's hidden descriptor cache keeps its limit until the
register is *reloaded*, so one plain real-mode `mov fs, ax` anywhere would
reset it and the 4GB window would be gone — silently, as a copy that wraps
inside 64KB. **Only `xmem.inc` writes FS or GS**, a rule the whole tree can
keep because a case-insensitive search finds nothing else. ES and DS were
never candidates: ES is the most contended register in the kernel and DS is
the kernel's own segment. The tier-2 copy re-arms from the resident GDT
inside the same `cli` window as the accesses that depend on it, because the
BIOS is re-entered by *interrupts* and not only by kernel calls.

### 41.5 xmem.inc — sizing, the allocator, and the copy

`XM_MAX_BLKS` (8) entries, 1KB granularity, stamped with the calling
instance and force-freed at teardown like a sound grant (§34.3) or a heap
claim (§50.4). Deliberately small: this is a bulk store for a handful of
large claims, not a malloc. Free space is **implicit** — whatever no in-use
entry covers — so a freed block merges with its neighbours for nothing.

`xm_copy` carries **one ABI over two transports**: `int 15h AH=87h` on tier
1, unreal mode on tier 2. The caller cannot tell which ran and must not care.

### 41.6 What the Task Manager reports

One line, `CPU 8086  XMS used/sizedK`, directly **below** the package-pool map
and above the process list (§28), with a **bar** under it: `used/sized` of the
bar's interior black, the rest white — the same shape and the same
element-check discipline as the performance view's RAM bar, because it answers
the same question about a different pool.

**The tier shares this line rather than taking one of its own**, because it is
the same fact: what the CPU is (§41.1) is what decides whether there can be any
memory above 1MB at all, and on the machine this OS is written for the honest
reading of the whole line is "an 8086, so none". Nothing else in the UI ever
said which tier `cpu_detect` settled on. The three names are padded to the same
six columns so the figures beside them do not shuffle between machines, and the
line needs no check word of its own — `tm_rowsum` hashes the composed string,
so the tier folds into the XMS line's.

`TM_STRMAX` now takes the **maximum** of its two candidate longest lines rather
than naming the winner. Which line is longest has already changed twice — the
RAM line grew by four the day HEAP joined it, the XMS line by ten the day the
tier did — and the failure mode is a `tm_str` that silently writes past the end
of `.bss`.

It gets no *map*, and that is the honest layout rather than a missing feature:
the two maps above it are conventional memory and a magnified slice of
conventional memory, and extended memory is in neither — real mode has no
address for it, which is the whole reason `xm_copy` exists. A bar needs no
addresses, so a bar is what it gets.

Two things about the bar are load-bearing:

- **Its frame is drawn by `tm_draw_mem`, not by `tm_xbar`.** On tier 0 the
  width is 0, which is exactly the value `tm_rowck_clear` leaves in the check
  word, so the body skips itself on the very first paint — a frame inside that
  gate would never be drawn at all. An empty bar beside `XMS 0/0K` is
  deliberate: an absent one reads as a missing feature rather than an empty
  pool.
- **The figures need five digits.** `tm_put3` emits one character per hardcoded
  place, so 64,448 KB came out as a plausible-looking `48K`. Every other figure
  in this window is bounded by 640K and fits; this one is bounded by `int 15h
  AH=88h` and does not.

The never-zero rule in `tm_elchk` (§28.1.1) exists because of this bar and was
then found to matter more widely: `mem_tab` is all zeroes on a machine with no
heap to claim from, so on 128KB the *conventional-memory map* hashed to 0 too
and stayed blank forever.

### 41.7 Testing

The coverage matrix is in §16 and it is uneven by construction: QEMU
presents no 8086 and no 286, so `make test` exercises **tier 2 only**, and
with A20 already open — neither gate path in §41.2 is genuinely run there.
Tier 1 belongs to `make 286`, tier 0 to `make xt` / `xt-640` / `xt-cga` /
`xt-hercules`, and all of those are interactive.

Two branches are cheap to reach under the harness and must both be checked:
run the `test` recipe by hand with `-m 1M` for **no extended memory at all**
(AH=88h answers 0 — the claim refuses, the caps slot reports 0, the three
allocator slots refuse, and the Task Manager line reads `0/0K`) and with
`-m 2M` for a small non-zero store, where an allocator that gets its
subtraction wrong will hand out a base past the top of RAM.

### 41.8 The package ABI

Five slots: `OSAPI_CPU_INFO` (0x0188), `OSAPI_XMEM_CAPS`
(0x0190), `OSAPI_XMEM_ALLOC` (0x0198), `OSAPI_XMEM_FREE` (0x01A0) and
`OSAPI_XMEM_COPY` (0x01A8). What ALLOC returns is an **opaque 32-bit token**,
not a pointer: every byte crosses through COPY. UI-task context only — on
tier 1 the copy goes through a BIOS call some 286 BIOSes implement by
resetting the CPU, which taken under the gfx lock is a dead machine.

### 41.9 Forbidden (binding)

1. Nothing above `cpudet.inc` may touch an A20 port or assume a gate exists.
2. A `cpu 386` island is **assembly-time permission only** — an island
   reached on a 286 is an illegal-opcode trap on a machine with no handler.
3. No code above 1MB, on any tier, ever.

### 41.10 Acceptance

- `make xt` and `make xt-640`: identical boot, identical desktop, identical
  Task Manager figures except the tier/XMS line, which reads
  `CPU 8086  XMS     0/    0K`. This is the regression that matters most —
  tier 0 is the target machine.
- `make test`: the line reads `386+` with five-digit KB figures; allocate,
  copy out, copy back and compare, and the bytes match.
- The same `test` recipe with `-m 1M`: `386+` and `0/0K`, and every allocator
  slot refuses cleanly rather than handing out a base above the top of RAM.
- `make 286` on a VM with more than 1MB: the line reads `286`, the HMA claim
  succeeds, the AH=87h transport round-trips the same buffer the tier-2 path
  did, and the caps figure is 64KB short of what AH=88h reported.
- All three adapters (§39.9), because the line is drawn text like any other
  and clips at `[tm_ylim]` — and on a narrow screen it is the **second
  column** that has to hold it (§28.1).
- `make clean && make`: both geometries, zero warnings, every §15.1 guard
  still passing, and `make check-images` clean.

## 42. Paint — the seventh package (apps/paint/paint.asm)

A bitmap editor over the published package ABI. Prefix `pt_`, embedded
palette icon (flags bit 0). Directory order on the apps disks stays pinned:
mines, hello, notepad, piano, fractal — **paint is appended last** so the
earlier indices hold.

It shipped with **no kernel change at all**, which is why
`docs/PAINT-NOTES.md` is a list of the capabilities whose absence shaped it.
Four of them have since landed and this section reflects them: the claim heap
(§50) replaced the app's unsanctioned grab of linear 0x66000, `gfx_blit4`
(§5.4) replaced its own run-coalescing blitter, `osapi_font_glyphs` (§6)
replaced its int 10h ROM-font probe, and `wm_resize` + `W_ONSIZE` (§11.1)
replaced its writing of W_W/W_H behind the kernel's back. It uses colours beyond 0/15, which
retires `[bb_mono]` (§32) — supported and expected; on a 1bpp adapter its
palette drops to §39.4's three ink classes.

Eight tools (pencil, eraser, dropper, rectangle, ellipse, selection, flood
fill, text), a per-tool line width that also sets an unfilled shape's border
thickness, one-level undo that doubles as redo, an internal clipboard, and
BMP and GIF load/save through the Standard File dialog (§38). The full design
record, including the kernel capabilities whose absence shaped it, is
`docs/PAINT-NOTES.md`.

**The canvas is not a fixed size.** It is whatever the screen and memory
allow, from 32x16 up, and everything else follows from that:

- **The window is `WF_SIZABLE` and the canvas IS its content.** Dragging the
  grow box resizes the picture: existing pixels keep the top-left corner, new
  area comes up white, and a shrink crops. `pt_onsize` is the window's
  `W_ONSIZE` negotiator (§11.1), so the size is settled *before* `ui_grow`
  commits it; `pt_track` still runs at the top of every W_PAINT and adopts
  whatever size the record now carries, which covers `wm_fullscreen` and
  anything else that changes it without asking. `pt_geom` fixes the three
  buffer bases **once**, from the largest canvas the machine can fund, so no
  base ever moves and `pt_resize` can stage the old rows in the undo image
  with no overlap to reason about.
- **A shrink that would throw away ink is refused, per axis.** Before adopting
  a smaller size `pt_track` asks `pt_lose_w`/`pt_lose_h` whether the columns or
  rows about to go are all white — `repe scasb` against 0xFF over the packed
  bytes, half a compare per pixel, with the boundary nibble handled only when
  the surviving width is odd. A dirty axis keeps its old size while the other
  one still moves, so widening-while-shortening does the half that is safe.
  The decision is `pt_sizeask`, which clamps and refuses but commits nothing;
  `pt_setsize` is the one caller that then commits. Splitting them is what lets
  the same rules answer `W_ONSIZE`: the negotiator returns the accepted frame
  size and **nothing has been drawn at either**, so the refusal costs no
  repaint at all — only the toast, deferred to the end of the paint through
  `[pt_apend]` = 2 because a toast drawn before the repaint would be wiped by
  it. `pt_wfix`, which writes the frame back to what the canvas needs, survives
  for the one path with no negotiation in front of it (`wm_fullscreen`) and
  because it runs from inside W_PAINT, where `OSAPI_WM_RESIZE` would re-enter
  the app's own paint proc. A toast rather than a dialog is deliberate: a modal
  window costs a repaint to raise, another to dismiss, a window slot and a
  click, and says no more than one line of the app's own status text does.
- **Rows are addressed by a (segment, offset) pair.** A canvas may exceed one
  64KB segment — 636x326 is 104KB — so `pt_rowseg[y]` names the paragraph a
  row starts in and `pt_rowoff[y]` the 0..15 bytes into it; the undo image has
  the identical layout `[pt_undelta]` paragraphs higher, so one table serves
  both. Every loop that walks a row goes through `pt_rowset`/`pt_urowset`,
  which cost six instructions a row and nothing a pixel.
- **The canvas is still a BMP.** 4bpp packed, high nibble = left pixel, rows
  **bottom-up behind a 118-byte DIB header**, stride = the BMP stride
  (ceil(w/2) rounded up to 4). Saving is one `OSAPI_FILE_WRITE` of the canvas
  base with no staging pass, **whatever the picture's size**: the write walks
  its source by segment (§18.4.1) and the canvas is one contiguous claim, so
  DX:AX goes straight into DX:CX. It used to need the file to fit 64KB as
  well, and a larger canvas could be edited but not saved; that refusal and
  its notice are gone.

  The decoder followed (§42.6), so a picture Paint saves is one Paint can
  re-open. The GIF encoder's own ceiling stays, and it is not the file
  API's.
- **The canvas base is a heap claim.** `pt_geom` asks `OSAPI_MEM_AVAIL` what
  the largest free run is, caps it at what the app can use, and takes it with
  `OSAPI_MEM_CLAIM` (§50.3); `[pt_base]` is whatever segment came back. That
  retires the whole of what this bullet used to describe — a hard-coded
  0x66000, chosen as the first paragraph above the back-buffer planes, plus a
  second base for the case where `bb_init` had refused the buffer and those
  150KB were dead for the session. The app no longer reads a kernel policy
  constant to guess whether the kernel will ever want that block, because the
  kernel now has to ask the same allocator: with Paint holding the memory the
  Control Panel's "Double buffered" row greys out and says "Not Enough Ram"
  (§31.3), and closing Paint frees the claim and brings the row back. The
  duplicate-instance guard (`pt_dupchk`, a magic record at the scratch base
  believed only while its window pointer still named a live Paint window) went
  with it: two instances now get two different claims by construction.
- **Four claims, sized for the canvas that is actually up.** `pt_alloc` takes
  them separately (§50.3): a fixed 12KB **scratch** (the flood-fill stack and
  the file readers' fallback buffer, claimed once and never moved), the
  **canvas**, an **undo image** the same size, and a **clipboard** at
  `PT_CLIPMINP`'s floor. Only the canvas must succeed; each of the other two
  is worth exactly one feature, so a refusal switches that feature off and
  nothing else — which is the three-tier degradation the single-block version
  produced by arithmetic, expressed as what it is. A fresh 448×280 Paint holds
  about 150KB on any machine, where the single block held 260KB on a 640KB one
  and left no room for the back buffer or a second instance.
- **A resize re-claims; nothing is staged.** `pt_resize` frees the undo image
  and the clipboard (a resize drops both anyway), claims a new canvas, copies
  the picture across block to block, frees the old canvas, and asks for undo
  and clipboard again at the new size. The peak is the old canvas plus the new
  one. This is what retires the constraint that shaped the whole of the old
  design: the old picture used to be staged **in the undo image**, so the undo
  image had to be big enough for any canvas that could ever be adopted, so
  every buffer had to be sized for the screen's maximum and the bases had to
  be fixed for the staging to be safe. Copying block to block needs no staging
  area, so a machine that could not fund an undo image can still resize, and
  `WF_SIZABLE` no longer depends on there being one. A refused claim re-fits
  the size against the block already held and falls back to the old staging
  path, so a resize can degrade but never fail half-done.
- **The clipboard grows on demand.** It starts at the floor — which is what
  the GIF codec's tables need, and a paste needs nothing until something has
  been copied — and `pt_clip_need` claims a bigger one the first time a Copy
  asks for more, taking the new block before releasing the old. A canvas-sized
  clipboard nobody has used is 60KB of dead claim.
- **What a grow is allowed to ask for** is `pt_growmax`: the largest free
  **run**, not the total free, because re-basing needs the new block and the
  old one live at the same time. `pt_fit` clamps against that or against the
  block already held, whichever is larger.
- **What is claimed is then divided in three tiers.** Below the point where
  the undo image can be funded, undo goes; below where the clipboard can,
  Cut/Copy/Paste and GIF go with it.

  Three things follow from the third tier and are load-bearing:
  **the window is not `WF_SIZABLE`** (`pt_resize` stages the old picture in the
  undo image, so with no undo image a size change cannot preserve anything, and
  a grow box that silently wiped the picture is worse than no grow box);
  **`pt_umark` early-outs**, so `[pt_undo_ok]` is never set and both
  `pt_undo_swap` and `pt_urestore` are no-ops by construction rather than by
  separate tests; and **the file reader falls back to the scratch area**,
  borrowing the flood-fill stack — idle during a load, re-initialised by the
  next fill — one paragraph in so the claim record survives and the buffer
  still starts at offset 0 of a segment, which is what both decoders assume.
  12KB still opens a small picture, and a file too big for it comes back as the
  file API's own `FERR_BIG`. **Open is therefore never disabled**, on any
  machine that can run the app at all.

  The tiers are decided **once**. Because `pt_geom` fixes the buffer bases from
  the largest canvas the machine can fund, the undo image is always big enough
  for any canvas `pt_fit` will allow and the clipboard's size is a constant —
  so there is deliberately no re-check on load or resize, because nothing that
  could have changed exists. What the tiers cost is expressed two ways, and both
  are needed: the menu item **keeps its own label and gains "(Not Enough Ram)"**
  (the kernel's menus have no disabled state, §12.2, and an item still has to
  say what it would do), and the command itself answers with a toast — which is
  what the Ctrl-key shortcut hits, since it never goes near a menu. 
- **Content layout** (content-relative): tool palette, two columns of four
  20x20 buttons at x 1/22, buttons past the canvas bottom simply not drawn and
  not clickable; the canvas size printed under them; a black divider at x 43;
  the canvas at x 44; a 22-row strip below it holding four width buttons, the
  current-colour well, as many colour swatches as fit, and two toggles
  right-anchored clear of the grow box (which owns the content's last 13
  columns and rows, §11). The strip spans the **whole** content width,
  including the columns under the palette, so the click ladder tests y first.
  **The grow box lives in that strip**, and `wm_draw_win` draws it *before*
  W_PAINT (§11.1), so the strip's white bed erases it: `pt_draw_strip` ends with
  `OSAPI_WM_GROW` for that reason. Leaving the redraw to the paint proc alone
  left the box missing after every tool, colour, width or toggle click until the
  next full repaint.
  The size readout lives in the palette rather than the title bar for a
  structural reason: `wm_draw_win` draws the title *before* calling W_PAINT,
  so a size adopted during that paint would be one repaint stale there and the
  only cure would be a second full repaint.
- **The canvas size is typed, not only dragged.** Under the tool buttons sit a
  `W` field, an `H` field and an Apply button, all inside the palette column's
  42 pixels; Enter in either field applies, Escape abandons the edit, the first
  digit after a field is clicked replaces its value rather than appending, and
  an empty field means "leave this axis alone". They need 41 rows below the last
  tool button, which a 110-row CGA canvas does not have, so `pt_szon` answers
  for the painter and the hit test both — a control that is not drawn is not
  clickable — and the two-line readout is what shows when they do not fit.

  **The canvas height therefore has a floor of `PT_SZ_END`, not `PT_CH_MIN`.**
  The fields are the only way to make a canvas taller — there is no menu item
  and no key — so a canvas short enough to hide them could never be grown
  again, and typing 100 into the height field, or dragging the grow box up, was
  a one-way trip for the rest of the session. It is a minimum window size and
  nothing more exotic. The floor is held against `[pt_chmax]` first: where the
  *screen* is what cannot fund the controls — CGA, whose 110 rows are 18 short —
  no floor can, and `PT_CH_MIN` is the honest answer. **Paint on a CGA machine
  cannot be resized at all**, which is a layout limit rather than a memory one
  and is not new.

  Apply and a grow-box drag both go through **`pt_setsize`**, the one place
  a size is decided: it clamps to the screen, then to what memory will fund
  (`pt_fit`), then to what the picture will stand to lose (`pt_lose_w` /
  `pt_lose_h`), so typing 900 into a field is exactly a drag that asked for too
  much and produces the same crop toast. The two differ only in what they owe
  afterwards: a drag arrives with the frame already redrawn at the dragged size,
  so a refusal owes a repaint; Apply changes the frame itself, so a *success*
  owes one and a refusal owes nothing but the toast.
- **The swatches narrow before any of them go.** `pt_org` divides the pixels
  left over after the right-anchored toggles by the live colour count and
  clamps the quotient into `[PT_SW_MIN, PT_SW_DX]` = [11, 21] pixels, publishing
  the result as `[pt_swdx]` (pitch) and `[pt_swsz]` (body, pitch − 1); only once
  the pitch has bottomed out does `[pt_nsw]` start dropping colours off the
  right. `pt_draw_strip` and `pt_strip_click` both read those two words — the
  hit test also rejects the inter-swatch gap — so a colour is clickable exactly
  where it is drawn at every width. `pt_org` is called from `pt_click` with the
  click point still in CX/DX, so like every other routine here it preserves all
  registers; the arithmetic needs CX, and forgetting to push it turned every
  content click into a stray palette click.
- **Keyboard shortcuts** are the control codes int 16h already delivers, so
  W_ONKEY needs no scan codes: Ctrl+Z undo/redo (the same exchange either way),
  Ctrl+C / Ctrl+X / Ctrl+V, and Delete, all routed to the very routines the
  Edit menu calls so the two doors cannot drift. Ctrl+Z ends an open text run
  first — the caret is screen-only and would otherwise be left over a picture
  that no longer has the text under it. Copy and cut are no-ops without a
  selection and paste without a clipboard, so no tool gating is needed.
- **Loading takes the file's own dimensions** as far as the screen and memory
  allow, then crops — a 700x440 picture opens as 594x342 on a 640KB VGA
  machine — and the window follows by writing W_W/W_H and calling
  `OSAPI_WM_FRONT` for the repaint. A crop sets a flag that makes **File >
  Save refuse**: one click must not overwrite the original with less than it
  held. Save As, a deliberate act on a name the user picks, is allowed and
  clears the flag on success. The reader takes 1, 4, 8 and 24bpp
  uncompressed BMPs, top-down or bottom-up, validating every header field
  before believing it — magic, both halves of every dword that must be zero,
  the depth, `biCompression`, and that the pixel data the header describes
  fits inside the bytes actually read (§19: every byte off the disk is
  hostile). Source palettes map to the sixteen by weighted city-block
  distance. A file over 64KB cannot be read at all (`FERR_BIG`); JPEG is
  recognised by magic and refused with a message, and the reason is in the notes.
- **GIF is implemented both ways, and lives in borrowed memory.** An LZW
  dictionary is 16KB by itself, so the codec's tables go where a load or a save
  has already invalidated something: **reading** stages the file in the undo
  image (as the BMP reader does) and puts `prefix[4096]` words +
  `suffix[4096]` bytes + a 4096-byte output stack in the clipboard, filling its
  reserved floor (`PT_CLIPMINP`) exactly — two build-time assertions keep that
  true; **writing** puts child/sibling/suffix for 2048 codes in the clipboard,
  builds the GIF in the undo image, and reads the canvas a row at a time through
  `pt_line`. What decides those placements is that DS must stay on the kernel
  segment for the bss, so ES is the only far pointer there is and **no inner
  loop may need two**: the reader's bit window borrows ES for three bytes and
  gives it straight back, and the writer's output goes through a 255-byte block
  in bss, flushed once per GIF sub-block — the shape the format wants anyway.
  The reader flattens the sub-blocks in place first (each block's data copied
  back over the byte that announced it, so the destination trails the source and
  one `rep movsb` per block is safe), which is what lets the bit reader be three
  bytes and a shift rather than a state machine. It takes 2..8-bit minimum code
  sizes, global or local colour tables, interlacing (the four passes), and skips
  extension blocks; every offset is bounded by the byte count actually read, and
  dimensions are capped at `PT_GDIM_MAX` so a header claiming 60,000 rows is
  refused rather than decoded into nothing for a very long time.
  **The writer stops at 11-bit codes**, which halves its tables, and the one
  place the two directions are not mirror images is documented at `pt_gadd`: a
  writer defines its new string as it emits the code before it, a reader cannot
  until it has seen the code after it, so the reader's table runs one entry
  behind and the two code-width rules are deliberately off by one to compensate
  (writer widens when free *passes* 1<<size, reader when it *reaches* it). The
  same offset is why the writer's ceiling is 2047 and not 2048. Both halves are
  verified by round-tripping flat, banded, striped and pure-noise pictures
  through a host decoder.
- **The save verb owns the format, and the extension follows it.** File carries
  Save Bmp / Save as Bmp… / Save Gif / Save as Gif…; each sets `[pt_sfmt]` and
  `pt_setext` rewrites the name's extension to match, so "Save Gif" on
  PICTURE.BMP writes PICTURE.GIF rather than GIF bytes into a file whose name
  promises a bitmap, and the Save-as dialog is offered the corrected name so
  what the user sees is what gets written. **Loading ignores the extension
  entirely** — the magic decides, because a name is a wish and a magic number is
  a fact. A canvas edited on a 1bpp adapter still saves its full 4-bit indices
  in either format: the reduction to §39.4's three ink classes is a property of
  the screen, not of the picture.
- **Drag loops invert `ui_drag`'s lock ordering (§13), and must.** A package's
  `gfx_*` output goes through the back buffer when double buffering is armed
  (§32), so a tracking loop that held the lock would show nothing until the
  button came up. `pt_wait`/`pt_wait_tick` draw under the lock, *release* it so
  `gfx_unlock`'s flush reaches VRAM, yield, and re-take it. The window is
  frontmost while it tracks, and since §11.3 a background painter under it
  draws its own visible region rather than over the top — so the released-lock
  window is safer than it was when this was written against `wm_obscured`.
  The same rule applies to anything drawn *before* a long operation rather than
  during one: the "Loading..." toast a file dialog's completion callback puts up
  is followed by one `pt_wait`, or the buffered machine flushes it only after
  the load it was announcing.

### 42.1 About Paint, and why it is not a window

Registered with `OSAPI_ABOUT_SET` (§12.2), and **only in `PT_M_LIVE`**: taking
the card down repaints through `pt_repaint`, which draws a canvas, and a Paint
that could not claim one has a notice on screen already saying the more useful
thing. It is `[pt_abon]` plus `pt_abmeas`/`pt_abdraw`/`pt_abdismiss`, the same
shape as §43.8 and §44.7 — a card on Paint's own content, dismissed by the next
click or key, drawn last by every repaint so nothing erases it.

**A second window would have been the obvious design and is still the wrong
one here.** A package's second window is never bound to its instance record:
only the window the entry proc returns goes through `inst_bind_win` (§21), so
`wm_owner` says nothing owns it, it gets no dock tile and no Task Manager row,
and there is no `OSAPI_WM_DESTROY` for the package to clean it up with. It is
*safe*, though: every teardown site — `app_close_win`'s task-less branch,
`inst_task_die`, the winless worker death and the loader's abort — runs
`wm_destroy_seg` over the dying region's segment **before** the region is
freed, destroying every window whose `W_SEG` stamp names it. So an extra window can never outlive the segment its
callback procs live in — it just makes a poor citizen of the ones that manage
it by hand. A card is a flag and some pixels: it cannot outlive the instance
because it never existed apart from it.

### 42.6 Opening a big picture, and the ceilings that were not the file API's

§18.4.1 lifted the file API's 64KB ceiling and §42's save with it. Three of
Paint's own ceilings were left, all of them written before the kernel had an
allocator to ask, and two of them have gone.

**The staging buffer is a claim now, not a borrowed one.** A file being
decoded used to be read into the **undo image** — "the biggest single buffer
we have" — and, when there was no undo image, into the flood-fill stack:
`PT_SC_KB`, twelve kilobytes, which was the real limit on what Paint could
open. Borrowing the undo image is also why opening a picture threw the undo
history away, and why the code that followed emptied the clipboard "because
the codec's tables live in it" — a line that had outlived its reason by two
refactors, the LZW tables having had their own claim for a while.
`pt_stage`/`pt_unstage` take a transient claim sized from `OSAPI_MEM_AVAIL`
and capped at `PT_STAGE_MAX`, and `pt_stagefit` shrinks it to the bytes the
read actually reported **before** `pt_adopt` goes looking for a canvas and an
undo image — nothing knows the file's size until the read reports it, so the
claim has to be for the largest run the heap had. The GIF *encoder* takes the
same claim, capped at `PT_GIF_MAX_KB`, instead of building the file in the
undo image.

**The BMP decoder is 32-bit.** `biHeight × stride` is the number it used to
give up on: 448 × 326 is 73,024 and every picture worth a resizable canvas
passes 65,535. It is a dword now, added to a dword `bfOffBits` — a picture
past the horizon has its pixel data past it too — and compared against the
dword the read reported. Per row, `pt_srowset` turns the 32-bit byte offset
into a **(segment, offset) pair** exactly as `pt_rowseg`/`pt_rowoff` have
done for the canvas since it outgrew a segment, and by the same arithmetic as
`dskw_norm` (§18.4.1): the paragraph part goes into the segment and what is
left is under 16, so the per-column offsets `pt_bmp_row` builds on top of it
cannot carry.

**The GIF decoder stays 16-bit, deliberately.** Every offset into the stream
is a word and `pt_gdeblk` flattens the sub-blocks with one `rep movsb` inside
one segment. A GIF is *compressed* — the 448×280 test picture is 1,285 bytes
where its BMP is 62,838 — so 64KB of GIF is a picture far past `PT_GDIM_MAX`,
and the ceiling is the format's natural size rather than a limitation. What
changed is that a file past it is **refused** ("GIF larger than 64KB") rather
than being handed to a decoder that would truncate it, which is reachable now
that the read can deliver one. The GIF *encoder*'s ceiling is the same shape
and stays for the same reason: `pt_gout` is a 16-bit write cursor into one
segment, and "GIF too big to save — try Bmp" says which of the two formats
has the limit.

Measured: the Paint before this refuses a 69,718-byte BMP outright with "File
too large" — its staging was the default canvas's undo image. The Paint after
it opens a 124,918-byte one (620×400, cropped to the screen's 594×390) and
every one of the 390 decoded rows matches the source pixel for pixel, 190 of
them read from source bytes past the 64KB horizon.

## 43. Solitaire — the eighth package (apps/solitaire/solitaire.asm)

Klondike over the published package ABI. Prefix `sol_`, embedded two-card
icon (flags bit 0), no kernel change of any kind. Directory order on the apps
disks stays pinned: mines, hello, notepad, piano, fractal, paint — **solitaire
is appended last** so the earlier indices hold. The file is `SOLITAIR.O88`,
truncated to an 8.3 stem (§19); the 16-byte name inside the header, which is
what the Task Manager and the dock show, is still `SOLITAIRE`.

It owns no worker task and claims no heap. Everything it does happens inside
`W_PAINT`, `W_ONKEY`, `W_ONCLICK` and its `AM_ONCMD`, under the caller's lock.

### 43.1 The card

One byte: rank in bits 0..3 (0 = ace .. 12 = king), suit in bits 4..5, bit 6
(`C_FACEUP`) set when the card shows. Suit order is **spade, heart, diamond,
club**, which is also the foundations' left-to-right order, so "is it red" is
`suit - 1 < 2` unsigned — one subtract and one compare (`sol_isred`).

Thirteen piles of at most 24 cards each, tableau first so a tableau pile index
*is* its column index: 0..6 tableau, 7..10 foundations (one per suit, in suit
order), 11 stock, 12 waste. 24 is not arbitrary — a tableau column tops out at
6 face-down plus a K..A run = 19, and the stock starts at 52 − 28 = 24.

### 43.2 Two metric sets and a per-column fan

`sol_met_big` (VGA 640x480, Hercules 720x348) is 32x44 cards, 4px gaps, a 5px
face-down fan and a 14px face-up one. `sol_met_sml` (CGA 640x200, which has
156 rows of desktop) is 28x28, 3px gaps, 3px and 12px. `sol_entry` picks by
screen height ≥ 300 and copies the record over thirteen contiguous bss words —
**those words must stay in the record's order**, it is one loop.

The face-up fan must clear the rank glyph's 8 rows or a buried card cannot be
read. Both records do, and the layout is sized so the deepest column the game
can build — 6 face-down + 13 face-up — fits the window at full stretch on VGA.

It does not fit on CGA, and that is what `sol_colfan` is for. Given a column it
answers the fan steps *that column* will be drawn at: the metrics' values when
the last card's offset lands inside `[sol_avail]`, and otherwise the face-up
step tightened by division until it does, then the face-down one, never below
one pixel. **Nothing is cached per pile.** The draw pass and the hit test both
call it, so they cannot end up disagreeing about where a card is.

### 43.3 Faces are drawn, backs are blitted

A face is a white fill, three or four black edges, one or two `font_char`
glyphs and two suit pips. The pips are 1-bit masks run-coalesced by
`sol_maskrun` — one `gfx_hline` per run of set bits — because a pip goes on a
card that is already drawn and `gfx_blit4` is opaque.

A back is a lattice, which has a run every two or three pixels and would cost
several hundred far calls a card. `sol_mkback` renders it **once** into a
packed 4bpp image at start-up — black edge, white margin, a field of diamonds
white wherever `(x+y)` or `(x−y)` lands on a multiple of 8 — and every later
draw is a single `gfx_blit4` (§5.4). The field colour is the only thing that
changes with the adapter: index 1 on VGA, index 9 on 1bpp so it reduces to the
50% dither with the white lattice still crossing it.

Every card is drawn with a **visible height**, which is what the next card in
the fan leaves showing, clamped to the content bottom. A buried card costs
only the rows that survive, its bottom edge is not drawn (the next card's top
edge is one row below it and two black rules would show), and each glyph and
pip is gated on that height — so a two-pixel sliver draws its top edge and
nothing else, and nothing can spill past the card onto the desktop.

### 43.4 Colour is never the only carrier

On a 1bpp adapter (§39.4) index 12 reduces to **white**, so a red pip on a
white face would be nothing at all. There, the two red suits are drawn with
**hollow** masks and the two black suits solid — the trick the black-and-white
Macintosh card games used — and the rank text goes black for every suit. On
four bits the colour does that job and every pip is solid.

The ghost pips in the empty foundations are the exception: they take
`sol_pipsold`, which is always solid, because they are drawn in the dither
class and a 50% dither eats every other pixel of a 1px outline. A ghost says
which suit belongs here, not what colour it is.

The felt is index 2 — green on VGA, black on Hercules and CGA. White cards on
black read as well as white cards on green, and the black card edges that
vanish into the felt are exactly the ones the white card silhouette replaces.

### 43.5 The drag is an XOR outline

`sol_drag` is `ui_drag`'s loop (§13) written against the API, and its ordering
is binding for the same reason: **the outline is XOR-erased before the lock is
released and redrawn after it is taken again**, because XOR is self-inverting
only while nothing else touches the pixels underneath. `sol_linger` holds it
lit for a whole tick so the cursor blits inside the unlock/lock pair cannot
dominate the loop.

No card moves while the pointer does, so a drag costs one `gfx_xor_rect` plus
one `gfx_xor_fill` per card boundary, per tick, however many cards are in the
hand — and those rules are what make a hand of seven read as seven cards.
Nothing is repainted until the button comes up; an illegal drop repaints
nothing at all, because the cards never moved and the screen is already right.

Two things differ from `ui_drag`, both deliberate:

- **The button is sampled by level, not by event.** The kernel's drag cannot
  do that (it must not swallow a re-press). Here it is what makes a press and
  release too quick to register as movement land on the drop path with the
  outline still over the card it came from — which is the auto-play gesture
  below, and it needs no double-click timer.
- **The drop target is chosen by the centre of the hand's TOP CARD**, not by
  the pointer, which may be anywhere inside a seven-card outline. The column
  is `(centre_x − margin + gap/2) / pitch`; above `sol_taby` only a foundation
  takes a drop, and only one card.

### 43.6 Rules and gestures

Standard Klondike. A tableau run may only be lifted whole and only if it is
*already* a descending alternating sequence — the same rule the drop applies,
checked in `sol_grab` before the outline goes up rather than after it comes
down. An empty column takes a king; a foundation takes an ace, then its own
suit in order. `sol_move` is the one place a card changes hands, so it is also
the one place that turns a newly uncovered tableau card face up and that
shrinks the waste's fan.

- **Click the stock** to deal one or three; **click it empty** to turn the
  whole waste back over, face down, unlimited times.
- **Click a face-down top card** to turn it over.
- **Press and release without moving** sends that one card to a foundation if
  any will take it.
- **Menus**: Game — New Game, Restart Deal (the same shuffle again, kept in
  `sol_deck`), Auto Finish (send everything that will go, a move a tick, with
  the lock dropped between them so each one reaches the glass); Deal — Draw
  One, Draw Three, as a **radio pair made out of `MENU_DIS`** (§12.2): the
  mode already in force points at its disabled twin and is drawn grey.
- **Keys**: N, R, A, Space for the same four commands.

The RNG is seeded **once**, from `get_ticks` in the entry proc. Every later
New Game walks on down the same stream, so two deals inside one tick still
differ — which re-seeding from the clock would not give.

### 43.7 Repaint

`sol_drawpile` is the unit: erase one pile's slot back to felt and redraw it.
A move touches two piles and costs two of them. A tableau column's slot runs
the full height of the content; the waste's is two fan steps wider than a card,
which is what the empty column between it and the foundations is for.

The win plaque is the exception — it sits on the felt between the piles and no
pile owns its rectangle — so a move that puts it up **or takes it down** costs
the whole content instead. `sol_checkwin` therefore clears the flag as well as
setting it: a won game is not a dead end, the foundations can still be dragged
back off, and `sol_domove` tests the flag on both sides of the check.

Two things a card back makes expensive, and what each costs:

- **The card back is a lattice, and a lattice does not coalesce.** `gfx_blit4`
  emits one `gfx_fill` per run of equal pixels, and the back's diagonals put a
  run boundary every few pixels: **634 runs** for the 32x44 metrics, 336 for
  CGA's 28x28. A face is two fills, four edges and a couple of glyphs. So a
  back is the one drawing in this program worth going out of the way not to
  repeat, and both rules below exist for it.
- **The stock is only ever redrawn when its PICTURE changes.** That picture is
  one bit — a card back, or the turn-over-again ring — so it changes only when
  the last card leaves the pile and when a recycle refills it. Dealing from a
  stock that still has cards leaves exactly what is already on the screen, and
  `sol_cmd_deal` compares the emptiness before and after rather than redrawing
  it. This was 635 wasted operations on **every single click of the stock**,
  which is the action a player repeats most.
- **A pile whose card covers its whole rect is not erased first.** `sol_covers`
  answers that for the stock and the foundations — one card drawn into a
  one-card rect — where the erase was a second fill of the same pixels. A
  tableau's rect runs the full height of the content and the waste's is two fan
  steps wider than a card, so both still need clearing, and an empty pile needs
  it most: its slot outline covers almost nothing.

- **A tableau column keeps its buried backs.** A column redrew every card
  whenever any of them changed, and the cards at the bottom are face-DOWN - the
  expensive drawing - so a column with five buried cards paid ~205 runs to
  repaint pixels that had not moved. What makes skipping them safe is that
  **face-down cards are indistinguishable**: every back is the same image, so
  the question is never "is it still the same card" but only "is it still drawn
  in the same place at the same size". Two numbers settle that, cached per
  column by `sol_prec`: the face-down fan step it was drawn at (`sol_pfa`), and
  how many leading cards were drawn as slivers of exactly that height
  (`sol_pslv`). A leading card's offset is index x step, so an unchanged step
  means an unchanged position; `sol_keep` takes the smaller of what is wanted
  now and what was drawn then.

  `sol_plan` turns that count into the row the erase **starts** at, and that is
  the load-bearing half: an erase reaching any higher would wipe the very
  slivers being kept. It is also why the erase moved out of the head of
  `sol_drawpile` into a decision - the rect now depends on the fan, which
  depends on `sol_colfan`.

  The cache is invalidated by `sol_pinv` wherever something else may have
  painted over a column: a full repaint (which fills the content with felt
  first), the win plaque (which lands on the felt between the piles, a pixel
  off a column's slivers on a short window), and a change of content origin -
  which is what a window move looks like from inside the app.

Measured on the running game: a drag off a six-card column keeps 4 buried backs
on the source and 2 on the destination, so that one move skips 246 fill runs.
The scheme is checked by comparison rather than by argument - after a stress of
auto-plays, deals and drags that empties a column outright, the incrementally
drawn content is **byte-identical** to the same position forced through a full
`W_PAINT`, and still identical after the window is moved and played on again.

### 43.8 About Solitaire

Registered with `OSAPI_ABOUT_SET` (§12.2) from the entry proc, so the app's
name in the bar becomes a one-item pull-down. Picking it is a window callback
like any other: under the gfx lock, billed to the instance.

**The panel is state, not a modal loop.** `[sol_abon]` goes up, the content
is redrawn with the credit on top, and the next click or key anywhere in the
window takes it down again (`sol_abdismiss`, which returns CF=1 so the caller
spends that click doing nothing else). A loop here would hold the gfx lock
against every background task for as long as the player left the credits up.

Measured, never pinned: the widest line sets the width and the line count
sets the height, both clamped to the content, so it is right on all three
adapters (§39) and nothing needs re-measuring when a line changes.

## 44. Arkanoid — the ninth package (apps/arkanoid/arkanoid.asm)

A brick-breaker over the published package ABI. Prefix `ark_`, embedded icon,
**no kernel change of any kind**. Directory order on the apps disks stays
pinned; arkanoid is appended last. `ARKANOID` is exactly eight characters, so
unlike `SOLITAIR.O88` the file name needs no truncating.

### 44.1 The game is the worker task

A ball has to keep moving between keystrokes, and a window callback only runs
when something happens to the window, so the loop lives in `ark_worker`
(§20.6) — the same shape as apps/fractal. One frame a tick, about 18 fps. The
sleep is the frame rate *and* what keeps the machine usable: a worker that
spun would starve the UI task it shares the scheduler with.

**It sleeps to a DEADLINE, not for a duration, and the difference is a factor
of two.** `task_sleep` is relative — the wake tick is computed from `[ticks]`
*at the call* (§8) — so a loop that works and then sleeps 1 has a period of
`ceil(work) + 1` ticks. The instant a frame's work crosses one 55ms tick the
rate does not sag, it **halves**: 18.2 fps to 9.1, which is what "it lags when
there is a lot on screen" actually feels like. So `ark_worker` keeps
`[ark_due]`, the tick the next frame is owed at, advances it by one each
frame, and sleeps only the difference against `osapi_get_ticks`. A frame that
overran does not sleep at all, and the period becomes `max(1, work)` — a
smooth degradation instead of a step.

Measured on QEMU with an artificial frame costing exactly one tick, which is
the worst point for the old shape: **9.2 fps before, 18.3 fps after**, with
identical work.

`[ark_lagmax]`'s job is the other half. A worker that is *persistently* late
would see its deadline run away from `[ticks]` and never sleep again, so once
it is `ARK_LAGMAX` ticks behind it re-anchors the deadline to now. Small on
purpose: the point is to absorb one slow frame, not to run a backlog of them.

Everything the UI task does is set a word the worker reads — `[ark_launch]`,
`[ark_pdir]`, `[ark_pkeep]` — with no protocol at all, because the 8086
recognises interrupts only at instruction boundaries and every one of them is
a plain word access. It is the same no-lock sharing apps/fractal uses for its
restart flag.

`ark_render` is the one lock hold per frame, and it obeys rule 5: re-read the
origin (the window may have been dragged while we slept), check `W_FLAGS`
bit 1, then `osapi_wm_clip_set`. CF=1 means not one pixel shows, so the frame
is skipped and the game keeps running invisibly — what the kernel's own Bounce
does. The credits panel (§44.7) is the third skip, and there the window *is*
visible; the UI task owns the content until the panel comes down.

**A skipped frame raises `[ark_full]`, so the next frame that draws draws
everything.** `ark_update` ran and moved the world; the screen did not follow,
and the difference has to be reconciled somewhere. Every erase in this package
is aimed at where something was last *drawn* rather than where the update last
moved it from — see §44.4 — so a single skip is survivable without this. But
that is an invariant six separate pieces of state have to keep independently
(the ball's `[ark_obx]`/`[ark_oby]`, the paddle's whole-lane erase and its
`[ark_padwipe]` gate, `[ark_dirty]`, `[ark_stat]`, `[ark_msg]`, and the
capsules' `[ark_puold]`/`[ark_shold]`), and one of them not keeping it cost a
stranded capsule on real hardware.

It is *also* true that the kernel repaints the window anyway — un-hiding goes
through `wm_show`, uncovering through `wm_paint_dmg`, and both end in
`W_PAINT`. That is a guarantee this package cannot enforce and does not own,
and §11.90/§11.91 exist precisely to make `W_PAINT` run **less** often. One
byte buys independence from a policy that is still being tuned.

`ark_abdismiss` settles the debt itself: it calls `ark_draw_all` on the
keystroke that takes the panel down — immediately, rather than up to a tick
later — and then clears `[ark_full]`, `[ark_msg]` and `[ark_stat]`, exactly as
`ark_render`'s own full branch does. Without that, every frame spent under the
panel would have queued a whole-board repaint for the worker to perform a
second time.

### 44.2 The keyboard has no key-up, so a hold is inferred from the repeat RATE

int 16h delivers keypresses and nothing else. There is no key-up, and a
typematic repeat is **byte-identical** to a fresh press, so neither "is left
held" nor even "did that key come up in between" can be asked. The **interval
between events** is the only evidence there is, and which interval you read it
by decides the whole feel of the paddle.

Every earlier version read the typematic **delay**, and all of its failures
were the same failure. A fresh press latched the paddle in motion for
`ARK_PKEEP` ticks, chosen at 11 to outlast the ~9-tick delay so that the first
repeat would arrive while the latch still stood; a repeat arriving inside it
meant *held*, and promoted the paddle to `ARK_PFAST`. That works, and it welds
the tap to the delay: the latch is what makes the paddle move, so a tap moves
for 11 ticks whether or not anyone wanted it to. At the old `ARK_PSTEP` that
was **44 pixels** — a whole paddle width, a fifth of the paddle's travel, for a
key already released — and the paddle could not be aimed at all. Shortening the
latch stalled the hold. Tapering the speed inside it made a hold decelerate and
then surge as the repeats landed, which read worse than the overshoot did.
Slowing it enough to keep the tap short made both the tap and the first half
second of a hold a crawl. With the delay as the discriminator, a tap and the
opening of a hold are the *same event*, so every number serves both and none of
them can serve both well.

**The typematic RATE is a far better discriminator, because nothing else
produces it.** Repeats arrive about 1.65 ticks apart at the usual 10.9 cps, and
no human taps a key seven times a second. So: two key events in the same
direction no more than `ARK_PRATE` ticks apart mean the key is **held**, and
nothing else does. `[ark_page]` counts ticks since the last event — aged by
`ark_do_paddle`, stopped at `ARK_PRATE` so it cannot wrap, reset by every key
event — and one comparison against it is the entire inference.

Everything follows from that:

- **A press whose neighbour is further off is the player pressing again**, and
  it simply restarts the tap. That is the answer to "can we tell a re-press
  from a repeat" — not from the event, which carries nothing, but from the
  interval, and only for intervals a human cannot produce. Measured: two
  presses stay two taps down to about **130 ms** apart, and read as a hold
  below that.
- **The tap stopped having to outlast anything.** `ARK_PTAP` is a pure feel
  knob now: 7 ticks, and `ARK_PTAP` × `ARK_PSTEP` is the whole tap, 35 pixels.
- **The first repeat of a real hold is 9 ticks after the press, so it reads as
  a re-press and restarts the tap** — which is exactly right, and is why a hold
  keeps moving through a delay it cannot see the end of. The *second* repeat,
  1.65 ticks later, promotes it.
- **Erring low on `ARK_PRATE` is the safe direction.** Too high and a player
  tapping to walk the paddle trips into a hold (3 puts that at ~4.5 taps a
  second, 2 at ~7). Too low — on a machine whose repeat rate is slower than
  `ARK_PRATE` — costs only the fast speed: every repeat then restarts the tap
  instead, so a held key still moves, just at `ARK_PSTEP`. The old scheme's
  equivalent failure was the paddle stalling.

`ARK_PHOLD` (4) is what each repeat of a confirmed hold refills, so it is also
exactly how long the paddle coasts once the key comes up — 32 pixels. It has
only to outlast the typematic rate, and tolerates one down to ~4.5 cps. It is
unchanged from the scheme above, and is the one part of it that was always
right: refilling `ARK_PKEEP` there instead charged the *delay* to the release
and sailed the paddle on for 88 pixels after the player let go.

The `or al, al` gate on the scan code is not optional: the numeric keypad
sends '4' and '6' with the arrow scan codes, so without it typing a digit
steers the paddle — the trap apps/notepad documents.

### 44.2.1 Three speeds, and nothing between them

`[ark_pspd]` holds one of exactly three values and **is** the paddle's state
machine:

| speed | value | set by | ends |
|---|---|---|---|
| stopped | 0 | `ark_do_paddle`, when the countdown runs out | — |
| tapping | `ARK_PSTEP`, 5 px/tick | a press with no repeat-interval neighbour | after `ARK_PTAP` ticks, mid-stride |
| holding | `ARK_PFAST`, 8 px/tick | the second of two events `ARK_PRATE` ticks apart | `ARK_PHOLD` ticks after the last repeat |

Nothing ramps, tapers, eases or coasts between them, and `ark_do_paddle`
changes the speed exactly one way — to 0, when `[ark_pkeep]` runs out. **The
abrupt stop is the point, not a compromise**: a tap that decays is a tap whose
distance depends on when you stopped watching it, and the paddle is aimed by
tapping.

Two details are load-bearing:

- **`[ark_pspd]` = 0 is what tells `ark_onkey` a hold is over.** The hold
  branch is `[ark_pspd] == ARK_PFAST`, and without the store to 0 a press
  arriving long after a hold had expired would be read as its continuation and
  take off at full speed.
- **`[ark_pacc]` carries the quarter pixels a fractional speed owes between
  frames**, exactly as §44.3.2 does for the ball, because `ARK_PTAP` ticks of a
  *whole* number of pixels gives a tap of 28, 35 or 42 and nothing in between.
  `ARK_PFAST` is `8 * ARK_VQ`, a whole number of pixels, so the accumulator
  never shows in a rally.

**`ARK_PSTEP` is halfway to `ARK_PFAST`, and that is the setting.** It was
tried at 1.25 px a tick and at 2, both of which made a tap a nudge — and made
the step up to a hold a 4x jump the player could feel, which is the "ramp" the
flat speeds were supposed to have removed. At 5 the tap reads as a *move*, the
promotion is barely visible, and the tap's distance rises with the speed
because `ARK_PTAP` is unchanged: 35 pixels, most of a paddle width, in 385 ms.
That is the trade the knob makes and there is nothing subtle left in it —
`ARK_PTAP` × `ARK_PSTEP` is the tap, and the two speeds are how far apart tap
and hold feel.

Measured on the running game: a tap moves 5 px a tick from its first tick and
stops dead at 35; two presses 300 ms apart travel 65 px and 150 ms apart 50 px,
both as taps; the same two 90 ms apart are a hold at 42 px; a press followed by
a 500 ms delay and four 91 ms repeats travels 98 px.

**One consumer had to stop reading the paddle's motion, and that is a real
distinction rather than a patch.** `ark_english` and the rail clamp ask a
physical question — how fast is this thing actually going — and `[ark_pvel]`,
the pixels moved this frame, answers it. `ark_throw` asks a different one:
which way did the player *ask* for. Those two agreed while the paddle moved 4
pixels a tick and stopped agreeing the moment it did not, at the 2 px a tick
`ARK_PSTEP` briefly was: a flick moved the paddle two pixels in the tick Space
was pressed and `[ark_pvel]` halved that to one, so a serve barely left the
vertical unless the player had held the key first. The mechanism had gone
quiet, not the intent — and reading the intent is what has kept the serve
stable while `ARK_PSTEP` was tuned underneath it three times.

So `ark_throw` reads the **state machine**, and has exactly three rungs to
match it: stopped, so nothing is being asked for, and the serve leaves straight
up; tapping, and it leaves at `ARK_THRTAP`; holding, and it leaves at
`ARK_THRHOLD`, harder. The last two are **3 and 4 pixels**, and they track the
two speeds rather than deriving from them: a flick that moves the paddle 5 px a
tick has to throw harder than one that moved it 2, or the aim stops matching
the gesture. `ARK_THRHOLD` is `ARK_VXMAX` exactly — the flattest angle the game
has, a ceiling a serve may *ask* for and nothing afterwards can exceed — and an
assembly-time `%if` in arkanoid.asm refuses a value past it. Measured: 0.00,
−3.1 and −4.2 px/frame.

A paddle held against a rail still serves off it, which the physical reading
would refuse. That is the intent answering, and it is the right answer: the
player asked for left.

### 44.3 The ball steps one pixel at a time

`ark_do_ball` walks the velocity with Bresenham, one axis per step, testing
after **each single pixel** — not one jump of (vx,vy) per frame. At four
pixels a tick a whole-vector jump steps straight over a brick's edge and out
the other side, and the ball tunnels through what it should have bounced off.

Stepping buys a second property that the drawing depends on: the ball is never
*inside* anything, because `ark_move1` reflects instead of taking the step that
would put it there. That is what lets the ball be erased with a plain
background fill. The one thing that can still overlap it is the **paddle**,
which moves under a parked ball, so the paddle is redrawn whenever the erased
rect reaches its lane. `ark_reflect` needs no geometry for the same reason:
exactly one coordinate changed, so that is the axis to reverse.

### 44.3.1 The paddle reflects, it does not re-launch

A paddle bounce **mirrors** — `vy` flips to `-[ark_vymag]` and `vx` is *kept* —
and then two things are **added** to the vx it kept:

- **Where along the paddle it landed**, `ark_zbias`, five zones of −2..+2.
- **How the paddle itself was moving**, `ark_english`, −2..+2 from
  `[ark_pvel]` — the pixels the paddle actually moved that frame, measured
  after its rail clamps so a paddle pinned against a rail imparts nothing.

The sum is clamped to ±`ARK_VXMAX`, because vx accumulates across bounces and
without a ceiling a rally converges on horizontal and stops coming down.

This replaced a zone table that **assigned** both components outright, and the
difference is the whole feel of the game: with the old table a ball arriving
steeply from the left and one drifting in from the right left the paddle
identically if they landed in the same zone, so a rally had no continuity and
read as arbitrary. `[ark_vymag]` exists for the same reason — it is the
authority on the rally's vertical speed, so a bounce restores the tempo it
already had instead of inventing one per zone, and it is the single number
Slow reduces.

**The serve is thrown, not aimed.** `ark_throw` gives it `vx` from **which key
is down** — §44.2.1's three rungs — because a serve has no incoming direction
to build on: the flick *is* the aim. A paddle standing still serves straight
up, which is honest rather than a hidden default: the player who wants an angle
flicks, and the one who does not chooses after the first bounce. It also means
the ball can be walked along the paddle before release. It read `[ark_pvel]` at
twice `ark_english`'s weight until §44.2.1 made the paddle move too slowly for
that to survive the halving.

Measured on the running game (with `ark_zbias` zeroed, so only preservation
and english can move vx): a stationary serve leaves with dx exactly 0; a
serve flicked right leaves at +21px per 0.18s sample and one flicked left at
−21; `|vy|` holds at 3px a frame across fifty-five samples and every kind of
bounce, where the old table would have swung it between 2 and 4 per zone; and
a ball that arrives at vx=0 leaves a paddle held right at vx=+2, which is
`ark_english` exactly. Those figures are in the whole-pixel units of the time;
§44.3.2 rescaled every one of them and moved the base speed, and the *shape*
each measurement was demonstrating is what still holds.

### 44.3.2 Velocity is in quarter pixels, and a paddle bounce has a floor

`[ark_bvx]`/`[ark_bvy]`/`[ark_vymag]` count **quarter** pixels a frame
(`ARK_VQ` = 4), and `ark_do_ball` carries the remainder of each axis across
frames in `[ark_accx]`/`[ark_accy]`. The walk in §44.3 is untouched: it is
handed a whole-pixel `(dx, dy)` exactly as it was, and still tests after every
single pixel. All that moved is where those two numbers come from.

The reason is that the speed ladder could not express anything between whole
pixels. It was 3, 4, 5 by wall, and 3 was sluggish while the only cure
available was a whole extra pixel a frame — a third faster in one step. The
base is `ARK_VYBASE` = 15 quarters = **3.75 px/frame** now, a third of the way
from the old base to the old ceiling, rising `ARK_VYSTEP` (0.75) a wall to the
same 5 px/frame ceiling. Two adds and two shifts a frame buy the whole range.

Three details are load-bearing:

- **The split is a FLOOR** (`sar` twice, not `idiv`), so the remainder stays
  0..3 and never changes sign. The long-run average is exact either way;
  flooring is what keeps the accumulator from oscillating about zero.
- **It is not reset on a bounce.** What it holds is less than one pixel of
  travel, so the worst a sign flip can do is delay the first step of the new
  direction by a single frame — cheaper than the special case.
- **`ark_english` and `ark_throw` scale at their exit, once.** Both work in
  pixels and clamp in pixels — `ark_english` from `[ark_pvel]`, `ark_throw`
  from the state machine — so the arithmetic in the body stays the pixel
  arithmetic it reads as; the `imul ARK_VQ` is the last instruction before the
  `ret`. `ark_zbias` is written `-2*ARK_VQ` … `2*ARK_VQ` for the same reason.
  `[ark_pspd]` and `[ark_pacc]` are in quarter pixels for the same argument
  again, one level down: see §44.2.1.

**`ARK_VXMIN` is the other half, and it is a fix rather than a rescale.** The
walk takes `max(|dx|, |dy|)` steps, so a ball going straight up moves `vymag`
pixels a frame while one at the vx ceiling moves the ceiling: a dead-centre
hit off a still paddle was *measurably the slowest shot in the game*, and it
came straight back down to where the paddle already was. So a **paddle**
bounce — and only a paddle bounce; a wall or a brick may still send the ball
vertical — floors `|vx|` at one pixel a frame. The sign is whichever way the
ball was already going, falling back in order to the paddle's own motion and
then to which half of the paddle it landed on (`[ark_zlast]`, banked by the
zone computation; zone 2 is the middle, so `< 2` is the left half and the test
can never tie).

Measured on the running game: the opening rally travels **68px in 1.005s**, or
3.72 px/frame at 18.2 fps, against the 3.75 asked for. A serve with a still
paddle rises and falls at a fixed x=318..321 as designed, and the paddle bounce
that follows leaves at 20px per 1.0s sample — `ARK_VXMIN` exactly, where it
used to leave at 0.

### 44.4 Powerups

One broken brick in `ARK_PUCHANCE` drops a capsule, up to three falling at
once; catching it with the paddle applies it. Five kinds: **E** expand, **C**
catch (the ball sticks until Space), **L** laser (Space fires a volley of two
bolts, each breaking one brick, up to `ARK_MAXSHOT` = 4 in the air), **S**
slow, and a **heart** for an extra life.

A capsule is identified by the **mark** drawn on it, not by its colour — five
colours cannot survive §39.4's reduction to three inks. Its height must
*contain* that mark: an 8px letter drawn one row into an 8px box hangs a row
below the rect that erases it, and every frame leaves a slice of the last one
behind. The laser bolt spawns clear of the paddle for the same class of reason
— spawned *on* it, the bolt's first erase punches a hole in the paddle it was
fired from.

**A volley leaves the two muzzles, and that is what makes the end columns
reachable.** `ark_do_paddle` clamps `[ark_px]` to
`[ark_rail] .. [ark_cwid]-[ark_rail]-[ark_pw]`, so a bolt fired from the
paddle's *centre* reached only x half a paddle-width inside each rail. Against
a 24px brick that margin is 22px unexpanded — a 2px slice of column 0 and
nothing more — and **one Expand takes the paddle to `[ark_pwmax]` = 68 and the
margin to 34, wider than a brick**, so columns 0 and `ARK_COLS-1` became
unhittable outright. The same on the CGA set: brick 20, margin 17 then 29.
That is the worst possible pairing, because Expand is common and a laser is
what the player reaches for when the ends of the wall are all that is left.

The muzzles sit **at** the clamp limits: `ark_draw_paddle` puts the left one at
`[ark_px]` and the right at `[ark_px]+[ark_pw]-2`, each 2px wide like the bolt
itself, so `ark_fire` spawns at those two x and a fully deflected paddle fires
from the rail — exactly where column 0 begins, since `ark_cell` derives a
column by dividing `x - [ark_rail]` by the brick width, and where the last
column ends. The reach is the whole wall at every paddle width, and a wider
paddle now widens the **spread** instead of narrowing the reach. Drawing and
firing read the same two numbers, which is the point: a muzzle the bolt does
not come out of is a lie about where the gun is.

A volley is a **pair and is fired as one**. `ark_fire` wants two free slots and
fires nothing with fewer, because the muzzles are drawn as a pair and one of
them firing alone reads as a dropped shot rather than a deliberate half-volley.
`ARK_MAXSHOT` is 4 rather than 2 for the same arithmetic: a volley costs two
slots, so four keeps two presses in the air, which is what the player had when
a volley was a single bolt.

**The extra life is a heart and not a letter**, which costs this module a
sprite: the kernel's ROM font is glyphs 32..126 (§6), so there is no character
to ask for. `ark_heart` draws it as six rows of horizontal runs through
`ark_fillc` — a table of `x1,x2` pairs, `0xFF` ending a row's second run —
because that is the primitive every other shape here already uses, and six
fills is cheaper than 42 pixel calls on a 4.77MHz machine. `ark_puletter`
carries 0 in that slot, and 0 is the flag `ark_draw_pu` branches on. It was an
**X** before, which said nothing about what it did; an extra life is the one
effect worth a glyph of its own rather than the one letter in five a player
has to learn by dying. It is also the **only** thing in the game that hands a
life back — `ark_apply`'s other four branches never touch `[ark_lives]`.

Two constants carry the game's difficulty and are set by feel rather than by
derivation. `ARK_PUCHANCE` is 8, half as often as it was: at 4 a wall of
bricks rained capsules. And `ARK_VYSLOW` — what one **S** takes off
`[ark_vymag]` — is 2 quarters, an eighth of the base against the *third* the
old whole-pixel step took off the old base, with the floor at `ARK_VYFLOOR`
(2.5 px/frame) rather than 2. A ball with no vy would never come back down,
which is what the floor is for; a ball at a third of its speed after one
capsule was what the step size was for.

**Three capsules are the frame's dominant cost, so each one is three fills
rather than six.** What matters here is the *count* of `gfx_fill` calls, not
the pixels: each carries `vga_rect_setup`'s clip, offset and mask arithmetic,
which for a 12×10 sprite dwarfs the writing. Two things pay for themselves:

- **The 1px frame is a solid rect the body is inset into**, not a
  `gfx_frame` — which is four fills inside the kernel. Black rect, then the
  body at `x+1 .. x+PUW-2`, `y+1 .. y+PUH-2`: two fills for what took five,
  and the same pixels. The pen is black on entry and the *body* fill leaves it
  the capsule's colour, so the letter has to set it back — drawn in the body's
  own colour it is invisible, which is exactly what happened first.
- **The erase is the vacated strip, not the whole capsule.** A capsule falls
  `ARK_PUFALL` rows a frame and is redrawn whole immediately after, so
  erasing all ten rows spent 120 pixels a frame on pixels nothing would ever
  see. `ark_wipe_pu` clears the rows between where it was last drawn and where
  it is now, and the full rect only on the frame it is caught or lost — the
  one case with no redraw behind it.

**`[ark_puold]` and `[ark_shold]` mean "where it was last DRAWN", and only
`ark_draw_pu`/`ark_draw_shots` may write them.** They used to be set by
`ark_do_pu`/`ark_do_shots`, one line before the move — which is the same thing
*only while every update is followed by a draw*. `ark_render` skips a frame
whenever the window is invisible, `wm_clip_set` returns CF=1, or the credits
panel is up (§44.7), and `ark_update` keeps running through all three. One
skipped frame and the erase is two rows off the pixels; the sliver it leaves
is permanent, because the next erase is aimed at the new position too. That is
the "a caught capsule was not cleared" report, and forcing the condition —
drop every third frame's drawing with capsules in flight — reproduces it as
red trails down the whole playfield: **1,186 stray pixels before the fix, 292
after** (the 292 being the capsules themselves).

Deriving the erase from the *update* is the trap; deriving it from the *draw*
cannot drift, however many frames are skipped. The falling case computes its
height as `[ark_puy] - [ark_puold]` and skips the fill entirely when that is
zero, which is every frame of the pause after a death.

Measured against the previous code with all three capsules and both bolts
pinned on screen: **24.1 → 15.2 `gfx_fill` calls per frame**, with the capsule
sprite pixel-for-pixel identical.

The same trick does **not** apply to the bolts, and `osapi_gfx_blit4` does not
apply to either. A bolt is 2×6 and moves 6 rows, so its old and new rects do
not overlap and two fills is already minimal. And a blit coalesces *runs*: a
framed capsule is 26 runs (one per frame row, three per body row) against the
three fills it costs drawn directly, so blitting it would be eight times the
work. Solitaire's card back wins from `blit4` (§43) because a lattice is
hundreds of calls collapsing into few runs; this is the opposite shape.

### 44.5 Sound comes from the worker

Every game event is a duration-limited `osapi_snd_tone` (§34): the rails and
ceiling, the paddle, a chipped brick, a broken one, the serve, a caught
capsule, the laser, a lost life, a cleared wall.

**Calling it from a worker is correct by construction**, and the SDK's
worker-safe list did not say so until this package needed it. `snd_req_inst`
(§34.3) stamps a grant with `[snd_inst]` when a callback is being dispatched
and with the **running task's own `T_INST`** when one is not — which is
exactly the worker's case — so the tone is attributed to this instance and
`snd_release_inst` releases it at teardown like any other. A duration-limited
tone self-expires through `snd_tick`, so the worker never has to come back and
turn it off. `osapi_snd_play` stays UI-task-only for a different reason: it
runs the clip with the scheduler locked, so a worker calling it freezes the
desktop rather than merely misattributing a grant.

A refusal (CF=1, something louder owns the speaker) is ignored on purpose:
sound is decoration here, and a game that stalled for it would be worse than a
quiet one.

### 44.6 Two metric sets, and a palette that cannot go black

24x10 bricks over six rows on VGA and Hercules, 20x7 over five on CGA's 200
rows, chosen by screen height exactly as apps/solitaire chooses cards, with
the paddle, the ball and the strip all scaling with them.

The brick palette is **not** a free choice. Everything is drawn on a black
field, so a row colour from §39.4's black class (0..6) makes that whole row
invisible on a 1bpp adapter — which is what `CBROWN` did to row 1 until a CGA
screenshot showed the row missing. The table is therefore drawn only from the
white class (12, 14, 15) and the dither class (7..11, 13), and alternates
between them so two touching rows stay apart once colour has reduced to three
inks. The rest of the palette follows the same rule: a two-hit brick carries a
white **notch** rather than a second colour, and an armed paddle grows two
**muzzles** rather than merely turning red.

---

### 44.7 The credits are a panel, and the worker must be held off it

Same shape as §43.8 — `OSAPI_ABOUT_SET`, a measured panel, dismissed by the
next click, key or menu pick — with one thing Solitaire does not have to
worry about: **a worker task drawing underneath it**.

The game loop is the worker (§44.1), so while `[ark_abon]` is set the worker
takes the gfx lock, arms its clip, sees the flag and unlocks without drawing.
The UI task owns the content until the panel comes down, and the game is
paused underneath rather than running invisibly. Every full repaint re-draws
the panel last, so it stays on top of whatever the resume puts back.

**`W_ONCLICK` is wired for the panel and for nothing else.** Nothing in this
game steers with the mouse, so the callback's whole body is `ark_track` plus
`ark_abdismiss` — but a panel that a key takes down and a click does not reads
as a hung window, which is the only reason the slot is non-zero. It is the
window's *content* that dispatches it: a click on the frame or the drop shadow
never reaches a callback, so the panel correctly survives one.

### 44.8 Losing the front pauses the rally, and the pause is sticky

A ball that keeps moving while its window is covered is deliberate — §44.1's
whole argument is that a dropped *frame* must not stop the game — and it is
exactly wrong when the player has gone to another window. They come back to a
lost life they never saw.

`ark_focuschk` runs once per frame, from `ark_update` just after the paddle
moves. If `[ark_mode]` is `M_PLAY` and `OSAPI_WM_TOP` (slot 0x0260, §20.3)
answers something other than this window, it banks `M_PLAY` in
`[ark_wasmode]`, drops to `M_PAUSE` and raises `[ark_full]`. Only `M_PLAY` is
interrupted: every other
mode is already still, and `M_READY` has the ball parked on the paddle where
losing the front costs nothing.

**Coming back to the front does not resume**, and that is the point rather
than an omission. A ball that starts moving the instant a window is raised is
a ball nobody was watching yet — the same reason a new life waits on Space. It
resumes the way every other pause does, through `ark_cmd_pause`, and it uses
`ark_cmd_pause`'s own `[ark_wasmode]` so the two cannot leave the mode in
different places.

Three things about where it runs:

- **It is on the WORKER, holding no lock.** That is what `wm_top` can be asked
  from: it takes no lock, touches no VRAM and answers out of `wm_zord`.
- **It does not draw.** `[ark_full]` makes the next `ark_render` repaint the
  board with its banner, under the gfx lock, where drawing belongs.
- **It needs asking rather than telling.** A package learns it *has* the front
  (`W_ONCLICK`, `W_PAINT`), but nothing tells it when it *loses* the front —
  `W_FLAGS` bit 1 only says visible, and a covered window is still visible.
  `OSAPI_WM_TOP` exists for this.

## 45. Tracker — the tenth package (apps/tracker/tracker.asm)

A four-channel ProTracker MOD player: `tracker.asm` (shell, menus, the file
dialog completion proc), `trkplay.inc` (the loader and the mixer) and
`trkui.inc` (the FastTracker II-style fullscreen interface). Prefix `trk_`,
mixer prefix `mp_`, UI prefix `tui_`. It ships with `BEVERLY.MOD` beside it on
the apps disk, because a player with nothing to play is not a demonstration of
anything — `os88disk.py` takes any non-`.o88` argument as a plain data file
(§24).

It is the most demanding client the API has, and it is the only thing in the
tree that exercises three features at once:

- **Ring mode** (§34.5, verb 0 with `AH` bit 0). Nothing else uses it. The
  mixer worker stages at `ringbase + (total & mask)` and feeds a *delta*
  forever, so a module plays with no close-and-reopen seam and out of a grant
  far smaller than the song.
- **A read past the 64KB horizon.** `BEVERLY.MOD` is 116KB, and the whole of
  it lands in one `OSAPI_FILE_READ` because the destination advances by
  SEGMENT (§18.4.1). This is what that ceiling's removal was *for*, and it
  used to be a separate slot, `OSAPI_FILE_READBIG`. The Disk window shows the
  file's size as 65535 — the directory listing's size field is 16 bits and
  saturates — which is a display limit and not a load limit; the chain walk
  uses the real length.
- **The mixer is a worker task** (§20.6), so the GUI stays live while it
  plays, and `OSAPI_GFX_DBUF` plus `OSAPI_GFX_SCROLL` keep the fullscreen
  pattern view from tearing under it.

The module blob is a **heap claim**, sized from `OSAPI_MEM_AVAIL` and capped
at 128KB. Its lifetime is the fence that matters: `trk_play_stop` closes the
stream and *drains* the worker's in-flight feed pass before the blob is freed
or replaced, because a mixer mid-fetch from a grant that has just been handed
back reads samples out of whatever claimed the memory next.

It is the app class the sound layer was built toward (§34.6: "a music player
plays … staged PCM via `OSAPI_SND_STREAM`"), and building it is what forced
the two kernel amendments it rides on: the worker-safe stream verbs plus ring
mode (§20.3/§34.5) and a read with no 64KB ceiling (§18.4.1 — which arrived
as `dskw_readbig` and is now just what `dskw_read` does). On the apps disks it lives in
the `APPS` folder (§24), appended after paint, with `BEVERLY.MOD` after it.

**Ported, not written here.** `trkplay.inc` and `trkui.inc` are byte-identical
to the tree this came from and `tracker.asm` differs by 57 lines — the
`retf` → `ret` conversion (§20.8 rule 5) and the memory API (KB not paragraphs,
the answer in DX not AX). Everything below is that tree's description corrected
where this one contradicts it, and every place that took a real correction —
§45.3, §45.4, §45.8, §45.11 — is the memory model.

### 45.1 Windowed is a splash; the app lives fullscreen

The entry proc creates an ordinary centred 420x180 window — a splash card:
the name, the loaded module's title, the key map, and *Press any key for
fullscreen*. That promise is kept literally: while the window has never been
fullscreen, **any** key or click enters fullscreen; afterwards F toggles and
Esc returns, and windowed keys drive the player normally (a module keeps
playing on the splash, which shows a live position line).

Tracker is the first shipped client of `wm_fullscreen` (§11.2), and its
lifecycle is the section's contract exercised end to end. Fullscreen is
entered ONLY from `W_ONKEY`/`W_ONCLICK`/`AM_ONCMD` — the contexts that hold
the gfx lock the slot requires; never the entry proc (no lock there). One
ordering detail is load-bearing: **`[trk_fs]` is flipped before the
`osapi_fullscreen` call**, because entering fronts and repaints under the
held lock — the `W_PAINT` that runs *inside* the call must already see the
fullscreen answer, or it paints the splash across the bare screen. A refusal
(CF=1, someone else owns the screen) flips it back. Exit is Esc in
`W_ONKEY`, the documented convention; the close and minimize boxes need
nothing, because `wm_fs_drop` runs on both paths (§11.2).

Under `WF_FULL` the menu bar is unreachable, so the bar's two commands
(File ▸ Open…, View ▸ Fullscreen) are duplicates of keys (L, F) — the
Arkanoid rule. `About Tracker` (`OSAPI_ABOUT_SET`) is the `[ark_abon]`
panel-in-content pattern verbatim: the flag is checked by the worker **under
the lock, right after the clip is armed**, and the whole frame is dropped
while it is set — but only the *drawing* pauses; the audio feed keeps
running, because a dropped frame should not stop the music.

### 45.2 The audio is a ring stream, and the worker feeds it

The player's architecture is one sentence plus one handshake: **the UI task
opens, closes and pre-mixes at open; the worker mixes and feeds; and the
close drains the worker first.** `mp_gen` is not reentrant (its cursors are
shared package bss), so the worker brackets every feed pass with
`[trk_mixing]` — set before the pass's own entry guards, cleared last — and
`trk_stream_close` spins that flag to zero after dropping `[trk_sopen]`.
Every UI path that resets the replayer, runs the `mp_gen` pre-roll, or
frees the module blob sits behind a `trk_stream_close`, so a worker
suspended anywhere inside a feed pass finishes it before the UI touches
`mp_*` state; a pass that enters *between* close and reopen sees
`trk_sopen` = 0 and falls out touching nothing. The drain is deadlock-free
(the feed path never takes the gfx lock the UI holds; pre-emption keeps the
worker running) and bounded (the fill loop re-checks `trk_sopen` per half).
Opening (`trk_play`, reached from
Enter/Space/P and the load completion proc) allocates one 16KB grant from
the `SND_SEG` pool (verb 7, once — force-freed at teardown like every
grant), pre-mixes two 2048-byte halves, stages them at ring offsets 0 and
2048, and opens a **ring-mode** stream (§20.3 verb 0 with `SND_OPENF_RING`
in AH, rate request 11,000 Hz, initial valid total 4096). From then on the
worker's every wake runs the feed *before* the draw, lock-free, on the
verbs the §20.3 amendment made any-task:

- verb 3 answers `consumed`, free-running 16-bit in ring mode; `lead =
  total − consumed` is exact across wrap because both counters are.
- While `lead ≤ 16384−2048` and fewer than 6 halves this wake: `mp_gen`
  renders 2048 bytes into `mp_outbuf`, verb 6 stages them at `grant +
  (total & 16383)` — a half never crosses the ring seam, because 16384 is a
  multiple of 2048, so the copy needs no split — and verb 1 publishes
  `total + 2048`.
- The polled `consumed` goes stale across the loop, which errs on the
  conservative side: `consumed` only grows, so the computed lead only
  over-reports fullness, never over-feeds.

Underrun is the normal quiet path (§34.5: the stream pauses, the next feed
resumes it within a tick or two). A watchdog-**ended** stream is different —
it never resumes, and the worker cannot close it (verb 2 stays
UI-callback-only), so the worker only flags `[trk_ended]` and stops feeding.
**F00 takes the same exit**: the effect stops the replayer on the worker,
whose pass keeps polling until `consumed` catches `total` (the stop row's
tail is heard) and then latches `[trk_ended]`. Every UI callback runs
`trk_reap` first, which closes a flagged (or F00-stopped) stream — so the
machine's single stream record is held no longer than the tracker's next
paint, key, click or menu command, which is the documented residue of
verb 2's UI-only rule. The 6-half cap bounds the
worker's lock-free burst at ~1.1 s of mixing per wake, so a wake can catch
up after a stall without starving the machine.

### 45.3 Loading is one read, and that is why the ceiling went

BEVERLY.MOD is 116,085 bytes. `dskw_read`'s count was 16-bit, so a file
≥ 65,536 bytes was `FERR_BIG` *unconditionally*, and this app is why the
kernel grew a second entry point for it — `dskw_readbig`, at slot 0x01E8,
which §18.4.1 has since folded back into `dskw_read` itself. Nothing about
the load path changed with it but the name and a `xor bx, bx`. It is the
whole client story of §50 + §18.4 + §38 in one proc (`trk_fdone`, the fdlg
completion):

1. Copy the ES:DI name out **first** — ES is `KERNEL_SEG` and the buffer
   dies with the call (§38.6).
2. Stop playback and close the stream — the close **drains
   `[trk_mixing]`** (§45.2), so no `mp_mixch` is left mid-fetch from the
   old grant — *then* clear `mp_loaded` and free the previous module
   grant: no reader may trust a blob about to move, and on the worker
   path the drain is what enforces that rule.
3. `OSAPI_MEM_AVAIL` → take `min(largest run, 128 KB)` in ONE
   `OSAPI_MEM_CLAIM` (the one-block rule, §50.3). Refusal is a status-line
   "Out of memory", not an abort.
4. `OSAPI_FILE_READ` with ES:BX = the grant at its first byte, DX:CX = its
   byte capacity.
   `FERR_BIG` reads back as "File too big" — a rare answer, because the
   heap is not a fixed arena: a 640KB machine measures 566KB of it and a 512KB machine about
   439KB, so a 116KB module fits both with room to spare (§45.8).
5. `mp_load` validates the hostile bytes (the §45.5 checklist) and answers
   CF=1 with its own verdict string, which goes straight to the status
   line; success starts playback inline — pre-mix, stage, ring open, all
   sanctioned UI-lock context.

The dialog itself opens on top of the fullscreen surface and works there
(§38 — `fdlg_grab` runs before the `[wm_fs]` branch). Its one residue is
documented: closing the dialog paints the menu bar over rows 0..MBAR_H−1
with no callback on cancel. The worker owns the fix: every 16th frame it
repaints the whole top band, so a cancelled dialog's bar strip lives for at
most a second.

### 45.4 Memory layout

Four stores, none of them guessed:

- **The package segment** — image + bss, including the mixer's 65×256
  volume table (16,640 bytes, built at load: `vt[vol][b] = (int8)b·vol»6`)
  and the 2048-byte `mp_outbuf`.
- **The module blob** — one heap claim (§50), sized
  `min(largest free run, 128 KB)` **in KB** from `MEM_AVAIL` at load time
  regardless of the module's actual size, and held until the next load or
  teardown. Consequence, stated honestly: while a module is loaded the
  Tracker's claim holds up to 128KB that other packages and other instances
  then cannot have. Against a fixed arena that claim would have been
  effectively *all* of it on a 512KB machine; against a 566KB heap it is a
  fifth.

  **...and then `trk_trim` gives the difference back.** The over-claim is
  unavoidable at claim time — the dialog's completion proc is handed a name,
  not a directory entry, so the size is not known until the read returns it
  — but it need not survive the load. One `OSAPI_MEM_REGROW` (§50.3.1) after
  the read shrinks the claim to `ceil(bytes / 1024)` KB, and shrinking is the
  path that **always succeeds in place**: the record's length changes and
  nothing moves. Measured on a 5,596-byte module: the claim goes 128KB → 6KB
  and the machine's heap use falls from 201KB to 79KB.
  <br><br>
  A claim-copy-free would need both blocks at once and may hand back a
  different base, which is why the over-claim used to be documented as
  unfixable. `mem_regrow` shrinking in place is what fixes it, and this is
  the clearest single example of what it was for.
  <br><br>
  The trim runs **before `mp_load`**, so no sample pointer exists yet to be
  invalidated even on the impossible path where a shrink relocated; and
  `mp_load` bounds every read against `[mp_bloblen_*]` rather than against
  the claim, so trimming to exactly those bytes cannot narrow what it may
  look at. A refusal is harmless — the app keeps the oversized claim.

  The claim holds the file verbatim;
  samples are addressed through
  normalized per-sample bases (`seg = blob_seg + (start >> 4)`), so every
  sample is reachable inside one 8086 segment window, and pattern *p* lives
  at segment `blob_seg + 67 + 64·p`, offset 12 — nothing ever offsets more
  than 64KB from one base, which is how a 116KB blob is walked on an 8086.
- **The stream ring** — one 16KB grant out of the **sound driver's** staging
  pool (verb 7, §34.6). Not a kernel segment: the pool belongs to whichever
  driver attached and **its size is not a constant an app may assume** — the
  Sound Blaster's is 20,480 bytes.
- Nothing else: no frame buffer, no second window.

All three grants are stamped with the instance and force-freed at teardown
(§50.2/§34.3), which is why the close box needs no code at all: the worker
dies inside `OSAPI_TASK_ALIVE`, and the kernel sweeps the stream, the pool
grant and the heap claim behind it.

### 45.5 The replayer is ProTracker, validated hostile

`trkplay.inc` is a period-native PT replayer (PAL: rate = 3,546,895/period)
with the ft2-clone's semantics as the reference. The load checklist runs
before one byte is trusted: magic at 1080 ∈ {`M.K.`, `M!K!`, `4CHN`,
`FLT4`}; song length 1..128; restart ≥ songlen → 0; the pattern count is
the max over **all 128** order bytes + 1 and `1084 + 1024·P` must fit the
32-bit file size; every sample's byte extent is clamped to the bytes
actually present (a truncated file yields short samples, never a wild
pointer — BEVERLY.MOD's 9 trailing bytes are why the check is ≤, not =);
volumes clamp to 64; loop fix-ups per the PT rules (loop start past the
data disables it, an overflowing loop length is trimmed, looping iff the
result exceeds 2 bytes). Per-sample play length caps at 60,000 bytes so the
mixer's 16-bit position can never wrap. At play time every period is
clamped to [113..856] **before** the step DIV — the clamp *is* the #DE
guard — and effect handling ignores what it does not implement rather than
faulting.

Effects, v1: 0 arpeggio, 1/2 porta, 3 tone porta, 4 vibrato (the exact
32-entry PT table), 5/6 slide combos, 7 tremolo, 9 sample offset, A volume
slide, B position jump, C set volume, D pattern break (BCD), E1/E2 fine
porta, E6 pattern loop, E9 retrig, EA/EB fine volume, EC note cut, ED note
delay, EE pattern delay, F speed/tempo split at 32 with **F00 = stop**.
Ignored honestly: 8xx pan (mono output), E3/E4/E5/E7 (waveform control and
finetune — v1 plays finetune 0). Timing is the PT model: tick rate =
BPM·2/5 Hz, `mp_gen` renders `mixrate·5/(2·BPM)` samples a tick, tick 0
reads the row, ticks 1..speed−1 run the per-tick effects.

The mixer accumulates per channel into a 16-bit chunk buffer through the
volume table and converts once: `out = 128 + (sum >> 2)` — four channels at
64 volume cannot clip. A muted or silent channel still advances its
position arithmetically, so unmuting rejoins the song where it really is.
Mixing throughput on a real 8088 is **not promised** (§45.8); the mixer is
honest about being a QEMU/286-era luxury.

### 45.6 The FT2 screen, parameterized by adapter

`tui_layout_init` asks `OSAPI_VIDEO` once and copies one of three layout
records plus one of two colour tables (the Arkanoid metric-record pattern,
by height):

- **640x480 VGA** — the FT2-proportioned screen: top desktop area y=0..171
  (position editor with a 5-entry order window banded on the current entry,
  TRACKER nameplate, BPM/Spd and Ptn/Ln boxes, status line, 2x2 volume-bar
  scopes, two FT2 button stacks, a 12-row instrument list + song-name box),
  pattern editor y=172..479 with **16 rows above and 18 below** the band.
- **720x348 Hercules** — the mid layout: same top blocks, scopes in one row
  of four, no stacks or instrument list, 14+14 rows, channel block centred
  in the 720.
- **640x200 CGA** — compact: one title/readout line, four inline bars,
  channel header, 10+10 rows.

The pattern view is the deliverable — the four FT2 tells, in order: the
black field cut by a **full-width current-row band** that rows scroll
*through*; **hex row numbers on both edges** (left only on CGA);
separator-ruled channel columns with `C-2 01 A0F` cells (the 1px `TC_SEP`
rules `tui_row1` redraws after each strip erase; `mp_cell2txt`'s pinned format:
note `...` when the period is 0, instrument `..` when 0, effect `...` when
effect and param are both 0); the desktop above with the position editor
top-left and the Play/Stop stack right. Colours are the FT2 "Arctic"
palette mapped to 16: bg 0, pattern text 9, band 7 with text 15, bevels
15/8, accent 1.

On 1bpp the table swaps whole (§39.4 discipline): **colour 9 never appears
on a mono adapter** — it reduces to black and pattern text would vanish
into the pattern field, the exact CBROWN lesson of §44.6. Mono is text 15
on 0, the band inverted (solid white, black text), faces black inside white
bevels, and the desktop the 50% dither.

Every erase+text pair obeys the §11.3 granularity rule the `fr_status` way:
one `osapi_wm_clip_test` per unit — a pattern row strip, a readout value, a
whole desktop element — and the pair is skipped whole when any of it is
covered, so a partly covered element goes *stale*, never half-blank. Pure
fills (backgrounds, VU bars) clip per pixel and are never gated. In
fullscreen the only thing that ever covers this window is the file dialog,
which is exactly when the gates earn their bytes.

The worker's frame (`tui_draw_dyn`) is change-driven: the pattern area and
position readouts redraw only when row/position/pattern moved, tempo
readouts on change, mute flags on toggle; the VU bars every frame (rise
instantly, decay 2 units a frame, so they read as needles); the top band
every 16th frame (§45.3's dialog-cancel rule).

### 45.7 Keys

| Key | Action |
|---|---|
| Enter | Play song (Right Ctrl in FT2) |
| Space | Stop / play toggle |
| P | Loop the current pattern (Right Alt in FT2) |
| Left / Right | Song position −/+ (`mp_setpos`) |
| Up / Down | Scroll pattern rows while stopped |
| 1..4 | Toggle channel mute (a click in that scope does the same) |
| L | Load… (the Standard File dialog) |
| F | Fullscreen toggle |
| X | XT mode toggle (§45.9 — also File ▸ the relabeling menu item) |
| R | Cycle the sample rate 11 → 22 → 44 kHz (§45.10 — also the Rate menu) |
| S | Smooth toggle (§45.11 — also View ▸ the relabeling menu item) |
| Esc | Exit fullscreen (windowed: ignored) |

The `or al, al` keypad gate of §44.2 applies verbatim: the numeric keypad
sends digits with arrow scan codes, so ascii is tested before any scan code
is trusted. There is no held-key inference here — every action is
edge-triggered, so no deadline machinery is needed.

### 45.8 The honest degradations

- **No Sound Blaster: a viewer, not a player.** `osapi_snd_caps` without
  `PCM_BG` refuses Play with a status-line message; loading, the pattern
  view, scrolling and the whole fullscreen surface still work. No silent
  tick-driven fake playback is attempted, and no FM fallback in v1 (FM is
  now worker-whitelisted — that is future work, not a promise).
- **512KB machine: big modules play.** A fixed ~107KB package arena could
  not hold a 116KB blob at all, and that limit is gone. The claim heap is not
  a fixed arena — it is everything above the kernel — so a 640KB machine
  measures 566KB and a 512KB machine about 439KB, and the largest MOD this
  player accepts fits either. `FERR_BIG` / "Out of memory" is still the
  answer when the heap genuinely cannot fund the claim; it is no longer the
  answer on an ordinary machine.
- **Mono adapters: the band carries the look.** The blue-on-black pattern
  text distinction dies by design; the inverted band, the bevels and the
  MUTE flags carry every state in shape, not hue.
- **Real-8088 mixing throughput is not promised in the default mode.** The
  kernel quantizes the 11,000 Hz request through the TC (§34.5), the mixer
  follows the granted rate's arithmetic but not its wall-clock cost on an
  8088; the floor machine still gets the viewer — and §45.9's XT mode is
  the mode that *does* aim at the 8088, opt-in and honest about its trade.

### 45.9 XT mode — playback sized for a 4.77 MHz 8088

Off by default on a 286-or-better — and **on by default on a tier-0
machine**: the entry proc asks `osapi_cpu_info` (§41.8) and a `CPU_8086`
answer pre-arms the mode with its menu item already relabeled, because the
machine this mode exists for should not have to find the toggle. Toggled
either way by the **X** key or the File menu's relabeling
`XT Mode: Off/On` item (the §12.2 copy rule applies: the item's string is
repointed and `OSAPI_MENU_SET` re-called — the Solitaire Deal-menu idiom).
Toggling while playing stops playback first (through the §45.2 drain);
the mode change is a table rebuild plus constants, never a mid-stream
switch. What it changes, and why each piece pays on an 8088:

- **Rate: 11,000 → 5,500 Hz.** The mixer's cost is linear in output
  samples; one 2,048-byte ring half now carries ~372 ms of audio, so the
  worker's whole feed cadence relaxes by the same factor.
- **The volume tables pre-scale the output stage away**: entries become
  `(int8)b · vol >> 8` (±31 — four channels sum inside a byte around the
  0x80 bias), so the default mode's 16-bit accumulator buffer, its
  `rep stosw` zero pass and its shift-and-bias conversion pass all
  disappear. The first audible channel *stores* `128 + vt[b]` straight
  into `mp_outbuf`, later channels *add* — a chunk with no audible
  channel is one `rep stosb` of 0x80. One table format per mode: the
  toggle rebuilds the 65×256 table (16,640 `imul`s — an intentional
  sub-second freeze on the machines this mode exists for).
- **The bounds check leaves the inner loop.** Each channel's chunk is cut
  into runs: one `div` computes a conservative sample count that cannot
  reach the sample/loop limit (`(limit − pos − 1) / (stepint + 1)`), the
  run is mixed with no compare at all, and only the approach to the
  boundary walks the checked/wrapping path a sample at a time. The XT
  inner loop is fetch, `xlat`, byte add, `inc di`, `add`/`adc` position,
  `loop` — ~95 cycles on an 8088 against the default path's ~160-plus-
  conversion, and it multiplies with the rate halving: ~7.9M cycles/s of
  mixing at 11 kHz becomes ~2.1M at 5,500 — under half the 4.77M budget,
  with silent channels (most MODs rest some channel most of the time)
  skipped for the price of the position advance.
- **The pattern view redraws per position, not per row.** The full 30+-row
  scroll repaint is the other 8088-killer (§45.6); in XT mode a row change
  repaints only the band's two hex row numbers (clip-gated per the §11.3
  granularity rule) and the readouts/VU bars, and the whole pattern area
  repaints when the song *position* (or the stopped-scroll view) moves.

An underrun under XT mode still takes §34.5's honest path — bounded
silence, resume on catch-up — so an overloaded machine degrades to
stuttering audio with a live UI, never a wedge. Verified in QEMU (both
modes play the §24 test module at the same pitch — the step math is
rate-invariant); the wall-clock claim itself is 86Box `make xt-sound`
territory, cycle-counted here and honestly not QEMU-provable.

### 45.9.1 …and on a real 8088 the pattern view stops being animated at all

§45.9's per-position repaint is still more than a 4.77 MHz 8088 has. On the
machine itself the view does not scroll and does not move: one line updates
now and again while the rest stands still, which reads as a hung display
rather than a slow one. The audio is fine throughout, and keeping it that
way is the point.

So on a **tier-0 machine**, and **only while something is playing**, the
grid is not animated. The row area is cleared once at the playback
transition and a **single banded line** — the row being played, drawn by the
same `tui_row1` the grid uses — is updated in place. One row of four cells
and two hex row numbers per row change, against thirty-odd rows of the same.
Everything above the pattern area is unaffected: the readouts, the order
window, the VU bars and the status line all keep following the music.

Three things about the gating are deliberate:

- **It keys on `[trk_cpu0]`, the MACHINE, not on `[mp_xt]`.** XT mode is a
  playback setting anyone can switch on with **X**, and a 386 running it can
  animate the grid perfectly well. The entry proc latches
  `osapi_cpu_info`'s answer into `[trk_cpu0]` next to where it pre-arms
  `[mp_xt]`, because those two facts stop being the same one the moment a
  user touches the toggle.
- **It keys on playback.** A stopped 8088 gets the whole pattern to look at,
  and Up/Down still scroll it — there is no deadline when nothing is
  playing.
- **The transition is noticed in `tui_draw_dyn`, not at the five places that
  can start or stop a module.** Play, stop, pattern-loop, a load, the §45.2
  watchdog: all of them move `[mp_playing]`, and one comparison against
  `[tui_lplay]` catches every one. `tui_draw_pat` then draws whichever view
  is now correct, so nothing else in the module has to know the mode exists.
  The comparison sits **above** the windowed/fullscreen split, because
  §45.9.2 hangs off it too and the transport changes either way; only the
  `tui_draw_pat` behind it is tier-0 and fullscreen.

The line is **centred in the cleared area**, not left at `TL_BANDY`. With
the grid gone there is nothing for the band's usual one-third-down position
to relate to, and a lone strip near the top of a large empty field looks
like a rendering fault. The three channel separators are ruled down the full
height once, at clear time: they cost nothing per frame and they are the
difference between a line floating in a black field and a line sitting in
the four-channel view it belongs to.

**The volume bars are 3 rows, not 8** (`TUI_VUH`), on every machine. A bar
is the one element redrawn on every frame, so its height is a direct
multiplier on the per-frame fill cost; three rows still read as a level
meter.

### 45.9.2 The status line carries the transport, and stopping parks the view

The status line is the only place the keyboard is ever named, and it used to
stop naming it. `[tui_msgp]` is non-zero from the first load onward, so
`tui_s_hint` — `'ENTER play  SPACE stop  L load'`, the line the app opens
with — never came back; and `trk_play` set no message at all on success, so
starting playback left whatever the load had put there and `'Stopped'` was
set once and then stuck with no legend after it. Between them the user got a
line that said nothing about what the transport was doing and nothing about
which key would change it.

`trk_transport` is the fix and it is one routine: it reads `[mp_playing]`
and writes `'Playing  SPACE stop  L load'` or
`'Stopped  ENTER play  L load'` — the state **and** the key that leaves it,
in both directions.

Two call sites, and both are needed for different reasons. `trk_play`'s
success path calls it so the line changes on the keystroke rather than up to
a tick later, which on a 4.77 MHz machine is the difference between a
responsive app and a dropped keypress. `tui_draw_dyn`'s `[tui_lplay]`
transition (§45.9.1) calls it because that is the **only** site that sees a
stop nobody asked for — song end, an F00, the §45.2 watchdog. It does not
matter that the two overlap on a user-driven stop: the second call redraws
one line of text with the same content. What matters is that `trk_transport`
deliberately does **not** touch `[tui_lplay]`; leaving that to
`tui_draw_dyn` alone is what makes a transition that happened while
`trk_render` was skipping frames (hidden, wholly covered, About panel up)
still get noticed when the frames come back.

**Stopping also parks the pattern view where the music got to.**
`tui_viewrow` reads `[tui_vrow]` while stopped, and that word is the Up/Down
scroll position — untouched since before playback started, so stopping
snapped the view back to wherever the user pressed play, discarding the one
piece of information a stop is *for*. `trk_transport` copies `[mp_row]` into
it on the stopping edge. That is a copy rather than a snapshot taken earlier
because `mp_stop` leaves `[mp_row]` alone, so the replayer's own row is
still valid at the moment the transport is observed to have stopped — from
the keystroke and from the watchdog alike.

### 45.10 The Rate menu — 11 / 22 / 44 kHz for the other end of the range

The XT trades fidelity for cycles; a 286/386 has cycles to spend, and the
**Rate** menu spends them: `11 kHz` (default, requested as 11,000),
`22 kHz` (22,050) and `44 kHz` (44,100 — the §34.5 wide-rate regime, so a
DSP ≥ 4.00; on an older card the open refuses err 2 and the status line
says so, the `bb_avail` honesty pattern). The active item is its own
`MENU_DIS`-disabled twin — the Solitaire Deal-menu radio idiom — and the
**R** key cycles the selection for fullscreen reach. A rate change while
playing stops playback first (the §45.2 drain), exactly like the XT
toggle; XT mode overrides the selection with its own 5,500 Hz while it is
on, and the selection returns when it is off. The mixer's cost is linear
in the rate: 44 kHz is 4× the default's samples — chosen for machines
where the default is loafing, refused honestly where it is not.

### 45.11 Smooth — the fullscreen redraw rides the §32 back buffer

The pattern scroll repaints 30-plus row strips erase-then-text, and on a
direct-to-VRAM path the CRT catches every intermediate state — the flicker
is architectural, not a bug in the strips. The cure is the §32 back
buffer: while it is armed, a worker draw burst renders to RAM and
`gfx_unlock` flushes the finished frame once. **View ▸ `Smooth: On/Off`**
(the relabeling idiom; key **S**; default Off — the flush cost is opt-in)
makes the tracker arm it via
slot 0x01F0 **on entering fullscreen** and hand back the user's previous
state on leaving; while Smooth is off, or where the slot refuses (mono
adapters — where the software renderer already IS the direct path — or a
heap that cannot fund the 150KB claim right now), fullscreen draws exactly
as before. That second refusal is a **live** condition, not a
boot-time verdict: `bb_avail` is about the adapter alone and the memory
question is asked of the heap every time the buffer is armed (§32), so
Smooth can be refused with Paint open and granted after it closes. Two recorded
consequences: the flush costs VRAM bandwidth (the §32 ~24× figure), which
is why the toggle exists — a slow-bus VGA machine can decline, and XT
mode's band-relight keeps the dirty rect small enough that the two modes
compose well; and a close **while fullscreen** takes the kernel's
`wm_destroy` safety net, which the app never sees — the buffer then stays
armed, a legal user-settable mode the Control Panel's Display page shows
and can disarm, recorded here rather than fenced with kernel machinery.

### 45.12 The scroll path and the delta-drawn VU bars

Two updates stopped repainting what had not changed. **The row scroll**:
when the view moves by exactly one row inside one pattern and position,
the row area's content is the same pixels eight rows away, so the redraw
collapses to two `gfx_scroll` calls (§5.5 — the upper and lower row
regions move separately, because the 11-row band between them breaks the
8-row rhythm) plus **three** `tui_row1` strips: the band, the strip that
just left it (band colours back to normal), and the row entering at the
region's far edge. Both directions work — Down while stopped scrolls the
other way with the mirrored three strips. Any other change (position,
pattern, a seek, the compact layouts' pagination) takes `tui_draw_pat` as
before, and so does a `gfx_scroll` refusal — the §5.5 whole-shape clip
answer while the file dialog covers the area, which is exactly when the
per-strip path's own clip gates are the correct renderer. XT mode never
reaches any of this (§45.9's band relight short-circuits first).
**The VU bars** keep a last-drawn width per channel and paint only the
difference — a rising bar fills its growth, a falling one erases its
shrinkage, a steady one costs nothing; `tui_el_scopes` zeroes the four
widths whenever it repaints the cells under them.

## 46. ArtfulType — the eleventh package (apps/artful/artful.asm)

A port of ActionRetro's **ArtfulType** — "a distraction-free Markdown
writing app for classic 68k Macintosh computers" (github.com/ActionRetro,
GPLv3 code; the name, icon and artwork are reserved assets, which is why
the package ships a hand-drawn nib icon and why the 128×100 typewriter
bitmap is carried as a conversion of the original's `splash_image.h` under
the user's own porting decision) — onto the §11.2 fullscreen surface.
Windowed, the instance is the splash card: the branding, version, URL, and
**New** / **Open…** buttons (plus a `File` menu on the real bar and `N` /
`O` / `W` keys). Any of them takes the whole screen; Esc or File > Quit
hands it back with the document intact, and the kernel's §11.2 safety net
covers a close while fullscreen. Sources: `artful.asm` (entry, callbacks,
worker, tables) plus `atdoc.inc` (gap buffer + layout), `atrend.inc`
(renderer), `atui.inc` (bar, menus, alerts, splash), `atedit.inc` (input,
selection, clipboard, undo), `atcmd.inc` (commands), `atfile.inc` (files),
`atimg.inc` (the artwork). `ARTFUL.O88`, APPS folder, after TRACKER.

### 46.1 The performance contract

Sized for the 4.77MHz floor: a printable keystroke costs a gap-buffer
store, a one-paragraph relayout (§46.3) and the repaint of that
paragraph's visual lines — each line ONE `OSAPI_GFX_BLIT4` (§5.4).
Scrolling is `OSAPI_GFX_SCROLL` for moves of up to three lines plus a
render of the revealed band; only mode/zoom switches, undo restores and
W_PAINT redraw the whole page. Nothing on the typing path walks the whole
document: caret motion never moves the gap (at_getb branches around it),
and the layout tail after an edit is one add-loop shift.

### 46.2 The document model — canonical markdown, live-preview rendering

The document is always canonical markdown (newline = LF; CR LF and lone CR
fold to LF on load, control bytes and >126 drop; saved bytes are the
buffer verbatim) in a gap buffer that is a **heap claim** (§46.9). A second
`OSAPI_MEM_CLAIM` block (64/32/16KB tried in that order, §50.3 rules;
force-freed at teardown like every claim) carries the undo/redo stacks and
the full-size clipboard; without it undo reports itself unavailable through
the menu gray and the clipboard falls back to 2KB of bss. There is no second styled
buffer and no sync machinery (the original's `BuildHiddenView` /
`SyncHiddenToCanonical` pair): **Writer mode is a rendering property**.
Styled lines hide the delimiters and draw the spans; the caret's logical
line renders RAW — the live-preview rule, which is what keeps caret
arithmetic exact while editing — and collapses to styled the moment the
caret leaves. Markdown mode renders everything raw at body size
(`ClearStyles`' rule). Toggle semantics per logical line: `**` bold, `*`
italic, `` ` `` code (suppressing the others inside), `~~` strike (which
the classic Mac original could not render), `[text](url)` inside one
visual line underlines the text and hides the rest; span state carries
across a WRAP but resets at every newline — the independence that makes
§46.3 cheap.

### 46.3 Layout — raw-width wrap, one paragraph at a time

`at_lstart[]`/`at_lattr[]` (2,048 entries: logical start; heading level,
continuation flag, span nibble at line start) describe visual lines. Word
wrap measures RAW characters in both modes, so layout is independent of
the caret and caret motion never reflows; only the heading CELL WIDTH
differs between modes (Writer scales headings). Geometry per (zoom 0..1,
level 0..3): cells 8/16/16/8 and 16/24/24/16 px, rows 10/20/20/12 and
20/30/30/22 — H1 and H3 bold, H2 plain. A document ending in a newline
owns a trailing empty line (the caret must live somewhere after it).
`at_relayout` rescans exactly the edited paragraph(s) into a 64-entry
staging window, converges at the next old paragraph start (+delta, equal
attr), splices, and add-shifts the surviving tail; overflow falls back to
the full `at_layout`, which bulk operations already afford.

### 46.4 The renderer — one line, one blit

`at_parse` turns a line slice into per-character visibility, style and
x-position arrays (the one hidden-set definition, shared by rendering,
click mapping, selection and Style > None). `at_compose` builds 1bpp rows
in a strip, styling ROM 8x8 glyphs itself (paint's §42 probe, copied to
bss at entry): bold = overstrike, italic = a two-step shear (top half
right by `scale` px), links = underline, strike = centre rule, headings =
bit-doubled/tripled scale-ups through 16-entry nibble tables. `at_expand`
widens 1bpp to packed 4bpp through 256×4-byte tables — white background,
or CLGRAY for code-span columns (solid gray on VGA, a §39.4 dither on
mono) — and one `OSAPI_GFX_BLIT4` delivers the line. Selection is an XOR
overlay folded in after each blit; drag-selection XORs only the delta
range per mouse sample. The caret is an XOR bar under strict on/off
bookkeeping (`at_caret_on/off`, always under the lock), which is what
makes the blink worker's toggling safe: the package's one §20.6 task
sleeps 9 ticks, re-checks its gates under the lock, arms the §11.3 clip
and toggles. The colours survive every adapter by construction: black on
white, dithered code cells, XOR selection/caret.

### 46.5 The chrome — the app draws its own Macintosh

Fullscreen makes the kernel bar unreachable (§11.2), which is exactly what
lets the app draw ArtfulType's: rows 0..19, BLACK with white titles in
Writer mode (main.c's `UpdateMenuBarLook`, done honestly because the bar
is the app's pixels), standard white in Markdown mode. The pull-downs are
Mac press-drag-release menus in the `sol_drag` idiom (§43): draw under the
held lock, unlock/yield/relock, sample the button by LEVEL; hover moves an
XOR bar, release flashes the pick three times, and the box is erased by
repainting the lines it covered — a package has no save-under, and a line
repaint is one blit. Items carry right-aligned `^`-shortcuts, gray
disabled states (Undo/Redo/zoom bounds, CLGRAY text), hand-drawn check
marks (Markdown/Writer), and separator rules. The modal alerts (Save
changes / About / errors) route every key and click while `[at_modal]` is
set and repaint what they covered on close; W_PAINT re-raises a live alert
a `wm_paint_all` crossed.

### 46.6 Commands — markdown.c on one buffer

Menu picks and ^key shortcuts land on the same `at_docmd` (the notepad
two-doors rule): ^N/^O/^S new/open/save, Save As, ^Q quit, ^Z undo
(Shift-^Z redo), ^X/^C/^V clipboard, ^A select all, ^B/^I/^K
bold/italic/code, ^L link. `WrapSelection`'s port toggles delimiter pairs
around the selection (outer- and inner-wrapped both strip); headings
replace whatever level the line has (same level toggles off); Link wraps
`[selection](` with the caret parked inside the parens — in Writer mode
the caret parks the paragraph raw, so the URL is typed into visible syntax
and styles itself on the way out; None deletes exactly the characters the
styled parser would hide. Undo/redo are whole-document snapshots (the
original's design: canonical text round-trips styling for free), 15 deep,
coalesced per typing run, in compacting stacks inside the heap claim.
Zoom has two sizes (Default/Large — one bitmap font scales by integers,
against the original's five point sizes), session-only.

### 46.7 Files

Open/Save ride the §38 dialog from fullscreen — the dialog window fronts the
surface, modality routes input, and the completion proc repaints the page
before any failure alert so nothing lands on the hole the dialog left.
Names live per instance (`UNTITLED.MD` seeded); the file slots read and
write whole documents in place (no staging copy: the gap parks at the end
for a save, so `ES:BX` is the claim itself, and a load folds line endings
in place inside it — §46.9). `FERR_*` becomes a
human sentence in an error alert. New/Open/Quit with unsaved changes ask
first — Save / Cancel / Don't Save, with Save continuing the pending
action through the Save As completion when the document is untitled.
### 46.9 The document is a heap claim, and it grows

The gap buffer was 20,480 bytes of **bss**, and the reason given for it was
that the app then worked with an empty heap. That reason was never true on
this kernel: a package's *region* is itself a heap claim (§20.1), so a
machine whose heap cannot fund a claim cannot load ArtfulType either. It was
a holdover from a kernel where a package got one 64KB segment for code and
data and there was no memory API to ask for anything else. What it actually
cost was 20KB of the 60KB an image and its bss share.

The text is `[at_dseg]:0000` now, `[at_dcap]` bytes, claimed by `at_entry`
*before* it creates the window — an editor with nowhere to type is not a
window worth opening, so a refusal is `LD_EABORT` and the loader says so,
which is why nothing below has to test `[at_dseg]`. bss falls from 42,165
bytes to 21,695, and image + bss from 59,198 of `APP_MAX_SIZE`'s 61,440 to
39,057.

**`at_dresize` is the whole of it, and the gap is what makes it more than a
resize.** Text is `[0, at_gs) ++ [at_ge, cap)`, so the high run always ends
at the buffer's ceiling — which is why the new gap end is simply
`newcap - T` and never a delta. Growing must slide that run *up* to the new
ceiling, backwards because the ranges overlap, and **after** the regrow;
shrinking must slide it *down* **before** the bytes past the new ceiling
stop being ours. Each ordering is the one whose failure is benign: a refused
grow has moved nothing, and a refused shrink (which cannot happen — a shrink
always succeeds in place, §50.3.1) leaves more heap held than the books say,
which wastes memory and cannot corrupt anything.

Three movements hang off it: `at_dgrow` from `at_ins` when a keystroke
spends the gap and from `at_insn`/the paste path when a run does not fit;
`at_load_named`, which opens the claim to `AT_MAXKB` before the read because
nothing knows the file's size until the read reports it, lands the file in
the document buffer itself, folds line endings **in place** (the fold only
drops bytes, so the write index cannot outrun the read index) and calls
`at_dfit` on both paths; and `at_cmd_newdoc`, which empties the buffer
*first* — so the shrink has no high run to carry — and then gives the heap
back.

**`AT_MAXKB` is 60, and it is not the heap's limit.** `at_gs`, `at_ge`,
`at_caret` and every entry in `at_lstart` are **words**, so a document can
never reach 65,536 bytes whatever the machine has; 60KB leaves the top of
that range alone. Lifting it means widening the gap buffer's whole
arithmetic, not asking for more memory.

The cost is one segment register. `at_getb` — the hottest routine in the
app, one call per character of `at_scan` — is `push es` / `mov es,
[at_dseg]` / fetch / `pop es`, about 32 clocks a character on a 4.77MHz
8088. ES is **preserved** rather than left holding the document because
`at_slice` is not the only caller that holds ES across a read and a silently
clobbered ES is a corruption bug, where this is a measured and bounded cost:
~2ms on the paragraph rescan a keystroke actually pays for, and a fifth of a
second on a full relayout of a *maximal* document — which happens on a load,
a mode switch or a zoom, and never on a keystroke. The move primitives
(`at_gapto`, `at_copyout`, the paste and the snapshot restore) switch DS as
well, through `at_dsegs`, and every cursor and count they need is latched in
a register first: past that call no package variable is readable, which is
the `dsk_copy_in` discipline of §18 applied inside an app.

## 47. Disabled controls — the greying standard (binding)

Every part of the UI that can refuse a click has to say so the same way. This
section is the whole of how, and it is binding on the kernel and on packages
alike. **It breaks silently**: get it wrong and the control still works, still
refuses, and still looks fine on the machine you tested it on — it just stops
saying anything on the other two adapters, or says the opposite of what it
means.

### 47.1 The seven rules

1. **Disabled is a FLAG, not a colour: `gfx_pen_dis` / `gfx_pen_live` /
   `gfx_pen_cf`.** They set `[gfx_color]` = `CDGRAY` *and* `[gfx_dis]` = 1
   together, and nothing in the kernel sets either half by hand. `gfx_pen_cf`
   takes the answer in CF, which is the shape every greying predicate already
   returns, so a call site is `call <ok-test>` then `call gfx_pen_cf`.

   It is a flag because the renderer has to do something a colour cannot
   express — `font_ink` masks a disabled glyph to a checkerboard on mono
   (rule 3) — and because inferring it from `CDGRAY` catches every other use of
   that colour. The first cut did exactly that and made Minesweeper's digit 8
   (§23) look like a menu item you cannot pick. A flag says what is *meant*;
   a colour only says what was *picked*.

   `gfx_unlock` clears `[gfx_dis]`, for the reason it clears the clip region
   (§11.3): it is valid for exactly one lock hold, so a site that forgets
   `gfx_pen_live` cannot leak dithered text into whatever draws next.

   `CLGRAY` remains a *decoration* colour — Minesweeper's covered cells,
   Arkanoid's rails, Recorder's centre line — and is never a disabled state.

2. **Grey the whole control, not its caption.** Every mark the control is made
   of takes the disabled pen: the radio ring, the checkbox square, the button
   frame, the icon, and the label. A black ring with faint writing beside it
   reads as a live control that someone mislabelled — which is exactly what the
   Sound page shipped as until it was reported.

3. **Grey does not survive 1bpp; the flag does.** Every middle grey rounds to
   black in text (§39.4) — a dithered 8x8 glyph costs half its strokes, which
   is the wrong trade for Piano's coloured letters and right for nothing —
   so a `CDGRAY` label used to be *pixel-identical* to a live one on Hercules
   and CGA. `[gfx_dis]` is the carve-out: `font_ink` masks a disabled glyph to
   a checkerboard, exactly as the 1bpp Macintosh drew a greyed-out menu item,
   and just as readable.

   Shapes never needed it — `gfx_ink` maps `CDGRAY` to the 50% dither, so a
   ring or a frame comes out dotted on mono — which is why rule 2's "grey the
   whole control" was load-bearing before this existed and remains the rule for
   **packages**, which have no way to reach the flag (§20.3 publishes no slot
   for it, deliberately: nothing has needed one). A package's disabled *label*
   still rounds to black on mono, so a package's disabled control must include
   a non-text mark — a frame, a box, an icon — and `rc_btn` (Recorder) is the
   reference.

   Words are still worth adding — `'Save Gif (NoRam)'` (§12.2), or a caption
   that says why (§31.3) — but they say *why not*, not *whether*. They stopped
   being load-bearing for kernel-drawn text when the renderer took the state.

4. **One predicate, three consumers.** The test that greys the control, the
   test that refuses the click and the text that explains it are the *same
   call* — `cpf_dbok` (§31.3), `cp_snd_rowok` (§31.7) — never three copies of
   the condition. Copies drift, and both drifts are bad: a control that looks
   available and refuses, or looks unavailable and works.

5. **Grey a fact, never a guess.** Grey when the answer is stable and knowable
   without doing the thing — no hardware (§51.2's `DSV_TIERS`), the wrong
   adapter, a driver that is not loaded. When the only way to know is to *try*,
   do not grey: attempt it and report what came back. Memory is the case that
   splits: `mem_avail` is a real predicate for an ordinary claim, so the
   back-buffer row greys on it (§31.3); `mem_claim_dma` has no predicate at all
   because the 64KB page rule is inside its scan (§50.3), so the Sound Blaster
   row does not grey on memory and the click reports instead (§34.8).

   The corollary is a cost rule: **a greying test runs on every paint**, so it
   must be cheap. If answering it means probing hardware, the answer belongs in
   a value someone already computed — that is what `DSV_TIERS` is for, and
   §31.7 records what re-probing cost when the page did it on a click instead.

6. **A refused click says exactly one thing.** Greyed → say nothing more; the
   control already said it, and a second explanation for something the user can
   see is disabled is noise. Not greyed but refused → the page owes words at
   the point of refusal, in the vocabulary it already uses for that failure
   (the Sound page reports `drv_errstr`, the loader's own strings).

7. **Every redraw path applies the same pen.** A page that repaints part of
   itself after a click — glyphs only, one row, one line — must run the same
   predicate the full paint runs. Miss it and a control's two halves disagree
   about being disabled, which is worse than either answer alone.

### 47.2 Verifying it

**A greying change is not done until it has been looked at on a 1bpp adapter.**
The two adapters differ in *kind*, not just in colour depth — a glyph becomes a
checkerboard, a ring becomes dotted — and VGA will show a perfectly convincing
grey for something that renders quite differently, or not at all, on the other
two:

```
python3 tools/hercshot.py build/qmp.sock 0x70000 out.png   # after make test VIDEO=herc HERCSEG=0x7000
make test VIDEO=cga                                        # and screendump normally
```

One trap on the CGA path: **QEMU double-scans mode 6 to 640x400**, so a
screendump cropped at 640x200 shows the top half of the guest screen and
nothing else. Take every second row.

### 47.3 Where the tree stands

Conformant, and worth reading as the reference:

- **`cp_snd_rowok` / `cp_snd_radios` / `cp_snd_paint`** (§31.7) — one
  predicate, glyph and label both, dotted ring on mono.
- **`rc_btn`** (Recorder) — greys the button frame with the label, so the
  frame dithers and the button reads as disabled on every adapter.
- **Paint's menu items** (§12.2) — text-only controls that also carry the state
  in words, `'Save Gif (NoRam)'` and five more. Since `font_ink` learned the
  dither those words say *why not* rather than *whether*, which is still worth
  having.
- **The Display page's back-buffer row** (§31.3) — `cp_dbradios` takes
  `cpf_dbok`'s pen for row 1's glyph. It used to draw it `CBLACK` beside a grey
  label, which rule 2 forbids and §31.3 had *already* described correctly; the
  code was the thing that disagreed.
- **Every disabled menu item everywhere** (§12.2, `MENU_DIS`) — fixed in the
  renderer rather than per app, which is why there is nothing to list. It was
  text and nothing else, so on Hercules and CGA it was pixel-identical to a live
  item and merely declined to highlight; `font_ink`'s `CDGRAY` dither made all
  of them, in the kernel and in every package at once, say so. The same change
  covers **`MENU_DIS` used as a RADIO MARK** — Solitaire's Draw One / Draw Three
  (§43) and Tracker's 11/22/44 kHz (§45), where grey means "this is the
  setting": the current item is now the dithered one, so a mono user can see
  which is which without a checkmark glyph and without spending a character of
  `MENU_MAXCH`.
- **"Close Window"** (§12.3) — greys from `wm_top` rather than beeping.
- **The file dialog's default button** (§38.8) — `fdlg_actok` behind the pen,
  the click and the Enter key alike, greyed while the name box is empty and no
  row is chosen. `fdlg_btn` stopped forcing `CBLACK` so the frame greys too.

**Left as beeps, deliberately.** The second half of the sweep asked which
refusals *ought* to grey and do not, and three answer no:

- **A press outside the modal dialog** (`fdlg_grab`, §38.2). Greying the whole
  desktop is not a control state, and the dialog's presence is the explanation.
- **Clock / Bounce / Disk at the instance cap** (§12.3). `MAX_TASKS` is 12 and
  the predicate is per-kind rather than one word; the cost is not worth a fact
  a user meets about never.
- **The Disk window's context menus** (`files.inc`, §22). Up One Folder at the
  root, Rename with no disk: silent no-ops. The recorded reason — four windows
  share an immutable `.text` descriptor, so nothing per-window can live in one
  — **is now obsolete**, because `ui_loc_gate` shows the swap can happen just
  before the menu drops, when exactly one window is relevant. It stays a
  no-op because it is a feature rather than a correction, not because it
  cannot be done. Whoever picks it up should read this paragraph first.

**Owed:** nothing else identified. §47.2 is the standing obligation — a greying
change is not finished until it has been looked at on a mono adapter.

**Packages have no `[gfx_dis]`, on purpose.** §20.3 publishes no slot for it
because nothing has needed one: a package's disabled control is covered by rule
2, whose non-text mark dithers through `gfx_ink` without help. If an app ever
needs disabled *text* with no shape beside it, that is when the slot gets
added — and adding one is an append, which §20.8 allows.

## 48. Missile Command — the twelfth package (apps/missile/missile.asm)

A port of Atari's 1980 arcade game onto the published package ABI, from the
original 6502 sources (`W3MAIN` / `W3DSUP` / `W3COMN`, "WWIII", project
23603, July 1979). Prefix `mc_`, embedded icon, one worker task, **no kernel
change of any kind**. Directory order on the apps disks stays pinned;
missile is appended last in `GAMES`. `MISSILE` is seven characters, so the
file name needs no truncating.

It runs windowed — seven eighths of the desktop band — or on the §11.2
fullscreen surface, `F` in and Esc out. No heap claim: every array is sized
by the arcade's own object counts and lives in the package bss, about 800
bytes of it, so this package costs its image and nothing else.

### 48.1 What is the arcade's, verbatim

The numbers are not re-invented. `mc_icbwav` **is** `ICBWAV`
(12, 15, 18, 12, 16, …) and `mc_crmwav` **is** `CRMWAV`, the smart-bomb
count that stays 0 until wave 6; both clamp at the end of the table exactly
as the 6502 clamps them. Six cities and three bases stand at
`CITY1H..CITY6H` and `MISB1H..MISB3H` on the arcade's own 0..255 field,
mapped onto whatever content rectangle the window actually got — which is
what preserves the shape of the board (base, three cities, base, three
cities, base) at any size. Ten ABMs a base (`MAXMIS`), eight ICBMs and eight
ABMs in flight (`NICBMS`/`NABMS`), seven ICBMs on screen (`MXICON`), first
satellite at wave `SPUTWV` = 2, first MIRV at `MIRVWV` = 1.

Scoring is `SETICS`: 25 an ICBM, 100 a satellite or bomber (`SPUTKI`'s
"4X ICBM"), 125 a smart bomb (`CMKILL`'s "5X"), all times
`min(6, (wave+1)/2)` — `SMULTI` capped at `MAXMUL`. End of wave pays 5 ×
multiplier per unused ABM (`ABMADD`) and 100 × multiplier per surviving city
(`ENDWV4`'s "4 ICBM POINTS/CITY"), tallied one at a time with a beep each,
which is what `ENDWV2`/`ENDWV4` do a frame at a time. A bonus city every
10,000 points (`BONINL`'s default interval). `mc_rad` is `OLDRAD`/`NEWRAD`:
0, 0, 2, 3, … 13, 13, … 1, 0, 0 over `EXDONE` = 27 frames. An explosion
below `[mc_lowest]` does no damage and pays nothing (`LOWEST`), which is
what stops a player farming points off the deck.

**`[mc_lives]` is `PLIVES`**: the number of cities the player is *entitled*
to, decremented when one is lost and incremented by a bonus, with the wave
transition regenerating cities up to it (`REGEN`). That one variable is why
a bonus city awarded mid-wave appears at the start of the next one, and why
the game ends when it reaches zero.

**All three bases come back every wave, with ten missiles each** — `NEWWV1`
writes `MAXMIS` into every `NMMISB` and `0E0` into `MBLEFT`, "ALL 3 BASES
ALIVE". Only cities are permanent losses. Getting this wrong is not a
fidelity detail: a player who lost all three launchers to one bad wave had
nothing to defend with ever again, and testing found wave 2 opening with no
bases and no way to fire.

### 48.2 A trackball becomes the mouse, and three buttons become one

The arcade aims with a trackball and picks a launcher with one of three fire
buttons. Here the crosshair follows the mouse — polled from the worker with
`osapi_mouse`, which is worker-safe (§20.6 rule 7) — and a click fires from
the **nearest live base that still has missiles**. Keys 1/2/3 still pick a
base outright, because the left and right launchers are what a player
reaches for when the middle one runs dry.

The click itself is not acted on in `W_ONCLICK`. The worker owns every
object in the game, so the UI task's whole job is to leave the target behind
in three words — apps/arkanoid's `[ark_launch]` with a point attached.
`[mc_fire]` is a **counter** rather than a flag so two clicks inside one
frame both fire.

**The crosshair is an XOR overlay**, drawn last each frame and undone first
the next, so nothing else ever draws while it is on the screen — the same
condition §32 requires of the window manager's own drag outline. Its arms
are deliberately long: the kernel keeps drawing the arrow cursor at the same
point (§11.2 — even fullscreen, the cursor stays live), and a short
crosshair simply hides underneath it.

### 48.3 There is no line primitive

`mc_line` is a Bresenham that **coalesces each horizontal run into one
`gfx_hline`**, and it draws the missile trails one *segment* at a time — the
two or three pixels a missile moved this frame, not the whole trail — which
is what makes fifteen missiles in flight affordable at 4.77MHz. A full
repaint needs no frame buffer for the same reason trails are cheap: every
missile carries the point it launched from and the point it has been drawn
to, so the whole trail is one `mc_line`.

**The erase has to be one pixel wider than the draw, and that is not a
nicety.** The trail is drawn as a chain of per-frame segments and erased as
a single whole line, and those two rasterizations are not the same pixels —
each segment is its own Bresenham between two rounded endpoints, while the
erase runs between the extremes. Neither differs from the true line by more
than a pixel in the minor axis, and "no more than a pixel" still left **104
of a measured 217-pixel trail on the screen**: every dead missile left a
dashed line that never went away, and a stalled game was a sky full of them.
`[mc_lfat]` grows each flushed run by one pixel in every direction, which
covers exactly that error and costs nothing — a run is a rect either way, so
it is the same number of `gfx_fill` calls.

The honest alternative — replaying the erase segment by segment — needs the
frame count since launch *and* breaks the moment `mc_render` skips a frame
or a smart bomb re-aims, because then the drawn segments are not the ones a
replay would produce. The dilation has neither failure mode.

### 48.4 The explosion scales, and its square root is free

The arcade's burst is 27 pixels across on a 256×231 field — a tenth of the
screen, and **that proportion is the game**: it decides how much sky one ABM
covers and therefore whether a wave is survivable. Left at a literal
13-pixel radius on a 560-pixel-wide window it was less than half as big in
relative terms, and the game became unwinnable for a reason that had nothing
to do with the design. `mc_escale` scales the whole `OLDRAD` ramp once per
layout, by `min(cw/256, ch/231)` in eighths — a **minimum over both axes**,
because this window is much wider than it is tall and dramatically so on
CGA's 200 rows, where a width-scaled burst would swallow the entire sky.

Scaled radii put a circle table out of reach (the radii now wanted would
have made one 1KB), so there is none. `mc_shrink` walks a running half-width
down until `half² + dy² ≤ r²`; because the half-width falls monotonically as
`dy` rises, it is decremented exactly `R` times across a whole disc — O(R)
for the entire circle, against O(R) multiplies per *row* for an honest
integer square root. `mc_erase_ring` runs two of them at once, one per
radius, to give back the annulus a shrinking burst vacated.

Growing, the new disc covers the old one, so one filled circle is the frame's
whole work; the colour cycles every frame either way, which is the arcade's
flashing and costs nothing because the disc is redrawn regardless.

### 48.5 Two ways a wave could never end

Both were found by leaving the game running, and both present identically —
a still screen on a machine that is otherwise perfectly alive, because the
worker is looping happily and simply has nothing left to do.

- **No target left.** `mc_launch_icbm` picks a living city or base to aim
  at. Once the last one is gone that refusal is *permanent*, and the first
  version treated it like the transient one above it (no free slot: try
  again next tick). The wave's budget stood forever, `mc_check_wave` waited
  on it, `mc_nextwave` never ran, and the THE END it would have reached
  never arrived. Spending the budget outright ends the wave on the next
  frame and the game with it.
- **A drifting on-screen count.** `MXICON` gates every launch, so a count
  that drifts high stops the wave launching and produces the same hang.
  Five launch sites and three death sites had to agree for a counter to stay
  honest, so `mc_recount` derives it every frame instead — an
  eight-iteration loop needs no agreement at all.

### 48.6 The palette cycles, and none of it may go black

`SETCOL` walks ten palettes at one per two waves; so does `mc_pal`. But
everything here is drawn on a black field, so a colour from §39.4's black
class (0..6) makes that object **invisible** on Hercules and CGA — the trap
§44.6 records. Every entry is therefore from the white class (12/14/15) or
the dither class (7..11, 13), and **within a palette the ground and the
cities come from different classes, as do the ICBM trails and the ABM
trails**, so the four things a player must tell apart stay apart once colour
has reduced to three inks.

Text is stricter still: all three figures in the score strip are drawn from
the **white class only**. A dithered 8×8 glyph on a black field loses the
half of each stroke the pattern masks out, and a 1px stroke has nothing
left — the wave counter was `CLGREEN` and on CGA it was not faint but
*absent*, zero lit pixels across the hundred columns it occupied.

A destroyed base is a **notch bitten out of the ground**, not a stump. The
first version drew two low stubs in the ground colour on top of the ground,
which is invisible on every adapter, so a launcher that was gone looked
exactly like bare terrain and a player could not tell it from one that was
merely out of missiles.

### 48.7 The rest of the game

The loop is the worker (§20.6, apps/arkanoid's shape), one frame a tick,
sleeping to a **deadline** rather than for a duration for the reason §44.1
measures. `mc_render` is the one lock hold a frame and obeys rule 5:
re-read the origin, check `W_FLAGS` bit 1, `wm_clip_set`; CF=1 skips the
frame and raises `[mc_full]`, so the next frame that draws draws everything.

MIRVs split partway down and their children come out of the **same wave
budget**, so a MIRV does not add ICBMs to a wave — it delivers them all at
once, lower down, which is the point of the weapon. The satellite (or
bomber; which shape is a coin toss, as `SOBJID` is) crosses the sky from
wave 2 and drops ICBMs out of that same budget. Smart bombs from wave 6 fly
slower and re-aim at a point displaced well to one side of a burst they see
coming, rather than reversing: reversing makes them hover, and a bomb that
never arrives is not a threat.

Sound is duration-limited `osapi_snd_tone` from the worker throughout, on
§44.5's argument. The credits panel is §44.7's: drawn inside our own
content, the worker held off it by `[mc_abon]` under the lock, the game
paused underneath.

## 49. TameGram — the thirteenth package (apps/tamegram/tamegram.asm)

A four-direction, dual-faction containment matrix, contributed by **Jason
Page** (store.amfile.org) and credited under its own name in the bar through
`OSAPI_ABOUT_SET` (§12.2). Prefix `tg_`, embedded icon, one worker task,
**no kernel change of any kind**. Directory order on the apps disks stays
pinned; tamegram is appended last in `GAMES`, and `TAMEGRAM` is exactly
eight characters so the file name needs no truncating. No heap claim: the
32x32 board is 1,024 bytes of package bss and the whole package is under 6KB
of image plus bss.

Units spawn at the centre of a fixed 32x32 matrix and fall toward one of the
four edges; the gravity vector re-rolls with every piece unless `F` locks
it. Each piece belongs to one of two factions, and a contiguous run of
**eight** same-faction cells purges — rows and columns both, `floor(len/8)*8`
cells a run. What survives settles along the current vector one step a tick
and the scan runs again, so a cascade is a state machine (`PLAY` → `FLASH` →
`SETTLE` → `FLASH` … → `PLAY`) rather than a loop inside one frame.

### 49.1 The worker's UPDATE holds the gfx lock, and that is the point

Arkanoid (§44) and Missile Command (§47) run their update lock-free and take
the lock only to draw. This one does not, and the reason is the shape of the
piece geometry: `tg_cell_of` resolves one of the four cells from the shared
words `[tg_tr]`/`[tg_tc]` into the shared words `[tg_cr]`/`[tg_cc]`, and the
**drawing path uses the same four words**. Every UI callback already runs on
the UI task under the gfx lock (§11/§12.2) — `W_PAINT`, the About handler, a
menu command — so with a lock-free update the UI task can repaint in the
middle of `tg_fits`, and the remaining cells of a trial position get
evaluated against a different origin. `tg_fits` then reports "fits" for a
position it never tested, `tg_step_grav` commits to it, and `tg_lock_piece`
stamps a flat index built from an off-board row.

Taking the lock around `tg_update` makes a tick atomic against every UI
callback, which is the whole fix. It is affordable for two reasons that are
worth stating because they are what make this a *local* decision and not a
precedent: the expensive half of a frame (`tg_draw_all`) was always inside
the lock anyway, and the expensive half of an update (`tg_scan_clears`, 2,048
cell reads) runs once per piece **lock**, not once per tick. Rule 4 still
binds — `OSAPI_TASK_ALIVE` is called at the top of the loop with the lock
free, because gfx_lock is not reentrant.

### 49.2 One bounds test, at the index

`tg_gidx` answers `CF=1` for a cell that is off the board instead of handing
back an index, and every reader and the single writer already go through it.
That is deliberate placement rather than defence in depth spread thin:
`tg_grid` is the last object in the bss and a package's region is an ordinary
heap claim (§50.3), so an index built from row 32 does not fault — it writes
33 bytes past the end of the claim, into whatever claim was made after ours.

`tg_lock_piece` skips an off-board cell rather than clamping it. So does
`tg_erase_prev`, and for a different reason: a *clamped* erase rect would
black out a cell on the far side of the matrix.

### 49.3 Nothing draws outside the content box

`tg_fillc`, `tg_framec` and `tg_str` are the only three ways this module
reaches the screen, and all three clamp to `[tg_cwid]`/`[tg_chgt]` before
adding the window origin. The gfx primitives clip to the **screen**, not to a
window (§11.3), and `W_PAINT` runs with no clip region armed, so a rect that
is one row too tall paints over the window below rather than being trimmed.

`tg_str` is all-or-nothing: `font_char` draws a whole 8x8 cell or none of it,
so a string that would leave the box is **dropped**, not clipped. A missing
HUD field is a layout bug you can see; a glyph on the neighbour's window is
one you cannot.

That clamp is what caught the layout. 32 8px cells plus a 28px HUD want 284
content rows, and CGA's desktop band has 136 — so the matrix used to hang 19
rows through the bottom of its own window, and a HUD laid out to 242px ran
off the side of a 128px-wide one. `tg_metrics` re-derives `[tg_csz]` from the
**live** content box on every callback and every frame, so it follows what
`wm_fit` actually granted rather than what the entry proc asked for: 8px
cells on VGA and Hercules, 3px on CGA. The content is never narrower than
`TG_HUD_W` (248px, one glyph clear of the widest HUD row) and the matrix is
centred in it by `[tg_bx0]`. A zero cell size is refused, because a zero cell
makes every rect inside-out and an inside-out rect is a stripe rather than a
small mistake.

### 49.4 Two colours are chosen at boot, not pinned

§39.4 leaves a 4bpp screen three greys and a 1bpp screen none. On VGA the HUD
band is `CDGRAY` under `CLGRAY`-bordered matrix; on Hercules and CGA both of
those land in the **dither** class, and white text on a 50% pattern is
legible only in principle. `[tg_hudbg]`/`[tg_frcol]` are set from
`OSAPI_VIDEO`'s `DH`: black band and a solid white border on 1bpp. The HUD
text itself is `CWHITE` on every adapter — §47's lesson, that a dithered 8x8
glyph loses the half of each stroke the pattern masks out, so text comes from
the white class and only a filled *area* may dither.

The factions straddle the two classes deliberately: `CYELLOW` (white) against
`CLGREEN` (dither), and `CLBLUE` (dither) against `CLRED` (white) in the
colour-blind palette that `C` toggles. So the two factions stay apart on all
three adapters without a second carrier.

### 49.5 A PLAY frame costs eight cells

`[tg_dirty]` says a frame is owed; `[tg_full]` says it must be a whole one. A
falling piece raises only the first, and `tg_frame` then erases the four
cells of `tg_snap_active`'s snapshot and draws the four the piece arrived at
— no content wipe, no 1,024-cell rescan, no HUD. Everything else raises
`[tg_full]`: a lock (the HUD's score moved and the board grew a piece), a
purge, a settle step, a mode change, a dismissed About.

`tg_lock_piece` drops `[tg_hasop]` at the moment it stamps, because those
cells belong to the board now and erasing them as a stale piece would rub out
what was just committed. The purge animation is the other partial path:
`tg_draw_flash_pages` rewrites only the marked cells, because a full wipe
every tick puts an intermediate black frame behind every shrink on the
direct-to-VRAM path.

### 49.6 One panel mechanism, two texts

The bar carries **two** menus: `Protocol` (New Protocol / Halt / Colour-Blind
/ Lock Vector) and `Help`, whose single item `How to Play` puts the rules and
the whole key list on screen — because nothing else in the app says what any
key does, and every one of them is a keypress with no on-screen affordance.

Both it and the credits are the **same** panel: `tg_draw_panel` measures,
centres and draws whatever `[tg_plines]` names, `[tg_panel]` says one is up,
and while it stands the worker neither updates nor draws, so a falling unit is
frozen rather than landing behind the text. Opening one over the other
replaces it. Any key or click takes it down.

It is drawn **inside our own content**, not in a window of its own, for the
reason §44.7 and §47 give: a package instance is bound to one window (§29), so
a second window's close box lands on the instance teardown path and takes the
game with it. There is no package-reachable equivalent of `fdlg`'s unowned
window.

Two things the second text needed that the credits did not:

- **A pitch ladder.** CGA's content box is 124 rows (96 of matrix, 28 of HUD)
  and thirteen lines at VGA's 10px pitch want 144. `tg_pmeas` walks
  10 → 9 → 8 and stops at 8 because a glyph is 8 rows tall. Without it
  `tg_str` would silently DROP the lines that fell outside the content box
  (§49.3), and a key list missing its last four entries is worse than a tight
  one.
- **`TG_PLEFT`.** A line beginning with byte 1 is drawn left-aligned at the
  panel's text margin instead of centred, and the byte is not drawn — the same
  shape as the SDK's `MENU_DIS`, and for the same reason: no second array, no
  per-line struct. Prose and a credit read well centred; a two-column key
  table centred is a ragged diamond, so the title and the dismiss hint are
  centred and everything between them is not.

### 49.7 Two bugs the port fixed, both invisible until they were not

**`retf`.** The contributed source ended all six callbacks — entry, paint,
key, click, menu command, About — in `retf`, which is `main`'s convention.
Here the kernel reaches them through the three-byte dispatcher in the
package's own header (§20.1/§20.2), so every proc is a near proc with a near
`ret`; a `retf` returns into the loader's stack frame and hangs the machine
at the first paint. This is the trap `docs/PORTING.md` names, and it is worth
naming again because it assembles perfectly.

**`tg_num` ate the y coordinate.** `div bx` takes its dividend in DX:AX and
leaves the remainder in DX, which is where the caller's y sits. The y is
banked in SI now. Left in DX it survived every counter that was still zero —
the loop does not run at all for 0 — and then, on the first piece to lock,
drew THREAT's digit at y equal to the digit's own ASCII code: a score of 3
put a stray `3` on board row 3 and left the HUD field blank. A wrong number
would have been obvious. A number somewhere else entirely reads as a
corrupted board.

## 50. memory.inc — the claim heap

Everything above the kernel, in one map the kernel owns.
`HEAP_SEG` (§2/§3) to the top of conventional memory as int 12h reports it;
KB-granular, segment-aligned blocks; a record per live claim; and one rule
that makes the whole thing worth having — **a claim is taken when it is
used and released when it is not**.

### 50.1 Why it exists

Before it, memory outside the pool was carved up by *constants*: `SND_SEG`
(64KB), `SAVE_SEG` (48KB), `VIEW_SEG` (16KB), `BB_SEG` (150KB). Each was
spoken for from boot whether or not a byte was ever written, none of them
could be reclaimed, and two of them were mostly empty in every configuration
that ever shipped. On the 256KB floor machine that is 278KB of address
space promised out of 256KB — the reason `docs/RAM-FIGURE-AUDIT.md` found
the Task Manager reporting 145K on a machine that was really 224K committed.

And a package that needed more than its 19.5KB region had **no way to ask**.
`apps/paint` therefore took linear 0x66000 unilaterally and read
`DB_MIN_KB` — a kernel policy constant — to guess whether the back buffer
would ever want that block (`docs/PAINT-NOTES.md` calls this "the least
defensible thing in this file"). One allocator retires the whole class of
problem: two packages cannot pick the same address, an app cannot outlive
its claim, a kernel feature and a package cannot both believe they own a
block, and the Task Manager can bill every byte to whoever holds it.

**The kernel owns the map, not the memory.** There is no per-access gating:
a claim is a segment and a length, and what the holder does inside it is its
own business, exactly as with a package's own region. Gating individual
accesses would need hardware this machine does not have.

### 50.2 The map

`mem_tab` — `MEM_MAX` (16) records × 6 bytes, kernel `.bss`:

```
MC_SEG   0  word  base segment, 0 = free record
MC_PARA  2  word  size in paragraphs (KB << 6)
MC_OWN   4  word  owner: 0..INST_MAX-1 = instance slot;
                  0xFF00 | tag = the kernel's own (MEM_K_SAVE, MEM_K_BB)
```

The record **is** the allocator, the `inst_tab` idiom of §29.2 rule 7:
occupancy is derived by walking the table, so freeing is one word store and
no free list can disagree with reality. First fit, restart past the overlap.

**Two ends, one heap.** `mem_claim` fits from the bottom upward and is what
data asks for; `mem_claim_hi` fits from the top downward and is what a
package's REGION asks for (§20.1). The asymmetry is not tidiness: a data
claim can move within its lifetime by being freed and re-claimed, and a
region can never move at all, because its base is its CS. Allocated from one
end they interleave, and one long-lived data claim landing mid-heap
permanently splits the space a package can be loaded into — it then fails to
load not because 8KB is not free but because 8KB is not CONTIGUOUS. From
opposite ends they meet only when the heap is genuinely full, and either
side may still use all of it when the other is not there.

**No owner may hold more than `MEM_OWNER_MAX` = 8 claims.** It does not
shrink the table — the table is sized by what a machine can hold — and that
is not what it is for. Since a region is a claim, exhausting the table now
means *no package can load*, so the cost has to fall on whoever caused it:
one app is refused its ninth claim rather than every other app being refused
its first. Eight is Paint's measured peak (scratch, canvas, undo, clipboard,
LZW, and the transient sixth it holds while trading a bigger clipboard for a
smaller) plus its region and one spare.

The kernel's own claims, and what each replaced:

| tag / owner | KB | taken by | replaces |
|-------------|----|----------|----------|
| `MEM_K_SAVE` | `MENU_SAVE_KB` = 20 | `menu_drop`, for exactly as long as a menu is on screen; **released before it returns** | `SAVE_SEG`, 48KB pinned |
| `MEM_K_BB` | `BB_KB` = 150 | `bb_set` when the Display page arms it; **freed when it is switched off** | `BB_SEG`, 150KB pinned |
| the Disk instance's slot | `VIEW_KB` = 3 | `fm_kinit`, per open window | `VIEW_SEG`, 4 × 4KB pinned |

Each has a documented "then don't" path, because a claim can be refused:
the save-under falls back to repainting on menu dismissal (§12.4), the back
buffer stays off and the Control Panel says **"Not Enough Ram"** (§31.3),
and a Disk window reads the global mount snapshot instead of a cache
(§2.3). None of them is a boot failure.

**Concurrency (binding).** Every operation runs inside one
`pushf`/`cli` … `popf` window — the `task_spawn` precedent (§8). Claims are
made from the UI task in practice, but `mem_free_owner` runs on a dying
package's own worker task inside `inst_task_die` (§29.4), so the two really
can meet.

### 50.3 The API (§20.3 slots)

```
osapi_mem_claim     AX = KB wanted
                    out CF=0 and DX = base segment; CF=1 refused
osapi_mem_claim_dma AX = KB wanted, CX = KB of the HEAD that must not cross
                    a 64KB PHYSICAL boundary (0 = the plain claim)
                    out CF=0 and DX = base segment; CF=1 refused
osapi_mem_free      DX = the segment you were given
                    out CF=0 released; CF=1 not yours / no such claim
osapi_mem_avail     out AX = largest free run in KB, BX = total free KB
```

The **ownership fence** is `ES`, stamped by the X stub from the caller's own
DS (§20.3): a package's owner word is **the segment it runs in**, so there is
nothing to pass and nothing to forge, and it answers from the *entry proc*,
where there is no window yet and no published instance — which is exactly
where an app sizes itself. `mem_own` asks the claim map rather than
`inst_tab` for that reason: a live claim starting at `ES` and owned by an
instance slot is a package's region, and by `MEM_K_DRV` a driver's image
(§51.3), and nothing else looks like either.

**`osapi_mem_claim_dma` is for a buffer a bus master addresses.** The 8237
has no register for address bits 16..19 — the page port holds them and the
chip never carries into them — so a transfer crossing a 64KB *physical*
boundary wraps to the start of its page and moves the wrong memory. `CX` is a
**head** and not the whole block because usually only part of a buffer is the
chip's: the sound driver's 32KB claim is a 12KB double-buffer-plus-ring the
card reads, under a 20KB staging pool the driver copies with `rep movsb`, and
constraining all 32KB would rule out every base in a page's upper half for
nothing.

The constraint is answered **inside the scan** (§50.2): a candidate whose head
would straddle bumps to the next page floor — the same shape as the bump past
an overlapping claim, monotonic, so termination is unchanged — and the block
returned is the lowest one satisfying both. It replaced a claim-test-reclaim
loop in the driver that held each failed block so the next attempt would land
elsewhere: correct, but it could hold 128KB to find 32KB and refuse on a
machine that had the room the whole time.

Two limits, both deliberate: a head bigger than 64KB is refused up front (no
page can hold it, and that is also what bounds the bump). **The claim record
carries it** — `MC_DMA`, the head in paragraphs, 0 for an ordinary claim — so
`mem_regrow` keeps the property a block was granted under even when it has to
move. It did not, and the hazard was documented here as a rule to remember
instead: a regrow that took path 3 searched as though the block were ordinary
and could land the head straddling, which the 8237 answers by wrapping to the
start of its page and moving the wrong memory, with nothing to see. Paths 1
and 2 never needed the care — the rule is on the HEAD, so a shrink and an
extend upward both keep it — and `MC_SIZE` went 6 → 8 to hold the word, 64
bytes across the table. Claim the size you need and do not grow it.

`osapi_mem_avail` is what a package sizes itself from. `apps/paint` used to
divide an int 12h figure and hope; asking the allocator is the difference
between "how much RAM does this machine have" and "how much can I have".

Rules for a package (none enforceable, all binding):

1. **Claim at startup, from a window callback or your entry proc.** The
   fence is `mem_own`'s claim-map test, not an instance scan — your REGION
   is a live claim from loader step 5 while your instance publishes only at
   step 9, *after* the entry returns (§21) — which is exactly what lets an
   entry proc size itself before it has a window or a record.
2. **Handle refusal.** CF=1 is a normal outcome on a small machine. Give up
   a feature, or put up a notice and stay running; do not read memory you
   were not given.
3. **Never assume a claim's address.** It is wherever the map had room.
4. **You do not have to free it.** Teardown does (§50.4). Freeing early is
   how you hand memory back mid-session.


### 50.3.1 `mem_regrow` — resizing a claim without needing it twice over

Growing a claim used to mean claim-new, copy, free-old, which needs **old +
new free at once and contiguously**. A heap with plenty of total room refused
resizes it could afford, and the app reported "not enough memory" over
hundreds of free KB. `mem_regrow` (slot 0x0238) takes three paths, and the
first two move nothing:

1. **Shrink or level** — the record's length changes and that is all. The
   tail is free for everyone else immediately.
2. **Grow with free paragraphs directly above** — extend in place. This needs
   only the *difference*, not old + new, and it is what a canvas dragged
   bigger hits nearly every time.
3. **Grow with something above** — a new block, chosen **highest-fit** so the
   grower lands at the top with the free space above it; the kernel copies
   and frees the old block. Only this path pays for a move, and it is the
   same copy the caller used to make itself.

The caller must always take the base back from DX: a grow that moved leaves
the old segment pointing at memory that is no longer theirs.

**The slot needs `osapi_mem_regrow` in front of it, and went without one for
its whole life.** `mem_regrow`'s ownership fence is `cmp [si+MC_OWN], bx`, and
BX is an *implicit* input — `OSAPI_XSTUB` stamps ES from the caller's DS and
stops there, exactly as it does for `mem_free`, which then calls `mem_own` to
turn ES into an owner. The regrow cell called `mem_regrow` **directly**, so the
fence compared an owner segment against whatever the package happened to leave
in BX. It never matched. The slot always answered CF=1, no package could ever
resize a claim, and `apps/paint`'s fragmentation fallback — the entire reason
this routine was written — had never once run.

That is the failure mode of a fence with an implicit input, and it is worth
stating as a rule: **a refusal that means "you got the calling convention
wrong" is indistinguishable from one that means "no room"**, so the caller
reports the honest-sounding message and everyone believes it. Both shipped
callers did. The lesson generalises to every X-stubbed slot whose body takes
an owner: the stub supplies it or nothing does.

**It does not compact the heap**, and cannot. A claim's base lives in its
holder's own bss — a package's `[pt_base]`, the kernel's `[bb_seg]` — and
nothing in `memory.inc` can reach in and rewrite those, so sliding somebody
else's block down would hand them a pointer into memory that stopped being
theirs. Real compaction needs a relocation callback every holder implements.
Until there is one, the fix is to stop *creating* the fragmentation, which is
what path 2 does: a claim that grows in place never leaves a hole behind it.

**So "`mem_claim` refuses while the total free would have sufficed" has exactly
two cures, and they are a decision rather than a bug fix:**

1. **A relocation callback.** The only thing that makes real compaction safe,
   and it is an ABI addition every holder has to implement — a package, the
   back buffer, the menu save-under, the sound driver's DMA claim (which
   additionally may not move at all, §50.3's page rule). It is not a change
   `memory.inc` can make on its own, and it is not one to make speculatively.
2. **Better placement, which needs no ABI at all.** `mem_claim` is a
   bump-and-retry **first fit** going up and a highest fit going down; a
   **best fit** would put a small claim in the smallest hole that takes it and
   leave the big runs whole. That is contained to one scan — but the retry
   loop's termination rests on the candidate base only ever moving *forward*
   (past an overlapping claim, or up to the next 64KB page floor, §50.3), and
   best fit means visiting every hole rather than stopping at the first. It is
   a real option and a real rewrite of the one routine in the kernel that must
   not be subtly wrong.

Neither is written. The measurement that should come first is whether the
refusal actually happens in practice: `mem_avail` already answers with the
largest run *and* the total, so the two are distinguishable today, and nothing
in the tree has yet reported a refusal where they differed.

### 50.4 Teardown

`mem_free_rec` sits beside `snd_release_rec` at all three §29.4 teardown
sites — `app_close_win`'s task-less path, `inst_task_die`, and
`inst_pkg_alive`'s window-less case — and releases every claim the dying
instance holds. That is what makes rule 4 above true, and it is the reason
`files.inc` needs no close hook for its view cache: the Disk window's claim
is owned by the Disk *instance*, and the instance's death frees it.

### 50.5 What the Task Manager shows

The heap is the RAM figure's fourth term (§28), and unlike the constants it
replaced it is *live*: arm double buffering and the figure rises 150K, close
Paint and it falls by whatever Paint held. `mem_claimed_kb` sums every
claim; `mem_kernel_kb` sums only the `0xFFxx`-tagged ones, so a package's
claim lands on the package's row rather than on System's.

## 51. driver.inc — loadable drivers

The kernel carries what every machine has. What only *some* machines have is
a **driver**: an ordinary file on the system disk, loaded into the heap on
demand, publishing a small table of services the kernel dispatches to. The
first one is sound (§51.4) — the OPL2 and Sound Blaster tiers whose code
would otherwise be resident on a 128KB machine that has neither card.

The whole subsystem rests on one enabling change: **the system disk is a
FAT12 volume now** (§19.3). A driver is a file on it, the settings that say
which drivers load are a file on it, and both are reached through the file
API that already existed.

### 51.1 A driver is a package that is not an application

Same 32-byte header, same `org 0`, same paragraph-aligned heap claim, same
three-byte dispatcher at `PKG_DISP` — so `drv_call` is `wm_pkgcall` with the
far pointer taken out of a driver row instead of a window record, and a
driver author writes near procs with near `ret`s exactly as a package author
does. Four things differ, and each is doing work:

- **It is a `.DRV` file.** The mount types a directory entry as an
  application only when its extension is `O88` (§19), so a driver is *data*
  to the file manager and can never be double-clicked into the loader.
- **Its header version is 4.** A package is 3, so if one ever did reach
  `ld_check_hdr` it would be refused there too. Two independent gates,
  because "the kernel ran a driver as an application" is not a failure mode
  worth one gate.
- **It has no instance record**: no dock tile, no Task Manager row, no
  window, no `I_CYC` billing. Its IMAGE is a kernel claim (`MEM_K_DRV`), so
  the Task Manager counts it under System, which is what it is — **and so are
  the bulk buffers it claims for itself**, which is less obvious and had to
  be made true. Those carry the driver's own SEGMENT as their owner word,
  because `mem_own` answers with `ES` and a driver has no instance to name
  instead — exactly like a package's data claims, but with no row to be
  billed to. They were therefore in the `HEAP` and `RAM` totals, and in the
  memory map's bands, and in no line of the list: on a 128KB machine with a
  Sound Blaster that is 32KB of DMA buffer belonging to nobody. `mem_sum_kb`
  asks `drv_owns_seg` as well as testing for a `0xFFxx` tag, so `System`'s
  `HEAP` column now equals the `HEAP` total whenever nothing else holds a
  claim.
- **Its bss ships inside its image**, zero-filled on the floppy by
  `tools/os88drv.py`. A package's bss is claimed by the loader because a
  package's is tens of KB and its file arrives through a peek-then-size
  dance; a driver's is a few hundred bytes, and paying for them on disk buys
  a load path with **exactly one claim in it** — made at the size the
  directory entry already reported, before a byte is read. Anything bulk (a
  DMA buffer, a ring) is the driver's own `OSAPI_MEM_CLAIM` at attach.

### 51.2 The contract

The entry proc is the only thing the kernel calls by offset; everything else
it reaches through the table that entry returns.

```
in:  AL = verb, DS = CS = the driver's segment, ES = KERNEL_SEG
DRVV_ATTACH (0)  probe + hook.  out CF=0 and SI = the service table;
                 CF=1 = no hardware, AND NOTHING WAS HOOKED
DRVV_DETACH (1)  silence, unhook, restore, free. Cannot fail.
DRVV_TIER   (2)  in AH = how much of yourself the user wants (SND_RT_*,
                 34.8). out CF=0 and SI = the service table, RE-COPIED
                 because a tier change alters it; CF=1 and AL = a DRVE_*
                 saying why not, with the table untouched. OPTIONAL - the
                 kernel sends it only when a setting asks for one.
```

**Attach must be all-or-nothing** and **detach cannot fail.** The first
because the kernel frees the image the moment a driver says no, so anything
it left behind — an interrupt vector, a port, a claim — outlives it by
definition. The second because the user turned it off; there is no answer
but yes.

**`DRVV_TIER` is for a driver whose tiers cost different amounts of memory**,
and it inherits both halves of that rule: turning a tier *off* cannot fail, and
turning one *on* is a claim and therefore can. Its refusal codes are the
loader's own `DRVE_*` (§51.3), so the Control Panel names a refused tier change
with the string it already has for a refused load — one vocabulary for both. A
driver with a single tier need not implement the verb at all; it answers `CF=1`
and the caller reports that, with nothing to undo. What tiers a driver *has* is
`DSV_TIERS` below, and this verb must never change it.

The service table, in the driver's segment, copied whole by the kernel at
attach so every later dispatch is a near read plus one far call:

```
DSV_CAPS    dw  capability bits it ADDS to OSAPI_SND_CAPS
DSV_FM      dw  near proc behind slot 0x00F8      (0 = none)
DSV_STREAM  dw  near proc behind slot 0x0100      (0 = none)
DSV_TICK    dw  near proc called from snd_tick - INSIDE IRQ0, at IF=0
DSV_RELINST dw  near proc: AL = an instance slot being torn down
DSV_NAME    dw  -> a NUL sink name, in ITS segment
DSV_TONE    dw  near proc: the tone tier's sink while it is loaded
DSV_TIERS   dw  bitmap of 1 << SND_RT_*: the tiers it COULD provide
```

**`DSV_TIERS` is the only cell that says what a driver *could be* rather than
what it *is*.** Every other cell is the running configuration and `DRVV_TIER`
rewrites them; this one is decided at attach from what the probes found,
cleared at detach, and **never touched by a tier change**. It exists so the
Control Panel can grey a tier the machine does not have without asking the
driver to probe again — see §31.7 for what that costs and why the answer cannot
change between attach and detach. 0 means the driver did not answer, and the
panel then assumes it can do everything.

**Widening the table means rebuilding every `.drv`.** `drv_publish` copies
`DSV_SIZE/2` words, so a kernel that knows about a new cell reads past the end
of a driver built before it. Every driver is in this tree and `make` rebuilds
them all, which is the same reason `APP_MAX_SIZE` can be mirrored in three
places (§20.1).

The copy is the `dsk_get_dir` idiom in a new place: every consumer downstream
is then an ordinary near read with DS = KERNEL_SEG, and `snd_tick` — which
runs inside IRQ0 — does not have to point a segment register anywhere to find
out whether it has work.

**`drv_svc_call` takes NO GENERAL REGISTER, and that is a contract.** Every
one of them is an argument to something in the sound ABI (§34.2, §34.5): AL
the verb, AH a handle or sub-op, BX the FM frequency *or* the caller's
segment, CX a length, DH the requesting instance, DX a rate, SI and DI the
two ends of a staging copy. So the dispatcher lives in memory as a far
pointer (`drv_fptr`/`drv_fseg`, armed by `drv_publish`, cleared by
`drv_release`) and the service selector arrives in **BP**, which nothing in
the ABI uses and which the routine is documented to clobber anyway.

It ate two registers in turn before the gate packages caught them, and both
failures were silent in the same way:

- **the driver row in BX** became the FM frequency. No OPL2 block fits
  15,000 Hz, so every FM call came back refused — while *tones*, which pass
  AX, worked perfectly. `tests/fmtest` found it.
- **the service selector in DI** became the staging destination. `DSV_STREAM`
  is 4, which is inside no grant, so every verb-5/6 copy refused as out of
  range and every open that staged first failed with it. `tests/sbtest` found
  it.

The lesson is written into the contract rather than the changelog: a
dispatcher that consumes an argument register is a dispatcher that will
consume a *different* one next time the ABI grows.

**`DSV_TONE` is the interesting one.** Publishing it *moves the tone tier off
the PC speaker* onto the driver's hardware. An OPL2 publishes it, because an
FM note is two register writes and then zero CPU, and moving tones there
leaves the speaker for the exclusive-clip tier that has nowhere else to go
(§34.4). A Sound Blaster does not.

### 51.3 Loading, and what happens when it fails

`drv_load` is the package loader's order (§21) with the instance half
removed: mount A:, size the file out of the mount snapshot, claim exactly
that, read, validate the header against the image that actually arrived,
attach. Anything that fails after the claim frees it, so a refused driver
costs nothing but the time.

**Every failure is survivable and none of them stops the boot:**

```
DRVE_DISK   no readable system disk in A:
DRVE_NOENT  it is not on that disk
DRVE_BAD    not a driver image
DRVE_MEM    the heap cannot fund it
DRVE_HW     it loaded and found no hardware
```

The ordering at boot is binding. **`drv_boot` runs before the desktop's first
paint**, so a machine whose sound driver loads has sound from the first
frame. **`drv_notice` runs after it**, because a window cannot go up on a
screen that has not been painted — so a failure is banked in the row and
reported later, never where it happens.

`drv_notice` opens the **Control Panel on its Drivers page** rather than
putting up a notice of its own, and that is a design rather than a saving:
the page already names every driver, already says what its last attempt
answered, and is the one place the user can do something about it. A notice
window would have said the same words and then made them go and find this
page. The `[cp_sel]`-before-`KIND_CTRL` precedent is the menu bar clock's
(§31.5).

A machine with **no system disk at all** is not told about drivers it never
enabled: only a row whose `DRVR_WANT` is set counts as a failure.

### 51.4 Unloading, and why detach comes first

`drv_unload` detaches, then frees — never the other way round. The detach
verb is what silences the chip and unhooks the IRQ, and freeing the claim
under a live interrupt vector points that vector at whatever claims the
memory next.

Dynamic load and unload are the same two routines the boot uses, so there is
no second path to keep in step: the Control Panel's checkbox calls
`drv_load` / `drv_unload` on the spot, mounting A: on demand. **The box shows
what is LOADED (`DRVR_SEG`), not what the settings file wants** — a driver
the user enabled on a machine with no card is unchecked, with the reason
under it.

### 51.5 SYSTEM.CFG

A small file in the system disk's root, written through the ordinary file API,
so it is an ordinary file — deletable, copyable and readable from DOS. It
carries the **whole Control Panel**, not just the driver list.

**Nothing in it is positional.** It was a fixed 32-byte struct, which is the
cheapest thing that works and the wrong thing to keep: adding a setting is
fine, but *removing* one leaves a hole every later version must go on
reserving, and any rearrangement silently reinterprets an old file's bytes as
the wrong settings. Every value now travels with a key that says what it is.

```
+0    'O','8','8','C','F','G',0,0     signature
+8    dw  container generation (3)     the shape BELOW, not the settings
+10.. records, then a key of 0:
        dw  key      two ASCII characters
        db  ver      what this key MEANS
        db  len      bytes of data
        db  data[len]
```

Six keys today, 43 bytes: `DW` driver-wanted bitmap, `SR` sound route, `CH`
clock 12/24, `CS` clock seconds, `SM` scheduler mode, `BB` back buffer. They
are ASCII so a hex dump of the file reads as the list of settings it is.

Four rules follow, and they are the point of the format:

1. **A key this kernel does not know is skipped** by its own `len`, and is
   **not carried over** — the writer emits the table and nothing else, so a
   setting written by a newer kernel survives an older one reading it exactly
   until that older one saves. That is intended: preserving bytes whose
   meaning cannot be checked is worse than losing them, because the next
   kernel to read them cannot tell a stale record from a current one.
2. **A key whose meaning changed carries a new `ver`**, so the old record does
   not match and the **default stands**. This is the case a positional struct
   cannot express at all — same name, different encoding, no way to tell them
   apart. *To change what a key means, bump its `ver`.*
3. **A different container generation means all defaults.** Generation 2 was
   the positional struct, and a struct read as records is nonsense.
4. **Anything absent falls back to the default**, because `drv_cfg_load` packs
   the *live* state into the struct before deserializing over it — and at boot
   the live state is exactly the defaults. There is no second copy of them to
   keep in step.

Every step of the walk is bounded by the byte count the read returned: a
record header that would run past the end, or a `len` that would, ends it. A
torn or corrupt file therefore costs the settings it did not carry and nothing
else.

A missing or malformed file means **the defaults**, never an error — which is
what makes a freshly built image boot with sound enabled and a disk with a
foreign `SYSTEM.CFG` boot at all.

The settings write is a **separate outcome from the load**, and the Control
Panel reports them separately: a load that succeeds and a save that cannot
reach the disk leaves the driver running and says so in the caption. Pretending
one implies the other would be the lie that matters here — the user would
believe a setting had been kept.

### 51.7 A driver's worker task

`OSAPI_TASK_SPAWN` is not available to a driver, and the reason is the thing
that makes that call safe: `inst_pkg_spawn`'s fence is a chain of five tests
all keyed on an **instance record** (§20.6), and a driver has none — no
window, no record, no `I_SPTR` to be identical to. `OSAPI_DRV_TASK`
(slot **0x0248**) is its own slot with its own fence of the same shape: the
caller's segment must be the segment of the driver whose services are
published (`ES == [drv_fseg]`), which is an identity test rather than an
approximation.

```
AX = a near entry in the driver's own segment -> spawn; DH reaches the new
     task's DH. out CF=0 and AL = the slot, CF=1 refused.
AX = 0  -> "this IS the worker, and it is exiting". NEVER RETURNS.
```

**DH, not a variable.** `task_spawn`'s argument word puts DL in `T_INST`
(0xFF here — a driver has no instance, so its worker bills to System) and
hands the whole of DX to the task. DH is how a driver gives a worker
something *per-task*, such as a stream generation: a close and a re-open
between the spawn and the worker's first instruction would leave both
workers reading a shared variable's second value.

**Two rules, and the second is not optional:**

1. A driver cannot spawn from **attach** — `drv_publish` arms the fence only
   after attach returns. Spawn from a service call, which is where a stream
   opens anyway.
2. **`DRVV_DETACH` must not return until the worker is gone.** `drv_unload`
   frees the driver's image the moment detach returns, and a worker still on
   the run queue leaves the scheduler holding a stack frame whose CS points
   at memory the next heap claim takes. `sbl_detach` is the reference: it
   stops the stream, then spins on its own liveness byte with
   `OSAPI_TASK_YIELD` until the worker clears it.

**A driver may claim heap.** `mem_own`'s identity test accepts a driver's
image (`MEM_K_DRV`) as well as a package's region, so `OSAPI_MEM_CLAIM`
works from a driver — it has to, because the SDK tells drivers to put bulk
buffers there rather than in their image. Those claims are owned by the
driver's *segment*, and `drv_release` sweeps them with `mem_free_owner`
**before** freeing the image, because the image's segment is the owner word
and freeing it first would leave a claim nothing could ever name again.

### 51.6 Author rules

1. **Attach all-or-nothing, detach cannot fail.** Restated because it is the
   whole contract.
2. **`DSV_TICK` runs inside IRQ0 at IF=0.** Keep it short, and touch no port
   that needs a long counted delay.
3. **Stage what you are handed, and read it through ES.** A package's
   pointer is in the package's segment and you run in yours; the kernel
   stages the one case that exists (a patch-load's 11 bytes) into its own
   buffer and hands you ES:SI. **Do not point DS at it** — your own tables
   are in DS, and taking DS to the caller takes them with it. `opl_patch`
   reads the patch with an `es lodsb` and its register bases with a plain
   `[opl_opregs+bx]` for exactly this reason; the version that repointed DS
   still made a noise, which is how a bug like that survives a listening
   test.
4. **Bulk memory is a claim, not bss.** Take it at attach, free it at detach.
   Your **image segment** is your owner word, exactly as a package's region is
   its own (§50.3), so `OSAPI_MEM_CLAIM` needs no driver variant. A buffer a
   **bus master** will address does: ask `OSAPI_MEM_CLAIM_DMA` for it, with
   `CX` = the KB of it the chip actually sees. Do not claim, check the
   address, and claim again — that holds every block that failed while it
   looks for one that does not.
5. **You may own a task.** `task_spawn` takes a segment, so a driver's refill
   loop is an ordinary background task — but it must be gone before detach
   returns.
