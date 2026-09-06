#!/usr/bin/env python3
"""Telnet: the 80x25 screen, both renderers, and a session you can retry.

    make && python3 tests/telnet.py [--adapter cga|herc] [--machine <cfg>]

**THERE IS NO WIRE IN THIS GATE AND THAT IS DELIBERATE.** Everything below is
about the TERMINAL - which rows are drawn, where the pen is, what the About
panel leaves behind, and what the Connect button does with a session that has
ended - and none of it is about the transport. tests/socktest and
tests/brfetch already stand the cable up, at four minutes a run; standing it
up again to type into a box would make this gate slower than the thing it
tests and no more true. The screen and the state bytes are driven directly.

**THE ADAPTER HERE IS 1bpp AND THAT IS THE POINT OF IT.** After SPEC.md 70.8
the screen is a character AND AN ATTRIBUTE, and the pen is not read on a mono
adapter at all (5.4.2.2) - so the polarity goes into the composed BAND
instead, and that arm is what CGA and Hercules exercise. The COLOUR arm is
tests/telpen.py, which needs a VGA and `fbuf`.

SEVEN ASSERTIONS. The first four are this gate's originals, carried across the
80x25 rewrite; the last three arrived with it.

1. THE PEN AND THE ROWS FIT. The text pen must be a byte column, or a run's x
   is not a multiple of 8 and OSAPI_GFX_BLIT1 refuses the band outright
   (5.4.2); and te_vrows rows of text must fit INSIDE the content box, because
   the gfx primitives clip to the screen and not to the window, so the ones
   that did not fit used to be painted over the dock.

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

4. THE INCREMENTAL DRAWING IS RIGHT, INCLUDING THE SCROLL. Cells are written
   into te_scr, the rows that changed are marked in te_drb, and the worker
   composes and blits them; the result must be pixel-identical to drawing all
   twenty-five. Then the buffer is scrolled by hand and [te_scrl] set, so
   te_scrollpaint moves the pixels with one OSAPI_GFX_SCROLL - and that must
   be pixel-identical too. A screenshot of one build cannot say whether the
   pixels it did NOT draw were already correct; only the A/B can.

5. THE POLARITY RULE (70.8.4). A cell whose background is not black and whose
   foreground is 0 or 8 is drawn INVERSE - lit paper, dark glyph - and every
   other pair is a lit glyph on dark ground. That is the only rule that keeps
   a board's highlighted menu item from rendering as nothing at all in one
   bit, and it is asserted by COUNTING LIT PIXELS in the two rows, because the
   two are each other's photographic negative and nothing else on the screen
   is.

6. FULL SCREEN IS THE BOARD'S OWN SCREEN, AND IT SCROLLS (70.8.7/70.8.8).
   te_scr maps 1:1 onto text VRAM - all 25 rows, no centring, no status line -
   so the assertion is a memcmp of 4,000 bytes rather than a screenshot. Then
   a scroll debt is left the way te_scroll1 leaves one, and VRAM must FOLLOW
   THE BUFFER. It did not: te_tx_owed zeroed [te_scrl] without moving
   anything and te_scrollck marked exactly one row, so rows 0..23 kept
   pre-scroll text for the rest of the session and the bottom row was
   rewritten over and over. A board's output is one long scroll and it was
   legible on one line.

7. THE KEPT WORKER IS NOT PARKED IN THE BRACKET (70.8.8). te_show took the
   gfx lock ELEVEN INSTRUCTIONS BEFORE it tested [te_txm], and the bracket
   holds that lock for its whole life (53.6) - so the first byte to arrive
   after entering full screen parked the task that owns the socket until ^].
   [te_dirty] is the probe: te_step zeroes it at the top of every pass, so a
   byte poked into it while the screen is up comes back zero if the worker is
   turning and stays one if it is not.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import dispcp                                          # noqa: E402
import os88marty                                       # noqa: E402
import os88mouse                                       # noqa: E402
import os88sym                                         # noqa: E402
from os88geom import MB_ENTSZ                          # noqa: E402

S = os88sym.linear
MACHINE = {"cga": "os8088_5150_cga", "herc": "os8088_5150_herc"}

TS_IDLE, TS_OPEN, TS_WAIT, TS_UP, TS_DOWN, TS_ERR = range(6)
TE_COLS, TE_ROWS = 80, 25
TE_CELLS = TE_COLS * TE_ROWS
DRB_ALL = bytes([0xFF, 0xFF, 0xFF, 0x01])   # 25 bits (SPEC.md 70.8.1)
DRB_NONE = bytes(4)


def cells(rows):
    """(text, attribute) a row -> the 80x25 char/attr buffer."""
    out = bytearray()
    for r in range(TE_ROWS):
        t, at = rows[r] if r < len(rows) else ("", 0x07)
        t = (t + " " * TE_COLS)[:TE_COLS]
        for ch in t:
            out += bytes([ord(ch) & 0xFF, at])
    return bytes(out)


def drb(*rows):
    """...and the bitmap naming exactly those rows."""
    b = bytearray(4)
    for r in rows:
        b[r >> 3] |= 1 << (r & 7)
    return bytes(b)


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
            # THE ORIGIN IS READ OUT OF THE GUEST rather than derived from
            # the window's corner: te_oy is what OSAPI_WM_CONTENT answered and
            # TE_TOPY is the package's own constant, so this is the pen the
            # renderer actually used. `frame + TITLE_H + 1` is one pixel out.
            y0 = rw("te_oy") + 22
            y1 = y0 + rw("te_vrows") * 8
            # ...AND THE X RANGE IS IN PIXELS. `vram` answers one byte per
            # PIXEL, and this sliced it by BYTE COLUMN - so the comparison
            # covered the leftmost ten cells of the terminal and called that
            # the terminal. It is the whole width now.
            x0 = rw("te_px")
            x1 = x0 + rw("te_vcols") * 8
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

        def park():
            """THE POINTER IS PARKED BEFORE ANY FRAME THAT IS COMPARED.

            The arrow is drawn INTO the framebuffer (SPEC.md 7.1), so one
            resting on the terminal is a box of about thirty differing pixels
            - which is exactly what the About panel's restore measured before
            this existed, and it reads like a smear rather than like a
            pointer. The menu bar is above the terminal's own rect, so a
            pointer parked there is outside every comparison below."""
            mo.to(2, 2)
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
        buf = cells([(l, 0x07) for l in lines])
        poked([("te_scr", buf), ("te_drb", DRB_ALL)])
        settle_draw()
        base = frame()

        # --- 1b: RESIZING moves the VIEW and never the screen (70.5) --------
        # The buffer stays 64x18 whatever the window is - the host is told
        # nothing and its idea of where a line wraps must not move - so what a
        # resize changes is te_vcols and te_vrows, and a narrower window must
        # not paint one pixel outside itself, which is what font_run clipping
        # to the SCREEN would otherwise let it do.
        # THE TARGET IS CLAMPED TO THE SCREEN, and that is new with 80x25: the
        # window opens as wide as the desktop allows (70.8.10), so on a
        # 640-wide one the grow box is already at the right edge and a drag to
        # `edge + 140` is a point the pointer cannot be moved to at all. The
        # kernel clamps the SIZE either way, so dragging to the last column
        # restores the width the window opened at.
        vw = u16(m.read(S("vid_w"), 2))
        vh = u16(m.read(S("vid_h"), 2))

        def grow(dw, dh):
            x, y, w, h = dispcp.win_rect(m, S, tw)
            gx, gy = x + w - 6, y + h - 6
            mo.drag(gx, gy, max(0, min(gx + dw, vw - 1)),
                    max(0, min(gy + dh, vh - 1)))
            os88marty.settle(m)
            return dispcp.win_rect(m, S, tw)

        x0, y0, w0, h0 = dispcp.win_rect(m, S, tw)
        c0, r0 = rw("te_vcols"), rw("te_vrows")
        say("opened %dx%d: te_vcols %d, te_vrows %d" % (w0, h0, c0, r0))
        # THE WINDOW OPENS AS WIDE AS THE DESKTOP ALLOWS, UP TO EIGHTY
        # (70.8.10). On a 640-pixel screen it cannot reach eighty - the
        # aligned pen and the padding take the rest - and that is ACCEPTED
        # rather than solved: full screen is where a board is used and it is
        # one keystroke away. So the assertion is the DERIVATION, which is
        # checkable at any size, and it is made again after each resize below.
        def viewfits():
            """What te_vcols/te_vrows must be for the live content box."""
            ox, cw, chh, px_ = (rw("te_ox"), rw("te_cw"), rw("te_chh"),
                                rw("te_px"))
            c = max(1, min(TE_COLS, (ox + cw - px_) // 8))
            r = max(1, min(TE_ROWS, (chh - 22 - 10) // 8))
            return c, r

        wantc, wantr = viewfits()
        say("the box can show %dx%d and the view is %dx%d"
            % (wantc, wantr, c0, r0))
        if (c0, r0) != (wantc, wantr):
            fails.append("the window opened showing %dx%d and its content box "
                         "can show %dx%d - the view did not follow the box"
                         % (c0, r0, wantc, wantr))
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
        c2, r2 = rw("te_vcols"), rw("te_vrows")
        wantc, wantr = viewfits()
        say("restored to %dx%d: te_vcols %d, te_vrows %d (the box can show "
            "%dx%d)" % (w2, h2, c2, r2, wantc, wantr))
        # **NOT AN EXACT ROUND TRIP, AND DELIBERATELY NOT ASSERTED AS ONE.**
        # The window opens at the desktop's own width now, so the grow box
        # starts ON the right edge and the drag back is clamped there - the
        # window comes home a few pixels narrower and no drag can do better.
        # What must hold is the same thing that held on the way out: the view
        # is what the box can show.
        if (c2, r2) != (wantc, wantr):
            fails.append("back at %dx%d the view is %dx%d and the box can "
                         "show %dx%d - the derivation stopped following the "
                         "box" % (w2, h2, c2, r2, wantc, wantr))
        if c2 < c1 or r2 < r1:
            fails.append("growing the window back did not widen the view "
                         "(%dx%d shrunk, %dx%d grown)" % (c1, r1, c2, r2))

        # --- ...and BASE IS RE-TAKEN HERE, after the resizes above.
        # It used to be captured before them, which was harmless while the two
        # drags were an exact round trip. They are not one any more (see
        # `grow`), so a frame from before the dance is a frame of a WIDER
        # window and every byte past the new right edge is in the diff.
        poked([("te_scr", buf), ("te_drb", DRB_ALL)])
        settle_draw()
        park()
        base = frame()

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
        park()
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
        poked([("te_scr", buf), ("te_drb", DRB_ALL)])
        settle_draw()

        # (a) one row changed: only that row is owed, and the screen must
        #     match a full redraw of the same buffer
        row = vtop + 3              # ...INSIDE the view: a short window shows
                                    # the last te_vrows rows, and a row above
                                    # te_vtop is correctly never drawn
        newr = list((l, 0x07) for l in lines)
        newr[row] = ("row %02d REWRITTEN by the incremental path" % row, 0x07)
        cur = cells(newr)
        poked([("te_scr", cur), ("te_drb", drb(row))])
        settle_draw()
        part = frame()
        poked([("te_drb", DRB_ALL)])
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
        scr = newr[1:] + [("SCROLLED IN AT THE BOTTOM", 0x07)]
        scrolled = cells(scr)
        poked([("te_scr", scrolled),
               ("te_drb", drb(TE_ROWS - 1)),      # what te_scroll1 leaves:
                                                  # the bitmap shifted (nothing
                                                  # was owed) and the row it
                                                  # opened marked
               ("te_scrl", bytes([1, 0]))])
        settle_draw()
        blit = frame()
        poked([("te_drb", DRB_ALL)])
        settle_draw()
        letters = frame()
        b1, l1 = band(blit), band(letters)
        n = sum(1 for i in range(min(len(b1), len(l1))) if b1[i] != l1[i])
        say("a scroll BLIT vs lettering every row: %d differing bytes" % n)
        if n:
            fails.append("%d bytes differ between the scroll blit and a full "
                         "redraw - OSAPI_GFX_SCROLL moved the wrong rect, or "
                         "the row it opened was not lettered (70.4)" % n)

        # --- 5: THE 1bpp POLARITY RULE (SPEC.md 70.8.4) ---------------------
        # A cell whose background is not black and whose foreground is 0 or 8
        # is drawn INVERSE - lit paper, dark glyph - and every other pair is a
        # lit glyph on dark ground. It is the only rule that keeps a board's
        # highlighted menu item from rendering as nothing at all in one bit,
        # and the two rows below are each other's negative: the SAME text, the
        # same glyphs, and the lit counts have to swap. Counting is the right
        # instrument here because nothing else on this screen is a negative of
        # anything, so a count that did not swap cannot be a coincidence.
        POL = "INVERSE and normal, the same forty characters in each"
        prow = [(("", 0x07)) for _ in range(TE_ROWS)]
        prow[vtop] = (POL, 0x30)        # black on cyan: bg != 0, fg 0
        prow[vtop + 1] = (POL, 0x03)    # cyan on black: the plain case
        poked([("te_scr", cells(prow)), ("te_drb", DRB_ALL),
               ("te_scrl", bytes([0, 0])), ("te_cvis", bytes([0]))])
        settle_draw()
        w_, h_, vr = m.vram()
        py = rw("te_oy") + 22
        x0 = rw("te_px")
        x1 = x0 + rw("te_vcols") * 8

        def litrow(r):
            return sum(sum(vr[py + (r - vtop) * 8 + k][x0:x1])
                       for k in range(8))

        inv, norm = litrow(vtop), litrow(vtop + 1)
        span = (x1 - x0) * 8
        say("polarity: the INVERSE row lights %d of %d, the normal one %d"
            % (inv, span, norm))
        if inv <= span * 3 // 4:
            fails.append("a cell of black on cyan lit %d of %d pixels - it "
                         "must be drawn INVERSE, lit paper and dark glyph, or "
                         "a board's highlighted menu item is nothing at all "
                         "in one bit (70.8.4)" % (inv, span))
        if norm >= span // 4:
            fails.append("a cell of cyan on black lit %d of %d pixels - a "
                         "colour on black is a lit glyph on dark ground and "
                         "is not inverted (70.8.4)" % (norm, span))
        poked([("te_cvis", bytes([1]))])

        # --- 6: THE TEXT-MODE BRACKET (SPEC.md 70.6/70.8.7) -----------------
        # ^] enters it, and what proves it is a FOREIGN mode is the kernel's
        # own fsx_task latch plus the card being asked for 80x25 - not a
        # screenshot, which on a mono adapter is a plausible picture either
        # way. The terminal's own rows are then read back out of the
        # CHARACTER CELLS, which is the whole feature: a cell is one word
        # store there and a glyph cell here.
        poked([("te_scr", buf), ("te_drb", DRB_ALL)])
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
                # --- 6a: THE BUFFER MAPS 1:1 (SPEC.md 70.8.7) ---------------
                # 80x25 against 80x25, no centring, no status line: a row is
                # at r*160 and te_scr's cell IS the cell in VRAM, so this is a
                # memcmp of 4,000 bytes rather than a screenshot. On MDA the
                # ATTRIBUTES are mapped (70.8.9), so the characters are what
                # both adapters can be asked about.
                # THE LAST TWELVE CELLS OF ROW 24 ARE THE LEAVE HINT and are
                # excluded from the memcmp by NAME rather than by count: it is
                # drawn once on entry, in the inverse attribute, and the host
                # is allowed to overwrite it (70.8.7) - so a hint that survives
                # this pass is the assertion, and a hint that never appeared
                # is a different failure from a row that did not.
                HINT = b" ^] to leave"
                HOFF = TE_CELLS - len(HINT)

                def cmpvram(tag, want):
                    v = m.readseg(tseg, 0, TE_CELLS * 2)
                    ch = bytes(v[i] for i in range(0, len(v), 2))
                    at = bytes(v[i + 1] for i in range(0, len(v), 2))
                    nd = sum(1 for i in range(HOFF) if ch[i] != want[i])
                    stale = sum(1 for r in range(TE_ROWS - 1)
                                if ch[r * 80:(r + 1) * 80]
                                != want[r * 80:(r + 1) * 80])
                    say("  %s: %d of %d cells differ, %d whole rows above the "
                        "bottom stale" % (tag, nd, HOFF, stale))
                    say("    row 0 : %r" % ch[:40].decode("latin-1"))
                    say("    row 24: %r" % ch[24 * 80:24 * 80 + 40]
                        .decode("latin-1"))
                    return nd, stale, ch, at

                want = bytes(buf[i] for i in range(0, len(buf), 2))
                nd, stale, chs, attrs = cmpvram("text VRAM vs the buffer", want)
                if nd:
                    fails.append("%d of %d character cells differ between the "
                                 "buffer and text VRAM - the two are supposed "
                                 "to be one layout (70.8.7)" % (nd, HOFF))
                if chs[HOFF:] != HINT:
                    fails.append("the bottom right of the screen is %r and "
                                 "the way out is drawn there once on entry "
                                 "(70.8.7)" % chs[HOFF:])
                seen = set(attrs[:HOFF])
                if seen != {7}:
                    fails.append("the attributes are %r and 0x07 in, grey on "
                                 "black out, is what both mappings answer for "
                                 "it (70.8.9)" % sorted(seen))
                if set(attrs[HOFF:]) != {0x70}:
                    fails.append("the leave hint's attribute is %r and it is "
                                 "drawn inverse" % sorted(set(attrs[HOFF:])))
                # ...and now the HOST overwrites it, which is what 70.8.7 says
                # it is allowed to do. Row 24 is marked and re-emitted from the
                # buffer, so the scroll below has nothing of the hint's to
                # carry up the screen and every one of the 2,000 cells is in
                # the comparison.
                poked([("te_drb", drb(TE_ROWS - 1))])
                os88marty.settle(m)
                HOFF = TE_CELLS

                # --- 6b: AND IT SCROLLS (SPEC.md 70.8.8, defect 1) ----------
                # The state te_scroll1 leaves behind: the buffer moved up one,
                # the bitmap shifted (nothing was owed) and the row it opened
                # marked, and [te_scrl] standing at 1. VRAM must FOLLOW.
                sc = [(l, 0x07) for l in lines][1:]
                sc.append(("SCROLLED IN FULL SCREEN", 0x07))
                sbuf = cells(sc)
                poked([("te_scr", sbuf), ("te_drb", drb(TE_ROWS - 1)),
                       ("te_scrl", bytes([1, 0]))])
                os88marty.settle(m)
                want2 = bytes(sbuf[i] for i in range(0, len(sbuf), 2))
                nd2, stale, _, _ = cmpvram("after a scroll debt", want2)
                if nd2:
                    fails.append("%d of %d cells differ after a scroll debt, "
                                 "and %d whole rows above the bottom still "
                                 "hold pre-scroll text: the debt was zeroed "
                                 "without moving anything and only the row it "
                                 "opened was re-emitted (70.8.8)"
                                 % (nd2, HOFF, stale))
                if rw("te_scrl"):
                    fails.append("[te_scrl] is %d after the pass - the debt "
                                 "was not spent" % rw("te_scrl"))

                # --- 7: THE KEPT WORKER IS TURNING (70.8.8, defect 2) -------
                # te_step zeroes [te_dirty] at the top of every pass, so a 1
                # poked in while the bracket is up comes back 0 if the worker
                # got a turn - and stays 1 if te_show parked it on the gfx
                # lock the bracket holds (53.2/53.6).
                poked([("te_dirty", bytes([1]))])
                os88marty.settle(m)
                d = rb("te_dirty")
                say("the worker in the bracket: [te_dirty] poked to 1, read "
                    "back %d" % d)
                if d:
                    fails.append("[te_dirty] is still 1 after the bracket ran "
                                 "- the kept worker is parked in te_show's "
                                 "OSAPI_GFX_LOCK, which the bracket holds for "
                                 "its whole life, and the session is frozen "
                                 "until ^] (70.8.8, SPEC.md 53.2)")
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
