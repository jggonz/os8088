#!/usr/bin/env python3
"""CELLS PUT ON THE GLASS, not calls (SPEC.md 11.3.3).

A repaint is priced by the primitive calls it makes - until a clip region is
armed, at which point a call can put nothing down at all. Two things in this
tree make that the difference between a measurement and a mirage:

* **SPEC.md 11.3.3's cull.** A cell the region culls is still a CALL:
  `font_run` is entered and paints nothing.
* **SPEC.md 28.2's retry.** A chunk the region cut zeroes its own check word
  so the next interval tries again - so an armed region makes the call count
  go UP while the pixels go down.

So a gate that wants to know what a repaint actually drew has to read the
armed region and work out which cells got through. That is this module, in
one place because two copies of it would be two opinions about what "drawn"
means.

`Cells` is a breakpoint pump: it services `font_run_x` and `gfx_fill`,
attributes each to the package or the kernel by its RETURN ADDRESS (slot
0x0258 is an X cell and slot 0x0038 a plain one, so the kernel's own drawing
is told apart from an app's), and counts the cells the region would let
through. Drive the mouse with its own `to`/`edge`/`click`/`drag`, not
os88mouse's: those wait on the guest's published `mouse_btn`, and a guest
parked at a breakpoint never publishes it.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88geom, os88sym

S = os88sym.linear
KSEG = os88geom.KERNEL_SEG
WCR_SZ = 8
STEP = 8
FILL_SLOT = 0x0038 + 6          # the slot's `call gfx_fill` is at +3


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


class Pump:
    """Drive the mouse with a breakpoint pump in the SAME loop.

    os88mouse waits on the guest's own published `mouse_btn`, and a guest
    parked at a breakpoint never publishes it - so a gate that arms one and
    then calls `mo.click` hangs. Everything here polls `mo.where()`, which is
    a debug read and answers whether the guest is running or stopped, and
    services the breakpoint between packets.

    Subclasses answer `on_stop(r, at)`.
    """

    def __init__(self, m, mo, syms):
        self.m, self.mo = m, mo
        self.at = {m.sym(x): x for x in syms}
        m.bp_exec(*syms)
        m.run()

    def on_stop(self, r, at):
        raise NotImplementedError

    def serve(self):
        """Service one stop, if the guest is at one.

        HITS GO MISSING HERE and nothing in this module can currently stop
        them. Measured against the kernel's own control flow: a run reported
        `wm_su_occl` with no `wm_su_try`, and wm_su_occl is reachable through
        no other path (kernel/wm.inc has one caller each), so the try stop was
        lost. It is not advance() consuming a pending stop - advance() called
        while already stopped answers advanced_frames 0 and leaves IP where it
        was, measured - and it is not the register filter, since the raw log
        shows the same. How often it happens depends on how many breakpoints
        are armed and how busy the guest is, which is why the same leg passed
        6 runs in 6 with four breakpoints and 1 in 3 with two.

        SO A GATE THAT COUNTS RARE EVENTS HERE MUST RETRY THE GESTURE rather
        than fail on one observation (tests/zoomsave.py does). Until this is
        understood, treat a zero from a single pass as `not seen`, never as
        `did not happen`.
        """
        m = self.m
        if not m.stopped():
            return False
        r = m.regs()
        at = ((r["cs"] & 0xFFFF) << 4) + (r["ip"] & 0xFFFF)
        if at in self.at:
            self.on_stop(r, self.at[at])
        m.run()                         # ALWAYS: an advance() completion stops
        return True                     # the guest too, and an unserviced one
                                        # wedges the pump for good

    def pump(self, k=400, frames=4):
        for _ in range(k):
            if not self.serve():
                self.m.advance(frames=frames)

    def to(self, x, y, l=False, tries=400):
        for _ in range(tries):
            cx, cy, _ = self.mo.where()
            if (cx, cy) == (x, y):
                return True
            self.m.mouse(max(-STEP, min(STEP, x - cx)),
                         max(-STEP, min(STEP, y - cy)), l=l)
            for _ in range(3):
                if not self.serve():
                    self.m.advance(frames=2)
        return False

    def edge(self, down, tries=300):
        want = 1 if down else 0
        for i in range(tries):
            if i % 30 == 0:
                self.m.mouse(0, 0, l=down)
            if self.serve():
                continue
            if (self.mo.where()[2] & 1) == want:
                return True
            self.m.advance(frames=4)
        return False

    def click(self, x, y):
        self.to(x, y)
        return self.edge(True) and self.edge(False)

    def dblclick(self, x, y):
        self.to(x, y)
        return (self.edge(True) and self.edge(False)
                and self.edge(True) and self.edge(False))

    def drag(self, x0, y0, x1, y1):
        ok = self.to(x0, y0) and self.edge(True)
        ok = self.to(x1, y1, l=True) and ok
        return self.edge(False) and ok

    def close(self):
        self.m.breakpoints([])
        self.m.run()


class Bp(Pump):
    """Count kernel calls by symbol, and how many were about OUR window.

    `want` is {symbol: register name}; `who` is the window record offset the
    register is compared against."""

    def __init__(self, m, mo, want, who):
        self.want, self.who = want, who
        self.n = {}
        Pump.__init__(self, m, mo, list(want))

    def on_stop(self, r, sym):
        mine = (r[self.want[sym]] & 0xFFFF) == self.who
        k = sym + ("/us" if mine else "/other")
        self.n[k] = self.n.get(k, 0) + 1

    def take(self):
        got, self.n = dict(self.n), {}
        return got


class Cells(Pump):
    """Count what a repaint puts on the glass, while driving the mouse."""

    def __init__(self, m, mo, land=None, cull_sym="wm_dmg_cull"):
        self.m, self.mo = m, mo
        self.land = land                # an optional rect to attribute against
        self.fr = m.sym("font_run_x")
        self.fi = m.sym("gfx_fill")
        # SPEC.md 20.3: every X cell jumps to ONE body, `api_x`, whose
        # `call bp` is the whole of how a package reaches a kernel routine.
        # The window is that body's 12 bytes. It stays exact even though the
        # body is shared, because the breakpoint is on font_run_x and no
        # other cell targets it.
        self.stub = m.sym("api_x") - (KSEG << 4)
        try:
            m.sym(cull_sym)
            self.cull_sym = cull_sym    # a kernel without 11.3.3 has no flag,
        except Exception:               # and every cell is then painted
            self.cull_sym = None
        self.runs = self.fills = 0
        self.cells = self.inside = 0
        Pump.__init__(self, m, mo, ["font_run_x", "gfx_fill"])

    # --- the region, as the kernel has it right now ------------------------
    def frags(self):
        n = u16(self.m.read(S("wm_clip_n"), 2))
        if not n:
            return None
        b = self.m.read(S("wm_clip_tab"), n * WCR_SZ)
        return [(u16(b, k * WCR_SZ), u16(b, k * WCR_SZ + 2),
                 u16(b, k * WCR_SZ + 4), u16(b, k * WCR_SZ + 6))
                for k in range(n)]

    def painted(self, cell, fr, cull):
        """wm_clip_test's answer for one cell - CONTAINMENT, or OVERLAP when
        the cull has the corners exchanged (SPEC.md 11.3.3)."""
        if fr is None:
            return True
        x1, y1, x2, y2 = cell
        for a, b, c, e in fr:
            if cull:
                if x2 >= a and x1 <= c and y2 >= b and y1 <= e:
                    return True
            elif x1 >= a and x2 <= c and y1 >= b and y2 <= e:
                return True
        return False

    def hits(self, r):
        cull = 0
        if self.cull_sym:
            cull = self.m.read(S(self.cull_sym), 1)[0]
        fr = self.frags()
        x, y = r["cx"] & 0xFFFF, r["dx"] & 0xFFFF
        txt = self.m.read(((r["es"] & 0xFFFF) << 4) + (r["si"] & 0xFFFF), 48)
        ln = txt.find(b"\0")
        ln = 48 if ln < 0 else ln
        self.runs += 1
        for k in range(ln):
            cell = (x + 8 * k, y, x + 8 * k + 7, y + 7)
            if not self.painted(cell, fr, cull):
                continue
            self.cells += 1
            if self.land and (cell[0] <= self.land[2] and cell[2] >= self.land[0]
                              and cell[1] <= self.land[3]
                              and cell[3] >= self.land[1]):
                self.inside += 1

    def on_stop(self, r, sym):
        m = self.m
        b = m.read(((r["ss"] & 0xFFFF) << 4) + (r["sp"] & 0xFFFF), 2)
        ret = b[0] | (b[1] << 8)
        if sym == "font_run_x":
            if self.stub <= ret < self.stub + 12:
                self.hits(r)
        elif ret == FILL_SLOT:
            self.fills += 1

    def take(self):
        got = (self.runs, self.fills, self.cells, self.inside)
        self.runs = self.fills = self.cells = self.inside = 0
        return got
