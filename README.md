# os8088

This is a hobby project. I rely on AI to speed up the work (including the
writing and maintenance of the webpage and the markdown files you see here).
If you spot a mistake, you're welcome to contribute a fix in the form of a PR.

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

> **This is an experimental project, written with AI.** Essentially all of the
> code, and these docs, were produced by coding agents (Claude Code and Codex)
> under human direction and review. It exists for hobbyists, retrocomputing
> enthusiasts and nostalgia — not for production use, and not as a claim of
> hand-written craftsmanship.

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
make xt-cga   # the same XT with a CGA card instead of VGA
make xt-hercules  # ...and the same XT with a Hercules card
make 286      # 86Box: 286 @ 12.5MHz, 1MB, VGA
make 386sx    # 86Box: 386SX @ 16MHz, 2MB, VGA
make 386      # 86Box: 386DX @ 25MHz, 2MB, VGA
make 486      # 86Box: 486DX2 @ 66MHz, 8MB, VGA, Sound Blaster 16
make pentium  # 86Box: Pentium @ 133MHz, 16MB, VGA, Sound Blaster 16
make xt-sound # the 640KB XT with a Sound Blaster 2.0 (OPL2 + DSP)
make 286-sound  # 86Box: the 286, with a Sound Blaster 16
make 386-sound  # 86Box: the 386DX, with a Sound Blaster 16
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

Boots straight into the GUI, and boots *clean* — nothing is running, and
everything is launched from the menus. The classic Mac interactions are all
here: windows you drag, raise, close and minimize; pull-down menus that
belong to whichever application is in front; desktop drive icons; a dock; and
a Standard File dialog for opening and saving.

**System**

- **Pre-emptive multitasking** off the 18.2Hz PIT tick — 12 task slots. The
  Control Panel switches it to cooperative, with a watchdog so a task that
  never yields still can't take the machine.
- **Apps as instances**: up to 12 live at once, several copies of one app
  included, each with its own window, task and memory. The dock carries a
  tile per instance.
- **Task Manager** — live CPU and RAM, one row per instance, with the work
  done inside an app's window callbacks billed to that app.
- **Control Panel** — Scheduler, Buffer, Display, Sound and Date/Time.
- **Menu bar clock** — read from the hardware RTC at boot if the machine has
  one, kept from the PIT after that, and settable.
- **A system clipboard**, shared across apps.

**Disks and files**

- **FAT12 and FAT16, read and write.** The disks are ordinary volumes, so any
  PC, Mac or Linux box mounts them — and every byte read off one is still
  treated as hostile.
- **Hard disks** — mounts every FAT partition it finds, installs os8088 onto
  a drive, and boots from that partition.
- **A file manager per volume** — open, rename, delete, make folders, and
  Cut/Copy/Paste with drag and drop. Copies stream rather than having to fit
  in memory.
- **File associations** — double-click a document and it opens in its
  program, on any volume.

**Software**

Fifteen loadable packages ship on the software disk, all closable and most
multi-instance:

- **Apps** — Note Pad (word wrap, DOS-readable text files), Paint,
  ArtfulType, Fractal, Piano, Recorder, Tracker and ModPlug Player (both play
  Amiga MOD files).
- **Games** — Minesweeper, Solitaire, Arkanoid, Missile Command and TameGram.
- ...plus the Task Manager itself, and HELLO, a minimal package that exists to
  be the smallest thing the SDK can build.

Timer and Bounce are built into the kernel rather than loaded. **Drivers**
load the same way packages do — hard disk, sound, and a serial debug
monitor.

**Hardware**

- **One binary drives three adapters**, picked at boot: VGA (640x480, 16
  colors), CGA, and the Hercules mono card.
- **Microsoft serial mouse** on COM1 or COM2 — both are probed, so the port
  the modem is usually on stays free.
- **Sound** — PC speaker, AdLib/OPL2, and Sound Blaster.
- **8086 real mode throughout**, so everything from a 4.77MHz IBM PC/XT to a
  Pentium runs the same image.

## How

| piece         | how it works on an XT                                       |
|---------------|--------------------------------------------------------------|
| graphics      | VGA mode 12h, 640x480x16 planar, drawn directly by default (the real Mac drew directly too). Set/Reset + Bit Mask fills, XOR for drag outlines and menu highlights. A 150KB back buffer is available as a runtime option on machines with the heap for it — it is a claim, not a reservation, so a small machine never pays for it. **CGA and Hercules are the same binary**: the adapter is probed at boot, a 1bpp renderer takes over, and the live screen size is read at runtime rather than assumed — so anything that clips or anchors to an edge is right on all three. |
| multitasking  | round-robin off int 08h (PIT, 18.2Hz): chain to the BIOS tick, then save the register frame on the task stack, swap SP, and iret into the next ready task. 12 task slots, 512-byte stacks (sized against a measured 150-byte high-water mark). Pre-emptive by default; in cooperative mode the tick declines to switch and a task runs until it yields, sleeps or exits — with a ~1s watchdog so a runaway one can't take the machine with it. |
| mouse         | Microsoft serial mouse on **COM1 or COM2** (IRQ4 / IRQ3), 1200 baud 7N1, 3-byte packets — the period-correct XT mouse. The port is neither asked nor configured: both are probed for a UART, every one that answers is listened to at once, and the first to deliver a run of clean packets wins — so the other port stays free for the modem that is usually on it. QEMU emulates a mouse natively (`-chardev msmouse`); `make run MOUSEPORT=com2` puts it on the second port. |
| cursor        | arrow with save-under, drawn by the mouse ISR itself when it's safe, deferred to the next unlock when a task holds the drawing lock. |
| keyboard      | BIOS int 16h, polled by the UI task. |
| font          | the VGA ROM's own 8x8 font, copied out via int 10h AX=1130h at boot. |
| disks         | BIOS int 13h, with retries — reads and writes share one routine, so the CHS math and the retry policy can't drift apart, and contiguous clusters coalesce into one transfer because a call costs roughly a disk revolution whatever it moves. Task switching pauses during a transfer (the tick still runs — the floppy motor needs it). FAT12 and FAT16, on floppies and on hard-disk partitions. |
| software      | `.o88` packages on plain FAT volumes — any PC, Mac or Linux box can read and write the disks, and so can os8088: apps create, replace, rename and delete files through the API, and the kernel validates every byte it reads off a disk before any of it becomes an address. A package is a flat binary assembled at `org 0` and loaded into a paragraph-aligned **claim off the heap**, which is **its own address space**, one segment per package — so there are no relocations of any kind, and `tools/os88pkg.py` is a validator rather than a generator. It calls the kernel through a fixed table of far-call cells at 0060:0010, and the kernel calls back through a three-byte dispatcher in the package's header. Several packages, or several copies of one, run at once. |
| concurrency   | one drawing mutex (`gfx_lock`); background tasks re-check visibility *under* the lock and then arm a clip region — their window's content rect less every window above it — so a covered window draws the part that shows instead of skipping the frame; ISRs run IF=0 throughout and never draw over a held lock. SPEC.md is the binding contract. |

The kernel is ~54KB of code and data, and 89.5KB all in — buffers, stacks,
the mount-time FAT snapshot and the boot-time code it later hands back.
`tools/kernsize.py` prints both for the build in front of you, and attributes
every byte to the module that emitted it, so prefer it to any figure written
down here. Kernel code is
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
reports minus **91.0KB** — `tools/kernsize.py` prints that figure for the
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
docs/HERCULES-TESTING.md  testing on Hercules - it IS automatable, and all
                     three ways of getting it wrong give a black image
                     rather than an error
boot/boot.asm        512-byte boot sector: LBA->CHS, retrying reads. It
                     relocates itself, because the kernel lands where it runs
kernel/kernel.asm    constants, the memory ladder and its guards, boot
                     sequence, the API jump table, the %includes of every
                     module, size assertions
kernel/*.inc         34 modules. SPEC.md section 4 is the ownership table and
                     the authority on which one owns what - a copy of that
                     list here is a copy that goes stale. The load-bearing
                     ones: vga12 (planar primitives + the drawing lock),
                     vgabb (the software renderer and the 1bpp driver),
                     viddet (which adapter is fitted, and the live geometry),
                     sched (the PIT hook and the context switch), wm
                     (windows, z-order, damage rects), memory (the claim
                     heap), disk/diskw (int 13h, FAT read and write),
                     loader (package validation and launch), driver
                     (loadable drivers), assoc (file associations)
apps/os88api.inc     the package SDK: API offsets, header + icon macros
apps/<name>/         the fifteen shipped packages, one directory each. Each
                     one's design notes are its own SPEC.md section
drivers/<name>/      loadable drivers (.DRV): hard disk, sound, and the
                     serial debug monitor
tests/               every package that is NOT shipped - capability gates
                     and benchmarks. Built only by their own make targets
tools/os88pkg.py     package validator/stamper (.bin -> .o88)
tools/os88disk.py    FAT12 image builder (+ --verify, a structural fsck)
tools/kernsize.py    where the kernel's bytes went, per module, against the
                     budget guards
tools/checkdocs.py   the documentation gate that every `make` runs
tools/qmp.py         QMP client for scripted control of a test boot
tools/mouse.py       absolute mouse positioning over the QMP socket
```

## Software packages

Programs live on a **FAT12** software floppy (drive B:) — and, once os8088 is
installed on one, on a hard-disk partition. Either way it is an ordinary
volume that DOS, Windows, macOS and Linux can all mount, read and write.
os8088 reads and writes them too, and treats every byte on them as untrusted:
the boot sector's BPB is validated rule by rule before any number off it is
used, and files are read by walking their real FAT cluster chains, so a
`.o88` a host OS wrote back fragmented still loads. Files that aren't
packages list with a generic icon, or open in whatever program claims their
extension. The build:

```
apps/mines/mines.asm --nasm--> build/mines.bin    org 0, header baked in
build/mines.bin --os88pkg.py--> build/mines.o88   validated and stamped
build/*.o88 --tools/os88disk.py--> build/apps.img FAT12 floppy (and apps360.img)
```

A package is written against `apps/os88api.inc`: `OS88_HEADER 'NAME', entry`
emits the header, `OSAPI_*` constants name the kernel's jump-table entries
(gfx primitives, fonts, windows, files, sound, ticks, a PRNG), and
`OS88_IMAGE_END` seals the image with its size and loader-zeroed bss.

**There is no relocation of any kind**, and that is what lets several run at
once: a package is assembled once at `org 0` and loaded into a
paragraph-aligned claim off the heap, which becomes its own CS = DS. Nothing
in the image depends on where it landed, so `os88pkg.py` is a validator
rather than a generator. The ceiling on one package is 60KB — that is the
*segment*, not a pool; the pool it used to be allocated from was retired, and
deleting it is what returned 60KB to every machine and made a 128KB machine
viable at all.

At load time the kernel reads the file in, zeroes bss and calls the entry,
which registers a window and returns; from then on the program is
event-driven — its paint/key/click procs are called like any built-in
window's — and from one of those it can claim a single pre-empted worker task
of its own. Closing a package frees its claim, its task and its instance
slot.

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

`make xt-cga` and `make xt-hercules` are the same 256KB XT with the other two
adapters os8088 supports (`vm/xt-cga`, `vm/xt-hercules`) — CGA, and the
Hercules mono card of 1982. **These two are the only way to exercise the
adapter detection probe at all**: QEMU has no Hercules card, so `make test
VIDEO=cga` drives the mono renderer but never the code that works out which
card is fitted. A drawing change is not checked on 1bpp until it has been
looked at here — grey rounds to black on both, so a greyed-out menu item is
a checkerboard rather than a pale one.

The other end of the range: `make 286`, `make 386sx`, `make 386`, `make 486`
and `make pentium` boot the 1.44MB image on AT-class 86Box machines — an AMI
286 clone board at 12.5MHz with 1MB (`vm/286`), a Shuttle HOT-304 386SX at
16MHz with 2MB (`vm/386sx`), a Micronics 386DX at 25MHz with 2MB
(`vm/386dx`), an AMI SiS-471 486DX2 at 66MHz with 8MB (`vm/486`) and an ASUS
430FX Pentium P54C at 133MHz with 16MB (`vm/pentium`) — all with the same
OTI-067 VGA and serial mouse, and the last two with a Sound Blaster 16.
os8088 is 8086 code in real mode, so every one of them runs it verbatim, just
faster; the extra megabytes stay invisible, because int 12h still answers
640K and the OS never leaves real mode. That is what the fast end is *for*:
everything sized while looking at a 4.77MHz 8088 — typematic deadlines, the
tracker's ring refill, Arkanoid's frame pacing — also has to behave on a
machine two orders of magnitude quicker, which QEMU's untimed execution
cannot answer either. (Not `ibmat` for the 286: 86Box caps the real 5170
planar at 512KB, the same silent clamp as the XT. Nor a bare `pentium`:
that is not a family name, and 86Box quietly falls back to a 75MHz P54C, so
a config claiming a P133 boots a P75. And unlike an XT, these machines have a
CMOS — on the first launch the BIOS stops at its setup screen, and picking
"EXIT FOR BOOT" once writes `vm/<machine>/nvr/`.)

`make xt-sound`, `make 286-sound` and `make 386-sound` add a sound card to
three of the machines above: a Sound Blaster 2.0 on a 640KB XT
(`vm/xt-sound`), so the OPL2 is the FM tier and the DSP the streaming tier on
the CPU this OS is actually for, and an SB16 on the 286 and the 386
(`vm/286-sound`, `vm/386-sound`). `make test ADLIB=1` and `SB16=1` give the
driver a card to attach to under QEMU, but only these give it one on a
machine whose bus and clock are period-correct — and pacing a stream is the
one thing that means nothing anywhere else.

All twelve, at a glance:

| target | machine | CPU | RAM | video | sound |
|---|---|---|---|---|---|
| `xt` | IBM PC/XT | 8088 @ 4.77MHz | 256KB | OTI-067 VGA | — |
| `xt-640` | XT, 1986 board | 8088 @ 4.77MHz | 640KB | OTI-067 VGA | — |
| `xt-cga` | IBM PC/XT | 8088 @ 4.77MHz | 256KB | CGA | — |
| `xt-hercules` | IBM PC/XT | 8088 @ 4.77MHz | 256KB | Hercules | — |
| `xt-sound` | XT, 1986 board | 8088 @ 4.77MHz | 640KB | OTI-067 VGA | Sound Blaster 2.0 |
| `286` | AMI 286 clone | 286 @ 12.5MHz | 1MB | OTI-067 VGA | — |
| `286-sound` | AMI 286 clone | 286 @ 12.5MHz | 1MB | OTI-067 VGA | Sound Blaster 16 |
| `386sx` | Shuttle HOT-304 | 386SX @ 16MHz | 2MB | OTI-067 VGA | — |
| `386` | Micronics 386 | 386DX @ 25MHz | 2MB | OTI-067 VGA | — |
| `386-sound` | Micronics 386 | 386DX @ 25MHz | 2MB | OTI-067 VGA | Sound Blaster 16 |
| `486` | AMI SiS 471 | 486DX2 @ 66MHz | 8MB | OTI-067 VGA | Sound Blaster 16 |
| `pentium` | ASUS P/I-P55TP4XE | Pentium P54C @ 133MHz | 16MB | OTI-067 VGA | Sound Blaster 16 |

The XT-class machines boot the 360KB pair; the AT-class ones boot the 1.44MB
pair. None of them can be scripted — 86Box has no automation socket, so
these are all interactive, and `make test` over QMP remains the only way to
drive the system from a script.

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

MIT -- see [LICENSE](LICENSE). No third-party code is vendored into the OS, so
the whole tree is covered by that one license. As noted at the top, the code
was written with AI coding agents; it is an experimental hobby project and, per
the MIT text, comes with no warranty of any kind.

The website in the sibling `os8088-web` repository is a separate matter: it
vendors the v86 emulator (BSD-2-Clause), SeaBIOS and SeaVGABIOS binaries
(LGPLv3) and a bitmap font (CC BY-SA 4.0), each shipped with its own license.
