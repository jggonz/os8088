"""A poll's budget on the GUEST's clock, for a loop that cannot be an `until`.

    import os88deadline
    d = os88deadline.Guest(m, 60)               # was: t0 = time.time()
    while d:                                    # was: while time.time() - t0 < 60
        if m.stopped():
            break
        time.sleep(0.02)

WHY (docs/plans/soak-speed-2/flake.md). `t0 = time.time(); while time.time()
- t0 < 60:` round a read of the guest gives the guest whatever 60 host
seconds buy: ~270 guest seconds on an idle box, a fifth of that with eight
emulators on four cores - so the same loop is generous alone and a red row
in a loaded soak, red as "the feature never happened". os88marty's `until`
and `quiesce` already budget on the guest's clock; this is the same budget
for the loop shapes they do not fit (a breakpoint pump that must also watch
`m.stopped()`, a loop that collects as it goes, a `while cond and budget`).

`Guest(m, secs)` is TRUE while the guest has spent fewer than
`secs * ratio` of its own seconds since construction, where `ratio` is
os88marty's GUEST_BUDGET_RATIO (the conversion `until(limit=)` uses: a
host-second figure written on an idle box, as guest time) unless `guest=`
names the guest budget directly. It turns FALSE at a host backstop
`backstop` times wider than the old host figure as well, so a guest that has
STOPPED - a breakpoint the loop does not test for, an emulator gone - still
ends the loop rather than hanging the row; os88test's CPU-charged timeout is
the outer bound past that. `spent()` answers the guest seconds so far and
`why()` says which limit ended it, for the failure message.

Anything with `status()["cycles"]` works (a Marty, the Bochs shim).
"""
import time

try:
    import os88marty
    _HZ = os88marty.GUEST_HZ
    _RATIO = os88marty.GUEST_BUDGET_RATIO
except Exception:                       # pragma: no cover - import order
    _HZ, _RATIO = 4772727.0, 3.0


class Guest:
    __slots__ = ("m", "budget", "c0", "t0", "host", "ended")

    def __init__(self, m, secs=None, guest=None, backstop=10.0):
        self.m = m
        self.budget = guest if guest is not None else secs * _RATIO
        self.c0 = self._cycles()
        self.t0 = time.time()
        # host: a STOPPED guest's backstop only; the budget is the guest's
        self.host = (secs if secs is not None else self.budget) * backstop
        self.ended = None

    def _cycles(self):
        try:
            return int(self.m.status().get("cycles", 0))
        except Exception:
            return getattr(self, "c0", 0)

    def spent(self):
        return (self._cycles() - self.c0) / _HZ

    def __bool__(self):
        if self.spent() >= self.budget:
            self.ended = "the guest spent its %.0f-second budget" % self.budget
            return False
        if time.time() - self.t0 >= self.host:      # host: backstop only
            self.ended = ("the HOST backstop (%.0fs) with only %.1f guest s "
                          "spent - the guest stopped or the box starved it"
                          % (self.host, self.spent()))
            return False
        return True

    def why(self):
        return self.ended or "not ended"
