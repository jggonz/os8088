#!/usr/bin/env python3
"""SPEC.md 110.13: Sticky Note - the pad on the desk, the editor behind it,
the state file between them, and the papers on a colour screen.

    make && python3 tests/stiknote.py [--leg herc|vga]

The SHIPPED 360KB system disk copied to a scratch file, on MartyPC. Every
coordinate is the package's own, out of tools/os88stik.py's STIKNOTE_GEO /
STIKNOTE_ED, which t_stik holds to snp_geos and sne_geos; the state file is
flushed off the emulated drive and parsed by tools/os88stik.py, whose
stiknote_wrap() is the walk's mirror.

herc (default) - a 5150 with a Hercules card: the MID layout, 1bpp pixels
  A  launched from A:/STIKAPPS, the pad is a DESK window of the MID layout,
     and its first beat writes STIKNOTE.DAT: the welcome note, alone, on the
     desk, made once.
  B  THE STACK: the sheet's top edge is ink across the sheet and the stack's
     right edge down the card; the two notches - the card's top right and
     bottom left - are the DESKTOP's ground (both lit and unlit pixels: the
     dither), not paper; the turned corner's diagonal is ink and the corner
     past it paper.
  C  a press on a box on the pad ticks it: the box is the TICKED record and
     the file has the marker SN_BOX1.
  D  the arrow opens the EDITOR: a second, titled window, not a desk one.
     New, a title, Enter, `[ ]` and a line, Enter and a second: the file has
     the note `Groceries` with two OPEN boxes - Enter on a box continues the
     list, and `[ ]` becomes the marker (the space after it swallowed).
  E  Ctrl+T on the second line takes its box away.
  F  Delete, and Proceed on the question: one note again.
  G  `On desktop` off hides the pad and clears the flag; the editor's close
     box then closes the PROGRAM, and its Sticky App row is gone from
     SYSTEM.CFG.
  H  launched again with the pad off the desk, the editor is what opens and
     the pad stays hidden.
  I  the Color menu is GREYED on this 1bpp screen (SPEC.md 47), each item
     saying why.

vga - an XT with a VGA: the TALL layout and the papers
  J  the pad's sheet is YELLOW (the default paper) and its text black; the
     editor's page yellow and its list white.
  K  Color > Pink: the pad's sheet and the editor's page are pink, the file
     says so, and the list is still white.

**BREAK IT ON PURPOSE**: take sn_setpen's call out of snp_pcard and J reads
white; draw the notches as paper (snp_put's titled arm on a desk window) and
B fails; drop the marker insert from sne_kenter's box arm and D's file reads
`eggs` without a box.
"""
import os
import shutil
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import os88ui      # noqa: E402
import os88marty   # noqa: E402
import os88geom as geom   # noqa: E402
import os88flush   # noqa: E402
import os88stik    # noqa: E402

ROOT = os.path.join(HERE, "..")
BDIR = os.path.join(ROOT, os.environ.get("OS88_BUILD", "build"))
TITLE = "Sticky Note"
S = os88stik
STIK_ST = S.stik_st_off(S.STIKNOTE_STIK_BSS, S.STIKNOTE_PAYLEN)
STIK_S_DIRTY = 16
BOX9 = (0xFF80, 0x8080, 0x8080, 0x8080, 0x8080, 0x8080, 0x8080, 0x8080,
        0xFF80)                 # snpad.inc's sn_box9, and its ticked twin
TICK9 = (0xFF80, 0x8080, 0x8280, 0x8680, 0xCC80, 0xB880, 0x9080, 0x8080,
         0xFF80)
YELLOW, PINK, WHITE, BLACK = (255, 255, 85), (255, 85, 255), \
    (255, 255, 255), (0, 0, 0)

fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def image_size():
    b = open(os.path.join(BDIR, "stiknote.bin"), "rb").read()
    return struct.unpack_from("<H", b, 8)[0]


class Note(object):
    """The running Sticky Note: its two windows, its bss, its state file."""

    def __init__(self, ui, tmp):
        self.ui, self.m, self.tmp = ui, ui.m, tmp
        self.image = image_size()
        self.woff = S.widget_off(S.STIKNOTE_STIK_BSS, S.STIKNOTE_PAYLEN)
        self.pad_i = None
        self.wseg = None

    def launch(self, expect_editor=False):
        ui = self.ui
        w = ui.path("A:/STIKAPPS/STIKNOTE.O88")
        check(w is not None and w.title == TITLE, "Sticky Note opened")
        for o in list(ui.windows()):        # the folder windows go: the pad is
            if o.title != TITLE and o.visible:     # UNDER every window
                ui.close(o)
        pads = [o for o in ui.windows() if o.title == TITLE
                and o.flags & geom.WF_DESK]
        if not pads and not expect_editor:
            raise SystemExit("no desk window called %r: %r"
                             % (TITLE, ui.windows()))
        if pads:
            self.pad_i = pads[0].i
            self.wseg = struct.unpack_from("<H", self.m.read(
                ui._S("wm_wins") + pads[0].i * geom.WIN_SIZE + geom.W_SEG,
                2))[0]
        os88marty.guest_sleep(self.m, 1.5)      # the first beat
        return w

    # --- state ---------------------------------------------------------------
    def bss(self, off, n=1):
        return self.m.readseg(self.wseg, self.image + off, n)

    def word(self, off):
        return struct.unpack("<H", bytes(self.bss(off, 2)))[0]

    def li(self):
        return self.bss(self.woff["stik_li"])[0]

    def pad(self):
        for o in self.ui.windows():
            if o.i == self.pad_i:
                return o
        return None

    def editor(self):
        """The editor: our titled window - the OLDEST of them, as an alert
        (os88ui_ask's, captioned with our title) is newer."""
        for o in self.ui.windows():
            if o.title == TITLE and not o.flags & geom.WF_DESK \
                    and o.w > 300:
                return o
        return None

    def g(self):
        return S.STIKNOTE_GEO[self.li()]

    def e(self):
        return S.STIKNOTE_ED[self.li()]

    def face(self):
        p = self.pad()
        return p.x + 1, p.y

    def at(self, x, y):
        fx, fy = self.face()
        return fx + x, fy + y

    def ed_at(self, x, y):
        cx, cy = self.editor().content[:2]
        return cx + x, cy + y

    def press(self, pt):
        self.ui.mo.click(pt[0], pt[1], settle=0)
        os88marty.pace(self.m, 1.0)

    def raw(self, path):
        out = os.path.join(self.tmp, "flushed.img")
        self.m.pause()
        try:
            self.m.flush(0, out)
        finally:
            self.m.run()
        v = os88flush.Volume(open(out, "rb").read())
        try:
            return v.read(path)
        except Exception:
            return None

    def saved(self, what):
        """Wait for the save an edit owes, then the state file off A:."""
        os88marty.guest_sleep(self.m, 1.0)
        if self.wseg is not None and self.pad() is not None:
            self.ui._wait(lambda: not self.bss(STIK_ST)[0] & STIK_S_DIRTY,
                          what, 60, snapshot=lambda: "stik_st = %#x"
                          % self.bss(STIK_ST)[0])
        d = self.raw("SYSTEM/APPDATA/STIKNOTE.DAT")
        if d is None:
            return None
        p = S.parse_dat(d, S.STIKNOTE_PAYLEN, S.STIKNOTE_PAYVER)
        return S.parse_stiknote(p["payload"])

    def box_line(self, note, k):
        """The pad's body line (index) of note `note`'s paragraph k."""
        body = note["text"].partition("\n")[2]
        line = 0
        for i, para in enumerate(body.split("\n")):
            if i == k:
                return line
            line += len(S.stiknote_wrap(para))
        raise SystemExit("no paragraph %d" % k)

    def box_xy(self, line):
        g = self.g()
        return self.at(S.STIKNOTE_BX + 4, g["B0"] + g["P"] * line + 3)


def rec_bad(img, x, y, rec, w):
    bad = []
    for r, bits in enumerate(rec):
        for c in range(w):
            ink = bool(bits & (0x8000 >> c))
            if (not img[y + r][x + c]) != ink:
                bad.append((x + c, y + r))
    return bad


def herc(tmp):
    img = os.path.join(tmp, "os8088-360.img")
    shutil.copy(os.path.join(BDIR, "os8088-360.img"), img)
    with os88ui.boot(img, machine="os8088_5150_herc_gla") as ui:
        n = Note(ui, tmp)
        m = n.m
        print("A: the launch and the first beat")
        n.launch()
        check(n.li() == 1, "Hercules takes the MID layout (stik_li %d)"
              % n.li())
        st = n.saved("the first beat's save")
        check(st is not None and st["n"] == 1 and st["notes"][0]["title"]
              == "Welcome" and st["desk"] and st["init"],
              "STIKNOTE.DAT: the welcome note, alone, on the desk (%r)"
              % (st and [x["title"] for x in st["notes"]]))
        g = n.g()
        H = g["H"]

        def frame():
            ui.mo.to(20, 300)
            os88marty.pace(m, 1.0)
            return m.vram("herc")[2]

        print("B: the stack, the notches and the turned corner")
        f = frame()
        fx, fy = n.face()
        top = [not f[fy][fx + x] for x in range(S.STIKNOTE_FACE_W
                                                 - S.STIKNOTE_S)]
        check(all(top), "the sheet's top edge is ink across 188 columns")
        right = [not f[fy + y][fx + S.STIKNOTE_FACE_W - 1]
                 for y in range(S.STIKNOTE_S, H)]
        check(all(right), "the stack's right edge is ink down the card")
        for what, xs, ys in (("top right", range(188, 192), range(0, 4)),
                             ("bottom left", range(0, 4),
                              range(H - 4, H))):
            px = [f[fy + y][fx + x] for y in ys for x in xs]
            check(any(px) and not all(px), "the %s notch is the desktop's "
                  "dither, not paper (%d of 16 lit)" % (what, sum(px)))
        K = g["K"]
        r0 = H - S.STIKNOTE_S - K
        diag = [not f[fy + r0 + r][fx + 187 - r] for r in range(K)]
        past = [f[fy + r0 + r][fx + 187] for r in range(2, K)]
        check(all(diag) and all(past), "the turned corner: its diagonal ink, "
              "the corner past it paper")

        print("C: a box ticked on the pad")
        note = st["notes"][0]
        line = n.box_line(note, 1)          # `Type [ ] for a box`
        n.press(n.box_xy(line))
        st = n.saved("the tick's save")
        body = st["notes"][0]["body"]
        check(body[1] == (True, "Type [ ] for a box"), "the file has it "
              "TICKED (%r)" % (body[1],))
        f = frame()
        bx, by = n.at(S.STIKNOTE_BX, g["B0"] + g["P"] * line - 1)
        bad = rec_bad(f, bx, by, TICK9, 9)
        check(not bad, "the box is the ticked record, every pixel (%d off)"
              % len(bad))

        print("D: the editor, a new note and its boxes")
        n.press(n.at(S.STIKNOTE_OPENX + 6, g["OY"] + 4))
        ui._wait(lambda: n.editor() is not None and n.editor().visible,
                 "the editor to open", 30, snapshot=ui.titles)
        e = n.e()
        check(not n.editor().flags & geom.WF_DESK, "the editor is a titled "
              "window, not a desk one")
        n.press(n.ed_at(S.STIKNOTE_ED_NEW[0] + 10, e["BY"] + 4))
        os88marty.pace(m, 1.0)
        for s in ("Groceries\n", "[ ]milk\n", "eggs"):
            m.type_text(s)
            os88marty.pace(m, 0.8)
        st = n.saved("the new note's save")
        check(st["n"] == 2 and st["cur"] == 1, "two notes, the new one "
              "current (%d, %d)" % (st["n"], st["cur"]))
        check(st["notes"][1]["text"] == "Groceries\n\x01milk\n\x01eggs",
              "Groceries: two open boxes (%r)" % st["notes"][1]["text"])

        print("E: Ctrl+T")
        m.ctrl("KeyT")
        os88marty.pace(m, 0.8)
        st = n.saved("Ctrl+T's save")
        check(st["notes"][1]["text"] == "Groceries\n\x01milk\neggs",
              "the second line's box is gone (%r)" % st["notes"][1]["text"])

        print("F: Delete, and Proceed")
        n.press(n.ed_at(S.STIKNOTE_ED_DEL[0] + 10, e["BY"] + 4))
        ui._wait(lambda: len([w for w in ui.windows() if w.title == TITLE])
                 == 3, "the question to go up", 30, snapshot=ui.titles)
        q = [w for w in ui.windows() if w.title == TITLE
             and not w.flags & geom.WF_DESK and w.i != n.editor().i]
        q = q[-1] if q else None
        if q is None:                   # (the editor is the older record)
            raise SystemExit("no question window: %r" % ui.windows())
        # os88ui_arect: two 72-px buttons 12 apart, centred on W_W, at the
        # content's row 46 - Proceed, ADANGER's index 1 (Enter is Cancel)
        row = q.x + (q.w - (2 * 72 + 12)) // 2
        n.press((row + 72 + 12 + 36, q.y + geom.TITLE_H + 46 + 6))
        ui._wait(lambda: len([w for w in ui.windows() if w.title == TITLE])
                 == 2, "the question to be answered", 30, snapshot=ui.titles)
        st = n.saved("the delete's save")
        check(st["n"] == 1 and st["notes"][0]["title"] == "Welcome",
              "one note again (%r)" % [x["title"] for x in st["notes"]])

        print("G: off the desk, and the editor's close")
        n.press(n.ed_at(S.STIKNOTE_ED_CBX + 4, e["CBY"] + 4))
        ui._wait(lambda: not n.pad().visible, "the pad to hide", 30)
        st = n.saved("the flag's save")
        check(not st["desk"], "the file says off the desk")
        ui.close(n.editor())
        ui._wait(lambda: not [w for w in ui.windows() if w.title == TITLE],
                 "the program to close", 30, snapshot=ui.titles)
        cfg = n.raw("SYSTEM/SYSTEM.CFG")
        rows = [r for r in (S.stik_rows(cfg) if cfg else [])
                if r["path"].endswith("STIKNOTE.O88")]
        check(not rows, "its Sticky App row is gone (%r)" % rows)

        print("H: launched off the desk")
        n.launch(expect_editor=True)
        ui._wait(lambda: n.editor() is not None and n.editor().visible,
                 "the editor to open", 30, snapshot=ui.titles)
        os88marty.guest_sleep(m, 2.5)
        pads = [o for o in ui.windows() if o.title == TITLE
                and o.flags & geom.WF_DESK]
        check(pads and not pads[0].visible, "the pad stays hidden")

        print("I: the Color menu, on 1bpp")
        ui.raise_window(n.editor())
        col = [c for c in ui.menus() if c[0] == "Color"]
        its = col[0][3] if col else []
        check(len(its) == 5 and not any(on for _, on in its)
              and all(t.endswith("(color screen)") for t, _ in its),
              "five papers, every one greyed with why (%r)" % its)


def px(fb, x, y):
    w, h, data = fb
    i = (y * w + x) * 3
    return tuple(data[i:i + 3])


def vga(tmp):
    img = os.path.join(tmp, "os8088-360.img")
    shutil.copy(os.path.join(BDIR, "os8088-360.img"), img)
    with os88ui.boot(img, machine="os8088_xt_vga") as ui:
        n = Note(ui, tmp)
        m = n.m
        n.launch()
        check(n.li() == 0, "VGA takes the TALL layout (stik_li %d)" % n.li())
        g = n.g()

        print("J: the papers - yellow, black ink, a white list")
        ui.mo.to(20, 460)
        os88marty.pace(m, 1.0)
        fb = m.fbuf()
        fx, fy = n.face()
        check(px(fb, fx + 100, fy + g["B0"] + g["P"] * 7 + 4) == YELLOW,
              "the pad's sheet is yellow (%r)"
              % (px(fb, fx + 100, fy + g["B0"] + g["P"] * 7 + 4),))
        check(px(fb, fx, fy) == BLACK, "...its edge black")
        n.press(n.at(S.STIKNOTE_OPENX + 6, g["OY"] + 4))
        ui._wait(lambda: n.editor() is not None and n.editor().visible,
                 "the editor to open", 30, snapshot=ui.titles)
        ui.mo.to(620, 460)
        os88marty.pace(m, 1.0)
        fb = m.fbuf()
        e = n.e()
        page = n.ed_at(S.STIKNOTE_ED_LW + 100, e["TBH"] + 120)
        lst = n.ed_at(60, e["TBH"] + 120)
        check(px(fb, *page) == YELLOW, "the editor's page is yellow (%r)"
              % (px(fb, *page),))
        check(px(fb, *lst) == WHITE, "...its list white (%r)"
              % (px(fb, *lst),))

        print("K: Color > Pink")
        ui.menu_pick("Color", "  Pink")    # (two cells kept for the `*`)
        os88marty.pace(m, 1.0)
        st = n.saved("the paper's save")
        check(st["notes"][0]["paper"] == "pink", "the file says pink")
        ui.mo.to(620, 460)
        os88marty.pace(m, 1.0)
        fb = m.fbuf()
        check(px(fb, *page) == PINK, "the page is pink (%r)"
              % (px(fb, *page),))
        check(px(fb, *lst) == WHITE, "...the list still white")
        # the pad, under the editor: read where the editor does not cover it
        ed = n.editor()
        pts = [n.at(x, y) for x in (20, 100, 170) for y in
               (g["B0"] + g["P"] * 7 + 4, g["FY"] - 6)]
        pts = [p for p in pts if not ed.covers(*p)]
        check(pts and all(px(fb, *p) == PINK for p in pts),
              "the pad's sheet is pink (%r)" % [px(fb, *p) for p in pts])


def main():
    leg = "herc"
    if "--leg" in sys.argv:
        leg = sys.argv[sys.argv.index("--leg") + 1]
    tmp = tempfile.mkdtemp(prefix="stiknote-")
    try:
        {"herc": herc, "vga": vga}[leg](tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("stiknote %s: %s (%d failure(s))"
          % (leg, "ok" if not fails else "FAILED", len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
