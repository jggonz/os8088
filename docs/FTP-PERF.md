# FTP throughput — where it stands, and what is left

The state of the FTP server's speed after one long session on it, written so
the next round starts from evidence instead of from scratch. Every number here
came off **the 5150** unless it says otherwise; QEMU numbers are labelled and
are never times (PERFORMANCE.md Part 3).

---

## 1. The number, and how it moved

| build | B/s | what changed |
|---|---|---|
| first field run | **7,062** | — (with a 6 s dead gap at the start) |
| `3969745` | **8,720** | the TIME-WAIT reap: the gap became 0 s |
| `0c12f31` | ~12,600 | `rep movsb` for the ring copies |
| `04fa06c`+ | **~15,000** | the field's own figure |
| `1f1bd7e` | **16,029** | the baseline the second session started from |
| `669ff4f` | **16,919** | `ne_dma_read`/`write` unrolled ×8 (§72.16.5) |
| `a11d737` | **30,455** | `OSAPI_FILE_DFREE` out of the per-chunk path (§77.40) |

**7 KB/s to 30 KB/s, 4.3×.** `Got 304552 in 10s 30455B/s gap 0s`, read off the
glass. The same upload took 43 s at the first field figure.

That row was *predicted* before it was read, and the prediction is worth
keeping: the rate line had scrolled out of the log window, so the split's own
disjoint columns were used instead — `net 6578 + draw 33 + wait 3301 +
idle 314` = **10,226 ms**, giving 29.8 KB/s against the 30.455 the next run
printed. **1.5% out.** That the columns can be used that way is the whole
point of §77.35's accounting closing over the wall.

## 2. What actually fixed it, in order of size

1. **`rep movsb` for the ring copies (§72.16).** `sk_rxput`, `sk_rxget`,
   `ring_in`, `ring_out`, `ring_move` were hand loops at ~83 clocks a byte;
   `rep movsb` is 9+17n, about 17. Measured **8,670 ms off the driver** on a
   297 KB transfer, against nine seconds predicted.
2. **`sk_rxput`'s restructure (§72.15.4).** It read `[bx+SKO_RXH]` *twice per
   byte* and bracketed every store with `push di`/`pop di` — fifteen
   instructions and four memory operands per byte. It was 71% of every
   received frame. §72.13.1 had fixed its sibling and left it.
3. **The TIME-WAIT reap (§72.14).** Not throughput — it removed a **6-second
   hole** at the start of every transfer that followed another one. Four
   sockets, and one FTP session uses all four, so the data socket a `LIST`
   just closed held the fourth for `TCP_TWTMO`. Measured 6.15 s → 0.39 s.
4. **The ring size (§72.13).** The window *was* the constraint at 1024 bytes
   (window ÷ turnaround = 7.1 KB/s, and the field measured 7.3). Making the
   rings a heap claim removed that ceiling — **and on its own bought nothing
   measurable**, because the per-byte cost immediately became the constraint
   instead. Both facts matter.

## 3. What did NOT work, and is written down so it is not tried again

- **A bigger staging buffer in the FTP server.** `FD_STGSZ` 8192 → 32768:
  the field measured no change, twice. The staging buffer was never the
  constraint.
- **Fewer, larger disk appends.** The same change made the *gap* worse — the
  worker stops reading while the UI task writes, so the chunk size IS the
  length of the silence on the data connection (§77.24). The number that made
  the fewest appends made the worst gaps.
- **Blaming the disk seek count.** Measured: no change.
- **Blaming retransmits.** A packet capture showed 126 segments, **zero**
  retransmissions, 129,671 bytes on the wire for a 129,430-byte file. The wire
  was already perfect.
- **A driver-side pump worker** (`ETHPUMP`, SPEC.md §72.19). Three builds went
  to the 5150 and **every one was slower**: 16029 B/s with no worker, 13843
  with it pumping instead of the verbs, 14502–15227 with it on standby behind
  them. The package's own polling already bounds the latency it was for, and
  during a transfer there is no gap for it to fill — only a third contender for
  the card. Built, measured, removed; §6.1 below is the full account.

## 4. Where the time goes NOW

**The current profile, `a11d737`, 304,552-byte STOR to the hard disk:**

```
disk 3163 net 6578 draw 33
wait 3301 wake 3254 idle 314 pass 79
dfree 0 glass 65 wk 60
```

| | ms | % of 10,226 |
|---|---|---|
| `net` — the worker's whole turn: every socket verb and the drain loop | 6,578 | **64%** |
| `disk` — the whole of `fd_do_write`, standing included | 3,163 | **31%** |
| `idle` — the wire had nothing | 314 | 3% |
| `glass` + `draw` — all painting, lock to unlock | 98 | 1% |
| `wait − disk` — handshake latency | **138** | 1% |

Two things worth reading off that table:

- **`wait` has collapsed onto `disk`** — 3,301 against 3,163. §77.36's yield
  *did* work; it was worth 8,214 ms of latency and none of it was visible
  while `dfree` was sitting on top of it. A change measured against a broken
  instrument is not measured.
- **`disk` is now entirely serialised with `net`**, and that is the next
  structural item (§77.36.1): one staging buffer means the worker stops
  reading while the UI task writes. Overlapping them is worth up to the whole
  3,163 ms — about **43 KB/s** — and costs `FD_STGSZ` of package memory.

### 4.1 The older, driver-side profile



The last full field profile (`netbench`, §72.15), a 297 KB upload,
`ACTIVE` 23,781 ms:

| | ms | % |
|---|---|---|
| **OUTSIDE the driver — the FTP server, the disk, the scheduler** | **13,499** | **57%** |
| `card` — `ne_ring_read`, the NIC's ring into our frame buffer | 2,942 | 12% |
| `verb` overhead — `eth_pkg` minus `pump` and `rxget` | 2,004 | 8% |
| `rxput` + `rxget` — the two ring copies | 2,641 | 11% |
| `pump` overhead — the timers, per call | 1,001 | 4% |
| `frame`, `cksum`, `tx` | 2,503 | 10% |

**The whole driver is 43%.** Making it infinitely fast wins at most that.

## 4.2 The driver's own split, `netbench` on the 5150, August 2026

`ACTIVE` 14,669 ms (the profiler's own brackets and NETBENCH's window are in
this run, so it is slower than the 10,226 ms a plain build takes — the
PERCENTAGES are the reading, not the milliseconds):

| stage | calls | KB | ms | pct | us/KB |
|---|---|---|---|---|---|
| `verb` — all of the driver | 565 | | 11,615 | **79%** | |
| `pump` — contains card/frame/cksum/rxput | 564 | | 7,687 | 52% | |
| **`frame`** | 234 | 309 | **3,760** | **25%** | 12,172 |
| `card` — the NIC ring in | 468 | 264 | 2,540 | 17% | 9,624 |
| `rxget` — socket → package | 390 | 297 | 1,689 | 11% | 5,687 |
| `rxput` — frame → socket | 218 | 297 | 1,568 | 10% | 5,282 |
| `cksum` | 2,338 | 31 | 750 | 5% | 24,195 |
| `tx` | 526 | 28 | 671 | 4% | 23,981 |
| **NOT in the driver** | | | 3,054 | **21%** | |

**THE COLUMNS ARE INCLUSIVE, AND THE TABLE SAYS SO IN ITS OWN FOOTNOTE**:
*"pump contains card/frame/cksum/rxput. verb contains all."* `ip_cksum` is
called from `eth_frame_i` and `sk_rxput` from `tcp_in` inside it, so `frame`'s
25% is mostly its children. Subtracting gives the ranking that actually
matters:

| | exclusive ms | pct |
|---|---|---|
| `card` | 2,540 | **17%** |
| `rxget` | 1,689 | 12% |
| `rxput` | 1,568 | 11% |
| `verb` minus pump/rxget/tx | ~1,568 | 11% |
| **`frame` itself** | ~1,442 | **10%** |
| `pump` minus card/frame — the timers | ~1,387 | 9% |
| `cksum` | 750 | 5% |
| `tx` | 671 | 5% |

**There is no 25% item. The driver is a flat profile**, and reading an
inclusive total as a ranking is what made it look otherwise — the same
mistake in kind as §77.38's, one layer out.

What survives against the bar:

- **`cksum`, 5% at 24,195 us/KB** — the worst rate in the table by 2× on the
  smallest byte count, and pure loop overhead. Unrolled ×8 (§72.16.6): the
  body is 23 clocks against `loop`'s 17, so **~40% of it, ~2% of ACTIVE**.
- **`rep movsw` for the ring copies is RETIRED, and the arithmetic is why.**
  §5 item 4 priced it from 8086 timings — 25 clocks a word against `movsb`'s
  17 a byte. **The target is an 8088 with an 8-bit bus**, where every word
  access costs 4 extra clocks and `movsw` does two of them: 33 clocks a word,
  **16.5 a byte against 17**. Three percent of 21%, or **0.6% of the
  transfer**, against the odd-count and odd-alignment hazard §72.16.2 warned
  about. Not worth writing.
- `card` at 17% has already had its ×8 unroll (§72.16.5); a word read is not
  available on the field's 8-bit NE1000.

**So the flat profile is the finding.** Nothing left is a single-digit-hours
change worth a summed 10%, and the honest next move is to stop rather than to
instrument `frame` for a 10% item spread across header parsing.

## 5. The next things to try, in the order the evidence ranks them

1. ~~**Find out what the 57% is.**~~ **DONE, and it was the worker asleep.**
   The field's `Got 304552 in 19s` line carried `disk 3595 net 7624 draw 33`,
   so of 19,000 ms: 40% inside the driver, 19% disk, 0.2% paint and **41% in
   none of them**. `fd_worker` ended every pass with `TASK_SLEEP 1` — one pass
   per 55 ms tick — and 19 s is 345 ticks, which is **883 bytes a pass**: one
   `NET_SKMAX` buffer, the exact ceiling §77.30 was written to kill and fixed
   only *inside* the pass. A pass that moved bytes now yields instead
   (SPEC.md §77.34). **This is the biggest single item ever found in this
   file** and it was arithmetic on a number that had been on screen for weeks.
1b. **The pacing change was worth NOTHING, and the DMA unroll was worth all
   of it.** The next field run said `Got 304552 in 18s 16919B/s` /
   `disk 3562 net 6580 draw 33`. `net` fell 7,624 → **6,580**, within 5% of
   the unroll's prediction; `disk` and `draw` unchanged; and **the remainder
   went 7,748 → 7,825 — it did not move.** So the tick was not the gate and
   883-bytes-per-tick was arithmetic that came out right rather than a cause.
   A remainder that survives the removal of its supposed cause is asking for
   a finer instrument, not another guess: `wait N idle N pass N` is that
   instrument (SPEC.md §77.35), and it brackets the worker's two naps
   separately so the 43% has to name itself.
2. ~~**`card`, 12%.**~~ **DONE.** `ne_dma_read`'s `in al,dx / stosb / loop`
   is ~19 clocks of work behind 17 clocks of `loop`; unrolled ×8, with
   `count & 7` as a leading byte loop because a frame length is odd as often
   as not. `ne_dma_write` got the same, and `lodsb` in place of
   `mov al,[si] / inc si` while it was open. SPEC.md §72.16.5; ~1,000 ms
   predicted, **not yet measured on the machine**.
1c. **AND THE INSTRUMENT ANSWERED ON ITS FIRST RUN.** `disk 3570 net 6588
   draw 33` / `wait 11784 idle 119 pass 170`, and the four disjoint columns
   sum to 18,524 against an 18-second wall — nothing is hiding in other
   tasks. **`idle` is 119 ms**, which retires every theory about the receive
   window or the peer pacing us. **`wait` is 65% of the transfer and only a
   third of it is floppy**: 37 chunks at 318 ms a commit against 96 ms of
   actual write, because the worker napped a whole tick at a time waiting to
   notice a finished job. It yields now (SPEC.md §77.36). The remaining
   `wait` floor is `disk` itself, and a second staging buffer is what removes
   that (§77.36.1) — `disk` 3,570 would hide inside `net` 6,588 entirely.
1d. **AND THE YIELD CHANGED NOTHING, WHICH LOCATED IT.** Two runs on the yield
   build: `wait 11519 / pass 167` and `wait 11466 / pass 168`, against the
   sleep build's `11784 / 170`. **`pass` did not move** — the worker was
   already getting all the turns a yield could give it, ~9 a second, so each
   scheduling round was ~69 ms and something else owned the machine. During
   11,519 ms of `wait` the UI task accounts for `disk` 3,562 + `draw` 33:
   **~7,900 ms in neither bracket.**

   It was `OSAPI_FILE_GOTO`. `fd_do_write` resolves the path *outside* the
   `FT_DISK` bracket — `fd_split`'s `fd_goroot` before, `fd_unbank` after —
   and a full `GOTO` is a whole `dsk_chdir`: BPB, FAT window, **directory
   scan, sort, and one icon harvest per file** (SPEC.md §19.2.2). Two per
   chunk, 37 chunks, ~107 ms each. **The kernel had already found this exact
   bug in the hard-disk installer** and published the cure; ftpd now uses
   `OSAPI_FILE_GOTO_QM` at all sixteen sites (SPEC.md §77.37), and `FT_DISK`
   wraps the whole of `fd_do_read`/`fd_do_write` so the class cannot hide
   again.
1f. **THE SECOND STAGE WORKS AND BUYS NOTHING.** `wait` 11,554 → **198**,
   `pass` 168 → **12**, rate **30,455 → 30,455 B/s**. `dsk_xfer` raises
   `[sch_lock]` across every `int 13h`, so while the UI task writes, no task
   runs — there is no concurrency to exploit and the disk time merely changed
   columns from `wait` into `net` (SPEC.md §77.41.2). `FD_STG2` is 0.
1e. **AND IT WAS `OSAPI_FILE_DFREE`, DOCUMENTED AS FREE.** `wake 11367` of
   `wait 11593` put the time inside ftpd's UI callback; bracketing the two
   things in it that were assumed rather than measured gave
   **`dfree 7914 glass 11 wk 75`**. The whole paint path — gfx lock, clip,
   unlock, and the file-progress widget's teardown — is **11 ms**, so the
   drawing theory was wrong too. 7,914 over 75 calls is **~105 ms each**, and
   `fd_dfree_bank` ran on every request behind the comment *"No disk I/O (the
   SDK says so), so this is free to ask on every request."*

   True, and not what free means: `dskw_dfree` answers from the resident FAT
   snapshot **by counting every entry in it** — O(clusters) of CPU. **44% of
   the transfer.** It banks one byte, sectors-per-cluster, which cannot change
   under a running transfer; it is now banked once at the session root and on
   any request outside a transfer (SPEC.md §77.40), and the SDK, the kernel
   comment and SPEC.md §18.4.5 now say what the cell costs instead of what it
   does not do.
### 5.1 Is there anything left worth chasing?

Against a **10% bar**, and priced on the 10,226 ms the transfer now takes:

| candidate | worth | verdict |
|---|---|---|
| **A second staging buffer** (§77.36.1) | **up to 3,156 ms — ~31%** | the only item over the bar. Costs `FD_STGSZ` (8KB) of package memory, so it is a memory decision rather than a code one |
| **A bigger `FD_STGSZ` on its own** | **~0** | the thing it buys is fewer appends, and the field already measured that at no change (§3). `disk` is 3,156 over 37 commits — 85 ms each — so halving the count saves tens of ms, not seconds. It only becomes interesting *paired* with the second buffer, where §77.24's silence argument (the reason it came back down) disappears |
| **Everything inside `net`** — `verb` overhead, `rep movsw` for the ring copies, the pump timers | **unknown** | the §4.1 percentages are against a 24-second transfer and are now stale. `netbench` needs a re-run before any of them can be ranked, and each was 4–11% of a total that has since halved |

So: **one item, and it is 8KB.** Everything else needs a fresh profile to even
put in order, and on the old ordering nothing else cleared 10% on its own.

3. **`verb` overhead, 8%.** 2,004 ms over 583 calls is **3.4 ms a call** and
   nothing explains it — `eth_claim` is a test-and-set and the dispatch is a
   table call. This is the most suspicious number in the table. It needs a
   finer stage inside `eth_pkg` before anything is changed.
4. **`rep movsw`, 11% → ~8%.** 25 clocks a word against `movsb`'s 17 a byte —
   12.5 vs 17. It needs odd-count and odd-alignment handling on both pointers,
   and a wrong tail byte in a ring copy is a corrupted transfer rather than a
   slow one. Do it after 1–3, measured.
5. **`pump` overhead, 4%.** 1.7 ms per pump even when the ring is empty:
   `tcp_timers` walks four sockets, then `dns_timer` and `dhcp_timer`. 582
   pumps for 234 frames — most find nothing. A cheap "has a tick passed"
   guard in front of the three timers would remove most of it.

## 6. The instruments, and how to take a reading

- **`make netbench`** — `NETBENCH.O88` beside `FTPD.O88` on one disk, three
  geometries. Open both, press **S**, run the transfer, press **X**, then
  **R**. **W** writes `NETBENCH.TXT` to the floppy. §72.15.
  - **BOOT THE `os8088*.img` BUILT ALONGSIDE IT.** `ETHER.DRV` ships on the
    *system* disk, so a netbench B: disk paired with a system disk somebody
    built earlier gets the **shipping** driver and `NETV_PROF` answers
    `NETE_VERB` — a profiler that refuses for no visible reason. The target
    builds both halves now; it used to build only the B: disk.
  - `wall` is S-to-X and **has your hands in it**; `ACTIVE` is the first
    payload byte to the last and is what the percentages divide by (§72.17).
  - `ACTIVE 0` with a `NO PAYLOAD MOVED` line means an idle machine, priced
    against the wall — which is a real measurement: the poll loop costs
    **3.0% of an idle 8088** (§72.18.1).
  - A save takes seconds and says `WRITING THE REPORT` while it does.
- **The FTP window's own second line** — `disk N net N draw N`, per transfer,
  in ms (§77.32). Same clock, so the two reports add.
- **`python3 tools/os88disk.py --verify-hdd IMG`** — an independent fsck for a
  partitioned image, including one behind an ST-11M-style reserved area.

### 6.1 The ETHPUMP A/B, on the 5150 — and why it reads backwards

Same 304552-byte `banana split.mod` STOR from WinSCP, two runs each way, over
the same session; `gap 0s` on all four.

| build | wall | rate | disk | net | draw |
|---|---|---|---|---|---|
| A — verbs pump (shipping) | 19s | **16029 B/s** | 3579 | 7649 | 33 |
| A — again | 19s | **16029 B/s** | 3586 | 7636 | 33 |
| B — worker pumps instead | 22s | **13843 B/s** | 3613 | 2497 | 33 |
| B — again | 22s | **13843 B/s** | 3621 | 2398 | 32 |

Repeatable to the byte per second, which is itself worth noting: the transfer
is deterministic enough that a 13.6% difference is not noise.

**The `net` column is the trap.** It is ftpd's own timer for time inside
`OSAPI_DRV_CALL` (§77.32), and it falls by 68% — the app really is spending a
third of the time in the driver that it used to. That is the redesign working
exactly as drawn, and it is not a win: the pumping did not get cheaper, it
moved into a task ftpd's split cannot see, and the wall clock is the only
column that counts the whole machine. **A self-timer that stops covering the
work is not the work stopping.**

Where it went: during a transfer the app calls verbs many times a tick, so
there is no gap for a worker to fill, and it becomes a third contender for the
card's mutex. `eth_claim` is answered `NETE_BUSY` and never waited on, and
`fd_recv_stage` abandons its whole drain on a `NETE_BUSY` (§77.30) — so one
collision costs the app a pass, not an instruction. SPEC.md §72.19 is the
fix: the worker test-and-clears a beat the verbs set, and pumps only after a
full turn in which nobody did. Busy stack: the A build. Idle stack, or one
whose package is inside a 400ms disk commit: pumped anyway.

**And it is worse than slow: on B the drag KILLED the transfer.** The field,
same session: *"Dragging the window during the write was smooth on B, but it
also killed the transfer — the file progress stops popping up, the writes stop,
and the client eventually times out on a control connection error."* On A the
same drag is choppier and the transfer survives it.

Both halves of that are the same cause and neither is a win:

- **The smoothness came from ftpd doing less per turn, not from the worker
  doing more.** A verb that pumps drains up to `ETH_BUDGET` = 8 frames, and 8
  frames is up to eighty milliseconds of byte-at-a-time DMA on whichever task
  called the verb. On B the verbs skipped that, so ftpd's worker turns got
  short and the drag loop got the CPU. **That points the latency work at
  `ETH_BUDGET`, not at where the pump runs** — a smaller or adaptive budget
  buys the same smoothness without moving anything to another task, and is the
  cheaper experiment of the two.
- **The stall is the contention above, taken to its limit.** A drag is the case
  where ftpd gets the fewest turns and the worker keeps taking the card on
  every one of its own, so a larger share of ftpd's few verb calls come back
  `NETE_BUSY` — and each one throws away a whole `fd_recv_stage` drain. The
  data path goes to nothing, and the control timeout is downstream of that
  rather than a second fault. *(Mechanism inferred, not instrumented: what is
  measured is that it happens on B and not on A.)*

**Round two — the standby, and the bug underneath all of it.** Same transfer,
B2 = `ETHPUMP=1` with the worker standing down whenever a verb had pumped:

| build | wall | rate | disk | net | draw |
|---|---|---|---|---|---|
| A — verbs pump | 19s | 16029 B/s | 3579 | 7649 | 33 |
| B — worker instead of verbs | 22s | 13843 B/s | 3613 | 2497 | 33 |
| B2 — worker on standby | 20s | **15227 B/s** | 3618 | 7140 | 144 |

`net` back to 7140 says the verbs are pumping again, and 5% is a lot better
than 13.6%. But **the drag still killed the transfer, and afterwards the client
could not even reconnect** — which is the finding, because a stalled transfer
is a contention story and a dead listener is not. Every verb was being refused,
including `ACCEPT`.

It was a register-contract bug, in the worker I wrote:

```
    mov cx, ETH_BUDGET
.f: call eth_claim
    call eth_pump1              ; ...which returns ne_rx's LENGTH in CX
    loop .f                     ; ...so this counts down from ~1500
```

`eth_pump1` is documented at its label as "CX is `ne_rx`'s length and
`eth_frame`'s input, so it flows between them and is the caller's to save", and
`eth_pump_i` keeps its budget in **BP** for exactly that reason. The worker's
eight-frame budget was really the last frame's length, so it drained the ring a
thousand frames at a time with no yield in it — and every package verb landing
in that storm got `NETE_BUSY`. A drag is where the package gets the fewest
turns, so it is where all of them landed in it.

Two things came out of the fix (SPEC.md §72.19): the budget moved to BP
and the worker re-reads the beat *inside* the drain, and — separately — the
beat moved to `eth_pkg`'s door and is set **before** the claim. Setting it only
where the claim succeeded is a stable livelock: a bounced verb leaves no trace,
so the worker reads an idle stack and pumps again forever. That is the half
that explains "cannot reconnect".

**On "cannot reconnect", and it is inferred rather than proven.** The field
waited 200s and it never cleared; stopping and restarting the server got a
connection and a `PWD` through before it timed out again. A restart does not
touch `[eth_busy]`, and `LISTEN`/`ACCEPT`/`SEND` all worked immediately after
one — so the card's mutex was **not** what was stuck. `NET_SOCKS` is **4**
(netpkg.inc), and a session in flight is already using three of them; a
transfer that dies with its connections stranded leaves nothing for `sk_alloc`
to hand the next `ACCEPT`. Stopping the server is what closes them. So the
starvation is the fault and the dead listener is downstream of it, which is why
the fix is aimed at the starvation and the reconnect is a thing to re-check
rather than a thing separately fixed.

**And the case the worker was FOR turned out not to need it.** A 300KB upload
is where the app polls hardest, so these runs look like the worst case for a
worker and the argument was that an idle server — an incoming SYN while the UI
repaints — was the case it would win. It is not: **ftpd calls verbs every UI
pass**, so an arriving SYN waits on ftpd's poll rate and not on the driver's.
The worker advances TCP state without the app, which is real and is worth
nothing while the app is asking anyway.

**So it was removed** (SPEC.md §72.19), along with the thing that started it:
"dragging the window during an upload kills the transfer" was never this
driver's — it was an event drain eating a package's wake (SPEC.md §74.1.1),
and it reproduced identically on a kernel with no worker in it. With that fixed
the field dragged the window for ten seconds at a stretch mid-transfer and
*"the transfer smoothly continued, no disconnect"* — on the plain build, with
no drag lag and no slowdown.

## 7. Rules this exercise re-learned the hard way

- **QEMU cannot see any of this.** It prices instructions at host speed: the
  `rep movsb` change moved its counts not at all. What QEMU proves is that the
  copies are still *exact* — `tests/ftpd.py` round-trips every byte value.
- **A prediction gets written down before the build ships, or it is worthless.**
  §72.16 predicted nine seconds and measured 8,670 ms. §72.13 predicted ~58
  KB/s from an *estimated* per-byte ceiling and was wrong by 8×.
- **Instrument before optimising.** Three rounds went into the part that was
  measurable rather than the part that was large, and a table found the real
  one in a single run.
- **An open bracket is the profiler bug you will write.** Twice: `prof_start`
  and `fd_tzero` both zero their counters from inside a stage that is already
  being timed, and the close then measures from the epoch. Re-stamp every
  open bracket on reset.

## 8. Open, and not throughput

`docs/FIELD-NOTES.md` **29** — the 5150 hard-freezes during an FTP session.
One cause was found and fixed (an unaligned disk buffer, §77.31) and it did
not close the note: the machine still freezes, most recently on a bare `PWD`.
That is the next piece of work and it is a correctness one, not a speed one.
