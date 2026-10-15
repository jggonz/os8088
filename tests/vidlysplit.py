#!/usr/bin/env python3
"""THE POOL SPLIT FOR THE MACHINE IT IS - SPEC.md 98.1.9.2, on QEMU.

    make && python3 tests/vidlysplit.py [--arm layer|base]

WHY QEMU: docs/TESTING.md's closed list, entry 1 - XMS is a 286-and-up
store and every MartyPC machine is an 8088.

vidlysplit's clip - a layer and a base bank, bigger than `-m 2`'s ~832 KB
pool - asking for a base bank of 256 KB and a LAYER bank of 800, and its
disk carrying the base with no bank (H_LYNEED 0). At the open both banks
are taken before anything knows the machine: the base's to its ask, the
layer's from what is left (576 KB). The CPU is timed at the play's start,
and the pool is split then:

ARM layer: QEMU is the faster machine, the layer is read - so the base's
bank goes (it needs none here) and the layer's is its whole ask, 800 KB:
25 slots where the open took 18.

ARM base: the yardstick poked impossibly fast, so this CPU is the BASE'S
machine and the layer is not read - its bank is given back, and the base's
stays its 256 KB.

ARM diskslow - THE DISK TEST (98.1.9.3): the file's two disks set to 4000
and 3000 KB/s, far past anything QEMU's floppy reads - so the CPU passes and
the DISK, timed on the ring's fill, says this is the base's machine: the
layer off, the card's reason the disk, and the layer's bank given back,
the base's kept at its 256 KB. ARM layer is the other half of it: the
encode's 59 and 20 KB/s (286-slow has no curve, so its idle disks are
its disks) put the line at 56, so the disk test RUNS and says
yes - which that arm asserts, so a test that never measured cannot pass as
one that passed. This is the LOGIC only: QEMU's floppy read stalls the
guest while its clock runs on, so the rate it times is not B:'s throttle
(~200 KB/s against 64). The instrument's accuracy is vidlyplaydisk's, on
MartyPC's cycle-exact 5150.

ARM flip - THE LIVE FLIP (98.1.9.4): ARM layer with the layer's own claim
refused (vp_lynoc, a gate's byte), so its slots are the RING claim's top -
as many as its own claim would take (four here, out of the ring's headroom),
its mirror (a whole slot when carved, 98.1.9.8) and its bank's bounce,
right after the base bank's bounce: the ring that
many slots fewer than the machine gave it, the layer read, and at the
split the base's bank what this machine needs (none) plus what the ring
lent of the header's ring ([vp_lycmp]), the layer's its 25 slots. Broken
on purpose (vp_lycarve not called) the layer is not read. ARM flipcmp is
the PAY-BACK: the header's ring poked to 10, so the carved ring (7 of 11)
is three short of it and lends 96 KB, which the split must give back to the
base's bank - as VP_XBMIN's 128 KB, the least bank there is, four slots,
and the layer what is left of the ~26.

Both play from KEY 1: the CPU is timed on a key, a one-bit file from its
first frame has none, and untimed the pool is left as the open took it -
the base's bank is never risked on a guess. ARM untimed is that: from the
first frame, nothing moves.
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


def clip(tmp, disks=None):
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
         "--layer-disk", "60000", "--layer-memory", "64",
         "--layer-bank", "800"], check=True)
    # NO PREFILL: the bank and the layer's slots are filled before the
    # first frame otherwise, and the order is over before it can be seen
    d = bytearray(open(out, "rb").read())
    struct.pack_into("<H", d, vid.H_XPRE, 0)
    if disks:                       # (the layer's, the base's: KB/s)
        struct.pack_into("<H", d, vid.H_LYIDL, disks[0])
        struct.pack_into("<H", d, vid.H_LYBKS, disks[1])
    open(out, "wb").write(d)
    vid.verify_v88(out)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", choices=("layer", "base", "untimed",
                                      "diskslow", "flip", "flipcmp"),
                    default="layer")
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    sysimg0 = os88build.at("build/os8088.img")
    for p in (pkg, sysimg0):
        if not os.path.exists(p):
            sys.exit("vidlysplit: no %s - run `make`" % p)
    if not shutil.which("ffmpeg"):
        print("vidlysplit: SKIP - no ffmpeg")
        return 0
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        v88 = clip(tmp, (4000, 3000) if a.arm == "diskslow" else None)
        r = vid.Reader(v88)
        print("   clip: %d frames, %d KB, layer order %d (the bank kept %d KB),"
              " bank asked %d KB, prefill %d" % (r.frames, os.path.getsize(v88)
                                                // 1024, r.lyord, r.lyneed,
                                                r.xbank, r.xpre))
        if r.lyord != 1 or not r.lsp0 or not r.xbank:
            sys.exit("vidlysplit: the encode wrote no layer-first file")
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
        xmcheck.wait_desktop(q.sock, "vidlysplit")
        S = xmcheck.sym
        ui = os88ui.UI(q, mouse=object(), sym=S, verbose=False)
        state = lambda: "(no window yet)"           # noqa: E731

        def wait(cond, what, secs):
            if not os88qemu.acted(q, cond, secs=secs, what=what, poll=0.05):
                raise SystemExit("vidlysplit: timed out waiting for %s (%s)"
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
        n0, l0 = rw("vp_xbn") if rb("vp_xbank") else 0, rw("vp_lyxn")
        print("   at the open: the base's bank %d slots, the layer's %d"
              % (n0, l0))
        if n0 != 8 or l0 != 18:
            bad.append("the open took %d and %d slots, not the base's ask "
                       "(8) and the rest (18) - the fixture is not what the "
                       "row tests" % (n0, l0))
        # FROM KEY 1: a one-bit file from its first frame has no key for the
        # CPU to be timed on, and untimed the pool is not split
        if a.arm != "untimed":
            s0 = rw("vp_sel")
            q.hmp("sendkey right")
            wait(lambda: rw("vp_sel") != s0, "Right to key 1", 20)
        if a.arm in ("flip", "flipcmp"):  # (the layer's own claim refused)
            qpoke(q, [(base + syms["vp_lynoc"], b"\1")])
        if a.arm == "flipcmp":          # (the header's ring past the carve)
            qpoke(q, [(base + syms["vp_rneed"], b"\x0a")])
        if a.arm == "base":             # (the yardstick impossibly fast:
            qpoke(q, [(base + syms["vp_lytl"], struct.pack("<H", 1)),
                      (base + syms["vp_lytb"], struct.pack("<H", 1))])
        q.hmp("sendkey p")
        wait(lambda: rb("vp_ready") == 1, "the play", 120)
        n1, l1 = rw("vp_xbn") if rb("vp_xbank") else 0, rw("vp_lyxn")
        print("   playing: %s; the base's bank %d slots, the layer's %d"
              % (state(), n1, l1))
        dv, dkb = rb("vp_lydv"), rw("vp_lydkb")
        print("   the disk test: verdict %d (1 yes, 2 no), this disk %d KB/s, "
              "the file's %d and %d" % (dv, dkb, rw("vp_lykbs"),
                                        rw("vp_lybks")))
        if a.arm == "layer" and (dv != 1 or dkb < rw("vp_lydln")):
            bad.append("the disk test did not run and say yes (verdict %d, "
                       "%d KB/s against the line's %d)"
                       % (dv, dkb, rw("vp_lydln")))
        if a.arm == "diskslow":
            if dv != 2 or not 0 < dkb < rw("vp_lydln"):
                bad.append("the disk test said %d at %d KB/s, not no under "
                           "the line's 3500" % (dv, dkb))
            if rb("vp_lyon") or not rb("vp_lywhy"):
                bad.append("the layer is %s, its reason %d - not off for "
                           "the disk" % ("on" if rb("vp_lyon") else "off",
                                         rb("vp_lywhy")))
            if (n1, l1) != (8, 0):
                bad.append("the base's machine by its disk kept %d slots for "
                           "the base and %d for the layer, not 8 and 0"
                           % (n1, l1))
        if a.arm in ("flip", "flipcmp"):
            k, k0, lent = rw("vp_k"), rw("vp_lyk0"), rb("vp_lyb")
            ring, lyseg, msl = rw("vp_ring"), rw("vp_lyseg"), rw("vp_msl")
            cmp = rw("vp_lycmp")
            print("   the flip: %d slots lent, the ring %d of %d, the layer "
                  "at %04x (the ring %04x), %d KB of the header's ring lent"
                  % (lent, k, k0, lyseg, ring, cmp))
            # (its slots, its MIRROR - a whole slot when carved, 98.1.9.8 -
            # and its bank's bounce)
            if not rb("vp_lyon") or lent != rw("vp_lyns") + 2 or \
                    k != k0 - lent:
                bad.append("the flip: layer %s, %d slots lent, ring %d of "
                           "%d - not the layer read out of three"
                           % ("on" if rb("vp_lyon") else "off", lent, k, k0))
            elif lyseg != ring + (k + msl + 1) * 2048:
                bad.append("the layer's slots at %04x, not past the ring's "
                           "%d, its mirror and the bank's bounce (%04x)"
                           % (lyseg, k, ring + (k + msl + 1) * 2048))
            elif a.arm == "flipcmp" and (cmp != (10 - k) * 32 or n1 * 32
                                         != max(cmp, 128) or
                                         not 20 <= l1 <= 24):
                bad.append("the pay-back: the ring lent %d KB (%d short of "
                           "10), the base kept %d slots and the layer %d"
                           % (cmp, 10 - k, n1, l1))
            elif a.arm == "flip" and (n1 * 32 != cmp or l1 != 25):
                bad.append("the split kept %d slots for the base (the ring "
                           "lent %d KB) and gave the layer %d, not 25"
                           % (n1, cmp, l1))
        elif a.arm == "layer":
            if not rb("vp_lyon"):
                bad.append("the layer is not read on QEMU - nothing tested")
            elif n1 != 0 or l1 != 25:
                bad.append("the layer's machine kept %d slots for the base "
                           "(it needs none) and gave the layer %d, not 25"
                           % (n1, l1))
        elif a.arm == "untimed":
            if rw("vp_lytm") or (n1, l1) != (n0, l0):
                bad.append("untimed (bench %d), the pool moved: %d and %d "
                           "slots, where the open took %d and %d"
                           % (rw("vp_lytm"), n1, l1, n0, l0))
        elif a.arm in ("flip", "flipcmp"):
            pass
        elif rb("vp_lyon") or n1 != 8 or l1 != 0:
            bad.append("the base's machine (layer %s) kept %d slots for the "
                       "base and %d for the layer, not 8 and 0"
                       % ("on" if rb("vp_lyon") else "off", n1, l1))
        q.close()
    for b in bad:
        print("   FAIL: %s" % b)
    print("vidlysplit (%s): %s" % (a.arm, "FAIL" if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
