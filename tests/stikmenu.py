#!/usr/bin/env python3
"""SPEC.md 110.12: Locator's Sticky Apps menu finds the Desk Calendar by its
CARD, and every one of its picks does what it says.

    make && python3 tests/stikmenu.py

One boot of MartyPC's VGA XT on a scratch copy of the SHIPPED 360KB system
disk, which carries STIKAPPS\\DESKCAL.O88 with its card (flags bit 7). The
menu's own state is read out of STIKMENU.DRV's image - its base is the
kernel's mod_tab row, its labels' offsets os88sym's - so every assertion is
about what the module DECIDED, not about pixels:

  A  a press on the 'Sticky Apps' cell loads STIKMENU.DRV and drops a panel
     listing the system disk's THREE Sticky Apps (DESKCAL.O88 and, since
     SPEC.md 110.13 and 110.14, STIKNOTE.O88 and DESKCLOK.O88), each row a
     pick: DESKCAL.O88 on the boot
     volume, its card's name 'Desk Calendar', not running, '0 active';
     Arrange and Hide greyed (no widget), More live;
  B  releasing on its row POSTS the pick and LAUNCHES it: a desk window
     called 'Desk Calendar' comes up, and the module is dropped after;
  C  a press released OFF the panel picks nothing: nothing posted, and the
     module dropped by the track thunk itself;
  D  Hide All hides the widget (visible bit clear, still used), the next
     panel offers Show All, and Show All shows it again;
  E  Arrange puts its frame at W_X = 7 mod 8, at the column's top (MBAR_H +
     4), right of the drive zones' column;
  F  picking the RUNNING row closes it - through app_close_win, so its own
     negotiator removes its Sticky App row from the desktop table.
  G  a second boot with six more Sticky Apps - one-sector stubs, a header
     and a card - lists nine, PAGED (a page smaller than nine and a pager
     row); a release on the pager turns to page 2 and the panel STAYS UP;
     a click off it then closes it with nothing posted.

**BREAK IT ON PURPOSE** (docs/WRITING-TESTS.md 1): clear DESKCAL's
OS88_F_STIK and A fails (no card, nothing listed); make stm_track_x drop the
image whatever the track answered and B fails (the posted act finds
mod_gone); call wm_close_req instead of app_close_win in stm_a_close and F's
row check fails (the negotiator is cleared before the close).
"""
import os
import shutil
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import os88marty   # noqa: E402
import os88ui      # noqa: E402
import os88stik    # noqa: E402
import os88sym     # noqa: E402
import os88geom as geom  # noqa: E402

ROOT = os.path.join(HERE, "..")
BDIR = os.path.join(ROOT, os.environ.get("OS88_BUILD", "build"))
TITLE = "Desk Calendar"
# Every layout constant is read out of the kernel's own assembly
# (os88sym.equates), not copied here: kernel/stikmenu.inc's STE_*/SL_*/SK_*/
# IT_*/SF_* and mod.inc's MODR_SIZE, so the row cannot agree with a stale copy
EQ = os88sym.equates()
MODR_SIZE = EQ["MODR_SIZE"]
STE_SIZE, STE_RUN, STE_FILE, STE_NAME = (EQ[n] for n in (
    "STE_SIZE", "STE_RUN", "STE_FILE", "STE_NAME"))
SL_SIZE, SL_KIND, SL_PARAM, SL_FLAGS, SL_H, SL_Y = (EQ[n] for n in (
    "SL_SIZE", "SL_KIND", "SL_PARAM", "SL_FLAGS", "SL_H", "SL_Y"))
SK_APP, SK_ITEM, SK_PAGE = EQ["SK_APP"], EQ["SK_ITEM"], EQ["SK_PAGE"]
IT_ARR, IT_HIDE, IT_MORE = EQ["IT_ARR"], EQ["IT_HIDE"], EQ["IT_MORE"]
SF_SEL, SF_DIS = EQ["SF_SEL"], EQ["SF_DIS"]
T = 120                         # guest seconds: a floppy scan on a 5150

fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


class Menu:
    """The Sticky Apps menu, driven and read."""

    def __init__(self, m, ui):
        self.m, self.ui = m, ui
        self.mod = EQ["MOD_STIKM"]
        self.off = os88sym.syms()

    def base(self):
        """STIKMENU.DRV's linear base, 0 when it is not held."""
        seg = struct.unpack("<H", bytes(self.m.read(
            self.ui._S("mod_tab") + self.mod * MODR_SIZE, 2)))[0]
        return seg << 4

    def rd(self, name, n):
        return bytes(self.m.read(self.base() + self.off[name], n))

    def b(self, name):
        return self.rd(name, 1)[0]

    def w(self, name):
        return struct.unpack("<H", self.rd(name, 2))[0]

    def cell(self):
        for title, x0, x1, _ in self.ui.menus():
            if title == "Sticky Apps":
                return (x0 + x1) // 2
        raise os88ui.UIError("no 'Sticky Apps' cell: %r"
                             % [c[0] for c in self.ui.menus()])

    def drop(self):
        """Press the cell and hold until the panel is up; the slabs."""
        self.ui.mo.to(self.cell(), geom.MBAR_H // 2)
        self.ui.mo._edge(True)
        # The panel is up when stm_live is 1: set after the draw and cleared
        # before the close, so the last drop's image - a reload lands in the
        # same claim, whose memory still holds its bytes until this one's
        # read overwrites them - always left it 0
        self.ui._wait(lambda: self.base() and self.b("stm_live"),
                      "the Sticky Apps panel to drop", T)
        n = self.b("stm_nsl")
        raw = self.rd("stm_sl", n * SL_SIZE)
        return [dict(kind=raw[i * SL_SIZE + SL_KIND],
                     param=raw[i * SL_SIZE + SL_PARAM],
                     flags=raw[i * SL_SIZE + SL_FLAGS],
                     h=raw[i * SL_SIZE + SL_H],
                     y=struct.unpack_from("<H", raw, i * SL_SIZE + SL_Y)[0])
                for i in range(n)]

    def release_on(self, slabs, k):
        """Move onto slab k with the button held, see it highlighted, let go."""
        x = (self.w("stm_x1") + self.w("stm_x2")) // 2
        y = slabs[k]["y"] + slabs[k]["h"] // 2
        self.ui.mo.to(x, y, l=True)
        self.ui._wait(lambda: self.w("stm_sel") == k,
                      "slab %d highlighted" % k, T)
        self.ui.mo._edge(False)

    def release_off(self):
        self.ui.mo.to(20, self.ui._word("vid_ph") - 40, l=True)
        self.ui.mo._edge(False)

    def gone(self):
        self.ui._wait(lambda: not self.base(), "STIKMENU.DRV dropped", T)


def is_open(ui, title):
    """True while a window called `title` is open (ui.window RAISES when
    there is none)."""
    try:
        return ui.window(title) is not None
    except os88ui.UIError:
        return False


def item(slabs, it):
    return [i for i, s in enumerate(slabs)
            if s["kind"] == SK_ITEM and s["param"] == it][0]


def entries(menu):
    n = menu.b("stm_n")
    raw = menu.rd("stm_ents", n * STE_SIZE)
    out = []
    for k in range(n):
        e = raw[k * STE_SIZE:(k + 1) * STE_SIZE]
        out.append(dict(file=e[STE_FILE:STE_FILE + 13].split(b"\0")[0],
                        run=e[STE_RUN],
                        name=e[STE_NAME:STE_NAME + 24].split(b"\0")[0],
                        vol=e[0]))
    return out


def stik_rows(m, ui):
    """The desktop table's Sticky App rows, by path."""
    sseg = ui._word("sc_seg")
    n = ui._byte("sc_nrows")
    out = []
    for k in range(n):
        r = os88stik.parse_row(bytes(m.read((sseg << 4) + k * os88stik.SC_REC,
                                            os88stik.SC_REC)))
        if r["stik"] and r["vol"] != 0xFF:      # 0xFF: a free row
            out.append(r)
    return out


def stub(name):
    """A one-sector .O88 that is nothing BUT a header and a card - what the
    menu reads to list an App (SPEC.md 110.12). It would not launch, and the
    paging leg never launches it: six of them cost six clusters where six
    copies of the calendar would not fit a 360KB disk."""
    b = bytearray(512)
    b[0:2], b[2], b[3] = b"O8", EQ.get("PKG_FMT", 10), 0x80
    struct.pack_into("<HHH", b, 6, 0x20 + 192, 512, 0)  # entry, image, bss
    b[12:15] = bytes((0xFF, 0xD5, 0xCB))
    b[16:16 + len(name)] = name.encode()
    c = 32
    b[c:c + 4] = b"SA\x01\x00"
    b[c + 4:c + 4 + len(name)] = name.encode()
    b[c + 28:c + 28 + 12] = b"A paging app"
    for r in range(32):
        b[c + 64 + r * 4:c + 68 + r * 4] = b"\x81\x00\x00\x81"
    return bytes(b)


def paging(tmp):
    """G: more Apps than a page holds - a pager, and a turn that keeps the
    panel up."""
    import subprocess
    img = os.path.join(tmp, "paging.img")
    shutil.copy(os.path.join(BDIR, "os8088-360.img"), img)
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    for k in range(6):
        f = os.path.join(tmp, "PAGE%d.O88" % k)
        open(f, "wb").write(stub("Pager %d" % k))
        subprocess.run(["mcopy", "-o", "-i", img, f,
                        "::/STIKAPPS/PAGE%d.O88" % k], check=True, env=env,
                       stdout=subprocess.DEVNULL)
    print("G: nine Sticky Apps page")
    with os88marty.launch(img, machine=os88marty.machine(
            "os8088_xt_vga", None), boot=True) as m:
        ui = os88ui.UI(m)
        ui.up(limit=180.0)
        os88marty.no_saver(m)
        menu = Menu(m, ui)
        slabs = menu.drop()
        n, shown = menu.b("stm_n"), menu.b("stm_shown")
        check(n == 9, "nine listed (%d)" % n)
        check(menu.b("stm_paged") == 1 and shown < n,
              "paged: %d a page of %d" % (shown, n))
        pg = [i for i, s in enumerate(slabs) if s["kind"] == SK_PAGE]
        if check(len(pg) == 1, "one pager row"):
            menu.release_on(slabs, pg[0])
            ui._wait(lambda: menu.b("stm_top") == shown, "the page turned", T)
            check(menu.b("stm_top") == shown and menu.base()
                  and menu.b("stm_live"),
                  "page 2 begins at entry %d and the panel is still up"
                  % menu.b("stm_top"))
            menu.ui.mo.to(20, ui._word("vid_ph") - 40)    # a click OFF it
            menu.ui.mo._edge(True)
            menu.ui.mo._edge(False)
            menu.gone()
            check(ui._word("ui_pcmd") == 0, "...and a click off closes it, "
                  "nothing posted")


def main():
    tmp = tempfile.mkdtemp(prefix="stikmenu-")
    img = os.path.join(tmp, "stikmenu.img")
    shutil.copy(os.path.join(BDIR, "os8088-360.img"), img)
    try:
        with os88marty.launch(img, machine=os88marty.machine(
                "os8088_xt_vga", None), boot=True) as m:
            ui = os88ui.UI(m)
            ui.up(limit=180.0)
            os88marty.no_saver(m)
            menu = Menu(m, ui)
            boot = ui._byte("dsk_bootvol")

            print("A: the panel lists the Desk Calendar by its card")
            slabs = menu.drop()
            ents = entries(menu)
            check(len(ents) == 3 and sorted(e["file"] for e in ents)
                  == [b"DESKCAL.O88", b"DESKCLOK.O88", b"STIKNOTE.O88"],
                  "three Sticky Apps listed, the calendar, the clock and "
                  "Sticky Note "
                  "(%r)" % [e["file"] for e in ents])
            k = [i for i, e in enumerate(ents)
                 if e["file"] == b"DESKCAL.O88"]
            k = k[0] if k else 0
            if ents:
                e = ents[k]
                check(e["file"] == b"DESKCAL.O88" and e["vol"] == boot,
                      "it is DESKCAL.O88 on the boot volume (%r, vol %d)"
                      % (e["file"], e["vol"]))
                check(e["name"] == b"Desk Calendar",
                      "its card names it 'Desk Calendar' (%r)" % e["name"])
                check(e["run"] == 0, "it is not running")
            check(menu.b("stm_nrun") == 0, "'No Sticky Apps active'")
            apps = [i for i, s in enumerate(slabs) if s["kind"] == SK_APP]
            check(len(apps) == 3 and all(slabs[a]["flags"] & SF_SEL
                                         for a in apps),
                  "three app rows, each a pick")
            check(slabs[item(slabs, IT_ARR)]["flags"] & SF_DIS and
                  slabs[item(slabs, IT_HIDE)]["flags"] & SF_DIS and
                  slabs[item(slabs, IT_MORE)]["flags"] & SF_SEL,
                  "Arrange and Hide greyed with no widget, More live")

            print("B: releasing on its row launches it")
            menu.release_on(slabs, apps[k])
            w = ui.wait_window(TITLE, limit=T)
            check(w is not None, "the Desk Calendar is up")
            if w is not None:
                check(w.flags & geom.WF_DESK, "as a desk window (%#06x)"
                      % w.flags)
            menu.gone()
            check(True, "...and STIKMENU.DRV was dropped after the act")

            print("C: a release off the panel picks nothing")
            menu.drop()
            menu.release_off()
            menu.gone()
            check(ui._word("ui_pcmd") == 0, "nothing posted")

            print("D: Hide All, then Show All")
            slabs = menu.drop()
            ents = entries(menu)
            check(ents and ents[k]["run"] == 1 and menu.b("stm_nrun") == 1,
                  "the row is RUNNING now, '1 Sticky App active'")
            menu.release_on(slabs, item(slabs, IT_HIDE))
            menu.gone()
            w = ui.window(TITLE)
            rec = ui._S("wm_wins") + (w.i if w else 0) * geom.WIN_SIZE
            fl = struct.unpack("<H", bytes(m.read(rec + geom.W_FLAGS, 2)))[0]
            check(fl & 1 and not fl & 2, "hidden, not closed (W_FLAGS %#06x)"
                  % fl)
            slabs = menu.drop()
            check(menu.b("stm_nvis") == 0 and menu.b("stm_ndesk") == 1,
                  "the panel counts one desk window, none visible (%d, %d)"
                  % (menu.b("stm_ndesk"), menu.b("stm_nvis")))
            menu.release_on(slabs, item(slabs, IT_HIDE))   # now 'Show All'
            menu.gone()
            fl = struct.unpack("<H", bytes(m.read(rec + geom.W_FLAGS, 2)))[0]
            check(fl & 2, "Show All shows it again (W_FLAGS %#06x)" % fl)

            print("E: Arrange")
            slabs = menu.drop()
            menu.release_on(slabs, item(slabs, IT_ARR))
            menu.gone()
            x = struct.unpack("<H", bytes(m.read(rec + geom.W_X, 2)))[0]
            y = struct.unpack("<H", bytes(m.read(rec + geom.W_Y, 2)))[0]
            ww = struct.unpack("<H", bytes(m.read(rec + geom.W_W, 2)))[0]
            xe = ui._word("vid_band_xe")
            check(x % 8 == 7, "W_X %d is 7 mod 8: the face on the grid" % x)
            check(y == geom.MBAR_H + 4, "at the column's top (W_Y %d)" % y)
            check(x + ww <= xe - 64 + 8, "clear of the drive zones' column "
                  "(right edge %d, band %d)" % (x + ww, xe))

            print("F: picking the running row closes it, row and all")
            check(len(stik_rows(m, ui)) == 1, "its Sticky App row is there")
            slabs = menu.drop()
            apps = [i for i, s in enumerate(slabs) if s["kind"] == SK_APP]
            menu.release_on(slabs, apps[k])
            menu.gone()
            ui._wait(lambda: not is_open(ui, TITLE), "the calendar closed", T)
            check(not is_open(ui, TITLE), "the Desk Calendar is closed")
            check(not stik_rows(m, ui),
                  "its own negotiator removed its Sticky App row")
        paging(tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("stikmenu: %s" % ("PASS" if not fails else
                           "FAIL (%d): %s" % (len(fails), "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
