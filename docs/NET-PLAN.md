# os8088 parallel network plan

**Research document, not a contract.** SPEC.md is the binding contract for what
the kernel *is*; this is the study of what it would take to put a **LapLink
parallel link to a DOS machine** behind a loadable driver (SPEC.md §51) — a
`Network` volume on the desktop that the file manager browses and writes, plus
a path for a package to reach an **mTCP** connection on the DOS side. Nothing
here is implemented. Every interface named would land in SPEC.md *before* its
code.

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
| 1 | `dsk_xfer`'s driver branch dispatches through `[drv_svc + DSV_SIZE + DSV_BLK]` — **class 2, hard-coded**. A `DRVC_DISK` driver is a singleton (§51.2.1), so a network driver either *is* HDD.DRV's class and cannot coexist with it, or its block calls dispatch into HDD.DRV's table | a volume kind that names its own class | **~40 B** | **Stage 1** |
| 2 | The mount is `disk_mount` — BPB, FAT window, root scan. A volume whose remote side has *files* and no sectors has nothing for it to read | a `DVK_NET` branch at the mount, `dsk_chdir`, `dsk_free_clus` and nine `dskw_*` bodies | **~380–460 B** | Stage 2 |
| 3 | **There is no generic package→driver call.** `OSAPI_VOL_*` and `OSAPI_DRV_CFG` are fenced *to drivers*; nothing lets a package reach a driver's own service at all | one opaque slot at 0x03B8 | **~60 B** | Stage 3 (mTCP) |

Item 1 is the surprise. **Block mode is not free after all** — it is nearly
free, but the machine this project is calibrated against has an ST-225 on an
ST11M, so "a network drive that cannot coexist with the hard-disk driver" is a
real collision on the one machine that matters most.

### The recommendation

**Three stages, each shippable, each proving the one after it.**

| stage | what it is | kernel cost | what it buys |
|---|---|---|---|
| **1** | **Block volume** over the cable. The DOS side serves 512-byte sectors from an image file. `DVK_NET` + class-aware dispatch, and nothing else | **~40 B** | a working `Network` drive, read **and** write, with the whole FAT layer, the Disk window, the file dialog, package launching and the association cache **already working**. Proves the wire, the framing, the timing, the Control Panel page and the desktop zone at near-zero kernel risk |
| **2** | **File redirector.** A `DRVC_NET` service table the kernel dispatches for listing, chdir, read, write, delete, rename, mkdir, rmdir, stat and free space | **~380–460 B**, one image rung + one cold rung | the *actual contents of the remote system*, at any size, with no coherency hazard and no 32 MB cap |
| **3** | **mTCP forwarding.** One opaque `OSAPI_DRV_CALL` slot; the socket API itself lives entirely in the driver and its SDK header | **~60 B** | packages get TCP. The kernel learns nothing about networking, which is the point |

Stage 1 is worth building **even though Stage 2 supersedes it**, and §2.1 is
the argument. Stage 3 is independent of both and could be built second.

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

**The status bits are scattered and one is inverted**, so decoding a nibble is
`in al, dx` / `shr al, 3` / `xlat` against a 32-entry table, not a mask and a
shift. The table costs 32 bytes and removes five instructions from the hottest
loop in the driver.

### 1.2 What it costs, priced against this project's own measured numbers

PERFORMANCE.md's field table gives the one constant that decides this:

| quantity | field 5150 |
|---|---|
| **An ISA status-port `in`** | **8.7 µs** |
| Floppy throughput | 7,457 bytes/second |
| Floppy, per 512-byte sector inside a coalesced run | 65 ms |
| Floppy, per `int 13h` **call** | **~400 ms**, whatever it moves |
| Floppy, open and read a one-sector file | 810 ms |

A nibble handshake is roughly two `out`s and four `in`s — send data+strobe,
poll for the far side's acknowledge, read, acknowledge back, poll for release.
Six port accesses.

| model | per nibble | per byte | throughput |
|---|---|---|---|
| pessimistic — every access costs the measured 8.7 µs | 52 µs | 104 µs | **9.6 KB/s** |
| tuned — unrolled, `dx` held, `xlat` decode, ~5 µs/access, 4 accesses | 20 µs | 40 µs | **25 KB/s** |

So **10–25 KB/s**, or **1.3× to 3.4× the floppy**. Both figures are modelled
and PERFORMANCE.md Part 6's rule applies in full: **this must be measured
before it is quoted anywhere else.** The 8.7 µs figure is itself a poll-loop
iteration rather than a bare `in`, so the pessimistic column is a genuine
ceiling on the cost and not a guess in the other direction.

**The number that actually decides it is latency, not throughput.** A 512-byte
sector is **20–52 ms on the wire**. The floppy charges **~400 ms for any
`int 13h` call at all**. So a network volume is *dramatically* faster than the
floppy for exactly the traffic the FAT layer generates most of — a directory
sector, a FAT sector, a one-sector stat — and roughly comparable when
streaming a file. Opening and reading a one-sector file is 810 ms on the
floppy and would be ~60–150 ms over the cable.

That is a better result than it first looks, and it is what makes Stage 1
worth shipping rather than merely worth prototyping.

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

#### 1.4.1 The candidate set, and why the BIOS table is trusted here

Three addresses are the whole universe in practice, and they are the three the
POST itself scans, in this order:

| base | what puts it there |
|---|---|
| **3BCh** | the MDA / Hercules family — **the GB101, so the 5150's** |
| **378h** | the usual dedicated or multi-I/O card address |
| **278h** | the second one on a multi-I/O card |

**The BIOS has already done this scan and published the answer** at
`0040:0008`: four words, LPT1..LPT4, zero meaning absent. Read it first.

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
| a **disk image file** (`OS8088.IMG`) on its hard disk | **Ship this.** No coherency hazard at all — DOS is not looking inside the file. Gives a 5150 with one floppy a 32 MB hard disk over a cable, which is a real feature on its own |
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

The cheap fix is a **new volume kind rather than a new field**. `DV_KIND`
already exists and the row is exactly 16 bytes with no spare
(`KIND`,`UNIT`,`FLAGS`,pad,`SECS`,`SEG`,`LBL[8]`), so growing it costs
`DVOL_MAX` × 4 = 32 bytes of `.bss` and touches every reader. `DVK_NET = 2`
costs one `cmp`/`je` in `dsk_xfer` and one in `dsk_vol_drop_drv`'s class gate:

```
DVK_BIOS  equ 0
DVK_DRV   equ 1     ; DRVC_DISK's
DVK_NET   equ 2     ; DRVC_NET's - same DSV_BLK contract, different class row
DVK_FREE  equ 0xFF
```

`dsk_vol_drop_drv`'s comment already warns about this exact shape — "a
teardown that says *this driver's* while meaning *every driver's* reads
correctly right up until a second driver exists". A second block-capable class
is that second driver, and the gate has to learn the kind or unticking the
network driver will unmount the hard disk.

### 2.2 File mode — the redirector

This is what the ask actually describes, and it is a redirect at the **file
layer**, not the block layer. The kernel branches on `DVK_NET` and calls out
to a `DRVC_NET` service table.

**The key insight that keeps File Manager at zero:** the staged §19.1
directory entry carries a **"first cluster" word at offset 18**, and only two
consumers ever interpret it — `dsk_chdir` for a folder, and the loader for a
package. Both of those are already redirect points. So on a network volume
that word is **an opaque handle the driver assigns**, the §19.1 entry format
does not change by one byte, and every consumer above it — the Disk window's
rows, its icons, its sort, the file dialog, the `..` synthesis, the type word,
`fm_hit`, the view cache — is untouched.

The service table:

```
NSV_LIST    list the current folder into disk_dir/disk_icons/disk_nfiles
            (the driver stages §19.1 entries; the kernel already owns the
            sort, §19.4, so the driver must NOT sort)
NSV_CHDIR   AX = a folder handle from an entry, or 0 = the root; DX = up
NSV_STAT    SI = name -> exists, size, attributes
NSV_READ    SI = name, ES:BX = buffer, DX:CX = capacity -> DX:AX = size
NSV_WRITE   SI = name, ES:BX = bytes, DX:CX = count
NSV_APPEND  the chunked pair (§18.4.4), so a copy still streams
NSV_READAT  ...and its read half
NSV_DELETE  SI = name
NSV_RENAME  SI = old, DI = new
NSV_MKDIR   SI = name
NSV_RMDIR   SI = name
NSV_DFREE   -> DX:AX = free bytes, BX = the granule
NSV_CPNAME/CPPAINT/CPCLICK/CPCLOSE   the Control Panel page (§31.9)
```

The branch sites in the kernel, counted:

| site | what the branch does | est. |
|---|---|---|
| `disk_mount` | `DVK_NET` → `NSV_LIST`, skip BPB/FAT/root scan entirely | 30 B |
| `dsk_chdir` / `dsk_chdir_q` | → `NSV_CHDIR` | 30 B |
| `dsk_free_clus` | → `NSV_DFREE` | 20 B |
| `dskw_wbody` / `rbody` / `dbody` / `nbody` / `mkbody` / `rmbody` | → the six verbs | 90 B |
| `dskw_stat` / `dskw_apbody` / `dskw_read_at` | → three more | 45 B |
| `dsk_read_chain` (the loader, `assoc`) | → `NSV_READ` by handle | 25 B |
| class table, `DRVC_NET`, `drv_net_call`, `DVK_NET` in `dsk_xfer` | the dispatch | 100 B |
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
each `.O88`, which is one `NSV_READAT` per package — or hand back zeroed slots
and let §25's generic icon do its job, which is what `hello/` ships to prove.
**Zero the slots first and measure before adding the harvest**: at ~30 ms a
read it is a second of listing time for a folder of thirty packages.

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
  `NSV_DFREE`. It costs a round trip, so the driver should cache the answer
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
OSAPI_DRV_CALL   KERNEL_SEG:0x03B8   (the first free slot; the table
                 currently ends at 0x03B8)
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
  one pointer, not a draw path. Keying it off `DVK_NET` is one `cmp` in
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
| Stage 1 — `DVK_NET`, class-aware dispatch, `NT` cfg key, icon | ~40 B | 0 | ~38 B | **image: yes** (227 left) |
| Stage 2 — the redirector | ~120 B | ~300 B | ~30 B | **image and cold** |
| Stage 3 — `OSAPI_DRV_CALL` | ~60 B | 0 | 0 | folded into Stage 1's rung |

Stage 1 crosses the image rung on its own (227 bytes of slack, ~78 bytes of
growth plus the icon data). Stages 1–3 together are **two rungs, 1,024 bytes,
leaving one step of the three `kern_big` has spare.**

**`kern_small` must be checked separately and may say no.** It has 135 bytes of
image slack and 100 of cold against 6 steps of budget, and the whole reason
the split exists (docs/KERN-SPLIT-PLAN.md) is that the small build is allowed
to drift tighter. A parallel network driver on a 128 KB machine is a
reasonable thing to want and a reasonable thing to refuse; that is the owner's
call and it should be asked, not assumed.

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
| 0 | **`comscan` learns LPT** (§1.4.8). The BIOS table, the latch probe and the status byte, per candidate base | a report from the 5150 naming 3BCh, and one from the DOS machine naming whatever the DIO-500 is at. **No longer a gate** — the 5150's port is confirmed — but it is what tells the two ends where to start |
| 1 | The wire: a `tests/lptbench` that clocks bytes both ways | a measured KB/s figure, on the iron, into PERFORMANCE.md Part 9. Not the model in §1.2 |
| 2 | `NET.DRV` Stage 1 + `OS88NET /IMG` | a `Network` icon on the desktop, a Disk window listing it, a package launched off it, a file written to it, `os88disk.py --verify` on the image afterwards from the host |
| 3 | `OSAPI_DRV_CALL` + `DSV_PKGCALL` | a package reaching a driver at all — testable with a stub verb before any networking exists |
| 4 | Stage 2, the redirector | the same Disk window over `OS88NET /D:C:\` — and the same `tests/filetest` battery, which is the existing 25-case write gate and applies unchanged |
| 5 | mTCP + a Telnet package | a connection, from a 1981 machine |

**Nothing here can be tested under QEMU or MartyPC**, and that is the hard part
of the whole plan: it needs two machines and a cable. 86Box has `char_pipe_lpt`
with nibble and DirectParallel modes (docs/DEBUG-PLAN.md §1.1 surveyed them),
so a *host-side* stub server against 86Box is the closest thing to a harness
and is worth building at step 1 — but the verdict on the protocol comes off
two period boxes and nowhere else.

---

## 10. Questions for the owner

1. ~~**Does the 5150 have a parallel port?**~~ **Answered: yes, on the GB101**
   — the Hercules-family LPT at 3BCh. The far end is a **DIO-500** multi-I/O
   card at an address that has not been read off it yet, which is why §1.4 is
   a scan on both sides rather than a constant anywhere.
2. **Is the writer machine the intended far end?** The case in §0 assumes so,
   and it is much the strongest case. If the far end is a modern PC with a
   USB parallel adapter, most of those are printer-class devices that cannot
   do bit-level I/O and the answer is no.
3. **Stage 1 only, or Stage 1 → 2?** Stage 1 is ~40 bytes of kernel and gives a
   working network drive over an image file. Stage 2 is ~400 more and two rungs
   for the remote machine's real filesystem. Both are defensible; Stage 2 is
   what the ask describes.
4. **`kern_small` too, or `kern_big` only?** §7. The small build has 135 bytes
   of image slack and this is exactly the sort of feature the split exists to
   let it decline.
5. **Does mTCP forwarding go in the same driver, or a second one?** One driver
   is one file and one Control Panel page; two means `DRVC_NET` for the drive
   and another class for sockets, and classes are a scarce, `.bss`-costing
   resource. One driver with a `/NET`-capable server is the recommendation.
