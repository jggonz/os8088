#!/usr/bin/env python3
"""SPEC.md 110.4: a DESKTOP INTEGRATED APPLICATION comes back at boot.

    make && make diatest && python3 tests/diaboot.py

`make diatest`'s diarestore360.img is the shipped 360KB system disk plus
DIAPROBE.O88 in SYSTEM\\DIA\\, a SYSTEM.CFG whose desktop trailer holds ONE
DIA row to it (written by tools/os88dia.py, the host's own second reader),
and a DIAPROBE.DAT that puts the probe's frame at (303,184) on a 640x480
screen. One boot on a VGA XT, and every claim is asked of the GUEST'S state:

  A  the probe is UP when the desktop is - dia_boot launched it, from kmain's
     tail, with nothing but SYSTEM.CFG asking - and its frame is at
     (303,184), where its own state file put it, not at its template's
     (40,60).
  B  its row holds NO desktop cell: [desk_zslot] of the zone is 0xFF, the
     row still carries the mark, and no `Shortcut not found` was said.
  C  its first tick REGISTERED it - the settings core loaded and answered
     the zone - and because the row was byte-identical it WROTE NOTHING:
     SYSTEM.CFG on the guest's A: is the fixture to the byte.
     And DIAPROBE.DAT is the fixture still: it opened where it was saved,
     so nothing was owed.
  D  a DRAG is saved: two polls agree, and DIAPROBE.DAT on the guest's A:
     holds the new place (SPEC.md 110.5.1's walk, on the disk it came from).
  E  ...and AFTER that save its close box still removes the row BY NAME
     (OSAPI_DESK_ITEM AH = 0xFF): the table is empty and SYSTEM.CFG has no
     trailer. A W1 build failed exactly here - the save's walk used the row
     record as its find buffer, the close removed by `APPDATA`'s directory
     entry, matched nothing, and the closed widget came back at every boot.

**BREAK IT ON PURPOSE** (docs/WRITING-TESTS.md 1): take `call dia_boot` out
of driver.inc's drv_notice_x and A fails (no probe); take K3's mark test out
of desk_live and B fails (the row takes a cell); make sc_m_add's DIA arm skip
sc_m_same and C fails (SYSTEM.CFG rewritten); make sc_m_pdel ignore AL = 0xFF
and E fails (the row stays); point os88dia.inc's dia_seek back at dia_rec and
E fails the same way.

The floppy is COPIED to a scratch directory: the guest writes SYSTEM.CFG.
"""
import os
import shutil
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import os88ui      # noqa: E402
import os88geom as geom   # noqa: E402
import os88flush   # noqa: E402
import os88dia     # noqa: E402

ROOT = os.path.join(HERE, "..")
BDIR = os.path.join(ROOT, os.environ.get("OS88_BUILD", "build"))
DESK_SC0 = os88dia.DESK_SC0
# os88dia.inc's bss, after the image: header + payload (0 for the probe), the
# 128-byte row, the 13-byte name, the banked folder word and drive byte, then
# dia_st (SPEC.md 110.5)
DIA_ST = 16 + 0 + 128 + 13 + 2 + 1
DIA_S_TRIED, DIA_S_ROW, DIA_S_DIRTY = 1, 2, 16

fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def volume(m, tmp, drive=0):
    """The guest's floppy as it stands NOW (os88flush.Volume)."""
    out = os.path.join(tmp, "flushed%d.img" % drive)
    m.pause()
    try:
        m.flush(drive, out)
    finally:
        m.run()
    return os88flush.Volume(open(out, "rb").read())


def probe_image():
    b = open(os.path.join(BDIR, "diaprobe.bin"), "rb").read()
    return struct.unpack_from("<H", b, 8)[0]     # +8 the image's size


def main():
    tmp = tempfile.mkdtemp(prefix="diaboot-")
    sysimg = os.path.join(tmp, "diarestore360.img")
    shutil.copy(os.path.join(BDIR, "diarestore360.img"), sysimg)
    fixture = open(os.path.join(BDIR, "diafix", "restore", "system.cfg"),
                   "rb").read()
    fixdat = open(os.path.join(BDIR, "diafix", "DIAPROBE.DAT"), "rb").read()
    st_off = probe_image() + DIA_ST
    hdr_off = probe_image()             # DIA_BSS 0: the header comes first

    with os88ui.boot(sysimg, machine="os8088_xt_vga") as ui:
        m = ui.m

        print("A: the probe came back with the desktop")
        w = ui.wait_window("DiaProbe", limit=60)
        check((w.x, w.y) == (303, 184),
              "its frame is where DIAPROBE.DAT put it (%d,%d)" % (w.x, w.y))
        wseg = struct.unpack_from("<H", m.read(
            ui._S("wm_wins") + w.i * geom.WIN_SIZE + geom.W_SEG, 2))[0]

        print("B: its row holds no cell")
        zb = m.read(ui._S("desk_zslot") + DESK_SC0, 1)[0]
        check(zb == 0xFF, "[desk_zslot] of its zone is 0xFF (%#x)" % zb)
        seg = ui._word("sc_seg")
        row = bytes(m.read(seg << 4, os88dia.SC_REC)) if seg else bytes(128)
        r = os88dia.parse_row(row)
        check(r["dia"] and r["kind"] == 1 and r["path"]
              == "SYSTEM\\DIA\\DIAPROBE.O88",
              "the row is the DIA row (%r)" % (r,))
        txt, _ = ui.toast()
        check("not found" not in txt.lower(), "nothing was not found (%r)"
              % txt)

        print("C: the first tick registered it, and wrote nothing")
        def st():
            return m.readseg(wseg, st_off, 1)[0]
        ui._wait(lambda: st() & DIA_S_ROW, "the probe's registration", 90,
                 snapshot=lambda: "dia_st = %#x" % st())
        check(st() & DIA_S_ROW, "the settings core answered its zone "
              "(dia_st %#x)" % st())
        v = volume(m, tmp)
        check(v.read("SYSTEM.CFG") == fixture,
              "SYSTEM.CFG is the fixture to the byte: an identical row "
              "writes nothing")
        check(v.read("SYSTEM/APPDATA/DIAPROBE.DAT") == fixdat,
              "DIAPROBE.DAT unchanged: it opened where it was saved")

        print("D: a drag is saved where the probe came from")
        ui.drag_window(w, 64, 40)
        w = ui.window("DiaProbe")
        moved = (w.x, w.y)
        check(moved != (303, 184), "the frame moved (%d,%d)" % moved)

        def hdr():                  # the header in the probe's own bss
            return struct.unpack("<HH", m.readseg(wseg, hdr_off + 4, 4))

        def owed():
            return hdr() != moved or st() & DIA_S_DIRTY
        ui._wait(lambda: not owed(), "the drag's save", 120,
                 snapshot=lambda: "dia_st = %#x, header at %r" % (st(),
                                                                  hdr()))
        try:
            d = os88dia.parse_dat(volume(m, tmp).read(
                "SYSTEM/APPDATA/DIAPROBE.DAT"), 0, 1)
        except Exception as e:
            d = {"pos": False, "x": None, "y": None, "err": str(e)}
        check(d["pos"] and (d["x"], d["y"]) == moved,
              "DIAPROBE.DAT on A: holds the new place %r (%r)" % (moved, d))

        print("E: after that save, the close box removes the row by name")
        ui.close(w)
        ui.wait_toast(says="Saved", limit=90)
        check(ui._word("sc_seg") == 0, "the table's claim went with it")
        v = volume(m, tmp)
        cfg = v.read("SYSTEM.CFG")
        check(not os88dia.parse_cfg(cfg)["trailer"],
              "SYSTEM.CFG has no trailer (%d bytes)" % len(cfg))

    shutil.rmtree(tmp, ignore_errors=True)
    print("diaboot: %s" % ("PASS" if not fails else
                           "FAIL (%d): %s" % (len(fails), "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
