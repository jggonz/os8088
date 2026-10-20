#!/usr/bin/env python3
"""SPEC.md 26.9: OSAPI_DESK_ITEM from a PACKAGE, end to end on an XT.

    make && make deskitem && python3 tests/deskitem.py [--shots DIR]

The shipped 360KB system disk in A: and `make deskitem`'s scratch disk in B:,
which carries one package, DESKITEM.O88, whose File menu has Add and Remove.
Add hands the kernel a LINK record (SPEC.md 26.8.1) to the package itself and
asks for cell 5; Remove takes it off again. Every claim is asked of the
GUEST'S OWN STATE:

  A  Add makes ONE link: zone DESK_SC0 is PLACED in cell 5, the one it asked
     for; its row names volume 1, kind 1 and the path `DESKITEM.O88`, and the
     record the package handed over was HOSTILE on purpose - its caption fills
     all 13 bytes with no NUL and the picture's header is junk - so the row's
     caption must read `DESK ITEM AB` and its header `1, 16`, which only the kernel can have put
     there. SYSTEM.CFG on A: carries the trailer.
  B  a double-click on the link launches the package it points at - a second
     DeskItem window.
  C  a Remove whose zone no longer holds THIS link is refused: the row's path
     is changed under the package - a zone is reused, so that is what the
     user's own shortcut in it looks like - and Remove must answer CF = 1
     (`di_res` 'r') and leave the row where it is.
  D  Remove takes it off: the cell is nothing, the claim is gone, and
     SYSTEM.CFG has no trailer again.

**BREAK IT ON PURPOSE** (docs/WRITING-TESTS.md 1): take the header stamp out
of sc_m_add and A fails on `1, 16`; take the wish out (store `DSL_PIN | cell`
without DSL_GONE, so the reflow never places it) and A fails on cell 5; make
sc_m_pdel jump straight into sc_m_del and C fails on the row going.

The floppies are COPIED to a scratch directory first: the guest writes
SYSTEM.CFG, and the shipped images must not be the ones it writes to.
"""
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import os88ui      # noqa: E402
import os88geom as geom   # noqa: E402
import os88fat     # noqa: E402
import os88marty   # noqa: E402

ROOT = os.path.join(HERE, "..")
SC_REC = geom.SC_REC
DESK_SC0 = 9
DSL_SLOT, DSL_PIN, DSL_GONE = 0x3F, 0x40, 0x80

fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def cstr(b):
    return bytes(b).split(b"\0")[0].decode("latin-1")


def live_cfg(m, tmp):
    """SYSTEM.CFG as the guest's A: holds it now (desksc.py's note)."""
    out = os.path.join(tmp, "flushed.img")
    m.pause()
    try:
        m.flush(0, out)
    finally:
        m.run()
    try:
        return bytes(os88fat.Fat12(out).read("SYSTEM.CFG"))
    except KeyError:
        return b""


def _u16le(b):
    return b[0] | (b[1] << 8)


def res_off():
    """di_res's offset in DESKITEM's image: the byte before its link
    record, whose first sixteen bytes are fixed (tests/deskitem/deskitem.asm).
    """
    bdir = os.path.join(ROOT, os.environ.get("OS88_BUILD", "build"))
    img = open(os.path.join(bdir, "deskitem.bin"), "rb").read()
    i = img.find(b"\x01\x05\x01\x00DESKITEM.O88")
    if i < 1:
        raise SystemExit("deskitem: no link record in deskitem.bin")
    return i - 1


def has_trailer(data):
    return len(data) >= 4 and data[-4:-2] == b"SC"


def shot(m, path):
    w, h, data = m.fbuf()
    os88marty.write_png_rgb(path, w, h, data)


def main():
    shots = None
    if "--shots" in sys.argv:
        shots = sys.argv[sys.argv.index("--shots") + 1]
        os.makedirs(shots, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="deskitem-")
    bdir = os.path.join(ROOT, os.environ.get("OS88_BUILD", "build"))
    sysimg = os.path.join(tmp, "os8088-360.img")
    appimg = os.path.join(tmp, "deskitem360.img")
    shutil.copy(os.path.join(bdir, "os8088-360.img"), sysimg)
    shutil.copy(os.path.join(bdir, "deskitem360.img"), appimg)

    with os88ui.boot(sysimg, apps=appimg, machine="os8088_xt_vga") as ui:
        m = ui.m

        def zbyte(z):
            return m.read(ui._S("desk_zslot") + z, 1)[0]

        def fresh():
            m.write(ui._S("toast_buf"), b"\0")

        print("A: File > Add")
        ui.raise_window(ui.path("B:/DESKITEM.O88"))
        fresh()
        ui.menu_pick("File", "Add")
        ui.wait_toast(says="Saved", limit=90)
        v = zbyte(DESK_SC0)
        check(v == DSL_PIN | 5, "the link is PLACED in cell 5, the one it "
              "asked for (%#x)" % v)
        seg = ui._word("sc_seg")
        row = bytes(m.read(seg << 4, SC_REC)) if seg else bytes(SC_REC)
        check(row[0] == 1 and row[2] == 1,
              "volume 1 and kind 1, a package (%d, %d)" % (row[0], row[2]))
        check(cstr(row[4:49]) == "DESKITEM.O88",
              "the path below the root (%r)" % cstr(row[4:49]))
        check(cstr(row[49:62]) == "DESK ITEM AB" and row[61] == 0,
              "the caption, TERMINATED by the kernel (%r)" % row[49:62])
        check(row[62:64] == b"\x01\x10",
              "the picture's header STAMPED by the kernel (%r)" % row[62:64])
        check(has_trailer(live_cfg(m, tmp)), "SYSTEM.CFG carries the trailer")
        if shots:
            ui.settle()
            shot(m, os.path.join(shots, "a_added.png"))

        print("B: double-click it")
        rows = ui._word("desk_rows")
        col, r = divmod(5, rows)
        x = ui._word("vid_desk_zx") - col * geom.DESK_PX + geom.DESK_CW // 2
        y = 32 + r * ui._word("desk_zstep") + ui._word("desk_zh1") // 2
        first = [w.i for w in ui.windows() if w.title == "DeskItem"]
        ui.mo.dblclick(x, y)
        ui._wait(lambda: len([w for w in ui.windows()
                              if w.title == "DeskItem"]) > len(first),
                 "a second DeskItem window", 60,
                 snapshot=lambda: ui.titles())
        check(True, "the link launched the package it points at")
        for w in ui.windows():          # the SECOND one goes: only the first
            if w.title == "DeskItem" and w.i not in first:  # holds the zone
                ui.close(w)

        print("C: File > Remove, the zone holding somebody else's link")
        w = [w for w in ui.windows() if w.i in first][0]
        wseg = _u16le(m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE
                             + geom.W_SEG, 2))
        res = (wseg << 4) + res_off()

        def path0():
            return m.read((ui._word("sc_seg") << 4) + 4, 1)[0]
        m.write((ui._word("sc_seg") << 4) + 4, b"X")    # XESKITEM.O88
        ui.raise_window(w)
        ui.menu_pick("File", "Remove")
        ui._wait(lambda: m.read(res, 1)[0] in b"rR", "Remove's answer", 90,
                 snapshot=lambda: chr(m.read(res, 1)[0]))
        check(m.read(res, 1)[0] == ord("r"),
              "Remove REFUSED a zone that no longer holds the link (%r)"
              % chr(m.read(res, 1)[0]))
        check(zbyte(DESK_SC0) == DSL_PIN | 5 and path0() == ord("X"),
              "...and the row in it is still there (%#x)" % zbyte(DESK_SC0))
        m.write((ui._word("sc_seg") << 4) + 4, b"D")

        print("D: File > Remove")
        ui.raise_window(w)
        fresh()
        ui.menu_pick("File", "Remove")
        ui.wait_toast(says="Saved", limit=90)
        check(zbyte(DESK_SC0) == 0xFF, "its cell is nothing (%#x)"
              % zbyte(DESK_SC0))
        check(ui._word("sc_seg") == 0, "the claim went with it")
        check(not has_trailer(live_cfg(m, tmp)),
              "SYSTEM.CFG has no trailer again")

    shutil.rmtree(tmp, ignore_errors=True)
    print("deskitem: %s" % ("PASS" if not fails else
                            "FAIL (%d): %s" % (len(fails), "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
