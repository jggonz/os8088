#!/usr/bin/env python3
"""gfx_points' fast loop, on a MIXED pair of cards (SPEC.md 5.6.9.5.2).

tests/ptsext.py drives all three of SPEC.md 5.6.9.4's extended-desktop paths
and self-compares band against band, and it could not have caught this: it
boots `os8088_5150_both_gla_mono`, where BOTH displays are 1bpp.  The two
tests at gfx_points' door - `[vid_mono]` and `[vid_planes]` - are then true of
either card, so no hook can ever enter one the inline loop cannot write, and
the defect cannot be EXPRESSED on that machine whatever the kernel does.

THE MACHINE IS THE TEST.  `os8088_xt_vga_herc` is a VGA primary with a
Hercules beside it, and on it those two tests describe whichever display the
LAST primitive left current (SPEC.md 39.14.3 restores none on purpose) rather
than the one `.hook` is about to enter - so a call made while the Hercules was
current, on an array whose first point is on the VGA, reached the one-bit loop
with `ES = [vid_rseg] = 0` and wrote the IVT, the BIOS data area and the
kernel's own .text.  `.done`'s second pass was worse: it entered the OTHER
card with no test at all, so EVERY straddling array did it.

WHAT IS ASSERTED IS THE INVARIANT AND NOT THE CRASH.  An exec breakpoint at
`gfx_points.pass` - the instruction before `mov es, bx` - reads the display
the loop is about to write to, and the fix's whole claim is that it is always
one the loop can serve: `[vid_mono]` set, `[vid_planes]` 1, `[vid_rseg]`
non-zero.  That fires BEFORE the damage, so the row names the defect instead
of reporting the reboot it causes twenty frames later, and it is exact rather
than statistical - the pre-fix kernel trips it on the first straddling repaint
where the field's own scenario needed a drag or two to land somewhere fatal.

BREAK IT ON PURPOSE (docs/WRITING-TESTS.md 1): revert 5.6.9.5.2 - put
`jmp short .pass` back at `.hook` in place of the two re-tests - and this row
reports `vid_mono=0 vid_planes=4 vid_rseg=0000` at the first straddle and
exits 1, with `make test-fast` 46/46 and `ptsext` green against that same
kernel.
"""
import sys, os, time

_R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_R, "tools"))
sys.path.insert(0, os.path.join(_R, "tests"))
import os88marty, os88ui, os88sym, dispcp
from gfxpoints import PT_W

SYS = os.path.join(_R, "build/os8088-360.img")
APP = os.path.join(_R, "build/ptstest360.img")

TITLE_H = 18
PT_X = 8                # tests/ptstest/ptstest.asm draws its bands PT_X in
                        # from the content origin, PT_W wide
MAX_HITS = 3000         # a cap, not a budget: one repaint is a handful of
                        # hits and a drag is tens of repaints, so this only
                        # bounds a kernel that has gone round in circles


def u16(m, name):
    b = m.read(os88sym.linear(name), 2)
    return b[0] | (b[1] << 8)


class Watch:
    """The breakpoint, and what it found.

    Every stop is sampled and resumed, so the guest runs the drag at its own
    pace with the row reading one snapshot per pass.  `bad` is the list of
    passes that would have written off the display they were drawing to.
    """

    def __init__(self, m):
        self.m, self.hits, self.bad, self.seen = m, 0, [], set()

    def arm(self):
        self.m.bp_exec("gfx_points.pass")

    def disarm(self):
        self.m.breakpoints([])
        if self.m.stopped():
            self.m.run()

    def poll(self):
        """Sample and resume if the guest is sitting at the breakpoint."""
        m = self.m
        if not m.stopped():
            return
        while m.stopped() and self.hits < MAX_HITS:
            self.hits += 1
            mono = m.read(os88sym.linear("vid_mono"), 1)[0]
            planes = m.read(os88sym.linear("vid_planes"), 1)[0]
            rseg = u16(m, "vid_rseg")
            cur = m.read(os88sym.linear("vid_cur"), 1)[0]
            key = (cur, mono, planes, rseg)
            self.seen.add(key)
            if mono == 0 or planes != 1 or rseg == 0:
                if key not in [b[0] for b in self.bad]:
                    self.bad.append((key, self.hits))
            m.run()
            time.sleep(0.01)
        if self.hits >= MAX_HITS:
            self.disarm()


def drag(m, ui, w, watch, wx):
    """The window's FRAME to x=wx, one bounded step at a time.

    NOT ui.move_window: that verb waits on guest state, and the guest spends
    this drag sitting at a breakpoint.  The loop below owns the pacing, so a
    stop is sampled and resumed rather than timed out on.

    The argument is where the WINDOW goes, not where the pointer does - the
    grab is at the title bar's midpoint, so the two differ by half a window
    and a row written in pointer coordinates lands 88px short of where it
    meant to.  That is exactly far enough to leave the point bands on one
    card while the frame straddles, which reads as a kernel with no defect
    in it.
    """
    mo = ui.mo
    r = dispcp.win_rect(m, os88sym.linear, w)
    gx, gy = r[0] + r[2] // 2, r[1] + TITLE_H // 2
    tgt = wx + r[2] // 2
    for _ in range(80):
        watch.poll()
        cx, cy, _ = mo.where()
        if (cx, cy) == (gx, gy):
            break
        m.mouse(max(-40, min(40, gx - cx)), max(-40, min(40, gy - cy)))
    mo._sep()
    mo._edge(True)
    for _ in range(100):
        watch.poll()
        cx = mo.where()[0]
        if cx == tgt:
            break
        m.mouse(max(-40, min(40, tgt - cx)), 0, l=True)
    mo._edge(False)
    # let the release repaint land: what 2s of an idle box bought, counted on
    # the GUEST's clock - a bounded loop and not `pace`, because a pause ends
    # at a breakpoint and this one has to go on servicing them
    c0 = last = m.status()["cycles"]
    still = 0
    while last - c0 < 2.0 * (os88marty.GUEST_PACE or 4.5) * os88marty.GUEST_HZ:
        watch.poll()
        time.sleep(0.05)
        c = m.status()["cycles"]
        still = still + 1 if c == last else 0
        if still > 100:
            raise os88marty.MartyError("the guest clock stopped at cycle %d "
                                       "after the release" % c)
        last = c
    return dispcp.win_rect(m, os88sym.linear, w)


def main():
    S = os88sym.linear
    with os88ui.boot(SYS, apps=APP, machine="os8088_xt_vga_herc") as ui:
        m = ui.m
        cards = {c["idx"]: c["type"] for c in m.cards()}
        print("cards: %s" % cards)
        if "vga" not in cards.values() or not any(
                t in ("mda", "hercules") for t in cards.values()):
            print("ptsmix: this machine is not a MIXED pair - %s" % cards)
            return 1
        ui.path("B:/PTSTEST.O88")
        ui.settle()
        mo = ui.mo
        dispcp.open_panel(m, mo, S, os88marty.settle)
        dispcp.set_mode(m, mo, S, os88marty.settle, "right")
        dispcp.close_panel(m, mo, S, os88marty.settle)
        try:
            os88marty.until(m, lambda _m: m.read(S("vid_ndisp"), 1)[0] == 2,
                            "the desktop to extend", poll=0.2, limit=10)
        except os88marty.MartyError:
            pass                        # ...and the SETUP check says so
        nd = m.read(S("vid_ndisp"), 1)[0]
        seam = u16(m, "vid_cw") if m.read(S("vid_cur"), 1)[0] == 0 else 0
        print("ndisp=%d seam=%d" % (nd, seam))
        if nd != 2 or seam <= 0:
            print("ptsmix: SETUP FAILED - vid_ndisp=%d seam=%d" % (nd, seam))
            return 1

        w = ui.window("PtsTest")
        # WHERE THE BANDS STRADDLE, not where the WINDOW does - and the two
        # are different by enough to make the row green against a kernel that
        # has the defect.  The bands are PT_W wide starting PT_X in from the
        # content origin, and the window is wider than that, so a frame put
        # 60px short of the seam straddles it with every POINT still on the
        # primary: gfx_points is then handed an array that fits one display,
        # takes one pass, and the second pass this row exists to watch never
        # runs.  The offsets below put the seam at four phases THROUGH the
        # bands, so the first point is on either card in turn.
        dx = w.content[0] - w.x
        def band_at(frac):
            return seam - dx - PT_X - int(PT_W * frac)
        crossings = 0
        watch = Watch(m)
        watch.arm()
        for rd, frac in enumerate((0.5, 0.25, 0.75, 0.5)):
            for tgt, what in ((seam + 200, "onto the SECONDARY"),
                              (band_at(frac), "bands ACROSS the seam"),
                              (300, "back on the PRIMARY")):
                r = drag(m, ui, w.i, watch, tgt)
                b1, b2 = r[0] + dx + PT_X, r[0] + dx + PT_X + PT_W
                if b1 < seam < b2:
                    crossings += 1
                print("  round %d %-22s rect=%s bands=%d..%d hits=%d"
                      % (rd, what, r, b1, b2, watch.hits))
                # THE RECORD ITSELF, because a wild write lands in the
                # kernel's own .text and .bss and wm_wins is in it: a rect
                # of (4806, 22685, 41163, 50932) is not a window that moved,
                # it is a window record that was drawn on.
                if not (0 <= r[0] < 4096 and 0 <= r[1] < 4096
                        and 0 < r[2] < 4096 and 0 < r[3] < 4096):
                    print("  FAIL the window RECORD is not a rectangle any "
                          "more: %s - something has written over wm_wins "
                          "(SPEC.md 5.6.9.5.2)" % (r,))
                    watch.bad.append(((-1, -1, -1, -1), watch.hits))
                    watch.disarm()
                    return 1
                if watch.bad:
                    break               # the verdict is in; the guest is now
            if watch.bad:               # writing over itself and every round
                break                   # after this one is a no-op anyway
        watch.disarm()

        print("passes sampled: %d" % watch.hits)
        for cur, mono, planes, rseg in sorted(watch.seen):
            print("   .pass on display %d: mono=%d planes=%d rseg=%04X"
                  % (cur, mono, planes, rseg))
        if crossings == 0:
            print("ptsmix: SCENARIO NEVER HAPPENED - the point BANDS never "
                  "straddled the seam, so no array was ever split")
            return 1
        if watch.hits == 0:
            print("ptsmix: SCENARIO NEVER HAPPENED - gfx_points never reached "
                  "its inline loop, so nothing was asserted")
            return 1
        if watch.bad:
            for (cur, mono, planes, rseg), n in watch.bad:
                print("  FAIL pass %d entered display %d and took the ONE-BIT "
                      "loop: mono=%d planes=%d rseg=%04X - the loop writes "
                      "ES:DI and ES would be %04X (SPEC.md 5.6.9.5.2)"
                      % (n, cur, mono, planes, rseg, rseg))
            print("\nptsmix: %d of %d sampled passes were on a display the "
                  "inline loop cannot write" % (len(watch.bad), watch.hits))
            return 1

    print("\nptsmix: %d passes over %d straddles - gfx_points' inline loop "
          "was never entered on a display it cannot write (SPEC.md 5.6.9.5.2)"
          % (watch.hits, crossings))
    return 0


if __name__ == "__main__":
    sys.exit(main())
