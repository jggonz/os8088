#!/usr/bin/env python3
"""SPEC.md 107.5.1: a DIA's save asks whether the disk is still HOME.

    make && make diatest && python3 tests/diaswap.py

A widget autostarted from the system disk outlives the disk: at 360KB the
user swaps A: to reach anything else, and every shipped disk carries
`SYSTEM\\APPDATA`. One boot of `diarestore360.img` on a VGA XT, then:

  A  the probe up and registered; then a COPY OF THE APPS DISK goes in A:
     and the probe is dragged.
  B  the save is REFUSED as not home: the apps disk gains no DIAPROBE.DAT,
     the state stays owed, and it is not retried every poll - a W1 build
     re-walked the disk every 2 s for as long as it was out (61 reads in
     68 guest s); the backoff waits 1, 2, 4, 8, 16 polls, so 40 guest s of
     it is a handful of tries.
  C  the system disk goes back and the owed save lands THERE: its
     DIAPROBE.DAT holds the dragged place and the state is clean, and both
     floppies still pass os88disk.py --verify.
  D  the apps disk goes in again and the probe's close box is clicked: the
     remove cannot read CTRL.DRV, whose toast asks for the system disk, and
     the close is DECLINED once (SPEC.md 107.5) - the probe stays up rather
     than closing with its row stranded in SYSTEM.CFG. The system disk back,
     the next close goes ahead and the row is gone.

**BREAK IT ON PURPOSE**: take dia_enter's own-name FIND out of os88dia.inc
and B fails (the apps disk gains SYSTEM/APPDATA/DIAPROBE.DAT); take the
backoff out of dia_tick and B's read count fails; make dia_close ignore
the remove's CF and D fails (the probe closes, its row left behind).
"""
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
DIA_ST = 16 + 0 + 128 + 13 + 2 + 1      # tests/diaboot.py's layout
DIA_S_ROW, DIA_S_DIRTY, DIA_S_RDONLY = 2, 16, 32
HZ = 4772727.0
MAXREADS = 24       # 40 guest s: <= 6 tries at the backoff, ~2-3 reads each;
                    # a try every poll would be ~20 tries and 40+ reads

fails = []


def check(cond, what):
    print("   %s  %s" % ("ok " if cond else "FAIL", what))
    if not cond:
        fails.append(what)
    return cond


def snap(m, tmp, name):
    out = os.path.join(tmp, name)
    m.pause()
    try:
        m.flush(0, out)
    finally:
        m.run()
    return out


def main():
    tmp = tempfile.mkdtemp(prefix="diaswap-")
    sysimg = os.path.join(tmp, "diarestore360.img")
    shutil.copy(os.path.join(BDIR, "diarestore360.img"), sysimg)
    apps = os.path.join(tmp, "apps360.img")
    shutil.copy(os.path.join(BDIR, "apps360.img"), apps)
    b = open(os.path.join(BDIR, "diaprobe.bin"), "rb").read()
    image = struct.unpack_from("<H", b, 8)[0]

    with os88ui.boot(sysimg, machine="os8088_xt_vga") as ui:
        m = ui.m
        print("A: the probe is up and registered; the apps disk goes in A:")
        w = ui.wait_window("DiaProbe", limit=60)
        wseg = struct.unpack_from("<H", m.read(
            ui._S("wm_wins") + w.i * geom.WIN_SIZE + geom.W_SEG, 2))[0]

        def st():
            return m.readseg(wseg, image + DIA_ST, 1)[0]

        def hdr():
            return struct.unpack("<HH", m.readseg(wseg, image + 4, 4))
        ui._wait(lambda: st() & DIA_S_ROW, "the probe's registration", 90,
                 snapshot=lambda: "dia_st = %#x" % st())
        home = snap(m, tmp, "home.img")       # the system disk as it stands
        m.mount(0, apps)
        os88marty.pace(m, 3)
        ui.drag_window(w, 64, 40)
        w = ui.window("DiaProbe")
        moved = (w.x, w.y)
        check(moved != (303, 184), "the frame moved (%d,%d)" % moved)

        print("B: not home - nothing written, owed, and backed off")
        m.disk(reset=True)
        c0 = int(m.status()["cycles"])
        while (int(m.status()["cycles"]) - c0) / HZ < 40:
            os88marty.pace(m, 5)
        d = m.disk()
        check(hdr() == moved, "the poll took the new place %r" % (hdr(),))
        check(st() & DIA_S_DIRTY and not st() & DIA_S_RDONLY,
              "the save is still owed, and not given up (dia_st %#x)" % st())
        check(d.get("writes", 0) == 0, "no write reached the apps disk "
              "(%r)" % (d,))
        check(d.get("reads", 0) <= MAXREADS, "%d reads in 40 guest s: backed "
              "off, not every poll (at most %d)" % (d.get("reads", 0),
                                                    MAXREADS))
        out = snap(m, tmp, "apps_after.img")
        v = os88flush.Volume(open(out, "rb").read())
        names = [e.name for e in v.listdir("SYSTEM/APPDATA")]
        check("DIAPROBE.DAT" not in names, "the apps disk's SYSTEM/APPDATA "
              "has no DIAPROBE.DAT (%r)" % names)

        print("C: the system disk back - the owed save lands at home")
        m.mount(0, home)
        ui._wait(lambda: not st() & DIA_S_DIRTY, "the owed save", 120,
                 snapshot=lambda: "dia_st = %#x" % st())
        back = snap(m, tmp, "home_after.img")
        v = os88flush.Volume(open(back, "rb").read())
        try:
            dat = os88dia.parse_dat(v.read("SYSTEM/APPDATA/DIAPROBE.DAT"),
                                    0, 1)
        except Exception as e:
            dat = {"pos": False, "x": None, "y": None, "err": str(e)}
        check(dat["pos"] and (dat["x"], dat["y"]) == moved,
              "the system disk's DIAPROBE.DAT holds %r (%r)" % (moved, dat))

        print("D: closed with the apps disk in A: - declined ONCE")
        m.mount(0, apps)
        os88marty.pace(m, 3)
        w = ui.window("DiaProbe")
        ui.raise_window(w)
        w = ui.window("DiaProbe")
        ui.mo.click(*geom.close_xy(w.x, w.y), settle=0)
        txt = ui.wait_toast(says="Sys Disk", limit=60)
        check("Needs Sys Disk" in txt, "CTRL.DRV's toast asks for the "
              "system disk (%r)" % txt)
        os88marty.guest_sleep(m, 3.0)
        check(any(o.title == "DiaProbe" for o in ui.windows()),
              "the close was DECLINED: the probe is still up, so the row "
              "still on the system disk is not stranded")
        m.mount(0, back)                # the system disk as C left it
        os88marty.pace(m, 3)
        ui.close(ui.window("DiaProbe"))
        fin = snap(m, tmp, "home_fin.img")
        cfg = os88flush.Volume(open(fin, "rb").read())
        try:
            rows = os88dia.dia_rows(cfg.read("SYSTEM.CFG"))
        except Exception:
            rows = []
        check(not rows, "with the system disk back the next close goes "
              "ahead and takes the row away (%d DIA rows left)" % len(rows))
    for f in (out, back, fin):
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                         "os88disk.py"),
                            "--verify", f], capture_output=True, text=True)
        check(r.returncode == 0, "%s passes os88disk.py --verify%s"
              % (os.path.basename(f), "" if not r.returncode else
                 ": " + (r.stdout + r.stderr).strip()[-200:]))

    shutil.rmtree(tmp, ignore_errors=True)
    print("diaswap: %s" % ("PASS" if not fails else
                           "FAIL (%d): %s" % (len(fails), "; ".join(fails))))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
