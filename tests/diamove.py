#!/usr/bin/env python3
"""SPEC.md 107.4, 107.5, 107.5.2: the Desk Calendar stays WHERE IT WAS PUT,
across a reset - on every adapter, and on a screen of another size.

    make && python3 tests/diamove.py [--leg vga|cga|anchor]

The shipped 360KB system disk, copied to a scratch file. `--leg vga` (the
default, row `diamove`) and `--leg cga` (row `diamovecga`) are one shape on
MartyPC's VGA XT and its 5150 CGA:

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

THE CGA LEG is the one wave 2 shipped broken: the template was the 255-row
TALL frame, so wm_create's fit cut it to the 155-row band and clamped every
restored y to 20 before PREFER made it 139 - the place came back at y = 20
and the next poll saved 20 over the user's. Its drag lands at y = 24.

`--leg anchor` (row `diaanchor`) writes DESKCAL.DAT with tools/os88dia.py
before the boot - a place (503,88) saved on a 720x348 screen - and opens the
calendar on the VGA XT's 640x480:

  D  each axis is ANCHORED to the old screen's nearer edge (SPEC.md
     107.5.2): x 503 -> 423 and y 88 -> 220, both from the far edge. y 220 is
     a frame hanging over the dock, which wm_create's fit pulls up above it
     (to 200): dia_made must give it back.
  E  nobody touched it, so NOTHING IS WRITTEN: three polls later the state
     file on A: is byte for byte the fixture, and no save is owed. A wave-2
     build saved the anchored place ~2 s after the launch.

**BREAK IT ON PURPOSE**: make dia_tick skip dia_pos and B fails (no save);
take dia_load's placement out and C fails (the template's place); take
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


def move_leg(tmp, machine, template, drag, screen):
    sysimg = os.path.join(tmp, "os8088-360.img")
    shutil.copy(os.path.join(BDIR, "os8088-360.img"), sysimg)
    image = image_size()

    with os88ui.boot(sysimg, machine=machine) as ui:
        m = ui.m
        print("A: open it, and it registers")
        w = ui.path("A:/SYSTEM/DIA/DESKCAL.O88")
        check(w.title == TITLE and (w.x, w.y) == template,
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
        ui.drag_window(w, *drag)
        w = ui.window(TITLE)
        moved = (w.x, w.y)
        check(moved != template, "the frame moved (%d,%d)" % moved)
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
              and tuple(d["screen"] or ()) == screen,
              "DESKCAL.DAT on A: holds %r on %dx%d (%r)"
              % ((moved,) + screen
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


# the anchor leg's fixture: a place saved on a Hercules-sized screen
SAVED, SAVED_SCREEN = (503, 88), (720, 348)
ANCHORED = (423, 220)           # 640 - (720 - 503), 480 - (348 - 88)


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
              "the anchored frame hangs over the dock (y %d + h %d > %d) - "
              "the case wm_create's fit clamps" % (ANCHORED[1], w.h, dock))
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
            move_leg(tmp, "os8088_xt_vga", TEMPLATE, (-216, 88), (640, 480))
        elif a.leg == "cga":
            # the template's (360,40) is 36 under the snap and the 176-row
            # dock (40 + 139 > 176); a drag 12 up lands at y = 24, which a
            # wave-2 build brought back as 20
            move_leg(tmp, os88marty.machine("os8088_5150_cga"), (359, 36),
                     (-216, -12),
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
