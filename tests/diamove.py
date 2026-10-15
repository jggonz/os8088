#!/usr/bin/env python3
"""SPEC.md 110.4, 110.5, 110.5.2: the Desk Calendar stays WHERE IT WAS PUT,
across a reset - on every adapter, and on a screen of another size.

    make && python3 tests/diamove.py [--leg vga|cga|anchor]

The shipped 360KB system disk, copied to a scratch file. `--leg vga` (the
default, row `diamove`) and `--leg cga` (row `diamovecga`) are one shape on
MartyPC's VGA XT and its 5150 CGA:

  A  A: > SYSTEM > DIA > DESKCAL.O88 opens it, and its first tick registers
     its row in SYSTEM.CFG.
  B  the WIDGET is DRAGGED by its tag row (ui.widget_drag - v2 has no title
     bar); within two polls (~4 s) the new place is saved - DESKCAL.DAT on
     the guest's A: holds the FACE's origin, flagged DIA_HF_CONT, and the
     screen it was saved on, and nothing is owed any more.
  C  RESET. The boot's dia_boot relaunches it from the row, dia_load reads
     the state file in the entry proc, and the widget comes back AT THE
     DRAGGED PLACE - not at the default (screen width - 264) - holding no
     desktop cell, with nobody touching the mouse; and it is a widget again:
     wm_zord[0], the TOP instance slot (INST_MAX - 1), no dock tile.

The reset is waited out on the BIOS tick restarting rather than on settle(),
which returns during the BIOS disk probe after a warm boot on a loaded host.

THE CGA LEG is the one wave 2 shipped broken (the template was TALL's frame
and every restored y came back as 20): v2's COMPACT face is 146 rows, sized
before the create, and its drag lands at y = 28.

`--leg anchor` (row `diaanchor`) writes a V1 DESKCAL.DAT with tools/os88dia.py
before the boot - a TITLED frame's place (560,100) saved on an 800x600
screen, no DIA_HF_CONT - and opens the calendar on the VGA XT's 640x480:

  D  the v1 place is CONVERTED to a face, (561,118) - a border in and a
     title bar down (SPEC.md 110.11) - and each axis ANCHORED with the
     face's size (SPEC.md 110.5.2): x from the far edge, 561 -> 401, the
     frame at 399 under the snap; y from the near one, 118 - a face hanging
     over the dock (118 + 355 > 456), which wm_create's fit pulls up above
     it: dia_made must give it back.
  E  nobody touched it, so NOTHING IS WRITTEN: three polls later the state
     file on A: is byte for byte the fixture, and no save is owed. A wave-2
     build saved the anchored place ~2 s after the launch.

**BREAK IT ON PURPOSE**: make dia_tick skip dia_pos and B fails (no save);
take dia_load's placement out and C fails (the default place); take
`call dia_boot` out of drv_notice_x and C fails (no window); take the
template's sizing out of dc_entry AND dia_made's y out and the CGA leg's C
fails at y = 20; take dia_made's y out alone and D fails (y clamped above
the dock); put back the "never saved: save it" branch in dia_beat and E fails.
"""
import argparse
import os
import shutil
import struct
import subprocess
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
TEMPLATE = (375, 42)            # the default place: face (640 - 264, 42)
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


def move_leg(tmp, machine, template, drag, screen):
    sysimg = os.path.join(tmp, "os8088-360.img")
    shutil.copy(os.path.join(BDIR, "os8088-360.img"), sysimg)
    image = image_size()

    with os88ui.boot(sysimg, machine=machine) as ui:
        m = ui.m
        print("A: open it, and it registers")
        w = ui.path("A:/SYSTEM/DIA/DESKCAL.O88")
        check(w.title == TITLE and (w.x, w.y) == template,
              "the calendar opened at its default place (%d,%d)"
              % (w.x, w.y))
        check(w.flags & geom.WF_DESK, "...a DESK window (W_FLAGS %#06x)"
              % w.flags)
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
        ui.drag_window(w, *drag)
        w = ui.window(TITLE)
        moved = (w.x, w.y)
        check(moved != template, "the frame moved (%d,%d)" % moved)
        face = (moved[0] + 1, moved[1])
        ui._wait(lambda: hdr() == (os88dia.DIA_HF_POS | os88dia.DIA_HF_CONT,)
                 + face and not st() & DIA_S_DIRTY, "the drag's save", 120,
                 snapshot=lambda: "dia_st = %#x, header %r" % (st(), hdr()))
        try:
            d = os88dia.parse_dat(volume(m, tmp).read(
                "SYSTEM/APPDATA/DESKCAL.DAT"), os88dia.DESKCAL_PAYLEN,
                os88dia.DESKCAL_PAYVER)
        except Exception as e:
            d = {"pos": False, "x": None, "y": None, "screen": None,
                 "err": str(e)}
        check(d["pos"] and d.get("cont") and (d["x"], d["y"]) == face
              and tuple(d["screen"] or ()) == screen,
              "DESKCAL.DAT on A: holds the FACE %r (DIA_HF_CONT) on %dx%d "
              "(%r)" % ((face,) + screen
                 + ({k: v for k, v in d.items() if k != "payload"},)))

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
        zo = list(m.read(ui._S("wm_zord"), m.read(ui._S("wm_zn"), 1)[0]))
        inst = geom.instances(m)
        mine = [k for k, rr in inst.items()
                if (rr["win"] + geom.KERNEL_SEG * 16 - ui._S("wm_wins"))
                // geom.WIN_SIZE == w2.i]
        check(zo[:1] == [w2.i] and w2.flags & geom.WF_DESK,
              "...a widget again: wm_zord[0] (%r)" % (zo,))
        check(mine == [geom.INST_MAX - 1],
              "...in the TOP instance slot, INST_MAX - 1 (%r)" % (mine,))


# the anchor leg's fixture: a place saved on a Hercules-sized screen
SAVED, SAVED_SCREEN = (560, 100), (800, 600)   # a v1 TITLED frame's place
ANCHORED = (399, 118)           # face (561, 118): x 561 - 800 + 640 = 401,
                                # the frame 400 snapped to 399; y near: 118


def anchor_leg(tmp):
    sysimg = os.path.join(tmp, "os8088-360.img")
    shutil.copy(os.path.join(BDIR, "os8088-360.img"), sysimg)
    image = image_size()
    fixture = os88dia.dat(os88dia.deskcal(), x=SAVED[0], y=SAVED[1],
                          screen=SAVED_SCREEN)
    src = os.path.join(tmp, "DESKCAL.DAT")
    open(src, "wb").write(fixture)
    subprocess.run(["mcopy", "-i", sysimg, src, "::/SYSTEM/APPDATA/"],
                   check=True, stdout=subprocess.DEVNULL,
                   env=dict(os.environ, MTOOLS_SKIP_CHECK="1"))

    with os88ui.boot(sysimg, machine="os8088_xt_vga") as ui:
        m = ui.m
        print("D: a place saved on 720x348, opened on 640x480")
        w = ui.path("A:/SYSTEM/DIA/DESKCAL.O88")
        dock = ui._word("vid_dock_y0")
        check(ANCHORED[1] + w.h > dock,
              "the anchored face hangs over the dock (y %d + h %d > %d) - "
              "the case wm_create's fit clamps" % (ANCHORED[1], w.h, dock))
        check(w.flags & geom.WF_DESK and w.h == 355,
              "a TALL desk widget, 355 rows (%d)" % w.h)
        check((w.x, w.y) == ANCHORED,
              "anchored to the far edges: (%d,%d), want %r"
              % (w.x, w.y, ANCHORED))
        wseg = wseg_of(ui, w)

        def st():
            return m.readseg(wseg, image + DIA_ST, 1)[0]
        ui._wait(lambda: st() & DIA_S_ROW, "the registration", 120,
                 snapshot=lambda: "dia_st = %#x" % st())

        print("E: untouched, it writes nothing")
        os88marty.guest_sleep(m, 7.0)       # three polls and more
        check(not st() & DIA_S_DIRTY, "no save owed (dia_st = %#x)" % st())
        got = volume(m, tmp).read("SYSTEM/APPDATA/DESKCAL.DAT")
        check(bytes(got) == fixture,
              "DESKCAL.DAT on A: is still the fixture, (%d,%d) on %dx%d"
              % (SAVED + SAVED_SCREEN))
        w = ui.window(TITLE)
        check((w.x, w.y) == ANCHORED, "...and the window has not moved")


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--leg", choices=("vga", "cga", "anchor"), default="vga")
    a = ap.parse_args(argv)
    tmp = tempfile.mkdtemp(prefix="diamove-")
    try:
        if a.leg == "vga":
            move_leg(tmp, "os8088_xt_vga", TEMPLATE, (-216, 40), (640, 480))
        elif a.leg == "cga":
            # COMPACT's default face is (376, 22); a drag 6 down lands at
            # y = 28, its bottom 174 above the 176-row dock (a wave-2 build
            # brought every CGA place back as 20)
            move_leg(tmp, os88marty.machine("os8088_5150_cga"), (375, 22),
                     (-216, 6),
                     (640, 200))
        else:
            anchor_leg(tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    name = "diamove" + ("" if a.leg == "vga" else a.leg)
    print("%s: %s" % (name, "PASS" if not fails else
                      "FAIL (%d): %s" % (len(fails), "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
