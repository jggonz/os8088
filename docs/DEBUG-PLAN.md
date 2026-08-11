# A debug channel for the testing and benchmark kernels

**The question.** QEMU has proved unreliable for some kinds of agent-driven
debugging. 86Box is the higher-fidelity emulator and is already the only place
several things can be tested at all — but every 86Box target in the Makefile is
*interactive*, and says so:

```
# All three carry an OTI-067 VGA, a serial Microsoft mouse on COM1 and 1.44MB
# drives (so they boot the same images QEMU does), and all three are
# interactive: 86Box has no automation socket.
```

So: can 86Box be given a host-side interface — serial, parallel, or ethernet —
that a debugging tool on the host talks to, to send commands, dump memory and
take actions?

**Short answer: yes, with no patch to 86Box and no new emulator — and the
channel should be TWO channels, split by direction.** Serial carries commands
in and small replies out; **bulk output goes out through a raw disk image the
host reads as a file**, because 9600 baud is what an 8088 can receive
(section 2.1) and a whole-segment dump at 960 B/s is 68 seconds. The rest of
this document is which facility, why that one, what it costs in the guest,
and what was rejected.

**Scope: `SERDBG=1` builds only.** The knob follows `DISKCNT=1` (SPEC.md
§18.94) exactly — its own `BUILD=` directory, its own disk image, on the
`VIDSTAMP` so changing it rebuilds, and never in `build/`. Nothing here ships.

> **OUTCOME — both halves are built. Read this document for the reasoning and
> the two others for what exists.**
>
> - **SPEC.md §58 / `drivers/debug/debug.asm`** — DEBUG.DRV, the serial
>   monitor, built as a loadable driver rather than the `SERDBG=1` kernel this
>   plan proposed. A knob kernel is a different binary, so the machine you
>   debugged is not the machine that ships; a driver loads into the shipped
>   one. It is the only half that works on real iron.
> - **docs/MARTYPC-DEBUG.md / `tools/martypc/`** — a debug server in MartyPC's
>   headless frontend. It costs the guest **nothing** (no driver, no UART, no
>   IRQ, not one cycle), answers on a frozen machine, and does what a guest
>   stub structurally cannot: breakpoints, single-step, cycle counts. On an
>   emulator it supersedes the driver entirely.
>
> **And MartyPC is now the FIRST tool to reach for, not the last**
> (docs/TESTING.md carries the ordering). The boundary this paragraph used to
> draw has moved twice and is now somewhere else entirely: its floppy was 30x
> fast and is within a measurement quantum of the field machine (Sets 35/37),
> and it *does* catch SPEC.md §18.91's `AL` bug, because MartyPC runs the IBM
> ROM and the bug is the ROM's. What is left to the 5150 is the CHIP — what a
> real 765 puts in ST1, whether a real drive returns short — and anything
> taken off a GLaBIOS machine, whose `int 13h` is 1.61x lighter than the
> period ROM's (Set 38).
>
> Two things in the plan below were **not** built and the reasons are in the
> outcome docs: the **disk data plane** of section 3 (it needs a `[sch_lock]`
> entry point, which is kernel code — §58.4's divisor switch is the bulk path
> instead), and **Stage 0's trace hook** into the benchmarks. Section 5's
> rejection of "patch the emulator" was **wrong about MartyPC** and right
> about 86Box: the objection was that patching means *building* a debugger and
> owning a fork, and MartyPC already has the debugger — the work was exposing
> it, in ~500 lines against a pinned commit.

**Status of the text below: it is the PLAN, as written before either half was
built, and it is kept that way deliberately** — the outcome box above says
what changed and why, and a plan rewritten to agree with its outcome records
no decision. Every 86Box fact in it is cited to 86Box's own source at `master`
and none of it has been run: the 86Box half is the next pass. The os8088 facts
are read out of this tree.

---

## 1. What 86Box already has

Three separate facilities, none of which needs 86Box modified. All of this is
from `src/char/`, `src/device/unittester.c` and `src/config.c` at `master`.

### 1.1 Character devices on COM and LPT — the one to use

`src/char/CMakeLists.txt` builds six chardev back ends, and `src/char/char.c`
registers these for a **COM** port:

| `serialN_device =` | what it is |
|---|---|
| `serial_passthrough` | a real serial port on the host |
| `pipe` | **named pipe (Windows) / FIFO pair or PTY (Unix)** |
| `file` | write the guest's output to a host file; optionally replay a host file *in* |
| `stdio` | 86Box's own stdin/stdout |
| `loopback` | ties TX to RX in the guest |
| `fujinet` | not relevant here |

Config shape (`src/config.c` ~L1144, ~L3594):

```ini
[Ports (COM & LPT)]
serial2_enabled = 1
serial2_device = pipe

[Named Pipe (COM) #2]
path = /tmp/os88dbg
mode = 0            ; 0 auto, 1 server, 2 client  (CHAR_PIPE_MODE_*)
reconnect = 1
```

**On Unix, `path` is a prefix.** `char_pipe_init` appends `.in` and `.out`
(`src/char/char_pipe.c` ~L446), `mkfifo`s them if absent, and connects. In
server mode the guest's transmit lands in `<path>.out` and the host writes to
`<path>.in`; client mode swaps them, which is how two 86Box instances talk to
each other. **Or `path` may name a character device**: if `stat()` says
`S_ISCHR`, 86Box opens that path `O_RDWR | O_NONBLOCK` and logs
`Connected to PTY` (~L203). So a host tool can `openpty()`, write the slave
name into the config, and get one bidirectional fd with no FIFOs on disk at
all.

The FIFO pair is the better default here: it survives 86Box restarting, it is
a path the Makefile can name deterministically, and `reconnect = 1` means the
host tool can come and go.

**This requires 86Box v6.0 or newer.** Named-pipe passthrough on Linux and
macOS hosts, and the auto client/server mode, both landed in v6.0 (May 2026).
On an older build the only Unix routes are `serial_passthrough` against real
host hardware and `file`.

### 1.2 The Unit Tester — screenshots and a scripted exit, with no guest UART

`[Other peripherals] unittester_enabled = 1` (`src/config.c` L2474) enables
`src/device/unittester.c`, which is spec'd and versioned at
`doc/specifications/86box-unit-tester.md` (v1.0.0). It is *guest-driven*: write
`'8','6','B','o','x'` plus an I/O base to port 80h with interrupts off, then
drive two ports at that base.

Four commands: capture screen snapshot, read a snapshot rectangle as BGRX,
**verify a snapshot rectangle as a CRC-32**, and **exit 86Box**.

Two things about that are worth more than they look:

- **It reads the monitor framebuffer.** On `make xt-hercules` that is a real
  Hercules GB101 being rendered by 86Box — so this is the first mechanism in
  the tree that could pixel-check the *actual* Hercules path automatically.
  Today `tools/hercshot.py` reads guest RAM under QEMU with the framebuffer
  relocated by `HERCSEG=`, which exercises the renderer and never the card.
- **`EXIT` gives 86Box a scripted teardown**, which it does not otherwise
  have. Every 86Box run today ends with a human closing a window.

It gives no access to memory, registers or ports, so it does not answer the
question on its own. It is complementary and it is nearly free.

### 1.3 The POST card — breadcrumbs that survive a hang

`[Other peripherals] postcard_enabled = 1` (`src/config.c` L2473). `out 80h,
al` is two bytes of guest code, touches no device state, needs no
initialisation and cannot block. It is the cheapest possible "how far did I
get" trail, and it keeps working after the machine has stopped.

**Caveat, and it is a real one:** `postcard_setui` (`src/device/postcard.c`)
formats the codes into the 86Box **status bar**, not into a log. Reading it
back needs a screenshot of the 86Box window or a build with
`ENABLE_POSTCARD_LOG` and `-L <logfile>`. So it is a human's instrument as
shipped, not an agent's — worth knowing about, not worth building on.

*(A real POST card in an ISA slot works identically, and a 5150 has slots. That
is a hardware suggestion for the field machine's owner to accept or refuse,
not something this tree can arrange.)*

### 1.4 What 86Box does **not** have

There is no host-driven control channel — no QMP, no monitor socket, no GDB
stub. The CLI (`src/86box.c` ~L881) has `--debug/-D`, `--logfile/-L`,
`--testmode/-T` (whose entry point, `pc_test_mode_entry_point`, is a single
`pclog` and nothing else), `--donothing/-Y` and `--instrument/-J` (behind
`USE_INSTRUMENT`, and a profiling run-length rather than an interface). The
Makefile's comment is correct and stays correct.

**So this is not "QMP but with better fidelity". It is the first automation
86Box gets here at all**, and that reframes the value: the win is not
replacing `tools/qmp.py`, it is that `make xt`, `make xt-hercules`,
`make xt-sound` and the three AT-class targets stop being human-only.

---

## 2. The guest end, and the one thing that fights it

**os8088 owns both UARTs.** SPEC.md §9.5: both COM ports are probed,
programmed, hooked and listened to at once, and the loser is retired with
IER = 0 and its IRQ masked. A debug stub that simply takes 2F8 would be
fighting the mouse prober for it, and losing in a way that looks like a mouse
bug.

**The fix is one line, and it uses a mechanism `mouse.inc` already has.**
`kernel/mouse.inc:129`:

```asm
mou_bases   dw 0x3F8, 0x2F8     ; UART base per port; 0 = nothing answered
```

Every UART access in the module goes through `mou_pout`/`mou_pin`, which
no-op on a zero base — that is how a one-serial-card machine runs the
identical sequence it always did. So:

```asm
%ifdef SERDBG
mou_bases   dw 0x3F8, 0        ; COM2 belongs to the debug stub
%else
mou_bases   dw 0x3F8, 0x2F8
%endif
```

and the mouse is on exactly the code path a one-port machine takes — a path
that is already exercised, already tested, and already the field machine's
(`docs/FIELD-MACHINES.md`: the Portable III's modem is on COM1 and the mouse
on the other port). `[mou_need]` falls to 1 by itself, because "one port is
not a contest" is already how the threshold is derived.

Two consequences to hold on to. The debug build **cannot test §9.5's two-port
contest**, which is fine — that is what `make test MOUSEPORT=com2` is for, and
a debug kernel is not a shipped kernel. And on a machine where the mouse is
genuinely on COM2, `SERDBG=1` costs you the mouse; SPEC.md §9.6's keyboard
cursor is the escape hatch and works by itself, since it keys off
`[mou_seen]`.

### 2.1 Baud is not a free parameter on a 4.77 MHz 8088

This is the part most likely to be got wrong, and 86Box will reproduce the
failure faithfully because it is cycle-accurate at 4.772728 MHz.

**Transmit is self-paced and safe at any rate.** The guest polls LSR bit 5 and
writes THR; if it cannot keep up, it simply goes slower. Nothing is lost.

**Receive is not.** An 8250 has no FIFO — one holding register, and the next
byte overwrites it. An 8086/8088 interrupt response is ~61 clocks before the
first instruction of the handler, plus whatever instruction was executing:
call it ~13 µs at 4.77 MHz before the ISR does anything at all.

| baud | byte time | verdict on a 4.77 MHz 8088 |
|---|---|---|
| 115200 | 8.7 µs | **overruns**; shorter than interrupt latency |
| 38400 | 26 µs | marginal — a nested tick or mouse IRQ eats it |
| 19200 | 52 µs | workable |
| 9600 | 104 µs | comfortable, and what the stub should default to |

One divisor sets both directions, so 9600 both ways gives ~960 B/s and a 64 KB
segment in ~68 s. **That is the right trade anyway**, because the traffic this
is for is targeted: a 32-byte instance record, the §18.94 counter block, a
window record, a task table. A whole-segment dump is what MartyPC's debugger
is for (docs/FIELD-MACHINES.md), and it should stay that way.

If a *fast* dump is ever wanted on an AT-class 86Box target, 19200 or 38400 is
reachable there because the CPU is 12.5–25 MHz. Do not make the XT default
depend on it.

### 2.2 Where the stub runs decides what it can see

Three options, and they differ in exactly one property that matters:

| site | survives a spinning task | survives IF = 0 | cost when idle |
|---|---|---|---|
| a poll in `ui_task` | no | no | one compare per pass |
| the int 08h tick hook | yes | no | one compare per tick |
| **IRQ3, the byte's own interrupt** | **yes** | no | **nothing** |

**IRQ3 is the answer**, and it is the shape the tree already knows: `mou_isr3`
exists, the vector arithmetic is in `mou_ivecs`/`mou_masks`, and an ISR that
saves the interrupted frame is what a break command needs to report registers.
It costs nothing at all when nobody is debugging, which matters because a
benchmark kernel that pays per tick is measuring a different machine.

Nothing reachable from the guest survives `cli` or a triple fault. For those,
the POST breadcrumbs of section 1.3 and MartyPC's dump are the instruments, and they
always will be.

### 2.3 Where the code lives

`.cold` (SPEC.md §2.6): resident, CS of its own, DS still `KERNEL_SEG`, so it
costs `KERN_BUDGET` and not `KERN_CODE_MAX`. The ISR body has to be in
`.text` — an interrupt vector is a far pointer and the shim discipline does
not survive one — but the command interpreter, the hex formatter and the
tables belong cold. Budget is not tight for a knob build, but the stub must
not change what a benchmark measures: no allocation, no heap claim, no
`.lowbss` growth (task stacks are 256 bytes with a canary, SPEC.md §8, and the
ISR lands on whichever one is current).

---

## 3. The data plane: a raw disk image the host reads as a file

Serial is the wrong pipe for bulk, and section 2.1 says why in one number:
the receive direction caps the divisor, one divisor sets both, and 9600 baud
is 960 B/s. A 64 KB segment is 68 seconds and a megabyte is 18 minutes. But
**the highest-value instrument this project has is exactly the thing serial
cannot carry** — docs/FIELD-MACHINES.md on MartyPC: *"Ask for a dump whenever
the question is 'what does the kernel think'"*, and *"a dump is
self-validating"*, because linear `0x600` onward is `build/kernel.bin` byte
for byte apart from writable state. Today that costs a human a menu click in
a third emulator. A disk data plane makes it a command.

So: the guest writes the payload to a disk, and the host reads the image
file. Four facts make this work.

### 3.1 86Box flushes every sector write to the host file

`hdd_image_write` (`src/disk/hdd_image.c` ~L664):

```c
num_write          = fwrite(buffer, 512, count, hdd_images[id].file);
hdd_images[id].pos = sector + num_write;
fflush(hdd_images[id].file);
```

**The image must be RAW, not VHD.** The `HDD_IMAGE_VHD` branch above it goes
through `mvhd_write_sectors` and has no `fflush` at all, so a VHD is not
guaranteed to be readable underneath a running 86Box.

The host reader must not cache either: `seek`+`read` per request against the
page cache is coherent on Linux and macOS; a buffer held across requests is
not.

### 3.2 The guest end is `int 13h`, not the hard disk driver

This is the decision that makes it cheap, and it goes against the obvious
build. **Do not mount a volume, do not use `drivers/hdd`, do not write a
file.** Three reasons:

- **A debug channel must not depend on the filesystem**, which is the
  subsystem it will most often be asked to debug. Writing a file means BPB
  validation, the FAT window, `dskw_write`'s commit order, a driver attached
  and a volume mounted — every one of them a thing that can be broken at the
  moment you need the channel.
- **On an XT it would not work anyway.** SPEC.md §52 gates the driver's
  rung 1 (the IDE task file) on `CPU_286`, because an 8088's `in ax, dx` is
  two 8-bit bus cycles at the same port and loses the high byte. Rung 0 on
  an XT *is* the controller's option ROM, which is `int 13h`.
- **The pattern and its one safety rule are already in the tree.**
  `dsk_dbg_raw` (SPEC.md §18.94) is a far entry that issues a raw `int 13h`
  **holding `sch_lock`**, and it exists because `tests/sysbench` calling
  `int 13h` itself **hard froze the 5150**: a BIOS runs its disk handler and
  its IRQ nesting on whichever 256-byte task stack is current (SPEC.md §8),
  on top of the caller's own frames. The debug writer inherits that rule
  verbatim.

So the writer is `AH=03h, DL=81h, ES:BX, CHS`, `sch_lock` held, ~40 lines.
The host reader is `f.seek(lba << 9); f.read(n << 9)`. No FAT parser on
either side, no loop mount, and `tools/os88disk.py` is not involved.

### 3.3 Give it a disk of its own, with no partition table

Not the boot disk and not a partition on a shared one. SPEC.md §52.4 mounts
**every** FAT partition it finds, and the allocator would then be free to
write over a debug region living inside one. A second drive (`81h`) with a
zeroed sector 0 has no valid type byte in any slot, so §52.4's walk mounts
nothing and the region cannot collide with anything the OS does.

It also keeps the failure modes apart: a debug disk that fills, tears or goes
missing says nothing about the volume under test.

### 3.4 The two planes must be sequenced, and serial is what sequences them

Polling an image file for "is it written yet" is the whole difficulty of this
design, and the fix is to not poll. The guest replies **over serial** when
the write has returned:

```
host →  d 0060:0000 4000        ; dump 16KB from KERNEL_SEG:0
guest →  ok 81 0000 0020 3f7a   ; drive 81h, LBA 0, 32 sectors, checksum
host  →  (reads 16384 bytes at offset 0 of build/dbgdisk.img, verifies 3f7a)
```

The checksum is not ceremony — it is what separates a channel you can trust
from one that silently hands back the previous run's bytes. It catches a
torn read, a stale mapping, the wrong offset and the wrong image, all with a
16-bit sum the guest computes from the same buffer it wrote.

**Small replies stay on serial.** A 32-byte instance record is 33 ms over the
wire; arranging a disk write for it is silly. The rule is a threshold — under
~256 bytes the answer comes back inline, over it the `ok` line names where
the payload is.

### 3.5 What it costs, and where it does not work

| | serial only | serial + disk |
|---|---|---|
| 64 KB segment | ~68 s | ~1–2 s |
| targeted 32-byte read | 33 ms | 33 ms (unchanged) |
| new hardware in `vm/*/86box.cfg` | none | a controller and an image |
| works on the 5150 | **yes** | **no** |

That last row is the one to weigh. docs/FIELD-MACHINES.md is explicit that the
5150's C: is a real DOS 3.3 install and **nothing may format, partition,
write or delete on it** — so the data plane is an emulator accelerator, not a
field instrument, and the serial output path in Stage 0 stays exactly as
important as it was. It is also why this does not replace serial output: it
supplements it.

**On the controller.** 86Box's `hdc_st506_xt.c` registers
`st506_xt_st11_m` — the Seagate ST11M, which is *the controller the
calibration machine has* (docs/FIELD-MACHINES.md), so the emulated path and
the real machine's are the same option ROM. `xtide` is there too if a
modern XT-IDE is preferred for the debug drive; on the 5150 it would be
refused as non-period, but in 86Box nothing is at stake.

**QEMU gets this for free**, which is worth knowing because it means the guest
code can be developed under the fast emulator: `make test HDD=40` already
attaches `build/hdd.img` as a raw IDE disk. Same `int 13h`, same host-side
read. Verify QEMU's write visibility once — the default `cache=writeback`
should make the data visible to another reader through the host page cache,
but that is an assumption, not something checked here.

---

## 4. Recommended build, in three stages

Each stage is independently useful and none of them blocks the next.

### Stage 0 — one-way trace out COM2. ~30 lines of guest code.

```ini
serial2_device = file
[File (COM) #2]
path = /path/to/build/trace.txt
append = 0
```

Guest side is `dbg_putc` (poll LSR bit 5, `out` to THR) and `dbg_puts`. The
host tails a file. No protocol, no host tool, no ISR.

**This is worth doing on its own merits, before any debugger exists**, because
it fixes three things about how this tree reports results today:

- `gfxbench` and `sysbench` write their reports to a **file on the floppy**.
  A floppy WRITE is ~73 ms/sector on the calibration machine — writes are 3.0x
  dearer than reads and Set 24 is where the two were finally split (this said
  65, a READ figure, and from Set 17 at that) — the harness is
  paying, in the same units it is measuring, for the privilege of reporting.
  Serial costs the guest a polled `out` per byte and no disk at all.
- A run that hangs before it writes its report loses **everything**. Streamed,
  you keep every row up to the hang, which is usually where the answer is.
- `tests/trklog.inc` exists specifically to write one line per system tick to
  `TRKLOG.TXT` for a field bug. That is the same instrument, built the
  expensive way because there was no cheap way.

### Stage 1 — the bidirectional monitor.

Guest: IRQ3 stub, line protocol, one command per line, hex in and hex out.
The command set that earns its keep is small and is dictated by what this
tree's own debugging has actually needed:

| command | why it is in the list |
|---|---|
| `m <seg>:<off> <len>` | read memory inline — "what does the kernel think" |
| `d <seg>:<off> <len>` | **the same, out through the data plane** (section 3) |
| `M <seg>:<off> <bytes>` | write memory — flip a flag without a rebuild |
| `i <port>` / `o <port> <val>` | read/write an I/O port — the 6845, the UART, the OPL2 |
| `r` | the interrupted register frame |
| `b` | the §18.94 counter block, and `mou_dbg_blk` (SPEC.md §9.4.2) |
| `c <seg>:<off>` | far-call a kernel routine, `sch_lock` held |
| `g` | resume |

`m` and `d` are the same operation over the two planes, and **one threshold
picks between them** so a caller never has to: under ~256 bytes the answer is
hex on the wire, over it the guest writes sectors and replies with an `ok`
line naming drive, LBA, count and checksum. Implement `m` first — `d` is
Stage 1b and needs section 3's disk in the machine, which `m` does not.

The rest are conveniences. Note that
**os8088 already publishes fixed-offset diagnostic blocks for exactly this
purpose** — `dsk_dbg_blk` at `0060:000E` with magic `0x4444` (SPEC.md
§18.94) and `mou_dbg_blk` at `0060:0006` with magic `'MO'` (SPEC.md §9.4.2),
both with their layouts asserted at assembly time. Their own spec says why
they exist: *"A test package reads these by offset, off a floppy, on a machine
with no debugger."* A serial monitor reads them directly and the test package
stops being needed to get at them.

Host: `tools/os88dbg.py`, opening `<path>.out`/`<path>.in` **and the debug
image**, with a REPL and an
importable API so a scripted run can assert on a value. It should resolve
symbols from a NASM listing (`nasm … -l`) rather than hard-coded offsets —
docs/FIELD-MACHINES.md's second dump rule is *"re-derive every offset from a
listing of the exact commit"*, and a tool that does it automatically is a tool
that cannot get it wrong.

### Stage 2 — the Unit Tester, for what serial cannot do.

`unittester_enabled = 1`, and a small guest helper (`out 80h` handshake, then
the two-port command loop). Buys automated screenshots of the **real**
Hercules and CGA cards, CRC-verified rectangles for regression checks, and a
scripted `EXIT` so an 86Box run can terminate itself. No UART, no port
conflict, no interaction with stages 0 and 1.

**Check first whether `unittester_enabled` and `postcard_enabled` can both be
on**: the unit tester's trigger port is 0x80 (`unittester.c` L107) and the POST
card's is the same port. They are probably mutually exclusive. Pick the unit
tester.

### The Makefile shape

`DISKCNT=1` is the precedent and it should be copied literally: `SERDBG=1`
joins `VIDSTAMP`, builds into `build/serk/`, produces its own bootable image,
and a `vm/xt-debug/` config carries the `[Ports (COM & LPT)]` and
`[Named Pipe (COM) #2]` sections. `make xt-debug` unprotects the floppies the
way every other 86Box target does, creates the FIFOs if absent, and launches.

The data plane adds `hdc = st506_xt_st11_m` and an `hdd_02_*` row for
`build/dbgdisk.img` — a **raw** image (section 3.1), created blank and zeroed
by the rule that builds it, on drive `81h` with no partition table (section
3.3). It is scratch and stays untracked like everything else in `build/`;
`rm -f build/dbgdisk.img` is the reset.

---

## 5. What was rejected, and why

### Ethernet / NE2000 — no.

86Box has the hardware: `src/network/net_ne2000.c` registers `ne1k` and
`novell_ne1k` (8-bit ISA, so an XT can take one) alongside the NE2000s, with
`net_01_card` / `net_01_net_type = slirp` in the config. So the emulator side
is not the problem.

Four reasons not to:

1. **It costs a driver and a stack.** An NE1000 driver plus enough of
   ARP/IP/UDP to be addressable is thousands of lines of 8086 assembly, in a
   tree whose kernel is guarded to the byte. Against ~200–400 bytes for a
   polled UART stub. Even in a knob build that is not a good trade.
2. **It cannot ever run where the numbers come from.** The calibration machine
   is kept *intentionally, entirely period* — docs/FIELD-MACHINES.md names
   that as a deliberate constraint and pre-refuses "just put a Gotek in it".
   An ethernet card is exactly that proposal. A debug channel that works on
   the emulator **and** on the iron is worth far more than a fast one that
   works on the emulator alone, and serial is the one that does both: a 5150
   has serial ports, and `make comscan` exists to survey them.
3. **Its one real argument was bandwidth, and section 3 takes it away.** A
   whole-memory dump *is* the case ethernet would have been for — and a raw
   `int 13h` write to a disk the host reads as a file gets there in ~40 lines
   of guest code, with no driver, no stack, no addressing and no host-side
   network. Whatever an 8-bit NE1000 could do for a dump, a disk does with two
   orders of magnitude less code.
4. **It adds a whole class of failure** — DHCP, SLiRP's NAT, host firewalls,
   port forwarding for host→guest — between the agent and the answer, in a
   project where the debugging is hard enough already.

There is no remaining case to revisit. If bulk transfer ever outgrows section
3's disk, the next step is a bigger disk.

### Parallel port — no, but for a duller reason.

`char_pipe_lpt` and `char_file_lpt` exist, with nibble / bidirectional /
DirectParallel modes. But XT-era LPT is output-only in the base case, os8088
has no LPT code at all, and the one thing it would be good at — fast one-way
output — is what Stage 0 already gives over a port the kernel already knows
how to talk to. It buys nothing that serial does not, and needs a second
module to do it.

### Patching 86Box to add a control socket — no.

It would be the most capable answer and it puts a fork of the emulator between
this project and its results, which is a maintenance obligation with no end.
Everything above is stock 86Box and stock config keys.

---

## 6. Risks, in the order they are likely to bite

1. **86Box version.** Unix named-pipe passthrough is v6.0+. Check
   `86Box --version` before anything else. Below that, Stage 0's `file`
   device is the fallback and Stage 1 needs a PTY via `serial_passthrough`
   against a host `socat`-made pair.
2. **Serial passthrough is one-way on Linux in at least one open report** —
   [86Box#5132](https://github.com/86Box/86Box/issues/5132), Arch host, build
   6461, guest can send but not receive. That report is against `mode = 3`,
   the *host serial device* route, using the legacy config section — a
   different chardev from `pipe`, and predating the v6.0 rework. It is not
   known to affect the FIFO/PTY route. **Verify RX end-to-end before building
   the protocol on top of it**: a loopback test (host writes, guest echoes)
   is ten minutes and settles it.
3. **86Box rewrites its config on exit.** It has twice put `wp://` back on a
   floppy path — that is what `UNPROTECT` in the Makefile exists for. Assume
   it will do the same to the serial keys, and have `make xt-debug` assert
   them at launch the same way.
4. **The mouse.** `SERDBG=1` costs COM2. If a debug session needs both the
   mouse *and* the channel, COM1 must be the mouse — which it is on every
   `vm/*/86box.cfg` here (`mouse_type = msserial`).
5. **86Box's serial-port count.** `serial1`/`serial2` default enabled,
   `serial3`/`serial4` default off (`src/config.c` L1145). Moving the stub to
   3E8/2E8 to avoid the mouse entirely is possible — `tests/comscan` already
   surveys all four — but those need enabling explicitly and an XT's IRQ
   sharing there is its own problem. Prefer COM2.
6. **Deadlock and re-entrancy.** The stub must not take `gfx_lock` to report
   anything, must not call the file API, and must be safe to enter from an
   interrupt while any task holds any lock. It reads memory and ports; that
   is all `m`, `i` and `b` need. `c` (far-call) is the one command that can
   hang the machine, and it should be the last one implemented.
7. **The data plane's `int 13h` runs on a 256-byte task stack.** This is not
   a theoretical risk — it is docs/FIELD-NOTES.md 10, where `tests/sysbench`
   calling `int 13h` itself hard froze the 5150. `sch_lock` held across the
   call, and the buffer 512-byte aligned so no transfer straddles a 64 KB DMA
   boundary (SPEC.md §2 asserts that ladder for the same reason).
8. **A raw image, and no VHD.** Section 3.1: only the raw branch of
   `hdd_image_write` calls `fflush`. A `.vhd` debug disk would appear to work
   and hand back stale bytes.
9. **A stale image read is silent.** The host must not hold a buffer across
   requests, and the checksum in the `ok` line (section 3.4) is what catches
   it when it does. Do not make the checksum optional "for speed" — the whole
   value of a dump is that it is trusted evidence.

---

## 7. Open questions for the field machine's owner

Not answerable from here, and Stage 0's value on real iron depends on them
(docs/FIELD-MACHINES.md's rule: ask, do not infer from what a machine
generally has).

- **Does the 5150 have a free serial port?** The table records the SixPakPlus
  Rev 1 for RAM and the MM58167 clock and does not say what the mouse is on.
  If the mouse has the only port, Stage 0 on the 5150 needs a second card,
  which is a change to a machine that is deliberately period.
- **Is a null-modem cable to a capture host acceptable?** The reasoning
  differs from the Gotek case in a way worth putting to them rather than
  deciding here: a serial cable changes no timing path the calibration
  depends on — not the floppy, not the disk, not the bus — and it would turn a
  field run from "read numbers off the screen and type them in" into "capture
  a log". Their machine, their call.
