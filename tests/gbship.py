#!/usr/bin/env python3
"""RealDMG's SHIPPED cartridge: the committed copy, the packed copy, and the
one a user actually opens.

    python3 tests/gbship.py [--machine M] [--secs S]

SPEC.md 24.6's emu360.img and the 1.44MB and 1.2MB apps disks carry
REALDMG.O88 and its demo cartridge, MEDIA/GBDEMO.GB, LZ4-packed
(docs/REALDMG-SPEC.md section 19). Three things can be wrong with that and
no other row sees any of them, because every other RealDMG row assembles a
ROM of its own and puts it on a scratch disk PLAIN:

  A. THE COMMITTED COPY IS STALE. apps/realdmg/GBDEMO.GB is committed so
     that `make` never needs rgbds (it is fetched from GitHub and compiled),
     which is only honest while it is what tools/gbours.py builds from
     tools/gbprof/gbdemo.asm. So it is rebuilt here and compared, byte for
     byte - an edit to the demo that nobody re-committed is red.
  B. THE PACKED COPY IS NOT THAT CARTRIDGE. Every shipped disk's
     MEDIA/GBDEMO.GB is read off the volume by t_image's own FAT walker,
     must be a 'CZ' file, and must unwrap to exactly the committed bytes.
  C. REALDMG CANNOT READ IT. The packed file is only a cartridge if the
     package's read goes through the kernel's transparent expansion
     (SPEC.md 20.14.3): a read that bypassed it would hand rdg_init 1,110
     bytes of LZ4 and a bad header. So the SHIPPED pair boots on MartyPC's
     5150 - os8088-360.img in A:, emu360.img in B: - the cartridge is
     opened by double-click, which ASSOC.DAT gives to RealDMG, and the game's
     own frame counter must run. Break it on purpose by forcing rw_load onto
     the bank cache's raw READ_SEQ path and the package refuses the file.

SOAK: one package, and it needs rgbds for A.
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
sys.path.insert(0, os.path.join(ROOT, "tests", "unit"))
import os88lz  # noqa: E402
import os88marty as M  # noqa: E402
import os88ui  # noqa: E402
import gbwindow  # noqa: E402
from t_image import Vol  # noqa: E402

COMMITTED = os.path.join(ROOT, "apps", "realdmg", "GBDEMO.GB")
# every shipped disk that carries the cartridge, and the path it is at
SHIPPED = [("emu360.img", "MEDIA/GBDEMO.GB"),
           ("apps.img", "MEDIA/GBDEMO.GB"),
           ("apps120.img", "MEDIA/GBDEMO.GB")]

fails = []


def fail(why):
    fails.append(why)
    print("FAIL gbship: " + why)


def volfile(img, want):
    with open(img, "rb") as f:
        blob = f.read()
    v = Vol(blob, img)
    for path, name11, attr, clus, size in v.walk():
        stem = name11[:8].decode("ascii", "replace").strip()
        ext = name11[8:11].decode("ascii", "replace").strip()
        if path + stem + "." + ext == want:
            chain, _ = v.chain(clus)
            out = b"".join(blob[v.cluster_lba(c) * v.byts:
                                (v.cluster_lba(c) + v.spc) * v.byts] for c in chain)
            return out[:size]
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--secs", type=float, default=60,
                    help="guest seconds to wait for the game to start")
    a = ap.parse_args()
    with open(COMMITTED, "rb") as f:
        want = f.read()

    # --- A: the committed copy is what the source builds ---------------------
    rom = subprocess.check_output([sys.executable, os.path.join(ROOT, "tools", "gbours.py"),
                                   "gbdemo"], text=True).strip().splitlines()[-1]
    with open(rom, "rb") as f:
        built = f.read()
    if built != want:
        fail("apps/realdmg/GBDEMO.GB is not what tools/gbours.py builds now "
             "(%d bytes against %d): copy %s over it and commit" % (len(want), len(built), rom))
    else:
        print("A  apps/realdmg/GBDEMO.GB = tools/gbours.py gbdemo, %d bytes" % len(want))

    # --- B: every shipped copy unwraps to it ---------------------------------
    for img, path in SHIPPED:
        got = volfile(os.path.join(ROOT, "build", img), path)
        if got is None:
            fail("%s has no %s" % (img, path))
            continue
        if not os88lz.cz_parse(got):
            fail("%s's %s is not a packed ('CZ') file" % (img, path))
            continue
        if os88lz.cz_unwrap(got) != want:
            fail("%s's %s does not unwrap to the committed cartridge" % (img, path))
            continue
        print("B  %s %s: %d bytes packed, unwraps to the committed %d" % (img, path, len(got), len(want)))

    # --- C: RealDMG plays the packed one off the shipped pair ----------------
    viewoff = gbwindow.label_off(os.path.join(ROOT, "build", "realdmg.lst"), "rdg_viewseg")
    with os88ui.boot(os.path.join(ROOT, "build", "os8088-360.img"),
                     apps=os.path.join(ROOT, "build", "emu360.img"),
                     machine=a.machine) as ui:
        m = ui.m
        ui.path("B:/MEDIA/GBDEMO.GB")
        seg = view = None
        f = 0
        left = a.secs
        while left > 0:
            M.guest_sleep(m, 1)
            left -= 1
            seg = seg or gbwindow.find_seg(m, b"REALDMG", viewoff)
            if seg:
                view = int.from_bytes(m.read(seg * 16 + viewoff, 2), "little")
            if view:
                f = gbwindow.frames(m, view)
                if f > 30:
                    break
        if not seg:
            fail("RealDMG never started from B:/MEDIA/GBDEMO.GB")
        elif not view or f <= 30:
            fail("RealDMG started but the packed cartridge never ran "
                 "(%d game frames in %.0f guest seconds)" % (f, a.secs))
        else:
            print("C  emu360.img: B:/MEDIA/GBDEMO.GB opened in RealDMG, %d game frames" % f)
    print("gbship: %s" % ("PASS" if not fails else "FAIL"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
