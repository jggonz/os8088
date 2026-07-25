# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

jop: a Macintosh System 1-style GUI OS for the Intel 8086, written entirely in real-mode NASM assembly, booted from floppy. Pre-emptive multitasking, overlapping windows, serial mouse, and loadable software packages — all in 256KB of RAM.

**SPEC.md is the binding contract.** Every symbol name, register contract, constant, and data layout is pinned there. Update SPEC.md *before* changing any interface, not after.

## Commands

```
make          # build all four floppy images into build/
make run      # boot in QEMU with emulated serial mouse (1.44MB images)
make test     # boot headless with QMP socket at build/qmp.sock for scripted testing
make debug    # boot QEMU halted, waiting for gdb on :1234
make xt       # boot 360KB images on an emulated IBM PC/XT in 86Box
make clean
```

Requires `nasm`, `qemu-system-i386`, `python3`. No linker anywhere — everything is `nasm -f bin` flat binaries (deliberately, to avoid Apple's Mach-O-only toolchain).

There are no unit tests. Testing = boot `make test`, then drive it over QMP:

```
python3 tools/mouse.py build/qmp.sock click 180 150      # absolute mouse click
python3 tools/mouse.py build/qmp.sock press X Y / move / up   # for menus: press, drag, release
python3 tools/qmp.py build/qmp.sock 'sendkey h'
python3 tools/qmp.py build/qmp.sock 'screendump /abs/path/shot.ppm'
python3 tools/qmp.py build/qmp.sock 'quit'
```

Testing quirks (learned the hard way):
- Never inject raw HMP `mouse_move` — QEMU's msmouse backend truncates large deltas (big negative deltas flip positive). Always go through `tools/mouse.py`, which chunks moves to ≤60px and derives absolute position by pinning against the kernel's edge clamp.
- Menus need press/move/up sequences, not `click`.
- Small changes (e.g. one revealed 16px Minesweeper cell) are easy to misread as "nothing happened" in a full 640x480 screendump — crop and zoom before concluding a click was lost.
- Only QEMU is routinely verified. `vm/xt/86box.cfg` keys are best-effort guesses and 86Box rewrites its own preference keys on exit (harmless drift).

## Architecture

### Hard rules (from SPEC.md §1 — these break silently if violated)

- **8086 only.** `kernel.asm` opens with `cpu 8086` and the build uses `-w+error`, so NASM rejects anything newer: no `pusha`, no `push imm`, no `shl reg, imm` other than 1 (use CL), no `movzx`, no 32-bit registers.
- **Tiny model.** CS = DS = SS = 0x1000 for kernel *and* loaded programs; all inter-module calls are near. ES is scratch but must be restored unless documented.
- **Register discipline.** Every public routine preserves all registers except documented outputs. ISRs push DS/ES, load DS = KERNEL_SEG, `cld` before string ops. Critical sections use `pushf`/`cli` … `popf`, never `cli` … `sti`.
- **.bss discipline.** Large buffers go in `section .bss` (free on disk with `-f bin`). NASM section state persists across `%include`: any .inc that opens `section .bss` must switch back to `section .text` before it ends, or the next include's code silently lands in .bss. `-w+error` turns the tell-tale warning into a build failure.
- **Label hygiene.** One flat namespace; every module-internal label carries its module prefix (`vga_`, `mou_`, `sch_`, `wm_`, `menu_`, `ui_`, `dsk_`, `ld_`, `fm_`, `ico_`, `desk_`, …) or is a NASM local label.
- **Memory budget.** Kernel image + .bss must fit below offset 0xA000 (loaded programs occupy 0xA000..0xEFFF in the same segment); `kernel.asm` ends with a build-time assertion that fails the build if exceeded.

### Concurrency (SPEC.md §7 — the crux)

Pre-emptive round-robin scheduling: the int 08h PIT hook chains the BIOS tick, saves the register frame on the task stack, swaps SP, and irets into the next ready task. One drawing mutex (`gfx_lock`) guards all VGA access and hides the cursor; public drawing routines *assume* the caller holds it. Background tasks (Clock, Bounce) re-check window visibility *under* the lock. The mouse ISR draws the cursor itself only when the lock is free, deferring to the next unlock otherwise. Task switching pauses during floppy reads (the tick still runs — the motor needs it).

### Layout

- `boot/boot.asm` — 512-byte boot sector; geometry comes from `-DSPT`/`-DHEADS`, sector count from the measured kernel size (both injected by the Makefile).
- `kernel/kernel.asm` — constants, boot sequence, the jop API jump table at 1000:0010, `%include`s of all modules, final .bss and size assertion. Module ownership is the table in SPEC.md §4; each `.inc` owns one subsystem (vga12, font, mouse, sched, events, wm, menu, ui, apps, disk, loader, files, icons, desk, taskmgr).
- `kernel/video.inc`, `keyboard.inc`, `string.inc`, `gfx.inc` are dead — left in the tree but **no longer included** (relics of the pre-GUI text shell, as is `kernel-shell.asm.bak`).
- `apps/` — loadable packages. `jopapi.inc` is the SDK: `JOP_HEADER` emits the 32-byte package header, `JAPI_*` constants name jump-table entries, `JOP_IMAGE_END` seals size + bss. `mines/` (embedded icon) and `hello/` (proves the generic-icon fallback).
- `tools/` — host-side Python: `jopkg.py` (validates/stamps `.bin` → `.jop`), `jopdisk.py` (builds jopfs floppy images), `qmp.py` + `mouse.py` (test drivers).

### Software package pipeline

```
apps/mines/mines.asm --nasm--> build/mines.bin   (org 0xA000, .jop header baked in)
                    --jopkg.py--> build/mines.jop
build/*.jop        --jopdisk.py--> build/apps.img / apps360.img   (jopfs floppy, drive B:)
```

jopfs is a purpose-built read-only filesystem: superblock (magic `JOPFS2`) naming disk geometry, 32-entry directory, icon table at LBA 3–6, file data from LBA 7, sector-aligned. The kernel loads a package at 1000:A000, zeroes its bss, and calls its entry, which registers a window and returns; from then on its paint/key/click procs are ordinary near pointers called like any built-in window's. Loading another package replaces the resident one. **Directory order on the apps disk is pinned in the Makefile: mines first, hello second — tests rely on it.**

### Two geometries of everything

Every image is built twice: 1.44MB (18 spt, for QEMU) and 360KB (9 spt, for 86Box / a real XT). If you change the boot path or jopfs, check both.
