#!/usr/bin/env python3
"""Does holding the mouse button repeat the gun? (SPEC.md 67.11.3)

    make && python3 tests/cycfire.py [--machine os8088_5150_cga_gla]

67.11.1 made the SPACE key a level so that holding it repeats at `cy_fire`'s
cooldown. The mouse button was still a press EDGE - one `W_ONCLICK`, one shot -
and 67.11.3 makes it a level as well, armed by that edge so a button held down
on somebody else's window cannot fire this gun.

THE MEASUREMENT IS THE SHOT TABLE, with `cy_shots_update` stubbed to a `ret`.
Frozen, a shot never travels and never expires, so `cy_s_act` stops being a
snapshot of what is in flight and becomes a COUNT OF WHAT WAS FIRED, up to
CY_MAXSHOT. A click then reads 1 and a hold reads the table full - which is the
whole claim, measured the same way in both gestures and in the same run, so no
part of it rests on comparing two builds of anything.

Five gestures are driven, and each is a different sentence:

  click       one press and release      -> exactly 1 shot, and no drift after
  hold        press, wait, release       -> the table FULL, and [cy_mheld] set
  release     ...and then wait again     -> no further shot, [cy_mheld] clear
  fullscreen  the same hold in a BRACKET  -> the table full again, off the
                                            bracket's own edge detect
  elsewhere   press on the DESKTOP       -> no shot at all: the arming edge is
                                            W_ONCLICK, and it never ran

The last one is the reason the level is armed rather than simply read, and it
is the one a bare `OSAPI_MOUSE` in `cy_input` would fail.

The wave is stopped exactly as tests/cycweb.py stops it, and for its reason: an
enemy that reaches the rim kills the player mid-gesture, and a death sweep
clears the shot table under the measurement.

Measured on a cycle-accurate 5150/CGA, four-second hold: before 67.11.3, click
1, hold 1 and fullscreen 1; after, click 1, hold 6 and fullscreen 6 - the table
full - with the release and the desktop press reading 0 in both. Point --apps and --src at an older build
to take the "before" column again; the cy_mheld corroboration reports n/a there
rather than failing, because that byte is what the change added.
"""
import sys, os, time, argparse
sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")
import os88marty, os88mouse, os88sym, os88geom, dispcp
from cycweb import Pkg, pkg_syms, u16, quiet, CYS_TITLE, CYS_PLAY

CY_MAXSHOT = 6
CY_FIRECD = 3


def shots(m, p):
    """How many of the frozen shot slots are live."""
    return sum(m.read(p.addr("cy_s_act"), CY_MAXSHOT))


def mheld(p):
    """[cy_mheld], or None on a build that predates it.

    --src is allowed to point at an OLDER cyclone.asm - that is what makes a
    "before" run possible at all - and the byte this feature added is simply
    not in one. The shot counts are the claim; this is corroboration, and it
    reports n/a rather than dying on a KeyError twelve steps into a boot.
    """
    return p.rb("cy_mheld") if "cy_mheld" in p.s else None


def yn(v):
    return "n/a" if v is None else str(v)


def clear(m, p):
    m.write(p.addr("cy_s_act"), bytes(CY_MAXSHOT))
    p.wb("cy_firecd", 0)
    p.wb("cy_firereq", 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--src", default="apps/cyclone/cyclone.asm")
    ap.add_argument("--hold", type=float, default=4.0,
                    help="host seconds to keep the button down")
    a = ap.parse_args()
    S = os88sym.linear

    with os88marty.launch("build/os8088-360.img", apps=a.apps,
                          machine=a.machine, boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wx, wy, _, _ = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "GAMES")
        wx, wy, _, _ = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])
        entry = dispcp.row_of(m, S, "CYCLONE.O88")
        row = dispcp.scroll_to(m, mo, S, os88marty.settle, wx, wy, entry)
        x, y = dispcp.row_xy(wx, wy, row)
        mo.dblclick(x, y)
        time.sleep(6)

        win = seg = None
        for w in os88geom.windows(m, S):
            if w.title.startswith("Cyclone"):
                win = w
                seg = u16(m.read(os88geom.winptr(m, w.i, S)
                                 + os88geom.W_SEG, 2))
        if not seg:
            raise RuntimeError("no Cyclone window - it did not launch")
        syms, image = pkg_syms(a.src)
        p = Pkg(m, seg, syms)
        lo, n = syms["cy_entry"], 2048
        live = bytes(m.read(seg * 16 + lo, n))
        if live != image[lo:lo + n]:
            bad = next(i for i in range(n) if live[i] != image[lo + i])
            raise RuntimeError("the CYCLONE.O88 running here is not this "
                               "source (first difference at +%d) - run make"
                               % (lo + bad))
        cx0, cy0, cx1, cy1 = win.content
        px, py = (cx0 + cx1) // 2, (cy0 + cy1) // 2
        print("cyclone at segment %04X, content %s, aiming at (%d,%d)"
              % (seg, win.content, px, py))

        for _ in range(10):
            if p.rb("cy_state") != CYS_TITLE:
                break
            m.key("Enter")
            time.sleep(2)
        os88marty.until(m, lambda _m: p.rb("cy_state") == CYS_PLAY,
                        "the warp to finish", poll=0.5, limit=90)
        quiet(m, p)
        # FREEZE THE SHOTS: cy_s_act stops being what is in flight and becomes
        # what was fired. Everything below reads that table.
        m.write(p.addr("cy_shots_update"), bytes([0xC3]))

        bad = []

        # --- one click ------------------------------------------------------
        clear(m, p)
        mo.click(px, py, settle=0.5)
        time.sleep(1.0)
        one = shots(m, p)
        held = mheld(p)
        print("click:     %d shot(s), cy_mheld = %s  (want 1, 0)"
              % (one, yn(held)))
        if one != 1:
            bad.append("a click fired %d shots, not 1" % one)
        if held:
            bad.append("a click left cy_mheld set - the release was missed")

        # --- press, hold, read, release -------------------------------------
        clear(m, p)
        mo.to(px, py)
        mo._edge(True)
        held_flag = mheld(p)
        time.sleep(a.hold)
        many = shots(m, p)
        print("hold %.1fs:  %d shot(s), cy_mheld = %s  (want %d, 1)"
              % (a.hold, many, yn(held_flag), CY_MAXSHOT))
        if many < CY_MAXSHOT:
            bad.append("a %.1fs hold fired %d shots and the table holds %d - "
                       "the button is not repeating" % (a.hold, many,
                                                        CY_MAXSHOT))
        if held_flag is not None and not held_flag:
            bad.append("the press did not arm cy_mheld")

        # --- ...and the release stops it ------------------------------------
        mo._edge(False)
        time.sleep(0.5)
        clear(m, p)
        time.sleep(1.5)
        after = shots(m, p)
        held = mheld(p)
        print("released:  %d shot(s), cy_mheld = %s  (want 0, 0)"
              % (after, yn(held)))
        if after:
            bad.append("the gun fired %d shots after the release" % after)
        if held:
            bad.append("cy_mheld survived the release")

        # --- and the same hold inside the FULLSCREEN bracket -----------------
        # A different arming path (SPEC.md 67.11.3): no events are dispatched
        # in a bracket, so cy_fsx_main edge-detects the button itself and only
        # sets [cy_mheld] - the firing is still cy_input's, which cy_update
        # reaches from inside the bracket exactly as the worker does outside
        # it. The seed on the way in is why the menu click that opened the
        # bracket does not count as that edge.
        m.key("KeyF")
        os88marty.until(m, lambda _m: p.rb("cy_fsx") == 1,
                        "the bracket to open", poll=0.5, limit=60)
        clear(m, p)
        w = os88geom.word(m, "vid_w", S)
        h = os88geom.word(m, "vid_h", S)
        mo.to(w // 2, h // 2)
        mo._edge(True)
        time.sleep(a.hold)
        fsx = shots(m, p)
        held = mheld(p)
        mo._edge(False)
        print("fullscreen %.1fs: %d shot(s), cy_mheld = %s  (want %d, 1)"
              % (a.hold, fsx, yn(held), CY_MAXSHOT))
        if fsx < CY_MAXSHOT:
            bad.append("a %.1fs hold in the bracket fired %d shots and the "
                       "table holds %d" % (a.hold, fsx, CY_MAXSHOT))
        if held is not None and not held:
            bad.append("the bracket's own edge did not arm cy_mheld")
        m.key("Escape")
        os88marty.until(m, lambda _m: p.rb("cy_fsx") == 0,
                        "the bracket to close", poll=0.5, limit=60)
        time.sleep(2)

        # --- a press that is NOT ours ---------------------------------------
        # The desktop, well clear of every window: no W_ONCLICK runs, so
        # nothing arms, and a level read without one would fire anyway.
        dx, dy = 4, os88geom.word(m, "vid_h", S) - 4
        for w in os88geom.windows(m, S):
            if w.covers(dx, dy):
                raise RuntimeError("(%d,%d) is not bare desktop" % (dx, dy))
        clear(m, p)
        mo.to(dx, dy)
        mo._edge(True)
        time.sleep(2.0)
        stray = shots(m, p)
        mo._edge(False)
        print("elsewhere: %d shot(s)  (want 0)" % stray)
        if stray:
            bad.append("a press on the desktop fired %d shots - the level is "
                       "not armed by W_ONCLICK" % stray)

        for b in bad:
            print("FAIL: %s" % b)
        print("cycfire: %s" % ("FAIL" if bad else "ok"))
        return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
