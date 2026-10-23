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

The two patches, both inert unless asked for:

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

### `to` and `pointer`: why the pointer is closed-loop

86Box's mice are relative, and os8088's pointer is whatever the kernel made of
the packets. `to X Y` reads `mouse_x`/`mouse_y` out of the kernel through the
nasm map (`build/kernel.map`, or `build/megak/kernel.map` when the A: image is
named `mega*`; `launch --map` for anything else), sends a packet, and reads
again until they agree. On the mr286 a 200-pixel packet stream moved the
pointer 198 pixels, so it converges in a round or two. `sym NAME` prints the
linear address of any `.text`/`.bss` kernel symbol, for `memr`; anything in
`.cold` or `.lowbss` wants `tools/os88sym.py`.

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

## What it is NOT

- **Not cycle-accurate.** 86Box times a machine by its model, not by the bus
  cycle; MartyPC agrees with the 5150 to 0-4% on 45 of 47 `gfxbench` rows and
  86Box has never been measured against it here. A number off `step` is
  emulated milliseconds of 86Box's machine. docs/TESTING.md's rule stands: a
  disk number lands on the 5150.
- **Not a suite tier yet.** No row in `tests/suite.py` launches 86Box; the
  first one that does owes docs/WRITING-TESTS.md the usual things - and a
  `wants=` for the build, since nothing in `make` builds 86Box.
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
