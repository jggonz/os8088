#!/usr/bin/env python3
"""instwiz - INSTALL.O88's wizard, driven by a row (SPEC.md 117, 52.10.4.4).

NOT A ROW: a library, imported by tests/install.py and tests/instdeep.py.

The wizard is the only face the install engine has (SPEC.md 52.10.4.4), so
every row that installs drives it - the ones that START it from the Control
Panel's Install button through tests/instdeep.py's open_installer, and
tests/install.py, which double-clicks the file. One copy of its geometry and
its page numbers, read out of apps/install/install.asm and the package's own
map, rather than one per row that can drift.

Every page is read out of the package's own memory ([in_page], through the
window record's segment and build/install.map), so a click that did not land
is a named failure at the step, not a timeout twenty steps on.
"""
import os
import re
import struct
import sys

sys.path.insert(0, "tools")
import os88marty as M                                       # noqa: E402
import os88geom as geom                                     # noqa: E402
import os88build                                            # noqa: E402

SRC = open("apps/install/install.asm").read()


def const(name):
    m = re.search(r"^%s\s+equ\s+(\d+)" % name, SRC, re.M)
    if not m:
        sys.exit("instwiz: %s is not an `equ` in apps/install/install.asm"
                 % name)
    return int(m.group(1))


P = {n: const(n) for n in ("P_WELCOME", "P_DRIVE", "P_SLOT", "P_MODE",
                            "P_CONFIRM", "P_APPS", "P_DONE", "P_STOP",
                            "P_BUSY")}
NAMES = {v: k for k, v in P.items()}
BX0, BW, BGAP, BY, BH = (const(n) for n in
                         ("IN_BX0", "IN_BW", "IN_BGAP", "IN_BY", "IN_BH"))
LOGN, LOGC = const("IN_LOGN"), const("IN_LOGC")
BACK, NEXT, CANCEL = 0, 1, 2


def symmap(shipped=False):
    """The package's symbols, off the map its build rule writes. `shipped`:
    the kern_big build's map whatever the kernel - for a row that runs the
    INSTALL.O88 off a SHIPPED disk (tests/httpinstall.py serves
    os8088-360.img over HTTP), not mega.img's own."""
    out = {}
    # kern_mega ships its OWN INSTALL.O88 (the Makefile's MEGAINST, SPEC.md
    # 117.10.2), whose map is beside it in megak/apps/
    path = os88build.at("build/install.map")
    if os.environ.get("OS88_MEGA") and not shipped:
        path = os.path.join(os.environ.get("OS88_MEGA_BUILD")
                            or os.path.dirname(path),
                            "megak", "apps", "install.map")
    for line in open(path):
        p = line.split()
        if len(p) == 3 and re.match(r"^[0-9A-F]+$", p[0]):
            out[p[2]] = int(p[0], 16)
    return out


SYM = symmap()


def marty_until(m):
    def until(cond, what, limit):
        M.until(m, lambda _: cond(), what, limit=limit)
    return until


class Wizard(object):
    """The INSTALL window on one machine: its pages, read and clicked.

    `i`, `x`, `y` are its window record's index and its OUTER origin, `click`
    a callable (x, y) - the caller's own mouse, whichever driver that is.

    MartyPC by default. A QEMU row passes the three things that differ:
    `sym` (the kernel's symbols), `until(cond, what, limit)` - a wait in GUEST
    seconds that FAILS when it runs out - and `key(name)`. `m.read(linear, n)`
    is the one memory call, and both machines have it."""

    def __init__(self, m, click, i, x, y, sym=None, until=None, key=None):
        self.m, self.click, self.i, self.x, self.y = m, click, i, x, y
        self.until = until or marty_until(m)
        self.key = key or m.key
        base = (sym or m.sym)("wm_wins") + i * geom.WIN_SIZE
        self.seg = struct.unpack("<H", bytes(m.read(base + geom.W_SEG, 2)))[0]

    def mem(self, name, n, add=0):
        # the region is a SELECTOR on kern_mega (docs/BOCHS.md): m.lin
        lin = getattr(self.m, "lin", None)
        at = SYM[name] + add
        return bytes(self.m.read(lin(self.seg, at) if lin else
                                 self.seg * 16 + at, n))

    def byte(self, name):
        return self.mem(name, 1)[0]

    def word(self, name, add=0):
        return struct.unpack("<H", self.mem(name, 2, add))[0]

    def page(self):
        return self.byte("in_page")

    def caption(self):
        return self.mem("in_cap", 48).split(b"\0")[0].decode("latin-1")

    def button(self, n):
        x0, y0 = self.x + 1, self.y + geom.TITLE_H
        self.click(x0 + BX0 + n * (BW + BGAP) + BW // 2, y0 + BY + BH // 2)

    def wait(self, want, what, limit=60.0):
        def ok():
            pg = self.page()
            if pg == P["P_STOP"] and want != P["P_STOP"]:
                sys.exit("instwiz: FAIL - the install STOPPED %s: %r"
                         % (what, self.caption()))
            return pg == want
        self.until(ok, "INSTALL to show %s (%s)" % (NAMES[want], what), limit)
        print("  %-10s %s" % (NAMES[want][2:], what))

    def next(self, want, what, limit=60.0):
        self.button(NEXT)
        self.wait(want, what, limit)

    def log(self):
        """The lines the Installing page keeps (SPEC.md 117.4.1), oldest
        first - the last IN_LOGN of the run."""
        n = self.byte("in_logn")
        raw = self.mem("in_log", LOGN * (LOGC + 1))
        return [raw[i * (LOGC + 1):i * (LOGC + 1) + LOGC].decode("latin-1")
                .rstrip() for i in range(n)]

    def ended(self, what, limit=900.0, sample=None):
        """Install has been pressed on Confirm: wait for the Installing page
        to have its answer. It STAYS on its log until Next (SPEC.md 117.1)."""
        def done():
            pg = self.page()
            if pg not in (P["P_CONFIRM"], P["P_BUSY"]):
                sys.exit("instwiz: FAIL - Install left the Installing page "
                         "by itself, for %s: its log must stay until Next"
                         % NAMES.get(pg, pg))
            fin = pg == P["P_BUSY"] and self.byte("in_logend")
            if pg == P["P_BUSY"] and not fin and sample:
                sample()
            return fin
        self.until(done, "the Installing page to end (%s)" % what, limit)
        lines = self.log()
        print("  BUSY       %s; the log ends:" % what)
        for ln in lines:
            print("             | %s" % ln)
        return lines

    def run(self, wipe=None, expect=("P_DONE", "P_APPS"), limit=900.0):
        """Welcome to the end with every default the wizard offers, except
        `wipe`: True picks Erase on Keep or Erase, False picks Keep, None
        leaves what the page chose (SPEC.md 52.10.15 - KEEP over a volume,
        and over a slot with nothing on it there is nothing to choose).

        out: the page Next went on to after the log, one of `expect`."""
        self.wait(P["P_WELCOME"], "on launch")
        self.next(P["P_DRIVE"], "after Welcome: the driver and the drives",
                  limit=120.0)
        self.next(P["P_SLOT"], "after the drive", limit=120.0)
        self.button(NEXT)
        self.until(lambda: self.page() in (P["P_MODE"], P["P_CONFIRM"]),
                   "INSTALL to leave the partition page", 60.0)
        if self.page() == P["P_MODE"]:
            print("  MODE       %s" % ("Erase" if self.byte("in_wipe")
                                       else "Keep"))
            if wipe is not None and self.byte("in_wipe") != int(wipe):
                self.key("ArrowDown" if wipe else "ArrowUp")
                self.until(lambda: self.byte("in_wipe") == int(wipe),
                           "the arrow key to pick %s"
                           % ("Erase" if wipe else "Keep"), 20.0)
                print("  MODE       picked %s" % ("Erase" if wipe else "Keep"))
            self.next(P["P_CONFIRM"], "after keep or erase")
        self.button(NEXT)                        # Install
        self.ended("after Install", limit=limit)
        want = [P[e] for e in expect]
        self.button(NEXT)
        self.until(lambda: self.page() in want + [P["P_STOP"]],
                   "INSTALL to go on from its log", 60.0)
        pg = self.page()
        if pg not in want:
            sys.exit("instwiz: FAIL - the install ended on %s, not %s: %r"
                     % (NAMES[pg], "/".join(expect), self.caption()))
        print("  %-10s after Next on the log" % NAMES[pg][2:])
        return pg


def qemu(q, sym, click, win):
    """The wizard on a QEMU machine (tests/os88qemu.py's), for the rows that
    install where MartyPC cannot: a 286 (tests/hmadrv.py) and a NIC
    (tests/httpinstall.py). `win` is an os88geom window record."""
    import os88qemu
    import xmcheck

    def until(cond, what, limit):
        if not os88qemu.acted(q, cond, secs=limit, what=what):
            sys.exit("instwiz: FAIL - timed out waiting for %s" % what)

    def key(name):
        xmcheck.qmp(q.sock, "sendkey %s" % {"ArrowDown": "down",
                                            "ArrowUp": "up"}[name])
    return Wizard(q, click, win.i, win.x, win.y, sym=sym, until=until,
                  key=key)
