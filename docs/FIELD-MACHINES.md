# FIELD-MACHINES.md — the real machines, and how to get a number off one

[PERFORMANCE.md](../PERFORMANCE.md) Part 9 records **sets**. This file records
the **machines** they came from, what is in each one, what a run on one
costs, and the rules for reading a result. Its sibling
[FIELD-NOTES.md](FIELD-NOTES.md) records what real hardware **found**; this
one records what real hardware **is**.

---

## Why the register is keyed on a fork name

An agent cannot tell one contributor from another; the repo can. What a
session can always see is its remote, so the register is keyed on the **full
fork name** of whoever holds the iron — `Elendilon/os8088`, not the bare
handle — because this file is written to be merged upstream, where a bare
handle names a person with no way to tell which tree they hold the hardware
for.

Handles, not email addresses: this repo is public (it ships releases and feeds
os8088.com), so a personal address in a tracked file is published, not
recorded.

---

## The rule that comes before any of the numbers

> **A result is not a field result because a human handed it to you.** Do not
> assume any figure came off the 5150 unless the run on it was actually
> discussed. **Ask.**

The owner of the 5150 also tests on **PCem** routinely, and Part 9's Set 4
came off **MartyPC**. Both model period hardware at period speed, so their
numbers are plausible in the same units as the iron's — which is exactly what
makes an unlabelled one dangerous. A QEMU figure announces itself by being
absurd; a PCem or MartyPC figure does not. This is PERFORMANCE.md Part 6
rule 8 — every figure carries its machine — applied to the conversation.

| you were given | what it is worth |
|---|---|
| a `.TXT` report the owner says came off the 5150 | a **field set**. Part 9, with its four provenance lines |
| a report from **PCem** or **MartyPC** | a good cross-check of *work* and a sanity check on *time* — a model of the machine, not the machine. **Name the emulator in Part 9**, as Set 4 does, or leave it out |
| a report from **QEMU** | instruction counts only, and only under `-icount`. Never microseconds |
| a screenshot, a description, "it looked fine" | evidence about behaviour, not about time |

---

## The IBM 5150 — `Elendilon/os8088`'s

**The machine this project is calibrated against.** Every measured number in
PERFORMANCE.md Part 2 came off it (Part 9 Sets 1 and 2).

| | |
|---|---|
| owner | **`Elendilon/os8088`** |
| machine | **IBM PC 5150**, Intel 8088 at 4.77 MHz |
| motherboard | the 64–256K board, **256 KB populated** |
| expansion | **AST SixPakPlus Rev 1** — the other **384 KB** (640 KB total, which is what `int 12h` answers) **and the clock**. §2.7's boot sector relocates to the top of whatever `int 12h` reports, so if this machine stops booting after a memory change, check the motherboard DIP switches first: a board the switches do not mention is a machine with plenty of RAM and a small answer, and the sector prints `RAM` and stops rather than loading a kernel over itself |
| clock | the SixPakPlus's **MM58167 at 2C0h** — §37.90's **rung 2**, the machine the ladder was written for and the only place rung 2 has ever been verified. An XT BIOS implements `int 1Ah` AH=00h/01h and nothing else, so rung 4 (BIOS) can never claim here. docs/FIELD-NOTES.md 14 is the one time it was not detected |
| video | **Hercules GB101 → IBM 5151** (mono TTL) **and IBM CGA → IBM 5153** (RGB), both cards on their own monitors. It boots on the **Hercules**, and that is **SW1-5/6**, not a property of the probe (below). The second column needs neither a card swap nor a build: Control Panel → Display switches it at run time (§39.11). **The GB101 has been lent to 5150 #2** for VGA-plus-mono runs (docs/FIELD-NOTES.md 24) — ask which box holds it before a run that needs it |
| floppy | **one** — a **Tandon TM100-2**, 360 KB 5.25" DD. There is no drive B. **But SW1 says there are two**, which is why this machine is the witness for §18.97's FDC probe: it showed a `Disk B` icon that could never mount. Confirmed on the iron: `claimed 2`, `ST3 = 21` before and after the recalibrate, `ST0 = 71`, `probe stop 03`, `verdict 0`, and the desktop comes up with drive A alone (§18.97.1 is the first, wrong round). **If the switches get corrected the probe stops running** and those rows read `claimed 1 / probe ran 0` — still right, no longer a test of anything — so say which way the switches are set when reporting a run |
| hard disk | **Seagate ST-225**, 20 MB MFM, on a **Seagate ST11M** controller, in the second bay |
| parallel | **one port, at 0x3BC**, on the Hercules GB101 (HGC-family cards carry an LPT). Confirmed with `tests/lptlink`. The DOS machine at the other end of the LapLink cable is at **0378**, which is why docs/plans/completed/NET-PLAN.md §1.4 scans instead of assuming: on a mono machine LPT1 *is* 3BC. The cable moves **3,741 B/s** sending, **3,538** receiving (PERFORMANCE.md Set 39) — a 360 KB image in **99 seconds** against the seven-step path below |
| serial | **one port, at 0x3F8 (COM1)**, with the mouse on it — `sysbench`'s §9.4.2 block reports `COM1 03F8, COM2 0000`. With one port there is no §9.5 contest and `[mou_need]` is 1 by default, so everything §9.5.1 says about a modem on the other port is untestable here; the Compaq Portable III is the two-port machine |
| sound | none |
| period | **intentionally, entirely period.** No Gotek, no XT-IDE, no flash. That is what makes its floppy and disk timings mean what they say — do not propose "just put a Gotek in it" as a way to shorten a turnaround |

### The clock reads 1980 after a power cycle, and that is correct

A **failed diode** on the SixPakPlus means it cannot hold time across a power
*off*: the backup battery tries to backfeed the ISA bus and sags to 0.6 V. So
a cold start comes up at **1980**, the §37.90 fallback. Across a *warm* start
the RTC keeps its 5 V and everything survives, year included. So on this
machine:

- reading and writing the clock both work, which is what makes it a valid
  rung-2 witness (§37.90's verification is "survives a warm boot");
- "the date is wrong after switching it on" is **expected**, and chasing it
  is chasing a diode;
- the time **not surviving a warm boot, or the year being dropped**, is a
  real defect.

### What it has measured

Outputs, not a specification — here so a run that disagrees is recognisable
as news. PERFORMANCE.md Part 2 is the authority for every figure; the sets
are cited so a reader can check which side of a fix a number sits on.

| | |
|---|---|
| CPU, derived independently from `MUL` and `DIV` | **4.64 and 4.68 MHz** against a nominal 4.7727 |
| PIT counts per tick | 65,542, then **65,536 exactly** on the second boot |
| instruction floor | **4.34 clocks per instruction byte** |
| any small `gfx_*` call, both cards | **~756 µs** fixed — 765.64 / 765.70 µs for `GFX_PIXEL` (§5.7; never quote 756 as a floor a design must beat) |
| one 8×8 glyph cell | 901 µs Hercules, 909 µs CGA |
| a solid fill | 177 µs/row + 0.28 µs/px Hercules; 182 + 0.33 CGA |
| framebuffer read-modify-write | 79.6 clocks/byte Hercules, 81.0 CGA — only ~7 of them the bus |
| floppy, `FILE_READ` throughput | **21,307 B/s** warm, **12,969** cold motor (Set 24). **It was 7,457 at Set 17 and 2,100 before that, and both are quoted all over this tree's history** — check which side of Sets 17 and 22/24 a figure comes from before comparing anything to it |
| floppy, one `int 13h` call | **199 ms** for one sector — one 300 RPM revolution — and **384 ms** for a 9-sector track (Sets 14/22). Cost disk work in **calls**, not sectors |
| the BIOS's own best, a track in one call | **11,570 B/s** (Set 24); os8088 is 1.84x this because a `dsk_xfer` run crosses the track boundary and the ROM's single call stops at EOT |
| floppy, open+read a one-sector file | 796–810 ms — Set 17, **not re-measured since §18.95's cache**; an upper bound |
| hard disk, `FILE_READ` | **74,553 B/s**; boot from it **3,240 ms** against 9,941 from the floppy (Set 24) |
| the kernel's own interrupts | 1–3% of a busy CPU |

### Two things the 5150 can test that nothing else here can

- **The MFM hard disk.** An ST-225 behind an **ST11M**, a controller *with a
  ROM* — §52.1's **rung 0**, the `int 13h` path, which is the whole of MFM
  support. Rung 1 (the IDE task file) is gated on `CPU_286` because an
  8088's `in ax, dx` is two 8-bit bus cycles and loses the drive's high byte,
  so **rung 0 is the only rung an 8088 can take and the 5150 is the only
  machine that can prove it.**
- **The clock ladder's rung 2** (§37.90) — field-verified here, and in no
  emulator.

### The hard disk is an os8088 install, and it is still not yours

The ST-225 **was a DOS 3.3 install and its owner deliberately overwrote it**:
§52.10's installer partitioned, formatted and populated it, C: is a FAT16
os8088 volume, and the machine boots from it (Set 24). What has not changed is
whose disk it is. The rules, from its owner:

0. **The hard disk is only mounted when a run asks for it, and the asking
   happens BEFORE the images are sent.** The driver is off by default (§51.3
   — a fresh image carries no `SYSTEM.CFG`), and its owner leaves it that way.
   A set that wants the hard-disk rows says so while the disks are being
   prepared: the operator ticks **Drivers → Hard Disk** in the Control Panel
   and **closes the panel** (§31.8 — closing is what writes `SYSTEM.CFG`)
   before the run. A set that does not ask gets `No volume at index 2 - no
   hard disk mounted`, which is the correct answer and not a fault.
1. **Do not format it. Do not partition it — unless the owner asks.** It has
   been pointed at once, by request, in writing; that is not a precedent.
2. **Do not leave anything behind.** Whatever a test writes, it removes.
3. **Do not delete anything you did not write.**

`sysbench`'s hard-disk block is built to that constraint: it mounts, walks
the FAT, reads one file and puts the current volume back — never writes,
creates or deletes. Which file it reads is asked of the volume (`sb_hdpick`
takes the biggest ordinary file in the root that fits the claim), because the
`COMMAND.COM` it used to read stopped existing when C: stopped being DOS.
Its write rows (§18.4) go to whatever volume is CURRENT, which is the
operator's choice at the keyboard. Both paths were verified under QEMU
before real hardware, including that the disk image is byte-for-byte
identical afterwards — repeat that check if the block changes.

---

## The IBM 5150 #2 — `Elendilon/os8088`'s, the "not period" one

The same CPU as the calibration machine and none of its discipline, and that
is what it is *for*: it carries a modern multi-function card, so it can answer
questions the period machine cannot — and its timings must never be quoted
as field numbers. It is also where the **VGA** in this register lives, so it
is the only real machine that can test §39's VGA paths and §39.11's
VGA-plus-mono pairing.

| | |
|---|---|
| owner | **`Elendilon/os8088`** |
| machine | **IBM PC 5150**, Intel 8088 at 4.77 MHz — stock, not turbo |
| memory | **640 KB**: 256 KB on the board, 384 KB on an ISA expansion card |
| keyboard | a generic AT keyboard through an **AT→XT adapter** — not a Model F. Worth knowing before any §9.6/§9.7 scancode result is read off it |
| video | **PVGA1A-JK, 256 KB** (Western Digital / Paradise) as primary, **plus the Hercules GB101 from 5150 #1** when a run needs two cards |
| the modern card | a **Picomem**: drive A and B (360 KB), a hard disk, an AdLib, a Sound Blaster, an NE2000 and EMS. `make PICOMEM=1` brings its sound up at attach (§34.10) |
| **audio questions go here** | 5150 #1 has no sound card. For a mixer question the answer is worth having: what §45.9's XT mode spends is CPU, and this is a stock 4.77 MHz 8088 — the Picomem replaces the *storage*, not the processor. What is the card's rather than a real one's is the DSP and its DMA, so a delivered-audio ratio taken here is a statement about that emulation; the CPU cost underneath it is genuine |
| period | **no.** Every storage timing here is the Picomem's — **nothing from here goes into PERFORMANCE.md Part 2** |

What it has found:

- **The Hercules was not detected** beside the VGA primary — §39.11.1.1. A
  Hercules whose configuration switch has never been written is an MDA, and
  the memory probe correctly rejects that signature; the probe now writes
  3BFh and asks again, and `sysbench`'s `mono probe` row (`[vid_hprobe]`)
  says which answer it took. The same shape had been seen on 86Box and filed
  as an emulator difference — it was not.
- **The screen recoloured after a few minutes — it was the card**: four
  socketed chips reseated after cleaning (docs/FIELD-NOTES.md 24.1.3). The
  trigger was drawing volume, not time or the disk. It also cost the tree a
  flawed instrument: the DAC readback row did not correlate with the screen
  and is now taken twice so it can say when it is lying (§39.21), which it
  did on its first outing (FIELD-NOTES 24.1.4).
- **The extended desktop runs on it** — VGA at (0,0) 640x480, Hercules at
  **(640, 20)** 720x348 (§39.19.3: the second monitor's top row is the
  desktop's band, not the screen's). PERFORMANCE.md Sets 62 and 63 are its
  first numbers, and the first VGA numbers off real hardware at all.
- **The Hercules destabilised too** once the desktop reached it — wavy, "out
  of phase". A Hercules has no palette, so two cards misbehaving on one
  machine points at the **supply or the bus** (FIELD-NOTES 24.2): 384 KB of
  ISA RAM, a Picomem and two video cards on a 63.5 W 5150 supply.

---

## The Toshiba T1100 Plus — `Elendilon/os8088`'s, the only 8086 here

Nearly the target and not quite: an 8086, so the same instruction set over a
**16-bit bus**. It is the one machine that separates "slow because the CPU is
slow" from "slow because every instruction byte is fetched one at a time".

| | |
|---|---|
| owner | **`Elendilon/os8088`** |
| CPU | **i80C86-2**, 7.16 MHz fast / 4.77 MHz slow, switchable from the keyboard; powers on in fast mode |
| RAM | 256 KB on the board + 384 KB expansion = 640 KB |
| video | CGA-compatible LCD |
| floppy | **two 720 KB 3.5" drives** — 300 RPM, 250 kbit/s |
| other | the modem expansion is fitted |
| disks | it takes the **720 KB** images: `make field`'s `build/cga720.img`, or `make combo720` |

What it has been worth (PERFORMANCE.md Set 11): `sysbench`'s `est CPU MHz`
came out at **7.12 / 7.29** against the manual's 7.16 — the only independent
check that estimator has ever had. Its instruction table shows the 16-bit bus
in plain sight: `mov r16,r16` is 8.69 clocks on the 5150 (2 bytes at 4.34
each) and **3.31** here, while `mul` and `div` scale by the clock alone. It
hits the same **2,161 B/s** floppy wall as the 5150 under `FLOPPY1=1`
(docs/FIELD-NOTES.md 7), which is what makes that wall two machines wide.

---

## The Compaq Portable III — `Elendilon/os8088`'s, the two-port machine

An AT-class BIOS, a 1.2 MB drive and a cross-wired serial card: it keeps the
disk work honest and it found §9.5's first two field bugs. A result from it
is not a result from the 5150, and Part 6 rule 8 applies between them.

| | |
|---|---|
| owner | **`Elendilon/os8088`** |
| CPU | 80286 (tier 1) |
| video | the plasma panel, which §39.1's probe finds as CGA 640x200 |
| floppy | **one 1.2 MB 5.25"**, which reads the 360 KB field disks — send `make combo` / `make comscan`'s 360 KB images, not the 1.44 MB ones |
| serial | **two ports**, 3F8 and 2F8. A **1200 baud modem on COM1** (silent so far), and the mouse on **2F8 driving IRQ4** — the cross-wiring §9.5.2 and §9.5.2.1 exist for. `sysbench` reports it as `winning row 2`, `winning IRQ 0010`; `make test MOUSEPORT=com2irq4` is this machine in QEMU |
| everything else | **not recorded, because it has not been measured.** Ask, or read it off `comscan` |

**The floppy.** A 1.2 MB drive turns at **360 RPM — 167 ms a revolution —
even with 360 KB media in it**, where every other drive here is 300 RPM /
200 ms. Read its reports with that in mind: `int 13h 1 sector` comes out at
0.989 of a revolution at 360 RPM and an impossible 0.82 at 300, which is how
the drive identifies itself from the report alone. It is the machine that
shows a *latency* cost for what it is (PERFORMANCE.md Set 19: its BIOS streams
a track at 22,368 B/s against the 5150's 11,984, and the gap to its own
ceiling is 2.03x against 1.49x for identical code issuing identical calls,
because a 4.77 MHz 8088 spends 52.5 ms in the ROM's head-settle loop where a
286 spends a fraction — Set 37). Its own throughput figures (Set 18) are
Set 17-era and it has not been re-run since §18.95's cache, so a comparison
against the 5150's current 21,307 is historical.

Three things to know before running it:

- **360 KB media in a 1.2 MB drive is marginal by construction** — 48 tpi
  tracks under a 96 tpi head — so a disk it refuses is a disk to rewrite
  before it is a bug: it refused one build with `os8088: disk error` and then
  booted the same code from a fresh disk (Set 18). A disk it has *written*
  may be unreadable in a real 360 KB drive afterwards; the owner keeps those
  disks separate. Which images write: the combo and field disks do (the
  reports land on them, and the Control Panel writes `SYSTEM.CFG` on close);
  **`comscan` never writes to a disk at all.**
- **Its cold read is 2.4x its warm one** (3.63 s against 1.48), where the
  5150's are within 5%: the AT BIOS identifying the media by trying data
  rates, paid once. A cold-motor row from here is not comparable to one from
  an XT.
- **`mouse found 0` with both ports present is what a machine with nothing
  plugged in says.** The owner has one serial mouse and it normally lives on
  the 5150; Set 18's `mouse found 0` from here was exactly that. Ask what
  was connected before diagnosing a mouse — the same error as reading `No
  volume at index 2` as a missing hard disk.

**What it found** (docs/FIELD-NOTES.md 13, both halves confirmed fixed on the
machine). `tests/comscan` (docs/TESTING.md) showed the mouse at 0x2F8 with
its card driving IRQ4 — 81 packets, zero protocol violations to a polled
reader, and a kernel that derived IRQ3 from the base and never heard it. §9.5.2
is the fix: every hooked line services every live port. The second half was
`mou_lockon` retiring the losing port with an 8259 mask taken from its
*base*, which on this machine masked the mouse's own line after eight
packets; §9.5.2.1 takes the mask from the line the winning packets arrived
on. The same report carries the first real-hardware confirmation of §9.4.1's
identify burst: `packets needed COM1 8` against `packets needed COM2 1`.

**It owes one measurement, and it is the only machine that can take it** —
see M1 under *Standing requests* below.

The modem on COM1 stayed silent throughout, so none of §9.5.1's
Hayes-result-code defences have been exercised on real hardware; they remain
QEMU-verified only.

---

## The Packard Bell Victory 286 — `Elendilon/os8088`'s

| | |
|---|---|
| owner | **`Elendilon/os8088`** |
| board | Packard Bell Victory (theretroweb.com/motherboards/s/packard-bell-victory) |
| CPU | **16 MHz AMD 286** |
| RAM | 4 MB, 100 ns |
| video | onboard **Paradise PVGA1A**, 256 KB — a **VGA** |
| clock | Dallas DS1287/DS12887, potted |
| floppy | 1.44 MB drive A, 1.2 MB drive B — **the one machine here with no 360 KB drive** |
| quirk | it sometimes decides to boot in mono; the Display page corrects that from inside the running OS |

**Its disk is `make combo144`.** `make combo` is 360 KB and cannot be read
here. `combo144.img` carries the full payload — every application,
`BIGFILE.DAT` (without which `sysbench` skips the cache-capacity sweep and the
DOS read-rate cross-check), `BEVERLY.MOD` and `README.TXT` — and it is built
from the full lists, not from `$(COMBOARGS)`, which is the 360 KB disk's
subtractive list and would silently drop seven packages and a driver on a
volume with room for them.

**Its first set was thrown away, and the trap is worth knowing.** It was run
from a `VIDEO=cga` field disk, so a VGA machine spent the whole suite driving
the CGA framebuffer path — a fourth combination the project does not support
— with reports self-identifying as `CGA 640x200 mono` in files hand-renamed
`GFXVGA.TXT`. `combo.img`/`combo144.img` carry the ordinary shipped kernel
with no adapter forced, so the probe answers VGA here and the trap is not in
the default ask any more. Its two 8088-only derived rows (`est CPU MHz x100`
read **8879**, `shl clk/bit x100` read **29**) are what validated
`sysbench`'s book-per-tier (PERFORMANCE.md Part 8.1): re-derived against the
286 book, the two independent estimates are **15.83 and 15.86 MHz**, and a
re-run prints them directly.

**It found a real bug, the second machine to find the same one**
(docs/FIELD-NOTES.md 21, §18.97.2, §18.97.3). It came up with **no Drive B**
and a 1.2 MB drive plainly present: §18.97's probe removed the row on "ST3's
TRK0 clear before and after a RECALIBRATE", and this drive answers `ST3 =
0021` on both reads — the same ST3 byte as the 5150's genuinely absent drive
— with `ST0 = 0021` where the 5150 says `0071`. ST0 separates them (normal
termination, no Equipment Check, is the FDC saying the head reached track 0),
so ST0 is consulted before any removal and only ever to answer *keep*; and
the verdict is acted on **on tier 0 only**, because on an AT the drive count
comes out of CMOS setup and is somebody's decision, where on a 5150 it is a
factory-default DIP switch. Confirmed fixed on the machine. 1.2 MB media was
never the problem — §18.2's BPB rules admit it — what was missing was the
drive's icon.

`mouse found 0` here is the rule above: nothing was plugged in.

---

## PCem and MartyPC — the other places results come from

Not machines, but reports come off them and are easy to mistake for field
sets. PCem is where the 5150's owner tests routinely; MartyPC (`make marty`,
docs/MARTYPC-DEBUG.md) is a cycle-accurate 5150 and produced Set 4. Both put
their figures in the right units, so nothing about them looks wrong.

- **PCem runs 10–20% fast** (PERFORMANCE.md Set 11, row for row against the
  5150). A PCem timing is a **lower bound on the real cost**, never an upper
  one: a stall PCem shows at 990 ms is about 1.2 s on the iron, and "it keeps
  up on PCem" is not yet "it keeps up". Work counts are unaffected.
- **MartyPC lands within 0–4% on 45 of 47 `gfxbench` rows** (Set 11) — it
  models the prefetch queue and bus contention, which is where this project's
  costs live. Its floppy turns (Set 35), but its BIOS returns what its author
  believed the hardware returns, so anything with a disk in it is still the
  5150's.
- Either is the right tool for *reproducing* something the 5150 showed
  without the seven-step trip, and for anything the 5150 must not be pointed
  at — a format, a partition, anything that writes.
- Their figures go into Part 9 **named**, or not at all. Set 4 is the worked
  example: it says MartyPC on its machine line and carries its own
  calibration, which is what makes it comparable to the next run.

**A long log is only comparable to another long log if it says how fast the
machine was.** A run must time a fixed, known quantity of work at each end
and print it — Set 4's `CAL` lines — and if the two brackets disagree, the
machine moved underneath the measurement and the rows between are suspect.

### Memory dumps

`make marty` runs MartyPC in the container with a debug server, so a dump of
a build you can run yourself is a command rather than a favour:

    python3 tools/os88marty.py <addr> verify

dumps `KERNEL_SEG`, diffs it against `build/kernel.bin` and prints the
differing runs. **Ask nobody for a dump of a build you can run yourself.**
What still needs the owner is a dump of a machine whose behaviour differs
from the emulator's — and a dump taken in the container proves what the code
does, not what the 5150 does with it. MartyPC's own debugger dumps the full
1 MB and the code segment from its GUI when the owner is running it
interactively.

A dump is **self-validating**: the image lands at `KERNEL_SEG`, so linear
`0x600` onward is `build/kernel.bin` byte for byte apart from writable state.
That proves the machine was running *the build you sent* (the one hand-taken
dump differed in 1,353 of 71,112 bytes, all of it `.text` data with real
initialisers), gives you every kernel variable at its listing offset with no
instrumentation, and the differing bytes are themselves the answer.
`boot_ticks` at `0060:000C` is the cheapest check that you are reading the
right image at the right base: `FFFF` in the file, the elapsed count in the
dump. Three rules:

- **Say what state you want it taken in**, because the interesting state is
  usually the untouched one — the mouse dump had to be taken at the desktop
  with the mouse deliberately not moved.
- **Re-derive every offset from a listing of the exact commit**: `nasm … -l`
  and then `0x600 + offset`. Anything before `font.inc` moves them all.
- **Find a value that pins the reading before you trust any of it.**
  `mouse_x`/`mouse_y` sitting on `[vid_w]/2, [vid_h]/2` said "Hercules, and
  nothing has moved yet" in one word each.

A dump is evidence about *logic*, never about time.

---

## How to take a set on the 5150

### `make combo` — one disk, and it is the default ask

```sh
make combo          # -> build/combo.img, 360KB bootable
make combo720       # the same, full payload, for a 720KB drive
make combo144       # the same, full payload, for a 1.44MB drive (NOT the 5150)
```

**Build and send this for a field or bench request unless something below
says otherwise.** The system, the applications, every game and all the
benchmarks on one bootable 360 KB floppy, with room left for the reports and
`SYSTEM.CFG`.

**"The applications" is a maintained list.** The packages outgrew 354
clusters, so `COMBO_DROP` in the Makefile names what comes off the 360 KB
disk — currently **Artful, ModPlug, TeXPad, Tracker, Recorder, Sheet and
Chart** — and `COMBO_DRVDROP` takes **`ETHER.DRV`** off with them, because the
machine this disk is for has no NIC and it is the largest file after the
kernel. A combo disk therefore cannot bring the Ethernet stack up on a
machine that has a card: `make ethertest` is that disk. Ticking Ethernet in
the Drivers page on this disk reports `Not on the system disk`, which is what
that page says for any driver that is not there. `combo720` and `combo144`
drop nothing (713 and 2,847 clusters). When the 360 KB disk stops fitting
again, `os88disk.py` refuses it with `packages need N clusters; disk holds
354` and another name goes in `COMBO_DROP`.

Three things are left off the 360 KB disk by that arithmetic:

| | | why |
|---|---|---|
| `MEDIA/BEVERLY.MOD` | 42 cl lz4-packed (114 plain) | data rather than software: Tracker and ModPlug launch with nothing to open. `apps360.img` carries it in `MEDIA/` (§20.13.5) — swap that disk in when the module is the point |
| `BIGFILE.DAT` | 104 cl | sysbench's cache-capacity sweep and the DOS read-rate cross-check; sysbench says so and skips those rows. It is on the `make field` disks |
| `README.TXT` | 9 cl | the manual, on a disk that is for running |

**One image and not one per card** (§39.19): both cards live in the 5150
permanently, the machine boots on whichever §39.1 picks, and the Control
Panel's Display page switches the primary to the other — or extends the
desktop across both — with no rebuild. `combo.img` is the ordinary shipped
kernel with no adapter forced, so there is no forced-adapter kernel in the
request at all, which is what put the Packard Bell down the CGA path.
`gfxbench` names its report after the adapter it *found*, so both sets land
on the one disk without colliding.

**Which card it boots on is a DIP switch — SW1-5/6.** `vid_detect`'s last
rung is `int 11h` bits 5:4 (`11b` = 80×25 mono → `VID_HERC`, anything else
→ `VID_CGA`), and on a 5150 that field *is* the switch pair. The register's
machine is set to mono, so it comes up Hercules — a setting, not a
discovery. Measured on MartyPC with the same kernel and disk: the two-card
profile reads bits 5:4 = `0x20` and boots CGA with `avail = 0x06`, the
mono-switched one reads `0x30` and boots Hercules. This is the same SW1 byte
§18.97 argues with, treated oppositely on purpose: bits 7:6 claim what is
*plugged in*, which the FDC can check, so it is probed; bits 5:4 say which
display the owner wants primary, which nothing can verify, so it is obeyed.
The extended desktop is off by default (§39.19.1) — one visit to the Display
page.

**The benchmarks are in the ROOT of the boot disk.** With one floppy drive, a
benchmark on a separate data floppy is a disk swap mid-session, and on this
machine a disk swap is a walk to another room. **That is a rule for every
field harness and not a fact about this target** — docs/TESTING.md carries it
where a harness author will meet it.

**It may not be write-protected.** The reports are the point, and a protected
disk answers int 13h status 03h, reported as `Write protected`.

### `make field` — the disks that answer a question `combo.img` cannot

```sh
make field          # -> herc, cga, cga720, flop1 and cqdiag
```

The narrow cases. Every one is `DISKCNT=1` (§18.94.1), and three carry a
second knob:

| disk | the question only it answers |
|---|---|
| `cga720.img` | the **Toshiba T1100 Plus**: `VIDEO=cga` on 720 KB media. `combo720.img` is the unforced alternative |
| `flop1.img` | `FLOPPY1=1`, one sector per `int 13h` — the A/B for docs/FIELD-NOTES.md 7. A knob kernel, so a disk of its own |
| `cqdiag.img` | `BOOTDIAG=1`: the boot sector prints int 13h's status as two hex digits instead of `DSK` — one boot instead of a bisect on a machine that will not start |
| `herc.img` / `cga.img` | a run that must pin the adapter at BOOT, or a comparison against an older set taken on them. `cga.img`'s kernel is built in `build/cgak/`, never in `build/`, so a forced kernel can never reach the shipped tree |

`bigfile.dat` is 104 KB because `SB_RAH_WMAX` is 12: the deepest byte a
floppy sweep touches is 11 × 9216 + 1024 = 102,400. Raise `SB_RAH_WMAX` and
the file has to grow with it; the report distinguishes *the file ran out*
from *a read refused*, so a sweep bounded by the disk can never read as a
cliff bounded by the cache. (The kernel's `DSK_RAH_RUNS` is 14 now, a
ceiling that §18.95.5 sizes down from the free run, so on a 640 KB machine
the sweep sees no cliff and says so.)

All of these names are 8.3-short on purpose: DOS 3.3 has no tab completion
and they get typed by hand into `dskimage`. **None of them is ever
committed** — `build/` is gitignored outright (§16) — they are built on
demand and **sent**.

### Then, on the machine

- Boot the image, open **Disk A**, launch `GFXBENCH.O88`. **`R`** runs it,
  **`S`** saves the report, named after the adapter it **found** —
  `GFXHERC.TXT` / `GFXCGA.TXT` / `GFXVGA.TXT`.
- **For the second card, do not swap disks: switch the display.** Control
  Panel ▸ **Display** ▸ pick the other adapter ▸ **Set Primary** (§31.10),
  then run `GFXBENCH.O88` again; it re-reads the geometry from `OSAPI_VIDEO`
  at run time.
- **On an EXTENDED desktop, do not even switch: drag the window across.**
  `gfxbench` names the card its **sandbox** is on (§39.19), so `R`, drag onto
  the other monitor, `R` again is a set from both cards in one launch. **Read
  the `sandbox straddles` row before comparing two reports**: a 1 means the
  window crossed the seam and the primitives were being split, refused or
  drawn per cell (§39.14.6/§39.14.7), which is not the same measurement as a
  run that reads 0.
- Then `SYSBENCH.O88`, to `SYSBENCH.TXT`. **Once, not per card** — none of
  its rows is a question about the adapter.
- `gfxbench` is about fifteen seconds. `sysbench` is about a minute on a
  floppy-only machine and **two or more with the hard disk mounted** — its
  read row calibrates itself off the first read and then runs for about six
  seconds, and it prints the iteration count it chose. **The machine is
  frozen while either runs, by design**; the bottom line says which block it
  is on.
- Bring the `.TXT` files back and paste them into Part 9 with the four
  provenance lines it asks for.

### The path an image takes to get there

This is the real cost of a field run, and why "just rebuild and try again" is
not a thing to ask for casually. The 5150 has no modern storage by design, so
an image travels:

1. Fetch the SD card from the **writer machine** — 5150 #2, whose Picomem
   boots from `.vhd` images on SD. The Picomem is on *that* machine, never on
   the calibration 5150.
2. Mount the VHD on the primary system.
3. Copy the `.img` into the VHD.
4. Unmount the VHD, then the SD card.
5. SD card back into the writer machine; boot it to **DOS 3.3**.
6. `dskimage` writes the image to a real 360 KB disk. **It has to be a real
   360 KB drive** — a 360 KB disk written in a 1.2 MB drive is not reliably
   readable in one.
7. Carry the disk to the 5150 and boot.

Two consequences. **Batch the questions**: the marginal cost of one more
benchmark row is nothing and the marginal cost of one more *trip* is the seven
steps above. And **make the build deterministic before you hand it over**:
quote a commit and build the image from a clean checkout of it, so a disk
that behaves oddly is a finding rather than a question about which build it
was. The LapLink cable (Set 39) is the shortcut when the DOS machine is at
the other end of it.

---

## What to ask the 5150's owner for, and what not to

**Worth a field run** — nothing else can answer these:

- **Time.** QEMU is exact about how much work the guest does and useless
  about how long it takes (PERFORMANCE.md Part 4). Anything whose answer is
  in microseconds is a field question.
- **The three defects no emulator can show** (Part 3): a visible redraw, a
  double-draw flash, and input overrun. Judged by a person watching the
  glass; no screenshot substitutes.
- **A model this repo has been spending without measuring.** Part 9's "what
  the next set is being asked" table is the current list.
- **The rungs no emulator has**: §37.90's MM58167 and RP5C01 clock tiers, and
  §39.1's video detection on real cards.
- **What a real peripheral does on a real card.** §9.4.1 turns on whether a
  real serial mouse answers a DTR/RTS raise with `'M'`; QEMU's `msmouse`
  ignores DTR and MartyPC's is a model of one. `sysbench`'s mouse block is a
  **state dump rather than a measurement** for exactly this reason, and it
  is the shape to copy: when the field question is about logic, publish the
  state and let the machine print it.

**Not worth a field run** — the container answers these, faster and
reproducibly:

- **Counts.** Fills, glyphs, walk iterations are exact under QEMU and
  MartyPC; instrument a counter and read it.
- **Instruction counts.** `-icount shift=3,sleep=off` is deterministic to ±1
  and machine-independent.
- **Whether the pixels are right.** A byte-for-byte screendump comparison on
  `VIDEO=cga`, plus `tools/hercshot.py` for Hercules, settles rendering
  without leaving the container.

**Send it a question about time or about what a human sees; keep every
question about work.**

### Standing requests, unanswered

Newest last, each with what to boot, what to read, and what the number
settles. A request that has been answered moves into PERFORMANCE.md Part 9
with its four provenance lines and comes off this list.

**M1 — the identify window against a modem (§9.4.5, §9.5.1). The Compaq
Portable III, and only it.** §9.4.5 closes the mouse identify window as soon
as a port has answered like a mouse and gone quiet — `mouse_init` 1,200 ms
down to 596 — and that window's other job is draining a modem's banner before
the ISR reads it as packet headers. A 1200 baud modem on COM1 with the mouse
on the other port is exactly the case, and no emulator here has a modem. The
run is two boots of `BOOTPROF=1` disks, `MOUIDSLOW=1` against the default,
checking that the mouse still comes up and nothing phantom arrives from the
modem side; the numbers are on the screen. Until it is done, §9.4.5's fences
are an argument.

**W1 — WEAVE's VM speed and its canvas frame rate (WEAVE-SPEC §4.12, §14;
docs/plans/completed/WEAVE-PLAN.md §4.2).**

*What it settles.* WEAVE-SPEC §4.12 contracts the bytecode VM at
**10,000–30,000 ops/s on the 4.77 MHz target**, and that figure is design
arithmetic. WEAVE-SPEC §4.10's 1,536-op slice cap is 51–154 ms *because* of
it, and WEAVE-SPEC §4.11.1's 64-op `ontick` bound is "under 10% of the VM contract"
*because* of it; if the reading lands below ~10k, both shrink. WEAVE-SPEC
§14's canvas rows are the same question about the picture — the call count
is settled (MartyPC is exact about work: 1.00–1.06 gfx calls a frame), the
milliseconds are not.

*What to boot.* Two 360 KB disks built from the commit the request quotes, no
knobs: the plain system floppy in A: and **`make weavedisk`**'s
`build/weave360.img` in B:. It carries `WEAVE.O88`, `WEAVE.OVL`, `WEAVE.WSM`
and the bundles in one folder, and they have to be together or the canvas
refuses at open (WEAVE-SPEC §10.3).

*What to do — four gestures.*

1. Open drive B, double-click **`FORM.WAB`**, type a name into the field and
   press the button a few times. Then **Bundle → Bundle Info** and read the
   line in the content area; it ends `; WVM <n> ops/s`. **That `n` is the
   number.** (About carries it too, but About is a toast and takes itself
   down in about three seconds.)
2. `n` reads `-` until a full one-second window of EXHAUSTED slices has
   closed (WEAVE-SPEC §4.12). If it does, open **`SHEET.WAB`** instead, put
   `=SUM(A1:A20)` in a cell, press Enter, and read Bundle Info again.
3. Open **`PONG.WAB`**, click **Serve**, and **watch it**: does the ball move
   smoothly, and does anything flicker — the ball vanishing for a frame, the
   paddles blinking, the field tearing. Judged by a person looking at the
   glass.
4. Bundle Info again, on PONG, after the rally. The line then ends `; canvas
   <frames> frames <blits> blits <n> ovf`. **Report all three.**
   `blits/frames` is WEAVE-SPEC §14's row; `ovf` is the staging ring's
   dropped-record count and must be **0**.

*What NOT to ask for.* Nothing about how many calls a frame makes — settled
in the container. Only the ops/s figure, the two things a person sees, and
the three canvas counters.

### Handing over a build

State the **commit** and hand over the images rather than a branch name — a
branch moves. Build them from a clean checkout of the commit you quote, and
**quote the knobs each image carries**, or the floppy holds something the
source no longer says. The Makefile owns which knobs those are: every `make
field` kernel is `DISKCNT=1`, three of the five carry a second knob (above),
and `combo.img` is the plainest of the lot — the shipped kernel, no knob at
all — which is part of why it is the default ask.

**A benchmark kernel is not bound by `KERN_BUDGET`.** Every machine in this
register has 640 KB, so the only ceiling on an instrument is the RAM in the
box. What to watch is **parity**, and `make field` runs `tools/fieldsize.py`
to watch it:

- **`boot ticks` and every heap row are measurements of the kernel that is
  running.** The kernel is read off the floppy a sector at a time and the
  heap starts where it ends, so both move when the image does.
- **Nothing else does** — drawing, CPU, RAM bandwidth and the floppy's
  bytes/second are measured against the machine.
- **The unit is `KIMG_PARA`'s 512-byte rung**, not the byte: two kernels in
  the same rung have an identical memory map and sector count, so their rows
  compare exactly. `fieldsize.py` says which case you are in.

Growing past a rung is allowed. It just has to be **known about** rather than
discovered later in a number that moved for no visible reason.

### Delivering images in `Elendilon/os8088`

The ordinary case, as distinct from a field run, is CLAUDE.md's "Working in
this fork" — the fork owner's standing preference, not a property of the
project. The part that is this register's: **the 360 KB set is THREE disks**
— `build/os8088-360.img`, `build/apps360.img` and `build/media360.img` — the
geometry because it is what the register's machines read. Since the disks
are lz4-packed (§20.13.5) `apps360.img` carries `BEVERLY.MOD` itself and the
media disk is a second copy of it; the owner's standing rule is still all
three. "Send" means attach the files: a path into a session's `build/` is in
a container the owner cannot reach.
