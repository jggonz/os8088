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

ON KERN_MEGA (tests/megaport.txt's `mega-gbship`, OS88_MEGA): B reads
build/megaapps.img and megaapps120.img, and C boots kern_mega's pair -
mega.img and megaapps.img, which carry REALDMG's ARM (SPEC.md 20.18) and
the same packed MEDIA/GBDEMO.GB. That is the leg the bug was on: the arm
puts every cartridge of 32KB or more IN PLACE above 1MB (REALDMG-SPEC
10.3.1), read a bank at a time by OSAPI_FILE_READ_SEQ, which is RAW (SPEC.md
20.14.3), so the shipped demo came in as 1,095 bytes of LZ4 and RealDMG
said "not a GB ROM" while kern_big played it (KERN-MEGA-PLAN 14.147). RED,
measured: `--apps` a copy of megaapps.img with the arm from before
rw_czprobe - RealDMG starts, the cartridge never runs.

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
MEGA = bool(os.environ.get("OS88_MEGA"))
if MEGA:
    SHIPPED = [("megaapps.img", "MEDIA/GBDEMO.GB"),
               ("megaapps120.img", "MEDIA/GBDEMO.GB")]

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
    ap.add_argument("--apps", help="C boots this apps disk instead of the "
                    "shipped one (the red leg: a disk with another REALDMG.O88)")
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
    if MEGA:                            # kern_mega's pair, and the ARM's map
        sysimg, apps = "os8088.img", "apps.img"     # (os88megacommon swaps
        lst = os.path.join(ROOT, "build", "megak", "apps", "realdmg.lst")
    else:                                           #  both for mega's own)
        sysimg, apps = "os8088-360.img", "emu360.img"
        lst = os.path.join(ROOT, "build", "realdmg.lst")
    apps = a.apps or os.path.join(ROOT, "build", apps)
    viewoff = gbwindow.label_off(lst, "rdg_viewseg")
    with os88ui.boot(os.path.join(ROOT, "build", sysimg), apps=apps,
                     machine=a.machine) as ui:
        m = ui.m
        ui.path("B:/MEDIA/GBDEMO.GB")
        seg = view = None
        f = was = 0
        left = a.secs
        while left > 0:
            M.guest_sleep(m, 1)
            left -= 1
            if MEGA:                    # the package and its view are LDT
                import dispapps         #   selectors (tests/gbsave.py)
                got = dispapps.pkg_seg(m, 0)
                seg = got and got[1]
                view = seg and int.from_bytes(m.readseg(seg, viewoff, 2), "little")
                was = f
                f = view and int.from_bytes(
                    m.readseg(view, gbwindow.S_FRAMES, 2), "little") or 0
                # RUNNING, not a number: a refused cartridge leaves the view
                # claimed and its counter whatever the claim held (the red
                # leg read 39,810 there), so it must ADVANCE between two
                # looks a guest second apart, at a Game Boy's rate or under
                if f > 30 and was and 0 < f - was <= 120:
                    break
                continue
            seg = seg or gbwindow.find_seg(m, b"REALDMG", viewoff)
            if seg:
                view = int.from_bytes(m.read(seg * 16 + viewoff, 2), "little")
            if view:
                f = gbwindow.frames(m, view)
                if f > 30:
                    break
        if not seg:
            fail("RealDMG never started from B:/MEDIA/GBDEMO.GB")
        elif not view or f <= 30 or (MEGA and not (was and 0 < f - was <= 120)):
            fail("RealDMG started but the packed cartridge never ran "
                 "(%d game frames in %.0f guest seconds)" % (f, a.secs))
        else:
            print("C  %s: B:/MEDIA/GBDEMO.GB opened in RealDMG, %d game frames"
                  % (os.path.basename(apps), f))
    print("gbship: %s" % ("PASS" if not fails else "FAIL"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
