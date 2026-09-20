# HANDOFF - The Wire's eighth connection never connects

**Status: DIAGNOSED, NOT FIXED.** `soak -k thewire` is red on this and on
nothing else; every other assertion in that row passes, including the whole
archive transfer, byte for byte.

Read this before diagnosing `thewire` yourself. The session that found it
spent most of its time ruling out three things that look like the answer and
are not, and the evidence for each is below so that nobody takes it again.

## 1. What happens

Step 10 of `tests/thewire.py` (SPEC.md 92.14) is **Load Program on the
archive**: fetch `TREEONE.WPK` again, mount a RAM disk, write the tree into
it, and hand the last entry - `MSEG.O88`, which carries `WAH_PROGRAM` - to
`OSAPI_PKG_START` **by name**.

It never starts. The Wire opens a socket, moves to `WS_WAIT`, and stays
there. Measured: **903 seconds** with `[wr_state]` = 2 (`WS_WAIT`) and
`[wr_job]` = 1 (`WJ_LOAD`) throughout. It is not slow, it is stopped.

`wrhttp.inc`'s `.wait` arm polls `NETV_STATUS`, sees `NSK_CONNECT` and falls
out to wait again. `NETV_OPEN` **succeeded** - it returned a handle, which is
the only way `WS_WAIT` is reached at all - and `ip_parse` took the dotted-quad
path, so `tcp_syn` ran. The socket then sits in `NSK_CONNECT` for ever.

**It is the EIGHTH connection of the session.** The seven before it all
completed: `catalog.bin`, `HELLO.PIC`, `BIGONE.O88`, `MINES.O88`,
`HELLO.PIC` again, `HELLO.O88`, and `TREEONE.WPK` for the Add chain.

## 2. The three things it is NOT

**Not contention.** The row is red at soak width 4 and red run alone. The
guest is not being starved: it is halted in a poll loop that never changes
state, and 903 guest-seconds of it changed nothing.

**Not the host.** `tests/thewire.py`'s `Server` is single-threaded and
serial, which is the first thing to suspect - and it is idle and ready. The
session instrumented it: during the whole wait `len(srv.asked)` goes **7 ->
7** and `srv.is_alive()` is `True`. The server finished request 7, closed it,
and is back in `accept()`. **No SYN arrives.** That is now asserted rather
than remembered - the row prints the count and fails on it (see 4 below).

**Not the launch.** This is what the row USED to say, and it is why the
finding took a session: the wait was a 120-second `time.sleep` loop that fell
straight through on a timeout, after which every assertion below it ran
against a machine that had not sent a byte. The reported failure was "Load
Program on the archive opened no window", which reads as `OSAPI_PKG_START`
refusing the parted package, and sends the reader through SPEC.md 21.5's four
`LD_*` codes for a transfer that never happened.

## 3. A REAL defect found on the way, which is NOT this one

`wr_hclose` (`apps/thewire/wrhttp.inc`) treats handle **0** as "no handle":

```asm
    cmp byte [wr_hnd], 0
    je .out                     ; nothing to close
```

and `wr_hnd`'s own comment says `0 = none`. But `sk_alloc`
(`drivers/ether/tcp.inc`) begins `xor al, al` and hands out **slot 0 as a
valid handle**, so a socket the Wire is given as 0 is never closed. That is a
leak, and it is real.

**IT IS NOT THE CAUSE, and that was tested rather than assumed**: biasing the
stored handle by one inside the Wire - so `0` means none and the driver's 0 is
storable - was built and run, and the hang is **identical**. The change was
reverted; do not re-derive it.

**IT IS SYSTEMIC, which is why it is written down here rather than fixed in
one package.** Every net client in the tree makes the same assumption:

| package | word |
|---|---|
| The Wire | `wr_hnd` |
| Browser | `br_hnd` |
| Telnet | `te_hnd` |
| FTP server | `fd_dhnd`, `fd_dlhnd`, `fd_lhnd` |

Four packages independently decided a handle is never 0, and all four rely on
bss-zero to mean "none". That is strong evidence the **ABI should guarantee
it** rather than four callers being wrong in the same way - so the fix
belongs at the driver's verb boundary (return slot+1 from `NETV_OPEN`, take
handle-1 in every verb that accepts one), not in a package. It cannot be done
in `sk_ptr`: that routine is used for the internal walks too, with AL
counting 0..`NET_SOCKS`-1, so making IT 1-based breaks `sk_alloc`.

It is an ABI change to SPEC.md 72's published surface and wants the owner's
call, which is why this session did not take it.

## 4. What the row says now

`tests/thewire.py` was changed so the next reader is not sent the same way:

* the wait records whether it **settled**, and a timeout now fails with the
  state, the job and **the host's request count during the wait**, saying in
  as many words that nothing below that line is evidence about
  `OSAPI_PKG_START`;
* the "was the host asked for the `.WPK`" check was a **false green** - it
  searched all of `srv.asked`, and the Add chain a few steps earlier fetched
  that exact path, so it was satisfied no matter what this step did. It
  requires a **new** request now, counted from a mark taken before the click.

## 5. Where to start

`drivers/ether/tcp.inc`'s `tcp_syn` and the socket table after seven
open/close cycles. The question is why the eighth `tcp_syn` puts nothing on
the wire when `NETV_OPEN` has just succeeded and the record says
`NSK_CONNECT`. Watch the NE2000's TX path with `make netbench`'s brackets
(SPEC.md 72.15), and note that `ETHER.DRV` is **QEMU-only to test** -
MartyPC has no NIC of any kind.

There is also no **connect timeout** anywhere in `wrhttp.inc`, and SPEC.md 92
does not specify one, so a socket stuck in `NSK_CONNECT` hangs the Wire for
the rest of the session with the status cell still saying it is working.
Whether that wants fixing here or in the stack is part of the same decision.
