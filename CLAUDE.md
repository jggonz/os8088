# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

os8088: a Macintosh System 1-style GUI OS for the Intel 8086, written entirely in real-mode NASM assembly, booted from floppy. Pre-emptive multitasking, overlapping windows, serial mouse, a bottom dock, and loadable software packages that run as closable, multi-instance apps — all in 256KB of RAM.

**SPEC.md is the binding contract.** Every symbol name, register contract, constant, and data layout is pinned there. Update SPEC.md *before* changing any interface, not after.

## Commands

```
make          # build all four floppy images into build/
make run      # boot in QEMU with emulated serial mouse (1.44MB images)
make run-640  # same, as a maxed-out 640KB machine (-m 1M; QEMU/SeaBIOS can't boot below 1MB, int 12h caps at 640K anyway; SeaBIOS's EBDA makes it 639K)
make test     # boot headless with QMP socket at build/qmp.sock for scripted testing
make test-snd # make test + PC speaker captured to build/snd.wav; verify with
              # tools/sndcheck.py (note: the wav holds speaker-ON time only, not
              # wall time - a silent boot yields an empty capture, and QEMU leaves
              # the RIFF sizes zeroed, which sndcheck.py absorbs)
make debug    # boot QEMU halted, waiting for gdb on :1234
make xt       # boot 360KB images on an emulated IBM PC/XT in 86Box
make xt-640   # same XT with a full 640KB RAM (vm/xt640/86box.cfg)
make clean
```

Requires `nasm`, `qemu-system-i386`, `python3`. No linker anywhere — everything is `nasm -f bin` flat binaries (deliberately, to avoid Apple's Mach-O-only toolchain).

There are no unit tests. Testing = boot `make test`, then drive it over QMP:

```
python3 tools/mouse.py build/qmp.sock click 180 150      # absolute mouse click
python3 tools/mouse.py build/qmp.sock to X Y / down / up      # for menus: position, press, drag (`to` while held), release
python3 tools/qmp.py build/qmp.sock 'sendkey h'
python3 tools/qmp.py build/qmp.sock 'screendump /abs/path/shot.ppm'
python3 tools/qmp.py build/qmp.sock 'quit'
```

Testing quirks (learned the hard way):
- Never inject raw HMP `mouse_move` — QEMU's msmouse backend truncates large deltas (big negative deltas flip positive). Always go through `tools/mouse.py`, which chunks moves to ≤60px and derives absolute position by pinning against the kernel's edge clamp.
- Menus need press/move/up sequences (`mouse.py down` / `to` / `up`), not `click`.
- Double-clicks compare birth ticks with a 9-tick (~0.5s) window: two separate `mouse.py click` invocations are too slow. Position with `mouse.py to X Y`, then send both clicks over one QMP connection: `qmp.py build/qmp.sock 'mouse_button 1' 'sleep 0.08' 'mouse_button 0' 'sleep 0.12' 'mouse_button 1' 'sleep 0.08' 'mouse_button 0'`.
- Small changes (e.g. one revealed 16px Minesweeper cell) are easy to misread as "nothing happened" in a full 640x480 screendump — crop and zoom before concluding a click was lost.
- `mouse.py down X Y` / `up X Y` now goto-then-press (any other argument shape errors out); bare `down`/`up` still act at the CURRENT cursor position — historically `down X Y` silently ignored the coordinates, a footgun that read as a kernel bug.
- Unpaced `mouse_move`/`mouse_button` sequences over one QMP connection outrun the 1200-baud msmouse: the button packet is processed at a stale position and drags silently do nothing — interleave `'sleep 0.1'` (or more) between moves and presses.
- Run `tools/sndcheck.py` only after QMP `quit` — a still-running QEMU's wav capture is partial and under-reports duration (and quitting with an SB stream underrun-paused flushes a residual ~20 ms blip at the file's very end; see docs/SOUND-PLAN.md Phase 4).
- Only QEMU is routinely verified. `vm/xt/86box.cfg` keys are best-effort guesses and 86Box rewrites its own preference keys on exit (harmless drift — except that it silently clamps `mem_size` to the machine's maximum: `ibmxt` caps at 256K, which is why `vm/xt640` uses `ibmxt86`, the 1986 board revision).

## Architecture

### Hard rules (from SPEC.md §1 — these break silently if violated)

- **8086 only.** `kernel.asm` opens with `cpu 8086` and the build uses `-w+error`, so NASM rejects anything newer: no `pusha`, no `push imm`, no `shl reg, imm` other than 1 (use CL), no `movzx`, no 32-bit registers.
- **Near model.** CS = DS = 0x1000 for kernel *and* loaded programs; **SS = 0x0800** (`LOW_SEG`), because every task stack lives outside the kernel segment. Inter-module calls inside `.text` are near; `.fartext` modules go through the shims in SPEC.md §33. ES is scratch but must be restored unless documented. **SS ≠ DS means `[bp+disp]` addresses SS** — code holding a kernel pointer in BP needs `[ds:bp+…]`.
- **Register discipline.** Every public routine preserves all registers except documented outputs. ISRs push DS/ES, load DS = KERNEL_SEG, `cld` before string ops. Critical sections use `pushf`/`cli` … `popf`, never `cli` … `sti`.
- **Section discipline.** Four sections, all declared with their attributes at the top of `kernel.asm`; modules switch with a bare `section <name>` and **must switch back to `section .text` before the file ends**, or the next include's code silently lands in the wrong one. `-w+error` turns the tell-tale warning into a build failure.
  - `.text` — kernel image, `KERNEL_SEG`.
  - `.bss` — kernel scratch. Free on disk with `-f bin`.
  - `.lowbss` — scratch in `LOW_SEG` (linear 0x08000): task stacks + disk buffers. Reached through SS or ES, **never DS** (SPEC.md §2.1).
  - `.fartext` — far code, copied to `FAR_SEG` (linear 0x00600) by `far_init`. Costs the kernel window nothing (SPEC.md §33).
- **Label hygiene.** One flat namespace; every module-internal label carries its module prefix (`vga_`, `mou_`, `sch_`, `wm_`, `inst_`, `menu_`, `ui_`, `dsk_`, `ld_`, `fm_`, `ico_`, `desk_`, `dock_`, …) or is a NASM local label.
- **Memory budget.** Kernel image + .bss must fit below `APP_LOAD_OFF` = 0xB000 (loaded programs occupy 0xB000..0xFDFF in the same segment); `kernel.asm` ends with four build-time assertions that fail the build if exceeded (SPEC.md §15.1). Headroom is ~28KB — measure with the recipe in SPEC.md §15.1 before assuming otherwise. `docs/MEMORY-PLAN.md` is the standing plan for where more space comes from, including the one step not yet taken (packages into their own segments).

### Concurrency (SPEC.md §7 — the crux)

Pre-emptive round-robin scheduling: the int 08h PIT hook chains the BIOS tick, saves the register frame on the task stack, swaps SP, and irets into the next ready task. Tasks are dynamic (MAX_TASKS=12): `task_spawn` takes an argument word (delivered in the task's DX) and returns the slot; a task terminates only via `task_exit` (self-exit; usually through `inst_task_die`), which frees the task slot and the instance record inside one IF=0 window. One drawing mutex (`gfx_lock`) guards all VGA access and hides the cursor; public drawing routines *assume* the caller holds it. Background tasks (Clock, Bounce instances) re-check window visibility *under* the lock. The mouse ISR draws the cursor itself only when the lock is free, deferring to the next unlock otherwise. Task switching pauses during floppy reads (the tick still runs — the motor needs it).

### Double buffering (SPEC.md §32 — conditional)

**Off by default, switched at runtime.** `bb_init` only probes int 12h and sets `bb_avail` if conventional RAM ≥ 500KB (500 not 512, so a real 512KB machine still qualifies after the BIOS takes its cut). `bb_on` starts 0, so every machine boots drawing straight to VRAM; the Control Panel's **Display** page (SPEC.md §31.3) flips it via `bb_set`, which seeds the buffer from VRAM (`bb_sync`, GC4 Read Map Select per plane) on the way in and flushes it on the way out. While on, every `gfx_*`/font/icon draw renders into a 4-plane back buffer at segment 0x4000 (`kernel/vgabb.inc`, software or/and/xor — RAM has no VGA latches) and `gfx_unlock` flushes the dirty rect to VRAM before the cursor reappears; `menu_track` flushes once for the pull-down because it draws while holding the lock. Below the floor `bb_avail` stays 0, the page says so and refuses the click, and a 256KB machine can never leave the VRAM path.

Two things keep it affordable, because the flush (VRAM) costs ~24× the render (RAM):

- **`[bb_mono]`** — all four planes hold identical bytes as long as everything is drawn in colour 0 or 15, which is the whole UI (its greys are 0/15 dither). While set, the flush copies *one* plane with Map Mask = 0Fh and the hardware fans it out: a quarter of the VRAM writes, and no mid-flush colour fringing. `bb_mono_chk` retires it one-way on the first other colour (a Minesweeper digit); the planes are always fully rendered, so the flush just reverts to four passes. It hangs off `gfx_fill`/`font_char` ahead of the `bb_on` dispatch, so it tracks colour even while buffering is off — `bb_set` can arm the buffer at any time and seeds it from VRAM.
- **Transient overlays never enter the back buffer.** The drag outline and the menu highlights are XOR overlays drawn and erased inside one held lock — the cursor's contract — so they call `vga_xor_rect_vram`/`vga_xor_fill_vram` direct, like the cursor calls `vga_save_vram`. Routed through the buffer, a 1px outline dirtied the whole window rect and flushed it twice per drag pass. The public `gfx_xor_*` still dispatch to the buffer: packages reach them through the API table and their output is persistent.

### Instances (SPEC.md §29 — how apps live and die)

Everything running — built-in kind or loaded package — is a record in `kernel/instance.inc`'s `inst_tab` (12 × 32B). Boot is clean (no instances); menus call `app_launch` (new instance, or front the existing one at the kind's cap), the close box calls `app_close_win` (task-less: synchronous teardown; task-owned: die flag `I_STATE=2` + hide, the task tears down at next wake), and the title bar's right-hand minimize box hides to the dock (`kernel/dock.inc`, bottom strip rows 456..479, one tile per live instance, stable slot↔tile mapping, XOR-inverted when minimized). `wm_owner[]` maps window slot → instance. The Task Manager lists *instances*, not tasks — one row per `inst_tab` slot plus a "System" row — because task-less apps (About, Disk, every package) only ever run inside window callbacks. Those callbacks are timed at the `W_PAINT`/`W_ONKEY`/`W_ONCLICK` dispatch sites and billed to `I_CYC` via `task_cycles`/`task_debit`, which *move* the cycles off the running task so the rows still add to one total.

### Where the memory went (SPEC.md §2.1/§33, `docs/MEMORY-PLAN.md`)

The kernel's 64KB segment is not all the kernel's. Three moves bought it room:

- **Task stacks and disk buffers left the segment** into `LOW_SEG` (linear 0x08000), 20KB of `.lowbss`. Consequence: SS ≠ DS. The disk buffers are read only through `dsk_get_dir`/`dsk_get_icon`, which stage one entry back into the kernel segment so every consumer keeps a plain DS:SI pointer.
- **The package pool slid up** to 0xB000..0xFDFF, handing the kernel the 4KB task 0's stack used to hold. `APP_LOAD_OFF`/`APP_MAX_SIZE` are mirrored in `kernel/kernel.asm`, `apps/os88api.inc`, `tools/os88pkg.py` and the Makefile's `-DOS88_ORG` probe org — change them together, and rebuild every `.o88` (the link base is in the header).
- **Cold modules moved to `.fartext`** (`ctrl.inc`, `taskmgr.inc`), copied to `FAR_SEG` at boot by `far_init`. The blob rides at the tail of the kernel image and lands *on top of* `.bss` — which is exactly why `far_init` is kmain's first act and why `splash.inc` has always kept its state in `.text`. Far code keeps DS = `KERNEL_SEG`, so **all its data stays in `.text`**; only code moves. It reaches the kernel via `KCALL`/`FARK` and is reached via `FARSHIM`. The migration recipe is in `docs/MEMORY-PLAN.md`.

### Layout

- `boot/boot.asm` — 512-byte boot sector; geometry comes from `-DSPT`/`-DHEADS`, sector count from the measured kernel size (both injected by the Makefile).
- `kernel/kernel.asm` — constants, boot sequence, the os8088 API jump table at 1000:0010, `%include`s of all modules, final .bss and size assertion. Module ownership is the table in SPEC.md §4; each `.inc` owns one subsystem (farcall, vga12, font, mouse, sched, events, wm, instance, menu, ui, apps, disk, loader, files, icons, desk, dock, taskmgr, ctrl).
- `kernel/video.inc`, `keyboard.inc`, `string.inc`, `gfx.inc` are dead — left in the tree but **no longer included** (relics of the pre-GUI text shell, as is `kernel-shell.asm.bak`).
- `apps/` — loadable packages. `os88api.inc` is the SDK: `OS88_HEADER` emits the 32-byte package header, `OSAPI_*` constants name jump-table entries, `OS88_IMAGE_END` seals size + bss. `mines/` (embedded icon), `hello/` (proves the generic-icon fallback) and `notepad/` (the former built-in Note Pad kind, moved out to reclaim ~1.4KB of kernel budget — its per-instance bss replaced the fixed 2-instance pool, so the cap is gone).
- `tools/` — host-side Python: `os88pkg.py` (validates/stamps `.bin` → `.o88`), `os88disk.py` (builds os88fs floppy images), `qmp.py` + `mouse.py` (test drivers).

### Software package pipeline

```
apps/mines/mines.asm --nasm x2--> build/mines.bin (org 0xB000) + build/mines.alt.bin (org 0xB800)
                    --os88pkg.py--> build/mines.o88   (v2: reloc table diffed from the pair)
build/*.o88        --os88disk.py--> build/apps.img / apps360.img   (os88fs floppy, drive B:)
```

os88fs is a purpose-built read-only filesystem: superblock (magic `OS88FS2`) naming disk geometry, 32-entry directory, icon table at LBA 3–6, file data from LBA 7, sector-aligned. Packages are format v2 (SPEC.md §20.2): assembled at the 0xB000 link base, shipped with a relocation table that os88pkg.py generates by diffing the dual assembly (class 0 = package-address words, +delta at load; class 1 = `call OSAPI_*` rel16 displacements, −delta). The kernel allocates a region from the 0xB000..0xFDFF pool (first-fit; occupancy derived from the instance table), reads, relocates, zeroes bss, and calls the entry, which registers a window and returns; from then on its paint/key/click procs are ordinary near pointers called like any built-in window's. **Multiple packages — or multiple instances of one — run at once**; closing one frees its region. One binding author rule: package addresses only as whole 16-bit words (os88pkg's reconstruction check fails the build otherwise). **Directory order on the apps disk is pinned in the Makefile: mines first, hello second — tests rely on it; notepad is appended third so those indices hold.**

### Two geometries of everything

Every image is built twice: 1.44MB (18 spt, for QEMU) and 360KB (9 spt, for 86Box / a real XT). If you change the boot path or os88fs, check both.
