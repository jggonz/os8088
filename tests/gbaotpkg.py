#!/usr/bin/env python3
"""A precompiled cartridge on the machine: GAME.O88 (REALDMG-SPEC 20.10).

    python3 tests/gbaotpkg.py [--rom ROM] [--machine M] [--secs S] [--keep]

tools/gbaot.py translates the ROM whole on the host and packages it; this
boots os8088 on MartyPC with the package, the ROM beside it and the shipped
REALDMG.O88, and opens the PACKAGE - no document: it opens the ROM it was
made from by itself. Then:

  * the machine is the precompiled one: S_JITON and S_JITFIX set, nothing
    translated (S_JITNBR 0), EVERY IMAGE CLAIMED READ - on a volume of 4KB
    clusters, a hard disk's, where the first package's reads were all
    refused - and the worker found running IN THE CODE IMAGES when it is
    sampled (the core alone is the interpreter);
  * and the same with the ROM INSIDE the package (--embed, REALDMG-SPEC
    20.13), opened with nothing beside it;
  * the game's frames a guest second, against REALDMG.O88's own recompiler
    on the same ROM, reported.

The default ROM is ours, GBPOP.GB (tools/gbours.py): its busy code is in
ROM. GBRMW.GB runs its loop from WRAM, which only the dynamic recompiler
translates - a precompiled cartridge interprets RAM (REALDMG-SPEC 20.5). --rom takes any, and
nothing of it is kept: a commercial cartridge's package is the owner's
measurement and is never committed.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88marty as M  # noqa: E402
import os88ui  # noqa: E402
import gbwindow  # noqa: E402
import gbaot  # noqa: E402
import gbstatic  # noqa: E402

CLOCK = 4772727.0
S_JITON, S_JITFIX, S_JITNBR = 0x4000 + 0xB7, 0x4000 + 0xF6, 0x4000 + 0xFE


def equ_off(lst, name):
    """a bss word's offset (an equ, so not a label): off the first store to it"""
    import re
    for line in open(lst, errors="replace"):
        r = re.match(r"\s*\d+\s+[0-9A-F]{8}\s+[0-9A-F]+\[([0-9A-F]{4})\]\s+(?:<\d+>\s+)?"
                     r"mov \[%s \+ bx\]" % name, line)
        if r:
            return int.from_bytes(bytes.fromhex(r.group(1)), "little")
    sys.exit("gbaotpkg: %s not in %s" % (name, lst))


def rd16(m, a):
    return int.from_bytes(m.read(a, 2), "little")


def rate(m, view, secs):
    f0, c0 = gbwindow.frames(m, view), int(m.status()["cycles"])
    M.guest_sleep(m, secs)
    f1, c1 = gbwindow.frames(m, view), int(m.status()["cycles"])
    return (f1 - f0) / ((c1 - c0) / CLOCK)


def started(m, name, viewoff):
    seg = view = None
    for _ in range(90):
        M.guest_sleep(m, 1)
        seg = seg or gbwindow.find_seg(m, name, viewoff)
        if seg:
            view = rd16(m, seg * 16 + viewoff)
        if view and gbwindow.frames(m, view) > 30:
            return seg, view
    return None, None


def _rwa_max():
    """realdmg.asm's RWA_MAX - rw_aseg's words, rw_anseg after them - read
    from the source rather than restated (it was 14, a stale 14 here)"""
    import re
    src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "apps",
                            "realdmg", "realdmg.asm")).read()
    return int(re.search(r"^RWA_MAX\s+equ\s+(\d+)", src, re.M).group(1))


RWA_MAX = _rwa_max()


def leg(m_args, disk, stem, nseg, lst, viewoff, asegoff, fails, label):
    """open B:/STEM.O88 and read what the package made of it: (seg, view,
    images claimed, images in memory, samples in them of 200), or None"""
    with os88ui.boot(*m_args[0], apps=disk, machine=m_args[1]) as ui:
        m = ui.m
        ui.path("B:/%s.O88" % stem)
        seg, view = started(m, b"REALDMG", viewoff)
        if not view:
            fails.append("%s: the package never started its game" % label)
            return None
        on, fix = m.read(view * 16 + S_JITON, 1)[0], m.read(view * 16 + S_JITFIX, 1)[0]
        bss = m.read(seg * 16 + asegoff, 2 * RWA_MAX + 1)
        claimed = bss[2 * RWA_MAX]           # rw_anseg, after rw_aseg's words
        images = [int.from_bytes(bss[2 * i:2 * i + 2], "little") for i in range(nseg)]
        here = [x for x in images if x]
        inimg = 0
        for _ in range(200):
            m.pause()
            cs = m.regs()["cs"]
            m.run()
            inimg += cs in here
            M.guest_sleep(m, 0.02)
        fps = rate(m, view, 10)
        nbr = rd16(m, view * 16 + S_JITNBR)
        print("gbaotpkg %s: S_JITON %d S_JITFIX %d, %d translated; %d of %d images claimed are "
              "in memory; %d of 200 samples in them; %.2f frames a guest second" % (
                  label, on, fix, nbr, len(here), claimed, inimg, fps))
        if not (on == 1 and fix == 1):
            fails.append("%s: not the precompiled machine (S_JITON %d, S_JITFIX %d)" % (label, on, fix))
        if nbr:
            fails.append("%s: %d blocks were translated while running" % (label, nbr))
        if len(here) != claimed:
            fails.append("%s: %d images claimed and only %d read - a read the volume refused "
                         "(REALDMG-SPEC 20.10: whole clusters)" % (label, claimed, len(here)))
        if here and not inimg:
            fails.append("%s: the worker was never found in the code images" % label)
        return fps


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--rom")
    ap.add_argument("--machine", default="os8088_5150_herc_sb_720_gla")
    ap.add_argument("--secs", type=float, default=20)
    ap.add_argument("--keep", action="store_true", help="keep the work directory")
    a = ap.parse_args()
    if a.rom:
        rom_path = a.rom
    else:
        import gbours
        rom_path = gbours.build("gbpop")
    tmp = tempfile.mkdtemp(prefix="gbaotpkg-")
    romname = gbaot.rom_name(rom_path)
    stem = romname.rsplit(".", 1)[0]
    gb = os.path.join(tmp, romname)
    shutil.copy(rom_path, gb)
    rom = gbstatic.Rom(open(gb, "rb").read())
    found, extra = gbaot.walk(rom)
    img = gbaot.translate(os.path.join(ROOT, "build"), rom, gbaot.leaders(rom, found, extra),
                          log=lambda *x: None)
    d = os.path.join(tmp, "aot")
    gbaot.save(img, d, rom)
    pkg = os.path.join(tmp, stem + ".O88")
    gbaot.package(os.path.join(ROOT, "build"), gbaot.load(d), rom, pkg, romname)
    disk = os.path.join(tmp, "apps.img")
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"), "-o", disk,
                           "--size", "720", os.path.join(ROOT, "build", "realdmg.o88"), pkg, gb],
                          stdout=subprocess.DEVNULL)
    epkg = os.path.join(tmp, "EMB.O88")       # ...and with the ROM inside it
    gbaot.package(os.path.join(ROOT, "build"), gbaot.load(d), rom, epkg, romname, embed=True)
    # THE VOLUME HAS 4KB CLUSTERS: OSAPI_FILE_READ_AT reads whole clusters
    # only, a part starts on a 512-byte boundary, and the first package read
    # in place - every floppy here clean, every hard disk interpreted
    disk = os.path.join(tmp, "apps.img")
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"), "-o", disk,
                           "--size", "720", "--spc", "8", os.path.join(ROOT, "build", "realdmg.o88"),
                           pkg, gb, epkg], stdout=subprocess.DEVNULL)
    lst = os.path.join(ROOT, "build", "gbaot", "realdmgaot.lst")
    viewoff = gbwindow.label_off(lst, "rdg_viewseg")
    asegoff = equ_off(lst, "rw_aseg")
    fails = []
    nseg = len(img["segs"]) - 1
    margs = ((os.path.join(ROOT, "build", "os8088-720.img"),), a.machine)
    aot = leg(margs, disk, stem, nseg, lst, viewoff, asegoff, fails, "the ROM beside it")
    leg(margs, disk, "EMB", nseg, lst, viewoff, asegoff, fails, "the ROM inside it")
    with os88ui.boot(os.path.join(ROOT, "build", "os8088-720.img"), apps=disk,
                     machine=a.machine) as ui:
        m = ui.m
        ui.path("B:/" + romname)
        viewoff0 = gbwindow.label_off(os.path.join(ROOT, "build", "realdmg.lst"), "rdg_viewseg")
        seg, view = started(m, b"REALDMG", viewoff0)
        if view and aot:
            M.guest_sleep(m, 10)
            dyn = rate(m, view, 10)
            print("gbaotpkg: REALDMG.O88 on %s: %.2f frames a guest second (precompiled %.2f, x%.2f)"
                  % (romname, dyn, aot, aot / dyn if dyn else 0))
    if a.keep:
        print("gbaotpkg: kept " + tmp)
    else:
        shutil.rmtree(tmp)
    for f in fails:
        print("FAIL gbaotpkg: " + f)
    print("gbaotpkg: %s" % ("PASS" if not fails else "FAIL"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
