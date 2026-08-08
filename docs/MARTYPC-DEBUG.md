# The MartyPC debugger — an instrument the guest cannot feel

**What it is.** A remote debug server bolted into [MartyPC](https://github.com/dbalsom/martypc)'s
headless frontend, and a host client. It gives a process on your machine
memory, registers, I/O ports, breakpoints, single-step and cycle counts on a
running os8088, with **no code in the guest at all** — no driver, no UART, no
interrupt, not one guest cycle spent.

**Why it exists.** 86Box has no automation socket of any kind and a real 5150
has no debugger, so until this the only way to ask "what does the kernel
think" about a machine outside the container was to ask a human for a MartyPC
dump by hand (docs/FIELD-MACHINES.md). That document rates a dump the highest
instrument in the register — *"ask for a dump whenever the question is 'what
does the kernel think'"* — and it cost a menu click and a file transfer. This
is that instrument as a command.

**It is pinned and static, on purpose** (`tools/martypc/UPSTREAM`). A debugger
that changes under you is one more variable in a session whose whole point is
removing them; re-pinning is a deliberate act, not maintenance.

**THIS IS WHAT YOU DEVELOP ON.** Anything that runs on an 8088 — which is
the whole of this OS bar the 286/386 targets, and now includes **all three**
of SPEC.md §39's adapters, input, screenshots and sound — is tested here.
QEMU is a fallback with a three-item list (286/386, **rung 1** of the
hard-disk driver, and SPEC.md §9.5's COM2/cross-wired/modem mouse cases);
docs/TESTING.md states it
in full, and states it as a list on purpose, so that "a legitimate need" is
something you can check rather than something you can argue yourself into.
The 5150 remains the only instrument for anything with a disk in its timing.

`make marty` builds from source with cargo, which costs a few minutes the
first time in a fresh container — **build it at the start of a session rather
than when you first need it**, because the moment you first need it is the
moment the cost feels like a reason to type `make test` instead.

---

## The one thing it is not: a disk

**MartyPC is cycle-accurate. It is not disk-accurate.** It models the 8088's
instruction timing, its prefetch queue and its bus contention, and it does not
model a platter turning at 300 rpm, a head seeking, or a 2:1 interleave.
PERFORMANCE.md Set 11 measured the gap on the same test, the same kernel and
the same media:

| | real 5150 | MartyPC |
|---|---|---|
| read 16 KB, cold motor | **8.07 s** | **0.27 s** — 30x fast |
| boot | **38,886 ms** | **2,306 ms** — 17x fast |

So: **if a disk is anywhere in the path, the number this tool gives you is
wrong, and wrong by more than an order of magnitude in the flattering
direction.** That catches a great deal that is not obviously about disks — a
boot time, a package launch, a Tracker module load, a `SYSTEM.CFG` save, the
Control Panel closing. When any of those is the question, the instrument is
the machine in docs/FIELD-MACHINES.md and there is no substitute.

**It will not catch a disk CORRECTNESS bug either, and that is the sharper
half.** SPEC.md §18.91's `AL` bug is the worked example: `dsk_xfer` asked the
BIOS for nine sectors, the BIOS moved nine, and answered `AL = 1` — and the
kernel believed `AL` and re-read the rest one sector at a time. On the 5150
that was 148 sectors in 34 `int 13h` calls for a 32-sector file, 4.6x the
traffic, and it made the *batching optimisation measure slower than no
batching*. **The same binary on the same image under QEMU moved 34 sectors in
6 calls** — correct, fast, and completely silent about the bug. The boot
sector carried the identical bug for as long again and it took the 5150 plus
SPEC.md §18.94's counters to find either. An emulator's floppy controller
returns what its author believed the hardware returns; real hardware is under
no such obligation, and the whole class — `int 1Eh`'s parameter table, short
`int 13h` reads, BIOS interrupt stack depth — is behaviour an emulator smooths
over rather than reproduces.

Read that as a boundary on the tool, not a complaint about it: everything on
the CPU side agrees with the 5150 to within 0–4% across 45 of 47 `gfxbench`
rows, which is the closest any emulator has come here.

---

## Build and run

Needs `cargo` (Rust) and, on Linux, `libudev-dev` + `pkg-config` — MartyPC
depends on `serialport`, whose build script hard-fails without them.

**In a fresh container, `apt-get update` FIRST.** The shipped index names
`libudev-dev_255.4-1ubuntu8.14`, which has been superseded and removed from
the pool, so installing it straight off 404s — and since this is the default
test target, that 404 is the first thing a session hits. A refresh is the
whole fix; it resolves to a version that exists (`…8.16` today) and installs.
**Do not pin a version here.** CLAUDE.md's QEMU recipe pins one deliberately,
for the opposite reason — there the `-updates` build is the broken one and an
older version is wanted — and applying that shape to `libudev-dev` reinstates
the same 404. Skipping the deps entirely does not fail at apt at all: it fails
minutes later inside cargo, on `serialport`.

```sh
tools/martypc/build.sh              # clone at the pin, patch, stage, build
cd build/martypc/run
MARTYPC_DEBUG_ADDR=127.0.0.1:9001 ./martypc_headless \
    --mount fd:0:media/floppies/os8088-360.img &

python3 tools/os88marty.py 127.0.0.1:9001 run
python3 tools/os88marty.py 127.0.0.1:9001 verify
```

`--mount fd:N:path`, not `floppy:` — the device word is `fd`, `hd` or `cart`.
Copy `build/os8088-360.img` into `build/martypc/run/media/floppies/` first.

**The machine starts PAUSED**, whatever `auto_poweron` says, and `run` starts
it. A debugger that attaches to a machine already millions of cycles into its
boot cannot breakpoint anything it wanted to watch, and "it had already
happened" is the one failure a debugger must not have.

---

## The machines

`tools/martypc/configs/os8088_machines.toml` is appended to MartyPC's own
`ibm5150.toml` by `build.sh`:

| config | what it is |
|---|---|
| `os8088_5150_cga` | the default: IBM 5150, 8088 at 4.77MHz, 640K, CGA, real 1982 IBM BIOS |
| `os8088_5150_herc` | the same with a Hercules — MartyPC models it as an MDA **subtype**, so the block needs `subtype = "Hercules"` as well as `type = "MDA"`, and SPEC.md §39.1's probe is what decides |
| `os8088_5150_cga_gla` | the same with GLaBIOS |
| `os8088_5150_sb` | the same with an AdLib **and** a Sound Blaster (DSP 2.01, 0x220, IRQ 7) |
| `os8088_5150_sbonly` | ...and with the FM half taken out: a DSP at 0x220 and **nothing at 0x388**. No real card is built that way, which is why it needs an emulator — it is SPEC.md §51.3.1's jumpered-off-FM case, and `_sb`/`_sbonly` are one pair with `make SNDSNIFF=sb` between them |
| `os8088_xt_vga` | an IBM 5160 XT with GLaBIOS and a VGA — SPEC.md §39's mode 12h |
| `os8088_xt_hdd` | the same XT with an **XT-IDE** controller — SPEC.md §52's rung 0 |

The first five are shaped after docs/FIELD-MACHINES.md's calibration machine,
as closely as MartyPC allows.

**`subtype = "Hercules"` is load-bearing and its absence is silent.** Without
it MartyPC builds a plain MDA, whose `mem_mask` is `MDA_MEM_MASK` = 0x0FFF —
so the card decodes **4KB and mirrors it eight times** across the 32KB
aperture, and the mask only ever moves to `HGC_MEM_MASK_HALF` inside an
`if let VideoCardSubType::Hercules`. The kernel's own probe is unaffected and
still reports Hercules (it programs 3BF/3B8 and reads the geometry back
correctly: `[vid_w]`/`[vid_h]`/`[vid_stride]` = 720/348/90, `[vid_mono]` = 1),
so **everything on the guest side looks right** — while every write above
0x0FFF aliases on top of the first 4KB.

What it looks like when it is wrong: `shot` returns a **sheared** picture with
the desktop repeated down the screen, because it is decoding §39.3's four
banks out of one mirrored page; `shot --rendered` returns an **all-black**
720x350, because the card is rasterising MDA text out of graphics bytes. Both
are pictures rather than errors, which is the failure mode this file warns
about elsewhere. The one-command diagnosis is to dump the aperture and test
it for period: `dump 0xB0000 32768` and check whether byte *i* equals byte
*i*+4096.

**The hard-disk one tests RUNG 0, and that is the point.** SPEC.md §52.1's
rung 1 reads the IDE task file directly and is gated on `CPU_286`, because an
8088's `in ax, dx` is two 8-bit bus cycles at the same port and loses the
drive's high byte — so on the machine this project is about, an option ROM
answering `int 13h` is the only transport there is, and QEMU (a modern CPU
and an ATA disk at 1F0h) can only ever test the other rung. The controller is
**XT-IDE** and not the IBM/Xebec because MartyPC's romdef matches the Xebec's
BIOS by MD5 alone and that ROM is IBM's; the XT-IDE entry matches by
filename, and `ide_xtl.bin` — XTIDE Universal BIOS, GPL — already ships in
`media/roms/XUB/`. So this machine costs no new asset either.

The disk is MartyPC's own bundled `media/hdds/default_xtide.vhd`: 615/4/26 =
**63,960 sectors**, just under SPEC.md §18.7's 65,535-sector volume cap.
**Copy it rather than mounting it in place** — a format is a write, and a
test that mutates the run tree's shipped image behaves differently the second
time:

```sh
cp build/martypc/run/media/hdds/default_xtide.vhd /tmp/scratch.vhd
MARTYPC_DEBUG_ADDR=127.0.0.1:9001 ./martypc_headless \
    --machine-config-name os8088_xt_hdd \
    --mount fd:0:media/floppies/os8088-360.img \
    --mount hd:0:/tmp/scratch.vhd &
```

Verified end to end: the Drivers page loads `HDD.DRV`, its own Control Panel
page appears (SPEC.md §31.9) reporting **`BIOS0  615x 4x 26  31M`** — the
geometry MartyPC mounted, read back through `int 13h` — Format lists the
partition table, Mount says `Mounted 1 volume` and puts an **`HDD C`** zone on
the desktop, and opening it lists the DOS filesystem on the shipped image
(`COMMAND.COM`, `CONFIG.SYS`, `AUTOEXEC.BAT`, `Free 31760K`).

**Two bugs in headless had to be fixed to get there, both of which look like
the driver failing.** Attaching a VHD is the eframe frontend's job, and the
headless `insert_vhds()` that mirrors it had a `hdc_mut()` — the **Xebec
alone** — so an XT-IDE machine took the else branch and logged "No Hard Disk
Controller present" while having one. And it resolves a drive's name through
the resource manager, which only ever scans `media/hdds/`, so
`--mount hd:0:/abs/path.vhd` failed with "File not found scanning Vhd
directory" — a path is taken as a path now, which is what a scratch copy
needs.

**The VGA one is an XT and not a 5150, deliberately.** An 8-bit ISA VGA card
in a 5160 is a machine people actually built; a VGA in a 5150 is not, and the
first version of this config was one. Nothing is lost by moving — the 5160 is
the same 4.77MHz 8088 on the same bus — and nothing was ever at stake, because
**this config could never have been a timing instrument**: the calibration
machine has a Hercules and a CGA in it and no VGA at all, so there is no field
number for a VGA figure to be compared against. It is a correctness
instrument, and GLaBIOS boots it faster.

**Use the IBM ROM for anything you will quote.** GLaBIOS is a modern
reimplementation and is optimised in ways the 1982 ROM is not, so its POST and
its `int 13h` are **not period timings** — it is the one to iterate against
and never the one to take a number from. The BIOS in `tools/martypc/roms/` is
the 27 OCT 82 `1501476` U33 part, which is the ROM the calibration machine
actually has; MartyPC identifies it by MD5 and the machine configs name that
ROM set explicitly rather than letting `auto` pick.

*(That ROM is IBM's. It is here because the repo's owner put it here, and it
is the one file in this tree not covered by the project's own licence.)*

---

## The protocol

Newline-delimited JSON over TCP, one reply per command. `tools/os88marty.py`
is the client — a CLI, a REPL and an importable `Marty` class.

| command | |
|---|---|
| `status` | exec state, cycles, instructions, CS:IP |
| `regs` / `setreg` | all sixteen-bit registers and flags |
| `read` / `write` | memory, by flat `addr` or by `seg`+`off` |
| `inb` / `outb` | I/O ports |
| `run` / `pause` / `step` / `reset` | execution |
| `bp` | breakpoints: `exec`, `execseg`, `mem`, `memseg`, `int`, `io` |
| `screen` | the video card's text, in text modes |
| `video` | which card, its raster geometry and its display apertures |
| `fbuf` | the card's RENDERED framebuffer as rgb24 — the only route on VGA |
| `flicker` | one sample per DISPLAYED FRAME, and the flash/redraw counts |
| `pace` | per-frame changed counts over a long run — frame pacing / smoothness. `ignore` excludes a rect (a blinking cursor); `video` reports the card's cursor state |
| `advance` | run a bounded amount of GUEST time — `frames=` or `cycles=` |
| `snapshot` / `restore` | fork a holder process; wake it on a port, any number of times |
| `key` | a keypress by MartyKey name — `KeyA`, `Enter`, `ArrowRight` |
| `mouse` | one Microsoft packet: relative `dx`/`dy` and button state |
| `history` / `callstack` | the CPU's own instruction history |
| `quit` | stop the emulator |

Three things about it are load-bearing:

- **Reads do not perturb the machine.** Memory comes back through
  `BusInterface::get_vec_at_ex`, which costs no cycles and only ever *peeks* a
  mapped device. It is `get_vec_at_ex` and **not** `peek_range`, which is the
  obvious choice and does not resolve MMIO at all: it slices the flat memory
  vector, so a read of `0xB8000` returned whatever was in RAM under the video
  card — a screen of zeroes on any machine whose card had never written
  through, with no error to say so. That
  matters more than usual here: MartyPC is cycle-accurate, and an instrument
  that costs cycles cannot measure a machine whose cycles are the thing under
  test. **I/O ports are the exception and say so** — there is no peek for a
  port, so an `inb` is a real bus read and several devices clear a status or
  advance a sequencer by being read at all.
- **`read` resolves MMIO, so video RAM reads like any other memory** — and
  getting that wrong cost an hour, so it is worth the paragraph. It went
  through `BusInterface::peek_range`, which slices the flat memory vector and
  does **not** resolve MMIO, so `0xB8000` returned whatever was in RAM under
  the card: a screen of zeroes, with no error to say so. A machine that had
  POSTed and printed `Disk Boot Fail. You monster.` looked, through `read`,
  exactly like one that had hung. `get_vec_at_ex` is the one to use — equally
  side-effect-free (it peeks a mapped device rather than reading it), a plain
  slice when the range touches no device, so ordinary reads cost what they
  did. `screen` is still the right call for **text** modes, because it asks
  the card for characters rather than making you decode them.
- **`bp` replaces the whole set.** A debugger that can only add breakpoints
  accumulates them until something stops for a reason nobody remembers asking
  for.
- **Resuming from a breakpoint takes a `step` FIRST, and `run` on its own
  wedges the machine.** MartyPC clears the CPU's latched breakpoint flag
  inside `machine.run()`'s `BreakpointHit → Run` transition, and this
  server's `run` sets the state to `Running` itself, which skips that
  transition — so the CPU re-reports `BreakpointHit` at the same address
  forever, **with an empty breakpoint list and `bp` answering `count: 0`**.
  `step` goes through the `BreakpointHit → Step` arm, which does clear the
  flag, so `m.step(); m.run()` resumes. Worth recognising rather than
  re-deriving: every symptom points at the guest — `status` says
  `"breakpoint"` at an address nothing is armed on, and a scripted test that
  polls `state != "running"` reports a *hit* on every later check, so a
  breakpoint that never fired reads as one that fired every time.
- **`execseg` and `memseg` are folded to flat addresses, because the
  segmented breakpoint types do not work.** `BreakPointType::Execute(seg,
  off)` and `MemAccess(seg, off)` are declared in `breakpoints.rs` and matched
  by **neither** CPU — grep `cpu_808x` and `cpu_vx0` for them and you get
  nothing, while their `*Flat` twins are handled in six places each. Passed
  through, they arm silently and never fire. That is measured, not inferred:
  on `0060:37F5`, os8088's timer hook, which executes 18.2 times a second,
  `execseg` never stopped and `exec` on the same address stopped immediately.
  Folding costs one property worth naming — a flat breakpoint aliases every
  `seg:off` pair reaching the same linear address — and on a real-mode 8086
  that is nearly always what was meant.
- **`reset` does not zero the cycle counter.** It is free-running for the life
  of the process, so every span is a delta. A "cycles" figure read straight
  out of `status` after a reset is the age of the emulator, not of the run.

---

## What it is for, and what it is not

**For:** anything on an emulator. `verify` is the one to reach for first —
it dumps `KERNEL_SEG` and diffs it against `build/kernel.bin`, which proves in
one command that the machine is running the build you think it is *and* hands
you every live variable at its listing offset with no instrumentation added.
Breakpoints answer questions that previously needed a knob kernel: an `int`
breakpoint on 13h counts disk calls on an **unmodified shipped kernel**, where
SPEC.md §18.94 needs `make DISKCNT=1` and a test package on the floppy.

**Screenshots, without leaving:** `os88marty.py shot out.png` reads the
framebuffer straight out of VRAM and decodes SPEC.md §39.3's banked layout —
the same arithmetic `tools/hercshot.py` applies to QEMU, so a picture from
either route is the same picture. **Do not start QEMU just to look at the
screen**: if MartyPC is already up, that is minutes of an agent's time for
something one command already answers. Verified against QEMU's CGA on the
same desktop: **60.0% lit in both**, 76,815 pixels against 76,809, and the
six-pixel difference is the clock — MartyPC reads `Jul 04 2026`, which is
SPEC.md §37.90's no-RTC fallback, correctly, because a 5150 has no CMOS.

The card is asked which it is (`video`), never sniffed: an unmapped `0xB0000`
reads as **zeroes rather than erroring**, so "is there something at the MDA
aperture" answers yes on a CGA-only machine. That guess shipped for about ten
minutes and produced a confident 720x348 image of nothing.

`shot` reads **guest VRAM** and is CGA and Hercules only, which is a property
of the format rather than of the tool: both are 1bpp, so the bytes *are* the
pixels. **Mode 12h is four planes behind the Graphics Controller's Read Map
Select** and is not readable as flat memory at all — you would have to drive
the latches to get a plane out.

**`shot --rendered` is the other route, and it covers everything.** It asks
the CARD what it rasterised (`fbuf`) instead of asking memory what is in it,
so it works in every mode on every adapter and comes back as 24-bit colour.
VGA takes it automatically, having no other option. The two are a genuine
cross-check rather than a convenience: on a CGA desktop they produce
framebuffers that agree on **0 pixels of 128,000**, one having walked
SPEC.md §39.3's banked layout in guest memory and the other having come out
of the card's raster. Reach for the VRAM route by default on the 1bpp
adapters anyway, because its output is byte-comparable with
`tools/hercshot.py` and so with every "0 differing pixels" check in this
tree.

Two traps live inside `fbuf`, and both produce a black or sheared picture
rather than an error. `display_buf()` casts the card's own array to `&[u8]`,
and the cards disagree about what an element is: CGA, MDA and EGA hold
one-byte palette indices, **VGA holds packed RGBA at four bytes per pixel**
— and a wrong guess still yields a plausible-looking histogram, because the
VGA's channel bytes are full of `0x00` and `0xFF`. Deriving the size from
`buf.len() / (pitch * field_h)` is the obvious fix and is wrong twice over:
the buffer is allocated at the card's **maximum** raster rather than its
current one, and `field_h` on a double-scanned CGA is twice the rows the card
actually renders. `render_depth()` is the card's own answer and is the one to
use. The palette is the VGA's alone (its DAC is the guest's to program);
the others answer `None` and get the standard IBM 16.

**Input, without a guest module and without QEMU.** This was the last thing
on the "go to QEMU for it" list, and it should not have been. `key` enters
the emulator's keyboard buffer, so the guest sees it through the 8255 and
int 09h; `mouse` builds a **real Microsoft 3-byte packet** and clocks it into
the serial controller, so the guest's own `mou_isr` decodes it. Neither needs
a byte of code in the guest, and both exercise *more* than a poke would — a
debug module writing `[mouse_x]` would skip the UART, the packet decoder and
SPEC.md §9.5's whole port contest, which is the code most likely to be wrong.
It is better than QEMU's `msmouse` on the same grounds: that one is not a
UART-level device and ignores DTR entirely (docs/TESTING.md).

Verified, and the proof is deliberately not a screenshot. `mou_seen` — the
byte SPEC.md §9.4.2 publishes, set by the mouse ISR only on a **complete
decoded packet** — goes 0 → 1 when packets are injected, and the chip menu
opens under a press-drag. For the keyboard, the test is SPEC.md §9.6: on a
machine whose mouse has not spoken, the arrow keys *are* the mouse, so ten
`ArrowRight` presses moved `mouse_x` from 320 to 350. That is the full path —
emulator buffer, 8255, int 09h, BIOS buffer, int 16h, `kbm_poll` — and it is
a path QEMU can barely reach, because there you would have to arrange for a
machine with no mouse.

`os88marty.py` wraps both: `key`, `type_text`, `mouse`, `mouse_move`,
`click`. Long moves are chunked because a packet carries a **signed byte**,
exactly as `tools/mouse.py` chunks for QEMU. `key` names a **MartyKey**
variant — `KeyC`, `Enter`, `ArrowUp`, `Digit1` — the emulator's own
vocabulary rather than a second mapping table here; a bare `'c'` is refused
rather than guessed at.

**The speed scaler is forced to 1.0 by the `mouse` command, and that is what
makes counting in pixels work at all.** MartyPC's mouse defaults to
`DEFAULT_MOUSE_SPEED = 0.25` — a human's acceleration preference — so an
unscaled `dx` of 60 reaches the guest as 15. A script that derives absolute
position the way `tools/mouse.py` does, by slamming into a corner and
counting from the kernel's own edge clamp, then lands **a quarter of the way**
to everything it aims at. Nothing errors: the pointer moves, the chip menu
opens under a press, and every click misses — which reads as a broken
hit-test rather than a scaled delta, and cost a round of debugging before it
was found. There is no TOML key for it in this build (`SerialMouseConfig`
carries only `type` and `port`, and `bus/mod.rs` passes `None`), so the fix
is at the command. In the **GUI** build the same knob is a runtime slider at
**Input ▸ Mouse ▸ Speed**, 0.10x–2.00x, defaulting to 0.5x.

**On 86Box, none of this exists** and the question comes back. There the
keyboard has a zero-code answer anyway — poke the BIOS buffer at
`0040:001A`/`001C`, which `int 16h` reads — and the mouse would need
`DEBUG.DRV` and a guest-side injection verb. Neither is built; both are
wanted only if 86Box automation is.

**Audio capture**, which headless MartyPC did not have at all — marty_core's
`sound` feature was not even enabled for the crate, so a device's samples
went nowhere. `MARTYPC_WAV=/tmp/cap` writes **one 16-bit PCM file per sound
source** at that source's own rate (`/tmp/cap.pc_speaker.wav`), no mixing,
because the speaker and a card run at different rates and answer different
questions. The format is what `tools/sndcheck.py` already parses, so every
existing assertion — RMS, the Goertzel dominant-frequency scan,
`--expect-silence` — works against a MartyPC capture unchanged. Verified by
programming PIT channel 2 for 880 Hz through `outb` alone, with nothing in
the guest involved: `sndcheck` reported **dominant 891.0 Hz**, inside its 5%
tolerance.

Two differences from QEMU's `-audiodev wav` are worth knowing. The capture is
**continuous**, not gated — QEMU's pcspk stream only runs while the speaker
is on, so its file time *is* speaker-on time and a silent boot yields an
empty file; here the file is guest time and silence is silence. And **the
guest is also driving port 61h**, so a tone you open by hand may be closed
again by `snd_tick` a moment later — which is why the run above shows 0.26 s
of clean tone rather than the three seconds it was held for.

**And there is a Sound Blaster now** — `devices/sblaster.rs`, added by our
patch, which was the one real gap left on an 8088. Upstream had `adlib.rs`
(an OPL2 via `opl3_rs`) and `dma.rs` but no DSP, so SPEC.md §34.5's stream
tier could only be reached under QEMU. The card is a DSP 2.01 by default at
`0x220`/IRQ 7 on 8-bit DMA channel 1:

```toml
    [[machine.sound]]
    type = "SoundBlaster"
    io_base = 0x220
    irq = 7
    dsp_version = [2, 1]
```

`dsp_version` is the interesting knob and it is there for one reason: it is
what the driver branches on. At `[2, 1]` os8088 takes the classic
`0x48` + `0x1C` auto-init path; drop it to `[1, 5]` and the same driver has
to re-arm the 8237 per half-buffer instead, which is a different code path in
`sb.inc` that nothing else can make it take. The SB16-only commands (`0x41`,
`0xC6`) are **refused rather than half-implemented**, which is honest: a card
reporting a DSP below 4.00 is a card a correct driver never sends them to.

What it models, in the order it matters: the reset handshake (write 1 then 0
at base+6, read `0xAA` at base+0xA), `0xE1` version, `0xF2` forced IRQ — which
is how a driver *finds* its line, so it fires with nothing running — `0x40`
time constant, `0x48` block length, `0x14`/`0x24` single-cycle and
`0x1C`/`0x2C` auto-init in both directions, `0xD0`/`0xD4` pause and continue,
`0xD1`/`0xD3` speaker, and `0xDA` exit-auto-init-at-the-block-boundary.
Reading base+0xE acknowledges the 8-bit IRQ, and a block completing while the
previous interrupt is **still unacknowledged is counted as a missed ack**
rather than hidden — the guest not keeping up is exactly the thing a
cycle-accurate card exists to show you.

It pulls its bytes through the real 8237 (`do_dma_read_u8`, the same call the
FDC makes) and resamples to the host rate through a carried fractional
accumulator, so a long stream does not drift. Verified three ways:

- **From outside the guest entirely** — buffer written straight into RAM,
  8237 channel 1 and the DSP programmed over `outb`, nothing in the guest
  involved. A 20,000-byte square at `tc=206` (20 kHz, period 20 samples) came
  back as **1.00 s, peak rms 0.7500, dominant 1000.0 Hz** — duration,
  amplitude and pitch all exact. The auto-init variant of the same test
  looped for **53.96 s of guest time** without drifting off 1000.0 Hz.
- **Through os8088's own driver.** The Drivers page loads `SOUND.DRV`, the
  probe finds the card, and the Sound page's third radio button — `Sound
  Blaster` — comes up selected. That is the whole discovery path: reset,
  version, the `0xF2` IRQ probe against four candidate lines with the
  driver's own stubs hooked, and the 8259 mask dance around it.
- **`tests/sbtest`, the gate package**, which is the assertion that counts.
  `g:00000 o:K` in its window and **2.00 s at dominant 1000.0 Hz** in the
  capture — byte for byte the figure `docs/TESTING.md` documents for QEMU's
  SB16. Its underrun leg is the sharper one: `st:1 c:02400` (underrun-paused,
  exactly the 2,400 granted bytes consumed) with **0.30 s of tone and then
  silence** — 2,400 bytes at 8,000 Hz to the sample, and nothing looping.

**One caveat, and it is the `0x10` command.** Direct DAC writes are accepted
and dropped rather than played. Nothing in this tree uses them — os8088's
driver is DMA-only — but a program that does will hear nothing and get no
error, which is the failure mode worth writing down rather than discovering.

**And VGA mode 12h works, which this document said twice that it did not.**
The correction is worth more than the feature, because the mistake was a
*shape*: marty_core ships a register-level VGA — CRTC, sequencer, attribute
controller, graphics controller, a 25.175 MHz dot clock and a `640x480+96+32`
display aperture spelled out as constants — and its `vga` feature is **on by
default**. It rasterises 12h correctly and always did. What was wrong was one
line in the headless crate's `Cargo.toml`: `marty_frontend_common` was taken
with `default-features = false` and nothing added back, so the arm of
`get_rom_requirements` that asks for `ibm_vga` was **compiled out**. The
machine then came up with a VGA card and no video BIOS behind it, the `_ =>
{}` swallowed the requirement, and nothing in the log said a thing.

Two symptoms sent the diagnosis the wrong way and are worth recognising. The
card's `is_in_graphics_mode()` answers **false in mode 12h** — `mode_graphics`
is initialised to false in the VGA and never assigned anywhere, so `video`
reported a text mode on a machine that was drawing a desktop. And the first
framebuffer read came back 57% "index 255", which reads as a plausible
palette histogram and was four-bytes-per-pixel RGBA being read one byte at a
time. Neither errored. `field_w`/`field_h` is the honest question: **800x524
is mode 12h's raster** and a text mode's is not.

Verified end to end: os8088 probes VGA, sets `vid_w=640 vid_h=480
vid_planes=4 stride=80`, and the desktop, a Disk window and Minesweeper all
render — the last with **eight distinct colours** on screen, every one a
standard EGA/VGA palette entry (blue 1s, green 2s, a red 3, the exploded
mine). The VGA BIOS is MartyPC's own bundled `BOCHS-VGABIOS.bin`, LGPL and
shipped with its licence, so this cost no new asset.

`os8088_xt_vga` is the machine. A 5150 with a VGA in it is an anachronism
and a deliberate one: what is under test is os8088's mode 12h path on the CPU
the project is calibrated against.

## Capturing a screen — every mode, including the fullscreen ones

There are **three** capture routes and they answer different questions. Picking
the wrong one does not error; it produces a plausible picture of nothing.

| route | what it is | works in |
|---|---|---|
| `shot` (VRAM) | decodes SPEC.md §39.3's banked **graphics** framebuffer out of guest memory | CGA mode 6, Hercules graphics |
| `shot --rendered` (`fbuf`) | asks the CARD what it rasterised, as rgb24 | **every mode**, CGA and VGA (see the Hercules note) |
| `screen` | the card's text rows as **characters** | text modes |

**`video` reports `mode` and `text`, and that is the discriminator to use.**
It comes from the card's `display_mode()`, derived from its actual registers —
unlike `graphics`, which is a dead field on the VGA and always false. `shot`
now reads it and routes itself, so the failure below cannot happen silently
again.

**The failure it exists to prevent.** SPEC.md §53.4's `FSXM_TEXT80` puts a
fullscreen app into an 80x25 **text** mode; Tracker's XT-mode fullscreen
(§45.13) is the shipped consumer. The VRAM route decodes character/attribute
pairs as a bitmap, so it returns a full-size image made of noise, with no
error and no clue. `shot` now says what it is doing:

```
$ os88marty.py <addr> shot out.png
out.png: card is in Mode3TextCo80 (a TEXT mode) - capturing the RENDERED framebuffer.
  The VRAM route would decode character cells as a bitmap and show you nothing real.
  For the characters themselves: os88marty.py <addr> screen
```

and `--kind cga` against a text mode is **refused** rather than obeyed.

**Every fullscreen mode, and how to capture it.** The nine `FSXM_*` ids
(`apps/os88api.inc`) reduce to three cases:

| `FSXM_*` | what it is | capture with |
|---|---|---|
| `TEXT80`, `TEXT40` | 80x25 / 40x25 text | **`screen`** for content, `shot` (auto-rendered) for pixels |
| `CGA320`, `CGA640`, `HERC` | 1bpp / 4-colour banked graphics | `shot` — VRAM route, byte-comparable with `hercshot.py` |
| `VGA0D`, `VGA13`, `VGA12`, `MODEX` | planar or chunky VGA | `shot` — auto-rendered; VRAM is planar behind the GC and not flat-readable |

For a text mode, **`screen` is usually what you actually wanted**: it gives
you the characters, so an assertion can be `"Pos 08/52" in rows[1]` rather
than a pixel comparison. Verified against Tracker's fullscreen:

```
  TRACKER   Beverly Hills Cop                                       XT 5500 Hz
          Pos 08/52  Ptn 15  Row 33  BPM 125  Spd 07
```

**Flicker is measurable here, and that is PERFORMANCE.md Part 3.1.** The
`flicker` command steps the machine until the card finishes a frame, grabs
the rendered buffer, and repeats — sampling the glass exactly as often as an
eye does, because a CRT shows whatever the raster read on its last pass. It
reports two things per frame: `changed` (the ordinary delta, which prices a
**visible redraw** in frames × the frame period) and `transient` — the pixels
whose value before the operation and after it are the *same*, but which showed
something else in between. That second one is the **double-draw flash** stated
as arithmetic, and it needs no notion of "background" and cannot fire on an
honest change.

```sh
python3 tools/os88marty.py 127.0.0.1:9001 flicker -n 90 --click
```

A Disk window's full repaint measures **11 frames (183 ms) of redraw and 1,963
flashed pixels for 10 frames (166 ms)** on CGA; an idle desktop and a bare
pointer move measure zero, and a Note Pad keystroke flashes nothing in its
text. Three traps, all in Part 3.1 at length: inject the input while **paused**
so the action lands inside the capture rather than racing it; check `settled`
or every count was measured against a moving target; and **always read the
bbox** — a count alone misattributes, which is how 42 pixels of "text flash"
turned out to be the mouse pointer blinking under the gfx lock.

**It does not work on Hercules**, and that is a MartyPC gap rather than a
choice: the MDA does not rasterise Hercules graphics mode, so the rendered
buffer is 0 lit of 252,000 and `frame_count()` never advances. `shot`'s VRAM
route reads guest memory and works there perfectly, which is why nobody had
noticed. Measure the flash on CGA — §39.5 is one renderer for both 1bpp
adapters — and note that `--rendered` is likewise CGA and VGA only.

**It has snapshots, and it did not need a save format to get them** —
docs/SNAPSHOT-PLAN.md is the full pattern, §7 and §8. The headline is that **the emulator is
bit-exact deterministic**: two independent processes reach a breakpoint at the
same 261,943,446 cycles with the same 1 MB memory hash, and stay identical
through injected input. So "continue from a known state" is available today by
replaying the inputs, with no snapshot format at all.

The sharp edge is that **a wall-clock client destroys that determinism**: two
free-running instances paused after the same `sleep(22)` were **21.7 M cycles
apart**. So `advance(frames=…)` / `advance(cycles=…)` is the way to wait, never
`time.sleep`, and `tools/os88drive.py` is the mouse driver paced that way —
two processes running the same script from reset land on the identical cycle
count and the identical 1 MB.

**And `snapshot`/`restore` freezes a state outright**, by forking a holder
process rather than serializing anything, so nothing can be left out of it:

```python
s = m.snapshot()               # {'id': 1, 'cycles': 167309139}
r = m.restore(s["id"], 9995)   # a Marty on the restored machine, byte-identical
r.quit(); r = m.restore(s["id"], 9995)   # …and again, from the same state
```

Unix only, in-memory only, and the mounted floppy is shared rather than rolled
back — SNAPSHOT-PLAN §8 has the limits. Combine it with a `bp mem` watchpoint
to snapshot the instant a value is touched.

**Not for:** the real 5150 — that is `DEBUG.DRV`'s job (SPEC.md §58), and the
two are complementary rather than competing. And not for a machine that is
not an 8088: the 286 and 386 targets are 86Box's.

**And a number from it is still a number from an emulator.**
docs/FIELD-MACHINES.md's first rule is unchanged: a timing goes in
PERFORMANCE.md Part 9 labelled MartyPC, and a dump is evidence about *logic*,
never about time. On the CPU that labelling is a formality — it agrees with
the 5150 to 0–4%. On a disk it is the whole point. What is new is that this one is cycle-accurate for the CPU,
so `step` gives real cycle counts — 50 instructions measured 719 cycles on a
booted desktop, 14.4 cycles per instruction, which is the same class of
figure as PERFORMANCE.md Part 2's instruction floor.

---

## What was verified, and how

All of the following was run end to end in the container, against
`build/os8088-360.img` and the real 27 OCT 82 IBM BIOS:

- The BIOS date string read out of `0xFFFF5` as `10/27/82`, and the reset
  vector at `0xFFFF0` as `EA 5B E0 00 F0` — `jmp F000:E05B`.
- os8088 boots, twice: once from the development tree and once from what
  `build.sh` produces from scratch, with identical results.
- **Reset to the kernel's first instruction is 300,798,299 cycles**,
  23,586,325 instructions, 12.75 cycles each, on a 5150 with the 1982 IBM
  BIOS reading `build/os8088-360.img`. That is an exec breakpoint on `0x600`
  against a `reset`, which is the only honest way to ask it: the first two
  attempts *polled memory* every few seconds of wall clock while MartyPC runs
  faster than real time at a load-dependent rate, and got 81M and 313M cycles
  for the same event on the same machine — a 3.9x spread that was measuring
  when somebody looked.
  **It is a cycle count and NOT a boot time.** Dividing it by 4.772728 MHz
  gives 63.02 s, and that figure is worth nothing: a boot is mostly POST and
  floppy, and this tool is 30x fast on the floppy. The real machine's boot is
  PERFORMANCE.md's 38,886 ms and the only way to move that number is to
  measure it there. What the cycle count IS good for is a **delta** against
  another MartyPC run — that is how you tell whether a change to the boot path
  did anything, which is a question this can answer and the 5150 answers
  slowly.
- `verify`: **71,624 bytes dumped, 1,351 differing (1.89%)** in 183 runs, with
  `boot_ticks` reading 40 live against `0xFFFF` in the file — **byte-identical
  between the development build and `build.sh`'s**, which is the check that
  the vendored patch is the thing that was tested. For scale,
  docs/FIELD-MACHINES.md's hand-taken MartyPC dump was 1,353 differing of
  71,112 — the same instrument, automated.
- `regs` at the desktop: `cs=0060 ds=0060 ss=1260 sp=2228` — SPEC.md §1's near
  model on screen, CS = DS = `KERNEL_SEG` and SS = `LOW_SEG`.
- Breakpoints: an `int 08h` breakpoint fired three times in a row at
  `0060:37F5`, os8088's own tick hook.
- `step 50`: 50 instructions, 719 cycles.
- `screen`: GLaBIOS's POST panel read back in full, including its
  `RAM [ 256 KB OK ]`, `Video [ CGA ]` and `COM [ 03F8 02F8 ]` lines.

---

## Two upstream findings

Both are in `tools/martypc/patches/01-headless-debug-server.patch` and both
are worth offering upstream:

- **`peek_range` was off by one.** (No longer load-bearing for us — `read`
  uses `get_vec_at_ex` now — but still a real bug.) `if address + len < self.memory.len()`
  refuses a range *ending* at the last byte of memory — so
  `peek_range(0xFFFF0, 16)`, the reset vector paragraph and the most-read
  sixteen bytes in an 8088 machine, was refused while fifteen bytes at the
  same address succeeded. `<=`.
- **Two breakpoint types are dead code.** `BreakPointType::Execute(seg, off)`
  and `MemAccess(seg, off)` are in the public enum and no CPU matches on
  them — so a frontend that offers them offers controls that arm and never
  fire. This works around it (above); upstream should either implement or
  retire them. **A control that looks live and is not** is the sharpest kind
  of bug in a debugger, because it makes the *absence* of a stop look like
  evidence.
- **Headless mode never mounted floppies.** `--mount fd:N:path` is parsed into
  `config.emulator.media.floppy` and then nothing reads it — mounting is done
  by the eframe frontend's file manager, which a headless run does not have.
  So a headless machine always booted with empty drives, which GLaBIOS reports
  as `Disk Boot Fail. You monster.` and the IBM BIOS reports by dropping into
  cassette BASIC. Both look like a bad image rather than an absent one.

The server itself is the answer to the crate's own standing TODO — *"We don't
have any backend to run an event loop. If we want to actually run the emulator
now we need some way of controlling / stopping it."* A socket is both.
