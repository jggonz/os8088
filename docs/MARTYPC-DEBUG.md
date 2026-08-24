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

## The disk: it used to have no platter, and now it has one

**Stock MartyPC does not model a floppy at all.** It models the 8088's
instruction timing, its prefetch queue and its bus contention exactly, and
then hands a sector over the instant it is asked for. Read the upstream source
and there is nothing to look for: `operation_read_data` calls the drive once
and streams the whole run to DMA as fast as the CPU can turn, `command_seek_head`
returns `CommandComplete` in the same breath it is issued so a seek costs
nothing, `FloppyDriveMechanicalState` — `MotorSpinningUp`, `HeadSeeking` — is an
enum **no code anywhere references**, and `media_geom`'s sectors-per-track is
hardcoded to `0` because until now nothing needed it. That is why it read a
16 KB file ~30x faster than the 5150 (PERFORMANCE.md Part 9 Set 11).

`tools/martypc/patches/04-floppy-disk-timing.patch` gives it a platter.

### What it models

| | |
|---|---|
| **Rotation** | a head angle per drive, advanced while the motor turns. 300 RPM (360K/720K/1.44M drives) or 360 (a 1.2M), so a revolution is 200 ms or 167 |
| **Data rate** | 250 kbit/s DD, 500 HD, and **300** for DD media in a 1.2M drive, which spins it 20% fast. 32 us a byte at 250, so a 512-byte sector's data field is 16.384 ms |
| **Interleave** | the physical order of the logical sectors round the track, from the machine config — a raw sector image cannot supply it. **No os8088 machine sets one**: the field 5150's media is 1:1 and the second revolution its track read costs is the ROM's, not the platter's (Set 37) |
| **Pacing** | one sector at a time: wait for it to come round, stream it, wait for the next. Not one lump per run — see the GLaBIOS note below |
| **Seek** | a step per cylinder crossed at the rate the BIOS asked for through SPECIFY, and **no settle** — the settle is the BIOS's own software wait and charging it here counted it twice (Set 37). The platter keeps turning while the head steps |

### Why one mechanism gets all three field rows right

The 5150's three raw `int 13h` rows (Sets 14 and 22) are not three facts, they
are one fact seen three ways — **a sector is readable only as it passes the
head** — and the model reproduces them without being told any of them:

| | field, IBM 5150 | this model |
|---|---:|---:|
| one sector, re-read | 199,106 us | **199,106** |
| a 9-sector track, one call | 398,211 us | **384,480** |
| the same nine, as nine calls | 1,991,057 us | **2,004,789** |

Row 1 is exact and rows 2 and 3 are **one measurement quantum** out — 13,731
us, which is `bl_run`'s tick over the row's four iterations rather than
anything either machine did. Row 2 sits on that boundary: 384,480 is the
figure the *field machine itself* reported for it in Set 14.

The mechanism is worth stating because it is not the obvious one. A 9-sector
1:1 track is **one** revolution of transfer and both machines take **two**,
and the missing turn is not interleave: it is the IBM ROM asking for the
diskette parameter table's 25 ms head settle and spending **52.5 ms** on it,
in a `LOOP $` at `F000:EEB8`, once per `int 13h`. MartyPC reproduces that by
running the ROM. PERFORMANCE.md Set 37 is how it was found and why the
`11,570 B/s ≈ 11,520` agreement that had said "2:1 media" for four sets could
never have discriminated.

### What it does NOT fix, and this is the half that matters

**It changes what the disk COSTS, not what it SAYS** — and that matters far
less than it sounds, because **the BIOS is not modelled at all: it is
EXECUTED.** With the real ROM in `roms/`, MartyPC runs IBM's own `int 13h`, so
a bug in that code is present by construction.

**Measured: SPEC.md §18.91's `AL` bug reproduces here.** Same image, same
machine (`os8088_5150_herc`), shipped kernel against `make DISKAL=1`:

| | shipped (trusts `CF`) | `DISKAL=1` (trusts `AL`) |
|---|---|---|
| int 13h-level reads | 23 | **177** |
| sectors moved | 177 | **846** — 4.8x |
| **longest run** | **9** | **9** |
| `boot_ticks` | 211 | **1152** |

`longest_run` is 9 in both: the kernel asks for nine sectors, is given nine,
and asks again — Set 16's finding restated by the emulator — and the 4.8x
traffic is Set 15's 4.6x. **QEMU missed this because SeaBIOS is a different
BIOS**, not because emulation cannot see it, and that distinction was never
drawn here, which is why MartyPC inherited a blindness it does not have.

So the boundary has moved, and it now runs between the ROM and the chip:

- **BIOS-level** — what `int 13h` returns, `int 1Eh`'s EOT, the ROM's own
  arithmetic: **reproduced**, because it is IBM's code executing.
- **Controller-level** — what a real NEC 765 puts in ST1 on a CRC error,
  whether a real drive ever returns short, what the result phase holds after
  an odd request: still the emulator author's belief, still the 5150's.
- **timing**: worth asking here, still checked on the 5150 before anything
  goes in PERFORMANCE.md.

Not modelled, and each is a place not to trust a number: **motor spin-up**
(the BIOS's own ~1 s wait is a CPU-timed loop and so was always accurate, but
the drive itself comes up to speed instantly), **the PIO paths** (PCjr), and
**Format Track**, charged a flat revolution and never calibrated.
**Hard disks are untouched**: this is the floppy alone.

**The seek WAS the one guess and is not any more** (PERFORMANCE.md Set 36).
It took its step rate from what the BIOS asks for through SPECIFY and its
settle from the DPT, because every raw row this project had read one track and
never moved the head. `sysbench`'s seek block put the 5150 at **7.81 ms a
cylinder against the model's 8.00**, with the break falling between 10 and 20
cylinders exactly where the model puts it. What the same rows then exposed was
MartyPC's **39-cylinder row at 3.000 revolutions against the field's 2.138** —
one symptom of the same thing behind the track row and the boot, which Set 37
identified as the 2:1 media the field machine never had plus a head settle
charged twice. With both gone, **five of the six seek rows are exact** and the
39-cylinder one is a tick short. The step rate is still 8.00 against the
field's 7.81 and is the only part of the seek nothing has re-measured.

### Counting the traffic from outside: `m.disk()`

The counters behind that table are the **controller's**, read over the debug
socket, so the guest needs no `DISKCNT=1` kernel, no test package and no
knowledge that it is being watched — which is the point, because a *shipped*
image is what you want to measure and SPEC.md §18.94's block needs a
knob-built one.

```python
m.disk(reset=True)          # ...drive the thing you care about...
print(m.disk())
# {'reads': 23, 'read_sectors': 177, 'longest_run': 9, 'writes': 0,
#  'seeks': 28, 'seek_cylinders': 52, 'resets': 3,
#  'transfer_ms': 7979.0, 'seek_ms': 716.0}
```

What to read in it: **`longest_run` near the track length** is a kernel
batching properly; **`read_sectors` far above the payload** is §18.91's shape;
**`resets`** is a BIOS giving up, which is how GLaBIOS's 250 ms limit was
found.

### Where a whole BOOT goes: `tools/os88boot.py`

`m.disk()` says what the drive was asked for; this says what the *boot* spent,
phase by phase, and it needs no knob kernel either.

```
python3 tools/os88boot.py --apps build/apps360.img --json build/boot.json
```

It is a **stopwatch and not a sampler** (`tools/os88prof.py` is the sampler,
and is the right instrument for a package that loops). A boot is a straight
line of named phases that each run once, so the exact instrument is a
breakpoint on the RETURN ADDRESS of every `call` in `kmain` — the address a
phase reaches exactly once, where a breakpoint on the callee would fire for
every other caller of `gfx_lock`, `spl_step` and `vid_init` too — with the
cycle counter and the FDC counters read at each. The addresses come out of the
kernel's own listing, asserted byte-identical to `build/kernel-full.bin` on the
way, and the marks are armed one at a time so a mark that is never reached is
a timeout naming itself rather than a silent skip.

Two phases are opened further, because between them they are most of a boot
and neither is one thing. The machine's ROM `int 13h` and the loading bar are
both bracketed inside the boot sector, by taking their return address off the
**guest's own stack** at the entry: the sector relocates itself to the top of
whatever `int 12h` reported, so its addresses are a property of the machine's
memory size and there is nothing here to derive them from — and a far `call`
and an `int` leave CS:IP as the bottom two words of the frame, so one read
serves both.

**Cycles are the answer and the host does not enter into it**: `cycles /
4772727` is seconds on the field machine, the drive's mechanics included (the
model above). Two full runs agree on every row to the cycle.

The walk cross-checks itself and prints the result. SPEC.md §15.4's boot timer
is stamped by the boot sector from BIOS ticks, so it measures everything below
`post`; a complete walk agrees with it to under one 54.9 ms tick, and a walk
that has lost a phase does not.

**On IRON, the knob instead.** None of the above exists on a 5150 — no debug
socket, no cycle counter, no symbol map — so `make BOOTPROF=1` (SPEC.md §15.5)
asks the same question from inside and draws the answer on the desktop when
the first frame is up. Eleven PIT stamps through `kmain`, so the sub-3 ms
phases are real numbers rather than the zero a 54.9 ms tick would report;
everything before `sched_init` is one row in ticks, because the clock does not
exist yet. Boot it, photograph the box, and the first repaint takes it away.
The two instruments agreed row for row on `os8088_5150_cga` — `first paint`
178 ms against 178.2, `mouse_init` 1200 against 1196.7 — which is the check
that neither is measuring something of its own.

**And the machine still decides whether the number may be quoted.** Two of the
four things a boot spends time on are the ROM's own code, so the section
below binds this tool exactly as it binds `m.disk()`: a GLaBIOS twin boots
faster than any 5150 ever did, and only `os8088_5150_cga` and its IBM-ROM
siblings answer for the field machine. What does *not* move with the ROM is
the mechanical column, which is the FDC model Set 37 calibrated.

### GLaBIOS gives up on a floppy op after ~250 ms, and that is still true

It no longer forces a config difference — every machine here carries the same
1:1 media (Set 37) — but it is why a disk number must not be taken off a
GLaBIOS machine. Measured here: **that BIOS abandons a floppy operation after
~250 ms and resets the controller**, three times in a row, after which the
boot sector prints `DSK` — status **80**, a timeout, which
`make BOOTDIAG=1` puts on the screen as two hex digits. It surfaced when the
IBM machines were briefly given 2:1 media, where a 9-sector run takes 372 ms
and can never finish under that BIOS; at 1:1 nothing here reaches the limit
and `os8088_5150_cga_gla` boots `combo.img` in 175 ticks.

Three things said it was the BIOS and not the model. The FDC presents a
correctly BUSY status register for the whole delay (that bit comes from
`self.busy`, which the patch does not touch). **Seeks of 329 ms complete fine
on the same machine in the same boot** — so it is a read timeout, not an
inability to wait. And the IBM ROM completed the identical reads.

That episode produced the one modelling correction worth keeping. The first
version charged a whole multi-sector run as **one silent delay**, which is not
how a drive behaves — a real controller starts DRQing the moment the first
sector arrives and pauses only over the inter-sector gaps. Pacing it per
sector is both more faithful and shorter-gapped. It did not save GLaBIOS,
because that BIOS's limit turned out to be on the whole operation rather than
on silence — but the model is right for the reason it was changed.

### `int 19h` does not restart the machine on a GLaBIOS twin

The Chip menu's Restart (SPEC.md 20.10) ends in `int 19h`, and on
`os8088_5150_cga_gla` that leaves a **blank 80-column text screen with the
tick still running** and never boots — measured on two kernels that differ by
33 bytes, so it is the BIOS and not the build. On `os8088_5150_cga`, the same
script reboots properly: the card comes back to `Mode6HiResGraphics` about
twenty guest seconds later and the desktop is up.

So **a restart is tested on an IBM-ROM machine**, and that is not a
convenience: SPEC.md 9.6.5's freeze only exists on the far side of a completed
reboot, and a machine that never gets there measures the wrong thing twice
over — a `int 19h` that stalls in the BIOS still ticks, so a liveness check on
`0040:006C` alone will call it healthy. Gate on the video MODE coming back to
graphics before you believe anything after a Restart.

### What it measures now

`boot ticks`, os8088's own counter, on the 360KB image:

| machine | stock | Set 35 (2:1) | **now** | field 5150 |
|---|---|---|---|---|
| `os8088_5150_cga_gla` (GLaBIOS) | 41 (2.25 s) | 130 (7.14 s) | **175** | — |
| `os8088_5150_cga` (IBM ROM) | — | 210 (11.53 s) | **185** | 180 (Set 22, `herc.img`) |
| `os8088_5150_herc` (IBM ROM) | — | 211 (11.59 s) | **188** | **180** |
| ...`combo.img`, the like-for-like | — | 222 | **188** | **205** — 0.92x |

The last row is the one to quote: the same image on both machines. **4.4x fast
became 1.17x slow, then 1.27x slow once the platter really turned, and is now
0.92x** — and the middle step is the instructive one, because making the
rotation unconditional was plainly correct and made the headline number
*worse*. That is what said the residual was somewhere a frozen platter had
been flattering, and it was the media (Set 37).

**Where the residual is, measured rather than assumed.** The 8% left on the
boot is not the seek model, which the field pinned in Set 36 at 7.81 ms a
cylinder against the model's 8.00. It is also not the comparison itself,
though that has one caveat worth stating: the field boots a `make field` disk
and this boots the shipped one, so `tools/fieldsize.py`'s rung check is what
says the two are comparable. The one raw row still more than a quantum out is
the 39-cylinder seek, one tick short.

So the standing rule is **relaxed, not withdrawn**: a disk figure from here is
now worth having, and PERFORMANCE.md Part 9's disk rows still come off the
5150.

**And it DOES catch a disk correctness bug, which is where the boundary now
falls.** SPEC.md §18.91's `AL` bug is the worked example: `dsk_xfer` asked the
BIOS for nine sectors, the BIOS moved nine, and answered `AL = 1` — and the
kernel believed `AL` and re-read the rest one sector at a time. On the 5150
that was 148 sectors in 34 `int 13h` calls for a 32-sector file, 4.6x the
traffic, and it made the *batching optimisation measure slower than no
batching*. **The same binary on the same image under QEMU moved 34 sectors in
6 calls** — correct, fast, and completely silent about the bug, because
SeaBIOS is a different BIOS. MartyPC runs the IBM ROM, so it reproduces the
signature: `make DISKAL=1` boots `os8088_5150_herc` in **893 ticks against
188**, with `m.disk()` reporting **870 sectors in 183 reads against 183 in
24**, longest run 9 in both — 4.75x the traffic against the field's 4.6x, and
no test package or `DISKCNT=1` kernel involved.

**The boundary is between the ROM and the CHIP.** What a real 765 puts in ST1,
or whether a real drive ever returns short, is still the emulator author's
belief and still the 5150's question.

Read that as a boundary on the tool, not a complaint about it: everything on
the CPU side agrees with the 5150 to within 0–4% across 45 of 47 `gfxbench`
rows, which is the closest any emulator has come here.

---

## …but the BYTES it writes can be checked, and now are

The section above is about *time*. What the guest actually **wrote** is a
different question, and the answer used to be that nobody could ask it.

MartyPC mounts a floppy by reading the file once into an in-memory
`DiskImage`; every sector the guest writes after that lands there and nowhere
else. Nothing in `martypc_headless` or `marty_core` ever writes one back —
that is the eframe frontend's **Media ▸ Save Floppy As**, a `fluxfox::
ImageWriter` behind a menu item a headless run does not have. So a scripted
session could drive os8088 into saving a document and then had only one way to
find out whether it had worked: ask os8088. **That is not a test.** The writer
and the reader are the same FAT12 code, so the one failure that matters most —
both halves agreeing on the same wrong thing — is precisely the one that
cannot be seen from inside. docs/FIELD-NOTES.md 4 is what that costs when it
happens: a stale listing resolved a display row against a snapshot that had
shifted, the loader read the entry next door, and a perfectly good package was
reported as `Bad package`.

`flush` is the missing menu item, reached from the socket, and
**`tools/os88flush.py`** is what to do with the bytes on this side:

```
python3 tools/os88flush.py 127.0.0.1:9001 disks
python3 tools/os88flush.py 127.0.0.1:9001 save 0 /tmp/after.img
python3 tools/os88flush.py 127.0.0.1:9001 ls 1 APPS      # -R for the whole tree
python3 tools/os88flush.py 127.0.0.1:9001 get 0 SYSTEM.CFG /tmp/cfg.bin
python3 tools/os88flush.py 127.0.0.1:9001 diff 0
python3 tools/os88flush.py 127.0.0.1:9001 verify 0
```

…and, in a scripted session, sharing the one connection the debug server
allows (`Mouse(marty=m)`'s idiom, for `Mouse(marty=m)`'s reason — a second
client does not error, it **hangs**):

```python
with os88marty.launch("build/os8088-360.img", apps="build/apps360.img") as m:
    f = os88flush.Flush(marty=m)
    assert not f.dirty(0)                     # nothing written at boot
    ...drive the UI...
    print(f.diff(0)["added"])                 # ['SYSTEM.CFG']
    cfg = f.volume(0).read("SYSTEM.CFG")      # its exact bytes, on the host
```

The `Volume` class walks the BPB, the FAT and the directories itself, with no
kernel code anywhere near it. That independence is the whole point, and it
buys two things a guest-side check cannot: it sees the **hidden and system**
files SPEC.md §19.6 marks and `disk_mount`'s species filter drops — a listing
off a flushed system disk shows `KERNEL.SYS`, `SOUND.DRV`, `HDD.DRV` and
`ASSOC.DAT`, none of which is visible from inside os8088 at all — and its
`verify` hands the image to `tools/os88disk.py --verify`, the same structural
fsck the build uses, so "os8088 is happy with this volume" and "this volume is
coherent" stop being the same claim.

Four things about it are load-bearing:

- **It pauses the machine, and it has to.** A flush is a read of the image at
  the instant it is asked for, and a save here is a multi-sector commit — data,
  then the FAT, then the directory entry (SPEC.md §18.4). Caught mid-commit,
  the volume that comes out is *genuinely* inconsistent, and it reads
  afterwards as a corrupt disk rather than as a mistimed grab. Every verb
  pauses, flushes and puts the machine back the way it found it, in both
  directions.
- **`writes` is not a dirty flag, and it looks exactly like one.** The count
  in `disks` is fluxfox's `write_ct`. `post_load_process` sets it to **1** at
  mount, so it never reads 0 — and for a raw sector image, which MartyPC loads
  at BitStream resolution, it is never advanced at all: the one call that would
  do it is commented out upstream (below). Measured here: it read 1 through a
  Control Panel close that demonstrably wrote three sectors. `dirty()` compares
  the **content** against the image the drive was mounted from, which is slower,
  always right, and cannot rot.
- **A bare `save(drive)` writes back over the mounted image** — the menu's
  *Save Floppy*, not *Save Floppy As* — which under `launch()` is the session's
  private copy in the run tree. That is usually what you want, and it destroys
  the only pristine copy `diff` and `dirty` have to compare against. Name a
  path when you want to keep the reference.
- **The emulator writes the file, so the path is the emulator's.** Its working
  directory is the run tree, so a relative path lands there rather than beside
  you; every verb here hands the server an absolute path and reads the result
  back itself.

Verified end to end on `os8088_5150_cga_gla`: a fresh boot is `dirty() ==
False`; a Control Panel change plus a close puts `SYSTEM.CFG` in `added`, with
sectors 1, 3, 5 and 268 differing — the two FAT copies, the root directory and
the file's data cluster, which is SPEC.md §18.4's commit order made visible
from outside the machine for the first time — and the file reads back 86 bytes
beginning `O88CFG`. `get 1 APPS/HELLO.O88` off the live disk is byte-identical
to `build/hello.o88`.

---

## Build and run

Needs `cargo` (Rust) and, on Linux, `libudev-dev` + `pkg-config` — MartyPC
depends on `serialport`, whose build script hard-fails without them.

### Installing the deps in a fresh Ubuntu container

**This whole subsection is about ONE environment**: a fresh Ubuntu container,
which is what an agent session gets. **A Mac has never had either problem** —
`tools/setup-macos.sh` installs through Homebrew and neither of the two apt
failures below exists there. (What the Mac script does *not* install is Rust,
so `make marty` on a Mac wants `cargo` put in front of it by hand.)

**`apt-get update` FIRST, before anything else.** The shipped index names
`libudev-dev_255.4-1ubuntu8.14`, which has been superseded and removed from
the pool, so installing it straight off 404s — and since this is the default
test target, that 404 is the first thing a session hits. A refresh is the
whole fix; it resolves to a version that exists (`…8.16` today) and installs.

```sh
apt-get update                       # the shipped index is stale; this is slow
apt-get install -y --no-install-recommends libudev-dev pkg-config
```

**Do not pin a version here.** Skipping the deps entirely does not fail at apt
at all: it fails minutes later inside cargo, on `serialport`.

**`qemu-system-x86` fails the same way and needs the OPPOSITE fix**, which is
why the two are written down together — the shapes look identical and the
cures are inverted. The index lists the `noble-updates` build, whose `.deb`
404s on `archive.ubuntu.com` and then times out against
`security.ubuntu.com`, so a plain install burns several minutes and fails.
Pin all three packages to the **base** noble version:

```sh
V='1:8.2.2+ds-0ubuntu1'              # the BASE version, NOT -updates
apt-get install -y --no-install-recommends \
        "qemu-system-x86=$V" "qemu-system-common=$V" "qemu-system-data=$V"
```

`-t noble` is **not** enough — it still resolves to the `-updates` version.
`--no-install-recommends` skips the gstreamer/libcaca display extras, which
404 the same way and which a headless `-display none` run never touches.

So: **`libudev-dev` wants the NEWER version a refreshed index names, because
the stale entry has been superseded; QEMU wants an OLDER one than the index
names, because the `-updates` build is the broken one.** Applying either
cure to the other package reinstates exactly the 404 you are trying to
escape. `pkg-config` installs normally either way.

If a previous attempt is wedged, clear `/var/lib/dpkg/lock-frontend` and run
`dpkg --configure -a` first — and **do not `pkill -f apt-get` from inside a
Bash tool call**, because the pattern matches the calling shell and kills it.

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

### From a script: `os88marty.launch`

Do not hand-roll the above in Python. Every scripted session needs a fresh
emulator, every one of them wrote the same twenty lines, and essentially all
of this harness's lost time is in those twenty lines — none of whose failures
announce themselves.

```python
import os88marty
from os88mouse import Mouse

with os88marty.launch("build/os8088-360.img",
                      apps="build/apps360.img",
                      machine="os8088_5150_cga") as m:
    mo = Mouse(marty=m)                 # ONE connection, shared
    mo.dblclick(608, 105)
    os88marty.settle(m)                 # ...instead of time.sleep(4)
    m.vram("cga")
```

- **It kills survivors by PID, out of `/proc`, and waits for the port.** A
  leftover emulator keeps 9001, the new one cannot bind and says so only in
  its log, and the client then drives the *stale* machine — a different image
  at a different point in its boot. It reads as a hang, or as a change that
  did nothing.
- **Never `pkill -f martypc_headless` and never `pgrep -f`.** The pattern
  matches the calling shell's own command line, so `pkill` can kill the
  caller and `until ! pgrep -f …` never finishes. The `[m]artypc` bracket
  dodge fixes only the first of those.
- **It owns the process.** `close()` — or leaving the `with` — kills it, on
  the failure paths too, so one session cannot leak a survivor onto the next.
- **It asserts `cycles == 0`** before returning, which is the direct test for
  "am I attached to the machine I just started".
- **Each floppy is copied into the run directory first.** The guest WRITES to
  a mounted image (`SYSTEM.CFG`, saved files), so a run would otherwise dirty
  `build/`.

`settle(m)` is the wait, and it replaces every `time.sleep(4)`: it returns
once two rendered frames a second apart are identical, which an os8088 screen
only is between events. `launch` uses it with a gate for the boot, and the
gate is not optional — the two obvious ones are both wrong:

- **Stillness alone returns during the BIOS POST**, which sits perfectly
  still for seconds before the floppy is touched. Measured: an 8.3-second
  "boot" showing a quarter of the desktop's lit pixels.
- **"Has the card left text mode" hangs on Hercules.** MartyPC's MDA reports
  text mode forever, in graphics mode as in any other.

So the gate is the **desktop**, sampled through `vram` on the 1bpp cards and
`fbuf` on VGA — because `vram` is impossible on VGA and is the *exact* answer
on the other two, where `fbuf` comes back cropped to a display aperture. **On a
Hercules that crop is `(−16, +2)`: guest (x, y) renders at `fbuf` (x−16,
y+2)**, over a 720x350 window on a 720x348 framebuffer. Measured, not assumed,
and measured twice independently — `fbuf` scanned against `vram` over one
desktop agrees on **2,280 of 2,280** sampled pixels at that offset and at no
other, and a second correlation over a different desktop put the mismatch at
**0.0000 at dx = −16, dy = +2** across 4,992 samples. The horizontal half of it
had been written down here for a while; the vertical half had not, and a pixel
gate that compares `fbuf` against anything else needs both.

**It is THREE facts, and it was one.** The gate used to be the menu bar's
white field alone, which is *nearly* enough — `wm_paint_all` draws the
dither, the drive zones and the dock **before** the bar, so the bar going up
means the rest is already there — but "nearly" was carrying the whole
argument, and everything after a boot gate is measuring a machine it believed
was ready. So the bar is tested the way SPEC.md §12 defines it, white field
**and** the 1px black rule under it, and the **dock strip** as well: the first
thing on the screen and the last. Measured from reset at 8 Hz, field / rule /
dock —

| | field | rule | dock |
|---|---|---|---|
| POST text | 0.26 | **0.25** | 0.25 |
| splash | 0.00 | 0.00 | 0.00 |
| CGA desktop | 0.93 | 0.00 | 0.96 |
| Hercules desktop | 0.94 | 0.00 | 0.96 |
| VGA desktop | 1.00 | 0.00 | 0.96 |

— and the rule is what rejects POST text, which is the one screen here whose
top band is genuinely lit.

**And the gate and the stillness test read the screen ONCE, together.** They
used to read it independently, one round trip apart — and this emulator runs
the guest **several times faster than real time** (measured: a CGA boot is
25.8M cycles, 5.4 guest seconds, in 1.25 s of host), so a round trip is tens
of milliseconds of *guest* time, which is most of a desktop paint on a 4.77MHz
machine. Two reads can therefore report a state that never existed: a probe
built that way reported the menu bar up while the same screen was 26% lit, on
a machine whose desktop is 56% and whose bar goes up last. That is a lie about
the guest produced entirely by the instrument, and it is the same family as
the offset crop above — **when the host is fast, two questions asked
separately are two questions asked about different machines.**

Measured boots: **CGA 4.6 s, Hercules 4.7 s, VGA 7.1 s** on this container,
against 17.5 / 16.1 / 7.1 on the one those figures were first taken on and a
26-second fixed sleep before that. The spread between the two containers is
the point: a number that is a property of the HOST is exactly what `settle`
exists so that nothing has to hard-code.

**`card=` is not optional on a two-card machine.** `settle`, `launch` and
`_Screen` ask `video` with no card by default, which answers MartyPC's
**primary** — and os8088 need not be driving it: a `make VIDEO=herc` kernel on
`os8088_5150_both_gla` draws on the Hercules while the config's first
`[[machine.video]]` is the CGA. The boot gate then watches a card nothing is
drawing on and times out after 120 seconds saying *"this machine never
finished booting"*, about a machine that booted fine. Pass
`launch(..., card=1)`; a caller running `settle` itself passes
`gate=desktop_up, card=<idx>`, the card belonging to `settle` rather than to
the predicate. `advance(frames=…)` takes it too, and there it decides which
card's 50Hz or 60Hz is being counted.

This paragraph used to say `fbuf` was **dead** on Hercules — that the MDA does
not rasterise graphics mode at all — and that is not true at the pinned build:
a real os8088 Hercules desktop measures **136,617 lit of 252,000 (54.2%)**
through `fbuf` against **55.7%** through `vram`, the two agreeing to within the
aperture crop. See the correction under `flicker` below.

### `until(m, cond, what)` — the wait for work that draws nothing

`settle` watches pixels, so it is **silently wrong for anything that holds
the gfx lock for its whole run**. A hard-disk install freezes the UI while it
copies: the screen is *more* still while it is busy than when it is done, so
`settle` sees stillness about five seconds in and returns — and a
`with launch(...)` block then kills the emulator mid-copy. Nothing about that
looks like a wait ending early. It looks like the install stopping halfway,
which is a bug in the installer, and it is where this call came from.

Ask about the thing instead. `cond` is called with the Marty each round and
may look wherever the answer actually is — guest memory, or the **host** side
of a mounted image, which is usually the better one because a commit tends to
be a single write you can watch for:

```python
STUB = bytes.fromhex("fa31c08ed88ec08ed0bc007c")   # hd_bootstub's opening
os88marty.until(m, lambda _: open(vhd, "rb").read(12) == STUB,
                "the installer to commit the MBR")
```

SPEC.md §52.10 writes the partition table **last**, as the commit, so that
one comparison is an exact "the install finished" and needs no offsets and no
`DISKCNT=1` kernel.

It separates the two ways the wait fails, because they want different fixes.
A guest that has **stopped executing** can never satisfy any condition, so it
says which state and where — `the guest is 'breakpoint' at 0060:3C21 and is
not executing` — rather than blaming the condition. That is the shape a
still-armed breakpoint takes, and it is exactly what cost a session an
afternoon above. Everything else is an honest timeout that says the guest is
still running, so the limit is too short or the condition is asking about the
wrong thing.

**Pick by whether the screen is the evidence**: `settle` for a boot, a click
or a repaint; `until` for a format, a copy, an install, a save — anything
whose progress is on a disk rather than on the glass.

**And widening `settle`'s window to cope is the trap on the other side, which
now raises.** Reaching for `settle(m, quiet=30, limit=2400)` on an install
looks like patience and is unsatisfiable: `stable` identical samples `quiet`
apart is `stable * quiet` **host** seconds of unchanged screen, the menu
bar's clock changes once a **guest** minute, and the guest runs *faster* than
real time — so at 3.3x a 60-host-second window spans three ticks and no two
samples can ever agree. It waits out the whole limit and then blames the
guest. Measured: an install driven that way sat for **40 minutes** with the
install long finished, and was reported as os8088 being slow. `settle` now
samples the cycle counter first and refuses a window it can prove cannot
close, naming `until` in the message.

**The install itself is 64 guest seconds** (counters polled rather than
pixels watched: floppy traffic goes quiet 63.6 s after the confirm click,
1,250 sectors in 162 reads, 100 seeks over 583 cylinders, ending on *Done —
remove the floppy, Restart* with C: mounted). That is ~19 s of host time
here, and it matches the field machine's "under two minutes". **So the floppy
timing patch is NOT implicated in that 40 minutes** — worth stating because
it is the natural suspect, being the most recent thing to make the disk
slower on purpose. Its modelled cost for that run is 28.1 s of transfer and
4.7 s of seek: 22.4 ms per sector against the field's measured ~24 ms
(Set 24's 21,307 B/s), and 8.0 ms per cylinder stepped. Both are right, and
neither adds up to 40 minutes of anything.

### Naming a kernel flag: `os88sym`

`m.sym("fpg_on")` is the address of a kernel symbol, and `python3
tools/os88sym.py --all` lists every one. **Do not take an address out of
`nasm -l`'s listing** — for anything in `.bss` both the address column and
the bracketed operand bytes are section-relative and fixed up afterwards, so
`menu_bovr` reads as `0x0879` there and is at `0xCBA4` in the binary. That is
a plausible small number pointing into `.text`: reading a byte from it
succeeds, returns something, and means nothing. Two sessions have lost time
to it, one of them concluding a feature was broken from a flag that was never
the flag.

`os88sym` uses `nasm`'s `[map]` directive on a *temporary copy* of
`kernel/kernel.asm` — `kernsize.py`'s idiom — and asserts the result is
byte-identical to `build/kernel.bin`, so a map describing a different kernel
is an error rather than a subtly wrong answer. A knob build moves everything:
pass the same `-D`s (`--define DISKCNT=1`).

---

## The machines

### Which of them a DISK number may come off

Read this before quoting a floppy figure from any of them, because the answer
is not "all of them" and the difference is 1.61x rather than a rounding error.

**The drive is the same everywhere and that is measured** (PERFORMANCE.md Part
9 Set 38). One `combo.img` booted on all eighteen machines, with `m.disk()`
read from outside the guest, puts **six of them bit-identical** — 24 reads,
186 sectors, longest run 9, 29 seeks, 54 cylinders, 432.0 ms of seek — across
CGA, Hercules, a two-card machine, a Sound Blaster with no OPL, a 720KB drive
as B: and a four-drive machine. There is no per-machine drive constant and
nothing to tune: the Tandon TM100-2 does not care what is in the next slot.

**The BIOS is not the same, and it is what the number is made of.**

| class | machines | a disk TIMING here is… |
|---|---|---|
| **IBM ROM, 5150** | `_cga`, `_herc`, `_both`, `_sb`, `_sbonly`, `_sb_128k`, `_sb_256k`, `_cga_720b`, `_cga_4fdd` | **field-comparable.** `sysbench`'s raw block lands 9–10 of 11 rows within one measurement quantum of docs/FIELD-MACHINES.md's 5150, 6–8 of them exactly |
| **GLaBIOS** | `_cga_gla`, `_herc_gla`, `_both_gla`, `_both_gla_mono`, `_cga_1fd`, `_xt_vga`, `_xt_vga_sb`, `_xt_hdd`, `_xt_hdd_sb` | **counts yes, seconds no.** A track read is **1.61x** lighter, and nine one-sector reads cost *the same as one* track read where the IBM ROM pays ten revolutions |

That last one is not an emulator artifact. On 1:1 media sector *n+1* follows
*n* immediately, so nine separate reads fit one revolution **if the BIOS turns
a call around inside one sector time (22 ms)**. GLaBIOS does; the 1982 IBM ROM
cannot, its head-settle loop alone being 52.5 ms (Set 37).

**No single machine is "the calibration", including `os8088_5150_herc`.**
Which rows land exactly shuffles between the three IBM machines measured —
`_cga` nails both track rows and misses `seek 5 cyl`, `_herc` the reverse —
because a row sitting on a 13,731 µs quantum boundary falls whichever side the
guest's turnaround puts it. Quote the **class**, not the machine.

### The list

`tools/martypc/configs/os8088_machines.toml` is appended to MartyPC's own
`ibm5150.toml` by `build.sh`:

| config | what it is |
|---|---|
| `os8088_5150_cga` | the default: IBM 5150, 8088 at 4.77MHz, 640K, CGA, real 1982 IBM BIOS |
| `os8088_5150_herc` | the same with a Hercules — MartyPC models it as an MDA **subtype**, so the block needs `subtype = "Hercules"` as well as `type = "MDA"`, and SPEC.md §39.1's probe is what decides |
| `os8088_5150_cga_gla` | the same with GLaBIOS |
| `os8088_5150_sb` | the same with an AdLib **and** a Sound Blaster (DSP 2.01, 0x220, IRQ 7) |
| `os8088_5150_sb_gla` | `_sb` with **GLaBIOS**, which is the sound machine a CONTAINER can actually run: `_sb` above wants `ibm5150_82_v4` and that ROM is IBM's and is not in this tree, so until this stanza existed a fresh checkout had **no machine with a Sound Blaster in it at all** and every mixer measurement had to be taken somewhere else. Every other class already had its `_gla` twin. The one thing it is not good for is a DISK number — PERFORMANCE.md Set 38 puts GLaBIOS's `int 13h` at 1.61x lighter than the period ROM's — which a CPU-bound measurement taken with the drive stopped does not touch |
| `os8088_5150_sbonly` | ...and with the FM half taken out: a DSP at 0x220 and **nothing at 0x388**. No real card is built that way, which is why it needs an emulator — it is SPEC.md §51.3.1's jumpered-off-FM case, and `_sb`/`_sbonly` are one pair with `make SNDSNIFF=sb` between them |
| `os8088_5150_sb_128k` | `_sb` at the **RAM floor** — 128KB, `MIN_RAM_KB`. It tests the CLAIM HEAP under a sound card rather than the card: `HEAP_SEG` is at 91.5KB, so this machine has **36.5KB of heap** for the driver's image, its DMA buffer, its staging pool and the app, where `_sb` has 548.5KB and can never show a refusal. SPEC.md §34.6's two claims are sized against this machine and neither can be judged on the other one. Boot it with `launch(..., boot=2)` and wait on the desktop's own lit count — `settle`'s menu-bar gate fires on the splash here, because a slow machine holds a still frame for longer than the gate's patience |
| `os8088_xt_vga` | an IBM 5160 XT with GLaBIOS and a VGA — SPEC.md §39's mode 12h |
| `os8088_xt_vga_sb` | ...and the same XT with the AdLib + Sound Blaster pair, which exists **to be run with `--turbo`** — see below |
| `os8088_xt_hdd` | the same XT with an **XT-IDE** controller — SPEC.md §52's rung 0 — **and a parallel port**, which makes it the one machine here where TWO drivers publish a Control Panel page at once (SPEC.md §31.9/§62.7). That is what exercises `drv_cp_class`'s ordinal-to-class walk rather than asserting it: Hard Drive lands on row 5 and Network on row 6 with class 3 unpublished in between, and on a machine with one page an off-by-one there is invisible |
| `os8088_5150_cga_hdd` | the same XT-IDE disk on a **5150 behind the PERIOD ROM**, which every other hard-disk machine here lacks — the rest are `Ibm5160` on GLaBIOS, so until this existed "a hard disk" and "the ROM the reporter runs" were two axes that could not be crossed. That matters because this tree already has one defect (SPEC.md §18.91's `AL`) that *only* the IBM ROM exposes, and because a field report arrives in this shape: docs/FIELD-MACHINES.md's machine is a 5150 with an ST-225 on an ST11M, where a controller's own option ROM is rung 0 exactly as XT-IDE is here. Needs the ROM this tree cannot ship, so a container without one runs the GLaBIOS twins only |
| `os8088_5150_cga_lpt` | the CGA GLaBIOS 5150 with a **Centronics card at 0x378** — SPEC.md §62's machine, and the only other one here with a parallel port. MartyPC's `ParallelPort` has a readable data register, so `lp_latch` succeeds and the whole os8088 side of the link is testable: the scan, the attach, the publication, the page, and `net_connect` failing in bounded time. **IT CAN BE GIVEN A PARTNER, and this row said for two milestones that it could not** — the claim was that the status lines read a constant, and they do until something writes them: stock `lpt_port.rs` implements `status_register_write` and stores the byte the guest reads back, so the debug server's `outb` drives exactly the lines `lp_snib`/`lp_rnib` poll. `tests/lptlink/partner.py` is that far end in both roles (SPEC.md §62.10.3), and on this machine it has driven a handshake, a mount, a listing and a whole recursive **folder copy** (§62.10.6). No patch was needed; the capability was there the whole time and the row was the reason nobody looked. What is still out of reach here is the WIRE's verdict — timings, levels, a real cable — which is the 5150's question, and `tests/lptlink` is how it is asked.

**Two things to know before driving it.** MartyPC headless free-runs at about **4x real time** (measured: 74.0 guest ticks a wall second against 18.2), and `net_connect` leaves the reply deadline at `REPLY_TMO` = ten GUEST seconds — so a mouse `settle` of 6 s is 24 guest seconds of nobody answering, `net_lost` fires, and every verb after it is refused by the `NS_LINKED` gate with **no wire traffic at all**. That reads as a UI that does nothing, nowhere near its cause. Click with a short settle and answer immediately. And `Partner.serve` **steps** the guest, so it leaves it paused: call `m.run()` afterwards or `os88mouse` reports the next target as one it cannot reach. GLaBIOS on purpose: nothing here is a timing question, so this is one of the machines that runs in a container with no IBM ROM |
| `os8088_5150_both` | **two cards**: a CGA *and* a Hercules, which is docs/FIELD-MACHINES.md's machine as it actually is. SPEC.md §39.11's adapter switching exists for this, and docs/DUAL-DISPLAY-PLAN.md is the study of driving both at once |
| `os8088_5150_both_gla` | its GLaBIOS twin, and the one `tests/dualcheck.py` runs by default — the IBM ROM this tree cannot ship is what the other needs |
| `os8088_5150_herc_gla` | a single-card Hercules on GLaBIOS: `os8088_5150_herc` without the ROM, and the control for "does this card rasterise at all" with no second card to confuse the question |
| `os8088_5150_herc_gla_144` | ...and the same machine with **1.44MB drives**, for `make combo144`. It exists because every other machine here takes `pcxt_2_360k_floppies`, so a 1.44MB image could be BUILT here and never BOOTED here. **It is an anachronism on purpose**: a 1.44MB disk wants 500 kbps and the IBM XT's 8-bit controller runs 250, so no stock XT reads one — it took a third-party 8-bit HD controller and a driver, and the 5150's own ROM tops out at 360KB whatever is plugged in. MartyPC models the DRIVE (`FloppyDriveType::Floppy144M`, `type = "1.44m"`) and has the one `IbmNec` FDC, so what this proves is that **our** boot sector and **our** FAT12 code handle 18 spt on an 8088 — not that the media is period. Measured: it boots to a desktop **byte-identical** to `combo.img` on `_herc_gla` (139,438 lit pixels on both) in **128.7M cycles against 151.7M**, because 18 sectors a track is fewer `int 13h` calls for the same kernel; a Disk window opens on the volume, which is the mount, the FAT snapshot, the root listing and the icon harvest's per-package first-sector read. Take no PERFORMANCE.md number off it, and put no 1.44MB disk in the calibration 5150 (docs/FIELD-MACHINES.md). Its `fdc` is declared INLINE rather than as an overlay, so no file of upstream's needs patching — `build.sh` copies `install/` wholesale and only ever APPENDS our machine file |
| `os8088_5150_cga_720b` | the default with an **80-cylinder drive as B** — SPEC.md §18.96.2's machine. The 1982 ROM answers no `AH=08h` for a floppy, so `dskw_fmt_probe` reads a 360KB machine and offers 360K for a disk that could hold 720K; this is the only machine here where §22.12's **Space** key has something to toggle to. It drops the `pcxt_2_360k_floppies` overlay and declares `[machine.fdc]` itself, because the drives are the point — upstream's own `pcxt_4_360k_floppies` is the other one worth knowing about, and the FDC takes **four**. Put a non-FAT image of the size under test in B: the reach test's verdict is whether LBA 1439 can be written and read back, so a 1440-sector image passes and a 720-sector one takes the 360K fallback |

**Two cards is a real configuration and it took two patches to make honest.**
The `[[machine.video]]` blocks are an array and the bus builder installs every
one of them, keyed by `VideoCardId { idx, vtype }` in config order — but
upstream maps a Hercules-subtype MDA at B0000 **and** B8000 unconditionally
(the mapping is built in the constructor, before any guest has written 3BFh), a
CGA maps B8000, and `Bus::register_map` resolves the overlap by **last writer
wins**: it stamps `mmio_map_fast` and never reads the `priority` field the
descriptor carries. So one card silently vanished into the other, and which one
depended only on the block order. `patches/02-hercules-page1-decode.patch`
narrows the Hercules to page 0 — what a real card with 3BFh bit 1 clear
decodes, and what `vid_setmode` deliberately leaves it at (SPEC.md §39.6).

**The obvious test does not catch that**, which is why there is a gate: write
to B0000, write to B8000, read both back, and they differ — because they are
32KB apart inside one card's 64KB. `tests/dualcheck.py` asks the *rasters*
instead (a write to one card's memory must change **that** card's rendered
output and not the other's), parks the CPU so no guest contributes, and is
verified to **fail** — revert patch 02 and it exits 1 naming the check.

**And a third patch was needed to make a two-card machine boot the card the
field one does.** SPEC.md §39.1's `vid_detect` chooses its adapter from
`int 11h` bits 5:4 — SW1-5/6 on a 5150 — and upstream *derives* those bits
from the card list: `have_expansion` first, then "any CGA present → CGA
hires", then MDA. So every two-card machine reported colour whatever order the
blocks were in, os8088 booted on the CGA, and the calibration machine's
arrangement — both cards, switches set to mono, boots Hercules — could not be
expressed at all. `patches/03-video-dip-config.patch` adds an optional
`video_dip` to the machine config (`"mda"` / `"cga_lores"` / `"cga_hires"` /
`"expansion"`); absent, the derivation runs exactly as before, so no existing
machine moved. **`os8088_5150_both_gla_mono` is the result**, and it is the
only machine here that reaches SPEC.md §39.11.1's `vid_cga_alias` — the
routine that finds a CGA hiding behind a Hercules-primary machine, whose bug
survived precisely because "the direction the old emulator could reproduce was
CGA-primary, where the routine never runs". Verified: `avail = 0x06`, both
cards seen, os8088 running Hercules, desktop rendered at 720×348.

**Its `[[machine.video]]` blocks list the MDA FIRST, and that is about the
harness rather than the guest.** MartyPC's own primary card is the first in
that list, and `fbuf` — which `os88marty.launch`'s boot gate reads — reports
the primary. With the CGA first the gate watches an unprogrammed card, never
sees a desktop and times out on a machine that booted perfectly (measured:
`fbuf` returns *3 bytes*). The DIP decides which card the **guest** picks; the
order decides which one the **tooling** looks at, and on a two-card machine
they have to agree. `launch(..., card="herc")` does not rescue it, so put the
card os8088 will drive first.

The first five are shaped after docs/FIELD-MACHINES.md's calibration machine,
as closely as MartyPC allows.

**Which ROM you are running is decided by the machine's family, and the split
is systematic: every `os8088_5150_*` runs the REAL IBM ROM** (the 27 Oct 1982
`1501476`, in the repo), **and every `os8088_xt_*` — an IBM 5160 — runs
GLaBIOS.** `os8088_5150_cga_gla` is the one deliberate exception, and it is
there so the ROM itself can be A/B'd against `os8088_5150_cga` with nothing
else changed.

Worth stating rather than leaving to the table, because it decides what a
result *means*: a BIOS-dependent measurement taken on a 5150 machine here is
about the same ROM the field machine runs, and one taken on an XT is not.
A session assumed the opposite and attributed a null result to GLaBIOS that
had been measured on the IBM ROM (SPEC.md §39.6).

### The fastest machine here is 7.16MHz, and it is a CONTROL

`--turbo` takes the XT clock from 4.77MHz to 7.16, and that is the whole of
the available range: MartyPC is an 8088/8086 emulator by design, so every
machine type it offers is a 5150, a 5160, a PCjr, a Tandy or a Compaq
Portable. 1.5x is not a modern CPU and it does not have to be — it answers
whether a cost is **CPU-bound**, which shows as a proportional change and not
as an absolute one.

```
./martypc_headless --machine-config-name os8088_xt_vga_sb --turbo \
                   --mount fd:0:media/floppies/os8088-360.img
```

Two things about it. **The CGA panics under turbo** — its video clock is
derived from the CPU clock and `devices/cga/videocard.rs:399` asserts on the
result (`ticks_advanced: 27 > clocks: 20`), which is why the turbo machine is
a VGA one; the MDA is untested for the same reason, and nothing in a timing
question about a *worker* cares which adapter drew. And **no PERFORMANCE.md
number may be taken off it**: 7.16MHz is not a machine anybody in
docs/FIELD-MACHINES.md owns, and GLaBIOS is not a period ROM.

Its first use is the worked example of what a control is for. SPEC.md
§45.16.4's mode-C burst (since removed — §45.16.3; the reasoning below is
what a control machine is FOR and does not depend on the mode still
existing) was designed to hold a row for one system tick and
measured ~700 ms for eight rows against a designed 385 — and the question
"is that the design or is that the 5150 being slow" is exactly the question a
faster machine answers. It was the latter: on the 5150 **99%** of the
stretched burst steps have the worker mixing inside them against **2%** of
the healthy ones, and at 7.16MHz the mean burst step is **5.3% of a cycle
against one tick = 5.49%** — i.e. one tick, as designed — with the burst
falling from ~69% of the cycle to 48% (the ideal, if no wake is ever missed,
is 38.5%).

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

**Every capture takes an optional `card=` and every capture reports which card
answered.** Absent means the primary, so nothing that already worked changes;
otherwise it is an index (`VideoCardId.idx`, the `[[machine.video]]` position —
**not** iteration order, which is a `MartyHashMap`'s business) or a type name,
and an ambiguous type name is refused rather than guessed. It applies to
`video`, `screen`, `fbuf`, `flicker`, `pace` and `advance` — the last because
`frames=` is a question about one card and the two disagree, 50Hz Hercules
against 60Hz CGA, so pacing a capture on the wrong counter reads as jitter that
is not there. Before this, all six went through `primary_videocard()`, which is
`videocard_ids[0]`: on a two-card machine they silently answered about card 0
while the caller believed otherwise.

**`park` exists because `setreg ip` cannot be made to work.** `Register16::PC`
is settable and setting it is not enough — `pc` is the FETCH pointer and the
core derives `ip() = pc - queue.len()`, so a bare write leaves whatever the 8088
had already prefetched from the old address in front of the new one, and those
bytes execute first (measured: parking at 0x0500 landed at 0xD4CC). The flush is
not reachable through `CpuDispatch`, so `park` goes through the CPU's reset
vector, and **clears every register** as a documented consequence. Devices are
untouched: it resets the processor, not the machine.

**So DO NOT park the CPU to call a kernel routine when a flag the kernel
already polls will do it.** Forcing a `wm_paint_all` — which every "does the
incremental drawing agree with a full repaint" test needs — was done for a
while by building a stub in `menu_bcell`, parking on it, and handing it the
banked SS:SP. It works, and it is a trap with a long fuse: the frame it is
handed belongs to whichever task the pause happened to catch, so if that was
the UI task **inside a lock hold**, the stub's own `gfx_lock` spins on
`sti / task_yield`, the scheduler switches away, and the CS:IP restored at the
end names a task whose stack has moved on. Measured, that surfaced THREE
assertions later as a window rect reading 2056x2056 and a keyboard that had
stopped arriving — nowhere near the call that caused it.
`tests/dispcalc.py`'s `full_repaint` is the shape to copy instead:

```python
m.cmd(cmd="run")
m.write(S("cp_dirty"), b"\x01")     # ui_task step 3: gfx_lock / wm_paint_all
while m.read(S("cp_dirty"), 1)[0]:  # / gfx_unlock, on its own stack
    time.sleep(0.05)
os88marty.settle(m); m.cmd(cmd="pause")
```

Nothing is parked, nothing is reset, no kernel scratch is borrowed, and the
repaint happens at a moment the kernel chose. The general rule: **prefer
poking a byte the guest already polls over executing kernel code from
outside** — `[cp_dirty]`, `[desk_zdirty]`, `[menu_bdirty]`, `[cal_dirty]` and
their kind are all deferred-work posts, which is exactly what a harness wants.
The one cost is that the repaint now takes real wall time, so a run can
straddle the menu bar's once-a-minute clock change: exclude `y < MBAR_H,
x >= [vid_clk_hx]` rather than chasing 42 pixels at the top right.
| `flicker` | one sample per DISPLAYED FRAME, and the flash/redraw counts |
| `pace` | per-frame changed counts over a long run — frame pacing / smoothness. `ignore` excludes a rect (a blinking cursor); `video` reports the card's cursor state |
| `advance` | run a bounded amount of GUEST time — `frames=` or `cycles=` |
| `cards` | **every** video card, in config order: `idx`, `type`, `primary`, `mode`, `field_w/h`, `frames`. The answer to "did my two-card config actually produce two cards", which `video` cannot give — on a machine whose second entry was dropped it looks exactly like a one-card machine |
| `park` | point the CPU at `cs:ip` with the prefetch queue flushed, so a harness can take the guest **out** of a measurement |
| `snapshot` / `restore` | fork a holder process; wake it on a port, any number of times |
| `key` | a keypress by MartyKey name — `KeyA`, `Enter`, `ArrowRight` |
| `mouse` | one Microsoft packet: relative `dx`/`dy` and button state. **To click a CONTROL use `tools/os88mouse.py` instead** — it reads the live cursor from the debug registry (SPEC.md §9.4.3) and converges on an absolute target, where aiming this one by dead reckoning drifts and misses silently |
| `history` / `callstack` | the CPU's own instruction history |
| `disks` | what is in each floppy drive, and where it was mounted from |
| `flush` | write a drive's live image back out to a host file — the guest's writes, which live nowhere else. `tools/os88flush.py` is the client |
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
- **A HIT IS NOT `"paused"` — it is `"breakpoint"`.** `pause()` and a
  breakpoint are different states, and a wait written as
  `while status()["state"] != "paused"` is false forever at a breakpoint that
  is firing perfectly: the tool reports success, the machine stops, and the
  script sees nothing and times out. Test `!= "running"` (what `until()` does,
  and what `Marty.stopped()` is), or `== "breakpoint"` (what
  `tests/dispfreeze.py` does). **This cost a whole session here** — five
  separate investigations concluded "breakpoints do not fire in this build",
  including one against the live `int 08h` vector, and every one of them was
  the poll and not the server.
- **`sym()` is FLAT; `execseg`'s `off` is an OFFSET.** `sym("wm_show")`
  answers `KERNEL_SEG*16 + offset`, so it pairs with `{"type": "exec",
  "addr": ...}` — which is what every user in the tree does
  (`tools/os88span.py`, `tools/winmove.py`, `tests/dispreboot.py`). Put it in
  an `execseg`'s `off` instead and the breakpoint is armed 0x600 further on,
  in real code, at an address that is simply never reached — no error, no hit.
  `Marty.bp_exec("wm_show", ...)` takes symbols or flat addresses and cannot
  get this wrong.
- **`run` resumes from a breakpoint, and it took a fix in this server to do
  it.** MartyPC clears the CPU's latched breakpoint flag in exactly one place,
  `machine.run()`'s `BreakpointHit → Run` arm. `run` used to set the state to
  `Running` itself, which enters through the `Running` arm instead and skips
  that clear — so the next step re-reported the same breakpoint and the machine
  advanced **zero cycles, forever, on the first breakpoint of the session**.
  Both `run` and `advance` hand the transition to `machine.run()` now, the way
  `reset` always did; measured, a resumed `int 08h` breakpoint advances ~260k
  cycles between ticks where it advanced 0 before. **`advance` had the same
  defect and its own comment said otherwise**: it routed through `Paused`
  first, and `Paused → Run` does not clear the flag either — only the
  `BreakpointHit` arms do.

  This is worth keeping in mind even fixed, because of how it FAILED. Every
  symptom pointed at the guest: `status` answered, `regs` answered, `read`
  answered, and the guest merely stopped executing — so scripted input went
  nowhere and read as *the guest ignoring the mouse*. `bp` answered `count: 0`
  while `status` still said `"breakpoint"` at an address nothing was armed on.
  A session lost an afternoon to it and wrote the workaround down here instead
  of fixing it; if a resume ever looks stuck again, check `cycles` across two
  `status` calls before believing anything about the guest.
- **A `state` that is not `"running"` is not necessarily `"paused"`.** A
  breakpoint reports **`"breakpoint"`**, and a poll written as
  `if state != "paused": keep waiting` therefore spins straight through every
  hit it was written to catch — the trace looks clean and reports that nothing
  fired. Test for `state != "running"`, or for the pair.
- **The `int` breakpoint type catches `INT n` as well as hardware
  interrupts.** `sw_interrupt` ends in the same `intr_routine` the INTR
  microcode uses, and that is where the vector's flag is tested — so `int` on
  13h stops on the guest's own disk calls, not just on IRQs.
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

**THE RENDERED FRAME IS NOT IN THE GUEST'S COORDINATE SYSTEM, and nothing
says so.** A whole-screen capture does not care; **a CROP does**, and this is
the trap that costs a session. Measured by correlating `fbuf` against the
guest's own framebuffer on a Hercules desktop, the card's frame is **720x350
for a 720x348 screen and sits at dx = −16, dy = +2** — a perfect match at
that alignment and at no other. VGA mode 12h happens to come back 640x480 at
(0, 0), and CGA at (0, 0) too, so two adapters out of three encourage the
assumption the third breaks. There is no correction to apply blind: the
offset is the card's raster phase, not a constant of the tool.

What it looks like when it bites: a crop taken at a window's guest rect is
sampling 16 columns to the *right* of that window, so the middle of the
window still compares perfectly and only the edges disagree — 1,670 differing
pixels of a Minesweeper window, all of them in the rightmost 14 columns, and
every one of them showing the window *behind* it. That reads as a smeared
restore, which is exactly the defect `tools/sucheck.py` exists to detect, and
it survived a forced-full-repaint control (which agreed with the "broken"
capture to 0 pixels, because both were mis-cropped identically).

So: **crop with `vram` on the 1bpp adapters** — it decodes SPEC.md §39.3's
banked layout out of guest memory and is in guest coordinates by
construction — and on VGA, where there is no flat framebuffer to read, at
least **assert `fbuf`'s dimensions against `[vid_w]`/`[vid_h]`** before
believing a crop. `tools/sucheck.py`'s `fb()` is the worked example of both.

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

**IT DOES WORK ON HERCULES, and this paragraph used to say it did not.**
Measured at the pinned build on `os8088_5150_herc_gla`: a 12-frame capture of
an idle Hercules desktop returns `settled` with 0 changed and 0 transient — the
same "the instrument is not manufacturing defects" baseline the CGA gives — at
720x350, with frames arriving every ~93,500 cycles (19.6 ms, ~51 Hz, which is
the Hercules field rate). `frame_count()` advances: 30 CGA frames against 25
Hercules frames over the same interval, on the two-card machine.

The old claim was that the MDA does not rasterise graphics mode, so the
rendered buffer is 0 lit of 252,000 and `frame_count()` never advances. Both
halves of that are what a card **nobody has programmed** looks like, and that
is not the same thing as a card that cannot rasterise — an MDA sitting in a
machine whose guest is driving the *other* card reads exactly so, forever.
Whatever produced the original observation, it does not reproduce: `fbuf` on a
live Hercules desktop is 54.2% lit and tracks `vram` to within the aperture
crop.

**Two honest limits remain.** The correction was measured on the GLaBIOS
machine, because the IBM ROM this tree cannot ship is needed for
`os8088_5150_herc`; and `mode`/`text` is still dead on the MDA (it answers
`Mode0TextBw40` in Hercules graphics), so `field_w`/`field_h` remains the
discriminator there, exactly as it is on VGA.

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
  when somebody looked. **The rate itself is ~4.8x** — measured 4.87 and 4.81
  over two 10-second samples of the cycle counter against 4.772727 MHz, the
  check `settle`'s docstring spells out — and it belongs to the HOST, so
  re-measure rather than quoting it. For a harness it means every
  `settle(limit=)` must be sized as though guest seconds arrive FASTER than
  wall ones, because they do.
  **It is a cycle count and NOT a boot time.** Dividing it by 4.772728 MHz
  gives 63.02 s, and that figure was worth nothing when it was taken: a boot
  is mostly POST and floppy, and this tool was then 30x fast on the floppy.
  Since Set 37 the floppy half is within a quantum of the iron and the POST
  half still is not measured against anything. The real machine's boot is
  PERFORMANCE.md's **9,886 ms** (see the table above: 38,886 was the figure
  before Part 9 Set 18's `AL` fix) and the only way to move that number is to
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
  `0060:37F5`, os8088's own tick hook — and **resumes**, ~260k cycles of guest
  time between consecutive ticks, against 0 before the `run` fix above.
- `step 50`: 50 instructions, 719 cycles.
- `screen`: GLaBIOS's POST panel read back in full, including its
  `RAM [ 256 KB OK ]`, `Video [ CGA ]` and `COM [ 03F8 02F8 ]` lines.

---

## Upstream findings

All are in `tools/martypc/patches/01-headless-debug-server.patch` and all are
worth offering upstream:

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
- **A bitstream track's write counter never advances** (fluxfox, branch
  `marty_consumer_0.34`). `DiskImage::write_sector` — the one the FDC actually
  calls — delegates straight to the track and increments nothing, and
  `BitStreamTrack::add_write` exists but its only call site is inside a
  commented-out block; `MetaSectorTrack` does increment. Since
  `post_load_process` sets the count to **1** at mount, `write_ct` on a raw
  sector image reads 1 for the life of the machine however much the guest
  writes, which the eframe floppy viewer uses to decide whether to redraw its
  visualisation. It is the same shape as the dead breakpoint types: a signal
  that is present, plausible and silently constant, so **the absence of a
  change looks like evidence** that nothing happened. This works around it by
  not believing it — `tools/os88flush.py` compares content — and the fix
  upstream is one uncommented line.
- **`attach_image` takes a path and throws it away.** The parameter is
  literally `_path`, so once an image is in a drive nothing on the machine
  knows where its bytes came from. The eframe frontend does not notice because
  its file manager holds the path alongside the drive; a headless run mounts
  from argv and has nowhere to put one. `mount_floppy` keeps its own two-entry
  registry so that `flush` can write back over the file the drive was mounted
  from, which is what makes *Save Floppy* (as opposed to *Save Floppy As*) a
  thing a harness can ask for at all.

The server itself is the answer to the crate's own standing TODO — *"We don't
have any backend to run an event loop. If we want to actually run the emulator
now we need some way of controlling / stopping it."* A socket is both.
