#!/usr/bin/env python3
"""Can a pickup be SWEPT UP? (SPEC.md 67.24)

    make && python3 tests/cycpu.py [--machine os8088_5150_herc_gla]

A pickup used to be taken only if the claw was on its EXACT lane on the ONE
frame it reached the lip, which on a window this size is very nearly
impossible - the arcade lets you sweep past one and collect it. 67.24 widens
that two ways, and this row is the truth table for both:

    claw on the lane            taken   (it always was)
    claw one lane either side   taken   (the adjacency)
    claw three lanes away       not taken
    ...and then swept onto it   taken   (the grace window)
    ...and swept on too late    not taken

CY_PUJUMP is the instrument: the JUMP pickup increments [cy_pw_jump] and
nothing else does, so one byte says whether a take happened and the row can
reset it between cases. The pickup is PLACED rather than waited for - dropping
one needs a kill and a one-in-eight roll - at a depth one step short of the
lip, so the very next frame is the one that decides.
"""
import sys, os, re, time, argparse

_R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_R, "tools"))
sys.path.insert(0, os.path.join(_R, "tests"))
import os88marty, os88mouse, os88sym, os88geom, dispcp          # noqa: E402
from cycweb import pkg_syms, Pkg, u16, CYS_TITLE, CYS_PLAY      # noqa: E402

def equ(name):
    """Read an `equ` straight out of the app.

    THE EXPECTATIONS ARE DERIVED FROM IT and not written down here: CY_PUNEAR
    and CY_PUGRACE are play-feel knobs the owner turns, so a row that hard-coded
    "one lane either side counts" would go from a gate to a lie the first time
    one of them moved. tests/unit/t_mirror.py's rule, applied to a constant this
    file would otherwise be the second copy of.
    """
    src = open(os.path.join(_R, "apps/cyclone/cyclone.asm")).read()
    m = re.search(r"^%s\s+equ\s+(\d+)" % name, src, re.M)
    if not m:
        raise RuntimeError("no `%s equ` in cyclone.asm" % name)
    return int(m.group(1))


CY_TOPD = 17
CYP_JUMP = 2
LIP = CY_TOPD * 256
NEAR_DP = LIP - 45                  # one 90-unit drift short of the lip
CY_PUREACH = 1                      # depth steps below the lip it is takeable
EARLY_DP = (CY_TOPD - CY_PUREACH) * 256 - 45    # ...one drift short of THAT
DEEP_DP = (CY_TOPD - CY_PUREACH) * 256 - 400    # ...and well below the zone
CYCPT = 262000                      # guest cycles a TICK: 4.77MHz / 18.2Hz.
                                    # EVERY wait here is in these and not in
                                    # time.sleep - os88marty.advance's own
                                    # docstring is the reason, and the grace
                                    # window is 15 ticks, so a wall-clock wait
                                    # lands somewhere different every run


class Game:
    def __init__(self, m, p):
        self.m, self.p = m, p

    def arm(self, lane):
        """Empty the board, park the claw on `lane`, and LET THE SWEEP BRACKET
        CATCH UP before anything is placed.

        [cy_psweep0] is where the claw was when pickups were last tested, so
        teleporting the claw between cases is itself a sweep (SPEC.md 67.24.4)
        - and it is, correctly. Without this settle every case inherits an arc
        reaching back to the PREVIOUS case's lane and collects things it should
        not, which reads as the feature over-triggering.
        """
        p, m = self.p, self.m
        # PIN THE WAVE. cy_spawn_tick is stubbed so nothing new arrives, and a
        # level with cy_wleft and cy_left both zero is a level CLEARED: the
        # game warps out, and cy_pu_update does not run in those states. It
        # reads as the pickup never being taken, which is indistinguishable
        # from the feature being broken - and cost a debugging round here.
        p.ww("cy_wleft", 40)
        p.ww("cy_left", 1)
        for i in range(3):                      # CY_MAXPU
            m.write(p.addr("cy_u_act") + i, bytes([0]))
            m.write(p.addr("cy_g_t") + i, bytes([0]))
        self.claw(lane)
        self.ticks(2)                           # ...so psweep0 == cy_plane
        p.wb("cy_pw_jump", 0)

    def place(self, lane, dp=None):
        """One JUMP pickup, one drift short of the lip, and nothing else."""
        p, m = self.p, self.m
        m.write(p.addr("cy_u_kind"), bytes([CYP_JUMP]))
        m.write(p.addr("cy_u_lane"), bytes([lane]))
        m.write(p.addr("cy_u_dp"),
                (NEAR_DP if dp is None else dp).to_bytes(2, "little"))
        m.write(p.addr("cy_u_act"), bytes([1]))

    def claw(self, lane):
        self.p.ww("cy_plane", lane)

    def settle(self, limit=40):
        """Run until the pending full repaint has been spent.

        A shape change sets [cy_full], and cy_draw_all is ~200ms on the target
        - three or four ticks in ONE frame - so a fixed wait after it lands
        mid-repaint and the first case measures a frame the pickup never got
        an update in. Waiting for the flag is the honest form.
        """
        for _ in range(limit):
            if self.p.rb("cy_full") == 0:
                return
            self.ticks(2)
        raise RuntimeError("the repaint never finished")

    def ticks(self, n):
        """Run exactly n GAME ticks. advance() ends STOPPED, which is fine -
        nothing between two of them does anything but poke memory."""
        self.m.advance(cycles=int(n * CYCPT))

    def land(self, limit=25):
        """Run until the pickup has REACHED THE LIP, rather than for a fixed
        number of ticks.

        How long that takes is not a constant: a frame that is repainting the
        whole web is three or four ticks long on its own, so a fixed wait after
        a shape change lands mid-repaint and the case measures a frame the
        pickup never got an update in. Tuning the wait made the failures MOVE
        between runs, which is the tell. [cy_u_act] leaving 1 is the event
        itself, and the take - or the grace record - has happened by then.
        """
        for _ in range(limit):
            if self.p.rb("cy_u_act") != 1:
                return True
            self.ticks(1)
        return False

    def took(self):
        return self.p.rb("cy_pw_jump") != 0


def case(g, name, lane, claw0, claw1=None, wait=0, want=True, dp=None):
    """A pickup on `lane` with the claw on `claw0`; then, optionally, the claw
    sweeps to `claw1` `wait` ticks after it landed."""
    g.arm(claw0)
    g.place(lane, dp)
    if not g.land():
        print("  %-34s VOID - the pickup never reached the lip" % name)
        return 1
    if claw1 is not None:
        g.ticks(wait)
        g.claw(claw1)
        g.ticks(3)
    st = g.p.rb("cy_state")
    if st != CYS_PLAY:
        print("  %-34s VOID - cy_state is %d, not PLAY" % (name, st))
        return 1
    got = g.took()
    ok = got == want
    print("  %-34s took=%-5s want=%-5s  %s"
          % (name, got, want, "ok" if ok else "FAIL"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_gla")
    a = ap.parse_args()
    S = os88sym.linear
    bad = 0
    near, grace = equ("CY_PUNEAR"), equ("CY_PUGRACE")
    print("CY_PUNEAR = %d (%s), CY_PUGRACE = %d frames (%.2fs)"
          % (near, "one lane either side" if near else "the exact lane only",
             grace, grace / 18.2))
    with os88marty.launch(os.path.join(_R, "build/os8088-360.img"),
                          apps=os.path.join(_R, "build/apps360.img"),
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
        seg = [u16(m.read(os88geom.winptr(m, w.i, S) + os88geom.W_SEG, 2))
               for w in os88geom.windows(m, S)
               if w.title.startswith("Cyclone")][0]
        syms, image = pkg_syms()
        lo, n = syms["cy_entry"], 2048
        if bytes(m.read(seg * 16 + lo, n)) != image[lo:lo + n]:
            raise RuntimeError("the CYCLONE.O88 running here is not this "
                               "source - run `make`")
        p = Pkg(m, seg, syms)
        mo.to(2, 2)
        for _ in range(10):
            if p.rb("cy_state") != CYS_TITLE:
                break
            m.key("Enter")
            time.sleep(2)
        os88marty.until(m, lambda _m: p.rb("cy_state") == CYS_PLAY,
                        "the warp to finish", poll=0.5, limit=90)
        m.write(p.addr("cy_spawn_tick"), bytes([0xC3]))     # a quiet board
        n_lane = p.rw("cy_nlane")
        print("playing; %d lanes, closed=%d" % (n_lane, p.rw("cy_closed")))
        g = Game(m, p)

        bad += case(g, "claw ON the lane",          5, 5)
        bad += case(g, "claw one lane LEFT",        5, 4, want=bool(near))
        bad += case(g, "claw one lane RIGHT",       5, 6, want=bool(near))
        bad += case(g, "claw three lanes away",     5, 8, want=False)
        # HALF the window, not 0: a record filed on one frame is first tested
        # on the next, so sweeping on immediately proves the scan runs and
        # nothing at all about the TIMER.
        bad += case(g, "...swept on, inside grace", 5, 8, claw1=5,
                    wait=grace // 2)
        bad += case(g, "...swept on, too late",     5, 8, claw1=5,
                    wait=grace + 6, want=False)

        # --- SPEC.md 67.24.2: the zone reaches BELOW the lip ---------------
        # The take has to happen while the pickup is still short of CY_TOPD,
        # and [cy_u_dp] is not cleared when the slot is freed - so reading it
        # back after the take is what proves "early" rather than "eventually".
        g.arm(5)
        g.place(5, EARLY_DP)
        g.land()
        dp = p.rw("cy_u_dp")
        ok = g.took() and dp < LIP
        print("  %-34s took=%-5s dp=%d (lip %d)  %s"
              % ("taken a step BEFORE the lip", g.took(), dp, LIP,
                 "ok" if ok else "FAIL"))
        bad += not ok

        # --- SPEC.md 67.24.4: THE CLAW SKIPS LANES ------------------------
        # cy_aim_mouse jumps straight to the lane nearest the pointer, so a
        # sweep is a jump of several lanes in ONE frame and the lanes in
        # between are never [cy_plane] on a frame boundary. Poking the lane
        # between two ticks is exactly that motion, and [cy_psweep0] is
        # captured at the END of cy_pu_update so the poke lands inside the arc.
        def sweep(name, lane, frm, to, want):
            g.arm(frm)
            g.place(lane)
            g.ticks(1)              # ...the pickup enters the zone here
            g.claw(to)              # ...and the claw jumps ACROSS it here
            g.ticks(2)
            got = g.took()
            ok = got == want
            print("  %-34s took=%-5s want=%-5s  %s"
                  % (name, got, want, "ok" if ok else "FAIL"))
            return 0 if ok else 1

        bad += sweep("swept ACROSS it, 3 -> 7", 5, 3, 7, True)
        bad += sweep("swept ACROSS it, 7 -> 3", 5, 7, 3, True)
        bad += sweep("swept past, never over it", 5, 7, 9, False)
        bad += sweep("stood still, wrong lane", 5, 9, 9, False)
        # ...and the SHORT way round: on a closed web 15 -> 2 is three lanes
        # forward, so lane 8 is on the far side and must NOT be collected.
        bad += sweep("wrapped 15 -> 2, lane 0", 0, 15, 2, True)
        bad += sweep("wrapped 15 -> 2, lane 8", 8, 15, 2, False)

        # ...and not from arbitrarily far down the tube
        g.arm(5)
        g.place(5, DEEP_DP)
        g.ticks(1)
        ok = not g.took()
        print("  %-34s took=%-5s  %s"
              % ("not taken from deep in the tube", g.took(),
                 "ok" if ok else "FAIL"))
        bad += not ok

        # --- AND THE WEB'S TOPOLOGY, which is the reason cy_pu_near asks
        # cy_wrap instead of doing arithmetic on the index. Lane 0's left
        # neighbour is the LAST lane on a closed web and lane 0 itself on an
        # open one, so the same pair of positions must answer differently on
        # the circle and on the flat ribbon.
        for shape, closed in (("cy_sh_circle", 1), ("cy_sh_flat", 0)):
            p.ww("cy_shape", syms[shape])
            p.wb("cy_needlay", 1)
            p.wb("cy_full", 1)
            g.settle()                  # ...and NOT advance(frames=) + sleep:
                                        # advance ends STOPPED, so the sleep
                                        # after it ran no guest time at all and
                                        # every case below it measured nothing
            nl, cl = p.rw("cy_nlane"), p.rw("cy_closed")
            if cl != closed:
                print("  %-34s SKIP (closed=%d, wanted %d)" % (shape, cl, closed))
                continue
            bad += case(g, "%s: lane 0, claw 0 (control)" % shape[6:],
                        0, 0, want=True)
            bad += case(g, "%s: lane 0, claw %d (wrap L)" % (shape[6:], nl - 1),
                        0, nl - 1, want=bool(closed) and bool(near))
            bad += case(g, "%s: lane %d, claw 0 (wrap R)" % (shape[6:], nl - 1),
                        nl - 1, 0, want=bool(closed) and bool(near))

        print("\ncycpu: %d failing case(s)" % bad)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
