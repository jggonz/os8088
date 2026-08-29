# os8088 parallel network plan

**Research document, not a contract — and now PARTLY IMPLEMENTED.**
> The study below was written before the code and is kept as the reasoning
> behind it. What shipped is SPEC.md §62 (block mode) and SPEC.md §62.9 (the
> `DRVC_FILE` redirector), and `NET.DRV` is on every disk; where the two
> disagree, SPEC.md is the contract. §2.2.1 records which milestones are
> built and which are not.

> **Stage 3 — the socket half — has moved to docs/NET-STACK-PLAN.md**, which
> takes §5 further than this document could: it covers mTCP forwarding *and*
> an 8-bit Ethernet card behind **one** socket API, because those are the same
> feature to a package and two very different prices to build. Its consumer is
> docs/BROWSER-PLAN.md. §1–§4 and §6–§9 here are unchanged and still describe
> the transport, the redirector and the DOS side.

SPEC.md is the binding contract for what
the kernel *is*; this is the study of what it would take to put a **LapLink
parallel link to a DOS machine** behind a loadable driver (SPEC.md §51) — a
`Network` volume on the desktop that the file manager browses and writes, plus
a path for a package to reach an **mTCP** connection on the DOS side. Every
interface named still lands in SPEC.md *before* its code.

The ask, in the requester's words:

- A **parallel "network" driver**. A LapLink-style null adapter cable between
  the parallel ports of the os8088 machine and a DOS machine.
- A **DOS program** written for the DOS side.
- When connected, a new **"Network" drive appears on the desktop**.
- **File Manager can view the contents of the remote system, and read/write
  files.**
- **Prefer the code to live in the driver**; avoid growing File Manager as much
  as possible.
- The driver also **forwards data to and from an mTCP connection** running on
  the DOS system.

---

## 0. The verdict, up front

**It fits, the File Manager needs exactly zero changes, and the driver can own
almost all of it — but "almost" is doing work in three places, and one of them
is needed before the first byte crosses the cable.**

The good news first, because it is the load-bearing finding:

> **The File Manager's entire disk surface is seven cold-segment thunks and
> nine `dskw_*` bodies.** `files.inc` never touches a FAT, a cluster, a BPB or
> an `int 13h`. It navigates with `cw_dsk_chdir`, reads staged entries with
> `cw_dsk_get_dir`, asks free space with `cw_dsk_free_clus`, and acts through
> `dskw_delete_x` / `dskw_rename_x` / `dskw_mkdir_x` / `dskw_rmtree` /
> `dskw_format`. **Every one of those is a redirect point.** Put the branch
> there and `files.inc`, `fdlg.inc` and `filecp.inc` do not change by one byte.

So the question is never "how much does File Manager grow" — it is "how much
does the *kernel* grow", and that is a different budget with a much tighter
guard. The live figures, measured on this tree today:

```
kernsize[big]:   footprint 98,816 of 100,352 -> 1,536 spare (3 steps)
                 image rung 227 bytes left    cold rung 107 bytes left
kernsize[small]: footprint 93,184 of  96,256 -> 3,072 spare (6 steps)
                 image rung 135 bytes left    cold rung 100 bytes left
```

**227 bytes of image slack and 107 of cold.** Anything real crosses both rungs,
and a rung is 512 bytes. That is the arithmetic every estimate below is against.

### The three things a driver cannot do today

| # | Why the driver cannot do it | Kernel change | Est. | Needed by |
|---|---|---|---|---|
| 1 | `dsk_xfer`'s driver branch dispatches through `[drv_svc + DSV_SIZE + DSV_BLK]` — **class 2, hard-coded**. A `DRVC_DISK` driver is a singleton (§51.2.1), so a network driver either *is* HDD.DRV's class and cannot coexist with it, or its block calls dispatch into HDD.DRV's table | a volume ROW that names its own class (§2.1) | **~40 B** | **Stage 1** |
| 2 | The mount is `disk_mount` — BPB, FAT window, root scan. A volume whose remote side has *files* and no sectors has nothing for it to read | a `DVK_FILE` branch at the mount, `dsk_chdir`, `dsk_free_clus` and nine `dskw_*` bodies | **~380–460 B** | Stage 2 |
| 3 | **There is no generic package→driver call.** `OSAPI_VOL_*` and `OSAPI_DRV_CFG` are fenced *to drivers*; nothing lets a package reach a driver's own service at all | one opaque slot at 0x03D0 | **~60 B** | Stage 3 (mTCP) |

Item 1 is the surprise. **Block mode is not free after all** — it is nearly
free, but the machine this project is calibrated against has an ST-225 on an
ST11M, so "a network drive that cannot coexist with the hard-disk driver" is a
real collision on the one machine that matters most.

### The recommendation

**Three stages, each shippable, each proving the one after it.**

| stage | what it is | kernel cost | what it buys |
|---|---|---|---|
| **1** | **Block volume** over the cable. The DOS side serves 512-byte sectors from an image file. `DV_CLASS` + class-aware dispatch, and nothing else | **~40 B** | a working `Network` drive, read **and** write, with the whole FAT layer, the Disk window, the file dialog, package launching and the association cache **already working**. Proves the wire, the framing, the timing, the Control Panel page and the desktop zone at near-zero kernel risk |
| **2** | **File redirector.** A `DRVC_FILE` service table the kernel dispatches for listing, chdir, read, write, delete, rename, mkdir, rmdir, stat and free space | **~380–460 B**, one image rung + one cold rung | the *actual contents of the remote system*, at any size, with no coherency hazard and no 32 MB cap |
| **3** | **mTCP forwarding.** One opaque `OSAPI_DRV_CALL` slot; the socket API itself lives entirely in the driver and its SDK header | **~60 B** | packages get TCP. The kernel learns nothing about networking, which is the point |

Stage 1 is worth building **even though Stage 2 supersedes it**, and §2.1 is
the argument. Stage 3 is independent of both and could be built second.

### Decisions taken

Four questions in §10 have been answered by the owner and the plan below is
written to them rather than around them.

- **Go all the way to Stage 2**, because a remote drive cannot be assumed to
  be under 32 MB — and because the redirector is useful to something other
  than a cable. **That second reason changes the design and §2.2 follows it**:
  the class is `DRVC_FILE` rather than a `DRVC_NET`, the volume row names its own
  class rather than encoding one in a new kind, and networking is the first
  client of a general hook instead of being the hook.
- **`kern_small` gets all of it.** §7.1 is the arithmetic, and it says
  something better than "the risk of leaving it out": **`kern_small` can
  afford this within its standing headroom and `kern_big` cannot.**
- **One driver.** The user-facing question is *"is network on?"*, not *"is the
  particular part of networking I want on?"* — one `drv_tab` row, one Control
  Panel page, one file. §5.5 is how that is reconciled with a 128 KB machine
  not paying for TCP it never uses.
- Step 0 and step 1 of §9 **merge into one artifact**, `tests/lptlink`, for
  the reason §9.1 gives.

### And the reason to want this at all is in docs/FIELD-MACHINES.md

The seven-step path an image takes to reach the 5150 ends like this:

> 5. SD card back into the **writer machine**; boot it to **DOS 3.3**.
> 6. `dskimage` writes the 360 KB image to a real 360 KB disk.
> 7. Carry the disk to the 5150 and boot.

**The writer machine is a period box running DOS 3.3, and it is already in the
room.** A parallel cable between it and the 5150 collapses steps 5–7 into
"drag the file across" for everything that is not the kernel itself — every
benchmark, every app, every test data file, and the `.TXT` reports coming
*back*, which today need the whole trip run in reverse. With SPEC.md §52.10's
hard-disk install the 5150 boots off its ST-225 and the cable delivers the
rest, at which point the floppy trip is needed for kernel changes and nothing
else.

That is the strongest case for this feature and it should be stated in the
commit that starts it.

---

## 1. The transport

### 1.1 The cable

The LapLink / INTERLNK / FastLynx "null printer" cable is a settled standard
and there is no reason to invent another. Five data lines out meet five status
lines in, in both directions:

| this end | DB25 | → | far end | DB25 | status bit read |
|---|---|---|---|---|---|
| D0 | 2 | → | ERROR (nFault) | 15 | bit 3 |
| D1 | 3 | → | SELECT | 13 | bit 4 |
| D2 | 4 | → | PAPEROUT (PE) | 12 | bit 5 |
| D3 | 5 | → | ACK (nAck) | 10 | bit 6 |
| D4 | 6 | → | BUSY | 11 | **bit 7, inverted in hardware** |
| GND | 25 | — | GND | 25 | |

…and the mirror image, 15→2, 13→3, 12→4, 10→5, 11→6.

**Four data bits and one handshake bit each way.** That is nibble mode, and it
is the only mode that works everywhere: a 5150's original parallel card has an
output-only data register, and so do most XT-era cards. A PS/2 bidirectional
port could do byte-at-a-time, but the target class cannot, so the fast path is
not worth having two protocols for.

**The status bits are contiguous and the inverted one is the top one**, which
is luckier than it sounds. The five received bits land in status bits 3..7,
and BUSY — the inverted one — is bit 7, so it becomes bit 4 after a shift of
3. One `xor` fixes it *before* the shift and the whole decode is three
instructions:

```
    in  al, dx          ; status
    xor al, 0x80        ; BUSY is inverted in hardware, and it is the top bit
    shr al, 1 / 1 / 1   ; (8086: no shl reg, imm - CL or three ones)
    and al, 0x1F        ; D4..D0 as the far end drove them
```

An earlier draft of this section called for a 32-entry `xlat` table on the
assumption that the bits were scattered. They are not, and the table is not
needed — worth recording because the same wrong assumption is what makes
people reach for bidirectional mode.

#### 1.1.1 The handshake is fully interlocked, and that is not the obvious choice

Four phases per nibble: the sender puts the nibble, raises its strobe, waits
for the ack to rise, drops the strobe, waits for the ack to fall. Both ends
idle at D4 = 0 between nibbles and **neither leaves until the other has
confirmed**.

The obvious design is half that — a two-phase toggle, where the sender puts
nibble+phase and the receiver echoes the phase back as its ack, one wait each.
It is correct within one direction and **racy at a direction reversal**: the
old receiver acks and then, as the new sender, immediately drives the same
line, so the old sender never sees the ack and waits for ever. Between two
machines of similar speed it mostly works; between a 4.77 MHz 8088 and
anything modern it is the usual outcome rather than a rare one — which is to
say it fails on exactly the pairing this exists for, and presents as a cable
fault.

Four phases fix the race *within* a direction and leave the same one at the
seam, because the receiver's ack-down and its first strobe-up are microseconds
apart. **`lp_turn` is the answer**: an end that has just been receiving waits
a whole system tick before it drives anything. It costs one tick per reversal
— a handful per transfer, not one per nibble — and it is called *from*
`lp_snib` on a flag rather than from the six places that reverse direction,
because a guard a call site can forget is a hang on somebody else's machines.

**And the deadlines are in TICKS, not in poll counts.** A poll is ~15 µs on a
5150 and ~1 µs on anything modern, so a fixed iteration count is a different
length of time at each end — and the fast end's patience ran out inside the
guard that exists to protect it. Both machines measure a tick identically;
`LP_SPIN` survives only as the granularity at which the deadline is re-checked.

All three of those were found in `tests/lptlink/linksim.py`, a host-side model
of the link layer, before any of it reached hardware. None would have shown up
on a matched pair on the bench.

### 1.2 What it costs, priced against this project's own measured numbers

PERFORMANCE.md's field table gives the one constant that decides this:

| quantity | field 5150 | set |
|---|---|---|
| **An ISA status-port `in`** | **8.7 µs** | |
| Floppy `FILE_READ` throughput, warm | **21,307 bytes/second** | **24** |
| …cold motor | 12,969 B/s | 24 |
| Floppy, per 512 bytes **delivered** | 24 ms | 24 |
| Floppy, raw `int 13h`, ONE sector | **199 ms** — one whole revolution | 22 |
| Floppy, raw `int 13h`, a 9-sector track | 384 ms | 14/22 |
| Floppy, open and read a one-sector file | 796–810 ms, **not re-measured since §18.95's cache** | 17 |

**This section's first estimate was 10–25 KB/s and it was too generous**,
because it priced a two-phase handshake — six port accesses a nibble — and
§1.1.1's is four-phase. The 5150 pays, per nibble: two `out`s to put the
nibble and raise the strobe, a poll for the ack to rise, an `out` to drop the
strobe, and a poll for the ack to fall. Every one of those is an ISA access at
the measured 8.7 µs, and a poll that finds its answer immediately still costs
one:

| | per nibble | per byte | throughput |
|---|---|---|---|
| 3 `out` + 2 polls, all at 8.7 µs, far end responding instantly | ~44 µs | ~87 µs | **~11.5 KB/s** |
| the same with realistic loop overhead (~15 µs a poll iteration) | ~77 µs | ~154 µs | **~6.5 KB/s** |

`tests/lptlink/linksim.py`, which charges a modelled clock per poll at each
end's own cost, says **6,499 bytes/second** for a 5150 against a modern far
end — the second row.

### 1.2.0 FIELD RESULT: 1,536 bytes/second, and the wire is not the cost

**The link came up first try and moved 16 KB each way with zero errors**, DOS
box calling as master on 0378, 5150 answering as slave on 03BC. Scan,
handshake, magic, turnaround guard and both direction reversals all confirmed
on iron. What it measured:

| | ticks | rate | errors |
|---|---|---|---|
| 5150 **sending** | 79, 78 | **3,741 B/s** | 0 |
| 5150 **receiving** | 83, 83 | **3,538 B/s** | 0 |

*(16,128 bytes each way, two runs with the master role swapped. The first
build measured 1,536 / 1,595; the accounting below is why, and PERFORMANCE.md
Part 9 Set 39 is the full record.)*

**Which direction is faster follows the 5150's ROLE, not who is master.**
Swapping the master swapped the labels and left the numbers where they were,
and receiving reproduced to the exact tick in both runs. `lp_rnib` does about
six instructions more than `lp_snib`; isolated that is ~12.7 µs at Part 2's
floor and measured it is 7.7, the difference being that the two ends run
concurrently so some of the receiver's extra work happens while the sender is
already waiting. That the asymmetry tracks the *slow machine's* role is what
confirms the 5150 is the bottleneck in both directions, which is what every
estimate here assumed and none had checked.

**The first build was 4.2x slower than §1.2's model, and the model was not
wrong about the wire — it was wrong about everything around it.** 325 µs per
nibble, of which
the two `in`s and three `out`s that actually move data are about 30. Counted
against PERFORMANCE.md Part 2's measured constants (4.34 clocks per instruction
byte, 4.77 MHz) the prediction is **360 µs, within 11% of the measurement**,
and it says exactly where:

| per nibble, on the 5150 | µs |
|---|---|
| `lp_wfar`, four calls | **289** |
| …of which `ticks()`, four reads | **94** |
| `lp_rnib`'s own body and frame | 71 |
| the wire | ~30 |

**Nine tenths of it was arriving, not working** — and the single worst item was
reading the BIOS tick counter to build a deadline **that is almost never
reached**. This is SPEC.md §5.7's finding in a new place: one `gfx_pixel` was
196 guest instructions of generic rect machinery across eleven routines with
no hot spot anywhere.

Fixed the way §5.7 was: the deadline is **lazy** (`ticks` is not read until a
whole `LP_SPIN` of polls has gone unanswered, which on a working link never
happens), the wait is a **macro rather than a called proc** so there is no
frame four times a nibble, the poll body is 9 bytes rather than 16 (the level
is a branch now, not a compare against a variable), and `DX` walks between the
two ports with `inc`/`dec` instead of being reloaded from memory. **Measured
after: 133.7 µs a nibble, 3,741 B/s, 2.44x** — against 93 µs predicted, the
residual being the layering *above* the nibble (`lp_sbyte`'s frame and the
benchmark's own byte loop, ~28 µs by the same count) plus the far end's
latency.

**The remaining lever is the handshake itself, and it is deliberately not
spent here.** Two phases instead of four halves both the waits and the `out`s
per nibble — §1.1.1 records that this is safe *only* with the turnaround
guard — and it is worth perhaps another 1.5x. It belongs in the driver, where
the code above the transport is real file I/O rather than a benchmark loop;
tuned here, the harness gets optimised instead of the feature.

### 1.2.1 The cable is SLOWER than the floppy, and the case survives anyway

An earlier draft of this section said the link was "1.3× to 3.4× the floppy",
and then that it was only "slightly slower". **Both were wrong, and by a lot,
because they were priced against 7,457 B/s — PERFORMANCE.md Part 2's floppy
throughput row, which was two Part 9 rounds stale.** The current figure is
**21,307 B/s** (Set 24). So:

| | bytes/second | |
|---|---|---|
| floppy `FILE_READ`, warm | **21,307** | Set 24 |
| **this cable, measured** | **3,741** | §1.2.0, on the iron |
| the ST-225 (Set 24) | 74,553 | for scale |
| ratio | **the cable is 5.7× slower than the floppy** | |

That is a real result and it is not fatal, but it does retire an argument this
plan was leaning on. **A 360 KB image crosses the cable in 99 seconds**; the
same image reaches the 5150 any other way via the seven-step path, which is
the comparison that actually decides this. **The case for this feature was never the transfer rate,
and §0 already says so: it is the TRIP.** A cable at 6.5 KB/s that needs no
trip beats a floppy at 21 KB/s that needs the seven-step path in
docs/FIELD-MACHINES.md — SD card, VHD mount, copy, unmount, writer machine,
`dskimage`, carry it across the room. 16 KB over the cable is ~2.5 s; 16 KB
onto a floppy that has to be walked to the 5150 is the rest of the afternoon.
Nothing about the throughput comparison touches that.

What DOES change is the two claims that leaned on the old number:

- **"Faster than the floppy for metadata" is now marginal, not dramatic.** A
  512-byte sector is ~79 ms on the wire against **199 ms** for an isolated
  `int 13h` — still 2.5× better, but §18.95's sector cache is what took the
  floppy from 7,457 to 21,307 in the first place, and it removes most repeat
  metadata reads *before* either number applies. Do not build an argument on
  this without measuring it.
- **"A network drive is a fast hard disk for a floppy-only machine" is
  false.** The ST-225 is 74,553 B/s (Set 24) — 11× this cable. Block mode's
  value is that it needs no controller, not that it is quick.

Every figure here is still a MODEL and PERFORMANCE.md Part 6's rule applies in
full: **it must be measured on two real machines before it is quoted anywhere
else**, which is what step 1 exists for. If the real number lands near 6,500,
the known optimisation is to go back to a two-phase toggle *keeping* §1.1.1's
turnaround discipline — the race was at the reversal, not in the toggle, and
`lp_turn` plus tick-based deadlines are what make a reversal safe. That is
roughly a 2× win, taking the cable to about a third of the floppy rather than
a seventh, and it must not be attempted without the guard.

### 1.3 Three rules the transport has to obey

- **Every loop is bounded.** This is the clock ladder's rule (§37.90) and it
  binds harder here: `DSV_BLK` runs with `[sch_lock]` raised and the gfx lock
  held, so a spin waiting for a handshake that will never come is a **hard
  freeze with the cursor stopped**, not a slow operation. Every wait takes a
  tick-based deadline and returns an `int 13h`-shaped status byte on timeout.
  A cable that is not plugged in must produce a failed mount, not a dead
  machine.
- **os8088 is the master and never receives unsolicited data.** Every exchange
  is request-then-response, driven from this side. That makes the multiplexer
  in §5.3 trivial and removes the entire class of "a socket's data arrived in
  the middle of a sector read" bug.
- **The link is not a serial port and must not grow an IRQ.** LPT1's IRQ7 is
  unjumpered on a great many XT cards and shared with a printer on the rest.
  Polling is both simpler and the only portable answer, and the transfers are
  short enough that it costs nothing the floppy does not already cost.

### 1.4 Finding the port, on both sides

**The 5150's port is confirmed: it is on the GB101**, which is the
Hercules-family LPT at 3BCh. That answers step 0's blocking question and
retires it — but it also makes the *general* problem the design problem,
because the far end in the room is a DIO-500 multi-I/O card that is almost
certainly not at 3BCh, and neither end may assume the other's address.

**So neither side is configured; both sides scan.** That is §9.5's shape
exactly — probe every candidate, arm every one that answers, let the one that
delivers win — and it is worth following closely, because §9.5 is the module
in this tree that has already been through every way of getting this wrong.

#### 1.4.0 FIELD RESULT: the two machines are at different addresses

`tests/lptlink` has been run on both ends and this section's premise is
confirmed rather than assumed:

| machine | port | BIOS-listed | latch | status | control |
|---|---|---|---|---|---|
| **5150**, Hercules GB101 | **03BC** | yes | ok | DD | 88 |
| **the DOS box**, DIO-500 | **0378** | yes | ok | D7 | EC |

Neither found anything at the other's address. **That is the whole argument
for §1.4 in one table** — a driver with a constant in it would have worked on
exactly one of these two machines.

Two things in those bytes are worth reading rather than skipping:

- **The DOS box's control register is `EC`, and bit 5 is SET** — its data
  register was left in INPUT mode. `lp_init` clears that bit by design, but
  the *latch probe* ran before it and did not, so it was probing a port that
  may not have been able to drive the pins at all. It passed anyway, because
  that card reads its latch back regardless; another might not. Fixed — the
  probe now forces output first and restores the control byte after.
- **A phantom `9FC0` appeared in the DOS box's BIOS list.** `0040:000E` is
  LPT4 only on a pre-PS/2 BIOS; on everything since it is the **segment of the
  EBDA**, and 0x9FC00 is an EBDA sitting just under 640 KB. The probe rejected
  it, so the fail-safe held — but the tool had already written 0xAA and 0x55
  to I/O port 9FC0h to find that out, which is precisely the unprovoked write
  to an unknown port §1.4.3 exists to forbid. The table is **three words**
  now, and anything at or above 0x400 is refused as not-an-I/O-port whatever
  the BIOS says.

#### 1.4.1 The candidate set, and why the BIOS table is trusted here

Three addresses are the whole universe in practice, and they are the three the
POST itself scans, in this order:

| base | what puts it there |
|---|---|
| **3BCh** | the MDA / Hercules family — **the GB101, so the 5150's** |
| **378h** | the usual dedicated or multi-I/O card address |
| **278h** | the second one on a multi-I/O card |

**The BIOS has already done this scan and published the answer** at
`0040:0008`: **three** words — LPT1..LPT3, zero meaning absent. Read it first.

**Three and not four**, which the field run caught: `0040:000E` is LPT4 only
on a pre-PS/2 BIOS and is the **EBDA segment** on everything since. Reading
four words invents a parallel port out of a memory address (§1.4.0), and
worse, probes it.

That is a *measurement* and not a claim, which is the opposite of §18.97's
situation and worth being explicit about, because this tree's instinct is now
to distrust the BIOS data area. §18.97 exists because `int 11h`'s floppy count
comes from **SW1's DIP switches** — a human's assertion about the machine, and
on the field 5150 a wrong one. The parallel table is filled by the POST
*probing* each address with a write and a read-back. It is the same test §1.4.2
does, already done, and it costs a memory read to consult.

It is still verified rather than trusted outright, for three reasons that are
all real: an XT-clone BIOS may scan fewer addresses than three, a card can sit
at a base the POST never looks at, and a port the BIOS found may have been
shadowed since. So: **the candidate list is the BIOS table plus the three
standard bases, deduped** — and the port that is in one and not the other is
worth *reporting*, because a disagreement between the two is information (it is
`comscan`'s `warn_outside`, which exists for exactly the serial version of this).

**One rule falls out and it is easy to get wrong: work in addresses, never in
LPT numbers.** On a machine with a mono card, LPT1 *is* 3BCh and a card at 378h
is LPT2 — so "LPT1" on the 5150 and "LPT1" on a machine with no mono card are
different hardware. The 5150 is precisely the machine that breaks the
assumption, and it is the machine this is for. Nothing in either half of this
should contain the string "LPT1" outside a message to a human.

#### 1.4.2 Is a port there? The data latch, two values

`base+0` is a readable latch on every parallel port from the original IBM
Printer Adapter onward — the *bidirectional* data path is the later extension,
not the readback. So:

```
save    = in  base+0
          out base+0, 0xAA   ->  in base+0 must read 0xAA
          out base+0, 0x55   ->  in base+0 must read 0x55
          out base+0, save
```

**Two values, not one, and for §9.5's stated reason**: an unpopulated ISA
address answers 0FFh, but a bus that happens to float to whatever single value
you wrote would otherwise pass. This is the third place in the tree to use that
idiom — the UART's divisor latch (§9.5), the CGA-alias test's two writes
through two segments (§39.11.1), and now this — and in all three the failure
the second value catches is the same one.

**It restores what it found**, which is not politeness: a printer on that port
sees its data lines change, and because nothing strobes, nothing prints.

#### 1.4.3 The control register is read-modify-write, and a bare store resets a printer

The one genuine footgun in parallel-port programming, and it belongs in the
plan rather than being discovered:

| control bit | signal | writing 0 does |
|---|---|---|
| 0 | STROBE | — |
| 1 | AUTOFEED | — |
| **2** | **INIT, active LOW** | **pulses reset at an attached printer** |
| 3 | SELECT-IN | — |
| 5 | direction (bidirectional ports only) | selects output — what we want |

So `out base+2, 0` — the obvious way to put a port in a known state — **resets
whatever printer is on it**. The driver reads the control register, clears bit
5, preserves the low four bits exactly as found, writes that back, and restores
the saved byte at detach. It never writes a constant.

That is §9.4's "put DTR/RTS back up first" in the other connector, and it is
also why nibble mode uses **D4 as its strobe** rather than the control
register's STROBE line: the data register is ours to drive and the control
register is the printer's to be left alone.

#### 1.4.4 Presence is not the question — the *cable* is

A port that answers §1.4.2 tells you nothing about what is plugged into it.
Four states, and only one of them is a link:

| on the port | the status register reads |
|---|---|
| nothing at all | a constant — the pull-ups' idle value |
| a printer | a constant — that printer's idle status |
| the cable, far end powered, **server not running** | a constant — whatever the far side's data latch was left holding |
| the cable **and a running server** | **whatever we ask it to be** |

**So the discriminator is: can I make the far side's status change on demand?**
Drive one pattern on our five data lines, read status; drive a different one,
read status. A far end that is polling echoes; two different reads mean a live,
cooperating, correctly-wired partner and nothing else can produce them.

**It is the two-value probe a third time**, now applied to the far machine
instead of to a chip, and that is the pleasing part: the same shape answers "is
there a latch here", "is there a card here" and "is there a *computer* here".

And the diagnosis falls out for free, which matters more than the detection
does. The three failing rows above are distinguishable, so the Control Panel
page can say **which** — `No port`, `Port, no cable`, `Cable, no server`,
`Linked` — instead of a single unhelpful refusal. That is §47's say-why-not
applied to a connection, and it is the thing that will save the most time when
somebody's cable is in the wrong socket.

#### 1.4.5 Two sweepers can miss each other forever

os8088 is the master (§1.3), so os8088 sweeps its candidates *issuing* a hello
and the DOS side sweeps its candidates *listening*. Both sides scanning at once
is the obvious design and it has a real failure: **two sweeps running at
similar rates can stay out of phase indefinitely**, each arriving at the right
port just after the other has left it, and the symptom is a link that connects
"sometimes" and looks like a cable fault.

The fix is that the two sweeps must not be the same speed. **The DOS side's
dwell on each candidate is longer than os8088's entire sweep**, so a full
os8088 pass happens inside one DOS dwell and cannot be missed twice. The DOS
side is the one with a whole machine to spend, so it is the right side to make
patient.

#### 1.4.6 What it costs, and when it runs

**Nothing against the kernel** — all of §1.4 is inside the driver, and the
estimates in §0 are unchanged by it.

Against the machine: reading `0040:0008` is a memory read, so deciding whether
the Drivers-page row is even worth offering is free and happens at boot. The
*probe* writes to ports, so it happens at **attach**, when the user has ticked
the row — which is §51.3's rule, and unlike §51.3.1's FM sniff there is no case
for making an exception, because a write to a port with a printer on it is
exactly the class of thing §51.3.1 gated behind a knob.

**And the answer is remembered.** §51.9's blob holds the base that worked, so
the next attach tries that one first and only sweeps if it has gone away — a
`NT` key beside `HD`, because §51.9 says a second claimant is a second key and
not a bigger blob.

#### 1.4.7 ECP, EPP, and not caring

A multi-I/O card of the DIO-500's generation may come up in ECP or EPP mode.
**Nibble mode needs nothing from either**: SPP-compatible access at `base+0`,
`+1` and `+2` works in every mode on real hardware, which is what makes nibble
mode the universal transport in the first place. Do not negotiate a mode, do
not touch the ECP extended registers at `base+0x400`, and do not assume the
card is at a base that has a `+0x400` at all.

#### 1.4.8 The survey is still worth building — as a diagnostic, not a gate

Step 0 was written as a gate ("stop if there is no port") and that is answered.
It should still be built, for the reason `comscan` exists: when this does not
work on somebody's machine, the question "what is actually on this machine's
parallel ports" needs an answer that does not require os8088 to boot.

`make comscan` already builds three ways — a DOS `.com` whose output redirects
to a file, a **bootable** 360KB floppy that needs no DOS and no mouse, and a
1.44MB twin — and the field machine's owner already knows how to run it. Adding
a parallel section to it is much cheaper than a second tool and puts both port
surveys in one report. It should print, per candidate base: whether the BIOS
listed it, the §1.4.2 latch result, the raw status byte, and the §1.4.4 verdict.

---

## 2. The two ways to be a drive

### 2.1 Block mode — the whole FAT layer, for ~40 bytes

A `DRVC_DISK`-shaped driver publishes `DSV_BLK` and the kernel is done: the
volume table gets a row, `dsk_xfer` branches to the driver with a
volume-relative 16-bit LBA, and **everything above it already works** — the
BPB validator, the FAT window, the directory walker, the write path with its
commit ordering, the Disk window, the Standard File dialog, the copy engine,
the loader, `ASSOC.DAT`, the desktop zone, the Control Panel page.

The BPB rules are **already volume-kind aware** and were made so for the hard
disk (§18.2 rules 10–13): a driver-backed volume has no FAT-size cap, gets
1..63 sectors per track and 1..255 heads, and rule 13 checks the declared
total against **the sector count its driver registered** rather than against a
floppy geometry. A network block volume passes validation unchanged.

**What the DOS side serves decides what this is worth:**

| the DOS side serves | verdict |
|---|---|
| a **disk image file** (`OS8088.IMG`) on its hard disk | **Ship this.** No coherency hazard at all — DOS is not looking inside the file. Gives a 5150 with one floppy 32 MB of storage over a cable and no controller — but see §1.2.1: it is **a third the speed of that machine's own floppy and a eleventh of an ST-225**, so the value is that it needs no card, never that it is quick |
| a **real DOS partition** via `int 25h`/`int 26h` | works, and is genuinely "the contents of the remote system" — but see the two limits below |
| a **floppy in the DOS machine's drive** | works, and is the cheapest possible way to read a 1.44 MB disk on a 360 KB machine |

Two limits on serving a live partition, and they are why Stage 2 exists:

- **32 MB, hard.** §18.2 rule 8 refuses `TotSec16 == 0`, which is every volume
  of 65,536 sectors or more, because the whole kernel addresses an LBA in one
  word (§18.7). A DOS 3.3 machine's partitions are ≤32 MB and fit; a DOS 6.22
  machine's 500 MB drive does not, and cannot be made to without a change that
  reaches `dsk_clus2lba`, the run coalescer, `dsk_dirw_next` and `dskw_flush`.
- **Coherency.** DOS caches directory sectors and the FAT, and SMARTDRV caches
  writes. os8088 writing sectors underneath a live DOS is how a filesystem
  gets corrupted. Survivable — the server program owns the machine while it
  runs, and can commit and disable caching — but it is a hazard that has to be
  *managed*, where Stage 2 does not have one at all.

**And item 1 of the verdict table is needed here.** `dsk_xfer`'s driver branch
reads `[drv_svc + DSV_SIZE + DSV_BLK]`, which is class 2's row — so a network
driver must either declare itself `DRVC_DISK`, in which case attaching it
disconnects HDD.DRV (§51.2.1 is explicit that a class is a publication slot),
or the volume row must say which class owns it.

**The fix is that the row names its own class**, and the first draft of this
plan got it wrong by proposing a new *kind* (`DVK_NET`) instead. A kind
answers "how is this volume reached"; the question here is "*by whom*", and
those are different — Stage 2 needs a file-served volume that could belong to
any class too, and encoding each combination as a kind is how you end up with
four of them.

The row is exactly 16 bytes and full:

```
DV_KIND(1) DV_UNIT(1) DV_FLAGS(1) pad(1) DV_SECS(2) DV_SEG(2) DV_LBL(8) = 16
```

…but **`DV_LBL` is 8 bytes for a label nothing sets.** §26.4 is explicit:
"nothing gives a volume a `DV_LBL` of its own today (§52.4 — the kernel names
them)", and since that section the caption is `A:` rather than `Disk A`. The
longest string the kernel ever puts there is `HDD C` — five characters and a
NUL. So:

```
DV_KIND(1) DV_UNIT(1) DV_FLAGS(1) DV_CLASS(1) DV_SECS(2) DV_SEG(2) DV_LBL(7) = 16
                                  ^^^^^^^^^^                       ^^^^^^^^
                                  the pad byte, spent               8 -> 7
```

**`DV_CLASS` costs zero bytes of `.bss` and zero instructions**: it takes the
existing pad byte, `DV_SIZE` stays 16 so `dsk_vol_row`'s index stays a
shift rather than a multiply, and `DV_LBL` keeps a byte of slack over the
longest label there is. `dsk_xfer`'s `mov bp, [drv_svc + DSV_SIZE + DSV_BLK]`
becomes a lookup through `drv_cls_svc` on `[bx+DV_CLASS]` — which is a routine
that already exists, because §51.2.1's per-class publication needed it.

Two consequences to hold on to. **`DV_CLASS` = 0 on a BIOS volume**, so a row
that was never set carries the value that can never dispatch anywhere.
And **`dsk_vol_drop_drv` takes the class as an argument** instead of dropping
every kind-1 row: its own comment warns about exactly this shape — "a teardown
that says *this driver's* while meaning *every driver's* reads correctly right
up until a second driver exists" — and §51.8 records what it cost the last
time, when unticking *sound* unmounted every hard-disk partition.

### 2.2 File mode — the redirector

This is what the ask actually describes, and it is a redirect at the **file
layer**, not the block layer. The kernel branches on `DVK_FILE` and calls out
to a `DRVC_FILE` service table.

**It is deliberately not a network feature, and the name says so.** A volume
whose contents are served by *somebody else answering questions about files*
is a general shape, and the cable is only its first client. Three others are
plausible without inventing anything: a **serial** redirector over the port
`comscan` already surveys, a **RAM disk** that needs no FAT at all, and a
**host-filesystem passthrough** under an emulator, which would be worth a
great deal to this project's own harness. `DRVC_FILE` costs exactly what
`DRVC_NET` would have cost and forecloses none of them, so the general name
is free — and §20.8 rule 4 means it is free *now* and expensive later, because
the day a `.DRV` ships against `DRVC_NET` is the day the number is frozen.

**The key insight that keeps File Manager at zero:** the staged §19.1
directory entry carries a **"first cluster" word at offset 18**, and only two
consumers ever interpret it — `dsk_chdir` for a folder, and the loader for a
package. Both of those are already redirect points. So on a redirected volume
that word is **an opaque handle the driver assigns**, the §19.1 entry format
does not change by one byte, and every consumer above it — the Disk window's
rows, its icons, its sort, the file dialog, the `..` synthesis, the type word,
`fm_hit`, the view cache — is untouched.

The service table:

```
FSV_LIST    list the current folder into disk_dir/disk_icons/disk_nfiles
            (the driver stages §19.1 entries; the kernel already owns the
            sort, §19.4, so the driver must NOT sort)
FSV_CHDIR   AX = a folder handle from an entry, or 0 = the root; DX = up
FSV_STAT    SI = name -> exists, size, attributes
FSV_READ    SI = name, ES:BX = buffer, DX:CX = capacity -> DX:AX = size
FSV_WRITE   SI = name, ES:BX = bytes, DX:CX = count
FSV_APPEND  the chunked pair (§18.4.4), so a copy still streams
FSV_READAT  ...and its read half
FSV_DELETE  SI = name
FSV_RENAME  SI = old, DI = new
FSV_MKDIR   SI = name
FSV_RMDIR   SI = name
FSV_DFREE   -> DX:AX = free bytes, BX = the granule
FSV_CPNAME/CPPAINT/CPCLICK/CPCLOSE   the Control Panel page (§31.9)
```

The branch sites in the kernel, counted:

| site | what the branch does | est. |
|---|---|---|
| `disk_mount` | `DVK_FILE` → `FSV_LIST`, skip BPB/FAT/root scan entirely | 30 B |
| `dsk_chdir` / `dsk_chdir_q` | → `FSV_CHDIR` | 30 B |
| `dsk_free_clus` | → `FSV_DFREE` | 20 B |
| `dskw_wbody` / `rbody` / `dbody` / `nbody` / `mkbody` / `rmbody` | → the six verbs | 90 B |
| `dskw_stat` / `dskw_apbody` / `dskw_read_at` | → three more | 45 B |
| `dsk_read_chain` (the loader, `assoc`) | → `FSV_READ` by handle | 25 B |
| class table, `DRVC_FILE`, `drv_fs_call`, `DV_CLASS` in `dsk_xfer` | the dispatch | 100 B |
| `desk.inc` — a network icon and its `[desk_pnet]` pointer | data, plus one branch | 40 B + icon |
| **total** | | **~380–460 B** |

Against 227 bytes of image slack and 107 of cold, that is **one rung of each =
1,024 bytes of the 1,536 spare, leaving one step against a standard of four**.
It is affordable and it is exactly the conversation `KERN_BUDGET` exists to
force — it should be asked for explicitly, with these numbers, before the code
is written.

**What Stage 2 buys over Stage 1:** any remote drive size, no 32 MB cap, no
coherency hazard (DOS does its own file I/O through `int 21h`, so its caches
stay coherent by construction), the remote machine's *real* filesystem, and —
not a small thing — the DOS side can serve anything DOS can reach, including a
CD-ROM, a RAM disk or a network drive of its own.

**What it gives up:** the association cache and the icon harvest. `ASSOC.DAT`
(§54.7) and the first-sector icon harvest both assume a volume you can read
sectors of. The driver can do the harvest itself — 64 bytes at offset 32 of
each `.O88`, which is one `FSV_READAT` per package — or hand back zeroed slots
and let §25's generic icon do its job, which is what `hello/` ships to prove.
**Zero the slots first and measure before adding the harvest**: at ~30 ms a
read it is a second of listing time for a folder of thirty packages.

### 2.2.1 Where the build stands, and where it picks up

**The interface is PINNED (SPEC.md §62.9) and the constants are in;
no branch site is built yet.** `DVK_FILE`, `DRVC_FILE` = 5, `DSV_FS` as a
pointer to the thirteen-verb table, and the fifth publication slot: 184
bytes. What is done is the part §1's rule says must come first.

Two design decisions were settled while pinning it and neither was in the
original sketch above:

- **`DSV_FS` is a POINTER, not thirteen more `DSV_*` cells.** The table is
  copied per class into the kernel's `.bss`, so thirteen cells would be
  charged to every machine including the ones with no driver at all.
- **The driver APPENDS to the listing through `OSAPI_FS_ENT`** rather than
  staging into it. `dsk_put_dir` is module-internal and knows whether this
  volume's listing is the `.lowbss` floor or a donated claim (§22.6) — three
  pieces of bookkeeping a driver must not hold. Gated to `FSV_LIST`.

**MILESTONE 1 IS BUILT AND VERIFIED** — SPEC.md §62.9.4 is the record, and it
came in at **`.text` +341, `.bss` +5, `.cold` +0: one image rung, four steps
of `KERN_BUDGET` left**, inside §0's 380–460 byte estimate. `drv_fs_call`,
`disk_mount`'s `DVK_FILE` branch, `dsk_xfer`'s refusal, `dsk_free_clus` →
`FSV_DFREE`, `OSAPI_FS_ENT`, `dsk_synth_up`'s banked parent handle, and
`DVK_FILE` awareness in the five volume-table routines that had been asking
`is this DVK_DRV` when they meant `is this a driver's`.

**The harness earned its keep on its first run.** `RAMDISK.DRV` needs no
hardware, so the whole path ran on a cycle-accurate 8088 in a container and
found two bugs before any of it went near a cable — one of them a NASM
line-continuation trap that assembles clean, leaves a service table one word
short, and presents as "the Disk window will not open" four call layers from
the cause. That is precisely what block mode did not have, and why two of ITS
bugs reached the field instead of the build.

**MILESTONE 2 IS BUILT AND VERIFIED TOO** (SPEC.md §62.9.5): reads, by handle.
Minesweeper launches off a redirected volume and Note Pad opens a document on
one — the loader's path and an application's, which are different because one
arrives holding a handle and the other a name. **`.text` +42, `.cold` +205, no
rung crossed**, so the footprint is unchanged and the spare is still four
steps; the `.cold` rung has 147 bytes left and milestone 3's six write verbs
will cross it.

The harness paid for itself twice more. §62.9.3's branch-site table was
**missing the loader's header peek** — SPEC.md §21 step 2 reads a file's first
sector to validate its `.o88` header before claiming a region, and `dsk_xfer`
refuses a redirected volume, so every package was `Bad package` while the
listing and the sizes were perfect. And the same step re-validates the entry's
first-cluster word against `[dsk_maxclus]`, which on such a volume compares an
opaque handle against **the last FAT volume's** cluster count: small handles
pass by luck, and a driver numbering its files from 4,096 would have had every
package refused on a volume that browsed and read correctly. Neither would
have been found on a cable without first being blamed on the cable.

**MILESTONE 3 IS BUILT AND VERIFIED** (SPEC.md §62.9.6): writes. New Folder,
Delete, Rename, a document saved back onto a redirected volume, and a package
copied across one and then RUN. **`.text` +0, `.bss` +4, `.cold` +213 — one
cold rung**, footprint 100,352 → 100,864, three steps spare.

Four bugs, and the two in the KERNEL are the same mistake in different
modules: `fcp_goto` and `fcp_rdnext` both range-check a "first cluster"
against `[dsk_maxclus]`, which on a redirected volume compares an opaque
handle against **the last FAT volume's** cluster count — and `fcp_goto` also
rejects anything below 2, so the copy engine refused the first subfolder any
`DRVC_FILE` driver ever hands out. `fcp_goto` had a second fault worth
carrying to the cable: its quiet path deliberately skips the mount, and a
mount is the only thing that calls `FSV_CHDIR` — so it moved `[dsk_cwd]`
behind the driver's back and every later name resolved in the folder the copy
came FROM. **A driver-side cwd is state the kernel can silently
desynchronise.**

**MILESTONE 4 IS BUILT AND VERIFIED** (SPEC.md §62.9.7): a FOLDER crosses.
The gap milestones 1-3 left was `fcp_scan`, which enumerates a source
directory by walking its **raw sectors** — and there are none. `FSV_ENUM`
(verb 24) hands back one entry by ORDINAL into `dsk_ent`, which is exactly
where `dsk_synth` puts one, so the two paths converge on the instruction
after it and the whole tail is shared rather than mirrored. The second
FAT-only site wanted a REFUSAL rather than an implementation: `fcp_relink`,
the same-volume move fast path, is raw directory slots end to end and is
already a fast path **with a fallback**, so a redirected volume declines it
and the copy engine — entirely built — does the move.

**`.cold` +50, footprint unchanged**; the driver grew 68
bytes. Read back from OUTSIDE with `os88flush`, because asking os8088 to list
what os8088 wrote is the writer and the reader agreeing: `DOCS/DEEP`,
`DOCS/DEEP/BOTTOM.TXT` 127 bytes, `DOCS/HELLO.TXT` 26 bytes, both bodies
byte-correct against their seeds.

The harness paid again, and by being READ rather than run: `rd_stage` spends
`BX` and `rd_enum` used it afterwards as the caller's buffer offset, so the
32 bytes would have landed somewhere in `KERNEL_SEG`. The first run **looked
like a pass on a screenshot** with that bug live, because the top-level
folder is made from the clipboard entry and never goes near the enumerate —
which is why the verification pulls the file bodies off the host rather than
counting rows in a window.

**…AND THE CABLE ONLY GOT IT TWO MILESTONES LATER** (SPEC.md §62.10.6). That
paragraph describes the RAM disk. `NET.DRV` carried `0` in the `FSV_ENUM`
cell, and §62.9.7's own probe — `fcp_mkroot` reading the cell with
`drv_fs_has` — turned that into an honest `Protected` notice rather than a
silent nothing, so the field reported it as *a folder that will not drag onto
or off the Link volume* and the diagnosis was already written down when it
arrived. The verb is `'E'` on the wire, one folder handle and an ordinal out,
a status and one 32-byte entry back; the DOS side walks it through the same
`srv_keep` a listing does, so the ordinal counts the rows the window shows.
Three statuses rather than two, because `CF=1, AX=0` is what an ABSENT verb
answers as well as what the end of a folder does — collapse them and an
unwalkable folder reads as an empty one, which is a paste reporting success
over a subtree it never copied.

**THE ACTIVITY BAR REPORTS ON A CABLE NOW** (SPEC.md §12.8.1), which is the
one thing three milestones of branch sites left visibly missing. §12.8's
widget took a SECTOR count, a redirected volume has none, so the single
slowest operation the whole redirector makes possible — 116KB of module at
3,741 bytes/second, half a minute of frozen screen — was the *only* file
operation in the machine that said nothing at all. `fpg_begin` takes bytes in
`DX:CX` now, which is the file API's own 32-bit count convention and
`FSV_STAT`'s answer pair, so the redirected arming site is a bare `call` with
no argument setup; every other producer already held a byte count and was
dividing it away. The trough stays 512-byte units internally, so `fpg_step` —
`dsk_xfer`'s per-sector caller, on the disk path for the life of the machine —
is untouched and still free.

**Converting the front door alone would have shipped a bar that lies.** A
redirected read is ONE far call, not a loop the kernel is standing in, so
between `FSV_READ` going out and coming back only the driver knows how far the
file has got — arm it and never step it and the bar sits at 0% for the whole
transfer. So `OSAPI_FS_PROG` (0x03C8) is the other half, and the contract is
"bytes since your last report" rather than a running total, with the sub-unit
remainder carried rather than rounded so a wire's 64-byte frames sum exactly.
`RAMDISK.DRV` reports in 64-byte pieces on purpose: a RAM disk delivers a file
in one `rep movsb`, and without that this slot would have shipped with no
consumer but the cable it was written for — which is precisely how
`OS88NET.COM` reached the field twice unexecuted. **`.text` +103, `.cold`
+23, footprint unchanged.**

The cable's file client is now a SECOND `DRVC_FILE` driver against a kernel
proven three milestones deep, which is what the whole RAM-disk detour bought.

**PHASE 1 OF THAT CLIENT IS DONE AND BOTH ENDS HAVE RUN** (SPEC.md §62.10.4).
`NET.DRV` is `DRVC_FILE`, publishes `FSV_LIST`/`CHDIR`/`DFREE`, and mounts a
browsable volume over the cable: Connect is `I C X L X` on the wire and
opening the volume is `C X L X F X`, with the Disk window showing a listing
that exists nowhere on either floppy. The DOS side gained the same three verbs,
a `(parent, name)` handle table walked upward to rebuild a path, the current
directory as its default root and `/W` for the whole machine.

Both halves ran on a cycle-accurate 5150 — which is the part §9 said could not
be done. The os8088 half is `tests/lptlink/partner.py` as the far end; the DOS
half is the *mirror*, `tests/dosstub` booting `OS88NET.COM` with `partner.py`
playing NET.DRV, and it needed nine more `int 21h` functions in the stub over
a nine-row synthetic tree. Five faults came out of it and four were silent —
a word store into a `db` that clobbered the next variable, an ordering that
made `/W` unmountable, a table row one byte short, a `make` knob that rebuilt
nothing, and a harness that was kinder than the thing it stood in for. SPEC.md
§62.10.4.2 has each of them.

**PHASE 2 IS THE READ PATH AND IT IS DONE, BOTH ENDS** (SPEC.md §62.10.4.3):
`FSV_STAT`, `FSV_READ` and `FSV_READAT`, which between them make a redirected
volume's files *readable* — and it is where `OSAPI_FS_PROG` finally got the
consumer it was written for. **It cost the kernel nothing at all**: the three
branch sites were built with the RAM disk and did not change, which is the
whole return on that detour.

**AND A PACKAGE HAS NOW BEEN LAUNCHED OFF THE WIRE**, which §10's table has
listed as owed since block mode. `MINES.O88` served from `tests/lptlink` and
double-clicked in the network Disk window is `READAT` for the loader's
512-byte header peek and then `READ` for all 1,517 bytes — and Minesweeper
runs, owning the menu bar with its own dock tile. Every byte arrived correct
and nothing in the harness had to check it: the loader validates the header
and the code then executes.

Two decisions in it are worth carrying forward. The **capacity goes out with
the command**, so an oversized file is cut at the source instead of crossing
the wire to be discarded — half a minute of cable for a 116KB module. And the
destination is **re-normalised on the same cadence as the progress report**,
because both want "often enough to be smooth, rarely enough to be free" and a
second constant is a second thing to keep in step.

It also found a pinned-protocol mistake worth naming: **`READ` and `WRITE`
collided with block mode's `NC_READ` and `NC_WRITE`** — the same one-byte
space, and on the DOS side the same command loop. §62.10.1's table had been
written from the verb names alone. They are `G` and `U` now; the tempting fix
was a mode flag, which is this tree's own second-opinion failure in a new
place.

**PHASE 3 IS THE WRITE PATH**: `FSV_WRITE`, `APPEND`, `DELETE`, `RENAME`,
`MKDIR`, `RMDIR` (SPEC.md §62.10.4.5). It cost the kernel nothing either — all
six branch sites came with the RAM disk, they share `dskw_fsop`, and none of
them changed. Six verbs, one shape: a folder handle, a name, whatever the verb
carries, and one `FERR_*` back.

That last word is where a real bug lived. **The file verbs had been answering
`2` for "no such thing", which is the BLOCK protocol's int 13h numbering and
is `FERR_IO` in the file protocol's.** Free while the driver mapped every
non-zero status onto a code of its own; not free the moment a status is passed
through, which is what a write does.

`/RO` is enforced in one routine every verb calls first, so a verb added later
inherits the refusal rather than remembering it — the machine at that end may
be somebody's real DOS box, and §FIELD-MACHINES keeps a live DOS 3.3 install
on the calibration machine's C:.

**Both ends are verified** (SPEC.md §62.10.4.5). The DOS side is twelve cases
against `tests/dosstub` - every refusal, and a final listing back to its
starting four so nothing leaked - and the os8088 side is `File > New Folder`
in a network Disk window, the one verb with no application behind it:
`M C L F` on the wire, and the folder appears in the window and on the far
side.

What is still owed at this boundary is the WIRE's verdict, which is unchanged:
two period boxes and a cable.

### 2.3 What was considered and rejected

**A synthetic FAT image in RAM.** The driver fetches the remote listing,
builds a FAT12 volume in a heap claim, and serves it through `DSV_BLK`.
Attractive because it is Stage 1's zero kernel cost with Stage 2's semantics —
and wrong three times over: the claim is tens of KB on a machine whose floor
is 128 KB, every write has to be reverse-engineered from sector traffic back
into a file operation, and a file bigger than the claim cannot be represented
at all. It is a filesystem written twice with a translation layer in between.

**Growing the LBA to 32 bits so a big DOS partition mounts in block mode.**
That is a change to `dsk_clus2lba`, the run coalescer, `dsk_dirw_next`,
`dskw_flush` and every `int 13h` caller, in a kernel with 227 bytes of image
slack, to remove a limit Stage 2 does not have. No.

**Putting any of it in File Manager.** There is nothing to put there. §3.

---

## 3. File Manager does not change, and here is the proof

`files.inc` is in `.cold`, so every call it makes into the kernel proper goes
through a named thunk. The complete list of thunks it uses that touch a disk:

```
cw_dsk_chdir      cw_dsk_get_dir    cw_dsk_relist     cw_dsk_dotdot
cw_dsk_free_clus  cw_dsk_vol_row    cw_dsk_copy_seg
```

…plus direct calls to `dskw_delete_x`, `dskw_rename_x`, `dskw_mkdir_x`,
`dskw_rmdir`, `dskw_rmtree`, `dskw_format`, `dskw_fmt_row`, `dskw_fmt_probe`,
`dskw_fmt_reach` and `dskw_char_x`.

**Not one of them mentions a FAT, a cluster, a BPB, a sector or `int 13h`.**
Every one is a redirect point, and the redirect goes *below* them. The same is
true of `fdlg.inc` (one `dskw_mkdir_x`, one `dskw_char_x`) and of
`filecp.inc`, which is the heaviest consumer and still only speaks
`dskw_stat_x` / `dskw_append_x` / `dskw_write_x` / `dskw_mkdir_x` /
`dskw_delete_x` / `dskw_rmtree`.

Three consequences worth stating because they are free and look like work:

- **`Format` must grey on a network volume.** §47 rule 3 — the predicate is a
  fact (this volume has no sectors to format), not a guess, so it greys rather
  than failing on click, and rule 1 means `gfx_pen_cf` and not a bare
  `CDGRAY`. §52.2.2 is the worked example of getting this wrong.
- **The status line's `Size … Free …`** comes from `cw_dsk_free_clus`, which is
  `FSV_DFREE`. It costs a round trip, so the driver should cache the answer
  per listing rather than per paint.
- **§22.8's dirty-folder mark works unchanged**, because it hangs off
  `dskw_sync`, which every successful file operation passes through.

---

## 4. The DOS side

An ordinary DOS `.EXE`, in C (Open Watcom or Turbo C) or assembler, that owns
the machine while it runs. It is not a TSR: a TSR sharing the port with DOS's
own printing and with mTCP's packet driver is a support burden with no upside,
and the machine it runs on is a transfer station, not somebody's desktop.

```
OS88NET [/P:378] [/D:C:\OS8088] [/IMG:file] [/RO] [/NET]
```

- **`/P`** — the port, and it is an **override, not a requirement**. With no
  `/P` the program runs §1.4's scan for itself: read its own `0040:0008`, add
  3BCh / 378h / 278h, dedupe, latch-probe each, and then **listen on every
  survivor round-robin** until one of them carries a hello. That is §9.5's
  "arm every candidate and let the one that delivers win", and on a DOS box
  with a whole machine to spend there is no reason to do anything cheaper.
  §1.4.5's dwell rule binds here: this side is the patient one.
- **`/D`** — the directory it exports as the root of the Network volume.
  Everything is resolved beneath it and `..` cannot escape it. That is the one
  security property this program has and it must be enforced on the *server*
  side, after path composition, never by trusting the client.
- **`/IMG`** — Stage 1: serve this file as sectors instead.
- **`/RO`** — refuse every write verb. The default for a first run.
- **`/NET`** — link mTCP in and answer the socket verbs (§5).
- A visible screen: link state, bytes moved, the last verb, and errors. The
  operator is standing at this machine and it is the only place either end can
  say what went wrong.

**Long names truncate to 8.3 on the way out**, because §19.1's staged entry is
8.3 and every consumer's truncation budget is built on that. Collisions get
the `~1` treatment DOS itself uses. Directories are served with `..` **omitted**
— §19.5's parent link is synthesized by the kernel and a second one would be a
duplicate row.

---

## 5. mTCP forwarding

### 5.1 The kernel change is one slot, and it is worth having anyway

There is **no way for a package to call a driver** today. `OSAPI_VOL_*` and
`OSAPI_DRV_CFG` exist and are fenced *to drivers*; a package has no route to a
driver's own services at all. So the socket API is either a new `OSAPI_NET_*`
family in the kernel — a dozen slots and a networking ABI the kernel has to
understand and then keep for ever under §20.8 rule 4 — or **one opaque cell**:

```
OSAPI_DRV_CALL   KERNEL_SEG:0x03D0   (the first free slot; the table
                 currently ends at 0x03D0)
    BH = the DRVC_* class, BL = a driver-defined verb
    AX/CX/DX/SI/ES = the driver's to define
    out CF = 1 with AX = 0 if no driver of that class is published, or it
             published no DSV_PKGCALL; otherwise whatever the driver answers
```

The kernel's whole knowledge of networking is then: none. It looks up the
class's published row, checks a `DSV_PKGCALL` cell, and far-calls it — which
is `drv_call` with a different table index, ~60 bytes.

**`DSV_PKGCALL` is the fence and it is not optional.** Without it a package
could name class 2 and reach `DSV_BLK`, which is a raw sector write to the
hard disk from any `.O88` on the floppy. A driver opts in by publishing the
cell; HDD.DRV publishes 0 and is unreachable.

This one slot is the right shape independently of mTCP — it is what every
future driver needs to have an API, and it costs less than the smallest
special-cased family would.

### 5.2 The socket API lives in the driver's SDK header

`drivers/net/os88net.inc`, published beside `os88drv.inc`, defining the verbs
a package passes in `BL`:

```
NETV_STATE    -> link up/down, mTCP up/down, the DOS side's IP
NETV_RESOLVE  ES:SI = a hostname -> a 4-byte address (async, see below)
NETV_OPEN     ES:SI = address + port -> a handle
NETV_LISTEN   port -> a handle
NETV_STATUS   handle -> connected / connecting / closed / bytes readable
NETV_SEND     handle, ES:SI, CX -> CX = bytes actually taken
NETV_RECV     handle, ES:DI, CX -> CX = bytes actually delivered
NETV_CLOSE    handle
```

Nothing in that list is in SPEC.md, nothing is a published kernel slot, and
all of it can change without a version bump — which is exactly right for the
half of this that is least likely to be right first time.

### 5.3 Synchronous file service, asynchronous sockets, one wire

This is the hardest part of the design and it has a clean answer.

**A file operation is a disk operation**: it runs under `[sch_lock]` with the
gfx lock held, it freezes the machine for the tens of milliseconds it takes,
and that is exactly what the floppy already does — §12.8's file-activity
widget reports it for free.

**A socket operation cannot be**, because a `recv` may block for seconds and
`connect` for tens of them. So:

- **Every `NETV_*` call is non-blocking.** It returns what is available now,
  and `NETV_STATUS` is how a package waits. This is the ordinary shape for a
  package that already has a worker task.
- **The driver's worker (`OSAPI_DRV_TASK`, §51.7) owns the polling.** It wakes,
  takes the wire if it is free, asks the DOS side for pending socket data,
  parks it in a ring, and yields. §51.7 rule 2 binds: `DRVV_DETACH` must not
  return until that worker is gone.
- **The mux is a busy flag and nothing more**, because §1.3's master/slave rule
  means the wire only ever carries a reply to something this side asked for. A
  file verb takes the flag; the worker's poll skips its turn if the flag is
  set. No queue, no reordering, no interleaving, no framing question about
  whose response arrived.

### 5.4 What this is realistically for

At 10–25 KB/s and 8-bit-bus latency, this is not a machine that will browse
the web. What it is good for is the thing a 4.77 MHz machine actually wants: a
**terminal**, a **file fetch**, an **NTP-ish clock set** against §37.90's
ladder, and — the honest one — the sheer novelty of a 1981 IBM PC holding a
TCP connection. Scope the first package accordingly: a Telnet client is about
the right size and it exercises `OPEN`, `SEND`, `RECV`, `STATUS` and `CLOSE`
with nothing else in the way.

### 5.5 One driver, and the socket half is an overlay

**One driver is the decision, and it is the right one for the reason it was
given**: the question a user should have to answer is *"is network on?"*, not
*"is the particular part of networking I want to use on?"* — so one `drv_tab`
row, one Control Panel page, one `.DRV`, one tick.

That collides with §7.1's real constraint, which is the 128 KB machine's
35.5 KB heap, and the collision is resolved the way this tree has already
resolved it twice. **The socket layer is an `OS88_OVERLAY`** (§52.11): a
second `.DRV` that `NET.DRV` loads *itself*, on first use, and frees when the
last handle closes. `hddtool.drv` is 10,753 bytes doing exactly this, and the
mechanism, the macro and the loader path all exist.

So the user still sees one thing to turn on, and a machine that only ever uses
the *drive* never has the TCP code in memory at all. The seam is where §5.1's
slot already puts it: `OSAPI_DRV_CALL` arrives at `NET.DRV`, which loads the
overlay if it is not resident and forwards. A package cannot tell, and should
not be able to.

**Two rules come with the overlay and both are §51.7's.** A load can be
refused — a 128 KB machine with a Disk window open may genuinely not have the
room — so `NETV_OPEN` returning "no memory" is an ordinary path a package must
handle, exactly as `mem_claim`'s refusal is everywhere else in this OS. And
`DRVV_DETACH` must not return until the overlay is unloaded and the worker is
gone, because `drv_unload` frees the image the moment detach returns.

---

## 6. The desktop, the page, and the icon

- **The volume row supports a label already and nothing uses it.** §26.4 notes
  that "nothing gives a volume a `DV_LBL` of its own today (§52.4 — the kernel
  names them), but a driver that did would otherwise letter outside the rect
  that cleans up after it." A network volume is the first plausible user of
  that field; the safe answer is to **let the kernel name it** `N:` like every
  other drive and leave `DV_LBL` unused, because §26.4's white rect hugs the
  caption and a longer label reopens a bug that has already been fixed once.
- **A new icon, and it is pure data.** `icon_draw` reads a `words, height`
  header, so `ico_net32` plus a CGA-aspect `ico_net14` (§26.4 — the CGA's
  pixels are 2.4:1, which is why the disk icon is 32×14 there) is a table and
  one pointer, not a draw path. Keying it off `DV_KIND`/`DV_CLASS` is one `cmp` in
  `desk_draw_zone`'s icon pick.
- **The Control Panel page is the driver's** (§31.9), and it should show the
  port, the link state, the far side's directory, bytes moved, and a
  **Connect / Disconnect** button. `DSV_CPNAME` is what makes the page exist
  at all, and clearing it at detach is what makes it disappear — that falls
  out rather than being arranged.
- **`SYSTEM.CFG`'s driver blob** (§51.9) is where the port address and the
  auto-connect flag live. 34 bytes, shared, and currently HDD.DRV's — **a
  second claimant is a second key, not a bigger blob**, and §51.9 says so
  explicitly. That is a small kernel change (a `NT` key beside `HD`) and it
  should be counted in Stage 1's budget, not discovered in Stage 3.

---

## 7. What it costs

### Kernel

| | image `.text` | `.cold` | `.bss` | rungs crossed |
|---|---|---|---|---|
| Stage 1 — `DV_CLASS`, class-aware dispatch, `NT` cfg key, icon | ~40 B | 0 | ~38 B | **image: yes** (227 left) |
| Stage 2 — the redirector | ~120 B | ~300 B | ~30 B | **image and cold** |
| Stage 3 — `OSAPI_DRV_CALL` | ~60 B | 0 | 0 | folded into Stage 1's rung |

Stage 1 crosses the image rung on its own (227 bytes of slack, ~78 bytes of
growth plus the icon data). Stages 1–3 together are **two rungs, 1,024 bytes**.

### 7.1 …and `kern_small` can afford it where `kern_big` cannot

The obvious worry is that the small build is the one that has to decline this.
Run the arithmetic and it comes out the other way:

| | spare before | rungs crossed | spare after | against the standard of four |
|---|---|---|---|---|
| `kern_big` | 1,536 (3 steps) | image + cold = 1,024 | **512 (1 step)** | **below — needs a raise** |
| `kern_small` | 3,072 (6 steps) | image + cold = 1,024 | **2,048 (4 steps)** | **exactly at it** |

Both builds cross the same two rungs, because both have less slack in each
than the change adds (big 227/107, small 135/100, against ~288 of image and
~300 of cold). But `kern_small` starts with twice the headroom, so it lands on
the four-step standard CLAUDE.md names, and `kern_big` lands one step under it
and owes the `KERN_BUDGET` conversation — the seventeenth, and the same shape
as the sixteenth.

**So "leave it out of `kern_small` to be safe" would have been the expensive
choice**, and not only on the arithmetic. Three further reasons, in the order
they matter:

- **The redirector is structural, not a feature.** The branch sites are inside
  `disk_mount`, `dsk_chdir` and nine `dskw_*` bodies — routines *both* builds
  run on every file operation. `%ifdef KERN_SMALL` around twelve of those
  leaves the two builds with **different file layers**, so every future change
  down there has to be reasoned about twice and tested twice, for ever. That
  is a permanent tax, where a missing feature is a one-line refusal.
- **The 128 KB machine wants this most.** It is the machine least likely to
  have a hard disk and most likely to be shuffling floppies — the case in §0
  is *strongest* on the smallest machine. This is KERNEL-MEMORY move 17's
  argument exactly ("a redraw optimisation is worth most on the slowest
  machine", which is why that move raised *both* guards), and move 15's
  "small should drift tighter" does not apply in this direction.
- **The `.DRV` is optional and the hook is not.** A 128 KB machine that never
  ticks the row pays one `drv_tab` entry and a file on the floppy. The hook
  being present costs it two rungs whether or not it is ever used, which is
  the honest price and is what the table above is measuring.

**The real constraint on a 128 KB machine is the heap, not the guard.** Its
heap is `int 12h` − 92.5 KB ≈ **35.5 KB**, and a three-stage `NET.DRV` at
~8 KB plus its buffers is a fifth of it — which is precisely the argument
§34.8 makes about the Sound Blaster tier's 12 KB DMA buffer. §5.5 is what to
do about that, and it is the same answer: make the expensive half optional.

### Disk

| | bytes |
|---|---|
| `NET.DRV` — transport, protocol, CP page (Stage 1) | ~4–5 KB, cf. `sound.drv` 5,503 and `hdd.drv` 6,657 |
| …plus the file verbs (Stage 2) | ~+2 KB |
| …plus the socket layer (Stage 3) | ~+1.5 KB |

The 360 KB system disk currently uses **139 of 354 clusters**, so there is
room for all of it. If it ever gets tight, §52.11's `OS88_OVERLAY` mechanism
already exists for splitting a driver's rarely-used half into a second `.DRV`
the driver loads itself — `hddtool.drv` is 10,753 bytes doing exactly that.

### The machine

Nothing, when the driver is not loaded. `drv_tab` ships every row
`DRVR_WANT = 0` (§51.3) and this one must too: probing three parallel port
addresses costs microseconds, but §51.3's rule is about the 5 KB read off the
floppy to be told there is no cable, and that argument is unchanged here.

**A sniff like `drv_snd_sniff` (§51.3.1) is tempting and should be resisted
for now.** Writing to an unknown port range on every boot is what §51.3.1
gated behind a knob for the Sound Blaster, and a parallel port with a printer
on it is a real machine that would get a page ejected at it. Reading the
BIOS's own `0040:0008` table to decide whether the row is even offerable is
free and sufficient.

---

## 8. What will break, in the order it is likely to

1. **`out base+2, 0`.** §1.4.3 — control bit 2 is INIT and it is **active
   low**, so the obvious way to put a parallel port into a known state resets
   whatever printer is on it. Read-modify-write preserving the low nibble,
   always, and restore the saved byte at detach. It is first on this list
   because it is the only item that damages something outside the two
   machines, and because the probe in §1.4.2 runs on ports the user has not
   told us anything about.
2. **The unplugged cable.** Every wait is bounded or the machine hangs with
   the gfx lock held. Test it by pulling the cable mid-transfer, not by
   never plugging it in.
3. **Two sweeps in phase.** §1.4.5 — both ends scanning at similar rates can
   miss each other indefinitely, and it presents as an intermittent cable
   fault rather than as a timing bug. The DOS side's dwell on one candidate
   must exceed os8088's whole sweep.
4. **The `dsk_vol_drop_drv` class gate.** Unticking the network driver must
   not unmount the hard disk. Its own comment predicts this bug; §51.8 records
   the last time it happened, when unticking *sound* unmounted every
   partition.
5. **The sector cache skips driver volumes on purpose, and this is the one
   volume that wants it.** `dsk_xfer` has `cmp byte [dsk_vkind], DVK_DRV /
   je .nocache`, on the reasoning that "a driver splits its own runs and has
   no revolution to save". That reasoning holds for IDE and is wrong for a
   cable, where a round trip is the dominant cost and reading to the end of a
   notional track is nearly free. **Measure it before changing it** — §18.95
   was simulated against a 400-entry real trace before it was built, and
   §19.2.3.1 is the negative result that came from not doing that.
6. **A DOS side that lies.** Every name, size, attribute and handle coming
   over the wire is attacker-controlled in exactly the sense §18.2 means it —
   an entry with a 40-character name or a size of 0xFFFFFFFF must be refused
   at the staging step, not passed to `font_str`. §19.1's "every byte outside
   0x21..0x7E replaced with `_`" rule applies to a remote name for the same
   reason it applies to a FAT one.
7. **Two masters on one filesystem** (Stage 1 with a live partition). Serve an
   image file first, and treat live-partition service as a separate decision
   with its own testing.
8. **`ES` on entry.** Every driver callback gets `ES = KERNEL_SEG`, and a
   `rep stosb` through it writes into the kernel. §56 records that exact bug
   in ModPlug; a protocol driver full of buffer fills is where it will happen
   next.

---

## 9. Staging, and what proves each step

| # | build | proved by |
|---|---|---|
| **1** | **`tests/lptlink`** — §9.1 | **DONE.** Link up first try, 16 KB each way, **0 errors in four transfers**, at **3,741 B/s** after §1.2.0's fix. PERFORMANCE.md Part 9 **Set 39**. The wire, the scan, the handshake and both reversals are settled facts now |
| **2** | **`NET.DRV` Stage 1 + `OS88NET`** | **DONE, on the iron.** A `Network` volume on the desktop, a Disk window listing it, and a text file DOUBLE-CLICKED open — the association resolved, Note Pad loaded and the document arrived, all across the cable (PERFORMANCE.md Part 9 **Set 40**). Everything above `dsk_xfer` worked unchanged, which was the whole bet. ~10 s for that open — but **the package came off the FLOPPY, not the cable**: `disk_mount`'s `asc_use` seeds §54.4.2's hint from the boot disk's `ASSOC.DAT`, so rung 1 finds `A:\APPS\NOTEPAD.O88` first. ~13 sectors crossed, ~1.4 s of it turnaround. A package launched OFF the wire is still owed, and it is `loader_run` from the network Disk window rather than any document open. The WRITE path too: a file copied on, the image carried back, `--verify` clean, four sectors changed and no others, and the file byte-identical to its source. Still owed: a re-run on the BATCHED protocol, whose framing is not the one any of this was measured on. *(Superseded detail:* The kernel half, the driver, the attach, the publication, the Control Panel page and the volume plumbing are all verified on a cycle-accurate 5150 (SPEC.md §62.7) — MartyPC has a parallel port with a readable data register but nothing on the far end of it, so `os8088_5150_cga_lpt` proves *everything except the partner*: the scan finds 0x378, the page says `No partner`, Connect fails in 2.1 s and the machine does not hang. The DOS end runs too, on `tests/dosstub` (SPEC.md §62.8) — which had to be built, because there is no DOS here and `OS88NET.COM` had therefore shipped twice without one instruction of it executing. the emulator half of this is SPEC.md §62.7's acceptance list, and `tests/dosstub` is how the DOS end is run without DOS.)* |
| 3 | `OSAPI_DRV_CALL` + `DSV_PKGCALL` | a package reaching a driver at all — testable with a stub verb before any networking exists |
| 4 | Stage 2, the redirector | the same Disk window over `OS88NET /D:C:\` — and the same `tests/filetest` battery, which is the existing 25-case write gate and applies unchanged |
| 5 | mTCP + a Telnet package | a connection, from a 1981 machine |

**Nothing here can be tested END TO END under QEMU or MartyPC**, and that is
the hard part of the whole plan: it needs two machines and a cable. But the
line falls further along than this section first assumed, and it is worth
being exact about where. MartyPC's `ParallelPort` has a readable data register
and a writable status register, so **the whole os8088 side up to the wire is
testable**: the latch probe, the port scan, `DRVV_ATTACH`'s refusal on a
machine with no port, the publication of class 4, the Control Panel page, the
bounded-timeout failure of `net_connect`, and the two-driver-page dispatch.
Two machine configs carry it — `os8088_5150_cga_lpt` (a CGA 5150 with a
Centronics card at 0x378) and `os8088_xt_hdd`, which gained one so that a hard
disk and a network link publish pages at the same time. What no emulator here
can do is answer as a PARTNER: the status lines read a constant, so
`mst_hello` always times out. 86Box has `char_pipe_lpt`
with nibble and DirectParallel modes (docs/DEBUG-PLAN.md §1.1 surveyed them),
so a *host-side* stub server against 86Box is the closest thing to a harness
and is worth building at step 1 — but the verdict on the protocol comes off
two period boxes and nowhere else.

### 9.1 Why the survey and the benchmark are one artifact

They were two steps and they are one, because they want the same code and the
same trip. A port survey that stops at "there is a latch here" cannot answer
§1.4.4's question — *is there a computer on the other end* — and answering it
means implementing the handshake; and once the handshake works, the throughput
figure is a loop around it. Splitting them means writing the transport twice
and asking for two trips to the machines to run them.

So `tests/lptlink` is **one source built two ways**, which is `comscan`'s own
shape (`-DCOMFILE` for a DOS `.COM`, plain for a **bootable** image whose
"kernel" is the diagnostic itself):

- `build/lptlink.com` — runs on the DOS machine, and is also the far end.
- `build/lptlink.img` — a bootable 360 KB floppy for the 5150, and
  `lptlink144.img` for a 1.44 MB drive.

**Neither end is os8088**, which is the whole point at this step: no kernel
byte moves, no driver exists yet, and a failure is a failure of the cable or
the protocol and cannot be anything else. It also survives as the permanent
field diagnostic, the way `comscan` did — the tool you reach for when this
does not work on somebody's machine, and the one that runs on a machine that
will not boot os8088 at all.

---

## 10. Questions, and what was answered

**Answered by the owner, and the plan above is written to them:**

1. **Does the 5150 have a parallel port?** — **Yes, on the GB101**, the
   Hercules-family LPT at 3BCh. The far end is a **DIO-500** multi-I/O card at
   an address nobody has read off it, and the point was never one machine
   anyway, which is why §1.4 is a scan on both sides rather than a constant
   anywhere.
2. **Stage 1 only, or all the way to Stage 2?** — **Stage 2**, on two grounds:
   a remote drive cannot be assumed to be under 32 MB, and *"this setup could
   be useful for other redirectors"*. The second reshaped §2.2 — the class is
   `DRVC_FILE`, the volume row names its class in a byte that already existed,
   and a cable is the first client of a general hook rather than the hook
   itself.
3. **`kern_small` too?** — **Yes, both builds.** §7.1 is the arithmetic, and it
   supports the decision more strongly than the reason given: `kern_small`
   lands *exactly* on the four-step standard and `kern_big` lands one step
   under it. The deciding argument is that the redirector is **structural** —
   the branch sites are inside routines both builds run on every file
   operation, so an `%ifdef` would leave the two builds with different file
   layers and tax every future change down there for ever.
4. **One driver, or two?** — **One.** *"Is network on?"*, not *"is the
   particular feature of networking I want to use on?"*. §5.5 reconciles that
   with a 128 KB machine's 35.5 KB heap by making the socket half an
   `OS88_OVERLAY` the driver loads on first use — one thing to turn on, and
   the TCP code is not in memory on a machine that only uses the drive.

**Still open:**

5. **Is the writer machine the intended far end?** The case in §0 assumes so,
   and it is much the strongest case. If the far end is a modern PC with a USB
   parallel adapter, most of those are printer-class devices that cannot do
   bit-level I/O and the answer is no. This does not block step 1 — `lptlink`
   will say what any given machine can do — but it decides how much the
   feature is worth.
6. **`kern_big`'s raise.** §7.1 says Stage 2 leaves it one step under the
   four-step standard, so it owes `KERN_BUDGET`'s seventeenth move. That is a
   conversation to have with these numbers in front of you, at the point Stage
   2's code is written and its real cost is measured rather than estimated —
   not now, and not by discovering it in a `kernsize` line.
