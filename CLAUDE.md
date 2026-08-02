# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

os8088: a Macintosh System 1-style GUI OS for the Intel 8086, written entirely in real-mode NASM assembly, booted from floppy. Pre-emptive multitasking, overlapping windows, serial mouse, a bottom dock, and loadable software packages that run as closable, multi-instance apps — all in 256KB of RAM. One binary drives VGA, Hercules or CGA, picked at boot.

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
make xt-cga      # XT + real CGA card, 256KB (vm/xt-cga)
make xt-hercules # XT + real Hercules card, 256KB (vm/xt-hercules)
make clean
```

Two build knobs exist only for testing the video fallbacks (SPEC.md §39.9):

```
make test VIDEO=cga                    # force the CGA path on a VGA machine
make test VIDEO=herc HERCSEG=0x7000    # force Hercules, framebuffer in RAM
python3 tools/hercshot.py build/qmp.sock 0x70000 out.png
```

`VIDEO=` is tracked by a stamp file, so changing it rebuilds the kernel — without that,
make sees an up-to-date `kernel.bin`, boots the previous adapter, and it reads exactly
like the probe being broken.

Requires `nasm`, `qemu-system-i386`, `python3`. No linker anywhere — everything is `nasm -f bin` flat binaries (deliberately, to avoid Apple's Mach-O-only toolchain).

There are no unit tests. Testing = boot `make test`, then drive it over QMP:

```
python3 tools/mouse.py build/qmp.sock click 180 150      # absolute mouse click
python3 tools/mouse.py build/qmp.sock to X Y / down / up      # for menus: position, press, drag (`to` while held), release
python3 tools/mouse.py --screen 640x200 build/qmp.sock ...    # MUST match the adapter (SPEC.md §39): the harness
                                                              # pins against the kernel's own edge clamp
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
- `mouse.py`'s derived absolute position can be 1–2px off after a run of moves. On narrow targets (the Disk window's 14px scroll bar) a click can silently land just outside the rect — aim at the visual center of the glyph, and when a click "does nothing", screendump and check where the drawn cursor actually sits before suspecting the hit-test.
- Run `tools/sndcheck.py` only after QMP `quit` — a still-running QEMU's wav capture is partial and under-reports duration (and quitting with an SB stream underrun-paused flushes a residual ~20 ms blip at the file's very end; see docs/SOUND-PLAN.md Phase 4).
- **QEMU emulates no CGA and no Hercules card** — only VGA-class devices. `make test VIDEO=cga` works because SeaVGABIOS's `int 10h AX=0006h` is a byte-exact CGA framebuffer, but it never exercises the detection probe. Hercules has no automatable path at all: `HERCSEG` + `tools/hercshot.py` verifies its pixels out of RAM, and `make xt-hercules` is the only real test.
- `tools/mouse.py` paces its moves explicitly (one connection, `sleep` between packets) because the msmouse backend runs at 1200 baud and drops a move whose predecessor is still in flight. On a fast host the old one-process-per-move spacing was not enough, and the symptom is a cursor that never moves while every screendump still looks plausible.
- Only QEMU is routinely verified. `vm/xt/86box.cfg` keys are best-effort guesses and 86Box rewrites its own preference keys on exit (harmless drift — except that it silently clamps `mem_size` to the machine's maximum: `ibmxt` caps at 256K, which is why `vm/xt640` uses `ibmxt86`, the 1986 board revision).
- 86Box's `wp://` prefix on an `fdd_0N_fn` path mounts that floppy **write-protected**, and int 13h then answers status 03h — which the OS faithfully reports as "Write protected" (`FERR_WPROT`). The data floppy carried `wp://` from the read-only-filesystem era and had to lose it before SPEC.md §18.4 writes could work on the XT; the **boot** floppy keeps it deliberately. If saving to B: starts failing on 86Box again, check this before suspecting `diskw.inc` — 86Box may have rewritten the key on exit.

## Architecture

### Hard rules (from SPEC.md §1 — these break silently if violated)

- **Three video adapters, one binary (SPEC.md §39).** VGA 640x480x16 planar, else
  Hercules 720x348 mono, else CGA 640x200 mono, probed at boot by `kernel/viddet.inc`.
  **`SCREEN_W`/`SCREEN_H`/`ROW_BYTES` are VGA reference values, not the truth** — the live
  screen is `[vid_w]`/`[vid_h]`/`[vid_stride]` and the derived words in §39.2. New code that
  clips, centres or anchors to a screen edge must read those, or it is wrong on two adapters
  out of three.
- **8086 only.** `kernel.asm` opens with `cpu 8086` and the build uses `-w+error`, so NASM rejects anything newer: no `pusha`, no `push imm`, no `shl reg, imm` other than 1 (use CL), no `movzx`, no 32-bit registers.
- **Near model.** CS = DS = 0x1000 for kernel *and* loaded programs; **SS = 0x0800** (`LOW_SEG`), because every task stack lives outside the kernel segment. Inter-module calls inside `.text` are near; `.fartext` modules go through the shims in SPEC.md §33. ES is scratch but must be restored unless documented. **SS ≠ DS means `[bp+disp]` addresses SS** — code holding a kernel pointer in BP needs `[ds:bp+…]`.
- **Register discipline.** Every public routine preserves all registers except documented outputs. ISRs push DS/ES, load DS = KERNEL_SEG, `cld` before string ops. Critical sections use `pushf`/`cli` … `popf`, never `cli` … `sti`.
- **Section discipline.** Four sections, all declared with their attributes at the top of `kernel.asm`; modules switch with a bare `section <name>` and **must switch back to `section .text` before the file ends**, or the next include's code silently lands in the wrong one. `-w+error` turns the tell-tale warning into a build failure.
  - `.text` — kernel image, `KERNEL_SEG`.
  - `.bss` — kernel scratch. Free on disk with `-f bin`.
  - `.lowbss` — scratch in `LOW_SEG` (linear 0x08000): task stacks + disk buffers. Reached through SS or ES, **never DS** (SPEC.md §2.1).
  - `.fartext` — far code, copied to `FAR_SEG` (linear 0x00600) by `far_init`. Costs the kernel window nothing (SPEC.md §33).
- **Label hygiene.** One flat namespace; every module-internal label carries its module prefix (`vga_`, `mou_`, `sch_`, `wm_`, `inst_`, `menu_`, `ui_`, `dsk_`, `dskw_`, `ld_`, `fm_`, `ico_`, `desk_`, `dock_`, …) or is a NASM local label.
- **Memory budget.** Kernel image + .bss must fit below `APP_LOAD_OFF` = 0xB000 (loaded programs occupy 0xB000..0xFDFF in the same segment); `kernel.asm` ends with five build-time assertions that fail the build if exceeded (SPEC.md §15.1). Headroom is ~12KB — measure with the recipe in SPEC.md §15.1 before assuming otherwise. `docs/MEMORY-PLAN.md` is the standing plan for where more space comes from, including the one step not yet taken (packages into their own segments).

### Concurrency (SPEC.md §7 — the crux)

Pre-emptive round-robin scheduling: the int 08h PIT hook chains the BIOS tick, saves the register frame on the task stack, swaps SP, and irets into the next ready task. Tasks are dynamic (MAX_TASKS=12): `task_spawn` takes an argument word (delivered in the task's DX) and returns the slot; a task terminates only via `task_exit` (self-exit; usually through `inst_task_die`), which frees the task slot and the instance record inside one IF=0 window. One drawing mutex (`gfx_lock`) guards all VGA access and hides the cursor; public drawing routines *assume* the caller holds it. Background tasks (Clock, Bounce instances) re-check window visibility *under* the lock. The mouse ISR draws the cursor itself only when the lock is free, deferring to the next unlock otherwise. Task switching pauses during floppy transfers (the tick still runs — the motor needs it).

### The mono adapters reuse the back-buffer renderer (SPEC.md §39)

There is **no second graphics driver**. `kernel/vgabb.inc` was written as a latch-free,
port-free *software* renderer over `vga12.inc`'s coordinate core, targeting a RAM back
buffer — and nothing in it cares that the target is RAM. Point it at the framebuffer
(`[vid_rseg]`), tell it there is one plane instead of four (`[vid_planes]`), and route its
row advances through `gfx_nextrow`, and it *is* the Hercules/CGA renderer. The planar bodies
in `vga12.inc` are simply unreachable on mono and keep their assembly-time constants.

Consequences that are easy to undo by accident:

- **`[bb_on]` means "use the software renderer"** — permanently 1 on mono. The narrower
  `[bb_dbl]` means "a back buffer is armed and must be flushed", and is what `gfx_flush`, the
  Control Panel and the Task Manager's RAM figures read. Conflating them makes a mono machine
  claim double buffering and bill 150KB it never allocated.
- **`gfx_rowbase` and `gfx_nextrow` read their parameters through `CS`, not `DS`.**
  `bb_xfer` runs with DS pointed at the framebuffer (save) or the caller's buffer (restore);
  through DS they would fetch framebuffer bytes as a scan-line stride.
- **`gfx_nextrow` touches DI and flags and nothing else.** Several callers are inner loops
  with no spare register and one is inside IRQ4.
- **The banked layout needs a bank's rows to stay inside its own 0x2000 window.** Hercules
  uses 7,830 of 8,192 and CGA 8,000. `viddet.inc` asserts it; a stride or height change
  breaks it silently.
- The cursor is the one path with no `bb_*` twin (its save-under bypasses the buffer by
  contract), so `cur_pass_mono` is the only genuinely new renderer loop in the port.
- Colours reduce to black / white / a 50% dither (§39.4). The shipped apps' palettes were
  chosen so every distinction they carry in colour survives the reduction.

### Double buffering (SPEC.md §32 — conditional, VGA only)

Unavailable on a mono adapter by design: the renderer already writes the framebuffer
directly, so there is nothing to double, and `bb_init` refuses to set `[bb_avail]` there.

**Off by default, switched at runtime.** `bb_init` only probes int 12h and sets `bb_avail` if conventional RAM ≥ 500KB (500 not 512, so a real 512KB machine still qualifies after the BIOS takes its cut). `bb_on` starts 0, so every machine boots drawing straight to VRAM; the Control Panel's **Display** page (SPEC.md §31.3) flips it via `bb_set`, which seeds the buffer from VRAM (`bb_sync`, GC4 Read Map Select per plane) on the way in and flushes it on the way out. While on, every `gfx_*`/font/icon draw renders into a 4-plane back buffer at segment 0x4000 (`kernel/vgabb.inc`, software or/and/xor — RAM has no VGA latches) and `gfx_unlock` flushes the dirty rect to VRAM before the cursor reappears; `menu_track` flushes once for the pull-down because it draws while holding the lock. Below the floor `bb_avail` stays 0, the page says so and refuses the click, and a 256KB machine can never leave the VRAM path.

Two things keep it affordable, because the flush (VRAM) costs ~24× the render (RAM):

- **`[bb_mono]`** — all four planes hold identical bytes as long as everything is drawn in colour 0 or 15, which is the whole UI (its greys are 0/15 dither). While set, the flush copies *one* plane with Map Mask = 0Fh and the hardware fans it out: a quarter of the VRAM writes, and no mid-flush colour fringing. `bb_mono_chk` retires it one-way on the first other colour (a Minesweeper digit); the planes are always fully rendered, so the flush just reverts to four passes. It hangs off `gfx_fill`/`font_char` ahead of the `bb_on` dispatch, so it tracks colour even while buffering is off — `bb_set` can arm the buffer at any time and seeds it from VRAM.
- **Transient overlays never enter the back buffer.** The drag outline and the menu highlights are XOR overlays drawn and erased inside one held lock — the cursor's contract — so they call `vga_xor_rect_vram`/`vga_xor_fill_vram` direct, like the cursor calls `vga_save_vram`. Routed through the buffer, a 1px outline dirtied the whole window rect and flushed it twice per drag pass. The public `gfx_xor_*` still dispatch to the buffer: packages reach them through the API table and their output is persistent.

### Instances (SPEC.md §29 — how apps live and die)

Everything running — built-in kind or loaded package — is a record in `kernel/instance.inc`'s `inst_tab` (12 × 32B). Boot is clean (no instances); menus call `app_launch` (new instance, or front the existing one at the kind's cap), the close box calls `app_close_win` (task-less: synchronous teardown; task-owned: die flag `I_STATE=2` + hide, the task tears down at next wake), and the title bar's right-hand minimize box hides to the dock (`kernel/dock.inc`, bottom strip rows 456..479, one tile per live instance, stable slot↔tile mapping, XOR-inverted when minimized). `wm_owner[]` maps window slot → instance. The Task Manager lists *instances*, not tasks — one row per `inst_tab` slot plus a "System" row — because task-less apps (About, Disk, every package) only ever run inside window callbacks. Those callbacks are timed at the `W_PAINT`/`W_ONKEY`/`W_ONCLICK` dispatch sites and billed to `I_CYC` via `task_cycles`/`task_debit`, which *move* the cycles off the running task so the rows still add to one total.

### The menu bar belongs to the active app (SPEC.md §12/§12.2/§12.3)

The bar is **chip menu → active application's name → that application's menus**, and only the chip (System) menu is fixed. `kernel/menu.inc`'s `menu_bar` is therefore a *runtime* table rebuilt by `menu_layout` every time the owner changes, not the static `menu_table` it used to be. Ownership is a **window**, `[menu_win]`, and the menus hang off the window record's new `W_MENUS` word (`WIN_SIZE` 18 → 20 — `wm_idx2ptr` multiplies by `WIN_SIZE` now instead of open-coding the stride, which is what broke the first time it changed).

Three one-line hooks move it, and nothing else in the kernel knows the bar exists: `wm_front` activates the window it raises (so launching, raising, un-minimizing and dock clicks all follow for free); the event ladder's window branch activates the clicked window too (a click on the *already* frontmost window never reaches `wm_front`, and the bar still has to follow); and `menu_check`, run at the top of every `menu_draw_bar`, reverts to Locator the moment `[menu_win]` names a window that stopped being visible — one validation covering close, minimize and hide.

**Locator** is the kernel acting as an application (the Finder analogue): the desktop, the drive icons, the Disk browser (up to **four** windows, each on its own drive and folder) and the menus that launch everything else. It is not an instance — it is just the menu set the bar falls back to when no window owns it, and **clicking the bare desktop switches back to it** (the `.desk_icons` branch, before `desk_click`). `menu_loc_set` is an ordinary app menu set whose `AM_ONCMD` is 0, the one value reserved to mean *dispatched by the kernel*: `ui_dispatch` recognises it and rebuilds a `CMD_*` from `ui_loc_base` instead of calling through, which is how the old flat command dispatch survives intact behind the new (cell, item) return. `fm_kinit` points every Disk window at `fm_menus` — Locator's *second* set, same `AM_NAME` but a real `AM_ONCMD` — so the file browser reads as Locator's own window rather than an app called "Disk", and the bar carries File/Folder/View/Special while one of its windows is active.

For an application, the whole interface is `OSAPI_MENU_SET` (slot 0x00AC) plus the `OS88_MENUSET`/`OS88_MENU`/`OS88_MENUSET_END` macros in `apps/os88api.inc`. The command handler is **a window callback reached through the bar**: near-called on the UI task under the gfx lock, billed to the instance, same rules as `W_ONCLICK` — it may draw and may call the file API, must never take the lock, and **must repaint itself**, because the kernel does not repaint after it returns.

### The Standard File dialog is modal, and that is what makes it cheap (SPEC.md §38)

`kernel/fdlg.inc` is the kernel's Open/Save chooser — the other half of the
file API, which until it existed gave packages five whole-file operations and
no way to *name* a file (which is why Note Pad wrote a hard-coded `NOTES.TXT`).
Two things about it are load-bearing and easy to undo by accident:

- **It is not an instance.** No `KIND_*`, no `inst_tab` record — a bare
  `wm_create`d window this module owns, the same species as a `menu_track`
  pull-down rather than an application. So it has no dock tile, no Task
  Manager row and no callback billing (`inst_win_owner` answers 0 for an
  unowned window), and its close **and minimize** boxes reduce to `wm_hide`,
  which `fdlg_gate` reads as *cancelled*. That is why this module has no
  close-path code at all.
- **`[fdlg_win]` is the modal gate**, enforced at three call sites and nowhere
  else: `fdlg_grab` (every button press, swallowed unless it lands in the
  dialog's rect), `fdlg_top` (the keyboard poll) and `fdlg_reap` (the UI
  task's idle pass, which only affects latency). Because nothing else is
  clickable while it is up, no other window can navigate the volume — which
  is precisely why the dialog reads the global mount snapshot directly and
  needs no `VIEW_SEG` cache of its own, the exact opposite of the Disk
  window's rule. The gate lives in `.text` as a `dw 0`, not in `.bss`: `-f
  bin` zeroes nothing and `fdlg_grab` reads it on the machine's very first
  mouse press.

`fdlg_open` (API slot 0x00B0) is called from a window callback that already
holds the gfx lock, so it creates and shows the window inline and returns; the
answer comes back later through a completion callback, run after the dialog is
destroyed so the app repaints onto clean screen.

### Where the memory went (SPEC.md §2.1/§33, `docs/MEMORY-PLAN.md`)

The kernel's 64KB segment is not all the kernel's. Three moves bought it room:

- **Task stacks and disk buffers left the segment** into `LOW_SEG` (linear 0x08000), 20KB of `.lowbss`. Consequence: SS ≠ DS. The disk buffers are read only through `dsk_get_dir`/`dsk_get_icon`, which stage one entry back into the kernel segment so every consumer keeps a plain DS:SI pointer.
- **The package pool slid up** to 0xB000..0xFDFF, handing the kernel the 4KB task 0's stack used to hold. `APP_LOAD_OFF`/`APP_MAX_SIZE` are mirrored in `kernel/kernel.asm`, `apps/os88api.inc`, `tools/os88pkg.py` and the Makefile's `-DOS88_ORG` probe org — change them together, and rebuild every `.o88` (the link base is in the header).
- **The file manager's listings left the segment too** (SPEC.md §2.3/§22.1): `VIEW_SEG` = 0x2C00, four 4KB slots carved off the top of the `SAVE_SEG` block (which narrowed to 0x20000..0x2BFFF), one per open Disk window, each a byte-for-byte copy of `disk_dir` + `disk_icons`. That is what makes several file-manager windows affordable: **paints read the window's cache, actions re-sync the global snapshot first** (`fmv_sync`), so a repaint, a drag or a `wm_paint_all` costs zero floppy I/O. `docs/MEMORY-PLAN.md` Step D lost 16KB to it and says so.
- **Cold modules moved to `.fartext`** (`ctrl.inc`, `taskmgr.inc`), copied to `FAR_SEG` at boot by `far_init`. The blob rides at the tail of the kernel image and lands *on top of* `.bss` — which is exactly why `far_init` is kmain's first act and why `splash.inc` has always kept its state in `.text`. Far code keeps DS = `KERNEL_SEG`, so **all its data stays in `.text`**; only code moves. It reaches the kernel via `KCALL`/`FARK` and is reached via `FARSHIM`. The migration recipe is in `docs/MEMORY-PLAN.md`.

### Layout

- `boot/boot.asm` — 512-byte boot sector; geometry comes from `-DSPT`/`-DHEADS`, sector count from the measured kernel size (both injected by the Makefile).
- `kernel/kernel.asm` — constants, boot sequence, the os8088 API jump table at 1000:0010, `%include`s of all modules, final .bss and size assertion. Module ownership is the table in SPEC.md §4; each `.inc` owns one subsystem (viddet, farcall, vga12, font, mouse, sched, events, wm, instance, menu, ui, apps, disk, diskw, loader, files, fdlg, icons, desk, dock, taskmgr, ctrl).
- `kernel/viddet.inc` — adapter detection, runtime geometry, `gfx_rowbase`/`gfx_nextrow`/`gfx_ink`. Included **before** `splash.inc`: the splash probes and sets the mode on its first tick, so this must be resident in the first `SPL_RESIDENT` sectors and all its data lives in `.text`, never `.bss`.
- `kernel/video.inc`, `keyboard.inc`, `string.inc`, `gfx.inc` are dead — left in the tree but **no longer included** (relics of the pre-GUI text shell, as is `kernel-shell.asm.bak`).
- `apps/` — loadable packages. `os88api.inc` is the SDK: `OS88_HEADER` emits the 32-byte package header, `OSAPI_*` constants name jump-table entries, `OS88_IMAGE_END` seals size + bss. `mines/` (embedded icon), `hello/` (proves the generic-icon fallback), `notepad/` (the former built-in Note Pad kind, moved out to reclaim ~1.4KB of kernel budget — its per-instance bss replaced the fixed 2-instance pool, so the cap is gone), plus the sound-layer packages `recorder/` and `piano/` (SPEC.md §35/§36) and the gate packages `fmtest/`, `sbtest/` and `filetest/`, which never ship on the apps disks and ride their own scratch images.
- `tools/` — host-side Python: `os88pkg.py` (validates/stamps `.bin` → `.o88`), `os88disk.py` (builds FAT12 data-floppy images; `--verify` is a structural fsck, `--scramble` builds a legally fragmented test image), `qmp.py` + `mouse.py` (test drivers).

### Software package pipeline

```
apps/mines/mines.asm --nasm x2--> build/mines.bin (org 0xB000) + build/mines.alt.bin (org 0xB800)
                    --os88pkg.py--> build/mines.o88   (v2: reloc table diffed from the pair)
build/*.o88        --os88disk.py--> build/apps.img / apps360.img   (FAT12 floppy, drive B:)
```

The data disk is a standard **FAT12** volume (SPEC.md §19) — DOS, Windows, macOS and Linux all mount and write it, and since SPEC.md §18.4 so does os8088; every byte read off it is still treated as hostile. `disk_mount` validates the BPB against the 17-rule table in SPEC.md §18.2 before trusting any derived number, snapshots the FAT into `FAT_SEG` (ES-only, `dsk_next_clus` its single reader), re-shapes the root directory into synthesized 32-byte entries (volume label, LFN, subdirectory, hidden/system and deleted entries filtered; 8.3 display names like `MINES.O88`; 32-entry cap), and harvests icons by peeking each type-1 entry's first sector — a v2 `.o88` with the embedded-icon flag donates bytes 32..95, everything else gets the all-zero generic-icon sentinel. Loads go through `dsk_read_chain`, a size-driven cluster-chain walk with run coalescing: files a host OS wrote back fragmented load fine, a corrupt chain fails bounded as "Bad package", and FAT16 (reachable only on 2.88M test geometry — cluster count decides, per the Microsoft spec) differs only in `dsk_next_clus`'s entry decode.

**Writing** is `kernel/diskw.inc` (prefix `dskw_`, the only caller of `disk_write`): seven operations — write (create or replace), read, delete, rename, dfree, plus `dskw_mkdir` and `dskw_rmdir` for folders (SPEC.md §18.5/§18.6) — the first five reached by the OS directly and by packages through API slots 0x0098..0x00A8, UI-task context only. Names resolve in the volume's **current directory** (`[dsk_cwd]`, SPEC.md §19.2), not the root. Three rules are binding and easy to break by accident. (1) **Commit order**: allocate + write the data, flush the FAT, *then* write the directory entry (one sector — the commit), *then* free the replaced chain and flush again; a crash leaks lost clusters, never a cross-link. (2) **Rollback**: any failure before the commit re-reads the FAT off the disk (`dskw_refat`), so a half-built chain cannot survive in RAM to be flushed later. (3) **Coherence by remount**: a successful metadata change re-runs `disk_mount`, so `disk_dir`/`disk_icons`/`disk_nfiles` stay exactly a mount snapshot and no new staleness rule enters the kernel. Writes are gated on `[dsk_mntok]`, set only by a fully successful mount — which is why the boot floppy (no valid BPB) can never be written. Verify write changes with the `apps/filetest` gate package (`make test-snd TESTAPPS=build/filetest.img`, plus the `-frag` and `-fat16` images) **and** `python3 tools/os88disk.py --verify <img>` from the host afterwards — the in-kernel free-space check and the host fsck catch different bugs.

 Packages are format v2 (SPEC.md §20.2): assembled at the 0xB000 link base, shipped with a relocation table that os88pkg.py generates by diffing the dual assembly (class 0 = package-address words, +delta at load; class 1 = `call OSAPI_*` rel16 displacements, −delta). The kernel allocates a region from the 0xB000..0xFDFF pool (first-fit; occupancy derived from the instance table), reads, relocates, zeroes bss, and calls the entry, which registers a window and returns; from then on its paint/key/click procs are ordinary near pointers called like any built-in window's. **Multiple packages — or multiple instances of one — run at once**; closing one frees its region. One binding author rule: package addresses only as whole 16-bit words (os88pkg's reconstruction check fails the build otherwise). **Root-directory order on the apps disk is pinned in the Makefile: mines, hello, notepad, recorder, piano — the kernel filters the volume-label entry, so mines stays index 0; tests click by row, and new packages append after piano so the indices hold.**

### Two geometries of everything

Every image is built twice: 1.44MB (18 spt, for QEMU) and 360KB (9 spt, for 86Box / a real XT). If you change the boot path or the FAT driver / disk layout, check both.
