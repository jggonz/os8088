# =============================================================================
# trace.py - price a code path in guest cycles, by breakpoint
#
# Put an `exec` breakpoint on every label of interest, inject an input, and
# stamp each stop with the guest's cycle counter. No code is added to the
# guest, so nothing is perturbed and the numbers are the shipped binary's.
#
# THREE THINGS THAT EACH PRODUCED A WRONG NUMBER FIRST (docs/NOTEPAD-NOTES.md
# 6.5). They are the whole reason this is a module rather than ten lines at a
# call site:
#
# 1. A LEG IS BILLED TO THE LABEL AT ITS END. The delta beside a name is the
#    time BEFORE that name, not inside it, so a routine with no breakpoint of
#    its own is invisible and its cost lands on whatever is traced next. A fix
#    was written against the wrong routine this way. Trace the entries either
#    side of anything you are about to blame.
# 2. `run` DOES NOT RESUME FROM A BREAKPOINT. MartyPC's ExecutionControl stays
#    latched in BreakpointHit through a Run and advances nothing, reporting
#    success and zero cycles - debug_server.rs says so in a comment, and this
#    harness hit it anyway. Hence step(1) then advance().
# 3. THE BUDGET IS A SILENT TRUNCATION. An `advance` shorter than the leg being
#    measured ends the trace early and reads as "the path stopped there". One
#    unbounded Note Pad walk is 22 million cycles.
# =============================================================================
from pkg import ms

DEFAULT_BUDGET = 80_000_000     # ~17 s of guest time: longer than any one leg
                                # this has needed, and see trap 3 above


class Tracer:
    def __init__(self, lab, labels, names):
        self.m = lab.m
        self.names = list(names)
        self.byaddr = {}
        self.bps = []
        for n in self.names:
            flat = (lab.seg << 4) + labels[n]
            self.byaddr[flat] = n
            self.bps.append({"type": "exec", "addr": flat})

    def arm(self):
        self.m.breakpoints(self.bps)

    def disarm(self):
        self.m.breakpoints([])

    def collect(self, budget=DEFAULT_BUDGET, maxhits=600):
        """[(label, cycles)] for every stop, in order, until nothing is hit."""
        hits = []
        while len(hits) < maxhits:
            self.m.step(1)                      # off the address we stopped AT
            r = self.m.advance(cycles=budget)
            if r["state"] != "breakpoint":
                break
            nm = self.byaddr.get(r["flat_ip"], "?%05x" % r["flat_ip"])
            hits.append((nm, r["cycles"]))
            if nm.startswith("?"):
                break
        return hits

    def press(self, key, budget=DEFAULT_BUDGET, settle=60):
        """Arm, inject one key, collect, disarm, settle. -> (t0, hits)."""
        self.arm()
        t0 = self.m.status()["cycles"]
        self.m.key(key)
        hits = self.collect(budget=budget)
        self.disarm()
        self.m.advance(frames=settle)
        return t0, hits


def report(t0, hits, title=""):
    if title:
        print("--- %s" % title)
    if not hits:
        print("    (no breakpoint hit)")
        return
    prev = t0
    for nm, c in hits:
        print("    %-26s %9.1f ms" % (nm, ms(c - prev)))
        prev = c
    print("    %-26s %9.1f ms" % ("TOTAL", ms(hits[-1][1] - t0)))


def total_ms(t0, hits):
    return ms(hits[-1][1] - t0) if hits else 0.0


def leg(hits, a, b):
    """Cycles from the first `a` to the first `b` at or after it."""
    ia = next((i for i, h in enumerate(hits) if h[0] == a), None)
    if ia is None:
        return None
    ib = next((i for i, h in enumerate(hits) if h[0] == b and i >= ia), None)
    return None if ib is None else hits[ib][1] - hits[ia][1]


def which(hits, candidates):
    """The first of `candidates` that appears in the trace."""
    seen = [n for n, _ in hits]
    for c in candidates:
        if c in seen:
            return c
    return "-"
