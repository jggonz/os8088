#!/usr/bin/env python3
"""The installer's ACTION BUTTON becomes Restart, and there is no third one
(SPEC.md 52.10.6.1).

The install dialog used to carry three buttons - `[Install] [Close] [Restart]`
- and the third was a greyed rectangle in every stage but the last.  It is two
now, and the first one says what the click does *now*: `Install`, then
`Copy Apps`, then `Restart`.

    python3 tests/instrest.py

THREE ASSERTIONS, and the first is what makes the other two trustworthy.

  1. **The caption is READ, not inferred.**  The kernel's own 8x8 glyph table
     comes out of the guest (`font_glyphs`, .lowbss - so LOW_SEG, which is
     what os88sym.py is for), the expected string is rendered from it on the
     host, and the result is searched for inside the button's rect.  A pixel
     count or a "did these bytes change" diff would pass on a button that had
     gone blank, gone greyed, or picked up the wrong string; this cannot.
     It is checked against `Install` FIRST, at the idle stage, where the
     answer is already known - so a failure there is the APPARATUS (a wrong
     polarity, a wrong glyph base, a moved button) and not the feature.  That
     distinction has cost this tree whole rounds.

  2. **Nothing is drawn where the third button was.**  `HIW_B2X`/`HIW_BW2` are
     gone from inst.inc, so their rect must be flat window background - while
     the two surviving buttons must still have ink in them, or the check would
     pass just as well on a window that failed to draw at all.

  3. **Clicking it restarts the machine.**  A caption is a promise and the
     behaviour is the thing; a button reading `Restart` that installs, or does
     nothing, is exactly the defect.  On this machine the restart is watched
     as the DESKTOP GOING AWAY rather than as a completed boot: `os8088_xt_hdd`
     is a GLaBIOS machine, and SPEC.md 9.6.5 records that `int 19h` there
     leaves a blank 80-column text screen with the tick still running.  That
     is a property of the BIOS and not of either kernel - and it is still
     decisive here, because nothing else this dialog can do leaves graphics
     mode at all.

REQUIRES A DISK: os8088_xt_hdd, and it ERASES the VHD, exactly as
tests/instdeep.py does - the two share that machine, that pristine copy and
the clicks that drive the install.
"""
import os
import sys

sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88marty as M                                       # noqa: E402
import instdeep as ID                                       # noqa: E402

FONT_FIRST = 32                     # font.inc: glyphs 32..126, 8 bytes each
FONT_N = 95

# inst.inc's button band, content-relative.  HIW_B2X/HIW_BW2 are NOT imported
# from anywhere because they no longer exist - the whole point - so they are
# written here as what they WERE, which is what this test has to look at.
HIW_BY, HIW_BH = ID.HIW_BY, ID.HIW_BH
HIW_B0X, HIW_BW0 = ID.HIW_B0X, ID.HIW_BW0
HIW_B1X, HIW_BW1 = 104, 56          # Close
GONE_X, GONE_W = 168, 72            # ...and the retired third button


def glyph_table(m):
    return m.read(m.sym("font_glyphs"), FONT_N * 8)


def render(text, tab):
    """`text` as rows of 0/1, 8 rows by 8*len - 1 = INK.

    The kernel's font is a fixed 8x8 cell and `font_width_x` is 8 a character,
    so there is no advance to model: the string is its glyphs side by side.
    """
    out = []
    for r in range(8):
        row = []
        for ch in text:
            b = tab[(ord(ch) - FONT_FIRST) * 8 + r]
            row.extend((b >> bit) & 1 for bit in range(7, -1, -1))
        out.append(row)
    return out


def screen(m):
    """The CGA framebuffer as rows of 0/1, one entry a pixel.  1 = LIT."""
    w, h, rows = m.vram("cga")
    return w, h, rows


def find(rows, x0, y0, w, h, want):
    """Is `want` (rows of INK bits) drawn anywhere inside this rect?

    SEARCHED rather than checked at a computed pen, because os88ui_btn centres
    a label and this test must not carry a second copy of that arithmetic -
    which is the thing that would silently go stale if the button ever moved.

    A lit framebuffer pixel is WHITE.  The button's interior is filled CWHITE
    and the caption is drawn in CBLACK over it, so an ink pixel reads 0 and
    the ground reads 1: the match is against `1 - ink`.
    """
    gh, gw = len(want), len(want[0])
    for y in range(y0, y0 + h - gh + 1):
        for x in range(x0, x0 + w - gw + 1):
            for r in range(gh):
                row, wr = rows[y + r], want[r]
                if any(row[x + c] != 1 - wr[c] for c in range(gw)):
                    break
            else:
                return x, y
    return None


def ink(rows, x0, y0, w, h):
    """Dark pixels in a rect - what a drawn control has and bare window has
    none of."""
    return sum(1 for y in range(y0, y0 + h)
               for x in range(x0, x0 + w) if not rows[y][x])


# Bare desktop, clear of the installer window (x 63..363), of the drive-zone
# column (x 526.. on CGA - SPEC.md 26.1) and of the menu bar.
PARK = (500, 30)


def park(mo):
    """Take the POINTER off the thing about to be read.

    The arrow is 8x12 and the kernel draws it into the framebuffer (SPEC.md
    7.1), so a cursor left where the last click landed is ink ON the control -
    and the last click here lands in the middle of the very button whose
    caption this test reads. It cost a run: the caption really did say
    `Restart` and the match failed on four rows the arrow was sitting in.
    """
    mo.to(*PARK)


def caption(m, mo, ix, iy, tab, want, stage):
    park(mo)
    rows = screen(m)[2]
    hit = find(rows, ix + HIW_B0X, iy + HIW_BY, HIW_BW0, HIW_BH,
               render(want, tab))
    if hit is None:
        sys.exit("instrest: the action button does not read %r at the %s "
                 "stage (SPEC.md 52.10.6.1)" % (want, stage))
    print("  %-8s the action button reads %-9r at (%d,%d)"
          % (stage + ":", want, hit[0], hit[1]))
    return rows


def no_third_button(rows, ix, iy):
    gone = ink(rows, ix + GONE_X, iy + HIW_BY, GONE_W, HIW_BH)
    b0 = ink(rows, ix + HIW_B0X, iy + HIW_BY, HIW_BW0, HIW_BH)
    b1 = ink(rows, ix + HIW_B1X, iy + HIW_BY, HIW_BW1, HIW_BH)
    print("  ink: action %d, Close %d, where the third button was %d"
          % (b0, b1, gone))
    if not b0 or not b1:
        sys.exit("instrest: the two surviving buttons are not drawn - this "
                 "run proves nothing about the third")
    if gone:
        sys.exit("instrest: %d dark pixels where the standalone Restart "
                 "button used to be (SPEC.md 52.10.6.1)" % gone)


def main():
    if not os.path.exists("build/martypc/run/martypc_headless"):
        sys.exit("no MartyPC - `make marty` first")
    for img in ("build/os8088-360.img", "build/apps360.img"):
        if not os.path.exists(img):
            sys.exit("no %s - `make` first" % img)
    # NO PRISTINE DANCE. Every instance gets its own clone of the staged
    # tree's VHD (os88marty.launch), so the disk this erases is this run's.
    with M.launch("build/os8088-360.img", apps="build/apps360.img",
                  machine=ID.MACHINE) as m:
        M.settle(m)
        tab = glyph_table(m)
        mo, ix, iy = ID.open_installer(m)

        # The apparatus first, against an answer that is already known.
        rows = caption(m, mo, ix, iy, tab, "Install", "idle")
        no_third_button(rows, ix, iy)

        ID.run_install(m, mo, ix, iy)
        M.settle(m)

        rows = caption(m, mo, ix, iy, tab, "Restart", "done")
        no_third_button(rows, ix, iy)

        # ...and the promise kept.  The desktop is 93%+ lit across the menu
        # bar's field (M.desktop_up); a blank text screen is not, and nothing
        # else this dialog does can leave graphics mode.
        before = M._Screen(m)
        if not M.desktop_up(before):
            sys.exit("instrest: the desktop is not up before the click - %r"
                     % (before,))
        mo.click(ix + HIW_B0X + HIW_BW0 // 2, iy + HIW_BY + HIW_BH // 2,
                 settle=0)
        M.until(m, lambda mm: not M.desktop_up(M._Screen(mm)),
                "the machine to leave the desktop - the restart", limit=60.0)
        print("  the click left the desktop: %r" % (M._Screen(m),))

    print("instrest: two buttons, the action button reads Install and then "
          "Restart, and clicking it restarts the machine")


if __name__ == "__main__":
    main()
