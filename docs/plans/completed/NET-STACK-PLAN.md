# os8088 networking — the socket API, mTCP forwarding, and an Ethernet card

**Research document, not a contract.** SPEC.md is the binding contract for what
the kernel *is*; this is the study of what it would take to give os8088 **TCP
sockets from two completely different directions** — forwarded over the
parallel cable from mTCP on a DOS box, and terminated locally on an 8-bit
Ethernet card — and to have a package see one thing. Every interface named
here lands in SPEC.md *before* its code.

docs/plans/completed/NET-PLAN.md is the prior work and is not superseded: its stages 1 and 2
are **built and on the iron** (SPEC.md §62.9, §62.10), and its §5 is the first
sketch of the socket half. This document is stage 3 and what comes after it.

docs/plans/completed/BROWSER-PLAN.md is the other half of the same ask — the consumer. It is a
separate document because its problem is not networking at all.

---

## 0. The verdict, up front

**The two sources are not two versions of one feature. They differ in where
TCP terminates, and that single fact sets the price of each.**

| | the cable | the card |
|---|---|---|
| where TCP terminates | **the DOS box**, inside mTCP | **os8088** |
| protocol code in os8088 | **none** | ARP, IP, ICMP, UDP, TCP, DNS, DHCP |
| kernel change | **~60 bytes, one cell** | the same ~60 bytes, and nothing more |
| driver | ~1.5 KB added to `NET.DRV` | **~9–10 KB, a new `ETHER.DRV`** |
| Drivers page | already has its row | **needs a fifth row, and there is no room** |
| testable in this container | **yes, both ends** | **yes** — 86Box, and this is a reversal |
| testable on real period iron | two boxes and a cable | 5150 #2 has an NE2000 |

**So the recommendation is: build the API once, forward over the cable first,
and let the card arrive behind an interface that is already proven.**

Three findings decide the shape, and each is a fact about this tree rather
than a preference:

1. **`DRVC_NET` = 4 already exists and its publication slot is already
   allocated.** `drv_owner` and `drv_svc` are `DRVC_MAX`-sized arrays and
   `DRVC_MAX` is 5, so classes 1..5 each have a row in `.bss` today. Class 4
   was block mode's and has been vacant since `NET.DRV` became `DRVC_FILE`
   (SPEC.md §62.10). **A socket-serving class therefore costs zero new
   `.bss`** — the 42 bytes `os88drv.inc` warns a class costs have been spent
   already.
2. **The kernel never learns what a socket is.** One opaque cell —
   docs/plans/completed/NET-PLAN.md §5.1's `OSAPI_DRV_CALL` — takes a class in `BH` and a
   driver-defined verb in `BL` and far-calls whoever published the class. That
   is `drv_call` with a different table index. Both implementations arrive
   through it and a package cannot tell them apart, **which is the whole
   design**: it is SPEC.md §62.9's argument in a second place, where the class
   was made `DRVC_FILE` rather than `DRVC_NET` precisely so a cable would be
   the first client of a general hook instead of being the hook.
3. **It fits in the slack that is already there.** Measured on this tree
   today, `kern_big` has **204 bytes left in its image rung** and the cell
   plus its dispatch is ~58 of them. **`KERN_BUDGET` untouched** — and
   `kernel.asm`'s own comment on move 16 says this is what that move was
   granted for.

### What it is honestly for

docs/plans/completed/NET-PLAN.md §5.4 said, at 10–25 KB/s, "this is not a machine that will
browse the web", and then the field measured the cable at **3,741 bytes per
second** (PERFORMANCE.md Set 39) — a fifth of that machine's own floppy. That
sentence is right about the cable and **wrong as a general statement about
os8088**, which is why this document exists: a card is not a cable, and the
gap between them is roughly an order of magnitude.

What is realistic, in the order it is worth building:

- **A terminal.** Telnet exercises the whole API with nothing in the way. It
  is the right first consumer and it works acceptably at 3,741 B/s, because a
  terminal's data rate is a human's typing speed.
- **A file fetch.** HTTP GET to a file, no rendering. Useful immediately, and
  it is the transfer case docs/FIELD-MACHINES.md's seven-step path exists to
  avoid.
- **Setting the clock.** SPEC.md §37.90's ladder ends at a BIOS that on a 5150
  cannot set a clock at all; an SNTP packet is 48 bytes and would give that
  machine a correct date for the first time.
- **A text-and-table browser** — docs/plans/completed/BROWSER-PLAN.md, and the finding there
  is that on a card the browser is **render-bound, not fetch-bound**, so it is
  the transport that stops being the problem.

**And one thing that is not realistic and must be said plainly: HTTPS.** A
TLS handshake is public-key arithmetic — an RSA-2048 private operation is
minutes on a 4.77 MHz 8088 with no multiplier worth the name — and the modern
web is essentially all HTTPS. Nothing in this plan reaches a site on the
public internet directly. What it reaches is a LAN server, a period service,
or **a transcoding proxy**, and docs/plans/completed/BROWSER-PLAN.md §8.3 treats that as a
first-class configuration rather than a workaround, because over the cable the
DOS box is already in the room running mTCP and is the obvious place to put
one.

---

## 1. One API, two implementations

### 1.1 The cell, and why it is generic

There is **no way for a package to call a driver** today. `OSAPI_VOL_*` and
`OSAPI_DRV_CFG` are fenced *to drivers*; nothing lets a package reach a
driver's own service at all. The alternative to one opaque cell is an
`OSAPI_NET_*` family — a dozen slots and a networking ABI the kernel has to
understand — and SPEC.md §20.8 rule 4 is unfrozen only while this tree hosts
every caller, so that is a dozen contracts to get right rather than one.

```
OSAPI_DRV_CALL      the first free cell (0x0428 when this was written;
                    0x0448 as built, the integration branch having taken
                    0x0428 for OSAPI_DRV_DLG in the same round)
    BH = the DRVC_* class, BL = a driver-defined verb
    AX/CX/DX/SI/DI/ES = the driver's to define
    out CF = 1, AX = 0   no driver of that class is published, or it
                         published no DSV_PKGCALL
        otherwise        whatever the driver answers
```

**`DSV_PKGCALL` is the fence and it is not optional.** Without it a package
could name `DRVC_DISK` and reach `DSV_BLK`, which is a raw sector write to the
hard disk from any `.O88` on the floppy. A driver opts in by publishing the
cell; `HDD.DRV` publishes 0 and is unreachable. It goes at `DSV_SIZE` = 28,
taking the table to 30 and costing **2 × `DRVC_MAX` = 10 bytes** of `.bss` —
the same arithmetic `driver.inc` already records for `DSV_FS`, and the same
reason `DSV_FS` is a pointer rather than thirteen cells.

**Built as described** (SPEC.md §20.11), with two corrections worth recording
because both were found by writing it rather than by reading it. `ES` is not
"the driver's to define": it is **the calling package's segment**, put there
by the X stub, which is the whole reason the cell needs one — and it is the
only driver entry point in the machine where `ES` is not `KERNEL_SEG`.
And **`DI` is an ARGUMENT**, not something the driver may answer in and not
something the dispatcher may spend: a verb taking `ES:DI` is the ordinary
shape for one that fills a buffer. `drv_pkg_call_x` used it as scratch in its
first version and a receiving verb wrote the page into the caller's segment
at an offset nobody chose — SPEC.md §20.11 has the whole shape, because it is
a transfer that succeeds and delivers elsewhere. `BP` really is spent (the
header's dispatcher is `call bp`), so a verb answers in
`AX`/`BX`/`CX`/`DX`/`SI` and `CF`.

### 1.2 The verbs

**Built as `drivers/net/netpkg.inc`** (the name this section guessed at was
`os88net.inc`), published beside `os88drv.inc` and included by both ends,
reached through `DSV_PKGCALL` with the class in `BH` — and the class is
**`DRVC_FILE`**, not `DRVC_NET`: §0's finding 1 read the vacant class 4 as
where this would go, and a driver publishes in ONE class, which for `NET.DRV`
has been the redirector's since SPEC.md §62.10. Sockets are a second service
of the same driver, not a second driver. **Nothing here is a kernel slot and
nothing here is in SPEC.md**, which is exactly right for the half least likely
to be right first time.

```
NETV_STATE     -> link up/down, stack up/down, our IP, the gateway, the DNS
NETV_RESOLVE   ES:SI = a hostname   -> a request handle (never blocking)
NETV_OPEN      ES:SI = addr + port  -> a socket handle
NETV_LISTEN    port                 -> a listening handle
NETV_ACCEPT    a listening handle   -> a NEW connected handle, or none yet
NETV_STATUS    handle -> connecting / connected / closing / closed / failed,
                         and how many bytes are readable now
NETV_SEND      handle, ES:SI, CX -> CX = bytes actually taken
NETV_RECV      handle, ES:DI, CX -> CX = bytes actually delivered
NETV_CLOSE     handle
```

**Every verb is non-blocking, and that is the load-bearing rule.** A file
operation may freeze the machine for tens of milliseconds because that is what
the floppy already does and SPEC.md §12.8's widget reports it. A `connect` is
seconds and a `recv` may never complete at all, so a blocking socket call would
be a hang with the gfx lock held — SPEC.md §37.90's bounded-loop rule, in the
place it bites hardest. `NETV_STATUS` is how a package waits, which is the
ordinary shape for one that already has a worker task (SPEC.md §20.6).

**`NETV_RESOLVE` returns a handle rather than an address** for the same
reason: a DNS lookup is a round trip on the cable and a UDP exchange with a
timeout on the card, and neither can be answered inside the call.

**`NETV_ACCEPT` is separate from `NETV_LISTEN`, and §1.5 is why.** mTCP's own
model is that a socket listens and *becomes* the connection, which is enough
for one inbound connection at a time and is not enough for a server that must
stay reachable while it is serving. Splitting them costs one verb.

**The API must carry at least four simultaneous handles.** Three is the real
floor — an FTP server is a control connection plus a data connection plus a
listener that must stay open across both — and a browser fetching while
something else is connected wants the fourth. It is a table size in the
driver, so choosing four now costs bytes and choosing it later costs a
protocol change.

### 1.3 Why two implementations behind one class is safe

SPEC.md §51.2.1: one class is one publication slot, so **two drivers cannot
both publish `DRVC_NET`** — ticking whichever is second is refused with
`DRVE_TWICE`. The cable's socket half and the card are therefore mutually
exclusive at run time, which is the same stated limitation the RAM disk and
the cable already carry as `DRVC_FILE`, and it is the right answer rather than
a defect: a machine with both should use the card, and choosing is a tick.

**A package must not care which is attached, and `NETV_STATE` is what keeps it
honest.** It reports whether there is a stack and what its address is, and a
package that branches on anything else is coupled to a transport.

---

### 1.4 SPEC.md §20.6 rule 7 has to be amended, and this is easy to miss

**Rule 7 is an allowlist, not a denylist.** It names what a worker task may
call — `gfx_*`, `font_*`, `wm_content`, `osapi_get_ticks`, `osapi_mouse`,
`task_sleep`, `task_yield`, `OSAPI_TASK_ALIVE` and a few more — and says
everything marked *UI-task/window-callback context only* is forbidden to it.
A cell that does not exist yet is on neither list.

**The whole non-blocking design in §1.2 assumes the worker is what polls**, so
if the package's route to a driver is not worker-callable, there is nothing to
poll with and the design collapses back to blocking calls on the UI task. So
the cell must be added to rule 7's allowlist **in the same change that adds
the cell**, and it must be true rather than merely asserted:

- the cell itself takes no lock, raises no `sch_lock` and touches no shared
  kernel scratch — it is a table lookup and a far call, so this is a property
  of ~50 bytes and is easy to guarantee;
- **whether a given verb is worker-safe is the driver's claim, not the
  kernel's**, because `DSV_PKGCALL` is opaque by construction. `os88net.inc`
  marks each verb, and a driver that would block or touch `dsk_secbuf` behind
  one must say so.

There is a precedent for exactly this shape and it is worth copying rather
than re-deriving: `OSAPI_SND_TONE` is worker-safe and the SDK did not say so
until Arkanoid needed it (SPEC.md §44), while `osapi_snd_play` blocks with
`sch_lock` raised and is out. Same cell family, opposite answers, and the
distinction lives in the documentation because nothing enforces it — rule 7
ends "**None of this is enforced.**"

**Done, in the same commit as the cell.** Rule 7 now names `OSAPI_DRV_CALL`
and says which half of the safety is the kernel's; `drivers/ramdisk/rdpkg.inc`
is the reference for the other half, and it marks both of its verbs.

### 1.5 Serving, not only fetching — what an FTP server needs from this API

An FTP server is named as a later consumer and it is the one that can be
designed out of by accident, because **it inverts the direction of every
assumption the cable was built on**. Nothing about it breaks this design, but
four things have to be true from the start and three of them are free.

**It needs `LISTEN` and `ACCEPT`** — §1.2, and they are in.

**It needs several sockets at once.** FTP is a control connection and a data
connection, and passive mode needs a second listener; §1.2's four-handle floor
covers it.

**Over the cable, the 5150 has no address of its own.** mTCP on the DOS box
owns the IP, so an inbound connection arrives *there* and the DOS side
forwards it — which works, and which means the server is reachable at the DOS
box's address. On a card the stack is local and the question does not arise.
Worth stating plainly, because "my FTP server is on the wrong IP" is otherwise
a confusing first experience.

**And docs/plans/completed/NET-PLAN.md §1.3's master/slave rule needs one word of precision
rather than an exception.** It says os8088 "never receives unsolicited data",
and a server sounds like the opposite. It is not: this side still initiates
every exchange on the wire, and an inbound connection is something the far end
*reports when polled*. The rule is about **wire traffic**, not about who
started the TCP connection — so §2.2's busy flag, the whole mux, survives
untouched, and the only cost is that an inbound connection waits up to one
poll interval (~55 ms at `OSAPI_TASK_SLEEP 1`).

#### 1.5.1 The one real constraint: a worker may not write a file

SPEC.md §20.6 rule 7 again, and here it bites rather than being a formality.
**The file slots are UI-task/window-callback only** — they share `dsk_secbuf`,
the FAT snapshot and `sch_lock` — so the worker that drains a socket **cannot
write what it receives to disk**. An FTP server is socket-to-file by
definition, so this is its central design problem and not a detail.

The answer exists and is proven: **the worker stages, the UI task commits.**
Frotz does exactly this for `@save` (SPEC.md §61.6) — the VM worker cannot
raise a file dialog, so it stages a Quetzal image into its own claim, sets a
request word, and waits on the ordinary input path until a UI-task callback
sees the flag and does the file work.

Two consequences worth having written down before anybody builds it:

- **A transfer cannot stream straight to disk.** It goes through a bounded
  staging buffer that the UI task drains, so the buffer size is a real design
  parameter rather than an implementation detail, and a large upload is a loop
  of stage-and-commit rather than one write.
- **Every buffer is claimed before the worker starts**, because `OSAPI_MEM_*`
  is not on rule 7's list either. Frotz's `zi_load` is the model: story,
  stack, save buffer and scrollback, all up front.

Neither of those is a reason to change anything now. They are reasons the API
in §1.2 is the right shape, and the note that stops someone designing a
streaming server that cannot be built.

---

## 2. The cable: forwarding mTCP

### 2.1 What is already there

Everything below the socket layer. `drivers/net/lplink.inc` is the transport,
`%include`d by **both** `NET.DRV` and `tests/lptlink` so a wire fix cannot
drift between the diagnostic and the thing it diagnoses; the four-phase
handshake, the turnaround guard, the tick-based deadlines and the port scan on
both ends are all measured facts (docs/plans/completed/NET-PLAN.md §1.2.0/1.4.0). The DOS
side, `OS88NET.COM`, already has a command loop, a handle table and sixteen
`int 21h` functions behind it.

So stage 3 is: **`/NET` links mTCP in, eight more one-byte verbs join the
command loop, and the driver grows an overlay.**

**Two of those three were wrong, and the os8088 half is BUILT anyway**
(SPEC.md §62.11). Taking them in order:

- **The verbs are seven, not eight, and they are LOWERCASE.** `NETV_RESOLVE`
  is reserved in the numbering and not implemented — `NETV_OPEN` takes a
  *name*, so the lookup lives inside `NSK_CONNECT` and nothing in §7's
  staging ever wants an address without a connection. The case is that the
  one-byte space had six free capitals for seven verbs (§62.10.1 shares it
  with block mode's four and file mode's fifteen), so the socket layer took
  the lowercase half outright.
- **The overlay is not needed here.** §2.3's premise was a driver carrying
  TCP code a small machine might never use; the TCP is the *far side's*, so
  what this half contains is seven command letters and a four-row handle
  table. The whole driver went 4,207 → 5,215 bytes. §2.3 still holds for
  `ETHER.DRV`, which is where a real stack lands.
- **"`/NET` links mTCP in" cannot be done at all**, and that is the finding
  worth carrying forward — see §2.4.

**What IS built and measured**: the ABI (`drivers/net/netpkg.inc`, included
by both ends), the driver's half (`drivers/net/netsock.inc`), the wire
frames, the two-task mutex, and `tests/socktest` — a package with a worker
that fetches a page over the cable and checks the exact bytes. Its far end is
`tests/lptlink/partner.py`'s `SocketBox`, which is **real host sockets**, so
everything above the DOS side is exercised today with no cable, no card and
no DOS in the container.

**It passes** on a cycle-accurate 5150: ten wire commands
(`o s w r r s r r s c`), 232 bytes arriving in four reads of 65, 0, 167 and 0,
`HTTP/1.0 200 OK` at the front of them, every handle back afterwards. The two
zero-length reads are the point — the first is followed by more data and the
second is the end, and only the socket's STATE tells them apart.

**Two bugs it found, and both are the kind this staging exists to catch.**
`OSAPI_DRV_CALL` was entering a verb with `DI` clobbered, so `NETV_SEND` was
perfect and `NETV_RECV` put the page in the caller's segment at an offset
nobody chose — a transfer that succeeds, counts correctly and delivers
elsewhere (SPEC.md §20.11). And `RAMDISK.DRV` shares `DRVC_FILE` with
`NET.DRV`, so `NETV_OPEN` and `RDPV_UPCASE` were the same number at the same
address; verb 0 is `PKGV_IDENT` for every package door now (§20.11.1).
Neither is about networking, and neither could have been found by reading.

### 2.2 The mux is a busy flag, and that is all

docs/plans/completed/NET-PLAN.md §5.3 got this right and it is worth restating because it is
the part that looks hard. **os8088 is the master and never receives
unsolicited data** (§1.3 there): every exchange is request-then-response,
driven from this side. So there is no queue, no reordering, no interleaving
and no framing question about whose response arrived. A file verb takes the
flag; the driver's worker (SPEC.md §51.7) skips its poll turn if the flag is
set.

**One amendment to that flag, and it is new here.** When the mux was written,
both claimants were the UI task. §1.4 makes the *worker* a claimant too, so
the flag is now touched by two tasks that pre-empt each other — it must be
taken and released with interrupts off, not with a plain test-and-store. This
is `task_spawn`'s own lesson (SPEC.md §20.6): its slot scan and `T_STATE`
publish went under one `cli` the first time two tasks could spawn at once.

What that costs is **latency, not correctness**: socket data only moves when
this side asks, so the poll interval is the floor on responsiveness. A worker
waking on `OSAPI_TASK_SLEEP 1` polls at 18.2 Hz, which is far finer than
3,741 B/s can fill.

### 2.3 The socket half is an overlay

SPEC.md §52.11's `OS88_OVERLAY` — a second `.DRV` the driver loads itself on
first use and frees when the last handle closes. `hddtool.drv` is **11,224
bytes** doing exactly this, so the mechanism, the macro and the loader path
all exist.

The reason is the 128 KB machine. The memory ladder this build produces puts
the heap at `0x1aa0` = **106.5 KB**, so a 128 KB machine has **21.5 KB** of
heap and a 640 KB machine has **533 KB**. A driver that carried the socket
layer resident would spend a tenth of the small machine's heap on code it may
never use. The user still sees one thing to turn on — docs/plans/completed/NET-PLAN.md §10
question 4, *"is network on?"* rather than *"is the particular part of
networking I want on?"* — and a machine that only ever uses the drive never
has the TCP code in memory.

Two rules come with it and both are SPEC.md §51.7's. A load can be **refused**
on a small machine, so `NETV_OPEN` answering "no memory" is an ordinary path a
package must handle. And `DRVV_DETACH` must not return until the overlay is
unloaded and the worker is gone, because `drv_unload` frees the image the
moment detach returns.

**Built, and it is NOT an overlay** (SPEC.md §62.11.5). The argument above is
about a driver carrying a TCP stack, and the cable's socket half is not one:
the TCP is the far side's, and what this half contains is seven command
letters and a four-row handle table. Measured, the whole of `NET.DRV` went
**4,207 → 5,215 bytes** — an overlay would buy back about a kilobyte of a
21.5 KB heap, for a loader path, a free path and a `DRVV_DETACH` that has to
wait for it. The paragraph above is kept because it is right where it was
aimed: `ETHER.DRV` in stage E carries a real stack, and that is the one to
build an overlay for.

### 2.4 "Forwarding mTCP" is not a thing a `.COM` can do

**mTCP is a set of APPLICATIONS, not a library.** There is no TSR to call, no
interrupt to raise and no linkable object — Michael Brutman's stack is
compiled into each of its programs. So the sentence this plan opened with,
*"`/NET` links mTCP in"*, describes something that cannot be written, and the
DOS half of the cable needs **a TCP implementation of its own**, against a
DOS packet driver.

Two things follow, and they are why the os8088 half was built first rather
than waiting.

**That implementation is the same work as stage E's.** A packet driver on the
DOS side and an 8390 on ours differ in how a frame is handed over and in
nothing above that, so ARP, IP, ICMP, UDP, DNS and TCP should be written once
and shaped so both ends can use them — which also makes stage E cheaper than
this plan costed it, not dearer.

**And it changes nothing above the wire.** `partner.py`'s `SocketBox` is a
complete far end with real sockets, so the verbs, the mutex, the frames and
every package above them are testable now; when the DOS stack lands, what
changes is who answers the seven letters. That separation is exactly what
SPEC.md §20.11's door was for, one level up.

**What it does NOT change is the browser's schedule.** Stage D was always
fetch-bound over the cable and is now fetch-bound over `partner.py`, which is
a *better* harness than a DOS box for everything except the wire's verdict:
it can serve a page byte-exactly, split a write to force a partial read, and
refuse a connection on demand.

---

## 3. The card: one driver, one chip, three front-ends

### 3.1 The survey

The user asked for "one driver per standard as makes sense". **The standards
make less difference than they look like they do**, and that is the finding
that shapes this section:

| board | bus | NIC | how the host reaches a packet |
|---|---|---|---|
| **NE1000** | 8-bit ISA | DP8390 | PIO through a remote-DMA data port |
| **NE2000** | 16-bit ISA | DP8390 | the same port, 16 bits wide |
| **3C503 EtherLink II** | 8-bit ISA | DP8390 | **shared memory** window, or PIO |
| **WD8003 / SMC** | 8-bit ISA | DP8390 | **shared memory** window |

**All four are the same NIC.** National Semiconductor's DP8390 (and its 83C690
clones) does the ring buffer, the address filter, the CRC and the collision
back-off in every one of them; what differs is the *window* the host copies
through and the handful of board registers that open it.

So: **one `ETHER.DRV`, one 8390 core, a thin front-end per board.** That is
"one per standard as makes sense" answered by measurement rather than by
taste, and three further facts make it the only workable answer:

- **The Drivers page is full.** Rows are `CP_DROWH` (26) apart from `CP_DR0Y`
  (24) in a `CP_CH` (132) pane, so a fifth row's caption falls off the bottom,
  and the panel cannot grow because CGA is 200 rows against `wm_fit`'s 155
  ceiling (SPEC.md §31.1). `DEBUG.DRV` was already unlisted — and has since
  been removed outright (SPEC.md §58) — to make room for
  the RAM disk. Three ethernet rows are not available at any price; **one is,
  and it still needs §3.5's change.**
- **One class is one publication slot** (SPEC.md §51.2.1), so two ethernet
  drivers could not both publish `DRVC_NET` anyway.
- **A shared core is this tree's own idiom.** `lplink.inc` is `%include`d by
  the driver and the diagnostic for exactly this reason.

The Control Panel page reports **which board answered**, which is the
diagnostic that matters and is free once the probe is a ladder.

### 3.2 A 5150 can only take some of these, and it matters

The target is an 8-bit ISA machine. **NE2000 is a 16-bit card**; many clones
negotiate down and work in an 8-bit slot and many do not, and the ones that do
are running an 8-bit data path anyway. NE1000, 3C503 and WD8003 are 8-bit
parts by design.

**Recommendation: write the 8390 core against the 8-bit path and treat 16-bit
as an optimisation that never runs on the calibration machine.** That is
SPEC.md §52's gating argument in another place — the hard-disk driver's rung 1
is gated on `CPU_286` because an 8088's `in ax, dx` loses the drive's high
byte, and the same instruction is what a 16-bit NE2000 transfer would use.

### 3.3 The 8088 has no `INS`, and that is the throughput ceiling

**`INS`/`OUTS` and `REP INSW` are 80186 instructions.** `kernel.asm` opens
with `cpu 8086` and the build uses `-w+error`, so the transfer loop out of an
NE2000's data port is `in al, dx` / `stosb` / `loop` — **one discrete bus
access per byte**.

PERFORMANCE.md Part 2 measures **an ISA status-port `in` at 8.7 µs** on the
field 5150. So:

| | µs per byte | bytes/second | a 1514-byte frame |
|---|---|---|---|
| **PIO** (`in al, dx` + `stosb` + loop) | ~10–12 (model) | **~85–100 K** | ~16 ms |
| **shared memory** (`rep movsw` through the card's window) | ~3 (model) | **~330 K** | ~4.5 ms |
| for scale: RAM `rep stosw` | 1.76 (measured) | 570 K | |
| for scale: the cable | 267 (measured) | **3,741** | |

**The shared-memory boards are architecturally about three times faster on
this machine**, and that is a genuinely counter-intuitive result: the NE2000
is the card everyone has and the card every emulator models, and it is the
slower one here — because the instruction that makes PIO fast does not exist
on this CPU.

> **Every figure in that table except the two marked measured is a MODEL, and
> PERFORMANCE.md Part 6's rule applies in full: it must be measured on real
> hardware before it is quoted anywhere else.** The two µs/byte estimates are
> derived from Part 2's `in` cost and its `rep stosw` and framebuffer-word
> ratios, not from a run. §5 is where they get measured.

That does **not** make NE2000 the wrong first target — §5 says it is the
right one, for testability — but it does mean the driver should be structured
so a shared-memory front-end is a peer of the PIO one from day one, and it
means **nobody should be surprised when a WD8003 beats an NE2000 on a 5150**.

### 3.4 The receive window is CPU flow control, not network flow control

This is the sharp edge of putting a stack on a 4.77 MHz machine and it does
not exist on the cable at all.

A 10 Mbit segment delivers **1.25 MB/s**. The 8088 drains a card at, by the
model above, **85–330 KB/s** — a mismatch of four to fifteen times. The 8390's
on-card ring is **8–16 KB**, which is six to ten full frames, so a sender
running flat out overruns the ring in **milliseconds**.

Three consequences, all of which have to be designed in rather than
discovered:

- **Advertise a small TCP receive window — one or two segments, ~2 KB.** The
  window is what stops the far end sending faster than this machine can
  copy. It is not a tuning parameter here; it is the flow-control device for
  the *CPU*, and a large window on this machine is an overrun generator.
- **The drain must be bounded per interrupt.** SPEC.md §37.90's rule again: an
  ISR that loops until the ring is empty, on a busy LAN, never returns. Take a
  fixed number of frames and leave the rest for the next entry.
- **Broadcast traffic is the hazard nobody plans for.** A modern LAN carries
  constant mDNS, NetBIOS and DHCP broadcast, and ARP requires broadcast
  reception, so it cannot simply be filtered off. A 5150 on a busy switch port
  will spend real time in the ISR receiving frames it discards. **Measure the
  idle cost on a live network before believing any throughput number**, and
  expect the honest deployment to be a quiet segment.

### 3.5 A fifth driver row needs SPEC.md §31.1's scrolling list

`DRV_MAX` is 4 and every row is spoken for: sound, hard disk, RAM disk,
parallel link. `SYSTEM.CFG`'s want bitmap is a **word**, so sixteen rows fit
there and the settings file is not the constraint — the *page* is, and
`driver.inc`'s own comment says the answer: "a fifth driver wants the
scrolling list that section already names as the answer, not a taller window."

**That is a prerequisite of shipping any Ethernet driver and should be costed
with it rather than discovered.** It is also the one part of this whole plan
that is pure kernel and touches no networking.

One trap it carries, already recorded: the want bitmap is **one bit per row by
index**, so appending is free and inserting renumbers every saved
`SYSTEM.CFG`. `ETHER.DRV` appends.

### 3.6 What a stack costs, calibrated against this tree

Against the driver sizes this build actually produced — `net.drv` 4,207,
`sound.drv` 5,531, `hdd.drv` 7,128, `hddtool.drv` 11,224:

| | estimate |
|---|---|
| 8390 core (ring, init, address filter, TX/RX) | ~1.5 KB |
| front-ends: NE1000/NE2000 PIO, 3C503, WD8003 | ~400 B each |
| ARP + cache | ~400 B |
| IP + ICMP echo | ~600 B |
| UDP | ~250 B |
| **TCP** — state machine, retransmit, window, timers | **~3.5–4 KB** |
| DNS resolver | ~700 B |
| DHCP client | ~600 B |
| Control Panel page, config blob, probe ladder | ~800 B |
| **total** | **~9–10 KB** |

Comfortably an `OS88_OVERLAY` of `hddtool.drv`'s size, and the 360 KB system
disk this build produced uses **235 of 354 clusters — 119 KB free**. It fits.
`combo.img` is tighter (304 of 354 per CLAUDE.md) and would have to choose.

Host-side buffers are heap: a 2 KB receive window and a 2 KB retransmit buffer
per connection, so **~8 KB for two connections**. On a 640 KB machine's 533 KB
heap that is nothing; on a 128 KB machine's 21.5 KB it is a third, which is
§2.3's argument arriving in the other implementation.

---

## 4. What the kernel actually changes

| | `.text` | `.bss` | `.cold` | rung |
|---|---|---|---|---|
| `OSAPI_DRV_CALL` cell + dispatch | ~50 | 0 | 0 | — |
| the cell's table entry | 8 | 0 | 0 | — |
| `DSV_PKGCALL` (`DSV_SIZE` 28 → 30) | 0 | **10** | 0 | — |
| **stage 3 total** | **~58** | **10** | 0 | **none crossed** |
| SPEC.md §31.1's scrolling driver list | ~150? | ~4 | ~100? | **measure** |

Measured slack on this tree today, `make` fresh:

```
kernsize[big]: footprint 107,520 of 108,544 -> 1,024 spare (2 steps)
               image rung 204 left    cold rung 329 left
               .text+.bss 57,140 of 65,536 -> 8,396 left
```

**The socket cell fits in the 204 bytes already in the image rung.** The
scrolling list does not obviously fit in what is left after it and is the item
that will ask `KERN_BUDGET` its seventeenth question — which is a conversation
to have with a `kernsize` line in front of you at the point that code is
written, exactly as docs/plans/completed/NET-PLAN.md §10 question 6 says, and not now.

**Everything else in this document is driver and package.** The kernel learns
nothing about ARP, IP, TCP, DNS, HTTP or HTML, and that is the property to
protect: SPEC.md §20.8 rule 4 is unfrozen only while this tree hosts every
caller of every slot, and a networking ABI in the kernel would be the largest
thing ever to bet on that.

---

## 5. Testability — and this is a reversal

docs/plans/completed/NET-PLAN.md §9 ends "**Nothing here can be tested END TO END under QEMU
or MartyPC**", and for the cable that is still true: the status lines read a
constant, so `mst_hello` always times out. **For the card it is false, and
that is the strongest practical argument for building it.**

| harness | what it can do |
|---|---|
| **86Box** | **the whole path.** `src/network/net_ne2000.c` registers `ne1k` and `novell_ne1k` — **8-bit ISA, so an XT can take one** — alongside the NE2000s, with `net_01_card` / `net_01_net_type = slirp` in the config. 3C503 and WD8003 are modelled too. So a real HTTP GET from an emulated XT is reachable **in this container**, which the cable never was |
| **MartyPC** | **nothing.** No network device — so `make marty`, this tree's *default* test target, is blind to all of it. That is worth stating plainly: Ethernet is the first subsystem here whose primary harness is 86Box rather than MartyPC |
| **QEMU** | `ne2k_isa` exists, but QEMU is the emulator furthest from the target and has no XT. Not a reason to start it |
| **5150 #2** | **has an NE2000**, via the Picomem (docs/FIELD-MACHINES.md). A real 8088 bus, real timing on the CPU side |
| **5150 #1** | the calibration machine, kept **entirely period** on purpose. An NE2000 in it is exactly the "just put a Gotek in it" proposal that register pre-refuses. A *period* NE1000 or 3C503 would not be — but that is its owner's decision |

**The Picomem caveat is binding and is the register's own rule**:
docs/FIELD-MACHINES.md says of machine #2 that "every storage timing on this
machine is the Picomem's, not a drive's — so **nothing from here goes into
PERFORMANCE.md Part 2**". The same applies to its NE2000. **It can prove the
driver correct; it cannot produce §3.3's missing numbers.** Those need a
period card in a period machine, and until somebody runs one, §3.3's table
stays marked as a model.

That split — 86Box for the protocol, machine #2 for the bus, a period card for
the numbers — is the same three-tier discipline docs/TESTING.md already
imposes everywhere else, and it means the risky half is the half that can be
iterated in a container.

---

## 6. docs/plans/completed/DEBUG-PLAN.md said no to Ethernet, and that does not bind here

It has to be addressed, or a reader finds "**Ethernet / NE2000 — no.**" in
this tree and concludes the question was already settled. It was — for a
**debug channel**, which is a different question. Its four reasons, taken in
turn:

1. *"It costs a driver and a stack … against ~200–400 bytes for a polled UART
   stub."* **True, and it is the price of the product rather than of the
   plumbing.** For a debug channel the stack is overhead; here the stack *is*
   the feature. The comparison that matters is not against a UART stub, it is
   against having no networking.
2. *"It cannot ever run where the numbers come from."* **Partly answered.**
   That was written when the register held one 5150. Machine #2 exists now and
   has an NE2000, and §5 says exactly what it can and cannot settle.
3. *"Its one real argument was bandwidth, and section 3 takes it away"* — a
   memory dump goes out through a disk. **Does not apply.** Nothing here is a
   dump.
4. *"It adds a whole class of failure — DHCP, SLiRP's NAT, host firewalls."*
   **Applies, and stands.** It is a real cost, and it is why §7 puts a static
   address ahead of DHCP and a LAN server ahead of the internet.

So there is no contradiction, but there is a thing to keep true: **this must
not become the debug channel by the back door.** The serial monitor (removed,
SPEC.md §58) and
the MartyPC debugger are the answers there, for the reason DEBUG-PLAN gives —
a channel that works on the emulator *and* the iron beats a fast one that
works on the emulator alone.

---

## 7. Staging

Each step is shippable and each proves the one after it. **The ordering's one
real claim is that D comes before E**: the expensive, uncertain rendering work
should be finished against a transport that already exists and is measured,
so that when the card lands the only new thing is the card.

| # | build | proves |
|---|---|---|
| **A** | ✅ **BUILT** — `OSAPI_DRV_CALL` (slot 0x0448) + `DSV_PKGCALL`, two verbs in `RAMDISK.DRV`, `tests/drvcall` | a package reaching a driver at all — testable in a container before any networking exists. SPEC.md §20.11. Measured: `.text` +103, `.bss` +10 (estimated ~58; the extra is the fence and the class arithmetic). **No rung on `kern_big`; one on `kern_small`, which is now at 0 spare** — its rung had two bytes left, so the next change to that build owes a decision either way |
| **B** | ✅ **the os8088 half is BUILT** — `netpkg.inc`, `netsock.inc`, seven lowercase wire verbs, the two-task mutex, `tests/socktest`. ⬜ the DOS half is **its own piece** (§2.4) | a package holds a TCP connection over the cable and fetches a page, byte-exact, against real host sockets. SPEC.md §62.11. **`/NET` cannot "link mTCP in"** — mTCP is applications, not a library — so the DOS side needs a stack of its own, and that stack is the same work as E's |
| **C** | ✅ **BUILT** — `apps/telnet/telnet.asm`, and `apps/os88line.inc` with it | the API with no rendering in the way. A dumb terminal: RFC 854's three bytes, every option refused, a worker that polls and a keystroke ring that DROPS rather than blocking. SPEC.md §70 |
| **D** | ✅ **BUILT** — `apps/browser/brnet.inc`, a location bar and `File ▸ Open Location…` | the rendering half needed **no change at all** to gain a network, which is what the plan claimed when it kept fetching out of the layout work. HTTP/1.0 + `Connection: close`, so no chunked decoder. SPEC.md §71. **Measured**: seven wire commands for a page, `GET /net.htm HTTP/1.0` as the SERVER saw it, `br_nlines` = 14 |
| **D′** | **the first real site** — and the code for it is IN | a search on frogfind.com: `GET`, a form, and simple markup. `br_submit` composes the URL (BROWSER-PLAN §7.4) and now FETCHES it, so what is left is a partner with a route to the internet rather than any more browser |
| **E** | ✅ **BUILT** — SPEC.md §31.1.1's scrolling driver list, then `ETHER.DRV`: an 8390 core, ARP/IP/ICMP/UDP, stop-and-wait TCP, DHCP and DNS, a Control Panel page, and §72.7's Setup window for a LAN with no DHCP server. SPEC.md §72 | the same `NETV_*` verbs from a card. **Measured**: DHCP binds 10.0.2.15, and the browser fetches a page over TCP with `GET /eth.htm HTTP/1.0` as the SERVER saw it and `br_nlines` = 14. The "without one byte changing" claim came within ONE question of holding — see below |
| **F** | ✅ **BUILT** — `apps/ftpd/ftpd.asm`, `NETV_ADDR`, `tests/ftpd.py`. SPEC.md §77 | §1.5 — `LISTEN`/`ACCEPT`, four handles and the stage-and-commit pattern, all of which A put in place. It is the first thing here that makes os8088 a *server*, and the answer to docs/FIELD-MACHINES.md's seven-step path in the other direction. **Measured**: a real `ftplib` client lists, downloads byte-exact (text *and* every byte value), uploads 20,000 bytes through the stage-and-commit loop, and the file is read back off the image by a FAT12 reader that shares no code with the writer. §1.5.1's "central design problem" is `OSAPI_WM_ONWAKE` — the worker stages, the UI task commits, `[fd_req]` is the whole handshake. **It needed one verb and found two bugs**: PASV cannot be answered without the machine's own address (§1.3 promised it in `NETV_STATE` and never delivered it), and nothing in this tree had ever LISTENED, so closing a listener did not destroy its pending queue — one refused transfer stranded the fourth handle and the *next* transfer hung |

**A and B are small and the browser is not**, so the two lines should run in
parallel rather than in sequence: the browser's own steps 0–4
(docs/plans/completed/BROWSER-PLAN.md §10) need no network at all, and D′ is the point where
the two lines meet.

**What A settled, now that it is built.** Three things, and the second is the
one that could not have been settled by reading:

- **The door is one cell and the kernel learned nothing.** `drv_pkg_call_x`
  finds the class's publication slot, checks the fence and far-calls the
  driver's own dispatcher — the same three steps `drv_fs_call` takes, and
  none of them is about networking. Every socket verb this plan describes is
  the *driver's* contract now, in `drivers/net/netpkg.inc`, included by both
  ends the way `drivers/ramdisk/rdpkg.inc` already is.
- **`ES` really is the package's, and it had to be proved rather than
  argued.** `RDPV_UPCASE` writes through `ES:SI` into the caller's own image
  and the gate reads the bytes back out of it. §8's first entry is the
  mirror-image hazard in the same register, and the two together are why the
  X stub is documented at both ends: a driver here gets the *package's*
  segment, and everywhere else it gets `KERNEL_SEG`.
- **The fence is real and `HDD.DRV` is behind it.** The gate checks the
  refusal from both directions — before any driver is published, and against
  a verb number the driver does not have — because a check that only ever
  sees the passing state is one that cannot fail for the reason it claims.

What A did **not** settle is anything about the wire. That is B, and the
first thing it needs is the verb numbering in §1.2 written into
`drivers/net/netpkg.inc` and answered by `tests/lptlink/partner.py`.

Within E, the internal order that de-risks it was: 8390 core and a receive that
prints frame counts → ARP → ICMP echo (a `ping` from the host to an emulated
XT is the first end-to-end proof and it is one packet) → UDP → DNS → TCP →
DHCP last, because a static address removes DEBUG-PLAN's fourth objection from
the critical path.

**What E settled, now that it is built.** Four things, and only the first was
predicted:

- **The claim was "without one byte changing" and it cost ONE question.** A
  package names a publication CLASS, and `ETHER.DRV` is `DRVC_NET` where
  NET.DRV is `DRVC_FILE` — it serves no volume, so it has no business in the
  redirector's slot, and a machine may have both attached. So `NET_CLASS`
  stopped being an `equ` and became `[net_cls]`, a byte `apps/os88sock.inc`'s
  `net_find` fills by asking each candidate class for `NETV_IDENT`. **Every
  `mov bh, NET_CLASS` already written assembles unchanged** — the operand went
  from an immediate to a memory byte — so what the three packages actually
  lost is the five-line IDENT block `net_find` subsumes. The card is tried
  first, because a machine with both should not use the 3,741 B/s one.
- **The gate had to leave MartyPC, and that is the whole of why.** MartyPC has
  no network card of any kind, so the emulator this tree develops on cannot
  host this driver. `tests/ethernet.py` is QEMU's, on CLAUDE.md's short list
  beside the 286/386 targets — and it asserts BEHAVIOUR only, because a
  machine that is not an 8088 has no timing worth reading.
- **Both bugs that cost real time were REGISTERS, not protocol.** `tcp_in`
  pushed six registers and popped five, so its `ret` took the saved `DX` as a
  return address and the machine ran off into the driver's own data with
  everything else still working; and `eth_pkg` dispatched with
  `mov di, [tab+bx] / call di`, which destroys **DI — `NETV_RECV`'s
  destination offset** — so the reply landed in the caller's segment at the
  address of the verb about to run. The kernel's own `drv_pkg_call_x` had that
  second one in stage A. **The wire was right both times**: a pcap showed a
  correct three-way handshake, a correct GET and a correct 507-byte reply
  while the browser reported a failure.
- **DHCP found the mistake this file should have predicted**: `mem_copy`
  preserves DI, so a caller composing options has to advance it. Without the
  two `add di, cx`, the REQUEST's option 50 was overwritten by the options
  after it, the server answered NAK, and the retry path reset the counter it
  was being measured against — so DISCOVER went out twelve thousand times in
  six seconds and it looked like a deadline bug.

---

## 8. What will break, in the order it is likely to

1. **`ES` on entry.** Every driver callback gets `ES = KERNEL_SEG`, and a
   `rep stosb` through it writes into the kernel. SPEC.md §56 records that
   exact bug in ModPlug; a protocol driver full of buffer fills is where it
   happens next, and a network buffer is the worst possible thing to scribble
   through a live kernel.
2. **An unbounded drain.** §3.4. On a quiet bench a ring never fills and the
   loop always terminates; on a real LAN it does not. Test on a busy segment,
   not on a crossover cable.
3. **The receive window sized like a modern one.** §3.4 again. 64 KB is a
   correct TCP window and a broken one here.
4. **A blocking socket verb.** §1.2. It will look fine on a fast emulator with
   a local server and hang the machine with the gfx lock held the first time a
   host does not answer.
5. **Everything arriving over the wire is hostile**, in exactly the sense
   SPEC.md §18.2 means it. A DNS answer with a 300-byte name, a
   `Content-Length` of 0xFFFFFFFF, a TCP segment claiming to be 64 KB — every
   one refused at the parse, not passed to a buffer or to `font_str`. SPEC.md
   §19.1's "every byte outside 0x21..0x7E replaced with `_`" rule applies to a
   remote name for the same reason it applies to a FAT one.
6. **`in ax, dx` reaching an 8088.** §3.2. It assembles under `cpu 8086`, it
   runs, and it silently loses the high byte — SPEC.md §52's rung 1 is the
   worked example. Any 16-bit path is gated, not assumed.
7. **The class collision.** §1.3. Attaching the card while the cable's socket
   half is up must be a clean `DRVE_TWICE` refusal and not two drivers
   half-publishing. SPEC.md §51.8 records what the last one of these cost,
   when unticking *sound* unmounted every hard-disk partition.
8. **A `.DRV` shipped against a class number that later moves.** `DRVC_NET`
   is 4 and vacant *today*; `drv_check` binds a `drv_tab` row's class to the
   image header's, so the row and the driver's `OS88_DRIVER` move together or
   the driver is refused at load.

---

## 9. Questions for the owner

1. **Which card is actually available to test on real iron?** Machine #2's
   NE2000 is a Picomem, so it proves the driver and not the bus. Is there a
   **period** NE1000, 3C503 or WD8003 in the room, or reachable? §3.3's whole
   table is a model until one exists, and the answer might change which
   front-end is written first.
2. **Is the far end of the cable staying?** docs/plans/completed/NET-PLAN.md §10 question 5 is
   still open, and it decides whether B–D are the main line or a stepping
   stone to E.
3. **Static address or DHCP first?** Static is smaller, removes a whole class
   of failure, and is what a bench LAN wants. DHCP is what a real network
   wants. Recommendation: static first, DHCP in E's tail.
4. **Does the browser justify SPEC.md §31.1's scrolling driver list on its
   own?** That change is the real kernel cost of Ethernet and it benefits
   every future driver. It should be asked for with a measured `kernsize`
   line, not folded silently into `ETHER.DRV`'s commit.
5. **Is the FTP server os8088 serving, or os8088 connecting out?**
   **SETTLED: serving.** §1.5's harder reading was the one built — os8088 as
   the server, so a modern machine can reach the 5150's disks — because that
   is what "do file operations remotely" most usefully means and because a
   client is a strict subset of it. SPEC.md §77.
