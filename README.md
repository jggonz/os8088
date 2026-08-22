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
make          # build all seven floppy images
make run      # boot it in QEMU (with an emulated serial mouse)
make run-640  # the same, on a 640KB machine
make run-720  # the same, off the 720KB pair
make xt       # boot the 360KB image on an emulated IBM PC/XT in 86Box
make xt-640   # the same XT with a full 640KB of RAM
make xt-cga   # the same XT with a CGA card instead of VGA
make xt-hercules  # ...and the same XT with a Hercules card
make xt-multimon  # ...and an XT with BOTH mono cards, a monitor window each
make 286      # 86Box: 286 @ 12.5MHz, 1MB, VGA
make 386sx    # 86Box: 386SX @ 16MHz, 2MB, VGA
make 386      # 86Box: 386DX @ 25MHz, 2MB, VGA
make 486      # 86Box: 486DX2 @ 66MHz, 8MB, VGA, Sound Blaster 16
make pentium  # 86Box: Pentium @ 133MHz, 16MB, VGA, Sound Blaster 16
make xt-sound # the 640KB XT with a Sound Blaster 2.0 (OPL2 + DSP)
make xt-sound-1.44 # the 640KB XT with SB 1.0 and every app on a 1.44MB B:
make 286-sound  # 86Box: the 286, with a Sound Blaster 16
make 386-sound  # 86Box: the 386DX, with a Sound Blaster 16
make worddisk # build the Microsoft Word floppy, all three geometries
make xt-word  # 86Box: the 640KB XT with the Word disk in B:
make 386-word # 86Box: the 386DX with the Word disk in B:
make zdisk    # build the Frotz story floppies (fetches the stories first)
make xt-z     # 86Box: the 640KB XT with a story disk in B:
make 386-z    # 86Box: the 386DX with a story disk in B:
make cworddisk  # build the floppy for the word processor written in C
make 386-c-word # 86Box: the 386DX with that disk in B:
make runcpmdisk # build the RunCPM floppies - the CP/M 2.2 emulator, its
                # overlay, DR's CCP, the master disk as CP/M drive A, and
                # the CP/M games and applications beside it (make cpmsw
                # fetches those; the 1.44MB disk carries the most)
make xt-runcpm  # 86Box: the 4.77MHz XT with the 360KB RunCPM disk in B:
make 286-runcpm # 86Box: the 12.5MHz 286 with the 720KB one - arcade games
make 386-runcpm # 86Box: the 386DX with the 1.44MB one - everything
make c64disk  # build the C64 floppy - a Commodore 64: the package, its
              # overlay and C64.ROM, the KERNAL/BASIC/CHARGEN sidecar
              # (make c64rom builds that from the ROMs in apps/c64/rom/)
make c64disk  # ...in all three geometries: c64.img, c64720.img, c64360.img
make xt-c64   # 86Box: the 4.77MHz XT with the 360KB C64 disk in B: - where
              # the speed figure on the status row is the one that matters
make 286-c64  # 86Box: the 12.5MHz 286 with the 720KB one
make 386-c64  # 86Box: the 386DX with the 1.44MB one
make allapps  # one 1.44MB floppy with every program on it, both word
              # processors, Frotz and RunCPM included
make test     # boot headless with a QMP socket for scripted testing
make debug    # boot with QEMU halted, waiting for gdb on :1234
make marty    # a cycle-accurate IBM 5150 (MartyPC) with a debugger attached -
              # memory, registers, breakpoints, single-step, cycle counts,
              # and no code in the guest. THE FIRST TOOL TO REACH FOR when
              # what you are testing runs on an 8088 (docs/MARTYPC-DEBUG.md)
make clean
```

`make` builds the six shipping floppies and needs nothing but `nasm` and
`python3`. The three disks written in C — `cworddisk`, `runcpmdisk` and
`allapps`, which carries both — want the compiler first: `tools/setup-cc.sh`
fetches and builds it into `build/cc`, and nothing else in the tree depends on
it. `runcpmdisk` and `allapps` also fetch RunCPM's command processor and
master disk (`make runcpm-src`), and `runcpmdisk` the CP/M software that
rides beside it (`make cpmsw`); none of it is committed here.

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
- **Typefaces** — **ten** `.F88` faces in `FONTS/` on the system disk, found
  at run time by any app that asks: Charter and the house 8x8 cell, a Times, a
  Helvetica and a Courier, two more text faces and three monospaces, each
  fitted onto an 8-pixel grid from an open outline font (SPEC.md 6.4.1). The
  kernel keeps its 8x8 cell for chrome; an app composes a row in a real face
  and puts it down in one call.

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

Eighteen loadable packages ship on the software disk, all closable and most
multi-instance:

- **Apps** — Note Pad (word wrap, DOS-readable text files), TeXPad, Paint,
  ArtfulType, Fractal, Calculator, Piano, Recorder, Tracker and ModPlug Player
  (both play Amiga MOD files).
- **Games** — Minesweeper, Solitaire, Arkanoid, Missile Command, Cyclone 88
  and TameGram.
- ...plus the Task Manager itself, and HELLO, a minimal package that exists to
  be the smallest thing the SDK can build.

**TeXPad** is the newest of them and a contributed one: a two-pane pad for a
small, paper-oriented subset of TeX — source on the left, the typeset page on
the right, and File > Export writing PDF 1.4 or PostScript Level 1 that a host
reader opens. The dialect is text, sections, quotes, lists and ruled tables,
with paper size, margins, gutter, facing pages and page numbers on the menus;
there is no math engine, no figures and no colour. `PAPER.TEX` and `GUIDE.TEX`
ride the software disk beside it, and `GUIDE.TEX` is the markup written up as
a document TeXPad sets.

Timer and Bounce are built into the kernel rather than loaded. **Drivers**
load the same way packages do — hard disk, sound, and a serial debug
monitor.

**Frotz** ships separately, on a story floppy of its own (`make zdisk`): an
interpreter for Infocom's Z-machine, v1–v8, windowed, with a scrollback,
sound and pictures. It is not a port of David Griffith's Frotz — this tree
has no C toolchain — but an independent implementation of the Z-Machine
Standard 1.1 in 8086 assembly, named in the same tradition as `dfrotz`. The
whole story stays resident (a floppy seek costs ~400ms here, so paging would
mean minutes per turn), which is why it wants a 640KB machine and says so
plainly when a story will not fit. **No story file is committed to this
repository** — `tools/getstories.py` fetches fifteen freely-released ones
into `build/`, and `STORIES=` puts your own beside them.

**Microsoft Word 1.1A** ships separately too, on a document floppy of its own
(`make worddisk`): a reimplementation of Word for Windows 1.1a ("Opus") as a
native package — Draft and Page views, a two-row ruler, real `.DOC` files in
the Word for Windows 1.x/2.x binary format, RTF in and out, wildcard Search,
Sort, Renumber and Table of Contents. Its **Font menu is built from the
disk**: it lists whatever `FONTS/` is carrying — ten families as shipped, from
Times to JetBrains Mono — and choosing one sets the whole document in it. The faces are set at fixed pitch for now — their
shapes, their height and their leading, but eight pixels a character. It is not a recompile: Opus is pcode
built against the Windows 2.x API, none of which exists here, so the UI
definition is mined from the Computer History Museum's source release and
every menu string is verbatim from it. The disk carries `WORD.O88`,
`WORD.OVL`, a generated `WELCOME.DOC` and an empty `DOCS\`. **`all` does not
build it and no shipped disk grows a byte** — `make wordcheck` is the format
gate, which round-trips the `.DOC` through an independent host-side reader.

**Two more ship on disks of their own, and both are written in C** (below):
`make cworddisk` builds a second Word 1.1a that saves RTF, and `make
runcpmdisk` builds **RunCPM**, a CP/M 2.2 emulator in a window — a Z80 with
Digital Research's own command processor at the `A>` prompt, its drives kept
as folders on the floppy, and RunCPM's master disk in drive A so MBASIC, PIP,
SUBMIT, TE and Z80ASM run — with **CP/M games and applications beside it**:
LADDER, CATCHUM and PM, Nemesis and Dungeon Master, GAINA, WordStar 3.30 and
Turbo Pascal 3.01A, as much of it as each geometry holds. `make allapps` puts
every one of these on one 1.44MB floppy.

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
| graphics      | VGA mode 12h, 640x480x16 planar, drawn directly by default (the real Mac drew directly too). Set/Reset + Bit Mask fills, XOR for drag outlines and menu highlights. **CGA and Hercules are the same binary**: the adapter is probed at boot, a 1bpp renderer takes over, and the live screen size is read at runtime rather than assumed — so anything that clips or anchors to an edge is right on all three. |
| multitasking  | round-robin off int 08h (PIT, 18.2Hz): chain to the BIOS tick, then save the register frame on the task stack, swap SP, and iret into the next ready task. 12 task slots, 512-byte stacks (sized against a measured 150-byte high-water mark). Pre-emptive by default; in cooperative mode the tick declines to switch and a task runs until it yields, sleeps or exits — with a ~1s watchdog so a runaway one can't take the machine with it. |
| mouse         | Microsoft serial mouse on **COM1 or COM2** (IRQ4 / IRQ3), 1200 baud 7N1, 3-byte packets — the period-correct XT mouse. The port is neither asked nor configured: both are probed for a UART, every one that answers is listened to at once, and the first to deliver a run of clean packets wins — so the other port stays free for the modem that is usually on it. QEMU emulates a mouse natively (`-chardev msmouse`); `make run MOUSEPORT=com2` puts it on the second port. |
| cursor        | arrow with save-under, drawn by the mouse ISR itself when it's safe, deferred to the next unlock when a task holds the drawing lock. |
| keyboard      | BIOS int 16h, polled by the UI task. |
| font          | two answers, and the second is new. **System chrome** is the VGA ROM's own 8x8 cell, copied out via int 10h AX=1130h at boot — one glyph, one byte-aligned store, and the fast path every menu, title and dialog is priced against. **An application** can set type in a real typeface instead: `FONTS/` on the system disk carries `.F88` faces, `apps/os88type.inc` composes a whole row of glyphs into a 1bpp band in the app's own RAM, and **one** kernel call puts the band on the screen. That split is the whole design — a proportional pen can never reach the 8x8 fast path, so lettering a 104-glyph line one glyph at a time would be 79ms of per-call *floor* on an XT before a pixel moved; composed and emitted once it is one floor, and measured it matches the 8x8 row it replaces. Glyph data, metrics, wrap and hit-testing live in the packages that want them, so the second and fifth face cost the kernel nothing. |
| disks         | BIOS int 13h, with retries — reads and writes share one routine, so the CHS math and the retry policy can't drift apart, and contiguous clusters coalesce into one transfer because a call costs roughly a disk revolution whatever it moves. Task switching pauses during a transfer (the tick still runs — the floppy motor needs it). FAT12 and FAT16, on floppies and on hard-disk partitions. |
| software      | `.o88` packages on plain FAT volumes — any PC, Mac or Linux box can read and write the disks, and so can os8088: apps create, replace, rename and delete files through the API, and the kernel validates every byte it reads off a disk before any of it becomes an address. A package is a flat binary assembled at `org 0` and loaded into a paragraph-aligned **claim off the heap**, which is **its own address space**, one segment per package — so there are no relocations of any kind, and `tools/os88pkg.py` is a validator rather than a generator. It calls the kernel through a fixed table of far-call cells at 0060:0010, and the kernel calls back through a three-byte dispatcher in the package's header. Several packages, or several copies of one, run at once. |
| concurrency   | one drawing mutex (`gfx_lock`); background tasks re-check visibility *under* the lock and then arm a clip region — their window's content rect less every window above it — so a covered window draws the part that shows instead of skipping the frame; ISRs run IF=0 throughout and never draw over a held lock. SPEC.md is the binding contract. |

The kernel is ~54KB of code and data, and 103KB all in — buffers, stacks,
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
                     softgfx (the software renderer and the 1bpp driver),
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

### A package can also be written in C

The OS itself is assembly and stays that way. But a **package** can be written
in C, and two are. The first is `apps/cword` — a second reimplementation of **Microsoft Word
1.1a**, in C this time, with the same nine-menu bar, ribbon, ruler and status
line as `apps/word` and RTF as its file format. It has both of the product's
views — Draft, which wraps to the window, and Page, which wraps to the sheet —
and its **Font box lists the typefaces on the system disk** and sets the
document in the one you pick, proportional metrics and all.

It does not fit in one segment, and that is the interesting part. A package's
image and bss share 60KB and that ceiling IS the segment; C costs two to three
times the image of hand assembly for the same work. So a good deal of it lives
in `CWORD.OVL`, a file beside the package that is read into a heap claim the
first time a dialog, a file command, the clipboard, the ruler or the Font list
is asked for — the split falling exactly between what a keystroke touches and
what a menu command touches. When the typefaces went in, that line is what
moved: the resident half got *smaller* while the program grew a view and a text
engine.

The second C package is `apps/runcpm` — a reimplementation of
[RunCPM](https://github.com/MockbaTheBorg/RunCPM) 6.9 (Marcelo Dantas /
Mockba the Borg, MIT), a **CP/M 2.2 emulator in a window**: a Z80 written in
8086 assembly running in a 64KB claim of its own, the BDOS and BIOS in C, CP/M
drives as folders on the floppy (`A\0`, `A\1`, `B\0`…), Digital Research's
own CCP at the `A>` prompt, and an 80×25 terminal drawn damage-only through a
glass shadow — one composed row band per changed row, so a keystroke's echo is
one drawing call. It passes ZEXDOC, the Z80 instruction exerciser, from the
prompt (`make rczex`), and MBASIC, TE, PIP, SUBMIT and Z80ASM off RunCPM's
master disk run on it. It is the port that gave the kernel its wake event —
the emulator runs on the UI task in bounded slices and never blocks, so a
program waiting for a key costs nothing — and it too spills into a second
segment, `RUNCPM.OVL`, split by frequency: what a record, a console byte or a
keystroke touches stays resident, and the per-command half of the disk layer
goes out. Nothing of RunCPM's is committed: `tools/getruncpm.py` fetches the
CCP and the master disk at a pinned commit and `make runcpmdisk` builds the
three floppies from them (the 360KB one curated to the programs and texts,
with a `LEFT-OFF.TXT` naming what it leaves off).

**And there is software to run on it.** `tools/getcpmsw.py` fetches CP/M
games and applications from the public RunCPM software collection — every
file pinned by id, SHA-256 and size, nothing committed, the collection's own
`<DRIVE>/<USER>` coordinates kept — and each floppy takes what it holds, an
area whole or not at all, every one of them a **user area of drive A** so
that the emulator's own icon stays on the first screen of the Disk window:

| disk | machine | CP/M software |
|---|---|---|
| `build/runcpm.img` (1.44MB) | `make 386-runcpm` — 386DX/25 | **all of it**, in drive A's user areas: `USER 5` LADDER, CATCHUM, PM · `USER 6` Nemesis, Dungeon Master, Castle · `USER 7` GAINA · `USER 8` Turbo Pascal 3.01A · `USER 9` WordStar 3.30 — beside 59 files of the master disk |
| `build/runcpm720.img` (720KB) | `make 286-runcpm` — 286 at 12.5MHz | `USER 5`, the arcade games, beside 70 files of the master disk |
| `build/runcpm360.img` (360KB) | `make xt-runcpm` — 4.77MHz XT | no room for games: the master disk's programs and texts, and a `GAMES.TXT` saying where they are |

Two things a session needs to know, both on the disk in `GAMES.TXT`. **Tell
the games they are on a `10) Heathkit/Zenith H19 (ANSI)` terminal**, not the
VT-100 entry — LADDER's and CATCHUM's VT-100 setting emits ANSI coordinates
biased by 32, which a real VT-100 wraps just as ours does (checked against
upstream RunCPM through a VT-100 model). And **the machine is the play
speed**: nothing throttles the emulated Z80 — upstream has no limiter either
— so an arcade game is unplayably fast under QEMU on a modern host and runs
at period speed on the three 86Box machines above. Zork, Hitchhiker and
Colossal Cave are not there and cannot be: their data files are 76KB, 113KB
and 68KB, and this port opens a file whole through a 16-bit count.

The compiler is [SmallerC](https://github.com/alexfru/SmallerC) (2-clause
BSD), pinned to one commit and **fetched rather than vendored** — it is not in
this repository, `tools/setup-cc.sh` builds it into the gitignored `build/cc`,
and nothing in `make` depends on it. It was chosen because it emits **NASM
source**, so the output drops into the existing `nasm -f bin` pipeline and
there is still no linker anywhere in the tree.

`tools/cc8086.py` stands between the compiler and the assembler. It lowers the
seven 80186/80386 forms SmallerC emits into 8086 instructions — differentially
tested against a real x86 under QEMU, 428 cases — and **refuses four
constructs outright**, because each is silently wrong here rather than merely
unsupported:

- **the address of a local.** `SS` is not `DS` in this OS, so `&x` on an
  automatic is a stack offset that every later dereference resolves through
  the package's own segment. Every addressable object must be `static`.
- **string instructions.** They address `ES:DI` and `ES` is the kernel's
  segment, so a compiler-emitted `rep movsb` writes into the kernel. This also
  rules out struct assignment, struct arguments and struct returns by value,
  which is how the compiler implements them.
- **`long`, `float`, `double`, bit-fields and anonymous unions.** There is no
  32-bit integer in 16-bit mode; `float` is worse than absent, because it
  compiles and is silently two bytes wide.
- **stack frames over 96 bytes.** The UI task's whole stack is 1,024 bytes and
  a background task's is 256.

The 60KB per-package ceiling is unchanged and C reaches it two to four times
sooner, so a C package is a small package. Everything else — the header, the
loader, the disk format, the 60KB check — cannot tell a C package from an
assembly one, which is the test of whether it was done right.
**[docs/C-TOOLCHAIN.md](docs/C-TOOLCHAIN.md)** is the guide;
[SPEC.md](SPEC.md) §73 is the contract.

**Porting something else the same way is a skill.** The CWORD port took four
sessions of finding out what the toolchain, the 60KB segment and a 4.77 MHz
8088 would and would not allow, and everything it learned is written down in
[`.claude/skills/port-to-os8088/`](.claude/skills/port-to-os8088/) as a
Claude Code skill. Type `/port-to-os8088` in a session on this repo, point it
at the original program's source — it offers to scan the directories next to
this one for repositories, or to clone a list you give it — and it scouts the
source and this tree with a team of agents, drafts a plan, asks you only the
questions that are yours to answer (the name, a scope cut, which of two file
formats), then builds the port one wave at a time, each wave reviewed,
verified on QEMU and committed, and opens the pull request. It runs on Opus 5
or Fable 5. **[CONTRIBUTING.md](CONTRIBUTING.md#porting-a-program-with-the-agent)**
says how to use it, and `LESSONS.md` inside the skill is worth reading on its
own even if you never run it: it is the list of everything that assembles
cleanly and runs wrong when C meets this machine.

## Three geometries of everything

| image                  | geometry                 | for                             |
|------------------------|--------------------------|---------------------------------|
| `build/os8088.img`     | 1.44MB, 18 spt, 2 heads  | QEMU boot floppy (A:)           |
| `build/os8088-720.img` | 720KB, 9 spt, 2 heads    | 3.5" DD / USB floppy / Gotek    |
| `build/os8088-360.img` | 360KB, 9 spt, 2 heads    | 86Box / real XT boot floppy     |
| `build/apps.img`       | 1.44MB FAT12             | QEMU software floppy (B:)       |
| `build/apps720.img`    | 720KB FAT12              | 3.5" DD software floppy         |
| `build/apps360.img`    | 360KB FAT12              | 86Box / real XT software floppy |
| `build/media360.img`   | 360KB FAT12              | 360KB media floppy — the shipped module, which the 360KB apps disk has no room for |
| `build/zork*.img`      | 1.44MB / 720KB / 360KB   | Frotz story floppies (`make zdisk`) |
| `build/word*.img`      | 1.44MB / 720KB / 360KB   | Microsoft Word floppies (`make worddisk`) |
| `build/cword*.img`     | 1.44MB / 720KB / 360KB   | Word in C, package + `CWORD.OVL` (`make cworddisk`) |
| `build/runcpm*.img`    | 1.44MB / 720KB / 360KB   | RunCPM, package + `RUNCPM.OVL` + CP/M drive A + the games and applications each holds (`make runcpmdisk`) |
| `build/apps-all.img`   | 1.44MB FAT12             | every program on one floppy, the four above included (`make allapps`) |

The boot sector takes its geometry from `-DSPT` / `-DHEADS` at assembly
time and reads exactly as many sectors as the measured kernel occupies.
A 1.44MB drive postdates the 8086 by years, so period hardware gets the
360KB build.

**No binary is committed to this repository.** `build/` is gitignored
outright — the seven images, the kernel, the boot sectors, the drivers and
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

`make xt-sound`, `make xt-sound-1.44`, `make 286-sound` and `make 386-sound`
add a sound card to four of the machines above. The first XT has a Sound
Blaster 2.0 (`vm/xt-sound`); the 1.44MB variant has a Sound Blaster 1.0 and
mounts `build/apps-all.img` in B: (`vm/xt-sound-1.44`), putting every
application on the same 4.77MHz machine. The 286 and 386 use an SB16
(`vm/286-sound`, `vm/386-sound`). `make test ADLIB=1` and `SB16=1` give the
driver a card to attach to under QEMU, but only these give it one on a
machine whose bus and clock are period-correct — and pacing a stream is the
one thing that means nothing anywhere else.

All targets, at a glance:

| target | machine | CPU | RAM | video | sound |
|---|---|---|---|---|---|
| `xt` | IBM PC/XT | 8088 @ 4.77MHz | 256KB | OTI-067 VGA | — |
| `xt-640` | XT, 1986 board | 8088 @ 4.77MHz | 640KB | OTI-067 VGA | — |
| `xt-cga` | IBM PC/XT | 8088 @ 4.77MHz | 256KB | CGA | — |
| `xt-hercules` | IBM PC/XT | 8088 @ 4.77MHz | 256KB | Hercules | — |
| `xt-sound` | XT, 1986 board | 8088 @ 4.77MHz | 640KB | OTI-067 VGA | Sound Blaster 2.0 |
| `xt-sound-1.44` | XT, 1986 board | 8088 @ 4.77MHz | 640KB | OTI-067 VGA | Sound Blaster 1.0 |
| `286` | AMI 286 clone | 286 @ 12.5MHz | 1MB | OTI-067 VGA | — |
| `286-sound` | AMI 286 clone | 286 @ 12.5MHz | 1MB | OTI-067 VGA | Sound Blaster 16 |
| `386sx` | Shuttle HOT-304 | 386SX @ 16MHz | 2MB | OTI-067 VGA | — |
| `386` | Micronics 386 | 386DX @ 25MHz | 2MB | OTI-067 VGA | — |
| `386-sound` | Micronics 386 | 386DX @ 25MHz | 2MB | OTI-067 VGA | Sound Blaster 16 |
| `486` | AMI SiS 471 | 486DX2 @ 66MHz | 8MB | OTI-067 VGA | Sound Blaster 16 |
| `pentium` | ASUS P/I-P55TP4XE | Pentium P54C @ 133MHz | 16MB | OTI-067 VGA | Sound Blaster 16 |
| `xt-multimon` | XT, 1986 board | 8088 @ 4.77MHz | 640KB | CGA **and** Hercules | — |
| `xt-z` | XT, 1986 board | 8088 @ 4.77MHz | 640KB | OTI-067 VGA | Sound Blaster 2.0 |
| `386-z` | Micronics 386 | 386DX @ 25MHz | 2MB | OTI-067 VGA | Sound Blaster 16 |
| `xt-word` | XT, 1986 board | 8088 @ 4.77MHz | 640KB | OTI-067 VGA | — |
| `386-word` | Micronics 386 | 386DX @ 25MHz | 2MB | OTI-067 VGA | — |

The XT-class machines boot the 360KB system disk; most pair it with the 360KB
apps disk, while `xt-sound-1.44` mounts the everything disk in a 1.44MB B:
drive. The AT-class machines boot the 1.44MB pair.

`xt-multimon` is the **two-card** XT — a CGA and a Hercules, a monitor window
each — and the only 86Box machine that can show the extended desktop. It boots
Single; Control Panel → Display → Desktop is what extends it.

The last four put a **dedicated floppy** in B: instead of the apps disk.
`xt-z` and `386-z` are the Frotz machines and take a **story floppy** — `xt-z`
with a 720KB 3.5" DD drive (360KB does not hold a library), `386-z` with two
1.44MB drives and a second library disk to swap in. Both carry the full 640KB,
because a Z-machine story is resident. `xt-word` and `386-word` are the Word
machines and take the **document floppy** `make worddisk` builds; `xt-word`
uses a 720KB 3.5" DD drive, `386-word` two 1.44MB drives.

None of them can be scripted — 86Box has no automation socket, so
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

MIT -- see [LICENSE](LICENSE). As noted at the top, the code was written with
AI coding agents; it is an experimental hobby project and, per the MIT text,
comes with no warranty of any kind.

Two things in the tree are not simply covered by that, and both are named
rather than buried:

- **The C compiler is not vendored.** SmallerC (2-clause BSD) is fetched at a
  pinned commit into the gitignored `build/cc` by `tools/setup-cc.sh` and is
  never committed here. What is committed is the hash and `tools/cc8086.py`.
- **Two of the word processors draw on the Microsoft Word for Windows 1.1a
  source code** ("Opus"), released by the Computer History Museum in 2014.
  `apps/word` takes its menu strings, ribbon and ruler layout, dialogs and key
  assignments from that release and writes real Word `.DOC` files built out of
  its documented structures; `apps/cword` takes the same menus, keys, ribbon,
  ruler and status fields, and transcribes RTF keyword and property tables from
  it — `cwrtftbl.c`, `cwrtftbl.h`, and the interpreter shape in `cwrtfio.c`. Those files carry a header naming **Microsoft Corporation** as
  copyright holder, the CHM release as the source, and the specific Opus file
  each table came from. **The CHM release carries no licence grant**; it is
  published for study, and every file in it is headed "COPYRIGHT (C) 1987
  MICROSOFT". Anyone redistributing this repository, or a floppy image built
  from it, should be aware of that and decide for themselves. The rest of both
  programs — their windows, their layout engines, their redraw paths — is
  os8088's own.

The website in the sibling `os8088-web` repository is a separate matter: it
vendors the v86 emulator (BSD-2-Clause), SeaBIOS and SeaVGABIOS binaries
(LGPLv3) and a bitmap font (CC BY-SA 4.0), each shipped with its own license.
