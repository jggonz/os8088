#!/usr/bin/env python3
"""How long does a click take to be ACTED ON while a worker draws? (SPEC.md 7.3)

    make wiredisk && python3 tests/uilat.py [--machine os8088_5150_cga_gla]

WIREFRAME DOES NOT SHIP (SPEC.md 78.9), so `make wiredisk` is what puts
WIRE.O88 on a floppy for this to open. `make` alone still builds the
package - it just no longer lands on the apps disk.

`tools/os88mouse.py` cannot answer this. Its injection path costs ~0.51 guest
seconds between the packet and the guest seeing the button - flat, whatever the
machine is doing - so a number taken by sending a click and watching for the
result carries a half-second floor and cannot tell 1 ms from 400 (SPEC.md
7.3.1). Nor can a counter sampled per second: the UI task only asks for the gfx
lock when it has something to DRAW, so contention measured while the machine
merely runs reports zero and means nothing.

So this brackets the kernel's own path with two memory breakpoints and reads
`cycles` at each. `evq_tail` moves when the mouse ISR queues the press - on an
idle machine nothing else pushes - and `menu_ent` is written by menu_track once
the pull-down is up. The difference is what the OS took and nothing else.

THE GATE is SPEC.md 7.3's regression: before the lock handover this click cost
1,382-14,722 ms with apps/wire drawing, and 37-70 ms after. UILAT_MAX is set
well above the latter and far below the former, so the row fails loudly if the
handover is lost and does not flake on the spread.

**ON THE GLaBIOS TWIN**, `os8088_5150_herc_gla`, because `os8088_5150_herc`
wants the IBM ROM this repo cannot ship. Checked with that ROM dropped in
beside GLaBIOS: PASS on both, idle desktop 2 ms on both, and the wire legs
overlap - 14/24/174 ms against 41/60/66 edge-at-a-time, 32/78/21 against
29/25/56 whole-figure. The within-machine spread swamps any difference between
them, which is why the budget is 500 ms and not a figure.
"""
import argparse
import os
import sys

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
UILAT_MAX = 0.500               # seconds; see the docstring
CHIP = (8, 8)                   # the chip menu's title cell


def click_latency(m, mo, S, tries=3):
    """[seconds] from the press being queued to menu_track having it up."""
    out = []
    for _ in range(tries):
        m.pause()
        m.write(S("menu_ent"), b"\x00\x00")
        m.breakpoints([{"type": "mem", "addr": S("evq_tail")}])
        m.run()
        mo._pk(l=True)                          # one packet; nothing resent
        if m.wait_stop(120) is None:
            out.append(None)
        else:
            t1 = m.status()["cycles"]
            m.breakpoints([{"type": "mem", "addr": S("menu_ent")}])
            m.run()
            out.append(None if m.wait_stop(120) is None
                       else (m.status()["cycles"] - t1) / HZ)
        m.breakpoints([])
        m.run()
        mo._pk(l=False)
        m.advance(frames=40)
        m.run()
    return out


def report(label, got):
    txt = ", ".join("%.0f ms" % (g * 1000) if g is not None else "TIMED OUT"
                    for g in got)
    worst = max((g for g in got if g is not None), default=None)
    bad = worst is None or worst > UILAT_MAX
    print("  %-24s %s%s" % (label, txt, "   <-- FAIL" if bad else ""))
    return bad


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/wire360.img")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    S = os88sym.linear
    bad = 0

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        os88marty.settle(m)
        mo = os88mouse.Mouse(marty=m)
        mo.to(*CHIP)
        m.advance(frames=40)
        m.run()
        bad += report("idle desktop", click_latency(m, mo, S))

        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        disk = dispcp.win_list(m, S)[-1]
        wx, wy = dispcp.win_rect(m, S, disk)[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "APPS")
        wx, wy = dispcp.win_rect(m, S, disk)[:2]
        rows = [r[0] for r in dispcp.listing(m, S)]
        row = dispcp.scroll_to(m, mo, S, os88marty.settle, wx, wy,
                               rows.index("WIRE.O88"))
        x, y = dispcp.row_xy(wx, wy, row)
        mo.dblclick(x, y)
        m.advance(frames=200)
        m.run()
        got = dispapps.pkg_seg(m, 0)
        if got is None:
            sys.exit("uilat: WIRE.O88 did not open")
        seg = got[1]
        base = int.from_bytes(m.readseg(seg, 8, 2), "little")
        for mode, name in ((1, "wire, edge at a time"), (0, "wire, whole figure")):
            m.pause()
            m.write((seg << 4) + base + 39, bytes([mode]))       # wr_mode
            m.run()
            m.advance(frames=60)
            m.run()
            mo.to(*CHIP)
            m.advance(frames=40)
            m.run()
            bad += report(name, click_latency(m, mo, S))

    print()
    print("uilat: %s (budget %.0f ms)"
          % ("FAIL (%d)" % bad if bad else "PASS", UILAT_MAX * 1000))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
