# jop 1.0 — GUI specification

This document is the **binding contract** for all kernel modules. Every symbol
name, register contract, constant, and data layout here is pinned. Implement
exactly what is written; put questions in your report, not in the code.

## 0. Goal

A Macintosh System 1-style graphical OS for an 8086/8088 XT-class machine with
an ISA VGA card. 640x480, 16 colors (VGA mode 12h). Pre-emptive multitasking
via the PIT timer interrupt. Serial Microsoft mouse on COM1. Boots from floppy
straight into the GUI: gray dithered desktop, menu bar with pull-down menus,
draggable overlapping windows with title bars and close boxes. Built-in apps:
About dialog, Note Pad (typing), Clock and Bounce (each running as its own
pre-empted background task, updating live while the user types or drags).

## 1. Hard rules (apply to every module)

1. **8086 only.** `kernel.asm` opens with `cpu 8086`; NASM will reject
   anything newer. Consequences: no `pusha/popa`, no `push imm`, no
   `shl reg, imm` other than 1 (use CL), no `movzx`, no 32-bit registers, no
   `imul r,r,imm`. `rep movsb/stosb/lodsb`, `mul`, `div` are fine.
2. **Tiny model.** CS = DS = SS = 0x1000 for all kernel code and tasks. ES is
   scratch — any routine may change and use ES freely, but must restore it
   before returning unless documented otherwise. All calls between modules are
   **near** calls.
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
5. **Label hygiene.** One flat namespace. Prefix every module-internal label
   with the module's prefix (`vga_`, `font_`, `mou_`, `cur_`, `sch_`, `evq_`,
   `wm_`, `menu_`, `ui_`, `app_`, `dsk_`/`disk_`, `ld_`/`loader_`,
   `fm_`/`files_`, `ico_`/`icon_`, `desk_`) or use NASM local labels
   (`.foo`).
6. Public drawing routines may assume the caller holds the **gfx lock**
   (§7) and that the cursor is hidden. They must not take the lock
   themselves.
7. Every gfx routine clips to the screen (0..639, 0..479) and leaves the VGA
   Graphics Controller in the default state on return: Set/Reset enable = 0,
   Bit Mask = FF, Function = replace, Data Rotate = 0, Map Mask = 0Fh,
   Read Map = 0, write mode 0 / read mode 0.

## 2. Memory map

| linear        | segment | contents                                          |
|---------------|---------|----------------------------------------------------|
| 0x00500       | —       | free; boot stack grows down from 0x7C00            |
| 0x07C00       | 0000    | boot sector                                        |
| 0x10000       | 0x1000  | kernel: code, data, .bss (stacks, buffers)         |
| 0x1A000       | 0x1000  | `APP_LOAD_OFF` — loaded-program region, 20KB (§20) |
| 0x1F000       | 0x1000  | free gap; boot/task-0 stack grows down from 0xFFFE |
| 0x20000       | 0x2000  | `SAVE_SEG` — save-under heap (menus), raw, via ES  |
| 0xA0000       | 0xA000  | VGA planar framebuffer, 80 bytes/row               |

Kernel image + .bss must fit below offset **0xA000** (task stacks included);
`kernel.asm` ends with a build-time assertion (§15). Loaded programs occupy
kernel-segment offsets 0xA000..0xEFFF so they share the tiny model: their
paint/key/click procs are ordinary near pointers. Works in 256KB of RAM.

## 3. Global constants (defined once in kernel.asm, used everywhere)

```nasm
KERNEL_SEG   equ 0x1000
SAVE_SEG     equ 0x2000
VGA_SEG      equ 0xA000
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
APP_LOAD_OFF equ 0xA000      ; where packages load (kernel segment offset)
APP_MAX_SIZE equ 0x5000      ; image + bss budget, 0xA000..0xEFFF
```

## 4. Module files and ownership

| file                | owns                                                    |
|---------------------|---------------------------------------------------------|
| `kernel/kernel.asm` | entry, constants, init order, includes, .bss layout, **jop API jump table at 0x0010** (§20.3) + japi helper routines |
| `kernel/vga12.inc`  | mode set, planar primitives, save/restore, gfx lock     |
| `kernel/font.inc`   | 8x8 font (copied from VGA BIOS ROM at init), text draw  |
| `kernel/mouse.inc`  | COM1 UART, IRQ4 ISR, packet decode, cursor (save-under) |
| `kernel/sched.inc`  | PIT hook, context switch, task table, spawn/yield/sleep |
| `kernel/events.inc` | 8-byte event records, system event ring queue           |
| `kernel/wm.inc`     | window records, z-order, frames, hit test, paint-all    |
| `kernel/menu.inc`   | menu bar, pull-down tracking, command return            |
| `kernel/ui.inc`     | UI task: event pump, keyboard poll, drag, dispatch      |
| `kernel/apps.inc`   | About, Note Pad, Clock task, Bounce task                |
| `kernel/disk.inc`   | BIOS int 13h floppy reads, jopfs mount + directory (§18–19) |
| `kernel/loader.inc` | package validation, load into APP region, launch (§21)  |
| `kernel/files.inc`  | Disk window: file list UI, selection, open, refresh (§22) |
| `kernel/icons.inc`  | 1-bit icon format, draw routine, built-in library (§25) |
| `kernel/desk.inc`   | desktop drive icons: detect, paint, click/open (§26)    |
| `kernel/taskmgr.inc`| Task Manager window: CPU load gauge + history graph, RAM readout, task list (§28) |

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
| `gfx_unlock`    | —                                    | show cursor + release mutex           |

Save/restore for a W-px-wide, H-px-tall rect uses
`bytes = ((x2/8) - (x1/8) + 1) * H * 4`; provide `gfx_save_size`
(same rect regs in, AX = byte count out) so callers can budget buffers.

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
window.

## 7. Concurrency model (read carefully — this is the crux)

- **gfx lock**: byte `gfx_lock_flag` in vga12.inc. `gfx_lock`:
  `cli`; if flag set → `sti`, `call task_yield`, retry; else set flag, `sti`,
  then `call cursor_hide`. `gfx_unlock`: `call cursor_show`, then clear flag.
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
  tasks (clock, bounce) use only jop primitives and `ticks`. The syscall gate
  remains for compatibility but the GUI does not depend on it.
- **Scheduler lock**: `sch_lock` byte; when non-zero the timer ISR counts
  ticks but does not switch. Used only inside scheduler internals — normal
  code uses gfx_lock, which does NOT stop pre-emption (background tasks keep
  running during drags; they just block on gfx_lock when they try to draw).

## 8. sched.inc — pre-emptive round-robin

- `MAX_TASKS equ 4`. Task 0 is the boot thread (becomes the UI task). Each
  task has a 1536-byte stack in .bss (`sch_stacks`).
- Task record (8 bytes): `T_STATE` (0 free, 1 ready, 2 sleeping), `T_SP`
  (saved SP), `T_WAKE` (tick count to wake at), padding. `T_SIZE equ 8`.
- Timer: hook int 08h. Handler: push registers + DS/ES, load DS=KERNEL_SEG,
  chain to the saved BIOS vector first (`pushf` + far call), then
  `inc word [ticks]`, wake sleepers whose `T_WAKE` has passed, and if
  `sch_lock` is clear, switch: with all GP regs + ES + DS on the current
  stack, store SP in the task record, pick the next ready task round-robin,
  load its SP, pop, `iret`. (The BIOS handler already sent EOI; do not send
  another.)
- `sched_init` — set up task 0 as current/ready, save old int 08h vector,
  install handler.
- `task_spawn` — in: AX = entry point (near). Builds a fresh stack frame that
  `iret`s into the entry with IF=1, DS=ES=KERNEL_SEG. Out: CF set if no free
  slot. **Publication order matters**: the timer ISR is live during spawn, so
  the full frame must be built and T_SP stored before the slot is marked
  ready — the `mov byte [T_STATE], 1` store must be the last write to the
  record (a byte store is atomic w.r.t. interrupts on the 8086). A task's
  entry routine must never `ret` (loop forever).
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
  (dword) is module-internal.

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
  3 bytes wide × 16 rows × 4 planes = 192 bytes.
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

Window record, 16 bytes, fixed offsets:

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
MAX_WIN  equ 8
```

(WIN_SIZE grew 16 → 18: any ×16 shift idioms in wm.inc must become a true
×18 multiply. The wm_create template grows to **16 bytes**:
{x,y,w,h,title,paint,onkey,onclick} words — every existing template in
apps.inc gains a trailing `dw 0`.)

Storage: `wm_wins` (MAX_WIN × WIN_SIZE, .bss), z-order byte array
`wm_zord` (window indices, index 0 = backmost) + `wm_zn` count. Every used
window is always present in `wm_zord` — membership is established at
wm_create and removed only by `wm_destroy`; `wm_hide` clears only the
visible bit, and `wm_show`/`wm_front` only reorder. The visible flag alone
decides whether a window is painted, hit-tested, or counted by
`wm_top`/`wm_obscured`.

Frame drawing (paint-all does this before calling W_PAINT):
- 1px black outline around the whole frame; 1px black drop shadow along the
  right and bottom edges, offset (+1,+1), Mac-style.
- Title bar: rows W_Y+1 .. W_Y+TITLE_H-2 (16 rows), white. If the window is
  frontmost: 6 horizontal black pinstripes inset 3px from each side, and a
  close box — 11×11 white square with black frame at x = W_X+8, occupying
  rows W_Y+4 .. W_Y+14, pinstripes broken 2px around it. Title text
  centered, black, on a white gap 6px each side of the text (so stripes
  don't touch it).
- 1px black separator line at row W_Y+TITLE_H-1. Content area =
  x+1 .. x+w-2, y+TITLE_H .. y+h-2, filled white before W_PAINT is called.

| symbol         | contract                                                     |
|----------------|--------------------------------------------------------------|
| `wm_init`      | zero table                                                   |
| `wm_create`    | in SI → 16-byte template {x,y,w,h,title,paint,onkey,onclick} words; out BX = window ptr, CF on table full. Created **hidden**; appends the window's index to `wm_zord` (frontmost) and increments `wm_zn`. Does not repaint — callable without the gfx lock. |
| `wm_destroy`   | in BX = win ptr: clear W_FLAGS (used+visible), remove its index from `wm_zord` (compact the array, decrement `wm_zn`), repaint all. Caller holds the gfx lock. The record slot becomes reusable by wm_create. |
| `wm_show`      | in BX = win ptr: set visible, bring to front, repaint all    |
| `wm_hide`      | in BX = win ptr: clear visible, repaint all                  |
| `wm_front`     | in BX = win ptr: raise to front of z-order, repaint all      |
| `wm_top`       | out BX = frontmost visible window ptr, 0 if none             |
| `wm_hit`       | in CX=x, DX=y; out BX = topmost visible window ptr containing the point (0 if none), AL = 0 content, 1 title bar, 2 close box. AL=2 only when BX is the frontmost visible window (the only one with a close box drawn); on any other window that region reports AL=1. |
| `wm_paint_all` | full repaint: desktop gray (below menu bar), then `desk_paint` (§26 — desktop icons sit on the desktop, under every window), menu bar, every visible window back→front (frame + white content + W_PAINT). Caller holds gfx lock. |
| `wm_content`   | in BX = win ptr; out AX = content left, DX = content top     |
| `wm_obscured`  | in BX = win ptr; out CF=1 if any visible window above BX in z-order overlaps its frame rect (background tasks use this to skip live updates when covered). Result is only trustworthy while the caller holds the gfx lock — the UI task mutates `wm_zord`/window rects under it. |

Paint procs and key handlers run on the **UI task** (via wm_paint_all /
dispatch) or on the window's own background task — in all cases the caller
of W_PAINT already holds the gfx lock. W_PAINT must not lock, block or spawn.

## 12. menu.inc

Menu bar: rows 0..MBAR_H-1, white, 1px black line at row MBAR_H-1. First
menu is the Apple: an 11×11 one-color apple silhouette bitmap (any
recognizable apple-with-bite shape; hand-authored `dw` rows are fine — this
is the one place bitmap data is hand-made). Others are text titles.

```nasm
; menu table (menu.inc data):
; per menu: { titleptr (0 = apple glyph), itemsptr, item count, cmd base }
; items: array of near ptrs to NUL strings
CMD_ABOUT  equ 1
CMD_NOTE   equ 2
CMD_CLOCK  equ 3
CMD_BOUNCE equ 4
CMD_FILES  equ 5
CMD_CLOSE  equ 6
CMD_TASKS  equ 7
CMD_REBOOT equ 8
```

Menus: **Apple**: "About jop..." (CMD_ABOUT). **File**: "Note Pad"
(CMD_NOTE), "Clock" (CMD_CLOCK), "Bounce" (CMD_BOUNCE), "Disk"
(CMD_FILES), "Close Window" (CMD_CLOSE). **Special**: "Task Manager"
(CMD_TASKS), "Restart" (CMD_REBOOT). (CMD_REBOOT was renumbered 7 → 8
when Task Manager was inserted, as CMD_CLOSE/CMD_REBOOT were when Disk
was — cmd = menu base + item index must still hold.)

| symbol          | contract                                                   |
|-----------------|-------------------------------------------------------------|
| `menu_draw_bar` | draw the bar + titles (gfx lock held by caller)             |
| `menu_track`    | in: CX = mousedown x. Runs the whole interaction while the button is held (caller holds gfx lock): highlight title (xor), drop the menu (gfx_save under it to SAVE_SEG:0), track item highlight following `mouse_y`, on release restore save-under + unhighlight; out AX = CMD_* or 0. Item cells are 16px tall, menu width = widest item + 16px padding. |

`menu_track` polls `mouse_btn`/`mouse_x`/`mouse_y` directly (the ISR keeps
them fresh; cursor stays hidden during tracking since the gfx lock is held —
acceptable, tracking feedback is the highlight).

## 13. ui.inc — the UI task (task 0)

Loop forever:
1. Poll keyboard: int 16h AH=01; if a key, fetch (AH=00) and near-call the
   front window's W_ONKEY (if any) under gfx_lock.
2. `evq_pop`; on EVT_MDOWN at (x,y) — first store the event's EV_C into
   the public word `ui_click_t` (the click's birth tick; §22/§26 read it
   during dispatch):
   - y < MBAR_H → gfx_lock, `menu_track`, gfx_unlock, then dispatch CMD_*
     (below).
   - else `wm_hit`: close box → if released over it (simplify: immediately)
     `wm_hide` that window. Title bar → if not front, `wm_front`; then run
     the **drag loop**: gfx_lock; xor-draw the outline at the window rect;
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
     near-call it (CX=x, DX=y, SI=win ptr), gfx_unlock; else ignore.
   - no window hit (wm_hit BX=0) → call `desk_click` (§26) with CX=x,
     DX=y, no lock held — desktop icons are hit-tested only after every
     window has declined the click, which gives them correct z-order
     semantics for free.
3. If `[ld_pending]` is non-zero (§21): AX = [ld_pending] − 1, zero
   `[ld_pending]`, call `loader_run`. This runs **outside** the gfx lock —
   loader_run manages its own locking.
4. `task_yield`.

Command dispatch: CMD_ABOUT/NOTE/CLOCK/BOUNCE/TASKS → `wm_show` the
corresponding window (created at boot, initially hidden or shown per §15;
CMD_TASKS shows `[tm_win]`, §28). CMD_FILES →
call `files_open` (§22; same position in the dispatch flow as the wm_show
cases — files_open does its own locking). CMD_CLOSE → `wm_hide` frontmost.
CMD_REBOOT → gfx_lock (never released), `vga_text`, `sched_unhook`,
`int 0x19`.

All wm_* calls that repaint are made under gfx_lock by the UI task.

## 14. apps.inc

Four windows, records created at boot by `apps_init` (called from kmain
before the first paint). Templates:

- **About** — 300×120 at (170,140), title "About jop". Paint: centered lines
  "jop 1.0", "a graphical OS for the 8086", "pre-emptive - 640x480 - 16
  colors". No onkey. Hidden at boot.
- **Note Pad** — 260×180 at (60,60), title "Note Pad". 512-byte text buffer
  in .bss. Paint: render buffer, 8px chars, line wrap at content width,
  6px left/top margin, then a 1px black caret. Vertical clip: stop before
  emitting any text row whose bottom (content top + 6 + 8*row + 7) would
  exceed the content bottom (W_Y + W_H - 2); overflow lines are discarded,
  not drawn (no scrolling), and if the caret's row does not fit, the caret
  is not drawn. Onkey: printable ASCII appends (buffer full = beep-less
  drop), backspace (ASCII 8) deletes, Enter (13) newline. After editing,
  the handler repaints **its own content only** (white-fill content, redraw
  text) — caller already holds the lock. Visible at boot.
- **Clock** — 130×60 at (450,80), title "Clock". Keeps its own time in
  .bss: `clk_h`/`clk_m`/`clk_s` (bytes, boot at 0), `clk_last` (word, last
  `[ticks]` sample), `clk_acc` (word, tenth-of-tick accumulator). Its
  **own task** (`app_clock_task`): loop { task_sleep 9; AX = [ticks];
  delta = AX - [clk_last] (subtraction idiom, safe across wrap);
  [clk_last] = AX; clk_acc += delta*10; while clk_acc >= 182:
  clk_acc -= 182 and advance seconds with carries (s 60→0/m+1, m 60→0/h+1,
  h 24→0). This time-keeping runs every iteration; only drawing is
  conditional: gfx_lock; **re-check under the lock** window visible and not
  `wm_obscured` — if it fails, gfx_unlock and skip; else white-fill the
  string rect (font background is transparent, §6), set `[gfx_color]` =
  CBLACK, draw HH:MM:SS from clk_h/m/s centered in content; gfx_unlock }.
  Paint proc renders the same string from clk_h/m/s. Visible at boot.
- **Bounce** — 150×130 at (440,260), title "Bounce". Own task: loop
  { task_sleep 2; gfx_lock; **check under the lock** visible and not
  obscured — if the check fails, gfx_unlock and skip the frame without
  erasing or stepping; else erase 8×8 black square at old pos (white
  fill), step x/y by velocity, bounce off content edges, draw at new pos;
  gfx_unlock }. Paint proc: white content + square at current pos.
  Visible at boot.

`apps_init` creates all four (wm_create), marks Note Pad/Clock/Bounce
visible (set W_FLAGS bit1 directly; the boot sequence does one
wm_paint_all afterwards), stores the window ptrs, and `task_spawn`s
`app_clock_task` and `app_bounce_task`.

## 15. kernel.asm — boot sequence

Keep the 0x0000 cold entry. At 0x0010 the retired syscall gate is replaced
by the **jop API jump table** (§20.3) — a run of 4-byte `jmp near` slots at
pinned offsets. kernel.asm also owns the tiny japi helper routines
(§20.4) and the `japi_seed` word. `cpu 8086` + `bits 16` + `org 0`.

kmain: set segments/stack (SP=0xFFFE), `sti`, `cld`, then:
`sched_init` → `evq_init` → `vga_mode12` → `font_init` → `wm_init` →
`mouse_init` → `desk_init` → `apps_init` → `files_init` → `loader_init` →
`tm_init` → gfx_lock → `wm_paint_all` → gfx_unlock → `cursor_show` → jump
into `ui_task` (task 0 never returns). Include order appends `icons.inc`,
`desk.inc` and `taskmgr.inc` after `files.inc`.

End of file (after all `%include` lines, with `section .text` in effect):

```nasm
kernel_text_end:
KTEXT_SIZE equ kernel_text_end - $$

section .bss
; (modules already declared their own .bss blocks inside their .inc files;
;  NASM accumulates them in declaration order — this block lands last)
kernel_bss_end:
KBSS_SIZE equ kernel_bss_end - $$
%if KTEXT_SIZE + KBSS_SIZE > 0xA000
%error "kernel too big: image + bss must stay below APP_LOAD_OFF (0xA000)"
%endif
```

(The sizes must be same-section label differences bound via `equ` — a bare
label in `%if` is a non-scalar and will not assemble. Keep this block last.
The limit is 0xA000 now, not 0xF000: everything above it belongs to the
loaded-program region, §20.)

## 16. Build & test

- Makefile: boot-image recipes unchanged. `run` target keeps the serial
  mouse (`-chardev msmouse,id=m0 -serial chardev:m0`) and now also attaches
  the software floppy as drive B:
  `-drive file=build/apps.img,format=raw,if=floppy,index=1`.
  `test` target: same, plus `-display none -qmp unix:build/qmp.sock,server,nowait`.
- New tooling targets: see §24 (apps, packages, jopfs images).
- 86Box config (`vm/xt/86box.cfg`): set the mouse to a serial Microsoft
  mouse on COM1 (best-effort; cannot be verified headless).
- The kernel may exceed 8 sectors; the two images are already built
  separately with correct geometry, and the boot sector's LBA→CHS handles
  cylinders > 0. Nothing to change in boot.asm.

## 17. Definition of done

1. `make` builds all images with zero warnings; kernel < 0xA000 with .bss.
2. QEMU boots to: gray desktop, menu bar (apple + File + Special), Note Pad
   + Clock + Bounce windows open, arrow cursor.
3. Clock ticks and ball bounces **while** typing in Note Pad and while a
   drag outline is being moved (pre-emption visibly working).
4. Mouse moves cursor; windows drag by title bar; clicking a back window
   raises it; close box hides; menus pull down and dispatch.
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
10. Special → Task Manager opens a window with a live CPU load gauge and
    history graph (near 0% idle, visibly rising while dragging a window),
    a RAM readout, and the four-slot task list with per-task CPU shares —
    all updating twice a second while Clock and Bounce keep running.

## 18. disk.inc — floppy reads (BIOS int 13h)

Only the UI task touches the disk (extends §7's BIOS rule to int 13h).
During a read, task switching is paused: `inc byte [sch_lock]` before,
`dec` after (the timer ISR still chains the BIOS tick, which the floppy
motor logic needs). The gfx lock is NOT held across disk I/O.

Geometry lives in variables so both 1.44MB (18 spt) and 360KB (9 spt) data
disks work: `disk_spt` (word), `disk_heads` (word) — loaded from the jopfs
superblock at mount. LBA→CHS: cyl = LBA/(spt×heads); rem = LBA%(spt×heads);
head = rem/spt; sector = rem%spt + 1. Reads go **one sector per int 13h
call** (AH=02, AL=1) — no multi-sector calls, so track boundaries and DMA
alignment never matter. Each sector: up to 3 attempts, with AH=00 reset on
failure between attempts.

| symbol       | contract                                                      |
|--------------|----------------------------------------------------------------|
| `disk_read`  | in: AX=LBA, CX=sector count, ES:BX → dest (advances BX by 512 per sector; caller's ES:BX budget must cover count×512). Drive from `[disk_drive]`. Out: CF=1 on unrecoverable error. Preserves registers per §1. |
| `disk_mount` | in: DL=drive (0=A, 1=B). Sets `[disk_drive]`, reads LBA 0 with the *fallback* geometry spt=9/heads=2 (CHS 0/0/1 — identical under any real floppy geometry), validates the superblock (§19), loads `disk_spt`/`disk_heads`/`disk_nfiles`, then reads the 2 directory sectors (LBA 1–2) into `disk_dir` and the 4 icon-table sectors (LBA 3–6) into `disk_icons`. Out: CF=1 if the disk is unreadable or not jopfs (then `disk_nfiles`=0). |
| `disk_drive`  | byte variable, current drive (init 1 = B:)                   |
| `disk_nfiles` | word, valid after a successful mount (else 0)                |
| `disk_dir`    | 1024-byte .bss buffer: the 32 directory entries              |
| `disk_icons`  | 2048-byte .bss buffer: the 32 icon-table entries (§19); entry i belongs to directory entry i, 64 bytes each, all-zero = no icon |

## 19. jopfs — on-disk format (data floppies)

A jopfs floppy is not bootable and holds only packages. All words
little-endian. Sector size 512.

**LBA 0 — superblock:**

| off | size | contents                                  |
|-----|------|-------------------------------------------|
| 0   | 8    | magic `"JOPFS2"` then two zero bytes      |
| 8   | 2    | sectors per track (18 or 9)               |
| 10  | 2    | heads (2)                                 |
| 12  | 2    | file count (0..32)                        |
| 14  | 498  | zero                                      |

(Format v2: v1 disks — magic `JOPFS1`, no icon table, data from LBA 3 —
are no longer accepted; nothing shipped in v1, so no compatibility
shim.)

**LBA 1–2 — directory**, 32 entries × 32 bytes:

| off | size | contents                                            |
|-----|------|------------------------------------------------------|
| 0   | 16   | file name, printable ASCII, NUL-padded (≤15 chars)  |
| 16  | 2    | type: 1 = application package (.jop)                |
| 18  | 2    | start LBA of file data                              |
| 20  | 2    | size in bytes                                       |
| 22  | 10   | zero                                                |

**LBA 3–6 — icon table**, 32 entries × 64 bytes, entry i belongs to
directory entry i: 16 words of AND-style mask (white underlay) then 16
words of data (black pixels), bit 15 = leftmost pixel, row-major (the
§25 16×16 icon body, without the 2-byte header). An all-zero entry means
"no icon" — viewers fall back to the built-in `ico_app16`. jopdisk.py
fills entries from each package's embedded icon (§20.2 flags bit 0), or
zeros.

File data starts at LBA 7; every file starts on a sector boundary. Entries
are packed from index 0; `file count` in the superblock is authoritative.

## 20. Loadable programs — the .jop package format

### 20.1 Region

A package is a flat 8086 binary assembled with `org APP_LOAD_OFF` (0xA000)
and loaded verbatim to KERNEL_SEG:0xA000. It runs in the tiny model exactly
like kernel code: CS=DS=SS=KERNEL_SEG, near calls everywhere, §1 hard rules
apply (cpu 8086, register discipline, no bare `sti` in handlers). Its
paint/onkey/onclick procs are near pointers into the region. Budget:
image + zeroed-bss ≤ APP_MAX_SIZE (0x5000). One program is resident at a
time; loading another replaces it (§21).

### 20.2 Header — first 32 bytes of the file (and of memory at 0xA000)

| off | size | contents                                                  |
|-----|------|------------------------------------------------------------|
| 0   | 2    | magic: bytes `'J','P'` (word 0x504A)                      |
| 2   | 1    | format version = 1                                        |
| 3   | 1    | flags: bit 0 = embedded icon follows the header; bits 1–7 zero |
| 4   | 2    | load offset — must equal 0xA000                           |
| 6   | 2    | entry offset (absolute, ≥ 0xA020, < load+image size)      |
| 8   | 2    | image size = total file bytes, header included            |
| 10  | 2    | bss size — bytes the loader zeroes after the image        |
| 12  | 4    | zero (reserved)                                           |
| 16  | 16   | program name, printable, NUL-padded (shown by tools)      |

**Embedded icon** (flags bit 0): file offset 32..95 holds the program's
16×16 icon — 16 mask words then 16 data words (same body layout as the
jopfs icon table, §19). With the flag set, image size must be ≥ 96 and
the entry offset ≥ 0xA060. The kernel loader ignores the flag entirely
(the icon rides along in memory like any other data); it exists for
jopdisk.py, which copies the block into the disk's icon table.

**Entry contract**: near-called by the loader with DS=ES=KERNEL_SEG, IF=1,
gfx lock NOT held. The program creates its window(s) via the API table
(wm_create is lock-free) and **returns** BX = window ptr with CF clear;
the loader wm_shows it. CF set = abort (loader reports "load failed"); an
aborting entry must return BX = its already-created window ptr (or 0 if it
created none) so the loader can wm_destroy it — otherwise aborted loads
would leak window records.
The entry must not call wm_show/wm_hide/wm_front, spawn tasks, or draw.
After entry returns, the program is pure event-driven code: its W_PAINT /
W_ONKEY / W_ONCLICK procs run under the gfx lock per §11.

### 20.3 The jop API jump table (kernel.asm, fixed offsets)

At KERNEL_SEG:0x0010, 4-byte slots, each `jmp near target` + 1 pad byte.
Programs `call` these absolute offsets; register contracts are the target
routines' own (§5, §6, §8, §11). Pinned layout:

```
0x0010 gfx_lock        0x0034 gfx_xor_fill    0x0058 wm_obscured
0x0014 gfx_unlock      0x0038 font_char       0x005C task_yield
0x0018 gfx_pixel       0x003C font_str        0x0060 task_sleep
0x001C gfx_hline       0x0040 font_width      0x0064 japi_get_ticks
0x0020 gfx_vline       0x0044 wm_create       0x0068 japi_set_color
0x0024 gfx_fill        0x0048 wm_show         0x006C japi_mouse
0x0028 gfx_frame       0x004C wm_hide         0x0070 japi_srand
0x002C gfx_fill_gray   0x0050 wm_front        0x0074 japi_rand
0x0030 gfx_xor_rect    0x0054 wm_content
```

### 20.4 japi helpers (kernel.asm)

| symbol           | contract                                            |
|------------------|------------------------------------------------------|
| `japi_get_ticks` | out AX = [ticks]                                     |
| `japi_set_color` | in AL → [gfx_color]                                  |
| `japi_mouse`     | out CX=[mouse_x], DX=[mouse_y], AL=[mouse_btn]       |
| `japi_srand`     | in AX → [japi_seed]                                  |
| `japi_rand`      | seed = seed×25173 + 13849; out AX = seed             |

### 20.5 apps/jopapi.inc — the program-side SDK

NASM include used by packages (not by the kernel). Provides: `JAPI_*` equs
for every table offset (§20.3), the window-record W_* / template offsets
(§11), color constants, and a `JOP_HEADER 'NAME', entry_label` macro that
emits the §20.2 header (image size via a forward-referenced
`equ` to an end label the program declares with `JOP_BSS n` /
end-of-file macro — exact macro design is the implementer's, but a package
source must be able to consist of just `%include "jopapi.inc"`, the header
macro, code/data, and an end macro).

Icon support: `JOP_HEADER 'NAME', entry, 1` sets flags bit 0; the author
then writes `JOP_ICON16` (asserts, via `%if`-on-equ, that it starts at
offset 32), 32 hand-authored `dw` rows (16 mask, 16 data), and
`JOP_ICON16_END` (asserts offset 96). The third JOP_HEADER parameter is
optional and defaults to 0.

## 21. loader.inc

State (.bss, cleared by `loader_init`): `ld_pending` (word: 0 = none, else
directory index+1 — set by files.inc, consumed by ui.inc §13), `ld_appwin`
(word: window ptr of the resident program, 0 = none), `ld_status` (byte:
0 ok, 1 disk error, 2 not a valid package, 3 too large, 4 entry aborted).

`loader_run` — in AX = directory index. UI task only, gfx lock not held on
entry. Steps:
1. Validate the entry: index < [disk_nfiles], type = 1, size ≤ APP_MAX_SIZE
   and non-zero → else status 2, step 7.
2. If `[ld_appwin]` non-zero: gfx_lock, `wm_destroy` it, gfx_unlock, zero
   it. (Must happen before the read clobbers the region the old program's
   procs live in.)
3. `disk_read` the file — start LBA and ceil(size/512) sectors — into
   ES=KERNEL_SEG, BX=APP_LOAD_OFF. CF → status 1, step 7.
4. Validate the header (§20.2): magic, version, load offset, image+bss ≤
   APP_MAX_SIZE, entry in range → else status 2, step 7.
5. Zero bss-size bytes at load+image size.
6. Near-call the entry. CF → status 4. Else: store BX in `ld_appwin`,
   gfx_lock, `wm_show` BX, gfx_unlock, status 0.
7. Set `[ld_status]`, call `files_refresh` (§22).

## 22. files.inc — the Disk window (file manager)

Built-in window, record created hidden by `files_init` (from kmain), title
"Disk", 320×200 at (110,80). No background task. State: `fm_sel` (word,
selected row, 0xFFFF = none), `fm_clkt` (word, [ticks] at last row click),
`fm_mountok` (byte, 1 = last mount succeeded).

Content layout (coords relative to content top-left): header line at
(6,6): `"Drive B:  N files"` (drive letter from [disk_drive]) or, when the
last mount failed, `"No jop disk in drive B:"`. A **Refresh button** at
the top right: 1px black frame from (content_w−68, 2) to (content_w−6,
15), label "Refresh" centered inside — remounts the current drive so a
swapped disk shows its real contents. Status line from
`[ld_status]` at (6,182-TITLE_H): e.g. "", "Disk error", "Bad package",
"Too large", "Load failed" — plus "Loading..." while a load is pending.
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
  then gfx_lock, wm_show, gfx_unlock. Callers: CMD_FILES dispatch and
  desk_click (§26).
- `files_open` (from CMD_FILES dispatch, no lock held): AL = [disk_drive],
  fall into files_open_drive.
- `W_ONCLICK` (lock held): the Refresh button rect is tested first —
  inside it: `disk_mount` the current drive, update fm_mountok, clear
  selection, repaint content. Otherwise map DX to a row ((y−22)/16, guard
  y<22); row ≥ [disk_nfiles] or ≥ 8 → clear selection, repaint. Else if
  row == fm_sel and [ui_click_t]−fm_clkt < 9 (birth ticks, §10) → double-click: set [ld_pending]
  = row+1 (ui.inc runs the loader after the lock drops), repaint content
  (shows "Loading..."). Else select row, stamp fm_clkt, repaint content.
  Repaint = white-fill own content + redraw (like Note Pad's onkey; the
  caller already holds the lock).
- `W_ONKEY` (lock held): 'a'/'A' → drive 0, 'b'/'B' → drive 1, 'r'/'R' →
  same drive; all three: `disk_mount`, update fm_mountok, clear selection,
  repaint content. Enter (13) with a valid selection → same as
  double-click. (disk_mount under the gfx lock stalls painters ~a second;
  acceptable.)
- `files_refresh` (called by loader_run, no lock held): acquire gfx_lock,
  and if the window is visible call `wm_paint_all`; unlock. It must be a
  full repaint, not content-only: loader_run calls it right after wm_show
  raised the loaded program's window, which may overlap the Disk window —
  a content-only repaint would paint over the new front window.

## 23. Minesweeper — the first software package (apps/mines/mines.asm)

Not kernel code: a .jop package built with jopapi.inc, org 0xA000, all
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
  first reveal (japi_srand with japi_get_ticks, then japi_rand), excluding
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

- `tools/jopkg.py IN.bin -o OUT.jop` — package validator/stamper. Reads a
  NASM flat binary that already carries the §20.2 header; verifies magic,
  version, load offset 0xA000, entry range, image size field == actual
  file size, image+bss ≤ 0x5000, name printable ≤15 chars NUL-padded,
  flags bits 1–7 zero, and when flags bit 0 is set: image ≥ 96 and entry
  ≥ 0xA060; prints a one-line summary (name, entry, image/bss, icon
  yes/no) and copies to OUT.jop.
- `tools/jopdisk.py -o OUT.img --size {1440,360} [PKG.jop ...]` — builds a
  jopfs v2 floppy (§19): superblock geometry 18/2 or 9/2, directory
  entries named from each package's header name field, icon table LBA 3–6
  (entry i = package i's embedded icon bytes 32..95 when flags bit 0,
  else 64 zero bytes), data from LBA 7, sector-aligned. Zero packages is
  legal (an empty disk — useful for testing Refresh). Fails if >32 files
  or the disk overflows. Total image size: 1474560 or 368640 bytes.
- Makefile: `build/mines.bin` from `apps/mines/mines.asm` and
  `build/hello.bin` from `apps/hello/hello.asm` (§27), each
  (`nasm -f bin -w+error -I apps/`, dep on apps/jopapi.inc), packaged via
  jopkg.py, then `build/apps.img` (1440) + `build/apps360.img` (360) from
  **mines.jop + hello.jop** via jopdisk.py; all built by `all`.
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

Bitmaps are hand-authored `dw` rows (like the menu-bar apple, the one
sanctioned place for hand-made bitmap data — icons are the second).

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
| `desk_click` | in CX=x, DX=y (no lock held; called by ui.inc when wm_hit found no window). Zone hit: if same zone as desk_sel and [ui_click_t]−desk_clkt < 9 (birth ticks, §10) → clear the selection and call `files_open_drive` with AL = drive. Else select it, stamp desk_clkt. Miss: clear any selection. All its own drawing (selection flips) happens under gfx_lock/gfx_unlock acquired internally, redrawing only the affected zones — EXCEPT when a visible window overlaps a zone's drawn rect (x 582..633 with the label overhang, window rect incl. the 1px shadow): a partial redraw would paint desktop over that window, so the flip falls back to a full wm_paint_all under the same lock. |

Selection is purely visual bookkeeping; a window covering an icon simply
paints over it (desk_paint runs before windows in wm_paint_all), and
clicks over windows never reach desk_click.

## 27. HELLO — the second package (apps/hello/hello.asm)

Deliberately minimal, to prove the SDK surface and the no-icon fallback:
`JOP_HEADER 'HELLO', entry` (no icon flag — the Disk window must show
`ico_app16` for it), one window "Hello" 240×90 at (200,150), paint =
two centered lines: "Hello from a" / ".jop package!", no onkey, no
onclick, no bss. Entry: wm_create, return BX/CF. Prefix `hl_`.

## 28. taskmgr.inc — the Task Manager window

Built-in window "Task Manager", 176×162 at (250,100), record created
hidden by `tm_init` (from kmain, after loader_init). Label prefix `tm_`.
No onkey, no onclick. `tm_init`: reads total conventional RAM once via
int 12h (kmain runs on task 0, so §7's only-the-UI-task-calls-BIOS rule
holds), zeroes all module state including the history ring (.bss is not
zeroed at boot), creates the window (ptr in `tm_win`), and `task_spawn`s
`tm_task`, which takes the last free slot — the task table is then full:
UI, Clock, Bounce, TaskMgr.

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
appears honestly in the task list as TaskMgr.

**Sampling** (once per interval, after the spin phase):

- Under one `pushf`/`cli` … `popf`: snapshot `sch_cycles` (16 bytes) and
  the four T_STATE bytes. Per-task share: diff_i = new_i − old_i (32-bit,
  old ← new), total = Σ diff_i; normalize by shifting total and every diff
  right while total's high word is non-zero; **total = 0 → every share is
  0 (no DIV ever executes with a zero divisor)**; else
  share_i = diff_i·100/total (≤ 100 since diff_i ≤ total).
- Under one `pushf`/`cli` … `popf`: read `[ld_appwin]` and, when non-zero,
  the resident header's image-size and bss-size words at APP_LOAD_OFF+8
  and +10 (loader_run zeroes ld_appwin before overwriting the region, so a
  non-zero gate observed atomically with the reads implies an intact
  header). used bytes = `kernel_bss_end` (bare label = the kernel
  text+bss footprint, org 0) + image + bss. usedK = (used+1023) >> 10.
  Total KB is the boot-time int 12h value. **All bar math is in KB**:
  barw = usedK·160/totalK (`mul` then `div`; totalK cannot be 0 from
  int 12h, but a 0 check that skips the bar is required anyway).
- History: load% scaled to 0..40 (·40 then /100), stored at
  `tm_hist[tm_pos]`. The ring index IS the graph column — an oscilloscope
  sweep, no scrolling — then tm_pos advances mod 160.

**Drawing.** `tm_paint` (W_PAINT) is a bare, unconditional full redraw —
no lock, no visibility check (wm_paint_all calls it with the lock already
held, §11). tm_task's periodic path wraps its drawing Clock-style
(§14): gfx_lock, re-check visible + not `wm_obscured` under the lock (else
skip), and touches only what changed — the CPU text line, the new sweep
column plus an all-white gap column at the advanced tm_pos, the RAM line
and bar, and the four task rows. The full 160-column graph render happens
only in tm_paint, so the periodic lock hold stays small (Bounce-scale).
All drawing is self-backgrounding (each element white-fills its own rect
or paints both segments), so tm_paint needs no preceding content clear
beyond the one wm_paint_all already does.

**Content layout** (content-relative; content is 174×143):

- (6,4): `"CPU nnn%"` (white-fill (6,4)-(90,11) first; n right-aligned,
  space-padded, 0..100).
- Graph: 1px black frame (6,14)-(167,55); interior columns x = 7+i,
  i = 0..159, rows 15..54. Column value v (0..40): white vline rows
  15..54−v, then black vline rows 55−v..54 (v=0 → all white, v=40 → all
  black). The column at tm_pos draws all white (the sweep gap).
- (6,61): `"RAM uuuK/tttK"` (white-fill (6,61)-(167,68) first).
- RAM bar: 1px black frame (6,71)-(167,80); interior (7,72)-(166,79):
  black for barw pixels from the left, white for the remainder.
- (6,87): header `"#  TASK    ST   CPU"`.
- Task rows i = 0..3 at y = 97 + 11·i (white-fill (6,y)-(167,y+7) first),
  20 chars ('%' included; a free row omits it and is 19): slot digit,
  2 spaces, name left-justified in 7, space, state
  in 3, 2 spaces, share right-aligned in 3 + `'%'`. Names come from a
  fixed slot-indexed table {UI, Clock, Bounce, TaskMgr} — kmain's spawn
  order is deterministic and pinned here. State: tm_task's own slot shows
  `run` (it is running when it samples); otherwise T_STATE 1 → `rdy`,
  2 → `slp`; a free slot shows name `-`, state `---`, share ` --` with no
  `%`.

Menu/dispatch: see §12/§13 — Special gains "Task Manager" (CMD_TASKS = 7)
above "Restart" (CMD_REBOOT, renumbered to 8); dispatch wm_shows
`[tm_win]` under gfx_lock exactly like the §14 windows.
