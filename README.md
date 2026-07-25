# jop

A Macintosh System 1-style graphical operating system for the Intel 8086,
written in real-mode assembly and booted from a floppy. 640x480, 16 colors,
overlapping draggable windows, pull-down menus — and pre-emptive
multitasking, which the real 1984 Macintosh never had.

```
make        # build both floppy images
make run    # boot it in QEMU (with an emulated serial mouse)
make xt     # boot the 360KB image on an emulated IBM PC/XT in 86Box
make test   # boot headless with a QMP socket for scripted testing
make debug  # boot with QEMU halted, waiting for gdb on :1234
make clean
```

![what it looks like: gray dithered desktop, menu bar, Note Pad, Clock and
Bounce windows](docs/screenshot.png)

## What it does

Boots straight into the GUI: a 50%-dither gray desktop, a menu bar
(Apple, File, Special), and three windows already open. All of classic Mac's
core interactions work:

- **Windows** — title bars with pinstripes and a close box on the frontmost,
  1px drop shadows, drag by title bar (XOR outline, Mac-style), click to
  raise, close box to hide, reopen from the File menu.
- **Menus** — press in the bar, drag through pull-downs with live highlight,
  release to choose. Apple → About jop; File → Note Pad / Clock / Bounce /
  Disk / Close Window; Special → Task Manager / Restart.
- **Note Pad** — click it, type; wraps lines, Backspace and Return work.
- **Disk icons** — the desktop shows an icon per floppy drive the BIOS
  reports (int 11h). Click to select, double-click to open that drive in
  the Disk window, freshly mounted.
- **Disk** — a file manager (File → Disk, or a desktop disk icon).
  Mounts the software floppy, lists each file with its icon, and
  double-clicking a program loads it from disk and runs it. Yes:
  loadable software, from a second floppy, on an 8086. A Refresh button
  re-reads the directory after you swap disks; A/B/R keys switch drives.
- **Icons** — a 1-bit icon system, classic Mac style: a built-in library
  (32×32 floppy, 16×16 generic application) plus per-application icons
  that ship inside each `.jop` package and get copied onto the disk's
  icon table by the packaging tools. Minesweeper carries a mine glyph;
  packages without one fall back to the generic icon.
- **Minesweeper** — the first software package: a colorful 9×9
  minesweeper that ships on `build/apps.img`, loaded through the Disk
  window. Blue 1s, green 2s, red flags, first-click-safe mine placement,
  flood fill; `F` toggles flag mode, `N` starts a new game.
- **Clock** and **Bounce** — each runs as its *own pre-empted task*. The
  clock ticks and the ball bounces while you type or hold a drag: that is
  the PIT timer interrupt switching tasks out from under each other, on an
  8086. When another window covers them they stop drawing (and the clock
  keeps time silently); uncover them and they resume.

## How

| piece         | how it works on an XT                                       |
|---------------|--------------------------------------------------------------|
| graphics      | VGA mode 12h, 640x480x16 planar, drawn directly (no double buffer — a 150KB backbuffer wouldn't fit in 256KB of RAM; the real Mac drew directly too). Set/Reset + Bit Mask fills, XOR for drag outlines and menu highlights. |
| multitasking  | pre-emptive round-robin: int 08h (PIT, 18.2Hz) chains to the BIOS tick, then saves the register frame on the task stack, swaps SP, and irets into the next ready task. 4 task slots, 1536-byte stacks. |
| mouse         | Microsoft serial mouse on COM1, IRQ4, 1200 baud 7N1, 3-byte packets — the period-correct XT mouse. QEMU emulates one natively (`-chardev msmouse`). |
| cursor        | arrow with save-under, drawn by the mouse ISR itself when it's safe, deferred to the next unlock when a task holds the drawing lock. |
| keyboard      | BIOS int 16h, polled by the UI task. |
| font          | the VGA ROM's own 8x8 font, copied out via int 10h AX=1130h at boot. |
| floppy        | BIOS int 13h, one sector per call with retries; task switching pauses during a read (the tick still runs — the floppy motor needs it). |
| software      | `.jop` packages on a jopfs data floppy in B:. A package is a flat binary loaded at 1000:A000 — inside the kernel segment, so its window procs are ordinary near pointers — calling the kernel through a fixed jump table at 1000:0010. |
| concurrency   | one drawing mutex (`gfx_lock`); background tasks re-check visibility *under* the lock; ISRs run IF=0 throughout and never draw over a held lock. SPEC.md is the binding contract. |

The kernel is ~6KB. Everything runs in the tiny model — CS = DS = SS =
0x1000, all near calls, no linker: NASM `-f bin` flat binaries only, which
keeps Apple's Mach-O-only toolchain out of the picture.

Only 8086 instructions are used, and `cpu 8086` at the top of kernel.asm
makes NASM enforce that: no `pusha`, no `push imm`, no shifts by an
immediate other than 1. The 8088 in a real XT is the same programming model
on a slower bus.

## Memory map

| linear    | segment | contents                                          |
|-----------|---------|----------------------------------------------------|
| `0x00500` | —       | free; boot stack grows down from `0x7C00`          |
| `0x07C00` | `0000`  | boot sector, where the BIOS puts us                |
| `0x10000` | `1000`  | kernel: code, data, .bss (task stacks, buffers)    |
| `0x20000` | `2000`  | menu save-under heap                               |
| `0xA0000` | `A000`  | VGA planar framebuffer, 80 bytes per row           |

Fits and runs in 256KB of RAM; a build-time assertion fails the build if
image + bss ever pass offset 0xF000.

## Layout

```
SPEC.md              the binding module contracts (interfaces, layouts,
                     concurrency rules) - read this first
boot/boot.asm        512-byte boot sector: LBA->CHS, retrying reads
kernel/kernel.asm    constants, boot sequence, includes, size assertion
kernel/vga12.inc     mode 12h planar primitives, save/restore, gfx lock
kernel/font.inc      ROM font grab + transparent text drawing
kernel/mouse.inc     COM1 UART, IRQ4 ISR, packet decode, cursor
kernel/sched.inc     PIT hook, context switch, spawn/yield/sleep
kernel/events.inc    ISR-safe event ring queue
kernel/wm.inc        window records, z-order, frames, hit test, painter
kernel/menu.inc      menu bar, pull-down tracking
kernel/ui.inc        UI task: event pump, keyboard, drags, dispatch
kernel/apps.inc      About, Note Pad, Clock task, Bounce task
kernel/disk.inc      int 13h floppy reads, jopfs mount + directory
kernel/loader.inc    .jop package validation, load, launch, replace
kernel/files.inc     the Disk window (file manager)
kernel/icons.inc     1-bit icon format, draw routine, built-in library
kernel/desk.inc      desktop drive icons: detect, paint, click to open
apps/jopapi.inc      the package SDK: API offsets, header + icon macros
apps/mines/          Minesweeper, the first software package
apps/hello/          HELLO, a minimal second package (no icon)
tools/jopkg.py       package validator/stamper (.bin -> .jop)
tools/jopdisk.py     jopfs floppy image builder
tools/qmp.py         QMP client for scripted control of a test boot
tools/mouse.py       absolute mouse positioning over the QMP socket
```

## Software packages

Programs live on a second floppy (drive B:) with a purpose-built
read-only filesystem, **jopfs**: a superblock that names the disk
geometry, a 32-entry directory, sector-aligned file data. The build:

```
apps/mines/mines.asm --nasm--> build/mines.bin      flat binary, org 0xA000,
                                                    32-byte .jop header baked in
build/mines.bin --tools/jopkg.py--> build/mines.jop validated package
build/*.jop --tools/jopdisk.py--> build/apps.img    jopfs floppy (and apps360.img)
```

A package is written against `apps/jopapi.inc`: `JOP_HEADER 'NAME', entry`
emits the header, `JAPI_*` constants name the kernel's jump-table entries
(gfx primitives, fonts, windows, ticks, a PRNG), and `JOP_IMAGE_END`
seals the image with its size and loader-zeroed bss. At load time the
kernel validates the header, reads the file into 1000:A000, zeroes bss,
and calls the entry, which registers a window and returns; from then on
the program is event-driven — its paint/key/click procs are called like
any built-in window's. Loading another package replaces the resident one.

## Two images

| image                | geometry                | for                             |
|----------------------|-------------------------|---------------------------------|
| `build/jop.img`      | 1.44MB, 18 spt, 2 heads | QEMU boot floppy (A:)           |
| `build/jop360.img`   | 360KB, 9 spt, 2 heads   | 86Box / real XT boot floppy     |
| `build/apps.img`     | 1.44MB jopfs            | QEMU software floppy (B:)       |
| `build/apps360.img`  | 360KB jopfs             | 86Box / real XT software floppy |

The boot sector takes its geometry from `-DSPT` / `-DHEADS` at assembly
time and reads exactly as many sectors as the measured kernel occupies.
A 1.44MB drive postdates the 8086 by years, so period hardware gets the
360KB build.

## Emulators

`make run` boots QEMU with `-chardev msmouse,id=m0 -serial chardev:m0` —
grab the guest pointer and the arrow follows.

`make xt` boots the 360KB image on an emulated IBM PC/XT in 86Box
(`vm/xt/86box.cfg`): 8088 @ 4.77MHz, 256KB RAM, an Oak OTI-067 8-bit ISA
VGA card, and a Microsoft serial mouse on COM1. VGA arrived in 1987, nine
years after the 8086, but ISA VGA cards did work in XTs — a legitimate if
fancy period configuration. Expect the real 4.77MHz experience: repaints
you can watch.

## Scripted testing

`make test` boots headless with a QMP socket at `build/qmp.sock`:

```
python3 tools/mouse.py build/qmp.sock click 180 150     # click Note Pad
python3 tools/qmp.py build/qmp.sock 'sendkey h' 'sendkey i'
python3 tools/qmp.py build/qmp.sock 'screendump /abs/path/shot.ppm'
python3 tools/qmp.py build/qmp.sock 'quit'
```

(QEMU's msmouse backend truncates large injected deltas, so `tools/mouse.py`
splits every move into ≤60px chunks and derives absolute positions by
pinning against the kernel's edge clamp first.)
