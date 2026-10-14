#!/usr/bin/env python3
"""THE LAYER's XMS BANK and its PREFILL (SPEC.md 98.1.9) - on QEMU.

    make && python3 tests/vidlybank.py [--arm bank|nobank]

WHY QEMU: docs/TESTING.md's closed list, entry 1 - XMS is a 286-and-up
store and every MartyPC machine is an 8088.

THE INSTRUMENT IS tests/vidxms.py's DISK SWAP. A six-second Life clip made
for a 12 KB/s disk with a layer for 40 KB/s, `--layer-bank 512
--layer-prefill all`, is small enough to be held whole in XMS - the base's
source - and its layer in the player's two slots and its bank. Play holds
at the layer's prefill's end (vp_pfwait); B: is then changed for a BLANK
floppy. Every layer record drawn after that came out of the slots or the
bank, which the disk can no longer answer for.

ARM bank: the file opens with a layer bank (vp_lyxn); the prefill fills
the slots and the bank with the whole layer; the play reaches the end and
DRAWS EVERY LAYER RECORD, none missed - the slots fed from the bank as the
hook let them go (vp_lydrain), copied down out of XMS.

ARM nobank: the NEGATIVE CONTROL - the bank's slots zeroed before Play
(vp_lyxn 0), so the prefill fills the two slots and nothing more. After the
swap the layer's reader finds no disk and stops, and the play must draw
FEWER layer records than the file has. That is what says the swap bites.
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

SKIP = 77


def clip(tmp):
    src = os.path.join(tmp, "life.mkv")
    subprocess.run(
        ["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
         "life=s=640x200:mold=10:r=15:ratio=0.4:death_color=#000000:"
         "life_color=#ffffff,trim=duration=6", "-c:v", "ffv1", src],
        check=True)
    out = os.path.join(tmp, "CLIP.V88")
    subprocess.run(
        [sys.executable, "tools/os88venc.py", src, out, "--quiet",
         "--preset", "cga", "--profile", "5150-st225", "--audio", "none",
         "--fps", "15", "--disk", "12000", "--reserve", "32",
         "--layer-disk", "40000", "--layer-memory", "64",
         "--layer-bank", "512", "--layer-prefill", "all"], check=True)
    vid.verify_v88(out)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", choices=("bank", "nobank"), default="bank")
    a = ap.parse_args()
    if not shutil.which("ffmpeg"):
        print("   SKIP: needs ffmpeg")
        return SKIP
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    sysimg0 = os88build.at("build/os8088.img")
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        v88 = clip(tmp)
        r = vid.Reader(v88)
        lrec = sum(1 for f, rec, *_ in r.layer_records() if rec is not None)
        # the 32 KB chunks the player reads the layer in, to the file's end
        # (98.1.9.8: one continuous run, its key table after it)
        lsps = -(-(len(r.d) - r.lsp0) // vid.SLOT)
        print("   clip: %d frames, %d bytes; a layer of %d records in %d "
              "chunks, its bank asked %d KB"
              % (r.frames, len(r.d), lrec, lsps, r.lbank))
        disk = os.path.join(tmp, "vidly.img")
        blank = os.path.join(tmp, "blank.img")
        sysimg = os.path.join(tmp, "sys.img")
        shutil.copy(sysimg0, sysimg)
        for out, files in ((disk, [v88, pkg]), (blank, [])):
            subprocess.run([sys.executable, "tools/os88disk.py", "-o", out,
                            "--size", "1440"] + files, check=True,
                           capture_output=True)
        q = Q(tmp, sysimg, disk, 128)
        for _ in range(150):
            if os.path.exists(q.sock):
                break
            time.sleep(0.2)
        xmcheck.wait_desktop(q.sock, "vidlybank")
        S = xmcheck.sym
        ui = os88ui.UI(q, mouse=object(), sym=S, verbose=False)

        def wait(cond, what, secs):
            if not os88qemu.acted(q, cond, secs=secs, what=what, poll=0.05):
                raise SystemExit("vidlybank: timed out waiting for %s (%s)"
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
        state = lambda: "(no window yet)"           # noqa: E731
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

        def rb(n):
            return q.read(base + syms[n], 1)[0]

        def rw(n):
            return u16(q.read(base + syms[n], 2))

        def wb(n, v):
            qpoke(q, [(base + syms[n], v)])

        def state():
            return ("done=%d err=%d on=%d got=%d mis=%d skp=%d rd=%d use=%d "
                    "ns=%d xn=%d xc=%d xq=%d eof=%d xon=%d xfull=%d"
                    % (rw("vp_done"), rb("vp_err"), rb("vp_lyon"),
                       rw("vp_lygot"), rw("vp_lymis"), rw("vp_lyskp"),
                       rw("vp_lyrd"), rw("vp_lyuse"), rw("vp_lyns"),
                       rw("vp_lyxn"), rw("vp_lyxc"), rb("vp_lyxq"),
                       rb("vp_lyeof"), rb("vp_xon"), rb("vp_xfull")))
        wait(lambda: rb("vp_loaded") == 1, "the clip's header", 30)
        print("   opened: %s; lysp %08x bank %d KB prefill %04x"
              % (state(), struct.unpack("<I", q.read(base + syms["vp_lysp"],
                                                      4))[0],
                 rw("vp_lybkb"), rw("vp_lypkb")))
        if rw("vp_lyxn") < 2:
            sys.exit("vidlybank: the layer took no XMS bank (%s)" % state())
        if rb("vp_xon") != 1:
            sys.exit("vidlybank: the file was not held (%s)" % state())
        wait(lambda: rb("vp_xfull") == 1, "the hold to fill", 120)
        if a.arm == "nobank":
            wb("vp_lyxn", b"\0\0")
        wb("vp_pfwait", b"\1")
        q.hmp("sendkey p")
        wait(lambda: rb("vp_pfly") == 1 and
             rw("vp_lyrd") - rw("vp_lyuse") + rw("vp_lyxc") >=
             min(rw("vp_pfn"), lsps if rb("vp_lyeof") else 9999),
             "the layer's prefill to fill", 120)
        os88qemu.pace(q, 0.5)
        print("   prefilled: %s" % state())
        q.hmp("change floppy1 %s raw" % blank)
        os88qemu.pace(q, 0.5)
        wb("vp_pfwait", b"\0")
        wait(lambda: rb("vp_ready") == 0 and rb("vp_played") == 1,
             "the play to end", 120)
        print("   the play, B: blank: %s" % state())
        got = rw("vp_lygot")
        if rw("vp_done") != r.frames or rb("vp_err"):
            bad.append("the play drew %d of %d frames (error %d)"
                       % (rw("vp_done"), r.frames, rb("vp_err")))
        if a.arm == "bank":
            if got != lrec or rw("vp_lymis"):
                bad.append("the play drew %d of %d layer records, %d missed: "
                           "the bank did not carry it" % (got, lrec,
                                                           rw("vp_lymis")))
        elif got >= lrec:
            bad.append("with no bank the play still drew all %d layer "
                       "records after the swap - the swap does not bite"
                       % got)
        q.close()
    if bad:
        print("\nvidlybank (%s): FAIL" % a.arm)
        for x in bad:
            print("  - " + x)
        return 1
    print("\nvidlybank (%s): ok" % a.arm)
    return 0


if __name__ == "__main__":
    sys.exit(main())
