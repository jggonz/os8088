#!/usr/bin/env python3
"""THE LAYER BEFORE THE BASE'S BANK - SPEC.md 98.1.9.1, on QEMU.

    make && python3 tests/vidlyorder.py [--arm layer|bank]

WHY QEMU: docs/TESTING.md's closed list, entry 1 - XMS is a 286-and-up
store and every MartyPC machine is an 8088.

A file with a LAYER and a base BANK, bigger than `-m 2`'s pool so the base
takes a bank and not a hold: 28 seconds of Life made for a 20 KB/s disk with
a 256 KB bank and no prefill, its layer for 60 KB/s. The encode models the
faster machine reading the layer BEFORE it refills the base's bank, which
makes the header's layer-order byte 1, and the player reads in that order:
its ring, then the layer's slots, then the bank. B: is throttled, so the
order is seen happening.

The play is held before frame 20 (vp_stopat): the hook takes nothing more,
the ring is full, and the reader goes on to the bank - the layer's slots
having been filled before the first frame, in either order. With the bank
part full the layer's slots are marked TAKEN ([vp_lyuse] moved on by all of
them), and what the reader refills first is the order:

ARM layer: the layer's slots are full again before the bank takes more
than ONE chunk while they had room - the one it was reading when they were
marked - and the layer was read. Every look is taken with the machine
stopped, so the figure is the order and not the looks' interval.

The layer comes first only while the bank holds what the faster machine
needs to carry the base (the header's H_LYNEED, this clip's a slot or two),
which the bank is well past when the slots are marked.

ARM bank: the NEGATIVE CONTROL and the threshold's own test - [vp_lyneed]
poked past the bank's size before Play, so the bank is always short of it:
the bank goes on filling first, as every file before was read, which is
also what says the instrument tells the two orders apart.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88qemu                                             # noqa: E402
import os88build                                            # noqa: E402
import os88geom as geom                                     # noqa: E402
import os88ui                                               # noqa: E402
import os88vid as vid                                       # noqa: E402
import xmcheck                                              # noqa: E402
from cycweb import pkg_syms                                 # noqa: E402
from vidxms import Q, u16                                   # noqa: E402
from pxswin import qpoke                                    # noqa: E402

BPS = 64 * 1024                 # B:'s throttle: the order seen happening
HOLD = 20


def clip(tmp):
    src = os.path.join(tmp, "life.mkv")
    subprocess.run(
        ["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
         "life=s=640x200:mold=10:r=15:ratio=0.4:seed=5088:death_color=#000000:"
         "life_color=#ffffff,trim=duration=28", "-c:v", "ffv1", src],
        check=True)
    out = os.path.join(tmp, "CLIP.V88")
    subprocess.run(
        [sys.executable, "tools/os88venc.py", src, out, "--quiet",
         "--preset", "cga", "--profile", "286-slow", "--audio", "none",
         "--fps", "15", "--disk", "20000", "--reserve", "32",
         "--bank", "256", "--prefill", "0",
         "--layer-disk", "60000", "--layer-memory", "64"], check=True)
    # NO PREFILL: the bank and the layer's slots are filled before the
    # first frame otherwise, and the order is over before it can be seen
    d = bytearray(open(out, "rb").read())
    struct.pack_into("<H", d, vid.H_XPRE, 0)
    open(out, "wb").write(d)
    vid.verify_v88(out)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", choices=("layer", "bank"), default="layer")
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    sysimg0 = os88build.at("build/os8088.img")
    for p in (pkg, sysimg0):
        if not os.path.exists(p):
            sys.exit("vidlyorder: no %s - run `make`" % p)
    if not shutil.which("ffmpeg"):
        print("vidlyorder: SKIP - no ffmpeg")
        return 0
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        v88 = clip(tmp)
        r = vid.Reader(v88)
        print("   clip: %d frames, %d KB, layer order %d (the bank kept %d KB),"
              " bank asked %d KB, prefill %d" % (r.frames, os.path.getsize(v88)
                                                // 1024, r.lyord, r.lyneed,
                                                r.xbank, r.xpre))
        if r.lyord != 1 or not r.lsp0 or not r.xbank:
            sys.exit("vidlyorder: the encode wrote no layer-first file")
        disk = os.path.join(tmp, "clip.img")
        sysimg = os.path.join(tmp, "sys.img")
        shutil.copy(sysimg0, sysimg)
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "1440", v88, pkg], check=True,
                       capture_output=True)
        q = Q(tmp, sysimg, disk, 2, bps=BPS)
        for _ in range(150):
            if os.path.exists(q.sock):
                break
            time.sleep(0.2)
        xmcheck.wait_desktop(q.sock, "vidlyorder")
        S = xmcheck.sym
        ui = os88ui.UI(q, mouse=object(), sym=S, verbose=False)
        state = lambda: "(no window yet)"           # noqa: E731

        def wait(cond, what, secs):
            if not os88qemu.acted(q, cond, secs=secs, what=what, poll=0.05):
                raise SystemExit("vidlyorder: timed out waiting for %s (%s)"
                                 % (what, state()))
        box = {}
        x, y = geom.drive_pt(q, "B", S)
        xmcheck.dblclick(q.sock, x, y)

        def diskwin():
            for w in ui.windows():
                if w.visible and ui.fs_of(w) == (1, 0, 1):
                    box["w"] = w
                    return True
            return False
        wait(diskwin, "drive B's window", 30)
        os88qemu.pace(q, 1)
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
        wait(player, "the Video Player's window", 30)
        rec = q.read(S("wm_wins") + box["p"].i * geom.WIN_SIZE, geom.WIN_SIZE)
        base = u16(rec, geom.W_SEG) << 4
        rb = lambda n: q.read(base + syms[n], 1)[0]          # noqa: E731
        rw = lambda n: u16(q.read(base + syms[n], 2))        # noqa: E731

        def state():
            return ("done=%d held=%d lyon=%d lyord=%d lyeof=%d ly %d/%d bank %d/%d "
                    "xbank=%d" % (rw("vp_done"), rb("vp_held"), rb("vp_lyon"),
                                  rb("vp_lyord"), rb("vp_lyeof"), (rw("vp_lyrd") -
                                                   rw("vp_lyuse")) & 0xFFFF,
                                  rw("vp_lyns"), rw("vp_xcnt"), rw("vp_xbn"),
                                  rb("vp_xbank")))
        wait(lambda: rb("vp_loaded") == 1, "the clip's header", 30)
        print("   opened: %s" % state())
        if rb("vp_xbank") != 1 or rb("vp_lyord") != 1:
            sys.exit("vidlyorder: no base bank, or the order byte unread "
                     "(%s)" % state())
        if a.arm == "bank":             # (the bank below what the base
            qpoke(q, [(base + syms["vp_lyneed"],    # needs: all of it)
                       struct.pack("<H", 0xFFE0))])
        qpoke(q, [(base + syms["vp_stopat"], struct.pack("<H", HOLD))])
        q.hmp("sendkey p")
        wait(lambda: rb("vp_held") == 1 and rw("vp_done") == HOLD,
             "the hold before frame %d" % HOLD, 120)
        # THE LAYER'S SLOTS EMPTIED, with the bank part full: what the reader
        # refills first is the order. (The start fills the layer's slots
        # before the first frame in either order, and a held play takes
        # nothing, so the slots are marked taken: [vp_lyuse] moved on by
        # every slot - the hook is stopped, and the play ends here)
        wait(lambda: 0 < rw("vp_xcnt") <= rw("vp_xbn") - 8,
             "the bank part full", 120)
        q.hmp("stop")                   # (read and poked at one instant:
        x0 = rw("vp_xcnt")              # the bank reads on between QMP calls)
        qpoke(q, [(base + syms["vp_lyuse"],
                   struct.pack("<H", (rw("vp_lyuse") + rw("vp_lyns"))
                               & 0xFFFF))])
        q.hmp("cont")
        fill = lambda: (rw("vp_lyrd") - rw("vp_lyuse")) & 0xFFFF  # noqa
        # ...watched in FROZEN looks: how far the bank got while the layer's
        # slots still had room is the order, whatever a look's interval
        room_x = x0
        t0 = time.time()
        while time.time() - t0 < 120:
            q.hmp("stop")
            f, x, ns = fill(), rw("vp_xcnt"), rw("vp_lyns")
            eof, xn = rb("vp_lyeof"), rw("vp_xbn")
            q.hmp("cont")
            if f < ns:
                room_x = max(room_x, x)
            if f >= ns or eof or x >= xn:
                break
            time.sleep(0.05)
        x1, f1 = room_x, f
        print("   the layer's slots emptied with the bank at %d of %d: while "
              "they had room it got to %d (%s)"
              % (x0, rw("vp_xbn"), x1, state()))
        if not rb("vp_lyon"):
            bad.append("the layer is not being read here - nothing tested")
        elif a.arm == "layer":
            if x1 - x0 > 1 or f1 < rw("vp_lyns"):
                bad.append("the bank took %d chunks before the layer's "
                           "slots were refilled: the bank was read first"
                           % (x1 - x0))
        elif x1 - x0 < 4:
            bad.append("read bank first, the layer's slots refilled before "
                       "the bank took a chunk - the instrument does not tell "
                       "the orders apart")
        q.close()
    for b in bad:
        print("   FAIL: %s" % b)
    print("vidlyorder (%s): %s" % (a.arm, "FAIL" if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
