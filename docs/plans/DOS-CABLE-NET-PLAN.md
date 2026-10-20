# DOS-CABLE-NET-PLAN.md — a DOS program on the wire, over a parallel cable

**A machine with a parallel port and no network card is the machine `NET.DRV`
exists for.** SPEC.md §96.23 gave the DOS box a packet driver over
`ETHER.DRV`'s raw verbs, which is a *card* feature: on a cable-only machine a
DOS program gets nothing, because mTCP speaks packet driver and nothing else.

This is how it gets something better than nothing — and the design's whole
claim is that "better than nothing" undersells it by a factor of three.

SPEC.md §96.23 is the contract for the DOS-facing half and §72.22 for the
driver half; this file is the design record for the middle.

---

## 1. The obvious answer is a raw relay, and the arithmetic refuses it

The cable already carries a private protocol (`nwire.inc`) and the far end
already has a real packet driver against a real card (`drivers/net/pktdrv.inc`
— "a packet driver where the NE2000 was"). So the obvious design is two new
wire commands, `NW_RAWTX` and `NW_RAWRX`, and frames cross the cable.

**It does not work, and the reason is latency rather than bandwidth.**

| | |
|---|---|
| cable throughput (PERFORMANCE.md Set 39) | **3,741 B/s** |
| a 1,514-byte frame, one way | **0.405 s** |
| protocol frames per data frame (FTP-PERF, measured) | **2.32** |
| so: seconds per 1.5KB of payload | ~1.34 → **~1.1 KB/s** |
| a single round trip (SYN out, SYN\|ACK back) | **0.81 s** |

That last row is the one that kills it. mTCP's initial retransmit timeout is
about a second, so **every round trip sits on the edge of its RTO**; the first
queue behind a data frame pushes it over, the retransmit adds two more frames,
and the link spends its capacity re-sending things. It is not slow, it is a
collapse mode.

**Translating to sockets fixes the latency, not just the volume.** Only
*payload* crosses the cable — every acknowledgement the client's TCP needs is
generated locally, in microseconds. mTCP measures an RTT of about zero, its
timers never fire, and the cable runs at its full 3,741 B/s.

**~3.4× the throughput, and the difference between "slow" and "thrashes".**

---

## 2. Where it lives, and the two places it must not

The translation is **an `OP_SEG | OP_LAZY | OP_COMP` part of `DOS.O88`**
(SPEC.md §20.12): code reached by far call, fetched on demand, compressed on
the disk.

**Not in `NET.DRV`.** A driver is resident while mounted, so ~2KB there is
paid by every user of the cable — for a feature only the DOS box can reach.
That is the objection that decided this file, and it is `XMEM.DRV`'s argument
word for word (SPEC.md §41.12): *a machine with no XMS was reserving about
1.2KB of kernel image forever for a feature it can never reach.*

**Not a new driver either.** A `DOSNET.DRV` publishing the raw verbs in
`DRVC_NET` would need to call `NET.DRV` for its sockets, and **no driver in
this tree calls another** — `OSAPI_DRV_CALL` is published as *"a PACKAGE calls
a DRIVER"*. The alternative, its own copy of `lplink.inc`, makes two drivers
owners of one parallel port. Either way it is a new precedent bought for
nothing: the code is DOS-only, so it belongs to the DOS package.

**A sidecar `.OVL` was considered and refused.** §73.14's overlays are the C
toolchain's, emitted for `ovl_*` functions, and `DOS.O88` is assembly; and a
separate file beside a `SYSAPPS` package is a file a copy can leave behind,
which is the lesson `apps/c64` already paid for (its ROMs were a sidecar until
§20.12 wave 6, and *"a copy that took the program and left it behind was a
machine that could not start"*).

### 2.1 The part does protocol, and the image does everything ownership-bound

**Rule, and it is load-bearing: the part may call `OSAPI_DRV_CALL` and must
not claim memory or read files.**

A part's segment is a heap claim, not the package's region, and the
ownership-sensitive slots identify their caller *by the segment it runs in*
(`OSAPI_MEM_CLAIM`: "You are identified by the segment you run in"). A claim
made from the part would be owned by the wrong thing. `OSAPI_DRV_CALL` is
safe because the caller's segment is used for exactly one purpose — where the
buffers are (`[net_useg]`, `usr_read`/`usr_write`) — and no fence reads it.

So: claims, files and the part's own fetch are the image's; frames, headers,
sequence numbers and socket verbs are the part's.

---

## 3. The laziness is per SESSION, and §96.3 is why

**Every claim and every fetch must happen before the arena is sized.** §96.3
gives the DOS program `OSAPI_MEM_AVAIL`'s whole answer with no arithmetic
between the two calls, so by the time a client calls `access_type` there is no
heap left — which is already why §96.23.7's frame buffer is claimed in front
of the sizing call.

So the part cannot be fetched when the program asks for it. The question the
box asks instead, at `dos_run` entry, is **"is there a cable and no card?"**,
and `net_find` already answers it: it tries `DRVC_NET` first and falls back to
`DRVC_FILE` (SPEC.md §72, and it prefers the card for this exact reason).

| what `net_find` says | what happens |
|---|---|
| `DRVC_NET` — a card | nothing fetched; §96.23's raw verbs are used directly |
| `DRVC_FILE` — the cable | the part is fetched, pre-bracket |
| nothing | nothing fetched; no packet driver is published (§96.23.5) |

### 3.1 The trade, per machine — MEASURED, and it went the other way

This section was written around SPEC.md §20.12.9's table, which prices one
plain `OP_ASSET` row at **800 bytes** of parts code, and it said wave 1 would
measure ours rather than guess "because a `LAZY|SEG|COMP` row is a different
gate set". **The measurement is the finding, and it inverts the conclusion
this section originally reached.**

Built into `DOS.O88`, image delta against the same tree:

| the row declared | image | S |
|---|---:|---:|
| none (today) | 14,292 | — |
| `OP_ASSET`, or `OP_SEG` — *identical* | 16,124 | **1,832** |
| `OP_SEG, OP_LAZY` | 16,569 | **2,277** |

**`OP_COMP` with `OP_LAZY` is refused outright**, and the standard says why in
as many words: a lazy row's `zkb` word banks the segment it was fetched into
and a compressed row's carries its packed length, and there is one word. So
the part would ship uncompressed — which the owner had already discounted, but
it is a constraint worth knowing before designing around it.

Call the translation **T** (~2,000). Then:

| machine | parted | inline | verdict |
|---|---:|---:|---|
| no network at all | 2,277 | 2,000 | **inline** by 277 |
| a card | 2,277 | 2,000 | **inline** by 277 |
| cable only | 4,277 | 2,000 | **inline** by 2,277 |

**The standard costs more than the thing it defers.** Parting is a loss on
every machine, which is not a close call in the other direction either — so
waves 2–4 build the translation INLINE, and §8's rehome is what makes parting
worth doing at all rather than an optimisation on top of it.

The 1,832 for a plain row against §20.12.9's 800 is not explained here and
should not be guessed at: that table's figures are the *parts code* measured in
the `mseg` test package, and this is a whole-image delta in a package with
its own strings, alignment and bss chain. What is certain is the number that
decides the design, and it is the delta.

### 3.2 What inline actually costs, since it is what ships first

2,000 bytes of `DOS.O88`'s image, so 2,000 fewer bytes of arena for the DOS
program — **0.3% of a 640KB machine**, and none of it resident: a package's
region exists only while its window is open. That is a different quantity from
the one that sent this design away from `NET.DRV` (§2), where the bytes are
resident for as long as the driver is mounted and are paid by every user of
the cable, DOS or not.

---

## 4. What the translation actually does

It is a **slirp in reverse**: the client believes it is on an Ethernet, and
everything it emits is terminated locally and re-issued as socket calls.

- **ARP** — the client ARPs for its configured gateway. We answer every
  request with one synthetic MAC: *we* are the gateway, the router and the
  whole segment. A canned 42-byte reply with two fields poked.
- **IP** — parse the header, check nothing we do not have to, build ours with
  the right checksum. `ip_cksum` is 12 bytes in the ether driver and
  `ip_finish` 46; ours are the same arithmetic.
- **TCP** — the expensive part, and §5 is its own section.
- **UDP port 53** — intercepted, and §6 is why that is an opportunity rather
  than a gap.
- **everything else** — dropped, which is what a packet driver does to a
  frame nobody registered for.

---

## 5. The TCP endpoint, and what it does NOT need

Our whole TCP is **1,943 bytes** (the `tcp_` hull in `ether.bin`). The
endpoint here is a fraction of it, because what is underneath is not a network:

- **no congestion control** — no window scaling, no slow start. The far side
  of a `NETV_SEND` is a reliable in-order local transport.
- **no retransmit queue of our own.** `NETV_SEND` either takes the bytes or
  does not; there is nothing to time out.
- **no reassembly.** In-order only; anything else is dropped and the client
  retransmits, which is a path it already has.
- **no listen side** in this plan (§9).

What it does need, and none of it is optional:

1. **A per-connection state machine** — `SYN` → `SYN|ACK` → established →
   `FIN`/`RST`, plus recognising the client's retransmits idempotently.
2. **32-bit sequence arithmetic on an 8086**, which is the hidden cost and the
   one place to reuse rather than re-derive: `tcp.inc` already does it.
3. **The TCP checksum with its pseudo-header**, both directions.
4. **A fixed advertised window** equal to our staging buffer, so the client
   never sends more than we can hold.

**The mapping is the easy half**: a `SYN` to (IP, port) becomes `NETV_OPEN`;
the client's payload becomes `NETV_SEND` and is acknowledged the moment the
verb takes it; `NETV_RECV`'s bytes become an IP+TCP frame handed up through
§96.23.4's up-call; `NSK_CLOSING` becomes a `FIN`.

---

## 6. The DNS hijack — `NW_OPEN` takes a NAME, and that is the whole trick

There is **no UDP in the socket ABI**, so mTCP's DNS cannot be relayed. That
looks like the feature's hard edge and is instead its best simplification,
because `NETV_OPEN` takes `ES:SI` = **a host name** and `CX` = a port: *the
far side resolves*.

So:

1. the client asks its configured nameserver for `os8088.com`;
2. we answer with a synthetic address out of a private pool, and remember the
   pair;
3. the client connects to that address, and we `NETV_OPEN` **the name**.

**We never need a resolver, a cache or a UDP path.** The synthetic pool is
ours to choose and never reaches the wire; a name we have no mapping for is a
connection we refuse with `RST`, which is what a client understands.

---

## 7. The waves

| wave | what | gate |
|---|---|---|
| 1 | **DONE, and it changed the plan**: measure S. §3.1 is the result — 2,277 for a `SEG\|LAZY` row, against a 2,000-byte translation, so the part is REFUSED for now and waves 2–4 build inline | `S` is a number in §3.1 |
| 2 | ARP, IP, and the DNS hijack | the client's ARP is answered and a DNS query gets a synthetic A record |
| 3 | **DONE, and the route on the cable is now this** (SPEC.md 96.26.3) — no knob. One connection reads `FLAGS 12 10 10 18 11`: the `SYN\|ACK`, the handshake, 235 bytes of HTTP and a clean close, with the host's own log recording the `GET`. All three defects were REGISTERS, not protocol (§7.2) | `tests/dosxlat.py`, registered — and it reads the CLIENT's record of every segment, because nothing outside the client can tell "the box opened a socket" from "the client ever heard about it" |
| — | **the bss went with it** (SPEC.md 96.26.6): 4,299 bytes of every DOS program's arena, on every machine, for a wire that may not be there. `image + bss` **25,586 → 21,287**, and `DS` is the claim inside every `dn_*` | `t_appsmall`-style A/B: the stock arm's figures are in §7.3 |
| 4 | **DONE, and it passed first time** (SPEC.md 96.26.7). `tests/doscable.py`: a real Crynwr client, in the real DOS box, over a real `NET.DRV`, over MartyPC's parallel port a nibble at a time, to a real host socket - with NO knob, `net_find` picking the cable because `NETV_RAW` is one of the three verbs it refuses. `FLAGS 12 10 10 18 11`, the same five segments as the card arm, and the partner's own log reads `o ssssssssss w s r s c` | `tests/doscable.py`, registered |
| — | *below here is optional and separately revertible* | |
| 8 | `OSAPI_PKG_REHOME` — give **S** back. **BEHIND A DEPENDENCY, and deliberately parked** (§7.5) | `kernsize`-style A/B on the region |
| 9 | inbound (`NW_LISTEN`/`NW_ACCEPT` exist) | `ftpsrv` serves across the cable |

### 7.5 Wave 8 is parked, and the reason is a dependency rather than a doubt

**The owner's call, and it is about ORDER rather than value.** The whole value
of `OSAPI_PKG_REHOME` is that the region it frees can then be MOVED and the
heap COMPACTED — and today a region cannot move under its own package's calls:
`mem_frameless` asks `mem_in_nest`, and a region is pinned for every moment it
could be claiming (SPEC.md 66.6.1, and docs/plans/DISK-CPU-PLAN.md's §5 is the
same wall from the other side). That is a design question in another document,
not this plan's.

So the arithmetic is: rehome hands bytes back to a heap that cannot yet put
them anywhere useful, **and we are the party that wants those bytes now** —
which is the case where the give-back is worth least. §96.26.6 took the 4,299
bytes this feature actually needed straight out of the package's bss instead,
which needed no ABI and no compaction at all.

**It still needs doing.** It simply sits behind that dependency, and the row
above says so rather than reading as un-started work somebody should pick up.

### 7.4 What wave 4 found, and the one thing it had to build

**Nothing in the translation.** It passed on its first run, which is what
§7.0's argument predicted: the endpoint consumes five verbs and both drivers
answer them, so once the card arm was right the cable arm was a question about
`net_find` and about whether `NET.DRV` survives the bracket. Both answers were
already yes - `drv_suspend_x` has skipped `DRVC_FILE` since long before this
plan, on the ground that a volume is *"a thing a DOS program WANTS"*.

**And `SocketBox` was already there**, which this plan did not know: §7.1
reads as though the socket verbs still had to be put on top of the transport,
and `tests/lptlink/partner.py` has served them against real host sockets since
`tests/socktest.py` was written. So the wave was a test file and one harness
method, not a far side.

**The one thing built is `Partner.idle_until_wire`.** `_await_strobe` steps
400 cycles a debug round trip, which is one round trip per 84 microseconds of
guest time - fine while a nibble is in flight and terrible for a phase where
the guest is not using the cable at all. The DOS box's launch is exactly that
phase: a floppy mount and two programs, **13.2 million cycles**, which is
33,000 round trips spent watching a line nothing is driving. Stepping it in
25,600-cycle chunks until the data register moves is **516**. It is safe for
`_spend_stall`'s reason and the arithmetic is the whole argument: every
deadline in this transport is in TICKS (docs/plans/completed/NET-PLAN.md 9.1), and a chunk is
5.4 ms against `LP_TMO`'s 110 and `TURN_RX`'s 440.

**It is still the slow row in the tree, and that is the instrument and not the
subject.** `tests/socktest.py` - the same arrangement one layer down, fetching
a page from an ALREADY-RUNNING package - takes ~13 minutes; this adds the box's
launch in front of it. The payload is 45 bytes on purpose.

Two things noticed in passing and not chased, because neither is this branch's
subject: `socktest` itself fetches its page correctly and then fails its own
handle-leak assertion with *"8 of 4 handles free after the close"*, which reads
as a bug in that assertion rather than in the wire - and it is UNREGISTERED, so
nobody has been running it (its exemption reason says *"needs `make socktest`
and QEMU networking"*, and it needs no QEMU at all).

### 7.2 What wave 3 actually cost, and it was never the protocol

Three defects stood between a correct endpoint and a working one, and **every
one was a register**. Worth writing down because each presented as something
else:

1. **`dn_pump`'s loop counter was `CL`**, and `CX` is an *output* of every
   verb here — `NETV_STATUS` answers the readable byte count in it. `dec cl`
   then ran on whatever the driver had returned: a 0 wrapped to 255, so one
   pump made hundreds of far calls, a poll made ten pumps of them, and the
   tick handler took **longer than a tick**. The machine spent all its time
   in its own timer and the DOS program never ran again — which reads as a
   hang *in the program*. `eth_ncall` wrapping its 16 bits was the tell.
2. **`dos_pkt_poll`'s frame budget was `DX` across the up-call** — the same
   fault one layer out, and found by reading for it after (1).
   `dos_pkt_deliver` far-calls the *client's* receiver twice and a client owes
   us no register, so a receiver that used `DX` turned a ten-frame drain into
   up to 65,535 of them, inside a tick handler.
3. **`dn_seg` banked the flags in `DL`**, which `dn_put32` needs as the high
   half of `DX:AX`. The byte on the wire was therefore the last sequence
   number's top half — `0x2B` where `0x12` was meant — and the client dropped
   the segment as malformed while every counter on our side said *sent*.

The lesson for anything else built against `OSAPI_DRV_CALL`: the slot
publishes **"`BP`, `DS` and `ES` come back yours"** and says `AX`, `CX`, `DX`,
`SI` and `DI` are the driver's to define. A loop counter in one of the second
group is a defect waiting for a verb that happens to return in it.

**And wave 9 made it four, with the same shape and no driver in it at all.**
`dn_lsn_ports` parses `OS88LISTEN=<port>` a digit at a time and held the digit
in `DL` — across `mul bx`, which writes `DX:AX`. A port anybody would type
gives a high half of 0, so `add al, dl` added **nothing**: five digits
accumulated to zero, the routine's own `or ax, ax / jz` refused to listen on
port 0, and the whole inbound direction was silent with the environment row
read perfectly. So the family is not "a register a *driver* may return in" —
it is **a register an INSTRUCTION owns**, and `mul`, `div` and `loop` own
theirs unconditionally. Three of the four were found by reading for the
pattern after the first; this one was found by a test that could say *no
listener was taken* rather than *nothing arrived*.

Which is the other half of the lesson and is about the TEST. `LSN 0` is one
message for three different bugs in three different files — the environment
never read, `NETV_LISTEN` refused, or a connection that never came — so the
row now reads the box's own `dn_lsn` rows out of the network claim and checks
them **first**. `tests/dosmap.py` had to learn to see them: nasm's map writes
absolute equates under a `---- No Section ----` heading as two fields where a
section's symbols are three, so an offset into a heap claim was invisible to a
reader that took only the three-field lines. Hardcoding them in the test was
the alternative, and `DOS_TRACE_SZ`'s own comment is about why that loses.

**A row-walk defect sat beside it and is fixed in the same commit**:
`dn_lsn_init` restored `SI` across `dn_streqc` and not across
`dn_lsn_ports`, so `.next` added `DOS_ENVBUF` to a cursor eleven-plus bytes
into the row that had just matched — every row after one naming ports was
scanned from the wrong place. `MTCPCFG=` is exactly the row a user types
beside it, so the ordinary case was the broken one.

### 7.6 …and then FOUR more, and every one was in the harness or the probe

The box needed one more line after that (nothing, in the end — the register
fix was the whole of the box's share). What stood between a listening DOS
program and a served request was four defects in the *test*, and they are
written down because each is a general shape rather than this row's bad luck.

**1. The box accepts EARLY, and the probe threw the evidence away.**
`dn_accept` accepts the moment it knows where the client is — `[dn_cip]`,
which any IP frame teaches — so a connection already waiting is accepted
*during* the probe's outbound leg, and `receiver` banks its SYN there.
`do_listen` then opened with `mov word [isyn], 0`, discarding it, and waited
ninety ticks for a SYN that had arrived and been recorded. The three stores
were redundant as well as harmful: a `.COM` starts with the zeros its image
carries and runs once per launch.

**2. `rxbuf` is whatever arrived LAST, and `do_listen` read two of the SYN's
four address fields out of it.** With the SYN early, `rxbuf` held the
outbound connection's FIN by the time the roles swapped, so the reply went
out on the wrong source port and address — and the box, correctly, read it as
a *new outbound connection*: `NETV_OPEN 10.88.0.255:49152` in the far side's
log, the box's own invented pool address being dialled by the box. All four
fields are banked with the SYN now (`isyn_dport`, `isyn_dst` are new). The
same mistake one line along made `[ldata]` count the outbound leg's own
45-byte answer as the request: `LDATA 45 LFIRST 4854` — `HT` of our own
reply — so the payload counter takes the port as well as `[isyn]`.

**3. The host end timed out in 20 seconds** against a leg that is minutes of
host wall clock, closed its socket, and the far side went `NSK_CLOSING` — so
the box closed the flow before the DOS program's reply was ever sent, and the
row failed with *"the host got nothing back"* about a program that had
answered perfectly. `docs/WRITING-TESTS.md` names this family; the read now
gets the whole deadline and the connect keeps a short one so retries work.

**4. …and then it waited for 512 bytes of a 24-byte answer that never
closes.** The probe replies and holds the screen on `int 16h`, so an
EOF-or-512 loop blocks for the whole (now long) timeout after the answer has
arrived, and the caller reads `got` as `None`. One segment IS the answer; the
loop takes a 5-second tail for a split one and keeps what it has.

**The reason all four were invisible is that the row said `LSN 0`** — one
message for the environment never read, `NETV_LISTEN` refused, a SYN staged
and refused, a SYN staged and never collected, and a connection that never
came. It reads the box's own `dn_lsn` rows, `dn_flows`, `[dn_pend]` and the
driver's dropped counter now, and checks them in that order, so each of those
is a different sentence. That is what turned a week's worth of guessing into
four runs.

### 7.7 The row was 20+ minutes and is 525 seconds, and the fix was in `serve`

Worth its own entry because it is not about this row. `partner.py` drives
MartyPC one debug round trip per `STEP` = 400 guest cycles, so the cost of a
cable row is **guest cycles ÷ 400** — an idle guest tick is 262,000 cycles,
which is **655 round trips spent watching a line nobody is driving**. The box
polls `NETV_ACCEPT` once a tick while a listener is open, so the inbound leg
is almost entirely gap, and it ran past twenty minutes.

`idle_until_wire` already existed for exactly this, with its safety argument
already written down (25,600-cycle chunks against `LP_TMO`'s 2 ticks and
`TURN_RX`'s 8) — `serve` was simply not using it for the gap *between*
commands, only for the launch. It does now: 64x fewer round trips per idle
tick, full resolution the moment the strobe moves, and the strobe is tested
first because a coarse wait keys off a *change* and a master that is already
asserting would otherwise be waited out.

| | before | after |
|---|---|---|
| `doscable`, whole row | killed at 1,200s, mid-leg | **525s** |
| one idle guest tick | 655 round trips | **10** |

What is left is boot, the Control Panel mount, **two** program launches and
the outbound leg's dense back-to-back traffic. The one structural cut still
on the table: the second launch re-runs the entire outbound TCP connection
only because the probe is a linear program, where `dn_accept` needs just one
client IP frame to learn `[dn_cip]` — a `/L` argument on the second run
(SPEC.md 96.20 already carries arguments to the program, and
`tests/dosargs.py` gates it) would drop sixteen commands.

### 7.3 …and the memory, which was the owner's call and the bigger win

Asked mid-wave: *every byte of RAM the DOS box uses is one a DOS program
cannot use*. Correct, and it lands hardest on this feature — §96.3 hands the
program `OSAPI_MEM_AVAIL`'s whole answer, and the package's image plus bss
comes off the same heap first.

| | bytes |
|---|---|
| the frame built for the client | 1,514 |
| the client's own frame, staged | 1,514 |
| the poll's private stack | 1,024 |
| flows, names, pseudo-header, counters | 247 |
| **`image + bss`, before → after** | **25,586 → 21,287** |

The claim is taken only when `net_find` answers, and sized by route: **3 KB**
for a card, **5 KB** for the cable. So a machine with no wire — which is every
`kern_small` machine, since it ships no network driver at all (SPEC.md 24.5) —
gets the whole 4,299 back, and on the 128 KB floor machine that is 8% of the
arena.

**It cost fourteen `equ` lines and four wrappers, not a rewrite**, and the
reason is worth keeping: `dosnet.inc`'s premise was already *one segment, no
segment override*, so pointing `DS` at the claim left all 78 of its frame
references untouched. SPEC.md 96.26.6 is the mechanism and the one hazard it
introduces.

**The 16 KB the other agent found is `DOSTRACE`'s ring, and it is
`%ifdef`'d** — it costs a shipped build nothing. What did need doing there was
different and more urgent: the trace arm measured **61,437 of
`APP_MAX_SIZE`'s 61,440**, three bytes, so this wave's 77 bytes stopped the
instrument assembling and the only row that noticed was `dosdbg` quoting the
assembler about a probe. The ring is 256 entries now — 8,192 bytes cheaper and
the better number anyway, since the failure that sized it makes 169 calls and
256 is exactly what `TRACE.LOG` holds and what `tests/dostrap`'s reference
tracer keeps.

### 7.0 The translation is developed on the CARD, and that is not a shortcut

The translation consumes `NETV_OPEN`, `NETV_STATUS`, `NETV_SEND`, `NETV_RECV`
and `NETV_CLOSE` — and **both** drivers answer those verbs. `ETHER.DRV` is one
socket provider and `NET.DRV` is the other; which one a machine has is
`net_find`'s answer and nothing else in the translation cares.

So it is built and tested against the **card** under QEMU, where
`tests/dospkt.py`'s harness, a real network and mTCP already work, and the
cable is then the same code with `[net_cls]` holding a different class.
`DOSNETCARD=1` forces the translation on a machine that has a card, which is
what makes that testable at all — on such a machine §96.23's raw path is
strictly better and would otherwise always win.

That inverts the risk the hard way round on purpose: the protocol logic is the
part most likely to be wrong, and it is the part that can be exercised against
a real host at emulator speed. What is left for §7.1 is only *"do the same
verbs work when the wire under them is a cable"*, which is a much smaller
question than "is this TCP endpoint correct".

### 7.1 Testing needs no second machine, and that was not obvious

`tests/lptlink/partner.py` plays the far end of the cable **from the host**,
over MartyPC's `ParallelController` — `status_register_write` stores what the
debug server writes and `data_register_read` returns what the guest drives, so
the host owns both directions of the wire. It has the transport (nibble, byte,
word, dword) and stops there.

Wave 4 puts the **socket verbs** on top of it and proxies them to real host
sockets. The guest then runs mTCP over the cable and the *host* provides the
network — which is the one arrangement where this is testable in a container:
MartyPC has no NIC, and it does not need one, because the network is on the
far side of the cable by construction.

It is **exact rather than fast** (every nibble is debug-server round trips), so
the gate's payload is measured in hundreds of bytes, not a file.

---

## 8. `OSAPI_PKG_REHOME` — and why it is not wave 1

§20.12.10 is built: a loader tells the kernel *"the program is at DX, not at
me"*, its own region is freed, the carve is re-owned and `ld_start` runs step 8
again against the part. §20.12.9 measured what it is for — **a package that is
nothing but a reader spends 2KB of a 640KB machine on a program that has
finished its job.**

Applied here, `DOS.O88` becomes a thin loader whose part 0 is the DOS box and
part 1 is this translation, and **S is freed after launch**: the resident cost
of the whole feature goes to about zero. The loader fetches part 1
conditionally — the `net_find` question of §3 is answerable before the rehome —
and writes its segment into the head of part 0's bss, which §20.12.10.2 says
needs no mechanism because step 7 is deliberately not re-entered.

**It is a separate wave because it restructures the launch path of the most
complex package in the tree**, and it changes identity, bss handling and build
rules while changing no behaviour. Waves 1–4 add a capability and can be
judged on whether it works; wave 8 is a pure size change and can be judged on
one number. Landing them together would make a failure in either look like a
failure in the other.

§3.1's measurement makes it **the thing that decides whether parting happens
at all**, rather than an optimisation on top of a parted design. Parting
without it is a loss on every machine; parting with it takes the feature's
resident cost to about zero.

### 8.1 The hazard to design for: this package's bss is NOT zeroed after a rehome

§20.12.10.1 is explicit that `ld_start` jumps back to **step 8** and not step
7, because on the rehome path *"the bss arrived inside the part"* and zeroing
it would erase what the loader just wrote there.

**`DOS.O88` relies on a zeroed bss in at least one place that is load-bearing
and silent.** §96.6.1's per-drive current directories are one cluster word
each with *no "has this been initialised" flag*, on the stated grounds that
"a slot nobody has touched is 0 — which `.bss` already is, and which is
exactly right". A rehomed launch that skipped the zeroing would hand a DOS
program a current directory pointing at whatever cluster the heap last held.

So part 0 must **ship its bss as real zero bytes on the disk** (§20.12.10.2's
"§51.1.2's rule, one format along"), which costs the part's file about 1.4KB
and one more `int 13h` at launch — ~400 ms on the target machine
(PERFORMANCE.md). That is the trade wave 8 has to state: the resident bytes
come back, and a launch gets measurably slower. Both numbers, not one.

---

## 9. What will not work, stated plainly

- **`ping` will not work.** There is no raw ICMP over sockets. mTCP's `ping`
  is the one program that will fail on a cable machine and succeed on a card
  machine, which is worth a sentence in the user-facing docs.
- **UDP applications will not work** — `sntp`, `dnstest`. What does work is
  every TCP one: `ftp`, `telnet`, `htget`, `nc`, `ircjr`.
- **Inbound connections are wave 9.** `NW_LISTEN` and `NW_ACCEPT` are already
  on the wire, so `ftpsrv` and `httpserv` are a later wave rather than a
  refusal.
- **`kern_small` gets none of it, and needs none.** It ships no drivers, so
  there is no `NET.DRV` and no cable; and `DOS.O88` is in `SMALLOMIT` already
  (§96.1), so the small disks never carry the package, let alone the part.
  Nothing here needs an `%ifdef`.
