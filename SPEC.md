# os8088 1.0 — GUI specification

This document is the **binding contract** for all kernel modules. Every symbol
name, register contract, constant, and data layout here is pinned. Implement
exactly what is written; put questions in your report, not in the code.

## 0. Goal

A Macintosh System 1-style graphical OS for an 8086/8088 XT-class machine with
an ISA VGA card. 640x480, 16 colors (VGA mode 12h). Pre-emptive multitasking
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
2. **Near model.** CS = DS = `KERNEL_SEG` (0x1000) for all kernel code and
   tasks; **SS = `LOW_SEG`** (0x0800), because every task stack lives outside
   the kernel segment (§2.1). ES is scratch — any routine may change and use
   ES freely, but must restore it before returning unless documented
   otherwise. Calls between modules in `.text` are **near** calls; modules in
   `.fartext` are reached only through the shims of §33.

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
   `.lowbss` (scratch in `LOW_SEG`, §2.1) and `.fartext` (far code, §33).
   All four are declared once, with their attributes, at the top of
   `kernel.asm`; modules switch with a bare `section <name>`.
5. **Label hygiene.** One flat namespace. Prefix every module-internal label
   with the module's prefix (`vga_`, `font_`, `mou_`, `cur_`, `sch_`, `evq_`,
   `wm_`, `menu_`, `ui_`, `app_`, `inst_`, `dsk_`/`disk_`, `ld_`/`loader_`,
   `fm_`/`files_`, `ico_`/`icon_`, `desk_`, `dock_`, `tm_`, `cp_`, `snd_`,
   `opl_`, `sbl_`) or use NASM local labels (`.foo`).
6. Public drawing routines may assume the caller holds the **gfx lock**
   (§7) and that the cursor is hidden. They must not take the lock
   themselves.
7. Every gfx routine clips to the screen (0..639, 0..479) and leaves the VGA
   Graphics Controller in the default state on return: Set/Reset enable = 0,
   Bit Mask = FF, Function = replace, Data Rotate = 0, Map Mask = 0Fh,
   Read Map = 0, write mode 0 / read mode 0. When double buffering is
   active (§32) the drawing routines render into the back buffer and touch
   no VGA register at all; only `gfx_flush` (Sequencer Map Mask) and the
   cursor path program the hardware, and both restore the same defaults.

## 2. Memory map

| linear        | segment | contents                                          |
|---------------|---------|----------------------------------------------------|
| 0x00500       | —       | free; boot stack grows down from 0x7C00 (dead after handoff) |
| 0x00600       | 0x0060  | `FAR_SEG` — far code (§33), copied here by `far_init` |
| 0x07C00       | 0000    | boot sector (dead once it jumps to the kernel)     |
| 0x08000       | 0x0800  | `LOW_SEG` — `.lowbss`: disk buffers, then task stacks (§2.1) |
| 0x0FFFE       | 0x0800  | `STK0_TOP` — task 0's stack top, grows down        |
| 0x10000       | 0x1000  | kernel: code, data, .bss                           |
| 0x1B000       | 0x1000  | `APP_LOAD_OFF` — package pool, 19.5KB: multi-instance, sector-granular first-fit (§20/§21) |
| 0x1FE00       | 0x1000  | unused: the pool's exclusive end would not fit a 16-bit immediate at 0x10000 |
| 0x20000       | 0x2000  | `SAVE_SEG` — save-under heap (menus), raw, via ES; **extent pinned to 0x20000..0x2FFFF** (§2.2) |
| 0x30000       | 0x3000  | `SND_SEG` — sound buffers (§34): SB DMA double buffer, record ring, sample staging pool; raw, via **ES only** (§2.2) |
| 0x40000       | 0x4000  | `BB_SEG` — double-buffer back buffer, 4 planes × 0x9600 bytes (§32); touched only while the Control Panel's Display page has it switched on, which needs conventional RAM ≥ `DB_MIN_KB` |
| 0xA0000       | 0xA000  | VGA planar framebuffer, 80 bytes/row               |

Kernel image + .bss must fit below offset **`APP_LOAD_OFF`** (0xB000);
`kernel.asm` ends with build-time assertions (§15.1). Loaded programs occupy
kernel-segment offsets 0xB000..0xFDFF so they share CS and DS with the
kernel: their paint/key/click procs are ordinary near pointers. Works in
256KB of RAM: the back buffer is the only thing above 0x40000, and a machine
that fails the §32 RAM probe never touches it — everything below 0x40000 is
identical in both modes.

### 2.1 Low memory

Linear 0x00600..0x0FFFF — ~62KB between the BIOS data area and the kernel
image — is free on every machine once the boot sector has handed off.
Nothing the kernel keeps there needs to be addressable through DS, which is
the whole point: it is 24KB the kernel's own 64KB window does not spend.

`LOW_SEG` (0x0800) holds the `.lowbss` section, addressed through **SS** (the
task stacks) or **ES** (the disk buffers), never DS:

- `sch_stacks` — 11 × 1,536 bytes, task slots 1..11 (§8).
- Task 0 runs on the same segment at `STK0_TOP`, growing down toward the top
  of `.lowbss`. All tasks share one SS, so a switch is still an SP swap and
  SS is not part of the saved frame.
- `disk_dir`, `disk_icons`, `dsk_secbuf` — written by int 13h through ES:BX,
  read only through `dsk_get_dir` / `dsk_get_icon`, which stage one entry
  back into the kernel segment (§18).

`LOW_SEG:0x8000` **is** `KERNEL_SEG:0x0000`. Every `LOW_SEG` offset must stay
strictly below `LOW_LIMIT`, and the §15.1 assertions keep task 0 8KB of
clearance above `.lowbss`.

`FAR_SEG` (0x0060) holds the `.fartext` blob — see §33.

The full plan this came from, including the step still on the shelf, is
`docs/MEMORY-PLAN.md`.

### 2.2 SND_SEG — the sound segment (§34)

Linear 0x30000..0x3FFFF is the last fully-free 64KB block on the 256KB
floor (RAM ends at 0x40000 there), and the sound layer claims all of it:
`SND_SEG` = 0x3000, reached through **ES only**, never DS, and wholly
inside 8237 DMA physical page 3, so every buffer in it is
64KB-page-crossing-safe by construction. Internal region map, pinned — a
64KB segment with one implicit owner is a bug factory:

```
0x0000..0x0FFF   SB DMA double buffer, 2 × 2KB   (kernel-owned, §34.5, Phase 4)
0x1000..0x2FFF   SB record ring, 8KB             (kernel-owned, §34.6, Phase 5)
0x3000..0xFFFF   staging pool, ~52KB             (granted to instances, §34.6)
```

Packages never hold an ES pointer into the segment — data crosses through
kernel-staged copies in both directions (§34.6, the `dsk_get_dir` idiom of
§18). The Task Manager's RAM figure carries the segment the way it carries
`KLOWFAR_KB` (§15.1/§28) once the layer claims it (Phase 2).

**In the same breath, `SAVE_SEG`'s extent is pinned to 0x20000..0x2FFFF**:
the save-under heap may never grow past 0x30000, and `docs/MEMORY-PLAN.md`
Step D (packages into their own segments) must carve its per-package
segments from that same block on the floor machine — the bound that keeps
"SND_SEG is free" true forever. On bigger machines Step D may range above
0x40000/`BB_SEG` instead.

## 3. Global constants (defined once in kernel.asm, used everywhere)

```nasm
KERNEL_SEG   equ 0x1000
SAVE_SEG     equ 0x2000
VGA_SEG      equ 0xA000
; low memory (§2.1)
FAR_SEG      equ 0x0060      ; linear 0x00600 - far code (.fartext, §33)
LOW_SEG      equ 0x0800      ; linear 0x08000 - .lowbss: stacks + disk buffers
LOW_LIMIT    equ 0x8000      ; LOW_SEG:LOW_LIMIT IS KERNEL_SEG:0
STK0_TOP     equ 0x7FFE      ; task 0's stack top
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
APP_LOAD_OFF equ 0xB000      ; where packages load (kernel segment offset)
APP_MAX_SIZE equ 0x4E00      ; image + bss budget, 0xB000..0xFDFF
; double buffering (§32)
BB_SEG        equ 0x4000     ; back buffer base segment (plane 0)
BB_PLANE_PARA equ 0x960      ; paragraphs per plane (0x9600 bytes = 480 rows × 80)
DB_MIN_KB     equ 500        ; int 12h floor: double-buffer only at ≥ 500KB
; sound (§34, from Phase 1)
SND_SEG       equ 0x3000     ; sound buffers: linear 0x30000-0x3FFFF, ES only (§2.2)
```

## 4. Module files and ownership

| file                | owns                                                    |
|---------------------|---------------------------------------------------------|
| `kernel/kernel.asm` | entry, constants, init order, includes, .bss layout, **os8088 API jump table at 0x0010** (§20.3) + osapi helper routines, **boot splash entry at 0x0008** (§15) |
| `kernel/splash.inc` | boot-time loading screen (§15): mode 12h welcome dialog, pixel progress bar, spinning vector "8088"; far-ticked by the boot sector per sector read; self-contained, no .bss |
| `kernel/vga12.inc`  | mode set, planar primitives, save/restore, gfx lock     |
| `kernel/vgabb.inc`  | double buffering (§32): RAM probe, back buffer, software planar primitives, dirty rect, `gfx_flush` |
| `kernel/font.inc`   | 8x8 font (copied from VGA BIOS ROM at init), text draw  |
| `kernel/mouse.inc`  | COM1 UART, IRQ4 ISR, packet decode, cursor (save-under) |
| `kernel/sched.inc`  | PIT hook, context switch, task table, spawn/yield/sleep |
| `kernel/events.inc` | 8-byte event records, system event ring queue           |
| `kernel/clock.inc`  | system clock (§37): hardware RTC probe/read/write (int 1Ah), the wall-clock date + time advanced from `[ticks]`, field editing and formatting — prefix `clk_` |
| `kernel/wm.inc`     | window records, z-order, frames, hit test, paint-all, `wm_owner` side table |
| `kernel/instance.inc` | instance table: records, kind descriptors, launch/close lifecycle (§29) |
| `kernel/menu.inc`   | menu bar, pull-down tracking, command return            |
| `kernel/ui.inc`     | UI task: event pump, keyboard poll, drag, dispatch      |
| `kernel/apps.inc`   | built-in app kinds: About, Clock, Bounce — state pools, kinit procs, per-instance tasks |
| `kernel/disk.inc`   | BIOS int 13h floppy reads, os88fs mount + directory (§18–19) |
| `kernel/loader.inc` | package validation, pool allocation, per-instance load + relocate, launch (§21) |
| `kernel/files.inc`  | Disk window: file list UI, selection, open, refresh (§22) |
| `kernel/icons.inc`  | 1-bit icon format, draw routine, built-in library (§25) |
| `kernel/desk.inc`   | desktop drive icons: detect, paint, click/open (§26)    |
| `kernel/dock.inc`   | bottom dock strip: one tile per running instance, minimize/restore/activate (§30) |
| `kernel/taskmgr.inc`| Task Manager window: CPU load gauge + history graph, RAM readout, per-instance process list with CPU + memory (§28) |
| `kernel/ctrl.inc`   | Control Panel window: two-pane item list + settings pages (§31), prefix `cp_` |
| `kernel/snd.inc`    | sound core (§34): driver table + router, tone tier, speaker driver (tone + PWM clips), `snd_tick`, the five API slot targets, `snd_release_inst`/`snd_unhook` — prefix `snd_`, lands Phases 1–2 |
| `kernel/sndfm.inc`  | AdLib/OPL2 driver (§34): probe, init + patch loader, `opl_wr`, FM op — prefix `opl_`, lands Phase 3 |
| `kernel/sndsb.inc`  | Sound Blaster driver (§34): detect, `sbl_isr`, DMA ch1, streams, recording — prefix `sbl_`, lands Phases 4–5 |
| `kernel/farcall.inc`| Far-code mechanism (§33): the `FARK`/`KCALL`/`FARSHIM` macros, `far_init`, and the list of kernel routines far code may call |

`kernel/video.inc`, `keyboard.inc`, `string.inc`, `gfx.inc` remain in the
tree but are **no longer included**; the GUI replaces the text shell.

## 5. vga12.inc

Mode 12h planar programming: GC index port 0x3CE / data 0x3CF, Sequencer
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
still behave sanely — draw nothing — if the clipped rect is empty).
**Color for every drawing op comes from the byte variable `[gfx_color]`.**

| symbol          | in                                   | effect                                |
|-----------------|--------------------------------------|---------------------------------------|
| `vga_mode12`    | —                                    | BIOS int 10h AX=0012h                 |
| `vga_text`      | —                                    | BIOS int 10h AX=0003h                 |
| `gfx_color`     | byte variable                        | current drawing color                 |
| `gfx_pixel`     | CX=x, DX=y                           | plot pixel                            |
| `gfx_hline`     | AX=x1, BX=x2, DX=y                   | horizontal line, inclusive            |
| `gfx_vline`     | AX=x, BX=y1, DX=y2                   | vertical line, inclusive              |
| `gfx_fill`      | AX=x1, BX=y1, CX=x2, DX=y2           | solid rect, inclusive corners         |
| `gfx_frame`     | AX=x1, BX=y1, CX=x2, DX=y2           | 1px outline rect                      |
| `gfx_fill_gray` | AX=x1, BX=y1, CX=x2, DX=y2           | 50% dither: black/white checkerboard (pixel parity (x+y)&1: even=white, odd=black) — ignores gfx_color |
| `gfx_fill_pat`  | AX=x1, BX=y1, CX=x2, DX=y2, `[gfx_pat]` = near ptr to 8 pattern bytes | 8×8 dither fill, screen-aligned: row y uses byte `pat[y&7]`, bit 7 = leftmost pixel of each screen byte, bit set = white (15), clear = black (0) — ignores gfx_color. Writes only colors 0/15, so like `gfx_fill_gray` it never retires `[bb_mono]` (§32) |
| `gfx_xor_rect`  | AX=x1, BX=y1, CX=x2, DX=y2           | 1px outline, XOR 0Fh (drag outline)   |
| `gfx_xor_fill`  | AX=x1, BX=y1, CX=x2, DX=y2           | filled rect, XOR 0Fh (menu highlight) |
| `gfx_save`      | AX=x1, BX=y1, CX=x2, DX=y2, ES:DI=buf| copy region to buffer; x1 is rounded **down** to a byte boundary and x2 **up** internally. Buffer layout: plane 0 rows, plane 1 rows, plane 2, plane 3. Returns DI advanced past data. |
| `gfx_restore`   | AX=x1, BX=y1, CX=x2, DX=y2, ES:SI=buf| write region back (same rounding/layout). Returns SI advanced. |
| `gfx_lock`      | —                                    | acquire drawing mutex + hide cursor (§7) |
| `gfx_unlock`    | —                                    | flush the back buffer (§32), show cursor, release mutex |
| `gfx_flush`     | —                                    | copy the dirty back-buffer rect to VRAM; no-op when double buffering is off or nothing is dirty (§32) |

Save/restore for a W-px-wide, H-px-tall rect uses
`bytes = ((x2/8) - (x1/8) + 1) * H * 4`; provide `gfx_save_size`
(same rect regs in, AX = byte count out) so callers can budget buffers.

**Double-buffer dispatch (§32).** Every public drawing entry above
(`gfx_pixel` … `gfx_restore`) starts with a `[bb_on]` test and branches to
its `bb_*` twin in vgabb.inc when double buffering is active; contracts,
clipping and buffer layouts are identical in both paths. Four VRAM bodies
remain reachable under their own names, for callers that must not touch the
back buffer: `vga_save_vram`/`vga_restore_vram` (the mouse cursor's
save-under, §9) and `vga_xor_rect_vram`/`vga_xor_fill_vram` (the drag
outline and the menu highlights, §12/§13). All four are transient overlays —
drawn and erased within one held lock — so they live in VRAM only, never in
the back buffer (§32).

## 6. font.inc

`font_init` runs **after** `vga_mode12`: int 10h AX=1130h BH=03h returns
ES:BP → the ROM 8x8 font; copy glyphs 32..126 (95 glyphs × 8 bytes) into a
kernel buffer. No font bytes are hard-coded.

| symbol       | in                       | effect                              |
|--------------|--------------------------|--------------------------------------|
| `font_init`  | —                        | copy ROM font to RAM                 |
| `font_char`  | CX=x, DX=y, AL=char      | draw 8x8 glyph, color `[gfx_color]`, transparent background |
| `font_str`   | CX=x, DX=y, SI=NUL str   | draw string left→right               |
| `font_width` | SI=NUL str               | out AX = pixel width (8 × length)    |

Characters outside 32..126 draw as space. Glyph rows may straddle two VRAM
bytes; use Set/Reset + Bit Mask with the glyph row shifted across a 16-bit
window. Under double buffering (§32) `font_char` branches after clipping to
a software renderer that applies the same shifted row masks to all four
back-buffer planes (or/and-not per the `[gfx_color]` plane bit).

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
- **BIOS calls**: only the UI task calls int 10h/16h after boot. Background
  tasks (clock, bounce) use only os8088 primitives and `ticks`. The syscall gate
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
  Slots 1..11 are dynamic (spawned by `app_launch`/§29, freed by
  `task_exit`); each has a 1536-byte stack:
  `sch_stacks resb (MAX_TASKS-1) * SCH_STACK` **in `.lowbss`** (§2.1),
  slot n's stack top at
  `sch_stacks + n*SCH_STACK` (slot 1 owns bytes 0..1535).
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
- `task_spawn` — in: AX = entry point (near), DX = argument word — delivered
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
  on the 8086). A task's entry routine must never `ret` — it terminates
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
  loaded package, which cannot spawn at all, §20.2) at 0% forever. Two
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
                          ; only by a debugger (QMP `xp` on segment 0x1000)
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
once live is ~30 cycles; the pinned worst case is one sanctioned OPL2
key-off (a single register write via `opl_wr`, ~90–280 µs, §34.1) when a
timed tone routed to OPL2 expires — bounded, rare (at most once per tone
end at 18.2 Hz) and honest: the alternative, a deferred pend flag, has
unbounded latency and lets a routed tone drone. Phase 4 adds the SB
stream watchdog (§34.5), the same bound class.

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

**Liveness (why cooperative mode is safe for the built-ins).** Every wait
loop in the tree already yields: `ui_task`'s idle pass (§13 step 4),
`ui_drag`'s track loop and its linger loop (§13), `menu_track`'s poll loop
(§12), the `gfx_lock` spin (§7), `tm_task`'s measurement spin (§28), and
Clock/Bounce through `task_sleep` (§14). No built-in path depends on being
pre-empted. The watchdog exists for **loaded packages** (§20–21): they are
task-less, run inside the UI task's `W_PAINT`/`W_ONKEY`/`W_ONCLICK`
dispatch (§11), are under no obligation to yield, and cannot be escaped
from by keyboard — keys are polled with int 16h from the UI task (§13 step
1) and there is no keyboard ISR. A runaway package would hard-hang the
machine in a switch-free cooperative mode; with the watchdog it merely
makes the machine slow, and the Task Manager keeps updating.

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
  packet), update `mouse_x` (clamp 0..639), `mouse_y` (0..479), `mouse_btn`
  (bit 0 = left). On button *change*, push an event (§10): EVT_MDOWN /
  EVT_MUP with a=x, b=y, c=[ticks] — the click's birth time; double-click
  detection compares birth ticks, never processing time, so clicks queued
  behind a slow disk mount cannot collapse into a double-click. Move the cursor per §7 (draw only when
  `gfx_lock_flag` is clear AND `cur_level` >= 0; otherwise just update
  position and set `cur_dirty`). Send EOI (AL=0x20 → port 0x20) — the BIOS
  does not handle IRQ4 for us. `cld` before any string op; never `sti`.
- `mouse_unhook` — restore int 0x0C vector, mask IRQ4 again, IER=0.
- Cursor: classic Mac arrow, 11 px tall, hot spot (0,0) — black body,
  1px white outline. Two 16-row × 16-bit tables: `cur_and` (white outline
  mask) and `cur_data` (black body). Draw: white pass = Set/Reset white,
  Bit Mask = mask row bits; black pass likewise. Save-under buffer in .bss:
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
```

Single system queue, 16 records, ring buffer in .bss. Producers may be ISRs:
`evq_push` (SI → record; copies 8 bytes; guards the copy + index update with
`pushf`/`cli` … `popf` so the caller's IF is preserved — it is called from
the mouse ISR, which must keep IF=0 throughout (§7); never a bare `sti`;
drops silently when full) and `evq_pop` (DI → destination; CF=1 if empty;
same `pushf`/`cli` … `popf` guard).
Keyboard events are *not* queued — the UI task polls BIOS int 16h directly.

## 11. wm.inc — windows

Window record, 18 bytes, fixed offsets:

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
                 ; Same rules as W_PAINT: must not lock, block or spawn.
WIN_SIZE equ 18
MAX_WIN  equ 12

WF_SIZABLE equ 4  ; W_FLAGS bit2: the window can be resized (§11.1)
WF_FULL    equ 8  ; W_FLAGS bit3: the window is fullscreen (§11.2)
WMIN_W     equ 96 ; smallest frame a resize can leave, outer px (§11.1)
WMIN_H     equ 64
```

(WIN_SIZE grew 16 → 18: any ×16 shift idioms in wm.inc must become a true
×18 multiply. The wm_create template stays **16 bytes**:
{x,y,w,h,title,paint,onkey,onclick} words — feature bits like WF_SIZABLE
are **not** template words; they are OR-ed in after wm_create (KD_WFLAG for
built-ins, §29.3; `wm_sizable` for packages, §20.3), so every shipped .o88's
16-byte template stays valid. MAX_WIN grew 8 → 12 for instancing (§29);
`apps/os88api.inc` mirrors it. **W_W/W_H are no longer set-once**: `ui_grow`
(§13) and `wm_fullscreen` (§11.2) rewrite them at runtime, so a resizable or
fullscreen window's W_PAINT/W_ONCLICK must derive their layout from the live
record every call — never from constants that bake in the template size.)

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
| `wm_create`    | in SI → 16-byte template {x,y,w,h,title,paint,onkey,onclick} words; out BX = window ptr, CF on table full. Created **hidden**; appends the window's index to `wm_zord` (frontmost) and increments `wm_zn`. Does not repaint — callable without the gfx lock. |
| `wm_destroy`   | in BX = win ptr: clear W_FLAGS (used+visible), reset the slot's `wm_owner` entry to 0xFF, remove its index from `wm_zord` (compact the array, decrement `wm_zn`), repaint all. Caller holds the gfx lock. The record slot becomes reusable by wm_create. |
| `wm_show`      | in BX = win ptr: set visible, bring to front, repaint all    |
| `wm_hide`      | in BX = win ptr: clear visible, repaint all                  |
| `wm_front`     | in BX = win ptr: raise to front of z-order, repaint all      |
| `wm_top`       | out BX = frontmost visible window ptr, 0 if none             |
| `wm_hit`       | in CX=x, DX=y; out BX = topmost visible window ptr containing the point (0 if none), AL = 0 content, 1 title bar, 2 close box, 3 minimize box, 4 grow box. AL=2/AL=3 only when BX is the frontmost visible window (the only one with the boxes drawn); on any other window those regions report AL=1. AL=4 only when BX is the frontmost visible window **and** has WF_SIZABLE (and not WF_FULL): the 13×13 grow-box rect of the frame drawing above; anywhere else that region is plain content (AL=0). A WF_FULL window reports AL=0 for every point — it has no chrome. |
| `wm_paint_all` | full repaint: desktop gray (below menu bar), then `desk_paint` (§26 — desktop icons sit on the desktop, under every window), then `dock_paint` (§30 — the dock strip sits on the desktop under every window, like the icons), menu bar, every visible window back→front (frame + white content + W_PAINT). Caller holds gfx lock. |
| `wm_content`   | in BX = win ptr; out AX = content left, DX = content top. WF_FULL set → AX = W_X, DX = W_Y (no border, no title bar — §11.2). |
| `wm_sizable`   | in BX = win ptr, AL = 0 clear / non-zero set WF_SIZABLE. No repaint (the grow box appears at the next paint). UI-task context only (entry procs and window callbacks qualify); safe with or without the gfx lock there — every W_FLAGS writer runs on the UI task or under the lock. API slot 0x008C (§20.3). |
| `wm_grow_paint`| in BX = win ptr (caller holds the gfx lock): draw the grow box **iff** BX is the frontmost visible window with WF_SIZABLE set and WF_FULL clear; a no-op otherwise, so it is always safe to call. wm_draw_win uses it after W_PAINT, and a resizable window's **self-initiated content repaint must end with it** — the white-fill idiom (§22) erases the corner, and without the call the box vanishes until the next full repaint while wm_hit still reports AL=4 there. Packages reach it through API slot 0x0094 (§20.3). |
| `wm_fullscreen`| in AL = 1 enter (BX = win ptr) / AL = 0 exit; **caller holds the gfx lock** (the intended callers are W_ONKEY/W_ONCLICK handlers, which already do). See §11.2. Out CF=1 refused (enter while another window owns the screen), CF=0 done. API slot 0x0090 (§20.3). |
| `wm_ptr2idx`   | in BX = win ptr (record-aligned); out AL = window index, AH = 0. Clobbers nothing else. The one public home of the `(ptr − wm_wins) / WIN_SIZE` idiom. |
| `wm_obscured`  | in BX = win ptr; out CF=1 if any visible window above BX in z-order overlaps its frame rect (background tasks use this to skip live updates when covered). Result is only trustworthy while the caller holds the gfx lock — the UI task mutates `wm_zord`/window rects under it. |

Paint procs and key handlers run on the **UI task** (via wm_paint_all /
dispatch) or on the window's own background task — in all cases the caller
of W_PAINT already holds the gfx lock. W_PAINT must not lock, block or spawn.

### 11.1 Resizing

A window is resizable iff W_FLAGS bit2 (`WF_SIZABLE`) is set. The bit is
**not** part of the 16-byte template: built-ins get it from their kind row's
`KD_WFLAG` byte (§29.3, OR-ed into W_FLAGS by app_launch right after
wm_create), packages call `wm_sizable` (API slot 0x008C) from their entry
proc after OSAPI_WM_CREATE. Fixed-layout windows — dialogs, the Control
Panel, Minesweeper — simply never set it and nothing about them changes.

What the bit buys: the frontmost window draws the grow box (frame drawing
above), `wm_hit` reports AL=4 inside it, and ui.inc answers with the
**resize loop** `ui_grow` (§13) — a sibling of the title-bar drag loop with
the identical binding lock/XOR ordering, tracking an outline anchored at
(W_X, W_Y) whose size follows the mouse: cur = orig + (mouse − start),
clamped to at least WMIN_W×WMIN_H while tracking (the XOR rect must stay
well-formed). On release it clamps again — WMIN_W ≤ w ≤ SCREEN_W − W_X,
WMIN_H ≤ h ≤ SCREEN_H − W_Y (the frame stays on screen; position never
changes) — writes W_W/W_H, and calls wm_paint_all under the still-held
lock. There is no resize callback: the full repaint re-enters W_PAINT,
and a resizable window's procs are required to lay out from the live
record (record note above). Self-initiated repaints (the fm_repaint idiom,
§22) must white-fill using the live W_W/W_H for the same reason.

`ui_drag`'s release clamp is unchanged (x + w ≤ SCREEN_W with the live
width), so a grown window still cannot be dragged off screen.

### 11.2 Fullscreen

The SDK/kernel foundation for apps that want the whole 640×480: a
fullscreen surface **is a real window** — that one decision buys almost
everything, because wm_obscured (which gates every unbidden background
drawer: Clock, Bounce, the Task Manager sampler) sees a frame covering the
entire screen and reports "covered" to everyone beneath it.

`wm_fullscreen` (API slot 0x0090; caller holds the gfx lock):

- **Enter** (AL=1, BX = win ptr): another window already owns the screen
  (`[wm_fs]` non-zero and ≠ BX) → CF=1, nothing changes. Else save
  W_X/W_Y/W_W/W_H into `wm_fs_save` (4 words, .bss), store BX in `wm_fs`
  (word, .bss, 0 = none — **the** fullscreen latch), set the frame to
  (0, 0, SCREEN_W, SCREEN_H), set WF_FULL, `wm_front` (raises + repaints
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

## 12. menu.inc

Menu bar: rows 0..MBAR_H-1, white, 1px black line at row MBAR_H-1. First
menu is the System menu, titled by the os8088 logo: an 11×11 one-color
DIP-chip silhouette bitmap (7px-wide body with a top notch and four pins
per side; hand-authored `dw` rows are fine — this is the one place bitmap
data is hand-made). Others are text titles.

```nasm
; menu table (menu.inc data):
; per menu: { titleptr (0 = logo glyph), itemsptr, item count, cmd base }
; items: array of near ptrs to NUL strings
CMD_ABOUT  equ 1   ; --- System ---
CMD_CTRL   equ 2
CMD_TASKS  equ 3
CMD_NOTE   equ 4   ; --- File ---
CMD_CLOCK  equ 5
CMD_BOUNCE equ 6
CMD_FILES  equ 7
CMD_CLOSE  equ 8
CMD_REBOOT equ 9   ; --- Special ---
```

Menus: **System** (logo): "About os8088..." (CMD_ABOUT), "Control Panel"
(CMD_CTRL, string `menu_s_ctrl`, §31), "Task Manager" (CMD_TASKS,
string `menu_s_tasks`, §28) — System's item count in `menu_table` is 3.
**File**: "Clock" (CMD_CLOCK), "Bounce"
(CMD_BOUNCE), "Disk" (CMD_FILES), "Close Window" (CMD_CLOSE).
**Special**: "Restart" (CMD_REBOOT) — one item. Menu bases:
System = CMD_ABOUT, File = CMD_NOTE, Special = CMD_REBOOT.

**`cmd = menu base + item index`, so each menu's commands must stay
consecutive from its base** — that arithmetic is how `menu_track` turns a
highlighted item index into a CMD_*. Moving "Task Manager" out of Special
and onto the end of the System menu therefore renumbered the File menu
(3 → 4, … 7 → 8) and left CMD_REBOOT alone as Special's only item and its
base, exactly as inserting Disk, Task Manager and Control Panel renumbered
their successors before. Bar hit ranges in `menu_table` are unchanged (the
titles did not move); the pull-down's width is computed from the widest
item by `menu_widest`, so the System menu is as wide as "Control Panel" and
Special shrinks to "Restart". A one-item menu needs no special case — the
count only feeds the rect height, the item loop and `menu_hover`'s bound.

| symbol          | contract                                                   |
|-----------------|-------------------------------------------------------------|
| `menu_draw_bar` | draw the bar + titles (gfx lock held by caller)             |
| `menu_track`    | in: CX = mousedown x. Runs the whole interaction while the button is held (caller holds gfx lock): highlight title (xor), drop the menu (gfx_save under it to SAVE_SEG:0), track item highlight following `mouse_y`, on release restore save-under + unhighlight; out AX = CMD_* or 0. Item cells are 16px tall, menu width = widest item + 16px padding. |

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

| symbol            | contract                                                  |
|-------------------|------------------------------------------------------------|
| `menu_draw_clock` | in: nothing (gfx lock held by the caller). Formats the live clock with `clk_fmt` (§37), white-fills the whole cell — x `MENU_CLK_HX`..`SCREEN_W-1`, rows 0..`MBAR_H-2`, the black rule excluded — and draws the string **right-aligned**: `font_width` gives its pixel width and the pen goes at `SCREEN_W - 8 - width`, `MENU_TEXT_Y`. Preserves all registers. |

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
ui_task hit-tests `x >= MENU_CLK_HX` *before* `menu_track`, so the cell is
not a menu title and never drops a pull-down; the panel window appearing
(or coming forward) is the click's feedback.

## 13. ui.inc — the UI task (task 0)

Loop forever:
1. Poll keyboard: int 16h AH=01; if a key, fetch (AH=00) and near-call the
   front window's W_ONKEY (if any) under gfx_lock, billed to the window's
   instance (§11 "callback billing").
2. `evq_pop`; on EVT_MDOWN at (x,y) — first store the event's EV_C into
   the public word `ui_click_t` (the click's birth tick; §22/§26 read it
   during dispatch):
   - `[wm_fs]` non-zero (§11.2) → skip both menu-bar branches below and go
     straight to `wm_hit`: the bar is under the fullscreen surface, and
     the fullscreen window claims every point as content.
   - y < MBAR_H and x >= `MENU_CLK_HX` → the menu-bar clock (§12.1): store
     `CP_ITIME` into `[cp_sel]` and `app_launch` KIND_CTRL (§31.5), no lock
     held, beeping on refusal like any other launch. Tested **before**
     `menu_track`, so the cell never drops a pull-down.
   - y < MBAR_H → gfx_lock, `menu_track`, gfx_unlock, then dispatch CMD_*
     (below).
   - else `wm_hit`: close box → **quit**: gfx_lock, `app_close_win` BX
     (§29 — looks up the owning instance via `wm_ptr2idx` + `wm_owner`
     and runs the close protocol: synchronous teardown for task-less
     instances, die-flag + hide for task-owned ones; an ownerless window
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
     y >= MBAR_H), call `wm_paint_all`, then gfx_unlock. Do **not** call
     gfx_lock again in the release step — the lock is non-reentrant (§7)
     and task 0 already holds it; re-acquiring would deadlock the GUI.
   - grow box (AL=4, frontmost + WF_SIZABLE only, §11.1) → the **resize
     loop** `ui_grow`: identical structure, lock discipline and binding
     erase-before-unlock ordering as the drag loop, but the outline stays
     anchored at (W_X, W_Y) and its **size** follows the mouse:
     cur w/h = orig w/h + (mouse − start), clamped to ≥ WMIN_W/WMIN_H
     every pass. On release (outline drawn, lock held): xor-erase, clamp
     w to WMIN_W..SCREEN_W−W_X and h to WMIN_H..SCREEN_H−W_Y, write
     W_W/W_H, `wm_paint_all`, gfx_unlock — same no-relock rule.
   - content of non-front window → `wm_front`.
   - content of front window → if its `W_ONCLICK` is non-zero: gfx_lock,
     near-call it (CX=x, DX=y, SI=win ptr) billed to the window's instance
     (§11 "callback billing"), gfx_unlock; else ignore.
   - no window hit (wm_hit BX=0) → call `dock_click` (§30) with CX=x,
     DX=y, no lock held; CF=1 = the click was consumed (anywhere in the
     dock strip). Only if it declines (CF=0), call `desk_click` (§26) —
     dock and desktop icons are hit-tested only after every window has
     declined the click, which gives them correct z-order semantics for
     free.
3. If `[inst_launch]` is non-zero (§29): AX = [inst_launch] − 1, zero
   `[inst_launch]`, call `app_launch` with AL = kind. Then if
   `[ld_pending]` is non-zero (§21): AX = [ld_pending] − 1, zero
   `[ld_pending]`, call `loader_run`. Then if `[cp_dirty]` is non-zero
   (§31.2): zero it, gfx_lock, `wm_paint_all`, gfx_unlock — the scheduler
   mode was flipped from the Control Panel and every already-painted
   window that quotes it (the About box's third line, §14) must follow.
   All three run **outside** the gfx lock with the same
   consume-before-run rule — app_launch and loader_run manage their own
   locking, and the repaint takes the lock here rather than inside
   `cp_onclick`, which already holds it. Then, on the same deferred
   channel, if `[clk_dirty]` is non-zero (§37): zero it and call
   `clk_rtc_write` — the Control Panel changed the time and the hardware
   RTC is written **here**, outside the lock, because a page proc may not
   call BIOS (§31.1).
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

Command dispatch: CMD_ABOUT/CTRL/NOTE/CLOCK/BOUNCE/TASKS → `call
app_launch` with AL = the matching KIND_* (§29; CMD_CTRL → KIND_CTRL, the
Control Panel of §31). ui_dispatch runs on the UI task with
no lock held, so the call is direct — the deferred `inst_launch` channel
in step 3 exists for lock-held posters (e.g. W_ONCLICK handlers). Note
Pad/Clock/Bounce launch a **new instance** each time (up to their §29
caps); About/Control Panel/Task Manager are singletons — at cap,
app_launch fronts (and un-minimizes) the existing instance instead. CMD_FILES → call
`files_open` (§22 — mounts, then launches/fronts the Disk singleton via
app_launch; does its own locking). CMD_CLOSE → **quit** the frontmost:
gfx_lock, `wm_top`, and if BX ≠ 0 `app_close_win` under the same lock,
gfx_unlock. CMD_REBOOT → gfx_lock (never released), `vga_text`,
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

- **About** — 300×120 at (170,140), title "About os8088". Paint: centered
  lines "os8088 1.0", "a graphical OS for the 8086", and a third line whose
  scheduling word **tracks the live mode** (§8.2, read with
  `sched_mode_get`): "pre-emptive - 640x480 - 16 colors" or
  "cooperative - 640x480 - 16 colors". Either implementation is fine — two
  whole alternative strings picked by mode, or the mode word drawn ahead of
  a shared " - 640x480 - 16 colors" tail — because "pre-emptive" and
  "cooperative" are both exactly 11 characters, so the line's pixel width
  and its centered x are identical in both modes and the existing centering
  math is unchanged. No onkey. Singleton (cap 1).
- **Clock** — 130×60 at (350,60), title "Clock". Cap 10 (the template
  position keeps the whole +16·9 cascade on-screen and above the dock).
  Per-instance
  task (`app_clock_task`; entry receives DX = instance index, caches the
  record and state ptrs): loop { task_sleep 9; **if I_STATE = 2 →
  teardown via `inst_task_die`** (§29); AX = [ticks]; delta = AX −
  CLK_LAST (subtraction idiom, safe across wrap); CLK_LAST = AX; CLK_ACC
  += delta*10; while CLK_ACC >= 182: CLK_ACC −= 182 and advance seconds
  with carries (s 60→0/m+1, m 60→0/h+1, h 24→0). This time-keeping runs
  every iteration; only drawing is conditional: gfx_lock; **re-check
  under the lock** window (I_WIN) visible and not `wm_obscured` — if it
  fails, gfx_unlock and skip; else white-fill the string rect (font
  background is transparent, §6), set `[gfx_color]` = CBLACK, draw
  HH:MM:SS from the instance's CLK_H/M/S centered in content;
  gfx_unlock }. Paint proc renders the same string from the state block.
  The accumulator design is binding.
- **Bounce** — 150×130 at (300,150), title "Bounce". Cap 10 (the template
  position keeps the whole +16·9 cascade on-screen and above the dock).
  Per-instance task: loop { task_sleep 2; **if I_STATE = 2 → `inst_task_die`**;
  gfx_lock; **check under the lock** visible and not obscured — if the
  check fails, gfx_unlock and skip the frame without erasing or stepping;
  else erase 8×8 black square at old pos (white fill), step x/y by
  velocity, bounce off content edges, draw at new pos; gfx_unlock }.
  Paint proc: square at the instance's current pos.

The close protocol for these tasks is §29's: the UI task never destroys a
task-owned instance's window — it sets I_STATE = 2 and hides; the task
notices at its next wake (≤ 9 ticks for Clock, ≤ 2 for Bounce) and tears
itself down with `inst_task_die` → `task_exit`.

## 15. kernel.asm — boot sequence

Keep the 0x0000 cold entry. At 0x0010 the retired syscall gate is replaced
by the **os8088 API jump table** (§20.3) — a run of 4-byte `jmp near` slots at
pinned offsets. kernel.asm also owns the tiny osapi helper routines
(§20.4) and the `osapi_seed` word. `cpu 8086` + `bits 16` + `org 0`.

**Boot splash entry — 1000:0008.** A third fixed entry point sits between
the cold entry and the API table: a `jmp near spl_tick` at offset 0x0008,
**far-called by the boot sector after every sector it reads** once at least
`SPL_RESIDENT` (= 4) sectors are in memory. Contract: AX = sectors loaded
so far, DX = total sectors to load; `spl_tick` preserves every register and
segment (flags clobbered), runs on the boot stack with the boot sector's
segments, and returns with `retf`. The boot sector defines the same
`SPL_RESIDENT` constant; the two must agree with this section.

The splash module (`splash.inc`, prefix `spl_`) is **included first, before
every other module**, and ends with a build assertion that its last byte
lies below `SPL_RESIDENT * 512` — it must be fully resident before the
first tick can arrive. Because it runs mid-load it is **self-contained**:
it calls int 10h (mode set 12h, cursor set, teletype text — BIOS text is
legal here; the "only the UI task calls int 10h after boot" rule of §8
starts at kmain) and its own planar drawing primitives, never another
module's routines (they are not yet resident). Its state lives in
in-module data words, **never .bss**: .bss begins at the image's end, so
the final sector read lands on top of it, and nothing has cleared it yet.
The splash must never delay loading — no waits, no timing loops; it only
draws, once per completed sector, inside the disk's own rotational latency.
First tick: mode 12h + chrome (welcome dialog, bar trough, title). Every
tick: bar fill = AX×288/DX pixels, right-aligned percentage, and one
spin step of the vector "8088" (cosine-scaled about its vertical axis,
angle index = AX mod 16). kmain's own `vga_mode12` then wipes the splash.

kmain: set DS/ES = `KERNEL_SEG` and SS:SP = `LOW_SEG:STK0_TOP` (§2.1),
`sti`, `cld`, then: `far_init` (**first** — the `.fartext` blob is sitting
on top of `.bss` until it runs, §33) →
`sched_init` → `evq_init` → `clk_init` (§37 — the RTC probe, before the
mode set so a machine without one is dated from the fallback constants
from the first paint onward) → `vga_mode12` → `bb_init` (§32 — the RAM probe
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

### 15.1 Size guards

End of file (after all `%include` lines, with `section .text` in effect).
`kernel_text_end` **must** be the last thing in `.text`: it is at once the
image size, the base of `.bss` and the landing address of the `.fartext`
blob. Each section measures itself against its own `$$` — a label difference
across two sections is not a constant in `-f bin` and will not assemble.

```nasm
kernel_text_end:
KTEXT_SIZE equ kernel_text_end - $$

section .fartext
kernel_far_end:
KFAR_SIZE equ kernel_far_end - $$

section .lowbss
kernel_low_end:
KLOW_SIZE equ kernel_low_end - $$

section .bss
; (modules already declared their own .bss blocks inside their .inc files;
;  NASM accumulates them in declaration order — this block lands last)
kernel_bss_end:
KBSS_SIZE equ kernel_bss_end - $$

%if KTEXT_SIZE + KBSS_SIZE > APP_LOAD_OFF
%error "kernel too big: image + bss must stay below APP_LOAD_OFF"
%endif
%if KTEXT_SIZE + KFAR_SIZE > APP_LOAD_OFF
%error "kernel too big: image + fartext must stay below APP_LOAD_OFF"
%endif
%if KLOW_SIZE > STK0_TOP - 8192
%error "lowbss too big: task 0's stack needs 8KB of clearance below STK0_TOP"
%endif
%if STK0_TOP >= LOW_LIMIT
%error "STK0_TOP must stay below LOW_LIMIT (LOW_SEG:LOW_LIMIT is the kernel)"
%endif

KLOWFAR_KB equ (KLOW_SIZE + KFAR_SIZE + 1023) / 1024   ; §28 RAM figure
```

The second guard exists because the far blob lands *inside* the kernel
window and only leaves it when `far_init` runs. Keep this block last.

## 16. Build & test

- Makefile: boot-image recipes unchanged. `run` target keeps the serial
  mouse (`-chardev msmouse,id=m0 -serial chardev:m0`) and now also attaches
  the software floppy as drive B:
  `-drive file=build/apps.img,format=raw,if=floppy,index=1`.
  `test` target: same, plus `-display none -qmp unix:build/qmp.sock,server,nowait`.
- New tooling targets: see §24 (apps, packages, os88fs images).
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
    Disk and every loaded package show state `evt`, their own region size
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
    segment 0x1000).

## 18. disk.inc — floppy reads (BIOS int 13h)

Only the UI task touches the disk (extends §7's BIOS rule to int 13h).
During a read, task switching is paused: `inc byte [sch_lock]` before,
`dec` after (the timer ISR still chains the BIOS tick, which the floppy
motor logic needs). The gfx lock is NOT held across disk I/O.

Geometry lives in variables so both 1.44MB (18 spt) and 360KB (9 spt) data
disks work: `disk_spt` (word), `disk_heads` (word) — loaded from the os88fs
superblock at mount. LBA→CHS: cyl = LBA/(spt×heads); rem = LBA%(spt×heads);
head = rem/spt; sector = rem%spt + 1. Reads go **one sector per int 13h
call** (AH=02, AL=1) — no multi-sector calls, so track boundaries and DMA
alignment never matter. Each sector: up to 3 attempts, with AH=00 reset on
failure between attempts.

| symbol       | contract                                                      |
|--------------|----------------------------------------------------------------|
| `disk_read`  | in: AX=LBA, CX=sector count, ES:BX → dest (advances BX by 512 per sector; caller's ES:BX budget must cover count×512). Drive from `[disk_drive]`. Out: CF=1 on unrecoverable error. Preserves registers per §1. |
| `disk_mount` | in: DL=drive (0=A, 1=B). Sets `[disk_drive]`, reads LBA 0 with the *fallback* geometry spt=9/heads=2 (CHS 0/0/1 — identical under any real floppy geometry), validates the superblock (§19), loads `disk_spt`/`disk_heads`/`disk_nfiles`, then reads the 2 directory sectors (LBA 1–2) into `disk_dir` and the 4 icon-table sectors (LBA 3–6) into `disk_icons`. Out: CF=1 if the disk is unreadable or not os88fs (then `disk_nfiles`=0). |
| `disk_drive`  | byte variable, current drive (init 1 = B:)                   |
| `disk_nfiles` | word, valid after a successful mount (else 0)                |
| `disk_dir`    | 1024-byte **`.lowbss`** buffer (§2.1): the 32 directory entries. int 13h writes it through ES:BX; read it only via `dsk_get_dir` |
| `disk_icons`  | 2048-byte **`.lowbss`** buffer (§2.1): the 32 icon-table entries (§19); entry i belongs to directory entry i, 64 bytes each, all-zero = no icon. Read it only via `dsk_get_icon` |
| `dsk_get_dir` | in: AX = entry index. Stages that entry's 32 bytes from `LOW_SEG` into the kernel-segment buffer `dsk_ent`; out: SI = `dsk_ent`. Consumers keep an ordinary DS:SI pointer and never see a segment |
| `dsk_get_icon`| in: AX = entry index. Same, 64 bytes into `dsk_ico`; out: SI = `dsk_ico` |
| `dsk_copy_in` | in: SI = `LOW_SEG` offset, DI = kernel offset, CX = even byte count. The one routine that reaches into low memory for data |

## 19. os88fs — on-disk format (data floppies)

An os88fs floppy is not bootable and holds only packages. All words
little-endian. Sector size 512.

**LBA 0 — superblock:**

| off | size | contents                                  |
|-----|------|-------------------------------------------|
| 0   | 8    | magic `"OS88FS2"` then one zero byte       |
| 8   | 2    | sectors per track (18 or 9)               |
| 10  | 2    | heads (2)                                 |
| 12  | 2    | file count (0..32)                        |
| 14  | 498  | zero                                      |

(Format v2: v1 disks — magic `OS88FS1`, no icon table, data from LBA 3 —
are no longer accepted; nothing shipped in v1, so no compatibility
shim.)

**LBA 1–2 — directory**, 32 entries × 32 bytes:

| off | size | contents                                            |
|-----|------|------------------------------------------------------|
| 0   | 16   | file name, printable ASCII, NUL-padded (≤15 chars)  |
| 16  | 2    | type: 1 = application package (.o88)                |
| 18  | 2    | start LBA of file data                              |
| 20  | 2    | size in bytes                                       |
| 22  | 10   | zero                                                |

**LBA 3–6 — icon table**, 32 entries × 64 bytes, entry i belongs to
directory entry i: 16 words of AND-style mask (white underlay) then 16
words of data (black pixels), bit 15 = leftmost pixel, row-major (the
§25 16×16 icon body, without the 2-byte header). An all-zero entry means
"no icon" — viewers fall back to the built-in `ico_app16`. os88disk.py
fills entries from each package's embedded icon (§20.2 flags bit 0), or
zeros.

File data starts at LBA 7; every file starts on a sector boundary. Entries
are packed from index 0; `file count` in the superblock is authoritative.

## 20. Loadable programs — the .o88 package format

### 20.1 The pool

A package is a flat 8086 binary assembled with `org APP_LOAD_OFF` (0xB000
— the **link base**) and **relocated at load time** to a per-instance
region allocated from the 19.5KB pool 0xB000..0xFDFF (§21). It runs in the
same near model as kernel code: CS=DS=KERNEL_SEG, SS=LOW_SEG, near calls
everywhere, §1 hard rules apply (cpu 8086, register discipline, no bare
`sti` in handlers). Its paint/onkey/onclick procs are near pointers into
its region. Budget: image + zeroed-bss ≤ APP_MAX_SIZE (0x4E00). Multiple
package instances can be resident at once — including two instances of
the same package: each is a fully relocated copy with its own image, data
and bss, so package state (equ offsets from `os88_image_end`) is
per-instance automatically. Closing an instance frees its region (§29.2
rule 7).

### 20.2 Header — first 32 bytes of the file (and of each loaded region)

| off | size | contents                                                  |
|-----|------|------------------------------------------------------------|
| 0   | 2    | magic: bytes `'O','8'` (word 0x384F)                      |
| 2   | 1    | format version = 2 (relocatable; v1 files are rejected)   |
| 3   | 1    | flags: bit 0 = embedded icon follows the header; bits 1–7 zero |
| 4   | 2    | link base — must equal `APP_LOAD_OFF` (0xB000, the org the image was assembled at) |
| 6   | 2    | entry offset, **image-relative** (≥ 0x20; ≥ 0x60 with icon; < image size) |
| 8   | 2    | image size = resident bytes: header + icon + code + data (**excludes** the reloc table) |
| 10  | 2    | bss size — bytes the loader zeroes after the image        |
| 12  | 2    | relocation count n (0 legal). NASM emits 0; **os88pkg.py stamps the real count** |
| 14  | 2    | zero (reserved)                                           |
| 16  | 16   | program name, printable, NUL-padded (shown by tools)      |

**Relocation table**: appended immediately after the image — n
little-endian words. Each entry's low 15 bits are the **image offset** of
a word to patch (in [0x20, image−2], ascending, non-overlapping; image ≤
0x4E00, so bit 15 is free); bit 15 is the fixup **class**:

- bit 15 = 0 — an embedded package address (imm16 / disp16 / `dw label`):
  the loader **adds** `base − `APP_LOAD_OFF``.
- bit 15 = 1 — the rel16 displacement of a near call/jmp to a **fixed
  kernel offset** (`call OSAPI_*`): the displacement is target − (site+2),
  and the site moves with the base while the target does not, so the
  loader **subtracts** `base − `APP_LOAD_OFF``. (Package-internal relative
  branches shift with both ends and need no fixup.)

Total file bytes = image + 2n (this is what the os88fs directory size
field holds; the dir type stays 1). After the patches the table is dead
(bss zeroing overwrites it). The table is generated host-side by **dual
assembly** (§24): the Makefile assembles each package twice — at org
0xB000 and org 0xB800 (`-DOS88_ORG=0xB800`) — and os88pkg.py diffs the two
images: a word whose value grew by 0x800 is class 0, one that shrank by
0x800 is class 1. **Author rule (binding)**: a package address may only
ever be embedded as a whole 16-bit word — never byte-truncated, shifted,
split, or folded into a non-address constant. os88pkg's reconstruction
check refuses the package otherwise, so violations fail the build.

**Embedded icon** (flags bit 0): file offset 32..95 holds the program's
16×16 icon — 16 mask words then 16 data words (same body layout as the
os88fs icon table, §19). With the flag set, image size must be ≥ 96 and
the entry offset ≥ 0x60. os88disk.py copies the block into the disk's icon
table; the kernel points the instance's dock tile (I_ICON, §29) at the
block inside the loaded region.

**Entry contract** (unchanged from v1): near-called by the loader — at
`base + entry` — with DS=ES=KERNEL_SEG, IF=1, gfx lock NOT held. The code
is fully relocated, so its own labels already encode the base; no base
register is passed. The program creates its window(s) via the API table
(wm_create is lock-free) and **returns** BX = window ptr with CF clear;
the loader registers the instance (§29) and wm_shows it. CF set = abort
(loader reports "load failed"); an aborting entry must return BX = its
already-created window ptr (or 0 if it created none) so the loader can
wm_destroy it — otherwise aborted loads would leak window records.
The entry must not call wm_show/wm_hide/wm_front, spawn tasks, or draw.
After entry returns, the program is pure event-driven code: its W_PAINT /
W_ONKEY / W_ONCLICK procs run under the gfx lock per §11.

### 20.3 The os8088 API jump table (kernel.asm, fixed offsets)

At KERNEL_SEG:0x0010, 4-byte slots, each `jmp near target` + 1 pad byte.
Programs `call` these absolute offsets; register contracts are the target
routines' own (§5, §6, §8, §11). Pinned layout:

```
0x0010 gfx_lock        0x0034 gfx_xor_fill    0x0058 wm_obscured
0x0014 gfx_unlock      0x0038 font_char       0x005C task_yield
0x0018 gfx_pixel       0x003C font_str        0x0060 task_sleep
0x001C gfx_hline       0x0040 font_width      0x0064 osapi_get_ticks
0x0020 gfx_vline       0x0044 wm_create       0x0068 osapi_set_color
0x0024 gfx_fill        0x0048 wm_show         0x006C osapi_mouse
0x0028 gfx_frame       0x004C wm_hide         0x0070 osapi_srand
0x002C gfx_fill_gray   0x0050 wm_front        0x0074 osapi_rand
0x0030 gfx_xor_rect    0x0054 wm_content
```

**Sound slots (§34), append-only from 0x0078 — all five ship in Phase 1.**
Only 0x0078/0x007C are live there; the PLAY, FM and STREAM targets are
3-byte stubs returning the error path (CF=1 / AX = 4 no-sink) until their
phases land, so a package built against the full ABI never jumps past the
table end on any kernel from Phase 1 on. The
kernel.asm table-span assertion goes 26 × 4 → **31 × 4** once, in the same
change as the slots; `apps/os88api.inc` mirrors the new `OSAPI_*` equs
(§20.5). The targets live in snd.inc; contracts:

```
0x0078 osapi_snd_caps    out AX = merged caps word (§34.2), BL = tone route
                         (0 = speaker, 1 = OPL2), DX = present-driver bits.
0x007C osapi_snd_tone    in AX = freq Hz (0 = off), CX = duration ticks
                         (0 = until off), DL = priority (§34.3); out CF=1
                         refused (higher-priority owner), else CF=0 and
                         AL = owner generation (matches status queries).
0x0080 osapi_snd_play    PCM_EXCL clip (§34.4): ES:SI = 8-bit unsigned
                         mono, CX = count, DX = rate Hz (N = 1193182/rate
                         must land in 74..255); BLOCKS for the clip; a
                         mouse click aborts it. out AX = 0 ok / 1 busy
                         (incl. a PCM_BG stream open) / 2 rate /
                         3 disabled-by-user / 4 no sink / 5 aborted.
                         ES restored per §1.            (live Phase 2)
0x0084 osapi_snd_fm      in AL = verb: 0 note-on / 1 note-off / 2 patch /
                         3 all-off; CL = channel, BX = freq Hz, DS:SI =
                         11-byte patch (§34.2); out CF=1 no FM sink, or
                         refused: bad verb/channel/frequency, a reserved
                         or another requester's channel (§34.3).
                                                        (live Phase 3)
0x0088 osapi_snd_stream  PCM_BG / PCM_IN + staging: verbs below.
                                                        (live Phases 4–5)
```

`osapi_snd_play` takes **ES:SI** (not DS:SI) so a clip staged in `SND_SEG`
plays in place without copying into the shared kernel segment; packages
that keep clips in their own image just set ES = DS. (The busy loop is
CPU-paced — no DMA constraint — so an arbitrary caller buffer is legal
here, unlike the SB path, §34.5.)

**`osapi_snd_stream` verbs** (AL = verb, AH = stream handle where one
exists; §34.5–34.6). Grant offsets are absolute `SND_SEG` offsets
everywhere they appear (verb 7 returns one; verbs 0 and 6 take one — a
caller holding several grants is unambiguous by construction):

| verb | name | contract |
|------|------|----------|
| 0 | open-out | DX = rate Hz (4000..22222, the §34.5 TC range), SI = grant offset, CX = valid bytes staged so far (> 0, inside the caller's grant); spawns the kernel refill task (§34.5); out AL = 0 with AH = handle, else AX = err. Streams must be fully staged before playback (§34.5) — CX short of the grant **is** the caller explicitly accepting progressive-feed risk. |
| 1 | feed | AH = handle, CX = new total valid length (never smaller, never past the grant's end) — extends a progressively staged stream; an underrun-paused stream resumes (§34.5). Out AX = 0 / err. |
| 2 | close | AH = handle; halts playback, frees the stream record (its refill task exits at the next wake). Out AX = 0 / stale. |
| 3 | status | AH = handle; out AX = state, DX = bytes consumed (capped at the valid length; input: bytes captured into the grant) — **this poll is the notification mechanism**: callbacks check it; there are no sound events (§34.3). States, pinned: **0 playing (input: recording), 1 underrun-paused (§34.5 — data ran out or the refill starved; resumable by a feed. Input, §34.6: capacity full, or the drain starved), 2 ended (stopped by the §34.5 watchdog), 0FFFFh stale.** A fully-staged clip that plays out reads underrun-paused with DX = its length — the owner's cue to close, exactly as a capacity-full capture reads paused with DX = its capacity; "ended" is reserved for the watchdog stop. |
| 4 | open-in | DX = rate Hz (4000 up to the §34.5 input ceiling: 13,000 on a single-cycle DSP, 15,000 on auto-init), SI = grant offset, CX = capture capacity in bytes (> 0, inside the caller's grant); spawns the kernel drain task, which moves record-ring halves into the grant until CX bytes have landed, then stops the DSP (§34.6). Out AL = 0 with AH = handle, else AX = err. **Half-duplex with playback is err 1 busy** (§34.3): one stream record, either direction. Feed on an input stream is err 7 — a capture has no valid length to extend. |
| 5 | read | copy CX bytes from the grant at offset SI into caller DS:DI — the kernel-staged copy out, verb 6's exact mirror (§34.6); the range must sit inside one grant the caller owns. **No handle**: the grant owns the bytes, so captured data stays readable after the stream that recorded it closes, until the grant is freed. Works on every machine, like verbs 6–7. Out AX = 0 / err 7. |
| 6 | stage | copy CX bytes from caller DS:SI into the grant at offset DI (kernel-staged copy — callers never touch ES = SND_SEG, §2.2); the range must sit inside one grant the caller owns. Out AX = 0 / err. |
| 7 | grant | AH = sub-op (0 alloc, 1 free): CX = bytes → out SI = grant offset, or free (SI = offset, owner only — **refused with err 7 while an open stream's read offset sits inside the grant**: close the stream first, or the refill task would copy from unowned, re-allocatable bytes); stamped with the calling instance, force-freed by `snd_release_inst` (§34.3 — teardown is safe: it closes the stream before freeing grants, and the drain copy is teardown-fenced so a mid-copy preempted drain task cannot write into freed bytes, §34.6). Out AX = 0 / err. |

Error codes, pinned (AX; CF set on any error, and on the stale status):
0 ok, 1 busy (a stream is already open, or a `PCM_EXCL` clip is running),
2 rate out of range, 4 no sink (no card, no IRQ discovered, or the verb's
phase has not landed), 6 no task slot, 7 bad argument (grant/range/
length/sub-op), 8 no staging space, 0FFFFh stale handle. The staging
verbs 5–7 work on every machine — the pool exists wherever `SND_SEG` does
(§2.2); only the stream verbs (0–4) need the card.

**Window-management slots (§11.1/§11.2), append-only from 0x008C.** The
kernel.asm table-span assertion goes 31 × 4 → **34 × 4** across these
additions; `apps/os88api.inc` mirrors the equs plus WF_SIZABLE/WF_FULL and
WMIN_W/WMIN_H (§20.5):

```
0x008C wm_sizable      in BX = win ptr, AL = 0 clear / non-zero set
                       WF_SIZABLE (§11.1). UI-task context only (entry
                       procs and window callbacks qualify).
0x0090 wm_fullscreen   in AL = 1 enter (BX = win ptr) / 0 exit; caller
                       holds the gfx lock (window callbacks do); out
                       CF=1 = enter refused, screen already owned
                       (§11.2).
0x0094 wm_grow_paint   in BX = win ptr; caller holds the gfx lock. The
                       grow-box restore of §11: a resizable package's
                       self-initiated content repaint must end with this
                       call (the white-fill idiom erases the corner). A
                       no-op unless BX is the frontmost visible window
                       with WF_SIZABLE set and WF_FULL clear, so it is
                       always safe to call.
```

**Stream verbs are UI-task/window-callback context only (binding).**
Every caller — packages and Control Panel alike — runs inside a window
callback on the single UI task, and the half-duplex/busy refusals of the
open verbs rest on that serialization: the open-verb busy check and the
eventual stream-record publish are many IF=1 instructions apart (IRQ
discovery can even sleep on ticks between them), which two concurrent
opens would race. A future background-task caller must first make the
record claim a single `pushf`/`cli` unit (the grant allocator's
scan+claim standard, §34.6) — it cannot simply start calling these verbs
from a task.

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
for every table offset (§20.3), the window-record W_* / template offsets
(§11), color constants, and a `OS88_HEADER 'NAME', entry_label` macro that
emits the §20.2 header (image size via a forward-referenced
`equ` to an end label the program declares with `OS88_BSS n` /
end-of-file macro — exact macro design is the implementer's, but a package
source must be able to consist of just `%include "os88api.inc"`, the header
macro, code/data, and an end macro). OS88_HEADER opens with
`org OS88_ORG` when that macro is defined (`-DOS88_ORG=0xB800`, the §24
relocation-probe pass) and `org APP_LOAD_OFF` otherwise; it emits version
2, the image-relative entry (`entry − $$`), and a zero relocation count
for os88pkg.py to stamp. The org value must only ever affect emitted
addresses, never instruction selection — os88pkg verifies this (equal
lengths, whole-word diffs).

Icon support: `OS88_HEADER 'NAME', entry, 1` sets flags bit 0; the author
then writes `OS88_ICON16` (asserts, via `%if`-on-equ, that it starts at
offset 32), 32 hand-authored `dw` rows (16 mask, 16 data), and
`OS88_ICON16_END` (asserts offset 96). The third OS88_HEADER parameter is
optional and defaults to 0.

## 21. loader.inc

State (.bss, cleared by `loader_init`): `ld_pending` (word: 0 = none, else
directory index+1 — set by files.inc, consumed by ui.inc §13), `ld_status`
(byte: 0 ok, 1 disk error, 2 not a valid package, 3 too large, 4 entry
aborted, 5 out of memory), plus loader_run scratch words (registers run
out on the 8086). `ld_appwin` is gone — the instance table (§29) tracks
residency.

**The pool allocator is the instance table.** A package record's byte
range [I_SPTR, I_SPTR + I_SIZE) is occupied iff I_STATE ≠ 0 (§29.2 rule
7); I_SIZE is the ALLOCATED size (512-multiple). `ld_alloc` (in: AX =
bytes, a 512-multiple; out: CF=1 no hole, else BX = region base):
first-fit lowest base over [0xB000, 0xFE00) — start at 0xB000; if any
in-use package record overlaps [start, start+AX), set start = that
record's end and rescan from the top; fail when start + AX > 0xFE00.
UI-task-only, so allocation never races itself; freeing is the record
store (task-less close path or task_exit, §29). No compaction — regions
never move once relocated.

`ld_check_hdr` (module-internal) — in: SI → 32 readable header bytes,
[ld_fsz] = file size; out: CF=0 + scratch (img/bss/entry/reloc-count)
filled, or CF=1 + AL = status. Checks: magic; **version = 2** (a v1 file
→ "Bad package"); link base = `APP_LOAD_OFF`; image ≥ 0x20; entry in
[0x20, image) (icon rule enforced by os88pkg, not re-checked); image+bss ≤
APP_MAX_SIZE (else "Too large"); image + 2·count = file size (guards
truncated files and stale directories).

`loader_run` — in AX = directory index. UI task only, gfx lock not held
on entry. Steps:
1. Validate the entry: index < [disk_nfiles], type = 1, size ≤
   APP_MAX_SIZE and non-zero → else status 2, step 10. **No eviction
   exists** — a load never disturbs running instances; every failure
   path below frees whatever it reserved and leaves them untouched.
2. Peek the header: `disk_read` 1 sector (the file's first) into
   `dsk_secbuf` (UI-task-only shared scratch). CF → status 1.
3. `ld_check_hdr` on the peek → status 2/3.
4. need = roundup512(max(image+bss, file size)); > APP_MAX_SIZE → status
   3. (Sector-granular allocation makes the whole-file read safe: it
   writes ceil(fsize/512)·512 ≤ need bytes, never a neighbour's region.)
5. `inst_alloc` (§29) → CF → status 5. `ld_alloc` need bytes → CF →
   status 5 (the unpublished instance record stays free). Note the
   record is not yet published, so the region is reserved only by
   single-threadedness (rule §29.2.8).
6. `disk_read` the whole file — ceil(fsize/512) sectors — to
   ES=KERNEL_SEG, BX=base. CF → status 1. Re-run `ld_check_hdr` against
   the in-region header (the disk could have been swapped between the
   peek and the read) → status 2/3.
7. Relocate: for each of the n table words at base+image (each validated
   in [0x20, image−2] → else status 2): word at base+offset += base −
   `APP_LOAD_OFF`.
8. Zero bss-size bytes at base+image (this overwrites the reloc table —
   it is disposable).
9. Near-call base+entry (contract §20.2; DS=ES=KERNEL_SEG, lock free).
   CF → the abort path (BX sanity-checked exactly as before: inside
   wm_wins, record-aligned, then locked wm_destroy), status 4. Else:
   fill the reserved instance record — I_KIND = KIND_PKG, I_TASK = 0xFF,
   I_SPTR = base, I_SIZE = need, `inst_set_name` from base+16, I_ICON =
   base+32 when header flags bit 0 else 0 — `inst_bind_win` BX, publish
   I_STATE = 1, then gfx_lock, `wm_show` BX, gfx_unlock, status 0.
   (`wm_create` failing inside the entry surfaces as CF = status 4.)
10. Set `[ld_status]`, call `files_refresh` (§22).

Closing a package instance is §29's task-less path: locked wm_destroy +
I_STATE ← 0 — that store frees the region. The Task Manager's RAM readout
sums package records' I_SIZE under one cli (§28) and no longer peeks at
package headers; the old "`ld_appwin` zeroed before the region is
overwritten" invariant is retired.

## 22. files.inc — the Disk window (file manager)

Built-in singleton app kind (KIND_FILES, cap 1 — the mount state below is
module-global), title "Disk", 320×200 at (110,80), **resizable**
(KD_WFLAG = WF_SIZABLE, §11.1/§29.3 — the one built-in that is). No
background task, no boot-time window: `files_init` (from kmain) only
resets module state; the window is created on demand by `app_launch`
(§29), whose KD_INIT is `fm_kinit` (clears `fm_sel`/`fm_clkt`, resets
`fm_scroll` and `fm_view`). State: `fm_sel` (word, selected **directory
index** — not a row; 0xFFFF = none), `fm_clkt` (word, birth tick of the
last entry click), `fm_mountok` (byte, 1 = last mount succeeded),
`fm_view` (byte, 0 = list view, 1 = icon view), `fm_scroll` (word, first
visible row — an entry row in list view, a grid row in icon view).

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

All coordinates below are content-relative. Header line at (6,6):
`"Drive B:  N files"` (drive letter from [disk_drive]) or, when the last
mount failed, `"No os8088 disk (B:)"` (19 chars — short enough to clear
the buttons at the default width; it was "No os8088 disk in drive B:"
until the view toggle claimed that room). Two buttons at the top right,
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
cw). Status line from `[ld_status]` at (6, status_y): "", "Disk error",
"Bad package", "Too large", "Load failed", "Out of memory" (0..5) — plus
"Loading..." while a load is pending — truncated to (cw−12)/8 chars
through the same scratch-buffer idiom as the header ("Out of memory" is
104px and a legal resize can leave cw = 94). In list view the name is
truncated to the room left of the size column ((cw − 88)/8 chars); every
string the window draws is bounded by the live cw one way or another.

**The row area** spans x 0..cw−16, y 22..list_bot−1 (a 2px gutter before
the scroll bar); when `rows_fit` is 0 the row area and the scroll bar are
simply omitted (a legal degenerate window). Rows shown = min(total −
fm_scroll, rows_fit), where total = [disk_nfiles] rows in list view,
ceil(nfiles / cols) grid rows in icon view. `fm_scroll` is clamped to
0..max(0, total − rows_fit) at every use — paint clamps first, so a
shrink-resize self-heals.

- **List view** (fm_view = 0): rows 16px tall, entry index = row +
  fm_scroll. Per row: the file's 16×16 icon at x=4 (from `disk_icons`
  entry i; all-zero entry → built-in `ico_app16`, §25), name at x=24,
  size right-aligned ending at cw−22, text baselines at row top + 4.
- **Icon view** (fm_view = 1): a grid of 78×40 cells, `cols` per grid
  row, cell (r, c) at (c·78, 22 + (r − fm_scroll)·40), entry index =
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
fm_scroll·track_h/total, clamped to the track; otherwise the track is
bare and the arrows are inert. One degenerate exception: a track shorter
than the 8px minimum thumb (frame heights 83..89) stays bare and its
clicks do nothing — the arrows and keys still scroll. There is **no thumb drag** — W_ONCLICK is
a one-shot dispatch under the held lock, and a tracking loop belongs to
the UI task (§13), so the bar scrolls by clicks alone (a deliberate,
recorded scope cut): up/down arrow = one row; a track click above the
thumb's top = a page (rows_fit rows) up, anywhere else in the track = a
page down.

Behaviour:
- `files_open_drive` (public; in AL = drive 0/1, no lock held): **always**
  `disk_mount` DL=AL — a swapped or newly chosen disk must never show
  stale contents — record success in fm_mountok, clear the selection,
  then `app_launch` KIND_FILES (creates the window, or fronts +
  un-minimizes the existing instance at cap; §29). Callers: CMD_FILES
  dispatch and desk_click (§26).
- `files_open` (from CMD_FILES dispatch, no lock held): AL = [disk_drive],
  fall into files_open_drive.
- `W_ONCLICK` (lock held; every path below ends in the content repaint —
  white-fill own content from the **live** W_W/W_H + redraw + a closing
  `wm_grow_paint` (§11), because the white fill erases the grow box):
  test order is buttons → scroll bar → rows.
  1. Refresh rect → `disk_mount` the current drive, update fm_mountok,
     clear selection, reset fm_scroll.
  2. View-toggle rect → flip fm_view, reset fm_scroll (selection kept).
  3. Scroll bar (only when the row area is drawn) → adjust fm_scroll per
     the arrow/track rules above, clamped; nothing else changes.
  4. Row area → map to an entry index per the current view (icon view:
     reject x past cols·78); y < 22, past the shown rows, or index ≥
     [disk_nfiles] → clear selection. Index == fm_sel and
     [ui_click_t]−fm_clkt < 9 (birth ticks, §10) → double-click: set
     [ld_pending] = index+1 (ui.inc runs the loader after the lock
     drops; the repaint shows "Loading..."). Else select it (fm_sel =
     directory index), stamp fm_clkt.
- `W_ONKEY` (lock held): 'a'/'A' → drive 0, 'b'/'B' → drive 1, 'r'/'R' →
  same drive; all three: `disk_mount`, update fm_mountok, clear selection,
  reset fm_scroll, repaint content. 'v'/'V' → toggle the view like the
  button. Scan codes: Up/Down (48h/50h) scroll one row, PgUp/PgDn
  (49h/51h) a page — the view, not the selection, clamped like the bar.
  Enter (13) with a valid selection → same as double-click. (disk_mount
  under the gfx lock stalls painters ~a second; acceptable.)
- `files_refresh` (called by loader_run, no lock held): find the live
  Disk instance via `inst_find_kind` KIND_FILES (§29) — none = nothing to
  do (the user closed the window mid-load); else acquire gfx_lock, and if
  its window is visible call `wm_paint_all`; unlock. It must be a
  full repaint, not content-only: loader_run calls it right after wm_show
  raised the loaded program's window, which may overlap the Disk window —
  a content-only repaint would paint over the new front window.

## 23. Minesweeper — the first software package (apps/mines/mines.asm)

Not kernel code: a .o88 package built with os88api.inc, org `APP_LOAD_OFF`, all
services via the API table. Label prefix `mn_`. Everything below is
content-relative; the procs fetch the content origin via `wm_content`
(JAPI) each call.

- Window: "Minesweeper", 146×183 at (240,120) → content 144×164.
- Board: 9×9 cells, 16×16 px each, at content (0,20). 10 mines.
- Status strip (content rows 0..19, light gray): mines-remaining counter
  "10" minus flags placed (may go negative → clamp display at 0) at left;
  center text: playing → "F=FLAG" when flag mode is ON else blank;
  lost → "BOOM! N=NEW"; won → "YOU WIN!". Keep every string ≤ 17 chars.
- Cell rendering, 16 colors, Mac-meets-Win31: covered = light gray face,
  2px white bevel top/left, 2px dark gray bevel bottom/right; flag = red
  (12) pennant + black mast on a covered cell; revealed = white face, 1px
  dark gray grid border, centered colored digit; digit colors:
  1=1 (blue), 2=2 (green), 3=4 (red), 4=5 (magenta), 5=6 (brown),
  6=3 (cyan), 7=0 (black), 8=8 (dark gray). Mine = black disc with spokes;
  the one you clicked sits on a light-red (12) cell, others (shown on
  loss) on light gray. Wrong flags on loss: mine + black X.
- Input: W_ONCLICK reveals (or flags, in flag mode) the cell under the
  click. W_ONKEY: 'f'/'F' toggles flag mode (repaint status strip),
  'n'/'N' new game (repaint content). Space = reveal is not required.
- Rules: first reveal is always safe — mines are placed lazily on the
  first reveal (osapi_srand with osapi_get_ticks, then osapi_rand), excluding
  the clicked cell. Reveal of a 0-count cell flood-fills neighbours
  (iterative, explicit queue in the package's own buffer — no recursion;
  stack budget is the UI task's 1536 bytes). Flags block reveal. Reveal of
  a mine → lost (reveal all mines, mark wrong flags). All 71 safe cells
  revealed → won. After win/lose, clicks are dead until 'N'.
- Handlers repaint only what changed (single cells; whole board on
  new-game/win/lose), Note-Pad-style, under the caller's lock.

## 24. Packaging toolchain (host side)

Python 3, stdlib only, both tools executable with clear argparse `--help`
and non-zero exit + stderr message on any validation failure.

- `tools/os88pkg.py IN.bin --alt ALT.bin -o OUT.o88` — package
  validator/reloc-generator/stamper. IN is the org-0xB000 assembly, ALT
  the same source at org 0xB800 (`-DOS88_ORG=0xB800`; the probe delta
  0x800 has a zero low byte, so every fixup word differs in exactly its
  high byte). Steps, any failure → exit 1, no output: (1) equal lengths
  (an org-dependent encoding would desync the images); (2) header
  validation — magic, version 2, link base 0xB000, image field == file
  size (no table yet), entry image-relative in [0x20, image) (≥ 0x60
  with the icon flag), image+bss ≤ 0x4E00, reloc-count field 0, reserved
  0, flags bits 1–7 zero, name printable ≤15 NUL-padded — plus
  byte-equality of the two headers (and icon blocks); (3) diff scan:
  each differing byte at offset i must satisfy i ≥ 33, fixup site
  o = i−1, (ALT[i] − IN[i]) & 0xFF ∈ {8 (class 0), 0xF8 (class 1)},
  IN[o] == ALT[o], o ≤ image−2, o ≥ previous o + 2; (4) **hard verify**:
  IN with 0x800 added (class 0) / subtracted (class 1) at every recorded
  fixup word must reconstruct ALT byte-for-byte — this is what catches
  split/truncated addresses (§20.2 author rule); (5) loadable
  bound: max(image+bss, roundup512(image + 2n)) ≤ 0x4E00; (6) emit IN
  with the count stamped at offset 12 and the n sorted offsets appended.
  Summary line gains `relocs=N`.
- `tools/os88disk.py -o OUT.img --size {1440,360} [PKG.o88 ...]` — builds a
  os88fs v2 floppy (§19): superblock geometry 18/2 or 9/2, directory
  entries named from each package's header name field, icon table LBA 3–6
  (entry i = package i's embedded icon bytes 32..95 when flags bit 0,
  else 64 zero bytes), data from LBA 7, sector-aligned. Accepts format-v2
  packages only: version byte 2 and image + 2·reloc-count == file size
  (a v1 file fails with "rebuild with the v2 toolchain"). The directory
  size field is the full file length, reloc table included. Zero packages
  is legal (an empty disk — useful for testing Refresh). Fails if >32
  files or the disk overflows. Total image size: 1474560 or 368640 bytes.
- Makefile: `build/mines.bin` from `apps/mines/mines.asm` and
  `build/hello.bin` from `apps/hello/hello.asm` (§27), each
  (`nasm -f bin -w+error -I apps/`, dep on apps/os88api.inc), plus a
  second assembly of each at org 0xB800 (`-DOS88_ORG=0xB800` →
  `build/X.alt.bin`); both fed to os88pkg.py (`X.bin --alt X.alt.bin`),
  then `build/apps.img` (1440) + `build/apps360.img` (360) from
  **mines.o88 + hello.o88** via os88disk.py; all built by `all`.
  `run`/`debug`/`test` attach build/apps.img as floppy index 1. 86Box's
  fdd_02 gets apps360.img (best-effort config keys).

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
| `icon_draw` | in CX=x, DX=y (top-left), SI → record. Caller holds the gfx lock. Clips to the screen (skip clipped rows/bytes — windows can hang off the bottom edge). Leaves the GC in the default state (§1 rule 7); preserves registers. |
| `icon_draw16`| in CX=x, DX=y, SI → **body only** (16 mask words + 16 data words, no 2-byte header) — the §19 icon-table / §20.2 embedded-icon layout. |
| `ico_disk32`| library record: 32×32 floppy disk (rect body, shutter, label area) |
| `ico_app16` | library record: 16×16 generic application (a recognizable "program" glyph, e.g. a diamond/tool shape) |

Bitmaps are hand-authored `dw` rows (like the menu-bar logo, the one
sanctioned place for hand-made bitmap data — icons are the second).

Under double buffering (§32) `ico_core` branches after its clip/shift
setup to a software pass pair: the white underlay ORs the shifted mask-row
bits into all four back-buffer planes, the black pass AND-NOTs the data-row
bits out of them — same 3-byte window, same edge clipping as the VRAM
passes.

## 26. desk.inc — desktop drive icons

State: `desk_ndrives` (byte), `desk_sel` (byte, 0xFF = none),
`desk_clkt` (word). `desk_init` (from kmain): int 11h equipment word —
if bit 0 is set, drives = ((AX>>6) & 3) + 1, else 0; clamp to 2. QEMU
with two floppy `-drive`s reports 2.

Layout, per drive i (0 = A, 1 = B): hit zone x 584..631, y
(32+60·i)..(75+60·i); inside it the 32×32 `ico_disk32` at x=592,
y=32+60·i, and below it the label "Disk A"/"Disk B" (48px), black text
on a white gap 2px around the text, centered in the zone.

| symbol       | contract                                                    |
|--------------|--------------------------------------------------------------|
| `desk_paint` | draw every drive's icon + label; the selected one (desk_sel) gets `gfx_xor_fill` over its hit zone. Called by wm_paint_all after the desktop fill (lock held by caller). |
| `desk_click` | in CX=x, DX=y (no lock held; called by ui.inc when wm_hit found no window and `dock_click` declined the click, §30). Zone hit: if same zone as desk_sel and [ui_click_t]−desk_clkt < 9 (birth ticks, §10) → clear the selection and call `files_open_drive` with AL = drive. Else select it, stamp desk_clkt. Miss: clear any selection. All its own drawing (selection flips) happens under gfx_lock/gfx_unlock acquired internally, redrawing only the affected zones — EXCEPT when a visible window overlaps a zone's drawn rect (x 582..633 with the label overhang, window rect incl. the 1px shadow): a partial redraw would paint desktop over that window, so the flip falls back to a full wm_paint_all under the same lock. |

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
`wm_sizable` (slot 0x008C) right after wm_create, making it the first
package to exercise §11.1 and the proof that the SDK path works. Paint
renders the buffer at 8px per char with a 6px left/top margin, wrapping at
the content width, dropping any row whose bottom would pass the content
bottom (no scrolling) and drawing a 1px caret only when its own row fits —
all computed from the **live** window record each call, which is exactly
why resizing costs the paint proc nothing: the next repaint re-wraps at
the new width. Onkey appends printable 32..126, deletes on backspace,
stores 13 on Enter, then white-fills (live W_W/W_H) and redraws **its own
content only**, ending with `wm_grow_paint` (slot 0x0094) per §11.1 — the
white fill erases the grow box. No icon flag, so the Disk window shows
`ico_app16`.

Two things got simpler in the move. The built-in reached its state through
`inst_of_win` → `I_SPTR` because every instance shared one pool; a package
addresses its own bss directly (`np_len` word + `np_buf` 512 bytes + three
paint scratch words = `NP_BSS_TOTAL` 520). And the cap is gone: the pool
that fixed it at 2 no longer exists, so instances are bounded only by the
region pool and the instance table like any other package.

Removing the kind renumbered two pinned sets — `KIND_CLOCK`..`KIND_CTRL`
down by one (§29) and `CMD_CLOCK`..`CMD_REBOOT` down by one (§12), the File
menu losing its first item and re-basing on `CMD_CLOCK`. Directory order on
the apps disk stays mines, hello, notepad: the first two keep their indices
so existing tests are unaffected.

## 28. taskmgr.inc — the Task Manager window

Built-in singleton app kind (KIND_TASKMGR, cap 1 — one sampler), window
"Task Manager", 176×264 at (250,100). Label prefix `tm_`. No onkey, no
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
already holds, so it white-fills the whole content rect (174×245 from the
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
and every loaded package, which cannot spawn at all (§20.2/§21) — runs
purely inside window callbacks on the UI task and would never appear.
Rows are therefore `TM_ROWS = INST_MAX + 1`: row 0 is **System** (the
kernel), row 1+i mirrors instance slot i, so a row's position is stable
for as long as the instance lives (the slot↔tile rule of §30). Each
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
  and I_TASK < MAX_TASKS (a free record's I_TASK byte is stale). Cycles
  of a task whose instance died this interval belong to no row and simply
  drop out. total = Σ row_i; normalize by shifting total and every row
  right while total's high word is non-zero; **total = 0 → every share is
  0 (no DIV ever executes with a zero divisor)**; else
  share_i = row_i·100/total (≤ 100 since row_i ≤ total).
- RAM, straight off the same snapshot (no second cli window): sum I_SIZE
  over slots with I_STATE ≠ 0 — built-ins carry I_SIZE 0, so this counts
  exactly the resident package regions, and dying instances still count
  because their region is still resident. The loader keeps ΣI_SIZE ≤
  APP_MAX_SIZE, so the 16-bit sum cannot overflow. used bytes =
  `kernel_bss_end` (bare label = the kernel text+bss footprint, org 0) +
  that sum. usedK = (used+1023) >> 10, **plus 150 when `[bb_on]` is set**
  (§32: the 4 × 0x9600-byte back buffer is exactly 150KB — added after the
  shift because 153600 does not fit a 16-bit byte count). The System row's
  memory cell adds the same 150K (via `tm_memcol_kb`, the KB-input tail of
  `tm_memcol`), so the rows still sum to the bar. Total KB is the
  boot-time int 12h value. **All bar math is in KB**: barw =
  usedK·160/totalK (`mul` then `div`; totalK cannot be 0 from int 12h,
  but a 0 check that skips the bar is required anyway).
- History: load% scaled to 0..40 (·40 then /100), stored at
  `tm_hist[tm_pos]`. The ring index IS the graph column — an oscilloscope
  sweep, no scrolling — then tm_pos advances mod 160.

**Drawing.** `tm_paint` (W_PAINT) dispatches on `[tm_view]` and runs the
active view's full body — bare and unconditional, no lock, no visibility
check (wm_paint_all calls it with the lock already held, §11). tm_task's
periodic path wraps its drawing Clock-style (§14): gfx_lock, re-check
visible + not `wm_obscured` under the lock (else skip), and touches only
what changed. Performance view: the CPU + scheduler text line (one line,
redrawn whole every interval, so a mode change shows up within one sample
period without any extra plumbing), the new sweep column plus an all-white
gap column at the advanced tm_pos, the RAM line and bar, and the process
rows — the full 160-column graph render happens only in tm_paint, so the
periodic lock hold stays small (Bounce-scale). Memory view: the RAM line,
the pool caption, both map interiors and the rows; the two map frames are
painted only by the full bodies (tm_paint / tm_click). All drawing is
self-backgrounding (each element white-fills its own rect or paints both
segments), so tm_paint needs no preceding content clear beyond the one
wm_paint_all already does.

**Content layout — performance view** (content-relative; content is
174×245):

- (6,4): the CPU + scheduler line, exactly 20 chars like the process rows
  below, so its last glyph lands at x = 158..165 inside the 174px content
  (white-fill (6,4)-(167,11) first): `[0..7]` `"CPU nnn%"` (n
  right-aligned, space-padded, 0..100), `[8]` space, `[9..19]` the
  **read-only** scheduler-mode field, left-justified and space-padded to
  11 chars — `"SCH preempt"` or `"SCH coop   "` — from `sched_mode_get`
  (§8.2). The Task Manager only *displays* the mode; it is changed from the
  Control Panel (§31). The padding is what erases the longer word when the
  mode changes, so the field must always be written full-width.
- Graph: 1px black frame (6,14)-(167,55); interior columns x = 7+i,
  i = 0..159, rows 15..54. Column value v (0..40): white vline rows
  15..54−v, then black vline rows 55−v..54 (v=0 → all white, v=40 → all
  black). The column at tm_pos draws all white (the sweep gap).
- (6,61): `"RAM uuuK/tttK"` (white-fill (6,61)-(167,68) first).
- RAM bar: 1px black frame (6,71)-(167,80); interior (7,72)-(166,79):
  black for barw pixels from the left, white for the remainder.
- (6,87): header `"NAME    ST  CPU MEM"`.
- Process rows r = 0..TM_ROWS−1 at y = 97 + 11·r (white-fill
  (6,y)-(167,y+7) first), exactly 20 chars — the 8px font puts the last
  glyph at x = 158..165, inside the 174px content. Columns, by index:
  `[0..6]` name left-justified in 7 (truncated), `[7]` space, `[8..10]`
  state, `[11]` space, `[12..14]` CPU share right-aligned, `[15]` `'%'`,
  `[16..18]` memory right-aligned, `[19]` `'K'`.
- Row 0 is the kernel: name `System`, state `run` if `sch_cur` was 0 at
  snapshot time else `rdy`, memory = `kernel_bss_end` rounded up to KB.
- Row 1+i renders instance slot i. Name is the I_NAME snapshot. State:
  I_STATE 2 → `die`; else I_TASK = 0xFF (or ≥ MAX_TASKS) → `evt`
  (task-less: it only runs inside window callbacks); else its task's slot
  = `sch_cur` → `run`, T_STATE 2 → `slp`, otherwise `rdy`. Memory =
  I_SIZE rounded up to KB, or `"   -"` (no `'K'`) when I_SIZE is 0 —
  every built-in kind, which owns no region of its own; a misleading `0K`
  is not used.
- A free slot renders `-` / `---` / `  -` / `   -`: name dash, state
  dashes, no `'%'` and no `'K'`.

**Content layout — memory view** (content-relative; the same 174×245):

- (6,4): the same `"RAM uuuK/tttK"` readout as the performance view
  (white-fill (6,4)-(167,11) first).
- Conventional-memory map: 1px black frame (6,14)-(167,29), interior
  (7,15)-(166,28) = 160×14. KB scale: KB k maps to interior column
  k·160/totalK; a region [a,b) KB fills columns a·160/totalK ..
  (b−1)·160/totalK inclusive, clamped so no region drops below 1px.
  The interior is white-filled (free), then: [0,64) 50% gray
  (`gfx_fill_gray` — IVT/BDA, the far-code blob at FAR_SEG and LOW_SEG's
  stacks + disk buffers, §2.1), [64,128) solid black (the kernel segment,
  detailed by the segment map below), [192,256) 50% gray (`SND_SEG`,
  §2.2 — claimed whole by the sound layer from boot, so the band is
  unconditional like the low keep), and [256,406) 50% gray while
  `[bb_on]` is set (the §32 back buffer, read live at draw time).
- (6,33): `"SEG 64K POOL"` + pool-used KB right-aligned in 3 (ceil of
  Σ I_SIZE over in-use snapshot slots) + `"K/20K"` — exactly 20 chars
  (white-fill (6,33)-(167,40) first; 20 = ceil(0x4E00/1024), the §21 pool
  size).
- Kernel-segment map: 1px black frame (6,43)-(167,58), interior
  (7,44)-(166,57) = 160×14. Offset scale: segment offset o maps to
  interior column o·160/65536 = o·5/2048 (16-bit `mul` by 5 carries into
  DX; the 32-bit divide by 2048 cannot overflow: DX ≤ 4); a region [a,b)
  fills a·5/2048 .. (b−1)·5/2048 inclusive, clamped ≥ 1px. The interior
  is white-filled, then: [0, kernel_bss_end) solid black (kernel text +
  bss), [0xFE00, 0x10000) 50% gray (above the pool, §21), then per
  snapshot slot with I_STATE ≠ 0 and I_SIZE ≠ 0 a `gfx_fill_pat` region
  [I_SPTR, I_SPTR + I_SIZE) in that slot's pattern. The headroom between
  kernel_bss_end and APP_LOAD_OFF and any unallocated pool read white —
  free space, drawn honestly.
- (16,62): header `"NAME    ADDR SIZE"` (17 chars).
- Process rows r = 0..TM_ROWS−1 at y = 74 + 11·r (white-fill
  (6,y)-(167,y+7) first): a legend square (6,y)-(13,y+7) — 1px black
  frame, interior (7,y+1)-(12,y+6) in the row's map fill — then 17 chars
  at x = 16 (2px clear of the square): `[0..6]` name left-justified in 7
  (truncated), `[7]` space, `[8..11]` ADDR, `[12]` space, `[13..16]` SIZE.
- Row 0 (System): square interior solid black; ADDR `0000`; SIZE =
  kernel_bss_end in KB rounded up — the black map region in numbers. The
  low-memory keep, the far blob and the back buffer belong to the top RAM
  line, not to this in-segment column.
- Row 1+i, in use with I_SIZE ≠ 0 (a package): square interior =
  pattern i; ADDR = the I_SPTR snapshot as 4 uppercase hex digits;
  SIZE = I_SIZE in KB rounded up.
- Row 1+i, in use with I_SIZE = 0 (a built-in kind): no square (the band
  stays white); name, then `"   -    -"` — it lives in kernel .bss, not
  the pool.
- A free slot: no square, name dash, `"   -    -"`.
- A dying instance (I_STATE 2) still draws its region and its row: the
  region is still resident (§21).

**Slot patterns.** `tm_pats` — 12 × 8 bytes in .text (far code reads
data through DS, §33 rule 2); pattern i = `tm_pats + i·8`, fixed
slot↔pattern for the instance's life (the §30 slot↔tile rule again).
All twelve are black-on-white dither/hatch textures (§5 bit sense: set =
white) chosen to stay tellable-apart at a few pixels' width; none is the
0xAA/0x55 50% checker, which the maps already use to mean
"system-reserved".

Menu/dispatch: see §12/§13 — "Task Manager" (CMD_TASKS = 3) is the System
menu's third item, under "Control Panel"; dispatch calls `app_launch`
KIND_TASKMGR like the §14 kinds.

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
I_TASK   equ 3    ; byte: task slot index (§8), 0xFF = task-less
I_WIN    equ 4    ; word: window record ptr (valid while I_STATE != 0)
I_SPTR   equ 6    ; word: builtin — per-instance state block ptr (0 = none)
                  ;       package — region base offset (>= 0xB000)
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
KIND_CLOCK   equ 2
KIND_BOUNCE  equ 3
KIND_FILES   equ 4
KIND_TASKMGR equ 5
KIND_CTRL    equ 6       ; Control Panel (§31)
KIND_PKG     equ 0x80    ; bit 7: package instance
```

### 29.2 Concurrency rules (binding)

1. **Publish-last**: a record becomes visible by the `I_STATE ← 1` byte
   store, which must be the LAST write when creating an instance (the
   task_spawn precedent, §8).
2. **Free points**: task-less instances are freed (I_STATE ← 0) by the UI
   task under the gfx lock (inside `app_close_win`); task-owned instances
   are freed by `task_exit`'s release byte — interrupt-atomically, and
   simultaneously with the task slot (§8).
3. **Lock-held readers** (dock, wm paint paths) may only dereference
   I_WIN/I_ICON/I_NAME of records read as I_STATE = 1 *during the same
   lock hold*; records read as I_STATE = 2 (dying) must be skipped.
4. **Lock-free readers** (the Task Manager) snapshot fields under
   `pushf`/`cli` … `popf`.
5. **Only the owning task destroys a task-owned instance's window** — the
   UI task may only `wm_hide` it. (Otherwise the wm slot could be reused
   while the sleeping task still holds the old ptr and would draw into a
   stranger's window.)
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
(word, task entry, 0 = task-less), `KD_POOL` (word, state pool base, 0 =
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
(stride 8), Bounce 10 (stride 8), Files 1 (module-global mount state),
TaskMgr 1 (one sampler), Control Panel 1 (no per-instance state, §31). The
per-kind caps deliberately over-subscribe INST_MAX now that Clock and
Bounce allow 10 each — `INST_MAX` (and MAX_TASKS, §8) is the real ceiling,
and a launch on a full table simply fails with CF=1.

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
| `app_close_win` | in BX = window ptr; **caller holds the gfx lock**; UI task only. Unowned window → wm_hide (fallback). I_STATE = 2 already → wm_hide (idempotent). Task-less (I_TASK = 0xFF) → I_STATE ← 2, wm_destroy (clears wm_owner, repaints), I_WIN ← 0, I_STATE ← 0 — for a package instance that final store frees the region (rule 29.2.7). Task-owned → I_STATE ← 2 (the die flag), wm_hide (instant feedback); the task tears down at its next wake. |
| `inst_minimize` | in BX = window ptr, lock held: set I_FLAGS bit0 (unowned → skip), wm_hide. |
| `inst_restore` | in DI = record, lock held: clear I_FLAGS bit0, wm_show I_WIN. |
| `inst_task_die` | in DI = the CURRENT task's instance record; no lock held; **never returns**: gfx_lock, wm_destroy I_WIN (clears wm_owner), I_WIN ← 0, gfx_unlock, then `jmp task_exit` with BX = record ptr (I_STATE is offset 0 — the release byte). |
| `inst_launch_post` | in AL = kind: one atomic word store of kind+1 into `inst_launch` — the deferred launch channel for lock-held posters (drained by ui_task step 3, §13). Rapid double posts coalesce (last wins). |

**Sound teardown (§34.3, Phase 1).** Both free points release the
instance's sound grants inside the teardown window they already own:
`app_close_win`'s task-less path calls `snd_release_inst` (in: AL =
instance index) before its final `I_STATE ← 0` store, and `inst_task_die`
calls it before `task_exit` — so tone ownership, FM channel bits, stream
ownership and staging grants (§34) join the window, the task slot and the
package region on the list of what teardown must free. Closing a live
stream ends its refill task. A closed package can never leave a tone
droning or a dangling SB stream.

### 29.5 State (.bss)

`inst_tab` (INST_MAX × I_RECSZ = 384 bytes), `inst_launch` (word: 0 =
none, else kind + 1), plus module scratch (template copy buffer, pool-slot
cursor — UI task only). All zeroed by `inst_init`.

## 30. dock.inc — the dock strip

A taskbar-style strip along the bottom of the screen showing one tile per
**running** instance (I_STATE = 1, §29), built-in or package. Minimized
instances stay in the dock with an inverted tile; clicking a tile restores
a minimized instance (`inst_restore`) or fronts a visible one (`wm_front`).
Label prefix `dock_`. The dock is not exposed to packages.

### Geometry (pinned)

```nasm
DOCK_H      equ 24              ; strip rows 456..479
DOCK_Y0     equ SCREEN_H - DOCK_H   ; 456: 1px black rule, full width
DOCK_TY0    equ DOCK_Y0 + 3     ; tile top row = 459 (tiles rows 459..478)
DOCK_TILE_W equ 24
DOCK_TILE_H equ 20
DOCK_X0     equ 8               ; first tile's left edge
DOCK_STEP   equ 28              ; tile + 4px gap; 8 + 12*28 = 344 < 640
```

Look: black `gfx_hline` across row DOCK_Y0, white fill rows DOCK_Y0+1..479
(an inverted menu bar). Tile i (= instance index i — **stable slot↔tile
mapping**, holes stay; quitting one instance never moves another's tile):
1px black frame DOCK_TILE_W × DOCK_TILE_H at x = DOCK_X0 + i·DOCK_STEP,
row DOCK_TY0; the instance's 16×16 icon body (`I_ICON`, via `icon_draw16`)
at (x+4, DOCK_TY0+2); I_ICON = 0 → the generic `ico_app16` **body** (the
library record's data at `ico_app16+2` — icon_draw16 takes a header-less
body, §25). Minimized (I_FLAGS bit0): `gfx_xor_fill` over the tile
interior (x+1..x+DOCK_TILE_W−2, DOCK_TY0+1..DOCK_TY0+DOCK_TILE_H−2).

The dock renders ONLY records read as I_STATE = 1 during the same lock
hold (§29.2 rule 3); dying records are skipped, so a closing instance's
tile vanishes with the `wm_hide` repaint. Icon pointers must satisfy
§29.1's lifetime rule — never a `disk_icons` index.

### Contracts

| symbol       | contract                                                    |
|--------------|--------------------------------------------------------------|
| `dock_init`  | reset module scratch. From kmain, right after desk_init.    |
| `dock_paint` | draw the rule, the strip and every live instance's tile. Called by wm_paint_all after `desk_paint`, before the menu bar and windows (lock held by caller) — windows cover the dock exactly like desktop icons (§26). |
| `dock_click` | in CX=x, DX=y (no lock held; called by ui.inc when wm_hit found no window, BEFORE desk_click). Out: CF=1 = consumed (any click with y ≥ DOCK_Y0 — strip background clicks are consumed no-ops), CF=0 = not in the dock. Tile hit on a live instance: minimized → gfx_lock, `inst_restore`, gfx_unlock; else → gfx_lock, `wm_front` on I_WIN, gfx_unlock. Single click activates; no double-click logic. |

Every dock-state transition (launch, quit, minimize, restore) rides a
`wm_show`/`wm_hide`/`wm_destroy` full repaint, so dock_paint needs no
partial-redraw path; if a future teardown path ever changes dock state
without a repainting wm_* call, it must add a desk_zone_redraw-style
partial redraw (overlap check against all windows, full wm_paint_all
fallback).

The window drag clamp (§13) is unchanged: windows may be dropped over the
dock; clicks in the overlap go to the window (wm_hit wins), and the strip
repaints when the window moves away — desk-icon semantics throughout.

## 31. ctrl.inc — the Control Panel window

Built-in singleton app kind (KIND_CTRL = 6, cap 1), window "Control Panel",
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
`[bb_on]`, which is **0 at boot** — double buffering is opt-in.

Caption: "Smoother; costs 150K" normally, or "Needs 500K of memory" when
`[bb_avail]` = 0. On such a machine the page is display-only: a click in
either band is ignored outright rather than moving a dot that `bb_set`
would refuse to honour.

`cp_disp_click` mirrors `cp_sched_click` — signed comparisons, x ignored,
a hit on the live row does nothing — and calls `bb_set` (§32), which
requires the gfx lock the click handler already holds. It then redraws just
the two glyphs. No `[cp_dirty]`: unlike the scheduler mode, no window quotes
this setting, and the switch is invisible except as speed.

### 31.4 Sound page (§34 — lands Phase 2; the AdLib route arms at Phase 3)

Third item in the panel list, heading "Sound"; like the Display page it is
one `cp_items` row plus a paint/click pair, and no pane machinery changes.
Note the far-code shape this relies on: the row's paint/click words are
**`.fartext` offsets dispatched through the two existing
`cp_paint`/`cp_onclick` FARSHIMs** — legal because the only code that
calls through `cp_items` is itself far (§33 rule 5) — so the page adds
**no new shims**. Contents, all read live at paint time (the page keeps no
state of its own, exactly as the Scheduler page reads `sched_mode_get`):

- A **tone route** radio pair, "Speaker" / "AdLib", following the live
  route (`snd_route` override, else the preference list, §34.2). The
  AdLib row obeys the `bb_avail` idiom (§32) three layers deep: with no
  OPL2 present the setter refuses, the caption says so ("AdLib: no"), and
  the click is ignored outright rather than moving a dot the router would
  refuse.
- An **exclusive clips** checkbox bound to `snd_excl_ok` (§34.4, default
  on): unchecked, `osapi_snd_play` returns err 3, and the caption states
  the trade honestly — an exclusive clip freezes the desktop while it
  plays.
- A **Test** button: synthesises the built-in test clip — the Recorder
  demo's 1 s sine sweep (§35): 8,000 samples at 8,000 Hz (N = 149, the
  default carrier), 400 Hz rising linearly to 800 Hz over the first half
  second and back down over the second, from the same 256-entry sine
  table (128 ± 88) and Bresenham-stepped phase increment (3277 → 6554;
  its own copy — kernel code cannot call into a package) —
  into a staging grant taken through the public verb-7 surface (§34.6:
  the Phase 4 allocator; stamped with the panel's own instance, freed on
  the way out; a refused grant plays nothing and the counters stay 0) and
  plays it through slot 0x0080's target (`osapi_snd_play`), ES pointing
  away from DS exactly as a package would. The click **blocks** for the
  clip — the sanctioned §34.4 `sch_lock` contract, disclosed by the
  caption — and the result codes are deliberately ignored: the counters
  row below reports what actually happened, which is the point of the
  button.
- Two caption rows, both read live at paint time. The **status row**:
  AdLib presence (`"AdLib: yes"` / `"AdLib: no"` — the caption layer of
  the `bb_avail` idiom) and, in a second column, the exclusive-clips
  trade stated honestly (`"Clips freeze GUI"`, or `"Clips: off"` while
  `snd_excl_ok` is clear). The **counters row** doubles as the Phase 2
  floor gate's instrument: it renders `E:`/`R:` followed by the
  `snd_pcm_emitted` / `snd_pcm_resync` counters (§34.4),
  screendump-readable, which is how the automated test asserts
  emitted ≈ clip length and resync ≈ 0.

No `[cp_dirty]`: nothing else on screen quotes a sound setting.

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
  `'Hardware clock: yes'` or `'Hardware clock: none'` read live from
  `[clk_rtc]` (§37) — the `bb_avail` caption idiom again, here purely
  informative: editing works either way.
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
  lock through a `FARSHIM` like the other two entry points. It returns
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

New `FARK` entries for this page: `inst_find_kind`, `clk_snap`,
`clk_fld_str`, `clk_fld_adj` (§33; `wm_content`, `wm_obscured`, `gfx_fill`,
`gfx_frame` and `font_str` already have wrappers).

## 32. vgabb.inc — optional double buffering

**Why it exists.** The original design drew straight into VRAM because
256KB of RAM leaves no room for a 640×480×4-plane shadow (150KB). Machines
with more memory can afford one, and get flicker-free updates: everything
drawn inside one gfx_lock/gfx_unlock burst appears on screen at once.
Module prefix `bb_`; file included right after `vga12.inc`.

**Probe — `bb_init`** (from kmain, right after `vga_mode12`; task 0, so the
§7 BIOS rule holds): int 12h → AX = conventional KB. If AX ≥ `DB_MIN_KB`
(500) it sets `[bb_avail]` = 1, and that is all it does — it neither arms
the buffer nor touches the planes.

**Double buffering is OFF at boot and switched at runtime.** `[bb_on]`
starts 0, so a fresh boot runs exactly the pre-§32 direct-VRAM code and
**nothing else in this section applies** until the user turns it on from
the Control Panel's Display page (§31.3), which calls `bb_set`. `[bb_avail]`
gates that: below the floor `[bb_on]` can never become 1, and the page says
why instead of offering a switch that would refuse. Both bytes are
initialized data (`db`, next to `gfx_lock_flag`), **not** .bss — nothing
zeroes .bss at boot.

**Switching — `bb_set`** (AL = 0 off / 1 on; caller HOLDS the gfx lock).
Turning ON calls `bb_sync` first: per plane, Graphics Controller Read Map
Select (GC4) = that plane, then a straight 0x9600-byte copy from `VGA_SEG`
to the plane segment at identical offsets, leaving GC4 back at its default
(§1 rule 7). Seeding is not optional — the buffer has never been written
while disabled, and after a disable it is arbitrarily stale, so the first
flush would otherwise push dead pixels over live ones. The cursor must be
hidden for it (it is — the lock is held), or it would be captured into the
buffer and smeared by that same flush. `bb_set` then resets the dirty rect
and publishes `[bb_on]` = 1 **last**, since every drawing entry dispatches
on it. Turning OFF calls `gfx_flush` first, so nothing drawn under the old
mode is stranded in RAM, then clears `[bb_on]`. Both directions no-op when
already in that state, so a repeated click cannot re-copy 150KB.

Note the EBDA caveat: a BIOS that steals
top-of-memory (SeaBIOS's 640K → 639) still passes, and so does a real 512K
machine once its BIOS has taken a cut — which is why the floor is 500 and not
512. The gate is deliberately the reported value.

**Back buffer layout.** Plane p lives at segment `BB_SEG + p*BB_PLANE_PARA`
(0x4000, 0x4960, 0x52C0, 0x5C20 — 0x960 paragraphs = 0x9600 bytes apart),
offsets 0..0x95FF, 80-byte rows — byte-for-byte the same geometry
as one VRAM plane, so `vga_rect_setup`'s offsets work unchanged in both
worlds. Linear span 0x40000..0x657FF: untouchable on a 256KB machine,
and 406KB of address space in all, so it clears the 500KB floor with room.

**Rendering.** RAM has no latches, no Set/Reset, no write modes — the
`bb_*` twins do in software, per plane, what the VGA ALU did in hardware:

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
  (§5) — `gfx_save_size` budgets are valid for both paths.
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

**Flush — `gfx_flush`.** Public, callable only with the gfx lock held
(cursor hidden). No-op when `[bb_on]` = 0 (single `cmp`/`je`) or the rect
is empty. Otherwise: for each plane, Sequencer Map Mask (SEQ2) = that
plane's bit, copy the dirty rows (`rep movsw` + odd-byte tail) from the
plane segment to `VGA_SEG` at the same offsets, then Map Mask back to 0Fh
and reset the dirty rect. GC state is untouched (defaults hold outside the
primitives). Interrupts stay enabled — the tick may switch tasks mid-flush,
but any drawer blocks on the gfx lock and the mouse ISR defers the cursor
while the lock is held, so nobody else touches VRAM or the Map Mask.

**The monochrome fast path — `[bb_mono]`.** The flush is the expensive half
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

**Transient overlays bypass the back buffer.** The drag outline and the two
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
memory (§28) whenever `[bb_on]` is set, so the RAM line and the System row
both move the moment the Display page switches it (39K ↔ 189K on a 639K
QEMU). The §16 test flow boots direct-to-screen like every machine; turning
the buffer on is a deliberate act, and `make xt` (256K) cannot do it at all.

## 33. farcall.inc — far code modules

Cold modules put their **code** in `section .fartext`, which is assembled at
`vstart=0`, shipped at the tail of the kernel image, and copied to
`FAR_SEG:0000` by `far_init` — kmain's first act (§15). That code costs the
kernel's 64KB window nothing at run time; only the shims stay behind.

**Why the blob is free.** `.bss` is declared `vfollows=.text` — *not*
following `.fartext` — so `.bss` deliberately overlaps the blob's landing
zone at `kernel_text_end`. The blob is copied out before anything writes
`.bss`, and `.bss` is uninitialised by definition, so the same addresses
serve both in turn. This is the same hazard `splash.inc` has always lived
with (§15: it keeps its state in `.text` because `.bss` is where the last
sector lands), and it is why `far_init` must run before `sched_init`.

**The contract.** All of it:

1. **DS stays `KERNEL_SEG`.** Far code addresses kernel variables exactly
   like near code, so every `[var]`, `[si+off]` and `lodsb` works unchanged.
2. **Therefore all data stays in `.text` or `.bss`.** Strings, tables,
   window templates, bitmaps — anything reached through DS — must not move.
   Only executable code moves. A module's data *may* hold pointers to its
   own far code (see rule 5).
3. The kernel calls in through `FARSHIM name, far_body`: a 6-byte near stub
   in `.text` that far-calls the body. Window templates, kind tables and
   `call [bx+W_PAINT]` keep naming the stub, so no dispatch site changes.
   A task entry must be a shim too — `task_spawn` builds a frame with
   CS = `KERNEL_SEG` (§8) and can only launch a near entry.
4. Far code calls back with `KCALL routine`, which far-calls the 4-byte
   `call`/`retf` wrapper emitted by `FARK routine`. Neither hop touches a
   register or a flag, so a routine that returns CF still does. Every
   `KCALL` target needs a `FARK` entry in the list at the bottom of
   `farcall.inc`. A tail `jmp` to something that never returns
   (`inst_task_die`) becomes `jmp far KERNEL_SEG:…` and needs no wrapper.
5. Calls between routines of the same far module stay near. An indirect
   near call through a table of `.fartext` labels is legal **only** from far
   code — a near pointer means nothing without knowing which CS will run it.
6. Far bodies reached by `FARSHIM` end in `retf`, not `ret`.

**What may not move:** the boot path (`splash.inc` runs before `far_init`),
any interrupt handler (vectors are seg:off into `KERNEL_SEG`), and anything
on a hot inner loop — each crossing is a far call, roughly 1.5× a near one.

**Resident so far:** `ctrl.inc` (§31) and `taskmgr.inc` (§28). Both keep
their data, their `.bss` and their two or three near shims in the kernel
segment; everything else is far. `tm_init` and `tm_kinit` also stay near —
they are self-contained and too small to be worth a shim.

**The sound layer (§34, landed through Phase 4):** its cold halves are far
— the device probes (the OPL2 timer dance, the SB reset scan and the SB
IRQ-discovery *orchestration*) behind the `SDRV_PROBE` FARSHIM stubs
(§34.2), the PWM xlat-table builder and the Sound page bodies (Phase 2),
OPL2 init + patch loader (Phase 3), SB detect + IRQ discovery (Phase 4;
shims `sbl_probe` / `sbl_irq_disc`, `FARK`s `sbl_dsp_wr` / `sbl_dsp_rd` /
`osapi_snd_stream`) — each phase adding its `FARSHIM`s and the `FARK`
entries its far bodies `KCALL` through. The hot/ISR halves are barred
from moving by the rule above and stay in `.text`: `snd_tick`, the tone
core, `spk_pcm_run`, `opl_wr`, `sbl_isr` **and the IRQ-discovery
candidate stubs** (interrupt handlers, even though discovery itself is
cold), the DSP/DMA primitives, the refill-task body and the staging
copies (§34.7).

**Accounting.** `KLOWFAR_KB` (§15.1) is what the kernel occupies outside its
own segment — `.lowbss` plus `.fartext` — and both the Task Manager's RAM
total and its System row add it, so the rows still sum to the total (§28).

## 34. snd.inc / sndfm.inc / sndsb.inc — the sound layer

Multiple outputs, honest routing, speaker first. The layer lands in five
phases (`docs/SOUND-PLAN.md` is the standing plan: tone core → speaker PCM
+ Control Panel page → OPL2 → Sound Blaster output → SB recording); every
contract below is binding from the phase marked on it, and the API surface
— all five §20.3 slots — ships whole in Phase 1, the unbuilt tiers as
clean error stubs. The lens throughout: every promise is backed by a
per-device budget on the floor machine (an IBM XT's 4.77 MHz 8088), and
where the floor cannot deliver, the API says so at call time (CF/AX error)
and the Control Panel says so in prose — the `bb_avail` idiom (§32), three
layers deep: the probe flag gates the setter AND the caption AND the
click. Cycle figures are 8086-nominal; a real 8088's 8-bit bus and
prefetch stalls inflate them 20–40%, so every margin claim here is a
*bound to be validated at its phase gate*, not an established fact.

Module split (§4): `snd.inc` (prefix `snd_`) owns the driver table, the
router, the tone tier, the speaker driver and the API slot targets;
`sndfm.inc` (`opl_`) the OPL2 driver; `sndsb.inc` (`sbl_`) the Sound
Blaster driver. `%include`d after `ctrl.inc`; every file ends on
`section .text` (§1 rule 4). Buffers live in `SND_SEG` (§2.2), never in
`.bss` or `.lowbss` — rejected, recorded: the kernel window is for code,
and `.lowbss`'s remaining 4,094 bytes are task-stack clearance, not a
buffer pool. `SND_SEG` costs the kernel nothing and exists on every
machine.

**No software mixing.** The router assigns one owner per (sink, tier);
simultaneity is different tiers on different sinks — FM music on OPL2, a
tone beep on the speaker and an SB stream all coexist, minus §34.3's PCM
exclusion. Mixing two PCM streams costs 20+ cycles/sample/stream — a
fast-machine luxury behind a future `bb_avail`-style gate at most, out of
scope for all five phases; the ownership contracts are shaped so a future
mixer changes no caller. And **PCM has two tiers with different
contracts**, and a request states which it needs: `PCM_BG` (background
block/IRQ semantics — SB only) and `PCM_EXCL` (exclusive clip: the
caller's task holds the CPU for the clip — speaker and the SB-direct/Covox
class). A `PCM_BG` request on a speaker-only machine **fails cleanly**
(AX = 4); it does not silently freeze the desktop — degradation is the
caller's explicit second call.

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
- **OPL2 at 388h/389h** (Phase 3): all access through `opl_wr` (`.text`,
  ≈40 bytes; in AH = register, AL = value): address select via 388h, 6
  counted status reads, data write to 389h — that much under one
  `pushf`/`cli` … `popf` — then 35 counted status reads at **the caller's
  IF** (the address register is stable, and same-tier interleaving is
  excluded by the router's ownership, not by cli). Real cost on the
  floor: ~430–1,300 cycles ≈ **90–280 µs per register write**, of which
  ≤ ~100 µs is `opl_wr`'s own IF=0 window when called from task context
  at IF=1 (the 52 µs "datasheet" figure is the chip minimum, not this
  implementation). A note-on is 2 writes ≈ 0.2–0.6 ms. Two callers run
  whole writes — delays included — at IF=0: `snd_tick`'s sanctioned
  key-off (§8.2), and §34.3's grant-atomicity window, which makes an
  OPL2-routed **tone grant** one IF=0 stretch of ~0.6–0.7 ms on the
  floor (two full writes plus the F-Number math) — the accepted cost of
  the binding single-window grant rule, still well under a tick, and the
  UART buffers one mouse byte across it.
- **Sound Blaster** (Phases 4–5): DSP base from the §34.2 reset scan; the
  discovered IRQ's vector hooked mouse_init-style (§34.5); **DMA channel 1
  only — never channel 2, the floppy's**.

### 34.2 Driver table, capabilities, ops

Capability bits, published in `snd_caps` (one byte per driver row plus a
merged word):

```nasm
SND_CAP_TONE     equ 01h   ; square voice(s): freq on/off
SND_CAP_FM       equ 02h   ; 9-ch 2-op patch + note-on/off (OPL2 model)
SND_CAP_PCM_BG   equ 04h   ; self-clocked block PCM out (DMA+IRQ) - background-safe
SND_CAP_PCM_EXCL equ 08h   ; CPU-paced PCM out - exclusive clip contract
SND_CAP_PCM_IN   equ 10h   ; block PCM input (recording)
```

Wire format for all PCM, pinned: **8-bit unsigned mono** — native on the
SB/Covox class, cheap per-rate rescale for speaker PWM (once per clip via
the §34.4 xlat table, never a per-sample multiply).

Driver table `snd_drv` (`.text`, stride `SDRV_SIZE` = 16 — the
`inst_kinds` idiom):

```nasm
SDRV_CAPS   equ 0   ; db  capability bits the hardware class supports
SDRV_FLAGS  equ 1   ; db  bit0 = present (probe result; published LAST -
                    ;     the §29.2 publish-last idiom)
SDRV_TONE   equ 2   ; dw  near ptr: tone op
SDRV_NOTE   equ 4   ; dw  near ptr: FM op        (0 if unsupported)
SDRV_PCM    equ 6   ; dw  near ptr: PCM-out op   (0 if unsupported)
SDRV_PCMIN  equ 8   ; dw  near ptr: PCM-in op    (0 if unsupported)
SDRV_PROBE  equ 10  ; dw  near ptr to the probe's FARSHIM stub in .text
                    ;     (0 = unconditionally present) - §33 rule 3: near
                    ;     pointers reach far code only through shims;
                    ;     KCALL/FARK is the reverse direction (probe bodies
                    ;     calling kernel helpers)
SDRV_NAME   equ 12  ; dw  .text string ptr ('Speaker','AdLib','Sound Blaster')
            equ 14  ; dw  reserved (SB: packed base/IRQ/DMA config word)
```

Rows, indices named `DRV_SPK`/`DRV_OPL`/`DRV_SB`: 0 = speaker (present = 1
as initialised data — always there), 1 = OPL2, 2 = SB. A future Covox row
is a fourth entry with `SDRV_PROBE` = 0 and presence set from a Control
Panel checkbox — announced, not detected (it is undetectable resistors).
The op pointers (`SDRV_TONE`..`SDRV_PCMIN`) are plain `.text` routines,
never far.

**Route selection is data, not code**: per-tier preference lists in
`.text` (`snd_pref_tone db DRV_OPL, DRV_SPK, 0FFh`;
`snd_pref_pcm db DRV_SB, DRV_SPK, 0FFh`; …) — the first *present* driver
wins. One override byte per tier (`snd_route`, initialised `.text`,
default 0FFh = auto) lets the Control Panel pin a tier to a specific sink,
checked before the list; a route change to an absent driver is refused at
three layers (setter, caption, click — §31.4). Adding an SN76489 or Covox
row later is a table row plus a list entry — zero router code changes.

**Ops register contracts.** Every op preserves all registers except its
documented outputs; callable from task context only unless stated:

- **Tone op** — AX = frequency in Hz (19..20000), or 0 = off. DL = voice
  (0 for the speaker; 0–2 reserved for a future SN76489 row). Out: CF=0
  ok, CF=1 unsupported voice/freq. Speaker body: divisor = 1193182/AX via
  `div`; `pushf`/`cli` … `popf` around the 43h/42h/42h/61h-RMW quad (mode
  3, 0B6h → 43h, divisor lo/hi → 42h, 61h |= 03h); off = 61h &= 0FCh.
- **FM op** — AL = verb: 0 note-on (CL = channel 0–8, BX = freq Hz — the
  F-Number/block math is done inside: F-Number = Hz·2^(20−block)/49716,
  block chosen so F fits 10 bits), 1 note-off (CL), 2 patch-load (CL,
  DS:SI → 11-byte patch: 5 operator regs × 2 operators + C0h), 3 all-off.
  Out: CF=1 no FM sink or refused — bad verb/channel/frequency (the OPL2
  voice tops out at 6,208 Hz, block 7's 10-bit F-Number ceiling), or a
  reserved / another requester's channel (§34.3); CF=0 done. Note-on and
  patch-load claim the channel on first touch; note-off leaves the claim
  standing (a melody keeps its channels between notes); all-off keys off
  and releases everything the requester holds. A note-on refused for its
  frequency **rolls back a claim it just created** — a refusal leaves no
  side effect, so a requester probing frequencies cannot accumulate
  silent claimed channels — but never releases a pre-existing claim,
  including patch-load's. FM is fire-and-forget — a
  note costs ~0.2–0.6 ms once, then zero CPU while it sounds: background
  music under full multitasking even on the floor, which is why the
  preference list routes tones to OPL2 when present.
- **PCM-out op** — AL = verb: 0 start (ES:SI buf, CX len, DX rate Hz),
  1 stop, 2 feed/status. Exclusive drivers (speaker) implement verb 0 as
  **run-to-completion** — it returns when the clip ends or aborts;
  background drivers (SB) return immediately and interrupt per block, and
  their verb-0 data source is the kernel double buffer only (the refill
  task feeds it — callers never hand SB a raw pointer). This asymmetry is
  deliberately **not** papered over: it is the hardware truth, and the
  public API exposes it as two different calls (§20.3).
- **PCM-in op** — SB only; verbs mirror PCM-out into the record ring
  (§34.6); half-duplex with PCM-out enforced by the router, not the
  driver.
- **Probe** (far body behind the `.text` FARSHIM; run cold from
  `snd_init`): out AL = 1 present (+config in the reserved word), 0
  absent. Order and recipes, pinned: speaker (none needed) → **OPL2
  timer-flag dance** at 388h (~200 µs: mask/reset via 04h ← 60h/80h, read
  status s1; timer-1 FFh + start 21h; wait ≥ 80 µs by counted status
  reads; read s2; present iff (s1 & E0h) == 00h and (s2 & E0h) == C0h;
  clean up 04h ← 60h/80h) → **SB reset scan** over bases {220h, 240h,
  210h, 230h, 250h, 260h} with a **10 ms poll timeout per base** (an
  absent bus floats FFh; the ~100 ms allowance is only for a
  present-but-slow DSP, retried once on 220h): write 1 → 2x6h, hold
  ≥ 3 µs, write 0, poll 2xEh bit 7, read 2xAh == 0AAh → SB version via E1h
  (the auto-init strategy gate at ≥ 2.00, §34.5) → **SB IRQ discovery
  deferred to first use** (§34.5). Presence flags are published **last**,
  after each device is fully configured.

### 34.3 Router — ownership, priority, generations

- **Tone tier**: one logical channel, single owner. Owner record =
  {instance byte, priority byte, generation byte, expiry ticks}. Steal
  policy: a new request with priority ≥ the current owner's takes the
  channel (kernel UI beeps use priority 0C0h; the package default is
  040h); lower priority is refused CF=1. Tone-off (AX = 0) obeys the same
  compare, so background music cannot silence an alert. Duration-limited
  tones (CX ≠ 0) self-expire via `snd_tick` — no task needed — and the
  expiry is **generation-guarded**: the tick silences only if the owner
  generation still matches the one stamped at grant. Route: speaker by
  default; the Control Panel can route tones to OPL2 (a square-ish patch
  on channel 8) to free the speaker — and **whenever OPL2 is in the tone
  route or preference list, channel 8 is reserved out of the FM
  allocator's bitmap**, so a package grabbing all FM channels cannot
  steal the tone channel.
- **Grant atomicity (binding)**: every grant, steal and release updates
  its owner record (generation, priority, expiry) *and* its ports inside a
  **single** `pushf`/`cli` … `popf` window. `snd_tick` runs at IF=0
  between any two task-context instructions; without this rule a tick
  landing between the generation stamp and the expiry store sees
  new-generation-with-stale-expiry and can silence a just-granted tone.
  The generation guard is only sound because task-side writers are atomic
  w.r.t. the tick. The same rule covers the PWM steal path: `snd_ch2mode`,
  the generation stamp and the silencing are one unit. And `snd_ch2mode`
  = 2 is stamped **only when the resolved PCM route is the speaker**: a
  clip granted to a non-speaker sink (SB direct, Covox — Phase 4+) must
  leave ch2 untouched, because only the speaker op's idle path ever
  clears mode 2 — an unconditional stamp would wedge tone refusal
  forever after the first non-speaker clip.
- **FM tier**: 9 channels (8 while the tone reservation is active),
  claimed per requester on first touch of a **caller-named** channel
  (bitmap + owner stamps — there is no allocator picking channels);
  channel handles are the raw 0–8 index; the bitmap is force-released by
  `snd_release_inst`.
- **PCM_EXCL**: a whole-machine resource, one clip at a time, no queue — a
  second caller gets AX = 1 busy. **Refused (AX = 1) while any PCM_BG
  stream is open**: an exclusive clip raises `sch_lock` for its whole
  duration (§34.4), which would freeze the SB refill task and force the
  stream through its underrun path — the router refuses the combination
  instead of shipping the surprise.
- **PCM_BG / PCM_IN**: per-card owner record {instance, generation};
  input and output are **mutually exclusive on one SB** (same DSP + DMA
  channel) — the router models half-duplex, the driver never sees the
  conflict. Stream handles carry the generation byte: feed/close/status on
  a stale handle return "stale" instead of acting on a reused stream.
- **`snd_release_inst`** — in: AL = instance slot; Phase 1. Every grant —
  tone ownership, FM channel bits, stream ownership, staging grants — is
  stamped with its owner instance, and this routine force-releases all of
  them, closing a live stream and ending its refill task. Called from both
  §29 teardown paths (§29.4); a closed package can never leave a tone
  droning or a dangling SB stream.
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

### 34.5 Sound Blaster (Phases 4–5)

- **Init** follows `mouse_init`'s order verbatim: hook the discovered
  IRQ's vector (save old, install under `pushf`/`cli`) while masked at the
  8259 → program/verify the DSP → unmask. The hook is made once, at the
  end of the first successful discovery, and **stays installed until
  `snd_unhook`** — between streams `snd_sb_expect` is clear and strays
  ride the ISR's spurious path. The mirror-image unhook joins
  `snd_unhook` (§34.7).
- **IRQ discovery, deferred to first use**: hook candidates {7, 5, 3, 2}
  onto `.text` discovery stubs (~30 bytes each: record which vector fired,
  confirm, EOI, iret — interrupt handlers, so §33 bars them from far even
  though discovery itself is cold), issue DSP command F2h
  (trigger-8-bit-IRQ, supported by all DSPs), and keep the vector that
  fired **only after the stub's 2xEh bit-7 read confirms the DSP asserted
  it** (defeats a coincidental spurious IRQ 7 during the window; retry F2h
  once if the confirm fails); unhook the losers.
- **`sbl_isr`** (`.text`, mou_isr discipline: push used regs + DS + ES,
  DS = KERNEL_SEG, `cld`, IF=0 throughout, no BIOS, never `sti`, own EOI
  `out 20h, 20h` — the BIOS does not handle this IRQ): first the
  **spurious-IRQ-7 guard** — treat the interrupt as SB only if
  `snd_sb_expect` is set AND 2xEh bit 7 confirms. An unexpected interrupt
  on a non-7 line **is always EOId**: a real unacknowledged ISR bit would
  block its whole priority level forever. An unexpected interrupt on
  **line 7 asks the 8259** (OCW3 0Bh → the next read of 20h returns the
  in-service register): ISR bit 7 **clear** is the 8259's spurious-IRQ-7
  artifact — no ISR bit was set, so no EOI, just iret; ISR bit 7 **set**
  is a REAL IRQ 7 — the SB Pro's factory default line — and IS EOId.
  (The naive "line 7 ⇒ no EOI" rule would wedge an IRQ-7 card: a block
  IRQ latched at the PIC just before a close is delivered after the close
  clears the expectation and retires the SB-side status with its own 2xEh
  read, so it lands on this path with bit 7 in service — and no
  non-specific EOI from the timer/keyboard/mouse handlers can ever retire
  bit 7, the lowest priority.) The IRQ-discovery stubs apply the same
  split on their confirm-failed path — during the ~330 ms force-unmask
  window a stray from an unrelated device on 2/3/5 is real and gets its
  EOI. The confirm read doubles as the ACK;
  single-cycle mode (DSP < 2.00): re-program DMA ch1 + command 14h for
  the other half of the double buffer (~40 instructions — the
  audible-gap mitigation on 1.x); auto-init (DSP ≥ 2.00, chosen at
  detect): nothing to re-arm; flag the consumed half for the refill task;
  EOI. Never touches VGA, gfx_lock or the switch path.
- **DMA recipe (binding)**: channel 1 only. Mask 0Ah = 05h; for each
  16-bit value **clear the flip-flop via 0Ch and write two successive
  bytes (lo, hi)**: offset → 02h/02h, (len − 1) → 03h/03h; page 3 → 83h;
  mode 49h out / 45h in (59h/55h auto-init); unmask 0Ah = 01h. Nothing is
  multiplied by 2 — address doubling is a 16-bit-channel concept that does
  not exist on the XT. The double buffer is 2 × 2 KB at `SND_SEG`:0
  (§2.2) — wholly inside 8237 physical page 3, page-crossing-safe by
  construction. Rates are quantised via TC = 256 − 1e6/rate; ceilings
  honoured (out ≤ ~22 kHz; in ≤ 13 kHz on 1.x / 15 kHz on 2.0 normal
  mode). **Every DSP poll (2xCh busy, 2xEh ready) carries a timeout.**
- **The staged rule (binding), and the floppy truth behind it**: the DMA
  and `sbl_isr` keep running during int 13h windows (`sch_lock` does not
  mask interrupts), but the refill task does not — task switching pauses
  (§18), so after the double buffer drains (~512 ms at 8 kHz) a stream fed
  live from disk *will* underrun. Therefore **streams must be fully staged
  into their `SND_SEG` grant before playback starts**: open-out checks CX
  covers the clip, or the caller accepts progressive-feed risk explicitly
  (§20.3 verb 0).
- **Underrun contract**: when `sbl_isr` finds the next half not refilled,
  it pauses output (D0h, bounded write-poll; on a single-cycle DSP there
  is simply no re-arm), bumps `snd_sb_under` and marks the stream
  underrun-paused (visible via the status verb); the refill task resumes
  (D4h on auto-init, a fresh half arm on single-cycle) after catching up,
  or the owner closes. Bounded silence + a visible status — never stale
  audio looping, never a wedge. **Data exhaustion is the same path,
  deliberately**: a stream that has consumed every staged byte reads
  underrun-paused with the consumed count at its valid length — resumable
  by a feed, closable by its owner — because with no clip-length
  parameter in the ABI the kernel cannot distinguish "finished" from
  "starved", and refusing to guess is the honest contract (§20.3 verb 3
  pins the states). A short final half is padded with 80h silence by the
  refill copy, so the pause always lands on a block edge. `snd_tick`'s
  **stream watchdog** covers the complementary failure: a block IRQ that
  fails to arrive within ~2× the block period (while one is expected)
  halts the stream and marks it **ended** instead of hanging the owner.
- **The refill task — the kernel owns stream pacing.** Packages run only
  inside window callbacks and cannot own tasks, so open-out/open-in spawn
  a **transient kernel task** from the existing 12-slot pool (the
  Clock/Bounce spawn idiom; a full pool is a clean err 6). It copies
  grant → double-buffer halves as `sbl_isr` flags them consumed (or
  ring → grant for recording) and exits at stream end, close or teardown.
  SB playback DMA never runs from a grant — the copy hop satisfies the DMA
  contract by construction, not by caller discipline. Rejected, recorded:
  a *resident* sound task would cost a 1,536-byte `.lowbss` stack for
  mostly-idle work; tone expiry is a `snd_tick` leaf and stream refills
  are transient spawns that exit with their stream.
- **The DSP < 2.00 fallback, pinned now**: QEMU's sb16 cannot exercise
  single-cycle DSPs at all; that path is verified on 86Box (`vm/xtsb`, an
  XT with an SB 1.5/2.0). If that config proves unmaintainable, the
  version gate **refuses** DSP < 2.00 (the caps bit stays clear) rather
  than shipping a permanently unverified branch. One known bound of the
  branch, recorded: the single-cycle re-arm programs the 8237 from the
  ISR, and the 8237's byte-pair flip-flop is shared with the BIOS's
  channel-2 programming inside int 13h — a floppy read concurrent with a
  single-cycle stream can interleave the pairs. Auto-init hardware never
  programs DMA from the ISR and has no such window; the 86Box gate
  decides whether the 1.x branch ships or the gate refuses it.

### 34.6 Recording and staging

- **Recording** (Phase 5): STREAM verbs 4–5 (§20.3) → DSP 24h single-cycle
  or 2Ch auto-init (chosen by the same §34.5 version gate as playback;
  DMA modes 45h/55h — write transfer, everything else per the §34.5
  recipe) into the 8 KB `SND_SEG` record ring (§2.2), 2 × 4 KB halves; the
  input rate ceiling is honoured at open (13 kHz single-cycle / 15 kHz
  auto-init normal mode — err 2, never a clamp). Capture starts with the
  speaker (DAC) off — a live DAC feeds output back into the capture path
  on real hardware. **Half-duplex with playback is enforced by the
  router's owner record** (§34.3): one stream record with a direction
  byte, so open-in while an out-stream is open — and the reverse — is
  err 1 busy by construction, and the driver never sees the conflict.
- **The drain task — the refill task's input mirror** (§34.5's execution
  model, verbatim): verb 4 spawns a transient kernel task that moves
  captured ring halves into the owner's grant, oldest first, until the
  stated capacity fills — then it stops the DSP (halt + exit auto-init)
  and the stream reads **paused (1) with the captured count at its
  capacity**, the input mirror of data exhaustion: closable by its owner,
  not resumable (feed on an input stream is err 7). The ISR's input leg
  flags each captured half and, if the half it must capture into next is
  still un-drained (the drain starved), pauses input — bounded loss and a
  visible status, never a silent overwrite; the drain task resumes
  (D4h / a fresh 24h arm) after emptying it, under the same IF=0
  re-verification window as the refill task's resume. The `snd_tick`
  watchdog covers input identically: block IRQs that stop arriving within
  ~2× the ring-half period halt the stream as **ended (2)**. Consumers
  read captured bytes via the verb-5 staging copy — no ES pointer ever
  crosses (§2.2).
- **The drain copy is teardown-fenced (binding)** — the input/output
  asymmetry, recorded: the refill copy only *reads* its grant, so a
  teardown that frees the grant mid-copy costs at worst one stale-data
  audio glitch (bounded, benign); the drain copy *writes* grant bytes,
  and force-teardown (`snd_release_inst` on the UI task) frees — and a
  relaunch can re-grant — those pool bytes the instant it stops the
  stream, so a drain task preempted inside a whole-half `rep movsb`
  (~21 ms on an 8088) would resume writing ring data into another
  instance's memory. The ring → grant copy therefore runs in **512-byte
  chunks, each inside one `pushf`/`cli` window that re-verifies act +
  generation before writing** (the refill-resume rule applied to the
  copy itself): every act-clear and grant-free runs in task context and
  IF=0 excludes the switch, so a chunk that starts writing holds the
  grant for its whole write — a torn-down stream stops at the chunk edge
  with nothing written after the free. `sbl_consumed` advances inside
  the window *after* each chunk lands: verb 3 reports it as *bytes
  captured into the grant* (§20.3), so the poll-then-read pattern
  (status, then read) never returns bytes the copy has not written yet.
- **The staging pool** (`SND_SEG` 0x3000..0xFFFF, ~52 KB, §2.2) has a tiny
  allocator: first-fit grants stamped with the owning instance slot — the
  §21 package-pool idiom: occupancy derived from the grant records, no
  free list — allocated and freed through verb 7 and force-released by
  `snd_release_inst` at teardown. **Packages never hold an ES pointer into
  `SND_SEG`**: data goes in via the stage verb (kernel copies
  caller → grant) and comes out via the read verb (kernel copies
  grant → caller) — the `dsk_get_dir` staging idiom (§18), in both
  directions.
- What packages get: a Minesweeper explosion is one
  `call OSAPI_SND_TONE` (reloc class 1 handles it, §20.2); a music player
  plays FM notes via 0x0084, or staged PCM via 0x0088 on an SB machine —
  and on a speaker-only machine it *asks* `osapi_snd_caps` first and
  offers the user the exclusive-clip trade-off instead of pretending. A
  file-read-into-grant OSAPI call — the missing piece for playing clips
  from disk that a package didn't ship with — is future work, noted here
  and not promised: today a package stages data it generated or carries in
  its image, bounded by its ~19 KB pool budget.

### 34.7 State, boot gate, teardown

- **The `snd_live` boot-gate rule (binding).** `sched_init` hooks int 08h
  as kmain's second act; `snd_init` runs seconds of ticks later, and
  nothing clears `.bss` at boot (§8). So `snd_tick`'s first instruction
  tests `snd_live` — a `db 0` in initialised `.text` data (the
  `osapi_seed` idiom of §15), set to 1 as `snd_init`'s **last** act
  (publish-last) — and returns while it is clear. **Every byte `snd_tick`
  can read must have a defined value from the instant int 08h is hooked**:
  either initialised `.text` data, or behind the `snd_live` gate.
- **Initialised `.text` data**: `snd_live`, `snd_route` (0FFh = auto), the
  preference lists, `snd_excl_ok` (default 1), `snd_drv` (row 0's present
  bit = 1 as data), the driver name strings, the OPL2 patch bytes and the
  Sound-page state.
- **`.bss`** (~100 bytes of state + the 256-byte xlat table): owner
  records, generations, deadlines, `snd_ch2mode`, the saved 61h boot
  bits, stream bookkeeping, `snd_btn0`/`snd_abort`, `snd_sb_expect`, and
  the debug counters `snd_pcm_emitted` / `snd_pcm_resync` /
  `snd_sb_under`. All stored explicitly by `snd_init` (§8's rule) and
  unreadable by the tick until `snd_live` publishes.
- **Buffers**: all in `SND_SEG` (§2.2); the layer claims the segment at
  Phase 2 and the Task Manager RAM figure carries it via the `KLOWFAR_KB`
  idiom (§15.1/§28).
- **Boot**: `snd_init` joins kmain's §15 sequence **after `tm_init`** (it
  needs `far_init` for its far probes and `sched_init` for tick-based
  timeouts — both long since run): save the 61h boot bits, run the probes
  cold, publish presence flags, set `snd_live` last. Boot stays clean —
  no instances, no tasks, no sound; a machine with no cards boots and runs
  byte-identically to a soundless kernel until the first sound call.
- **Teardown**: `sched_unhook` gains a `call snd_unhook` beside
  `mouse_unhook` (§8) — silence 61h bits 0–1 back to the saved boot state,
  leave ch2 quiescent, and (if hooked) mask the SB IRQ, reset the DSP and
  restore its vector.
- **Sections** (§33): ISRs and their whole call path stay in `.text` — the
  driver table, caps/route bytes, owner records, the tone core,
  `snd_beep`, `snd_tick`, the router, all five API slot targets,
  `snd_release_inst`, `snd_unhook`, `spk_pcm_run`, `opl_wr`, `sbl_isr` +
  the IRQ-discovery candidate stubs, the DSP/DMA primitives, the
  refill- and drain-task bodies and the staging copies. Far, behind
  `FARSHIM`/`KCALL`: the probes, OPL2 init (~245 writes ≈ 25–70 ms — fine
  cold at boot, never from ISR context) and the patch loader, the Sound
  page bodies, and the PWM xlat-table builder. Far code keeps
  DS = KERNEL_SEG, so all of it reads its data from `.text` (§33 rule 2).

## 35. Recorder — the fourth package (apps/recorder/recorder.asm)

A sound wave recorder and player over the §34 layer, and the first
*shipped* package to touch it (fmtest/sbtest are gate scaffolding, never
on the apps disks). Prefix `rc_`, embedded microphone icon (flags bit 0).
Directory order on the apps disks stays pinned: mines, hello, notepad —
**recorder is appended fourth** so the earlier indices hold.

- Window: "Recorder", 220×140 at (210,150) → content 218×121. Content
  layout, top to bottom: a **waveform strip** (1px frame (4,2)–(213,69),
  white field, light-gray centerline at row 36), two status lines at
  y 76 / 88 (the state message; `NNNNN BYTES  IN:SB|--`), and four
  52×16 buttons at y 100: **REC / STOP / PLAY / DEMO** at x 4/58/112/166.
  Disabled buttons draw dark gray; enabled black.
- One grant, one rate: on first need (REC or DEMO) the app verb-7 allocs
  a single **40,000-byte** `SND_SEG` grant (5 s at the app's one rate,
  **8,000 Hz** — PWM N = 149, inside 74..255) and keeps it for the
  instance's life; `rc_len` is the valid take inside it. All data moves
  through the staging verbs (6 in, 5 out) — the app never holds an ES
  pointer into `SND_SEG` (§34.6).
- **Caps decide, three layers deep** (the `bb_avail` idiom, §32): caps are
  read once at entry. No `SND_CAP_PCM_IN` → REC draws disabled, a click on
  it only writes "NO REC DEVICE (SB NEEDED)", and the byte line reads
  `IN:--` — the app is a full player on speaker-only machines. PLAY
  prefers `PCM_BG` (verb 0 on the already-staged grant — background, the
  GUI keeps running) and falls back to `PCM_EXCL`
  (`osapi_snd_play`, ES = DS, 4,000-byte verb-5 read-back chunks = 0.5 s,
  blocking, click-aborts mid-chunk per §34.4). §34.4's baseline latch
  only covers presses *inside* a chunk, so between chunks the app samples
  `osapi_mouse` against its own release-folded baseline (latched before
  the first chunk, exactly the kernel's fold) and a button newly down in
  the inter-chunk gap aborts before the next chunk is issued — no press
  is ever swallowed by a chunk boundary. One asymmetry, accepted: a
  gap-abort press is not drained (only the kernel can, §34.4), so it also
  lands as an ordinary click. The status line names the device that
  played ("PLAYING (SB)" / "PLAY DONE (SPEAKER)" / …).
- **Progress is polled** (§34.3 — there are no sound events): every
  W_PAINT and W_ONCLICK first runs `rc_poll`, which reads verb 3 and
  retires state changes. **State 1 is discriminated by its consumed
  count** — §20.3 pins one state for both the terminal and the transient
  pause, and only the owner knows the length: recording, capacity-full
  pause (consumed = capacity) → close + "REC STOPPED (FULL)", while a
  drain-starve pause (consumed < capacity) just refreshes the live count
  and leaves the stream for the kernel's resume; playback,
  data-exhaustion pause (consumed = length) → close + "PLAY DONE (SB)",
  while a refill-starve pause (consumed < length — e.g. a floppy read
  paused the refill task, §18) is left open for the auto-resume
  (§34.5/§34.6). Watchdog "ended" → "REC/PLAY STOPPED (WATCHDOG)";
  stale → idle. `rc_poll` preserves all registers — the onclick hit-tests
  CX/DX *after* polling, and verb 3 returns in AX/DX (a leak here cost a
  debug cycle; recorded so it stays fixed). STOP reads the final captured
  count (verb 3) before closing, so a stopped take keeps its bytes.
- **DEMO** stages a built-in 1 s sine sweep as if it had been recorded:
  400 Hz rising linearly to 800 Hz over the first half second and back
  down over the second, at 8 kHz. A 256-entry sine table (128 ± 88 —
  the old square's 0xD8/0x28 span) indexed by the top byte of a 16-bit
  phase accumulator; the per-sample increment runs 3277 (400 Hz) to
  6554 (800 Hz), Bresenham-stepped ±1 so each 4,000-sample half sweeps
  smoothly. 8,000 bytes synthesised in 1,000-byte chunks (the turnaround
  lands on a chunk boundary) and verb-6 staged. It exists so the real
  playback paths are exercisable anywhere — a sweep has no single line,
  so any sndcheck dominant assertion on it must accept the whole
  400–800 Hz band.
- The **waveform strip** decimates the take to its 208 columns (offset =
  column × `rc_len` / 208, one sample per column via verb 5) and draws a
  vertical line from the centerline (positive samples up, span/4 scale:
  +31/−32 px inside the 66px field — two `sar`s, so the negative side
  keeps the extra step). Empty take = centerline only.
- Teardown needs nothing app-side: close-box mid-record/mid-play rides
  `snd_release_inst` (§34.3), which closes the stream and frees the grant;
  a relaunch re-grants cleanly. Task Manager: one ordinary task-less
  instance row (§28), nothing new.
- QEMU truth (§34.5/§34.6, and the test plan's pinned limits): sb16 never
  delivers input IRQs, so REC always lands on the watchdog path — "REC
  STOPPED (WATCHDOG)" with 0 bytes IS the deterministic automated test;
  live capture is 86Box/real-hardware work. Corollary: the input watchdog
  (~2× the block period; ~21 ticks ≈ 1.15 s at 8 kHz) retires the
  recording that fast on QEMU, and `rc_poll` runs before the STOP
  hit-test — a STOP click landing later reads "NOT RUNNING", not
  "STOPPED". Real hardware's input IRQs rewind the watchdog, so this is
  a QEMU-testing note, not a behavior bug. And pcspk emits zero frames in
  ch2 mode 0, so the speaker fallback is asserted via its result codes and
  the §31.4 E:/R: counters, never the wav.

## 36. Piano — the fifth package (apps/piano/piano.asm)

A colorful playable piano over the §34 tone tier: 1.5+ octaves of clickable
keys, a live note viewer (a scrolling mini-staff), a recorded sequence with
timed replay, and three embedded public-domain songs. Prefix `pn_`,
embedded piano-keys icon (flags bit 0). Directory order on the apps disks
stays pinned: mines, hello, notepad, recorder — **piano is appended
fifth** so the earlier indices hold. Drawing uses colors beyond 0/15,
which retires `[bb_mono]` (§32) — supported and expected.

- Window: "Piano", 224×177 at (250,100) → content 222×158. Content
  layout, top to bottom (all coordinates content-relative):
  - **Note viewer** — a CBLUE 1px frame (2,2)–(219,49), white field, five
    black staff lines (treble: E4 G4 B4 D5 F5) at y 40/34/28/22/16,
    x 8..213. Up to **21 note columns**, 10 px apart at x = 8 + col·10;
    each played note is a 4×3 CBLUE dot centered at y = 46 − 3·step
    (step = diatonic degree from C4 = 0 … G5 = 11), with a black ledger
    line (colx+2..colx+11 at y 46) for middle C and a small CLRED
    hand-drawn sharp glyph (two 6px vlines at colx+1/colx+3, two 5px
    hlines at y−2/y+1) beside the dot for the five sharps. The viewer
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
  can, §34.4), so it also lands as an ordinary click — the recorder's
  documented gap-abort asymmetry, which is what makes close-box- and
  key-click-during-replay work naturally. Durations are the timestamp
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
the UI task's inner loop and, through `FARK` wrappers, from the Control
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
Panel setting (the scheduler mode, double buffering, the tone route): there
is nowhere to persist them — os88fs is read-only (§19) — so both return to
their defaults at boot. They change **display only**; the clock itself is
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
(§29.4), so a reader can be pre-empted mid-format while the UI task carries
a second. Two rules make that safe, and both are binding:

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
probe failure leaves a sane clock, then calls `clk_rtc_read` and sets
`[clk_rtc]` only if it succeeds. The probe is deliberately paranoid,
because the machines this OS targets are exactly the ones that may have no
CMOS clock at all and whose BIOS may simply `iret` out of an unknown
`int 1Ah` function, leaving CF and the registers as it found them:

1. CX and DX are poisoned to 0FFFFh and CF is **set** before each call, so
   an unimplemented function is detected by the poison surviving.
2. `AH=02h` (time: CH/CL/DH = hour/min/sec BCD) then `AH=04h` (date:
   CH/CL = century/year BCD, DH/DL = month/day) — CF = 1 from either
   (documented as "clock not operating") fails the probe.
3. Every byte must be valid packed BCD (`clk_bcd` returns CF = 1 when
   either nibble exceeds 9) and in range: hour ≤ 23, min/sec ≤ 59, month
   1..12, day 1..31, century 19 or 20.
4. Only after all of that are the values committed, out of a scratch
   buffer, as one set — a probe that fails halfway can never leave a
   half-RTC, half-fallback clock.

`clk_rtc_write` is the inverse (`AH=03h` + `AH=05h`, values re-encoded with
`clk_tobcd`) and returns immediately when `[clk_rtc]` is 0. It calls BIOS,
so it runs **only** from ui_task step 3, outside the gfx lock, draining
`[clk_dirty]` (§13) — never from a window callback (§31.1).

| symbol | contract |
|--------|-----------|
| `clk_init` | Boot: display settings to their defaults (24-hour, no seconds), fallback date, then the RTC probe. Preserves all registers. |
| `clk_tick` | UI task only. Advances the clock from the `[ticks]` delta. Out: AL = the change mask above. Clobbers AX only. |
| `clk_snap` | Copies the six fields to `clk_sn_*` under `pushf`/`cli`. Preserves all registers. `FARK`ed for §31.5. |
| `clk_fmt` | Calls `clk_snap`, then formats the bar's line into `clk_str` in the live form: `'Mmm DD YYYY  HH:MM'`, plus `':SS'` if `[clk_secs]`, and in 12-hour mode the hour drawn 1..12 **without a leading zero** and a trailing `' AM'`/`' PM'`. 18..24 glyphs; `clk_str` is 26 bytes. Out: SI = `clk_str`. Preserves everything else. |
| `clk_fld_str` | In: AL = field 0..6 (month, day, year, hour, minute, second, meridiem). Out: SI = a NUL string for that field alone in `clk_fbuf` — `'Mmm'`, `'DD'`, `'YYYY'`, `'HH'`, `'MM'`, `'SS'`, `'AM'`/`'PM'`. Always the field's **full width, zero-padded** — unlike `clk_fmt`, because a field is a fixed-width editable cell whose highlight box must not change size under it; in 12-hour mode the hour reads `'12'`, `'01'`..`'11'`. Reads the last `clk_snap` and does **not** take one. Preserves everything else. `FARK`ed for §31.5. |
| `clk_fld_adj` | In: AL = field 0..6, BL = +1 or −1. Steps that field with wrap (month 1..12, day 1..month length, year 1980..2099, hour 0..23, min/sec 0..59); field 6 flips the meridiem by ±12 hours, either sign. Then re-clamps the day to the new month length (31 Mar − 1 month = 28 Feb, never 31 Feb), zeroes `clk_acc` and re-samples `clk_last` so the new second starts from now, and sets `[clk_dirty]` + `[clk_barq]`. Preserves all registers. `FARK`ed for §31.5. |

**The hour is always stored 0..23** and stepped 0..23 — 12-hour mode is a
rendering of it, not a second representation. So `+` on an hour showing
`11 AM` gives `12 PM`, which is what a clock does; and the meridiem field
is the only thing that jumps by 12.
| `clk_rtc_write` | Pushes the live time back to the hardware RTC if `[clk_rtc]`; no-op otherwise. **BIOS call** — outside the gfx lock only. Preserves all registers. |
| `clk_bcd` / `clk_tobcd` | BCD ↔ binary byte helpers; `clk_bcd` returns CF = 1 on a non-decimal nibble. `clk_tobcd` clobbers AH. |

**Month names** are a 12×3 ASCII table (`'Jan'`…`'Dec'`), indexed by
month−1 ×3 — the same data serves `clk_fmt` and `clk_fld_str`.

**What this deliberately does not do.** No timezone, no DST (the DST byte
`AH=02h` returns is ignored and `AH=03h` is written 0), no day-of-week (the
BIOS date call does not return one and nothing displays it), and no
re-reading of the RTC after boot — the PIT is the clock from then on, which
is exactly how DOS behaves on the same hardware.
