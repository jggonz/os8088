#!/usr/bin/env python3
"""Choosing a VESA mode on the Display page (SPEC.md 120.6).

    make vesatest && python3 tests/vesacp.py [--png out.png]

Boots build/vesa.img on QEMU's standard VGA (tests/vesa.py's fixture: the
desktop comes up at 1024x768x16, Auto's pick), opens the Control Panel from
the Builtins menu, clicks its Display page and drives the VESA row:

  1. the row is listed (`VID_A_VESA` lit: slot `adapter_row(avail, 4)`, at
     the four-row pitch `CPV_ROWH` reads out of THIS build) and already the
     pending choice, so a click on it CYCLES the class: `CFG_VE` goes from
     0 (Auto) to the first offered record, and the label on the glass
     changes with it - the row's cells are compared before and after;
  2. a second click walks on to the second record: `CFG_VE` changes again,
     the label changes again, and the cells PAST the new label are white -
     a shorter label erases the longer one it replaces (the first cut of
     the composer left `16` on the glass after `640x400x8`), and the
     driver's walk keeps the painter's DI (the second cut drew the second
     label off the pane);
  3. Set Primary with that class pending: `VXV_PICK`, then the switch - the
     desktop is on `VID_VESA` at the class's geometry (640x480 at 8bpp, the
     second record on this card, `vx_class` 0x84), and a screendump is that
     size.

QEMU by name on CLAUDE.md's closed list, tests/vesa.py's reason. Nothing
here is a time.
"""
import os
import subprocess
import sys
import time

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
from os88geom import MBAR_H, MENU_ITEM_H, TITLE_H, VID_VESA  # noqa: E402

CFG_VE = 9                      # driver.inc: the class byte in drv_cfg
CP_PLX, CP_PLDY = 22, 2         # ctrl.inc: the row label's x and baseline
LABEL_CELLS = 16                # 'Vesa 1024x768x16', the longest label
# the second offered record on QEMU's standard VGA, after 640x400x8 (0101h)
WANT2 = dict(cw=640, ch=480, bpp=8, cls=0x84)


def run(*a):
    subprocess.run(["python3", "tools/mouse.py", "--screen", "1024x768",
                    vesa.SOCK] + list(a), cwd=ROOT, check=True,
                   capture_output=True)


def click(x, y):
    run("click", str(x), str(y))


def cfg_ve(q):
    return q.read(os88sym.linear("drv_cfg") + CFG_VE, 1)[0]


def cells(pix, w, x0, y0, n):
    """The n label cells from (x0, y0) as a tuple of pixels, for a compare."""
    out = []
    for y in range(y0, y0 + 8):
        for x in range(x0, x0 + 8 * n):
            out.append(vesa.px(pix, w, x, y))
    return tuple(out)


def main():
    out_png = None
    if "--png" in sys.argv:
        out_png = sys.argv[sys.argv.index("--png") + 1]
    vesa.kill_stale()
    fails = []
    vesa.launch()
    q = heapmap.Qmp(vesa.SOCK)
    for _ in range(200):
        try:
            q.hmp("info status")
            break
        except OSError:
            time.sleep(0.1)
    S = os88sym.linear
    os88qemu.acted(q, lambda: vesa.byte(q, "vid_kind") == VID_VESA, 120.0,
                   "vid_kind = VID_VESA")
    if vesa.byte(q, "vid_kind") != VID_VESA:
        print("vesacp: FAIL\n  the desktop did not come up on VID_VESA")
        return 1
    os88qemu.pace(q, 3.0)

    # --- the Control Panel, off the Builtins menu, then its Display page ----
    run("to", "12", "8")
    run("down")
    os88qemu.pace(q, 0.8)
    run("to", "12", str(MBAR_H + 1 + MENU_ITEM_H + 8))
    os88qemu.pace(q, 0.8)
    run("up")
    os88qemu.pace(q, 2.0)
    win = dispcp._cp_win(q, S)
    if win is None:
        print("vesacp: FAIL\n  no Control Panel window after the Builtins menu")
        return 1
    wx, wy = win
    hide = q.read(S("cp_hide"), 1)[0]
    row = sum(1 for r in range(dispcp.CP_IVID) if not (hide & (1 << r)))
    click(wx + 1 + dispcp.CP_IX + 30,
          wy + TITLE_H + 1 + dispcp.CP_I0Y + row * dispcp.CP_IROWH
          + dispcp.CP_IROWH // 2)
    os88qemu.pace(q, 2.0)
    if q.read(S("cp_sel"), 1)[0] != dispcp.CP_IVID:
        print("vesacp: FAIL\n  the Display page did not open (cp_sel %d)"
              % q.read(S("cp_sel"), 1)[0])
        return 1

    # --- the VESA row: where it is on THIS build ---------------------------
    avail = vesa.byte(q, "vid_avail")
    rowh = dispcp._rowh()
    slot = dispcp.adapter_row(avail, VID_VESA)
    if not avail & (1 << VID_VESA):
        fails.append("vid_avail %02x: VID_A_VESA is not lit, so no row" % avail)
    gx = wx + 1 + dispcp.CP_RX + dispcp.CP_PGX + 6
    gy = wy + TITLE_H + 1 + dispcp.CPV_R0Y + slot * rowh + 6
    lx = wx + 1 + dispcp.CP_RX + CP_PLX
    ly = wy + TITLE_H + 1 + dispcp.CPV_R0Y + slot * rowh + CP_PLDY
    if vesa.byte(q, "cp_vsel") != VID_VESA:
        fails.append("cp_vsel %d: the running kind is not the pending choice"
                     % vesa.byte(q, "cp_vsel"))
    ve0 = cfg_ve(q)
    if ve0 != 0:
        fails.append("CFG_VE %d at the start, want 0 (Auto)" % ve0)
    w, h, pix = vesa.screendump(q, os.path.join(ROOT, "build", "vesacp-shot"))
    if out_png:
        shot.png(out_png, w, h, pix)
    lab0 = cells(pix, w, lx, ly, LABEL_CELLS)

    # --- 1. a click on the chosen VESA row cycles its class ---------------
    click(gx, gy)
    os88qemu.pace(q, 2.0)
    ve1 = cfg_ve(q)
    if not ve1 & 0x80 or ve1 == ve0:
        fails.append("after one click CFG_VE %d, want the first record (bit 7 "
                     "set)" % ve1)
    w, h, pix = vesa.screendump(q, os.path.join(ROOT, "build", "vesacp-shot"))
    if out_png:
        shot.png(out_png.replace(".png", "-1.png"), w, h, pix)
    lab1 = cells(pix, w, lx, ly, LABEL_CELLS)
    if lab1 == lab0:
        fails.append("the row's label did not change with the first cycle")

    # --- 2. ...and again, with the longer label's tail erased -------------
    click(gx, gy)
    os88qemu.pace(q, 2.0)
    ve2 = cfg_ve(q)
    if not ve2 & 0x80 or ve2 == ve1:
        fails.append("after two clicks CFG_VE %d, want the second record (was "
                     "%d)" % (ve2, ve1))
    w, h, pix = vesa.screendump(q, os.path.join(ROOT, "build", "vesacp-shot"))
    if out_png:
        shot.png(out_png.replace(".png", "-2.png"), w, h, pix)
    lab2 = cells(pix, w, lx, ly, LABEL_CELLS)
    if lab2 == lab1:
        fails.append("the row's label did not change with the second cycle "
                     "(the driver's walk clobbered the painter's DI?)")
    # 'Vesa 640x480x8' is 14 cells: 15 and 16 must be the pane's white
    tail = vesa.white_share(pix, w, lx + 8 * 14, ly, lx + 8 * 16, ly + 8)
    if tail < 0.99:
        fails.append("the cells past the second label are %.0f%% white: a "
                     "longer label's tail was left on the glass" % (100 * tail))

    # --- 3. Set Primary: PICK, then the switch ---------------------------
    bx = wx + 1 + dispcp.CP_RX + dispcp.CPV_BTNX + 40
    by = wy + TITLE_H + 1 + dispcp.CPV_BTNY + 9
    click(bx, by)
    os88qemu.acted(q, lambda: vesa.word(q, "vid_cw") == WANT2["cw"], 30.0,
                   "vid_cw = %d" % WANT2["cw"])
    os88qemu.pace(q, 3.0)
    got = dict(kind=vesa.byte(q, "vid_kind"), cw=vesa.word(q, "vid_cw"),
               ch=vesa.word(q, "vid_ch"), bpp=vesa.byte(q, "vid_planes"),
               cls=vesa.byte(q, "vx_class"))
    if got["kind"] != VID_VESA:
        fails.append("after Set Primary vid_kind %d, want %d" % (got["kind"],
                                                                 VID_VESA))
    for k in ("cw", "ch", "bpp", "cls"):
        if got[k] != WANT2[k]:
            fails.append("after Set Primary %s = %s, want %s (the second "
                         "record on QEMU's standard VGA)" % (k, got[k], WANT2[k]))
    w, h, pix = vesa.screendump(q, os.path.join(ROOT, "build", "vesacp-shot"))
    if out_png:
        shot.png(out_png.replace(".png", "-3.png"), w, h, pix)
    if (w, h) != (WANT2["cw"], WANT2["ch"]):
        fails.append("the screendump is %dx%d after the switch, want %dx%d"
                     % (w, h, WANT2["cw"], WANT2["ch"]))
    if fails:
        print("vesacp: FAIL")
        for f in fails:
            print("  " + f)
        return 1
    print("vesacp: ok - the VESA row cycles %d -> %d -> %d, Set Primary lands "
          "on %dx%dx%d" % (ve0, ve1, ve2, got["cw"], got["ch"], got["bpp"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
