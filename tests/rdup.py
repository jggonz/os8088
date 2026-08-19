#!/usr/bin/env python3
"""SPEC.md 62.9.11.3: the Ram Disk page acts on the RELEASE.

    python3 tests/rdup.py [machine]

The third driver page to be converted (SPEC.md 13.8.4), and the one that came
in last - so it arrived with the good half already done (one rect per control,
`os88ui_btn`, `os88ui_glyph`, `os88ui_bhit`) and the whole of the event half
missing: it acted on the press, drew no pressed state, and published neither
DSV_CPUP nor DSV_CPDRAG. Every control on it but one wanted the release -
Mount unmounts a live volume, Clear forgets the boot image, `-`/`+` resize a
store - and the exception, the size box taking the caret, is exactly SPEC.md
13.6's safe prefix action.

  P  DSV_CPUP and DSV_CPDRAG are PUBLISHED (the kernel's own copy of them)
  D  a press draws `+` DOWN...
  B  ...and it comes back UP when the pointer slides off it, still held
  A  a slide-off release does NOT change the size
  C  press-and-release ON it DOES

C is the one that says the conversion did not eat the feature: a page that had
simply stopped dispatching would pass P, D, B and A.

The SIZE FIELD is compared as a BITMAP and never as a lit-pixel count - two
different numbers can weigh the same, and this is a four-character field where
that is not far-fetched.
"""
import sys
import time

sys.path.insert(0, "tools")
import os88marty as M
from os88mouse import Mouse
from os88geom import WIN_SIZE, MAX_WIN

MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla"

# ctrl.inc's geometry and page.inc's, mirrored deliberately: a gate that
# derives its coordinates from the kernel is testing the kernel against itself.
CP_I0Y, CP_IROWH = 6, 14
CP_RX = 96
CP_DBY1, CP_DROWH = 20, 26
CP_IDRV = 2
RP_SY, RP_STX, RP_SW = 18, 48, 6
RP_PX, RP_MW, RP_FH = 146, 18, 12
DRVR_SZ, DRVR_SEG = 16, 2
RD_ROW = 3                      # drv_tab row 3 is the RAM disk since
                                # SPEC.md 31.1's reorder
DRVC_FILE, DSV_SIZE = 5, 34
DSV_CPUP, DSV_CPDRAG = 30, 32
TITLE_H = 18

fails = []


def check(name, cond, note=""):
    print(f"  [{'PASS' if cond else 'FAIL'}] {name} {note}")
    if not cond:
        fails.append(name)


def _u16(b, o):
    return b[o] | (b[o + 1] << 8)


def wins(m):
    b = m.read(m.sym("wm_wins"), WIN_SIZE * MAX_WIN)
    return [tuple(_u16(b[i * WIN_SIZE:], o) for o in (2, 4, 6, 8))
            for i in range(MAX_WIN) if _u16(b[i * WIN_SIZE:], 0) & 2]


def quiet(m, s=12.0):
    try:
        M.settle(m, limit=s)
    except M.MartyError:
        pass


with M.launch("build/os8088-360.img", apps="build/apps360.img",
              machine=MACHINE) as m:
    M.settle(m)
    mo = Mouse(marty=m)
    mono = m.video()["type"] in ("cga", "mda", "herc")
    print(f"== {MACHINE} : the Ram Disk page fires on the release ==")

    def bmp(rect):
        """The rect as BYTES, so any change at all is a change."""
        if mono:
            w, h, rows = m.vram()
            return bytes(rows[y][x] for y in range(rect[1], rect[3] + 1)
                         for x in range(rect[0], rect[2] + 1))
        w, h, px = m.fbuf()
        return bytes(px[(y * w + x) * 3]
                     for y in range(rect[1], rect[3] + 1)
                     for x in range(rect[0], rect[2] + 1))

    def lit(rect):
        b = bmp(rect)
        return sum(1 for v in b if v) if not mono else sum(b)

    mo.menu(8, 8, 8, 40)                        # chip menu -> Control Panel
    quiet(m)
    cp = [w for w in wins(m) if w[2] >= 280 and w[3] >= 100]
    check("the Control Panel opened", bool(cp))
    if not cp:
        sys.exit("no panel")
    cp = cp[0]
    x0, y0 = cp[0] + 1, cp[1] + TITLE_H

    def item(n):
        mo.click(x0 + 40, y0 + CP_I0Y + n * CP_IROWH + 7)
        quiet(m)

    item(CP_IDRV)                               # Drivers, then tick Ram Disk
    mo.click(x0 + CP_RX + 40, y0 + CP_DBY1 + RD_ROW * CP_DROWH + CP_DROWH // 2)
    quiet(m, 25.0)
    seg = int.from_bytes(
        m.read(m.sym("drv_tab") + RD_ROW * DRVR_SZ + DRVR_SEG, 2), "little")
    check("the Ram Disk driver loaded", seg != 0, f"(seg {seg:#06x})")
    if not seg:
        sys.exit("no driver")

    # --- P: the two cells are PUBLISHED -----------------------------------
    # The kernel's own copy, which is what drv_cp_call reads - not the
    # driver's table. A table that stopped short would show whatever follows
    # it here, so a NON-ZERO answer is only half of it; the behaviour below
    # is the other half.
    base = m.sym("drv_svc") + (DRVC_FILE - 1) * DSV_SIZE   # class 1 is index 0
    up = int.from_bytes(m.read(base + DSV_CPUP, 2), "little")
    dg = int.from_bytes(m.read(base + DSV_CPDRAG, 2), "little")
    check("DSV_CPUP and DSV_CPDRAG are published", up != 0 and dg != 0,
          f"(CPUP={up:#06x} CPDRAG={dg:#06x})")

    nst = m.read(m.sym("cp_nst"), 1)[0]
    item(nst)                                   # the driver's own page, first
    quiet(m, 20.0)                              # paint LOADS RAMPAGE.DRV

    plus = (x0 + CP_RX + RP_PX, y0 + RP_SY - 2,
            x0 + CP_RX + RP_PX + RP_MW - 1, y0 + RP_SY - 2 + RP_FH - 1)
    inner = (plus[0] + 1, plus[1] + 1, plus[2] - 1, plus[3] - 1)
    field = (x0 + CP_RX + RP_STX, y0 + RP_SY,
             x0 + CP_RX + RP_STX + RP_SW * 8 - 1, y0 + RP_SY + 7)

    was = bmp(field)
    check("the Ram Disk page painted", any(was),
          f"({lit(field)} lit in the size field)")

    # --- D: the press draws it DOWN ---------------------------------------
    # The INTERIOR and a HALVING, not a difference: a pressed 18x12 cell turns
    # its white interior black, and the mouse arrow parked on it is ~15 lit
    # pixels all by itself - which is enough to pass a `!=` against a build
    # whose down state never drew at all (PERFORMANCE.md Part 3.1's own trap,
    # and it has already caught this exact conversion once in files.inc).
    mo.to(plus[0] + 6, plus[1] - 24)            # park off it first
    time.sleep(0.8)
    upl = lit(inner)
    mo.to((plus[0] + plus[2]) // 2, (plus[1] + plus[3]) // 2)
    mo._edge(True)
    time.sleep(0.9)
    dnl = lit(inner)
    check("a press draws `+` DOWN", dnl * 2 < upl,
          f"({upl} lit upright, {dnl} held)")

    # --- B: it comes back UP while STILL HELD -----------------------------
    mo.to(plus[0] - 40, (plus[1] + plus[3]) // 2, l=True)
    time.sleep(1.2)
    off = lit(inner)
    check("...and back UP when the pointer slides off it", off == upl,
          f"({off} lit, upright is {upl})")

    # --- A: the release lands off it: NOTHING fires ------------------------
    mo._edge(False)
    quiet(m)
    check("a slide-off release does NOT change the size", bmp(field) == was,
          f"({lit(field)} lit, was {lit(field)})")

    # --- C: press AND release on it ---------------------------------------
    mo.to((plus[0] + plus[2]) // 2, (plus[1] + plus[3]) // 2)
    mo._edge(True)
    time.sleep(0.7)
    mo._edge(False)
    quiet(m)
    check("press-and-release ON it DOES change the size", bmp(field) != was,
          "(the field is compared as a bitmap, so any digit moving counts)")

    # --- E: the SIZE BOX still fires on the press --------------------------
    # SPEC.md 13.6's exception, and the reason the conversion is not simply
    # "everything moved to the release": taking the caret is a safe prefix
    # action, so that one control is left where it was. The caret is drawn by
    # the press alone, with the button still DOWN.
    box = (x0 + CP_RX + 42, y0 + RP_SY - 3,
           x0 + CP_RX + 42 + 59, y0 + RP_SY - 3 + RP_FH - 1)
    before = bmp(box)
    mo.to((box[0] + box[2]) // 2, (box[1] + box[3]) // 2)
    mo._edge(True)
    time.sleep(0.9)
    check("the size box takes the caret on the PRESS", bmp(box) != before,
          "(13.6: a safe prefix action keeps the press)")
    mo._edge(False)
    quiet(m)

    # --- F: Mount, which is the control the release was worth having for ---
    # It puts a whole volume up, so a mis-aimed press was the expensive one.
    cap = (x0 + CP_RX + 2, y0 + 96, x0 + CP_RX + 2 + 27 * 8 - 1, y0 + 96 + 7)
    was = bmp(cap)
    mnt = (x0 + CP_RX + 2, y0 + 52, x0 + CP_RX + 2 + 63, y0 + 52 + 15)
    mo.to((mnt[0] + mnt[2]) // 2, (mnt[1] + mnt[3]) // 2)
    mo._edge(True)
    time.sleep(0.8)
    mo.to(mnt[0] - 40, (mnt[1] + mnt[3]) // 2, l=True)   # slide off, HELD
    time.sleep(1.0)
    mo._edge(False)
    quiet(m, 20.0)
    check("a slide-off release does NOT mount", bmp(cap) == was)
    mo.to((mnt[0] + mnt[2]) // 2, (mnt[1] + mnt[3]) // 2)
    mo._edge(True)
    time.sleep(0.7)
    mo._edge(False)
    quiet(m, 25.0)
    check("press-and-release on Mount DOES", bmp(cap) != was,
          "(the 'Mounted D: ...' caption row)")

print()
if fails:
    print(f"{len(fails)} FAILED: {', '.join(fails)}")
    sys.exit(1)
print("all pass")
