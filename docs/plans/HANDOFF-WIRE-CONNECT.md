# HANDOFF - The Wire's eighth transfer never gets off the ground

**Status: DIAGNOSED FURTHER, STILL NOT FIXED.** `soak -k thewire` is red on
this and on nothing else; every other assertion in that row passes, including
the whole archive transfer, byte for byte.

**THIS FILE EXISTS ONLY UNTIL THE BUG IS FIXED, AND IT GOES WITH THE FIX.**
Its first revision sent a reader to `tcp_syn` and to an ABI change, and both
were wrong; what follows is measured rather than inferred. `WIRE_SOCKDUMP=1
python3 tests/thewire.py` re-takes every reading below in one run.

## 1. What happens

Step 10 of `tests/thewire.py` (SPEC.md 92.14) is **Load Program on the
archive**: fetch `TREEONE.WPK` again, mount a RAM disk, write the tree into
it, and hand the last entry - `MSEG.O88`, which carries `WAH_PROGRAM` - to
`OSAPI_PKG_START` by name.

It never starts. `[wr_state]` sits at `WS_WAIT` (2) with `[wr_job]` = 1
(`WJ_LOAD`) for as long as anyone waits - 903 seconds in one measurement. It
is not slow, it is stopped.

## 2. WHAT THE FIRST REVISION GOT WRONG

Two of its conclusions are refuted, and a reader who follows them loses a
session.

**"The eighth `tcp_syn` puts nothing on the wire."** It does. The socket is
read straight out of `ETHER.DRV`'s bss at the hang and it is armed correctly:

```
sk[0]  SKO_ST = 01 (NSK_CONNECT)   SKO_TS = 02 (TS_SYNSENT)
       SKO_RP = 1F9C = 8092        SKO_RIP = 10.0.2.2
       SKO_TMO = 2063 ticks        SKO_IDX = 0
```

`TS_SYNSENT` is set by `tcp_syn` AFTER `tcp_out`, so `tcp_syn` ran to the end.
The connection is not refused, misaddressed or unresolved: `dns_sk` is `FF`
(no lookup outstanding) and `SKO_RIP` is the dotted quad from `WIRE.CFG`.

**"`sk_alloc` hands out slot 0 as a valid handle", and the ABI should be made
1-based.** Handles are ALREADY 1-based. `sk_alloc` ends

```asm
    mov [bx+SKO_IDX], al
    inc al                      ; handles are 1-based: 0 is "no handle"
```

and `sk_h2p` opens `or al, al / jz .no` and then `dec al`. So `0` really does
mean "no handle", `wr_hclose`'s `cmp byte [wr_hnd], 0 / je .out` is correct,
there is no leak, and there is nothing to change in SPEC.md 72's published
surface. It also explains why biasing the stored handle by one "changed
nothing": it was already biased. **The four packages listed in that revision
are not wrong in the same way; they are right.**

## 3. WHAT IS ACTUALLY WRONG: NOTHING PUMPS THE STACK AGAIN

Measured twice, thirty seconds apart, with the guest running throughout:

```
pass 0  tick=1188222  TS=02  TMO=2065  (tick-TMO=1186157)  TRY=0  nrx=115
pass 1  tick=1188768  TS=02  TMO=2065  (tick-TMO=1186703)  TRY=0  nrx=115
```

Three things in that, and together they are the finding:

* **`TMO` is never re-armed and `TRY` never increments.** `tcp_timers`'
  `.ctmo` arm must `tcp_abort` a `TS_SYNSENT` socket once `eth_now` passes
  `SKO_TMO` - `TCP_CONNTMO` is 550 ticks, about 30 seconds - and the deadline
  here is over a million ticks in the past. So `tcp_timers` never walks this
  socket.
* **`eth_nrx` does not move.** No frame is taken off the card in thirty
  seconds.
* `eth_pump_i` always reaches `eth_ptimers` once it is entered - the budget
  loop falls through to `.timers` whether the ring emptied or the budget ran
  out - so **`eth_pump` is not being called at all**, which means no `NETV_*`
  verb is being issued by anything on the machine.

That is why there is no retransmit, no connect timeout and no progress. The
stack is not broken; it is not being run.

## 4. WHAT IT IS NOT - each one read, not reasoned

| suspect | reading at the hang |
|---|---|
| the card mutex (SPEC.md 72.2.2) | `eth_busy = 00` - free |
| the raw consumer (72.22), which skips drain AND timers | `eth_raw = 00` |
| the link | `eth_up = 1` |
| a name that never resolved | `dns_sk = FF`, `SKO_RIP` = 10.0.2.2 |
| a compaction park (66.5.5) | `inst_parkreq = 00`, `sch_parked` all zero |
| the gfx lock | `gfx_lock_flag = 00`, `own = FF`, `want = 00` |
| the worker having DIED | it has not - see below |

And it is **not contention**: the row is red alone and red at soak width 4,
and the guest is executing throughout.

## 5. WHERE IT IS: THE WORKER IS READY AND GOING NOWHERE

`wr_worker` is `OSAPI_TASK_ALIVE` / `wr_nstep` / `OSAPI_TASK_SLEEP(1)` round a
loop, and `wr_nstep`'s `.wait` arm calls `wr_qstat` -> `NETV_STATUS`
unconditionally, which pumps. So a running worker moves `eth_nrx`. It does
not.

The task table at the hang:

```
tasks: 0:st2/instFF   1:st1/instFF   4:st1/inst00
the Wire's window 0 is owned by instance 00        wr_hired = 01
```

`T_STATE` is 0 free / 1 ready / 2 sleeping. **Task 4 is the Wire's worker -
`T_INST` = 0 and the Wire's window is instance 0 - and it is READY**: not
dead, not sleeping, not parked. It is runnable and the UI task is asleep, so
it is getting the CPU and still not reaching `wr_nstep`.

Its saved frame (`T_SP` = 0CE2, stacks at `LOW_SEG`) carries a far return to
`90C0:1DDE`, which the package's own map resolves to **`wr_nshow + 8`** -
`wr_nshow` being three `push`es and then `call OSAPI_GFX_LOCK`, so that is the
return address from the lock. But the lock is FREE and the anti-starvation
rule in `gfx_lock.free` cannot hold this task (`gfx_lock_last` = 0, `sch_cur`
= 4), so it is not stuck there either - which means that word is an OLDER
frame and the worker is deeper in. Reading it out of a raw stack dump has
given everything it can.

**Start here**: `make debug` and gdb on the worker's task, or a counter in
`wr_nstep`'s head. The question is one line long - where is task 4 spinning? -
and everything around it is now answered.

One more thing to fix whatever the cause turns out to be: **`[wr_hired]` is a
latch nothing clears.** `wr_hire` returns at once when it is 1, so a Wire that
loses its worker never gets another one for the rest of the session, and the
window stays open and dead.

## 6. What the row says, and the instrument

`tests/thewire.py` records whether the step **settled**, and a timeout fails
with the state, the job and the host's request count during the wait, saying
in as many words that nothing below that line is evidence about
`OSAPI_PKG_START`. The "was the host asked for the `.WPK`" check was a false
green - it searched all of `srv.asked`, and the Add chain a few steps earlier
fetched that exact path - and requires a NEW request now, counted from a mark
taken before the click.

`WIRE_SOCKDUMP=1` is every reading in this file, taken in one run on the
failure. It costs the green path nothing and `ether_syms` refuses a map that
is not byte-for-byte `build/ether.bin`, so the offsets are this driver's.

## 7. A real defect found on the way, which is NOT this one

There is a socket state that `tcp_timers` skips entirely. `eth_v_open` sets
`SKO_ST = NSK_CONNECT` **before** `ip_parse`; on the name path it sets
`SKF_RES`, arms `SKO_TMO` and returns with `SKO_TS` still 0 - and
`tcp_timers`' loop opens `cmp byte [bx+SKO_TS], TS_CLOSED / je .next`. So a
socket waiting on DNS gets no retransmit and no connect deadline from that
routine; only `dns_timer` can end it, and `dns_timer` only scans for new work
while `[dns_sk] == 0xFF`. Nothing in this row reaches it - `wr_host` is a
dotted quad - but a name that never resolves while the resolver is busy would
hang the same way.
