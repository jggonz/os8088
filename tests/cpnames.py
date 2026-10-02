#!/usr/bin/env python3
"""DO THE CONTROL PANEL'S LIST NAMES AND PAGE HEADINGS LETTER? (SPEC.md 2.8.6.1,
31.1, 31.9)

    python3 tests/cpnames.py [machine]
    python3 tests/cpnames.py hdd        # ...and a DRIVER's row, on os8088_xt_hdd

Kernel size pass 8 moved the item table and every static list name into
CTRL.DRV's image, and made the page HEADING - which is the same string - the
dispatcher's to draw rather than each page's. Both halves fail SILENTLY: a
name read through DS instead of CS letters whatever sits at that offset in
KERNEL_SEG, and a heading the dispatcher forgot is a blank strip on a page
that otherwise draws. Neither moves a byte of `kernsize`, and every row that
opens the panel by RECORD ([cp_sel]) passes either way.

So this asserts TEXT, rendered on the host from the kernel's own glyph table
([font_seg]:[font_base], SPEC.md 6) and searched for in the framebuffer - the instrest.py method:

  * every static page SHOWN on this machine has its name in the LEFT pane,
    black on white (or white on the selection bar when it is the page open);
  * opening each one puts that same name at the pane's heading position,
    black on white.

Break it on purpose: drop the `call cp_stage` out of CPSTAGEX (or the `cs:`
out of it) and every name letters rubbish; delete the heading block from
cp_item_paint and the second half goes red on every page.
"""
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, ROOT + "/tools")
sys.path.insert(0, ROOT + "/tests")
import os88marty                                           # noqa: E402
import os88mouse                                           # noqa: E402
import os88sym                                             # noqa: E402
import dispcp                                              # noqa: E402

S = os88sym.linear
HDD = len(sys.argv) > 1 and sys.argv[1] == "hdd"
MACHINE = ("os8088_xt_hdd" if HDD else
           sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla")
DUMP = os.environ.get("CPNAMES_DUMP")     # a directory: the panel, per page,
                                         # for an A/B against another build

FONT_FIRST, FONT_N = 32, 95
TITLE_H = 18
CP_IX, CP_I0Y, CP_IROWH, CP_ITDY, CP_RX = 6, 6, 14, 2, 96
CP_PMX, CP_PHY = 2, 6
CP_DIVX = 90
CP_IDRV, CP_DBY1, CP_DROWH, CP_RX = 2, 20, 26, 96  # the Drivers page (hddcp.py)
HDD_ROW, HDD_PAGE = 1, "Hard Drive"     # drv_tab row 1; drivers/hdd's DSV_CPNAME

# kernel/ctrl.inc's cp_items, kern_big's order (SPEC.md 31.14.2).
NAMES = ["Scheduler", "Date/Time", "Drivers", "Display", "Sound", "Theme",
         "Dock", "Floppy"]


def render(text, tab):
    out = []
    for r in range(8):
        row = []
        for ch in text:
            b = tab[(ord(ch) - FONT_FIRST) * 8 + r]
            row.extend((b >> bit) & 1 for bit in range(7, -1, -1))
        out.append(row)
    return out


def find(rows, x0, y0, w, h, want, inverse=False):
    """`want` (rows of INK bits) anywhere in the rect. A lit pixel is WHITE, so
    black ink on white reads 0; white on the black selection bar reads 1."""
    gh, gw = len(want), len(want[0])
    for y in range(y0, y0 + h - gh + 1):
        for x in range(x0, x0 + w - gw + 1):
            for r in range(gh):
                row, wr = rows[y + r], want[r]
                if any(row[x + c] != (wr[c] if inverse else 1 - wr[c])
                       for c in range(gw)):
                    break
            else:
                return x, y
    return None


def main():
    fails = []
    with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                          machine=MACHINE) as m:
        os88marty.no_saver(m)
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        seg = int.from_bytes(m.read(S("font_seg"), 2), "little")
        off = int.from_bytes(m.read(S("font_base"), 2), "little")
        tab = m.read(seg * 16 + off, FONT_N * 8)   # where the renderers read
        dispcp.open_panel(m, mo, S, os88marty.settle, page=None)
        os88marty.settle(m)
        hide = m.read(S("cp_hide"), 1)[0]
        shown = [r for r in range(len(NAMES)) if not hide & (1 << r)]
        print("  static records shown: %s" % [NAMES[r] for r in shown])
        lettered = set()
        for rec in shown:
            name = NAMES[rec]
            dispcp.open_panel(m, mo, S, os88marty.settle, page=rec)
            os88marty.settle(m)
            wx, wy = dispcp._cp_win(m, S)
            cx, cy = wx + 1, wy + TITLE_H + 1
            _w, _h, rows = m.vram()
            want = render(name, tab)
            # the heading, at the pane's own (CP_PMX, CP_PHY)
            at = find(rows, cx + CP_RX + CP_PMX, cy + CP_PHY - 1,
                      len(name) * 8 + 2, 10, want)
            print("  %-10s heading %s" % (name, at))
            if at is None:
                fails.append("%s: no heading lettered at the pane's top" % name)
            # the list: every OTHER shown name, black on white. The open
            # page's own row is on the selection bar, and white-on-black is
            # font_run's inverse arm, which is not this row's subject. The
            # list SCROLLS (SPEC.md 31.1.5), so a name need only letter on
            # some page: the ones scrolled out of view are not asked for
            for other in shown:
                if other != rec and find(rows, cx + CP_IX - 1, cy + CP_I0Y,
                                         CP_DIVX - CP_IX, CP_IROWH * 8,
                                         render(NAMES[other], tab)):
                    lettered.add(other)
            if DUMP:
                with open(os.path.join(DUMP, "page%d.txt" % rec), "w") as f:
                    for y in range(wy, wy + 152):
                        f.write("".join("#" if rows[y][x] else "."
                                        for x in range(wx, wx + 321)) + "\n")
        for r in shown:
            print("  %-10s list    %s" % (NAMES[r], "lettered" if r in lettered
                                         else "NOT FOUND"))
            if r not in lettered:
                fails.append("%s: its list row never lettered the name"
                             % NAMES[r])
        if HDD:
            # A DRIVER's list name is staged too - cp_drv_name, out of the
            # driver's own segment into cp_sbuf - and it is a different stager
            # from a static row's. Tick the hard disk in on the Drivers page
            # (hddcp.py's opening) and its page's row must letter.
            dispcp.open_panel(m, mo, S, os88marty.settle, page=CP_IDRV)
            wx, wy = dispcp._cp_win(m, S)
            mo.click(wx + 1 + CP_RX + 40, wy + TITLE_H + 1 + CP_DBY1
                     + HDD_ROW * CP_DROWH + CP_DROWH // 2)
            os88marty.settle(m)
            nst = m.read(S("cp_nst"), 1)[0]
            _w, _h, rows = m.vram()
            cx, cy = wx + 1, wy + TITLE_H + 1
            at = find(rows, cx + CP_IX - 1, cy + CP_I0Y, CP_DIVX - CP_IX,
                      CP_IROWH * 8, render(HDD_PAGE, tab))
            print("  %-10s list    %s (a driver's page, row %d)"
                  % (HDD_PAGE, at, nst))
            if at is None:
                fails.append("the hard disk's page row does not letter %r"
                             % HDD_PAGE)
    for f in fails:
        print("FAIL: " + f)
    print("cpnames: %s" % ("FAIL" if fails else "ok"))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
