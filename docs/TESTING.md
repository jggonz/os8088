# What can actually be tested, and where

**DEVELOP ON MARTYPC. QEMU IS A FALLBACK WITH A SHORT LIST.** If what you
are testing runs on an 8088 — which is the whole of this OS bar the 286/386
targets — `make marty` gives you a cycle-accurate 4.77MHz 8088 running a real
period BIOS, with a debugger attached: memory, registers, I/O ports,
breakpoints, single-step and cycle counts, none of it costing the guest a
cycle (docs/MARTYPC-DEBUG.md). It covers **all three** of SPEC.md §39's
adapters, scripted input, screenshots and sound. **And for anything with a
disk in its timing, go to the 5150 — no emulator here is disk-accurate,
MartyPC included.**

**Here is the whole of QEMU's remaining list**, stated as a list so that "a
legitimate need" is something you can check rather than something you can
argue yourself into:

1. **286 and 386.** 86Box covers these too, and models the machine rather
   than just the CPU — prefer it where the question is about the machine.
2. **Rung 1 of the hard-disk driver** (SPEC.md §52.1) — the IDE task file
   read directly, which is gated on `CPU_286` because an 8088's `in ax, dx`
   is two 8-bit bus cycles at the same port and loses the drive's high byte.
   QEMU has an ATA disk at 1F0h and a CPU that clears that gate; MartyPC's
   8088 never can. **Rung 0 is MartyPC's now** and is the one the target
   machine actually uses (`os8088_xt_hdd` — an XT-IDE controller whose GPL
   option ROM ships with MartyPC, answering int 13h), so the two are
   complementary and neither replaces the other.
3. **SPEC.md §9.5's awkward mouse cases** — a mouse on COM2, the cross-wired
   IRQ4 card, and a modem chattering on the other port (`MOUSEPORT=com2`,
   `com2irq4`, a socket chardev). MartyPC can put its mouse on either port,
   but the cross-wired and modem cases are not built.

That is the list. **"It is quicker to type" is not on it, and neither is
"I already know the QMP commands."** If you find a fourth entry, add it here
rather than treating the rule as advisory.

That ordering is a **reversal**, and it is written down because the old one
cost a great deal. Everything in this tree was QEMU-first for years, and QEMU
is the emulator that is *furthest* from the target: it runs the guest at host
speed on a CPU that is not an 8088, through SeaBIOS rather than a period ROM,
with no CGA and no Hercules card in it at all. It is excellent at counting
work and it is not a machine. Nearly every entry in docs/FIELD-NOTES.md is
something QEMU showed as fine — and SPEC.md §18.91's `AL` bug is the sharper
shape: QEMU ran the buggy binary *correctly and quickly* and reported
nothing, while the real machine was moving 4.6x the sectors anyone asked for.
A tool that is wrong in the flattering direction does not announce itself.

| reach for | when | why |
|---|---|---|
| **MartyPC** | **the default** — any 8088 machine, any of the three adapters, and any question of the form *what is the machine doing* | cycle-accurate CPU, a real BIOS ROM, real CGA/Hercules/VGA cards, and a debugger that perturbs nothing |
| **QEMU** | the three-item fallback list above, and nothing else | on an 8088, MartyPC covers all three adapters, input, screenshots and sound. QMP and `tools/mouse.py` remain the harness for what is genuinely left |
| **86Box** | a machine that is **not an 8088** (the 286 and 386 targets), real sound cards on a period bus, a second opinion on the video probe | period-correct whole machines, and the widest hardware library |
| **the 5150** | anything with a **disk** in it, and the three defects no emulator shows | docs/FIELD-MACHINES.md |

## The one rule that outranks the table: no emulator here is disk-accurate

**MartyPC is cycle-accurate on the CPU and it is not a floppy drive.** It
models instruction timing, the prefetch queue and bus contention; it does not
model a disk that spins at 300 rpm, a head that has to seek, or an interleave.
PERFORMANCE.md Set 11 measured the gap on the same test and the same media:

| | real 5150 | MartyPC |
|---|---|---|
| read 16 KB, cold motor | **8.07 s** | **0.27 s** — 30x fast |
| boot | **38,886 ms** | **2,306 ms** — 17x fast |

So **if a disk is in the path, MartyPC's number is wrong** and it is wrong by
more than an order of magnitude. That includes anything that *contains* a
disk read without being about one — a boot time, a package launch, a Tracker
module load, a Control Panel save. PCem is no better and QEMU is worse. There
is exactly one instrument for disk timing and it is the machine in
docs/FIELD-MACHINES.md.

**And it will not catch a disk CORRECTNESS bug either**, which is the sharper
half. SPEC.md §18.91's `AL` bug is the worked example: `dsk_xfer` asked the
BIOS for nine sectors, the BIOS moved nine and answered `AL = 1`, and the
kernel believed `AL` and re-read the rest one sector at a time. On the 5150
that was 148 sectors and 34 `int 13h` calls for a 32-sector file — 4.6x the
traffic. **The same binary on the same image under QEMU moved 34 sectors in 6
calls**: correct, fast, and completely silent about the bug. It took the real
machine plus §18.94's counters to see it at all, and the boot sector carried
the identical bug undiscovered for as long again. An emulator's BIOS returns
what its author thought the hardware returns; the hardware is under no such
obligation.

The same caution applies to `int 1Eh`'s diskette parameter table, short
`int 13h` reads, and interrupt stack depth — docs/FIELD-NOTES.md 5 and
SPEC.md §8. All three are BIOS behaviours an emulator smooths over.

---

This document exists because the opposite keeps getting concluded about
capability. It has happened for Hercules — `docs/HERCULES-TESTING.md` opens by
saying so, and that claim had sat in CLAUDE.md costing people time — and it
keeps happening for sound, for a duller reason: the AdLib and Sound Blaster
recipes are real, committed and mechanical, but they live in the middle of
`docs/SOUND-PLAN.md`, an 850-line *plan*, interleaved with phase history. A
plan document is not where anyone looks to answer "can I test this?", so the
answer people reach is "no".

Every QEMU recipe below was run end to end on a stock QEMU 8.2.2 and the
measured result is quoted with it. If one of them fails, that is a finding
about the tree, not about the emulator.

**This document answers *where a test can run*. [PERFORMANCE.md](../PERFORMANCE.md)
answers *what the target machine costs* — the calibration numbers, the
standing budget every redraw path has already been measured down to, and the
three visible defects the emulator cannot show. Read that one before changing
anything that draws or loops; "Modelling the old machine from a fast one",
below, is the short version of it.**

---

## The matrix

The **MartyPC** column is "is this the right tool for it", not "does the
emulator have the hardware": a ✅ means reach for it first.

| Capability | MartyPC | QEMU | How (QEMU) | Verified result |
|---|---|---|---|---|
| VGA 640x480x16 (mode 12h) | ✅ | ✅ | `make test`, or the `os8088_xt_vga` machine | boots to Locator; loads packages. MartyPC has a register-level VGA and rasterises 12h — `vid_w=640 vid_h=480 vid_planes=4`, raster 800x524, and Minesweeper renders in 8 distinct palette colours |
| CGA 640x200 mono | ✅ | ✅ | `make test VIDEO=cga` | renders; dumps 640x400 (line-doubled) |
| Hercules 720x348 mono | ✅ | ✅ | `make test VIDEO=herc HERCSEG=0x7000` | renders; 55.8% lit at the desktop |
| Adapter switching (SPEC.md §39.11) | ⚠️ | ➖ | the `os8088_xt_vga` / `_5150_both` / `_5150_herc` / `_5150_cga` machines | MartyPC is the instrument, and one direction is out of its reach. Verified: the page lists **Vga + Cga** on `_xt_vga`, **Hercules + Cga** on `_5150_both`, and exactly **one** row on the two single-card machines; the live switch works **both ways on `_xt_vga`** and **CGA → Hercules on `_5150_both`**; the choice survives a reboot through `SYSTEM.CFG`'s `VM` key; and a disk asking for a card the machine lacks is **refused**, staying on the probe's answer. NOT verifiable here: **Hercules → CGA on a dual-card machine**, because MartyPC's MDA decodes the whole 64KB at B0000–BFFFF whatever 3BFh's page bit says — measured, B0000 and B8000 read back byte-identical — so the two cards contend for B8000 and the CGA's rendered output stays black although the card is correctly in `Mode6HiResGraphics`. A real HGC with that bit clear (which `vid_setmode` leaves clear) decodes B0000–B7FFF only. **That direction wants a run on the 5150.** The same aliasing is what `vid_cga_alias` (§39.11.1) exists to reject, and it is why the Hercules-only machine stopped reporting a CGA that is not there. **Hiding the page** (§31.10.1) verified on both single-card 5150s — the list is Scheduler/Buffer/Date-Time/Drivers/Sound and no Display row — with the static/driver boundary checked on `os8088_xt_hdd`, where the hard-disk driver's own page lands at row 5 (the slot the hidden page vacated) and dispatches to the driver, not to Display. **Blanking the outgoing card** (§39.11.4) verified on `_5150_both` in both directions: CGA → Hercules takes the CGA card from `Mode6HiResGraphics` to `Mode1TextCo40` (3D8h video bit clear), and Hercules → CGA trips an I/O breakpoint on 3B8h — a port `vid_setmode`'s CGA path never touches, so that write is `vid_blank` and nothing else |
| PC speaker | ✅ | ✅ | `make test-snd`, or `MARTYPC_WAV=` | dominant 880.0 Hz (891.0 on MartyPC, inside tolerance) |
| AdLib / OPL2 | ✅ | ✅ | `make test-snd ADLIB=1`, or the `os8088_5150_sb` machine | dominant 880.0 Hz from a keyed 440; the Sound page's Test tone came out of MartyPC's OPL2 at 660 Hz |
| Sound Blaster (DMA streams) | ✅ | ✅ | `make test-snd SB16=1 TESTAPPS=build/sbtest.img`, or the `os8088_5150_sb` machine | 2.00 s at 1000.0 Hz on BOTH. MartyPC's is a DSP **2.01** by default — the classic `0x48`+`0x1C` auto-init path — where QEMU's is an SB16; `dsp_version` picks |
| Boot sound probe (SPEC.md §51.3.1) | ⚠️ | ✅ | the `os8088_5150_sb` / `_sbonly` / `_cga` machines, fresh image | MartyPC is the instrument here: its AdLib **answers the OPL2 timer-flag dance** on a cycle-accurate 8088, which is the whole of what the probe reads and what QEMU cannot show — QEMU's `-device sb16` has an OPL *stub* that does not answer, so on QEMU an SB16-only box reads as cardless. `_sb` → row 0 `WANT` 1, `SEG` 9E80, and the Sound page comes up on **Sound Blaster** with nothing ticked; `_cga` → `WANT` 0, nothing loaded, no `DRVE_HW`; `_sbonly` → `WANT` 0 by default and 1 under `make SNDSNIFF=sb`, against a real DSP 2.01 |
| Scripted mouse / keys | ✅ | ✅ | **`tools/os88mouse.py click X Y`** (absolute, closes the loop — see the MartyPC section; `os88marty.py mouse` is the relative primitive under it), `os88marty.py key`, or `tools/mouse.py` on QEMU | MartyPC drives the REAL devices: a Microsoft packet through the UART (`mou_seen` goes 0→1) and a keystroke through int 09h (SPEC.md §9.6's arrows moved `mouse_x` 320→350) |
| **Screenshots** (CGA/Herc) | ✅ | ✅ | `os88marty.py shot`, or `tools/shot.py` / `hercshot.py` | MartyPC reads VRAM directly — 60.0% lit, matching QEMU's CGA on the same desktop |
| Mouse on COM2 (SPEC.md §9.5) | ➖ | ✅ | `make test MOUSEPORT=com2` | both UARTs probe present, COM2 wins, COM1 retired |
| A **cross-wired IRQ** (SPEC.md §9.5.2) | ➖ | ✅ | `make test MOUSEPORT=com2irq4` | the Compaq Portable III: mouse at 2F8 driving IRQ4. Undetectable before the fix |
| A **modem** on the other port | ➖ | ✅ | a socket chardev at 3F8 — see below | eight result codes claim nothing, move nothing, click nothing |
| Performance benchmarks | ✅ | ✅ | `make bench` (from `tests/`, not in `all`) | numbers are always in flux — see below |
| **Flicker** — the double-draw flash | ✅ | ❌ | `os88marty.py flicker` (PERFORMANCE.md Part 3.1) | one sample per displayed frame. A Disk window repaint flashes 1,963 px for 166 ms; an idle desktop and a pointer move measure zero. CGA and VGA — MartyPC's MDA does not rasterise Hercules graphics mode |
| Fullscreen exclusive (SPEC.md §53) | ➖ | ✅ | `make test TESTAPPS=build/fsxtest.img` | every FSXM mode the adapter owns sets, draws and restores — the desktop screendump below the bar is byte-identical after a full sweep; Mode X dumps 640x480 (line-doubled 320x240) |
| Boot-sector relocation (SPEC.md §2.7) | ✅ | ✅ | `make test RAMKB=<n>` — see below | 105 boots, 104 prints `RAM` and never loads a byte |
| A machine that reports a **small** `int 12h` to the KERNEL | ✅ | ❌ | MartyPC `conventional.size`, or 86Box `mem_size` | `RAMKB=` moves the sector only; the heap still sees the real answer. MartyPC's real BIOS counts what the config says it has |
| Video **detection probe** | ✅ | ❌ | `make marty`, or `make xt-cga` / `xt-hercules` | MartyPC has a modelled CGA and MDA/Hercules, so the probe genuinely runs — this stopped being 86Box-only |
| 6845 programming | ✅ | ❌ | `make marty`, or `make xt-hercules` | modelled by both; MartyPC's `screen` reads the result back without a screenshot |
| Period-correct **CPU** timing | ✅ | ❌ | `make marty` (8088 only), or `make xt` / `286` / `386` | MartyPC is cycle-accurate and agrees with the 5150 on 45 of 47 gfxbench rows. **Not the disk** — see the rule above. 286/386 are 86Box only |

`VIDEO=` forces a code path; it does not exercise the probe that would have
chosen it. That distinction is the whole of QEMU's ❌ column for video: QEMU
emulates no CGA and no Hercules card, so what is untestable *there* is the
*choosing*, not the *drawing* — and the drawing is almost all of the code.
**MartyPC has both cards**, so the probe is no longer 86Box-only; what is
still 86Box's alone is a machine that is not an 8088.

---

## How much RAM the machine says it has

`boot/boot.asm` relocates itself to the top of conventional memory (SPEC.md
§2.7), which it finds with `int 12h`. **SeaBIOS answers 639 whatever `-m`
says** — conventional memory is capped there and the rest is above 1MB — so
neither the arithmetic nor the refusal below the floor can be reached here by
configuring QEMU. `RAMKB=<n>` assembles the sector to believe a number:

```sh
make test RAMKB=128         # where a 128KB machine (MIN_RAM_KB) puts it
make test RAMKB=104         # below the floor: must refuse
python3 tools/qmp.py build/qmp.sock 'xp /4xb 0x600'   # 00 00 00 00 = never loaded
```

Verifying it landed where it should is a memory dump, not a screenshot: the
sector's last two bytes are its `0xAA55` signature, so on a machine of *n* KB
they are at linear `n*1024 - 2`, and its first three are `EB 3C 90`.

```sh
python3 tools/qmp.py build/qmp.sock 'xp /4xb 0x9fbfc'   # 639KB: .. 55 aa
python3 tools/qmp.py build/qmp.sock 'xp /8xb 0x9fa00'   # eb 3c 90 'MSDOS'
```

Three things to know before trusting a run of this:

- **It shares the `VIDEO=`/`RTC=` stamp**, and needs to: the knob touches
  neither `boot.asm` nor `kernel.bin`, so without the stamp `make` rebuilds
  nothing and the machine boots the PREVIOUS relocation while you read the
  new one. That failure was seen once, and it reads as the address arithmetic
  being wrong.
- **It moves the sector and nothing else.** The kernel still asks the real
  `int 12h` for the top of its heap, so this is not a small-machine
  simulation — the rows in docs/KERNEL-MEMORY.md's RAM table below the boot
  floor are still simulated by clamping the heap.
- **The boundary is arithmetic, so test it at the boundary.** The sector
  refuses when its computed base is below where the kernel's read plus its
  own 2,048-byte stack would end, which for a 71,112-byte kernel is 105KB.
  Both sides of that were measured; the number moves whenever the kernel's
  size does.

---

## Video

CGA works because SeaVGABIOS's `int 10h AX=0006h` is a byte-exact CGA
framebuffer, so an ordinary `screendump` shows it. Note the dump comes back
**640x400** — QEMU line-doubles 640x200 — so a crop's Y and H are twice the
kernel's own. VGA is 1:1.

```sh
make test VIDEO=cga
python3 tools/shot.py build/qmp.sock /tmp/cga.png
python3 tools/mouse.py --screen 640x200 build/qmp.sock click X Y
```

Hercules needs its framebuffer relocated into spare RAM (B0000 is unmapped
under QEMU and silently swallows every write), and it is **never**
screendumpable — that framebuffer is guest RAM the VGA device has never heard
of, so `screendump` returns a black or stale VGA screen and does not error.
That silent non-failure is how "Hercules doesn't work" gets concluded from
one screenshot.

```sh
make test VIDEO=herc HERCSEG=0x7000
python3 tools/hercshot.py build/qmp.sock 0x70000 /tmp/herc.png   # LINEAR = HERCSEG*16
python3 tools/mouse.py --screen 720x348 build/qmp.sock click X Y
```

`HERCSEG` is a segment and `hercshot` takes the linear address; the missing
zero is the commonest mistake. Full recipe and the four ways to get it
silently wrong: `docs/HERCULES-TESTING.md`.

**`VIDEO=`/`RTC=` are tracked by a stamp file**, so a knob-built kernel is a
*different* kernel and changing the knob rebuilds it. Nothing in `build/` is
committed, so a forced kernel can no longer reach the repo — but it does stay
on your disk images until something rebuilds them, and a release must be built
knob-free:

```sh
rm -f build/os8088.img build/os8088-720.img build/os8088-360.img && make
```

---

## The mouse's port, and the modem on the other one (SPEC.md §9.5)

`make test MOUSEPORT=com2` gives QEMU a **live but silent** UART at 3F8 and
the mouse at 2F8. That shape is the point: leaving 3F8 unpopulated would make
the probe find one port and the kernel take the single-port path — testing
the easy half and none of the contest. Read the answer out of the kernel rather than off the glass; the
offsets move whenever an include before `mouse.inc` does, so re-derive them
from a listing (`nasm … -l`) and peek at `KERNEL_SEG*16 + offset`:

```
mou_bases  0x03f8 0x02f8     both probed present
mou_need   8                 two live ports, so a contest
mou_port   2   seen 1        the mouse is on COM2
mou_hpst   2                 mou_lockon has retired COM1
```

### `comscan` — when the mouse is not found on real hardware

`make comscan` builds the field diagnostic for exactly that (`tests/comscan`).
It is **not** an os8088 package, deliberately: the thing being diagnosed is the
mouse, so anything you have to click is unreachable on the one machine that
needs it. Two builds from one source —

- **`build/comscan.img`** (360KB) / **`build/comscan144.img`** (1.44MB), a
  *bootable* floppy. The shipped boot sector will load anything at
  `KERNEL_SEG:0000` that honours its three-point handoff (a `retf` at 0x0008,
  a spare word at 0x000C, entry at 0x0000), so this needs no DOS, no os8088
  and no mouse.
- **`build/comscan.com`**, a DOS program on the same disk. Its output goes
  through int 21h rather than the BIOS, so `COMSCAN > COMSCAN.TXT` captures
  the whole report to a file that can be carried off the machine.

It surveys 3F8/2F8/3E8/2E8 — including the two os8088 never probes, because a
live UART at 3E8 is the entire bug on its own and it says so in as many words.
Per port: the BIOS POST's own list, os8088's divisor-latch probe *and the same
probe with a long settle* (they disagree only if the kernel's is too quick for
that machine's bus), the scratch register, an internal loopback, the part type,
a raw register dump, then a 1200 7N1 DTR/RTS pulse and a polled capture with
the Microsoft packet machine run over it — bytes, the identify byte, packets,
violations, and the longest clean run against `MOU_LOCKN`.

**The measurement that matters, and the one nothing here could have made, is
which IRQ line the card actually drives.** os8088 derives it from the base
(3F8 → IRQ4, 2F8 → IRQ3); a card jumpered elsewhere gets probed, programmed
and hooked and then simply never interrupts. Two ways to ask it were built and
only the second works:

- **Reading the 8259's IRR with the lines masked** needs no handler and cannot
  storm, and it is wrong. A masked request is never acknowledged and an 8259
  clears IRR on acknowledgement, so a line asserted once before you arrived
  reads as 1 forever. Measured on QEMU, the base/hot/cold readings around a
  byte were `18h`, `18h`, `18h` — both serial lines stuck up, the card's own
  contribution invisible, every port reporting no interrupt at all.
- **Hooking int 0Bh/0Ch/0Dh/0Fh and arming RX on one port at a time**, which is
  what the kernel itself does. A runaway guard masks any line that will not go
  quiet after `IRQGUARD` interrupts, so a shared line cannot hang the machine.
  One flush pass runs first against *no* port, because unmasking delivers
  everything latched at boot in one go and otherwise whichever port is probed
  first collects it — COM1 reported IRQ3 *and* IRQ4 without a byte ever
  arriving on it.

Verified under QEMU both ways round: mouse on COM2 → `IRQ3`, COM1 silent;
mouse on COM1 → `IRQ4`, COM2 silent. **And it earned its keep on the first
real run**: the Compaq Portable III's mouse turned out to be at 0x2F8 driving
**IRQ4**, which is SPEC.md §9.5.2 and was invisible to every other test in
this tree.

**QEMU can reproduce a cross-wired card**, which turns a field-only bug into
a regression test — and all four combinations are `MOUSEPORT=` knobs:

| | mouse | and the other port |
|---|---|---|
| `make test` | 3F8, IRQ4 | nothing |
| `make test MOUSEPORT=com2` | 2F8, IRQ3 | live but silent at 3F8 |
| `make test MOUSEPORT=com2irq4` | **2F8, IRQ4 — the Compaq Portable III** | live but silent at 3F8 |
| `make test MOUSEPORT=com1irq3` | 3F8, IRQ3 | live but silent at 2F8 |

`-serial` cannot set an IRQ, so those go through `-device isa-serial`, which
takes `iobase=` and `irq=`. On a kernel without §9.5.2 the two cross-wired
rows never find the mouse at all — `[mou_seen]` stays 0 through forty
`mouse_move`s and the cursor never leaves its start.

One trap in writing that test, and it produced two false failures: a movement
pattern that **nets to zero** returns the cursor to where it started, so
"the cursor changed" reports a perfectly working mouse as broken. Drift in one
direction.

### Can we boot a real 5150 BIOS instead of SeaBIOS?

Asked, checked, and **no** — but the instinct behind it is sound and worth
recording so the next person does not re-derive it.

SeaBIOS genuinely does misrepresent a period machine, and it has cost this
project real bugs: the **int 1Eh diskette parameter table** it never reads,
which is the whole of FIELD-NOTES 5 (a real ROM ships EOT = 8 and the
multi-track bit, and QEMU cannot show any of it); a real BIOS's **short int
13h reads**, which SeaBIOS never returns; and **interrupt stack usage**,
which SeaBIOS keeps on an internal stack where a real BIOS lands it on
whichever task stack is current — the reason `tests/stackprobe`'s QEMU answer
is not the answer.

`-bios` will not fix any of that, because it maps a *file* and does not change
the *hardware underneath it*. QEMU has no XT-class machine — `-machine help`
lists i440fx variants, q35, microvm and `isapc`, all 486-era or later — and
the concrete blocker is the configuration read at the very start of the 5150
POST: an IBM PC reads its DIP switches through the **8255 PPI at 60h–63h**,
and every QEMU machine puts an **8042 keyboard controller** at 60h/64h with a
port-61h NMI/speaker latch instead. There is no 8255 anywhere in
`qemu-system-i386 -device help`. The POST fails its first configuration read,
before video, and beeps.

**86Box is the answer to that question and the repo already has it
configured** — `make xt`, `xt-640`, `xt-cga`, `xt-hercules`, `286`, `386sx`,
`386dx`, each with the real ROM set. That is what the "What 86Box is genuinely
for" section below is about.

Two footnotes. `-machine isapc` *does* boot os8088 (ISA-only, no PCI) and is
marginally more period-shaped than the default, but the BIOS is still SeaBIOS
so it buys nothing on the list above; it is not worth changing the default
for. And for the bug that prompted the question — §9.5.2's cross-wired IRQ —
**neither would have helped**: that is a property of a card's jumper, not of
the BIOS, and `-device isa-serial,irq=` models it exactly.

One trap that cost a whole debugging round, and it is the kernel's own idiom
misapplied: the per-port state is walked with a **word** index (0, 2, 4, 6), so
a *byte* array needs `NPORT*2` entries and not `NPORT`. Declared four bytes for
four ports, every array silently overflowed into the next — `p_live[2]` was
`p_phase[0]`, a dead COM3 read as live, and the survey was nonsense.
`kernel/mouse.inc`'s two-port arrays are four bytes and correct, which is
exactly how the wrong shape got copied across.

**The case that actually matters is a talkative device on the other port**,
because a Hayes result code is a well-formed Microsoft packet (§9.5.1). QEMU
can be that device: put a socket chardev at 3F8 and type at it.

```sh
qemu-system-i386 -drive file=build/os8088.img,format=raw,if=floppy -boot a \
  -chardev socket,id=modem,host=127.0.0.1,port=45881,server=on,wait=off \
  -serial chardev:modem \        # 0x3F8 - the "modem"
  -serial null \                 # 0x2F8 - a live UART, saying nothing
  -display none -qmp unix:build/qmp.sock,server,nowait -daemonize
# then: connect to 45881 and send OK/RING/NO CARRIER/CONNECT, CRLF-wrapped,
# pacing each burst by len*10/1200 seconds - it is a 1200-baud line
```

A third trap is in the survey rather than the harness, and real hardware is
what exposed it. **A test that leaves the baud rate wherever the last test
left it is a test whose result depends on the machine.** `t_latch` scribbles
0x55 into DLL and leaves DLM as the BIOS set it, so the loopback that ran
next was clocked at a rate that differed *per port* by however much their
as-found divisors differed — and its bounded wait then timed out on the
slower one and reported a healthy UART as broken. On the Compaq Portable III
that read as COM1 failing loopback while COM2 passed, on two ports that are
both ordinary 8250s. `t_loop` programs divisor 1 (115200 baud, ~87us a byte)
before it starts, and the survey prints the **as-found divisor** in a `DIV`
column, which is what would have made it diagnosable from the first report.

Two traps in building the harness, both of which produced a green run that
proved nothing:

- **`msmouse` speaks during boot.** With a real mouse attached to 2F8 the
  contest is over before the first byte of chatter is sent, so the modem is
  being tested against a port that has already lost. To test the *open*
  contest there must be no mouse anywhere — 3F8 the socket, 2F8 `-serial
  null` — and then `[mou_seen]` must simply stay 0 forever.
- **Assert on more than the port.** The first fix stopped the modem
  *claiming* a port while it was still moving the cursor and latching a right
  button (`mouse_btn` = 2) — a modem opening context menus is the same bug
  wearing a different hat. Check `mouse_x`/`mouse_y` and `mouse_btn` too;
  they must be exactly where the machine booted.

Then run it the other way round — the socket at 3F8 *and* `msmouse` at 2F8 —
and check the mouse still reaches its run, `mou_lockon` still retires COM1,
and chatter afterwards is ignored. The one thing to expect and not to file as
a bug: on a two-port machine the first ~8 packets of the session are counted
and discarded, so `tools/mouse.py`'s first absolute position is wrong. **In
practice it is wrong by the whole move, not by a few pixels** — `mouse.py`
pins against the top-left clamp with a burst of large negative deltas and then
walks to the target, the contest eats the front of the burst, and the pin
still lands because it over-drives; what gets lost is the walk. So the first
`to X Y` after boot leaves the cursor **at 0,0**, which reads exactly like a
mouse that is not working at all, and a screendump cropped to the target shows
nothing rather than something near it. Call `mouse.py` a second time — the
whole session is exact from there. Costed the same way twice, on VGA and on a
CGA field disk.

### The identify burst (SPEC.md §9.4.1)

**QEMU can test the half that must refuse, and cannot test the half that must
accept.** `msmouse` is not a UART-level device — it ignores MCR/DTR entirely
and emits packets during boot regardless — so out of a plain `make test` you
get `[mou_seen]` = 1, `[mou_hpst]` = 2 and `[mou_ident]` = 0: the *old* code
path, exactly, which is the right regression baseline and no test of the new
one.

**MartyPC models the UART and the mouse's reply to a DTR/RTS raise**, which
makes it the cheapest place to test the accepting half — the first
confirmation of it came off a MartyPC memory dump (SPEC.md §9.4.1 carries the
figures). It gives **two live ports**, `03F8` and `02F8`, so it is the
*contest* case rather than the easy one, and `[mou_need]` going `01 08` is
the assertion that matters there. Dump memory at the desktop **with the mouse
deliberately untouched** — that is the state that used to be broken, and
`mouse_x`/`mouse_y` still sitting on `[vid_w]/2, [vid_h]/2` is how you know
nothing has moved yet. It boots the **360KB** pair.

The refusal half uses §9.5's socket-chardev harness, with the sender timed to
land inside `mouse_init`'s drain window — which closes about **1.2 s after
QEMU launch** (sweep a single `M` at 0.2/0.6/1.0/1.4 s to re-find it; the
absolute position drifts a little per boot). Offsets move whenever an include
before `mouse.inc` does, so re-derive them from a listing:

```
idn   idb0   ident  idany  need        verdict
01    'M'    01     01     01 .. 08    a mouse: identified, need lowered
02    'M'    01     01     01 .. 08    'M3' likewise
05    'M'    01     01     08 .. 08    identified, but past MOU_IDSTRICT
1f    'O'    00     00     08 .. 08    Hayes codes - rule 2
2e    'M'    00     00     08 .. 08    'M' + a banner - rule 3
08    'M'    00     00     08 .. 08    a trickle that never stops - rule 4
```

The last row is the one worth building deliberately, because it is the only
way to isolate rule 4: send `'M'` every ~150 ms across the whole window, so
whenever the window closes a byte arrived inside `MOU_IDQUIET` of it, while
the count stays at `MOU_IDMAX` and rule 3 still passes.

**On a machine with no debugger, `sysbench` prints all of it** — SPEC.md
§9.4.2 publishes the block at `0060:0006` unconditionally, so a `make field`
disk carries it and no knob is involved. Its section is a state dump, not a
measurement: the bases and the first identify byte are hex, everything else
decimal, and a mouse that identified reads `first byte 4D`, `identified 1`,
`poller stamp 0`. Under `make test` it reads `44` ident bytes, first byte
`004D` and `identified 0` — msmouse's packet stream happens to start with a
header byte of 0x4D, and **rule 3 refuses it for being 44 bytes long**, which
is the refusal working on live data rather than on a scripted payload.

**Assert on `[mou_hpt]` as well as the identify state**, and that is the
actual regression this exists to prevent: read it, wait four seconds, read it
again. Unchanged means the recovery cycle stood down; advancing by **58**
means the mouse is still being power-cycled every 3.19 s. Two traps, both of
which produced a green run that proved nothing. A test whose sender never
lands in the window reads exactly like a rule correctly refusing — check
`[mou_idn]` is non-zero before believing a refusal. And `[mou_seen]` must stay
**0** in every one of these: an identify moves the prior and must never settle
the contest, so a run where it went to 1 is testing the packet path, not this
one.

---

## Sound

`make test-snd` is `make test` plus a wav capture at `build/snd.wav`,
finalized when QMP `quit` stops QEMU — so **run `tools/sndcheck.py` only
after `quit`**, or you measure a partial file. The capture is stream-on time,
not wall time: a silent boot yields an empty file, which is a pass for
`--expect-silence` rather than a broken harness.

Without `ADLIB=1`/`SB16=1` there is no card, the tone route falls to the PC
speaker, and that is what gets captured:

```sh
make test-snd TESTAPPS=build/fmtest.img
# launch FMTEST, then:
python3 tools/qmp.py build/qmp.sock 'sendkey b' 'sleep 2' 'quit'
python3 tools/sndcheck.py build/snd.wav 880          # -> dominant 880.0 Hz
```

The two gate packages are the mechanical checks. Neither ever ships on the
apps disks — each rides its own scratch image.

```sh
# AdLib: click once. The patch sets carrier MULT=2, so a keyed 440 must SOUND
# at 880 - that doubling is the assertion, and it only holds if the caller's
# patch bytes reached the operator registers.
make test-snd ADLIB=1 TESTAPPS=build/fmtest.img
python3 tools/sndcheck.py build/snd.wav 880          # -> dominant 880.0 Hz

# Sound Blaster: click once for a synthesised 1 kHz square, staged in 20
# chunks and played for 2 s.
make test-snd SB16=1 TESTAPPS=build/sbtest.img
python3 tools/sndcheck.py build/snd.wav 1000         # -> 2.00 s at 1000.0 Hz
```

The window says which half failed: FMTEST shows `K` (both verbs fine), `P`
(patch refused) or `N` (note-on refused), and a bare `N` means the frequency
never reached the driver. SBTEST shows `g:` grant and `o:` open.

`make test ADLIB=1` (without `-snd`) is the same card with no capture — the
right thing when you want to watch the driver attach rather than measure a
tone. **With no card the probe correctly finds nothing**, which is the right
answer and not the one you are trying to test; `sound.drv` ships on the boot
disk and `drv_boot` loads it before the first paint, so a driver that failed
to attach announces itself by opening the Control Panel on its Drivers page.

Depth, including the underrun and capture edge cases: `docs/SOUND-PLAN.md`
Phase 4.

---

## Hard disks

### Booting FROM one (SPEC.md §52.10)

The boot chain — MBR, volume boot record, kernel — is testable **without the
installer, and deliberately so**: if a disk the fixture builds boots and one
the installer builds does not, the fault is in the installer, and without a
fixture there is no way to make that distinction.

`tools/os88hdd.py` writes what SPEC.md §52.10.4 says the installer writes, in
the same order: `boot/mbr.asm`'s 446 bytes and one active partition entry, a
FAT16 volume, `boot/boothd.asm` with that volume's BPB over its first 62 bytes
and the kernel's sector count patched into the word at offset 508, and
`KERNEL.SYS` first and contiguous from cluster 2 — which the VBR *requires*,
because it reads the kernel as a flat run rather than walking a cluster chain.

MartyPC's `os8088_xt_hdd` is the machine, and it is **rung 0** (an option ROM
answering int 13h), which is the rung the field machine uses:

```sh
python3 tools/os88hdd.py \
    --template build/martypc/run/media/hdds/default_xtide.vhd \
    --out /tmp/boot.vhd --kernel build/kernel.bin \
    --vbr build/boothd.bin --mbr build/mbr.bin
cd build/martypc/run && MARTYPC_DEBUG_ADDR=127.0.0.1:9001 ./martypc_headless \
    --machine-config-name os8088_xt_hdd --mount hd:0:/tmp/boot.vhd &
python3 tools/os88marty.py 127.0.0.1:9001 run          # it starts PAUSED
python3 tools/os88marty.py 127.0.0.1:9001 verify       # the kernel is aboard
python3 tools/os88marty.py 127.0.0.1:9001 shot --rendered /tmp/hd.png
```

**NOTE THE `run`.** `martypc_headless` comes up with the CPU paused at the
reset vector, and every debug command answers perfectly well while it sits
there — so a `verify` that reports 88% differing and `boot_ticks: 0` is a
machine that was never started, not a boot that failed. That reads exactly
like a broken kernel and cost an hour once.

**NO FLOPPY IS MOUNTED**, which is the whole point: `--mount fd:0:` is absent,
so anything that reaches for A: has nothing to find. What the screenshot must
show is the desktop with a **Disk C** zone beside Disk A and Disk B. That zone
is the proof rather than a detail — `HDD.DRV` is not loaded (a fixture disk
carries no `SYSTEM.CFG`, so nothing asks for it), so the only thing that could
have created a third volume row is `dsk_boot_from` adopting the partition the
machine booted from (SPEC.md §52.10.3).

Two things this cannot tell you, both of which want the 5150: whether the
**timing** is tolerable (no emulator here is disk-accurate, and MartyPC's
error is 30× in the flattering direction — see the rule at the top of this
file), and whether a **real** option ROM's AH=08h reports the geometry its
AH=02h then translates with, which is the one assumption `boot/boothd.asm`
cannot make without hardware.

### The driver, its partitioner and its formatter

QEMU has an ATA disk at 1F0h and SeaBIOS gives it to int 13h as drive 80h, so
**both rungs of the driver's transport ladder (SPEC.md §52.1) are testable
here** — and so are the partitioner, the formatter, the mount and the desktop
zone, end to end:

```sh
make test HDD=40                 # a blank 40MB raw IDE disk, KEPT between runs
# System menu -> Control Panel -> Drivers -> tick Hard Drive
# -> the 'Hard Drive' page appears in the list; select it
# -> Format -> pick a slot -> Format   ... partitions AND formats it (SPEC.md
#                                          52.2); a slot with something in it
#                                          asks for the click again first
# -> Close -> Mount                    ... one icon per FAT partition: HDD C,
#                                          HDD D, ...
rm -f build/hdd.img              # start over from a blank disk
```

**Pair it with a host-side read**, exactly as the floppy write path is paired
with `os88disk.py --verify`: the in-kernel checks and a structural read catch
different bugs, and every bug found while building this was found by the host
half.

**`tools/os88disk.py --verify` is a FLOPPY fsck and will refuse a hard-disk
partition** - it checks `BPB_FATSz16 <= 10` and a real floppy geometry, both
of which a 31MB FAT16 volume legitimately breaks (SPEC.md 18.2 rule 10 drops
the cap for a driver-backed volume; 18.8 is why). Read the partition by hand
instead. The snippet below prints the BPB; the one after it is the test that
actually catches things - **compare every copied file against its source byte
for byte**, which is how a chunk-size bug that truncated a 116KB file to
64,512 bytes was found, with no error reported anywhere on screen.

```sh
python3 - <<'EOF'
d = open('build/hdd.img','rb').read()
e = d[446:462]                                   # partition entry 0
base = int.from_bytes(e[8:12],'little')
print('type', hex(e[4]), 'lba', base, 'secs', int.from_bytes(e[12:16],'little'))
b = d[base*512:base*512+512]                     # its boot sector
print('jmp', b[:3].hex(), 'spc', b[13], 'fatsz', int.from_bytes(b[22:24],'little'),
      'tot16', int.from_bytes(b[19:21],'little'), 'fstype', b[54:62])
EOF
```

And the one that earns its keep - a FAT16 reader that walks a partition and
compares every file it finds against the original on the host. `tools/` has
no hard-disk fsck, so this is it:

```sh
python3 - <<'EOF'
d = open('build/hdd.img','rb').read()
def vol(lba):
    b = d[lba*512:lba*512+512]
    return dict(spc=b[13], rsvd=int.from_bytes(b[14:16],'little'), nfat=b[16],
                root=int.from_bytes(b[17:19],'little'),
                fatsz=int.from_bytes(b[22:24],'little'), base=lba)
def rd(v,s,n=1): o=(v['base']+s)*512; return d[o:o+n*512]
def fat(v,c):
    off=c*2; b=rd(v, v['rsvd']+off//512); return int.from_bytes(b[off%512:off%512+2],'little')
def dirsec(v): return v['rsvd']+v['nfat']*v['fatsz']
def clus(v,c): return dirsec(v)+(v['root']*32+511)//512+(c-2)*v['spc']
def ents(v, first=None):
    raw = rd(v, dirsec(v), (v['root']*32+511)//512) if first is None else b''
    c = first
    while first is not None and 2 <= c < 0xFFF0:
        raw += rd(v, clus(v,c), v['spc']); c = fat(v,c)
    out=[]
    for i in range(0, len(raw), 32):
        e = raw[i:i+32]
        if not e[0]: break
        if e[0]==0xE5 or (e[11]&0x3F)==0x0F: continue
        out.append((e[:11].decode('latin1'), e[11],
                    int.from_bytes(e[26:28],'little'), int.from_bytes(e[28:32],'little')))
    return out
def content(v,c,size):
    o=b''
    while 2 <= c < 0xFFF0 and len(o) < size: o += rd(v, clus(v,c), v['spc']); c = fat(v,c)
    return o[:size]
v = vol(int.from_bytes(d[446+8:446+12],'little'))     # partition 0
for n,a,c,sz in ents(v):
    print(n, sz)
    if a & 0x10:
        for n2,_,c2,s2 in ents(v,c):
            if n2.startswith('.'): continue
            want = open('build/'+n2[:8].strip().lower()+'.o88','rb').read()
            print('   ', n2, s2, 'OK' if content(v,c2,s2)==want else '*** MISMATCH')
EOF
```

**Persistence: EVERYTHING needs the panel closed first.** Nothing on this page
writes `SYSTEM.CFG` from a click any more - not the geometry editor, and since
SPEC.md 51.9's verb 2 was retired not Mount and Unmount either. All of it is
staged into the kernel's settings struct on the spot and written at the panel's
teardown (SPEC.md 31.8), so a persistence run is: mount, type a geometry,
**click the close box on the LEFT of the title bar**, then quit QEMU, then
`make test HDD=40` again - and Disk A, Disk B and every hard-disk volume should
be on the desktop with no clicks at all. A run that quits with the panel still
open reboots with the probe's numbers back and nothing mounted, which reads
exactly like the blob not persisting and is the test being wrong. **Minimizing
is not closing**, and neither is a hard reset from outside; Special > Restart
is the other way that does flush.

Worth testing once as a pair, because it is the property the blob exists for:
untick the driver, close the panel, reboot (no driver, no icons), then tick it
again - the volumes come straight back, having round-tripped through a boot
where nothing could read them.

Both **change the image and the change persists** - `build/os8088.img` gains
`SYSTEM.CFG` - so `rm -f build/os8088.img build/os8088-720.img
build/os8088-360.img && make` when the next run's starting state matters,
exactly as for a floppy write test. Nothing under `build/` is committed, so
this costs a stale starting state and never a stale commit.

What QEMU cannot show: the **MFM** rung — rung 0 against a real XT controller's
ROM rather than SeaBIOS — and the 8-bit-bus behaviour that gates rung 1 off an
8088 in the first place. 86Box ships the XT ST-506 family (IBM/Xebec, DTC
5150X, WD1002A-WX1 and the Seagate ST-11M/R); confirm the exact `hdc =` key
with the launch-and-`kill -TERM`-and-read-back trick above, because 86Box
rewrites its config with what it actually accepted.

**And check the desktop on CGA**, always: `make test VIDEO=cga HDD=40`. A third
drive zone does not fit above the dock on a 200-line screen and wraps into a
second column to the left (SPEC.md §26.1) — which is invisible on VGA and
therefore exactly the kind of thing that ships broken.

## Everything not shipped lives in `tests/`

`tests/` holds every package that is not shipping software, and it is **not**
`apps/`. Nothing under it is built by `all`, no artifact of it is tracked,
and none of it reaches a shipped floppy — so a normal build and every image
the project ships are exactly what they were before it existed.

Two kinds live there, and the difference is what they assert.

**Gates** answer pass/fail against a capability, and are the mechanical
checks referenced throughout this document:

| Package | Asserts | Run it with |
|---|---|---|
| `fmtest` | the AdLib FM surface (SPEC.md §34.2/§51.4) | `make test-snd ADLIB=1 TESTAPPS=build/fmtest.img` |
| `sbtest` | the Sound Blaster streams (§34.5/§34.6) | `make test-snd SB16=1 TESTAPPS=build/sbtest.img` |
| `filetest` | the write path (§18.4) | `make test TESTAPPS=build/filetest.img` |
| `fsxtest` | fullscreen exclusive (§53): keys 0–8 cycle every mode with an identifying pattern, `x` runs a same-mode bracket, `t` keys a duration-0 tone for the §53.3 legs; the window shows the `fsx_caps` mask (01EF/000F/0011 by adapter) and the last result (`K`/`R`/`F`/`S`) | `make test TESTAPPS=build/fsxtest.img` (also under `VIDEO=cga` / `VIDEO=herc`; `make test-snd` + two instances for the sound legs) |
| `stackprobe` | the 256-byte task-stack margin (§8) | `make test TESTAPPS=build/stkprobe.img` |
| `trklog` | not a gate — a **recorder**. Tracker itself, built with `-DTRKLOG`, logging one record per system tick and writing it to `TRKLOG.TXT` (SPEC.md §45.14) | `make test SB16=1 TESTAPPS=build/trklog.img` |

`benchlib.inc` is the one shared source under `tests/` — the timing loop, the
48-bit arithmetic, the report arena and the file writer that `gfxbench` and
`sysbench` both use. It is shared rather than copied for the reason
PERFORMANCE.md Part 6 rule 7 gives: two harnesses that disagree is how three
of the four sizing bugs in this project were found, and two harnesses that
were copy-pasted cannot disagree.

`trklog` is the odd one out and worth reading the shape of before writing
another like it. It is not a separate program: it is `apps/tracker` assembled
a second time with `-DTRKLOG`, so the thing being measured is the shipped code
and not a copy of it that can drift from it. The hooks in `apps/tracker` are
every one of them inside `%ifdef TRKLOG`; the shipped `TRACKER.O88` carries no
records, no claims and no D/W keys. Recipe:

```
make test SB16=1 TESTAPPS=build/trklog.img   # builds the disk on demand
# double-click Disk B, launch TRKLOG.O88
# X    XT mode (5,500 Hz - what the 4.77MHz floor machine boots with)
# L    load BEVERLY.MOD (it is on the same disk), which starts playback
#      NOT 'P' - that is the PATTERN LOOP toggle (song vs loop one pattern).
#      The bench build lists its own keys on the windowed splash, because P
#      was reached for as the pace cycler for a whole round of testing.
#
#      CLICK.MOD is FOUR patterns and four orders (tests/mkclick.py), and it
#      had to become that: with one of each, `Pos` never left 00, P had
#      nothing to loop that the song did not already play, and every click
#      sounded identical - so the ear could not tie the display to the music
#      at all. Now the position is AUDIBLE (a pitch per pattern, C-2 E-2 G-2
#      C-3, climbing) and so is which of a bar's four clicks you are hearing
#      (row 00 is a fifth above the other three: TICK tick tick tick).
#      Verified in a MARTYPC_WAV capture: bursts 2.0 s apart at ~1555/1025/
#      992/959 Hz, then ~1753/1290/1257/1213, then ~2117/1555/1510/1455.
# D    arm the log       -> the status line counts 'LOG nnnn /0512'
# F    fullscreen; let it run through a few pattern boundaries (~9 s each)
# Esc  back to windowed  (W is refused in a bracket - the file API is
#                         UI-callback-only, SPEC.md 53.7)
# W    write TRKLOG.TXT to B:  -> 'Wrote TRKLOG.TXT'
#
# M    at ANY point, windowed or fullscreen: stamp FL bit 10h into the
#      current tick because you HEARD something. It is the only input to
#      the file that is not a measurement, and the first field capture is
#      why it exists - 62 seconds in which every counter was healthy and
#      nothing in the file knew whether any of it was audible.
#
# Y    the display back on the MIXER (SPEC.md 45.15 off) - and again to undo
# T    the frame clock back on the tick (SPEC.md 45.16 off) - likewise
#      Both are recorded in FL (20h, 40h) on EVERY record, so a file always
#      says which mode it was taken in. They exist so that "it is smooth
#      with Y, jerky without" can be answered in one sitting on the machine
#      that has the problem, instead of one rebuild per hypothesis.
#      T is WINDOWED-ONLY in practice: ttx_clkpick runs at bracket entry and
#      sets the clock back, so pressing it inside fullscreen changes nothing
#      until the next F. Press it, then F.
#
# E    the WINDOWED pacing experiment, seven modes (SPEC.md 45.16.3/45.16.4).
#      Windowed-only - the bracket has a 54.6 Hz clock of its own and nothing
#      to fix. The live mode is named on screen ('PACE C bar'), because what
#      is being judged is what the line does over a minute and the key's own
#      message has gone by then.
#        every  the shipped cadence - an irregular 110/165
#        out    burst, then stars OUT from the centre to the edges
#        hide   burst, ALL stars for the whole wait, then the letters
#               revealed from the centre once the display is back in step
#      (bar, sweep and in were built, judged and dropped; so was the beat
#      ruler, which made the animation sixteen cells wider than it needed to
#      be - see SPEC.md 45.16.4.)
#      In every mode but the first the BODY - the position, the row and the
#      ruler - is REPLACED by the animation for the whole hold. A frozen row
#      number beside a moving animation is a frozen row number, which is what
#      the first two builds got wrong. The LABEL is never blanked, for the
#      other half of the same reason: it is not part of the experiment, and
#      one blinking once a second competes with what it is labelling.
#      (The every-other-frame grid was here as 'B' and is GONE - measurably
#      the widest spread of the lot, and it read as the worst.)
#
#          PACE every  Pos 01/04  Row 02
#          BURST out   Pos 01/04       *         <- the hold, in place of them
#
#      'Pos xx/yy' is the ORDER POSITION out of the song's length - which
#      entry of the arrangement is playing, not a row. It steps once every 64
#      rows, which is 8 s on CLICK.MOD and 8.9 on BEVERLY.MOD, so beside a row
#      counter running at eight a second it looks dead; the denominator is
#      there to say that it is a different kind of number.
#
#      A number answers "which row" and the question here is "are the rows
#      EVENLY spaced", which only a moving marker answers. Even walk plus a
#      click on the wrap is the whole of "smooth and in time".
#
#      JUDGE THESE ON CLICK.MOD, NOT ON BEVERLY.MOD (SPEC.md 45.16.5). The
#      mixer eats the drawing: windowed on a 4.77MHz machine CLICK.MOD gets
#      16.8 frames/s against 8.00 rows/s, and BEVERLY.MOD gets 6.0 against
#      7.14 - so on a real module the display cannot show every row at all,
#      C and D never complete a burst, and there is no bang or sweep to see.
#
# K    XT mode's SAMPLE RATE - 4,000 / 5,500 / 11,000 Hz - WITHOUT leaving XT
#      mode (docs/FIELD-NOTES.md 16). Windowed-only, takes effect at the next
#      Play, and the text screen's header cell says which rate is running.
#      X is the wrong way to do this: it changes the SURFACE too, to the
#      graphics FT2 screen, which SPEC.md 45.9.1 measured at 2,567 glyph
#      cells a second - unusable on a real XT, and a starved frame clock
#      degenerates tui_playpos so the measurement is not the one you wanted.
```

**The buffer is a RING of the LAST 512 ticks (28 s).** It used to stop at the
first 1,024, which is the wrong half of a session to keep - the listener arms
it, plays, hears the thing, and only then reaches for W. `tlog_save` emits
oldest-first, so the file reads forwards in time either way.

The columns beyond §45.14's original set: **AR AP** are what the SCREEN
showed, which since SPEC.md §45.15 is the row the CARD is playing and not the
row the mixer is on - so `PS PT RW` and `AR AP` disagreeing by the ring lead
is the healthy case, and either of them frozen is not. **SD** is the number of
stamps between the two, i.e. the same lead counted in ROWS: it should sit near
`1.19 x BPM / speed` (22 at the default tempo) and pinning at 63 means the
stamp ring lapped. **FX DX** are the longest single drawing frame and feed
pass in that tick, in ticks, where 00 is the healthy answer for both.
**PLAY** is `CONS` interpolated between block IRQs (SPEC.md §45.15.1):
`PLAY - CONS` should stay between roughly zero and one block, and `AR`
advancing one row at a time rather than three is what it buys.

That set earned itself on its first run: `SD` pinned at 63 with `AR` frozen
for a hundred ticks and then jumping 44 rows, twice a capture, exactly when
`CONS` was in the upper half of its 16-bit range - which is how `mp_at_pos`'s
`jg` (a signed compare of the operands, so it honours the overflow flag) was
caught standing in for the kernel's wrap-safe `js` idiom. After the fix the
same capture holds `SD` at 22-25 and the screen row never stands still for
more than 7 ticks, which is the block-IRQ interval and not a stall.

Read it back off the image from the host with a FAT12 extractor, or mount
`build/trklog.img` — it is an ordinary floppy. **The disk must not be
write-protected**, for the same reason the bench disks must not be. The log
buffer is a heap claim taken at D and given back at D, so an unarmed log costs
no memory and cannot split the heap.

`filetest` also has a fragmented-volume variant, `build/filetest-frag.img`,
and its results are worth pairing with the host-side fsck — the in-kernel
free-space check and `python3 tools/os88disk.py --verify <img>` catch
different bugs.

`stackprobe` is the one gate whose QEMU answer is NOT the answer: its worker
0xCC-fills its own stack slice, spins so every interrupt the machine takes
lands there, and reports the live high-water mark against 256 (the canary
line confirms it watched the word SPEC.md §8 protects). SeaBIOS services its
interrupt entries on an internal stack, so under QEMU only this kernel's own
tick and mouse handlers land on the slice (~90 bytes with the worker's own
frames); a real IBM BIOS runs int 09h on the current task's stack and STIs
early, so the tick and the mouse nest ON TOP of it. `make stackprobe` builds
`build/stkprobe360.img` for exactly that trip: boot `os8088-360.img` on the
real machine (or `make xt`), launch `STKPROBE.O88` off the probe floppy, hold
a key down, mash the mouse, play a Tracker module — then read High water.
Measured on a real 5150 (Hercules, 20MB MFM) under a floppy-to-hard-disk
copy plus typematic plus mouse: **112 of 256, canary intact, 217 samples** —
the ~20 bytes over QEMU's 92 being the BIOS nesting this gate exists to see.

**Benchmarks** answer *how fast*. `fontbench` prices the *primitive* (SPEC.md
§6.1.1): one ten-character run drawn four ways, as the hand-written
`gfx_fill` + `font_str` pair and as one `font_run`, each byte-aligned and
again at x+5. `typebench` prices the *keystroke* (§11.94): 40 characters typed
into a 40-cell line with the whole line redrawn after each, which is what
`np_redraw` does to its dirty band. `gfxbench` prices the *whole drawing
surface* on whichever adapter it booted on; `sysbench` prices the *machine*
underneath it. All four ride one disk.

```sh
make bench                                                 # build the two disks
make test                            TESTAPPS=build/bench.img
make test VIDEO=cga                  TESTAPPS=build/bench.img
make test VIDEO=herc HERCSEG=0x7000  TESTAPPS=build/bench.img
```

### A benchmark that is meant for the FIELD is ONE BOOTABLE DISK

Binding, and stated here because this is the file a person reaches for when
they are *writing* a harness — docs/FIELD-MACHINES.md has said it since
`make field` was built, under that target, and being filed under a target is
how it gets missed. `tests/npbench` was built as a second disk to be put in
B: and had to be rebuilt as a boot disk.

**The calibration machine has one floppy drive** (docs/FIELD-MACHINES.md). A
harness on a data floppy therefore means a swap mid-session, and on that
machine a swap is a walk to another room and back — so the operator cannot
boot the OS and mount the bench at the same time, and the numbers do not get
taken.

So a field harness rides the SYSTEM disk: `--boot`, `--kernel`, the drivers,
`TASKMGR.O88` in `SYSTEM/`, and the harness itself where a double-click
reaches it. `make field` and `make npbench` are both that shape; copy either
rule. Two consequences that are easy to miss:

- **It must not be write-protected.** The report is the deliverable and a
  protected disk answers int 13h status 03h, which the OS correctly reports as
  `Write protected`.
- **A harness that is a rebuild of a shipped app should carry the app's own
  file name**, not the harness's. SPEC.md §54's association resolves a
  document by the app's *stem*, so `npbench` ships as `APPS/NOTEPAD.O88` and
  the operator opens the reference note by double-clicking it. Named for
  itself, they would have to launch it and drive a file dialog by hand.

`make bench`'s four harnesses ride a scratch `TESTAPPS=` image because they
are driven under an emulator here, where there are two drives; the moment one
of them is wanted on the 5150 it needs the treatment above.

### `gfxbench` and `sysbench` — the two that write a file

The first two benchmarks answer one question each and fit on a screen. These
two answer forty, and a CGA screen holds seventeen lines — so they page
(`Space`/`PgDn`/`PgUp`/`Up`/`Dn`/`Home`/`End`, or a click) and they **save
the whole report to a text file** with `S` or the Bench menu. `R` re-runs.
That file is the deliverable: it is meant to be carried off the machine and
pasted into [PERFORMANCE.md](../PERFORMANCE.md).

| what | where it lands |
|---|---|
| `gfxbench` on VGA / Hercules / CGA | `GFXVGA.TXT` / `GFXHERC.TXT` / `GFXCGA.TXT` |
| `sysbench` | `SYSBENCH.TXT` |

**The file goes to the CURRENT volume and directory** (SPEC.md §19.2), which
right after launching a package off the bench disk is that disk's root — so
the ordinary thing works. It means the bench floppy must **not** be
write-protected, and on 86Box that is the `wp://` prefix the config keeps
growing back — which `make xt` and friends now strip off both floppy keys at
launch.

`gfxbench` is ONE package for Hercules and CGA on purpose. Both are the same
1bpp software renderer over four different numbers (SPEC.md §39.3), which it
reads from `OSAPI_VIDEO` at run time; two sources would be two chances to
drift, and the whole value is that the Hercules column and the CGA column are
the same measurement. It runs on VGA too, for contrast.

What it measures, and why in that shape:

- **Raw bandwidth first**, because everything above it is explained by it.
  The same loop — 32 rows of 64 bytes — runs against plain RAM and against
  the framebuffer, so the ratio between those rows IS the bus penalty with
  the loop, the addressing and the string instruction identical on both
  sides. Word write, byte write, word read and byte read-modify-write are
  priced separately because the kernel's inner loops use all four and on an
  8-bit bus they are not proportional.
- **Primitives at TWO SIZES** wherever the cost has a per-call part and a
  per-pixel part (8×8 against 64×64, 8 px against 256 px). One size cannot
  separate them, and pricing a rect the harness never drew needs both terms.
  The derived block does that subtraction and prints its inputs beside it.
- **`gfx_blit4` twice** — a solid source and a four-pixel-run source. That
  pair is PERFORMANCE.md Part 3 item 4 made mechanical: a run coalescer that
  has quietly stopped coalescing shows as a ratio near 100, and one number
  could never show it.
- **The same ten characters as `fontbench`**, so the two harnesses check each
  other for free.
- **The gfx lock, measured backwards** — `GFX_UNLOCK+LOCK pair`. A package
  cannot call `OSAPI_GFX_LOCK`: it runs inside a callback that already holds
  it and the lock is not reentrant, so that is a deadlock rather than a slow
  row. Unlock-then-lock is the same two routines in the other order, and it
  is an idiom the kernel already uses inside a callback (`fm_drag`). What is
  in them is not the mutex but the **mouse cursor** (SPEC.md §7.1), and a
  field log put the pair at 21.8% of a Missile Command session with no pixel
  of the game in it. It is the one row here whose measured span cannot be
  interrupt-free — `gfx_lock` ends with `sti` by contract — so one IRQ per
  iteration can land inside it; the error is bounded and upward, and
  `[bl_max]` and the `!` flag are what to read it against.

`sysbench`'s headline is the one PERFORMANCE.md Part 2 has been quoting from
memory: **8086-nominal clocks against a real 8088**, per instruction class,
with the book figure and the ratio printed beside the measurement. The
interesting part is that the ratio is not one number — it is near 1.0 for
`mul`, which is execution-bound, and much worse for `nop`, which is starved
by a 4-byte prefetch queue behind an 8-bit bus. It also prices RAM
bandwidth, the clock ladder, the API's far-call floor, **what the kernel's
own interrupts cost per second of ordinary work** (the same workload timed
with interrupts off and then on), and the floppy — twice, because the first
read pays the motor spin-up and quoting either figure alone misleads.

Three things about reading their output:

1. **A method-`t` row of 0 counts finished inside one 55 ms tick.** True on a
   fast host, and never true on the machine this is for.
2. **A `!` flag means one iteration came within a third of the PIT wrap.**
   The number is still probably right; it is no longer trustworthy.
3. **Under QEMU almost every row is noise**, and two are worse than noise:
   the retrace period (QEMU's status port toggles on every read so a poll
   always terminates) and the VRAM rows under `HERCSEG=` (B0000 is unmapped,
   so those rows measure plain RAM and the bus ratio reads 100). Both say so
   in the report's own header. `build/bench360.img` on real iron is the
   point of the exercise.

Every one of these images builds on demand — `TESTAPPS` is a prerequisite of
the test targets, so naming one is enough. `make bench` exists for building
the two benchmark disks *without* booting, e.g. to write `bench360.img` to a
real floppy.

The rest of this section is about the benchmarks, because a gate's answer is
a boolean and does not rot the way a number does.

**The `testing` branch still exists, and is now for developing these**, not
for holding them. A harness takes several rounds to get right — two of the
three corrections below were to the measuring apparatus, not the thing
measured — and that iteration does not belong in `experimental`'s history. A
finished harness lands here; the midway artifacts of writing one stay there.

**Treat every number as provisional and cite where it came from.** This is
not a caveat about tidiness — the figures have been wrong in ways only real
hardware exposed, twice in quick succession: the elapsed counter was 16-bit
and a real run overflowed it, and then the ratio overflowed because it came
from counts shifted right by 4 that real rows exceed. A third correction went
the other way: SPEC.md §6.1.1 predicted `font_run`'s true win sat near the
framebuffer-traffic figure, and a 4.77 MHz 8088 with a Hercules card measured
1.30x — the *instruction* figure to three digits. Per-cell overhead dominates
the byte-writes it guards. So a benchmark number quoted without a date and a
machine is worth very little.

**Under QEMU the numbers are not time at all.** QEMU runs the guest at host
speed, so add `-icount shift=3,sleep=off` and the PIT counts guest
*instructions* — reproducible and machine-independent (±1 count across runs),
but not microseconds, and it understates the mono win because what alignment
removes is disproportionately memory traffic. The Makefile has no knob for
it; override the whole command instead, which is the shortest correct form:

```sh
make bench
make test TESTAPPS=build/bench.img \
     QEMU="qemu-system-i386 -icount shift=3,sleep=off"
```

`build/bench360.img` on a real 4.77 MHz 8088 (or 86Box) is where the PIT is a
wall clock and the microsecond column means microseconds. That is where these
numbers are worth taking. A VGA run measures the *fallback* path by design —
`font_run`'s fast path is mono-only.

Nothing under `build/` is committed — bench artifacts included, along with the
shipped images themselves — so there is no way for one of these disks to reach
the repo or a release. What keeps them off a normal build is `all`, which
builds nothing from `tests/`: that is the arrangement this folder exists for.

---

### `tests/assoctest` — the file type association gate (SPEC.md 54)

```
make test TESTAPPS=build/assoctest.img
```

...then **double-click `TEST.AST`, not `ASSTEST.O88`**. Every row is about
what happens when a *document* is opened, so launching the program by hand is
the control: rows 1-4 read `-` and that is correct, not a failure.

Six rows, all of which must read **PASS**:

1. the **header declaration** (§54.6) routed the document here - nothing was
   registered at runtime, so only a rule in this package's header can have.
   It ships **no icon**, so its block sits at offset 32 rather than 96, which
   is the layout arithmetic most likely to be got wrong
2. `OSAPI_ARG_FILE` handed over a name, and it is `TEST.AST`
3. the locator works - `FILE_GOTO` then `FILE_READ` returns bytes
4. `ARG_FILE` is **read-and-clear** - the second ask reports nothing, which is
   what stops a later instance inheriting a document
5. `OSAPI_ASSOC_SET` takes a registration
6. ...and **refuses honestly** when the table fills, rather than corrupting it

Before the double-click, the listing itself is half the test: `TEST.AST` must
already carry a **document page icon**, and a *bare* one, because the program
ships no glyph to inset.

## Modelling the old machine from a fast one

Everything above is about *where* to run a test. This is about the systematic
error in running it anywhere but the target, and it has now cost four bugs, so
it is worth stating as a method rather than a warning.

**Most of this section is about QEMU, and MartyPC removes a good deal of
it** — a cycle-accurate 8088 does not have "a clock that tells you nothing",
and a constant sized while watching one is sized against roughly the right
machine. Read it anyway, for two reasons. It is the record of *how* these
mistakes are made, and the shape recurs whatever the emulator: an
optimisation that keeps its form and loses its substance still measures as a
success. And **the part that MartyPC does not fix is the disk**, where its
error is 30x and in the flattering direction — so every rule below applies to
disk work on MartyPC exactly as written.

**The container is roughly three orders of magnitude faster than a 4.77 MHz
8088.** Every constant you size while looking at QEMU is sized against the
wrong machine, and the failures are not proportional — they are structural,
because the constants encode *ranges*:

| what was sized against QEMU | what a real XT did |
|---|---|
| a 16-bit elapsed counter, one subtraction start-to-end | rows are 1.5M counts; it lapped silently into a small plausible number |
| `>= 32768 means the run overran` | most legitimate rows are 32768..65535; it discarded them |
| a ratio computed from `counts >> 4` | `>> 4` is still 90,000; it overflowed the word and printed 696 for 134 |
| `OSAPI_WM_GROW` on every keystroke | free in the emulator; a visible flicker in a 13×13 corner at 33 ms a keystroke |

The rule that falls out: **when a harness has to hold a range, size it from the
slowest machine it will ever run on, not the one in front of you.** A 32-bit
accumulator folded per iteration costs a few instructions and cannot lap; a
16-bit one sized "generously" against QEMU is wrong by 20x on hardware.

### Three calibration numbers, so an estimate needs no machine

All three are measured on the 4.77 MHz IBM 5150 this project targets
(`tests/gfxbench` and `tests/sysbench`, PERFORMANCE.md Part 9), not modelled:

- **About 756 µs of fixed cost per `gfx_*` drawing call**, before it draws a
  single pixel. `GFX_PIXEL` and an 8-pixel `GFX_HLINE` measured within 1 µs of
  each other, on Hercules *and* on CGA, whose framebuffers are 13% apart — so
  the floor is CPU-side (~3,600 clocks of far call, lock, clip test, dispatch
  and `gfx_rowbase`), not bus-side. **A redraw is priced by how many primitive
  calls it makes, not by how many pixels it covers.** That is the single most
  useful sentence in PERFORMANCE.md and it is the one this project spent years
  not believing.
- **About 1 ms per 8×8 glyph cell.** Four independent measurements agree:
  `fontbench` 10.09 ms per ten cells, `typebench` 33.3 ms per forty,
  `gfxbench` 901 µs for one `font_char` and 915 µs per cell across a whole
  78×34 page. A 40-cell line redraw is ~36 ms, so a keystroke that redraws its
  row costs about that.
- **Instructions are the better proxy, not framebuffer traffic.** SPEC.md
  §6.1.1 predicted the opposite and was corrected by measurement: per-call and
  per-cell overhead dominate the byte-writes they guard. The general form is
  the **8088 instruction floor** — 4.34 clocks per instruction *byte*, which
  is what the prefetch queue can deliver — so an 8086 cycle count under-reports
  an 8088 by anywhere from 1.01× to 4.34× depending purely on encoding length.

Two figures that used to sit here were wrong and are worth knowing were wrong,
because they are still quoted in old commit messages: a framebuffer
read-modify-write is **79.6 clocks, not ~30**, and only about 7 of those are
the bus; and the "add 20–40% for the 8088" rule of thumb was replaced by the
instruction floor above. The back buffer's ~24× flush-to-render ratio was
never measured on hardware and cannot be — double buffering is VGA-only
(SPEC.md §32) and this machine has no VGA.

The full table is [PERFORMANCE.md Part 2](../PERFORMANCE.md), together with the
standing budget every redraw path in the tree has already been measured down
to. Check a change against that table before concluding it is free.

### Count work, don't time it — QEMU is exact about the first and useless at the second

**On MartyPC you can now do both**, which is the shortest statement of why it
comes first: `step` returns real cycles, so "how much work" and "how long"
are one question there — for the CPU. For a disk it is still neither, and no
amount of cycle accuracy makes 0.27 s into 8.07 s.

Under QEMU the split is absolute. The container's clock tells you nothing
about a 4.77 MHz machine, but the *amount of work* the guest does is identical
on both, and QEMU will report it exactly. So when the question is "is this
slow because it does too much?", **instrument a counter and read it over QMP**
rather than reaching for 86Box:

```nasm
; kernel/font.inc, in .text so the offset is fixed
dbg_cells:  dw 0
...
font_run_cell:
    inc word [cs:dbg_cells]
```

```sh
nasm ... -l /tmp/k.lst   &&  grep dbg_cells /tmp/k.lst     # -> 0x1E78
python3 tools/qmp.py build/qmp.sock 'xp /2xh 0x2478'       # KERNEL_SEG*16 + off
```

`h` is a word; HMP's `w` is four bytes. Editing any include **before** the one
holding the counter moves the offset, so re-derive it after every rebuild.

A **package** can write the same counter — `mov ax, KERNEL_SEG / mov es, ax /
inc word [es:0x1E7E]` — which is how a walk inside an app is counted without
knowing the segment its region was claimed at.

This is what settled the Note Pad question (SPEC.md §27.4). A user reported
typing getting slower as a note grew and inferred that more than one character
was being redrawn. The cell counter said **2 cells per keystroke at every note
length and every window width** — the drawing was already right — and a
counter in the layout walk said 404 iterations, growing linearly. The cost was
in a place no screenshot could show and no wall clock here could measure.

Two rules that fall out of it:

- **Measure before redesigning.** The obvious hypothesis (the delta span is
  growing) was wrong, and the fix it would have produced was a fix to working
  code.
- **A counter is not a timer.** It tells you how many times something ran, not
  what it cost. Multiply by the calibration numbers above to get milliseconds,
  and say that you did — ~500 8086 cycles per walk iteration is a reading of
  the instruction stream, not a measurement.

### Reading a kernel word whose offset you cannot grep for

`ticks` is in `.bss`, and a `.bss` label's address in the listing is
**section-relative** — `mov ax, [ticks]` prints as `A1 [2A02]`, and 0x022A is
an offset into `.bss`, not into the segment. Grepping the listing for it and
adding `KERNEL_SEG*16` reads the wrong address, and the wrong address is
usually a plausible-looking number rather than an error. Take the operand out
of the **built binary** instead, which is by definition the linked one:

```sh
python3 - <<'EOF'
import struct
d = open('build/kernel.bin','rb').read()
off = struct.unpack('<H', d[0x606:0x608])[0]     # the imm16 of osapi_get_ticks
print("ticks at linear 0x%X" % (0x600 + off))    # KERNEL_SEG*16 + off
EOF
```

`0x605` is `osapi_get_ticks`'s `A1` opcode; the two bytes after it are the
address. Every other `.bss` word is that one plus the difference of their
listing offsets. It moves on every rebuild — re-derive, and note that the
answer can be **odd**, so a word dump on even addresses straddles it and shows
two neighbours changing instead of one.

That is how `FSXF_FASTTICK` (SPEC.md §53.2.1) is verified, and the check is
worth copying for anything that claims to leave a clock alone:

```sh
# at the desktop, inside an armed bracket, and after leaving it - all three
# must read 18.2/s, because the ISR divides and [ticks] does not change rate
python3 tools/qmp.py build/qmp.sock 'xp /1xh 0x<ticks>'   # ...twice, 10s apart
python3 tools/qmp.py build/qmp.sock 'xp /2xb 0x<sch_fast>' # N inside, 0 after
```

Both of that feature's bugs assembled cleanly, booted, and drew a correct
first frame: one halved the tick rate (the flag was stored from the register
the divisor's table index had been shifted into), and the other corrupted the
low byte of the app's entry offset. The rate reading caught the first and
nothing else would have — a display that scrolls smoothly at half speed looks
like a display that scrolls smoothly.

### Prefer a self-checking harness to a careful one

Three of the four bugs above were caught by **one number on screen
contradicting another**, not by inspection:

- `typebench`'s CHAR row does 1.33x `fontbench`'s PAIR work, so it cannot be
  the smaller number — yet PAIR reported the overrun sentinel and CHAR
  reported 15551. Only one reading is consistent, and it identified the lap
  and its size.
- The ratio was wrong while the counts and milliseconds beside it were right,
  which localises the fault to the one column computed differently.

So put **redundant quantities on the screen**: a raw count *and* a derived
time, two rows whose relative sizes are known in advance, a ratio you can
recompute by hand from the columns next to it. A harness that reports one
number per run is one you have to trust.

### What the emulator cannot show at all

Not "shows inaccurately" — cannot show. **Do not call all of these
"flicker"**: they are three defects with three causes and three fixes, and
lumping them together is how one gets fixed and the others ship
([PERFORMANCE.md Part 1](../PERFORMANCE.md) is the full vocabulary).

- **A visible redraw, which is not a flicker at all.** A window's whole
  content, or the whole screen, being painted again. On real hardware you
  *watch it happen* — the fill sweeps, then the text lands row by row — and
  on a heavy application (Paint, the Task Manager, a full Disk window) that
  is **seconds**, not a flash. Under QEMU it is microseconds and a
  screendump taken either side of it is identical. This is the single most
  expensive mistake available in this codebase, and the one an emulator is
  least able to warn about.
- **A double-draw flash.** Anything drawn twice — background, then content.
  The erase-and-letter pair is the canonical case: it leaves a line blank
  between the fill and the last glyph, tens of milliseconds on an XT,
  several display frames. The area is smaller than a full redraw so it reads
  as a flash rather than a wait, but it is still **very plainly visible**, on
  every keystroke. Note Pad's per-keystroke flash (SPEC.md §27.2) and the
  grow box's were both found by a person watching the real machine, and
  neither appears in any timing column, because the two methods take
  comparable *time* and differ in what is on screen during it.
- **Perceived latency and input overrun.** Whether a human can outpace the
  redraw — and start losing keystrokes to a full BIOS buffer — is a property
  of the real machine's speed against a real person's typing. A view that
  costs more than its frame budget reads as a *hung* display rather than a
  slow one, which is why the tracker stops animating its grid on a tier-0
  machine (SPEC.md §45.9.1).

And one the emulator reports as a **success**, which is worse:

- **An optimisation that kept its shape and lost its substance.**
  `gfx_blit4`'s first version emitted one call per run exactly as designed,
  and decoded every pixel individually inside the scan — 75–90 clocks a pixel
  against `repe scasb`'s seven and a half, both written down rather than
  measured. **Under QEMU it measured as exactly as fast**, because QEMU models
  no 8086 timing: every screendump was right and every test passed. That is
  why the cycle counts in `kernel/vga12.inc` are written down rather than
  measured, and why rewriting something whose *reason* is speed means
  verifying the reason survived, not the structure.

  This entry used to quantify it as "a 448×280 repaint went from about a
  quarter of a second to over two". Those two figures came from the same two
  written-down cycle counts and were never measured; the primitive has since
  been priced on the target machine, and it costs **`runs × 0.5 ms`** — so
  what a blit costs is decided by how *flat* the art is, not how big it is
  (SPEC.md §5.4, PERFORMANCE.md Part 9). The lesson above survives the
  correction intact. The numbers did not.

For all of these, the emulator's role is to prove *correctness* before you
burn a floppy. The judgement is made on hardware.

**Wall clock here is still a lower bound worth having.** Paint's figures
under `make run-640` — a full-canvas flood fill in ~4 s, a 448×280 4bpp BMP
open in ~8 s — are useful precisely because they are already slow *in the
emulator*. A real 8 MHz machine is several times slower and a 4.77 MHz 8088
slower again, so anything measured in seconds here is out of reach on the
target. That is how JPEG was ruled out (docs/PAINT-NOTES.md), and the
AT-class 86Box targets (`make 286`, `make 386sx`, `make 386`) are the honest
middle of that range.

## MartyPC — the first thing to reach for

> **Before you script a single click: use `tools/os88mouse.py`, not
> `os88marty.py mouse`.** The latter is *relative* — a real Microsoft packet
> through the real UART, which is what makes it worth having — so aiming at a
> control means dead reckoning from the kernel's edge clamp, and that drifts:
> a packet is a signed byte per axis, the UART is 1200 baud so anything under
> ~25 ms apart can be dropped, and the clamp eats overshoot without saying so.
> **The failure looks exactly like a broken feature**: the click lands three
> pixels off a 16px control, nothing happens, and the session reports a bug
> that is not there. `os88mouse.py` reads the live cursor out of the debug
> registry (SPEC.md §9.4.3), sends the exact remaining delta and re-reads
> until it agrees — pixel-exact in ~0.8 s, and it *fails loudly* when it
> cannot converge.
>
> ```sh
> python3 tools/os88mouse.py 127.0.0.1:9001 where
> python3 tools/os88mouse.py 127.0.0.1:9001 click 445 153
> python3 tools/os88mouse.py 127.0.0.1:9001 dblclick 150 90    # NOT two clicks
> python3 tools/os88mouse.py 127.0.0.1:9001 menu 12 8 40 45    # NOT click
> python3 tools/os88mouse.py 127.0.0.1:9001 drag 200 78 200 120
> ```
>
> `menu` is its own verb because a menu **cannot** be opened with a click —
> `menu_track` draws the pull-down and then polls a level, so press-and-release
> in place opens and closes it in one breath.
>
> **`dblclick` is its own verb for the same kind of reason, and it is the one
> that has cost the most time.** Two `click`s are not a double-click: `click`
> ends in a 1.5 s settle and every detector in the system (SPEC.md §22/§26/§38
> and `ui_tdbl`'s title bar) compares the two presses' BIRTH TICKS against a
> **9-tick** window. Take the sleep out and the opposite trap closes: packets
> sent faster than the 1200-baud UART can carry them are **dropped**, so the
> guest decodes one press instead of two. Either way the guest sees a single
> click — a file row *selects* instead of launching, a title bar *drags*
> instead of zooming — and nothing anywhere says so.
>
> So `dblclick` proves all four button edges against the published `mouse_btn`
> (the same discipline `to` applies to the position) and then measures the gap
> between the two presses in the guest's own 18.2 Hz ticks, read from the BIOS
> counter at `0040:006C` — the kernel's clock and the kernel's units, not a
> guess about host timing. It prints the span, and **raises** if an edge never
> arrived or the window was missed. A healthy double-click reads 2–4 ticks.
>
> **One connection at a time.** The debug server accepts a single client, so a
> script that wants both the mouse driver and the framebuffer must share one:
> build the `Mouse` and use its `.m`, never a second `Marty`. Opening a second
> one does not error — it *hangs* until the read times out.

`make marty`, and the whole recipe is docs/MARTYPC-DEBUG.md. What it gives
that neither of the others does:

- **A cycle-accurate 8088 running a real 1982 IBM BIOS**, agreeing with the
  5150 on 45 of 47 `gfxbench` rows (PERFORMANCE.md Set 11). QEMU's guest runs
  at host speed through SeaBIOS; 86Box is period-*correct* rather than
  cycle-accurate.
- **A debugger that costs the guest nothing.** Memory, registers, ports,
  breakpoints, single-step, cycle counts, over a socket, with no driver and no
  UART in the guest. `verify` dumps `KERNEL_SEG` and diffs it against
  `build/kernel.bin` in one command, which is docs/FIELD-MACHINES.md's
  self-validating dump automated.
- **A modelled CGA and MDA/Hercules**, so the SPEC.md §39.1 detection probe
  actually runs — the thing this document called 86Box-only for years.

- **A PC speaker, an OPL2 and a Sound Blaster**, captured to one wav per
  source with `MARTYPC_WAV=`, in the format `tools/sndcheck.py` already
  parses. The SB is ours (`devices/sblaster.rs`, in our patch — upstream has
  the OPL2 and the 8237 but no DSP), and it is a **DSP 2.01**, so os8088's
  driver takes the classic `0x48`+`0x1C` auto-init path rather than QEMU's
  SB16 one. `tests/sbtest` gives the same 2.00 s at 1000.0 Hz on both.

What it does **not** cover, and where to go instead: 286/386 (86Box), and
**anything with a disk in it** (the 5150, and nothing else).

**Input is not on that list either, and no guest module was needed for it.**
`os88marty.py key` enters the emulator's keyboard buffer, so the guest sees a
keystroke through the 8255 and int 09h; `mouse` builds a real Microsoft
3-byte packet and clocks it into the serial controller, so `mou_isr` decodes
it. Both drive *more* of the real path than a memory poke would — a poke to
`[mouse_x]` skips the UART, the decoder and SPEC.md §9.5's port contest — and
more than QEMU's `msmouse`, which is not a UART-level device and ignores DTR.

**Screenshots are NOT on that list.** `os88marty.py shot out.png` reads the
framebuffer out of VRAM and decodes SPEC.md §39.3's banked layout — the same
arithmetic `tools/hercshot.py` uses, verified against QEMU's CGA at 60.0% lit
on both. Starting QEMU to look at a screen when MartyPC is already running
costs minutes for something one command answers. CGA and Hercules only: they
are 1bpp so the bytes are the pixels, where mode 12h is four planes behind
the Graphics Controller and not flat-readable at all. **`shot --rendered` is
the route that covers everything** — it asks the card what it rasterised
rather than asking memory what is in it, comes back in colour, and is taken
automatically on VGA. On a CGA desktop the two routes agree on 0 pixels of
128,000, which is a real cross-check and not a convenience.

An example worth copying, because it is the shape of question this is for.
"How many `int 13h` calls does one file load issue?" used to need
`make DISKCNT=1`, SPEC.md §18.94's published counter block, and a test package
on the floppy. It is now an `int` breakpoint on 13h against an **unmodified
shipped kernel** — but the *timing* of those calls still has to come off the
5150, because MartyPC will happily tell you a nine-sector read took 0.27 s.

---

## What 86Box is genuinely for

Narrower than it was, now that MartyPC covers the 8088 probe, the 6845 and
the sound cards: **a machine that is not an 8088** (the 286, 386, 486 and
Pentium targets), a **period bus** under a card rather than a modelled one,
and a second opinion on the video probe. `make xt`,
`xt-640`, `xt-cga`, `xt-hercules`, `xt-sound`, `286`, `286-sound`, `386sx`,
`386`, `386-sound`, `486`, `pentium`.

The last two are the *fast* end rather than the period end: a 486DX2/66 and a
Pentium 133, both with an SB16. 8086 real-mode code runs on them verbatim, so
what they answer is whether the constants sized against a 4.77 MHz 8088 still
behave two orders of magnitude up — typematic deadlines, the tracker's ring
refill, Arkanoid's frame pacing. Neither MartyPC nor QEMU can answer that:
one is a cycle-accurate 4.77 MHz 8088 by construction, and the other does not
model a clock speed at all.

It is not installed in the web container and needs BIOS ROMs, so those
targets do not run there. Nothing above them does.

Three 86Box-specific traps worth knowing before blaming the OS: it silently
clamps `mem_size` to the machine's maximum; a `wp://` prefix on an
`fdd_0N_fn` path mounts that floppy write-protected — which the OS then
faithfully reports as "Write protected", and which means `SYSTEM.CFG`
settings do not survive a reboot. No shipped profile carries it on either
floppy now, and every `make` target that launches 86Box strips it from both
keys first, because 86Box rewrites its own config on exit and has twice put
it back. The price is the one QEMU already charges: a session that changes a
Control Panel setting changes `build/os8088.img` and the change persists, so
rebuild it before a run whose starting state matters.

And an unrecognised `cpu_family` is **silently replaced** rather than
rejected, at that family's *default* speed. `cpu_family = pentium` is not a
name 86Box knows: it boots a P54C at 75MHz while the config still says 133.
The cheap check for any candidate machine or CPU is the one in CLAUDE.md —
launch 86Box on a throwaway copy of the config, `kill -TERM` it, and read the
file back, because 86Box rewrites it on exit with whatever it actually
accepted.
