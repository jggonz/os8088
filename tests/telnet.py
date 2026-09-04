#!/usr/bin/env python3
"""Telnet: the About panel, a session you can retry, and the drawing (70.3/70.4).

    make && python3 tests/telnet.py [--adapter cga|herc] [--machine <cfg>]

**THERE IS NO WIRE IN THIS GATE AND THAT IS DELIBERATE.** Everything below is
about the TERMINAL - which rows are drawn, where the pen is, what the About
panel leaves behind, and what the Connect button does with a session that has
ended - and none of it is about the transport. tests/socktest and
tests/brfetch already stand the cable up, at four minutes a run; standing it
up again to type into a box would make this gate slower than the thing it
tests and no more true. The screen and the state bytes are driven directly.

FOUR ASSERTIONS.

1. THE PEN AND THE ROWS FIT. The text pen must be a byte column, or every row
   falls off font_run's single-store path and the terminal flickers (70.4);
   and te_vrows rows of text must fit INSIDE the content box, because the gfx
   primitives clip to the screen and not to the window, so the ones that did
   not fit used to be painted over the dock.

2. THE ABOUT PANEL, AND WHAT IT PUTS BACK. The credits go over the terminal
   and the next click restores it BYTE FOR BYTE - which is the half that would
   break silently, because a panel that leaves a smear behind looks perfect in
   the screenshot that opened it.

3. A REFUSED SESSION CAN BE RETRIED. te_state is set to TS_ERR from outside
   the guest and Connect is pressed: the state must LEAVE the error. That is
   the reported bug (70.3) - TS_DOWN and TS_ERR sort ABOVE TS_UP, so the
   button asked the worker to CLOSE a session that had already ended, the
   worker reads that request only in its TS_UP arm, and one refusal wedged the
   app for the session. Forcing the byte is the same shape as SPEC.md 11.2's
   own guard test: the state is te_toggle's INPUT, and a test that can only
   reach it through a real refusal is a test of the network.

4. THE INCREMENTAL DRAWING IS RIGHT, INCLUDING THE SCROLL. Text is written
   into te_scr, the rows that changed are marked, and the worker letters them;
   the result must be pixel-identical to lettering all eighteen. Then the
   buffer is scrolled by hand and [te_scrl] set, so te_scrollpaint moves the
   pixels with one OSAPI_GFX_SCROLL - and that must be pixel-identical too.
   A screenshot of one build cannot say whether the pixels it did NOT draw
   were already correct; only the A/B can.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")
import dispcp                                          # noqa: E402
import os88marty                                       # noqa: E402
import os88mouse                                       # noqa: E402
import os88sym                                         # noqa: E402
from os88geom import MB_ENTSZ                          # noqa: E402

S = os88sym.linear
MACHINE = {"cga": "os8088_5150_cga", "herc": "os8088_5150_herc"}

TS_IDLE, TS_OPEN, TS_WAIT, TS_UP, TS_DOWN, TS_ERR = range(6)
TE_COLS, TE_ROWS = 64, 18


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def say(*a):
    print(*a)
    sys.stdout.flush()


def te_syms():
    """The package's own map, asserted to describe the shipped binary."""
    d = tempfile.mkdtemp()
    src, mp, out = (os.path.join(d, n) for n in ("t.asm", "t.map", "t.bin"))
    shutil.copy("apps/telnet/telnet.asm", src)
    with open(src, "a") as f:
        f.write("\n[map all %s]\n" % mp)
    subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                    "-I", "apps/telnet/", "-I", "drivers/net/",
                    "-o", out, src], check=True)
    if open(out, "rb").read() != open("build/telnet.bin", "rb").read():
        sys.exit("telnet: the mapped build is not build/telnet.bin - every "
                 "offset it names would be plausible and wrong")
    syms = {}
    for line in open(mp):
        m = re.match(r"^\s+([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+(\S+)\s*$", line)
        if m:
            syms[m.group(3)] = int(m.group(1), 16)
    return syms


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--adapter", default="cga", choices=sorted(MACHINE))
    ap.add_argument("--machine", default=None,
                    help="override the machine the --adapter picks. The two "
                         "defaults want the LICENSED IBM ROM (ibm5150_82_v4), "
                         "which a fresh container has no copy of - MartyPC "
                         "then exits at once with 'ROM set not found in ROM "
                         "set map', which reads like a broken harness rather "
                         "than a missing file. os8088_5150_cga_gla and "
                         "os8088_5150_herc_gla are the GLaBIOS twins, and "
                         "nothing here takes a timing, so they answer every "
                         "question this gate asks")
    ap.add_argument("--shot", default=None)
    a = ap.parse_args()
    fails = []
    sy = te_syms()

    with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                          machine=a.machine or MACHINE[a.adapter]) as m:
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        mono = True

        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wins = dispcp.win_list(m, S)
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "APPS")
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "TELNET.O88")
        wins2 = dispcp.win_list(m, S)
        if len(wins2) <= len(wins):
            sys.exit("telnet: TELNET.O88 did not open a window")
        tw = wins2[-1]
        rec = m.read(S("wm_wins") + tw * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
        pseg = rec[22] | (rec[23] << 8)
        tx0, ty0, tww, thh = dispcp.win_rect(m, S, tw)
        say("telnet at %04X, window %dx%d at (%d,%d)"
            % (pseg, tww, thh, tx0, ty0))

        def rb(n):
            return m.readseg(pseg, sy[n], 1)[0]

        def rw(n):
            d = m.readseg(pseg, sy[n], 2)
            return d[0] | (d[1] << 8)

        def poke(n, data):
            m.write(pseg * 16 + sy[n], data)

        def poked(pairs):
            """A set of pokes with the GUEST STOPPED.

            The worker draws on its own turn, so a screen buffer written while
            the machine runs can be drawn half-old - and the result then
            differs from run to run, which is exactly what this measured
            before the pause went in: 0 bytes one time and 631 the next, from
            an unchanged build."""
            m.pause()
            try:
                for n, data in pairs:
                    poke(n, data)
            finally:
                m.run()

        def pokew(n, v):
            poke(n, bytes([v & 255, (v >> 8) & 255]))

        def frame():
            w, h, rows = m.vram()
            return b"".join(bytes(r) for r in rows)

        def band(fb):
            """The TERMINAL's own rect out of a framebuffer.

            The comparison below is about what the credits put back, and the
            MENU BAR is not this app's to account for: opening a pull-down is
            the kernel's business and it recomposes the bar on the way out
            (SPEC.md 12.9). Comparing whole frames put those scan lines in the
            diff and made a working restore look broken."""
            w, h, rows = m.vram()
            stride = len(fb) // h
            y0 = ty0 + dispcp.TITLE_H + 1 + 22
            y1 = y0 + rw("te_vrows") * 8
            x0 = rw("te_px") // 8
            x1 = x0 + TE_COLS
            return b"".join(fb[y * stride + x0:y * stride + x1]
                            for y in range(y0, min(y1, h)))

        def rowspan(a, b):
            """Which SCAN LINES differ - a byte count says nothing about
            where, and where is the whole diagnosis here."""
            w, h, rows = m.vram()
            stride = len(a) // h
            ys = [y for y in range(h)
                  if a[y * stride:(y + 1) * stride]
                  != b[y * stride:(y + 1) * stride]]
            if not ys:
                return ""
            return " (scan lines %d..%d, %d of them)" % (ys[0], ys[-1],
                                                         len(ys))

        def settle_draw():
            """The worker draws on its own turn; two still frames is enough."""
            os88marty.settle(m)

        # --- 1: the pen, and the rows that fit ------------------------------
        px, vrows, vtop = rw("te_px"), rw("te_vrows"), rw("te_vtop")
        say("te_px %d (%s), te_vrows %d, te_vtop %d"
            % (px, "8-aligned" if px % 8 == 0 else "SKEWED", vrows, vtop))
        if px % 8:
            fails.append("the text pen is at %d, which is not a byte column - "
                         "every row falls off font_run's fast path (70.4)"
                         % px)
        need = 22 + vrows * 8
        have = thh - dispcp.TITLE_H - 1
        say("the rows need %d of %d content rows" % (need, have))
        if need > have:
            fails.append("%d rows need %d pixels in a %d-pixel content box - "
                         "the bottom is drawn outside the window (70.4)"
                         % (vrows, need, have))
        if vtop + vrows != TE_ROWS:
            fails.append("te_vtop %d + te_vrows %d is not TE_ROWS - the window "
                         "does not show the BOTTOM of the buffer"
                         % (vtop, vrows))

        # --- put some text on the screen, the way the host would ------------
        lines = [("line %02d  the quick brown fox jumps over it" % i)
                 for i in range(TE_ROWS)]
        buf = b"".join(("%-64s" % l).encode("latin-1") for l in lines)
        poked([("te_scr", buf), ("te_dr0", bytes([0, 0])),
               ("te_dr1", bytes([TE_ROWS - 1, 0]))])
        settle_draw()
        base = frame()

        # --- 1b: RESIZING moves the VIEW and never the screen (70.5) --------
        # The buffer stays 64x18 whatever the window is - the host is told
        # nothing and its idea of where a line wraps must not move - so what a
        # resize changes is te_vcols and te_vrows, and a narrower window must
        # not paint one pixel outside itself, which is what font_run clipping
        # to the SCREEN would otherwise let it do.
        def grow(dw, dh):
            x, y, w, h = dispcp.win_rect(m, S, tw)
            mo.drag(x + w - 6, y + h - 6, x + w - 6 + dw, y + h - 6 + dh)
            os88marty.settle(m)
            return dispcp.win_rect(m, S, tw)

        x0, y0, w0, h0 = dispcp.win_rect(m, S, tw)
        c0, r0 = rw("te_vcols"), rw("te_vrows")
        say("opened %dx%d: te_vcols %d, te_vrows %d" % (w0, h0, c0, r0))
        if c0 != TE_COLS:
            fails.append("the window opens showing %d of %d columns - it "
                         "should be wide enough for all of them" % (c0,
                                                                   TE_COLS))
        x1, y1, w1, h1 = grow(-140, -40)
        c1, r1 = rw("te_vcols"), rw("te_vrows")
        say("shrunk to %dx%d: te_vcols %d, te_vrows %d" % (w1, h1, c1, r1))
        if w1 >= w0 or h1 >= h0:
            fails.append("the window did not resize (%dx%d -> %dx%d) - it may "
                         "not be WF_SIZABLE" % (w0, h0, w1, h1))
        else:
            if c1 >= c0:
                fails.append("a narrower window still shows %d columns - the "
                             "view did not follow the box" % c1)
            if r1 >= r0:
                fails.append("a shorter window still shows %d rows" % r1)
            # ...and the run must END inside the window. **THIS IS AN
            # ASSERTION ABOUT THE GEOMETRY AND NOT ABOUT THE PIXELS, and it
            # is worth being exact about which.** It catches a te_vcols
            # derivation that stops following the box, which is the likely
            # regression. It does NOT prove that nothing is painted outside
            # the frame: two attempts at that both measured nothing. Counting
            # lit pixels in the band beside the window measures the DESKTOP,
            # which is a dither and is lit at any size; and comparing that
            # band across a redraw still read 0 when A/B'd against a build
            # that letters all 64 columns regardless - which says the
            # apparatus was wrong, not that the overspill is impossible. The
            # pixel-level proof is not in this gate.
            right = rw("te_px") + rw("te_vcols") * 8
            edge = x1 + w1 - 1
            say("the run ends at x=%d and the window at x=%d"
                % (right - 1, edge))
            if right - 1 > edge:
                fails.append("the text runs to x=%d and the window ends at "
                             "x=%d - font_run clips to the SCREEN, so those "
                             "columns are painted over whatever is beside it "
                             "(70.5)" % (right - 1, edge))
        x2, y2, w2, h2 = grow(140, 40)          # ...and back
        say("restored to %dx%d: te_vcols %d, te_vrows %d"
            % (w2, h2, rw("te_vcols"), rw("te_vrows")))
        if (rw("te_vcols"), rw("te_vrows")) != (c0, r0):
            fails.append("back at %dx%d the view is %dx%d and opened %dx%d - "
                         "the derivation is not reversible"
                         % (w2, h2, rw("te_vcols"), rw("te_vrows"), c0, r0))

        # --- 2: the About panel ---------------------------------------------
        # THE CELL'S X COMES OUT OF THE KERNEL'S OWN BAR TABLE, never from the
        # window's corner: the app-name cell sits after the chip and File, and
        # aiming at tx0 + 20 hit a different cell on every run - which read as
        # the About panel being erratic when the CLICK was.
        # **THE APP-NAME CELL'S TITLE IS KERNEL DATA** (SPEC.md 12.7): both its
        # strings live in menu_abstr, so MB_SEG is 0 there and reading the
        # title through the package's segment answers with our own image.
        MB_XL, MB_XR, MB_SEG = 6, 8, 10
        KSEG = 0x0060
        ax_ = None
        for c in range(7):
            b0 = S("menu_bar") + c * MB_ENTSZ
            xl = u16(m.read(b0 + MB_XL, 2))
            xr = u16(m.read(b0 + MB_XR, 2))
            t = u16(m.read(b0, 2))
            seg = u16(m.read(b0 + MB_SEG, 2)) or KSEG
            if t and xr > xl:
                nm = bytes(m.readseg(seg, t, 12)).split(b"\0")[0]
                say("  bar cell %d: %r at %d..%d" % (c, nm, xl, xr))
                if nm == b"Telnet":
                    ax_ = (xl + xr) // 2
        if ax_ is None:
            sys.exit("telnet: no app-name cell in the menu bar")
        mo.menu(ax_, 6, ax_, 26)                # ...press, onto item 0, release
        os88marty.settle(m)
        during = frame()
        say("About: te_abon %d, %d bytes of the screen changed"
            % (rb("te_abon"), sum(1 for i in range(min(len(base),
                                                       len(during)))
                                  if base[i] != during[i])))
        if rb("te_abon") != 1:
            fails.append("the About handler did not run (te_abon %d)"
                         % rb("te_abon"))
        if during == base:
            fails.append("the About panel changed no pixel")
        mo.click(tx0 + 60, ty0 + dispcp.TITLE_H + 40)   # ...dismiss it
        os88marty.settle(m)
        after = frame()
        if rb("te_abon") != 0:
            fails.append("the click did not dismiss the credits")
        b0, a0 = band(base), band(after)
        n = sum(1 for i in range(min(len(b0), len(a0))) if b0[i] != a0[i])
        say("dismissed: %d differing bytes in the terminal's own rect" % n)
        if n:
            fails.append("%d bytes differ after the credits were dismissed - "
                         "the terminal was not put back" % n)

        # --- 3: a refused session can be retried ----------------------------
        # te_state is te_toggle's INPUT; forcing it is SPEC.md 11.2's own
        # guard-test shape, and a test that could only reach TS_ERR through a
        # real refusal would be a test of the network.
        for st, name in ((TS_ERR, "TS_ERR"), (TS_DOWN, "TS_DOWN")):
            poked([("te_state", bytes([st])), ("te_want", bytes([0])),
                   ("te_msg", bytes([0, 0]))])   # ...a NEW attempt is visible
            os88marty.settle(m)
            bx = m.readseg(pseg, sy["te_btn"] + 0, 2)
            by = m.readseg(pseg, sy["te_btn"] + 2, 2)
            mo.click((bx[0] | (bx[1] << 8)) + 30, (by[0] | (by[1] << 8)) + 7)
            os88marty.settle(m)
            got, want, msg = rb("te_state"), rb("te_want"), rw("te_msg")
            say("Connect from %s -> te_state %d, te_want %d, te_msg %04X"
                % (name, got, want, msg))
            # THE STATE IS BACK AT TS_ERR BY NOW AND THAT IS CORRECT: there is
            # no link driver on this machine, so the attempt this started ran
            # and failed for want of one. What says the attempt HAPPENED is
            # te_msg naming that reason - and what says the bug is gone is
            # te_want, which the old `jae .down` set to 1 and left set.
            if want:
                fails.append("Connect from %s asked the worker to CLOSE "
                             "(te_want %d) - a finished session is not one to "
                             "close, and the worker never reads that request "
                             "outside TS_UP, so the app is wedged (70.3)"
                             % (name, want))
            if msg != sy["te_s_nodrv"]:
                fails.append("Connect from %s did not start a new attempt: "
                             "te_msg is %04X and a machine with no link "
                             "driver should be reporting te_s_nodrv (%04X)"
                             % (name, msg, sy["te_s_nodrv"]))
        poked([("te_state", bytes([TS_IDLE])), ("te_want", bytes([0]))])

        # --- 4: the incremental drawing, and the scroll ---------------------
        poked([("te_scr", buf), ("te_dr0", bytes([0, 0])),
               ("te_dr1", bytes([TE_ROWS - 1, 0]))])
        settle_draw()

        # (a) one row changed: only that row is owed, and the screen must
        #     match a full redraw of the same buffer
        row = 5
        newl = ("row %02d REWRITTEN by the incremental path" % row).ljust(64)
        poked([("te_scr", buf[:row * TE_COLS] + newl.encode("latin-1")
                + buf[(row + 1) * TE_COLS:]),
               ("te_dr0", bytes([row, 0])), ("te_dr1", bytes([row, 0]))])
        settle_draw()
        part = frame()
        poked([("te_dr0", bytes([0, 0])),
               ("te_dr1", bytes([TE_ROWS - 1, 0]))])
        settle_draw()
        full = frame()
        p0, f0 = band(part), band(full)
        n = sum(1 for i in range(min(len(p0), len(f0))) if p0[i] != f0[i])
        say("one dirty row vs a full redraw: %d differing bytes" % n)
        if n:
            fails.append("%d bytes differ after one row was drawn on its own "
                         "- the partial path is not what the full one draws"
                         % n)

        # (b) THE SCROLL. The buffer moves up one row by hand and [te_scrl] is
        #     set, which is exactly what te_scrollck leaves behind; the blit
        #     must land the same pixels as lettering every row.
        cur = (buf[:row * TE_COLS] + newl.encode("latin-1")
               + buf[(row + 1) * TE_COLS:])     # ...what is ON the screen
        scrolled = cur[TE_COLS:] + b" " * TE_COLS
        scrolled = (scrolled[:TE_COLS * (TE_ROWS - 1)]
                    + ("SCROLLED IN AT THE BOTTOM").ljust(64).encode("latin-1"))
        poked([("te_scr", scrolled),
               ("te_dr0", bytes([TE_ROWS, 0])),   # nothing owed but the
               ("te_dr1", bytes([0, 0])),         # scroll itself
               ("te_scrl", bytes([1, 0]))])
        settle_draw()
        blit = frame()
        poked([("te_dr0", bytes([0, 0])),
               ("te_dr1", bytes([TE_ROWS - 1, 0]))])
        settle_draw()
        letters = frame()
        b1, l1 = band(blit), band(letters)
        n = sum(1 for i in range(min(len(b1), len(l1))) if b1[i] != l1[i])
        say("a scroll BLIT vs lettering every row: %d differing bytes" % n)
        if n:
            fails.append("%d bytes differ between the scroll blit and a full "
                         "redraw - OSAPI_GFX_SCROLL moved the wrong rect, or "
                         "the row it opened was not lettered (70.4)" % n)

        # --- 5: THE TEXT-MODE BRACKET (SPEC.md 70.6) ------------------------
        # ^] enters it, and what proves it is a FOREIGN mode is the kernel's
        # own fsx_task latch plus the card being asked for 80x25 - not a
        # screenshot, which on a mono adapter is a plausible picture either
        # way. The terminal's own rows are then read back out of the
        # CHARACTER CELLS, which is the whole feature: a cell is one word
        # store there and a glyph cell here.
        poked([("te_scr", buf), ("te_dr0", bytes([0, 0])),
               ("te_dr1", bytes([TE_ROWS - 1, 0]))])
        settle_draw()
        m.ctrl("BracketRight")                  # ^], the escape
        os88marty.settle(m)
        # [fsx_task] is 0xFF DISARMED and the task index when a bracket is
        # up - so "armed" is != 0xFF, not != 0, which is the way round a byte
        # named like a flag invites you to read it.
        latch = m.read(S("fsx_task"), 1)[0]
        tseg = rw("te_tseg")
        say("after ^]: te_txm %d, fsx_task %02X, te_tseg %04X"
            % (rb("te_txm"), latch, tseg))
        if not rb("te_txm"):
            fails.append("^] did not enter the text bracket (te_txm 0)")
        elif latch == 0xFF:
            fails.append("te_txm is set but the kernel's fsx_task latch is "
                         "disarmed - the app thinks it is exclusive and the "
                         "kernel does not")
        else:
            if tseg not in (0xB000, 0xB800):
                fails.append("the text framebuffer is %04X, and FSXM_TEXT80 "
                             "is B000 on Hercules and B800 on the rest"
                             % tseg)
            else:
                # the cells themselves: row 0 of the terminal, centred
                TET_X0, TET_Y0, TET_COLS = 8, 2, 80
                off = (TET_Y0 * TET_COLS + TET_X0) * 2
                cells = m.readseg(tseg, off, TE_COLS * 2)
                got = bytes(cells[i] for i in range(0, len(cells), 2))
                attr = set(cells[i] for i in range(1, len(cells), 2))
                say("text row 0: %r" % got[:40].decode("latin-1"))
                if got.rstrip() != lines[0].encode("latin-1").rstrip():
                    fails.append("the text screen's row 0 is %r and the "
                                 "buffer's is %r" % (got[:24], lines[0][:24]))
                if attr != {7}:
                    fails.append("the attributes are %r and a terminal is "
                                 "grey on black" % sorted(attr))
            m.ctrl("BracketRight")              # ...and out again
            os88marty.settle(m)
            say("after ^] again: te_txm %d, fsx_task %02X"
                % (rb("te_txm"), m.read(S("fsx_task"), 1)[0]))
            if rb("te_txm"):
                fails.append("^] did not leave the bracket")
            if m.read(S("fsx_task"), 1)[0] != 0xFF:
                fails.append("the kernel's fsx_task latch is still armed - "
                             "the bracket did not come home")

        if a.shot:
            w, h, rows = m.vram()
            os88marty.write_png(a.shot, w, h, rows)
            say("wrote %s" % a.shot)

    for f in fails:
        say("FAIL: " + f)
    say("telnet: " + ("ok" if not fails else "FAILED"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
