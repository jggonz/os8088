# os8088

## Contributing

Patches welcome — and you do not need to already know 8086 assembly.
**[CONTRIBUTING.md](CONTRIBUTING.md)** walks through contributing with a
coding agent (**Claude Code** or **Codex**) on **macOS, Linux or Windows**:
toolchain setup per platform, the rules the assembler enforces, how to boot
and drive the OS headlessly to verify a change, and what a reviewable pull
request looks like.

```
git clone https://github.com/jggonz/os8088.git && cd os8088
git config core.hooksPath .githooks     # one-time: secret-scan hook
make && make run                        # build, then boot it in QEMU
claude                                  # ...or: codex
```

---

A Macintosh System 1-style graphical operating system for the Intel 8086,
written in real-mode assembly and booted from a floppy. 640x480, 16 colors,
overlapping draggable windows, pull-down menus, closable multi-instance
apps, a dock — and pre-emptive multitasking, which the real 1984 Macintosh
never had (switchable to cooperative from the Control Panel, if you want to
feel what they were up against).

```
make          # build all six floppy images
make run      # boot it in QEMU (with an emulated serial mouse)
make run-640  # the same, on a 640KB machine
make run-720  # the same, off the 720KB pair
make xt       # boot the 360KB image on an emulated IBM PC/XT in 86Box
make xt-640   # the same XT with a full 640KB of RAM
make 286      # 86Box: 286 @ 12.5MHz, 1MB, VGA
make 386sx    # 86Box: 386SX @ 16MHz, 2MB, VGA
make 386      # 86Box: 386DX @ 25MHz, 2MB, VGA
make test     # boot headless with a QMP socket for scripted testing
make debug    # boot with QEMU halted, waiting for gdb on :1234
make marty    # a cycle-accurate IBM 5150 (MartyPC) with a debugger attached -
              # memory, registers, breakpoints, single-step, cycle counts,
              # and no code in the guest. THE FIRST TOOL TO REACH FOR when
              # what you are testing runs on an 8088 (docs/MARTYPC-DEBUG.md)
make clean
```

![what it looks like: gray dithered desktop, menu bar, drive icons, Note Pad,
Timer, Bounce, Control Panel and Task Manager windows, and the dock
strip](docs/screenshot.png)

## What it does

Boots straight into the GUI, and boots *clean*: a 50%-dither gray desktop, a
menu bar carrying Locator's menus, an icon per floppy drive, an empty dock
strip along the bottom — and nothing running. Everything is launched from
the menus. All of classic Mac's core interactions work:

- **Windows** — title bars with pinstripes, a close box and a minimize box on
  the frontmost, 1px drop shadows, drag by title bar (XOR outline, Mac-style),
  click to raise. The close box *quits* the app; the minimize box sends it to
  the dock.
- **Apps as instances** — every running thing, built-in or loaded from disk,
  is a record in one instance table (12 slots). Launching from the menu opens
  a *new* instance up to that app's cap — two Note Pads, ten Timers — and at
  the cap it fronts the one already open instead. Closing frees the slot, the
  task and the memory.
- **Dock** — the bottom strip carries one tile per live instance. Click a
  tile to bring that window up; minimized instances show inverted until you
  restore them.
- **Menu bar clock** — the right end of the bar carries the date and the time
  of day. It can show 12- or 24-hour time and, optionally, seconds; seconds
  are **off by default**, and that is not just taste — with them hidden the
  UI task doesn't take the drawing lock 59 seconds out of 60, so the bar
  doesn't repaint and the cursor doesn't blink for it. At boot the kernel
  reads the hardware RTC
  through int 1Ah — and is fussy about it, because half these machines have
  no CMOS clock at all: the call is poisoned and CF-guarded, every byte must
  be valid BCD and in range, and anything short of that falls back to
  **4 July 2026, 00:00:00**. From then on the PIT is the clock, exactly as
  DOS does it on the same hardware. Click the cell to set it.
- **Menus, and the app that owns them** — press in the bar, drag through
  pull-downs with live highlight, release to choose. The chip menu on the
  left is the same in every application (About os8088 / Control Panel /
  Task Manager); **everything to its right belongs to whatever application
  is in front** — its name, then its own menus. Bring another window
  forward and the bar swaps to that app; click the bare desktop and it
  swaps back to **Locator**, which is what the OS itself is called when it
  is acting as an application (the desktop, the drive icons, the Disk
  browser, and the menus that launch everything else): Locator →
  File → Timer / Bounce / Disk / Close Window, Special → Restart. Apps get
  their menus from one SDK call, so a loaded `.o88` from the software
  floppy takes over the bar exactly like a built-in.
- **Note Pad** — a loadable package on the software floppy. Type; it wraps
  lines, Backspace and Return work, and every instance has its own buffer.
  Its **File menu** opens and saves files on the data floppy — Open… and
  Save As… put up the kernel's **Standard File dialog** (a modal window
  that lists the disk, walks into folders and takes a typed 8.3 name), F2
  saves to the current document and F3 asks. DOS line endings both ways:
  the file opens straight up in Windows Notepad, and one written there
  opens here.
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
  that ship inside each `.o88` package; when a disk is mounted the kernel
  harvests them by peeking each package's first sector. Minesweeper
  carries a mine glyph; packages without one — and any file that isn't a
  package at all — fall back to the generic icon.
- **Minesweeper** — the first software package: a colorful 9×9
  minesweeper that ships on `build/apps.img`, loaded through the Disk
  window. Blue 1s, green 2s, red flags, first-click-safe mine placement,
  flood fill; `F` toggles flag mode, `N` starts a new game.
- **Timer** and **Bounce** — each instance runs as its *own pre-empted task*,
  up to ten of each. The Timer is a stopwatch: it starts counting the moment
  you open it, and **Start · Stop · Reset** sit under the digits — Start greys
  out while it runs and Stop while it does not, so a button never looks
  available and then refuses. Stopping throws its interval away rather than
  owing it, so Start picks up where the digits stand; Reset zeroes them
  without stopping the count. The timers tick and the balls bounce while you
  type or
  hold a drag: that is the PIT timer interrupt switching tasks out from under
  each other, on an 8086. Cover half of one and it keeps going in the half
  you can see — the kernel hands a background task the *visible region* of
  its window and clips every draw to it. Cover it entirely and the ball still
  steps, invisibly, so it turns up where it now is rather than where it was
  buried.
- **Fractal** — five fractals, four palettes, five zoom levels, rendered by
  a package's own background task while the rest of the desktop stays live.
  It renders in three progressive passes, so a coarse full image lands after
  a quarter of the work, and it keeps a run-length copy of that first pass:
  move the window and the picture is back instantly and the render *resumes*
  instead of starting over. On a 4.77MHz XT a frame takes about two minutes,
  which is exactly why that matters.
- **Task Manager** — System → Task Manager: a live CPU load gauge with a
  scrolling history graph, a RAM readout with a usage bar, and one row per
  instance with its state, CPU share and memory. Apps with no task of their own
  only ever run inside their window callbacks, so those callbacks are timed at
  the dispatch site and billed to the instance; an app that *does* own a task
  adds that task's time to the same row — the rows still add up to one total.
- **Control Panel** — System → Control Panel: a two-pane browser, the item
  list on the left and the selected item's settings on the right. It opens
  on **Scheduler**, a two-way toggle between **Pre-emptive** (the boot
  default) and **Cooperative**. Flipping it takes effect on the very next
  timer tick — the About box's third line and the Task Manager's SCH field
  follow — and cooperative mode still can't hang the machine: the timer keeps
  a watchdog on the running task and forces a switch if one holds the CPU for
  a second without yielding. The other pages: **Display** (double buffering
  on machines with the RAM for it), **Sound**, and **Date/Time** — click a
  field of the date or the time, then `+` / `-` to set it, with the month
  lengths and leap years honored (31 March minus a month is 28 February) and
  the new time written back to the hardware RTC if the machine has one. The
  same page carries the clock's two display options — **12-hour clock**
  (which adds an AM/PM field you can click and step, since the hour itself
  is always kept 0..23) and **seconds in menu bar**.

## How

| piece         | how it works on an XT                                       |
|---------------|--------------------------------------------------------------|
| graphics      | VGA mode 12h, 640x480x16 planar, drawn directly by default (the real Mac drew directly too). A 150KB back buffer is available as a runtime option on machines with the heap for it — it is a claim, not a reservation, so a small machine never pays for it. Set/Reset + Bit Mask fills, XOR for drag outlines and menu highlights. |
| multitasking  | round-robin off int 08h (PIT, 18.2Hz): chain to the BIOS tick, then save the register frame on the task stack, swap SP, and iret into the next ready task. 12 task slots, 512-byte stacks (sized against a measured 150-byte high-water mark). Pre-emptive by default; in cooperative mode the tick declines to switch and a task runs until it yields, sleeps or exits — with a ~1s watchdog so a runaway one can't take the machine with it. |
| mouse         | Microsoft serial mouse on **COM1 or COM2** (IRQ4 / IRQ3), 1200 baud 7N1, 3-byte packets — the period-correct XT mouse. The port is neither asked nor configured: both are probed for a UART, every one that answers is listened to at once, and the first to deliver a run of clean packets wins — so the other port stays free for the modem that is usually on it. QEMU emulates a mouse natively (`-chardev msmouse`); `make run MOUSEPORT=com2` puts it on the second port. |
| cursor        | arrow with save-under, drawn by the mouse ISR itself when it's safe, deferred to the next unlock when a task holds the drawing lock. |
| keyboard      | BIOS int 16h, polled by the UI task. |
| font          | the VGA ROM's own 8x8 font, copied out via int 10h AX=1130h at boot. |
| floppy        | BIOS int 13h, one sector per call with retries — reads and writes share one routine, so the CHS math and the retry policy can't drift apart; task switching pauses during a transfer (the tick still runs — the floppy motor needs it). |
| software      | `.o88` packages on a plain FAT12 data floppy in B: — any PC, Mac or Linux box can read and write the disk, and so can os8088: apps create, replace, rename and delete whole files through five API slots, and the kernel validates every byte it reads off the disk before any of it becomes an address (Note Pad saves and loads DOS-readable text files, named through the kernel's Standard File dialog). A package is a flat binary assembled at org 0 and loaded into a first-fit region of a 60KB pool that is **its own address space**, one segment per package, so there are no relocations at all: it calls the kernel through a fixed table of far-call cells at 0060:0010, and the kernel calls back through a three-byte dispatcher in the package's header. Several packages, or several copies of one, run at once. |
| concurrency   | one drawing mutex (`gfx_lock`); background tasks re-check visibility *under* the lock and then arm a clip region — their window's content rect less every window above it — so a covered window draws the part that shows instead of skipping the frame; ISRs run IF=0 throughout and never draw over a held lock. SPEC.md is the binding contract. |

The kernel is ~44KB of code and data, 63.5KB with its buffers. Kernel code is
near-model — CS = DS = `KERNEL_SEG` (0x0060) — with SS pointed at the task
stacks just above it, and no linker anywhere: NASM `-f bin` flat binaries only, which keeps Apple's
Mach-O-only toolchain out of the picture.

Only 8086 instructions are used, and `cpu 8086` at the top of kernel.asm
makes NASM enforce that: no `pusha`, no `push imm`, no shifts by an
immediate other than 1. The 8088 in a real XT is the same programming model
on a slower bus.

## Memory map

A ladder: every rung is the one below it plus its size, so there are no gaps
and only the sizes are real numbers.

| linear    | segment | contents                                          |
|-----------|---------|----------------------------------------------------|
| `0x00600` | `0060`  | kernel: code, data, .bss                           |
| derived   | —       | the mount-time FAT snapshot                        |
| derived   | —       | task stacks and disk buffers, then task 0's stack  |
| derived   | —       | the claim heap: everything else, handed out on demand — a package's region is a claim off the top of it, like any other |
| `0xA0000` | `A000`  | VGA planar framebuffer, 80 bytes per row           |

**The whole kernel — buffers and stacks included — is one span held to a
single budget**, and a build-time assertion says so. Nothing in the ladder
carries growth room: each rung is the measured size of what it holds, so the
heap starts wherever this build's kernel actually ends and moves when the
kernel does. There is no package pool — a 60KB reservation that every machine
paid for whether or not anything was loaded — and retiring it is what returned
that memory to the heap. `docs/KERNEL-MEMORY.md` is the standing account of what the budget is spent
on, and of the measured RAM floor. The heap is simply whatever int 12h
reports minus **89.0KB** — `tools/kernsize.py` prints that figure for the
build in front of you, and it moves whenever the kernel does, so prefer it to
any number written down here.

## Layout

```
SPEC.md              the binding module contracts (interfaces, layouts,
                     concurrency rules) - read this first
PERFORMANCE.md       the target machine - a 4.77MHz 8088, ~1000x slower than
                     the emulator you are testing on. Calibration numbers,
                     the standing redraw budget, and the visible defects
                     QEMU cannot show - read this second
docs/TESTING.md      WHICH TOOL to reach for and what each can test:
                     MartyPC first, then QEMU, then 86Box, then the 5150 for
                     anything with a disk in it
docs/MARTYPC-DEBUG.md a cycle-accurate 5150 with a debugger attached, and the
                     one boundary that matters - cycle accurate is NOT disk
                     accurate
docs/KERNEL-MEMORY.md what the kernel's byte budget is spent on, and the
                     measured RAM floor
boot/boot.asm        512-byte boot sector: LBA->CHS, retrying reads
kernel/kernel.asm    constants, boot sequence, includes, size assertion
kernel/vga12.inc     mode 12h planar primitives, save/restore, gfx lock
kernel/font.inc      ROM font grab + transparent text drawing
kernel/mouse.inc     COM1/COM2 UARTs, IRQ4+IRQ3 ISRs, packet decode, cursor
kernel/sched.inc     PIT hook, context switch, spawn/yield/sleep,
                     pre-emptive/cooperative mode + watchdog
kernel/events.inc    ISR-safe event ring queue
kernel/clock.inc     the system clock: RTC probe/read/write, date + time
                     kept from the PIT, formatting for the bar and the panel
kernel/wm.inc        window records, z-order, frames, hit test, painter
kernel/instance.inc  the instance table: app kinds, launch, close, billing
kernel/menu.inc      menu bar: the active app's name + menus, runtime bar
                     layout, pull-down tracking, Locator's own menu set
kernel/ui.inc        UI task: event pump, keyboard, drags, dispatch
kernel/apps.inc      About, Timer task, Bounce task
kernel/disk.inc      int 13h floppy transfers, FAT12/16 mount + chain walk
kernel/diskw.inc     the FAT write path: allocate, flush, directory entries
kernel/loader.inc    .o88 package validation, region alloc, relocate, launch
kernel/files.inc     the Disk window (file manager)
kernel/icons.inc     1-bit icon format, draw routine, built-in library
kernel/desk.inc      desktop drive icons: detect, paint, click to open
kernel/dock.inc      the bottom dock strip: one tile per live instance
kernel/taskmgr.inc   the Task Manager window: CPU, RAM, instance list
kernel/ctrl.inc      the Control Panel window: item list + settings pages
apps/os88api.inc      the package SDK: API offsets, header + icon macros
apps/mines/          Minesweeper, the first software package
apps/hello/          HELLO, a minimal second package (no icon)
tools/os88pkg.py       package validator/stamper (.bin -> .o88)
tools/os88disk.py     FAT12 data floppy builder (+ --verify fsck)
tools/qmp.py         QMP client for scripted control of a test boot
tools/mouse.py       absolute mouse positioning over the QMP socket
```

## Software packages

Programs live on a second floppy (drive B:) that is a plain **FAT12**
volume — DOS, Windows, macOS and Linux can all mount it, read it and
write files onto it. os8088 only ever reads the disk, and treats
everything on it as untrusted: the boot sector's BPB is validated rule by
rule before any number off it is used, and files are read by walking
their real FAT cluster chains, so a `.o88` a host OS wrote back
fragmented still loads. Files that aren't packages just list with a
generic icon. The build:

```
apps/mines/mines.asm --nasm x2--> build/mines.bin      org 0xB000, header baked in
                                  build/mines.alt.bin  the same source at org 0xB800
build/mines.bin + .alt --os88pkg.py--> build/mines.o88   package + relocation table
build/*.o88 --tools/os88disk.py--> build/apps.img       FAT12 floppy (and apps360.img)
```

A package is written against `apps/os88api.inc`: `OS88_HEADER 'NAME', entry`
emits the header, `OSAPI_*` constants name the kernel's jump-table entries
(gfx primitives, fonts, windows, ticks, a PRNG), and `OS88_IMAGE_END`
seals the image with its size and loader-zeroed bss.

Packages are **relocatable**, which is what lets several run at once. Each is
assembled twice, at two different link bases; `os88pkg.py` diffs the two
binaries to recover exactly which words are addresses, and ships that as a
relocation table. At load time the kernel picks a free region out of the
`0xB000..0xFDFF` pool, reads the file in, walks the table adding the load
delta, zeroes bss, and calls the entry, which registers a window and returns;
from then on the program is event-driven — its paint/key/click procs are
called like any built-in window's — and from one of those it can claim a
single pre-empted worker task of its own. Closing a package frees its region.

## Three geometries of everything

| image                  | geometry                 | for                             |
|------------------------|--------------------------|---------------------------------|
| `build/os8088.img`     | 1.44MB, 18 spt, 2 heads  | QEMU boot floppy (A:)           |
| `build/os8088-720.img` | 720KB, 9 spt, 2 heads    | 3.5" DD / USB floppy / Gotek    |
| `build/os8088-360.img` | 360KB, 9 spt, 2 heads    | 86Box / real XT boot floppy     |
| `build/apps.img`       | 1.44MB FAT12             | QEMU software floppy (B:)       |
| `build/apps720.img`    | 720KB FAT12              | 3.5" DD software floppy         |
| `build/apps360.img`    | 360KB FAT12              | 86Box / real XT software floppy |

The boot sector takes its geometry from `-DSPT` / `-DHEADS` at assembly
time and reads exactly as many sectors as the measured kernel occupies.
A 1.44MB drive postdates the 8086 by years, so period hardware gets the
360KB build.

**No binary is committed to this repository.** `build/` is gitignored
outright — the six images, the kernel, the boot sectors, the drivers and
every package are products of `make`, which needs only `nasm` and `python3`.
For a floppy you can boot without a toolchain, take a
[release](https://github.com/jggonz/os8088/releases) or
[os8088.com](https://os8088.com); the build is deterministic
(`tools/os88disk.py` pins the volume serial and every FAT timestamp), so a
released image rebuilds from its own sources byte for byte.

720KB is the one in between, and it is the same sector at both ends: 9
sectors and 2 heads like the 360KB disk, on 80 cylinders instead of 40,
and the boot sector derives its cylinder from the LBA rather than counting
them — so the two share `build/boot360.bin` and only the BPB differs. It is
there for the machines that can take neither of the others: an XT or AT
fitted with a 3.5" DD drive, and every USB floppy drive and Gotek made,
which read 720KB and 1.44MB and nothing 5.25" at all.

## Emulators

`make run` boots QEMU with `-chardev msmouse,id=m0 -serial chardev:m0` —
grab the guest pointer and the arrow follows.

`make run-640` boots the same QEMU as a maxed-out 640KB machine.
QEMU/SeaBIOS cannot actually run with less than 1MB of guest RAM, but
conventional memory tops out at 640K regardless of installed RAM, so
`-m 1M` makes int 12h — the only way the OS learns the memory size —
report a full conventional memory map, same as a fully populated XT.
(SeaBIOS reserves 1KB at the top for its EBDA, so the OS sees 639K;
86Box's XT BIOS has no EBDA and reports a true 640K.)

`make xt` boots the 360KB image on an emulated IBM PC/XT in 86Box
(`vm/xt/86box.cfg`): 8088 @ 4.77MHz, 256KB RAM, an Oak OTI-067 8-bit ISA
VGA card, and a Microsoft serial mouse on COM1. VGA arrived in 1987, nine
years after the 8086, but ISA VGA cards did work in XTs — a legitimate if
fancy period configuration. Expect the real 4.77MHz experience: repaints
you can watch. `make xt-640` boots the same setup with a full 640KB of
RAM (`vm/xt640/86box.cfg`) — on the 1986 XT board revision (`ibmxt86`),
because the original 1982 planar maxes out at 256KB and 86Box silently
clamps `mem_size` back to the board's limit.

The other end of the range: `make 286`, `make 386sx` and `make 386` boot the
1.44MB image on AT-class 86Box machines — an AMI 286 clone board at 12.5MHz
with 1MB (`vm/286`), a Shuttle HOT-304 386SX at 16MHz with 2MB
(`vm/386sx`) and a Micronics 386DX at 25MHz with 2MB (`vm/386dx`) — all
three with the same OTI-067 VGA and serial mouse. os8088 is 8086 code in
real mode, so a 286 or a 386 runs it verbatim, just faster; the extra
megabytes stay invisible, because int 12h still answers 640K and the OS
never leaves real mode. (Not `ibmat` for the 286: 86Box caps the real 5170
planar at 512KB, the same silent clamp as the XT. And unlike an XT, these
machines have a CMOS — on the first launch the BIOS stops at its setup
screen, and picking "EXIT FOR BOOT" once writes `vm/<machine>/nvr/`.)

## Scripted testing

`make test` boots headless with a QMP socket at `build/qmp.sock`:

```
python3 tools/mouse.py build/qmp.sock to 110 8          # Locator's File menu
python3 tools/mouse.py build/qmp.sock down              # menus need press...
python3 tools/mouse.py build/qmp.sock to 110 30         # ...drag to Timer...
python3 tools/mouse.py build/qmp.sock up                # ...release to choose
python3 tools/qmp.py build/qmp.sock 'sendkey h' 'sendkey i'
python3 tools/qmp.py build/qmp.sock 'screendump /abs/path/shot.ppm'
python3 tools/qmp.py build/qmp.sock 'quit'
```

(QEMU's msmouse backend truncates large injected deltas, so `tools/mouse.py`
splits every move into ≤60px chunks and derives absolute positions by
pinning against the kernel's edge clamp first. Boot is clean, so anything you
want to click has to be launched from a menu first — and note that the bar
belongs to whichever app is in front, so a test that clicks a menu by
coordinate must click the desktop first to get Locator's menus back.)

## Secret scanning

There are no credentials anywhere in this repo or its history, and a tracked
pre-commit hook is there to keep it that way: `.githooks/pre-commit` runs
[gitleaks](https://github.com/gitleaks/gitleaks) over the staged diff and
refuses the commit if anything credential-shaped shows up.

Git does not enable tracked hooks on its own, so each clone needs a one-time:

```
brew install gitleaks
git config core.hooksPath .githooks
```

Without gitleaks on PATH the hook warns loudly and lets the commit through,
rather than making commits impossible on a machine that lacks the tool. To
bypass it deliberately -- a test fixture that is *meant* to look like a key --
use `SKIP_GITLEAKS=1 git commit` or `git commit --no-verify`. If a real secret
ever does land, rotate it; deleting the line does not un-leak it.

## License

MIT -- see [LICENSE](LICENSE). Everything here is hand-written; no third-party
code is vendored into the OS, so the whole tree is covered by that one license.

The website in the sibling `os8088-web` repository is a separate matter: it
vendors the v86 emulator (BSD-2-Clause), SeaBIOS and SeaVGABIOS binaries
(LGPLv3) and a bitmap font (CC BY-SA 4.0), each shipped with its own license.
