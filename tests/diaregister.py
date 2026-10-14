#!/usr/bin/env python3
"""SPEC.md 107.2, 107.5, 107.5.2: the Desk Calendar REGISTERS itself, once.

    make && python3 tests/diaregister.py

The SHIPPED 360KB system disk - no fixture: what is on it is what a user
has - copied to a scratch file, because the guest writes SYSTEM.CFG and
DESKCAL.DAT. One boot on a VGA XT (MartyPC), driven by name:

  A  nothing is remembered yet: the desktop comes up with no Desk Calendar
     and the disk has no SYSTEM.CFG trailer.
  B  A: > SYSTEM > DIA > DESKCAL.O88, double-clicked: the calendar opens and
     its first tick registers it - SYSTEM.CFG on the guest's A: holds ONE
     DIA row (+3 = 'D', KIND 1), whose path is SYSTEM\\DIA\\DESKCAL.O88 and
     whose caption is `Calendar`. OSAPI_CLK_GET's first caller is here too:
     the month on show and the selected day are today's. Untouched, it then
     WRITES NOTHING: seven guest seconds on, no save is owed and A: has no
     DESKCAL.DAT (SPEC.md 107.5.2).
  C  a second double-click is REFUSED by the package itself (dia_load's
     singleton, SPEC.md 107.5.2): `Desk Calendar is open`, ld_status
     LD_EABORT, one calendar window - and SYSTEM.CFG is byte-identical to
     B's, and nothing was written: the refusal comes before dia_load's
     first file call (the loader's read of the package is all it costs).
  D  its close box removes the row BY NAME: SYSTEM.CFG has no trailer, and
     the close saved DESKCAL.DAT - 2,456 bytes, a valid header holding the
     window's place and the empty v1 payload.
  E  a reset brings NO calendar back: the row went with the close.

**BREAK IT ON PURPOSE**: take dia_single's call out of dia_load and C fails
(a second window opens); take dia_register's call out of dia_beat and B
fails (no row); put dia_beat's "never saved: save it" branch back and B
fails (an unasked DESKCAL.DAT); make dia_close skip the remove and D and E
fail.
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
DIA_ST = os88dia.dia_st_off(os88dia.DESKCAL_DIA_BSS, os88dia.DESKCAL_PAYLEN)
DIA_S_ROW, DIA_S_DIRTY = 2, 16
LD_EABORT = 4

fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def volume(m, tmp, drive=0):
    out = os.path.join(tmp, "flushed%d.img" % drive)
    m.pause()
    try:
        m.flush(drive, out)
    finally:
        m.run()
    return os88flush.Volume(open(out, "rb").read())


def image_size():
    b = open(os.path.join(BDIR, "deskcal.bin"), "rb").read()
    return struct.unpack_from("<H", b, 8)[0]


def cfg_of(v):
    try:
        return v.read("SYSTEM.CFG")
    except Exception:
        return None


def calendars(ui):
    return [w for w in ui.windows() if w.title == TITLE]


def main():
    tmp = tempfile.mkdtemp(prefix="diaregister-")
    sysimg = os.path.join(tmp, "os8088-360.img")
    shutil.copy(os.path.join(BDIR, "os8088-360.img"), sysimg)
    image = image_size()

    with os88ui.boot(sysimg, machine="os8088_xt_vga") as ui:
        m = ui.m
        print("A: nothing remembered yet")
        check(not calendars(ui), "no Desk Calendar at boot")
        cfg = cfg_of(volume(m, tmp))
        check(cfg is None or not os88dia.parse_cfg(cfg)["trailer"],
              "no desktop trailer on the shipped disk")

        print("B: double-click SYSTEM/DIA/DESKCAL.O88 - it registers")
        w = ui.path("A:/SYSTEM/DIA/DESKCAL.O88")
        check(w is not None and w.title == TITLE, "the calendar opened")
        wseg = struct.unpack_from("<H", m.read(
            ui._S("wm_wins") + w.i * geom.WIN_SIZE + geom.W_SEG, 2))[0]

        def st():
            return m.readseg(wseg, image + DIA_ST, 1)[0]
        ui._wait(lambda: st() & DIA_S_ROW, "the calendar's registration", 120,
                 snapshot=lambda: "dia_st = %#x" % st())
        cfg_b = cfg_of(volume(m, tmp))
        rows = os88dia.dia_rows(cfg_b) if cfg_b else []
        check(len(rows) == 1, "SYSTEM.CFG holds one DIA row (%d)" % len(rows))
        if rows:
            r = rows[0]
            check(r["kind"] == 1 and r["path"] == "SYSTEM\\DIA\\DESKCAL.O88"
                  and r["cap"] == "Calendar" and r["vol"] == 0,
                  "...KIND 1, A:, SYSTEM\\DIA\\DESKCAL.O88, `Calendar` (%r)"
                  % (r,))
        bss = m.readseg(wseg, image, 50)
        ty = struct.unpack_from("<H", bss, os88dia.DESKCAL_BSS_TY)[0]
        tm, td = bss[os88dia.DESKCAL_BSS_TM], bss[os88dia.DESKCAL_BSS_TD]
        y = struct.unpack_from("<H", bss, os88dia.DESKCAL_BSS_Y)[0]
        mo, sel = bss[os88dia.DESKCAL_BSS_M], bss[os88dia.DESKCAL_BSS_SEL]
        h = bytes(m.read(ui._S("clk_day"), 2))
        cy = struct.unpack("<H", bytes(m.read(ui._S("clk_year"), 2)))[0]
        check((ty, tm, td) == (cy, h[1], h[0]),
              "today is OSAPI_CLK_GET's date (%d-%d-%d, clock %d-%d-%d)"
              % (ty, tm, td, cy, h[1], h[0]))
        check((y, mo, sel) == (ty, tm, td),
              "it opened on today's month with today selected (%d-%d %d)"
              % (y, mo, sel))

        # A LAUNCH ALONE WRITES NOTHING (SPEC.md 107.5.2): with no state
        # file the place on record is where the window opened, so three
        # polls later no save is owed and A: has no DESKCAL.DAT - a wave-2
        # build wrote one ~2 s after every first launch. The file comes with
        # the close (D) or a drag (diamove)
        def hdr():
            return m.readseg(wseg, image + os88dia.DESKCAL_DIA_BSS, 4)
        os88marty.guest_sleep(m, 7.0)
        check(not hdr()[3] & os88dia.DIA_HF_POS and not st() & DIA_S_DIRTY,
              "untouched, nothing is owed (dia_st = %#x, header flags %#x)"
              % (st(), hdr()[3]))
        try:
            volume(m, tmp).read("SYSTEM/APPDATA/DESKCAL.DAT")
            had = True
        except Exception:
            had = False
        check(not had, "...and A: has no DESKCAL.DAT yet")

        print("C: a second launch is refused, and writes nothing")
        m.disk(reset=True)
        got = ui.open("DESKCAL.O88", expect="refusal")
        check(got is None, "no second window opened")
        check(ui._byte("ld_status") == LD_EABORT,
              "the package refused itself (ld_status %d)"
              % ui._byte("ld_status"))
        txt = ui.wait_toast(says="is open")
        check(txt == "Desk Calendar is open", "the toast says %r" % txt)
        check(len(calendars(ui)) == 1, "one calendar window")
        d = m.disk()
        check(d.get("writes", 0) == 0, "no disk write (%r)" % (d,))
        check(cfg_of(volume(m, tmp)) == cfg_b,
              "SYSTEM.CFG is B's to the byte")

        print("D: the close box takes the row away and saves")
        w = ui.window(TITLE)
        ui.close(w)
        ui.wait_toast(says="Saved", limit=90)
        ui._wait(lambda: not calendars(ui), "the calendar to close", 30)
        v = volume(m, tmp)
        cfg_d = cfg_of(v)
        check(cfg_d is not None and not os88dia.parse_cfg(cfg_d)["trailer"],
              "SYSTEM.CFG has no trailer (%r bytes)"
              % (len(cfg_d) if cfg_d else None))
        try:
            dat = v.read("SYSTEM/APPDATA/DESKCAL.DAT")
            pd = os88dia.parse_dat(dat, os88dia.DESKCAL_PAYLEN,
                                   os88dia.DESKCAL_PAYVER)
            cal = os88dia.parse_deskcal(pd["payload"])
        except Exception as e:
            dat, pd, cal = b"", {"pos": False}, {"err": str(e)}
        check(len(dat) == os88dia.DIA_HDR_SIZE + os88dia.DESKCAL_PAYLEN,
              "DESKCAL.DAT is %d bytes (%d)"
              % (os88dia.DIA_HDR_SIZE + os88dia.DESKCAL_PAYLEN, len(dat)))
        check(pd.get("pos") and (pd["x"], pd["y"]) == (w.x, w.y)
              and tuple(pd.get("screen") or ()) == (640, 480),
              "...holding the window's place (%r at %r)"
              % ({k: v for k, v in pd.items() if k != "payload"},
                 (w.x, w.y)))
        check(cal.get("appts") == [] and cal.get("tasks") == [],
              "...and the empty v1 payload (%r)" % (cal,))

        print("E: a reset brings no calendar back")
        t0 = struct.unpack("<H", bytes(m.read(0x46C, 2)))[0]
        m.reset()
        m.run()
        ui._wait(lambda: struct.unpack("<H", bytes(m.read(0x46C, 2)))[0]
                 < t0, "the BIOS tick to restart", 60)
        ui.ready()
        os88marty_pace(m, 10)
        check(not calendars(ui), "no Desk Calendar after the reset")

    shutil.rmtree(tmp, ignore_errors=True)
    print("diaregister: %s" % ("PASS" if not fails else
                               "FAIL (%d): %s" % (len(fails),
                                                  "; ".join(fails))))
    return 1 if fails else 0


def os88marty_pace(m, s):
    import os88marty
    os88marty.pace(m, s)


if __name__ == "__main__":
    sys.exit(main())
