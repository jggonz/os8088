#!/usr/bin/env python3
"""doswtty - how fast a program PRINTS in a DOS window (SPEC.md 96.54.14).

tests/dostrap/dwtty.asm puts text on the screen three ways, each started by a
key and counted into PSP:0103h when it is done, so guest seconds (MartyPC's
cycle counter, 4.77 MHz) from the key to the count are its time:

    9   100 lines through INT 21h AH=09h - a command-line utility, a scroll
        a line
    e   the same lines through INT 10h AH=0Eh, the teletype a character at
        a time
    b   20 rows of 66 characters as a compiled BASIC PRINT puts them:
        AH=02h, AH=09h with CX = 1, AH=02h - three calls a character

Each leg runs in the bracket (the whole machine, the ROM's own teletype onto
the Hercules's text page) and then in a window, and the row holds the
window's speed as a share of the bracket's at a floor per leg.

**AND THE GLASS AFTER EACH WINDOWED LEG IS THE FULL REPAINT'S** - doswcell's
leg 3, after a run of output rather than a cell: every row is marked whole
from the host and repainted, and the band must not change by a pixel. It is
what 96.54.14's speed rests on: a glyph marks only its own cell and the
caret's cells are con_curmove's, so a caret the band's scroll carried up and
nothing marked would be a stale underline here (con_scrollpaint's [con_lcur]
move taken out: 'b' passes and '9' and 'e' do not).

MEASURED, the window's speed as a share of the bracket's, before 96.54.14
(3241006c) and after, several boots each:

    leg   Hercules        CGA            VGA XT
    9     44% -> 96-114%  46% -> 115%    170% -> 430%
    e     39% -> 73-86%   41% -> 92-94%  146% -> 350%
    b     22% -> 44-49%   19% -> 39-43%  120% -> 270%

(the VGA BIOS's own teletype is the slow one on that machine).

**Break it on purpose** (docs/WRITING-TESTS.md 1): every leg reads under its
floor on the tree before 96.54.14 - three marks a character, a mark and two
host crossings per cursor move, and the band scrolled once a line - and its
glass after leg 9 was 3,318 pixels off a full repaint. With con_scrollpaint's
[con_lcur] move taken out, leg l reads 16 stale pixels (the caret's two
scanlines). The scroll race itself is intermittent: 4 of 10 runs before the
fix, so a green row is not proof of it.

    python3 tests/doswtty.py           (DOSWTTY_MACHINE=os8088_xt_vga, ...;
                                        DOSWTTY_SHOTS=<prefix> writes the two
                                        captures of a glass that differed)
"""

import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dosmap                                                  # noqa: E402
import os88marty                                               # noqa: E402
import os88ui                                                  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SYS = os.path.join(ROOT, "build", "os8088-360.img")
PROBE = os.path.join(ROOT, "tests", "dostrap", "dwtty.asm")
HZ = 4772727.0
MACHINE = os.environ.get("DOSWTTY_MACHINE", "os8088_5150_herc_gla")
LEGS = (("9", "INT 21h AH=09h", 80.0),
        ("l", "five bare LFs", None),
        ("e", "INT 10h AH=0Eh", 65.0),
        ("b", "AH=02h/09h/02h", 35.0))
LIMIT = 120.0                       # guest seconds a leg may take at all
SNOW = 8 if "cga" in MACHINE else 0  # pixels of CGA snow a capture may carry
                                    # (cell_at): a stale caret is 16, a stale
                                    # line hundreds


def fail(msg):
    print("doswtty: FAIL: %s" % msg)
    sys.exit(1)


def main():
    if not os.path.exists(SYS):
        fail("%s is missing - a plain `make` builds it" % SYS)
    tmp = tempfile.mkdtemp(prefix="doswtty")
    try:
        com = os.path.join(tmp, "DWTTY.COM")
        subprocess.run(["nasm", "-f", "bin", "-w+error", "-o", com, PROBE],
                       check=True)
        disk = os.path.join(tmp, "dwtty360.img")
        subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                     "os88disk.py"),
                        "-o", disk, "--size", "360", com], check=True,
                       stdout=subprocess.DEVNULL)
        run(disk)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("doswtty: ok")


def band_diff(a, b, x0, y0, vc, vr):
    """The pixels differing inside the band between two fbuf captures."""
    w, h, pa = a
    pb = b[2]
    out = []
    for y in range(max(0, y0), min(h, y0 + vr * 8)):
        row = y * w * 3
        lo, hi = row + max(0, x0) * 3, row + min(w, x0 + vc * 8) * 3
        if pa[lo:hi] == pb[lo:hi]:
            continue
        for x in range(max(0, x0), min(w, x0 + vc * 8)):
            i = row + x * 3
            if pa[i:i + 3] != pb[i:i + 3]:
                out.append((x, y))
    return out


def cell_at(px):
    """The top left of the one solid 8x8 cell among `px`, which on CGA may
    carry a stray pixel or two of SNOW: MartyPC renders the bus contention a
    real CGA shows, so a capture is the glass AND whatever the card's reads
    lost while the CPU wrote it."""
    pts = set(px)
    for x, y in sorted(px, key=lambda p: (p[1], p[0])):
        if sum((x + i, y + j) in pts for i in range(8) for j in range(8)) == 64:
            return x, y
    return None


def run(disk):
    with os88ui.boot(SYS, apps=disk, machine=MACHINE) as ui:
        m = ui.m
        dm = dosmap.package()

        def cyc():
            return int(m.status()["cycles"])

        def done():
            base = dosmap.lin(m, dosmap.instance(m))
            psp = int.from_bytes(m.read(base + dm["dos_ldpsp"], 2), "little")
            if not psp or m.read(psp * 16 + 0x100, 3) != b"\xeb\x03\x90":
                return None
            return int.from_bytes(m.read(psp * 16 + 0x103, 2), "little")

        def leg(key, n):
            os88marty.pace(m, 1)
            c0 = cyc()
            m.type_text(key)
            while done() != n + 1:
                if (cyc() - c0) / HZ > LIMIT:
                    fail("leg %r never finished (%s guest s)" % (key, LIMIT))
                os88marty.guest_sleep(m, 0.05)
            return (cyc() - c0) / HZ

        def legs(tag):
            n = None
            for _ in range(200):
                n = done()
                if n == 0:
                    break
                os88marty.guest_sleep(m, 0.1)
            if n != 0:
                fail("%s: DWTTY.COM is not waiting for a key" % tag)
            return [leg(k, i) for i, (k, _, _) in enumerate(LEGS)]

        def addr(name):
            return dosmap.lin(m, dosmap.instance(m)) + dm[name]

        def word(name):
            return int.from_bytes(m.read(addr(name), 2), "little")

        def until(cond, what):
            try:
                os88marty.until(m, lambda _m: cond(), what, poll=0.25,
                                limit=10)
            except os88marty.MartyError:
                fail("%s never happened (marks %s, scroll %d, dirty %d)"
                     % (what, m.read(addr("con_drb"), 4).hex(),
                        word("con_scrl"), m.read(addr("dvw_dirty"), 1)[0]))

        def caught_up():
            """The incremental paint of what the program printed is done:
            nothing marked, no scroll owed, no paint waiting to be posted."""
            until(lambda: m.read(addr("con_drb"), 4) == b"\0\0\0\0"
                  and word("con_scrl") == 0
                  and m.read(addr("dvw_dirty"), 1)[0] == 0,
                  "the band catching up with the console")
            os88marty.pace(m, 0.5)

        def force():
            """Every row marked whole from the host, and repainted."""
            m.pause()
            m.write(addr("con_dc0"), bytes(25))
            m.write(addr("con_dc1"), bytes([79] * 25))
            m.write(addr("con_drb"), b"\xff\xff\xff\x01")
            m.write(addr("dvw_dirty"), b"\x01")
            m.run()
            until(lambda: m.read(addr("con_drb"), 4) == b"\0\0\0\0"
                  and word("con_scrl") == 0, "a forced full repaint")
            os88marty.pace(m, 0.5)

        def geom():
            return (word("con_px"), word("con_oy") + word("con_topy"),
                    word("con_vcols"), word("con_vrows"))

        def calibrate():
            """fbuf is the raster WITH its border: where a solid cell poked
            into the buffer unmarked lands after a forced repaint is the
            offset (doswcell's instrument)."""
            x0, y0, vc, vr = geom()
            # a BLANK cell, so all 64 of its pixels change, inside the band's
            # middle row and inside 640 pixels: the dock and the screen's
            # edge cut a band on a 640x200 CGA
            r = vr // 2
            scr = m.read(addr("con_scr") + r * 160, 160)
            c = next((c for c in range(min(vc, 80) - 2, 0, -1)
                      if scr[c * 2] == 0x20 and x0 + 8 * (c + 1) <= 640),
                     None)
            if c is None:
                fail("no blank cell on row %d to calibrate with" % r)
            force()
            b = m.fbuf()
            cell = (r * 80 + c) * 2
            old = m.read(addr("con_scr") + cell, 2)
            m.write(addr("con_scr") + cell, b"\xdb\x07")
            force()
            p = m.fbuf()
            m.write(addr("con_scr") + cell, old)
            force()
            w, h = b[0], b[1]
            px = band_diff(b, p, 0, y0, w // 8, (h - y0) // 8 + 1)
            first = cell_at(px)
            if not first or len(px) > 64 + SNOW:
                fail("a solid cell poked at row %d column %d came back from "
                     "a forced repaint as %d pixels, not one cell"
                     % (r, c, len(px)))
            return first[0] - (x0 + c * 8), first[1] - (y0 + r * 8)

        def glass(what, off):
            x0, y0, vc, vr = geom()
            caught_up()
            a = m.fbuf()
            force()
            b = m.fbuf()
            px = band_diff(a, b, x0 + off[0], y0 + off[1], vc, vr)
            n = len(px) if len(px) > SNOW else 0
            first = px[0] if px else None
            if n and os.environ.get("DOSWTTY_SHOTS"):  # the two, to look at
                for t, c in (("a", a), ("b", b)):
                    os88marty.write_png_rgb("%s-%s.png" % (
                        os.environ["DOSWTTY_SHOTS"], t), c[0], c[1], c[2])
            if n:
                fail("after %s in the window, %d pixels of the band changed "
                     "when every row was repainted whole (the first at %r): "
                     "the incremental paint left stale cells (SPEC.md "
                     "96.54.14)" % (what, n, first))
            # ...AND THE CARET IS ON IT, which equality cannot say: two lit
            # scanlines under the cursor's cell (SPEC.md 70.8.2)
            cx, cy = word("con_cx"), word("con_cy") - word("con_vtop")
            w = b[0]
            if cy >= vr or cx >= vc or SNOW:
                return                  # off a short band's view, or on a
                                        # 640x200 CGA whose dock is over the
                                        # band's last row
            for r in (6, 7):
                yy = y0 + off[1] + cy * 8 + r
                xx = x0 + off[0] + cx * 8
                lit = sum(b[2][(yy * w + x) * 3:(yy * w + x) * 3 + 3]
                          != b"\0\0\0" for x in range(xx, xx + 8))
                if lit != 8:
                    fail("after %s the caret at row %d column %d is not "
                         "drawn: scanline %d of its cell has %d of 8 lit"
                         % (what, cy, cx, r, lit))

        if not ui.path("B:/DWTTY.COM"):
            fail("could not launch DWTTY.COM")
        full = legs("bracket")
        m.type_text("q")
        os88marty.pace(m, 3)
        os88marty.settle(m)
        ui.raise_window(ui.window("DOS"))
        dosmap.run_in_window(ui)
        os88marty.pace(m, 1)
        off = calibrate()
        n = None
        for _ in range(200):
            n = done()
            if n == 0:
                break
            os88marty.guest_sleep(m, 0.1)
        if n != 0:
            fail("window: DWTTY.COM is not waiting for a key")
        win = []
        for i, (k, what, _) in enumerate(LEGS):
            win.append(leg(k, i))
            glass(what, off)
        m.type_text("q")
        bad = []
        for (k, what, floor), f, w in zip(LEGS, full, win):
            if floor is None:
                continue                # a glass leg, too short to time
            pct = 100.0 * f / w
            print("doswtty: %-15s bracket %6.2f s, window %6.2f s: %5.1f%% "
                  "(floor %.0f%%)" % (what, f, w, pct, floor))
            if pct < floor:
                bad.append("%s at %.1f%%" % (what, pct))
        if bad:
            fail("the window prints at under its floor: %s - the per-"
                 "character marks or the band's scroll have got dearer "
                 "(SPEC.md 96.54.14)" % ", ".join(bad))


if __name__ == "__main__":
    main()
