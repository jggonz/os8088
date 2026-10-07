#!/usr/bin/env python3
"""Does the pointer wear the CLOCK while the machine is frozen, and is it
the arrow again afterwards?

    make && python3 tests/curbusy.py

SPEC.md 7.5: the third cursor shape belongs to a gfx-lock HOLD rather than to a
window, because during that hold the UI task is not available to apply anything
- it is either the task inside the hold or the task waiting for it. So the hold
puts the picture on itself (`fpg_arm` for a file operation, `OSAPI_CUR_BUSY`
for a package that is about to go quiet) and `gfx_unlock` is what takes it off.

WHY THIS FILE EXISTS, and it is not the picture. The first build put the swap
inside SPEC.md 7.4.3.1's re-show, where it is free - and that block fired ZERO
times in a driven session, because a package launch and an assoc open both read
with the lock FREE (SPEC.md 7.4.2.1) and there is no hide to spend there. The
feature looked finished, assembled clean, passed the whole fast tier, and put
nothing on the screen. **Only driving it says whether it works**, so these are
the three assertions that were false the first time round:

  * the shape becomes the clock while SPEC.md 12.8's widget is armed
  * ...and it is DRAWN, not merely set - `[cur_level]` = 0 in the same sample.
    That one is not decoration: the picture may only change off the glass
    (SPEC.md 7.2.2), so a lit pointer has to be hidden first, and a version
    that let `cur_shape_set`'s own `cur_unlazy` hide it a SECOND time settled
    the refcount at -2 and left the pointer GONE for the whole freeze with the
    shape byte saying busy
  * ...and it is the ARROW again once the machine is idle
  * ...and a PACKAGE gets it by asking (Paint, decoding a picture: SPEC.md
    7.5.4, 42.6), with the lock its callback is already inside

Break either half on purpose - take `fpg_arm`'s `call cur_busy_on` out, or
Paint's `call OSAPI_CUR_BUSY` - and the matching row goes red.
"""
import argparse
import collections
import os
import sys
import threading
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
import os88marty                                            # noqa: E402
import os88ui                                               # noqa: E402

CUR_ARROWSH, CUR_BUSYSH = 0, 2
NAME = {0: "arrow", 1: "CROSS", 2: "CLOCK"}


class Watch(object):
    """Sample [cur_shape] off the guest while the driver works.

    A freeze is the one window in which nothing on the host can ask the guest
    to tell it anything - the guest is inside int 13h - so the shape is SAMPLED
    rather than waited for, and the assertion is over what was seen.
    """

    def __init__(self, m, addr, level=None):
        self.m, self.addr, self.level = m, addr, level
        self.seen = collections.Counter()
        self.stop = threading.Event()
        self.t = threading.Thread(target=self._run, daemon=True)

    def _run(self):
        while not self.stop.is_set():
            try:
                sh = self.m.read(self.addr, 1)[0]
                lv = ((self.m.read(self.level, 1)[0] + 128) % 256 - 128
                      if self.level is not None else 0)
                self.seen[(sh, lv >= 0)] += 1
            except Exception:                   # the debug server is busy
                pass

    def __enter__(self):
        self.t.start()
        return self

    def __exit__(self, *e):
        self.stop.set()
        self.t.join(timeout=3)


def fmt(seen):
    """`shape x drawn?` as one readable line."""
    return "  ".join("%s%s = %d" % (NAME.get(sh, sh), "" if up else "(HIDDEN)", n)
                     for (sh, up), n in sorted(seen.items()))


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/office360.img")
    a = ap.parse_args(argv)
    say = lambda s: print("  " + s, flush=True)
    bad = []

    with os88ui.boot(a.image, apps=a.apps, machine=a.machine) as ui:
        m = ui.m
        shape = m.sym("cur_shape")

        level = m.sym("cur_level")

        got = m.read(shape, 1)[0]
        say("at rest              cur_shape = %d %s" % (got, NAME.get(got, "?")))
        if got != CUR_ARROWSH:
            bad.append("at rest the pointer is %s, not the arrow"
                       % NAME.get(got, got))

        # --- 1. THE KERNEL'S HALF: a mount, a directory walk and a package
        #        load are all freezes, and two of the three hold no lock.
        with Watch(m, shape, level) as w:
            ui.open_drive("B")
            ui.path("B:/PAINT.O88")
        say("during a launch      %s" % fmt(w.seen))
        if not (w.seen.get((CUR_BUSYSH, True)) or w.seen.get((CUR_BUSYSH, False))):
            bad.append("the clock never went up for the file work")
        elif not w.seen.get((CUR_BUSYSH, True)):
            bad.append("the clock was SET but never DRAWN - [cur_level] "
                       "was negative in every sample (SPEC.md 7.5.3)")

        # --- 2. ...AND IT COMES OFF. gfx_unlock is what does it, so the wait
        #        is for a machine that has drawn something since (SPEC.md 7.5).
        try:
            os88marty.until(m, lambda _: m.read(shape, 1)[0] == CUR_ARROWSH,
                            "the arrow back", poll=0.5, limit=15)
        except os88marty.MartyError:
            pass                                # the read below reports it
        got = m.read(shape, 1)[0]
        say("once idle again      cur_shape = %d %s" % (got, NAME.get(got, "?")))
        if got != CUR_ARROWSH:
            bad.append("the clock survived the hold: cur_shape = %d" % got)

        # --- 3. THE PACKAGE'S HALF (SPEC.md 7.5.4). Paint asks for it around
        #        its picture decode, which is seconds on the target machine.
        #        The bp is on the SLOT's own entry, so this asserts that a
        #        package reached the kernel and not that a picture appeared.
        m.bp_exec("cur_busy")
        hit = {}

        def go():
            try:
                ui.path("B:/MEDIA/SAMPLE.BMP")
            except Exception as e:              # a breakpoint stops the guest
                hit["e"] = "%s: %s" % (type(e).__name__, str(e)[:80])

        th = threading.Thread(target=go, daemon=True)
        th.start()
        # Bounded in GUEST seconds. Not `until`: the gesture's own `advance`
        # pauses the guest for a moment, and that wait raises on a pause.
        #
        # AT THE BREAKPOINT, and not merely stopped: `stopped()` is also true
        # for the gesture's own momentary pauses, and a sample taken in one of
        # those reads the lock flag somewhere other than cur_busy's entry -
        # which is how this failed under a loaded soak with the clock on the
        # glass the whole decode ("lock held = 0", CLOCK = 59 below it).
        c0 = int(m.status().get("cycles", 0))
        want = m.sym("cur_busy") & 0xFFFFF

        def at_bp():
            st = m.status()
            return (st.get("state") == "breakpoint"
                    and ((st.get("cs", 0) << 4) + st.get("ip", 0)) & 0xFFFFF
                    == want)

        while not at_bp():
            if (int(m.status().get("cycles", 0)) - c0) / os88marty.GUEST_HZ \
                    > 30 * os88marty.GUEST_BUDGET_RATIO:
                break
            time.sleep(0.25)
        reached = at_bp()
        held = m.read(m.sym("gfx_lock_flag"), 1)[0] if reached else 0
        say("Paint decoding a BMP OSAPI_CUR_BUSY reached = %s, lock held = %d"
            % (reached, held))
        if not reached:
            bad.append("Paint never reached OSAPI_CUR_BUSY")
        elif not held:
            bad.append("Paint asked for the clock with no lock held, so "
                       "the kernel refused it (SPEC.md 7.5.4)")
        m.breakpoints([])
        m.run()
        with Watch(m, shape, level) as w:
            th.join(timeout=180)
        say("across the decode    %s" % fmt(w.seen))
        if not (w.seen.get((CUR_BUSYSH, True)) or w.seen.get((CUR_BUSYSH, False))):
            bad.append("the pointer was not the clock during the decode")

    for b in bad:
        say("FAIL " + b)
    print("curbusy: %s" % ("the pointer says the machine is busy, and stops "
                           "saying it - PASS" if not bad
                           else "the clock does NOT track the freeze"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
