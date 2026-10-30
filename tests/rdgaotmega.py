#!/usr/bin/env python3
"""A PRECOMPILED cartridge on kern_mega (docs/REALDMG-SPEC.md section 20.15).

    python3 tests/rdgaotmega.py                 the row
    python3 tests/rdgaotmega.py --leg embed     one leg (shipped, embed,
                                                beside, oneclaim)
    python3 tests/rdgaotmega.py --rom R.GB      another cartridge (a commercial
                                                one is the owner's measurement:
                                                reports, never committed)
    python3 tests/rdgaotmega.py --keep          leave the E9 logs and shots

tools/gbaot.py translates blargg's cpu_instrs whole on the host - SQUEEZED
(`--squeeze`, every image's code ends at 0210h), so a 64KB test ROM fills
ten code images with 76 links between them and leaves some blocks to the
interpreter, which is every path a commercial cartridge takes at a fraction
of its size - and packages it as `--mega`, kern_mega's build of the package
(realdmg.asm assembled RDG_AOT + PKG_MEGA). It boots `make megatrap`'s
kernel under Bochs (tests/rdgmega.py's shape: every completed #GP a line on
E9), opens the PACKAGE with no document, and reads the view:

  * cpu_instrs must PASS - all eleven - off the link port's bytes, run by the
    precompiled machine: S_JITON and S_JITFIX set, nothing translated;
  * every code image in ONE pool block (rw_axclaim): named to the core as a
    pair in S_JITXT, each based at or above 1MB and 64KB apart;
  * the cartridge's banks above 1MB in place (bank 1's window, the run's
    32KB step to bank 2), or - the `oneclaim` leg, the package built
    RW_NOXPLAN - in one claim, read a bank at a time through a window each
    (rw_aromrd), banks 16KB apart;
  * no trap whose CS is the core's or an image's code selector, and no
    kern_mega FAULT.

Legs: `shipped` - the plain package (no --mega) must REFUSE there, as 20.14
says; `embed` - the ROM inside the package; `beside` - the ROM next to it,
under the name it was made from; `oneclaim` - embed, the banks in one claim.

THE FAILING CASE (docs/WRITING-TESTS.md 1): `--image` with a package image
from before 20.15 - realdmg.asm at the commit before it (fc5e8c96),
assembled RDG_AOT + PKG_MEGA with its #error and its kern_mega refusal
taken out - is red, measured: the game never starts (nothing claimed for
the images, no view, S_JITFIX 0, an empty link port), where this tree's
image passes every leg.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88bochs  # noqa: E402
import os88build  # noqa: E402
import rdgmega

def _defs(names):
    """The view's offsets out of apps/realdmg/rdgdefs.inc, not restated"""
    v = {}
    for ln in open(os.path.join(ROOT, "apps", "realdmg", "rdgdefs.inc")):
        p = ln.split(";")[0].split()
        if len(p) == 3 and p[1] == "equ" and p[0] in names:
            v[p[0]] = int(p[2].replace("RDG_STATE+", ""), 0)
    return v


HY = _defs(("S_HYON", "S_HYN", "S_JITSEGB", "S_JITCSB"))  # noqa: E402

LIMIT = 150
REGION_ALLOW = 200
S_JITON, S_JITFIX = 0x40B7, 0x40F6
SQUEEZE = 0x210


S_FRAMES = 0x403E
GAME_SECS = 45                  # --rom: a commercial cartridge has no verdict;
                                #   its frames must keep coming this long


def leg(t, out, name, pkg, lst, rom_beside, where, game=False):
    """one boot: PKG (and the ROM beside it, or none) on B:, opened with no
    document. WHERE: "refuse", "high" (banks in place) or "one". -> fails"""
    os.makedirs(out)
    files = ["REALDMG:" + pkg]
    if rom_beside:
        files.append("REALDMG:" + rom_beside)
    disk = os.path.join(out, "aotmega.img")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"), "-o", disk,
                    "--size", "1440", "--folder", "SYSTEM/APPDATA"] + files,
                   check=True, stdout=subprocess.DEVNULL)
    viewoff = rdgmega.label_off(lst, "rdg_viewseg")
    t0 = time.time()
    box = os88bochs.Box(out, [], bimg=disk, btype="1_44", build=t.dir)
    fails, text, fix, on, xn, xlin, b1lin, coded = [], "", 0, 0, 0, [], 0, set()
    hyon, hyn, hytiers = 0, 0, []
    f0 = f1 = None
    rbase = None
    bstep = 0
    try:
        if not box.wait_for("kern_mega: protected mode", 300):
            return ["%s: kern_mega never reached protected mode" % name]
        box.up()
        box.capture()
        box.dbl(591, 105, "B:'s Disk window")
        box.dbl(150, 123, "the REALDMG folder")
        mark = os.path.getsize(box.e9)
        box.dbl(160, 139, "the package to start", lock=False)
        if where == "refuse":
            time.sleep(20)
            if box.pkg_base("RealDMG") is not None:
                fails.append("the plain precompiled package STARTED under kern_mega")
            if any(l.startswith("kern_mega FAULT") for l in box.lines()):
                fails.append("a kern_mega FAULT")
            box.shot(os.path.join(out, "end.png"))
            print("rdgaotmega: %s: %s" % (name, "refused" if not fails else "; ".join(fails)))
            return ["%s: %s" % (name, f) for f in fails]
        end = time.time() + (GAME_SECS if game else LIMIT)
        view = None
        while time.time() < end:
            time.sleep(3)
            if any(l.startswith("kern_mega FAULT") for l in box.lines()):
                break
            if view is None:
                base = box.pkg_base("RealDMG")
                if base is None:
                    continue
                rbase = base
                view = int.from_bytes(box.read(base + viewoff, 2), "little") or None
                if view is None:
                    continue
            vb = rdgmega.lin(box, view)
            fr = int.from_bytes(box.read(vb + S_FRAMES, 2), "little")
            if f0 is None:
                f0 = fr
            f1 = fr
            if game:
                continue
            n = box.read(vb + rdgmega.S_SERN, 1)[0]
            text = box.read(vb + rdgmega.RDG_SERBUF, max(n, 1))[:n].decode("latin-1")
            if "Passed" in text or "Failed" in text:
                break
        box.shot(os.path.join(out, "end.png"))
        if view is not None:
            vb = rdgmega.lin(box, view)
            on = box.read(vb + S_JITON, 1)[0]
            fix = box.read(vb + S_JITFIX, 1)[0]
            xn = box.read(vb + rdgmega.S_JITXN, 1)[0]
            for i in range(xn):
                d = int.from_bytes(box.read(vb + rdgmega.S_JITXT + 4 * i, 2), "little")
                c = int.from_bytes(box.read(vb + rdgmega.S_JITXT + 4 * i + 2, 2), "little")
                xlin.append(rdgmega.lin(box, d) if d else 0)
                if c:
                    coded.add(c)
            # the misses translated (REALDMG-SPEC 20.16): the runtime tiers
            # - the second cache segment, then the S_JITXT pairs ahead of
            # the images, 64KB apart in a pool block of their own - are not
            # images; what the squeezed images left must be made there
            hyon = box.read(vb + HY["S_HYON"], 1)[0]
            if hyon:
                r0 = int.from_bytes(box.read(vb + HY["S_JITSEGB"], 2), "little")
                r0lin = rdgmega.lin(box, r0)
                k = 0
                while k < len(xlin) and xlin[k] == r0lin + 0x10000 * (k + 1):
                    k += 1
                hytiers, xlin = [r0lin] + xlin[:k], xlin[k:]
                xn -= k
                hyn = int.from_bytes(box.read(vb + HY["S_HYN"], 2), "little")
                c = int.from_bytes(box.read(vb + HY["S_JITCSB"], 2), "little")
                if c:
                    coded.add(c)
            w = int.from_bytes(box.read(vb + rdgmega.S_DISP + 2, 2), "little")
            if w:
                coded.add(w)
            b1 = int.from_bytes(box.read(vb + rdgmega.S_BTAB1, 2), "little")
            b1lin = rdgmega.lin(box, b1) if b1 else 0
            b2 = int.from_bytes(box.read(vb + rdgmega.S_BTAB1 + 2, 2), "little")
            bstep = (rdgmega.lin(box, b2) - b1lin) if b1 and b2 else 0
    finally:
        box.stop()
    with open(box.e9, "rb") as f:
        f.seek(mark)
        tail = f.read().decode("latin-1").splitlines()
    for l in tail:
        if l.startswith("kern_mega FAULT"):
            fails.append("a kern_mega FAULT: %s" % l)
    traps = [(int(m.group(1), 16), l) for l in tail for m in [rdgmega.TRAP.match(l)] if m]
    other = [l for cs, l in traps if cs in coded]
    region = [l for cs, l in traps if cs not in coded]
    print("rdgaotmega: %s: serial %r" % (name, " ".join(text.split())))
    print("rdgaotmega: %s: the region at linear %06X" % (name, rbase or 0))
    print("rdgaotmega: %s: JITON %d JITFIX %d; %d images at %s; bank 1 at linear %06X"
          % (name, on, fix, xn, " ".join("%06X" % x for x in xlin[:4]) +
             (" ..." if len(xlin) > 4 else ""), b1lin))
    print("rdgaotmega: %s: %d traps after the launch - %d the region's and the kernel's, "
          "%d the core's or an image's; %.0fs under Bochs"
          % (name, len(traps), len(region), len(other), time.time() - t0))
    if game:
        adv = ((f1 - f0) & 0xFFFF) if f0 is not None else 0
        print("rdgaotmega: %s: %d frames over the run" % (name, adv))
        if adv < 100:
            fails.append("the game ran %d frames - it stopped" % adv)
    elif "Passed all tests" not in text:
        fails.append("cpu_instrs did not pass, precompiled, in %ds (its link port said %r)"
                     % (LIMIT, text[-60:]))
    if not (on and fix):
        fails.append("not the precompiled machine: S_JITON %d, S_JITFIX %d" % (on, fix))
    print("rdgaotmega: %s: misses translated %s" % (name, "OFF" if not hyon else
          "(S_HYON %d): %d runtime tiers at %s, %d blocks made" % (
              hyon, len(hytiers), " ".join("%06X" % x for x in hytiers), hyn)))
    if not hyon:
        fails.append("the misses are not translated (REALDMG-SPEC 20.16): S_HYON 0")
    elif not game and hyn == 0:
        fails.append("the squeezed images' misses made no block at run time")
    if xn < (1 if game else 2):
        fails.append("%d code images named to the core" % xn)
    if (rbase or 0) < 0x100000:          # REALDMG-SPEC 16.2.7: OS88_HIGH
        fails.append("the package's region is at linear %06X, below 1MB: the "
                     "--mega package declares OS88_HIGH" % (rbase or 0))
    if any(x < 0x100000 for x in xlin):
        fails.append("a code image is based below 1MB: %s" % " ".join("%06X" % x for x in xlin))
    if any(b - a != 0x10000 for a, b in zip(xlin, xlin[1:])):
        fails.append("the code images are not 64KB apart in one pool block: %s"
                     % " ".join("%06X" % x for x in xlin))
    if where == "high" and (b1lin < 0x100000 or bstep != 0x8000):
        fails.append("bank 1 at linear %06X, bank 2 %X past it: the banks were not "
                     "put in place (above 1MB, the run's 32KB step)" % (b1lin, bstep))
    if where == "one" and bstep != 0x4000:     # (one claim: a window a bank,
        fails.append("banks 1 and 2 are %X apart, not 16KB: not one claim"   # 16KB
                     % bstep)                  #   apart; the claim may be high)
    if other:
        fails.append("%d traps charged to the core or an image, the first `%s`"
                     % (len(other), other[0]))
    if len(region) > REGION_ALLOW:
        fails.append("%d traps outside the core and the images (%d allowed)"
                     % (len(region), REGION_ALLOW))
    return ["%s: %s" % (name, f) for f in fails]


def image(t, sub, defines):
    """the package image (realdmg.asm, RDG_AOT + DEFINES) into the private
    tree, and its listing"""
    d = os.path.join(t.dir, sub)
    os.makedirs(d, exist_ok=True)
    out = os.path.join(d, "realdmgaot.bin")
    subprocess.check_call(["nasm", "-f", "bin", "-w+error", "-DRDG_AOT"] +
                          ["-D" + x for x in defines] +
                          ["-I", "apps/", "-I", "apps/realdmg/", "-I", "drivers/sound/",
                           "-I", os.path.join(t.dir, "gbaot") + "/",
                           "-l", out[:-4] + ".lst", "-o", out, "apps/realdmg/realdmg.asm"],
                          cwd=ROOT)
    return out, out[:-4] + ".lst"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--leg", choices=("shipped", "embed", "beside", "oneclaim"),
                    action="append")
    ap.add_argument("--rom", help="another cartridge: its frames must keep coming in place "
                    "of cpu_instrs' verdict, and the rest is asserted as the row does")
    ap.add_argument("--image", help="a package image to run in place of this tree's "
                    "PKG_MEGA one (the failing case)")
    ap.add_argument("--squeeze", type=lambda x: int(x, 0), default=None,
                    help="images' code end (default 0210h for cpu_instrs, none for --rom)")
    a = ap.parse_args()
    import gbaot
    import gbstatic as G
    if a.rom:
        rom_path = a.rom
    else:
        subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "getgbtest.py")],
                              stdout=subprocess.DEVNULL)
        import getgbtest
        rom_path = os.path.join(getgbtest.DEST, "cpu_instrs", "cpu_instrs.gb")
    squeeze = a.squeeze if a.squeeze is not None else (0 if a.rom else SQUEEZE)
    t = os88build.tree("MEGATRAP=1", targets=(
        "mega.img", "megarun.o88", "mbr.bin", "rdgcore0.bin", "rdgcore1.bin"))
    out = os.path.join(t.dir, "rdgaotmega")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    data = open(rom_path, "rb").read()
    rom = G.Rom(data)
    found, extra = gbaot.walk(rom)
    lead = gbaot.leaders(rom, found, extra)
    img = gbaot.translate(t.dir, rom, lead, log=lambda *x: None, squeeze=squeeze)
    built = os.path.join(out, "built")
    gbaot.save(img, built, rom)
    romname = "CPUINSTR.GB" if not a.rom else gbaot.rom_name(a.rom)
    say = lambda s: print("rdgaotmega: " + s)
    mega, megalst = (a.image, a.image[:-4] + ".lst") if a.image else image(t, "aotmega", ["PKG_MEGA"])
    one, onelst = image(t, "aotone", ["PKG_MEGA", "RW_NOXPLAN"])
    plain, plainlst = image(t, "aotplain", [])
    pk = {}
    for nm, im, emb in (("shipped", plain, True), ("embed", mega, True),
                        ("beside", mega, False), ("oneclaim", one, True)):
        if a.leg and nm not in a.leg:
            continue
        if emb and len(data) >= 0xF0000:
            print("rdgaotmega: %s: a %dKB ROM cannot be inside a package; skipped"
                  % (nm, len(data) // 1024))
            continue
        p = os.path.join(out, nm, "CPUAOT.O88")
        os.makedirs(os.path.dirname(p))
        gbaot.package(t.dir, gbaot.load(built), rom, p, romname, image=im, log=say,
                      embed=emb, mega=(nm != "shipped"))
        pk[nm] = p
    fails = []
    lsts = {"shipped": plainlst, "embed": megalst, "beside": megalst, "oneclaim": onelst}
    wh = {"shipped": "refuse", "embed": "high", "beside": "high", "oneclaim": "one"}
    for nm, p in pk.items():
        beside = None
        if nm == "beside":
            beside = os.path.join(out, nm, romname)
            shutil.copy(rom_path, beside)
        fails += leg(t, os.path.join(out, nm, "run"), nm, p, lsts[nm], beside, wh[nm],
                     game=bool(a.rom))
    for f in fails:
        print("FAIL: " + f)
    if not fails and not a.keep:
        shutil.rmtree(out, ignore_errors=True)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
