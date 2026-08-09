# FIELD-MACHINES.md — the real machines, and how to get a number off one

[PERFORMANCE.md](../PERFORMANCE.md) Part 9 records **sets**. This file records
the **machines** they came from, and how to reach the person holding one.

They are different things, and keeping them apart matters: Part 6 rule 8 says
every figure carries its machine, and Part 9's sets duly say "IBM PC 5150,
8088 at 4.77 MHz, 640KB" — but not *whose*, not what else is in the slots, and
not how a build gets from this repo onto a floppy in that machine. That last
one is the whole cost of a field run and it has never been written down.

Its sibling is [FIELD-NOTES.md](FIELD-NOTES.md), which records what real
hardware **found**. This one records what real hardware **is**.

---

## Why the register is in the repo rather than in someone's head

**An agent cannot tell one contributor from another, and the repo can.**

A session is told which account it is running as and forgets it when the
session ends; there is no memory between them. Nothing in a commit says "this
contributor owns a 5150" — the git history here is 120-odd commits authored
`Claude <noreply@anthropic.com>` plus two humans, and an author line is not a
hardware inventory. So an agent asked to "check this on the real machine" has
no way to know whether there *is* one, which one, or who to ask.

What is durable is the **fork**. `Elendilon/os8088` is visible to every
session that works in it — it is the remote, the branch name and the owner —
and it is visible to every human reading the repo. So the register is keyed on
the **full fork name** of whoever owns the iron — `Elendilon/os8088`, not
`Elendilon` — and it lives here rather than in a conversation.

The full name and not the bare handle, because **this file is written to be
merged upstream**. In the fork, "owner: `Elendilon`" is unambiguous and a
reader can work out the rest; in the parent repository, with contributors from
several forks, a bare handle names a person with no way to tell which tree
they hold the hardware for. The fork name survives the merge with its meaning
intact, and it is still exactly what an agent can see from its remote.

Handles, not email addresses: this repo is public (it ships releases and feeds
os8088.com), and a personal address in a tracked file is published, not
recorded. If you want a contact route in here, say so and put in the one you
want published.

---

## The rule that comes before any of the numbers

> **A result is not a field result because a human handed it to you.** Do not
> assume any figure came off the 5150 unless the run on it was actually
> discussed. **Ask.**

The owner of the 5150 also tests on **PCem** routinely, and Part 9's Set 4
came off **MartyPC** — and neither is QEMU, so this is not the usual
"emulators lie" caution. Both model period hardware at period speed, which
makes their numbers *plausible in the same units* as the iron's, and that is
exactly what makes an unlabelled one dangerous: a QEMU figure announces
itself by being absurd, and a PCem or MartyPC figure does not.

This is PERFORMANCE.md Part 6 rule 8 — every figure carries its machine —
applied to the conversation rather than to the document. A number whose
provenance you assumed is a number you will write into Part 9 under the wrong
heading, and the next reader has no way to catch it.

| you were given | what it is worth |
|---|---|
| a `.TXT` report the owner says came off the 5150 | a **field set**. Part 9, with its four provenance lines |
| a report from **PCem** or **MartyPC** | a good cross-check of *work* and a reasonable sanity check on *time* — but a model of the machine, not the machine. **PCem runs ~20% fast**, so its timings are a floor. **Name the emulator in Part 9**, as Set 4 does, or leave it out |
| a report from **QEMU** | instruction counts only, and only under `-icount`. Never microseconds |
| a screenshot, a description, "it looked fine" | evidence about behaviour, not about time |

---

## The IBM 5150 — `Elendilon/os8088`'s

**The machine this project is calibrated against.** Every measured number in
PERFORMANCE.md Part 2 came off it (Part 9 Sets 1 and 2), and SPEC.md quotes it
by name in a dozen places.

| | |
|---|---|
| owner | **`Elendilon/os8088`** |
| machine | **IBM PC 5150**, Intel 8088 at 4.77 MHz |
| motherboard | the 64–256K board, **256 KB populated** |
| expansion | **AST SixPakPlus Rev 1** — carries the other **384 KB** (256 + 384 = the 640 KB every set reports) **and the clock**. That 640 is what `int 12h` answers, and since SPEC.md §2.7 the boot sector relocates itself to the top of it — so if this machine ever stops booting after a memory change, the first thing to check is the motherboard DIP switches, which are where an XT's RAM count comes from. A board the switches do not mention is a machine with plenty of RAM and a small answer, and the sector prints `RAM` and stops rather than loading a kernel over itself |
| clock | the SixPakPlus's **MM58167 at 2C0h** — §37.90's **rung 2**, and the machine the whole ladder was written for: an XT BIOS implements `int 1Ah` AH=00h/01h and nothing else, so this BIOS knows nothing about a clock sitting in its own backplane. It is also what keeps rung 3 off a SixPakPlus — rung 3 is claimed only when the BIOS *can* read the clock, and here it cannot |
| video | **Hercules GB101 → IBM 5151** (mono TTL) **and IBM CGA, new style → IBM 5153** (RGB). **Both cards and both monitors, always, in the machine.** So the second column costs a *build*, never a card swap — but the probe (§39.1) finds the Hercules first, so the CGA needs a kernel told to ignore it |
| floppy | **one** — a **Tandon TM100-2**, 360 KB 5.25" DD. There is no drive B |
| hard disk | **Seagate ST-225**, 20 MB MFM, on a **Seagate ST11M** controller, in the second bay |
| serial | **one port, at 0x3F8 (COM1)**, with the mouse on it — `sysbench`'s SPEC.md §9.4.2 block reports `COM1 03F8, COM2 0000`. Worth having written down, because it decides which half of a two-sided mouse change this machine can witness: with one port there is no §9.5 contest, `[mou_need]` is 1 by default, and everything §9.5.1 says about a modem on the other port is untestable here. The **Compaq Portable III** below is the two-port machine |
| sound | none |
| period | **intentionally, entirely period. No modern hardware is attached to the 5150.** No Gotek, no XT-IDE, no flash. That is the property that makes its floppy and disk timings mean what they say, and it is a deliberate constraint rather than an accident — do not propose "just put a Gotek in it" as a way to shorten a turnaround |

### The clock reads 1980 after a power cycle, and that is correct

A **failed diode** on the SixPakPlus means it cannot hold time across a power
*off*: the backup battery ends up trying to backfeed the whole ISA bus and
sags to 0.6 V. So a cold start comes up at **1980**, the §37.90 fallback, and
that is the hardware behaving as this hardware behaves.

**It is not a clock bug, and it is not the fallback misfiring.** Across a
*warm* start the SixPak's RTC still has its 5 V, and everything survives —
the year included. So on this machine:

- **reading and writing the clock both fully work**, which is what makes it a
  valid rung-2 witness (§37.90's verification is explicitly "survives a warm
  boot");
- a report of "the date is wrong after switching it on" from here is
  **expected**, and chasing it is chasing a diode;
- a report of the time **not surviving a warm boot, or the year being
  dropped**, is a real defect and worth acting on.

### What it has measured

Not a specification — these are outputs, and they are here so a run that
disagrees with them is recognisable as news rather than noise.

| | |
|---|---|
| CPU, derived independently from `MUL` and `DIV` | **4.64 and 4.68 MHz** against a nominal 4.7727 |
| PIT counts per tick | 65,542, then **65,536 exactly** on the second boot |
| instruction floor | **4.34 clocks per instruction byte** |
| any small `gfx_*` call, both cards | **~756 µs** fixed — 765.64 / 765.70 µs for `GFX_PIXEL`, 0.008% apart |
| one 8×8 glyph cell | 901 µs Hercules, 909 µs CGA |
| a solid fill | 177 µs/row + 0.28 µs/px Hercules; 182 + 0.33 CGA |
| framebuffer read-modify-write | 79.6 clocks/byte Hercules, 81.0 CGA — only ~7 of them the bus |
| floppy, sector inside a coalesced run | **65 ms**, **7,457 bytes/second** (Set 17; it was 238 ms / 2,100 B/s before SPEC.md §18.91's `AL` bug was fixed, and that older pair is quoted all over this tree's history) |
| floppy, one `int 13h` call | **~400 ms** — 1–2 of the 200 ms revolutions, near enough whatever it moves |
| floppy, isolated single-sector access (LBA 0, a lone directory sector) | **~150–200 ms** modelled, seek + latency + settle — **not measured** |
| floppy, open+read a one-sector file | 810 ms |
| the kernel's own interrupts | 1–3% of a busy CPU |

Two of those are the load-bearing ones. **756 µs** is the per-call floor
(SPEC.md §5.7), and on the disk it is **the `int 13h` call, not the sector** —
a call costs one to two revolutions almost regardless of what it moves, which
is why §18.94's counters report both and why a mount is quoted as
`12 sectors / 4 calls`. A 116 KB module load was 57 seconds and is about 15.

**Check which side of the `AL` fix a disk figure comes from before comparing
anything to it.** PERFORMANCE.md Part 2 has the three quantities and why one
number was doing all three.

### Two things the 5150 can test that nothing else here can

- **The MFM hard disk.** SPEC.md §52's driver has never run on real spinning
  MFM — though **the volume mounts**: that much has been tried, and it worked
  first time. This is an ST-225 behind an **ST11M**, which is a controller
  *with a ROM* — and that is §52's **rung 0**, the int 13h path, which is also the
  whole of MFM support. Rung 1 (the IDE task file) is gated on `CPU_286` for
  an arithmetic reason — an 8088's `in ax, dx` is two 8-bit bus cycles at the
  same port, so the drive's high byte is lost — so **rung 0 is the only rung
  an 8088 can ever take, and the 5150 is the only machine that can prove it.**
  Everything §52 says about partitioning, formatting, the capacity-table
  cluster sizes and the `SYSTEM.CFG` automount is, on real MFM, untested.
- **The clock ladder's rung 2.** Already field-verified here (§37.90), and
  the only rung no emulator can reach.

### The hard disk is a real DOS 3.3 install, and it is not yours

The rules for anything that touches C: on this machine, from its owner:

0. **The hard disk is only mounted when a run asks for it, and the asking
   happens BEFORE the images are sent.** The driver is off by default
   (SPEC.md §51.3 — a freshly built image carries no `SYSTEM.CFG`, so nothing
   is loaded and nothing is probed), and its owner leaves it that way. So a
   set that wants the hard-disk rows has to say so **while the disks are
   being prepared**, not after they arrive: the operator ticks
   **Drivers → Hard Disk** in the Control Panel and **closes the panel**
   (§31.8 — closing is what writes `SYSTEM.CFG`) before the run. A set that
   does not ask gets `No volume at index 2 - no hard disk mounted`, which is
   the correct answer and not a fault. **If a change makes those rows
   necessary, ask in the message that carries the images.**
1. **Do not format it. Do not partition it.** §52's disk tool is exactly the
   thing that must not be pointed at it.
2. **Do not leave anything behind.** Whatever a test writes, it removes.
3. **Do not delete anything you did not write.** Not even something that
   looks like scratch.

That rules out a whole class of measurement, and it is the right trade — a
20 MB drive with somebody's DOS install on it is not a scratch volume. What
it leaves is **reads**, which is most of what is interesting anyway, and
`sysbench`'s hard-disk block is built to that constraint: it mounts, walks
the FAT, reads one file that a DOS 3.3 disk is guaranteed to have
(`COMMAND.COM`), and puts the current volume back. **It never writes, never
creates and never deletes.** The write half of the picture stays unmeasured
on purpose, because a run interrupted between creating a scratch file and
removing it would break rule 2.

Both of its paths were verified under QEMU before it was ever pointed at real
hardware — with no hard disk it prints its refusal and the report still saves
to A:, and with a 20 MB FAT16 partition on ST-225 geometry every row produces
a number, the read returns the file's exact size, and **the disk image is
byte-for-byte identical afterwards**. That last check is the one that matters
here, and it is the one to repeat if anything in that block changes.

---

## The Toshiba T1100 Plus — `Elendilon/os8088`'s, and the only 8086 in the register

The second real machine, and it earns its place by being *nearly* the target
and not quite: an 8086 rather than an 8088, so the same instruction set over
a **16-bit bus**. That makes it the one machine that separates "this is
slow because the CPU is slow" from "this is slow because every instruction
byte is fetched one at a time".

| | |
|---|---|
| owner | **`Elendilon/os8088`** |
| CPU | **i80C86-2**, 7.16 MHz fast / 4.77 MHz slow, switchable from the keyboard; it powers on in fast mode |
| RAM | 256KB on the board + the **384KB expansion** = 640KB |
| video | CGA-compatible, LCD |
| floppy | **two 720KB 3.5" drives** — 300 RPM, 250 kbit/s, 100 ms average latency, 6 ms track-to-track |
| other | the modem expansion is fitted |
| disks | it takes the **720KB** images (`build/cga720.img`), which is why they exist — no `dd` step, unlike the 360KB pair |

Two things it has already been worth. Its `est CPU MHz` came out at **7.12 /
7.29** against the manual's 7.16, so sysbench's estimator is **0.6% out on a
machine nobody calibrated it against** — which is the only independent check
that number has ever had. And its instruction table is the 16-bit bus in
plain sight: `mov r16,r16` is 4.34 clocks a byte on the 5150 (2 bytes, 8.69
clocks) and **3.31 clocks total** here, while `mul` and `div` scale by the
clock alone. Part 2's instruction floor is a property of the 8088's bus, and
this is the machine that shows it by not having it.

It walls at the same **2,161 bytes/second** on the floppy as the 5150 does
(docs/FIELD-NOTES.md 7), which is what makes that wall two machines wide.

---

## The Compaq Portable III — `Elendilon`'s, and the fast floppy

The third real machine, and the one that keeps the disk work honest: an
AT-class BIOS and a **1.2 MB drive**, so every floppy assumption calibrated
against the 5150's 360 KB Tandon gets a second reading from different
hardware.

| | |
|---|---|
| CPU | 80286 (tier 1) |
| video | CGA 640x200 — the plasma panel, which the §39 probe finds as CGA |
| floppy | **1.2 MB 5.25"**, and it reads the 360 KB field disks |
| serial | **two ports**, 3F8 and 2F8 — and the mouse is on **2F8 driving IRQ4**, the cross-wiring SPEC.md §9.5.2/§9.5.2.1 both exist for. `sysbench` reports it as `winning row 2` with `winning IRQ 0010` |

What it has already been worth (PERFORMANCE.md Part 9 Set 18): **11,047 B/s**
on a 16 KB read against the 5150's 7,457, because a 1.2 MB drive spins at
**360 RPM** — a revolution is 166.7 ms, not 200 — so the same batched read
costs 0.28 revolutions a sector. It is the second BIOS to confirm that
trusting `CF` rather than `AL` reads the right bytes (`data check ... OK`),
which one machine alone could not establish.

Two things to know before running it.

**360 KB media in a 1.2 MB drive is marginal by construction** — 48 tpi
tracks written under a 96 tpi head — so a disk this machine refuses is a disk
to rewrite before it is a bug. It refused one build with `os8088: disk error`
and then booted the same code from a fresh disk with no error at all
(Set 18). `make BOOTDIAG=1` builds a sector that prints int 13h's status
instead of that message, which is one boot rather than a bisect.

**Its cold read is 2.4x its warm one** (3.63 s against 1.48), where the
5150's are within 5%. That is the AT BIOS identifying the media by trying
data rates, and it is paid once — so a cold-motor row from this machine is
not comparable to a cold-motor row from an XT.

**Its mouse is UNTESTED, not broken.** `sysbench` reports `mouse found 0`
with both ports probing present and no identify bytes on either — which looks
exactly like §9.5.2's cross-wired IRQ, and is not: **there was no mouse
plugged into it.** The owner has one serial mouse and it lives on the 5150.
A `mouse found 0` from a machine with nothing attached is the correct answer,
and reading it as a fault is the same error as reading `No volume at index 2`
as a missing hard disk. Ask what was connected before diagnosing a mouse.

---

## The Packard Bell Victory 286 — in the register, results discarded once

| | |
|---|---|
| owner | **`Elendilon/os8088`** |
| board | Packard Bell Victory (theretroweb.com/motherboards/s/packard-bell-victory) |
| CPU | **16 MHz AMD 286** |
| RAM | 4MB, 100ns |
| video | onboard **Paradise PVGA1A**, 256KB — a **VGA** card |
| clock | Dallas DS1287/DS12887, potted |
| floppy | 1.44MB drive A, 1.2MB drive B |

**Its first set was thrown away, and the reason is a trap for the next
person.** It was run from a `VIDEO=cga` field disk, so a VGA machine spent
the whole suite driving the CGA framebuffer path — a fourth combination that
is not one of the three the project supports — and the reports self-identify
as `CGA 640x200 mono` while the files had been hand-renamed `GFXVGA.TXT`.
Two derived rows were worse than useless: `est CPU MHz x100` read **8866**
and `shl clk/bit x100` read **29**, both because they are computed against
**8088** instruction timings a 286 does not have.

So before this machine is worth running again it needs a **VGA field disk**,
which `make field` does not build. And whoever adds one should fix the two
8088-only derived rows to say so on a tier-1 machine rather than printing a
number. It also has a known quirk: **it sometimes decides to boot in mono**,
which may be what put it into the CGA path in the first place.

---

## The Compaq Portable III — `Elendilon/os8088`'s, and mostly unrecorded

**A second real machine, and the one that found SPEC.md §9.5's first field
bug.** It is in here because it is not the 5150 and the difference matters:
the 5150 is an 8088 at 4.77 MHz with a Hercules and a CGA, and this is a
286-class portable with a plasma display. A result from one is not a result
from the other, and PERFORMANCE.md Part 6 rule 8 applies between them exactly
as it applies between iron and an emulator.

| | |
|---|---|
| owner | **`Elendilon/os8088`** |
| machine | **Compaq Portable III** |
| serial | a **1200 baud modem on COM1**. The mouse is on the other port, which is what §9.5 was built for |
| floppy | **one 1.2MB 5.25"**, and it boots the **360KB** images — so `make field`'s disks and `make comscan`'s `comscan.img` are the ones to send, not the 1.44MB pair |
| everything else | **not recorded, because it has not been measured.** Do not fill this table in from what a Portable III generally has — ask, or read it off `comscan` |

**A 1.2MB drive writing a 360KB disk is a known hazard and the owner is
handling it by keeping those disks separate** — a 1.2M drive's head is
narrower than a 360K drive's track, so a disk it has *written* may be
unreadable in a real 360K drive afterwards. It is worth knowing which images
write: the field disks do (their whole point is that the benchmark reports
land back on the disk they came from, and the Control Panel writes
`SYSTEM.CFG` on close), and **`comscan` never writes to a disk at all**.

**What it found, and it is now diagnosed and fixed:** with §9.5's two-port
support in, the mouse was **not detected** on this machine. `tests/comscan`
(docs/TESTING.md) was written for it and answered it in one run:

```
BIOS POST found (40:00h): 03F8 02F8 0000 0000
COM1 03F8  ok  ok  --  --  8250        COM1 03F8: 0 bytes  (silent)
COM2 02F8  ok  ok  ok  ok  8250        COM2 02F8: 244 bytes  first=43 'C'
                                            pkts 81  viol 0  best clean run 81
                                       IRQ line: IRQ4
```

**The mouse is at 0x2F8 and its card drives IRQ4**, where os8088 derives IRQ3
from the base. 81 packets with zero protocol violations to a polled reader —
a perfect mouse the kernel could not hear, because the COM1 vector fired,
read 0x3F8's receive register, found nothing, and left the byte at 0x2F8
holding a line that never made another edge. SPEC.md §9.5.2 is the fix: every
hooked line now services every live port. Reproduced in QEMU first
(`-device isa-serial,iobase=0x2f8,irq=4`), which fails identically on the old
kernel and passes on the new one.

**…and that fix was half of it.** The machine came back with the mouse
detected, moving **exactly once**, and then frozen — a different symptom from
the same one fact. `mou_lockon` retires the losing port with an 8259 mask
taken from its *base*, so retiring 3F8 masked **IRQ4**, which on this machine
is the mouse's own line; the port settles after eight packets and the next UI
pass kills it. SPEC.md §9.5.2.1 is that fix (the mask comes from the line the
winning packets arrived on) and docs/FIELD-NOTES.md 13 is the write-up.
`make test MOUSEPORT=com2irq4` is this machine, and it reproduces both halves.

**So this machine has now found the same wrong assumption twice**, in two
routines, with two unrelated-looking symptoms — which is the thing to expect
of it rather than to be surprised by. It is the only two-port machine in the
register and the only cross-wired one, so it is the sole witness for §9.5's
contest, §9.5.1's modem defences and §9.5.2's line/port split alike.
**Both are now confirmed fixed on the machine** (docs/FIELD-NOTES.md 13), and
the same report carries the first real-hardware confirmation of §9.4.1's
identify burst: `packets needed COM1 8` against `packets needed COM2 1`, the
threshold dropped on the one port that answered like a mouse.

**Its floppy drive is a 1.2MB 5.25", and that is a measurement fact, not
trivia.** It turns at **360 RPM — 167 ms a revolution — even with 360KB media
in it**, where every other machine here is a 300 RPM/200 ms drive, and its
media is close to **1:1 interleaved** where the 5150's is 2:1. So it is the
only machine in the register that can show a *latency* cost for what it is:
its BIOS streams a track at 22,368 B/s against the 5150's 11,984, os8088 gets
11,047 against 8,062, and the gap to its own ceiling is therefore **2.03x
against the 5150's 1.49x** for identical code issuing an identical 5 calls
(PERFORMANCE.md Part 9 Set 19). A single calibration machine flatters a
latency bug, and this is the machine that says so.

Read its reports with the revolution time in mind: `int 13h 1 sector` comes
out at 0.989 of a revolution at 360 RPM and an impossible 0.82 at 300, which
is how the drive identifies itself from the report alone.

Two lessons from the same run, both about the instrument rather than the
machine. **COM1's loopback failure was comscan's own bug**, not the modem's —
the test inherited whatever divisor the previous one left, so it timed out on
one port and not the other; it programs its own divisor now and prints the
as-found one. And the modem on COM1 stayed **silent** throughout, so none of
§9.5.1's Hayes-result-code defences were exercised on real hardware — they
remain QEMU-verified only.

---

## PCem and MartyPC — the other places results come from

Not machines in the register's sense, but they belong here because reports
come off them and are easy to mistake for field sets. PCem is where the
5150's owner tests routinely; MartyPC is a cycle-accurate 5150 emulator and
produced Part 9's Set 4.

Both emulate period hardware at period speed, which puts them in a different
class from QEMU entirely: their numbers are in the right units and the right
order of magnitude, so nothing about them looks wrong. Treat either as a
**very good model** and never as the machine —

**PCem runs about 20% fast**, and that figure is the 5150's owner's, from
running the same things on both. It is the single most useful thing to know
about a PCem report, because it is small enough to be invisible and large
enough to matter: a stall PCem shows at 990 ms is about 1.2 s on the iron,
and a frame budget that just fits under PCem is over on the machine. So a
PCem timing is a **lower bound on the real cost**, never an upper one, and
"it keeps up on PCem" is not yet "it keeps up". Work counts — instructions,
calls, glyph cells — are unaffected; only the clock is.

- it is the right tool for *reproducing* something the 5150 showed, without
  spending the seven-step trip below;
- it is the right tool for anything the 5150 must not be pointed at — a
  format, a partition, a disk tool run, anything that writes;
- and their figures go into Part 9 **named**, or not at all. Part 9's four
  provenance lines exist for exactly this, and Set 4 is the worked example:
  it says MartyPC on its machine line and carries its own calibration, which
  is what makes it comparable to the next run rather than to nothing.

## MartyPC — the same caveat, one class better

Also not a machine in the register's sense, and it goes in Part 9 **labelled
MartyPC** for the same reason PCem does. The difference worth knowing is that
MartyPC is **cycle accurate** rather than approximately period-correct, so it
models the 8088's prefetch queue and bus contention rather than a clock rate
— which is precisely where this project's costs live (Part 2's instruction
floor is a prefetch-starvation number). Set 4 came off it.

It has already produced something the 5150 has not: a **77-second log of a
running application**, one row a second, rather than a benchmark that runs
once. That is a different instrument and it needs the discipline below.

**A long log is only comparable to another long log if it says how fast the
machine was.** Two earlier runs could not be compared at all — kernel code
neither of them touched moved 16–19% between them, and nothing in either log
said so, which made every conclusion drawn from the pair worthless. A run
must time a fixed, known quantity of work at each end and print it; Set 4's
`CAL` lines are that, and their CPU figure agreeing to 0.01% is what licenses
every other number in the set. If the two brackets disagree, the machine
moved *underneath* the measurement and the rows between them are suspect.

### It takes MEMORY DUMPS — and an agent can now take its own

**This section has been overtaken in the best way.** `make marty`
(docs/MARTYPC-DEBUG.md) runs MartyPC *in the container*, with a debug server
attached, so a dump is a command rather than a favour:

    python3 tools/os88marty.py 127.0.0.1:9001 verify

which dumps `KERNEL_SEG`, diffs it against `build/kernel.bin`, and prints the
differing runs. **Ask nobody for a dump of a build you can run yourself.**

What still needs the owner is a dump of a machine *whose behaviour differs
from the emulator's* — which, given everything in docs/FIELD-NOTES.md, is
more often than it sounds, and always when a disk is involved. MartyPC's
floppy is 30x fast and its BIOS returns what its author believed the hardware
returns (PERFORMANCE.md, Set 11), so **a dump taken in the container proves
what the code does and not what the 5150 does with it**.

The rest of this section is unchanged and applies to both, because the rules
are about dumps and not about who took them. MartyPC's own debugger will also
dump the full 1MB and the code segment from its GUI, which is still the route
when the owner is running it interactively.

A dump is a strong instrument because it is **self-validating**. The kernel
image lands at `KERNEL_SEG`, so linear `0x600` onward is `build/kernel.bin`
byte for byte apart from writable state, which does three things at once: it
proves the machine was running *the build you sent* (diff it — a
mouse-identify dump came back 1,353 differing bytes of 71,112, all of it
`.text` data with real initialisers), it gives you every kernel variable at
its listing offset with no instrumentation added, and the differing bytes are
themselves the answer. `boot_ticks` at `0060:000C` is the cheapest check that
you are reading the right image at the right base: `FFFF` in the file, the
elapsed count in the dump.

Three rules, all learned on the one dump this register has so far:

- **Say what state you want it taken IN, because the interesting state is
  usually the untouched one.** The mouse dump had to be taken at the desktop
  with the mouse *deliberately not moved* — the whole question was what the
  kernel believed before any packet arrived, and one nudge erases it.
- **Re-derive every offset from a listing of the exact commit**, and never
  from an earlier session's numbers. `nasm … -l` and then `0x600 + offset`;
  anything before `font.inc` moves them all.
- **Find a value that pins the reading before you trust any of it.**
  `mouse_x`/`mouse_y` sitting on `[vid_w]/2, [vid_h]/2` said "Hercules, and
  nothing has moved yet" in one word each, and without it a `mou_seen` of 0
  beside a moved cursor looked like a contradiction in the kernel rather than
  a correct reading of an untouched machine.

What it still cannot do is the register's own first rule: it is an emulator,
so its **timings go in Part 9 labelled MartyPC** and a dump is evidence about
*logic*, never about time.

---

## How to take a set on the 5150

### `make field` — two bootable disks, because there is no drive B

```sh
make field          # -> build/herc.img and build/cga.img, both 360KB bootable
```

Both are shaped by the machine, and neither decision in them is cosmetic:

- **The benchmarks are on the BOOT disk**, in the root, for the reason
  `TASKMGR.O88` is on it at all (§28.3 — that one is in `SYSTEM/`, because
  it is the kernel's and these are yours to double-click). With one floppy drive, a
  benchmark on a separate data floppy means a disk swap mid-session — and on
  this machine a disk swap is a walk to another room and back (below).
  **THAT IS A RULE FOR EVERY FIELD HARNESS AND NOT A FACT ABOUT THIS TARGET**,
  which is how it gets missed: it is written down here, under `make field`, so
  someone writing a *new* bench does not read it. `tests/npbench` was built as
  a second disk and had to be rebuilt as a boot disk; docs/TESTING.md now
  carries the same rule where a harness author will actually meet it. Boot
  either image and the four harnesses are one double-click away in Disk A,
  and the reports they save land back on the disk they came from.
  `os88disk.py` marks them visible + read-only (§19.6), so they list and
  cannot be deleted by accident.
- **One image per card.** Both cards live in the machine permanently, so the
  probe can only be asked one question at a time and it answers *Hercules*
  (§39.1). `herc.img` is the ordinary shipped kernel — it exercises the probe
  on the way past — and `cga.img` is a `VIDEO=cga` kernel that ignores the
  Hercules. That kernel is built in `build/cgak/`, never in `build/`: a
  forced kernel that reaches `build/` is a machine that boots the wrong card
  for **everyone**, and that is a mistake that has been made.

**Neither disk may be write-protected.** The reports are the point, and a
protected disk answers int 13h status 03h, which the OS faithfully reports as
`Write protected`.

They are 8.3-short and unambiguous at a DOS prompt on purpose: DOS 3.3 has no
tab completion and these names get typed by hand into `dskimage`.

**They are never committed**, and neither is anything else under `build/` —
it is gitignored outright (SPEC.md §16), and `all` never builds these two in
any case. They are somebody's test disks, built on demand and **sent** —
attach them to the person who is going to write them to a floppy. Adding them
to the repo would put a pair of large binaries under version control that no
source change updates, which is exactly why the shipped images stopped being
tracked.

### Then, on the machine

- Boot the image, open **Disk A**, launch `GFXBENCH.O88`.
  **`R`** runs it, **`S`** saves the report. It names the file after the
  adapter it found: `GFXHERC.TXT` / `GFXCGA.TXT` / `GFXVGA.TXT`, so the two
  disks cannot produce a file that overwrites the other's.
- Then `SYSBENCH.O88`, likewise, to `SYSBENCH.TXT`.
- `gfxbench` is about fifteen seconds. `sysbench` is about a minute on a
  floppy-only machine and **two or more with the hard disk mounted** — its
  read row calibrates itself off the first read and then runs for about six
  seconds, because a benchmark here has to be accurate and useful rather than
  quick, and method T quantises to 54.92 ms. It prints the iteration count it
  chose. **The machine is frozen while either runs, by design** — the screen
  sitting still is not a hang, and the bottom line says which block it is on.
- Bring the `.TXT` files back and paste them into Part 9 with the four
  provenance lines it asks for.

### The path an image takes to get there

This is the real cost of a field run, and it is why "just rebuild and try
again" is not a thing anyone should ask for casually. The 5150 has no modern
storage in it by design, so an image travels:

1. Fetch the SD card from the **writer machine** — a second period box with
   both a genuine 360 KB drive and a **picomem** (a modern card that boots it
   from `.vhd` images on SD). The picomem is on *that* machine, never on the
   5150.
2. Mount the VHD on the primary system.
3. Copy the `.img` into the VHD.
4. Unmount the VHD, then the SD card.
5. SD card back into the writer machine; boot it to **DOS 3.3**.
6. `dskimage` writes the 360 KB image to a real 360 KB disk. **It has to be a
   real 360 KB drive** — head geometry differs between 360 KB and 1.2 MB
   drives, and a 360 KB disk written in a 1.2 MB drive is not reliably
   readable in one.
7. Carry the disk to the 5150 and boot.

Two consequences worth acting on. **Batch the questions**: everything a set
can answer should be in the image before it is written, because the marginal
cost of one more benchmark row is nothing and the marginal cost of one more
*trip* is the seven steps above. And **make the build deterministic before
you hand it over** — quote a commit and build the image from a clean checkout
of it, so a disk that behaves oddly is a finding rather than a question about
which build it was.

---

## What to ask the 5150's owner for, and what not to

**Worth a field run** — nothing else can answer these:

- **Time.** QEMU is exact about how much work the guest does and useless about
  how long it takes (Part 4). Anything whose answer is in microseconds is a
  field question.
- **The three defects QEMU cannot show at all** (Part 3): a visible redraw, a
  double-draw flash, and input overrun. These are judged by a person watching
  the glass, and no screenshot substitutes.
- **A model this repo has been spending without measuring.** Part 9's "what
  the next set is being asked" table is the current list — the variable-shift
  slope, the table-lookup cost, the memory-form `mul`, the per-row fill term,
  and the whole-screen repaint.
- **The rungs no emulator has**: §37.90's MM58167 and RP5C01 clock tiers, and
  §39.1's video detection probe on real cards.
- **What a real PERIPHERAL does on a real card.** Not the same question as
  "what does the emulator model" — SPEC.md §9.4.1 turns on whether a real
  serial mouse answers a DTR/RTS raise with `'M'`, and QEMU's `msmouse`
  ignores DTR outright while MartyPC's is a model of one. `sysbench`'s mouse
  block is a **state dump rather than a measurement** for exactly this
  reason, and it is the shape to copy: when the field question is about
  logic and not time, publish the state and let the machine print it.

**Not worth a field run** — the container already answers these, faster and
reproducibly:

- **Counts.** How many fills, glyphs or walk iterations something does is
  exact under QEMU; instrument a counter and read it over QMP (Part 4).
- **Instruction counts.** `-icount shift=3,sleep=off` is deterministic to ±1
  and machine-independent.
- **Whether the pixels are right.** A byte-for-byte screendump comparison on
  `VIDEO=cga`, plus `tools/hercshot.py` for Hercules, settles rendering
  without leaving the container.

The rule of thumb: **send it a question about time or about what a human
sees; keep every question about work.**

### Handing over a build

State the **commit**, and hand over the images rather than a branch name — a
branch moves. Build them from a clean checkout of the commit you quote with
`make field`, and quote the knobs each image carries, or the floppy holds
something the source no longer says.

**Which knobs those are is now the Makefile's business, not yours.** Every
field kernel is `DISKCNT=1` (SPEC.md §18.94.1) and three of the five carry a
second knob of their own — `VIDEO=cga`, `FLOPPY1=1`, `BOOTDIAG=1`. That is a
change from the rule this section used to state, which was "no knob at all",
and it is why the knob banner now names `DISKCNT=1` as the expected one.

**A benchmark kernel is not bound by `KERN_BUDGET`.** Every machine in this
register has 640KB (the 5150 by way of its SixPakPlus), so the only ceiling
on an instrument is the RAM in the box, and instruments may be compiled into
these kernels freely. The thing to watch is not size but **parity**, and it
costs nothing to watch because `make field` runs `tools/fieldsize.py`:

- **`boot ticks` and every heap row are measurements of the kernel that is
  running.** The kernel is read off the floppy a sector at a time and the
  heap starts where it ends, so both move when the image does.
- **Nothing else does** — drawing, CPU, RAM bandwidth and the floppy's own
  bytes/second are all measured against the machine, not the kernel's extent.
- **The unit is `KIMG_PARA`'s 512-byte rung**, not the byte. Two kernels in
  the same rung have an identical memory map and an identical sector count,
  so their rows compare *exactly*; today's field kernel and the shipped one
  are both 72,199 bytes and both 142 sectors, which is why folding the disk
  counters in cost the boot-tick series nothing.

Growing past a rung is allowed. It just has to be **known about** rather than
discovered later in a number that moved for no visible reason.

### Delivering images in `Elendilon/os8088` — on request, and after every commit

Everything above is about a **field run**: a set of numbers, a seven-step path
to a real 360 KB disk, and a knob banner. This section is about the ordinary
case, and it has different rules. They apply to work on **any branch of the
`Elendilon/os8088` fork**, and they are that fork owner's standing preference
rather than a property of the project — a session working in a different fork
should not assume them.

**Send the 360 KB pair after every commit, without being asked.** That is
`build/os8088-360.img` (the system disk) and `build/apps360.img` (the apps
disk) — the 360 KB geometry because it is what the register's machines read.
The owner does not have to ask each time, and a commit that lands without its
images is a commit they cannot put on a machine.

**"Send" means attach the files to the reply.** A path into the session's own
`build/` or scratch directory is not a delivery: those live in a container the
owner cannot reach, and the container is reclaimed when the session ends. Use
whatever the running harness offers for attaching a file (in Claude Code, the
`SendUserFile` tool).

**Do not boot an image after building it.** Not on request, not after a
commit. Build it, send it, and say what is in it. The reasoning is the trade
rather than a claim that booting is worthless: a boot-and-drive cycle costs
minutes of session time, and by the time an image is being built that cycle
has almost always just been run as part of the change being committed — so
the second one re-establishes what the first one already established. When
the owner wants an image exercised harder because it is going to real
hardware, they will ask for that specifically, and then the rest of this file
applies.

Two things this does **not** relax. A change is still tested before it is
committed — the rule removes a redundant boot after the build, not the
verification that earned the commit (docs/TESTING.md is still the matrix, and
`make marty` is still the default instrument). And a **merge** onto the
integration branch still rebuilds and boots before it is pushed *when it could
change a shipped byte*, because a merge combines two trees that were never
tested together, so no earlier run covers the result.

That last one has a stated exemption, and it follows from the same reasoning
rather than softening it: **a merge that cannot reach the images needs neither
the build nor the boot.** Documentation and harness code `make` never invokes
produce an identical set of six images by construction, so there is nothing
for a boot to cover. The line is "could this change a byte under `build/`" and
**not** "is it under `tools/`" — `os88disk.py`, `os88pkg.py`, `os88drv.py` and
`os88mini.py` all write shipped bytes, the last by generating a prerequisite
of the kernel. CLAUDE.md carries the full list and the md5 check that settles
a doubtful case.
