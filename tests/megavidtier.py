#!/usr/bin/env python3
"""The two tiers beside the bank in place (SPEC.md 98.3.18.18), on kern_mega.

    python3 tests/megavidtier.py              the row
    python3 tests/megavidtier.py --keep       ...and leave the E9 log and shots
    python3 tests/megavidtier.py --pkg F.o88  play another VIDEO.O88

Under protected mode the Video Player reads its XMS bank in place through
OSAPI_XMEM_RUN's selectors (98.3.18.16), and the conventional ring beside it
is a second TIER: a chunk read straight off the disk into it is decoded
through a SELECTOR of the ring's (vp_tcaddr), and at a switch a seam is
copied into the tier being left, down out of the bank or up into it
(vp_tseam). This is the kern_mega arm of tests/vidxip.py --arm banktier: the
arm assembled VP_TTEST=3, which switches every three chunks wherever both
tiers can take it, so the seams are many rather than two a play.

The clip is vidxip's bank one (400 frames of lin80, 1.3 MB, silent) on a
1.44MB B: beside VIDEO.O88, and the machine Bochs at 2MB: too little XMS to
hold the file, so it is BANKED. One play with Space, full screen, to its end.
It must end with every frame drawn ([vp_done] = the clip's frames), no
kern_mega FAULT, the tier on and at least four seams counted ([vp_tsw]) and
chunks read straight into the conventional tier ([vp_tcn]).

THE FAILING CASE (docs/WRITING-TESTS.md 1): the seam's copy taken out of
vp_tseam (its `or cx, cx / jnz .go` made a `jmp .out`) - on kern_big the same
build wedges at the first hold that reads across a switch (vidxip's banktier
times out), here the play stops short of its last frame or faults. --pkg runs
such a build.

It reads build/ (wants=) and writes only under build/bochs-megavidtier/.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88bochs  # noqa: E402
import vidxip  # noqa: E402

BUILD = os.path.join(ROOT, "build")
NF = 400
PLAY = 60                       # 400 frames at 24 fps, and its settle


def build_pkg(tmp):
    """The kern_mega arm assembled VP_TTEST=3, with its four parts"""
    b = os.path.join(tmp, "video.bin")
    subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                    "-DPKG_MEGA", "-DVP_TTEST=3",
                    "--before", "[map all %s]" % os.path.join(tmp, "video.map"),
                    "-o", b, "apps/video/video.asm"], check=True, cwd=ROOT)
    fx = os.path.join(tmp, "vfx.bin")
    subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                    "-DPKG_MEGA", "-o", fx, "apps/video/vfx.asm"],
                   check=True, cwd=ROOT)
    # ...and its COLD PART, linked to THIS assembly's map (SPEC.md 98.3.24.1)
    lk, vc = os.path.join(tmp, "vclink.inc"), os.path.join(tmp, "vcold.bin")
    subprocess.run([sys.executable, "tools/os88plink.py", b,
                    os.path.join(tmp, "video.map"), "apps/video/vcold.asm",
                    "-o", lk], check=True, cwd=ROOT)
    subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/", "-I",
                    tmp + "/", "-DPKG_MEGA", "-o", vc,
                    "apps/video/vcold.asm"], check=True, cwd=ROOT)
    o = os.path.join(tmp, "VIDEO.O88")
    subprocess.run([sys.executable, "tools/os88pkg.py", b, "-o", o,
                    "--part", os.path.join(BUILD, "vblk.bin"),
                    "--part", os.path.join(BUILD, "vblkpm.bin"),
                    "--part", fx, "--part", vc], check=True,
                   capture_output=True, cwd=ROOT)
    return o, os.path.join(tmp, "video.map")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--pkg", help="the VIDEO.O88 to play, with --map")
    ap.add_argument("--map", help="its NASM map (for [vp_tsw] and the rest)")
    a = ap.parse_args()
    out = os.path.join(BUILD, "bochs-megavidtier")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    tmp = tempfile.mkdtemp(prefix="megavidtier-")
    t0 = time.time()
    fails = []
    try:
        vidxip.NF = NF
        v88 = vidxip.clip(tmp, False)
        if a.pkg:
            stage, vmap = os.path.join(tmp, "VIDEO.O88"), a.map
            shutil.copy(a.pkg, stage)
        else:
            stage, vmap = build_pkg(tmp)
        disk = os.path.join(out, "tier.img")
        subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                     "os88disk.py"),
                        "-o", disk, "--size", "1440", stage, v88],
                       check=True, stdout=subprocess.DEVNULL)
        box = os88bochs.Box(out, [], bimg=disk, btype="1_44", megs=2)

        def rd(label, n=2):
            v = box.pkg_read("Video", label, n, vmap)
            return v[0] if n == 1 else v[0] | (v[1] << 8)

        def fault():
            return [l for l in box.faults() if "kern_mega FAULT" in l]
        try:
            if not box.wait_pm(300):
                fails.append("kern_mega never reached protected mode")
                return 1
            box.up()
            box.capture()
            box.dbl(591, 105, "B:'s Disk window")
            box.dbl(170, 123, "CLIP.V88's open")
            box.xd("key", "space")
            tier = [0]

            def over():
                if rd("vp_ttwo", 1):
                    tier[0] = 1
                return fault() or rd("vp_played", 1)
            box.until(over, "the play to end", PLAY + 120, poll=0.5)
            box.shot(os.path.join(out, "play.png"))
            f = fault()
            done, tsw, tcn = rd("vp_done"), rd("vp_tsw"), rd("vp_tcn")
            print("megavidtier: done %d of %d, tiers %s, %d seams, %d chunks "
                  "straight into the conventional tier"
                  % (done, NF, "ON" if tier[0] else "off", tsw, tcn))
            if f:
                fails.append(f[0])
            if done != NF:
                fails.append("the play stopped at frame %d of %d" % (done, NF))
            if not tier[0] or tsw < 4 or not tcn:
                fails.append("the two tiers: %s, %d seams, %d chunks straight "
                             "in - the row tests nothing"
                             % ("on" if tier[0] else "OFF", tsw, tcn))
        finally:
            box.stop()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("megavidtier: %.0fs under Bochs" % (time.time() - t0))
    for f in fails:
        print("FAIL: " + f)
    if not fails and not a.keep:
        shutil.rmtree(out, ignore_errors=True)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
