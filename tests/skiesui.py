#!/usr/bin/env python3
"""CLEAR SKIES' title page (SPEC.md 88.10) and the drop-down it is the first
user of (SPEC.md 13.14), driven on MartyPC's VGA machine.

    python3 tests/skiesui.py [--machine os8088_xt_vga]

The VGA machine, because it is the one with a CHOICE of fullscreen mode and
so the one with a Mode menu; everything else here is the same on the three
adapters. Every position comes off the package's own records - the
drop-downs' rects are written by the painter in screen coordinates, so a
click at a rect's centre is a click on the control wherever the window
landed - and every answer is read out of the package's bss, not the glass.

  1. the plane list DROPS on a press in its box, and a press anywhere else
     takes it down without picking (OS88UI_DR_OPEN);
  2. the location list drops, and a press on its second item PICKS it:
     OS88UI_DR_SEL = 1, cs_airport = the second airport's record, and
     cs_inited = 0 so the next flight starts on that runway;
  3. Esc takes an open list down;
  4. Flight -> Instructions turns the page (cs_page = 1) and a click turns it
     back;
  5. the Mode row - on the Settings page since SPEC.md 88.13, and gone from
     the bar - offers Mode X and CGA, its pick lands (cs_modepref, cs_want)
     and F then flies in CGA320 (cs_back = 2), the frame counter climbing;
  6. after all of that the menu bar still drops.

Check 2a is the drop-down's SAVE-UNDER (SPEC.md 13.14.1): opening the list
banks the pixels it covers and closing it writes them back, so the band the
list lay over must come back pixel for pixel, with the claim released. The
pointer is parked at one place for both captures, which is what lets the
comparison be exact rather than approximate.

Check 2 also reads the kernel's `ui_armw` between the pick's PRESS and its
RELEASE, and that word must name the launcher's own window (cs_win). It is
the check that would have caught the first build: its click handler came
back with SI pointing at the airport record it had picked, ui_task armed
the release to THAT as the window (SPEC.md 13.7, `mov [ui_armw], si` after
W_ONCLICK returns), and the release far-called through whatever kernel
bytes sat at that offset - an unmapped segment, that time. The launcher
looked fine; the bar died three seconds later. What the release does with
a wrong arm depends on the bytes it finds, so the row reads the ARM and
not the wreckage.

--clobber-si is the red run (docs/WRITING-TESTS.md 1): it patches the two
`pop si` that put SI back - the click handler's and the pick's - into
`pop bp`, which is the first build's bug exactly, and check 2 must then
read the airport record where the window should be.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88ui                                               # noqa: E402
import os88sym                                              # noqa: E402
import dispapps                                             # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DR_SEL, DR_OPEN = 12, 16                # os88ui.inc's record (OS88UI_DR_*)
DR_WIN, DR_SEG = 14, 18                 # ...the window a press clips to, and
                                        # the bank the close writes back
DR_TOP = 22                             # ...where the OPEN list starts (13.14.2)
DR_SIZE = 26                            # ...whose two banking words (13.14.1)
                                        # were APPENDED, so a record declared
                                        # to the old length overlaps the next.
                                        # 24 until 13.14.5 gave the control an
                                        # OS88UI_DR_DIS, and this row caught
                                        # that change - which is what it is
                                        # for. t_mirror holds this copy to
                                        # apps/os88ui.inc's now, so the THIRD
                                        # spelling cannot drift either
CSB_MODEX, CSB_CGA = 1, 2
CS_ARTX, CS_ARTY = 152, 40              # the aircraft band, in CONTENT
CS_ARTW, CS_ARTH = 152, 96              # coordinates (skies.asm, csart.inc)
CSP_ART = 42                            # the band a plane record names (88.10.1)
bad = []


def _shot(m):
    w, h, px = m.fbuf()
    return w, h, bytes(1 if px[i] or px[i+1] or px[i+2] else 0
                       for i in range(0, len(px), 3))


def _band(s, r):
    w, h, px = s
    x1, y1, x2, y2 = r
    return bytes(px[y*w + x] for y in range(max(y1, 0), min(y2, h - 1) + 1)
                 for x in range(max(x1, 0), min(x2, w - 1) + 1))


def check(cond, what):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", what))
    if not cond:
        bad.append(what)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--clobber-si", action="store_true",
                    help="break the click handler's SI on purpose: the row must go red")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    mp = dispapps._map("skies")
    with os88ui.boot(a.image, apps=a.apps, machine=a.machine) as ui:
        m = ui.m
        ui.path("B:/GAMES/SKIES.O88")
        slot, seg = dispapps.pkg_seg(m, 0)
        base = int.from_bytes(m.readseg(seg, 8, 2), "little")

        def bss(name, n=2):
            return int.from_bytes(m.readseg(seg, base + dispapps.bss_off("skies", name), n), "little")

        def rec(name, off, n=2):
            return int.from_bytes(m.readseg(seg, mp[name] + off, n), "little")

        def rect(name):
            return [rec(name, 2 * i) for i in range(4)]

        def click(x, y):
            ui.mo.click(x, y)
            m.advance(frames=20)
            m.run()

        m.advance(frames=30)
        m.run()
        if a.clobber_si:
            lin = seg << 4
            for name, nxt, tail in (("cs_onclick", "cs_onup", b"\x5f\x5e\x5a\x59\x5b\x58\xc3"),
                                    ("cs_drtake", "cs_drcloseall", b"\x5e\x58\x9d")):
                code = m.read(lin + mp[name], mp[nxt] - mp[name])
                at = code.find(tail)
                if at < 0:
                    sys.exit("skiesui: the epilogue of %s is not the one this patch knows" % name)
                si_at = at + tail.index(b"\x5e")
                m.pause()
                m.write(lin + mp[name] + si_at, b"\x5d")      # pop si -> pop bp
                m.run()
            print("  (SI clobbered on purpose in cs_onclick and cs_drtake: this run must fail)")
        check(mp["cs_drport"] - mp["cs_drplane"] == DR_SIZE,
              "the two drop-down records are OS88UI_DR_SIZE apart (%d)"
              % (mp["cs_drport"] - mp["cs_drplane"]))
        pl, po = rect("cs_drplane"), rect("cs_drport")
        check(pl[2] > pl[0] and po[1] > pl[3], "the painter wrote the drop-downs' rects (%s, %s)" % (pl, po))

        # WHERE THE OPEN LIST IS is os88ui_drfit's answer and no longer the row
        # under the box (SPEC.md 13.14.2): the Location list is nine items on a
        # control near the foot of a 137-row page, so it slides UP into the
        # window. Every cell below is measured off OS88UI_DR_TOP for that
        # reason - a test that kept the old arithmetic would click on the
        # title band and report that picking was broken.
        def cell(rc, n):
            """the middle row of item n of rc's open list, screen"""
            return rec(rc, DR_TOP) + 1 + 12 * n + 6

        # --- 1. the plane list drops and closes -------------------------------
        click((pl[0] + pl[2]) // 2, (pl[1] + pl[3]) // 2)
        check(rec("cs_drplane", DR_OPEN, 1) == 1, "a press in the plane box drops its list")
        click(po[0] + 20, pl[1] - 30)                       # the title band
        check(rec("cs_drplane", DR_OPEN, 1) == 0 and rec("cs_drplane", DR_SEL) == 0,
              "a press elsewhere takes it down and picks nothing")

        # --- 1a. ...and a PICK changes the PICTURE (SPEC.md 88.10.1) ----------
        # The band is the plane record's own (CSP_ART), so the page draws
        # whichever aeroplane is in use. The pointer is parked at the same
        # place, clear of the band, for all three captures, so what differs
        # between them is the aeroplane and nothing else. A painter that
        # blitted one fixed band would leave the picture identical here.
        artpark = (bss("cs_winox") + 20, bss("cs_winoy") + 30)

        def artshot():
            ui.mo.to(*artpark)
            m.advance(frames=20)
            m.run()
            m.pause()
            fw, fh, f = m.fbuf(0)
            m.run()
            x0, y0 = bss("cs_winox") + CS_ARTX, bss("cs_winoy") + CS_ARTY
            return b"".join(f[((y0 + y) * fw + x0) * 3:((y0 + y) * fw + x0 + CS_ARTW) * 3]
                            for y in range(CS_ARTH))

        def artdiff(p, q):
            return sum(1 for i in range(0, len(p), 3) if p[i:i + 3] != q[i:i + 3])

        first = int.from_bytes(m.readseg(seg, mp["cs_planes"], 2), "little")
        second = int.from_bytes(m.readseg(seg, mp["cs_planes"] + 2, 2), "little")
        check(bss("cs_plane") == first, "the launcher opens in the first aeroplane")
        trainer = artshot()
        click((pl[0] + pl[2]) // 2, (pl[1] + pl[3]) // 2)   # drop it...
        click(pl[0] + 20, cell("cs_drplane", 1))            # ...and take item 2
        check(bss("cs_plane") == second and bss("cs_inited", 1) == 0,
              "the pick is the aeroplane in use, and a fresh flight is owed "
              "(cs_plane %04x, cs_inited %d)" % (bss("cs_plane"), bss("cs_inited", 1)))
        aerobat = artshot()
        d = artdiff(trainer, aerobat)
        check(d > 500, "the second aeroplane is a DIFFERENT PICTURE "
                       "(%d of %d pixels differ)" % (d, len(trainer) // 3))
        arts = [int.from_bytes(m.readseg(seg, r + CSP_ART, 2), "little")
                for r in (first, second)]
        check(arts[0] != arts[1] and 0 not in arts,
              "and the two records name two bands (CSP_ART %04x, %04x)" % tuple(arts))
        click((pl[0] + pl[2]) // 2, (pl[1] + pl[3]) // 2)   # back to item 1
        click(pl[0] + 20, cell("cs_drplane", 0))
        check(bss("cs_plane") == first, "picking the first aeroplane back takes it")
        d = artdiff(trainer, artshot())
        check(d == 0, "and puts every pixel of its picture back (%d differ)" % d)

        # --- 2. the location list picks ---------------------------------------
        click((po[0] + po[2]) // 2, (po[1] + po[3]) // 2)
        check(rec("cs_drport", DR_OPEN, 1) == 1, "a press in the location box drops its list")
        # SPEC.md 13.14.2: the whole list is INSIDE the window's content, which
        # a nine-item list under a box at content y 88 of 137 rows is not.
        n = int.from_bytes(m.readseg(seg, mp["cs_drport"] + 10, 2), "little")
        top = rec("cs_drport", DR_TOP)
        cy, ch = bss("cs_winoy"), bss("cs_ch")
        check(top >= cy and top + 12 * n + 1 <= cy + ch - 1,
              "all %d items fit inside the content (rows %d..%d of %d..%d)"
              % (n, top, top + 12 * n + 1, cy, cy + ch - 1))
        check(top < po[3] + 1,
              "...and a list that long slid UP to do it (top %d, the box ends %d)"
              % (top, po[3]))
        was_wld = bss("cs_wldnow", 1)
        ui.mo.to(po[0] + 20, cell("cs_drport", 1))          # the second cell:
        ui.mo._edge(True)                                   # PRESS, and look
        # WAIT FOR THE HANDLER, don't count frames. `mov [ui_armw], si` is
        # AFTER `call ui_bill` in ui.inc, so the arm does not exist until
        # W_ONCLICK has RETURNED - and since SPEC.md 88.10.5 that handler
        # READS A WORLD off the floppy, several int 13h calls at ~400 ms each
        # on the target machine. Ten frames covered the whole gesture and now
        # covers the start of it.
        #
        # AND THE LIST GOING DOWN IS NOT THE END OF IT, which is the trap:
        # probed at two-frame resolution the list is already shut and
        # cs_apnow already set on the FIRST reading, while cs_wldnow does not
        # change for another ten frames - the drop-down takes the pick and
        # THEN cs_drtake reads the country in. So the world is what to wait
        # on, and it is worth asserting on its own account.
        for _ in range(40):
            m.advance(frames=10)
            m.run()
            if bss("cs_wldnow", 1) != was_wld:
                break
        check(bss("cs_wldnow", 1) != was_wld,
              "the pick read its own world into the overlay (cs_wldnow %d, "
              "was %d)" % (bss("cs_wldnow", 1), was_wld))
        armw = int.from_bytes(m.read(os88sym.linear("ui_armw"), 2), "little")
        check(armw == bss("cs_win"),
              "the pick's press leaves the release owed to the LAUNCHER's window (ui_armw %04x, cs_win %04x)"
              % (armw, bss("cs_win")))
        ui.mo._edge(False)
        m.advance(frames=20)
        m.run()
        # cs_ports READ NOW, after the pick: a record lives in the world
        # overlay (88.10.5), so row 1's pointer is only true once row 1's
        # world is the one loaded - which is what the press just did.
        second = int.from_bytes(m.readseg(seg, mp["cs_ports"] + 2, 2), "little")
        check(rec("cs_drport", DR_SEL) == 1,
              "a press on the second item picks it")
        check(bss("cs_airport") == second and bss("cs_inited", 1) == 0,
              "the pick is the airport in use, and the next flight starts there (cs_airport %04x, cs_inited %d)"
              % (bss("cs_airport"), bss("cs_inited", 1)))

        # --- 2a. ...and the pixels it covered come BACK (SPEC.md 13.14.1) -----
        # The pointer is parked at the same place for both captures, so the
        # comparison can be exact: what differs is the list, or nothing.
        park = (po[0] - 30, po[1] - 40)
        ui.mo.to(*park)
        m.advance(frames=20)
        m.run()
        m.pause()
        fw, fh, was = m.fbuf(0)
        m.run()
        click((po[0] + po[2]) // 2, (po[1] + po[3]) // 2)
        kb = rec("cs_drport", 20)
        check(rec("cs_drport", 18) != 0 and kb > 0,
              "opening the list banked the pixels under it (%d KB)" % kb)
        click(po[0] + 20, cell("cs_drport", 0))         # the first item: the
        ui.mo.to(*park)                                 # pick does not change
        m.advance(frames=20)                            # what is drawn
        m.run()
        m.pause()
        fw, fh, now = m.fbuf(0)
        m.run()
        top = rec("cs_drport", DR_TOP)
        y0, y1 = top, min(top + 12 * 2 + 2, fh)
        band = lambda f: b"".join(f[(y * fw + po[0]) * 3:(y * fw + po[2] + 1) * 3]
                                  for y in range(y0, y1))
        a, b = band(was), band(now)
        d = sum(1 for i in range(0, len(a), 3) if a[i:i + 3] != b[i:i + 3])
        check(d == 0, "closing it put every pixel back (%d of %d differ)"
              % (d, len(a) // 3))
        check(rec("cs_drport", 18) == 0, "and the claim went with it")
        # ...and the BOX itself, which the write-back cannot reach, shows the
        # item that was picked rather than the one it had (SPEC.md 13.14.1)
        box = lambda f: b"".join(f[(y * fw + po[0]) * 3:(y * fw + po[2] + 1) * 3]
                                 for y in range(po[1] + 1, po[3]))
        check(box(was) != box(now),
              "the closed box was redrawn with the pick's caption")

        # --- 3. Esc closes ----------------------------------------------------
        click((po[0] + po[2]) // 2, (po[1] + po[3]) // 2)
        m.key("Escape")
        m.advance(frames=20)
        m.run()
        check(rec("cs_drport", DR_OPEN, 1) == 0, "Esc takes an open list down")

        # --- 4. the instructions page -------------------------------------------
        ui.menu_pick("Flight", "Instructions")
        m.advance(frames=20)
        m.run()
        check(bss("cs_page", 1) == 1, "Flight -> Instructions turns the page")
        click(po[0] + 20, po[1])
        check(bss("cs_page", 1) == 0, "a click turns it back")

        # --- 5. the Mode row, which lives on the Settings page now (88.13) -------
        cells = {c[0]: c for c in ui.menus()}
        check("Mode" not in cells, "the Mode menu is gone from the bar (%s)"
              % sorted(cells))
        if bss("cs_want", 1) == CSB_MODEX:
            ui.menu_pick("Flight", "Settings")
            m.advance(frames=30)
            m.run()
            md = [rec("cs_drmode", 2 * i) for i in range(4)]
            check(md[2] > md[0], "the Settings page put the Mode row up %s" % md)
            click((md[0] + md[2]) // 2, (md[1] + md[3]) // 2)
            click(md[0] + 20, cell("cs_drmode", 1))     # the second item: CGA
            check(bss("cs_modepref", 1) == 1 and bss("cs_want", 1) == CSB_CGA,
                  "picking CGA makes CGA320 the mode to fly in (want %d)"
                  % bss("cs_want", 1))
            m.type_text("f")                            # off the page...
            m.advance(frames=40)
            m.run()
            for _ in range(20):                         # ...which since
                if bss("cs_page", 1) == 0:              # 88.13.9 WRITES A
                    break                               # FILE: a floppy
                m.advance(frames=40)                    # create is several
                m.run()                                 # int 13h calls and
                                                        # forty frames does
                                                        # not cover it
            m.type_text("f")                            # ...and into the air
            m.advance(frames=120)
            m.run()
            f0 = bss("cs_frames")
            m.advance(frames=60)
            m.run()
            check(bss("cs_back", 1) == CSB_CGA and bss("cs_frames") > f0,
                  "F flies in CGA320 on the VGA (back %d, frames %d -> %d)"
                  % (bss("cs_back", 1), f0, bss("cs_frames")))
            m.type_text("f")
            m.advance(frames=60)
            m.run()

        # --- 6. the bar still drops --------------------------------------------
        ui.menu_pick("Flight", "Instructions")
        m.advance(frames=20)
        m.run()
        check(bss("cs_page", 1) == 1, "the menu bar still answers after every gesture above")

        # --- 7. THE TWO REFUSALS, both forced in the guest ---------------------
        # os88ui_drop's two failure paths are the ones no gesture reaches, and
        # both shipped answering the wrong thing. They are FORCED here rather
        # than arranged, because the conditions - a window that cannot be
        # clipped to, a rect that straddles two displays - want hardware or a
        # cover this machine has not got. Forcing them is the only way either
        # gets a test at all, and both were live defects until it had one.
        click(400, 300)                                 # a click turns the page
        m.advance(frames=30)                            # back (check 4's rule -
        m.run()                                         # the MENU only opens it)
        check(bss("cs_page", 1) == 0,
              "7: back on the title page, where the drop-downs are (page %d)"
              % bss("cs_page", 1))
        pl = rect("cs_drplane")
        bx, by = (pl[0] + pl[2]) // 2, (pl[1] + pl[3]) // 2
        drec = (seg << 4) + mp["cs_drplane"]

        # 7a. a press that could not arm the clip must leave the control SHUT.
        # It used to leave OS88UI_DR_OPEN at 1 with nothing drawn, and the next
        # press then hit-tested cells nobody could see and picked one.
        #
        # THE ARRANGING CHECK IS THE LEG. Without it this reads "the control is
        # shut", which a click that missed the box entirely also satisfies -
        # and that is exactly how it first ran: green against the defect it was
        # written for, because leg 6 had left the Instructions page up and
        # there was no box under the pointer at all.
        click(bx, by)
        check(rec("cs_drplane", DR_OPEN, 1) == 1,
              "7a: the same click DOES open it with a window (case arranged)")
        click(bx, by)                                   # ...and press the BOX
        m.advance(frames=20)                            # again to shut it: a
        m.run()                                         # click on the desktop
        check(rec("cs_drplane", DR_OPEN, 1) == 0,       # deactivates the window
              "7a: pressing the box again shuts it (case arranged)")
        win = m.read(drec + DR_WIN, 2)
        m.pause()
        m.write(drec + DR_WIN, b"\x00\x00")
        m.run()
        click(bx, by)
        shut_after_refusal = rec("cs_drplane", DR_OPEN, 1) == 0
        m.pause()
        m.write(drec + DR_WIN, win)
        m.write(drec + DR_OPEN, b"\x00")
        m.run()
        check(shut_after_refusal,
              "7a: ...and a press that cannot arm the clip leaves it SHUT")

        # 7b. a write-back that REFUSES must be reported as a repaint. It
        # answered "repaired", so the caller drew nothing and the list stayed
        # on the glass with the control reading shut - 2,710 pixels of it.
        def park():
            # THE POINTER IS PART OF THE PICTURE. Both captures are taken with
            # it in the same place or the arrow's own pixels answer the
            # question: parked on the box, it hangs four rows into the band
            # below and reads as five pixels of a list that is not there.
            ui.mo.to(pl[0] - 24, pl[1] - 24)
            m.advance(frames=20)
            m.run()

        check(rec("cs_drplane", DR_OPEN, 1) == 0,
              "7b: the control is shut before the reference capture")
        park()
        shut_px = _shot(m)
        click(bx, by)
        top = rec("cs_drplane", DR_TOP)
        opened = rec("cs_drplane", DR_OPEN, 1) == 1 and rec("cs_drplane", DR_SEG) != 0
        rest = m.sym("api_gfx_rest")
        keep = m.read(rest, 2)
        m.pause()
        m.write(rest, bytes([0xF9, 0xC3]))              # stc; ret - always refuse
        m.run()
        click(bx, by)                                   # press the box: closes
        m.advance(frames=40)
        m.run()
        park()
        after_px = _shot(m)
        m.pause()
        m.write(rest, keep)
        m.run()
        r = (pl[0], top, pl[2], top + 4 * 12)
        d = sum(1 for p, q in zip(_band(shut_px, r), _band(after_px, r)) if p != q)
        check(opened, "7b: the list dropped and banked (case, not assertion)")
        check(d == 0,
              "7b: a REFUSED write-back is reported as a repaint (%d pixels of "
              "the list left standing)" % d)

    if bad:
        for b in bad:
            print("FAIL: " + b)
        return 1
    print("  ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
