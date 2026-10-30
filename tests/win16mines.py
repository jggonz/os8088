#!/usr/bin/env python3
"""win16mines - Windows 3.1's Minesweeper played on WIN16 (SPEC.md 124.15,
docs/plans/WIN16-PLAN.md S8).

    python3 tests/win16mines.py            the QEMU leg (row win16mines)
    python3 tests/win16mines.py --marty    the XT legs (row win16minesxt)
    python3 tests/win16mines.py --keep     leave QEMU and its disks for a look

The program is Microsoft's and is NEVER in this tree: it is whatever the
win16-reference skill put in build/win16/ (W31MINE.EXE, Minesweeper 3.0a off
Windows 3.1), and the suite runs this only where that file exists (the
`win16samples` capability). Each leg writes its OWN B: - WIN16.O88, the
-DDOS_WIN16 DOS.O88 that hands an NE to it, the program and an empty
SYSTEM/APPDATA/WIN16 - so a stale build/win16test.img cannot decide anything.

THE BOARD IS READ, NOT GUESSED. W31MINE keeps it in its DGROUP at 0x440, 32
bytes a row with a border row and column all round (0x10), a covered cell
0x0F, a mine 0x80 on top, a revealed one 0x40; the width and height are the
words at 0x3C6/0x3C8. WIN16's `w16_dgroup` names the segment. A cell (c, r),
both counted from the border, is at client (16c - 4, 39 + 16r); the client's
screen origin is what WIN16's hit test last computed (`w16_hox`/`w16_hoy`),
after one click on the client's empty bottom edge.

QEMU, VGA:
  1. the program opens a window called Minesweeper, WIN16 not refused;
  2. a right-click flags a covered cell (low nibble 0x0E) and a second one
     takes the flag off again;
  3. every safe cell clicked: the game is WON - each safe cell revealed and
     the program still running - and Enter, Enter takes the name dialog's
     default and the Best Times box;
  4. F2, a safe cell, then a mine: the mine is revealed (LOST), and WIN16
     still running;
  5. the close box ends it, and SYSTEM\\APPDATA\\WIN16\\WINMINE.INI on B:
     holds the won game's Time1 (not 999) and a Name1.

MartyPC (--marty):
  6. a 5150 with an 8088 refuses the program with SPEC.md 124.4's sentence -
     `Needs a V20, 80186 or better.` - in WIN16's own window;
  7. the same 5150 with a NEC V20 (Hercules) plays a game to a win, and the
     XT's cost is PRINTED: the launch, and each reveal from the click to the
     UI's next idle, in BIOS ticks and ms. A number, not a pass/fail: it is
     the S13 final pass's measurement.
"""
import argparse
import os
import shutil
import statistics
import struct
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)
import os88build                                              # noqa: E402
import os88geom as geom                                       # noqa: E402

PROG = "W31MINE.EXE"
WINDIR = "SYSTEM/APPDATA/WIN16"
BOARD, BW, BH = 0x440, 0x3C6, 0x3C8     # W31MINE's DGROUP
COVERED, MINE, SHOWN, EDGE = 0x0F, 0x80, 0x40, 0x10
FLAG = 0x0E
W16S_REFUSED = 2
REFUSAL = "Needs a V20, 80186 or better."
TICK_MS = 54.925
V20 = "os8088_5150_herc_hdd_v20_gla"
XT = "os8088_5150_cga_gla"


def b(name):
    return os88build.at(os.path.join("build", name))


def sample():
    return b(os.path.join("win16", PROG))


def pkg_syms():
    """WIN16's own symbols: win16.asm re-assembled with a map. The package
    is the TRIMMED image (the dialog part cut off its end, os88ovl.py
    --trim), which moves nothing before the cut, so the map's offsets are
    the loaded image's."""
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "w.asm"), os.path.join(d, "w.map")
        open(cp, "w").write('%%include "win16/win16.asm"\n'
                            "[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                        "-I", "apps/win16/", "-o", os.path.join(d, "w.bin"),
                        cp], check=True)
        out = {}
        for line in open(mp):
            f = line.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                out[f[2]] = int(f[0], 16)
        return out


def bdisk(out, size):
    """The leg's B:: WIN16, its DOS, the program, the Windows directory."""
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", out,
                    "--size", str(size), b("win16.o88"),
                    b("win16/DOS.O88"), sample(),
                    "--folder", "SYSTEM/APPDATA", "--folder", WINDIR],
                   check=True, capture_output=True)


def u16(raw, o=0):
    return struct.unpack_from("<H", raw, o)[0]


class Game(object):
    """One machine with Minesweeper on it, over a driver `d` that has
    read(linear, n), click(x, y), rclick(x, y), dblclick(x, y), key(name),
    idle(cap) and wait(cond, secs, what)."""

    def __init__(self, d, ui, sym):
        self.d, self.ui, self.sym = d, ui, sym
        self.S = pkg_syms()
        self.w = None
        self.seg = 0
        self.ox = self.oy = 0

    def ticks(self):
        return struct.unpack("<I", self.d.read(0x46C, 4))[0]

    def pw(self, name, off=0):
        return u16(self.d.read((self.seg << 4) + self.S[name] + off, 2))

    def pb(self, name, off=0):
        return self.d.read((self.seg << 4) + self.S[name] + off, 1)[0]

    def text(self, name):
        raw = self.d.read((self.seg << 4) + self.S[name], 48)
        return raw.split(b"\0")[0].decode("latin-1")

    def launch(self):
        """B:'s window, the program double-clicked in it, and the window it
        opened: WIN16's, whatever its title (a refusal keeps WIN16's own).
        Answers the ticks it took from the double-click."""
        ui, d = self.ui, self.d
        before = set(w.i for w in ui.windows())
        d.dblclick(*geom.drive_pt(d, "B", self.sym))
        box = {}

        def disk():
            for w in ui.windows():
                if w.i not in before and w.visible:
                    box["w"] = w
                    return True
            return False
        d.wait(disk, 60, "B:'s window")
        d.idle(4.0)
        dw = box["w"]
        i, _ = ui.entry(PROG, dw)
        before = set(w.i for w in ui.windows())
        t0 = self.ticks()
        d.dblclick(*ui.row_xy(dw, i - ui.scroll(dw)))

        def opened():
            # WIN16's window, not DOS.O88's: the association opens DOS, which
            # hands the NE on and closes, and while both are up the first new
            # window with a segment is DOS's - whose bytes at w16_state are
            # not a WIN16 state (win16fpu's Prog.launch, the same test)
            for w in ui.windows():
                if w.i not in before and w.visible:
                    seg = u16(d.read(geom.winptr(d, w, self.sym)
                                     + geom.W_SEG, 2))
                    if seg and d.read((seg << 4) + self.S["w16_name"],
                                      len(PROG) + 1) == PROG.encode() + b"\0":
                        box["p"] = (w, seg)
                        return True
            return False
        d.wait(opened, 120, "a window for %s" % PROG)
        self.w, self.seg = box["p"]
        d.wait(lambda: self.pb("w16_state") == W16S_REFUSED or
               "Minesweeper" in self.title(), 120,
               "Minesweeper, or WIN16's refusal")
        d.idle(6.0)
        return self.ticks() - t0

    def title(self):
        for w in self.ui.windows():
            if w.i == self.w.i:
                self.w = w
                return w.title
        return ""

    def gone(self):
        return all(w.i != self.w.i or not w.visible
                   for w in self.ui.windows())

    def origin(self):
        """Click the client's empty bottom edge (below the board, no cell),
        then read the client origin WIN16's hit test computed for it."""
        self.title()
        cx0, cy0, cx1, cy1 = self.w.content
        self.d.click(cx0 + 6, cy1 - 2)
        self.d.idle(2.0)
        self.ox, self.oy = self.pw("w16_hox"), self.pw("w16_hoy")

    def board(self):
        dg = self.pw("w16_dgroup")
        raw = self.d.read((dg << 4) + BW, 4)
        w, h = u16(raw, 0), u16(raw, 2)
        rows = self.d.read((dg << 4) + BOARD, 32 * (h + 2))
        return w, h, [list(rows[r * 32:r * 32 + w + 2]) for r in range(h + 2)]

    def xy(self, c, r):
        return self.ox + 16 * c - 4 + 8, self.oy + 39 + 16 * r + 8

    def cells(self, want):
        w, h, g = self.board()
        return [(c, r) for r in range(1, h + 1) for c in range(1, w + 1)
                if want(g[r][c])], g

    def hit(self, c, r, right=False):
        t0 = self.ticks()
        (self.d.rclick if right else self.d.click)(*self.xy(c, r))
        self.d.idle(3.0)
        return self.ticks() - t0

    def win(self, costs=None):
        """Click safe cells until none is covered; answers the board."""
        for _ in range(16 * 30):
            todo, g = self.cells(lambda v: not v & (MINE | SHOWN))
            if not todo:
                return g
            t = self.hit(*todo[0])
            if costs is not None:
                costs.append(t)
        return self.board()[2]

    def lose(self):
        """A safe cell first - the first click is never a mine, so a game
        cannot be lost with it - then a mine. Answers that mine's value."""
        safe, _ = self.cells(lambda v: not v & (MINE | SHOWN))
        self.hit(*safe[0])
        mines, _ = self.cells(lambda v: v & MINE and not v & SHOWN)
        c, r = mines[0]
        self.hit(c, r)
        return self.board()[2][r][c]

    def close(self):
        self.title()
        self.d.click(*geom.close_xy(self.w.x, self.w.y, self.w.w,
                                    m=self.d, sym=self.sym))
        self.d.wait(self.gone, 30, "the program's window to close")
        self.d.idle(6.0)


# --- QEMU ---------------------------------------------------------------------

class QDrive(object):
    """A private QEMU: the shipped 1.44MB system disk in A:, the leg's B:."""

    def __init__(self, work, bimg, keep):
        import os88qemu
        import xmcheck
        self.q, self.x = os88qemu, xmcheck
        self.p = os88qemu.Private(work, b("os8088.img"), bimg)
        self.sock = self.p.sock
        if keep:
            import atexit
            atexit.unregister(self.p.close)
        else:
            os88qemu.own(os.path.join(work, "qemu.pid"), self.sock)
        xmcheck.wait_desktop(self.sock, "win16mines")

    def hmp(self, *cmds):
        return self.x.qmp(self.sock, *cmds)

    def read(self, linear, n):
        return bytes(self.x.read_bytes(self.sock, linear, n))

    def readseg(self, seg, off, n):
        return self.read((seg << 4) + off, n)

    def idle(self, cap=2.0):
        self.q.ui_done(self, self.x.sym, cap=cap)

    def click(self, x, y):
        self.q.mouse_click(self, self.x.sym, self.hmp, x, y)

    def rclick(self, x, y):
        self.q.mouse_click(self, self.x.sym, self.hmp, x, y, mask=2)

    def dblclick(self, x, y):
        self.q.mouse_dblclick(self, self.x.sym, self.hmp, x, y)

    def key(self, name):
        self.hmp("sendkey %s" % {"Enter": "ret", "F2": "f2"}[name])

    def wait(self, cond, secs, what):
        if not self.q.acted(self, cond, secs=secs, what=what, poll=0.1):
            raise RuntimeError("timed out waiting for %s" % what)


def ini(img):
    r = subprocess.run(["mtype", "-i", img, "::/%s/WINMINE.INI" % WINDIR],
                       capture_output=True)
    return r.stdout.decode("latin-1") if r.returncode == 0 else None


def qemu_leg(bad, keep):
    import os88ui
    import xmcheck
    work = tempfile.mkdtemp(prefix="w16m-")     # short: a QMP socket path
    try:
        bimg = os.path.join(work, "src.img")
        bdisk(bimg, 1440)
        d = QDrive(work, bimg, keep)
        ui = os88ui.UI(d, mouse=object(), sym=xmcheck.sym, verbose=False)
        g = Game(d, ui, xmcheck.sym)
        g.launch()
        print("1. %s opened %r (WIN16 state %d)"
              % (PROG, g.title(), g.pb("w16_state")))
        if g.pb("w16_state") == W16S_REFUSED:
            bad.append("1: WIN16 refused it on QEMU: %s / %s"
                       % (g.text("w16_l1"), g.text("w16_l2")))
            return
        if "Minesweeper" not in g.title():
            bad.append("1: the window is %r, not Minesweeper" % g.title())
        g.origin()
        w, h, _ = g.board()
        print("   client at (%d,%d), a %dx%d board" % (g.ox, g.oy, w, h))
        # Windows 3.1's Beginner is 8 x 8 (win16ref/ref/w31-vga-winmine.png, a
        # fresh install; 9 x 9 is Windows 2000's). This said 9 x 9 until S13's
        # final pass, the first registered run of the row
        if (w, h) != (8, 8):
            bad.append("1: the board is %dx%d - Beginner's is 8x8 (an empty "
                       "WINMINE.INI)" % (w, h))
        # 2. flags
        c, r = 5, 5
        g.hit(c, r, right=True)
        v1 = g.board()[2][r][c]
        g.hit(c, r, right=True)
        g.hit(c, r, right=True)         # Marks: flag -> ? -> covered
        v3 = g.board()[2][r][c]
        print("2. right-clicks: %02x then, two more, %02x" % (v1, v3))
        if v1 & 0x0F != FLAG:
            bad.append("2: a right-click left cell (%d,%d) at %02x, not a "
                       "flag (0x0E)" % (c, r, v1))
        if v3 & 0x0F != COVERED:
            bad.append("2: two more right-clicks left it at %02x, not covered"
                       % v3)
        # 3. win
        grid = g.win()
        left = [(cc, rr) for rr in range(1, h + 1) for cc in range(1, w + 1)
                if not grid[rr][cc] & (MINE | SHOWN)]
        print("3. played to a win: %d safe cells still covered" % len(left))
        if left:
            bad.append("3: %d safe cells were never revealed" % len(left))
        d.key("Enter")                  # the name dialog's OK
        d.idle(4.0)
        d.key("Enter")                  # ...and the Best Times box's
        d.idle(4.0)
        # 4. lose
        d.key("F2")
        d.idle(4.0)
        v = g.lose()
        print("4. F2, a safe cell, then a mine: it reads %02x" % v)
        if not v & SHOWN:
            bad.append("4: the mine clicked was not revealed (%02x) - the "
                       "game was not lost" % v)
        if g.gone() or g.pb("w16_state") == W16S_REFUSED:
            bad.append("4: WIN16 did not survive the loss")
        # 5. exit, and what it saved
        g.close()
        text = ini(d.p.b)
        print("5. WINMINE.INI:\n   " +
              ("\n   ".join((text or "(absent)").splitlines())))
        if text is None:
            bad.append("5: no %s\\WINMINE.INI on B: after the exit" % WINDIR)
            return
        keys = dict(ln.split("=", 1) for ln in text.splitlines()
                    if "=" in ln)
        t1 = keys.get("Time1", "").strip()
        if not t1.isdigit() or int(t1) >= 999:
            bad.append("5: Time1 is %r - the win was not recorded" % t1)
        if not keys.get("Name1", "").strip():
            bad.append("5: no Name1 for the recorded time")
        if not keep:
            d.p.close()
    finally:
        if not keep:
            shutil.rmtree(work, ignore_errors=True)
        else:
            print("kept: %s (QMP %s)" % (work, os.path.join(work, "qmp.sock")))


# --- MartyPC ------------------------------------------------------------------

class MDrive(object):
    def __init__(self, ui):
        import os88marty
        self.ui, self.m, self.mo, self.mm = ui, ui.m, ui.mo, os88marty

    def read(self, linear, n):
        return bytes(self.m.read(linear, n))

    def readseg(self, seg, off, n):
        return self.read((seg << 4) + off, n)

    def idle(self, cap=2.0):
        # THE UI FINISHING, not the screen standing still: once a game is
        # under way Minesweeper's clock repaints its counter every second, so
        # a wait for an unchanging screen never ends (win16minesxt's V20 leg,
        # S13's first registered run, 90 guest seconds into the first reveal)
        self.mm.ui_done(self.m, limit=max(cap, 30.0))

    def click(self, x, y):
        self.mo.click(x, y, settle=0)

    def rclick(self, x, y):
        self.mo.to(x, y, r=True)
        self.mo.to(x, y)

    def dblclick(self, x, y):
        self.mo.dblclick(x, y, settle=0)

    def key(self, name):
        self.m.key(name)

    def wait(self, cond, secs, what):
        t = 0.0
        while not cond():
            if t >= secs:
                raise RuntimeError("timed out waiting for %s" % what)
            self.mm.pace(self.m, 0.5)
            t += 0.5


def marty_leg(bad):
    import os88ui
    work = tempfile.mkdtemp(prefix="w16mx-", dir=os.path.join(ROOT, "build"))
    try:
        bimg = os.path.join(work, "b360.img")
        bdisk(bimg, 360)
        # 6. the 8088
        with os88ui.boot(b("os8088-360.img"), apps=bimg, machine=XT,
                         verbose=False) as ui:
            g = Game(MDrive(ui), ui, ui.sym)
            g.launch()
            st, l1, l2 = g.pb("w16_state"), g.text("w16_l1"), g.text("w16_l2")
            print("6. an 8088: state %d, %r / %r" % (st, l1, l2))
            if st != W16S_REFUSED or REFUSAL not in (l1 + l2):
                bad.append("6: an 8088 did not refuse %s with %r (state %d, "
                           "%r / %r)" % (PROG, REFUSAL, st, l1, l2))
        # 7. the V20
        with os88ui.boot(b("os8088-360.img"), apps=bimg, machine=V20,
                         verbose=False) as ui:
            g = Game(MDrive(ui), ui, ui.sym)
            t = g.launch()
            if g.pb("w16_state") == W16S_REFUSED:
                bad.append("7: a V20 was refused: %s / %s"
                           % (g.text("w16_l1"), g.text("w16_l2")))
                return
            g.origin()
            costs = []
            grid = g.win(costs)
            w, h, _ = g.board()
            left = sum(1 for rr in range(1, h + 1) for cc in range(1, w + 1)
                       if not grid[rr][cc] & (MINE | SHOWN))
            if left:
                bad.append("7: the V20's game left %d safe cells" % left)
            print("7. a V20 (4.77 MHz, Hercules): launch %d ticks (%.1f s); "
                  "%d reveals, click to idle median %d ticks (%.0f ms), "
                  "worst %d (%.0f ms)"
                  % (t, t * TICK_MS / 1000, len(costs),
                     statistics.median(costs) if costs else 0,
                     (statistics.median(costs) if costs else 0) * TICK_MS,
                     max(costs or [0]), max(costs or [0]) * TICK_MS))
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--marty", action="store_true")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    if not os.path.exists(sample()):
        print("win16mines: SKIP - no %s; the win16-reference skill puts "
              "Microsoft's samples there, and this tree never carries them"
              % sample())
        return 77
    bad = []
    if a.marty:
        marty_leg(bad)
    else:
        qemu_leg(bad, a.keep)
    for f in bad:
        print("FAIL: " + f)
    if not bad:
        print("win16mines: %s" % ("an 8088 refuses it and a V20 plays it"
                                  if a.marty else
                                  "flagged, won, recorded, lost and saved"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
