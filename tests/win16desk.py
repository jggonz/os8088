#!/usr/bin/env python3
"""win16desk - Windows programs living with the desktop (SPEC.md 124.18,
docs/plans/WIN16-PLAN.md S12).

    python3 tests/win16desk.py            QEMU (row win16desk)
    python3 tests/win16desk.py --keep     leave QEMU and its B: for a look

*As a user, my Windows programs behave like any other os8088 program: two at
once, beside Paint, named in the Task Manager, closed by the close box or
ended when they will not close, and leaving the memory as they found it.*

B: is laid out as SHIPPED (124.18.1): WIN16.O88 in WIN16\\ with WIN16.TXT and
COPYING.W16, SYSTEM\\APPDATA\\WIN16 built, the programs at the root, and NO
DOS.O88 - the system disk's own is the one that hands each NE on, and it
finds WIN16.O88 in its folder through B:'s ASSOC.DAT.

  1. ICONS.EXE (a fixture) opens, its instance is named ICONS - what the
     Task Manager, the dock and the menu bar read (124.18.3). Its window
     shows the five system icons and its own, drawn by DrawIcon; a MessageBox
     of each kind shows its icon (124.13), and its About box the program's
     own through an SS_ICON (124.18.4) - each FOUND on the glass, pixel for
     pixel, from the art it was built from (apps/win16/icons/, mkbmp.py's
     diamond). Its close box gives back every heap claim it took: mem_tab
     is the pre-launch one.
  2. WRITERA and WRITERB (writer.c under two names) each write WIN.INI's
     [W16Merge] <own stem>=written and wait in a MessageBox, both holding a
     copy; ended B first and A second, WIN.INI on B: has BOTH keys - the
     second to end merged with what the first wrote (124.18.2).
  3. MENU.EXE and a second program run at once, two WIN16 instances, and
     Paint beside them: Windows 3.0's Minesweeper (WINMINE.EXE, where the
     win16-reference skill put it) or, without it, ICONS again. Paint closes.
  4. End Task (124.12): MENU's F5 makes it refuse WM_CLOSE; the first click
     on its close box is refused, the second raises "It has not closed. End
     it?", and Proceed ends it. The other program closes by its close box,
     and mem_tab is the pre-launch one again.

The fixtures are `make win16fixtures`'s; without them the row is a SKIP.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)
import os88geom as geom                                       # noqa: E402
from win16mines import QDrive, b, u16                         # noqa: E402
from win16fpu import Prog, open_b, pkg_syms, msgrow, gone     # noqa: E402
from win16cpu import raise_, at_loop                          # noqa: E402
from kdhand import alert_button                               # noqa: E402

sys.path.insert(0, os.path.join(ROOT, "tests", "win16"))
import mkbmp                                                  # noqa: E402
import w16icon                                                # noqa: E402

W16S_REFUSED, W16S_RUNNING, W16S_ENDED = 2, 3, 4
SYSICONS = (("hand", 32513), ("question", 32514), ("exclamation", 32515),
            ("asterisk", 32516), ("application", 32512))
BOXKEY = {"hand": "h", "question": "q", "exclamation": "e", "asterisk": "a"}
NEED = ("ICONS.EXE", "MENU.EXE", "WRITER.EXE")
MINE = "WINMINE.EXE"
WINDIR = "SYSTEM/APPDATA/WIN16"


def bdisk(out, names):
    """the shipped layout (124.18.1), DOS.O88 left to the system disk"""
    work = os.path.dirname(out)
    progs = []
    for n in names:
        src = b(os.path.join("win16", "WRITER.EXE" if n.startswith("WRITER") else n))
        dst = os.path.join(work, n)
        shutil.copyfile(src, dst)
        progs.append(dst)
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", out,
                    "--size", "1440"]
                   + ["WIN16:" + b(x) for x in ("win16.o88", "win16/WIN16.TXT",
                                                 "win16/COPYING.W16")]
                   + progs + [b("paint.o88")]
                   + ["--folder", "SYSTEM/APPDATA", "--folder", WINDIR],
                   check=True, capture_output=True)


def claims(d, sym):
    """mem_tab as a set of (segment, paragraphs, owner): the heap's state"""
    raw = d.read(sym("mem_tab"), 32 * geom.MC_SIZE)
    out = set()
    for i in range(32):
        r = raw[i * geom.MC_SIZE:(i + 1) * geom.MC_SIZE]
        seg = u16(r[geom.MC_SEG:geom.MC_SEG + 2])
        if seg:
            out.add((seg, u16(r[geom.MC_PARA:geom.MC_PARA + 2]),
                     u16(r[geom.MC_OWN:geom.MC_OWN + 2])))
    return out


def inst_name(d, sym, seg):
    """I_NAME of the live instance whose package base is seg: the name the
    Task Manager lists, the dock's tip and the menu bar's heading"""
    raw = d.read(sym("inst_tab"), geom.INST_MAX * 32)
    for i in range(geom.INST_MAX):
        r = raw[i * 32:(i + 1) * 32]
        if r[0] == 1 and u16(r[6:8]) == seg:
            return r[12:28].split(b"\0")[0].decode("latin-1")
    return None


def owned(d, ui, sym, p):
    """the program's windows - its own and any dialog it has up - newest last.
    By W_SEG, WIN16's segment: a dialog's window is WIN16's (124.13) but no
    instance's in wm_owner, which names the main window's alone"""
    out = []
    for w in ui.windows():
        if w.visible and u16(d.read(geom.winptr(d, w, sym) + geom.W_SEG, 2)) == p.seg:
            out.append(w)
    out.sort(key=lambda w: w.i != p.w.i)        # the main window first
    return out


def heap_is(d, sym, m0, what, bad):
    m1 = claims(d, sym)
    if m1 != m0:
        bad.append("%s: mem_tab is not the pre-launch one - gained %s, lost %s"
                   % (what, sorted("%04x:%dp own %04x" % c for c in m1 - m0),
                      sorted("%04x:%dp own %04x" % c for c in m0 - m1)))
    else:
        print("   mem_tab is the pre-launch one: %d claims" % len(m1))


def start(d, ui, sym, S, dw, name, bad):
    raise_(d, ui, dw)
    p = Prog(d, ui, sym, S, name)
    p.launch(dw)
    return p


def running(d, p, what, bad):
    d.wait(lambda: p.pb("w16_state") in (W16S_REFUSED, W16S_ENDED) or at_loop(p),
           300, "%s to run" % p.name)
    if p.pb("w16_state") != W16S_RUNNING:
        bad.append("%s: %s did not run: %s / %s" % (what, p.name, p.text("w16_l1"),
                                                      p.text("w16_l2")))
        return False
    return True


def fresh(ui, p):
    """p.w as it is NOW: the Win Prog.launch kept is the window as it first
    opened, before the program sized and titled it, and a click or a search
    aimed at that rectangle misses (S13's first run, ICONS and WINMINE)"""
    for w in ui.windows():
        if w.i == p.w.i:
            p.w = w
    return p.w


def close(d, ui, sym, p):
    raise_(d, ui, fresh(ui, p))
    d.click(*geom.close_xy(p.w.x, p.w.y, p.w.w, m=d, sym=sym))


def art(name, ident):
    return w16icon.read_art(os.path.join(ROOT, "apps", "win16", "icons",
                                         name + ".txt"), ident)


def glass(d, work):
    """the screen as rows of Windows palette indices (nearest colour), with
    the pointer parked at the top-right first: left where the double-click
    was, it sits on the icon at a window's client origin"""
    import shot
    d.q.mouse_to(d, d.x.sym, d.hmp, 630, 4)
    d.idle(1.0)
    path = os.path.join(work, "s.ppm")
    d.hmp("screendump %s" % path)
    w, h, rgb = shot.read_ppm(path)
    cache, out = {}, []
    for y in range(h):
        row = []
        for x in range(w):
            c = rgb[(y * w + x) * 3:(y * w + x) * 3 + 3]
            if c not in cache:
                cache[c] = min(range(16), key=lambda i: sum(
                    (a - b_) ** 2 for a, b_ in zip(c, w16icon.PAL[i])))
            row.append(cache[c])
        out.append(row)
    return out


def find(scr, win, rows):
    """where in the window `rows` is drawn, see-through pixels skipped"""
    pts = [(x, y, c) for y, r in enumerate(rows) for x, c in enumerate(r)
           if c is not None]
    for oy in range(win.y, win.y + win.h - 31):
        for ox in range(win.x, win.x + win.w - 31):
            if all(scr[oy + y][ox + x] == c for x, y, c in pts):
                return ox - win.x, oy - win.y
    return None


def look(d, work, win, rows, what, bad):
    at = find(glass(d, work), win, rows)
    if at is None:
        bad.append("1: %s is not on the glass in %r" % (what, win.title))
    else:
        print("   %s at (%d,%d) in %r" % (what, at[0], at[1], win.title))


def dialog(d, ui, sym, p, key, what):
    """press a key in the program's window and wait for the window it opens"""
    for attempt in range(2):        # a key straight after Enter can be lost
        raise_(d, ui, fresh(ui, p))
        d.hmp("sendkey %s" % key)
        try:
            d.wait(lambda: len(owned(d, ui, sym, p)) > 1, 15, what)
            d.idle(2.0)
            return owned(d, ui, sym, p)[-1]
        except RuntimeError:
            if attempt:
                raise


def ini(img):
    r = subprocess.run(["mtype", "-i", img, "::/%s/WIN.INI" % WINDIR],
                       capture_output=True)
    return r.stdout.decode("latin-1") if r.returncode == 0 else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    missing = [n for n in NEED if not os.path.exists(b(os.path.join("win16", n)))]
    if missing:
        print("win16desk: SKIP - no %s in build/win16/ (make win16fixtures)"
              % ", ".join(missing))
        return 77
    second = MINE if os.path.exists(b(os.path.join("win16", MINE))) else "ICONS.EXE"
    names = ["ICONS.EXE", "MENU.EXE", "WRITERA.EXE", "WRITERB.EXE"]
    if second == MINE:
        names.append(MINE)
    else:
        print("   (no build/win16/%s: ICONS stands beside Paint)" % MINE)

    import os88ui
    import xmcheck
    sym = xmcheck.sym
    S = pkg_syms()
    bad = []
    work = tempfile.mkdtemp(prefix="w16d-")     # short: a QMP socket path
    try:
        bimg = os.path.join(work, "src.img")
        bdisk(bimg, names)
        d = QDrive(work, bimg, a.keep)
        ui = os88ui.UI(d, mouse=object(), sym=sym, verbose=False)
        dw = open_b(d, ui, sym)
        m0 = claims(d, sym)
        print("0. B: open, %d heap claims before any Windows program" % len(m0))

        # 1. one program, its name, its close box, the heap
        p = start(d, ui, sym, S, dw, "ICONS.EXE", bad)
        if running(d, p, "1", bad):
            d.idle(3.0)
            nm = inst_name(d, sym, p.seg)
            print("1. ICONS runs in WIN16 at %04x, its instance named %r" % (p.seg, nm))
            if nm != "ICONS":
                bad.append("1: the instance is named %r, not ICONS (124.18.3)" % nm)
            fresh(ui, p)
            for name, ident in SYSICONS:
                look(d, work, p.w, art(name, ident), "DrawIcon's " + name, bad)
            look(d, work, p.w, mkbmp.diamond(), "DrawIcon's own icon", bad)
            for name, key in BOXKEY.items():
                box = dialog(d, ui, sym, p, key, "the %s MessageBox" % name)
                look(d, work, box, art(name, dict(SYSICONS)[name]),
                     "MessageBox's " + name, bad)
                d.key("Enter")
                d.wait(lambda: len(owned(d, ui, sym, p)) == 1, 30,
                       "the %s MessageBox to close" % name)
                d.idle(2.0)
            box = dialog(d, ui, sym, p, "d", "the About box")
            look(d, work, box, mkbmp.diamond(), "the About box's SS_ICON", bad)
            d.key("Enter")
            d.wait(lambda: len(owned(d, ui, sym, p)) == 1, 30,
                   "the About box to close")
            d.idle(2.0)
            close(d, ui, sym, p)
            d.wait(lambda: gone(ui, p), 60, "ICONS to close")
            d.idle(3.0)
            heap_is(d, sym, m0, "1 (ICONS closed)", bad)

        # 2. two programs writing WIN.INI at once
        row = msgrow()
        wa = start(d, ui, sym, S, dw, "WRITERA.EXE", bad)
        d.wait(lambda: wa.waiting_msgbox(row), 120, "WRITERA's MessageBox")
        wb = start(d, ui, sym, S, dw, "WRITERB.EXE", bad)
        d.wait(lambda: wb.waiting_msgbox(row), 120, "WRITERB's MessageBox")
        d.idle(3.0)
        print("2. WRITERA (%04x) and WRITERB (%04x) both wait in a MessageBox"
              % (wa.seg, wb.seg))
        for w in (wb, wa):                       # B ends first, A merges last
            box = owned(d, ui, sym, w)
            raise_(d, ui, box[-1] if box else w.w)
            d.key("Enter")
            d.wait(lambda: w.pb("w16_state") == W16S_ENDED or gone(ui, w), 60,
                   "%s to end" % w.name)
            d.idle(3.0)
        text = ini(d.p.b) or ""      # the copy QEMU writes, not src.img
        print("   WIN.INI: " + text.strip().replace("\r\n", " | ").replace("\n", " | "))
        for k in ("WRITERA=written", "WRITERB=written"):
            if k not in text:
                bad.append("2: WIN.INI has no %s - the second to end wrote back "
                           "its own copy instead of merging (124.18.2)" % k)

        # 3. two Windows programs at once, and Paint beside them
        pm = start(d, ui, sym, S, dw, "MENU.EXE", bad)
        ok3 = running(d, pm, "3", bad)
        p2 = start(d, ui, sym, S, dw, second, bad)
        ok3 = running(d, p2, "3", bad) and ok3
        raise_(d, ui, dw)
        before = set(w.i for w in ui.windows())
        i, _ = ui.entry("PAINT.O88", dw)
        d.dblclick(*ui.row_xy(dw, i - ui.scroll(dw)))
        box = {}

        def painted():
            for w in ui.windows():
                if w.i not in before and w.visible:
                    box["w"] = w
                    return True
            return False
        d.wait(painted, 60, "Paint's window")
        d.idle(3.0)
        n1, n2 = inst_name(d, sym, pm.seg), inst_name(d, sym, p2.seg)
        print("3. %s (%04x) and %s (%04x) at once, Paint beside them: %s"
              % (n1, pm.seg, n2, p2.seg, ", ".join(w.title for w in ui.windows()
                                                     if w.visible)))
        if ok3 and (pm.seg == p2.seg or pm.pb("w16_state") != W16S_RUNNING
                    or p2.pb("w16_state") != W16S_RUNNING):
            bad.append("3: the two programs are not two running instances")
        if n1 != "MENU" or n2 != second.split(".")[0]:
            bad.append("3: the instances are named %r and %r" % (n1, n2))
        pw = box["w"]
        raise_(d, ui, pw)
        d.click(*geom.close_xy(pw.x, pw.y, pw.w, m=d, sym=sym))
        d.wait(lambda: all(w.i != pw.i or not w.visible for w in ui.windows()),
               60, "Paint to close")
        d.idle(3.0)
        for w in (pm, p2):
            if w.pb("w16_state") != W16S_RUNNING:
                bad.append("3: %s stopped when Paint closed" % w.name)

        # 4. End Task, then the close box, then the heap
        raise_(d, ui, fresh(ui, pm))
        d.hmp("sendkey f5")                      # MENU refuses WM_CLOSE now
        d.idle(3.0)
        close(d, ui, sym, pm)
        d.idle(3.0)
        if gone(ui, pm):
            bad.append("4: MENU closed on a WM_CLOSE it refused")
        else:
            base = pm.seg << 4
            close(d, ui, sym, pm)
            d.wait(lambda: u16(d.read(base + S["os88ui_awin"], 2)) != 0, 30,
                   "the End Task question")
            d.idle(2.0)
            print("4. MENU refused WM_CLOSE; the second click asks to end it")
            d.click(*alert_button(d, base, S, 1))         # Proceed
            d.wait(lambda: gone(ui, pm), 60, "MENU to be ended")
            d.idle(3.0)
        close(d, ui, sym, p2)
        d.wait(lambda: gone(ui, p2), 60, "%s to close" % second)
        d.idle(3.0)
        heap_is(d, sym, m0, "4 (everything closed)", bad)
        if not a.keep:
            d.p.close()
    finally:
        if not a.keep:
            shutil.rmtree(work, ignore_errors=True)
        else:
            print("kept: %s" % work)
    for f in bad:
        print("FAIL: " + f)
    if not bad:
        print("win16desk: two programs at once beside Paint, named, closed and "
              "ended, WIN.INI merged, and the heap as it was")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
