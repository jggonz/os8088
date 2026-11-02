# 86Box, driven - the instrument for machines MartyPC is not

**86Box CAN be scripted, read and debugged now**, and every document in this
tree that says otherwise was written before upstream 86Box grew an
**automation socket** (`doc/specifications/automation.md` in its tree) and was
built here with it. The claim *"86Box has no debugger and no automation
socket, so a session can start one and cannot read the result"* stood for
months and was true of the macOS app every `make xt`-style target launches.
It is not true of the build `tools/86box/build.sh` makes:

| what | how |
|---|---|
| run it with **no display at all** | Qt's `offscreen` platform; no Xvfb, no window manager |
| **hold** the machine and run it by an exact amount of emulated time | `step MS` - faster than real time, ~7x on a four-core box |
| read the **screen** | `shot` - the frame the card drew, as a PNG, no window chrome |
| read the **text screen** as characters | `text` (BIOS setup, POST messages, a text-mode program) |
| read and write **physical memory** | `memr` / `memw` - so a kernel symbol is readable by name (`sym`, `pointer`) |
| **keys** and the **mouse** | set-1 scancodes; relative mouse packets into whatever mouse the machine has - the serial `msserial` included |
| **reset**, change a **floppy**, power off | `reset hard`, `fdd`, `quit` |
| count **every CPU reset**, a guest triple fault included | `info`'s `soft_resets` and `last_reset_ms` |
| **where the CPU faulted** | this tree's patch 01: every exception and every shutdown, with CS:IP, to the run's `stderr.log`, resolved against the kernel's nasm map by `faults` |
| a real **debugger** | upstream's GDB stub, on a private loopback port per instance |

`tools/os88box.py` is the driver. It is the same shape as
`tools/os88marty.py` + `tools/os88ui.py` deliberately, but thinner: 86Box is
the instrument for **the machines MartyPC does not model** - a 286 or a 386
running the reset path, an AT BIOS, an SB16, an NE1000, a period IDE disk, the
owner's own `pc5150`-style configurations - and MartyPC stays the default for
anything an 8088 can answer (docs/TESTING.md).

## Build it once per container

```sh
python3 tools/os88box.py build      # = tools/86box/build.sh
```

About three minutes on four cores. It fetches 86Box and its ROM set at the
pins in `tools/86box/UPSTREAM` (shallow, one commit each; the ROMs are ~250MB)
into `build/86box/`, applies `tools/86box/patches/`, and builds with
`-DAUTOMATION=ON -DGDBSTUB=ON` and no dynarec - the interpreter is the CPU
model that faults where a real 286 faults. As root with apt, the preflight
installs the two dozen packages itself (the same gating as
`tools/martypc/build.sh`); anywhere else it prints the command and stops.
`OS88_86BOX` and `OS88_86BOX_ROMS` point the driver at a binary and ROM set
somewhere else.

The patches - the first two inert unless asked for, the third a CORRECTION
of the CPU model:

- **`01-fault-log.patch`** - with `OS88_FAULTLOG=1` (the driver always sets
  it), every exception the CPU raises is printed to stderr *before it is
  delivered* - `OS88ABRT <vector> at CS:IP err <code> msw <MSW> ss sp ds es` -
  every shutdown or reset pulse as `OS88SOFTRESET at CS:IP`, and the reason
  86Box raised each #GP, #SS, #TS and #NP as `OS88WHY`. This is the
  one that matters: without it a triple fault is a reboot and nothing else.
- **`02-gdbstub-norun.patch`** - upstream's stub starts the CPU **stopped**
  and waits for GDB, so with it compiled in every automation `step` came back
  `"stopped":"debugger"` having run nothing. Now it stops only with
  `OS88_GDBWAIT=1` (`launch --gdb-wait`), and it binds loopback rather than
  every interface.
- **`03-cs-exec-check.patch`** - a far JMP or CALL (and `loadcs`) through a
  DATA descriptor raises #GP(selector), as an 80286 does. Upstream tests the
  descriptor's S bit and then treats ANY non-system segment as code, so a
  far transfer through a data selector simply worked - and kern_mega's most
  common completed fault (`mega_gpfix`, a package far-jumping into a claim
  by its identity segment) cost NOTHING here. That made this machine BLIND
  to a whole class of kern_mega slowness: RealDMG's base build ran Kirby's
  Dream Land at 101.5% of real time on `286-mega` while Bochs counted ~35,000
  of those faults a second (docs/REALDMG-SPEC.md section 16.2.4). Bochs
  always had the check; `pmoderetf` and `pmodeiret` had it here already.

Three more serve the port backend (*kern_big rows on 86Box* and *kern_mega
rows on 86Box*, below), and each is inert until something asks for it:

- **`04-sb-capture.patch`** - with `OS88_SBWAV=FILE`, the first Sound
  Blaster's DSP output is written to FILE as a 48 kHz mono 16-bit WAV, sample
  and hold, on the EMULATED clock. MartyPC's `MARTYPC_WAV` capture has the
  same shape, so a row that reads its capture back runs unchanged.
- **`05-automation-verbs.patch`** - two automation verbs. `screenshot1 PATH`
  is the SECOND monitor's buffer (the extended desktop's other card), and
  `fdc` answers `{"reads":N,"writes":N}`.
- **`06-fdc-counters.patch`** - those two counters are the READ DATA and
  WRITE DATA commands the controller completed. They are MartyPC's `disk()`.
- **`07-picomem.patch`** - the PicoMEM, as **its own firmware**: an ISA
  memory-card device (`isamem0_type = picomem`, section `[PicoMEM #1]` with
  `irq`, `sd_image`, `lib`) that is a thin adapter around `libpmhost`, the
  card's firmware compiled for the host by `tools/picomem/build.sh`. The
  library is loaded with `dlopen`, so this patch builds without it; a machine
  that asks for the card without the library logs why and runs without it.
  The firmware's decode is mirrored per 8-port block and per 8 KB block, so
  nothing else on the bus changes. `OS88_PMHOST_LOG=1` puts every command the
  PC sends, with its answer, in `86box.log`, and `OS88_PMHOST=LIB` swaps the
  library, and `OS88_PMSD=IMG` another microSD. The card's NE2000 is on the
  network through a companion NIC, `net_01_card = picomem_ne2k`. The card's
  DAC is mixed in, and `OS88_PMWAV=FILE` captures it at
  its own 49,716 Hz in card time. `usb_mouse = 1` makes 86Box's pointer the
  card's USB mouse (a HID report every 8 ms), for a machine whose own
  `mouse_type` is `none` - `tools/os88box86api.py` sets that itself when a
  profile asks for the card's mouse. `vm/xt-picomem1` is the machine, and
  `vm/xt-picomem1-usbm` the same one with the card's mouse as its only
  pointer. **A 2.x library** (`libpmhost-v2`) takes the card's jumpers as
  `irq3`/`irq5`/`irq6`/`irq7` and its DRQ as `dma`: each IRQ pin reaches
  its line only when jumpered, and the 2.x's real DMA is a DRQ a byte,
  answered from 86Box's 8237 after each card slice. `vm/xt-picomem2` (with
  `-1fd` and `-usbm` twins) is that card as the field 2.x is jumpered, IRQ 7
  and DRQ 1 (docs/plans/completed/PICOMEM-EMU-PLAN.md).

A binary built before these three is refused by the backend, which names
the rebuild (`python3 tools/os88box.py build`).

## Use

```sh
python3 tools/os88box.py launch 286-mega --hold     # a vm/ name, a dir, or a .cfg
python3 tools/os88box.py 286-mega step 30000        # 30 s of emulated time
python3 tools/os88box.py 286-mega shot out.png
python3 tools/os88box.py 286-mega to 594 112        # os8088's pointer, by pixel
python3 tools/os88box.py 286-mega click 2           # double-click
python3 tools/os88box.py 286-mega type "12*12"; python3 tools/os88box.py 286-mega key return
python3 tools/os88box.py 286-mega faults            # what the CPU did that it should not
python3 tools/os88box.py 286-mega quit              # power off - SAVES THE CMOS
```

From Python:

```python
import sys; sys.path.insert(0, "tools")
import os88box
b = os88box.launch("286-mega", hold=True)
b.cmd("step 30000")
b.to(594, 112); b.click(2); b.pause(4000)
b.shot("disk.png")
print(b.cmd("info")["soft_resets"])
```

Every instance gets its own run directory, `build/86box/run/<name>/`, holding
its config, **copies** of its disk images (a guest write never reaches
`build/`; `--inplace` when it should), its CMOS (`nvr/`), `86box.log`,
`stderr.log` (the fault log) and the socket. Two instances with different
`--name`s never meet - separate sockets, separate GDB ports.

`launch` takes the `vm/` profiles as they are - `vm/286-mega` is the fork
owner's mr286 (16 MHz 286, 4MB, OTI-067, SB16, NE1000, IDE) with `mega.img`,
`apps360.img` and `media360.img` in A:, B: and D: - and rewrites only what a
container needs: image paths resolved against the profile's own directory
(a Windows path that does not exist here becomes an empty drive, said so on
stderr), `image_history` dropped, the software renderer, the confirmation
dialogs off. `--a`/`--b`/`--c`/`--d` replace the images, `--mem` the RAM and
`--set 'Section.key=value'` anything else. A CD-ROM's disc
(`cdrom_01_image_path`, a `.cue` or an `.iso`) is resolved against the
profile and NOT copied - a `.cue` names its `.bin` relative to itself - and
`--cd` replaces it; left relative, 86Box looked for it in the run directory
and dropped the drive without a word (KERN-MEGA-PLAN 14.68).
`vm/286-mega-cdrom` and `vm/386-mega-cdrom` are kern_mega with a CD-ROM and
`tests/megacdrom.py` is the row that drives the first.
`vm/286-mega-ems` is `vm/286-mega` with 86Box's Lo-tech EMS board
(`isamem0_type = lotechems`: registers 260h, frame E000h, 2 MB) added and
nothing else changed - the one machine here that gives kern_mega expanded
memory - and `tests/vidrmbems.py` (SPEC.md 107.8) is its row. Like
`vm/286-hma` it has no make target: its row builds its own disks.

### `to` and `pointer`: why the pointer is closed-loop

86Box's mice are relative, and os8088's pointer is whatever the kernel made of
the packets. `to X Y` reads `mouse_x`/`mouse_y` out of the kernel through the
nasm map (`build/kernel.map`, or `build/megak/kernel.map` when the A: image is
named `mega*`; `launch --map` for anything else), sends a packet, and reads
again until they agree. On the mr286 a 200-pixel packet stream moved the
pointer 198 pixels, so it converges in a round or two. `sym NAME` prints the
linear address of any `.text`/`.bss` kernel symbol, for `memr`; anything in
`.cold` or `.lowbss` wants `tools/os88sym.py`.

### Waiting on the guest, not on the host

A held machine runs only when asked, so every wait is a `step` - and a
fixed one is paid in full whether the guest needed it or not. `Box` has the
gates `tools/os88marty.py` grew (docs/plans/completed/SOAK-SPEED.md, round 3):

| | |
|---|---|
| `kernel_up(prompts=)` | the boot gate on KERNEL STATE: `desk_rows`, `menu_nbar`, `[spl_live]` 0 and `[ticks]` moving, then `ui_done`. PROMPTS answer a BIOS on the way - the ami286 from a fresh CMOS is `(("bypass MEMORY test", "esc"), ("to RESUME", "f1"), ("EXIT FOR BOOT", "return"))` (`rdgwin.BIOS_PROMPTS`), up at ~18 emulated s where a fixed 80 was spent |
| `ui_idle()` / `ui_done(cap_ms=)` | os88marty's: ui_task asleep, nothing queued, the gfx lock free, the key ring empty - held 60 ms. `cap_ms` bounds it at the old fixed step, so a modal popup or a busy worker costs no more than before |
| `until(cond, what, guest_ms, step_ms, need=)` | any guest condition, budgeted in EMULATED ms (a loaded host cannot shorten it); `need=False` answers None where the assertion after it is the judge |
| `wait_mem(addr, data, ms)` / `wait_text(text, ms)` | upstream's `wait mem` / `wait text`, checked by 86Box every emulated ms with no round trip a step |
| `to(x, y)` | each packet waits for the pointer to MOVE and stop (10 ms steps) rather than a fixed 60 |
| `quit(remove=)` | waits for the process to be GONE - the floppy images are written at exit, so a read of `fdd2.img` after it needs no sleep |
| `sym` / `word` / `byte` | kernel symbols, the map parsed once per process (it was ~15 ms a `pointer`) |

`launch(turbo=True)` (`--turbo`) sets 86Box's floppy turbo on every drive:
no rotation or seek, so the boot's floppy phase and a package load cost a
fraction of the emulated AND the host time (floppy emulation is the slow
part to emulate: ~2.4x real time against ~7x at an idle desktop). For a row
whose subject is not the floppy's timing. **Not under kern_mega**: there the
speaker's quick-volume module load failed with `Needs Sys Disk A:` under
turbo and loads without it (`tests/megacdrom.py`) - unexplained, and a
candidate defect in the protected-mode floppy path rather than the row's.

`launch(temp=True)` (`--temp`) marks a run directory disposable: `with
launch(...) as b:` quits and removes it, and every `launch` first `reap`s
the dead temp ones (`os88box.py reap`), so a row's disk copies do not pile up.
`OS88_BOXLOG=<file>` logs every request - instance, verb, host seconds,
emulated ms advanced, caller's file:line - which is where a row's time goes.

### `faults`: the instrument for "it reboots"

```
$ python3 tools/os88box.py 286-mega faults
#GP  0028:466E err FFE8 PM ss:sp 1180:1AF7 ds 0010 es 0020 mega_idt_gate+7F
(100 BIOS-side faults and resets at F000:xxxx hidden - --all shows them)
```

One line per exception, before delivery: the vector, CS:IP, the error code,
real or protected mode, the stack and data selectors, the symbol, and in
brackets **86Box's own reason** - the string every fault site in its CPU
passes and nothing upstream prints (`[loadseg(): Normal segment RPL >
DPL]`, `[Segment can't write]`). The reason is what the error code cannot
say: a #GP's code masks the selector's low two bits, so a segment OFF
kern_mega's 8-paragraph grid reads as the grid value beside it and looks
like a valid selector that faulted for nothing. CS
`0060` (KERNEL_SEG, and kern_mega's identity selector for it) and `0028`
(kern_mega's bootstrap alias, `MEGA_SEL_BCODE`) resolve against `.text`;
`--seg HEXCS=.section` adds others, and `--seg HEXCS=build/video.map`
resolves a PACKAGE's code alias against its own map (the Video Player's
`2474:438A` is `vp_lrget.add+17`). A `RESET` line names the fault that
preceded it, which is the triple fault's cause.

**The F000 lines are hidden by default and are not os8088's**: an AT BIOS
leaves protected mode the way the 286 has to, by shutting the CPU down, so
its POST memory test and every `int 15h AH=87h` block move are an `#GP` and a
reset at `F000:xxxx`. On the mr286, POST, a boot and three windows came to 50
pairs.

Two exceptions are EXPECTED on kern_mega and are handled by design, so their
presence is not a finding: `mega_strcal`'s `#GP` (the 286 string-restart
calibration, KERN-MEGA-PLAN 10.3 item 54.1) and a `#GP` at
`mega_ldt_free.out` with a freed selector in ES (`mega_gpfix .stale`,
KERN-MEGA-PLAN decision 14).

### The worked example (KERN-MEGA-PLAN item 88)

The owner reported a boot loop off 86Box's mr286 - loading screen, at most a
frame of desktop, POST again. Bochs is a 386 and could not show it. The route
that found it is the one this tool now packages:

1. **Reproduce on the owner's profile** (`vm/286-mega`): it looped here too.
2. **Bisect with real images**: a git worktree per commit, `make mega` in it,
   `launch --a <worktree>/build/mega.img`, step, shot. Item 81's tree booted,
   item 82's looped.
3. **`faults`** named the instruction: `#GP FFE8 at 0028:mega_idt_gate` - a
   selector load during `mega_enter`'s first IDT build, on a bootstrap GDT
   with no entry `FFE8`. Without patch 01 the only evidence was a reboot.

Steps 1 and 2 were done by hand with xdotool against a windowed build before
the automation socket was found, and cost most of an hour in BIOS setup
screens, a reset confirmation dialog and a mouse that would not move; with
the socket each bisection point is a build plus about ten seconds.

## AT machines start in BIOS setup once

A fresh run directory has no CMOS, so an AT-class BIOS stops on *"CMOS is
corrupt - press any key"* and then in setup. Do it once, through the socket,
and **`quit`** - 86Box writes `nvr/` on a clean power-off, and a `kill` loses
it. `text` reads setup as characters, which makes it scriptable. The mr286's
MR BIOS 1.65 wanted, from *Press any key*: `return` (setup), `right` four
times (the Floppy page), `down`, then `space` until each row reads
`3½ 1.4M`, `down` to the next, `esc`, `f10` (record and exit, no prompt):

```python
row = lambda r: bytes.fromhex(b.cmd("text")["chars"][r]).decode("cp437")
for r in (10, 11, 12):                       # Floppy 0, 1, 2
    while "1.4" not in row(r):
        b.tap("space")
    b.tap("down")
b.tap("esc"); b.tap("f10")
```

`launch --fresh` (or `--clear-cmos`) starts that over. A machine's layout is
its BIOS's, so this is per profile, not a library call.

## GDB

The stub always listens, on the port `launch` prints (and `state.json`
keeps). Attach to a **running** machine at any time:

```sh
gdb -q -ex "set architecture i8086" -ex "target remote 127.0.0.1:PORT"
```

Detaching lets it run on. Two things to know: **a HELD machine hangs the
attach** - the stub answers GDB's first question only once the CPU has
stopped, and a held CPU never gets that far - so either attach while it
runs or launch with `--gdb-wait`, which starts it stopped at the reset vector
`F000:FFF0` until GDB continues or detaches; and the stub reports `eip` as a
LINEAR address (`0xfef1d` with `cs 0xf000`), not an offset.

## kern_big rows on 86Box

`tools/os88box86api.py` serves the os88marty TEST API - `os88marty.Marty`,
the class every MartyPC row is written against and is named for the emulator
it was first a client of - out of an 86Box, for the row's OWN kernel and
disks. Nothing is swapped, and symbols come through os88sym's normal path
(it re-assembles `kernel.asm` and checks `build/kernel.bin`, or
`$OS88_BUILD`'s).

```sh
OS88_EMU=86box python3 tests/<row>.py                       # kern_big
OS88_EMU=86box OS88_BUILD=build/smallk OS88_DEFINES=KERN_SMALL \
    python3 tests/<row>.py                                  # kern_small
```

`os88marty.launch` answers an `Api86Box` under `$OS88_EMU=86box`, and
`tests/os88qemu.py`'s launchers (`make_test`) start `tests/os88qmpmega.py`'s
QMP server over the same machine, so a QEMU row's `xp`, `sendkey`,
`mouse_move` and `screendump` reach it unchanged. `os88marty.emulator()` is
how a row asks which it got.

**It is for CLAUDE.md's QEMU closed list**: the hardware MartyPC has not
got, asked of a period BIOS rather than SeaBIOS - a 286 (HDD.DRV's rung 1), an
AT's MC146818 (the RTC ladder's write half), the 8042's aux port (the PS/2
mouse), a network card. **It is not for a timing**: 86Box is not
cycle-accurate (*What it is NOT*), so a row whose assertion is an 8088 cycle
count stays MartyPC's however well it runs here.

**The machine** is the row's MartyPC name, mapped (`machine_config`): an IBM
XT, `vm/xt640` (8088 at 4.77 MHz, 640KB, a serial mouse on COM1), when
everything fits one, and an AT when the name or a floppy says so. A name
for a machine only 86Box has carries no `os8088_` prefix, which is
MartyPC's namespace and which `tests/unit/t_machines.py` holds to its list.

| the name, or the disks | 86Box gets |
|---|---|
| (an `os8088_*` name, 360KB or 720KB disks) | `vm/xt640`, the cards and devices below. 360KB is a 5.25" DD drive, 720KB a 3.5" DD one. No CMOS to prime |
| `at286`, a 1.2MB or 1.44MB floppy, `_ne2k` | `vm/at286-port`, the owner's mr286 (MR BIOS 286, 16 MHz, 4MB, ISA IDE): an XT BIOS drives no HD floppy, and an NE2000 is a 16-bit card |
| `_ps2`, `at386` | `vm/386-ps2`, the Packard Bell Legend 300SX - the one machine here whose 8042 has an aux port. `_ps2` is its PS/2 mouse with both UARTs OFF, as QEMU's `-serial none` |
| `_NNNk` | the XT's RAM |
| `_hdd` | the row's own fixed disk: the AT's IDE, an XT-IDE card on an XT |

and the card, `_sb` and `_ne2k` tokens as in the kern_mega table below (an
XT's card is an NE1000 at 300h on IRQ 3). `$OS88_86BOX_VM`, `$OS88_86BOX_SND`
name another profile and another sound card.

**An AT's CMOS is primed once per configuration** and cached under
`build/86box/nvrcache/`, as below - the mr286's with its seed and MR BIOS's
setup, the Packard Bell's in ROUNDS: Phoenix has no seed, so the first boot
saves its own defaults (F2, any key, Esc, F4), `_cmos_fix` makes the floppy
types, the equipment byte and the checksum agree with the machine, and a
round that still meets *"Invalid configuration information"* (its memory
count moves with shadowing, and only its setup rewrites that) saves again,
until one boots without it. A fresh box pays ~15-45 s for each new shape;
every later launch POSTs straight through.

**Ports** - `m.inb`/`m.outb`, and `m.cmos()` on an AT - are a stub run at
0050:0000 between two hardware breakpoints, real mode only: the CPU is first
run to IRQ0's handler (at most a tick of guest time; a held machine is
usually in the idle task's HLT, and a GDB single step there executes nothing,
86Box's timers running on the CPU's cycles), the stub runs with IF clear,
and the bytes, the registers and CS:IP go back. `m.cmos` is ABSENT on an XT,
as on MartyPC, so `getattr(m, "cmos", None)` is how a row asks
(`tests/dtwrite.py`).

**A QMP row** gets the mr286 with a VGA (`at286_vga`; `at386_vga_ps2` for
`MOUSEPORT=ps2`), 86Box PACED as for kern_mega below, and its floppies in
86Box's TURBO mode - the rows were written against QEMU's instant floppy,
and paced at an AT's real speed a 60KB browser was still coming off the disk
when `tests/ethernet.py` looked.

**Registering one** is a line in `tests/box86port.txt`, which registers
`box-<row>` - the same grammar and the same parser as `tests/megaport.txt`
(`tests/suite.py`'s `_ports`), with `OS88_EMU=86box` in the row's
environment. A line is there only once the row PASSES and was SEEN TO GO
RED, and its comment names the break. `$OS88_86BOX_POKE` is the cheap one
(the poker below, for any kernel; its desktop gate first checks that the
booted kernel's code is in memory, since an AT's POST leaves patterns that
read as a desktop).

## kern_mega rows on 86Box

`tools/os88mega86box.py` is the second backend of kern_mega's port
mechanism. docs/BOCHS.md's *Rows ported from MartyPC* covers the first.
Its machine, its server and its CMOS priming are `tools/os88box86api.py`'s
(*kern_big rows on 86Box*, above) and what it adds is kern_mega's.
That server answers the requests `Marty` is built on out of an 86Box:
memory through automation's `memr`/`memw`, registers and breakpoints
through the GDB stub (`Z1`, linear, as many as wanted), keys
as set-1 scancodes, the serial mouse's own reports, `fbuf` off
`screenshot`/`screenshot1`, `disk()` off `fdc`, and `reset` as a hard
reset. So os88ui, os88mouse, `until`/`settle` and every row above them run
unchanged. Selectors, `.cold`/`.mega` symbols, the boot toast and the disk
mapping (a shipped floppy becomes `mega.img`/`megaapps.img`) are
`tools/os88megacommon.py`'s, and the Bochs backend shares that module.

```sh
OS88_MEGA=1 OS88_MEGA_EMU=86box python3 tests/<row>.py
```

**Choose it where Bochs lacks the hardware**: a CGA, a Hercules, an MDA,
two cards at once, a Sound Blaster whose DMA runs on the emulated clock,
a 5.25" 1.2MB drive, or an AT BIOS taking the 286's reset path. A line in
`tests/megaport.txt` chooses it with the token `OS88_MEGA_EMU=86box`. That
gives the row `needs=("86box",)` and no X display. `ROW/VARIANT` registers
a second port of a row as `mega-ROW-VARIANT`, beside its Bochs port.
`# 86BOX HARDWARE (wave 3)` is the list.

**The machine** is `vm/at286-port`: the owner's mr286 (MR BIOS 286, 16 MHz,
4MB, a serial mouse on COM1, ISA IDE). `$OS88_MEGA_VM` names another
profile. Whatever the row's MartyPC machine name says is added to it
(`machine_config`):

| name contains | 86Box gets |
|---|---|
| `both` / `both…mono` | a CGA and a Hercules, monitor 0 and 1; `mono` makes the Hercules primary |
| `vga_herc` / `vga_mda` | a VGA beside a Hercules / an MDA |
| `herc`, `cga`, `ega`, `vga` | that card alone. No adapter in the name means a CGA, as MartyPC's default machine has |
| `_sb` | a Sound Blaster 2.0 at 220h, IRQ 7, DMA 1. `$OS88_MEGA_SND=sb16[:irq]` gives an SB16 instead |
| `_ne2k` | an NE2000 at 300h, IRQ 10, on slirp. The host is 10.0.2.2, as on QEMU |
| `_hdd` | the row's own fixed disk on the IDE controller, mounted in place |

A floppy drive's type follows its image: a 360KB or 1.2MB image gets a
5.25" HD drive and anything else a 3.5" one. `$OS88_MEGA_MEGS` sets the RAM.

**The CMOS is primed once per configuration.** MR BIOS stops at POST on any
configuration change. So the first launch of each machine shape is a held
boot that answers the prompts: setup's Video page gets *Primary Video =
Monochrome* for a mono primary, and the Fixed Disk page gets type 47,
identified with `?`. F10 records the result. 86Box's saved CMOS is cached under
`build/86box/nvrcache/` by a hash of the configuration, with the shutdown
byte cleared, and every later launch POSTs straight through. The seed is
`vm/at286-port/nvr-seed/`.

**The clock is the guest's.** The backend runs 86Box fast-forwarded (about
10x a real 286 at an idle desktop) and reports `cycles` as emulated time at
4.77 MHz. So a guest budget means the same seconds of machine time it
means on MartyPC. `tests/os88qmpmega.py`'s QMP server is the exception and
runs 86Box PACED. A QMP row times its input on the host's clock (a
double-click is four `mouse_button`s 0.1 s apart), and fast-forwarded
those would be a guest second apart. That server takes the same
`OS88_MEGA_EMU=86box`, which is how the Telnet rows reach 86Box's slirp.

**Seeing a port go red** without a knob build: `$OS88_MEGA_POKE` takes
`SYM[±OFF]=HEX;...`. It writes those bytes over the kernel once the desktop
is up (protected mode, the desktop and menu bar laid out, the splash gone,
`[ticks]` moving) and prints what it replaced. 86Box's interpreter keeps no
decoded copy of code, so a byte written is the byte executed. `f8c3`
(`clc`/`ret`) makes a routine succeed having done nothing, and `f8cb` does
the same for a far one. The comment on each wave 3 line names its break.

**What it cannot do**: `mem`/`io` watchpoints (refused), a mouse wheel, and
a cycle COUNT that means anything. 86Box is not cycle-accurate, and its
1 ms `step` is the resolution. A row whose assertion is a MartyPC cycle
budget is not a port.

## What it is NOT

- **Not cycle-accurate.** 86Box times a machine by its model, not by the bus
  cycle; MartyPC agrees with the 5150 to 0-4% on 45 of 47 `gfxbench` rows and
  86Box has never been measured against it here. A number off `step` is
  emulated milliseconds of 86Box's machine. docs/TESTING.md's rule stands: a
  disk number lands on the 5150.
- **Not in a gating tier.** Six soak rows launch 86Box (`gbreplay`,
  `gbbank`, `gbhifreq`, `megacdrom`, `hma286` - XMEM.DRV's LOADALL and the
  HMA's pool on vm/286-hma, through an os88ui adapter in the row that other
  rows can lift - and `rdgspeed286`, which needs the owner's cartridges),
  plus the kern_mega ports in `tests/megaport.txt`'s `# 86BOX HARDWARE` section
  and every `box-<row>` of `tests/box86port.txt`.
  `needs=("86box",)` skips all of them on a box without the build, since
  nothing in `make` builds 86Box.
- **Host-timed devices are not deterministic**, even held: networking and some
  sound paths run on host threads (upstream's *Determinism notes*).
- The machines under `make xt` / `make 286` / ... still launch the macOS app
  at `$(BOX)`, interactively. This tool is the container's route to the same
  `vm/` profiles.

## Fallback: a window, xdotool and the keypad mouse

`launch --window` runs the GUI on `$DISPLAY` instead (an `Xvfb :99` works).
That is the route without the socket, and it is worse in every way: keys by
`xdotool key` into the focused window, screenshots of the whole root window
with the toolbar in them, the toolbar's reset button and a confirmation
dialog for a reset, and **no injected mouse** - under Xvfb 86Box's XInput2
capture saw none of xdotool's motion, so the pointer was driven with
os8088's own keypad mouse (SPEC.md 9.6, live only while the mouse has never
spoken): `KP_*` to move, Space as the button, two Spaces 165-500 ms apart for
a double-click. It is kept here because it works on an 86Box built without
`-DAUTOMATION=ON`, which is every release build.
