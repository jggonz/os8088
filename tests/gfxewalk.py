#!/usr/bin/env python3
"""IS THE WALK REALLY IN THE APP, AND IS ITS POINT LIST BIG ENOUGH? (SPEC.md 5.12.5)

    make && python3 tests/gfxewalk.py [--machine os8088_5150_herc_gla]

Since wave 5 of docs/plans/completed/GFX-EMBEDDABLE-PLAN.md, Cyclone's warp and Missile's
trails step SPEC.md 5.6.7's resumable walk in their OWN images - apps/os88gfx.inc's
`GFXE_WALK` - and commit the pixels through `OSAPI_GFX_POINTS`. Two things
about that arrangement can be wrong in a way no picture shows, and this row is
both of them:

  1. **The route is live.** `gfx_points` must actually be entered, with a
     non-zero count, from each program. If a conversion were half-done - a
     block stepped and never committed - the figure would simply be thinner,
     which is exactly the kind of defect docs/PERFORMANCE.md says an emulator
     cannot show and a person on a 4.77MHz machine reads as slowness.
  2. **The point list is big enough.** A full list COMMITS ITSELF and the
     caller never finds out (SPEC.md 5.12.5), so a buffer sized too small is
     not a defect - it is an extra arrival a frame, silently, for ever.
     `[gfxe_pflush]` counts those, and it must be 0 on a program whose worst
     frame has been thought about.

**IT IS THE SECOND ONE THAT NEEDS A TEST.** The first is visible in `cycweb`
and `dispmcfs` if it breaks badly enough; nothing anywhere else can see the
second, because the picture is right either way.

Broken on purpose to check they go red (docs/WRITING-TESTS.md 1): `CY_PTMAX`
at 4 takes Cyclone's flush count to **2,264** and its `gfx_points` calls from
133 to 1,317 - **for the same 5,207 points**, which is the whole argument for
the check, the picture being identical either way; and stubbing `gfxe_pput` to
a `ret` takes the entry count to 0.

**ON THE GLaBIOS TWIN** - the IBM ROM is not in this repository.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispapps                                             # noqa: E402
import dispcp                                               # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HZ = 4772727.0
WINDOW = 14.0                   # guest seconds of play to watch
PKGS = [("CYCLONE.O88", "cyclone"), ("MISSILE.O88", "missile")]


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    S = os88sym.linear
    bad = []

    for fname, app in PKGS:
        with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
            os88marty.settle(m)
            mo = os88mouse.Mouse(marty=m)
            dispcp.open_drive(m, mo, S, os88marty.settle, "B")
            disk = dispcp.win_list(m, S)[-1]
            wx, wy = dispcp.win_rect(m, S, disk)[:2]
            dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "GAMES")
            wx, wy = dispcp.win_rect(m, S, disk)[:2]
            rows = [r[0] for r in dispcp.listing(m, S)]
            row = dispcp.scroll_to(m, mo, S, os88marty.settle, wx, wy,
                                   rows.index(fname))
            x, y = dispcp.row_xy(wx, wy, row)
            mo.dblclick(x, y)
            m.advance(frames=250)
            m.run()
            got = dispapps.pkg_seg(m, 0)
            if got is None:
                bad.append("%s did not open" % fname)
                continue
            seg = got[1]
            pm = dispapps._map(app)

            def on_hit(mm, rec):
                return rec["regs"]["cx"]

            with os88marty.bp_trace(m, "gfx_points", regs=True,
                                    on_hit=on_hit) as tr:
                t0 = m.status()["cycles"]
                while m.status()["cycles"] < t0 + WINDOW * HZ:
                    time.sleep(0.02)

            calls = [h["hit"] for h in tr.hits]
            live = [c for c in calls if c]
            pts = sum(live)
            flush = int.from_bytes(
                m.readseg(seg, pm["gfxe_pflush"], 2), "little")
            print("  %-13s %4d gfx_points calls (%d with points), %5d points,"
                  " gfxe_pflush = %d"
                  % (app, len(calls), len(live), pts, flush))

            if not live:
                bad.append("%s never reached gfx_points with a point in it - "
                           "the walk is stepping and nothing is committing it"
                           % app)
            if flush:
                bad.append("%s forced %d self-commit(s): its point list is too "
                           "small for its worst frame, so it pays an extra "
                           "arrival - raise its GFXE_PT_MAX" % (app, flush))

    print()
    if bad:
        for b in bad:
            print("FAIL: " + b)
        return 1
    print("ok: both walks are app-side and both point lists hold a worst frame")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
