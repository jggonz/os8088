#!/usr/bin/env python3
"""SPEC.md 107.4, 107.5: the Desk Calendar stays WHERE IT WAS PUT, across a
reset.

    make && python3 tests/diamove.py

The shipped 360KB system disk, copied to a scratch file. One VGA XT
(MartyPC):

  A  A: > SYSTEM > DIA > DESKCAL.O88 opens it, and its first tick registers
     its row in SYSTEM.CFG.
  B  its title bar is DRAGGED; within two polls (~4 s) the new place is
     saved - DESKCAL.DAT on the guest's A: holds it and the 640x480 screen
     it was saved on, and nothing is owed any more.
  C  RESET. The boot's dia_boot relaunches it from the row, dia_load reads
     the state file in the entry proc, and the window comes back AT THE
     DRAGGED PLACE - not at the template's (359,40) - holding no desktop
     cell, with nobody touching the mouse.

The reset is waited out on the BIOS tick restarting rather than on settle(),
which returns during the BIOS disk probe after a warm boot on a loaded host.

**BREAK IT ON PURPOSE**: make dia_tick skip dia_pos and B fails (no save);
take dia_load's placement out and C fails (the template's place); take
`call dia_boot` out of drv_notice_x and C fails (no window).
"""
import os
import shutil
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import os88ui      # noqa: E402
import os88marty   # noqa: E402
import os88geom as geom   # noqa: E402
import os88flush   # noqa: E402
import os88dia     # noqa: E402

ROOT = os.path.join(HERE, "..")
BDIR = os.path.join(ROOT, os.environ.get("OS88_BUILD", "build"))
TITLE = "Desk Calendar"
TEMPLATE = (359, 40)            # dc_tpl's (360,40) under the snap
DIA_ST = os88dia.dia_st_off(os88dia.DESKCAL_DIA_BSS, os88dia.DESKCAL_PAYLEN)
HDR = os88dia.dia_hdr_off(os88dia.DESKCAL_DIA_BSS)
DIA_S_ROW, DIA_S_DIRTY = 2, 16

fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def volume(m, tmp):
    out = os.path.join(tmp, "flushed.img")
    m.pause()
    try:
        m.flush(0, out)
    finally:
        m.run()
    return os88flush.Volume(open(out, "rb").read())


def image_size():
    b = open(os.path.join(BDIR, "deskcal.bin"), "rb").read()
    return struct.unpack_from("<H", b, 8)[0]


def wseg_of(ui, w):
    return struct.unpack_from("<H", ui.m.read(
        ui._S("wm_wins") + w.i * geom.WIN_SIZE + geom.W_SEG, 2))[0]


def main():
    tmp = tempfile.mkdtemp(prefix="diamove-")
    sysimg = os.path.join(tmp, "os8088-360.img")
    shutil.copy(os.path.join(BDIR, "os8088-360.img"), sysimg)
    image = image_size()

    with os88ui.boot(sysimg, machine="os8088_xt_vga") as ui:
        m = ui.m
        print("A: open it, and it registers")
        w = ui.path("A:/SYSTEM/DIA/DESKCAL.O88")
        check(w.title == TITLE and (w.x, w.y) == TEMPLATE,
              "the calendar opened at its template's place (%d,%d)"
              % (w.x, w.y))
        wseg = wseg_of(ui, w)

        def st():
            return m.readseg(wseg, image + DIA_ST, 1)[0]

        def hdr():
            b = m.readseg(wseg, image + HDR, 8)
            return b[3], struct.unpack_from("<H", b, 4)[0], \
                struct.unpack_from("<H", b, 6)[0]
        ui._wait(lambda: st() & DIA_S_ROW, "the registration", 120,
                 snapshot=lambda: "dia_st = %#x" % st())
        ui.raise_window(w)

        print("B: drag it; the place is saved once it settles")
        ui.drag_window(w, -216, 88)
        w = ui.window(TITLE)
        moved = (w.x, w.y)
        check(moved != TEMPLATE, "the frame moved (%d,%d)" % moved)
        ui._wait(lambda: hdr() == (os88dia.DIA_HF_POS,) + moved
                 and not st() & DIA_S_DIRTY, "the drag's save", 120,
                 snapshot=lambda: "dia_st = %#x, header %r" % (st(), hdr()))
        try:
            d = os88dia.parse_dat(volume(m, tmp).read(
                "SYSTEM/APPDATA/DESKCAL.DAT"), os88dia.DESKCAL_PAYLEN,
                os88dia.DESKCAL_PAYVER)
        except Exception as e:
            d = {"pos": False, "x": None, "y": None, "screen": None,
                 "err": str(e)}
        check(d["pos"] and (d["x"], d["y"]) == moved
              and tuple(d["screen"] or ()) == (640, 480),
              "DESKCAL.DAT on A: holds %r on 640x480 (%r)"
              % (moved, {k: v for k, v in d.items() if k != "payload"}))

        print("C: reset - it comes back where it was put")
        t0 = struct.unpack("<H", bytes(m.read(0x46C, 2)))[0]
        m.reset()
        m.run()
        ui._wait(lambda: struct.unpack("<H", bytes(m.read(0x46C, 2)))[0]
                 < t0, "the BIOS tick to restart", 60)
        ui.ready()
        w2 = ui.wait_window(TITLE, limit=90)
        check((w2.x, w2.y) == moved,
              "the calendar is back at (%d,%d), not at the template's"
              % (w2.x, w2.y))
        seg = ui._word("sc_seg")
        n = ui._byte("sc_nrows")
        rows = [os88dia.parse_row(m.read((seg << 4) + i * os88dia.SC_REC,
                                         os88dia.SC_REC))
                for i in range(n)] if seg else []
        dia = [i for i, r in enumerate(rows) if r["dia"]]
        check(len(dia) == 1, "one DIA row in the table (%d)" % len(dia))
        if dia:
            z = m.read(ui._S("desk_zslot") + os88dia.DESK_SC0 + dia[0], 1)[0]
            check(z == 0xFF, "...holding no desktop cell (%#x)" % z)

    shutil.rmtree(tmp, ignore_errors=True)
    print("diamove: %s" % ("PASS" if not fails else
                           "FAIL (%d): %s" % (len(fails), "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
