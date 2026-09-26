#!/usr/bin/env python3
"""MISSILE'S SEGMENT PATH STILL DRAWS (SPEC.md 48.16.1)

    make && python3 tests/mcseg.py [--machine os8088_5150_herc_gla]

`mc_line` is the arm a trail takes when `mc_tr_lay` will not lay a walk for it -
the Mode X surface, or an endpoint off the content. Since SPEC.md 48.16.1 it
lays the line into the point list and commits it with one `OSAPI_GFX_POINTS`
instead of calling `OSAPI_GFX_LINE`, and the erase's SPEC.md 5.6.5 dilation is
three walks either side of the minor axis, which is what the kernel did.

**THIS ROW EXISTS BECAUSE THE PATH DOES NOT RUN.** Instrumented over 45 guest
seconds of live play, `mc_tr_lay` was called 39 times and refused **0**, so
`mc_line` was reached **0** times - and a path nobody executes is a path that
rots silently. So the row FORCES it: `mc_tr_lay` is patched to `stc`/`ret` in
the running guest, which sends every trail down the segment arm, and then the
game is played.

What it asserts is that `mc_line` is reached and that its own `gfxe_wline` lays
pixels. It deliberately does NOT compare pixel for pixel against the walk arm:
SPEC.md 5.6.7 says the walk runs in the caller's direction where `gfx_line`
normalises downward, so the two were never identical and this row would be
asserting a difference that predates it.

**IT COUNTED THE WRONG THING FIRST, and that is worth writing down.** The first
version asserted playfield ink and `gfx_points` calls - and stayed GREEN through
two deliberate breakages, because Missile's BATCH (`mc_dsc_run`) drives
`gfx_points` every frame and the playfield's ink is mostly explosions and
terrain. A whole-machine counter cannot see one arm of one routine. So the
assertion is on `gfxe_wline`, which in this package has exactly one caller.

The other breakage is a finding rather than a gap: stubbing the final
`gfxe_pput` leaves the row green **and should**, because a full point list
commits itself (SPEC.md 5.12.5.1) - so a missing final commit delays pixels by
at most one list and never loses them.

Broken on purpose to check it goes red (docs/WRITING-TESTS.md 1): three `nop`s
over `.walk`'s `call gfxe_wline` - same size, so the map check cannot mask it -
takes the count to 0.
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
WINDOW = 20.0                   # guest seconds of play with the arm forced


def ink(m, box):
    w, h, rows = m.vram()
    px = [b for r in rows for b in r]
    x0, y0, x1, y1 = box
    return sum(1 for y in range(y0, y1) for x in range(x0, x1)
               if px[y * w + x])


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    S = os88sym.linear

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        os88marty.settle(m)
        os88marty.no_saver(m)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        disk = dispcp.win_list(m, S)[-1]
        wx, wy = dispcp.win_rect(m, S, disk)[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "GAMES")
        wx, wy = dispcp.win_rect(m, S, disk)[:2]
        rows = [r[0] for r in dispcp.listing(m, S)]
        row = dispcp.scroll_to(m, mo, S, os88marty.settle, wx, wy,
                               rows.index("MISSILE.O88"))
        x, y = dispcp.row_xy(wx, wy, row)
        mo.dblclick(x, y)
        m.advance(frames=250)
        m.run()
        got = dispapps.pkg_seg(m, 0)
        if got is None:
            sys.exit("mcseg: MISSILE.O88 did not open")
        slot, seg = got[0], got[1]
        pm = dispapps._map("missile")
        pr = dispcp.win_rect(m, S, slot)
        box = (pr[0] + 2, pr[1] + 20, pr[0] + 300, pr[1] + 150)

        # FORCE the segment arm: mc_tr_lay answers "no walk for this one".
        m.pause()
        m.write((seg << 4) + pm["mc_tr_lay"], bytes([0xF9, 0xC3]))   # stc; ret
        m.run()
        print("  mc_tr_lay patched to stc/ret - every trail takes mc_line")

        # `gfxe_wline` has exactly ONE caller in this package - mc_line's
        # .walk - so counting it is counting the arm, where counting
        # gfx_points or playfield ink counts the whole game.
        base = seg << 4
        def on_hit(mm, rec):
            r = rec["regs"]
            sw = lambda v: v - 65536 if v > 32767 else v
            return max(abs(sw(r["cx"]) - sw(r["ax"])),
                       abs(sw(r["dx"]) - sw(r["bx"]))) + 1

        with os88marty.bp_trace(m, base + pm["gfxe_wline"],
                                base + pm["mc_line"],
                                regs=True, on_hit=on_hit) as tr:
            t0 = m.status()["cycles"]
            k = 0
            while m.status()["cycles"] < t0 + WINDOW * HZ:
                k += 1
                mo.click(pr[0] + 30 + (k * 41) % 240,
                         pr[1] + 40 + (k * 29) % 110)
                os88marty.pace(m, 0.03)
            m.pause()
            lit = ink(m, box)
            m.run()

        W, L = base + pm["gfxe_wline"], base + pm["mc_line"]
        lines = [h["hit"] for h in tr.hits if h["addr"] == W]
        entries = sum(1 for h in tr.hits if h["addr"] == L)
        pts = sum(lines)
        print("  mc_line entered %d times; gfxe_wline %d times, %d pixels; "
              "%d lit in the playfield" % (entries, len(lines), pts, lit))

        bad = []
        if not entries:
            bad.append("mc_line was never entered even with mc_tr_lay forced "
                       "to refuse - the force did not take, so this row "
                       "measured nothing")
        if not lines:
            bad.append("the forced segment arm never reached gfxe_wline - "
                       "mc_line is not laying its line at all")
        elif pts < 100:
            bad.append("gfxe_wline laid only %d pixels over %d lines: the "
                       "walk is not running to length" % (pts, len(lines)))
        print()
        if bad:
            for b in bad:
                print("FAIL: " + b)
            return 1
        print("ok: mc_line is reached and lays its line through gfxe_wline")
        return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
