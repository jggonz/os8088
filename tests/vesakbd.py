#!/usr/bin/env python3
"""CP850 text on the packed-pixel desktop (SPEC.md 6.7, 120.3.2).

    make vesatest && python3 tests/vesakbd.py [--png out.png]

Boots build/vesa.img on QEMU's standard VGA (tests/vesa.py's fixture, the
desktop at 1024x768x16), opens the Control Panel's Keyboard page, picks
Spanish (Spain) and types `a` and the key right of L - which that layout
makes n-tilde, CP850 A4h - into the page's typing sample. The sample is a
font_run, so on this display it is VESA.DRV's one-pass VX_FONTRUN.

Checked in causal order, each its own sentence:

  1. the layout is live (KEYBOARD.DRV resident) and the sample holds the
     two bytes `a` A4h - the keys reached the page, translated;
  2. the kernel's live bounds say the CP850 face is bound (font_last 255,
     the sample's private glyph hold) - what the renderer must read;
  3. on the glass, cell 0 (`a`) has ink and cell 1 is n-tilde's eight rows
     EXACTLY, the authored shape tests/kbdlayoutfont.py pins (independent of
     keyfont-data.inc); cell 2, the sample's padding SPACE, has none.

As first shipped VESA.DRV's run bounded the index by the constant 126, so
step 3's cell 1 was a blank cell with 1 and 2 passing: VX_ABI_VER 6 hands it
font_last/font_span through VXB_FONTLIM. QEMU by name, tests/vesa.py's
reason; on kern_mega (`mega-vesakbd`, tests/megaport.txt) Bochs's VBE, where
the driver reads the bounds through the kernel's data selector in protected
mode. Every wait is on guest state.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tests"))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import dispcp                                               # noqa: E402
import heapmap                                              # noqa: E402
import os88qemu                                             # noqa: E402
import os88sym                                              # noqa: E402
import shot                                                 # noqa: E402
import vesa                                                 # noqa: E402
from os88geom import TITLE_H, VID_VESA                      # noqa: E402

SPAIN = 2                       # kbdsettings.CHOICES: the radio row and ID
ENYE = 0xA4
# n-tilde's rows, authored (tools/cp850font.py; tests/kbdlayoutfont.py pins
# the same eight bytes in the module's table)
ENYE_ROWS = (0x18, 0x64, 0x78, 0x44, 0x44, 0x44, 0x44, 0x00)


def cell_bits(pix, w, x0, y0, bg):
    """The 8x8 cell at (x0, y0) as eight row bytes, bit 7 left: ink = any
    pixel that is not the sample's ground."""
    rows = []
    for y in range(y0, y0 + 8):
        b = 0
        for x in range(x0, x0 + 8):
            b = (b << 1) | (vesa.px(pix, w, x, y) != bg)
        rows.append(b)
    return tuple(rows)


def main():
    out_png = None
    if "--png" in sys.argv:
        out_png = sys.argv[sys.argv.index("--png") + 1]
    vesa.kill_stale()
    fails = []
    vesa.launch()
    q = heapmap.Qmp(vesa.SOCK)
    S = os88sym.linear
    eq, syms = os88sym.equates(), os88sym.syms()
    if not vesa.desk_up(q, fails, autostart=False):
        print("vesakbd: FAIL\n  " + "\n  ".join(fails))
        return 1
    if vesa.byte(q, "vid_kind") != VID_VESA:
        print("vesakbd: FAIL\n  the desktop is not on VID_VESA")
        return 1
    os88qemu.ui_done(q, S, cap=3.0, what="the first paint")
    mo = os88qemu.Mouse(q, S, q.hmp, after=2.0)

    def word(name):
        return vesa.word(q, name)

    def sample():
        fp = S("CPFP") + eq["CPE_PAINT"] * 4 + 2
        seg = int.from_bytes(q.read(fp, 2), "little")
        # a SELECTOR on kern_mega: os88qemu.lin answers its base there
        n = q.read(os88qemu.lin(seg, syms["cp_key_len"]), 1)[0]
        return q.read(os88qemu.lin(seg, syms["cp_key_text"]), n) if n else b""

    # --- the Control Panel off the Builtins menu, then its Keyboard page ---
    from os88geom import MBAR_H, MENU_ITEM_H
    import cpface
    cpface.compact(q, S)  # the compact face's geometry (SPEC.md 135.13, tests/cpface.py)
    mo.run("to", 12, 8)
    mo.run("down")
    os88qemu.ui_done(q, S, cap=0.8)
    mo.run("to", 12, MBAR_H + 1 + MENU_ITEM_H + 8)
    os88qemu.ui_done(q, S, cap=0.8)
    mo.run("up")
    os88qemu.acted(q, lambda: dispcp._cp_win(q, S) is not None, 20.0,
                   "the Control Panel window")
    win = dispcp._cp_win(q, S)
    if win is None:
        print("vesakbd: FAIL\n  no Control Panel window after the Builtins menu")
        return 1
    wx, wy = win
    os88qemu.ui_done(q, S, cap=3.0, what="the panel's first paint")
    hide = vesa.byte(q, "cp_hide")
    rec = eq["CP_IKEY"]
    if hide & (1 << rec):
        print("vesakbd: FAIL\n  the Keyboard page is hidden (cp_hide %02x)" % hide)
        return 1
    row = sum(1 for r in range(rec) if not (hide & (1 << r)))
    mo.click(wx + 1 + eq["CP_IX"] + 20,
             wy + TITLE_H + 1 + eq["CP_I0Y"] + row * eq["CP_IROWH"]
             + eq["CP_IROWH"] // 2)
    os88qemu.acted(q, lambda: vesa.byte(q, "cp_sel") == rec, 20.0,
                   "the Keyboard page")
    if vesa.byte(q, "cp_sel") != rec:
        print("vesakbd: FAIL\n  the Keyboard page did not open (cp_sel %d)"
              % vesa.byte(q, "cp_sel"))
        return 1
    os88qemu.ui_done(q, S, cap=3.0, what="the Keyboard page's paint")

    # --- Spanish (Spain), then `a` and the n-tilde key ----------------------
    mo.click(wx + 1 + eq["CP_RX"] + eq["CP_PLX"] + 20,
             wy + TITLE_H + 1 + eq["CPQ_R0Y"] + SPAIN * eq["CPQ_ROWH"] + 6)
    os88qemu.acted(q, lambda: word("mod_r_keyboard") != 0, 30.0,
                   "KEYBOARD.DRV to load")
    os88qemu.ui_done(q, S, cap=3.0, what="the layout's activation")
    if not word("mod_r_keyboard"):
        print("vesakbd: FAIL\n  picking Spanish loaded no KEYBOARD.DRV")
        return 1
    os88qemu.key(q, "a")
    os88qemu.key(q, "semicolon")
    os88qemu.acted(q, lambda: sample() == b"a" + bytes([ENYE]), 10.0,
                   "the sample to hold 'a' and n-tilde")
    os88qemu.ui_done(q, S, cap=3.0, what="the sample's paint")

    # 1. the bytes
    got = sample()
    if got != b"a" + bytes([ENYE]):
        fails.append("the sample holds %r, want b'a\\xa4': the keys were not "
                     "translated by the Spanish layout (nothing below is "
                     "about the renderer)" % got)
    # 2. the bounds the renderer must read
    last = vesa.byte(q, "font_last")
    span = vesa.byte(q, "font_last", 1)
    if (last, span) != (255, 223):
        fails.append("font_last/font_span %d/%d, want 255/223: the sample's "
                     "glyph hold did not bind the CP850 face" % (last, span))

    # 3. the glass
    w, h, pix = vesa.screendump(q, os.path.join(ROOT, "build", "vesakbd-shot"))
    if out_png:
        shot.png(out_png, w, h, pix)
    x0 = wx + 1 + eq["CP_RX"] + eq["CPQ_TEXTX"]
    y0 = wy + TITLE_H + eq["CPQ_TEXTY"]     # the page's BP is the frame's
                                            # y + TITLE_H: CPQ_SY1's frame
                                            # line lands at wy+TITLE_H+108
    bg = vesa.px(pix, w, x0 + 16 + 7, y0 + 7)     # cell 2's corner: a SPACE
    pad = cell_bits(pix, w, x0 + 16, y0, bg)
    if any(pad):
        fails.append("cell 2 (the sample's padding SPACE) at (%d,%d) has ink "
                     "%s: the sample is not where CPQ_TEXTX/Y put it, so the "
                     "cells below say nothing" % (x0 + 16, y0, pad))
    a = cell_bits(pix, w, x0, y0, bg)
    if not any(a):
        fails.append("cell 0 ('a') at (%d,%d) is blank: the run drew no ASCII "
                     "either" % (x0, y0))
    enye = cell_bits(pix, w, x0 + 8, y0, bg)
    if enye != ENYE_ROWS:
        fails.append("cell 1 (n-tilde, A4h) at (%d,%d) is %s, want %s%s"
                     % (x0 + 8, y0, ["%02x" % r for r in enye],
                        ["%02x" % r for r in ENYE_ROWS],
                        " - a BLANK cell: VX_FONTRUN bounded the index by "
                        "ASCII, not the live font_span" if not any(enye) else ""))

    q.hmp("quit")
    if fails:
        print("vesakbd: FAIL")
        for f in fails:
            print("  " + f)
        return 1
    print("vesakbd: ok - Spanish on the %dx%d desktop: 'a' and n-tilde "
          "drawn by VX_FONTRUN with font_span %d" % (w, h, span))
    return 0


if __name__ == "__main__":
    sys.exit(main())
