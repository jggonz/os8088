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
   `wm_`, `menu_`, `ui_`, `app_`) or use NASM local labels (`.foo`).
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
| 0x20000       | 0x2000  | `SAVE_SEG` — save-under heap (menus), raw, via ES  |
| 0xA0000       | 0xA000  | VGA planar framebuffer, 80 bytes/row               |

Kernel image + .bss must fit below offset 0xF000 (task stacks included);
`kernel.asm` ends with a build-time assertion (§15). Works in 256KB of RAM.

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
```

## 4. Module files and ownership

| file                | owns                                                    |
|---------------------|---------------------------------------------------------|
| `kernel/kernel.asm` | entry, constants, init order, includes, .bss layout, syscall gate (kept as-is for compat, at 0x0010) |
| `kernel/vga12.inc`  | mode set, planar primitives, save/restore, gfx lock     |
| `kernel/font.inc`   | 8x8 font (copied from VGA BIOS ROM at init), text draw  |
| `kernel/mouse.inc`  | COM1 UART, IRQ4 ISR, packet decode, cursor (save-under) |
| `kernel/sched.inc`  | PIT hook, context switch, task table, spawn/yield/sleep |
| `kernel/events.inc` | 8-byte event records, system event ring queue           |
| `kernel/wm.inc`     | window records, z-order, frames, hit test, paint-all    |
| `kernel/menu.inc`   | menu bar, pull-down tracking, command return            |
| `kernel/ui.inc`     | UI task: event pump, keyboard poll, drag, dispatch      |
| `kernel/apps.inc`   | About, Note Pad, Clock task, Bounce task                |

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
  `mouse_unhook` too) — used before reboot.

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
  EVT_MUP with a=x, b=y. Move the cursor per §7 (draw only when
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
EVT_MDOWN equ 1     ; a=x, b=y
EVT_MUP   equ 2     ; a=x, b=y
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
WIN_SIZE equ 16
MAX_WIN  equ 6
```

Storage: `wm_wins` (MAX_WIN × WIN_SIZE, .bss), z-order byte array
`wm_zord` (window indices, index 0 = backmost) + `wm_zn` count. Every used
window is always present in `wm_zord` — membership is established at
wm_create and never removed; `wm_hide` clears only the visible bit, and
`wm_show`/`wm_front` only reorder. The visible flag alone decides whether a
window is painted, hit-tested, or counted by `wm_top`/`wm_obscured`.

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
| `wm_create`    | in SI → 14-byte template {x,y,w,h,title,paint,onkey} words; out BX = window ptr, CF on table full. Created **hidden**; appends the window's index to `wm_zord` (frontmost) and increments `wm_zn`. |
| `wm_show`      | in BX = win ptr: set visible, bring to front, repaint all    |
| `wm_hide`      | in BX = win ptr: clear visible, repaint all                  |
| `wm_front`     | in BX = win ptr: raise to front of z-order, repaint all      |
| `wm_top`       | out BX = frontmost visible window ptr, 0 if none             |
| `wm_hit`       | in CX=x, DX=y; out BX = topmost visible window ptr containing the point (0 if none), AL = 0 content, 1 title bar, 2 close box. AL=2 only when BX is the frontmost visible window (the only one with a close box drawn); on any other window that region reports AL=1. |
| `wm_paint_all` | full repaint: desktop gray (below menu bar), menu bar, every visible window back→front (frame + white content + W_PAINT). Caller holds gfx lock. |
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
CMD_CLOSE  equ 5
CMD_REBOOT equ 6
```

Menus: **Apple**: "About jop..." (CMD_ABOUT). **File**: "Note Pad"
(CMD_NOTE), "Clock" (CMD_CLOCK), "Bounce" (CMD_BOUNCE), "Close Window"
(CMD_CLOSE). **Special**: "Restart" (CMD_REBOOT).

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
2. `evq_pop`; on EVT_MDOWN at (x,y):
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
   - content of front window → (v1: ignore; apps are keyboard/task driven).
3. `task_yield`.

Command dispatch: CMD_ABOUT/NOTE/CLOCK/BOUNCE → `wm_show` the corresponding
window (created at boot, initially hidden or shown per §15). CMD_CLOSE →
`wm_hide` frontmost. CMD_REBOOT → gfx_lock (never released), `vga_text`,
`sched_unhook`, `int 0x19`.

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

Keep the 0x0000 cold entry and the 0x0010 syscall gate (existing dispatcher
and its little helpers may be dropped; the gate may simply `retf` for now —
offset layout preserved). `cpu 8086` + `bits 16` + `org 0`.

kmain: set segments/stack (SP=0xFFFE), `sti`, `cld`, then:
`sched_init` → `evq_init` → `vga_mode12` → `font_init` → `wm_init` →
`mouse_init` → `apps_init` → gfx_lock → `wm_paint_all` → gfx_unlock →
`cursor_show` → jump into `ui_task` (task 0 never returns).

End of file (after all `%include` lines, with `section .text` in effect):

```nasm
kernel_text_end:
KTEXT_SIZE equ kernel_text_end - $$

section .bss
; (modules already declared their own .bss blocks inside their .inc files;
;  NASM accumulates them in declaration order — this block lands last)
kernel_bss_end:
KBSS_SIZE equ kernel_bss_end - $$
%if KTEXT_SIZE + KBSS_SIZE > 0xF000
%error "kernel too big: image + bss must stay below 0xF000"
%endif
```

(The sizes must be same-section label differences bound via `equ` — a bare
label in `%if` is a non-scalar and will not assemble. Keep this block last.)

## 16. Build & test

- Makefile: unchanged image recipes. `run` target gains the serial mouse:
  `-chardev msmouse,id=m0 -serial chardev:m0`.
  New `test` target: same plus `-display none -qmp unix:build/qmp.sock,server,nowait`
  run in background, for scripted screendumps and input injection.
- 86Box config (`vm/xt/86box.cfg`): set the mouse to a serial Microsoft
  mouse on COM1 (best-effort; cannot be verified headless).
- The kernel may exceed 8 sectors; the two images are already built
  separately with correct geometry, and the boot sector's LBA→CHS handles
  cylinders > 0. Nothing to change in boot.asm.

## 17. Definition of done

1. `make` builds both images with zero warnings; kernel < 0xF000 with .bss.
2. QEMU boots to: gray desktop, menu bar (apple + File + Special), Note Pad
   + Clock + Bounce windows open, arrow cursor.
3. Clock ticks and ball bounces **while** typing in Note Pad and while a
   drag outline is being moved (pre-emption visibly working).
4. Mouse moves cursor; windows drag by title bar; clicking a back window
   raises it; close box hides; menus pull down and dispatch.
5. Only 8086 instructions (`cpu 8086` proves it at build time).
