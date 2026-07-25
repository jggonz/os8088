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
pre-empted background task, updating live while the user types or drags), and
a Task Manager (live CPU sweep graph, memory bars, per-task CPU share — also
its own background task, §28).

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
   `wm_`, `menu_`, `ui_`, `app_`, `tm_`, `dsk_`/`disk_`, `ld_`/`loader_`,
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
| `kernel/taskman.inc`| Task Manager window + task: CPU meter, memory bars, task list (§28) |
| `kernel/disk.inc`   | BIOS int 13h floppy reads, jopfs mount + directory (§18–19) |
| `kernel/loader.inc` | package validation, load into APP region, launch (§21)  |
| `kernel/files.inc`  | Disk window: file list UI, selection, open, refresh (§22) |
| `kernel/icons.inc`  | 1-bit icon format, draw routine, built-in library (§25) |
| `kernel/desk.inc`   | desktop drive icons: detect, paint, click/open (§26)    |

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
- **Idle flag**: `sch_idle_flag` byte, declared in sched.inc's .bss. It is
  the one piece of scheduler state that a *task* owns: **ui.inc writes it,
  sched.inc only reads it** (from `sch_account`, §8). Non-zero means task 0
  is sitting in its idle poll — int 16h reporting no key and `evq_pop`
  reporting an empty queue — rather than doing work. Without it the CPU
  meter (§28) would read 100% forever, because task 0 is always runnable and
  the scheduler cannot tell a spin from a computation. The write points are
  pinned in §13; the flag is a plain byte and needs no guard (a byte store
  is atomic w.r.t. interrupts on the 8086, and a sample landing on either
  side of the store is charged to the interval it belongs to).

## 8. sched.inc — pre-emptive round-robin

- `MAX_TASKS equ 4`. Task 0 is the boot thread (becomes the UI task). Each
  task has a 1536-byte stack in .bss (`sch_stacks`).
- Task record (8 bytes): `T_STATE` (0 free, 1 ready, 2 sleeping), `T_SP`
  (saved SP), `T_WAKE` (tick count to wake at), `T_NAME`. `T_SIZE equ 8`.

```nasm
T_STATE equ 0    ; byte: 0 free, 1 ready, 2 sleeping
T_SP    equ 2    ; word: saved SP
T_WAKE  equ 4    ; word: [ticks] value to wake at
T_NAME  equ 6    ; word: near ptr to a NUL-terminated name, 0 = unnamed
T_SIZE  equ 8    ; record stride — UNCHANGED
```

  `T_NAME` claims the two former padding bytes, so `T_SIZE` stays 8 and every
  `mov cl,3` / `shl bx,cl` record-indexing idiom in the module remains valid.
  The name is display-only (§28 draws it); nothing in the scheduler reads it.
- Timer: hook int 08h. Handler: push registers + DS/ES, load DS=KERNEL_SEG,
  chain to the saved BIOS vector first (`pushf` + far call), then
  `inc word [ticks]`, wake sleepers whose `T_WAKE` has passed, and if
  `sch_lock` is clear, switch: with all GP regs + ES + DS on the current
  stack, store SP in the task record, pick the next ready task round-robin,
  load its SP, pop, `iret`. (The BIOS handler already sent EOI; do not send
  another.)
- `sched_init` — set up task 0 as current/ready, zero all CPU-accounting
  state (§8.1), store `sch_name_sys` into task 0's `T_NAME`, reprogram PIT
  channel 0 to mode 2 (§8.1), save the old int 08h vector, install the
  handler. The PIT reprogramming and the vector install happen inside the
  same `pushf`/`cli` window, PIT first.
- `task_spawn` — in: AX = entry point (near), **SI = near ptr to the task's
  name (0 = unnamed)**. Builds a fresh stack frame that `iret`s into the
  entry with IF=1, DS=ES=KERNEL_SEG. Out: CF set if no free slot; SI is an
  input, not an output, and is preserved like every other register.
  **Publication order matters**: the timer ISR is live during spawn, so the
  full frame must be built, T_SP stored **and `T_NAME` written** before the
  slot is marked ready — the `mov byte [T_STATE], 1` store must be the last
  write to the record (a byte store is atomic w.r.t. interrupts on the 8086),
  so a reader that sees a used slot always sees a valid name pointer. A
  task's entry routine must never `ret` (loop forever).
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
- `sched_unhook` — restore PIT channel 0 to **mode 3, divisor 0** (`0x36`,
  then two zero bytes) **before** restoring the original int 08h vector, then
  restore int 0Ch (calls `mouse_unhook` too) — used before reboot. Order is
  binding: the ROM expects the mode it programmed, and the vector must not go
  back to the BIOS while the counter is still in jop's mode.
- `sch_switch` calls `sch_account` (§8.1) as the **first** thing it does,
  before `mov [sch_cur], dl` changes the current task (parking SP first is
  fine). This is the scheduler's only accounting call site, which keeps the
  ISR's fast path untouched when `sch_lock` is held — that time is charged to
  the still-current task at the next switch, which is correct.

### 8.1 CPU accounting

The task manager (§28) needs sub-tick resolution, so `sched_init`
**reprograms PIT channel 0 to mode 2** (rate generator), binary, lo/hi
access, divisor 0 (= 65536):

```nasm
    mov al, 0x34                ; ch 0, lo/hi, mode 2, binary
    out 0x43, al
    jmp short $+2               ; ISA I/O settling delay
    xor al, al
    out 0x40, al                ; divisor lo
    jmp short $+2
    out 0x40, al                ; divisor hi -> 0 = 65536
```

The interrupt rate is unchanged (65536 input clocks, ~18.2 Hz — the BIOS's
own rate), so `[ticks]`, `task_sleep` and the BIOS tick chain all keep their
meaning. What changes is the *readable* counter: mode 2 counts a straight
65535→0 once per period, while the BIOS's mode 3 (square wave) decrements by
**2** and reloads **twice** per period, so a latched mode-3 count is
ambiguous — it cannot tell the first half-period from the second, and the
reconstructed timestamp would jump backwards half a period at a time. Mode 2
makes `65536 - count` an unambiguous "clocks elapsed since this tick".

State (.bss, all zeroed by `sched_init` — .bss is not zeroed by the loader):

```nasm
sch_cpu_lo:    resw MAX_TASKS   ; per-task accumulator, low word, PIT clocks
sch_cpu_hi:    resw MAX_TASKS   ; high word
sch_idle_lo:   resw 1           ; idle accumulator (task 0's poll loop)
sch_idle_hi:   resw 1
sch_last_lo:   resw 1           ; timestamp of the last accounting sample
sch_last_hi:   resw 1
sch_idle_flag: resb 1           ; written by ui.inc (§7, §13), read here
sch_snap_lo:   resw MAX_TASKS+1 ; sch_cpu_take snapshot: tasks 0..N-1, idle
sch_snap_hi:   resw MAX_TASKS+1
```

`sch_name_sys db 'System', 0` is **data, not .bss** — it lives in
`section .text` next to the code, and `sched_init` stores it into task 0's
`T_NAME`.

`sch_account` — module-internal; call **only with IF=0 and DS=KERNEL_SEG**;
preserves all registers (flags clobbered):

1. Latch and read channel 0: `mov al,0` / `out 0x43,al` / `in al,0x40` (low
   byte) / shuffle / `in al,0x40` (high byte) → the current 16-bit count.
2. Reconstruct an absolute 32-bit timestamp in PIT input clocks:
   `now = ([ticks] * 65536) + (65536 - count)`, i.e. high word = `[ticks]`,
   low word = `0 - count`. **Handle `count == 0` explicitly**: the low word
   is then 0 and the high word must be `[ticks] + 1`.
3. `delta = now - [sch_last]` (32-bit `sub`/`sbb`). If the result is
   **negative** (high word's sign bit set), the counter has wrapped but the
   timer ISR has not run yet (we sample with IF=0, so the pending interrupt
   is held off) and `[ticks]` is one behind the hardware. That is exactly one
   uncounted wrap, so **rebuild** the timestamp with `[ticks] + 1` and redo
   the subtract, then charge it normally. Do *not* simply discard the sample:
   `sch_last` would stay put and the **next** sample would charge the whole
   span — the discarded interval included — to whichever task is current by
   then, so one dropped sample after a long `sch_lock` section (a multi-sector
   `disk_read`) transfers seconds of one task's time to another.
   If the delta is *still* negative after the rebuild, the 32-bit timestamp
   itself has wrapped (`[ticks]` rolls over every 65536 ticks, ≈ 3.6 h of
   uptime). The elapsed time is then unknowable, so that one interval is
   dropped — but `sch_last` **is** resynced to `now`, because leaving it in
   the future would make every later delta negative too and freeze the
   accounting permanently.
4. Otherwise add the delta (`add` lo, `adc` hi) to the right accumulator and
   store `now` into `sch_last`:
   - `[sch_cur] == 0` **and** `[sch_idle_flag] != 0` → `sch_idle_lo/hi`
   - otherwise → `sch_cpu_lo/hi[ [sch_cur] ]`

Charging the *whole* interval to the current task is exact, not an
approximation: `sch_cur` only ever changes inside `sch_switch`, and
`sch_switch` samples before it changes it, so all time since the previous
sample belongs to the task that was current.

| symbol         | contract                                                   |
|----------------|-------------------------------------------------------------|
| `sch_cpu_take` | **Public, task level** (§28 calls it). In: nothing. Out: nothing. Clobbers: flags only — every register is preserved. `pushf`/`cli`, call `sch_account` (charging the in-flight interval), copy every accumulator into `sch_snap_lo/hi` — the MAX_TASKS task accumulators first, then the idle accumulator, in that order — zero the live accumulators, `popf`. The caller therefore reads a consistent set of deltas covering exactly the time since the previous take. |

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
(CMD_TASKS, §28) then "Restart" (CMD_REBOOT). (CMD_CLOSE/CMD_REBOOT were
renumbered when Disk and then Task Manager were inserted — cmd = menu base +
item index must still hold, so Task Manager being the Special menu's *first*
item is what forces CMD_TASKS=7 / CMD_REBOOT=8.)

The Special menu's row of the menu table therefore becomes two items:

```nasm
menu_s_tasks db 'Task Manager', 0
menu_items_special: dw menu_s_tasks
                    dw menu_s_restart
; title, items, count, cmd base, bar hit range [xl..xr], title x:
    dw menu_s_special, menu_items_special, 2, CMD_TASKS, 86, 165, 98
```

'Task Manager' (96px) becomes Special's widest item, so the dropped menu
spans x 86..197 (widest + 16px padding from the title's left edge) — still
far inside the 640px screen, no bar geometry changes.

| symbol          | contract                                                   |
|-----------------|-------------------------------------------------------------|
| `menu_draw_bar` | draw the bar + titles (gfx lock held by caller)             |
| `menu_track`    | in: CX = mousedown x. Runs the whole interaction while the button is held (caller holds gfx lock): highlight title (xor), drop the menu (gfx_save under it to SAVE_SEG:0), track item highlight following `mouse_y`, on release restore save-under + unhighlight; out AX = CMD_* or 0. Item cells are 16px tall, menu width = widest item + 16px padding. |

`menu_track` polls `mouse_btn`/`mouse_x`/`mouse_y` directly (the ISR keeps
them fresh; cursor stays hidden during tracking since the gfx lock is held —
acceptable, tracking feedback is the highlight).

## 13. ui.inc — the UI task (task 0)

Loop forever, with `mov byte [sch_idle_flag], 1` as the **first** statement
of the loop body (§7, §8.1) — the loop is presumed idle until it finds work:
1. Poll keyboard: int 16h AH=01; if a key, fetch (AH=00) and near-call the
   front window's W_ONKEY (if any) under gfx_lock. Clear the idle flag
   (`mov byte [sch_idle_flag], 0`) the instant int 16h AH=00 returns the
   key — **before** gfx_lock, since blocking on the lock is work, not idle.
2. `evq_pop`; when it returns an event (CF clear) clear the idle flag
   immediately, before any dispatch. On EVT_MDOWN at (x,y) — first store the
   event's EV_C into the public word `ui_click_t` (the click's birth tick;
   §22/§26 read it during dispatch):
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
   `[ld_pending]`, clear the idle flag (a disk load is the least idle thing
   the system does), call `loader_run`. This runs **outside** the gfx lock —
   loader_run manages its own locking.
4. `task_yield`.

Those three stores — after int 16h yields a key, after `evq_pop` yields an
event, and before `loader_run` — are the **only** places ui.inc clears
`sch_idle_flag`, and the top of the loop is the only place it sets it. The
consequence is the intended one: an empty keyboard poll plus an empty
`evq_pop` (the system's actual idle loop) is charged to idle, while menus,
drags, clicks, key handling and package loads are charged to task 0 as real
work.

Command dispatch: CMD_ABOUT/NOTE/CLOCK/BOUNCE → `wm_show` the corresponding
window (created at boot, initially hidden or shown per §15). CMD_TASKS →
`wm_show` `[tm_win]` (§28), handled exactly like the CMD_CLOCK case:
gfx_lock, wm_show, gfx_unlock. CMD_FILES → call `files_open` (§22; same
position in the dispatch flow as the wm_show cases — files_open does its own
locking). CMD_CLOSE → `wm_hide` frontmost. CMD_REBOOT → gfx_lock (never
released), `vga_text`, `sched_unhook`, `int 0x19`.

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
`mouse_init` → `desk_init` → `apps_init` → `tm_init` → `files_init` →
`loader_init` → gfx_lock → `wm_paint_all` → gfx_unlock → `cursor_show` →
jump into `ui_task` (task 0 never returns). `tm_init` (§28) must come after
`apps_init` — it spawns the last task and its int 12h call is the boot
thread's, not the task's (§7) — and before the first paint, so the Task
Manager's (hidden) window record exists when wm_paint_all first walks the
z-order. Include order: `taskman.inc` immediately after `apps.inc`, then
`disk.inc`/`loader.inc`/`files.inc`, then `icons.inc` and `desk.inc`.

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
10. Special → Task Manager opens a window whose CPU graph sweeps, whose
    percentage falls back toward idle when nothing is happening and climbs
    while a drag is held, whose memory bars show the reserved RAM and the
    kernel segment (the program band appearing when Minesweeper is
    loaded), and whose task list names System / Clock / Bounce / Tasks with
    live states and CPU shares.

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
0 ok, 1 disk error, 2 not a valid package, 3 too large, 4 entry aborted),
`ld_size` (word: bytes of the resident package, image + bss, 0 = none — read
by §28's memory bar).

**Invariant: `ld_size` is non-zero exactly when `ld_appwin` is non-zero.**
It is written in exactly the two places `ld_appwin` is: zeroed next to the
eviction in step 2 (and by `loader_init`), and set to header image size +
header bss size next to the store in step 6. Every failure path after
eviction leaves both at 0, and every failure path before eviction leaves the
previous program's pair untouched. Note that steps 5–6 consume the registers
holding the two sizes and the package entry is not required to preserve any
register, so compute image+bss into a scratch .bss word *before* calling the
entry and commit it alongside `ld_appwin`.

`loader_run` — in AX = directory index. UI task only, gfx lock not held on
entry. Steps:
1. Validate the entry: index < [disk_nfiles], type = 1, size ≤ APP_MAX_SIZE
   and non-zero → else status 2, step 7.
2. If `[ld_appwin]` non-zero: gfx_lock, `wm_destroy` it, gfx_unlock, zero
   it and `[ld_size]`. (Must happen before the read clobbers the region the
   old program's procs live in.)
3. `disk_read` the file — start LBA and ceil(size/512) sectors — into
   ES=KERNEL_SEG, BX=APP_LOAD_OFF. CF → status 1, step 7.
4. Validate the header (§20.2): magic, version, load offset, image+bss ≤
   APP_MAX_SIZE, entry in range → else status 2, step 7.
5. Zero bss-size bytes at load+image size.
6. Near-call the entry. CF → status 4. Else: store BX in `ld_appwin` and
   image+bss in `[ld_size]`, gfx_lock, `wm_show` BX, gfx_unlock, status 0.
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

## 28. taskman.inc — the Task Manager

Built-in window plus its own background task. Label prefix `tm_`. Included
from kernel.asm immediately after `apps.inc`; `kmain` calls `tm_init` right
after `apps_init` (§15). It is **not** a .jop package: it reads scheduler,
loader and kernel-image internals that the API table (§20.3) does not
export, and it must keep sampling whether or not a program is resident.

Window template (§11, 16-byte form): `dw 150, 100, 304, 242, tm_ttl,
tm_paint, 0, 0` with `tm_ttl db 'Task Manager', 0` — no onkey, no onclick.
The width is 304, not a rounder number, because every framed element ends at
content-relative +295 (`TM_RIGHT`): 304 is the width that mirrors the +6 left
margin on the right. Widening the window without moving `TM_RIGHT` reopens a
lopsided right gutter.
The window is created **hidden** and stays hidden at boot (do not set the
visible bit); it opens from Special → Task Manager (CMD_TASKS, §12/§13),
and its close box hides it like any other window.

### 28.1 Content layout

Content is 302 × 223 px and is white-filled by the frame painter before
W_PAINT runs (§11). All coordinates below are **relative to the content
origin** — `CX0` = W_X+1, `CY0` = W_Y+TITLE_H, both fetched with
`wm_content` — and **inclusive** on both ends. All text is CBLACK.

| element              | geometry (content-relative)                        |
|----------------------|-----------------------------------------------------|
| "CPU" label          | text at (+6, +4)                                    |
| CPU percent, e.g. "37%" | text at (+260, +4); erase rect (+260,+4)..(+295,+11) |
| graph frame          | `gfx_frame` (+6,+14)..(+295,+65)                    |
| graph interior       | columns x = +7..+294 (**288** columns), rows y = +15..+64 (**50** rows) |
| graph column i       | i = 0..287 at x = +7+i; a bar of height h (0..50) is a black `gfx_vline` from y = +64−h+1 down to +64. **h = 0 draws nothing.** |
| "Memory" label       | text at (+6, +72)                                   |
| RAM bar frame        | `gfx_frame` (+6,+84)..(+295,+97)                    |
| RAM bar interior     | x +7..+294 (**288** px), y +85..+96                 |
| RAM text line        | text at (+6, +101); erase rect (+6,+101)..(+295,+108) |
| kernel-segment bar frame | `gfx_frame` (+6,+114)..(+295,+127)              |
| kernel-segment interior  | x +7..+294 (**288** px), y +115..+126           |
| kernel-segment text  | text at (+6, +131); erase rect (+6,+131)..(+295,+138) |
| separator            | black `gfx_hline` x +6..+295 at y +146              |
| column headers       | y +152: "Task" at +6, "State" at +120, "CPU" at +180 |
| task row r           | r = 0..MAX_TASKS−1, top y = +166 + r×14              |
| … row text           | name at x +6, state at x +120, percent at x +180     |
| … row bar            | `gfx_frame` (+212, top)..(+295, top+8); interior x +213..+294 (**82** px), y top+1..top+7, filled black from the left for pct×82/100 px |
| rows erase rect      | (+6,+166)..(+295,+221)                              |

The 288-px interiors and the 50-row graph are not arbitrary: 288 columns =
one column per history slot (§28.3) and 50 rows makes `h = pct/2` exact, so
neither drawing path needs a division.

### 28.2 Routines

| symbol         | contract                                                   |
|----------------|-------------------------------------------------------------|
| `tm_init`      | In: nothing. Out: nothing. Preserves all registers. Zeroes/seeds module state **first** (.bss is not zeroed at boot, §1 — the history ring included), then `int 12h` → AX = KB of conventional memory into `tm_ram_kb` (a BIOS call from the boot thread before the task exists, which §7 permits — the *task* must never call the BIOS), `wm_create` from the template into `tm_win`, and `task_spawn` `tm_task` with SI = `tm_name` (`db 'Tasks', 0`, §8). |
| `tm_task`      | Background task, never returns. `.loop`: `mov ax, 9` / `task_sleep` (≈ twice a second), then (1) `tm_sample` **unconditionally** — visible or not — so the accumulators stay bounded and the history keeps flowing, the same discipline as the Clock task's time-keeping (§14); (2) `gfx_lock`; if W_FLAGS bit 1 is clear **or** `wm_obscured` returns CF=1, skip drawing; else `tm_draw_dyn`; always `gfx_unlock`. The visibility test is re-done **under the lock** (§7/§14). Never calls the BIOS. Never nests gfx_lock. |
| `tm_sample`    | Takes no lock and touches no screen state. `sch_cpu_take` (§8.1), then sums the MAX_TASKS+1 snapshot dwords into a 32-bit total; total = 0 → every percentage is 0 and the divisions are skipped. Per-task percentages go into `tm_pct_tab`; `tm_cpu` = 100 − idle percent. Finally advances the history ring (§28.3). |
| `tm_pct`       | Percent helper: given a 32-bit value and the 32-bit total, out AX = value×100/total, 0..100. On the 8086 without 32-bit registers: shift **both** dwords right by the same count `n` into single words, compute DX:AX = word×100, divide by the total word. A percentage is a ratio, so any `n` applied to both sides is exact at 1% resolution. `tm_sample` picks `n` and publishes it in `tm_shift`: **8** normally (`word = (hi & 0xFF) << 8 \| (lo >> 8)`, done with byte moves), **16** (`word = hi`) when the total's high byte is non-zero. See §28.3a — the sample interval is *not* bounded by the 0.5 s sleep. Guard a zero divisor; the quotient is ≤ 100 so DIV cannot fault. |
| `tm_paint`     | W_PAINT. In: SI = window ptr; content already white-filled, gfx lock held by the caller; clobbers nothing. Draws the complete static furniture (labels, all four frames, the separator, the column headers), then **every** history column from the ring except the two sweep-gap columns (left white), then the same dynamic content `tm_draw_dyn` draws. |
| `tm_draw_dyn`  | Lock held by the caller. Draws **only**: the newest graph column, the two-column sweep gap, the CPU percent string, both memory bars and their text lines, and the task-rows block — each preceded by the erase of its own rect. It must **not** redraw the whole graph. |
| `tm_u16`       | In: AX = value, DI = destination buffer. Out: DI advanced past the last digit; no NUL is written. Clobbers per §1 otherwise. Value 0 prints `"0"`. Every number in §28.4/§28.5 is built into `tm_strbuf` with it. |

### 28.3 History ring and the sweep

`TM_HIST_N equ 288` — one slot per graph column. `tm_sample` advances
`tm_hpos = (tm_hpos + 1) MOD TM_HIST_N` and stores `tm_hist[tm_hpos] =
tm_cpu / 2` (0..50, exact for the 50-row interior).

The graph is a **sweep**, not a scroll: the newest bar is written at the
column the write head has just reached, the two columns *ahead* of it are
blanked white to make the head visible, and everything behind it is left
alone until the head comes round again. A scrolling graph would have to
white-fill and redraw all 288 columns twice a second; on a 4.77MHz XT that
is the whole frame budget, and mode-12h column writes are the most expensive
thing the kernel does. The sweep costs three columns per update instead of
288 — the classic trade a period machine makes for a live graph.

Binding consequence: `tm_draw_dyn` touches columns `tm_hpos`,
`(tm_hpos+1) MOD 288` and `(tm_hpos+2) MOD 288` and no others — it
white-fills the newest column's interior first, then draws its black bar,
then white-fills the two gap columns. `tm_paint` redraws the whole ring
**except** those two gap columns, so a full repaint and an incremental
update agree pixel-for-pixel.

### 28.3a The sample interval is unbounded

`tm_task` sleeps 9 ticks per pass, but the *interval between two
`sch_cpu_take` calls* is that sleep **plus** however long the pass blocked in
`gfx_lock` — and `menu_track` (§12) holds the gfx lock for the entire time a
pull-down is held open, while `disk_read` (§18) holds `sch_lock` across its
int 13h retries and so stops task switching altogether. Both are unbounded by
user or hardware behaviour, exactly as apps.inc already documents for the
Clock task's tick arithmetic.

Consequence, and it is binding: **a long interval must be rescaled, never
discarded.** Past 2²⁴ PIT clocks (14.06 s) a `>> 8` total no longer fits a
word, which is why `tm_shift` exists. Reporting 0% on overflow would be worse
than reporting nothing — it would claim an idle machine for precisely the
interval in which every task was spinning on the lock, and burn that false
zero into the history ring for a full 2.4-minute sweep. The only case that
still reports all-zero is a total under 256 clocks, i.e. nothing measured at
all.

### 28.4 Memory accounting

`tm_ram_kb` is the int 12h figure (typically 640).

```nasm
TM_RESERVED_KB equ 192      ; 64K below the kernel segment (IVT, BDA, boot)
                            ; + the 64K kernel segment (code, .bss, task
                            ;   stacks, the loaded-program region, task 0's
                            ;   stack at 0xFFFE)
                            ; + the 64K SAVE_SEG save-under heap
```

- **RAM bar** — gray dither (`gfx_fill_gray`) for the reserved fraction,
  `TM_RESERVED_KB × 288 / tm_ram_kb` pixels (mul then div, so the
  intermediate lives in DX:AX), white for the rest.
- **RAM text** — built at runtime from `tm_ram_kb`:
  `"RAM 640K  used 192K  free 448K"`.
- **Kernel-segment bar** — the whole 64K segment, at 288 px: gray dither for
  the kernel image + .bss, **solid black** for the resident package, white
  for the rest. Kernel bytes come from the `kernel_bss_end` label (§15) — a
  forward reference from an earlier include, which assembles fine because it
  is an ordinary relocatable symbol in an instruction operand (it may not
  appear in a `%if`). Package bytes are `[ld_size]` (§21). Pixels =
  `bytes × 288 / 65536`, computed as `(bytes >> 8) × 288 / 256` so every
  intermediate stays 16-bit — **with a floor of 1 px for any non-zero byte
  count.** The shift-first scaling otherwise annihilates a package under 256
  bytes (HELLO.JOP is 151), so the bar would show no black while the text
  line beside it reads `"program 1K"`, `tm_kb` having rounded up.
- **Kernel-segment text** — `"kernel 21K  program 0K  free 43K"`, each
  figure rounded **up** to whole KB, `free = 64 − kernel − program` clamped
  at 0.

### 28.5 Task rows

Walk `sch_tasks` (§8), skip records with `T_STATE` = 0 (free) and pack the
used ones upward so the list has no gaps; row r therefore shows the r-th
*used* slot, not slot r.

- **Name** — `[T_NAME]`; a `"(task N)"`-style fallback is drawn only when
  the pointer is 0. Never dereference a null.
- **State** — `"Run"` when the slot index equals `[sch_cur]`, else
  `"Ready"` for T_STATE = 1 and `"Sleep"` for T_STATE = 2.
- **CPU** — the slot's `tm_pct_tab` entry followed by `"%"`, and the same
  percentage as a black bar in the row's 82-px gauge.

The idle share is deliberately **not** a row: it is already the difference
between `tm_cpu` and 100%, and showing it twice would imply a fifth task.

### 28.6 State (.bss, `section .text` restored at the end of the file)

`tm_win` (word), `tm_ram_kb` (word), `tm_hpos` (word), `tm_cpu` (byte or
word), `tm_pct_tab` (`resb MAX_TASKS`), `tm_hist` (`resb TM_HIST_N`),
`tm_strbuf` (`resb 48`), `tm_shift` (byte, §28.3a), plus whatever scratch the
drawing paths need (content origin, computed pixel counts). Nothing here is
touched by an ISR,
so no store needs a guard; `tm_init` seeds all of it.
