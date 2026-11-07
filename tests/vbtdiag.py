#!/usr/bin/env python3
"""VBATTLED.O88's PROFILE (SPEC.md 126.8): a play timed on the machine,
written as VBPROFn.DAT beside the engine, read by tools/vbtprof.py.

    make vbattledisk && python3 tests/vbtdiag.py

MartyPC's VGA XT with 1.44 MB drives (os8088_xt_vga_144), the system disk in A: and a copy of
build/vbattlev.img in B: (VBATTLED.O88 and the demo's BATTLEV.VBT):
VBATTLED.O88 opened, played, the title watched a few guest seconds, Esc,
and B: read back on the host:

  1. B:VBPROF1.DAT is there: 'VBPR' version 5, VGA4, the bundle's name;
  2. its frames are the play's: as many as the hook drew while it ran
     (vb_prn at the end), each with a decode time, the title's clip;
  3. tools/vbtprof.py reads it and prints the play.

--arm xms: the same on QEMU with 128 MB (docs/TESTING.md's closed list,
entry 1: XMS is a 286-and-up store, every MartyPC machine an 8088), the
demo's CGA4 bundle beside VBATTLED.O88 on B: - the profile KEPT IN XMS
(4 KB of conventional memory staged, the foreground moving it up):

  x1. VBPROF1.DAT says so (+60 1), its claim the XMS one's, and the hold
      kept off it;
  x2. every frame the hook drew is in it - none dropped from the staging -
      and the reads with them (QEMU's times are not the machine's; only
      the path is held).

BROKEN ON PURPOSE (vb_prmark's call out of the hook) 2 goes red: a profile
of no frames; (vb_prdrain's call out of the play's loop) x2 goes red - the
staging fills and frames are dropped.
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests")]
import os88build    # noqa: E402
import os88fat      # noqa: E402
import os88geom as geom    # noqa: E402
import os88marty    # noqa: E402
import os88ui       # noqa: E402
import vbtprof      # noqa: E402
from cycweb import pkg_syms     # noqa: E402

fails = []


def ok(cond, what):
    print("%s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", choices=("conv", "xms"), default="conv")
    if ap.parse_args().arm == "xms":
        return xms()
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",),
                       defines=("VB_DIAG",))
    tmp = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
    try:
        b = os.path.join(tmp, "b.img")
        shutil.copy(os88build.at("build/vbattlev.img"), b)
        with os88ui.boot(os88build.at("build/os8088.img"), apps=b,
                         machine="os8088_xt_vga_144") as ui:
            m = ui.m
            w = ui.path("B:/VBATTLED.O88")
            base = struct.unpack_from("<H", m.read(
                geom.winptr(m, w.i, ui._S) + geom.W_SEG, 2))[0] << 4

            def rb(nm, k=1):
                return m.read(base + syms[nm], k)

            def rw(nm):
                return struct.unpack_from("<H", rb(nm, 2))[0]
            m.key("Enter")
            os88marty.until(m, lambda mm: rb("vb_tplay")[0] == 1, "the title",
                            poll=0.1, limit=600, guest=120)
            os88marty.until(m, lambda mm: rw("vb_prn") >= 40,
                            "40 frames of it", poll=0.1, limit=300,
                            guest=60)
            m.pause()
            drawn = rw("vb_prn")
            claimed = rw("vb_prseg")
            m.run()
            m.key("Escape")
            os88marty.until(m, lambda mm: rw("vb_prseg") == 0,
                            "the profile written", poll=0.1, limit=300,
                            guest=60)
            os88marty.ui_done(m)
            m.pause()
            back = os.path.join(tmp, "back.img")
            m.flush(1, back)
            m.run()
        fat = os88fat.Fat12(back)
        try:
            prof = bytes(fat.read("VBPROF1.DAT"))
        except Exception as e:
            prof = b""
            print("     (no VBPROF1.DAT: %s)" % e)
        p = os.path.join(tmp, "VBPROF1.DAT")
        open(p, "wb").write(prof)
        good = prof[:4] == b"VBPR"
        h, fr, rd = vbtprof.read_prof(p) if good else ({}, [], [])
        ok(good and h["ver"] == 5 and h["kind"] == "VGA4" and
           h["name"] == "BATTLEV.VBT" and claimed,
           "1. B:VBPROF1.DAT: %r, version %s (the hold's line), %s, %r" % (
               prof[:4], h.get("ver"), h.get("kind"), h.get("name")))
        clipf = [f for f in fr if f["mode"] == vbtprof.CLIP_MODE]
        ok(good and len(fr) >= drawn >= 40 and clipf and
           all(f["t"] > 0 for f in clipf if not f["stall"]),
           "2. its %d frames are the play's (%d drawn when Esc was pressed), "
           "the title's %d each with a decode time (mean %.1f ms)" % (
               len(fr), drawn, len(clipf), sum(f["t"] for f in clipf) /
               max(1, len(clipf))))
        r = subprocess.run([sys.executable, "tools/vbtprof.py", p],
                           capture_output=True, text=True)
        print("\n".join("     | " + x for x in r.stdout.splitlines()[:12]))
        ok(r.returncode == 0 and "frames profiled" in r.stdout,
           "3. tools/vbtprof.py reads it")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if fails:
        print("vbtdiag: %d FAILED" % len(fails))
        return 1
    print("vbtdiag: all passed")
    return 0


def xms():
    """The profile kept in XMS, on QEMU (the docstring's x1, x2)"""
    import os88qemu
    import os88vbt as B
    import vbtproj as P
    import xmcheck
    from vidxms import Q
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/vbattle/vbattle.asm", ("apps/",),
                       defines=("VB_DIAG",))
    tmp = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
    try:
        vbp = os.path.join(tmp, "x.vbp")
        B.demo_project(vbp, bars=2)
        pr = P.Project.load(vbp)
        vbt = os.path.join(tmp, "BATTLE.VBT")
        open(vbt, "wb").write(B.build_project(pr))
        disk = os.path.join(tmp, "vbt.img")
        sysimg = os.path.join(tmp, "sys.img")
        shutil.copy(os88build.at("build/os8088.img"), sysimg)
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "1440", os88build.at(os.environ.get(
                            "VBTD_ENGINE", "build/vbattled.o88")),
                        vbt], check=True, capture_output=True)
        q = Q(tmp, sysimg, disk, 128)
        xmcheck.wait_desktop(q.sock, "vbtdiag")
        S = xmcheck.sym
        ui = os88ui.UI(q, mouse=object(), sym=S, verbose=False)
        mo = os88qemu.Mouse(q, S, q.hmp, after=0)

        def wait(cond, what, secs=60):
            if not os88qemu.acted(q, cond, secs=secs, what=what, poll=0.05):
                raise SystemExit("vbtdiag: timed out waiting for %s" % what)
        x, y = geom.drive_pt(q, "B", S)
        mo.dblclick(x, y)
        box = {}

        def diskwin():
            for w in ui.windows():
                if w.visible and ui.fs_of(w) == (1, 0, 1):
                    box["w"] = w
                    return True
            return False
        wait(diskwin, "drive B's window", 30)
        os88qemu.ui_done(q, S, cap=1.0)
        i, _ = ui.entry(os.path.basename(os.environ.get(
            "VBTD_ENGINE", "VBATTLED.O88")).upper(), box["w"])
        before = set(o.i for o in ui.windows())
        mo.dblclick(*ui.row_xy(box["w"], i - ui.scroll(box["w"])))

        def engine():
            for o in ui.windows():
                if o.i not in before and o.visible:
                    box["p"] = o
                    return True
            return False
        wait(engine, "the battle's window", 30)
        os88qemu.ui_done(q, S, cap=1.0)
        rec = q.read(S("wm_wins") + box["p"].i * geom.WIN_SIZE,
                     geom.WIN_SIZE)
        base = struct.unpack_from("<H", rec, geom.W_SEG)[0] << 4

        def rb(n):
            return q.read(base + syms[n], 1)[0]

        def rw(n):
            return struct.unpack_from("<H", q.read(base + syms[n], 2))[0]
        if os.environ.get("VBTD_HCAP"):     # (a pool under the bundle: the
            from pxswin import qpoke        # cache in SLOTS)
            qpoke(q, [(base + syms["vb_hcap"], struct.pack(
                "<H", int(os.environ["VBTD_HCAP"])))])
        q.hmp("sendkey ret")
        if os.environ.get("VBTD_DBG"):
            import time
            for _ in range(20):
                time.sleep(2)
                print("   ready %d tplay %d err %04x status %04x prx %d prkb %d "
                      "prseg %04x" % (rb("vb_ready"), rb("vb_tplay"),
                                      rw("vb_err"), rw("vb_status"),
                                      rb("vb_prx"), rw("vb_prkb"),
                                      rw("vb_prseg")), flush=True)
                print("   says: %r" % q.read(base + rw("vb_status"), 60)
                      .split(b"\0")[0])
        wait(lambda: rb("vb_tplay") == 1, "the title", 120)
        wait(lambda: rw("vb_prn") >= 500, "500 frames of it (the staging's "
             "400 round once)", 300)
        kept = rb("vb_prx")
        q.hmp("sendkey ret")            # (the menu, and a key timed to its
        wait(lambda: rb("vb_state") == 1, "the menu", 240)  # band: x3)
        q.hmp("sendkey down")
        wait(lambda: rw("vb_prkn") >= 1, "the key timed to its band", 60)
        q.hmp("sendkey esc")
        wait(lambda: rw("vb_prseg") == 0 and rb("vb_prx") == 0,
             "the profile written", 120)
        drawn = rw("vb_prn")
        os88qemu.ui_done(q, S, cap=1.0)
        q.close()
        fat = os88fat.Fat12(disk)
        try:
            prof = bytes(fat.read("VBPROF1.DAT"))
        except Exception as e:
            prof = b""
            print("     (no VBPROF1.DAT: %s)" % e)
        p = os.path.join(tmp, "VBPROF1.DAT")
        open(p, "wb").write(prof)
        good = prof[:4] == b"VBPR"
        h, fr, rd = vbtprof.read_prof(p) if good else ({}, [], [])
        ok(good and kept == 1 and prof[60] == 1 and 40 < h["claimkb"] < 64,
           "x1. VBPROF1.DAT kept in XMS: the play's flag %d, the file's +60 "
           "%s, a %s KB block (under the 64 KB a pool keeps for debugging)" % (kept, prof[60] if good else None,
                                  h.get("claimkb")))
        drop = struct.unpack_from("<H", prof, 62)[0] if good else -1
        clipf = [f for f in fr if f["mode"] == vbtprof.CLIP_MODE and
                 not f["stall"]]
        ok(good and len(fr) == drawn >= 500 and drop == 0 and rd and
           clipf and all(f["t"] > 0 for f in clipf),
           "x2. every frame the hook drew is in it (%d of %d, %d dropped, "
           "the clips' each with a time), and %d reads" % (
               len(fr), drawn, drop, len(rd)))
        per = h.get("div", 0) * vbtprof.PIT_US / 1000
        ok(good and h["ver"] == 5 and h.get("cache") == 1 and
           h.get("iinp") == 1 and h.get("pool", 0) > 1024 and
           h.get("irings", 0) >= 1 and h.get("keys", 0) >= 1 and
           h["kmax"] * per < 2000,
           "x3. version 4: the CACHE on (pool %s KB, %s KB filled, %s rings "
           "in place, the idle loop in the block %s), %s key(s) to their "
           "band's redraw, %.0f ms at most" % (
               h.get("pool"), h.get("cfkb"), h.get("irings"), h.get("iinp"),
               h.get("keys"), h.get("kmax", 0) * per))
        cf = [f["periods"] for f in fr[1:] if f["mode"] == vbtprof.CLIP_MODE
              and f["periods"] is not None]
        mean = sum(cf) / float(max(1, len(cf)))
        due = 1000.0 / h.get("fps", 15) / max(1e-6, per)
        ok(good and h.get("copies", 0) > 0 and h.get("cmax", 0) > 0 and
           cf and abs(mean - due) < 0.5,
           "x4. version 5: %s XMS copies timed (%.2f ms at the longest), and "
           "a clip frame's gap in the hook's periods, %.2f on average "
           "against the frame's %.2f" % (h.get("copies"), h.get("cmax", 0),
                                         mean, due))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if fails:
        print("vbtdiag: %d FAILED" % len(fails))
        return 1
    print("vbtdiag: all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
