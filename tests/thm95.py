#!/usr/bin/env python3
"""Does the WINDOWS theme (SPEC.md 76.14) draw, click, menu and refuse as written?

    python3 tests/thm95.py [--legs ABCDE]

Five legs, every one on MartyPC's 4.77 MHz 8088:

  A. VGA, a kernel built to BOOT Windows (`THEMEDARK=3`, a private tree):
     the three caption buttons PIXEL BY PIXEL against the art (lit edge, face,
     dark edge, the X, and the navy ground beside each band - which is where
     SPEC.md 5.4.2.9's Bit Mask defect showed); one title bar's COST off the
     trace, under a ceiling; the click-open menu (a click on the chip leaves it
     up, Esc closes, Up/Down step, Enter runs, a click OFF it closes and goes
     no further, press-drag-release still runs, a right-click leaves a
     context menu up although it opens over an item); maximize, restore and
     minimize by their buttons; a BACKGROUND window's X closing it without a
     raise; and the held button's light CLIPPED where another window covers
     it (SPEC.md 76.14.5).
  B. Hercules, the same kernel: the 1bpp row - a white button face, the black
     X, the dither desktop - and the click-open menu once.
  C. VGA, the SHIPPED kernel: the Theme page's Windows row mounts WIN95.DRV
     and hooks every door; Bright unhooks them all and drops the claim.
  D. VGA, the shipped kernel on a system disk WITHOUT WIN95.DRV: the row is
     refused, Color stands, nothing is claimed, and the toast says why.
  E. VGA, Windows SAVED in SYSTEM.CFG and no WIN95.DRV: the boot comes up
     Color with every door at rest (SPEC.md 76.14.1).

Break it on purpose: swap regions 2 and 5 in wn_hit (A's close leg fails),
drop the evq_drain from wn_stick's .close (A's off-click raises the Disk
window), move wn_stick's click test after its `.drag` test (A's right-click
runs item 0), or take ui_lit_xor's clip out (A's light paints the cover).
"""
import argparse
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path[:0] = [str(HERE), str(ROOT / "tools")]
import os88build                                            # noqa: E402
import os88geom as geom                                     # noqa: E402
import os88marty                                            # noqa: E402
import os88ui                                               # noqa: E402
import dispcp                                               # noqa: E402

VGA = "os8088_xt_vga"
HERC = "os8088_5150_herc_gla"
FAILS = []

# the 16-colour indices the art lands in (SPEC.md 76.14's table)
BLACK, NAVY, TEAL, LGRAY, DGRAY, WHITE = ((0, 0, 0), (0, 0, 170),
                                          (0, 170, 170), (170, 170, 170),
                                          (85, 85, 85), (255, 255, 255))
THM_COLOR, THM_WIN = 2, 3
DOORS = ("wm_draw_title_b", "wm_hit_ttl_b", "wm_grow_glyph_b", "kret_ret",
         "wmz_crect_b")


def fail(msg):
    print("   FAIL: " + msg)
    FAILS.append(msg)


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def px(m, x, y):
    """One rendered pixel, RGB."""
    w, h, data = m.fbuf(0)
    i = (y * w + x) * 3
    return tuple(data[i:i + 3])


def grab(m):
    w, h, data = m.fbuf(0)
    return lambda x, y: tuple(data[(y * w + x) * 3:(y * w + x) * 3 + 3])


def kind(m):
    return m.read(m.sym("thm_kind"), 1)[0]


def modseg(m, eq):
    return u16(m.read(m.sym("mod_tab") + eq["MOD_WIN"] * 4, 2))


def doors(m):
    """thv's five offsets, and whether each is the kernel's resting body."""
    raw = m.read(m.sym("thv"), 20)
    return [u16(raw, i * 4) for i in range(5)], [u16(raw, i * 4 + 2)
                                                for i in range(5)]


def at_rest(m, where):
    offs, segs = doors(m)
    bodies = [m.sym(d) for d in DOORS]          # sym() is LINEAR, so the
    got = [(s << 4) + o for o, s in zip(offs, segs)]    # door's target is too
    if got != bodies:
        fail("%s: a door still points away from the kernel's own body: %s"
             % (where, ["%05X" % g for g in got]))
    return got == bodies


def hooked(m, eq, where):
    seg = modseg(m, eq)
    offs, segs = doors(m)
    if not seg or any(s != seg for s in segs):
        fail("%s: WIN95.DRV is not what the doors point at (module at %04x, "
             "doors at %s)" % (where, seg, ["%04x" % s for s in segs]))
        return False
    return True


def wait(m, cond, what, guest=20.0):
    return os88marty.until(m, lambda _: cond(), what, guest=guest)


# --- A ------------------------------------------------------------------------
def leg_a(eq):
    print("A: VGA, Windows booted")
    t = os88build.tree("THEMEDARK=3").apply()
    with os88ui.boot(t.img("os8088-360.img"), apps=t.img("apps360.img"),
                     machine=VGA) as ui:
        m, mo = ui.m, ui.mo
        if kind(m) != THM_WIN or not hooked(m, eq, "A boot"):
            fail("A: the THEMEDARK=3 kernel did not boot Windows (kind %d)"
                 % kind(m))
            return
        disk = ui.open_drive("B")
        os88marty.settle(m)
        a_pixels(m, disk)
        a_cost(m, ui, disk)
        a_menus(m, mo, ui, disk)
        a_buttons(m, mo, ui, disk)


def a_pixels(m, w):
    """The close button against its art, and the ground either side of it."""
    g = grab(m)
    x1, y1 = w.x + w.w - geom.WN_CLOSE, w.y + geom.WN_BTY
    checks = [((x1, y1), WHITE, "close's top-left: the lit edge"),
              ((x1, y1 + 5), WHITE, "close's left column: the lit edge"),
              ((x1 + 15, y1 + 5), BLACK, "close's right column: the dark edge"),
              ((x1 + 5, y1 + 13), BLACK, "close's bottom row: the dark edge"),
              ((x1 + 2, y1 + 2), LGRAY, "close's face"),
              ((x1 + 4, y1 + 3), BLACK, "the X's top-left stroke"),
              ((x1 + 7, y1 + 6), BLACK, "the X's crossing"),
              ((x1 + 16, y1 + 5), NAVY, "the ground RIGHT of close (5.4.2.9)"),
              ((x1 - 1, y1 + 5), NAVY, "the ground LEFT of close (5.4.2.9)"),
              ((w.x + w.w - geom.WN_MIN - 1, y1 + 5), NAVY,
               "the ground left of minimize (5.4.2.9)")]
    bad = [(what, xy, g(*xy), want) for xy, want, what in checks
           if g(*xy) != want]
    for what, xy, got, want in bad:
        fail("A pixels: %s at %s is %s, want %s" % (what, xy, got, want))
    if not bad:
        print("   the close button is its art, and the ground beside it navy")


def a_cost(m, ui, disk):
    """One title bar's cost, entry to return, off the guest's own stack."""
    ent = m.sym("wm_draw_title")
    st = {"ret": None, "t0": 0}
    spans = []

    def hit(mm, rec):
        r = rec["regs"]
        flat = (r["cs"] << 4) + r["ip"]
        if flat == ent and st["ret"] is None:
            ret = u16(mm.read((r["ss"] << 4) + r["sp"], 2))
            st.update(ret=(r["cs"] << 4) + ret, t0=rec["cycles"])
            mm.breakpoints([{"type": "exec", "addr": ent},
                            {"type": "exec", "addr": st["ret"]}])
        elif st["ret"] is not None and flat == st["ret"]:
            spans.append(rec["cycles"] - st["t0"])
            st["ret"] = None
            mm.breakpoints([{"type": "exec", "addr": ent}])

    a = ui.open_drive("A")
    os88marty.settle(m)
    with os88marty.bp_trace(m, ent, regs=True, on_hit=hit):
        for _ in range(2):
            ui.raise_window(disk)
            os88marty.settle(m)
            ui.raise_window(a)
            os88marty.settle(m)
    ui.close(a)
    if not spans:
        fail("A cost: no title bar was traced")
        return
    ms = statistics.median(spans) / 4772.7
    print("   one title bar: median %.1f ms of %d (a 4.77 MHz 8088)"
          % (ms, len(spans)))
    if ms > 40.0:
        fail("A cost: a title bar takes %.1f ms - over the 40 ms SPEC.md "
             "76.14.11 records as the ceiling" % ms)


def a_menus(m, mo, ui, disk):
    dropd = lambda: m.read(m.sym("menu_dropd"), 1)[0]          # noqa: E731
    sel = lambda: u16(m.read(m.sym("menu_sel"), 2))            # noqa: E731
    mo.click(14, 9)
    wait(m, lambda: dropd() == 1, "the chip menu to STAY UP after a click")
    m.key("Escape")
    wait(m, lambda: dropd() == 0, "Esc to close it")
    mo.click(14, 9)
    wait(m, lambda: dropd() == 1, "the chip menu up again")
    m.key("ArrowDown")
    wait(m, lambda: sel() == 0, "Down to highlight item 0")
    m.key("ArrowDown")
    wait(m, lambda: sel() == 1, "Down again to highlight item 1")
    m.key("ArrowUp")
    wait(m, lambda: sel() == 0, "Up back to item 0")
    m.key("Enter")
    wait(m, lambda: "About os8088" in ui.titles(), "Enter to run About")
    about = ui.window("About os8088")
    ui.close(about)
    print("   click-open, Esc, Up/Down and Enter")

    # A CLICK OFF THE MENU GOES NO FURTHER: aimed at the Disk window's title,
    # which a delivered press would raise
    a = ui.open_drive("A")
    os88marty.settle(m)
    top0 = geom.zorder(m, ui.sym)[-1]
    disk = ui._refresh(disk)
    mo.click(14, 9)
    wait(m, lambda: dropd() == 1, "the chip menu up for the off-click")
    mx2 = u16(m.read(m.sym("menu_x2"), 2))
    tx = max(disk.x + 30, mx2 + 40)         # off the menu, on the title bar
    if not (disk.x < tx < disk.x + disk.w - geom.WN_MIN - 4):
        fail("A menus: no part of the Disk title bar is clear of the menu")
    mo.click(tx, disk.y + 8)
    wait(m, lambda: dropd() == 0, "a click off the menu to close it")
    os88marty.settle(m)
    if geom.zorder(m, ui.sym)[-1] != top0:
        fail("A menus: the click that closed the menu ALSO raised the window "
             "under it - wn_stick's drain did not eat it")
    else:
        print("   a click off the menu closes it and reaches nothing")
    ui.close(a)

    # PRESS-DRAG-RELEASE STILL RUNS AN ITEM
    mo.menu(14, 9, 30, os88geom_item(0))
    wait(m, lambda: "About os8088" in ui.titles(),
         "press-drag-release on the chip menu to run About")
    ui.close(ui.window("About os8088"))
    print("   press-drag-release still runs the item")

    # A RIGHT-CLICK LEAVES THE CONTEXT MENU UP (SPEC.md 76.14.7.1). The popup
    # opens AT the pointer, so a release a pixel or two from the press is on
    # its first item - which used to run it. Released 2px right and 2px down,
    # inside item 0's cell and inside the 4px click radius
    disk = ui._refresh(disk)
    x0, y0 = disk.x + disk.w // 2, disk.y + disk.h // 2
    titles0 = sorted(ui.titles())
    seen = {}

    def aim(mm):
        wait(m, lambda: dropd() == 1, "the context menu to open")
        seen["x1"] = u16(m.read(m.sym("menu_x1"), 2))
        seen["y1"] = u16(m.read(m.sym("menu_y1"), 2))
        return x0 + 2, y0 + 2
    mo.rmenu(x0, y0, x0, y0, aim=aim)
    if (seen.get("x1"), seen.get("y1")) != (x0, y0):
        fail("A context: the popup was not anchored at the press (%s) - the "
             "release is not on item 0 and the leg proves nothing" % seen)
    elif dropd() != 1 or sel() != 0xFFFF:
        fail("A context: a right-click released on item 0 %s"
             % ("ran it" if dropd() != 1 else "left item %d lit" % sel()))
    else:
        m.key("Escape")
        wait(m, lambda: dropd() == 0, "Esc to close the context menu")
        if sorted(ui.titles()) != titles0:
            fail("A context: the windows changed - an item ran")
        else:
            print("   a right-click leaves the context menu up, nothing lit")


def os88geom_item(n):
    return geom.MBAR_H + 1 + n * geom.MENU_ITEM_H + geom.MENU_ITEM_H // 2


def a_buttons(m, mo, ui, disk):
    disk = ui._refresh(disk)
    ui.raise_window(disk)
    disk = ui._refresh(disk)
    r0 = (disk.x, disk.y, disk.w, disk.h)
    mo.click(*geom.button_xy(disk.x, disk.y, disk.w, "max"))
    wait(m, lambda: ui._refresh(disk).y == geom.MBAR_H,
         "the maximize button to zoom the Disk window to the bar")
    z = ui._refresh(disk)
    mo.click(*geom.button_xy(z.x, z.y, z.w, "max"))
    wait(m, lambda: (lambda d: (d.x, d.y, d.w, d.h))(ui._refresh(disk)) == r0,
         "the restore button to put it back at %r" % (r0,))
    print("   maximize, and restore, by the button")

    # THE HELD LIGHT IS CLIPPED (SPEC.md 76.14.5): a background window whose
    # close button is half under the Disk window, pressed on its visible half.
    # About, because it is narrower than a Disk window and none may be dragged
    # off its display
    mo.menu(14, 9, 30, os88geom_item(0))
    wait(m, lambda: "About os8088" in ui.titles(), "About to open")
    other = ui.window("About os8088")
    os88marty.settle(m)
    disk = ui.move_window(disk, 316, 100)
    other = ui._refresh(other)
    other = ui.move_window(other, disk.x - 4 - other.w + geom.WN_CLOSE,
                           disk.y + 30)    # 4..11px of the button showing,
                                           # whatever WF_SNAP rounds it to
    ui.raise_window(disk)
    disk, other = ui._refresh(disk), ui._refresh(other)
    bx1 = other.x + other.w - geom.WN_CLOSE
    by = other.y + geom.WN_BTY + 7
    if not (bx1 + 3 < disk.x < bx1 + 15):
        fail("A clip: the layout did not put the button under the window's "
             "edge (button %d..%d, window at %d)" % (bx1, bx1 + 15, disk.x))
    else:
        before = grab(m)
        cover = [(x, by) for x in range(disk.x + 1, bx1 + 15)]
        mine = (bx1 + 2, by)
        mo.to(*mine)
        mo._edge(True)
        wait(m, lambda: m.read(m.sym("ui_armlit"), 1)[0] == 1,
             "the held button to light")
        after = grab(m)
        if after(*mine) == before(*mine):
            fail("A clip: the visible half of the held button did not light")
        painted = [xy for xy in cover if after(*xy) != before(*xy)]
        if painted:
            fail("A clip: the light inverted %d pixels of the window COVERING "
                 "the button (first %r)" % (len(painted), painted[0]))
        else:
            print("   the held light stops at the covering window's edge")
        mo.to(disk.x + 60, disk.y + 80, l=True)     # slide off: cancel
        wait(m, lambda: m.read(m.sym("ui_armlit"), 1)[0] == 0,
             "the light to go out when the pointer leaves")
        mo._edge(False)
        os88marty.settle(m)
        if all(o.i != other.i for o in ui.windows()):
            fail("A clip: sliding off a held X still closed the window")

    # A BACKGROUND WINDOW'S X CLOSES IT WITHOUT A RAISE
    disk = ui.move_window(disk, 20, 40)
    other = ui.move_window(ui._refresh(other), 300, 220)
    ui.raise_window(disk)               # Disk in front, A behind it
    disk, other = ui._refresh(disk), ui._refresh(other)
    if geom.zorder(m, ui.sym)[-1] == other.i:
        fail("A buttons: could not put the A: window behind")
    raised = []
    bx, by = geom.button_xy(other.x, other.y, other.w, "close")
    if disk.covers(bx, by):
        fail("A buttons: the background close button is covered - the layout "
             "this leg assumes moved")
    with os88marty.bp_trace(m, "wm_front", on_hit=lambda mm, rec:
                            raised.append(rec)):
        mo.click(bx, by)
        wait(m, lambda: all(o.i != other.i for o in ui.windows()),
             "a background window's X to close it")
    if raised:
        fail("A buttons: closing a background window RAISED it first "
             "(wm_front ran %d times)" % len(raised))
    else:
        print("   a background window's X closes it, with no raise")

    # MINIMIZE
    disk = ui._refresh(disk)
    mo.click(*geom.button_xy(disk.x, disk.y, disk.w, "min"))
    wait(m, lambda: not ui._refresh(disk).visible,
         "the minimize button to hide the Disk window")
    print("   minimize by the button")


# --- B ------------------------------------------------------------------------
def leg_b(eq):
    print("B: Hercules, Windows booted")
    t = os88build.tree("THEMEDARK=3").apply()
    with os88ui.boot(t.img("os8088-360.img"), apps=t.img("apps360.img"),
                     machine=HERC) as ui:
        m, mo = ui.m, ui.mo
        if kind(m) != THM_WIN or not hooked(m, eq, "B boot"):
            fail("B: Windows did not survive a 1bpp primary (kind %d)"
                 % kind(m))
            return
        w = ui.open_drive("B")
        os88marty.settle(m)
        _, _, rows = m.vram()           # the framebuffer, in guest pixels:
        x1, y1 = w.x + w.w - geom.WN_CLOSE, w.y + geom.WN_BTY   # the RENDERED
        for xy, lit, what in (((x1 + 2, y1 + 2), True, "the white face"),  # frame
                              ((x1 + 7, y1 + 6), False, "the black X"),    # is
                              ((x1 + 15, y1 + 5), False, "the dark edge"), # offset
                              ((x1 + 3, y1), True, "the lit top row")):
            if bool(rows[xy[1]][xy[0]]) != lit:
                fail("B pixels: %s at %s is %s" % (what, xy,
                                                    "lit" if not lit else "dark"))
        # THE 1bpp DESKTOP IS THE DITHER (SPEC.md 76.14.6, the owner's pick):
        # a 4x4 patch of open desktop lit in exactly half its pixels, where a
        # black ground would light none of them
        lit = sum(1 for y in range(200, 204) for x in range(696, 700)
                  if rows[y][x])
        if lit != 8:
            fail("B desktop: %d of 16 pixels lit - the 1bpp Windows ground is "
                 "the 50%% dither, 8 of 16" % lit)
        mo.click(14, 9)
        wait(m, lambda: m.read(m.sym("menu_dropd"), 1)[0] == 1,
             "the chip menu to stay up on 1bpp")
        m.key("Escape")
        wait(m, lambda: m.read(m.sym("menu_dropd"), 1)[0] == 0, "Esc")
        print("   the 1bpp row draws its buttons, and the menu stays up")


# --- C, D, E ------------------------------------------------------------------
def fixture(folder, name, win95, saved):
    files = [os88build.at("build/ctrl.drv")]
    if win95:
        files.append(os88build.at("build/win95.drv"))
    if saved:
        d = folder / name
        d.mkdir()
        cfg = d / "system.cfg"
        # 'TH' ver 3, five bytes (driver.inc's CFG_DATA): the theme, the zoom,
        # and the screen saver's three - minutes 0, which is never
        cfg.write_bytes(b"O88CFG\0\0" + (3).to_bytes(2, "little")
                        + b"TH" + bytes([3, 5, THM_WIN, 1, 1, 5, 0]) + b"\0\0")
        files.append(str(cfg))
    image = folder / (name + ".img")
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", str(image),
                    "--size", "360", "--boot", os88build.at("build/boot360.bin"),
                    "--kernel", os88build.at("build/kernel.sys"), *files],
                   check=True, stdout=subprocess.DEVNULL, cwd=str(ROOT))
    return str(image)


def pick(m, mo, row):
    wx, wy = dispcp._cp_win(m, m.sym)
    mo.click(wx + 1 + dispcp.CP_RX + dispcp.CP_PGX + 6,
             wy + geom.TITLE_H + 1 + 18 + row * 16 + 6, settle=0)


def shipped():
    """C, D and E are about the SHIPPED kernel: put the paths and the symbol
    reader back on the shared tree, which A and B moved to THEMEDARK=3's"""
    os88build.plain().apply()


def leg_c(eq, folder):
    shipped()
    print("C: the Theme page mounts and drops WIN95.DRV")
    with os88ui.boot(fixture(folder, "c", True, False), machine=VGA) as ui:
        m, mo = ui.m, ui.mo
        at_rest(m, "C boot")
        dispcp.open_panel(m, mo, m.sym, os88marty.settle, page=5)
        pick(m, mo, 3)
        wait(m, lambda: kind(m) == THM_WIN, "the Windows row to take")
        hooked(m, eq, "C Windows")
        pick(m, mo, 0)
        wait(m, lambda: kind(m) == 0, "the Bright row to take")
        if modseg(m, eq):
            fail("C: Bright left WIN95.DRV claimed")
        at_rest(m, "C Bright")
        print("   Windows hooks five doors to the module, Bright puts them back")


def leg_d(eq, folder):
    shipped()
    print("D: Windows with no WIN95.DRV on the disk")
    with os88ui.boot(fixture(folder, "d", False, False), machine=VGA) as ui:
        m, mo = ui.m, ui.mo
        dispcp.open_panel(m, mo, m.sym, os88marty.settle, page=5)
        before = m.read(m.sym("toast_buf"), 24).split(b"\0")[0]
        pick(m, mo, 3)
        wait(m, lambda: m.read(m.sym("toast_buf"), 24).split(b"\0")[0]
             != before, "the refusal's toast")
        toast = m.read(m.sym("toast_buf"), 24).split(b"\0")[0]
        if kind(m) != THM_COLOR:
            fail("D: a refused Windows left kind %d, not Color" % kind(m))
        if modseg(m, eq):
            fail("D: a refused Windows left a claim")
        at_rest(m, "D refused")
        if not toast.startswith(b"WIN95.DRV"):
            fail("D: the refusal's toast is %r" % toast)
        print("   refused: Color stands, nothing claimed, toast %r"
              % toast.decode("latin-1"))


def leg_e(eq, folder):
    shipped()
    print("E: Windows saved, no WIN95.DRV, at boot")
    with os88ui.boot(fixture(folder, "e", False, True), machine=VGA) as ui:
        m = ui.m
        if kind(m) != THM_COLOR:
            fail("E: the boot came up kind %d, not Color" % kind(m))
        if modseg(m, eq):
            fail("E: the boot left a claim")
        at_rest(m, "E boot")
        print("   the boot came up Color with every door at rest")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--legs", default="ABCDE")
    a = ap.parse_args()
    eq = __import__("os88sym").equates()
    if "MOD_WIN" not in eq:
        sys.exit("thm95: this kernel has no MOD_WIN - kern_small? The Windows "
                 "theme is kern_big's alone (SPEC.md 76.14.1)")
    with tempfile.TemporaryDirectory(prefix="os88-thm95-") as tmp:
        folder = Path(tmp)
        for leg in a.legs:
            {"A": leg_a, "B": leg_b}.get(leg, lambda e: None)(eq)
            if leg in "CDE":
                {"C": leg_c, "D": leg_d, "E": leg_e}[leg](eq, folder)
    print()
    for f in FAILS:
        print("thm95: FAIL: " + f)
    if FAILS:
        return 1
    print("thm95: the Windows theme draws, clicks, menus and refuses as "
          "SPEC.md 76.14 says - PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
