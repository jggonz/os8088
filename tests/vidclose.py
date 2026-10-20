#!/usr/bin/env python3
"""vidclose - SPEC.md 98.3.18.10: the close box pressed while the file's
HOLD is still loading into XMS from the window's timer.

The field's report (a 286, XMS): play, Esc, and close the window quickly
while the disk is still busy - and the machine froze, once in seven. Esc
ends the session; what keeps the disk busy after it is vp_ontimer ->
vp_xstep reading the rest of the file into its hold (98.3.18). What this
gates is the ORDER of the teardown: the player's close negotiator stops any
session (vp_sstop, the path Stop takes) and gives the hold back BEFORE the
kernel frees the instance, so the region the kernel frees holds nothing of a
play or a load still alive.

Read off the FREED region, which nothing has reused yet: [vp_sess] and
[vp_xon] are 0 only if the negotiator ran. Then XMEM.DRV's table holds no
block of any instance's, the machine ticks, and the next round opens the
player again. Three rounds, Esc at a different frame each, B: throttled so
the hold is mid-load at every close (the row says so, or it fails as a test
of nothing).

QEMU by name: MartyPC has no XMS (SPEC.md 41.7). Break on purpose: take the
negotiator's install out of vp_entry and the first round fails on [vp_xon].
"""
import os
import random
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tests"), os.path.join(ROOT, "tools")]
os.chdir(ROOT)
import os88build                                              # noqa: E402
import os88geom as geom                                       # noqa: E402
import os88qemu                                               # noqa: E402
import os88ui                                                 # noqa: E402
import vidbank                                                # noqa: E402
import xmcheck                                                # noqa: E402
from cycweb import pkg_syms                                   # noqa: E402
from vidxms import Q, u16                                     # noqa: E402

ROUNDS = 3


def main():
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    rnd = random.Random(7)
    tmp = tempfile.mkdtemp(dir=os.path.join(ROOT, "build"))
    bad = []
    q = None
    try:
        v88 = vidbank.clip(tmp, sound=True)
        disk = os.path.join(tmp, "d.img")
        sysimg = os.path.join(tmp, "sys.img")
        shutil.copy(os88build.at("build/os8088.img"), sysimg)
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "1440", v88,
                        os88build.at("build/video.o88")],
                       check=True, capture_output=True)
        # a SLOW disk, so the bank is still filling when the close lands
        q = Q(tmp, sysimg, disk, 4, bps=65536)
        os88qemu.acted(q, lambda: os.path.exists(q.sock), secs=30,
                       what="the QMP socket")
        xmcheck.wait_desktop(q.sock, "vidclose")
        S = xmcheck.sym
        xbase = xmcheck.table_base(q.sock)
        ui = os88ui.UI(q, mouse=object(), sym=S, verbose=False)
        x, y = geom.drive_pt(q, "B", S)
        xmcheck.dblclick(q.sock, x, y)
        box = {}

        def diskwin():
            for w in ui.windows():
                if w.visible and ui.fs_of(w) == (1, 0, 1):
                    box["w"] = w
                    return True
            return False
        if not os88qemu.acted(q, diskwin, secs=30, what="Disk B"):
            bad.append("Disk B never opened")
            return report(bad)
        os88qemu.pace(q, 1)
        for it in range(ROUNDS):
            w = box["w"]
            i, _ = ui.entry("CLIP.V88", w)
            before = set(o.i for o in ui.windows())
            xmcheck.dblclick(q.sock, *ui.row_xy(w, i - ui.scroll(w)))

            def player():
                for o in ui.windows():
                    if o.i not in before and o.visible:
                        box["p"] = o
                        return True
                return False
            if not os88qemu.acted(q, player, secs=30, what="the player"):
                bad.append("round %d: the player did not open" % it)
                break
            p = box["p"]
            rec = q.read(S("wm_wins") + p.i * geom.WIN_SIZE, geom.WIN_SIZE)
            base = u16(rec, geom.W_SEG) << 4

            def rb(n):
                return q.read(base + syms[n], 1)[0]

            def rw(n):
                return u16(q.read(base + syms[n], 2))
            os88qemu.acted(q, lambda: rb("vp_loaded") == 1, secs=30,
                           what="the file loaded")
            os88qemu.pace(q, 0.5)
            q.hmp("sendkey p")
            if not os88qemu.acted(q, lambda: rb("vp_ready") == 1, secs=60,
                                  what="the play"):
                bad.append("round %d: the play never started" % it)
                break
            nfr = rnd.randrange(20, 120)
            os88qemu.acted(q, lambda: rw("vp_done") >= nfr, secs=30,
                           what="frame %d" % nfr)
            q.hmp("sendkey esc")
            if not os88qemu.acted(q, lambda: rb("vp_ready") == 0, secs=20,
                                  what="the bracket's end"):
                bad.append("round %d: Esc did not end the bracket" % it)
                break
            os88qemu.pace(q, 0.3)
            sess, xon, xfull = rb("vp_sess"), rb("vp_xon"), rb("vp_xfull")
            have = u16(q.read(base + syms["vp_xhave"], 4)) | \
                u16(q.read(base + syms["vp_xhave"] + 2, 2)) << 16
            print("   round %d: Esc at frame %d (drawn %d); session %d, "
                  "hold %d, %d KB of it loaded%s"
                  % (it, nfr, rw("vp_done"), sess, xon, have // 1024,
                     ", whole" if xfull else ""))
            if not xon or xfull:
                bad.append("round %d: the close does not land on a hold "
                           "still loading - this tests nothing" % it)
            pw = [o for o in ui.windows() if o.i == p.i][0]
            xmcheck.click(q.sock, *geom.close_xy(pw.x, pw.y))
            if not os88qemu.acted(
                    q, lambda: all(o.i != p.i for o in ui.windows()),
                    secs=20, what="the close"):
                bad.append("round %d: the window did not close" % it)
                break
            s2, x2 = rb("vp_sess"), rb("vp_xon")
            own = [b for b in xmcheck.blocks(q.sock, xbase)
                   if b[3] != xmcheck.XM_OWN_KERN]
            t0 = q.read(0x46C, 2)
            alive = os88qemu.acted(q, lambda: q.read(0x46C, 2) != t0,
                                   secs=5, what="a tick")
            print("   ...closed: the freed region's session %d, hold %d; "
                  "%d instance block(s) in XMS; ticking %s"
                  % (s2, x2, len(own), alive))
            if s2 or x2:
                bad.append("round %d: the region was freed with a play or "
                           "a hold alive (vp_sess %d, vp_xon %d): no "
                           "negotiator stopped it" % (it, s2, x2))
            if own:
                bad.append("round %d: %d XMS block(s) still owned"
                           % (it, len(own)))
            if not alive:
                bad.append("round %d: the machine stopped ticking" % it)
                break
            os88qemu.pace(q, 0.5)
    finally:
        if q is not None:
            q.close()
        shutil.rmtree(tmp, ignore_errors=True)
    return report(bad)


def report(bad):
    for b in bad:
        print("   FAIL: %s" % b)
    print("vidclose: %s" % ("FAIL" if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
