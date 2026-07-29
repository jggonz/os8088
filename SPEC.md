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
   `fm_`/`files_`, `ico_`/`icon_`, `desk_`, `dock_`, `tm_`, `cp_`) or use
   NASM local labels (`.foo`).
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
| 0x20000       | 0x2000  | `SAVE_SEG` — save-under heap (menus), raw, via ES  |
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
  ticks but does not switch. Used only inside scheduler internals — normal
  code uses gfx_lock, which does NOT stop pre-emption (background tasks keep
  running during drags; they just block on gfx_lock when they try to draw).
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
`inc word [ticks]` → `sch_account` (§8.1) → the sleeper-wake scan →
`sch_lock` → the mode check. So cycle accounting and `task_sleep` deadlines
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
W_FLAGS equ 0    ; word: bit0 = used, bit1 = visible
W_X     equ 2    ; word, frame left (screen coords)
W_Y     equ 4    ; word, frame top (below menu bar: y >= MBAR_H)
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
```

(WIN_SIZE grew 16 → 18: any ×16 shift idioms in wm.inc must become a true
×18 multiply. The wm_create template stays **16 bytes**:
{x,y,w,h,title,paint,onkey,onclick} words. MAX_WIN grew 8 → 12 for
instancing (§29); `apps/os88api.inc` mirrors it.)

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

| symbol         | contract                                                     |
|----------------|--------------------------------------------------------------|
| `wm_init`      | zero table                                                   |
| `wm_create`    | in SI → 16-byte template {x,y,w,h,title,paint,onkey,onclick} words; out BX = window ptr, CF on table full. Created **hidden**; appends the window's index to `wm_zord` (frontmost) and increments `wm_zn`. Does not repaint — callable without the gfx lock. |
| `wm_destroy`   | in BX = win ptr: clear W_FLAGS (used+visible), reset the slot's `wm_owner` entry to 0xFF, remove its index from `wm_zord` (compact the array, decrement `wm_zn`), repaint all. Caller holds the gfx lock. The record slot becomes reusable by wm_create. |
| `wm_show`      | in BX = win ptr: set visible, bring to front, repaint all    |
| `wm_hide`      | in BX = win ptr: clear visible, repaint all                  |
| `wm_front`     | in BX = win ptr: raise to front of z-order, repaint all      |
| `wm_top`       | out BX = frontmost visible window ptr, 0 if none             |
| `wm_hit`       | in CX=x, DX=y; out BX = topmost visible window ptr containing the point (0 if none), AL = 0 content, 1 title bar, 2 close box, 3 minimize box. AL=2/AL=3 only when BX is the frontmost visible window (the only one with the boxes drawn); on any other window those regions report AL=1. |
| `wm_paint_all` | full repaint: desktop gray (below menu bar), then `desk_paint` (§26 — desktop icons sit on the desktop, under every window), then `dock_paint` (§30 — the dock strip sits on the desktop under every window, like the icons), menu bar, every visible window back→front (frame + white content + W_PAINT). Caller holds gfx lock. |
| `wm_content`   | in BX = win ptr; out AX = content left, DX = content top     |
| `wm_ptr2idx`   | in BX = win ptr (record-aligned); out AL = window index, AH = 0. Clobbers nothing else. The one public home of the `(ptr − wm_wins) / WIN_SIZE` idiom. |
| `wm_obscured`  | in BX = win ptr; out CF=1 if any visible window above BX in z-order overlaps its frame rect (background tasks use this to skip live updates when covered). Result is only trustworthy while the caller holds the gfx lock — the UI task mutates `wm_zord`/window rects under it. |

Paint procs and key handlers run on the **UI task** (via wm_paint_all /
dispatch) or on the window's own background task — in all cases the caller
of W_PAINT already holds the gfx lock. W_PAINT must not lock, block or spawn.

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

## 13. ui.inc — the UI task (task 0)

Loop forever:
1. Poll keyboard: int 16h AH=01; if a key, fetch (AH=00) and near-call the
   front window's W_ONKEY (if any) under gfx_lock, billed to the window's
   instance (§11 "callback billing").
2. `evq_pop`; on EVT_MDOWN at (x,y) — first store the event's EV_C into
   the public word `ui_click_t` (the click's birth tick; §22/§26 read it
   during dispatch):
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
   `cp_onclick`, which already holds it.
4. `task_yield`.

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
`sched_init` → `evq_init` → `vga_mode12` → `bb_init` (§32 — the RAM probe
must run after the mode set, which clears VRAM, and before the first
drawing call) → `font_init` → `wm_init` →
`inst_init` → `mouse_init` → `desk_init` → `files_init` → `loader_init` →
`tm_init` → gfx_lock → `wm_paint_all` → gfx_unlock → `cursor_show` → jump
into `ui_task` (task 0 never returns). (`dock_init` runs right after
`desk_init`.) **Clean boot**: no app instances exist — the first paint
shows only the desktop, drive icons, the empty dock strip and menu bar;
everything is launched from the menus (§13/§29). Include order:
`instance.inc` right after `wm.inc`; `icons.inc`, `desk.inc`, `dock.inc`,
`taskmgr.inc` and then `ctrl.inc` (§31) after `files.inc`;
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
module-global), title "Disk", 320×200 at (110,80). No background task, no
boot-time window: `files_init` (from kmain) only resets module state;
the window is created on demand by `app_launch` (§29), whose KD_INIT is
`fm_kinit` (clears `fm_sel`/`fm_clkt`). State: `fm_sel` (word, selected
row, 0xFFFF = none), `fm_clkt` (word, [ticks] at last row click),
`fm_mountok` (byte, 1 = last mount succeeded).

Content layout (coords relative to content top-left): header line at
(6,6): `"Drive B:  N files"` (drive letter from [disk_drive]) or, when the
last mount failed, `"No os8088 disk in drive B:"`. A **Refresh button** at
the top right: 1px black frame from (content_w−68, 2) to (content_w−6,
15), label "Refresh" centered inside — remounts the current drive so a
swapped disk shows its real contents. Status line from
`[ld_status]` at (6,182-TITLE_H): "", "Disk error", "Bad package",
"Too large", "Load failed", "Out of memory" (0..5) — plus "Loading..."
while a load is pending.
File rows: **16 px tall**, first at y=22, at most **8** rows shown
(entries beyond are mounted but not listed; row 8 ends at y=149, clear of
the status line at 164). Per row: the file's 16×16 icon at x=4 (from
`disk_icons` entry i; all-zero entry → built-in `ico_app16`, §25), name
at x=24, size right-aligned at content right minus 6, text baselines at
row top + 4. Selected row: drawn inverted (`gfx_xor_fill` over the row
band after drawing is acceptable).

Behaviour:
- `files_open_drive` (public; in AL = drive 0/1, no lock held): **always**
  `disk_mount` DL=AL — a swapped or newly chosen disk must never show
  stale contents — record success in fm_mountok, clear the selection,
  then `app_launch` KIND_FILES (creates the window, or fronts +
  un-minimizes the existing instance at cap; §29). Callers: CMD_FILES
  dispatch and desk_click (§26).
- `files_open` (from CMD_FILES dispatch, no lock held): AL = [disk_drive],
  fall into files_open_drive.
- `W_ONCLICK` (lock held): the Refresh button rect is tested first —
  inside it: `disk_mount` the current drive, update fm_mountok, clear
  selection, repaint content. Otherwise map DX to a row ((y−22)/16, guard
  y<22); row ≥ [disk_nfiles] or ≥ 8 → clear selection, repaint. Else if
  row == fm_sel and [ui_click_t]−fm_clkt < 9 (birth ticks, §10) → double-click: set [ld_pending]
  = row+1 (ui.inc runs the loader after the lock drops), repaint content
  (shows "Loading..."). Else select row, stamp fm_clkt, repaint content.
  Repaint = white-fill own content + redraw (like the Note Pad package's onkey; the
  caller already holds the lock).
- `W_ONKEY` (lock held): 'a'/'A' → drive 0, 'b'/'B' → drive 1, 'r'/'R' →
  same drive; all three: `disk_mount`, update fm_mountok, clear selection,
  repaint content. Enter (13) with a valid selection → same as
  double-click. (disk_mount under the gfx lock stalls painters ~a second;
  acceptable.)
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
fixed two-instance text pool. Behaviour is unchanged from §14: one window
"Note Pad" 260×180 at (60,60); paint renders the buffer at 8px per char
with a 6px left/top margin, wrapping at the content width, dropping any row
whose bottom would pass the content bottom (no scrolling) and drawing a 1px
caret only when its own row fits; onkey appends printable 32..126, deletes
on backspace, stores 13 on Enter, then white-fills and redraws **its own
content only**. No icon flag, so the Disk window shows `ico_app16`.

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
onclick, no boot-time window or task: `tm_init` (from kmain, after
loader_init) only reads total conventional RAM once via int 12h (kmain
runs on task 0, so §7's only-the-UI-task-calls-BIOS rule holds). The
window + monitor task exist only while an instance is open: `app_launch`
runs `tm_kinit` (zeroes all module state including the history ring —
the gauge calibration restarts from scratch at every launch — and caches
the window ptr in `tm_win`), then spawns `tm_task` with DX = the instance
index. `tm_task` checks I_STATE = 2 once per interval (after the spin
phase) and tears down via `inst_task_die` (§29).

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
  its I_STATE, I_TASK, I_SIZE, I_CYC and 16 I_NAME bytes (§29 records
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

**Drawing.** `tm_paint` (W_PAINT) is a bare, unconditional full redraw —
no lock, no visibility check (wm_paint_all calls it with the lock already
held, §11). tm_task's periodic path wraps its drawing Clock-style
(§14): gfx_lock, re-check visible + not `wm_obscured` under the lock (else
skip), and touches only what changed — the CPU + scheduler text line (one
line, redrawn whole every interval, so a mode change shows up within one
sample period without any extra plumbing), the new sweep
column plus an all-white gap column at the advanced tm_pos, the RAM line
and bar, and the process rows. The full 160-column graph render happens
only in tm_paint, so the periodic lock hold stays small (Bounce-scale).
All drawing is self-backgrounding (each element white-fills its own rect
or paints both segments), so tm_paint needs no preceding content clear
beyond the one wm_paint_all already does.

**Content layout** (content-relative; content is 174×245):

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
simultaneous instances), 1 pad byte.

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
    db 1, 0                           ; Control Panel: stateless task-less
                                      ; singleton
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
| `app_launch` | in AL = kind (built-in). UI task only, no lock held; takes its own locks. out CF=1 failed (instance/window/task table full — silent no-op for the caller), CF=0 done. Order: cap check (at cap → gfx_lock, clear the live instance's minimized bit, wm_show it, gfx_unlock — i.e. "launch" of a full singleton fronts it; only-dying-instances → CF=1, retry after a task period) → inst_alloc → pool-slot pick (first candidate `pool + s·stride` not held by a same-kind record with I_STATE != 0) → template copied to scratch with x/y cascaded +16·s → wm_create (CF → fail; record was never published) → fill record (I_KIND, I_TASK=0xFF, I_ICON, name) + inst_bind_win → KD_INIT → **publish I_STATE ← 1** → if KD_TASK: task_spawn (AX = entry, DX = instance index), I_TASK ← returned slot; spawn CF → rollback (I_STATE ← 0, then locked wm_destroy) → gfx_lock, wm_show, gfx_unlock. |
| `app_close_win` | in BX = window ptr; **caller holds the gfx lock**; UI task only. Unowned window → wm_hide (fallback). I_STATE = 2 already → wm_hide (idempotent). Task-less (I_TASK = 0xFF) → I_STATE ← 2, wm_destroy (clears wm_owner, repaints), I_WIN ← 0, I_STATE ← 0 — for a package instance that final store frees the region (rule 29.2.7). Task-owned → I_STATE ← 2 (the die flag), wm_hide (instant feedback); the task tears down at its next wake. |
| `inst_minimize` | in BX = window ptr, lock held: set I_FLAGS bit0 (unowned → skip), wm_hide. |
| `inst_restore` | in DI = record, lock held: clear I_FLAGS bit0, wm_show I_WIN. |
| `inst_task_die` | in DI = the CURRENT task's instance record; no lock held; **never returns**: gfx_lock, wm_destroy I_WIN (clears wm_owner), I_WIN ← 0, gfx_unlock, then `jmp task_exit` with BX = record ptr (I_STATE is offset 0 — the release byte). |
| `inst_launch_post` | in AL = kind: one atomic word store of kind+1 into `inst_launch` — the deferred launch channel for lock-held posters (drained by ui_task step 3, §13). Rapid double posts coalesce (last wins). |

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
320×120 at (160,130). Label prefix `cp_`. Included from kernel.asm right
after `taskmgr.inc`.

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
cp_tpl:  dw 160, 130, 320, 120, cp_ttl, cp_paint, 0, cp_onclick
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

Content is 318×101 (frame 320×120 minus the 1px borders and the 18px title
bar). Every coordinate below is relative to the window's content origin,
which `cp_paint`/`cp_onclick` derive from the live `W_X`/`W_Y` — **never
hardcoded screen coords**, because the window drags. Both callbacks receive
the window in SI and `wm_content` takes it in BX (out AX = content left,
DX = content top), so both copy SI → BX first and then carry the origin in
**DI = content left, BP = content top** — the register pair every helper in
the module takes it in.

```nasm
CP_CW    equ 318     ; content width          CP_RX   equ 96   ; right pane x
CP_CH    equ 101     ; content height
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
cp_items_end:
CP_ITEMS   equ (cp_items_end - cp_items) / CP_ISTRIDE
```

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

**Accounting.** `KLOWFAR_KB` (§15.1) is what the kernel occupies outside its
own segment — `.lowbss` plus `.fartext` — and both the Task Manager's RAM
total and its System row add it, so the rows still sum to the total (§28).
