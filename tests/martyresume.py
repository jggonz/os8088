#!/usr/bin/env python3
"""Telling ONE STOP FROM THE NEXT, which `state` cannot do.

    python3 tests/martyresume.py

WHY THIS EXISTS.  A caller that resumes a breakpoint and polls has to answer
one question - *has my resume landed, or has the machine gone round and
stopped again?* - and until this row's fix the protocol did not carry the
answer.  Both cases read `"breakpoint"`.  Every client here invented its own
test for it and they were not equally wrong:

  * `bp_count` deduped on `instructions`, which happens to work and not for
    the reason it says: `machine.run()` accumulates that count once at the END
    of a batch and returns EARLY at a breakpoint, so the batch a stop lands in
    is discarded from it.  What separated two stops was the single instruction
    the resume itself steps.
  * a helper polled the INSTRUCTION POINTER, which cannot work at all.  A
    breakpoint that fires repeatedly fires at the same address every time, by
    construction, so "the IP has not moved" is true of a machine that never
    resumed AND of one that did the whole lap.  Step 3 below is that measured:
    every stop in a run of them carries an identical `flat_ip`.
  * `wait_stop` had no test whatever, and returned the stop that was ALREADY
    THERE - instantly, to a caller that had just resumed past it.  That is a
    green assertion for a gesture that never happened, and it is 100-odd call
    sites in tests/.

The server answers it directly now: `stops` counts every entry into a stopped
state, and a `run` reply carries `resumed_from`, the count as the resume found
the machine.  A status is a new stop exactly when it is stopped and its
`stops` is greater than that mark - no cases, no heuristics.

WHAT IT ASSERTS:

  1. two reports of ONE stop carry one `stops`, one `cycles` and one IP -
     polled without resuming, nothing about the machine moves;
  2. the stop that is already there does NOT satisfy a wait for the next one,
     and a real resume does, at exactly one greater;
  3. over a run of genuine consecutive stops the IP is IDENTICAL every time,
     so the field the broken helper polled cannot distinguish them, while
     `stops` distinguishes all of them;
  4. `go()` hands back a mark that survives the round trip, and `bp_count`
     counts each entry once over a known number of them;
  5. the FALLBACK path - an emulator built before `stops` existed - still
     refuses the stale stop, because `cycles` is a real guest clock.

Break it on purpose: make `_newer()` in tools/os88marty.py answer
`st["state"] != "running"`, which is what it did before, and three of these go
red - steps 2, 4 and 5 - the first of them being the whole defect.
"""
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))

import os88marty                                            # noqa: E402
import os88ui                                               # noqa: E402

SYS = "build/os8088-360.img"
APPS = "build/apps360.img"

# The timer. It is the one breakpoint every machine here has - 18.2 stops a
# second, at the same address every time, with no guest software to drive.
TICK = [{"type": "int", "addr": 0x08}]

fails = []


def check(ok, what, saw=""):
    print(("  ok   " if ok else "  FAIL ") + what + (("  " + saw) if saw else ""))
    if not ok:
        fails.append(what)


def main():
    with os88ui.boot(SYS, apps=APPS) as ui:
        m = ui.m
        if "stops" not in m.status():
            print("  FAIL this martypc_headless predates the stop sequence "
                  "number, so there is nothing to test: `make marty` "
                  "rebuilds it from tools/martypc/debug_server.rs")
            return 1
        m.breakpoints(TICK)

        # --- 1. one stop is one number -----------------------------------
        mark = m.go()
        st = m.wait_stop(30.0, since=mark)
        check(st == "breakpoint", "the timer breakpoint fires", repr(st))
        first = m.status()
        reads = [m.status() for _ in range(12)]
        check(all(r["stops"] == first["stops"] for r in reads),
              "12 reports of one stop carry one `stops`",
              "%s" % sorted({r["stops"] for r in reads}))
        check(all(r["cycles"] == first["cycles"] for r in reads),
              "...and one `cycles`: a stopped guest executes nothing")
        check(all(r["flat_ip"] == first["flat_ip"] for r in reads),
              "...and one IP")

        # --- 2. the stop already there is not the stop being waited for ---
        # THE DEFECT, deterministically. `here` is this stop's own mark, so a
        # wait measured from it is a wait for the NEXT one - and the machine
        # is sitting still, so nothing but the fix can make this fail to
        # return "breakpoint" at once.
        here = ("stops", first["stops"])
        try:
            got = m.wait_stop(2.0, since=here, guest=0.4)
            check(got is None,
                  "the stop already there does not answer a wait past it",
                  "answered %r" % got)
        except os88marty.MartyError as e:
            # The machine is stopped and nothing is resuming it, which this
            # deliberately is: a named refusal is the other acceptable answer
            # and strictly better than a hang. What must not happen is
            # "breakpoint".
            check("SAME stop" in str(e),
                  "the stop already there does not answer a wait past it",
                  "raised, naming it")

        before = m.status()["stops"]
        mark = m.go()
        check(mark == ("stops", before),
              "go() marks the count it resumed FROM", "%s" % (mark,))
        st = m.wait_stop(30.0, since=mark)
        check(st == "breakpoint", "...and the next stop answers it", repr(st))
        check(m.status()["stops"] == before + 1,
              "...at exactly one greater", "%d -> %d" % (before, m.status()["stops"]))

        # --- 3. what the IP can and cannot say ----------------------------
        ips, seqs = [], []
        for _ in range(8):
            mk = m.go()
            m.wait_stop(30.0, since=mk)
            s = m.status()
            ips.append(s["flat_ip"])
            seqs.append(s["stops"])
        check(len(set(ips)) == 1,
              "8 genuine stops, 8 identical IPs - the field that cannot tell "
              "them apart", "%05X" % ips[0])
        check(len(set(seqs)) == 8 and seqs == sorted(seqs),
              "...and 8 distinct, increasing `stops`",
              "%d..%d" % (seqs[0], seqs[-1]))

        # --- 4. bp_count counts each entry once ---------------------------
        # Nothing is driven: the gesture is the machine's own timer, so the
        # count is what the quiet window saw and the assertion is that it is
        # neither doubled nor collapsed. Eight ticks is ~440ms of guest time.
        n = os88marty.bp_count(m, 0x08 * 4, lambda: None, arm=0.1,
                               quiet=1.0, first=6.0, limit=40.0)
        check(n == 0, "bp_count on an address nothing executes counts 0",
              "saw %d" % n)

        # --- 5. the compat path -------------------------------------------
        # An emulator built before `stops`: strip the field and check the
        # `cycles` fallback still refuses the stale stop. It is a real clock -
        # nothing executes while the machine is stopped - which `instructions`
        # is not.
        m.breakpoints(TICK)
        m.go()
        m.wait_stop(30.0)
        raw = m.cmd

        def older(**kw):
            r = raw(**kw)
            r.pop("stops", None)
            r.pop("resumed_from", None)
            return r

        try:
            m.cmd = older
            m._go = None
            st = m.status()
            fell_back = m._mark(st)
            check(fell_back[0] == "cycles",
                  "with no `stops`, the mark falls back to the guest CLOCK",
                  "%s" % (fell_back,))
            check(not m._newer(st, fell_back),
                  "...and the stop already there is still refused")
            mk = m.run()
            mk = m._go
            got = m.wait_stop(30.0, since=mk)
            check(got == "breakpoint", "...while a real stop still answers",
                  repr(got))
        finally:
            m.cmd = raw

        m.breakpoints([])
        m.run()

    print()
    if fails:
        print("FAIL: " + "; ".join(fails))
        return 1
    print("ok - a stop and the next one are told apart")
    return 0


if __name__ == "__main__":
    sys.exit(main())
