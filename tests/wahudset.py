#!/usr/bin/env python3
"""WireArena says a setting when a key changes it (SPEC.md 128.2): the HUD's
message, row 24, for four seconds - the owner, 2026-11-15: "lets add a toast
that tells you when you change a setting, and what to ... right now I just
have to see what changes".

On MartyPC's 5150 with a CGA, a match alone at 40 columns, each key in turn,
each read twice - out of the game's own message buffer (`wa_hmsg`, `wa_hmsl`)
and off the glass (text memory's row 24), so a message composed and never
drawn is red too:

  - F2, F5, F3: "Detail Low", "Detail Very High", "Detail Medium" - and the
    level is the one named (`wa_level`), vsync on with Very High alone;
  - V twice: "Vsync on", then "Vsync off" (`wa_vsync` agreeing);
  - + then -: "Columns 80", then "Columns 40" - the width is a mode set,
    and the message is drawn on the NEW screen.

Registered as wahudset in soak; make wirearenadisk first.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, 'tools'), os.path.join(ROOT, 'tests')]
import os88marty                                                # noqa: E402
import os88ui                                                   # noqa: E402
from os88fixture import need                                    # noqa: E402
from walift import amap, start, SYS, APPS, TICK                 # noqa: E402

fails = []


def check(ok, what):
    print('  %s %s' % ('ok  ' if ok else 'FAIL', what), flush=True)
    if not ok:
        fails.append(what)


def main():
    need(APPS)
    sym = amap()
    with os88ui.boot(SYS, apps=APPS, machine="os8088_5150_cga",
                     quick=True) as ui:
        m = ui.m
        w = ui.path("B:/WIRERENA.O88")
        p = start(ui, w, sym)
        f0 = p.byte(sym["wa_frame"])
        os88marty.until(m, lambda _: p.byte(sym["wa_frame"]) != f0,
                        "the match's first frames", guest=20)

        def said():
            n = p.word(sym["wa_hmsl"])         # (0: no message yet)
            return p.read(sym["wa_hmsg"], n).decode('latin-1') \
                if 0 < n < 64 else ''

        def row24():
            cols = p.word(sym["wa_w"])
            b = m.read(0xB8000 + 24 * cols * 2, cols * 2)
            return b[0::2].decode('cp437')

        def press(key, want, also=None):
            m.key(key)
            m.advance(cycles=TICK * 6)
            msg, glass = said(), row24()
            check(msg == want and want in glass and (also is None or also()),
                  "%s: %r on the HUD (%r in the game, row 24 %r)"
                  % (key, want, msg, glass.strip()))

        press("F2", "Detail Low", lambda: p.byte(sym["wa_level"]) == 1)
        press("F5", "Detail Very High", lambda: p.byte(sym["wa_level"]) == 4
              and p.byte(sym["wa_vsync"]) == 1)
        press("F3", "Detail Medium", lambda: p.byte(sym["wa_level"]) == 2
              and p.byte(sym["wa_vsync"]) == 0)
        press("KeyV", "Vsync on", lambda: p.byte(sym["wa_vsync"]) == 1)
        press("KeyV", "Vsync off", lambda: p.byte(sym["wa_vsync"]) == 0)
        press("Equal", "Columns 80", lambda: p.word(sym["wa_w"]) == 80)
        press("Minus", "Columns 40", lambda: p.word(sym["wa_w"]) == 40)
    if fails:
        sys.exit("wahudset: %d failure(s)" % len(fails))
    print("wahudset: ok - every setting a key changes says what it is now")


if __name__ == "__main__":
    main()
