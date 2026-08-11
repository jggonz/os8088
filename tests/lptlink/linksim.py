#!/usr/bin/env python3
"""lptlink.asm's link layer, as two co-routined ends over a modelled cable.

Run it:  python3 tests/lptlink/linksim.py

A host-side model of the ALGORITHM, transcribed from lp_wfar / lp_turn /
lp_snib / lp_rnib / slv_hunt, because this is a two-machine protocol and there
are only two machines. SPEC.md 18.94.3 set the precedent: the sector cache was
simulated against a real trace before it was built rather than after.

It earned its keep immediately. Three defects, none of which would have shown
up on a matched pair on the bench and all of which would have shown up in the
field as "the cable does not work":

  1. The handshake was a two-phase TOGGLE - sender puts nibble+phase, receiver
     echoes the phase as its ack. Correct within one direction and RACY at a
     direction reversal: the old receiver acks and then, as the new sender,
     immediately drives the same line, so the old sender never sees the ack
     and waits for ever.
  2. Four phases fixed that race and left the same one at the seam, because
     the receiver's ack-down and its first strobe-up are microseconds apart.
     lp_turn is the answer - a whole system tick before a reversed end drives
     anything - and it is called from lp_snib on a flag rather than from the
     six sites that reverse, because a guard a call site can forget is a hang
     on somebody else's pair of machines.
  3. The timeouts were counted in POLLS, and a poll is not the same length on
     the two ends of this cable. The fast end's patience ran out inside the
     slow end's ordinary response - and worse, inside the guard that exists to
     protect it. Every deadline is in TICKS now, which both machines measure
     identically.

Generators rather than threads: a two-party protocol has to be STEPPED to be
debugged, and each `yield` is one poll, charged to a shared clock at that
end's own per-poll cost.
"""
LP_SPIN = 5000
LP_TMO = 2                       # ticks
TURN_RX = 8                      # ticks
TICK_US = 54925                  # 18.2065 Hz
MAGQ = 0xF48383F3


class Clock:
    """One shared wall clock in microseconds. Each end advances it by its OWN
    per-poll cost, which is what makes a speed ratio mean something: on an ISA
    bus the poll cost is the BUS, not the CPU, so a 5150 is ~15us and anything
    modern is ~1us - about 15:1, not 300:1."""
    def __init__(self):
        self.us = 0

    def ticks(self):
        return (self.us // TICK_US) & 0xFFFF


class Port:
    def __init__(self):
        self.data = 0x00
        self.peer = None

    def status(self):
        d = self.peer.data & 0x1F
        return (((d << 3) & 0xF8) ^ 0x80) | 0x05   # junk in bits 0..2


class End:
    def __init__(self, port, name, clock, poll_us):
        self.p = port
        self.name = name
        self.clk = clock
        self.poll_us = poll_us
        self.p.data = 0x00
        self.lastop = False
        self.trace = []

    def wfar(self, want_high, allow_ticks):
        """lp_wfar: poll until the far D4 reaches a level, bounded IN TICKS."""
        deadline = self.clk.ticks() + allow_ticks
        while True:
            for _ in range(LP_SPIN):
                v = self.p.status() ^ 0x80
                if bool(v & 0x80) == want_high:
                    return v
                self.clk.us += self.poll_us
                yield "poll"
            if ((self.clk.ticks() - deadline) & 0xFFFF) < 0x8000:
                return None

    def turn(self):
        """lp_turn: two tick edges, so at least one whole tick has passed."""
        t = self.clk.ticks()
        while self.clk.ticks() == t:
            self.clk.us += self.poll_us
            yield "turn"
        t = self.clk.ticks()
        while self.clk.ticks() == t:
            self.clk.us += self.poll_us
            yield "turn"

    def snib(self, nib):
        """lp_snib. Four-phase, plus lp_turn's guard on a direction reversal."""
        if self.lastop:                       # a reversal owes lp_turn
            yield from self.turn()
            self.lastop = False
        n = nib & 0x0F
        self.p.data = n                       # nibble first, strobe low
        self.p.data = n | 0x10                # ...then the strobe
        if (yield from self.wfar(True, LP_TMO)) is None:
            self.p.data = n
            self.trace.append("snib: ack never rose")
            return False
        self.p.data = n                       # strobe down, nibble held
        if (yield from self.wfar(False, LP_TMO)) is None:
            self.p.data = n
            self.trace.append("snib: ack never fell")
            return False
        return True

    def rnib(self):
        """lp_rnib. A reversal INTO receiving waits out the far end's guard."""
        allow = LP_TMO if self.lastop else TURN_RX
        v = yield from self.wfar(True, allow)
        if v is None:
            self.p.data = 0x00
            self.lastop = True
            self.trace.append("rnib: strobe never rose")
            return None
        nib = (v >> 3) & 0x0F                 # sampled by the read that saw it
        self.p.data = 0x10                    # ack up
        if (yield from self.wfar(False, LP_TMO)) is None:
            self.p.data = 0x00
            self.lastop = True
            self.trace.append("rnib: strobe never fell")
            return None
        self.p.data = 0x00                    # ack down - both ends idle
        self.lastop = True
        return nib

    def sbyte(self, b):
        if not (yield from self.snib(b & 0x0F)):
            return False
        return (yield from self.snib((b >> 4) & 0x0F))

    def rbyte(self):
        lo = yield from self.rnib()
        if lo is None:
            return None
        hi = yield from self.rnib()
        if hi is None:
            return None
        return ((hi << 4) | lo) & 0xFF


def master(A, nbytes):
    for byte in b"O88?":
        if not (yield from A.sbyte(byte)):
            return "hello send failed"
    for byte in b"O88!" + bytes([1]):
        got = yield from A.rbyte()
        if got != byte:
            return f"hello reply mismatch: got {got}, want {byte}"
    for i in range(nbytes):                       # 'B' - a run outbound
        if not (yield from A.sbyte((i * 7) & 0xFF)):
            return f"outbound stalled at {i}"
    errs = 0
    for i in range(nbytes):                       # 'R' - a run inbound
        got = yield from A.rbyte()
        if got is None:
            return f"inbound stalled at {i}"
        if got != ((i * 11) & 0xFF):
            errs += 1
    return "ok" if errs == 0 else f"{errs} inbound mismatches"


def slave(B, nbytes):
    win = 0
    n = 0
    while True:                                    # slv_hunt
        nib = yield from B.rnib()
        if nib is None:
            return "hunt timed out"
        win = ((win << 4) | nib) & 0xFFFFFFFF
        n += 1
        if win == MAGQ:
            break
        if n > 64:
            return "hunt never locked"
    for byte in b"O88!" + bytes([1]):
        if not (yield from B.sbyte(byte)):
            return "reply failed"
    errs = 0
    for i in range(nbytes):
        got = yield from B.rbyte()
        if got is None:
            return f"take stalled at {i}"
        if got != ((i * 7) & 0xFF):
            errs += 1
    for i in range(nbytes):
        if not (yield from B.sbyte((i * 11) & 0xFF)):
            return f"give stalled at {i}"
    return "ok" if errs == 0 else f"{errs} received mismatches"


def run(nbytes=256, verbose=False, ratio=1):
    """ratio = how many steps the SLAVE takes per master step. 1 is a matched
    pair; 300 is a 4.77MHz 8088 master against something modern, which is the
    pairing this whole tool exists for and the one the two-phase handshake
    could never survive."""
    a, b = Port(), Port()
    a.peer, b.peer = b, a
    clk = Clock()
    A = End(a, "master", clk, 15)                 # a 5150: ~15us per poll
    B = End(b, "slave", clk, max(1, 15 // ratio)) # ...against whatever
    ga, gb = master(A, nbytes), slave(B, nbytes)
    ra = rb = None
    steps = 0
    while ra is None or rb is None:
        steps += 1
        if steps > 40_000_000:
            return "DEADLOCK", A.trace + B.trace, clk.us
        if ra is None:
            try:
                next(ga)
            except StopIteration as e:
                ra = e.value
        for _ in range(ratio):
            if rb is None:
                try:
                    next(gb)
                except StopIteration as e:
                    rb = e.value
    return (ra, rb), A.trace + B.trace, clk.us


def magic_window(data):
    """slv_hunt's window: 8 nibbles, low nibble of each byte first."""
    win = 0
    for b in data:
        for nib in (b & 0x0F, (b >> 4) & 0x0F):
            win = ((win << 4) | nib) & 0xFFFFFFFF
    return win


def main():
    fails = 0

    # 1. the decode: xor 0x80, shr 3, and 0x1F recovers what the far end drove
    a, b = Port(), Port()
    a.peer, b.peer = b, a
    bad = [d for d in range(0x20)
           if (((b.__setattr__("data", d) or a.status()) ^ 0x80) >> 3) & 0x1F != d]
    if bad:
        print(f"FAIL decode: {bad}")
        fails += 1
    else:
        print("decode:  32/32 nibble+strobe values recovered through the cable")

    # 2. the hand-computed magic constant
    win = magic_window(b"O88?")
    if win != MAGQ:
        print(f"FAIL magic: computed {win:08X}, lptlink.asm says {MAGQ:08X}")
        fails += 1
    else:
        print(f"magic:   O88? -> {win:08X} = MAGQ_HI/MAGQ_LO in the .asm")

    # 3. the hunt locks on at BOTH nibble alignments, which is the whole
    #    reason the window is nibbles and not bytes
    for skew in (0, 1):
        nibs = []
        for byte in b"\x5A\xA5" + b"O88?":
            nibs += [byte & 0x0F, (byte >> 4) & 0x0F]
        win = 0
        locked = False
        for nib in nibs[skew:]:
            win = ((win << 4) | nib) & 0xFFFFFFFF
            locked = locked or win == MAGQ
        if not locked:
            print(f"FAIL resync: nibble skew {skew} never locked")
            fails += 1
    else:
        print("resync:  the hunt locks on at both nibble alignments")

    # 4. the whole exchange, across the speed ratios this cable will see.
    #    The far end's poll cost is bounded BELOW by the ISA bus (~1us), not
    #    by its CPU, so 15:1 is about the real limit and 150:1 cannot happen.
    print("link:    hello + 256B each way + two direction reversals")
    for ratio in (1, 2, 5, 15, 50, 150):
        res, trace, us = run(256, ratio=ratio)
        tag = f"far end {max(1, 15 // ratio)}us/poll"
        if res != ("ok", "ok"):
            print(f"  FAIL {tag}: {res}")
            for t in trace:
                print("       ", t)
            fails += 1
        else:
            print(f"  ok   {tag:>20}")

    # ...and what the model says the wire would do, which is a MODEL and goes
    # nowhere near PERFORMANCE.md. The .TXT off two real machines is the
    # number (Part 6 rule 8).
    res, _, us = run(4096, ratio=15)
    if res == ("ok", "ok") and us:
        print(f"\nmodelled: {int(4096 * 2 * 1_000_000 / us):,} bytes/sec for a 5150 "
              f"against a modern far end")
        print("          - a MODEL. The figure that counts comes off two machines.")

    print(f"\nFAILURES: {fails}")
    return 1 if fails else 0


if __name__ == "__main__":
    raise SystemExit(main())
