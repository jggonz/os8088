#!/usr/bin/env python3
"""os88lockw - who holds the gfx lock, for how long, and what do they DRAW?

    make marty
    python3 tools/os88lockw.py                     # 3 s of a quiet desktop
    python3 tools/os88lockw.py --secs 5 --each     # ...and every hold, listed

THE POINTER IS HIDDEN FOR THE WHOLE OF A LOCK HOLD (SPEC.md 7.1.4), so a hold
is a frozen mouse whether or not a pixel moves - and that is the measurement
this makes, because it is the one a screenshot cannot. Breakpoints on
`gfx_lock` and `gfx_unlock` bracket each hold and breakpoints on the
primitives count what was drawn inside it, all off MartyPC's cycle counter, so
the milliseconds are a 4.77 MHz 8088's and the guest is charged nothing.

WHAT IT IS FOR is the case where the answer is NONE. os8088's redraw paths are
built around not drawing what has not changed (SPEC.md 11.3, 28.2), and they
work - but a path that concludes "nothing changed" still has to walk, compose
and hash to get there, and it does that with the lock held. The Task Manager's
heap page measured 51.2 ms a refresh with ZERO drawing calls in it, twice a
second, which is a tenth of every second of frozen pointer buying nothing
(SPEC.md 28.6). That shape is invisible to every other instrument here: there
is no flash to catch, `font_run` means there is not even a blank interval, and
a counter in a primitive counts the draws that did not happen.

So: a hold with draws in it is work. A hold with none is a question.

A BARE DESKTOP MEASURES ZERO and that is correct - os8088 draws on events, so
a screen with nothing open on it is genuinely still. Open the window under
test first, or import `watch(m, secs)` and drive the machine yourself, which
is what tests do.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                             # noqa: E402
import os88sym                                               # noqa: E402

HZ = 4772727.0                  # the 5150's, which is what MartyPC counts in

# The bracket, then every primitive that puts pixels on the glass. Not an
# exhaustive list of the kernel's drawing surface - a blit or a pattern fill
# would want adding - but these four are what a text-and-frames window spends.
BRACKET = ("gfx_lock", "gfx_unlock")
DRAWS = ("font_run", "font_char", "gfx_fill", "gfx_hline", "gfx_frame")


def watch(m, secs, each=False, quiet=False):
    """Bracket every gfx lock hold for `secs` of GUEST time."""
    names = list(BRACKET) + [d for d in DRAWS if _has(d)]
    addr = {os88sym.linear(n): n for n in names}
    m.bp_exec(*names)
    held, holds, draws, cur = 0, [], {}, None
    c0 = m.status()["cycles"]
    window = int(secs * HZ)
    while True:
        m.run()
        if m.wait_stop(2.0) is None:
            # Nothing took the lock for two seconds, which on a BARE DESKTOP
            # is the right answer and not a failure: os8088 draws on events,
            # so a screen with no window on it is genuinely still. Open
            # something before measuring, or import watch() and drive it.
            break
        st = m.status()
        if st["cycles"] - c0 > window:
            break
        what = addr.get(st["flat_ip"])
        if what == "gfx_lock":
            cur = (st["cycles"], {})
        elif what == "gfx_unlock" and cur is not None:
            n = st["cycles"] - cur[0]
            held += n
            holds.append((n, cur[1]))
            cur = None
        elif what and cur is not None:
            cur[1][what] = cur[1].get(what, 0) + 1
            draws[what] = draws.get(what, 0) + 1
    span = m.status()["cycles"] - c0
    m.bp_exec()
    m.run()
    if not quiet:
        if each:
            for n, d in holds:
                print("  %8d cycles = %6.1f ms   %s"
                      % (n, n * 1000.0 / HZ, d or "NO DRAWING AT ALL"))
        print("%d hold(s), %.1f ms of %.2f s = %.1f%% of the time with the "
              "pointer hidden; draws: %s"
              % (len(holds), held * 1000.0 / HZ, span / HZ,
                 100.0 * held / max(span, 1), draws or "NONE"))
    return {"holds": holds, "held": held, "span": span, "draws": draws}


def _has(name):
    try:
        os88sym.linear(name)
        return True
    except Exception:
        return False


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--secs", type=float, default=3.0,
                    help="guest seconds to watch (default 3)")
    ap.add_argument("--each", action="store_true", help="list every hold")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    a = ap.parse_args(argv)
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        watch(m, a.secs, each=a.each)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
