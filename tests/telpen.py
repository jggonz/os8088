#!/usr/bin/env python3
"""THE PEN'S FOURTH REFUSAL IS LIFTED (SPEC.md 5.4.2.2.1, 70.8.2/70.8.3)

    make && python3 tests/telpen.py [--machine os8088_xt_vga]

`gfx_blit1_pen` arms the VGA's Set/Reset for the planes both colours agree on
and hands the varying planes the band. That works while every varying plane
wants the SAME thing - the band, or its complement - and until SPEC.md 5.4.2.2.1
a pair that wanted both answered `CF = 1` and drew nothing:

    accepted  <=>  ink AND paper == ink   or   ink AND paper == paper

So white on anything was fine and anything on black was fine, and **green on
red was refused** - 2 AND 4 is 0 and neither operand is 0 - which is most of
the sixteen-colour pairs an ANSI board's art is made of. 5.4.2.2.1 splits the
band between two passes with the Map Mask instead, so every pair is legal now.

**THIS IS THE ONLY ROW IN THE TREE THAT CAN SEE IT.** The pen is not read at
all on a 1bpp adapter (5.4.2.2), so CGA and Hercules answer a different
question; and mode 12h is four planes behind the Graphics Controller, so there
is no flat framebuffer to read either. `os8088_xt_vga` plus `fbuf` - what the
CARD rasterised - is the whole apparatus.

WHAT IT COMPARES, AND WHY IT IS NOT A GOLDEN IMAGE. TELNET's glyph table is
read out of the GUEST (`te_glyf`, SPEC.md 70.8.6) and every cell is rendered
on the HOST at the two colours its attribute names, then compared pixel for
pixel with what the card put on the screen. So the assertion is "the machine
drew the glyph it holds, in the colours the attribute asked for", and nothing
has to be regenerated when the font changes.

FOUR OF THE SIX PAIRS UNDER TEST WERE REFUSED BEFORE, and the test says which:
a build without 5.4.2.2.1 draws nothing at all for those rows, so their cells
come back as whatever the window's background is and every pixel differs.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import os88marty                                        # noqa: E402
import os88mouse                                        # noqa: E402
import os88sym                                          # noqa: E402
import dispcp                                           # noqa: E402
import os88build                                       # noqa: E402

S = os88sym.linear
TE_COLS, TE_ROWS = 80, 25

# The standard EGA/VGA sixteen, which mode 12h's DAC comes up holding: the
# same table kernel/vga12.inc writes a plane bit for. It is a CONSTANT of the
# adapter and not of this build, which is what makes an absolute comparison
# fair here.
PAL = [(0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170),
       (170, 0, 0), (170, 0, 170), (170, 85, 0), (170, 170, 170),
       (85, 85, 85), (85, 85, 255), (85, 255, 85), (85, 255, 255),
       (255, 85, 85), (255, 85, 255), (255, 255, 85), (255, 255, 255)]


def refused(ink, paper):
    """The predicate SPEC.md 5.4.2.2.1 lifted, so the report can name the pairs
    that are new rather than claiming credit for the ones that always drew."""
    return (ink & paper) != ink and (ink & paper) != paper


# ink, paper, and a line of text to put in that pair.  The first four are the
# refused ones; the last two always drew and are the control.
CASES = [
    (2, 4, "green on red - the pair the pen refused"),
    (3, 5, "cyan on magenta - refused, and drawn now"),
    (14, 1, "yellow on blue - refused: 14 AND 1 is 0"),
    (10, 4, "bright green on red - refused"),
    (15, 0, "white on black - the default pair, always drew"),
    (0, 7, "black on light grey - a subset pair, always drew"),
]


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
    if open(out, "rb").read() != open(os88build.at("build/telnet.bin"), "rb").read():
        sys.exit("telpen: the mapped build is not build/telnet.bin - every "
                 "offset it names would be plausible and wrong")
    syms = {}
    for line in open(mp):
        m = re.match(r"^\s+([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s+(\S+)\s*$", line)
        if m:
            syms[m.group(3)] = int(m.group(1), 16)
    return syms


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga")
    ap.add_argument("--shot", default=None)
    a = ap.parse_args()
    fails = []
    sy = te_syms()

    with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                          machine=a.machine) as m:
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        wins = dispcp.win_list(m, S)
        wx, wy = dispcp.win_rect(m, S, wins[-1])[:2]
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "APPS")
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "TELNET.O88")
        wins2 = dispcp.win_list(m, S)
        if len(wins2) <= len(wins):
            sys.exit("telpen: TELNET.O88 did not open a window")
        tw = wins2[-1]
        rec = m.read(S("wm_wins") + tw * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
        pseg = rec[22] | (rec[23] << 8)
        tx0, ty0, tww, thh = dispcp.win_rect(m, S, tw)

        def rw(n):
            d = m.readseg(pseg, sy[n], 2)
            return d[0] | (d[1] << 8)

        px, vcols, vtop = rw("te_px"), rw("te_vcols"), rw("te_vtop")
        mono = m.readseg(pseg, sy["te_mono"], 1)[0]
        say("telnet at %04X: te_px %d, te_vcols %d, te_vtop %d, te_mono %d"
            % (pseg, px, vcols, vtop, mono))
        if mono:
            sys.exit("telpen: this machine is 1bpp and the pen is not read "
                     "there at all (SPEC.md 5.4.2.2) - run it on a VGA")

        # --- THE POINTER IS PARKED FIRST. The arrow is drawn INTO the
        # framebuffer (SPEC.md 7.1), so one resting on a cell is a box of
        # differing pixels that reads exactly like a decoder bug.
        mo.to(4, 470)

        # --- the buffer: one CASE per row, from row 0 ------------------------
        buf = bytearray()
        for r in range(TE_ROWS):
            if r < len(CASES):
                ink, paper, text = CASES[r]
                at = (paper << 4) | ink
            else:
                at, text = 0x07, ""
            text = (text + " " * TE_COLS)[:TE_COLS]
            for ch in text:
                buf += bytes([ord(ch) & 0xFF, at])
        m.pause()
        try:
            m.write(pseg * 16 + sy["te_scr"], bytes(buf))
            m.write(pseg * 16 + sy["te_drb"], bytes([0xFF, 0xFF, 0xFF, 0x01]))
            m.write(pseg * 16 + sy["te_cvis"], bytes([0]))   # no underline to
                                                             # account for
            m.write(pseg * 16 + sy["te_scrl"], bytes([0, 0]))
        finally:
            m.run()
        os88marty.settle(m)

        glyf = m.readseg(pseg, sy["te_glyf"], 256 * 8)
        w, h, fb = m.fbuf()
        say("framebuffer %dx%d, %d bytes" % (w, h, len(fb)))
        if a.shot:
            os88marty.write_png_rgb(a.shot, w, h, fb)
            say("wrote %s" % a.shot)

        # THE ORIGIN IS READ OUT OF THE GUEST, not derived from the window's
        # corner. te_oy is what OSAPI_WM_CONTENT answered and TE_TOPY is the
        # package's own constant, so this is the pen the renderer actually
        # used; deriving it as `frame + TITLE_H + 1` is one pixel out and
        # every cell then samples a scan line of the row below.
        y0 = rw("te_oy") + 22                       # TE_TOPY

        def pix(x, y):
            i = (y * w + x) * 3
            return (fb[i], fb[i + 1], fb[i + 2])

        for r, (ink, paper, text) in enumerate(CASES):
            if r < vtop:
                continue                            # not on show on a short
            bad = 0                                 # window; there is none here
            first = None
            ncell = min(vcols, 46)                  # the text, not the padding
            for c in range(ncell):
                ch = buf[(r * TE_COLS + c) * 2]
                rowsrc = glyf[ch * 8:ch * 8 + 8]
                for sy_ in range(8):
                    bits = rowsrc[sy_]
                    for sx in range(8):
                        lit = (bits >> (7 - sx)) & 1
                        want = PAL[ink] if lit else PAL[paper]
                        got = pix(px + c * 8 + sx,
                                  y0 + (r - vtop) * 8 + sy_)
                        if got != want:
                            bad += 1
                            if first is None:
                                first = (c, sx, sy_, want, got)
            tag = "REFUSED before" if refused(ink, paper) else "always drew"
            say("  ink %2d on paper %2d (%s): %d of %d pixels differ"
                % (ink, paper, tag, bad, ncell * 64))
            if bad:
                fails.append("ink %d on paper %d (%s): %d of %d pixels are "
                             "not the glyph in the colours the attribute "
                             "named; first at cell %d pixel (%d,%d), wanted "
                             "%r and got %r (SPEC.md 5.4.2.2.1)"
                             % (ink, paper, tag, bad, ncell * 64, first[0],
                                first[1], first[2], first[3], first[4]))

    for f in fails:
        say("FAIL: " + f)
    say("telpen: " + ("ok" if not fails else "FAILED"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
