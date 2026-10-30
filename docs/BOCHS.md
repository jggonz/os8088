# Bochs: running kern_mega under an emulator that checks protected mode

Bochs is the instrument for **kern_mega** (docs/plans/KERN-MEGA-PLAN.md), and
for nothing else in this tree. Every other kernel runs in real mode, where
MartyPC is the default instrument and QEMU the fallback (docs/TESTING.md).
This file is how to install it, drive it, read it, and where it lies.

## Why Bochs and not QEMU

QEMU's TCG checks no segment limit on a data access and lets code write
through CS. So on QEMU kern_mega runs code that faults on a real 286: every
one of the Victory 286's first five faults ran clean there. Bochs checks every
access the way the CPU does, and reproduced those faults to the CS:IP and SP
in the field photographs. If kern_mega faults under Bochs it will fault on the
iron. If it runs under Bochs it will probably run on the iron, except for
the five blind spots at the end of this file.

## Install (Ubuntu container, as root)

```
apt-get install -y bochs bochsbios vgabios bochs-x xvfb xdotool x11-apps imagemagick
```

That is the whole of it: Debian's Bochs 2.7 has the debugger compiled in,
which the harness relies on. **The BIOS must be `BIOS-bochs-legacy`**. The
default BIOS's 32-bit init crashes here, and the harness's bochsrc already
names the legacy one. Nothing needs building, and no ROM is fetched.

## The machine the harness builds

`tools/os88bochs.py` writes `OUT/bochsrc` (default OUT is `build/bochs`) and
starts Bochs on an Xvfb display - `:99`, or `$OS88_BOCHS_DISPLAY` - that it
starts if that display is not up:

| part | what |
|---|---|
| CPU | Bochs's default, a 386-class CPU - see blind spot 2 |
| RAM | 16MB (`megs: 16`), so the XMEM pool and `.cold` above 1MB are real |
| video | Bochs VGA with VBE, the stock `vgabios.bin` |
| A: | `build/mega.img`, COPIED to `OUT/a.img` each boot (1.44MB) |
| B: | `build/apps120.img` by default, or `--b IMG`; `--btype 1_2` or `1_44` is the DRIVE, the media size is read off the image |
| C: | a 20MB ATA disk (615/4/17) - see `--c` below |
| mouse | PS/2, captured with ctrl + middle button (`toggle=ctrl+mbutton`) |
| E9 | `port_e9_hack`: every byte the kernel writes to port E9h lands in `OUT/e9.out` |
| sound | `--sb`: an SB16 at 220h, IRQ 5, DMA 1, its output written to `OUT/sb.wav` |
| network | `--ne2k`: an NE2000 at 300h, IRQ 10, on Bochs's own slirp (the host is 10.0.2.2). `$OS88_BOCHS_SLIRP` names a slirp config for it - a first line `# slirp config`, then up to five `hostfwd = tcp::2121-:21` lines - which is how a HOST client reaches a server in the guest: FTPD's control port and four of its passive ports, with `curl --ftp-skip-pasv-ip` (KERN-MEGA-PLAN 14.46) |
| clock | `slowdown` at `cpu: ips=4000000` - see *The clock* below |

### The clock

**Guest time is the guest's INSTRUCTION COUNT, and the host only caps it.**
`clock: sync=slowdown` makes Bochs's time `ptime / ips` - 4,000,000
instructions a guest second, Bochs's default, HLT included - and sleeps
whenever the guest gets ahead of the wall clock, so it runs at real time at
most (measured: 4.00M instructions and 18.2 BIOS ticks a host second on an
idle desktop). That is a SLOWER guest CPU than the rows used to see: under
`realtime` the CPU ran as fast as the host ran Bochs, ~100M a wall second
here, and two rows were racing it against a fixed pause (`mega-ethernet`'s
settle against the Browser's load, `mega-vidvga4`'s `before` against the
progress widget) - both wait on the state they meant now. 100M was tried and
refused: ten Trackers' busy phase runs ~6M a host second, so the guest fell
to 1/17 of real time and `os88marty.pace`'s backstop fired. A loaded host
that cannot keep 4M runs the guest slower than real time, which costs host
seconds and nothing else, because every wait below is the guest's.
It ran `sync=realtime` until 2026-10-27, and that is the mode that made
kern_mega rows **fail under host load and pass alone**:

- under `realtime` the PIT follows the HOST clock and the CPU runs as fast
  as the host lets it, so a starved Bochs is a SLOWER MACHINE to the guest -
  fewer instructions a tick. Tracker's speaker calibration (SPEC.md 45.25)
  then priced every rung too dear and refused the Play
  (`mega-ringmove-tracker`), and an 8 kHz rate hook ate what was left;
- a host stall (Bochs descheduled, a debugger stop) was CAUGHT UP in ticks
  afterwards, so the guest saw time it had not executed: a double-click
  whose four edges went out 0.33 s apart read a tick count ~54 on, and
  `mega-alertbtn`, `-trkcompact`, `-bptrace` and `-int0sweep` read 18-50
  ticks against the 9-tick window;
- and the shim's `status()["cycles"]` was the HOST seconds the guest had
  been running, so a "guest-second" budget drained while the guest made no
  progress.

Under `slowdown` the guest's tick density is the same on any box - but **a
stop costs the guest nothing ONLY with the clock preload**,
`tools/bochs/bxclock.c`, which `os88bochs.Box` builds into
`build/bochs/` with the host's `cc` and puts on Bochs's `LD_PRELOAD`.
Bochs 2.7's slowdown timer (iodev/slowdown_timer.cc) caps the guest against
`time(NULL)`, the WALL clock: host seconds spent stopped at the debugger -
every read, screenshot and breakpoint - left the guest behind it, and on the
resume Bochs ran flat out until it had caught up. Measured on a loaded box,
half a host second of running after a 2 s stop held **2.41-2.60 guest
seconds** against 0.42-0.45 with no stop; a `boxed()`-shaped look (pause,
three reads, the glass, run, `pace(0.01)`) whose stop took 0.3 host s cost
**0.39-0.50 guest s** where the same look with a quick stop cost 0.11. So a
row's guest time per host interval moved with host load, which is what
`mega-vidfskeysvga4` (7 looks at a toast that needs 8) and
`mega-assocopen` (a double-click 9-10 ticks apart) failed on. The preload
replaces `time()` with a clock that does not move while Bochs does not run:
the slowdown timer calls it every guest millisecond, so a running Bochs
calls it every host millisecond or less, and any gap over 50 ms
(`$OS88_BXCLOCK_GAP_MS`) is a stop and is taken out whole. With it the same
two measurements read **0.59-0.61** and **0.07-0.10**. `$OS88_BXCLOCK=0` is
the A/B; without a compiler `os88build.have_bochs()` answers no and every
Bochs row SKIPS (`os88soak.py check` names it), rather than run on a clock
whose answers depend on the box.

**The shim reads the clock OFF the guest**
(`os88megabochs._cycles`): every stop it makes for a read carries a `ptime`
for free, and a status poll on a running guest takes one when the last is
older than 50 ms (at most every 0.5 s, and no oftener than leaves the guest
running ~80% of the time - a stop is ten times dearer on a loaded box). It
never extrapolates, so a reading can lag the guest but never lead it, and a
budget spent against it cannot run out while the guest stood still. That lag
is the second way host time leaked in, because a guest capped at real time
runs the whole gap between two readings: a `pace(0.01)` (0.045 guest s)
ended on the first reading past it, and on a loaded box that was half a
guest second - `mega-vidfskeysvga4` fitted 4 looks where it fits 15, with
the preload in. So **`os88marty.pace` and `advance(cycles=)` end on Bochs's
own TIME BREAKPOINT** (`os88megabochs._run_exact`: `sb N` on a stopped
guest, `c`, the prompt it stops at), exact to the instruction - or to the
guest's next interrupt when it is in HLT, a breakpoint being checked at an
instruction's end - whatever the host is doing. Two limits: it is not used
while a row has a breakpoint of its own armed (a time breakpoint cannot be
deleted, so one a row's breakpoint got ahead of would stop the guest later,
unasked), nor inside a `bp_trace` pump; both fall back to the polled clock.
And **Bochs 2.7's second `sb` is a defect**: dbg_main.cc re-arms the timer
CONTINUOUS, so it fires every N for ever and stops a guest the shim thinks
is running (`mega-stikmenu` took 524 s). pc_system.cc's handler re-arms
one-shot for the next one queued, so the shim queues a SENTINEL years away
before its first `sb` and every later one is the queue's head.

Two more host-time leaks surfaced once waits were exact, and both are fixed.
**A halted CPU slept the HOST**: Debian builds Bochs with
`BX_USE_IDLE_HACK`, so every HLT (cpu/proc_ctrl.cc) waits up to 1 ms in the
X GUI's `select()` for an X event - a host millisecond that buys the guest
only the ticks to its next timer. kern_mega's idle task halts after every
IRQ0, so with the PIT at 8 kHz (Tracker's speaker player on a card-less
machine) the guest ran at an EIGHTH of real time: 0.35M instructions a host
second at 4% CPU, and `mega-trkcompact`'s `pace(1.0)` - 4.5 guest seconds -
had not run after 45 host seconds (4 of 4 runs; it had failed this way
before, on the polled clock, in 2 of 6 soaks). The same preload makes a
`select()` called FROM the X GUI plugin with a timeout a poll
(`$OS88_BXIDLE=1` keeps the sleep, the A/B); the slowdown timer still caps
the guest at real time. And **a SIGINT can be lost**: Bochs echoes the `c`
as readline reads it, and `bx_dbg_continue_command` then clears
`interrupt_requested` before it runs, so a stop sent in between is erased.
An exact pace stops the guest milliseconds after resuming it, which lands in
that window on a busy box, and `_halt`'s second SIGINT came a host SECOND
later - a guest second run inside one look (traced: `_halt` 1.032 s,
4.5M instructions in a 0.045 s pace), `mega-vidfskeysvga4` 5-7 looks where
20 fit. The kick is every 50 ms now (`MegaBochs.KICK`; a SIGINT at the prompt
is harmless, the next `c` clearing it, and the empty line it makes is let
land before prompts are counted). `$OS88_BOCHS_EXACT=0` puts the polled
clock back for an A/B.

Rows that read guest state and then ACT on it in host time are still a race
on any emulator, and three are now stepped rather than polled: `pxfolder`
reads the shown picture and sends Esc with the machine stopped, `weavemove`
walks from the serve to its reading in `advance` steps, and `vidfskeys`'s
looks are `advance`s (see each row). A row on `os88bochs.Box` directly has
`Box.guest_sleep(secs)` for the same job - `ptime` read every 50 ms host,
each read a stop that costs the guest nothing - and `megavidv2` spaces its
keys with it: its "Space again a second in, while the blocks are coming"
was a host second, which with four Bochs to a core paused the play before
its first frame, so both of its tails were wrong (old row 2 of 2 red beside
four Bochs rows, new 3 of 3 green). What
`slowdown` still does NOT give is a smooth map from host time to guest time
(it holds the guest up to a second ahead of the clock), so a burst of guest
time can follow a host stall. **That is why input whose SPACING matters is not host-timed**:
a double-click is queued on a stopped guest and reaches it in one GUI pass
(`queued_input`, below). `$OS88_BOCHS_SYNC=realtime` puts the old clock
back, shim and all, for an A/B; `$OS88_BOCHS_IPS` changes the speed (a
host that cannot reach it runs the guest slower than real time, which the
guest-time waits absorb), and `Box(ips=)` changes it for ONE machine whose
subject needs a faster CPU - XT-MP3's Auto measures its decode against guest
time and rightly declines to play live at 4M (`tests/x3mega.py`'s play leg
asks 25M). A KEY whose spacing matters is the double-click's case one device
along: `tests/lemmega.py`'s F12-twice nuke queues each press on a stopped
guest and stops on a `watch w` of the game's own state between them, where
`key F12 F12 --delay 100` reached one pass of the game as one press.

**MANY TO A CORE.** Because a guest is capped at real time, a Bochs row keeps
a FRACTION of a core busy - the guest 3% idle and 13% flat out, the whole row
with its harness 0.1-0.25 (docs/plans/SOAK-PARALLEL.md 19.1) - so
`tools/os88test.py`'s emulator lane weighs a Bochs-only row a quarter of a
lane (`--bochs-per-core`, `$OS88_BOCHS_PER_CORE`, default 4; 1 is the old
one-row-a-lane), a whole one for its first 15 s, and admits one only while
the box has 0.75 of a core idle. The limit is SATURATION and not any shared
resource: displays (`:110`-`:149`, forty), run dirs and process ownership are
per row already (SOAK-PARALLEL 18.1), while past the box's last core every
guest runs below real time and the few host-second waits named above stop
meaning what they say.

## Commands

```
python3 tools/os88bochs.py boot  [options]      # boot and leave it running
python3 tools/os88bochs.py sweep [options] PKG.o88 ...
python3 tools/os88bochs.py dbg   [--out DIR] "CMD" ...  # break in, run, continue
python3 tools/os88bochs.py stop                  # end every Bochs on the box
```

| option | effect |
|---|---|
| `--out DIR` | where the disks, bochsrc, `e9.out`, `bochs.log` and screenshots go (default `build/bochs`) |
| `--sb` | the SB16 above |
| `--ne2k` | the NE2000 above |
| `--cfg BITS` | write a SYSTEM.CFG onto A: wanting drivers, one bit each by `kernel/driver.inc`'s `drv_cfgbit`: **bit 0 SOUND.DRV, bit 1 HDD.DRV**. `--cfg 1` with `--sb` is a sound machine; `--cfg 2` gives the Hard Drive page its driver |
| `--c run` | (default) C: is built fresh: MBR, kern_mega's boot record and kernel, and a `RUN/` folder holding MEGARUN as `0RUN.O88` plus any packages named |
| `--c blank` | C: is 20MB of zeros - for the installer (Format, then Install) |
| `--c keep` | C: is `OUT/c.img` exactly as the last boot in that OUT left it |
| `--bootc` | boot from C: instead of A: |
| `--noa` | no disk in A: |
| `--b IMG`, `--btype` | B:'s image and drive type |
| `--a IMG` | A:'s image in place of the tree's `mega.img` - a kern_mega system disk with something changed, such as `make megacdtest`'s |
| `--cd ISO` | an ATAPI CD-ROM, the secondary master at 170h, holding an ISO 9660 image (CDROM.DRV, SPEC.md 119). **Data only**: Bochs 2.7 answers PLAY AUDIO and a SubQ READ SUB-CHANNEL "not implemented", so `build/megacdsim.img`'s `-DCDSIM_AUDIO` driver is what gives CDRack a TOC here, and 86Box's `vm/286-mega-cdrom` is where a disc plays (KERN-MEGA-PLAN 14.68). `--cfg 0x101` wants SOUND.DRV and CDROM.DRV |

`boot` returns once Bochs is up and leaves it running; the machine outlives the
command. The desktop is up about **6 seconds** after reset (it was quoted at
80 here). Wait for it by condition, not by a fixed sleep - from a script,
`Box.up()` (below); by hand:

```
until grep -aq "kern_mega: protected mode" build/bochs/e9.out; do sleep 1; done
```

## Driving it: tools/os88bochsui.py

```
python3 tools/os88bochsui.py capture         # once per boot: grab the mouse
python3 tools/os88bochsui.py goto X Y        # absolute, in GUEST pixels
python3 tools/os88bochsui.py move DX DY      # relative, guest pixels
python3 tools/os88bochsui.py click | dbl
python3 tools/os88bochsui.py down | up
python3 tools/os88bochsui.py key Return      # xdotool key names
python3 tools/os88bochsui.py type 'text'
python3 tools/os88bochsui.py shot out.png    # the guest's 640x480 (plus status bar)
python3 tools/os88bochsui.py e9 [OUTDIR]     # the tail of e9.out
```

`goto` pins the pointer at the top-left first (the kernel clamps it there),
then walks, because Bochs moves the guest one pixel per two X pixels. So `goto`
is absolute and repeatable, and costs a second or two. **A menu pick** is
`goto` the title, `down`, `move` to the item, `up`.

Coordinates on the 640x480 desktop that every recipe below uses:

| target | guest x, y |
|---|---|
| A: / B: / C: drive icons | 591,45 / 591,105 / 590,160 |
| System menu (the chip) | 10,5; Control Panel is +35 below it, Restart +99 |
| Control Panel page list | x 195; Hard Drive at y 267, Display at 197 |
| first rows of a fresh Disk window | x ~150, y 123, 139, 155, ... (16px a row) |

Take a `shot` and read it before trusting a coordinate in a window you have not
opened before.

## Waiting on the guest: `os88bochs.Box` from a script

The debugger is cheap: a SIGINT stop, an `xp`, and `c` is **~20 ms** when each
step is ended on the debugger's NEXT PROMPT rather than on a clock
(`os88bochs.session`; the module's `dbg` is it too). So a script reads the
kernel instead of sleeping:

| call | what it waits on / answers |
|---|---|
| `box.sym(name)` | a kernel label's LINEAR address, off the BOOTED tree's `megak/kernel.map` (`.text`/`.bss` at KERNEL_SEG, `.lowbss` at LOW_SEG, `.cold` at COLD_SEG) |
| `box.read(addr_or_label, n)`, `box.reads([(a, n), ...])` | bytes; `reads` is one stop for all of them |
| `box.until(cond, what, secs)` | polls a condition that reads the guest; RuntimeError naming WHAT |
| `box.up()` | the boot gate: `desk_rows`/`menu_nbar` written, `[spl_live]` 0, `[ticks]` moving, then `ui_done` - os88marty's `kernel_up` |
| `box.ui_done(what, lock=True)` | ui_task asleep with nothing queued, no wake, the gfx lock free (`lock=False` for a game's worker) and the BIOS key ring empty, at two reads 0.25 s apart with the tick moving - what a sleep after a click stood for |
| `box.goto(x, y)`, `box.click(x, y)`, `box.dbl(x, y)` | the pointer CLOSED-LOOP on `mouse_x`/`mouse_y` (~0.6 s, no homing walk), then `ui_done` |
| `box.megarun(row=0)` | capture, then C:, RUN and 0RUN.O88 - MEGARUN starts the rest |
| `box.instances()`, `box.pkg_base(name)`, `box.pkg_read(name, label, n, map)` | a package's own bytes by its header name, through its LDT descriptor when it is OS88_HIGH (`build/<pkg>.map` names the label) |

**A stop RELEASES THE MOUSE.** Bochs drops its capture whenever the debugger
takes over, so after a read the next move or click reaches nobody. A Box that
has captured re-captures inside the NEXT xdotool call (`xd`, so a row's own
`os88bochs.xd(...)` is covered too): X delivers the toggle ahead of the input.
And a stop waits until Bochs has PROCESSED the last input: every `xd` chain
ends in a SENTINEL key (`os88bochs.SENTINEL`, XF86Launch9) that Bochs's X GUI
maps to nothing, so it hands the guest nothing and logs
`keysym 1008ff49 unhandled!` instead - X and `handle_events` keep a chain in
order, so that line in `bochs.log` means every event before it was taken, and
`_Input.taken` waits for it. It watched the X socket drain before
(sock_diag, then `INPUT_GAP`), and that was READ, not PROCESSED: xcb pulls
events off the socket while Bochs is blocked writing a frame to a slow X
server, a stop taken then left them unprocessed, the resume took them
UNCAPTURED, and the GRAB that had come with them toggled the capture back on
behind the harness - whose next GRAB toggled it OFF. Only on a loaded box.
`$OS88_BOCHS_TAKEN=socket` is the A/B: at four Bochs rows to a box,
`mega-assocopen` and `-minesrc` six times each failed 1 of 12 the old way
(minesrc's G, a press that did not take) and 0 of 12 this way. And a PRESS
is held for a whole guest tick before its release
(`os88mouse.Mouse.HOLD_TICKS`): a loop that polls the button LEVEL - a
tracker, the Sticky Apps panel, the volume popup - never saw a press
released a few host ms after the guest first showed it. A hand-made
double-click passes `hold=False` (tests/assocopen.py), or the hold lands
between its two presses.

| `box.in_fsx()` | an exclusive fullscreen bracket is running (`[fsx_task]` not FFh) - ui_task is FROZEN inside one, so `ui_done` cannot end there; wait on this into and out of it |
| `box.fault()` | the first `kern_mega FAULT` line, or None; every `until` raises on one at once rather than waiting out its budget |
| `box.shot(path)` | a screenshot after `GLASS` (0.45 s): the X window lags the guest by up to one of Bochs's 5 Hz VGA updates |
| `m.fbuf()` (os88megabochs) | the same window, after `GLASS` only while the guest RUNS: a stopped Bochs paints nothing, and before the clock preload (*The clock*) every host second spent stopped was run back as a guest sprint on the resume, so a sleep there only handed the guest time (`mega-vidfskeysvga4` looked at its toast 4 times instead of 11) |
| `box.xd(...)`, `box.key(...)`, `box.type(...)` | xdotool on THIS machine's display - one call at a time (a thread wiggling the mouse beside a click is safe) |
| `box.at_prompt(cmds)` | the machine is ALREADY stopped (an `lb` breakpoint hit): run CMDS and continue, on the prompts |
| text screen | `box.read(0xB8000, n)[0::2]` is the characters - how a hibernate's `os8088 has hibernated` is waited for, it having no E9 line |

**SEVERAL MACHINES IN ONE PROCESS.** `Box(..., display=os88bochs.take_display(),
megs=N)` gives a machine its own Xvfb display and input state, so a row runs
its independent legs in threads at once (tests/megavidv2.py). Drive such a box
through its own methods - the module's `xd`/`shot` are the DEFAULT display's.
`take_display()` holds an flock on `/tmp/.os88bochs-N.lock` (:150 up) for the
life of the process; `tools/os88test.py` takes the same locks for each Bochs
row's own display (:110 up), so two runners - or a runner and a row's extra
legs - never share one.

## Rows ported from MartyPC: `OS88_MEGA=1` and tests/megaport.txt

Most of the soak is written against MartyPC's `Marty` API, and
`tools/os88megabochs.py` answers that API out of a Bochs running kern_mega
instead. With `OS88_MEGA=1` in the environment, `os88marty.launch()` (and so
`os88ui.boot()`) hands back a `MegaBochs`, and the row runs unchanged:

    OS88_MEGA=1 python3 tests/sheetbtn.py

**Bochs is one of two backends.** With `OS88_MEGA_EMU=86box` as well, the
same call hands back an 86Box (`tools/os88mega86box.py`). Use it for the
hardware Bochs has not got: a CGA, a Hercules, two video cards at once, a
Sound Blaster on the emulated clock, a 1.2MB drive, or a slirp that carries
the Telnet rows. docs/86BOX.md's *kern_mega rows on 86Box* is that backend.
What both answer the same way (selectors, `.cold`/`.mega` symbols, the boot
toast, the disk mapping) is `tools/os88megacommon.py`.

It is a SERVER rather than a wrapper: `Marty` is built on one method,
`cmd(**kw)`, and the shim answers those requests itself, so `type_text`,
`readseg`, `bp_exec`, `go`, `wait_stop`, `advance` and every
`os88marty.until`/`ui_done`/`settle` above them are MartyPC's own code. What
it maps:

- **disks** - a shipped system floppy in build/ (`os8088*.img`, `small*.img`)
  becomes `build/mega.img`, a shipped apps floppy (`apps*.img`)
  `build/megaapps.img`; any other image (a category disk, a scratch disk the
  row built) goes in B: as it is - and a shipped SYSTEM floppy in B: is
  `mega.img` too (the rows that boot C: and install or upgrade from B:). No
  fixed disk unless the machine name has `_hdd` or the row hands one over
  (below).
- **symbols** - `os88sym` reads `build/megak/kernel.map` under `OS88_MEGA`
  (`$OS88_MEGA_BUILD` for another tree); nothing is re-assembled.
- **memory** - `read`/`write` are linear (`xp`/`writemem`/`setpmem`).
  `readseg(seg, off)` takes a real segment OR an LDT selector, and every
  package region and claim is a selector here, so a row that computes
  `seg << 4` itself calls `m.lin(seg, off)` instead. A move REBASES a
  selector and never changes it (KERN-MEGA-MEMORY-PLAN MC7.4), so a row
  asking "did it move" or "does anything still name the old block" asks it
  of the PARAGRAPH under each word - `tests/sheetmove.py`'s `seg_key(m, v)`,
  read at the moment the row means (a selector's base read later is where
  the block IS) - and a 1MB machine (`@OS88_BOCHS_MEGS=1`) is the one with
  a conventional heap to move.
- **time** - `status()["cycles"]` is the GUEST's time at 4.77 MHz, read
  off Bochs's instruction count (*The clock*, above), so a guest budget
  means what it means on MartyPC and a loaded box cannot shorten it.
  Nothing measured in cycles is a measurement of an 8088 here. The
  clock STARTS at the host's local time, so a row whose answer depends on
  the hour pins it with `@OS88_BOCHS_TIME0=<Unix time>` (`mega-dosdir`,
  KERN-MEGA-PLAN 14.114).
- **the glass** - `fbuf()` is the Bochs window's guest area (`xwd`), with
  the DAC's six bits widened as a VGA reads them (`FC` -> `FF`); the window
  is refreshed 20 times a second. `screen()` reads B8000.
- **the FDC** - `disk()` counts `<<READ DONE>>`/`<<WRITE DONE>>` in Bochs's
  own floppy log (`debug: floppy=report`), which is all os88ui and `ui_done`
  use it for.
- **breakpoints** - `exec`/`execseg` are `lb`, and what Bochs's fixed pool
  of linear slots cannot take goes to `pb` (paging is off: the same address);
  an address neither takes is an error. `mem`, `int` and `io` are refused.
  `.cold` and `.mega` symbols are where kern_mega RUNS them above 1MB once it
  is in protected mode (`sym()`), and a stop's `flat_ip` is its LINEAR
  address - BpTrace keeps all of it rather than MartyPC's 20 bits. `park`
  sets CS:IP on a stopped machine (`set cs` / `set eip`).
- **input** - a key sent before any mouse event captures the window first
  (keys went nowhere until something focused it); a held Ctrl survives the
  re-capture after a debugger stop (that is Ctrl + middle click, and let it
  go); a double-click's four edges are QUEUED ON A STOPPED GUEST
  (`queued_input`) - Bochs's GUI drains the X queue in one pass and each
  button change is a PS/2 packet of its own, so the gesture is a few guest
  ms long whatever the host does, inside a `bp_trace` block too (a stop met
  before that pass is recorded for its trace). Sent to a running guest, one
  edge or one chain at a time, the span was the host's. Bochs's SERIAL
  mouse ($OS88_BOCHS_MOUSE=serial) cannot take a queue - it LATCHES the
  button state and polls its idle UART every 100 guest ms
  (iodev/serial.cc), so two edges inside one poll are one packet - so
  there each edge goes out on a STOPPED guest that then runs EXACTLY 0.15
  guest seconds on a time breakpoint (`paced_input`), a poll each and the
  presses 0.30 s (5.5 ticks) apart. It was host-timed - a second unstopped,
  then the edges 0.2 HOST seconds apart - and the clock preload takes a
  descheduled Bochs's time out whole, so on a loaded box two edges fell in
  one poll or the presses past the window: an `open_drive` loop missed 12
  of 30 at load 5 and 25 of 40 at load 7-12 (the drive SELECTED, nothing
  open: mega-irqgrab, mega-dosmouevt, mega-kdnomouse), and 0 of 80 paced.
  With a breakpoint of the row's own armed there is no exact run,
  and the host-timed spacing is the fallback. A key sent while the guest is STOPPED waits for the
  resume and then goes one GUI pass apart: queued in X, the lot reached
  Bochs's 16-scancode 8042 buffer in one pass and a paused
  `os88marty.text_taken` kept four shifted characters of twelve.
- **the boot toast** - kern_mega's `Entered protected mode` is EXPIRED
  (`[toast_die]` wound back, its own path) by `kernel_up` and `UI.up`, since
  it is up for eight seconds after the desktop and a row waiting for "a
  toast" read it as the answer.
- **reset** - a POWER CYCLE: Bochs's debugger has none, so the shim stops
  that Bochs and boots another on the same display from the disks as the
  guest left them, re-arming the breakpoints; SYSTEM.CFG is not re-injected.
- **a floppy swap** - `mount(drive, path)` writes the new disk's bytes
  OVER the drive's image in place while Bochs runs (it reads a floppy image
  per sector, uncached), and tells the guest nothing, as MartyPC's does: the
  kernel notices once the motor has stopped (SPEC.md 18.9.1). The geometry
  is the one Bochs booted with, so the two images must be the same size
  (tests/czto.py, tests/imgswap.py).
- **FLAGS** - `setreg("flags", v)` is `set $eflags`, the only spelling
  Bochs 2.7's debugger takes (`set eflags` is a syntax error, and the shim
  used to send it and answer ok, so a row's injected CF never reached the
  guest - tests/czto.py's CRC error read as a good sector). Measured, that
  command writes bits 0-7 as given, puts its bit 9 in DF and its bit 10 in
  OF, and leaves IF and TF alone: the shim passes DF and OF one bit down,
  and IF/TF cannot be written at all.
- **a menu straight after a launch** - os88ui's `menu_pick` waits, on
  Bochs only, for the bar to carry the named item: a package's window is
  visible (all `open` confirms) before its entry proc has run
  OSAPI_MENU_SET, and at real time a pick read the half-built bar
  (tests/toastfpg.py).
- **scratch disks** - a row that COPIES a shipped floppy to put files on it
  copies `os88marty.system_disk(path)` / `apps_disk(path)`, which are
  `mega.img` / `megaapps.img` under `OS88_MEGA` and PATH otherwise; a
  Makefile-built FIXTURE disk (`make stiktest`'s stik*360.img) is made again
  on mega.img by `tests/megafix.py`'s `system_disk(path)`, and an APPS
  fixture built out of shipped packages (covoxlisten720.img's Tracker and
  Audio) gets every package that has a kern_mega ARM swapped for it by
  `megafix.arm_disk(path)`. Files on an
  `_hdd` machine's C: are `extra=["--put", SPEC]` (os88disk.py's
  `FOLDER:path`).
- **breakpoints** - `exec`/`execseg` are `lb`; `int` is an `lb` on the
  vector's handler - the IDT gate under protected mode, so `int 0` is #DE
  (tests/int0sweep.py); `mem` and `io` are refused. `park` moves CS:IP but
  cannot clear IF (`set $eflags` leaves IF alone - FLAGS above): parked code starts with
  `cli` (tests/evqfull.py).
- **selectors** - `m.lin(seg, off)` reads the LDT or the GDT. kern_mega's
  GDT is an identity map except the kernel's DATA (0010, based at
  KERNEL_SEG x 16; KERNEL_SEG's own 0060 is its read-only CODE) and
  COLD_SEG, whose base is `.cold`'s copy above 1MB (tests/tnest.py).
- **the mouse and keys** - `launch(..., wheel=True)` is an IntelliMouse and
  `m.wheel()` turns it (Bochs 2.7 never clears its last Z, so the shim
  follows each turn with a zero-sum motion, or the next stop sends one more
  notch); `launch(..., noserial=True)` takes COM1 out, QEMU's `-serial
  none` (tests/ps2mouse.py). X KEY AUTOREPEAT IS OFF on the display: X
  repeats a held key as release/press pairs, which Bochs hands the guest as
  break/make, so a held Alt+Enter was a dozen presses.
- **disks** - `disks()` names each drive's image AS MOUNTED, copied before
  Bochs runs, so os88flush's diff has a reference (tests/cfgtrip.py).
- **a write to CODE that has already run is not seen.** Bochs caches
  decoded instructions per physical page and `setpmem` does not invalidate
  that cache, so a row that patches a package's code after the code has
  executed runs the OLD bytes (tests/uidrdis.py, measured by single-stepping
  a patched `stc` that executed as the `push ax` it replaced). Patch before
  the code first runs - uidrdis takes a second boot for it.
- **a row's OWN fixed disk** - what MartyPC is handed, the shim takes:
  `extra=["--mount", "hd:0:PATH"]` (os88marty.launch's own argument), or a
  `run_dir=` from `os88marty.stage_run_dir()` on an `_hdd` machine, whose
  `media/hdds/*.vhd` is the machine's disk (tests/install.py, instkeep.py,
  inststate.py). It is mounted IN PLACE and WRITTEN THROUGH, so the host
  reads back what the guest wrote and the next launch on the same file
  boots what the last one left; its CHS is the VHD footer's (a raw image:
  16 heads of 63 sectors, or `("--chs", "C/H/S")` in `extra`), which is
  what Bochs's BIOS then reports. The machine tries A: then C:, and an A:
  that is all zeros (tests/hdboot.py's BLANK - a MartyPC row's way of
  offering the BIOS C:) or `None` is an EMPTY drive. `m.cimg` names the
  file. Bochs locks a disk image with `PATH.lock` and a stopped Bochs
  leaves it, so the Box removes a stale one before it mounts the file again.
  A disk tool row's red check is a throwaway system disk with the driver's
  bytes patched, booted as `$OS88_MEGA_A` (tests/hdsize.py's history in
  KERN-MEGA-PLAN 14.95).
- **ports, through the GUEST** - Bochs's debugger cannot touch an I/O
  port, so `inb`, `outb` and `planes` are a few bytes of code the guest
  runs: at 0000:0500 (its bytes put back after), entered through GDT
  selector 0050h - the identity map's DATA descriptor there, flipped to
  CODE for the run and back - with their inputs in registers, and
  `pushf`/`cli` ... `popf` round the port work, so it is atomic against the
  guest's interrupts and IF comes back as it was. Each stub sits at a fixed
  offset and its bytes never change, for the decode cache above. `planes`
  STOPS inside its stub with read mode 0 and the plane's Read Map Select set
  and reads A0000 through the debugger, which reads it through Bochs's VGA
  as a CPU would (tests/blitp.py, gorillasreactions.py).
- **refused by name** - `flicker`, `pace` (the frame instrument; the
  guest-seconds `os88marty.pace()` works), `snapshot`. A row built on one
  is not portable. EFLAGS cannot be written either: `setreg('flags')` is a
  no-op, Bochs 2.7's `set eflags` being a syntax error and `set $eflags`
  forcing IF on and garbling the bits above it.

Two knobs reach the machine. `$OS88_BOCHS_MOUSE=serial` swaps the PS/2
mouse for a Microsoft serial one on COM1 - kern_dos's INT 33h serves a serial
mouse and nothing else (kerndos/kdmouse.inc), so a DOS row about the mouse
under a trip wants it. A machine name with `_ne2k` in it (or
`$OS88_BOCHS_NE2K`) adds the NE2000 on slirp - the host is 10.0.2.2,
`$OS88_BOCHS_SLIRP` its host-forwards - and, unless the row passes its own
`cfg`, a SYSTEM.CFG on A: that wants ETHER.DRV (`cfg="keep"` leaves A:'s
own alone); `ne2k_io=` puts the card at another port. `cimg=` boots a row's
own raw hard disk as C: (os88disk.py `--hdd`'s 65/16/63, Box's own, or any
size 16 heads of 63 or 32 sectors divide).

**QEMU's network rows run through `tests/os88qmpmega.py`.** They were
written against QEMU's QMP socket, driven from every side at once (their own
`Qemu` classes, `xmcheck`, `tools/qmp.py`, `mouse.py`, `shot.py`), so under
`OS88_MEGA` the launchers in `tests/os88qemu.py` (`make_test`) and the rows'
own (`httpmount.QH`, `httpinstall.Q`, `vidxms.Q`, `wirezone.launch`) start a
`Server` instead: a kern_mega Bochs with the NE2000, behind a server on the
same socket path that answers QMP's `human-monitor-command` (`xp`,
`pmemsave`, `sendkey`, `mouse_move`, `mouse_button`, `screendump`, `info
status`, `stop`/`cont`, `system_reset`, `quit`), `input-send-event` and
`query-status`. A `mouse_move` moves an INTENDED position only, clamped as
the kernel clamps it, and the next command drives the guest there closed
loop - so tools/mouse.py's pin-and-walk lands where it meant to. A row reads
a segment it took out of the guest through `os88qemu.lin(seg, off)` (a
selector's base here) and a driver's through `os88qemu.drv_base`. A system
disk FIXTURE (ether360.img, telnetsys.img, httpmount.img, a row's own copy)
is rebuilt on mega.img by `mega_twin` - the files in which it differs from
the shipped disk of its geometry, never one the kernel builds - and B:, C:
and a twinned A: are the live images through links, so a row reading its
disk mid-run or after `quit` reads what the guest wrote.

**Bochs's slirp is not QEMU's, and two rows are blocked on it** (telansi,
telzm; docs/plans/KERN-MEGA-PLAN.md's 14.85-14.89): its `pktlog` (a
`# slirp config` file with `pktlog = FILE`, through `$OS88_BOCHS_SLIRP`)
shows a host's FIN re-sent every ~120 ms with no back-off, SYN+ACKs four at
a time and a stream reset after a run of fast retransmits - against
ETHER.DRV, which is POLLED and drains its ring only when a package calls it.
A row that opens a dozen sessions one after another, or streams thousands of
two-byte segments, loses some of them there and not on QEMU. **Both rows run
on 86Box's slirp** (`OS88_MEGA_EMU=86box` on their megaport.txt lines). The
QMP server above takes that backend too, and telansi then found a kern_mega
keyboard defect that Bochs's slirp had kept it from reaching: Ctrl+] was
ASCII 0 (KERN-MEGA-PLAN 14.110).

A port is a line in `tests/megaport.txt`; `tests/suite.py` registers it as
`mega-<name>`, the same command under `env OS88_MEGA=1`, `needs=("bochs",)`.
An upper-case `NAME=VALUE` on the line goes into the row's environment
beside it (`irqgrab OS88_BOCHS_MOUSE=serial`).

## Reading it

* **`OUT/e9.out`** - the kernel's own trace. A `kern_mega FAULT vector VV
  stack: ...` line is a fault with sixteen stack words and the registers;
  MEGARUN names every package before it starts one. The menu-bar diagnostics
  (`NMI`, `RST`, `FDC RESET`, `RFL`, `Q`, `DOSP`) are echoed here too - on a
  `MEGADIAG=1` build (`make megadiag`), the only one that draws them since
  KERN-MEGA-PLAN 14.65.
* **`OUT/bochs.log`** - Bochs's own errors. A triple fault reads
  `exception(): 3rd (13) exception with no resolution, shutdown status is
  0ah, resetting`; one of those per reflection is NORMAL on the 286 path, which
  Bochs does not take anyway (blind spot 2).
* **`dbg`** - breaks in with SIGINT, runs Bochs debugger commands, prints what
  they wrote and continues. Kernel memory is at **linear 0x600 + the symbol's
  offset** (`KERNEL_SEG` is 0x60):

  ```
  grep -E " mega_rstn$" build/megak/kernel.map     # Virtual column = offset
  python3 tools/os88bochs.py dbg "xp /4bx 0xD2F7"
  python3 tools/os88bochs.py dbg "setpmem 0xD0F6 1 1"   # poke a byte
  python3 tools/os88bochs.py dbg "r" "sreg"             # registers, selectors
  ```

  **Take the map from the SAME build.** A failed `make mega` truncates
  `kernel.map`, and a test build with extra code moves every `.bss` offset
  after it. Both have produced plausible wrong numbers here.
* **A fault's CS:IP**: CS 0060 is `.text`, CS `COLD_SEG` (`kernel.asm`'s
  `COLD_HOME`) is `.cold` at its vstart, 80h on kern_mega since KERN-MEGA-PLAN
  14.45 (4000h before it), and CS `FFD8` is `.mega` at 0. A CS with bit 2 set is an LDT
  code alias, whose image is segment `(CS & ~7) << 2`. Resolve its IP against
  that image's own `nasm -l` listing, assembled with the same defines as the
  shipped one (SOUND.DRV, for instance, is `-DPICOMEM`).

## Recipes

**Boot to the desktop and look.**
```
python3 tools/os88bochs.py boot
until grep -aq "protected mode" build/bochs/e9.out; do sleep 3; done; sleep 15
python3 tools/os88bochsui.py shot desk.png
```

**Start every package and charge each fault to the package that caused it.**
`sweep` builds C: with MEGARUN and a batch of packages, opens C:\RUN, starts
each one five seconds apart, and moves on to the next boot at a fault or a
minute of silence. A screenshot goes in OUT for every start.
```
python3 tools/os88bochs.py sweep --batch 6 build/*.o88
```

**Sound.** `boot --sb --cfg 1`, open B: (591,105), MEDIA, then double-click
BEVERLY.MOD (Tracker) or a WAV. `OUT/sb.wav` grows while it plays.

**Copy a file C: to B: and check it.** Put a file in C:\RUN by naming it
after `boot` (`--c run` stages the arguments), copy it with the Disk window's
Edit menu, then compare on the host:
```
python3 tools/os88fat.py cat -o got.bin build/bochs/b.img NAME.BIN && cmp want.bin got.bin
```

**The hard-disk installer, and boot from what it wrote.**
```
python3 tools/os88bochs.py boot --c blank --cfg 2 --out build/bochshd
#   Control Panel -> Hard Drive -> Format (click twice), Close, Install,
#   Install (click twice); wait for "Done - remove the floppy, Restart"
python3 tools/os88bochs.py stop
python3 tools/os88bochs.py boot --c keep --bootc --noa --out build/bochshd
```

**Restart.** System menu -> Restart. kern_mega warm-resets through the 8042,
and the second `kern_mega: protected mode` in e9.out is the proof.

**Count the #GP fixups: the trap census** (KERN-MEGA-PLAN 10.3, items
55-62). Every fixup is correct and costs a fault, so a loop that leans on one
is slow without looking broken. `make megatrap` builds the census kernel into
`build/megatrap/` (`MEGATRAP=1`: one E9 line per #GP - error, CS:IP, DS, ES,
and the name in the image's header), and `--build` boots that tree:
```
make megatrap
python3 tools/os88bochs.py boot --build build/megatrap --sb --cfg 1 \
    --b MYDISK.img --btype 1_44 --out build/census/x
#   ...open the program and USE it: starting one traps almost nothing
python3 tools/megatraps.py build/census/x/e9.out --top 20
```
`megatraps.py` ranks the sites, names the fixup that took each (`xfer`,
`write`, `null0`) and resolves the IP in the image's own listing - by header
name, or `--lst 1600=part.lst` for a nameless image such as a PiXEL part.
A `sweep --build build/megatrap --linger 60` leaves a boot running after
MEGARUN's last start, for the loops a program runs on its own. From Python,
`sweep(..., parallel=True)` boots every batch AT ONCE, one display each
(tests/megatrap0.py), and only what a FAULT left unreached goes round again
one boot at a time.

**Count something per second without a debugger.** The kernel's `mega_rfsec`
(once a second, from the tick) and `mega_tell` (from `ui_task`, the only
place a toast may be staged) are the pattern. A new counter belongs there, not
in an ISR that draws - and inside `%ifdef MEGA_DIAG`, since a shipped
kern_mega puts no field reading on the glass (KERN-MEGA-PLAN 14.65).

## Things that cost an hour each here

* **ONE Bochs per DISPLAY** (and as many displays as you like - `Box(display=)`
  above). Clicks, keys and screenshots go to whichever
  window is on top of the display, and two machines on one read exactly like a
  guest that froze. `boot` refuses while one runs ON ITS DISPLAY. Two at once
  is two displays: `OS88_BOCHS_DISPLAY=:97 tools/os88bochs.py ... --out
  build/bochs-<name>` (and the same variable for `os88bochsui.py`). `stop`,
  and the `already running` check, count only the bochs-bin and fifo holders
  whose environment names this display - they used to be box-wide, which
  ended every other agent's machine. `pkill bochs` does NOT match: the
  process is `bochs-bin`, so use `os88bochs.py stop`.
* **Capture the mouse after every boot**, or moves go nowhere.
* **The harness copies A: from `build/mega.img` every boot.** Rebuild with
  `make mega` before you boot, or you test the previous kernel.
* **A throwaway test build must be reverted.** The pattern used here: copy
  the file aside, patch, `make mega`, copy it back, check `git diff`, and grep
  for your `TEST ONLY` marker. Then rebuild before you ship anything.

## Blind spots: where Bochs does not stand for the Victory 286

1. **It cannot run two machines at once** (above).
2. **Its CPU is a 386**, so a reflection leaves protected mode by clearing
   CR0.PE. The 286 path - a CPU RESET through the BIOS's shutdown code
   (CMOS 0Fh = 0Ah), back at `mega_r286` - does not run under Bochs, and
   forcing it does not help: Bochs's triple fault resets the WHOLE machine,
   devices included, where an AT resets only the CPU. A forced-286 build hangs
   opening C:. Two field defects lived only on this path (KERN-MEGA-PLAN 10.3
   item 24 is one). Whatever touches `mega_r286` was the field machine's to
   test, and is now **86Box's first** (docs/86BOX.md): `tools/os88box.py
   launch 286-mega` is the owner's mr286, a real 286 whose shutdown resets
   only the CPU, driven headless - and its `faults` verb is what found
   KERN-MEGA-PLAN item 88, a boot loop that existed only on a 286 BIOS.
3. **HDD.DRV takes rung 1, native IDE,** on any 286 or later, so a Bochs C:
   never goes through the BIOS. On the Victory, C: is a BIOS-only PicoMEM and
   every C: call is a reflection. To exercise BIOS-backed C: under Bochs,
   build a throwaway HDD.DRV whose rung-1 probe is skipped
   (`drivers/hdd/hdd.asm`, the `cmp al, CPU_286` before `hd_at_channel`).
4. **There is no PicoMEM.** Its Sound Blaster is an option ROM's multiplexer
   on one IRQ line, raising the SB's IRQ as a software `int 8+n` and copying
   each DMA block itself (KERN-MEGA-PLAN 10.3 items 18-24). A Bochs SB16 is a
   plain card. The reflection machinery can still be exercised by a throwaway
   build that routes IRQ 5 through `mega_irv` and replays it to the driver's
   alias (item 18 has the shape), but the card's own behaviour cannot be.

5. **Its floppy controller ignores the data rate.** Every floppy mount's
   first read (LBA 0, at the 9-sector fallback geometry) makes `mega_fd13`
   guess 250 kbps, which a real 765 refuses for a 1.44MB disk and the BIOS
   then reads through a reflection. Bochs reads it natively, so no run here
   prints `FD BIOS` (a `MEGADIAG=1` toast; E9's `kern_mega: floppy fell back`
   line on every build) and that fallback is the field machine's to exercise
   (KERN-MEGA-PLAN 10.3 item 29).

The field machine is the owner's Packard Bell Victory 286 (docs/FIELD-MACHINES.md).
Anything in the five blind spots has a menu-bar diagnostic on the
`MEGADIAG=1` build (`make megadiag`, KERN-MEGA-PLAN 14.65) and a line saying
what a photograph of it should show; the shipped kern_mega draws none.
