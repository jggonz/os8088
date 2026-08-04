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
make test ADLIB=1 # ...with an emulated AdLib at 388h, so the sound DRIVER
              # (SPEC.md 51.4) has something to attach to. SB16=1 likewise.
make test-snd # make test + PC speaker captured to build/snd.wav; verify with
              # tools/sndcheck.py (note: the wav holds speaker-ON time only, not
              # wall time - a silent boot yields an empty capture, and QEMU leaves
              # the RIFF sizes zeroed, which sndcheck.py absorbs)
make debug    # boot QEMU halted, waiting for gdb on :1234
make xt       # boot 360KB images on an emulated IBM PC/XT in 86Box
make xt-640   # same XT with a full 640KB RAM (vm/xt640/86box.cfg)
make xt-cga      # XT + real CGA card, 256KB (vm/xt-cga)
make xt-hercules # XT + real Hercules card, 256KB (vm/xt-hercules)
make 286         # 86Box AT clone: 286 @ 12.5MHz, 1MB, VGA (vm/286)
make 386sx       # 86Box Shuttle HOT-304: 386SX @ 16MHz, 2MB, VGA (vm/386sx)
make 386         # 86Box Micronics: 386DX @ 25MHz, 2MB, VGA (vm/386dx)
make xt-sound    # ...the XT again with a Sound Blaster 2.0 in it (vm/xt-sound)
make 286-sound   # 286 + SB16 (vm/286-sound)
make 386-sound   # 386DX + SB16 (vm/386-sound)
make check-images # are the git-tracked binaries in build/ what the sources build?
make clean
```

**`make check-images` before committing anything under `build/`.** `build/` is
gitignored, but ~21 artifacts inside it are force-added and shipped — the kernel,
both boot sectors, both bootable floppies, both software floppies, and every
package's `.bin`/`.o88`. Nothing makes them follow a source change, so they go
stale in silence: the tree still builds, still boots, and still looks right while
carrying a floppy image that no longer holds what the source says it does. That
is not hypothetical — two "Rebuild the shipped images" commits exist because
someone caught it by hand, and a merge shipped a Paint two fixes out of date
until the merge rebuilt it. The target builds everything a second time into
`build/.check` and compares byte for byte, which only works because the
toolchain is deterministic on purpose (`tools/os88disk.py` pins the volume
serial and every FAT timestamp for exactly this reason). It reads its list from
`git ls-files build`, so it cannot drift from what is actually tracked, and it
fails three ways: **STALE** (rebuild and commit), **ORPHAN** (tracked, nothing
builds it) and **SCRATCH** (a tracked `VIDEO=`/`RTC=` stamp — which has been
force-added twice, and which needs naming specially because two empty files
compare equal). Its comparison build is always knob-free, so a kernel built with
`VIDEO=`/`RTC=` that reached the tree reads as stale — which mechanizes the
warning the kernel recipe already prints.

Two build knobs exist only for testing the video fallbacks (SPEC.md §39.9):

```
make test VIDEO=cga                    # force the CGA path on a VGA machine
make test VIDEO=herc HERCSEG=0x7000    # force Hercules, framebuffer in RAM
python3 tools/hercshot.py build/qmp.sock 0x70000 out.png   # LINEAR = HERCSEG*16
python3 tools/mouse.py --screen 720x348 build/qmp.sock ...  # MANDATORY here
# ...the whole recipe, and the four ways to get it silently wrong, are in
# docs/HERCULES-TESTING.md
```

A third does the same for the clock (SPEC.md §37.90) — QEMU has an MC146818 and
nothing else, so the other three rungs of the RTC ladder are unreachable without it:

```
make test RTC=bios     # int 1Ah instead of the chip
make test RTC=none     # no clock at all: the 4 July 2026 fallback
make test RTC=ns       # the MM58167 probe against a machine that has none -
                       # it must REJECT and boot, not hang or invent a clock
```

`RTC=` shares `VIDEO=`'s stamp file, so changing it rebuilds the kernel; the
shipped images are always built without either.

`VIDEO=` is tracked by a stamp file, so changing it rebuilds the kernel — without that,
make sees an up-to-date `kernel.bin`, boots the previous adapter, and it reads exactly
like the probe being broken.

**Installing the toolchain in a fresh container (read this before fighting apt).**
`nasm` installs normally. `qemu-system-x86` does **not**: the package index
lists the `noble-updates` build, whose `.deb` 404s on `archive.ubuntu.com` and
then times out against `security.ubuntu.com`, so a plain
`apt-get install qemu-system-x86` burns several minutes and fails. Two things
fix it, and both are needed:

```
apt-get update                       # the shipped index is stale; this is slow
V='1:8.2.2+ds-0ubuntu1'              # the BASE noble version, not -updates
apt-get install -y --no-install-recommends \
        "qemu-system-x86=$V" "qemu-system-common=$V" "qemu-system-data=$V"
```

`-t noble` is **not** enough — it still resolves to the `-updates` version.
Pin all three packages explicitly. `--no-install-recommends` skips the
gstreamer/libcaca display extras, which 404 the same way and which a headless
`-display none` run never touches. If a previous attempt is wedged, clear
`/var/lib/dpkg/lock-frontend` and re-run `dpkg --configure -a` first, and do
not `pkill -f apt-get` from inside a Bash tool call — the pattern matches the
call's own shell and kills it.

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
- **QEMU mounts `build/apps.img` writable, and the OS writes to it.** Any test that saves a file, makes a folder or deletes one *modifies a tracked, shipped artifact* — `git status` then shows `build/apps.img` dirty and `make check-images` reports it STALE, with nothing in `apps/` having changed to explain it. `make` will not fix it: the image is newer than every `.o88`, so it is skipped. `rm -f build/apps.img build/apps360.img && make` does. Do this **before** committing after any file-write test, or the tree ships a floppy with your scratch files on it.
- **A previous session's QEMU may still be running.** `make test` then fails with `cannot create PID file`, but the stale instance keeps answering on `build/qmp.sock` — so every screendump succeeds and shows the OLD kernel, which reads exactly like a change that did nothing. `make test` prints the error; if it scrolls past, `ps aux | grep qemu-system` and compare its start time against `build/kernel.bin`'s mtime.
- To measure drawing work rather than guess at it, drop a counter in the kernel (`inc word [cs:dbg_x]` at the top of `font_char` / `gfx_fill`, with the `dw 0` in `.text` so it has a fixed offset), get that offset from `nasm ... -l /tmp/k.lst`, and read it over QMP with `xp /2xh 0x<KERNEL_SEG*16 + offset>` — HMP's `w` is 4 bytes, so `h` is what you want for a word. Editing any include *before* `font.inc` moves the offset, so re-derive it after every rebuild.
- **QEMU emulates no CGA and no Hercules card** — only VGA-class devices. `make test VIDEO=cga` works because SeaVGABIOS's `int 10h AX=0006h` is a byte-exact CGA framebuffer, but it never exercises the detection probe. **Hercules IS automatable under QEMU** — `docs/HERCULES-TESTING.md` is the recipe, and it is worth reading before you conclude otherwise, because the three ways of getting it wrong all produce a black image or a machine that ignores every click rather than an error. In short: `make test VIDEO=herc HERCSEG=0x7000`, then `python3 tools/hercshot.py build/qmp.sock 0x70000 out.png` (**`HERCSEG` is a segment, hercshot takes the LINEAR address** — that extra zero is the commonest mistake), and drive it with `tools/mouse.py --screen 720x348`. A QMP `screendump` shows you the *VGA* device and will never show a Hercules pixel; it does not error, which is how "Hercules mode doesn't work" gets concluded from one screenshot. What is genuinely out of reach is only the detection probe and the 6845 programming — `make xt-hercules` is the test for those two.
- `tools/mouse.py` paces its moves explicitly (one connection, `sleep` between packets) because the msmouse backend runs at 1200 baud and drops a move whose predecessor is still in flight. On a fast host the old one-process-per-move spacing was not enough, and the symptom is a cursor that never moves while every screendump still looks plausible.
- Only QEMU is routinely verified. `vm/xt/86box.cfg` keys are best-effort guesses and 86Box rewrites its own preference keys on exit (harmless drift — except that it silently clamps `mem_size` to the machine's maximum: `ibmxt` caps at 256K, which is why `vm/xt640` uses `ibmxt86`, the 1986 board revision; the same trap rules out `ibmat` for the 1MB 286, which 86Box clamps to 512K). The cheap way to test a candidate machine without booting it: launch 86Box on a throwaway copy of the config, `kill -TERM` it, and read the config back — 86Box rewrites it on exit with whatever it actually accepted.
- The AT-class targets (`286`, `386sx`, `386`) boot the **1.44MB** images, not the 360KB ones, and they have a CMOS the XT does not: on a fresh `vm/<machine>/nvr/` the BIOS stops at its setup screen and wants "EXIT FOR BOOT" picked once. That is a one-time cost per VM directory, not a failure.
- 86Box's `wp://` prefix on an `fdd_0N_fn` path mounts that floppy **write-protected**, and int 13h then answers status 03h — which the OS faithfully reports as "Write protected" (`FERR_WPROT`). The data floppy carried `wp://` from the read-only-filesystem era and had to lose it before SPEC.md §18.4 writes could work on the XT; the **boot** floppy keeps it deliberately, on all seven 86Box machines. If saving to B: starts failing on 86Box again, check this before suspecting `diskw.inc` — 86Box may have rewritten the key on exit.
- **That `wp://` on the boot floppy means more than it used to.** Since the system disk became a FAT12 volume (SPEC.md §19.3), `SYSTEM.CFG` in its root is where the whole Control Panel lives, and `cp_flush` rewrites it on every click. Write-protected, those writes fail and **nothing persists across a reboot** — the driver list, the sound route, the clock options and the back-buffer setting all come back at their defaults. That is not a bug in `ctrl.inc`, and it matters most on exactly the three machines added for the sound driver: a card enabled on `make xt-sound` will not still be enabled next boot. Drop the `wp://` on that machine's `fdd_01_fn` if you are testing persistence, and put it back afterwards — an unprotected boot floppy is a tracked, shipped artifact the OS will happily dirty.

## Architecture

### Hard rules (from SPEC.md §1 — these break silently if violated)

- **Three video adapters, one binary (SPEC.md §39).** VGA 640x480x16 planar, else
  Hercules 720x348 mono, else CGA 640x200 mono, probed at boot by `kernel/viddet.inc`.
  **`SCREEN_W`/`SCREEN_H`/`ROW_BYTES` are VGA reference values, not the truth** — the live
  screen is `[vid_w]`/`[vid_h]`/`[vid_stride]` and the derived words in §39.2. New code that
  clips, centres or anchors to a screen edge must read those, or it is wrong on two adapters
  out of three.
- **8086 only.** `kernel.asm` opens with `cpu 8086` and the build uses `-w+error`, so NASM rejects anything newer: no `pusha`, no `push imm`, no `shl reg, imm` other than 1 (use CL), no `movzx`, no 32-bit registers.
- **Near model — for the kernel.** CS = DS = `KERNEL_SEG` (0x0060) for all kernel code and every task; **SS = `LOW_SEG`**, because every task stack lives outside the kernel's own segment (just above it). **Every** inter-module call in the kernel is near — there is no far code and no second code segment (SPEC.md §33). ES is scratch but must be restored unless documented. **SS ≠ DS means `[bp+disp]` addresses SS** — code holding a kernel pointer in BP needs `[ds:bp+…]`. **A package owns its own segment** (SPEC.md §20.1), so every crossing of that boundary is a far call in one direction and a dispatcher call in the other; see "Packages own a segment" below.
- **Register discipline.** Every public routine preserves all registers except documented outputs. ISRs push DS/ES, load DS = KERNEL_SEG, `cld` before string ops. Critical sections use `pushf`/`cli` … `popf`, never `cli` … `sti`.
- **Section discipline.** Four sections, all declared with their attributes at the top of `kernel.asm`; modules switch with a bare `section <name>` and **must switch back to `section .text` before the file ends**, or the next include's code silently lands in the wrong one. `-w+error` turns the tell-tale warning into a build failure.
  - `.text` — kernel image, `KERNEL_SEG`.
  - `.bss` — kernel scratch. Free on disk with `-f bin`.
  - `.lowbss` — task stacks + disk buffers, in `LOW_SEG` just **above** the kernel image. Reached through SS or ES, **never DS** (SPEC.md §2.1).
- **Label hygiene.** One flat namespace; every module-internal label carries its module prefix (`vga_`, `mou_`, `sch_`, `wm_`, `inst_`, `menu_`, `ui_`, `dsk_`, `dskw_`, `ld_`, `fm_`, `ico_`, `desk_`, `dock_`, …) or is a NASM local label.
- **Memory budget — read `docs/KERNEL-MEMORY.md` before spending any.** The whole kernel fits the first 64KB above the BIOS: image, `.bss`, the FAT snapshot, the disk buffers and every task stack are one contiguous span from `KERNEL_SEG`, and guard 1 in `kernel.asm` measures it against `KERN_BUDGET` = 65,536. It is at 65,024 today — **512 bytes of headroom, and no growth room anywhere in the ladder by design**. The one exception is the menu save-under, a heap claim rather than a reservation. **Growing past 64KB is a decision to take with whoever asked for the feature, not a build fix.** There is also nowhere to hide code any more: `.fartext` is retired (SPEC.md §33), so cold code is ordinary code. The heap starts where *this build's* kernel ends, so it moves whenever the kernel does. Nothing catches a task stack that outgrows its 512-byte slice — re-run the fill probe (KERNEL-MEMORY) before trusting a smaller number.

### Concurrency (SPEC.md §7 — the crux)

Pre-emptive round-robin scheduling: the int 08h PIT hook chains the BIOS tick, saves the register frame on the task stack, swaps SP, and irets into the next ready task. Tasks are dynamic (MAX_TASKS=12): `task_spawn` takes an argument word (delivered in the task's DX) and returns the slot; a task terminates only via `task_exit` (self-exit; usually through `inst_task_die`), which frees the task slot and the instance record inside one IF=0 window. One drawing mutex (`gfx_lock`) guards all VGA access and hides the cursor; public drawing routines *assume* the caller holds it. Background tasks (Clock, Bounce instances, and a package's optional worker) re-check window visibility *under* the lock and then arm a clip region (below). The mouse ISR draws the cursor itself only when the lock is free, deferring to the next unlock otherwise. Task switching pauses during floppy transfers (the tick still runs — the motor needs it).

### The clip region (SPEC.md §11.3 — how a covered window keeps drawing)

`wm_obscured` answers a boolean, and every background painter used it as a veto: one covered pixel and the whole frame was skipped, because the `gfx_*` primitives take **absolute screen coordinates and clip only to the screen edge**, so a covered window that drew would paint over the window on top of it. `wm_clip_set` replaces the veto with a region — the window's content rect minus every visible window above it in `wm_zord`, drop shadows included, into a 16-rect list. While it is armed the seven clipped primitives draw only inside it — and a primitive that is *not* on that list is a hole, not a design decision: `gfx_fill_pat` was off it for as long as it existed, which let the Task Manager's memory map (claim bands, buffer texture, region patterns — nearly all of it) paint its full width across whatever window was on top. `gfx_blit4` and `gfx_scroll` are still off it, deliberately and documented in SPEC.md 11.3, because a blit cannot take a sub-rect without advancing its source to match.

Four things are load-bearing:

- **The hook is at the PUBLIC entry, above the `cmp byte [bb_on], 0` dispatch.** One implementation then covers the VRAM path, the back-buffer path, VGA and both mono adapters — because on mono the software renderer *is* the direct path (§39.5). Below the dispatch it would work on VGA and silently no-op on Hercules and CGA, which is the expected failure mode; `make test VIDEO=cga` and `tools/hercshot.py` are what catch it. Same reasoning that places `bb_mono_chk`.
- **`gfx_unlock` clears the clip.** The region is computed from `wm_zord` and the window rects, which the UI task mutates only under the lock, so it is valid for exactly one lock hold and meaningless after. Dying with the lock is also what keeps the drag outline and the menu highlights unclipped (rule 2) without either of them knowing the region exists.
- **`wm_paint_all` is never clipped.** It draws back to front and the painter's algorithm resolves overlap for free. Clipping is for asynchronous single-window drawing only.
- **Two primitives clip whole-shape, not per-pixel**: `font_char`'s 8x8 cell and `ico_core`'s icon body, via `wm_clip_test` — neither can draw half a shape, and both already skip one that would cross a *screen* edge. And `gfx_xor_rect` decomposes into four `gfx_xor_fill` strips first, because an outline is not the intersection of its bounding rect with anything.
- **The granularity rule, which is the sharp edge.** Fills clip per pixel and glyphs clip per whole cell, so **anything that erases a rect and then draws text into it must not let the two disagree**. Ungated, a window cut horizontally by another window's edge gets its visible rows white-filled and then no text back in them — it goes *blank*, not stale, and re-blanks on every update. Two ways out, both in the tree: erase per cell behind a `wm_clip_test` on that cell (`app_clk_render`), or gate the whole erase+draw pair on a `wm_clip_test` of the whole rect and skip both (`fr_status`).

  **The whole-rect gate is charged to the wrong thing when the cut is VERTICAL**, and the Task Manager is where that showed: a window overlapping the list's right-hand columns cuts exactly one glyph cell per row, and the gate threw away every row to protect it — so a partly-covered Task Manager stopped listing anything new, and an app launched while it was covered never appeared until something forced a full repaint. `tm_row_draw` is the answer, and it is **one width for two questions**: a row is split into `TM_NCHUNK` chunks of `TM_CHUNK` characters, and a chunk is both the unit of "did this text change" (`tm_chunksum` against its own word in `tm_rowck`) and the unit of "may this be drawn" (`wm_clip_test` on the chunk's rect). A vertical edge costs the one chunk it crosses instead of the row; a chunk that *is* crossed zeroes its own check word so it is retried rather than recorded as drawn while blank; a horizontal cut still fails every chunk and so still draws nothing, which is what the gate was right about. **The content check comes first**, and getting that order wrong is a real regression on a 4.77MHz machine: an occluded Task Manager must cost a hash per chunk, not a redraw per row. The string is zero-padded to the full chunk span so every chunk hashes deterministically and a row that got *shorter* changes the chunk it lost its characters from, and the last chunk's fill runs on to the band's right edge, because the pen is inset and no chunk covers the tail. `wm_clip_test` is API slot 0x0180 for exactly this. Solid-only drawing is unaffected — Bounce erases and redraws with `gfx_fill` at both ends.

Overflow (more than 16 rects) degrades to CF=1, "skip this frame" — exactly what `wm_obscured` used to say, so it cannot regress anything. `wm_obscured` stays, and `cp_tick` and `tm_update` still use it: it is the cheaper answer for a drawer that repaints its whole pane in one go.

### Coming to the front costs one window; going away costs a rectangle (SPEC.md §11.90/§11.91)

Neither `wm_show` nor `wm_front` calls `wm_paint_all`. **Coming to the front
reveals nothing** — the window moves up, so for every other window the covered
area can only grow — and the full pass was a whole-screen planar dither plus
every visible window's frame and `W_PAINT`, paid to raise one window. Both go
through `wm_raise`, which draws four things in order: the menu bar
(`menu_activate` just handed it over), the dock (the owning instance may be new,
and the *active* tile moves) — **and neither of those usually draws anything**,
because both are incremental now: `menu_draw_bar` is gated on `[menu_bdirty]`,
which only `menu_relayout` and a fullscreen/save-under overdraw set, and
`dock_paint` keys each tile on its icon plus live/minimized/active so a focus
change costs two tiles and a quiet desktop costs none — then **the outgoing
front window's title bar**
(`wm_draw_title` — the pinstripes and the two boxes belong to the frontmost
window alone), then this window, last and therefore on top.

How much of *this* window is drawn is the one thing the two entry points
disagree about. `wm_show` always draws it whole: a newly visible window has no
pixels on screen. `wm_front` asks **`wm_obscured` before `wm_lift`**, while the
z-order still says what was on top — nothing was, so only the pinstripes
changed, so only `wm_draw_title` runs. A click on a background window's title
bar is that case, and it now costs two title bars and the chrome. Raising a
window that is *already* frontmost repaints no window at all.

Three traps:

- **`wm_top` is read BEFORE the visible bit goes on** in `wm_show`. `wm_create`
  has already appended the new window to `wm_zord`, so once it is visible
  `wm_top` answers with *itself* and the outgoing front never loses its stripes.
- **A window over the dock costs a rectangle, not the screen.** The strip is
  drawn under windows, and `wm_fit` keeps a window above it but `ui_grow`'s
  clamp is looser, so a grown window can hang over it — where `dock_paint`
  would draw on top of a window instead of under it. That used to be
  `wm_fast_ok`'s second veto, so **one** oversized window made every focus
  change, show and un-minimize a full-screen repaint. `wm_dock_under` owns it
  now: `dock_paint` reports in CF whether it drew anything, `wm_dock_clear`
  whether a window is on the strip, and only if both say yes does
  `wm_dmg_wins` — §11.91's mark-and-draw pass, factored out for this — put
  those windows back. Fullscreen (§11.2) is the one veto left.
- **`wm_front` on a hidden window falls back** rather than draw a window that
  has no pixels on screen. `wm_show` is the entry point for that.

**Hiding, destroying and dragging do reveal — but only inside the rect the
window vacated**, and `wm_paint_dmg` is that argument (SPEC.md §11.91). It takes
an inclusive damage rect and repaints the desktop dither clipped to it, the
drive zones it touches, the chrome (always — a tile leaves, the focus cue moves,
the bar may lose its owner), and then the windows. `wm_hide` and `wm_destroy`
pass the window's frame rect; `ui_drag` passes the union of where the window was
and where it is. A window closing on the left of the screen no longer redraws a
window on the right.

Four things hold it up:

- **A window is marked if it overlaps the damage — *or* overlaps a window
  already marked below it.** The second half is not optional: a marked window is
  redrawn *whole*, so it would paint over anything it overlaps. Marking runs
  bottom-to-top over `wm_zord`, so one pass reaches the transitive closure. And
  nothing in that pass may keep a loop counter in a general register —
  `wm_win_rect` writes all four.
- **A touched drive zone is folded into the rect, not special-cased in the
  marking** (`desk_dmg_zones` grows the rect to it), because a zone is drawn
  whole and a window over it must therefore be redrawn. The **dock is not**:
  the strip is full width, so a rect grown to reach it is full width for the
  damage's whole height, which erased the drive icons out from under any
  window tall enough to touch the bottom of the screen. It is a per-window
  test in the marking pass instead — a window whose rect reaches
  `[vid_dock_y0]` is marked, and nothing else moves.
- **A wholly covered window is not drawn at all.** `wm_covered` is §11.3's
  region arithmetic seeded with the *frame* rect instead of the content rect;
  empty means every pixel it would write is written again by something above it.
  `wm_paint_all` uses it too. The visible consequence is that **`W_PAINT` does
  not run on a wholly covered window**, so a paint proc must be a repaint and
  nothing else. The overflow degradation is the *opposite* of `wm_clip_set`'s:
  more than 16 fragments means "not covered, draw it", because skipping on a
  maybe loses pixels. This is not the old `wm_obscured` veto coming back — a
  *partly* covered window is still redrawn in full.
- **Hiding the front window promotes the one underneath**, and the promotion is
  visible. After the marked windows are drawn, `wm_paint_dmg` re-asks `wm_top`
  and owes it one `wm_draw_title` if it was not redrawn in this pass. Forget it
  and the new front window sits there looking inactive until something else
  repaints the world.

An empty damage rect is legal and means "nothing was revealed, but the chrome
changed": `wm_destroy` passes one when the window was **already hidden**, which
is the second half of closing a task-owned app (the close box hides, the worker
destroys a moment later). That used to be a second whole-screen repaint for two
strips' worth of change.

The one consumer that had to follow is the file manager, and it got cheaper at
both ends. A window that posted a load has `'Loading...'` in its status line,
and nothing repaints it any more; `files_poster` arms `wm_clip_set` on that
window and calls **`fm_status_only`** — one *line*, not the window's whole
content. The double-click that posted the load does the same. Both fall back to
`fm_repaint` when `wm_clip_test` says a clip edge crosses the line, because a
fill clips per pixel and glyphs clip per cell, so the line would go blank rather
than stale (the granularity rule). `files_poster` also needs `fm_win_of`, the
reverse of `fm_vp_set`, because `[ld_pwin]` holds the poster's **state block**,
not its window — a distinction that silently draws a Disk window's contents
through a garbage rect if you miss it.

### Retitling costs a strip, and the dock stopped being a trap (SPEC.md §11.92/§39.7)

A caption changes on an **event**, never on a paint — so a window knows what it
wants to be called *after* the frame carrying that caption has been drawn.
`wm_title_set` (API slot 0x0228) is the correction: BX = window, AX = the new
`W_TITLE` (or **0**, "the bytes it already names changed underneath it"), lock
held, and it draws `y .. y+TITLE_H-1` and **nothing else** — no content fill, no
`W_PAINT`, no other window. Three ways out, picked by the granularity rule:
nothing above it → `wm_draw_title`; wholly covered → draw nothing (answered
*before* `wm_clip_test`, which reads an empty list as "disarmed, draw freely");
anything in between → `wm_paint_dmg` over the strip, because a fill clips per
pixel and a glyph per cell and the caption would go blank rather than stale.

The file manager is the reference consumer and lost `[fm_full]` — a flag that
escalated its next repaint to the whole frame — for `[fm_tdirty]`, a **pointer**
to the window owing a caption. Deferred and a pointer because `fm_settitle`'s
callers disagree about the lock: `fm_go`/`fm_mount`/`fm_view` hold it,
`fm_kinit` runs before the window exists on screen, and `fmv_sync`'s
folder-vanished path arrives from `ld_run`, which holds none. `fm_title_flush`
spends it, and only `fm_repaint` and `files_poster` call that.

**`wm_fit` takes one pixel off both height clamps** (`dock_y0 - MBAR_H - 1` and
`dock_y0 - h - 1`). The drop shadow is on row `y+h` and `wm_dock_clear` tests
`y+h` with `jae`, so a frame that merely *reaches* the strip is already on it —
and every window later shown over that one pays a `wm_dock_under` pass. One
subtraction fixes every fixed-size template at once, which is why Solitaire,
Arkanoid and the Task Manager needed nothing beyond keeping their own derived
layouts in step. **`wm_dock_snap`** then handles what the user does by hand:
called by `ui_drag` and `ui_grow` after their own clamps, it moves a window
**up** off the strip, and only when both gates open — less than `DOCK_H`/2 rows
covered (past that it was deliberate; leave it and let `wm_dock_under` pay) and
`dock_y0 - 1 - h >= MBAR_H` (a window taller than the desktop band is left
completely alone, because Paint grown to nearly the whole screen is a legal
size). In `ui_grow` a snap moves the **origin**, which nothing else in a resize
does, so bank the old rect's last row before the call and union against it.

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

**Off by default, switched at runtime.** `bb_init` only probes int 12h and sets `bb_avail` if conventional RAM ≥ 500KB (500 not 512, so a real 512KB machine still qualifies after the BIOS takes its cut). `bb_on` starts 0, so every machine boots drawing straight to VRAM; the Control Panel's **Display** page (SPEC.md §31.3) flips it via `bb_set`, which seeds the buffer from VRAM (`bb_sync`, GC4 Read Map Select per plane) on the way in and flushes it on the way out. While on, every `gfx_*`/font/icon draw renders into a 4-plane back buffer — a 150KB heap claim (SPEC.md §50), not a pinned segment (`kernel/vgabb.inc`, software or/and/xor — RAM has no VGA latches) and `gfx_unlock` flushes the dirty rect to VRAM before the cursor reappears; `menu_track` flushes once for the pull-down because it draws while holding the lock. Below the floor `bb_avail` stays 0, the page says so and refuses the click, and a 256KB machine can never leave the VRAM path.

Two things keep it affordable, because the flush (VRAM) costs ~24× the render (RAM):

- **`[bb_mono]`** — all four planes hold identical bytes as long as everything is drawn in colour 0 or 15, which is the whole UI (its greys are 0/15 dither). While set, the flush copies *one* plane with Map Mask = 0Fh and the hardware fans it out: a quarter of the VRAM writes, and no mid-flush colour fringing. `bb_mono_chk` retires it one-way on the first other colour (a Minesweeper digit); the planes are always fully rendered, so the flush just reverts to four passes. It hangs off `gfx_fill`/`font_char` ahead of the `bb_on` dispatch, so it tracks colour even while buffering is off — `bb_set` can arm the buffer at any time and seeds it from VRAM.
- **Transient overlays never enter the back buffer.** The drag outline and the menu highlights are XOR overlays drawn and erased inside one held lock — the cursor's contract — so they call `vga_xor_rect_vram`/`vga_xor_fill_vram` direct, like the cursor calls `vga_save_vram`. Routed through the buffer, a 1px outline dirtied the whole window rect and flushed it twice per drag pass. The public `gfx_xor_*` still dispatch to the buffer: packages reach them through the API table and their output is persistent.

### Instances (SPEC.md §29 — how apps live and die)

Everything running — built-in kind or loaded package — is a record in `kernel/instance.inc`'s `inst_tab` (12 × 32B). Boot is clean (no instances); menus call `app_launch` (new instance, or front the existing one at the kind's cap), the close box calls `app_close_win` (task-less: synchronous teardown; task-owned: die flag `I_STATE=2` + hide, the task tears down at next wake), and the title bar's right-hand minimize box hides to the dock (`kernel/dock.inc`, bottom strip rows 456..479, one tile per live instance, stable slot↔tile mapping). A tile carries two independent marks: **minimized** XOR-inverts its interior, **active** — the instance owning the frontmost visible window — doubles its border. Two different kinds of mark on purpose, and a heavier border is the one that survives the 1bpp reduction. `wm_owner[]` maps window slot → instance. The Task Manager lists *instances*, not tasks — one row per `inst_tab` slot plus a "System" row — because task-less apps (About, Disk, and any package that has not claimed a worker) only ever run inside window callbacks. Those callbacks are timed at the `W_PAINT`/`W_ONKEY`/`W_ONCLICK` dispatch sites and billed to `I_CYC` via `task_cycles`/`task_debit`, which *move* the cycles off the running task so the rows still add to one total.

A package may claim **one** worker task from a callback (`OSAPI_TASK_SPAWN`/`OSAPI_TASK_ALIVE`, SPEC.md §20.6 → `inst_pkg_spawn`/`inst_pkg_alive`) — the first time two packages can be pre-empted against each other, and the first time a package instance takes the *task-owned* close path instead of the synchronous one. The trap: a worker that returns or exits on its own leaks its instance record and its region for the session, because `app_close_win` then sets a die flag nobody ever reads. It must call `OSAPI_TASK_ALIVE` every loop, and that call is where it dies. Two kernel-side rules hold the feature up: `inst_pkg_spawn` fences the package's BX with an **ownership test** (the record must be a package whose own `[I_SPTR, I_SPTR+I_SIZE)` contains the entry in AX), because attaching a worker to a stranger's record puts *both* instances on the wrong teardown path; and `task_spawn` runs its slot scan and its `T_STATE` publish under one `cli`, because this is the first time two different tasks can spawn at once.

### The menu bar belongs to the active app (SPEC.md §12/§12.2/§12.3)

The bar is **chip menu → active application's name → that application's menus**, and only the chip (System) menu is fixed. `kernel/menu.inc`'s `menu_bar` is therefore a *runtime* table rebuilt by `menu_layout` every time the owner changes, not the static `menu_table` it used to be. Ownership is a **window**, `[menu_win]`, and the menus hang off the window record's new `W_MENUS` word (`WIN_SIZE` 18 → 20 — `wm_idx2ptr` multiplies by `WIN_SIZE` now instead of open-coding the stride, which is what broke the first time it changed).

Three one-line hooks move it, and nothing else in the kernel knows the bar exists: `wm_front` activates the window it raises (so launching, raising, un-minimizing and dock clicks all follow for free); the event ladder's window branch activates the clicked window too (a click on the *already* frontmost window never reaches `wm_front`, and the bar still has to follow); and `menu_check`, run at the top of every `menu_draw_bar`, hands the bar to `wm_top` the moment `[menu_win]` names a window that stopped being visible — one validation covering close, minimize and hide. It **promotes rather than reverting** because the title bar does: losing the front window promotes whatever was under it and `wm_paint_dmg` gives that window the pinstripes (§11.91), so a bar that fell back to Locator instead made the screen say two different things about which app is active. `wm_top` answers 0 when nothing visible is left, and 0 *is* Locator, so the old fallback is still the last rung. A deliberate switch to Locator (clicking the bare desktop) is sticky — `[menu_win]` = 0 leaves `menu_check` at its first test.

**Locator** is the kernel acting as an application (the Finder analogue): the desktop, the drive icons, the Disk browser (up to **four** windows, each on its own drive and folder) and the menus that launch everything else. It is not an instance — it is just the menu set the bar falls back to when no window owns it, and **clicking the bare desktop switches back to it** (the `.desk_icons` branch, before `desk_click`). `menu_loc_set` is an ordinary app menu set whose `AM_ONCMD` is 0, the one value reserved to mean *dispatched by the kernel*: `ui_dispatch` recognises it and rebuilds a `CMD_*` from `ui_loc_base` instead of calling through, which is how the old flat command dispatch survives intact behind the new (cell, item) return. `fm_kinit` points every Disk window at `fm_menus` — Locator's *second* set, same `AM_NAME` but a real `AM_ONCMD` — so the file browser reads as Locator's own window rather than an app called "Disk", and the bar carries File/Folder/View/Special while one of its windows is active.

For an application, the whole interface is `OSAPI_MENU_SET` plus the `OS88_MENUSET`/`OS88_MENU`/`OS88_MENUSET_END` macros in `apps/os88api.inc`. The command handler is **a window callback reached through the bar**: called on the UI task under the gfx lock, billed to the instance, same rules as `W_ONCLICK` — it may draw and may call the file API, must never take the lock, and **must repaint itself**, because the kernel does not repaint after it returns.

One trap the bar has already sprung once: **every string in a menu set is an offset in the owning window's segment**, so `menu_bar` carries a `MB_SEG` word *per cell* and `[menu_dseg]` names the dropped one. With a single "active app's segment" instead, the System menu's own items were read out of the package's segment and every one of them drew as `O8` — the first two bytes of the package header.

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
  needs no view cache of its own, the exact opposite of the Disk
  window's rule. The gate lives in `.text` as a `dw 0`, not in `.bss`: `-f
  bin` zeroes nothing and `fdlg_grab` reads it on the machine's very first
  mouse press.

`fdlg_open` (API slot 0x0150) is called from a window callback that already
holds the gfx lock, so it creates and shows the window inline and returns; the
answer comes back later through a completion callback, run after the dialog is
destroyed so the app repaints onto clean screen.

### Where the memory went (SPEC.md §2, `docs/KERNEL-MEMORY.md`)

**The kernel is one span, and it fits 64KB.** `KERNEL_SEG` = 0x0060 — the first paragraph above the BIOS data area — through the top of task 0's stack: image, `.bss`, the FAT snapshot, the disk caches, the sector buffer and every task stack. 65,024 bytes of a 65,536-byte budget. Above it: the claim heap, and nothing else. The 60KB package pool is retired — a package's region is an ordinary heap claim (SPEC.md §20.1), which returned those 60KB to every machine and dropped the RAM floor from 256KB to **128KB**.

Five things got it there, and `docs/KERNEL-MEMORY.md` is the maintained account:

- **Task stacks and disk buffers are in `LOW_SEG`** — `.lowbss`, 9KB, addressed through SS or ES and never DS, which is why SS ≠ DS everywhere. It sits *above* the image now; there is no low memory under the kernel any more, because the kernel starts as low as the BIOS lets it. The disk buffers are read only through `dsk_get_dir`/`dsk_get_icon`, which stage one entry back into the kernel segment so every consumer keeps a plain DS:SI pointer.
- **A package's region is a heap claim, taken from the TOP down** (SPEC.md §20.1/§50.3) while data claims grow up from the bottom. Not tidiness: a data claim can move within its lifetime by being freed and re-claimed, and **a region can never move at all** — its base is its CS. From one end they interleave and a long-lived data claim mid-heap permanently splits the space a package can load into. The region's owner word is the instance **slot** and its data claims' is the **segment**, so `mem_free_rec` releases both and the Task Manager does not count the region twice. `APP_MAX_SIZE` is mirrored in `kernel/kernel.asm`, `apps/os88api.inc` and `tools/os88pkg.py` — change them together and rebuild every `.o88`.
- **The file manager's listings are heap claims** (SPEC.md §2.3/§22.1): `VIEW_KB` (3KB) per open Disk window, a byte-for-byte copy of `disk_dir` + `disk_icons` reached through the `FS_VSEG` segment its state block carries. Paints read the window's cache, actions re-sync the global snapshot first (`fmv_sync`), so a repaint, a drag or a `wm_paint_all` costs zero floppy I/O — and a machine with no Disk window open pays nothing.
- **Nothing has growth room.** Every rung is the measured size of what it holds, so the pool and the heap move with the kernel. `KERN_MAX` — a fixed ceiling with slack under it — is retired: that slack was memory nothing could ever use. The same mistake in miniature was `STK0_TOP`, which used to be "whatever is left below the kernel segment", so task 0's stack silently ate every byte saved anywhere beneath it; `STK0_SIZE` is a named constant now, and that is what turned two rounds of buffer-shrinking into actual memory.
- **`.fartext` is retired** (SPEC.md §33). Cold modules used to be copied to a segment of their own at boot so their code would not count against the kernel's 64KB *window*. The mechanism needed a 10,752-byte reservation to hold a 5,455-byte blob, so once the whole *footprint* became the number being steered by it was spending 5,297 bytes to save nothing. There is now nowhere to put code that is "too cold to be worth the space" — if the image must shrink it shrinks by doing less.

Two invariants that are easy to break, both asserted:

- **Every disk-visible base is 512-byte aligned.** int 13h moves one sector per call, which bounds a transfer to 512 bytes but does **not** stop one straddling a 64KB physical boundary — only starting 512-aligned does, and the DMA controller answers a straddle with error 09h. The FAT snapshot, the disk buffers, a package image and a package's file buffer out of the heap are all int 13h targets, so `KIMG_PARA` rounds the image to a whole 512 bytes and the rest of the ladder follows. It held by accident while every base was a round constant; the symptom when it broke was a **"Disk error" on any save big enough to reach the next 64KB boundary** — Paint's 63KB BMP immediately, a Note Pad file never.
- **The boot sector relocates itself.** It runs at 0000:7C00 and is *still running* while the kernel's sectors land, and the kernel now covers 0x7C00. So it copies itself to `BOOT_RELOC:7C00` (linear 0x11000) and far-jumps there, **keeping the same offset** so every `org 0x7C00` label still resolves. **Three files carry `KERNEL_SEG`** — `kernel/kernel.asm`, `boot/boot.asm` and `apps/os88api.inc`, the last because it is baked into every package's far-call targets, so a kernel move means rebuilding every `.o88` and both apps floppies.

### Layout

- `boot/boot.asm` — 512-byte boot sector; geometry comes from `-DSPT`/`-DHEADS`, sector count from the measured kernel size (both injected by the Makefile).
- `kernel/kernel.asm` — constants, the derived memory ladder and its guards, boot sequence, the os8088 API jump table at 0060:0010, `%include`s of all modules, final .bss and size assertions. Module ownership is the table in SPEC.md §4; each `.inc` owns one subsystem (viddet, vga12, font, mouse, sched, events, wm, instance, menu, ui, apps, disk, diskw, loader, files, fdlg, icons, desk, dock, taskmgr, ctrl).
- `kernel/viddet.inc` — adapter detection, runtime geometry, `gfx_rowbase`/`gfx_nextrow`/`gfx_ink`. Included **before** `splash.inc`: the splash probes and sets the mode on its first tick, so this must be resident in the first `SPL_RESIDENT` sectors and all its data lives in `.text`, never `.bss`.
- `kernel/video.inc`, `keyboard.inc`, `string.inc`, `gfx.inc` are dead — left in the tree but **no longer included** (relics of the pre-GUI text shell, as is `kernel-shell.asm.bak`).
- `apps/` — loadable packages. `os88api.inc` is the SDK: `OS88_HEADER` emits the 32-byte package header (including the dispatcher bytes at +12), `OSAPI_*` `%define`s name the far-call table cells, `OS88_IMAGE_END` seals size + bss. `mines/` (embedded icon), `hello/` (proves the generic-icon fallback — the only thing in the tree that still ships without an icon, deliberately), `notepad/` (the former built-in Note Pad kind, moved out to reclaim ~1.4KB of kernel budget — its per-instance bss replaced the fixed 2-instance pool, so the cap is gone), plus `piano/` (SPEC.md §36, the PC speaker's one client), `fractal/` (SPEC.md §40 — the reference worker task, and the reason both halves of the redraw work exist), `paint/` (SPEC.md §42: a bitmap editor whose canvas, undo image and clipboard are a **heap claim** sized from `OSAPI_MEM_AVAIL`, giving up features tier by tier and finally putting up a notice window on a machine too small. Its BMP **and GIF** codecs borrow those same buffers for their work areas, which is the only reason a 16KB LZW dictionary fits at all; `docs/PAINT-NOTES.md` records which of the capabilities it asked for have since landed and which have not), `solitaire/` (SPEC.md §43 — Klondike, and the one package that drags the way the *window manager* does: `sol_drag` is `ui_drag`'s erase-before-unlock loop written against the API, so a hand of cards costs a few XOR strips a tick and nothing repaints until the button comes up. Faces are drawn but **backs are blitted** — the lattice is rendered once into a packed 4bpp image, so each later draw is one `OSAPI_GFX_BLIT4` instead of hundreds of far calls — and on 1bpp its red pips go *hollow* rather than red, because index 12 reduces to white and would vanish into the card face), `arkanoid/` (SPEC.md §44 — a brick-breaker whose **game loop is the worker task**, because a ball has to keep moving between keystrokes: one frame per `OSAPI_TASK_SLEEP 1`, and everything the UI task does is set a word the worker reads. Two things it discovered are worth knowing before writing another real-time app. int 16h has **no key-up**, so a held arrow is inferred from typematic repeat — each press refills a deadline in ticks that must outlast the ~9-tick typematic *delay*, or a hold stalls for half a second and reads as a dropped keyboard. And **`OSAPI_SND_TONE` is worker-safe**, which the SDK's list did not say until this: `snd_req_inst` stamps a grant with the running task's own `T_INST` when no callback is being dispatched, so a worker's tone is attributed to its instance and released at teardown — only the *blocking* `OSAPI_SND_PLAY` is UI-task-only, and for the different reason that it freezes the desktop), and the gate package `filetest/`, which never ships on the apps disks and rides its own scratch image.

**The fractal's restore cache (SPEC.md §40.1)** is the other half of the redraw work and the thing most easily broken by a well-meaning edit. There is no frame buffer — 320x170 at 4bpp is 27KB against a 19,968-byte pool shared by *every* resident package — so what is cached is progressive **pass 0 alone**, one word per run (colour in bits 15..12, last column in 11..0), 4,000 bytes. `W_PAINT` no longer calls `fr_kick`: `fr_redraw` replays the cache and tells the worker to *resume*. Four rules hold it up. `fr_kick` is the single invalidation point, because every view change already funnelled through it. `fr_cache_row` runs under the gfx lock, after the restart check and *before* the visibility check — under the lock so it is atomic against `fr_kick`, before the visibility check because a row nobody can see is exactly the row worth caching. `fr_restart` now carries three values (0 idle / 1 restart / 2 resume), read with a read-and-clear `xchg`: a separate test and store could see the resume, have `fr_kick` overwrite it with a restart, and clear the restart away. And **`fr_advance` and the `fr_prog` increment live inside `fr_emit_body`, behind that same restart check** — everything meaning "this row was consumed" belongs in one lock hold. Out in the worker's loop, where they used to be, a stale `fr_advance` steps past the row `fr_redraw` just published to resume from; no pass ever paints it (pass 1 is rows 2 mod 4, pass 2 the odd ones) and `fr_crow` can never match `fr_row` again, so the cache freezes too. That was harmless while the flag only meant "restart" — the loop top rewrote pass and row anyway — and the resume value is exactly what makes it not. The 4,000 bytes are not arbitrary — image + bss must stay inside one 512-rounded 7,168-byte region, or two Fractals plus Minesweeper plus Note Pad stop fitting the pool.
- `tools/` — host-side Python: `os88pkg.py` (validates/stamps `.bin` → `.o88`), `os88disk.py` (builds FAT12 data-floppy images; `--verify` is a structural fsck, `--scramble` builds a legally fragmented test image), `qmp.py` + `mouse.py` (test drivers).

### Software package pipeline

```
apps/mines/mines.asm --nasm--> build/mines.bin (org 0)
                    --os88pkg.py--> build/mines.o88   (v3: validated, not relocated)
build/*.o88        --os88disk.py--> build/apps.img / apps360.img   (FAT12 floppy, drive B:)
```

The data disk is a standard **FAT12** volume (SPEC.md §19) — DOS, Windows, macOS and Linux all mount and write it, and since SPEC.md §18.4 so does os8088; every byte read off it is still treated as hostile. `disk_mount` validates the BPB against the 17-rule table in SPEC.md §18.2 before trusting any derived number, snapshots the FAT into `FAT_SEG` (ES-only, `dsk_next_clus` its single reader), re-shapes the root directory into synthesized 32-byte entries (volume label, LFN, subdirectory, hidden/system and deleted entries filtered; 8.3 display names like `MINES.O88`; 32-entry cap), and harvests icons by peeking each type-1 entry's first sector — a v3 `.o88` with the embedded-icon flag donates bytes 32..95, everything else gets the all-zero generic-icon sentinel. Loads go through `dsk_read_chain`, a size-driven cluster-chain walk with run coalescing: files a host OS wrote back fragmented load fine, a corrupt chain fails bounded as "Bad package", and FAT16 (reachable only on 2.88M test geometry — cluster count decides, per the Microsoft spec) differs only in `dsk_next_clus`'s entry decode.

**Writing** is `kernel/diskw.inc` (prefix `dskw_`, the only caller of `disk_write`): seven operations — write (create or replace), read, delete, rename, dfree, plus `dskw_mkdir` and `dskw_rmdir` for folders (SPEC.md §18.5/§18.6) — the first five reached by the OS directly and by packages through API slots 0x0120..0x0140, UI-task context only. Names resolve in the volume's **current directory** (`[dsk_cwd]`, SPEC.md §19.2), not the root. Three rules are binding and easy to break by accident. (1) **Commit order**: allocate + write the data, flush the FAT, *then* write the directory entry (one sector — the commit), *then* free the replaced chain and flush again; a crash leaks lost clusters, never a cross-link. (2) **Rollback**: any failure before the commit re-reads the FAT off the disk (`dskw_refat`), so a half-built chain cannot survive in RAM to be flushed later. (3) **Coherence by remount**: a successful metadata change re-runs `disk_mount`, so `disk_dir`/`disk_icons`/`disk_nfiles` stay exactly a mount snapshot and no new staleness rule enters the kernel. Writes are gated on `[dsk_mntok]`, set only by a fully successful mount — which is why the boot floppy (no valid BPB) can never be written. Verify write changes with the `apps/filetest` gate package (`make test-snd TESTAPPS=build/filetest.img`, plus the `-frag` image) **and** `python3 tools/os88disk.py --verify <img>` from the host afterwards — the in-kernel free-space check and the host fsck catch different bugs.

 Packages are format v3 (SPEC.md §20.2) and **own a segment**: assembled at org 0, loaded on a paragraph boundary claimed off the top of the heap (`mem_claim_hi`), bss zeroed, entry far-called with DS = CS = the package's own segment. There is no relocation of any kind — no dual assembly, no reloc table, no author rule about whole-word addresses — and `tools/os88pkg.py` is a validator rather than a generator.

Three things carry the boundary, and each is solved once rather than per call site:

- **Calling out.** Every API slot is an 8-byte cell that switches DS and `retf`s, and the SDK makes `OSAPI_X` a `%define KERNEL_SEG:offset`, so `call OSAPI_X` is a far call and **no package call site changed** when this landed. Three cells in ten defer to a longer stub: **X stubs** put the caller's DS in ES so the kernel can reach package data (`wm_create`'s template, `font_str`'s string, the spawn fence, the claim owner); **N stubs** stage a file name into the kernel's own buffer first.
- **Calling in.** The window record carries **one** far pointer, `W_DISP`/`W_SEG`, aimed at a three-byte **dispatcher** in the package's header (`call bp` / `retf`, at `PKG_DISP` = 12). Every callback stays an ordinary near proc with a near `ret` — a package author never writes `retf`, so a missing one cannot exist — and dispatch is re-entrant across packages because the pointer comes out of the record, not a global. `wm_pkgcall` is the single site.
- **Reading what you were handed.** **ES = KERNEL_SEG on entry to every callback**, because the window record and the file dialog's name buffer live there. `[es:bx+W_W]`, not `[bx+W_W]` — without the override a package reads its own image at that offset, which assembles cleanly and runs wrong.

Each instance may own one worker task, spawned from a callback and torn down through `OSAPI_TASK_ALIVE`. **Multiple packages — or multiple instances of one — run at once**; closing one frees its region *and every heap claim it held*. **The apps disk is **foldered** (SPEC.md §19.2): the root holds `APPS` and `GAMES` and nothing else, so root indices are 0 = APPS, 1 = GAMES and a package is two double-clicks away. Order inside each folder is pinned in the Makefile (`APPS_TOOLS` = hello, notepad, piano, fractal, paint; `APPS_GAMES` = mines, solitair, arkanoid) — tests click by row, new packages append at the end of their folder, and each folder's indices are now independent of what the other holds.** A package's file name is an 8.3 stem, so it is not always the app's name: Solitaire ships as `SOLITAIR.O88` and carries `SOLITAIRE` in its 16-byte header field, which is what the dock and the Task Manager show.

### The clock is a ladder, not a BIOS call (SPEC.md §37.90)

`int 1Ah` AH=02h..05h is the **last** rung. An XT BIOS implements AH=00h/01h and
nothing else, so on a 5150 with an AST SixPakPlus the BIOS knows nothing about a
clock that is sitting right there; and a BIOS that implements the two *read*
functions may still `iret` out of the two *write* ones — a clock you can read and
never set. `clk_probe` walks four rungs (MC146818 at 70h/71h, then RP5C01/TC8521
at 2C0h, then MM58167 at 2C0h, then the BIOS) and `clk_rtc_write` dispatches on
`[clk_tier]`.

Three things about it are load-bearing:

- **Probe order exists so that no rung writes to a chip a later rung would have
  identified differently.** Two different parts live at 2C0h. The RP5C01 rung is
  claimed **only** when its digits decode to the same hour, day, month and year
  `int 1Ah` just reported — one test that confirms the chip, the base, the
  addressing mode and the MODE page with **no writes at all** — so it runs first
  and a machine whose BIOS cannot read the clock can never reach it. The MM58167
  rung, which does write (a scratch nibble, restored on the single path out of
  `clk_ns_half`), runs after.
- **Every loop is bounded.** The one way to hang is to wait forever for a bit that
  never changes on a machine where every read is 0FFh — the exact bug Linux
  shipped until v5.11. The UIP poll takes its `pushf`/`cli` **per access**, not
  around the loop: 2.3 ms with interrupts off is forty tick periods.
- **The chip's own settings are obeyed, never rewritten.** Register B's DM
  (0 = BCD) and 24/12 (1 = 24-hour) polarities are both counterintuitive and both
  belong to the machine's BIOS; flipping them behind its back makes the clock read
  wrong from DOS afterwards. 12-hour mode's PM bit is stripped *before* BCD
  decoding and re-applied *after* BCD encoding — the other order feeds 8Ch to
  `clk_tobcd`.

`RTC=` in the Makefile forces one rung so the other three are testable at all
(see Commands above); the Control Panel's Date/Time page names the rung that
answered, because on a machine whose clock will not hold a setting that is the
whole diagnosis.

### What came back from `main` (SPEC.md §41, §12.2, §11)

The forks were resynced once, partially and deliberately. Four things crossed:

- **The API slot numbers.** Every slot up to 0x01B0 is at `main`'s number,
  and **above that the tables have parted** (SPEC.md §20.3). They used not
  to: five cells were **held empty** and ten more RESERVED so that a package
  source would assemble for either fork. The branches are merging, so the
  holes bought nothing and were closed — everything above 0x01B0 moved
  **down 88 bytes**, which is the third and last time that block has moved.
  Two of the five held cells were *filled* rather than dropped
  (`OSAPI_SND_FM`/`OSAPI_SND_STREAM` at 0x00F8/0x0100, now the loadable
  sound driver's).

  What survives is the half of the rule that was never about the other fork:
  **a shipped slot keeps its contract**, and "we no longer implement this" is
  a refusing stub, not a reuse. Reusing 0x01C8 for a KB-counting `mem_avail`
  where `main` puts a paragraph-counting one would have failed silently and
  by a factor of 64 — which is why the merge closed the gap by *moving* this
  fork's block rather than by overlaying it. SPEC.md §20.8 rule 4 is the
  written form; **renumbering invalidates every `.o88` at once** and is only
  survivable because every package is in this tree and `make` rebuilds them.

  **No app reads the window record through ES any more.** `wm_geom` answers
  content size and visibility, so Fractal and Note Pad ask the kernel instead
  of dereferencing a pointer whose segment they only held by convention. The
  record is still readable and the SDK still publishes the offsets; it is no
  longer the idiom, and the worker-task case (Fractal) was the one where the
  convention was thinnest.
- **`OSAPI_WM_GEOM` (0x01B0).** Content width/height and visibility in one
  call. Reading `[es:bx+W_W]` still works here and most apps still do it, but
  those are FRAME dimensions and every caller repeated the same
  `-2` / `-TITLE_H-1`; this is that subtraction in one place, at the number a
  package written for `main` already calls.
- **`OSAPI_ABOUT_SET` (0x01E0).** The app's name in the bar becomes a
  one-item pull-down, `About <Name>`. The cell is **appended last** in
  `menu_bar` so the app's own menus keep bar index == set index + 1 and
  `ui_dispatch`'s `dec ah` needs no adjustment; `[menu_abcell]` names it.
  Both its strings live in one kernel buffer — the item is `'About Paint'`
  and the title is `menu_abstr+MENU_ABPFX_LEN`, the same bytes from the name
  onward — so its `MB_SEG` is 0 even under a package. Solitaire and Arkanoid
  ship credit panels behind it; Arkanoid's also holds its **worker** off the
  content while the panel is up, or the game would draw underneath it.
- **`dskw_readbig` (0x01E8).** The one file op with no 64KB ceiling: the
  destination advances by SEGMENT, so a package loads a 96KB file into a heap
  claim in one call. It allocates nothing — the caller supplies the
  destination, and only the partial final sector stages. `os88disk.py` ships
  non-`.O88` files as data now, which is how a big file gets onto a volume to
  be read; `apps/filetest` check 01 covers it against a generated `BIG.DAT`
  whose byte at offset i is `i >> 9`, so a destination that failed to advance
  reads a *different* byte rather than a plausible one.
- **`cpudet.inc` + `xmem.inc` (§41).** CPU tiers, the A20 line and the store
  above 1MB, at `main`'s five slots. On tier 0 — the target machine — all of
  it is zero KB and every entry point returns having touched no port. The
  claim heap is unaffected: §50 is still the answer for *conventional* memory
  a package cannot fit in its own segment, and §41 is the answer for bulk
  data that does not fit conventional memory at all. The Task Manager shows
  it as one `XMS used/sizedK` line **below** the package-pool map, with no map
  of its own — real mode has no address for it, so it is in neither of the
  two maps above (SPEC.md §41.6).

**This is what raised `KERN_BUDGET` from 64KB to 70KB** — the one time it has
moved, granted explicitly, and `docs/KERNEL-MEMORY.md` records what it cost.
The 64KB *segment* limit (guard 2) is untouched and unraisable: 16-bit
offsets. Raising the budget also moved `BOOT_RELOC` (0x0940 → 0x0AA0), which
is mirrored in `boot/boot.asm`.

The apps disk is **foldered** now, like `main`'s: `APPS/` and `GAMES/`, via a
`DIR:` prefix per package in the Makefile. Root indices are 0 = APPS,
1 = GAMES, and a package is two double-clicks away rather than one.

### Loadable drivers (SPEC.md §51, `kernel/driver.inc`)

**A driver is a package that is not an application.** Same 32-byte header,
same `org 0`, same paragraph-aligned heap claim, same three-byte dispatcher
at `PKG_DISP` — so `drv_call` is `wm_pkgcall` with the far pointer taken out
of a driver row instead of a window record, and a driver author writes near
procs with near `ret`s. Four differences, each load-bearing: it is a **.DRV
file** (the mount types only `*.O88` as an application, so it can never be
double-clicked into the loader); its **header version is 4** (so
`ld_check_hdr` refuses it too — two independent gates); it has **no instance
record** (its memory is `MEM_K_DRV`, counted under System); and **its bss
ships inside its image**, which is what lets `drv_load` make exactly ONE
claim, at the size the directory entry already reported, before a byte is
read.

`DRVV_ATTACH` must be all-or-nothing — the kernel frees the image the moment
a driver says no, so anything it hooked outlives it — and `DRVV_DETACH`
cannot fail. Detach happens BEFORE the free, always: freeing the claim under
a live interrupt vector points it at whatever claims that memory next.

A driver publishes a **service table** the kernel copies into `.bss` at
attach (the `dsk_get_dir` staging idiom), so every later dispatch is a near
read plus one far call and `snd_tick` — inside IRQ0 — needs no segment
register to find out whether it has work. `DSV_TONE` is the interesting cell:
publishing it **moves the tone tier off the PC speaker**, which is what an
OPL2 wants (an FM note is two register writes and then no CPU) and a Sound
Blaster does not.

**`drv_svc_call` takes no register but DI, and that is a contract**: every
other general register is an argument to something in the sound ABI — AL the
verb, BX the FM frequency, CL the channel, DH the requesting instance, SI and
ES a staged buffer — so the dispatcher is a far pointer in memory
(`drv_fptr`/`drv_fseg`) rather than something passed in. It went through BX
once and quietly ate the frequency: every FM call came back refused while
*tones*, which pass AX, worked perfectly.

**Porting an app from `main` is two mechanical edits and one trap.** Over
there a callback is far-called and ends in `retf`; here the kernel reaches it
through the package's own dispatcher, so **every proc — the entry included —
is a near proc with a near `ret`**, and the `push cs / call x` trick around a
retf-ending helper goes with it. A `retf` left in place returns into the
loader's stack frame and hangs the machine at the first paint. `apps/fmtest`
is the reference port and the FM gate: `make test-snd ADLIB=1
TESTAPPS=build/fmtest.img`, click twice, and the wav must show 880 Hz
dominant from a keyed 440 — which is only true if the CALLER'S patch bytes
reached the operator registers.

**Nothing here can stop the boot.** No disk, no file, no card and no memory
are all recorded in the row and reported afterwards — `drv_boot` runs before
the first paint so a loaded driver is live from frame one, and `drv_notice`
runs after it and opens the **Control Panel on its Drivers page**, which
already names every driver and says what its last attempt answered.

**The system disk is a FAT12 volume now** (SPEC.md §19.3) and that is what
makes all of it possible: `BPB_RsvdSecCnt` covers the kernel's sectors, so
`boot/boot.asm`'s raw `read LBA 1..K` is untouched while everything after it
is an ordinary file system — drive A: mounts, browses and **writes**.
`SYSTEM.CFG` in its root is 32 bytes carrying the *whole* Control Panel (the
driver list, the sound route, the clock options, the scheduler mode, the back
buffer), written by `cp_flush` on every click and restored by `drv_boot`. A
missing or malformed file means the defaults, never an error.

Two traps. **`build/os8088.img` is now writable and the OS writes to it** —
any test that touches a Control Panel setting dirties a tracked, shipped
artifact, exactly like `build/apps.img`; `rm -f build/os8088.img
build/os8088-360.img && make` before committing. And **`make test ADLIB=1`**
(or `SB16=1`) is the only way to exercise the driver at all: without a card
QEMU's probe correctly finds nothing, which is the right answer and not the
one you want to be testing against.

### The claim heap (SPEC.md §50, `kernel/memory.inc`)

Everything above the kernel is handed out on demand. `mem_claim` takes KB and an owner word and answers a segment; `mem_free`/`mem_free_owner`/`mem_free_rec` give it back, and `mem_avail` reports the largest free run and the total. Five things about it are load-bearing:

- **A package's owner word is the segment it runs in**, put in ES by the slot's X stub — so there is nothing to pass and nothing to forge, and `OSAPI_MEM_CLAIM` works from the entry proc where the app has no window yet. That is where an app sizes itself.
- **`mem_claim_dma` puts the 64KB page rule inside the scan** (SPEC.md §50.3). An ISA bus master cannot carry into the page port, so a DMA buffer must not straddle a 64KB *physical* boundary — and the sound driver used to discover that by claiming, testing the address it got, and claiming again while **holding** the block that failed, so finding 32KB could hold 128KB and refuse on a machine that had the room all along. `CX` is the KB of the block's **head** that the chip sees, not the whole block (the SB's staging pool is copied with `rep movsb` and may straddle freely), and a candidate that would straddle bumps to the next page floor — the same monotonic shape as the bump past an overlapping claim, so termination is unchanged. Two things follow: a head over 64KB is refused up front, and **`mem_regrow` does not preserve the constraint** — no record carries it, so a claim that moves can land straddling.
- **Teardown frees claims.** `mem_free_rec` runs at all three instance teardown sites, which is why `OSAPI_MEM_CLAIM` needs no close hook.
- **The kernel is a client too**, with its own owner tags: the menu save-under (`MEM_K_SAVE`, claimed by `menu_drop` for exactly as long as a menu is on screen and released *before* the chosen item runs) and the back buffer (`MEM_K_BB`). `bb_set` claims 150KB when double buffering is armed and frees it when it is switched off, and the Control Panel's Display row **greys out with "Not Enough Ram"** when `bb_canfit` says the heap cannot fund it right now. That is live state — open Paint and the row greys, close it and it comes back.
- **Refusal is a normal path, not a panic.** Every claim in the tree has a fallback: the menu save-under repaints the menu's own rect instead of restoring it, a Disk window with no listing cache reads the global mount snapshot, Paint gives up features tier by tier and finally puts up a notice window.

### Two geometries of everything

Every image is built twice: 1.44MB (18 spt, for QEMU) and 360KB (9 spt, for 86Box / a real XT). If you change the boot path or the FAT driver / disk layout, check both.
